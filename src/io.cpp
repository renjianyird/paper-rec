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

#include "io.hpp"
#include "i18n.hpp"
#include "argc.hpp"
#include <conio.h>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <windows.h>

// 新增：静态工具函数（报错日志+控制台句柄校验）
namespace IO::Internal {
    static std::mutex ioMutex; // 保证多线程下IO操作安全
    static HANDLE GetConsoleHandleSafe() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole == INVALID_HANDLE_VALUE) {
            DWORD errCode = GetLastError();
            std::stringstream errMsg;
            errMsg << "Failed to get console handle! Error code: " << errCode;
            throw std::runtime_error(errMsg.str());
        }
        return hConsole;
    }

    // 输出错误日志（带时间戳，兼容翻译）
    void LogError(const std::string& message) {
        std::lock_guard<std::mutex> lock(ioMutex);
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::cerr << "[IO_ERROR] [" << st.wYear << "-" << st.wMonth << "-" << st.wDay 
                  << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] " 
                  << message << std::endl;
    }
}

void IO::SetColor(WORD color)
{
    try {
        HANDLE hConsole = IO::Internal::GetConsoleHandleSafe();
        if (!SetConsoleTextAttribute(hConsole, color)) {
            DWORD errCode = GetLastError();
            std::stringstream errMsg;
            errMsg << "Failed to set console color! Error code: " << errCode;
            IO::Internal::LogError(errMsg.str());
            // 兜底：不终止程序，仅提示
            std::cerr << "[WARN] Failed to set console color (code: " << errCode << ")" << std::endl;
        }
    } catch (const std::exception& e) {
        IO::Internal::LogError("SetColor failed: " + std::string(e.what()));
    } catch (...) {
        IO::Internal::LogError("SetColor failed with unknown exception!");
    }
}

BOOL IO::Confirm(std::string message)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex); // 防止多线程同时读取输入
    char res = '\0';

    try {
        SetColor(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        
        // 校验翻译键是否有效
        std::string confirmPrefix = t("confirm_prefix");
        std::string confirmSuffix = t("confirm_suffix");
        if (confirmPrefix.find("[MISSING_TRANSLATION]") != std::string::npos ||
            confirmSuffix.find("[MISSING_TRANSLATION]") != std::string::npos) {
            IO::Internal::LogError("Confirm: Missing translation keys (confirm_prefix/confirm_suffix)");
            // 兜底文本
            confirmPrefix = "[Confirm]";
            confirmSuffix = "(Y/N)";
        }

        std::cout << confirmPrefix << " " << message << " " << confirmSuffix << " " << std::flush;

        // 安全读取输入：处理_getch()异常和EOF
        if (_kbhit()) { // 先检查是否有输入
            res = _getch();
            // 处理特殊键（如回车、ESC）
            if (res == '\r' || res == '\n' || res == 27) {
                res = 'n'; // 默认为取消
            }
        } else {
            IO::Internal::LogWarning("Confirm: No input detected, default to 'N'");
            res = 'n';
        }

        std::cout << res << std::endl;
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("Confirm failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        return FALSE; // 异常时默认取消
    } catch (...) {
        IO::Internal::LogError("Confirm failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        return FALSE;
    }

    return (res == 'y' || res == 'Y');
}

void IO::Input(std::string message, std::string &input)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    input.clear(); // 先清空输出变量

    try {
        SetColor(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

        // 校验翻译键
        std::string inputPrefix = t("input_prefix");
        if (inputPrefix.find("[MISSING_TRANSLATION]") != std::string::npos) {
            IO::Internal::LogError("Input: Missing translation key (input_prefix)");
            inputPrefix = "[Input]";
        }

        std::cout << inputPrefix << " " << message << std::flush;

        // 重置cin状态（解决之前输入错误导致的流失效）
        if (!std::cin.good()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            IO::Internal::LogWarning("Input: Reset invalid cin state");
        }

        std::getline(std::cin, input);

        // 处理空输入
        if (input.empty()) {
            IO::Internal::LogWarning("Input: Empty input received for message: " + message);
        }

        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("Input failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (...) {
        IO::Internal::LogError("Input failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
}

void IO::Debug(std::string message)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    try {
        if (!ARGC::HasArg("verbose"))
            return;

        // 校验翻译键
        std::string debugPrefix = t("debug_prefix");
        if (debugPrefix.find("[MISSING_TRANSLATION]") != std::string::npos) {
            IO::Internal::LogError("Debug: Missing translation key (debug_prefix)");
            debugPrefix = "[DEBUG]";
        }

        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << debugPrefix << " " << message << std::endl;
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("Debug failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (...) {
        IO::Internal::LogError("Debug failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
}

void IO::Info(std::string message)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    try {
        std::string infoPrefix = t("info_prefix");
        if (infoPrefix.find("[MISSING_TRANSLATION]") != std::string::npos) {
            IO::Internal::LogError("Info: Missing translation key (info_prefix)");
            infoPrefix = "[INFO]";
        }

        SetColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << infoPrefix << " " << message << std::endl;
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("Info failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (...) {
        IO::Internal::LogError("Info failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
}

void IO::Warn(std::string message)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    try {
        std::string warnPrefix = t("warn_prefix");
        if (warnPrefix.find("[MISSING_TRANSLATION]") != std::string::npos) {
            IO::Internal::LogError("Warn: Missing translation key (warn_prefix)");
            warnPrefix = "[WARN]";
        }

        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << warnPrefix << " " << message << std::endl;
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("Warn failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (...) {
        IO::Internal::LogError("Warn failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
}

void IO::Error(std::string message)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    try {
        std::string errorPrefix = t("error_prefix");
        if (errorPrefix.find("[MISSING_TRANSLATION]") != std::string::npos) {
            IO::Internal::LogError("Error: Missing translation key (error_prefix)");
            errorPrefix = "[ERROR]";
        }

        SetColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cout << errorPrefix << " " << message << std::endl;
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("Error failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (...) {
        IO::Internal::LogError("Error failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
}

void IO::ShowProgress(double percentage, size_t downloaded, size_t total)
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    static double lastPercentage = -1;
    static auto lastUpdateTime = std::chrono::steady_clock::now();

    try {
        // 1. 参数合法性校验
        if (total == 0) {
            IO::Internal::LogWarning("ShowProgress: Total size is 0 (division by zero risk)");
            return;
        }
        if (percentage < 0.0 || percentage > 100.0) {
            IO::Internal::LogWarning("ShowProgress: Invalid percentage (" + std::to_string(percentage) + "%), clamp to 0-100");
            percentage = std::clamp(percentage, 0.0, 100.0);
        }
        if (downloaded > total) {
            IO::Internal::LogWarning("ShowProgress: Downloaded (" + std::to_string(downloaded) + ") > Total (" + std::to_string(total) + ")");
            downloaded = total;
        }

        // 2. 频率控制（避免过度刷新）
        auto now = std::chrono::steady_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdateTime).count();
        if (percentage < 100.0 && timeDiff < 100 && std::abs(percentage - lastPercentage) < 1.0)
            return;

        lastPercentage = percentage;
        lastUpdateTime = now;

        // 3. 进度条计算（防止越界）
        int barWidth = 50;
        int filledWidth = static_cast<int>(std::round(percentage * barWidth / 100.0));
        filledWidth = std::clamp(filledWidth, 0, barWidth); // 确保不超出范围

        // 4. 输出进度条
        SetColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "\r[";
        for (int i = 0; i < barWidth; ++i)
        {
            if (i < filledWidth)
                std::cout << "=";
            else if (i == filledWidth && percentage < 100.0)
                std::cout << ">";
            else
                std::cout << " ";
        }

        // 5. 格式化大小（优化精度，避免科学计数法）
        std::stringstream downloadedSS, totalSS;
        if (total >= 1024 * 1024) {
            downloadedSS << std::fixed << std::setprecision(1) << (double)downloaded / (1024 * 1024) << " MB";
            totalSS << std::fixed << std::setprecision(1) << (double)total / (1024 * 1024) << " MB";
        } else if (total >= 1024) {
            downloadedSS << std::fixed << std::setprecision(1) << (double)downloaded / 1024 << " KB";
            totalSS << std::fixed << std::setprecision(1) << (double)total / 1024 << " KB";
        } else {
            downloadedSS << downloaded << " B";
            totalSS << total << " B";
        }

        std::cout << "] " << std::fixed << std::setprecision(1) << percentage << "% ("
                  << downloadedSS.str() << "/" << totalSS.str() << ")" << std::flush;

        if (percentage >= 100.0)
            std::cout << std::endl;

        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("ShowProgress failed: " + std::string(e.what()));
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << std::endl; // 避免进度条残留
    } catch (...) {
        IO::Internal::LogError("ShowProgress failed with unknown exception!");
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << std::endl;
    }
}

void IO::FlushProgress()
{
    std::lock_guard<std::mutex> lock(IO::Internal::ioMutex);
    try {
        std::cout << std::endl;
        SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    } catch (const std::exception& e) {
        IO::Internal::LogError("FlushProgress failed: " + std::string(e.what()));
    } catch (...) {
        IO::Internal::LogError("FlushProgress failed with unknown exception!");
    }
}

// 新增：补充LogWarning实现（适配原有逻辑）
namespace IO::Internal {
    void LogWarning(const std::string& message) {
        std::lock_guard<std::mutex> lock(ioMutex);
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::cerr << "[IO_WARN] [" << st.wYear << "-" << st.wMonth << "-" << st.wDay 
                  << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] " 
                  << message << std::endl;
    }
}
