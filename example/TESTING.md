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

`shm_segment_benchmark` 保留 POSIX/XSI Block 读写比较。旧的单进程
`shm_zero_copy_benchmark` 已删除，由 `shm_benchmark_sender` 与
`shm_benchmark_receiver` 取代；旧数据不应与新吞吐/CPU 口径合并。
`make benchmarks` 只构建性能程序；它们不属于 `check` 的自动回归。

### 构建和运行

依赖 Linux 的 POSIX SHM、System V 通知区、Unix socket、`/proc`、Python 3.8+、
Fast DDS（仅构建链接依赖，不使用 DDS 数据路径），绑核示例还需 `taskset`。
以下是可复用示例，实际执行记录见 [testlog](testlog.md#2026-09-11-独立进程-shm-性能实验)。
必须用独立优化构建目录，避免混用默认未优化或 sanitizer 对象；改变 OPTFLAGS
时使用新 BUILD_DIR 或先清理对应构建目录，Make 不会仅因变量值变化重编译对象。

```sh
cd /home/jim/cpp/CyberRT
BUILD_DIR="$PWD/example/build-benchmark"
make -C example -j2 BUILD_DIR="$BUILD_DIR" OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver
"$BUILD_DIR/bin/shm_benchmark_receiver" --self-test
PYTHONDONTWRITEBYTECODE=1 python3 example/test_shm_benchmark_stats.py
RUN_DIR="$PWD/log/shm-benchmark-$(date +%Y%m%d-%H%M%S)"
CMW_PATH="$PWD" PYTHONDONTWRITEBYTECODE=1 unshare --user --map-root-user --ipc \
  python3 example/run_shm_benchmark.py --bin-dir "$BUILD_DIR/bin" --output-dir "$RUN_DIR" \
  --sizes 4096 65536 1048576 4194304 --warmup-ms 1000 --duration-ms 5000 \
  --drain-ms 1000 --repeats 3 --sender-cpu 0 --receiver-cpu 1
```

`unshare` 让所有收发进程共享一个新的 IPC 命名空间，隔离旧版 System V 通知区；
它不改变 POSIX 数据路径，也不隔离 CPU、内存或其他 VM 负载。宿主环境已干净时
可去掉 `unshare`。若系统禁止用户命名空间，应使用管理员提供的兼容 IPC 环境，
不得自动删除其他程序的通知区。CPU 编号按本机允许的 affinity 选择；省略两个
`--*-cpu` 参数则不绑核，必须在解释结果时说明。

runner 串行执行四档 × 两种模式 × 三次，第二次交换 copy/loan 顺序，避免所有
copy 都先测。短测可选 `--sizes 4096 4194304 --warmup-ms 200 --duration-ms 300
--drain-ms 200`，仍重复三次，不能冒充正式五秒测量。输出目录必须是 `log/`
下的新目录，已有目录会被拒绝，以免覆盖历史结果。

两个二进制也可分别从两个终端启动；双方参数必须完全一致，仅 `--output` 不同：

```sh
# 终端 1；需要干净且与终端 2 相同的 IPC 命名空间。
cd /home/jim/cpp/CyberRT
CMW_PATH="$PWD" ./example/build-benchmark/bin/shm_benchmark_receiver \
  --mode loan --size 4096 --channel manual_shm_bench_1 \
  --control "$PWD/log/manual_shm_bench_1.sock" --output "$PWD/log/manual_shm_bench_1-receiver.json" \
  --warmup-ms 1000 --duration-ms 5000 --drain-ms 1000
# 终端 2，在终端 1 启动后的 10 秒内执行。
cd /home/jim/cpp/CyberRT
CMW_PATH="$PWD" ./example/build-benchmark/bin/shm_benchmark_sender \
  --mode loan --size 4096 --channel manual_shm_bench_1 \
  --control "$PWD/log/manual_shm_bench_1.sock" --output "$PWD/log/manual_shm_bench_1-sender.json" \
  --warmup-ms 1000 --duration-ms 5000 --drain-ms 1000
```

手动测试须为每次运行选择新 channel、socket、结果文件名。`--mode` 为 `copy|loan`；
`--size` 只接受上述四档，非法参数以非零退出。预热/正式时长范围为 100–60000 ms，
排空为 100–10000 ms。32 个槽位、8 MiB 槽容量固定不可调，实际 Segment 为
`ShmConf(8 MiB).managed_shm_size()`（约 256 MiB）；需要足够的 `/dev/shm` 和内存。
序号统计最多支持 16,777,216 次正式尝试，达到上限的结果无效。当前库最大 SHM
消息为 32 MiB，本基准只声明四档支持；不外推更大 Payload。

### 阶段和测量口径

1. **连接就绪**：Unix socket 校验参数和不同 PID；目标 SHM 路径收到并完整校验
   PROBE 后才就绪。检查 `/proc/self/maps`、POSIX 段大小、`State` 消息类型和容量。
   Loan 每次借出和每次回调还校验 SHM-backed、channel、block、generation，接收只读。
   copy 统计实际序列化/反序列化调用，不能仅凭命令行模式认定走了 SHM。
2. **预热**：持续发送独立 WARMUP 阶段，随后停止并发送 BARRIER；接收线程处理
   完 barrier 才确认，正式开始前再留 300 ms。迟到的预热消息不计正式结果，
   如果预热在正式开始后仍被处理，则整组结果无效。预热的墙钟、数量、CPU 全部排除。
3. **正式测量**：两进程共享 Linux `CLOCK_MONOTONIC` 的绝对 `[start,end)`，
   连续发送，不等待逐条确认。序号对应发送尝试，包含失败尝试；正式循环开始时
   在窗口内的尝试都记录，最后一条可能在 end 后完成，另记 `completion_after_end`。
   `send_success` 是 API 返回 true 的次数；`acquire_fail` 是 Loan 获取失败，
   `transmit_fail` 是发送 API 返回 false，保留已有失败/丢弃策略，不重试同一序号。
   普通路径的失败返回不能再细分为槽位、序列化或通知失败，不能全部声称资源耗尽。
4. **停止并排空**：发送端报出实际停止时间和成功序号 bitmap，并保持 Segment 存活。
   排空截止为 `max(end, sender_stop)+drain_ms`；到期停止接收并 join 回调线程。
   截止后才完成的回调另记 `after_cutoff`，不计有效接收或缺失恢复。排空到期不代表
   可靠队列承诺已送达全部消息；未收到的成功发送仍明确计入缺失。

有效接收按**完成全内容校验的时刻**归窗，再按序号去重。`window_valid_bytes =
window_unique × size_bytes`，吞吐为它除以统一正式时长，以 MiB/s（2^20 bytes）表示。
`size_bytes` 包含 24-byte magic/阶段/序号头，业务有效数据长度相同；普通路径额外
包含 `DataStream` 编码头（实际 `serialized_size` 单列），不计入有效字节。两条路径
均每条生成内容；普通 string 的分配/初始化、序列化与反序列化成本保留，Loan 在
借出 buffer 直接填充。接收端统一逐字节校验，不用三点抽样。没有预生成正式内容。

`drain_unique` 不进入吞吐分子；`duplicates_window`、`duplicates_drain` 分开报告。
最终缺失以成功序号 bitmap 对接收 bitmap 求差集，`missing_success_after_drain`
不包含 API 已报告失败的尝试。另列 `missing_attempts_after_drain` 与
`received_failed_send`（API 返回 false 但接收端仍收到），不假定失败返回保证未投递。
接收丢弃可来自旧代次通知、读锁失败、通知覆盖等，当前 API 不提供原因计数，
不能把缺失都归咎于某一种耗尽原因。单纯成功发送数不能用来计算有效吞吐。

CPU 使用 `CLOCK_PROCESS_CPUTIME_ID`，包含各进程所有线程的用户态和内核态 CPU。
在相同绝对边界采样，发送端另有一个休眠采样线程；记录实际采样时间与 CPU 差值。
`CPU% = CPU 时间 / 实际采样墙钟时间 × 100`，**100% 代表占满一个逻辑核**。
采样边界可能受调度影响，任一边界延迟超过 20 ms，runner 拒绝该组；原始纳秒
时间可审计这一误差，不能理解为完全无误差的瞬时采样。停止后的尾条与排空不计
正式 CPU，正式 end 之后的接收也不计吞吐。

本版本仅提供“数据准备 + 传输 + 接收全内容校验”的吞吐实验，没有纯传输模式，
没有逐条等待的延迟模式，也不输出单程或往返延迟。日志设为 ERROR、关闭 console，
保留库自身日志构造开销；不是移除日志后的理论极限。单机 VM 结果不代表跨主机。

### 输出、校验和清理

- `manifest.json` 保存环境、HEAD、二进制/基准源码 SHA256、每次运行的完整收发
  命令、PID、退出码、原始结果和验证状态；短测/失败也保留独立目录。
- `results.csv` 保存每轮计数、有效字节、吞吐、CPU 和边界纳秒时间；`summary.json`
  与 `summary.md` 报告各组三次的中位数及 `[min,max]`，不把范围当作置信区间。
- 任一子进程失败、超时、内容/阶段污染、路径/计数不一致或 CPU 边界延迟超限，
  runner 非零退出且不生成整套成功汇总；丢失和重复如实统计，不伪造零值。
- 连接及探针分别最多 10 秒，控制 socket 读写最多 90 秒；runner 对每对进程
  设置 `warmup + duration + drain + 35 秒` 总上限，先 terminate，3 秒后仍未退出则 kill。
- 正常退出由 Lease/Segment 引用计数回收本轮 POSIX 段；runner 在两进程退出后
  仅检查并清理 sender 输出的本轮确切段名及 socket，记录是否有遗留，不清理其他段。
  手动运行异常退出时，依据 sender 的 `segment_path` 输出确认无人使用后清理该段。
  System V 通知区随隔离 IPC 命名空间销毁，宿主旧通知区保持原样。
- 运行与构建 `.log` 均在根目录 `log/`；runner 将本轮 Logger 日志归档到输出目录。
  `make clean` 保留日志，但删除其指定 BUILD_DIR。`__pycache__` 可通过上述环境变量避免。
- `receiver --self-test` 不启动中间件，覆盖全内容损坏、阶段隔离、窗口边界、重复与
  序号越界；`test_shm_benchmark_stats.py` 用合成数据验证排空不计吞吐、CPU 公式、
  缺失公式、中位数/范围，并确认伪路径和污染结果会被拒绝。真实跨进程路径由矩阵验证。


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
