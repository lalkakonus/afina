#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace Afina {
namespace Network {
namespace MTblocking {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl), _thread_pool(this, THREAD_COUNT) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_accept, uint32_t n_workers) {
    _logger = pLogging->select("network");
    _logger->info("Start mt_blocking network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    running.store(true);
    _thread = std::thread(&ServerImpl::OnRun, this);
}

// See Server.h
void ServerImpl::Stop() {
    running.store(false);
    shutdown(_server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join() {
    assert(_thread.joinable());
    _thread.join();
    close(_server_socket);
}

Worker::Worker(ServerImpl * ptr): on_run(true), is_active(false), thread(&Worker::Process, this, ptr) {};

Worker::~Worker() {
	on_run = false;
	cv.notify_one();
	thread.join();
}

bool Worker::CheckActive() const{
	std::lock_guard<std::mutex> lock(active_mutex);
	return is_active;
}

void Worker::Process(ServerImpl * ptr) {
	while (on_run) {
		std::unique_lock<std::mutex> lock(active_mutex);
		cv.wait(lock, [&]{return is_active || !on_run;});
		if (!on_run) {
			break;
		}
		lock.unlock();
		ptr->ProcessThread(_socket);		
		lock.lock();
		is_active = false;
	}
}

void Worker::Start(int client_soket) {
	std::unique_lock<std::mutex> lock(active_mutex);
	is_active = true;
	_socket = client_soket;
	cv.notify_one();
}

ThreadPool::ThreadPool(ServerImpl * ptr, int max_count = 1): _max_count(max_count) {
	_workers.reserve(max_count);
	for (int i = 0; i < _max_count; i++)
		_workers.emplace_back(new Worker(ptr));
}

std::shared_ptr<Worker> ThreadPool::GetFreeWorker() {
	for (auto& ptr: _workers) {
		if (ptr->CheckActive())
			return ptr;
	}
	return nullptr;
}

bool ThreadPool::AddConnection(int client_socket) {
	if (auto it=GetFreeWorker()) {
		it->Start(client_socket);	
		return true;
	}
	return false;
}

// See Server.h
void ServerImpl::OnRun() {
    // Here is connection state
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument

	while (running.load()) {
        _logger->debug("waiting for connection...");

        // The call to accept() blocks until the incoming connection arrives
        int client_socket;
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        if ((client_socket = accept(_server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            continue;
        }

        // Got new connection
        if (_logger->should_log(spdlog::level::debug)) {
            std::string host = "unknown", port = "-1";

            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
            if (getnameinfo(&client_addr, client_addr_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                host = hbuf;
                port = sbuf;
            }
            _logger->debug("Accepted connection on descriptor {} (host={}, port={})\n", client_socket, host, port);
        }

        // Configure read timeout
        {
            struct timeval tv;
            tv.tv_sec = 5; // TODO: make it configurable
            tv.tv_usec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        }

        // TODO: Start new thread and process data from/to connection
    	if (!_thread_pool.AddConnection(client_socket)) {
			close(client_socket);
		}
	}

    // Cleanup on exit...
    _logger->warn("Network stopped");
}

void ServerImpl::ProcessThread(int client_socket) {
	
	// Process new connection:
	// - read commands until socket alive
	// - execute each command
	// - send response
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
	
	try {
		int readed_bytes = -1;
		char client_buffer[4096];
		while ((readed_bytes = read(client_socket, client_buffer, sizeof(client_buffer))) > 0) {
			_logger->debug("Got {} bytes from socket", readed_bytes);

			// Single block of data readed from the socket could 
			// trigger inside actions a multiple times,
			// for example:
			// - read#0: [<command1 start>]
			// - read#1: [<command1 end> <argument> <command2> 
			// <argument for command 2> <command3> ... ]
			while (readed_bytes > 0) {
				_logger->debug("Process {} bytes", readed_bytes);
				// There is no command yet
				if (!command_to_execute) {
					std::size_t parsed = 0;
					if (parser.Parse(client_buffer, readed_bytes, parsed)) {
						// There is no command to be launched, continue to parse input stream
						// Here we are, current chunk finished some command, process it
						_logger->debug("Found new command: {} in {} bytes", 
										parser.Name(), parsed);
						command_to_execute = parser.Build(arg_remains);
						if (arg_remains > 0) {
							arg_remains += 2;
						}
					}

					// Parsed might fails to consume any bytes from input stream. 
					// In real life that could happens,
					// for example, because we are working with UTF-16 chars and only 1 byte 
					// left in stream
					if (parsed == 0) {
						break;
					} else {
						std::memmove(client_buffer, client_buffer + parsed, 
									 readed_bytes - parsed);
						readed_bytes -= parsed;
					}
				}

				// There is command, but we still wait for argument to arrive...
				if (command_to_execute && arg_remains > 0) {
					_logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
					// There is some parsed command, and now we are reading argument
					std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
					argument_for_command.append(client_buffer, to_read);

					std::memmove(client_buffer, client_buffer + to_read,
								 readed_bytes - to_read);
					arg_remains -= to_read;
					readed_bytes -= to_read;
				}

				// Thre is command & argument - RUN!
				if (command_to_execute && arg_remains == 0) {
					_logger->debug("Start command execution");

					std::string result;
					command_to_execute->Execute(*pStorage, argument_for_command, result);

					// Send response
					if (send(client_socket, result.data(), result.size(), 0) <= 0) {
						throw std::runtime_error("Failed to send response");
					}

					// Prepare for the next command
					command_to_execute.reset();
					argument_for_command.resize(0);
					parser.Reset();
				}
			} 
		}

		if (readed_bytes == 0) {
			_logger->debug("Connection closed");
		} else {
			throw std::runtime_error(std::string(strerror(errno)));
		}
	} catch (std::runtime_error &ex) {
		_logger->error("Failed to process connection on descriptor {}: {}",
					   client_socket, ex.what());
	}

	// We are done with this connection
	close(client_socket);
}

} // namespace MTblocking
} // namespace Network
} // namespace Afina
