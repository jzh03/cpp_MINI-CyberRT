#include <cmw/transport/dispatcher/rtps_dispatcher.h>
#include <cmw/transport/rtps/attributes_filler.h>
#include <fastrtps/rtps/RTPSDomain.h>
#include <fastrtps/rtps/reader/RTPSReader.h>
namespace hnu    {
namespace cmw   {
namespace transport {

RtpsDispatcher::RtpsDispatcher() : participant_(nullptr) {}
RtpsDispatcher::~RtpsDispatcher() { Shutdown(); }

void RtpsDispatcher::Shutdown() {
  if (is_shutdown_.exchange(true)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(readers_mutex_);
    for (auto& item : readers_) {
      if (item.second.reader && participant_ && !participant_->is_shutdown())
        RTPSDomain::removeRTPSReader(item.second.reader);
      item.second.reader = nullptr;
      delete item.second.mp_history;
      item.second.mp_history = nullptr;
    }
    readers_.clear();
  }

  participant_ = nullptr;
}

bool RtpsDispatcher::AddReader(const RoleAttributes& self_attr,
                               const std::function<void()>& attach,
                               const std::function<void()>& detach) {
    if (is_shutdown_.load() || participant_ == nullptr || participant_->is_shutdown()) {
    std::cout << "please set participant firstly." << std::endl;
    return false;
  }

    uint64_t channel_id = self_attr.channel_id;
    std::lock_guard<std::mutex> lock(readers_mutex_);

    if(readers_.count(channel_id) > 0){
        if (!config::SameQosProfile(readers_.at(channel_id).qos, self_attr.qos_profile)) {
            AERROR << "Conflicting QoS for shared RTPS reader: " << self_attr.channel_name;
            return false;
        }
        attach();
        return true;
    }

    //创建一个reader
    Reader new_reader;
    //填充reader的配置信息
    RtpsReaderAttributes reader_attr;

    if (!config::NormalizeQosProfile(self_attr.qos_profile, &new_reader.qos) ||
        !AttributesFiller::FillInReaderAttr(self_attr.channel_name, new_reader.qos,
                                           &reader_attr)) {
        AERROR << "Invalid RTPS reader QoS: " << self_attr.channel_name;
        return false;
    }
    auto* participant = participant_->fastrtps_participant();
    if (!participant) return false;
    //创建reader的回调函数
    new_reader.reader_listener = std::make_shared<ReaListener>(
        std::bind(&RtpsDispatcher::OnMessage, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3),self_attr.channel_name);
    //创建rtps reader history
    new_reader.mp_history = new QosReaderHistory(reader_attr.hatt, new_reader.qos);
    //创建rtps reader
    new_reader.reader = RTPSDomain::createRTPSReader(
                    participant, reader_attr.ratt, new_reader.mp_history,
                    new_reader.reader_listener.get());
    if (!new_reader.reader) {
        delete new_reader.mp_history;
        return false;
    }
    // A matching writer may deliver data as soon as registerReader completes.
    attach();
    //注册rtps reader
    bool reg = participant->registerReader(new_reader.reader, reader_attr.Tatt, reader_attr.Rqos);
    if (!reg) {
        RTPSDomain::removeRTPSReader(new_reader.reader);
        delete new_reader.mp_history;
        detach();
        return false;
    }

    readers_[channel_id] = new_reader;          
    return true;
}

void RtpsDispatcher::OnMessage(uint64_t channel_id,
                               const std::shared_ptr<std::string>& msg_str,
                               const MessageInfo& msg_info) {
    if (is_shutdown_.load()) {
    return;
  }     

  ListenerHandlerBasePtr* handler_base = nullptr;
  if (msg_listeners_.Get(channel_id, &handler_base)) {
    auto handler =
        std::dynamic_pointer_cast<ListenerHandler<std::string>>(*handler_base);
    //根据信号执行注册的槽函数
    handler->Run(msg_str, msg_info);
  }
}

}
}
}
