#include "ContainerRegistry.h"
#include "NetworkManager.h"
#include "TranslationService.h"

namespace {
    // Colors for special entries
    constexpr uint32_t COLOR_KEEP = 0x88CC88;  // Light green
    constexpr uint32_t COLOR_PASS = 0xDDAA44;  // Amber
    constexpr uint32_t COLOR_SELL = 0x88BBDD;  // Light blue
    constexpr uint32_t COLOR_DISABLED = 0x555555;  // Gray

    // Special FormID markers
    constexpr RE::FormID PASS_FORMID = 0;
}

/**
 * SpecialContainerSource - Provides Keep, Pass, and Sell Container entries
 *
 * Group 0 entries that appear at the top of the picker:
 * - Keep: Items stay in master container (uses master FormID)
 * - Pass: Skip this filter, items fall through (FormID 0)
 * - Sell: Items route to designated sell container
 */
class SpecialContainerSource : public IContainerSource {
public:
    // Takes the current master FormID for this network context
    explicit SpecialContainerSource() = default;

    const char* GetSourceID() const override { return "special"; }

    // Highest priority - special entries are always checked first
    int GetPriority() const override { return 0; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        // We own:
        // 1. Pass (FormID 0)
        // 2. Sell container (if designated)
        // Note: Keep uses the master FormID which varies by network context
        // Keep is handled via picker entry but ownership is complex - we claim
        // the sell container specifically

        if (a_formID == PASS_FORMID) return true;

        auto sellFormID = NetworkManager::GetSingleton()->GetSellContainerFormID();
        if (sellFormID != 0 && a_formID == sellFormID) return true;

        return false;
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        // Pass
        if (a_formID == PASS_FORMID) {
            return ContainerDisplay{
                .name = T("$SLID_Pass"),
                .location = "",
                .color = COLOR_PASS,
                .available = true,
                .group = 0
            };
        }

        // Sell container
        auto sellFormID = NetworkManager::GetSingleton()->GetSellContainerFormID();
        if (sellFormID != 0 && a_formID == sellFormID) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(sellFormID);
            bool available = (ref != nullptr);

            std::string location;
            if (ref) {
                auto* cell = ref->GetParentCell();
                if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                    location = cell->GetFullName();
                }
            }

            return ContainerDisplay{
                .name = T("$SLID_SellContainer"),
                .location = location,
                .color = available ? COLOR_SELL : COLOR_DISABLED,
                .available = available,
                .group = 0
            };
        }

        // Fallback - shouldn't happen if OwnsContainer is called first
        return ContainerDisplay{
            .name = "Unknown",
            .location = "",
            .color = COLOR_DISABLED,
            .available = false,
            .group = 0
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;

        // Keep - items stay in master container
        if (a_masterFormID != 0) {
            entries.push_back(PickerEntry{
                .name = T("$SLID_Keep"),
                .location = "",
                .formID = a_masterFormID,
                .isTagged = false,
                .color = COLOR_KEEP,
                .group = 0,
                .enabled = true
            });
        }

        // Pass - filter skipped, items fall through
        entries.push_back(PickerEntry{
            .name = T("$SLID_Pass"),
            .location = "",
            .formID = PASS_FORMID,
            .isTagged = false,
            .color = COLOR_PASS,
            .group = 0,
            .enabled = true
        });

        // Sell container - always shown; disabled when none designated
        auto* mgr = NetworkManager::GetSingleton();
        auto sellFormID = mgr->GetSellContainerFormID();
        if (sellFormID != 0 && sellFormID != a_masterFormID) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(sellFormID);
            std::string location;
            if (ref) {
                auto* cell = ref->GetParentCell();
                if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                    location = cell->GetFullName();
                }
            }
            entries.push_back(PickerEntry{
                .name = T("$SLID_SellContainer"),
                .location = location,
                .formID = sellFormID,
                .isTagged = false,
                .color = COLOR_SELL,
                .group = 0,
                .enabled = true
            });
        } else {
            // No sell container - show disabled entry
            entries.push_back(PickerEntry{
                .name = T("$SLID_SellContainer"),
                .location = "",
                .formID = 0,
                .isTagged = false,
                .color = COLOR_DISABLED,
                .group = 0,
                .enabled = false
            });
        }

        return entries;
    }
};

// Registration function called from main.cpp
void RegisterSpecialContainerSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<SpecialContainerSource>()
    );
}
