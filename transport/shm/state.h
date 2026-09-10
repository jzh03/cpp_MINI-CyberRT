#ifndef CMW_TRANSPORT_SHM_STATE_H_
#define CMW_TRANSPORT_SHM_STATE_H_


#include <type_traits>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace hnu{
namespace cmw{
namespace transport{

enum class ShmMessageType : uint8_t {
    UNKNOWN = 0,
    SERIALIZED = 1,
    LOANED = 2,
};

class State
{

public:
    explicit State(const uint64_t& ceiling_msg_size);
    ~State();

    //为啥不直接 fetch_sub 而要使用 cas 操作，想了一下应该是确保reference_count_的值不能小于0
    void DecreaseReferenceCounts() {
        uint32_t  current_reference_count = reference_count_.load();
        do{
            if(current_reference_count == 0)
            {
                return;
            }
        } while (!reference_count_.compare_exchange_strong(current_reference_count
             , current_reference_count - 1));
 
        
    }

    //增加引用计数
    void IncreaseReferenceCounts() { reference_count_.fetch_add(1); }

    uint32_t FetchAddSeq(uint32_t diff) { return seq_.fetch_add(diff); }

    uint32_t seq() { return seq_.load(); }

    void set_need_remap(bool need) { need_remap_.store(need);}

    bool need_remap() { return need_remap_;}
    
    uint64_t ceiling_msg_size() { return ceiling_msg_size_.load(); }
    //返回引用计数
    uint32_t reference_counts() { return reference_count_.load(); }

    bool TrySetMessageType(ShmMessageType message_type) {
        if(message_type == ShmMessageType::UNKNOWN) {
            return true;
        }
        uint8_t expected = static_cast<uint8_t>(ShmMessageType::UNKNOWN);
        return message_type_.compare_exchange_strong(
                   expected, static_cast<uint8_t>(message_type),
                   std::memory_order_acq_rel, std::memory_order_acquire) ||
               expected == static_cast<uint8_t>(message_type);
    }

    ShmMessageType message_type() const {
        return static_cast<ShmMessageType>(
            message_type_.load(std::memory_order_acquire));
    }

    
private:
    std::atomic<bool> need_remap_ = {false};
    std::atomic<uint32_t> seq_ = {0};
    std::atomic<uint32_t> reference_count_ = {0};
    std::atomic<uint64_t> ceiling_msg_size_;
    std::atomic<uint8_t> message_type_ = {
        static_cast<uint8_t>(ShmMessageType::UNKNOWN)};

};

static_assert(!std::is_polymorphic<State>::value,
              "Shared memory objects must not contain a vptr.");




}
}
}


#endif
