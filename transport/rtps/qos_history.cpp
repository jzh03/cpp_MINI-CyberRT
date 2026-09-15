#include <cmw/transport/rtps/qos_history.h>

#include <chrono>
#include <fastrtps/rtps/writer/RTPSWriter.h>

namespace hnu {
namespace cmw {
namespace transport {
using namespace eprosima::fastrtps::rtps;
using eprosima::fastrtps::RecursiveTimedMutex;

QosWriterHistory::QosWriterHistory(const HistoryAttributes& attributes,
                                   const config::QosProfile& qos)
    : WriterHistory(attributes), keep_all_(qos.history == config::HISTORY_KEEP_ALL),
      transient_local_(qos.durability == config::DURABILITY_TRANSIENT_LOCAL) {}

bool QosWriterHistory::AddChange(CacheChange_t* change, WriteParams& params) {
  if (!mp_mutex || !change) return false;
  std::lock_guard<RecursiveTimedMutex> guard(*mp_mutex);
  if (m_changes.size() >= static_cast<size_t>(m_att.maximumReservedCaches)) {
    // VOLATILE can reclaim a delivered sample when capacity is needed.
    // TRANSIENT_LOCAL KEEP_ALL retains it for late readers, even after ACK.
    // KEEP_LAST expires the oldest sample, including its retransmission window.
    if (keep_all_ && (transient_local_ ||
                     !mp_writer->is_acked_by_all(m_changes.front())))
      return false;
    if (!remove_min_change(std::chrono::steady_clock::now()))
      return false;
  }
  return WriterHistory::add_change(change, params);
}

QosReaderHistory::QosReaderHistory(const HistoryAttributes& attributes,
                                   const config::QosProfile& qos)
    : ReaderHistory(attributes), keep_all_(qos.history == config::HISTORY_KEEP_ALL) {}

bool QosReaderHistory::can_change_be_added_nts(
    const GUID_t& writer, uint32_t payload_size, size_t missing,
    bool& will_never_be_accepted) const {
  if (!ReaderHistory::can_change_be_added_nts(
          writer, payload_size, missing, will_never_be_accepted)) return false;
  will_never_be_accepted = false;
  const size_t capacity = static_cast<size_t>(m_att.maximumReservedCaches);
  // Reserve room for missing earlier samples on the reliable KEEP_ALL path.
  return !keep_all_ || (m_changes.size() < capacity &&
                        missing < capacity - m_changes.size());
}

bool QosReaderHistory::received_change(CacheChange_t* change, size_t missing) {
  if (!mp_mutex || !change) return false;
  std::lock_guard<RecursiveTimedMutex> guard(*mp_mutex);
  bool never = false;
  if (!can_change_be_added_nts(change->writerGUID,
          change->serializedPayload.length, missing, never)) return false;
  if (m_changes.size() >= static_cast<size_t>(m_att.maximumReservedCaches)) {
    if (keep_all_ || !remove_change(m_changes.front())) return false;
  }
  return ReaderHistory::add_change(change);
}

}
}
}
