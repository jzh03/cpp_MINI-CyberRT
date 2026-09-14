#ifndef CMW_BASE_BOUNDED_QUEUE_H_
#define CMW_BASE_BOUNDED_QUEUE_H_

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#include <cmw/base/wait_strategy.h>

namespace hnu {
namespace cmw {
namespace base {

// A bounded MPMC queue. Queue metadata and every T object are protected by
// mutex_, so a slot cannot be recycled while another consumer is copying it.
template <typename T>
class BoundedQueue {
 public:
  using value_type = T;
  using size_type = uint64_t;

  BoundedQueue() = default;
  BoundedQueue& operator=(const BoundedQueue&) = delete;
  BoundedQueue(const BoundedQueue&) = delete;
  ~BoundedQueue() { BreakAllWait(); }

  bool Init(uint64_t size);
  bool Init(uint64_t size, WaitStrategy* strategy);
  bool Enqueue(const T& element);
  bool Enqueue(T&& element);
  bool WaitEnqueue(const T& element);
  bool WaitEnqueue(T&& element);
  bool Dequeue(T* element);
  bool WaitDequeue(T* element);
  uint64_t Size();
  bool Empty();
  void SetWaitStrategy(WaitStrategy* strategy);
  void BreakAllWait();
  uint64_t Head() const { return head_.load(std::memory_order_acquire); }
  uint64_t Tail() const { return tail_.load(std::memory_order_acquire); }
  uint64_t Commit() const { return commit_.load(std::memory_order_acquire); }

 private:
  template <typename U>
  bool EnqueueImpl(U&& element);

  mutable std::mutex mutex_;
  std::vector<T> pool_;
  uint64_t capacity_ = 0;
  uint64_t size_ = 0;
  uint64_t read_index_ = 0;
  uint64_t write_index_ = 0;
  std::unique_ptr<WaitStrategy> wait_strategy_;
  std::atomic<bool> break_all_wait_{false};

  // Preserve the original public counters: an empty queue starts at
  // head=0, tail=commit=1.
  std::atomic<uint64_t> head_{0};
  std::atomic<uint64_t> tail_{1};
  std::atomic<uint64_t> commit_{1};
};

template <typename T>
inline bool BoundedQueue<T>::Init(uint64_t size) {
  return Init(size, new SleepWaitStrategy());
}

template <typename T>
bool BoundedQueue<T>::Init(uint64_t size, WaitStrategy* strategy) {
  if (size == 0 || strategy == nullptr) {
    delete strategy;
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (capacity_ != 0) {
    delete strategy;
    return false;
  }
  if (size > pool_.max_size()) {
    delete strategy;
    return false;
  }
  try {
    pool_.resize(static_cast<std::size_t>(size));
  } catch (const std::exception&) {
    delete strategy;
    return false;
  }
  capacity_ = size;
  wait_strategy_.reset(strategy);
  break_all_wait_.store(false, std::memory_order_release);
  return true;
}

template <typename T>
template <typename U>
bool BoundedQueue<T>::EnqueueImpl(U&& element) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (break_all_wait_.load(std::memory_order_acquire) || capacity_ == 0 ||
        size_ >= capacity_) {
      return false;
    }
    pool_[write_index_] = std::forward<U>(element);
    write_index_ = (write_index_ + 1) % capacity_;
    ++size_;
    const uint64_t next = tail_.fetch_add(1, std::memory_order_acq_rel) + 1;
    commit_.store(next, std::memory_order_release);
  }
  wait_strategy_->NotifyOne();
  return true;
}

template <typename T>
bool BoundedQueue<T>::Enqueue(const T& element) {
  return EnqueueImpl(element);
}

template <typename T>
bool BoundedQueue<T>::Enqueue(T&& element) {
  return EnqueueImpl(std::move(element));
}

template <typename T>
bool BoundedQueue<T>::Dequeue(T* element) {
  if (element == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) {
      return false;
    }
    *element = std::move(pool_[read_index_]);
    read_index_ = (read_index_ + 1) % capacity_;
    --size_;
    head_.fetch_add(1, std::memory_order_release);
  }
  wait_strategy_->NotifyOne();
  return true;
}

template <typename T>
bool BoundedQueue<T>::WaitEnqueue(const T& element) {
  if (!wait_strategy_) return false;
  while (!break_all_wait_.load(std::memory_order_acquire)) {
    const uint64_t observed = wait_strategy_->PrepareWait();
    if (Enqueue(element)) return true;
    if (!wait_strategy_->EmptyWait(observed)) return false;
  }
  return false;
}

template <typename T>
bool BoundedQueue<T>::WaitEnqueue(T&& element) {
  if (!wait_strategy_) return false;
  while (!break_all_wait_.load(std::memory_order_acquire)) {
    const uint64_t observed = wait_strategy_->PrepareWait();
    if (Enqueue(std::move(element))) return true;
    if (!wait_strategy_->EmptyWait(observed)) return false;
  }
  return false;
}

template <typename T>
bool BoundedQueue<T>::WaitDequeue(T* element) {
  if (!wait_strategy_) return false;
  while (!break_all_wait_.load(std::memory_order_acquire)) {
    const uint64_t observed = wait_strategy_->PrepareWait();
    if (Dequeue(element)) return true;
    if (!wait_strategy_->EmptyWait(observed)) return false;
  }
  return false;
}

template <typename T>
uint64_t BoundedQueue<T>::Size() {
  std::lock_guard<std::mutex> lock(mutex_);
  return size_;
}

template <typename T>
bool BoundedQueue<T>::Empty() {
  std::lock_guard<std::mutex> lock(mutex_);
  return size_ == 0;
}

template <typename T>
void BoundedQueue<T>::SetWaitStrategy(WaitStrategy* strategy) {
  if (strategy == nullptr) return;
  // Configuration only: callers must stop queue operations before replacing
  // the strategy, just as they must before destroying the queue itself.
  std::lock_guard<std::mutex> lock(mutex_);
  wait_strategy_.reset(strategy);
}

template <typename T>
void BoundedQueue<T>::BreakAllWait() {
  if (break_all_wait_.exchange(true, std::memory_order_acq_rel)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (wait_strategy_) wait_strategy_->BreakAllWait();
}

}  // namespace base
}  // namespace cmw
}  // namespace hnu

#endif
