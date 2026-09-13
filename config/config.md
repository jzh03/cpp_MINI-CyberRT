# 配置怎么使用

程序运行前，在仓库根目录设置：

```bash
export CMW_PATH="$PWD"
```

这让运行库能找到 `conf/` 和日志目录。Demo 脚本会自动设置，无需重复操作。

## 配置入口

| 配置 | 用途 | 读取位置 |
| --- | --- | --- |
| [conf/cmw.pb.conf](../conf/cmw.pb.conf) | 全局协程池、默认工作线程数 | [GlobalData::InitConfig](../common/global_data.cpp) |
| `conf/<进程组>.conf` | 指定 classic 分组、任务优先级和工作线程 | [Scheduler 工厂](../scheduler/scheduler_factory.cpp)、[SchedulerClassic](../scheduler/policy/scheduler_classic.cpp) |
| [RoleAttributes](RoleAttributes.h) | 单个通信端点的频道、类型、主机信息和 QoS | Node / Transport 创建端点时 |

文件名虽然有 `.pb.conf`，**当前解析格式是 JSON**，见 [conf_parse.cpp](conf_parse.cpp)。
全局 JSON 解析只接入 `scheduler_conf`；不要以为在文件中填写 `transport_conf` 就会改变实际后端。

## 默认配置适合先跑通

当前全局配置如下：

```json
{
  "scheduler_conf": {
    "routine_num": 100,
    "default_proc_num": 16
  }
}
```

`routine_num` 用于协程上下文池容量；`default_proc_num` 用于没有专用配置时的工作线程数。
默认不足以满足业务负载时再调节，不能把线程数设得越大就当作越快。

## 需要专用调度配置时

1. 在 `CMW_PATH/conf/` 准备 JSON 文件，例如 `my_process.conf`，使用 `classic` 策略。
2. 在首次创建 Subscriber/调度器前调用 `GlobalData::SetProcessGroup("my_process")`。
3. 启动后检查日志是否实际读取了该配置；如果提示使用默认值，核对文件名和路径。

[example_sched_classic.conf](../conf/example_sched_classic.conf) 展示了完整字段，但包含特定机器的 CPU 编号及实时线程策略，不能直接套到所有机器。

| 常用字段 | 含义 |
| --- | --- |
| `policy` | 当前使用 `classic`；`choreography` 工厂分支未完成 |
| `classic_conf.groups[].name` | 调度分组名称 |
| `processor_num` | 该分组工作线程数 |
| `tasks[].name` / `prio` | 按任务名称匹配优先级，classic 范围 0～19 |
| `cpuset` / `affinity` | CPU 范围及绑定方式，需要与机器允许的 CPU 一致 |
| `processor_policy` / `processor_prio` | 操作系统线程调度策略和优先级，部分策略需要权限 |

工厂先尝试当前目录下的 `conf/`，再尝试 `CMW_PATH/conf/`；classic 的组配置从后者读取。
为避免两处文件不一致，统一把配置放在项目根目录的 `conf/`。[调度流程](../doc/scheduler.md)。

## 通信配置容易混淆的地方

- 自动选路直接使用 [SelectMode](transport_mode.h) 的真实 IP/PID 判断，不靠修改 JSON 中的模式名切换。
- 默认 SegmentFactory 创建 POSIX Segment；NotifierFactory 默认使用 ConditionNotifier。
- 要强制后端，使用低层 Transport 的 `OptionalMode` 参数；Node 没有该参数。
- `qos_profile.msg_size` 在 Loan 中作为请求容量限制，仍受 SHM 最大消息限制。
- `qos_profile.depth` 与 Subscriber 的待处理队列大小不是同一个概念；不要只改一个值就声称消息不会丢失。

字段定义见 [qos_profile.h](qos_profile.h)，使用示例见 [Demo](../example/demo/README.md)。

## 常见问题

配置找不到时检查 `CMW_PATH`；JSON 写错时先检查语法。`WorkRoot()` 未设环境变量时回退 `/cmw`，
与 Logger 的编译时根目录回退不同，所以手动启动统一设置 `CMW_PATH` 最清楚。
不要为运行 Demo 修改系统调度权限或 CPU 配置；先用默认配置完成验证。
