#ifndef CMW_TRANSPORT_SHM_STATE_H_
#define CMW_TRANSPORT_SHM_STATE_H_


#include <type_traits>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
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

    // An opener may map a segment before the last current owner starts
    // removing its name.  It becomes an owner only if this CAS succeeds.
    bool TryAcquireReference() {
        uint32_t count = reference_count_.load(std::memory_order_acquire);
        while(count != kClosingReferenceCount && count != 0 &&
              count < kClosingReferenceCount - 1) {
            if(reference_count_.compare_exchange_weak(
                   count, count + 1, std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }

    // Returns true only to the owner that changes the last reference into the
    // closing sentinel.  That owner alone is allowed to remove the OS object.
    bool ReleaseReference() {
        uint32_t count = reference_count_.load(std::memory_order_acquire);
        while(count != kClosingReferenceCount && count != 0) {
            const uint32_t next =
                count == 1 ? kClosingReferenceCount : count - 1;
            if(reference_count_.compare_exchange_weak(
                   count, next, std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
                return next == kClosingReferenceCount;
            }
        }
        return false;
    }

    uint32_t FetchAddSeq(uint32_t diff) { return seq_.fetch_add(diff); }

    uint32_t seq() { return seq_.load(); }

    void set_need_remap(bool need) { need_remap_.store(need);}

    bool need_remap() { return need_remap_;}
    
    uint64_t ceiling_msg_size() { return ceiling_msg_size_.load(); }
    uint32_t reference_counts() const {
        const uint32_t count = reference_count_.load(std::memory_order_acquire);
        return count == kClosingReferenceCount ? 0 : count;
    }

    bool is_closing() const {
        return reference_count_.load(std::memory_order_acquire) ==
               kClosingReferenceCount;
    }

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
    static constexpr uint32_t kClosingReferenceCount =
        std::numeric_limits<uint32_t>::max();
    std::atomic<bool> need_remap_ = {false};
    std::atomic<uint32_t> seq_ = {0};
    // Construction creates the first owner.  Zero is never an attachable
    // state, and UINT32_MAX permanently seals this segment incarnation.
    std::atomic<uint32_t> reference_count_;
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
