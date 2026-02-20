#pragma once
#include <string>

namespace I18N {
    enum class Language {
        ENGLISH,
        CHINESE
    };

    bool Initialize();
    void SetCurrentLanguage(Language lang);
    std::string Translate(const std::string& key);
}

inline std::string t(const std::string& key) {
    return I18N::Translate(key);
}
