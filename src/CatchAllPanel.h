#pragma once

#include "ContainerScanner.h"
#include "MenuLayout.h"
#include "ScaleformUtil.h"
#include <functional>
#include <string>

namespace CatchAllPanel {

    // --- Callbacks from orchestrator ---
    struct Callbacks {
        std::function<void()> commitToNetwork;
        std::function<void()> recalcPredictions;
        std::function<void()> hideMenu;
        std::function<void()> resetRepeat;
        std::function<void()> saveState;
    };

    // --- Lifecycle ---
    void Init(RE::GFxMovieView* a_movie, RE::FormID a_masterFormID, const Callbacks& a_callbacks);
    void Destroy();
    void Draw();
    void Update();  // count flash

    // --- Selection ---
    void Select();
    void Deselect();
    bool IsSelected();

    // --- Activation ---
    void Activate();         // open container dropdown
    void OpenLinkedContainer();

    // --- Hold-A ---
    void StartHoldA();
    void CancelHoldA();
    bool IsHoldingA();

    // --- Prediction / counts ---
    void SetPrediction(int a_count, bool a_isMaster);
    void ClearPrediction();
    void SetCount(int a_count, bool a_flash);
    void RefreshCount();

    // --- Data ---
    void SetCatchAll(const std::string& a_name, RE::FormID a_formID,
                     const std::string& a_location, int a_count);
    RE::FormID GetContainerFormID();
    const std::string& GetContainerName();
    bool HasLinkedContainer();  // has a container that isn't master

    // --- Guide text ---
    std::string GetGuideText();

    // --- Mouse ---
    enum class HitZone { kNone, kRow, kChestIcon };
    HitZone HitTest(float a_mx, float a_my, int& a_outIndex);
    void UpdateHover(float a_mx, float a_my);
    void ClearHover();
    bool IsHovered();
}
