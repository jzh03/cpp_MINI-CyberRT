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
    static std::shared_ptr<LoanedMessage> DeserializeLoanedRtpsPayload(
        const std::shared_ptr<std::string>& msg_str,
        std::size_t max_payload, uint64_t channel_id) {
        if (msg_str == nullptr) {
            return nullptr;
        }
        auto message = LoanedMessage::DeserializePayload(
            msg_str->data(), msg_str->size(), max_payload, channel_id);
        if (message != nullptr || msg_str->size() < sizeof(uint32_t)) {
            return message;
        }

        const char* serialized = msg_str->data();
        const uint32_t payload_size =
            (static_cast<uint32_t>(static_cast<uint8_t>(serialized[0])) << 24) |
            (static_cast<uint32_t>(static_cast<uint8_t>(serialized[1])) << 16) |
            (static_cast<uint32_t>(static_cast<uint8_t>(serialized[2])) << 8) |
            static_cast<uint32_t>(static_cast<uint8_t>(serialized[3]));
        if (payload_size > max_payload ||
            payload_size > std::numeric_limits<std::size_t>::max() -
                               sizeof(uint32_t)) {
            return nullptr;
        }
        const std::size_t wire_size = sizeof(uint32_t) + payload_size;
        // Fast DDS transports std::string samples with up to three zero padding
        // bytes.  Keep the wire decoder strict; accept padding only here.
        if (wire_size >= msg_str->size()) {
            return nullptr;
        }
        const std::size_t padding_size = msg_str->size() - wire_size;
        if (padding_size > 3) {
            return nullptr;
        }
        for (std::size_t i = wire_size; i < msg_str->size(); ++i) {
            if (serialized[i] != '\0') {
                return nullptr;
            }
        }
        return LoanedMessage::DeserializePayload(
            serialized, wire_size, max_payload, channel_id);
    }
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
            auto msg = DeserializeLoanedRtpsPayload(
                msg_str, max_payload, channel_id);
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
            auto msg = DeserializeLoanedRtpsPayload(
                msg_str, max_payload, channel_id);
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
