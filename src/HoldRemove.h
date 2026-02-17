#pragma once

#include "ScaleformUtil.h"

namespace HoldRemove {

    // Callback invoked when hold completes or right-click fires.
    // Parameter is the family index of the filter to potentially remove.
    using Callback = std::function<void(int familyIndex)>;

    // Begin hold-to-remove on a filter row.
    // a_movie: Scaleform movie for drawing. a_dataIndex: family index.
    // a_rowClip: the GFxValue MovieClip for the row (used for _removeFill child).
    // a_rowW/a_rowH: row dimensions for progress fill.
    // a_callback: invoked when hold completes (caller decides what dialog to show).
    void Start(RE::GFxMovieView* a_movie, int a_dataIndex,
               RE::GFxValue* a_rowClip,
               double a_rowW, double a_rowH,
               Callback a_callback);

    // Called each frame (kUpdate) while hold is active. Returns true if still holding.
    bool Update();

    // Cancel an in-progress hold (button released early, focus changed, etc.)
    void Cancel();

    // True if a hold animation is in progress (not yet at threshold).
    bool IsHolding();

    // Index of the filter being held for removal. -1 if none.
    int GetHoldIndex();

    // Clear the hold index (called after callback processes).
    void ClearHoldIndex();

    // Right-click instant removal: skip the hold, invoke callback immediately.
    void TriggerImmediate(int a_dataIndex, Callback a_callback);

    // Clear any _removeFill child clips from the given row slots.
    // Pass the row GFxValue array and count.
    void ClearProgress(RE::GFxValue* a_rows, int a_count);

    // Clean up on menu close.
    void Destroy();
}
