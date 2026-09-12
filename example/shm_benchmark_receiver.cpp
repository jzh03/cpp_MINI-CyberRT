#include "shm_benchmark_common.h"

using namespace shm_bench;
struct Collector {
  explicit Collector(const Options& options) : o(options), seen(kMaxSequence / 8 + 1, 0) {}
  Options o;
  std::atomic<bool> probe{false}, barrier{false};
  std::atomic<uint64_t> start{0}, end{0}, cutoff{UINT64_MAX};
  std::vector<uint8_t> seen;
  uint64_t window_unique = 0, drain_unique = 0, duplicates_window = 0, duplicates_drain = 0;
  uint64_t invalid = 0, warmup = 0, warmup_after_start = 0, early = 0, after_cutoff = 0;
  uint64_t shm_callbacks = 0, formal_callbacks = 0;
  void Receive(const uint8_t* data, size_t size, bool path_ok) {
    Header h{};
    const bool valid = size == o.size && Verify(data, size, &h) && path_ok;
    const auto completed = Now(); // Full validation must complete within window.
    if (!valid) { ++invalid; return; }
    if (h.phase == PROBE) { probe.store(true); return; }
    if (h.phase == BARRIER) { barrier.store(true); return; }
    if (h.phase == WARMUP) {
      ++warmup;
      if (start.load() && completed >= start.load()) ++warmup_after_start;
      return;
    }
    ++formal_callbacks;
    if (o.mode == "loan") ++shm_callbacks;
    Record(h.sequence, completed);
  }
  void Record(uint64_t sequence, uint64_t completed) {
    if (sequence == 0 || sequence > kMaxSequence) { ++invalid; return; }
    if (completed < start.load()) { ++early; return; }
    if (completed >= cutoff.load()) { ++after_cutoff; return; }
    const bool in_window = completed < end.load();
    if (Bit(seen, sequence)) {
      if (in_window) ++duplicates_window; else ++duplicates_drain;
    } else {
      SetBit(&seen, sequence);
      if (in_window) ++window_unique; else ++drain_unique;
    }
  }
};

// Deterministic checks for the measurement contract; no middleware is started.
void SelfTest() {
  Options o; o.mode = "loan";
  Collector c(o);
  c.start.store(100); c.end.store(200); c.cutoff.store(300);
  c.Record(1, 99);   // early: excluded
  c.Record(1, 100);  // inclusive start
  c.Record(1, 199);  // duplicate: no bytes added
  c.Record(2, 200);  // exclusive end: drain
  c.Record(2, 299);  // duplicate in drain
  c.Record(3, 300);  // exclusive drain cutoff
  c.Record(0, 150); c.Record(kMaxSequence + 1, 150);
  Require(c.window_unique == 1 && c.drain_unique == 1 && c.duplicates_window == 1 &&
          c.duplicates_drain == 1 && c.early == 1 && c.after_cutoff == 1 && c.invalid == 2 &&
          Bit(c.seen, 1) && Bit(c.seen, 2) && !Bit(c.seen, 3), "window/duplicate contract failed");
  std::vector<uint8_t> data(o.size);
  Header h{};
  Fill(data.data(), data.size(), MEASURE, 42);
  Require(Verify(data.data(), data.size(), &h) && h.sequence == 42 && h.phase == MEASURE,
          "payload round trip failed");
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] ^= 128;
    Require(!Verify(data.data(), data.size(), &h), "payload corruption escaped full validation");
    data[i] ^= 128;
  }
  Collector phases(o);
  phases.start.store(UINT64_MAX);
  Fill(data.data(), data.size(), PROBE, 0); phases.Receive(data.data(), data.size(), true);
  Fill(data.data(), data.size(), WARMUP, 42); phases.Receive(data.data(), data.size(), true);
  Fill(data.data(), data.size(), BARRIER, 0); phases.Receive(data.data(), data.size(), true);
  Require(phases.probe.load() && phases.barrier.load() && phases.warmup == 1 &&
          phases.formal_callbacks == 0 && phases.window_unique == 0, "phase separation failed");
  phases.Receive(data.data(), data.size(), false);
  phases.Receive(data.data(), data.size() - 1, true);
  Require(phases.invalid == 2, "size/path validation failed");
  std::cout << "PASS full-content corruption, phase isolation, window boundaries, duplicates, sequence bounds\n";
}

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") { SelfTest(); return 0; }
    const Options o = Parse(argc, argv);
    InitProcess(o, "_receiver");
    Socket listener; listener.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    Require(listener.fd >= 0, "socket failed");
    const auto address = Address(o);
    Require(bind(listener.fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
            "bind failed (use a fresh control path)");
    struct Unlink { std::string path; ~Unlink() { unlink(path.c_str()); } } cleanup{o.control};
    Require(listen(listener.fd, 1) == 0 && listener.Available(10000), "accept timeout");
    Socket control; control.fd = accept(listener.fd, nullptr, nullptr);
    Require(control.fd >= 0, "accept failed"); control.Configure();
    const Hello peer = control.Receive<Hello>(); CheckHello(o, peer); control.Send(Greeting(o));
    const auto attr = Attributes(o, "_receiver");
    Collector collector(o);
    struct Shutdown { ~Shutdown() { ShmDispatcher::Instance()->Shutdown(); } } shutdown;
    std::shared_ptr<Receiver<CopyMessage>> copy;
    std::shared_ptr<Receiver<LoanedMessage>> loan;
    if (o.mode == "loan") {
      loan = std::make_shared<ShmReceiver<LoanedMessage>>(attr,
          [&](const std::shared_ptr<LoanedMessage>& m, const MessageInfo&, const config::RoleAttributes&) {
            collector.Receive(m->data(), m->size(), m->is_shm_backed() && m->is_read_only() &&
                m->channel_id() == attr.channel_id && m->generation() != 0 && m->block_index() < kSlots);
          });
      loan->Enable();
    } else {
      copy = std::make_shared<ShmReceiver<CopyMessage>>(attr,
          [&](const std::shared_ptr<CopyMessage>& m, const MessageInfo&, const config::RoleAttributes&) {
            collector.Receive(reinterpret_cast<const uint8_t*>(m->payload.data()), m->payload.size(), true);
          });
      copy->Enable();
    }
    auto wait_flag = [&](const std::atomic<bool>& flag) {
      const auto deadline = Now() + 10000000000ULL;
      while (!flag.load()) { Require(Now() < deadline, "SHM receive probe/barrier timeout"); SleepUntil(Now() + 1000000); }
    };
    control.Send('R'); wait_flag(collector.probe);
    CheckMapping(attr.channel_id, bool(loan)); control.Send('P');
    std::cout << "ready: received validated SHM probe, peer_pid=" << peer.pid << '\n';
    control.Expect('W'); wait_flag(collector.barrier); control.Send('B');
    const Window w = control.Receive<Window>();
    Require(w.start > Now() && w.end == w.start + o.duration_ms * 1000000, "invalid scheduled window");
    collector.start.store(w.start); collector.end.store(w.end); control.Send('A');
    CpuWindow cpu; cpu.Measure(w);
    const SendStats stats = control.Receive<SendStats>();
    Require(stats.attempts <= kMaxSequence, "invalid attempt count");
    // Cutoff depends on sender stop, not the receipt of its accounting data.
    const auto drain_end = std::max(w.end, stats.stop_ns) + o.drain_ms * 1000000;
    Require(Now() < drain_end, "control accounting arrived after drain deadline");
    collector.cutoff.store(drain_end);
    std::vector<uint8_t> successes(kMaxSequence / 8 + 1, 0);
    control.Read(successes.data(), stats.attempts / 8 + 1);
    SleepUntil(drain_end);
    if (loan) loan->Disable(); else copy->Disable();
    CheckMapping(attr.channel_id, bool(loan));
    ShmDispatcher::Instance()->Shutdown(); // Join callback thread before reading counters.
    uint64_t missing_success = 0, received_failed = 0, seen_count = 0, success_count = 0;
    for (uint64_t seq = 1; seq <= stats.attempts; ++seq) {
      const bool sent = Bit(successes, seq), received = Bit(collector.seen, seq);
      missing_success += sent && !received; received_failed += !sent && received;
      seen_count += received; success_count += sent;
    }
    Require(success_count == stats.success && seen_count == collector.window_unique + collector.drain_unique,
            "sequence accounting mismatch");
    std::ofstream out(o.output); Require(bool(out), "cannot open receiver result");
    out << '{'; CommonJson(out, o, peer, w, cpu);
    out << ",\"window_unique\":" << collector.window_unique
        << ",\"window_valid_bytes\":" << collector.window_unique * o.size
        << ",\"drain_unique\":" << collector.drain_unique
        << ",\"duplicates_window\":" << collector.duplicates_window
        << ",\"duplicates_drain\":" << collector.duplicates_drain
        << ",\"missing_success_after_drain\":" << missing_success
        << ",\"missing_attempts_after_drain\":" << stats.attempts - seen_count
        << ",\"received_failed_send\":" << received_failed
        << ",\"invalid\":" << collector.invalid << ",\"early\":" << collector.early
        << ",\"after_cutoff\":" << collector.after_cutoff
        << ",\"warmup_received_excluded\":" << collector.warmup
        << ",\"warmup_after_start\":" << collector.warmup_after_start
        << ",\"formal_callbacks\":" << collector.formal_callbacks
        << ",\"shm_callbacks\":" << collector.shm_callbacks
        << ",\"deserializations_all_phases\":" << CopyMessage::Deserializations().load()
        << ",\"drain_end_ns\":" << drain_end << "}\n";
    out.close(); Require(bool(out), "receiver result write failed");
    control.Send('D');
    return collector.invalid || collector.early || collector.warmup_after_start ? 2 : 0;
  } catch (const std::exception& e) { std::cerr << "receiver: " << e.what() << '\n'; return 1; }
}
