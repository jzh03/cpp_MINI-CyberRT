// Build: make test_loaned_message_rtps_multiprocess

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/config/transport_config.h>
#include <cmw/init.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/transport.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

RoleAttributes MakeRoleAttributes(const std::string& channel,
                                  const std::string& suffix) {
  auto global_data = common::GlobalData::Instance();
  RoleAttributes attr{};
  attr.channel_name = channel;
  attr.channel_id = common::GlobalData::RegisterChannel(channel);
  attr.host_name = global_data->HostName();
  attr.host_ip = global_data->HostIp();
  attr.process_id = global_data->ProcessId();
  attr.node_name = channel + suffix;
  attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
  attr.id = common::GlobalData::GenerateHashId(attr.node_name);
  attr.message_type = "LoanedMessage";
  attr.qos_profile.msg_size = 1024;
  return attr;
}

bool ReadByteWithTimeout(int fd, char expected, int timeout_ms) {
  pollfd descriptor{fd, POLLIN, 0};
  if(poll(&descriptor, 1, timeout_ms) != 1 || !(descriptor.revents & POLLIN)) {
    return false;
  }
  char value = 0;
  return read(fd, &value, sizeof(value)) == sizeof(value) && value == expected;
}

bool WriteByte(int fd, char value) {
  return write(fd, &value, sizeof(value)) == sizeof(value);
}

int RunPublisher(const std::string& channel, int control_fd) {
  if(!ReadByteWithTimeout(control_fd, 'S', 5000)) {
    return 2;
  }
  hnu::cmw::Init("LoanedMessageRtpsPublisher");
  auto transmitter = Transport::Instance()->CreateTransmitter<LoanedMessage>(
      MakeRoleAttributes(channel, "_publisher"), config::OptionalMode::RTPS);
  if(transmitter == nullptr) {
    return 3;
  }

  const std::string payload = "loaned-rtps-payload";
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(12);
  while(std::chrono::steady_clock::now() < deadline) {
    auto message = transmitter->AcquireLoanedMessage(payload.size());
    if(message == nullptr || !message->is_heap_backed()) {
      transmitter->Disable();
      return 4;
    }
    std::memcpy(message->mutable_data(), payload.data(), payload.size());
    if(!message->set_size(payload.size()) ||
       !transmitter->TransmitLoanedMessage(std::move(message))) {
      transmitter->Disable();
      return 5;
    }
    if(ReadByteWithTimeout(control_fd, 'A', 100)) {
      transmitter->Disable();
      Transport::Instance()->Shutdown();
      return 0;
    }
  }
  transmitter->Disable();
  Transport::Instance()->Shutdown();
  return 6;
}

TEST(LoanedMessageRtpsTest, WireDecoderRejectsMalformedLengths) {
  const std::string empty("\0\0\0\0", 4);
  auto decoded = LoanedMessage::DeserializePayload(empty.data(), empty.size(), 16, 7);
  ASSERT_NE(nullptr, decoded);
  EXPECT_TRUE(decoded->is_heap_backed());
  EXPECT_TRUE(decoded->is_read_only());
  EXPECT_EQ(0U, decoded->size());
  EXPECT_EQ(nullptr, decoded->mutable_data());

  const std::string truncated("\0\0\0\x04" "abc", 7);
  EXPECT_EQ(nullptr, LoanedMessage::DeserializePayload(
      truncated.data(), truncated.size(), 16, 7));
  const std::string over_limit("\0\0\x01\x00", 4);
  EXPECT_EQ(nullptr, LoanedMessage::DeserializePayload(
      over_limit.data(), over_limit.size(), 32, 7));
  const std::string trailing("\0\0\0\x01" "xy", 6);
  EXPECT_EQ(nullptr, LoanedMessage::DeserializePayload(
      trailing.data(), trailing.size(), 16, 7));
  EXPECT_EQ(nullptr, LoanedMessage::DeserializePayload("\0\0\0", 3, 16, 7));
}

TEST(LoanedMessageRtpsTest, ForcedRtpsRoundTripUsesHeapBackedReadOnlyMessage) {
  const std::string channel = "loaned_rtps_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  int control_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(control_pipe));
  const pid_t child = fork();
  ASSERT_NE(-1, child);
  if(child == 0) {
    close(control_pipe[1]);
    const int result = RunPublisher(channel, control_pipe[0]);
    close(control_pipe[0]);
    _exit(result);
  }

  close(control_pipe[0]);
  signal(SIGPIPE, SIG_IGN);
  hnu::cmw::Init("LoanedMessageRtpsSubscriber");
  std::mutex mutex;
  std::condition_variable condition;
  bool received = false;
  bool valid = false;
  auto receiver = Transport::Instance()->CreateReceiver<LoanedMessage>(
      MakeRoleAttributes(channel, "_subscriber"),
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo&,
          const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(mutex);
        valid = message->is_heap_backed() && message->is_read_only() &&
            message->mutable_data() == nullptr &&
            std::string(reinterpret_cast<const char*>(message->data()),
                        message->size()) == "loaned-rtps-payload";
        received = true;
        condition.notify_all();
      },
      config::OptionalMode::RTPS);
  ASSERT_NE(nullptr, receiver);
  ASSERT_TRUE(WriteByte(control_pipe[1], 'S'));

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(12), [&]() {
      return received;
    }));
    EXPECT_TRUE(valid);
  }
  EXPECT_TRUE(WriteByte(control_pipe[1], 'A'));
  receiver->Disable();

  int status = 0;
  pid_t finished = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while(std::chrono::steady_clock::now() < deadline) {
    finished = waitpid(child, &status, WNOHANG);
    if(finished != 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  close(control_pipe[1]);
  if(finished == 0) {
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    FAIL() << "RTPS publisher exceeded the watchdog deadline";
  }
  ASSERT_EQ(child, finished);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
  Transport::Instance()->Shutdown();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
