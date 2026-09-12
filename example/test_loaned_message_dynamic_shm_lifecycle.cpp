#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <poll.h>
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
#include <cmw/transport/message/loaned_message.h>

namespace hnu {
namespace cmw {
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
  attr.qos_profile.msg_size = 64;
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

bool Publish(Publisher<transport::LoanedMessage>* publisher,
             const std::string& payload) {
  auto message = publisher->AcquireMessage(payload.size());
  if(message == nullptr || message->mutable_data() == nullptr) {
    return false;
  }
  std::memcpy(message->mutable_data(), payload.data(), payload.size());
  return message->set_size(payload.size()) && publisher->Publish(std::move(message));
}

int RunPublisher(const std::string& channel, int control_fd, int status_fd) {
  hnu::cmw::Init("LoanedDynamicShmPublisher");
  discovery::TopologyManager::Instance();
  if(!WriteByte(status_fd, 'R')) {
    return 2;
  }
  std::unique_ptr<Publisher<transport::LoanedMessage>> publisher;
  while(true) {
    char command = 0;
    if(read(control_fd, &command, sizeof(command)) != sizeof(command)) {
      return 3;
    }
    if(command == 'X') {
      if(publisher != nullptr) {
        publisher->Shutdown();
      }
      return 0;
    }
    if(command == 'J') {
      publisher.reset(new Publisher<transport::LoanedMessage>(
          MakeRoleAttributes(channel, "_publisher")));
      if(!publisher->Init() || !WriteByte(status_fd, 'J')) {
        return 4;
      }
      continue;
    }
    if(publisher == nullptr) {
      return 5;
    }
    if(command == 'W') {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(5);
      while(std::chrono::steady_clock::now() < deadline &&
            publisher->HasSubscriber()) {
        poll(nullptr, 0, 20);
      }
      if(!WriteByte(status_fd, publisher->HasSubscriber() ? 'F' : 'L')) {
        return 6;
      }
      continue;
    }
    if(command == 'H') {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(5);
      while(std::chrono::steady_clock::now() < deadline) {
        Publish(publisher.get(), "loaned-dynamic-ready");
        if(ReadByteWithTimeout(control_fd, 'C', 20)) {
          if(!WriteByte(status_fd, 'H')) {
            return 7;
          }
          break;
        }
      }
      continue;
    }
    if(command == '1' || command == '2') {
      const std::string payload = std::string("loaned-dynamic-") + command;
      if(!publisher->HasSubscriber() || !Publish(publisher.get(), payload) ||
         !WriteByte(status_fd, 'P')) {
        return 8;
      }
      continue;
    }
    return 9;
  }
}

bool ReceiveAndLeave(const std::string& channel, char sequence, int control_fd,
                     int status_fd) {
  const std::string expected = std::string("loaned-dynamic-") + sequence;
  std::mutex mutex;
  std::condition_variable condition;
  bool ready = false;
  bool received = false;
  Subscriber<transport::LoanedMessage> subscriber(
      MakeRoleAttributes(channel, std::string("_subscriber_") + sequence),
      [&](const std::shared_ptr<transport::LoanedMessage>& message) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::string payload(reinterpret_cast<const char*>(message->data()),
                                  message->size());
        ready = ready || payload == "loaned-dynamic-ready";
        received = received || payload == expected;
        condition.notify_all();
      });
  if(!subscriber.Init()) {
    return false;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  if(!WriteByte(control_fd, 'H')) {
    subscriber.Shutdown();
    return false;
  }
  std::unique_lock<std::mutex> lock(mutex);
  const bool ready_delivered = condition.wait_for(lock, std::chrono::seconds(10), [&]() {
    return ready;
  });
  lock.unlock();
  if(!ready_delivered || !WriteByte(control_fd, 'C') ||
     !ReadByteWithTimeout(status_fd, 'H', 5000) ||
     !WriteByte(control_fd, sequence) ||
     !ReadByteWithTimeout(status_fd, 'P', 5000)) {
    subscriber.Shutdown();
    return false;
  }
  lock.lock();
  const bool delivered = condition.wait_for(lock, std::chrono::seconds(5), [&]() {
    return received;
  });
  lock.unlock();
  subscriber.Shutdown();
  return delivered;
}

TEST(LoanedMessageDynamicShmLifecycleTest,
     SubscriberLeaveAndJoinRestoresLoanedShmDelivery) {
  const std::string channel = "loaned_dynamic_shm_" + std::to_string(getpid()) +
      "_" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());
  int control_pipe[2] = {-1, -1};
  int status_pipe[2] = {-1, -1};
  ASSERT_EQ(0, pipe(control_pipe));
  ASSERT_EQ(0, pipe(status_pipe));
  const pid_t child = fork();
  ASSERT_NE(-1, child);
  if(child == 0) {
    close(control_pipe[1]);
    close(status_pipe[0]);
    const int result = RunPublisher(channel, control_pipe[0], status_pipe[1]);
    close(control_pipe[0]);
    close(status_pipe[1]);
    _exit(result);
  }
  close(control_pipe[0]);
  close(status_pipe[1]);

  hnu::cmw::Init("LoanedDynamicShmSubscriber");
  discovery::TopologyManager::Instance();
  ASSERT_TRUE(ReadByteWithTimeout(status_pipe[0], 'R', 5000));

  // Both processes have constructed their Discovery endpoints at this point,
  // but Fast DDS matches those endpoints asynchronously.  Do not publish the
  // publisher's one-shot JOIN before the match can receive it.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  ASSERT_TRUE(WriteByte(control_pipe[1], 'J'));
  ASSERT_TRUE(ReadByteWithTimeout(status_pipe[0], 'J', 5000));
  ASSERT_TRUE(ReceiveAndLeave(channel, '1', control_pipe[1], status_pipe[0]));
  ASSERT_TRUE(WriteByte(control_pipe[1], 'W'));
  ASSERT_TRUE(ReadByteWithTimeout(status_pipe[0], 'L', 5000));

  ASSERT_TRUE(ReceiveAndLeave(channel, '2', control_pipe[1], status_pipe[0]));
  ASSERT_TRUE(WriteByte(control_pipe[1], 'X'));
  close(control_pipe[1]);
  close(status_pipe[0]);

  int status = 0;
  ASSERT_EQ(child, waitpid(child, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  // Join workers before process-static ClassicContext tables are destroyed.
  hnu::cmw::scheduler::Instance()->Shutdown();
  auto dispatcher = hnu::cmw::transport::ShmDispatcher::Instance(false);
  if(dispatcher != nullptr) dispatcher->Shutdown();
  return result;
}
