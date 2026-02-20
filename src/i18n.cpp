#include "i18n.hpp"
#include "io.hpp"
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <mutex>

namespace I18N
{
    static Language currentLanguage = Language::CHINESE;
    static std::mutex i18n_mutex;

    static std::unordered_map<std::string, std::unordered_map<Language, std::string>> translations = {
        // 基础
        {"paper", {{Language::ENGLISH, "PAPER"}, {Language::CHINESE, "PAPER 工具"}}},
        {"app_starting", {{Language::ENGLISH, "Application starting..."}, {Language::CHINESE, "程序启动中..."}}},
        {"app_terminating", {{Language::ENGLISH, "Application terminating"}, {Language::CHINESE, "程序退出中..."}}},

        // 错误提示
        {"error", {{Language::ENGLISH, "Error"}, {Language::CHINESE, "错误"}}},
        {"argc_init_failed", {{Language::ENGLISH, "ARGC init failed"}, {Language::CHINESE, "参数解析初始化失败"}}},
        {"i18n_init_failed", {{Language::ENGLISH, "I18N init failed"}, {Language::CHINESE, "多语言初始化失败"}}},
        {"capture_failed", {{Language::ENGLISH, "Capture failed"}, {Language::CHINESE, "抓包失败"}}},
        {"get_update_data_failed", {{Language::ENGLISH, "Get update data failed"}, {Language::CHINESE, "获取更新信息失败"}}},
        {"delta_url_missing", {{Language::ENGLISH, "deltaUrl missing"}, {Language::CHINESE, "deltaUrl 不存在"}}},
        {"download_failed", {{Language::ENGLISH, "Download failed"}, {Language::CHINESE, "下载失败"}}},
        {"file_not_exists", {{Language::ENGLISH, "File not exists"}, {Language::CHINESE, "文件不存在"}}},
        {"hash_replace_failed", {{Language::ENGLISH, "Hash replace failed"}, {Language::CHINESE, "哈希替换失败"}}},
        {"segment_md5_parse_failed", {{Language::ENGLISH, "segmentMd5 parse failed"}, {Language::CHINESE, "分段MD5解析失败"}}},
        {"md5_segment_calc_failed", {{Language::ENGLISH, "MD5 segment calc failed"}, {Language::CHINESE, "分段MD5计算失败"}}},
        {"full_hash_calc_failed", {{Language::ENGLISH, "Full hash calc failed"}, {Language::CHINESE, "完整文件哈希计算失败"}}},
        {"host_enable_failed", {{Language::ENGLISH, "HOST enable failed"}, {Language::CHINESE, "HOST配置启用失败"}}},
        {"http_server_start_failed", {{Language::ENGLISH, "HTTP server start failed"}, {Language::CHINESE, "HTTP服务器启动失败"}}},
        {"product_url_empty", {{Language::ENGLISH, "productUrl is empty"}, {Language::CHINESE, "产品地址为空"}}},
        {"invalid_product_url", {{Language::ENGLISH, "Invalid productUrl"}, {Language::CHINESE, "无效的产品地址"}}},
        {"json_key_missing", {{Language::ENGLISH, "JSON key missing"}, {Language::CHINESE, "JSON字段缺失"}}},
        {"json_type_error", {{Language::ENGLISH, "JSON type error"}, {Language::CHINESE, "JSON类型错误"}}},
        {"runtime_error", {{Language::ENGLISH, "Runtime error"}, {Language::CHINESE, "运行时错误"}}},
        {"unexpected_error", {{Language::ENGLISH, "Unexpected error"}, {Language::CHINESE, "未知异常"}}},
        {"unknown_error", {{Language::ENGLISH, "Unknown error"}, {Language::CHINESE, "未知错误"}}},
        {"segmentation_fault", {{Language::ENGLISH, "Segmentation fault"}, {Language::CHINESE, "程序崩溃（段错误）"}}},

        // 日志前缀
        {"debug_prefix", {{Language::ENGLISH, "[DEBUG]"}, {Language::CHINESE, "[调试]"}}},
        {"info_prefix", {{Language::ENGLISH, "[INFO]"}, {Language::CHINESE, "[信息]"}}},
        {"warn_prefix", {{Language::ENGLISH, "[WARN]"}, {Language::CHINESE, "[警告]"}}},
        {"error_prefix", {{Language::ENGLISH, "[ERROR]"}, {Language::CHINESE, "[错误]"}}},

        // 交互
        {"confirm_prefix", {{Language::ENGLISH, "[Confirm]"}, {Language::CHINESE, "[确认]"}}},
        {"confirm_suffix", {{Language::ENGLISH, "(Y/N)"}, {Language::CHINESE, "(Y/N)"}}},
        {"input_prefix", {{Language::ENGLISH, "[Input]"}, {Language::CHINESE, "[输入]"}}},
        {"press_x_to_exit", {{Language::ENGLISH, "Press X to exit"}, {Language::CHINESE, "按 X 退出"}}},
    };

    bool Initialize() {
        std::lock_guard<std::mutex> lock(i18n_mutex);
        return true;
    }

    void SetCurrentLanguage(Language lang) {
        std::lock_guard<std::mutex> lock(i18n_mutex);
        currentLanguage = lang;
    }

    std::string Translate(const std::string& key) {
        std::lock_guard<std::mutex> lock(i18n_mutex);

        auto kit = translations.find(key);
        if (kit == translations.end()) {
            return "[NO_TRANSLATION:" + key + "]";
        }

        auto lit = kit->second.find(currentLanguage);
        if (lit != kit->second.end()) {
            return lit->second;
        }

        // 找不到就 fallback 英文
        lit = kit->second.find(Language::ENGLISH);
        if (lit != kit->second.end()) {
            return lit->second;
        }

        return "[INVALID_KEY:" + key + "]";
    }
}
