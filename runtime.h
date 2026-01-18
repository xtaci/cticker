#ifndef CTICKER_RUNTIME_H
#define CTICKER_RUNTIME_H

#include <stdbool.h>
#include <pthread.h>
#include "cticker.h"

/**
 * @brief Shared runtime state for the application.
 *
 * Responsibilities:
 * - Own shared ticker buffers used by the UI and fetch thread.
 * - Manage lifecycle of the fetch thread and UI initialization.
 * - Provide a single place for shutdown signaling.
 */
typedef struct {
    /** Mutex protecting shared ticker updates. */
    pthread_mutex_t data_mutex;
    /** Latest ticker data (shared between fetch and UI). */
    TickerData *global_tickers;
    /** Snapshot buffer used by the UI thread. */
    TickerData *ticker_snapshot;
    /** Snapshot index map used for sorting. */
    int *ticker_snapshot_order;
    /** Number of tracked symbols. */
    int ticker_count;
    /** Loaded configuration (kept alive for the fetch thread). */
    Config config;
    /** Background fetch thread handle. */
    pthread_t fetch_thread;
} RuntimeContext;

/**
 * @brief Install signal handlers and initialize runtime state.
 */
void runtime_setup_signal_handlers(void);

/**
 * @brief Query whether the application should keep running.
 */
bool runtime_is_running(void);

/**
 * @brief Request a graceful shutdown.
 */
void runtime_request_shutdown(void);

/**
 * @brief Initialize config, UI, shared buffers, and start the fetch thread.
 * @return 0 on success, -1 on failure.
 */
int runtime_init(RuntimeContext *ctx);

/**
 * @brief Stop the worker thread and release UI/resources.
 */
void runtime_shutdown(RuntimeContext *ctx);

#endif
