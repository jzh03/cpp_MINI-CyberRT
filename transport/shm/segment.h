#ifndef CMW_TRANSPORT_SHM_SEGMENT_H_
#define CMW_TRANSPORT_SHM_SEGMENT_H_


#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <cmw/transport/shm/block.h>
#include <cmw/transport/shm/shm_conf.h>
#include <cmw/transport/shm/state.h>


namespace hnu{
namespace cmw{
namespace transport{



class Segment;
using SegmentPtr = std::shared_ptr<Segment>;

//可写的块内存结构体
struct WritableBlock
{
    uint32_t index = 0;
    uint64_t generation = 0;
    Block* block = nullptr;
    uint8_t* buf = nullptr;
};

using ReadableBlock = WritableBlock;

class Segment
{

public:
    explicit Segment(uint64_t channel_id, uint64_t initial_msg_size = 0);
    virtual ~Segment() {}

    bool AcquireBlockToWrite(std::size_t msg_size, WritableBlock* writable_block);
    bool AcquireBlockToWrite(std::size_t msg_size, ShmMessageType message_type,
                             WritableBlock* writable_block);
    bool AcquireBlockToWriteWithoutRecreate(
        std::size_t msg_size, ShmMessageType message_type,
        WritableBlock* writable_block);
    void ReleaseWrittenBlock(const WritableBlock& writable_block);

    bool AcquireBlockToRead(ReadableBlock* readable_block);
    void ReleaseReadBlock(const ReadableBlock& readable_block);

    uint64_t payload_capacity() const { return conf_.ceiling_msg_size(); }
    uint64_t message_info_capacity() const {
        return conf_.block_buf_size() - conf_.ceiling_msg_size();
    }
    uint64_t block_num() const { return conf_.block_num(); }
    ShmMessageType message_type() const {
        return state_ == nullptr ? ShmMessageType::UNKNOWN :
               state_->message_type();
    }

protected:
    virtual bool Destroy();
    virtual void Reset() = 0;
    virtual bool Remove() = 0;
    virtual bool OpenOnly() = 0;
    virtual bool OpenOrCreate() = 0;
    bool InitializeLayout();
    bool HasValidLayout(std::size_t mapped_size);
    bool init_;
    ShmConf conf_;
    uint64_t channel_id_;

    State* state_;
    Block* blocks_;
    void* managed_shm_;
    std::mutex block_buf_lock_;
    std::unordered_map<uint32_t, uint8_t*> block_buf_addrs_;

private:
    bool Remap();
    bool Recreate(const uint64_t& msg_size);
    uint32_t GetNextWritableBlockIndex();
};

class WritableBlockLease
{
public:
    WritableBlockLease() = default;
    WritableBlockLease(const SegmentPtr& segment, const WritableBlock& block);
    ~WritableBlockLease();

    WritableBlockLease(const WritableBlockLease&) = delete;
    WritableBlockLease& operator=(const WritableBlockLease&) = delete;
    WritableBlockLease(WritableBlockLease&& other) noexcept;
    WritableBlockLease& operator=(WritableBlockLease&& other) noexcept;

    explicit operator bool() const { return owns_block_; }
    const WritableBlock& block() const { return block_; }
    void Release();

private:
    SegmentPtr segment_;
    WritableBlock block_;
    bool owns_block_ = false;
};

class ReadableBlockLease
{
public:
    ReadableBlockLease() = default;
    ReadableBlockLease(const SegmentPtr& segment, const ReadableBlock& block);
    ~ReadableBlockLease();

    ReadableBlockLease(const ReadableBlockLease&) = delete;
    ReadableBlockLease& operator=(const ReadableBlockLease&) = delete;
    ReadableBlockLease(ReadableBlockLease&& other) noexcept;
    ReadableBlockLease& operator=(ReadableBlockLease&& other) noexcept;

    explicit operator bool() const { return owns_block_; }
    const ReadableBlock& block() const { return block_; }
    void Release();

private:
    SegmentPtr segment_;
    ReadableBlock block_;
    bool owns_block_ = false;
};






}
}
}
#endif
