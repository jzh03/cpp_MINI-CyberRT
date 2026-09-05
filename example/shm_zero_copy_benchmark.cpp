// Build: make shm_zero_copy_benchmark
// Run:   CMW_PATH="$(cd .. && pwd)" ./build/bin/shm_zero_copy_benchmark
//        CMW_PATH="$(cd .. && pwd)" ./build/bin/shm_zero_copy_benchmark --quick

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <time.h>
#include <unistd.h>

#include <cmw/common/global_data.h>
#include <cmw/init.h>
#include <cmw/serialize/serializable.h>
#include <cmw/transport/message/loaned_message.h>
#include <cmw/transport/receiver/intra_receiver.h>
#include <cmw/transport/receiver/shm_receiver.h>
#include <cmw/transport/transport.h>
#include <cmw/transport/transmitter/intra_transmitter.h>
#include <cmw/transport/transmitter/shm_transmitter.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

using Clock = std::chrono::steady_clock;

struct CopyMessage : public serialize::Serializable {
  uint64_t sequence = 0;
  std::string payload;

  SERIALIZE(sequence, payload)
};

struct Result {
  std::size_t payload_size = 0;
  std::string mode;
  uint32_t iterations = 0;
  uint32_t success = 0;
  uint32_t failures = 0;
  double elapsed_ms = 0.0;
  double throughput_mib_s = 0.0;
  double message_rate = 0.0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double p99_us = 0.0;
  double publisher_cpu_ms = 0.0;
  double subscriber_cpu_ms = 0.0;
};

double ProcessCpuMilliseconds() {
  timespec value{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value);
  return value.tv_sec * 1000.0 + value.tv_nsec / 1000000.0;
}

RoleAttributes MakeRoleAttributes(const std::string& channel,
                                  const std::string& suffix,
                                  uint32_t payload_size) {
  auto global_data = common::GlobalData::Instance();
  RoleAttributes attr{};
  attr.channel_name = channel;
  attr.channel_id = common::GlobalData::RegisterChannel(channel);
  attr.host_name = global_data->HostName();
  attr.host_ip = global_data->HostIp();
  attr.process_id = global_data->ProcessId();
  attr.node_name = channel + suffix;
  attr.node_id = common::GlobalData::RegisterNode(attr.node_name);
  attr.id = common::GlobalData::GenerateHashId(attr.node_name);
  attr.message_type = "BenchmarkLoanedMessage";
  attr.qos_profile.msg_size = payload_size;
  return attr;
}

void FillPayload(uint8_t* data, std::size_t size, uint64_t sequence) {
  if(size < sizeof(sequence)) {
    return;
  }
  std::memcpy(data, &sequence, sizeof(sequence));
  for(std::size_t index = sizeof(sequence); index < size; ++index) {
    data[index] = static_cast<uint8_t>((sequence + index * 17) & 0xff);
  }
}

bool VerifyPayload(const uint8_t* data, std::size_t size, uint64_t* sequence) {
  if(data == nullptr || sequence == nullptr || size < sizeof(*sequence)) {
    return false;
  }
  std::memcpy(sequence, data, sizeof(*sequence));
  const std::size_t samples[] = {sizeof(*sequence), size / 2, size - 1};
  for(std::size_t sample : samples) {
    if(sample < sizeof(*sequence) || sample >= size) {
      continue;
    }
    if(data[sample] != static_cast<uint8_t>((*sequence + sample * 17) & 0xff)) {
      return false;
    }
  }
  return true;
}

class Collector {
 public:
  explicit Collector(uint32_t count) : send_times_(count + 1) {}

  void MarkSent(uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(sequence < send_times_.size()) {
      send_times_[sequence] = Clock::now();
    }
  }

  void Receive(const uint8_t* data, std::size_t size) {
    const double cpu_before = ProcessCpuMilliseconds();
    uint64_t sequence = 0;
    const bool valid = VerifyPayload(data, size, &sequence);
    const Clock::time_point received_at = Clock::now();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if(valid && sequence < send_times_.size() &&
         send_times_[sequence] != Clock::time_point()) {
        latencies_us_.push_back(std::chrono::duration<double, std::micro>(
            received_at - send_times_[sequence]).count());
        ++success_;
      } else {
        ++failures_;
      }
      subscriber_cpu_ms_ += ProcessCpuMilliseconds() - cpu_before;
    }
    condition_.notify_all();
  }

  bool WaitFor(uint32_t expected, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, timeout, [&]() {
      return success_ + failures_ >= expected;
    });
  }

  void AddFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++failures_;
    condition_.notify_all();
  }

  void ResetMeasurements() {
    std::lock_guard<std::mutex> lock(mutex_);
    latencies_us_.clear();
    success_ = 0;
    failures_ = 0;
    subscriber_cpu_ms_ = 0.0;
  }

  uint32_t success() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return success_;
  }
  uint32_t failures() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failures_;
  }
  double subscriber_cpu_ms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriber_cpu_ms_;
  }
  std::vector<double> latencies() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latencies_us_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<Clock::time_point> send_times_;
  std::vector<double> latencies_us_;
  uint32_t success_ = 0;
  uint32_t failures_ = 0;
  double subscriber_cpu_ms_ = 0.0;
};

double Percentile(std::vector<double> values, double percentile) {
  if(values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t index = static_cast<std::size_t>(std::ceil(
      percentile * static_cast<double>(values.size()))) - 1;
  return values[std::min(index, values.size() - 1)];
}

Result FinishResult(const std::string& mode, std::size_t payload_size,
                    uint32_t iterations, const Collector& collector,
                    Clock::time_point start, Clock::time_point finish,
                    double publisher_cpu_start) {
  Result result;
  result.payload_size = payload_size;
  result.mode = mode;
  result.iterations = iterations;
  result.success = collector.success();
  result.failures = collector.failures();
  result.elapsed_ms = std::chrono::duration<double, std::milli>(finish - start).count();
  if(result.elapsed_ms > 0.0) {
    result.throughput_mib_s =
        result.success * payload_size / (1024.0 * 1024.0) / (result.elapsed_ms / 1000.0);
    result.message_rate = result.success / (result.elapsed_ms / 1000.0);
  }
  const auto latencies = collector.latencies();
  result.p50_us = Percentile(latencies, 0.50);
  result.p95_us = Percentile(latencies, 0.95);
  result.p99_us = Percentile(latencies, 0.99);
  result.publisher_cpu_ms = ProcessCpuMilliseconds() - publisher_cpu_start;
  result.subscriber_cpu_ms = collector.subscriber_cpu_ms();
  return result;
}

template <typename Send>
void RunIterations(uint32_t warmup, uint32_t iterations, Collector* collector,
                   const Send& send) {
  for(uint32_t sequence = 1; sequence <= warmup; ++sequence) {
    send(sequence, false);
    collector->WaitFor(sequence, std::chrono::milliseconds(5000));
  }
  collector->ResetMeasurements();
  for(uint32_t sequence = 1; sequence <= iterations; ++sequence) {
    send(warmup + sequence, true);
    if(!collector->WaitFor(sequence, std::chrono::milliseconds(5000))) {
      collector->AddFailure();
    }
  }
}

Result RunLoaned(const std::string& mode, config::OptionalMode optional_mode,
                 std::size_t payload_size, uint32_t iterations) {
  const std::string channel = "loaned_bench_" + mode + "_" +
      std::to_string(getpid()) + "_" +
      std::to_string(Clock::now().time_since_epoch().count());
  const RoleAttributes publisher_attr = MakeRoleAttributes(channel, "_pub", payload_size);
  const RoleAttributes subscriber_attr = MakeRoleAttributes(channel, "_sub", payload_size);
  const uint32_t warmup = std::min<uint32_t>(10, iterations);
  Collector collector(warmup + iterations);

  std::shared_ptr<Transmitter<LoanedMessage>> transmitter;
  std::shared_ptr<Receiver<LoanedMessage>> receiver;
  if(optional_mode == config::OptionalMode::INTRA) {
    transmitter = std::make_shared<IntraTransmitter<LoanedMessage>>(publisher_attr);
    receiver = std::make_shared<IntraReceiver<LoanedMessage>>(
        subscriber_attr, [&](const std::shared_ptr<LoanedMessage>& message,
                              const MessageInfo&, const RoleAttributes&) {
          collector.Receive(message->data(), message->size());
        });
  } else if(optional_mode == config::OptionalMode::SHM) {
    transmitter = std::make_shared<ShmTransmitter<LoanedMessage>>(publisher_attr);
    receiver = std::make_shared<ShmReceiver<LoanedMessage>>(
        subscriber_attr, [&](const std::shared_ptr<LoanedMessage>& message,
                              const MessageInfo&, const RoleAttributes&) {
          collector.Receive(message->data(), message->size());
        });
  } else {
    receiver = Transport::Instance()->CreateReceiver<LoanedMessage>(
        subscriber_attr, [&](const std::shared_ptr<LoanedMessage>& message,
                              const MessageInfo&, const RoleAttributes&) {
          collector.Receive(message->data(), message->size());
        }, config::OptionalMode::RTPS);
    transmitter = Transport::Instance()->CreateTransmitter<LoanedMessage>(
        publisher_attr, config::OptionalMode::RTPS);
  }
  receiver->Enable();
  transmitter->Enable();

  const double cpu_start = ProcessCpuMilliseconds();
  const Clock::time_point start = Clock::now();
  RunIterations(warmup, iterations, &collector,
      [&](uint32_t sequence, bool measured) {
        auto message = transmitter->AcquireLoanedMessage(payload_size);
        if(message == nullptr || (payload_size != 0 && message->mutable_data() == nullptr)) {
          collector.AddFailure();
          return;
        }
        FillPayload(message->mutable_data(), payload_size, sequence);
        if(!message->set_size(payload_size)) {
          collector.AddFailure();
          return;
        }
        (void)measured;
        collector.MarkSent(sequence);
        if(!transmitter->TransmitLoanedMessage(std::move(message))) {
          collector.AddFailure();
        }
      });
  const Clock::time_point finish = Clock::now();
  transmitter->Disable();
  receiver->Disable();
  return FinishResult(mode, payload_size, iterations, collector, start, finish, cpu_start);
}

Result RunShmCopy(std::size_t payload_size, uint32_t iterations) {
  const std::string channel = "shm_copy_bench_" + std::to_string(getpid()) + "_" +
      std::to_string(Clock::now().time_since_epoch().count());
  const RoleAttributes publisher_attr = MakeRoleAttributes(channel, "_pub", payload_size);
  const RoleAttributes subscriber_attr = MakeRoleAttributes(channel, "_sub", payload_size);
  const uint32_t warmup = std::min<uint32_t>(10, iterations);
  Collector collector(warmup + iterations);
  ShmReceiver<CopyMessage> receiver(
      subscriber_attr, [&](const std::shared_ptr<CopyMessage>& message,
                            const MessageInfo&, const RoleAttributes&) {
        collector.Receive(reinterpret_cast<const uint8_t*>(message->payload.data()),
                          message->payload.size());
      });
  ShmTransmitter<CopyMessage> transmitter(publisher_attr);
  receiver.Enable();
  transmitter.Enable();

  const double cpu_start = ProcessCpuMilliseconds();
  const Clock::time_point start = Clock::now();
  RunIterations(warmup, iterations, &collector,
      [&](uint32_t sequence, bool) {
        auto message = std::make_shared<CopyMessage>();
        message->sequence = sequence;
        message->payload.resize(payload_size);
        FillPayload(reinterpret_cast<uint8_t*>(&message->payload[0]), payload_size, sequence);
        collector.MarkSent(sequence);
        if(!static_cast<Transmitter<CopyMessage>&>(transmitter).Transmit(message)) {
          collector.AddFailure();
        }
      });
  const Clock::time_point finish = Clock::now();
  transmitter.Disable();
  receiver.Disable();
  return FinishResult("SHM-copy", payload_size, iterations, collector, start, finish,
                      cpu_start);
}

uint32_t IterationsFor(std::size_t payload_size, bool quick) {
  if(quick) {
    return 3;
  }
  if(payload_size <= 4 * 1024) return 200;
  if(payload_size <= 64 * 1024) return 100;
  if(payload_size <= 1024 * 1024) return 30;
  if(payload_size <= 8 * 1024 * 1024) return 10;
  return 5;
}

void PrintHeader(std::ostream& output) {
  output << "size_bytes,mode,iterations,success,failures,elapsed_ms,throughput_mib_s,"
         << "message_rate,p50_us,p95_us,p99_us,publisher_cpu_ms,subscriber_cpu_ms\n";
}

void PrintResult(std::ostream& output, const Result& result) {
  output << result.payload_size << ',' << result.mode << ',' << result.iterations << ','
         << result.success << ',' << result.failures << ',' << std::fixed
         << std::setprecision(3) << result.elapsed_ms << ',' << result.throughput_mib_s
         << ',' << result.message_rate << ',' << result.p50_us << ',' << result.p95_us
         << ',' << result.p99_us << ',' << result.publisher_cpu_ms << ','
         << result.subscriber_cpu_ms << '\n';
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  hnu::cmw::Init("ShmZeroCopyBenchmark");
  const std::vector<std::size_t> sizes = {
      4 * 1024, 64 * 1024, 1024 * 1024, 8 * 1024 * 1024, 16 * 1024 * 1024};
  std::ofstream csv("build/shm_zero_copy_benchmark.csv");
  hnu::cmw::transport::PrintHeader(std::cout);
  hnu::cmw::transport::PrintHeader(csv);
  for(std::size_t size : sizes) {
    const uint32_t iterations = hnu::cmw::transport::IterationsFor(size, quick);
    // Payload fill occurs before the timing mark; the reported interval starts
    // at Publish and ends at the receiving callback.
    std::vector<hnu::cmw::transport::Result> results;
    results.push_back(hnu::cmw::transport::RunLoaned(
        "INTRA", hnu::cmw::config::OptionalMode::INTRA, size, iterations));
    results.push_back(hnu::cmw::transport::RunShmCopy(size, iterations));
    results.push_back(hnu::cmw::transport::RunLoaned(
        "SHM-loaned", hnu::cmw::config::OptionalMode::SHM, size, iterations));
    try {
      results.push_back(hnu::cmw::transport::RunLoaned(
          "RTPS", hnu::cmw::config::OptionalMode::RTPS, size, iterations));
    } catch(const std::exception& error) {
      hnu::cmw::transport::Result unavailable;
      unavailable.payload_size = size;
      unavailable.mode = "RTPS";
      unavailable.iterations = iterations;
      unavailable.failures = iterations;
      std::cerr << "RTPS benchmark unavailable: " << error.what() << '\n';
      results.push_back(unavailable);
    }
    for(const auto& result : results) {
      hnu::cmw::transport::PrintResult(std::cout, result);
      hnu::cmw::transport::PrintResult(csv, result);
    }
  }
  try {
    hnu::cmw::transport::Transport::Instance()->Shutdown();
  } catch(const std::exception& error) {
    std::cerr << "RTPS cleanup unavailable: " << error.what() << '\n';
  }
  std::cout << "CSV: example/build/shm_zero_copy_benchmark.csv\n";
  return 0;
}
