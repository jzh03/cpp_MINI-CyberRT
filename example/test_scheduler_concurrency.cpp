#include <cmw/common/global_data.h>
#include <cmw/common/log.h>
#include <cmw/croutine/croutine.h>
#include <cmw/scheduler/scheduler_factory.h>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace {

using hnu::cmw::common::GlobalData;
using hnu::cmw::croutine::CRoutine;
using hnu::cmw::scheduler::Scheduler;

bool TestNotifyAcrossReadyToWait(Scheduler* scheduler) {
  const std::string name = "scheduler_notify_ready_to_wait_regression";
  std::promise<void> running;
  auto running_future = running.get_future();
  std::promise<void> resumed;
  auto resumed_future = resumed.get_future();
  std::atomic<bool> allow_hang{false};

  if (!scheduler->CreateTask([&]() {
        running.set_value();
        while (!allow_hang.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        CRoutine::GetCurrentRoutine()->HangUp();
        resumed.set_value();
      }, name)) {
    return false;
  }

  if (running_future.wait_for(std::chrono::seconds(2)) !=
      std::future_status::ready) {
    allow_hang.store(true, std::memory_order_release);
    scheduler->RemoveTask(name);
    return false;
  }

  // The routine is still READY and running here. The notification must remain
  // recorded when it subsequently transitions to DATA_WAIT in HangUp().
  const uint64_t task_id = GlobalData::GenerateHashId(name);
  const bool notified = scheduler->NotifyTask(task_id);
  allow_hang.store(true, std::memory_order_release);
  const bool woke = resumed_future.wait_for(std::chrono::seconds(2)) ==
                    std::future_status::ready;
  const bool removed = scheduler->RemoveTask(name);
  return notified && woke && removed;
}

bool TestRemoveWaitsForRunningRoutine(Scheduler* scheduler) {
  const std::string name = "scheduler_remove_running_regression";
  std::promise<void> running;
  auto running_future = running.get_future();
  std::atomic<bool> finish{false};

  if (!scheduler->CreateTask([&]() {
        running.set_value();
        while (!finish.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
      }, name)) {
    return false;
  }
  if (running_future.wait_for(std::chrono::seconds(2)) !=
      std::future_status::ready) {
    finish.store(true, std::memory_order_release);
    scheduler->RemoveTask(name);
    return false;
  }

  auto removing = std::async(std::launch::async,
                             [&]() { return scheduler->RemoveTask(name); });
  const bool waited = removing.wait_for(std::chrono::milliseconds(10)) ==
                      std::future_status::timeout;
  finish.store(true, std::memory_order_release);
  const bool completed = removing.wait_for(std::chrono::seconds(2)) ==
                         std::future_status::ready;
  return waited && completed && removing.get();
}

}  // namespace

int main() {
  Logger_Init("scheduler_concurrency");
  Logger::Instance()->console(false);
  Scheduler* scheduler = hnu::cmw::scheduler::Instance();
  const bool passed = scheduler != nullptr &&
                      TestNotifyAcrossReadyToWait(scheduler) &&
                      TestRemoveWaitsForRunningRoutine(scheduler);
  if (scheduler != nullptr) scheduler->Shutdown();
  if (!passed) {
    std::cerr << "scheduler concurrency regression failed" << std::endl;
    return 1;
  }
  std::cout << "scheduler concurrency regression passed" << std::endl;
  return 0;
}
