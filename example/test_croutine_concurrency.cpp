#include <cmw/croutine/croutine.h>
#include <cmw/common/log.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>

namespace {

using hnu::cmw::croutine::CRoutine;
using hnu::cmw::croutine::RoutineState;

bool TestNotificationBeforeHangUp() {
  CRoutine routine([]() {});
  routine.set_state(RoutineState::READY);
  // SchedulerClassic::NotifyProcessor records the notification regardless of
  // the currently observed state.
  routine.SetUpdateFlag();
  routine.set_state(RoutineState::DATA_WAIT);
  if (!routine.Acquire()) return false;
  const RoutineState state = routine.UpdateState();
  routine.Release();
  return state == RoutineState::READY;
}

bool TestStopDuringResume() {
  std::atomic<bool> entered{false};
  std::atomic<bool> finish{false};
  auto routine = std::make_shared<CRoutine>([&]() {
    entered.store(true, std::memory_order_release);
    while (!finish.load(std::memory_order_acquire)) std::this_thread::yield();
  });

  std::atomic<RoutineState> result{RoutineState::READY};
  std::thread processor([&]() {
    if (!routine->Acquire()) return;
    result.store(routine->Resume(), std::memory_order_release);
    routine->Release();
  });
  while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();

  routine->Stop();
  for (int i = 0; i < 10000; ++i) {
    (void)routine->state();
  }
  finish.store(true, std::memory_order_release);
  processor.join();
  return result.load(std::memory_order_acquire) == RoutineState::FINISHED;
}

bool TestConcurrentStateAccess() {
  CRoutine routine([]() {});
  std::atomic<bool> start{false};
  std::thread writer([&]() {
    while (!start.load(std::memory_order_acquire)) {}
    for (int i = 0; i < 100000; ++i) {
      routine.set_state((i & 1) ? RoutineState::READY
                                : RoutineState::DATA_WAIT);
    }
  });
  std::thread reader([&]() {
    start.store(true, std::memory_order_release);
    for (int i = 0; i < 100000; ++i) (void)routine.state();
  });
  writer.join();
  reader.join();
  return true;
}

bool TestConcurrentStopAndResumeCheck() {
  CRoutine routine([]() {});
  std::atomic<bool> start{false};
  std::thread stopper([&]() {
    while (!start.load(std::memory_order_acquire)) {}
    for (int i = 0; i < 100000; ++i) routine.Stop();
  });
  std::thread processor([&]() {
    start.store(true, std::memory_order_release);
    for (int i = 0; i < 100000; ++i) (void)routine.Resume();
  });
  stopper.join();
  processor.join();
  return routine.state() == RoutineState::FINISHED;
}

}  // namespace

int main() {
  Logger_Init("croutine_concurrency");
  Logger::Instance()->console(false);
  if (!TestNotificationBeforeHangUp() || !TestStopDuringResume() ||
      !TestConcurrentStateAccess() || !TestConcurrentStopAndResumeCheck()) {
    std::cerr << "croutine concurrency regression failed" << std::endl;
    return 1;
  }
  std::cout << "croutine concurrency regression passed" << std::endl;
  return 0;
}
