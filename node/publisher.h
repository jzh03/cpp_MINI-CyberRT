#ifndef CMW_NODE_PUBLISHER_H_
#define CMW_NODE_PUBLISHER_H_

#include <cstddef>
#include <memory>
#include <type_traits>
#include <cmw/node/publisher_base.h>
#include <cmw/transport/transport.h>
#include <cmw/discovery/topology_manager.h>
#include <cmw/transport/transmitter/transmitter.h>
#include <cmw/transport/transmitter/rtps_transmitter.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/config/message_type.h>
#include <cmw/common/global_data.h>
#include <cmw/common/log.h>
namespace hnu    {
namespace cmw   {

template<typename MessageT>
class Publisher : public PublisherBase
{
public:
    using TransmitterPtr = std::shared_ptr<transport::Transmitter<MessageT>>;
    using ChangeConnection = typename discovery::Manager::ChangeConnection;

    explicit Publisher(const RoleAttributes& role_attr);
    virtual ~Publisher();


    bool Init() override;

    void Shutdown() override;

    bool HasSubscriber() override;

    void GetSubscribers(std::vector<RoleAttributes>* subscribers) override;

    virtual bool Publish(const MessageT& msg);
    virtual bool Publish(const std::shared_ptr<MessageT>& msg_ptr);

    template <typename T = MessageT>
    typename std::enable_if<
        std::is_same<T, transport::LoanedMessage>::value,
        std::unique_ptr<transport::LoanedMessage>>::type
    AcquireMessage(std::size_t capacity);

    template <typename T = MessageT>
    typename std::enable_if<
        std::is_same<T, transport::LoanedMessage>::value, bool>::type
    Publish(std::unique_ptr<transport::LoanedMessage> message);

private:
    bool PublishImpl(const MessageT& msg, std::false_type);
    bool PublishImpl(const MessageT& msg, std::true_type);
    bool JoinTheTopology();
    void LeaveTheTopology();
    void OnChannelChange(const ChangeMsg& change_msg);

    TransmitterPtr transmitter_;

    ChangeConnection change_conn_;

    discovery::ChannelManagerPtr channel_manager_;

};

template<typename MessageT>
Publisher<MessageT>::Publisher(const RoleAttributes& role_attr)
    : PublisherBase(role_attr) , transmitter_(nullptr) , channel_manager_(nullptr){}

template<typename MessageT>
Publisher<MessageT>::~Publisher(){
    Shutdown();
}

template<typename MessageT>
bool Publisher<MessageT>::Init(){
    if (role_attr_.channel_name.empty()) {
        AERROR << "Invalid publisher channel name";
        return false;
    }
    if (role_attr_.channel_id == 0) {
        role_attr_.channel_id =
            common::GlobalData::RegisterChannel(role_attr_.channel_name);
    }
    role_attr_.message_type =
        config::MessageTypeIdentifier<MessageT>(role_attr_.message_type);
    if (role_attr_.message_type.empty()) {
        AERROR << "Invalid publisher message type identifier";
        return false;
    }
    std::string error;
    if (!config::NormalizeQosProfile(this->role_attr_.qos_profile,
                                     &this->role_attr_.qos_profile, &error)) {
        AERROR << "Invalid publisher QoS: " << error;
        return false;
    }
    {
        std::lock_guard<std::mutex> lg(lock_);
        if(init_){
            return true;
        }
        transmitter_ = transport::Transport::Instance()->CreateTransmitter<MessageT>(role_attr_);
        if(transmitter_ == nullptr){
            return false;
        }
        init_ = true;
    }
    this->role_attr_.id = transmitter_->id().HashValue();
    channel_manager_ = 
        discovery::TopologyManager::Instance()->channel_manager();
    if (channel_manager_ == nullptr || !JoinTheTopology()) {
        std::lock_guard<std::mutex> lg(lock_);
        init_ = false;
        if (transmitter_ != nullptr) transmitter_->Disable();
        transmitter_ = nullptr;
        channel_manager_ = nullptr;
        return false;
    }
    return true;
}

template<typename MessageT>
void Publisher<MessageT>::Shutdown(){
    {
        std::lock_guard<std::mutex> lg(lock_);
        if(!init_){
            return;
        }
        init_ = false;
    }
    LeaveTheTopology();
    transmitter_ = nullptr;
    channel_manager_ = nullptr;
}

template<typename MessageT>
bool Publisher<MessageT>::Publish(const MessageT& msg){
    RETURN_VAL_IF(!PublisherBase::IsInit() , false);
    return PublishImpl(msg,
        typename std::is_same<MessageT, transport::LoanedMessage>::type());
}

template<typename MessageT>
bool Publisher<MessageT>::PublishImpl(const MessageT& msg, std::false_type){
    auto msg_ptr = std::make_shared<MessageT>(msg);
    return Publish(msg_ptr);
}

template<typename MessageT>
bool Publisher<MessageT>::PublishImpl(const MessageT& msg, std::true_type){
    (void)msg;
    AERROR << "LoanedMessage must be published with Publish(std::unique_ptr).";
    return false;
}

template<typename MessageT>
bool Publisher<MessageT>::Publish(const std::shared_ptr<MessageT>& msg_ptr){
    RETURN_VAL_IF(!PublisherBase::IsInit(), false);
    return transmitter_->Transmit(msg_ptr);
}

template<typename MessageT>
template<typename T>
typename std::enable_if<
    std::is_same<T, transport::LoanedMessage>::value,
    std::unique_ptr<transport::LoanedMessage>>::type
Publisher<MessageT>::AcquireMessage(std::size_t capacity) {
    if(!PublisherBase::IsInit() || transmitter_ == nullptr) {
        return nullptr;
    }
    return transmitter_->AcquireLoanedMessage(capacity);
}

template<typename MessageT>
template<typename T>
typename std::enable_if<
    std::is_same<T, transport::LoanedMessage>::value, bool>::type
Publisher<MessageT>::Publish(std::unique_ptr<transport::LoanedMessage> message) {
    if(!PublisherBase::IsInit() || transmitter_ == nullptr) {
        return false;
    }
    return transmitter_->TransmitLoanedMessage(std::move(message));
}

template<typename MessageT>
bool Publisher<MessageT>::JoinTheTopology(){

    //
    change_conn_ = channel_manager_->AddChangeListener(std::bind(
        &Publisher<MessageT>::OnChannelChange, this , std::placeholders::_1));
    
    const std::string& channel_name = this->role_attr_.channel_name;
    std::vector<RoleAttributes> subscribers;
    // 覆盖 Subscriber 先启动场景：初始化时主动启用已经存在的 Reader。
    channel_manager_->GetReadersOfChannel(channel_name , &subscribers);

    for(auto& subscriber : subscribers){
        ADEBUG << "ENABLE";
        transmitter_->Enable(subscriber);
    }

    //加入拓扑图
    const bool announced =
        channel_manager_->Join(this->role_attr_, RoleType::ROLE_WRITER);
    if (!announced && !channel_manager_->HasWriter(this->role_attr_)) {
        channel_manager_->RemoveChangeListener(change_conn_);
        return false;
    }
    if (!announced) {
        AERROR << "Writer joined locally but Discovery announcement failed: "
               << this->role_attr_.channel_name;
    }
    return true;
}

template<typename MessageT>
void Publisher<MessageT>::LeaveTheTopology(){
    channel_manager_->RemoveChangeListener(change_conn_);
    channel_manager_->Leave(this->role_attr_, RoleType::ROLE_WRITER);
}

template<typename MessageT>
void Publisher<MessageT>::OnChannelChange(const ChangeMsg& change_msg){
    
    if(change_msg.role_type != RoleType::ROLE_READER){
        return;
    }

    auto& subscriber_attr = change_msg.role_attr;
    if(subscriber_attr.channel_name != this->role_attr_.channel_name){
        return;
    }

    auto operate_type = change_msg.operate_type;
    if(operate_type == OperateType::OPT_JOIN){
        //确定有新的关注channel_name 的 reader 加入时 transmitter_ 才会 enable
        transmitter_->Enable(subscriber_attr);
    } else {
        transmitter_->Disable(subscriber_attr);
    }

}

template<typename MessageT>
bool Publisher<MessageT>::HasSubscriber(){
    RETURN_VAL_IF(!PublisherBase::IsInit(), false);
    return channel_manager_->HasReader(role_attr_.channel_name);
}

template<typename MessageT>
void Publisher<MessageT>::GetSubscribers(std::vector<RoleAttributes>* subscribers){
    if(subscribers == nullptr){
        return;
    }

    if(!PublisherBase::IsInit()){
        return;
    }

    channel_manager_->GetReadersOfChannel(role_attr_.channel_name , subscribers);
}

}
}


#endif
