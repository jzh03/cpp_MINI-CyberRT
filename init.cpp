#include <cmw/init.h>
#include <cmw/common/log.h>
#include <string>
#include <cmw/common/global_data.h>

namespace hnu {
namespace cmw {


bool Init(const char* binary_name){
    //初始化日志
    std::string logfile_name = (std::string)binary_name + ".log";
    Logger_Init(logfile_name);

    //初始化 global_data
    auto global_data = common::GlobalData::Instance();

    return true;
}

std::unique_ptr<Node> CreateNode(const std::string& node_name,
                                 const std::string& name_space) {
    if (node_name.empty()) {
        AERROR << "Cannot create a Node with an empty name";
        return nullptr;
    }
    std::unique_ptr<Node> node(new Node(node_name, name_space));
    if (!node->IsValid()) {
        AERROR << "Cannot create Node because topology initialization failed: "
               << node_name;
        return nullptr;
    }
    return node;
}

}
}
