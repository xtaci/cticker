#ifndef CHART_H
#define CHART_H

/**
 * @file chart.h
 * @brief Chart data lifecycle and user-interaction API.
 *
 * This module manages candlestick buffers and translates user input into
 * chart navigation (scrolling, period changes, cursor movement).
 * All ncurses rendering is delegated to ui_chart.c.
 */

#include <stdbool.h>
#include <pthread.h>
#include "cticker.h"

/**
 * @brief Shared state needed by chart operations.
 *
 * The chart module reads live ticker data under this mutex to overlay
 * the current price on the latest candle.
 */
typedef struct {
    /** Mutex guarding shared ticker snapshot updates. */
    pthread_mutex_t *data_mutex;
    /** Shared latest ticker rows (owned by main runtime). */
    TickerData *global_tickers;
    /** Pointer to current ticker count (owned by main runtime). */
    int *ticker_count;
} ChartContext;

/** @name Chart lifecycle */
///@{

/**
 * @brief Open the chart for a given symbol index.
 * @return true if chart data was loaded successfully.
 */
bool chart_open(const ChartContext *ctx,
                int symbol_index,
                Period current_period,
                PricePoint **chart_points,
                int *chart_count,
                char *chart_symbol,
                int *chart_cursor_idx,
                int *chart_symbol_index);

/**
 * @brief Close the chart and release associated buffers.
 */
void chart_close(bool *show_chart,
                 PricePoint **chart_points,
                 int *chart_count,
                 int *chart_cursor_idx,
                 char *chart_symbol,
                 int *chart_symbol_index);

///@}

/** @name Chart data refresh */
///@{

/**
 * @brief Reload candles if the last candle has expired.
 */
void chart_refresh_if_expired(const ChartContext *ctx,
                              char *chart_symbol,
                              Period current_period,
                              PricePoint **chart_points,
                              int *chart_count,
                              int *chart_cursor_idx);

/**
 * @brief Force an immediate candle reload.
 */
void chart_force_refresh(const ChartContext *ctx,
                         char *chart_symbol,
                         Period current_period,
                         PricePoint **chart_points,
                         int *chart_count,
                         int *chart_cursor_idx,
                         bool follow_latest);

/**
 * @brief Patch the latest candle with the live ticker price.
 */
void chart_apply_live_price(const ChartContext *ctx,
                            const char *symbol,
                            PricePoint *points,
                            int chart_count,
                            int chart_symbol_index);

///@}

/** @name Chart user input */
///@{

/**
 * @brief Process a keyboard event while the chart is shown.
 */
void chart_handle_input(int ch,
                        const ChartContext *ctx,
                        char *chart_symbol,
                        Period *current_period,
                        PricePoint **chart_points,
                        int *chart_count,
                        int *chart_cursor_idx,
                        bool *show_chart,
                        bool *follow_latest,
                        int *chart_symbol_index);

/**
 * @brief Process a mouse event while the chart is shown.
 */
void chart_handle_mouse(const ChartContext *ctx,
                        const MEVENT ev,
                        char *chart_symbol,
                        Period *current_period,
                        PricePoint **chart_points,
                        int *chart_count,
                        int *chart_cursor_idx,
                        bool *show_chart,
                        bool *follow_latest,
                        int *chart_symbol_index);

///@}

#endif
