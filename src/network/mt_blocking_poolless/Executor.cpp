#include "afina/Executor.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace Afina {

Executor::Executor(int size/*=4*/, int min_size/*=1*/, int max_size/*=10*/,
				   int max_queue_size/*=10*/): LOW_WATERMARK(min_size), 
											   HIGHT_WATERMARK(max_size),
											   MAX_QUEUE_SIZE(max_queue_size),
											   free_threads_cnt(0),
											   idle_time(10),
											   state(State::kRun) {

	// Correct size into [LOW_WATERMARK, HIGHT_WATERMARK] interval
	size = size > HIGHT_WATERMARK ? HIGHT_WATERMARK : size;
	size = size < LOW_WATERMARK ? LOW_WATERMARK : size;

	threads.reserve(size);
	for (int i = 0; i < size; i++) {
		threads.emplace_back(Afina::perform, this, []{});
	};
}

Executor::~Executor() {};

// See Executor.h
void Executor::Stop(bool await/*=false*/) {
	std::unique_lock<std::mutex> lock(mutex);
	state = State::kStopping;
	empty_condition.notify_all();	
	if (await) {
		stop_condition.wait(lock, [this]{return !threads.size();});
		state = State::kStopped;
	};
}
	
// Pop function from queue
std::pair<std::function<void()>, bool> Executor::get_function() {
	std::unique_lock<std::mutex> lock(mutex);
	free_threads_cnt++;
	if (!tasks.size()) {
		auto predicate = [this]{ return tasks.size() || state == State::kStopping; };
		empty_condition.wait_for(lock, idle_time, predicate);
		if (!tasks.size() or state == State::kStopping) {
			// thread should be deleted
			free_threads_cnt--;
			return std::make_pair<std::function<void()>, bool> ([]{}, true);
		};
	};

	// Pop function and return it
	free_threads_cnt--;
	auto function = std::move(tasks.front());
	tasks.pop_front();
	return std::make_pair(function, false);
}

void Executor::delete_thread(std::thread::id id) {
	auto predicate = [&id](const std::thread& thread){ return id == thread.get_id(); };
	auto erase_it = std::find_if(threads.begin(), threads.end(), predicate);

	// May be delete it (not necessary)
	if (erase_it == threads.end()) {
		throw std::range_error("Deleted thread not found.");
	};
	
	erase_it->detach();
	threads.erase(erase_it);
	
	if (!threads.size() && state == State::kStopping) {
		stop_condition.notify_one();
	};
}

void perform(Afina::Executor* executor, std::function<void()> perform_first) {
	bool condition1, condition2, condition3;
	
	perform_first();

	while (true) {
		
		// Get function from queue
		auto result_pair = executor->get_function();
		
		// Check necessity of current thread
		std::unique_lock<std::mutex> lock(executor->mutex);
		
		// Thread pool is going to stop
		condition1 = executor->state == executor->State::kStopping;
		// Thread vector bigger than min size
		condition2 = executor->threads.size() > executor->LOW_WATERMARK;
		// Thread should be deleted
		condition3 = result_pair.second;
		
		if ((condition1 || condition2) && condition3) {
			executor->delete_thread(std::this_thread::get_id());
			return;
		};
		lock.unlock();

		// Execute unqueued function
		result_pair.first();
	};
}

} // namespace Afina
