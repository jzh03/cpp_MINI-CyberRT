#ifndef CMW_TRANSPORT_DISPATCHER_RTPS_DISPATCHER_H_
#define CMW_TRANSPORT_DISPATCHER_RTPS_DISPATCHER_H_


#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <cmw/common/macros.h>
#include <cmw/transport/dispatcher/dispatcher.h>
#include <cmw/transport/rtps/attributes_filler.h>
#include <cmw/transport/rtps/participant.h>
#include <cmw/transport/rtps/rea_listener.h>
#include <cmw/transport/message/loaned_message.h>
#include <fastrtps/rtps/rtps_fwd.h>
#include <cmw/serialize/data_stream.h>

namespace hnu    {
namespace cmw   {
namespace transport {

struct Reader {
    Reader() : reader(nullptr) , reader_listener(nullptr) {}

    eprosima::fastrtps::rtps::RTPSReader* reader;
    eprosima::fastrtps::rtps::ReaderHistory* mp_history;
    RealistenerPtr reader_listener;
};

class RtpsDispatcher;
using RtpsDispatcherPtr = RtpsDispatcher*;

class RtpsDispatcher : public Dispatcher {

public:
    virtual ~RtpsDispatcher();

    void Shutdown() override;

    template <typename MessageT>
    void AddListener(const RoleAttributes& self_attr,
                     const MessageListener<MessageT>& listener);

    template <typename MessageT>
    void AddListener(const RoleAttributes& self_attr,
                     const RoleAttributes& opposite_attr,
                     const MessageListener<MessageT>& listener);

    void set_participant(const ParticipantPtr& participant) {
        participant_ = participant;
  }
private:
    template <typename MessageT>
    void AddListenerImpl(const RoleAttributes& self_attr,
                         const MessageListener<MessageT>& listener,
                         std::false_type);
    template <typename MessageT>
    void AddListenerImpl(const RoleAttributes& self_attr,
                         const MessageListener<MessageT>& listener,
                         std::true_type);
    template <typename MessageT>
    void AddListenerImpl(const RoleAttributes& self_attr,
                         const RoleAttributes& opposite_attr,
                         const MessageListener<MessageT>& listener,
                         std::false_type);
    template <typename MessageT>
    void AddListenerImpl(const RoleAttributes& self_attr,
                         const RoleAttributes& opposite_attr,
                         const MessageListener<MessageT>& listener,
                         std::true_type);
    void OnMessage(uint64_t channel_id,
                   const std::shared_ptr<std::string>& msg_str,
                   const MessageInfo& msg_info);
    void AddReader(const RoleAttributes& self_attr);
    std::unordered_map<uint64_t , Reader> readers_;
    std::mutex readers_mutex_;
    ParticipantPtr participant_;

    DECLARE_SINGLETON(RtpsDispatcher)

};


template <typename MessageT>
void RtpsDispatcher::AddListener(const RoleAttributes& self_attr,
                                 const MessageListener<MessageT>& listener) {
    AddListenerImpl(self_attr, listener,
                    typename std::is_same<MessageT, LoanedMessage>::type());
}

template <typename MessageT>
void RtpsDispatcher::AddListenerImpl(const RoleAttributes& self_attr,
                                     const MessageListener<MessageT>& listener,
                                     std::false_type) {

    auto listener_adapter = [listener](const std::shared_ptr<std::string>& msg_str, 
                                       const MessageInfo& msg_info){
            auto msg = std::make_shared<MessageT>();
            serialize::DataStream ds(*msg_str);
            ds >> *msg;
            listener(msg , msg_info);
    };
    //调用基类的AddListener来注册回调函数
    Dispatcher::AddListener<std::string>(self_attr ,listener_adapter);
    AddReader(self_attr);

}

template <typename MessageT>
void RtpsDispatcher::AddListenerImpl(const RoleAttributes& self_attr,
                                     const MessageListener<MessageT>& listener,
                                     std::true_type) {
    const std::size_t max_payload = self_attr.qos_profile.msg_size == 0
        ? std::numeric_limits<uint32_t>::max()
        : self_attr.qos_profile.msg_size;
    auto listener_adapter = [listener, max_payload, channel_id = self_attr.channel_id](
        const std::shared_ptr<std::string>& msg_str,
        const MessageInfo& msg_info) {
            auto msg = LoanedMessage::DeserializePayload(
                msg_str->data(), msg_str->size(), max_payload, channel_id);
            if(msg == nullptr) {
                AERROR << "invalid loaned RTPS payload.";
                return;
            }
            listener(msg, msg_info);
    };
    Dispatcher::AddListener<std::string>(self_attr, listener_adapter);
    AddReader(self_attr);
}

template <typename MessageT>
void RtpsDispatcher::AddListener(const RoleAttributes& self_attr,
                                 const RoleAttributes& opposite_attr,
                                 const MessageListener<MessageT>& listener){
    AddListenerImpl(self_attr, opposite_attr, listener,
                    typename std::is_same<MessageT, LoanedMessage>::type());
}

template <typename MessageT>
void RtpsDispatcher::AddListenerImpl(const RoleAttributes& self_attr,
                                     const RoleAttributes& opposite_attr,
                                     const MessageListener<MessageT>& listener,
                                     std::false_type) {

    auto listener_adapter = [listener](const std::shared_ptr<std::string>& msg_str, 
                                       const MessageInfo& msg_info){
            auto msg = std::make_shared<MessageT>();
            serialize::DataStream ds(*msg_str);
            ds >> *msg;
            listener(msg , msg_info);
    };
    //调用基类的AddListener来注册回调函数
    Dispatcher::AddListener<std::string>(self_attr,opposite_attr,listener_adapter);
    //创建一个rtps reader
    AddReader(self_attr);
}

template <typename MessageT>
void RtpsDispatcher::AddListenerImpl(
    const RoleAttributes& self_attr, const RoleAttributes& opposite_attr,
    const MessageListener<MessageT>& listener, std::true_type) {
    const std::size_t max_payload = self_attr.qos_profile.msg_size == 0
        ? std::numeric_limits<uint32_t>::max()
        : self_attr.qos_profile.msg_size;
    auto listener_adapter = [listener, max_payload, channel_id = self_attr.channel_id](
        const std::shared_ptr<std::string>& msg_str,
        const MessageInfo& msg_info) {
            auto msg = LoanedMessage::DeserializePayload(
                msg_str->data(), msg_str->size(), max_payload, channel_id);
            if(msg == nullptr) {
                AERROR << "invalid loaned RTPS payload.";
                return;
            }
            listener(msg, msg_info);
    };
    Dispatcher::AddListener<std::string>(self_attr, opposite_attr,
                                         listener_adapter);
    AddReader(self_attr);
}


}
}
}



#endif
