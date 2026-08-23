# cmw
基于Cyberrt和fast-dds的中间件项目
项目视频：https://space.bilibili.com/281708692/lists/5849251?type=season

8.23:修复了 DataStream 的序列化安全问题：
    1、vector<T> 改为记录元素数量并逐元素序列化；
    2、读取前检查剩余 buffer，非法长度 或截断数据会进入失败状态且不错误推进位置；
    3、基础类型改用 memcpy，避免未对齐访问和 UB。