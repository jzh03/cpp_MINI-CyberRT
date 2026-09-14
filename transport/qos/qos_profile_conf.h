#ifndef CMW_TRANSPORT_QOS_QOS_PROFILE_CONF_H_
#define CMW_TRANSPORT_QOS_QOS_PROFILE_CONF_H_

#include <cmw/config/qos_profile.h>
namespace hnu    {
namespace cmw   {
namespace transport {

using namespace config;
class QosProfileConf {
public:
    static const QosProfile QOS_PROFILE_DEFAULT;
    // Discovery keeps announcements for processes that start later.
    static const QosProfile QOS_PROFILE_TOPO_CHANGE;
};

}
}
}

#endif
