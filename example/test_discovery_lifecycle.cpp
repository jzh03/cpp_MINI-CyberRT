#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <cmw/discovery/topology_manager.h>
#include <cmw/discovery/specific_manager/manager.h>
#include <cmw/init.h>
#include <cmw/transport/rtps/participant.h>

namespace hnu {
namespace cmw {
namespace discovery {
namespace {

class ScopedEnvironment {
 public:
  explicit ScopedEnvironment(const char* name) : name_(name) {
    const char* value = std::getenv(name);
    if (value != nullptr) {
      had_value_ = true;
      value_ = value;
    }
  }

  ~ScopedEnvironment() {
    if (had_value_) {
      setenv(name_.c_str(), value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::string value_;
  bool had_value_ = false;
};

class ReaderFailingManager : public Manager {
 public:
  ReaderFailingManager() {
    channel_name_ = "discovery_lifecycle_failure_injection";
  }

  bool reader_was_attempted() const { return reader_was_attempted_; }
  void ThrowOnReaderCreation() { failure_ = Failure::kThrow; }
  void AllowReaderCreation() { failure_ = Failure::kNone; }
  bool ResourcesAreClear() const {
    return writer_ == nullptr && writer_history_ == nullptr &&
           reader_ == nullptr && reader_history_ == nullptr &&
           listener_ == nullptr && !is_discovery_started_.load();
  }

 protected:
  bool CreateReader(RtpsParticipant* participant) override {
    reader_was_attempted_ = true;
    if (failure_ == Failure::kReturnFalse) return false;
    if (failure_ == Failure::kThrow) {
      throw std::runtime_error("injected reader creation failure");
    }
    return Manager::CreateReader(participant);
  }
  bool Check(const config::RoleAttributes&) override { return true; }
  bool Dispose(const config::ChangeMsg&) override { return true; }
  void OnTopoModuleLeave(const std::string&, int) override {}

 private:
  enum class Failure { kReturnFalse, kThrow, kNone };
  Failure failure_ = Failure::kReturnFalse;
  bool reader_was_attempted_ = false;
};

TEST(DiscoveryLifecycle, FailedInitCanRetryAndExplicitShutdownCanRestart) {
  ScopedEnvironment restore_ip("CMW_IP");
  setenv("CMW_IP", "", 1);

  auto* topology = TopologyManager::Instance();
  ASSERT_NE(nullptr, topology);
  EXPECT_FALSE(topology->IsInitialized());
  EXPECT_EQ(nullptr, topology->node_manager());
  EXPECT_EQ(nullptr, topology->channel_manager());
  EXPECT_EQ(nullptr, CreateNode("node-without-discovery"));

  setenv("CMW_IP", "127.0.0.1", 1);
  ASSERT_TRUE(topology->Init());
  EXPECT_TRUE(topology->IsInitialized());
  EXPECT_NE(nullptr, topology->node_manager());
  EXPECT_NE(nullptr, topology->channel_manager());
  EXPECT_TRUE(topology->Init());

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool callback_entered = false;
  bool release_callback = false;
  auto connection = topology->AddChangeListener([&](const config::ChangeMsg& msg) {
    if (msg.change_type != config::ChangeType::CHANGE_PARTICIPANT ||
        msg.operate_type != config::OperateType::OPT_JOIN ||
        msg.role_attr.host_name != "lifecycle-peer") {
      return;
    }
    std::unique_lock<std::mutex> lock(callback_mutex);
    callback_entered = true;
    callback_condition.notify_all();
    callback_condition.wait(lock, [&] { return release_callback; });
  });
  auto peer = std::make_shared<transport::Participant>(
      "lifecycle-peer+12345", 0, nullptr);
  ASSERT_NE(nullptr, peer->fastrtps_participant());
  bool callback_observed = false;
  {
    std::unique_lock<std::mutex> lock(callback_mutex);
    callback_observed = callback_condition.wait_for(
        lock, std::chrono::seconds(5), [&] { return callback_entered; });
    if (!callback_observed) release_callback = true;
  }
  callback_condition.notify_all();
  ASSERT_TRUE(callback_observed);

  std::mutex shutdown_mutex;
  std::condition_variable shutdown_condition;
  bool shutdown_started = false;
  auto shutdown = std::async(std::launch::async, [&] {
    {
      std::lock_guard<std::mutex> lock(shutdown_mutex);
      shutdown_started = true;
    }
    shutdown_condition.notify_all();
    topology->Shutdown();
  });
  bool shutdown_was_started = false;
  {
    std::unique_lock<std::mutex> lock(shutdown_mutex);
    shutdown_was_started = shutdown_condition.wait_for(
        lock, std::chrono::seconds(5), [&] { return shutdown_started; });
  }
  EXPECT_TRUE(shutdown_was_started);
  if (shutdown_was_started) {
    EXPECT_EQ(std::future_status::timeout,
              shutdown.wait_for(std::chrono::milliseconds(50)));
  }
  {
    std::lock_guard<std::mutex> lock(callback_mutex);
    release_callback = true;
  }
  callback_condition.notify_all();
  EXPECT_EQ(std::future_status::ready,
            shutdown.wait_for(std::chrono::seconds(5)));
  shutdown.get();
  peer->Shutdown();

  topology->Shutdown();
  EXPECT_FALSE(topology->IsInitialized());
  EXPECT_EQ(nullptr, topology->node_manager());
  EXPECT_EQ(nullptr, topology->channel_manager());

  ASSERT_TRUE(topology->Init());
  EXPECT_TRUE(topology->IsInitialized());
  topology->Shutdown();
  EXPECT_FALSE(topology->IsInitialized());
}

TEST(DiscoveryLifecycle, WriterCreationIsRolledBackWhenReaderCreationFails) {
  ScopedEnvironment restore_ip("CMW_IP");
  setenv("CMW_IP", "127.0.0.1", 1);
  auto participant = std::make_shared<transport::Participant>(
      "lifecycle-manager+12346", 0, nullptr);
  auto* fastdds_participant = participant->fastrtps_participant();
  ASSERT_NE(nullptr, fastdds_participant);

  ReaderFailingManager manager;
  EXPECT_FALSE(manager.StartDiscovery(fastdds_participant));
  EXPECT_TRUE(manager.reader_was_attempted());
  EXPECT_TRUE(manager.ResourcesAreClear());

  manager.AllowReaderCreation();
  EXPECT_TRUE(manager.StartDiscovery(fastdds_participant));
  EXPECT_FALSE(manager.ResourcesAreClear());
  manager.Shutdown();
  EXPECT_TRUE(manager.ResourcesAreClear());

  ReaderFailingManager throwing_manager;
  throwing_manager.ThrowOnReaderCreation();
  EXPECT_FALSE(throwing_manager.StartDiscovery(fastdds_participant));
  EXPECT_TRUE(throwing_manager.reader_was_attempted());
  EXPECT_TRUE(throwing_manager.ResourcesAreClear());

  throwing_manager.AllowReaderCreation();
  EXPECT_TRUE(throwing_manager.StartDiscovery(fastdds_participant));
  throwing_manager.Shutdown();
  EXPECT_TRUE(throwing_manager.ResourcesAreClear());
  participant->Shutdown();
}

}  // namespace
}  // namespace discovery
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("DiscoveryLifecycle");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
