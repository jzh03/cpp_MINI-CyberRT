#ifndef CMW_TRANSPORT_TRANSMITTER_TRANSMITTER_H_
#define CMW_TRANSPORT_TRANSMITTER_TRANSMITTER_H_


#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <cmw/transport/common/endpoint.h>
#include <cmw/transport/message/message_info.h>
#include <cmw/event/perf_event_cache.h>
namespace hnu    {
namespace cmw   {
namespace transport {

using namespace event;
class LoanedMessage;

template <typename M>
class Transmitter: public Endpoint
{

public:
    using MessagePtr = std::shared_ptr<M>;

    explicit Transmitter(const RoleAttributes& attr);

    virtual ~Transmitter();


    virtual void Enable() = 0;
    virtual void Disable() = 0;

    virtual void Enable(const RoleAttributes& attr);
    virtual void Disable(const RoleAttributes& attr);

    virtual bool Transmit(const MessagePtr& msg);
    virtual bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) = 0;

    virtual std::unique_ptr<LoanedMessage> AcquireLoanedMessage(
        std::size_t capacity) {
        (void)capacity;
        return nullptr;
    }

    virtual bool TransmitLoanedMessage(std::unique_ptr<LoanedMessage> message);
    virtual bool TransmitLoanedMessage(std::unique_ptr<LoanedMessage> message,
                                       const MessageInfo& msg_info) {
        (void)message;
        (void)msg_info;
        return false;
    }

    uint64_t NextSeqNum() { return ++seq_num_; }
    uint64_t seq_num() const { return seq_num_; }

protected:
    // One publishing thread (including synchronous reentry) per transmitter.
    // Topology operations do not access seq_num_ or msg_info_.
    //帧号
    uint64_t seq_num_;
    //帧附加数据
    MessageInfo msg_info_;
};


/* 构造 Transmitter 需要先构造它的父类 Endpoint */
template <typename M>
Transmitter<M>::Transmitter(const RoleAttributes& attr) 
        :Endpoint(attr), seq_num_(0)
{
    msg_info_.set_sender_id(this->id_);
    msg_info_.set_seq_num(this->seq_num_);
}

template <typename M>
Transmitter<M>::~Transmitter() {}


template <typename M>
bool Transmitter<M>::Transmit(const MessagePtr& msg){

    MessageInfo msg_info = msg_info_;
    msg_info.set_seq_num(NextSeqNum());

    PerfEventCache::Instance()->AddTransportEvent(TransPerf::TRANSMIT_BEGIN, attr_.channel_id ,msg_info.seq_num());
    
    return Transmit(msg, msg_info);

}

template <typename M>
bool Transmitter<M>::TransmitLoanedMessage(
    std::unique_ptr<LoanedMessage> message) {
    if(message == nullptr) {
        return false;
    }

    MessageInfo msg_info = msg_info_;
    msg_info.set_seq_num(NextSeqNum());
    PerfEventCache::Instance()->AddTransportEvent(
        TransPerf::TRANSMIT_BEGIN, attr_.channel_id, msg_info.seq_num());
    return TransmitLoanedMessage(std::move(message), msg_info);
}

template <typename M>
void Transmitter<M>::Enable(const RoleAttributes& opposite_attr)
{
    (void)opposite_attr;
    Enable();
}

template <typename M>
void Transmitter<M>::Disable(const RoleAttributes& opposite_attr) {
  (void)opposite_attr;
  Disable();
}
 

}
}
}




#endif
