#include <cmw/transport/rtps/attributes_filler.h>

#include <algorithm>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

template <typename EndpointQos>
void FillCommon(const std::string& channel, const config::QosProfile& qos,
                HistoryAttributes* history, EndpointAttributes* endpoint,
                EndpointQos* advertised, TopicAttributes* topic) {
  const int32_t capacity = static_cast<int32_t>(
      qos.history == config::HISTORY_KEEP_LAST ? qos.depth : qos.max_samples);
  history->payloadMaxSize = qos.msg_size + 255u;
  history->maximumReservedCaches = capacity;
  history->initialReservedCaches = std::min(capacity, 16);
  history->extraReservedCaches = 1;  // Allocate the replacement before eviction.
  endpoint->reliabilityKind = qos.reliability == config::RELIABILITY_RELIABLE
      ? RELIABLE : BEST_EFFORT;
  endpoint->durabilityKind = qos.durability == config::DURABILITY_TRANSIENT_LOCAL
      ? TRANSIENT_LOCAL : VOLATILE;
  advertised->m_reliability.kind = qos.reliability == config::RELIABILITY_RELIABLE
      ? RELIABLE_RELIABILITY_QOS : BEST_EFFORT_RELIABILITY_QOS;
  advertised->m_durability.kind = qos.durability == config::DURABILITY_TRANSIENT_LOCAL
      ? TRANSIENT_LOCAL_DURABILITY_QOS : VOLATILE_DURABILITY_QOS;
  topic->topicName = channel;
  topic->topicDataType = "string";
  topic->topicKind = NO_KEY;
  topic->historyQos.kind = qos.history == config::HISTORY_KEEP_LAST
      ? KEEP_LAST_HISTORY_QOS : KEEP_ALL_HISTORY_QOS;
  topic->historyQos.depth = static_cast<int32_t>(qos.depth);
  topic->resourceLimitsQos.max_samples = capacity;
  topic->resourceLimitsQos.max_instances = 1;
  topic->resourceLimitsQos.max_samples_per_instance = capacity;
  topic->resourceLimitsQos.allocated_samples = history->initialReservedCaches;
}

}

AttributesFiller::AttributesFiller() {}
AttributesFiller::~AttributesFiller() {}

bool AttributesFiller::FillInWriterAttr(const std::string& channel_name,
                                       const config::QosProfile& input,
                                       RtpsWriterAttributes* writer_attr) {
  config::QosProfile qos;
  if (!writer_attr || channel_name.empty() ||
      !config::NormalizeQosProfile(input, &qos)) return false;
  RtpsWriterAttributes result;
  FillCommon(channel_name, qos, &result.hatt, &result.watt.endpoint,
             &result.Wqos, &result.Tatt);
  if (qos.mps != 0 && qos.reliability == config::RELIABILITY_RELIABLE) {
    const uint64_t mps = std::max(64u, std::min(1024u, qos.mps));
    const uint64_t nanoseconds = 256ull * 1000000000ull / mps;
    result.watt.times.heartbeatPeriod.seconds = nanoseconds / 1000000000ull;
    result.watt.times.heartbeatPeriod.nanosec = nanoseconds % 1000000000ull;
  }
  *writer_attr = result;
  return true;
}

bool AttributesFiller::FillInReaderAttr(const std::string& channel_name,
                                       const config::QosProfile& input,
                                       RtpsReaderAttributes* reader_attr) {
  config::QosProfile qos;
  if (!reader_attr || channel_name.empty() ||
      !config::NormalizeQosProfile(input, &qos)) return false;
  RtpsReaderAttributes result;
  FillCommon(channel_name, qos, &result.hatt, &result.ratt.endpoint,
             &result.Rqos, &result.Tatt);
  *reader_attr = result;
  return true;
}

}
}
}
