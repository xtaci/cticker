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
#include <ncurses.h>
#include "cticker.h"

#define REFRESH_INTERVAL 5  // Refresh every 5 seconds

static volatile int running = 1;
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
        running = 0;
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
    
    while (running) {
        pthread_mutex_lock(&data_mutex);
        
        for (int i = 0; i < config->symbol_count; i++) {
            fetch_ticker_data(config->symbols[i], &global_tickers[i]);
        }
        
        pthread_mutex_unlock(&data_mutex);
        
        /* Sleep for refresh interval, but wake up early if shutting down. */
        for (int i = 0; i < REFRESH_INTERVAL && running; i++) {
            sleep(1);
        }
    }
    
    return NULL;
}

static int reload_chart_data(const char *symbol, Period period,
                             PricePoint **points, int *count) {
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
 * @brief Program entry point.
 *
 * Sets up config, starts the worker thread, then runs the UI state machine.
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    Config config;
    int selected = 0;
    Period current_period = PERIOD_1MIN;
    bool show_chart = false;
    char chart_symbol[MAX_SYMBOL_LEN] = {0};
    PricePoint *chart_points = NULL;
    int chart_count = 0;
    int chart_cursor_idx = -1;
    pthread_t fetch_thread;
    
    /* Setup signal handlers. */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Load configuration (symbols list). */
    if (load_config(&config) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }
    
    if (config.symbol_count == 0) {
        fprintf(stderr, "No symbols configured\n");
        return 1;
    }
    
    /* Allocate the shared ticker array used by both the worker and UI. */
    ticker_count = config.symbol_count;
    global_tickers = calloc(ticker_count, sizeof(TickerData));
    if (!global_tickers) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }
    
    /* Initialize UI (ncurses). From this point, exit via cleanup_ui(). */
    init_ui();

    // Show a splash screen while the first batch of data is being fetched.
    // This avoids a blank (black) screen on slow networks.
    draw_splash_screen();
    
    /* Start data fetching thread. */
    if (pthread_create(&fetch_thread, NULL, fetch_data_thread, &config) != 0) {
        cleanup_ui();
        free(global_tickers);
        fprintf(stderr, "Failed to create fetch thread\n");
        return 1;
    }
    
    /* Initial data fetch so the board isn't empty on first paint. */
    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < config.symbol_count; i++) {
        fetch_ticker_data(config.symbols[i], &global_tickers[i]);
    }
    pthread_mutex_unlock(&data_mutex);
    
    /*
     * Main loop.
     * Two UI modes:
     *  - Price board: select symbol and open chart (Enter)
     *  - Chart view : left/right candle cursor, space changes interval
     */
    while (running) {
        if (show_chart) {
            draw_chart(chart_symbol, chart_points, chart_count, current_period,
                       chart_cursor_idx);
        } else {
            pthread_mutex_lock(&data_mutex);
            TickerData *tickers_copy = malloc(ticker_count * sizeof(TickerData));
            if (tickers_copy) {
                /* Copy under lock, then draw without holding the mutex. */
                memcpy(tickers_copy, global_tickers, ticker_count * sizeof(TickerData));
                pthread_mutex_unlock(&data_mutex);
                
                draw_main_screen(tickers_copy, ticker_count, selected);
                free(tickers_copy);
            } else {
                pthread_mutex_unlock(&data_mutex);
            }
        }
        
        int ch = handle_input();
        
        if (show_chart) {
            switch (ch) {
                case ' ':
                    /* Cycle chart interval and reload candles. */
                    current_period = (Period)((current_period + 1) % PERIOD_COUNT);
                    if (reload_chart_data(chart_symbol, current_period,
                                          &chart_points, &chart_count) == 0) {
                        if (chart_count > 0) {
                            if (chart_cursor_idx >= chart_count) {
                                chart_cursor_idx = chart_count - 1;
                            }
                            if (chart_cursor_idx < 0) {
                                chart_cursor_idx = chart_count - 1;
                            }
                        } else {
                            chart_cursor_idx = -1;
                        }
                    } else {
                        beep();
                    }
                    break;
                case KEY_LEFT:
                    if (chart_cursor_idx > 0) {
                        chart_cursor_idx--;
                    }
                    break;
                case KEY_RIGHT:
                    if (chart_cursor_idx >= 0 && chart_cursor_idx < chart_count - 1) {
                        chart_cursor_idx++;
                    }
                    break;
                case 'q':
                case 'Q':
                case 27:  // ESC
                    /* Back to price board. */
                    show_chart = false;
                    if (chart_points) {
                        free(chart_points);
                        chart_points = NULL;
                        chart_count = 0;
                    }
                    chart_cursor_idx = -1;
                    break;
            }
        } else {
            switch (ch) {
                case KEY_UP:
                    if (selected > 0) selected--;
                    break;
                case KEY_DOWN:
                    if (selected < ticker_count - 1) selected++;
                    break;
                case '\n':
                case '\r':
                case KEY_ENTER:
                    /* Fetch historical data and show chart for the selected symbol. */
                    pthread_mutex_lock(&data_mutex);
                    strcpy(chart_symbol, global_tickers[selected].symbol);
                    pthread_mutex_unlock(&data_mutex);
                    
                    if (reload_chart_data(chart_symbol, current_period,
                                           &chart_points, &chart_count) == 0) {
                        show_chart = true;
                        chart_cursor_idx = (chart_count > 0) ? (chart_count - 1) : -1;
                    } else {
                        beep();
                    }
                    break;
                case 'q':
                case 'Q':
                    running = 0;
                    break;
            }
        }
    }
    
    /* Cleanup: stop worker, release chart buffer, restore terminal. */
    pthread_join(fetch_thread, NULL);
    
    if (chart_points) {
        free(chart_points);
    }
    
    cleanup_ui();
    free(global_tickers);
    
    return 0;
}
