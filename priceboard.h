#ifndef PRICEBOARD_H
#define PRICEBOARD_H

#include <stdbool.h>
#include <pthread.h>
#if defined(__has_include)
#  if __has_include(<ncursesw/ncurses.h>)
#    include <ncursesw/ncurses.h>
#  elif __has_include(<ncurses.h>)
#    include <ncurses.h>
#  else
#    error "ncurses headers not found"
#  endif
#else
#  include <ncurses.h>
#endif

#ifndef BUTTON4_PRESSED
#define BUTTON4_PRESSED 0
#endif

#ifndef BUTTON5_PRESSED
#define BUTTON5_PRESSED 0
#endif
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

void priceboard_clamp_selected(const PriceboardContext *ctx, int *selected);

int priceboard_resolve_symbol_index(const PriceboardContext *ctx, int display_index);

void priceboard_cycle_sort(PriceboardSortField field);

const char *priceboard_next_sort_hint(PriceboardSortField field);

void priceboard_render(const PriceboardContext *ctx, int selected);

bool priceboard_handle_input(const PriceboardContext *ctx,
                             int ch,
                             int *selected,
                             Period current_period,
                             bool *show_chart,
                             PricePoint **chart_points,
                             int *chart_count,
                             char *chart_symbol,
                             int *chart_cursor_idx,
                             int *chart_symbol_index,
                             const ChartContext *chart_ctx);

void priceboard_handle_mouse(const PriceboardContext *ctx,
                             const MEVENT ev,
                             int *selected,
                             Period current_period,
                             bool *show_chart,
                             PricePoint **chart_points,
                             int *chart_count,
                             char *chart_symbol,
                             int *chart_cursor_idx,
                             int *chart_symbol_index,
                             const ChartContext *chart_ctx);

#endif
