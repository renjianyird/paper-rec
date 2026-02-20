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

#include "host.hpp"
#include "define.hpp"
#include "io.hpp"
#include "i18n.hpp"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string>

const std::string HOST::HOST_ENTRY = "192.168.137.1 iotapi.abupdate.com";
const std::string HOST::HOSTS_FILE_PATH = "C:\\Windows\\System32\\drivers\\etc\\hosts";

// 新增：检查是否为管理员权限
bool HOST::IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID pAdminSid = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    // 创建管理员组SID
    if (!AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &pAdminSid)) {
        IO::Error(t("failed_create_admin_sid") + ": " + std::to_string(GetLastError()));
        return false;
    }

    // 检查当前进程是否属于管理员组
    if (!CheckTokenMembership(NULL, pAdminSid, &isAdmin)) {
        IO::Error(t("failed_check_admin_permission") + ": " + std::to_string(GetLastError()));
        FreeSid(pAdminSid);
        return false;
    }

    FreeSid(pAdminSid);
    return isAdmin == TRUE;
}

// 修复：完善错误处理，抛出异常而非直接终止
bool HOST::ReadHostsFile(std::vector<std::string> &lines) {
    IO::Debug(t("opening_hosts_file") + ": " + HOSTS_FILE_PATH);
    std::ifstream file(HOSTS_FILE_PATH, std::ios::in);
    if (!file.is_open()) {
        std::string errMsg = t("cannot_open_hosts_file") + " (" + HOSTS_FILE_PATH + "): " + std::to_string(GetLastError());
        IO::Error(errMsg);
        // 新增：抛出异常，让调用方决定是否终止
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("reading_hosts_file"));
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }

    // 检查读取过程中是否出错
    if (file.bad()) {
        std::string errMsg = t("failed_read_hosts_file") + ": " + std::to_string(GetLastError());
        IO::Error(errMsg);
        file.close();
        throw std::runtime_error(errMsg);
    }

    file.close();
    return true;
}

// 修复：完善错误处理 + 换行符改为\r\n
bool HOST::WriteHostsFile(const std::vector<std::string> &lines) {
    // 前置检查：管理员权限
    if (!IsAdmin()) {
        std::string errMsg = t("need_admin_permission_to_write_hosts");
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("writing_hosts_file"));
    // 新增：使用二进制模式避免自动转换换行符
    std::ofstream file(HOSTS_FILE_PATH, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::string errMsg = t("failed_write_hosts_file") + " (" + HOSTS_FILE_PATH + "): " + std::to_string(GetLastError());
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    for (const auto &line : lines) {
        // 修复：Windows下hosts文件用\r\n换行
        file << line << "\r\n";
    }

    // 检查写入过程中是否出错
    if (file.bad()) {
        std::string errMsg = t("failed_write_content_to_hosts") + ": " + std::to_string(GetLastError());
        IO::Error(errMsg);
        file.close();
        throw std::runtime_error(errMsg);
    }

    file.close();
    IO::Debug(t("hosts_file_updated"));
    return true;
}

// 修复：忽略注释行（#开头），避免误判
bool HOST::ContainsHostEntry(const std::vector<std::string> &lines) {
    for (const auto &line : lines) {
        // 跳过注释行
        size_t commentPos = line.find('#');
        std::string lineWithoutComment = (commentPos == std::string::npos) ? line : line.substr(0, commentPos);
        
        if (lineWithoutComment.find("iotapi.abupdate.com") != std::string::npos &&
            lineWithoutComment.find("192.168.137.1") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 修复：删除时也忽略注释行，避免误删
void HOST::RemoveHostEntry(std::vector<std::string> &lines) {
    lines.erase(
        std::remove_if(lines.begin(), lines.end(),
                       [](const std::string &line) {
                           size_t commentPos = line.find('#');
                           std::string lineWithoutComment = (commentPos == std::string::npos) ? line : line.substr(0, commentPos);
                           return lineWithoutComment.find("iotapi.abupdate.com") != std::string::npos &&
                                  lineWithoutComment.find("192.168.137.1") != std::string::npos;
                       }),
        lines.end());
}

// 修复：完善资源释放 + 错误处理
void HOST::FlushDnsCache() {
    IO::Debug(t("flushing_dns_cache"));
    IO::Debug(t("loading_dnsapi_dll"));
    
    HMODULE hDnsApi = LoadLibraryA("dnsapi.dll");
    if (hDnsApi == NULL) {
        std::string errMsg = t("failed_load_dnsapi") + ": " + std::to_string(GetLastError());
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    typedef BOOL(WINAPI * DnsFlushResolverCacheFunc)();
    DnsFlushResolverCacheFunc DnsFlushResolverCache =
        (DnsFlushResolverCacheFunc)GetProcAddress(hDnsApi, "DnsFlushResolverCache");
    
    BOOL result = FALSE;
    if (DnsFlushResolverCache == NULL) {
        std::string errMsg = t("failed_get_dns_function") + ": " + std::to_string(GetLastError());
        IO::Error(errMsg);
        FreeLibrary(hDnsApi); // 修复：确保释放DLL
        throw std::runtime_error(errMsg);
    }

    result = DnsFlushResolverCache();
    FreeLibrary(hDnsApi); // 提前释放，避免泄漏

    if (!result) {
        std::string errMsg = t("failed_flush_dns_cache") + ": " + std::to_string(GetLastError());
        IO::Error(errMsg);
        throw std::runtime_error(errMsg);
    }

    IO::Debug(t("dns_cache_flushed"));
}

// 修复：新增返回值 + 异常捕获 + 权限检查
bool HOST::enable() {
    try {
        IO::Debug(t("enabling_host_redirect"));

        // 前置检查：管理员权限
        if (!IsAdmin()) {
            IO::Error(t("enable_host_need_admin"));
            return false;
        }

        std::vector<std::string> lines;
        ReadHostsFile(lines);

        if (ContainsHostEntry(lines)) {
            IO::Warn(t("host_entry_already_exists"));
            return true; // 已存在视为执行成功
        }

        IO::Debug(t("adding_host_entry") + ": " + HOST_ENTRY);
        lines.push_back(HOST_ENTRY);
        WriteHostsFile(lines);
        IO::Info(t("host_entry_added"));
        
        FlushDnsCache();
        return true;
    } catch (const std::runtime_error &e) {
        IO::Error(t("enable_host_failed") + ": " + e.what());
        return false;
    } catch (...) {
        IO::Error(t("enable_host_unknown_error"));
        return false;
    }
}

// 修复：新增返回值 + 异常捕获 + 权限检查
bool HOST::disable() {
    try {
        IO::Debug(t("disabling_host_redirect"));

        // 前置检查：管理员权限
        if (!IsAdmin()) {
            IO::Error(t("disable_host_need_admin"));
            return false;
        }

        std::vector<std::string> lines;
        ReadHostsFile(lines);

        if (!ContainsHostEntry(lines)) {
            IO::Warn(t("host_entry_not_found"));
            return true; // 不存在视为执行成功
        }

        IO::Debug(t("removing_host_entry"));
        RemoveHostEntry(lines);
        WriteHostsFile(lines);
        IO::Info(t("host_entry_removed"));
        
        FlushDnsCache();
        return true;
    } catch (const std::runtime_error &e) {
        IO::Error(t("disable_host_failed") + ": " + e.what());
        return false;
    } catch (...) {
        IO::Error(t("disable_host_unknown_error"));
        return false;
    }
}
