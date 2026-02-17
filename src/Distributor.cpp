#include "Distributor.h"
#include "FilterRegistry.h"
#include "NetworkManager.h"
#include "VendorRegistry.h"
#include "Settings.h"

#include <map>
#include <random>
#include <set>

namespace Distributor {

    // --- COBJ-based lookup set builders ---

    static std::set<RE::FormID> BuildCOBJOutputSet(RE::BGSKeyword* a_benchKeyword) {
        std::set<RE::FormID> result;
        if (!a_benchKeyword) return result;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return result;

        auto& cobjs = dataHandler->GetFormArray<RE::BGSConstructibleObject>();
        for (auto* cobj : cobjs) {
            if (!cobj) continue;
            if (cobj->benchKeyword != a_benchKeyword) continue;
            if (auto* created = cobj->createdItem) {
                result.insert(created->GetFormID());
            }
        }
        return result;
    }

    static std::set<RE::FormID> BuildCOBJInputSet(RE::BGSKeyword* a_benchKeyword) {
        std::set<RE::FormID> result;
        if (!a_benchKeyword) return result;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return result;

        auto& cobjs = dataHandler->GetFormArray<RE::BGSConstructibleObject>();
        for (auto* cobj : cobjs) {
            if (!cobj) continue;
            if (cobj->benchKeyword != a_benchKeyword) continue;

            // Collect all required items (inputs) via ForEachContainerObject
            cobj->requiredItems.ForEachContainerObject([&](RE::ContainerObject& a_entry) {
                if (a_entry.obj) {
                    result.insert(a_entry.obj->GetFormID());
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        return result;
    }

    static COBJSets BuildCOBJSets() {
        COBJSets sets;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return sets;

        // CraftingCookpot — outputs = cooked food
        auto* kwCookpot = dataHandler->LookupForm<RE::BGSKeyword>(Settings::uCraftingCookpot, Settings::sCookpotPlugin);
        if (kwCookpot) {
            sets.cookedFood = BuildCOBJOutputSet(kwCookpot);
            logger::debug("COBJSets: {} cooked food items", sets.cookedFood.size());
        } else {
            logger::warn("COBJSets: CraftingCookpot keyword {:06X} not found in {}",
                         Settings::uCraftingCookpot, Settings::sCookpotPlugin);
        }

        // CraftingSmelter — inputs = ores
        auto* kwSmelter = dataHandler->LookupForm<RE::BGSKeyword>(Settings::uCraftingSmelter, Settings::sSmelterPlugin);
        if (kwSmelter) {
            sets.smeltableInputs = BuildCOBJInputSet(kwSmelter);
            logger::debug("COBJSets: {} smeltable ore items", sets.smeltableInputs.size());
        } else {
            logger::warn("COBJSets: CraftingSmelter keyword {:06X} not found in {}",
                         Settings::uCraftingSmelter, Settings::sSmelterPlugin);
        }

        // BYOHCarpenterTable — inputs = building materials
        auto* kwCarpenter = dataHandler->LookupForm<RE::BGSKeyword>(Settings::uCraftingCarpenter, Settings::sCarpenterPlugin);
        if (kwCarpenter) {
            sets.hearthfireMats = BuildCOBJInputSet(kwCarpenter);
            logger::debug("COBJSets: {} hearthfire material items", sets.hearthfireMats.size());
        } else {
            logger::warn("COBJSets: BYOHCarpenterTable keyword {:06X} not found in {}",
                         Settings::uCraftingCarpenter, Settings::sCarpenterPlugin);
        }

        // CraftingTanningRack — outputs = leathers
        auto* kwTanning = dataHandler->LookupForm<RE::BGSKeyword>(Settings::uCraftingTanningRack, Settings::sTanningRackPlugin);
        if (kwTanning) {
            sets.tanningOutputs = BuildCOBJOutputSet(kwTanning);
            logger::debug("COBJSets: {} tanning output items", sets.tanningOutputs.size());
        } else {
            logger::warn("COBJSets: CraftingTanningRack keyword {:06X} not found in {}",
                         Settings::uCraftingTanningRack, Settings::sTanningRackPlugin);
        }

        return sets;
    }

    // Lazy-cached COBJSets — built once, cached for session
    static float RandomJitter() {
        static std::mt19937 rng{std::random_device{}()};
        static std::uniform_real_distribution<float> dist(-6.0f, 6.0f);
        return dist(rng);
    }

    static bool s_cobjSetsBuilt = false;
    static COBJSets s_cobjSetsCache;

    const COBJSets& GetCOBJSets() {
        if (!s_cobjSetsBuilt) {
            s_cobjSetsCache = BuildCOBJSets();
            s_cobjSetsBuilt = true;
        }
        return s_cobjSetsCache;
    }

    PipelineResult RunPipeline(
        const std::vector<FilterStage>& a_filters,
        RE::FormID a_catchAllFormID,
        RE::FormID a_masterFormID,
        const std::vector<PoolItem>& a_pool,
        bool a_resolveRefs) {

        PipelineResult result;
        result.filterOutcomes.resize(a_filters.size());

        auto* registry = FilterRegistry::GetSingleton();
        bool hasCatchAll = (a_catchAllFormID != 0 && a_catchAllFormID != a_masterFormID);

        // Pre-resolve container refs if needed for route building
        std::vector<RE::TESObjectREFR*> filterRefs;
        RE::TESObjectREFR* catchAllRef = nullptr;
        if (a_resolveRefs) {
            filterRefs.resize(a_filters.size(), nullptr);
            for (size_t i = 0; i < a_filters.size(); ++i) {
                if (a_filters[i].containerFormID != 0) {
                    filterRefs[i] = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_filters[i].containerFormID);
                }
            }
            if (hasCatchAll) {
                catchAllRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_catchAllFormID);
            }
        }

        for (const auto& poolItem : a_pool) {
            if (!poolItem.item || poolItem.count <= 0) continue;

            int firstMatch = -1;

            for (size_t i = 0; i < a_filters.size(); ++i) {
                // Unlinked filters are invisible to the pipeline
                if (a_filters[i].containerFormID == 0) continue;

                auto* filter = registry->GetFilter(a_filters[i].filterID);
                if (!filter || !filter->Matches(poolItem.item)) continue;

                if (firstMatch == -1) {
                    // First matching linked filter claims this item
                    firstMatch = static_cast<int>(i);
                    result.filterOutcomes[i].claimedCount += poolItem.count;
                    if (a_resolveRefs && filterRefs[i]) {
                        result.routes.push_back({poolItem.item, poolItem.count, filterRefs[i]});
                    }
                } else {
                    // Subsequent matching filters: contested
                    result.filterOutcomes[i].contestedCount += poolItem.count;
                    result.filterOutcomes[i].contestedBy[static_cast<size_t>(firstMatch)] += poolItem.count;
                }
            }

            if (firstMatch == -1) {
                // No filter claimed — catch-all or origin
                if (hasCatchAll) {
                    result.catchAllCount += poolItem.count;
                    if (a_resolveRefs && catchAllRef) {
                        result.routes.push_back({poolItem.item, poolItem.count, catchAllRef});
                    }
                } else {
                    result.originCount += poolItem.count;
                }
            }
        }

        return result;
    }

    uint32_t GatherToMaster(const std::string& a_networkName) {
        auto* mgr = NetworkManager::GetSingleton();
        auto* net = mgr->FindNetwork(a_networkName);
        if (!net) {
            logger::error("GatherToMaster: network '{}' not found", a_networkName);
            return 0;
        }

        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(net->masterFormID);
        if (!masterRef) {
            logger::error("GatherToMaster: master container {:08X} not found", net->masterFormID);
            return 0;
        }

        // Collect all pipeline containers (filters + catch-all, excluding master itself)
        std::set<RE::FormID> activeContainers;
        for (const auto& filter : net->filters) {
            if (filter.containerFormID != 0 && filter.containerFormID != net->masterFormID) {
                activeContainers.insert(filter.containerFormID);
            }
        }
        if (net->catchAllFormID != 0 && net->catchAllFormID != net->masterFormID) {
            activeContainers.insert(net->catchAllFormID);
        }

        struct GatherEntry {
            RE::TESBoundObject* item;
            int32_t count;
            RE::TESObjectREFR* source;
        };
        std::vector<GatherEntry> toGather;

        for (auto containerID : activeContainers) {
            auto* containerRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(containerID);
            if (!containerRef) continue;

            auto inv = containerRef->GetInventory();
            for (auto& [item, data] : inv) {
                if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                toGather.push_back({item, data.first, containerRef});
            }
        }

        uint32_t totalItems = 0;
        for (const auto& entry : toGather) {
            logger::debug("  Gathering {}x {} from {:08X} to master",
                         entry.count, entry.item->GetName(), entry.source->GetFormID());
            entry.source->RemoveItem(entry.item, entry.count, RE::ITEM_REMOVE_REASON::kStoreInContainer,
                                     nullptr, masterRef);
            totalItems += entry.count;
        }

        logger::info("GatherToMaster: gathered {} items from {} containers in network '{}'",
                     totalItems, activeContainers.size(), a_networkName);
        return totalItems;
    }

    DistributeResult Distribute(const std::string& a_networkName) {
        DistributeResult result;

        auto* mgr = NetworkManager::GetSingleton();
        auto* net = mgr->FindNetwork(a_networkName);
        if (!net) {
            logger::error("Distribute: network '{}' not found", a_networkName);
            return result;
        }

        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(net->masterFormID);
        if (!masterRef) {
            logger::error("Distribute: master container {:08X} not found", net->masterFormID);
            return result;
        }

        // Phase 1: Gather all items from pipeline containers to master
        GatherToMaster(a_networkName);

        // Phase 2: Build pool from master inventory
        std::vector<PoolItem> pool;
        auto masterInv = masterRef->GetInventory();
        for (auto& [item, data] : masterInv) {
            if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
            pool.push_back({item, data.first});
        }

        // Phase 3: Run pipeline
        auto pipelineResult = RunPipeline(net->filters, net->catchAllFormID,
                                          net->masterFormID, pool, true);

        // Phase 4: Execute routes
        std::map<RE::TESObjectREFR*, uint32_t> perContainerCount;
        for (const auto& route : pipelineResult.routes) {
            logger::debug("  Moving {}x {} -> {:08X}",
                         route.count,
                         route.item->GetName(),
                         route.target->GetFormID());

            masterRef->RemoveItem(route.item, route.count, RE::ITEM_REMOVE_REASON::kStoreInContainer,
                                  nullptr, route.target);
            result.totalItems += route.count;
            perContainerCount[route.target] += route.count;
        }

        // Build per-container result with names
        for (const auto& [containerRef, count] : perContainerCount) {
            std::string name = "Container";
            if (auto* base = containerRef->GetBaseObject()) {
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                }
            }
            result.perContainer.emplace_back(name, count);
        }

        logger::info("Sort: distributed {} items in network '{}'", result.totalItems, a_networkName);

        return result;
    }

    PredictionResult PredictDistribution(
        RE::FormID a_masterFormID,
        const std::vector<FilterStage>& a_filters,
        RE::FormID a_catchAllFormID) {

        PredictionResult result;
        result.filterCounts.resize(a_filters.size(), 0);
        result.contestedCounts.resize(a_filters.size(), 0);
        result.contestedByMaps.resize(a_filters.size());

        // Derive the set of all containers from the inputs
        std::set<RE::FormID> allContainers;
        if (a_masterFormID != 0) allContainers.insert(a_masterFormID);
        for (const auto& f : a_filters) {
            if (f.containerFormID != 0) allContainers.insert(f.containerFormID);
        }
        if (a_catchAllFormID != 0) allContainers.insert(a_catchAllFormID);

        // Build item pool from all containers (simulates GatherToMaster)
        std::vector<PoolItem> pool;
        for (auto formID : allContainers) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
            if (!ref) continue;
            auto inv = ref->GetInventory();
            for (auto& [item, data] : inv) {
                if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                pool.push_back({item, data.first});
            }
        }

        // Run pipeline (counts only, no ref resolution)
        auto pipelineResult = RunPipeline(a_filters, a_catchAllFormID, a_masterFormID,
                                          pool, false);

        // Map pipeline outcomes to prediction result
        for (size_t i = 0; i < pipelineResult.filterOutcomes.size(); ++i) {
            result.filterCounts[i] = pipelineResult.filterOutcomes[i].claimedCount;
            result.contestedCounts[i] = pipelineResult.filterOutcomes[i].contestedCount;
            result.contestedByMaps[i] = pipelineResult.filterOutcomes[i].contestedBy;
        }
        result.catchAllCount = pipelineResult.catchAllCount;
        result.originCount = pipelineResult.originCount;

        return result;
    }

    uint32_t Whoosh(const std::string& a_networkName) {
        auto* mgr = NetworkManager::GetSingleton();
        auto* net = mgr->FindNetwork(a_networkName);
        if (!net) {
            logger::error("Whoosh: network '{}' not found", a_networkName);
            return 0;
        }

        if (!net->whooshConfigured) {
            return 0;
        }

        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(net->masterFormID);
        if (!masterRef) {
            logger::error("Whoosh: master container {:08X} not found", net->masterFormID);
            return 0;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::error("Whoosh: player not available");
            return 0;
        }

        auto* registry = FilterRegistry::GetSingleton();

        struct MoveEntry {
            RE::TESBoundObject* item;
            int32_t count;
        };
        std::vector<MoveEntry> toMove;

        auto playerInv = player->GetInventory();
        for (auto& [item, data] : playerInv) {
            if (!item || data.first <= 0 || IsPhantomItem(item)) continue;

            auto& invData = data.second;

            if (invData->IsQuestObject()) continue;
            if (invData->IsWorn()) continue;
            if (invData->IsFavorited()) continue;

            if (item->IsGold()) continue;
            if (item->IsLockpick()) continue;
            if (item->GetFormType() == RE::FormType::Light) continue;

            // Item drains if ANY enabled filter matches
            bool shouldDrain = false;
            for (const auto& filterID : net->whooshFilters) {
                auto* filter = registry->GetFilter(filterID);
                if (filter && filter->Matches(item)) {
                    shouldDrain = true;
                    logger::debug("  Whoosh check: {} matched filter '{}'",
                                 item->GetName(), filterID);
                    break;
                }
            }

            if (shouldDrain) {
                toMove.push_back({item, data.first});
            }
        }

        uint32_t movedCount = 0;
        for (const auto& entry : toMove) {
            logger::debug("  Whoosh: {}x {}", entry.count, entry.item->GetName());

            player->RemoveItem(entry.item, entry.count, RE::ITEM_REMOVE_REASON::kStoreInContainer,
                               nullptr, masterRef);
            movedCount += entry.count;
        }

        logger::info("Whoosh: moved {} items ({} stacks) from player to master {:08X} in network '{}'",
                     movedCount, toMove.size(), net->masterFormID, a_networkName);

        return movedCount;
    }

    SalesResult ProcessSales() {
        SalesResult result;

        auto* mgr = NetworkManager::GetSingleton();
        if (!mgr->HasSellContainer()) {
            return result;
        }

        auto sellFormID = mgr->GetSellContainerFormID();
        auto* sellRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(sellFormID);
        if (!sellRef) {
            logger::warn("ProcessSales: sell container {:08X} not found", sellFormID);
            return result;
        }

        // Timer check: if timer started and not enough time elapsed, skip
        const auto& sellState = mgr->GetSellState();
        if (sellState.timerStarted) {
            auto* calendar = RE::Calendar::GetSingleton();
            if (calendar) {
                float currentHours = calendar->GetHoursPassed();
                float elapsed = currentHours - sellState.lastSellTime;
                if (elapsed < Settings::fSellIntervalHours) {
                    logger::debug("ProcessSales: timer not elapsed ({:.1f}h / {:.1f}h)",
                                  elapsed, Settings::fSellIntervalHours);
                    return result;
                }
            }
        }

        // Iterate sell container inventory
        auto inv = sellRef->GetInventory();
        if (inv.empty()) {
            logger::debug("ProcessSales: sell container is empty");
            return result;
        }

        // Gold form for depositing
        constexpr RE::FormID kGold001 = 0x0000000F;
        auto* goldForm = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold001);
        if (!goldForm) {
            logger::error("ProcessSales: Gold001 form not found");
            return result;
        }

        // Get current game time for transaction records
        float gameTime = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            gameTime = calendar->GetHoursPassed();
        }

        struct SellEntry {
            RE::TESBoundObject* item;
            int32_t count;
            float   pricePerUnit;  // float — truncate per-line total, not per-unit
            std::string name;
        };
        std::vector<SellEntry> toSell;
        int32_t itemsCollected = 0;

        for (auto& [item, data] : inv) {
            if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
            if (item->IsGold()) continue;

            int32_t baseValue = item->GetGoldValue();
            float pricePerUnit = baseValue * Settings::fSellPricePercent;

            // Collect up to batch size
            int32_t available = data.first;
            int32_t toTake = std::min(available, static_cast<int32_t>(Settings::iSellBatchSize) - itemsCollected);
            if (toTake <= 0) break;

            std::string name = item->GetName();
            if (name.empty()) name = "Unknown Item";

            toSell.push_back({item, toTake, pricePerUnit, name});
            itemsCollected += toTake;

            if (itemsCollected >= Settings::iSellBatchSize) break;
        }

        if (toSell.empty()) {
            return result;
        }

        // Execute sales — accumulate float, truncate per line item
        uint32_t totalGold = 0;
        for (const auto& entry : toSell) {
            int32_t goldEarned = static_cast<int32_t>(entry.pricePerUnit * entry.count);
            totalGold += goldEarned;

            sellRef->RemoveItem(entry.item, entry.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, nullptr);

            SaleTransaction tx;
            tx.itemName = entry.name;
            tx.vendorName = "General Vendor";
            tx.vendorAssortment = "General Assortment";
            tx.quantity = entry.count;
            tx.goldEarned = goldEarned;
            tx.pricePerUnit = entry.pricePerUnit;
            tx.gameTime = gameTime;
            result.transactions.push_back(std::move(tx));

            logger::debug("  Sold {}x {} for {} gold ({:.2f} per unit)",
                         entry.count, entry.name, goldEarned, entry.pricePerUnit);
        }

        // Deposit gold into sell container
        if (totalGold > 0) {
            sellRef->AddObjectToContainer(goldForm, nullptr, totalGold, nullptr);
        }

        result.itemsSold = static_cast<uint32_t>(itemsCollected);
        result.goldEarned = totalGold;

        // Record in NetworkManager
        mgr->RecordSale(result.itemsSold, result.goldEarned);
        mgr->SetLastSellTime(gameTime + RandomJitter());

        logger::info("ProcessSales: sold {} items for {} gold from sell container {:08X}",
                     result.itemsSold, result.goldEarned, sellFormID);

        return result;
    }

    VendorSalesResult ProcessVendorSales() {
        VendorSalesResult result;

        auto* mgr = NetworkManager::GetSingleton();
        if (!mgr->HasSellContainer()) return result;

        auto sellFormID = mgr->GetSellContainerFormID();
        auto* sellRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(sellFormID);
        if (!sellRef) {
            logger::warn("ProcessVendorSales: sell container {:08X} not found", sellFormID);
            return result;
        }

        auto* vendorReg = VendorRegistry::GetSingleton();
        const auto& vendors = vendorReg->GetVendors();
        if (vendors.empty()) return result;

        float currentHours = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            currentHours = calendar->GetHoursPassed();
        }

        constexpr RE::FormID kGold001 = 0x0000000F;
        auto* goldForm = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold001);
        if (!goldForm) {
            logger::error("ProcessVendorSales: Gold001 form not found");
            return result;
        }

        for (const auto& vendor : vendors) {
            if (!vendor.active) continue;

            // Timer check
            float elapsed = currentHours - vendor.lastVisitTime;
            if (elapsed < Settings::fVendorIntervalHours) {
                logger::debug("ProcessVendorSales: {} — timer not elapsed ({:.1f}h / {:.1f}h)",
                              vendor.vendorName, elapsed, Settings::fVendorIntervalHours);
                continue;
            }

            // Look up the vendor's buy list from their faction
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(vendor.factionFormID);
            if (!faction) {
                logger::warn("ProcessVendorSales: faction {:08X} for {} not found",
                             vendor.factionFormID, vendor.vendorName);
                continue;
            }

            auto* buyList = faction->vendorData.vendorSellBuyList;
            bool inverted = faction->vendorData.vendorValues.notBuySell;

            // Scan sell container for items matching this vendor's buy list
            auto inv = sellRef->GetInventory();
            if (inv.empty()) {
                logger::debug("ProcessVendorSales: sell container empty, skipping {}", vendor.vendorName);
                continue;
            }

            struct VendorBuyEntry {
                RE::TESBoundObject* item;
                int32_t count;
                float pricePerUnit;
                std::string name;
            };
            std::vector<VendorBuyEntry> toBuy;
            int32_t itemsCollected = 0;

            for (auto& [item, data] : inv) {
                if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                if (item->IsGold()) continue;

                // Check if item matches vendor's buy list
                bool matches = false;
                if (buyList) {
                    // BGSListForm contains keywords — check if item has any of them
                    auto* keyworded = item->As<RE::BGSKeywordForm>();
                    if (keyworded) {
                        buyList->ForEachForm([&](RE::TESForm& a_form) {
                            auto* keyword = a_form.As<RE::BGSKeyword>();
                            if (keyword && keyworded->HasKeyword(keyword)) {
                                matches = true;
                                return RE::BSContainer::ForEachResult::kStop;
                            }
                            return RE::BSContainer::ForEachResult::kContinue;
                        });
                    }
                } else {
                    // No buy list = buys nothing (unless inverted)
                    matches = false;
                }

                // Apply inversion if set
                if (inverted) matches = !matches;

                if (!matches) continue;

                int32_t baseValue = item->GetGoldValue();
                float pricePerUnit = baseValue * Settings::fVendorPricePercent;
                if (vendor.invested) {
                    pricePerUnit *= 1.05f;
                }

                int32_t available = data.first;
                int32_t toTake = std::min(available,
                    static_cast<int32_t>(Settings::iVendorBatchSize) - itemsCollected);
                if (toTake <= 0) break;

                std::string name = item->GetName();
                if (name.empty()) name = "Unknown Item";

                toBuy.push_back({item, toTake, pricePerUnit, name});
                itemsCollected += toTake;

                if (itemsCollected >= Settings::iVendorBatchSize) break;
            }

            if (toBuy.empty()) {
                // Vendor visited but found nothing to buy — still update timer
                vendorReg->RecordVendorSale(vendor.npcBaseFormID, 0, 0, currentHours);
                logger::debug("ProcessVendorSales: {} visited but found no matching items",
                              vendor.vendorName);
                continue;
            }

            // Execute purchases
            uint32_t vendorGold = 0;
            for (const auto& entry : toBuy) {
                int32_t goldEarned = static_cast<int32_t>(entry.pricePerUnit * entry.count);
                vendorGold += goldEarned;

                sellRef->RemoveItem(entry.item, entry.count,
                    RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, nullptr);

                SaleTransaction tx;
                tx.itemName = entry.name;
                tx.vendorName = vendor.vendorName;
                tx.vendorAssortment = vendor.storeName;
                tx.quantity = entry.count;
                tx.goldEarned = goldEarned;
                tx.pricePerUnit = entry.pricePerUnit;
                tx.gameTime = currentHours;
                result.transactions.push_back(std::move(tx));

                logger::debug("  {} bought {}x {} for {} gold ({:.2f}/unit)",
                              vendor.vendorName, entry.count, entry.name,
                              goldEarned, entry.pricePerUnit);
            }

            // Deposit gold into sell container
            if (vendorGold > 0) {
                sellRef->AddObjectToContainer(goldForm, nullptr, vendorGold, nullptr);
            }

            // Record in vendor registry
            vendorReg->RecordVendorSale(vendor.npcBaseFormID,
                static_cast<uint32_t>(itemsCollected), vendorGold, currentHours);

            result.totalItemsSold += itemsCollected;
            result.totalGoldEarned += vendorGold;
            ++result.vendorsVisited;

            logger::info("ProcessVendorSales: {} bought {} items for {} gold",
                         vendor.vendorName, itemsCollected, vendorGold);
        }

        if (result.vendorsVisited > 0) {
            logger::info("ProcessVendorSales: {} vendors visited, {} items sold for {} gold total",
                         result.vendorsVisited, result.totalItemsSold, result.totalGoldEarned);
        }

        return result;
    }
}
