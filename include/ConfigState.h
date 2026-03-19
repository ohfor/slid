#pragma once

#include "Network.h"

#include <string>
#include <vector>

namespace ConfigState {

    // --- Network context (session identity) ---

    void SetContext(const std::string& a_networkName, RE::FormID a_masterFormID);
    const std::string& GetNetworkName();
    RE::FormID GetMasterFormID();

    // --- Network data loading ---

    struct LoadedStage {
        std::string filterID;
        std::string name;
        std::string containerName;
        std::string location;
        RE::FormID  containerFormID = 0;
        int         count = 0;
    };

    struct LoadedNetwork {
        std::vector<LoadedStage> stages;  // includes catch-all as last entry
        bool                     hasNetwork = false;
    };

    // Build stage data from the current network (catch-all is the last entry).
    LoadedNetwork BuildFromNetwork();

    // --- Commit ---

    // Push in-memory filter stages (including catch-all) to NetworkManager.
    void CommitToNetwork(const std::string& a_networkName,
                         const std::vector<FilterStage>& a_filters);
}
