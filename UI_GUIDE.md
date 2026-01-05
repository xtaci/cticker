# CTicker User Interface

## Main Screen

The main screen displays a real-time price board with the following layout:

```
┌──────────────────────────────────────────────────────────────────────┐
│  CTicker - Cryptocurrency Price Board      2026-01-03 14:49:51      │
│                                                                      │
│  Symbol                    Price      Change 24h                    │
│  ──────────────────────────────────────────────────────────────────  │
│  BTCUSDT               42,123.45         +2.34%                      │
│  ETHUSDT                2,234.56         +1.23%                      │
│  BNBUSDT                  234.56         -0.45%                      │
│  ADAUSDT                    0.45         +3.21%                      │
│                                                                      │
│                                                                      │
│                                                                      │
│  Keys: ↑/↓ Navigate | Enter: View Chart | q: Quit                   │
└──────────────────────────────────────────────────────────────────────┘
```

### Features:
- **Selected Row**: Highlighted with inverted colors (black text on white background)
- **Price Changes**: 
  - Green text for positive changes (+)
  - Red text for negative changes (-)
- **Real-time Updates**: Prices refresh automatically every 5 seconds

### Controls:
- `↑` (Up Arrow): Move selection up
- `↓` (Down Arrow): Move selection down
- `Enter`: View price chart for selected symbol
- `q`: Quit application

## Chart Screen

When you press Enter on a symbol, you see the price chart:

```
┌──────────────────────────────────────────────────────────────────────┐
│  BTCUSDT - 1 Day Chart                                               │
│                                                                      │
│   42,500.00                                                          │
│                                      │                               │
│                                 │    │                               │
│   42,250.00            │   │    │    │    │                          │
│                        │   │    │    │    │    │                     │
│                   │    │   │    │    │    │    │    │                │
│   42,000.00  │    │    │   │    │    │    │    │    │    │           │
│              │    │    │   │    │    │    │    │    │    │           │
│         │    │    │    │   │    │    │    │    │    │    │    │      │
│   41,750.00 │    │    │   │    │    │    │    │    │    │    │      │
│         │    │    │    │   │    │    │    │    │    │    │    │      │
│                                                                      │
│  Current Price: 42,123.45    Change: +2.34%                          │
│                                                                      │
│  Keys: 1: 1 Day | 7: 1 Week | 30: 1 Month | ESC/q: Back             │
└──────────────────────────────────────────────────────────────────────┘
```

### Features:
- **ASCII Line Chart**: Visual representation of price movement
- **Color-coded Bars**:
  - Green: Price going up
  - Red: Price going down
- **Y-axis Labels**: Price scale on the left
- **Current Price**: Displayed at the bottom
- **Price Change**: Percentage change since period start

### Controls:
- `1`: Switch to 1-day chart (15-minute intervals, 96 data points)
- `7`: Switch to 1-week chart (1-hour intervals, 168 data points)
- `30` or `3` + `0`: Switch to 1-month chart (4-hour intervals, 180 data points)
- `ESC` or `q`: Return to main screen

## Terminal Requirements

- Minimum terminal size: 80 columns × 24 rows
- Color support recommended
- UTF-8 encoding recommended for proper character display
- Modern terminal emulator (e.g., gnome-terminal, iTerm2, Windows Terminal)

## Example Workflow

1. **Start the application**:
   ```bash
   ./cticker
   ```

2. **Navigate the list**:
   - Press `↓` to move down
   - Press `↑` to move up

3. **View a chart**:
   - Navigate to your desired symbol
   - Press `Enter`

4. **Switch time periods**:
   - Press `1` for 1-day view
   - Press `7` for 1-week view
   - Press `30` for 1-month view

5. **Return to main screen**:
   - Press `ESC` or `q`

6. **Exit the application**:
   - From the main screen, press `q`
