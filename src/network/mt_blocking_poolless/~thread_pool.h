#ifndef AFINA_NETWORK_MT_BLOCKING_THREAD_POOL_H
#define AFINA_NETWORK_MT_BLOCKING_THREAD_POOL_H

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace Afina {
namespace Network {
namespace MTblocking {

const int THREAD_COUNT = 5;

class ServerImpl;

class Worker {
public:
	Worker(std::shared_ptr<Afina::Logging::Service>, ServerImpl * ptr, int);
	~Worker();

	bool CheckActive() const;

	void Start(int client_soket);	

private:
	void Process(ServerImpl * ptr);

	int _socket, _num;
	bool is_active, on_run;
	std::condition_variable cv;
	mutable std::mutex active_mutex;
	std::thread thread; // on_run, is_acive - initialize first
    std::shared_ptr<Afina::Logging::Service> pLogging;
};

class ThreadPool {
public:
	ThreadPool(std::shared_ptr<Afina::Logging::Service>, ServerImpl * ptr, int);

	bool AddConnection(int client_socket);

private:	
	std::shared_ptr<Worker> GetFreeWorker();

	const unsigned int _max_count;
	std::vector<std::shared_ptr<Worker>> _workers;
    std::shared_ptr<Afina::Logging::Service> pLogging;
};

} // namespace MTblocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_BLOCKING_THREAD_POOL_H
