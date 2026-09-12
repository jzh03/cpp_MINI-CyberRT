#ifndef CMW_EXAMPLE_SHM_BENCHMARK_COMMON_H_
#define CMW_EXAMPLE_SHM_BENCHMARK_COMMON_H_

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/shm/posix_segment.h>
#include <cmw/transport/transmitter/shm_transmitter.h>
#include <cmw/transport/receiver/shm_receiver.h>

namespace shm_bench {
using namespace hnu::cmw;
using namespace hnu::cmw::transport;
constexpr uint64_t kCapacity = 8 * 1024 * 1024;
constexpr uint64_t kSlots = 32;
constexpr uint64_t kMaxSequence = 16 * 1024 * 1024;
constexpr uint64_t kMagic = 0x434d5742454e4331ULL;
enum Phase : uint64_t { PROBE = 0, WARMUP = 1, BARRIER = 2, MEASURE = 3 };
struct Header { uint64_t magic, phase, sequence; };

inline void Require(bool ok, const std::string& reason) {
  if (!ok) throw std::runtime_error(reason);
}
inline uint64_t Now() {
  timespec ts{};
  Require(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "monotonic clock failed");
  return uint64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}
inline uint64_t Cpu() {
  timespec ts{};
  Require(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0, "CPU clock failed");
  return uint64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}
inline void SleepUntil(uint64_t ns) {
  timespec ts{time_t(ns / 1000000000), long(ns % 1000000000)};
  int error;
  do { error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr); }
  while (error == EINTR);
  Require(error == 0, "absolute sleep failed");
}
struct Options {
  std::string mode, channel, control, output;
  uint64_t size = 4096, warmup_ms = 1000, duration_ms = 5000, drain_ms = 1000;
};
inline Options Parse(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; i += 2) {
    Require(i + 1 < argc, "options require values");
    std::string key = argv[i], value = argv[i + 1];
    if (key == "--mode") o.mode = value;
    else if (key == "--channel") o.channel = value;
    else if (key == "--control") o.control = value;
    else if (key == "--output") o.output = value;
    else {
      Require(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos,
              "expected unsigned integer: " + key);
      uint64_t n = std::stoull(value);
      if (key == "--size") o.size = n;
      else if (key == "--warmup-ms") o.warmup_ms = n;
      else if (key == "--duration-ms") o.duration_ms = n;
      else if (key == "--drain-ms") o.drain_ms = n;
      else throw std::runtime_error("unknown option: " + key);
    }
  }
  Require(o.mode == "copy" || o.mode == "loan", "--mode copy|loan required");
  Require(!o.channel.empty() && !o.control.empty() && !o.output.empty(),
          "--channel, --control and --output required");
  Require(o.size == 4096 || o.size == 65536 || o.size == 1048576 || o.size == 4194304,
          "supported benchmark payloads: 4096, 65536, 1048576, 4194304");
  Require(o.warmup_ms >= 100 && o.warmup_ms <= 60000 && o.duration_ms >= 100 &&
          o.duration_ms <= 60000 && o.drain_ms >= 100 && o.drain_ms <= 10000,
          "invalid phase duration");
  return o;
}
inline config::RoleAttributes Attributes(const Options& o, const char* suffix) {
  auto* g = common::GlobalData::Instance();
  config::RoleAttributes a{};
  a.channel_name = o.channel;
  a.channel_id = common::GlobalData::RegisterChannel(o.channel);
  a.host_name = g->HostName(); a.host_ip = g->HostIp(); a.process_id = getpid();
  a.node_name = o.channel + suffix;
  a.node_id = common::GlobalData::RegisterNode(a.node_name);
  a.id = common::GlobalData::GenerateHashId(a.node_name);
  a.message_type = o.mode == "loan" ? "LoanedMessage" : "BenchmarkCopy";
  a.qos_profile.msg_size = kCapacity;
  return a;
}
inline uint8_t PayloadSeed(uint64_t sequence, uint64_t phase) {
  // Include every sequence byte so corrupting a high sequence bit is detected.
  sequence ^= sequence >> 32;
  sequence ^= sequence >> 16;
  sequence ^= sequence >> 8;
  return uint8_t(sequence + phase * 31);
}
// Both paths run this exact generation loop for every message, inside timing.
inline void Fill(uint8_t* data, size_t size, Phase phase, uint64_t sequence) {
  Header h{kMagic, phase, sequence};
  std::memcpy(data, &h, sizeof(h));
  const uint8_t seed = PayloadSeed(sequence, phase);
  for (size_t i = sizeof(h); i < size; ++i)
    data[i] = uint8_t((seed + i * 17) & 255);
}
inline bool Verify(const uint8_t* data, size_t size, Header* h) {
  if (!data || size < sizeof(Header)) return false;
  std::memcpy(h, data, sizeof(*h));
  if (h->magic != kMagic || h->phase > MEASURE) return false;
  const uint8_t seed = PayloadSeed(h->sequence, h->phase);
  for (size_t i = sizeof(*h); i < size; ++i)
    if (data[i] != uint8_t((seed + i * 17) & 255)) return false;
  return true;
}
struct CopyMessage : serialize::Serializable {
  std::string payload;
  static std::atomic<uint64_t>& Serializations() { static std::atomic<uint64_t> n{0}; return n; }
  static std::atomic<uint64_t>& Deserializations() { static std::atomic<uint64_t> n{0}; return n; }
  void serialize(serialize::DataStream& stream) const override {
    Serializations().fetch_add(1, std::memory_order_relaxed);
    char type = serialize::DataStream::CUSTOM;
    stream.write(&type, 1); stream.write(payload);
  }
  bool unserialize(serialize::DataStream& stream) override {
    char type = 0;
    bool ok = stream.read(&type, 1) && type == serialize::DataStream::CUSTOM && stream.read(payload);
    if (ok) Deserializations().fetch_add(1, std::memory_order_relaxed);
    return ok;
  }
};
inline size_t SerializedSize(size_t size) {
  CopyMessage m; m.payload.resize(size);
  serialize::DataStream stream; stream << m;
  return stream.ByteSize();
}
class Socket {
 public:
  int fd = -1;
  Socket() = default;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  ~Socket() { if (fd >= 0) close(fd); }
  void Configure() {
    timeval timeout{90, 0};
    Require(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
            "socket timeout setup failed");
  }
  void Write(const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size) {
      ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
      if (n < 0 && errno == EINTR) continue;
      Require(n > 0, "control write failed"); p += n; size -= n;
    }
  }
  void Read(void* data, size_t size) {
    char* p = static_cast<char*>(data);
    while (size) {
      ssize_t n = recv(fd, p, size, 0);
      if (n < 0 && errno == EINTR) continue;
      Require(n > 0, "control read failed/timeout/peer exited"); p += n; size -= n;
    }
  }
  template<class T> void Send(const T& v) { Write(&v, sizeof(v)); }
  template<class T> T Receive() { T v{}; Read(&v, sizeof(v)); return v; }
  void Expect(char c) { Require(Receive<char>() == c, "control phase mismatch"); }
  bool Available(int ms) {
    pollfd p{fd, POLLIN, 0};
    int n; do { n = poll(&p, 1, ms); } while (n < 0 && errno == EINTR);
    Require(n >= 0, "control poll failed");
    return n > 0;
  }
};
inline sockaddr_un Address(const Options& o) {
  sockaddr_un a{}; a.sun_family = AF_UNIX;
  Require(o.control.size() < sizeof(a.sun_path), "control socket path too long");
  std::memcpy(a.sun_path, o.control.c_str(), o.control.size() + 1);
  return a;
}
struct Hello { uint64_t magic, pid, channel, mode, size, warmup, duration, drain; };
inline Hello Greeting(const Options& o) {
  return {kMagic, uint64_t(getpid()), common::GlobalData::RegisterChannel(o.channel),
          uint64_t(o.mode == "loan"), o.size, o.warmup_ms, o.duration_ms, o.drain_ms};
}
inline void CheckHello(const Options& o, const Hello& peer) {
  Hello self = Greeting(o);
  Require(peer.pid != self.pid && peer.magic == self.magic && peer.channel == self.channel &&
          peer.mode == self.mode && peer.size == self.size && peer.warmup == self.warmup &&
          peer.duration == self.duration && peer.drain == self.drain, "peer PID/config mismatch");
}
struct Window { uint64_t start, end; };
// Samples all process threads at the shared absolute boundaries. Scheduling
// error is exported; samples >20 ms late invalidate a run in the runner.
struct CpuWindow {
  uint64_t start_ns = 0, end_ns = 0, cpu_ns = 0;
  void Measure(Window w) {
    SleepUntil(w.start); start_ns = Now(); const auto begin = Cpu();
    SleepUntil(w.end); end_ns = Now(); cpu_ns = Cpu() - begin;
  }
};
inline std::string ShmPath(uint64_t channel) { return "/dev/shm/cmw_" + std::to_string(channel); }
// Inspect the actual POSIX mapping, not the requested mode alone.
inline void CheckMapping(uint64_t channel, bool loan) {
  const std::string path = ShmPath(channel);
  struct stat st{};
  Require(stat(path.c_str(), &st) == 0 && uint64_t(st.st_size) == transport::ShmConf(kCapacity).managed_shm_size(),
          "actual POSIX segment size mismatch");
  int fd = open(path.c_str(), O_RDONLY);
  Require(fd >= 0, "cannot inspect POSIX segment");
  void* p = mmap(nullptr, sizeof(transport::State), PROT_READ, MAP_SHARED, fd, 0); close(fd);
  Require(p != MAP_FAILED, "cannot map segment state");
  auto* state = static_cast<transport::State*>(p);
  bool valid = state->ceiling_msg_size() == kCapacity &&
      state->message_type() == (loan ? ShmMessageType::LOANED : ShmMessageType::SERIALIZED);
  munmap(p, sizeof(transport::State));
  Require(valid, "actual SHM capacity/type mismatch");
  std::ifstream maps("/proc/self/maps"); std::string line; bool mapped = false;
  while (std::getline(maps, line)) if (line.find(path) != std::string::npos) mapped = true;
  Require(mapped, "target POSIX segment absent from process mappings");
}
inline bool Bit(const std::vector<uint8_t>& bits, uint64_t seq) {
  return bits[seq / 8] & (uint8_t(1) << (seq % 8));
}
inline void SetBit(std::vector<uint8_t>* bits, uint64_t seq) {
  (*bits)[seq / 8] |= uint8_t(1) << (seq % 8);
}
struct SendStats {
  uint64_t attempts = 0, success = 0, acquire_fail = 0, transmit_fail = 0;
  uint64_t completion_after_end = 0, stop_ns = 0;
};
inline void InitProcess(const Options& o, const char* suffix) {
  signal(SIGPIPE, SIG_IGN);
  const std::string name = o.channel + suffix;
  Require(Init(name.c_str()), "Init failed");
  logger::Logger::Instance()->level(logger::Logger::LOG_ERROR);
  logger::Logger::Instance()->console(false);
}
inline void CommonJson(std::ostream& out, const Options& o, const Hello& peer,
                       Window w, const CpuWindow& cpu) {
  out << "\"mode\":\"" << o.mode << "\",\"size_bytes\":" << o.size
      << ",\"scope\":\"prepare-and-transport-full-validation\",\"pid\":" << getpid()
      << ",\"peer_pid\":" << peer.pid << ",\"channel_id\":" << peer.channel
      << ",\"slots\":" << kSlots << ",\"slot_capacity\":" << kCapacity
      << ",\"segment_bytes\":" << transport::ShmConf(kCapacity).managed_shm_size()
      << ",\"path_verified\":true,\"shm_type\":\"" << (o.mode == "loan" ? "LOANED" : "SERIALIZED")
      << "\",\"serialized_size\":" << SerializedSize(o.size)
      << ",\"warmup_ms\":" << o.warmup_ms << ",\"duration_ms\":" << o.duration_ms
      << ",\"drain_ms\":" << o.drain_ms << ",\"window_start_ns\":" << w.start
      << ",\"window_end_ns\":" << w.end << ",\"cpu_start_ns\":" << cpu.start_ns
      << ",\"cpu_end_ns\":" << cpu.end_ns << ",\"cpu_ns\":" << cpu.cpu_ns;
}
}  // namespace shm_bench
#endif
