#pragma once
#include <string>
#include <map>
#include <stdexcept>

// 补充statusCodes的声明（需确保全局/类内已定义）
extern const std::map<int, std::string> statusCodes;

class HTTP_RESPONSE {
public:
    int statusCode = 200; // 默认状态码，避免未初始化
    std::map<std::string, std::string> headers;
    std::string body;

    // 成员函数声明（明确可能抛出异常）
    void setBody(std::string bodyContent);
    std::string headerToString();
    std::string toString();

    // 可选：构造函数（初始化默认Header，如Content-Type）
    HTTP_RESPONSE() {
        headers["Content-Type"] = "text/plain; charset=utf-8"; // 默认Content-Type
    }
};
