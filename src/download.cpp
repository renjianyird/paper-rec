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

#include "download.hpp"
#include "io.hpp"
#include "i18n.hpp"
#include "define.hpp"
#include <wininet.h>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <codecvt>
#include <locale>

// 新增：错误码枚举（定位下载模块具体错误）
enum class DownloadError {
    SUCCESS = 0,
    EMPTY_URL = 2001,
    EMPTY_FILENAME = 2002,
    WININET_INIT_FAILED = 2003,
    WININET_CONNECT_FAILED = 2004,
    WININET_REQUEST_FAILED = 2005,
    HTTP_SEND_FAILED = 2006,
    HTTP_READ_FAILED = 2007,
    HTTP_QUERY_CONTENT_LENGTH_FAILED = 2008,
    JSON_PARSE_FAILED = 2009,
    FILE_CREATE_FAILED = 2010,
    FILE_WRITE_FAILED = 2011,
    URL_OPEN_FAILED = 2012,
    INVALID_ENCODING = 2013
};

// 新增：获取Windows系统错误信息
std::string GetWin32ErrorMsg(DWORD errorCode) {
    LPSTR msgBuffer = nullptr;
    DWORD bufLen = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msgBuffer, 0, NULL);
    
    std::string errorMsg;
    if (bufLen > 0) {
        errorMsg = msgBuffer;
        LocalFree(msgBuffer);
    } else {
        errorMsg = "Unknown Win32 error (code: " + std::to_string(errorCode) + ")";
    }
    return errorMsg;
}

// 新增：增强型错误终止函数（带错误码、系统详情）
void DownloadDie(DownloadError errorCode, const std::string& customMsg) {
    DWORD sysErr = GetLastError();
    std::ostringstream oss;
    oss << "[DOWNLOAD FATAL] [" << (int)errorCode << "] " << customMsg 
        << " | SystemError: " << GetWin32ErrorMsg(sysErr);
    
    IO::Error(oss.str());
    exit(static_cast<int>(errorCode));
}

// 修复：安全的string转tstring（支持UTF-8转宽字符）
tstring DOWNLOAD::toTString(std::string s) {
    try {
        // 处理空字符串
        if (s.empty()) return tstring();
        
        // UTF-8转宽字符（适配中文/特殊字符）
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wstr = converter.from_bytes(s);
        return tstring(wstr.begin(), wstr.end());
    } catch (const std::range_error& e) {
        DownloadDie(DownloadError::INVALID_ENCODING, 
                    t("failed_convert_string_encoding") + " | " + e.what());
    }
    return tstring();
}

// 新增：安全关闭WinINet句柄（避免空指针）
void SafeCloseInternetHandle(HINTERNET& hHandle) {
    if (hHandle) {
        InternetCloseHandle(hHandle);
        hHandle = NULL; // 置空避免重复释放
    }
}

nlohmann::json DOWNLOAD::getUpdateData(CAPTURE::CAPTURE_RESULT captureResult)
{
    IO::Debug(t("preparing_modified_request"));
    nlohmann::json modifiedBody = captureResult.request_body;
    modifiedBody["version"] = "99.99.90";
    modifiedBody["networkType"] = "WIFI";
    IO::Debug(t("modified_version_to") + ": " + std::string(modifiedBody["version"]));

    // 初始化句柄（避免野指针）
    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    try {
        IO::Debug(t("initializing_wininet"));
        hInternet = InternetOpen(TEXT("paper-download"), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hInternet) {
            throw std::runtime_error(t("failed_initialize_wininet"));
        }

        IO::Debug(t("connecting_to_server"));
        hConnect = InternetConnect(hInternet, TEXT("iotapi.abupdate.com"), INTERNET_DEFAULT_HTTP_PORT, 
                                   NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (!hConnect) {
            throw std::runtime_error(t("failed_connect_to_server"));
        }

        IO::Debug(t("opening_http_request_for") + ": " + captureResult.productUrl);
        hRequest = HttpOpenRequest(hConnect, TEXT("POST"), 
                                   toTString(captureResult.productUrl).c_str(), 
                                   NULL, NULL, NULL, 
                                   INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
        if (!hRequest) {
            throw std::runtime_error(t("failed_open_http_request"));
        }

        // 添加请求头（校验返回值）
        BOOL headerResult = HttpAddRequestHeaders(hRequest, 
                                                  TEXT("Content-Type: application/json;charset=UTF-8"), 
                                                  -1, HTTP_ADDREQ_FLAG_ADD);
        if (!headerResult) {
            throw std::runtime_error(t("failed_add_http_header"));
        }

        std::string bodyStr = modifiedBody.dump();
        DWORD bodyLength = static_cast<DWORD>(bodyStr.length());
        IO::Debug(t("request_body_size") + ": " + std::to_string(bodyLength) + " " + t("bytes"));

        IO::Debug(t("sending_http_request"));
        BOOL sendResult = HttpSendRequest(hRequest, NULL, 0, 
                                          (LPVOID)bodyStr.c_str(), bodyLength);
        if (!sendResult) {
            throw std::runtime_error(t("failed_send_http_request"));
        }

        IO::Debug(t("reading_http_response"));
        std::string response;
        char buffer[4096];
        DWORD dwRead = 0;
        while (true) {
            BOOL readResult = InternetReadFile(hRequest, buffer, sizeof(buffer), &dwRead);
            if (!readResult) {
                // 区分“读取失败”和“读取完成”
                DWORD err = GetLastError();
                if (err != ERROR_SUCCESS) {
                    throw std::runtime_error(t("failed_read_http_response") + " | " + GetWin32ErrorMsg(err));
                }
                break;
            }
            if (dwRead == 0) break;
            response.append(buffer, dwRead);
        }

        IO::Debug(t("response_size") + ": " + std::to_string(response.length()) + " " + t("bytes"));

        IO::Debug(t("parsing_json_response"));
        nlohmann::json responseJson;
        try {
            responseJson = nlohmann::json::parse(response);
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error(t("failed_parse_json_response") + " | " + e.what());
        }
        IO::Debug(t("json_response_parsed"));
        IO::Debug(t("response_json") + ": " + responseJson.dump(2, ' '));

        return responseJson;
    } catch (const std::runtime_error& e) {
        // 异常时确保所有句柄释放
        SafeCloseInternetHandle(hRequest);
        SafeCloseInternetHandle(hConnect);
        SafeCloseInternetHandle(hInternet);
        DownloadDie(DownloadError::HTTP_READ_FAILED, e.what());
    }

    // 正常流程释放句柄
    SafeCloseInternetHandle(hRequest);
    SafeCloseInternetHandle(hConnect);
    SafeCloseInternetHandle(hInternet);

    return nlohmann::json(); // 逻辑兜底（实际不会执行）
}

void DOWNLOAD::downloadFile(std::string url, std::string filename)
{
    // 新增：参数校验
    if (url.empty()) {
        DownloadDie(DownloadError::EMPTY_URL, t("url_is_empty"));
    }
    if (filename.empty()) {
        DownloadDie(DownloadError::EMPTY_FILENAME, t("filename_is_empty"));
    }

    // 文件已存在且用户选择跳过
    if (std::filesystem::exists(filename) && IO::Confirm(t("file_exists_skip_download"))) {
        IO::Debug(t("skip_download_file_exists") + ": " + filename);
        return;
    }

    IO::Info(t("downloading_image_file") + ": " + url);

    // 初始化句柄（避免野指针）
    HINTERNET hInternet = NULL;
    HINTERNET hUrl = NULL;
    std::ofstream outFile;

    try {
        hInternet = InternetOpen(TEXT("paper-download"), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hInternet) {
            throw std::runtime_error(t("failed_initialize_wininet"));
        }

        hUrl = InternetOpenUrl(hInternet, toTString(url).c_str(), NULL, 0, 
                               INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!hUrl) {
            throw std::runtime_error(t("failed_open_url"));
        }

        // 获取文件大小（强化错误处理）
        DWORD contentLength = 0;
        DWORD bufferSize = sizeof(contentLength);
        BOOL queryResult = HttpQueryInfo(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                                         &contentLength, &bufferSize, NULL);
        if (!queryResult) {
            IO::Warning(t("failed_get_content_length") + " | " + GetWin32ErrorMsg(GetLastError()));
            contentLength = 0; // 置0避免除零
        }
        IO::Debug(t("file_size") + ": " + (contentLength > 0 ? std::to_string(contentLength) : t("unknown")) + " " + t("bytes"));

        // 创建输出文件（强化错误处理）
        outFile.open(filename, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            throw std::runtime_error(t("failed_create_output_file") + ": " + filename);
        }

        char buffer[1024 * 1024]; // 1MB缓冲区
        DWORD bytesRead = 0;
        size_t totalDownloaded = 0;

        IO::Debug(t("start_downloading_file"));
        while (true) {
            BOOL readResult = InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead);
            if (!readResult) {
                DWORD err = GetLastError();
                if (err != ERROR_SUCCESS) {
                    throw std::runtime_error(t("failed_read_file_data") + " | " + GetWin32ErrorMsg(err));
                }
                break;
            }
            if (bytesRead == 0) break;

            // 写入文件并校验
            outFile.write(buffer, bytesRead);
            if (outFile.fail()) {
                throw std::runtime_error(t("failed_write_file_data") + ": " + filename);
            }

            totalDownloaded += bytesRead;

            // 进度显示（避免除零）
            if (contentLength > 0) {
                double percentage = (static_cast<double>(totalDownloaded) / contentLength) * 100.0;
                IO::ShowProgress(percentage, totalDownloaded, contentLength);
            } else {
                IO::Debug(t("downloaded") + ": " + std::to_string(totalDownloaded) + " " + t("bytes"));
            }
        }

        // 校验文件完整性
        if (contentLength > 0 && totalDownloaded != contentLength) {
            IO::Warning(t("file_incomplete") + ": " + std::to_string(totalDownloaded) + "/" + std::to_string(contentLength) + " " + t("bytes"));
        }

        IO::Info(t("download_complete") + ": " + filename);
    } catch (const std::runtime_error& e) {
        // 异常时清理资源
        if (outFile.is_open()) {
            outFile.close();
            std::filesystem::remove(filename); // 删除不完整文件
        }
        SafeCloseInternetHandle(hUrl);
        SafeCloseInternetHandle(hInternet);
        DownloadDie(DownloadError::FILE_WRITE_FAILED, e.what());
    }

    // 正常流程清理资源
    if (outFile.is_open()) {
        outFile.close();
    }
    SafeCloseInternetHandle(hUrl);
    SafeCloseInternetHandle(hInternet);
}
