#ifndef CHART_H
#define CHART_H

#include <stdbool.h>
#include <pthread.h>
#include "cticker.h"

typedef struct {
    /** Mutex guarding shared ticker snapshot updates. */
    pthread_mutex_t *data_mutex;
    /** Shared latest ticker rows (owned by main runtime). */
    TickerData *global_tickers;
    /** Pointer to current ticker count (owned by main runtime). */
    int *ticker_count;
} ChartContext;

bool chart_open(const ChartContext *ctx,
                int symbol_index,
                Period current_period,
                PricePoint *chart_points[static 1],
                int chart_count[static 1],
                char chart_symbol[static 1],
                int chart_cursor_idx[static 1],
                int chart_symbol_index[static 1]);

void chart_close(bool show_chart[static 1],
                 PricePoint *chart_points[static 1],
                 int chart_count[static 1],
                 int chart_cursor_idx[static 1],
                 char chart_symbol[static 1],
                 int chart_symbol_index[static 1]);

void chart_refresh_if_expired(const ChartContext *ctx,
                              char chart_symbol[static 1],
                              Period current_period,
                              PricePoint *chart_points[static 1],
                              int chart_count[static 1],
                              int chart_cursor_idx[static 1]);

void chart_force_refresh(const ChartContext *ctx,
                         char chart_symbol[static 1],
                         Period current_period,
                         PricePoint *chart_points[static 1],
                         int chart_count[static 1],
                         int chart_cursor_idx[static 1],
                         bool follow_latest);

void chart_apply_live_price(const ChartContext *ctx,
                            const char symbol[static 1],
                            PricePoint points[static 1],
                            int chart_count,
                            int chart_symbol_index);

void chart_handle_input(int ch,
                        const ChartContext *ctx,
                        char chart_symbol[static 1],
                        Period current_period[static 1],
                        PricePoint *chart_points[static 1],
                        int chart_count[static 1],
                        int chart_cursor_idx[static 1],
                        bool show_chart[static 1],
                        bool follow_latest[static 1],
                        int chart_symbol_index[static 1]);

void chart_handle_mouse(const ChartContext *ctx,
                        const MEVENT ev,
                        char chart_symbol[static 1],
                        Period current_period[static 1],
                        PricePoint *chart_points[static 1],
                        int chart_count[static 1],
                        int chart_cursor_idx[static 1],
                        bool show_chart[static 1],
                        bool follow_latest[static 1],
                        int chart_symbol_index[static 1]);

#endif
