# 调度器：消息回调在哪个线程执行

Node 的 Subscriber 回调由调度器运行。收到消息后，数据进入 DataVisitor，通知对应任务，再由工作线程恢复协程执行回调。
只使用 Publisher/Subscriber 时，不必自己创建 Processor 或 CRoutine。

## 先看执行流程

```mermaid
flowchart LR
    D[收到消息] --> V[DataVisitor 缓存并通知]
    V --> S[Scheduler 标记任务可运行]
    S --> P[Processor 工作线程]
    P --> C[CRoutine 执行 Subscriber 回调]
    C --> Y[回调返回并 Yield]
```

| 对象 | 职责 | 源码 |
| --- | --- | --- |
| Scheduler | 创建、通知、移除任务，管理工作线程 | [scheduler.cpp](../scheduler/scheduler.cpp) |
| SchedulerClassic | 将任务放到指定分组和优先级队列 | [scheduler_classic.cpp](../scheduler/policy/scheduler_classic.cpp) |
| Processor | 运行一个工作线程，循环取协程执行 | [processor.cpp](../scheduler/processor.cpp) |
| ClassicContext | 选择本分组中可运行的协程，无任务时等待 | [classic_context.cpp](../scheduler/policy/classic_context.cpp) |
| CRoutine | 保存栈、状态和执行函数，可挂起再恢复 | [协程说明](croutine.md) |

当前工厂可用策略是 `classic`；`choreography` 分支尚未创建实例，不要因为存在同名配置文件就认为已经支持。

## 怎样选择配置

普通 Demo 使用默认配置即可。需要指定进程组时，在第一次创建 Subscriber 或访问调度器之前设置：

```cpp
#include <cmw/common/global_data.h>
#include <cmw/scheduler/scheduler_factory.h>

// 前提：程序已经 Init，conf/my_process.conf 是准备好的 classic 配置。
hnu::cmw::common::GlobalData::Instance()->SetProcessGroup("my_process");
auto* sched = hnu::cmw::scheduler::Instance();
```

调度器读取 `conf/<进程组>.conf`；请将文件放在 `CMW_PATH/conf/`，避免依赖启动目录。
没有对应文件时使用默认 classic 分组，工作线程数取全局 `default_proc_num`，未配置时回退为 2。
本仓库 [cmw.pb.conf](../conf/cmw.pb.conf) 当前设置为 16。[配置格式与字段](../config/config.md)。

现有 `example_sched_classic.conf` 含特定机器的 CPU 编号和线程策略，不能不检查就照搬到虚拟机。

## 自定义任务如何接入

| 接口 | 使用要点 |
| --- | --- |
| `CreateTask(func, name)` | 名称须唯一；保存并检查 bool 返回值 |
| `CreateTask(factory, name)` | 用 RoutineFactory 绑定 DataVisitor 和通知逻辑 |
| `NotifyTask(id)` | 通知调度器重新检查任务；不是调用方同步执行回调 |
| `RemoveTask(name)` | 停止并移除指定任务，可能等待正在执行的协程 |
| `Shutdown()` | 退出任务和 Processor；程序结束前显式执行 |

普通 Node Subscriber 已封装这些步骤，可从 [Subscriber::Init](../node/subscriber.h) 读起。
任务的 ID 由 `GlobalData::RegisterTaskName(name)` 生成；名称还用于匹配配置里的任务优先级。

## 调度规则与限制

- classic 组内优先级为 0～19，数值越大越先检查；越界值被限制到 19。
- 选中协程前会尝试 Acquire，避免多个 Processor 同时运行同一协程。
- 这是协作式调度：正在运行的函数要返回或 Yield，其他任务才能使用该工作线程。
- 优先级不等于操作系统实时保证，也不保证公平性或固定延迟。
- 回调中长时间阻塞会占住工作线程；增加线程数不能代替正确的任务拆分。
- 删除任务或 Shutdown 不会强制展开挂起协程的栈，资源生命周期见 [协程退出](croutine.md#退出与资源管理)。

关闭前先停止业务调用，再注销/关闭 Subscriber，最后 Shutdown 调度器。不要从仍在运行的任务内部等待它自己退出。

## 怎样验证或排查

在仓库根目录运行 `./example/demo/run_demo.sh A`，即可通过真实 Subscriber 验证调度链路。
`test_scheduler` 和 `test_croutine` 是旧手工检查程序，不属于 `make check`，不能只看退出 0 就断言完整调度测试通过。
[正式测试范围](../example/TESTING.md#测试覆盖)。

| 现象 | 优先检查 |
| --- | --- |
| `No scheduler conf ... use default` | 是否需要专用配置；不需要时这是默认配置提示 |
| CreateTask 返回 false | 调度器是否已关闭、任务名称是否重复 |
| 收到数据却不执行回调 | 任务是否被移除、回调是否阻塞、DataVisitor 是否还存活 |
| 停止程序一直等待任务 | 任务是否长期运行而不返回/Yield |
| 线程策略或绑核异常 | CPU 编号是否在允许范围、是否使用了需权限的实时策略 |
