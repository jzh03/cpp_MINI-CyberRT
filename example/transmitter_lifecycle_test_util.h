#ifndef CMW_EXAMPLE_TRANSMITTER_LIFECYCLE_TEST_UTIL_H_
#define CMW_EXAMPLE_TRANSMITTER_LIFECYCLE_TEST_UTIL_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <gtest/gtest.h>
#include <cmw/common/global_data.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/transmitter/transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace lifecycle_test {

inline RoleAttributes Attributes(const std::string& prefix) {
  RoleAttributes attr{};
  auto global = common::GlobalData::Instance();
  attr.channel_name = prefix + "_" + std::to_string(getpid()) + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  attr.channel_id = common::GlobalData::RegisterChannel(attr.channel_name);
  attr.host_ip = global->HostIp();
  attr.host_name = global->HostName();
  attr.process_id = global->ProcessId();
  attr.node_name = attr.channel_name;
  attr.id = common::GlobalData::GenerateHashId(attr.node_name);
  attr.qos_profile.msg_size = 64;
  attr.message_type = "LoanedMessage";
  return attr;
}

inline bool Fill(LoanedMessage* message, uint64_t sequence) {
  if(message == nullptr || message->mutable_data() == nullptr ||
     message->capacity() < 64) {
    return false;
  }
  std::memcpy(message->mutable_data(), &sequence, sizeof(sequence));
  for(size_t i = sizeof(sequence); i < 64; ++i) {
    message->mutable_data()[i] = static_cast<uint8_t>(sequence + i);
  }
  return message->set_size(64);
}

class Received {
 public:
  explicit Received(bool dds_sequence = false) : dds_sequence_(dds_sequence) {}
  void OnMessage(const std::shared_ptr<LoanedMessage>& message,
                 const MessageInfo& info, const RoleAttributes&) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t sequence = 0;
    bool valid = message != nullptr && message->size() == 64;
    if(valid) {
      std::memcpy(&sequence, message->data(), sizeof(sequence));
      // ReaListener exposes DDS writer sequence numbers, which restart
      // when the Writer is recreated. Payload sequence remains end-to-end.
      valid = dds_sequence_ ? info.seq_num() > 0 : sequence == info.seq_num();
      for(size_t i = sizeof(sequence); i < 64; ++i) {
        valid = valid && message->data()[i] == static_cast<uint8_t>(sequence + i);
      }
    }
    EXPECT_TRUE(valid);
    EXPECT_TRUE(sequences_.insert(sequence).second) << "duplicate " << sequence;
    condition_.notify_all();
  }

  bool Wait(uint64_t sequence, int milliseconds = 20) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, std::chrono::milliseconds(milliseconds),
        [&]() { return sequences_.count(sequence) != 0; });
  }

 private:
  bool dds_sequence_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::set<uint64_t> sequences_;
};

inline bool Send(Transmitter<LoanedMessage>* transmitter, uint64_t sequence) {
  auto loan = transmitter->AcquireLoanedMessage(64);
  if(!Fill(loan.get(), sequence)) {
    return false;
  }
  MessageInfo info;
  info.set_seq_num(sequence);
  return transmitter->TransmitLoanedMessage(std::move(loan), info);
}

// A single sending thread; phase handshakes guarantee that every closed
// interval is observed. Stable intervals require actual delivery, not merely
// a successful return from the transport.
inline void ExerciseHeapLifecycle(Transmitter<LoanedMessage>* transmitter,
                                  Received* received) {
  transmitter->Disable();
  EXPECT_EQ(nullptr, transmitter->AcquireLoanedMessage(64));
  transmitter->Enable();
  auto old = transmitter->AcquireLoanedMessage(64);
  ASSERT_TRUE(Fill(old.get(), 1));
  transmitter->Disable();
  transmitter->Enable();
  MessageInfo info;
  info.set_seq_num(1);
  EXPECT_TRUE(transmitter->TransmitLoanedMessage(std::move(old), info));

  std::mutex mutex;
  std::condition_variable condition;
  int phase = 0;
  int acknowledged = 0;
  bool stop = false;
  std::thread sender([&]() {
    uint64_t sequence = 2;
    while(true) {
      int current;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if(stop) { break; }
        current = phase;
      }
      if(current % 2 != 0) {
        EXPECT_EQ(nullptr, transmitter->AcquireLoanedMessage(64));
        auto heap = LoanedMessage::CreateHeap(transmitter->attributes().channel_id, 64);
        EXPECT_TRUE(Fill(heap.get(), sequence));
        MessageInfo closed_info;
        closed_info.set_seq_num(sequence++);
        EXPECT_FALSE(transmitter->TransmitLoanedMessage(std::move(heap), closed_info));
        std::unique_lock<std::mutex> lock(mutex);
        acknowledged = current;
        condition.notify_all();
        condition.wait_for(lock, std::chrono::seconds(5), [&]() {
          return stop || phase != current;
        });
      } else {
        const uint64_t sent = sequence++;
        const bool success = Send(transmitter, sent);
        const bool delivered = success && received->Wait(sent);
        std::lock_guard<std::mutex> lock(mutex);
        if(delivered && phase == current) {
          acknowledged = current;
          condition.notify_all();
        }
      }
    }
  });
  for(int round = 0; round < 8; ++round) {
    // Sender continues publishing while resources are removed/recreated.
    transmitter->Disable();
    {
      std::unique_lock<std::mutex> lock(mutex);
      phase = round * 2 + 1;
      condition.notify_all();
      EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
        return acknowledged == phase;
      }));
    }
    transmitter->Enable();
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++phase;
      condition.notify_all();
      EXPECT_TRUE(condition.wait_for(lock, std::chrono::seconds(5), [&]() {
        return acknowledged == phase;
      }));
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex);
    stop = true;
    condition.notify_all();
  }
  sender.join();
  transmitter->Disable();
}

}  // namespace lifecycle_test
}  // namespace transport
}  // namespace cmw
}  // namespace hnu
#endif
