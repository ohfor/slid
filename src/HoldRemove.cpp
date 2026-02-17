#include "HoldRemove.h"

namespace HoldRemove {

    // Module-static state
    static RE::GFxMovieView* s_movie = nullptr;
    static RE::GFxValue*     s_rowClip = nullptr;
    static double            s_rowW = 0.0;
    static double            s_rowH = 0.0;
    static bool              s_active = false;
    static int               s_dataIndex = -1;
    static Callback          s_callback;
    static std::chrono::steady_clock::time_point s_startTime;

    // Hold duration before triggering callback
    static constexpr float HOLD_DURATION = 1.0f;

    // --- Drawing helpers ---

    static void DrawProgress(float a_ratio) {
        if (!s_rowClip || s_rowClip->IsUndefined()) return;

        RE::GFxValue fillClip;
        s_rowClip->GetMember("_removeFill", &fillClip);
        if (fillClip.IsUndefined()) {
            RE::GFxValue args[2];
            args[0].SetString("_removeFill");
            args[1].SetNumber(5.0);  // between bg(1) and text fields(10+)
            s_rowClip->Invoke("createEmptyMovieClip", &fillClip, args, 2);
        }
        if (fillClip.IsUndefined()) return;

        fillClip.Invoke("clear", nullptr, nullptr, 0);

        double fillW = s_rowW * static_cast<double>(a_ratio);
        if (fillW < 1.0) return;

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(0x884444));  // reddish fill
        fillArgs[1].SetNumber(80.0);
        fillClip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        fillClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(fillW);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(s_rowH);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);

        fillClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    // --- Public API ---

    void Start(RE::GFxMovieView* a_movie, int a_dataIndex,
               RE::GFxValue* a_rowClip,
               double a_rowW, double a_rowH,
               Callback a_callback) {
        s_movie = a_movie;
        s_dataIndex = a_dataIndex;
        s_rowClip = a_rowClip;
        s_rowW = a_rowW;
        s_rowH = a_rowH;
        s_callback = std::move(a_callback);
        s_active = true;
        s_startTime = std::chrono::steady_clock::now();

        DrawProgress(0.0f);
        logger::debug("Remove: hold started on filter index {}", s_dataIndex);
    }

    bool Update() {
        if (!s_active) return false;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - s_startTime).count();
        float ratio = std::clamp(elapsed / HOLD_DURATION, 0.0f, 1.0f);

        DrawProgress(ratio);

        if (ratio >= 1.0f) {
            s_active = false;
            // Clear the fill visual before invoking callback
            if (s_rowClip && !s_rowClip->IsUndefined()) {
                RE::GFxValue fillClip;
                s_rowClip->GetMember("_removeFill", &fillClip);
                if (!fillClip.IsUndefined()) {
                    fillClip.Invoke("clear", nullptr, nullptr, 0);
                }
            }
            // Invoke callback directly — caller decides what dialog to show
            int idx = s_dataIndex;
            auto cb = std::move(s_callback);
            s_callback = nullptr;
            if (cb) cb(idx);
            return false;  // hold complete
        }

        return true;  // still holding
    }

    void Cancel() {
        if (!s_active) return;
        s_active = false;

        // Clear fill on the row
        if (s_rowClip && !s_rowClip->IsUndefined()) {
            RE::GFxValue fillClip;
            s_rowClip->GetMember("_removeFill", &fillClip);
            if (!fillClip.IsUndefined()) {
                fillClip.Invoke("clear", nullptr, nullptr, 0);
            }
        }

        logger::debug("Remove: hold cancelled");
    }

    bool IsHolding() {
        return s_active;
    }

    int GetHoldIndex() {
        return s_dataIndex;
    }

    void ClearHoldIndex() {
        s_dataIndex = -1;
    }

    void TriggerImmediate(int a_dataIndex, Callback a_callback) {
        s_dataIndex = a_dataIndex;
        s_active = false;  // no hold animation

        // Invoke callback directly
        if (a_callback) a_callback(a_dataIndex);
    }

    void ClearProgress(RE::GFxValue* a_rows, int a_count) {
        for (int i = 0; i < a_count; i++) {
            if (a_rows[i].IsUndefined()) continue;
            RE::GFxValue fillClip;
            a_rows[i].GetMember("_removeFill", &fillClip);
            if (!fillClip.IsUndefined()) {
                fillClip.Invoke("clear", nullptr, nullptr, 0);
            }
        }
    }

    void Destroy() {
        s_movie = nullptr;
        s_rowClip = nullptr;
        s_active = false;
        s_dataIndex = -1;
        s_callback = nullptr;
    }
}
