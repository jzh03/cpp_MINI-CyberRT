#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/node/publisher.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/shm/shm_conf.h>
#include <cmw/transport/shm/xsi_segment.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

static_assert(!std::is_copy_constructible<LoanedMessage>::value,
              "LoanedMessage must not be copyable");
static_assert(!std::is_copy_assignable<LoanedMessage>::value,
              "LoanedMessage must not be copy assignable");

std::string UniqueChannelName(const std::string& prefix)
{
    return prefix + "_" + std::to_string(getpid()) + "_" +
           std::to_string(std::chrono::steady_clock::now()
                              .time_since_epoch().count());
}

config::RoleAttributes MakeRoleAttributes(const std::string& channel_name,
                                          const std::string& node_suffix,
                                          uint32_t msg_size = 0)
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
    attr.message_type = "LoanedMessage";
    attr.qos_profile.msg_size = msg_size;
    return attr;
}

bool AcquireWithTimeout(ShmTransmitter<LoanedMessage>* transmitter,
                        std::size_t capacity,
                        std::unique_ptr<LoanedMessage>* message)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    while(std::chrono::steady_clock::now() < deadline) {
        *message = transmitter->AcquireLoanedMessage(capacity);
        if(*message != nullptr) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

TEST(ShmLoanedMessageTest, DirectCommitKeepsReadLeaseAndRecoversBlocks)
{
    const std::string channel_name = UniqueChannelName("loaned_direct");
    const config::RoleAttributes receiver_attr =
        MakeRoleAttributes(channel_name, "_receiver");
    const config::RoleAttributes transmitter_attr =
        MakeRoleAttributes(channel_name, "_transmitter");

    std::mutex mutex;
    std::condition_variable condition;
    std::shared_ptr<LoanedMessage> held_message;
    std::vector<std::vector<uint8_t>> received_payloads;
    std::vector<uint64_t> received_sequences;
    bool hold_next_message = true;

    ShmReceiver<LoanedMessage> receiver(
        receiver_attr,
        [&](const std::shared_ptr<LoanedMessage>& message,
            const MessageInfo& msg_info, const config::RoleAttributes&) {
            std::lock_guard<std::mutex> lock(mutex);
            received_payloads.emplace_back(message->data(),
                                           message->data() + message->size());
            received_sequences.emplace_back(msg_info.seq_num());
            if(hold_next_message) {
                held_message = message;
            }
            condition.notify_all();
        });
    receiver.Enable();

    ShmTransmitter<LoanedMessage> transmitter(transmitter_attr);
    transmitter.Enable();

    {
        auto abandoned = transmitter.AcquireLoanedMessage(64);
        ASSERT_NE(nullptr, abandoned);
        EXPECT_NE(nullptr, abandoned->mutable_data());
    }

    auto message = transmitter.AcquireLoanedMessage(64);
    ASSERT_NE(nullptr, message);
    ASSERT_NE(nullptr, message->mutable_data());
    EXPECT_EQ(64U, message->capacity());
    EXPECT_FALSE(message->set_size(message->capacity() + 1));
    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    std::memcpy(message->mutable_data(), payload, sizeof(payload));
    ASSERT_TRUE(message->set_size(sizeof(payload)));
    const uint32_t held_block_index = message->block_index();
    EXPECT_TRUE(transmitter.TransmitLoanedMessage(std::move(message)));
    EXPECT_FALSE(transmitter.TransmitLoanedMessage(std::move(message)));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
            return held_message != nullptr;
        }));
        ASSERT_EQ(1U, received_payloads.size());
        EXPECT_EQ(std::vector<uint8_t>(payload, payload + sizeof(payload)),
                  received_payloads.front());
        EXPECT_EQ(1U, received_sequences.front());
        EXPECT_EQ(nullptr, held_message->mutable_data());
        EXPECT_EQ(sizeof(payload), held_message->size());
    }

    const uint32_t block_num = static_cast<uint32_t>(ShmConf().block_num());
    std::vector<std::unique_ptr<LoanedMessage>> busy_messages;
    busy_messages.reserve(block_num - 1);
    for(uint32_t i = 0; i < block_num - 1; ++i) {
        auto busy = transmitter.AcquireLoanedMessage(1);
        ASSERT_NE(nullptr, busy);
        EXPECT_NE(held_block_index, busy->block_index());
        busy_messages.emplace_back(std::move(busy));
    }
    EXPECT_EQ(nullptr, transmitter.AcquireLoanedMessage(1));
    busy_messages.clear();

    for(uint32_t i = 0; i < block_num; ++i) {
        auto candidate = transmitter.AcquireLoanedMessage(1);
        ASSERT_NE(nullptr, candidate);
        EXPECT_NE(held_block_index, candidate->block_index());
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        held_message.reset();
        hold_next_message = false;
    }

    bool held_block_recovered = false;
    for(uint32_t i = 0; i < block_num; ++i) {
        auto candidate = transmitter.AcquireLoanedMessage(1);
        ASSERT_NE(nullptr, candidate);
        held_block_recovered = held_block_recovered ||
                               candidate->block_index() == held_block_index;
    }
    EXPECT_TRUE(held_block_recovered);

    const std::size_t continuous_count = block_num * 2;
    for(std::size_t i = 0; i < continuous_count; ++i) {
        std::unique_ptr<LoanedMessage> continuous;
        ASSERT_TRUE(AcquireWithTimeout(&transmitter, 1, &continuous));
        continuous->mutable_data()[0] = static_cast<uint8_t>(i);
        ASSERT_TRUE(continuous->set_size(1));
        ASSERT_TRUE(transmitter.TransmitLoanedMessage(std::move(continuous)));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
            return received_payloads.size() >= continuous_count + 1;
        }));
    }

    transmitter.Disable();
    receiver.Disable();
    ShmDispatcher::Instance()->Shutdown();
}

TEST(ShmLoanedMessageTest, RejectsWrongPublisherAndChannelTypeMixing)
{
    const std::string channel_name = UniqueChannelName("loaned_validation");
    const config::RoleAttributes attr_a =
        MakeRoleAttributes(channel_name, "_a", 64);
    const config::RoleAttributes attr_b =
        MakeRoleAttributes(channel_name, "_b", 64);

    ShmTransmitter<LoanedMessage> transmitter_a(attr_a);
    ShmTransmitter<LoanedMessage> transmitter_b(attr_b);
    transmitter_a.Enable();
    transmitter_b.Enable();

    EXPECT_EQ(nullptr, transmitter_a.AcquireLoanedMessage(65));
    auto invalid_size = transmitter_a.AcquireLoanedMessage(64);
    ASSERT_NE(nullptr, invalid_size);
    EXPECT_FALSE(invalid_size->set_size(65));
    EXPECT_FALSE(transmitter_a.TransmitLoanedMessage(std::move(invalid_size)));

    auto wrong_owner = transmitter_a.AcquireLoanedMessage(64);
    ASSERT_NE(nullptr, wrong_owner);
    ASSERT_TRUE(wrong_owner->set_size(1));
    EXPECT_FALSE(transmitter_b.TransmitLoanedMessage(std::move(wrong_owner)));

    ShmTransmitter<std::string> serialized_transmitter(attr_b);
    serialized_transmitter.Enable();
    MessageInfo info;
    EXPECT_FALSE(serialized_transmitter.Transmit(
        std::make_shared<std::string>("serialized"), info));

    serialized_transmitter.Disable();
    transmitter_b.Disable();
    transmitter_a.Disable();
}

TEST(ShmLoanedMessageTest, XsiFixedCapacityAcquireAndReadOnlyView)
{
    const uint64_t channel_id =
        (static_cast<uint64_t>(getpid()) << 32) ^ static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    SegmentPtr writer = std::make_shared<XsiSegment>(channel_id, 64);

    WritableBlock writable_block;
    ASSERT_TRUE(writer->AcquireBlockToWriteWithoutRecreate(
        64, ShmMessageType::LOANED, &writable_block));
    const uint32_t block_index = writable_block.index;
    const uint64_t generation = writable_block.generation;
    {
        WritableBlockLease write_lease(writer, writable_block);
        LoanedMessage writable(writable_block.buf, 64, std::move(write_lease),
                               channel_id, writer.get());
        ASSERT_NE(nullptr, writable.mutable_data());
        std::memset(writable.mutable_data(), 0x5a, 64);
        EXPECT_TRUE(writable.set_size(64));
        writable_block.block->set_msg_size(64);
    }
    EXPECT_FALSE(writer->AcquireBlockToWriteWithoutRecreate(
        writer->payload_capacity() + 1, ShmMessageType::LOANED,
        &writable_block));

    SegmentPtr reader = std::make_shared<XsiSegment>(channel_id);
    ReadableBlock readable_block;
    readable_block.index = block_index;
    ASSERT_TRUE(reader->AcquireBlockToRead(&readable_block));
    ReadableBlockLease read_lease(reader, readable_block);
    auto readable = std::make_shared<LoanedMessage>(
        readable_block.buf, 64, reader->payload_capacity(),
        std::move(read_lease), channel_id, readable_block.index, generation);
    EXPECT_EQ(nullptr, readable->mutable_data());
    EXPECT_EQ(64U, readable->size());
    EXPECT_EQ(0x5a, readable->data()[0]);
    readable.reset();
}

TEST(ShmLoanedMessageTest, PublisherApiRejectsLoanWithoutActiveShmPeer)
{
    const std::string channel_name = UniqueChannelName("loaned_publisher");
    Publisher<LoanedMessage> publisher(
        MakeRoleAttributes(channel_name, "_publisher", 64));
    ASSERT_TRUE(publisher.Init());
    EXPECT_EQ(nullptr, publisher.AcquireMessage(64));
    publisher.Shutdown();
}

TEST(ShmLoanedMessageTest, HybridReceiverAcceptsOnlyShmLoanPath)
{
    const std::string channel_name = UniqueChannelName("loaned_receiver");
    const config::RoleAttributes receiver_attr =
        MakeRoleAttributes(channel_name, "_receiver", 64);
    config::RoleAttributes shm_publisher_attr =
        MakeRoleAttributes(channel_name, "_remote", 64);
    ++shm_publisher_attr.process_id;

    auto receiver = Transport::Instance()->CreateReceiver<LoanedMessage>(
        receiver_attr,
        [](const std::shared_ptr<LoanedMessage>&, const MessageInfo&,
           const config::RoleAttributes&) {},
        OptionalMode::HYBRID);
    ASSERT_NE(nullptr, receiver);
    receiver->Enable(shm_publisher_attr);
    receiver->Disable(shm_publisher_attr);
    receiver->Disable();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv)
{
    hnu::cmw::Init("ShmLoanedMessageTest");
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
