namespace I18N {
    enum class Language {
        ENGLISH,
        CHINESE
    };
    bool Initialize();
    void SetCurrentLanguage(Language lang);
    std::string Translate(const std::string& key);
}
