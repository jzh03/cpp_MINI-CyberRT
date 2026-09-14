


#ifndef CMW_NODE_SUBSCRIBER_BASE_H_
#define CMW_NODE_SUBSCRIBER_BASE_H_

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


#include <cmw/config/RoleAttributes.h>
#include <cmw/config/message_type.h>
#include <cmw/common/macros.h>
#include <cmw/event/perf_event_cache.h>
#include <cmw/transport/transport.h>
#include <cmw/transport/receiver/receiver.h>
#include <cmw/data/data_dispatcher.h>

namespace hnu    {
namespace cmw   {

using namespace config;
class SubscriberBase
{
public:
    explicit SubscriberBase(const RoleAttributes& role_attr )
                : role_attr_(role_attr) , init_(false) {}
    virtual ~SubscriberBase() {}

    virtual bool Init() = 0;

    virtual void Shutdown() = 0;

    /**
     * @brief Clear local data
     */
    virtual void ClearData() = 0;

    /**
     * @brief Get stored data
     */
    virtual void Observe() = 0;

    /**
     * @brief Query whether the Reader has data to be handled
     *
     * @return true if data container is empty
     * @return false if data container has data
     */
    virtual bool Empty() const = 0;   

    /**
     * @brief Query whether we have received data since last clear
     *
     * @return true if the reader has received data
     * @return false if the reader has not received data
     */
    virtual bool HasReceived() const = 0;

    /**
     * @brief Get time interval of since last receive message
     *
     * @return double seconds delay
     */
    virtual double GetDelaySec() const = 0;

    /**
     * @brief Get the value of pending queue size
     *
     * @return uint32_t result value
     */
    virtual uint32_t PendingQueueSize() const = 0;


    virtual bool HasPublisher() { return false; }

    virtual void GetPublishers(std::vector<RoleAttributes>* publishers) {}

    const std::string& GetChannelName() const {
        return role_attr_.channel_name;
    }

    uint64_t ChannelId() const { return role_attr_.channel_id; }

    const QosProfile& GetQosProfile() const {
        return role_attr_.qos_profile;
    }
    
    bool IsInit() const { return init_.load(); }
protected:

    RoleAttributes role_attr_;
    std::atomic<bool> init_;

};

// ReceiverManager caches one transport receiver per MessageT/channel for the
// process lifetime. Keep the channel's message type bound for the same
// lifetime, so a later ReceiverManager with a different C++ type cannot appear
// to succeed while the shared Dispatcher still owns the original handler.
class ReceiverChannelTypeRegistry {
 public:
    bool IsCompatible(const std::string& channel_name,
                      const std::string& message_type) {
        if (channel_name.empty() || message_type.empty()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto existing = message_types_.find(channel_name);
        return existing == message_types_.end() ||
            config::IsMessageTypeCompatible(existing->second, message_type);
    }

    template <typename CreateFn>
    bool BindOrCreate(const std::string& channel_name,
                      const std::string& message_type,
                      CreateFn&& create) {
        if (channel_name.empty() || message_type.empty()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto existing = message_types_.find(channel_name);
        if (existing != message_types_.end() &&
            !config::IsMessageTypeCompatible(existing->second, message_type)) {
            AERROR << "Conflicting message type for cached receiver: "
                   << channel_name;
            return false;
        }
        if (!create()) return false;
        if (existing == message_types_.end()) {
            message_types_.emplace(channel_name, message_type);
        }
        return true;
    }

 private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::string> message_types_;
    DECLARE_SINGLETON(ReceiverChannelTypeRegistry)
};

inline ReceiverChannelTypeRegistry::ReceiverChannelTypeRegistry() {}


template <typename MessageT>
class ReceiverManager{
    public:
        ~ReceiverManager() { receiver_map_.clear(); }
    
        auto GetReceiver(const RoleAttributes& role_attr) ->
                typename std::shared_ptr<transport::Receiver<MessageT>>;
    private:
        std::unordered_map<std::string, 
                    typename std::shared_ptr<transport::Receiver<MessageT>>> receiver_map_;
        std::mutex receiver_map_mutex_;
        DECLARE_SINGLETON(ReceiverManager<MessageT>)
};

template <typename MessageT>
ReceiverManager<MessageT>::ReceiverManager() {}

template <typename MessageT>
auto ReceiverManager<MessageT>::GetReceiver(const RoleAttributes& role_attr) ->
                typename std::shared_ptr<transport::Receiver<MessageT>>{
    RoleAttributes normalized_attr(role_attr);
    normalized_attr.message_type = config::MessageTypeIdentifier<MessageT>(
        normalized_attr.message_type);
    if (normalized_attr.message_type.empty()) {
        AERROR << "Invalid receiver message type identifier";
        return nullptr;
    }
    std::lock_guard<std::mutex> lg(receiver_map_mutex_);
    const std::string& channel_name = normalized_attr.channel_name;
    auto existing = receiver_map_.find(channel_name);
    if (existing != receiver_map_.end() && existing->second &&
        !config::SameQosProfile(existing->second->attributes().qos_profile,
                                normalized_attr.qos_profile)) {
        AERROR << "Conflicting QoS for shared receiver: " << channel_name;
        return nullptr;
    }
    // Ensure one compatible Receiver per channel and message type.
    if(receiver_map_.count(channel_name) == 0){
        std::shared_ptr<transport::Receiver<MessageT>> created;
        const bool bound = ReceiverChannelTypeRegistry::Instance()->BindOrCreate(
            channel_name, normalized_attr.message_type, [&]() {
                created = transport::Transport::Instance()->CreateReceiver<MessageT>(
                    normalized_attr, [](const std::shared_ptr<MessageT>& msg,
                                  const transport::MessageInfo& msg_info,
                                  const RoleAttributes& subscriber_attr){
                            data::DataDispatcher<MessageT>::Instance()->Dispatch(
                                subscriber_attr.channel_id, msg);
                            }
                );
                return created != nullptr;
            });
        if (!bound) return nullptr;
        receiver_map_[channel_name] = std::move(created);
    }
    auto result = receiver_map_[channel_name];
    if (!result) receiver_map_.erase(channel_name);
    return result;

}



}
}

#endif
