#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/transmitter/hybrid_transmitter.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

struct SerializedPayload : public serialize::Serializable {
  std::string payload;

  SERIALIZE(payload)
};

RoleAttributes MakeRoleAttributes(const std::string& channel,
                                  const std::string& suffix,
                                  uint32_t msg_size = 128) {
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
  attr.message_type = "ShmTransmitterLifecycleRegression";
  attr.qos_profile.msg_size = msg_size;
  return attr;
}

std::string UniqueChannel(const std::string& prefix) {
  return prefix + "_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

bool Fill(const std::unique_ptr<LoanedMessage>& message,
          const std::string& payload) {
  if(message == nullptr || message->mutable_data() == nullptr ||
     payload.size() > message->capacity()) {
    return false;
  }
  std::memcpy(message->mutable_data(), payload.data(), payload.size());
  return message->set_size(payload.size());
}

bool WaitForCount(std::mutex* mutex, std::condition_variable* condition,
                  const std::vector<std::pair<std::string, uint64_t>>* received,
                  std::size_t count) {
  std::unique_lock<std::mutex> lock(*mutex);
  return condition->wait_for(lock, std::chrono::seconds(5), [&]() {
    return received->size() >= count;
  });
}

TEST(ShmTransmitterLifecycleRegression,
     UnalignedMetadataRoundTripsForSerializedAndLoanedPaths) {
  const std::vector<std::size_t> payload_sizes = {1, 4, 7, 8, 9};

  const std::string serialized_channel = UniqueChannel("shm_unaligned_serialized");
  const RoleAttributes serialized_receiver_attr =
      MakeRoleAttributes(serialized_channel, "_receiver");
  const RoleAttributes serialized_transmitter_attr =
      MakeRoleAttributes(serialized_channel, "_transmitter");
  std::mutex serialized_mutex;
  std::condition_variable serialized_condition;
  std::vector<std::pair<std::string, uint64_t>> serialized_received;
  ShmReceiver<SerializedPayload> serialized_receiver(
      serialized_receiver_attr,
      [&](const std::shared_ptr<SerializedPayload>& message,
          const MessageInfo& info, const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(serialized_mutex);
        serialized_received.emplace_back(message->payload, info.seq_num());
        serialized_condition.notify_all();
      });
  serialized_receiver.Enable();
  ShmTransmitter<SerializedPayload> serialized_transmitter(
      serialized_transmitter_attr);
  serialized_transmitter.Enable();

  bool saw_unaligned_serialized_metadata = false;
  for(std::size_t size : payload_sizes) {
    SerializedPayload message;
    message.payload.assign(size, static_cast<char>('a' + size));
    serialize::DataStream stream;
    stream << message;
    saw_unaligned_serialized_metadata =
        saw_unaligned_serialized_metadata ||
        stream.ByteSize() % alignof(uint64_t) != 0;
    MessageInfo info;
    info.set_seq_num(100 + size);
    ASSERT_TRUE(serialized_transmitter.Transmit(
        std::make_shared<SerializedPayload>(message), info));
  }
  EXPECT_TRUE(saw_unaligned_serialized_metadata);
  ASSERT_TRUE(WaitForCount(&serialized_mutex, &serialized_condition,
                           &serialized_received, payload_sizes.size()));
  {
    std::lock_guard<std::mutex> lock(serialized_mutex);
    ASSERT_EQ(payload_sizes.size(), serialized_received.size());
    for(std::size_t index = 0; index < payload_sizes.size(); ++index) {
      EXPECT_EQ(payload_sizes[index], serialized_received[index].first.size());
      EXPECT_EQ(100 + payload_sizes[index], serialized_received[index].second);
    }
  }

  const std::string loaned_channel = UniqueChannel("shm_unaligned_loaned");
  const RoleAttributes loaned_receiver_attr =
      MakeRoleAttributes(loaned_channel, "_receiver");
  const RoleAttributes loaned_transmitter_attr =
      MakeRoleAttributes(loaned_channel, "_transmitter");
  std::mutex loaned_mutex;
  std::condition_variable loaned_condition;
  std::vector<std::pair<std::string, uint64_t>> loaned_received;
  ShmReceiver<LoanedMessage> loaned_receiver(
      loaned_receiver_attr,
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo& info,
          const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(loaned_mutex);
        loaned_received.emplace_back(
            std::string(reinterpret_cast<const char*>(message->data()),
                        message->size()),
            info.seq_num());
        loaned_condition.notify_all();
      });
  loaned_receiver.Enable();
  ShmTransmitter<LoanedMessage> loaned_transmitter(loaned_transmitter_attr);
  loaned_transmitter.Enable();
  for(std::size_t size : payload_sizes) {
    const std::string payload(size, static_cast<char>('k' + size));
    auto message = loaned_transmitter.AcquireLoanedMessage(size);
    ASSERT_TRUE(Fill(message, payload));
    MessageInfo info;
    info.set_seq_num(200 + size);
    ASSERT_TRUE(loaned_transmitter.TransmitLoanedMessage(std::move(message), info));
  }
  ASSERT_TRUE(WaitForCount(&loaned_mutex, &loaned_condition, &loaned_received,
                           payload_sizes.size()));

  RoleAttributes shm_peer = MakeRoleAttributes(loaned_channel, "_shm_peer");
  ++shm_peer.process_id;
  RoleAttributes intra_peer = MakeRoleAttributes(loaned_channel, "_intra_peer");
  HybridTransmitter<LoanedMessage> hybrid(loaned_transmitter_attr, nullptr);
  hybrid.Enable(shm_peer);
  hybrid.Enable(intra_peer);
  auto heap_message = hybrid.AcquireLoanedMessage(7);
  ASSERT_NE(nullptr, heap_message);
  ASSERT_TRUE(heap_message->is_heap_backed());
  ASSERT_TRUE(Fill(heap_message, "heap-07"));
  MessageInfo heap_info;
  heap_info.set_seq_num(307);
  ASSERT_TRUE(hybrid.TransmitLoanedMessage(std::move(heap_message), heap_info));
  ASSERT_TRUE(WaitForCount(&loaned_mutex, &loaned_condition, &loaned_received,
                           payload_sizes.size() + 1));
  {
    std::lock_guard<std::mutex> lock(loaned_mutex);
    ASSERT_EQ(payload_sizes.size() + 1, loaned_received.size());
    EXPECT_EQ("heap-07", loaned_received.back().first);
    EXPECT_EQ(307U, loaned_received.back().second);
  }

  hybrid.Disable();
  loaned_transmitter.Disable();
  loaned_receiver.Disable();
  serialized_transmitter.Disable();
  serialized_receiver.Disable();
}

TEST(ShmTransmitterLifecycleRegression,
     RejectsOldShmLoansAndRecoversAfterEnable) {
  const std::string channel = UniqueChannel("shm_loan_epoch");
  ShmTransmitter<LoanedMessage> transmitter(
      MakeRoleAttributes(channel, "_transmitter"));
  transmitter.Enable();

  auto disabled_loan = transmitter.AcquireLoanedMessage(8);
  ASSERT_TRUE(Fill(disabled_loan, "old-off"));
  transmitter.Disable();
  MessageInfo old_info;
  old_info.set_seq_num(1);
  EXPECT_FALSE(transmitter.TransmitLoanedMessage(std::move(disabled_loan),
                                                 old_info));
  EXPECT_EQ(nullptr, transmitter.AcquireLoanedMessage(8));

  transmitter.Enable();
  auto restarted_loan = transmitter.AcquireLoanedMessage(8);
  ASSERT_TRUE(Fill(restarted_loan, "old-on"));
  transmitter.Disable();
  transmitter.Enable();
  EXPECT_FALSE(transmitter.TransmitLoanedMessage(std::move(restarted_loan),
                                                 old_info));

  auto current_loan = transmitter.AcquireLoanedMessage(8);
  ASSERT_TRUE(Fill(current_loan, "new-on"));
  MessageInfo current_info;
  current_info.set_seq_num(2);
  EXPECT_TRUE(transmitter.TransmitLoanedMessage(std::move(current_loan),
                                                current_info));
  transmitter.Disable();
}

TEST(ShmTransmitterLifecycleRegression, SendAndEnableDisableDoNotRace) {
  const std::string channel = UniqueChannel("shm_send_disable");
  std::mutex received_mutex;
  std::condition_variable received_condition;
  std::vector<std::pair<std::string, uint64_t>> received;
  ShmReceiver<LoanedMessage> receiver(
      MakeRoleAttributes(channel, "_receiver"),
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo& info,
          const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received.emplace_back(
            std::string(reinterpret_cast<const char*>(message->data()),
                        message->size()),
            info.seq_num());
        received_condition.notify_all();
      });
  receiver.Enable();
  ShmTransmitter<LoanedMessage> transmitter(
      MakeRoleAttributes(channel, "_transmitter"));
  transmitter.Enable();

  std::mutex start_mutex;
  std::condition_variable start_condition;
  bool start = false;
  std::atomic<uint32_t> successful_sends(0);
  std::thread sender([&]() {
    {
      std::unique_lock<std::mutex> lock(start_mutex);
      start_condition.wait(lock, [&]() { return start; });
    }
    for(uint32_t sequence = 0; sequence < 40; ++sequence) {
      auto message = transmitter.AcquireLoanedMessage(1);
      if(message == nullptr) {
        continue;
      }
      message->mutable_data()[0] = static_cast<uint8_t>(sequence);
      if(!message->set_size(1)) {
        continue;
      }
      MessageInfo info;
      info.set_seq_num(sequence);
      if(transmitter.TransmitLoanedMessage(std::move(message), info)) {
        ++successful_sends;
      }
    }
  });
  std::thread toggler([&]() {
    {
      std::unique_lock<std::mutex> lock(start_mutex);
      start_condition.wait(lock, [&]() { return start; });
    }
    for(uint32_t iteration = 0; iteration < 20; ++iteration) {
      transmitter.Disable();
      transmitter.Enable();
    }
  });
  {
    std::lock_guard<std::mutex> lock(start_mutex);
    start = true;
  }
  start_condition.notify_all();
  sender.join();
  toggler.join();

  transmitter.Enable();
  auto final_message = transmitter.AcquireLoanedMessage(1);
  ASSERT_NE(nullptr, final_message);
  ASSERT_NE(nullptr, final_message->mutable_data());
  final_message->mutable_data()[0] = 'z';
  EXPECT_TRUE(final_message->set_size(1));
  MessageInfo final_info;
  final_info.set_seq_num(999);
  EXPECT_TRUE(transmitter.TransmitLoanedMessage(std::move(final_message), final_info));
  {
    std::unique_lock<std::mutex> lock(received_mutex);
    EXPECT_TRUE(received_condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return std::find(received.begin(), received.end(),
                       std::make_pair(std::string("z"), uint64_t(999))) !=
             received.end();
    }));
  }
  EXPECT_LE(successful_sends.load(), 40U);
  transmitter.Disable();
  receiver.Disable();
}

TEST(ShmTransmitterLifecycleRegression, HeapLoanSendsAcrossShmLifecycleRace) {
  const std::string channel = UniqueChannel("shm_heap_send_disable");
  std::mutex received_mutex;
  std::condition_variable received_condition;
  std::vector<std::pair<std::string, uint64_t>> received;
  ShmReceiver<LoanedMessage> receiver(
      MakeRoleAttributes(channel, "_receiver"),
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo& info,
          const RoleAttributes&) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received.emplace_back(
            std::string(reinterpret_cast<const char*>(message->data()),
                        message->size()),
            info.seq_num());
        received_condition.notify_all();
      });
  receiver.Enable();
  const RoleAttributes publisher_attr = MakeRoleAttributes(channel, "_publisher");
  RoleAttributes shm_peer = MakeRoleAttributes(channel, "_shm_peer");
  ++shm_peer.process_id;
  const RoleAttributes intra_peer = MakeRoleAttributes(channel, "_intra_peer");
  HybridTransmitter<LoanedMessage> transmitter(publisher_attr, nullptr);
  transmitter.Enable(shm_peer);
  transmitter.Enable(intra_peer);

  std::mutex start_mutex;
  std::condition_variable start_condition;
  bool start = false;
  std::thread sender([&]() {
    {
      std::unique_lock<std::mutex> lock(start_mutex);
      start_condition.wait(lock, [&]() { return start; });
    }
    for(uint32_t sequence = 0; sequence < 30; ++sequence) {
      auto message = transmitter.AcquireLoanedMessage(1);
      if(message == nullptr || !message->is_heap_backed()) {
        continue;
      }
      message->mutable_data()[0] = static_cast<uint8_t>(sequence);
      if(!message->set_size(1)) {
        continue;
      }
      MessageInfo info;
      info.set_seq_num(sequence);
      transmitter.TransmitLoanedMessage(std::move(message), info);
    }
  });
  std::thread toggler([&]() {
    {
      std::unique_lock<std::mutex> lock(start_mutex);
      start_condition.wait(lock, [&]() { return start; });
    }
    for(uint32_t iteration = 0; iteration < 15; ++iteration) {
      transmitter.Disable(shm_peer);
      transmitter.Enable(shm_peer);
    }
  });
  {
    std::lock_guard<std::mutex> lock(start_mutex);
    start = true;
  }
  start_condition.notify_all();
  sender.join();
  toggler.join();

  auto final_message = transmitter.AcquireLoanedMessage(4);
  ASSERT_NE(nullptr, final_message);
  ASSERT_TRUE(final_message->is_heap_backed());
  ASSERT_TRUE(Fill(final_message, "heap"));
  MessageInfo final_info;
  final_info.set_seq_num(1000);
  ASSERT_TRUE(transmitter.TransmitLoanedMessage(std::move(final_message), final_info));
  {
    std::unique_lock<std::mutex> lock(received_mutex);
    EXPECT_TRUE(received_condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return std::find(received.begin(), received.end(),
                       std::make_pair(std::string("heap"), uint64_t(1000))) !=
             received.end();
    }));
  }
  transmitter.Disable();
  receiver.Disable();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("ShmTransmitterLifecycleRegression");
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  auto dispatcher = hnu::cmw::transport::ShmDispatcher::Instance(false);
  if(dispatcher != nullptr) {
    dispatcher->Shutdown();
  }
  return result;
}
