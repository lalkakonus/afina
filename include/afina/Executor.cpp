namespace Afina {

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

namespace Executor {
    
void Stop(bool await = false) {
	std::unique_lock<std::mutex> lock(mutex);
	state = State::kStopping;
	if (await) {
		stop_condition.wait(lock, []{ return not threads.size(); });
		state = State::kStop;
	};
}


template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
	auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

	std::unique_lock<std::mutex> lock(mutex);
	if (state != State::kRun or tasks.size() == MAX_QUEUE_SIZE) {
		return false;
	};

	if (not free_threads_cnt and threads.size() < HIGHT_WATERMARK) {
		threads.push_back(Afina::preform, this, exec);
	} else {
		// Enqueue new task
		tasks.push_back(exec);
		empty_condition.notify_one();
	};
	return true;
}

Executor(std::string name, int size, int min_size = 1, 
		 int max_size = 10,  int max_queue_size = 10): LOW_WATERMARK(min_size), 
								   					   HIGHT_WATERMARK(max_size),
													   MAX_QUEUE_SIZE(max_queue_size),
													   free_threads_cnt(0),
													   state(State::kRun) {

	size = max(size, LOW_WATERMARK);
	size = min(size, HIGHT_WATERMARK);

	threads.reserve(size);
	for (int i = 0; i < size; i++) {
		tasks.emplace_back(Afina::perform, this);
	};
}
	
std::pair<std::function<void()>, bool> Afina::Executor::get_function() {
	std::unique_lock<std::mutex> lock(mutex);
	free_threads_cnt++;
	if (not tasks.size()) {
		if (state == State::kStopping) {
			free_threads_cnt--;
			return std::make_pair<std::function<void()>, bool> ([]{}, true);
		};
		if (not executor->empty_condition.wait_for(lock, idle_time, [&tasks]{ return tasks.size(); })) {
			delete_thread = true;
			free_threads_cnt--;
			return std::make_pair<std::function<void()>, bool> ([]{}, true);
		};
	};

	free_threads_cnt--;
	function = std::move(tasks.front());
	tasks.pop_front();
	return std::make_pair(function, false);
}

bool delete_thread(std::thread::id id) {
	predicate = [](const std::thread& thread){ return id == thread.get_id(); };
	
	auto erase_pos = std::find_if(threads.begin(), threads.end(), predicate)

	if (erase_pos != threads.end()) {
		if (state == State::kStopping) {
			threads[erase_pos].detach();
			threads.erase(erase_pos);
			if (not tasks.size() and state == State::kStopping) {
				stop_condition.notify_one();
			};
			return true;
		} else {
			return false;
		};
	};
	throw std::range_error();
}

} // namespace Executor

void Afina::pereform(Afina::Executor* executor, std::function<void()> perform_first) {
	perform_first();
	
	while (executor->state == kRun or executor->state == kStopping) {
		
		// Get function from queue
		auto result_pair = executor->get_function();
		
		// Check necessity of current thread
		std::unique_lock<std::mutex> lock(mutex);
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
