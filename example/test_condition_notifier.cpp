#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <poll.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <cmw/transport/shm/condition_notifier.h>

namespace hnu { namespace cmw { namespace transport {
// No runtime hooks or timing guesses: pause after taking the same publication
// lock as Notify, then resume through its actual publication implementation.
class ConditionNotifierTestPeer {
public:
    static std::atomic<uint32_t>& PublishLock(ConditionNotifier& n) {
        return n.indicator_->publish_lock;
    }
    static std::atomic<uint32_t>& SlotLock(ConditionNotifier& n, uint64_t seq) {
        return n.indicator_->slots[seq % kBufLength].lock;
    }
    static uint64_t End(ConditionNotifier& n) { return n.indicator_->next_seq.load(); }
    static bool Resume(ConditionNotifier& n, const ReadableInfo& info) {
        return n.PublishLocked(info);
    }
    static void SetEnd(ConditionNotifier& n, uint64_t seq) {
        n.indicator_->next_seq.store(seq);
    }
    static size_t Size() { return sizeof(ConditionNotifier::Indicator); }
};
}}}
using namespace hnu::cmw::transport;
using Peer = ConditionNotifierTestPeer;
using Clock = std::chrono::steady_clock;
namespace {
ReadableInfo Message(uint64_t id) {
    return ReadableInfo(id, static_cast<uint32_t>(id ^ 0xa5961234),
                        id * 7 + 19, id ^ 0xfedcba9876543210ULL);
}
bool Same(const ReadableInfo& a, const ReadableInfo& b) {
    return a.host_id() == b.host_id() && a.block_index() == b.block_index() &&
           a.channel_id() == b.channel_id() && a.generation() == b.generation();
}
bool Take(std::atomic<uint32_t>& lock) {
    uint32_t expected = 0;
    return lock.compare_exchange_strong(expected, 1, std::memory_order_acquire);
}
struct Held {
    std::atomic<uint32_t>& lock;
    bool owns;
    explicit Held(std::atomic<uint32_t>& value) : lock(value), owns(Take(lock)) {}
    ~Held() { Release(); }
    void Release() { if(owns) { lock.store(0, std::memory_order_release); owns = false; } }
};
key_t UniqueKey() {
    static unsigned serial = 0;
    return static_cast<key_t>(0x52000000U | ((getpid() & 0xffff) << 8) | ++serial);
}
class NotifierTest : public testing::Test {
protected:
    key_t key = UniqueKey();
    std::unique_ptr<ConditionNotifier> notifier;
    void SetUp() override {
        ASSERT_EQ(-1, shmget(key, 0, 0600));
        ASSERT_EQ(ENOENT, errno);
        notifier.reset(new ConditionNotifier(key, true));
        ASSERT_GE(shmget(key, 0, 0600), 0);
    }
    void TearDown() override {
        notifier.reset();
        EXPECT_EQ(-1, shmget(key, 0, 0600));
    }
};

TEST_F(NotifierTest, SlotContentionDropsWithoutAdvancingAndRecovers) {
    ASSERT_TRUE(notifier->Notify(Message(1)));
    // Hold a retained slot just as a reader does during its copy. Fill all
    // other slots, bringing the next writer back to this held slot.
    Held reader(Peer::SlotLock(*notifier, 1));
    ASSERT_TRUE(reader.owns);
    for(uint64_t i = 2; i <= kBufLength; ++i) ASSERT_TRUE(notifier->Notify(Message(i)));
    EXPECT_FALSE(notifier->Notify(Message(99999)));
    EXPECT_EQ(kBufLength + 1, Peer::End(*notifier));
    auto output = Message(999);
    const auto start = Clock::now();
    EXPECT_FALSE(notifier->Listen(25, &output));
    EXPECT_GE(Clock::now() - start, std::chrono::milliseconds(25));
    EXPECT_LT(Clock::now() - start, std::chrono::seconds(1));
    EXPECT_TRUE(Same(output, Message(999)));
    reader.Release();
    ASSERT_TRUE(notifier->Listen(0, &output));
    EXPECT_TRUE(Same(output, Message(1)));
    ASSERT_TRUE(notifier->Notify(Message(kBufLength + 1)));
    for(uint64_t i = 2; i <= kBufLength + 1; ++i) {
        ASSERT_TRUE(notifier->Listen(0, &output));
        ASSERT_TRUE(Same(output, Message(i)));
    }
    EXPECT_FALSE(notifier->Listen(0, &output));
}

TEST_F(NotifierTest, BusyReaderRetriesWithinDeadline) {
    ASSERT_TRUE(notifier->Notify(Message(7)));
    Held held(Peer::SlotLock(*notifier, 1));
    ASSERT_TRUE(held.owns);
    std::atomic<bool> entered{false};
    bool success = false;
    ReadableInfo output;
    std::thread listener([&] { entered.store(true); success = notifier->Listen(1000, &output); });
    while(!entered.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    held.Release();
    listener.join();
    EXPECT_TRUE(success);
    EXPECT_TRUE(Same(output, Message(7)));
}

TEST_F(NotifierTest, PausedPublisherCannotBeOvertaken) {
    std::atomic<bool> ready{false}, resume{false};
    bool success = false;
    std::thread writer([&] {
        Held publish(Peer::PublishLock(*notifier));
        if(!publish.owns) { ready.store(true); return; }
        ready.store(true);
        while(!resume.load()) std::this_thread::yield();
        success = Peer::Resume(*notifier, Message(1));
    });
    while(!ready.load()) std::this_thread::yield();
    for(uint64_t i = 0; i < kBufLength * 3; ++i) EXPECT_FALSE(notifier->Notify(Message(100 + i)));
    EXPECT_EQ(1U, Peer::End(*notifier));
    auto output = Message(999);
    EXPECT_FALSE(notifier->Listen(5, &output));
    EXPECT_TRUE(Same(output, Message(999)));
    resume.store(true);
    writer.join();
    ASSERT_TRUE(success);
    ASSERT_TRUE(notifier->Listen(10, &output));
    EXPECT_TRUE(Same(output, Message(1)));
    for(uint64_t i = 2; i < kBufLength * 3; ++i) {
        ASSERT_TRUE(notifier->Notify(Message(i)));
        ASSERT_TRUE(notifier->Listen(10, &output));
        ASSERT_TRUE(Same(output, Message(i)));
        ASSERT_EQ(i + 1, Peer::End(*notifier));
    }
}

TEST_F(NotifierTest, SlowReaderStartsAtOldestRetainedAfterMultipleWraps) {
    ConditionNotifier second(key, false);
    const uint64_t total = kBufLength * 3 + 29;
    for(uint64_t i = 1; i <= total; ++i) ASSERT_TRUE(notifier->Notify(Message(i)));
    for(auto* reader : {notifier.get(), &second}) {
        ReadableInfo output;
        for(uint64_t i = total - kBufLength + 1; i <= total; ++i) {
            ASSERT_TRUE(reader->Listen(0, &output));
            ASSERT_TRUE(Same(output, Message(i))) << i;
        }
        EXPECT_FALSE(reader->Listen(0, &output));
    }
    ASSERT_TRUE(notifier->Notify(Message(total + 1)));
    for(auto* reader : {notifier.get(), &second}) {
        ReadableInfo output;
        ASSERT_TRUE(reader->Listen(0, &output));
        EXPECT_TRUE(Same(output, Message(total + 1)));
    }
}

TEST_F(NotifierTest, ConcurrentWritersKeepFieldsAndSequencesConsistent) {
    constexpr uint64_t writers = 4, per_writer = 16000, total = writers * per_writer;
    std::vector<uint8_t> accepted(total + 1, 0);
    std::set<uint64_t> received;
    std::atomic<bool> start{false}, done{false};
    bool valid = true;
    std::thread reader([&] {
        while(!start.load()) std::this_thread::yield();
        while(!done.load()) {
            ReadableInfo output;
            if(!notifier->Listen(1, &output)) continue;
            const auto id = output.host_id();
            if(id == 0 || id > total || !Same(output, Message(id)) ||
               !received.insert(id).second) valid = false;
        }
    });
    std::vector<std::thread> threads;
    for(uint64_t w = 0; w < writers; ++w) threads.emplace_back([&, w] {
        while(!start.load()) std::this_thread::yield();
        for(uint64_t i = 1; i <= per_writer; ++i) {
            const auto id = w * per_writer + i;
            accepted[id] = notifier->Notify(Message(id));
        }
    });
    start.store(true);
    for(auto& thread : threads) thread.join();
    done.store(true);
    reader.join();
    ReadableInfo output;
    while(notifier->Listen(0, &output)) {
        const auto id = output.host_id();
        ASSERT_GE(id, 1U); ASSERT_LE(id, total);
        EXPECT_TRUE(Same(output, Message(id)));
        EXPECT_TRUE(received.insert(id).second);
    }
    EXPECT_TRUE(valid);
    EXPECT_FALSE(received.empty());
    uint64_t successes = 0;
    for(auto value : accepted) successes += value;
    EXPECT_EQ(successes + 1, Peer::End(*notifier));
    for(auto id : received) { ASSERT_LE(id, total); EXPECT_EQ(1, accepted[id]); }
    ASSERT_TRUE(notifier->Notify(Message(total + 1)));
    ASSERT_TRUE(notifier->Listen(50, &output));
    EXPECT_TRUE(Same(output, Message(total + 1)));
    EXPECT_FALSE(notifier->Listen(2, &output));
    std::cout << "accepted=" << successes << " dropped=" << total - successes
              << " received=" << received.size() << std::endl;
}

TEST_F(NotifierTest, InvalidArgumentsShutdownAndSequenceExhaustion) {
    auto output = Message(1);
    EXPECT_FALSE(notifier->Listen(-1, &output));
    EXPECT_FALSE(notifier->Listen(0, nullptr));
    Peer::SetEnd(*notifier, std::numeric_limits<uint64_t>::max() - 1);
    EXPECT_TRUE(notifier->Notify(Message(2)));
    EXPECT_FALSE(notifier->Notify(Message(3)));
    EXPECT_EQ(std::numeric_limits<uint64_t>::max(), Peer::End(*notifier));
    notifier->Shutdown();
    EXPECT_FALSE(notifier->Notify(Message(4)));
    EXPECT_FALSE(notifier->Listen(0, &output));
    notifier->Shutdown();
}

bool Byte(int fd, char* value, bool write_byte) {
    if(write_byte) return write(fd, value, 1) == 1;
    pollfd p{fd, POLLIN, 0};
    int result;
    do { result = poll(&p, 1, 5000); } while(result < 0 && errno == EINTR);
    return result > 0 && read(fd, value, 1) == 1;
}
struct Child {
    pid_t pid = -1;
    int command = -1, reply = -1;
    bool Start(const char* mode, key_t key) {
        int to_child[2], from_child[2];
        if(pipe(to_child)) return false;
        if(pipe(from_child)) { close(to_child[0]); close(to_child[1]); return false; }
        const std::string key_arg = std::to_string(key);
        const std::string input = std::to_string(to_child[0]);
        const std::string output = std::to_string(from_child[1]);
        pid = fork();
        if(pid == 0) {
            close(to_child[1]); close(from_child[0]);
            execl("/proc/self/exe", "test_condition_notifier", "--child", mode,
                  key_arg.c_str(), input.c_str(), output.c_str(), nullptr);
            _exit(127);
        }
        close(to_child[0]); close(from_child[1]);
        command = to_child[1]; reply = from_child[0];
        return pid > 0;
    }
    bool Ready() { char c = 0; return Byte(reply, &c, false) && c == 'R'; }
    bool Go() { char c = 'G'; return Byte(command, &c, true); }
    int Wait() {
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        int status = 0;
        while(Clock::now() < deadline) {
            auto result = waitpid(pid, &status, WNOHANG);
            if(result == pid) { pid = -1; return WIFEXITED(status) ? WEXITSTATUS(status) : -2; }
            if(result < 0 && errno != EINTR) return -3;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return -4;
    }
    ~Child() {
        if(pid > 0) { kill(pid, SIGKILL); while(waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }
        if(command >= 0) close(command);
        if(reply >= 0) close(reply);
    }
};
int ChildMain(const std::string& mode, key_t key, int input, int output) {
    ConditionNotifier notifier(key, false);
    char c = 'R';
    if(mode == "reject") {
        auto info = Message(777);
        auto start = Clock::now();
        if(notifier.Notify(Message(1)) || notifier.Listen(20, &info) ||
           !Same(info, Message(777)) || Clock::now() - start > std::chrono::seconds(1)) return 10;
        return 0;
    }
    if(mode == "slot" || mode == "publish") {
        Held held(mode == "slot" ? Peer::SlotLock(notifier, 1) : Peer::PublishLock(notifier));
        if(!held.owns || !Byte(output, &c, true) || !Byte(input, &c, false)) return 11;
        if(mode == "publish" && !Peer::Resume(notifier, Message(1))) return 12;
        return 0;
    }
    if(!Byte(output, &c, true) || !Byte(input, &c, false)) return 13;
    ReadableInfo info;
    for(uint64_t i = 1; i <= 128; ++i) {
        if(!notifier.Listen(1000, &info) || !Same(info, Message(i))) return 14;
    }
    info = Message(777);
    auto start = Clock::now();
    if(notifier.Listen(30, &info) || !Same(info, Message(777)) ||
       Clock::now() - start < std::chrono::milliseconds(30) ||
       Clock::now() - start > std::chrono::seconds(1)) return 15;
    return 0;
}
TEST_F(NotifierTest, ExecProcessesShareSlotAndPublicationLocks) {
    Child slot;
    ASSERT_TRUE(slot.Start("slot", key)); ASSERT_TRUE(slot.Ready());
    EXPECT_FALSE(notifier->Notify(Message(999)));
    EXPECT_EQ(1U, Peer::End(*notifier));
    ASSERT_TRUE(slot.Go()); ASSERT_EQ(0, slot.Wait());
    Child publisher;
    ASSERT_TRUE(publisher.Start("publish", key)); ASSERT_TRUE(publisher.Ready());
    for(uint64_t i = 0; i < kBufLength * 3; ++i) EXPECT_FALSE(notifier->Notify(Message(i + 50)));
    EXPECT_EQ(1U, Peer::End(*notifier));
    ReadableInfo info;
    EXPECT_FALSE(notifier->Listen(20, &info));
    ASSERT_TRUE(publisher.Go()); ASSERT_EQ(0, publisher.Wait());
    EXPECT_EQ(2U, Peer::End(*notifier));
    ASSERT_TRUE(notifier->Listen(50, &info)); EXPECT_TRUE(Same(info, Message(1)));
    ASSERT_TRUE(notifier->Notify(Message(2)));
    ASSERT_TRUE(notifier->Listen(50, &info)); EXPECT_TRUE(Same(info, Message(2)));
    EXPECT_FALSE(notifier->Listen(0, &info));
}
TEST_F(NotifierTest, ExecReadersBroadcastAndTimeoutIndependently) {
    Child first, second;
    ASSERT_TRUE(first.Start("reader", key)); ASSERT_TRUE(second.Start("reader", key));
    ASSERT_TRUE(first.Ready()); ASSERT_TRUE(second.Ready());
    for(uint64_t i = 1; i <= 128; ++i) ASSERT_TRUE(notifier->Notify(Message(i)));
    ASSERT_TRUE(first.Go()); ASSERT_TRUE(second.Go());
    EXPECT_EQ(0, first.Wait()); EXPECT_EQ(0, second.Wait());
}

// Independent wire fixture: catches both unversioned and same-size bad ABIs.
struct Header {
    uint64_t magic = 0x434d574e4f544631ULL;
    uint32_t version = 1, size = Peer::Size(), slot_size = 56, info_size = 40;
    uint32_t slot_align = 8, info_align = 8, capacity = 4096, slots_offset = 56;
};
struct RawSegment {
    key_t key = UniqueKey();
    int id = -1;
    void* addr = nullptr;
    bool Create(size_t size) {
        id = shmget(key, size, IPC_CREAT | IPC_EXCL | 0600);
        if(id < 0) return false;
        addr = shmat(id, nullptr, 0);
        if(addr == reinterpret_cast<void*>(-1)) addr = nullptr;
        return addr != nullptr;
    }
    ~RawSegment() {
        if(addr) shmdt(addr);
        if(id >= 0) shmctl(id, IPC_RMID, nullptr);
    }
};
TEST_F(NotifierTest, CreatedHeaderMatchesIndependentWireFixture) {
    const int id = shmget(key, 0, 0600);
    ASSERT_GE(id, 0);
    void* addr = shmat(id, nullptr, SHM_RDONLY);
    ASSERT_NE(reinterpret_cast<void*>(-1), addr);
    Header expected;
    EXPECT_EQ(0, std::memcmp(addr, &expected, sizeof(expected)));
    EXPECT_EQ(0, shmdt(addr));
}
TEST(NotifierLayoutTest, ExecRejectsOldWrongSizeVersionAndAbiWithoutMutation) {
    for(int scenario = 0; scenario < 14; ++scenario) {
        RawSegment resource;
        size_t size = Peer::Size();
        if(scenario == 0) size = sizeof(uint64_t) + kBufLength * (sizeof(ReadableInfo) + sizeof(uint64_t));
        if(scenario == 1) size = 1;
        if(scenario == 2) --size;
        if(scenario == 3) ++size;
        ASSERT_TRUE(resource.Create(size));
        if(scenario >= 4 && scenario != 12) {
            Header header;
            if(scenario == 13) ++header.magic;
            if(scenario == 4) ++header.version;
            if(scenario == 5) ++header.size;
            if(scenario == 6) ++header.slot_size;
            if(scenario == 7) ++header.info_size;
            if(scenario == 8) ++header.slot_align;
            if(scenario == 9) ++header.info_align;
            if(scenario == 10) ++header.capacity;
            if(scenario == 11) ++header.slots_offset;
            std::memcpy(resource.addr, &header, sizeof(header));
        }
        if(scenario == 0) *static_cast<uint64_t*>(resource.addr) = 123;
        const char* data = static_cast<char*>(resource.addr);
        const std::vector<char> before(data, data + size);
        Child child;
        ASSERT_TRUE(child.Start("reject", resource.key));
        EXPECT_EQ(0, child.Wait()) << scenario;
        EXPECT_EQ(0, std::memcmp(before.data(), data, size)) << scenario;
        EXPECT_EQ(resource.id, shmget(resource.key, 0, 0600));
        shmid_ds state{};
        ASSERT_EQ(0, shmctl(resource.id, IPC_STAT, &state));
        EXPECT_EQ(1U, state.shm_nattch);  // Failed opener detached.
    }
}
}  // namespace
int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    if(argc == 6 && std::string(argv[1]) == "--child")
        return ChildMain(argv[2], static_cast<key_t>(std::stoll(argv[3])), std::stoi(argv[4]), std::stoi(argv[5]));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
