
#ifndef CMW_BASE_WAIT_STRATEGY_H_
#define CMW_BASE_WAIT_STRATEGY_H_
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace hnu    {
namespace cmw   {
namespace base {

class WaitStrategy {
 public:
  virtual void NotifyOne() {}
  virtual void BreakAllWait() {}
  virtual uint64_t PrepareWait() { return 0; }
  virtual bool EmptyWait(uint64_t /*observed*/) { return EmptyWait(); }
  virtual bool EmptyWait() = 0;
  virtual ~WaitStrategy() {}
};

//阻塞等待策略
class BlockWaitStrategy : public WaitStrategy {
 public:
  BlockWaitStrategy() {}
  void NotifyOne() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    // A queue uses one strategy for not-empty and not-full waiters. Wake all
    // so the waiter whose predicate changed gets a chance to recheck it.
    cv_.notify_all();
  }

  bool EmptyWait() override {
    return EmptyWait(PrepareWait());
  }

  uint64_t PrepareWait() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
  }

  bool EmptyWait(uint64_t observed) override {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock,
             [this, observed]() { return broken_ || generation_ != observed; });
    return !broken_;
  }

  void BreakAllWait() override {
    std::lock_guard<std::mutex> lock(mutex_);
    broken_ = true;
    cv_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  uint64_t generation_ = 0;
  bool broken_ = false;
};

//睡眠等待策略
class SleepWaitStrategy : public WaitStrategy {
 public:
  SleepWaitStrategy() {}
  explicit SleepWaitStrategy(uint64_t sleep_time_us)
      : sleep_time_us_(sleep_time_us) {}

  bool EmptyWait() override {
    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time_us_));
    return true;
  }

  void SetSleepTimeMicroSeconds(uint64_t sleep_time_us) {
    sleep_time_us_ = sleep_time_us;
  }

 private:
  uint64_t sleep_time_us_ = 10000;
};

//调度等待策略
class YieldWaitStrategy : public WaitStrategy {
 public:
  YieldWaitStrategy() {}
  bool EmptyWait() override {
    std::this_thread::yield();
    return true;
  }
};


class BusySpinWaitStrategy : public WaitStrategy {
 public:
  BusySpinWaitStrategy() {}
  bool EmptyWait() override { return true; }
};

//超时阻塞等待策略
class TimeoutBlockWaitStrategy : public WaitStrategy {
 public:
  TimeoutBlockWaitStrategy() {}
  explicit TimeoutBlockWaitStrategy(uint64_t timeout)
      : time_out_(std::chrono::milliseconds(timeout)) {}

  void NotifyOne() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    cv_.notify_all();
  }

  bool EmptyWait() override {
    return EmptyWait(PrepareWait());
  }

  uint64_t PrepareWait() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
  }

  bool EmptyWait(uint64_t observed) override {
    /* 线程阻塞 如果超时了则返回false，没超时则返回false*/
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, time_out_,
                      [this, observed]() {
                        return broken_ || generation_ != observed;
                      })) {
      return false;
    }
    return !broken_;
  }

  void BreakAllWait() override {
    std::lock_guard<std::mutex> lock(mutex_);
    broken_ = true;
    cv_.notify_all();
  }

  void SetTimeout(uint64_t timeout) {
    time_out_ = std::chrono::milliseconds(timeout);
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::chrono::milliseconds time_out_{0};
  uint64_t generation_ = 0;
  bool broken_ = false;
};

}
}
}




#endif
