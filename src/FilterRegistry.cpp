#include "FilterRegistry.h"
#include "TraitEvaluator.h"
#include "Settings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

    // -----------------------------------------------------------------------
    // Parsing helpers (duplicated from Settings.cpp — trivial, not worth sharing)
    // -----------------------------------------------------------------------

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

    // -----------------------------------------------------------------------
    // FormType string→enum map (for the FormType INI key)
    // -----------------------------------------------------------------------

    static const std::unordered_map<std::string, RE::FormType> s_formTypeMap = {
        {"Weapon",      RE::FormType::Weapon},
        {"Armor",       RE::FormType::Armor},
        {"Book",        RE::FormType::Book},
        {"Scroll",      RE::FormType::Scroll},
        {"AlchemyItem", RE::FormType::AlchemyItem},
        {"Ingredient",  RE::FormType::Ingredient},
        {"Ammo",        RE::FormType::Ammo},
        {"KeyMaster",   RE::FormType::KeyMaster},
        {"SoulGem",     RE::FormType::SoulGem},
        {"Misc",        RE::FormType::Misc},
        {"Light",       RE::FormType::Light},
    };

    // -----------------------------------------------------------------------
    // INIFilter — single class replacing all 50 per-filter classes
    // -----------------------------------------------------------------------

    class INIFilter : public IFilter {
    public:
        INIFilter(std::string a_id, std::string a_displayName,
                  std::string a_description,
                  std::vector<RE::FormType> a_formTypes,
                  std::vector<std::string> a_requireTraits,
                  std::vector<std::string> a_excludeTraits,
                  std::vector<std::string> a_requireAnyTraits,
                  bool a_defaultExclude)
            : m_id(std::move(a_id))
            , m_displayName(std::move(a_displayName))
            , m_description(std::move(a_description))
            , m_formTypes(std::move(a_formTypes))
            , m_requireTraits(std::move(a_requireTraits))
            , m_excludeTraits(std::move(a_excludeTraits))
            , m_requireAnyTraits(std::move(a_requireAnyTraits))
            , m_defaultExclude(a_defaultExclude)
        {}

        std::string_view GetID() const override { return m_id; }
        std::string_view GetDisplayName() const override { return m_displayName; }
        std::string_view GetDescription() const override { return m_description; }

        const IFilter* GetParent() const override { return m_parent; }

        void BindContainer(RE::FormID a_containerFormID) const override {
            m_containerFormID = a_containerFormID;
        }

        RE::FormID GetContainer() const override { return m_containerFormID; }

        bool Matches(RE::TESBoundObject* a_item) const override {
            if (!a_item) return false;

            // FormType gate — if specified, item must match at least one
            if (!m_formTypes.empty()) {
                auto ft = a_item->GetFormType();
                bool found = false;
                for (auto t : m_formTypes) {
                    if (ft == t) { found = true; break; }
                }
                if (!found) return false;
            }

            // RequireTrait — all must pass
            for (const auto& trait : m_requireTraits) {
                if (!TraitEvaluator::Evaluate(trait, a_item)) return false;
            }

            // ExcludeTrait — none must pass
            for (const auto& trait : m_excludeTraits) {
                if (TraitEvaluator::Evaluate(trait, a_item)) return false;
            }

            // RequireAnyTrait — at least one must pass (if non-empty)
            if (!m_requireAnyTraits.empty()) {
                bool anyPassed = false;
                for (const auto& trait : m_requireAnyTraits) {
                    if (TraitEvaluator::Evaluate(trait, a_item)) {
                        anyPassed = true;
                        break;
                    }
                }
                if (!anyPassed) return false;
            }

            return true;
        }

        RE::FormID Route(RE::TESBoundObject* a_item) const override {
            // Check children first
            auto* reg = FilterRegistry::GetSingleton();
            const auto& children = reg->GetChildren(std::string(m_id));
            for (const auto& childID : children) {
                auto* child = reg->GetFilter(childID);
                if (child && child->Matches(a_item)) {
                    auto cid = child->GetContainer();
                    if (cid != 0) return cid;
                }
            }
            // Then self
            if (Matches(a_item)) {
                if (m_containerFormID != 0) return m_containerFormID;
            }
            return 0;
        }

        void SetParent(const IFilter* a_parent) { m_parent = a_parent; }
        bool IsDefaultExclude() const { return m_defaultExclude; }

    private:
        std::string m_id;
        std::string m_displayName;
        std::string m_description;
        const IFilter* m_parent = nullptr;
        mutable RE::FormID m_containerFormID = 0;

        std::vector<RE::FormType> m_formTypes;
        std::vector<std::string> m_requireTraits;
        std::vector<std::string> m_excludeTraits;
        std::vector<std::string> m_requireAnyTraits;
        bool m_defaultExclude = false;
    };

    // -----------------------------------------------------------------------
    // Parsed filter definition (intermediate, before creating INIFilter)
    // -----------------------------------------------------------------------

    struct FilterDef {
        std::string id;
        std::string displayName;
        std::string description;
        std::string parentID;
        std::string requirePlugin;
        bool enabled = true;
        bool defaultExclude = false;
        std::vector<std::string> formTypeNames;
        std::vector<std::string> requireTraits;
        std::vector<std::string> excludeTraits;
        std::vector<std::string> requireAnyTraits;
    };

    // -----------------------------------------------------------------------
    // INI parser — scans SLID_*.ini files for [Filter:ID] sections
    // -----------------------------------------------------------------------

    static std::vector<FilterDef> ParseFilterINIs() {
        auto iniPath = Settings::GetINIPath();
        if (iniPath.empty()) {
            logger::warn("FilterRegistry: could not determine INI path for filter definitions");
            return {};
        }

        auto dir = iniPath.parent_path();

        // Collect matching files, sorted alphabetically for deterministic order
        std::vector<std::filesystem::path> iniFiles;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            auto filename = entry.path().filename().string();
            if (filename.size() < 9) continue;  // "SLID_X.ini" minimum
            auto lower = filename;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("slid_") == std::string::npos || lower.substr(lower.size() - 4) != ".ini") continue;
            // Skip mod author export file — it's meant to be renamed and shipped, not loaded as-is
            if (lower == "slid_modauthorexport.ini") continue;
            iniFiles.push_back(entry.path());
        }
        std::sort(iniFiles.begin(), iniFiles.end());

        // Map from filter ID to definition (last writer wins)
        std::unordered_map<std::string, FilterDef> defMap;
        // Track insertion order for deterministic output
        std::vector<std::string> insertionOrder;

        for (const auto& filePath : iniFiles) {
            std::ifstream file(filePath);
            if (!file.is_open()) continue;

            logger::info("FilterRegistry: scanning {} for filter definitions", filePath.filename().string());

            std::string line;
            std::string currentFilterID;  // empty = not in a [Filter:X] section

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
                    // Check for [Filter:ID] pattern
                    if (section.size() > 7 && section.substr(0, 7) == "Filter:") {
                        currentFilterID = Trim(section.substr(7));
                        if (!currentFilterID.empty() && defMap.find(currentFilterID) == defMap.end()) {
                            insertionOrder.push_back(currentFilterID);
                            defMap[currentFilterID].id = currentFilterID;
                        } else if (!currentFilterID.empty()) {
                            // Overwrite existing — keep in same order position
                            defMap[currentFilterID] = FilterDef{};
                            defMap[currentFilterID].id = currentFilterID;
                        }
                    } else {
                        currentFilterID.clear();
                    }
                    continue;
                }

                if (currentFilterID.empty()) continue;

                // Key = Value
                auto eqPos = line.find('=');
                if (eqPos == std::string::npos) continue;

                auto key = Trim(line.substr(0, eqPos));
                auto val = Trim(line.substr(eqPos + 1));

                auto& def = defMap[currentFilterID];

                if (key == "Enabled")          def.enabled = ParseBool(val, true);
                else if (key == "Display")     def.displayName = val;
                else if (key == "Description") def.description = val;
                else if (key == "Parent")      def.parentID = val;
                else if (key == "FormType")    def.formTypeNames = ParseCSV(val);
                else if (key == "RequireTrait")    def.requireTraits = ParseCSV(val);
                else if (key == "ExcludeTrait")    def.excludeTraits = ParseCSV(val);
                else if (key == "RequireAnyTrait") def.requireAnyTraits = ParseCSV(val);
                else if (key == "DefaultExclude")  def.defaultExclude = ParseBool(val, false);
                else if (key == "RequirePlugin")   def.requirePlugin = val;
            }
        }

        // Build result in insertion order
        std::vector<FilterDef> result;
        for (const auto& id : insertionOrder) {
            auto it = defMap.find(id);
            if (it != defMap.end()) {
                result.push_back(std::move(it->second));
            }
        }

        return result;
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// FilterRegistry singleton
// ---------------------------------------------------------------------------

const std::vector<std::string> FilterRegistry::s_emptyChildren;

FilterRegistry* FilterRegistry::GetSingleton() {
    static FilterRegistry instance;
    return &instance;
}

void FilterRegistry::Init() {
    // Parse all [Filter:ID] sections from SLID_*.ini files
    auto defs = ParseFilterINIs();

    if (defs.empty()) {
        logger::error("FilterRegistry: no filter definitions found in SLID_*.ini files");
        return;
    }

    // Track default-excluded filter IDs
    std::unordered_set<std::string> defaultExcluded;

    // First pass: create INIFilter instances for all enabled entries
    for (auto& def : defs) {
        if (!def.enabled) {
            logger::debug("FilterRegistry: filter '{}' disabled, skipping", def.id);
            continue;
        }

        if (!def.requirePlugin.empty()) {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh || !dh->LookupModByName(def.requirePlugin)) {
                logger::debug("FilterRegistry: filter '{}' requires plugin '{}' which is not loaded, skipping",
                    def.id, def.requirePlugin);
                continue;
            }
        }

        // Resolve FormType names to enums
        std::vector<RE::FormType> formTypes;
        for (const auto& ftName : def.formTypeNames) {
            auto it = s_formTypeMap.find(ftName);
            if (it != s_formTypeMap.end()) {
                formTypes.push_back(it->second);
            } else {
                logger::warn("FilterRegistry: filter '{}' has unknown FormType '{}'", def.id, ftName);
            }
        }

        auto filter = std::make_unique<INIFilter>(
            def.id,
            def.displayName,
            def.description,
            std::move(formTypes),
            std::move(def.requireTraits),
            std::move(def.excludeTraits),
            std::move(def.requireAnyTraits),
            def.defaultExclude
        );

        if (def.defaultExclude) {
            defaultExcluded.insert(def.id);
        }

        m_order.push_back(def.id);
        m_filters[def.id] = std::move(filter);
    }

    // Second pass: resolve parent pointers
    for (const auto& def : defs) {
        if (!def.enabled || def.parentID.empty()) continue;

        auto filterIt = m_filters.find(def.id);
        if (filterIt == m_filters.end()) continue;

        auto parentIt = m_filters.find(def.parentID);
        if (parentIt == m_filters.end()) {
            logger::warn("FilterRegistry: filter '{}' references parent '{}' which is not found/enabled — treating as root",
                def.id, def.parentID);
            continue;
        }

        static_cast<INIFilter*>(filterIt->second.get())->SetParent(parentIt->second.get());
    }

    // Build family index
    for (const auto& id : m_order) {
        auto* filter = GetFilter(id);
        if (!filter) continue;
        auto* parent = filter->GetParent();
        if (parent) {
            std::string parentID(parent->GetID());
            m_children[parentID].push_back(id);
        } else {
            m_familyRoots.push_back(id);
        }
    }

    // Store default-excluded set
    m_defaultExcluded = std::move(defaultExcluded);

    // Log summary
    logger::info("FilterRegistry: initialized {} filters ({} roots, {} families with children)",
        m_filters.size(), m_familyRoots.size(), m_children.size());
    for (const auto& [rootID, kids] : m_children) {
        logger::info("  family '{}': {} children ({})", rootID, kids.size(),
            [&]() {
                std::string s;
                for (size_t i = 0; i < kids.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += kids[i];
                }
                return s;
            }());
    }
}

const IFilter* FilterRegistry::GetFilter(const std::string& a_id) const {
    auto it = m_filters.find(a_id);
    if (it != m_filters.end()) {
        return it->second.get();
    }
    return nullptr;
}

const std::vector<std::string>& FilterRegistry::GetFilterOrder() const {
    return m_order;
}

size_t FilterRegistry::GetFilterCount() const {
    return m_filters.size();
}

const std::vector<std::string>& FilterRegistry::GetChildren(const std::string& a_rootID) const {
    auto it = m_children.find(a_rootID);
    if (it != m_children.end()) {
        return it->second;
    }
    return s_emptyChildren;
}

const std::vector<std::string>& FilterRegistry::GetFamilyRoots() const {
    return m_familyRoots;
}

std::unordered_set<std::string> FilterRegistry::DefaultWhooshFilters() {
    auto* reg = GetSingleton();
    std::unordered_set<std::string> result;
    for (const auto& id : reg->m_order) {
        if (reg->m_defaultExcluded.count(id) == 0) {
            result.insert(id);
        }
    }
    return result;
}

void FilterRegistry::DumpToLog() const {
    logger::info("=== FilterRegistry Dump ===");
    logger::info("Total filters: {}", m_filters.size());
    logger::info("Family roots: {}", m_familyRoots.size());

    for (const auto& rootID : m_familyRoots) {
        auto* root = GetFilter(rootID);
        if (!root) continue;

        logger::info("  [{}] {}", rootID, root->GetDisplayName());
        logger::info("    Description: {}", root->GetDescription());

        const auto& children = GetChildren(rootID);
        for (const auto& childID : children) {
            auto* child = GetFilter(childID);
            if (!child) continue;
            logger::info("    - [{}] {}", childID, child->GetDisplayName());
        }
    }

    logger::info("Default excluded from Whoosh: {}", m_defaultExcluded.size());
    for (const auto& id : m_defaultExcluded) {
        logger::info("  - {}", id);
    }
    logger::info("=== End FilterRegistry Dump ===");
}
