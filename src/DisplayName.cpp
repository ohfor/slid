#include "DisplayName.h"
#include "NetworkManager.h"

namespace DisplayName {

    /// Get the base form name for a container reference (fallback when no tag exists).
    static std::string GetBaseName(RE::TESObjectREFR* a_ref) {
        if (auto* base = a_ref->GetBaseObject()) {
            const char* name = base->GetName();
            if (name && name[0]) return name;
        }
        return "";
    }

    /// Get the display-ready name for a container: tag name if tagged, else base form name.
    static std::string GetContainerLabel(RE::FormID a_formID, RE::TESObjectREFR* a_ref) {
        auto* mgr = NetworkManager::GetSingleton();
        auto tagName = mgr->GetTagName(a_formID);
        if (!tagName.empty()) return tagName;
        return GetBaseName(a_ref);
    }

    void Apply(RE::FormID a_formID) {
        if (a_formID == 0) return;

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        if (!ref) return;

        auto* mgr = NetworkManager::GetSingleton();

        // Priority 1: Master container → "NetworkName: label"
        auto networkName = mgr->FindNetworkByMaster(a_formID);
        if (!networkName.empty()) {
            auto label = GetContainerLabel(a_formID, ref);
            std::string displayName = networkName + ": " + label;
            ref->SetDisplayName(displayName.c_str(), true);
            logger::debug("DisplayName::Apply: {:08X} -> \"{}\" (master)", a_formID, displayName);
            return;
        }

        // Priority 2: Sell container → "Sell: label"
        if (a_formID == mgr->GetSellContainerFormID()) {
            auto label = GetContainerLabel(a_formID, ref);
            std::string displayName = "Sell: " + label;
            ref->SetDisplayName(displayName.c_str(), true);
            logger::debug("DisplayName::Apply: {:08X} -> \"{}\" (sell)", a_formID, displayName);
            return;
        }

        // Priority 3: Tagged container → tag name as-is
        if (mgr->IsTagged(a_formID)) {
            auto tagName = mgr->GetTagName(a_formID);
            if (!tagName.empty()) {
                ref->SetDisplayName(tagName.c_str(), true);
                logger::debug("DisplayName::Apply: {:08X} -> \"{}\" (tagged)", a_formID, tagName);
                return;
            }
        }

        // No role — do nothing
    }

    void Clear(RE::FormID a_formID) {
        if (a_formID == 0) return;

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        if (!ref) return;

        // Remove ExtraTextDisplayData to restore engine default name
        ref->extraList.RemoveByType(RE::ExtraDataType::kTextDisplayData);
        logger::debug("DisplayName::Clear: {:08X} restored to base name", a_formID);
    }

    void ApplyAll() {
        auto* mgr = NetworkManager::GetSingleton();

        // Collect all FormIDs that need display names, then apply.
        // Using a set avoids duplicate Apply calls for containers with multiple roles.
        std::unordered_set<RE::FormID> formIDs;

        // Masters
        for (const auto& net : mgr->GetNetworks()) {
            if (net.masterFormID != 0) {
                formIDs.insert(net.masterFormID);
            }
        }

        // Sell container
        auto sellID = mgr->GetSellContainerFormID();
        if (sellID != 0) {
            formIDs.insert(sellID);
        }

        // Tagged containers
        for (const auto& [fid, tag] : mgr->GetTagRegistry()) {
            formIDs.insert(fid);
        }

        for (auto fid : formIDs) {
            Apply(fid);
        }

        logger::info("DisplayName::ApplyAll: applied to {} containers", formIDs.size());
    }

}
