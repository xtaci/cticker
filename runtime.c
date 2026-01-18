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
 * @file runtime.c
 * @brief Runtime lifecycle management and shutdown signaling.
 *
 * This module owns:
 * - the global "running" flag shared by threads
 * - signal handling for clean shutdown
 * - initialization of UI, buffers, mutex, and fetch thread
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdatomic.h>
#include "runtime.h"
#include "fetcher.h"
#include "cticker.h"

// Global running flag shared between main/UI and fetch thread.
static _Atomic bool running = true;

// Minimal signal handler: only flip a flag (async-signal-safe).
static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        atomic_store_explicit(&running, false, memory_order_relaxed);
    }
}

// Register SIGINT/SIGTERM to request a clean shutdown.
void runtime_setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

// Read-only accessor used by UI loop and fetch thread.
bool runtime_is_running(void) {
    return atomic_load_explicit(&running, memory_order_relaxed);
}

// Explicit shutdown request (e.g., when user presses Q).
void runtime_request_shutdown(void) {
    atomic_store_explicit(&running, false, memory_order_relaxed);
}

// Allocate buffers, initialize UI/mutex, and start fetch thread.
int runtime_init(RuntimeContext *ctx) {
    if (!ctx) {
        return -1;
    }

    if (load_config(&ctx->config) != 0) {
        fprintf(stderr, "Failed to load configuration\n");
        return -1;
    }

    if (ctx->config.symbol_count == 0) {
        fprintf(stderr, "No symbols configured\n");
        return -1;
    }

    ctx->ticker_count = ctx->config.symbol_count;
    ctx->global_tickers = calloc(ctx->ticker_count, sizeof(TickerData));
    if (!ctx->global_tickers) {
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    ctx->ticker_snapshot = malloc((size_t)ctx->ticker_count * sizeof(TickerData));
    if (!ctx->ticker_snapshot) {
        free(ctx->global_tickers);
        ctx->global_tickers = NULL;
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    ctx->ticker_snapshot_order = malloc((size_t)ctx->ticker_count * sizeof(int));
    if (!ctx->ticker_snapshot_order) {
        free(ctx->ticker_snapshot);
        ctx->ticker_snapshot = NULL;
        free(ctx->global_tickers);
        ctx->global_tickers = NULL;
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }

    if (pthread_mutex_init(&ctx->data_mutex, NULL) != 0) {
        free(ctx->ticker_snapshot_order);
        ctx->ticker_snapshot_order = NULL;
        free(ctx->ticker_snapshot);
        ctx->ticker_snapshot = NULL;
        free(ctx->global_tickers);
        ctx->global_tickers = NULL;
        fprintf(stderr, "Failed to initialize mutex\n");
        return -1;
    }

    init_ui();
    draw_splash_screen();

    if (pthread_create(&ctx->fetch_thread, NULL, fetcher_thread_main, ctx) != 0) {
        cleanup_ui();
        pthread_mutex_destroy(&ctx->data_mutex);
        free(ctx->ticker_snapshot_order);
        ctx->ticker_snapshot_order = NULL;
        free(ctx->ticker_snapshot);
        ctx->ticker_snapshot = NULL;
        free(ctx->global_tickers);
        ctx->global_tickers = NULL;
        fprintf(stderr, "Failed to create fetch thread\n");
        return -1;
    }

    fetcher_initial_fetch(ctx);
    return 0;
}

// Join worker thread and release all runtime resources.
void runtime_shutdown(RuntimeContext *ctx) {
    if (!ctx) {
        return;
    }

    pthread_join(ctx->fetch_thread, NULL);
    cleanup_ui();
    pthread_mutex_destroy(&ctx->data_mutex);
    free(ctx->global_tickers);
    ctx->global_tickers = NULL;
    free(ctx->ticker_snapshot_order);
    ctx->ticker_snapshot_order = NULL;
    free(ctx->ticker_snapshot);
    ctx->ticker_snapshot = NULL;
    ctx->ticker_count = 0;
}
