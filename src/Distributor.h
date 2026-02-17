#pragma once

#include "Network.h"
#include "NetworkManager.h"

#include <set>
#include <unordered_map>

namespace Distributor {
    // Result of a distribution operation, with per-container breakdown
    struct DistributeResult {
        uint32_t totalItems = 0;
        std::vector<std::pair<std::string, uint32_t>> perContainer;  // container name -> count
    };

    // Result of a sales processing cycle
    struct SalesResult {
        uint32_t itemsSold  = 0;
        uint32_t goldEarned = 0;
        std::vector<SaleTransaction> transactions;
    };

    // Pre-built lookup sets for categorization (built lazily, cached for session)
    struct COBJSets {
        std::set<RE::FormID> cookedFood;       // outputs of CraftingCookpot COBJs
        std::set<RE::FormID> smeltableInputs;  // inputs to CraftingSmelter COBJs (ores)
        std::set<RE::FormID> hearthfireMats;   // inputs to BYOHCarpenterTable COBJs
        std::set<RE::FormID> tanningOutputs;   // outputs of CraftingTanningRack COBJs
    };

    // Lazy-cached accessor — builds on first call, returns cached thereafter.
    // Filters that need COBJ data call this internally.
    const COBJSets& GetCOBJSets();

    // Gather all items from pipeline containers back to master (Sort phase 1).
    // Returns total items moved. Used by summoned chest before opening master.
    uint32_t GatherToMaster(const std::string& a_networkName);

    // Run distribution for a named network (must be called on game thread).
    DistributeResult Distribute(const std::string& a_networkName);

    // Whoosh: drain player inventory into master container based on per-filter set
    // Returns number of items moved. Returns 0 if not configured (caller shows popup).
    uint32_t Whoosh(const std::string& a_networkName);

    // Predicted counts per filter slot and catch-all after a hypothetical Sort
    struct PredictionResult {
        std::vector<int> filterCounts;  // one per filter, same order as input
        std::vector<int> contestedCounts;  // contested items per filter (matched but claimed by higher-priority)
        std::vector<std::unordered_map<size_t, int32_t>> contestedByMaps;  // per filter: earlier_index → stolen count
        int catchAllCount = 0;          // items not matching any filter
        int originCount = 0;            // items staying in master (when catch-all IS master)
    };

    // Dry-run distribution: compute where items WOULD go without moving anything.
    // Pools all items from master + all linked filter/catch-all containers,
    // runs the pipeline, returns predicted counts per slot.
    PredictionResult PredictDistribution(
        RE::FormID a_masterFormID,
        const std::vector<FilterStage>& a_filters,
        RE::FormID a_catchAllFormID);

    // --- Pipeline types ---

    struct PoolItem {
        RE::TESBoundObject* item;
        int32_t count;
    };

    struct FilterOutcome {
        int32_t claimedCount = 0;
        int32_t contestedCount = 0;
        std::unordered_map<size_t, int32_t> contestedBy;  // earlier_filter_index → count
    };

    struct PipelineResult {
        std::vector<FilterOutcome> filterOutcomes;  // one per filter, pipeline order
        int32_t catchAllCount = 0;
        int32_t originCount = 0;

        struct RouteEntry {
            RE::TESBoundObject* item;
            int32_t count;
            RE::TESObjectREFR* target;
        };
        std::vector<RouteEntry> routes;
    };

    // Run the filter pipeline over a pool of items.
    // a_resolveRefs: true = populate routes with container refs (for Distribute),
    //                false = counts only (for PredictDistribution).
    PipelineResult RunPipeline(
        const std::vector<FilterStage>& a_filters,
        RE::FormID a_catchAllFormID,
        RE::FormID a_masterFormID,
        const std::vector<PoolItem>& a_pool,
        bool a_resolveRefs);

    // Process sales: sell items from sell container, deposit gold.
    // Respects timer interval, batch size, and price settings from INI.
    SalesResult ProcessSales();

    // Vendor-specific sales result (one per vendor that visited)
    struct VendorSalesResult {
        uint32_t totalItemsSold  = 0;
        uint32_t totalGoldEarned = 0;
        std::vector<SaleTransaction> transactions;
        int vendorsVisited = 0;
    };

    // Process registered vendor sales: each vendor buys items matching their
    // faction buy list from the sell container on independent timers.
    VendorSalesResult ProcessVendorSales();
}
