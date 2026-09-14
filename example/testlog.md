# 测试日志

测试简介和使用指南见 [TESTING.md](TESTING.md)。本文件记录实际执行结果、
环境限制及布局测量；较早的失败记录保留，后续复跑结果单独记录。
功能性更新见 [README](../README.md)，文档职责见 [AGENTS.md](../AGENTS.md)。
历史条目未给出完整命令的部分保留原记录，不将使用指南中的示例补写为已执行命令。
纯文档及维护约定的改动和静态检查不纳入本页。

本次历史补录依据本地聊天的工具调用/返回值与 Git 提交差异核对；日期使用
实际执行时间（Asia/Shanghai），关联提交不等于当时干净 HEAD。下列会话
文件位于本机 `/home/jim/.codex/sessions/YYYY/MM/DD/`；未将整段聊天复制入库。
命令块保留当时参数和重定向路径，工作目录从工具调用参数另行标明；未完整
保存的退出码、日志或环境字段明确说明。历史 `/tmp` 日志不保证仍存在，
不把助手建议的命令、用户手动 Demo 输出或提交标题当作已执行测试的证据。

## 查找记录

按改动模块分组，组内按时间从早到晚排列；同日按正文先后，日期未记录的条目放在组末。
每次更新正文时同步更新本索引，具体约定见 [AGENTS.md](../AGENTS.md#更新规则)。
历史命令和失败记录保留原样；要复制命令重新操作，请看 [测试指南](TESTING.md) 或 [Demo 指南](demo/README.md)。

| 改动模块 | 记录时间 | 记录入口 |
| --- | --- | --- |
| Serialize 序列化 | 2026-08-23 | [边界检查、ASan/UBSan 与旧对象崩溃复验](#2026-08-23-序列化边界与发布订阅回归) |
| SHM 共享内存 | 2026-08-25 | [Segment、Dispatcher、收发恢复；泄漏检查关闭](#2026-08-25-shm-健壮性与-sanitizer-回归) |
| SHM 共享内存 | 2026-09-01 | [POSIX/XSI、跨进程回归与三次基准执行](#2026-09-01-posix-修复与双后端基准) |
| SHM 共享内存 | 2026-09-03 | [布局保护、RAII；Hybrid 失败和清理后复验](#2026-09-03-generation-与-lease-首轮回归) |
| SHM 共享内存 | 2026-09-03 | [SHM Loan/View、持有背压及普通 SHM 复验](#2026-09-03-loanedmessage-跨进程与背压回归) |
| SHM 共享内存 | 2026-09-05 | [普通复验、受限 UBSan 通过及 TSan 启动失败](#2026-09-05-元数据对齐与发送生命周期回归) |
| SHM 共享内存 | 2026-09-10 | [布局、Loan 首轮验证与环境失败](#2026-09-10-首轮验证) |
| SHM 共享内存 | 2026-09-10 20:01 | [普通构建及 UBSan 复跑](#2026-09-10-2001-后续复跑) |
| SHM 共享内存 | 2026-09-11 | [Notifier 并发、丢弃与 sanitizer 回归](#2026-09-11-notifier-发布短锁槽位互斥及丢弃策略) |
| SHM 共享内存 | 2026-09-11 | [独立进程性能实验：三轮数据与统计口径](#2026-09-11-独立进程-shm-性能实验) |
| SHM 共享内存 | 未记录 | [共享内存布局 v1/v2 实测数据](#本次共享内存布局核对) |
| Transport 发送端 | 2026-08-30 | [Writer 生命周期及 INTRA/SHM 离开重加入](#2026-08-30-rtps-资源释放与动态订阅回归) |
| Transport 发送端 | 2026-09-05 | [Hybrid 通过、RTPS 环境失败及旧 quick 基准](#2026-09-05-loanedmessage-混合传输首轮验证) |
| Transport 发送端 | 2026-09-10 | [INTRA、SHM、RTPS 生命周期同步与重入](#2026-09-10-发送端生命周期同步) |
| Discovery 自动发现 | 2026-08-27 | [INTRA、自动 XSI SHM、同机强制 RTPS](#2026-08-27-discovery-驱动的传输选择) |
| Discovery 自动发现 | 2026-09-13 | [全新订阅进程发现修复、失败复现及 Demo 复验](#2026-09-13-discovery-全新订阅进程自动发现修复) |
| QoS 策略 | 2026-09-14 | [TRANSIENT_LOCAL、历史与共享冲突；失败保留、30 程序回归、ASan 和 Demo 构建](#2026-09-14-qos-统一与历史策略回归) |
| QoS 策略 | 2026-09-14 | [业务 VOLATILE、预设精简、缓存复用与 Discovery 晚加入；30 程序回归及 9 项 ASan 通过](#2026-09-14-业务-volatile-与-discovery-策略精简) |
| Logger 日志 | 2026-09-11 | [统一日志目录、旧日志迁移与路径检查](#2026-09-11-统一日志目录) |
| 通信 Demo | 2026-09-12 | [A～E、两轮完整运行、4 MiB 与 Ctrl+C](#2026-09-12-面试通信-demo-本地验收) |
| 测试构建与执行器 | 2026-09-06 | [fast 13 过、integration 6 过 2 崩溃；runner 失败检测](#2026-09-06-测试集整理与首轮失败) |
| 测试构建与执行器 | 2026-09-06 | [旧对象、RTPS 填充、runner 修复与 ASan/UBSan](#2026-09-06-独立构建与-loan-崩溃修复复验) |
| 运行环境 | 未记录 | [早期 TSan 启动限制](#更早的环境记录日期未记录) |

## 2026-08-23 序列化边界与发布订阅回归

分支 `dev`；测试时完整 HEAD 未单独保存，不以事后提交号代替。普通构建使用
`example/build`，早期独立构建为 `/tmp/cyberrt-serialize-build-verified`。环境为
本地 Linux VM、C++14、Fast DDS 安装目录 `/home/jim/cpp/fastdds_2.12/install`。

关联提交：`ac11f99`（2026-08-23）。来源：本地会话 `01a02d78-75be-7282-b206-84a5aa9c0388`
（文件 `rollout-2026-08-23T15-14-24-01a02d78-75be-7282-b206-84a5aa9c0388.jsonl`）；下列行号指 JSONL 原文件。

15:39 的独立目录构建，以及 15:40 的序列化测试及 ASan+UBSan：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 377 行）：

```sh
make BUILD_DIR=/tmp/cyberrt-serialize-build-verified test_serialize test_publisher_subscriber -j2
```

工作目录 `/tmp`（会话第 405 行）：

```sh
/tmp/cyberrt-serialize-build-verified/bin/test_serialize
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 405 行）：

```sh
g++ -std=c++14 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iexample/build/include -Ithirdparty serialize/data_stream.cpp example/test_serialize.cpp -lgtest -lpthread -o /tmp/cyberrt_test_serialize_sanitized_verified
```

工作目录 `/tmp`（会话第 409 行）：

```sh
env ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/cyberrt_test_serialize_sanitized_verified
```

15:54 的发布订阅失败：下面同一命令先在沙箱内运行，再在沙箱外运行。

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 509 行）：

```sh
timeout 20s env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ./build/bin/test_publisher_subscriber
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 513 行）：

```sh
timeout 20s env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ./build/bin/test_publisher_subscriber
```

强制重建后的复验及 16:26 最后一次运行：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 553 行）：

```sh
make -B test_serialize test_publisher_subscriber -j2
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 580 行）：

```sh
timeout 20s env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ./build/bin/test_publisher_subscriber
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 645 行）：

```sh
make test_serialize test_publisher_subscriber -j2
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 653 行）：

```sh
./build/bin/test_serialize
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 656 行）：

```sh
timeout 20s env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ./build/bin/test_publisher_subscriber
```

独立目录序列化 4 项及 ASan+UBSan 4 项均退出 0；`detect_leaks=0`，不包括泄漏验证。
沙箱内发布订阅退出 134；沙箱外旧二进制退出 139，随后 `make -B` 重建后输出
`Publisher/Subscriber serialization round-trip: PASS`。最后一轮普通序列化输出
Vector round-trip、Truncated payload、Invalid string length、Invalid vector length
四项 PASS，退出 0；发布订阅输出 round-trip PASS。部分发布订阅调用先返回运行中
会话，所列即时输出未保存最终退出码，不能只凭 PASS 行推断完整进程清理成功。
这是当时单机发布订阅路径的字段往返验证，不是跨主机验证。未指定输出重定向的
结果保存在上述聊天工具输出中；旧运行日志迁移规则见[统一日志目录](#2026-09-11-统一日志目录)。

## 2026-08-25 SHM 健壮性与 sanitizer 回归

分支 `dev`，测试时完整 HEAD 未单独保存。普通产物在 `example/build`；
ASan+UBSan 使用 `/tmp/cmw_shm_sanitized`。运行特意在 `/tmp` 进行，
使用显式 `CMW_PATH` 和 `CMW_IP=127.0.0.1`；不能按今天的默认日志目录规则
改写这些历史工作目录。

关联提交：`a765d9c`（2026-08-25）。来源：本地会话 `01a03828-0399-7740-a617-fcaf43315315`
（文件 `rollout-2026-08-25T17-02-21-01a03828-0399-7740-a617-fcaf43315315.jsonl`）；下列行号指 JSONL 原文件。

普通构建与三项 SHM 程序：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 280 行）：

```sh
make -j4 test_shm_segment_robustness test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/tmp`（会话第 292 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_shm_segment_robustness
```

工作目录 `/tmp`（会话第 296 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_shm_dispatcher_robustness
```

工作目录 `/tmp`（会话第 300 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_shm_transmitter_receiver
```

独立 ASan+UBSan 构建、三项执行及 Segment 最后复验：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 340 行）：

```sh
make -s -j4 BUILD_DIR=/tmp/cmw_shm_sanitized CXXFLAGS='-std=c++14 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-L/home/jim/cpp/fastdds_2.12/install/lib -Wl,-rpath,/home/jim/cpp/fastdds_2.12/install/lib -fsanitize=address,undefined' test_shm_segment_robustness test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/tmp`（会话第 348 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/cmw_shm_sanitized/bin/test_shm_segment_robustness
```

工作目录 `/tmp`（会话第 352 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/cmw_shm_sanitized/bin/test_shm_dispatcher_robustness
```

工作目录 `/tmp`（会话第 356 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/cmw_shm_sanitized/bin/test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 377 行）：

```sh
make -s -j4 test_shm_segment_robustness
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 377 行）：

```sh
make -s -j4 BUILD_DIR=/tmp/cmw_shm_sanitized CXXFLAGS='-std=c++14 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-L/home/jim/cpp/fastdds_2.12/install/lib -Wl,-rpath,/home/jim/cpp/fastdds_2.12/install/lib -fsanitize=address,undefined' test_shm_segment_robustness
```

工作目录 `/tmp`（会话第 381 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_shm_segment_robustness
```

工作目录 `/tmp`（会话第 381 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 /tmp/cmw_shm_sanitized/bin/test_shm_segment_robustness
```

相关序列化/发布订阅回归：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 306 行）：

```sh
make -j4 test_serialize test_publisher_subscriber
```

工作目录 `/tmp`（会话第 310 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_serialize
```

工作目录 `/tmp`（会话第 315 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_publisher_subscriber
```

工作目录 `/tmp`（会话第 319 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 /home/jim/cpp/CyberRT/example/build/bin/test_publisher_subscriber
```

普通及 sanitizer 的 Segment 7 项、Dispatcher 1 项、Transmitter/Receiver 1 项
均通过，运行退出 0；最后 Segment 两种配置各 7 项复验退出 0。Segment 使用
内存替身测试边界、满块有界失败与 remap 后索引，不代表真实 OS SHM；后两个
程序覆盖真实本机 SHM 异常丢弃、重建和超限后恢复。ASan 设置 `detect_leaks=0`，
不能声称无泄漏。序列化四项 PASS、退出 0；Publisher/Subscriber 沙箱内退出 134，
沙箱外调用曾持续运行，本轮不将其计入三项 SHM 通过结果。
未单独重定向构建/测试输出，证据为会话工具结果；项目内旧运行日志后来按
[迁移清单](#2026-09-11-统一日志目录)归档。

## 2026-08-27 Discovery 驱动的传输选择

分支 `dev`；会话早期状态输出包含 `ac11f99ff36adf975d27b0135f206916f9d15362`，
测试时工作区另有 SHM 健壮性及本轮修改，不能当作该提交的干净构建。
普通 `BUILD_DIR` 为 `/home/jim/cpp/CyberRT/example/build`，未启用 sanitizer。

关联提交：`b3d95d3`（2026-08-30）。来源：本地会话 `01a041f5-ff9d-7383-99de-8410947d6d52`
（文件 `rollout-2026-08-27T14-43-55-01a041f5-ff9d-7383-99de-8410947d6d52.jsonl`）；下列行号指 JSONL 原文件。

早期 INTRA/SHM 建链失败保留；最终验收前重新构建四个入口：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 282 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 15s ./build/bin/test_hybrid_intra --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 298 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 25s ./build/bin/test_hybrid_shm_multiprocess --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 696 行）：

```sh
make -B -j2 test_transport_mode_selection test_hybrid_intra test_hybrid_shm_multiprocess test_rtps_same_host_multiprocess
```

15:19～15:20 顺序执行：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 704 行）：

```sh
./build/bin/test_transport_mode_selection --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 707 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 20s ./build/bin/test_hybrid_intra --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 711 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 25s ./build/bin/test_hybrid_shm_multiprocess --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 715 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 25s ./build/bin/test_rtps_same_host_multiprocess --gtest_color=no
```

最终模式选择 4 项、Hybrid INTRA 2 项、跨进程 Hybrid SHM 1 项、同机强制
RTPS 1 项均通过，工具结果退出 0。早期 INTRA 等待回调和 SHM 交付曾失败；
重编并修正 SHM sender identity 哈希接线后复验通过，不能把早期失败直接
归因于所有环境或宣称所有旧二进制有效。自动 SHM 当时使用 XSI；模式选择
里的不同 host 仅为元数据单元测试，RTPS 是同机显式强制后端，没有真实跨主机验证。
结果保存在会话工具输出；运行日志的历史路径和迁移位置参见
[统一日志目录](#2026-09-11-统一日志目录)。

## 2026-08-30 RTPS 资源释放与动态订阅回归

同一会话延续至 8 月 30 日，不能使用会话文件名中的 8 月 27 日作为本轮日期。
分支 `dev`，完整测试时 HEAD 未单独保存；普通目录 `example/build`，无 sanitizer。

关联提交：`9234c11`（2026-09-01）。来源：本地会话 `01a041f5-ff9d-7383-99de-8410947d6d52`
（文件 `rollout-2026-08-27T14-43-55-01a041f5-ff9d-7383-99de-8410947d6d52.jsonl`）；下列行号指 JSONL 原文件。

14:36 的完整相关目标构建与 14:38～14:39 的回归：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1185 行）：

```sh
make -B -j2 publisher subscriber test_publisher_subscriber test_transport_mode_selection test_hybrid_intra test_hybrid_shm_multiprocess test_rtps_same_host_multiprocess test_hybrid_dynamic_intra test_hybrid_dynamic_shm_lifecycle test_rtps_lifecycle_regression test_shm_segment_robustness test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1201 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 25s ./build/bin/test_publisher_subscriber
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1205 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 25s ./build/bin/test_hybrid_intra --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1209 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 35s ./build/bin/test_hybrid_shm_multiprocess --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1213 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 30s ./build/bin/test_rtps_same_host_multiprocess --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1217 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 25s ./build/bin/test_hybrid_dynamic_intra --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1220 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./build/bin/test_hybrid_dynamic_shm_lifecycle --gtest_color=no
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1223 行）：

```sh
env CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./build/bin/test_rtps_lifecycle_regression --gtest_color=no
```

发布订阅 round-trip、Hybrid INTRA、跨进程自动 SHM、同机强制 RTPS、动态
INTRA、动态 SHM 和 RTPS 生命周期均通过且退出 0。RTPS 生命周期 1 项，
覆盖三轮 Subscriber 重连与 Writer Enable/Disable 幂等；这不是 ASan 泄漏检查。
`test_hybrid_dynamic_intra` 是当时真实目标，9 月 6 日在 `00e3126` 中合并进
`test_hybrid_intra`，不能照搬为当前构建入口。未单独重定向输出，原始结果在会话
中；运行日志后续迁移参见[统一日志目录](#2026-09-11-统一日志目录)。

## 2026-09-01 POSIX 修复与双后端基准

分支 `dev`；开始时工作区干净且领先远端一个反向提交，关联前序
`4e1e8a8`。使用默认 `example/build`、C++14 和本地 Fast DDS 2.12 安装；
无 sanitizer。以下记录的是 POSIX 恢复并修复后的验证。

关联提交：`06f373b`（2026-09-01）。来源：本地会话 `01a05d12-8b5e-7a91-8af5-8fa38a321cf5`
（文件 `rollout-2026-09-01T21-04-51-01a05d12-8b5e-7a91-8af5-8fa38a321cf5.jsonl`）；下列行号指 JSONL 原文件。

21:07 初次 POSIX 构建与运行：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 63 行）：

```sh
git diff --check
git diff -- transport/shm/posix_segment.h transport/shm/posix_segment.cpp example/Makefile example/test_posix_segment_multiprocess.cpp
make test_posix_segment_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 69 行）：

```sh
./build/bin/test_posix_segment_multiprocess
ls -1 /dev/shm/cmw_* 2>/dev/null || true
ipcs -m
```

Hybrid 首轮沙箱失败及沙箱外复验；Dispatcher/收发回归：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 81 行）：

```sh
sed -n '1,360p' example/test_hybrid_dynamic_shm_lifecycle.cpp
sed -n '1,340p' example/test_rtps_same_host_multiprocess.cpp
sed -n '1,220p' init.cpp
sed -n '1,240p' config/conf_parse.cpp
make test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle test_shm_transmitter_receiver test_shm_dispatcher_robustness
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 109 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 116 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 122 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_dynamic_shm_lifecycle
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 130 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_dispatcher_robustness
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_transmitter_receiver
```

基准三次实际执行，以及补齐 Factory 用例后的最终回归：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 162 行）：

```sh
git diff --check
make shm_segment_benchmark
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 168 行）：

```sh
./build/bin/shm_segment_benchmark
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 190 行）：

```sh
make shm_segment_benchmark && ./build/bin/shm_segment_benchmark
git status --short --branch
git diff --stat
git diff --check
git diff -- transport/shm/posix_segment.h transport/shm/posix_segment.cpp transport/shm/segment_factory.cpp example/Makefile example/test_posix_segment_multiprocess.cpp example/shm_segment_benchmark.cpp
ls -1 /dev/shm/cmw_* 2>/dev/null || true
ipcs -m
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 218 行）：

```sh
make shm_segment_benchmark && ./build/bin/shm_segment_benchmark
ls -1 /dev/shm/cmw_* 2>/dev/null || true
ipcs -m
git diff --check
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 234 行）：

```sh
make test_posix_segment_multiprocess test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle && ./build/bin/test_posix_segment_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 240 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_shm_multiprocess
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_dynamic_shm_lifecycle
```

初次 POSIX 测试 1 项通过；增加 Factory 默认值检查后最终 2 项通过。Hybrid
沙箱内报 `getifaddrs: Operation not permitted` 和 `open: Operation not permitted`，
用例失败；沙箱外 Subscriber-first 和离开/重加入各 1 项通过，日志确认 `posix`。
Dispatcher 的显式 XSI 回归及普通 SHM 收发恢复各 1 项通过。三次基准均完成
POSIX/XSI 的 4 KiB、64 KiB、1 MiB、8 MiB 共 8 组，各组 `errors=0`。

最后一次基准输出如下；前两次原始数值仍在会话第 170、192 行，不用最终数据
覆盖它们。此为 Segment 层 VM 实验，不能与后来独立发布/订阅性能数据混用。

| 后端 | 字节 | 迭代 | median latency μs | MiB/s | errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| POSIX | 4096 | 20000 | 0.99 | 3930.68 | 0 |
| XSI | 4096 | 20000 | 1.02 | 3845.70 | 0 |
| POSIX | 65536 | 5000 | 4.48 | 13944.03 | 0 |
| XSI | 65536 | 5000 | 4.38 | 14273.22 | 0 |
| POSIX | 1048576 | 256 | 152.68 | 6549.63 | 0 |
| XSI | 1048576 | 256 | 145.80 | 6858.65 | 0 |
| POSIX | 8388608 | 32 | 3161.82 | 2530.19 | 0 |
| XSI | 8388608 | 32 | 3410.28 | 2345.85 | 0 |

结束时检查 `/dev/shm/cmw_*` 无残留、`ipcs -m` 无段。工具当时多只打印
`r.output`，未保留各次 shell 数字退出码；以上通过结论依据 GTest 和基准输出。
没有单独重定向基准日志，原始输出位于会话。

## 2026-09-03 generation 与 Lease 首轮回归

分支 `dev`，完整 HEAD 未单独保存；普通目录 `example/build`，未启用 sanitizer。
这一轮为 Block generation、RAII Lease、布局检查和 Notifier 发布顺序验证。

关联提交：`ab6a6c8`（2026-09-03）。来源：本地会话 `01a065f8-54f8-7830-b245-3a2c3b397dd1`
（文件 `rollout-2026-09-03T14-32-48-01a065f8-54f8-7830-b245-3a2c3b397dd1.jsonl`）；下列行号指 JSONL 原文件。

首次增量构建链接失败后强制重建；generation/Lease 通过：

工作目录 `/home/jim/cpp/CyberRT`（会话第 153 行）：

```sh
make -C example test_shm_segment_robustness test_posix_segment_multiprocess test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 191 行）：

```sh
make -B -C example test_shm_block_lease_generation test_shm_segment_robustness test_posix_segment_multiprocess test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 196 行）：

```sh
pgrep -af 'make -B -C example|g\+\+.*CyberRT' || true
make -C example test_shm_block_lease_generation test_shm_segment_robustness test_posix_segment_multiprocess test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 202 行）：

```sh
timeout 30s example/build/bin/test_shm_block_lease_generation
status=$?
printf 'test_shm_block_lease_generation exit=%s\n' "$status"
exit "$status"
```

初次回归及修正后的定向复验：

工作目录 `/home/jim/cpp/CyberRT`（会话第 210 行）：

```sh
timeout 30s example/build/bin/test_shm_segment_robustness
timeout 30s example/build/bin/test_posix_segment_multiprocess
timeout 30s example/build/bin/test_shm_dispatcher_robustness
timeout 45s example/build/bin/test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 231 行）：

```sh
make -C example test_shm_block_lease_generation test_posix_segment_multiprocess test_shm_dispatcher_robustness test_shm_transmitter_receiver
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 237 行）：

```sh
timeout 30s example/build/bin/test_shm_block_lease_generation && timeout 30s example/build/bin/test_posix_segment_multiprocess && timeout 30s example/build/bin/test_shm_dispatcher_robustness && timeout 45s example/build/bin/test_shm_transmitter_receiver
```

Publisher/Hybrid 配置与建链诊断：

工作目录 `/home/jim/cpp/CyberRT`（会话第 287 行）：

```sh
make -C example test_publisher_subscriber test_hybrid_shm_multiprocess && timeout 45s example/build/bin/test_publisher_subscriber && timeout 45s example/build/bin/test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 295 行）：

```sh
timeout 45s example/build/bin/test_publisher_subscriber && timeout 45s example/build/bin/test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 319 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 45s example/build/bin/test_publisher_subscriber && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s example/build/bin/test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 340 行）：

```sh
unshare --user --map-root-user --ipc --fork /bin/bash -c 'CMW_PATH=/home/jim/cpp/CyberRT timeout 45s example/build/bin/test_hybrid_shm_multiprocess > /tmp/codex_hybrid_shm_isolated.log 2>&1'
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 422 行）：

```sh
printf '%s\n' '--- remaining POSIX CyberRT SHM ---'
find /dev/shm -maxdepth 1 -type f -name 'cmw_*' -printf '%f %s bytes\n' | sort
printf '%s\n' '--- remaining System V SHM ---'
ipcs -m
CMW_PATH=/home/jim/cpp/CyberRT timeout 45s example/build/bin/test_hybrid_shm_multiprocess > /tmp/codex_hybrid_shm_after_cleanup.log 2>&1
task_hybrid_status=$?
tail -n 140 /tmp/codex_hybrid_shm_after_cleanup.log
printf 'test_hybrid_shm_multiprocess exit=%s\n' "$task_hybrid_status"
exit "$task_hybrid_status"
```

首次 Dispatcher 链接失败，强制重编后完成构建。generation/Lease 3 项退出 0；
Segment robustness 7 项通过。首轮 POSIX 双进程读检查失败，Dispatcher/收发
程序曾 SIGSEGV；修正 Block 数组构造及测试所需布局后，POSIX 2 项、
Dispatcher 1 项、收发恢复 1 项通过。
Publisher/Subscriber 未设置 `CMW_PATH` 时配置打开失败并崩溃，设置后有通过
结果；Hybrid 先遇到旧 Notifier 布局安全拒绝。隔离 IPC 及清理后仍出现
`delivered == false`，不能记为通过。历史诊断日志为
`/tmp/codex_hybrid_shm_isolated.log` 和 `/tmp/codex_hybrid_shm_after_cleanup.log`。
当时清理了确认无人使用的旧项目 IPC；完整清理命令见会话第 395～466 行，
不将针对当时 shmid 的清理脚本当作今天的操作指南。

随后 Loan 阶段同名 Hybrid 测试成功，见[下一轮](#2026-09-03-loanedmessage-跨进程与背压回归)。
两次聊天总结分别将 Hybrid 失败解释为 Discovery 或旧 IPC，证据不足以唯一
确定根因；这里保留实际失败和后续成功，不采纳互相矛盾的归因。

## 2026-09-03 LoanedMessage 跨进程与背压回归

本轮实际执行在 9 月 3 日下午，提交日期为 9 月 5 日。分支 `dev`，完整
测试时 HEAD 未单独保存；普通目录 `example/build`，无 sanitizer。

关联提交：`73d8366`（2026-09-05）。来源：本地会话 `01a065f8-54f8-7830-b245-3a2c3b397dd1`
（文件 `rollout-2026-09-03T14-32-48-01a065f8-54f8-7830-b245-3a2c3b397dd1.jsonl`）；下列行号指 JSONL 原文件。

16:11 相关目标强制重建；16:13～16:14 普通 SHM 回归：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 799 行）：

```sh
make -B test_shm_loaned_message test_shm_loaned_message_multiprocess test_shm_block_lease_generation test_shm_segment_robustness test_posix_segment_multiprocess test_shm_dispatcher_robustness test_shm_transmitter_receiver test_publisher_subscriber test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 824 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_block_lease_generation
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 828 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_segment_robustness
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 832 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_posix_segment_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 836 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_dispatcher_robustness
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 842 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_transmitter_receiver
```

Loan 两个入口及后续最终单进程回归：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 729 行）：

```sh
make test_shm_loaned_message test_shm_loaned_message_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 781 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_loaned_message
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 787 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_loaned_message_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1113 行）：

```sh
make test_shm_loaned_message && CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_shm_loaned_message
```

重新构建普通发布订阅目标并复验 Hybrid：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 934 行）：

```sh
make -B /home/jim/cpp/CyberRT/example/build/obj/example/test_hybrid_shm_multiprocess.o
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 934 行）：

```sh
make -B /home/jim/cpp/CyberRT/example/build/obj/example/test_publisher_subscriber.o
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 941 行）：

```sh
make test_hybrid_shm_multiprocess test_publisher_subscriber
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 954 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_shm_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 960 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_publisher_subscriber
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 1007 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT ./build/bin/test_hybrid_shm_multiprocess
```

最终单进程 Loan 5/5、跨进程 Loan 1/1；generation/Lease 3、Segment 7、
POSIX 2、Dispatcher 1、收发恢复 1 项有通过结果。跨进程用例验证持有
只读 View 时原块不可复用、耗尽后 Acquire 有界失败、释放最后引用后恢复。
16:19 和 16:25 的 Hybrid 各 1 项通过；发布订阅输出 serialization round-trip PASS。
这轮仍是当时 SHM 专用 Loan 能力，不能扩展为混合路由、RTPS 或跨主机验证。
上述五个普通 SHM 回归均保存退出 0；Loan 早期调用的工具输出存在截断，
未逐条恢复完整数字退出码，最终 5/5 与跨进程 1/1 以 GTest 输出核对。没有单独重定向测试输出，
日志来源为会话；前轮失败仍保留在上一节。

## 2026-09-05 LoanedMessage 混合传输首轮验证

分支 `dev`，完整 HEAD 未单独保存；普通 `example/build`，无 sanitizer。
实际执行日期取工具时间 9 月 5 日，不取提交标题中的 9.3。

关联提交：`4d837c2`（2026-09-05，提交标题写作 9.3）。来源：本地会话 `01a07005-fd9b-7380-9070-47b9ea791af2`
（文件 `rollout-2026-09-05T13-23-55-01a07005-fd9b-7380-9070-47b9ea791af2.jsonl`）；下列行号指 JSONL 原文件。

Hybrid 首轮失败、重建与最后复验：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 252 行）：

```sh
make test_loaned_message_hybrid test_loaned_message_rtps_multiprocess -j2
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 258 行）：

```sh
CMW_PATH="$(cd .. && pwd)" ./build/bin/test_loaned_message_hybrid
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 270 行）：

```sh
make test_loaned_message_hybrid -j2 && CMW_PATH="$(cd .. && pwd)" ./build/bin/test_loaned_message_hybrid
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 282 行）：

```sh
make test_loaned_message_hybrid -B -j2 && CMW_PATH="$(cd .. && pwd)" ./build/bin/test_loaned_message_hybrid
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 461 行）：

```sh
make test_loaned_message_hybrid -j2 && CMW_PATH="$(cd .. && pwd)" ./build/bin/test_loaned_message_hybrid > /tmp/cyberrt_hybrid_final.log 2>&1; result=$?; tail -20 /tmp/cyberrt_hybrid_final.log; exit $result
```

同机强制 RTPS 执行和早期 quick benchmark：

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 328 行）：

```sh
CMW_PATH="$(cd .. && pwd)" ./build/bin/test_loaned_message_rtps_multiprocess
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 310 行）：

```sh
make shm_zero_copy_benchmark -j2
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 322 行）：

```sh
make shm_zero_copy_benchmark -j2
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 337 行）：

```sh
CMW_PATH="$(cd .. && pwd)" ./build/bin/shm_zero_copy_benchmark --quick
```

工作目录 `/home/jim/cpp/CyberRT/example`（会话第 369 行）：

```sh
make shm_zero_copy_benchmark -j2 && CMW_PATH="$(cd .. && pwd)" ./build/bin/shm_zero_copy_benchmark --quick > /tmp/cyberrt_benchmark_quick.log 2>&1; result=$?; tail -80 /tmp/cyberrt_benchmark_quick.log; exit $result
```

Hybrid 早期两个版本的断言失败，强制重编及测试调整后最终 1 项通过；
`/tmp/cyberrt_hybrid_final.log` 保存最后输出。RTPS 的畸形长度检查 1 项通过，
跨进程往返 1 项因网络权限异常失败，整个程序不能记为通过；9 月 6 日的
后续修复另行记录。quick benchmark 曾因缺少匹配的 `Transmit` 重载编译失败，
修正后运行，输出保存至 `/tmp/cyberrt_benchmark_quick.log`。
`shm_zero_copy_benchmark` 为历史目标，后续在 `3c128c3` 中被独立进程实验
替代；quick 输出不能当成独立 Publisher/Subscriber 的正式性能结论，
这里不补写吞吐提升比例。

## 2026-09-05 元数据对齐与发送生命周期回归

分支 `dev`，完整测试时 HEAD 未单独保存；普通目录 `example/build`。
Sanitizer 通过显式 `CXXFLAGS`/`LDFLAGS` 配置，不能替换成今天的 `SANITIZE=` 示例。

关联提交：`11b0e1a`（2026-09-05）。来源：本地会话 `01a0707d-3ce3-71b1-aba4-d25ea5e7f1b9`
（文件 `rollout-2026-09-05T15-34-10-01a0707d-3ce3-71b1-aba4-d25ea5e7f1b9.jsonl`）；下列行号指 JSONL 原文件。

生命周期测试首轮失败与复验：

工作目录 `/home/jim/cpp/CyberRT`（会话第 84 行）：

```sh
make -C example test_shm_transmitter_lifecycle_regression -j4
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 90 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_transmitter_lifecycle_regression
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 95 行）：

```sh
make -C example test_shm_transmitter_lifecycle_regression -j4 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_transmitter_lifecycle_regression > /tmp/shm_transmitter_lifecycle_regression.log 2>&1; status=$?; tail -80 /tmp/shm_transmitter_lifecycle_regression.log; exit $status
```

动态 SHM 首次构建失败、重建及运行：

工作目录 `/home/jim/cpp/CyberRT`（会话第 105 行）：

```sh
make -C example test_loaned_message_dynamic_shm_lifecycle -j4
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 111 行）：

```sh
make -C example test_loaned_message_dynamic_shm_lifecycle -j4
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 117 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 35s ./example/build/bin/test_loaned_message_dynamic_shm_lifecycle > /tmp/loaned_dynamic_shm.log 2>&1; status=$?; tail -100 /tmp/loaned_dynamic_shm.log; exit $status
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 124 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 35s ./example/build/bin/test_loaned_message_dynamic_shm_lifecycle > /tmp/loaned_dynamic_shm.log 2>&1; status=$?; tail -100 /tmp/loaned_dynamic_shm.log; exit $status
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 146 行）：

```sh
make -C example test_loaned_message_dynamic_shm_lifecycle -j4 && CMW_PATH=/home/jim/cpp/CyberRT timeout 35s ./example/build/bin/test_loaned_message_dynamic_shm_lifecycle > /tmp/loaned_dynamic_shm.log 2>&1; status=$?; tail -100 /tmp/loaned_dynamic_shm.log; exit $status
```

相关构建失败和最终三个普通回归：

工作目录 `/home/jim/cpp/CyberRT`（会话第 154 行）：

```sh
make -C example -B test_shm_loaned_message test_loaned_message_hybrid test_shm_transmitter_receiver test_hybrid_dynamic_shm_lifecycle -j4 > /tmp/cyberrt-related-build.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_loaned_message > /tmp/test_shm_loaned.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_loaned_message_hybrid > /tmp/test_loaned_hybrid.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_transmitter_receiver > /tmp/test_shm_tx_rx.log 2>&1; status=$?; tail -20 /tmp/cyberrt-related-build.log; tail -20 /tmp/test_shm_loaned.log; tail -20 /tmp/test_loaned_hybrid.log; tail -20 /tmp/test_shm_tx_rx.log; exit $status
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 164 行）：

```sh
make -C example -B test_shm_loaned_message test_loaned_message_hybrid test_shm_transmitter_receiver -j4 > /tmp/cyberrt-related-build.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_loaned_message > /tmp/test_shm_loaned.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_loaned_message_hybrid > /tmp/test_loaned_hybrid.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_transmitter_receiver > /tmp/test_shm_tx_rx.log 2>&1; status=$?; tail -8 /tmp/cyberrt-related-build.log; tail -10 /tmp/test_shm_loaned.log; tail -10 /tmp/test_loaned_hybrid.log; tail -10 /tmp/test_shm_tx_rx.log; exit $status
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 172 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_loaned_message > /tmp/test_shm_loaned.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_loaned_message_hybrid > /tmp/test_loaned_hybrid.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_transmitter_receiver > /tmp/test_shm_tx_rx.log 2>&1; status=$?; tail -12 /tmp/test_shm_loaned.log; tail -12 /tmp/test_loaned_hybrid.log; tail -12 /tmp/test_shm_tx_rx.log; exit $status
```

UBSan 首轮及改用显式 MessageInfo 重载后的复验；TSan 尝试：

工作目录 `/home/jim/cpp/CyberRT`（会话第 177 行）：

```sh
make -C example BUILD_DIR=/tmp/cyberrt-ubsan CXXFLAGS='-std=c++14 -g -fsanitize=undefined -fno-sanitize-recover=all' LDFLAGS='-L/home/jim/cpp/fastdds_2.12/install/lib -Wl,-rpath,/home/jim/cpp/fastdds_2.12/install/lib -fsanitize=undefined' test_shm_transmitter_lifecycle_regression -j4 > /tmp/cyberrt-ubsan-build.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' timeout 45s /tmp/cyberrt-ubsan/bin/test_shm_transmitter_lifecycle_regression > /tmp/cyberrt-ubsan-run.log 2>&1; status=$?; tail -18 /tmp/cyberrt-ubsan-build.log; tail -40 /tmp/cyberrt-ubsan-run.log; exit $status
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 182 行）：

```sh
make -C example BUILD_DIR=/tmp/cyberrt-ubsan CXXFLAGS='-std=c++14 -g -fsanitize=undefined -fno-sanitize-recover=all' LDFLAGS='-L/home/jim/cpp/fastdds_2.12/install/lib -Wl,-rpath,/home/jim/cpp/fastdds_2.12/install/lib -fsanitize=undefined' test_shm_transmitter_lifecycle_regression -j4 > /tmp/cyberrt-ubsan-build.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' timeout 45s /tmp/cyberrt-ubsan/bin/test_shm_transmitter_lifecycle_regression > /tmp/cyberrt-ubsan-run.log 2>&1; status=$?; tail -40 /tmp/cyberrt-ubsan-run.log; exit $status
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 188 行）：

```sh
make -C example BUILD_DIR=/tmp/cyberrt-tsan CXXFLAGS='-std=c++14 -g -fsanitize=thread -fno-omit-frame-pointer' LDFLAGS='-L/home/jim/cpp/fastdds_2.12/install/lib -Wl,-rpath,/home/jim/cpp/fastdds_2.12/install/lib -fsanitize=thread' test_shm_transmitter_lifecycle_regression -j4 > /tmp/cyberrt-tsan-build.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT TSAN_OPTIONS='halt_on_error=1:second_deadlock_stack=1' timeout 45s /tmp/cyberrt-tsan/bin/test_shm_transmitter_lifecycle_regression --gtest_filter='ShmTransmitterLifecycleRegression.SendAndEnableDisableDoNotRace:ShmTransmitterLifecycleRegression.HeapLoanSendsAcrossShmLifecycleRace' > /tmp/cyberrt-tsan-run.log 2>&1; status=$?; tail -25 /tmp/cyberrt-tsan-build.log; tail -60 /tmp/cyberrt-tsan-run.log; exit $status
```

最后普通构建与生命周期复验：

工作目录 `/home/jim/cpp/CyberRT`（会话第 195 行）：

```sh
make -C example -B test_shm_transmitter_lifecycle_regression test_loaned_message_dynamic_shm_lifecycle -j4 > /tmp/cyberrt-final-build.log 2>&1 && CMW_PATH=/home/jim/cpp/CyberRT timeout 45s ./example/build/bin/test_shm_transmitter_lifecycle_regression > /tmp/cyberrt-final-lifecycle.log 2>&1; status=$?; tail -12 /tmp/cyberrt-final-build.log; tail -18 /tmp/cyberrt-final-lifecycle.log; git diff --check; git status --short; exit $status
```

生命周期首轮 2 过/2 失败，调整测试后 4/4 通过；最终普通复验仍为 4/4。
动态 SHM 先因 LoanedMessage 默认构造缺失编译失败，补齐后沙箱运行报网络权限
错误；沙箱外重跑还经历等待问题，修正编排后的 1 项通过。
相关全构建中普通 Hybrid 消息曾触发 Loan 模板实例化错误；缩小到三个指定
目标后，沙箱内 `test_shm_loaned_message` 仍 SIGSEGV，沙箱外最终
Loan 5 项、Hybrid 1 项、普通 SHM 收发 1 项通过。不能把缩小后的通过写成
原四目标构建全部通过。

UBSan 首轮报 `PerfEventCache` 的 64 字节对齐错误。第二轮测试改用显式
`MessageInfo` 的发送重载，绕开该计数路径后 4 项通过；当时没有修复
PerfEventCache，故只算该重载下的定向验证。全发送路径的 `-faligned-new`
修复和完整 UBSan 回归见[9 月 10 日记录](#2026-09-10-发送端生命周期同步)。
TSan 在进入用例前报 `unexpected memory mapping`，未通过。

历史日志路径见各命令的 `/tmp/*.log`，包括
`/tmp/shm_transmitter_lifecycle_regression.log`、`/tmp/loaned_dynamic_shm.log`、
`/tmp/cyberrt-ubsan-run.log` 和 `/tmp/cyberrt-tsan-run.log`。当时同名重定向
可能已覆盖前次文件；失败的不可替代证据为聊天工具输出，不能声称磁盘还保存
每次日志。本轮数字退出码未全部随 `r.output` 保存，按输出区分通过、失败及未启动。

## 2026-09-06 测试集整理与首轮失败

分支 `dev`；工作区在 `11b0e1a` 之后继续修改，未单独保存本轮完整 HEAD。
普通构建目录为 `example/build`；本轮先整理测试入口及清理路径，后续才处理
运行时和旧构建问题，不能将同一提交的最终状态套在本轮失败上。

关联提交：`00e3126`（2026-09-06，同时包含后续修复）。来源：本地会话 `01a0756e-d46d-7c61-a0e4-064837f2bd1f`
（文件 `rollout-2026-09-06T14-36-32-01a0756e-d46d-7c61-a0e4-064837f2bd1f.jsonl`）；下列行号指 JSONL 原文件。

四个整理后的入口先在沙箱内执行，再在沙箱外执行：

工作目录 `/home/jim/cpp/CyberRT`（会话第 207 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build/bin --timeout 30 test_blocker test_node test_publisher_subscriber test_hybrid_intra
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 214 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build/bin --timeout 30 test_blocker test_node test_publisher_subscriber test_hybrid_intra
```

runner 非零与超时验证：

工作目录 `/home/jim/cpp/CyberRT`（会话第 354 行）：

```sh
chmod +x example/.run_tests_nonzero_fixture.sh example/.run_tests_timeout_fixture.sh
set +e
example/run_tests.sh --bin-dir "$(pwd)/example" --timeout 1 .run_tests_nonzero_fixture.sh .run_tests_timeout_fixture.sh
runner_status=$?
set -e
printf 'fixture-runner-status=%s\n' "$runner_status"
test "$runner_status" -ne 0
```

工作目录 `/home/jim/cpp/CyberRT`（会话第 359 行）：

```sh
chmod +x example/run_tests.sh
set +e
example/run_tests.sh --bin-dir "$(pwd)/example" --timeout 1 .run_tests_nonzero_fixture.sh .run_tests_timeout_fixture.sh
runner_status=$?
set -e
printf 'fixture-runner-status=%s\n' "$runner_status"
test "$runner_status" -ne 0
```

清理路径修正后，15:17 的完整实际 check：

工作目录 `/home/jim/cpp/CyberRT`（会话第 557 行）：

```sh
set +e
CMW_PATH=/home/jim/cpp/CyberRT make -C example -j4 check > /tmp/cyberrt-final-check-after-cleanup.log 2>&1
check_status=$?
set -e
printf '%s\n' '--- check summary ---'
rg -n "^\[PASS\]|^\[FAIL\]|^\[TIME\]|^Test runner summary:|^  (passed|failed|timed out):|^check:" /tmp/cyberrt-final-check-after-cleanup.log || true
printf 'make-check-status=%s\n' "$check_status"
test "$check_status" -ne 0
```

四入口沙箱运行汇总 `passed=1 failed=3 timed_out=0`。临时 fixture 内容为
`.run_tests_nonzero_fixture.sh` 执行 `exit 7`、`.run_tests_timeout_fixture.sh`
执行 `sleep 5`；第一次 runner 无执行权限，补 `chmod +x` 后得到
`passed=0 failed=1 timed_out=1`、runner 返回 1，随后移除两个 fixture。
这证明失败检测生效，不能计为中间件程序通过。

完整 check 在先前若干 Node/Publisher 清理超时修正后，fast 13/13 通过、
integration 6/8 通过；SHM Loan 跨进程和 RTPS Loan 跨进程均 SIGSEGV，
`make-check-status=2`。上面包装命令最后检查非零是否符合预期，因此包装
shell 自身可以返回 0，不能据此宣称 `make check` 通过。
早期完整 check 还有 fast 12 过/1 超时、integration 5 过/3 失败，
保存在 `/tmp/cyberrt-final-check.log`（会话第 417、447 行）；15:17 结果
为 `/tmp/cyberrt-final-check-after-cleanup.log`（第 557、581 行）。
修复后的新目录复验见[下一轮](#2026-09-06-独立构建与-loan-崩溃修复复验)。

## 2026-09-06 独立构建与 Loan 崩溃修复复验

同一 `dev` 工作区后续验证；独立普通目录分别为
`/tmp/cyberrt-loaned-fixed-20260906`、`/tmp/cyberrt-final-20260906`，
ASan+UBSan 为 `/tmp/cyberrt-loaned-asan-20260906`。使用本地 GCC/C++14
及 `/home/jim/cpp/fastdds_2.12/install`，未重编 Fast DDS 依赖。

关联提交：`00e3126`（2026-09-06）。来源：本地会话 `01a0756e-d46d-7c61-a0e4-064837f2bd1f`
（文件 `rollout-2026-09-06T14-36-32-01a0756e-d46d-7c61-a0e4-064837f2bd1f.jsonl`）；下列行号指 JSONL 原文件。

16:13 前后的独立目录完整 check 使用脱离工具终端的执行方式保存退出状态：

工作目录 `/home/jim/cpp/CyberRT`（会话第 1131 行）：

```sh
test ! -e /tmp/cyberrt-full-check-complete-20260906.status && setsid bash -c 'CMW_PATH=/home/jim/cpp/CyberRT make -s -C /home/jim/cpp/CyberRT/example BUILD_DIR=/tmp/cyberrt-loaned-fixed-20260906 -j4 check > /tmp/cyberrt-full-check-complete-20260906.log 2>&1; printf "%s\n" "$?" > /tmp/cyberrt-full-check-complete-20260906.status' >/dev/null 2>&1 &
```

随后最终源码另开新目录构建并检查：

工作目录 `/home/jim/cpp/CyberRT`（会话第 1285 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT make -C example BUILD_DIR=/tmp/cyberrt-final-20260906 -j4 check > /tmp/cyberrt-final-check-20260906.log 2>&1; status=$?; printf '%s\n' "$status" > /tmp/cyberrt-final-check-20260906.status; exit "$status"
```

22:27 修复 runner 对 zombie 的误判后重跑完整 check：

工作目录 `/home/jim/cpp/CyberRT`（会话第 1347 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT make -s -C example BUILD_DIR=/tmp/cyberrt-final-20260906 -j4 check
```

22:28 两个原崩溃入口的 ASan+UBSan 复验：

工作目录 `/home/jim/cpp/CyberRT`（会话第 1359 行）：

```sh
CMW_PATH=/home/jim/cpp/CyberRT make -s -C example BUILD_DIR=/tmp/cyberrt-loaned-asan-20260906 -j4 CXXFLAGS='-std=c++14 -g -O1 -MMD -MP -fno-omit-frame-pointer -fsanitize=address,undefined' LDFLAGS='-L/home/jim/cpp/fastdds_2.12/install/lib -Wl,-rpath,/home/jim/cpp/fastdds_2.12/install/lib -fsanitize=address,undefined' test_shm_loaned_message_multiprocess test_loaned_message_rtps_multiprocess && CMW_PATH=/home/jim/cpp/CyberRT ASAN_OPTIONS='halt_on_error=1:detect_leaks=1' UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' bash example/run_tests.sh --bin-dir /tmp/cyberrt-loaned-asan-20260906/bin --timeout 90 test_shm_loaned_message_multiprocess test_loaned_message_rtps_multiprocess
```

早期 SIGSEGV 随独立重编消失，Makefile 增加头文件依赖跟踪；同时诊断出
ReadableInfo 跨进程 vptr 和 RTPS Loan 实收尾部零填充问题，分别修正。
普通消息实例化 Loan 专用模板的问题用 C++14 tag dispatch 处理，删除测试
兼容头；动态 SHM 用例增加 Discovery 异步匹配等待。

第一组独立完整 check 的 `/tmp/cyberrt-full-check-complete-20260906.log`
在会话第 1166 行显示 fast 13/13、integration 8/8，无失败或超时；
对应 `.status` 保存退出状态。之后进一步调整测试源码再跑出现过失败，
不能用这次成功覆盖后面的尝试。新目录曾受 runner 把 zombie 当存活进程的
错误影响，最终改为 watchdog + wait。22:27 会话收尾报告完整 check
13 fast + 8 integration 通过，但第 1354 行工具输出截断，未保留完整尾部
及数字退出码；此项明确为历史收尾报告，不能当作本次重新验证的结果。

22:28 sanitizer 输出直接保存 `passed=2 failed=0 timed_out=0`：
SHM Loan 跨进程 1 项、RTPS Loan 2 项通过，开启 `detect_leaks=1`，没有
ASan/UBSan 报告。只验证这两个程序，不代表全套 sanitizer 通过，RTPS
仍为同机强制后端。最后两条命令没有文件重定向，证据在聊天工具输出；
其它历史日志和状态文件路径保持上述实际值。

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

## 2026-09-12 面试通信 Demo 本地验收

工作目录 `/home/jim/cpp/CyberRT`；分支 `dev`，起始 HEAD
`3c128c37fbc1e5359f6ddb5f185ea5dcd5dfe50e`，开始时 `git status --short` 为空。
Ubuntu 22.04 虚拟机，内核 `6.8.0-138-generic`，x86_64，g++ 11.4.0；Fast DDS
`/home/jim/cpp/fastdds_2.12/install`。新增独立构建目录
`/home/jim/cpp/CyberRT/example/demo/build`，最终编译选项 C++14、`-faligned-new -O2 -g0`，
未启用 sanitizer。本次不运行完整回归或 benchmark。

按本次用户明确要求，Demo 构建输出、原始角色输出和归档放在 `example/demo/runs/`，
该目录已忽略。运行库 Logger 仍按原规则先写根目录 `log/`，脚本在每个进程退出后将
其确切文件原样移到本轮 `*.runtime.log`；原路径和迁移位置记录在每轮 `commands.txt`。
这项 Demo 专用归档不改变其他程序的日志规则。使用指南见 [Demo README](demo/README.md)。

### 构建与环境失败，均保留

以下命令实际从上述工作目录执行：

```bash
mkdir -p example/demo/runs/dev-build
make -C example -f demo/Makefile -j2 demo-transport > example/demo/runs/dev-build/build.log 2>&1
g++ -std=c++14 -faligned-new -fsyntax-only -DCMW_PROJECT_ROOT='"/home/jim/cpp/CyberRT"' -Iexample/demo/build/include -Ithirdparty -I/home/jim/cpp/fastdds_2.12/install/include -include example/demo/trace.h example/demo/demo_transport.cpp > example/demo/runs/dev-build/syntax.log 2>&1
```

首次构建退出 2，根分区只剩约 2.7 MiB，编译器写对象/临时汇编时报 ENOSPC；不是程序
测试失败。单独语法检查退出 0。用户随后明确授权清理虚拟机中可以清理的文件；只删除
下列旧临时构建目录的 `obj/` 内普通 `.o` 和 `.d` 文件，未删源码、程序或日志。
实际执行的清理命令：

```bash
python3 - <<'PY'
from pathlib import Path
roots = [Path('/tmp') / name for name in ['cyberrt-lifecycle-normal', 'cyberrt-lifecycle-ubsan', 'cyberrt-notifier-normal', 'cyberrt-notifier-address', 'cyberrt-notifier-undefined']]
count = total = 0
with open('example/demo/runs/dev-build/space-cleanup.txt', 'w') as log:
    for root in roots:
        for p in (root / 'obj').rglob('*'):
            if p.is_file() and not p.is_symlink() and p.suffix in ('.o', '.d'):
                size = p.stat().st_size
                log.write(f'{p}\t{size}\n')
                p.unlink()
                count += 1
                total += size
    log.write(f'files={count} bytes={total}\n')
print(f'Removed {count} old temporary object/dependency files; {total} bytes')
PY
df -h .
make -C example -f demo/Makefile -j2 demo-transport > example/demo/runs/dev-build/build-retry.log 2>&1
```

清理退出 0，共 855 个文件、993624506 bytes，清单在 `space-cleanup.txt`。
第二次构建退出 2：force-include 的诊断头进入 `.S` 编译，找不到 `cstdio`。
增加 `__ASSEMBLER__` 保护后，实际复验：

```bash
make -C example -f demo/Makefile -j2 demo-transport > example/demo/runs/dev-build/build-assembler-fix.log 2>&1
./example/demo/run_demo.sh all --seconds 3 --no-build > example/demo/runs/dev-build/short-all-console.log 2>&1
```

构建退出 0。首轮短测退出 1；A 通过，B 发送失败。原始目录
`example/demo/runs/20260912-164545-Rmm6kA/` 的 `B_pub.runtime.log` 记录
`incompatible notifier shm layout`：宿主遗留 key `0x31c7e6c9`、shmid `19`、
196616 bytes 的旧通知区与当前实现不兼容。B 首条未收到，C～E 未执行，不能写成通过。
没有删除或修改该宿主通知区。实际隔离能力检查：

```bash
unshare --user --map-root-user --ipc sh -c 'id; ipcs -m' > example/demo/runs/dev-build/ipc-probe.log 2>&1
cat example/demo/runs/dev-build/ipc-probe.log
ipcs -m
make -C example -f demo/Makefile -j2 demo-transport > example/demo/runs/dev-build/build-qos-fix.log 2>&1
./example/demo/run_demo.sh all --seconds 3 --no-build > example/demo/runs/dev-build/short-isolated-console.log 2>&1
```

IPC probe 退出 0，独立 namespace 内无旧通知区，宿主旧通知区不变。
脚本随后自动进入独立 user/IPC namespace，保留原网络 namespace；普通 QoS depth
使用 16，Loan 的 Blocker history 使用 0；构建退出 0。
第二轮短测退出 1，原始目录 `example/demo/runs/20260912-164730-oqIxeu/`：A/B/C 通过，
D 第一订阅端通过且真实 OFFLINE/SHM DISABLED 被观察到，但全新 D_sub2 进程未收消息，
20 秒超时；E 未执行。发布端重新匹配并 ENABLED，发送仍成功，证明不能把发送成功当送达。

Demo 层修复：仅 D 在观察到真实非空订阅者集合变化后，读取本 Node 已登记的 Writer
完整属性，用现有 `ChannelManager::Join(writer, ROLE_WRITER)` 重新公告仍在线的发布端。
不重启 Publisher、不伪造 reader/host、不修改核心 Discovery。后续 D 的通过结论均以
这个明确输出的 `REANNOUNCE_LIVE_WRITER` 为前提，不宣称原生无条件新进程自动恢复。

```bash
make -C example -f demo/Makefile -j2 demo-transport > example/demo/runs/dev-build/build-reannounce.log 2>&1
./example/demo/run_demo.sh all --seconds 3 --no-build > example/demo/runs/dev-build/short-reannounce-console.log 2>&1
```

构建和短测均退出 0，原始目录 `example/demo/runs/20260912-164932-Y8jWB4/`。
A～E 全部满足真实后端事件、有效接收、连续 10 条和进程退出检查；D 同一发布 PID
`362621`，重新加入后首序号 `44`，大于前一订阅者末序号 `31`。
完整命令含频道、Payload、频率、时长、PID、wait 状态见该轮 `commands.txt`。

双终端进入同一 namespace 的说明也实际检查过（退出 0），命令如下；结果保存为
`example/demo/runs/dev-build/manual-namespace-probe.txt`：

```bash
python3 - <<'PY'
from pathlib import Path
import subprocess
p = subprocess.Popen(['unshare', '--user', '--map-root-user', '--ipc', 'sh', '-c', 'readlink /proc/self/ns/ipc; read reply'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
try:
    expected = p.stdout.readline().strip()
    r = subprocess.run(['nsenter', '--target', str(p.pid), '--user', '--ipc', '--preserve-credentials', 'readlink', '/proc/self/ns/ipc'], capture_output=True, text=True)
    result = f'target_pid={p.pid} expected={expected} exit={r.returncode} stdout={r.stdout} stderr={r.stderr}'
    Path('example/demo/runs/dev-build/manual-namespace-probe.txt').write_text(result)
    print(result)
    assert r.returncode == 0 and r.stdout.strip() == expected
finally:
    p.communicate('exit\n', timeout=5)
PY
```

### 默认完整流程与最终检查

默认两轮连续运行的实际命令：

```bash
for round in 1 2; do date --iso-8601=seconds > "example/demo/runs/dev-build/full-${round}-start.txt"; ./example/demo/run_demo.sh > "example/demo/runs/dev-build/full-${round}-console.log" 2>&1 || exit "$?"; date --iso-8601=seconds > "example/demo/runs/dev-build/full-${round}-end.txt"; done
make -C example -n publisher subscriber test_hybrid_intra benchmarks > example/demo/runs/dev-build/original-targets-dry-run.log 2>&1
nm -C example/demo/build/bin/demo_transport > example/demo/runs/dev-build/symbols.txt
bash -n example/demo/run_demo.sh
```

原目标 dry-run、符号导出和 Bash 语法检查退出 0；dry-run 不是构建原目标或运行 benchmark。
完整运行的结果和后续验收在下文追加，不能仅凭命令存在推定通过。

两轮脚本均退出 0，各 188 秒：第一轮 16:50:35～16:53:43，第二轮
16:53:43～16:56:51（Asia/Shanghai）。第一轮原始目录
[`20260912-165035-7xn25A`](demo/runs/20260912-165035-7xn25A/commands.txt)，第二轮
[`20260912-165343-AFJOBA`](demo/runs/20260912-165343-AFJOBA/commands.txt)。
链接中的 `commands.txt` 包含实际全部角色命令、频道、PID、重定向和退出状态，未省略参数。
每轮 10 个角色进程均退出 0，所有场景均 PASS；统计如下：

| 场景 | 第一轮有效接收 | 第二轮有效接收 | 真实证据（每轮目录内） |
| --- | --- | --- | --- |
| A，自动 INTRA | 293 | 292 | `A.log` 的 MATCHED、INTRA ENABLED、首条和最终汇总 |
| B，跨进程自动 SHM | 293 | 291 | `B_pub.log` 的 IP/PID、SHM ENABLED；`B_sub1.log` 的有效接收 |
| C，1 MiB SHM Loan/View | 149 | 149 | `C_pub.log` 的 LOAN_SEND shm_backed；`C_sub1.log` 的 SHM_READ_ONLY_VIEW |
| D，正常退出与恢复 | 前 293 / 后 292 | 前 291 / 后 291 | `D_pub.log` 的 OFFLINE、DISABLED、真实 Writer 重公告、第二次 ENABLED；两个 `D_sub*.log` 的连续校验 |
| E，同机强制 RTPS | 293 | 294 | `E_pub.log` 的 RTPS ENABLED；`E_sub1.log` 的首条和有效接收 |

所有接收段均 `invalid=0 gaps_online=0 order_errors=0`；第一条之前不统计缺口。
发送端所有 `attempts=success`、`fail=0`，这不表示每次发送都送达。
第一轮 A/B/C/D/E 的发送尝试分别为 293/297/150/598/295，第二轮为
292/293/149/596/296。无订阅者时的普通发送仍独立记录 `no_peer_attempts`。
第一轮 D 发布 PID `377397`、前一接收末序号 294、恢复首序号 306；第二轮
PID `409942`、前末序号 292、恢复首序号 305。每轮两次重公告使用同一 Writer id。
发布端不重启，恢复后连续接收至少 10 条且完成 30 秒观察；不包含离线补发结论。

4 MiB 和 Ctrl+C 的实际补充验收命令：

```bash
./example/demo/run_demo.sh C --payload 4194304 --seconds 3 --no-build > example/demo/runs/dev-build/loan-4m-console.log 2>&1
python3 example/demo/check_interrupt.py > example/demo/runs/dev-build/interrupt-summary.txt 2>&1
ipcs -m > example/demo/runs/dev-build/ipc-final.log
```

三条命令退出 0。4 MiB 原始目录
[`20260912-165716-YEPHGa`](demo/runs/20260912-165716-YEPHGa/commands.txt)：
15 条有效接收（序号 1～15），全字节及只读 View 属性校验通过，发送失败/在线缺口均为 0。
Ctrl+C 辅助检查本身退出 0；其被检查脚本返回 **130**，不是完整场景 PASS。
辅助程序在 B 连续接收 10 条后向本次独立进程组发送 SIGINT，两个子进程
`432943`、`432944` 均正常退出 0，已 wait 且 `/proc/<pid>` 不存在，没有使用 SIGKILL。
证据目录 [`20260912-165720-AH5zYd`](demo/runs/20260912-165720-AH5zYd/commands.txt)，
控制台记录 [`interrupt-x2l8550w/console.log`](demo/runs/interrupt-x2l8550w/console.log)。
最终宿主 `ipcs -m` 仍只有原 key `0x31c7e6c9`、shmid `19`、196616 bytes、连接数 0 的通知区。

另外实际执行一次负向检查：发布端发送 1024 字节，订阅端期望 2048 字节，必须拒绝。
完整命令如下：

```bash
unshare --user --map-root-user --ipc python3 - <<'PY' > example/demo/runs/dev-build/reject-mismatch-summary.txt 2>&1
from pathlib import Path
import os, subprocess, tempfile
root = Path.cwd()
out = Path(tempfile.mkdtemp(prefix='reject-', dir=root / 'example/demo/runs'))
env = dict(os.environ, CMW_PATH=str(root), CMW_DEMO_TRACE='1')
channel = 'demo_reject_' + str(os.getpid())
children = []
try:
    for role, payload in [('pub', '1024'), ('sub', '2048')]:
        cmd = [str(root / 'example/demo/build/bin/demo_transport'), '--role', role, '--scenario', 'B', '--channel', channel, '--payload', payload, '--hz', '10', '--seconds', '3']
        stream = (out / (role + '.log')).open('w')
        p = subprocess.Popen(cmd, env=env, stdout=stream, stderr=subprocess.STDOUT)
        children.append((role, p, stream))
        with (out / 'commands.txt').open('a') as f:
            f.write(repr(cmd) + f' pid={p.pid}\n')
    status = {role: p.wait(timeout=15) for role, p, stream in children}
    text = (out / 'sub.log').read_text()
    assert status['sub'] == 1 and '[CHECK] INVALID' in text and '[RESULT] FAIL' in text, (status, text)
    print('PASS rejection test: real SHM payload-size mismatch caused invalid reception and nonzero exit;', status, 'logs=', out)
finally:
    for role, p, stream in children:
        if p.poll() is None:
            p.terminate()
        p.wait(timeout=10)
        stream.close()
        source = root / 'log' / f'demo_{channel}_{p.pid}.log'
        if source.exists():
            source.rename(out / f'{role}.runtime.log')
PY
```

负向检查命令退出 0，表示确实观察到预期失败；原始目录
[`reject-2ktr8ty5`](demo/runs/reject-2ktr8ty5/commands.txt)：发布端退出 0、订阅端退出 1，
接收端打印 INVALID 和 RESULT FAIL，未将错误消息计为有效接收。

文档、入口和空白初检输出在 `demo/runs/dev-build/audit-initial.log`，完整两轮数据/路由
复算输出在 `full-evidence-audit.log`，均退出 0。最终将这些检查保存为
[`final-audit.py`](demo/runs/dev-build/final-audit.py)，实际复验命令：

```bash
python3 example/demo/runs/dev-build/final-audit.py > example/demo/runs/dev-build/audit-final.log 2>&1
git diff --check
git status --short
```

检查退出 0：本地 Markdown 链接/锚点、原目标 dry-run 隔离、Demo 唯一 main、两轮
接收区间逐项复算、发送结果、Loan/View、D 相同 Writer id 的启停重公告、生成文件忽略
和空白均通过。扫描本次所有保留记录中的 **47 个子 PID、24 个 channel id**，PID 均不存在，
对应 `/dev/shm/cmw_<id>` 均不存在；没有删任何宿主通知区或陌生共享段。

未验证真实跨主机或 SIGKILL 故障恢复，未重跑无关 benchmark/完整回归；D 的成功以
Demo 显式重公告为前提。工作保留在本地 `dev`，未 commit、push 或合并。

补齐记录后执行 `python3 example/demo/runs/dev-build/final-audit.py > example/demo/runs/dev-build/audit-final-after-record.log 2>&1`
和 `git diff --check`，复查包含本 testlog 新链接的文档、证据及资源状态。

## 2026-09-13 Discovery 全新订阅进程自动发现修复

工作目录 `/home/jim/cpp/CyberRT`，分支 `dev`，HEAD
`3c128c37fbc1e5359f6ddb5f185ea5dcd5dfe50e`。Ubuntu 22.04、Linux
`6.8.0-138-generic`、GCC 11.4.0、本地 Fast DDS 2.12.0。环境摘要保存在
`log/discovery-20260913/environment.txt`。

开始时保存当前文件哈希、Demo 源码和历史 testlog，均在 `log/discovery-20260913/`。
保留之前的文档整理及三个发送后端的诊断修改。本次没有安装依赖、修改系统配置或删除宿主共享内存。

### 问题与修改

去掉 Demo 的 `REANNOUNCE_LIVE_WRITER` 后重现：Publisher 能发现新 Reader 并启用 SHM，
但全新订阅进程没有旧 Writer 信息，无法收到消息。

本机源码确认：`QOS_PROFILE_TOPO_CHANGE` 声明 RELIABLE / TRANSIENT_LOCAL；
`AttributesFiller` 只设置公告 QoS，将实际 Reader/Writer 的 reliability 留在 BEST_EFFORT，
Reader durability 留在 VOLATILE。Fast DDS 的端点构造使用 `ratt/watt.endpoint`，
`registerReader/registerWriter` 的公告 QoS 不会替代这个配置；可靠 Writer 的历史发送路径位于
本地 SDK `src/Fast-DDS/src/cpp/rtps/writer/StatefulWriter.cpp` 的 late-joiner 分支。

修复仅在 `Manager::CreateReader/CreateWriter` 显式设置底层端点为
`RELIABLE + TRANSIENT_LOCAL`，与 Discovery 公告一致。不修改普通 RTPS 数据端点的配置、
SHM 布局、锁或序列化协议。Demo 删除原来的 Writer 重公告。
新增 `test_discovery_late_join`，接入 `check-integration`；每轮通过全新 exec 查询历史 Writer，
确认此前的 Writer LEAVE 生效，然后创建 Subscriber、校验连续消息，三轮保持同一 Publisher。

### 修复前的实际失败

以下构建使用默认普通目录 `example/build/`、Demo 目录 `example/demo/build/`，
Fast DDS 为 `/home/jim/cpp/fastdds_2.12/install`；未覆盖 `BUILD_DIR`、`OPTFLAGS`、
`FAST_DDS_HOME` 或 `SANITIZE`。Demo 保留其 Makefile 的 `-O2 -g0`。
修复前已经删除 Demo 重公告，但 Discovery 代码仍是旧实现：

```bash
cd /home/jim/cpp/CyberRT
mkdir -p log/discovery-20260913
timeout --signal=TERM --kill-after=5s 300s make -C example -f demo/Makefile -j2 demo-transport > log/discovery-20260913/build-before.log 2>&1
timeout --signal=TERM --kill-after=5s 90s ./example/demo/run_demo.sh D --seconds 3 --no-build > log/discovery-20260913/demo-D-before.log 2>&1
timeout --signal=TERM --kill-after=5s 300s make -C example -j2 test_discovery_late_join > log/discovery-20260913/regression-build-before.log 2>&1
CMW_PATH="$PWD" unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 90 test_discovery_late_join > log/discovery-20260913/regression-before.log 2>&1
```

两个构建退出 0。D 退出 1：第二个 Subscriber 等待 20 秒仍为 `valid=0`；原始记录在
`example/demo/runs/20260913-143942-eC6t5Q/`。新回归也退出 1：10 秒发现期限内
`historical Writer discovery: count=0`，不是构建失败，也不是外层 90 秒超时。
这些失败保留，未用成功日志覆盖。

### 修复后的构建与运行

修改 Discovery 后实际执行：

```bash
cd /home/jim/cpp/CyberRT
timeout --signal=TERM --kill-after=5s 300s make -C example -j2 test_discovery_late_join test_hybrid_intra test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle test_loaned_message_discovery_churn test_rtps_same_host_multiprocess > log/discovery-20260913/regression-build-after.log 2>&1
timeout --signal=TERM --kill-after=5s 300s make -C example -f demo/Makefile -j2 demo-transport > log/discovery-20260913/demo-build-after.log 2>&1
CMW_PATH="$PWD" unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 90 test_discovery_late_join test_hybrid_intra test_hybrid_shm_multiprocess test_hybrid_dynamic_shm_lifecycle test_loaned_message_discovery_churn test_rtps_same_host_multiprocess > log/discovery-20260913/regression-after.log 2>&1
timeout --signal=TERM --kill-after=5s 120s ./example/demo/run_demo.sh D --no-build > log/discovery-20260913/demo-D-after.log 2>&1 && timeout --signal=TERM --kill-after=5s 120s ./example/demo/run_demo.sh all --seconds 3 --no-build > log/discovery-20260913/demo-all-after.log 2>&1
```

两个构建、回归 runner、D 默认流程和 A～E 短流程均退出 0。
回归 runner 汇总为 **passed=6、failed=0、timed_out=0**，合计 8 个 gtest 用例：

| 程序 | 实际结果 |
| --- | --- |
| `test_discovery_late_join` | 三个全新进程在 Reader JOIN 前发现同一旧 Writer，各收 10 条连续有效消息 |
| `test_hybrid_intra` | 同进程自动 INTRA 的 3 个用例通过 |
| `test_hybrid_shm_multiprocess` | Subscriber 先启动、真实 Discovery 自动 SHM 通过 |
| `test_hybrid_dynamic_shm_lifecycle` | 普通消息动态 SHM、较长拓扑通知通过 |
| `test_loaned_message_discovery_churn` | Loan 发布期间的 INTRA/SHM 订阅变化通过 |
| `test_rtps_same_host_multiprocess` | 同机强制 RTPS 通过 |

新回归中，Publisher PID 为 **69397**，Writer ID 为 **1928490569995350791**。
三个订阅 PID 为 69405、69455、69551，接收区间分别是 2～11、74～83、88～97；
每轮均打印 `before Reader JOIN` 和 `round=... PASS`。离线区间不计入下一进程的序号缺口。
运行库日志由 Logger 写入根目录 `log/DiscoveryLateJoin_<pid>.log`。

D 默认每段接收 30 秒，原始记录为 `example/demo/runs/20260913-144618-FOLPl6/`：
Publisher PID **71246** 始终不变，发送 attempts=success=599、fail=0；旧订阅进程收到
2～293，共 292 条，新进程收到 306～598，共 293 条。真实 OFFLINE、SHM DISABLED、
再次 ENABLED 和 `RECOVERED ... contiguous=10` 均出现，没有 Writer 重公告。

A～E 短流程原始记录为 `example/demo/runs/20260913-144721-95IFso/`：
A/B/C/D 前段/D 后段/E 分别收到 30/30/15/30/30/30 条有效消息；C 为默认 1 MiB、5 Hz。
D Publisher PID 79270，旧段最后 31、新段首条 44。全部接收端 invalid、gaps_online、
order_errors 为 0，所有发布端 fail=0。原始命令、PID、退出码和运行库日志仍沿用 Demo 的独立运行目录，
本次汇总与构建输出保存在根目录 `log/discovery-20260913/`。

### 静态检查与边界

实际命令：

```bash
cd /home/jim/cpp/CyberRT
make -C example -n publisher subscriber tests demos benchmarks > log/discovery-20260913/build-targets.log 2>&1
make -C example -f demo/Makefile -n demo-transport > log/discovery-20260913/demo-target.log 2>&1
nm example/demo/build/bin/demo_transport > log/discovery-20260913/demo-symbols.log
python3 log/discovery-20260913/audit_evidence.py > log/discovery-20260913/audit-evidence.log 2>&1
python3 log/discovery-20260913/audit_docs.py > log/discovery-20260913/audit-docs.log 2>&1
git diff --check
git status --short
```

入口 dry-run、符号和运行证据检查退出 0：新增 main 未混入原目标，Demo 中不再调用 Writer 重公告；
检查到的 20 个 Demo/新回归子 PID 均不存在，7 个 Demo 频道共享段不存在；本次范围外的已有文件
与开始时哈希一致。文档检查结果在 `audit-docs.log`，覆盖本地链接/锚点、围栏、shell 语法、
测试目标和历史 testlog 前缀保留；shell 示例的静态解析不计为额外程序测试。

本次只验证同机正常退出及全新进程加入，没有运行 sanitizer、全部回归或 benchmark；
未重复 9 月 12 日的两轮 3 分钟完整演示、4 MiB 和 Ctrl+C 检查。
原有有限 History 的淘汰策略未改，历史公告淘汰后的拓扑重建、跨主机和 SIGKILL 恢复不在本次结论内。
恢复只接收之后的新消息，不提供离线补发。修改保留在本地，未 commit、push 或合并。

最终文档检查退出 0：13 个 Markdown、174 处本地链接/锚点、28 段 shell 示例、40 处目标引用通过；
历史 testlog 原文按前缀比对保留。补齐本摘要后再次执行
`python3 log/discovery-20260913/audit_docs.py > log/discovery-20260913/audit-docs-final.log 2>&1`
和 `git diff --check`，结果均为退出 0。


## 2026-09-14 QoS 统一与历史策略回归

分支 `dev`，HEAD `7c00e7f0f4e2ad6682b0181e030ce5a53dd7f42f`，在未提交的 QoS 修改上验证。
日期使用 Asia/Shanghai；首个构建于 15:12 开始，完整 check 于 15:22 开始。
环境：Linux `6.8.0-138-generic` x86_64，g++ `11.4.0`（Ubuntu 22.04），
C++14、`-faligned-new`；Fast DDS `/home/jim/cpp/fastdds_2.12/install`（2.12）。
除另有说明，SANITIZE/OPTFLAGS 未指定，GoogleTest 不作过滤；所有命令工作目录均为
`/home/jim/cpp/CyberRT`。功能与兼容性见 [README](../README.md#qos-配置与执行边界)，
用例职责见 [TESTING](TESTING.md#qos-回归)。

实际执行命令，按顺序保留：

```bash
mkdir -p log/qos-20260914
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 test_qos > log/qos-20260914/build-01.log 2>&1
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 test_qos test_qos_rtps > log/qos-20260914/build-02.log 2>&1
```

- `build-01.log`：退出 2，新增 QosHistory 使用 `RecursiveTimedMutex` 时遗漏
  `eprosima::fastrtps` 命名空间，未进入程序测试。
- `build-02.log`：修正后退出 0，仅表示两个目标构建成功。

```bash
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build-qos-20260914/bin --timeout 90 test_qos test_qos_rtps > log/qos-20260914/targeted-01.log 2>&1
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build-qos-20260914/bin --timeout 90 test_qos_rtps > log/qos-20260914/targeted-02.log 2>&1
```

- `targeted-01.log`：沙箱内运行，整体退出 1。`test_qos` 的 11 项全部通过；
  RTPS 8 项因 `getifaddrs/open: Operation not permitted` 无法完成网络端点创建。
  属于环境阻止启动，不是 RTPS 语义验证通过；runner 为 passed=1、failed=1、timed_out=0。
- `targeted-02.log`：获得本机网络测试执行权限后，在独立 user/IPC namespace 运行，
  退出 1。RTPS 7 项通过；`ReliableKeepAllRetriesAfterReaderCapacityBecomesAvailable`
  在释放接收 History 空间后等不到第三条样本，8 秒等待失败。
  排查发现原始端点夹具走 Fast DDS 的进程内直送；其 Writer 根据 `processDataMsg`
  返回值处理投递状态，不能用来验证 UDP 拒收后的协议重传。后续夹具关闭
  `INTRAPROCESS_FULL`，显式采用 `INTRAPROCESS_OFF` 和 UDPv4；没有增加应用重发。
  这次失败记录保留，不把后续成功视为原进程内路径已修复。

```bash
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 tests demos > log/qos-20260914/build-03.log 2>&1
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 tests demos > log/qos-20260914/build-04.log 2>&1
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build-qos-20260914/bin --timeout 90 test_qos_rtps > log/qos-20260914/targeted-03.log 2>&1
```

- `build-03.log`：退出 2，测试新增的 UDPv4TransportDescriptor 缺少
  `eprosima::fastdds::rtps` 命名空间；构建未全部完成，没有运行测试。
- `build-04.log`：修正后退出 0，全部正式测试和旧手工 demos 目标仅构建成功。
- `targeted-03.log`：独立 IPC、允许本机网络，退出 0，RTPS 8 项全部通过。
  包括 KEEP_LAST 两条保留深度、KEEP_ALL 满时连续拒绝且保留旧样本、可靠性匹配与拒绝、
  Reader 保留容量、只发布一次的第三条样本在空间释放后由 RTPS 重传成功、
  两种历史策略在全新 exec 接收进程中回放、两种创建顺序下共享 Reader 冲突拒绝。
  原始端点用例为本机 UDPv4；生产回放用例为同机强制 RTPS 跨进程。

```bash
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' unshare --user --map-root-user --ipc make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 check > log/qos-20260914/check-01.log 2>&1
```

`check-01.log`：退出 0，快速组 passed=16、集成组 passed=14，failed=0、timed_out=0，
最终 `check: PASSED`。采用 Makefile 默认单程序超时：fast 30 秒、integration 90 秒。
快速组包含新增 `test_qos` 和扩展的 `test_node`（3 项），集成组包含新增 `test_qos_rtps`；
同时覆盖已有 INTRA、跨进程 SHM、Loan、RTPS 生命周期和 Discovery 晚加入回归。
这是同机环境、受控 host 元数据路由及同机强制 RTPS 的验证，未进行真实跨主机实验。
未据此声称 SHM 可靠重传、SHM/INTRA 历史回放或慢回调无损。

完整 check 后，将新增测试夹具中的 RoleAttributes 改为显式零初始化；运行库实现未再修改。
随后执行以下构建，ASan 与 Demo 使用不同构建目录并行进行：

```bash
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build/qos-asan-20260914 SANITIZE=address -j2 test_qos_rtps > log/qos-20260914/build-asan-01.log 2>&1
make -C example -f demo/Makefile -j2 demo-transport > log/qos-20260914/build-demo-01.log 2>&1
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build/qos-asan-20260914/bin --timeout 90 test_qos_rtps > log/qos-20260914/asan-01.log 2>&1
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 test_qos test_qos_rtps > log/qos-20260914/build-05.log 2>&1
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build-qos-20260914/bin --timeout 90 test_qos test_qos_rtps > log/qos-20260914/targeted-04.log 2>&1
```

- `build-asan-01.log`：退出 0，采用 Makefile 的 ASan/non-PIE/frame-pointer 配置。
  Fast DDS 等外部库未重新插桩。
- `build-demo-01.log`：退出 0，默认构建目录 `/home/jim/cpp/CyberRT/example/demo/build`。
  仅确认新 Demo 构建成功，本轮没有运行 Demo A～E，不把它记为场景验收通过。
- `asan-01.log`：独立 IPC、允许本机网络，退出 0，8 项 RTPS QoS 测试全部通过，
  无 AddressSanitizer 错误；`detect_leaks=0`，不包含泄漏验证。
- `build-05.log`：退出 0，仅重建两个初始化方式调整后的 QoS 测试。
- `targeted-04.log`：退出 0，最终源码对应的 `test_qos` 11 项与 `test_qos_rtps` 8 项全部通过，
  runner passed=2、failed=0、timed_out=0。没有重复执行其余未变的完整回归。

所有输出重定向均在 [log/qos-20260914](../log/qos-20260914/)；程序 Logger 文件位于
根目录 `log/`，没有迁移或覆盖用户正在查看的根目录 `PublisherTest.log`。

## 2026-09-14 业务 VOLATILE 与 Discovery 策略精简

分支 `dev`，HEAD `7c00e7f0f4e2ad6682b0181e030ce5a53dd7f42f`，在未提交工作区上验证。
日期使用 Asia/Shanghai。环境：Linux `6.8.0-138-generic` x86_64、g++ `11.4.0`
（Ubuntu 22.04）、C++14、`-faligned-new`；Fast DDS 位于
`/home/jim/cpp/fastdds_2.12/install`（2.12）。所有命令工作目录为 `/home/jim/cpp/CyberRT`。
普通构建未指定 SANITIZE/OPTFLAGS/FAST_DDS_HOME；ASan 沿用独立构建目录及 Makefile 配置。
历史 TRANSIENT_LOCAL 验证记录保留；本轮最终语义见 [README](../README.md#qos-配置与执行边界)，
测试使用方法见 [QoS 回归](TESTING.md#qos-回归)。

本轮把业务默认值改为 VOLATILE，Discovery 显式保留 RELIABLE + TRANSIENT_LOCAL；
删除六个未使用的场景预设、构造工厂与零值常量、示例中的重复默认赋值，以及业务回放测试的
子进程框架。保留显式 RTPS TRANSIENT_LOCAL、历史容量和可靠重传验证。
VOLATILE KEEP_ALL 在满载时允许回收已完成投递的最旧样本，未确认样本仍保留并对新发送返回 false。

实际执行命令，普通构建及完整回归：

```bash
mkdir -p log/qos-volatile-20260914 && make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 tests demos > log/qos-volatile-20260914/build-01.log 2>&1
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' unshare --user --map-root-user --ipc make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build-qos-20260914 -j2 check > log/qos-volatile-20260914/check-01.log 2>&1
```

- `build-01.log`：退出 0，正式测试及 Makefile 的旧手工 demos 目标构建成功；构建本身不计为测试通过。
- `check-01.log`：退出 0，fast passed=16、integration passed=14，failed=0、timed_out=0，
  最终 `check: PASSED`。每程序超时为 Makefile 默认 fast 30 秒、integration 90 秒，GoogleTest 无过滤。
  `test_qos` 11 项、`test_node` 3 项、`test_qos_rtps` 9 项通过，同时运行其余正式回归。
- RTPS 原始端点经本机 UDPv4、关闭 Fast DDS 进程内直送：VOLATILE Reader 对 VOLATILE 和
  TRANSIENT_LOCAL Writer 均不补收匹配前的两条消息，能收到后发的第三条；KEEP_ALL 容量为 2 时
  无 Reader 及收到 ACK 后均可继续发送。接收容量满载时第三、四条各仅发布一次，第五条被 Writer
  拒绝；释放一个 Reader 槽位后第三条经协议重传到达，未靠应用重复发布。
- Discovery KEEP_ALL 原始历史在容量满时保留公告，晚加入 Reader 能回放前两条；显式 RTPS
  TRANSIENT_LOCAL KEEP_LAST 仍只保留配置深度。共享生产 RTPS Reader 冲突拒绝后原 listener 仍可收新消息。
- `test_discovery_late_join`：三个全新进程均在 Reader JOIN 前发现原 Writer，业务 QoS 公告为 VOLATILE，
  然后经自动 SHM 接收新消息，三轮均 PASS。Publisher PID 为 `308624`，Writer ID 为
  `2932741003498723756`；没有依赖业务历史回放或应用 Writer 重公告。

随后另行构建并运行 ASan：

```bash
make -C example BUILD_DIR=/home/jim/cpp/CyberRT/example/build/qos-asan-20260914 SANITIZE=address -j2 test_qos_rtps > log/qos-volatile-20260914/build-asan-01.log 2>&1
env CMW_PATH=/home/jim/cpp/CyberRT CMW_IP=127.0.0.1 GTEST_FILTER='*' ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 unshare --user --map-root-user --ipc bash example/run_tests.sh --bin-dir /home/jim/cpp/CyberRT/example/build/qos-asan-20260914/bin --timeout 90 test_qos_rtps > log/qos-volatile-20260914/asan-01.log 2>&1
```

- `build-asan-01.log`：退出 0，使用 Makefile 的 ASan/non-PIE/frame-pointer 配置；外部 Fast DDS 未重新插桩。
- `asan-01.log`：退出 0，9 项 RTPS QoS 用例全部通过，无 AddressSanitizer 错误；
  runner passed=1、failed=0、timed_out=0。`detect_leaks=0`，不包含泄漏检查。

输出重定向均位于 [log/qos-volatile-20260914](../log/qos-volatile-20260914/)，运行库日志位于根目录 `log/`。
本轮范围为同机 INTRA、跨进程 SHM、同机强制 RTPS 和受控 host 元数据选路；
未做真实跨主机实验、业务 INTRA/SHM 历史补发、SHM 可靠重传或 Demo A～E 场景验收。
