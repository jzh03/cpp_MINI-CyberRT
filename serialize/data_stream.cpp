/**
 * @File Name: data_stream.cpp
 * @brief  
 * @Author : Timer email:330070781@qq.com
 * @Version : 1.0
 * @Creat Date : 2023-11-17
 * 
 */
#include <cmw/serialize/data_stream.h>
namespace hnu{
namespace cmw{
namespace serialize{

DataStream::DataStream() : m_pos(0), m_failed(false)
{
    m_byteorder = byteorder();
}

DataStream::DataStream(const string & str) : m_pos(0), m_failed(false)
{
    m_byteorder = byteorder();
    m_buf.clear();
    reserve(str.size());
    write(str.data(), str.size());
}
DataStream::DataStream(const char* ptr, size_t size) : m_pos(0), m_failed(false)
{   
    m_byteorder = byteorder();
    m_buf.clear();  //清空vector
    reserve(size);
    write(ptr,size);
}

DataStream::~DataStream()
{
}

void DataStream::reserve(int len)
{
    int size = m_buf.size();
    int cap = m_buf.capacity();
    if (size + len > cap)
    {
        while (size + len > cap)
        {
            if (cap == 0)
            {
                cap = 1;
            }
            else
            {
                cap *= 2;
            }
        }
        m_buf.reserve(cap);
    }
}

DataStream::ByteOrder DataStream::byteorder()
{
    int n = 0x12345678;
    char str[4];
    memcpy(str, &n, sizeof(int));
    if (str[0] == 0x12)
    {
        return ByteOrder::BigEndian;
    }
    else
    {
        return ByteOrder::LittleEndian;
    }
}

bool DataStream::has_remaining(size_t size) const
{
    return m_pos >= 0 && static_cast<size_t>(m_pos) <= m_buf.size() &&
           size <= m_buf.size() - static_cast<size_t>(m_pos);
}

size_t DataStream::remaining() const
{
    if (m_pos < 0 || static_cast<size_t>(m_pos) > m_buf.size())
    {
        return 0;
    }
    return m_buf.size() - static_cast<size_t>(m_pos);
}

bool DataStream::fail(int pos)
{
    // 回退到本次读取起点，避免失败后留下错误推进的位置。
    if (pos >= 0 && static_cast<size_t>(pos) <= m_buf.size())
    {
        m_pos = pos;
    }
    m_failed = true;
    return false;
}

bool DataStream::read_type(DataType type)
{
    if (m_failed || !has_remaining(sizeof(char)) || m_buf[m_pos] != type)
    {
        return fail(m_pos);
    }
    ++m_pos;
    return true;
}

bool DataStream::read_value(DataType type, void* value, size_t size)
{
    if (m_failed || value == nullptr || !has_remaining(sizeof(char)) ||
        m_buf[m_pos] != type || size > remaining() - sizeof(char))
    {
        return fail(m_pos);
    }

    // memcpy 不要求 buffer 按目标类型对齐，可避免直接指针解引用的 UB。
    std::memcpy(value, m_buf.data() + m_pos + sizeof(char), size);
    m_pos += sizeof(char) + size;
    return true;
}

void DataStream::show() const
{
    DataStream stream(*this);
    stream.reset();
    std::cout << "data size = " << stream.size() << std::endl;

    auto read_or_throw = [&stream](auto& value, const char* error) {
        if (!stream.read(value))
        {
            throw std::logic_error(error);
        }
    };

    while (stream.m_pos < stream.size())
    {
        DataType type = static_cast<DataType>(stream.m_buf[stream.m_pos]);
        switch (type)
        {
        case DataType::BOOL:
        {
            bool value = false;
            read_or_throw(value, "parse bool error");
            std::cout << (value ? "true" : "false");
            break;
        }
        case DataType::CHAR:
        {
            char value = 0;
            read_or_throw(value, "parse char error");
            std::cout << value;
            break;
        }
        case DataType::INT32:
        {
            int32_t value = 0;
            read_or_throw(value, "parse int32 error");
            std::cout << value;
            break;
        }
        case DataType::INT64:
        {
            int64_t value = 0;
            read_or_throw(value, "parse int64 error");
            std::cout << value;
            break;
        }
        case DataType::UINT32:
        {
            uint32_t value = 0;
            read_or_throw(value, "parse uint32 error");
            std::cout << value;
            break;
        }
        case DataType::UINT64:
        {
            uint64_t value = 0;
            read_or_throw(value, "parse uint64 error");
            std::cout << value;
            break;
        }
        case DataType::FLOAT:
        {
            float value = 0;
            read_or_throw(value, "parse float error");
            std::cout << value;
            break;
        }
        case DataType::DOUBLE:
        {
            double value = 0;
            read_or_throw(value, "parse double error");
            std::cout << value;
            break;
        }
        case DataType::STRING:
        {
            string value;
            read_or_throw(value, "parse string error");
            std::cout << value;
            break;
        }
        case DataType::VECTOR:
        case DataType::LIST:
        case DataType::MAP:
        case DataType::SET:
        {
            int32_t len = 0;
            if (!stream.read_type(type) || !stream.read(len) || len < 0 ||
                static_cast<size_t>(len) > stream.remaining())
            {
                throw std::logic_error("parse container error");
            }
            break;
        }
        case DataType::CUSTOM:
        {
            char custom_type = 0;
            if (!stream.read(&custom_type, sizeof(custom_type)))
            {
                throw std::logic_error("parse custom type error");
            }
            break;
        }
        default:
            throw std::logic_error("parse data error");
        }
    }
    std::cout << std::endl;
}

void DataStream::write(const char * data, int len)
{
    reserve(len);
    int size = m_buf.size();
    m_buf.resize(m_buf.size() + len);
    std::memcpy(&m_buf[size], data, len);
}

void DataStream::write(bool value)
{
    char type = DataType::BOOL;
    write((char *)&type, sizeof(char));
    write((char *)&value, sizeof(char));
}

void DataStream::write(char value)
{
    char type = DataType::CHAR;
    write((char *)&type, sizeof(char));
    write((char *)&value, sizeof(char));
}

void DataStream::write(int32_t value)
{
    char type = DataType::INT32;
    write((char *)&type, sizeof(char));
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&value;
        char * last = first + sizeof(int32_t);
        std::reverse(first, last);
    }
    write((char *)&value, sizeof(int32_t));
}

void DataStream::write(uint32_t value)
{
    char type = DataType::UINT32;
    write((char *)&type, sizeof(char));
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&value;
        char * last = first + sizeof(uint32_t);
        std::reverse(first, last);
    }
    write((char *)&value, sizeof(uint32_t));
}

void DataStream::write(int64_t value)
{
    char type = DataType::INT64;
    write((char *)&type, sizeof(char));
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&value;
        char * last = first + sizeof(int64_t);
        std::reverse(first, last);
    }
    write((char *)&value, sizeof(int64_t));
}

void DataStream::write(uint64_t value)
{
    char type = DataType::UINT64;
    write((char*)&type ,sizeof(char));
    if(m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char*)&value;
        char * last = first + sizeof(uint64_t);
        std::reverse(first, last);
    }
    write((char*)&value, sizeof(uint64_t));
}

void DataStream::write(float value)
{
    char type = DataType::FLOAT;
    write((char *)&type, sizeof(char));
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&value;
        char * last = first + sizeof(float);
        std::reverse(first, last);
    }
    write((char *)&value, sizeof(float));
}

void DataStream::write(double value)
{
    char type = DataType::DOUBLE;
    write((char *)&type, sizeof(char));
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&value;
        char * last = first + sizeof(double);
        std::reverse(first, last);
    }
    write((char *)&value, sizeof(double));
}

void DataStream::write(const char * value)
{
    char type = DataType::STRING;
    write((char *)&type, sizeof(char));
    int len = strlen(value);
    write(len);
    write(value, len);
}

void DataStream::write(const string & value)
{
    char type = DataType::STRING;
    write((char *)&type, sizeof(char));
    int len = value.size();
    write(len);
    write(value.data(), len);
} 

void DataStream::write(const Serializable & value)
{
    value.serialize(*this);
}


void DataStream::write_args()
{
    
}

bool DataStream::read(char * data, int len)
{
    if (m_failed || len < 0 || (len > 0 && data == nullptr) ||
        !has_remaining(static_cast<size_t>(len)))
    {
        return fail(m_pos);
    }
    if (len == 0)
    {
        return true;
    }
    std::memcpy(data, m_buf.data() + m_pos, len);
    m_pos += len;
    return true;
}

bool DataStream::read(bool & value)
{
    char data = 0;
    if (!read_value(DataType::BOOL, &data, sizeof(char)))
    {
        return false;
    }
    value = data != 0;
    return true;
}

bool DataStream::read(char & value)
{
    char data = 0;
    if (!read_value(DataType::CHAR, &data, sizeof(char)))
    {
        return false;
    }
    value = data;
    return true;
}

bool DataStream::read(int32_t & value)
{
    int32_t data = 0;
    if (!read_value(DataType::INT32, &data, sizeof(int32_t)))
    {
        return false;
    }
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&data;
        char * last = first + sizeof(int32_t);
        std::reverse(first, last);
    }
    value = data;
    return true;
}

bool DataStream::read(uint32_t & value)
{
    uint32_t data = 0;
    if (!read_value(DataType::UINT32, &data, sizeof(uint32_t)))
    {
        return false;
    }
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&data;
        char * last = first + sizeof(uint32_t);
        std::reverse(first, last);
    }
    value = data;
    return true;
}

bool DataStream::read(int64_t & value)
{
    int64_t data = 0;
    if (!read_value(DataType::INT64, &data, sizeof(int64_t)))
    {
        return false;
    }
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&data;
        char * last = first + sizeof(int64_t);
        std::reverse(first, last);
    }
    value = data;
    return true;
}

bool DataStream::read(uint64_t & value)
{
    uint64_t data = 0;
    if (!read_value(DataType::UINT64, &data, sizeof(uint64_t)))
    {
        return false;
    }
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&data;
        char * last = first + sizeof(uint64_t);
        std::reverse(first, last);
    }
    value = data;
    return true;
}

bool DataStream::read(float & value)
{
    float data = 0;
    if (!read_value(DataType::FLOAT, &data, sizeof(float)))
    {
        return false;
    }
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&data;
        char * last = first + sizeof(float);
        std::reverse(first, last);
    }
    value = data;
    return true;
}

bool DataStream::read(double & value)
{
    double data = 0;
    if (!read_value(DataType::DOUBLE, &data, sizeof(double)))
    {
        return false;
    }
    if (m_byteorder == ByteOrder::BigEndian)
    {
        char * first = (char *)&data;
        char * last = first + sizeof(double);
        std::reverse(first, last);
    }
    value = data;
    return true;
}

bool DataStream::read(string & value)
{
    int start = m_pos;
    if (!read_type(DataType::STRING))
    {
        return fail(start);
    }

    // 先验证完整长度，再写入临时对象，防止越界和异常大内存申请。
    string result;
    int32_t len = 0;
    if (!read(len) || len < 0 || static_cast<size_t>(len) > remaining() ||
        static_cast<size_t>(len) > result.max_size())
    {
        return fail(start);
    }

    if (len > 0)
    {
        result.assign(m_buf.data() + m_pos, len);
    }
    m_pos += len;
    value.swap(result);
    return true;
}

bool DataStream::read(Serializable & value)
{
    int start = m_pos;
    if (m_failed || !value.unserialize(*this) || m_failed)
    {
        return fail(start);
    }
    return true;
}


bool DataStream::read_args()
{
    return !m_failed;
}

const char * DataStream::data() const
{
    return m_buf.data();
}

int DataStream::size() const
{
    return m_buf.size();
}

void DataStream::clear()
{
    m_buf.clear();
    m_pos = 0;
    m_failed = false;
}

void DataStream::reset()
{
    m_pos = 0;
    m_failed = false;
}

//返回序列化后的数据占用内存的字节数
size_t DataStream::ByteSize()
{
    return sizeof(char) * m_buf.size();
}

void DataStream::save(const string & filename)
{
    ofstream fout(filename);
    fout.write(data(), size());
    fout.flush();
    fout.close();
}

void DataStream::load(const string & filename)
{
    ifstream fin(filename);
    stringstream ss;
    ss << fin.rdbuf();
    const string & str = ss.str();
    m_buf.clear();
    m_pos = 0;
    m_failed = false;
    reserve(str.size());
    write(str.data(), str.size());
}

DataStream & DataStream::operator << (bool value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (char value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (int32_t value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (int64_t value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (uint32_t value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (uint64_t value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (float value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (double value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (const char * value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (const string & value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator << (const Serializable & value)
{
    write(value);
    return *this;
}

DataStream & DataStream::operator >> (bool & value)
{
    read(value);
    return *this;
}

DataStream & DataStream::operator >> (char & value)
{
    read(value);
    return *this;
}

DataStream & DataStream::operator >> (int32_t & value)
{
    read(value);
    return *this;
}

DataStream & DataStream::operator >> (int64_t & value)
{
    read(value);
    return *this;
}
DataStream & DataStream::operator >> (uint32_t & value)
{
    read(value);
    return *this;
}

DataStream & DataStream::operator >> (uint64_t & value)
{
    read(value);
    return *this;
}

DataStream & DataStream::operator >> (float & value)
{
    read(value);
    return *this;
}

DataStream & DataStream::operator >> (double & value)
{
    read(value);
    return *this; 
}

DataStream & DataStream::operator >> (string & value)
{
    read(value);
    return *this; 
}

DataStream & DataStream::operator >> (Serializable & value)
{
    read(value);
    return *this;
}

}
}
}
