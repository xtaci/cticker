# CTicker - Cryptocurrency TUI Ticker

A Terminal User Interface (TUI) based cryptocurrency ticker written in pure C. CTicker connects to the Binance API to display real-time prices for your chosen trading pairs with beautiful ASCII charts.

![CTicker Screenshot](https://via.placeholder.com/800x400.png?text=CTicker+Screenshot)

## Features

- 📊 **Real-time Price Updates**: Live cryptocurrency prices from Binance
- 💰 **Custom Portfolio**: Select and save your favorite trading pairs
- 📈 **Price Charts**: View historical price data with ASCII charts
- ⏱️ **Multiple Time Periods**: 1 day, 1 week, and 1 month views
- 🎨 **Beautiful TUI**: Clean terminal interface with color-coded price changes
- 💾 **Persistent Configuration**: Your portfolio is saved in your home directory

## Requirements

CTicker requires the following libraries:

- `libcurl` - For HTTP requests to Binance API
- `libjansson` - For JSON parsing
- `ncurses` - For the terminal user interface
- `pthread` - For multi-threading (usually included with gcc)

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get install libcurl4-openssl-dev libjansson-dev libncurses5-dev build-essential
```

**Fedora/RHEL/CentOS:**
```bash
sudo dnf install libcurl-devel jansson-devel ncurses-devel gcc make
```

**macOS (with Homebrew):**
```bash
brew install curl jansson ncurses
```

**Arch Linux:**
```bash
sudo pacman -S curl jansson ncurses
```

## Building

1. Clone the repository:
```bash
git clone https://github.com/xtaci/cticker.git
cd cticker
```

2. Check dependencies (optional):
```bash
make check-deps
```

3. Build the project:
```bash
make
```

4. (Optional) Install system-wide:
```bash
sudo make install
```

## Usage

### Running CTicker

Simply run the executable:
```bash
./cticker
```

Or if installed system-wide:
```bash
cticker
```

### First Run

On the first run, CTicker will create a default configuration file at `~/.cticker.conf` with three default trading pairs:
- BTCUSDT (Bitcoin/USDT)
- ETHUSDT (Ethereum/USDT)
- BNBUSDT (Binance Coin/USDT)

### Keyboard Controls

**Main Screen:**
- `↑` / `↓` - Navigate through trading pairs
- `Enter` - View price chart for selected pair
- `q` - Quit application

**Chart Screen:**
- `1` - Show 1-day chart (15-minute intervals)
- `7` - Show 1-week chart (1-hour intervals)
- `30` - Show 1-month chart (4-hour intervals)
- `ESC` / `q` - Return to main screen

### Customizing Your Portfolio

Edit the configuration file at `~/.cticker.conf` to add or remove trading pairs:

```bash
nano ~/.cticker.conf
```

Add one trading pair per line (use Binance symbol format):
```
BTCUSDT
ETHUSDT
BNBUSDT
ADAUSDT
SOLUSDT
DOTUSDT
```

**Valid Symbol Format:**
- Use Binance trading pair symbols (e.g., BTCUSDT, ETHUSDT)
- Symbols are case-sensitive (use uppercase)
- Maximum 50 symbols supported

## Configuration File

The configuration file is stored at `~/.cticker.conf` and contains a simple list of trading pair symbols, one per line.

Example:
```
BTCUSDT
ETHUSDT
BNBUSDT
ADAUSDT
SOLUSDT
```

## Features in Detail

### Real-time Price Board

The main screen displays:
- Symbol name
- Current price (formatted with K/M/B suffixes for large numbers)
- 24-hour price change percentage (color-coded: green for positive, red for negative)
- Current date and time

### Price Charts

Press `Enter` on any trading pair to view its price chart:
- ASCII line chart showing price movement
- Y-axis with price labels
- Color-coded chart (green for upward movement, red for downward)
- Current price and total change percentage
- Switchable time periods (1 day, 1 week, 1 month)

## Technical Details

- Written in pure C for performance and efficiency
- Uses ncurses for terminal UI rendering
- Multi-threaded design for non-blocking UI
- Connects to Binance REST API v3
- Auto-refreshes data every 5 seconds
- Thread-safe data handling with mutexes

## API Usage

CTicker uses the following Binance API endpoints:

- **24-Hour Ticker**: `/api/v3/ticker/24hr` - For real-time prices and 24h changes
- **Kline/Candlestick Data**: `/api/v3/klines` - For historical price data

No API key is required as we only use public endpoints.

## Troubleshooting

**"Failed to load configuration"**
- Ensure you have write permissions to your home directory
- The file will be auto-created on first run

**"Failed to fetch data" or network errors**
- Check your internet connection
- Verify that api.binance.com is accessible
- Some networks may block Binance API access

**Display issues**
- Ensure your terminal supports colors
- Try resizing your terminal window (minimum 80x24 recommended)
- Use a modern terminal emulator

**Build errors**
- Verify all dependencies are installed using `make check-deps`
- Check that pkg-config is installed
- Ensure you have a C compiler (gcc or clang)

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

See the LICENSE file for details.

## Disclaimer

This software is for informational purposes only. Cryptocurrency trading carries risk. Always do your own research before making investment decisions.

## Credits

- Developed by the CTicker team
- Uses data from Binance API
- Built with ncurses, libcurl, and jansson