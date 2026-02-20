#pragma once
#include <string>
#include <map>
#include <utility>
#include <stdexcept>

class HTTP_REQUEST {
public:
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;

    // 构造函数可能抛出runtime_error，需明确
    HTTP_REQUEST(std::string data);

    // 解析Range Header，带错误处理
    std::pair<size_t, size_t> parseRangeHeader(std::string rangeHeader, size_t fileSize);
};
