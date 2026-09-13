#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <cmw/init.h>
#include <cmw/node/node.h>
#include <cmw/scheduler/scheduler_factory.h>
#include <cmw/transport/dispatcher/shm_dispatcher.h>

namespace {
using namespace hnu::cmw;
using transport::LoanedMessage;
using Clock = std::chrono::steady_clock;
volatile sig_atomic_t stop_requested = 0;
void Stop(int) { stop_requested = 1; }

struct Options {
  std::string role = "intra", scenario = "A", channel = "mini_demo_manual";
  size_t payload = 0;
  unsigned hz = 0, seconds = 30;
};

void Help() {
  std::puts("MINI CyberRT local communication demo\n"
      "  --role intra|pub|sub  --scenario A|B|C|D|E\n"
      "  --channel NAME --payload BYTES --hz N --seconds N\n"
      "Defaults: A/intra, 1024 bytes (C: 1048576), 10 Hz (C: 5), 30 s.\n"
      "C accepts 1048576 or 4194304 bytes. Others: 16..65536 bytes.\n"
      "Subscriber duration starts at its first valid message; readiness timeout 20 s.\n"
      "SIGINT/SIGTERM requests orderly shutdown. CMW_DEMO_TRACE=1 enables routes.\n"
      "E is same-host forced RTPS only; Node has no explicit-mode parameter.");
}

unsigned Number(const std::string& value) {
  if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("expected a positive integer");
  const auto n = std::stoul(value);
  if (n == 0 || n > 10000000) throw std::runtime_error("integer out of range");
  return static_cast<unsigned>(n);
}

Options Parse(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--help") { Help(); std::exit(0); }
    if (++i == argc) throw std::runtime_error("missing value for " + key);
    const std::string v = argv[i];
    if (key == "--role") o.role = v;
    else if (key == "--scenario") o.scenario = v;
    else if (key == "--channel") o.channel = v;
    else if (key == "--payload") o.payload = Number(v);
    else if (key == "--hz") o.hz = Number(v);
    else if (key == "--seconds") o.seconds = Number(v);
    else throw std::runtime_error("unknown option " + key);
  }
  if (o.scenario.size() != 1 || o.scenario[0] < 'A' || o.scenario[0] > 'E' ||
      (o.role != "pub" && o.role != "sub" && o.role != "intra") ||
      ((o.scenario == "A") != (o.role == "intra")))
    throw std::runtime_error("A requires intra; B/C/D/E require pub or sub");
  if (o.channel.empty() || o.channel.size() > 100 ||
      o.channel.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
    throw std::runtime_error("channel must be 1..100 letters/digits/_/-");
  if (!o.payload) o.payload = o.scenario == "C" ? 1048576 : 1024;
  if (!o.hz) o.hz = o.scenario == "C" ? 5 : 10;
  if (o.hz > 100 || o.seconds > 600 ||
      (o.scenario == "C" ? o.payload != 1048576 && o.payload != 4194304 :
                            o.payload < 16 || o.payload > 65536))
    throw std::runtime_error("payload/hz/seconds outside demo limits");
  return o;
}

struct Message : serialize::Serializable {
  uint64_t sequence = 0;
  std::string payload;
  SERIALIZE(sequence, payload)
};

// Demo payload header is explicit little endian; it is not a middleware ABI.
void Put64(uint8_t* p, uint64_t n) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(n >> (i * 8));
}
uint64_t Get64(const uint8_t* p) {
  uint64_t n = 0;
  for (int i = 0; i < 8; ++i) n |= uint64_t(p[i]) << (i * 8);
  return n;
}
uint8_t Pattern(uint64_t seq, size_t offset) {
  return static_cast<uint8_t>((seq * 131 + offset * 17 + (offset >> 8)) & 255);
}
void Fill(uint8_t* p, size_t size, uint64_t seq) {
  Put64(p, seq);
  Put64(p + 8, size);
  for (size_t i = 16; i < size; ++i) p[i] = Pattern(seq, i);
}

struct ReceiveStats {
  std::mutex mutex;
  uint64_t valid = 0, invalid = 0, gaps = 0, order_errors = 0;
  uint64_t first = 0, last = 0, consecutive = 0;
  Clock::time_point first_time;

  void Check(const uint8_t* p, size_t size, const Options& o,
             bool path_ok, uint64_t ordinary_sequence = 0) {
    std::lock_guard<std::mutex> lock(mutex);
    uint64_t seq = size >= 16 && p ? Get64(p) : 0;
    bool ok = path_ok && p && size == o.payload && seq > 0;
    if (ok) ok = Get64(p + 8) == size &&
                 (!ordinary_sequence || seq == ordinary_sequence);
    for (size_t i = 16; ok && i < size; ++i) ok = p[i] == Pattern(seq, i);
    if (!ok) {
      if (++invalid == 1) std::puts("[CHECK] INVALID payload/sequence/length/Loan-View attributes");
      return;
    }
    if (valid) {
      if (seq <= last) ++order_errors;
      else if (seq > last + 1) gaps += seq - last - 1;
    }
    consecutive = !valid || seq == last + 1 ? consecutive + 1 : 1;
    if (!valid) {
      first = seq;
      first_time = Clock::now();
      std::printf("[CHECK] FIRST_VALID seq=%llu bytes=%zu path=%s\n",
                  (unsigned long long)seq, size,
                  o.scenario == "C" ? "SHM_READ_ONLY_VIEW" : "ordinary");
    }
    ++valid;
    last = seq;
    if (consecutive == 10 && valid == 10)
      std::printf("[CHECK] CONTIGUOUS_10 first=%llu last=%llu\n",
                  (unsigned long long)first, (unsigned long long)last);
  }
  void Report() {
    std::printf("[SUB] valid=%llu invalid=%llu gaps_online=%llu order_errors=%llu first=%llu last=%llu\n",
        (unsigned long long)valid, (unsigned long long)invalid,
        (unsigned long long)gaps, (unsigned long long)order_errors,
        (unsigned long long)first, (unsigned long long)last);
  }
};

RoleAttributes Attributes(const Options& o) {
  RoleAttributes a{};
  auto* g = common::GlobalData::Instance();
  a.channel_name = o.channel;
  a.channel_id = common::GlobalData::RegisterChannel(o.channel);
  a.host_name = g->HostName(); a.host_ip = g->HostIp();
  a.process_id = g->ProcessId();
  a.node_name = o.channel + "_" + o.role;
  a.node_id = common::GlobalData::RegisterNode(a.node_name);
  a.message_type = o.scenario == "C" ? "LoanedMessage" : "MiniDemoMessage";
  a.qos_profile.msg_size = o.payload;
  a.qos_profile.depth = o.scenario == "C" ? 0 : 16;
  return a;
}

template <typename T> bool Send(Publisher<T>& pub, const Options& o, uint64_t seq);
template <> bool Send(Publisher<Message>& pub, const Options& o, uint64_t seq) {
  auto m = std::make_shared<Message>();
  m->sequence = seq; m->payload.resize(o.payload);
  Fill(reinterpret_cast<uint8_t*>(&m->payload[0]), o.payload, seq);
  return pub.Publish(m);
}
template <> bool Send(Publisher<LoanedMessage>& pub, const Options& o, uint64_t seq) {
  auto m = pub.AcquireMessage(o.payload);
  if (!m || !m->is_shm_backed() || !m->mutable_data() ||
      m->channel_id() != common::GlobalData::RegisterChannel(o.channel)) return false;
  // Fill the borrowed SHM block itself, with no intermediate payload buffer.
  Fill(m->mutable_data(), o.payload, seq);
  if (seq == 1) std::printf("[PUB] LOAN_SEND shm_backed=1 block=%u generation=%llu bytes=%zu\n",
      m->block_index(), (unsigned long long)m->generation(), o.payload);
  return m->set_size(o.payload) && pub.Publish(std::move(m));
}

void Receive(const std::shared_ptr<Message>& m, ReceiveStats& s, const Options& o) {
  s.Check(reinterpret_cast<const uint8_t*>(m->payload.data()), m->payload.size(), o,
          m->sequence != 0, m->sequence);
}
void Receive(const std::shared_ptr<LoanedMessage>& m, ReceiveStats& s, const Options& o) {
  // Inspect the read-only View while its shared_ptr owns the ReadableBlockLease.
  // No pointer or shared_ptr escapes this callback.
  s.Check(m->data(), m->size(), o, m->is_shm_backed() && m->is_read_only() &&
      m->mutable_data() == nullptr && m->generation() != 0 &&
      m->channel_id() == common::GlobalData::RegisterChannel(o.channel));
}

template <typename T> int Run(const Options& o) {
  ReceiveStats stats;
  const auto a = Attributes(o);
  auto node = CreateNode(a.node_name);
  std::shared_ptr<Publisher<T>> pub;
  std::shared_ptr<Subscriber<T>> sub;
  std::shared_ptr<transport::Transmitter<Message>> rtps_pub;
  std::shared_ptr<transport::Receiver<Message>> rtps_sub;
  const bool sending = o.role != "sub", receiving = o.role != "pub";
  if (o.scenario == "E") {
    std::puts("[STAGE] 同机强制 RTPS 验证 (explicit Transport mode)");
    if (sending) rtps_pub = transport::Transport::Instance()->CreateTransmitter<Message>(a, OptionalMode::RTPS);
    else rtps_sub = transport::Transport::Instance()->CreateReceiver<Message>(a,
        [&](const std::shared_ptr<Message>& m, const transport::MessageInfo&, const RoleAttributes&) {
          Receive(m, stats, o);
        }, OptionalMode::RTPS);
    if ((sending && !rtps_pub) || (receiving && !rtps_sub)) throw std::runtime_error("RTPS endpoint failed");
  } else {
    if (receiving) sub = node->CreateSubscriber<T>(a,
        [&](const std::shared_ptr<T>& m) { Receive(m, stats, o); });
    if (sending) pub = node->CreatePublisher<T>(a);
    if ((sending && !pub) || (receiving && !sub)) throw std::runtime_error("Node endpoint failed");
  }
  std::printf("\n[STAGE] READY role=%s scenario=%s pid=%d channel=%s channel_id=%llu\n",
      o.role.c_str(), o.scenario.c_str(), getpid(), o.channel.c_str(),
      (unsigned long long)a.channel_id);
  auto start = Clock::now(), next = start, report = start;
  uint64_t attempts = 0, success = 0, fail = 0, no_peer = 0;
  std::vector<uint64_t> previous_peers;
  bool matched = false, timed_out = false;
  while (!stop_requested) {
    const auto now = Clock::now();
    bool has_peer = true;
    if (pub) {
      std::vector<RoleAttributes> peers;
      pub->GetSubscribers(&peers);
      std::vector<uint64_t> ids;
      for (const auto& peer : peers) ids.push_back(peer.id);
      std::sort(ids.begin(), ids.end());
      has_peer = !ids.empty();
      if (ids != previous_peers) {
        std::printf("[DISCOVERY] %s subscribers=%zu next_seq=%llu\n",
            has_peer ? "MATCHED" : "OFFLINE", ids.size(), (unsigned long long)(attempts + 1));
        for (const auto& peer : peers)
          std::printf("[DISCOVERY] peer=%llu ip=%s pid=%d\n", (unsigned long long)peer.id,
                      peer.host_ip.c_str(), peer.process_id);
        previous_peers = ids;
      }
      matched = matched || has_peer;
    }
    if (sending && now >= next && (o.scenario != "C" || has_peer)) {
      ++attempts;
      bool ok;
      if (rtps_pub) {
        auto m = std::make_shared<Message>();
        m->sequence = attempts; m->payload.resize(o.payload);
        Fill(reinterpret_cast<uint8_t*>(&m->payload[0]), o.payload, attempts);
        ok = rtps_pub->Transmit(m);
      } else ok = Send(*pub, o, attempts);
      if (ok) ++success; else ++fail;
      if (!has_peer) ++no_peer;
      next = now + std::chrono::microseconds(1000000 / o.hz);
    }
    {
      std::lock_guard<std::mutex> lock(stats.mutex);
      if (now >= report) {
        if (sending) std::printf("[PUB] attempts=%llu success=%llu fail=%llu no_peer_attempts=%llu last_seq=%llu\n",
            (unsigned long long)attempts, (unsigned long long)success,
            (unsigned long long)fail, (unsigned long long)no_peer, (unsigned long long)attempts);
        if (receiving) stats.Report();
        report = now + std::chrono::seconds(1);
      }
      if (stats.invalid || stats.gaps || stats.order_errors || fail) break;
      if (receiving && stats.valid && now - stats.first_time >= std::chrono::seconds(o.seconds)) break;
      if (receiving && !stats.valid && now - start >= std::chrono::seconds(20)) { timed_out = true; break; }
    }
    if (sending && !receiving && now - start >= std::chrono::seconds(o.seconds)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  // Stop publishing first; quiesce callbacks before reading final counters.
  if (sub) { sub->Shutdown(); sub->ClearData(); }
  if (pub) pub->Shutdown();
  if (rtps_sub) rtps_sub->Disable();
  if (rtps_pub) rtps_pub->Disable();
  sub.reset(); pub.reset(); node.reset();
  scheduler::Instance()->Shutdown();
  auto* shm = transport::ShmDispatcher::Instance(false);
  if (shm) shm->Shutdown();
  transport::Transport::Instance()->Shutdown();
  std::lock_guard<std::mutex> lock(stats.mutex);
  if (receiving) stats.Report();
  if (sending) std::printf("[PUB] FINAL attempts=%llu success=%llu fail=%llu no_peer_attempts=%llu last_seq=%llu\n",
      (unsigned long long)attempts, (unsigned long long)success, (unsigned long long)fail,
      (unsigned long long)no_peer, (unsigned long long)attempts);
  const bool ok = !timed_out && !fail && !stats.invalid && !stats.gaps && !stats.order_errors &&
      (!sending || (success > no_peer && (matched || o.scenario == "E"))) &&
      (!receiving || (stats.valid >= 10 && stats.consecutive >= 10));
  std::printf("[RESULT] %s role=%s reason=%s orderly_shutdown=1\n", ok ? "PASS" : "FAIL",
      o.role.c_str(), ok ? "local_checks_ok_delivery_requires_sub_result" :
      "readiness_timeout_or_insufficient_messages_or_validation_or_send_failure");
  return ok ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  try {
    const auto o = Parse(argc, argv);
    std::signal(SIGINT, Stop); std::signal(SIGTERM, Stop);
    const std::string log_name = "demo_" + o.channel + "_" + std::to_string(getpid());
    // Logger always writes under the real repository root. The script archives
    // this exact process's file into its run directory after the process exits.
    Init(log_name.c_str());
    logger::Logger::Instance()->console(false);
    logger::Logger::Instance()->level(logger::Logger::LOG_WARN);
    discovery::TopologyManager::Instance();
    std::puts("[STAGE] DISCOVERY_BOOTSTRAP (endpoint creation is asynchronous)");
    for (int i = 0; i < 100 && !stop_requested; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return o.scenario == "C" ? Run<LoanedMessage>(o) : Run<Message>(o);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[RESULT] FAIL reason=%s\n", e.what());
    return 2;
  }
}
