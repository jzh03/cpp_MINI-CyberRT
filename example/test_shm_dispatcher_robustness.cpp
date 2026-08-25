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
#include <cmw/common/util.h>
#include <cmw/init.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/transport/message/message_info.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/shm/notifier_factory.h>
#include <cmw/transport/shm/readable_info.h>
#include <cmw/transport/shm/xsi_segment.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

using serialize::DataStream;

struct DispatcherRobustnessMessage : public serialize::Serializable
{
    uint64_t sequence = 0;
    std::string payload;

    SERIALIZE(sequence, payload)
};

class InspectableXsiSegment : public XsiSegment
{
public:
    explicit InspectableXsiSegment(uint64_t channel_id)
        : XsiSegment(channel_id) {}

    uint64_t block_num() { return conf_.block_num(); }
};

std::string UniqueChannelName()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    return "shm_dispatcher_robustness_" + std::to_string(getpid()) + "_" +
           std::to_string(nonce);
}

config::RoleAttributes MakeRoleAttributes(const std::string& channel_name)
{
    auto global_data = common::GlobalData::Instance();

    config::RoleAttributes attr{};
    attr.channel_name = channel_name;
    attr.channel_id = common::GlobalData::RegisterChannel(channel_name);
    attr.host_name = global_data->HostName();
    attr.host_ip = global_data->HostIp();
    attr.process_id = global_data->ProcessId();
    attr.node_name = channel_name + "_receiver";
    attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
    attr.id = common::GlobalData::GenerateHashId(attr.node_name);
    attr.message_type = "DispatcherRobustnessMessage";
    return attr;
}

TEST(ShmDispatcherRobustnessTest,
     AcquireReadFailureIsDroppedWithoutReleasingTheWriteLock)
{
    const std::string channel_name = UniqueChannelName();
    const config::RoleAttributes attr = MakeRoleAttributes(channel_name);

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<DispatcherRobustnessMessage> received_messages;

    ShmReceiver<DispatcherRobustnessMessage> receiver(
        attr,
        [&](const std::shared_ptr<DispatcherRobustnessMessage>& message,
            const MessageInfo&, const config::RoleAttributes&) {
            std::lock_guard<std::mutex> lock(mutex);
            received_messages.push_back(*message);
            condition.notify_one();
        });
    receiver.Enable();

    InspectableXsiSegment segment(attr.channel_id);
    WritableBlock blocked_writable_block;
    ASSERT_TRUE(segment.AcquireBlockToWrite(64, &blocked_writable_block));

    auto notifier = NotifierFactory::CreateNotifier();
    const uint64_t host_id = common::Hash(
        common::GlobalData::Instance()->HostIp());
    const ReadableInfo unreadable_info(host_id, blocked_writable_block.index,
                                       attr.channel_id);
    const bool failure_notification_sent =
        notifier != nullptr && notifier->Notify(unreadable_info);
    EXPECT_TRUE(failure_notification_sent);

    ShmTransmitter<DispatcherRobustnessMessage> transmitter(attr);
    transmitter.Enable();

    DispatcherRobustnessMessage valid_message;
    valid_message.sequence = 1;
    valid_message.payload = "recovery";

    MessageInfo message_info;
    message_info.set_seq_num(valid_message.sequence);
    const bool valid_message_sent = transmitter.Transmit(
        std::make_shared<DispatcherRobustnessMessage>(valid_message),
        message_info);
    EXPECT_TRUE(valid_message_sent);

    bool valid_message_received = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        valid_message_received = condition.wait_for(
            lock, std::chrono::seconds(5),
            [&]() { return !received_messages.empty(); });
    }

    EXPECT_TRUE(valid_message_received);
    if (valid_message_received)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(received_messages.size(), 1U);
        EXPECT_EQ(received_messages[0].sequence, valid_message.sequence);
        EXPECT_EQ(received_messages[0].payload, valid_message.payload);
    }

    // Joining the dispatcher guarantees that the valid read lock has been
    // released before all block write locks are checked below.
    ShmDispatcher::Instance()->Shutdown();

    segment.ReleaseWrittenBlock(blocked_writable_block);

    std::vector<WritableBlock> writable_blocks;
    writable_blocks.reserve(segment.block_num());
    for (uint64_t i = 0; i < segment.block_num(); ++i)
    {
        WritableBlock writable_block;
        if (!segment.AcquireBlockToWrite(64, &writable_block))
        {
            break;
        }
        writable_blocks.push_back(writable_block);
    }

    const uint64_t acquired_block_num = writable_blocks.size();
    for (const auto& writable_block : writable_blocks)
    {
        segment.ReleaseWrittenBlock(writable_block);
    }

    EXPECT_EQ(acquired_block_num, segment.block_num());

    transmitter.Disable();
    receiver.Disable();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv)
{
    hnu::cmw::Init("ShmDispatcherRobustnessTest");
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
