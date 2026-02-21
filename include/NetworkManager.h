#pragma once

#include "Network.h"
#include "VendorRegistry.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

struct PresetFilterStage {
    std::string filterID;         // e.g. "weapons"
    std::string containerRef;     // raw INI: "Skyrim.esm|0x1234"
};

struct PresetTag {
    std::string containerRef;     // raw INI ref
    std::string displayName;
};

struct PresetWarning {
    std::string plugin;   // empty = unconditional, otherwise shown if plugin is loaded
    std::string message;
};

struct NetworkPreset {
    std::string name;                              // from [Preset:Name]
    std::string description;                       // optional, shown in MCM info text
    bool userGenerated = false;                    // true = exported by player, false = mod-authored
    std::vector<std::string> requirePlugins;        // empty = always available; all must be loaded
    std::string masterRef;                         // raw INI ref
    std::string catchAllRef;                       // empty = use master
    std::vector<PresetFilterStage> filters;        // ordered
    std::vector<PresetTag> tags;
    std::unordered_set<std::string> whooshFilters;
    bool whooshConfigured = false;
    std::vector<PresetWarning> warnings;           // activation notices

    // Resolved at load time
    RE::FormID resolvedMasterFormID = 0;
};

struct ContainerListEntry {
    std::string containerRef;    // raw INI: "Plugin.esp|0xABCD"
    std::string displayName;     // optional override from INI value
    RE::FormID resolvedFormID = 0;
};

struct ContainerList {
    std::string name;                              // from [ContainerList:Name]
    std::string description;
    std::vector<std::string> requirePlugins;
    std::vector<ContainerListEntry> containers;    // resolved at load time
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

    // Re-scan presets from INI files (clears and rebuilds m_presets)
    void ReloadPresets();

    // Presets (INI-declared dormant networks, activated by player)
    const std::vector<NetworkPreset>& GetPresets() const;
    size_t GetPresetCount() const;
    const NetworkPreset* FindPresetByName(const std::string& a_name) const;
    bool ActivatePreset(const std::string& a_name);
    std::string GetPresetWarnings(const std::string& a_name) const;

    // Container lists (INI-declared container groups, available in picker)
    const std::vector<ContainerList>& GetContainerLists() const;
    size_t GetContainerListCount() const;
    const ContainerList* FindContainerListByName(const std::string& a_name) const;

    // Container list enable/disable (per-save, persisted via cosave)
    bool IsContainerListEnabled(const std::string& a_name) const;
    void SetContainerListEnabled(const std::string& a_name, bool a_enabled);

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
    void LoadContainerListState(SKSE::SerializationInterface* a_intfc, uint32_t a_version);
    void Revert();

    // Build default empty filter list
    static std::vector<FilterStage> BuildDefaultFilters();

    mutable std::mutex m_lock;
    std::vector<Network> m_networks;
    std::unordered_map<RE::FormID, TaggedContainer> m_tagRegistry;
    std::set<std::string> m_recognizedMods;
    SellContainerState m_sellState;
    std::vector<SaleTransaction> m_transactionLog;
    std::vector<NetworkPreset> m_presets;
    std::vector<ContainerList> m_containerLists;
    std::set<std::string> m_disabledContainerLists;

    static constexpr uint32_t kUniqueID = 'SLID';
    static constexpr uint32_t kNetworkRecord = 'NETW';
    static constexpr uint32_t kTagsRecord = 'TAGS';
    static constexpr uint32_t kModsRecord = 'MODS';
    static constexpr uint32_t kSellRecord = 'SELL';
    static constexpr uint32_t kTlogRecord = 'TLOG';
    static constexpr uint32_t kClstRecord = 'CLST';
    static constexpr uint32_t kNetworkVersion = 4;
    static constexpr uint32_t kTagsVersion = 1;
    static constexpr uint32_t kModsVersion = 1;
    static constexpr uint32_t kSellVersion = 1;
    static constexpr uint32_t kTlogVersion = 1;
    static constexpr uint32_t kClstVersion = 1;
};
