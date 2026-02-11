#include "thread_pool.h"

#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <queue>
#include <chrono>

enum thread_task_status {
	TASK_NEW = 1,
	TASK_WAITING,
	TASK_RUNNING,
	TASK_FINISHED
};

struct thread_task {
	thread_task_f function;

	/* PUT HERE OTHER MEMBERS */
	std::condition_variable cv = std::condition_variable();
	thread_task_status status = TASK_NEW;
	std::mutex mutex = std::mutex();
	std::atomic<bool> detatch{false};
};

struct thread_pool {
	std::vector<std::thread> threads;
	std::queue<thread_task*> tasks;

	/* PUT HERE OTHER MEMBERS */
	size_t threadsLimit;
	std::mutex poolMutex = std::mutex();
	std::condition_variable poolCv = std::condition_variable();
	std::atomic<size_t> busyThreadsCount{0};
	bool stop = false;
};

int
thread_pool_new(int thread_count, struct thread_pool **pool)
{
	if ((thread_count > TPOOL_MAX_THREADS) || (thread_count <= 0)) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	(*pool) = new thread_pool();
	(*pool)->threadsLimit = thread_count;
	(*pool)->threads.reserve(thread_count);

	return 0;
}

int
thread_pool_delete(struct thread_pool *pool)
{
	if (pool->tasks.size() > 0 || pool->busyThreadsCount > 0) {
		return TPOOL_ERR_HAS_TASKS;
	}

	std::unique_lock lock(pool->poolMutex);
    pool->stop = true;
	lock.unlock();
	pool->poolCv.notify_all();

	for (auto& thread : pool->threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

	pool->threads.clear();

	delete pool;

	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{

	if (pool->tasks.size() == TPOOL_MAX_TASKS) {
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	if ((pool->busyThreadsCount.load() == pool->threads.size()) && (pool->threads.size() < pool->threadsLimit)) {
		pool->threads.emplace_back([pool]{
			while (!pool->stop) {
				std::unique_lock lock(pool->poolMutex);
				pool->poolCv.wait(lock, [pool]{ return !pool->tasks.empty() || pool->stop; });

				if (!pool->tasks.empty()) {
					thread_task *task = pool->tasks.front();
					pool->tasks.pop();
					lock.unlock();

					std::unique_lock taskLock(task->mutex);
					pool->busyThreadsCount++;
					task->status = TASK_RUNNING;
					taskLock.unlock();

					task->function();
					
					taskLock.lock();
					task->status = TASK_FINISHED;
					pool->busyThreadsCount--;
					bool should_delete = task->detatch.load();					

					taskLock.unlock();
					task->cv.notify_one();

					if (should_delete)
						delete task;
				}
			}
		});
	}
	
	std::unique_lock lock(pool->poolMutex);
	task->status = TASK_WAITING;
	pool->tasks.push(task);
	lock.unlock();

	pool->poolCv.notify_one();


	return 0;
}

int
thread_task_new(struct thread_task **task, const thread_task_f &function)
{
	*task = new thread_task{ .function = function};

	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	return task->status == TASK_FINISHED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	return task->status == TASK_RUNNING;
}

int
thread_task_join(struct thread_task *task)
{
	if (task->status == TASK_NEW)
		return TPOOL_ERR_TASK_NOT_PUSHED;
	

	if (task->status == TASK_FINISHED)
		return 0;

	std::unique_lock lock(task->mutex);
	task->cv.wait(lock, [=]{return task->status == TASK_FINISHED;});
	lock.unlock();

	return 0;
}

#if NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout)
{
	if (task->status == TASK_NEW)
		return TPOOL_ERR_TASK_NOT_PUSHED;

	if (task->status == TASK_FINISHED)
		return 0;
	
	if (timeout <= 0.0)
		return TPOOL_ERR_TIMEOUT;
	
	auto timeout_duration = std::chrono::duration<double>(timeout);

	std::unique_lock lock(task->mutex);
	bool done = task->cv.wait_for(lock, timeout_duration) != std::cv_status::timeout;
	lock.unlock();

	if (done)
		return 0;

	return TPOOL_ERR_TIMEOUT;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
	if ((task->status == TASK_WAITING) || (task->status == TASK_RUNNING)) {
		return TPOOL_ERR_TASK_IN_POOL;
	}

	delete task;

	return 0;
}

#if NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	std::unique_lock lock(task->mutex);
	if (task->status == TASK_NEW) {
		lock.unlock();
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	if (task->status == TASK_FINISHED) {
		lock.unlock();
		delete task;
		return 0;
	}

	task->detatch = true;

	lock.unlock();

	return 0;
}

#endif
