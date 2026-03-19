#include "ConfigState.h"
#include "ContainerRegistry.h"
#include "FilterRegistry.h"
#include "NetworkManager.h"
#include "TranslationService.h"

namespace ConfigState {

    // --- Network context ---
    static std::string  s_networkName;
    static RE::FormID   s_masterFormID = 0;

    // --- Network context ---

    void SetContext(const std::string& a_networkName, RE::FormID a_masterFormID) {
        s_networkName = a_networkName;
        s_masterFormID = a_masterFormID;
    }

    const std::string& GetNetworkName() { return s_networkName; }
    RE::FormID GetMasterFormID() { return s_masterFormID; }

    // --- Network data loading ---

    LoadedNetwork BuildFromNetwork() {
        LoadedNetwork result;
        auto* net = NetworkManager::GetSingleton()->FindNetwork(s_networkName);
        auto* registry = ContainerRegistry::GetSingleton();

        if (!net) {
            // No network configured — return a single catch-all stage (Keep/master)
            LoadedStage catchAllStage;
            catchAllStage.filterID = FilterRegistry::kCatchAllFilterID;
            catchAllStage.name = "Catch-All";
            catchAllStage.containerName = T("$SLID_Keep");
            catchAllStage.containerFormID = s_masterFormID;
            catchAllStage.location = "";
            catchAllStage.count = 0;
            result.stages.push_back(std::move(catchAllStage));
            return result;
        }

        result.hasNetwork = true;

        // Build all stages uniformly (including catch-all as last entry)
        for (const auto& filter : net->filters) {
            LoadedStage stage;
            stage.filterID = filter.filterID;

            if (FilterRegistry::IsCatchAll(filter.filterID)) {
                stage.name = "Catch-All";
            } else {
                auto* regFilter = FilterRegistry::GetSingleton()->GetFilter(filter.filterID);
                stage.name = regFilter ? std::string(regFilter->GetDisplayName()) : filter.filterID;
            }

            if (filter.containerFormID == s_masterFormID && filter.containerFormID != 0) {
                // "Keep" — items stay in master, no separate container to count
                stage.containerName = T("$SLID_Keep");
                stage.containerFormID = filter.containerFormID;
                stage.location = "";
                stage.count = 0;
            } else if (filter.containerFormID != 0) {
                auto display = registry->Resolve(filter.containerFormID);
                stage.containerName = display.name;
                stage.containerFormID = filter.containerFormID;
                stage.location = display.location;
                stage.count = registry->CountItems(filter.containerFormID);
            } else {
                if (FilterRegistry::IsCatchAll(filter.filterID)) {
                    // Catch-all with containerFormID == 0 means Keep (master)
                    stage.containerName = T("$SLID_Keep");
                    stage.containerFormID = s_masterFormID;
                    stage.location = "";
                    stage.count = 0;
                } else {
                    // "Pass" — filter skipped
                    stage.containerName = T("$SLID_Pass");
                    stage.containerFormID = 0;
                    stage.location = "";
                    stage.count = 0;
                }
            }
            result.stages.push_back(std::move(stage));
        }

        return result;
    }

    // --- Commit ---

    void CommitToNetwork(const std::string& a_networkName,
                         const std::vector<FilterStage>& a_filters) {
        auto* mgr = NetworkManager::GetSingleton();
        mgr->SetFilterConfig(a_networkName, a_filters);
        logger::debug("CommitToNetwork: saved {} filters (catchAll={:08X}) to network '{}'",
                     a_filters.size(), ExtractCatchAllFormID(a_filters), a_networkName);
    }
}
