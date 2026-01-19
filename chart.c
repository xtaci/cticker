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
 * @file chart.c
 * @brief Chart data management and input handling.
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>
#if defined(__has_include)
#  if __has_include(<ncursesw/ncurses.h>)
#    include <ncursesw/ncurses.h>
#  elif __has_include(<ncurses.h>)
#    include <ncurses.h>
#  else
#    error "ncurses headers not found"
#  endif
#else
#  include <ncurses.h>
#endif
#ifndef BUTTON4_PRESSED
#define BUTTON4_PRESSED 0
#endif

#ifndef BUTTON5_PRESSED
#define BUTTON5_PRESSED 0
#endif
#include "chart.h"

/*
 * Chart module notes:
 * - Owns only temporary chart buffers passed from main.
 * - Reads shared ticker data under the provided mutex.
 * - Keeps UI calls outside of critical sections.
 */

// Fetch a fresh candle array and swap it into the caller-owned buffer.
static int chart_reload_data(const char symbol[static 1], Period period,
                             PricePoint *points[static 1], int count[static 1]) {
    PricePoint *new_points = NULL;
    int new_count = 0;
    int rc = fetch_historical_data(symbol, period, &new_points, &new_count);
    if (rc == 0) {
        if (*points) {
            free(*points);
        }
        *points = new_points;
        *count = new_count;
    } else if (new_points) {
        free(new_points);
    }
    return rc;
}

// Release chart buffers and reset the UI viewport for chart mode.
static void chart_reset_state(PricePoint *chart_points[static 1],
                              int chart_count[static 1],
                              int chart_cursor_idx[static 1]) {
    if (*chart_points) {
        free(*chart_points);
        *chart_points = NULL;
    }
    *chart_count = 0;
    *chart_cursor_idx = -1;
    ui_chart_reset_viewport();
}

// Normalize cursor index into the current candle range.
static void chart_clamp_cursor(const int chart_count[static 1],
                               int chart_cursor_idx[static 1]) {
    if (*chart_count <= 0) {
        *chart_cursor_idx = -1;
        return;
    }

    if (*chart_cursor_idx >= *chart_count) {
        *chart_cursor_idx = *chart_count - 1;
    }
    if (*chart_cursor_idx < 0) {
        *chart_cursor_idx = *chart_count - 1;
    }
}

// Restore cursor based on a candle timestamp, used after refresh.
static int chart_restore_cursor_by_timestamp(const PricePoint *points,
                                             int count,
                                             uint64_t timestamp) {
    if (!points || count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (points[i].timestamp == timestamp) {
            return i;
        }
    }
    return -1;
}

// Move chart period forward/backward and reload data.
static void chart_change_period(const ChartContext *ctx,
                                int step,
                                char chart_symbol[static 1],
                                Period current_period[static 1],
                                PricePoint *chart_points[static 1],
                                int chart_count[static 1],
                                int chart_cursor_idx[static 1]) {
    (void)ctx;
    int old_period = *current_period;
    int next = (int)(*current_period) + step;
    if (next < 0) {
        next = PERIOD_COUNT - 1;
    } else if (next >= PERIOD_COUNT) {
        next = 0;
    }
    *current_period = (Period)next;
    if (chart_reload_data(chart_symbol, *current_period, chart_points, chart_count) == 0) {
        chart_clamp_cursor(chart_count, chart_cursor_idx);
    } else {
        *current_period = (Period)old_period;
        beep();
    }
}

// Resolve the selected symbol and fetch chart candles for it.
bool chart_open(const ChartContext *ctx,
                int symbol_index,
                Period current_period,
                PricePoint *chart_points[static 1],
                int chart_count[static 1],
                char chart_symbol[static 1],
                int chart_cursor_idx[static 1],
                int chart_symbol_index[static 1]) {
    *chart_symbol_index = -1;
    if (!ctx || !ctx->ticker_count || !ctx->global_tickers || !ctx->data_mutex) {
        beep();
        return false;
    }
    if (*ctx->ticker_count <= 0 || symbol_index < 0 || symbol_index >= *ctx->ticker_count) {
        beep();
        return false;
    }

    pthread_mutex_lock(ctx->data_mutex);
    snprintf(chart_symbol, MAX_SYMBOL_LEN, "%s", ctx->global_tickers[symbol_index].symbol);
    pthread_mutex_unlock(ctx->data_mutex);
    *chart_symbol_index = symbol_index;

    if (chart_reload_data(chart_symbol, current_period, chart_points, chart_count) == 0) {
        *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
        return true;
    }

    beep();
    return false;
}

// Exit chart mode and release buffers.
void chart_close(bool show_chart[static 1],
                 PricePoint *chart_points[static 1],
                 int chart_count[static 1],
                 int chart_cursor_idx[static 1],
                 char chart_symbol[static 1],
                 int chart_symbol_index[static 1]) {
    *show_chart = false;
    chart_symbol[0] = '\0';
    *chart_symbol_index = -1;
    chart_reset_state(chart_points, chart_count, chart_cursor_idx);
}

// Update the latest candle to reflect live ticker price.
void chart_apply_live_price(const ChartContext *ctx,
                            const char symbol[static 1],
                            PricePoint points[static 1],
                            int chart_count,
                            int chart_symbol_index) {
    if (!ctx || !symbol[0] || chart_count <= 0) {
        return;
    }

    TickerData latest = {0};
    bool found = false;

    pthread_mutex_lock(ctx->data_mutex);
    if (ctx->global_tickers) {
        if (chart_symbol_index >= 0 && chart_symbol_index < *ctx->ticker_count) {
            latest = ctx->global_tickers[chart_symbol_index];
            if (strncmp(latest.symbol, symbol, MAX_SYMBOL_LEN) == 0) {
                found = true;
            }
        }
        if (!found) {
            for (int i = 0; i < *ctx->ticker_count; ++i) {
                if (strncmp(ctx->global_tickers[i].symbol, symbol, MAX_SYMBOL_LEN) == 0) {
                    latest = ctx->global_tickers[i];
                    found = true;
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(ctx->data_mutex);

    if (!found) {
        return;
    }

    double current_price = latest.price;
    if (current_price <= 0.0) {
        return;
    }

    PricePoint *last = &points[chart_count - 1];
    if (current_price > last->high) {
        last->high = current_price;
        last->high_text[0] = '\0';
    }
    if (last->low == 0.0 || current_price < last->low) {
        last->low = current_price;
        last->low_text[0] = '\0';
    }
    last->close = current_price;
    last->close_text[0] = '\0';
}

// Refresh candles when the last candle has closed, preserving selection.
void chart_refresh_if_expired(const ChartContext *ctx,
                              char chart_symbol[static 1],
                              Period current_period,
                              PricePoint *chart_points[static 1],
                              int chart_count[static 1],
                              int chart_cursor_idx[static 1]) {
    (void)ctx;
    if (!chart_symbol[0] || !*chart_points || *chart_count <= 0) {
        return;
    }

    PricePoint *points = *chart_points;
    time_t now = time(NULL);
    PricePoint *last = &points[*chart_count - 1];
    if (now < (time_t)last->close_time) {
        return;
    }

    uint64_t retained_ts = 0;
    bool retain_selection = (*chart_cursor_idx >= 0 && *chart_cursor_idx < *chart_count);
    bool was_latest = false;
    if (retain_selection) {
        retained_ts = points[*chart_cursor_idx].timestamp;
        was_latest = (*chart_cursor_idx == *chart_count - 1);
    }

    if (chart_reload_data(chart_symbol, current_period, chart_points, chart_count) != 0) {
        return;
    }

    if (!*chart_points || *chart_count <= 0) {
        *chart_cursor_idx = -1;
        return;
    }

    if (!retain_selection) {
        *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
        return;
    }

    if (was_latest) {
        *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
        return;
    }

    int restored_idx = chart_restore_cursor_by_timestamp(*chart_points,
                                                         *chart_count,
                                                         retained_ts);
    if (restored_idx >= 0) {
        *chart_cursor_idx = restored_idx;
        return;
    }

    *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
}

// Force a reload (manual refresh), optionally follow latest candle.
void chart_force_refresh(const ChartContext *ctx,
                         char chart_symbol[static 1],
                         Period current_period,
                         PricePoint *chart_points[static 1],
                         int chart_count[static 1],
                         int chart_cursor_idx[static 1],
                         bool follow_latest) {
    (void)ctx;
    if (!chart_symbol[0]) {
        return;
    }

    uint64_t retained_ts = 0;
    bool retain_selection = (*chart_cursor_idx >= 0 && *chart_cursor_idx < *chart_count);
    if (retain_selection) {
        retained_ts = (*chart_points)[*chart_cursor_idx].timestamp;
    }

    if (chart_reload_data(chart_symbol, current_period, chart_points, chart_count) != 0) {
        beep();
        return;
    }

    if (!*chart_points || *chart_count <= 0) {
        *chart_cursor_idx = -1;
        return;
    }

    if (follow_latest) {
        *chart_cursor_idx = *chart_count - 1;
        return;
    }

    if (!retain_selection) {
        *chart_cursor_idx = *chart_count - 1;
        return;
    }

    int restored_idx = chart_restore_cursor_by_timestamp(*chart_points,
                                                         *chart_count,
                                                         retained_ts);
    if (restored_idx >= 0) {
        *chart_cursor_idx = restored_idx;
        return;
    }

    *chart_cursor_idx = *chart_count - 1;
}

// Handle keyboard input while in chart mode.
void chart_handle_input(int ch,
                        const ChartContext *ctx,
                        char chart_symbol[static 1],
                        Period current_period[static 1],
                        PricePoint *chart_points[static 1],
                        int chart_count[static 1],
                        int chart_cursor_idx[static 1],
                        bool show_chart[static 1],
                        bool follow_latest[static 1],
                        int chart_symbol_index[static 1]) {
    switch (ch) {
        case KEY_UP:
            chart_change_period(ctx, -1, chart_symbol, current_period, chart_points,
                                chart_count, chart_cursor_idx);
            break;
        case KEY_DOWN:
            chart_change_period(ctx, 1, chart_symbol, current_period, chart_points,
                                chart_count, chart_cursor_idx);
            break;
        case KEY_LEFT:
            if (*chart_cursor_idx > 0) {
                (*chart_cursor_idx)--;
                chart_clamp_cursor(chart_count, chart_cursor_idx);
                *follow_latest = false;
            }
            break;
        case KEY_RIGHT:
            if (*chart_cursor_idx >= 0 && *chart_cursor_idx < *chart_count - 1) {
                (*chart_cursor_idx)++;
                chart_clamp_cursor(chart_count, chart_cursor_idx);
                *follow_latest = false;
            }
            break;
        case 'f':
        case 'F':
            *follow_latest = !*follow_latest;
            if (*follow_latest && *chart_count > 0) {
                *chart_cursor_idx = *chart_count - 1;
            }
            break;
        case 'r':
        case 'R':
            chart_force_refresh(ctx, chart_symbol, *current_period, chart_points,
                                chart_count, chart_cursor_idx, *follow_latest);
            break;
        case 'q':
        case 'Q':
        case 27:  // ESC
            chart_close(show_chart, chart_points, chart_count, chart_cursor_idx,
                        chart_symbol, chart_symbol_index);
            *follow_latest = true;
            break;
        default:
            break;
    }
}

// Handle mouse input while in chart mode.
void chart_handle_mouse(const ChartContext *ctx,
                        const MEVENT ev,
                        char chart_symbol[static 1],
                        Period current_period[static 1],
                        PricePoint *chart_points[static 1],
                        int chart_count[static 1],
                        int chart_cursor_idx[static 1],
                        bool show_chart[static 1],
                        bool follow_latest[static 1],
                        int chart_symbol_index[static 1]) {
    if (ev.bstate & (BUTTON3_PRESSED | BUTTON3_RELEASED | BUTTON3_CLICKED)) {
        chart_handle_input(27, ctx, chart_symbol, current_period, chart_points,
                           chart_count, chart_cursor_idx, show_chart, follow_latest,
                           chart_symbol_index);
        return;
    }
    if (ev.bstate & BUTTON4_PRESSED) {
        chart_change_period(ctx, -1, chart_symbol, current_period, chart_points,
                            chart_count, chart_cursor_idx);
        return;
    }
    if (ev.bstate & BUTTON5_PRESSED) {
        chart_change_period(ctx, 1, chart_symbol, current_period, chart_points,
                            chart_count, chart_cursor_idx);
        return;
    }
    if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED)) {
        int idx = ui_chart_hit_test_index(ev.x, *chart_count);
        if (idx >= 0) {
            *chart_cursor_idx = idx;
            chart_clamp_cursor(chart_count, chart_cursor_idx);
            *follow_latest = false;
        }
    }
}
