#include "VendorRegistry.h"
#include "Settings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace {
    float RandomJitter() {
        static std::mt19937 rng{std::random_device{}()};
        static std::uniform_real_distribution<float> dist(-6.0f, 6.0f);
        return dist(rng);
    }
}

VendorRegistry* VendorRegistry::GetSingleton() {
    static VendorRegistry singleton;
    return &singleton;
}

bool VendorRegistry::RegisterVendor(const RegisteredVendor& a_vendor) {
    std::lock_guard lock(m_lock);

    // Check for duplicate
    for (const auto& v : m_vendors) {
        if (v.npcBaseFormID == a_vendor.npcBaseFormID) {
            logger::warn("VendorRegistry: NPC {:08X} ({}) already registered",
                         a_vendor.npcBaseFormID, a_vendor.vendorName);
            return false;
        }
    }

    m_vendors.push_back(a_vendor);
    logger::info("VendorRegistry: registered {} ({}) from {} — faction {:08X}",
                 a_vendor.vendorName, a_vendor.npcBaseFormID,
                 a_vendor.storeName, a_vendor.factionFormID);
    return true;
}

bool VendorRegistry::IsRegistered(RE::FormID a_npcBaseFormID) const {
    std::lock_guard lock(m_lock);
    for (const auto& v : m_vendors) {
        if (v.npcBaseFormID == a_npcBaseFormID) return true;
    }
    return false;
}

const RegisteredVendor* VendorRegistry::FindVendor(RE::FormID a_npcBaseFormID) const {
    std::lock_guard lock(m_lock);
    for (const auto& v : m_vendors) {
        if (v.npcBaseFormID == a_npcBaseFormID) return &v;
    }
    return nullptr;
}

RegisteredVendor* VendorRegistry::FindVendorMutable(RE::FormID a_npcBaseFormID) {
    std::lock_guard lock(m_lock);
    for (auto& v : m_vendors) {
        if (v.npcBaseFormID == a_npcBaseFormID) return &v;
    }
    return nullptr;
}

const std::vector<RegisteredVendor>& VendorRegistry::GetVendors() const {
    return m_vendors;
}

size_t VendorRegistry::GetActiveCount() const {
    std::lock_guard lock(m_lock);
    size_t count = 0;
    for (const auto& v : m_vendors) {
        if (v.active) ++count;
    }
    return count;
}

void VendorRegistry::RecordVendorSale(RE::FormID a_npcBaseFormID, uint32_t a_items, uint32_t a_gold, float a_gameTime) {
    std::lock_guard lock(m_lock);
    for (auto& v : m_vendors) {
        if (v.npcBaseFormID == a_npcBaseFormID) {
            v.totalItemsSold += a_items;
            v.totalGoldEarned += a_gold;
            v.lastVisitTime = a_gameTime + RandomJitter();
            return;
        }
    }
}

void VendorRegistry::SetVendorActive(RE::FormID a_npcBaseFormID, bool a_active) {
    std::lock_guard lock(m_lock);
    for (auto& v : m_vendors) {
        if (v.npcBaseFormID == a_npcBaseFormID) {
            v.active = a_active;
            return;
        }
    }
}

void VendorRegistry::ClearAll() {
    std::lock_guard lock(m_lock);
    m_vendors.clear();
    logger::info("VendorRegistry: cleared all vendors");
}

void VendorRegistry::Save(SKSE::SerializationInterface* a_intfc) const {
    std::lock_guard lock(m_lock);

    if (!a_intfc->OpenRecord(kVendorRecord, kVendorVersion)) {
        logger::error("VendorRegistry: failed to open VEND cosave record");
        return;
    }

    uint32_t count = static_cast<uint32_t>(m_vendors.size());
    a_intfc->WriteRecordData(&count, sizeof(count));

    for (const auto& v : m_vendors) {
        a_intfc->WriteRecordData(&v.npcBaseFormID, sizeof(v.npcBaseFormID));
        a_intfc->WriteRecordData(&v.factionFormID, sizeof(v.factionFormID));

        uint16_t nameLen = static_cast<uint16_t>(v.vendorName.size());
        a_intfc->WriteRecordData(&nameLen, sizeof(nameLen));
        a_intfc->WriteRecordData(v.vendorName.data(), nameLen);

        uint16_t storeLen = static_cast<uint16_t>(v.storeName.size());
        a_intfc->WriteRecordData(&storeLen, sizeof(storeLen));
        a_intfc->WriteRecordData(v.storeName.data(), storeLen);

        a_intfc->WriteRecordData(&v.registrationTime, sizeof(v.registrationTime));
        a_intfc->WriteRecordData(&v.lastVisitTime, sizeof(v.lastVisitTime));
        a_intfc->WriteRecordData(&v.totalItemsSold, sizeof(v.totalItemsSold));
        a_intfc->WriteRecordData(&v.totalGoldEarned, sizeof(v.totalGoldEarned));

        uint8_t activeByte = v.active ? 1 : 0;
        a_intfc->WriteRecordData(&activeByte, sizeof(activeByte));

        uint8_t investedByte = v.invested ? 1 : 0;
        a_intfc->WriteRecordData(&investedByte, sizeof(investedByte));
    }

    logger::info("VendorRegistry: saved {} vendors to cosave", count);
}

void VendorRegistry::Load(SKSE::SerializationInterface* a_intfc, uint32_t a_version) {
    std::lock_guard lock(m_lock);

    if (a_version != 1 && a_version != 2) {
        logger::warn("VendorRegistry: VEND record version {} unsupported, skipping", a_version);
        return;
    }

    uint32_t count = 0;
    a_intfc->ReadRecordData(&count, sizeof(count));

    for (uint32_t i = 0; i < count; ++i) {
        RegisteredVendor v;

        RE::FormID savedNPC = 0;
        a_intfc->ReadRecordData(&savedNPC, sizeof(savedNPC));

        RE::FormID savedFaction = 0;
        a_intfc->ReadRecordData(&savedFaction, sizeof(savedFaction));

        uint16_t nameLen = 0;
        a_intfc->ReadRecordData(&nameLen, sizeof(nameLen));
        v.vendorName.resize(nameLen, '\0');
        a_intfc->ReadRecordData(v.vendorName.data(), nameLen);

        uint16_t storeLen = 0;
        a_intfc->ReadRecordData(&storeLen, sizeof(storeLen));
        v.storeName.resize(storeLen, '\0');
        a_intfc->ReadRecordData(v.storeName.data(), storeLen);

        a_intfc->ReadRecordData(&v.registrationTime, sizeof(v.registrationTime));
        a_intfc->ReadRecordData(&v.lastVisitTime, sizeof(v.lastVisitTime));
        a_intfc->ReadRecordData(&v.totalItemsSold, sizeof(v.totalItemsSold));
        a_intfc->ReadRecordData(&v.totalGoldEarned, sizeof(v.totalGoldEarned));

        uint8_t activeByte = 0;
        a_intfc->ReadRecordData(&activeByte, sizeof(activeByte));
        v.active = (activeByte != 0);

        if (a_version >= 2) {
            uint8_t investedByte = 0;
            a_intfc->ReadRecordData(&investedByte, sizeof(investedByte));
            v.invested = (investedByte != 0);
        }

        // Resolve FormIDs
        RE::FormID resolvedNPC = 0;
        if (!a_intfc->ResolveFormID(savedNPC, resolvedNPC)) {
            logger::warn("VendorRegistry: failed to resolve NPC {:08X} ({}), skipping",
                         savedNPC, v.vendorName);
            continue;
        }
        v.npcBaseFormID = resolvedNPC;

        RE::FormID resolvedFaction = 0;
        if (!a_intfc->ResolveFormID(savedFaction, resolvedFaction)) {
            logger::warn("VendorRegistry: failed to resolve faction {:08X} for {}, skipping",
                         savedFaction, v.vendorName);
            continue;
        }
        v.factionFormID = resolvedFaction;

        m_vendors.push_back(std::move(v));
        logger::info("VendorRegistry: loaded vendor {} ({:08X}) from cosave",
                     m_vendors.back().vendorName, m_vendors.back().npcBaseFormID);
    }

    logger::info("VendorRegistry: loaded {} vendors from cosave", m_vendors.size());
}

void VendorRegistry::Revert() {
    std::lock_guard lock(m_lock);
    m_vendors.clear();
    logger::info("VendorRegistry: reverted");
}

int VendorRegistry::Validate() {
    std::lock_guard lock(m_lock);
    int pruned = 0;

    float now = 0.0f;
    if (auto* cal = RE::Calendar::GetSingleton()) {
        now = cal->GetHoursPassed();
    }

    for (auto it = m_vendors.begin(); it != m_vendors.end();) {
        auto* form = RE::TESForm::LookupByID(it->npcBaseFormID);
        if (!form) {
            logger::warn("VendorRegistry: NPC {:08X} ({}) no longer valid, removing",
                         it->npcBaseFormID, it->vendorName);
            it = m_vendors.erase(it);
            ++pruned;
        } else {
            // Reset stale vendor timer so it starts a fresh cycle with jitter
            if (it->active) {
                float remaining = Settings::fVendorIntervalHours - (now - it->lastVisitTime);
                if (remaining < 0.0f) {
                    it->lastVisitTime = now + RandomJitter();
                    logger::info("VendorRegistry: {} timer was stale ({:.1f}h overdue), reset to {:.1f}h",
                                 it->vendorName, -remaining, it->lastVisitTime);
                }
            }
            ++it;
        }
    }

    if (pruned > 0) {
        logger::info("VendorRegistry: pruned {} invalid vendors", pruned);
    }
    return pruned;
}

void VendorRegistry::LoadWhitelist() {
    auto iniPath = Settings::GetINIPath();
    if (iniPath.empty()) {
        logger::warn("VendorRegistry::LoadWhitelist: could not determine INI path");
        return;
    }

    auto dir = iniPath.parent_path();
    auto* dh = RE::TESDataHandler::GetSingleton();
    if (!dh) {
        logger::error("VendorRegistry::LoadWhitelist: TESDataHandler not available");
        return;
    }

    uint32_t totalEntries = 0;
    uint32_t resolved = 0;

    // Scan for all *SLID_*.ini files (same discovery pattern as FilterRegistry/Settings)
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

        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        std::string line;
        bool inVendorsSection = false;

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
                // Trim section name
                auto ss = section.find_first_not_of(" \t");
                auto se = section.find_last_not_of(" \t");
                section = (ss != std::string::npos) ? section.substr(ss, se - ss + 1) : "";
                inVendorsSection = (section == "Vendors");
                continue;
            }

            if (!inVendorsSection) continue;

            // Parse: Plugin.esm|0xFormID = True
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            auto key = line.substr(0, eqPos);
            // Trim key
            auto ks = key.find_first_not_of(" \t");
            auto ke = key.find_last_not_of(" \t");
            if (ks == std::string::npos) continue;
            key = key.substr(ks, ke - ks + 1);

            auto pipe = key.find('|');
            if (pipe == std::string::npos) continue;

            auto plugin = key.substr(0, pipe);
            auto formIDStr = key.substr(pipe + 1);

            // Trim both
            auto ps = plugin.find_first_not_of(" \t");
            auto pe = plugin.find_last_not_of(" \t");
            if (ps == std::string::npos) continue;
            plugin = plugin.substr(ps, pe - ps + 1);

            auto fs = formIDStr.find_first_not_of(" \t");
            auto fe = formIDStr.find_last_not_of(" \t");
            if (fs == std::string::npos) continue;
            formIDStr = formIDStr.substr(fs, fe - fs + 1);

            // Handle extended format: Plugin|FormID|Name = true (strip name part)
            auto pipe2 = formIDStr.find('|');
            if (pipe2 != std::string::npos) {
                formIDStr = formIDStr.substr(0, pipe2);
                // Re-trim
                fe = formIDStr.find_last_not_of(" \t");
                formIDStr = formIDStr.substr(0, fe + 1);
            }

            // Strip optional 0x prefix and parse hex
            if (formIDStr.size() > 2 && formIDStr[0] == '0' && (formIDStr[1] == 'x' || formIDStr[1] == 'X')) {
                formIDStr = formIDStr.substr(2);
            }
            uint32_t localID = 0;
            try {
                localID = static_cast<uint32_t>(std::stoul(formIDStr, nullptr, 16));
            } catch (...) {
                continue;
            }
            if (localID == 0) continue;

            ++totalEntries;

            // Mask off load-order index byte
            uint32_t maskedID = localID & 0x00FFFFFF;

            auto* form = dh->LookupForm(maskedID, plugin);
            if (form) {
                m_allowedVendors.insert(form->GetFormID());
                ++resolved;
            }
            // Silently skip entries whose plugin isn't loaded
        }
    }

    logger::info("VendorRegistry: vendor whitelist loaded — {}/{} resolved", resolved, totalEntries);
}

bool VendorRegistry::IsAllowedVendor(RE::FormID a_npcBaseFormID) const {
    return m_allowedVendors.contains(a_npcBaseFormID);
}

size_t VendorRegistry::AllowedVendorCount() const {
    return m_allowedVendors.size();
}

void VendorRegistry::DumpToLog() const {
    std::lock_guard lock(m_lock);
    logger::info("=== Vendor Registry Dump ===");
    logger::info("Total vendors: {}", m_vendors.size());

    for (const auto& v : m_vendors) {
        logger::info("  {} ({:08X}) — store: {}, faction: {:08X}, active: {}",
                     v.vendorName, v.npcBaseFormID, v.storeName, v.factionFormID, v.active);
        logger::info("    registered: {:.1f}h, lastVisit: {:.1f}h, sold: {}, gold: {}",
                     v.registrationTime, v.lastVisitTime, v.totalItemsSold, v.totalGoldEarned);
    }
    logger::info("=== End Vendor Dump ===");
}
