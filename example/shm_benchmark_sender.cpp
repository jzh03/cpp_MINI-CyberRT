#include "shm_benchmark_common.h"

using namespace shm_bench;
int main(int argc, char** argv) {
  try {
    const Options o = Parse(argc, argv);
    InitProcess(o, "_sender");
    Socket control; control.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    Require(control.fd >= 0, "socket failed"); control.Configure();
    const auto address = Address(o);
    const uint64_t connect_deadline = Now() + 10000000000ULL;
    while (connect(control.fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      Require(Now() < connect_deadline, "receiver connect timeout");
      SleepUntil(Now() + 10000000);
    }
    control.Send(Greeting(o)); const Hello peer = control.Receive<Hello>(); CheckHello(o, peer);
    const auto attr = Attributes(o, "_sender");
    std::cout << "segment_path=" << ShmPath(attr.channel_id) << std::endl;
    // Precreate a fixed layout before either transport touches it. No runtime
    // configuration or library policy is changed by this benchmark fixture.
    auto segment = std::make_shared<PosixSegment>(attr.channel_id, kCapacity);
    WritableBlock seed;
    Require(segment->AcquireBlockToWriteWithoutRecreate(o.size,
        o.mode == "loan" ? ShmMessageType::LOANED : ShmMessageType::SERIALIZED, &seed),
        "cannot precreate benchmark segment");
    segment->ReleaseWrittenBlock(seed);
    Require(segment->block_num() == kSlots, "slot count mismatch");
    std::shared_ptr<ShmTransmitter<CopyMessage>> copy;
    std::shared_ptr<ShmTransmitter<LoanedMessage>> loan;
    if (o.mode == "loan") { loan = std::make_shared<ShmTransmitter<LoanedMessage>>(attr); loan->Enable(); }
    else { copy = std::make_shared<ShmTransmitter<CopyMessage>>(attr); copy->Enable(); }
    uint64_t shm_loans = 0;
    auto send = [&](Phase phase, uint64_t sequence) -> int {
      if (loan) {
        auto m = loan->AcquireLoanedMessage(o.size);
        if (!m) return 1;
        Require(m->is_shm_backed() && m->mutable_data() && m->channel_id() == attr.channel_id &&
                m->block_index() < kSlots && m->generation() != 0, "loan path evidence failed");
        ++shm_loans;
        Fill(m->mutable_data(), o.size, phase, sequence);
        Require(m->set_size(o.size), "loan set_size failed");
        return loan->TransmitLoanedMessage(std::move(m)) ? 0 : 2;
      }
      auto m = std::make_shared<CopyMessage>(); m->payload.resize(o.size);
      Fill(reinterpret_cast<uint8_t*>(&m->payload[0]), o.size, phase, sequence);
      return static_cast<Transmitter<CopyMessage>&>(*copy).Transmit(m) ? 0 : 2;
    };
    auto probe = [&](Phase phase, char reply) {
      const uint64_t deadline = Now() + 10000000000ULL;
      do {
        send(phase, 0);
        if (control.Available(20)) { control.Expect(reply); return; }
      } while (Now() < deadline);
      throw std::runtime_error("SHM probe/barrier timeout");
    };
    control.Expect('R'); probe(PROBE, 'P');
    CheckMapping(attr.channel_id, bool(loan));
    std::cout << "ready: explicit SHM, peer_pid=" << peer.pid << " slots=32 capacity=8388608\n";
    uint64_t warmup_attempts = 0;
    const auto warmup_end = Now() + o.warmup_ms * 1000000;
    while (Now() < warmup_end) send(WARMUP, ++warmup_attempts);
    control.Send('W'); probe(BARRIER, 'B');
    // Allocate/touch accounting memory before the formal window.
    std::vector<uint8_t> success_bits(kMaxSequence / 8 + 1, 0);
    Window w{Now() + 300000000ULL, 0}; w.end = w.start + o.duration_ms * 1000000;
    control.Send(w); control.Expect('A');
    CpuWindow cpu;
    std::thread sampler([&] { cpu.Measure(w); });
    struct Join { std::thread& t; ~Join() { if (t.joinable()) t.join(); } } join{sampler};
    SendStats stats;
    SleepUntil(w.start);
    const auto serial_before = CopyMessage::Serializations().load();
    const auto loans_before = shm_loans;
    while (Now() < w.end) {
      Require(stats.attempts < kMaxSequence, "accounting sequence limit exceeded");
      int result = send(MEASURE, ++stats.attempts);
      if (result == 0) { ++stats.success; SetBit(&success_bits, stats.attempts); }
      else if (result == 1) ++stats.acquire_fail;
      else ++stats.transmit_fail;
      if (Now() >= w.end) ++stats.completion_after_end;
    }
    stats.stop_ns = Now();
    const auto serial_measured = CopyMessage::Serializations().load() - serial_before;
    const auto loans_measured = shm_loans - loans_before;
    sampler.join();
    control.Send(stats);
    control.Write(success_bits.data(), stats.attempts / 8 + 1);
    control.Expect('D'); // Keep the segment and transmitter alive through drain.
    CheckMapping(attr.channel_id, bool(loan));
    std::ofstream out(o.output); Require(bool(out), "cannot open sender result");
    out << '{'; CommonJson(out, o, peer, w, cpu);
    out << ",\"attempts\":" << stats.attempts << ",\"send_success\":" << stats.success
        << ",\"acquire_fail\":" << stats.acquire_fail << ",\"transmit_fail\":" << stats.transmit_fail
        << ",\"completion_after_end\":" << stats.completion_after_end
        << ",\"stop_ns\":" << stats.stop_ns << ",\"warmup_attempts_excluded\":" << warmup_attempts
        << ",\"measured_serializations\":" << serial_measured
        << ",\"measured_shm_loans\":" << loans_measured << "}\n";
    out.close(); Require(bool(out), "sender result write failed");
    return 0;
  } catch (const std::exception& e) { std::cerr << "sender: " << e.what() << '\n'; return 1; }
}
