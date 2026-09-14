#ifndef CMW_SERVICE_DISCOVERY_TOPOLOGY_MANAGER_H_
#define CMW_SERVICE_DISCOVERY_TOPOLOGY_MANAGER_H_


#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>


#include <cmw/base/signal.h>
#include <cmw/common/macros.h>
#include <cmw/discovery/specific_manager/node_manager.h>
#include <cmw/discovery/specific_manager/channel_manager.h>
#include <cmw/transport/rtps/participant.h>
#include <cmw/discovery/communication/participant_listener.h>

namespace hnu {
namespace cmw {
namespace discovery{ 


class NodeManager;
using NodeManagerPtr = std::shared_ptr<NodeManager>;

class ChannelManager;
using ChannelManagerPtr = std::shared_ptr<ChannelManager>;

class TopologyManager {

public:
    using ChangeSignal = base::Signal<const ChangeMsg&>;
    using ChangeFunc = std::function<void(const ChangeMsg&)>;
    using ChangeConnection = base::Connection<const ChangeMsg&>;

    using PartNameContainer = std::map<eprosima::fastrtps::rtps::GUID_t, std::string>;
    using PartInfo = eprosima::fastrtps::rtps::ParticipantDiscoveryInfo;

    virtual ~TopologyManager();

    // Init may be retried after a failed initialization or an explicit Shutdown.
    bool Init();
    void Shutdown();
    bool IsInitialized() const { return init_.load(); }

    ChangeConnection AddChangeListener(const ChangeFunc& func);
    void RemoveChangeListener(const ChangeConnection& conn);    

    NodeManagerPtr node_manager() const;
    ChannelManagerPtr channel_manager() const;

private:

    bool CreateParticipant(
        std::unique_ptr<ParticipantListener>* listener,
        transport::ParticipantPtr* participant);

    void OnParticipantChange(const PartInfo& info);

    bool Convert(const PartInfo& info, ChangeMsg* change_msg);

    bool ParseParticipantName(const std::string& participant_name,
                            std::string* host_name, int* process_id);

    std::atomic<bool> init_;
    bool initializing_ = false;
    bool shutting_down_ = false;
    mutable std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_condition_;

    NodeManagerPtr node_manager_;
    ChannelManagerPtr channel_manager_;

    transport::ParticipantPtr participant_;

    std::unique_ptr<ParticipantListener> participant_listener_;

    ChangeSignal change_signal_;

    mutable std::mutex participant_names_mutex_;
    PartNameContainer participant_names_;
    
    DECLARE_SINGLETON(TopologyManager)
};


}
}
}

#endif
