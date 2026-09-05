#ifndef CMW_TRANSPORT_MESSAGE_LOANED_MESSAGE_H_
#define CMW_TRANSPORT_MESSAGE_LOANED_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <cmw/transport/shm/segment.h>

namespace hnu {
namespace cmw {
namespace transport {

template <typename M>
class ShmTransmitter;
template <typename M>
class HybridTransmitter;
template <typename M>
class IntraTransmitter;
template <typename M>
class RtpsTransmitter;

// A process-local view of a continuous SHM payload.  Only its payload bytes
// and Block metadata live in shared memory; the leases remain local.
class LoanedMessage {
 public:
  enum class StorageMode {
    SHM_BACKED,
    HEAP_BACKED,
  };

  enum class State {
    WRITABLE,
    READ_ONLY,
    PUBLISHED,
  };

  LoanedMessage(uint8_t* data, std::size_t capacity,
                WritableBlockLease&& write_lease, uint64_t channel_id,
                const void* owner)
      : data_(data),
        size_(0),
        capacity_(capacity),
        channel_id_(channel_id),
        block_index_(write_lease.block().index),
        generation_(write_lease.block().generation),
        owner_(owner),
        storage_mode_(StorageMode::SHM_BACKED),
        state_(State::WRITABLE),
        write_lease_(std::move(write_lease)) {}

  LoanedMessage(const uint8_t* data, std::size_t size, std::size_t capacity,
                ReadableBlockLease&& read_lease, uint64_t channel_id,
                uint32_t block_index, uint64_t generation)
      : data_(const_cast<uint8_t*>(data)),
        size_(size),
        capacity_(capacity),
        channel_id_(channel_id),
        block_index_(block_index),
        generation_(generation),
        owner_(nullptr),
        storage_mode_(StorageMode::SHM_BACKED),
        state_(State::READ_ONLY),
        read_lease_(std::move(read_lease)) {}

  // Heap storage is deliberately process-local. It is used by the INTRA,
  // RTPS, and mixed-transport compatibility path only.
  static std::unique_ptr<LoanedMessage> CreateHeap(uint64_t channel_id,
                                                    std::size_t capacity) {
    try {
      std::unique_ptr<uint8_t[]> data;
      if (capacity != 0) {
        data.reset(new uint8_t[capacity]);
      }
      return std::unique_ptr<LoanedMessage>(
          new LoanedMessage(std::move(data), capacity, channel_id));
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }

  ~LoanedMessage() = default;

  LoanedMessage(const LoanedMessage&) = delete;
  LoanedMessage& operator=(const LoanedMessage&) = delete;
  LoanedMessage(LoanedMessage&&) = delete;
  LoanedMessage& operator=(LoanedMessage&&) = delete;

  uint8_t* mutable_data() {
    return state_ == State::WRITABLE ? data_ : nullptr;
  }

  const uint8_t* data() const { return data_; }
  std::size_t size() const { return size_; }
  std::size_t capacity() const { return capacity_; }
  uint64_t channel_id() const { return channel_id_; }
  uint32_t block_index() const { return block_index_; }
  uint64_t generation() const { return generation_; }
  bool is_shm_backed() const { return storage_mode_ == StorageMode::SHM_BACKED; }
  bool is_heap_backed() const { return storage_mode_ == StorageMode::HEAP_BACKED; }
  bool is_read_only() const { return state_ != State::WRITABLE; }

  // The RTPS representation is deliberately independent of the local
  // storage/lease state: four big-endian length bytes followed by payload.
  static bool SerializePayload(const LoanedMessage& message,
                               std::string* serialized) {
    if (serialized == nullptr || message.state_ == State::WRITABLE ||
        message.size_ > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    const uint32_t size = static_cast<uint32_t>(message.size_);
    serialized->resize(kWireHeaderSize + message.size_);
    (*serialized)[0] = static_cast<char>((size >> 24) & 0xff);
    (*serialized)[1] = static_cast<char>((size >> 16) & 0xff);
    (*serialized)[2] = static_cast<char>((size >> 8) & 0xff);
    (*serialized)[3] = static_cast<char>(size & 0xff);
    if (message.size_ != 0) {
      std::memcpy(&(*serialized)[kWireHeaderSize], message.data_, message.size_);
    }
    return true;
  }

  static std::shared_ptr<LoanedMessage> DeserializePayload(
      const char* serialized, std::size_t serialized_size,
      std::size_t max_payload_size, uint64_t channel_id) {
    if (serialized == nullptr || serialized_size < kWireHeaderSize) {
      return nullptr;
    }
    const uint32_t payload_size =
        (static_cast<uint32_t>(static_cast<uint8_t>(serialized[0])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(serialized[1])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(serialized[2])) << 8) |
        static_cast<uint32_t>(static_cast<uint8_t>(serialized[3]));
    if (payload_size > max_payload_size ||
        payload_size != serialized_size - kWireHeaderSize) {
      return nullptr;
    }
    auto message = CreateHeap(channel_id, payload_size);
    if (message == nullptr) {
      return nullptr;
    }
    if (payload_size != 0) {
      std::memcpy(message->data_, serialized + kWireHeaderSize, payload_size);
    }
    message->size_ = payload_size;
    message->size_set_ = true;
    message->state_ = State::READ_ONLY;
    return std::shared_ptr<LoanedMessage>(message.release());
  }

  bool set_size(std::size_t size) {
    if(state_ != State::WRITABLE || size > capacity_) {
      return false;
    }
    size_ = size;
    size_set_ = true;
    return true;
  }

 private:
  template <typename M>
  friend class ShmTransmitter;
  template <typename M>
  friend class HybridTransmitter;
  template <typename M>
  friend class IntraTransmitter;
  template <typename M>
  friend class RtpsTransmitter;

  LoanedMessage(std::unique_ptr<uint8_t[]>&& heap_data,
                std::size_t capacity, uint64_t channel_id)
      : data_(heap_data.get()),
        size_(0),
        capacity_(capacity),
        channel_id_(channel_id),
        block_index_(0),
        generation_(0),
        owner_(nullptr),
        storage_mode_(StorageMode::HEAP_BACKED),
        state_(State::WRITABLE),
        heap_data_(std::move(heap_data)) {}

  static constexpr std::size_t kWireHeaderSize = sizeof(uint32_t);

  bool CanPublish() const {
    return state_ == State::WRITABLE && size_set_ && size_ <= capacity_;
  }

  bool BeginPublish(const void* owner) {
    if(!CanPublish() || !is_shm_backed() || owner_ != owner ||
       !write_lease_) {
      return false;
    }
    state_ = State::PUBLISHED;
    return true;
  }

  bool BeginHeapPublish() {
    if(!CanPublish() || !is_heap_backed()) {
      return false;
    }
    state_ = State::PUBLISHED;
    return true;
  }

  static std::shared_ptr<LoanedMessage> MakeHeapSnapshot(
      const LoanedMessage& message) {
    if(!message.CanPublish()) {
      return nullptr;
    }
    auto snapshot = CreateHeap(message.channel_id_, message.size_);
    if(snapshot == nullptr) {
      return nullptr;
    }
    if(message.size_ != 0) {
      std::memcpy(snapshot->data_, message.data_, message.size_);
    }
    snapshot->size_ = message.size_;
    snapshot->size_set_ = true;
    snapshot->state_ = State::PUBLISHED;
    return std::shared_ptr<LoanedMessage>(snapshot.release());
  }

  const WritableBlock& writable_block() const { return write_lease_.block(); }
  void ReleaseWritableLease() { write_lease_.Release(); }

  uint8_t* data_;
  std::size_t size_;
  std::size_t capacity_;
  uint64_t channel_id_;
  uint32_t block_index_;
  uint64_t generation_;
  const void* owner_;
  StorageMode storage_mode_;
  State state_;
  bool size_set_ = false;
  std::unique_ptr<uint8_t[]> heap_data_;
  WritableBlockLease write_lease_;
  ReadableBlockLease read_lease_;
};

}  // namespace transport
}  // namespace cmw
}  // namespace hnu

#endif
