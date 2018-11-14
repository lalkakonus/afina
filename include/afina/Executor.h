#ifndef AFINA_THREADPOOL_H
#define AFINA_THREADPOOL_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

namespace Afina {

/**
 * # Thread pool
 */
class Executor {
public:
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

	Executor(int size = 4, int min_size = 2, int max_size = 10, int max_queue_size = 10);
    ~Executor();

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads
	 * just after each become free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are
	 * done and all threads are stopped
     */
    void Stop(bool await = false);

    /**
     * Add function to be executed on the threadpool. Method returns true in case if
	 * task has been placed onto execution queue, i.e scheduled for execution and 
	 * false otherwise.
     *
     * That function doesn't wait for function result.
	 * Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args);

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    // Why not Executor& ??, add default parametr
	friend void perform(Executor*, std::function<void()>);

    // Mutex to protect state below from concurrent modification
    std::mutex mutex;

    // Conditional variable to await new data in case of empty queue
    std::condition_variable empty_condition;
    // CV to await all tasks finished
	std::condition_variable stop_condition;

    // Vector of actual threads that perorm execution
    std::vector<std::thread> threads;

	// Count of free threads
	int free_threads_cnt;

	// High and low size of thread vector
	const int LOW_WATERMARK, HIGHT_WATERMARK;
	// Time after free thread self destruct
	const std::chrono::seconds idle_time;

	// Delete thread with certain id from vector
	void delete_thread(std::thread::id);

    // Task queue
    std::deque<std::function<void()>> tasks;
	const int MAX_QUEUE_SIZE;

	// Get function from task queue
	std::pair<std::function<void()>, bool> get_function();
    
    // Flag to stop enqueue tasks
    State state;

};

void perform(Executor* executor, std::function<void()> perform_first);

template <typename F, typename... Types> bool Executor::Execute(F &&func, Types... args) {
	auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

	std::unique_lock<std::mutex> lock(mutex);
	if (state != State::kRun || tasks.size() == MAX_QUEUE_SIZE) {
		return false;
	};

	if (!free_threads_cnt && threads.size() < HIGHT_WATERMARK) {
		threads.emplace_back(Afina::perform, this, exec);
	} else {
		tasks.push_back(exec);
		empty_condition.notify_one();
	};
	return true;
}

} // namespace Afina

#endif // AFINA_THREADPOOL_H
