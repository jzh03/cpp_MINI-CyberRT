# 测试入口

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
回归：`test_posix_segment_multiprocess`、`test_loaned_message_dynamic_shm_lifecycle`、
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
内存序正确性。此前 TSan 曾被 VM 的 unexpected memory mapping 阻止，不能视为
通过；UBSan 和 TSan 是同一测试的不同构建方式，不需要复制测试源文件。
