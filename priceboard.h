#ifndef PRICEBOARD_H
#define PRICEBOARD_H

#include <stdbool.h>
#include <pthread.h>
#include <ncursesw/ncurses.h>
#include "cticker.h"
#include "chart.h"

typedef enum {
    /** Default order (config order). */
    SORT_FIELD_DEFAULT = 0,
    /** Sort by last traded price. */
    SORT_FIELD_PRICE,
    /** Sort by 24h change percent. */
    SORT_FIELD_CHANGE,
} PriceboardSortField;

typedef struct {
    /** Mutex guarding shared ticker updates. */
    pthread_mutex_t *data_mutex;
    /** Shared latest ticker rows (owned by main runtime). */
    TickerData *global_tickers;
    /** Local render snapshot buffer (owned by main runtime). */
    TickerData *ticker_snapshot;
    /** Local order map for snapshot rows. */
    int *ticker_snapshot_order;
    /** Pointer to current ticker count (owned by main runtime). */
    int *ticker_count;
} PriceboardContext;

void priceboard_clamp_selected(const PriceboardContext *ctx, int selected[static 1]);

int priceboard_resolve_symbol_index(const PriceboardContext *ctx, int display_index);

void priceboard_cycle_sort(PriceboardSortField field);

const char *priceboard_next_sort_hint(PriceboardSortField field);

void priceboard_render(const PriceboardContext *ctx, int selected);

bool priceboard_handle_input(const PriceboardContext *ctx,
                             int ch,
                             int selected[static 1],
                             Period current_period,
                             bool show_chart[static 1],
                             PricePoint *chart_points[static 1],
                             int chart_count[static 1],
                             char chart_symbol[static 1],
                             int chart_cursor_idx[static 1],
                             int chart_symbol_index[static 1],
                             const ChartContext *chart_ctx);

void priceboard_handle_mouse(const PriceboardContext *ctx,
                             const MEVENT ev,
                             int selected[static 1],
                             Period current_period,
                             bool show_chart[static 1],
                             PricePoint *chart_points[static 1],
                             int chart_count[static 1],
                             char chart_symbol[static 1],
                             int chart_cursor_idx[static 1],
                             int chart_symbol_index[static 1],
                             const ChartContext *chart_ctx);

#endif
