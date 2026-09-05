# MINI_CyberRT

MINI_CyberRT 是针对原始 cmw 项目进行的二次开发，在保留原有 CyberRT 与 Fast DDS 中间件架构的基础上，进一步改善功能并增加测试覆盖。

原作者提供的项目视频：https://space.bilibili.com/281708692/lists/5849251?type=season

原作者提供的讲解文档：[飞书](https://ai.feishu.cn/drive/folder/PiqFfxWx5l9Ri2dds9WcI3ognCd?from=from_copylink)

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
- 没有 Subscriber 时保持原有 Publish 成功语义。
- 当前不提供 SHM/RTPS 失败后的自动 fallback，也不包含动态优先级或网络质量策略。

### 测试

- 新增序列化边界、SHM 消息大小/Recreate、全 Block 占用、读失败以及 SHM Transmitter/Receiver 恢复能力的测试。
- `test_transport_mode_selection`：纯元数据测试，验证 same-process、same-host different-process、different-host 三种模式判断。
- `test_hybrid_intra`：通过真实 Publisher、Subscriber 和 Discovery 验证同进程 INTRA 通信、连续消息、Shutdown 以及多个同模式 peer 的 LEAVE 行为。
- `test_hybrid_shm_multiprocess`：通过两个真实进程验证 Subscriber-first 启动以及同主机不同 PID 自动选择 SHM。
- `test_rtps_same_host_multiprocess`：在同一主机的两个进程中显式强制 RTPS，验证 RTPS 数据路径未发生回归。
- `test_hybrid_dynamic_intra`：验证同进程 Subscriber 离开后，新 Subscriber 可以重新 JOIN 并通过 INTRA 接收消息。
- `test_hybrid_dynamic_shm_lifecycle`：验证同机跨进程 Subscriber A LEAVE 后，Subscriber B 可以重新 JOIN 并通过 SHM 通信。
- `test_shm_transmitter_lifecycle_regression`：覆盖未对齐序号元信息的普通消息、SHM-backed Loan 和 Heap-backed Loan 路径，以及发送与 Enable/Disable 并发、旧 SHM Loan 拒绝和恢复收发。
- `test_loaned_message_dynamic_shm_lifecycle`：通过两个真实进程持续发布 LoanedMessage，验证 Subscriber 正常 LEAVE 后重新 JOIN 的 Discovery 驱动 SHM 恢复。
- `test_rtps_lifecycle_regression`：同机显式强制 RTPS，多轮验证 Enable、通信、Disable 和重新 Enable 的资源生命周期。
- `test_posix_segment_multiprocess`：验证 POSIX Segment 双进程 `OpenOnly()`、多 Block 读写、重新打开和资源清理。
- `shm_segment_benchmark`：以相同的跨进程 Block 读写路径比较 POSIX 与 XSI 的稳态延迟和吞吐。运行：`cd example && make shm_segment_benchmark && ./build/bin/shm_segment_benchmark`。

> `test_rtps_same_host_multiprocess` 和 `test_rtps_lifecycle_regression` 都只是同主机强制 RTPS 数据路径测试，不等价于真正的跨主机 RTPS E2E。不同 host metadata 自动选择 RTPS 已由模式单元测试覆盖，真实跨主机通信仍需要在两台主机或两台 VM 上进行集成验证。
