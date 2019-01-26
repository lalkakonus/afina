#include "Connection.h"

#include <iostream>

namespace Afina {
namespace Network {
namespace MTnonblock {

Connection::Connection(int socket_fd, std::shared_ptr<Afina::Storage> ps,
					   std::shared_ptr<Afina::Logging::Service> pl): 
					   _socket(socket_fd), read_shift(0), write_shift(0),
					   pLogging(pl), pStorage(ps) { 
	std::memset(&_event, 0, sizeof(struct epoll_event)); 
	_event.data.ptr = this;
    _event.events = mask_read;
	std::cout << "Connection ()\n";
}


// See Connection.h
void Connection::Start() { 
    _logger = pLogging->select("network");
	_logger->debug("Start new connection\n");
    _sync.store(true);
	_isActive.store(true);
}

// See Connection.h
void Connection::OnError() {
    _isActive.store(false);
	_logger->error("Error happens. Connection closed\n");
}

// See Connection.h
void Connection::OnClose() {
    _isActive.store(false);
	_logger->debug("Connection closed\n");
}

// See Connection.h
void Connection::DoRead() { 
	_sync.load(std::memory_order_acquire);
    _logger->debug("Start reading\n");
	try {
        int readed_bytes;
		while ((readed_bytes = read(_socket, client_buffer + read_shift,
									sizeof(client_buffer) - read_shift)) > 0) {
			_logger->debug("Got {} bytes from socket", readed_bytes);
            readed_bytes += read_shift;
            while (readed_bytes > 0) {
				_logger->debug("Processed {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, readed_bytes, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        command_to_execute = parser.Build(arg_remains);
                       	_logger->debug("Found new command: {} in {} bytes", parser.Name(),
										parsed);
						if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");
					std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);
                    result += "\r\n";

                    // Save response
					_response.push_back(result);
					_event.events = mask_read_write;

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            }
			read_shift = readed_bytes;
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {} : {}", _socket, ex.what());
    }
    _sync.store(true, std::memory_order_release);
}


// See Connection.h
void Connection::DoWrite() {
	_sync.load(std::memory_order_acquire);
	iovec iovecs[64];
    std::size_t iovcnt = _response.size();
	if (iovcnt > 64) {
		iovcnt = 64;
	}
	
	int i;
	// No reallocation for last element
	if (write_shift) {
		iovecs[0].iov_base = (char*) &_response[i][0] + write_shift;
		iovecs[0].iov_len = _response[i].size() - write_shift;
		i = 1;
	} else {
		i = 0;
	}

	for (; i < iovcnt; i++) {
		iovecs[i].iov_base = &_response[i][0];
		iovecs[i].iov_len = _response[i].size();
	}
	
	auto residue = writev(_socket, iovecs, iovcnt);
	_logger->debug("Bytes written {}", residue);
	int pos;
	for (pos = 0; pos < iovcnt; pos++) {
		if (residue >= iovecs[pos].iov_len) {
			residue -= iovecs[pos].iov_len;
		} else {
			break;
		}
	}

	_response.erase(_response.begin(), _response.begin() + pos);
	write_shift = residue;

	if (_response.empty()) {
		_event.events = mask_read;
	}
	
    _sync.store(true, std::memory_order_release);
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
