#include "ButtonBar.h"
#include "ScaleformUtil.h"

const std::string ButtonBar::s_emptyGuide;

void ButtonBar::Init(RE::GFxMovieView* a_movie, const std::string& a_clipPrefix,
                     int a_baseDepth, const std::vector<ButtonDef>& a_buttons,
                     double a_centerX, double a_y) {
    m_movie = a_movie;
    m_prefix = a_clipPrefix;
    m_baseDepth = a_baseDepth;
    m_buttons = a_buttons;
    m_y = a_y;
    m_holdIndex = -1;
    m_holdCompleted = -1;
    m_flashIndex = -1;

    if (!m_movie) return;

    RE::GFxValue root;
    m_movie->GetVariable(&root, "_root");
    if (root.IsUndefined()) return;

    // Compute total width of all buttons + gaps
    int count = static_cast<int>(m_buttons.size());
    double btnTotalW = 0.0;
    for (const auto& b : m_buttons) btnTotalW += b.width;
    btnTotalW += ButtonColors::GAP * (count - 1);

    // Center within the given totalW around centerX
    double startX = a_centerX - btnTotalW / 2.0;

    m_xPositions.clear();
    m_xPositions.reserve(count);
    double x = startX;

    for (int i = 0; i < count; ++i) {
        m_xPositions.push_back(x);

        std::string clipName = m_prefix + std::to_string(i);
        double btnW = m_buttons[i].width;

        // Create container clip
        RE::GFxValue clip;
        RE::GFxValue args[2];
        args[0].SetString(clipName.c_str());
        args[1].SetNumber(static_cast<double>(m_baseDepth + i));
        root.Invoke("createEmptyMovieClip", &clip, args, 2);
        if (clip.IsUndefined()) {
            x += btnW + ButtonColors::GAP;
            continue;
        }

        RE::GFxValue posX, posY;
        posX.SetNumber(x);
        posY.SetNumber(m_y);
        clip.SetMember("_x", posX);
        clip.SetMember("_y", posY);

        // Background child clip
        RE::GFxValue bgClip;
        RE::GFxValue bgArgs[2];
        bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
        clip.Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);

        // Label text field
        RE::GFxValue tfArgs[6];
        tfArgs[0].SetString("_label"); tfArgs[1].SetNumber(10.0);
        tfArgs[2].SetNumber(0.0); tfArgs[3].SetNumber(4.0);
        tfArgs[4].SetNumber(btnW); tfArgs[5].SetNumber(ButtonColors::HEIGHT - 4.0);
        clip.Invoke("createTextField", nullptr, tfArgs, 6);

        std::string labelPath = "_root." + clipName + "._label";
        ScaleformUtil::SetTextFieldFormat(m_movie, labelPath, ButtonColors::FONT_SIZE, ButtonColors::LABEL);

        // Center-align
        RE::GFxValue tf;
        m_movie->GetVariable(&tf, labelPath.c_str());
        if (!tf.IsUndefined()) {
            RE::GFxValue alignFmt;
            m_movie->CreateObject(&alignFmt, "TextFormat");
            if (!alignFmt.IsUndefined()) {
                RE::GFxValue alignVal;
                alignVal.SetString("center");
                alignFmt.SetMember("align", alignVal);
                RE::GFxValue fmtArgs[1] = {alignFmt};
                tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
            }
        }

        RE::GFxValue textVal;
        textVal.SetString(m_buttons[i].label.c_str());
        m_movie->SetVariable((labelPath + ".text").c_str(), textVal);

        x += btnW + ButtonColors::GAP;
    }
}

void ButtonBar::Draw(int a_selected, int a_hovered) {
    int count = static_cast<int>(m_buttons.size());
    for (int i = 0; i < count; ++i) {
        if (m_flashIndex == i) continue;  // don't overwrite flash

        uint32_t color = ButtonColors::NORMAL;
        int alpha = ButtonColors::ALPHA_NORMAL;
        if (a_selected == i) {
            color = ButtonColors::SELECT;
            alpha = ButtonColors::ALPHA_SELECT;
        } else if (a_hovered == i) {
            color = ButtonColors::HOVER;
            alpha = ButtonColors::ALPHA_HOVER;
        }

        DrawButton(i, color, alpha);
    }
}

void ButtonBar::Destroy() {
    if (!m_movie) return;

    int count = static_cast<int>(m_buttons.size());
    for (int i = 0; i < count; ++i) {
        // Remove fill clip
        std::string fillName = m_prefix + std::to_string(i) + "_fill";
        RemoveClip(fillName);

        // Remove container clip
        std::string clipName = m_prefix + std::to_string(i);
        std::string clipPath = "_root." + clipName;
        RE::GFxValue clip;
        m_movie->GetVariable(&clip, clipPath.c_str());
        if (!clip.IsUndefined()) {
            clip.Invoke("removeMovieClip", nullptr, nullptr, 0);
        }
    }

    m_holdIndex = -1;
    m_holdCompleted = -1;
    m_flashIndex = -1;
    m_movie = nullptr;
}

// --- Hold mechanics ---

void ButtonBar::StartHold(int a_index) {
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return;
    if (!m_buttons[a_index].holdable) return;

    m_holdIndex = a_index;
    m_holdCompleted = -1;
    m_holdStart = std::chrono::steady_clock::now();
    DrawHoldFill(a_index, 0.0f);
}

void ButtonBar::UpdateHold() {
    if (m_holdIndex < 0) return;

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - m_holdStart).count();

    const auto& def = m_buttons[m_holdIndex];
    float deadZone = def.holdDeadZone;
    float duration = def.holdDuration;

    if (elapsed < deadZone) return;  // still in dead zone

    float ratio = std::clamp((elapsed - deadZone) / duration, 0.0f, 1.0f);
    DrawHoldFill(m_holdIndex, ratio);

    if (ratio >= 1.0f) {
        int completed = m_holdIndex;
        ClearHoldFill(m_holdIndex);
        m_holdIndex = -1;
        m_holdCompleted = completed;
    }
}

void ButtonBar::CancelHold() {
    if (m_holdIndex >= 0) {
        ClearHoldFill(m_holdIndex);
        m_holdIndex = -1;
    }
}

int ButtonBar::CompletedHold() {
    int result = m_holdCompleted;
    m_holdCompleted = -1;
    return result;
}

bool ButtonBar::IsHolding() const {
    return m_holdIndex >= 0;
}

bool ButtonBar::IsHoldPastDeadZone() const {
    if (m_holdIndex < 0) return false;
    float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - m_holdStart).count();
    return elapsed >= m_buttons[m_holdIndex].holdDeadZone;
}

// --- Flash ---

void ButtonBar::Flash(int a_index) {
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return;

    m_flashIndex = a_index;
    m_flashStart = std::chrono::steady_clock::now();
    DrawButton(a_index, ButtonColors::FLASH, ButtonColors::ALPHA_FLASH);
}

void ButtonBar::UpdateFlash() {
    if (m_flashIndex < 0) return;
    float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - m_flashStart).count();
    if (elapsed >= ButtonColors::FLASH_DURATION) {
        m_flashIndex = -1;
        // Caller should call Draw() to restore normal state
    }
}

bool ButtonBar::IsFlashing() const {
    return m_flashIndex >= 0;
}

bool ButtonBar::IsFlashing(int a_index) const {
    return m_flashIndex == a_index;
}

// --- Hit testing ---

int ButtonBar::HitTest(float a_mx, float a_my) const {
    if (a_my < m_y || a_my > m_y + ButtonColors::HEIGHT) return -1;
    int count = static_cast<int>(m_buttons.size());
    for (int i = 0; i < count; ++i) {
        if (a_mx >= m_xPositions[i] && a_mx <= m_xPositions[i] + m_buttons[i].width) return i;
    }
    return -1;
}

// --- Guide text ---

const std::string& ButtonBar::GetGuideText(int a_index) const {
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return s_emptyGuide;
    return m_buttons[a_index].guideText;
}

// --- Accessors ---

int ButtonBar::Count() const {
    return static_cast<int>(m_buttons.size());
}

double ButtonBar::GetButtonX(int a_index) const {
    if (a_index < 0 || a_index >= static_cast<int>(m_xPositions.size())) return 0.0;
    return m_xPositions[a_index];
}

double ButtonBar::GetButtonW(int a_index) const {
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return 0.0;
    return m_buttons[a_index].width;
}

double ButtonBar::GetBarY() const {
    return m_y;
}

// --- Internal drawing ---

void ButtonBar::DrawButton(int a_index, uint32_t a_bgColor, int a_bgAlpha) {
    if (!m_movie) return;
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return;

    std::string clipPath = "_root." + m_prefix + std::to_string(a_index);
    RE::GFxValue clip;
    m_movie->GetVariable(&clip, clipPath.c_str());
    if (clip.IsUndefined()) return;

    RE::GFxValue bgClip;
    clip.GetMember("_bg", &bgClip);
    if (bgClip.IsUndefined()) return;

    bgClip.Invoke("clear", nullptr, nullptr, 0);

    double w = m_buttons[a_index].width;

    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(a_bgColor));
    fillArgs[1].SetNumber(static_cast<double>(a_bgAlpha));
    bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

    RE::GFxValue pt[2];
    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
    bgClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(w);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(ButtonColors::HEIGHT);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[0].SetNumber(0.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(0.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);

    bgClip.Invoke("endFill", nullptr, nullptr, 0);
}

void ButtonBar::DrawHoldFill(int a_index, float a_ratio) {
    if (!m_movie) return;
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return;

    std::string clipPath = "_root." + m_prefix + std::to_string(a_index);
    RE::GFxValue clip;
    m_movie->GetVariable(&clip, clipPath.c_str());
    if (clip.IsUndefined()) return;

    RE::GFxValue fillClip;
    clip.GetMember("_fill", &fillClip);
    if (fillClip.IsUndefined()) {
        RE::GFxValue args[2];
        args[0].SetString("_fill");
        args[1].SetNumber(5.0);  // between bg(1) and label(10)
        clip.Invoke("createEmptyMovieClip", &fillClip, args, 2);
    }
    if (fillClip.IsUndefined()) return;

    fillClip.Invoke("clear", nullptr, nullptr, 0);

    const auto& def = m_buttons[a_index];
    double fillW = def.width * static_cast<double>(a_ratio);
    if (fillW < 1.0) return;

    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(def.holdColor));
    fillArgs[1].SetNumber(static_cast<double>(def.holdAlpha));
    fillClip.Invoke("beginFill", nullptr, fillArgs, 2);

    RE::GFxValue pt[2];
    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
    fillClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(fillW);
    fillClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(ButtonColors::HEIGHT);
    fillClip.Invoke("lineTo", nullptr, pt, 2);
    pt[0].SetNumber(0.0);
    fillClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(0.0);
    fillClip.Invoke("lineTo", nullptr, pt, 2);

    fillClip.Invoke("endFill", nullptr, nullptr, 0);
}

void ButtonBar::ClearHoldFill(int a_index) {
    if (!m_movie) return;
    if (a_index < 0 || a_index >= static_cast<int>(m_buttons.size())) return;

    std::string clipPath = "_root." + m_prefix + std::to_string(a_index);
    RE::GFxValue clip;
    m_movie->GetVariable(&clip, clipPath.c_str());
    if (clip.IsUndefined()) return;

    RE::GFxValue fillClip;
    clip.GetMember("_fill", &fillClip);
    if (!fillClip.IsUndefined()) {
        fillClip.Invoke("clear", nullptr, nullptr, 0);
    }
}

void ButtonBar::RemoveClip(const std::string& a_name) {
    if (!m_movie) return;
    RE::GFxValue root;
    m_movie->GetVariable(&root, "_root");
    if (root.IsUndefined()) return;
    RE::GFxValue existing;
    root.GetMember(a_name.c_str(), &existing);
    if (!existing.IsUndefined()) {
        existing.Invoke("removeMovieClip", nullptr, nullptr, 0);
    }
}
