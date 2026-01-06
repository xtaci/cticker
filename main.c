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
#include <ncursesw/ncurses.h>
#include "cticker.h"

#define REFRESH_INTERVAL 5  // Refresh every 5 seconds

static _Atomic bool running = true;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static TickerData *global_tickers = NULL;
static int ticker_count = 0;

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
static void* fetch_data_thread(void *arg) {
    Config *config = (Config *)arg;
    int symbol_count = config->symbol_count;

    /* Fetch into a scratch buffer, then copy under lock to keep UI responsive. */
    TickerData *scratch = calloc(symbol_count, sizeof(TickerData));
    bool *updated = calloc(symbol_count, sizeof(bool));
    if (!scratch || !updated) {
        free(scratch);
        free(updated);
        atomic_store_explicit(&running, false, memory_order_relaxed);
        return NULL;
    }
   
    /* Keep fetching until signaled to stop. */ 
    while (atomic_load_explicit(&running, memory_order_relaxed)) {
        memset(updated, 0, symbol_count * sizeof(bool));
       
        /* Fetch all symbols into the scratch buffer. */ 
        for (int i = 0; i < symbol_count && atomic_load_explicit(&running, memory_order_relaxed); i++) {
            if (fetch_ticker_data(config->symbols[i], &scratch[i]) == 0) {
                updated[i] = true;
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
        
        /* Sleep for refresh interval, but wake up early if shutting down. */
        for (int i = 0; i < REFRESH_INTERVAL && atomic_load_explicit(&running, memory_order_relaxed); i++) {
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
static int perform_initial_fetch(const Config config[static 1]) {
    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < config->symbol_count; i++) {
        fetch_ticker_data(config->symbols[i], &global_tickers[i]);
    }
    pthread_mutex_unlock(&data_mutex);
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

    init_ui();
    draw_splash_screen();

    if (pthread_create(fetch_thread, NULL, fetch_data_thread, config) != 0) {
        cleanup_ui();
        free(global_tickers);
        fprintf(stderr, "Failed to create fetch thread\n");
        return -1;
    }

    perform_initial_fetch(config);
    return 0;
}

/**
 * @brief Stop the worker thread and release UI/resources.
 */
static void shutdown_runtime(pthread_t fetch_thread) {
    pthread_join(fetch_thread, NULL);
    cleanup_ui();
    free(global_tickers);
}

/**
 * @brief Draw the price board using a snapshot of shared tickers.
 */
static void render_price_board(int selected) {
    pthread_mutex_lock(&data_mutex);
    TickerData *tickers_copy = malloc(ticker_count * sizeof(TickerData));
    if (tickers_copy) {
        memcpy(tickers_copy, global_tickers, ticker_count * sizeof(TickerData));
    }
    pthread_mutex_unlock(&data_mutex);

    if (tickers_copy) {
        draw_main_screen(tickers_copy, ticker_count, selected);
        free(tickers_copy);
    }
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
    pthread_mutex_lock(&data_mutex);
    strcpy(chart_symbol, global_tickers[selected].symbol);
    pthread_mutex_unlock(&data_mutex);

    if (chart_reload_data(chart_symbol, current_period, chart_points, chart_count) == 0) {
        *chart_cursor_idx = (*chart_count > 0) ? (*chart_count - 1) : -1;
        return true;
    }

    beep();
    return false;
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
}

static void chart_change_period(int step, char chart_symbol[static 1],
                                Period current_period[static 1],
                                PricePoint *chart_points[static 1],
                                int chart_count[static 1],
                                int chart_cursor_idx[static 1]) {
    int old_period = *current_period;
    int next = (*current_period + step) % PERIOD_COUNT;
    if (next < 0) {
        next += PERIOD_COUNT;
    }
    *current_period = (Period)next;
    if (chart_reload_data(chart_symbol, *current_period, chart_points, chart_count) == 0) {
        if (*chart_count > 0) {
            if (*chart_cursor_idx >= *chart_count) {
                *chart_cursor_idx = *chart_count - 1;
            }
            if (*chart_cursor_idx < 0) {
                *chart_cursor_idx = *chart_count - 1;
            }
        } else {
            *chart_cursor_idx = -1;
        }
    } else {
        *current_period = (Period)old_period;
        beep();
    }
}

/**
 * @brief Handle key input while in chart mode.
 */
static void chart_handle_input(int ch, char chart_symbol[static 1],
                               Period current_period[static 1],
                               PricePoint *chart_points[static 1],
                               int chart_count[static 1],
                               int chart_cursor_idx[static 1],
                               bool show_chart[static 1]) {
    switch (ch) {
        case ' ': {
            chart_change_period(1, chart_symbol, current_period, chart_points, chart_count,
                                chart_cursor_idx);
            break;
        }
        case KEY_LEFT:
            if (*chart_cursor_idx > 0) {
                (*chart_cursor_idx)--;
            }
            break;
        case KEY_RIGHT:
            if (*chart_cursor_idx >= 0 && *chart_cursor_idx < *chart_count - 1) {
                (*chart_cursor_idx)++;
            }
            break;
        case 'q':
        case 'Q':
        case 27:  // ESC
            *show_chart = false;
            chart_reset_state(chart_points, chart_count, chart_cursor_idx);
            break;
        default:
            break;
    }
}

/**
 * @brief Handle key input while on the price board.
 */
static void handle_price_board_input(int ch, int selected[static 1], Period current_period,
                                     bool show_chart[static 1],
                                     PricePoint *chart_points[static 1],
                                     int chart_count[static 1], char chart_symbol[static 1],
                                     int chart_cursor_idx[static 1]) {
    switch (ch) {
        case KEY_UP:
            if (*selected > 0) {
                (*selected)--;
            }
            break;
        case KEY_DOWN:
            if (*selected < ticker_count - 1) {
                (*selected)++;
            }
            break;
        case '\n':
        case '\r':
        case KEY_ENTER:
            if (chart_open(*selected, current_period, chart_points, chart_count,
                                          chart_symbol, chart_cursor_idx)) {
                *show_chart = true;
            }
            break;
        case 'q':
        case 'Q':
            atomic_store_explicit(&running, false, memory_order_relaxed);
            break;
        default:
            break;
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

    while (atomic_load_explicit(&running, memory_order_relaxed)) {
        if (show_chart) {
            draw_chart(chart_symbol, chart_count, chart_points, current_period,
                       chart_cursor_idx);
        } else {
            render_price_board(selected);
        }

        int ch = handle_input();
        if (ch == ERR) {
            continue;
        }

        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                if (show_chart) {
                    if (ev.bstate & (BUTTON3_PRESSED | BUTTON3_RELEASED | BUTTON3_CLICKED)) {
                        chart_handle_input(27, chart_symbol, &current_period, &chart_points,
                                           &chart_count, &chart_cursor_idx, &show_chart);
                    } else if (ev.bstate & BUTTON4_PRESSED) {
                        chart_change_period(-1, chart_symbol, &current_period, &chart_points,
                                            &chart_count, &chart_cursor_idx);
                    } else if (ev.bstate & BUTTON5_PRESSED) {
                        chart_change_period(1, chart_symbol, &current_period, &chart_points,
                                           &chart_count, &chart_cursor_idx);
                    } else if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED)) {
                        int idx = ui_chart_hit_test_index(ev.x, chart_count);
                        if (idx >= 0) {
                            chart_cursor_idx = idx;
                        }
                    }
                } else {
                    bool changed = false;
                    if (ev.bstate & BUTTON4_PRESSED) {
                        if (selected > 0) {
                            selected--;
                            changed = true;
                        }
                    } else if (ev.bstate & BUTTON5_PRESSED) {
                        if (selected < ticker_count - 1) {
                            selected++;
                            changed = true;
                        }
                    } else if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED)) {
                        int row = ui_price_board_hit_test_row(ev.y, ticker_count);
                        if (row >= 0) {
                            selected = row;
                            if (chart_open(selected, current_period, &chart_points,
                                                         &chart_count, chart_symbol, &chart_cursor_idx)) {
                                show_chart = true;
                            }
                        }
                    }
                    if (changed) {
                        if (selected < 0) {
                            selected = 0;
                        }
                        if (selected > ticker_count - 1) {
                            selected = ticker_count - 1;
                        }
                    }
                }
            }
            continue;
        }

        if (show_chart) {
            chart_handle_input(ch, chart_symbol, &current_period, &chart_points,
                               &chart_count, &chart_cursor_idx, &show_chart);
        } else {
            handle_price_board_input(ch, &selected, current_period, &show_chart,
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
     *  - Chart view : left/right candle cursor, space changes interval
     */
    run_event_loop();

    shutdown_runtime(fetch_thread);
    return 0;
}
