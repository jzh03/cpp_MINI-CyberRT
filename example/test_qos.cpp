#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

#include <cmw/init.h>
#include <cmw/node/node.h>
#include <cmw/config/unit_test.h>
#include <cmw/transport/qos/qos_profile_conf.h>
#include <cmw/data/data_visitor.h>
#include <cmw/data/data_dispatcher.h>

namespace {
using namespace hnu::cmw;
using namespace hnu::cmw::config;
using namespace hnu::cmw::transport;

TEST(Qos, BusinessDefaultsAreVolatileAndDiscoveryRetainsAnnouncements) {
  QosProfile defaults;
  SubscriberConfig subscriber;
  EXPECT_EQ(defaults.depth, 1u);
  EXPECT_EQ(defaults.mps, 0u);
  EXPECT_EQ(defaults.reliability, RELIABILITY_RELIABLE);
  EXPECT_EQ(defaults.durability, DURABILITY_VOLATILE);
  EXPECT_EQ(subscriber.pending_queue_size, 1u);
  EXPECT_EQ(subscriber.history_depth, 1u);
  EXPECT_TRUE(SameQosProfile(defaults, subscriber.qos_profile));
  EXPECT_TRUE(SameQosProfile(defaults, QosProfileConf::QOS_PROFILE_DEFAULT));
  const auto& topology = QosProfileConf::QOS_PROFILE_TOPO_CHANGE;
  EXPECT_EQ(topology.history, HISTORY_KEEP_ALL);
  EXPECT_EQ(topology.reliability, RELIABILITY_RELIABLE);
  EXPECT_EQ(topology.durability, DURABILITY_TRANSIENT_LOCAL);
  QosProfile normalized;
  EXPECT_TRUE(NormalizeQosProfile(topology, &normalized));
}

TEST(Qos, SystemDefaultAndZeroDepthResolveWithoutLibraryDefaults) {
  QosProfile qos, normalized;
  qos.history = HISTORY_SYSTEM_DEFAULT;
  qos.depth = 0;
  qos.reliability = RELIABILITY_SYSTEM_DEFAULT;
  qos.durability = DURABILITY_SYSTEM_DEFAULT;
  ASSERT_TRUE(NormalizeQosProfile(qos, &normalized));
  EXPECT_TRUE(SameQosProfile(normalized, QosProfile()));
  EXPECT_EQ(normalized.depth, 1u);
  EXPECT_EQ(normalized.durability, DURABILITY_VOLATILE);
}

TEST(Qos, RejectsInvalidEnumsAndResourceCountsWithoutChangingOutput) {
  auto rejected = [](QosProfile bad) {
    QosProfile output;
    output.depth = 17;
    std::string reason;
    EXPECT_FALSE(NormalizeQosProfile(bad, &output, &reason));
    EXPECT_FALSE(reason.empty());
    EXPECT_EQ(output.depth, 17u);
    RtpsWriterAttributes writer;
    RtpsReaderAttributes reader;
    EXPECT_FALSE(AttributesFiller::FillInWriterAttr("invalid", bad, &writer));
    EXPECT_FALSE(AttributesFiller::FillInReaderAttr("invalid", bad, &reader));
  };
  QosProfile qos;
  qos.history = static_cast<QosHistoryPolicy>(99); rejected(qos);
  qos = QosProfile(); qos.reliability = static_cast<QosReliabilityPolicy>(99); rejected(qos);
  qos = QosProfile(); qos.durability = static_cast<QosDurabilityPolicy>(99); rejected(qos);
  qos = QosProfile(); qos.depth = std::numeric_limits<uint32_t>::max(); rejected(qos);
  qos = QosProfile(); qos.max_samples = 0; rejected(qos);
  qos = QosProfile(); qos.max_samples = std::numeric_limits<uint32_t>::max(); rejected(qos);
  qos = QosProfile(); qos.depth = qos.max_samples + 1; rejected(qos);
  qos = QosProfile(); qos.msg_size = std::numeric_limits<uint32_t>::max(); rejected(qos);
  EXPECT_FALSE(NormalizeQosProfile(QosProfile(), nullptr));
  EXPECT_FALSE(AttributesFiller::FillInWriterAttr("qos", QosProfile(), nullptr));
  EXPECT_FALSE(AttributesFiller::FillInReaderAttr("qos", QosProfile(), nullptr));
}

TEST(Qos, EndpointAndAdvertisementAgreeForEverySupportedPolicy) {
  for (auto reliability : {RELIABILITY_RELIABLE, RELIABILITY_BEST_EFFORT}) {
    for (auto durability : {DURABILITY_TRANSIENT_LOCAL, DURABILITY_VOLATILE}) {
      for (auto history : {HISTORY_KEEP_LAST, HISTORY_KEEP_ALL}) {
        QosProfile qos;
        qos.reliability = reliability;
        qos.durability = durability;
        qos.history = history;
        qos.depth = 5;
        qos.max_samples = 13;
        RtpsWriterAttributes writer;
        RtpsReaderAttributes reader;
        ASSERT_TRUE(AttributesFiller::FillInWriterAttr("qos", qos, &writer));
        ASSERT_TRUE(AttributesFiller::FillInReaderAttr("qos", qos, &reader));
        auto endpoint_rel = reliability == RELIABILITY_RELIABLE ? RELIABLE : BEST_EFFORT;
        auto advertised_rel = reliability == RELIABILITY_RELIABLE
            ? RELIABLE_RELIABILITY_QOS : BEST_EFFORT_RELIABILITY_QOS;
        auto endpoint_dur = durability == DURABILITY_TRANSIENT_LOCAL ? TRANSIENT_LOCAL : VOLATILE;
        auto advertised_dur = durability == DURABILITY_TRANSIENT_LOCAL
            ? TRANSIENT_LOCAL_DURABILITY_QOS : VOLATILE_DURABILITY_QOS;
        EXPECT_EQ(writer.watt.endpoint.reliabilityKind, endpoint_rel);
        EXPECT_EQ(reader.ratt.endpoint.reliabilityKind, endpoint_rel);
        EXPECT_EQ(writer.Wqos.m_reliability.kind, advertised_rel);
        EXPECT_EQ(reader.Rqos.m_reliability.kind, advertised_rel);
        EXPECT_EQ(writer.watt.endpoint.durabilityKind, endpoint_dur);
        EXPECT_EQ(reader.ratt.endpoint.durabilityKind, endpoint_dur);
        EXPECT_EQ(writer.Wqos.m_durability.kind, advertised_dur);
        EXPECT_EQ(reader.Rqos.m_durability.kind, advertised_dur);
        int expected = history == HISTORY_KEEP_LAST ? 5 : 13;
        EXPECT_EQ(writer.hatt.maximumReservedCaches, expected);
        EXPECT_EQ(reader.hatt.maximumReservedCaches, expected);
        EXPECT_LE(writer.hatt.initialReservedCaches, expected);
      }
    }
  }
}

TEST(Qos, HeartbeatUsesNanosecondsAndMpsIsOnlyAHint) {
  QosProfile qos;
  RtpsWriterAttributes writer;
  qos.mps = 512;
  ASSERT_TRUE(AttributesFiller::FillInWriterAttr("qos", qos, &writer));
  EXPECT_EQ(writer.watt.times.heartbeatPeriod.seconds, 0);
  EXPECT_EQ(writer.watt.times.heartbeatPeriod.nanosec, 500000000u);
  qos.mps = 1;
  ASSERT_TRUE(AttributesFiller::FillInWriterAttr("qos", qos, &writer));
  EXPECT_EQ(writer.watt.times.heartbeatPeriod.seconds, 4);
  EXPECT_EQ(writer.watt.times.heartbeatPeriod.nanosec, 0u);
  qos.mps = 0;
  ASSERT_TRUE(AttributesFiller::FillInWriterAttr("qos", qos, &writer));
  EXPECT_EQ(writer.watt.times.heartbeatPeriod.seconds, WriterAttributes().times.heartbeatPeriod.seconds);
}

TEST(Qos, WireRoundTripCarriesResourceLimitAndRejectsOldProfile) {
  QosProfile qos, decoded;
  qos.max_samples = 123;
  serialize::DataStream encoded;
  encoded << qos;
  serialize::DataStream input(std::string(encoded.data(), encoded.size()));
  ASSERT_TRUE(input.read(decoded));
  EXPECT_TRUE(SameQosProfile(qos, decoded));
  struct OldProfile : serialize::Serializable {
    QosProfile q;
    SERIALIZE(q.history, q.depth, q.mps, q.msg_size, q.reliability, q.durability)
  } old;
  serialize::DataStream old_encoded;
  old_encoded << old;
  serialize::DataStream old_input(std::string(old_encoded.data(), old_encoded.size()));
  EXPECT_FALSE(old_input.read(decoded));
}

TEST(Qos, CompatibilityUsesOfferedAndRequestedPolicies) {
  QosProfile writer, reader;
  EXPECT_TRUE(IsQosCompatible(writer, reader));
  writer.reliability = RELIABILITY_BEST_EFFORT;
  EXPECT_FALSE(IsQosCompatible(writer, reader));
  reader.reliability = RELIABILITY_BEST_EFFORT;
  EXPECT_TRUE(IsQosCompatible(writer, reader));
  reader.durability = DURABILITY_TRANSIENT_LOCAL;
  EXPECT_FALSE(IsQosCompatible(writer, reader));
  writer.durability = DURABILITY_TRANSIENT_LOCAL;
  EXPECT_TRUE(IsQosCompatible(writer, reader));
  reader.durability = DURABILITY_VOLATILE;
  EXPECT_TRUE(IsQosCompatible(writer, reader));
}

TEST(Qos, ObservationDepthIsIndependentOfTransportAndPendingQueue) {
  RoleAttributes attr{};
  attr.channel_name = "qos_observation";
  attr.qos_profile.depth = 7;
  Subscriber<Chatter> subscriber(attr, nullptr, 3, 2);
  EXPECT_EQ(subscriber.GetHistoryDepth(), 2u);
  EXPECT_EQ(subscriber.PendingQueueSize(), 3u);
  for (int i = 1; i <= 4; ++i) {
    auto message = std::make_shared<Chatter>();
    message->seq = i;
    subscriber.Enqueue(message);
  }
  subscriber.Observe();
  EXPECT_EQ(subscriber.GetOldestObserved()->seq, 3u);
  EXPECT_EQ(subscriber.GetLatestObserved()->seq, 4u);
  subscriber.SetHistoryDepth(0);
  subscriber.ClearData();
  subscriber.Enqueue(std::make_shared<Chatter>());
  subscriber.Observe();
  EXPECT_TRUE(subscriber.Empty());
  EXPECT_EQ(subscriber.GetQosProfile().depth, 7u);
  EXPECT_EQ(subscriber.PendingQueueSize(), 3u);
}

TEST(Qos, SlowConsumerLosesOldPendingMessagesEvenWithReliableProfile) {
  const auto channel = common::GlobalData::RegisterChannel("qos_slow_consumer");
  data::DataVisitor<Chatter> visitor(channel, 2);
  auto* dispatcher = data::DataDispatcher<Chatter>::Instance();
  auto send = [&](int value) {
    auto message = std::make_shared<Chatter>();
    message->seq = value;
    dispatcher->Dispatch(channel, message);
  };
  std::shared_ptr<Chatter> received;
  send(1);
  ASSERT_TRUE(visitor.TryFetch(received));
  // Pause the consumer while the callback queue receives a burst.
  for (int i = 2; i <= 5; ++i) send(i);
  ASSERT_TRUE(visitor.TryFetch(received));
  // Existing ChannelBuffer behavior skips directly to the newest sample after
  // overrun, even when another retained sample could still be read.
  EXPECT_EQ(received->seq, 5u);
  EXPECT_FALSE(visitor.TryFetch(received));
}

TEST(Qos, SharedReceiverRejectsConflictsInBothCreationOrders) {
  for (bool reversed : {false, true}) {
    RoleAttributes first{};
    first.channel_name = reversed ? "qos_shared_b" : "qos_shared_a";
    first.channel_id = common::GlobalData::RegisterChannel(first.channel_name);
    first.qos_profile.depth = reversed ? 5 : 1;
    RoleAttributes second = first;
    second.qos_profile.depth = reversed ? 1 : 5;
    auto* manager = ReceiverManager<Chatter>::Instance();
    auto receiver = manager->GetReceiver(first);
    ASSERT_NE(receiver, nullptr);
    EXPECT_EQ(manager->GetReceiver(second), nullptr);
    EXPECT_EQ(manager->GetReceiver(first), receiver);
  }
}

TEST(Qos, InvalidConfigurationFailsAtPublicAndDirectEntrypoints) {
  RoleAttributes invalid{};
  invalid.channel_name = "qos_invalid";
  invalid.qos_profile.max_samples = 0;
  Publisher<Chatter> publisher(invalid);
  EXPECT_FALSE(publisher.Init());
  Subscriber<Chatter> subscriber(invalid);
  EXPECT_FALSE(subscriber.Init());
  auto* transport = Transport::Instance();
  EXPECT_EQ(transport->CreateTransmitter<Chatter>(invalid), nullptr);
  EXPECT_EQ(transport->CreateReceiver<Chatter>(invalid, nullptr), nullptr);
  EXPECT_EQ(transport->CreateTransmitter<LoanedMessage>(invalid), nullptr);
  EXPECT_EQ(transport->CreateReceiver<LoanedMessage>(invalid, nullptr), nullptr);
  EXPECT_THROW(IntraTransmitter<Chatter> direct(invalid), std::invalid_argument);
  Subscriber<Chatter> zero_queue(RoleAttributes(), nullptr, 0);
  EXPECT_FALSE(zero_queue.Init());
}
}

int main(int argc, char** argv) {
  hnu::cmw::Init("QosTest");
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  hnu::cmw::transport::Transport::Instance()->Shutdown();
  return result;
}
