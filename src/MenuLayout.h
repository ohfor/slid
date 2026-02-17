#pragma once

#include <cstdint>

namespace MenuLayout {

    // Panel bounds
    constexpr double PANEL_X = 240.0;
    constexpr double PANEL_Y = 60.0;
    constexpr double PANEL_W = 800.0;
    constexpr double PANEL_H = 600.0;
    constexpr double PANEL_RIGHT = PANEL_X + PANEL_W;
    constexpr double PANEL_BOTTOM = PANEL_Y + PANEL_H;

    // Row layout
    constexpr int MAX_VISIBLE_ROWS = 9;
    constexpr int MAX_FIXED_ROWS = 0;
    constexpr double ROW_HEIGHT = 34.0;
    constexpr double BAND_H = 18.0;
    constexpr double FILTER_OFFSET = ROW_HEIGHT + BAND_H + 2.0;  // 54px
    constexpr double ROW_X = PANEL_X + 16.0;
    constexpr double ROW_Y = PANEL_Y + 72.0;
    constexpr double ROW_W = PANEL_W - 32.0;
    constexpr int TOTAL_ROW_SLOTS = MAX_VISIBLE_ROWS + MAX_FIXED_ROWS;

    // Catch-all position
    constexpr double CATCHALL_BAND_Y = ROW_Y + FILTER_OFFSET + (MAX_VISIBLE_ROWS + 1) * ROW_HEIGHT;
    constexpr double CATCHALL_ROW_Y  = CATCHALL_BAND_Y + BAND_H + 4.0;

    // Column offsets
    constexpr double COL_NUM_X = 0.0;
    constexpr double COL_NUM_W = 28.0;
    constexpr double COL_FILTER_X = 28.0;
    constexpr double COL_FILTER_W = 200.0;
    constexpr double COL_CONTAINER_X = 240.0;
    constexpr double COL_CONTAINER_W = 360.0;
    constexpr double COL_ITEMS_X = 620.0;
    constexpr double COL_ITEMS_W = 80.0;

    // Text colors
    constexpr uint32_t COLOR_TITLE       = 0xFFFFFF;
    constexpr uint32_t COLOR_HEADERS     = 0x888888;
    constexpr uint32_t COLOR_FILTER      = 0xDDDDDD;
    constexpr uint32_t COLOR_CONTAINER   = 0xAAAAAA;
    constexpr uint32_t COLOR_COUNT       = 0x999999;
    constexpr uint32_t COLOR_ROW_NUM     = 0x777777;
    constexpr uint32_t COLOR_CREDITS     = 0x555555;
    constexpr uint32_t COLOR_UNLINKED    = 0x666666;
    constexpr uint32_t COLOR_LOCATION    = 0x777777;
    constexpr uint32_t COLOR_MASTER_IND  = 0xBBBBBB;
    constexpr uint32_t COLOR_HINT        = 0x888888;

    // Row background colors
    constexpr uint32_t COLOR_ROW_NORMAL  = 0x111111;
    constexpr uint32_t COLOR_ROW_SELECT  = 0x444444;
    constexpr uint32_t COLOR_ROW_FIXED   = 0x222222;
    constexpr uint32_t COLOR_SEP_BAND    = 0x2A2A2A;
    constexpr uint32_t COLOR_ROW_LIFTED  = 0x554400;

    // Row background alpha
    constexpr int ALPHA_ROW_NORMAL  = 60;
    constexpr int ALPHA_ROW_SELECT  = 80;
    constexpr int ALPHA_ROW_FIXED   = 70;
    constexpr int ALPHA_ROW_LIFTED  = 100;

    // Mouse hover colors
    constexpr uint32_t COLOR_ROW_HOVER        = 0x2A2A2A;
    constexpr int      ALPHA_ROW_HOVER        = 70;
    constexpr uint32_t COLOR_PICKER_ROW_HOVER = 0x2A2A2A;
    constexpr int      ALPHA_PICKER_ROW_HOVER = 75;

    // Count flash
    constexpr float COUNT_FLASH_DURATION = 1.2f;
    constexpr uint32_t COLOR_COUNT_FLASH = 0xFFFF88;

    // Predictive count colors
    constexpr uint32_t COLOR_COUNT_INCREASE = 0x88CC88;
    constexpr uint32_t COLOR_COUNT_DECREASE = 0xCC8888;

    // Contest visualization colors
    constexpr uint32_t COLOR_CONTEST        = 0xDDAA44;  // amber for contested item count
    constexpr uint32_t COLOR_ROW_CONTEST    = 0x332200;  // dark amber tint for stealing rows
    constexpr int      ALPHA_ROW_CONTEST    = 70;

    // Chest icon constants
    constexpr double ICON_CHEST_SIZE = 14.0;
    constexpr double ICON_CHEST_X = 738.0;
    constexpr double ICON_CHEST_Y = 10.0;
    constexpr double ICON_CHEST_HIT_SIZE = 24.0;
    constexpr uint32_t COLOR_CHEST_ICON  = 0x666666;
    constexpr uint32_t COLOR_CHEST_HOVER = 0xAAAAAA;

    // Container picker popup constants
    constexpr int MAX_PICKER_VISIBLE = 8;
    constexpr double PICKER_W = 460.0;
    constexpr double PICKER_ROW_H = 30.0;
    constexpr double PICKER_PAD = 12.0;
    constexpr double PICKER_TITLE_H = 32.0;
    constexpr double PICKER_SEP_H = 1.0;
    constexpr uint32_t COLOR_PICKER_BG     = 0x0A0A0A;
    constexpr uint32_t COLOR_PICKER_BORDER = 0x666666;
    constexpr uint32_t COLOR_PICKER_TITLE  = 0xFFFFFF;
    constexpr uint32_t COLOR_PICKER_NONE   = 0xCC6666;
    constexpr uint32_t COLOR_PICKER_NAME   = 0xCCCCCC;
    constexpr uint32_t COLOR_PICKER_TAGGED = 0xDDCC88;
    constexpr uint32_t COLOR_KEEP          = 0x88CC88;  // green — items stay in master
    constexpr uint32_t COLOR_PASS          = 0xDDAA44;  // amber — filter skipped
    constexpr uint32_t COLOR_SELL          = 0x88BBDD;  // light blue — routed to sell container
    constexpr uint32_t COLOR_PICKER_LOC    = 0x777777;
    constexpr uint32_t COLOR_PICKER_ROW_NORMAL = 0x111111;
    constexpr uint32_t COLOR_PICKER_ROW_SELECT = 0x444444;
    constexpr int ALPHA_PICKER_BG = 95;
    constexpr int ALPHA_PICKER_ROW_NORMAL = 60;
    constexpr int ALPHA_PICKER_ROW_SELECT = 85;
    constexpr int ALPHA_DIM_OVERLAY = 50;

    // Hold-A to open container threshold
    constexpr float HOLD_OPEN_DURATION = 0.4f;
    constexpr float HOLD_VISUAL_DELAY  = 0.2f;

}  // namespace MenuLayout
