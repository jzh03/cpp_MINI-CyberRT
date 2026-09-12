#include <cmw/transport/shm/condition_notifier.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <thread>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <new>
#include <cmw/common/util.h>
#include <cmw/common/log.h>
namespace hnu{
namespace cmw{
namespace transport{

using common::Hash;

namespace {
constexpr uint64_t kNotifierMagic = 0x434d574e4f544631ULL;
constexpr uint32_t kNotifierVersion = 1;  // Independent of Payload Segment v2.

class TryLock {
public:
    explicit TryLock(std::atomic<uint32_t>& lock) : lock_(lock) {
        uint32_t expected = 0;
        owns_ = lock_.compare_exchange_strong(expected, 1,
                    std::memory_order_acquire, std::memory_order_relaxed);
    }
    ~TryLock() {
        if(owns_) lock_.store(0, std::memory_order_release);
    }
    explicit operator bool() const { return owns_; }
    TryLock(const TryLock&) = delete;
    TryLock& operator=(const TryLock&) = delete;
private:
    std::atomic<uint32_t>& lock_;
    bool owns_;
};
}  // namespace

ConditionNotifier::Indicator::Indicator()
    : version(kNotifierVersion), indicator_size(sizeof(Indicator)),
      slot_size(sizeof(Slot)), info_size(sizeof(ReadableInfo)),
      slot_align(alignof(Slot)), info_align(alignof(ReadableInfo)),
      capacity(kBufLength), slots_offset(offsetof(Indicator, slots)) {
    __atomic_store_n(&magic, kNotifierMagic, __ATOMIC_RELEASE);
}

ConditionNotifier::ConditionNotifier()
    : ConditionNotifier(
          static_cast<key_t>(Hash("/hnu/cmw/transport/shm/notifier")), false)
{}

ConditionNotifier::ConditionNotifier(key_t key, bool remove_on_shutdown)
    : key_(key), remove_on_shutdown_(remove_on_shutdown){
    shm_size_ = sizeof(Indicator);

    if(!Init()){
        AERROR << "fail to init condition notifier." ;
        is_shutdown_.store(true);
        return;
    }

    next_seq_ = indicator_->next_seq.load(std::memory_order_acquire);
    ADEBUG << "next_seq: " << next_seq_;
}

ConditionNotifier::~ConditionNotifier() {Shutdown();}

void ConditionNotifier::Shutdown( ){
    if(is_shutdown_.exchange(true)){
        return;
    }
    Reset();
    if(remove_on_shutdown_ && created_){
        Remove();
        created_ = false;
    }
}


bool ConditionNotifier::Notify(const ReadableInfo& info){
    if(is_shutdown_.load()){
        ADEBUG << "notifier is shutdown.";
        return false;
    }
    TryLock publish(indicator_->publish_lock);
    return publish && PublishLocked(info);
}

bool ConditionNotifier::PublishLocked(const ReadableInfo& info) {
    const uint64_t seq = indicator_->next_seq.load(std::memory_order_relaxed);
    // Do not wrap the sequence counter (zero also denotes an empty slot).
    if(seq == std::numeric_limits<uint64_t>::max()) return false;
    Slot& slot = indicator_->slots[seq % kBufLength];
    TryLock slot_guard(slot.lock);
    if(!slot_guard) return false;
    slot.info = info;
    slot.seq = seq;
    indicator_->next_seq.store(seq + 1, std::memory_order_release);
    return true;
}

bool ConditionNotifier::Listen(int timeout_ms, ReadableInfo* info) {
    if(info == nullptr || is_shutdown_.load()) return false;
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() +
        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
    // A zero/negative timeout still performs one nonblocking read attempt.
    bool first = true;
    while(!is_shutdown_.load() && (first || Clock::now() < deadline)) {
        first = false;
        const uint64_t end = indicator_->next_seq.load(std::memory_order_acquire);
        const uint64_t oldest = end > kBufLength ? end - kBufLength : 1;
        if(next_seq_ < oldest) next_seq_ = oldest;
        if(next_seq_ < end) {
            Slot& slot = indicator_->slots[next_seq_ % kBufLength];
            TryLock slot_guard(slot.lock);
            if(slot_guard && slot.seq == next_seq_) {
                *info = slot.info;
                ++next_seq_;
                return true;
            }
            // A busy slot or a concurrent wrap is retried using a fresh end.
            // Never adopt this slot's newer sequence: that could skip older
            // notifications still retained elsewhere in the ring.
        }
        const auto now = Clock::now();
        if(now >= deadline) return false;
        const auto pause = std::chrono::duration_cast<Clock::duration>(
            std::chrono::microseconds(50));
        std::this_thread::sleep_until(now + std::min(pause, deadline - now));
    }
    return false;
}


bool ConditionNotifier::Init() { return OpenOrCreate(); }


bool ConditionNotifier::OpenOrCreate(){
    int shmid = shmget(key_, shm_size_, 0644 | IPC_CREAT | IPC_EXCL);
    if(shmid == -1 && EEXIST == errno){
        ADEBUG << "shm already exist, open only.";
        return OpenOnly();
    }

    if(shmid == -1){
            AERROR << "create shm failed, error code: " << strerror(errno);
            return false;
    }

    managed_shm_ = shmat(shmid, nullptr ,0);

    if(managed_shm_ == reinterpret_cast<void*>(-1)){
        AERROR << "attach shm failed.";
        managed_shm_ = nullptr;
        shmctl(shmid, IPC_RMID, 0);
        return false;
    }

    indicator_ = new (managed_shm_) Indicator();

    if(indicator_ == nullptr){
        AERROR << "create indicator failed." ;
        shmdt(managed_shm_);
        managed_shm_ = nullptr;
        shmctl(shmid, IPC_RMID, 0);
        return false;
    }

    created_ = true;

    ADEBUG  << "open or create true.";
    return true;
    
}

bool ConditionNotifier::OpenOnly(){
    
    int shmid = shmget(key_, 0 , 0644);
    if(shmid == -1){
        AERROR << "get shm failed, error: " << strerror(errno);
        return false; 
    }

    struct shmid_ds shm_info;
    if(shmctl(shmid, IPC_STAT, &shm_info) == -1){
        AERROR << "get notifier shm size failed, error: " << strerror(errno);
        return false;
    }
    if(shm_info.shm_segsz != shm_size_){
        AERROR << "incompatible notifier shm layout.";
        return false;
    }

    managed_shm_ = shmat(shmid, nullptr, 0);
    if(managed_shm_ == reinterpret_cast<void*>(-1)) {
        managed_shm_ = nullptr;
        AERROR << "attach notifier shm failed.";
        return false;  // Never delete a segment owned by another process.
    }

    indicator_ = reinterpret_cast<Indicator*>(managed_shm_);
    // A concurrently creating process publishes magic last. A dead creator or
    // an unmarked old layout must not leave an opener waiting indefinitely.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(100);
    uint64_t magic;
    while((magic = __atomic_load_n(&indicator_->magic, __ATOMIC_ACQUIRE)) == 0 &&
          std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    if(magic != kNotifierMagic || indicator_->version != kNotifierVersion ||
       indicator_->indicator_size != sizeof(Indicator) ||
       indicator_->slot_size != sizeof(Slot) ||
       indicator_->info_size != sizeof(ReadableInfo) ||
       indicator_->slot_align != alignof(Slot) ||
       indicator_->info_align != alignof(ReadableInfo) ||
       indicator_->capacity != kBufLength ||
       indicator_->slots_offset != offsetof(Indicator, slots)) {
        AERROR << "incompatible or uninitialized notifier shm layout.";
        Reset();
        return false;
    }

    ADEBUG << "Open true" ;

    return true;


}


//删除共享内存
bool ConditionNotifier::Remove(){
    int shmid = shmget(key_, 0 , 0644);
    if(shmid == -1 || shmctl(shmid, IPC_RMID,0) == -1){
        AERROR<< "remove shm failed, error code: " << strerror(errno) ;
        return false;
    }

    ADEBUG<< "remove success.";
    return true;
}

//shm detach 断开与共享内存的连接
void ConditionNotifier::Reset(){
    indicator_ = nullptr;
    if(managed_shm_ != nullptr){
        shmdt(managed_shm_);
        managed_shm_ = nullptr;
    }
}


}
}
}
