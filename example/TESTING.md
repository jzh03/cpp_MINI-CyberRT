# 测试入口

本文只介绍测试程序功能和可复用的使用方法。下面的命令是操作示例，不代表已执行；
完整实际命令、执行记录和结果见 [测试日志](testlog.md)，功能性更新见
[README](../README.md)，维护约定见 [AGENTS.md](../AGENTS.md)。

在 `example/` 目录运行。构建需要本地 Fast DDS；运行中间件测试时需要
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
- `test_hybrid_intra`（真实 Publisher/Subscriber/Discovery 的同进程通信、连续消息、多个同模式 peer 离开与新订阅者加入）
- `test_intra_transmitter_lifecycle`（发送/启停并发与同步重入）
- `test_logger_paths`（日志根目录选择、路径约束、重复初始化追加、轮转与失败处理）

`make check-integration` 构建并顺序运行需要 fork、Discovery 收敛或强制 RTPS 的
回归：

| 程序 | 功能与覆盖范围 |
| --- | --- |
| `test_condition_notifier` | 通知槽位互斥、发布序号、丢弃与恢复、绕环、并发字段一致性及 fork+exec 广播/布局拒绝。 |
| `test_shm_segment_exec` | POSIX/XSI 的 fork+exec 读写、重开及不兼容布局拒绝。 |
| `test_posix_segment_multiprocess` | POSIX 双进程 OpenOnly、多 Block 读写、重开与资源清理。 |
| `test_loaned_message_dynamic_shm_lifecycle` | 两个真实进程验证 LoanedMessage 在 Subscriber LEAVE/JOIN 后恢复 SHM 通信。 |
| `test_shm_loaned_message_multiprocess` | 跨进程 Loan 只读接收、读 Lease 持有及块回收。 |
| `test_hybrid_shm_multiprocess` | Subscriber-first 启动，真实 Discovery 按同 host、不同 PID 自动选择 SHM。 |
| `test_rtps_same_host_multiprocess` | 同机两个进程显式强制 RTPS 收发。 |
| `test_hybrid_dynamic_shm_lifecycle` | 同机跨进程 Subscriber A 离开后 B 加入及 SHM 通信恢复；使用长 message_type 强制 Discovery 通知超过 255 字节。 |
| `test_rtps_lifecycle_regression` | 显式编排普通消息发送端启停，以真实探测消息确认匹配后单次发送并检查重复。 |
| `test_loaned_message_rtps_multiprocess` | LoanedMessage RTPS 线格式边界、跨进程 heap-backed 只读收发。 |
| `test_rtps_transmitter_lifecycle` | RTPS 发送/启停并发、恢复、普通消息序列化中途关闭及受控 Hybrid 路由变化。 |
| `test_loaned_message_discovery_churn` | 持续发布 Loan，真实 INTRA/SHM 订阅者多轮上下线与混合拓扑变化。 |

`make check` 按 fast、integration 的顺序运行两组；即使 fast 失败，仍会尝试
integration，并以汇总结果返回非零。

不同构建配置的测试应顺序执行，避免共享通知区的回归互相干扰；构建可按机器资源并行。

测试执行器给每个二进制一个独立进程组，非零返回、信号、缺失二进制和超时都会
失败；超时时仅终止该测试创建的进程组。可用
`CHECK_FAST_TIMEOUT=45` 或 `CHECK_INTEGRATION_TIMEOUT=120` 调整秒数。即使使用
`make -j check`，测试进程仍顺序执行，只有构建可并行。

`make demos` 仅构建手工示例和诊断工具：writer、reader、publisher、subscriber、
topology manager、log、getenv；不会启动它们。`make benchmarks` 仅构建性能测试，也不会运行；使用方法见下文。

`test_node_manager`、`test_channel_manager` 仍是打印式手工检查；
`test_croutine`、`test_scheduler`、`test_task`、`test_class_loader` 仍待正式化，
均不属于 `check`。它们仍可按原目标单独构建。

同机强制 RTPS 只验证强制 RTPS 数据路径，不代表跨主机自动选路。普通并发回归
只能覆盖所编排的交错，不证明没有所有竞态；Notifier 完整性测试也不证明全面的
内存序正确性。UBSan 和 TSan 是同一测试的不同构建方式，不需要复制测试源文件。

## Notifier 槽位保护回归

`test_condition_notifier` 属于 `check-integration`，只链接 Notifier、ReadableInfo、
Logger、pthread、libatomic 和 GoogleTest；该目标不依赖 Fast DDS 库或调度器。
运行需要 Linux SysV SHM、`fork`、`exec /proc/self/exe`、pipe/poll/waitpid。

覆盖读者持槽位锁直到写者绕回、丢弃不推进序号、Listen 超时不改输出、解锁恢复、
发布锁持有期间超过三圈的竞争尝试全部失败、慢读者三圈后从最早保留通知恢复、
4 写者各 16000 次尝试的字段一致性/无重复/成功数与序号一致、压力后收发恢复，
以及空参数、关闭、非正超时和序号耗尽。压力测试允许丢弃，不以收齐全部通知
为通过条件，但每条收到的通知都必须对应成功的发布。

暂停编排通过 friend 测试访问器取得实际发布锁，再恢复执行 Notify 使用的
同一个 `PublishLocked` 实现；没有生产运行时回调或睡眠注入。fork+exec 子进程
分别持槽位锁/发布锁、独立打开并广播读取 128 条通知，验证无后续消息时的超时。
独立 wire fixture 核对实际头部，并覆盖旧无版本布局、1 字节/错误段长、错误
magic/版本/大小/对齐/容量/偏移，以及零标记未初始化区；拒绝前后比较整个共享区，
检查资源未被删除、映射已解除。

父子管道和 waitpid 等待均有 5 秒上限，子进程 Listen 使用 1000ms 或 30ms
超时；父进程析构会终止并回收未退出的子进程。每个测试只移除自身独占创建的
SysV 资源，不清理全局通知区。运行器另给整个程序 30 秒硬超时。
以下是仓库根目录的可复用示例：

```sh
make -C example -j2 BUILD_DIR=/tmp/cmw-guide-notifier test_condition_notifier
CMW_PATH="$PWD" bash example/run_tests.sh \
  --bin-dir /tmp/cmw-guide-notifier/bin --timeout 30 test_condition_notifier
```

中间件相关回归共享默认通知区。若本机仍保留旧布局，可在系统允许非特权用户
命名空间时用 `unshare --user --map-root-user --ipc` 为测试隔离 IPC，不触碰旧区：

```sh
make -C example -j2 BUILD_DIR=/tmp/cmw-guide-notifier \
  test_shm_block_lease_generation test_shm_loaned_message
CMW_PATH="$PWD" unshare --user --map-root-user --ipc \
  bash example/run_tests.sh --bin-dir /tmp/cmw-guide-notifier/bin --timeout 90 \
  test_shm_block_lease_generation test_shm_loaned_message
```

ASan、UBSan 可分别用 `SANITIZE=address`、`SANITIZE=undefined` 和独立 BUILD_DIR
构建此目标。TSan 用 `SANITIZE=thread`，线程测试使用同一映射，便于检测实际
槽位复制冲突；TSan 不证明跨进程同步正确性，独立进程行为由 exec 测试验证。
若 TSan 启动报 `unexpected memory mapping`，可在系统允许时用
`setarch x86_64 -R` 仅关闭该测试进程的 ASLR 后复验；须保留首次失败记录，
不得把未启动的尝试记为通过。
本测试只验证通知与明确编排的同步，不验证进程崩溃恢复或真实跨主机通信。
`test_hybrid_shm_multiprocess` 与 `test_loaned_message_dynamic_shm_lifecycle`
在父进程测试结束后显式 Shutdown 调度器和已创建的 SHM dispatcher，确保工作线程
先于进程静态调度表退出；测试断言失败后也执行该清理。
具体返回规则、ABI 和并发边界以 [README](../README.md#notifier-槽位保护与丢弃策略)
为准，实际命令与 sanitizer 成功或环境阻止启动的结果见 [testlog](testlog.md)。

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
make -C example -j4 BUILD_DIR=/tmp/cmw-guide-layout-normal \
  test_shm_segment_exec test_shm_segment_robustness \
  test_shm_block_lease_generation test_shm_loaned_message \
  test_posix_segment_multiprocess
CMW_PATH="$PWD" bash example/run_tests.sh \
  --bin-dir /tmp/cmw-guide-layout-normal/bin --timeout 30 \
  test_shm_segment_exec test_shm_segment_robustness \
  test_shm_block_lease_generation test_shm_loaned_message \
  test_posix_segment_multiprocess
```

UBSan 必须使用独立 BUILD_DIR；此开关同时为全部 C++ 源文件和链接添加
`-fsanitize=undefined,vptr -fno-sanitize-recover=all`，并分别使用 `-fPIE`、`-pie`。
不要关闭 ASLR：

```sh
make -C example -j4 BUILD_DIR=/tmp/cmw-guide-layout-ubsan SANITIZE=undefined \
  test_shm_segment_exec
CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  bash example/run_tests.sh --bin-dir /tmp/cmw-guide-layout-ubsan/bin \
  --timeout 30 test_shm_segment_exec
```

布局升级和旧段处理要求见 [README 的兼容性说明](../README.md#共享区布局-v2-与兼容性)。

## 发送端生命周期同步回归

发送端同步约定、旧 Loan 行为及 API 并发边界见
[README 的生命周期说明](../README.md#发送端生命周期与并发边界)。

测试入口：

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
这些测试按单机环境设计，不构成真实跨主机验证。

在仓库根目录执行，务必设置 CMW_PATH；不同构建必须使用不同目录：

```sh
new_tests='test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_loaned_message_discovery_churn'
make -C example -j3 BUILD_DIR=/tmp/cmw-guide-lifecycle-normal $new_tests
CMW_PATH="$PWD" bash example/run_tests.sh --bin-dir /tmp/cmw-guide-lifecycle-normal/bin --timeout 90 $new_tests
CMW_PATH="$PWD" make -C example -j3 BUILD_DIR=/tmp/cmw-guide-lifecycle-normal check
CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  make -C example -j3 BUILD_DIR=/tmp/cmw-guide-lifecycle-ubsan SANITIZE=undefined check
```

`SANITIZE=address` 和 `SANITIZE=thread` 分别为所有 C++ 对象和链接添加
ASan/TSan，带 frame pointer，使用 non-PIE 可执行文件。未关闭任何 sanitizer
检查。针对性执行方法：

```sh
asan_tests="$new_tests test_shm_transmitter_lifecycle_regression test_loaned_message_hybrid test_shm_block_lease_generation test_rtps_lifecycle_regression test_loaned_message_rtps_multiprocess"
make -C example -j3 BUILD_DIR=/tmp/cmw-guide-lifecycle-asan SANITIZE=address $asan_tests
CMW_PATH="$PWD" ASAN_OPTIONS=halt_on_error=1 \
  bash example/run_tests.sh --bin-dir /tmp/cmw-guide-lifecycle-asan/bin --timeout 90 $asan_tests
tsan_tests='test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_shm_transmitter_lifecycle_regression'
make -C example -j2 BUILD_DIR=/tmp/cmw-guide-lifecycle-tsan SANITIZE=thread $tsan_tests
CMW_PATH="$PWD" TSAN_OPTIONS=halt_on_error=1 \
  bash example/run_tests.sh --bin-dir /tmp/cmw-guide-lifecycle-tsan/bin --timeout 30 $tsan_tests
```

SANITIZE 开关只对本项目的编译与链接生效，不会重编译外部 Fast DDS 库。
记录结果时须说明外部库是否经过 sanitizer 构建。sanitizer 与压力测试只覆盖
实际执行路径，资源安全还需结合 README 中的锁与所有权关系判断。

## 性能程序

- `shm_segment_benchmark`：以相同跨进程 Block 读写路径比较 POSIX/XSI 的稳态延迟与吞吐。
- `shm_zero_copy_benchmark`：比较 INTRA、SHM 复制、SHM Loan 和强制 RTPS 的性能，`--quick` 用于缩短运行。输出到终端和当前目录下的 `build/shm_zero_copy_benchmark.csv`。

从仓库根目录进入 example 后执行示例，使用默认 build 目录以匹配 CSV 输出路径：

```sh
cd example
make -j2 benchmarks
CMW_PATH="$(cd .. && pwd)" ./build/bin/shm_segment_benchmark
CMW_PATH="$(cd .. && pwd)" ./build/bin/shm_zero_copy_benchmark --quick
```

性能程序不属于 check 的通过/失败回归；实测参数、命令、输出及结果统一记录到 testlog。


## 日志路径回归与输出保存

运行日志的位置由 [README 的日志规则](../README.md#统一日志目录) 统一定义。
构建、测试输出如果重定向到 `.log`，也必须写到根目录 `log/` 下。
该目录含日志实现源码，不要整体删除或将整个目录加入 Git 忽略规则。

`test_logger_paths` 已接入 check-fast；单独构建仅需要 logger、gtest 和 pthread，
不启动 Transport/Discovery/调度器。它从不同工作目录验证编译时根目录，并在
根目录 log/ 内创建临时目录验证 CMW_PATH、自动创建目录、名称处理、追加、
轮转及失败路径。只清理自身的临时文件，不改动已有日志。

从仓库根目录运行示例：

```sh
make -C example -j2 BUILD_DIR=/tmp/cmw-guide-log-paths test_logger_paths > log/logger-paths-build.log 2>&1
CMW_PATH="$PWD" bash example/run_tests.sh \
  --bin-dir /tmp/cmw-guide-log-paths/bin --timeout 30 test_logger_paths \
  > log/logger-paths-test.log 2>&1
```

运行前选择未占用的输出文件名，避免覆盖需保留的记录。实际执行命令、退出码、
结果和日志路径应追加到 testlog.md；make clean 不删除 log/ 中的日志。
