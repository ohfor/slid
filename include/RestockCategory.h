#pragma once

#include <map>
#include <string>
#include <vector>

namespace RestockCategory {

    struct CategoryDef {
        std::string id;
        std::string parentID;        // empty = family root
        std::string displayNameKey;  // translation key e.g. "$SLID_RsCatRestoreHealth"
        uint16_t    defaultQuantity; // family default (only meaningful on roots)
        bool        defaultEnabled;
    };

    struct RestockConfig {
        std::map<std::string, uint16_t>  itemQuantities;  // leafCategoryID -> quantity (presence = enabled)
        bool configured = false;
    };

    /// Static category taxonomy
    const std::vector<CategoryDef>& GetAllCategories();

    /// Family roots (top-level categories)
    std::vector<std::string> GetFamilyRoots();

    /// Children of a family root (empty if root has no children or is standalone)
    std::vector<std::string> GetChildren(const std::string& a_rootID);

    /// Default configuration (sensible adventurer defaults)
    RestockConfig DefaultConfig();

    /// All leaf categories (non-root, or standalone roots with no children)
    std::vector<const CategoryDef*> GetLeafCategories();

    /// True if the given ID is a family root with children
    bool IsFamilyRoot(const std::string& a_id);

    /// Classify an item into its first matching category ID (first-claim-wins).
    /// Returns empty string if no category matches.
    std::string Classify(RE::TESBoundObject* a_item);

    /// Quality score for an item within a category (higher = better).
    /// Used for "best first" sorting when pulling from storage.
    float QualityScore(RE::TESBoundObject* a_item, const std::string& a_categoryID);
}
