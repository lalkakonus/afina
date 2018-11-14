#include "ThreadPool.h"

namespace Afina {
namespace Network {
namespace MTblocking {

Worker::Worker(std::shared_ptr<Afina::Logging::Service> pl,
			   ServerImpl * ptr, unsigned int id): _id(id), 
			   							  		   _onRun(true), 
												   _isActive(false),
												   thread(&Worker::Process, this, ptr), 
												   pLogging(pl) {};

Worker::~Worker() {
	_onRun = false;
	cv.notify_one();
	thread.join();
}

bool Worker::CheckActive() const{
	std::lock_guard<std::mutex> lock(mutex);
   	_logger = pLogging->select("root");
    _logger->debug("Check worker #{}", _id);
	return _isActive;
}

void Worker::Process(ServerImpl * ptr) {
	while (_onRun) {
		std::unique_lock<std::mutex> lock(activeMutex);
		cv.wait(lock, [&]{return _isActive || !_onRun;});
		if (!_onRun) {
			return;
		}
		lock.unlock();
		_logger = pLogging->select("root");
		_logger->debug("Worker #{} start connection pendind", _id);
		ptr->ProcessThread(_socket, _id);		
		lock.lock();
		isActive = false;
	}
}

void Worker::Start(int clientSoket) {
	std::unique_lock<std::mutex> lock(mutex);
	_isActive = true;
	_socket = clientSoket;
	cv.notify_one();
}

ThreadPool::ThreadPool(std::shared_ptr<Afina::Logging::Service> pl, 
					   ServerImpl * ptr, int maxCount = 1): _maxCount(maxCount), 
					   										 pLogging(pl) {
	_workers.reserve(_maxCount);
	for (int i = 0; i < _maxCount; i++) {
		_workers.emplace_back(new Worker(pl, ptr, i));
	}
}

std::shared_ptr<Worker> ThreadPool::GetFreeWorker() {
	for (auto& ptr: _workers) {
		if (!ptr->CheckActive())
			return ptr;
	}
	return nullptr;
}

bool ThreadPool::AddConnection(int clientSocket) {
	auto it=GetFreeWorker();
	if (it) {
		it->Start(clientSocket);	
		return true;
	}
   	auto _logger = pLogging->select("root");
    _logger->warn("No free threads");

	return false;
}

} // namespace MTblocking
} // namespace Network
} // namespace Afina
