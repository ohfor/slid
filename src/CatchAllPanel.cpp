#include "CatchAllPanel.h"
#include "ActivationHook.h"
#include "ConfigState.h"
#include "ContainerRegistry.h"
#include "Dropdown.h"
#include "NetworkManager.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

namespace CatchAllPanel {

    using namespace MenuLayout;

    // --- Static state ---

    static RE::GFxMovieView* s_movie = nullptr;
    static RE::FormID s_masterFormID = 0;
    static Callbacks s_callbacks;

    // Catch-all data
    static std::string s_containerName;
    static RE::FormID s_containerFormID = 0;
    static std::string s_location;
    static int s_count = 0;
    static int s_predictedCount = -1;

    // Row clip (slot at CATCHALL_ROW_Y)
    static RE::GFxValue s_rowClip;

    // Selection/hover
    static bool s_selected = false;
    static bool s_hovered = false;
    static int s_hoverChest = -1;  // 0 = hovering chest icon

    // Count flash
    static bool s_flashActive = false;
    static std::chrono::steady_clock::time_point s_flashStart;

    // Hold-A for open linked container
    static bool s_holdAActive = false;
    static std::chrono::steady_clock::time_point s_holdAStart;

    // Container dropdown instance
    static Dropdown s_dropdown;

    // --- Forward declarations ---
    static void PopulateRow();
    static void DrawRowBackground(uint32_t a_color, int a_alpha);
    static void DrawChestIcon(bool a_linked, bool a_hover);
    static void HandleDropdownResult(bool a_confirmed, int a_index, const std::string& a_id);

    // Compute closed-state text color based on current container assignment
    static uint32_t ComputeClosedColor() {
        if (s_containerFormID == s_masterFormID && s_containerFormID != 0)
            return COLOR_KEEP;
        auto sellFormID = NetworkManager::GetSingleton()->GetSellContainerFormID();
        if (s_containerFormID == sellFormID && sellFormID != 0)
            return COLOR_SELL;
        if (s_containerFormID != 0) {
            auto display = ContainerRegistry::GetSingleton()->Resolve(s_containerFormID);
            if (display.color != 0)
                return display.color;
        }
        return 0;  // default linked color
    }

    // --- Lifecycle ---

    void Init(RE::GFxMovieView* a_movie, RE::FormID a_masterFormID, const Callbacks& a_callbacks) {
        s_movie = a_movie;
        s_masterFormID = a_masterFormID;
        s_callbacks = a_callbacks;
        s_selected = false;
        s_hovered = false;
        s_hoverChest = -1;
        s_flashActive = false;
        s_predictedCount = -1;
        s_holdAActive = false;

        // Default to Keep (master) so first PopulateRow() renders green before SetCatchAll() is called
        s_containerFormID = a_masterFormID;
        s_containerName = T("$SLID_Keep");
        s_location = "";
        s_count = 0;
        s_dropdown.SetValue(std::to_string(a_masterFormID), T("$SLID_Keep"), "", COLOR_KEEP);
    }

    void Destroy() {
        s_dropdown.Destroy();
        s_rowClip = RE::GFxValue();
        s_movie = nullptr;
    }

    void Draw() {
        if (!s_movie) return;

        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        // Create row clip at CATCHALL_ROW_Y (depth 215, above filter rows 200-208)
        std::string rowName = "catchAllRow";
        RE::GFxValue args[2];
        args[0].SetString(rowName.c_str());
        args[1].SetNumber(215.0);
        root.Invoke("createEmptyMovieClip", &s_rowClip, args, 2);

        if (s_rowClip.IsUndefined()) return;

        RE::GFxValue posX, posY;
        posX.SetNumber(ROW_X);
        posY.SetNumber(CATCHALL_ROW_Y);
        s_rowClip.SetMember("_x", posX);
        s_rowClip.SetMember("_y", posY);

        // Background clip
        RE::GFxValue bgArgs[2];
        bgArgs[0].SetString("_bg");
        bgArgs[1].SetNumber(1.0);
        s_rowClip.Invoke("createEmptyMovieClip", nullptr, bgArgs, 2);

        // Text fields
        double textY = 8.0;
        double rowH = ROW_HEIGHT;

        // Filter name text
        {
            RE::GFxValue tfArgs[6];
            tfArgs[0].SetString("nameText"); tfArgs[1].SetNumber(10.0);
            tfArgs[2].SetNumber(COL_FILTER_X); tfArgs[3].SetNumber(textY);
            tfArgs[4].SetNumber(COL_FILTER_W); tfArgs[5].SetNumber(rowH - 4.0);
            s_rowClip.Invoke("createTextField", nullptr, tfArgs, 6);
            ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".nameText", 14, COLOR_FILTER);
        }

        // Container name text
        {
            RE::GFxValue tfArgs[6];
            tfArgs[0].SetString("containerText"); tfArgs[1].SetNumber(30.0);
            tfArgs[2].SetNumber(COL_CONTAINER_X); tfArgs[3].SetNumber(textY);
            tfArgs[4].SetNumber(COL_CONTAINER_W); tfArgs[5].SetNumber(rowH - 4.0);
            s_rowClip.Invoke("createTextField", nullptr, tfArgs, 6);
            ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".containerText", 14, COLOR_CONTAINER);
        }

        // Count text
        {
            RE::GFxValue tfArgs[6];
            tfArgs[0].SetString("countText"); tfArgs[1].SetNumber(12.0);
            tfArgs[2].SetNumber(COL_ITEMS_X); tfArgs[3].SetNumber(textY);
            tfArgs[4].SetNumber(COL_ITEMS_W); tfArgs[5].SetNumber(rowH - 4.0);
            s_rowClip.Invoke("createTextField", nullptr, tfArgs, 6);
            ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".countText", 14, COLOR_COUNT);
        }

        // Chest icon sub-clip
        {
            RE::GFxValue iconArgs[2];
            iconArgs[0].SetString("_chestIcon");
            iconArgs[1].SetNumber(20.0);
            s_rowClip.Invoke("createEmptyMovieClip", nullptr, iconArgs, 2);
        }

        PopulateRow();
    }

    void Update() {
        if (s_holdAActive) {
            float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - s_holdAStart).count();
            if (elapsed >= HOLD_OPEN_DURATION) {
                s_holdAActive = false;
                OpenLinkedContainer();
            }
        }
        if (!s_flashActive) return;
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_flashStart).count();
        if (elapsed >= COUNT_FLASH_DURATION) {
            s_flashActive = false;
            PopulateRow();
        }
    }

    // --- Selection ---

    void Select() {
        if (s_selected) return;
        s_selected = true;
        PopulateRow();
    }

    void Deselect() {
        if (!s_selected) return;
        s_selected = false;
        PopulateRow();
    }

    bool IsSelected() { return s_selected; }

    // --- Activation ---

    void Activate() {
        if (!s_movie) return;

        // Build dropdown entries — no Pass for catch-all (items must route somewhere)
        auto pickerEntries = ContainerRegistry::GetSingleton()->BuildPickerList(s_masterFormID);
        if (pickerEntries.empty()) return;

        std::vector<Dropdown::Entry> entries;
        int preSelect = -1;

        for (const auto& pe : pickerEntries) {
            // Skip Pass — catch-all must always route to a container
            if (pe.formID == 0 && pe.group == 0 && pe.name == T("$SLID_Pass")) continue;

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
                e.color = COLOR_PICKER_TAGGED;
            } else {
                e.color = COLOR_PICKER_NAME;
            }

            // Pre-select current container
            if (pe.formID == s_containerFormID) {
                preSelect = static_cast<int>(entries.size());
            }
            entries.push_back(std::move(e));
        }

        // Sync selected value for correct "> " prefix (preserve full state for cancel)
        s_dropdown.SetValue(std::to_string(s_containerFormID), s_containerName,
                            s_location, ComputeClosedColor());

        Dropdown::Config cfg;
        cfg.width = 400.0;
        cfg.title = T("$SLID_SelectContainer");
        cfg.preSelect = preSelect;

        s_dropdown.Open(s_movie, ROW_X + COL_CONTAINER_X, CATCHALL_ROW_Y,
                        cfg, std::move(entries), HandleDropdownResult);
    }

    void OpenLinkedContainer() {
        if (Dropdown::IsAnyOpen() || s_containerFormID == 0) return;

        logger::info("CatchAllPanel::OpenLinkedContainer: opening container {:08X}", s_containerFormID);

        if (s_callbacks.saveState) s_callbacks.saveState();
        if (s_callbacks.hideMenu) s_callbacks.hideMenu();

        auto formID = s_containerFormID;
        SKSE::GetTaskInterface()->AddTask([formID]() {
            auto* container = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
            if (!container) {
                logger::error("CatchAllPanel::OpenLinkedContainer: container {:08X} not found", formID);
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            ActivationHook::SetBypass(formID);
            container->ActivateRef(player, 0, nullptr, 0, false);
        });
    }

    // --- Hold-A ---

    void StartHoldA() {
        s_holdAActive = true;
        s_holdAStart = std::chrono::steady_clock::now();
    }

    void CancelHoldA() {
        s_holdAActive = false;
    }

    bool IsHoldingA() { return s_holdAActive; }

    // --- Prediction / counts ---

    void SetPrediction(int a_count, bool) {
        s_predictedCount = a_count;
        PopulateRow();
    }

    void ClearPrediction() {
        s_predictedCount = -1;
    }

    void SetCount(int a_count, bool a_flash) {
        s_count = a_count;
        if (a_flash) {
            s_flashActive = true;
            s_flashStart = std::chrono::steady_clock::now();
        }
        PopulateRow();
        if (a_flash && s_movie) {
            ScaleformUtil::SetTextFieldFormat(s_movie, "_root.catchAllRow.countText",
                14, COLOR_COUNT_FLASH);
        }
    }

    void RefreshCount() {
        PopulateRow();
    }

    // --- Data ---

    void SetCatchAll(const std::string& a_name, RE::FormID a_formID,
                     const std::string& a_location, int a_count) {
        s_predictedCount = -1;

        // Resolve display
        if (a_formID == s_masterFormID && a_formID != 0) {
            s_containerName = T("$SLID_Keep");
            s_containerFormID = a_formID;
            s_location = "";
            s_count = 0;
        } else {
            s_containerName = a_name;
            s_containerFormID = a_formID;
            s_location = a_location;
            s_count = a_count;
        }

        s_dropdown.SetValue(
            std::to_string(s_containerFormID),
            s_containerName,
            s_location,
            ComputeClosedColor());
        if (s_rowClip.IsUndefined()) return;
        PopulateRow();
    }

    RE::FormID GetContainerFormID() { return s_containerFormID; }
    const std::string& GetContainerName() { return s_containerName; }

    bool HasLinkedContainer() {
        return s_containerFormID != 0 && s_containerFormID != s_masterFormID;
    }

    // --- Guide text ---

    std::string GetGuideText() {
        if (HasLinkedContainer()) {
            return TF("$SLID_GuideCatchAllLinked", s_containerName);
        }
        return T("$SLID_GuideCatchAllMaster");
    }

    // --- Mouse ---

    HitZone HitTest(float a_mx, float a_my, int& a_outIndex) {
        a_outIndex = -1;

        // Catch-all row
        double rowY = CATCHALL_ROW_Y;
        if (a_mx >= ROW_X && a_mx <= ROW_X + ROW_W &&
            a_my >= rowY && a_my <= rowY + ROW_HEIGHT) {

            // Chest icon hit
            if (s_containerFormID != 0) {
                double iconCenterX = ROW_X + ICON_CHEST_X + ICON_CHEST_SIZE / 2.0;
                double iconCenterY = rowY + ICON_CHEST_Y + ICON_CHEST_SIZE / 2.0;
                double halfHit = ICON_CHEST_HIT_SIZE / 2.0;
                if (a_mx >= iconCenterX - halfHit && a_mx <= iconCenterX + halfHit &&
                    a_my >= iconCenterY - halfHit && a_my <= iconCenterY + halfHit) {
                    return HitZone::kChestIcon;
                }
            }
            return HitZone::kRow;
        }

        return HitZone::kNone;
    }

    void UpdateHover(float a_mx, float a_my) {
        bool oldHovered = s_hovered;
        int oldHoverChest = s_hoverChest;

        s_hovered = false;
        s_hoverChest = -1;

        int dummy = -1;
        auto zone = HitTest(a_mx, a_my, dummy);

        switch (zone) {
            case HitZone::kRow:
                s_hovered = true;
                break;
            case HitZone::kChestIcon:
                s_hovered = true;
                s_hoverChest = 0;
                break;
            default:
                break;
        }

        bool changed = (s_hovered != oldHovered || s_hoverChest != oldHoverChest);
        if (changed) {
            PopulateRow();
        }
    }

    void ClearHover() {
        if (!s_hovered && s_hoverChest < 0) return;
        s_hovered = false;
        s_hoverChest = -1;
        PopulateRow();
    }

    bool IsHovered() { return s_hovered; }

    // --- Internal: Row rendering ---

    static void PopulateRow() {
        if (!s_movie || s_rowClip.IsUndefined()) return;

        // Background
        uint32_t bgColor = (s_selected && !s_hovered) ? COLOR_ROW_SELECT :
                          (s_hovered ? COLOR_ROW_HOVER : COLOR_ROW_FIXED);
        int bgAlpha = (s_selected && !s_hovered) ? ALPHA_ROW_SELECT :
                     (s_hovered ? ALPHA_ROW_HOVER : ALPHA_ROW_FIXED);
        DrawRowBackground(bgColor, bgAlpha);

        // Name
        std::string nameText = T("$SLID_EverythingElse");
        {
            RE::GFxValue val;
            val.SetString(nameText.c_str());
            s_movie->SetVariable("_root.catchAllRow.nameText.text", val);
        }

        // Container — dropdown closed state
        s_dropdown.RenderClosed(s_movie, s_rowClip, "_root.catchAllRow",
                                COL_CONTAINER_X, 4.0,
                                COL_CONTAINER_W - 30.0, ROW_HEIGHT - 8.0,
                                s_selected);

        // Count — Keep has no separate container, show only prediction (no delta)
        // Unavailable containers show no count or prediction
        {
            bool isKeep = (s_containerFormID == s_masterFormID && s_containerFormID != 0);
            bool available = isKeep;
            if (!isKeep && s_containerFormID != 0) {
                auto display = ContainerRegistry::GetSingleton()->Resolve(s_containerFormID);
                available = display.available;
            }
            int displayCount = (!available) ? 0 : (isKeep ? 0 : s_count);
            int displayPredicted = available ? s_predictedCount : -1;

            // Keep: flatten prediction into count (never show delta arrow)
            if (isKeep) {
                displayCount = (displayPredicted >= 0) ? displayPredicted : -1;
                displayPredicted = -1;
            }

            if (s_flashActive && !isKeep) {
                std::string countStr = std::to_string(displayCount);
                RE::GFxValue val;
                val.SetString(countStr.c_str());
                s_movie->SetVariable("_root.catchAllRow.countText.text", val);
                ScaleformUtil::SetTextFieldFormat(s_movie, "_root.catchAllRow.countText", 14, COLOR_COUNT_FLASH);
            } else if (displayCount < 0) {
                // No container, no prediction — show nothing
                RE::GFxValue val;
                val.SetString("");
                s_movie->SetVariable("_root.catchAllRow.countText.text", val);
            } else if (displayPredicted >= 0 && displayPredicted != displayCount) {
                std::string currentStr = std::to_string(displayCount);
                std::string arrowStr = " > ";
                std::string predictedStr = std::to_string(displayPredicted);
                std::string fullStr = currentStr + arrowStr + predictedStr;

                RE::GFxValue val;
                val.SetString(fullStr.c_str());
                s_movie->SetVariable("_root.catchAllRow.countText.text", val);
                ScaleformUtil::SetTextFieldFormat(s_movie, "_root.catchAllRow.countText", 14, COLOR_COUNT);

                // Color only the predicted portion
                uint32_t predictedColor = (displayPredicted > displayCount)
                    ? COLOR_COUNT_INCREASE : COLOR_COUNT_DECREASE;
                RE::GFxValue tf;
                s_movie->GetVariable(&tf, "_root.catchAllRow.countText");
                if (!tf.IsUndefined()) {
                    RE::GFxValue deltaFmt;
                    s_movie->CreateObject(&deltaFmt, "TextFormat");
                    if (!deltaFmt.IsUndefined()) {
                        RE::GFxValue colorVal;
                        colorVal.SetNumber(static_cast<double>(predictedColor));
                        deltaFmt.SetMember("color", colorVal);

                        int start = static_cast<int>(currentStr.length() + arrowStr.length());
                        int end = static_cast<int>(fullStr.length());
                        RE::GFxValue fmtArgs[3];
                        fmtArgs[0].SetNumber(static_cast<double>(start));
                        fmtArgs[1].SetNumber(static_cast<double>(end));
                        fmtArgs[2] = deltaFmt;
                        tf.Invoke("setTextFormat", nullptr, fmtArgs, 3);
                    }
                }
            } else {
                std::string countStr = (displayCount >= 0) ? std::to_string(displayCount) : "";
                RE::GFxValue val;
                val.SetString(countStr.c_str());
                s_movie->SetVariable("_root.catchAllRow.countText.text", val);
                ScaleformUtil::SetTextFieldFormat(s_movie, "_root.catchAllRow.countText", 14, COLOR_COUNT);
            }
        }

        // Chest icon
        bool chestHover = (s_hoverChest >= 0);
        DrawChestIcon(HasLinkedContainer(), chestHover);
    }

    static void DrawRowBackground(uint32_t a_color, int a_alpha) {
        if (s_rowClip.IsUndefined()) return;

        RE::GFxValue bgClip;
        s_rowClip.GetMember("_bg", &bgClip);
        if (bgClip.IsUndefined()) return;

        bgClip.Invoke("clear", nullptr, nullptr, 0);

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(a_color));
        fillArgs[1].SetNumber(static_cast<double>(a_alpha));
        bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

        double w = ROW_W;
        double h = ROW_HEIGHT;

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        bgClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(w);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(h);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        bgClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    static void DrawChestIcon(bool a_linked, bool a_hover) {
        if (s_rowClip.IsUndefined()) return;

        RE::GFxValue iconClip;
        s_rowClip.GetMember("_chestIcon", &iconClip);
        if (iconClip.IsUndefined()) {
            RE::GFxValue args[2];
            args[0].SetString("_chestIcon");
            args[1].SetNumber(20.0);
            s_rowClip.Invoke("createEmptyMovieClip", &iconClip, args, 2);
        }
        if (iconClip.IsUndefined()) return;

        iconClip.Invoke("clear", nullptr, nullptr, 0);

        if (!a_linked) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            iconClip.SetMember("_visible", vis);
            return;
        }

        RE::GFxValue vis;
        vis.SetBoolean(true);
        iconClip.SetMember("_visible", vis);

        uint32_t color = a_hover ? COLOR_CHEST_HOVER : COLOR_CHEST_ICON;

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(color));
        fillArgs[1].SetNumber(100.0);
        iconClip.Invoke("beginFill", nullptr, fillArgs, 2);

        double x = ICON_CHEST_X;
        double y = ICON_CHEST_Y;
        double s = ICON_CHEST_SIZE;

        RE::GFxValue pt[2];
        pt[0].SetNumber(x); pt[1].SetNumber(y);
        iconClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(x + s);
        iconClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(y + s);
        iconClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(x);
        iconClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(y);
        iconClip.Invoke("lineTo", nullptr, pt, 2);
        iconClip.Invoke("endFill", nullptr, nullptr, 0);

        RE::GFxValue styleArgs[3];
        styleArgs[0].SetNumber(1.0);
        styleArgs[1].SetNumber(static_cast<double>(0x000000));
        styleArgs[2].SetNumber(60.0);
        iconClip.Invoke("lineStyle", nullptr, styleArgs, 3);

        double lidY = y + s * 0.3;
        pt[0].SetNumber(x); pt[1].SetNumber(lidY);
        iconClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(x + s);
        iconClip.Invoke("lineTo", nullptr, pt, 2);
    }

    // --- Internal: Dropdown callback ---

    static void HandleDropdownResult(bool a_confirmed, [[maybe_unused]] int a_index, const std::string& a_id) {
        if (!a_confirmed) {
            PopulateRow();
            return;
        }

        RE::FormID newFormID = static_cast<RE::FormID>(std::stoul(a_id));
        s_containerFormID = newFormID;

        // Resolve display name
        if (newFormID == s_masterFormID && newFormID != 0) {
            s_containerName = T("$SLID_Keep");
            s_location = "";
        } else {
            auto display = ContainerRegistry::GetSingleton()->Resolve(newFormID);
            s_containerName = display.name;
            s_location = display.location;
        }
        s_dropdown.SetValue(
            std::to_string(s_containerFormID),
            s_containerName,
            s_location,
            ComputeClosedColor());

        // Count items — Keep has no separate container, skip counting master
        s_count = 0;
        if (newFormID != s_masterFormID) {
            s_count = ContainerRegistry::GetSingleton()->CountItems(s_containerFormID);
        }

        if (s_callbacks.commitToNetwork) s_callbacks.commitToNetwork();
        if (s_callbacks.recalcPredictions) s_callbacks.recalcPredictions();
        PopulateRow();
    }
}
