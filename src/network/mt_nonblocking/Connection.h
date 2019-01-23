#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <cstring>
#include <string>
#include <sys/epoll.h>
#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <protocol/Parser.h>
#include <spdlog/logger.h>
#include "Utils.h"
#include <iostream>
#include <sys/uio.h>
#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
	Connection(int socket_fd, std::shared_ptr<Afina::Storage>, std::shared_ptr<Afina::Logging::Service>); 
	
	inline bool isActive() const {
		return _isActive.load(); 
	}

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

	static const int mask_read = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLONESHOT;
    static const int mask_read_write = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLOUT | EPOLLONESHOT;
    
    std::shared_ptr<spdlog::logger> _logger;
    std::shared_ptr<Afina::Storage> pStorage;
    std::shared_ptr<Afina::Logging::Service> pLogging;
    
    std::atomic<bool> _sync;
    std::atomic<bool> _isActive;

    int _socket;
    struct epoll_event _event;
    
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    
    int readed_bytes = 0;
    char client_buffer[4096];
    
    std::vector<std::string> _response;
	int read_shift, write_shift;

};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
