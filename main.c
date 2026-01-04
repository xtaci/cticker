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

// Signal handler for clean exit
static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        running = 0;
    }
}

// Thread function to fetch ticker data
static void* fetch_data_thread(void *arg) {
    Config *config = (Config *)arg;
    
    while (running) {
        pthread_mutex_lock(&data_mutex);
        
        for (int i = 0; i < config->symbol_count; i++) {
            fetch_ticker_data(config->symbols[i], &global_tickers[i]);
        }
        
        pthread_mutex_unlock(&data_mutex);
        
        // Sleep for refresh interval
        for (int i = 0; i < REFRESH_INTERVAL && running; i++) {
            sleep(1);
        }
    }
    
    return NULL;
}

static int reload_chart_data(const char *symbol, Period period,
                             PricePoint **points, int *count) {
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

// Main function
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
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Load configuration
    if (load_config(&config) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }
    
    if (config.symbol_count == 0) {
        fprintf(stderr, "No symbols configured\n");
        return 1;
    }
    
    // Allocate ticker data
    ticker_count = config.symbol_count;
    global_tickers = calloc(ticker_count, sizeof(TickerData));
    if (!global_tickers) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }
    
    // Initialize UI
    init_ui();
    
    // Start data fetching thread
    if (pthread_create(&fetch_thread, NULL, fetch_data_thread, &config) != 0) {
        cleanup_ui();
        free(global_tickers);
        fprintf(stderr, "Failed to create fetch thread\n");
        return 1;
    }
    
    // Initial data fetch
    pthread_mutex_lock(&data_mutex);
    for (int i = 0; i < config.symbol_count; i++) {
        fetch_ticker_data(config.symbols[i], &global_tickers[i]);
    }
    pthread_mutex_unlock(&data_mutex);
    
    // Main loop
    while (running) {
        if (show_chart) {
            draw_chart(chart_symbol, chart_points, chart_count, current_period,
                       chart_cursor_idx);
        } else {
            pthread_mutex_lock(&data_mutex);
            TickerData *tickers_copy = malloc(ticker_count * sizeof(TickerData));
            if (tickers_copy) {
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
                    // Fetch historical data and show chart
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
    
    // Cleanup
    pthread_join(fetch_thread, NULL);
    
    if (chart_points) {
        free(chart_points);
    }
    
    cleanup_ui();
    free(global_tickers);
    
    return 0;
}
