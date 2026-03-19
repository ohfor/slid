#include "Settings.h"

#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Settings {

    // --- Dirty tracking ---
    static std::unordered_set<std::string> s_dirtyKeys;

    void MarkDirty(const char* a_key) {
        s_dirtyKeys.insert(a_key);
    }

    bool IsDirty(const char* a_key) {
        return s_dirtyKeys.count(a_key) > 0;
    }

    // --- Parsing helpers ---

    static std::string Trim(const std::string& a_str) {
        auto start = a_str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = a_str.find_last_not_of(" \t\r\n");
        return a_str.substr(start, end - start + 1);
    }

    static bool ParseBool(const std::string& a_val, bool a_default) {
        auto v = Trim(a_val);
        if (v == "true" || v == "1" || v == "yes") return true;
        if (v == "false" || v == "0" || v == "no") return false;
        return a_default;
    }

    static int32_t ParseInt(const std::string& a_val, int32_t a_default) {
        try {
            return std::stoi(Trim(a_val));
        } catch (...) {
            return a_default;
        }
    }

    static float ParseFloat(const std::string& a_val, float a_default) {
        try {
            return std::stof(Trim(a_val));
        } catch (...) {
            return a_default;
        }
    }

    static uint32_t ParseHex(const std::string& a_val, uint32_t a_default) {
        auto v = Trim(a_val);
        // Strip optional 0x prefix
        if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
            v = v.substr(2);
        }
        try {
            return static_cast<uint32_t>(std::stoul(v, nullptr, 16));
        } catch (...) {
            return a_default;
        }
    }

    static std::vector<std::string> ParseCSV(const std::string& a_val) {
        std::vector<std::string> result;
        std::istringstream stream(a_val);
        std::string item;
        while (std::getline(stream, item, ',')) {
            auto trimmed = Trim(item);
            if (!trimmed.empty()) {
                result.push_back(trimmed);
            }
        }
        return result;
    }

    // --- Path resolution ---

    std::filesystem::path GetINIPath() {
        HMODULE hModule = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&GetINIPath), &hModule)) {
            return {};
        }

        wchar_t dllPath[MAX_PATH];
        if (GetModuleFileNameW(hModule, dllPath, MAX_PATH) == 0) {
            return {};
        }

        std::filesystem::path dir = std::filesystem::path(dllPath).parent_path();
        return dir / "SLID.ini";
    }

    std::filesystem::path GetCustomINIPath() {
        auto ini = GetINIPath();
        if (ini.empty()) return {};
        return ini.parent_path() / "SLIDCustom.ini";
    }

    std::filesystem::path GetDataDir() {
        auto ini = GetINIPath();
        if (ini.empty()) return {};
        return ini.parent_path() / "SLID";
    }

    std::filesystem::path GetUserDataDir() {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) return {};
        return *logDir / "SLID";
    }

    std::filesystem::path GetUserCustomINIPath() {
        auto dir = GetUserDataDir();
        if (dir.empty()) return {};
        return dir / "SLIDCustom.ini";
    }

    std::vector<std::filesystem::path> GetDataDirs() {
        std::vector<std::filesystem::path> dirs;
        auto gameDir = GetDataDir();
        if (!gameDir.empty()) dirs.push_back(gameDir);
        auto userDir = GetUserDataDir();
        if (!userDir.empty()) dirs.push_back(userDir);
        return dirs;
    }

    bool IsDataINI(const std::string& a_filename) {
        if (a_filename.size() < 9) return false;  // "SLID_X.ini" minimum
        auto lower = a_filename;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.find("slid_") != std::string::npos &&
               lower.size() >= 4 && lower.substr(lower.size() - 4) == ".ini";
    }

    // --- Load ---

    static void LoadFromFile(const std::filesystem::path& a_path, bool a_markDirty = false) {
        std::ifstream file(a_path);
        if (!file.is_open()) {
            logger::info("Settings: {} not found, skipping", a_path.string());
            return;
        }

        logger::info("Settings: loading {}", a_path.string());

        std::string currentSection;
        std::string line;
        uint32_t lineNum = 0;

        while (std::getline(file, line)) {
            ++lineNum;

            // Strip comment (first ; or # not inside a value)
            auto commentPos = line.find_first_of(";#");
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }

            line = Trim(line);
            if (line.empty()) continue;

            // Section header
            if (line.front() == '[' && line.back() == ']') {
                currentSection = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            // Key = Value
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                logger::warn("Settings: {}:{}: malformed (no '='): {}", a_path.filename().string(), lineNum, line);
                continue;
            }

            auto key = Trim(line.substr(0, eqPos));
            auto val = Trim(line.substr(eqPos + 1));

            // Track whether this key matched a known setting
            bool matched = false;

            if (currentSection == "General") {
                if (key == "bModEnabled") {
                    bModEnabled = ParseBool(val, bModEnabled); matched = true;
                } else if (key == "bDebugLogging") {
                    bDebugLogging = ParseBool(val, bDebugLogging); matched = true;
                } else if (key == "bShownWelcomeTutorial") {
                    bShownWelcomeTutorial = ParseBool(val, bShownWelcomeTutorial); matched = true;
                } else if (key == "bInterceptActivation") {
                    bInterceptActivation = ParseBool(val, bInterceptActivation); matched = true;
                }
            } else if (currentSection == "Powers") {
                if (key == "bSummonEnabled") {
                    bSummonEnabled = ParseBool(val, bSummonEnabled); matched = true;
                }
            } else if (currentSection == "Containers") {
                if (key == "sGenericContainerNames") {
                    auto parsed = ParseCSV(val);
                    if (!parsed.empty()) {
                        sGenericContainerNames = std::move(parsed);
                    }
                }
            } else if (currentSection == "Categories") {
                if (key == "uCraftingCookpot")        uCraftingCookpot   = ParseHex(val, uCraftingCookpot);
                else if (key == "sCookpotPlugin")      sCookpotPlugin     = Trim(val);
                else if (key == "uCraftingSmelter")     uCraftingSmelter   = ParseHex(val, uCraftingSmelter);
                else if (key == "sSmelterPlugin")       sSmelterPlugin     = Trim(val);
                else if (key == "uCraftingCarpenter")   uCraftingCarpenter = ParseHex(val, uCraftingCarpenter);
                else if (key == "sCarpenterPlugin")     sCarpenterPlugin   = Trim(val);
                else if (key == "sKeywordPlugin")       sKeywordPlugin     = Trim(val);
                else if (key == "uVendorItemAnimalHide") uVendorItemAnimalHide = ParseHex(val, uVendorItemAnimalHide);
                else if (key == "uVendorItemAnimalPart") uVendorItemAnimalPart = ParseHex(val, uVendorItemAnimalPart);
                else if (key == "uVendorItemOreIngot")   uVendorItemOreIngot   = ParseHex(val, uVendorItemOreIngot);
                else if (key == "uVendorItemGem")        uVendorItemGem        = ParseHex(val, uVendorItemGem);
                else if (key == "uVendorItemKey")        uVendorItemKey        = ParseHex(val, uVendorItemKey);
            } else if (currentSection == "Sales") {
                if (key == "fSellPricePercent")       { fSellPricePercent  = ParseFloat(val, fSellPricePercent); matched = true; }
                else if (key == "iSellBatchSize")     { iSellBatchSize     = ParseInt(val, iSellBatchSize); matched = true; }
                else if (key == "fSellIntervalHours") { fSellIntervalHours = ParseFloat(val, fSellIntervalHours); matched = true; }
            } else if (currentSection == "VendorSales") {
                if (key == "fVendorPricePercent")       { fVendorPricePercent  = ParseFloat(val, fVendorPricePercent); matched = true; }
                else if (key == "iVendorBatchSize")     { iVendorBatchSize     = ParseInt(val, iVendorBatchSize); matched = true; }
                else if (key == "fVendorIntervalHours") { fVendorIntervalHours = ParseFloat(val, fVendorIntervalHours); matched = true; }
                else if (key == "iVendorCost")          { iVendorCost          = ParseInt(val, iVendorCost); matched = true; }
            } else if (currentSection == "ContainerPicker") {
                if (key == "bIncludeUnlinkedContainers") { bIncludeUnlinkedContainers = ParseBool(val, bIncludeUnlinkedContainers); matched = true; }
            } else if (currentSection == "Compatibility") {
                if (key == "bSCIEIntegration")          { bSCIEIntegration = ParseBool(val, bSCIEIntegration); matched = true; }
                else if (key == "bSCIEIncludeContainers") { bSCIEIncludeContainers = ParseBool(val, bSCIEIncludeContainers); matched = true; }
            }

            if (a_markDirty && matched) {
                MarkDirty(key.c_str());
            }
        }
    }

    void Load() {
        auto basePath = GetINIPath();
        if (basePath.empty()) {
            logger::warn("Settings: could not determine INI path");
            return;
        }

        // Load shipped defaults first
        LoadFromFile(basePath);

        // Overlay user overrides from legacy location (next to DLL) — mark as dirty
        LoadFromFile(GetCustomINIPath(), true);

        // Overlay user overrides from Documents (new canonical location, wins over legacy)
        LoadFromFile(GetUserCustomINIPath(), true);

        logger::info("Settings: loaded (debug={}, genericNames={}, sellPrice={:.0f}%)",
                     bDebugLogging, sGenericContainerNames.size(), fSellPricePercent * 100.0f);
        logger::info("Settings: game data dir = {}", GetDataDir().string());
        logger::info("Settings: user data dir = {}", GetUserDataDir().string());
    }

    // --- Unique Items INI loader ---
    //
    // Format: Plugin.esm|0xFormID|group = True ; Comment
    //   - Plugin: ESM/ESP file name
    //   - FormID: hex with optional 0x prefix (load-order byte masked off)
    //   - group: optional child filter ID (e.g. "daedric_artifacts")
    //   - Value is ignored (convention: "True")
    //
    // All entries go into uniqueItemFormIDs (root filter).
    // Entries with a group also go into uniqueItemGroups[group] (child filter).

    void LoadUniqueItems() {
        auto dataDirs = GetDataDirs();
        if (dataDirs.empty()) {
            logger::warn("Settings::LoadUniqueItems: could not determine data dirs");
            return;
        }

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("Settings::LoadUniqueItems: TESDataHandler not available");
            return;
        }

        uint32_t totalEntries = 0;
        uint32_t resolved = 0;
        uint32_t fileCount = 0;

        // Scan for all *SLID_*.ini files across both directories
        // Game dir first, user dir second (overlay semantic)
        std::vector<std::filesystem::path> iniFiles;
        for (const auto& dir : dataDirs) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file()) continue;
                std::string filename;
                try { filename = entry.path().filename().string(); }
                catch (const std::system_error&) { continue; }
                if (!IsDataINI(filename)) continue;
                iniFiles.push_back(entry.path());
            }
        }

        for (const auto& filePath : iniFiles) {
            std::string filename;
            try { filename = filePath.filename().string(); }
            catch (const std::system_error&) { continue; }

            logger::info("Settings: loading unique items from {}", filename);
            ++fileCount;

            std::ifstream file(filePath);
            if (!file.is_open()) continue;

            std::string line;
            enum class Section { None, UniqueItems, DisplayNames } currentSection = Section::None;

            while (std::getline(file, line)) {
                // Strip comments
                auto commentPos = line.find_first_of(";#");
                if (commentPos != std::string::npos) {
                    line = line.substr(0, commentPos);
                }
                line = Trim(line);
                if (line.empty()) continue;

                // Section header
                if (line.front() == '[' && line.back() == ']') {
                    auto section = Trim(line.substr(1, line.size() - 2));
                    if (section == "UniqueItems")       currentSection = Section::UniqueItems;
                    else if (section == "DisplayNames") currentSection = Section::DisplayNames;
                    else                                currentSection = Section::None;
                    continue;
                }

                auto eqPos = line.find('=');
                if (eqPos == std::string::npos) continue;
                auto key = Trim(line.substr(0, eqPos));
                if (key.empty()) continue;

                if (currentSection == Section::DisplayNames) {
                    // Parse: id|Display Name|Description = True
                    auto pipe1 = key.find('|');
                    if (pipe1 == std::string::npos) continue;
                    auto id = Trim(key.substr(0, pipe1));
                    auto rest = key.substr(pipe1 + 1);

                    std::string displayName;
                    std::string description;
                    auto pipe2 = rest.find('|');
                    if (pipe2 == std::string::npos) {
                        displayName = Trim(rest);
                    } else {
                        displayName = Trim(rest.substr(0, pipe2));
                        description = Trim(rest.substr(pipe2 + 1));
                    }

                    if (!id.empty() && !displayName.empty()) {
                        uniqueItemFilterMeta[id] = {displayName, description};
                    }
                    continue;
                }

                if (currentSection != Section::UniqueItems) continue;

                // Parse: Plugin.esm|0xFormID|group = True
                // Split key by '|'
                std::string plugin;
                std::string formIDStr;
                std::string group;

                auto pipe1 = key.find('|');
                if (pipe1 == std::string::npos) continue;  // Need at least Plugin|FormID

                plugin = Trim(key.substr(0, pipe1));
                auto rest = key.substr(pipe1 + 1);

                auto pipe2 = rest.find('|');
                if (pipe2 == std::string::npos) {
                    formIDStr = Trim(rest);
                } else {
                    formIDStr = Trim(rest.substr(0, pipe2));
                    group = Trim(rest.substr(pipe2 + 1));
                }

                if (plugin.empty() || formIDStr.empty()) continue;

                auto localID = ParseHex(formIDStr, 0);
                if (localID == 0) continue;

                ++totalEntries;

                // Mask off the load-order index
                uint32_t maskedID = localID & 0x00FFFFFF;

                auto* form = dh->LookupForm(maskedID, plugin);
                if (form) {
                    auto runtimeID = form->GetFormID();
                    uniqueItemFormIDs.insert(runtimeID);
                    if (!group.empty()) {
                        uniqueItemGroups[group].insert(runtimeID);
                    }
                    ++resolved;
                }
                // Silently skip entries whose plugin isn't loaded
            }
        }

        logger::info("Settings: unique items loaded — {}/{} FormIDs resolved from {} SLID_*.ini file(s)",
            resolved, totalEntries, fileCount);
        for (const auto& [groupID, formIDs] : uniqueItemGroups) {
            logger::info("  group '{}': {} items", groupID, formIDs.size());
        }
    }

    // --- Save ---

    // Helper: write a section header only once, on first dirty key in that section
    struct SectionWriter {
        std::ofstream& file;
        std::string current;
        void Ensure(const char* a_section) {
            if (current != a_section) {
                current = a_section;
                file << "[" << a_section << "]\n";
            }
        }
    };

    void Save() {
        if (s_dirtyKeys.empty()) {
            logger::info("Settings::Save: nothing dirty, skipping");
            return;
        }

        auto path = GetUserCustomINIPath();
        if (path.empty()) {
            logger::warn("Settings::Save: could not determine custom INI path");
            return;
        }

        // Ensure user data directory exists
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            logger::error("Settings::Save: failed to create directory {}: {}",
                          path.parent_path().string(), ec.message());
            return;
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            logger::error("Settings::Save: failed to open {} for writing", path.string());
            return;
        }

        file << "; SLID User Settings — Auto-generated, survives mod updates.\n";
        file << "; Only contains settings you changed. Delete to reset to defaults.\n\n";

        SectionWriter sw{file, ""};

        // [General] — bModEnabled and bDebugLogging are global (SLID.ini only), never saved here
        if (IsDirty("bShownWelcomeTutorial")) { sw.Ensure("General"); file << "bShownWelcomeTutorial = " << (bShownWelcomeTutorial ? "true" : "false") << "\n"; }
        if (IsDirty("bInterceptActivation"))  { sw.Ensure("General"); file << "bInterceptActivation = " << (bInterceptActivation ? "true" : "false") << "\n"; }

        // [Powers]
        if (IsDirty("bSummonEnabled")) { sw.Ensure("Powers"); file << "bSummonEnabled = " << (bSummonEnabled ? "true" : "false") << "\n"; }

        // [Sales]
        if (IsDirty("fSellPricePercent"))  { sw.Ensure("Sales"); file << "fSellPricePercent = " << std::fixed << std::setprecision(2) << fSellPricePercent << "\n"; }
        if (IsDirty("iSellBatchSize"))     { sw.Ensure("Sales"); file << "iSellBatchSize = " << iSellBatchSize << "\n"; }
        if (IsDirty("fSellIntervalHours")) { sw.Ensure("Sales"); file << "fSellIntervalHours = " << std::fixed << std::setprecision(1) << fSellIntervalHours << "\n"; }

        // [VendorSales]
        if (IsDirty("fVendorPricePercent"))  { sw.Ensure("VendorSales"); file << "fVendorPricePercent = " << std::fixed << std::setprecision(2) << fVendorPricePercent << "\n"; }
        if (IsDirty("iVendorBatchSize"))     { sw.Ensure("VendorSales"); file << "iVendorBatchSize = " << iVendorBatchSize << "\n"; }
        if (IsDirty("fVendorIntervalHours")) { sw.Ensure("VendorSales"); file << "fVendorIntervalHours = " << std::fixed << std::setprecision(1) << fVendorIntervalHours << "\n"; }
        if (IsDirty("iVendorCost"))          { sw.Ensure("VendorSales"); file << "iVendorCost = " << iVendorCost << "\n"; }

        // [ContainerPicker]
        if (IsDirty("bIncludeUnlinkedContainers")) { sw.Ensure("ContainerPicker"); file << "bIncludeUnlinkedContainers = " << (bIncludeUnlinkedContainers ? "true" : "false") << "\n"; }

        // [Compatibility]
        if (IsDirty("bSCIEIntegration"))          { sw.Ensure("Compatibility"); file << "bSCIEIntegration = " << (bSCIEIntegration ? "true" : "false") << "\n"; }
        if (IsDirty("bSCIEIncludeContainers"))    { sw.Ensure("Compatibility"); file << "bSCIEIncludeContainers = " << (bSCIEIncludeContainers ? "true" : "false") << "\n"; }

        file.close();
        logger::info("Settings::Save: wrote {} ({} overrides)", path.string(), s_dirtyKeys.size());
    }

    // --- Setters (assign + mark dirty + save) ---

    void SetShownWelcomeTutorial(bool a_val) {
        bShownWelcomeTutorial = a_val;
        MarkDirty("bShownWelcomeTutorial");
        Save();
    }

    void SetInterceptActivation(bool a_val) {
        bInterceptActivation = a_val;
        MarkDirty("bInterceptActivation");
        Save();
    }

    void SetSummonEnabled(bool a_val) {
        bSummonEnabled = a_val;
        MarkDirty("bSummonEnabled");
        Save();
    }

    void SetIncludeUnlinkedContainers(bool a_val) {
        bIncludeUnlinkedContainers = a_val;
        MarkDirty("bIncludeUnlinkedContainers");
        Save();
    }

    void SetSCIEIntegration(bool a_val) {
        bSCIEIntegration = a_val;
        MarkDirty("bSCIEIntegration");
        Save();
    }

    void SetSCIEIncludeContainers(bool a_val) {
        bSCIEIncludeContainers = a_val;
        MarkDirty("bSCIEIncludeContainers");
        Save();
    }

    void SetSellPricePercent(float a_val) {
        fSellPricePercent = std::clamp(a_val, 0.0f, 1.0f);
        MarkDirty("fSellPricePercent");
        Save();
    }

    void SetSellBatchSize(int32_t a_val) {
        iSellBatchSize = std::max(1, a_val);
        MarkDirty("iSellBatchSize");
        Save();
    }

    void SetSellIntervalHours(float a_val) {
        fSellIntervalHours = std::max(1.0f, a_val);
        MarkDirty("fSellIntervalHours");
        Save();
    }

    void SetVendorPricePercent(float a_val) {
        fVendorPricePercent = std::clamp(a_val, 0.0f, 1.0f);
        MarkDirty("fVendorPricePercent");
        Save();
    }

    void SetVendorBatchSize(int32_t a_val) {
        iVendorBatchSize = std::max(1, a_val);
        MarkDirty("iVendorBatchSize");
        Save();
    }

    void SetVendorIntervalHours(float a_val) {
        fVendorIntervalHours = std::max(1.0f, a_val);
        MarkDirty("fVendorIntervalHours");
        Save();
    }

    void SetVendorCost(int32_t a_val) {
        iVendorCost = std::max(0, a_val);
        MarkDirty("iVendorCost");
        Save();
    }
}
