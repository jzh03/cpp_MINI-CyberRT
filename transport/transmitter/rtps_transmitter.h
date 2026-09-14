#ifndef CMW_TRANSPORT_TRANSMITTER_RTPS_TRANSMITTER_H_
#define CMW_TRANSPORT_TRANSMITTER_RTPS_TRANSMITTER_H_

#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/transmitter/transmitter.h>
#include <cmw/config/RoleAttributes.h>
#include <cmw/transport/rtps/participant.h>
#include <cmw/transport/rtps/attributes_filler.h>
#include <cmw/transport/rtps/qos_history.h>
#include <fastrtps/rtps/RTPSDomain.h>
#include <cmw/serialize/data_stream.h>
using namespace eprosima::fastrtps::rtps;
using namespace eprosima::fastrtps;


namespace hnu    {
namespace cmw   {
namespace transport {

using namespace config;

template <typename M>
class RtpsTransmitter : public Transmitter<M> {

public:
    using MessagePtr = std::shared_ptr<M>;
    using Transmitter<M>::TransmitLoanedMessage;

    RtpsTransmitter(const RoleAttributes& attr,
                    const ParticipantPtr& participant);

    virtual ~RtpsTransmitter();

    void Enable() override;
    void Disable() override;

    bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) override;

    std::unique_ptr<LoanedMessage> AcquireLoanedMessage(
        std::size_t capacity) override;
    bool TransmitLoanedMessage(std::unique_ptr<LoanedMessage> message,
                               const MessageInfo& msg_info) override;

private:
    bool Transmit(const M& msg, const MessageInfo& msg_info);
    bool TransmitImpl(const MessagePtr& msg, const MessageInfo& msg_info,
                      std::false_type);
    bool TransmitImpl(const MessagePtr& msg, const MessageInfo& msg_info,
                      std::true_type);
    bool TransmitSerialized(const char* serialized, std::size_t serialized_size,
                            const MessageInfo& msg_info);
    std::unique_ptr<LoanedMessage> AcquireLoanedMessageImpl(
        std::size_t capacity, std::false_type);
    std::unique_ptr<LoanedMessage> AcquireLoanedMessageImpl(
        std::size_t capacity, std::true_type);
    bool TransmitLoanedMessageImpl(std::unique_ptr<LoanedMessage> message,
                                   const MessageInfo& msg_info,
                                   std::false_type);
    bool TransmitLoanedMessageImpl(std::unique_ptr<LoanedMessage> message,
                                   const MessageInfo& msg_info,
                                   std::true_type);
    bool IsLoanedCapacityValid(std::size_t capacity) const;

    // Serializes enable/disable and the complete Writer/History use.
    // User-held heap loans do not retain this lock or any DDS resource.
    std::mutex lifecycle_mutex_;
    ParticipantPtr participant_;


    eprosima::fastrtps::rtps::RTPSWriter* rtps_writer;
    QosWriterHistory* mp_history;

};


template <typename M>
RtpsTransmitter<M>::RtpsTransmitter(const RoleAttributes& attr,
                                    const ParticipantPtr& participant)
        :Transmitter<M>(attr) , participant_(participant) , rtps_writer(nullptr),
         mp_history(nullptr) {}


template <typename M>
RtpsTransmitter<M>::~RtpsTransmitter() {
  Disable();
}

template <typename M>
void RtpsTransmitter<M>::Enable(){
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    if(this->enabled_){
        return;
    }

    if (participant_ == nullptr || participant_->is_shutdown()) {
        return;
    }

    auto* participant = participant_->fastrtps_participant();
    if (participant == nullptr) {
        return;
    }

    // 创建 RtpsWriter 的配置信息实例
    RtpsWriterAttributes writer_attr;
    // 填充 RtpsWriter 的配置信息
    if (!AttributesFiller::FillInWriterAttr(
        this->attr_.channel_name, this->attr_.qos_profile, &writer_attr)) {
      AERROR << "Invalid RTPS writer QoS: " << this->attr_.channel_name;
      return;
    }
    
    //创建rtps writer history
    mp_history = new QosWriterHistory(writer_attr.hatt, this->attr_.qos_profile);

    //创建rtps writer
    rtps_writer  = RTPSDomain::createRTPSWriter(participant, writer_attr.watt , mp_history);
    if (rtps_writer == nullptr) {
      delete mp_history;
      mp_history = nullptr;
      return;
    }

    //注册rtps writer
    bool reg = participant->registerWriter(rtps_writer , writer_attr.Tatt , writer_attr.Wqos);

    if(reg)
    {
      this->enabled_ = true;
#ifdef CMW_DEMO_ROUTE_TRACE
      CMW_DEMO_ROUTE_TRACE("RTPS", true, this->attr_.channel_name);
#endif
    } else {
      RTPSDomain::removeRTPSWriter(rtps_writer);
      rtps_writer = nullptr;
      delete mp_history;
      mp_history = nullptr;
    }

}

template <typename M>
void RtpsTransmitter<M>::Disable() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (rtps_writer != nullptr) {
    // Writer 由 RTPSDomain 删除；其关联的 History 仍由调用方负责释放。
    if (participant_ != nullptr && !participant_->is_shutdown()) {
      RTPSDomain::removeRTPSWriter(rtps_writer);
    }
    rtps_writer = nullptr;
  }
  if (mp_history != nullptr) {
    delete mp_history;
    mp_history = nullptr;
  }
#ifdef CMW_DEMO_ROUTE_TRACE
  if (this->enabled_) CMW_DEMO_ROUTE_TRACE("RTPS", false, this->attr_.channel_name);
#endif
  this->enabled_ = false;
}


template <typename M>
bool RtpsTransmitter<M>::Transmit(const MessagePtr& msg,
                                  const MessageInfo& msg_info) {
  if(msg == nullptr) {
    return false;
  }
  return TransmitImpl(msg, msg_info,
                      typename std::is_same<M, LoanedMessage>::type());
}

template <typename M>
bool RtpsTransmitter<M>::TransmitImpl(const MessagePtr& msg,
                                      const MessageInfo& msg_info,
                                      std::false_type) {
  return Transmit(*msg, msg_info);
}

template <typename M>
bool RtpsTransmitter<M>::TransmitImpl(const MessagePtr& msg,
                                      const MessageInfo& msg_info,
                                      std::true_type) {
  if(!msg->is_heap_backed()) {
    return false;
  }
  std::string serialized;
  if(!LoanedMessage::SerializePayload(*msg, &serialized)) {
    return false;
  }
  return TransmitSerialized(serialized.data(), serialized.size(), msg_info);
}

template <typename M>
bool RtpsTransmitter<M>::Transmit(const M& msg, const MessageInfo& msg_info) {
  serialize::DataStream ds;
  ds << msg;
  return TransmitSerialized(ds.data(), ds.size(), msg_info);
}

template <typename M>
bool RtpsTransmitter<M>::TransmitSerialized(const char* serialized,
                                             std::size_t serialized_size,
                                             const MessageInfo& msg_info) {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (!this->enabled_ || participant_ == nullptr ||
      participant_->is_shutdown()) {
    return false;
  }
  if(serialized == nullptr || serialized_size >
      std::numeric_limits<uint32_t>::max() || rtps_writer == nullptr ||
      mp_history == nullptr || participant_ == nullptr) {
    return false;
  }

  CacheChange_t* ch = rtps_writer->new_change(
      [serialized_size]() -> uint32_t {
        return static_cast<uint32_t>(serialized_size);
      }, ALIVE);
  if(ch == nullptr) {
    return false;
  }
  if(ch->serializedPayload.data == nullptr ||
     ch->serializedPayload.max_size < serialized_size) {
    rtps_writer->release_change(ch);
    return false;
  }


  eprosima::fastrtps::rtps::WriteParams wparams;
  char* ptr = reinterpret_cast<char*>(&wparams.related_sample_identity().writer_guid());
  memcpy(ptr, msg_info.sender_id().data() , ID_SIZE);
  memcpy(ptr + ID_SIZE , msg_info.spare_id().data() ,ID_SIZE);

  //sequenceNumber 
  wparams.related_sample_identity().sequence_number().high = (int32_t)((msg_info.seq_num() & 0xFFFFFFFF00000000) >> 32);
  wparams.related_sample_identity().sequence_number().low = (int32_t)(msg_info.seq_num() & 0xFFFFFFFF);

  ch->serializedPayload.length = static_cast<uint32_t>(serialized_size);

  if(serialized_size != 0) {
    std::memcpy(ch->serializedPayload.data, serialized, serialized_size);
  }
  //发送数据

  bool flag = mp_history->AddChange(ch, wparams);
  if(!flag) {
    // History only owns the change after a successful add.
    rtps_writer->release_change(ch);
  }
  return flag;

}

template <typename M>
std::unique_ptr<LoanedMessage> RtpsTransmitter<M>::AcquireLoanedMessage(
    std::size_t capacity) {
  return AcquireLoanedMessageImpl(
      capacity, typename std::is_same<M, LoanedMessage>::type());
}

template <typename M>
bool RtpsTransmitter<M>::TransmitLoanedMessage(
    std::unique_ptr<LoanedMessage> message, const MessageInfo& msg_info) {
  return TransmitLoanedMessageImpl(
      std::move(message), msg_info,
      typename std::is_same<M, LoanedMessage>::type());
}

template <typename M>
std::unique_ptr<LoanedMessage> RtpsTransmitter<M>::AcquireLoanedMessageImpl(
    std::size_t /*capacity*/, std::false_type) {
  return nullptr;
}

template <typename M>
std::unique_ptr<LoanedMessage> RtpsTransmitter<M>::AcquireLoanedMessageImpl(
    std::size_t capacity, std::true_type) {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (!this->enabled_ || !IsLoanedCapacityValid(capacity)) {
    return nullptr;
  }
  return LoanedMessage::CreateHeap(this->attr_.channel_id, capacity);
}

template <typename M>
bool RtpsTransmitter<M>::TransmitLoanedMessageImpl(
    std::unique_ptr<LoanedMessage> /*message*/, const MessageInfo& /*msg_info*/,
    std::false_type) {
  return false;
}

template <typename M>
bool RtpsTransmitter<M>::TransmitLoanedMessageImpl(
    std::unique_ptr<LoanedMessage> message, const MessageInfo& msg_info,
    std::true_type) {
  if (message == nullptr || !message->BeginHeapPublish()) {
    return false;
  }
  std::shared_ptr<LoanedMessage> shared_message(message.release());
  return Transmit(shared_message, msg_info);
}

template <typename M>
bool RtpsTransmitter<M>::IsLoanedCapacityValid(std::size_t capacity) const {
  const uint32_t configured_capacity = this->attr_.qos_profile.msg_size;
  return configured_capacity == 0 || capacity <= configured_capacity;
}


}
}
}




#endif
