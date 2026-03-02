#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace ButtonColors {
    constexpr uint32_t NORMAL  = 0x1A1A1A;
    constexpr uint32_t SELECT  = 0x444444;
    constexpr uint32_t HOVER   = 0x2A2A2A;
    constexpr uint32_t FLASH   = 0x666666;
    constexpr uint32_t LABEL   = 0xCCCCCC;
    constexpr int ALPHA_NORMAL = 70;
    constexpr int ALPHA_SELECT = 90;
    constexpr int ALPHA_HOVER  = 80;
    constexpr int ALPHA_FLASH  = 95;
    constexpr double HEIGHT    = 28.0;
    constexpr double GAP       = 10.0;
    constexpr int FONT_SIZE    = 13;
    constexpr float FLASH_DURATION = 0.12f;

    // Standard hold colors
    constexpr uint32_t HOLD_BLUE  = 0x446688;  // Default/Defaults
    constexpr uint32_t HOLD_RED   = 0x884444;  // Clear/destructive
    constexpr uint32_t HOLD_GREEN = 0x448844;  // Whoosh/configure
}

struct ButtonDef {
    std::string label;              // translated display text
    double width = 100.0;          // button width (all same height)
    std::string guideText;          // shown when focused (empty = no guide)

    // Hold-to-confirm (optional)
    bool holdable = false;
    uint32_t holdColor = 0;         // fill color during hold
    int holdAlpha = 80;
    float holdDuration = 1.0f;      // seconds
    float holdDeadZone = 0.0f;      // seconds before fill starts (Whoosh tap/hold distinction)
};

class ButtonBar {
public:
    void Init(RE::GFxMovieView* a_movie, const std::string& a_clipPrefix,
              int a_baseDepth, const std::vector<ButtonDef>& a_buttons,
              double a_centerX, double a_y);

    void Draw(int a_selected, int a_hovered);
    void Destroy();

    // Hold mechanics
    void StartHold(int a_index);
    void UpdateHold();
    void CancelHold();
    int  CompletedHold();       // returns index if hold just completed, -1 otherwise
    bool IsHolding() const;
    bool IsHoldPastDeadZone() const;  // true if past dead zone (hold started visually)

    // Flash (post-activation feedback)
    void Flash(int a_index);
    void UpdateFlash();
    bool IsFlashing() const;
    bool IsFlashing(int a_index) const;

    // Hit testing
    int  HitTest(float a_mx, float a_my) const;

    // Guide text for focused button
    const std::string& GetGuideText(int a_index) const;

    // Accessors
    int Count() const;
    double GetButtonX(int a_index) const;
    double GetButtonW(int a_index) const;
    double GetBarY() const;

private:
    RE::GFxMovieView* m_movie = nullptr;
    std::string m_prefix;
    int m_baseDepth = 0;
    std::vector<ButtonDef> m_buttons;
    double m_y = 0.0;
    std::vector<double> m_xPositions;

    // Hold state
    int m_holdIndex = -1;
    int m_holdCompleted = -1;  // set when hold finishes, consumed by CompletedHold()
    std::chrono::steady_clock::time_point m_holdStart;

    // Flash state
    int m_flashIndex = -1;
    std::chrono::steady_clock::time_point m_flashStart;

    void DrawButton(int a_index, uint32_t a_bgColor, int a_bgAlpha);
    void DrawHoldFill(int a_index, float a_ratio);
    void ClearHoldFill(int a_index);
    void RemoveClip(const std::string& a_name);

    static const std::string s_emptyGuide;
};
