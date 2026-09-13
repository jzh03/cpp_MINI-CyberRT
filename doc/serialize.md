# 定义消息与序列化

普通消息经 SHM 或 RTPS 发送前，由 `DataStream` 编码成字节；接收端按同一字段顺序解码。
INTRA 共享对象，纯 SHM Loan 使用专用字节缓冲，不走普通消息的序列化路径。

## 1. 定义自己的消息

业务类型继承 `Serializable`，用 `SERIALIZE` 列出要传输的字段。下面是可放入业务源码的示例：

```cpp
#include <cstdint>
#include <string>
#include <cmw/serialize/data_stream.h>

struct DemoMessage : public hnu::cmw::serialize::Serializable {
  uint64_t sequence = 0;
  std::string payload;

  SERIALIZE(sequence, payload)
};
```

然后通过 `Node::CreatePublisher<DemoMessage>`、`CreateSubscriber<DemoMessage>` 使用。
可运行的完整示例见 [demo_transport.cpp](../example/demo/demo_transport.cpp)，操作见 [Demo 指南](../example/demo/README.md)。

两端必须使用相同字段和顺序。不要直接传进程指针、mutex 或带虚表的对象内存。
修改字段不是自动兼容升级，应让通信双方一起更新。

## 2. 单独检查编码和解码

以下函数接在上面的类型定义后，用 `read()` 的返回值判断解码是否成功：

```cpp
bool RoundTrip() {
  DemoMessage sent;
  sent.sequence = 1;
  sent.payload = "hello";

  hnu::cmw::serialize::DataStream encoded;
  encoded << sent;
  hnu::cmw::serialize::DataStream decoded(encoded.data(), encoded.size());

  DemoMessage received;
  return decoded.read(received) &&
         received.sequence == sent.sequence && received.payload == sent.payload;
}
```

`operator >>` 返回流引用；需要显式处理错误时用 `read()` / `read_args()` 的 bool 结果。
读失败后不要继续使用未完整解码的对象；`reset()` 重置读位置与失败状态，`clear()` 还清空字节缓冲。

## 3. 编码规则

| 类型 | 当前规则 |
| --- | --- |
| bool、整数、浮点数 | 类型标记 + 值；通过 `memcpy` 处理，避免未对齐访问 |
| string | 类型标记 + 长度 + 字节 |
| vector/list/set | 类型标记 + 元素数量 + 逐项编码 |
| map | 类型标记 + 元素数量 + 每对 key/value |
| 自定义类型 | CUSTOM 标记 + `SERIALIZE(...)` 中按顺序列出的字段 |

容器保存的是元素数量，不能将 `vector<复杂类型>` 当作一段连续 wire 数据直接复制。
读取会检查类型、剩余长度和容器数量；截断、错误标记、非法长度会失败，不能误推进读取位置。
这些检查不能替代业务校验，序号、取值范围和内容仍需接收端检查。

## 4. 运行相关测试

在仓库根目录执行；以下是操作示例，不代表本次已经运行：

```bash
export CMW_PATH="$PWD"
make -C example -j2 test_serialize
bash example/run_tests.sh --bin-dir "$PWD/example/build/bin" --timeout 30 test_serialize
```

构建依赖与更多检查见 [测试指南](../example/TESTING.md)，实际结果见 [testlog](../example/testlog.md)。

源码阅读顺序：[serializable.h](../serialize/serializable.h) 的宏 → [data_stream.h](../serialize/data_stream.h) 的容器模板 → [data_stream.cpp](../serialize/data_stream.cpp) 的基础读写和边界检查。
