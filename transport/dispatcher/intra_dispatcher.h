#ifndef CMW_TRANSPORT_DISPATCHER_INTRA_DISPATCHER_H_
#define CMW_TRANSPORT_DISPATCHER_INTRA_DISPATCHER_H_

#include <cmw/common/macros.h>
#include <cmw/transport/dispatcher/dispatcher.h>

namespace hnu {
namespace cmw {
namespace transport {

class IntraDispatcher;
using IntraDispatcherPtr = IntraDispatcher*;

// 进程内消息分发器：复用 Dispatcher 的 listener 管理，直接分发消息智能指针。
class IntraDispatcher : public Dispatcher {
 public:
  virtual ~IntraDispatcher() {}

  template <typename MessageT>
  void Dispatch(uint64_t channel_id, const std::shared_ptr<MessageT>& msg,
                const MessageInfo& msg_info) {
    if (is_shutdown_.load()) {
      return;
    }

    // channel_id 定位同一进程中的订阅监听器，不进行序列化或内存拷贝。
    ListenerHandlerBasePtr* handler_base = nullptr;
    if (msg_listeners_.Get(channel_id, &handler_base)) {
      auto handler =
          std::dynamic_pointer_cast<ListenerHandler<MessageT>>(*handler_base);
      if (handler != nullptr) {
        handler->Run(msg, msg_info);
      }
    }
  }

  DECLARE_SINGLETON(IntraDispatcher)
};

}
}
}

#endif
