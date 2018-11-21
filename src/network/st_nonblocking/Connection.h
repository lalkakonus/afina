#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <sys/epoll.h>

namespace Afina {
namespace Network {
namespace STnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> pStorage, std::shared_ptr<Logging::Service>);

    inline bool isAlive() const { 
		return _isActive;
	}

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
    std::unique_ptr<Execute::Command> command_to_execute;

    std::shared_ptr<spdlog::logger> _logger;
	std::shared_ptr<Afina::Storage> _pStorage;
	
	int _shift;
    bool _isActive;
	int _socket;
    struct epoll_event _event;
	std::vector<std::string> _response;

};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
