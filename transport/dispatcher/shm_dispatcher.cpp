#include <cmw/transport/dispatcher/shm_dispatcher.h>
#include <cmw/common/global_data.h>
#include <cmw/common/util.h>
#include <cmw/transport/shm/notifier_factory.h>
#include <cmw/transport/shm/segment_factory.h>

namespace hnu    {
namespace cmw   {
namespace transport {


using common::GlobalData;

ShmDispatcher::ShmDispatcher() : host_id_(0)  { Init(); }
ShmDispatcher::~ShmDispatcher() { Shutdown(); }

bool ShmDispatcher::Init(){
    host_id_ = common::Hash(GlobalData::Instance()->HostIp());
    notifier_ = NotifierFactory::CreateNotifier();
    thread_ = std::thread(&ShmDispatcher::ThreadFunc, this);

    return true;
}

void ShmDispatcher::OnMessage(uint64_t channel_id,
                              const std::shared_ptr<ReadableBlock>& rb,
                              const MessageInfo& msg_info){
    if(is_shutdown_.load()){
        return;
    }

    ListenerHandlerBasePtr* handler_base = nullptr;

    //根据channel_id 拿到对应的ListenerHandler
    if (msg_listeners_.Get(channel_id, &handler_base)){
        auto handler = std::dynamic_pointer_cast<ListenerHandler<ReadableBlock>>(*handler_base);
        if(handler == nullptr) {
            AERROR << "shm listener type does not match serialized message.";
            return;
        }
        //执行回调
        handler->Run(rb, msg_info);
    } else {
    AERROR << "Cannot find " << GlobalData::GetChannelById(channel_id)
           << "'s handler.";
  }
}

void ShmDispatcher::AddLoanedListener(
    const RoleAttributes& self_attr,
    const MessageListener<LoanedMessage>& listener) {
    Dispatcher::AddListener<LoanedMessage>(self_attr, listener);
    AddSegment(self_attr);
}

void ShmDispatcher::AddLoanedListener(
    const RoleAttributes& self_attr, const RoleAttributes& opposite_attr,
    const MessageListener<LoanedMessage>& listener) {
    Dispatcher::AddListener<LoanedMessage>(self_attr, opposite_attr, listener);
    AddSegment(self_attr);
}


void ShmDispatcher::AddSegment(const RoleAttributes& self_attr){
    uint64_t channel_id = self_attr.channel_id;
    WriteLockGuard<AtomicRWLock> lock(segments_lock_);
    //如果segments_存在channel_id对应的segment，直接返回
    if(segments_.count(channel_id) > 0){
        return;
    }
    //创建一个segment
    auto segment = SegmentFactory::CreateSegment(channel_id);
    //保存在segments_中
    segments_[channel_id] = segment;

    previous_indexs_[channel_id] = UINT32_MAX;
}

void ShmDispatcher::ReadMessage(uint64_t channel_id, uint32_t block_index,
                                uint64_t generation){
      ADEBUG << "Reading sharedmem message: "
         << GlobalData::GetChannelById(channel_id)
         << " from block: " << block_index;
      auto rb = std::make_shared<ReadableBlock>();
      rb->index = block_index;
      //读取共享内存保存到rb中
      SegmentPtr segment = segments_[channel_id];
      if( !segment->AcquireBlockToRead(rb.get())){
        AWARN << "fail to acquire block, channel: "
          << GlobalData::GetChannelById(channel_id)
          << " index: " << block_index;
        return;
      }
    ReadableBlockLease read_lease(segment, *rb);

    if(rb->block->generation() != generation){
        ADEBUG << "stale shm block generation, channel: "
               << GlobalData::GetChannelById(channel_id)
               << " index: " << block_index;
        return;
    }

    if(rb->block->msg_size() > segment->payload_capacity()) {
        AERROR << "invalid shm payload size.";
        return;
    }

    if(rb->block->msg_info_size() < ID_SIZE * 2 + sizeof(uint64_t) ||
       rb->block->msg_info_size() > segment->message_info_capacity()){
        AERROR << "invalid shm message info size.";
        return;
    }

    MessageInfo msg_info;
    const char* msg_info_addr = 
                    reinterpret_cast<char*>(rb->buf) + rb->block->msg_size();
    // 通过 Identity::set_data 重建 sender_id，同时刷新逐 peer 路由使用的哈希值。
    Identity sender_id(false);
    sender_id.set_data(msg_info_addr);
    msg_info.set_sender_id(sender_id);
    // spare_id 同样需要重建对象，避免只覆盖字节而保留旧哈希。
    Identity spare_id(false);
    spare_id.set_data(msg_info_addr + ID_SIZE);
    msg_info.set_spare_id(spare_id);
    //拷贝 seq
    msg_info.set_seq_num(*(reinterpret_cast<uint64_t*>(const_cast<char*>(msg_info_addr+2*ID_SIZE))));

    if(segment->message_type() == ShmMessageType::LOANED) {
        ListenerHandlerBasePtr* handler_base = nullptr;
        if(!msg_listeners_.Get(channel_id, &handler_base)) {
            AERROR << "Cannot find " << GlobalData::GetChannelById(channel_id)
                   << "'s loaned handler.";
            return;
        }
        auto handler = std::dynamic_pointer_cast<ListenerHandler<LoanedMessage>>(
            *handler_base);
        if(handler == nullptr) {
            AERROR << "shm listener type does not match loaned message.";
            return;
        }

        auto message = std::make_shared<LoanedMessage>(
            rb->buf, rb->block->msg_size(), segment->payload_capacity(),
            std::move(read_lease), channel_id, rb->index, generation);
        handler->Run(message, msg_info);
        return;
    }

    OnMessage(channel_id,rb,msg_info);
}

void ShmDispatcher::ThreadFunc(){
    ReadableInfo readable_info;
    while (!is_shutdown_.load())
    {
        if(!notifier_->Listen(100, &readable_info)){
            //ADEBUG << "listen failed.";
            continue;
        }

        if(readable_info.host_id() != host_id_){

            ADEBUG << "shm readable info from other host." << host_id_ << " " << readable_info.host_id();
            continue;
        }

        uint64_t channel_id  = readable_info.channel_id();
        uint32_t block_index = readable_info.block_index();
        uint64_t generation = readable_info.generation();

        {
            ReadLockGuard<AtomicRWLock> lg(segments_lock_);
            //先判断当前进程的segments_是否包含channel_id对应的segments；
            if(segments_.count(channel_id) == 0){
                continue;
            }
            //保存之前的index
            if(previous_indexs_.count(channel_id) == 0){
                previous_indexs_[channel_id] == UINT32_MAX;
            }
            //拿到上一次的索引
            uint32_t& previous_index = previous_indexs_[channel_id];
            if(block_index != 0 && previous_index != UINT32_MAX){
                if (block_index == previous_index) {
                    ADEBUG << "Receive SAME index " << block_index << " of channel "
                            << channel_id;
                    } else if (block_index < previous_index) {
                    ADEBUG << "Receive PREVIOUS message. last: " << previous_index
                            << ", now: " << block_index;
                    } else if (block_index - previous_index > 1) {
                    ADEBUG << "Receive JUMP message. last: " << previous_index
                            << ", now: " << block_index;
                    }
            } 
            //更新上一次的索引
            previous_index = block_index;
            ReadMessage(channel_id, block_index, generation);
        }
    }
    
}


void ShmDispatcher::Shutdown(){
    if (is_shutdown_.exchange(true)) {
        return;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    {
        ReadLockGuard<AtomicRWLock> lock(segments_lock_);
        segments_.clear();
    }
}



}
}
}
