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

    struct Indicator{
        Indicator() : next_seq(1) {
            for(uint32_t i = 0; i < kBufLength; ++i){
                seqs[i].store(0, std::memory_order_relaxed);
            }
        }

        std::atomic<uint64_t> next_seq;
        ReadableInfo infos[kBufLength];
        std::atomic<uint64_t> seqs[kBufLength];
    };

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
        bool Init();         
        bool OpenOrCreate();  //创建共享内存
        bool OpenOnly();      //打开共享内存
        bool Remove();        //移除共享内存
        void Reset();         //重置共享内存

        key_t key_ = 0; //标识IPC资源
        void* managed_shm_ = nullptr;
        size_t shm_size_ = 0;
        Indicator* indicator_ = nullptr;
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
