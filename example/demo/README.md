# MINI CyberRT 通信 Demo

用约 3 分钟演示自动选路、大消息 Loan/View 和订阅者退出恢复。所有场景都在本机运行。

## 1. 先跑一遍

在仓库根目录执行：

```bash
./example/demo/run_demo.sh
```

脚本会自动构建、依次运行 A～E、检查结果并回收子进程。每段默认观察 30 秒，D 有两段；首次编译时间另计。
看到每个场景的 `[RESULT] PASS scene=...`，最后出现 `[RESULT] PASS requested=all`，才表示完整演示通过。

需要 Linux、g++、make、`unshare` 和已安装的 Fast DDS；[依赖路径检查](../../doc/fastrtps.md)。
脚本自动设置 `CMW_PATH`，并让本次进程共享独立 IPC 环境，避开宿主旧通知区。若 `unshare` 被禁用，脚本会报错退出。

只构建，或使用其他 Fast DDS 安装目录：

```bash
make -C example -f demo/Makefile -j2 demo-transport
FAST_DDS_HOME="$HOME/cpp/fastdds_2.12/install" ./example/demo/run_demo.sh
```

下文命令均为操作示例；已执行的验收记录见 [testlog](../testlog.md#2026-09-12-面试通信-demo-本地验收)。

## 2. 单独运行与讲解顺序

| 场景 | 运行命令 | 做什么、看什么 |
| --- | --- | --- |
| A，约 30 秒 | `./example/demo/run_demo.sh A` | 同进程收发；看 MATCHED、INTRA ENABLED 和首条有效消息 |
| B，约 30 秒 | `./example/demo/run_demo.sh B` | 同机两个进程；看不同 PID、SHM ENABLED 和有效接收 |
| C，约 30 秒 | `./example/demo/run_demo.sh C` | 1 MiB、5 Hz；看 LOAN_SEND、SHM_READ_ONLY_VIEW 和内容校验 |
| D，约 60 秒 | `./example/demo/run_demo.sh D` | 发布端不停；看 OFFLINE、DISABLED、重新 ENABLED 和 RECOVERED |
| E，约 30 秒 | `./example/demo/run_demo.sh E` | **同机强制 RTPS**；看 RTPS ENABLED 和有效接收 |

A/B 展示 Node、Discovery 和自动选路的接入；C 展示 Loan 生命周期管理；D 展示退出与恢复编排；E 展示显式 Transport 接口的使用。

常用选项：

```bash
./example/demo/run_demo.sh C --payload 4194304        # 4 MiB Loan
./example/demo/run_demo.sh all --seconds 3 --no-build # 已构建时的短时复验
./example/demo/build/bin/demo_transport --help       # 程序参数
```

`--seconds` 是每个接收观察段的时长，脚本接受 3～60 秒；`--no-build` 跳过构建。
普通消息默认 1024 字节、10 Hz，Loan 默认 1 MiB、5 Hz。

## 3. 怎么看输出

| 输出 | 含义 |
| --- | --- |
| `[DISCOVERY] MATCHED / OFFLINE` | 真实订阅者集合出现 / 变为空 |
| `[ROUTE] ... ENABLED / DISABLED` | 真实发送后端启用 / 停用；单凭启用不能证明送达 |
| `[CHECK] FIRST_VALID / CONTIGUOUS_10` | 第一条完整校验通过 / 连续收到 10 条正确消息 |
| `[PUB] attempts / success / fail` | 发送尝试数 / API 返回成功数 / 失败数 |
| `[SUB] valid / invalid / gaps_online / order_errors` | 有效接收数 / 校验错误 / 在线序号缺口 / 重复或倒序 |
| `[RESULT] PASS / FAIL` | 本进程或场景的检查结果；看清 `role`、`scene` 或 `requested` |

程序每秒汇总一次，不逐条刷屏。消息会检查序号、长度和全部 Payload 字节；发送失败、校验错误、在线缺口或顺序错误都会导致失败。
**发送成功不等于接收成功**：普通 Publish 无订阅者时也可能返回 true，另计为 `no_peer_attempts`。
每次订阅从首条有效消息建立统计基线，主动离线期间不计入新订阅者的缺口。

## 4. 双终端手动演示

先按第 1 节构建。两个终端都进入仓库根目录，按下列顺序操作。

### 第一步：让两端共享同一 IPC 环境

终端一执行并保持这个 shell 打开：

```bash
unshare --user --map-root-user --ipc bash
echo "DEMO_SHELL_PID=$$"
```

终端二执行，提示后输入终端一刚打印的 PID：

```bash
read -r -p '输入终端一的 DEMO_SHELL_PID: ' DEMO_SHELL_PID
nsenter --target "$DEMO_SHELL_PID" --user --ipc --preserve-credentials bash
```

不要在两端分别执行 `unshare`，否则它们不能共享通知区。

### 第二步：设置相同参数

在两个终端的新 shell 中都执行以下命令。每次重新演示，请双方一起换一个新频道名。

```bash
export CMW_PATH="$PWD" CMW_DEMO_TRACE=1
BIN="$PWD/example/demo/build/bin/demo_transport"
CHANNEL=interview_001
SCENE=D
PAYLOAD=1024
HZ=10
```

### 第三步：启动订阅端，再启动发布端

先在终端二执行；随后在终端一启动发布端，不要超过 20 秒：

```bash
"$BIN" --role sub --scenario "$SCENE" --channel "$CHANNEL" \
  --payload "$PAYLOAD" --hz "$HZ" --seconds 30
```

终端一执行：

```bash
"$BIN" --role pub --scenario "$SCENE" --channel "$CHANNEL" \
  --payload "$PAYLOAD" --hz "$HZ" --seconds 180
```

终端二到时正常退出。等终端一出现 `OFFLINE subscribers=0` 和 SHM `DISABLED`，再执行一次订阅端命令。
看到 `RECOVERED` 是自动脚本的结果；手动运行时查看第二次 `CONTIGUOUS_10`，以及新首序号大于上次末序号。
结束时在发布端按 Ctrl+C，最后两端执行 `exit` 离开隔离 shell。

换场景时，在两端设置相同变量后重复启动命令：

| 演示内容 | 两端共同设置 |
| --- | --- |
| B：普通 SHM | `SCENE=B; PAYLOAD=1024; HZ=10` |
| C：1 MiB Loan | `SCENE=C; PAYLOAD=1048576; HZ=5` |
| C：4 MiB Loan | `SCENE=C; PAYLOAD=4194304; HZ=5` |
| E：同机强制 RTPS | `SCENE=E; PAYLOAD=1024; HZ=10` |

同进程 A 只需一个终端：`"$BIN" --role intra --scenario A --channel "$CHANNEL" --seconds 30`。
程序还支持 `--hz`（1～100）、`--seconds`（1～600）；普通 Payload 为 16～65536 字节，C 只接受 1/4 MiB。至少收齐 10 条才能通过。

## 5. 实现要点与验证边界

- **自动选路：**A～D 使用真实 `CreateNode`、Publisher、Subscriber，保留默认 HYBRID。
  同 IP 同 PID 选 INTRA，同 IP 不同 PID 选 SHM；不同位置的订阅者可以使多条路径同时启用。
  E 使用 `Transport::CreateTransmitter/CreateReceiver(..., OptionalMode::RTPS)`，因为 Node 没有显式模式参数。
- **路径证据：**[trace.h](trace.h) 只在独立 Demo 构建中启用，`CMW_DEMO_TRACE=1` 打开启停输出。
  输出位置在三个发送后端的实际状态切换处。SHM 映射可能延迟到首次发送，仍须以有效接收确认通信。
- **D 怎么恢复：**Publisher 保持运行，旧订阅进程正常退出，观察到 OFFLINE 和 SHM DISABLED 后启动新进程。
  新进程通过 Discovery 保留的历史公告发现旧 Writer，自动恢复 SHM，校验至少 10 条连续新消息。
  已移除 Demo 的 Writer 重新公告；离线期间的消息不补发，也不计入新订阅进程的序号缺口。
  [修复说明与边界](../../README.md#discovery-后启动进程发现)；9 月 12 日的历史验收仍记录当时使用的重公告方式。
- **本机边界：**E 只验证同机强制 RTPS；没有验证真实跨机器自动选路或 SIGKILL 故障恢复。
  受控频率收发通过不等于极限吞吐无丢失。本 Demo 不输出性能提升倍数。

### Loan/View 到底省了哪次拷贝

阅读 [demo_transport.cpp](demo_transport.cpp) 的 `Send<LoanedMessage>` 和 `Receive(LoanedMessage)`：

1. `AcquireMessage(payload)` 借出 SHM 缓冲，确认 `is_shm_backed()`。
2. `Fill(m->mutable_data(), ...)` 在借出的空间直接生成内容，再 `set_size()`、`Publish(std::move(m))`；提交后不再使用原 Loan。
3. 接收回调中的 `shared_ptr<LoanedMessage>` 就是只读 View。通过 `data()` 校验内容，同时检查只读、channel 和 generation；不把裸指针留到回调外。

这省去了普通消息的中间 Payload 序列化/复制入 SHM，以及接收端反序列化时的 Payload 拷贝。
内容生成、校验、元数据和通知仍有成本。读 Lease 随最后一个消息引用释放；Demo 将 Loan 的 Blocker history 设为 0，运行库队列仍可能暂时持有引用。

不比较跨进程指针地址来证明零拷贝。当前纯 SHM Loan 才能保持直接提交原 Block；混合路径可能使用 heap 或复制。
1 MiB/4 MiB 在当前 32 MiB 上限内；共享段约需 65 MiB/257 MiB 空间。

性能测试另见 [benchmark 用法](../TESTING.md#性能程序) 和 [已有性能结果](../testlog.md#2026-09-11-独立进程-shm-性能实验)。

## 6. 失败排查与清理

脚本开头会打印 `run_dir=...`。日志保存在 `example/demo/runs/本轮目录/`：

| 文件 | 用途 |
| --- | --- |
| `build.log` | 构建错误 |
| `A.log`、`B_pub.log`、`B_sub1.log` 等 | 角色输出、路径事件和检查结果 |
| `*.runtime.log` | 运行库 Logger 日志，进程退出后从根目录 `log/` 原样迁入 |
| `commands.txt` | 完整命令、频道、PID、退出码和日志迁移位置 |

| 现象 | 先检查 |
| --- | --- |
| 找不到 Fast DDS 头文件或库 | [依赖检查](../../doc/fastrtps.md)，确认 `FAST_DDS_HOME` |
| `No space left on device` | `df -h . /dev/shm`；编译磁盘与共享内存是两处空间 |
| `unshare: Operation not permitted` | 当前环境不允许隔离运行，需使用已有的兼容 IPC 环境 |
| `incompatible notifier shm layout` | 两端是否进入了同一个新 IPC 环境；不要删除陌生通知区 |
| 匹配或首条超时 | 两端频道、场景、Payload 是否一致；查看 runtime 日志 |
| 已匹配却无法恢复接收 | 重新构建以包含 Discovery 修复，确认两端频道和 IPC 环境一致；恢复成功须看到新的 `CONTIGUOUS_10` |

构建上限 300 秒，匹配/首条最多 20 秒，离线最多 15 秒，正常退出最多 10 秒。启动时的 1 秒预留只用于初始化，不作为成功条件。
Ctrl+C 会请求本次子进程正常退出并等待；仍不退出才终止该子进程并报错。
脚本不执行批量杀进程或清空 `/dev/shm`，只报告本轮确切残留段名。无法确认资源无人使用时不要删除。

构建产物在 `example/demo/build/`，日志在 `runs/`，均已忽略。要重新构建，可删除自己的 `example/demo/build/`；保留 `runs/` 便于排查。
手动启动时 Logger 日志留在根目录 `log/`。Ctrl+C 回收复验可运行 `python3 example/demo/check_interrupt.py`，只用 Python 标准库。
