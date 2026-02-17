#include "TraitEvaluator.h"
#include "Settings.h"
#include "Distributor.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace TraitEvaluator {

    // -----------------------------------------------------------------------
    // Keyword cache — resolved at Init(), used by keyword: traits
    // -----------------------------------------------------------------------

    static std::unordered_map<std::string, RE::BGSKeyword*> s_keywordCache;

    // -----------------------------------------------------------------------
    // FormType string→enum map
    // -----------------------------------------------------------------------

    static const std::unordered_map<std::string, RE::FormType> s_formTypeMap = {
        {"Weapon",      RE::FormType::Weapon},
        {"Armor",       RE::FormType::Armor},
        {"Book",        RE::FormType::Book},
        {"Scroll",      RE::FormType::Scroll},
        {"AlchemyItem", RE::FormType::AlchemyItem},
        {"Ingredient",  RE::FormType::Ingredient},
        {"Ammo",        RE::FormType::Ammo},
        {"KeyMaster",   RE::FormType::KeyMaster},
        {"SoulGem",     RE::FormType::SoulGem},
        {"Misc",        RE::FormType::Misc},
        {"Light",       RE::FormType::Light},
    };

    // -----------------------------------------------------------------------
    // Weapon type string→enum map
    // -----------------------------------------------------------------------

    static const std::unordered_map<std::string, RE::WEAPON_TYPE> s_weaponTypeMap = {
        {"Bow",           RE::WEAPON_TYPE::kBow},
        {"Crossbow",      RE::WEAPON_TYPE::kCrossbow},
        {"OneHandSword",  RE::WEAPON_TYPE::kOneHandSword},
        {"OneHandDagger", RE::WEAPON_TYPE::kOneHandDagger},
        {"OneHandAxe",    RE::WEAPON_TYPE::kOneHandAxe},
        {"OneHandMace",   RE::WEAPON_TYPE::kOneHandMace},
        {"TwoHandSword",  RE::WEAPON_TYPE::kTwoHandSword},
        {"TwoHandAxe",    RE::WEAPON_TYPE::kTwoHandAxe},
        {"Staff",         RE::WEAPON_TYPE::kStaff},
    };

    // -----------------------------------------------------------------------
    // Slot string→BipedObjectSlot map
    // -----------------------------------------------------------------------

    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;

    static const std::unordered_map<std::string, Slot> s_slotMap = {
        {"ring",    Slot::kRing},
        {"amulet",  Slot::kAmulet},
        {"circlet", Slot::kCirclet},
    };

    // -----------------------------------------------------------------------
    // FormList cache — resolved lazily, used by formlist: traits
    // -----------------------------------------------------------------------

    struct FormListEntry {
        RE::BGSListForm* list = nullptr;
        bool resolved = false;
    };

    static std::unordered_map<std::string, FormListEntry> s_formListCache;

    // -----------------------------------------------------------------------
    // Handler: formlist:EditorID@Plugin.esp
    // -----------------------------------------------------------------------

    static bool EvalFormList(const std::string& suffix, RE::TESBoundObject* item) {
        // Check cache first
        auto it = s_formListCache.find(suffix);
        if (it != s_formListCache.end()) {
            return it->second.list && it->second.list->HasForm(item);
        }

        // Parse EditorID@PluginName
        auto atPos = suffix.find('@');
        if (atPos == std::string::npos || atPos == 0 || atPos == suffix.size() - 1) {
            logger::warn("TraitEvaluator: formlist trait '{}' must use format EditorID@Plugin.esp", suffix);
            s_formListCache[suffix] = {nullptr, true};
            return false;
        }

        auto editorID  = suffix.substr(0, atPos);
        auto pluginName = suffix.substr(atPos + 1);

        // Check if plugin is loaded
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh || !dh->LookupModByName(pluginName)) {
            logger::debug("TraitEvaluator: formlist plugin '{}' not loaded, caching as null", pluginName);
            s_formListCache[suffix] = {nullptr, true};
            return false;
        }

        // Resolve FormList by EditorID
        auto* list = RE::TESForm::LookupByEditorID<RE::BGSListForm>(editorID);
        if (!list) {
            logger::warn("TraitEvaluator: formlist EditorID '{}' not found in plugin '{}'", editorID, pluginName);
        } else {
            logger::info("TraitEvaluator: cached formlist '{}' ({} forms)", suffix, list->forms.size());
        }
        s_formListCache[suffix] = {list, true};
        return list && list->HasForm(item);
    }

    // -----------------------------------------------------------------------
    // Handler: formtype:X
    // -----------------------------------------------------------------------

    static bool EvalFormType(const std::string& suffix, RE::TESBoundObject* item) {
        auto it = s_formTypeMap.find(suffix);
        if (it == s_formTypeMap.end()) {
            logger::warn("TraitEvaluator: unknown formtype '{}'", suffix);
            return false;
        }
        return item->GetFormType() == it->second;
    }

    // -----------------------------------------------------------------------
    // Handler: keyword:EditorID
    // -----------------------------------------------------------------------

    static bool EvalKeyword(const std::string& suffix, RE::TESBoundObject* item) {
        auto* kwForm = item->As<RE::BGSKeywordForm>();
        if (!kwForm) return false;

        // Check cache
        auto it = s_keywordCache.find(suffix);
        if (it != s_keywordCache.end()) {
            return it->second && kwForm->HasKeyword(it->second);
        }

        // Try runtime EditorID lookup and cache
        auto* kw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(suffix);
        s_keywordCache[suffix] = kw;
        return kw && kwForm->HasKeyword(kw);
    }

    // -----------------------------------------------------------------------
    // Handler: weapon_type:X
    // -----------------------------------------------------------------------

    static bool EvalWeaponType(const std::string& suffix, RE::TESBoundObject* item) {
        if (item->GetFormType() != RE::FormType::Weapon) return false;
        auto* weapon = item->As<RE::TESObjectWEAP>();
        if (!weapon) return false;
        auto it = s_weaponTypeMap.find(suffix);
        if (it == s_weaponTypeMap.end()) {
            logger::warn("TraitEvaluator: unknown weapon_type '{}'", suffix);
            return false;
        }
        return weapon->GetWeaponType() == it->second;
    }

    // -----------------------------------------------------------------------
    // Handler: slot:X (ring, amulet, circlet, shield)
    // -----------------------------------------------------------------------

    static bool EvalSlot(const std::string& suffix, RE::TESBoundObject* item) {
        if (item->GetFormType() != RE::FormType::Armor) return false;
        auto* armor = item->As<RE::TESObjectARMO>();
        if (!armor) return false;

        if (suffix == "shield") {
            return armor->IsShield();
        }

        auto it = s_slotMap.find(suffix);
        if (it == s_slotMap.end()) {
            logger::warn("TraitEvaluator: unknown slot '{}'", suffix);
            return false;
        }
        return armor->GetSlotMask() == it->second;
    }

    // -----------------------------------------------------------------------
    // Handler: armor_weight:X (light, heavy, clothing)
    // -----------------------------------------------------------------------

    static bool EvalArmorWeight(const std::string& suffix, RE::TESBoundObject* item) {
        if (item->GetFormType() != RE::FormType::Armor) return false;
        auto* armor = item->As<RE::TESObjectARMO>();
        if (!armor) return false;

        if (suffix == "light")    return armor->IsLightArmor();
        if (suffix == "heavy")    return armor->IsHeavyArmor();
        if (suffix == "clothing") return armor->IsClothing();

        logger::warn("TraitEvaluator: unknown armor_weight '{}'", suffix);
        return false;
    }

    // -----------------------------------------------------------------------
    // Handler: cobj_output:X / cobj_input:X
    // -----------------------------------------------------------------------

    static bool EvalCOBJOutput(const std::string& suffix, RE::TESBoundObject* item) {
        auto& cobj = Distributor::GetCOBJSets();
        if (suffix == "CraftingCookpot")     return cobj.cookedFood.count(item->GetFormID()) > 0;
        if (suffix == "CraftingTanningRack") return cobj.tanningOutputs.count(item->GetFormID()) > 0;
        logger::warn("TraitEvaluator: unknown cobj_output bench '{}'", suffix);
        return false;
    }

    static bool EvalCOBJInput(const std::string& suffix, RE::TESBoundObject* item) {
        auto& cobj = Distributor::GetCOBJSets();
        if (suffix == "CraftingSmelter")    return cobj.smeltableInputs.count(item->GetFormID()) > 0;
        if (suffix == "BYOHCarpenterTable") return cobj.hearthfireMats.count(item->GetFormID()) > 0;
        logger::warn("TraitEvaluator: unknown cobj_input bench '{}'", suffix);
        return false;
    }

    // -----------------------------------------------------------------------
    // Handler: in_pool:X / in_group:X
    // -----------------------------------------------------------------------

    static bool EvalInPool(const std::string& suffix, RE::TESBoundObject* item) {
        if (suffix == "unique_items") {
            return Settings::uniqueItemFormIDs.count(item->GetFormID()) > 0;
        }
        logger::warn("TraitEvaluator: unknown pool '{}'", suffix);
        return false;
    }

    static bool EvalInGroup(const std::string& suffix, RE::TESBoundObject* item) {
        auto it = Settings::uniqueItemGroups.find(suffix);
        if (it != Settings::uniqueItemGroups.end()) {
            return it->second.count(item->GetFormID()) > 0;
        }
        return false;  // group not loaded = not matching, not an error
    }

    // -----------------------------------------------------------------------
    // Engine method traits (no prefix — whole name is the trait)
    // -----------------------------------------------------------------------

    static bool EvalIsFood(RE::TESBoundObject* item) {
        auto* alch = item->As<RE::AlchemyItem>();
        return alch && alch->IsFood();
    }

    static bool EvalIsPoison(RE::TESBoundObject* item) {
        auto* alch = item->As<RE::AlchemyItem>();
        return alch && alch->IsPoison();
    }

    static bool EvalHasEnchantment(RE::TESBoundObject* item) {
        auto* ench = item->As<RE::TESEnchantableForm>();
        return ench && ench->formEnchanting;
    }

    static bool EvalHasSpell(RE::TESBoundObject* item) {
        auto* book = item->As<RE::TESObjectBOOK>();
        return book && book->GetSpell() != nullptr;
    }

    static bool EvalTeachesSkill(RE::TESBoundObject* item) {
        auto* book = item->As<RE::TESObjectBOOK>();
        return book && book->TeachesSkill();
    }

    static bool EvalIsNote(RE::TESBoundObject* item) {
        auto* book = item->As<RE::TESObjectBOOK>();
        return book && book->IsNoteScroll();
    }

    static bool EvalUnreadBook(RE::TESBoundObject* item) {
        auto* book = item->As<RE::TESObjectBOOK>();
        return book && !book->IsRead();
    }

    static bool EvalUnlearnedSpell(RE::TESBoundObject* item) {
        auto* book = item->As<RE::TESObjectBOOK>();
        if (!book) return false;
        auto* spell = book->GetSpell();
        if (!spell) return false;
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player && !player->HasSpell(spell);
    }

    static bool EvalUnknownEnchantment(RE::TESBoundObject* item) {
        auto* enchantable = item->As<RE::TESEnchantableForm>();
        if (!enchantable || !enchantable->formEnchanting) return false;

        // MagicDisallowEnchanting keyword = item cannot be disenchanted
        auto* kwForm = item->As<RE::BGSKeywordForm>();
        if (kwForm) {
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (dh) {
                auto* kw = dh->LookupForm<RE::BGSKeyword>(Settings::uMagicDisallowEnchanting, Settings::sKeywordPlugin);
                if (kw && kwForm->HasKeyword(kw)) return false;
            }
        }

        auto* ench = enchantable->formEnchanting;
        // No baseEnchantment = unique/staff enchantment, not learnable
        if (!ench->data.baseEnchantment) return false;
        // Walk to root enchantment — kKnown is set on the base form
        while (ench->data.baseEnchantment) {
            ench = ench->data.baseEnchantment;
        }
        return (ench->GetFormFlags() &
                static_cast<uint32_t>(RE::TESForm::RecordFlags::kKnown)) == 0;
    }

    // -----------------------------------------------------------------------
    // Dispatch table
    // -----------------------------------------------------------------------

    using PrefixHandler = std::function<bool(const std::string& suffix, RE::TESBoundObject* item)>;
    using WholeHandler  = std::function<bool(RE::TESBoundObject* item)>;

    static std::unordered_map<std::string, PrefixHandler> s_prefixHandlers;
    static std::unordered_map<std::string, WholeHandler>  s_wholeHandlers;
    static std::unordered_set<std::string> s_warnedUnknown;

    // -----------------------------------------------------------------------
    // Init
    // -----------------------------------------------------------------------

    void Init() {
        // Pre-resolve known vanilla keywords (VR safety — EditorID lookup broken on VR)
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (dh) {
            auto resolve = [&](const char* editorID, uint32_t formID) {
                auto* kw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID);
                if (!kw) {
                    kw = dh->LookupForm<RE::BGSKeyword>(formID, Settings::sKeywordPlugin);
                }
                if (kw) {
                    s_keywordCache[editorID] = kw;
                }
            };
            resolve("VendorItemAnimalHide", Settings::uVendorItemAnimalHide);
            resolve("VendorItemAnimalPart", Settings::uVendorItemAnimalPart);
            resolve("VendorItemOreIngot",   Settings::uVendorItemOreIngot);
            resolve("VendorItemGem",        Settings::uVendorItemGem);
            resolve("VendorItemKey",        Settings::uVendorItemKey);
            resolve("MagicDisallowEnchanting", Settings::uMagicDisallowEnchanting);
        }

        // Build prefix dispatch table
        s_prefixHandlers["formtype"]     = EvalFormType;
        s_prefixHandlers["keyword"]      = EvalKeyword;
        s_prefixHandlers["weapon_type"]  = EvalWeaponType;
        s_prefixHandlers["slot"]         = EvalSlot;
        s_prefixHandlers["armor_weight"] = EvalArmorWeight;
        s_prefixHandlers["cobj_output"]  = EvalCOBJOutput;
        s_prefixHandlers["cobj_input"]   = EvalCOBJInput;
        s_prefixHandlers["in_pool"]      = EvalInPool;
        s_prefixHandlers["in_group"]     = EvalInGroup;
        s_prefixHandlers["formlist"]     = EvalFormList;

        // Build whole-name dispatch table (engine method traits)
        s_wholeHandlers["is_food"]               = EvalIsFood;
        s_wholeHandlers["is_poison"]             = EvalIsPoison;
        s_wholeHandlers["has_enchantment"]       = EvalHasEnchantment;
        s_wholeHandlers["has_spell"]             = EvalHasSpell;
        s_wholeHandlers["teaches_skill"]         = EvalTeachesSkill;
        s_wholeHandlers["is_note"]               = EvalIsNote;
        s_wholeHandlers["unknown_enchantment"]   = EvalUnknownEnchantment;
        s_wholeHandlers["unread_book"]            = EvalUnreadBook;
        s_wholeHandlers["unlearned_spell"]        = EvalUnlearnedSpell;

        logger::info("TraitEvaluator: initialized ({} keywords cached, {} prefix handlers, {} engine traits)",
            s_keywordCache.size(), s_prefixHandlers.size(), s_wholeHandlers.size());
    }

    // -----------------------------------------------------------------------
    // Evaluate
    // -----------------------------------------------------------------------

    bool Evaluate(const std::string& traitName, RE::TESBoundObject* item) {
        if (!item) return false;

        // Check whole-name handlers first (engine methods)
        auto wholeIt = s_wholeHandlers.find(traitName);
        if (wholeIt != s_wholeHandlers.end()) {
            return wholeIt->second(item);
        }

        // Parse prefix:suffix
        auto colonPos = traitName.find(':');
        if (colonPos != std::string::npos) {
            auto prefix = traitName.substr(0, colonPos);
            auto suffix = traitName.substr(colonPos + 1);

            auto prefixIt = s_prefixHandlers.find(prefix);
            if (prefixIt != s_prefixHandlers.end()) {
                return prefixIt->second(suffix, item);
            }
        }

        // Unknown trait — warn once
        if (s_warnedUnknown.insert(traitName).second) {
            logger::warn("TraitEvaluator: unknown trait '{}'", traitName);
        }
        return false;
    }

}  // namespace TraitEvaluator
