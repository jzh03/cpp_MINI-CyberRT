# 测试日志

测试简介和使用指南见 [TESTING.md](TESTING.md)。本文件记录实际执行结果、
环境限制及布局测量；较早的失败记录保留，后续复跑结果单独记录。

## 2026-09-10 首轮验证

分支为 `dev`，起始 HEAD 为 `00e3126`。

- 普通新目录构建成功。exec 4 项、Segment robustness 7 项、Block/Lease/Notifier
  3 项、原 POSIX multiprocess 2 项通过。
- 完整 `test_shm_loaned_message` 的前三项通过，随后
  `PublisherApiRejectsLoanWithoutActiveShmPeer` 遇到 `getifaddrs: Operation not permitted`
  并 SIGSEGV，因此整个二进制记为失败，最后一项没有在该轮执行。
  单独使用 `GTEST_FILTER=ShmLoanedMessageTest.DirectCommitKeepsReadLeaseAndRecoversBlocks:ShmLoanedMessageTest.XsiFixedCapacityAcquireAndReadOnlyView`
  运行同一 runner，两项均通过（只读、持有期间不可复用、释放后恢复）。
- 为诊断上述失败，另外以同一 UBSan BUILD_DIR 构建 `test_shm_loaned_message`，
  用 `GTEST_FILTER=ShmLoanedMessageTest.PublisherApiRejectsLoanWithoutActiveShmPeer`
  运行：非零退出，堆栈为 `Publisher::LeaveTheTopology()` 的
  `node/publisher.h:180` 空指针访问；未修改这条 RTPS/Topology 路径。
- UBSan exec 四项全部通过；`readelf -h` 确认 PIE，
  `/proc/sys/kernel/randomize_va_space` 为 2。
- 使用 `git archive HEAD` 在 `/tmp/cmw-vptr-before` 解出修复前源码，只复制新增
  测试及 Makefile，以 `/tmp/cmw-vptr-before-build` 和 `SANITIZE=undefined`
  构建。首次磁盘临时空间不足，改 `-j2` 后成功。同一 runner 配合
  `GTEST_FILTER=ShmSegmentExecTest.Posix:ShmSegmentExecTest.Xsi` 和
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` 执行，两项均按预期失败：
  POSIX `OpenOnly():199`、XSI `OpenOnly():150` 报 `State` 的 `invalid vptr`。
- 最初复用已有 `example/build` 遇到旧对象的 ReadableInfo 析构链接错误；
  使用全新普通目录后解决，没有为此修改其他模块。
- 普通及 UBSan 结果分别保存在本机 `/tmp/cmw-normal-test.log`、
  `/tmp/cmw-loan-focused.log`、`/tmp/cmw-ubsan-test.log`；修复前证据为
  `/tmp/cmw-before-test.log`，Publisher 诊断为 `/tmp/cmw-loan-ubsan-test.log`。
  临时文件不作为仓库测试输入。GDB 因沙箱禁止 ptrace 无法运行，提权请求的
  自动审批超时；诊断采用上述 UBSan 堆栈。

## 2026-09-10 20:01 后续复跑

用户已清理并重新构建，本轮环境已解除沙箱限制。以下命令在仓库根目录运行：

```sh
CMW_PATH="$PWD" bash example/run_tests.sh \
  --bin-dir "$PWD/example/build/bin" --timeout 30 \
  test_shm_segment_exec test_shm_segment_robustness \
  test_shm_block_lease_generation test_shm_loaned_message \
  test_posix_segment_multiprocess
make -C example -j2 BUILD_DIR=/tmp/cmw-shm-rerun-ubsan \
  SANITIZE=undefined test_shm_segment_exec test_shm_loaned_message
CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  bash example/run_tests.sh --bin-dir /tmp/cmw-shm-rerun-ubsan/bin \
  --timeout 30 test_shm_segment_exec test_shm_loaned_message
```

- 普通构建五个测试程序、21 个用例全部通过，无失败或超时。
- 全新独立目录的 UBSan 构建成功；exec/布局 4 项和完整 LoanedMessage 5 项
  全部通过，无 sanitizer 报错。编译及链接保留 `undefined,vptr` 和禁止恢复参数；
  `readelf -h` 确认两个二进制均为 PIE，ASLR 系统值仍为 2。
- 此前失败的 Publisher API 用例本次在普通和 UBSan 构建中均通过；
  未出现 `getifaddrs` 权限错误，当前这组验证已无环境阻塞。
- 日志：`/tmp/cmw-shm-rerun-normal.log`、
  `/tmp/cmw-shm-rerun-ubsan-build.log`、`/tmp/cmw-shm-rerun-ubsan-test.log`。
  本轮只补充验证记录，没有修改运行时源码。

## 本次共享内存布局核对

State 和 Block 不再含虚析构；原子成员仍由 placement new 正常构造。
ReadableInfo 保留原有零值 `reserved_`，用于保留原先的首字占位，里面没有函数地址。
Indicator 本身也无多态。Segment、PosixSegment、XsiSegment、ShmConf、
ConditionNotifier/NotifierBase 是进程内管理对象，保留原有多态。

本次采用新布局，不给 State/Block 增加占位。当前 x86-64 GCC 实测如下：

| 布局项 | 版本 1 | 版本 2 |
| --- | ---: | ---: |
| State sizeof / alignof | 40 / 8 | 32 / 8 |
| Block sizeof / alignof | 40 / 8 | 32 / 8 |
| ReadableInfo sizeof / alignof | 40 / 8 | 40 / 8 |
| State 起点 | 0 | 0 |
| Block 数组起点 / 步长 | 40 / 40 | 32 / 32 |
| 默认 512 块的 Payload 起点 | 20520 | 16416 |
| 尾部布局头长度 | 24 | 32 |

一般 Payload 起点为 `sizeof(State) + block_num * sizeof(Block)`，各块数据
步长仍为 `block_buf_size`。ShmConf 原有保守分配公式不变：
`4096 + 1024 + (1024 + block_buf_size) * block_num`，默认段为 9442304 字节。
尾部元数据版本升为 2，增加容量、State/Block 对齐信息。打开时首先使用
POSIX `fstat().st_size` 或 XSI `shmctl(IPC_STAT).shm_segsz`，从实际段末尾
复制普通元数据到本地结构（允许末尾未对齐），然后核对标记、版本、ABI、
容量档位、精确总长、Payload 边界，最后才访问 State 并核对其容量。
元数据定位不依赖 State。失败只解除映射，不增加/减少引用计数、不析构对象、
不删除不兼容段。此检查用于布局兼容性，不支持其他进程并发截断现有映射。

## 更早的环境记录（日期未记录）

此前 TSan 曾被 VM 的 `unexpected memory mapping` 阻止，不能视为通过。

## 2026-09-10 发送端生命周期同步

起始分支 `dev`，HEAD `47e28ed19afccb4923fe6dc179a0214e8ddd7acf`；工作区干净，
未找到适用的 AGENTS.md。原有布局 v2 已在该 HEAD 中，本轮未改共享区布局。
没有 commit、push、reset 或合并。

新增测试先以 `/tmp/cyberrt-lifecycle-normal` 构建，首轮 INTRA、强制 RTPS、
真实 Discovery churn 三个程序全部通过，随后增加普通/Loan 重入和受控 Hybrid
RTPS 快照交错用例。第一次 RTPS 校验误把 Payload 全程序号与 DDS Writer 序号
等同，按本地 ReaListener 的实际行为修正测试：Payload 序号跨启停唯一且校验
全部数据，DDS 序号有效且允许 Writer 重建后重新计数；没有改接收协议。

验证期间保留的失败与修正：

- 第一次完整普通 check 遗漏 `CMW_PATH`，若干依赖配置的程序失败；设置正确
  环境后 fast 14、integration 11 个程序全通过。该轮尚未包含后述依赖修正。
- 多配置同时高并行编译导致 UBSan 的 cc1plus 被系统杀死；降低并行度后重建。
- UBSan 完整回归揭示 Publish 必经的 `PerfEventCache` 对齐错误：
  `reference binding to misaligned address ... requires 64 byte alignment`。
  `BoundedQueue` 含 alignas(64) 成员，C++14 默认 new 未满足该要求。
  在所有构建加入 `-faligned-new`，不关闭 alignment/vptr 检查；对象规则现在
  依赖 Makefile，选项变更后重新编译。
- 新 Discovery 测试初次 ASan 在进程静态析构阶段发现调度线程仍读取已释放的
  静态哈希表，随后退出超时；在测试退出前显式调用 Scheduler/SHM Dispatcher
  Shutdown 并等待线程，保留全局关闭实现。重新运行发现 2592 字节/27 次分配
  的泄漏：9 个离开的本地 Subscriber 各遗留一条 heap Loan。调用链是
  Subscriber → CreateRoutineFactory → 回调返回 → Yield，CRoutine 删除时
  直接回收挂起栈，不展开局部 shared_ptr 的析构。单消息工厂在回调后 Yield 前
  reset 自己的引用；用户保留的消息引用不变，也避免遗留 SHM Lease。
- 早期 ASan 与其他配置测试同时运行时，现有 SHM 并发用例的最终通知被判为
  stale generation，旧 RTPS 生命周期用例一次等待收包失败。SHM 用例补上
  初始消息确认，确保延迟打开的接收映射已经绑定再开始启停，并增加内容与
  重复检查。后续各配置测试顺序执行，不并发运行共享通知区测试。
- TSan 三个程序均在进入测试前退出 66：
  `FATAL: ThreadSanitizer: unexpected memory mapping`，其中首个地址为
  `0x79f9c5eae000-0x79f9c6300000`。没有修改 ASLR 或继续改造环境。
  **TSan 未通过、未能执行测试**；这是最终依赖修正前的启动尝试，不声称最终
  代码获得了 TSan 验证。

失败诊断日志保留在本机 `/tmp/cyberrt-lifecycle-normal-check.log`、
`/tmp/cyberrt-lifecycle-ubsan-build.log`、`/tmp/cyberrt-lifecycle-ubsan-check.log`、
`/tmp/cyberrt-lifecycle-asan-test.log`、
`/tmp/cyberrt-lifecycle-asan-discovery-final.log`、
`/tmp/cyberrt-lifecycle-tsan-test.log`。这些临时文件不是测试输入。

最终必要修正后的执行（仓库根目录，GCC 11.4.0，Linux 6.8.0-138-generic，
本地 Fast DDS 源码标签 v2.12.0）：

```sh
make -C example -j1 BUILD_DIR=/tmp/cyberrt-lifecycle-normal tests
make -C example -j2 BUILD_DIR=/tmp/cyberrt-lifecycle-ubsan SANITIZE=undefined tests
asan_tests='test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_loaned_message_discovery_churn test_shm_transmitter_lifecycle_regression test_loaned_message_hybrid test_shm_block_lease_generation test_rtps_lifecycle_regression test_loaned_message_rtps_multiprocess'
make -C example -j1 BUILD_DIR=/tmp/cyberrt-lifecycle-asan SANITIZE=address $asan_tests
CMW_PATH="$PWD" ASAN_OPTIONS=halt_on_error=1 bash example/run_tests.sh \
  --bin-dir /tmp/cyberrt-lifecycle-asan/bin --timeout 90 $asan_tests
```

ASan 这轮七个程序通过，只有旧 `test_rtps_lifecycle_regression` 收包等待失败，
没有 sanitizer 内存错误。该测试固定等待 500ms 不能证明 DDS 已匹配；改为
通过管道确认真实 readiness probe 已收到，然后仍然只发送一次待验证消息，
保留单次接收与重复检测，并新增错误 Payload 检查。针对改动单独复验：

```sh
make -C example -j2 BUILD_DIR=/tmp/cyberrt-lifecycle-asan SANITIZE=address test_rtps_lifecycle_regression
CMW_PATH="$PWD" ASAN_OPTIONS=halt_on_error=1 bash example/run_tests.sh \
  --bin-dir /tmp/cyberrt-lifecycle-asan/bin --timeout 90 test_rtps_lifecycle_regression
```

该程序通过。因此最终 ASan 八个程序均有通过结果（七个原组通过、一个修正后
单独通过），无剩余 ASan/LeakSanitizer 报错；没有禁用泄漏检查。日志分别为
`/tmp/cyberrt-lifecycle-asan-verified.log`（原组含一个已修正的失败）和
`/tmp/cyberrt-lifecycle-asan-rtps-final.log`。后者约 1.16 秒。

TSan 实际尝试命令（最终依赖修正前）：

```sh
make -C example -j2 BUILD_DIR=/tmp/cyberrt-lifecycle-tsan SANITIZE=thread \
  test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_shm_transmitter_lifecycle_regression
CMW_PATH="$PWD" TSAN_OPTIONS=halt_on_error=1 bash example/run_tests.sh \
  --bin-dir /tmp/cyberrt-lifecycle-tsan/bin --timeout 30 \
  test_intra_transmitter_lifecycle test_rtps_transmitter_lifecycle test_shm_transmitter_lifecycle_regression
```

最终完整回归按配置顺序执行，避免共享通知区测试互相干扰：

```sh
CMW_PATH="$PWD" make -C example -j2 BUILD_DIR=/tmp/cyberrt-lifecycle-normal check
CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  make -C example -j2 BUILD_DIR=/tmp/cyberrt-lifecycle-ubsan SANITIZE=undefined check
```

两组均退出 0：普通 fast 14 / integration 11 个程序全部通过；UBSan fast 14 /
integration 11 个程序全部通过，无 runtime error、失败或超时。覆盖新增 INTRA/
RTPS/Discovery 三个入口，以及 Hybrid INTRA/SHM、普通 RTPS 生命周期、Loan、
SHM generation/Lease、布局 v2 的相关现有回归。最终日志：
`/tmp/cyberrt-lifecycle-normal-verified.log`、
`/tmp/cyberrt-lifecycle-ubsan-verified.log`。

Fast DDS 库沿用本地安装，未使用 sanitizer 重编译。RTPS 数据路径包含同机强制
后端及既有跨进程往返；构造不同 host 元数据的 Hybrid 只计受控路由测试。
真实 Discovery churn 的远端 Subscriber 实例在同一子进程内反复创建/正常离开；
不是多台主机，也不是每轮重启 VM/网络。测试只覆盖本轮单发布线程与拓扑/后端
启停的同步约定；多线程 Publish、并发 Publisher/Participant/Transport Shutdown
不在承诺范围内，TSan 未能启动。

最终 `git diff --check` 通过；新增未跟踪测试文件也单独检查空白错误。改动保留
在 dev 工作区供审阅，未提交或推送。运行时核心变更见 TESTING.md 的同步约定，
其余主要为三个新测试入口、共享测试辅助头、现有 SHM/RTPS 编排补强与验证说明。
