#ifndef CMW_CONFIG_TRANSPORT_MODE_H_
#define CMW_CONFIG_TRANSPORT_MODE_H_

#include <cmw/config/RoleAttributes.h>
#include <cmw/config/transport_config.h>

namespace hnu {
namespace cmw {
namespace config {

// 根据 Discovery 携带的主机 IP 和进程 ID，选择双方唯一对应的传输模式。
inline OptionalMode SelectMode(const RoleAttributes& local,
                               const RoleAttributes& opposite) {
  if (local.host_ip == opposite.host_ip) {
    if (local.process_id == opposite.process_id) {
      return OptionalMode::INTRA;
    }
    return OptionalMode::SHM;
  }
  return OptionalMode::RTPS;
}

}
}
}

#endif
