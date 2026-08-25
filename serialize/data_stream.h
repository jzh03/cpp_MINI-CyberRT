#ifndef CMW_SERIALIZE_DATA_STREADM_H_
#define CMW_SERIALIZE_DATA_STREADM_H_

#include <vector>
#include <iostream>
#include <string>
#include <stdexcept>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <limits>
#include <cmw/serialize/serializable.h>
#include <cmw/common/log.h>
#include <cmw/time/time.h>

using namespace std;
using std::ifstream;
using std::ofstream;
using std::stringstream;
namespace hnu{
namespace cmw{
namespace serialize{

class DataStream
{
public:
    enum DataType
    {
        BOOL = 0,  
        CHAR,
        INT32,
        INT64,
        UINT32,
        UINT64,
        FLOAT,
        DOUBLE,
        ENUM,
        STRING,
        VECTOR,
        LIST,
        MAP,
        SET,
        CUSTOM
    };

    enum ByteOrder
    {
        BigEndian,
        LittleEndian
    };

    DataStream();
    DataStream(const string & data);
    DataStream(const char* ptr, size_t size);
    ~DataStream();

    void show() const;
    void write(const char * data, int len);
    void write(bool value);
    void write(char value);
    void write(int32_t value);
    void write(uint32_t value);
    void write(uint64_t value);
    void write(int64_t value);
    void write(float value);
    void write(double value);
    void write(const char * value);
    void write(const string & value);
    void write(const Serializable & value);
 
    template <typename T>
    void write(const std::vector<T> & value);

    template <typename T>
    void write(const std::list<T> & value);

    template <typename K, typename V>
    void write(const std::map<K, V> & value);

    template <typename T>
    void write(const std::set<T> & value);

    //采用SFINAE特性保证T为模板类型
    template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
    void write(const T& value);

    template <typename T, typename ...Args>
    void write_args(const T & head, const Args&... args);

    void write_args();

    bool read(char * data, int len);
    bool read(bool & value);
    bool read(char & value);
    bool read(int32_t & value);
    bool read(uint32_t& value);
    bool read(uint64_t& value);
    bool read(int64_t & value);
    bool read(float & value);
    bool read(double & value);
    bool read(string & value);
    bool read(Serializable & value);

    template <typename T>
    bool read(std::vector<T> & value);

    template <typename T>
    bool read(std::list<T> & value);

    template <typename K, typename V>
    bool read(std::map<K, V> & value);

    template <typename T>
    bool read(std::set<T> & value);

    //采用SFINAE特性保证T为枚举类型
    template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
    bool read(T& value);

    template <typename T, typename ...Args>
    bool read_args(T & head, Args&... args);

    bool read_args();

    const char * data() const;
    int size() const;
    size_t ByteSize();
    void clear();
    void reset();
    void save(const string & filename);
    void load(const string & filename);

    DataStream & operator << (bool value);
    DataStream & operator << (char value);
    DataStream & operator << (int32_t value);
    DataStream & operator << (int64_t value);
    DataStream & operator << (uint32_t value);
    DataStream & operator << (uint64_t value);
    DataStream & operator << (float value);
    DataStream & operator << (double value);
    DataStream & operator << (const char * value);
    DataStream & operator << (const string & value);
    DataStream & operator << (const Serializable & value);

    template <typename T>
    DataStream & operator << (const std::vector<T> & value);

    template <typename T>
    DataStream & operator << (const std::list<T> & value);

    template <typename K, typename V>
    DataStream & operator << (const std::map<K, V> & value);

    template <typename T>
    DataStream & operator << (const std::set<T> & value);

    DataStream & operator >> (bool & value);
    DataStream & operator >> (char & value);
    DataStream & operator >> (int32_t & value);
    DataStream & operator >> (int64_t & value);
    DataStream & operator >> (uint32_t & value);
    DataStream & operator >> (uint64_t & value);
    DataStream & operator >> (float & value);
    DataStream & operator >> (double & value);
    DataStream & operator >> (string & value);
    DataStream & operator >> (Serializable & value);

    template <typename T>
    DataStream & operator >> (std::vector<T> & value);

    template <typename T>
    DataStream & operator >> (std::list<T> & value);

    template <typename K, typename V>
    DataStream & operator >> (std::map<K, V> & value);

    template <typename T>
    DataStream & operator >> (std::set<T> & value);

private:
    void reserve(int len);
    ByteOrder byteorder();
    // 以下辅助函数统一完成读取边界检查，并在失败后保持失败状态。
    bool read_type(DataType type);
    bool read_value(DataType type, void* value, size_t size);
    bool has_remaining(size_t size) const;
    size_t remaining() const;
    bool fail(int pos);

private:
    std::vector<char> m_buf;
    int m_pos;
    ByteOrder m_byteorder;
    // 失败状态具有粘性，reset/clear/load 后才允许重新读取。
    bool m_failed;
};

template <typename T>
void DataStream::write(const std::vector<T> & value)
{
    if (value.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    {
        throw std::length_error("vector is too large to serialize");
    }

    char type = DataType::VECTOR;
    write((char *)&type, sizeof(char));
    // 长度字段表示元素数量，每个元素继续使用既有格式序列化。
    int32_t len = static_cast<int32_t>(value.size());
    write(len);
    for (size_t i = 0; i < value.size(); ++i)
    {
        write(value[i]);
    }
}

template <typename T>
void DataStream::write(const std::list<T> & value)
{
    char type = DataType::LIST;
    write((char *)&type, sizeof(char));
    int len = value.size();
    write(len);
    for (auto it = value.begin(); it != value.end(); it++)
    {
        write((*it));
    }
}

template <typename K, typename V>
void DataStream::write(const std::map<K, V> & value)
{
    char type = DataType::MAP;
    write((char *)&type, sizeof(char));
    int len = value.size();
    write(len);
    for (auto it = value.begin(); it != value.end(); it++)
    {
        write(it->first);
        write(it->second);
    }
}

template <typename T>
void DataStream::write(const std::set<T> & value)
{
    char type = DataType::SET;
    write((char *)&type, sizeof(char));
    int len = value.size();
    write(len);
    for (auto it = value.begin(); it != value.end(); it++)
    {
        write(*it);
    }
}

template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
void DataStream::write(const T& value) {
    write(static_cast<int32_t>(value));
}

template <typename T, typename ...Args>
void DataStream::write_args(const T & head, const Args&... args)
{
    // if constexpr (std::is_enum_v<T>){
    //     int32_t intValue = static_cast<int32_t>(head);
    //     write(intValue);
    // }else{
    //     write(head);
    // }
    write(head);
    write_args(args...);
}

template <typename T>
bool DataStream::read(std::vector<T> & value)
{
    int start = m_pos;
    if (!read_type(DataType::VECTOR))
    {
        return fail(start);
    }

    // 使用临时容器，读取失败时保留调用方原有内容。
    std::vector<T> result;
    int32_t len = 0;
    if (!read(len) || len < 0 || static_cast<size_t>(len) > remaining() ||
        static_cast<size_t>(len) > result.max_size())
    {
        return fail(start);
    }

    // 不按不可信长度预分配内存，而是逐元素校验并读取。
    for (int32_t i = 0; i < len; ++i)
    {
        T element;
        if (!read(element))
        {
            return fail(start);
        }
        result.push_back(element);
    }
    value.swap(result);
    return true;
}

template <typename T>
bool DataStream::read(std::list<T> & value)
{
    int start = m_pos;
    if (!read_type(DataType::LIST))
    {
        return fail(start);
    }

    int32_t len = 0;
    if (!read(len) || len < 0 || static_cast<size_t>(len) > remaining())
    {
        return fail(start);
    }

    std::list<T> result;
    for (int32_t i = 0; i < len; ++i)
    {
        T v;
        if (!read(v))
        {
            return fail(start);
        }
        result.push_back(v);
    }
    value.swap(result);
    return true;
}

template <typename K, typename V>
bool DataStream::read(std::map<K, V> & value)
{
    int start = m_pos;
    if (!read_type(DataType::MAP))
    {
        return fail(start);
    }

    int32_t len = 0;
    if (!read(len) || len < 0 || static_cast<size_t>(len) > remaining())
    {
        return fail(start);
    }

    std::map<K, V> result;
    for (int32_t i = 0; i < len; ++i)
    {
        K k;
        if (!read(k))
        {
            return fail(start);
        }

        V v;
        if (!read(v))
        {
            return fail(start);
        }
        result[k] = v;
    }
    value.swap(result);
    return true;
}

template <typename T>
bool DataStream::read(std::set<T> & value)
{
    int start = m_pos;
    if (!read_type(DataType::SET))
    {
        return fail(start);
    }

    int32_t len = 0;
    if (!read(len) || len < 0 || static_cast<size_t>(len) > remaining())
    {
        return fail(start);
    }

    std::set<T> result;
    for (int32_t i = 0; i < len; ++i)
    {
        T v;
        if (!read(v))
        {
            return fail(start);
        }
        result.insert(v);
    }
    value.swap(result);
    return true;
}

template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
bool DataStream::read(T& value)
{
    int32_t int_value = 0;
    if (!read(int_value))
    {
        return false;
    }
    value = static_cast<T>(int_value);
    return true;
}

template <typename T, typename ...Args>
bool DataStream::read_args(T & head, Args&... args)
{
    int start = m_pos;
    if (!read(head) || !read_args(args...))
    {
        return fail(start);
    }
    return true;
}

template <typename T>
DataStream & DataStream::operator << (const std::vector<T> & value)
{
    write(value);
    return *this;
}

template <typename K, typename V>
DataStream & DataStream::operator << (const std::map<K, V> & value)
{
    write(value);
    return *this;
}

template <typename T>
DataStream & DataStream::operator << (const std::set<T> & value)
{
    write(value);
    return *this;
}

template <typename T>
DataStream & DataStream::operator >> (std::vector<T> & value)
{
    read(value);
    return *this;
}

template <typename T>
DataStream & DataStream::operator >> (std::list<T> & value)
{
    read(value);
    return *this;
}

template <typename K, typename V>
DataStream & DataStream::operator >> (std::map<K, V> & value)
{
    read(value);
    return *this;
}

template <typename T>
DataStream & DataStream::operator >> (std::set<T> & value)
{
    read(value);
    return *this;
}


}
}
}


#endif
