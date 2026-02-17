#include "NetworkManager.h"
#include "Settings.h"

#include <algorithm>
#include <random>

NetworkManager* NetworkManager::GetSingleton() {
    static NetworkManager singleton;
    return &singleton;
}

std::vector<FilterStage> NetworkManager::BuildDefaultFilters() {
    return {};
}

bool NetworkManager::CreateNetwork(const std::string& a_name, RE::FormID a_masterFormID) {
    std::lock_guard lock(m_lock);

    // Check for duplicate name
    for (const auto& net : m_networks) {
        if (net.name == a_name) {
            logger::warn("Network '{}' already exists", a_name);
            return false;
        }
    }

    Network net;
    net.name = a_name;
    net.masterFormID = a_masterFormID;
    net.filters = BuildDefaultFilters();
    net.catchAllFormID = 0;  // default = master
    m_networks.push_back(std::move(net));

    logger::debug("Created network '{}' with master {:08X} and {} default filters",
                 a_name, a_masterFormID, net.filters.size());
    return true;
}

bool NetworkManager::RemoveNetwork(const std::string& a_name) {
    std::lock_guard lock(m_lock);

    auto it = std::find_if(m_networks.begin(), m_networks.end(),
                           [&](const Network& n) { return n.name == a_name; });
    if (it == m_networks.end()) {
        logger::warn("Network '{}' not found", a_name);
        return false;
    }

    m_networks.erase(it);
    logger::debug("Removed network '{}'", a_name);
    return true;
}

Network* NetworkManager::FindNetwork(const std::string& a_name) {
    std::lock_guard lock(m_lock);

    for (auto& net : m_networks) {
        if (net.name == a_name) {
            return &net;
        }
    }
    return nullptr;
}

Network* NetworkManager::FindNetworkUnsafe(const std::string& a_name) {
    for (auto& net : m_networks) {
        if (net.name == a_name) {
            return &net;
        }
    }
    return nullptr;
}

const std::vector<Network>& NetworkManager::GetNetworks() const {
    return m_networks;
}

void NetworkManager::SetFilterConfig(const std::string& a_networkName,
                                     const std::vector<FilterStage>& a_filters,
                                     RE::FormID a_catchAllFormID) {
    std::lock_guard lock(m_lock);

    auto* net = FindNetworkUnsafe(a_networkName);
    if (!net) {
        logger::warn("SetFilterConfig: network '{}' not found", a_networkName);
        return;
    }

    net->filters = a_filters;
    net->catchAllFormID = a_catchAllFormID;

    logger::debug("SetFilterConfig: network '{}' updated with {} filters, catchAll={:08X}",
                 a_networkName, a_filters.size(), a_catchAllFormID);
}

void NetworkManager::SetWhooshConfig(const std::string& a_networkName, const std::unordered_set<std::string>& a_filters) {
    std::lock_guard lock(m_lock);

    auto* net = FindNetworkUnsafe(a_networkName);
    if (!net) {
        logger::warn("SetWhooshConfig: network '{}' not found", a_networkName);
        return;
    }

    net->whooshFilters = a_filters;
    net->whooshConfigured = true;

    logger::debug("SetWhooshConfig: network '{}' with {} filters", a_networkName, a_filters.size());
}

void NetworkManager::ClearAll() {
    std::lock_guard lock(m_lock);
    m_networks.clear();
    m_tagRegistry.clear();
    m_recognizedMods.clear();
    m_sellState = SellContainerState{};
    m_transactionLog.clear();
    VendorRegistry::GetSingleton()->ClearAll();
    logger::info("ClearAll: all SLID data cleared");
}

// --- Tag registry ---

bool NetworkManager::TagContainer(RE::FormID a_formID, const std::string& a_customName) {
    std::lock_guard lock(m_lock);

    auto [it, inserted] = m_tagRegistry.insert_or_assign(a_formID, TaggedContainer{a_customName});
    if (inserted) {
        logger::debug("TagContainer: tagged {:08X} as '{}'", a_formID, a_customName);
    } else {
        logger::debug("TagContainer: updated {:08X} to '{}'", a_formID, a_customName);
    }
    return true;
}

bool NetworkManager::UntagContainer(RE::FormID a_formID) {
    std::lock_guard lock(m_lock);

    if (m_tagRegistry.erase(a_formID) == 0) {
        logger::warn("UntagContainer: {:08X} not in tag registry", a_formID);
        return false;
    }
    logger::debug("UntagContainer: removed {:08X} from tag registry", a_formID);
    return true;
}

bool NetworkManager::IsTagged(RE::FormID a_formID) const {
    std::lock_guard lock(m_lock);
    return m_tagRegistry.count(a_formID) > 0;
}

const std::unordered_map<RE::FormID, TaggedContainer>& NetworkManager::GetTagRegistry() const {
    return m_tagRegistry;
}

std::string NetworkManager::GetTagName(RE::FormID a_formID) const {
    std::lock_guard lock(m_lock);
    auto it = m_tagRegistry.find(a_formID);
    if (it != m_tagRegistry.end()) {
        return it->second.customName;
    }
    return "";
}

void NetworkManager::ClearContainerReferences(RE::FormID a_formID) {
    std::lock_guard lock(m_lock);

    for (auto& net : m_networks) {
        for (auto& filter : net.filters) {
            if (filter.containerFormID == a_formID) {
                filter.containerFormID = 0;
            }
        }
        if (net.catchAllFormID == a_formID) {
            net.catchAllFormID = 0;
        }
    }

    // Clear sell container if it matches
    if (m_sellState.formID == a_formID) {
        m_sellState.formID = 0;
        m_sellState.timerStarted = false;
        logger::info("ClearContainerReferences: cleared sell container {:08X}", a_formID);
    }

    logger::debug("ClearContainerReferences: cleared all references to {:08X}", a_formID);
}

// --- Sell container ---

void NetworkManager::SetSellContainer(RE::FormID a_formID) {
    std::lock_guard lock(m_lock);
    m_sellState.formID = a_formID;

    // Start cooldown immediately from designation to prevent toggle-rest exploits
    float now = 0.0f;
    if (auto* calendar = RE::Calendar::GetSingleton()) {
        now = calendar->GetHoursPassed();
    }
    m_sellState.timerStarted = true;
    m_sellState.lastSellTime = now;
    logger::info("SetSellContainer: designated {:08X}, cooldown started at {:.1f}h", a_formID, now);
}

void NetworkManager::ClearSellContainer() {
    std::lock_guard lock(m_lock);
    m_sellState.formID = 0;
    m_sellState.timerStarted = false;
    logger::info("ClearSellContainer: sell container cleared (lifetime: {} items, {} gold)",
                 m_sellState.totalItemsSold, m_sellState.totalGoldEarned);
}

RE::FormID NetworkManager::GetSellContainerFormID() const {
    std::lock_guard lock(m_lock);
    return m_sellState.formID;
}

bool NetworkManager::HasSellContainer() const {
    std::lock_guard lock(m_lock);
    return m_sellState.formID != 0;
}

const SellContainerState& NetworkManager::GetSellState() const {
    return m_sellState;
}

void NetworkManager::RecordSale(uint32_t a_itemCount, uint32_t a_goldAmount) {
    std::lock_guard lock(m_lock);
    m_sellState.totalItemsSold += a_itemCount;
    m_sellState.totalGoldEarned += a_goldAmount;
}

void NetworkManager::SetLastSellTime(float a_gameHours) {
    std::lock_guard lock(m_lock);
    m_sellState.lastSellTime = a_gameHours;
    m_sellState.timerStarted = true;
}

void NetworkManager::AppendTransactions(const std::vector<SaleTransaction>& a_transactions) {
    std::lock_guard lock(m_lock);
    m_transactionLog.insert(m_transactionLog.begin(), a_transactions.begin(), a_transactions.end());
    if (m_transactionLog.size() > kMaxTransactionLog) {
        m_transactionLog.resize(kMaxTransactionLog);
    }
}

const std::vector<SaleTransaction>& NetworkManager::GetTransactionLog() const {
    return m_transactionLog;
}

// --- Query methods ---

std::string NetworkManager::FindNetworkByMaster(RE::FormID a_masterFormID) const {
    std::lock_guard lock(m_lock);
    for (const auto& net : m_networks) {
        if (net.masterFormID == a_masterFormID) {
            return net.name;
        }
    }
    return "";
}

std::vector<std::string> NetworkManager::GetNetworkNames() const {
    std::lock_guard lock(m_lock);
    std::vector<std::string> names;
    names.reserve(m_networks.size());
    for (const auto& net : m_networks) {
        names.push_back(net.name);
    }
    return names;
}

// --- Cosave ---

void NetworkManager::OnGameSaved(SKSE::SerializationInterface* a_intfc) {
    GetSingleton()->Save(a_intfc);
}

void NetworkManager::OnGameLoaded(SKSE::SerializationInterface* a_intfc) {
    GetSingleton()->Load(a_intfc);
}

void NetworkManager::OnRevert(SKSE::SerializationInterface* a_intfc) {
    (void)a_intfc;
    GetSingleton()->Revert();
}

void NetworkManager::Save(SKSE::SerializationInterface* a_intfc) const {
    std::lock_guard lock(m_lock);

    // Write networks record
    if (!a_intfc->OpenRecord(kNetworkRecord, kNetworkVersion)) {
        logger::error("Failed to open NETW cosave record");
        return;
    }

    uint32_t netCount = static_cast<uint32_t>(m_networks.size());
    a_intfc->WriteRecordData(&netCount, sizeof(netCount));

    for (const auto& net : m_networks) {
        // Network name (length-prefixed)
        uint16_t nameLen = static_cast<uint16_t>(net.name.size());
        a_intfc->WriteRecordData(&nameLen, sizeof(nameLen));
        a_intfc->WriteRecordData(net.name.data(), nameLen);

        // Master FormID
        a_intfc->WriteRecordData(&net.masterFormID, sizeof(net.masterFormID));

        // Filters (v4: string ID + FormID)
        uint32_t filterCount = static_cast<uint32_t>(net.filters.size());
        a_intfc->WriteRecordData(&filterCount, sizeof(filterCount));

        for (const auto& filter : net.filters) {
            uint16_t idLen = static_cast<uint16_t>(filter.filterID.size());
            a_intfc->WriteRecordData(&idLen, sizeof(idLen));
            a_intfc->WriteRecordData(filter.filterID.data(), idLen);
            a_intfc->WriteRecordData(&filter.containerFormID, sizeof(filter.containerFormID));
        }

        // Catch-all
        a_intfc->WriteRecordData(&net.catchAllFormID, sizeof(net.catchAllFormID));

        // Whoosh config (v4: string set)
        // Sort for deterministic output
        std::vector<std::string> sortedWhoosh(net.whooshFilters.begin(), net.whooshFilters.end());
        std::sort(sortedWhoosh.begin(), sortedWhoosh.end());
        uint16_t whooshCount = static_cast<uint16_t>(sortedWhoosh.size());
        a_intfc->WriteRecordData(&whooshCount, sizeof(whooshCount));
        for (const auto& id : sortedWhoosh) {
            uint16_t idLen = static_cast<uint16_t>(id.size());
            a_intfc->WriteRecordData(&idLen, sizeof(idLen));
            a_intfc->WriteRecordData(id.data(), idLen);
        }
        uint8_t whooshConfigByte = net.whooshConfigured ? 1 : 0;
        a_intfc->WriteRecordData(&whooshConfigByte, sizeof(whooshConfigByte));
    }

    logger::info("Saved {} networks to cosave (v{})", netCount, kNetworkVersion);

    // Write tags record
    if (!a_intfc->OpenRecord(kTagsRecord, kTagsVersion)) {
        logger::error("Failed to open TAGS cosave record");
        return;
    }

    uint32_t tagCount = static_cast<uint32_t>(m_tagRegistry.size());
    a_intfc->WriteRecordData(&tagCount, sizeof(tagCount));

    for (const auto& [formID, tag] : m_tagRegistry) {
        a_intfc->WriteRecordData(&formID, sizeof(formID));
        uint16_t tagNameLen = static_cast<uint16_t>(tag.customName.size());
        a_intfc->WriteRecordData(&tagNameLen, sizeof(tagNameLen));
        a_intfc->WriteRecordData(tag.customName.data(), tagNameLen);
    }

    logger::info("Saved {} tagged containers to cosave", tagCount);

    // Write mods record
    if (!a_intfc->OpenRecord(kModsRecord, kModsVersion)) {
        logger::error("Failed to open MODS cosave record");
        return;
    }

    uint32_t modCount = static_cast<uint32_t>(m_recognizedMods.size());
    a_intfc->WriteRecordData(&modCount, sizeof(modCount));

    for (const auto& mod : m_recognizedMods) {
        uint16_t modLen = static_cast<uint16_t>(mod.size());
        a_intfc->WriteRecordData(&modLen, sizeof(modLen));
        a_intfc->WriteRecordData(mod.data(), modLen);
    }

    logger::info("Saved {} recognized mods to cosave", modCount);

    // Write sell container record
    if (!a_intfc->OpenRecord(kSellRecord, kSellVersion)) {
        logger::error("Failed to open SELL cosave record");
        return;
    }

    a_intfc->WriteRecordData(&m_sellState.formID, sizeof(m_sellState.formID));
    a_intfc->WriteRecordData(&m_sellState.totalItemsSold, sizeof(m_sellState.totalItemsSold));
    a_intfc->WriteRecordData(&m_sellState.totalGoldEarned, sizeof(m_sellState.totalGoldEarned));
    a_intfc->WriteRecordData(&m_sellState.lastSellTime, sizeof(m_sellState.lastSellTime));
    uint8_t timerByte = m_sellState.timerStarted ? 1 : 0;
    a_intfc->WriteRecordData(&timerByte, sizeof(timerByte));

    logger::info("Saved sell container state (formID={:08X}, items={}, gold={})",
                 m_sellState.formID, m_sellState.totalItemsSold, m_sellState.totalGoldEarned);

    // Write transaction log record
    if (!a_intfc->OpenRecord(kTlogRecord, kTlogVersion)) {
        logger::error("Failed to open TLOG cosave record");
        return;
    }

    uint32_t txCount = static_cast<uint32_t>(m_transactionLog.size());
    a_intfc->WriteRecordData(&txCount, sizeof(txCount));

    for (const auto& tx : m_transactionLog) {
        uint16_t len;

        len = static_cast<uint16_t>(tx.itemName.size());
        a_intfc->WriteRecordData(&len, sizeof(len));
        a_intfc->WriteRecordData(tx.itemName.data(), len);

        len = static_cast<uint16_t>(tx.vendorName.size());
        a_intfc->WriteRecordData(&len, sizeof(len));
        a_intfc->WriteRecordData(tx.vendorName.data(), len);

        len = static_cast<uint16_t>(tx.vendorAssortment.size());
        a_intfc->WriteRecordData(&len, sizeof(len));
        a_intfc->WriteRecordData(tx.vendorAssortment.data(), len);

        a_intfc->WriteRecordData(&tx.quantity, sizeof(tx.quantity));
        a_intfc->WriteRecordData(&tx.goldEarned, sizeof(tx.goldEarned));
        a_intfc->WriteRecordData(&tx.pricePerUnit, sizeof(tx.pricePerUnit));
        a_intfc->WriteRecordData(&tx.gameTime, sizeof(tx.gameTime));
    }

    logger::info("Saved {} transaction log entries to cosave", txCount);

    // Write vendor registry record
    VendorRegistry::GetSingleton()->Save(a_intfc);
}

void NetworkManager::Load(SKSE::SerializationInterface* a_intfc) {
    std::lock_guard lock(m_lock);

    uint32_t type, version, length;

    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        switch (type) {
            case kNetworkRecord:
                LoadNetworks(a_intfc, version, length);
                break;
            case kTagsRecord:
                LoadTags(a_intfc);
                break;
            case kModsRecord:
                LoadMods(a_intfc, version);
                break;
            case kSellRecord:
                LoadSell(a_intfc, version);
                break;
            case kTlogRecord:
                LoadTransactionLog(a_intfc, version);
                break;
            case VendorRegistry::kVendorRecord:
                VendorRegistry::GetSingleton()->Load(a_intfc, version);
                break;
            default:
                logger::warn("Unknown cosave record type: {:08X}", type);
                break;
        }
    }
}

void NetworkManager::Revert() {
    std::lock_guard lock(m_lock);
    m_networks.clear();
    m_tagRegistry.clear();
    m_recognizedMods.clear();
    m_sellState = SellContainerState{};
    m_transactionLog.clear();
    VendorRegistry::GetSingleton()->Revert();
    logger::info("Cosave state reverted");
}

NetworkManager::ValidationResult NetworkManager::ValidateNetworks() {
    std::lock_guard lock(m_lock);

    ValidationResult result;

    // --- Filter ID migration (rename map) ---
    static const std::unordered_map<std::string, std::string> kIDMigrations = {
        {"hearthfire_materials", "building_materials"},
        {"dark_brotherhood", "guild_equipment"},
        {"nightingale", "guild_equipment"},
    };

    for (auto& net : m_networks) {
        for (auto& filter : net.filters) {
            auto it = kIDMigrations.find(filter.filterID);
            if (it != kIDMigrations.end()) {
                logger::info("Network '{}': migrating filter ID '{}' -> '{}'",
                             net.name, it->first, it->second);
                filter.filterID = it->second;
            }
        }
        // Deduplicate filter stages after many-to-one migration (e.g. dark_brotherhood + nightingale → guild_equipment)
        {
            std::unordered_set<std::string> seen;
            auto& filters = net.filters;
            filters.erase(
                std::remove_if(filters.begin(), filters.end(), [&](const FilterStage& f) {
                    if (!seen.insert(f.filterID).second) {
                        logger::info("Network '{}': removing duplicate filter '{}'", net.name, f.filterID);
                        ++result.prunedFilters;
                        return true;
                    }
                    return false;
                }),
                filters.end());
        }
        // Migrate whoosh filter IDs
        std::unordered_set<std::string> newWhoosh;
        bool whooshMigrated = false;
        for (const auto& id : net.whooshFilters) {
            auto it = kIDMigrations.find(id);
            if (it != kIDMigrations.end()) {
                newWhoosh.insert(it->second);
                whooshMigrated = true;
                logger::info("Network '{}': migrating whoosh filter '{}' -> '{}'",
                             net.name, it->first, it->second);
            } else {
                newWhoosh.insert(id);
            }
        }
        if (whooshMigrated) {
            net.whooshFilters = std::move(newWhoosh);
        }
    }

    // Validate networks and filter references
    for (auto netIt = m_networks.begin(); netIt != m_networks.end();) {
        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(netIt->masterFormID);
        if (!masterRef) {
            logger::warn("Network '{}': master {:08X} no longer valid, removing network",
                         netIt->name, netIt->masterFormID);
            netIt = m_networks.erase(netIt);
            ++result.prunedNetworks;
            continue;
        }

        // Validate filter containerFormIDs resolve directly
        for (auto& filter : netIt->filters) {
            if (filter.containerFormID != 0) {
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(filter.containerFormID);
                if (!ref) {
                    logger::warn("Network '{}': filter '{}' references {:08X} which no longer exists, clearing",
                                 netIt->name, filter.filterID, filter.containerFormID);
                    filter.containerFormID = 0;
                    ++result.prunedFilters;
                }
            }
        }

        // Validate catch-all
        if (netIt->catchAllFormID != 0) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(netIt->catchAllFormID);
            if (!ref) {
                logger::warn("Network '{}': catchAll {:08X} no longer exists, clearing",
                             netIt->name, netIt->catchAllFormID);
                netIt->catchAllFormID = 0;
                ++result.prunedFilters;
            }
        }

        ++netIt;
    }

    // Validate tag registry — prune dead FormIDs
    for (auto it = m_tagRegistry.begin(); it != m_tagRegistry.end();) {
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(it->first);
        if (!ref) {
            logger::warn("Tag registry: {:08X} ('{}') no longer valid, removing",
                         it->first, it->second.customName);
            it = m_tagRegistry.erase(it);
            ++result.prunedTags;
        } else {
            ++it;
        }
    }

    // Validate sell container
    if (m_sellState.formID != 0) {
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(m_sellState.formID);
        if (!ref) {
            logger::warn("Sell container {:08X} no longer valid, clearing", m_sellState.formID);
            m_sellState.formID = 0;
            m_sellState.timerStarted = false;
            result.prunedSell = true;
        } else if (m_sellState.timerStarted) {
            // Reset stale timer so it starts a fresh cycle with jitter
            float now = 0.0f;
            if (auto* cal = RE::Calendar::GetSingleton()) {
                now = cal->GetHoursPassed();
            }
            float remaining = Settings::fSellIntervalHours - (now - m_sellState.lastSellTime);
            if (remaining < 0.0f) {
                // Reset to now so next visit is a full interval away, plus ±6h stagger
                static std::mt19937 rng{std::random_device{}()};
                static std::uniform_real_distribution<float> dist(-6.0f, 6.0f);
                m_sellState.lastSellTime = now + dist(rng);
                logger::info("Sell timer was stale ({:.1f}h overdue), reset to {:.1f}h",
                             -remaining, m_sellState.lastSellTime);
            }
        }
    }

    return result;
}

// Helper to parse "Plugin.esp|0xFormID" into a resolved FormID
static RE::FormID ParseFormIDRef(const std::string& a_ref, RE::TESDataHandler* a_dh) {
    auto pipe = a_ref.find('|');
    if (pipe == std::string::npos) return 0;

    auto plugin = a_ref.substr(0, pipe);
    auto formIDStr = a_ref.substr(pipe + 1);

    // Check for a second pipe (extended format: Plugin|FormID|Name)
    auto pipe2 = formIDStr.find('|');
    if (pipe2 != std::string::npos) {
        formIDStr = formIDStr.substr(0, pipe2);
    }

    // Trim
    auto trimStr = [](std::string& s) {
        auto start = s.find_first_not_of(" \t");
        auto end = s.find_last_not_of(" \t");
        s = (start != std::string::npos) ? s.substr(start, end - start + 1) : "";
    };
    trimStr(plugin);
    trimStr(formIDStr);

    if (plugin.empty() || formIDStr.empty()) return 0;

    // Parse FormID (hex)
    RE::FormID localID = 0;
    try {
        size_t pos = 0;
        if (formIDStr.size() > 2 && formIDStr[0] == '0' && (formIDStr[1] == 'x' || formIDStr[1] == 'X')) {
            localID = static_cast<RE::FormID>(std::stoul(formIDStr.substr(2), &pos, 16));
        } else {
            localID = static_cast<RE::FormID>(std::stoul(formIDStr, &pos, 16));
        }
    } catch (...) {
        return 0;
    }

    // Resolve via TESDataHandler
    auto* form = a_dh->LookupForm(localID, plugin);
    return form ? form->GetFormID() : 0;
}

void NetworkManager::LoadConfigFromINI() {
    auto path = Settings::GetINIPath();
    auto dir = path.parent_path();

    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) {
        logger::error("NetworkManager::LoadConfigFromINI: TESDataHandler not available");
        return;
    }

    uint32_t networksCreated = 0;
    uint32_t tagsLoaded = 0;
    uint32_t sellSet = 0;

    // Collect matching files, sorted alphabetically
    std::vector<std::filesystem::path> iniFiles;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto filename = entry.path().filename().string();
        if (filename.size() < 9) continue;
        auto lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("slid_") == std::string::npos || lower.substr(lower.size() - 4) != ".ini") continue;
        if (lower == "slid_modauthorexport.ini") {
            logger::debug("NetworkManager: skipping export file '{}'", filename);
            continue;
        }
        iniFiles.push_back(entry.path());
    }
    std::sort(iniFiles.begin(), iniFiles.end());

    for (const auto& f : iniFiles) {
        logger::info("NetworkManager: scanning '{}'", f.filename().string());
    }

    for (const auto& filePath : iniFiles) {
        std::ifstream file(filePath);
        if (!file.is_open()) continue;

        std::string line;
        std::string currentSection;
        std::string currentNetworkName;

        while (std::getline(file, line)) {
            // Strip comments
            auto commentPos = line.find_first_of(";#");
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }
            // Trim
            auto start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            auto end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);
            if (line.empty()) continue;

            // Section header
            if (line.front() == '[' && line.back() == ']') {
                auto section = line.substr(1, line.size() - 2);
                auto ss = section.find_first_not_of(" \t");
                auto se = section.find_last_not_of(" \t");
                section = (ss != std::string::npos) ? section.substr(ss, se - ss + 1) : "";

                currentSection = section;
                currentNetworkName.clear();

                // Check for [Network:Name]
                if (section.size() > 8 && section.substr(0, 8) == "Network:") {
                    currentNetworkName = section.substr(8);
                    // Trim network name
                    auto ns = currentNetworkName.find_first_not_of(" \t");
                    auto ne = currentNetworkName.find_last_not_of(" \t");
                    currentNetworkName = (ns != std::string::npos) ? currentNetworkName.substr(ns, ne - ns + 1) : "";
                }
                continue;
            }

            // Parse key = value
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            auto key = line.substr(0, eqPos);
            auto value = line.substr(eqPos + 1);
            // Trim key and value
            auto ks = key.find_first_not_of(" \t");
            auto ke = key.find_last_not_of(" \t");
            key = (ks != std::string::npos) ? key.substr(ks, ke - ks + 1) : "";
            auto vs = value.find_first_not_of(" \t");
            auto ve = value.find_last_not_of(" \t");
            value = (vs != std::string::npos) ? value.substr(vs, ve - vs + 1) : "";

            // [Network:Name] section - look for Master =
            if (!currentNetworkName.empty() && key == "Master") {
                auto formID = ParseFormIDRef(value, dh);
                if (formID != 0) {
                    // Only create if network doesn't already exist
                    if (!FindNetworkUnsafe(currentNetworkName)) {
                        if (CreateNetwork(currentNetworkName, formID)) {
                            ++networksCreated;
                            logger::info("NetworkManager: created network '{}' from INI (master {:08X})",
                                         currentNetworkName, formID);
                        }
                    }
                }
                continue;
            }

            // [SellContainer] section
            if (currentSection == "SellContainer") {
                // Format: Plugin.esp|0xFormID = true
                auto valueLower = value;
                std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (valueLower != "true") continue;

                auto formID = ParseFormIDRef(key, dh);
                if (formID != 0 && m_sellState.formID == 0) {
                    SetSellContainer(formID);
                    ++sellSet;
                    logger::info("NetworkManager: set sell container from INI ({:08X})", formID);
                }
                continue;
            }

            // [TaggedContainers] section
            if (currentSection == "TaggedContainers") {
                // Format: Plugin.esp|0xFormID|Display Name = true
                auto valueLower = value;
                std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (valueLower != "true") continue;

                // Parse: Plugin.esp|0xFormID|Name
                auto pipe1 = key.find('|');
                if (pipe1 == std::string::npos) continue;
                auto pipe2 = key.find('|', pipe1 + 1);
                if (pipe2 == std::string::npos) continue;

                auto formRef = key.substr(0, pipe2);
                auto displayName = key.substr(pipe2 + 1);
                // Trim display name
                auto ds = displayName.find_first_not_of(" \t");
                auto de = displayName.find_last_not_of(" \t");
                displayName = (ds != std::string::npos) ? displayName.substr(ds, de - ds + 1) : "";

                auto formID = ParseFormIDRef(formRef, dh);
                if (formID != 0 && !displayName.empty()) {
                    // Only tag if not already tagged
                    if (!IsTagged(formID)) {
                        TagContainer(formID, displayName);
                        ++tagsLoaded;
                    }
                }
                continue;
            }
        }
    }

    if (networksCreated > 0 || tagsLoaded > 0 || sellSet > 0) {
        logger::info("NetworkManager::LoadConfigFromINI: {} networks, {} tags, {} sell container",
                     networksCreated, tagsLoaded, sellSet);
    }
}

void NetworkManager::DumpToLog() const {
    std::lock_guard lock(m_lock);

    logger::info("=== SLID Network Dump ===");
    logger::info("Total networks: {}", m_networks.size());

    for (const auto& net : m_networks) {
        logger::info("  Network '{}' (master: {:08X}, catchAll: {:08X})",
                     net.name, net.masterFormID, net.catchAllFormID);

        for (size_t i = 0; i < net.filters.size(); ++i) {
            const auto& f = net.filters[i];
            if (f.containerFormID != 0) {
                logger::info("    Filter[{}] '{}' -> {:08X}", i, f.filterID, f.containerFormID);
            } else {
                logger::info("    Filter[{}] '{}' -> (unlinked)", i, f.filterID);
            }
        }
    }

    logger::info("Tag registry: {} entries", m_tagRegistry.size());
    for (const auto& [formID, tag] : m_tagRegistry) {
        logger::info("  {:08X} = '{}'", formID, tag.customName);
    }

    logger::info("Recognized mods: {}", m_recognizedMods.size());
    for (const auto& mod : m_recognizedMods) {
        logger::info("  {}", mod);
    }

    logger::info("Sell container: formID={:08X}, items={}, gold={}, timer={}, lastTime={}",
                 m_sellState.formID, m_sellState.totalItemsSold, m_sellState.totalGoldEarned,
                 m_sellState.timerStarted, m_sellState.lastSellTime);
    logger::info("Transaction log: {} entries", m_transactionLog.size());

    logger::info("=== End Dump ===");
}

// --- Private helpers ---

void NetworkManager::LoadNetworks(SKSE::SerializationInterface* a_intfc, uint32_t a_version, uint32_t a_length) {
    // No migration — version must match exactly. Use Reset power to clear stale data.
    if (a_version != kNetworkVersion) {
        logger::warn("NETW record version {} != expected {}, discarding {} bytes. Use Reset power to clear.",
                     a_version, kNetworkVersion, a_length);
        // Skip the entire record by reading and discarding all bytes
        std::vector<uint8_t> discard(a_length);
        a_intfc->ReadRecordData(discard.data(), a_length);
        return;
    }

    uint32_t netCount = 0;
    a_intfc->ReadRecordData(&netCount, sizeof(netCount));

    for (uint32_t i = 0; i < netCount; ++i) {
        // Name
        uint16_t nameLen = 0;
        a_intfc->ReadRecordData(&nameLen, sizeof(nameLen));
        std::string name(nameLen, '\0');
        a_intfc->ReadRecordData(name.data(), nameLen);

        // Master FormID
        RE::FormID savedMaster = 0;
        a_intfc->ReadRecordData(&savedMaster, sizeof(savedMaster));

        RE::FormID resolvedMaster = 0;
        if (!a_intfc->ResolveFormID(savedMaster, resolvedMaster)) {
            logger::warn("Network '{}': failed to resolve master {:08X}, skipping", name, savedMaster);
            // Skip remaining data for this network to keep stream aligned
            uint32_t filterCount = 0;
            a_intfc->ReadRecordData(&filterCount, sizeof(filterCount));
            for (uint32_t j = 0; j < filterCount; ++j) {
                uint16_t idLen = 0;
                a_intfc->ReadRecordData(&idLen, sizeof(idLen));
                std::string dummy(idLen, '\0');
                a_intfc->ReadRecordData(dummy.data(), idLen);
                RE::FormID dummyID;
                a_intfc->ReadRecordData(&dummyID, sizeof(dummyID));
            }
            RE::FormID dummyCatchAll;
            a_intfc->ReadRecordData(&dummyCatchAll, sizeof(dummyCatchAll));
            uint16_t whooshCount = 0;
            a_intfc->ReadRecordData(&whooshCount, sizeof(whooshCount));
            for (uint16_t w = 0; w < whooshCount; ++w) {
                uint16_t idLen = 0;
                a_intfc->ReadRecordData(&idLen, sizeof(idLen));
                std::string dummy(idLen, '\0');
                a_intfc->ReadRecordData(dummy.data(), idLen);
            }
            uint8_t dummyWhooshConf;
            a_intfc->ReadRecordData(&dummyWhooshConf, sizeof(dummyWhooshConf));
            continue;
        }

        Network net;
        net.name = std::move(name);
        net.masterFormID = resolvedMaster;

        // Filters: string ID + FormID
        uint32_t filterCount = 0;
        a_intfc->ReadRecordData(&filterCount, sizeof(filterCount));

        for (uint32_t j = 0; j < filterCount; ++j) {
            FilterStage filter;

            uint16_t idLen = 0;
            a_intfc->ReadRecordData(&idLen, sizeof(idLen));
            filter.filterID.resize(idLen, '\0');
            a_intfc->ReadRecordData(filter.filterID.data(), idLen);

            RE::FormID savedContainerID = 0;
            a_intfc->ReadRecordData(&savedContainerID, sizeof(savedContainerID));

            RE::FormID resolvedContainerID = 0;
            if (savedContainerID != 0) {
                if (!a_intfc->ResolveFormID(savedContainerID, resolvedContainerID)) {
                    logger::warn("Network '{}': failed to resolve filter container {:08X}",
                                 net.name, savedContainerID);
                    resolvedContainerID = 0;
                }
            }

            filter.containerFormID = resolvedContainerID;
            net.filters.push_back(std::move(filter));
        }

        // Catch-all
        RE::FormID savedCatchAll = 0;
        a_intfc->ReadRecordData(&savedCatchAll, sizeof(savedCatchAll));
        if (savedCatchAll != 0) {
            if (!a_intfc->ResolveFormID(savedCatchAll, net.catchAllFormID)) {
                logger::warn("Network '{}': failed to resolve catchAll {:08X}", net.name, savedCatchAll);
                net.catchAllFormID = 0;
            }
        }

        // Whoosh config: string set + configured flag
        uint16_t whooshCount = 0;
        a_intfc->ReadRecordData(&whooshCount, sizeof(whooshCount));
        for (uint16_t w = 0; w < whooshCount; ++w) {
            uint16_t idLen = 0;
            a_intfc->ReadRecordData(&idLen, sizeof(idLen));
            std::string id(idLen, '\0');
            a_intfc->ReadRecordData(id.data(), idLen);
            net.whooshFilters.insert(std::move(id));
        }
        uint8_t whooshConfigByte = 0;
        a_intfc->ReadRecordData(&whooshConfigByte, sizeof(whooshConfigByte));
        net.whooshConfigured = (whooshConfigByte != 0);

        logger::info("Loaded network '{}' with master {:08X}, {} filters, whoosh={}",
                     net.name, net.masterFormID, net.filters.size(),
                     net.whooshConfigured ? "configured" : "not configured");
        m_networks.push_back(std::move(net));
    }
}

void NetworkManager::LoadTags(SKSE::SerializationInterface* a_intfc) {
    uint32_t tagCount = 0;
    a_intfc->ReadRecordData(&tagCount, sizeof(tagCount));

    for (uint32_t i = 0; i < tagCount; ++i) {
        RE::FormID savedID = 0;
        a_intfc->ReadRecordData(&savedID, sizeof(savedID));

        uint16_t nameLen = 0;
        a_intfc->ReadRecordData(&nameLen, sizeof(nameLen));
        std::string customName(nameLen, '\0');
        a_intfc->ReadRecordData(customName.data(), nameLen);

        RE::FormID resolvedID = 0;
        if (a_intfc->ResolveFormID(savedID, resolvedID)) {
            m_tagRegistry[resolvedID] = TaggedContainer{std::move(customName)};
        } else {
            logger::warn("Tag registry: failed to resolve {:08X} ('{}')", savedID, customName);
        }
    }

    logger::info("Loaded {} tagged containers from cosave", m_tagRegistry.size());
}

void NetworkManager::LoadMods(SKSE::SerializationInterface* a_intfc, uint32_t a_version) {
    if (a_version > kModsVersion) {
        logger::warn("Mods cosave version {} is newer than supported {}, skipping",
                     a_version, kModsVersion);
        return;
    }

    uint32_t modCount = 0;
    a_intfc->ReadRecordData(&modCount, sizeof(modCount));

    for (uint32_t i = 0; i < modCount; ++i) {
        uint16_t modLen = 0;
        a_intfc->ReadRecordData(&modLen, sizeof(modLen));
        std::string mod(modLen, '\0');
        a_intfc->ReadRecordData(mod.data(), modLen);
        m_recognizedMods.insert(std::move(mod));
    }

    logger::info("Loaded {} recognized mods from cosave", m_recognizedMods.size());
}

void NetworkManager::LoadSell(SKSE::SerializationInterface* a_intfc, uint32_t a_version) {
    if (a_version > kSellVersion) {
        logger::warn("Sell cosave version {} is newer than supported {}, skipping",
                     a_version, kSellVersion);
        return;
    }

    RE::FormID savedFormID = 0;
    a_intfc->ReadRecordData(&savedFormID, sizeof(savedFormID));
    a_intfc->ReadRecordData(&m_sellState.totalItemsSold, sizeof(m_sellState.totalItemsSold));
    a_intfc->ReadRecordData(&m_sellState.totalGoldEarned, sizeof(m_sellState.totalGoldEarned));
    a_intfc->ReadRecordData(&m_sellState.lastSellTime, sizeof(m_sellState.lastSellTime));
    uint8_t timerByte = 0;
    a_intfc->ReadRecordData(&timerByte, sizeof(timerByte));
    m_sellState.timerStarted = (timerByte != 0);

    if (savedFormID != 0) {
        RE::FormID resolvedID = 0;
        if (a_intfc->ResolveFormID(savedFormID, resolvedID)) {
            m_sellState.formID = resolvedID;
        } else {
            logger::warn("Sell container: failed to resolve {:08X}", savedFormID);
            m_sellState.formID = 0;
            m_sellState.timerStarted = false;
        }
    }

    logger::info("Loaded sell container state (formID={:08X}, items={}, gold={}, timer={})",
                 m_sellState.formID, m_sellState.totalItemsSold,
                 m_sellState.totalGoldEarned, m_sellState.timerStarted);
}

void NetworkManager::LoadTransactionLog(SKSE::SerializationInterface* a_intfc, uint32_t a_version) {
    if (a_version > kTlogVersion) {
        logger::warn("Transaction log cosave version {} is newer than supported {}, skipping",
                     a_version, kTlogVersion);
        return;
    }

    uint32_t txCount = 0;
    a_intfc->ReadRecordData(&txCount, sizeof(txCount));

    // Cap to max in case of corruption
    if (txCount > kMaxTransactionLog) {
        logger::warn("Transaction log has {} entries (max {}), truncating", txCount, kMaxTransactionLog);
        txCount = static_cast<uint32_t>(kMaxTransactionLog);
    }

    m_transactionLog.clear();
    m_transactionLog.reserve(txCount);

    for (uint32_t i = 0; i < txCount; ++i) {
        SaleTransaction tx;
        uint16_t len;

        a_intfc->ReadRecordData(&len, sizeof(len));
        tx.itemName.resize(len);
        a_intfc->ReadRecordData(tx.itemName.data(), len);

        a_intfc->ReadRecordData(&len, sizeof(len));
        tx.vendorName.resize(len);
        a_intfc->ReadRecordData(tx.vendorName.data(), len);

        a_intfc->ReadRecordData(&len, sizeof(len));
        tx.vendorAssortment.resize(len);
        a_intfc->ReadRecordData(tx.vendorAssortment.data(), len);

        a_intfc->ReadRecordData(&tx.quantity, sizeof(tx.quantity));
        a_intfc->ReadRecordData(&tx.goldEarned, sizeof(tx.goldEarned));
        a_intfc->ReadRecordData(&tx.pricePerUnit, sizeof(tx.pricePerUnit));
        a_intfc->ReadRecordData(&tx.gameTime, sizeof(tx.gameTime));

        m_transactionLog.push_back(std::move(tx));
    }

    logger::info("Loaded {} transaction log entries from cosave", m_transactionLog.size());
}
