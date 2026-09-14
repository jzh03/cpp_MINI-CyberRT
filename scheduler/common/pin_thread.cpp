#include <cmw/scheduler/common/pin_thread.h>

#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstring>
#include <sstream>

namespace hnu {
namespace cmw {
namespace scheduler {
namespace {
bool CpuNumber(const std::string& input, int* cpu) {
    const auto begin = input.find_first_not_of(" \t");
    if (begin == std::string::npos) return false;
    const auto end = input.find_last_not_of(" \t");
    int value = 0;
    for (size_t i = begin; i <= end; ++i) {
        const unsigned char ch = input[i];
        if (!std::isdigit(ch)) return false;
        value = value * 10 + (ch - '0');
        if (value >= CPU_SETSIZE) return false;
    }
    *cpu = value;
    return true;
}
}

bool ParseCpuset(const std::string& str, std::vector<int>* cpuset) {
    if (!cpuset) return false;
    if (str.empty()) return true;
    std::vector<int> parsed;
    std::istringstream stream(str);
    std::string part;
    bool valid = str.back() != ',';
    while (valid && std::getline(stream, part, ',')) {
        const auto dash = part.find('-');
        int first = 0, last = 0;
        if (dash == std::string::npos) {
            valid = CpuNumber(part, &first);
            last = first;
        } else {
            valid = CpuNumber(part.substr(0, dash), &first) &&
                    CpuNumber(part.substr(dash + 1), &last) && first <= last;
        }
        if (valid) {
            for (int cpu = first; cpu <= last; ++cpu) parsed.push_back(cpu);
        }
    }
    if (!valid) {
        AERROR << "Invalid CPU set: " << str;
        return false;
    }
    cpuset->insert(cpuset->end(), parsed.begin(), parsed.end());
    return true;
}

bool SetSchedAffinity(std::thread* thread, const std::vector<int>& cpus,
                      const std::string& affinity, int cpu_id) {
    if (!thread || !thread->joinable()) return false;
    if (cpus.empty()) return true;
    for (int cpu : cpus) {
        if (cpu < 0 || cpu >= CPU_SETSIZE) {
            AERROR << "CPU id outside cpu_set_t: " << cpu;
            return false;
        }
    }
    cpu_set_t requested;
    CPU_ZERO(&requested);
    if (affinity == "range") {
        for (int cpu : cpus) CPU_SET(cpu, &requested);
    } else if (affinity == "1to1") {
        if (cpu_id < 0 || static_cast<size_t>(cpu_id) >= cpus.size()) {
            AERROR << "Invalid 1to1 CPU index: " << cpu_id;
            return false;
        }
        CPU_SET(cpus[cpu_id], &requested);
    } else {
        AERROR << "Invalid affinity policy: " << affinity;
        return false;
    }
    const int result = pthread_setaffinity_np(
        thread->native_handle(), sizeof(requested), &requested);
    if (result != 0) {
        AERROR << "pthread_setaffinity_np failed: " << std::strerror(result);
        return false;
    }
    // Linux can silently intersect the mask with online/cgroup-allowed CPUs.
    cpu_set_t actual;
    CPU_ZERO(&actual);
    const int read_result = pthread_getaffinity_np(
        thread->native_handle(), sizeof(actual), &actual);
    if (read_result != 0 || !CPU_EQUAL(&requested, &actual)) {
        AERROR << "Requested affinity was not applied completely; read status: "
               << read_result;
        return false;
    }
    AINFO << "thread " << thread->get_id() << " set " << affinity << " affinity";
    return true;
}

bool SetSchedPolicy(std::thread* thread, const std::string& spolicy,
                    int sched_priority, pid_t tid) {
    if (!thread || !thread->joinable()) return false;
    if (spolicy.empty()) return true;
    int policy = SCHED_OTHER;
    sched_param parameter{};
    if (spolicy == "SCHED_FIFO" || spolicy == "SCHED_RR") {
        policy = spolicy == "SCHED_FIFO" ? SCHED_FIFO : SCHED_RR;
        const int minimum = sched_get_priority_min(policy);
        const int maximum = sched_get_priority_max(policy);
        if (minimum == -1 || maximum == -1 ||
            sched_priority < minimum || sched_priority > maximum) {
            AERROR << "Invalid " << spolicy << " priority: " << sched_priority;
            return false;
        }
        parameter.sched_priority = sched_priority;
    } else if (spolicy == "SCHED_OTHER") {
        if (tid <= 0 && pthread_equal(thread->native_handle(), pthread_self()))
            tid = static_cast<pid_t>(syscall(SYS_gettid));
        if (tid <= 0 || sched_priority < -20 || sched_priority > 19) {
            AERROR << "SCHED_OTHER requires target Linux tid and nice in [-20,19]";
            return false;
        }
    } else {
        AERROR << "Invalid scheduler policy: " << spolicy;
        return false;
    }
    const int result = pthread_setschedparam(thread->native_handle(), policy, &parameter);
    if (result != 0) {
        AERROR << "pthread_setschedparam " << spolicy << " failed: "
               << std::strerror(result);
        return false;
    }
    if (policy == SCHED_OTHER && setpriority(PRIO_PROCESS, tid, sched_priority) != 0) {
        AERROR << "setpriority tid=" << tid << " failed: " << std::strerror(errno);
        return false;
    }
    AINFO << "thread " << thread->get_id() << " set sched_policy: " << spolicy
          << " priority: " << sched_priority;
    return true;
}
}
}
}
