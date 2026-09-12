# 测试日志

测试简介和使用指南见 [TESTING.md](TESTING.md)。本文件记录实际执行结果、
环境限制及布局测量；较早的失败记录保留，后续复跑结果单独记录。
功能性更新见 [README](../README.md)，文档职责见 [AGENTS.md](../AGENTS.md)。
历史条目未给出完整命令的部分保留原记录，不将使用指南中的示例补写为已执行命令。

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

布局机制和升级兼容性见 [README](../README.md#共享区布局-v2-与兼容性)。
下面保留当时实际测量的 ABI 数据。

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

当时记录的默认段大小为 9442304 字节；实现约定统一维护在 README。

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
在 dev 工作区供审阅，未提交或推送。运行时核心变更见 [README 的同步约定](../README.md#发送端生命周期与并发边界)，
其余主要为三个新测试入口、共享测试辅助头、现有 SHM/RTPS 编排补强与验证说明。


## 2026-09-11 文档职责整理

起始分支 `dev`，HEAD `b72973e7548d78e4672db7f5c2bf55b5d8f07f94`，工作区干净。
新建根目录 AGENTS.md；将功能和同步/兼容性说明归入 README.md，测试程序介绍
与使用指南集中到 TESTING.md，历史实际命令、失败与复验结果保留在本文件。
本次仅修改这四份 Markdown 文档，未构建或运行中间件测试；上述历史通过结果
不代表本次重新执行。未补造历史缺失命令。

文档检查实际工作目录为 `/home/jim/cpp/CyberRT`，完整命令如下。
只检查本地链接，不访问 README 引用的外部网页；`bash -n` 仅检查语法，不执行示例。

```sh
python3 - <<'DOC_CHECK'
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote

files = [Path(p) for p in (
    'AGENTS.md', 'README.md', 'example/TESTING.md', 'example/testlog.md')]
for path in files:
    content = re.sub(r'```.*?```', '', path.read_text(), flags=re.S)
    for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)', content):
        if '://' in link:
            continue
        name, _, anchor = unquote(link).partition('#')
        target = path.parent / name if name else path
        assert target.is_file(), (path, link)
        if anchor:
            headings = re.findall(r'^#+ (.+)$', target.read_text(), re.M)
            anchors = {re.sub(r'[^\w\s-]', '', h.lower()).replace(' ', '-')
                       for h in headings}
            assert anchor in anchors, (path, link)
makefile = Path('example/Makefile').read_text().replace('\\\n', ' ')
guide = Path('example/TESTING.md').read_text()
count = 0
for group in ('FAST_TEST_TARGETS', 'INTEGRATION_TEST_TARGETS', 'BENCHMARK_TARGETS'):
    targets = re.search(r'^' + group + r'\s*:=\s*(.+)$', makefile, re.M).group(1).split()
    for target in targets:
        assert '`' + target + '`' in guide, target
    count += len(targets)
blocks = re.findall(r'```sh\n(.*?)```', guide, re.S)
for block in blocks:
    subprocess.run(['bash', '-n'], input=block, text=True, check=True)
subprocess.run(['git', 'diff', '--check'], check=True)
result = subprocess.run(['git', 'diff', '--no-index', '--check', '/dev/null', 'AGENTS.md'],
                        capture_output=True, text=True)
assert result.returncode in (0, 1) and not result.stdout and not result.stderr, result
print(f'PASS: {len(files)} documents, local links/anchors, {count} targets, '
      f'{len(blocks)} shell examples, whitespace checks')
DOC_CHECK
```

检查退出码 0，输出：

```text
PASS: 4 documents, local links/anchors, 27 targets, 7 shell examples, whitespace checks
```

本地链接与锚点、27 个正式回归/性能目标的介绍、示例命令语法及空白检查全部通过。
`git diff --check` 无输出；新增 AGENTS.md 也单独通过空白检查。

## 2026-09-11 统一日志目录

分支 `dev`，HEAD `b72973e7548d78e4672db7f5c2bf55b5d8f07f94`。
开始时 README.md、TESTING.md、testlog.md 和新建的 AGENTS.md 有前一轮文档修改，
本次在其基础上继续更新，没有覆盖或提交这些修改。

根目录原有 log/ 存放 logger 源码，本次复用该目录保存日志，不移动源码。
项目内 52 个旧 `.log` 按原相对路径迁入 log/：根目录文件进入 log/，example/
文件进入 log/example/。同名目标存在时另加 migrated 序号，不覆盖；全部文件
迁移前后 SHA-256 相同。原始路径、目标路径和校验值记录在
`log/log-migration-1789094668313297180.log`。仓库外的历史 `/tmp` 日志不在此次
迁移范围内，上文实际命令与历史路径保留。

迁移的完整实际命令（工作目录 `/home/jim/cpp/CyberRT`）：

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib
import subprocess
import time
root = Path.cwd()
files = subprocess.check_output(['rg', '--files', '-uu', '-g', '*.log', '-g', '!.git/**'], text=True).splitlines()
records = []
for name in files:
    source = Path(name)
    if source.parts[0] == 'log':
        continue
    target = Path('log') / source
    target.parent.mkdir(parents=True, exist_ok=True)
    index = 1
    while target.exists():
        target = target.with_name(source.stem + '.migrated-' + str(index) + '.log')
        index += 1
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    source.rename(target)
    assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
    records.append(f'{source} -> {target} sha256={digest}')
manifest = Path('log') / ('log-migration-' + str(time.time_ns()) + '.log')
manifest.write_text('\n'.join(records) + '\n')
print(f'Moved {len(records)} logs without overwriting; verified SHA-256; manifest: {manifest}')
assert all(Path(name).parts[0] == 'log' for name in subprocess.check_output(
    ['rg', '--files', '-uu', '-g', '*.log', '-g', '!.git/**'], text=True).splitlines())
PY
```

日志功能说明见 [README](../README.md#统一日志目录)，使用方法和覆盖见
[TESTING](TESTING.md#日志路径回归与输出保存)。本次针对性构建、执行命令：

```sh
make -C example -j2 BUILD_DIR=/tmp/cmw-log-paths test_logger_paths > log/logger-paths-build.log 2>&1
CMW_PATH=/home/jim/cpp/CyberRT bash example/run_tests.sh --bin-dir /tmp/cmw-log-paths/bin --timeout 30 test_logger_paths > log/logger-paths-test.log 2>&1
```

构建和测试均退出 0，1 个程序/1 个用例通过，无失败或超时。验证不同 cwd 下
编译时项目根目录的选择、CMW_PATH 的选择、目录创建、路径文件名处理、缺省
`.log` 后缀、重复初始化追加、轮转，以及非法路径/符号链接/不可用根目录失败
且不向 cwd 写日志。测试的临时文件全部位于项目 log/ 内并在结束后回收。
本轮没有执行中间件完整回归或 sanitizer，不能将前面的历史结果视为本轮复验。

补充检查复用了上一节已完整记录的 DOC_CHECK 命令，退出 0：四份文档的本地
链接/锚点、28 个正式回归/性能目标、8 段 shell 示例语法及空白检查通过。
另检查了新增 test_logger_paths.cpp 的空白，并扫描项目内所有 `.log`：当前
55 个文件（52 个迁移日志、迁移清单、构建输出、测试输出）全部位于 log/ 内。
`git diff --check` 无输出。Git 忽略规则仅覆盖 log/ 下的 `.log` 和 `.log.*`，
不会忽略 logger 源码，也不会掩盖其他目录意外生成的日志。未 commit 或 push。


## 2026-09-11 Notifier 发布短锁、槽位互斥及丢弃策略

工作目录 `/home/jim/cpp/CyberRT`；分支 `dev`，开始及验证时 HEAD 为
`2ea84d5aab570209b24d3d7aa45ace666bff9992`，本轮改动未提交。
环境：Linux jim-VM `6.8.0-138-generic` x86_64、Ubuntu GCC 11.4.0、C++14，
4 CPU，内存约 3.8GiB。本地 Fast DDS 使用
`/home/jim/cpp/fastdds_2.12/install`，外部 DDS 库未以 sanitizer 重编译。
构建、运行器输出及诊断 `.log` 均在根目录 `log/notifier-20260911/`；
程序 Logger 日志继续按追加方式写入根目录 `log/<程序名>.log`。

本轮功能为 Notifier 独立 v1 布局、发布锁和槽位锁，不改变 Payload Segment v2。
测试代码及入口说明见 [TESTING](TESTING.md#notifier-槽位保护回归)，行为约定见
[README](../README.md#notifier-槽位保护与丢弃策略)。

### 首轮定向构建与执行

以下命令实际在上述工作目录执行。首次构建退出 0；修正独立布局 fixture 的
预期槽位大小/偏移后再次构建退出 0（此前尚未运行程序），首轮 9 项测试通过，
运行器退出 0，没有失败或超时。随后增加“实际头部与独立 fixture 相符”的正向
测试及错误 magic 场景，最终回归中的 Notifier 为 10 项。

```sh
mkdir -p log/notifier-20260911
make -C example -j2 BUILD_DIR=/tmp/cyberrt-notifier-normal test_condition_notifier > log/notifier-20260911/build-normal.log 2>&1
make -C example -j2 BUILD_DIR=/tmp/cyberrt-notifier-normal test_condition_notifier > log/notifier-20260911/rebuild-normal.log 2>&1
CMW_PATH="$PWD" bash example/run_tests.sh --bin-dir /tmp/cyberrt-notifier-normal/bin --timeout 30 test_condition_notifier > log/notifier-20260911/notifier-normal.log 2>&1
```

`ipcs -m` 显示宿主命名空间有旧通知区 key `0x31c7e6c9`、shmid `19`、
196616 字节、连接数 0。本轮未删除、修改该区。先确认非特权 IPC 隔离可用：

```sh
unshare --user --map-root-user --ipc sh -c 'ipcs -m' > log/notifier-20260911/ipc-isolation.log 2>&1
```

退出 0，隔离命名空间无共享段。后续中间件回归在独立 IPC 命名空间依次运行；
各测试自身正常清理独占资源，命名空间退出还会回收其剩余 SysV 资源。

### 普通、UBSan、ASan 相关回归

实际完整命令如下；三个 BUILD_DIR 相互隔离，初始均为本轮新建目录（normal
在上面的定向测试中先建）。每个程序外部超时 90 秒，未设置 GoogleTest 过滤。
UBSan 开关保留 `undefined,vptr`、`-fno-sanitize-recover=all` 和 PIE；ASan 为
non-PIE，启用 frame pointer，未禁用泄漏检查或添加 suppression。

```sh
set -u
regression_tests='test_condition_notifier test_shm_segment_exec test_shm_segment_robustness test_shm_block_lease_generation test_shm_dispatcher_robustness test_shm_transmitter_receiver test_shm_loaned_message test_shm_transmitter_lifecycle_regression test_posix_segment_multiprocess test_shm_loaned_message_multiprocess test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle test_loaned_message_dynamic_shm_lifecycle test_loaned_message_hybrid test_loaned_message_discovery_churn'
for config in normal undefined address; do
  BUILD_DIR="/tmp/cyberrt-notifier-$config"
  make -C example -j2 BUILD_DIR="$BUILD_DIR" SANITIZE="$config" $regression_tests > "log/notifier-20260911/build-$config-regression.log" 2>&1
  build_status=$?
  echo "$config build exit=$build_status"
  if test "$build_status" -eq 0; then
    CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ASAN_OPTIONS=halt_on_error=1 unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir "$BUILD_DIR/bin" --timeout 90 $regression_tests > "log/notifier-20260911/regression-$config.log" 2>&1
    echo "$config regression exit=$?"
  fi
done
```

已完成的普通和 UBSan 两组构建、运行均退出 0，各 15 个程序、43 项测试通过，
没有失败、超时或 UBSan runtime error。普通并发压力本次成功发布 22693 条、
丢弃 41307 条、接收 8927 条；UBSan 分别为 24034、39966、6791。接收条数因
竞争丢弃和慢读者绕环而少于尝试条数，测试核对完整字段、无重复、每条接收都
对应成功发布、成功数与发布位置相符，以及压力后恢复。具体计数不是性能指标。

### TSan 首次启动失败与复验

TSan 使用独立目录，仅构建不依赖 DDS 的通知测试。保留 non-PIE、frame pointer
及 `halt_on_error=1`，未屏蔽竞态检查。实际命令：

```sh
make -C example -j1 BUILD_DIR=/tmp/cyberrt-notifier-thread SANITIZE=thread test_condition_notifier > log/notifier-20260911/build-thread.log 2>&1
build_status=$?
echo "thread build exit=$build_status"
if test "$build_status" -eq 0; then
  CMW_PATH="$PWD" TSAN_OPTIONS=halt_on_error=1 timeout 30 /tmp/cyberrt-notifier-thread/bin/test_condition_notifier --gtest_filter='NotifierTest.*-NotifierTest.Exec*' > log/notifier-20260911/notifier-thread.log 2>&1
  echo "thread tests exit=$?"
fi
```

构建退出 0；首次运行退出 66，在 GoogleTest 启动前报
`FATAL: ThreadSanitizer: unexpected memory mapping 0x74bde48ae000-0x74bde4d00000`。
这是环境阻止启动，不能记为测试通过。随后仅对测试进程关闭 ASLR：

```sh
CMW_PATH="$PWD" TSAN_OPTIONS=halt_on_error=1 setarch x86_64 -R timeout 30 /tmp/cyberrt-notifier-thread/bin/test_condition_notifier --gtest_filter='NotifierTest.*-NotifierTest.Exec*' > log/notifier-20260911/notifier-thread-noaslr.log 2>&1
status=$?
echo "thread no-ASLR exit=$status"
cat log/notifier-20260911/notifier-thread-noaslr.log
```

复验退出 0，7 项全部通过，没有 TSan 报告。压力成功发布 18439、丢弃 45561、
接收 5368。TSan 线程压力使用同一映射；本次 TSan 过滤掉 exec 测试和布局拒绝
套件，不把它的结论扩大为跨进程内存序证明，也未执行整个中间件的 TSan 回归。
独立进程共享锁、广播、超时及布局拒绝由普通/UBSan/ASan 的完整通知测试覆盖。


### ASan 首轮失败、原因与修复

上面的 ASan 原组构建成功，运行失败：原始运行器汇总为 `passed=12 failed=3
 timed_out=0`，详情保留在 `log/notifier-20260911/regression-address.log`。
该汇总对两次超时分类不准确：日志明确记录两个程序超过 90s 后被 watchdog
终止，运行器却归入 signal 15（143），不能据此声称没有超时。

- `test_hybrid_shm_multiprocess` 和 `test_loaned_message_dynamic_shm_lifecycle`
  功能断言已通过，但退出阶段 ASan 报 heap-use-after-free，随后未退出并分别
  被 90s watchdog 终止。程序未停止调度线程，线程仍访问进程静态的
  `ClassicContext::notify_grp_`；PC 的离线符号定位和源码与此对应。
- `test_hybrid_dynamic_shm_lifecycle` 的子进程 ASan 报 heap-buffer-overflow：
  `Manager::Write` 在 `manager.cpp:271` 复制 256 字节，但 DDS change 仅申请了
  255 字节；父进程因子进程失败而退出 1。
- Notifier 10 项在该 ASan 原组中通过，压力成功 10375、丢弃 53625、接收 6762。
  其他 11 个通过程序以原始汇总为准，不将“功能断言通过但退出异常”算作通过。

实际符号定位命令（退出 0）：

```sh
addr2line -Cfipe /tmp/cyberrt-notifier-address/bin/test_hybrid_shm_multiprocess 0x5615c9
```

结果为 `std::_Hashtable<std::string, std::pair<const std::string, int>, ...>::
_M_find_before_node`，`/usr/include/c++/11/bits/hashtable.h:1833`。结合调度器源码
中 `NOTIFY_GRP` 的类型和 Wait 访问，修复两个测试主函数在 RUN_ALL_TESTS 返回后
显式停止并 join 调度器、停止已存在的 SHM dispatcher，不修改调度器全局退出 API。
Discovery 写入改为按真实序列化长度申请并检查 change/容量，失败返回 false，
History 未接管的 change 被释放。动态 SHM 测试的 message_type 附加 256 字符，
确保不依赖 PID/时间戳长度也能覆盖超过旧固定容量的情况。

三个失败程序的 ASan 针对性复验实际命令：

```sh
set -u
repair_tests='test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle test_loaned_message_dynamic_shm_lifecycle'
make -C example -j2 BUILD_DIR=/tmp/cyberrt-notifier-address SANITIZE=address $repair_tests > log/notifier-20260911/build-address-repair.log 2>&1
build_status=$?
echo "address repair build exit=$build_status"
if test "$build_status" -eq 0; then
  CMW_PATH="$PWD" ASAN_OPTIONS=halt_on_error=1 unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir /tmp/cyberrt-notifier-address/bin --timeout 90 $repair_tests > log/notifier-20260911/address-repair.log 2>&1
  echo "address repair tests exit=$?"
fi
```


上述针对性复验构建退出 0，3 个程序均通过，运行器退出 0，没有 ASan 报告或超时。
由于 Discovery 核心实现有修复，再次执行最终 15 程序回归，实际命令如下：

```sh
set -u
regression_tests='test_condition_notifier test_shm_segment_exec test_shm_segment_robustness test_shm_block_lease_generation test_shm_dispatcher_robustness test_shm_transmitter_receiver test_shm_loaned_message test_shm_transmitter_lifecycle_regression test_posix_segment_multiprocess test_shm_loaned_message_multiprocess test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle test_loaned_message_dynamic_shm_lifecycle test_loaned_message_hybrid test_loaned_message_discovery_churn'
for config in normal undefined address; do
  BUILD_DIR="/tmp/cyberrt-notifier-$config"
  make -C example -j2 BUILD_DIR="$BUILD_DIR" SANITIZE="$config" $regression_tests > "log/notifier-20260911/build-$config-final.log" 2>&1
  build_status=$?
  echo "$config final build exit=$build_status"
  if test "$build_status" -eq 0; then
    CMW_PATH="$PWD" UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ASAN_OPTIONS=halt_on_error=1 unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir "$BUILD_DIR/bin" --timeout 90 $regression_tests > "log/notifier-20260911/regression-$config-final.log" 2>&1
    echo "$config final regression exit=$?"
  fi
done
```


最终三组构建及运行均退出 0，各 **15 个程序、43 项测试通过**，没有失败、
超时、ASan/LeakSanitizer 报告或 UBSan runtime error。最终日志分别为
`log/notifier-20260911/regression-normal-final.log`、
`log/notifier-20260911/regression-undefined-final.log`、
`log/notifier-20260911/regression-address-final.log`；对应构建输出为
`build-normal-final.log`、`build-undefined-final.log`、`build-address-final.log`
（同一日志目录）。此前失败日志保持原样，未被成功结果覆盖。

本轮验证包含同机 INTRA、真实跨进程 SHM、独立 exec 通知区访问及受控 Hybrid
元数据选路；DDS 用于同机 Discovery。没有真实跨主机验证，没有重跑所有 RTPS
数据路径，也不承诺持锁进程崩溃恢复。TSan 仅为上述 7 项线程测试。

### 文档、构建入口与工作区检查

本轮已实际检查本地 Markdown 文件和标题锚点、TESTING 中的测试源文件及 Makefile
注册、Notifier 的 integration 入口，以及 `git diff --check`。新测试未跟踪时另检
尾部空白。检查脚本的完整实际内容如下（脚本不生成程序运行日志）：

```sh
cat > /tmp/cyberrt-notifier-doc-check.py <<'PY'
import pathlib
import re
import subprocess
root = pathlib.Path('/home/jim/cpp/CyberRT')
paths = ['README.md', 'example/TESTING.md', 'example/testlog.md']
for path in paths:
    source = root / path
    for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', source.read_text()):
        if '://' in target:
            continue
        name, sep, anchor = target.partition('#')
        dest = (source.parent / name).resolve() if name else source
        assert dest.exists(), (path, target)
        if sep:
            anchors = set()
            for title in re.findall(r'^#+\s+(.+)$', dest.read_text(), re.M):
                anchors.add(re.sub(r'[^\w\-\s]', '', title.lower()).replace(' ', '-'))
            assert anchor in anchors, (path, target, anchors)
print('PASS local Markdown files and heading anchors')
makefile = (root / 'example/Makefile').read_text()
for name in set(re.findall(r'\btest_[a-z0-9_]+\b', (root / 'example/TESTING.md').read_text())):
    assert (root / 'example' / (name + '.cpp')).exists(), name
    assert name in makefile, name
assert 'INTEGRATION_TEST_TARGETS := test_condition_notifier' in makefile
print('PASS documented test sources and Makefile registration')
subprocess.run(['git', 'diff', '--check'], cwd=root, check=True)
# git diff does not include an untracked new source file.
for number, line in enumerate((root / 'example/test_condition_notifier.cpp').read_text().splitlines(), 1):
    assert line == line.rstrip(), number
print('PASS git diff --check and new test whitespace')
PY
python3 /tmp/cyberrt-notifier-doc-check.py > log/notifier-20260911/doc-check-initial.log 2>&1
```

首次检查退出 0。修复回归问题并更新文档后再次执行（退出 0）：

```sh
python3 /tmp/cyberrt-notifier-doc-check.py > log/notifier-20260911/doc-check-repair.log 2>&1
status=$?
echo "repair doc check exit=$status"
cat log/notifier-20260911/doc-check-repair.log
ipcs -m > log/notifier-20260911/ipc-final.log
```

`ipc-final.log` 确认宿主命名空间仍只有原 key `0x31c7e6c9`、shmid `19`、
196616 字节、连接数 0 的旧通知区，没有遗留本轮定向测试资源。

最终内容的检查命令（在上述工作目录）：

```sh
python3 /tmp/cyberrt-notifier-doc-check.py > log/notifier-20260911/doc-check-final.log 2>&1
git diff --check
git status --short
git rev-parse HEAD
```

最终文档/链接/入口和空白检查退出 0，HEAD 未改变。变更保留在 dev 工作区，
没有提交、推送或清理宿主旧通知区。


## 2026-09-11 独立进程 SHM 性能实验

### 范围、环境与可追溯数据

本轮替换旧单进程 benchmark，只比较**同一 VMware 虚拟机内、独立 exec 的收发进程、
显式 POSIX SHM**。没有 INTRA、RTPS、真实跨主机测试；没有重跑全部中间件回归。
采用“包含逐条数据准备、普通路径序列化/反序列化、接收全内容校验”的端到端吞吐口径，
没有纯传输或延迟测量。旧 benchmark 数据不与本次合并。

- 工作目录 `/home/jim/cpp/CyberRT`，分支 `dev`，基线 HEAD
  `9925d8705a933dc289240b3411c420bb07a698b3`；代码为本轮未提交修改。
- Ubuntu 22.04 环境，g++ `11.4.0-1ubuntu1~22.04.3`，Fast DDS 路径
  `/home/jim/cpp/fastdds_2.12/install`，优化参数 `-O2 -DNDEBUG`，C++14、`-faligned-new`。
  `SANITIZE` 未设置，未运行 sanitizer；ASAN/UBSAN/TSAN_OPTIONS、LD_LIBRARY_PATH 均未设置。
- VMware 完全虚拟化，4 个 vCPU，型号字符串 Intel Core i9-11900H @ 2.50GHz，
  1 NUMA 节点，约 3.8 GiB RAM；`/dev/shm` 约 1.9 GiB、实验前可用约 1.9 GiB。
  初始 load average 为 0.15/0.24/0.17；未控制宿主调度、频率或其他 VM 负载。
- 正式发送进程固定 CPU 0，接收进程（含 dispatcher）固定 CPU 1。每组预热 1000 ms、
  正式 5000 ms、排空 1000 ms；每路径每档三次，第二次交换模式先后顺序。
- 全部使用 32 槽、每槽 8 MiB，实际映射大小 268,506,112 bytes；业务 Payload
  4096/65536/1048576/4194304 bytes，普通序列化后各增加 7 bytes。
  四档都实际通过，没有超限档；不声明其他大小支持。该固定夹具不改变库默认配置。
- 正式执行时间 2026-09-11 17:10:32–17:13:29 +08:00。

[正式原始 manifest](../log/shm-benchmark-20260911/formal/manifest.json) 保留 24 次运行的
完整子进程命令、PID、环境、二进制/源码 SHA256、路径证据、退出状态和原始计数；
[逐轮 CSV](../log/shm-benchmark-20260911/formal/results.csv) 和
[汇总 JSON](../log/shm-benchmark-20260911/formal/summary.json) 可复算。
日志目录为 `log/shm-benchmark-20260911/`，运行指南和严格口径以
[TESTING](TESTING.md#性能程序) 为主。

### 实际构建、失败与修复记录

以下命令均已执行，工作目录与环境定义如下；没有用后来的成功覆盖首次失败。

```sh
cd /home/jim/cpp/CyberRT
mkdir -p log/shm-benchmark-20260911
make -C example -j2 BUILD_DIR=/home/jim/cpp/CyberRT/example/build-benchmark OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver > log/shm-benchmark-20260911/build.log 2>&1
make -C example -j2 BUILD_DIR=/home/jim/cpp/CyberRT/example/build-benchmark OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver > log/shm-benchmark-20260911/build-retry.log 2>&1
make -C example -j2 BUILD_DIR=/home/jim/cpp/CyberRT/example/build-benchmark OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver > log/shm-benchmark-20260911/build-fixed.log 2>&1
python3 -m py_compile example/run_shm_benchmark.py 
```

前三次构建退出分别为 2、2、0：第一次发现 `ShmConf`/`State` 命名空间歧义，第二次
仍有 `State` 歧义，补全 `transport::` 限定后成功。Python 语法检查退出 0。
生成的本轮 `__pycache__` 最后清除；优化对象与默认 build 隔离。

```sh
python3 example/run_shm_benchmark.py --output-dir log/shm-benchmark-20260911/smoke --sizes 4096 4194304 --warmup-ms 200 --duration-ms 300 --drain-ms 200 --sender-cpu 0 --receiver-cpu 1 > log/shm-benchmark-20260911/smoke-run.log 2>&1
unshare --user --map-root-user --ipc ipcs -m
ipcs -m
unshare --user --map-root-user --ipc python3 example/run_shm_benchmark.py --output-dir log/shm-benchmark-20260911/smoke-isolated --sizes 4096 4194304 --warmup-ms 200 --duration-ms 300 --drain-ms 200 --sender-cpu 0 --receiver-cpu 1 > log/shm-benchmark-20260911/smoke-isolated-run.log 2>&1
```

首个 smoke 退出 1，两子进程均退出 1，未进入正式窗口：宿主 key `0x31c7e6c9`、
shmid `19`、196616 bytes 的旧通知区与当前布局不兼容，接收 PROBE 超时。
这是环境阻止测量，不是性能数据。失败详情保存在
[失败 manifest](../log/shm-benchmark-20260911/smoke/manifest.json)。
两个 `ipcs` 命令退出 0，确认新 IPC 命名空间为空而宿主旧段仍在。
随后隔离 smoke 退出 0，12 次（两档 × 两模式 × 三次）均通过，
[短测 manifest](../log/shm-benchmark-20260911/smoke-isolated/manifest.json) 保留实际结果。
短测使用修复 Payload 高位校验前的版本和 300 ms 窗口，不混入正式汇总。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 example/test_shm_benchmark_stats.py > log/shm-benchmark-20260911/stats-tests.log 2>&1
make -C example -j2 BUILD_DIR=/home/jim/cpp/CyberRT/example/build-benchmark OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver > log/shm-benchmark-20260911/build-selftest.log 2>&1
./example/build-benchmark/bin/shm_benchmark_receiver --self-test > log/shm-benchmark-20260911/selftest-initial.log 2>&1
make -C example -j2 BUILD_DIR=/home/jim/cpp/CyberRT/example/build-benchmark OPTFLAGS='-O2 -DNDEBUG' shm_benchmark_sender shm_benchmark_receiver > log/shm-benchmark-20260911/build-final.log 2>&1
./example/build-benchmark/bin/shm_benchmark_receiver --self-test > log/shm-benchmark-20260911/selftest-fixed.log 2>&1
PYTHONDONTWRITEBYTECODE=1 python3 example/test_shm_benchmark_stats.py > log/shm-benchmark-20260911/stats-tests-final.log 2>&1
```

Python 初次 4 项测试通过（退出 0）；含自检的构建退出 0。
C++ 首次自检退出 1：`payload corruption escaped full validation`，定位为生成公式
只利用序号低字节，修改序号高位仍可能被接受。修复为序号所有字节参与 Payload seed，
补充异常退出时的 sampler join 和 dispatcher shutdown 后，最终构建退出 0，
C++ 自检退出 0（内容每字节损坏、阶段隔离、窗口边界、重复/越界），Python 4 项复验退出 0。

### 正式命令和结果

```sh
cd /home/jim/cpp/CyberRT
PYTHONDONTWRITEBYTECODE=1 unshare --user --map-root-user --ipc python3 example/run_shm_benchmark.py --output-dir log/shm-benchmark-20260911/formal --sizes 4096 65536 1048576 4194304 --warmup-ms 1000 --duration-ms 5000 --drain-ms 1000 --repeats 3 --sender-cpu 0 --receiver-cpu 1 > log/shm-benchmark-20260911/formal-run.log 2>&1
```

runner 退出 0，24 对收发进程共 48 个进程全部退出 0。默认 `--bin-dir` 实际解析为
`/home/jim/cpp/CyberRT/example/build-benchmark/bin`；runner 为每个子进程明确设置
`CMW_PATH=/home/jim/cpp/CyberRT`，其他相关环境见 manifest。没有过滤条件，完整四档矩阵。

以下每格为三次的**中位数 [最小值, 最大值]**。MiB/s 只用正式窗口内完成全内容校验
且去重后的接收字节；排空不计吞吐。CPU% 为进程 CPU 时间/实际采样墙钟，100% 为
占满一个逻辑核；两进程分别计量。缺失率为排空后缺失的成功发送数/发送成功数。

| Payload | Path | MiB/s median [min, max] | Sender CPU % | Receiver CPU % | Missing success % |
| --- | --- | --- | --- | --- | --- |
| 4096 | copy | 765.74 [748.62, 801.41] | 99.87 [99.84, 99.96] | 52.86 [51.67, 54.81] | 46.84 [46.80, 49.92] |
| 4096 | loan | 2072.84 [1912.01, 2176.97] | 99.85 [99.65, 99.92] | 97.62 [95.98, 98.09] | 17.71 [13.08, 21.06] |
| 65536 | copy | 2197.96 [2149.07, 2201.41] | 99.94 [99.87, 99.95] | 87.86 [87.30, 88.90] | 2.06 [1.88, 3.47] |
| 65536 | loan | 2994.59 [2397.34, 3035.75] | 99.94 [99.68, 99.97] | 86.03 [84.81, 87.27] | 1.97 [1.59, 3.50] |
| 1048576 | copy | 1890.00 [1764.20, 1995.00] | 99.86 [99.29, 99.93] | 83.85 [82.25, 90.65] | 0.00 [0.00, 0.55] |
| 1048576 | loan | 2745.00 [2686.40, 2750.00] | 99.96 [99.91, 99.97] | 90.08 [89.72, 91.93] | 0.12 [0.00, 0.55] |
| 4194304 | copy | 1586.40 [1359.20, 1587.20] | 99.90 [99.88, 99.90] | 81.45 [78.79, 95.28] | 0.00 [0.00, 2.59] |
| 4194304 | loan | 2804.00 [2801.60, 2836.80] | 99.94 [99.93, 99.97] | 86.92 [86.74, 89.30] | 0.00 [0.00, 0.00] |

上述都是过载持续发送结果。4 KiB 普通 SHM 的成功发送缺失率中位数 46.84%，
Loan 为 17.71%；不能用发送成功数代替接收吞吐。64 KiB Loan 的有效吞吐范围
2397.34–3035.75 MiB/s，波动明显；没有删除低值轮次。4 MiB 普通路径第一轮
存在 46 条缺失，后两轮为零，也全部保留。三次范围不等于统计置信区间。

逐轮计数如下。所有轮次 `acquire_fail=0`、`transmit_fail=0`，所以尝试数等于发送成功数；
不代表接收端无丢弃。重复、内容错误、正式开始前消息、正式阶段迟到预热、
截止后回调和“失败发送却收到”均为 0。没有从缺失数字推测具体丢弃原因。

| Payload bytes | 路径/轮次 | 发送尝试=成功 | 窗口有效接收 | 排空有效接收 | 排空后成功发送缺失 |
| --- | --- | --- | --- | --- | --- |
| 4096 | copy/1 | 1929765 | 1025802 | 21 | 903942 |
| 4096 | loan/1 | 3100362 | 2447371 | 31 | 652960 |
| 4096 | loan/2 | 3206007 | 2786526 | 10 | 419471 |
| 4096 | copy/2 | 1956979 | 980146 | 6 | 976827 |
| 4096 | copy/3 | 1801106 | 958230 | 32 | 842844 |
| 4096 | loan/3 | 3224124 | 2653237 | 17 | 570870 |
| 65536 | copy/1 | 178105 | 171926 | 4 | 6175 |
| 65536 | loan/1 | 244395 | 239567 | 17 | 4811 |
| 65536 | loan/2 | 198750 | 191787 | 12 | 6951 |
| 65536 | copy/2 | 179837 | 176113 | 11 | 3713 |
| 65536 | copy/3 | 179240 | 175837 | 27 | 3376 |
| 65536 | loan/3 | 246807 | 242860 | 14 | 3933 |
| 1048576 | copy/1 | 9452 | 9450 | 2 | 0 |
| 1048576 | loan/1 | 13782 | 13750 | 15 | 17 |
| 1048576 | loan/2 | 13728 | 13725 | 3 | 0 |
| 1048576 | copy/2 | 9977 | 9975 | 2 | 0 |
| 1048576 | copy/3 | 8888 | 8821 | 18 | 49 |
| 1048576 | loan/3 | 13508 | 13432 | 2 | 74 |
| 4194304 | copy/1 | 1774 | 1699 | 29 | 46 |
| 4194304 | loan/1 | 3547 | 3546 | 1 | 0 |
| 4194304 | loan/2 | 3504 | 3502 | 2 | 0 |
| 4194304 | copy/2 | 1985 | 1983 | 2 | 0 |
| 4194304 | copy/3 | 1985 | 1984 | 1 | 0 |
| 4194304 | loan/3 | 3507 | 3505 | 2 | 0 |

原始 CSV 同时保存各轮有效字节、warmup 排除计数和纳秒边界；正式窗口全部精确为
5,000,000,000 ns。CPU 实际采样墙钟范围为 4.995815813–5.004705574 s，
最大边界迟到 5.074875 ms，均低于 20 ms 拒绝阈值。末条跨 end 完成单列报告，
其窗口后收到的字节只计排空，窗口外 CPU 不计正式 CPU。

每轮实际 POSIX 映射和消息类型均验证通过：copy 为 SERIALIZED 且正式序列化次数
等于尝试数；Loan 的每个正式借出/接收回调均验证为 SHM-backed。所有 PID 配对不同，
所有槽位/容量一致，内容、阶段与成功 bitmap 对账均通过。24 次退出后均未遗留本轮
POSIX 段（runner 未执行兜底 unlink），socket 已清理。隔离命名空间中的当前通知区
为 229432 bytes、连接数 0，随命名空间退出销毁；没有删除宿主旧通知区。


### 结果复算、文档和构建入口审计

下面是实际执行的审计脚本完整内容。它重新检查 24 轮的计数与 CSV，重新计算
中位数/范围，核对实际二进制及基准源码 SHA256，并检查本地链接、标题锚点、
测试源码/Makefile 入口及空白；没有重跑中间件测试。

```sh
cd /home/jim/cpp/CyberRT
cat > /tmp/cyberrt-benchmark-audit.py <<'PY'
import csv
import hashlib
import json
import pathlib
import re
import subprocess
import sys
root = pathlib.Path('/home/jim/cpp/CyberRT')
sys.path.insert(0, str(root / 'example'))
from run_shm_benchmark import validate, metrics, summarize
base = root / 'log/shm-benchmark-20260911/formal'
manifest = json.loads((base / 'manifest.json').read_text())
rows = list(csv.DictReader((base / 'results.csv').open()))
assert len(rows) == len(manifest['runs']) == 24
for run, row in zip(manifest['runs'], rows):
    s, r = run['sender'], run['receiver']
    validate(s, r)
    assert run['exit_codes'] == {'sender': 0, 'receiver': 0}
    assert not run['segment_left_after_exit']
    assert not pathlib.Path(run['segment_path']).exists()
    for key, value in metrics(s, r).items():
        assert float(row[key]) == value, (run['tag'], key)
    for prefix, endpoint in [('send_', s), ('recv_', r)]:
        for key, value in endpoint.items():
            if isinstance(value, int) and not isinstance(value, bool):
                assert int(row[prefix + key]) == value
for name, digest in manifest['sha256'].items():
    assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest() == digest, name
reviewed = [{'size_bytes': int(row['size_bytes']), 'mode': row['mode'],
             **{k: float(row[k]) for k in metrics(manifest['runs'][0]['sender'], manifest['runs'][0]['receiver'])}}
            for row in rows]
assert summarize(reviewed) == json.loads((base / 'summary.json').read_text())
print('PASS 24 trial accounting, CSV, median/range, SHA256 and POSIX cleanup')
for name in ['README.md', 'example/TESTING.md', 'example/testlog.md']:
    source = root / name
    for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', source.read_text()):
        if '://' in target:
            continue
        filename, sep, anchor = target.partition('#')
        dest = (source.parent / filename).resolve() if filename else source
        assert dest.exists(), (name, target)
        if sep:
            anchors = {re.sub(r'[^\w\-\s]', '', title.lower()).replace(' ', '-')
                       for title in re.findall(r'^#+\s+(.+)$', dest.read_text(), re.M)}
            assert anchor in anchors, (name, target)
makefile = (root / 'example/Makefile').read_text()
for target in ['shm_segment_benchmark', 'shm_benchmark_sender', 'shm_benchmark_receiver']:
    assert target in makefile and (root / 'example' / (target + '.cpp')).exists()
assert 'shm_zero_copy_benchmark' not in makefile
for target in set(re.findall(r'\btest_[a-z0-9_]+\b', (root / 'example/TESTING.md').read_text())):
    cpp = root / 'example' / (target + '.cpp')
    assert cpp.exists() or (root / 'example' / (target + '.py')).exists(), target
    if cpp.exists():
        assert target in makefile, target
for path in list((root / 'example').glob('shm_benchmark_*')) + [root / 'example/run_shm_benchmark.py', root / 'example/test_shm_benchmark_stats.py']:
    for line in path.read_text().splitlines():
        assert line == line.rstrip(), path
subprocess.run(['git', 'diff', '--check'], cwd=root, check=True)
print('PASS local Markdown links/anchors, test/build targets and whitespace')
PY
PYTHONDONTWRITEBYTECODE=1 python3 /tmp/cyberrt-benchmark-audit.py > log/shm-benchmark-20260911/audit-initial.log 2>&1
make -C example -n BUILD_DIR=/home/jim/cpp/CyberRT/example/build-benchmark OPTFLAGS='-O2 -DNDEBUG' benchmarks > log/shm-benchmark-20260911/targets-dry-run.log 2>&1
ipcs -m > log/shm-benchmark-20260911/ipc-final.log
```

结果复算/哈希/清理以及文档/入口/空白审计均 PASS；`make -n` 与 `ipcs` 退出 0。
`make -n benchmarks` 仅验证构建入口解析，未实际构建或运行 `shm_segment_benchmark`。
宿主仍只有原 key `0x31c7e6c9`、shmid `19`、196616 bytes、连接数 0 的旧通知区。
此前也已单独执行 `git diff --check`（退出 0），并用下面命令检查仓库内 `log/`
以外的 `.log`/轮转文件，输出为空（不是运行程序测试）：

```sh
rg --files --hidden -g '*.log' -g '*.log.*' -g '!log/**' -g '!.git/**' --no-ignore | head -20
```

补齐最终执行记录后，实际复查命令如下：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 /tmp/cyberrt-benchmark-audit.py > log/shm-benchmark-20260911/audit-final.log 2>&1
git diff --check
git status --short
```

最终复算、哈希、链接、入口和空白检查退出 0。结果保留在工作区，未提交、推送；
原失败、短测和正式结果分别归档，没有替换失败记录或抽掉低吞吐轮次。
