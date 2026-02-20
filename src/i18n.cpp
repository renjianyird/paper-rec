#include "i18n.hpp"
#include "io.hpp"
#include <unordered_map>
#include <mutex>

namespace I18N {
    static Language currentLanguage = Language::CHINESE;
    static std::mutex i18n_mutex;

    static std::unordered_map<std::string, std::unordered_map<Language, std::string>> translations = {
        {"paper", {{Language::ENGLISH, "PAPER"}, {Language::CHINESE, "PAPER"}}},
        {"app_starting", {{Language::ENGLISH, "Starting"}, {Language::CHINESE, "启动中"}}},
        {"argc_init_failed", {{Language::ENGLISH, "ARGC init failed"}, {Language::CHINESE, "参数初始化失败"}}},
        {"i18n_init_failed", {{Language::ENGLISH, "I18N init failed"}, {Language::CHINESE, "多语言初始化失败"}}},
        {"capture_failed", {{Language::ENGLISH, "Capture failed"}, {Language::CHINESE, "抓包失败"}}},
        {"get_update_data_failed", {{Language::ENGLISH, "Get update data failed"}, {Language::CHINESE, "获取更新数据失败"}}},
        {"delta_url_missing", {{Language::ENGLISH, "deltaUrl missing"}, {Language::CHINESE, "deltaUrl 不存在"}}},
        {"download_failed", {{Language::ENGLISH, "Download failed"}, {Language::CHINESE, "下载失败"}}},
        {"file_not_exists", {{Language::ENGLISH, "File not exists"}, {Language::CHINESE, "文件不存在"}}},
        {"hash_replace_failed", {{Language::ENGLISH, "Hash replace failed"}, {Language::CHINESE, "哈希替换失败"}}},
        {"segment_md5_parse_failed", {{Language::ENGLISH, "segmentMd5 parse failed"}, {Language::CHINESE, "分段MD5解析失败"}}},
        {"md5_segment_calc_failed", {{Language::ENGLISH, "MD5 segment calc failed"}, {Language::CHINESE, "分段MD5计算失败"}}},
        {"full_hash_calc_failed", {{Language::ENGLISH, "Full hash calc failed"}, {Language::CHINESE, "文件哈希计算失败"}}},
        {"host_enable_failed", {{Language::ENGLISH, "HOST enable failed"}, {Language::CHINESE, "HOST配置失败"}}},
        {"http_server_start_failed", {{Language::ENGLISH, "HTTP server start failed"}, {Language::CHINESE, "HTTP服务器启动失败"}}},
        {"product_url_empty", {{Language::ENGLISH, "productUrl empty"}, {Language::CHINESE, "产品地址为空"}}},
        {"invalid_product_url", {{Language::ENGLISH, "Invalid productUrl"}, {Language::CHINESE, "无效产品地址"}}},
        {"json_key_missing", {{Language::ENGLISH, "JSON key missing"}, {Language::CHINESE, "JSON字段缺失"}}},
        {"json_type_error", {{Language::ENGLISH, "JSON type error"}, {Language::CHINESE, "JSON类型错误"}}},
        {"runtime_error", {{Language::ENGLISH, "Runtime error"}, {Language::CHINESE, "运行错误"}}},
        {"unknown_error", {{Language::ENGLISH, "Unknown error"}, {Language::CHINESE, "未知错误"}}},
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
        auto it = translations.find(key);
        if (it == translations.end()) return key;
        auto lit = it->second.find(currentLanguage);
        if (lit != it->second.end()) return lit->second;
        return key;
    }
}
