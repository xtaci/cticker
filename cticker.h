#ifndef CTICKER_H
#define CTICKER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SYMBOLS 50
#define MAX_SYMBOL_LEN 20
#define MAX_HISTORY 1440  // 1 day in minutes
#define CONFIG_FILE ".cticker.conf"

// Trading pair information
typedef struct {
    char symbol[MAX_SYMBOL_LEN];
    double price;
    double change_24h;
    uint64_t timestamp;
} TickerData;

// Price history point (candlestick)
typedef struct {
    uint64_t timestamp;
    double open;
    double high;
    double low;
    double close;
} PricePoint;

// Configuration structure
typedef struct {
    char symbols[MAX_SYMBOLS][MAX_SYMBOL_LEN];
    int symbol_count;
} Config;

// Period enum
typedef enum {
    PERIOD_1MIN,
    PERIOD_15MIN,
    PERIOD_1HOUR,
    PERIOD_4HOUR,
    PERIOD_1DAY,
    PERIOD_1WEEK,
    PERIOD_1MONTH,
    PERIOD_COUNT
} Period;

// Function declarations
// Config functions
int load_config(Config *config);
int save_config(const Config *config);

// API functions
int fetch_ticker_data(const char *symbol, TickerData *data);
int fetch_historical_data(const char *symbol, Period period, PricePoint **points, int *count);

// UI functions
void init_ui(void);
void cleanup_ui(void);
void draw_main_screen(TickerData *tickers, int count, int selected);
void draw_chart(const char *symbol, PricePoint *points, int count, Period period,
                int selected_index);
int handle_input(void);

#endif // CTICKER_H
