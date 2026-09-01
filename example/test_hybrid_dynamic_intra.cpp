// 构建（在 example 目录）：make test_hybrid_dynamic_intra
// 运行（在 example 目录）：CMW_PATH="$(cd .. && pwd)" ./build/bin/test_hybrid_dynamic_intra

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/node/publisher.h>
#include <cmw/node/subscriber.h>
#include <cmw/serialize/serializable.h>

namespace hnu {
namespace cmw {
namespace {

struct HybridDynamicIntraMessage : public serialize::Serializable {
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
  attr.message_type = "HybridDynamicIntraMessage";
  return attr;
}

TEST(HybridDynamicIntraTest, SubscriberLeaveThenNewSubscriberJoins) {
  const std::string channel_name =
      "hybrid_dynamic_intra_" +
      std::to_string(common::GlobalData::Instance()->ProcessId()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::mutex mutex;
  std::condition_variable condition;
  uint32_t subscriber_a_count = 0;
  uint32_t subscriber_b_count = 0;
  Publisher<HybridDynamicIntraMessage> publisher(
      MakeRoleAttributes(channel_name, "_publisher"));

  ASSERT_TRUE(publisher.Init());
  {
    Subscriber<HybridDynamicIntraMessage> subscriber_a(
        MakeRoleAttributes(channel_name, "_subscriber_a"),
        [&](const std::shared_ptr<HybridDynamicIntraMessage>& message) {
          std::lock_guard<std::mutex> lock(mutex);
          if (message->sequence == 1 && message->payload == "intra-a") {
            ++subscriber_a_count;
            condition.notify_one();
          }
        });
    ASSERT_TRUE(subscriber_a.Init());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    HybridDynamicIntraMessage message;
    message.sequence = 1;
    message.payload = "intra-a";
    ASSERT_TRUE(publisher.Publish(message));
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return subscriber_a_count == 1;
    }));
    EXPECT_EQ(1U, subscriber_a_count);
    subscriber_a.Shutdown();
  }

  Subscriber<HybridDynamicIntraMessage> subscriber_b(
      MakeRoleAttributes(channel_name, "_subscriber_b"),
      [&](const std::shared_ptr<HybridDynamicIntraMessage>& message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (message->sequence == 2 && message->payload == "intra-b") {
          ++subscriber_b_count;
          condition.notify_one();
        }
      });
  ASSERT_TRUE(subscriber_b.Init());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  HybridDynamicIntraMessage message;
  message.sequence = 2;
  message.payload = "intra-b";
  ASSERT_TRUE(publisher.Publish(message));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return subscriber_b_count == 1;
    }));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  {
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(1U, subscriber_a_count);
    EXPECT_EQ(1U, subscriber_b_count);
  }

  subscriber_b.Shutdown();
  publisher.Shutdown();
}

}  // namespace
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("HybridDynamicIntraTest");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
