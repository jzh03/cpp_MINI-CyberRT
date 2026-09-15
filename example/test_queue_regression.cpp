#include <cmw/base/bounded_queue.h>
#include <cmw/base/thread_pool.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct Payload {
  Payload() = default;
  explicit Payload(uint64_t value)
      : value(value), storage(std::make_shared<uint64_t>(value)) {}

  uint64_t value = 0;
  std::shared_ptr<uint64_t> storage;
};

bool TestMpmcWraparound() {
  constexpr uint64_t kProducerCount = 4;
  constexpr uint64_t kConsumerCount = 4;
  constexpr uint64_t kItemsPerProducer = 10000;
  constexpr uint64_t kTotal = kProducerCount * kItemsPerProducer;

  hnu::cmw::base::BoundedQueue<Payload> queue;
  if (!queue.Init(7, new hnu::cmw::base::YieldWaitStrategy())) return false;

  std::vector<std::atomic<uint32_t>> seen(kTotal);
  for (auto& count : seen) count.store(0, std::memory_order_relaxed);
  std::atomic<uint64_t> consumed{0};
  std::atomic<bool> failed{false};

  std::vector<std::thread> consumers;
  for (uint64_t i = 0; i < kConsumerCount; ++i) {
    consumers.emplace_back([&]() {
      while (consumed.load(std::memory_order_acquire) < kTotal) {
        Payload payload;
        if (!queue.Dequeue(&payload)) {
          std::this_thread::yield();
          continue;
        }
        if (!payload.storage || *payload.storage != payload.value ||
            payload.value >= kTotal) {
          failed.store(true, std::memory_order_release);
        } else if (seen[payload.value].fetch_add(1, std::memory_order_acq_rel) !=
                   0) {
          failed.store(true, std::memory_order_release);
        }
        consumed.fetch_add(1, std::memory_order_release);
      }
    });
  }

  std::vector<std::thread> producers;
  for (uint64_t producer = 0; producer < kProducerCount; ++producer) {
    producers.emplace_back([&, producer]() {
      for (uint64_t i = 0; i < kItemsPerProducer; ++i) {
        Payload payload(producer * kItemsPerProducer + i);
        while (!queue.Enqueue(std::move(payload))) std::this_thread::yield();
      }
    });
  }

  for (auto& producer : producers) producer.join();
  for (auto& consumer : consumers) consumer.join();
  if (failed.load(std::memory_order_acquire) || !queue.Empty()) return false;
  for (auto& count : seen) {
    if (count.load(std::memory_order_acquire) != 1) return false;
  }
  return true;
}

bool TestWaitAndBreak() {
  hnu::cmw::base::BoundedQueue<Payload> queue;
  if (!queue.Init(1, new hnu::cmw::base::BlockWaitStrategy())) return false;

  std::promise<void> started;
  auto started_future = started.get_future();
  std::atomic<bool> received{false};
  std::thread consumer([&]() {
    started.set_value();
    Payload payload;
    received.store(queue.WaitDequeue(&payload) && payload.value == 42,
                   std::memory_order_release);
  });
  started_future.wait();
  const bool enqueued = queue.Enqueue(Payload(42));
  if (!enqueued) queue.BreakAllWait();
  consumer.join();
  if (!enqueued || !received.load(std::memory_order_acquire)) return false;

  std::atomic<bool> stopped{false};
  std::thread waiter([&]() {
    Payload payload;
    stopped.store(!queue.WaitDequeue(&payload), std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  queue.BreakAllWait();
  waiter.join();
  return stopped.load(std::memory_order_acquire);
}

bool TestMixedWaitersAndNoStaleWake() {
  hnu::cmw::base::BoundedQueue<Payload> queue;
  if (!queue.Init(1, new hnu::cmw::base::BlockWaitStrategy())) return false;
  if (!queue.Enqueue(Payload(1))) return false;

  std::atomic<bool> producer_done{false};
  std::thread producer([&]() {
    producer_done.store(queue.WaitEnqueue(Payload(2)),
                        std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Payload payload;
  const bool first_ok = queue.Dequeue(&payload) && payload.value == 1;
  if (!first_ok) queue.BreakAllWait();
  producer.join();
  if (!first_ok || !producer_done.load(std::memory_order_acquire) ||
      !queue.Dequeue(&payload) || payload.value != 2) {
    return false;
  }

  // Fast-path activity must not accumulate wake tokens. The next empty wait
  // remains blocked until a new enqueue changes the strategy generation.
  for (uint64_t i = 0; i < 10000; ++i) {
    if (!queue.Enqueue(Payload(i)) || !queue.Dequeue(&payload)) return false;
  }
  auto waiting = std::async(std::launch::async, [&]() {
    Payload result;
    return queue.WaitDequeue(&result) && result.value == 99;
  });
  const bool remained_blocked =
      waiting.wait_for(std::chrono::milliseconds(10)) ==
      std::future_status::timeout;
  const bool enqueued = queue.Enqueue(Payload(99));
  if (!enqueued) queue.BreakAllWait();
  return remained_blocked && enqueued && waiting.get();
}

bool TestPreWaitNotification() {
  hnu::cmw::base::BlockWaitStrategy strategy;
  const uint64_t observed = strategy.PrepareWait();
  strategy.NotifyOne();
  auto result = std::async(std::launch::async,
                           [&]() { return strategy.EmptyWait(observed); });
  const bool ready = result.wait_for(std::chrono::milliseconds(50)) ==
                     std::future_status::ready;
  if (!ready) strategy.BreakAllWait();
  return ready && result.get();
}

bool TestUninitializedWaitFails() {
  hnu::cmw::base::BoundedQueue<Payload> queue;
  Payload payload;
  return !queue.WaitDequeue(&payload) && !queue.WaitEnqueue(Payload(1));
}

bool TestThreadPoolFullResult() {
  hnu::cmw::base::ThreadPool pool(1, 1);
  std::promise<void> entered;
  std::promise<void> release;
  auto release_future = release.get_future().share();
  auto first = pool.Enqueue([&]() {
    entered.set_value();
    release_future.wait();
    return 1;
  });
  entered.get_future().wait();
  auto second = pool.Enqueue([]() { return 2; });
  auto rejected = pool.Enqueue([]() { return 3; });
  const bool futures_ok = first.valid() && second.valid() && !rejected.valid();
  release.set_value();
  return futures_ok && first.get() == 1 && second.get() == 2;
}

}  // namespace

int main() {
  if (!TestMpmcWraparound() || !TestWaitAndBreak() ||
      !TestMixedWaitersAndNoStaleWake() || !TestPreWaitNotification() ||
      !TestUninitializedWaitFails() || !TestThreadPoolFullResult()) {
    std::cerr << "queue regression failed" << std::endl;
    return 1;
  }
  std::cout << "queue regression passed" << std::endl;
  return 0;
}
