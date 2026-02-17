#pragma once

#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

struct RegisteredVendor {
    RE::FormID  npcBaseFormID  = 0;
    RE::FormID  factionFormID  = 0;
    std::string vendorName;
    std::string storeName;
    float       registrationTime = 0.0f;  // game hours when registered
    float       lastVisitTime    = 0.0f;  // game hours of last sale cycle
    uint32_t    totalItemsSold   = 0;     // lifetime per vendor
    uint32_t    totalGoldEarned  = 0;     // lifetime per vendor
    bool        active           = true;
    bool        invested         = false;  // player has invested in this vendor (500+ gold on person)
};

class VendorRegistry {
public:
    static VendorRegistry* GetSingleton();

    // Registration
    bool RegisterVendor(const RegisteredVendor& a_vendor);
    bool IsRegistered(RE::FormID a_npcBaseFormID) const;
    const RegisteredVendor* FindVendor(RE::FormID a_npcBaseFormID) const;
    RegisteredVendor* FindVendorMutable(RE::FormID a_npcBaseFormID);

    // Query
    const std::vector<RegisteredVendor>& GetVendors() const;
    size_t GetActiveCount() const;

    // Modification
    void RecordVendorSale(RE::FormID a_npcBaseFormID, uint32_t a_items, uint32_t a_gold, float a_gameTime);
    void SetVendorActive(RE::FormID a_npcBaseFormID, bool a_active);

    // Reset
    void ClearAll();

    // Cosave — called by NetworkManager's cosave callbacks
    void Save(SKSE::SerializationInterface* a_intfc) const;
    void Load(SKSE::SerializationInterface* a_intfc, uint32_t a_version);
    void Revert();

    // Validation — prune vendors whose NPC base form no longer exists
    int Validate();

    // Whitelist — loaded from [Vendors] sections in SLID_*.ini files
    void LoadWhitelist();
    bool IsAllowedVendor(RE::FormID a_npcBaseFormID) const;
    size_t AllowedVendorCount() const;

    // Debug
    void DumpToLog() const;

    // Record type and version for cosave
    static constexpr uint32_t kVendorRecord  = 'VEND';
    static constexpr uint32_t kVendorVersion = 2;

private:
    VendorRegistry() = default;

    mutable std::mutex m_lock;
    std::vector<RegisteredVendor> m_vendors;
    std::unordered_set<RE::FormID> m_allowedVendors;
};
