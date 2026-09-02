#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/transport/shm/posix_segment.h>
#include <cmw/transport/shm/segment_factory.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

struct WrittenBlock {
    uint32_t index;
    std::vector<uint8_t> payload;
};

uint64_t UniqueChannelId()
{
    return (static_cast<uint64_t>(getpid()) << 32) ^
           static_cast<uint64_t>(std::chrono::steady_clock::now()
                                     .time_since_epoch()
                                     .count());
}

std::string PosixShmName(uint64_t channel_id)
{
    return "/cmw_" + std::to_string(channel_id);
}

int ReadWrittenBlocks(uint64_t channel_id,
                      const std::vector<WrittenBlock>& written_blocks)
{
    PosixSegment reader(channel_id);
    for(const auto& written_block : written_blocks){
        ReadableBlock readable_block;
        readable_block.index = written_block.index;
        if(!reader.AcquireBlockToRead(&readable_block)){
            return 1;
        }
        const bool matched = readable_block.buf != nullptr &&
            std::memcmp(readable_block.buf, written_block.payload.data(),
                        written_block.payload.size()) == 0;
        reader.ReleaseReadBlock(readable_block);
        if(!matched){
            return 2;
        }
    }
    return 0;
}

void ExpectChildReadsBlocks(uint64_t channel_id,
                            const std::vector<WrittenBlock>& written_blocks)
{
    const pid_t child = fork();
    ASSERT_NE(-1, child);
    if(child == 0){
        _exit(ReadWrittenBlocks(channel_id, written_blocks));
    }

    int status = 0;
    ASSERT_EQ(child, waitpid(child, &status, 0));
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(PosixSegmentMultiprocessTest, OpensReadsMultipleBlocksAndReopens)
{
    const uint64_t channel_id = UniqueChannelId();
    const std::string shm_name = PosixShmName(channel_id);
    std::vector<WrittenBlock> written_blocks;

    {
        PosixSegment writer(channel_id);
        for(uint32_t i = 0; i < 2; ++i){
            WritableBlock writable_block;
            ASSERT_TRUE(writer.AcquireBlockToWrite(128, &writable_block));
            ASSERT_NE(nullptr, writable_block.buf);

            WrittenBlock written_block;
            written_block.index = writable_block.index;
            written_block.payload.assign(
                128, static_cast<uint8_t>(0x30 + i));
            std::memcpy(writable_block.buf, written_block.payload.data(),
                        written_block.payload.size());
            writer.ReleaseWrittenBlock(writable_block);
            written_blocks.emplace_back(std::move(written_block));
        }

        ASSERT_NE(written_blocks[0].index, written_blocks[1].index);
        ExpectChildReadsBlocks(channel_id, written_blocks);
        ExpectChildReadsBlocks(channel_id, written_blocks);
    }

    errno = 0;
    const int fd = shm_open(shm_name.c_str(), O_RDWR, 0644);
    EXPECT_EQ(-1, fd);
    EXPECT_EQ(ENOENT, errno);
}

TEST(PosixSegmentMultiprocessTest, SegmentFactoryDefaultsToPosix)
{
    SegmentPtr segment = SegmentFactory::CreateSegment(UniqueChannelId());
    EXPECT_NE(nullptr, std::dynamic_pointer_cast<PosixSegment>(segment));
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
