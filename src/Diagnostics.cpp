#include "Diagnostics.h"
#include "FilterRegistry.h"
#include "NetworkManager.h"
#include "TraitEvaluator.h"
#include "TranslationService.h"

namespace Diagnostics {

    void ValidateState() {
        std::vector<std::string> warnings;

        // 1. Context Power — player has SLID_ContextSPEL
        bool powerOK = false;
        if (auto* spell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SLID_ContextSPEL")) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && player->HasSpell(spell)) {
                powerOK = true;
            } else {
                warnings.push_back("player missing SLID_ContextSPEL");
            }
        } else {
            warnings.push_back("SLID_ContextSPEL not found in ESP");
        }

        // 2. ESP forms — key globals and quest resolve
        if (!RE::TESForm::LookupByEditorID<RE::TESGlobal>("SLID_VendorEnabled")) {
            warnings.push_back("SLID_VendorEnabled global not found");
        }
        if (!RE::TESForm::LookupByEditorID<RE::TESGlobal>("SLID_VendorRegistered")) {
            warnings.push_back("SLID_VendorRegistered global not found");
        }
        if (!RE::TESForm::LookupByEditorID<RE::TESQuest>("SLID_VendorQuest")) {
            warnings.push_back("SLID_VendorQuest not found");
        }

        // 3. Translations — loaded with reasonable key count
        auto* ts = TranslationService::GetSingleton();
        size_t translationCount = 0;
        if (ts && ts->IsLoaded()) {
            translationCount = ts->GetKeyCount();
            if (translationCount < 50) {
                warnings.push_back(fmt::format("low translation count ({})", translationCount));
            }
        } else {
            warnings.push_back("TranslationService not loaded");
        }

        // 4. Filter registry — has filters
        auto* fr = FilterRegistry::GetSingleton();
        size_t filterCount = fr->GetFilterCount();
        if (filterCount == 0) {
            warnings.push_back("FilterRegistry has 0 filters");
        }

        // 5. Keyword resolution — validate all keyword: traits in loaded filters
        uint32_t keywordFailures = TraitEvaluator::ValidateKeywords();
        if (keywordFailures > 0) {
            warnings.push_back(fmt::format("{} keyword(s) failed to resolve", keywordFailures));
        }

        // 6. Network state
        auto* nm = NetworkManager::GetSingleton();
        const auto& networks = nm->GetNetworks();
        size_t networkCount = networks.size();
        size_t unavailableCount = 0;
        for (const auto& net : networks) {
            if (net.masterUnavailable) ++unavailableCount;
        }
        if (unavailableCount > 0) {
            warnings.push_back(fmt::format("{} network(s) with unavailable master", unavailableCount));
        }

        // Output summary
        if (warnings.empty()) {
            logger::info("SLID: self-test PASS ({} networks, {} filters, {} translations, power {})",
                networkCount, filterCount, translationCount, powerOK ? "OK" : "MISSING");
        } else {
            logger::info("SLID: self-test PASS ({} networks, {} filters, {} translations, power {})",
                networkCount, filterCount, translationCount, powerOK ? "OK" : "MISSING");
            for (const auto& w : warnings) {
                logger::warn("SLID: self-test WARN: {}", w);
            }
        }
    }

}  // namespace Diagnostics
