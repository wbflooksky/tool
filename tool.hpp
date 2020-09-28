#include <iostream>
#include <chrono>
#include <ctime>
#include <mutex>
#include <functional>
#include <queue>
#include <thread>
#include <map>
#include <sstream>
#include <vector>
#include <queue>
#include <memory>
#include <future>
#include <stdexcept>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <forward_list>
#include <shared_mutex>

#include "threadManager.hpp"

template <class T>
class SafeQueue {
public:
    SafeQueue(int size_limit) : size_limit_(size_limit) {}
    bool push(const T& value);
    bool pushNoCondition(T&& value);
    bool get(T& value);
    bool getNoCondition(T& value);
    bool emplace(T&& value);

private:
    std::mutex mtx_;
    std::condition_variable not_empty_cv_;
    std::queue<T> queue_;
    int queue_size_ = 0;
    int size_limit_;
};

template <class T> bool SafeQueue<T>::push(const T& value) {
    std::lock_guard<std::mutex> lgd(mtx_);
    if (queue_size_ == size_limit_ || queue_size_ > size_limit_) {
        return false;
    }
    queue_size_++;
    queue_.push(value);
    not_empty_cv_.notify_one();
    return true;
}

template <class T> bool SafeQueue<T>::pushNoCondition(T&& value) {
    std::lock_guard<std::mutex> lgd(mtx_);
    if (queue_size_ == size_limit_ || queue_size_ > size_limit_) {
        return false;
    }
    queue_size_++;
    queue_.emplace(std::forward<T>(value));
    return true;
}

template <class T> bool SafeQueue<T>::get(T& value) {
    std::unique_lock<std::mutex> ulk(mtx_);
    if (queue_size_ == 0) {
        return false;
    }
    queue_size_--;
    not_empty_cv_.wait(ulk, [this](){
        return queue_size_ != 0;
    });
    value = std::move(queue_.front());
    queue_.pop();
    return true;
}

template <class T> bool SafeQueue<T>::getNoCondition(T& value) {
    std::lock_guard<std::mutex> ulk(mtx_);
    if (queue_size_ == 0) {
        return false;
    }
    queue_size_--;
    value = std::move(queue_.front());
    queue_.pop();
    return true;
}

template <class T> bool SafeQueue<T>::emplace(T&& value) {
    std::lock_guard<std::mutex> lgd(mtx_);
    if (queue_size_ == size_limit_ || queue_size_ > size_limit_) {
        return false;
    }
    queue_.emplace(std::forward<T>(value));
    queue_size_++;
    not_empty_cv_.notify_one();
    return true;
}

class TimerItem {
public:
    TimerItem() = default;
    TimerItem(uint64_t timePoint, uint32_t interval, int execCount, std::function<void()>&& func) {
        timePoint_ = timePoint;
        interval_ = interval;
        execCount_ = execCount;
        func_ = std::move(func);
    }
    TimerItem(const TimerItem& timerItem) {
        timePoint_ = timerItem.timePoint_;
        interval_ = timerItem.interval_;
        execCount_ = timerItem.execCount_;
        func_ = timerItem.func_;
    }
    TimerItem(TimerItem&& timerItem) {
        timePoint_ = timerItem.timePoint_;
        interval_ = timerItem.interval_;
        execCount_ = timerItem.execCount_;
        func_ = std::move(timerItem.func_);
    }
    TimerItem& operator=(const TimerItem& timerItem) {
        timePoint_ = timerItem.timePoint_;
        interval_ = timerItem.interval_;
        execCount_ = timerItem.execCount_;
        func_ = timerItem.func_;
        return *this;
    }
    TimerItem& operator=(TimerItem&& timerItem) {
        timePoint_ = timerItem.timePoint_;
        interval_ = timerItem.interval_;
        execCount_ = timerItem.execCount_;
        func_ = std::move(timerItem.func_);
        return *this;
    }
    ~TimerItem() = default;

    uint64_t timePoint_;
    uint32_t interval_;
    int execCount_;
    std::function<void()> func_;
};

class Timer : public ThreadManager {
public:
    // select interval
    Timer(int interval = 10);
    ~Timer();
    template<class F, class... Args>
    void timerEvent(int execCount, uint32_t interval, F&& f, Args&&... args);
    void run() override;

private:

    int interval_;
    std::unique_ptr<SafeQueue<TimerItem>> safeQueue_ = nullptr;
    std::multimap<uint64_t, TimerItem> multimapTimer_;
};

Timer::Timer(int interval) {
    safeQueue_ = std::unique_ptr<SafeQueue<TimerItem>>(new SafeQueue<TimerItem>(1024));
    interval_ = interval;
}

// execCount:重复执行的次数，小于等于零的数代表会一直执行下去。
// interval:定时精度，默认10毫秒。
// f:要执行函数的指针。
// args：被执行函数的参数列表。
template<class F, class... Args> void Timer::timerEvent(int execCount, uint32_t interval, F&& f, Args&&... args) {
    safeQueue_->pushNoCondition(TimerItem(std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::steady_clock::now().time_since_epoch()).count() + interval, interval, execCount,
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)));
}
Timer::~Timer() {
    stop();
    wait();
}

void Timer::run() {
    while (allowRunning()) {
        // sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_));
        while (true) {
            TimerItem ti;
            if (!safeQueue_->getNoCondition(ti)) {
                break;
            }
            auto timePoint = ti.timePoint_;
            multimapTimer_.insert(std::pair<uint64_t, TimerItem>(timePoint, std::move(ti)));
        }
        for (auto it = multimapTimer_.begin(); it != multimapTimer_.end(); ) {
            auto timePoint = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (timePoint >= it->first) {
                it->second.func_();
                if (it->second.execCount_ > 0) {
                    it->second.execCount_--;
                }
                if (it->second.execCount_ != 0) {
                    it->second.timePoint_ = timePoint + it->second.interval_;
                    safeQueue_->pushNoCondition(std::move(it->second));
                }
                it = multimapTimer_.erase(it);
            }
            else {
                break;
            }
        }
    }
}

void printContext() {
    std::cout << "print" << std::endl;
}