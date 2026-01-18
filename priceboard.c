/*
MIT License

Copyright (c) 2026 xtaci

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file priceboard.c
 * @brief Price board rendering, sorting, and input handling.
 */

#include <stdlib.h>
#include <string.h>
#include <ncursesw/ncurses.h>
#include "priceboard.h"

/*
 * Priceboard module notes:
 * - Owns only snapshot buffers provided by main.
 * - Sorting is stable against the original config order.
 * - All ncurses calls happen outside the shared data lock.
 */

typedef enum {
    SORT_DIR_DESC = 0,
    SORT_DIR_ASC,
} PriceboardSortDirection;

static PriceboardSortField current_sort_field = SORT_FIELD_DEFAULT;
static PriceboardSortDirection current_sort_direction = SORT_DIR_DESC;

// Clamp an integer into the inclusive range [low, high].
static inline int clamp_int(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

// Keep the selected index within the current ticker list bounds.
void priceboard_clamp_selected(const PriceboardContext *ctx, int selected[static 1]) {
    if (!ctx || !ctx->ticker_count) {
        *selected = 0;
        return;
    }
    int count = *ctx->ticker_count;
    if (count <= 0) {
        *selected = 0;
        return;
    }
    *selected = clamp_int(*selected, 0, count - 1);
}

// Map a visible row index back to the original config order.
int priceboard_resolve_symbol_index(const PriceboardContext *ctx, int display_index) {
    if (!ctx || !ctx->ticker_snapshot_order || !ctx->ticker_count) {
        return -1;
    }
    int count = *ctx->ticker_count;
    if (display_index < 0 || display_index >= count) {
        return -1;
    }
    return ctx->ticker_snapshot_order[display_index];
}

// Extract the numeric field used by the current sort.
static double priceboard_sort_value(const TickerData row[static 1],
                                    PriceboardSortField field) {
    switch (field) {
        case SORT_FIELD_PRICE:
            return row->price;
        case SORT_FIELD_CHANGE:
            return row->change_24h;
        default:
            return 0.0;
    }
}

// Compare two rows using the active sort rules (stable on original order).
static int priceboard_compare_rows(const PriceboardContext *ctx, int left, int right) {
    const TickerData *lhs = &ctx->ticker_snapshot[left];
    const TickerData *rhs = &ctx->ticker_snapshot[right];
    double lhs_val = priceboard_sort_value(lhs, current_sort_field);
    double rhs_val = priceboard_sort_value(rhs, current_sort_field);
    int result = 0;
    if (lhs_val < rhs_val) {
        result = -1;
    } else if (lhs_val > rhs_val) {
        result = 1;
    }
    if (result == 0) {
        int lhs_origin = ctx->ticker_snapshot_order[left];
        int rhs_origin = ctx->ticker_snapshot_order[right];
        if (lhs_origin < rhs_origin) {
            result = -1;
        } else if (lhs_origin > rhs_origin) {
            result = 1;
        }
    }
    if (current_sort_direction == SORT_DIR_DESC) {
        result = -result;
    }
    return result;
}

// Swap snapshot rows and their origin index map.
static void priceboard_swap_rows(const PriceboardContext *ctx, int left, int right) {
    if (left == right) {
        return;
    }
    TickerData tmp = ctx->ticker_snapshot[left];
    ctx->ticker_snapshot[left] = ctx->ticker_snapshot[right];
    ctx->ticker_snapshot[right] = tmp;

    int origin_tmp = ctx->ticker_snapshot_order[left];
    ctx->ticker_snapshot_order[left] = ctx->ticker_snapshot_order[right];
    ctx->ticker_snapshot_order[right] = origin_tmp;
}

// Apply insertion sort for small N (MAX_SYMBOLS <= 50).
static void priceboard_apply_sort(const PriceboardContext *ctx) {
    if (!ctx || !ctx->ticker_snapshot || !ctx->ticker_snapshot_order || !ctx->ticker_count) {
        return;
    }
    int count = *ctx->ticker_count;
    if (count <= 1) {
        return;
    }
    if (current_sort_field == SORT_FIELD_DEFAULT) {
        return;
    }
    for (int i = 1; i < count; ++i) {
        int j = i;
        while (j > 0 && priceboard_compare_rows(ctx, j - 1, j) > 0) {
            priceboard_swap_rows(ctx, j - 1, j);
            --j;
        }
    }
}

// Cycle between desc/asc/default order for a sort field.
void priceboard_cycle_sort(PriceboardSortField field) {
    if (field == SORT_FIELD_PRICE) {
        if (current_sort_field != SORT_FIELD_PRICE) {
            current_sort_field = SORT_FIELD_PRICE;
            current_sort_direction = SORT_DIR_DESC;
        } else if (current_sort_direction == SORT_DIR_DESC) {
            current_sort_direction = SORT_DIR_ASC;
        } else {
            current_sort_field = SORT_FIELD_DEFAULT;
            current_sort_direction = SORT_DIR_DESC;
        }
        return;
    }

    if (field == SORT_FIELD_CHANGE) {
        if (current_sort_field != SORT_FIELD_CHANGE) {
            current_sort_field = SORT_FIELD_CHANGE;
            current_sort_direction = SORT_DIR_DESC;
        } else if (current_sort_direction == SORT_DIR_DESC) {
            current_sort_direction = SORT_DIR_ASC;
        } else {
            current_sort_field = SORT_FIELD_DEFAULT;
            current_sort_direction = SORT_DIR_DESC;
        }
    }
}

// Indicate the next sort direction for the UI hint (F5/F6).
const char *priceboard_next_sort_hint(PriceboardSortField field) {
    switch (field) {
        case SORT_FIELD_PRICE:
            if (current_sort_field != SORT_FIELD_PRICE) {
                return "↓";
            }
            if (current_sort_direction == SORT_DIR_DESC) {
                return "↑";
            }
            return "=";
        case SORT_FIELD_CHANGE:
            if (current_sort_field != SORT_FIELD_CHANGE) {
                return "↓";
            }
            if (current_sort_direction == SORT_DIR_DESC) {
                return "↑";
            }
            return "=";
        default:
            return "=";
    }
}

// Build a snapshot, apply sorting, and render the price board.
void priceboard_render(const PriceboardContext *ctx, int selected) {
    if (!ctx || !ctx->ticker_snapshot || !ctx->ticker_count) {
        return;
    }

    pthread_mutex_lock(ctx->data_mutex);
    memcpy(ctx->ticker_snapshot, ctx->global_tickers,
           (size_t)(*ctx->ticker_count) * sizeof(TickerData));
    pthread_mutex_unlock(ctx->data_mutex);

    if (ctx->ticker_snapshot_order) {
        for (int i = 0; i < *ctx->ticker_count; ++i) {
            ctx->ticker_snapshot_order[i] = i;
        }
    }
    priceboard_apply_sort(ctx);

    const char *price_hint = priceboard_next_sort_hint(SORT_FIELD_PRICE);
    const char *change_hint = priceboard_next_sort_hint(SORT_FIELD_CHANGE);
    draw_main_screen(ctx->ticker_snapshot, *ctx->ticker_count, selected,
                     price_hint, change_hint);
}

// Handle keyboard input while price board is active.
bool priceboard_handle_input(const PriceboardContext *ctx,
                             int ch,
                             int selected[static 1],
                             Period current_period,
                             bool show_chart[static 1],
                             PricePoint *chart_points[static 1],
                             int chart_count[static 1],
                             char chart_symbol[static 1],
                             int chart_cursor_idx[static 1],
                             int chart_symbol_index[static 1],
                             const ChartContext *chart_ctx) {
    switch (ch) {
        case KEY_UP:
            (*selected)--;
            priceboard_clamp_selected(ctx, selected);
            return false;
        case KEY_DOWN:
            (*selected)++;
            priceboard_clamp_selected(ctx, selected);
            return false;
        case '\n':
        case '\r':
        case KEY_ENTER: {
            priceboard_clamp_selected(ctx, selected);
            int symbol_index = priceboard_resolve_symbol_index(ctx, *selected);
            if (chart_open(chart_ctx, symbol_index, current_period, chart_points,
                           chart_count, chart_symbol, chart_cursor_idx,
                           chart_symbol_index)) {
                *show_chart = true;
            }
            return false;
        }
        case 'q':
        case 'Q':
            return true;
        case KEY_F(5):
            priceboard_cycle_sort(SORT_FIELD_PRICE);
            return false;
        case KEY_F(6):
            priceboard_cycle_sort(SORT_FIELD_CHANGE);
            return false;
        default:
            return false;
    }
}

// Handle mouse input while price board is active.
void priceboard_handle_mouse(const PriceboardContext *ctx,
                             const MEVENT ev,
                             int selected[static 1],
                             Period current_period,
                             bool show_chart[static 1],
                             PricePoint *chart_points[static 1],
                             int chart_count[static 1],
                             char chart_symbol[static 1],
                             int chart_cursor_idx[static 1],
                             int chart_symbol_index[static 1],
                             const ChartContext *chart_ctx) {
    if (ev.bstate & BUTTON4_PRESSED) {
        (*selected)--;
        priceboard_clamp_selected(ctx, selected);
        return;
    }
    if (ev.bstate & BUTTON5_PRESSED) {
        (*selected)++;
        priceboard_clamp_selected(ctx, selected);
        return;
    }
    if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED)) {
        int count = ctx && ctx->ticker_count ? *ctx->ticker_count : 0;
        int row = ui_price_board_hit_test_row(ev.y, count);
        if (row < 0) {
            return;
        }

        *selected = row;
        priceboard_clamp_selected(ctx, selected);
        int symbol_index = priceboard_resolve_symbol_index(ctx, *selected);
        if (chart_open(chart_ctx, symbol_index, current_period, chart_points,
                       chart_count, chart_symbol, chart_cursor_idx,
                       chart_symbol_index)) {
            *show_chart = true;
        }
    }
}
