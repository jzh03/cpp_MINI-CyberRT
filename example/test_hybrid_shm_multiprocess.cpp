// 构建（在 example 目录）：make test_hybrid_shm_multiprocess
// 运行（在 example 目录）：CMW_PATH="$(cd .. && pwd)" ./build/bin/test_hybrid_shm_multiprocess

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/discovery/topology_manager.h>
#include <cmw/init.h>
#include <cmw/scheduler/scheduler_factory.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/node/publisher.h>
#include <cmw/node/subscriber.h>
#include <cmw/serialize/serializable.h>

namespace hnu {
namespace cmw {

namespace {

// 验证同主机不同 PID 经 Discovery 自动选择 SHM 的真实双进程链路。
struct HybridShmMessage : public serialize::Serializable {
  uint64_t sequence = 0;
  std::string payload;

  SERIALIZE(sequence, payload)
};

RoleAttributes MakeRoleAttributes(const std::string& channel_name,
                                  const std::string& node_suffix) {
  auto global_data = common::GlobalData::Instance();
  RoleAttributes attr{};
  attr.channel_name = channel_name;
  attr.channel_id = common::GlobalData::RegisterChannel(channel_name);
  attr.host_name = global_data->HostName();
  attr.host_ip = global_data->HostIp();
  attr.process_id = global_data->ProcessId();
  attr.node_name = channel_name + node_suffix;
  attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
  attr.id = common::GlobalData::GenerateHashId(attr.node_name);
  attr.message_type = "HybridShmMessage";
  return attr;
}

bool ReadByteWithTimeout(int fd, char expected, int timeout_ms) {
  pollfd descriptor{fd, POLLIN, 0};
  if (poll(&descriptor, 1, timeout_ms) != 1 || !(descriptor.revents & POLLIN)) {
    return false;
  }
  char value = 0;
  return read(fd, &value, sizeof(value)) == sizeof(value) && value == expected;
}

bool WriteByte(int fd, char value) {
  return write(fd, &value, sizeof(value)) == sizeof(value);
}

int RunPublisher(const std::string& channel_name, int control_read_fd,
                 int ready_write_fd) {
  hnu::cmw::Init("HybridShmPublisher");
  discovery::TopologyManager::Instance();
  if (!WriteByte(ready_write_fd, 'R')) {
    return 2;
  }
  if (!ReadByteWithTimeout(control_read_fd, 'S', 5000)) {
    return 3;
  }

  Publisher<HybridShmMessage> publisher(
      MakeRoleAttributes(channel_name, "_publisher"));
  if (!publisher.Init()) {
    return 4;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  uint64_t sequence = 1;
  while (std::chrono::steady_clock::now() < deadline) {
    HybridShmMessage message;
    message.sequence = sequence;
    message.payload = "hybrid-shm-" + std::to_string(sequence);
    publisher.Publish(message);
    if (sequence == 3) {
      sequence = 1;
    } else {
      ++sequence;
    }
    if (ReadByteWithTimeout(control_read_fd, 'A', 100)) {
      publisher.Shutdown();
      return 0;
    }
  }

  publisher.Shutdown();
  return 5;
}

TEST(HybridShmMultiprocessTest, SubscriberFirstAutomaticallyUsesSameHostTransport) {
  // fork 前不能初始化 CMW/GlobalData，否则子进程会继承父进程缓存的 PID。
  const std::string channel_name =
      "hybrid_shm_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  int control_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(control_pipe));
  int ready_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(ready_pipe));

  const pid_t child = fork();
  ASSERT_NE(-1, child);
  if (child == 0) {
    close(control_pipe[1]);
    close(ready_pipe[0]);
    const int result =
        RunPublisher(channel_name, control_pipe[0], ready_pipe[1]);
    close(control_pipe[0]);
    close(ready_pipe[1]);
    _exit(result);
  }

  close(control_pipe[0]);
  close(ready_pipe[1]);
  signal(SIGPIPE, SIG_IGN);
  hnu::cmw::Init("HybridShmSubscriber");
  discovery::TopologyManager::Instance();
  if (!ReadByteWithTimeout(ready_pipe[0], 'R', 5000)) {
    close(control_pipe[1]);
    close(ready_pipe[0]);
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    FAIL() << "publisher discovery initialization timed out";
  }
  close(ready_pipe[0]);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  std::mutex mutex;
  std::condition_variable condition;
  std::vector<HybridShmMessage> received;
  Subscriber<HybridShmMessage> subscriber(
      MakeRoleAttributes(channel_name, "_subscriber"),
      [&](const std::shared_ptr<HybridShmMessage>& message) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(*message);
        condition.notify_one();
      });
  if (!subscriber.Init()) {
    close(control_pipe[1]);
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    FAIL() << "subscriber initialization failed";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  if (!WriteByte(control_pipe[1], 'S')) {
    subscriber.Shutdown();
    close(control_pipe[1]);
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    FAIL() << "could not start publisher child";
  }

  bool delivered = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    delivered = condition.wait_for(lock, std::chrono::seconds(10), [&]() {
      bool have_sequence[4] = {false, false, false, false};
      for (const auto& message : received) {
        if (message.sequence >= 1 && message.sequence <= 3 &&
            message.payload == "hybrid-shm-" + std::to_string(message.sequence)) {
          have_sequence[message.sequence] = true;
        }
      }
      return have_sequence[1] && have_sequence[2] && have_sequence[3];
    });
  }
  EXPECT_TRUE(delivered);
  EXPECT_TRUE(WriteByte(control_pipe[1], 'A'));
  subscriber.Shutdown();

  int status = 0;
  pid_t finished = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    finished = waitpid(child, &status, WNOHANG);
    if (finished != 0) {
      break;
    }
    poll(nullptr, 0, 20);
  }
  close(control_pipe[1]);
  if (finished == 0) {
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    FAIL() << "publisher child exceeded the watchdog deadline";
  }
  ASSERT_EQ(child, finished);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  // 父子进程均在 fork 后初始化，保证 Discovery 获取各自真实 PID。
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  // Join workers before process-static ClassicContext tables are destroyed.
  hnu::cmw::scheduler::Instance()->Shutdown();
  auto dispatcher = hnu::cmw::transport::ShmDispatcher::Instance(false);
  if(dispatcher != nullptr) dispatcher->Shutdown();
  return result;
}
