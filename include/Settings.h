#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Settings {

    // --- [General] ---
    inline bool bModEnabled = true;   // Master switch for mod functionality
    inline bool bDebugLogging = false;
    inline bool bShownWelcomeTutorial = false;  // Has user seen the welcome popup?
    inline bool bInterceptActivation = false;  // Intercept master/sell activation with MessageBox

    // --- [Powers] ---
    inline bool bSummonEnabled = true;

    // --- [ContainerPicker] ---
    inline bool bIncludeUnlinkedContainers = false; // Show untagged scanned containers in picker

    // --- [Compatibility] ---
    inline bool bSCIEIntegration = true;       // Enable SCIE integration when detected
    inline bool bSCIEIncludeContainers = true; // Include SCIE containers in Link picker

    // --- [Containers] ---
    // Display names filtered from scanned container lists.
    // On non-English installs these won't match — users should edit the INI
    // with their localized names. Proper fix deferred to Milestone 11.
    inline std::vector<std::string> sGenericContainerNames = {
        "Barrel", "Basket", "Bookcase", "Chest", "Cupboard",
        "Dresser", "End Table", "Knapsack", "Sack", "Wardrobe",
    };

    // --- [Categories] ---
    // Crafting station keywords (FormID + plugin) for COBJ-based categorization
    inline uint32_t uCraftingCookpot   = 0x000A5CB3;
    inline std::string sCookpotPlugin  = "Skyrim.esm";

    inline uint32_t uCraftingSmelter   = 0x000A5CCE;
    inline std::string sSmelterPlugin  = "Skyrim.esm";

    inline uint32_t uCraftingCarpenter = 0x014353;
    inline std::string sCarpenterPlugin = "HearthFires.esm";

    inline uint32_t uCraftingTanningRack = 0x0007866A;
    inline std::string sTanningRackPlugin = "Skyrim.esm";

    // Keyword FormIDs for Misc item sub-categorization
    inline std::string sKeywordPlugin            = "Skyrim.esm";
    inline uint32_t uVendorItemAnimalHide        = 0x0914EA;
    inline uint32_t uVendorItemAnimalPart        = 0x0914EB;
    inline uint32_t uVendorItemOreIngot          = 0x0914EC;
    inline uint32_t uVendorItemGem               = 0x0914ED;
    inline uint32_t uVendorItemKey               = 0x0914EF;

    // Keyword FormID used by Enchanted Items filter
    inline uint32_t uMagicDisallowEnchanting     = 0x0C27BD;

    // --- [UniqueItems] + [DisplayNames] ---
    // Runtime-resolved FormID sets loaded from SLID_*.ini files.
    // uniqueItemFormIDs = union of all entries (root filter check).
    // uniqueItemGroups  = per-child-filter sets keyed by filter ID.
    // Populated by LoadUniqueItems() after TESDataHandler is ready.
    inline std::unordered_set<RE::FormID> uniqueItemFormIDs;
    inline std::unordered_map<std::string, std::unordered_set<RE::FormID>> uniqueItemGroups;

    // Display names and descriptions for unique-items filters, keyed by filter ID.
    // Loaded from [DisplayNames] section: id|Display Name|Description = True
    struct FilterMeta {
        std::string displayName;
        std::string description;
    };
    inline std::unordered_map<std::string, FilterMeta> uniqueItemFilterMeta;

    // Scan for SLID_*.ini files and resolve FormID+plugin entries into
    // the uniqueItemFormIDs set (and per-group sets). Call once after
    // TESDataHandler is ready.
    void LoadUniqueItems();

    // --- [Sales] ---
    inline float   fSellPricePercent   = 0.10f;
    inline int32_t iSellBatchSize      = 10;
    inline float   fSellIntervalHours  = 24.0f;

    // --- [VendorSales] ---
    inline float   fVendorPricePercent   = 0.25f;   // 25% base value for registered vendors
    inline int32_t iVendorBatchSize      = 25;       // items per vendor per visit
    inline float   fVendorIntervalHours  = 48.0f;    // hours between vendor visits
    inline int32_t iVendorCost           = 5000;     // gold cost to establish trade arrangement

    // Returns the full path to SLID.ini next to the DLL
    std::filesystem::path GetINIPath();

    // Returns the full path to SLIDCustom.ini next to the DLL (legacy location)
    std::filesystem::path GetCustomINIPath();

    // Returns the path to the SLID data subfolder (SKSE/Plugins/SLID/)
    std::filesystem::path GetDataDir();

    // Returns the user data directory: Documents/My Games/{GameFolder}/SKSE/SLID/
    // VFS-independent — always the real filesystem. User-generated files go here.
    std::filesystem::path GetUserDataDir();

    // Returns Documents/.../SKSE/SLID/SLIDCustom.ini (new canonical location)
    std::filesystem::path GetUserCustomINIPath();

    // Returns both data directories: {GetDataDir(), GetUserDataDir()}
    // Game dir first (mod-author content), user dir second (user overlay).
    std::vector<std::filesystem::path> GetDataDirs();

    // Returns true if filename matches *slid_*.ini (case-insensitive contains)
    bool IsDataINI(const std::string& a_filename);

    // Load settings from INI. Reads SLID.ini (defaults), then overlays
    // SLIDCustom.ini from both legacy and user locations. Call once after logging is initialized.
    void Load();

    // Save user-changeable settings to SLIDCustom.ini (never overwrites SLID.ini).
    void Save();

    // --- Setters for user-overridable settings ---
    // Each setter assigns, marks dirty, and saves to SLIDCustom.ini.
    // bModEnabled and bDebugLogging are global (SLID.ini only) — assign directly.
    void SetShownWelcomeTutorial(bool a_val);
    void SetInterceptActivation(bool a_val);
    void SetSummonEnabled(bool a_val);
    void SetIncludeUnlinkedContainers(bool a_val);
    void SetSCIEIntegration(bool a_val);
    void SetSCIEIncludeContainers(bool a_val);
    void SetSellPricePercent(float a_val);
    void SetSellBatchSize(int32_t a_val);
    void SetSellIntervalHours(float a_val);
    void SetVendorPricePercent(float a_val);
    void SetVendorBatchSize(int32_t a_val);
    void SetVendorIntervalHours(float a_val);
    void SetVendorCost(int32_t a_val);
}
