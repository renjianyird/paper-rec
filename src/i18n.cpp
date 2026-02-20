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

#include "i18n.hpp"
#include "io.hpp"
#include <windows.h>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <mutex>

// 新增：日志输出工具函数（支持多语言报错）
namespace I18N::Internal {
    static std::mutex logMutex;

    // 输出错误日志（带时间戳）
    void LogError(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::cerr << "[ERROR] [" << st.wYear << "-" << st.wMonth << "-" << st.wDay 
                  << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] " 
                  << message << std::endl;
    }

    // 输出警告日志
    void LogWarning(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::cerr << "[WARN] [" << st.wYear << "-" << st.wMonth << "-" << st.wDay 
                  << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] " 
                  << message << std::endl;
    }
}

namespace I18N
{
    static Language currentLanguage = Language::ENGLISH;

    static std::unordered_map<std::string, std::unordered_map<Language, std::string>> translations = {
        // 原有翻译内容（省略，保持不变）
        {"app_starting", {{Language::ENGLISH, "Application starting..."}, {Language::CHINESE, "正在启动应用..."}}},
        {"app_terminating", {{Language::ENGLISH, "Application terminating normally"}, {Language::CHINESE, "应用正常关闭"}}},
        {"paper", {{Language::ENGLISH, "PAPER - Pen Adb Password Easily Reset"}, {Language::CHINESE, "PAPER - 一键重置词典笔 ADB 密码"}}},
        // ... 其他翻译项保持不变

        // 新增：错误提示的翻译项
        {"err_translation_key_not_found", {{Language::ENGLISH, "Translation key not found: "}, {Language::CHINESE, "未找到翻译键："}}},
        {"err_translation_language_not_found", {{Language::ENGLISH, "Language not found for key: "}, {Language::CHINESE, "翻译键无对应语言："}}},
        {"err_invalid_language", {{Language::ENGLISH, "Invalid language code: "}, {Language::CHINESE, "无效的语言代码："}}},
        {"warn_fallback_to_english", {{Language::ENGLISH, "Falling back to English for key: "}, {Language::CHINESE, "翻译键回退到英文："}}}
    };

    // 新增：初始化校验函数（程序启动时调用）
    bool Initialize() {
        bool isSuccess = true;
        // 校验核心翻译键是否存在
        const std::vector<std::string> criticalKeys = {"app_starting", "paper", "press_any_key_exit"};
        for (const auto& key : criticalKeys) {
            if (translations.find(key) == translations.end()) {
                Internal::LogError("Critical translation key missing: " + key);
                isSuccess = false;
            } else {
                // 校验关键语言是否都有翻译
                if (translations[key].find(Language::ENGLISH) == translations[key].end()) {
                    Internal::LogError("English translation missing for critical key: " + key);
                    isSuccess = false;
                }
                if (translations[key].find(Language::CHINESE) == translations[key].end()) {
                    Internal::LogWarning("Chinese translation missing for key: " + key);
                }
            }
        }
        return isSuccess;
    }

    // 新增：设置当前语言（带合法性校验）
    void SetCurrentLanguage(Language lang) {
        // 校验语言是否为合法值（假设 Language 是枚举，包含 ENGLISH/CHINESE）
        if (lang != Language::ENGLISH && lang != Language::CHINESE) {
            std::stringstream ss;
            ss << Internal::Translate("err_invalid_language") << static_cast<int>(lang);
            Internal::LogError(ss.str());
            return;
        }
        currentLanguage = lang;
    }

    // 优化：翻译查找函数（带容错和报错）
    std::string Translate(const std::string& key) {
        try {
            // 1. 检查翻译键是否存在
            auto keyIt = translations.find(key);
            if (keyIt == translations.end()) {
                std::string errMsg = Internal::Translate("err_translation_key_not_found") + key;
                Internal::LogError(errMsg);
                return "[MISSING_TRANSLATION: " + key + "]"; // 兜底返回
            }

            // 2. 检查当前语言是否有翻译
            auto langIt = keyIt->second.find(currentLanguage);
            if (langIt != keyIt->second.end()) {
                return langIt->second;
            }

            // 3. 回退到英文（如果英文存在）
            langIt = keyIt->second.find(Language::ENGLISH);
            if (langIt != keyIt->second.end()) {
                std::string warnMsg = Internal::Translate("warn_fallback_to_english") + key;
                Internal::LogWarning(warnMsg);
                return langIt->second;
            }

            // 4. 终极兜底
            std::string errMsg = Internal::Translate("err_translation_language_not_found") + key;
            Internal::LogError(errMsg);
            return "[NO_TRANSLATION_AVAILABLE: " + key + "]";

        } catch (const std::exception& e) {
            // 捕获所有异常，避免程序崩溃
            Internal::LogError("Exception in Translate() for key '" + key + "': " + e.what());
            return "[TRANSLATION_ERROR: " + key + "]";
        } catch (...) {
            Internal::LogError("Unknown exception in Translate() for key: " + key);
            return "[UNKNOWN_ERROR: " + key + "]";
        }
    }

    // 新增：内部翻译函数（避免循环依赖）
    namespace Internal {
        std::string Translate(const std::string& key) {
            auto keyIt = translations.find(key);
            if (keyIt != translations.end()) {
                auto langIt = keyIt->second.find(Language::ENGLISH); // 错误日志固定用英文
                if (langIt != keyIt->second.end()) {
                    return langIt->second;
                }
            }
            return "[" + key + "]";
        }
    }

    // 原有函数（如果有）保持不变，调用 Translate() 即可
}

// 新增：全局初始化（确保程序启动时执行校验）
namespace {
    static bool i18nInitialized = I18N::Initialize();
}
