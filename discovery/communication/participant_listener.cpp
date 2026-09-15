#include <cmw/discovery/communication/participant_listener.h>
#include <cmw/common/log.h>


namespace hnu {
namespace cmw {
namespace discovery{ 


ParticipantListener::ParticipantListener(const ChangeFunc& callback) 
        : callback_(callback) {}

ParticipantListener::~ParticipantListener(){
    Stop();
}

void ParticipantListener::Stop() {
    std::lock_guard<std::mutex> lck(mutex_);
    callback_ = nullptr;
}

void ParticipantListener::onParticipantDiscovery(
            eprosima::fastrtps::rtps::RTPSParticipant* p,
            eprosima::fastrtps::rtps::ParticipantDiscoveryInfo&& info){
    (void)p;
    std::lock_guard<std::mutex> lock(mutex_);
    RETURN_IF_NULL(callback_);
    callback_(info);
}


}
}
}
