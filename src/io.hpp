#pragma once
#include <string>
#include <cstdint>
#include <windows.h>

namespace IO {
    void SetColor(WORD color);
    BOOL Confirm(std::string message);
    void Input(std::string message, std::string &input);
    void Debug(std::string message);
    void Info(std::string message);
    void Warn(std::string message);
    void Error(std::string message);
    void ShowProgress(double percentage, size_t downloaded, size_t total);
    void FlushProgress();

    // 内部工具函数声明
    namespace Internal {
        void LogError(const std::string& message);
        void LogWarning(const std::string& message);
    }
}

// 兼容原有t()函数（假设是I18N::Translate的别名）
#ifndef t
#define t(key) I18N::Translate(key)
#endif
