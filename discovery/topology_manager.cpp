#include <cmw/discovery/topology_manager.h>
#include <exception>

#include <cmw/common/global_data.h>
#include <fastdds/rtps/participant/ParticipantDiscoveryInfo.h>
#include <cmw/time/time.h>
namespace hnu {
namespace cmw {
namespace discovery{ 

using namespace eprosima::fastrtps::rtps;
TopologyManager::TopologyManager()
    : init_(false),
      node_manager_(nullptr),
      channel_manager_(nullptr),
      participant_(nullptr),
      participant_listener_(nullptr){
    Init();
}

TopologyManager::~TopologyManager(){
    Shutdown();
}

void TopologyManager::Shutdown(){
    NodeManagerPtr node_manager;
    ChannelManagerPtr channel_manager;
    transport::ParticipantPtr participant;
    std::unique_ptr<ParticipantListener> participant_listener;
    {
        std::unique_lock<std::mutex> lock(lifecycle_mutex_);
        lifecycle_condition_.wait(lock, [this] {
            return !initializing_ && !shutting_down_;
        });
        if (!init_.load() && node_manager_ == nullptr &&
            channel_manager_ == nullptr && participant_ == nullptr &&
            participant_listener_ == nullptr) {
            return;
        }
        shutting_down_ = true;
        init_.store(false);
        node_manager = std::move(node_manager_);
        channel_manager = std::move(channel_manager_);
        participant = std::move(participant_);
        participant_listener = std::move(participant_listener_);
    }

    // Drain participant callbacks before managers are stopped. Fast DDS 2.12
    // removes endpoints and joins its event/receive threads synchronously when
    // the participant is removed, so the listener remains alive until then.
    if (participant_listener != nullptr) participant_listener->Stop();
    if (node_manager != nullptr) node_manager->Shutdown();
    if (channel_manager != nullptr) channel_manager->Shutdown();
    if (participant != nullptr) participant->Shutdown();
    participant.reset();
    participant_listener.reset();

    change_signal_.DisconnectAllSlots();
    {
        std::lock_guard<std::mutex> lock(participant_names_mutex_);
        participant_names_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        shutting_down_ = false;
    }
    lifecycle_condition_.notify_all();
}

bool TopologyManager::Init(){
    {
        std::unique_lock<std::mutex> lock(lifecycle_mutex_);
        lifecycle_condition_.wait(lock, [this] {
            return !initializing_ && !shutting_down_;
        });
        if (init_.load()) return true;
        initializing_ = true;
    }

    NodeManagerPtr node_manager;
    ChannelManagerPtr channel_manager;
    transport::ParticipantPtr participant;
    std::unique_ptr<ParticipantListener> participant_listener;
    bool result = false;
    try {
        node_manager = std::make_shared<NodeManager>();
        channel_manager = std::make_shared<ChannelManager>();
        result = CreateParticipant(&participant_listener, &participant);
        if (result) {
            auto* fastdds_participant = participant->fastrtps_participant();
            result = node_manager->StartDiscovery(fastdds_participant) &&
                     channel_manager->StartDiscovery(fastdds_participant);
        }
    } catch (const std::exception& error) {
        AERROR << "exception while initializing topology: " << error.what();
        result = false;
    } catch (...) {
        AERROR << "unknown exception while initializing topology";
        result = false;
    }

    if (!result) {
        std::cout << "init manager failed." << std::endl;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            initializing_ = false;
            shutting_down_ = true;
        }
        try {
            if (participant_listener != nullptr) participant_listener->Stop();
        } catch (...) {
            AERROR << "exception while stopping failed participant listener";
        }
        try {
            if (node_manager != nullptr) node_manager->Shutdown();
        } catch (...) {
            AERROR << "exception while rolling back node discovery";
        }
        try {
            if (channel_manager != nullptr) channel_manager->Shutdown();
        } catch (...) {
            AERROR << "exception while rolling back channel discovery";
        }
        try {
            if (participant != nullptr) participant->Shutdown();
        } catch (...) {
            AERROR << "exception while rolling back RTPS participant";
        }
        participant.reset();
        participant_listener.reset();
        {
            std::lock_guard<std::mutex> lock(participant_names_mutex_);
            participant_names_.clear();
        }
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            shutting_down_ = false;
        }
        lifecycle_condition_.notify_all();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        node_manager_ = std::move(node_manager);
        channel_manager_ = std::move(channel_manager);
        participant_ = std::move(participant);
        participant_listener_ = std::move(participant_listener);
        init_.store(true);
        initializing_ = false;
    }
    lifecycle_condition_.notify_all();
    return true;
}

NodeManagerPtr TopologyManager::node_manager() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return init_.load() ? node_manager_ : nullptr;
}

ChannelManagerPtr TopologyManager::channel_manager() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return init_.load() ? channel_manager_ : nullptr;
}


TopologyManager::ChangeConnection TopologyManager::AddChangeListener(const ChangeFunc& func){
    return change_signal_.Connect(func);
}

void TopologyManager::RemoveChangeListener(const ChangeConnection& conn){
    change_signal_.Disconnect(conn);
}


bool TopologyManager::CreateParticipant(
    std::unique_ptr<ParticipantListener>* listener,
    transport::ParticipantPtr* participant){
    if (listener == nullptr || participant == nullptr) return false;
    std::string participant_name =
      common::GlobalData::Instance()->HostName() + '+' +
      std::to_string(common::GlobalData::Instance()->ProcessId());
    std::cout <<"participant_name: " << participant_name << std::endl;
    //创建RTPSParticipantListener
    auto new_listener = std::unique_ptr<ParticipantListener>(new ParticipantListener(std::bind(
            &TopologyManager::OnParticipantChange, this , std::placeholders::_1)));
    //创建RTPSParticipant
    auto new_participant = std::make_shared<transport::Participant>(
        participant_name, 11511 , new_listener.get());
    
    if (new_participant->fastrtps_participant() == nullptr) return false;
    *listener = std::move(new_listener);
    *participant = std::move(new_participant);
    return true;
}

void TopologyManager::OnParticipantChange(const PartInfo& info){
    bool metadata_only = false;
    NodeManagerPtr node_manager;
    ChannelManagerPtr channel_manager;
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (init_.load()) {
            node_manager = node_manager_;
            channel_manager = channel_manager_;
        } else if (initializing_) {
            // Preserve GUID-to-host metadata for a participant discovered while
            // endpoint setup is still in progress. Business notification waits
            // until the topology is fully initialized.
            metadata_only = true;
        } else {
            return;
        }
    }

    ChangeMsg msg;
    if(!Convert(info , &msg)){
        return;
    }
    if (metadata_only) return;

    if(msg.operate_type == OperateType::OPT_LEAVE){
        auto& host_name = msg.role_attr.host_name;
        int process_id = msg.role_attr.process_id;
        if (node_manager != nullptr)
            node_manager->OnTopoModuleLeave(host_name, process_id);
        if (channel_manager != nullptr)
            channel_manager->OnTopoModuleLeave(host_name , process_id);
    }

    change_signal_(msg);

}


bool TopologyManager::Convert(const PartInfo& info, ChangeMsg* change_msg){
    auto guid = info.info.m_guid;
    auto status = info.status;
    std::string participant_name("");
    OperateType opt_type = OperateType::OPT_JOIN;


    switch (status)
    {
        //有新的participant加入
        case ParticipantDiscoveryInfo::DISCOVERY_STATUS::DISCOVERED_PARTICIPANT:
            participant_name = info.info.m_participantName;
            {
                std::lock_guard<std::mutex> lock(participant_names_mutex_);
                participant_names_[guid] = participant_name;
            }
            opt_type = OperateType::OPT_JOIN;
            break;
        
        case ParticipantDiscoveryInfo::DISCOVERY_STATUS::REMOVED_PARTICIPANT:

        //有participant离开
        case ParticipantDiscoveryInfo::DISCOVERY_STATUS::DROPPED_PARTICIPANT:
            {
                std::lock_guard<std::mutex> lock(participant_names_mutex_);
                auto iter = participant_names_.find(guid);
                if (iter != participant_names_.end()) {
                    participant_name = iter->second;
                    participant_names_.erase(iter);
                }
            }
            opt_type = OperateType::OPT_LEAVE;
            break;
    
    default:
        break;
    }

    std::string host_name("");
    int process_id = 0;
    //根据ParticipantName解析 host_name 和 process_id
    if(!ParseParticipantName(participant_name , &host_name , &process_id)){
        return false;
    }

    change_msg->timestamp = Time::Now().ToNanosecond();
    change_msg->change_type = ChangeType::CHANGE_PARTICIPANT;
    change_msg->operate_type = opt_type;
    change_msg->role_type = RoleType::ROLE_PARTICIPANT;

    // role attr 
    change_msg->role_attr.host_name = host_name;
    change_msg->role_attr.process_id = process_id;

    return true;
}


bool TopologyManager::ParseParticipantName(const std::string& participant_name,
                            std::string* host_name, int* process_id){
    
    auto pos = participant_name.find('+');
    if (pos == std::string::npos) {
        std::cout << "participant_name [" << participant_name << "] format mismatch."<< std::endl;
        return false;
    }
    *host_name = participant_name.substr(0 , pos);
    std::string pid_str = participant_name.substr(pos + 1);
    try {
        *process_id = std::stoi(pid_str);
    }catch (const std::exception& e){
        std::cout << "invalid process_id:" << e.what() << std::endl;
        return false;
    }
    return true;

}



}
}
}
