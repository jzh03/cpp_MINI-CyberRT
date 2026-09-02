#include <cmw/transport/dispatcher/intra_dispatcher.h>

namespace hnu {
namespace cmw {
namespace transport {

// IntraDispatcher 使用单例，保证同进程 Transmitter 和 Receiver 访问同一监听表。
IntraDispatcher::IntraDispatcher() {}

}
}
}
