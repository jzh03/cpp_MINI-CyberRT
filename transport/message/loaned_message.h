#ifndef CMW_TRANSPORT_MESSAGE_LOANED_MESSAGE_H_
#define CMW_TRANSPORT_MESSAGE_LOANED_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <utility>

#include <cmw/transport/shm/segment.h>

namespace hnu {
namespace cmw {
namespace transport {

template <typename M>
class ShmTransmitter;

// A process-local view of a continuous SHM payload.  Only its payload bytes
// and Block metadata live in shared memory; the leases remain local.
class LoanedMessage {
 public:
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
        state_(State::READ_ONLY),
        read_lease_(std::move(read_lease)) {}

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

  bool BeginPublish(const void* owner) {
    if(state_ != State::WRITABLE || !size_set_ || owner_ != owner ||
       !write_lease_) {
      return false;
    }
    state_ = State::PUBLISHED;
    return true;
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
  State state_;
  bool size_set_ = false;
  WritableBlockLease write_lease_;
  ReadableBlockLease read_lease_;
};

}  // namespace transport
}  // namespace cmw
}  // namespace hnu

#endif
