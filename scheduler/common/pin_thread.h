

#ifndef CMW_SCHEDULER_COMMON_PIN_THREAD_H_
#define CMW_SCHEDULER_COMMON_PIN_THREAD_H_

#include <string>
#include <thread>
#include <vector>

#include <cmw/common/log.h>

namespace hnu    {
namespace cmw   {
namespace scheduler {

// Invalid input leaves the output unchanged. Empty input means no restriction.
bool ParseCpuset(const std::string& str, std::vector<int>* cpuset);

bool SetSchedAffinity(std::thread* thread, const std::vector<int>& cpus,
                      const std::string& affinity, int cpu_id = -1);

// SCHED_OTHER uses a Linux nice value and requires the target thread's Linux
// tid (pthread_t is not a tid). Empty policy leaves settings unchanged.
// Calls must finish before the target thread exits or is joined.
bool SetSchedPolicy(std::thread* thread, const std::string& spolicy,
                    int sched_priority, pid_t tid = -1);

}
}
}

#endif
