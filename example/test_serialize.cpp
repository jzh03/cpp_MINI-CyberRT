#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <cmw/serialize/data_stream.h>

using namespace hnu::cmw::serialize;

template <typename T>
bool VectorRoundTrip(const std::vector<T>& expected)
{
    DataStream stream;
    stream << expected;

    std::vector<T> actual;
    return stream.read(actual) && actual == expected;
}

bool DataStreamTest_vector_round_trip()
{
    // 覆盖三类元素的空 vector 和正常多元素 vector。
    return VectorRoundTrip(std::vector<int>()) &&
           VectorRoundTrip(std::vector<int>{-7, 0, 42}) &&
           VectorRoundTrip(std::vector<double>()) &&
           VectorRoundTrip(std::vector<double>{-1.5, 0.0, 3.25}) &&
           VectorRoundTrip(std::vector<std::string>()) &&
           VectorRoundTrip(std::vector<std::string>{"", "CyberRT", "vector"});
}

bool DataStreamTest_truncated_payload()
{
    // 截断基础类型 payload 后，读取应失败且目标值保持不变。
    DataStream encoded;
    encoded.write(static_cast<int64_t>(123456789));
    if (encoded.size() <= 1)
    {
        return false;
    }

    DataStream truncated(encoded.data(), encoded.size() - 1);
    int64_t value = 7;
    if (truncated.read(value) || value != 7)
    {
        return false;
    }

    char next = 0;
    return !truncated.read(next);
}

bool DataStreamTest_invalid_string_length()
{
    // 声明长度超过剩余数据时，不应越界读取或修改原字符串。
    DataStream stream;
    char type = DataStream::STRING;
    stream.write(&type, sizeof(type));
    stream.write(static_cast<int32_t>(32));
    stream.write("abc", 3);

    std::string value = "unchanged";
    return !stream.read(value) && value == "unchanged";
}

bool DataStreamTest_invalid_vector_length()
{
    // 明显非法的元素数量不应触发异常大内存申请。
    DataStream stream;
    char type = DataStream::VECTOR;
    stream.write(&type, sizeof(type));
    stream.write(std::numeric_limits<int32_t>::max());
    stream.write(static_cast<int32_t>(17));

    std::vector<int> value{5};
    if (stream.read(value) || value != std::vector<int>{5})
    {
        return false;
    }

    int32_t trailing_value = 0;
    return !stream.read(trailing_value);
}

int main()
{
    try
    {
        bool vector_passed = DataStreamTest_vector_round_trip();
        bool truncated_passed = DataStreamTest_truncated_payload();
        bool string_length_passed = DataStreamTest_invalid_string_length();
        bool vector_length_passed = DataStreamTest_invalid_vector_length();

        std::cout << "Vector round-trip: "
                  << (vector_passed ? "PASS" : "FAIL") << std::endl;
        std::cout << "Truncated payload: "
                  << (truncated_passed ? "PASS" : "FAIL") << std::endl;
        std::cout << "Invalid string length: "
                  << (string_length_passed ? "PASS" : "FAIL") << std::endl;
        std::cout << "Invalid vector length: "
                  << (vector_length_passed ? "PASS" : "FAIL") << std::endl;

        return vector_passed && truncated_passed && string_length_passed &&
                       vector_length_passed
                   ? 0
                   : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Serialization test exception: " << error.what() << std::endl;
        return 1;
    }
}
