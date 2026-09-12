#include "transmitter_lifecycle_test_util.h"

#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <cmw/discovery/topology_manager.h>
#include <cmw/init.h>
#include <cmw/node/publisher.h>
#include <cmw/node/subscriber.h>

namespace hnu {
namespace cmw {
namespace {
using transport::LoanedMessage;
using namespace transport::lifecycle_test;

bool Byte(int fd, char value) { return write(fd, &value, 1) == 1; }
bool Read(int fd, char* value, int timeout = 10000) {
  pollfd descriptor{fd, POLLIN, 0};
  return poll(&descriptor, 1, timeout) == 1 &&
      (descriptor.revents & POLLIN) && read(fd, value, 1) == 1;
}

class Reader {
 public:
  Reader(RoleAttributes attr, const std::string& suffix) {
    attr.node_name += suffix;
    subscriber_.reset(new Subscriber<LoanedMessage>(attr,
        [this](const std::shared_ptr<LoanedMessage>& message) {
          std::lock_guard<std::mutex> lock(mutex_);
          uint64_t sequence = 0;
          bool valid = message && message->size() == 64;
          if(valid) {
            std::memcpy(&sequence, message->data(), sizeof(sequence));
            for(size_t i = sizeof(sequence); i < 64; ++i) {
              valid = valid && message->data()[i] == static_cast<uint8_t>(sequence + i);
            }
          }
          bad_ = bad_ || !valid || !sequences_.insert(sequence).second;
          condition_.notify_all();
        }));
  }
  bool Join() { return subscriber_->Init(); }
  bool Progress() {
    std::unique_lock<std::mutex> lock(mutex_);
    const size_t before = sequences_.size();
    return condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
      return bad_ || sequences_.size() >= before + 3;
    }) && !bad_;
  }
  bool Leave() {
    subscriber_->Shutdown();
    std::lock_guard<std::mutex> lock(mutex_);
    return !bad_;
  }
  ~Reader() { subscriber_->Shutdown(); }
 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool bad_ = false;
  std::set<uint64_t> sequences_;
  std::unique_ptr<Subscriber<LoanedMessage>> subscriber_;
};

class RuntimeCleanup {
 public:
  ~RuntimeCleanup() {
    // Stop worker threads before process-static scheduler tables are destroyed.
    scheduler::Instance()->Shutdown();
    auto dispatcher = transport::ShmDispatcher::Instance(false);
    if(dispatcher != nullptr) { dispatcher->Shutdown(); }
  }
};

int Child(const std::string& channel, int commands, int events) {
  Init("LoanedDiscoveryChurnReader");
  RuntimeCleanup runtime_cleanup;
  discovery::TopologyManager::Instance();
  auto attr = Attributes("remote");
  attr.channel_name = channel;
  attr.channel_id = common::GlobalData::RegisterChannel(channel);
  std::unique_ptr<Reader> a, b;
  if(!Byte(events, 'R')) { return 2; }
  char command;
  while(Read(commands, &command, 20000)) {
    bool success = true;
    if(command == 'A' || command == 'B') {
      auto& reader = command == 'A' ? a : b;
      reader.reset(new Reader(attr, std::string(1, command)));
      success = reader->Join() && reader->Progress();
    } else if(command == 'a' || command == 'b') {
      auto& reader = command == 'a' ? a : b;
      success = reader && reader->Leave();
      reader.reset();
    } else if(command == 'P') {
      success = (a && a->Progress()) || (b && b->Progress());
    } else if(command == 'Q') {
      a.reset();
      b.reset();
      Byte(events, 'Q');
      return 0;
    } else { return 3; }
    if(!Byte(events, success ? command : 'F') || !success) { return 4; }
  }
  return 5;
}

class ChildGuard {
 public:
  explicit ChildGuard(pid_t child) : child_(child) {}
  ~ChildGuard() {
    if(child_ > 0) {
      kill(child_, SIGKILL);
      waitpid(child_, nullptr, 0);
    }
  }
  bool Finish() {
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(std::chrono::steady_clock::now() < deadline) {
      if(waitpid(child_, &status, WNOHANG) == child_) {
        child_ = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
      }
      poll(nullptr, 0, 10);
    }
    return false;
  }
 private:
  pid_t child_;
};

class ChannelCleanup {
 public:
  explicit ChannelCleanup(uint64_t id) : name_("/cmw_" + std::to_string(id)) {}
  ~ChannelCleanup() { shm_unlink(name_.c_str()); }
 private:
  std::string name_;
};

class Publishing {
 public:
  explicit Publishing(Publisher<LoanedMessage>* publisher) : publisher_(publisher) {
    sender_ = std::thread([this]() {
      uint64_t sequence = 0;
      while(true) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if(stop_) { break; }
        }
        auto message = publisher_->AcquireMessage(64);
        if(Fill(message.get(), ++sequence)) {
          std::unique_lock<std::mutex> lock(mutex_);
          if(hold_) {
            EXPECT_TRUE(message->is_shm_backed());
            held_ = true;
            condition_.notify_all();
            EXPECT_TRUE(condition_.wait_for(lock, std::chrono::seconds(5), [this]() {
              return !hold_ || stop_;
            }));
          }
          lock.unlock();
          publisher_->Publish(std::move(message));
        }
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::milliseconds(1), [this]() { return stop_; });
      }
    });
  }
  bool HoldShmLoan() {
    std::unique_lock<std::mutex> lock(mutex_);
    hold_ = true;
    held_ = false;
    return condition_.wait_for(lock, std::chrono::seconds(5), [this]() { return held_; });
  }
  void Resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    hold_ = false;
    condition_.notify_all();
  }
  ~Publishing() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
      condition_.notify_all();
    }
    sender_.join();
  }
 private:
  Publisher<LoanedMessage>* publisher_;
  std::mutex mutex_;
  std::condition_variable condition_;
  bool stop_ = false;
  bool hold_ = false;
  bool held_ = false;
  std::thread sender_;
};

bool Peers(Publisher<LoanedMessage>* publisher, size_t expected) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while(std::chrono::steady_clock::now() < deadline) {
    std::vector<RoleAttributes> peers;
    publisher->GetSubscribers(&peers);
    if(peers.size() == expected) { return true; }
    poll(nullptr, 0, 10);
  }
  return false;
}

TEST(LoanedMessageDiscoveryChurn, RealIntraAndSameHostShmLeaveJoin) {
  // fork before Init creates any middleware threads. Child has its own real
  // Subscriber and Discovery endpoints; host metadata is never fabricated.
  const std::string channel = "discovery_churn_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  int commands[2], events[2];
  ASSERT_EQ(0, pipe(commands));
  ASSERT_EQ(0, pipe(events));
  signal(SIGPIPE, SIG_IGN);
  const pid_t child = fork();
  ASSERT_NE(-1, child);
  if(child == 0) {
    close(commands[1]); close(events[0]);
    _exit(Child(channel, commands[0], events[1]));
  }
  ChildGuard guard(child);
  close(commands[0]); close(events[1]);
  Init("LoanedDiscoveryChurnPublisher");
  RuntimeCleanup runtime_cleanup;
  discovery::TopologyManager::Instance();
  char event;
  ASSERT_TRUE(Read(events[0], &event));
  ASSERT_EQ('R', event);
  // Bootstrap only: discovery's initial endpoint match is asynchronous.
  // All subsequent topology transitions are checked through peer counts and
  // actual delivery, with a held-loan barrier for the mixed transition.
  poll(nullptr, 0, 1000);
  auto attr = Attributes("publisher");
  attr.channel_name = channel;
  attr.channel_id = common::GlobalData::RegisterChannel(channel);
  ChannelCleanup cleanup(attr.channel_id);
  Publisher<LoanedMessage> publisher(attr);
  ASSERT_TRUE(publisher.Init());
  {
    Publishing publishing(&publisher);
    auto command = [&](char value) {
      char response = 0;
      return Byte(commands[1], value) && Read(events[0], &response) && response == value;
    };
    // Same-process real Subscriber JOIN/LEAVE, including non-final peers.
    for(int round = 0; round < 3; ++round) {
      Reader a(attr, "local_a"), b(attr, "local_b");
      ASSERT_TRUE(a.Join()); ASSERT_TRUE(a.Progress());
      ASSERT_TRUE(b.Join()); ASSERT_TRUE(b.Progress());
      ASSERT_TRUE(a.Leave()); ASSERT_TRUE(Peers(&publisher, 1));
      ASSERT_TRUE(b.Progress());
      ASSERT_TRUE(b.Leave()); ASSERT_TRUE(Peers(&publisher, 0));
    }
    for(int round = 0; round < 3; ++round) {
      ASSERT_TRUE(command('A')); ASSERT_TRUE(Peers(&publisher, 1));
      ASSERT_TRUE(command('B')); ASSERT_TRUE(Peers(&publisher, 2));
      ASSERT_TRUE(command('a')); ASSERT_TRUE(Peers(&publisher, 1));
      ASSERT_TRUE(command('P'));
      ASSERT_TRUE(publishing.HoldShmLoan());
      Reader local(attr, "mixed_local");
      ASSERT_TRUE(local.Join()); ASSERT_TRUE(Peers(&publisher, 2));
      publishing.Resume();
      ASSERT_TRUE(local.Progress()); ASSERT_TRUE(command('P'));
      ASSERT_TRUE(local.Leave()); ASSERT_TRUE(Peers(&publisher, 1));
      ASSERT_TRUE(command('P'));
      ASSERT_TRUE(command('b')); ASSERT_TRUE(Peers(&publisher, 0));
    }
    ASSERT_TRUE(command('Q'));
  }
  publisher.Shutdown();
  EXPECT_TRUE(guard.Finish());
  close(commands[1]); close(events[0]);
  // ChannelCleanup only unlinks this test's unique channel, even on failure.
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
