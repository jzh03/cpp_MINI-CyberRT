
#include <cmw/discovery/specific_manager/manager.h>
#include <exception>
#include <limits>
#include <cmw/common/global_data.h>
#include <cmw/transport/rtps/attributes_filler.h>
#include <fastrtps/rtps/RTPSDomain.h>
#include <cmw/transport/rtps/attributes_filler.h>
#include <cmw/transport/qos/qos_profile_conf.h>
#include <cmw/transport/rtps/participant.h>
#include <fastrtps/rtps/reader/RTPSReader.h>
#include <cmw/common/log.h>
#include <cmw/time/time.h>
#include <cmw/serialize/data_stream.h>
#include <cmw/common/log.h>

namespace hnu {
namespace cmw {
namespace discovery{ 

using namespace transport;
Manager::Manager()
    : is_shutdown_(false),
      is_discovery_started_(false),
      allowed_role_(0),
      change_type_(ChangeType::CHANGE_PARTICIPANT),
      channel_name_(""),
      writer_(nullptr),
      writer_history_(nullptr),
      reader_history_(nullptr),
      reader_(nullptr),
      listener_(nullptr) {
  host_name_ = common::GlobalData::Instance()->HostName();
  process_id_ = common::GlobalData::Instance()->ProcessId();
}

Manager::~Manager() { Shutdown(); }

//开启服务发现机制
bool Manager::StartDiscovery(RtpsParticipant* participant){
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (is_shutdown_.load()) {
        AERROR << "cannot restart a shutdown discovery manager";
        return false;
    }
    if (participant == nullptr) {
        return false;
    }
    //将is_discovery_started_标志位设置为true
    if (is_discovery_started_.exchange(true)) {
        return true;
    }

    bool created = false;
    try {
        created = CreateWriter(participant) && CreateReader(participant);
    } catch (const std::exception& error) {
        AERROR << "exception while creating discovery endpoints: " << error.what();
    } catch (...) {
        AERROR << "unknown exception while creating discovery endpoints";
    }
    if(!created){
        AERROR << "create writer or reader failed.";
        StopDiscoveryLocked();
        return false;
    }
    return true;
}

//终止服务发现机制
void Manager::StopDiscovery(){
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    StopDiscoveryLocked();
}

void Manager::StopDiscoveryLocked() {
    if (!is_discovery_started_.exchange(false)) {
        return;
    }

    // Stop and drain user callbacks before removing any resource they access.
    if (listener_ != nullptr) listener_->Stop();
    if(reader_ != nullptr){
        eprosima::fastrtps::rtps::RTPSDomain::removeRTPSReader(reader_);
        reader_ = nullptr;
    }
    reader_history_.reset();
    listener_.reset();

    //为什么移除writer需要加锁
    {
        std::lock_guard<std::mutex> lg(lock_);
        if(writer_ != nullptr){
            //从rtpsRTPSDomain 中移除writer
            eprosima::fastrtps::rtps::RTPSDomain::removeRTPSWriter(writer_);
            writer_ = nullptr;
        }
        writer_history_.reset();
    }
}

void Manager::Shutdown(){
    if(is_shutdown_.exchange(true)){
        return;
    }
    StopDiscovery();
    //信号槽取消对槽函数的绑定
    signal_.DisconnectAllSlots();
}


/*加入拓扑网络*/
bool Manager::Join(const RoleAttributes& attr, RoleType role,
                  bool need_write ){
    if(is_shutdown_.load()){
        ADEBUG << "the manager has been shut down.";
        return false;
    }    

    //判断是否是允许的role
    RETURN_VAL_IF(!((1 << role) & allowed_role_), false);
    //判断配置信息是否为空
    RETURN_VAL_IF(!Check(attr), false);
    //创建一个msg
    ChangeMsg msg;
    //填充msg
    Convert(attr, role, OperateType::OPT_JOIN, &msg);
    //处理msg
    if (!Dispose(msg)) return false;
    //广播msg
    if (need_write) {
        return Write(msg);
    }
    return true;
}

/*离开拓扑网络*/
bool Manager::Leave(const RoleAttributes& attr, RoleType role){
    if(is_shutdown_.load()){
        ADEBUG << "the manager has been shut down.";
        return false;
    }   
    RETURN_VAL_IF(!((1 << role) & allowed_role_), false);
    RETURN_VAL_IF(!Check(attr), false);
    ChangeMsg msg;
    Convert(attr, role, OperateType::OPT_LEAVE, &msg);
    if (!Dispose(msg)) return false;
    if (NeedPublish(msg)) {
        return Write(msg);
    }
  return true;
}

//添加回调函数，绑定信号槽
Manager::ChangeConnection Manager::AddChangeListener(const ChangeFunc& func){
   return  signal_.Connect(func);
}

//移除回调函数，
void Manager::RemoveChangeListener(const ChangeConnection& conn) {
  auto local_conn = conn;
  //信号内部会删除此槽函数
  local_conn.Disconnect();
}

//创建rtspReader
bool Manager::CreateReader(RtpsParticipant* participant){

    RtpsReaderAttributes reader_attr;

    if (!AttributesFiller::FillInReaderAttr(
                channel_name_, QosProfileConf::QOS_PROFILE_TOPO_CHANGE, &reader_attr))
      return false;

    auto listener = std::unique_ptr<ReaderListener>(new ReaderListener(
            std::bind(&Manager::OnRemoteChange, this , std::placeholders::_1)));
    auto history = std::unique_ptr<QosReaderHistory>(new QosReaderHistory(
        reader_attr.hatt, QosProfileConf::QOS_PROFILE_TOPO_CHANGE));

    auto* reader = RTPSDomain::createRTPSReader(participant, reader_attr.ratt,
                                                history.get(), listener.get());
    if (!reader) return false;

    bool registered = false;
    try {
        registered = participant->registerReader(
            reader, reader_attr.Tatt, reader_attr.Rqos);
    } catch (...) {
        RTPSDomain::removeRTPSReader(reader);
        throw;
    }
    if (!registered) {
        RTPSDomain::removeRTPSReader(reader);
        return false;
    }
    reader_ = reader;
    reader_history_ = std::move(history);
    listener_ = std::move(listener);
    return true;
}

//创建rtpsWriter
bool Manager::CreateWriter(RtpsParticipant* participant){
    // 创建 RtpsWriter 的配置信息实例
    RtpsWriterAttributes writer_attr; 
    // 填充 RtpsWriter 的配置信息
    if (!AttributesFiller::FillInWriterAttr(
        channel_name_, QosProfileConf::QOS_PROFILE_TOPO_CHANGE, &writer_attr))
      return false;
    
    //创建rtps writer history
    auto history = std::unique_ptr<QosWriterHistory>(new QosWriterHistory(
        writer_attr.hatt, QosProfileConf::QOS_PROFILE_TOPO_CHANGE));
    //创建rtps writer
    auto* writer = RTPSDomain::createRTPSWriter(
        participant, writer_attr.watt, history.get());
    if (!writer) return false;
    //注册rtps writer
    bool registered = false;
    try {
        registered = participant->registerWriter(
            writer, writer_attr.Tatt, writer_attr.Wqos);
    } catch (...) {
        RTPSDomain::removeRTPSWriter(writer);
        throw;
    }
    if (!registered) {
        RTPSDomain::removeRTPSWriter(writer);
        return false;
    }
    writer_ = writer;
    writer_history_ = std::move(history);
    return true;
}

bool Manager::NeedPublish(const ChangeMsg& msg) const {
  (void)msg;
  return true;
}


void Manager::OnRemoteChange(const std::string& str_msg){
    if(is_shutdown_.load() || !is_discovery_started_.load()){
        ADEBUG <<  "the manager has been shut down.";
        return;
    }


    ChangeMsg msg;

    serialize::DataStream ds(str_msg);
    //需要将str_msg 反序列化成ChangeMsg类型的msg
    if (!ds.read(msg) || !config::NormalizeQosProfile(
            msg.role_attr.qos_profile, &msg.role_attr.qos_profile)) {
        AERROR << "Invalid or incompatible Discovery metadata";
        return;
    }

    //判断是否是同一进程
    if(IsFromSameProcess(msg)){
        ADEBUG << "FromSameProcess";
        return;
    }


    RETURN_IF(!Check(msg.role_attr));

    (void)Dispose(msg);
    

}


//填充msg
void Manager::Convert(const RoleAttributes& attr, RoleType role, OperateType opt,
               ChangeMsg* msg){
    
    msg->timestamp = cmw::Time::Now().ToNanosecond();  //时间戳为ns
    msg->change_type = change_type_;
    msg->operate_type = opt;
    msg->role_type = role;

    msg->role_attr = attr;

    if(msg->role_attr.host_name.empty()){
        msg->role_attr.host_name = host_name_;
    }
    if(!msg->role_attr.process_id){
        msg->role_attr.process_id = process_id_;
    }       
}

//槽函数通知执行回调
void Manager::Notify(const ChangeMsg& msg) { signal_(msg); }

//判断是否是同一进程
bool Manager::IsFromSameProcess(const ChangeMsg& msg){
    auto& host_name = msg.role_attr.host_name;
    int process_id = msg.role_attr.process_id;

    if (process_id != process_id_ || host_name != host_name_) {
        return false;
    }
    return true;
}


bool Manager::Write(const ChangeMsg& msg){
//使用eprosima::fastrtps::rtps::RTPSWriter* writer_ 发布数据
  
  //判断discovery是否启动了
  if(!is_discovery_started_.load()){
    ADEBUG << "discovery is not started.";
    return false;
  }

  //将ChangeMsg序列化
  serialize::DataStream ds;
  ds << msg;
  
  const size_t size = ds.size();
  if(size > std::numeric_limits<uint32_t>::max()) return false;
  std::lock_guard<std::mutex> lg(lock_);
  if(!is_discovery_started_.load() || writer_ == nullptr ||
     writer_history_ == nullptr) return false;

  // Topology strings have variable length; 255 bytes is not an upper bound.
  CacheChange_t* change = writer_->new_change(
      [size]() { return static_cast<uint32_t>(size); }, ALIVE);
  if(change == nullptr) return false;
  if(change->serializedPayload.data == nullptr ||
     change->serializedPayload.max_size < size) {
    writer_->release_change(change);
    return false;
  }
  change->serializedPayload.length = static_cast<uint32_t>(size);
  std::memcpy(change->serializedPayload.data, ds.data(), size);
  eprosima::fastrtps::rtps::WriteParams params;
  bool added = writer_history_->AddChange(change, params);
  if(!added) {
    writer_->release_change(change);
    AERROR << "Discovery history full or submission failed: " << channel_name_;
  }
  return added;
}


}
}
}
