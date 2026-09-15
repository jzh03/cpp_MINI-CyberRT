#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <unistd.h>

#include <cmw/init.h>
#include <cmw/transport/transport.h>
#include <cmw/transport/rtps/qos_history.h>
#include <cmw/transport/qos/qos_profile_conf.h>
#include <fastrtps/rtps/attributes/RTPSParticipantAttributes.h>
#include <fastrtps/rtps/reader/ReaderListener.h>
#include <fastrtps/xmlparser/XMLProfileManager.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>

namespace {
using namespace hnu::cmw;
using namespace hnu::cmw::config;
using namespace hnu::cmw::transport;
using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;
using Clock = std::chrono::steady_clock;

std::string Unique(const std::string& suffix) {
  return "qos_" + suffix + "_" + std::to_string(getpid()) + "_" +
      std::to_string(Clock::now().time_since_epoch().count());
}

struct Collect : eprosima::fastrtps::rtps::ReaderListener {
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<uint32_t> values;
  bool incompatible = false;
  bool consume = true;  // Set before registering the reader.
  void onNewCacheChangeAdded(RTPSReader* reader, const CacheChange_t* const change) override {
    uint32_t value = 0;
    if (change->serializedPayload.length >= sizeof(value))
      std::memcpy(&value, change->serializedPayload.data, sizeof(value));
    if (consume)
      reader->getHistory()->remove_change(const_cast<CacheChange_t*>(change));
    std::lock_guard<std::mutex> lock(mutex);
    values.push_back(value);
    condition.notify_all();
  }
  void on_requested_incompatible_qos(RTPSReader*, eprosima::fastdds::dds::PolicyMask) override {
    std::lock_guard<std::mutex> lock(mutex);
    incompatible = true;
    condition.notify_all();
  }
  bool Wait(size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, std::chrono::seconds(8), [&] { return values.size() >= count; });
  }
};

// Raw RTPS tests inspect history and wait for matching before live sends.
struct Endpoints {
  Collect listener;
  RTPSParticipant* participant = nullptr;
  RTPSWriter* writer = nullptr;
  RTPSReader* reader = nullptr;
  std::unique_ptr<QosWriterHistory> writer_history;
  std::unique_ptr<QosReaderHistory> reader_history;
  std::string channel = Unique("raw");
  Endpoints() {
    RTPSParticipantAttributes attr;
    attr.setName(channel.c_str());
    attr.useBuiltinTransports = false;
    attr.userTransports.push_back(
        std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
    participant = RTPSDomain::createParticipant(120 + getpid() % 50, attr);
  }
  ~Endpoints() {
    if (reader) RTPSDomain::removeRTPSReader(reader);
    if (writer) RTPSDomain::removeRTPSWriter(writer);
    reader_history.reset();
    writer_history.reset();
    if (participant) RTPSDomain::removeRTPSParticipant(participant);
  }
  bool Writer(const QosProfile& qos) {
    RtpsWriterAttributes attr;
    if (!participant || !AttributesFiller::FillInWriterAttr(channel, qos, &attr)) return false;
    writer_history.reset(new QosWriterHistory(attr.hatt, qos));
    writer = RTPSDomain::createRTPSWriter(participant, attr.watt, writer_history.get());
    return writer && participant->registerWriter(writer, attr.Tatt, attr.Wqos);
  }
  bool Reader(const QosProfile& qos) {
    RtpsReaderAttributes attr;
    if (!participant || !AttributesFiller::FillInReaderAttr(channel, qos, &attr)) return false;
    reader_history.reset(new QosReaderHistory(attr.hatt, qos));
    reader = RTPSDomain::createRTPSReader(participant, attr.ratt, reader_history.get(), &listener);
    return reader && participant->registerReader(reader, attr.Tatt, attr.Rqos);
  }
  bool Matched() {
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    while (Clock::now() < deadline) {
      if (writer->matched_reader_is_matched(reader->getGuid()) &&
          reader->matched_writer_is_matched(writer->getGuid())) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
  bool Send(uint32_t value) {
    auto* change = writer->new_change([] { return uint32_t(sizeof(uint32_t)); }, ALIVE);
    if (!change) return false;
    change->serializedPayload.length = sizeof(value);
    std::memcpy(change->serializedPayload.data, &value, sizeof(value));
    WriteParams params;
    const bool result = writer_history->AddChange(change, params);
    if (!result) writer->release_change(change);
    return result;
  }
};

TEST(QosRtps, ExplicitTransientLocalKeepsOnlyConfiguredDepthForLateReader) {
  Endpoints endpoint;
  QosProfile qos;
  qos.durability = DURABILITY_TRANSIENT_LOCAL;
  qos.depth = 2;
  ASSERT_TRUE(endpoint.Writer(qos));
  for (uint32_t i = 1; i <= 20; ++i) ASSERT_TRUE(endpoint.Send(i));
  ASSERT_EQ(endpoint.writer_history->getHistorySize(), 2u);
  ASSERT_TRUE(endpoint.Reader(qos));
  ASSERT_TRUE(endpoint.Matched());
  ASSERT_TRUE(endpoint.listener.Wait(2));
  std::lock_guard<std::mutex> lock(endpoint.listener.mutex);
  EXPECT_EQ(endpoint.listener.values, (std::vector<uint32_t>{19, 20}));
  EXPECT_EQ(endpoint.reader_history->getHistorySize(), 0u);
}

TEST(QosRtps, DiscoveryHistoryRejectsFullCacheAndReplaysAnnouncements) {
  Endpoints endpoint;
  QosProfile qos = QosProfileConf::QOS_PROFILE_TOPO_CHANGE;
  qos.max_samples = 2;
  ASSERT_TRUE(endpoint.Writer(qos));
  ASSERT_TRUE(endpoint.Send(1));
  ASSERT_TRUE(endpoint.Send(2));
  for (int i = 0; i < 20; ++i) EXPECT_FALSE(endpoint.Send(3));
  ASSERT_EQ(endpoint.writer_history->getHistorySize(), 2u);
  ASSERT_TRUE(endpoint.Reader(qos));
  ASSERT_TRUE(endpoint.Matched());
  ASSERT_TRUE(endpoint.listener.Wait(2));
  std::lock_guard<std::mutex> lock(endpoint.listener.mutex);
  EXPECT_EQ(endpoint.listener.values, (std::vector<uint32_t>{1, 2}));
}

TEST(QosRtps, VolatileLateReaderReceivesOnlyNewSamples) {
  for (auto durability : {DURABILITY_VOLATILE, DURABILITY_TRANSIENT_LOCAL}) {
    Endpoints endpoint;
    QosProfile writer, reader;
    writer.depth = reader.depth = 2;
    writer.durability = durability;
    ASSERT_TRUE(endpoint.Writer(writer));
    ASSERT_TRUE(endpoint.Send(1));
    ASSERT_TRUE(endpoint.Send(2));
    ASSERT_EQ(endpoint.writer_history->getHistorySize(), 2u);
    ASSERT_TRUE(endpoint.Reader(reader));
    ASSERT_TRUE(endpoint.Matched());
    {
      std::unique_lock<std::mutex> lock(endpoint.listener.mutex);
      EXPECT_FALSE(endpoint.listener.condition.wait_for(lock, std::chrono::milliseconds(400),
          [&] { return !endpoint.listener.values.empty(); }));
    }
    ASSERT_TRUE(endpoint.Send(3));
    ASSERT_TRUE(endpoint.listener.Wait(1));
    std::lock_guard<std::mutex> lock(endpoint.listener.mutex);
    EXPECT_EQ(endpoint.listener.values, (std::vector<uint32_t>{3}));
  }
}

TEST(QosRtps, VolatileKeepAllReclaimsDeliveredSamplesWhenFull) {
  Endpoints endpoint;
  QosProfile qos;
  qos.history = HISTORY_KEEP_ALL;
  qos.max_samples = 2;
  ASSERT_TRUE(endpoint.Writer(qos));
  // With no readers, older samples have no remaining delivery obligation.
  for (uint32_t i = 1; i <= 4; ++i) ASSERT_TRUE(endpoint.Send(i));
  ASSERT_TRUE(endpoint.Reader(qos));
  ASSERT_TRUE(endpoint.Matched());
  for (uint32_t i = 5; i <= 8; ++i) {
    ASSERT_TRUE(endpoint.Send(i));
    ASSERT_TRUE(endpoint.listener.Wait(i - 4));
    ASSERT_TRUE(endpoint.writer->wait_for_all_acked(Duration_t(5, 0)));
    EXPECT_LE(endpoint.writer_history->getHistorySize(), 2u);
  }
  std::lock_guard<std::mutex> lock(endpoint.listener.mutex);
  EXPECT_EQ(endpoint.listener.values, (std::vector<uint32_t>{5, 6, 7, 8}));
}

TEST(QosRtps, BestEffortWriterCannotMatchReliableReader) {
  Endpoints endpoint;
  QosProfile writer, reader;
  writer.reliability = RELIABILITY_BEST_EFFORT;
  ASSERT_TRUE(endpoint.Writer(writer));
  ASSERT_TRUE(endpoint.Send(1));
  ASSERT_TRUE(endpoint.Reader(reader));
  std::unique_lock<std::mutex> lock(endpoint.listener.mutex);
  ASSERT_TRUE(endpoint.listener.condition.wait_for(lock, std::chrono::seconds(8),
      [&] { return endpoint.listener.incompatible; }));
  EXPECT_TRUE(endpoint.listener.values.empty());
}

TEST(QosRtps, ReliableKeepAllRetriesAfterReaderCapacityBecomesAvailable) {
  Endpoints endpoint;
  QosProfile writer, reader;
  writer.history = reader.history = HISTORY_KEEP_ALL;
  writer.max_samples = 2;
  reader.max_samples = 2;
  writer.mps = 1024;  // A 250 ms heartbeat; there is no application resend.
  endpoint.listener.consume = false;
  ASSERT_TRUE(endpoint.Writer(writer));
  ASSERT_TRUE(endpoint.Reader(reader));
  ASSERT_TRUE(endpoint.Matched());
  ASSERT_TRUE(endpoint.Send(1));
  ASSERT_TRUE(endpoint.Send(2));
  ASSERT_TRUE(endpoint.listener.Wait(2));
  ASSERT_TRUE(endpoint.writer->wait_for_all_acked(Duration_t(5, 0)));
  ASSERT_TRUE(endpoint.Send(3));
  {
    std::unique_lock<std::mutex> lock(endpoint.listener.mutex);
    EXPECT_FALSE(endpoint.listener.condition.wait_for(lock, std::chrono::milliseconds(400),
        [&] { return endpoint.listener.values.size() > 2; }));
  }
  ASSERT_EQ(endpoint.reader_history->getHistorySize(), 2u);
  // A delivered sample may be reclaimed, but an unacknowledged one must stay.
  ASSERT_TRUE(endpoint.Send(4));
  EXPECT_FALSE(endpoint.Send(5));
  {
    std::lock_guard<RecursiveTimedMutex> lock(*endpoint.reader_history->getMutex());
    ASSERT_TRUE(endpoint.reader_history->remove_change(
        *endpoint.reader_history->changesBegin()));
  }
  ASSERT_TRUE(endpoint.listener.Wait(3));
  EXPECT_EQ(endpoint.reader_history->getHistorySize(), 2u);
  std::lock_guard<std::mutex> lock(endpoint.listener.mutex);
  EXPECT_EQ(endpoint.listener.values, (std::vector<uint32_t>{1, 2, 3}));
}

TEST(QosRtps, KeepLastReaderBoundsUnconsumedHistory) {
  Endpoints endpoint;
  QosProfile qos;
  qos.depth = 2;
  endpoint.listener.consume = false;
  ASSERT_TRUE(endpoint.Writer(qos));
  ASSERT_TRUE(endpoint.Reader(qos));
  ASSERT_TRUE(endpoint.Matched());
  for (uint32_t i = 1; i <= 6; ++i) {
    ASSERT_TRUE(endpoint.Send(i));
    ASSERT_TRUE(endpoint.listener.Wait(i));
    EXPECT_LE(endpoint.reader_history->getHistorySize(), 2u);
  }
  EXPECT_EQ(endpoint.reader_history->getHistorySize(), 2u);
}

TEST(QosRtps, ReliableWriterMatchesBestEffortReader) {
  Endpoints endpoint;
  QosProfile writer, reader;
  reader.reliability = RELIABILITY_BEST_EFFORT;
  ASSERT_TRUE(endpoint.Writer(writer));
  ASSERT_TRUE(endpoint.Reader(reader));
  ASSERT_TRUE(endpoint.Matched());
  ASSERT_TRUE(endpoint.Send(42));
  ASSERT_TRUE(endpoint.listener.Wait(1));
  std::lock_guard<std::mutex> lock(endpoint.listener.mutex);
  EXPECT_EQ(endpoint.listener.values, (std::vector<uint32_t>{42}));
}

struct Message : serialize::Serializable {
  uint32_t value = 0;
  SERIALIZE(value)
};

RoleAttributes Attr(const std::string& channel) {
  RoleAttributes attr{};
  attr.channel_name = channel;
  attr.channel_id = common::GlobalData::RegisterChannel(channel);
  attr.qos_profile.depth = 2;
  attr.qos_profile.max_samples = 2;
  return attr;
}

TEST(QosRtps, SharedDispatcherRejectsConflictsWithoutReplacingFirstListener) {
  for (bool reversed : {false, true}) {
    auto first = Attr(Unique("shared"));
    first.qos_profile.depth = reversed ? 2 : 1;
    auto second = first;
    second.qos_profile.depth = reversed ? 1 : 2;
    // Conflict only in QoS, with distinct endpoint identities.
    std::mutex mutex;
    std::condition_variable condition;
    bool received = false;
    auto receiver = Transport::Instance()->CreateReceiver<Message>(first,
        [&](const std::shared_ptr<Message>&, const MessageInfo&, const RoleAttributes&) {
          std::lock_guard<std::mutex> lock(mutex);
          received = true;
          condition.notify_all();
        }, OptionalMode::RTPS);
    ASSERT_NE(receiver, nullptr);
    EXPECT_EQ(Transport::Instance()->CreateReceiver<Message>(second, nullptr, OptionalMode::RTPS), nullptr);
    auto transmitter = Transport::Instance()->CreateTransmitter<Message>(first, OptionalMode::RTPS);
    ASSERT_NE(transmitter, nullptr);
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    std::unique_lock<std::mutex> lock(mutex);
    while (!received && Clock::now() < deadline) {
      lock.unlock();
      ASSERT_TRUE(transmitter->Transmit(std::make_shared<Message>()));
      lock.lock();
      condition.wait_for(lock, std::chrono::milliseconds(50), [&] { return received; });
    }
    EXPECT_TRUE(received);
    lock.unlock();
    receiver->Disable();
    transmitter->Disable();
  }
}
}

int main(int argc, char** argv) {
  // Reliability/retry tests must traverse RTPS packets, not Fast DDS's
  // process-local delivery shortcut (which acknowledges processDataMsg).
  LibrarySettingsAttributes settings;
  settings.intraprocess_delivery = INTRAPROCESS_OFF;
  eprosima::fastrtps::xmlparser::XMLProfileManager::library_settings(settings);
  Init("QosRtpsTest");
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  Transport::Instance()->Shutdown();
  return result;
}
