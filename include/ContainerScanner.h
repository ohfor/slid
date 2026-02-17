#pragma once

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

    // Container picker entry (used by picker and filter dialogue)
    struct PickerEntry {
        std::string name;       // display name (Keep/Pass/Sell Container/container name)
        std::string location;   // cell name (empty for special entries)
        RE::FormID formID;      // 0 for Pass
        bool isTagged = false;  // true = from tag registry (highlighted in picker)
        uint32_t color = 0;     // 0 = use default per-category color
        uint8_t group = 0;      // 0=special, 1=tagged, 2=scanned
        bool enabled = true;    // false = visible but not selectable
    };

    // Scans current cell for non-respawning / player-owned containers.
    // Excludes disabled, deleted, non-container refs, and the specified FormID.
    std::vector<ScannedContainer> ScanCellContainers(RE::FormID a_excludeFormID);

    // Resolves a container FormID to {displayName, locationName}.
    // Uses tag registry for custom names, base object name as fallback.
    std::pair<std::string, std::string> ResolveContainerInfo(RE::FormID a_formID);

    // Builds an ordered list of container entries for the picker/dialogue.
    // None → Sell → (Master if a_includeMaster) → Tagged → Scanned.
    std::vector<PickerEntry> BuildContainerList(RE::FormID a_masterFormID, bool a_includeMaster);
}
