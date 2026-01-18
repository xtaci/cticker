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
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>
#include <ncursesw/ncurses.h>
#include "cticker.h"
#include "chart.h"
#include "priceboard.h"

#define REFRESH_INTERVAL 5  // Refresh every 5 seconds

static _Atomic bool running = true;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static TickerData *global_tickers = NULL;
static TickerData *ticker_snapshot = NULL;
static int *ticker_snapshot_order = NULL;
static int ticker_count = 0;

/*
 * Runtime state:
 * - global_tickers: shared latest data (protected by data_mutex).
 * - ticker_snapshot: local copy used for UI rendering.
 * - ticker_snapshot_order: index map for sorting without losing config order.
 */

/**
 * @brief Check whether the application should keep running.
 */
static inline bool is_running(void) {
    return atomic_load_explicit(&running, memory_order_relaxed);
}


/**
 * @brief Signal handler for clean exit.
 *
 * Keep it minimal: just flip a flag that is checked by both threads.
 */
static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        atomic_store_explicit(&running, false, memory_order_relaxed);
    }
}

/**
 * @brief Fetch all symbols into a scratch buffer.
 */
static void fetch_all_symbols(const Config config[static 1],
                              TickerData scratch[static 1],
                              bool updated[static 1],
                              bool had_failure[static 1]) {
    /*
     * Fetch all symbols without holding the UI lock.
     * The caller decides how to publish updated rows.
     */
    int symbol_count = config->symbol_count;
    *had_failure = false;
    for (int i = 0; i < symbol_count && is_running(); i++) {
        if (fetch_ticker_data(config->symbols[i], &scratch[i]) == 0) {
            updated[i] = true;
        } else {
            *had_failure = true;
        }
    }
}

/**
 * @brief Copy updated scratch entries into the shared ticker buffer.
 */
static void apply_updated_tickers(const TickerData scratch[static 1],
                                  const bool updated[static 1],
                                  int count) {
    /* Publish only updated rows under the mutex. */
    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < count; i++) {
        if (updated[i]) {
            global_tickers[i] = scratch[i];
        }
    }
    pthread_mutex_unlock(&data_mutex);
}

/**
 * @brief Background worker that refreshes the latest prices.
 *
 * Threading note: all writes to ::global_tickers happen under ::data_mutex so
 * the UI can take a consistent snapshot.
 */
static void* thread_data_fetch(void *arg) {
    /*
     * Background worker: periodically refresh latest prices and
     * publish results to the shared ticker array.
     */
    Config *config = (Config *)arg;
    int symbol_count = config->symbol_count;

    /* Fetch into a scratch buffer, then copy under lock to keep UI responsive. */
    TickerData *scratch = calloc(symbol_count, sizeof(TickerData));
    bool *updated = calloc(symbol_count, sizeof(bool));
    if (!scratch || !updated) {
        free(scratch);
        free(updated);
        ui_set_status_panel_state(STATUS_PANEL_NETWORK_ERROR);
        atomic_store_explicit(&running, false, memory_order_relaxed);
        return NULL;
    }
   
    /* Keep fetching until signaled to stop. */ 
    while (is_running()) {
        ui_set_status_panel_state(STATUS_PANEL_FETCHING);
        memset(updated, 0, symbol_count * sizeof(bool));
        bool had_failure = false;

        fetch_all_symbols(config, scratch, updated, &had_failure);
        apply_updated_tickers(scratch, updated, symbol_count);

        ui_set_status_panel_state(had_failure ? STATUS_PANEL_NETWORK_ERROR
                                             : STATUS_PANEL_NORMAL);
        
        /* Sleep for refresh interval, but wake up early if shutting down. */
        for (int i = 0; i < REFRESH_INTERVAL && is_running(); i++) {
            sleep(1);
        }
    }

    /* Cleanup */
    free(scratch);
    free(updated);
    return NULL;
}



/**
 * @brief Register signal handlers for graceful shutdown.
 */
static void setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

/**
 * @brief Perform a synchronous fetch so the first paint has data.
 *
 * Copies directly into the shared ticker buffer under lock.
 */
static int priceboard_initial_fetch(const Config config[static 1]) {
    /* Initial synchronous fetch so the first paint has data. */
    /* Fetch without holding the shared lock; copy results under lock. */
    int symbol_count = config->symbol_count;
    TickerData *scratch = calloc(symbol_count, sizeof(TickerData));
    bool *updated = calloc(symbol_count, sizeof(bool));
    if (!scratch || !updated) {
        free(scratch);
        free(updated);
        ui_set_status_panel_state(STATUS_PANEL_NETWORK_ERROR);
        return -1;
    }

    ui_set_status_panel_state(STATUS_PANEL_FETCHING);
    bool had_failure = false;
    fetch_all_symbols(config, scratch, updated, &had_failure);
    apply_updated_tickers(scratch, updated, symbol_count);

    free(scratch);
    free(updated);
    ui_set_status_panel_state(had_failure ? STATUS_PANEL_NETWORK_ERROR : STATUS_PANEL_NORMAL);
    return 0;
}

/**
 * @brief Initialize config, UI, shared buffers, and start the fetch thread.
 *
 * @return 0 on success, -1 on failure.
 */
static int init_runtime(Config config[static 1], pthread_t fetch_thread[static 1]) {
    /* Initialize config, UI, shared buffers, then start fetch thread. */
    if (load_config(config) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return -1;
    }

    if (config->symbol_count == 0) {
        fprintf(stderr, "No symbols configured\n");
        return -1;
    }

    ticker_count = config->symbol_count;
    global_tickers = calloc(ticker_count, sizeof(TickerData));
    if (!global_tickers) {
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    ticker_snapshot = malloc((size_t)ticker_count * sizeof(TickerData));
    if (!ticker_snapshot) {
        free(global_tickers);
        global_tickers = NULL;
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    ticker_snapshot_order = malloc((size_t)ticker_count * sizeof(int));
    if (!ticker_snapshot_order) {
        free(ticker_snapshot);
        ticker_snapshot = NULL;
        free(global_tickers);
        global_tickers = NULL;
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    init_ui();
    draw_splash_screen();

    if (pthread_create(fetch_thread, NULL, thread_data_fetch, config) != 0) {
        cleanup_ui();
        free(ticker_snapshot_order);
        ticker_snapshot_order = NULL;
        free(ticker_snapshot);
        ticker_snapshot = NULL;
        free(global_tickers);
        global_tickers = NULL;
        fprintf(stderr, "Failed to create fetch thread\n");
        return -1;
    }

    priceboard_initial_fetch(config);
    return 0;
}

/**
 * @brief Stop the worker thread and release UI/resources.
 */
static void shutdown_runtime(pthread_t fetch_thread) {
    /* Join worker thread and release UI/resources. */
    pthread_join(fetch_thread, NULL);
    cleanup_ui();
    free(global_tickers);
    global_tickers = NULL;
    free(ticker_snapshot_order);
    ticker_snapshot_order = NULL;
    free(ticker_snapshot);
    ticker_snapshot = NULL;
}

/**
 * @brief Main UI loop dispatching draw/input for board vs chart.
 */
static void run_event_loop(void) {
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
        .data_mutex = &data_mutex,
        .global_tickers = global_tickers,
        .ticker_snapshot = ticker_snapshot,
        .ticker_snapshot_order = ticker_snapshot_order,
        .ticker_count = &ticker_count,
    };

    ChartContext chart_ctx = {
        .data_mutex = &data_mutex,
        .global_tickers = global_tickers,
        .ticker_count = &ticker_count,
    };

    while (is_running()) {
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
                atomic_store_explicit(&running, false, memory_order_relaxed);
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
    
    Config config;
    pthread_t fetch_thread;

    setup_signal_handlers();

    if (init_runtime(&config, &fetch_thread) != 0) {
        return 1;
    }

    /*
     * Main loop.
     * Two UI modes:
     *  - Price board: select symbol and open chart (Enter)
     *  - Chart view : left/right candle cursor, up/down change interval
     */
    run_event_loop();

    shutdown_runtime(fetch_thread);
    return 0;
}
