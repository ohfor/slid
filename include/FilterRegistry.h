#pragma once

#include "IFilter.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class FilterRegistry {
public:
    static FilterRegistry* GetSingleton();

    /// Create all filter instances and build family index. Call once from kDataLoaded.
    void Init();

    /// Look up the filter for a given string ID. Returns nullptr if not found.
    const IFilter* GetFilter(const std::string& a_id) const;

    /// Registration order (all filters, roots and children).
    const std::vector<std::string>& GetFilterOrder() const;

    /// Number of registered filters.
    size_t GetFilterCount() const;

    /// Child filter IDs for a given root ID. Empty vector if no children or not a root.
    const std::vector<std::string>& GetChildren(const std::string& a_rootID) const;

    /// Root filter IDs in registration order.
    const std::vector<std::string>& GetFamilyRoots() const;

    /// Default Whoosh filter set: all IDs except those with DefaultExclude = true in INI.
    static std::unordered_set<std::string> DefaultWhooshFilters();

    /// Debug: log all registered filters to SKSE log.
    void DumpToLog() const;

private:
    FilterRegistry() = default;

    std::unordered_map<std::string, std::unique_ptr<IFilter>> m_filters;
    std::vector<std::string> m_order;

    // Family index — built at end of Init()
    std::vector<std::string> m_familyRoots;
    std::unordered_map<std::string, std::vector<std::string>> m_children;
    static const std::vector<std::string> s_emptyChildren;

    // Filter IDs with DefaultExclude = true — excluded from default Whoosh set
    std::unordered_set<std::string> m_defaultExcluded;
};
