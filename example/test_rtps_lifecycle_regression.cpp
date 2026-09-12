// 构建（在 example 目录）：make test_rtps_lifecycle_regression
// 运行（在 example 目录）：CMW_PATH="$(cd .. && pwd)" ./build/bin/test_rtps_lifecycle_regression

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/config/transport_config.h>
#include <cmw/init.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/transport.h>

namespace hnu {
namespace cmw {

namespace {

// 同机显式强制 RTPS 的生命周期回归消息，不代表跨主机自动路由。
struct RtpsLifecycleMessage : public serialize::Serializable {
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
  attr.message_type = "RtpsLifecycleMessage";
  return attr;
}

bool ReadByteWithTimeout(int fd, char expected, int timeout_ms) {
  pollfd descriptor{fd, POLLIN, 0};
  if (poll(&descriptor, 1, timeout_ms) != 1 ||
      !(descriptor.revents & POLLIN)) {
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

class ProcessGuard {
 public:
  explicit ProcessGuard(pid_t process) : process_(process) {}

  ~ProcessGuard() {
    if (process_ > 0) {
      int status = 0;
      if (!WaitForChild(process_, &status, 0)) {
        kill(process_, SIGKILL);
        waitpid(process_, &status, 0);
      }
    }
  }

  void Release() { process_ = -1; }

 private:
  pid_t process_;
};

int RunPublisher(const std::string& channel_name, int command_read_fd,
                 int event_write_fd) {
  hnu::cmw::Init("RtpsLifecyclePublisher");
  auto transmitter = transport::Transport::Instance()->CreateTransmitter<
      RtpsLifecycleMessage>(MakeRoleAttributes(channel_name, "_publisher"),
                            config::OptionalMode::RTPS);
  if (transmitter == nullptr) {
    return 2;
  }

  // 使每轮均从 Disable 后的 Enable 开始。
  transmitter->Disable();
  transmitter->Disable();
  if (!WriteByte(event_write_fd, 'R')) {
    return 3;
  }

  uint64_t sequence = 1;
  while (true) {
    pollfd descriptor{command_read_fd, POLLIN, 0};
    if (poll(&descriptor, 1, 15000) != 1 || !(descriptor.revents & POLLIN)) {
      transmitter->Disable();
      transport::Transport::Instance()->Shutdown();
      return 4;
    }
    char command = 0;
    if (read(command_read_fd, &command, sizeof(command)) != sizeof(command)) {
      transmitter->Disable();
      transport::Transport::Instance()->Shutdown();
      return 5;
    }
    if (command == 'E') {
      transmitter->Enable();
      transmitter->Enable();
      if (!WriteByte(event_write_fd, 'E')) {
        return 6;
      }
    } else if (command == 'H') {
      RtpsLifecycleMessage probe;
      probe.payload = "rtps-lifecycle-ready";
      if (!transmitter->Transmit(std::make_shared<RtpsLifecycleMessage>(probe)) ||
          !WriteByte(event_write_fd, 'H')) {
        return 12;
      }
    } else if (command == 'S') {
      RtpsLifecycleMessage message;
      message.sequence = sequence++;
      message.payload = "rtps-lifecycle-" + std::to_string(message.sequence);
      if (!transmitter->Transmit(std::make_shared<RtpsLifecycleMessage>(message))) {
        return 7;
      }
      if (!WriteByte(event_write_fd, 'S')) {
        return 8;
      }
    } else if (command == 'D') {
      transmitter->Disable();
      transmitter->Disable();
      if (!WriteByte(event_write_fd, 'D')) {
        return 9;
      }
    } else if (command == 'Q') {
      transmitter->Disable();
      transmitter->Disable();
      transport::Transport::Instance()->Shutdown();
      return WriteByte(event_write_fd, 'Q') ? 0 : 10;
    } else {
      return 11;
    }
  }
}

int RunSubscriber(const std::string& channel_name, uint64_t expected_sequence,
                  int ready_write_fd, int result_write_fd) {
  hnu::cmw::Init("RtpsLifecycleSubscriber");
  std::mutex mutex;
  std::condition_variable condition;
  size_t received = 0;
  bool matched = false;
  bool malformed = false;
  auto receiver = transport::Transport::Instance()->CreateReceiver<
      RtpsLifecycleMessage>(
      MakeRoleAttributes(channel_name,
                         "_subscriber_" + std::to_string(expected_sequence)),
      [&](const std::shared_ptr<RtpsLifecycleMessage>& message,
          const transport::MessageInfo&, const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(mutex);
        if (message->sequence == 0 && message->payload == "rtps-lifecycle-ready") {
          if (!matched) {
            matched = true;
            WriteByte(ready_write_fd, 'M');
          }
          return;
        }
        if (message->sequence == expected_sequence &&
            message->payload == "rtps-lifecycle-" +
                                    std::to_string(expected_sequence)) {
          ++received;
          condition.notify_one();
        } else {
          malformed = true;
          condition.notify_one();
        }
      },
      config::OptionalMode::RTPS);
  if (receiver == nullptr || !WriteByte(ready_write_fd, 'R')) {
    return 2;
  }

  bool received_once = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    received_once = condition.wait_for(lock, std::chrono::seconds(10), [&]() {
      return malformed || received == 1;
    });
  }
  // 单次发送后再留出短窗口，捕获失效 Writer 残留造成的重复投递。
  if (received_once) {
    poll(nullptr, 0, 300);
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    received_once = received_once && !malformed && received == 1;
  }
  receiver->Disable();
  transport::Transport::Instance()->Shutdown();
  return WriteByte(result_write_fd, received_once ? 'O' : 'F') ? 0 : 3;
}

TEST(RtpsLifecycleRegressionTest, ForcedRtpsEnableDisableAcrossSubscriberReconnects) {
  // 这是同主机强制 RTPS lifecycle/regression test，不是跨主机测试。
  const std::string channel_name =
      "rtps_lifecycle_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  int command_pipe[2] = {-1, -1};
  int event_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(command_pipe));
  ASSERT_EQ(0, pipe(event_pipe));

  const pid_t publisher = fork();
  ASSERT_NE(-1, publisher);
  if (publisher == 0) {
    close(command_pipe[1]);
    close(event_pipe[0]);
    const int result =
        RunPublisher(channel_name, command_pipe[0], event_pipe[1]);
    close(command_pipe[0]);
    close(event_pipe[1]);
    _exit(result);
  }
  ProcessGuard publisher_guard(publisher);

  close(command_pipe[0]);
  close(event_pipe[1]);
  signal(SIGPIPE, SIG_IGN);
  ASSERT_TRUE(ReadByteWithTimeout(event_pipe[0], 'R', 5000));

  int publisher_status = 0;
  for (uint64_t round = 1; round <= 3; ++round) {
    int ready_pipe[2] = {-1, -1};
    int result_pipe[2] = {-1, -1};
    ASSERT_EQ(0, pipe(ready_pipe));
    ASSERT_EQ(0, pipe(result_pipe));

    const pid_t subscriber = fork();
    ASSERT_NE(-1, subscriber);
    if (subscriber == 0) {
      close(command_pipe[1]);
      close(event_pipe[0]);
      close(ready_pipe[0]);
      close(result_pipe[0]);
      const int result = RunSubscriber(channel_name, round, ready_pipe[1],
                                       result_pipe[1]);
      close(ready_pipe[1]);
      close(result_pipe[1]);
      _exit(result);
    }
    ProcessGuard subscriber_guard(subscriber);

    close(ready_pipe[1]);
    close(result_pipe[1]);
    ASSERT_TRUE(ReadByteWithTimeout(ready_pipe[0], 'R', 5000));
    ASSERT_TRUE(WriteByte(command_pipe[1], 'E'));
    ASSERT_TRUE(ReadByteWithTimeout(event_pipe[0], 'E', 5000));
    // Confirm matching through real probe delivery. The actual regression
    // message below is still sent exactly once; do not retry its payload.
    bool matched = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!matched && std::chrono::steady_clock::now() < deadline) {
      ASSERT_TRUE(WriteByte(command_pipe[1], 'H'));
      ASSERT_TRUE(ReadByteWithTimeout(event_pipe[0], 'H', 5000));
      matched = ReadByteWithTimeout(ready_pipe[0], 'M', 100);
    }
    ASSERT_TRUE(matched);
    close(ready_pipe[0]);
    ASSERT_TRUE(WriteByte(command_pipe[1], 'S'));
    ASSERT_TRUE(ReadByteWithTimeout(event_pipe[0], 'S', 5000));
    ASSERT_TRUE(ReadByteWithTimeout(result_pipe[0], 'O', 7000));
    close(result_pipe[0]);

    int subscriber_status = 0;
    if (!WaitForChild(subscriber, &subscriber_status, 5000)) {
      kill(subscriber, SIGKILL);
      waitpid(subscriber, &subscriber_status, 0);
      ADD_FAILURE() << "subscriber watchdog expired in round " << round;
    } else {
      EXPECT_TRUE(WIFEXITED(subscriber_status));
      EXPECT_EQ(0, WEXITSTATUS(subscriber_status));
    }
    subscriber_guard.Release();

    ASSERT_TRUE(WriteByte(command_pipe[1], 'D'));
    ASSERT_TRUE(ReadByteWithTimeout(event_pipe[0], 'D', 5000));
  }

  EXPECT_TRUE(WriteByte(command_pipe[1], 'Q'));
  EXPECT_TRUE(ReadByteWithTimeout(event_pipe[0], 'Q', 5000));
  close(command_pipe[1]);
  close(event_pipe[0]);
  if (!WaitForChild(publisher, &publisher_status, 5000)) {
    kill(publisher, SIGKILL);
    waitpid(publisher, &publisher_status, 0);
    FAIL() << "publisher watchdog expired";
  }
  publisher_guard.Release();
  EXPECT_TRUE(WIFEXITED(publisher_status));
  EXPECT_EQ(0, WEXITSTATUS(publisher_status));
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
