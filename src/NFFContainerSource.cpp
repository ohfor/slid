#include "ContainerRegistry.h"

namespace {
    constexpr uint32_t COLOR_FOLLOWER = 0xFFAA66;  // Warm orange
    constexpr uint32_t COLOR_DISABLED = 0x555555;

    constexpr const char* NFF_ESP = "nwsFollowerFramework.esp";
    constexpr RE::FormID NFF_QUEST_LOCAL_ID = 0x4220F4;

    // NFF alias layout:
    //   0-9:   follower NPC slots
    //   10-19: corresponding container slots (container for slot N is at alias N+10)
    constexpr uint32_t FOLLOWER_SLOT_START = 0;
    constexpr uint32_t FOLLOWER_SLOT_END = 9;
    constexpr uint32_t CONTAINER_OFFSET = 10;
}

/**
 * NFFContainerSource - Provides Nether's Follower Framework storage containers
 *
 * Group 1 entries. NFF assigns each recruited follower a storage container via
 * quest aliases: follower in alias N, container in alias N+10.
 * Aliases are dynamic — NFF shuffles them at runtime — so we never cache slot
 * assignments and iterate fresh on every call.
 */
class NFFContainerSource : public IContainerSource {
    RE::TESQuest* quest_ = nullptr;

public:
    NFFContainerSource() {
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) return;

        quest_ = dh->LookupForm<RE::TESQuest>(NFF_QUEST_LOCAL_ID, NFF_ESP);
        if (quest_) {
            logger::info("NFFContainerSource: found quest {:08X}", quest_->GetFormID());
        } else {
            logger::info("NFFContainerSource: {} not installed, source inactive", NFF_ESP);
        }
    }

    const char* GetSourceID() const override { return "nff"; }

    int GetPriority() const override { return 5; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        if (a_formID == 0 || !quest_) return false;

        // Claim if the FormID is in any NFF container alias (10-19).
        // Don't check the parallel follower slot here — that's Resolve's job
        // (availability). During a shuffle NFF may temporarily clear follower
        // slots while the container alias still holds the REFR.
        for (uint32_t i = FOLLOWER_SLOT_START; i <= FOLLOWER_SLOT_END; ++i) {
            uint32_t containerAlias = i + CONTAINER_OFFSET;
            auto it = quest_->refAliasMap.find(containerAlias);
            if (it != quest_->refAliasMap.end()) {
                auto refPtr = it->second.get();
                if (refPtr && refPtr.get() && refPtr.get()->GetFormID() == a_formID) {
                    return true;
                }
            }
        }
        return false;
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        if (!quest_) {
            return ContainerDisplay{
                .name = "NFF Container",
                .location = "",
                .color = COLOR_DISABLED,
                .available = false,
                .group = 1
            };
        }

        // Find which container slot this is, and get the follower name
        for (uint32_t i = FOLLOWER_SLOT_START; i <= FOLLOWER_SLOT_END; ++i) {
            uint32_t containerAlias = i + CONTAINER_OFFSET;
            auto contIt = quest_->refAliasMap.find(containerAlias);
            if (contIt == quest_->refAliasMap.end()) continue;

            auto contPtr = contIt->second.get();
            if (!contPtr || !contPtr.get() || contPtr.get()->GetFormID() != a_formID) continue;

            // Found the container — get follower name from the parallel slot
            std::string followerName;
            bool followerPresent = false;
            auto followerIt = quest_->refAliasMap.find(i);
            if (followerIt != quest_->refAliasMap.end()) {
                auto followerPtr = followerIt->second.get();
                if (followerPtr && followerPtr.get()) {
                    followerPresent = true;
                    auto* followerRef = followerPtr.get();
                    if (followerRef->GetName() && followerRef->GetName()[0] != '\0') {
                        followerName = followerRef->GetName();
                    }
                }
            }

            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
            bool available = (ref != nullptr) && followerPresent;

            std::string name;
            if (!followerName.empty()) {
                name = followerName + " (Additional Inventory)";
            } else {
                name = "NFF Container";
            }

            logger::debug("NFF::Resolve: {:08X} -> '{}' follower='{}' available={}", a_formID, name, followerName, available);
            return ContainerDisplay{
                .name = name,
                .location = "",
                .color = available ? COLOR_FOLLOWER : COLOR_DISABLED,
                .available = available,
                .group = 1
            };
        }

        // Fallback — container not found in any alias
        logger::debug("NFF::Resolve: {:08X} not found in any alias slot", a_formID);
        return ContainerDisplay{
            .name = "NFF Container",
            .location = "",
            .color = COLOR_DISABLED,
            .available = false,
            .group = 1
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;
        if (!quest_) return entries;

        logger::debug("NFF::GetPickerEntries: scanning aliases (master={:08X})", a_masterFormID);

        for (uint32_t i = FOLLOWER_SLOT_START; i <= FOLLOWER_SLOT_END; ++i) {
            // Check if follower slot is populated
            auto followerIt = quest_->refAliasMap.find(i);
            if (followerIt == quest_->refAliasMap.end()) continue;

            auto followerPtr = followerIt->second.get();
            if (!followerPtr || !followerPtr.get()) {
                logger::debug("NFF::GetPickerEntries: alias {} has stale handle, skipping", i);
                continue;
            }

            auto* followerRef = followerPtr.get();
            std::string followerName;
            if (followerRef->GetName() && followerRef->GetName()[0] != '\0') {
                followerName = followerRef->GetName();
            }

            // Get the container from the parallel slot
            uint32_t containerAlias = i + CONTAINER_OFFSET;
            auto contIt = quest_->refAliasMap.find(containerAlias);
            if (contIt == quest_->refAliasMap.end()) {
                logger::debug("NFF::GetPickerEntries: follower '{}' in alias {} but no container in alias {}", followerName, i, containerAlias);
                continue;
            }

            auto contPtr = contIt->second.get();
            if (!contPtr || !contPtr.get()) {
                logger::debug("NFF::GetPickerEntries: container alias {} has stale handle for '{}'", containerAlias, followerName);
                continue;
            }

            RE::FormID containerFormID = contPtr.get()->GetFormID();

            // Skip if this is the master container
            if (containerFormID == a_masterFormID) {
                logger::debug("NFF::GetPickerEntries: skipping '{}' container {:08X} (is master)", followerName, containerFormID);
                continue;
            }

            std::string name;
            if (!followerName.empty()) {
                name = followerName + " (Additional Inventory)";
            } else {
                name = "NFF Container";
            }

            logger::debug("NFF::GetPickerEntries: adding '{}' container {:08X}", followerName, containerFormID);
            entries.push_back(PickerEntry{
                .name = name,
                .location = "",
                .formID = containerFormID,
                .isTagged = false,
                .color = COLOR_FOLLOWER,
                .group = 1,
                .enabled = true
            });
        }

        logger::debug("NFF::GetPickerEntries: returning {} entries", entries.size());
        return entries;
    }
};

// Registration function called from main.cpp
void RegisterNFFContainerSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<NFFContainerSource>()
    );
}
