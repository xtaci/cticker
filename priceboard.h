#ifndef PRICEBOARD_H
#define PRICEBOARD_H

/**
 * @file priceboard.h
 * @brief Price board data management, sorting, and input API.
 *
 * The price board is the main screen showing all watched symbols.
 * This module manages sort state, snapshot creation, and user interaction;
 * actual ncurses rendering is in ui_priceboard.c.
 */

#include <stdbool.h>
#include <pthread.h>
#include "ncurses_compat.h"
#include "cticker.h"
#include "chart.h"

/** @brief Column sort selector for the price board. */
typedef enum {
    /** Default order (config order). */
    SORT_FIELD_DEFAULT = 0,
    /** Sort by last traded price. */
    SORT_FIELD_PRICE,
    /** Sort by 24h change percent. */
    SORT_FIELD_CHANGE,
} PriceboardSortField;

/**
 * @brief Shared state needed by price board operations.
 *
 * Snapshot buffers are filled from global_tickers under the mutex,
 * then sorted and rendered without holding the lock.
 */
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

/** @name Price board helpers */
///@{

/** @brief Clamp the selected index to valid bounds. */
void priceboard_clamp_selected(const PriceboardContext *ctx, int *selected);

/** @brief Map a display row back to the original config index. */
int priceboard_resolve_symbol_index(const PriceboardContext *ctx, int display_index);

/** @brief Cycle sort direction for the given field. */
void priceboard_cycle_sort(PriceboardSortField field);

/** @brief Return the arrow hint for the next sort state. */
const char *priceboard_next_sort_hint(PriceboardSortField field);

///@}

/** @name Price board rendering and input */
///@{

/** @brief Build a snapshot and render the price board. */
void priceboard_render(const PriceboardContext *ctx, int selected);

/** @brief Handle keyboard input on the price board. */
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

/** @brief Handle mouse input on the price board. */
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

///@}

#endif
