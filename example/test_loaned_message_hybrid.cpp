// Build: make test_loaned_message_hybrid

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/receiver/intra_receiver.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/transmitter/hybrid_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

std::string UniqueChannelName() {
  return "loaned_hybrid_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

RoleAttributes MakeRoleAttributes(const std::string& channel,
                                  const std::string& suffix) {
  auto global_data = common::GlobalData::Instance();
  RoleAttributes attr{};
  attr.channel_name = channel;
  attr.channel_id = common::GlobalData::RegisterChannel(channel);
  attr.host_name = global_data->HostName();
  attr.host_ip = global_data->HostIp();
  attr.process_id = global_data->ProcessId();
  attr.node_name = channel + suffix;
  attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
  attr.id = common::GlobalData::GenerateHashId(attr.node_name);
  attr.message_type = "LoanedMessage";
  attr.qos_profile.msg_size = 256;
  return attr;
}

bool Fill(const std::unique_ptr<LoanedMessage>& message,
          const std::string& payload) {
  if(message == nullptr || message->capacity() < payload.size() ||
     (payload.size() != 0 && message->mutable_data() == nullptr)) {
    return false;
  }
  if(!payload.empty()) {
    std::memcpy(message->mutable_data(), payload.data(), payload.size());
  }
  return message->set_size(payload.size());
}

TEST(LoanedMessageHybridTest, RoutesHeapAndShmStorageAcrossTopologyChanges) {
  const std::string channel = UniqueChannelName();
  const RoleAttributes publisher_attr = MakeRoleAttributes(channel, "_publisher");
  const RoleAttributes receiver_attr = MakeRoleAttributes(channel, "_receiver");
  RoleAttributes intra_peer = MakeRoleAttributes(channel, "_intra_peer");
  RoleAttributes shm_peer = MakeRoleAttributes(channel, "_shm_peer");
  RoleAttributes rtps_peer = MakeRoleAttributes(channel, "_rtps_peer");
  ++shm_peer.process_id;
  rtps_peer.host_ip = "198.51.100.77";

  std::mutex mutex;
  std::condition_variable condition;
  uint32_t intra_count = 0;
  uint32_t shm_count = 0;
  const uint8_t* expected_intra_address = nullptr;
  std::string intra_payload;
  std::string shm_payload;
  bool intra_shared_address = false;

  IntraReceiver<LoanedMessage> intra_receiver(
      receiver_attr,
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo&,
          const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++intra_count;
        intra_payload.assign(reinterpret_cast<const char*>(message->data()),
                             message->size());
        intra_shared_address = message->data() == expected_intra_address;
        EXPECT_TRUE(message->is_heap_backed());
        EXPECT_TRUE(message->is_read_only());
        EXPECT_EQ(nullptr, message->mutable_data());
        condition.notify_all();
      });
  ShmReceiver<LoanedMessage> shm_receiver(
      receiver_attr,
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo&,
          const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++shm_count;
        shm_payload.assign(reinterpret_cast<const char*>(message->data()),
                           message->size());
        EXPECT_TRUE(message->is_shm_backed());
        EXPECT_TRUE(message->is_read_only());
        condition.notify_all();
      });
  intra_receiver.Enable();
  shm_receiver.Enable();

  HybridTransmitter<LoanedMessage> transmitter(publisher_attr, nullptr);
  EXPECT_EQ(nullptr, transmitter.AcquireLoanedMessage(16));

  transmitter.Enable(shm_peer);
  auto shm_only = transmitter.AcquireLoanedMessage(32);
  ASSERT_NE(nullptr, shm_only);
  EXPECT_TRUE(shm_only->is_shm_backed());

  // Acquire sees SHM only; Publish sees a newly active INTRA peer. The
  // receiver gets the heap snapshot while SHM still receives the original block.
  ASSERT_TRUE(Fill(shm_only, "shm-to-hybrid"));
  transmitter.Enable(intra_peer);
  ASSERT_TRUE(transmitter.TransmitLoanedMessage(std::move(shm_only)));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return intra_count == 1 && shm_count == 1;
    }));
    EXPECT_EQ("shm-to-hybrid", intra_payload);
    EXPECT_EQ("shm-to-hybrid", shm_payload);
  }

  auto heap_mixed = transmitter.AcquireLoanedMessage(32);
  ASSERT_NE(nullptr, heap_mixed);
  EXPECT_TRUE(heap_mixed->is_heap_backed());
  expected_intra_address = heap_mixed->data();
  ASSERT_TRUE(Fill(heap_mixed, "heap-shared-intra"));
  ASSERT_TRUE(transmitter.TransmitLoanedMessage(std::move(heap_mixed)));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return intra_count == 2 && shm_count == 2;
    }));
    EXPECT_TRUE(intra_shared_address);
    EXPECT_EQ("heap-shared-intra", intra_payload);
    EXPECT_EQ("heap-shared-intra", shm_payload);
  }

  // Any RTPS route selects heap storage, even before a network send is made.
  transmitter.Enable(rtps_peer);
  auto with_rtps = transmitter.AcquireLoanedMessage(16);
  ASSERT_NE(nullptr, with_rtps);
  EXPECT_TRUE(with_rtps->is_heap_backed());
  transmitter.Disable(rtps_peer);

  // A heap loan can safely fall back to SHM if INTRA leaves before Publish.
  auto route_changed = transmitter.AcquireLoanedMessage(32);
  ASSERT_NE(nullptr, route_changed);
  EXPECT_TRUE(route_changed->is_heap_backed());
  ASSERT_TRUE(Fill(route_changed, "heap-to-shm"));
  transmitter.Disable(intra_peer);
  // The SHM receiver owns the read lease until its callback returns. Let the
  // preceding callback complete before requesting the next bounded block.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  ASSERT_TRUE(transmitter.TransmitLoanedMessage(std::move(route_changed)));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return shm_count == 3;
    }));
    EXPECT_EQ("heap-to-shm", shm_payload);
    EXPECT_EQ(2U, intra_count);
  }

  transmitter.Enable(intra_peer);
  auto unset_size = transmitter.AcquireLoanedMessage(16);
  ASSERT_NE(nullptr, unset_size);
  EXPECT_FALSE(transmitter.TransmitLoanedMessage(std::move(unset_size)));
  transmitter.Disable(intra_peer);
  transmitter.Disable(shm_peer);

  auto no_route = LoanedMessage::CreateHeap(publisher_attr.channel_id, 8);
  ASSERT_TRUE(Fill(no_route, "none"));
  EXPECT_FALSE(transmitter.TransmitLoanedMessage(std::move(no_route)));

  shm_receiver.Disable();
  intra_receiver.Disable();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("LoanedMessageHybridTest");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
