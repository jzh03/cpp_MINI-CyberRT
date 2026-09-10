#include "transmitter_lifecycle_test_util.h"

#include <cmw/init.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/receiver/intra_receiver.h>
#include <cmw/transport/transmitter/hybrid_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {
using namespace lifecycle_test;

TEST(IntraTransmitterLifecycle, ConcurrentLoanAndEnableDisable) {
  auto attr = Attributes("intra_lifecycle");
  IntraTransmitter<LoanedMessage> transmitter(attr);
  Received received;
  IntraReceiver<LoanedMessage> receiver(attr,
      std::bind(&Received::OnMessage, &received, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
  receiver.Enable();
  ExerciseHeapLifecycle(&transmitter, &received);
  receiver.Disable();
}

struct Payload : serialize::Serializable {
  uint64_t sequence = 0;
  SERIALIZE(sequence)
};

TEST(IntraTransmitterLifecycle, HybridOrdinaryReentryPreservesOuterMetadata) {
  auto attr = Attributes("intra_reentry");
  HybridTransmitter<Payload> hybrid(attr, nullptr);
  Transmitter<Payload>& transmitter = hybrid;
  auto peer = attr;
  ++peer.id;
  hybrid.Enable(peer);
  unsigned count = 0;
  // The per-writer listener exercises ListenerHandler's signal map lock too.
  IntraReceiver<Payload> receiver(peer,
      [&](const std::shared_ptr<Payload>& message, const MessageInfo& info,
          const RoleAttributes&) {
        ++count;
        const auto sequence = info.seq_num();
        EXPECT_EQ(message->sequence, sequence);
        if(sequence == 1) {
          hybrid.Disable(peer);
          hybrid.Enable(peer);
          auto nested = std::make_shared<Payload>();
          nested->sequence = 2;
          EXPECT_TRUE(transmitter.Transmit(nested));
          EXPECT_EQ(1U, info.seq_num());
          auto writer = transmitter.attributes();
          writer.id = transmitter.id().HashValue();
          receiver.Disable(writer);
        }
      });
  auto writer = transmitter.attributes();
  writer.id = transmitter.id().HashValue();
  receiver.Enable(writer);
  auto message = std::make_shared<Payload>();
  message->sequence = 1;
  EXPECT_TRUE(transmitter.Transmit(message));
  EXPECT_EQ(2U, count);
  receiver.Disable(writer);
  hybrid.Disable();
  EXPECT_TRUE(transmitter.Transmit(message));  // Established no-peer contract.
  IntraTransmitter<Payload> direct(attr);
  EXPECT_FALSE(direct.Transmit(message, MessageInfo()));
  direct.Enable();
  EXPECT_TRUE(direct.Transmit(message, MessageInfo()));
  direct.Disable();
  EXPECT_FALSE(direct.Transmit(message, MessageInfo()));
}

TEST(IntraTransmitterLifecycle, LoanCallbackReentersPublishAndDisable) {
  auto attr = Attributes("intra_loan_reentry");
  HybridTransmitter<LoanedMessage> transmitter(attr, nullptr);
  auto peer = attr;
  ++peer.id;
  transmitter.Enable(peer);
  unsigned count = 0;
  IntraReceiver<LoanedMessage> receiver(peer,
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo& info,
          const RoleAttributes&) {
        ++count;
        uint64_t sequence = 0;
        ASSERT_EQ(64U, message->size());
        std::memcpy(&sequence, message->data(), sizeof(sequence));
        EXPECT_EQ(sequence, info.seq_num());
        if(sequence == 1) {
          transmitter.Disable(peer);
          EXPECT_EQ(nullptr, transmitter.AcquireLoanedMessage(64));
          transmitter.Enable(peer);
          auto nested = transmitter.AcquireLoanedMessage(64);
          ASSERT_TRUE(Fill(nested.get(), 2));
          EXPECT_TRUE(transmitter.TransmitLoanedMessage(std::move(nested)));
          EXPECT_EQ(1U, info.seq_num());
        }
      });
  receiver.Enable();
  auto loan = transmitter.AcquireLoanedMessage(64);
  ASSERT_TRUE(Fill(loan.get(), 1));
  EXPECT_TRUE(transmitter.TransmitLoanedMessage(std::move(loan)));
  EXPECT_EQ(2U, count);
  transmitter.Disable();
  receiver.Disable();
}

TEST(IntraTransmitterLifecycle, DisableDoesNotWaitForAdmittedCallback) {
  auto attr = Attributes("intra_admitted");
  IntraTransmitter<LoanedMessage> transmitter(attr);
  transmitter.Enable();
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false;
  bool closed = false;
  IntraReceiver<LoanedMessage> receiver(attr,
      [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo&,
          const RoleAttributes&) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
          return closed;
        }));
        EXPECT_EQ(64U, message->size());
        EXPECT_EQ(nullptr, transmitter.AcquireLoanedMessage(64));
        transmitter.Disable();  // Callback must not wait for itself.
      });
  receiver.Enable();
  std::thread sender([&]() { EXPECT_TRUE(Send(&transmitter, 1)); });
  {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
      return entered;
    }));
  }
  transmitter.Disable();
  {
    std::lock_guard<std::mutex> lock(mutex);
    closed = true;
    condition.notify_all();
  }
  sender.join();
  receiver.Disable();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("IntraTransmitterLifecycle");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
