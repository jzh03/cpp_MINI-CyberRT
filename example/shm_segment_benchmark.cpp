#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmw/transport/shm/posix_segment.h>
#include <cmw/transport/shm/xsi_segment.h>

namespace hnu {
namespace cmw {
namespace transport {
namespace {

const uint32_t kRounds = 3;

struct BenchmarkCase {
    const char* backend;
    uint64_t message_size;
    uint64_t iterations;
};

struct SharedControl {
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};
    std::atomic<uint32_t> block_index{0};
    std::atomic<uint32_t> errors{0};
    std::atomic<bool> stop{false};
};

struct RoundResult {
    double elapsed_us = 0.0;
    uint64_t errors = 0;
    bool completed = false;
};

bool WriteFully(int fd, const void* data, std::size_t size)
{
    const char* bytes = static_cast<const char*>(data);
    while(size > 0){
        const ssize_t written = write(fd, bytes, size);
        if(written <= 0){
            return false;
        }
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool ReadFully(int fd, void* data, std::size_t size)
{
    char* bytes = static_cast<char*>(data);
    while(size > 0){
        const ssize_t read_size = read(fd, bytes, size);
        if(read_size <= 0){
            return false;
        }
        bytes += read_size;
        size -= static_cast<std::size_t>(read_size);
    }
    return true;
}

bool WaitForValue(const std::atomic<uint64_t>& value, uint64_t expected,
                  const SharedControl* control)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while(value.load(std::memory_order_acquire) < expected){
        if(control->stop.load(std::memory_order_acquire) ||
           std::chrono::steady_clock::now() >= deadline){
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

std::unique_ptr<Segment> CreateSegment(const std::string& backend,
                                       uint64_t channel_id)
{
    if(backend == PosixSegment::Type()){
        return std::unique_ptr<Segment>(new PosixSegment(channel_id));
    }
    return std::unique_ptr<Segment>(new XsiSegment(channel_id));
}

bool PrepareWriter(Segment* segment, uint64_t message_size)
{
    WritableBlock writable_block;
    if(!segment->AcquireBlockToWrite(message_size, &writable_block)){
        return false;
    }
    segment->ReleaseWrittenBlock(writable_block);
    return true;
}

bool PrepareReader(Segment* segment)
{
    ReadableBlock readable_block;
    readable_block.index = 0;
    if(!segment->AcquireBlockToRead(&readable_block)){
        return false;
    }
    segment->ReleaseReadBlock(readable_block);
    return true;
}

bool TransferMessages(Segment* segment, SharedControl* control,
                      const std::vector<uint8_t>& payload,
                      uint64_t start_sequence, uint64_t count)
{
    for(uint64_t offset = 0; offset < count; ++offset){
        const uint64_t sequence = start_sequence + offset;
        if(!WaitForValue(control->consumed, sequence, control)){
            control->stop.store(true, std::memory_order_release);
            return false;
        }

        WritableBlock writable_block;
        if(!segment->AcquireBlockToWrite(payload.size(), &writable_block) ||
           writable_block.buf == nullptr){
            control->errors.fetch_add(1, std::memory_order_relaxed);
            control->stop.store(true, std::memory_order_release);
            return false;
        }
        std::memcpy(writable_block.buf, payload.data(), payload.size());
        segment->ReleaseWrittenBlock(writable_block);

        control->block_index.store(writable_block.index,
                                   std::memory_order_release);
        control->produced.store(sequence + 1, std::memory_order_release);
    }
    return WaitForValue(control->consumed, start_sequence + count, control);
}

int RunReader(const std::string& backend, uint64_t channel_id,
              uint64_t message_size, uint64_t total_iterations,
              SharedControl* control, int ready_write_fd)
{
    std::unique_ptr<Segment> reader = CreateSegment(backend, channel_id);
    if(!PrepareReader(reader.get()) || !WriteFully(ready_write_fd, "R", 1)){
        control->errors.fetch_add(1, std::memory_order_relaxed);
        control->stop.store(true, std::memory_order_release);
        return 1;
    }

    const uint8_t expected = static_cast<uint8_t>(message_size & 0xff);
    for(uint64_t sequence = 0; sequence < total_iterations; ++sequence){
        if(!WaitForValue(control->produced, sequence + 1, control)){
            control->errors.fetch_add(1, std::memory_order_relaxed);
            control->stop.store(true, std::memory_order_release);
            return 2;
        }

        ReadableBlock readable_block;
        readable_block.index =
            control->block_index.load(std::memory_order_acquire);
        if(!reader->AcquireBlockToRead(&readable_block) ||
           readable_block.buf == nullptr){
            control->errors.fetch_add(1, std::memory_order_relaxed);
            control->stop.store(true, std::memory_order_release);
            return 3;
        }

        if(readable_block.buf[0] != expected ||
           readable_block.buf[message_size / 2] != expected ||
           readable_block.buf[message_size - 1] != expected){
            control->errors.fetch_add(1, std::memory_order_relaxed);
        }
        reader->ReleaseReadBlock(readable_block);
        control->consumed.store(sequence + 1, std::memory_order_release);
    }
    return 0;
}

RoundResult RunRound(const BenchmarkCase& benchmark_case,
                     uint64_t channel_id)
{
    RoundResult result;
    std::unique_ptr<Segment> writer =
        CreateSegment(benchmark_case.backend, channel_id);
    if(!PrepareWriter(writer.get(), benchmark_case.message_size)){
        result.errors = 1;
        return result;
    }

    void* shared_memory = mmap(nullptr, sizeof(SharedControl),
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(shared_memory == MAP_FAILED){
        result.errors = 1;
        return result;
    }
    SharedControl* control = new (shared_memory) SharedControl();

    int ready_pipe[2] = {-1, -1};
    if(pipe(ready_pipe) != 0){
        control->~SharedControl();
        munmap(shared_memory, sizeof(SharedControl));
        result.errors = 1;
        return result;
    }

    const uint64_t warmup_iterations =
        benchmark_case.message_size <= 64 * 1024 ? 32 : 4;
    const uint64_t total_iterations =
        warmup_iterations + benchmark_case.iterations;
    const pid_t reader = fork();
    if(reader == 0){
        close(ready_pipe[0]);
        const int reader_result = RunReader(
            benchmark_case.backend, channel_id, benchmark_case.message_size,
            total_iterations, control, ready_pipe[1]);
        close(ready_pipe[1]);
        _exit(reader_result);
    }
    close(ready_pipe[1]);
    char ready = 0;
    if(reader < 0 || !ReadFully(ready_pipe[0], &ready, 1) || ready != 'R'){
        close(ready_pipe[0]);
        if(reader > 0){
            kill(reader, SIGKILL);
            waitpid(reader, nullptr, 0);
        }
        control->~SharedControl();
        munmap(shared_memory, sizeof(SharedControl));
        result.errors = 1;
        return result;
    }
    close(ready_pipe[0]);

    std::vector<uint8_t> payload(benchmark_case.message_size,
                                 static_cast<uint8_t>(benchmark_case.message_size & 0xff));
    bool transferred = TransferMessages(writer.get(), control, payload, 0,
                                        warmup_iterations);
    const auto start = std::chrono::steady_clock::now();
    if(transferred){
        transferred = TransferMessages(writer.get(), control, payload,
                                       warmup_iterations,
                                       benchmark_case.iterations);
    }
    const auto finish = std::chrono::steady_clock::now();
    result.elapsed_us = std::chrono::duration_cast<std::chrono::duration<double,
        std::micro>>(finish - start).count();

    int status = 0;
    if(reader > 0){
        waitpid(reader, &status, 0);
    }
    result.errors = control->errors.load(std::memory_order_acquire);
    result.completed = transferred && reader > 0 && WIFEXITED(status) &&
                       WEXITSTATUS(status) == 0 && result.errors == 0;
    if(!result.completed && result.errors == 0){
        result.errors = 1;
    }

    control->~SharedControl();
    munmap(shared_memory, sizeof(SharedControl));
    return result;
}

RoundResult RunIsolatedRound(const BenchmarkCase& benchmark_case,
                             uint64_t channel_id)
{
    int result_pipe[2] = {-1, -1};
    RoundResult result;
    if(pipe(result_pipe) != 0){
        result.errors = 1;
        return result;
    }

    const pid_t worker = fork();
    if(worker == 0){
        close(result_pipe[0]);
        const RoundResult worker_result = RunRound(benchmark_case, channel_id);
        const bool written = WriteFully(result_pipe[1], &worker_result,
                                        sizeof(worker_result));
        close(result_pipe[1]);
        _exit(written ? 0 : 1);
    }
    close(result_pipe[1]);
    int status = 0;
    const bool read = worker > 0 && ReadFully(result_pipe[0], &result,
                                               sizeof(result));
    close(result_pipe[0]);
    if(worker <= 0 || waitpid(worker, &status, 0) != worker ||
       !WIFEXITED(status) || WEXITSTATUS(status) != 0 || !read){
        result.elapsed_us = 0.0;
        result.errors = 1;
        result.completed = false;
    }
    return result;
}

bool RunBenchmarkCase(const BenchmarkCase& benchmark_case, uint32_t case_index)
{
    std::vector<double> elapsed_us;
    uint64_t errors = 0;
    std::cout << "round | backend | message_size | iterations | elapsed_us | errors"
              << std::endl;
    for(uint32_t round = 0; round < kRounds; ++round){
        const uint64_t channel_id = 0x20000000ULL +
            ((static_cast<uint64_t>(getpid()) & 0xffffULL) << 6) +
            case_index * kRounds + round;
        const RoundResult result = RunIsolatedRound(benchmark_case, channel_id);
        std::cout << (round + 1) << " | " << benchmark_case.backend << " | "
                  << benchmark_case.message_size << " | "
                  << benchmark_case.iterations << " | " << std::fixed
                  << std::setprecision(2) << result.elapsed_us << " | "
                  << result.errors << std::endl;
        if(result.completed){
            elapsed_us.push_back(result.elapsed_us);
        }
        errors += result.errors;
    }

    if(elapsed_us.size() != kRounds){
        std::cout << benchmark_case.backend << " | "
                  << benchmark_case.message_size << " | "
                  << benchmark_case.iterations
                  << " | incomplete | incomplete | " << errors << std::endl;
        return false;
    }

    std::sort(elapsed_us.begin(), elapsed_us.end());
    const double median_elapsed_us = elapsed_us[elapsed_us.size() / 2];
    const double median_latency_us =
        median_elapsed_us / benchmark_case.iterations;
    const double throughput_mib_s =
        static_cast<double>(benchmark_case.message_size) *
        benchmark_case.iterations * 1000000.0 /
        (1024.0 * 1024.0 * median_elapsed_us);
    std::cout << benchmark_case.backend << " | "
              << benchmark_case.message_size << " | "
              << benchmark_case.iterations << " | " << std::fixed
              << std::setprecision(2) << median_latency_us << " | "
              << throughput_mib_s << " | " << errors << std::endl;
    return errors == 0;
}

}  // namespace
}  // namespace transport
}  // namespace cmw
}  // namespace hnu

int main()
{
    using hnu::cmw::transport::BenchmarkCase;
    using hnu::cmw::transport::RunBenchmarkCase;

    const std::vector<BenchmarkCase> benchmark_cases = {
        {"posix", 4 * 1024, 20000},
        {"xsi", 4 * 1024, 20000},
        {"posix", 64 * 1024, 5000},
        {"xsi", 64 * 1024, 5000},
        {"posix", 1024 * 1024, 256},
        {"xsi", 1024 * 1024, 256},
        {"posix", 8 * 1024 * 1024, 32},
        {"xsi", 8 * 1024 * 1024, 32},
    };

    std::cout << "backend | message_size | iterations | median_latency_us | "
              << "throughput_MiB_s | errors" << std::endl;
    bool success = true;
    for(uint32_t i = 0; i < benchmark_cases.size(); ++i){
        success = RunBenchmarkCase(benchmark_cases[i], i) && success;
    }
    return success ? 0 : 1;
}
