#ifndef AFINA_NETWORK_MT_BLOCKING_SERVER_H
#define AFINA_NETWORK_MT_BLOCKING_SERVER_H

// #include "ThreadPool.h"
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>

#include <afina/network/Server.h>

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace MTblocking {

const unsigned int THREAD_COUNT = 5;

class ServerImpl;

class Worker {
public:
	Worker(std::shared_ptr<Afina::Logging::Service>, ServerImpl * ptr, unsigned int id);
	~Worker();

	bool CheckActive() const;

	void Start(int clientSoket);	

private:
	void Process(ServerImpl * ptr);

	int _socket;
	unsigned int _id;
	bool _isActive, _onRun;
	std::condition_variable cv;
	mutable std::mutex mutex;
	std::thread thread; // on_run, is_acive - initialize first
    std::shared_ptr<Afina::Logging::Service> pLogging;
};

class ThreadPool {
public:
	ThreadPool(std::shared_ptr<Afina::Logging::Service>, ServerImpl * ptr, unsigned int maxCount);

	bool AddConnection(int clientSocket);

private:	
	std::shared_ptr<Worker> GetFreeWorker();

	const unsigned int _maxCount;
	std::vector<std::shared_ptr<Worker>> _workers;
    std::shared_ptr<Afina::Logging::Service> pLogging;
};


/**
 * # Network resource manager implementation
 * Server that is spawning a separate thread for each connection
 */
class ServerImpl : public Server {
public:
	ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl);
	~ServerImpl();

	// See Server.h
	void Start(uint16_t port, uint32_t, uint32_t) override;

	// See Server.h
	void Stop() override;

	// See Server.h
	void Join() override;

	void ProcessThread(int client_socket, int thread_num);

protected:
	/**
	 * Method is running in the connection acceptor thread
	 */
	void OnRun();

private:

	// Logger instance
	std::shared_ptr<spdlog::logger> _logger;

	// Atomic flag to notify threads when it is time to stop. Note that
	// flag must be atomic in order to safely publisj changes cross thread
	// bounds
	std::atomic<bool> running;

	// Server socket to accept connections on
	int _server_socket;

	// Thread to run network on
	std::thread _thread;

	std::shared_ptr<Logging::Service> _pl;
	// ThreadPool _thread_pool;
};

} // namespace MTblocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_BLOCKING_SERVER_H
