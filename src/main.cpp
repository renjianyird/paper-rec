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

#include "define.hpp"
#include "capture.hpp"
#include "io.hpp"
#include "core.hpp"
#include "download.hpp"
#include "hash.hpp"
#include "httpServer.hpp"
#include "i18n.hpp"
#include "argc.hpp"
#include "host.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <csignal>
#include <cstdio>

// 错误码定义
enum class ErrorCode {
    SUCCESS = 0,
    ARGC_INIT_FAILED = 1,
    I18N_INIT_FAILED = 2,
    CAPTURE_FAILED = 3,
    GET_UPDATE_DATA_FAILED = 4,
    DELTA_URL_MISSING = 5,
    DOWNLOAD_FILE_FAILED = 6,
    FILE_HASH_FAILED = 7,
    SEGMENT_MD5_PARSE_FAILED = 8,
    MD5_SEGMENT_CALC_FAILED = 9,
    HTTP_SERVER_START_FAILED = 10,
    HOST_CONFIG_FAILED = 11,
    JSON_PARSE_FAILED = 12,
    INVALID_PRODUCT_URL = 13
};

// 跨平台getch实现
#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
int _getch() {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

// 通用错误处理函数
void handleError(ErrorCode code, const std::string& msg) {
    IO::Error("[" + std::to_string(static_cast<int>(code)) + "] " + t("error") + ": " + msg);
    // 清理资源
    HOST::disable();
    throw std::runtime_error(msg + " (code: " + std::to_string(static_cast<int>(code)) + ")");
}

// JSON安全访问函数
template <typename T>
T getJsonValue(const nlohmann::json& jsonObj, const std::string& path, ErrorCode errCode) {
    std::vector<std::string> keys;
    size_t pos = 0;
    std::string token;
    std::string pathCopy = path;
    
    // 分割路径（如 "data.version.deltaUrl"）
    while ((pos = pathCopy.find('.')) != std::string::npos) {
        token = pathCopy.substr(0, pos);
        keys.push_back(token);
        pathCopy.erase(0, pos + 1);
    }
    keys.push_back(pathCopy);
    
    const nlohmann::json* current = &jsonObj;
    std::string currentPath;
    for (const auto& key : keys) {
        currentPath += (currentPath.empty() ? "" : ".") + key;
        if (!current->contains(key) || current->at(key).is_null()) {
            handleError(errCode, t("json_key_missing") + ": " + currentPath);
        }
        current = &(current->at(key));
    }
    
    try {
        return current->get<T>();
    } catch (const nlohmann::json::type_error& e) {
        handleError(errCode, t("json_type_error") + ": " + currentPath + " (" + e.what() + ")");
    }
}

int main(int argc, char *argv[]) {
    // 设置异常捕获
    std::signal(SIGSEGV, [](int sig) {
        IO::Error(t("segmentation_fault") + " (signal: " + std::to_string(sig) + ")");
        HOST::disable();
        exit(static_cast<int>(ErrorCode::CAPTURE_FAILED));
    });

    try {
        // 初始化参数解析（带错误检查）
        if (!ARGC::Initialize(argc, argv)) {
            handleError(ErrorCode::ARGC_INIT_FAILED, t("argc_init_failed"));
        }
        const std::string imageFile = ARGC::GetArg("image", "image.img");
        
        // 初始化国际化（带错误检查）
        if (!I18N::Initialize()) {
            handleError(ErrorCode::I18N_INIT_FAILED, t("i18n_init_failed"));
        }
        IO::Debug(t("app_starting"));

        // 核心初始化
        CORE::BindSignal();
        CORE::Header();
        CORE::ElevateNow();

        // 抓包逻辑（带错误检查）
        CAPTURE::CAPTURE_RESULT result;
        CAPTURE capturer;
        IO::Debug(t("starting_packet_capture"));
        if (!capturer.capture(result)) {
            handleError(ErrorCode::CAPTURE_FAILED, t("capture_failed"));
        }
        // 校验productUrl合法性
        if (result.productUrl.empty()) {
            handleError(ErrorCode::INVALID_PRODUCT_URL, t("product_url_empty"));
        }

        // 获取更新数据（带错误检查）
        IO::Debug(t("fetching_update_data"));
        auto updateData = DOWNLOAD::getUpdateData(result);
        if (updateData.is_null() || updateData.empty()) {
            handleError(ErrorCode::GET_UPDATE_DATA_FAILED, t("get_update_data_failed"));
        }

        // 安全获取deltaUrl
        std::string deltaUrl;
        try {
            deltaUrl = getJsonValue<std::string>(updateData, "data.version.deltaUrl", ErrorCode::DELTA_URL_MISSING);
        } catch (const std::runtime_error& e) {
            handleError(ErrorCode::DELTA_URL_MISSING, t("delta_url_missing") + " (" + e.what() + ")");
        }
        IO::Debug(t("delta_url_extracted") + ": " + deltaUrl);

        // 下载文件（带错误检查）
        IO::Info(t("downloading_file") + ": " + deltaUrl);
        if (!DOWNLOAD::downloadFile(deltaUrl, imageFile)) {
            handleError(ErrorCode::DOWNLOAD_FILE_FAILED, t("download_failed") + ": " + deltaUrl);
        }
        // 检查文件是否存在
        if (!std::filesystem::exists(imageFile)) {
            handleError(ErrorCode::DOWNLOAD_FILE_FAILED, t("file_not_exists") + ": " + imageFile);
        }

        // 替换Hash（带错误检查）
        IO::Debug(t("replacing_hash"));
        if (!HASH::replaceHash(imageFile)) {
            handleError(ErrorCode::FILE_HASH_FAILED, t("hash_replace_failed") + ": " + imageFile);
        }

        // 计算Segment MD5（带错误检查）
        IO::Info(t("calculating_hash"));
        nlohmann::json segmentMd5;
        try {
            std::string segmentMd5Str = getJsonValue<std::string>(updateData, "data.version.segmentMd5", ErrorCode::SEGMENT_MD5_PARSE_FAILED);
            segmentMd5 = nlohmann::json::parse(segmentMd5Str);
        } catch (const nlohmann::json::parse_error& e) {
            handleError(ErrorCode::SEGMENT_MD5_PARSE_FAILED, t("segment_md5_parse_failed") + ": " + e.what());
        }

        // 遍历计算分段MD5
        for (auto& md5 : segmentMd5) {
            try {
                uint64_t startPos = md5["startpos"].get<uint64_t>();
                uint64_t endPos = md5["endpos"].get<uint64_t>();
                std::string md5Value = HASH::MD5FileSegment(imageFile, startPos, endPos);
                if (md5Value.empty()) {
                    handleError(ErrorCode::MD5_SEGMENT_CALC_FAILED, 
                        t("md5_segment_calc_failed") + " (start: " + std::to_string(startPos) + ", end: " + std::to_string(endPos) + ")");
                }
                md5["md5"] = md5Value;
            } catch (const std::exception& e) {
                handleError(ErrorCode::MD5_SEGMENT_CALC_FAILED, t("md5_segment_error") + ": " + e.what());
            }
        }
        updateData["data"]["version"]["segmentMd5"] = segmentMd5.dump();

        // 计算完整文件哈希（带错误检查）
        IO::Debug(t("calculating_full_md5"));
        std::string fullMd5 = HASH::MD5File(imageFile);
        std::string sha1 = HASH::SHA1File(imageFile);
        if (fullMd5.empty() || sha1.empty()) {
            handleError(ErrorCode::FILE_HASH_FAILED, t("full_hash_calc_failed") + ": " + imageFile);
        }
        updateData["data"]["version"]["md5sum"] = fullMd5;
        updateData["data"]["version"]["sha"] = sha1;

        // 配置服务器地址（可通过参数覆盖）
        std::string serverIp = ARGC::GetArg("server-ip", "192.168.137.1");
        std::string fileUrl = "http://" + serverIp + "/" + std::filesystem::path(imageFile).filename().string();
        updateData["data"]["version"]["deltaUrl"] = fileUrl;
        updateData["data"]["version"]["bakUrl"] = fileUrl;

        IO::Debug(t("final_update_data") + ":\n" + updateData.dump(2, ' '));

        // 配置HOST（带错误检查）
        if (!HOST::enable()) {
            handleError(ErrorCode::HOST_CONFIG_FAILED, t("host_enable_failed"));
        }

        // 提取Product URL前缀（带错误检查）
        std::string productUrlPrefix = result.productUrl.substr(0, result.productUrl.find_last_of('/'));
        if (productUrlPrefix.empty()) {
            handleError(ErrorCode::INVALID_PRODUCT_URL, t("invalid_product_url") + ": " + result.productUrl);
        }

        // 启动HTTP服务器（带错误检查）
        HTTP_SERVER httpServer(80, imageFile, updateData.dump(), productUrlPrefix);
        IO::Info(t("starting_http_server") + " on port 80");
        if (!httpServer.start()) {
            handleError(ErrorCode::HTTP_SERVER_START_FAILED, t("http_server_start_failed"));
        }

        // 等待退出指令
        IO::Info(t("server_running") + " - " + t("press_x_to_exit"));
        while (_getch() != 'x');

        // 清理资源
        httpServer.stop();
        HOST::disable();
        IO::Debug(t("app_terminating"));
        _getch();

        return static_cast<int>(ErrorCode::SUCCESS);

    } catch (const std::runtime_error& e) {
        IO::Error(t("runtime_error") + ": " + e.what());
        HOST::disable();
        return static_cast<int>(ErrorCode::CAPTURE_FAILED);
    } catch (const std::exception& e) {
        IO::Error(t("unexpected_error") + ": " + e.what());
        HOST::disable();
        return -1;
    } catch (...) {
        IO::Error(t("unknown_error"));
        HOST::disable();
        return -1;
    }
}
