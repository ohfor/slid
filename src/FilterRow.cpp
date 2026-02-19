#include "FilterRow.h"
#include "ConfigState.h"
#include "ContainerRegistry.h"
#include "FilterRegistry.h"
#include "NetworkManager.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

// Child row visual constants
namespace {
    constexpr double CHILD_NAME_INDENT = 32.0;
    constexpr uint32_t COLOR_CHILD_FILTER = 0xAAAAAA;
    constexpr int CHILD_FONT_SIZE = 12;
    constexpr uint32_t COLOR_ROW_CHILD = 0x0D0D0D;
    constexpr int ALPHA_ROW_CHILD = 55;
    // Aggregate count (collapsed families)
    constexpr uint32_t COLOR_COUNT_AGGREGATE = 0x777777;
    // Expand indicator
    constexpr double EXPAND_X = 4.0;
    constexpr double EXPAND_Y = 11.0;
    constexpr double EXPAND_SIZE = 10.0;
    constexpr uint32_t COLOR_EXPAND = 0x888888;
}

FilterRow::FilterRow(Data a_data) : m_data(std::move(a_data)) {}

// --- Family ---

bool FilterRow::HasChildren() const { return !m_children.empty(); }
bool FilterRow::IsExpanded() const { return m_expanded; }
void FilterRow::SetExpanded(bool a_expanded) { m_expanded = a_expanded; }
const std::vector<FilterRow::Data>& FilterRow::GetChildren() const { return m_children; }
std::vector<FilterRow::Data>& FilterRow::MutableChildren() { return m_children; }
void FilterRow::SetChildren(std::vector<Data> a_children) { m_children = std::move(a_children); }

int FilterRow::GetDisplayRowCount() const {
    if (!m_expanded || m_children.empty()) return 1;
    return 1 + static_cast<int>(m_children.size());
}

// --- Data access ---

const FilterRow::Data& FilterRow::GetData() const { return m_data; }
FilterRow::Data& FilterRow::MutableData() { return m_data; }

// --- Pipeline output ---

FilterStage FilterRow::ToFilterStage() const {
    FilterStage f;
    f.filterID = m_data.filterID;
    f.containerFormID = m_data.containerFormID;
    return f;
}

std::vector<FilterStage> FilterRow::ToFilterStages() const {
    std::vector<FilterStage> stages;

    // Assigned children first (they specialize within the family's domain)
    for (const auto& child : m_children) {
        FilterStage f;
        f.filterID = child.filterID;
        f.containerFormID = child.containerFormID;
        stages.push_back(f);
    }

    // Root last (catches remainder of family's domain)
    {
        FilterStage f;
        f.filterID = m_data.filterID;
        f.containerFormID = m_data.containerFormID;
        stages.push_back(f);
    }

    return stages;
}

// --- Dropdown ---

namespace {
    int CountContainerItems(RE::FormID a_containerFormID) {
        return ContainerRegistry::GetSingleton()->CountItems(a_containerFormID);
    }

    std::vector<Dropdown::Entry> BuildContainerEntries() {
        auto pickerEntries = ContainerRegistry::GetSingleton()->BuildPickerList(ConfigState::GetMasterFormID());
        std::vector<Dropdown::Entry> entries;
        for (const auto& pe : pickerEntries) {
            Dropdown::Entry e;
            e.id = std::to_string(pe.formID);
            e.label = pe.name;
            e.sublabel = pe.location;
            e.group = pe.group;
            e.enabled = pe.enabled;
            if (!pe.enabled) {
                e.color = 0x555555;
            } else if (pe.color != 0) {
                e.color = pe.color;
            } else if (pe.isTagged) {
                e.color = MenuLayout::COLOR_PICKER_TAGGED;
            } else {
                e.color = MenuLayout::COLOR_PICKER_NAME;
            }
            entries.push_back(std::move(e));
        }
        return entries;
    }

    int FindPreSelect(const std::vector<Dropdown::Entry>& a_entries, RE::FormID a_formID) {
        std::string target = std::to_string(a_formID);
        for (int i = 0; i < static_cast<int>(a_entries.size()); i++) {
            if (a_entries[i].id == target) return i;
        }
        return -1;
    }
}

void FilterRow::OpenContainerDropdown(const DropdownContext& a_ctx, int a_childIndex,
                                       OnContainerResult a_onResult)
{
    if (!a_ctx.movie) return;

    // Determine current container for pre-selection
    RE::FormID currentContainer = 0;
    if (a_childIndex >= 0) {
        if (a_childIndex < static_cast<int>(m_children.size()))
            currentContainer = m_children[a_childIndex].containerFormID;
    } else {
        currentContainer = m_data.containerFormID;
    }

    auto entries = BuildContainerEntries();
    int preSelect = FindPreSelect(entries, currentContainer);

    // Sync selected value — also preserves full closed-state on cancel
    // (children share the parent's m_dropdown — stale m_selectedId from root otherwise)
    {
        RE::FormID masterFID = ConfigState::GetMasterFormID();
        std::string syncLabel;
        std::string syncLoc;
        uint32_t syncColor = 0;
        if (a_childIndex >= 0 && a_childIndex < static_cast<int>(m_children.size())) {
            syncLabel = m_children[a_childIndex].containerName;
            syncLoc = m_children[a_childIndex].location;
        } else {
            syncLabel = m_data.containerName;
            syncLoc = m_data.location;
        }
        if (currentContainer == masterFID && currentContainer != 0) {
            syncColor = MenuLayout::COLOR_KEEP;
            syncLabel = T("$SLID_Keep");
        } else if (currentContainer == 0) {
            syncColor = MenuLayout::COLOR_PASS;
            syncLabel = T("$SLID_Pass");
        } else {
            auto sellFID = NetworkManager::GetSingleton()->GetSellContainerFormID();
            if (currentContainer == sellFID && sellFID != 0)
                syncColor = MenuLayout::COLOR_SELL;
            else {
                auto display = ContainerRegistry::GetSingleton()->Resolve(currentContainer);
                if (display.color != 0)
                    syncColor = display.color;
            }
        }
        m_dropdown.SetValue(std::to_string(currentContainer), syncLabel, syncLoc, syncColor);
    }

    Dropdown::Config cfg;
    cfg.width = 400.0;
    cfg.title = T("$SLID_SelectContainer");
    cfg.preSelect = preSelect;

    // Capture by value — FilterRow* is stable (owned by s_filterRows which doesn't relocate during dropdown)
    auto* self = this;
    int childIdx = a_childIndex;
    auto onResult = std::move(a_onResult);

    RE::FormID oldFormID = currentContainer;

    m_dropdown.Open(a_ctx.movie,
        a_ctx.anchorX + MenuLayout::COL_CONTAINER_X, a_ctx.anchorY,
        cfg, std::move(entries),
        [self, childIdx, oldFormID, onResult](bool confirmed, [[maybe_unused]] int index, const std::string& id) {
            if (!confirmed) {
                logger::info("ContainerDropdown: cancelled");
                if (onResult) onResult(false);
                return;
            }

            RE::FormID newFormID = static_cast<RE::FormID>(std::stoul(id));

            if (newFormID == oldFormID) {
                logger::info("ContainerDropdown: same container selected, no change");
                if (onResult) onResult(false);
                return;
            }

            // Resolve display name — special entries get fixed names
            RE::FormID masterFormID = ConfigState::GetMasterFormID();
            std::string containerName;
            std::string containerLoc;

            if (newFormID == masterFormID && newFormID != 0) {
                containerName = T("$SLID_Keep");
                containerLoc = "";
            } else if (newFormID == 0) {
                containerName = T("$SLID_Pass");
                containerLoc = "";
            } else {
                auto display = ContainerRegistry::GetSingleton()->Resolve(newFormID);
                containerName = display.name;
                containerLoc = display.location;
            }

            // Keep/Pass have no separate destination — don't count master items
            bool isKeepOrPass = (newFormID == 0 || newFormID == masterFormID);
            int newCount = isKeepOrPass ? 0 : CountContainerItems(newFormID);

            if (childIdx >= 0) {
                auto& children = self->MutableChildren();
                if (childIdx >= static_cast<int>(children.size())) return;
                auto& child = children[childIdx];
                child.containerFormID = newFormID;
                child.containerName = containerName;
                child.location = containerLoc;
                child.count = newCount;
                logger::info("ContainerDropdown: child '{}' -> '{}'", child.name, child.containerName);
            } else {
                auto& data = self->MutableData();
                data.containerFormID = newFormID;
                data.containerName = containerName;
                data.location = containerLoc;
                data.count = newCount;
                logger::info("ContainerDropdown: root '{}' -> '{}'", data.name, data.containerName);
            }

            if (onResult) onResult(true);
        });
}

void FilterRow::BeginSetup(const DropdownContext& a_ctx,
                            const std::vector<FilterRow>& a_existingRows,
                            OnRefresh a_onRefresh,
                            std::function<void()> a_onCancelled)
{
    if (!a_ctx.movie) return;

    auto* registry = FilterRegistry::GetSingleton();
    const auto& familyRoots = registry->GetFamilyRoots();

    // Collect root filter IDs already in use (empty filterID from this row won't match)
    std::set<std::string> usedRootIDs;
    for (const auto& row : a_existingRows) {
        if (!row.GetData().filterID.empty())
            usedRootIDs.insert(row.GetData().filterID);
    }

    // Build dropdown entries from family roots
    std::vector<Dropdown::Entry> entries;
    for (const auto& id : familyRoots) {
        auto* filter = registry->GetFilter(id);
        if (!filter) continue;
        Dropdown::Entry e;
        e.id = id;
        e.label = std::string(filter->GetDisplayName());
        e.enabled = (usedRootIDs.find(id) == usedRootIDs.end());
        if (!e.enabled) {
            for (const auto& row : a_existingRows) {
                if (row.GetData().filterID == id && row.GetData().containerFormID != 0) {
                    e.sublabel = row.GetData().containerName;
                    break;
                }
            }
        }
        e.color = e.enabled ? MenuLayout::COLOR_FILTER : 0x555555;
        entries.push_back(std::move(e));
    }

    // Sort: available first (alpha), then unavailable (alpha)
    std::sort(entries.begin(), entries.end(),
        [](const Dropdown::Entry& a, const Dropdown::Entry& b) {
            if (a.enabled != b.enabled) return a.enabled > b.enabled;
            return a.label < b.label;
        });

    Dropdown::Config cfg;
    cfg.width = 360.0;
    cfg.title = T("$SLID_AddFilter");
    cfg.preSelect = -1;

    auto* self = this;
    auto onRefresh = std::move(a_onRefresh);
    auto onCancelled = std::move(a_onCancelled);

    m_dropdown.Open(a_ctx.movie, a_ctx.anchorX, a_ctx.anchorY,
        cfg, std::move(entries),
        [self, onRefresh, onCancelled](bool confirmed, [[maybe_unused]] int index, const std::string& id) {
            if (!confirmed || id.empty()) {
                logger::info("FilterDropdown: cancelled");
                if (onCancelled) onCancelled();
                return;
            }

            auto* reg = FilterRegistry::GetSingleton();
            auto* regFilter = reg->GetFilter(id);
            if (!regFilter) return;

            // Populate self
            self->m_data.filterID = id;
            self->m_data.name = std::string(regFilter->GetDisplayName());
            self->m_data.containerName = T("$SLID_Unlinked");
            self->m_data.containerFormID = 0;

            // Populate children from registry
            self->m_children.clear();
            const auto& registryChildren = reg->GetChildren(id);
            for (const auto& childID : registryChildren) {
                const IFilter* childFilter = reg->GetFilter(childID);
                if (!childFilter) continue;
                Data cd;
                cd.filterID = childID;
                cd.name = std::string(childFilter->GetDisplayName());
                cd.containerName = T("$SLID_Unlinked");
                cd.containerFormID = 0;
                cd.count = 0;
                cd.predictedCount = -1;
                self->m_children.push_back(std::move(cd));
            }

            logger::info("FilterDropdown: populated family '{}'", self->m_data.name);

            // Signal data changed — layout manager repaints and returns fresh context
            DropdownContext freshCtx;
            if (onRefresh) freshCtx = onRefresh();

            // Chain into container dropdown
            self->OpenContainerDropdown(freshCtx, -1,
                [onRefresh](bool containerConfirmed) {
                    if (containerConfirmed && onRefresh) onRefresh();
                    // Cancel is fine — row stays with "unlinked"
                });
        });
}

// --- Rendering ---

void FilterRow::Render(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
                       const std::string& a_clipPath,
                       int a_slotIndex, int a_displayNum,
                       bool a_selected, bool a_hovered, bool a_lifted,
                       bool a_chestHover, bool a_dropdownFocused) const
{
    RenderRoot(a_movie, a_clip, a_clipPath, a_slotIndex, a_displayNum,
               a_selected, a_hovered, a_lifted, a_chestHover, a_dropdownFocused);
}

void FilterRow::RenderRoot(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
                           const std::string& a_clipPath,
                           [[maybe_unused]] int a_slotIndex, int a_displayNum,
                           bool a_selected, bool a_hovered, bool a_lifted,
                           bool a_chestHover, bool a_dropdownFocused,
                           bool a_contested) const
{
    using namespace MenuLayout;

    // Background — contest tint only applies when not lifted/selected/hovered
    uint32_t bgColor;
    int bgAlpha;
    if (a_lifted) {
        bgColor = COLOR_ROW_LIFTED;
        bgAlpha = ALPHA_ROW_LIFTED;
    } else if (a_selected) {
        bgColor = COLOR_ROW_SELECT;
        bgAlpha = ALPHA_ROW_SELECT;
    } else if (a_hovered) {
        bgColor = COLOR_ROW_HOVER;
        bgAlpha = ALPHA_ROW_HOVER;
    } else if (a_contested) {
        bgColor = COLOR_ROW_CONTEST;
        bgAlpha = ALPHA_ROW_CONTEST;
    } else {
        bgColor = COLOR_ROW_NORMAL;
        bgAlpha = ALPHA_ROW_NORMAL;
    }
    DrawBackground(a_clip, bgColor, bgAlpha);

    // Text — all roots use the same indent to keep names aligned
    // Expand indicator area is always reserved; row number suppressed when indicator is present
    double nameIndent = EXPAND_X + EXPAND_SIZE + 4.0;
    int displayNum = HasChildren() ? -1 : a_displayNum;

    // Resolve container state once — used for count display, prediction, and color
    RE::FormID masterFID = ConfigState::GetMasterFormID();
    bool isKeep = (m_data.containerFormID == masterFID && m_data.containerFormID != 0);
    bool isPass = (m_data.containerFormID == 0);
    bool containerAvailable = isKeep || isPass;  // Keep/Pass are always "available"
    uint32_t sourceColor = 0;
    if (!isKeep && !isPass) {
        auto sellFID = NetworkManager::GetSingleton()->GetSellContainerFormID();
        if (m_data.containerFormID == sellFID && sellFID != 0) {
            sourceColor = COLOR_SELL;
            containerAvailable = true;  // sell container is always available
        } else {
            auto display = ContainerRegistry::GetSingleton()->Resolve(m_data.containerFormID);
            sourceColor = display.color;
            containerAvailable = display.available;
        }
    }

    // Collapsed families show aggregate count across all family members
    bool aggregate = HasChildren() && !m_expanded;
    int count = containerAvailable ? m_data.count : 0;
    int predicted = containerAvailable ? m_data.predictedCount : -1;
    int contested = m_data.contestedCount;
    int contAlpha = m_data.contestAlpha;
    uint32_t contColor = m_data.contestColor;
    if (aggregate) {
        // Deduplicate by container — multiple children sharing a container
        // must not double-count the items in that container.
        std::set<RE::FormID> seenContainers;
        count = 0;
        if (m_data.containerFormID != 0 && containerAvailable && seenContainers.insert(m_data.containerFormID).second) {
            count += m_data.count;
        }
        predicted = (containerAvailable && m_data.predictedCount >= 0) ? m_data.predictedCount : 0;
        contested = m_data.contestedCount;
        contAlpha = m_data.contestAlpha;
        bool hasPrediction = (containerAvailable && m_data.predictedCount >= 0);
        for (const auto& child : m_children) {
            if (child.containerFormID != 0 && seenContainers.insert(child.containerFormID).second) {
                // Check child container availability for aggregate
                bool childAvail = false;
                if (child.containerFormID == masterFID) {
                    childAvail = true;
                } else if (child.containerFormID != 0) {
                    auto cd = ContainerRegistry::GetSingleton()->Resolve(child.containerFormID);
                    childAvail = cd.available;
                }
                if (childAvail) count += child.count;
            }
            if (child.predictedCount >= 0) {
                predicted += child.predictedCount;
                hasPrediction = true;
            }
            contested += child.contestedCount;
            contAlpha = std::max(contAlpha, child.contestAlpha);
        }
        if (!hasPrediction) predicted = -1;
        contColor = 0;  // default color for aggregate
    }

    // Keep/Pass have no separate container — flatten prediction into count (no arrow)
    // -1 count signals "nothing to show" when no prediction is active
    if (isKeep || isPass) {
        count = (predicted >= 0) ? predicted : -1;
        predicted = -1;
    }

    DrawText(a_movie, a_clipPath, m_data.name,
             count, predicted, contested, contAlpha, contColor,
             displayNum, nameIndent, 14, 0xDDDDDD, aggregate);

    // Container column — dropdown closed state (sync value from data each frame)
    {
        uint32_t closedColor = 0;
        std::string closedLabel;
        if (isKeep) {
            closedColor = COLOR_KEEP;
            closedLabel = T("$SLID_Keep");
        } else if (isPass) {
            closedColor = COLOR_PASS;
            closedLabel = T("$SLID_Pass");
        } else {
            closedLabel = m_data.containerName;
            if (sourceColor != 0)
                closedColor = sourceColor;
        }
        m_dropdown.SetValue(
            std::to_string(m_data.containerFormID),
            closedLabel,
            m_data.location,
            closedColor);
    }
    m_dropdown.RenderClosed(a_movie, a_clip, a_clipPath,
                            COL_CONTAINER_X, 4.0,
                            COL_CONTAINER_W - 30.0, ROW_HEIGHT - 8.0,
                            a_dropdownFocused);

    // Chest icon — no icon for Keep (master) or Pass (unlinked)
    DrawChestIcon(a_clip, !isKeep && !isPass, a_chestHover);

    // Expand/collapse indicator
    if (HasChildren()) {
        DrawExpandIndicator(a_clip, m_expanded);
    } else {
        ClearExpandIndicator(a_clip);
    }
}

void FilterRow::RenderChild(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
                            const std::string& a_clipPath,
                            int a_childIndex,
                            bool a_selected, bool a_hovered,
                            bool a_chestHover, bool a_dropdownFocused,
                            bool a_contested) const
{
    using namespace MenuLayout;

    if (a_childIndex < 0 || a_childIndex >= static_cast<int>(m_children.size())) return;
    const auto& child = m_children[a_childIndex];

    // Child background — slightly different tint; contest tint when not selected/hovered
    uint32_t bgColor;
    int bgAlpha;
    if (a_selected) {
        bgColor = COLOR_ROW_SELECT;
        bgAlpha = ALPHA_ROW_SELECT;
    } else if (a_hovered) {
        bgColor = COLOR_ROW_HOVER;
        bgAlpha = ALPHA_ROW_HOVER;
    } else if (a_contested) {
        bgColor = COLOR_ROW_CONTEST;
        bgAlpha = ALPHA_ROW_CONTEST;
    } else {
        bgColor = COLOR_ROW_CHILD;
        bgAlpha = ALPHA_ROW_CHILD;
    }
    DrawBackground(a_clip, bgColor, bgAlpha);

    // Resolve container state once — used for count display, prediction, and color
    RE::FormID masterFID = ConfigState::GetMasterFormID();
    bool isKeep = (child.containerFormID == masterFID && child.containerFormID != 0);
    bool isPass = (child.containerFormID == 0);
    bool childAvailable = isKeep || isPass;
    uint32_t sourceColor = 0;
    if (!isKeep && !isPass) {
        auto sellFID = NetworkManager::GetSingleton()->GetSellContainerFormID();
        if (child.containerFormID == sellFID && sellFID != 0) {
            sourceColor = MenuLayout::COLOR_SELL;
            childAvailable = true;
        } else {
            auto display = ContainerRegistry::GetSingleton()->Resolve(child.containerFormID);
            sourceColor = display.color;
            childAvailable = display.available;
        }
    }

    // Text — indented, dimmer, smaller
    int childCount = childAvailable ? child.count : 0;
    int childPredicted = childAvailable ? child.predictedCount : -1;
    if (isKeep || isPass) {
        childCount = (childPredicted >= 0) ? childPredicted : -1;
        childPredicted = -1;
    }
    DrawText(a_movie, a_clipPath, child.name,
             childCount, childPredicted,
             child.contestedCount, child.contestAlpha, child.contestColor,
             -1, CHILD_NAME_INDENT, CHILD_FONT_SIZE, COLOR_CHILD_FILTER);

    // Container column — dropdown closed state
    {
        uint32_t closedColor = 0;
        std::string closedLabel;
        if (isKeep) {
            closedColor = MenuLayout::COLOR_KEEP;
            closedLabel = T("$SLID_Keep");
        } else if (isPass) {
            closedColor = MenuLayout::COLOR_PASS;
            closedLabel = T("$SLID_Pass");
        } else {
            closedLabel = child.containerName;
            if (sourceColor != 0)
                closedColor = sourceColor;
        }
        Dropdown childDD;
        childDD.SetValue(
            std::to_string(child.containerFormID),
            closedLabel,
            child.location,
            closedColor);
        childDD.RenderClosed(a_movie, a_clip, a_clipPath,
                             MenuLayout::COL_CONTAINER_X, 4.0,
                             MenuLayout::COL_CONTAINER_W - 30.0, MenuLayout::ROW_HEIGHT - 8.0,
                             a_dropdownFocused);
    }

    // Chest icon — no icon for Keep (master) or Pass (unlinked)
    DrawChestIcon(a_clip, !isKeep && !isPass, a_chestHover);

    // No expand indicator for children
    ClearExpandIndicator(a_clip);
}

// --- Drawing helpers ---

void FilterRow::DrawBackground(RE::GFxValue& a_clip, uint32_t a_color, int a_alpha) const {
    using namespace MenuLayout;

    RE::GFxValue bgClip;
    a_clip.GetMember("_bg", &bgClip);
    if (bgClip.IsUndefined()) {
        RE::GFxValue args[2];
        args[0].SetString("_bg");
        args[1].SetNumber(1.0);
        a_clip.Invoke("createEmptyMovieClip", &bgClip, args, 2);
    }
    if (bgClip.IsUndefined()) return;

    bgClip.Invoke("clear", nullptr, nullptr, 0);

    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(a_color));
    fillArgs[1].SetNumber(static_cast<double>(a_alpha));
    bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

    RE::GFxValue pt[2];
    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
    bgClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(ROW_W);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(ROW_HEIGHT - 2.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[0].SetNumber(0.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(0.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    bgClip.Invoke("endFill", nullptr, nullptr, 0);
}

void FilterRow::DrawText(RE::GFxMovieView* a_movie, const std::string& a_clipPath,
                         const std::string& a_name,
                         int a_count, int a_predictedCount,
                         int a_contestedCount, int a_contestAlpha, uint32_t a_contestColor,
                         int a_displayNum, double a_nameIndent,
                         int a_fontSize, uint32_t a_nameColor,
                         bool a_aggregate) const
{
    using namespace MenuLayout;

    // Row number (blank for children)
    {
        RE::GFxValue tf;
        a_movie->GetVariable(&tf, (a_clipPath + ".numText").c_str());
        if (!tf.IsUndefined()) {
            std::string num = (a_displayNum > 0) ? std::to_string(a_displayNum) : "";
            RE::GFxValue textVal;
            textVal.SetString(num.c_str());
            tf.SetMember("text", textVal);
        }
    }

    // Adjust name field position for indent
    {
        RE::GFxValue tf;
        a_movie->GetVariable(&tf, (a_clipPath + ".nameText").c_str());
        if (!tf.IsUndefined()) {
            RE::GFxValue xVal;
            xVal.SetNumber(COL_FILTER_X + a_nameIndent);
            tf.SetMember("_x", xVal);

            RE::GFxValue wVal;
            wVal.SetNumber(COL_FILTER_W - a_nameIndent);
            tf.SetMember("_width", wVal);
        }
    }

    // Filter name
    ScaleformUtil::SetTextFieldFormat(a_movie, (a_clipPath + ".nameText").c_str(), a_fontSize, a_nameColor);
    {
        RE::GFxValue tf;
        a_movie->GetVariable(&tf, (a_clipPath + ".nameText").c_str());
        if (!tf.IsUndefined()) {
            RE::GFxValue textVal;
            textVal.SetString(a_name.c_str());
            tf.SetMember("text", textVal);
        }
    }

    // Container column — rendered by Dropdown::RenderClosed (called from RenderRoot/RenderChild)

    // Count (with prediction delta)
    // a_count == -1 means "no container to count" (Keep/Pass) — show empty when no prediction
    {
        std::string countStr;
        uint32_t countColor = COLOR_COUNT;

        if (a_count < 0 && a_predictedCount < 0) {
            // No container, no prediction — show nothing
            countStr = "";
        } else if (a_aggregate) {
            // Collapsed family: bracketed total in muted color, no red/green
            if (a_predictedCount >= 0 && a_predictedCount != a_count) {
                countStr = "[" + std::to_string(a_count) + " > " + std::to_string(a_predictedCount) + "]";
            } else {
                countStr = "[" + std::to_string(a_count) + "]";
            }
            countColor = COLOR_COUNT_AGGREGATE;
        } else if (a_predictedCount >= 0 && a_predictedCount != a_count) {
            countStr = std::to_string(a_count) + " > " + std::to_string(a_predictedCount);
            countColor = (a_predictedCount > a_count) ? COLOR_COUNT_INCREASE : COLOR_COUNT_DECREASE;
        } else {
            countStr = std::to_string(a_count);
        }

        ScaleformUtil::SetTextFieldFormat(a_movie, (a_clipPath + ".countText").c_str(), 14, countColor);

        RE::GFxValue tf;
        a_movie->GetVariable(&tf, (a_clipPath + ".countText").c_str());
        if (!tf.IsUndefined()) {
            RE::GFxValue textVal;
            textVal.SetString(countStr.c_str());
            tf.SetMember("text", textVal);
        }
    }

    // Contest count (amber) — items matched but claimed by higher-priority filter
    {
        std::string contestStr;
        // Show during active predictions when alpha > 0 (includes fade-out of "+0")
        if (a_contestAlpha > 0 && a_predictedCount >= 0) {
            contestStr = "+" + std::to_string(a_contestedCount);
        }

        uint32_t cColor = (a_contestColor != 0) ? a_contestColor : COLOR_CONTEST;
        ScaleformUtil::SetTextFieldFormat(a_movie, (a_clipPath + ".contestText").c_str(), 12, cColor);

        RE::GFxValue tf;
        a_movie->GetVariable(&tf, (a_clipPath + ".contestText").c_str());
        if (!tf.IsUndefined()) {
            RE::GFxValue textVal;
            textVal.SetString(contestStr.c_str());
            tf.SetMember("text", textVal);

            RE::GFxValue alphaVal;
            alphaVal.SetNumber(static_cast<double>(a_contestAlpha));
            tf.SetMember("_alpha", alphaVal);
        }
    }
}

void FilterRow::DrawChestIcon(RE::GFxValue& a_clip, bool a_linked, bool a_hover) const {
    using namespace MenuLayout;

    if (!a_linked) {
        ClearChestIcon(a_clip);
        return;
    }

    RE::GFxValue iconClip;
    a_clip.GetMember("_chestIcon", &iconClip);
    if (iconClip.IsUndefined()) {
        RE::GFxValue args[2];
        args[0].SetString("_chestIcon");
        args[1].SetNumber(20.0);
        a_clip.Invoke("createEmptyMovieClip", &iconClip, args, 2);
    }
    if (iconClip.IsUndefined()) return;

    iconClip.Invoke("clear", nullptr, nullptr, 0);

    uint32_t color = a_hover ? COLOR_CHEST_HOVER : COLOR_CHEST_ICON;

    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(color));
    fillArgs[1].SetNumber(100.0);
    iconClip.Invoke("beginFill", nullptr, fillArgs, 2);

    RE::GFxValue pt[2];
    pt[0].SetNumber(ICON_CHEST_X); pt[1].SetNumber(ICON_CHEST_Y);
    iconClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(ICON_CHEST_X + ICON_CHEST_SIZE);
    iconClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(ICON_CHEST_Y + ICON_CHEST_SIZE);
    iconClip.Invoke("lineTo", nullptr, pt, 2);
    pt[0].SetNumber(ICON_CHEST_X);
    iconClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(ICON_CHEST_Y);
    iconClip.Invoke("lineTo", nullptr, pt, 2);
    iconClip.Invoke("endFill", nullptr, nullptr, 0);

    RE::GFxValue styleArgs[3];
    styleArgs[0].SetNumber(1.0);
    styleArgs[1].SetNumber(static_cast<double>(0x000000));
    styleArgs[2].SetNumber(60.0);
    iconClip.Invoke("lineStyle", nullptr, styleArgs, 3);

    double lidY = ICON_CHEST_Y + ICON_CHEST_SIZE * 0.3;
    pt[0].SetNumber(ICON_CHEST_X); pt[1].SetNumber(lidY);
    iconClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(ICON_CHEST_X + ICON_CHEST_SIZE);
    iconClip.Invoke("lineTo", nullptr, pt, 2);

    RE::GFxValue visVal;
    visVal.SetBoolean(true);
    iconClip.SetMember("_visible", visVal);
}

void FilterRow::ClearChestIcon(RE::GFxValue& a_clip) const {
    RE::GFxValue iconClip;
    a_clip.GetMember("_chestIcon", &iconClip);
    if (!iconClip.IsUndefined()) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        iconClip.SetMember("_visible", vis);
    }
}

void FilterRow::DrawExpandIndicator(RE::GFxValue& a_clip, bool a_expanded) const {
    RE::GFxValue indClip;
    a_clip.GetMember("_expandInd", &indClip);
    if (indClip.IsUndefined()) {
        RE::GFxValue args[2];
        args[0].SetString("_expandInd");
        args[1].SetNumber(15.0);
        a_clip.Invoke("createEmptyMovieClip", &indClip, args, 2);
    }
    if (indClip.IsUndefined()) return;

    indClip.Invoke("clear", nullptr, nullptr, 0);

    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(COLOR_EXPAND));
    fillArgs[1].SetNumber(80.0);
    indClip.Invoke("beginFill", nullptr, fillArgs, 2);

    RE::GFxValue pt[2];
    if (a_expanded) {
        // Down-pointing triangle (v)
        pt[0].SetNumber(EXPAND_X); pt[1].SetNumber(EXPAND_Y);
        indClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(EXPAND_X + EXPAND_SIZE);
        indClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(EXPAND_X + EXPAND_SIZE / 2.0);
        pt[1].SetNumber(EXPAND_Y + EXPAND_SIZE * 0.6);
        indClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(EXPAND_X); pt[1].SetNumber(EXPAND_Y);
        indClip.Invoke("lineTo", nullptr, pt, 2);
    } else {
        // Right-pointing triangle (>)
        pt[0].SetNumber(EXPAND_X); pt[1].SetNumber(EXPAND_Y);
        indClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(EXPAND_X + EXPAND_SIZE * 0.6);
        pt[1].SetNumber(EXPAND_Y + EXPAND_SIZE / 2.0);
        indClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(EXPAND_X);
        pt[1].SetNumber(EXPAND_Y + EXPAND_SIZE);
        indClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(EXPAND_X); pt[1].SetNumber(EXPAND_Y);
        indClip.Invoke("lineTo", nullptr, pt, 2);
    }

    indClip.Invoke("endFill", nullptr, nullptr, 0);

    RE::GFxValue vis;
    vis.SetBoolean(true);
    indClip.SetMember("_visible", vis);
}

void FilterRow::ClearExpandIndicator(RE::GFxValue& a_clip) const {
    RE::GFxValue indClip;
    a_clip.GetMember("_expandInd", &indClip);
    if (!indClip.IsUndefined()) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        indClip.SetMember("_visible", vis);
    }
}
