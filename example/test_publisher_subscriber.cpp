#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/node/subscriber.h>
#include <cmw/node/publisher.h>
#include <cmw/config/unit_test.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/serialize/serializable.h>

using namespace hnu::cmw;

// 回归消息同时覆盖基础字段、string 和 vector<int>。
struct SerializationRegressionMessage : public Serializable
{
    int32_t sequence;
    std::string text;
    std::vector<int> values;

    SERIALIZE(sequence, text, values)
};

void WriterReaderTest_constructor(){
    const std::string channel_name("constructor");
    RoleAttributes attr;
    attr.channel_name = channel_name;

    Publisher<config::UnitTest> publisher_a(attr);
    EXPECT_FALSE(publisher_a.IsInit());
    EXPECT_EQ(publisher_a.GetChannelName(), channel_name);

    Subscriber<config::UnitTest> subscriber_a(attr);
    EXPECT_FALSE(subscriber_a.IsInit());
    EXPECT_EQ(subscriber_a.GetChannelName(), channel_name);    

    attr.host_name = "caros";
    Publisher<config::UnitTest> publisher_b(attr);
    EXPECT_FALSE(publisher_b.IsInit());
    EXPECT_EQ(publisher_b.GetChannelName(), channel_name);

    Subscriber<config::UnitTest> subscriber_b(attr);
    EXPECT_FALSE(subscriber_b.IsInit());
    EXPECT_EQ(subscriber_b.GetChannelName(), channel_name);  

    attr.process_id = 12345;
    Publisher<config::UnitTest> publisher_c(attr);
    EXPECT_FALSE(publisher_c.IsInit());
    EXPECT_EQ(publisher_c.GetChannelName(), channel_name);

    Subscriber<config::UnitTest> subscriber_c(attr);
    EXPECT_FALSE(subscriber_c.IsInit());
    EXPECT_EQ(subscriber_c.GetChannelName(), channel_name);  
}

void WriterReaderTest_init_and_shutdown(){
    const std::string channel_name_a("init");
    const std::string channel_name_b("shutdown");  

    RoleAttributes attr;
    attr.channel_name = channel_name_a;
    attr.host_name = "caros";
    attr.process_id = 12345;

    Publisher<config::UnitTest> publisher_a(attr);
    EXPECT_TRUE(publisher_a.Init());
    EXPECT_TRUE(publisher_a.IsInit());   
    EXPECT_TRUE(publisher_a.Init());

    attr.channel_name = channel_name_b;
    attr.process_id = 54321;
    Subscriber<config::UnitTest> subscriber_a(attr);
    EXPECT_TRUE(subscriber_a.Init());
    EXPECT_TRUE(subscriber_a.IsInit());
    // repeated call
    EXPECT_TRUE(subscriber_a.Init());

    Publisher<config::UnitTest> publisher_b(attr);
    EXPECT_TRUE(publisher_b.Init());
    EXPECT_TRUE(publisher_b.IsInit());   

    attr.channel_name = channel_name_a;
    attr.host_name = "sorac";
    attr.process_id = 12345;

    Subscriber<config::UnitTest> subscriber_b(attr);
    EXPECT_TRUE(subscriber_b.Init());
    EXPECT_TRUE(subscriber_b.IsInit());

    //用相同的attr去创建另外一个Subscriber
    Subscriber<config::UnitTest> subscriber_c(attr);
    EXPECT_FALSE(subscriber_c.Init());
    EXPECT_FALSE(subscriber_c.IsInit());

    publisher_a.Shutdown();

    publisher_a.Shutdown();
    subscriber_a.Shutdown();

    subscriber_a.Shutdown();
    publisher_b.Shutdown();
    subscriber_b.Shutdown();
    subscriber_c.Shutdown();

    EXPECT_FALSE(publisher_a.IsInit());
    EXPECT_FALSE(publisher_b.IsInit());
    EXPECT_FALSE(subscriber_a.IsInit());
    EXPECT_FALSE(subscriber_b.IsInit());
    EXPECT_FALSE(subscriber_c.IsInit());
}

bool WriterReaderTest_serialization_round_trip()
{
    // 使用进程号隔离通道，避免与其他并行测试重名。
    auto global_data = common::GlobalData::Instance();
    const std::string channel_name =
        "serialization_regression_" + std::to_string(global_data->ProcessId());

    RoleAttributes subscriber_attr{};
    subscriber_attr.channel_name = channel_name;
    subscriber_attr.channel_id = common::GlobalData::RegisterChannel(channel_name);
    subscriber_attr.host_name = global_data->HostName();
    subscriber_attr.host_ip = global_data->HostIp();
    subscriber_attr.process_id = global_data->ProcessId();
    subscriber_attr.node_name = "serialization_regression_subscriber";
    subscriber_attr.node_id = common::GlobalData::RegisterNode(subscriber_attr.node_name);
    subscriber_attr.id = common::GlobalData::GenerateHashId(subscriber_attr.node_name);
    subscriber_attr.message_type = "SerializationRegressionMessage";

    RoleAttributes publisher_attr = subscriber_attr;
    publisher_attr.node_name = "serialization_regression_publisher";
    publisher_attr.node_id = common::GlobalData::RegisterNode(publisher_attr.node_name);
    publisher_attr.id = common::GlobalData::GenerateHashId(publisher_attr.node_name);

    std::mutex mutex;
    std::condition_variable condition;
    bool received = false;
    SerializationRegressionMessage actual{};

    Subscriber<SerializationRegressionMessage> subscriber(
        subscriber_attr,
        [&](const std::shared_ptr<SerializationRegressionMessage>& message) {
            std::lock_guard<std::mutex> lock(mutex);
            actual = *message;
            received = true;
            condition.notify_one();
        });
    Publisher<SerializationRegressionMessage> publisher(publisher_attr);

    if (!subscriber.Init())
    {
        return false;
    }
    if (!publisher.Init())
    {
        subscriber.Shutdown();
        return false;
    }

    SerializationRegressionMessage expected{};
    expected.sequence = 42;
    expected.text = "publisher subscriber";
    expected.values = {-3, 0, 7, 99};

    // RTPS 匹配是异步的，在限定时间内重复发布直到订阅端收到消息。
    std::unique_lock<std::mutex> lock(mutex);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!received && std::chrono::steady_clock::now() < deadline)
    {
        lock.unlock();
        publisher.Publish(expected);
        lock.lock();
        condition.wait_for(lock, std::chrono::milliseconds(250),
                           [&]() { return received; });
    }
    bool message_received = received;
    SerializationRegressionMessage received_message{};
    if (received)
    {
        received_message = actual;
    }
    lock.unlock();

    publisher.Shutdown();
    subscriber.Shutdown();

    return message_received &&
           received_message.sequence == expected.sequence &&
           received_message.text == expected.text &&
           received_message.values == expected.values;
}

int main()
{
    hnu::cmw::Init("Test_publisher_subscriber");
    WriterReaderTest_constructor();
    WriterReaderTest_init_and_shutdown();
    bool passed = WriterReaderTest_serialization_round_trip();
    std::cout << "Publisher/Subscriber serialization round-trip: "
              << (passed ? "PASS" : "FAIL") << std::endl;
    return passed ? 0 : 1;
}
