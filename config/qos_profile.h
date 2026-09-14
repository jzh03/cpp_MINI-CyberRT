#ifndef CMW_CONFIG_QOS_PROFILE_H_
#define CMW_CONFIG_QOS_PROFILE_H_


#include <cmw/serialize/serializable.h>
#include <cmw/serialize/data_stream.h>
#include <string>
namespace hnu    {
namespace cmw   {
namespace config {

using namespace serialize;
enum QosHistoryPolicy : int32_t {
  HISTORY_SYSTEM_DEFAULT = 0,
  HISTORY_KEEP_LAST = 1,
  HISTORY_KEEP_ALL = 2,
};

enum QosReliabilityPolicy : int32_t {
  RELIABILITY_SYSTEM_DEFAULT = 0,
  RELIABILITY_RELIABLE = 1,
  RELIABILITY_BEST_EFFORT = 2,
};

enum QosDurabilityPolicy : int32_t {
  DURABILITY_SYSTEM_DEFAULT = 0,
  DURABILITY_TRANSIENT_LOCAL = 1,
  DURABILITY_VOLATILE = 2,
};

/*Qos配置信息结构体，支持序列化*/
class QosProfile : public Serializable
{
public:
  QosHistoryPolicy history = HISTORY_KEEP_LAST;
  uint32_t depth = 1;  // KEEP_LAST history; 0 resolves to the project default (1).
  uint32_t mps = 0;    // RTPS heartbeat hint, NOT a publishing rate limit.
  uint32_t msg_size = 0;
  QosReliabilityPolicy reliability = RELIABILITY_RELIABLE;
  QosDurabilityPolicy durability = DURABILITY_VOLATILE;
  uint32_t max_samples = 1000;  // RTPS history resource ceiling.

  // Appending max_samples changes the Discovery wire format. All peers must
  // use this version; the old six-field profile is not supported on the wire.
  SERIALIZE(history,depth,mps,msg_size,reliability,durability,max_samples)
};

// Resolve SYSTEM_DEFAULT and validate before allocating transport resources.
// On failure, output is unchanged. In-place normalization is supported.
bool NormalizeQosProfile(const QosProfile& input, QosProfile* output,
                         std::string* error = nullptr);
bool SameQosProfile(const QosProfile& lhs, const QosProfile& rhs);
bool IsQosCompatible(const QosProfile& writer, const QosProfile& reader);

}
}
}



#endif
