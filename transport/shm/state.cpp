
#include <cmw/transport/shm/state.h>

namespace hnu{
namespace cmw{
namespace transport{

State::State(const uint64_t& ceiling_msg_size)
    : reference_count_(1), ceiling_msg_size_(ceiling_msg_size) {}

State::~State() {}
}
}
}
