#include <cmw/transport/qos/qos_profile_conf.h>


namespace hnu    {
namespace cmw   {
namespace transport {

const QosProfile QosProfileConf::QOS_PROFILE_DEFAULT = QosProfile();

const QosProfile QosProfileConf::QOS_PROFILE_TOPO_CHANGE = [] {
  QosProfile qos;
  qos.history = HISTORY_KEEP_ALL;
  qos.reliability = RELIABILITY_RELIABLE;
  qos.durability = DURABILITY_TRANSIENT_LOCAL;
  return qos;
}();

    
}
}
}
