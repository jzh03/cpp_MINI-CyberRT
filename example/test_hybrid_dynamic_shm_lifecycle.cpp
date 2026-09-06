// 构建（在 example 目录）：make test_hybrid_dynamic_shm_lifecycle
// 运行（在 example 目录）：CMW_PATH="$(cd .. && pwd)" ./build/bin/test_hybrid_dynamic_shm_lifecycle

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <thread>

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/discovery/topology_manager.h>
#include <cmw/init.h>
#include <cmw/node/publisher.h>
#include <cmw/node/subscriber.h>
#include <cmw/serialize/serializable.h>

namespace hnu {
namespace cmw {

namespace {

struct HybridDynamicShmMessage : public serialize::Serializable {
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
  attr.message_type = "HybridDynamicShmMessage";
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

bool WaitForChild(pid_t child, int* status, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(child, status, WNOHANG);
    if (result == child) {
      return true;
    }
    if (result == -1) {
      return false;
    }
    poll(nullptr, 0, 20);
  }
  return false;
}

void StopChild(pid_t child, int* status) {
  if (child <= 0) {
    return;
  }
  if (!WaitForChild(child, status, 100)) {
    kill(child, SIGKILL);
    waitpid(child, status, 0);
  }
}

int RunPublisher(const std::string& channel_name, int control_read_fd,
                 int status_write_fd) {
  hnu::cmw::Init("HybridDynamicShmPublisher");
  discovery::TopologyManager::Instance();
  if (!WriteByte(status_write_fd, 'R')) {
    return 2;
  }
  std::unique_ptr<Publisher<HybridDynamicShmMessage>> publisher;
  while (true) {
    pollfd descriptor{control_read_fd, POLLIN, 0};
    if (poll(&descriptor, 1, 15000) != 1 || !(descriptor.revents & POLLIN)) {
      return 3;
    }
    char command = 0;
    if (read(control_read_fd, &command, sizeof(command)) != sizeof(command)) {
      return 4;
    }
    if (command == 'X') {
      if (publisher != nullptr) {
        publisher->Shutdown();
      }
      return 0;
    }
    if (command == 'J') {
      if (publisher == nullptr) {
        publisher.reset(new Publisher<HybridDynamicShmMessage>(
            MakeRoleAttributes(channel_name, "_publisher")));
        if (!publisher->Init()) {
          return 5;
        }
      }
      if (!WriteByte(status_write_fd, 'J')) {
        publisher->Shutdown();
        return 6;
      }
      continue;
    }
    if (publisher == nullptr) {
      return 7;
    }
    if (command == 'W') {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(5);
      while (std::chrono::steady_clock::now() < deadline &&
             publisher->HasSubscriber()) {
        poll(nullptr, 0, 20);
      }
      if (!WriteByte(status_write_fd,
                     publisher->HasSubscriber() ? 'F' : 'L')) {
        publisher->Shutdown();
        return 8;
      }
      continue;
    }
    if (command == 'H') {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(5);
      bool stopped = false;
      while (std::chrono::steady_clock::now() < deadline) {
        HybridDynamicShmMessage message;
        message.payload = "hybrid-dynamic-shm-ready";
        publisher->Publish(message);
        if (ReadByteWithTimeout(control_read_fd, 'C', 100)) {
          stopped = true;
          break;
        }
      }
      if (!WriteByte(status_write_fd, stopped ? 'H' : 'F')) {
        publisher->Shutdown();
        return 9;
      }
      continue;
    }
    if (command >= '1' && command <= '9') {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(5);
      while (std::chrono::steady_clock::now() < deadline &&
             !publisher->HasSubscriber()) {
        poll(nullptr, 0, 20);
      }
      if (!publisher->HasSubscriber()) {
        if (!WriteByte(status_write_fd, 'F')) {
          publisher->Shutdown();
          return 10;
        }
        continue;
      }
      HybridDynamicShmMessage message;
      message.sequence = static_cast<uint64_t>(command - '0');
      message.payload = "hybrid-dynamic-shm-" +
                        std::to_string(message.sequence);
      if (!publisher->Publish(message) || !WriteByte(status_write_fd, 'P')) {
        publisher->Shutdown();
        return 11;
      }
      continue;
    }
    publisher->Shutdown();
    return 12;
  }
}

int RunSubscriber(const std::string& channel_name, uint64_t sequence,
                  int control_read_fd, int status_write_fd) {
  hnu::cmw::Init("HybridDynamicShmSubscriber");
  discovery::TopologyManager::Instance();
  if (!WriteByte(status_write_fd, 'R')) {
    return 2;
  }
  char command = 0;
  if (read(control_read_fd, &command, sizeof(command)) != sizeof(command)) {
    return 3;
  }
  if (command == 'X') {
    return 0;
  }
  if (command != 'J') {
    return 4;
  }
  std::mutex mutex;
  std::condition_variable condition;
  bool handshake_received = false;
  uint32_t receive_count = 0;
  Subscriber<HybridDynamicShmMessage> subscriber(
      MakeRoleAttributes(channel_name, "_subscriber_" +
                                           std::to_string(sequence)),
      [&](const std::shared_ptr<HybridDynamicShmMessage>& message) {
        bool notify_handshake = false;
        std::lock_guard<std::mutex> lock(mutex);
        if (!handshake_received &&
            message->payload == "hybrid-dynamic-shm-ready") {
          handshake_received = true;
          notify_handshake = true;
        }
        if (message->sequence == sequence &&
            message->payload == "hybrid-dynamic-shm-" +
                                    std::to_string(sequence)) {
          ++receive_count;
          condition.notify_one();
        }
        if (notify_handshake) {
          WriteByte(status_write_fd, 'H');
        }
      });
  if (!subscriber.Init()) {
    return 5;
  }
  // 等待 Subscriber routine 进入 DATA_WAIT，避免 SHM 首条通知早于 listener 就绪。
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  if (!WriteByte(status_write_fd, 'J')) {
    subscriber.Shutdown();
    return 6;
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (!condition.wait_for(lock, std::chrono::seconds(10), [&]() {
          return receive_count == 1;
        })) {
      subscriber.Shutdown();
      return 7;
    }
  }
  if (!WriteByte(status_write_fd, 'D')) {
    subscriber.Shutdown();
    return 8;
  }
  if (!ReadByteWithTimeout(control_read_fd, 'X', 5000)) {
    subscriber.Shutdown();
    return 9;
  }

  subscriber.Shutdown();
  std::lock_guard<std::mutex> lock(mutex);
  return receive_count == 1 ? 0 : 10;
}

pid_t StartSubscriber(const std::string& channel_name, uint64_t sequence,
                      int control_pipe[2], int status_pipe[2],
                      int publisher_control_fd, int publisher_status_fd) {
  if (pipe(control_pipe) != 0 || pipe(status_pipe) != 0) {
    return -1;
  }
  const pid_t child = fork();
  if (child == 0) {
    close(control_pipe[1]);
    close(status_pipe[0]);
    close(publisher_control_fd);
    close(publisher_status_fd);
    const int result =
        RunSubscriber(channel_name, sequence, control_pipe[0], status_pipe[1]);
    close(control_pipe[0]);
    close(status_pipe[1]);
    _exit(result);
  }
  close(control_pipe[0]);
  close(status_pipe[1]);
  return child;
}

TEST(HybridDynamicShmLifecycleTest, SubscriberLeaveThenNewSubscriberJoins) {
  // 父进程不初始化 CMW，所有参与通信的进程均在 fork 后获得自己的 PID。
  const std::string channel_name =
      "hybrid_dynamic_shm_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  int publisher_control_pipe[2] = {-1, -1};
  int publisher_status_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(publisher_control_pipe));
  ASSERT_EQ(0, pipe(publisher_status_pipe));

  const pid_t publisher_child = fork();
  ASSERT_NE(-1, publisher_child);
  if (publisher_child == 0) {
    close(publisher_control_pipe[1]);
    close(publisher_status_pipe[0]);
    const int result = RunPublisher(channel_name, publisher_control_pipe[0],
                                    publisher_status_pipe[1]);
    close(publisher_control_pipe[0]);
    close(publisher_status_pipe[1]);
    _exit(result);
  }
  close(publisher_control_pipe[0]);
  close(publisher_status_pipe[1]);
  signal(SIGPIPE, SIG_IGN);

  const bool publisher_ready =
      ReadByteWithTimeout(publisher_status_pipe[0], 'R', 5000);
  int subscriber_a_control_pipe[2] = {-1, -1};
  int subscriber_a_status_pipe[2] = {-1, -1};
  pid_t subscriber_a = -1;
  bool subscriber_a_ready = false;
  bool subscriber_a_connected = false;
  bool subscriber_a_received = false;
  bool subscriber_a_stopped = false;
  bool leave_observed = false;
  int subscriber_b_control_pipe[2] = {-1, -1};
  int subscriber_b_status_pipe[2] = {-1, -1};
  pid_t subscriber_b = -1;
  bool subscriber_b_ready = false;
  bool subscriber_b_joined = false;
  bool subscriber_b_connected = false;
  bool subscriber_b_received = false;
  bool subscriber_b_stopped = false;

  if (publisher_ready) {
    subscriber_a = StartSubscriber(channel_name, 1, subscriber_a_control_pipe,
                                   subscriber_a_status_pipe,
                                   publisher_control_pipe[1],
                                   publisher_status_pipe[0]);
    subscriber_a_ready = subscriber_a > 0 &&
                         ReadByteWithTimeout(subscriber_a_status_pipe[0], 'R', 5000);
    subscriber_b = StartSubscriber(channel_name, 2, subscriber_b_control_pipe,
                                   subscriber_b_status_pipe,
                                   publisher_control_pipe[1],
                                   publisher_status_pipe[0]);
    subscriber_b_ready = subscriber_b > 0 &&
                         ReadByteWithTimeout(subscriber_b_status_pipe[0], 'R', 5000);
  }
  bool publisher_joined = false;
  if (subscriber_a_ready && subscriber_b_ready) {
    // 三个 Discovery endpoint 都已启动后再广播 Writer JOIN，避免无 durability
    // 的 Discovery 丢失初始 role 消息。
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    publisher_joined = WriteByte(publisher_control_pipe[1], 'J') &&
                       ReadByteWithTimeout(publisher_status_pipe[0], 'J', 5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  if (publisher_joined) {
    subscriber_a_connected =
        WriteByte(subscriber_a_control_pipe[1], 'J') &&
        ReadByteWithTimeout(subscriber_a_status_pipe[0], 'J', 5000);
  }
  if (subscriber_a_connected) {
    subscriber_a_connected = WriteByte(publisher_control_pipe[1], 'H') &&
                             ReadByteWithTimeout(subscriber_a_status_pipe[0], 'H',
                                                 10000) &&
                             WriteByte(publisher_control_pipe[1], 'C') &&
                             ReadByteWithTimeout(publisher_status_pipe[0], 'H', 5000);
  }
  if (subscriber_a_connected) {
    const bool published = WriteByte(publisher_control_pipe[1], '1') &&
                           ReadByteWithTimeout(publisher_status_pipe[0], 'P', 5000);
    subscriber_a_received = published &&
                            ReadByteWithTimeout(subscriber_a_status_pipe[0], 'D', 10000);
    if (subscriber_a_received) {
      WriteByte(subscriber_a_control_pipe[1], 'X');
      int status = 0;
      subscriber_a_stopped = WaitForChild(subscriber_a, &status, 5000) &&
                             WIFEXITED(status) && WEXITSTATUS(status) == 0;
      if (subscriber_a_stopped) {
        subscriber_a = -1;
      }
    }
  }
  close(subscriber_a_control_pipe[1]);
  close(subscriber_a_status_pipe[0]);
  if (subscriber_a > 0) {
    int status = 0;
    StopChild(subscriber_a, &status);
    subscriber_a = -1;
  }

  if (subscriber_a_stopped) {
    leave_observed = WriteByte(publisher_control_pipe[1], 'W') &&
                     ReadByteWithTimeout(publisher_status_pipe[0], 'L', 5000);
  }
  if (leave_observed) {
    subscriber_b_joined = WriteByte(subscriber_b_control_pipe[1], 'J') &&
                          ReadByteWithTimeout(subscriber_b_status_pipe[0], 'J', 5000);
  }
  if (subscriber_b_joined) {
    subscriber_b_connected = WriteByte(publisher_control_pipe[1], 'H') &&
                             ReadByteWithTimeout(subscriber_b_status_pipe[0], 'H',
                                                 10000) &&
                             WriteByte(publisher_control_pipe[1], 'C') &&
                             ReadByteWithTimeout(publisher_status_pipe[0], 'H', 5000);
  }
  if (subscriber_b_connected) {
    const bool published = WriteByte(publisher_control_pipe[1], '2') &&
                           ReadByteWithTimeout(publisher_status_pipe[0], 'P', 5000);
    subscriber_b_received = published &&
                            ReadByteWithTimeout(subscriber_b_status_pipe[0], 'D', 10000);
    if (subscriber_b_received) {
      WriteByte(subscriber_b_control_pipe[1], 'X');
      int status = 0;
      subscriber_b_stopped = WaitForChild(subscriber_b, &status, 5000) &&
                             WIFEXITED(status) && WEXITSTATUS(status) == 0;
      if (subscriber_b_stopped) {
        subscriber_b = -1;
      }
    }
  }
  close(subscriber_b_control_pipe[1]);
  close(subscriber_b_status_pipe[0]);
  if (subscriber_b > 0) {
    int status = 0;
    StopChild(subscriber_b, &status);
  }

  const bool publisher_stopping = WriteByte(publisher_control_pipe[1], 'X');
  int publisher_status = 0;
  const bool publisher_stopped =
      WaitForChild(publisher_child, &publisher_status, 5000) &&
      WIFEXITED(publisher_status) && WEXITSTATUS(publisher_status) == 0;
  close(publisher_control_pipe[1]);
  close(publisher_status_pipe[0]);
  if (!publisher_stopped) {
    StopChild(publisher_child, &publisher_status);
  }

  EXPECT_TRUE(publisher_ready);
  EXPECT_TRUE(subscriber_a_ready);
  EXPECT_TRUE(subscriber_a_connected);
  EXPECT_TRUE(subscriber_a_received);
  EXPECT_TRUE(subscriber_a_stopped);
  EXPECT_TRUE(leave_observed);
  EXPECT_TRUE(subscriber_b_ready);
  EXPECT_TRUE(subscriber_b_joined);
  EXPECT_TRUE(subscriber_b_connected);
  EXPECT_TRUE(subscriber_b_received);
  EXPECT_TRUE(subscriber_b_stopped);
  EXPECT_TRUE(publisher_stopping);
  EXPECT_TRUE(publisher_stopped);
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
