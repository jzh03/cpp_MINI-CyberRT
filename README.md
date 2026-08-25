# MINI_CyberRT

MINI_CyberRT 是针对原始 cmw 项目进行的二次开发，在保留原有 CyberRT 与 Fast DDS 中间件架构的基础上，进一步改善健壮性并增加测试覆盖。

原作者提供的项目视频：https://space.bilibili.com/281708692/lists/5849251?type=season

原作者提供的讲解文档：[飞书](https://ai.feishu.cn/drive/folder/PiqFfxWx5l9Ri2dds9WcI3ognCd?from=from_copylink)

## 对原项目的改进

### 序列化安全

- `DataStream` 在读取前校验剩余 buffer，非法长度或截断数据会进入失败状态，不会错误推进读位置。
- `vector<T>` 改为记录元素数量并逐元素序列化；基础类型使用 `memcpy`，避免未对齐访问和未定义行为。

### SHM 健壮性

- 明确限制 SHM 最大消息为 32 MiB，超限消息直接返回失败，不再继续 Recreate 或 `memcpy`。
- Segment Recreate 后再次校验实际 Block capacity，并保留原有的正常自动扩容。
- 所有 Block 被占用时最多扫描一轮后返回失败，避免无限 busy-spin；读 Block 失败时立即丢弃本次消息，不再访问无效内存。

### API 正确性

- `Publisher::Publish()` 现在会正确返回底层 `Transmit()` 结果，避免非 `void` 函数无返回值的未定义行为。

### 测试

- 新增序列化边界、SHM 消息大小/Recreate、全 Block 占用、读失败以及 SHM Transmitter/Receiver 恢复能力的测试。
