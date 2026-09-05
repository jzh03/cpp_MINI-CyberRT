#ifndef CMW_TRANSPORT_TRANSMITTER_INTRA_TRANSMITTER_H_
#define CMW_TRANSPORT_TRANSMITTER_INTRA_TRANSMITTER_H_

#include <type_traits>

#include <cmw/transport/dispatcher/intra_dispatcher.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/transmitter/transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {

// INTRA 发送端：把原始 shared_ptr 直接交给进程内 Dispatcher。
template <typename M>
class IntraTransmitter : public Transmitter<M> {
 public:
  using MessagePtr = std::shared_ptr<M>;
  using Transmitter<M>::TransmitLoanedMessage;

  explicit IntraTransmitter(const RoleAttributes& attr)
      : Transmitter<M>(attr), dispatcher_(IntraDispatcher::Instance()) {}
  virtual ~IntraTransmitter() { Disable(); }

  void Enable() override { this->enabled_ = true; }

  void Disable() override { this->enabled_ = false; }

  bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) override {
    if (!this->enabled_) {
      return false;
    }
    // 保留原消息对象和 MessageInfo，不进入 SHM 或 FastDDS 数据路径。
    dispatcher_->Dispatch(this->attr_.channel_id, msg, msg_info);
    return true;
  }

  std::unique_ptr<LoanedMessage> AcquireLoanedMessage(
      std::size_t capacity) override {
    if(!std::is_same<M, LoanedMessage>::value || !this->enabled_ ||
       !IsLoanedCapacityValid(capacity)) {
      return nullptr;
    }
    return LoanedMessage::CreateHeap(this->attr_.channel_id, capacity);
  }

  bool TransmitLoanedMessage(std::unique_ptr<LoanedMessage> message,
                             const MessageInfo& msg_info) override {
    if(!std::is_same<M, LoanedMessage>::value || !this->enabled_ ||
       message == nullptr || !message->BeginHeapPublish()) {
      return false;
    }
    std::shared_ptr<LoanedMessage> shared_message(message.release());
    return Transmit(shared_message, msg_info);
  }

 private:
  bool IsLoanedCapacityValid(std::size_t capacity) const {
    const uint32_t configured_capacity = this->attr_.qos_profile.msg_size;
    return configured_capacity == 0 || capacity <= configured_capacity;
  }

  IntraDispatcherPtr dispatcher_;
};

}
}
}

#endif
