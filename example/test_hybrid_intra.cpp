// 构建（在 example 目录）：make test_hybrid_intra
// 运行（在 example 目录）：CMW_PATH="$(cd .. && pwd)" ./build/bin/test_hybrid_intra

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/node/publisher.h>
#include <cmw/node/subscriber.h>
#include <cmw/scheduler/scheduler_factory.h>
#include <cmw/serialize/serializable.h>

namespace hnu {
namespace cmw {

namespace {

// 验证真实 Publisher/Subscriber + Discovery + Hybrid + INTRA 完整链路。
struct HybridIntraMessage : public serialize::Serializable {
  uint64_t sequence = 0;
  std::string payload;

  SERIALIZE(sequence, payload)
};

std::string UniqueChannelName() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return "hybrid_intra_" + std::to_string(common::GlobalData::Instance()->ProcessId()) +
         "_" + std::to_string(nonce);
}

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
  attr.message_type = "HybridIntraMessage";
  return attr;
}

TEST(HybridIntraTest, SameProcessPublisherAndSubscriberDeliverMultipleMessages) {
  constexpr uint32_t kMessageCount = 3;
  const std::string channel_name = UniqueChannelName();
  const RoleAttributes subscriber_attr =
      MakeRoleAttributes(channel_name, "_subscriber");
  const RoleAttributes publisher_attr =
      MakeRoleAttributes(channel_name, "_publisher");

  std::mutex mutex;
  std::condition_variable condition;
  std::vector<HybridIntraMessage> received;
  Subscriber<HybridIntraMessage> subscriber(
      subscriber_attr,
      [&](const std::shared_ptr<HybridIntraMessage>& message) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(*message);
        condition.notify_one();
      },
      kMessageCount);
  Publisher<HybridIntraMessage> publisher(publisher_attr);

  ASSERT_TRUE(subscriber.Init());
  ASSERT_TRUE(publisher.Init());

  // 等待异步 Subscriber routine 进入 DATA_WAIT，再由同步 INTRA 路径投递消息。
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  for (uint64_t sequence = 1; sequence <= kMessageCount; ++sequence) {
    HybridIntraMessage message;
    message.sequence = sequence;
    message.payload = "intra-" + std::to_string(sequence);
    EXPECT_TRUE(publisher.Publish(message));
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return received.size() >= sequence;
    }));
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(kMessageCount, received.size());
    for (uint64_t sequence = 1; sequence <= kMessageCount; ++sequence) {
      EXPECT_EQ(sequence, received[sequence - 1].sequence);
      EXPECT_EQ("intra-" + std::to_string(sequence),
                received[sequence - 1].payload);
    }
  }

  subscriber.Shutdown();
  HybridIntraMessage after_shutdown;
  after_shutdown.sequence = 99;
  after_shutdown.payload = "must-not-arrive";
  publisher.Publish(after_shutdown);
  {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_FALSE(condition.wait_for(lock, std::chrono::milliseconds(500), [&]() {
      return received.size() > kMessageCount;
    }));
  }

  publisher.Shutdown();
}

TEST(HybridIntraTest, OneSubscriberLeaveDoesNotDisableAnotherIntraPeer) {
  const std::string channel_name = UniqueChannelName();
  const RoleAttributes subscriber_a_attr =
      MakeRoleAttributes(channel_name, "_subscriber_a");
  const RoleAttributes subscriber_b_attr =
      MakeRoleAttributes(channel_name, "_subscriber_b");
  const RoleAttributes publisher_attr =
      MakeRoleAttributes(channel_name, "_publisher");

  std::mutex mutex;
  std::condition_variable condition;
  uint32_t subscriber_a_count = 0;
  uint32_t subscriber_b_count = 0;
  Subscriber<HybridIntraMessage> subscriber_a(
      subscriber_a_attr,
      [&](const std::shared_ptr<HybridIntraMessage>&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++subscriber_a_count;
        condition.notify_one();
      },
      2);
  Subscriber<HybridIntraMessage> subscriber_b(
      subscriber_b_attr,
      [&](const std::shared_ptr<HybridIntraMessage>&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++subscriber_b_count;
        condition.notify_one();
      },
      2);
  Publisher<HybridIntraMessage> publisher(publisher_attr);

  ASSERT_TRUE(subscriber_a.Init());
  ASSERT_TRUE(subscriber_b.Init());
  ASSERT_TRUE(publisher.Init());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  HybridIntraMessage first_message;
  first_message.sequence = 1;
  first_message.payload = "before-leave";
  ASSERT_TRUE(publisher.Publish(first_message));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return subscriber_a_count >= 1 && subscriber_b_count >= 1;
    }));
  }

  subscriber_a.Shutdown();
  HybridIntraMessage second_message;
  second_message.sequence = 2;
  second_message.payload = "after-leave";
  ASSERT_TRUE(publisher.Publish(second_message));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return subscriber_b_count >= 2;
    }));
    EXPECT_EQ(1U, subscriber_a_count);
  }

  subscriber_b.Shutdown();
  publisher.Shutdown();
}

TEST(HybridIntraTest, SubscriberLeaveThenNewSubscriberJoins) {
  const std::string channel_name = UniqueChannelName();
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<HybridIntraMessage> subscriber_a_received;
  std::vector<HybridIntraMessage> subscriber_b_received;
  Publisher<HybridIntraMessage> publisher(
      MakeRoleAttributes(channel_name, "_publisher"));

  ASSERT_TRUE(publisher.Init());
  {
    Subscriber<HybridIntraMessage> subscriber_a(
        MakeRoleAttributes(channel_name, "_subscriber_a"),
        [&](const std::shared_ptr<HybridIntraMessage>& message) {
          std::lock_guard<std::mutex> lock(mutex);
          subscriber_a_received.push_back(*message);
          condition.notify_one();
        });
    ASSERT_TRUE(subscriber_a.Init());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    HybridIntraMessage first_message;
    first_message.sequence = 1;
    first_message.payload = "intra-a";
    ASSERT_TRUE(publisher.Publish(first_message));
    {
      std::unique_lock<std::mutex> lock(mutex);
      ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
        return subscriber_a_received.size() == 1;
      }));
      EXPECT_EQ(1U, subscriber_a_received.front().sequence);
      EXPECT_EQ("intra-a", subscriber_a_received.front().payload);
    }
    subscriber_a.Shutdown();
  }

  Subscriber<HybridIntraMessage> subscriber_b(
      MakeRoleAttributes(channel_name, "_subscriber_b"),
      [&](const std::shared_ptr<HybridIntraMessage>& message) {
        std::lock_guard<std::mutex> lock(mutex);
        subscriber_b_received.push_back(*message);
        condition.notify_one();
      });
  ASSERT_TRUE(subscriber_b.Init());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  HybridIntraMessage second_message;
  second_message.sequence = 2;
  second_message.payload = "intra-b";
  ASSERT_TRUE(publisher.Publish(second_message));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return subscriber_b_received.size() == 1;
    }));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  {
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(1U, subscriber_a_received.size());
    ASSERT_EQ(1U, subscriber_b_received.size());
    EXPECT_EQ(1U, subscriber_a_received.front().sequence);
    EXPECT_EQ("intra-a", subscriber_a_received.front().payload);
    EXPECT_EQ(2U, subscriber_b_received.front().sequence);
    EXPECT_EQ("intra-b", subscriber_b_received.front().payload);
  }

  subscriber_b.Shutdown();
  publisher.Shutdown();
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("HybridIntraTest");
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  hnu::cmw::scheduler::Instance()->Shutdown();
  return result;
}
