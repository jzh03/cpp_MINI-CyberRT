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

`make check-integration` 构建并顺序运行需要 fork、Discovery 收敛或强制 RTPS 的
回归：`test_shm_segment_exec`、`test_posix_segment_multiprocess`、`test_loaned_message_dynamic_shm_lifecycle`、
`test_shm_loaned_message_multiprocess`、`test_hybrid_shm_multiprocess`、
`test_rtps_same_host_multiprocess`、`test_hybrid_dynamic_shm_lifecycle`、
`test_rtps_lifecycle_regression`、`test_loaned_message_rtps_multiprocess`。
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
