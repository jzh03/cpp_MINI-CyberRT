#ifndef CMW_TRANSPORT_RECEIVER_INTRA_RECEIVER_H_
#define CMW_TRANSPORT_RECEIVER_INTRA_RECEIVER_H_

#include <cmw/transport/dispatcher/intra_dispatcher.h>
#include <cmw/transport/receiver/receiver.h>

namespace hnu {
namespace cmw {
namespace transport {

// INTRA 接收端：在进程内 Dispatcher 上注册和注销原有消息回调。
template <typename M>
class IntraReceiver : public Receiver<M> {
 public:
  IntraReceiver(const RoleAttributes& attr,
                const typename Receiver<M>::MessageListener& msg_listener)
      : Receiver<M>(attr, msg_listener), dispatcher_(IntraDispatcher::Instance()) {}
  virtual ~IntraReceiver() { Disable(); }

  void Enable() override {
    if (this->enabled_) {
      return;
    }
    dispatcher_->AddListener<M>(
        this->attr_, std::bind(&IntraReceiver<M>::OnNewMessage, this,
                               std::placeholders::_1, std::placeholders::_2));
    this->enabled_ = true;
  }

  void Disable() override {
    if (!this->enabled_) {
      return;
    }
    dispatcher_->RemoveListener<M>(this->attr_);
    this->enabled_ = false;
  }

  void Enable(const RoleAttributes& opposite_attr) override {
    // 按 Publisher ID 注册，支持同一 channel 上存在多个 Publisher。
    dispatcher_->AddListener<M>(
        this->attr_, opposite_attr,
        std::bind(&IntraReceiver<M>::OnNewMessage, this,
                  std::placeholders::_1, std::placeholders::_2));
  }

  void Disable(const RoleAttributes& opposite_attr) override {
    // 仅移除离开的 Publisher，不影响同 channel 上的其他发送端。
    dispatcher_->RemoveListener<M>(this->attr_, opposite_attr);
  }

 private:
  IntraDispatcherPtr dispatcher_;
};

}
}
}

#endif
