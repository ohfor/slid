#include "ContextResolver.h"
#include "NetworkManager.h"
#include "VendorRegistry.h"

namespace ContextResolver {

    namespace {

        /// Find which network a container belongs to (as filter-assigned or catch-all).
        /// Returns the network name, or empty string if not found.
        /// Does NOT check master — that's handled separately.
        std::string FindNetworkForContainer(RE::FormID a_formID) {
            auto* mgr = NetworkManager::GetSingleton();
            const auto& networks = mgr->GetNetworks();
            for (const auto& net : networks) {
                for (const auto& stage : net.filters) {
                    if (stage.containerFormID == a_formID) {
                        return net.name;
                    }
                }
                if (net.catchAllFormID == a_formID) {
                    return net.name;
                }
            }
            return "";
        }

        std::vector<ActionEntry> BuildMasterActions() {
            return {
                {Action::kOpen,      "$SLID_CtxOpen",      "$SLID_CtxOpenDesc"},
                {Action::kWhoosh,    "$SLID_CtxWhoosh",    "$SLID_CtxWhooshDesc"},
                {Action::kRestock,   "$SLID_CtxRestock",   "$SLID_CtxRestockDesc"},
                {Action::kWhooshAndRestock, "$SLID_CtxWhooshRestock", "$SLID_CtxWhooshRestockDesc"},
                {Action::kSort,      "$SLID_CtxSort",      "$SLID_CtxSortDesc"},
                {Action::kSweep,     "$SLID_CtxSweep",     "$SLID_CtxSweepDesc"},
                {Action::kConfigure, "$SLID_CtxConfigure", "$SLID_CtxConfigureDesc"},
                {Action::kDestroyLink, "$SLID_CtxDestroyLink", "$SLID_CtxDestroyLinkDesc"},
            };
        }

        std::vector<ActionEntry> BuildSellActions() {
            return {
                {Action::kSummary, "$SLID_CtxSummary", "$SLID_CtxSummaryDesc"},
                {Action::kRename,  "$SLID_CtxRename",  "$SLID_CtxRenameDesc"},
                {Action::kRemove,  "$SLID_CtxRemove",  "$SLID_CtxRemoveDesc"},
            };
        }

        std::vector<ActionEntry> BuildKnownActions() {
            return {
                {Action::kRename, "$SLID_CtxRename", "$SLID_CtxRenameDesc"},
                {Action::kRemove, "$SLID_CtxRemove", "$SLID_CtxRemoveDesc"},
            };
        }

        std::vector<ActionEntry> BuildContainerActions(bool a_hasNetworks, bool a_hasSell) {
            std::vector<ActionEntry> actions;

            if (a_hasNetworks) {
                actions.push_back({Action::kAddToLink, "$SLID_CtxAddToLink", "$SLID_CtxAddToLinkDesc"});
            }

            actions.push_back({Action::kCreateLink, "$SLID_CtxCreateLink", "$SLID_CtxCreateLinkDesc"});

            if (!a_hasSell) {
                actions.push_back({Action::kSetAsSell, "$SLID_CtxSetAsSell", "$SLID_CtxSetAsSellDesc"});
            }

            return actions;
        }

        std::vector<ActionEntry> BuildAirActions() {
            return {
                {Action::kOpen,      "$SLID_CtxOpen",      "$SLID_CtxOpenDesc"},
                {Action::kWhoosh,    "$SLID_CtxWhoosh",    "$SLID_CtxWhooshDesc"},
                {Action::kRestock,   "$SLID_CtxRestock",   "$SLID_CtxRestockDesc"},
                {Action::kWhooshAndRestock, "$SLID_CtxWhooshRestock", "$SLID_CtxWhooshRestockDesc"},
                {Action::kSort,      "$SLID_CtxSort",      "$SLID_CtxSortDesc"},
                {Action::kSweep,     "$SLID_CtxSweep",     "$SLID_CtxSweepDesc"},
                {Action::kConfigure, "$SLID_CtxConfigure", "$SLID_CtxConfigureDesc"},
                {Action::kDetect,    "$SLID_CtxDetect",    "$SLID_CtxDetectDesc"},
            };
        }
    }

    ResolvedContext Resolve(RE::FormID a_target) {
        auto* mgr = NetworkManager::GetSingleton();
        ResolvedContext result;
        result.targetFormID = a_target;

        logger::debug("ContextResolver: target={:08X}", a_target);

        // --- No target: air context ---
        if (a_target == 0) {
            auto names = mgr->GetNetworkNames();
            if (names.empty()) {
                logger::debug("ContextResolver: -> kAirEmpty (no target, no networks)");
                result.context = Context::kAirEmpty;
                return result;
            }
            result.context = Context::kAir;
            result.cyclableNetworks = names;
            result.networkName = names[0];
            result.actions = BuildAirActions();
            logger::debug("ContextResolver: -> kAir ({} networks)", names.size());
            return result;
        }

        // --- Actor: vendor context (always, even if not whitelisted) ---
        auto* form = RE::TESForm::LookupByID(a_target);
        if (!form) {
            auto names = mgr->GetNetworkNames();
            if (names.empty()) {
                result.context = Context::kAirEmpty;
            } else {
                result.context = Context::kAir;
                result.cyclableNetworks = names;
                result.networkName = names[0];
                result.actions = BuildAirActions();
            }
            logger::debug("ContextResolver: -> air (form not found)");
            return result;
        }

        auto* ref = form->As<RE::TESObjectREFR>();
        if (ref && ref->As<RE::Actor>()) {
            logger::debug("ContextResolver: -> kVendor");
            result.context = Context::kVendor;
            return result;
        }

        // --- Not a container: treat as air ---
        if (!ref || !ref->GetContainer()) {
            auto names = mgr->GetNetworkNames();
            if (names.empty()) {
                result.context = Context::kAirEmpty;
            } else {
                result.context = Context::kAir;
                result.cyclableNetworks = names;
                result.networkName = names[0];
                result.actions = BuildAirActions();
            }
            logger::debug("ContextResolver: -> air (not a container, ref={}, getContainer={})",
                          ref != nullptr, ref ? (ref->GetContainer() != nullptr) : false);
            return result;
        }

        // --- Container: determine role ---
        auto formID = ref->GetFormID();
        logger::debug("ContextResolver: container {:08X}, sell={:08X}, tagged={}, networks={}",
                      formID, mgr->GetSellContainerFormID(),
                      mgr->IsTagged(formID), mgr->GetNetworkNames().size());

        // Sell container (checked first — sell wins over tagged)
        if (formID == mgr->GetSellContainerFormID()) {
            logger::debug("ContextResolver: -> kSell");
            result.context = Context::kSell;
            result.actions = BuildSellActions();
            return result;
        }

        // Master container
        auto masterNet = mgr->FindNetworkByMaster(formID);
        if (!masterNet.empty()) {
            logger::debug("ContextResolver: -> kMaster ({})", masterNet);
            result.context = Context::kMaster;
            result.networkName = masterNet;
            result.actions = BuildMasterActions();
            return result;
        }

        // Tagged container
        if (mgr->IsTagged(formID)) {
            result.context = Context::kKnown;
            result.networkName = FindNetworkForContainer(formID);
            result.actions = BuildKnownActions();
            logger::debug("ContextResolver: -> kKnown (tagged, net='{}')", result.networkName);
            return result;
        }

        // Filter-assigned container (not tagged, but used in a network)
        auto assignedNet = FindNetworkForContainer(formID);
        if (!assignedNet.empty()) {
            result.context = Context::kKnown;
            result.networkName = assignedNet;
            result.actions = BuildKnownActions();
            logger::debug("ContextResolver: -> kKnown (filter-assigned, net='{}')", assignedNet);
            return result;
        }

        // Unknown container
        {
            auto names = mgr->GetNetworkNames();
            bool hasNetworks = !names.empty();
            bool hasSell = mgr->HasSellContainer();

            result.context = Context::kContainer;
            result.actions = BuildContainerActions(hasNetworks, hasSell);

            if (hasNetworks) {
                result.cyclableNetworks = names;
                result.networkName = names[0];
            }
            logger::debug("ContextResolver: -> kContainer (hasNetworks={}, hasSell={}, actions={})",
                          hasNetworks, hasSell, result.actions.size());
        }

        return result;
    }

}
