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
 * @file fetcher.c
 * @brief Background ticker fetch logic and initial bootstrap fetch.
 *
 * Design notes:
 * - Fetch happens without holding the runtime mutex.
 * - Only publish updated rows under the mutex to keep UI responsive.
 * - Uses runtime_is_running() to cooperate with shutdown requests.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fetcher.h"
#include "cticker.h"
#include "runtime.h"

// Background refresh cadence (seconds).
#define REFRESH_INTERVAL 5

// Fetch all symbols into a scratch buffer without holding the UI lock.
static void fetch_all_symbols(const Config config[static 1],
                              TickerData scratch[static 1],
                              bool updated[static 1],
                              bool had_failure[static 1]) {
    int symbol_count = config->symbol_count;
    *had_failure = false;
    for (int i = 0; i < symbol_count && runtime_is_running(); i++) {
        if (fetch_ticker_data(config->symbols[i], &scratch[i]) == 0) {
            updated[i] = true;
        } else {
            *had_failure = true;
        }
    }
}

// Publish updated rows to the shared ticker buffer under mutex.
static void apply_updated_tickers(RuntimeContext *ctx,
                                  const TickerData scratch[static 1],
                                  const bool updated[static 1]) {
    pthread_mutex_lock(&ctx->data_mutex);
    for (int i = 0; i < ctx->ticker_count; i++) {
        if (updated[i]) {
            ctx->global_tickers[i] = scratch[i];
        }
    }
    pthread_mutex_unlock(&ctx->data_mutex);
}

// Initial synchronous fetch so the first render has data.
int fetcher_initial_fetch(RuntimeContext *ctx) {
    if (!ctx) {
        return -1;
    }

    int symbol_count = ctx->config.symbol_count;
    TickerData *scratch = calloc(symbol_count, sizeof(TickerData));
    bool *updated = calloc(symbol_count, sizeof(bool));
    if (!scratch || !updated) {
        free(scratch);
        free(updated);
        ui_set_status_panel_state(STATUS_PANEL_NETWORK_ERROR);
        return -1;
    }

    ui_set_status_panel_state(STATUS_PANEL_FETCHING);
    bool had_failure = false;
    fetch_all_symbols(&ctx->config, scratch, updated, &had_failure);
    apply_updated_tickers(ctx, scratch, updated);

    free(scratch);
    free(updated);
    ui_set_status_panel_state(had_failure ? STATUS_PANEL_NETWORK_ERROR
                                          : STATUS_PANEL_NORMAL);
    return 0;
}

// Worker thread loop: fetch, publish, update status, sleep.
void *fetcher_thread_main(void *arg) {
    RuntimeContext *ctx = (RuntimeContext *)arg;
    if (!ctx) {
        return NULL;
    }

    int symbol_count = ctx->config.symbol_count;
    TickerData *scratch = calloc(symbol_count, sizeof(TickerData));
    bool *updated = calloc(symbol_count, sizeof(bool));
    if (!scratch || !updated) {
        free(scratch);
        free(updated);
        ui_set_status_panel_state(STATUS_PANEL_NETWORK_ERROR);
        runtime_request_shutdown();
        return NULL;
    }

    while (runtime_is_running()) {
        ui_set_status_panel_state(STATUS_PANEL_FETCHING);
        memset(updated, 0, (size_t)symbol_count * sizeof(bool));
        bool had_failure = false;

        fetch_all_symbols(&ctx->config, scratch, updated, &had_failure);
        apply_updated_tickers(ctx, scratch, updated);

        ui_set_status_panel_state(had_failure ? STATUS_PANEL_NETWORK_ERROR
                                              : STATUS_PANEL_NORMAL);

        for (int i = 0; i < REFRESH_INTERVAL && runtime_is_running(); i++) {
            sleep(1);
        }
    }

    free(scratch);
    free(updated);
    return NULL;
}
