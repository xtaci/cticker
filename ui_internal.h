#ifndef CTICKER_UI_INTERNAL_H
#define CTICKER_UI_INTERNAL_H

/**
 * @file ui_internal.h
 * @brief Shared declarations for the UI layer (core, priceboard, chart, format).
 *
 * Only UI .c files should include this header.  Non-UI modules
 * interact via the public APIs in cticker.h, chart.h, priceboard.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ncurses_compat.h"
#include "cticker.h"

// ── Color palette identifiers ────────────────────────────────────────

/** @brief ncurses color-pair IDs used by the UI. */
typedef enum {
    COLOR_PAIR_GREEN = 1,
    COLOR_PAIR_RED,
    COLOR_PAIR_HEADER,
    COLOR_PAIR_SELECTED,
    COLOR_PAIR_GREEN_BG,
    COLOR_PAIR_RED_BG,
    COLOR_PAIR_GREEN_SELECTED,
    COLOR_PAIR_RED_SELECTED,
    COLOR_PAIR_SYMBOL,
    COLOR_PAIR_SYMBOL_SELECTED,
    COLOR_PAIR_TITLE_BAR,
    COLOR_PAIR_FOOTER_BAR,
    COLOR_PAIR_STATUS_PANEL,
    COLOR_PAIR_STATUS_PANEL_FETCHING,
    COLOR_PAIR_STATUS_PANEL_ALERT,
    COLOR_PAIR_INFO_OPEN,
    COLOR_PAIR_INFO_HIGH,
    COLOR_PAIR_INFO_LOW,
    COLOR_PAIR_INFO_CLOSE,
    COLOR_PAIR_INFO_CURRENT,
} ColorPairId;

// ── Shared ncurses state ─────────────────────────────────────────────

/** @brief Root ncurses window for all rendering. */
extern WINDOW *main_win;
/** @brief Whether the terminal supports colors. */
extern bool colors_available;

// ── Price board viewport state ───────────────────────────────────────

extern double last_prices[MAX_SYMBOLS];
extern int last_visible_count;
extern int price_board_view_start_y;
extern int price_board_view_rows;
extern int price_board_scroll_offset;

// ── Chart viewport state (used for mouse hit-testing) ────────────────

extern int chart_view_start_x;
extern int chart_view_visible_points;
extern int chart_view_start_idx;
extern int chart_view_stride;
extern int chart_view_total_points;

// ── Shared UI helpers ────────────────────────────────────────────────
void reset_price_history(void);
void reset_chart_view_state(void);
void draw_footer_bar(const char *text);

void ui_format_number(char *buf, size_t size, double num);
void ui_trim_trailing_zeros(char *buf);
void ui_format_axis_price(char *buf, size_t size, double num, double range);
void ui_format_number_with_commas(char *buf, size_t size, double num);
void ui_format_integer_with_commas(char *buf, size_t size, long long value);
const char *ui_period_label(Period period);

#endif
