#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <type_traits>
#include <utility>
#include <stdexcept>

template <int size>
class ThreadPool {
public:
    ThreadPool() {
        threads_.reserve(size);
        for (int i = 0; i < size; i++) {
            threads_.emplace_back([this] {
                work();
            });
        }
    }

    ~ThreadPool() {
        stop();
        for (int i = 0; i < size; i++) {
            threads_[i].join();
        }
    }

    template<typename Func, typename... Args>
    auto run(Func&& func, Args&&... args) -> std::future<std::invoke_result_t<Func, Args...>> {
        using Result = std::invoke_result_t<Func, Args...>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::bind(
                std::forward<Func>(func),
                std::forward<Args>(args)...
            )
        );
        auto future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stopped) {
                throw std::runtime_error("The thread pool is stopped!");
            }
            tasks_.push([task] {
                (*task)();
            });
        }
        cv_.notify_one();
        return future;
    }

    auto stop() -> void {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stopped) return;
            stopped = true;
        }
        cv_.notify_all();
    }

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stopped = false;

    auto work() -> void {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx_);

                cv_.wait(lock, [this] {
                    return !tasks_.empty() || stopped;
                });

                if (stopped && tasks_.empty()) return;

                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }
};

#endif
