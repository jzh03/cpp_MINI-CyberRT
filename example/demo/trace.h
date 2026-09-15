#ifndef MINI_CYBERRT_DEMO_TRACE_H_
#define MINI_CYBERRT_DEMO_TRACE_H_

#ifndef __ASSEMBLER__

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

// Force-included only by the isolated Demo build, in every translation unit.
// A single stdio call keeps each event intact across runtime threads.
inline void DemoRouteTrace(const char* mode, bool enabled,
                           const std::string& channel) {
  const char* trace = std::getenv("CMW_DEMO_TRACE");
  if (trace != nullptr && std::string(trace) == "1") {
    std::fprintf(stdout, "[ROUTE] backend=%s state=%s pid=%d channel=%s\n",
                 mode, enabled ? "ENABLED" : "DISABLED", getpid(),
                 channel.c_str());
    std::fflush(stdout);
  }
}
#define CMW_DEMO_ROUTE_TRACE(mode, enabled, channel) \
  DemoRouteTrace(mode, enabled, channel)

#endif
#endif
