#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <cmw/transport/shm/posix_segment.h>
#include <cmw/transport/shm/xsi_segment.h>

using namespace hnu::cmw::transport;

namespace {

SegmentPtr MakeSegment(bool xsi, uint64_t id)
{
    if(xsi){
        return std::make_shared<XsiSegment>(id);
    }
    return std::make_shared<PosixSegment>(id);
}

uint64_t UniqueId()
{
    static uint32_t serial = 0;
    return 0x10000000u | ((getpid() & 0xffff) << 10) | (++serial);
}

// Own only resources successfully created with EXCL by this test.
struct Resource {
    bool xsi;
    uint64_t id = UniqueId();
    std::string name = "/cmw_" + std::to_string(id);
    int fd = -1;
    int shmid = -1;
    void* addr = nullptr;
    size_t size = 0;

    explicit Resource(bool use_xsi) : xsi(use_xsi) {}
    bool Create(size_t bytes) {
        size = bytes;
        if(xsi){
            shmid = shmget(static_cast<key_t>(id), size,
                           IPC_CREAT | IPC_EXCL | 0600);
            if(shmid < 0) return false;
            addr = shmat(shmid, nullptr, 0);
            if(addr == reinterpret_cast<void*>(-1)) addr = nullptr;
        } else {
            fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if(fd < 0 || ftruncate(fd, size) != 0) return false;
            addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if(addr == MAP_FAILED) addr = nullptr;
        }
        return addr != nullptr;
    }
    ~Resource() {
        if(addr){
            if(xsi) shmdt(addr);
            else munmap(addr, size);
        }
        if(shmid >= 0) shmctl(shmid, IPC_RMID, nullptr);
        if(fd >= 0){ close(fd); shm_unlink(name.c_str()); }
    }
};

int Child(bool xsi, uint64_t id, uint32_t index, uint64_t generation)
{
    auto segment = MakeSegment(xsi, id);
    ReadableBlock block;
    block.index = index;
    block.generation = generation;
    if(!segment->AcquireBlockToRead(&block)) return 10;
    ReadableBlockLease lease(segment, block);
    if(block.block->msg_size() != 5 || block.block->msg_info_size() != 0 ||
       block.block->generation() != generation ||
       std::memcmp(block.buf, "hello", 5) != 0) return 11;
    lease.Release();
    WritableBlock written;
    if(!segment->AcquireBlockToWrite(5, ShmMessageType::SERIALIZED, &written))
        return 12;
    WritableBlockLease write_lease(segment, written);
    std::memcpy(written.buf, "child", 5);
    written.block->set_msg_size(5);
    return 0;
}

void ExecReader(bool xsi, uint64_t id, const WritableBlock& block)
{
    const std::string backend = xsi ? "xsi" : "posix";
    const std::string channel = std::to_string(id);
    const std::string index = std::to_string(block.index);
    const std::string generation = std::to_string(block.generation);
    pid_t pid = fork();
    ASSERT_NE(-1, pid);
    if(pid == 0){
        execl("/proc/self/exe", "test_shm_segment_exec", "--reader",
              backend.c_str(), channel.c_str(), index.c_str(),
              generation.c_str(), nullptr);
        _exit(127);
    }
    int status = 0;
    pid_t result = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(std::chrono::steady_clock::now() < deadline){
        result = waitpid(pid, &status, WNOHANG);
        if(result == -1 && errno == EINTR) continue;
        if(result != 0) break;
        poll(nullptr, 0, 10);
    }
    if(result == 0){
        kill(pid, SIGKILL);
        while(waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
        FAIL() << "exec reader timed out";
    }
    ASSERT_EQ(pid, result);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
}

void IndependentProcesses(bool xsi)
{
    // Reserve a unique resource exclusively, then let the real backend create
    // it. The id includes the live parent's PID; never remove an existing id.
    Resource resource(xsi);
    ASSERT_TRUE(resource.Create(1));
    if(xsi){
        ASSERT_EQ(0, shmctl(resource.shmid, IPC_RMID, nullptr));
    } else {
        ASSERT_EQ(0, shm_unlink(resource.name.c_str()));
    }
    auto writer = MakeSegment(xsi, resource.id);
    WritableBlock block;
    ASSERT_TRUE(writer->AcquireBlockToWrite(5, ShmMessageType::SERIALIZED, &block));
    std::memcpy(block.buf, "hello", 5);
    block.block->set_msg_size(5);
    block.block->set_msg_info_size(0);
    writer->ReleaseWrittenBlock(block);
    // waitpid is the completion barrier; each exec opens and releases State
    // and Block, then the next independently loaded process reopens the same segment.
    ExecReader(xsi, resource.id, block);
    ExecReader(xsi, resource.id, block);
    EXPECT_EQ(ShmMessageType::SERIALIZED, writer->message_type());
    ReadableBlock reply;
    reply.index = (block.index + 1) % writer->block_num();
    ASSERT_TRUE(writer->AcquireBlockToRead(&reply));
    EXPECT_EQ(0, std::memcmp(reply.buf, "child", 5));
    writer->ReleaseReadBlock(reply);
    writer.reset();
    if(xsi){
        EXPECT_EQ(-1, shmget(static_cast<key_t>(resource.id), 0, 0600));
    } else {
        int fd = shm_open(resource.name.c_str(), O_RDWR, 0600);
        EXPECT_EQ(-1, fd);
        if(fd >= 0) close(fd);
    }
}

// Wire fixtures are deliberately independent of the implementation header.
struct Header {
    uint64_t magic = 0x434d5753484d3541ULL;
    uint32_t version = 2;
    uint32_t state_size = sizeof(State);
    uint32_t block_size = sizeof(Block);
    uint16_t state_align = alignof(State);
    uint16_t block_align = alignof(Block);
    uint64_t ceiling = 16384;
};

// The actual former State shape, including its process-private vptr.
struct OldState {
    virtual ~OldState() {}
    std::atomic<bool> remap{false};
    std::atomic<uint32_t> seq{0};
    std::atomic<uint32_t> refs{7};
    std::atomic<uint64_t> ceiling{16384};
    std::atomic<uint8_t> type{0};
};

void RejectLayouts(bool xsi)
{
    ShmConf conf;
    for(int scenario = 0; scenario < 12; ++scenario){
        Resource resource(xsi);
        size_t size = conf.managed_shm_size();
        if(scenario == 2) size = 1;
        if(scenario == 3) size = sizeof(Header) - 1;
        if(scenario == 4) size -= 1;  // unaligned trailer with otherwise valid metadata
        if(scenario == 10) size += 1;
        ASSERT_TRUE(resource.Create(size));
        if(scenario == 0){
            auto old = new (resource.addr) OldState();
            EXPECT_NE(sizeof(OldState), sizeof(State));
            EXPECT_NE(static_cast<char*>(resource.addr),
                      reinterpret_cast<char*>(&old->remap));
            struct OldHeader { uint64_t magic; uint32_t version, state, block, reserved; };
            OldHeader header{0x434d5753484d3541ULL, 1,
                             sizeof(OldState), 40, 0};
            std::memcpy(static_cast<char*>(resource.addr) + size - sizeof(header),
                        &header, sizeof(header));
        } else if(scenario >= 4 || scenario == 1){
            Header header;
            if(scenario == 1) header.version = 99;
            if(scenario == 5) ++header.state_size;
            if(scenario == 6) ++header.block_size;
            if(scenario == 7) ++header.block_align;
            if(scenario == 8) header.ceiling = 123;
            if(scenario == 9){
                auto state = new (resource.addr) State(131072);
                state->IncreaseReferenceCounts();
            }
            if(scenario == 11) ++header.state_align;
            std::memcpy(static_cast<char*>(resource.addr) + size - sizeof(header),
                        &header, sizeof(header));
        }
        const char* bytes = static_cast<const char*>(resource.addr);
        std::vector<char> before(bytes, bytes + size);
        {
            auto reader = MakeSegment(xsi, resource.id);
            ReadableBlock block;
            EXPECT_FALSE(reader->AcquireBlockToRead(&block)) << scenario;
            WritableBlock written;
            EXPECT_FALSE(reader->AcquireBlockToWrite(1, &written)) << scenario;
        }
        EXPECT_EQ(0, std::memcmp(before.data(), bytes, size)) << scenario;
    }
    // Full-size segment with no marker at all.
    Resource missing(xsi);
    ASSERT_TRUE(missing.Create(conf.managed_shm_size()));
    auto reader = MakeSegment(xsi, missing.id);
    ReadableBlock block;
    EXPECT_FALSE(reader->AcquireBlockToRead(&block));
}

TEST(ShmSegmentExecTest, Posix) { IndependentProcesses(false); }
TEST(ShmSegmentExecTest, Xsi) { IndependentProcesses(true); }
TEST(ShmSegmentExecTest, RejectPosixLayouts) { RejectLayouts(false); }
TEST(ShmSegmentExecTest, RejectXsiLayouts) { RejectLayouts(true); }

}  // namespace

int main(int argc, char** argv)
{
    if(argc == 6 && std::string(argv[1]) == "--reader"){
        return Child(std::string(argv[2]) == "xsi", std::stoull(argv[3]),
                     std::stoul(argv[4]), std::stoull(argv[5]));
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
