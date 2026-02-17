#include "Settings.h"

#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Settings {

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

    // --- Load ---

    void Load() {
        auto path = GetINIPath();
        if (path.empty()) {
            logger::warn("Settings: could not determine INI path");
            return;
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            logger::info("Settings: {} not found, using defaults", path.string());
            return;
        }

        logger::info("Settings: loading {}", path.string());

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
                logger::warn("Settings: line {}: malformed (no '='): {}", lineNum, line);
                continue;
            }

            auto key = Trim(line.substr(0, eqPos));
            auto val = Trim(line.substr(eqPos + 1));

            if (currentSection == "General") {
                if (key == "bModEnabled") {
                    bModEnabled = ParseBool(val, bModEnabled);
                } else if (key == "bDebugLogging") {
                    bDebugLogging = ParseBool(val, bDebugLogging);
                } else if (key == "bShownWelcomeTutorial") {
                    bShownWelcomeTutorial = ParseBool(val, bShownWelcomeTutorial);
                }
            } else if (currentSection == "Powers") {
                if (key == "bSummonEnabled") {
                    bSummonEnabled = ParseBool(val, bSummonEnabled);
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
                if (key == "fSellPricePercent")       fSellPricePercent  = ParseFloat(val, fSellPricePercent);
                else if (key == "iSellBatchSize")     iSellBatchSize     = ParseInt(val, iSellBatchSize);
                else if (key == "fSellIntervalHours") fSellIntervalHours = ParseFloat(val, fSellIntervalHours);
            } else if (currentSection == "VendorSales") {
                if (key == "fVendorPricePercent")       fVendorPricePercent  = ParseFloat(val, fVendorPricePercent);
                else if (key == "iVendorBatchSize")     iVendorBatchSize     = ParseInt(val, iVendorBatchSize);
                else if (key == "fVendorIntervalHours") fVendorIntervalHours = ParseFloat(val, fVendorIntervalHours);
                else if (key == "iVendorCost")          iVendorCost          = ParseInt(val, iVendorCost);
            } else if (currentSection == "ContainerPicker") {
                if (key == "bIncludeUnlinkedContainers") bIncludeUnlinkedContainers = ParseBool(val, bIncludeUnlinkedContainers);
            } else if (currentSection == "Compatibility") {
                if (key == "bSCIEIntegration")          bSCIEIntegration = ParseBool(val, bSCIEIntegration);
                else if (key == "bSCIEIncludeContainers") bSCIEIncludeContainers = ParseBool(val, bSCIEIncludeContainers);
            }
        }

        logger::info("Settings: loaded (debug={}, genericNames={}, sellPrice={:.0f}%)",
                     bDebugLogging, sGenericContainerNames.size(), fSellPricePercent * 100.0f);
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
        auto iniPath = GetINIPath();
        if (iniPath.empty()) {
            logger::warn("Settings::LoadUniqueItems: could not determine INI path");
            return;
        }

        auto dir = iniPath.parent_path();
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("Settings::LoadUniqueItems: TESDataHandler not available");
            return;
        }

        uint32_t totalEntries = 0;
        uint32_t resolved = 0;
        uint32_t fileCount = 0;

        // Scan for all SLID_*.ini files
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto filename = entry.path().filename().string();
            // Must start with "SLID_" and end with ".ini" (case-insensitive)
            if (filename.size() < 9) continue;  // "SLID_X.ini" minimum
            auto lower = filename;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.substr(0, 5) != "slid_" || lower.substr(lower.size() - 4) != ".ini") continue;
            // Skip mod author export file — it's meant to be renamed and shipped, not loaded as-is
            if (lower == "slid_modauthorexport.ini") continue;

            logger::info("Settings: loading unique items from {}", filename);
            ++fileCount;

            std::ifstream file(entry.path());
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

    void Save() {
        auto path = GetINIPath();
        if (path.empty()) {
            logger::warn("Settings::Save: could not determine INI path");
            return;
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            logger::error("Settings::Save: failed to open {} for writing", path.string());
            return;
        }

        file << "; ============================================================================\n";
        file << "; SLID (Skyrim Linked Item Distribution) — Configuration\n";
        file << "; ============================================================================\n";
        file << ";\n";
        file << "; Edit values below to customize SLID behavior. Delete this file to reset\n";
        file << "; all settings to their defaults.\n";
        file << ";\n";
        file << "; Lines starting with ; or # are comments.\n";
        file << "; Hex values use 0x prefix (e.g. 0x000A5CB3).\n";
        file << "\n";

        file << "[General]\n";
        file << "\n";
        file << "; Master switch for mod functionality. When disabled, all mod features\n";
        file << "; are inactive (powers, container intercepts, sales). Data is preserved.\n";
        file << "; Default: true\n";
        file << "bModEnabled = " << (bModEnabled ? "true" : "false") << "\n";
        file << "\n";
        file << "; Enable verbose debug logging. Produces much more log output.\n";
        file << "; Default: false\n";
        file << "bDebugLogging = " << (bDebugLogging ? "true" : "false") << "\n";
        file << "\n";
        file << "; Has the user seen the welcome tutorial popup?\n";
        file << "; Set to false in MCM to show it again on next interaction.\n";
        file << "bShownWelcomeTutorial = " << (bShownWelcomeTutorial ? "true" : "false") << "\n";
        file << "\n";

        file << "[Powers]\n";
        file << "\n";
        file << "; Enable the Summon Chest power.\n";
        file << "; Default: true\n";
        file << "bSummonEnabled = " << (bSummonEnabled ? "true" : "false") << "\n";
        file << "\n";

        file << "[Containers]\n";
        file << "\n";
        file << "; Comma-separated list of generic container base names to hide from the\n";
        file << "; scanned container list.\n";
        file << "sGenericContainerNames = ";
        for (size_t i = 0; i < sGenericContainerNames.size(); ++i) {
            if (i > 0) file << ",";
            file << sGenericContainerNames[i];
        }
        file << "\n";
        file << "\n";

        file << "[Categories]\n";
        file << "\n";
        file << "; Crafting station keywords used for COBJ-based item categorization.\n";
        file << std::hex;
        file << "uCraftingCookpot = 0x" << std::setfill('0') << std::setw(8) << uCraftingCookpot << "\n";
        file << std::dec;
        file << "sCookpotPlugin = " << sCookpotPlugin << "\n";
        file << std::hex;
        file << "uCraftingSmelter = 0x" << std::setfill('0') << std::setw(8) << uCraftingSmelter << "\n";
        file << std::dec;
        file << "sSmelterPlugin = " << sSmelterPlugin << "\n";
        file << std::hex;
        file << "uCraftingCarpenter = 0x" << std::setfill('0') << std::setw(6) << uCraftingCarpenter << "\n";
        file << std::dec;
        file << "sCarpenterPlugin = " << sCarpenterPlugin << "\n";
        file << "sKeywordPlugin = " << sKeywordPlugin << "\n";
        file << std::hex;
        file << "uVendorItemAnimalHide = 0x" << std::setfill('0') << std::setw(6) << uVendorItemAnimalHide << "\n";
        file << "uVendorItemAnimalPart = 0x" << std::setfill('0') << std::setw(6) << uVendorItemAnimalPart << "\n";
        file << "uVendorItemOreIngot = 0x" << std::setfill('0') << std::setw(6) << uVendorItemOreIngot << "\n";
        file << "uVendorItemGem = 0x" << std::setfill('0') << std::setw(6) << uVendorItemGem << "\n";
        file << "uVendorItemKey = 0x" << std::setfill('0') << std::setw(6) << uVendorItemKey << "\n";
        file << std::dec;
        file << "\n";

        file << "[Sales]\n";
        file << "\n";
        file << "; Fraction of base price the player receives when selling (0.0 - 1.0).\n";
        file << "; Default: 0.10 (10%)\n";
        file << "fSellPricePercent = " << std::fixed << std::setprecision(2) << fSellPricePercent << "\n";
        file << "\n";
        file << "; Maximum number of items processed per sell batch.\n";
        file << "; Default: 10\n";
        file << "iSellBatchSize = " << iSellBatchSize << "\n";
        file << "\n";
        file << "; Minimum hours between automatic sell cycles.\n";
        file << "; Default: 24.0\n";
        file << "fSellIntervalHours = " << std::fixed << std::setprecision(1) << fSellIntervalHours << "\n";
        file << "\n";

        file << "[VendorSales]\n";
        file << "\n";
        file << "; Fraction of base price registered vendors pay (0.0 - 1.0).\n";
        file << "; Default: 0.25 (25%)\n";
        file << "fVendorPricePercent = " << std::fixed << std::setprecision(2) << fVendorPricePercent << "\n";
        file << "\n";
        file << "; Maximum items a single vendor buys per visit.\n";
        file << "; Default: 25\n";
        file << "iVendorBatchSize = " << iVendorBatchSize << "\n";
        file << "\n";
        file << "; Hours between registered vendor visits.\n";
        file << "; Default: 48.0\n";
        file << "fVendorIntervalHours = " << std::fixed << std::setprecision(1) << fVendorIntervalHours << "\n";
        file << "\n";
        file << "; Gold cost to establish a trade arrangement with a vendor.\n";
        file << "; Default: 5000\n";
        file << "iVendorCost = " << iVendorCost << "\n";
        file << "\n";

        file << "[ContainerPicker]\n";
        file << "\n";
        file << "; Include untagged containers from cell scan in the container picker.\n";
        file << "; When false, only tagged containers appear (cleaner picker).\n";
        file << "; Default: false\n";
        file << "bIncludeUnlinkedContainers = " << (bIncludeUnlinkedContainers ? "true" : "false") << "\n";
        file << "\n";

        file << "[Compatibility]\n";
        file << "\n";
        file << "; Enable integration with SCIE (Skyrim Crafting Inventory Extender).\n";
        file << "; Default: true\n";
        file << "bSCIEIntegration = " << (bSCIEIntegration ? "true" : "false") << "\n";
        file << "\n";
        file << "; Include SCIE containers in the Link container picker.\n";
        file << "; Default: true\n";
        file << "bSCIEIncludeContainers = " << (bSCIEIncludeContainers ? "true" : "false") << "\n";

        file.close();
        logger::info("Settings::Save: wrote {}", path.string());
    }
}
