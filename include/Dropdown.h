#pragma once

#include <functional>
#include <string>
#include <vector>

class Dropdown {
public:
    struct Entry {
        std::string id;           // caller-defined identifier
        std::string label;        // primary display text
        std::string sublabel;     // secondary text (location) — dimmer color, appended
        uint32_t    color = 0xCCCCCC;
        bool        enabled = true;  // false = visible but not selectable (dimmed)
        uint8_t     group = 0;    // visual separator drawn between different groups
    };

    struct Config {
        double width      = 400.0;
        double rowHeight  = 28.0;
        int    maxVisible = 8;
        std::string title;         // optional header (empty = none)
        int    preSelect  = -1;    // pre-highlight index on open (-1 = first enabled)
    };

    using Callback = std::function<void(bool confirmed, int index, const std::string& id)>;

    Dropdown() = default;

    // --- Closed state ---
    // Set what the dropdown displays when closed.
    // a_closedColor: 0 = use default linked/unlinked color, nonzero = override.
    void SetValue(const std::string& a_id, const std::string& a_label,
                  const std::string& a_sublabel = "",
                  uint32_t a_closedColor = 0);

    // Render the closed control (border box + value text + chevron) into the
    // parent clip's containerText area.  Called each frame by FilterRow/CatchAllPanel.
    void RenderClosed(RE::GFxMovieView* a_movie, RE::GFxValue& a_parentClip,
                      const std::string& a_parentPath,
                      double a_x, double a_y, double a_w, double a_h,
                      bool a_focused) const;

    // --- Open state ---
    void Open(RE::GFxMovieView* a_movie, double a_anchorX, double a_anchorY,
              const Config& a_config, std::vector<Entry> a_entries, Callback a_callback);
    bool IsOpen() const;

    // Navigation (only when open)
    void Prev();
    void Next();
    void Confirm();
    void Cancel();

    // Mouse (only when open)
    void OnMouseClick(float a_mx, float a_my);
    void OnScrollWheel(int a_direction);
    void UpdateHover(float a_mx, float a_my);
    void ClearHover();

    // Cleanup
    void Destroy();

    // --- Static: global open tracking ---
    static bool IsAnyOpen();
    static Dropdown* GetOpen();  // nullptr if none open

    // Value access
    const std::string& GetSelectedId() const;
    const std::string& GetSelectedLabel() const;

private:
    // Value state
    std::string m_selectedId;
    std::string m_selectedLabel;
    std::string m_selectedSublabel;
    uint32_t m_closedColorOverride = 0;  // nonzero = override text color in RenderClosed

    // Entries (populated before Open)
    std::vector<Entry> m_entries;
    Config m_config;

    // Open state
    bool m_open = false;
    Callback m_callback;
    int m_cursorIndex = 0;
    int m_scrollOffset = 0;
    int m_hoverIndex = -1;
    RE::GFxMovieView* m_movie = nullptr;

    // Anchor (passed to Open)
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // Popup geometry (computed on Open)
    double m_popupX = 0.0, m_popupY = 0.0, m_popupW = 0.0, m_popupH = 0.0;
    double m_rowAreaY = 0.0;
    double m_scrollTrackX = 0.0;
    int m_visibleCount = 0;

    static constexpr int MAX_ROW_SLOTS = 8;

    // Scaleform clips for popup
    RE::GFxValue m_rowClips[MAX_ROW_SLOTS];
    RE::GFxValue m_scrollThumbClip;

    // Internal helpers
    void DrawPopup();
    void PopulateRows();
    void DrawRowBackground(int a_visIndex, uint32_t a_color, int a_alpha);
    void UpdateScrollbar();
    void DestroyPopupVisuals();
    int FindNextEnabled(int a_from, int a_dir) const;

    // Global singleton tracking
    static Dropdown* s_openInstance;
};
