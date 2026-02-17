#include "TranslationService.h"

#include <Windows.h>
#include <fstream>
#include <sstream>

TranslationService* TranslationService::GetSingleton() {
    static TranslationService instance;
    return &instance;
}

void TranslationService::Load() {
    std::string language = "ENGLISH";

    auto* ini = RE::INISettingCollection::GetSingleton();
    if (ini) {
        auto* setting = ini->GetSetting("sLanguage:General");
        if (setting && setting->GetType() == RE::Setting::Type::kString) {
            const char* val = setting->GetString();
            if (val && val[0]) {
                language = val;
            }
        }
    }

    logger::info("TranslationService: Game language is '{}'", language);

    auto path = std::filesystem::path("Data/Interface/Translations") /
                fmt::format("SLID_{}.txt", language);

    if (ParseFile(path)) {
        logger::info("TranslationService: Loaded {} translations from {}",
                     translations_.size(), path.string());
        loaded_ = true;
        return;
    }

    // Fallback to English
    if (language != "ENGLISH") {
        logger::warn("TranslationService: '{}' not found, falling back to ENGLISH", path.string());
        path = "Data/Interface/Translations/SLID_ENGLISH.txt";
        if (ParseFile(path)) {
            logger::info("TranslationService: Loaded {} translations from fallback {}",
                         translations_.size(), path.string());
            loaded_ = true;
            return;
        }
    }

    logger::warn("TranslationService: No translation file found - strings will show as raw keys");
}

std::string TranslationService::GetTranslation(const std::string& a_key) const {
    auto it = translations_.find(a_key);
    if (it != translations_.end()) {
        return it->second;
    }
    return a_key;  // Graceful degradation: show the key itself
}

std::string TranslationService::FormatTranslation(const std::string& a_key,
                                                   const std::string& a_arg0,
                                                   const std::string& a_arg1,
                                                   const std::string& a_arg2) const {
    std::string result = GetTranslation(a_key);

    // Replace positional placeholders {0}, {1}, {2}
    const std::string placeholders[] = {"{0}", "{1}", "{2}"};
    const std::string* args[] = {&a_arg0, &a_arg1, &a_arg2};

    for (int i = 0; i < 3; ++i) {
        auto pos = result.find(placeholders[i]);
        if (pos != std::string::npos) {
            result.replace(pos, 3, *args[i]);
        }
    }

    return result;
}

bool TranslationService::ParseFile(const std::filesystem::path& a_path) {
    // Open as binary to handle BOM and encoding ourselves
    std::ifstream file(a_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read entire file
    std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    file.close();

    if (raw.size() < 2) {
        return false;
    }

    // Check for UTF-16 LE BOM (0xFF 0xFE)
    size_t offset = 0;
    if (static_cast<unsigned char>(raw[0]) == 0xFF &&
        static_cast<unsigned char>(raw[1]) == 0xFE) {
        offset = 2;  // Skip BOM
    }

    // Convert UTF-16 LE to UTF-8
    // Each UTF-16 code unit is 2 bytes, little-endian
    std::wstring wide;
    for (size_t i = offset; i + 1 < raw.size(); i += 2) {
        wchar_t ch = static_cast<unsigned char>(raw[i]) |
                     (static_cast<unsigned char>(raw[i + 1]) << 8);
        wide += ch;
    }

    // Convert wstring to UTF-8 using Win32 API
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        logger::error("TranslationService: UTF-16 to UTF-8 conversion failed");
        return false;
    }
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        utf8.data(), utf8Len, nullptr, nullptr);

    // Parse lines
    std::istringstream stream(utf8);
    std::string line;
    int count = 0;

    while (std::getline(stream, line)) {
        // Strip \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip empty lines
        if (line.empty()) continue;

        // Skip lines that don't start with $
        if (line[0] != '$') continue;

        // Find first tab - separates key from value
        auto tabPos = line.find('\t');
        if (tabPos == std::string::npos) continue;

        std::string key = line.substr(0, tabPos);
        std::string value = line.substr(tabPos + 1);

        // Trim trailing whitespace from value
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.pop_back();
        }

        if (!key.empty() && !value.empty()) {
            translations_[key] = value;
            ++count;
        }
    }

    logger::debug("TranslationService: Parsed {} key-value pairs from {}", count, a_path.string());
    return count > 0;
}
