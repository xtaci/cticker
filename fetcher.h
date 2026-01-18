#ifndef CTICKER_FETCHER_H
#define CTICKER_FETCHER_H

#include "runtime.h"

/**
 * @brief Run an initial synchronous fetch so the first paint has data.
 *
 * This call blocks the UI briefly but ensures the first render
 * shows real prices instead of an empty board.
 */
int fetcher_initial_fetch(RuntimeContext *ctx);

/**
 * @brief Background worker thread entry point.
 *
 * Periodically fetches ticker data and publishes updates under the
 * runtime mutex. Uses runtime_is_running() for shutdown coordination.
 */
void *fetcher_thread_main(void *arg);

#endif
