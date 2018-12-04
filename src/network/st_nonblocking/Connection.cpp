#include "Connection.h"

namespace Afina {
namespace Network {
namespace STnonblock {

Connection::~Connection() {
	std::cout << "Connection~\n";
}

Connection::Connection(int s, std::shared_ptr<Afina::Storage> pStorage, 
					   std::shared_ptr<Afina::Logging::Service> pl):
		   pLogging(pl), _pStorage(pStorage), _shift(0), _isActive(false) {
	std::cout << "Connection()\n";
	std::memset(&_event, 0, sizeof(struct epoll_event));
	_event.data.fd = s;
}

Connection::Connection(const Connection &other) = default;//{
//	std::cout << "Connection &\n";
//};

Connection& Connection::operator=(const Connection &other) = default;//{
//	std::cout << "operator =\n";
//};

//Connection::Connection(Connection &&other) {
//	std::cout << "Connection &&\n";
//};

//Connection& Connection::operator=(Connection &&other) {
//	std::cout << "opearator move\n";
//};

// See Connection.h
void Connection::Start() { 
	// Set input flags
	try {
		_logger = pLogging->select("network");
		_isActive = true;
		make_socket_non_blocking(_event.data.fd);
		_event.events = EPOLLIN | EPOLLRDHUP | EPOLLPRI;
	} catch (const std::runtime_error &error) {
		_isActive = false;
		_logger->error(error.what());
	}
}

// See Connection.h
void Connection::OnError() {
	_logger->debug("Error happens");
	_isActive = false;
}

// Connection &Connection::opeartor=(Connection &&other) {
// }

// See Connection.h
void Connection::OnClose() {
	_isActive = false;
	std::cout << "Connection closed\n"; 
}

// See Connection.h
void Connection::DoRead() {
	int readed_bytes;
	_logger->debug("Start reading");
	try {
		while ((readed_bytes = read(_event.data.fd, client_buffer + _shift, 
									 sizeof(client_buffer - _shift))) > 0) {
			readed_bytes += _shift; 
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
					_logger->debug("Fill argument: {} bytes of {}",
									readed_bytes, arg_remains);
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
					command_to_execute->Execute(*_pStorage, argument_for_command, result);
					result += "\r\n";

					_response.push_back(result);
					_event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI;

					// Prepare for the next command
					command_to_execute.reset();
					argument_for_command.resize(0);
					parser.Reset();
				}
			} 
		_shift = readed_bytes;
		}
	} catch (std::runtime_error &ex) {
		_logger->error("Failed to process connection on descriptor {}: {}",
						_event.data.fd, ex.what());
	}
}

// See Connection.h
void Connection::DoWrite() {
	int iovcnt = _response.size();
	iovec* answer = new iovec[iovcnt];
	for (int i = 0; i < iovcnt; i++) {
		//answer[i].iov_base = _response[i].data();
		answer[i].iov_base = &_response[i][0];
		answer[i].iov_len = _response[i].size();
	}
	
	auto residue = writev(_event.data.fd, answer, iovcnt);
	_logger->debug("Bytes written {}", residue);
	int pos;
	for (pos = 0; pos < iovcnt; pos++) {
		if (residue >= answer[pos].iov_len) {
			residue -= answer[pos].iov_len;
		}
	}

	_response.erase(_response.begin(), _response.begin() + pos);
	if (residue) {
		_response[pos].erase(_response[pos].begin(), _response[pos].begin() + residue);
	}

	if (_response.empty()) {
		_event.events = EPOLLIN | EPOLLRDHUP | EPOLLPRI;
	}
	delete[] answer;
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
