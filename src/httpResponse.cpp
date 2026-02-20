// Copyright (C) 2025 Langning Chen
//
// This file is part of paper.
//
// paper is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// paper is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with paper.  If not, see <https://www.gnu.org/licenses/>.

#include "httpResponse.hpp"
#include "io.hpp"
#include "i18n.hpp"
#include <sstream>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <cctype>

// 新增：工具函数 - 检查字符串是否为空（含全空格）
static bool isEmptyOrWhitespace(const std::string& s) {
    return s.empty() || std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isspace(c);
    });
}

// 新增：工具函数 - 安全转换字符串（捕获可能的异常）
template <typename T>
static std::string safe_to_string(T value, const std::string& errMsg) {
    try {
        return std::to_string(value);
    } catch (const std::exception& e) {
        std::string fullErr = errMsg + ": " + e.what();
        IO::Error(fullErr);
        throw std::runtime_error(fullErr);
    } catch (...) {
        std::string fullErr = errMsg + ": unknown error";
        IO::Error(fullErr);
        throw std::runtime_error(fullErr);
    }
}

void HTTP_RESPONSE::setBody(std::string bodyContent)
{
    // 新增：空body日志提示
    if (bodyContent.empty()) {
        IO::Debug(t("response_body_is_empty") + ", Content-Length will be set to 0");
    } else {
        IO::Debug(t("setting_response_body") + " (" + safe_to_string(bodyContent.length(), t("failed_convert_body_length")) + " " + t("bytes") + ")");
    }

    // 检查body长度是否超出size_t范围（极端场景防护）
    if (bodyContent.length() > std::numeric_limits<size_t>::max()) {
        std::string errMsg = t("body_length_exceeds_max_limit") + ": " + std::to_string(bodyContent.length());
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    body = bodyContent;
    // 修复：安全设置Content-Length
    try {
        headers["Content-Length"] = safe_to_string(body.length(), t("failed_set_content_length"));
        IO::Debug(t("content_length_set") + ": " + headers["Content-Length"]);
    } catch (const std::runtime_error& e) {
        IO::Error(t("failed_update_content_length") + ": " + e.what());
        throw; // 向上抛异常，阻断响应构建
    }
}

std::string HTTP_RESPONSE::headerToString()
{
    IO::Debug(t("generating_response_header"));
    
    // 修复1：检查statusCode是否有效（防护无效状态码）
    std::string statusText;
    try {
        statusText = statusCodes.at(statusCode);
    } catch (const std::out_of_range& e) {
        std::string errMsg = t("invalid_http_status_code") + ": " + safe_to_string(statusCode, t("failed_convert_status_code"));
        IO::Error(errMsg);
        // 降级处理：使用默认状态文本，避免响应完全失败
        statusText = "Bad Request";
        statusCode = 400; // 重置为合法状态码
        IO::Warn(t("fallback_to_default_status") + ": 400 Bad Request");
    }

    IO::Debug(t("response_status_code") + ": " + safe_to_string(statusCode, t("failed_convert_status_code")) + " " + statusText);

    // 修复2：高效拼接Header（先用ostringstream）
    std::ostringstream headerStream;
    headerStream << "HTTP/1.1 "
                 << safe_to_string(statusCode, t("failed_convert_status_code"))
                 << " " << statusText << "\r\n";

    // 修复3：检查Header的Key/Value合法性
    for (const auto& [key, value] : headers) {
        if (isEmptyOrWhitespace(key)) {
            std::string errMsg = t("ignored_invalid_header_key") + ": empty or whitespace only";
            IO::Warn(errMsg);
            continue;
        }
        if (isEmptyOrWhitespace(value)) {
            IO::Warn(t("header_value_is_empty_or_whitespace") + ": " + key);
        }
        headerStream << key << ": " << value << "\r\n";
        IO::Debug(t("adding_response_header") + ": " + key + " = " + value);
    }

    std::string header = headerStream.str();
    IO::Debug(t("response_header_generated") + " (" + safe_to_string(header.length(), t("failed_convert_header_length")) + " " + t("bytes") + ")");
    return header;
}

std::string HTTP_RESPONSE::toString()
{
    IO::Debug(t("generating_full_response"));
    
    // 修复：先检查Header生成是否成功（捕获异常）
    std::string header;
    try {
        header = headerToString();
    } catch (const std::runtime_error& e) {
        std::string errMsg = t("failed_generate_response_header") + ": " + e.what();
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    // 拼接完整响应（Header + 空行 + Body）
    std::ostringstream fullResponseStream;
    fullResponseStream << header << "\r\n" << body;
    std::string fullResponse = fullResponseStream.str();

    // 检查响应总长度（极端场景防护）
    if (fullResponse.length() > std::numeric_limits<size_t>::max()) {
        std::string errMsg = t("full_response_length_exceeds_max_limit") + ": " + std::to_string(fullResponse.length());
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("response_size") + ": " + safe_to_string(fullResponse.length(), t("failed_convert_response_length")) + " " + t("bytes"));
    return fullResponse;
}
