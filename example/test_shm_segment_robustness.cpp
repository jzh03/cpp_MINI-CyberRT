#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <cmw/transport/shm/segment.h>
#include <cmw/transport/shm/shm_conf.h>

namespace hnu{
namespace cmw{
namespace transport{
namespace {

const uint64_t kSmallMessageSize = 1024;
const uint64_t kOneMegabyte = 1024 * 1024;
const uint64_t kEightMegabytes = 8 * 1024 * 1024;

struct PosixStateMappings
{
    explicit PosixStateMappings(std::string value) : name(std::move(value)) {}

    ~PosixStateMappings()
    {
        if(opener_mapping != MAP_FAILED) {
            munmap(opener_mapping, sizeof(State));
        }
        if(creator_mapping != MAP_FAILED) {
            munmap(creator_mapping, sizeof(State));
        }
        if(opener_fd >= 0) {
            close(opener_fd);
        }
        if(creator_fd >= 0) {
            close(creator_fd);
        }
        if(owns_name) {
            shm_unlink(name.c_str());
        }
    }

    std::string name;
    bool owns_name = false;
    int creator_fd = -1;
    int opener_fd = -1;
    void* creator_mapping = MAP_FAILED;
    void* opener_mapping = MAP_FAILED;
};

// Keep Segment's real acquire, recreate, and Block locking behavior while
// replacing the operating-system shared-memory mapping with test-owned memory.
// Every block points at the same capacity-sized test buffer because these
// tests never write blocks concurrently; this avoids allocating the roughly
// 256 MiB maximum segment while keeping every returned buffer large enough.
class MemorySegment : public Segment
{
public:
    explicit MemorySegment(bool force_undersized_recreate = false)
        : Segment(1),
          force_undersized_recreate_(force_undersized_recreate) {}

    ~MemorySegment() override { Reset(); }

    uint64_t ceiling_msg_size() { return conf_.ceiling_msg_size(); }
    uint64_t block_num() { return conf_.block_num(); }
    uint32_t open_or_create_count() const { return open_or_create_count_; }
    uint32_t open_only_count() const { return open_only_count_; }

    bool ScheduleRemap(uint64_t msg_size)
    {
        if(state_ == nullptr){
            return false;
        }
        remap_msg_size_ = msg_size;
        has_scheduled_remap_ = true;
        state_->set_need_remap(true);
        return true;
    }

private:
    bool InitializeStorage()
    {
        ShmConf default_conf;
        state_storage_.reset(new State(conf_.ceiling_msg_size()));
        block_storage_.reset(new Block[default_conf.block_num()]);
        dummy_buffer_.reset(new uint8_t[conf_.block_buf_size()]);

        state_ = state_storage_.get();
        blocks_ = block_storage_.get();
        managed_shm_ = dummy_buffer_.get();
        {
            std::lock_guard<std::mutex> lock(block_buf_lock_);
            block_buf_addrs_.clear();
            for(uint32_t i = 0; i < conf_.block_num(); ++i){
                block_buf_addrs_[i] = dummy_buffer_.get();
            }
        }
        init_ = true;
        return true;
    }

    bool OpenOrCreate() override
    {
        if(init_){
            return true;
        }
        ++open_or_create_count_;
        if(force_undersized_recreate_ && open_or_create_count_ > 1){
            conf_.Update(kSmallMessageSize);
        }
        return InitializeStorage();
    }

    bool OpenOnly() override
    {
        if(init_){
            return true;
        }
        ++open_only_count_;
        if(has_scheduled_remap_){
            conf_.Update(remap_msg_size_);
            has_scheduled_remap_ = false;
        }
        return InitializeStorage();
    }

    bool Remove() override { return true; }

    void Reset() override
    {
        init_ = false;
        state_ = nullptr;
        blocks_ = nullptr;
        managed_shm_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(block_buf_lock_);
            block_buf_addrs_.clear();
        }
        dummy_buffer_.reset();
        block_storage_.reset();
        state_storage_.reset();
    }

    bool force_undersized_recreate_ = false;
    bool has_scheduled_remap_ = false;
    uint64_t remap_msg_size_ = 0;
    uint32_t open_or_create_count_ = 0;
    uint32_t open_only_count_ = 0;
    std::unique_ptr<State> state_storage_;
    std::unique_ptr<Block[]> block_storage_;
    std::unique_ptr<uint8_t[]> dummy_buffer_;
};

void ExpectAcquireAndRelease(MemorySegment* segment, std::size_t msg_size)
{
    WritableBlock block;
    ASSERT_TRUE(segment->AcquireBlockToWrite(msg_size, &block));
    EXPECT_NE(nullptr, block.block);
    EXPECT_NE(nullptr, block.buf);
    EXPECT_GE(segment->ceiling_msg_size(), msg_size);
    segment->ReleaseWrittenBlock(block);
}

int RunAllBlocksOccupiedScenario()
{
    MemorySegment segment;
    ShmConf conf;
    const uint64_t max_message_size = conf.max_message_size();

    WritableBlock initial_block;
    if(!segment.AcquireBlockToWrite(max_message_size, &initial_block)){
        return 10;
    }
    segment.ReleaseWrittenBlock(initial_block);

    std::vector<WritableBlock> occupied_blocks;
    occupied_blocks.reserve(segment.block_num());
    for(uint32_t i = 0; i < segment.block_num(); ++i){
        WritableBlock block;
        if(!segment.AcquireBlockToWrite(kSmallMessageSize, &block)){
            for(const auto& occupied_block : occupied_blocks){
                segment.ReleaseWrittenBlock(occupied_block);
            }
            return 11;
        }
        occupied_blocks.emplace_back(block);
    }

    WritableBlock extra_block;
    bool acquired =
        segment.AcquireBlockToWrite(kSmallMessageSize, &extra_block);
    if(acquired){
        segment.ReleaseWrittenBlock(extra_block);
    }
    for(const auto& occupied_block : occupied_blocks){
        segment.ReleaseWrittenBlock(occupied_block);
    }
    return acquired ? 12 : 0;
}

TEST(ShmSegmentRobustnessTest, SmallMessageUsesInitialSegment)
{
    MemorySegment segment;

    ExpectAcquireAndRelease(&segment, kSmallMessageSize);

    ShmConf default_conf;
    EXPECT_EQ(default_conf.ceiling_msg_size(), segment.ceiling_msg_size());
    EXPECT_EQ(1u, segment.open_or_create_count());
}

TEST(ShmSegmentRobustnessTest, LargerMessageTriggersRecreate)
{
    MemorySegment segment;

    ExpectAcquireAndRelease(&segment, kSmallMessageSize);
    ExpectAcquireAndRelease(&segment, kOneMegabyte);

    EXPECT_EQ(ShmConf(kOneMegabyte).ceiling_msg_size(),
              segment.ceiling_msg_size());
    EXPECT_EQ(2u, segment.open_or_create_count());
}

TEST(ShmSegmentRobustnessTest, MaximumMessageBoundaries)
{
    MemorySegment segment;
    ShmConf conf;
    const uint64_t max_message_size = conf.max_message_size();

    ExpectAcquireAndRelease(&segment, max_message_size - 1024);
    ExpectAcquireAndRelease(&segment, max_message_size - 1);
    ExpectAcquireAndRelease(&segment, max_message_size);

    const uint32_t open_count = segment.open_or_create_count();
    WritableBlock rejected_block;
    EXPECT_FALSE(segment.AcquireBlockToWrite(max_message_size + 1,
                                             &rejected_block));
    EXPECT_EQ(nullptr, rejected_block.block);
    EXPECT_EQ(nullptr, rejected_block.buf);
    EXPECT_EQ(open_count, segment.open_or_create_count());
    EXPECT_EQ(max_message_size, segment.ceiling_msg_size());

    // A rejected oversized message must not poison the existing segment.
    ExpectAcquireAndRelease(&segment, kSmallMessageSize);
}

TEST(ShmSegmentRobustnessTest, RecreateRejectsUndersizedMappedSegment)
{
    MemorySegment segment(true);
    WritableBlock block;

    bool acquired = segment.AcquireBlockToWrite(kOneMegabyte, &block);
    if(acquired){
        segment.ReleaseWrittenBlock(block);
    }

    EXPECT_FALSE(acquired);
    EXPECT_LT(segment.ceiling_msg_size(), kOneMegabyte);
    EXPECT_EQ(2u, segment.open_or_create_count());
}

TEST(ShmSegmentRobustnessTest, SmallLargerSmallKeepsExpandedSegmentUsable)
{
    MemorySegment segment;

    ExpectAcquireAndRelease(&segment, kSmallMessageSize);
    ExpectAcquireAndRelease(&segment, kEightMegabytes);
    const uint64_t expanded_capacity = segment.ceiling_msg_size();
    const uint32_t open_count = segment.open_or_create_count();
    ExpectAcquireAndRelease(&segment, kSmallMessageSize);

    EXPECT_GE(expanded_capacity, kEightMegabytes);
    EXPECT_EQ(expanded_capacity, segment.ceiling_msg_size());
    EXPECT_EQ(open_count, segment.open_or_create_count());
}

TEST(ShmSegmentRobustnessTest, AllBlocksOccupiedReturnsWithoutBusySpin)
{
    pid_t child = fork();
    ASSERT_NE(-1, child);
    if(child == 0){
        alarm(1);
        int result = RunAllBlocksOccupiedScenario();
        alarm(0);
        _exit(result);
    }

    int status = 0;
    pid_t finished = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(std::chrono::steady_clock::now() < deadline){
        finished = waitpid(child, &status, WNOHANG);
        if(finished != 0){
            break;
        }
        poll(nullptr, 0, 10);
    }

    if(finished == 0){
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        FAIL() << "child did not finish within the watchdog deadline";
    }
    ASSERT_EQ(child, finished);
    ASSERT_TRUE(WIFEXITED(status))
        << "child terminated by signal " << WTERMSIG(status);
    EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(ShmSegmentRobustnessTest, ReadIndexIsValidatedAfterRemap)
{
    MemorySegment segment;
    ExpectAcquireAndRelease(&segment, kSmallMessageSize);

    ShmConf remapped_conf(kOneMegabyte);
    const uint32_t stale_index =
        static_cast<uint32_t>(remapped_conf.block_num());
    EXPECT_LT(stale_index, segment.block_num());
    ASSERT_TRUE(segment.ScheduleRemap(kOneMegabyte));

    ReadableBlock block;
    block.index = stale_index;
    bool acquired = segment.AcquireBlockToRead(&block);
    if(acquired){
        segment.ReleaseReadBlock(block);
    }

    EXPECT_FALSE(acquired);
    EXPECT_EQ(nullptr, block.block);
    EXPECT_EQ(nullptr, block.buf);
    EXPECT_EQ(remapped_conf.block_num(), segment.block_num());
    EXPECT_EQ(1u, segment.open_only_count());
}

TEST(ShmSegmentRobustnessTest, MappedOpenerCannotAttachAfterClosingStarts)
{
    PosixStateMappings resources(
        "/cmw_closing_attach_" +
        std::to_string(static_cast<long>(getpid())) + "_" +
        std::to_string(std::chrono::steady_clock::now()
                           .time_since_epoch().count()));
    resources.creator_fd =
        shm_open(resources.name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    resources.owns_name = resources.creator_fd >= 0;
    ASSERT_NE(-1, resources.creator_fd);
    ASSERT_EQ(0, ftruncate(resources.creator_fd, sizeof(State)));
    resources.creator_mapping =
        mmap(nullptr, sizeof(State), PROT_READ | PROT_WRITE, MAP_SHARED,
             resources.creator_fd, 0);
    ASSERT_NE(MAP_FAILED, resources.creator_mapping);
    State* creator_state =
        new (resources.creator_mapping) State(kSmallMessageSize);

    // Complete the second mmap, then pause before the attach CAS.  This is the
    // exact window that used to let an opener retain an unlinked incarnation.
    resources.opener_fd = shm_open(resources.name.c_str(), O_RDWR, 0600);
    ASSERT_NE(-1, resources.opener_fd);
    resources.opener_mapping =
        mmap(nullptr, sizeof(State), PROT_READ | PROT_WRITE, MAP_SHARED,
             resources.opener_fd, 0);
    ASSERT_NE(MAP_FAILED, resources.opener_mapping);
    State* const already_mapped =
        static_cast<State*>(resources.opener_mapping);

    EXPECT_EQ(1u, creator_state->reference_counts());
    ASSERT_TRUE(creator_state->ReleaseReference());
    ASSERT_TRUE(creator_state->is_closing());
    ASSERT_EQ(0, shm_unlink(resources.name.c_str()));

    EXPECT_FALSE(already_mapped->TryAcquireReference());
    EXPECT_TRUE(already_mapped->is_closing());
    EXPECT_EQ(0u, already_mapped->reference_counts());

}

TEST(ShmSegmentRobustnessTest, LastReferenceHasOneRemovalWinner)
{
    const uint32_t owner_count = 32;
    State state(kSmallMessageSize);
    for(uint32_t i = 1; i < owner_count; ++i) {
        ASSERT_TRUE(state.TryAcquireReference());
    }
    ASSERT_EQ(owner_count, state.reference_counts());

    std::atomic<bool> start(false);
    std::atomic<uint32_t> removal_winners(0);
    std::vector<std::thread> owners;
    owners.reserve(owner_count);
    for(uint32_t i = 0; i < owner_count; ++i) {
        owners.emplace_back([&]() {
            while(!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if(state.ReleaseReference()) {
                removal_winners.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for(auto& owner : owners) {
        owner.join();
    }

    EXPECT_EQ(1u, removal_winners.load());
    EXPECT_TRUE(state.is_closing());
    EXPECT_EQ(0u, state.reference_counts());
    EXPECT_FALSE(state.ReleaseReference());
    EXPECT_FALSE(state.TryAcquireReference());
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
