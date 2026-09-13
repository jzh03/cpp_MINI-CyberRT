# MINI CyberRT

基于 cmw 二次开发的 C++ 通信中间件。保留 Node、Discovery、Transport 和调度器架构，补充自动选路、SHM Loan、生命周期保护及回归测试。

## 快速开始

已有 Linux、g++、make、Fast DDS 环境时，在仓库根目录执行：

```bash
./example/demo/run_demo.sh
```

脚本自动构建并演示 A～E，约 3 分钟（首次编译另计）。看到 `[RESULT] PASS requested=all` 表示完整演示通过。
需要 `unshare` 支持；依赖检查见 [Fast DDS 环境](doc/fastrtps.md)，单场景和双终端操作见 [Demo 指南](example/demo/README.md)。

## 文档导航

| 想做什么 | 看这里 |
| --- | --- |
| 跑演示、准备面试讲解 | [通信 Demo](example/demo/README.md) |
| 构建、运行测试或 benchmark | [测试与性能程序](example/TESTING.md) |
| 查实际结果、失败原因和复验 | [测试记录](example/testlog.md) |
| 串起整个项目、理解运行顺序与数据流 | [总体架构](doc/architecture.md) |
| 理解一条消息怎么送达 | [通信流程](doc/transport.md) → [发现与拓扑](doc/topology.md) |
| 定义消息类型 | [序列化](doc/serialize.md) |
| 理解回调调度 | [调度器](doc/scheduler.md) → [协程](doc/croutine.md) |
| 配置与基础组件 | [配置说明](config/config.md)、[基础组件](base/base.md) |
| 维护文档 | [维护约定](AGENTS.md) |

## 功能与使用边界

### Discovery 驱动的混合传输

Node 的 Publisher/Subscriber 默认使用 HYBRID。Discovery 提供真实对端属性，再决定启用哪条路径：

| 双方关系 | 路径 | 数据怎么传 |
| --- | --- | --- |
| 相同 IP、相同 PID | INTRA | 直接共享消息 `shared_ptr`，不序列化 |
| 相同 IP、不同 PID | SHM | 通过本项目的共享内存和通知区 |
| 不同 IP | RTPS | 使用 Fast DDS 的 RTPS 接口 |

选路逻辑位于 [transport_mode.h](config/transport_mode.h)。一条消息向每种活跃模式发送一次；
同模式多个订阅者不会重复写入该后端，不同位置的订阅者可以使多路径并存。

- 重复 JOIN 幂等；最后一个同模式订阅者离开时才停用对应发送后端，后续 JOIN 可重新启用。
- 接收端离开时移除 peer listener，底层 channel Receiver/Dispatcher 可继续复用。
- 每个 Subscriber 有唯一 endpoint id；Publisher 初始化时扫描已发现的 Reader。
- 普通 Publish 没有订阅者时仍可返回 true；Loan 没有路由时 Acquire/Publish 失败。
- 不提供自动 fallback、失败重试或离线补发。多路径发送可能部分成功但整体返回 false，不回滚已投递消息。

低层 `Transport::CreateTransmitter/CreateReceiver` 可显式选择 INTRA、SHM、RTPS、HYBRID；Node 没有显式模式参数。

### LoanedMessage 零拷贝

发送接口是 `AcquireMessage(capacity)` → 填充 `mutable_data()` → `set_size()` → `Publish(std::move(message))`。
接收回调中的 `LoanedMessage` 是只读 View，通过 `data()` 访问。

| 拓扑/存储 | 行为 |
| --- | --- |
| 仅 SHM 活跃 | 借出 SHM Block，直接提交原块，避免中间 Payload 序列化和复制 |
| 含 INTRA 或 RTPS | 使用 heap；INTRA 共享引用，SHM 复制入 Block，RTPS 编码长度与字节 |
| 纯 SHM 借出后拓扑改变 | 必要时复制 heap 快照；仍提交到 SHM 时检查 owner/channel/epoch |

SHM View 持有读 Lease，最后一个引用释放后归还；不要在引用失效后使用裸指针。
后端关闭再启用后，旧 SHM Loan 不能提交；应重新 Acquire。Heap Loan 不受 SHM epoch 限制。
单消息 Subscriber 协程在回调后、Yield 前释放自身引用，用户保存的引用仍由用户管理。
关键调用及零拷贝范围见 [Demo 实现说明](example/demo/README.md#loanview-到底省了哪次拷贝)。

### 发送端生命周期与并发边界

支持**一个发布线程**与 Discovery 驱动的后端 Enable/Disable 并发，包括 INTRA 在同一发布线程中的同步重入。
每次发送使用独立 MessageInfo；`seq_num_` 不支持多个发布线程并发写。

| 后端 | 同步与关闭规则 |
| --- | --- |
| INTRA | 锁保护状态检查；进入回调前解锁，已接纳的回调可在 Disable 返回后完成 |
| RTPS | 生命周期锁覆盖资源检查和实际发送；Disable 等待资源使用结束，再删除 Writer、释放 History |
| SHM | 生命周期锁保护资源，Lease 保持映射，owner/channel/epoch 拒绝失效 Loan；用户持 Loan 不阻塞 Disable |
| Hybrid | 锁内取路由快照，解锁后发送；拓扑更新顺序为路由锁 → 后端锁 |

Publisher 的 Init/Shutdown、析构及全局 Transport/Participant Shutdown 必须在发布和拓扑回调停止后执行。
Signal Disconnect 不是回调完成屏障，捕获对象须保持存活。底层 RTPS listener 内直接重入 Fast DDS API 不在支持范围内；Node 用户回调由调度器执行。

### 序列化安全

`DataStream` 校验类型、长度和剩余字节，截断/非法输入返回失败，不错误推进读位置。
基础类型通过 `memcpy` 避免未对齐访问；`vector<T>` 按元素数量逐项编码。
自定义消息使用 `SERIALIZE(...)`，字段顺序须一致；[消息定义示例](doc/serialize.md)。

### SHM 健壮性

默认使用 POSIX SHM，XSI 保留用于独立测试。消息上限 **32 MiB**；超限直接失败。
块被占用时最多扫描一轮，失败不无限等待；重建后再次检查容量。
读取先验证块和 generation，失败则丢弃。MessageInfo 序号使用 `memcpy` 编解码，避免未对齐访问。

### 共享区布局 v2 与兼容性

共享区中的 State、Block、ReadableInfo、Indicator 不包含进程私有虚表指针。
Segment 打开前校验实际段长、版本、ABI、容量及 Payload 边界；拒绝不兼容段时不修改引用计数或删除旧段。

- 共享 Payload 段为 **v2**，Notifier 有独立版本，不能混为一谈。
- 不支持旧布局混用、在线迁移、跨 ABI 或其他进程并发截断映射。
- 升级前停止相关进程，确认确切 POSIX 段名或 XSI key/shmid，再处理对应旧段；不要全局清空 `/dev/shm` 或批量 `ipcrm`。
- 本地测试可用独立 IPC 环境避开旧通知区，见 [测试指南](example/TESTING.md#环境与清理)。

布局定位：分配量为 `4096 + 1024 + (1024 + block_buf_size) * block_num`；Payload 起点为
`sizeof(State) + block_num * sizeof(Block)`，步长为 `block_buf_size`。尾部元数据复制到本地后校验，不依赖 State 或对齐的尾标记起点；ReadableInfo 的 `reserved_` 保持零值。

### Notifier 槽位保护与丢弃策略

ConditionNotifier 使用 4096 槽位广播环，采用发布短锁和槽位互斥，允许丢弃。
共享锁使用 32 位原子量；要求 32/64 位原子操作始终 lock-free，面向相同 ABI 的 Linux 进程。

| 情况 | 结果 |
| --- | --- |
| 写者未取得发布锁或目标槽位锁 | `Notify()` 返回 false，不推进序号，不内部重试 |
| 通知发布成功 | 返回 true；不保证读者收到或 Payload 仍可读 |
| 慢读者落后超过一圈 | 跳过被覆盖部分，从仍保留的最早通知继续 |
| 读者遇到忙槽位 | 在 steady-clock 截止前重试；超时返回 false，不改变输出参数 |
| 新接收实例加入 | 从当前发布位置开始，不回放旧通知；各实例游标独立 |

写者先写完整通知和槽位序号，再推进已发布位置；读者持同一槽位锁核对序号并复制通知，锁不覆盖 Payload 读取或用户回调。
非正 Listen 超时只尝试一次；空参数、关闭、初始化失败均返回 false；序号耗尽拒绝新发布。
单实例只支持一个 Listen 调用者，可有多个 Notify 调用者；Shutdown/析构前必须结束全部调用。

通知区为 **Notifier v1**。打开时检查长度、magic、版本、大小/对齐、容量和偏移；创建者完成初始化后发布 magic，打开者最多等 100 ms。
不兼容或未初始化时明确失败，不自动改写或删除；失败实例须销毁重建才可重试。
持锁进程崩溃后不能自动恢复，需停用相关进程并人工确认后重建通知区。
[对应回归](example/TESTING.md#notifier-槽位保护回归)。

### Discovery 拓扑通知容量

按实际序列化长度向 DDS 申请缓冲，支持超过旧 255 字节容量的元数据。
超出 32 位长度、分配/容量/History 提交失败时返回 false，并释放未提交 change；本地已更新的拓扑不因远端发送失败回滚。

### Discovery 后启动进程发现

Discovery 的底层 Reader/Writer 使用 `RELIABLE + TRANSIENT_LOCAL`，与公告中的 QoS 一致。
修复前只设置了公告 QoS，实际端点仍使用 BEST_EFFORT，Reader 还是 VOLATILE，全新进程可能漏掉旧 Writer JOIN。
现在新订阅进程可通过保留的拓扑历史发现仍在线的发布者，无需应用重新公告。
普通 RTPS 消息的 QoS、SHM 布局和锁策略不变；拓扑 History 仍有容量限制，本次未覆盖历史公告淘汰后的状态重建。
[针对性回归](example/TESTING.md#全新订阅进程自动发现回归)。

### 面试通信 Demo

A～E 覆盖同进程自动 INTRA、跨进程自动 SHM、Loan/View、正常退出恢复和同机强制 RTPS。
路径诊断仅在独立 Demo 构建中启用，可关闭；原目标不受影响。
D 使用修复后的原生 Discovery 恢复通信，Publisher 持续运行，新 Subscriber 接收新消息；不提供离线补发。
[操作与讲解](example/demo/README.md)；[首次验收](example/testlog.md#2026-09-12-面试通信-demo-本地验收)；
[Discovery 修复复验](example/testlog.md#2026-09-13-discovery-全新订阅进程自动发现修复)。

### SHM 性能比较边界

独立 sender/receiver 直接使用 ShmTransmitter/ShmReceiver，比较普通序列化与 SHM-backed Loan，不经过 Hybrid。
四档 Payload 为 4 KiB、64 KiB、1 MiB、4 MiB；夹具固定 32 槽、每槽 8 MiB，不改变运行库默认策略。
实验包含内容生成、传输和全量校验；预热、正式窗口、排空分开统计，发送成功不等于接收成功。
结果不是纯传输带宽、无损容量或延迟保证。[运行与统计口径](example/TESTING.md#性能程序)；[已有三轮结果](example/testlog.md#2026-09-11-独立进程-shm-性能实验)。

### 统一日志目录

运行库通过 `Init()`、`Logger_Init()` 或 `Logger::open()` 写入根目录 `log/`。
调用者只需传文件名：路径只取末尾名称，自动补 `.log`；重复打开追加，轮转留在同一目录。

根目录优先取 `CMW_PATH`，未设时 Logger 使用编译时 `CMW_PROJECT_ROOT`；配置读取仍应显式设置 `CMW_PATH`。
无法定位根目录、创建目录或打开普通文件时明确失败，不回退到当前工作目录。
`log/` 含源码，不能整体删除或忽略；`make clean` 保留日志。
Demo 脚本另将自己的 Logger 文件原样归档到 `example/demo/runs/`，见 [Demo 日志说明](example/demo/README.md#6-失败排查与清理)。

## 项目来源

[原作者视频](https://space.bilibili.com/281708692/lists/5849251?type=season) · [原作者讲解文档](https://ai.feishu.cn/drive/folder/PiqFfxWx5l9Ri2dds9WcI3ognCd?from=from_copylink)
