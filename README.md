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

### SHM 性能比较边界

`shm_benchmark_sender` 与 `shm_benchmark_receiver` 是两个独立进程，直接实例化
`ShmTransmitter`/`ShmReceiver`，比较普通序列化 SHM 与 SHM-backed Loan。
连接探针、实际 POSIX 段的类型/容量/进程映射及 Loan Lease 属性共同验证目标路径，
不经过 Hybrid 自动选路。测试夹具预建固定的 32 槽位、每槽 8 MiB Segment，
四档业务 Payload 为 4 KiB、64 KiB、1 MiB、4 MiB；不会改变中间件的默认槽位策略。

当前比较包含每条消息的数据准备：普通路径分配业务 buffer、生成内容、经过
`DataStream` 序列化和 SHM 复制，接收端反序列化；Loan 直接在借出的 SHM
buffer 中生成相同内容，接收端读取只读 Lease。两条路径的接收端执行相同的逐字节校验和序号统计。
这是持续过载下的端到端吞吐比较，不能解释为纯传输带宽、无损容量或延迟结果。
预热、正式窗口与排空严格分账，发送成功不等于接收成功。
[运行参数、统计口径和资源清理](example/TESTING.md#性能程序)；
[实际三轮结果及环境限制](example/testlog.md#2026-09-11-独立进程-shm-性能实验)。

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
- Payload 起点为 `sizeof(State) + block_num * sizeof(Block)`，Block 数据步长仍为 `block_buf_size`。ReadableInfo 保留原有零值 `reserved_`。Notifier 通知区独立版本化，见下节。
- 不支持旧布局混用、在线迁移或其他进程并发截断现有映射。升级前停止相关旧进程，确认具体 channel 的 POSIX `/cmw_<channel_id>` 或 XSI key/shmid，只清理对应旧段，再启动全部新版进程重建。不要全局清理 `/dev/shm` 或批量 `ipcrm`。
- 更早版本遗留的通知区同样需要在停止旧进程后人工确认、重建，不应与旧程序混用。

### Notifier 槽位保护与丢弃策略

ConditionNotifier 使用 4096 槽位的广播环，采用“发布短锁＋槽位读写互斥＋允许丢弃”。
发布锁和每个槽位锁均为共享区内的 32 位原子量，使用 acquire/release；构建要求
32/64 位原子操作始终 lock-free，不能依赖进程私有的后备锁。本实现面向相同 ABI
的 Linux 进程，不承诺跨平台共享内存 ABI。

写者只尝试一次发布锁，再尝试一次目标槽位锁；取得两者后写入完整 ReadableInfo
及槽位序号，再推进已发布位置，依次释放槽位锁、发布锁。只有成功写入才消耗
序号，暂停的写者不会被其他写者越过。读者取得同一槽位锁后核对序号并复制
`host_id / channel_id / block_index / generation`；锁不覆盖 Payload 读取或用户回调。

| 场景 | 行为与返回值 |
| --- | --- |
| 写者拿不到发布锁或槽位锁 | 丢弃本次新通知，`Notify()` 返回 false，不推进序号；无内部重试。 |
| 写入成功 | `Notify()` 返回 true，表示通知已发布，不保证所有读者收到。 |
| 慢读者落后超过一圈 | 跳过覆盖部分，从当时仍保留的最早通知继续读；并发绕环时重新核对位置。 |
| 读者遇到忙槽位 | 在 `Listen()` 的 steady-clock 截止时间内重试，超时返回 false，输出参数保持不变。 |
| 多个接收进程 | 每个实例维护自己的游标，互不消费其他实例的通知，保持广播语义。 |

新实例从打开时的已发布位置开始，不回放此前通知。零或负 Listen 超时只尝试
一次；空输出指针、已关闭或初始化失败均返回 false。64 位序号到达上限后拒绝
新发布，避免回绕为零。单实例只支持一个 Listen 调用者；可同时有多个 Notify
调用者。Shutdown/析构须在全部 Notify/Listen 调用结束后进行。

通知区首次引入独立版本 **Notifier v1**，与 **Payload Segment v2** 无关：校验
实际段长、magic、版本、Indicator/Slot/ReadableInfo 大小、槽位及通知对齐、容量
和槽位偏移后才访问锁及游标。创建者构造完成后以 release 发布 magic；打开者
最多等待 100ms，未初始化或不兼容时明确失败，不改写、删除旧区，也不自动重建。
初始化失败的实例须销毁后重新创建才能重试。旧通知区升级前须停止相关进程，
确认具体 key/shmid 后单独重建；Payload 数据区布局本次不变。

原子锁不支持持锁进程崩溃后的自动恢复；进程暂停期间，竞争写者允许丢弃，读者
仍按超时退出。持锁进程永久退出时，需停止相关进程后重建通知区。通知成功也
不保证 Payload 仍可读，Payload 的 Lease/generation 检查继续生效。
测试覆盖和运行方式见 [Notifier 测试指南](example/TESTING.md#notifier-槽位保护回归)，
本地执行证据见 [测试日志](example/testlog.md)。

### Discovery 拓扑通知容量

Discovery 拓扑通知按实际序列化长度向 DDS 申请缓冲区，避免长 channel/node/type
元数据超过旧的 255 字节固定容量时越界。长度超出 DDS 的 32 位范围、分配失败、
容量不足或 History 提交失败时返回 false，未提交的 change 会释放；本地拓扑
Dispose 已执行的状态不因远端发送失败而回滚。

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


