#ifndef CMW_TRANSPORT_TRANSMITTER_SHM_TRANSMITTER_H_
#define CMW_TRANSPORT_TRANSMITTER_SHM_TRANSMITTER_H_

#include <cstring>
#include <iostream>
#include <memory>
#include <type_traits>

#include <cmw/common/global_data.h>
#include <cmw/transport/shm/notifier_factory.h>
#include <cmw/transport/shm/readable_info.h>
#include <cmw/transport/shm/segment_factory.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/transmitter/transmitter.h>
#include <cmw/common/util.h>
#include <cmw/common/log.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/time/time.h>

namespace hnu    {
namespace cmw   {
namespace transport {

template <typename M>
class ShmTransmitter : public Transmitter<M> {

public:
    using MessagePtr = std::shared_ptr<M>;
    using Transmitter<M>::TransmitLoanedMessage;

    explicit ShmTransmitter(const RoleAttributes& attr);
    virtual ~ShmTransmitter();

    void Enable() override;
    void Disable() override;

    bool Transmit(const MessagePtr& msg, const MessageInfo& info) override;

    std::unique_ptr<LoanedMessage> AcquireLoanedMessage(
        std::size_t capacity) override;
    bool TransmitLoanedMessage(std::unique_ptr<LoanedMessage> message,
                               const MessageInfo& msg_info) override;
    bool TransmitHeapLoanedMessage(
        const std::shared_ptr<LoanedMessage>& message,
        const MessageInfo& msg_info);
    bool CanTransmitLoanedMessage(const LoanedMessage& message) const;

private:
    bool Transmit(const M& msg, const MessageInfo& msg_info);
    bool TransmitImpl(const MessagePtr& msg, const MessageInfo& msg_info,
                      std::false_type);
    bool TransmitImpl(const MessagePtr& msg, const MessageInfo& msg_info,
                      std::true_type);
    uint64_t InitialSegmentMessageSize(std::true_type) const;
    uint64_t InitialSegmentMessageSize(std::false_type) const;

    SegmentPtr segment_;
    uint64_t channel_id_;
    uint64_t host_id_;
    NotifierPtr notifier_;
};

template <typename M>
ShmTransmitter<M>::ShmTransmitter(const RoleAttributes& attr)
    : Transmitter<M>(attr),
      segment_(nullptr),
      channel_id_(attr.channel_id),
      notifier_(nullptr)
      {
        host_id_ = common::Hash(attr.host_ip);
}

template <typename M>
ShmTransmitter<M>::~ShmTransmitter(){
    Disable();
}

template <typename M>
void ShmTransmitter<M>::Enable(){
    if(this->enabled_){
        return;
    }
    segment_ = SegmentFactory::CreateSegment(
        channel_id_, InitialSegmentMessageSize(
                         typename std::is_same<M, LoanedMessage>::type()));
    notifier_ =NotifierFactory::CreateNotifier();
    this->enabled_ = true;
}

template <typename M>
void ShmTransmitter<M>::Disable(){
    if (this->enabled_) {
    segment_ = nullptr;
    notifier_ = nullptr;
    this->enabled_ = false;
  }
}

template <typename M>
bool ShmTransmitter<M>::Transmit(const MessagePtr& msg,
                                 const MessageInfo& msg_info) {
  if(msg == nullptr) {
    return false;
  }
  return TransmitImpl(msg, msg_info,
                      typename std::is_same<M, LoanedMessage>::type());
}

template <typename M>
bool ShmTransmitter<M>::TransmitImpl(const MessagePtr& msg,
                                     const MessageInfo& msg_info,
                                     std::false_type) {
  return Transmit(*msg, msg_info);
}

template <typename M>
bool ShmTransmitter<M>::TransmitImpl(const MessagePtr& msg,
                                     const MessageInfo& msg_info,
                                     std::true_type) {
  (void)msg;
  (void)msg_info;
  AERROR << "LoanedMessage must be published with Publish(std::unique_ptr).";
  return false;
}

template <typename M>
uint64_t ShmTransmitter<M>::InitialSegmentMessageSize(std::true_type) const {
  return this->attr_.qos_profile.msg_size;
}

template <typename M>
uint64_t ShmTransmitter<M>::InitialSegmentMessageSize(std::false_type) const {
  return 0;
}

template <typename M>
bool ShmTransmitter<M>::Transmit(const M& msg, const MessageInfo& msg_info){
    if (!this->enabled_) {
    ADEBUG << "not enable.";
    return false;
   }

    WritableBlock wb;
    ADEBUG << "Debug Serialize start: " << Time::Now().ToMicrosecond();
    //序列化成字符串
    serialize::DataStream ds; 
    ds << msg;
    //拿到序列化数据所占内存字节数
    std::size_t msg_size = ds.ByteSize();
    //
    ADEBUG << "Debug Serialize end: " << Time::Now().ToMicrosecond();
    
    //拿到一块block去写，并对拿到的这块block加上写锁
    if(!segment_->AcquireBlockToWrite(msg_size, ShmMessageType::SERIALIZED,
                                      &wb)){
        AERROR << "acquire block failed.";
        return false;
    }
    WritableBlockLease write_lease(segment_, wb);

    //拷贝序列化后的数据到wb.buf处
    std::memcpy(wb.buf , ds.data(), msg_size);

    wb.block->set_msg_size(msg_size);


    char* msg_info_addr = reinterpret_cast<char*>(wb.buf) + msg_size;

    //拷贝sender_id
    std::memcpy(msg_info_addr, msg_info.sender_id().data() ,ID_SIZE);
    //拷贝spare_id_
    std::memcpy(msg_info_addr + ID_SIZE , msg_info.spare_id().data() , ID_SIZE);
    //拷贝 seq
    *reinterpret_cast<uint64_t*>(msg_info_addr + ID_SIZE*2) = msg_info.seq_num();

    wb.block->set_msg_info_size(ID_SIZE*2 +sizeof(uint64_t));

    const uint32_t block_index = wb.index;
    const uint64_t generation = wb.generation;
    //释放此block的写锁
    write_lease.Release();

    //新建一个ReadableInfo
    ReadableInfo readable_info(host_id_, block_index, channel_id_, generation);

    ADEBUG << "Writing sharedmem message: "
         << common::GlobalData::GetChannelById(channel_id_)
         << " to block: " << block_index;
    //通知接收数据的进程处理数据,发送ReadableInfo
    return notifier_->Notify(readable_info);

}

template <typename M>
std::unique_ptr<LoanedMessage> ShmTransmitter<M>::AcquireLoanedMessage(
    std::size_t capacity) {
  if(!std::is_same<M, LoanedMessage>::value || !this->enabled_ ||
     segment_ == nullptr) {
    return nullptr;
  }
  const uint32_t configured_capacity = this->attr_.qos_profile.msg_size;
  if((configured_capacity != 0 && capacity > configured_capacity) ||
     capacity == 0) {
    AERROR << "invalid loaned message capacity: " << capacity;
    return nullptr;
  }

  WritableBlock writable_block;
  if(!segment_->AcquireBlockToWriteWithoutRecreate(
         capacity, ShmMessageType::LOANED, &writable_block)) {
    return nullptr;
  }
  WritableBlockLease write_lease(segment_, writable_block);
  return std::unique_ptr<LoanedMessage>(new LoanedMessage(
      writable_block.buf, capacity, std::move(write_lease), channel_id_, this));
}

template <typename M>
bool ShmTransmitter<M>::TransmitLoanedMessage(
    std::unique_ptr<LoanedMessage> message, const MessageInfo& msg_info) {
  if(!std::is_same<M, LoanedMessage>::value || message == nullptr ||
     !this->enabled_ || segment_ == nullptr || notifier_ == nullptr ||
     message->channel_id() != channel_id_ || !message->BeginPublish(this)) {
    return false;
  }

  const WritableBlock& writable_block = message->writable_block();
  if(writable_block.block == nullptr || writable_block.buf == nullptr ||
     message->size() > message->capacity() ||
     message->size() > segment_->payload_capacity()) {
    return false;
  }

  writable_block.block->set_msg_size(message->size());
  char* msg_info_addr = reinterpret_cast<char*>(writable_block.buf) +
                        message->size();
  std::memcpy(msg_info_addr, msg_info.sender_id().data(), ID_SIZE);
  std::memcpy(msg_info_addr + ID_SIZE, msg_info.spare_id().data(), ID_SIZE);
  *reinterpret_cast<uint64_t*>(msg_info_addr + ID_SIZE * 2) =
      msg_info.seq_num();
  writable_block.block->set_msg_info_size(ID_SIZE * 2 + sizeof(uint64_t));

  const uint32_t block_index = writable_block.index;
  const uint64_t generation = writable_block.generation;
  message->ReleaseWritableLease();

  ReadableInfo readable_info(host_id_, block_index, channel_id_, generation);
  return notifier_->Notify(readable_info);
}

template <typename M>
bool ShmTransmitter<M>::CanTransmitLoanedMessage(
    const LoanedMessage& message) const {
  return std::is_same<M, LoanedMessage>::value && this->enabled_ &&
      segment_ != nullptr && notifier_ != nullptr && message.is_shm_backed() &&
      message.CanPublish() && message.channel_id() == channel_id_ &&
      message.owner_ == this && message.write_lease_ &&
      message.size() <= segment_->payload_capacity();
}

template <typename M>
bool ShmTransmitter<M>::TransmitHeapLoanedMessage(
    const std::shared_ptr<LoanedMessage>& message,
    const MessageInfo& msg_info) {
  if(!std::is_same<M, LoanedMessage>::value || message == nullptr ||
     !message->is_heap_backed() || !message->is_read_only() ||
     !this->enabled_ || segment_ == nullptr || notifier_ == nullptr ||
     message->channel_id() != channel_id_ ||
     message->size() > message->capacity() ||
     message->size() > segment_->payload_capacity()) {
    return false;
  }

  WritableBlock writable_block;
  if(!segment_->AcquireBlockToWriteWithoutRecreate(
         message->size(), ShmMessageType::LOANED, &writable_block)) {
    return false;
  }
  WritableBlockLease write_lease(segment_, writable_block);
  if(message->size() != 0) {
    std::memcpy(writable_block.buf, message->data(), message->size());
  }
  writable_block.block->set_msg_size(message->size());
  char* msg_info_addr = reinterpret_cast<char*>(writable_block.buf) +
                        message->size();
  std::memcpy(msg_info_addr, msg_info.sender_id().data(), ID_SIZE);
  std::memcpy(msg_info_addr + ID_SIZE, msg_info.spare_id().data(), ID_SIZE);
  *reinterpret_cast<uint64_t*>(msg_info_addr + ID_SIZE * 2) =
      msg_info.seq_num();
  writable_block.block->set_msg_info_size(ID_SIZE * 2 + sizeof(uint64_t));

  const uint32_t block_index = writable_block.index;
  const uint64_t generation = writable_block.generation;
  write_lease.Release();
  ReadableInfo readable_info(host_id_, block_index, channel_id_, generation);
  return notifier_->Notify(readable_info);
}



}
}
}


#endif
