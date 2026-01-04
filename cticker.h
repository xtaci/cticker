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


#ifndef CTICKER_H
#define CTICKER_H

/**
 * @file cticker.h
 * @brief Public types and function declarations for cticker.
 */

#include <stdint.h>
#include <stdbool.h>

#define MAX_SYMBOLS 50
#define MAX_SYMBOL_LEN 20
#define MAX_HISTORY 1440  // 1 day in minutes
#define CONFIG_FILE ".cticker.conf"

/**
 * @brief Trading pair information displayed on the price board.
 */
typedef struct {
    char symbol[MAX_SYMBOL_LEN];
    double price;
    double change_24h;
    uint64_t timestamp;
} TickerData;

/**
 * @brief Price history point (candlestick OHLC).
 */
typedef struct {
    uint64_t timestamp;
    double open;
    double high;
    double low;
    double close;
} PricePoint;

/**
 * @brief Configuration structure loaded from the user's config file.
 */
typedef struct {
    char symbols[MAX_SYMBOLS][MAX_SYMBOL_LEN];
    int symbol_count;
} Config;

/**
 * @brief Chart time interval selection.
 */
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

/** @name Config functions */
///@{
int load_config(Config *config);
int save_config(const Config *config);
///@}

/** @name API functions */
///@{
int fetch_ticker_data(const char *symbol, TickerData *data);
int fetch_historical_data(const char *symbol, Period period, PricePoint **points, int *count);
///@}

/** @name UI functions */
///@{
void init_ui(void);
void cleanup_ui(void);
void draw_main_screen(TickerData *tickers, int count, int selected);
void draw_chart(const char *symbol, PricePoint *points, int count, Period period,
                int selected_index);
int handle_input(void);
///@}

#endif // CTICKER_H
