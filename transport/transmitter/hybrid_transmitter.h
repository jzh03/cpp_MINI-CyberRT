#ifndef CMW_TRANSPORT_TRANSMITTER_HYBRID_TRANSMITTER_H_
#define CMW_TRANSPORT_TRANSMITTER_HYBRID_TRANSMITTER_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include <cmw/config/transport_mode.h>
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
    transmitter->Enable();
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
      transmitter->Disable();
    }
  }

  bool Transmit(const MessagePtr& msg, const MessageInfo& msg_info) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (intra_peers_.empty() && shm_peers_.empty() && rtps_peers_.empty()) {
      return true;
    }

    // 一次 Publish 向每种活跃模式各发送一次，不按 peer 数量重复发送。
    bool success = true;
    if (!intra_peers_.empty()) {
      success = GetTransmitter(OptionalMode::INTRA)->Transmit(msg, msg_info) &&
                success;
    }
    if (!shm_peers_.empty()) {
      success = GetTransmitter(OptionalMode::SHM)->Transmit(msg, msg_info) &&
                success;
    }
    if (!rtps_peers_.empty()) {
      success = GetTransmitter(OptionalMode::RTPS)->Transmit(msg, msg_info) &&
                success;
    }
    return success;
  }

 private:
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
