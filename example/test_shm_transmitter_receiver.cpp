#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/transport/message/message_info.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/shm/shm_conf.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

using serialize::DataStream;

struct ShmRegressionMessage : public serialize::Serializable
{
    uint64_t sequence = 0;
    std::string payload;

    SERIALIZE(sequence, payload)
};

std::string UniqueChannelName()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    return "shm_transmitter_receiver_" + std::to_string(getpid()) + "_" +
           std::to_string(nonce);
}

config::RoleAttributes MakeRoleAttributes(const std::string& channel_name,
                                          const std::string& node_suffix)
{
    auto global_data = common::GlobalData::Instance();

    config::RoleAttributes attr{};
    attr.channel_name = channel_name;
    attr.channel_id = common::GlobalData::RegisterChannel(channel_name);
    attr.host_name = global_data->HostName();
    attr.host_ip = global_data->HostIp();
    attr.process_id = global_data->ProcessId();
    attr.node_name = channel_name + node_suffix;
    attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
    attr.id = common::GlobalData::GenerateHashId(attr.node_name);
    attr.message_type = "ShmRegressionMessage";
    return attr;
}

bool WaitForMessageCount(
    std::mutex* mutex, std::condition_variable* condition,
    const std::vector<ShmRegressionMessage>* received_messages,
    std::size_t expected_count)
{
    std::unique_lock<std::mutex> lock(*mutex);
    return condition->wait_for(
        lock, std::chrono::seconds(5),
        [&]() { return received_messages->size() >= expected_count; });
}

bool SendMessage(ShmTransmitter<ShmRegressionMessage>* transmitter,
                 const ShmRegressionMessage& message)
{
    MessageInfo message_info;
    message_info.set_seq_num(message.sequence);
    return transmitter->Transmit(
        std::make_shared<ShmRegressionMessage>(message), message_info);
}

TEST(ShmTransmitterReceiverTest,
     OversizeFailureDoesNotBreakRecreateOrLaterMessages)
{
    const std::string channel_name = UniqueChannelName();
    const config::RoleAttributes receiver_attr =
        MakeRoleAttributes(channel_name, "_receiver");
    const config::RoleAttributes transmitter_attr =
        MakeRoleAttributes(channel_name, "_transmitter");

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<ShmRegressionMessage> received_messages;

    ShmReceiver<ShmRegressionMessage> receiver(
        receiver_attr,
        [&](const std::shared_ptr<ShmRegressionMessage>& message,
            const MessageInfo&, const config::RoleAttributes&) {
            std::lock_guard<std::mutex> lock(mutex);
            received_messages.push_back(*message);
            condition.notify_one();
        });
    receiver.Enable();

    ShmTransmitter<ShmRegressionMessage> transmitter(transmitter_attr);
    transmitter.Enable();

    ShmRegressionMessage small_message_1;
    small_message_1.sequence = 1;
    small_message_1.payload = "small-01";

    ShmRegressionMessage small_message_2;
    small_message_2.sequence = 2;
    small_message_2.payload = "small-02";

    EXPECT_TRUE(SendMessage(&transmitter, small_message_1));
    EXPECT_TRUE(SendMessage(&transmitter, small_message_2));
    EXPECT_TRUE(WaitForMessageCount(&mutex, &condition, &received_messages, 2));

    ShmRegressionMessage recreate_message;
    recreate_message.sequence = 3;
    recreate_message.payload.assign(32 * 1024, 'r');

    {
        serialize::DataStream recreate_stream;
        recreate_stream << recreate_message;
        ASSERT_GT(recreate_stream.ByteSize(), 16U * 1024U);
    }
    EXPECT_TRUE(SendMessage(&transmitter, recreate_message));
    EXPECT_TRUE(WaitForMessageCount(&mutex, &condition, &received_messages, 3));

    ShmConf shm_conf;
    const uint64_t max_message_size = shm_conf.max_message_size();

    ShmRegressionMessage oversized_message;
    oversized_message.sequence = 4;
    oversized_message.payload.assign(max_message_size, 'x');

    {
        serialize::DataStream oversized_stream;
        oversized_stream << oversized_message;
        ASSERT_GT(oversized_stream.ByteSize(), max_message_size);
    }
    EXPECT_FALSE(SendMessage(&transmitter, oversized_message));

    ShmRegressionMessage recovery_message;
    recovery_message.sequence = 5;
    recovery_message.payload = "small-05";

    EXPECT_TRUE(SendMessage(&transmitter, recovery_message));
    const bool recovered =
        WaitForMessageCount(&mutex, &condition, &received_messages, 4);
    EXPECT_TRUE(recovered);

    ShmDispatcher::Instance()->Shutdown();

    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(received_messages.size(), 4U);
        EXPECT_EQ(received_messages[0].sequence, small_message_1.sequence);
        EXPECT_EQ(received_messages[0].payload, small_message_1.payload);
        EXPECT_EQ(received_messages[1].sequence, small_message_2.sequence);
        EXPECT_EQ(received_messages[1].payload, small_message_2.payload);
        EXPECT_EQ(received_messages[2].sequence, recreate_message.sequence);
        EXPECT_EQ(received_messages[2].payload, recreate_message.payload);
        EXPECT_EQ(received_messages[3].sequence, recovery_message.sequence);
        EXPECT_EQ(received_messages[3].payload, recovery_message.payload);
    }

    transmitter.Disable();
    receiver.Disable();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv)
{
    hnu::cmw::Init("ShmTransmitterReceiverTest");
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();

    auto dispatcher =
        hnu::cmw::transport::ShmDispatcher::Instance(false);
    if (dispatcher != nullptr)
    {
        dispatcher->Shutdown();
    }
    return result;
}
