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

#include "httpServer.hpp"
#include <winsock2.h>
#include "io.hpp"
#include "define.hpp"
#include "i18n.hpp"
#include "httpRequest.hpp" // 补充HTTP_REQUEST解析头文件
#include "httpResponse.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <filesystem>
#include <thread>
#include <map>
#include <ws2tcpip.h>
#include <stdexcept>
#include <limits>
#include <algorithm>

// 定义BUFFER_SIZE（类静态成员初始化）
const int HTTP_SERVER::BUFFER_SIZE = 4096; // 补充默认值，避免未定义

// 新增：工具函数 - 安全转换字符串（捕获异常）
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

// 新增：工具函数 - 关闭Socket并清理资源
static void safe_closesocket(SOCKET s, const std::string& context) {
    if (s != INVALID_SOCKET) {
        if (closesocket(s) == SOCKET_ERROR) {
            IO::Error(context + " - " + t("failed_close_socket") + ": " + safe_to_string(WSAGetLastError(), t("failed_convert_error_code")));
        } else {
            IO::Debug(context + " - " + t("socket_closed_successfully"));
        }
    }
}

// 新增：工具函数 - 设置Socket超时（recv/send超时）
static bool set_socket_timeout(SOCKET s, int timeout_ms) {
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
        IO::Error(t("failed_set_recv_timeout") + ": " + safe_to_string(WSAGetLastError(), t("failed_convert_error_code")));
        return false;
    }
    if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
        IO::Error(t("failed_set_send_timeout") + ": " + safe_to_string(WSAGetLastError(), t("failed_convert_error_code")));
        return false;
    }
    return true;
}

HTTP_SERVER::HTTP_SERVER(int port, std::string imagePath, std::string otaData, std::string otaUrl)
    : serverPort(port), imagePath(imagePath), otaData(otaData), otaUrl(otaUrl), isRunning(false), serverSocket(INVALID_SOCKET)
{
    // 校验入参合法性
    if (port < 1 || port > 65535) {
        throw std::invalid_argument(t("invalid_port_number") + ": " + std::to_string(port));
    }
    if (imagePath.empty()) {
        IO::Warn(t("image_path_is_empty"));
    }
}

void HTTP_SERVER::start()
{
    // 双重检查，避免重复启动
    if (isRunning) {
        IO::Warn(t("server_already_running"));
        return;
    }

    IO::Debug(t("initializing_winsock"));
    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0) {
        std::string errMsg = t("wsa_startup_failed") + ": " + safe_to_string(wsaErr, t("failed_convert_error_code"));
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("creating_server_socket"));
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        int errCode = WSAGetLastError();
        std::string errMsg = t("failed_create_socket") + ": " + safe_to_string(errCode, t("failed_convert_error_code"));
        IO::Error(errMsg);
        WSACleanup();
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("setting_socket_options"));
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == SOCKET_ERROR) {
        int errCode = WSAGetLastError();
        IO::Warn(t("failed_set_reuseaddr") + ": " + safe_to_string(errCode, t("failed_convert_error_code")));
        // 非致命错误，继续执行
    }

    // 设置服务端Socket超时（避免accept阻塞卡死）
    if (!set_socket_timeout(serverSocket, 5000)) { // 5秒超时
        IO::Warn(t("failed_set_server_socket_timeout"));
    }

    IO::Debug(t("binding_to_port") + " " + safe_to_string(serverPort, t("failed_convert_port")) + "...");
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(serverPort);

    if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        int errCode = WSAGetLastError();
        std::string errMsg = t("failed_bind_socket") + ": " + safe_to_string(errCode, t("failed_convert_error_code"));
        IO::Error(errMsg);
        safe_closesocket(serverSocket, t("bind_failure_cleanup"));
        WSACleanup();
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("starting_listen"));
    if (listen(serverSocket, 16) == SOCKET_ERROR) {
        int errCode = WSAGetLastError();
        std::string errMsg = t("failed_listen_socket") + ": " + safe_to_string(errCode, t("failed_convert_error_code"));
        IO::Error(errMsg);
        safe_closesocket(serverSocket, t("listen_failure_cleanup"));
        WSACleanup();
        throw std::runtime_error(errMsg);
    }

    isRunning = true;
    IO::Info(t("server_started_press_x"));
    IO::Debug(t("server_listening") + " " + safe_to_string(serverPort, t("failed_convert_port")));

    try {
        serverThread = std::thread(
            [this]()
            {
                while (isRunning)
                {
                    sockaddr_in clientAddr;
                    int clientAddrLen = sizeof(clientAddr);
                    SOCKET clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &clientAddrLen);
                    if (clientSocket == INVALID_SOCKET)
                    {
                        int errCode = WSAGetLastError();
                        if (isRunning) {
                            // WSAEWOULDBLOCK 是超时正常返回，非错误
                            if (errCode != WSAEWOULDBLOCK) {
                                IO::Warn(t("failed_accept_connection") + ": " + safe_to_string(errCode, t("failed_convert_error_code")));
                            }
                        }
                        continue;
                    }

                    IO::Debug(t("new_client_connected"));
                    // 设置客户端Socket超时（5秒）
                    if (!set_socket_timeout(clientSocket, 5000)) {
                        IO::Warn(t("failed_set_client_socket_timeout"));
                    }

                    // 子线程捕获所有异常，避免进程崩溃
                    std::thread(
                        [this, clientSocket]()
                        {
                            try {
                                char buffer[BUFFER_SIZE];
                                // 清空缓冲区，避免脏数据
                                memset(buffer, 0, sizeof(buffer));
                                int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
                                if (bytesReceived > 0)
                                {
                                    buffer[bytesReceived] = '\0';
                                    std::string request(buffer);
                                    IO::Debug(t("received_request_bytes") + " " + safe_to_string(bytesReceived, t("failed_convert_byte_count")) + " " + t("bytes"));
                                    
                                    // 修复核心问题：解析std::string为HTTP_REQUEST对象
                                    HTTP_REQUEST parsedRequest;
                                    try {
                                        parsedRequest.parse(request); // 假设HTTP_REQUEST有parse方法解析请求字符串
                                        handleRequest(clientSocket, parsedRequest);
                                    } catch (const std::exception& e) {
                                        IO::Error(t("failed_parse_http_request") + ": " + e.what());
                                        // 返回400错误响应
                                        sendHttpResponse(clientSocket, 400, "text/plain", t("bad_request_invalid_format"));
                                    }
                                }
                                else if (bytesReceived == 0)
                                {
                                    IO::Debug(t("client_closed_connection"));
                                }
                                else
                                {
                                    int errCode = WSAGetLastError();
                                    IO::Error(t("failed_receive_data") + ": " + safe_to_string(errCode, t("failed_convert_error_code")));
                                }
                            } catch (const std::exception& e) {
                                IO::Error(t("client_thread_exception") + ": " + e.what());
                            } catch (...) {
                                IO::Error(t("client_thread_unknown_exception"));
                            }
                            // 安全关闭客户端Socket
                            safe_closesocket(clientSocket, t("client_connection_cleanup"));
                            IO::Debug(t("client_connection_closed"));
                        })
                        .detach();
                }
            });
    } catch (const std::exception& e) {
        IO::Error(t("failed_create_server_thread") + ": " + e.what());
        stop();
        throw;
    }
}

void HTTP_SERVER::stop()
{
    IO::Info(t("stopping_http_server"));
    isRunning = false;

    // 先关闭监听Socket，唤醒accept阻塞
    if (serverSocket != INVALID_SOCKET) {
        safe_closesocket(serverSocket, t("server_socket_cleanup"));
        serverSocket = INVALID_SOCKET;
    }

    // 等待服务线程结束（捕获join异常）
    if (serverThread.joinable()) {
        try {
            serverThread.join();
        } catch (const std::exception& e) {
            IO::Error(t("failed_join_server_thread") + ": " + e.what());
        }
    }

    // 清理Winsock
    if (WSACleanup() == SOCKET_ERROR) {
        int errCode = WSAGetLastError();
        IO::Error(t("failed_cleanup_winsock") + ": " + safe_to_string(errCode, t("failed_convert_error_code")));
    }

    IO::Info(t("http_server_stopped_successfully"));
}

void HTTP_SERVER::handleRequest(SOCKET clientSocket, HTTP_REQUEST request)
{
    try {
        IO::Debug(t("processing_http_request") + ": " + request.path);
        IO::Debug(t("http_method") + ": " + request.method);

        // 路径长度校验，避免越界
        if (request.path.length() > std::numeric_limits<size_t>::max() - 10) {
            IO::Error(t("request_path_too_long"));
            sendHttpResponse(clientSocket, 414, "text/plain", t("uri_too_long"));
            return;
        }

        if (request.path.substr(0, 10) == "/image.img")
        {
            IO::Info(t("serving_image_file"));
            sendFileResponse(clientSocket, request.headers);
        }
        else if (request.path.length() > 10 && request.path.substr(0, 10) == "/register/")
        {
            IO::Info(t("serving_register_data"));
            sendHttpResponse(clientSocket, 200, "application/json;charset=UTF-8", 
                R"({"status":1000,"msg":"success","data":{"deviceSecret":"de8b9bcd0a18afbf25b44f6d4f6c5f23","sha256":"8a6860050ac879171800a8315fc516b46d6baf81f73910ab1ab5d7e9059d427f","deviceId":"f730c7fa72bd3871"}})");
        }
        else if (request.path == otaUrl + "/checkVersion" && request.method == "POST")
        {
            IO::Info(t("serving_ota_data"));
            sendHttpResponse(clientSocket, 200, "application/json;charset=UTF-8", otaData);
        }
        else if (request.path == otaUrl + "/reportDownResult" && request.method == "POST")
        {
            IO::Info(t("serving_ota_report"));
            sendHttpResponse(clientSocket, 200, "application/json;charset=UTF-8", 
                R"({"status":1000,"msg":"success","data":null})");
        }
        else
        {
            IO::Info(t("request_not_found_404") + ": " + request.path);
            sendHttpResponse(clientSocket, 404, "text/plain", t("file_not_found"));
        }
    } catch (const std::exception& e) {
        IO::Error(t("failed_handle_request") + ": " + e.what());
        // 返回500内部错误
        sendHttpResponse(clientSocket, 500, "text/plain", t("internal_server_error"));
    }
}

void HTTP_SERVER::sendHttpResponse(SOCKET clientSocket, int statusCode, std::string contentType,
                                   std::string body, std::string extraHeaders)
{
    try {
        HTTP_RESPONSE response;
        response.statusCode = statusCode;
        response.headers["Content-Type"] = contentType;

        // 解析额外Header（如需要）
        if (!extraHeaders.empty()) {
            std::istringstream iss(extraHeaders);
            std::string line;
            while (std::getline(iss, line)) {
                size_t sep = line.find(": ");
                if (sep != std::string::npos) {
                    std::string key = line.substr(0, sep);
                    std::string value = line.substr(sep + 2);
                    if (!key.empty()) {
                        response.headers[key] = value;
                    }
                }
            }
        }

        // 捕获setBody异常
        response.setBody(body);
        std::string responseString = response.toString();

        // 分段发送（避免大响应体一次性发送失败）
        const char* respPtr = responseString.c_str();
        size_t totalLen = responseString.length();
        size_t sentLen = 0;
        while (sentLen < totalLen) {
            int sent = send(clientSocket, respPtr + sentLen, totalLen - sentLen, 0);
            if (sent == SOCKET_ERROR) {
                int errCode = WSAGetLastError();
                std::string errMsg = t("failed_send_response") + ": " + safe_to_string(errCode, t("failed_convert_error_code"));
                IO::Error(errMsg);
                throw std::runtime_error(errMsg);
            }
            sentLen += sent;
        }

        IO::Debug(t("sent_http_response") + ": " + safe_to_string(response.statusCode, t("failed_convert_status_code")));
    } catch (const std::exception& e) {
        IO::Error(t("failed_build_send_response") + ": " + e.what());
        // 尝试发送极简错误响应（避免客户端挂起）
        std::string errResp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error";
        send(clientSocket, errResp.c_str(), errResp.length(), 0);
    }
}

void HTTP_SERVER::sendFileResponse(SOCKET clientSocket, std::map<std::string, std::string> headers)
{
    std::ifstream file;
    try {
        IO::Debug(t("preparing_file_response") + ": " + imagePath);

        // 检查文件是否存在
        if (!std::filesystem::exists(imagePath)) {
            std::string errMsg = t("file_not_exist") + ": " + imagePath;
            IO::Error(errMsg);
            sendHttpResponse(clientSocket, 404, "text/plain", t("file_not_found"));
            return;
        }

        // 检查文件是否是普通文件（非目录/链接）
        if (!std::filesystem::is_regular_file(imagePath)) {
            std::string errMsg = t("not_regular_file") + ": " + imagePath;
            IO::Error(errMsg);
            sendHttpResponse(clientSocket, 403, "text/plain", t("forbidden_not_regular_file"));
            return;
        }

        // 安全获取文件大小
        size_t fileSize;
        try {
            fileSize = std::filesystem::file_size(imagePath);
        } catch (const std::filesystem::filesystem_error& e) {
            std::string errMsg = t("failed_get_file_size") + ": " + imagePath + " - " + e.what();
            IO::Error(errMsg);
            sendHttpResponse(clientSocket, 500, "text/plain", t("internal_server_error"));
            return;
        }
        IO::Debug(t("file_size") + ": " + safe_to_string(fileSize, t("failed_convert_file_size")) + " " + t("bytes"));

        // 打开文件（二进制模式）
        file.open(imagePath, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            std::string errMsg = t("failed_open_file") + ": " + imagePath + " - " + safe_to_string(errno, t("failed_convert_errno"));
            IO::Error(errMsg);
            sendHttpResponse(clientSocket, 500, "text/plain", t("internal_server_error"));
            return;
        }

        HTTP_RESPONSE responseHeader;
        size_t startPos = 0;
        size_t endPos = fileSize - 1;
        size_t contentLength = fileSize;
        responseHeader.statusCode = 200;

        // 处理Range请求
        auto rangeIt = headers.find("Range");
        if (rangeIt != headers.end() && !rangeIt->second.empty())
        {
            IO::Debug(t("processing_range_request") + ": " + rangeIt->second);
            try {
                auto range = HTTP_REQUEST::parseRangeHeader(rangeIt->second, fileSize);
                startPos = range.first;
                endPos = range.second;

                // 校验Range合法性
                if (startPos > endPos || endPos >= fileSize) {
                    IO::Warn(t("invalid_range_request") + ": " + rangeIt->second);
                    // 返回416 Range Not Satisfiable
                    sendHttpResponse(clientSocket, 416, "text/plain", t("range_not_satisfiable"));
                    file.close();
                    return;
                }

                contentLength = endPos - startPos + 1;
                IO::Debug(t("range") + ": " + safe_to_string(startPos, t("failed_convert_start_pos")) 
                    + "-" + safe_to_string(endPos, t("failed_convert_end_pos")) 
                    + " (" + t("length") + ": " + safe_to_string(contentLength, t("failed_convert_content_length")) + ")");

                responseHeader.statusCode = 206;
                responseHeader.headers["Content-Range"] = "bytes " 
                    + safe_to_string(startPos, t("failed_convert_start_pos")) 
                    + "-" + safe_to_string(endPos, t("failed_convert_end_pos")) 
                    + "/" + safe_to_string(fileSize, t("failed_convert_file_size"));
            } catch (const std::exception& e) {
                IO::Error(t("failed_parse_range_header") + ": " + e.what());
                // 降级为完整文件发送
                IO::Warn(t("fallback_to_full_file_send"));
                startPos = 0;
                endPos = fileSize - 1;
                contentLength = fileSize;
                responseHeader.statusCode = 200;
            }
        }
        else
        {
            IO::Debug(t("serving_full_file"));
        }

        // 设置文件响应Header
        responseHeader.headers["Content-Type"] = "application/octet-stream";
        responseHeader.headers["Content-Disposition"] = "attachment; filename=\"image.img\"";
        responseHeader.headers["Content-Length"] = safe_to_string(contentLength, t("failed_set_content_length"));

        // 生成并发送响应头
        std::string responseHeaderString = responseHeader.headerToString() + "\r\n";
        IO::Debug(t("sending_http_headers") + " (" + safe_to_string(responseHeaderString.length(), t("failed_convert_header_length")) + " " + t("bytes") + ")");
        
        // 发送响应头（分段发送）
        const char* headerPtr = responseHeaderString.c_str();
        size_t headerLen = responseHeaderString.length();
        size_t headerSent = 0;
        while (headerSent < headerLen) {
            int sent = send(clientSocket, headerPtr + headerSent, headerLen - headerSent, 0);
            if (sent == SOCKET_ERROR) {
                int errCode = WSAGetLastError();
                std::string errMsg = t("failed_send_headers") + ": " + safe_to_string(errCode, t("failed_convert_error_code"));
                IO::Error(errMsg);
                file.close();
                throw std::runtime_error(errMsg);
            }
            headerSent += sent;
        }

        // 定位到文件起始位置
        file.seekg(startPos);
        if (file.fail()) {
            std::string errMsg = t("failed_seek_file") + ": " + imagePath + " - pos: " + safe_to_string(startPos, t("failed_convert_seek_pos"));
            IO::Error(errMsg);
            file.close();
            sendHttpResponse(clientSocket, 500, "text/plain", t("internal_server_error"));
            return;
        }

        // 发送文件内容
        char buffer[BUFFER_SIZE];
        size_t remainingBytes = contentLength;
        size_t totalSent = 0;
        IO::Debug(t("starting_file_transfer"));

        while (remainingBytes > 0)
        {
            // 读取文件（最多BUFFER_SIZE字节）
            size_t readSize = std::min((size_t)BUFFER_SIZE, remainingBytes);
            file.read(buffer, readSize);
            size_t bytesRead = file.gcount();

            if (bytesRead == 0) {
                if (file.eof()) {
                    IO::Warn(t("file_read_eof_unexpectedly") + ": remaining bytes: " + safe_to_string(remainingBytes, t("failed_convert_remaining_bytes")));
                } else {
                    IO::Error(t("failed_read_file") + ": " + imagePath);
                }
                break;
            }

            // 发送读取的字节
            int sent = send(clientSocket, buffer, bytesRead, 0);
            if (sent == SOCKET_ERROR) {
                int errCode = WSAGetLastError();
                std::string errMsg = t("failed_send_file_data") + ": " + safe_to_string(errCode, t("failed_convert_error_code"));
                IO::Error(errMsg);
                file.close();
                throw std::runtime_error(errMsg);
            }

            remainingBytes -= sent;
            totalSent += sent;
        }

        file.close(); // 确保文件关闭
        IO::Info(t("sent_image_file") + " (" + safe_to_string(startPos, t("failed_convert_start_pos")) + "~" + safe_to_string(endPos, t("failed_convert_end_pos")) + ")");
        IO::Debug(t("file_transfer_completed") + ": " + safe_to_string(totalSent, t("failed_convert_total_sent")) + " " + t("bytes"));

    } catch (const std::exception& e) {
        IO::Error(t("failed_send_file_response") + ": " + e.what());
        // 确保文件关闭
        if (file.is_open()) {
            file.close();
        }
        // 发送500错误
        sendHttpResponse(clientSocket, 500, "text/plain", t("internal_server_error"));
    }
}
