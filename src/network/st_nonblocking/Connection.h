#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

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

namespace Afina {
class Storage;
}

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace STnonblock {

class Connection {
public:
	~Connection();
    Connection(int s, std::shared_ptr<Afina::Storage>, std::shared_ptr<Afina::Logging::Service>);

    inline bool isAlive() const { 
		return _isActive;
	}
	
	Connection(const Connection &other);
	Connection& operator=(const Connection &other); 

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

	std::size_t arg_remains;
	char client_buffer[4096];
    Protocol::Parser parser;
    std::string argument_for_command;
    std::shared_ptr<Execute::Command> command_to_execute;

    std::shared_ptr<spdlog::logger> _logger;
	std::shared_ptr<Afina::Storage> _pStorage;
    std::shared_ptr<Afina::Logging::Service> pLogging;
	
	int _shift;
    bool _isActive;
    struct epoll_event _event;
	std::vector<std::string> _response;

};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
