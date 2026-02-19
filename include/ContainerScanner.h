#pragma once

#include "IContainerSource.h"

#include <set>
#include <string>
#include <vector>

namespace ContainerScanner {

    // Result from ScanCellContainers
    struct ScannedContainer {
        RE::FormID formID;
        std::string gameName;
        std::string cellName;
    };

    // Scans current cell for non-respawning / player-owned containers.
    // Excludes disabled, deleted, non-container refs, and the specified FormID.
    // Used internally by CellScanContainerSource.
    std::vector<ScannedContainer> ScanCellContainers(RE::FormID a_excludeFormID);

    // Resolves a container FormID to {displayName, locationName}.
    // DEPRECATED: Delegates to ContainerRegistry::Resolve().
    // UI code should call ContainerRegistry directly.
    std::pair<std::string, std::string> ResolveContainerInfo(RE::FormID a_formID);

    // Builds an ordered list of container entries for the picker/dialogue.
    // DEPRECATED: Delegates to ContainerRegistry::BuildPickerList().
    // UI code should call ContainerRegistry directly.
    std::vector<PickerEntry> BuildContainerList(RE::FormID a_masterFormID, bool a_includeMaster);
}
