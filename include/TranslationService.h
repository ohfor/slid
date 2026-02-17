#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

/// Reads the shared SkyUI translation file for use by C++ code.
/// The same file (Data/Interface/Translations/SLID_LANGUAGE.txt) is used
/// by SkyUI for MCM translations and by this service for DLL notification strings.
class TranslationService {
public:
    static TranslationService* GetSingleton();

    /// Load translations from the translation file matching the game's sLanguage setting.
    /// Falls back to ENGLISH if the language-specific file is not found.
    /// Call after kDataLoaded (INI settings must be available for sLanguage lookup).
    void Load();

    /// Look up a translation key (e.g., "$SLID_ErrNoTarget").
    /// Returns the translated value, or the key itself if not found.
    std::string GetTranslation(const std::string& a_key) const;

    /// Look up a translation key and replace positional placeholders {0}, {1}, {2} with args.
    /// Allows translators to reorder arguments for grammar differences between languages.
    std::string FormatTranslation(const std::string& a_key,
                                   const std::string& a_arg0 = "",
                                   const std::string& a_arg1 = "",
                                   const std::string& a_arg2 = "") const;

private:
    TranslationService() = default;
    ~TranslationService() = default;
    TranslationService(const TranslationService&) = delete;
    TranslationService& operator=(const TranslationService&) = delete;

    /// Parse a UTF-16 LE BOM translation file into the translations map.
    /// Returns true if the file was found and parsed successfully.
    bool ParseFile(const std::filesystem::path& a_path);

    std::unordered_map<std::string, std::string> translations_;
    bool loaded_ = false;
};

// Convenience functions for quick translation access
inline std::string T(const std::string& a_key) {
    return TranslationService::GetSingleton()->GetTranslation(a_key);
}

inline std::string TF(const std::string& a_key,
                      const std::string& a_arg0 = "",
                      const std::string& a_arg1 = "",
                      const std::string& a_arg2 = "") {
    return TranslationService::GetSingleton()->FormatTranslation(a_key, a_arg0, a_arg1, a_arg2);
}
