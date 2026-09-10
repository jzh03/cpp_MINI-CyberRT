#ifndef CMW_TRANSPORT_SHM_READABLE_INFO_H_
#define CMW_TRANSPORT_SHM_READABLE_INFO_H_


#include <type_traits>
#include <stdint.h>
#include <string>
namespace hnu{
namespace cmw{
namespace transport{

class ReadableInfo{

public:
    ReadableInfo();
    ReadableInfo(uint64_t host_id , uint32_t block_index , uint64_t channel_id);
    ReadableInfo(uint64_t host_id , uint32_t block_index , uint64_t channel_id,
                 uint64_t generation);
    ~ReadableInfo() = default;

    ReadableInfo& operator=(const ReadableInfo& other);

    bool DeserializeFrom(const std::string& src);
    bool DeserializeFrom(const char* src , std::size_t len);
    bool SerializeTo(std::string* dst) const;

    uint64_t host_id() const { return host_id_; }
    void ser_host_id(uint64_t host_id) { host_id_ = host_id; }

    uint32_t block_index() const { return block_index_; }
    void set_block_index(uint32_t block_index) { block_index_ = block_index; }

    uint64_t channel_id() const { return channel_id_; }
    void set_channel_id(uint64_t channel_id) { channel_id_ = channel_id; }

    uint64_t generation() const { return generation_; }
    void set_generation(uint64_t generation) { generation_ = generation; }

    static const size_t ksize;
private:
    // Indicator stores this type in System V shared memory.  Keep an explicit
    // leading word so its layout remains compatible with the former vptr slot,
    // without storing a process-local vtable address in shared memory.
    uintptr_t reserved_ = 0;
    uint64_t host_id_;
    uint32_t block_index_;
    uint64_t channel_id_;
    uint64_t generation_;
    
};

static_assert(!std::is_polymorphic<ReadableInfo>::value,
              "Shared memory objects must not contain a vptr.");




}
}
}


#endif
