#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <windows.h>
#include <nlohmann/json.hpp>
#include "i18n.hpp"
#include "io.hpp"
#include "argc.hpp"
#include "capture.hpp"
#include "download.hpp"
#include "hash.hpp"
#include "httpServer.hpp"
#include "host.hpp"

using json = nlohmann::json;

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

void handleError(ErrorCode code, const std::string& msg) {
    IO::Error("[" + std::to_string((int)code) + "] " + msg);
    HOST::disable();
    throw std::runtime_error(msg);
}

template <typename T>
T getJsonValue(const json& j, const std::string& path, ErrorCode err) {
    std::vector<std::string> keys;
    std::string s = path;
    size_t pos;
    while ((pos = s.find('.')) != std::string::npos) {
        keys.push_back(s.substr(0, pos));
        s.erase(0, pos + 1);
    }
    keys.push_back(s);

    const json* cur = &j;
    for (const auto& k : keys) {
        if (!cur->contains(k) || cur->at(k).is_null())
            handleError(err, t("json_key_missing") + ": " + path);
        cur = &cur->at(k);
    }

    try {
        return cur->get<T>();
    } catch (...) {
        handleError(err, t("json_type_error") + ": " + path);
        throw;
    }
}

int main() {
    try {
        if (!ARGC::Initialize(__argc, __argv))
            handleError(ErrorCode::ARGC_INIT_FAILED, t("argc_init_failed"));

        if (!I18N::Initialize())
            handleError(ErrorCode::I18N_INIT_FAILED, t("i18n_init_failed"));

        CAPTURE capturer;
        CAPTURE_RESULT res;
        if (!capturer.capture(res))
            handleError(ErrorCode::CAPTURE_FAILED, t("capture_failed"));

        json update = DOWNLOAD::getUpdateData(res);
        if (update.is_null() || update.empty())
            handleError(ErrorCode::GET_UPDATE_DATA_FAILED, t("get_update_data_failed"));

        std::string deltaUrl = getJsonValue<std::string>(update, "data.version.deltaUrl", ErrorCode::DELTA_URL_MISSING);
        std::string img = ARGC::GetArg("image", "image.img");

        if (!DOWNLOAD::downloadFile(deltaUrl, img))
            handleError(ErrorCode::DOWNLOAD_FILE_FAILED, t("download_failed"));

        if (!std::filesystem::exists(img))
            handleError(ErrorCode::FILE_HASH_FAILED, t("file_not_exists"));

        if (!HASH::replaceHash(img))
            handleError(ErrorCode::FILE_HASH_FAILED, t("hash_replace_failed"));

        HOST::enable();

        HTTP_SERVER srv(80, img, update.dump(), res.productUrl);
        if (!srv.start())
            handleError(ErrorCode::HTTP_SERVER_START_FAILED, t("http_server_start_failed"));

        IO::Info(t("press_x_to_exit"));
        while (_getch() != 'x');

        srv.stop();
        HOST::disable();
        return 0;
    }
    catch (const std::exception& e) {
        IO::Error(e.what());
        HOST::disable();
        return 1;
    }
    catch (...) {
        IO::Error(t("unknown_error"));
        HOST::disable();
        return -1;
    }
}
