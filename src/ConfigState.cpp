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
            // No network configured — default catch-all is Keep (master)
            result.catchAll.containerName = T("$SLID_Keep");
            result.catchAll.containerFormID = s_masterFormID;
            result.catchAll.location = "";
            result.catchAll.count = 0;
            return result;
        }

        result.hasNetwork = true;

        // Build filter stages
        for (const auto& filter : net->filters) {
            LoadedStage stage;
            stage.filterID = filter.filterID;
            auto* regFilter = FilterRegistry::GetSingleton()->GetFilter(filter.filterID);
            stage.name = regFilter ? std::string(regFilter->GetDisplayName()) : filter.filterID;

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
                // "Pass" — filter skipped
                stage.containerName = T("$SLID_Pass");
                stage.containerFormID = 0;
                stage.location = "";
                stage.count = 0;
            }
            result.stages.push_back(std::move(stage));
        }

        // Build catch-all
        if (net->catchAllFormID != 0 && net->catchAllFormID != s_masterFormID) {
            auto display = registry->Resolve(net->catchAllFormID);
            result.catchAll.containerName = display.name;
            result.catchAll.containerFormID = net->catchAllFormID;
            result.catchAll.location = display.location;
            result.catchAll.count = registry->CountItems(net->catchAllFormID);
        } else {
            // Keep — catch-all routes to master (or no catch-all configured)
            result.catchAll.containerFormID = s_masterFormID;
            result.catchAll.containerName = T("$SLID_Keep");
            result.catchAll.location = "";
            result.catchAll.count = 0;
        }

        return result;
    }

    // --- Commit ---

    void CommitToNetwork(const std::string& a_networkName,
                         const std::vector<FilterStage>& a_filters,
                         RE::FormID a_catchAllFormID) {
        auto* mgr = NetworkManager::GetSingleton();
        mgr->SetFilterConfig(a_networkName, a_filters, a_catchAllFormID);
        logger::debug("CommitToNetwork: saved {} filters, catchAll={:08X} to network '{}'",
                     a_filters.size(), a_catchAllFormID, a_networkName);
    }
}
