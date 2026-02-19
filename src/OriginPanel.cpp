#include "OriginPanel.h"
#include "MenuLayout.h"
#include "ScaleformUtil.h"

namespace OriginPanel {

    // Colors (match SLIDMenu palette)
    static constexpr uint32_t COLOR_FILTER     = 0xDDDDDD;
    static constexpr uint32_t COLOR_CONTAINER  = 0xAAAAAA;
    static constexpr uint32_t COLOR_COUNT      = 0x999999;
    static constexpr uint32_t COLOR_ROW_FIXED  = 0x222222;
    static constexpr int      ALPHA_ROW_FIXED  = 70;

    // Predictive count colors
    static constexpr uint32_t COLOR_COUNT_INCREASE = 0x88CC88;
    static constexpr uint32_t COLOR_COUNT_DECREASE = 0xCC8888;

    // Count flash color (after Sort)
    static constexpr uint32_t COLOR_COUNT_FLASH = 0xFFFF88;

    // Column layout (matches SLIDMenu columns)
    static constexpr double COL_FILTER_X    = 28.0;
    static constexpr double COL_FILTER_W    = 200.0;
    static constexpr double COL_CONTAINER_X = 240.0;
    static constexpr double COL_CONTAINER_W = 360.0;
    static constexpr double COL_ITEMS_X     = 620.0;
    static constexpr double COL_ITEMS_W     = 80.0;

    static constexpr const char* CLIP_PATH = "_root.originRow";

    static RE::GFxValue s_clip;

    // Flash timer
    static bool s_flashActive = false;
    static std::chrono::steady_clock::time_point s_flashStart;
    static int s_lastCount = 0;

    void Draw(RE::GFxMovieView* a_movie, RE::FormID a_masterFormID,
              double a_x, double a_y, double a_w, double a_rowH) {
        if (!a_movie) return;

        RE::GFxValue root;
        a_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue args[2];
        args[0].SetString("originRow");
        args[1].SetNumber(199.0);
        root.Invoke("createEmptyMovieClip", &s_clip, args, 2);
        if (s_clip.IsUndefined()) return;

        RE::GFxValue posX, posY;
        posX.SetNumber(a_x);
        posY.SetNumber(a_y);
        s_clip.SetMember("_x", posX);
        s_clip.SetMember("_y", posY);

        // nameText — label ("Origin")
        RE::GFxValue nameArgs[6];
        nameArgs[0].SetString("nameText"); nameArgs[1].SetNumber(11.0);
        nameArgs[2].SetNumber(COL_FILTER_X); nameArgs[3].SetNumber(5.0);
        nameArgs[4].SetNumber(COL_FILTER_W); nameArgs[5].SetNumber(24.0);
        s_clip.Invoke("createTextField", nullptr, nameArgs, 6);

        // containerText — master chest name
        RE::GFxValue contArgs[6];
        contArgs[0].SetString("containerText"); contArgs[1].SetNumber(13.0);
        contArgs[2].SetNumber(COL_CONTAINER_X); contArgs[3].SetNumber(5.0);
        contArgs[4].SetNumber(COL_CONTAINER_W); contArgs[5].SetNumber(24.0);
        s_clip.Invoke("createTextField", nullptr, contArgs, 6);

        // countText — item count
        RE::GFxValue cntArgs[6];
        cntArgs[0].SetString("countText"); cntArgs[1].SetNumber(12.0);
        cntArgs[2].SetNumber(COL_ITEMS_X); cntArgs[3].SetNumber(6.0);
        cntArgs[4].SetNumber(COL_ITEMS_W); cntArgs[5].SetNumber(22.0);
        s_clip.Invoke("createTextField", nullptr, cntArgs, 6);

        // Format
        std::string basePath = CLIP_PATH;
        ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".nameText", 15, COLOR_FILTER);
        ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".containerText", 14, COLOR_CONTAINER);
        ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".countText", 14, COLOR_COUNT);

        // Background
        RE::GFxValue bgClip;
        RE::GFxValue bgArgs[2];
        bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
        s_clip.Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);
        if (!bgClip.IsUndefined()) {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(COLOR_ROW_FIXED));
            fillArgs[1].SetNumber(static_cast<double>(ALPHA_ROW_FIXED));
            bgClip.Invoke("beginFill", nullptr, fillArgs, 2);
            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            bgClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(a_w);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(a_rowH - 2.0);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            bgClip.Invoke("endFill", nullptr, nullptr, 0);
        }

        // Populate with master container data — direct REFR lookup, not source-managed
        std::string masterName;
        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_masterFormID);
        if (masterRef) {
            auto* cell = masterRef->GetParentCell();
            std::string cellName;
            if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                cellName = cell->GetFullName();
            }
            std::string baseName;
            if (masterRef->GetBaseObject() && masterRef->GetBaseObject()->GetName()) {
                baseName = masterRef->GetBaseObject()->GetName();
            }
            masterName = cellName.empty() ? baseName : (cellName + ": " + baseName);
        }
        if (masterName.empty()) masterName = "Master";

        RE::GFxValue nameVal, contVal, cntVal;
        std::string originLabel = "Origin";
        nameVal.SetString(originLabel.c_str());
        a_movie->SetVariable((basePath + ".nameText.text").c_str(), nameVal);
        contVal.SetString(masterName.c_str());
        a_movie->SetVariable((basePath + ".containerText.text").c_str(), contVal);

        // Count total items in master container
        int masterItemCount = 0;
        if (masterRef) {
            auto inv = masterRef->GetInventory();
            for (auto& [item, data] : inv) {
                if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                masterItemCount += data.first;
            }
        }
        std::string originCount = std::to_string(masterItemCount);
        cntVal.SetString(originCount.c_str());
        a_movie->SetVariable((basePath + ".countText.text").c_str(), cntVal);
    }

    void Update(RE::GFxMovieView* a_movie) {
        if (!s_flashActive) return;
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_flashStart).count();
        if (elapsed >= MenuLayout::COUNT_FLASH_DURATION) {
            s_flashActive = false;
            SetCount(a_movie, s_lastCount, false);
        }
    }

    void UpdateCount(RE::GFxMovieView* a_movie, int a_currentCount, int a_predictedCount) {
        if (!a_movie) return;

        std::string basePath = CLIP_PATH;
        if (a_predictedCount >= 0 && a_predictedCount != a_currentCount) {
            std::string currentStr = std::to_string(a_currentCount);
            std::string arrowStr = " > ";
            std::string predictedStr = std::to_string(a_predictedCount);
            std::string fullStr = currentStr + arrowStr + predictedStr;

            RE::GFxValue cntVal;
            cntVal.SetString(fullStr.c_str());
            a_movie->SetVariable((basePath + ".countText.text").c_str(), cntVal);
            ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".countText", 14, COLOR_COUNT);

            uint32_t predictedColor = (a_predictedCount > a_currentCount)
                ? COLOR_COUNT_INCREASE : COLOR_COUNT_DECREASE;

            RE::GFxValue tf;
            a_movie->GetVariable(&tf, (basePath + ".countText").c_str());
            if (!tf.IsUndefined()) {
                RE::GFxValue deltaFmt;
                a_movie->CreateObject(&deltaFmt, "TextFormat");
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
            std::string cntStr = std::to_string(a_currentCount);
            RE::GFxValue cntVal;
            cntVal.SetString(cntStr.c_str());
            a_movie->SetVariable((basePath + ".countText.text").c_str(), cntVal);
            ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".countText", 14, COLOR_COUNT);
        }
    }

    void SetCountFlash(RE::GFxMovieView* a_movie, bool a_flash) {
        if (!a_movie) return;
        std::string basePath = CLIP_PATH;
        uint32_t color = a_flash ? COLOR_COUNT_FLASH : COLOR_COUNT;
        ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".countText", 14, color);
    }

    void SetCount(RE::GFxMovieView* a_movie, int a_count, bool a_flash) {
        if (!a_movie) return;
        s_lastCount = a_count;
        if (a_flash) {
            s_flashActive = true;
            s_flashStart = std::chrono::steady_clock::now();
        }
        std::string basePath = CLIP_PATH;
        std::string countStr = std::to_string(a_count);
        RE::GFxValue cntVal;
        cntVal.SetString(countStr.c_str());
        a_movie->SetVariable((basePath + ".countText.text").c_str(), cntVal);
        uint32_t color = a_flash ? COLOR_COUNT_FLASH : COLOR_COUNT;
        ScaleformUtil::SetTextFieldFormat(a_movie, basePath + ".countText", 14, color);
    }

    void Destroy() {
        s_clip = RE::GFxValue();
        s_flashActive = false;
    }
}
