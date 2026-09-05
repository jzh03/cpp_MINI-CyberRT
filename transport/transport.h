#ifndef CMW_TRANSPORT_TRANSPORT_H_
#define CMW_TRANSPORT_TRANSPORT_H_


#include <cmw/transport/rtps/participant.h>
#include <cmw/common/macros.h>
#include <cmw/config/RoleAttributes.h>
#include <cmw/config/transport_config.h>
#include <cmw/transport/transmitter/transmitter.h>
#include <cmw/transport/transmitter/rtps_transmitter.h>
#include <cmw/transport/receiver/receiver.h>
#include <cmw/transport/dispatcher/rtps_dispatcher.h>
#include <cmw/transport/receiver/rtps_receiver.h>
#include <cmw/common/log.h>
#include <cmw/transport/transmitter/shm_transmitter.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/transmitter/intra_transmitter.h>
#include <cmw/transport/receiver/intra_receiver.h>
#include <cmw/transport/transmitter/hybrid_transmitter.h>
#include <cmw/transport/receiver/hybrid_receiver.h>
#include <cmw/transport/message/loaned_message.h>

namespace hnu    {
namespace cmw   {
namespace transport {

using namespace config;

/**
 * @brief  Transport 构造时会创建 participant，然后通过CreateTransmitter 将此participant作为参数传递用于创建RtpsTransmitte
 */
class Transport
{
public:
    virtual ~Transport();

    void Shutdown();

    //返回一个Transmitter的指针
    template <typename M>
    auto CreateTransmitter(const RoleAttributes& attr,
                           const OptionalMode& mode = OptionalMode::HYBRID) ->
            typename std::shared_ptr<Transmitter<M>>;
    
    //返回一个Receiver的指针
    template <typename M>
    auto CreateReceiver(const RoleAttributes& attr,
                        const typename Receiver<M>::MessageListener& msg_listener,
                        const OptionalMode& mode = OptionalMode::HYBRID) ->
            typename std::shared_ptr<Receiver<M>>;

    //返回在构造函数中创建的participant_
    ParticipantPtr participant() const { return participant_; }
private:
    //构造函数会调用，来创建一个participant
    void CreateParticipant();
    ParticipantPtr participant_ = nullptr;
    std::atomic<bool> is_shutdown_ = {false};

    RtpsDispatcherPtr rtps_dispatcher_ = nullptr;

    DECLARE_SINGLETON(Transport)
};


template <typename M>
auto Transport::CreateTransmitter(const RoleAttributes& attr,
                                  const OptionalMode& mode) ->
        typename std::shared_ptr<Transmitter<M>>
{
    if(is_shutdown_.load())
    {
        std::cout << "transport has been shut down." << std::endl;
        return nullptr;
    }
    std::shared_ptr<Transmitter<M>> transmitter = nullptr;
    RoleAttributes modified_attr = attr ;


    switch (mode)
    {
        case OptionalMode::INTRA:
            transmitter = std::make_shared<IntraTransmitter<M>>(modified_attr);
            break;
        case OptionalMode::SHM:
            transmitter = std::make_shared<ShmTransmitter<M>>(modified_attr);
            break;
        case OptionalMode::RTPS:
            transmitter = std::make_shared<RtpsTransmitter<M>>(modified_attr , participant());
            break;
        case OptionalMode::HYBRID:
            transmitter = std::make_shared<HybridTransmitter<M>>(
                modified_attr, participant());
            break;
        default:
            return nullptr;
    }

    RETURN_VAL_IF_NULL(transmitter, nullptr);

    // 显式模式保持原有立即启用语义；HYBRID 等待 Discovery 提供对端属性。
    if( mode != OptionalMode::HYBRID){
        ADEBUG << "transmitter Enable";
        transmitter->Enable();
    }
    AINFO << "CreateTransmitter Sucess";
    return transmitter;
}

template <typename M>
auto Transport::CreateReceiver(const RoleAttributes& attr,
                               const typename Receiver<M>::MessageListener& msg_listener,
                               const OptionalMode& mode ) ->
                               typename std::shared_ptr<Receiver<M>>
{
    if(is_shutdown_.load()){
        AINFO << "transport has been shut down.";
        return nullptr;
    }

    //新建一个Receiver<M>类型的共享指针
    std::shared_ptr<Receiver<M>> receiver = nullptr;

    RoleAttributes modified_attr = attr;
    ADEBUG << "Receiver Mode: " << mode;
    switch (mode)
    {
        case OptionalMode::INTRA:
            receiver = std::make_shared<IntraReceiver<M>>(modified_attr, msg_listener);
            break;
        case OptionalMode::SHM:
            receiver = std::make_shared<ShmReceiver<M>>(modified_attr, msg_listener);
            break;
        case OptionalMode::RTPS:
            receiver = std::make_shared<RtpsReceiver<M>>(modified_attr , msg_listener);
            break;
        case OptionalMode::HYBRID:
            receiver = std::make_shared<HybridReceiver<M>>(modified_attr, msg_listener);
            break;
        default:
            return nullptr;
    }

    //保证receiver不为空
    RETURN_VAL_IF_NULL(receiver, nullptr);

    // HYBRID Receiver 由 Publisher JOIN/LEAVE 驱动具体接收路径。
    if (mode != OptionalMode::HYBRID) {
        receiver->Enable();
    }
    return receiver;
}

template <>
inline std::shared_ptr<Transmitter<LoanedMessage>>
Transport::CreateTransmitter<LoanedMessage>(const RoleAttributes& attr,
                                             const OptionalMode& mode)
{
    if(is_shutdown_.load()) {
        std::cout << "transport has been shut down." << std::endl;
        return nullptr;
    }

    std::shared_ptr<Transmitter<LoanedMessage>> transmitter = nullptr;
    switch(mode) {
        case OptionalMode::INTRA:
            transmitter = std::make_shared<IntraTransmitter<LoanedMessage>>(attr);
            break;
        case OptionalMode::SHM:
            transmitter = std::make_shared<ShmTransmitter<LoanedMessage>>(attr);
            break;
        case OptionalMode::RTPS:
            transmitter = std::make_shared<RtpsTransmitter<LoanedMessage>>(
                attr, participant());
            break;
        case OptionalMode::HYBRID:
            transmitter = std::make_shared<HybridTransmitter<LoanedMessage>>(
                attr, participant());
            break;
        default:
            return nullptr;
    }

    RETURN_VAL_IF_NULL(transmitter, nullptr);
    if(mode != OptionalMode::HYBRID) {
        transmitter->Enable();
    }
    AINFO << "Create LoanedMessage Transmitter Sucess";
    return transmitter;
}

template <>
inline std::shared_ptr<Receiver<LoanedMessage>>
Transport::CreateReceiver<LoanedMessage>(
    const RoleAttributes& attr,
    const Receiver<LoanedMessage>::MessageListener& msg_listener,
    const OptionalMode& mode)
{
    if(is_shutdown_.load()) {
        AINFO << "transport has been shut down.";
        return nullptr;
    }

    std::shared_ptr<Receiver<LoanedMessage>> receiver = nullptr;
    switch(mode) {
        case OptionalMode::INTRA:
            receiver = std::make_shared<IntraReceiver<LoanedMessage>>(
                attr, msg_listener);
            break;
        case OptionalMode::SHM:
            receiver = std::make_shared<ShmReceiver<LoanedMessage>>(
                attr, msg_listener);
            break;
        case OptionalMode::RTPS:
            receiver = std::make_shared<RtpsReceiver<LoanedMessage>>(
                attr, msg_listener);
            break;
        case OptionalMode::HYBRID:
            receiver = std::make_shared<HybridReceiver<LoanedMessage>>(
                attr, msg_listener);
            break;
        default:
            return nullptr;
    }

    RETURN_VAL_IF_NULL(receiver, nullptr);
    if(mode != OptionalMode::HYBRID) {
        receiver->Enable();
    }
    return receiver;
}

}
}
}

#endif
