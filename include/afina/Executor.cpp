namespace Afina {

int max(int a, int b) {
	if (a > b) {
		return a;
	} else {
		return b;
	};
}

int min(int a, int b) {
	if (a < b) {
		return a;
	} else {
		return b;
	};
}

namespace Executor {
    
void Stop(bool await = false) {
	state = State::kStopping;
	if (await) {
		std::unique_lock<std::mutex> lock(mutex);
		empty_condition.wait(lock, []{return threads.size() == 0});
		state = State::kStop;
	};
}


template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
	// Prepare "task"
	auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

	std::unique_lock<std::mutex> lock(this->mutex);
	if (state != State::kRun or tasks.size() == MAX_QUEUE_SIZE) {
		return false;
	};

	if (free_threads_cnt == 0 and threads.size() < MAX_SIZE) {
		threads.push_back(Afina::preform, this, exec)
	} else {
		// Enqueue new task
		tasks.push_back(exec);
		empty_condition.notify_one();
	};
	return true;
}

Executor(std::string name, int size, int min_size = 1, 
		 int max_size = 10,  int max_queue_size = 10): MIN_SIZE(min_size), 
								   					   MAX_SIZE(max_size),
													   MAX_QUEUE_SIZE(max_queue_size),
													   free_threads_cnt(0) {

	size = max(size, MIN_SIZE);
	size = min(size, MAX_SIZE);

	threads.reserve(size);
	for (int i = 0; i < size; i++) {
		tasks.emplace_back(Afina::perform, this);
	};
}
	
std::function<void()> Afina::Executor::get_function(bool& delete_thread) {
	std::unique_lock<std::mutex> lock(mutex);
	free_threads_cnt++;
	if (!tasks.size()) {
		if (state == State::kStopping) {
			delete_thread = true;
			return [](){};
		};
		if (not executor->empty_condition.wait_for(lock, idle_time, [&tasks](){return tasks.size()})) {
			delete_thread = true;
			return [](){};
		};
	};

	free_threads_cnt--;
	function = std::move(tasks.front());
	tasks.pop_front();
	return function;
}

bool delete_thread(std::thread::id id) {
	predicate = [](const std::thread& thread){ return id==thread.get_id() };
	auto erase_pos = std::find_if(threads.begin(), threads.end(), predicate)

	if (erase_pos != threads.end()) {
		std::unique_lock<std::mutex> lock(mutex);
		if (threads.size() > MIN_SIZE or state == State::kStopping) {
			threads.erase(erase_pos);
			if (not tasks.size() and state == State::kStopping) {
				empty_condition.notify_one();
			return true;
		} else {
			return false;
		};
	};
	throw std::range_error();
}

} // namespace Executor

void Afina::preform(Afina::Executor* executor, std::function<void()> pereform_first) {
	perform_first();
	
	while ((executor->state == kRun) or (executor->state == kStopping)) {
		bool delete_thread = false;
		
		function = executor->get_function(delete_thread);
		if (delete_thread and executor->tasks.size() > executor->MIN_SIZE) {
			if (executor->delete_thread(std::this_thread::get_id())) {
				return;
			} else
				delete_thread = false;
		};
		function();
	};
}

} // namespace Afina
