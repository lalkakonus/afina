#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/logging/Service.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <algorithm>
#include <stdexcept>

#include "Utils.h"

namespace Afina {
namespace Network {
namespace STnonblock {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl) {}

// See Server.h
ServerImpl::~ServerImpl() {
}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_acceptors, uint32_t n_workers) {
	_logger = pLogging->select("network");
    _logger->info("Start network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Create server socket
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket: " + std::string(strerror(errno)));
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, (SO_KEEPALIVE | SO_REUSEADDR | SO_REUSEPORT),\
				   &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed: " + std::string(strerror(errno)));
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed: " + std::string(strerror(errno)));
    }

    make_socket_non_blocking(_server_socket);
    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed: " + std::string(strerror(errno)));
    }

    _event_fd = eventfd(0, EFD_NONBLOCK);
    if (_event_fd == -1) {
        throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
    }

    _work_thread = std::thread(&ServerImpl::OnRun, this);
}

// See Server.h
void ServerImpl::Stop() {
    _logger->warn("Stop network service");

    // Wakeup threads that are sleep on epoll_wait
    if (eventfd_write(_event_fd, 1)) {
        throw std::runtime_error("Failed to wakeup workers");
    }
}

// See Server.h
void ServerImpl::Join() {
    // Wait for work to be complete
    _work_thread.join();
	if (close(_server_socket) < 0) {
		_logger->error("Failed to close server socket: " + std::string(strerror(errno)));
	}
	_logger->warn("Server stopped");
}

// See ServerImpl.h
void ServerImpl::OnRun() {
	try {
		_logger->info("Start acceptor");
		int epoll_descr = epoll_create1(0);
		if (epoll_descr == -1) {
			throw std::runtime_error("Failed to create epoll file descriptor: " + std::string(strerror(errno)));
		}

		struct epoll_event event;
		event.events = EPOLLIN;
		event.data.fd = _server_socket;
		if (epoll_ctl(epoll_descr, EPOLL_CTL_ADD, _server_socket, &event)) {
			throw std::runtime_error("Failed to add file descriptor to epoll");
		}

		struct epoll_event event2;
		event2.events = EPOLLIN;
		event2.data.fd = _event_fd;
		if (epoll_ctl(epoll_descr, EPOLL_CTL_ADD, _event_fd, &event2)) {
			throw std::runtime_error("Failed to add file descriptor to epoll");
		}

		bool run = true;
		std::array<struct epoll_event, 64> mod_list;
		while (run) {
			int nmod = epoll_wait(epoll_descr, &mod_list[0], mod_list.size(), -1);
			_logger->debug("Acceptor wokeup: {} events", nmod);

			for (int i = 0; i < nmod; i++) {
				struct epoll_event &current_event = mod_list[i];
				if (current_event.data.fd == _event_fd) {
					_logger->debug("Break acceptor due to stop signal");
					run = false;
					continue;
				} else if (current_event.data.fd == _server_socket) {
					OnNewConnection(epoll_descr);
					continue;
				}

				// That is some connection!
				auto predicate = [&current_event](const Connection &connection) { 
					return connection._event.data.fd == current_event.data.fd; 
				};

				auto connection = std::find_if(_connections.begin(), _connections.end(), predicate);

				auto old_mask = connection->_event.events;
				if (current_event.events & EPOLLERR) {
					connection->OnError();
				} else {	
					if (current_event.events & EPOLLRDHUP) {
						connection->OnClose();
					} else {
						// Depends on what connection wants...
						if (current_event.events & EPOLLIN) {
							connection->DoRead();
						}
						if (current_event.events & EPOLLOUT) {
							connection->DoWrite();
						}
					}
				}

				// Does it alive?
				if (!connection->isAlive()) {
					if (epoll_ctl(epoll_descr, EPOLL_CTL_DEL, connection->_event.data.fd, NULL)) {
						_logger->error("Failed to delete connection from epoll");
					}
					_logger->debug("Connection closed.");
					if (close(connection->_event.data.fd) == -1) {
						std::cout << "Socket setsockopt() failed: " << std::string(strerror(errno)) << std::endl;
					}
					_connections.erase(connection);
				} else if (connection->_event.events != old_mask) {
					if (epoll_ctl(epoll_descr, EPOLL_CTL_MOD, connection->_event.data.fd, &connection->_event)) {
						_logger->error("Failed to change connection event mask");
						if (close(connection->_event.data.fd) == -1) {
							std::cout << "Socket setsockopt() failed: " <<\
								std::string(strerror(errno)) << std::endl;
						}
						_connections.erase(connection);
					}
				}
			}
		}
		
		for (auto &connection: _connections) {
			if (epoll_ctl(epoll_descr, EPOLL_CTL_DEL, connection._event.data.fd, NULL)) {
				_logger->error("# Failed to delete connection from epoll");
			}
			if (close(connection._event.data.fd) == -1) {
				_logger->error("# Failed to close socket" + std::string(strerror(errno)));
			}
		}
		
		_logger->warn("Acceptor stopped");
	} catch (const std::runtime_error &error) {
		_logger->error(error.what());
	} catch (...) {
		_logger->error("Error happens");
	}

}

void ServerImpl::OnNewConnection(int epoll_descr) {
    for (;;) {
        struct sockaddr in_addr;
        socklen_t in_len;

        // No need to make these sockets non blocking since accept4() takes care of it.
        in_len = sizeof in_addr;
        int infd = accept4(_server_socket, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break; // We have processed all incoming connections.
            } else {
                _logger->error("Failed to accept socket");
                break;
            }
        }

        // Print host and service info.
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        int retval =
            getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV);
        if (retval == 0) {
            _logger->info("Accepted connection on descriptor {} (host={}, port={})\n", infd, hbuf, sbuf);
        }

        // Register the new FD to be monitored by epoll.
		_connections.emplace_back(infd, pStorage, pLogging);

        // Register connection in worker's epoll
		Connection &connection = _connections.back(); 
        connection.Start();
        if (connection.isAlive()) {
        	if (epoll_ctl(epoll_descr, EPOLL_CTL_ADD, connection._event.data.fd, &connection._event) < 0) {
                _logger->error("Connection error ;(");
				if (close(connection._event.data.fd) == -1) {
					std::cout << "Socket setsockopt() failed: " << std::string(strerror(errno)) << std::endl;
				}
				_connections.pop_back();
			}
        } 
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
