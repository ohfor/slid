#pragma once

namespace Feedback {
    // High-level feedback for specific actions
    void OnSetMaster(RE::TESObjectREFR* a_container);
    void OnTagContainer(RE::TESObjectREFR* a_container);
    void OnUntagContainer(RE::TESObjectREFR* a_container);
    void OnDismantleNetwork(RE::TESObjectREFR* a_container);
    void OnDetectContainers();
    void OnAutoDistribute();
    void OnWhoosh();
    void OnSort();
    void OnSetSellContainer(RE::TESObjectREFR* a_container);
    void OnClearSellContainer(RE::TESObjectREFR* a_container);
    void OnError();
}
