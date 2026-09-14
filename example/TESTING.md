# 构建、测试与性能实验

本页所有命令都从**仓库根目录**执行，是可复用操作示例；实际执行结果在 [testlog.md](testlog.md)。
只想看通信演示，直接运行 `./example/demo/run_demo.sh`，详见 [Demo 指南](demo/README.md)。

## 先跑快速回归

准备 Linux、g++、make、Fast DDS 和 GoogleTest；[Fast DDS 路径检查](../doc/fastrtps.md)。

```bash
export CMW_PATH="$PWD"
unshare --user --map-root-user --ipc make -C example -j2 check-fast
```

这会构建并顺序运行快速回归。`unshare` 为测试创建独立 IPC 环境，避开旧通知区；不改变网络或系统配置。
如果宿主通知区已确认兼容，可直接运行 `make -C example -j2 check-fast`。若隔离权限被禁止，不能把未运行的测试当作通过。

看 runner 的 `[PASS]`、`[FAIL]`、`[TIME]` 和最后汇总；只有整体退出 0 才算该组通过。

## 选择正确的入口

| 命令（在根目录执行） | 行为 |
| --- | --- |
| `make -C example -j2` | 只构建 publisher、subscriber |
| `make -C example -j2 tests` | 只构建正式回归程序 |
| `make -C example -j2 check-fast` | 构建并运行快速回归 |
| `make -C example -j2 check-integration` | 构建并运行多进程、Discovery、RTPS 等集成回归 |
| `make -C example -j2 check` | 依次运行 fast、integration；fast 失败也会尝试 integration |
| `make -C example -j2 demos` | 只构建旧手工示例，不是新的 A～E 通信 Demo |
| `make -C example -j2 benchmarks` | 只构建性能程序，不运行 |
| `make -C example -f demo/Makefile -j2 demo-transport` | 只构建新的通信 Demo |

`-j2` 只控制编译并行度；测试进程按顺序运行。不要同时运行多套使用同一通知区的回归。
默认构建目录是 `example/build/`，二进制在其 `bin/` 下。换编译参数、ABI 或 sanitizer 时使用新的 `BUILD_DIR`，不要混用旧对象。

### 只运行一个测试

```bash
export CMW_PATH="$PWD"
make -C example -j2 test_serialize
bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 30 test_serialize
```

可在目标后继续列其他测试名。GoogleTest 过滤示例：

```bash
GTEST_FILTER='*' bash example/run_tests.sh \
  --bin-dir "$PWD/example/build/bin" --timeout 30 test_serialize
```

将 `*` 改为所需测试名或模式，并在实际记录中保留该过滤条件。

### 超时与日志

`check-fast` 默认每个程序 30 秒，`check-integration` 默认 90 秒，可调整：

```bash
CMW_PATH="$PWD" unshare --user --map-root-user --ipc \
  make -C example -j2 CHECK_FAST_TIMEOUT=45 CHECK_INTEGRATION_TIMEOUT=120 check
```

runner 为每个程序创建独立进程组。缺失二进制、非零退出、信号退出或超时都失败；超时时只回收该测试进程组。
保存输出时使用根目录 `log/` 下的新目录，例如：

```bash
RUN_DIR=$(mktemp -d "$PWD/log/test-run-XXXXXX")
CMW_PATH="$PWD" bash example/run_tests.sh \
  --bin-dir "$PWD/example/build/bin" --timeout 30 test_serialize \
  > "$RUN_DIR/test.log" 2>&1
```

退出码和命令需要一并记录到 testlog；日志文件不能替代结果摘要。

## 测试覆盖

快速回归：

| 程序 | 检查内容 |
| --- | --- |
| `test_serialize` | DataStream 边界、类型、截断和失败传播 |
| `test_qos` | 默认值、预设、归一化和非法配置、RTPS 属性映射、心跳单位、元数据、独立缓存、慢消费者及共享 Receiver 冲突 |
| `test_blocker`、`test_node`、`test_publisher_subscriber` | 基础 API、同进程消息字段往返 |
| `test_transport_mode_selection` | 按 IP/PID 元数据选路 |
| `test_shm_segment_robustness`、`test_shm_block_lease_generation` | Segment 边界、Lease、generation、通知完整性 |
| `test_shm_dispatcher_robustness`、`test_shm_transmitter_receiver` | 异常输入、重建、超限后恢复 |
| `test_shm_loaned_message`、`test_shm_transmitter_lifecycle_regression`、`test_loaned_message_hybrid` | Loan 存储、只读、epoch 与启停 |
| `test_hybrid_intra` | 真实 Node/Discovery 同进程通信、多 peer 离开与加入 |
| `test_intra_transmitter_lifecycle` | 同步重入、发送与后端启停并发 |
| `test_logger_paths` | 日志路径、追加、轮转和失败处理 |

集成回归：

| 程序 | 检查内容 |
| --- | --- |
| `test_qos_rtps` | 历史容量、满载拒绝、可靠性匹配、接收端释放容量后的重传、跨 exec 晚加入回放、共享 RTPS Reader 冲突 |
| `test_condition_notifier` | 槽位互斥、丢弃、绕环、并发、fork+exec 广播、布局拒绝 |
| `test_shm_segment_exec` | POSIX/XSI 跨 exec 读写、重开、布局拒绝 |
| `test_posix_segment_multiprocess` | POSIX 两进程读写、重开和清理 |
| `test_shm_loaned_message_multiprocess` | 跨进程只读 Loan、持有和释放 Lease |
| `test_hybrid_shm_multiprocess` | Subscriber 先启动，真实 Discovery 自动 SHM |
| `test_loaned_message_dynamic_shm_lifecycle` | 已编排的跨进程 Loan 订阅关系 LEAVE/JOIN 后恢复 |
| `test_hybrid_dynamic_shm_lifecycle` | 普通 SHM 订阅关系变化，含超过 255 字节的拓扑通知 |
| `test_rtps_same_host_multiprocess` | 同机两个进程显式强制 RTPS |
| `test_rtps_lifecycle_regression` | 普通 RTPS 启停、探针确认后发送和重复检查 |
| `test_loaned_message_rtps_multiprocess` | Loan RTPS 线格式、heap-backed 只读收发 |
| `test_rtps_transmitter_lifecycle` | RTPS 并发启停、恢复、受控 Hybrid 路由变化 |
| `test_loaned_message_discovery_churn` | 持续 Loan 发布下的多轮 INTRA/SHM 订阅变化与混合拓扑 |
| `test_discovery_late_join` | Publisher 先启动，三个全新 exec 进程读取旧 Writer 公告并连续接收；正常退出后再加入 |

`test_node_manager`、`test_channel_manager`、`test_croutine`、`test_scheduler`、`test_task`、`test_class_loader`
仍是旧手工检查，不属于 `check`。能单独构建不代表已经有完整自动验收合同。

### Notifier 槽位保护回归

`test_condition_notifier` 只链接 Notifier、ReadableInfo、Logger、pthread、atomic、GoogleTest，不依赖 Fast DDS 或调度器。
需要 Linux SysV SHM、fork/exec、pipe/poll/waitpid。

```bash
export CMW_PATH="$PWD"
make -C example -j2 test_condition_notifier
bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 30 test_condition_notifier
```

测试通过受控持锁检查丢弃不推进序号、读超时不改输出、绕环及恢复；4 个写者各尝试 16000 次，允许丢弃，但每条收到的通知必须对应成功发布。
exec 用例验证独立打开和广播，以及错误长度、magic、版本、大小、对齐、容量、偏移和未初始化布局；拒绝前后核对原区未改写或删除。
父子控制等待最多 5 秒，仅清理测试独占的 SysV 资源。上面 runner 给整个程序 30 秒；作为 integration 一部分运行时沿用该组超时。
不验证持锁进程崩溃恢复。[运行库约定](../README.md#notifier-槽位保护与丢弃策略)。

### 共享区无 vptr 回归

`test_shm_segment_exec` 使用 fork+exec 打开真实 POSIX/XSI Segment，校验读写、重开与布局拒绝；父进程 waitpid 截止为 10 秒。
拒绝用例检查旧虚表布局、错误尾标记/ABI/容量、截断以及 State 与尾标记不一致，并确认共享字节未被修改。

```bash
export CMW_PATH="$PWD"
make -C example -j2 test_shm_segment_exec test_shm_segment_robustness test_posix_segment_multiprocess
unshare --user --map-root-user --ipc bash example/run_tests.sh \
  --bin-dir "$PWD/example/build/bin" --timeout 30 \
  test_shm_segment_exec test_shm_segment_robustness test_posix_segment_multiprocess
```

只清理本测试创建的资源。[布局兼容性](../README.md#共享区布局-v2-与兼容性)。

### 发送端生命周期同步回归

```bash
export CMW_PATH="$PWD"
TARGETS='test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_loaned_message_discovery_churn'
make -C example -j2 $TARGETS
unshare --user --map-root-user --ipc bash example/run_tests.sh \
  --bin-dir "$PWD/example/build/bin" --timeout 90 $TARGETS
```

这些测试用条件变量、管道、真实拓扑计数和有限时间接收编排启停。切换期间允许发送失败/丢弃，恢复后验证内容和重复。
受控 host 元数据只验证选路，不是真实跨主机。Payload 序号与 DDS Writer 序号也不同，Writer 重建后后者可以重新计数。
仅覆盖实际编排的交错，不能证明没有所有竞态。[支持的并发范围](../README.md#发送端生命周期与并发边界)。

## Sanitizer 构建

每种配置用独立目录。以下是 UBSan 的可复制示例：

```bash
export CMW_PATH="$PWD"
BUILD_DIR=$(mktemp -d /tmp/cmw-guide-ubsan-XXXXXX)
make -C example -j2 BUILD_DIR="$BUILD_DIR" SANITIZE=undefined test_shm_segment_exec
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  unshare --user --map-root-user --ipc bash example/run_tests.sh \
  --bin-dir "$BUILD_DIR/bin" --timeout 30 test_shm_segment_exec
```

其他模式同样换新 BUILD_DIR，再替换构建与运行选项：

| 工具 | Make 参数 | 运行环境变量 | 构建方式 |
| --- | --- | --- | --- |
| UBSan/vptr | `SANITIZE=undefined` | `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` | PIE，不关闭 ASLR |
| ASan | `SANITIZE=address` | `ASAN_OPTIONS=halt_on_error=1` | frame pointer、non-PIE |
| TSan | `SANITIZE=thread` | `TSAN_OPTIONS=halt_on_error=1` | frame pointer、non-PIE |

开关只重编译本项目，不会重编译外部 Fast DDS；报告时说明外部库构建方式。
TSan 不证明跨进程同步正确性；`unexpected memory mapping` 属于启动受阻，应保留失败记录。
系统允许时可用 `setarch x86_64 -R` 仅对 TSan 进程复验，不能把未启动的尝试记为通过。

## QoS 回归

功能语义及兼容性以 [README](../README.md#qos-配置与执行边界) 为准。
以下是可复用命令，实际结果另记 [testlog](testlog.md)：

```bash
BUILD_DIR="$PWD/example/build-qos"
make -C example BUILD_DIR="$BUILD_DIR" -j2 test_qos test_qos_rtps test_node test_discovery_late_join
CMW_PATH="$PWD" CMW_IP=127.0.0.1 GTEST_FILTER='*' \
  unshare --user --map-root-user --ipc bash example/run_tests.sh \
  --bin-dir "$BUILD_DIR/bin" --timeout 90 test_qos test_qos_rtps test_node test_discovery_late_join
```

- `test_qos` 不创建 DDS 网络端点，验证业务 VOLATILE / Discovery TRANSIENT_LOCAL 默认值、
  属性映射和内存中的缓存行为。
  慢消费者用例暂停 DataVisitor 消费再灌入突发消息，验证溢出后跳到最新消息；
  这不是调度延迟或无损吞吐测试。
- `test_node` 同时检查不同创建重载的默认值、独立观察深度、共享 QoS 冲突后的清理和重试。
- `test_qos_rtps` 的底层 History 用例关闭 Fast DDS 进程内直送并使用 UDPv4，
  覆盖 VOLATILE 晚加入只收新消息、KEEP_ALL 回收已完成投递的样本且保留未确认样本、
  显式 TRANSIENT_LOCAL 的 KEEP_LAST 淘汰及 Discovery KEEP_ALL 满载拒绝与公告回放。
  可靠性用例覆盖不匹配和匹配、接收端容量释放后的协议重传；被拒收的样本只发布一次。
  生产 Transmitter/Receiver 用例验证共享 QoS 冲突不会替换原 listener。
- 跨进程晚加入由 `test_discovery_late_join` 覆盖：全新进程在 Reader JOIN 前发现旧 Writer，检查业务公告为
  VOLATILE，随后经自动 SHM 接收新消息。这不代表业务历史补发或真实跨主机验证。
- 原始 DDS 端点先删除再释放 History，生产路径结束时 Shutdown Transport。
  runner 为整个程序提供 90 秒超时和进程组清理；日志按根目录 `log/` 规则保存。

依赖沿用上述 Fast DDS 2.12、GoogleTest、Linux UDP 和 `unshare` 环境；
所有进程须使用本轮重新构建的二进制，不能用旧的六字段 QoS 程序参与验证。

## 性能程序

`shm_benchmark_sender` 与 `shm_benchmark_receiver` 比较普通 SHM 序列化和 SHM Loan，直接使用后端，不经过自动选路。
`shm_segment_benchmark` 保留 POSIX/XSI Block 比较。旧单进程 `shm_zero_copy_benchmark` 已删除，旧结果不能与新口径混合。

### 构建和运行

依赖 Linux POSIX SHM、SysV 通知区、Unix socket、`/proc`、Python 3.8+、Fast DDS 链接库。
共享段约 256 MiB；确认 `/dev/shm` 和内存足够。以下运行不绑核；需要固定 CPU 时另加参数并记录。

```bash
export CMW_PATH="$PWD"
BUILD_DIR="$PWD/example/build-benchmark"
make -C example -j2 BUILD_DIR="$BUILD_DIR" OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver
"$BUILD_DIR/bin/shm_benchmark_receiver" --self-test
PYTHONDONTWRITEBYTECODE=1 python3 example/test_shm_benchmark_stats.py
RUN_DIR="$PWD/log/shm-benchmark-$(date +%Y%m%d-%H%M%S)-$$"
PYTHONDONTWRITEBYTECODE=1 unshare --user --map-root-user --ipc \
  python3 example/run_shm_benchmark.py --bin-dir "$BUILD_DIR/bin" --output-dir "$RUN_DIR" \
  --sizes 4096 65536 1048576 4194304 --warmup-ms 1000 --duration-ms 5000 \
  --drain-ms 1000 --repeats 3
```

如果 BUILD_DIR 原来用了其他优化/sanitizer 参数，先换目录或清理自己的构建产物。Make 不会仅因变量变化就重编译所有对象。
runner 要求输出目录在 `log/` 下且尚不存在，自动创建；不要提前 `mkdir "$RUN_DIR"`。

| 选项 | 用途 |
| --- | --- |
| `--sizes` | 支持 4096、65536、1048576、4194304 字节 |
| `--warmup-ms` / `--duration-ms` | 预热/正式测量，各 100～60000 ms |
| `--drain-ms` | 排空，100～10000 ms |
| `--repeats` | 正式比较使用 3 轮，第二轮交换 copy/loan 次序 |
| `--sender-cpu` / `--receiver-cpu` | 需 taskset；CPU 编号必须在本机允许范围内 |

短测可把预热/正式/排空设为 200/300/200 ms，并只选 4096、4194304 两档；不能把短测当作上面的正式五秒结果。
隔离 IPC 不会隔离 CPU、内存或虚拟机的其他负载。[已有三轮结果](testlog.md#2026-09-11-独立进程-shm-性能实验)。

### 双终端手动运行

两端需要兼容且相同的 IPC 环境；[如何共享隔离环境](demo/README.md#第一步让两端共享同一-ipc-环境)。
两端从仓库根目录执行，参数保持一致，10 秒内先启动 receiver、再启动 sender：

```bash
# 终端一
CMW_PATH="$PWD" ./example/build-benchmark/bin/shm_benchmark_receiver \
  --mode loan --size 4096 --channel manual_shm_bench_1 \
  --control "$PWD/log/manual_shm_bench_1.sock" --output "$PWD/log/manual_shm_bench_1-receiver.json" \
  --warmup-ms 1000 --duration-ms 5000 --drain-ms 1000
```

```bash
# 终端二
CMW_PATH="$PWD" ./example/build-benchmark/bin/shm_benchmark_sender \
  --mode loan --size 4096 --channel manual_shm_bench_1 \
  --control "$PWD/log/manual_shm_bench_1.sock" --output "$PWD/log/manual_shm_bench_1-sender.json" \
  --warmup-ms 1000 --duration-ms 5000 --drain-ms 1000
```

`--mode` 可选 `copy|loan`。每轮双方一起换新 channel、socket 和结果文件名；不要覆盖旧记录。
夹具固定 32 槽、每槽 8 MiB，不可通过参数改变；最多支持 16,777,216 次正式尝试，超限结果无效。

### 阶段和测量口径

| 阶段 | 做什么 | 是否计入结果 |
| --- | --- | --- |
| 连接 | socket 校验参数/PID；真实 SHM PROBE 校验内容、映射、类型、容量和 Loan 属性 | 不计 |
| 预热 | 发送 WARMUP，接收 BARRIER 后再留 300 ms | 时间、数量、CPU 都不计；阶段污染使整组无效 |
| 正式窗口 | 两端用 CLOCK_MONOTONIC 的同一绝对 `[start,end)`，持续发送、不逐条等待确认 | 只统计本窗口完成校验的有效接收 |
| 排空 | 发布停止后保持 Segment，截止为 `max(end,sender_stop)+drain_ms` | 排空接收单列，不计吞吐/正式 CPU |

两种模式都逐条生成数据、接收端逐字节校验。普通路径包含分配、序列化和反序列化；Loan 直接填借出缓冲。
Payload 包含 24 字节业务头；额外 DataStream 头单列 `serialized_size`，不算有效字节。

| 指标 | 含义 |
| --- | --- |
| `send_success` | API 返回 true；不等于送达 |
| `acquire_fail` / `transmit_fail` | 获取 Loan 失败 / 发送 API 失败；不能全部归为资源耗尽 |
| 有效吞吐 MiB/s | `window_unique × size_bytes / 正式秒数 / 2^20` |
| `drain_unique` | 排空阶段唯一有效接收，排除在吞吐分子外 |
| `missing_success_after_drain` | 成功发送 bitmap 减接收 bitmap；排除已失败的尝试 |
| `missing_attempts_after_drain` | 所有尝试中的未收到数，另列失败尝试 |
| `received_failed_send` | API 返回 false 但仍收到，不能假定失败必然未投递 |
| `CPU%` | 进程全部线程 CPU 时间 / 实际采样墙钟时间 × 100；100% 是一个逻辑核 |

消息按完成全内容校验的时刻归窗并去重；窗口/排空重复分别统计。
最后一次发送可能跨过 end，另记 `completion_after_end`；截止后才完成的接收另记 `after_cutoff`，不计有效接收。
CPU 采样使用 CLOCK_PROCESS_CPUTIME_ID，任一采样边界延迟超过 20 ms，runner 拒绝该组。

这测量“内容准备 + 传输 + 全量校验”，没有纯传输或延迟模式。日志为 ERROR 且关闭 console，仍包含日志构造成本。
缺失原因无法从现有 API 完整区分；不要把缺失都归因于某一种锁/容量耗尽，也不要外推极限无丢失或跨主机性能。

### 输出、校验和清理

| 文件 | 保存内容 |
| --- | --- |
| `manifest.json` | 环境、HEAD、二进制/源码 SHA256、完整收发命令、PID、退出码与验证状态 |
| `results.csv` | 每轮计数、吞吐、CPU、原始时间 |
| `summary.json` / `summary.md` | 三轮中位数及 `[min,max]`，范围不是置信区间 |

子进程失败/超时、内容/阶段/路径/计数错误或 CPU 边界超限都会非零退出，不生成整套成功汇总。缺失和重复如实保留。
连接及探针各最多 10 秒，控制 socket 读写最多 90 秒；每对进程总上限为预热+正式+排空+35 秒，terminate 后 3 秒仍不退出才 kill。
两端退出后 runner 仅清理本轮确切段名和 socket；隔离通知区随 IPC 环境退出回收，宿主旧区不动。

`receiver --self-test` 和 Python 统计测试使用合成数据验证校验/统计，不代替真实双进程路径验证。

## 环境与清理

- `CMW_PATH` 指向仓库根目录，Fast DDS 目录通过 `FAST_DDS_HOME` 指定；不需要修改系统配置来运行这些示例。
- 遇到 `incompatible notifier shm layout`，先用同一独立 IPC 环境运行相关进程。不要清空 `/dev/shm`、批量 ipcrm 或删除归属不明的通知区。
- 正常测试按自身所有权回收资源；异常退出时只检查记录中的确切 PID、channel 段名和 socket。确认无人使用前不手动删除共享资源。
- `make -C example clean` 删除其 BUILD_DIR 和原 class-loader 插件产物，保留日志；不会替你停止运行中的程序。
- 同机自动 SHM、同机强制 RTPS、受控 host 路由和真实跨主机必须分开记录。正常退出恢复不等于 SIGKILL 崩溃恢复。

## 日志路径回归与输出保存

运行库日志写入根目录 `log/`，具体规则见 [README](../README.md#统一日志目录)。
该目录含 Logger 源码，不可整体删除或忽略。Demo 仅将自己的日志归档到 `example/demo/runs/`。

`test_logger_paths` 属于 check-fast，只依赖 logger、gtest、pthread；验证路径选择、目录创建、文件名处理、追加、轮转和失败，不启动通信模块。

```bash
export CMW_PATH="$PWD"
RUN_DIR=$(mktemp -d "$PWD/log/logger-check-XXXXXX")
make -C example -j2 test_logger_paths > "$RUN_DIR/build.log" 2>&1
bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 30 test_logger_paths \
  > "$RUN_DIR/test.log" 2>&1
```

## 全新订阅进程自动发现回归

在仓库根目录运行：

```bash
make -C example -j2 test_discovery_late_join
export CMW_PATH="$PWD"
unshare --user --map-root-user --ipc \
  bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 90 test_discovery_late_join
```

测试先创建 Publisher，再依次启动三个独立订阅进程。每轮先通过 Discovery 查询旧 Writer，
确认此前已离开的 Writer 没有残留，再创建 Subscriber 并验证至少 10 条连续消息。
它检查同一发布端 PID、Writer ID、递增序号及真实离线事件，不调用 `Join` 补公告。
日志中的 `before Reader JOIN`、三次 `round=... PASS` 和最终测试退出 0 共同表示通过。

发现和接收各限时 10 秒，父进程等待事件限时 15 秒，整项由 runner 限时 90 秒。
每轮正常关闭 Subscriber 并等待子进程退出；失败时仅回收本测试创建的子进程。
私有 IPC 环境随测试结束释放，不清理宿主共享内存。仅验证同机普通消息；
未验证历史公告淘汰、跨主机或 SIGKILL 恢复，也不承诺离线补发。
[实际失败与复验记录](testlog.md#2026-09-13-discovery-全新订阅进程自动发现修复)。

## 面试通信 Demo

[Demo 指南](demo/README.md) 负责 A～E、一条命令运行、双终端操作、参数、超时与清理。
Demo 源码及独立构建产物不进入原测试/benchmark 目标；D 使用原生 Discovery 恢复，无需 Writer 重公告。
[首次验收](testlog.md#2026-09-12-面试通信-demo-本地验收)；
[Discovery 修复复验](testlog.md#2026-09-13-discovery-全新订阅进程自动发现修复)。
