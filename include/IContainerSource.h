#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * IContainerSource - Interface for container data providers
 *
 * GUIDING PRINCIPLE: Adding a new container source = one new file + one Register() call.
 * Zero changes to registry, picker, or UI code.
 *
 * == IContainerSource Implementation Rules ==
 *
 * MUST:
 * 1. Return a stable GetSourceID() - used for debugging/logging only
 * 2. Return consistent GetPriority() - determines claim order (lower = first)
 * 3. OwnsContainer() must be pure and fast - no side effects, no I/O
 * 4. Resolve() must handle any FormID passed to it (return valid ContainerDisplay even for edge cases)
 * 5. GetPickerEntries() must return entries with formID, name, group, enabled, color all populated
 * 6. Own all source-specific logic: colors, names, availability checks, picker entry construction
 *
 * MUST NOT:
 * 1. Know about other sources (no "if (SCIEIntegration::OwnsThis(...))" checks)
 * 2. Cache availability state - compute fresh each Resolve() call
 * 3. Depend on specific registration order (beyond priority)
 * 4. Modify global state in any method
 * 5. Return entries from GetPickerEntries() that fail OwnsContainer() for their FormID
 *
 * INVARIANTS:
 * - If OwnsContainer(id) returns true, Resolve(id) must return meaningful data
 * - GetPickerEntries() results must all pass OwnsContainer() for their FormIDs
 * - Multiple calls to Resolve(id) may return different results (availability can change) - this is expected
 * - Priority determines claim order, not picker display order (that's by group)
 */

// Display information for a resolved container
struct ContainerDisplay {
    std::string name;        // Display name ("Keep", "Sell Container", "Alchemy Chest", etc.)
    std::string location;    // Secondary text (cell name, context, etc.) - empty for special entries
    uint32_t color;          // Source-defined color (0 = use default)
    bool available;          // Can items transfer to this container? (false = show disabled)
    uint8_t group;           // Picker ordering group (lower = appears first)
};

// Entry for container picker dropdown
struct PickerEntry {
    std::string name;        // Display name (Keep/Pass/Sell Container/container name)
    std::string location;    // Cell name (empty for special entries)
    RE::FormID formID;       // 0 for Pass
    bool isTagged = false;   // true = from tag registry (highlighted in picker)
    uint32_t color = 0;      // 0 = use default per-category color
    uint8_t group = 0;       // 0=special, 1=follower, 2=tagged, 3=SCIE, 4=scanned
    bool enabled = true;     // false = visible but not selectable
};

class IContainerSource {
public:
    virtual ~IContainerSource() = default;

    // Unique identifier for this source (debugging/logging only)
    virtual const char* GetSourceID() const = 0;

    // Priority for claim resolution (lower = checked first)
    // Default 100 - special sources use lower values
    virtual int GetPriority() const { return 100; }

    // Does this source own/manage this container?
    // Must be pure and fast - no side effects, no I/O
    virtual bool OwnsContainer(RE::FormID a_formID) const = 0;

    // Get display info for a container this source owns
    // Called when OwnsContainer() returned true
    // Must handle edge cases gracefully (deleted refs, etc.)
    virtual ContainerDisplay Resolve(RE::FormID a_formID) const = 0;

    // Get all containers from this source for the picker dropdown
    // a_masterFormID: current network's master container (for Keep entry)
    // All returned entries MUST pass OwnsContainer() for their FormIDs
    virtual std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const = 0;

    // Count total playable items in a container this source owns.
    // Default implementation uses LookupByID + GetInventory (works for normal placed refs).
    // Sources with non-standard container refs should override.
    virtual int CountItems(RE::FormID a_formID) const {
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        if (!ref) return 0;
        int count = 0;
        auto inv = ref->GetInventory();
        for (auto& [item, data] : inv) {
            if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
            count += data.first;
        }
        return count;
    }
};
