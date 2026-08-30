// 构建（在 example 目录）：make test_rtps_same_host_multiprocess
// 运行（在 example 目录）：CMW_PATH="$(cd .. && pwd)" ./build/bin/test_rtps_same_host_multiprocess

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// 验证同一主机上显式强制 RTPS 的双进程数据路径，不代表跨主机自动路由。
struct ForcedRtpsMessage : public serialize::Serializable {
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
  attr.message_type = "ForcedRtpsMessage";
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

int RunForcedRtpsPublisher(const std::string& channel_name, int control_read_fd) {
  if (!ReadByteWithTimeout(control_read_fd, 'S', 5000)) {
    return 2;
  }

  hnu::cmw::Init("ForcedRtpsPublisher");
  auto transmitter = transport::Transport::Instance()->CreateTransmitter<ForcedRtpsMessage>(
      MakeRoleAttributes(channel_name, "_publisher"), config::OptionalMode::RTPS);
  if (transmitter == nullptr) {
    return 3;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  uint64_t sequence = 1;
  while (std::chrono::steady_clock::now() < deadline) {
    ForcedRtpsMessage message;
    message.sequence = sequence;
    message.payload = "forced-rtps-" + std::to_string(sequence);
    transmitter->Transmit(std::make_shared<ForcedRtpsMessage>(message));
    if (sequence == 3) {
      sequence = 1;
    } else {
      ++sequence;
    }
    if (ReadByteWithTimeout(control_read_fd, 'A', 100)) {
      transmitter->Disable();
      transport::Transport::Instance()->Shutdown();
      return 0;
    }
  }

  transmitter->Disable();
  transport::Transport::Instance()->Shutdown();
  return 4;
}

TEST(RtpsSameHostMultiprocessTest, ForcedRtpsIntegrationIsNotCrossHostRouting) {
  // 这是同主机强制 RTPS 集成测试，不是跨主机集成测试。
  // fork 前不能初始化 CMW/GlobalData，否则子进程会继承父进程缓存的 PID。
  const std::string channel_name =
      "forced_rtps_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  int control_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(control_pipe));

  const pid_t child = fork();
  ASSERT_NE(-1, child);
  if (child == 0) {
    close(control_pipe[1]);
    const int result = RunForcedRtpsPublisher(channel_name, control_pipe[0]);
    close(control_pipe[0]);
    _exit(result);
  }

  close(control_pipe[0]);
  signal(SIGPIPE, SIG_IGN);
  hnu::cmw::Init("ForcedRtpsSubscriber");
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<ForcedRtpsMessage> received;
  auto receiver = transport::Transport::Instance()->CreateReceiver<ForcedRtpsMessage>(
      MakeRoleAttributes(channel_name, "_subscriber"),
      [&](const std::shared_ptr<ForcedRtpsMessage>& message,
          const transport::MessageInfo&, const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(*message);
        condition.notify_one();
      },
      config::OptionalMode::RTPS);
  if (receiver == nullptr) {
    close(control_pipe[1]);
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    FAIL() << "forced RTPS receiver initialization failed";
  }
  if (!WriteByte(control_pipe[1], 'S')) {
    receiver->Disable();
    close(control_pipe[1]);
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    FAIL() << "could not start forced RTPS publisher child";
  }

  bool delivered = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    delivered = condition.wait_for(lock, std::chrono::seconds(12), [&]() {
      bool have_sequence[4] = {false, false, false, false};
      for (const auto& message : received) {
        if (message.sequence >= 1 && message.sequence <= 3 &&
            message.payload == "forced-rtps-" + std::to_string(message.sequence)) {
          have_sequence[message.sequence] = true;
        }
      }
      return have_sequence[1] && have_sequence[2] && have_sequence[3];
    });
  }
  EXPECT_TRUE(delivered);
  EXPECT_TRUE(WriteByte(control_pipe[1], 'A'));
  receiver->Disable();

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
    FAIL() << "forced RTPS publisher exceeded the watchdog deadline";
  }
  ASSERT_EQ(child, finished);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));

  transport::Transport::Instance()->Shutdown();
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  // 父子进程均在 fork 后初始化，保证 GlobalData 获取各自真实 PID。
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
