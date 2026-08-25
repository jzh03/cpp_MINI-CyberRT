#ifndef CMW_SERIALIZ_SERIALIZABLE_H_
#define CMW_SERIALIZ_SERIALIZABLE_H_

namespace hnu{
namespace cmw{
namespace serialize{
    
class DataStream;

class Serializable
{
public:
    virtual void serialize(DataStream & stream) const = 0;
    virtual bool unserialize(DataStream & stream) = 0;
};

// 自定义类型读取时校验类型标记，并向上层传递任一字段的读取失败。
#define SERIALIZE(...)                              \
                                                    \
    void serialize(DataStream & stream) const       \
    {                                               \
        char type = DataStream::CUSTOM;             \
        stream.write((char *)&type, sizeof(char));  \
        stream.write_args(__VA_ARGS__);             \
    }                                               \
                                                    \
    bool unserialize(DataStream & stream)           \
    {                                               \
        char type = 0;                              \
        if (!stream.read(&type, sizeof(char)) ||     \
            type != DataStream::CUSTOM)             \
        {                                           \
            return false;                           \
        }                                           \
        return stream.read_args(__VA_ARGS__);       \
    }
    
}
}
}


#endif
