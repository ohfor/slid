#include "ContainerRegistry.h"

namespace {
    constexpr uint32_t COLOR_FOLLOWER = 0xFFAA66;  // Warm orange (same as NFF — shared group)
    constexpr uint32_t COLOR_DISABLED = 0x555555;

    constexpr const char* KWF_ESP = "KhajiitWillFollow.esp";
    constexpr RE::FormID KWF_QUEST_LOCAL_ID = 0x042D8D;

    // KWF hardcoded follower-to-storage mapping.
    // Follower aliases: 8=Bikhai, 12=Makara, 13=Nanak, 14=Sariq
    // Storage container REFRs: each has a dedicated REFR with a named CONT base.
    struct KWFMapping {
        uint32_t followerAlias;     // Quest alias index for the follower
        RE::FormID containerLocal;  // Local FormID of the storage container REFR
        const char* followerName;   // Display name (fallback if alias ref unavailable)
    };

    constexpr KWFMapping KWF_MAPPINGS[] = {
        { 8,  0x8AB797, "Bikhai" },
        { 12, 0x8AB798, "Makara" },
        { 13, 0x8AB799, "Nanak" },
        { 14, 0x8AB79A, "Sariq" },
    };
}

/**
 * KWFContainerSource - Provides Khajiit Will Follow storage containers
 *
 * Group 1 entries. KWF has 4 fixed followers with dedicated storage container
 * REFRs. Each follower's recruitment status is tracked via quest alias fill:
 * if the alias has a ref, the follower is recruited and their container is available.
 */
class KWFContainerSource : public IContainerSource {
    RE::TESQuest* quest_ = nullptr;

    struct ResolvedMapping {
        uint32_t followerAlias;
        RE::FormID containerFormID;  // Runtime FormID (0 if ESP not loaded)
        const char* followerName;
    };

    std::vector<ResolvedMapping> mappings_;

public:
    KWFContainerSource() {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;

        quest_ = dh->LookupForm<RE::TESQuest>(KWF_QUEST_LOCAL_ID, KWF_ESP);
        if (!quest_) {
            logger::info("KWFContainerSource: {} not installed, source inactive", KWF_ESP);
            return;
        }

        logger::info("KWFContainerSource: found quest {:08X}", quest_->GetFormID());

        // Resolve container REFRs to runtime FormIDs
        for (const auto& m : KWF_MAPPINGS) {
            auto* ref = dh->LookupForm<RE::TESObjectREFR>(m.containerLocal, KWF_ESP);
            if (ref) {
                mappings_.push_back({m.followerAlias, ref->GetFormID(), m.followerName});
                logger::debug("KWFContainerSource: {} container {:08X}", m.followerName, ref->GetFormID());
            } else {
                logger::warn("KWFContainerSource: container {:06X} not found for {}", m.containerLocal, m.followerName);
            }
        }
    }

    const char* GetSourceID() const override { return "kwf"; }

    int GetPriority() const override { return 6; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        if (a_formID == 0 || mappings_.empty()) return false;

        for (const auto& m : mappings_) {
            if (m.containerFormID == a_formID) return true;
        }
        return false;
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        for (const auto& m : mappings_) {
            if (m.containerFormID != a_formID) continue;

            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
            bool available = false;

            // Check if the follower is recruited (alias is filled)
            if (quest_) {
                auto it = quest_->refAliasMap.find(m.followerAlias);
                if (it != quest_->refAliasMap.end()) {
                    auto ptr = it->second.get();
                    if (ptr && ptr.get()) {
                        available = (ref != nullptr);
                    }
                }
            }

            // Get display name from the container's base object (KWF uses named CONTs)
            std::string name;
            if (ref && ref->GetBaseObject()) {
                auto* base = ref->GetBaseObject();
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                }
            }
            if (name.empty()) {
                name = std::string(m.followerName) + "'s Inventory";
            }

            logger::debug("KWF::Resolve: {:08X} -> '{}' follower={} available={}", a_formID, name, m.followerName, available);
            return ContainerDisplay{
                .name = name,
                .location = m.followerName,
                .color = available ? COLOR_FOLLOWER : COLOR_DISABLED,
                .available = available,
                .group = 1
            };
        }

        // Fallback — FormID not in any mapping
        logger::debug("KWF::Resolve: {:08X} not in mappings", a_formID);
        return ContainerDisplay{
            .name = "KWF Container",
            .location = "",
            .color = COLOR_DISABLED,
            .available = false,
            .group = 1
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;
        if (!quest_ || mappings_.empty()) return entries;

        logger::debug("KWF::GetPickerEntries: checking {} mappings (master={:08X})", mappings_.size(), a_masterFormID);

        for (const auto& m : mappings_) {
            // Check if follower is recruited (alias filled)
            auto it = quest_->refAliasMap.find(m.followerAlias);
            if (it == quest_->refAliasMap.end()) {
                logger::debug("KWF::GetPickerEntries: {} alias {} not in refAliasMap (not recruited)", m.followerName, m.followerAlias);
                continue;
            }

            auto ptr = it->second.get();
            if (!ptr || !ptr.get()) {
                logger::debug("KWF::GetPickerEntries: {} alias {} has stale handle (not recruited)", m.followerName, m.followerAlias);
                continue;
            }

            // Skip if this is the master container
            if (m.containerFormID == a_masterFormID) {
                logger::debug("KWF::GetPickerEntries: skipping {} container {:08X} (is master)", m.followerName, m.containerFormID);
                continue;
            }

            // Get display name from CONT base object
            std::string name;
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(m.containerFormID);
            if (ref && ref->GetBaseObject()) {
                auto* base = ref->GetBaseObject();
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                }
            }
            if (name.empty()) {
                name = std::string(m.followerName) + "'s Inventory";
            }

            logger::debug("KWF::GetPickerEntries: adding {} container {:08X} ('{}')", m.followerName, m.containerFormID, name);
            entries.push_back(PickerEntry{
                .name = name,
                .location = m.followerName,
                .formID = m.containerFormID,
                .isTagged = false,
                .color = COLOR_FOLLOWER,
                .group = 1,
                .enabled = true
            });
        }

        logger::debug("KWF::GetPickerEntries: returning {} entries", entries.size());
        return entries;
    }
};

// Registration function called from main.cpp
void RegisterKWFContainerSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<KWFContainerSource>()
    );
}
