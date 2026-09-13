// A new exec for every subscriber: no inherited Discovery cache or reannouncement.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include <cmw/init.h>
#include <cmw/node/node.h>
#include <cmw/scheduler/scheduler_factory.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>

extern char** environ;

namespace {
using namespace hnu::cmw;
using Clock = std::chrono::steady_clock;

struct Message : serialize::Serializable {
  uint64_t sequence = 0;
  std::string payload;
  SERIALIZE(sequence, payload)
};

struct Reception {
  uint64_t first = 0, last = 0, writer = 0;
};

// Workers destroy endpoints before stopping shared runtime facilities.
struct Runtime {
  Runtime() {
    Init(("DiscoveryLateJoin_" + std::to_string(getpid())).c_str());
    logger::Logger::Instance()->console(false);
  }
  ~Runtime() {
    scheduler::Instance()->Shutdown();
    auto* shm = transport::ShmDispatcher::Instance(false);
    if (shm) shm->Shutdown();
    transport::Transport::Instance()->Shutdown();
  }
};

bool Read(int fd, void* data, size_t size, int timeout_ms = 15000) {
  pollfd descriptor{fd, POLLIN, 0};
  return poll(&descriptor, 1, timeout_ms) == 1 &&
      (descriptor.revents & POLLIN) &&
      read(fd, data, size) == static_cast<ssize_t>(size);
}

bool Event(int fd, char expected) {
  char value = 0;
  return Read(fd, &value, 1) && value == expected;
}

bool Write(int fd, char value) { return write(fd, &value, 1) == 1; }

int PublisherWorker(const std::string& channel) {
  Runtime runtime;
  auto node = CreateNode(channel + "_publisher");
  // Replay must apply an earlier JOIN followed by LEAVE without a ghost writer.
  auto departed = node->CreatePublisher<Message>(channel + "_departed");
  if (!departed) return 2;
  departed->Shutdown();
  departed.reset();
  auto publisher = node->CreatePublisher<Message>(channel);
  if (!publisher || !Write(3, 'R')) return 3;
  bool matched = false;
  uint64_t sequence = 0;
  int result = 4;
  const auto deadline = Clock::now() + std::chrono::seconds(60);
  while (Clock::now() < deadline) {
    const bool online = publisher->HasSubscriber();
    if (online != matched) {
      if (!Write(3, online ? 'M' : 'L')) break;
      matched = online;
    }
    Message message;
    message.sequence = ++sequence;
    message.payload = "late-join-" + std::to_string(sequence);
    if (!publisher->Publish(message)) { result = 5; break; }
    char command = 0;
    if (Read(STDIN_FILENO, &command, 1, 50)) {
      if (command == 'X') result = 0;
      break;
    }
  }
  publisher->Shutdown();
  std::printf("[CHECK] publisher pid=%d last=%llu exit=%d\n", getpid(),
              (unsigned long long)sequence, result);
  return result;
}

int SubscriberWorker(const std::string& channel, int publisher_pid) {
  Runtime runtime;
  auto manager = discovery::TopologyManager::Instance()->channel_manager();
  std::vector<RoleAttributes> writers;
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  // Check historical discovery BEFORE creating any Reader on this channel.
  // The publisher cannot repair this by responding to a Reader JOIN.
  while (Clock::now() < deadline) {
    writers.clear();
    manager->GetWritersOfChannel(channel, &writers);
    if (!writers.empty()) break;
    poll(nullptr, 0, 10);
  }
  if (writers.size() != 1 || writers.front().process_id != publisher_pid ||
      manager->HasWriter(channel + "_departed")) {
    std::fprintf(stderr, "[CHECK] FAIL historical Writer discovery: count=%zu\n",
                 writers.size());
    return 6;
  }
  std::printf("[CHECK] discovered historical writer=%llu publisher_pid=%d before Reader JOIN\n",
              (unsigned long long)writers.front().id, publisher_pid);
  Reception reception;
  reception.writer = writers.front().id;
  std::mutex mutex;
  std::condition_variable condition;
  unsigned valid = 0;
  bool invalid = false;
  auto node = CreateNode(channel + "_subscriber_" + std::to_string(getpid()));
  auto subscriber = node->CreateSubscriber<Message>(channel,
      [&](const std::shared_ptr<Message>& message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!message->sequence ||
            message->payload != "late-join-" + std::to_string(message->sequence) ||
            (valid && message->sequence != reception.last + 1)) invalid = true;
        if (!valid) reception.first = message->sequence;
        reception.last = message->sequence;
        ++valid;
        condition.notify_one();
      });
  if (!subscriber) return 7;
  bool received;
  {
    std::unique_lock<std::mutex> lock(mutex);
    received = condition.wait_for(lock, std::chrono::seconds(10), [&]() {
      return invalid || valid >= 10;
    });
  }
  subscriber->Shutdown();
  subscriber->ClearData();
  std::lock_guard<std::mutex> lock(mutex);
  if (!received || invalid || valid < 10) return 8;
  std::printf("[CHECK] subscriber pid=%d valid=%u first=%llu last=%llu\n", getpid(),
              valid, (unsigned long long)reception.first, (unsigned long long)reception.last);
  return write(3, &reception, sizeof(reception)) == sizeof(reception) ? 0 : 9;
}

class Child {
 public:
  bool Start(const std::vector<std::string>& arguments) {
    int commands[2], events[2];
    if (pipe(commands) != 0) return false;
    if (pipe(events) != 0) { close(commands[0]); close(commands[1]); return false; }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // dup2 sources before closing originals; fd 3 is our event channel.
    posix_spawn_file_actions_adddup2(&actions, commands[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, events[1], 3);
    for (int fd : {commands[0], commands[1], events[0], events[1]}) {
      if (fd != STDIN_FILENO && fd != 3) posix_spawn_file_actions_addclose(&actions, fd);
    }
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("/proc/self/exe"));
    for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    const int result = posix_spawn(&pid_, "/proc/self/exe", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(commands[0]); close(events[1]);
    command_ = commands[1]; event_ = events[0];
    if (result != 0) pid_ = -1;
    return result == 0;
  }
  int Wait() {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    int status = 0;
    while (Clock::now() < deadline) {
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      }
      poll(nullptr, 0, 10);
    }
    return -1;
  }
  ~Child() {
    if (pid_ > 0) { kill(pid_, SIGKILL); waitpid(pid_, nullptr, 0); }
    if (command_ >= 0) close(command_);
    if (event_ >= 0) close(event_);
  }
  pid_t pid() const { return pid_; }
  int command() const { return command_; }
  int event() const { return event_; }
 private:
  pid_t pid_ = -1;
  int command_ = -1, event_ = -1;
};

TEST(DiscoveryLateJoin, FreshProcessesDiscoverExistingWriterAndReceive) {
  const std::string channel = "late_join_" + std::to_string(getpid()) + "_" +
      std::to_string(Clock::now().time_since_epoch().count());
  Child publisher;
  ASSERT_TRUE(publisher.Start({"--publisher", channel}));
  ASSERT_TRUE(Event(publisher.event(), 'R'));
  const pid_t publisher_pid = publisher.pid();
  Reception previous;
  for (int round = 0; round < 3; ++round) {
    Child subscriber;
    ASSERT_TRUE(subscriber.Start({"--subscriber", channel, std::to_string(publisher_pid)}));
    Reception received;
    ASSERT_TRUE(Read(subscriber.event(), &received, sizeof(received)));
    ASSERT_TRUE(Event(publisher.event(), 'M'));
    ASSERT_EQ(0, subscriber.Wait());
    ASSERT_TRUE(Event(publisher.event(), 'L'));
    ASSERT_GT(received.first, previous.last);
    ASSERT_GE(received.last - received.first, 9u);
    if (round) ASSERT_EQ(previous.writer, received.writer);
    previous = received;
    std::printf("[CHECK] round=%d PASS same_publisher_pid=%d writer=%llu\n", round + 1,
                publisher_pid, (unsigned long long)received.writer);
  }
  ASSERT_TRUE(Write(publisher.command(), 'X'));
  ASSERT_EQ(0, publisher.Wait());
}
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  signal(SIGPIPE, SIG_IGN);
  if (argc == 3 && std::string(argv[1]) == "--publisher") return PublisherWorker(argv[2]);
  if (argc == 4 && std::string(argv[1]) == "--subscriber")
    return SubscriberWorker(argv[2], std::atoi(argv[3]));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
