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
#include <stddef.h>

/** Maximum number of symbols supported in the watchlist. */
#define MAX_SYMBOLS 50

/** Maximum length of a symbol string (including the terminating NUL). */
#define MAX_SYMBOL_LEN 20

/** Reserved historical capacity (currently unused by the simple downloader). */
#define MAX_HISTORY 1440  // 1 day in minutes

/** User config file name stored under $HOME. */
#define CONFIG_FILE ".cticker.conf"

/**
 * @brief Trading pair information displayed on the price board.
 */
typedef struct {
    /** Trading pair symbol (e.g. "BTCUSDT"). */
    char symbol[MAX_SYMBOL_LEN];
    /** Last traded price. */
    double price;
    /** 24-hour price change percentage (e.g. +1.23). */
    double change_24h;
    /** 24h high price. */
    double high_price;
    /** 24h low price. */
    double low_price;
    /** 24h base asset volume. */
    double volume_base;
    /** 24h quote asset volume. */
    double volume_quote;
    /** 24h trade count. */
    int trade_count;
    /** Sample timestamp in seconds since Unix epoch. */
    uint64_t timestamp;
    /** Raw price text returned by the API. */
    char price_text[32];
    /** Raw high price text returned by the API. */
    char high_text[32];
    /** Raw low price text returned by the API. */
    char low_text[32];
} TickerData;

/**
 * @brief Price history point (candlestick OHLC).
 */
typedef struct {
    /** Candle open time in seconds since Unix epoch. */
    uint64_t timestamp;
    /** Candle close time in seconds since Unix epoch. */
    uint64_t close_time;
    /** Open price for the interval. */
    double open;
    /** High price for the interval. */
    double high;
    /** Low price for the interval. */
    double low;
    /** Close price for the interval. */
    double close;
    /** Base asset volume traded during the interval. */
    double volume;
    /** Quote asset volume traded during the interval. */
    double quote_volume;
    /** Number of trades recorded during the interval. */
    int trade_count;
    /** Taker buy volume measured in base asset units. */
    double taker_buy_base_volume;
    /** Taker buy volume measured in quote asset units. */
    double taker_buy_quote_volume;
    /** String-preserved open price. */
    char open_text[32];
    /** String-preserved high price. */
    char high_text[32];
    /** String-preserved low price. */
    char low_text[32];
    /** String-preserved close price. */
    char close_text[32];
} PricePoint;

/**
 * @brief Configuration structure loaded from the user's config file.
 */
typedef struct {
    /** List of trading pair symbols. */
    char symbols[MAX_SYMBOLS][MAX_SYMBOL_LEN];
    /** Number of valid entries in ::Config::symbols. */
    int symbol_count;
} Config;

/**
 * @brief Chart time interval selection.
 */
typedef enum {
    /** 1-minute candles. */
    PERIOD_1MIN,
    /** 15-minute candles. */
    PERIOD_15MIN,
    /** 1-hour candles. */
    PERIOD_1HOUR,
    /** 4-hour candles. */
    PERIOD_4HOUR,
    /** 1-day candles. */
    PERIOD_1DAY,
    /** 1-week candles. */
    PERIOD_1WEEK,
    /** 1-month candles. */
    PERIOD_1MONTH,
    /** Number of supported periods (sentinel). */
    PERIOD_COUNT
} Period;

/**
 * @brief Status indicators for the footer panel.
 */
typedef enum {
    /** Idle state when the fetch thread is sleeping. */
    STATUS_PANEL_NORMAL = 0,
    /** Active network fetch in progress. */
    STATUS_PANEL_FETCHING,
    /** Latest fetch attempt failed due to network/API issues. */
    STATUS_PANEL_NETWORK_ERROR,
} StatusPanelState;

/** @name Config functions */
///@{
/**
 * @brief Load configuration from the user's config file.
 *
 * The configuration contains the watchlist symbols shown on the price board.
 * If the config file does not exist, a default watchlist may be created.
 *
 * @param[out] config Configuration to populate.
 * @return 0 on success, non-zero on failure.
 */
int load_config(Config *config);

/**
 * @brief Save configuration to the user's config file.
 *
 * @param[in] config Configuration to persist.
 * @return 0 on success, non-zero on failure.
 */
int save_config(const Config *config);
///@}

/** @name API functions */
///@{
/**
 * @brief Fetch the latest ticker data for a symbol.
 *
 * Populates price and 24h change fields for the given trading pair.
 *
 * @param[in] symbol Trading pair symbol (e.g. "BTCUSDT").
 * @param[out] data Output structure to fill.
 * @return 0 on success, non-zero on failure.
 */
int fetch_ticker_data(const char *symbol, TickerData *data);

/**
 * @brief Fetch historical candlestick (OHLC) data for charting.
 *
 * On success this function allocates a buffer for @p *points.
 * The caller owns the returned memory and must free(@p *points).
 *
 * @param[in] symbol Trading pair symbol (e.g. "BTCUSDT").
 * @param[in] period Time interval selection.
 * @param[out] points Allocated array of ::PricePoint on success.
 * @param[out] count Number of valid points stored in @p *points.
 * @return 0 on success, non-zero on failure.
 */
int fetch_historical_data(const char *symbol, Period period,
                          PricePoint **points, int *count);
///@}

/** @name UI functions */
///@{
/**
 * @brief Initialize the ncurses UI.
 *
 * Must be called before any other UI function. After calling, the program
 * should eventually call cleanup_ui() to restore the terminal state.
 */
void init_ui(void);

/**
 * @brief Tear down the ncurses UI and restore terminal state.
 */
void cleanup_ui(void);

/**
 * @brief Render the startup splash screen.
 *
 * Intended to be shown while the first batch of market data is being fetched
 * so the user doesn't see a blank screen.
 */
void draw_splash_screen(void);

/**
 * @brief Render the main price board.
 *
 * @param[in] tickers Array of ticker rows to display.
 * @param[in] count Number of elements in @p tickers.
 * @param[in] selected Selected row index within @p tickers.
 * @param[in] sort_hint_price Symbol describing the next F5 sort outcome.
 * @param[in] sort_hint_change Symbol describing the next F6 sort outcome.
 */
void draw_main_screen(TickerData *tickers, int count, int selected,
                      const char *sort_hint_price, const char *sort_hint_change);

/**
 * @brief Map a mouse Y coordinate to a price board row index.
 *
 * @param[in] mouse_y Screen Y coordinate from ncurses mouse event.
 * @param[in] total_rows Total ticker rows available.
 * @return Row index within [0, total_rows), or -1 if the click was outside the list.
 */
int ui_price_board_hit_test_row(int mouse_y, int total_rows);

/**
 * @brief Map a mouse X coordinate to a candle index in the current chart viewport.
 *
 * @param[in] mouse_x Screen X coordinate from ncurses mouse event.
 * @param[in] total_points Total candle count in the series.
 * @return Candle index within [0, total_points), or -1 if outside the chart area.
 */
int ui_chart_hit_test_index(int mouse_x, int total_points);

/**
 * @brief Render the candlestick chart view.
 *
 * @param[in] symbol Trading pair symbol to display.
 * @param[in] count Number of elements in @p points.
 * @param[in] points Array of historical price points.
 * @param[in] period Time interval label for the chart.
 * @param[in] selected_index Selected candle index within @p points.
 */
void draw_chart(const char *restrict symbol, int count,
                PricePoint points[count], Period period, int selected_index);

/**
 * @brief Reset cached chart viewport metrics (used when leaving chart mode).
 */
void ui_chart_reset_viewport(void);

/**
 * @brief Update the footer status panel state.
 */
void ui_set_status_panel_state(StatusPanelState state);

/**
 * @brief Read a key press from the UI.
 *
 * The UI may be configured with a timeout; in that case this can return
 * ERR when no input is available.
 *
 * @return Key code (ncurses) or ERR on timeout.
 */
int handle_input(void);
///@}

#endif // CTICKER_H
