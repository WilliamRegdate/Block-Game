// JobSystem.h
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool
{
    public:
        explicit ThreadPool(unsigned count)
        {
            for (unsigned i = 0; i < count; ++i)
                workers.emplace_back([this] { Run(); });
        }
        ~ThreadPool()
        {
            { std::lock_guard<std::mutex> lock(m); stop = true; }
            cv.notify_all();
            for (auto& t : workers) t.join();
        }

        // priority = true puts the job at the front of the queue
        void Submit(std::function<void()> job, bool priority = false)
        {
            {
                std::lock_guard<std::mutex> lock(m);
                if (priority) jobs.push_front(std::move(job));
                else          jobs.push_back(std::move(job));
            }
            cv.notify_one();
        }

    private:
        void Run()
        {
            for (;;)
            {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(m);
                    cv.wait(lock, [this] { return stop || !jobs.empty(); });
                    if (stop && jobs.empty()) return;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                job();
            }
        }

        std::vector<std::thread>          workers;
        std::deque<std::function<void()>> jobs;
        std::mutex                        m;
        std::condition_variable           cv;
        bool                              stop = false;
};

// Workers push, the main thread drains once per frame.
template<typename T>
class ResultQueue
{
    public:
        void Push(T value)
        {
            std::lock_guard<std::mutex> lock(m);
            items.push_back(std::move(value));
        }
        void Drain(std::vector<T>& out)   // appends everything, leaves the queue empty
        {
            std::lock_guard<std::mutex> lock(m);
            for (T& v : items) out.push_back(std::move(v));
            items.clear();
        }
    private:
        std::mutex     m;
        std::vector<T> items;
};