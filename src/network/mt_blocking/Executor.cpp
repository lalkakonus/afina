#include "afina/Executor.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <stdexcept>

namespace Afina {

void perform(Executor* executor, std::function<void()> perform_first);

int max(const int& a, const int& b) {
	if (a > b) {
		return a;
	} else {
		return b;
	};
}

int min(const int& a, const int& b) {
	if (a < b) {
		return a;
	} else {
		return b;
	};
}

// using namespace Executor;
    
void Executor::Stop(bool await /*=false*/) {
	std::unique_lock<std::mutex> lock(mutex);
	auto state = State::kStopping;
	if (await) {
		stop_condition.wait(lock, [this]{return not threads.size();});
		state = State::kStopped;
	};
}


template <typename F, typename... Types> bool Executor::Execute(F &&func, Types... args) {
	auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

	std::unique_lock<std::mutex> lock(mutex);
	if (state != State::kRun or tasks.size() == MAX_QUEUE_SIZE) {
		return false;
	};

	if (not free_threads_cnt and threads.size() < HIGHT_WATERMARK) {
		threads.push_back(Afina::perform, this, exec);
	} else {
		// Enqueue new task
		tasks.push_back(exec);
		empty_condition.notify_one();
	};
	return true;
}

Executor::Executor(int size, int min_size /*= 1*/, 
		 int max_size /* = 10*/,  int max_queue_size /* = 10*/): LOW_WATERMARK(min_size), 
																 HIGHT_WATERMARK(max_size),
																 MAX_QUEUE_SIZE(max_queue_size),
																 free_threads_cnt(0),
																 idle_time(10),
																 state(State::kRun) {

	size = max(size, LOW_WATERMARK);
	size = min(size, HIGHT_WATERMARK);

	threads.reserve(size);
	//auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);
	std::function<void()> foo = []{};

	for (int i = 0; i < size; i++) {
		// std::thread t(&perform, this, foo);
		//threads.push_back(Afina::perform, this, foo);
		// threads.push_back(perform, this);
		//threads.emplace_back(perform, this);
	};
}
	
std::pair<std::function<void()>, bool> Executor::get_function() {
	std::unique_lock<std::mutex> lock(mutex);
	free_threads_cnt++;
	if (not tasks.size()) {
		if (state == State::kStopping) {
			free_threads_cnt--;
			return std::make_pair<std::function<void()>, bool> ([]{}, true);
		};
		if (not empty_condition.wait_for(lock, idle_time, [this]{ return tasks.size(); })) {
			free_threads_cnt--;
			return std::make_pair<std::function<void()>, bool> ([]{}, true);
		};
	};

	free_threads_cnt--;
	auto function = std::move(tasks.front());
	tasks.pop_front();
	return std::make_pair(function, false);
}

bool Executor::delete_thread(std::thread::id id) {
	auto predicate = [&id](const std::thread& thread){ return id == thread.get_id(); };
	
	auto erase_pos = std::find_if(threads.begin(), threads.end(), predicate);

	if (erase_pos != threads.end()) {
		if (state == State::kStopping) {
			erase_pos->detach();
			threads.erase(erase_pos);
			if (not tasks.size() and state == State::kStopping) {
				stop_condition.notify_one();
			};
			return true;
		} else {
			return false;
		};
	};
	throw std::range_error("abc");
}

//} // namespace Executor

void perform(Afina::Executor* executor, std::function<void()> perform_first) {
	perform_first();
	
	while (executor->state == executor->State::kRun or
		   executor->state == executor->State::kStopping) {
		
		// Get function from queue
		auto result_pair = executor->get_function();
		
		// Check necessity of current thread
		std::unique_lock<std::mutex> lock(executor->mutex);
		if (result_pair.second and executor->threads.size() > executor->LOW_WATERMARK) {
			if (executor->delete_thread(std::this_thread::get_id())) {
				return;
			};
		};
		lock.unlock();

		// Execute function
		result_pair.first();
	};
}

} // namespace Afina
