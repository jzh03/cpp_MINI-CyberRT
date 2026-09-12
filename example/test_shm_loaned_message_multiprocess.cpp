#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/shm/shm_conf.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

config::RoleAttributes MakeRoleAttributes(const std::string& channel_name,
                                          const std::string& suffix)
{
    auto global_data = common::GlobalData::Instance();
    config::RoleAttributes attr{};
    attr.channel_name = channel_name;
    attr.channel_id = common::GlobalData::RegisterChannel(channel_name);
    attr.host_name = global_data->HostName();
    attr.host_ip = global_data->HostIp();
    attr.process_id = global_data->ProcessId();
    attr.node_name = channel_name + suffix;
    attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
    attr.id = common::GlobalData::GenerateHashId(attr.node_name);
    attr.message_type = "LoanedMessage";
    return attr;
}

bool ReadByteWithTimeout(int fd, char* value, int timeout_ms)
{
    pollfd descriptor{fd, POLLIN, 0};
    if(poll(&descriptor, 1, timeout_ms) != 1 ||
       !(descriptor.revents & POLLIN)) {
        return false;
    }
    return read(fd, value, sizeof(*value)) == sizeof(*value);
}

bool WriteByte(int fd, char value)
{
    return write(fd, &value, sizeof(value)) == sizeof(value);
}

int RunSubscriber(const std::string& channel_name, int parent_read_fd,
                  int parent_write_fd)
{
    hnu::cmw::Init("ShmLoanedMessageSubscriber");
    std::shared_ptr<LoanedMessage> held_message;
    bool valid_payload = false;
    const std::string expected_payload = "loaned-multiprocess";

    ShmReceiver<LoanedMessage> receiver(
        MakeRoleAttributes(channel_name, "_subscriber"),
        [&](const std::shared_ptr<LoanedMessage>& message,
            const MessageInfo&, const config::RoleAttributes&) {
            valid_payload = message->size() == expected_payload.size() &&
                std::memcmp(message->data(), expected_payload.data(),
                            expected_payload.size()) == 0;
            held_message = message;
            WriteByte(parent_write_fd, valid_payload ? 'R' : 'E');
        });
    receiver.Enable();
    if(!WriteByte(parent_write_fd, 'S')) {
        return 2;
    }

    char control = 0;
    if(!ReadByteWithTimeout(parent_read_fd, &control, 5000) || control != 'X') {
        return 3;
    }
    held_message.reset();
    if(!WriteByte(parent_write_fd, valid_payload ? 'D' : 'E')) {
        return 4;
    }

    receiver.Disable();
    ShmDispatcher::Instance()->Shutdown();
    return 0;
}

TEST(ShmLoanedMessageMultiprocessTest, PosixReaderLeaseBackpressuresWriter)
{
    const std::string channel_name =
        "loaned_multiprocess_" + std::to_string(getpid()) + "_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    int parent_to_child[2] = {-1, -1};
    int child_to_parent[2] = {-1, -1};
    ASSERT_EQ(0, pipe(parent_to_child));
    ASSERT_EQ(0, pipe(child_to_parent));

    const pid_t child = fork();
    ASSERT_NE(-1, child);
    if(child == 0) {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        const int result = RunSubscriber(channel_name, parent_to_child[0],
                                         child_to_parent[1]);
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        _exit(result);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    signal(SIGPIPE, SIG_IGN);

    char signal = 0;
    ASSERT_TRUE(ReadByteWithTimeout(child_to_parent[0], &signal, 5000));
    ASSERT_EQ('S', signal);

    hnu::cmw::Init("ShmLoanedMessagePublisher");
    ShmTransmitter<LoanedMessage> transmitter(
        MakeRoleAttributes(channel_name, "_publisher"));
    transmitter.Enable();

    const std::string payload = "loaned-multiprocess";
    auto first = transmitter.AcquireLoanedMessage(payload.size());
    ASSERT_NE(nullptr, first);
    const uint32_t held_block_index = first->block_index();
    std::memcpy(first->mutable_data(), payload.data(), payload.size());
    ASSERT_TRUE(first->set_size(payload.size()));
    ASSERT_TRUE(transmitter.TransmitLoanedMessage(std::move(first)));

    ASSERT_TRUE(ReadByteWithTimeout(child_to_parent[0], &signal, 5000));
    ASSERT_EQ('R', signal);

    const uint32_t block_num = static_cast<uint32_t>(ShmConf().block_num());
    std::vector<std::unique_ptr<LoanedMessage>> busy_messages;
    busy_messages.reserve(block_num - 1);
    bool did_not_reuse_held_block = true;
    for(uint32_t i = 0; i < block_num - 1; ++i) {
        auto busy = transmitter.AcquireLoanedMessage(1);
        if(busy == nullptr) {
            did_not_reuse_held_block = false;
            break;
        }
        did_not_reuse_held_block = did_not_reuse_held_block &&
                                  busy->block_index() != held_block_index;
        busy_messages.emplace_back(std::move(busy));
    }
    EXPECT_TRUE(did_not_reuse_held_block);
    EXPECT_EQ(nullptr, transmitter.AcquireLoanedMessage(1));
    busy_messages.clear();

    EXPECT_TRUE(WriteByte(parent_to_child[1], 'X'));
    ASSERT_TRUE(ReadByteWithTimeout(child_to_parent[0], &signal, 5000));
    EXPECT_EQ('D', signal);

    bool held_block_recovered = false;
    for(uint32_t i = 0; i < block_num; ++i) {
        auto candidate = transmitter.AcquireLoanedMessage(1);
        ASSERT_NE(nullptr, candidate);
        held_block_recovered = held_block_recovered ||
                               candidate->block_index() == held_block_index;
    }
    EXPECT_TRUE(held_block_recovered);

    transmitter.Disable();
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    pid_t finished = 0;
    while(std::chrono::steady_clock::now() < deadline) {
        finished = waitpid(child, &status, WNOHANG);
        if(finished != 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(finished == 0) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        FAIL() << "subscriber child exceeded the watchdog deadline";
    }
    ASSERT_EQ(child, finished);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
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
