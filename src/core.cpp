#include "define.hpp"
#include "core.hpp"
#include "io.hpp"
#include "i18n.hpp"
#include <windows.h>
#include <sstream>
#include <string>

// 新增：错误码枚举（便于定位问题）
enum class CoreError {
    SUCCESS = 0,
    ALLOCATE_SID_FAILED = 1001,
    CHECK_TOKEN_FAILED = 1002,
    GET_MODULE_PATH_FAILED = 1003,
    SHELLEXECUTE_ELEVATE_FAILED = 1004,
    USER_DECLINED_ELEVATION = 1005,
    SIGNAL_RECEIVED = 1006
};

// 新增：获取系统错误信息（Windows API）
std::string GetSystemErrorMsg(DWORD errorCode) {
    LPSTR msgBuffer = nullptr;
    DWORD bufLen = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msgBuffer, 0, NULL);
    
    std::string errorMsg;
    if (bufLen > 0) {
        errorMsg = msgBuffer;
        LocalFree(msgBuffer); // 释放FormatMessage分配的内存
    } else {
        errorMsg = "Unknown system error (code: " + std::to_string(errorCode) + ")";
    }
    return errorMsg;
}

// 新增：增强型错误终止函数（带错误码、详细信息）
void CoreDie(CoreError errorCode, const std::string& customMsg) {
    DWORD sysErr = GetLastError();
    std::ostringstream oss;
    oss << "[FATAL] [" << (int)errorCode << "] " << customMsg 
        << " | SystemError: " << GetSystemErrorMsg(sysErr);
    
    IO::Error(oss.str()); // 输出错误日志（假设IO::Error是错误日志接口）
    exit(static_cast<int>(errorCode)); // 携带错误码退出
}

void CORE::SignalHandler(int signum) {
    std::string msg = t("received_signal") + " " + std::to_string(signum);
    CoreDie(CoreError::SIGNAL_RECEIVED, msg);
}

BOOL CORE::IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    // 1. 分配并初始化SID（强化错误处理）
    if (!AllocateAndInitializeSid(
            &ntAuthority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0,
            &adminGroup)) {
        DWORD err = GetLastError();
        std::string msg = t("failed_allocate_sid") + " (Error: " + GetSystemErrorMsg(err) + ")";
        CoreDie(CoreError::ALLOCATE_SID_FAILED, msg);
    }

    // 2. 检查令牌成员身份（确保FreeSid执行）
    BOOL checkResult = CheckTokenMembership(NULL, adminGroup, &isAdmin);
    FreeSid(adminGroup); // 提前释放，避免泄漏

    if (!checkResult) {
        DWORD err = GetLastError();
        std::string msg = t("failed_check_token") + " (Error: " + GetSystemErrorMsg(err) + ")";
        CoreDie(CoreError::CHECK_TOKEN_FAILED, msg);
    }

    return isAdmin;
}

void CORE::BindSignal() {
    IO::Debug(t("binding_signal_handlers"));
    // 新增：信号处理函数返回值校验
    if (signal(SIGINT, SignalHandler) == SIG_ERR) {
        IO::Warning(t("failed_bind_signal_SIGINT")); // 非致命错误仅警告
    }
    if (signal(SIGTERM, SignalHandler) == SIG_ERR) {
        IO::Warning(t("failed_bind_signal_SIGTERM"));
    }
    if (signal(SIGABRT, SignalHandler) == SIG_ERR) {
        IO::Warning(t("failed_bind_signal_SIGABRT"));
    }
    IO::Debug(t("signal_handlers_bound"));
}

void CORE::Header() {
    IO::Info(t("paper"));
    IO::Info("");
}

void CORE::ElevateNow() {
    IO::Debug(t("checking_admin_privileges"));
    if (!IsAdmin()) {
        IO::Debug(t("not_admin_requesting_elevation"));
        if (!IO::Confirm(t("admin_privileges_required"))) {
            CoreDie(CoreError::USER_DECLINED_ELEVATION, t("user_declined_elevation"));
        }

        // 1. 获取模块路径（强化编码兼容性）
        TCHAR modulePath[MAX_PATH] = {0}; // 初始化清空，避免脏数据
        DWORD pathLen = GetModuleFileName(NULL, modulePath, ARRAYSIZE(modulePath));
        if (pathLen == 0 || pathLen >= ARRAYSIZE(modulePath)) {
            DWORD err = GetLastError();
            std::string msg = t("failed_get_module_name") + " (Error: " + GetSystemErrorMsg(err) + ")";
            CoreDie(CoreError::GET_MODULE_PATH_FAILED, msg);
        }

        IO::Debug(t("attempting_elevation"));
        
        // 2. 初始化ShellExecuteInfo（清空所有字段）
        SHELLEXECUTEINFO shellExecuteInfo = {0};
        shellExecuteInfo.cbSize = sizeof(SHELLEXECUTEINFO); // 必须设置
        shellExecuteInfo.lpVerb = TEXT("runas");
        shellExecuteInfo.lpFile = modulePath;
        shellExecuteInfo.hwnd = NULL;
        shellExecuteInfo.nShow = SW_SHOWDEFAULT;
        shellExecuteInfo.fMask = SEE_MASK_NOCLOSEPROCESS; // 新增：跟踪进程

        // 3. 执行提权
        if (!ShellExecuteEx(&shellExecuteInfo)) {
            DWORD err = GetLastError();
            std::string msg = t("failed_elevate_privileges") + " (Error: " + GetSystemErrorMsg(err) + ")";
            CoreDie(CoreError::SHELLEXECUTE_ELEVATE_FAILED, msg);
        }

        IO::Debug(t("elevation_request_sent"));
        // 规范退出：释放资源后退出
        if (shellExecuteInfo.hProcess != NULL) {
            CloseHandle(shellExecuteInfo.hProcess); // 关闭进程句柄
        }
        exit(EXIT_SUCCESS); // 替代原exit(0)，更规范
    } else {
        IO::Debug(t("already_admin"));
    }
}
