#include <cmw/config/qos_profile.h>

#include <limits>

namespace hnu {
namespace cmw {
namespace config {

bool NormalizeQosProfile(const QosProfile& input, QosProfile* output,
                         std::string* error) {
  auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!output) return fail("null QoS output");
  QosProfile qos = input;
  const QosProfile defaults;
  if (qos.history == HISTORY_SYSTEM_DEFAULT) qos.history = defaults.history;
  if (qos.reliability == RELIABILITY_SYSTEM_DEFAULT)
    qos.reliability = defaults.reliability;
  if (qos.durability == DURABILITY_SYSTEM_DEFAULT)
    qos.durability = defaults.durability;
  if (qos.depth == 0) qos.depth = defaults.depth;
  if (qos.history != HISTORY_KEEP_LAST && qos.history != HISTORY_KEEP_ALL)
    return fail("invalid QoS history");
  if (qos.reliability != RELIABILITY_RELIABLE &&
      qos.reliability != RELIABILITY_BEST_EFFORT)
    return fail("invalid QoS reliability");
  if (qos.durability != DURABILITY_TRANSIENT_LOCAL &&
      qos.durability != DURABILITY_VOLATILE)
    return fail("invalid QoS durability");
  // Fast DDS uses int32_t counts and needs one extra allocation during replace.
  const uint32_t max_count = std::numeric_limits<int32_t>::max() - 1;
  if (qos.max_samples == 0 || qos.max_samples > max_count ||
      qos.depth > max_count)
    return fail("QoS sample count must fit a positive int32_t with one spare");
  if (qos.history == HISTORY_KEEP_LAST && qos.depth > qos.max_samples)
    return fail("KEEP_LAST depth exceeds max_samples");
  if (qos.msg_size > std::numeric_limits<uint32_t>::max() - 255u)
    return fail("QoS msg_size overflows RTPS allocation size");
  // KEEP_ALL does not use depth. Canonicalize irrelevant configuration as well.
  if (qos.history == HISTORY_KEEP_ALL) qos.depth = defaults.depth;
  *output = qos;
  if (error) error->clear();
  return true;
}

bool SameQosProfile(const QosProfile& lhs, const QosProfile& rhs) {
  QosProfile a, b;
  return NormalizeQosProfile(lhs, &a) && NormalizeQosProfile(rhs, &b) &&
      a.history == b.history && a.depth == b.depth && a.mps == b.mps &&
      a.msg_size == b.msg_size && a.reliability == b.reliability &&
      a.durability == b.durability && a.max_samples == b.max_samples;
}

bool IsQosCompatible(const QosProfile& writer, const QosProfile& reader) {
  QosProfile w, r;
  if (!NormalizeQosProfile(writer, &w) || !NormalizeQosProfile(reader, &r))
    return false;
  return !(r.reliability == RELIABILITY_RELIABLE &&
           w.reliability != RELIABILITY_RELIABLE) &&
      !(r.durability == DURABILITY_TRANSIENT_LOCAL &&
        w.durability != DURABILITY_TRANSIENT_LOCAL);
}

}
}
}
