#ifndef CMW_TRANSPORT_RTPS_QOS_HISTORY_H_
#define CMW_TRANSPORT_RTPS_QOS_HISTORY_H_

#include <cmw/config/qos_profile.h>
#include <fastrtps/rtps/history/ReaderHistory.h>
#include <fastrtps/rtps/history/WriterHistory.h>

namespace hnu {
namespace cmw {
namespace transport {

// The low-level RTPS API does not apply TopicAttributes::historyQos for us.
class QosWriterHistory : public eprosima::fastrtps::rtps::WriterHistory {
 public:
  QosWriterHistory(const eprosima::fastrtps::rtps::HistoryAttributes& attributes,
                   const config::QosProfile& qos);
  bool AddChange(eprosima::fastrtps::rtps::CacheChange_t* change,
                 eprosima::fastrtps::rtps::WriteParams& params);

 private:
  bool keep_all_;
  bool transient_local_;
};

class QosReaderHistory : public eprosima::fastrtps::rtps::ReaderHistory {
 public:
  QosReaderHistory(const eprosima::fastrtps::rtps::HistoryAttributes& attributes,
                   const config::QosProfile& qos);
  using ReaderHistory::received_change;
  bool can_change_be_added_nts(
      const eprosima::fastrtps::rtps::GUID_t& writer, uint32_t payload_size,
      size_t missing, bool& will_never_be_accepted) const override;
  bool received_change(eprosima::fastrtps::rtps::CacheChange_t* change,
                        size_t missing) override;

 private:
  bool keep_all_;
};

}
}
}
#endif
