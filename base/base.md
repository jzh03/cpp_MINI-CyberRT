# 基础组件导航

`base/` 提供通信和调度器复用的小组件。使用 Node 做业务开发时通常不必直接调用；阅读源码时按用途查找即可。

| 需要什么 | 文件 | 主要用途 |
| --- | --- | --- |
| 信号与回调连接 | [signal.h](signal.h) | Discovery 和消息分发通知 |
| 原子读写锁与作用域锁 | [atomic_rw_lock.h](atomic_rw_lock.h)、[rw_lock_guard.h](rw_lock_guard.h) | 保护进程内共享状态 |
| 有界/无界队列 | [bounded_queue.h](bounded_queue.h)、[unbounded_queue.h](unbounded_queue.h) | 缓存任务或事件 |
| 等待策略 | [wait_strategy.h](wait_strategy.h) | 队列无数据或空间不足时的等待方式 |
| 对象池 | [object_pool.h](object_pool.h)、[concurrent_object_pool.h](concurrent_object_pool.h) | 复用对象，协程上下文也使用对象池 |
| 哈希映射 | [atmoic_hash_map.h](atmoic_hash_map.h) | 全局注册表、分发器索引；文件名保留仓库原拼写 |
| 线程池 | [thread_pool.h](thread_pool.h) | 提交并在线程中执行任务 |

## 信号最小示例

以下是用法片段，可放入已包含项目头文件的程序中：

```cpp
#include <cmw/base/signal.h>

void Example() {
  hnu::cmw::base::Signal<int> changed;
  int latest = 0;
  auto connection = changed.Connect([&latest](int value) { latest = value; });
  changed(42);
  changed.Disconnect(connection);
}
```

Signal 在触发时复制槽列表，解锁后执行回调。Disconnect 只注销连接，**不是等待正在运行的回调结束**。
回调捕获的对象必须活到调用结束，不能在注销后立刻假定所有线程都不再使用它。
Connection 析构也不能代替显式注销。

## 队列与对象池的使用原则

- 检查初始化、入队、出队和对象获取的返回结果；满队列、空队列、池耗尽要有业务处理。
- 指针元素的生命周期由调用者保证；队列不会自动把裸指针变成共享所有权。
- 对象池降低重复分配成本，不等于无限容量或所有操作都无锁。
- 这些进程内工具不能直接复制进共享内存跨进程使用；SHM 使用自己的 State、Block 和 Lease。
- 带缓存行对齐的对象需要正确的分配方式；项目 C++14 构建启用 `-faligned-new`。

实际使用位置可从 [Discovery](../doc/topology.md)、[Transport](../doc/transport.md) 和 [协程](../doc/croutine.md) 进入。
正式构建和测试统一走 [example/TESTING.md](../example/TESTING.md)，不用给每个头文件单独造一套构建方式。
