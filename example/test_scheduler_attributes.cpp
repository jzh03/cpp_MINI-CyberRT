#include <gtest/gtest.h>
#include <cmw/scheduler/common/pin_thread.h>

#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <future>
#include <mutex>

namespace {
int last_policy = -1;
int last_priority = -1;
int sched_calls = 0;
int forced_sched_error = 0;
bool force_nice_error = false;
}

extern "C" int __real_pthread_setschedparam(pthread_t, int, const sched_param*);
extern "C" int __wrap_pthread_setschedparam(pthread_t thread, int policy,
                                           const sched_param* parameter) {
  last_policy = policy;
  last_priority = parameter->sched_priority;
  ++sched_calls;
  if (forced_sched_error) return forced_sched_error;
  return __real_pthread_setschedparam(thread, policy, parameter);
}
extern "C" int __real_setpriority(__priority_which_t, id_t, int);
extern "C" int __wrap_setpriority(__priority_which_t which, id_t who, int priority) {
  if (force_nice_error) { errno = EPERM; return -1; }
  return __real_setpriority(which, who, priority);
}

namespace {
using namespace hnu::cmw::scheduler;
struct Worker {
  std::mutex mutex;
  std::condition_variable cv;
  bool stop = false;
  std::thread thread;
  pid_t tid;
  Worker() {
    std::promise<pid_t> started;
    auto ready = started.get_future();
    thread = std::thread([this, &started] {
      std::unique_lock<std::mutex> lock(mutex);
      started.set_value(static_cast<pid_t>(syscall(SYS_gettid)));
      cv.wait(lock, [this] { return stop; });
    });
    tid = ready.get();
  }
  ~Worker() {
    { std::lock_guard<std::mutex> lock(mutex); stop = true; }
    cv.notify_one();
    thread.join();
  }
  std::vector<int> Cpus() {
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    EXPECT_EQ(pthread_getaffinity_np(thread.native_handle(), sizeof(cpus), &cpus), 0);
    std::vector<int> result;
    for (int i = 0; i < CPU_SETSIZE; ++i)
      if (CPU_ISSET(i, &cpus)) result.push_back(i);
    return result;
  }
};

TEST(SchedulerAttributes, RangeAndOneToOneApplyActualMasks) {
  Worker worker;
  auto allowed = worker.Cpus();
  ASSERT_FALSE(allowed.empty());
  ASSERT_TRUE(SetSchedAffinity(&worker.thread, {allowed.front()}, "range"));
  EXPECT_EQ(worker.Cpus(), (std::vector<int>{allowed.front()}));
  if (allowed.size() >= 2) {
    std::vector<int> pair{allowed[0], allowed[1]};
    ASSERT_TRUE(SetSchedAffinity(&worker.thread, pair, "1to1", 1));
    EXPECT_EQ(worker.Cpus(), (std::vector<int>{allowed[1]}));
    ASSERT_TRUE(SetSchedAffinity(&worker.thread, pair, "range"));
    EXPECT_EQ(worker.Cpus(), pair);
  }
  ASSERT_TRUE(SetSchedAffinity(&worker.thread, allowed, "range"));
}

TEST(SchedulerAttributes, InvalidInputsAreRejectedWithoutChangingAffinity) {
  Worker worker;
  const auto before = worker.Cpus();
  EXPECT_FALSE(SetSchedAffinity(nullptr, before, "range"));
  EXPECT_FALSE(SetSchedAffinity(&worker.thread, {-1}, "range"));
  EXPECT_FALSE(SetSchedAffinity(&worker.thread, {CPU_SETSIZE}, "range"));
  EXPECT_FALSE(SetSchedAffinity(&worker.thread, before, "1to1", -1));
  EXPECT_FALSE(SetSchedAffinity(&worker.thread, before, "1to1", before.size()));
  EXPECT_FALSE(SetSchedAffinity(&worker.thread, before, "unknown"));
  EXPECT_EQ(worker.Cpus(), before);
  std::vector<int> parsed{7};
  for (const auto& input : {"-1", "2-1", "0,,1", "0,", "0-1-2", "9999999999999999999", "abc"}) {
    EXPECT_FALSE(ParseCpuset(input, &parsed));
    EXPECT_EQ(parsed, (std::vector<int>{7}));
  }
  parsed.clear();
  EXPECT_TRUE(ParseCpuset("0-2, 4", &parsed));
  EXPECT_EQ(parsed, (std::vector<int>{0, 1, 2, 4}));
}

TEST(SchedulerAttributes, RealtimePriorityReachesSyscallAndErrorsPropagate) {
  Worker worker;
  for (const auto& policy : {std::string("SCHED_FIFO"), std::string("SCHED_RR")}) {
    const int native_policy = policy == "SCHED_FIFO" ? SCHED_FIFO : SCHED_RR;
    const int priority = sched_get_priority_min(native_policy);
    ASSERT_GT(priority, 0);
    const int before = sched_calls;
    EXPECT_FALSE(SetSchedPolicy(&worker.thread, policy, 0, worker.tid));
    EXPECT_EQ(sched_calls, before);
    // Deterministic permission failure verifies argument forwarding even on
    // hosts that cannot grant realtime scheduling to this test.
    forced_sched_error = EPERM;
    EXPECT_FALSE(SetSchedPolicy(&worker.thread, policy, priority, worker.tid));
    EXPECT_EQ(last_policy, native_policy);
    EXPECT_EQ(last_priority, priority);
    forced_sched_error = 0;
    const bool applied = SetSchedPolicy(&worker.thread, policy, priority, worker.tid);
    int actual_policy = -1;
    sched_param actual{};
    ASSERT_EQ(pthread_getschedparam(worker.thread.native_handle(), &actual_policy, &actual), 0);
    if (applied) {
      EXPECT_EQ(actual_policy, native_policy);
      EXPECT_EQ(actual.sched_priority, priority);
    } else {
      EXPECT_EQ(actual_policy, SCHED_OTHER);
      std::cout << "[LIMIT] host did not grant " << policy << "; failure was reported\n";
    }
    ASSERT_TRUE(SetSchedPolicy(&worker.thread, "SCHED_OTHER", 0, worker.tid));
  }
}

TEST(SchedulerAttributes, NiceUsesTargetTidAndReportsFailures) {
  Worker worker;
  const int before = sched_calls;
  EXPECT_FALSE(SetSchedPolicy(&worker.thread, "SCHED_OTHER", 0));
  EXPECT_EQ(sched_calls, before);
  EXPECT_FALSE(SetSchedPolicy(&worker.thread, "SCHED_OTHER", 20, worker.tid));
  EXPECT_FALSE(SetSchedPolicy(&worker.thread, "unknown", 0, worker.tid));
  force_nice_error = true;
  EXPECT_FALSE(SetSchedPolicy(&worker.thread, "SCHED_OTHER", 0, worker.tid));
  force_nice_error = false;
  errno = 0;
  const int initial = getpriority(PRIO_PROCESS, worker.tid);
  ASSERT_EQ(errno, 0);
  const int nice = std::min(initial + 1, 19);
  ASSERT_TRUE(SetSchedPolicy(&worker.thread, "SCHED_OTHER", nice, worker.tid));
  errno = 0;
  EXPECT_EQ(getpriority(PRIO_PROCESS, worker.tid), nice);
  EXPECT_EQ(errno, 0);
  int actual_policy;
  sched_param actual{};
  ASSERT_EQ(pthread_getschedparam(worker.thread.native_handle(), &actual_policy, &actual), 0);
  EXPECT_EQ(actual_policy, SCHED_OTHER);
  EXPECT_EQ(actual.sched_priority, 0);
}
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  Logger_Init("scheduler_attributes");
  Logger::Instance()->console(false);
  return RUN_ALL_TESTS();
}
