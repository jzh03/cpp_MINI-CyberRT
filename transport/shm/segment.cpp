
#include <cmw/transport/shm/segment.h>
#include <cmw/common/log.h>
#include <iostream>
namespace hnu{
namespace cmw{
namespace transport{

namespace {

struct SegmentLayoutHeader {
    uint64_t magic;
    uint32_t version;
    uint32_t state_size;
    uint32_t block_size;
    uint32_t reserved;
};

const uint64_t kSegmentLayoutMagic = 0x434d5753484d3541ULL;
const uint32_t kSegmentLayoutVersion = 1;

SegmentLayoutHeader* GetLayoutHeader(void* managed_shm, const ShmConf& conf)
{
    return reinterpret_cast<SegmentLayoutHeader*>(
        static_cast<char*>(managed_shm) + conf.managed_shm_size() -
        sizeof(SegmentLayoutHeader));
}

const SegmentLayoutHeader* GetLayoutHeader(const void* managed_shm,
                                           const ShmConf& conf)
{
    return reinterpret_cast<const SegmentLayoutHeader*>(
        static_cast<const char*>(managed_shm) + conf.managed_shm_size() -
        sizeof(SegmentLayoutHeader));
}

}  // namespace

Segment::Segment(uint64_t channel_id) 
    : init_(false),
      conf_(),
      channel_id_(channel_id),
      state_(nullptr),
      blocks_(nullptr),
      managed_shm_(nullptr),
      block_buf_lock_(),
      block_buf_addrs_() {}

bool Segment::AcquireBlockToWrite(std::size_t msg_size, WritableBlock* writable_block){
    RETURN_VAL_IF_NULL(writable_block ,false);
    *writable_block = WritableBlock();
    if(msg_size > conf_.max_message_size()){
        AERROR << "msg_size: " << msg_size
               << " larger than max shm message size: "
               << conf_.max_message_size() << ".";
        return false;
    }
    if(!init_ && !OpenOrCreate()){
        AERROR << "create shm failed, can't write now.";
        return false;
    }

    bool result = true;
    if(state_->need_remap()){
        result = Remap();
    }

    //如果msg_size 超过默认的 size，则销毁之前创建的共享内存，重新创建一块更大的内存
    if(result && msg_size > conf_.ceiling_msg_size()){
       AERROR<< "msg_size: " << msg_size
                << " larger than current shm_buffer_size: "
                << conf_.ceiling_msg_size() << " , need recreate.";
                result = Recreate(msg_size);
    }

    if(!result){
        AERROR << "segment update failed.";
        return false;
    }

    if(msg_size > conf_.ceiling_msg_size()){
        AERROR << "msg_size: " << msg_size
               << " larger than updated shm_buffer_size: "
               << conf_.ceiling_msg_size() << ".";
        return false;
    }

    uint32_t index = GetNextWritableBlockIndex();
    if(index == UINT32_MAX){
        AERROR << "all blocks are busy.";
        return false;
    }
    //将writable_block 指向 blocks_[index]
    writable_block->index = index;
    writable_block->generation = blocks_[index].generation();
    writable_block->block = &blocks_[index];
    writable_block->buf = block_buf_addrs_[index];
    return true;
}


void Segment::ReleaseWrittenBlock(const WritableBlock& writable_block){
    auto index = writable_block.index;
    if( index >= conf_.block_num()){
        return;
    }
    //释放写锁
    blocks_[index].ReleaseWriteLock();
}

bool Segment::AcquireBlockToRead(ReadableBlock* readable_block){
    RETURN_VAL_IF_NULL(readable_block, false);
    readable_block->block = nullptr;
    readable_block->buf = nullptr;
    if(!init_ && !OpenOnly()){
        AERROR << "failed to open shared memory, can't read now." ;
        return false;       
    }

    bool result = true;
    if( state_->need_remap() ){
        result = Remap();
    }

    if(!result){
        AERROR << "segment update failed." ;
        return false;
    }

    auto index = readable_block->index;
    if(index >= conf_.block_num()){
        AERROR << "invalid block_index[" << index << "].";
        return false;
    }

    if(!blocks_[index].TryLockForRead()){
        return false;
    }
    readable_block->block = blocks_ + index;
    readable_block->buf = block_buf_addrs_[index];
    return true;
}

//释放Block的读锁
void Segment::ReleaseReadBlock(const ReadableBlock& readable_block){
    auto index = readable_block.index;
    if(index >= conf_.block_num()){
        return;
    }
    blocks_[index].ReleaseReadLock();
}

bool Segment::Destroy(){
    if(!init_){
        return true;
    }
    init_ = false;

    try
    {
        state_->DecreaseReferenceCounts();
        uint32_t reference_counts = state_->reference_counts();
        if(reference_counts == 0){
            return Remove();
        }
    }
    catch(...)
    {
        AERROR << "exception.";
        return false;
    }

    ADEBUG<< "destroy." ;
    return true;
    
}


bool Segment::Remap(){
    init_ = false;
    ADEBUG << "before reset.";
    Reset();
    ADEBUG<< "after reset.";
    return OpenOnly();
}

bool Segment::Recreate(const uint64_t& msg_size){
    init_ = false;
    state_->set_need_remap(true);
    Reset();
    Remove();
    conf_.Update(msg_size);
    return OpenOrCreate();
}

uint32_t Segment::GetNextWritableBlockIndex(){
    const auto block_num = conf_.block_num();
    uint32_t start_index = state_->FetchAddSeq(1) % block_num;
    for(uint32_t i = 0; i < block_num; ++i)
    {
        uint32_t try_idx = (start_index + i) % block_num;
        //ADEBUG << "try_idx: " << try_idx;
        //为blocks_[try_idx] 这块内存加上写锁
        if(blocks_[try_idx].TryLockForWrite()){
            blocks_[try_idx].IncreaseGeneration();
            return try_idx;
        }
    }
    return UINT32_MAX;
}

bool Segment::InitializeLayout()
{
    if(managed_shm_ == nullptr ||
       conf_.managed_shm_size() < sizeof(SegmentLayoutHeader)){
        return false;
    }

    SegmentLayoutHeader* header = GetLayoutHeader(managed_shm_, conf_);
    header->magic = kSegmentLayoutMagic;
    header->version = kSegmentLayoutVersion;
    header->state_size = sizeof(State);
    header->block_size = sizeof(Block);
    header->reserved = 0;
    return true;
}

bool Segment::HasValidLayout() const
{
    if(managed_shm_ == nullptr ||
       conf_.managed_shm_size() < sizeof(SegmentLayoutHeader)){
        return false;
    }

    const SegmentLayoutHeader* header = GetLayoutHeader(managed_shm_, conf_);
    return header->magic == kSegmentLayoutMagic &&
           header->version == kSegmentLayoutVersion &&
           header->state_size == sizeof(State) &&
           header->block_size == sizeof(Block);
}

WritableBlockLease::WritableBlockLease(const SegmentPtr& segment,
                                       const WritableBlock& block)
    : segment_(segment), block_(block),
      owns_block_(segment_ != nullptr && block_.block != nullptr) {}

WritableBlockLease::~WritableBlockLease()
{
    Release();
}

WritableBlockLease::WritableBlockLease(WritableBlockLease&& other) noexcept
    : segment_(std::move(other.segment_)), block_(other.block_),
      owns_block_(other.owns_block_)
{
    other.block_ = WritableBlock();
    other.owns_block_ = false;
}

WritableBlockLease& WritableBlockLease::operator=(WritableBlockLease&& other) noexcept
{
    if(this != &other){
        Release();
        segment_ = std::move(other.segment_);
        block_ = other.block_;
        owns_block_ = other.owns_block_;
        other.block_ = WritableBlock();
        other.owns_block_ = false;
    }
    return *this;
}

void WritableBlockLease::Release()
{
    if(!owns_block_){
        return;
    }
    segment_->ReleaseWrittenBlock(block_);
    owns_block_ = false;
    block_ = WritableBlock();
    segment_.reset();
}

ReadableBlockLease::ReadableBlockLease(const SegmentPtr& segment,
                                       const ReadableBlock& block)
    : segment_(segment), block_(block),
      owns_block_(segment_ != nullptr && block_.block != nullptr) {}

ReadableBlockLease::~ReadableBlockLease()
{
    Release();
}

ReadableBlockLease::ReadableBlockLease(ReadableBlockLease&& other) noexcept
    : segment_(std::move(other.segment_)), block_(other.block_),
      owns_block_(other.owns_block_)
{
    other.block_ = ReadableBlock();
    other.owns_block_ = false;
}

ReadableBlockLease& ReadableBlockLease::operator=(ReadableBlockLease&& other) noexcept
{
    if(this != &other){
        Release();
        segment_ = std::move(other.segment_);
        block_ = other.block_;
        owns_block_ = other.owns_block_;
        other.block_ = ReadableBlock();
        other.owns_block_ = false;
    }
    return *this;
}

void ReadableBlockLease::Release()
{
    if(!owns_block_){
        return;
    }
    segment_->ReleaseReadBlock(block_);
    owns_block_ = false;
    block_ = ReadableBlock();
    segment_.reset();
}

}
}
}
