# CTicker Developer Documentation

## Architecture Overview

CTicker is a multi-threaded application with a clean separation of concerns:

```
┌─────────────────────────────────────────────────────┐
│                    main.c                           │
│         (Main loop, threading, coordination)        │
└──────────┬───────────────────────┬──────────────────┘
           │                       │
           ▼                       ▼
┌──────────────────┐    ┌─────────────────────┐
│     config.c     │    │       api.c         │
│  (Config I/O)    │    │ (Binance API calls) │
└──────────────────┘    └─────────────────────┘
                                   │
                                   ▼
                        ┌─────────────────────┐
                        │       ui.c          │
                        │  (ncurses display)  │
                        └─────────────────────┘
```

## Components

### cticker.h
Global header file with:
- Data structure definitions (TickerData, PricePoint, Config)
- Enums (Period)
- Function declarations
- Constants (MAX_SYMBOLS, MAX_SYMBOL_LEN, etc.)

### config.c
Configuration management:
- `load_config()`: Loads symbols from ~/.cticker.conf
- `save_config()`: Saves symbols to ~/.cticker.conf
- Creates default configuration if file doesn't exist

### api.c
Binance API integration:
- `fetch_ticker_data()`: Fetches 24-hour ticker data for a symbol
- `fetch_historical_data()`: Fetches kline/candlestick data
- Uses libcurl for HTTP requests
- Uses jansson for JSON parsing

API Endpoints used:
- `/api/v3/ticker/24hr` - Real-time price and 24h statistics
- `/api/v3/klines` - Historical candlestick data

### ui.c
Terminal user interface with ncurses:
- `init_ui()`: Initialize ncurses and color pairs
- `cleanup_ui()`: Clean up ncurses resources
- `draw_main_screen()`: Draw the main price board
- `draw_chart()`: Draw ASCII price chart
- `handle_input()`: Handle keyboard input

Color scheme:
- Green: Positive price changes
- Red: Negative price changes
- Cyan: Headers
- Black on White: Selected row

### main.c
Application entry point and main loop:
- Signal handling for clean exit
- Thread creation for background data fetching
- Main event loop
- State management (chart vs main screen)

## Threading Model

The application uses two threads:

1. **Main Thread**: Handles UI rendering and user input
2. **Fetch Thread**: Periodically fetches data from Binance API

Thread synchronization:
- `pthread_mutex_t data_mutex`: Protects global_tickers array
- Lock acquired when updating or reading ticker data
- Prevents race conditions between UI and data fetching

## Data Flow

```
User starts app
    ↓
Load config from ~/.cticker.conf
    ↓
Create UI and fetch thread
    ↓
┌───────────────────────────┐
│   Main Loop               │
│                           │
│  ┌────────────────────┐   │
│  │ Fetch Thread       │   │
│  │ (every 5 seconds)  │   │
│  │                    │   │
│  │ Lock mutex        │   │
│  │ Fetch data        │   │
│  │ Update tickers    │   │
│  │ Unlock mutex      │   │
│  └────────────────────┘   │
│                           │
│  ┌────────────────────┐   │
│  │ Main Thread        │   │
│  │ (every 1 second)   │   │
│  │                    │   │
│  │ Lock mutex        │   │
│  │ Copy data         │   │
│  │ Unlock mutex      │   │
│  │ Draw UI           │   │
│  │ Handle input      │   │
│  └────────────────────┘   │
│                           │
└───────────────────────────┘
    ↓
User quits
    ↓
Join threads and cleanup
```

## State Machine

The application has two main states:

### Main Screen State
- Shows list of trading pairs
- Displays price and 24h change
- Navigation: Up/Down arrows
- Actions: Enter (chart), q (quit)

### Chart Screen State
- Shows price chart for selected symbol
- Default period: 1 day
- Actions:
  - 1: Switch to 1-day chart
  - 7: Switch to 1-week chart
  - 30: Switch to 1-month chart
  - ESC/q: Return to main screen

## Building and Development

### Build Process
```bash
make clean    # Remove all object files and binary
make          # Compile the project
make install  # Install to /usr/local/bin
```

### Adding New Features

**To add a new data field to the ticker:**
1. Add field to `TickerData` struct in cticker.h
2. Parse field in `fetch_ticker_data()` in api.c
3. Display field in `draw_main_screen()` in ui.c

**To add a new chart period:**
1. Add enum value to `Period` in cticker.h
2. Add case in `get_interval_params()` in api.c
3. Add keyboard handler in main.c chart mode

**To add a new API endpoint:**
1. Add function declaration in cticker.h
2. Implement function in api.c
3. Call function from main.c or ui.c as needed

## Memory Management

- **global_tickers**: Allocated in main(), freed on exit
- **chart_points**: Allocated in fetch_historical_data(), freed when exiting chart mode
- **response.data**: Allocated during API calls, freed after parsing
- **JSON objects**: Reference counted, freed with json_decref()

## Error Handling

- Functions return 0 on success, -1 on error
- Network errors are handled gracefully (display shows last known data)
- Missing config file creates default configuration
- Invalid JSON responses are handled with null checks

## Code Style

- K&R brace style
- 4-space indentation
- Snake_case for functions and variables
- UPPER_CASE for constants and macros
- No trailing whitespace

## Testing

Run the test suite:
```bash
./test.sh
```

The test suite covers:
- Configuration loading and saving
- Default configuration creation
- Configuration reloading

## Future Enhancements

Potential areas for improvement:
- WebSocket support for real-time updates
- Support for more exchanges (Coinbase, Kraken, etc.)
- Candlestick chart display
- Volume indicators
- Portfolio value tracking
- Alert system for price thresholds
- Export historical data to CSV
