/**
 * @File Name: condition_notifier.h
 * @brief  基于共享内存的进程间通知方式
 * @Author : Timer email:330070781@qq.com
 * @Version : 1.0
 * @Creat Date : 2024-02-06
 * 
 */
#ifndef CMW_TRANSPORT_SHM_CONDITION_NOTIFIER_H_
#define CMW_TRANSPORT_SHM_CONDITION_NOTIFIER_H_

#include <cmw/transport/shm/notifier_base.h>
#include <atomic>
#include <sys/ipc.h>
#include <cmw/common/macros.h>

namespace hnu{
namespace cmw{
namespace transport{

const uint32_t kBufLength = 4096;
class ConditionNotifier : public NotifierBase{

    // Linux shared-memory ABI: these atomics must use hardware operations,
    // never a process-local libatomic fallback lock.
    static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr),
                  "Notifier requires lock-free 32-bit atomics");
    static_assert(__atomic_always_lock_free(sizeof(uint64_t), nullptr),
                  "Notifier requires lock-free 64-bit atomics");

    struct Slot {
        std::atomic<uint32_t> lock{0};
        uint64_t seq = 0;  // Protected by lock, together with info.
        ReadableInfo info;
    };

    struct Indicator {
        // SysV zero-fills new segments. Leave magic untouched until all other
        // members are constructed; access it only through __atomic operations.
        uint64_t magic;
        uint32_t version;
        uint32_t indicator_size;
        uint32_t slot_size;
        uint32_t info_size;
        uint32_t slot_align;
        uint32_t info_align;
        uint32_t capacity;
        uint32_t slots_offset;
        std::atomic<uint32_t> publish_lock{0};
        std::atomic<uint64_t> next_seq{1};
        Slot slots[kBufLength];
        Indicator();
    };

    static_assert(std::is_standard_layout<Indicator>::value,
                  "Notifier metadata requires a standard-layout Indicator");
    static_assert(!std::is_polymorphic<Indicator>::value,
                  "Shared memory Indicator must not contain a vptr.");

    public:
        ConditionNotifier(key_t key, bool remove_on_shutdown);
        virtual ~ConditionNotifier();
        void Shutdown() override;
        bool Notify(const ReadableInfo& info) override;
        bool Listen(int timeout_ms , ReadableInfo* info) override;
        static const char* Type() { return "contion"; }
    private:
        friend class ConditionNotifierTestPeer;
        // Caller owns publish_lock. Kept separate for deterministic pause tests.
        bool PublishLocked(const ReadableInfo& info);
        bool Init();         
        bool OpenOrCreate();  //创建共享内存
        bool OpenOnly();      //打开共享内存
        bool Remove();        //移除共享内存
        void Reset();         //重置共享内存

        key_t key_ = 0; //标识IPC资源
        void* managed_shm_ = nullptr;
        size_t shm_size_ = 0;
        Indicator* indicator_ = nullptr;
        // One Listen caller per instance; separate instances/processes broadcast.
        // Shutdown/destruction must not overlap Notify or Listen.
        uint64_t next_seq_ = 0;
        std::atomic<bool> is_shutdown_ = {false};
        bool remove_on_shutdown_ = false;
        bool created_ = false;
        DECLARE_SINGLETON(ConditionNotifier)
};

}
}
}

#endif
