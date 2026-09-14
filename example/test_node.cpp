#include <cmw/node/node.h>

#include <gtest/gtest.h>
#include <cmw/init.h>
#include <cmw/node/subscriber.h>
#include <cmw/node/publisher.h>
#include <cmw/config/unit_test.h>
#include <cmw/scheduler/scheduler_factory.h>

using namespace hnu::cmw;

TEST(NodeTest, NameAndSubscriberRegistration) {
    auto node = CreateNode("node_test");
    EXPECT_EQ(node->Name(), "node_test");
    config::RoleAttributes attr;
    attr.channel_name = "/node_test_channel";
    auto channel_id = common::GlobalData::RegisterChannel(attr.channel_name);
    attr.channel_id = channel_id;
    attr.qos_profile.depth = 10;

    auto subscriber = node->CreateSubscriber<config::Chatter>(attr.channel_name);
    ASSERT_NE(nullptr, subscriber);
    EXPECT_NE(nullptr, node->GetSubscriber<config::Chatter>(attr.channel_name));
}

TEST(NodeTest, QosDefaultsAndObservationConfigurationAcrossOverloads) {
    auto node = CreateNode("qos_node_overloads");
    auto by_name = node->CreateSubscriber<config::Chatter>("qos_node_name");
    config::RoleAttributes attr{};
    attr.channel_name = "qos_node_attributes";
    attr.qos_profile.depth = 0;
    auto by_attr = node->CreateSubscriber<config::Chatter>(attr);
    SubscriberConfig cfg;
    cfg.channel_name = "qos_node_config";
    cfg.history_depth = 0;
    cfg.pending_queue_size = 3;
    auto by_config = node->CreateSubscriber<config::Chatter>(cfg);
    ASSERT_NE(by_name, nullptr);
    ASSERT_NE(by_attr, nullptr);
    ASSERT_NE(by_config, nullptr);
    EXPECT_TRUE(config::SameQosProfile(by_name->GetQosProfile(), by_attr->GetQosProfile()));
    EXPECT_TRUE(config::SameQosProfile(by_name->GetQosProfile(), by_config->GetQosProfile()));
    EXPECT_EQ(by_attr->GetQosProfile().depth, 1u);
    EXPECT_EQ(by_config->GetQosProfile().durability, config::DURABILITY_VOLATILE);
    EXPECT_EQ(by_config->PendingQueueSize(), 3u);
    EXPECT_EQ(by_config->GetHistoryDepth(), 0u);
    EXPECT_EQ(by_attr->GetHistoryDepth(), 1u);
}

TEST(NodeTest, SharedReceiverConflictFailsCleanlyAndCanRetry) {
    auto first = CreateNode("qos_node_first");
    auto second = CreateNode("qos_node_second");
    SubscriberConfig cfg;
    cfg.channel_name = "qos_node_shared";
    ASSERT_NE(first->CreateSubscriber<config::Chatter>(cfg), nullptr);
    cfg.qos_profile.depth = 2;
    EXPECT_EQ(second->CreateSubscriber<config::Chatter>(cfg), nullptr);
    EXPECT_EQ(second->GetSubscriber<config::Chatter>(cfg.channel_name), nullptr);
    cfg.qos_profile.depth = 1;
    EXPECT_NE(second->CreateSubscriber<config::Chatter>(cfg), nullptr);
}

int main(int argc, char** argv)
{
    hnu::cmw::Init("NodeTest");
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    scheduler::Instance()->Shutdown();
    return result;
}
