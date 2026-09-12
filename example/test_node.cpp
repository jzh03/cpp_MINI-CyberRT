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
int main(int argc, char** argv)
{
    hnu::cmw::Init("NodeTest");
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    scheduler::Instance()->Shutdown();
    return result;
}
