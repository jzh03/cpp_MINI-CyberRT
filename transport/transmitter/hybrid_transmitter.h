#ifndef CMW_TRANSPORT_TRANSMITTER_HYBRID_TRANSMITTER_H_
#define CMW_TRANSPORT_TRANSMITTER_HYBRID_TRANSMITTER_H_

#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <cmw/config/transport_mode.h>
#include <cmw/config/message_type.h>
#include <cmw/transport/rtps/participant.h>
#include <cmw/transport/transmitter/intra_transmitter.h>
#include <cmw/transport/transmitter/rtps_transmitter.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {

// Hybrid 发送端：由 Discovery 对端信息按需启用 INTRA、SHM 或 RTPS。
template <typename M>
class HybridTransmitter : public Transmitter<M> {
 public:
  using MessagePtr = std::shared_ptr<M>;
  using PeerMap = std::unordered_map<std::string, RoleAttributes>;
  using Transmitter<M>::TransmitLoanedMessage;

  HybridTransmitter(const RoleAttributes& attr, const ParticipantPtr& participant)
      : Transmitter<M>(attr), participant_(participant) {}
  virtual ~HybridTransmitter() { Disable(); }

  // HYBRID 创建时没有对端信息，等待 Enable(opposite_attr) 决定实际模式。
  void Enable() override {}

  void Disable() override {
    std::lock_guard<std::mutex> lock(mutex_);
    DisablePeers(intra_peers_, intra_transmitter_);
    DisablePeers(shm_peers_, shm_transmitter_);
    DisablePeers(rtps_peers_, rtps_transmitter_);
  }

  void Enable(const RoleAttributes& opposite_attr) override {
    if (!config::IsMessageTypeCompatible(this->attr_.message_type,
                                         opposite_attr.message_type)) {
      AERROR << "Incompatible writer/reader message type: "
             << this->attr_.channel_name;
      return;
    }
    if (!config::IsQosCompatible(this->attr_.qos_profile, opposite_attr.qos_profile)) {
      AERROR << "Incompatible writer/reader QoS: " << this->attr_.channel_name;
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    OptionalMode mode = config::SelectMode(this->attr_, opposite_attr);
    PeerMap* peers = Peers(mode);
    if (peers == nullptr) {
      return;
    }
    const std::string peer_key = PeerKey(opposite_attr);
    if (peers->find(peer_key) != peers->end()) {
      return;
    }

    // 同一 peer 的重复 JOIN 不会重复初始化底层 Transport。
    peers->emplace(peer_key, opposite_attr);
    auto transmitter = GetTransmitter(mode);
    if(transmitter != nullptr) {
      transmitter->Enable();
    }
  }

  void Disable(const RoleAttributes& opposite_attr) override {
    std::lock_guard<std::mutex> lock(mutex_);
    OptionalMode mode = config::SelectMode(this->attr_, opposite_attr);
    PeerMap* peers = Peers(mode);
    if (peers == nullptr) {
      return;
    }
    auto peer = peers->find(PeerKey(opposite_attr));
    if (peer == peers->end()) {
      return;
    }
    peers->erase(peer);
    if (peers->empty()) {
      // 只有最后一个同模式 peer 离开时，才关闭对应 Transport。
      auto transmitter = GetTransmitter(mode);
      if(transmitter != nullptr) {
        transmitter->Disable();
      }
    }
  }

  bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) override {
    return TransmitImpl(msg, msg_info,
                        typename std::is_same<M, LoanedMessage>::type());
  }

  bool TransmitImpl(const MessagePtr& msg, const MessageInfo& msg_info,
                    std::false_type) {
    const LoanedRoute route = SnapshotLoanedRoute();
    // Keep the no-peer ordinary Publish success contract. Do not hold the
    // route lock across synchronous callbacks or backend resource use.
    bool success = true;
    if (route.intra != nullptr) {
      success = route.intra->Transmit(msg, msg_info) && success;
    }
    if (route.shm != nullptr) {
      success = route.shm->Transmit(msg, msg_info) && success;
    }
    if (route.rtps != nullptr) {
      success = route.rtps->Transmit(msg, msg_info) && success;
    }
    return success;
  }

  bool TransmitImpl(const MessagePtr& msg, const MessageInfo& msg_info,
                    std::true_type) {
    (void)msg;
    (void)msg_info;
    AERROR << "LoanedMessage must be published with Publish(std::unique_ptr).";
    return false;
  }

  std::unique_ptr<LoanedMessage> AcquireLoanedMessage(
      std::size_t capacity) override {
    return AcquireLoanedMessageImpl(
        capacity, typename std::is_same<M, LoanedMessage>::type());
  }

  bool TransmitLoanedMessage(std::unique_ptr<LoanedMessage> message,
                             const MessageInfo& msg_info) override {
    return TransmitLoanedMessageImpl(
        std::move(message), msg_info,
        typename std::is_same<M, LoanedMessage>::type());
  }

 private:
  struct LoanedRoute {
    std::shared_ptr<Transmitter<M>> intra;
    std::shared_ptr<ShmTransmitter<M>> shm;
    std::shared_ptr<Transmitter<M>> rtps;

    bool empty() const {
      return intra == nullptr && shm == nullptr && rtps == nullptr;
    }
    bool has_non_shm() const { return intra != nullptr || rtps != nullptr; }
  };

  std::unique_ptr<LoanedMessage> AcquireLoanedMessageImpl(
      std::size_t capacity, std::false_type) {
    (void)capacity;
    return nullptr;
  }

  std::unique_ptr<LoanedMessage> AcquireLoanedMessageImpl(
      std::size_t capacity, std::true_type) {
    const LoanedRoute route = SnapshotLoanedRoute();
    if(route.empty() || !IsLoanedCapacityValid(capacity)) {
      return nullptr;
    }
    // Only an exclusively SHM topology keeps the writable block lease.
    if(route.shm != nullptr && !route.has_non_shm()) {
      return route.shm->AcquireLoanedMessage(capacity);
    }
    return LoanedMessage::CreateHeap(this->attr_.channel_id, capacity);
  }

  bool TransmitLoanedMessageImpl(std::unique_ptr<LoanedMessage> message,
                                 const MessageInfo& msg_info,
                                 std::false_type) {
    (void)message;
    (void)msg_info;
    return false;
  }

  bool TransmitLoanedMessageImpl(std::unique_ptr<LoanedMessage> message,
                                 const MessageInfo& msg_info,
                                 std::true_type) {
    if(message == nullptr) {
      return false;
    }
    const LoanedRoute route = SnapshotLoanedRoute();
    if(route.empty() || !message->CanPublish()) {
      return false;
    }
    const bool source_is_shm = message->is_shm_backed();
    if(source_is_shm && route.shm != nullptr &&
       !route.shm->CanTransmitLoanedMessage(*message)) {
      return false;
    }

    if(route.shm != nullptr && !route.has_non_shm() &&
       source_is_shm) {
      return route.shm->TransmitLoanedMessage(std::move(message), msg_info);
    }

    std::shared_ptr<LoanedMessage> heap_message;
    if(source_is_shm) {
      // Discovery can add a non-SHM route after a pure-SHM acquire. Copy
      // before its writable lease is released, then keep every non-SHM user
      // on this stable process-local snapshot.
      heap_message = LoanedMessage::MakeHeapSnapshot(*message);
    } else {
      if(!message->BeginHeapPublish()) {
        return false;
      }
      heap_message.reset(message.release());
    }
    if(heap_message == nullptr) {
      return false;
    }

    // Preserve the established Hybrid order. The snapshot is intentionally
    // taken without retaining mutex_ across dispatcher, SHM, or Fast DDS work.
    bool success = true;
    if(route.intra != nullptr) {
      success = route.intra->Transmit(heap_message, msg_info) && success;
    }
    if(route.shm != nullptr) {
      if(source_is_shm) {
        success = route.shm->TransmitLoanedMessage(std::move(message), msg_info) &&
                  success;
      } else {
        success = route.shm->TransmitHeapLoanedMessage(heap_message, msg_info) &&
                  success;
      }
    }
    if(route.rtps != nullptr) {
      success = route.rtps->Transmit(heap_message, msg_info) && success;
    }
    return success;
  }

  LoanedRoute SnapshotLoanedRoute() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoanedRoute route;
    if(!intra_peers_.empty()) {
      route.intra = GetTransmitter(OptionalMode::INTRA);
    }
    if(!shm_peers_.empty()) {
      GetTransmitter(OptionalMode::SHM);
      route.shm = shm_transmitter_;
    }
    if(!rtps_peers_.empty()) {
      route.rtps = GetTransmitter(OptionalMode::RTPS);
    }
    return route;
  }

  bool IsLoanedCapacityValid(std::size_t capacity) const {
    const uint32_t configured_capacity = this->attr_.qos_profile.msg_size;
    return configured_capacity == 0 || capacity <= configured_capacity;
  }

  static std::string PeerKey(const RoleAttributes& attr) {
    // Discovery 正常路径使用 endpoint id；兜底键仅处理 id 尚未赋值的调用。
    if (attr.id != 0) {
      return std::to_string(attr.id);
    }
    return attr.host_ip + ":" + std::to_string(attr.process_id) + ":" +
           attr.channel_name;
  }

  PeerMap* Peers(OptionalMode mode) {
    switch (mode) {
      case OptionalMode::INTRA:
        return &intra_peers_;
      case OptionalMode::SHM:
        return &shm_peers_;
      case OptionalMode::RTPS:
        return &rtps_peers_;
      default:
        return nullptr;
    }
  }

  std::shared_ptr<Transmitter<M>> GetTransmitter(OptionalMode mode) {
    return GetTransmitterImpl(mode,
        typename std::is_same<M, LoanedMessage>::type());
  }

  std::shared_ptr<Transmitter<M>> GetTransmitterImpl(OptionalMode mode,
                                                       std::false_type) {
    // 子 Transport 延迟创建，未被 Discovery 选中的模式不占用运行资源。
    switch (mode) {
      case OptionalMode::INTRA:
        if (intra_transmitter_ == nullptr) {
          intra_transmitter_ = std::make_shared<IntraTransmitter<M>>(this->attr_);
        }
        return intra_transmitter_;
      case OptionalMode::SHM:
        if (shm_transmitter_ == nullptr) {
          shm_transmitter_ = std::make_shared<ShmTransmitter<M>>(this->attr_);
        }
        return shm_transmitter_;
      case OptionalMode::RTPS:
        if (rtps_transmitter_ == nullptr) {
          rtps_transmitter_ = std::make_shared<RtpsTransmitter<M>>(
              this->attr_, participant_);
        }
        return rtps_transmitter_;
      default:
        return nullptr;
    }
  }

  std::shared_ptr<Transmitter<M>> GetTransmitterImpl(OptionalMode mode,
                                                       std::true_type) {
    // LoanedMessage uses the same topology split as ordinary messages; only
    // its storage and per-transport send path differ.
    switch(mode) {
      case OptionalMode::INTRA:
        if(intra_transmitter_ == nullptr) {
          intra_transmitter_ = std::make_shared<IntraTransmitter<M>>(this->attr_);
        }
        return intra_transmitter_;
      case OptionalMode::SHM:
        if(shm_transmitter_ == nullptr) {
          shm_transmitter_ = std::make_shared<ShmTransmitter<M>>(this->attr_);
        }
        return shm_transmitter_;
      case OptionalMode::RTPS:
        if(rtps_transmitter_ == nullptr) {
          rtps_transmitter_ = std::make_shared<RtpsTransmitter<M>>(
              this->attr_, participant_);
        }
        return rtps_transmitter_;
      default:
        return nullptr;
    }
  }

  void DisablePeers(PeerMap& peers,
                    const std::shared_ptr<Transmitter<M>>& transmitter) {
    if (transmitter != nullptr && !peers.empty()) {
      transmitter->Disable();
    }
    peers.clear();
  }

  ParticipantPtr participant_;
  std::shared_ptr<IntraTransmitter<M>> intra_transmitter_;
  std::shared_ptr<ShmTransmitter<M>> shm_transmitter_;
  std::shared_ptr<RtpsTransmitter<M>> rtps_transmitter_;
  PeerMap intra_peers_;
  PeerMap shm_peers_;
  PeerMap rtps_peers_;
  std::mutex mutex_;
};

}
}
}

#endif
