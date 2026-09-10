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
