#include "RestockCategory.h"

#include <unordered_map>

namespace RestockCategory {

    // ---- Static category definitions ----

    // ActorValue ranges for classification.
    // Two AV systems exist: base skill AVs (6-32) and power-modifier AVs (123-163).
    // Vanilla uses base AVs; alchemy overhaul mods often use power-modifier AVs
    // for the same skills. Both must be recognized.
    namespace AV {
        // Skills — Combat (base)
        constexpr auto kOneHanded   = RE::ActorValue::kOneHanded;    // 6
        constexpr auto kTwoHanded   = RE::ActorValue::kTwoHanded;    // 7
        constexpr auto kArchery     = RE::ActorValue::kArchery;      // 8
        constexpr auto kBlock       = RE::ActorValue::kBlock;        // 9
        constexpr auto kHeavyArmor  = RE::ActorValue::kHeavyArmor;  // 11
        constexpr auto kLightArmor  = RE::ActorValue::kLightArmor;  // 12

        // Skills — Magic (base)
        constexpr auto kSmithing    = RE::ActorValue::kSmithing;     // 10
        constexpr auto kAlteration  = RE::ActorValue::kAlteration;   // 18
        constexpr auto kConjuration = RE::ActorValue::kConjuration;  // 19
        constexpr auto kDestruction = RE::ActorValue::kDestruction;  // 20
        constexpr auto kIllusion    = RE::ActorValue::kIllusion;     // 21
        constexpr auto kRestoration = RE::ActorValue::kRestoration;  // 22
        constexpr auto kEnchanting  = RE::ActorValue::kEnchanting;   // 23

        // Skills — Stealth (base)
        constexpr auto kSneak       = RE::ActorValue::kSneak;        // 13
        constexpr auto kLockpicking = RE::ActorValue::kLockpicking;  // 14
        constexpr auto kPickpocket  = RE::ActorValue::kPickpocket;   // 15
        constexpr auto kSpeech      = RE::ActorValue::kSpeech;       // 16
        constexpr auto kAlchemy     = RE::ActorValue::kAlchemy;      // 17

        // Attributes
        constexpr auto kHealth      = RE::ActorValue::kHealth;       // 24
        constexpr auto kMagicka     = RE::ActorValue::kMagicka;      // 25
        constexpr auto kStamina     = RE::ActorValue::kStamina;      // 26

        // Regen (base)
        constexpr auto kHealRate      = RE::ActorValue::kHealRate;       // 27
        constexpr auto kMagickaRate   = RE::ActorValue::kMagickaRate;    // 28
        constexpr auto kStaminaRate   = RE::ActorValue::kStaminaRate;    // 29

        // Misc
        constexpr auto kCarryWeight = RE::ActorValue::kCarryWeight;  // 30
        constexpr auto kSpeedMult   = RE::ActorValue::kSpeedMult;    // 32

        // Resist (39-45)
        constexpr auto kDamageResist  = RE::ActorValue::kDamageResist;  // 39
        constexpr auto kPoisonResist  = RE::ActorValue::kPoisonResist;  // 40
        constexpr auto kResistFire    = RE::ActorValue::kResistFire;    // 41
        constexpr auto kResistShock   = RE::ActorValue::kResistShock;   // 42
        constexpr auto kResistFrost   = RE::ActorValue::kResistFrost;   // 43
        constexpr auto kResistMagic   = RE::ActorValue::kResistMagic;   // 44
        constexpr auto kResistDisease = RE::ActorValue::kResistDisease; // 45

        // Utility
        constexpr auto kInvisibility    = RE::ActorValue::kInvisibility;     // 54
        constexpr auto kWaterBreathing  = RE::ActorValue::kWaterBreathing;   // 57
        constexpr auto kWaterWalking    = RE::ActorValue::kWaterWalking;     // 58

        // Power-modifier AVs (used by alchemy overhaul mods instead of base skill AVs)
        constexpr int kPowerUnarmed         = 60;
        constexpr int kPowerSneakAttacks    = 123;
        constexpr int kPowerShouts          = 125;
        constexpr int kPowerOneHanded       = 135;
        constexpr int kPowerTwoHanded       = 136;
        constexpr int kPowerMarksman        = 137;
        constexpr int kPowerBlock           = 138;
        constexpr int kPowerPickpocket      = 142;
        constexpr int kPowerLockpicking     = 143;
        constexpr int kPowerSneak           = 144;
        constexpr int kPowerBarter          = 146;
        constexpr int kPowerAlteration      = 147;
        constexpr int kPowerConjuration     = 148;
        constexpr int kPowerDestruction     = 149;
        constexpr int kPowerIllusion        = 150;
        constexpr int kPowerRestoration     = 151;
        constexpr int kPowerHealthRegen     = 155;
        constexpr int kPowerMagickaRegen    = 156;
        constexpr int kPowerStaminaRegen    = 157;
        constexpr int kPowerReflectDamage   = 163;
    }

    static bool IsResistAV(RE::ActorValue a_av) {
        // 40-45 only. DamageResist (39) excluded — it's Fortify Armor Rating, not elemental resist
        auto v = static_cast<int>(a_av);
        return v >= 40 && v <= 45;
    }

    static bool IsCombatSkillAV(RE::ActorValue a_av) {
        auto v = static_cast<int>(a_av);
        return a_av == AV::kOneHanded || a_av == AV::kTwoHanded ||
               a_av == AV::kArchery || a_av == AV::kBlock ||
               a_av == AV::kHeavyArmor || a_av == AV::kLightArmor ||
               v == AV::kPowerOneHanded || v == AV::kPowerTwoHanded ||
               v == AV::kPowerMarksman || v == AV::kPowerBlock ||
               v == AV::kPowerUnarmed;
    }

    static bool IsMagicSkillAV(RE::ActorValue a_av) {
        auto v = static_cast<int>(a_av);
        return a_av == AV::kAlteration || a_av == AV::kConjuration ||
               a_av == AV::kDestruction || a_av == AV::kIllusion ||
               a_av == AV::kRestoration || a_av == AV::kEnchanting ||
               v == AV::kPowerAlteration || v == AV::kPowerConjuration ||
               v == AV::kPowerDestruction || v == AV::kPowerIllusion ||
               v == AV::kPowerRestoration;
    }

    static bool IsStealthSkillAV(RE::ActorValue a_av) {
        auto v = static_cast<int>(a_av);
        return a_av == AV::kSneak || a_av == AV::kLockpicking ||
               a_av == AV::kPickpocket || a_av == AV::kSpeech ||
               a_av == AV::kAlchemy || a_av == AV::kSmithing ||
               v == AV::kPowerSneak || v == AV::kPowerLockpicking ||
               v == AV::kPowerPickpocket || v == AV::kPowerBarter ||
               v == AV::kPowerShouts || v == AV::kPowerSneakAttacks;
    }

    static bool IsAttributeAV(RE::ActorValue a_av) {
        return a_av == AV::kHealth || a_av == AV::kMagicka || a_av == AV::kStamina;
    }

    static bool IsRegenAV(RE::ActorValue a_av) {
        auto v = static_cast<int>(a_av);
        return a_av == AV::kHealRate || a_av == AV::kMagickaRate || a_av == AV::kStaminaRate ||
               v == AV::kPowerHealthRegen || v == AV::kPowerMagickaRegen || v == AV::kPowerStaminaRegen;
    }

    static bool IsMiscFortifyAV(RE::ActorValue a_av) {
        auto v = static_cast<int>(a_av);
        return a_av == AV::kCarryWeight || a_av == AV::kSpeedMult ||
               a_av == AV::kDamageResist || v == AV::kPowerReflectDamage;
    }

    // Build static category list
    static const std::vector<CategoryDef>& BuildCategories() {
        static const std::vector<CategoryDef> cats = {
            // -- Restore family --
            {"restore",          "",         "$SLID_RsCatRestore",          5, true},
            {"restore_health",   "restore",  "$SLID_RsCatRestoreHealth",    0, true},
            {"restore_magicka",  "restore",  "$SLID_RsCatRestoreMagicka",   0, true},
            {"restore_stamina",  "restore",  "$SLID_RsCatRestoreStamina",   0, true},
            {"restore_vampire",  "restore",  "$SLID_RsCatRestoreVampire",   0, false},
            {"restore_rare",     "restore",  "$SLID_RsCatRestoreRare",      0, false},

            // -- Resist family --
            {"resist",           "",         "$SLID_RsCatResist",           0, false},
            {"resist_fire",      "resist",   "$SLID_RsCatResistFire",       0, false},
            {"resist_frost",     "resist",   "$SLID_RsCatResistFrost",      0, false},
            {"resist_shock",     "resist",   "$SLID_RsCatResistShock",      0, false},
            {"resist_magic",     "resist",   "$SLID_RsCatResistMagic",      0, false},
            {"resist_disease",   "resist",   "$SLID_RsCatResistDisease",    0, false},
            {"resist_poison",    "resist",   "$SLID_RsCatResistPoison",     0, false},

            // -- Fortify: Combat --
            {"fortify_combat",           "",                "$SLID_RsCatFortifyCombat",     0, false},
            {"fortify_onehanded",        "fortify_combat",  "$SLID_RsCatFortifyOneHanded",  0, false},
            {"fortify_twohanded",        "fortify_combat",  "$SLID_RsCatFortifyTwoHanded",  0, false},
            {"fortify_archery",          "fortify_combat",  "$SLID_RsCatFortifyArchery",    0, false},
            {"fortify_block",            "fortify_combat",  "$SLID_RsCatFortifyBlock",      0, false},
            {"fortify_heavyarmor",       "fortify_combat",  "$SLID_RsCatFortifyHeavyArmor", 0, false},
            {"fortify_lightarmor",       "fortify_combat",  "$SLID_RsCatFortifyLightArmor", 0, false},

            // -- Fortify: Magic --
            {"fortify_magic",            "",                "$SLID_RsCatFortifyMagic",       0, false},
            {"fortify_alteration",       "fortify_magic",   "$SLID_RsCatFortifyAlteration",  0, false},
            {"fortify_conjuration",      "fortify_magic",   "$SLID_RsCatFortifyConjuration", 0, false},
            {"fortify_destruction",      "fortify_magic",   "$SLID_RsCatFortifyDestruction", 0, false},
            {"fortify_illusion",         "fortify_magic",   "$SLID_RsCatFortifyIllusion",    0, false},
            {"fortify_restoration",      "fortify_magic",   "$SLID_RsCatFortifyRestoration", 0, false},
            {"fortify_enchanting",       "fortify_magic",   "$SLID_RsCatFortifyEnchanting",  0, false},

            // -- Fortify: Stealth --
            {"fortify_stealth",          "",                "$SLID_RsCatFortifyStealth",     0, false},
            {"fortify_sneak",            "fortify_stealth", "$SLID_RsCatFortifySneak",       0, false},
            {"fortify_lockpicking",      "fortify_stealth", "$SLID_RsCatFortifyLockpicking", 0, false},
            {"fortify_pickpocket",       "fortify_stealth", "$SLID_RsCatFortifyPickpocket",  0, false},
            {"fortify_speech",           "fortify_stealth", "$SLID_RsCatFortifySpeech",      0, false},
            {"fortify_alchemy",          "fortify_stealth", "$SLID_RsCatFortifyAlchemy",     0, false},
            {"fortify_smithing",         "fortify_stealth", "$SLID_RsCatFortifySmithing",    0, false},

            // -- Fortify: Attributes --
            {"fortify_attributes",       "",                   "$SLID_RsCatFortifyAttributes",  0, false},
            {"fortify_health",           "fortify_attributes", "$SLID_RsCatFortifyHealth",      0, false},
            {"fortify_magicka",          "fortify_attributes", "$SLID_RsCatFortifyMagicka",     0, false},
            {"fortify_stamina",          "fortify_attributes", "$SLID_RsCatFortifyStamina",     0, false},

            // -- Fortify: Regen --
            {"fortify_regen",            "",              "$SLID_RsCatFortifyRegen",          0, false},
            {"fortify_healrate",         "fortify_regen", "$SLID_RsCatFortifyHealRate",       0, false},
            {"fortify_magickarate",      "fortify_regen", "$SLID_RsCatFortifyMagickaRate",    0, false},
            {"fortify_staminarate",      "fortify_regen", "$SLID_RsCatFortifyStaminaRate",    0, false},

            // -- Fortify: Misc --
            {"fortify_misc",             "",              "$SLID_RsCatFortifyMisc",           0, false},
            {"fortify_carryweight",      "fortify_misc",  "$SLID_RsCatFortifyCarryWeight",    0, false},
            {"fortify_speed",            "fortify_misc",  "$SLID_RsCatFortifySpeed",          0, false},
            {"fortify_armorrating",      "fortify_misc",  "$SLID_RsCatFortifyArmorRating",    0, false},

            // -- Utility family --
            {"utility",                  "",         "$SLID_RsCatUtility",              0, false},
            {"utility_invisibility",     "utility",  "$SLID_RsCatUtilityInvisibility",  0, false},
            {"utility_waterbreathing",   "utility",  "$SLID_RsCatUtilityWaterbreath",   0, false},
            {"utility_waterwalking",     "utility",  "$SLID_RsCatUtilityWaterwalk",     0, false},
            {"utility_ethereal",         "utility",  "$SLID_RsCatUtilityEthereal",      0, false},

            // -- Cure family --
            {"cure",             "",         "$SLID_RsCatCure",             2, false},
            {"cure_disease",     "cure",     "$SLID_RsCatCureDisease",      0, true},
            {"cure_poison",      "cure",     "$SLID_RsCatCurePoison",       0, false},

            // -- Poisons (standalone) --
            {"poisons",          "",         "$SLID_RsCatPoisons",          0, false},

            // -- Ammo family --
            {"ammo",             "",         "$SLID_RsCatAmmo",             100, true},
            {"ammo_arrows",      "ammo",     "$SLID_RsCatAmmoArrows",       0,   true},
            {"ammo_bolts",       "ammo",     "$SLID_RsCatAmmoBolts",        0,   true},
            {"ammo_magic",       "ammo",     "$SLID_RsCatAmmoMagic",        0,   false},

            // -- Food family --
            {"food",             "",         "$SLID_RsCatFood",             0, false},
            {"food_cooked",      "food",     "$SLID_RsCatFoodCooked",       0, false},
            {"food_drinks",      "food",     "$SLID_RsCatFoodDrinks",       0, false},

            // -- Soul Gems family --
            {"soulgems",         "",           "$SLID_RsCatSoulGems",         10, true},
            {"soulgems_empty",   "soulgems",   "$SLID_RsCatSoulGemsEmpty",    0,  true},
            {"soulgems_filled",  "soulgems",   "$SLID_RsCatSoulGemsFilled",   0,  true},

            // -- Supplies family --
            {"supplies",         "",         "$SLID_RsCatSupplies",         0, false},
            {"supplies_torches", "supplies", "$SLID_RsCatSuppliesTorches",  0, false},
            {"supplies_firewood","supplies", "$SLID_RsCatSuppliesFirewood", 0, false},
        };
        return cats;
    }

    const std::vector<CategoryDef>& GetAllCategories() {
        return BuildCategories();
    }

    std::vector<std::string> GetFamilyRoots() {
        std::vector<std::string> roots;
        for (const auto& cat : GetAllCategories()) {
            if (cat.parentID.empty()) {
                roots.push_back(cat.id);
            }
        }
        return roots;
    }

    std::vector<std::string> GetChildren(const std::string& a_rootID) {
        std::vector<std::string> children;
        for (const auto& cat : GetAllCategories()) {
            if (cat.parentID == a_rootID) {
                children.push_back(cat.id);
            }
        }
        return children;
    }

    RestockConfig DefaultConfig() {
        RestockConfig config;
        config.configured = true;

        // Build parent→defaultQuantity lookup
        std::unordered_map<std::string, uint16_t> rootQty;
        for (const auto& cat : GetAllCategories()) {
            if (cat.parentID.empty() && cat.defaultQuantity > 0) {
                rootQty[cat.id] = cat.defaultQuantity;
            }
        }

        for (const auto& cat : GetAllCategories()) {
            if (!cat.defaultEnabled) continue;

            if (!cat.parentID.empty()) {
                // Child leaf — resolve parent's quantity
                auto qit = rootQty.find(cat.parentID);
                uint16_t qty = (qit != rootQty.end()) ? qit->second : 1;
                config.itemQuantities[cat.id] = qty;
            } else if (GetChildren(cat.id).empty()) {
                // Standalone root (no children) — use own quantity
                config.itemQuantities[cat.id] = cat.defaultQuantity > 0 ? cat.defaultQuantity : 1;
            }
        }

        return config;
    }

    std::vector<const CategoryDef*> GetLeafCategories() {
        std::vector<const CategoryDef*> leaves;
        for (const auto& cat : GetAllCategories()) {
            if (!cat.parentID.empty()) {
                // Child — always a leaf
                leaves.push_back(&cat);
            } else if (GetChildren(cat.id).empty()) {
                // Standalone root with no children — also a leaf
                leaves.push_back(&cat);
            }
        }
        return leaves;
    }

    bool IsFamilyRoot(const std::string& a_id) {
        for (const auto& cat : GetAllCategories()) {
            if (cat.id == a_id) {
                return cat.parentID.empty() && !GetChildren(cat.id).empty();
            }
        }
        return false;
    }

    // ---- Classification engine ----

    // Drink sound FormID (ITMPotionUse)
    static constexpr RE::FormID kDrinkSoundFormID = 0x000B6435;

    // Firewood FormID
    static constexpr RE::FormID kFirewoodFormID = 0x0006F993;  // Firewood01

    // Helper: find the "best" (highest magnitude) effect matching a predicate
    struct EffectMatch {
        std::string categoryID;
        float magnitude = 0.0f;
    };

    static EffectMatch ClassifyAlchemy(RE::AlchemyItem* a_alch) {
        if (!a_alch) return {};

        // Poison check first (before iterating effects)
        if (a_alch->IsPoison()) {
            logger::debug("  Classify '{}' {:08X}: IsPoison -> poisons",
                         a_alch->GetName(), a_alch->GetFormID());
            return {"poisons", 0.0f};
        }

        // Food check
        if (a_alch->IsFood()) {
            auto* useSound = a_alch->data.consumptionSound;
            if (useSound && useSound->GetFormID() == kDrinkSoundFormID) {
                logger::debug("  Classify '{}' {:08X}: IsFood + drink sound -> food_drinks",
                             a_alch->GetName(), a_alch->GetFormID());
                return {"food_drinks", 0.0f};
            }
            logger::debug("  Classify '{}' {:08X}: IsFood -> food_cooked",
                         a_alch->GetName(), a_alch->GetFormID());
            return {"food_cooked", 0.0f};
        }

        // Pre-scan: detect vampire blood potions (has kScript effect alongside restore)
        bool hasScriptEffect = false;
        for (const auto& effect : a_alch->effects) {
            if (effect && effect->baseEffect &&
                effect->baseEffect->GetArchetype() == RE::EffectSetting::Archetype::kScript) {
                hasScriptEffect = true;
                break;
            }
        }

        // Iterate effects for categorization
        // Priority: Restore > Cure > Utility > Resist > Fortify
        // Within each, take the highest magnitude match
        EffectMatch bestRestore, bestCure, bestResist, bestFortify, bestUtility;

        for (const auto& effect : a_alch->effects) {
            if (!effect || !effect->baseEffect) continue;

            auto* mgef = effect->baseEffect;
            auto archetype = mgef->GetArchetype();
            auto av = mgef->data.primaryAV;
            float mag = effect->effectItem.magnitude;
            auto duration = effect->effectItem.duration;

            using Archetype = RE::EffectSetting::Archetype;
            using Flag = RE::EffectSetting::EffectSettingData::Flag;

            logger::debug("  Classify '{}' {:08X}: effect '{}' archetype={} av={} mag={:.0f} dur={} flags={:08X}",
                         a_alch->GetName(), a_alch->GetFormID(),
                         mgef->GetFullName(),
                         static_cast<int>(archetype),
                         static_cast<int>(av),
                         mag, duration,
                         mgef->data.flags.underlying());

            // Cure Disease
            if (archetype == Archetype::kCureDisease) {
                if (bestCure.categoryID.empty() || mag > bestCure.magnitude) {
                    bestCure = {"cure_disease", mag};
                }
                continue;
            }

            // Cure Poison (archetype name may vary)
            if (archetype == Archetype::kCurePoison) {
                if (bestCure.categoryID.empty() || mag > bestCure.magnitude) {
                    bestCure = {"cure_poison", mag};
                }
                continue;
            }

            // Utility potions — distinct archetypes that don't use value modifiers
            if (archetype == Archetype::kInvisibility) {
                if (bestUtility.categoryID.empty()) {
                    bestUtility = {"utility_invisibility", static_cast<float>(duration)};
                }
                continue;
            }
            if (archetype == Archetype::kEtherealize) {
                if (bestUtility.categoryID.empty()) {
                    bestUtility = {"utility_ethereal", static_cast<float>(duration)};
                }
                continue;
            }

            // Only process value modifier archetypes from here
            bool isValueMod = (archetype == Archetype::kValueModifier);
            bool isPeakValueMod = (archetype == Archetype::kPeakValueModifier);

            // Utility: Waterbreathing/Waterwalking use PeakValueModifier with specific AVs
            if (isPeakValueMod) {
                if (av == AV::kWaterBreathing) {
                    if (bestUtility.categoryID.empty()) {
                        bestUtility = {"utility_waterbreathing", static_cast<float>(duration)};
                    }
                    continue;
                }
                if (av == AV::kWaterWalking) {
                    if (bestUtility.categoryID.empty()) {
                        bestUtility = {"utility_waterwalking", static_cast<float>(duration)};
                    }
                    continue;
                }
            }

            if (!isValueMod && !isPeakValueMod) continue;

            // Skip hostile/detrimental effects (damage, weakness)
            bool isHostile = mgef->data.flags.any(Flag::kHostile);
            bool isDetrimental = mgef->data.flags.any(Flag::kDetrimental);
            if (isHostile || isDetrimental) continue;

            // Restore: beneficial + AV is Health/Magicka/Stamina + NOT kRecover
            // kRecover flag distinguishes Fortify (temporary buff, kRecover set) from
            // Restore (direct value change, kRecover NOT set). Works for both archetype 0
            // (vanilla) and archetype 34 (alchemy overhaul mods).
            bool hasRecover = mgef->data.flags.any(Flag::kRecover);
            if (IsAttributeAV(av) && !hasRecover) {
                std::string catID;
                if (hasScriptEffect) {
                    // Vampire blood potions: has kScript effect alongside restore
                    catID = "restore_vampire";
                } else if (mag >= 500.0f) {
                    // Rare restoratives: Welkynd Stones and similar ultra-high magnitude items
                    catID = "restore_rare";
                } else if (av == AV::kHealth) {
                    catID = "restore_health";
                } else if (av == AV::kMagicka) {
                    catID = "restore_magicka";
                } else if (av == AV::kStamina) {
                    catID = "restore_stamina";
                }

                if (!catID.empty() && mag > bestRestore.magnitude) {
                    bestRestore = {catID, mag};
                }
            } else if (IsResistAV(av)) {
                // Resist (either archetype) — note: DamageResist (39) excluded, handled as misc fortify
                std::string catID;
                if (av == AV::kResistFire)        catID = "resist_fire";
                else if (av == AV::kResistFrost)   catID = "resist_frost";
                else if (av == AV::kResistShock)   catID = "resist_shock";
                else if (av == AV::kResistMagic)   catID = "resist_magic";
                else if (av == AV::kResistDisease) catID = "resist_disease";
                else if (av == AV::kPoisonResist)  catID = "resist_poison";

                if (!catID.empty() && mag > bestResist.magnitude) {
                    bestResist = {catID, mag};
                }
            } else if (duration > 0) {
                // Fortify (beneficial, has duration, either archetype)
                // Maps both base skill AVs and power-modifier AVs to the same categories
                std::string catID;
                auto avi = static_cast<int>(av);
                if (IsCombatSkillAV(av)) {
                    if (av == AV::kOneHanded || avi == AV::kPowerOneHanded)     catID = "fortify_onehanded";
                    else if (av == AV::kTwoHanded || avi == AV::kPowerTwoHanded) catID = "fortify_twohanded";
                    else if (av == AV::kArchery || avi == AV::kPowerMarksman)    catID = "fortify_archery";
                    else if (av == AV::kBlock || avi == AV::kPowerBlock)         catID = "fortify_block";
                    else if (av == AV::kHeavyArmor)  catID = "fortify_heavyarmor";
                    else if (av == AV::kLightArmor)  catID = "fortify_lightarmor";
                    else if (avi == AV::kPowerUnarmed)       catID = "fortify_onehanded";  // unarmed → combat
                } else if (IsMagicSkillAV(av)) {
                    if (av == AV::kAlteration || avi == AV::kPowerAlteration)       catID = "fortify_alteration";
                    else if (av == AV::kConjuration || avi == AV::kPowerConjuration) catID = "fortify_conjuration";
                    else if (av == AV::kDestruction || avi == AV::kPowerDestruction) catID = "fortify_destruction";
                    else if (av == AV::kIllusion || avi == AV::kPowerIllusion)       catID = "fortify_illusion";
                    else if (av == AV::kRestoration || avi == AV::kPowerRestoration) catID = "fortify_restoration";
                    else if (av == AV::kEnchanting)  catID = "fortify_enchanting";
                } else if (IsStealthSkillAV(av)) {
                    if (av == AV::kSneak || avi == AV::kPowerSneak)              catID = "fortify_sneak";
                    else if (av == AV::kLockpicking || avi == AV::kPowerLockpicking) catID = "fortify_lockpicking";
                    else if (av == AV::kPickpocket || avi == AV::kPowerPickpocket)   catID = "fortify_pickpocket";
                    else if (av == AV::kSpeech || avi == AV::kPowerBarter)       catID = "fortify_speech";
                    else if (av == AV::kAlchemy)     catID = "fortify_alchemy";
                    else if (av == AV::kSmithing)    catID = "fortify_smithing";
                    else if (avi == AV::kPowerShouts)        catID = "fortify_speech";  // shouts → speech family
                    else if (avi == AV::kPowerSneakAttacks)  catID = "fortify_sneak";   // sneak attacks → sneak
                } else if (IsAttributeAV(av)) {
                    if (av == AV::kHealth)           catID = "fortify_health";
                    else if (av == AV::kMagicka)     catID = "fortify_magicka";
                    else if (av == AV::kStamina)     catID = "fortify_stamina";
                } else if (IsRegenAV(av)) {
                    if (av == AV::kHealRate || avi == AV::kPowerHealthRegen)         catID = "fortify_healrate";
                    else if (av == AV::kMagickaRate || avi == AV::kPowerMagickaRegen) catID = "fortify_magickarate";
                    else if (av == AV::kStaminaRate || avi == AV::kPowerStaminaRegen) catID = "fortify_staminarate";
                } else if (IsMiscFortifyAV(av)) {
                    if (av == AV::kCarryWeight)               catID = "fortify_carryweight";
                    else if (av == AV::kSpeedMult)            catID = "fortify_speed";
                    else if (av == AV::kDamageResist)         catID = "fortify_armorrating";
                    else if (avi == AV::kPowerReflectDamage)  catID = "fortify_armorrating";  // reflect → armor family
                }

                if (!catID.empty() && mag > bestFortify.magnitude) {
                    bestFortify = {catID, mag};
                }
            }
        }

        // Priority: Restore > Cure > Utility > Resist > Fortify
        EffectMatch result;
        if (!bestRestore.categoryID.empty())       result = bestRestore;
        else if (!bestCure.categoryID.empty())     result = bestCure;
        else if (!bestUtility.categoryID.empty())  result = bestUtility;
        else if (!bestResist.categoryID.empty())   result = bestResist;
        else if (!bestFortify.categoryID.empty())  result = bestFortify;

        if (result.categoryID.empty()) {
            logger::debug("  Classify '{}' {:08X}: no matching effect category",
                         a_alch->GetName(), a_alch->GetFormID());
        } else {
            logger::debug("  Classify '{}' {:08X}: -> {} (mag={:.0f})",
                         a_alch->GetName(), a_alch->GetFormID(),
                         result.categoryID, result.magnitude);
        }
        return result;
    }

    std::string Classify(RE::TESBoundObject* a_item) {
        if (!a_item) return "";

        auto formType = a_item->GetFormType();

        // Ammo
        if (formType == RE::FormType::Ammo) {
            auto* ammo = a_item->As<RE::TESAmmo>();
            if (ammo) {
                // Magical ammo: projectile has an explosion (fire arrows, frost bolts, etc.)
                auto* proj = ammo->GetRuntimeData().data.projectile;
                if (proj && proj->data.explosionType) {
                    return "ammo_magic";
                }
                // kNonBolt flag: when set = arrow, when unset = bolt
                if (ammo->GetRuntimeData().data.flags.none(RE::AMMO_DATA::Flag::kNonBolt)) {
                    return "ammo_bolts";
                }
                return "ammo_arrows";
            }
        }

        // Soul Gems
        if (formType == RE::FormType::SoulGem) {
            auto* gem = a_item->As<RE::TESSoulGem>();
            if (gem && gem->GetContainedSoul() == RE::SOUL_LEVEL::kNone) {
                return "soulgems_empty";
            }
            return "soulgems_filled";
        }

        // Torch (Light with carry flag — actual torches)
        if (formType == RE::FormType::Light) {
            return "supplies_torches";
        }

        // Firewood
        if (a_item->GetFormID() == kFirewoodFormID) {
            return "supplies_firewood";
        }

        // Alchemy items (potions, poisons, food, drinks)
        if (formType == RE::FormType::AlchemyItem) {
            auto* alch = a_item->As<RE::AlchemyItem>();
            auto match = ClassifyAlchemy(alch);
            return match.categoryID;
        }

        return "";
    }

    float QualityScore(RE::TESBoundObject* a_item, [[maybe_unused]] const std::string& a_categoryID) {
        if (!a_item) return 0.0f;

        auto formType = a_item->GetFormType();

        // Ammo: damage value
        if (formType == RE::FormType::Ammo) {
            auto* ammo = a_item->As<RE::TESAmmo>();
            if (ammo) {
                return ammo->GetRuntimeData().data.damage;
            }
        }

        // Soul gems: capacity
        if (formType == RE::FormType::SoulGem) {
            auto* gem = a_item->As<RE::TESSoulGem>();
            if (gem) {
                return static_cast<float>(gem->GetMaximumCapacity());
            }
        }

        // Alchemy items: no quality preference — pull in natural order.
        // Potion "quality" is ambiguous (magnitude vs duration vs total healing)
        // and biases toward depleting rare items. Let inventory order decide.

        return 0.0f;
    }

}
