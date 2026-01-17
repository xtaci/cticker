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

#define REFRESH_INTERVAL 5  // Refresh every 5 seconds

static _Atomic bool running = true;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static TickerData *global_tickers = NULL;
static TickerData *ticker_snapshot = NULL;
static int *ticker_snapshot_order = NULL;
static int ticker_count = 0;

typedef enum {
    SORT_FIELD_DEFAULT = 0,
    SORT_FIELD_PRICE,
    SORT_FIELD_CHANGE,
} PriceboardSortField;

typedef enum {
    SORT_DIR_DESC = 0,
    SORT_DIR_ASC,
} PriceboardSortDirection;

static PriceboardSortField current_sort_field = SORT_FIELD_DEFAULT;
static PriceboardSortDirection current_sort_direction = SORT_DIR_DESC;

/**
 * @brief Check whether the application should keep running.
 */
static inline bool is_running(void) {
    return atomic_load_explicit(&running, memory_order_relaxed);
}

/**
 * @brief Clamp an integer into the inclusive range [low, high].
 */
static inline int clamp_int(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/**
 * @brief Clamp the selected row index to the current ticker list bounds.
 */
static void clamp_selected(int selected[static 1]) {
    if (ticker_count <= 0) {
        *selected = 0;
        return;
    }
    *selected = clamp_int(*selected, 0, ticker_count - 1);
}

/**
 * @brief Map the current on-screen row back to the config order index.
 */
static int priceboard_resolve_symbol_index(int display_index) {
    if (!ticker_snapshot_order) {
        return -1;
    }
    if (display_index < 0 || display_index >= ticker_count) {
        return -1;
    }
    return ticker_snapshot_order[display_index];
}

/**
 * @brief Extract the numeric value used for sorting.
 */
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

/**
 * @brief Compare two snapshot rows using the active sort rules.
 */
static int priceboard_compare_rows(int left, int right) {
    const TickerData *lhs = &ticker_snapshot[left];
    const TickerData *rhs = &ticker_snapshot[right];
    double lhs_val = priceboard_sort_value(lhs, current_sort_field);
    double rhs_val = priceboard_sort_value(rhs, current_sort_field);
    int result = 0;
    if (lhs_val < rhs_val) {
        result = -1;
    } else if (lhs_val > rhs_val) {
        result = 1;
    }
    if (result == 0) {
        int lhs_origin = ticker_snapshot_order[left];
        int rhs_origin = ticker_snapshot_order[right];
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

/**
 * @brief Swap snapshot rows while keeping the origin index map in sync.
 */
static void priceboard_swap_rows(int left, int right) {
    if (left == right) {
        return;
    }
    TickerData tmp = ticker_snapshot[left];
    ticker_snapshot[left] = ticker_snapshot[right];
    ticker_snapshot[right] = tmp;

    int origin_tmp = ticker_snapshot_order[left];
    ticker_snapshot_order[left] = ticker_snapshot_order[right];
    ticker_snapshot_order[right] = origin_tmp;
}

/**
 * @brief Apply the current sort to the local snapshot buffer.
 */
static void priceboard_apply_sort(void) {
    if (!ticker_snapshot || !ticker_snapshot_order) {
        return;
    }
    if (ticker_count <= 1) {
        return;
    }
    if (current_sort_field == SORT_FIELD_DEFAULT) {
        return;
    }
    for (int i = 1; i < ticker_count; ++i) {
        int j = i;
        while (j > 0 && priceboard_compare_rows(j - 1, j) > 0) {
            priceboard_swap_rows(j - 1, j);
            --j;
        }
    }
}

/**
 * @brief Cycle between descending, ascending, and default order for a field.
 */
static void priceboard_cycle_sort(PriceboardSortField field) {
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

/**
 * @brief Describe the next sorting outcome if the user presses F5/F6.
 */
static const char *priceboard_next_sort_hint(PriceboardSortField field) {
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
 * @brief Background worker that refreshes the latest prices.
 *
 * Threading note: all writes to ::global_tickers happen under ::data_mutex so
 * the UI can take a consistent snapshot.
 */
static void* thread_data_fetch(void *arg) {
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
       
        /* Fetch all symbols into the scratch buffer. */ 
        for (int i = 0; i < symbol_count && is_running(); i++) {
            if (fetch_ticker_data(config->symbols[i], &scratch[i]) == 0) {
                updated[i] = true;
            } else {
                had_failure = true;
            }
        }

        // Copy updated tickers into the shared buffer under lock.
        pthread_mutex_lock(&data_mutex);
        for (int i = 0; i < symbol_count; i++) {
            if (updated[i]) {
                global_tickers[i] = scratch[i];
            }
        }
        pthread_mutex_unlock(&data_mutex);

        if (had_failure) {
            ui_set_status_panel_state(STATUS_PANEL_NETWORK_ERROR);
        } else {
            ui_set_status_panel_state(STATUS_PANEL_NORMAL);
        }
        
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
 * @brief Helper to reload chart data for a symbol/period.
 *
 * On success, takes ownership of the new_points buffer.
 */
static int chart_reload_data(const char symbol[static 1], Period period,
                             PricePoint *points[static 1], int count[static 1]) {
    /**
     * Helper that swaps in a freshly fetched candle array.
     *
     * On success we free the old buffer and take ownership of new_points.
     */
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
    for (int i = 0; i < symbol_count; i++) {
        if (fetch_ticker_data(config->symbols[i], &scratch[i]) == 0) {
            updated[i] = true;
        } else {
            had_failure = true;
        }
    }

    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < symbol_count; i++) {
        if (updated[i]) {
            global_tickers[i] = scratch[i];
        }
    }
    pthread_mutex_unlock(&data_mutex);

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
 * @brief Draw the price board using a snapshot of shared tickers.
 */
static void priceboard_render(int selected) {
    if (!ticker_snapshot) {
        return;
    }

    pthread_mutex_lock(&data_mutex);
    memcpy(ticker_snapshot, global_tickers, (size_t)ticker_count * sizeof(TickerData));
    pthread_mutex_unlock(&data_mutex);

    if (ticker_snapshot_order) {
        for (int i = 0; i < ticker_count; ++i) {
            ticker_snapshot_order[i] = i;
        }
    }
    priceboard_apply_sort();

    const char *price_hint = priceboard_next_sort_hint(SORT_FIELD_PRICE);
    const char *change_hint = priceboard_next_sort_hint(SORT_FIELD_CHANGE);
    draw_main_screen(ticker_snapshot, ticker_count, selected, price_hint, change_hint);
}

/**
 * @brief Load chart data for the selected symbol and prepare initial cursor.
 *
 * @return true on success (chart ready), false on failure.
 */
static bool chart_open(int selected, Period current_period,
                                     PricePoint *chart_points[static 1],
                                     int chart_count[static 1],
                                     char chart_symbol[static 1],
                                     int chart_cursor_idx[static 1]) {
    if (ticker_count <= 0 || selected < 0 || selected >= ticker_count) {
        beep();
        return false;
    }

    int symbol_index = priceboard_resolve_symbol_index(selected);
    if (symbol_index < 0) {
        beep();
        return false;
    }

    pthread_mutex_lock(&data_mutex);
    snprintf(chart_symbol, MAX_SYMBOL_LEN, "%s", global_tickers[symbol_index].symbol);
    pthread_mutex_unlock(&data_mutex);

    if (chart_reload_data(chart_symbol, current_period, chart_points, chart_count) == 0) {
        *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
        return true;
    }

    beep();
    return false;
}

static void chart_reset_state(PricePoint *chart_points[static 1],
                              int chart_count[static 1],
                              int chart_cursor_idx[static 1]);

/**
 * @brief Leave chart mode and release chart resources.
 */
static void chart_close(bool show_chart[static 1],
                        PricePoint *chart_points[static 1],
                        int chart_count[static 1],
                        int chart_cursor_idx[static 1]) {
    *show_chart = false;
    chart_reset_state(chart_points, chart_count, chart_cursor_idx);
}

/**
 * @brief Release chart buffers and reset chart view indices.
 */
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

/**
 * @brief Normalize the chart cursor index to a valid candle index.
 */
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

static void chart_change_period(int step, char chart_symbol[static 1],
                                Period current_period[static 1],
                                PricePoint *chart_points[static 1],
                                int chart_count[static 1],
                                int chart_cursor_idx[static 1]) {
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

/**
 * @brief Update the newest candle so it reflects the latest ticker price.
 */
static void chart_apply_live_price(const char symbol[static 1],
                                   PricePoint points[static 1],
                                   int chart_count) {
    if (!symbol[0] || chart_count <= 0) {
        return;
    }

    TickerData latest = {0};
    bool found = false;

    pthread_mutex_lock(&data_mutex);
    if (global_tickers) {
        for (int i = 0; i < ticker_count; ++i) {
            if (strncmp(global_tickers[i].symbol, symbol, MAX_SYMBOL_LEN) == 0) {
                latest = global_tickers[i];
                found = true;
                break;
            }
        }
    }
    pthread_mutex_unlock(&data_mutex);

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

/**
 * @brief Reload candles when the latest candle has closed, keeping selection stable.
 */
static void chart_refresh_if_expired(char chart_symbol[static 1],
                                     Period current_period,
                                     PricePoint *chart_points[static 1],
                                     int chart_count[static 1],
                                     int chart_cursor_idx[static 1]) {
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

    PricePoint *refreshed = *chart_points;
    for (int i = 0; i < *chart_count; ++i) {
        if (refreshed[i].timestamp == retained_ts) {
            *chart_cursor_idx = i;
            return;
        }
    }

    *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
}

/**
 * @brief Force reload candles regardless of expiry, optionally following latest.
 */
static void chart_force_refresh(char chart_symbol[static 1],
                                Period current_period,
                                PricePoint *chart_points[static 1],
                                int chart_count[static 1],
                                int chart_cursor_idx[static 1],
                                bool follow_latest) {
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

    PricePoint *refreshed = *chart_points;
    for (int i = 0; i < *chart_count; ++i) {
        if (refreshed[i].timestamp == retained_ts) {
            *chart_cursor_idx = i;
            return;
        }
    }

    *chart_cursor_idx = *chart_count - 1;
}

/**
 * @brief Handle key input while in chart mode.
 */
static void chart_handle_input(int ch, char chart_symbol[static 1],
                               Period current_period[static 1],
                               PricePoint *chart_points[static 1],
                               int chart_count[static 1],
                               int chart_cursor_idx[static 1],
                               bool show_chart[static 1],
                               bool follow_latest[static 1]) {
    switch (ch) {
        case KEY_UP:
            chart_change_period(-1, chart_symbol, current_period, chart_points, chart_count,
                                chart_cursor_idx);
            break;
        case KEY_DOWN:
            chart_change_period(1, chart_symbol, current_period, chart_points, chart_count,
                                chart_cursor_idx);
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
            chart_force_refresh(chart_symbol, *current_period, chart_points,
                                chart_count, chart_cursor_idx, *follow_latest);
            break;
        case 'q':
        case 'Q':
        case 27:  // ESC
            chart_close(show_chart, chart_points, chart_count, chart_cursor_idx);
            *follow_latest = true;
            break;
        default:
            break;
    }
}

/**
 * @brief Handle key input while on the price board.
 */
static void priceboard_handle_input(int ch, int selected[static 1], Period current_period,
                                     bool show_chart[static 1],
                                     PricePoint *chart_points[static 1],
                                     int chart_count[static 1], char chart_symbol[static 1],
                                     int chart_cursor_idx[static 1]) {
    switch (ch) {
        case KEY_UP:
            (*selected)--;
            clamp_selected(selected);
            break;
        case KEY_DOWN:
            (*selected)++;
            clamp_selected(selected);
            break;
        case '\n':
        case '\r':
        case KEY_ENTER:
            clamp_selected(selected);
            if (chart_open(*selected, current_period, chart_points, chart_count,
                                          chart_symbol, chart_cursor_idx)) {
                *show_chart = true;
            }
            break;
        case 'q':
        case 'Q':
            atomic_store_explicit(&running, false, memory_order_relaxed);
            break;
        case KEY_F(5):
            priceboard_cycle_sort(SORT_FIELD_PRICE);
            break;
        case KEY_F(6):
            priceboard_cycle_sort(SORT_FIELD_CHANGE);
            break;
        default:
            break;
    }
}

/**
 * @brief Handle mouse input while the chart view is active.
 */
static void chart_handle_mouse(const MEVENT ev,
                               char chart_symbol[static 1],
                               Period current_period[static 1],
                               PricePoint *chart_points[static 1],
                               int chart_count[static 1],
                               int chart_cursor_idx[static 1],
                               bool show_chart[static 1],
                               bool follow_latest[static 1]) {
    if (ev.bstate & (BUTTON3_PRESSED | BUTTON3_RELEASED | BUTTON3_CLICKED)) {
        chart_handle_input(27, chart_symbol, current_period, chart_points,
                           chart_count, chart_cursor_idx, show_chart, follow_latest);
        return;
    }
    if (ev.bstate & BUTTON4_PRESSED) {
        chart_change_period(-1, chart_symbol, current_period, chart_points,
                            chart_count, chart_cursor_idx);
        return;
    }
    if (ev.bstate & BUTTON5_PRESSED) {
        chart_change_period(1, chart_symbol, current_period, chart_points,
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

/**
 * @brief Handle mouse input while the price board is active.
 */
static void priceboard_handle_mouse(const MEVENT ev,
                                    int selected[static 1],
                                    Period current_period,
                                    bool show_chart[static 1],
                                    PricePoint *chart_points[static 1],
                                    int chart_count[static 1],
                                    char chart_symbol[static 1],
                                    int chart_cursor_idx[static 1]) {
    if (ev.bstate & BUTTON4_PRESSED) {
        (*selected)--;
        clamp_selected(selected);
        return;
    }
    if (ev.bstate & BUTTON5_PRESSED) {
        (*selected)++;
        clamp_selected(selected);
        return;
    }
    if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED)) {
        int row = ui_price_board_hit_test_row(ev.y, ticker_count);
        if (row < 0) {
            return;
        }

        *selected = row;
        clamp_selected(selected);
        if (chart_open(*selected, current_period, chart_points, chart_count,
                       chart_symbol, chart_cursor_idx)) {
            *show_chart = true;
        }
    }
}

/**
 * @brief Main UI loop dispatching draw/input for board vs chart.
 */
static void run_event_loop(void) {
    PricePoint *chart_points = NULL;
    Period current_period = PERIOD_1MIN;
    bool show_chart = false;
    int selected = 0;
    char chart_symbol[MAX_SYMBOL_LEN] = {0};
    int chart_count = 0;
    int chart_cursor_idx = -1;
    bool chart_follow_latest = true;

    while (is_running()) {
        if (show_chart) {
            bool follow_latest = chart_follow_latest ||
                                 (chart_cursor_idx >= 0 && chart_cursor_idx == chart_count - 1);
            chart_refresh_if_expired(chart_symbol, current_period, &chart_points,
                                     &chart_count, &chart_cursor_idx);
            chart_apply_live_price(chart_symbol, chart_points, chart_count);
            if (follow_latest && chart_count > 0) {
                chart_cursor_idx = chart_count - 1;
            }
            draw_chart(chart_symbol, chart_count, chart_points, current_period,
                       chart_cursor_idx);
        } else {
            clamp_selected(&selected);
            priceboard_render(selected);
        }

        int ch = handle_input();
        if (ch == ERR) {
            continue;
        }

        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (show_chart) {
                    chart_handle_mouse(ev, chart_symbol, &current_period, &chart_points,
                                      &chart_count, &chart_cursor_idx, &show_chart,
                                      &chart_follow_latest);
                } else {
                    priceboard_handle_mouse(ev, &selected, current_period, &show_chart,
                                            &chart_points, &chart_count, chart_symbol,
                                            &chart_cursor_idx);
                }
            }
            continue;
        }

        if (show_chart) {
            chart_handle_input(ch, chart_symbol, &current_period, &chart_points,
                               &chart_count, &chart_cursor_idx, &show_chart,
                               &chart_follow_latest);
        } else {
            priceboard_handle_input(ch, &selected, current_period, &show_chart,
                                     &chart_points, &chart_count, chart_symbol,
                                     &chart_cursor_idx);
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
