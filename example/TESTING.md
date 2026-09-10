# 测试入口

本文介绍测试范围和运行方法；执行结果与环境问题见 [测试日志](testlog.md)。

在 `example/` 目录运行。构建需要本地 Fast DDS；运行中间件测试时通常还需要
`CMW_PATH` 指向仓库根目录：

```sh
CMW_PATH="$(cd .. && pwd)" make check
```

`make tests` **只构建**正式回归，不执行。单独运行一个已构建测试时，可使用：

```sh
CMW_PATH="$(cd .. && pwd)" ./build/bin/test_blocker
```

`make check-fast` 构建并顺序运行快速回归：

- `test_serialize`（DataStream 边界和失败路径）
- `test_blocker`、`test_node`、`test_publisher_subscriber`（基础 API 与同进程端到端字段往返）
- `test_transport_mode_selection`（INTRA/SHM/RTPS 元数据选路）
- `test_shm_segment_robustness`、`test_shm_block_lease_generation`（Segment 边界、Lease/generation 与 Notifier 完整性）
- `test_shm_dispatcher_robustness`、`test_shm_transmitter_receiver`（异常恢复、重建、超限后的恢复）
- `test_shm_loaned_message`、`test_shm_transmitter_lifecycle_regression`、`test_loaned_message_hybrid`（Loan 生命周期、epoch/并发切换、存储选路）
- `test_hybrid_intra`（同进程 INTRA，包括离开后新订阅者加入）
- `test_intra_transmitter_lifecycle`（发送/启停并发与同步重入）

`make check-integration` 构建并顺序运行需要 fork、Discovery 收敛或强制 RTPS 的
回归：`test_shm_segment_exec`、`test_posix_segment_multiprocess`、`test_loaned_message_dynamic_shm_lifecycle`、
`test_shm_loaned_message_multiprocess`、`test_hybrid_shm_multiprocess`、
`test_rtps_same_host_multiprocess`、`test_hybrid_dynamic_shm_lifecycle`、
`test_rtps_lifecycle_regression`、`test_loaned_message_rtps_multiprocess`、
`test_rtps_transmitter_lifecycle`、`test_loaned_message_discovery_churn`。
`make check` 按 fast、integration 的顺序运行两组；即使 fast 失败，仍会尝试
integration，并以汇总结果返回非零。

测试执行器给每个二进制一个独立进程组，非零返回、信号、缺失二进制和超时都会
失败；超时时仅终止该测试创建的进程组。可用
`CHECK_FAST_TIMEOUT=45` 或 `CHECK_INTEGRATION_TIMEOUT=120` 调整秒数。即使使用
`make -j check`，测试进程仍顺序执行，只有构建可并行。

`make demos` 仅构建手工示例和诊断工具：writer、reader、publisher、subscriber、
topology manager、log、getenv；不会启动它们。`make benchmarks` 仅构建现有性能
测试，也不会运行。

`test_node_manager`、`test_channel_manager` 仍是打印式手工检查；
`test_croutine`、`test_scheduler`、`test_task`、`test_class_loader` 仍待正式化，
均不属于 `check`。它们仍可按原目标单独构建。

同机强制 RTPS 只验证强制 RTPS 数据路径，不代表跨主机自动选路。普通并发回归
只能覆盖所编排的交错，不证明没有所有竞态；Notifier 完整性测试也不证明全面的
内存序正确性。UBSan 和 TSan 是同一测试的不同构建方式，不需要复制测试源文件。

## 共享区无 vptr 回归

`test_shm_segment_exec` 已纳入 `check-integration`。父进程通过真实 Segment
创建消息，子进程 `fork + exec /proc/self/exe --reader ...` 后打开、读取、释放
Block 并写回消息；父进程用带 10 秒截止时间的 `waitpid` 回收子进程，再次 exec
验证重开。POSIX 和 XSI 共用测试逻辑，仅清理本测试独占创建的资源。
布局拒绝覆盖带真实虚析构的旧 State 和旧版尾标记、新版错误版本、缺失标记、
1/31 字节截断、非对齐的错误总长、错误类型大小/对齐、非法容量，以及
State 容量与尾标记不一致。失败前后比较完整映射字节，检查未修改引用计数等内容。

在仓库根目录执行共享内存相关的普通回归（使用全新目录，避免旧 ABI 对象残留）：

```sh
make -C example -j4 BUILD_DIR=/tmp/cmw-vptr-normal \
  test_shm_segment_exec test_shm_segment_robustness \
  test_shm_block_lease_generation test_shm_loaned_message \
  test_posix_segment_multiprocess
CMW_PATH="$PWD" bash example/run_tests.sh \
  --bin-dir /tmp/cmw-vptr-normal/bin --timeout 30 \
  test_shm_segment_exec test_shm_segment_robustness \
  test_shm_block_lease_generation test_shm_loaned_message \
  test_posix_segment_multiprocess
```

UBSan 必须使用独立 BUILD_DIR；此开关同时为全部 C++ 源文件和链接添加
`-fsanitize=undefined,vptr -fno-sanitize-recover=all`，并分别使用 `-fPIE`、`-pie`。
不要关闭 ASLR：

```sh
make -C example -j4 BUILD_DIR=/tmp/cmw-vptr-ubsan SANITIZE=undefined \
  test_shm_segment_exec
CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  bash example/run_tests.sh --bin-dir /tmp/cmw-vptr-ubsan/bin \
  --timeout 30 test_shm_segment_exec
```

旧布局不支持混用或在线迁移。升级前请人工确认并停止相关旧进程，确认具体
channel 对应的 POSIX `/cmw_<channel_id>` 或 XSI key/shmid 后，只清理这些旧段，
再启动全部使用新版的进程重建。不要全局清理 `/dev/shm` 或批量 `ipcrm`。
通知区 ReadableInfo 布局此次未变；更早版本遗留的通知区也应在停止旧进程后
人工确认、重建，不应与旧程序同时使用。

## 发送端生命周期同步回归

本轮范围是一个发布线程与 Discovery/后端 Enable、Disable 线程并发，包含
INTRA 同步回调在同一发布线程内重入。`seq_num_` 不支持多个发布线程并发写；
每次发送复制独立的 MessageInfo，嵌套发布不会改变外层序号。Publisher 本身的
Init/Shutdown、对象析构以及全局 Transport/Participant Shutdown 必须在发布
和拓扑回调停止后执行。底层 RTPS listener 内直接重入 Fast DDS API 的行为不在
本轮承诺内（ReaListener 自身也持有回调锁）；Node Subscriber 的用户回调由
调度器执行。本轮没有改动全局关闭顺序。

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
  旧 SHM Loan 在关闭期间或重新启用后的新 epoch 提交失败。布局 v2 未改。
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

新增入口：

- check-fast：`test_intra_transmitter_lifecycle`，关闭阶段握手、重新启用、旧
  heap Loan、普通消息和 Loan 的同步重入、停在回调中的 Disable。
- check-integration：`test_rtps_transmitter_lifecycle`，显式编排 RTPS 发送端
  启停与持续 Loan 并发、真实 Fast DDS 收发恢复、普通消息序列化中途关闭；
  构造不同 host 元数据的 Hybrid 测试只属于受控路由/生命周期验证，检查
  路由快照后 RTPS 被关闭导致部分成功，以及后续恢复。
- check-integration：`test_loaned_message_discovery_churn`，真实 Publisher/
  Subscriber，三轮同进程 INTRA 和同机跨进程 SHM 的最后/非最后 peer LEAVE，
  B JOIN 恢复，以及持 SHM Loan 时加入本地 Subscriber 的混合路由转换。
  每个接收者检查 64 字节长度、Payload 全程序号及全部内容，拒绝重复；
  RTPS 的 MessageInfo 序号按现有 DDS Writer 序号语义检查，重建会重新计数。

阶段使用条件变量、管道确认、真实拓扑计数及有限时间收发恢复进行编排；
仅新增 Discovery 测试初次进程间 endpoint 匹配保留 1 秒等待。切换期间允许丢弃/失败。
新 Discovery 测试有子进程守卫，测试执行器另有进程组超时，只清理唯一 channel
的共享段，不清理全局通知区。现有 SHM epoch/generation/Lease 和布局测试继续运行。
旧 RTPS lifecycle 测试也改为真实探测消息确认匹配，待验证消息仍只发一次。
这些测试均在一台 VM 内，不构成真实跨主机验证。

验证发现的必要依赖修正：单消息 Subscriber 协程在回调返回后、Yield 前释放
自己的消息引用，避免停止协程直接回收栈时遗留 Loan/Lease；用户在回调中保存
的 shared_ptr 不受影响。测试自身在退出前停止调度器和 SHM 分发线程。
C++14 构建加入 `-faligned-new`，满足 Publish 必经的 PerfEventCache 内
BoundedQueue 的 64 字节对齐；对象规则依赖 Makefile，修改选项后自动重编译。
其他构建入口也须提供 C++14 对齐分配支持或使用支持该语义的更新语言标准。

在仓库根目录执行，务必设置 CMW_PATH；不同构建必须使用不同目录：

```sh
new_tests='test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_loaned_message_discovery_churn'
make -C example -j3 BUILD_DIR=/tmp/cyberrt-lifecycle-normal $new_tests
CMW_PATH="$PWD" bash example/run_tests.sh --bin-dir /tmp/cyberrt-lifecycle-normal/bin --timeout 90 $new_tests
CMW_PATH="$PWD" make -C example -j3 BUILD_DIR=/tmp/cyberrt-lifecycle-normal check
CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  make -C example -j3 BUILD_DIR=/tmp/cyberrt-lifecycle-ubsan SANITIZE=undefined check
```

`SANITIZE=address` 和 `SANITIZE=thread` 分别为所有 C++ 对象和链接添加
ASan/TSan，带 frame pointer，使用 non-PIE 可执行文件。未关闭任何 sanitizer
检查。针对性执行方法：

```sh
asan_tests="$new_tests test_shm_transmitter_lifecycle_regression test_loaned_message_hybrid test_shm_block_lease_generation test_rtps_lifecycle_regression test_loaned_message_rtps_multiprocess"
make -C example -j3 BUILD_DIR=/tmp/cyberrt-lifecycle-asan SANITIZE=address $asan_tests
CMW_PATH="$PWD" ASAN_OPTIONS=halt_on_error=1 \
  bash example/run_tests.sh --bin-dir /tmp/cyberrt-lifecycle-asan/bin --timeout 90 $asan_tests
tsan_tests='test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_shm_transmitter_lifecycle_regression'
make -C example -j2 BUILD_DIR=/tmp/cyberrt-lifecycle-tsan SANITIZE=thread $tsan_tests
CMW_PATH="$PWD" TSAN_OPTIONS=halt_on_error=1 \
  bash example/run_tests.sh --bin-dir /tmp/cyberrt-lifecycle-tsan/bin --timeout 30 $tsan_tests
```

Fast DDS 使用既有本地安装库，未重新构建为 sanitizer 版本。sanitizer 与压力
测试只覆盖实际执行路径，资源安全结论还依赖上述锁与所有权关系。
