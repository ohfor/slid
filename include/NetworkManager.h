#pragma once

#include "Network.h"
#include "VendorRegistry.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct TaggedContainer {
    std::string customName;
};

struct SellContainerState {
    RE::FormID  formID          = 0;      // 0 = not designated
    uint32_t    totalItemsSold  = 0;      // lifetime (persisted)
    uint32_t    totalGoldEarned = 0;      // lifetime (persisted)
    float       lastSellTime    = 0.0f;   // game hours (persisted)
    bool        timerStarted    = false;  // persisted
};

struct SaleTransaction {
    std::string itemName;
    std::string vendorName;
    std::string vendorAssortment;
    int32_t     quantity;
    int32_t     goldEarned;
    float       pricePerUnit;  // float for display (base * percent)
    float       gameTime;
};

class NetworkManager {
public:
    static NetworkManager* GetSingleton();

    // CRUD
    bool CreateNetwork(const std::string& a_name, RE::FormID a_masterFormID);
    bool RemoveNetwork(const std::string& a_name);
    Network* FindNetwork(const std::string& a_name);
    const std::vector<Network>& GetNetworks() const;

    // Filter pipeline management (from config menu)
    void SetFilterConfig(const std::string& a_networkName,
                         const std::vector<FilterStage>& a_filters,
                         RE::FormID a_catchAllFormID);

    // Whoosh configuration
    void SetWhooshConfig(const std::string& a_networkName, const std::unordered_set<std::string>& a_filters);

    // Nuclear reset — clears everything (networks, tags, sell state, transaction log)
    void ClearAll();

    // Tag registry (global, not per-network)
    bool TagContainer(RE::FormID a_formID, const std::string& a_customName);
    bool UntagContainer(RE::FormID a_formID);
    bool IsTagged(RE::FormID a_formID) const;
    const std::unordered_map<RE::FormID, TaggedContainer>& GetTagRegistry() const;
    std::string GetTagName(RE::FormID a_formID) const;

    // Clear filter/catch-all references to a container across all networks
    void ClearContainerReferences(RE::FormID a_formID);

    // Sell container (global, one per save)
    void SetSellContainer(RE::FormID a_formID);
    void ClearSellContainer();
    RE::FormID GetSellContainerFormID() const;
    bool HasSellContainer() const;
    const SellContainerState& GetSellState() const;
    void RecordSale(uint32_t a_itemCount, uint32_t a_goldAmount);
    void SetLastSellTime(float a_gameHours);

    // Transaction log (in-memory only, newest first)
    static constexpr size_t kMaxTransactionLog = 100;
    void AppendTransactions(const std::vector<SaleTransaction>& a_transactions);
    const std::vector<SaleTransaction>& GetTransactionLog() const;

    // Cosave callbacks
    static void OnGameSaved(SKSE::SerializationInterface* a_intfc);
    static void OnGameLoaded(SKSE::SerializationInterface* a_intfc);
    static void OnRevert(SKSE::SerializationInterface* a_intfc);

    // Query methods
    std::string FindNetworkByMaster(RE::FormID a_masterFormID) const;
    std::vector<std::string> GetNetworkNames() const;

    // Validation (called post-load to prune dead references)
    struct ValidationResult { int prunedNetworks = 0; int prunedTags = 0; int prunedFilters = 0; bool prunedSell = false; };
    ValidationResult ValidateNetworks();

    // INI loading (networks, tags, sell container from *SLID_*.ini files)
    void LoadConfigFromINI();

    // Debug
    void DumpToLog() const;

private:
    NetworkManager() = default;

    // Internal find without locking (caller must hold m_lock)
    Network* FindNetworkUnsafe(const std::string& a_name);

    void Save(SKSE::SerializationInterface* a_intfc) const;
    void Load(SKSE::SerializationInterface* a_intfc);
    void LoadNetworks(SKSE::SerializationInterface* a_intfc, uint32_t a_version, uint32_t a_length);
    void LoadTags(SKSE::SerializationInterface* a_intfc);
    void LoadMods(SKSE::SerializationInterface* a_intfc, uint32_t a_version);
    void LoadSell(SKSE::SerializationInterface* a_intfc, uint32_t a_version);
    void LoadTransactionLog(SKSE::SerializationInterface* a_intfc, uint32_t a_version);
    void Revert();

    // Build default empty filter list
    static std::vector<FilterStage> BuildDefaultFilters();

    mutable std::mutex m_lock;
    std::vector<Network> m_networks;
    std::unordered_map<RE::FormID, TaggedContainer> m_tagRegistry;
    std::set<std::string> m_recognizedMods;
    SellContainerState m_sellState;
    std::vector<SaleTransaction> m_transactionLog;

    static constexpr uint32_t kUniqueID = 'SLID';
    static constexpr uint32_t kNetworkRecord = 'NETW';
    static constexpr uint32_t kTagsRecord = 'TAGS';
    static constexpr uint32_t kModsRecord = 'MODS';
    static constexpr uint32_t kSellRecord = 'SELL';
    static constexpr uint32_t kTlogRecord = 'TLOG';
    static constexpr uint32_t kNetworkVersion = 4;
    static constexpr uint32_t kTagsVersion = 1;
    static constexpr uint32_t kModsVersion = 1;
    static constexpr uint32_t kSellVersion = 1;
    static constexpr uint32_t kTlogVersion = 1;
};
