#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>
#include <thread>

#include <sys/shm.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/transport/shm/condition_notifier.h>
#include <cmw/transport/shm/posix_segment.h>
#include <cmw/transport/shm/readable_info.h>
#include <cmw/transport/shm/segment.h>
#include <cmw/transport/shm/shm_conf.h>
#include <cmw/transport/shm/xsi_segment.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

static_assert(!std::is_copy_constructible<WritableBlockLease>::value,
              "WritableBlockLease must not be copyable");
static_assert(!std::is_copy_assignable<WritableBlockLease>::value,
              "WritableBlockLease must not be copyable");
static_assert(!std::is_copy_constructible<ReadableBlockLease>::value,
              "ReadableBlockLease must not be copyable");
static_assert(!std::is_copy_assignable<ReadableBlockLease>::value,
              "ReadableBlockLease must not be copyable");

uint64_t UniqueChannelId()
{
    const uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return (static_cast<uint64_t>(getpid()) << 32) ^ timestamp;
}

key_t UniqueNotifierKey()
{
    const uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return static_cast<key_t>(0x30000000U |
                              ((static_cast<uint32_t>(getpid()) & 0x0fffU)
                               << 16) |
                              (static_cast<uint32_t>(timestamp) & 0xffffU));
}

bool ReturnAfterWriteAcquire(const SegmentPtr& segment, bool* acquired)
{
    WritableBlock block;
    if(!segment->AcquireBlockToWrite(64, &block)){
        return false;
    }
    *acquired = true;
    WritableBlockLease lease(segment, block);
    return false;
}

template <typename SegmentT>
void VerifyGenerationAndLeaseBehavior()
{
    const uint64_t channel_id = UniqueChannelId();
    SegmentPtr writer = std::make_shared<SegmentT>(channel_id);
    WritableBlockLease invalid_write_lease;
    ReadableBlockLease invalid_read_lease;
    EXPECT_FALSE(invalid_write_lease);
    EXPECT_FALSE(invalid_read_lease);

    WritableBlock first_block;
    ASSERT_TRUE(writer->AcquireBlockToWrite(64, &first_block));
    ASSERT_NE(nullptr, first_block.block);
    ASSERT_GT(first_block.generation, 0U);
    const uint32_t first_index = first_block.index;
    const uint64_t first_generation = first_block.generation;
    {
        WritableBlockLease first_lease(writer, first_block);
        ASSERT_TRUE(first_lease);
        first_lease.Release();
        EXPECT_FALSE(first_lease);
        first_lease.Release();
    }

    ReadableInfo current_info(1, first_index, channel_id, first_generation);
    SegmentPtr reader = std::make_shared<SegmentT>(channel_id);
    ReadableBlock readable_block;
    readable_block.index = current_info.block_index();
    ASSERT_TRUE(reader->AcquireBlockToRead(&readable_block));
    {
        ReadableBlockLease read_lease(reader, readable_block);
        ASSERT_TRUE(read_lease);
        EXPECT_EQ(current_info.generation(), readable_block.block->generation());
        ReadableBlockLease moved_lease(std::move(read_lease));
        EXPECT_FALSE(read_lease);
        EXPECT_TRUE(moved_lease);
        moved_lease.Release();
        EXPECT_FALSE(moved_lease);
        moved_lease.Release();
    }

    WritableBlock reused_block;
    const uint32_t block_num = static_cast<uint32_t>(ShmConf().block_num());
    for(uint32_t i = 0; i < block_num; ++i){
        WritableBlock candidate;
        ASSERT_TRUE(writer->AcquireBlockToWrite(64, &candidate));
        if(candidate.index == first_index){
            reused_block = candidate;
            break;
        }
        writer->ReleaseWrittenBlock(candidate);
    }
    ASSERT_NE(nullptr, reused_block.block);
    ASSERT_NE(first_generation, reused_block.generation);
    {
        WritableBlockLease reused_lease(writer, reused_block);
        ASSERT_TRUE(reused_lease);
    }

    ReadableInfo stale_info(1, first_index, channel_id, first_generation);
    readable_block = ReadableBlock();
    readable_block.index = stale_info.block_index();
    ASSERT_TRUE(reader->AcquireBlockToRead(&readable_block));
    {
        ReadableBlockLease stale_lease(reader, readable_block);
        ASSERT_TRUE(stale_lease);
        EXPECT_NE(stale_info.generation(), readable_block.block->generation());
    }

    WritableBlock after_stale_read;
    for(uint32_t i = 0; i < block_num; ++i){
        WritableBlock candidate;
        ASSERT_TRUE(writer->AcquireBlockToWrite(64, &candidate));
        if(candidate.index == first_index){
            after_stale_read = candidate;
            break;
        }
        writer->ReleaseWrittenBlock(candidate);
    }
    ASSERT_NE(nullptr, after_stale_read.block);
    {
        WritableBlockLease automatic_lease(writer, after_stale_read);
        ASSERT_TRUE(automatic_lease);
    }
    bool early_return_acquired = false;
    EXPECT_FALSE(ReturnAfterWriteAcquire(writer, &early_return_acquired));
    EXPECT_TRUE(early_return_acquired);

    std::vector<WritableBlock> occupied_blocks;
    occupied_blocks.reserve(block_num);
    for(uint32_t i = 0; i < block_num; ++i){
        WritableBlock block;
        if(!writer->AcquireBlockToWrite(64, &block)){
            break;
        }
        occupied_blocks.push_back(block);
    }
    EXPECT_EQ(block_num, occupied_blocks.size());
    WritableBlock failed_block;
    EXPECT_FALSE(writer->AcquireBlockToWrite(64, &failed_block));
    WritableBlockLease failed_lease(writer, failed_block);
    EXPECT_FALSE(failed_lease);
    for(const auto& block : occupied_blocks){
        writer->ReleaseWrittenBlock(block);
    }
}

TEST(ShmBlockLeaseGenerationTest, PosixGenerationAndLeases)
{
    VerifyGenerationAndLeaseBehavior<PosixSegment>();
}

TEST(ShmBlockLeaseGenerationTest, XsiGenerationAndLeases)
{
    VerifyGenerationAndLeaseBehavior<XsiSegment>();
}

TEST(ConditionNotifierMemoryOrderTest, PublishesCompleteReadableInfo)
{
    const key_t key = UniqueNotifierKey();
    ConditionNotifier notifier(key, true);
    const uint64_t channel_base = 0x7c00000000000000ULL;
    const uint64_t host_base = 0x5a00000000000000ULL;
    const uint64_t message_count = kBufLength * 2 + 1;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);

    std::mutex mutex;
    std::condition_variable condition;
    uint64_t acknowledged = 0;
    uint64_t received = 0;
    std::atomic<bool> valid(true);
    std::atomic<bool> stop(false);

    std::thread listener([&]() {
        while(!stop.load() && received < message_count &&
              std::chrono::steady_clock::now() < deadline){
            ReadableInfo info;
            if(!notifier.Listen(10, &info)){
                continue;
            }
            if(info.channel_id() < channel_base ||
               (info.channel_id() - channel_base) % 3 != 0){
                continue;
            }

            const uint64_t sequence = info.host_id() - host_base;
            const uint32_t expected_index = static_cast<uint32_t>(
                sequence ^ 0x5a5a5a5aULL);
            const uint64_t expected_channel = channel_base + sequence * 3;
            const uint64_t expected_generation =
                info.host_id() ^ expected_channel;
            if(sequence == 0 || sequence > message_count ||
               info.block_index() != expected_index ||
               info.channel_id() != expected_channel ||
               info.generation() != expected_generation){
                valid.store(false);
                condition.notify_one();
                break;
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                acknowledged = sequence;
                ++received;
            }
            condition.notify_one();
        }
    });

    bool completed = true;
    for(uint64_t sequence = 1; sequence <= message_count; ++sequence){
        const uint64_t host_id = host_base + sequence;
        const uint64_t channel_id = channel_base + sequence * 3;
        const uint32_t block_index = static_cast<uint32_t>(
            sequence ^ 0x5a5a5a5aULL);
        const ReadableInfo info(host_id, block_index, channel_id,
                                host_id ^ channel_id);
        if(!notifier.Notify(info)){
            ADD_FAILURE() << "failed to publish notifier test message";
            completed = false;
            break;
        }

        std::unique_lock<std::mutex> lock(mutex);
        if(!condition.wait_until(lock, deadline, [&]() {
               return !valid.load() || acknowledged >= sequence;
           })){
            ADD_FAILURE() << "timed out waiting for notifier test message";
            completed = false;
            break;
        }
        if(!valid.load()){
            completed = false;
            break;
        }
    }

    stop.store(true);
    listener.join();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(valid.load());
    EXPECT_EQ(message_count, received);

    notifier.Shutdown();
    EXPECT_EQ(-1, shmget(key, 0, 0644));
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
