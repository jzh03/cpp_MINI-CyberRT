#include "transmitter_lifecycle_test_util.h"

#include <cmw/init.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/receiver/rtps_receiver.h>
#include <cmw/transport/transport.h>
#include <cmw/transport/transmitter/rtps_transmitter.h>
#include <cmw/transport/transmitter/hybrid_transmitter.h>
#include <cmw/transport/receiver/intra_receiver.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {
using namespace lifecycle_test;

TEST(RtpsTransmitterLifecycle, ForcedSameHostConcurrentLoanAndEnableDisable) {
  auto attr = Attributes("rtps_concurrent");
  auto participant = std::make_shared<Participant>(attr.channel_name, 0, nullptr);
  {
    RtpsTransmitter<LoanedMessage> transmitter(attr, participant);
    Received received(true);
    RtpsReceiver<LoanedMessage> receiver(attr,
        std::bind(&Received::OnMessage, &received, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
    receiver.Enable();
    ExerciseHeapLifecycle(&transmitter, &received);
    receiver.Disable();
  }
  participant->Shutdown();
}

TEST(RtpsTransmitterLifecycle, ControlledHybridSnapshotCanLoseRtpsRoute) {
  auto attr = Attributes("hybrid_rtps_snapshot");
  auto participant = std::make_shared<Participant>(attr.channel_name, 0, nullptr);
  {
    HybridTransmitter<LoanedMessage> transmitter(attr, participant);
    auto intra_peer = attr;
    ++intra_peer.id;
    auto rtps_peer = attr;
    rtps_peer.id += 2;
    rtps_peer.host_ip = "198.51.100.77";
    bool leave = true;
    unsigned count = 0;
    IntraReceiver<LoanedMessage> intra(intra_peer,
        [&](const std::shared_ptr<LoanedMessage>& message, const MessageInfo&,
            const RoleAttributes&) {
          EXPECT_EQ(64U, message->size());
          ++count;
          if(leave) { transmitter.Disable(rtps_peer); }
        });
    Received received(true);
    RtpsReceiver<LoanedMessage> rtps(attr,
        std::bind(&Received::OnMessage, &received, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
    intra.Enable();
    rtps.Enable();
    transmitter.Enable(intra_peer);
    transmitter.Enable(rtps_peer);
    // The INTRA callback removes the last RTPS peer after the route snapshot
    // and before its DDS use. INTRA succeeds, the aggregate must fail.
    EXPECT_FALSE(Send(&transmitter, 1));
    EXPECT_EQ(1U, count);
    leave = false;
    transmitter.Enable(rtps_peer);
    bool recovered = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for(uint64_t sequence = 2; !recovered && std::chrono::steady_clock::now() < deadline;
        ++sequence) {
      recovered = Send(&transmitter, sequence) && received.Wait(sequence);
    }
    EXPECT_TRUE(recovered);
    transmitter.Disable();
    rtps.Disable();
    intra.Disable();
  }
  participant->Shutdown();
}

// Serialization is user code before resource admission. A deterministic
// barrier closes the writer after serialization starts, before DDS use.
struct BlockingPayload : serialize::Serializable {
  std::string payload = "ordinary";
  static std::mutex mutex;
  static std::condition_variable condition;
  static bool entered;
  static bool proceed;
  void serialize(serialize::DataStream& stream) const override {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    condition.notify_all();
    EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), []() {
      return proceed;
    }));
    stream << payload;
  }
  bool unserialize(serialize::DataStream& stream) override {
    return stream.read_args(payload);
  }
};
std::mutex BlockingPayload::mutex;
std::condition_variable BlockingPayload::condition;
bool BlockingPayload::entered = false;
bool BlockingPayload::proceed = false;

TEST(RtpsTransmitterLifecycle, OrdinarySendRechecksAfterSerialization) {
  auto attr = Attributes("rtps_ordinary");
  auto participant = std::make_shared<Participant>(attr.channel_name, 0, nullptr);
  {
    RtpsTransmitter<BlockingPayload> transmitter(attr, participant);
    transmitter.Enable();
    auto message = std::make_shared<BlockingPayload>();
    std::thread sender([&]() {
      EXPECT_FALSE(transmitter.Transmit(message, MessageInfo()));
    });
    {
      std::unique_lock<std::mutex> lock(BlockingPayload::mutex);
      EXPECT_TRUE(BlockingPayload::condition.wait_for(lock, std::chrono::seconds(5), []() {
        return BlockingPayload::entered;
      }));
    }
    transmitter.Disable();
    {
      std::lock_guard<std::mutex> lock(BlockingPayload::mutex);
      BlockingPayload::proceed = true;
      BlockingPayload::condition.notify_all();
    }
    sender.join();
    transmitter.Enable();
    EXPECT_TRUE(transmitter.Transmit(message, MessageInfo()));
    transmitter.Disable();
  }
  participant->Shutdown();
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  hnu::cmw::Init("RtpsTransmitterLifecycle");
  hnu::cmw::transport::Transport::Instance();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
