# Copilot instructions (cticker)

## Project overview
- CTicker is a pure-C TUI crypto ticker for Binance (ncursesw + libcurl + jansson + pthread).
- Core modules: config (watchlist) → background fetch thread (API) → UI renders from a snapshot.

## Build / run / test
- Dependencies: `libcurl`, `jansson`, `ncursesw`, `pthread` (see `README.md`).
- Build: `make check-deps` then `make` (links with `-lcurl -ljansson -lncursesw -lm -lpthread`; see `Makefile`).
- Run: `./cticker` (needs network access to `https://api.binance.com`).
- Install: `sudo make install` (copies `cticker` to `/usr/local/bin/`).
- Config-only test: `./test.sh` (overrides `HOME=/tmp/cticker_test` and compiles a small `test_config.c`; see `test.sh`).
- Debug build (override Make vars): `make clean && make CFLAGS='-Wall -Wextra -O0 -g -pthread'`.

## Architecture & data flow (read these first)
- Types and APIs: `cticker.h` defines `TickerData`, `PricePoint`, `Config`, `Period` and public functions.
- Entry point + orchestration: `main.c`
  - Shared state: `global_tickers` protected by `data_mutex`.
  - Fetch thread (`thread_data_fetch`) periodically calls `fetch_ticker_data()` and copies only updated rows under the mutex.
  - UI thread copies a snapshot (`ticker_snapshot`) under the mutex, then calls ncurses drawing WITHOUT holding the lock.
- Networking/JSON: `api.c`
  - `fetch_ticker_data(symbol, &out)` fills a caller-owned `TickerData`.
  - `fetch_historical_data(symbol, period, &points, &count)` allocates `*points`; caller must `free(*points)`.
- UI rendering + input: `ui.c`
  - `init_ui()` sets `timeout(1000)` and mouse masks; input is via `handle_input()` (wraps `wgetch`).
  - Hit-testing helpers: `ui_price_board_hit_test_row()` and `ui_chart_hit_test_index()`.

## Project-specific conventions (important)
- Thread-safety rule: never call ncurses while holding `data_mutex`; take a local copy first (see `priceboard_render()` in `main.c`).
- Memory ownership: if a function allocates (notably `fetch_historical_data()`), the caller frees; `main.c` centralizes chart buffer lifecycle (`chart_reload_data()` / `chart_reset_state()`).
- Config file format: `$HOME/.cticker.conf` (`CONFIG_FILE`), one symbol per line; empty lines and `#` comments ignored (see `config.c`).
- Limits: `MAX_SYMBOLS`=50 and `MAX_SYMBOL_LEN`=20 are hard caps; keep new features within these constraints.

## Where to change things (examples)
- Add a new ticker field from Binance `/api/v3/ticker/24hr`:
  1) Extend `TickerData` in `cticker.h`
  2) Parse it in `fetch_ticker_data()` in `api.c`
  3) Render it in `draw_main_screen()` in `ui.c`
- Add/adjust chart periods:
  - Enum: `Period` in `cticker.h`
  - Request mapping: `get_interval_params()` in `api.c`
  - Labeling: `period_label()` in `ui.c`
  - Interaction: chart input in `main.c` (`SPACE`/mouse wheel changes period).
