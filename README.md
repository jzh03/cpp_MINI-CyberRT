# MINI_CyberRT

MINI_CyberRT 是针对原始 cmw 项目进行的二次开发，在保留原有 CyberRT 与 Fast DDS 中间件架构的基础上，进一步改善功能并增加测试覆盖。

原作者提供的项目视频：https://space.bilibili.com/281708692/lists/5849251?type=season

原作者提供的讲解文档：[飞书](https://ai.feishu.cn/drive/folder/PiqFfxWx5l9Ri2dds9WcI3ognCd?from=from_copylink)
## 文档导航

- [测试程序介绍与使用指南](example/TESTING.md)：测试范围、构建与运行方法、sanitizer 和验证边界。
- [测试执行日志](example/testlog.md)：完整实际命令、执行记录、结果和环境限制。
- [文档维护约定](AGENTS.md)：功能说明、测试指南和执行记录的归档规则。
## 对原项目的改进

### 序列化安全

- `DataStream` 在读取前校验剩余 buffer，非法长度或截断数据会进入失败状态，不会错误推进读位置。
- `vector<T>` 改为记录元素数量并逐元素序列化；基础类型使用 `memcpy`，避免未对齐访问和未定义行为。

### SHM 健壮性

- 明确限制 SHM 最大消息为 32 MiB，超限消息直接返回失败，不再继续 Recreate 或 `memcpy`。
- Segment Recreate 后再次校验实际 Block capacity，并保留原有的正常自动扩容。
- 所有 Block 被占用时最多扫描一轮后返回失败，避免无限 busy-spin；读 Block 失败时立即丢弃本次消息，不再访问无效内存。
- 默认 Segment 后端为 POSIX SHM（`shm_open` + `mmap`）；`OpenOnly()` 会为当前进程重新建立 Block buffer 地址表。XSI/System V 后端仍保留，可显式用于回归和性能对比。
- SHM 消息元信息中的序号统一通过 `memcpy` 编解码，Payload 长度未对齐时仍保持原有字段偏移、布局和字节序。

### LoanedMessage 零拷贝

- `Publisher<LoanedMessage>::AcquireMessage(capacity)` 提供连续 Payload；写入后调用 `set_size()`，再通过 `Publish(std::move(message))` 发送。
- 仅 SHM 路径返回 SHM-backed Loan，直接提交原 Block；不经过 `DataStream`、Payload 序列化或中间 `memcpy`。
- 只要活跃路径包含 INTRA 或 RTPS，Loan 使用 Heap-backed 存储：INTRA 共享同一 `shared_ptr`，SHM 复制一次到 Block，RTPS 仅发送 `uint32_t` 长度和 Payload bytes。
- 接收端 Loan 始终只读；SHM 接收端持有读 Lease，Heap 存储在最后一个 `shared_ptr` 释放时回收。
- SHM Transmitter 的 Enable、Disable、Acquire 与发送路径使用同一生命周期锁；关闭会等待已进入发送临界区的操作完成，关闭后新的 Acquire/发送安全失败。
- SHM-backed Loan 记录发送端的启用周期；Disable 后或 Disable→Enable 后提交旧 Loan 会被拒绝，重新获取的 Loan 可正常发送。Heap-backed Loan 不受旧 SHM 周期限制。

### API 正确性

- `Publisher::Publish()` 现在会正确返回底层 `Transmit()` 结果，避免非 `void` 函数无返回值的未定义行为。
- 单消息 Subscriber 协程在回调返回后、Yield 前释放自己的消息引用，避免停止协程直接回收栈时遗留 Loan/Lease；用户在回调中保存的 `shared_ptr` 不受影响。
- C++14 构建启用 `-faligned-new`，满足 Publish 必经的 PerfEventCache 内 BoundedQueue 的 64 字节对齐要求；修改 Makefile 后相关对象会重新编译。其他构建入口也须提供对齐分配支持。

### Discovery 驱动的混合传输

默认 Transport 已从 RTPS 改为 HYBRID，并补全以下自动选路链路：

```text
Discovery / ChannelManager
        ↓
RoleAttributes(host_ip, process_id, endpoint id)
        ↓
HybridTransmitter / HybridReceiver
        ↓
INTRA / SHM / RTPS
```

传输模式由 Discovery 提供的对端属性确定：

| 通信双方关系 | 自动选择模式 |
| --- | --- |
| 相同 `host_ip`、相同 `process_id` | INTRA |
| 相同 `host_ip`、不同 `process_id` | SHM |
| 不同 `host_ip` | RTPS |

主要实现包括：

- `config/transport_mode.h` 集中提供简单且可复用的模式判断，Hybrid 发送端和接收端使用同一份逻辑。
- 新增 `IntraTransmitter`、`IntraReceiver` 和 `IntraDispatcher`。同进程消息直接传递原始 `std::shared_ptr<MessageT>`，不序列化、不进入共享内存，也不经过 Fast DDS。
- 新增 `HybridTransmitter`，根据 Subscriber JOIN/LEAVE 按需创建并启用 INTRA、SHM、RTPS 子 Transport。
- 新增 `HybridReceiver`，根据 Publisher 属性把 listener 注册或注销操作转发给对应 Receiver。
- `RtpsTransmitter` 在 Disable 时会从 Fast DDS Participant 删除 Writer 并释放对应 `WriterHistory`，支持动态拓扑下重复 Enable/Disable。
- `Transport::CreateTransmitter()` 和 `CreateReceiver()` 明确支持 `HYBRID`、`INTRA`、`SHM`、`RTPS`；显式模式仍可用于独立测试和调试。
- `RoleAttributes` 序列化已包含 `channel_id`，保证跨进程 Discovery 获得完整 channel 元数据。
- 每个 Subscriber 使用唯一 endpoint ID，使同一进程、同一 channel 上的多个 Subscriber 能被 Discovery 正确区分。
- Publisher 初始化时会扫描已经存在的 Reader，因此同时支持 Publisher-first 和 Subscriber-first 启动顺序。

Hybrid Transport 使用轻量 peer 表维护状态：

- 重复 JOIN 保持幂等，不重复初始化底层 Transport。
- 单个 peer LEAVE 只移除对应关系。
- Transmitter 在最后一个同模式 peer LEAVE 后关闭对应发送 Transport；后续 peer JOIN 时可以重新启用。
- Receiver 在 peer LEAVE 时只移除对应 listener 和 peer 关系；底层 Receiver/Dispatcher 按 channel 生命周期复用，不随单个 peer 频繁销毁和重建。HybridReceiver 整体关闭时清理登记的 listener，实际 RTPS Reader 等 channel 资源由全局 Dispatcher/Transport Shutdown 统一释放。
- 一次 Publish 会向所有活跃模式分别发送一次；多个同模式 Subscriber 不会造成同一消息重复写入该 Transport。
- 没有 Subscriber 时，普通 Publish 保持成功语义；Loan 的 Acquire/Publish 返回失败。
- 当前不提供 SHM/RTPS 失败后的自动 fallback，也不包含动态优先级或网络质量策略。

### 发送端生命周期与并发边界

支持的并发范围是一个发布线程与 Discovery/后端 Enable、Disable 线程并发，包含
INTRA 同步回调在同一发布线程内重入。`seq_num_` 不支持多个发布线程并发写；
每次发送复制独立的 MessageInfo，嵌套发布不会改变外层序号。Publisher 本身的
Init/Shutdown、对象析构以及全局 Transport/Participant Shutdown 必须在发布
和拓扑回调停止后执行。底层 RTPS listener 内直接重入 Fast DDS API 的行为不在
支持范围内（ReaListener 自身也持有回调锁）；Node Subscriber 的用户回调由
调度器执行。全局关闭不属于发送端启停同步范围。

原问题是 Hybrid 已取得子发送端 shared_ptr，Discovery 随后关闭最后一个 peer
对应的后端；发送端对象还在，内部 Writer/History 却可能已经释放。

- INTRA：短生命周期锁串行化 enabled_ 的读写和操作接纳；Dispatcher 为进程级
  对象，没有按 Enable 轮次销毁的发送资源。已接纳的同步 Dispatch 可以在
  Disable 返回后完成，关闭不等待回调。新操作在关闭状态返回 false/nullptr。
- RTPS：同一生命周期锁保护 Enable、Disable、Acquire 和从资源检查到
  new_change/add_change/失败归还 change 的全过程。普通消息和 Loan 共用
  TransmitSerialized。Disable 等待实际资源使用结束，先由 RTPSDomain 删除
  Writer，再释放调用方拥有的 History。用户持有的 heap Loan 不占此锁。
- SHM：保留现有生命周期锁、owner/channel/enable epoch 检查和 Lease。
  Acquire 只在取得块时持锁，用户持 Loan 不阻塞 Disable；Lease 维持映射，
  旧 SHM Loan 在关闭期间或重新启用后的新 epoch 提交失败。共享区布局为 v2。
- Hybrid：普通消息和 Loan 均在路由快照后解锁再发送，每种活跃模式一次。
  拓扑更新的锁顺序为路由锁→后端锁；发送不持路由锁进入后端。INTRA 进入
  回调前没有路由锁/生命周期锁，ListenerHandler 也只在复制 Signal 时持锁。
  Signal 的连接标记使用 atomic，Disconnect 与已取得快照的调用可以交错；
  Disconnect 不是回调完成屏障，回调捕获对象仍须由调用者保证存活。

普通 Hybrid Publish 无 peer 仍返回 true；无路由 Acquire 返回 nullptr，
Loan Publish 返回 false。heap Loan 没有 SHM epoch 限制，后续 Enable 后可以
提交；SHM Loan 若仍须提交到 SHM，必须通过该后端 owner/channel/epoch 校验。
纯 SHM Acquire 后加入 INTRA/RTPS，保留原 Lease 并复制 heap 快照；若 SHM
路由完全消失而只剩非 SHM，可从 Lease 保护的数据复制后发送；没有路由则拒绝。
heap Loan 获取后变成纯 SHM，沿用复制入 SHM 的路径。一次 Hybrid 发送可能
部分成功、整体 false，不回滚、不重试，避免重复投递。

### 共享区布局 v2 与兼容性

- State、Block、ReadableInfo 和 Indicator 的共享区内容不包含进程私有的虚表指针；原子成员仍正常构造。Segment、Notifier 等进程内管理对象保留多态。
- 布局版本为 2，尾部元数据记录容量及 State/Block 的大小和对齐信息。打开共享段时先核对实际长度、版本、ABI 和 Payload 边界，再访问 State；不兼容时仅解除映射，不修改引用计数或删除旧段。
- ShmConf 的保守分配公式为 `4096 + 1024 + (1024 + block_buf_size) * block_num`。元数据从实际段末尾复制到本地后校验，定位不依赖 State，允许尾标记起点未对齐。
- Payload 起点为 `sizeof(State) + block_num * sizeof(Block)`，Block 数据步长仍为 `block_buf_size`。ReadableInfo 保留原有零值 `reserved_`，通知区布局不变。
- 不支持旧布局混用、在线迁移或其他进程并发截断现有映射。升级前停止相关旧进程，确认具体 channel 的 POSIX `/cmw_<channel_id>` 或 XSI key/shmid，只清理对应旧段，再启动全部新版进程重建。不要全局清理 `/dev/shm` 或批量 `ipcrm`。
- 更早版本遗留的通知区同样需要在停止旧进程后人工确认、重建，不应与旧程序混用。

### 统一日志目录

运行日志统一写入项目根目录 `log/`，不再随启动工作目录变化。
`Init()` 和直接调用 `Logger_Init()`/`Logger::open()` 共用这一规则：
只取传入名称的最后一个路径分量，缺少 `.log` 后缀时自动补齐；重复初始化
按追加模式打开，轮转文件保留在同一目录。

项目根目录优先取 `CMW_PATH`；未设置或为空时，example/Makefile 构建的程序
使用编译时的 `CMW_PROJECT_ROOT`。其他构建入口须定义该宏或设置 `CMW_PATH`。
日志目录按需创建；根目录不可用、日志目录不是普通目录或目标文件是符号链接
等情况会明确报错，不退回当前目录写文件。

`log/` 中的 logger 源码仍由 Git 跟踪，`.log` 和 `.log.*` 日志文件被忽略。
`make clean` 保留运行日志和迁移归档，历史文件可存放在 `log/` 的子目录中。


