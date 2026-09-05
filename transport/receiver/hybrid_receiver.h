#ifndef CMW_TRANSPORT_RECEIVER_HYBRID_RECEIVER_H_
#define CMW_TRANSPORT_RECEIVER_HYBRID_RECEIVER_H_

#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <cmw/config/transport_mode.h>
#include <cmw/transport/receiver/intra_receiver.h>
#include <cmw/transport/receiver/rtps_receiver.h>
#include <cmw/transport/receiver/shm_receiver.h>

namespace hnu {
namespace cmw {
namespace transport {

// Hybrid 接收端：按 Publisher 的 Discovery 信息转发 listener，底层接收资源按
// channel 生命周期复用，不随单个 peer LEAVE 反复创建和销毁。
template <typename M>
class HybridReceiver : public Receiver<M> {
 public:
  using PeerMap = std::unordered_map<std::string, RoleAttributes>;

  HybridReceiver(const RoleAttributes& attr,
                 const typename Receiver<M>::MessageListener& msg_listener)
      : Receiver<M>(attr, msg_listener) {}
  virtual ~HybridReceiver() { Disable(); }

  // HYBRID 创建时等待 Publisher JOIN，不预先启用所有接收路径。
  void Enable() override {}

  void Disable() override {
    std::lock_guard<std::mutex> lock(mutex_);
    // HybridReceiver 整体关闭时统一移除仍登记的 peer listener。
    DisablePeers(intra_peers_, intra_receiver_);
    DisablePeers(shm_peers_, shm_receiver_);
    DisablePeers(rtps_peers_, rtps_receiver_);
  }

  void Enable(const RoleAttributes& opposite_attr) override {
    EnableImpl(opposite_attr, typename std::is_same<M, LoanedMessage>::type());
  }

  void Disable(const RoleAttributes& opposite_attr) override {
    DisableImpl(opposite_attr, typename std::is_same<M, LoanedMessage>::type());
  }

 private:
  void EnableImpl(const RoleAttributes& opposite_attr, std::false_type) {
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
    // 重复 JOIN 保持幂等，每个 Publisher 只注册一次 listener。
    peers->emplace(peer_key, opposite_attr);
    GetReceiver(mode)->Enable(opposite_attr);
  }

  void DisableImpl(const RoleAttributes& opposite_attr, std::false_type) {
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
    // 仅注销离开的 Publisher 并保留底层 Receiver，供后续 Publisher 复用。
    GetReceiver(mode)->Disable(peer->second);
    peers->erase(peer);
  }

  void EnableImpl(const RoleAttributes& opposite_attr, std::true_type) {
    // LoanedMessage now supports all existing receiver paths. Its concrete
    // decoder is selected by each Receiver/Dispatcher, not by Discovery.
    EnableImpl(opposite_attr, std::false_type());
  }

  void DisableImpl(const RoleAttributes& opposite_attr, std::true_type) {
    DisableImpl(opposite_attr, std::false_type());
  }

  static std::string PeerKey(const RoleAttributes& attr) {
    // 优先使用 Discovery endpoint id，避免同 channel 多 Publisher 相互覆盖。
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

  std::shared_ptr<Receiver<M>> GetReceiver(OptionalMode mode) {
    return GetReceiverImpl(mode,
        typename std::is_same<M, LoanedMessage>::type());
  }

  std::shared_ptr<Receiver<M>> GetReceiverImpl(OptionalMode mode,
                                                 std::false_type) {
    // 仅在某种模式第一次出现时创建对应 Receiver。
    switch (mode) {
      case OptionalMode::INTRA:
        if (intra_receiver_ == nullptr) {
          intra_receiver_ = std::make_shared<IntraReceiver<M>>(
              this->attr_, this->msg_listener_);
        }
        return intra_receiver_;
      case OptionalMode::SHM:
        if (shm_receiver_ == nullptr) {
          shm_receiver_ = std::make_shared<ShmReceiver<M>>(
              this->attr_, this->msg_listener_);
        }
        return shm_receiver_;
      case OptionalMode::RTPS:
        if (rtps_receiver_ == nullptr) {
          rtps_receiver_ = std::make_shared<RtpsReceiver<M>>(
              this->attr_, this->msg_listener_);
        }
        return rtps_receiver_;
      default:
        return nullptr;
    }
  }

  std::shared_ptr<Receiver<M>> GetReceiverImpl(OptionalMode mode,
                                                 std::true_type) {
    return GetReceiverImpl(mode, std::false_type());
  }

  void DisablePeers(PeerMap& peers,
                    const std::shared_ptr<Receiver<M>>& receiver) {
    if (receiver != nullptr) {
      for (const auto& peer : peers) {
        receiver->Disable(peer.second);
      }
      receiver->Disable();
    }
    peers.clear();
  }

  std::shared_ptr<IntraReceiver<M>> intra_receiver_;
  std::shared_ptr<ShmReceiver<M>> shm_receiver_;
  std::shared_ptr<RtpsReceiver<M>> rtps_receiver_;
  PeerMap intra_peers_;
  PeerMap shm_peers_;
  PeerMap rtps_peers_;
  std::mutex mutex_;
};

}
}
}

#endif
