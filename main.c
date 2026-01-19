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
 * @file main.c
 * @brief Application entry point + orchestration.
 *
 * High-level architecture:
 * - A background thread periodically fetches ticker data into a shared array.
 * - The main thread owns the UI loop (ncurses) and handles user input.
 * - A mutex protects shared data access between the fetch thread and UI.
 *
 * The UI stays responsive by:
 * - drawing from a local copy of the ticker array (so we don't hold the lock
 *   while doing ncurses calls)
 * - using timeouts in the UI layer (see init_ui())
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
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
#include "cticker.h"
#include "chart.h"
#include "priceboard.h"
#include "runtime.h"

/**
 * @brief Main UI loop dispatching draw/input for board vs chart.
 */
static void run_event_loop(RuntimeContext *runtime) {
    /* UI loop for main board and chart mode. */
    PricePoint *chart_points = NULL;
    Period current_period = PERIOD_1MIN;
    bool show_chart = false;
    int selected = 0;
    char chart_symbol[MAX_SYMBOL_LEN] = {0};
    int chart_count = 0;
    int chart_cursor_idx = -1;
    bool chart_follow_latest = true;
    int chart_symbol_index = -1;
    bool exit_requested = false;

    PriceboardContext priceboard_ctx = {
        .data_mutex = &runtime->data_mutex,
        .global_tickers = runtime->global_tickers,
        .ticker_snapshot = runtime->ticker_snapshot,
        .ticker_snapshot_order = runtime->ticker_snapshot_order,
        .ticker_count = &runtime->ticker_count,
    };

    ChartContext chart_ctx = {
        .data_mutex = &runtime->data_mutex,
        .global_tickers = runtime->global_tickers,
        .ticker_count = &runtime->ticker_count,
    };

    while (runtime_is_running()) {
        /* Render phase. */
        if (show_chart) {
            bool follow_latest = chart_follow_latest ||
                                 (chart_cursor_idx >= 0 && chart_cursor_idx == chart_count - 1);
            chart_refresh_if_expired(&chart_ctx, chart_symbol, current_period,
                                     &chart_points, &chart_count, &chart_cursor_idx);
            chart_apply_live_price(&chart_ctx, chart_symbol, chart_points, chart_count,
                                   chart_symbol_index);
            if (follow_latest && chart_count > 0) {
                chart_cursor_idx = chart_count - 1;
            }
            draw_chart(chart_symbol, chart_count, chart_points, current_period,
                       chart_cursor_idx);
        } else {
            priceboard_clamp_selected(&priceboard_ctx, &selected);
            priceboard_render(&priceboard_ctx, selected);
        }

        /* Input phase. */
        int ch = handle_input();
        if (ch == ERR) {
            continue;
        }

        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (show_chart) {
                    chart_handle_mouse(&chart_ctx, ev, chart_symbol, &current_period,
                                       &chart_points, &chart_count, &chart_cursor_idx,
                                       &show_chart, &chart_follow_latest,
                                       &chart_symbol_index);
                } else {
                    priceboard_handle_mouse(&priceboard_ctx, ev, &selected, current_period,
                                            &show_chart, &chart_points, &chart_count,
                                            chart_symbol, &chart_cursor_idx,
                                            &chart_symbol_index, &chart_ctx);
                }
            }
            continue;
        }

        if (show_chart) {
            chart_handle_input(ch, &chart_ctx, chart_symbol, &current_period,
                               &chart_points, &chart_count, &chart_cursor_idx,
                               &show_chart, &chart_follow_latest,
                               &chart_symbol_index);
        } else {
            exit_requested = priceboard_handle_input(&priceboard_ctx, ch, &selected,
                                                     current_period, &show_chart,
                                                     &chart_points, &chart_count,
                                                     chart_symbol, &chart_cursor_idx,
                                                     &chart_symbol_index, &chart_ctx);
            if (exit_requested) {
                runtime_request_shutdown();
            }
        }
    }

    if (chart_points) {
        free(chart_points);
    }
}

/**
 * @brief Program entry point.
 *
 * Sets up config, starts the worker thread, then runs the UI state machine.
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    RuntimeContext runtime = {0};

    runtime_setup_signal_handlers();

    if (runtime_init(&runtime) != 0) {
        return 1;
    }

    /*
     * Main loop.
     * Two UI modes:
     *  - Price board: select symbol and open chart (Enter)
     *  - Chart view : left/right candle cursor, up/down change interval
     */
    run_event_loop(&runtime);

    runtime_shutdown(&runtime);
    return 0;
}
