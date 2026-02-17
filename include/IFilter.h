#pragma once

#include <string_view>

namespace RE {
    class TESBoundObject;
}

class IFilter {
public:
    virtual ~IFilter() = default;
    virtual std::string_view GetID() const = 0;          // "weapons", "armor", etc.
    virtual std::string_view GetDisplayName() const = 0;  // "Weapons", "Armor", etc.
    virtual std::string_view GetDescription() const = 0;  // guide text
    virtual bool Matches(RE::TESBoundObject* a_item) const = 0;

    // Family hierarchy — nullptr for family roots
    virtual const IFilter* GetParent() const = 0;

    // Container binding — runtime state, set before Route() calls
    virtual void BindContainer(RE::FormID a_containerFormID) const = 0;
    virtual RE::FormID GetContainer() const = 0;

    // Composite dispatch — checks children first, then self. Query API, not called by pipeline.
    virtual RE::FormID Route(RE::TESBoundObject* a_item) const = 0;
};
