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
 * @file ui.c
 * @brief ncurses-based UI rendering and input handling.
 */

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <locale.h>
#include "cticker.h"

#define COLOR_PAIR_GREEN 1
#define COLOR_PAIR_RED 2
#define COLOR_PAIR_HEADER 3
#define COLOR_PAIR_SELECTED 4
#define COLOR_PAIR_GREEN_BG 5
#define COLOR_PAIR_RED_BG 6
#define COLOR_PAIR_GREEN_SELECTED 7
#define COLOR_PAIR_RED_SELECTED 8
#define COLOR_PAIR_SYMBOL 9
#define COLOR_PAIR_SYMBOL_SELECTED 10
#define COLOR_PAIR_TITLE_BAR 11

#define PRICE_FLICKER_DURATION_MS 500
#define PRICE_CHANGE_EPSILON 1e-9
#define PRICE_COL 18
#define CHANGE_COL 35
#define HIGH_COL 52
#define LOW_COL 70
#define VOLUME_COL 88
#define TRADES_COL 108
#define QUOTE_COL 126

static WINDOW *main_win = NULL;
static bool colors_available = false;
static double last_prices[MAX_SYMBOLS];
static int last_visible_count = 0;
/**
 * @brief Scroll offset (top index) for the main price board.
 *
 * The price board is treated as a viewport:
 * - Header/title occupy fixed rows at the top.
 * - Footer/help occupies fixed rows at the bottom.
 * - The area in-between is a scrollable list of tickers.
 *
 * We store the scroll offset globally so it persists across frames.
 * On every redraw we clamp/adjust it to keep the selected row visible.
 */
static int price_board_scroll_offset = 0;

static void format_number(char *buf, size_t size, double num);
static void format_number_with_commas(char *buf, size_t size, double num);
static void format_integer_with_commas(char *buf, size_t size, long long value);
static void trim_trailing_zeros(char *buf);

static void reset_price_history(void) {
    for (int i = 0; i < MAX_SYMBOLS; ++i) {
        last_prices[i] = NAN;
    }
    last_visible_count = 0;
    // Reset scroll to top when the UI is initialized.
    price_board_scroll_offset = 0;
}

// Render the price column cell with the appropriate color treatment for
// direction, selection state, and the short-lived flicker animation.
static void draw_price_cell(int y, const char *price_str, chtype arrow,
                            bool daily_up, bool row_selected, bool flicker,
                            bool flicker_up) {
    int pair = COLOR_PAIR_GREEN;
    if (colors_available) {
        if (flicker) {
            pair = flicker_up ? COLOR_PAIR_GREEN_BG : COLOR_PAIR_RED_BG;
        } else if (row_selected) {
            pair = daily_up ? COLOR_PAIR_GREEN_SELECTED : COLOR_PAIR_RED_SELECTED;
        } else {
            pair = daily_up ? COLOR_PAIR_GREEN : COLOR_PAIR_RED;
        }
        wattron(main_win, COLOR_PAIR(pair) | A_BOLD);
    }

    // Print arrow separately to keep cell width predictable.
    mvwaddch(main_win, y, PRICE_COL, arrow);
    mvwprintw(main_win, y, PRICE_COL + 1, "%14s", price_str);

    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(pair) | A_BOLD);
    }
}

// Render the 24h change column cell with the correct gain/loss palette.
static void draw_change_cell(int y, const char *change_str, bool change_up,
                             bool row_selected) {
    if (!colors_available) {
        mvwprintw(main_win, y, CHANGE_COL, "%s", change_str);
        return;
    }

    int pair = row_selected
        ? (change_up ? COLOR_PAIR_GREEN_SELECTED : COLOR_PAIR_RED_SELECTED)
        : (change_up ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
    wattron(main_win, COLOR_PAIR(pair) | A_BOLD);
    mvwprintw(main_win, y, CHANGE_COL, "%s", change_str);
    wattroff(main_win, COLOR_PAIR(pair) | A_BOLD);
}

// Draw the floating info box in the top-right corner that mirrors the
// currently selected candle values.
static void draw_info_box(int x, int y, int width, int height,
                          const PricePoint *point) {
    if (!point || width < 10 || height < 6) {
        return;
    }

    int right = x + width - 1;
    int bottom = y + height - 1;

    mvwaddch(main_win, y, x, ACS_ULCORNER);
    mvwaddch(main_win, y, right, ACS_URCORNER);
    mvwaddch(main_win, bottom, x, ACS_LLCORNER);
    mvwaddch(main_win, bottom, right, ACS_LRCORNER);
    mvwhline(main_win, y, x + 1, ACS_HLINE, width - 2);
    mvwhline(main_win, bottom, x + 1, ACS_HLINE, width - 2);
    mvwvline(main_win, y + 1, x, ACS_VLINE, height - 2);
    mvwvline(main_win, y + 1, right, ACS_VLINE, height - 2);

    char open_str[32], high_str[32], low_str[32], close_str[32];
    char volume_str[16], quote_volume_str[16];
    char taker_buy_base_str[16], taker_buy_quote_str[16];
    if (point->open_text[0]) {
        snprintf(open_str, sizeof(open_str), "%s", point->open_text);
    } else {
        format_number(open_str, sizeof(open_str), point->open);
    }
    trim_trailing_zeros(open_str);
    if (point->high_text[0]) {
        snprintf(high_str, sizeof(high_str), "%s", point->high_text);
    } else {
        format_number(high_str, sizeof(high_str), point->high);
    }
    trim_trailing_zeros(high_str);
    if (point->low_text[0]) {
        snprintf(low_str, sizeof(low_str), "%s", point->low_text);
    } else {
        format_number(low_str, sizeof(low_str), point->low);
    }
    trim_trailing_zeros(low_str);
    if (point->close_text[0]) {
        snprintf(close_str, sizeof(close_str), "%s", point->close_text);
    } else {
        format_number(close_str, sizeof(close_str), point->close);
    }
    trim_trailing_zeros(close_str);
    format_number(volume_str, sizeof(volume_str), point->volume);
    format_number(quote_volume_str, sizeof(quote_volume_str), point->quote_volume);
    format_number(taker_buy_base_str, sizeof(taker_buy_base_str), point->taker_buy_base_volume);
    format_number(taker_buy_quote_str, sizeof(taker_buy_quote_str), point->taker_buy_quote_volume);

    double change = (point->open != 0.0)
        ? ((point->close - point->open) / point->open) * 100.0
        : 0.0;
    char change_str[16];
    snprintf(change_str, sizeof(change_str), "%+.2f%%", change);

    time_t ts = (time_t)point->timestamp;
    struct tm tm_buf;
    char open_time_str[32];
    strftime(open_time_str, sizeof(open_time_str), "%Y-%m-%d %H:%M",
             localtime_r(&ts, &tm_buf));
    time_t close_ts = (time_t)point->close_time;
    char close_time_str[32];
    strftime(close_time_str, sizeof(close_time_str), "%Y-%m-%d %H:%M",
             localtime_r(&close_ts, &tm_buf));

    int content_x = x + 2;
    int line = y + 1;
    mvwprintw(main_win, line++, content_x, "Open Time : %s", open_time_str);
    mvwprintw(main_win, line++, content_x, "Close Time: %s", close_time_str);
    mvwprintw(main_win, line++, content_x, "Open : %s", open_str);
    mvwprintw(main_win, line++, content_x, "High : %s", high_str);
    mvwprintw(main_win, line++, content_x, "Low  : %s", low_str);
    mvwprintw(main_win, line++, content_x, "Close: %s", close_str);
    mvwprintw(main_win, line++, content_x, "Vol  : %s", volume_str);
    mvwprintw(main_win, line++, content_x, "Quote Vol: %s", quote_volume_str);
    mvwprintw(main_win, line++, content_x, "Trades   : %d", point->trade_count);
    mvwprintw(main_win, line++, content_x, "Taker Buy (B): %s", taker_buy_base_str);
    mvwprintw(main_win, line++, content_x, "Taker Buy (Q): %s", taker_buy_quote_str);

    if (colors_available) {
        int color = (point->close >= point->open)
            ? COLOR_PAIR(COLOR_PAIR_GREEN)
            : COLOR_PAIR(COLOR_PAIR_RED);
        wattron(main_win, color | A_BOLD);
        mvwprintw(main_win, line, content_x, "Change: %s", change_str);
        wattroff(main_win, color | A_BOLD);
    } else {
        mvwprintw(main_win, line, content_x, "Change: %s", change_str);
    }
}

// Initialize ncurses and prepare the root window plus color palette.
void init_ui(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    // Use a 1s input timeout so the main loop can redraw periodically even
    // without user interaction (prices update in the background thread).
    timeout(1000);
    
    // Initialize colors
    colors_available = has_colors();
    if (colors_available) {
        start_color();
        short selection_bg = COLOR_BLUE;
        init_pair(COLOR_PAIR_GREEN, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_PAIR_RED, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PAIR_HEADER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK, selection_bg);
        init_pair(COLOR_PAIR_GREEN_BG, COLOR_BLACK, COLOR_GREEN);
        init_pair(COLOR_PAIR_RED_BG, COLOR_BLACK, COLOR_RED);
        init_pair(COLOR_PAIR_GREEN_SELECTED, COLOR_GREEN, selection_bg);
        init_pair(COLOR_PAIR_RED_SELECTED, COLOR_RED, selection_bg);
        init_pair(COLOR_PAIR_SYMBOL, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_SYMBOL_SELECTED, COLOR_YELLOW, selection_bg);
        init_pair(COLOR_PAIR_TITLE_BAR, COLOR_MAGENTA, COLOR_BLACK);
    }
    
    main_win = newwin(LINES, COLS, 0, 0);
    keypad(main_win, TRUE);
    // Mirror the input timeout on the main window so handle_input() uses the
    // same cadence regardless of which window is active.
    wtimeout(main_win, 1000);
    reset_price_history();
}

// Tear down ncurses resources so the terminal is restored.
void cleanup_ui(void) {
    if (main_win) {
        delwin(main_win);
    }
    endwin();
}

/**
 * @brief Render a startup splash screen while initial data is loading.
 *
 * This is intentionally lightweight: we draw once and return. The caller
 * should proceed to fetch the first batch of data; once done, the normal
 * price board rendering will overwrite this screen.
 */
void draw_splash_screen(void) {
    if (!main_win) {
        return;
    }

    werase(main_win);

    static const char *art[] = {
        "  _____ _______ _      _             ",
        " / ____|__   __(_)    | |            ",
        "| |       | |   _  ___| | _____ _ __ ",
        "| |       | |  | |/ __| |/ / _ \\ '__|",
        "| |____   | |  | | (__|   <  __/ |   ",
        " \\_____|  |_|  |_|\\___|_|\\_\\___|_|   ",
        NULL
    };

    int art_lines = 0;
    int art_width = 0;
    for (int i = 0; art[i] != NULL; ++i) {
        int len = (int)strlen(art[i]);
        if (len > art_width) art_width = len;
        art_lines++;
    }

    const char *loading1 = "Loading...";
    const char *loading2 = "Fetching data from Binance API";

    int total_lines = art_lines + 2 + 2;  // art + blank + two loading lines
    int start_y = (LINES - total_lines) / 2;
    if (start_y < 0) start_y = 0;

    int start_x = (COLS - art_width) / 2;
    if (start_x < 0) start_x = 0;

    if (colors_available) {
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    }

    int y = start_y;
    for (int i = 0; art[i] != NULL; ++i) {
        mvwaddnstr(main_win, y++, start_x, art[i], COLS - start_x - 1);
    }

    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    }

    y++;  // blank line
    int l1x = (COLS - (int)strlen(loading1)) / 2;
    if (l1x < 0) l1x = 0;
    int l2x = (COLS - (int)strlen(loading2)) / 2;
    if (l2x < 0) l2x = 0;

    if (colors_available) {
        wattron(main_win, A_BOLD);
    }
    mvwaddnstr(main_win, y++, l1x, loading1, COLS - l1x - 1);
    if (colors_available) {
        wattroff(main_win, A_BOLD);
    }
    mvwaddnstr(main_win, y, l2x, loading2, COLS - l2x - 1);

    wrefresh(main_win);
}

// Format a number with a precision that keeps small prices legible.
static void format_number(char *buf, size_t size, double num) {
    if (fabs(num) >= 1.0) {
        snprintf(buf, size, "%.2f", num);
    } else {
        snprintf(buf, size, "%.8f", num);
    }
}

// Trim useless trailing zeros (and the decimal point, if needed) from
// numeric strings. This is applied only at render time so we preserve the
// original API payload elsewhere.
static void trim_trailing_zeros(char *buf) {
    char *dot = strchr(buf, '.');
    if (!dot) {
        return;
    }
    char *end = buf + strlen(buf) - 1;
    while (end > dot && *end == '0') {
        *end-- = '\0';
    }
    if (end == dot) {
        *end = '\0';
    }
}

// Specialized formatter for Y-axis labels so extremely tight ranges still
// show meaningful precision. The decimal depth ramps up as the visible
// range shrinks to highlight subtle price moves.
static void format_axis_price(char *buf, size_t size, double num,
                              double range) {
    int decimals = 2;
    if (range < 0.5) decimals = 4;
    if (range < 0.05) decimals = 6;
    if (range < 0.005) decimals = 8;
    if (range < 0.0005) decimals = 10;
    if (decimals > 10) decimals = 10;
    snprintf(buf, size, "%.*f", decimals, num);
    trim_trailing_zeros(buf);
}

// Apply thousands separators to a numeric string produced by format_number().
static void insert_commas(const char *src, char *dest, size_t dest_size) {
    if (dest_size == 0) {
        return;
    }

    const char *start = src;
    bool negative = false;
    if (*start == '-') {
        negative = true;
        start++;
    }

    const char *dot = strchr(start, '.');
    size_t int_len = dot ? (size_t)(dot - start) : strlen(start);
    size_t out = 0;

    if (negative && out < dest_size - 1) {
        dest[out++] = '-';
    }

    for (size_t i = 0; i < int_len && out < dest_size - 1; ++i) {
        dest[out++] = start[i];
        size_t remaining = int_len - i - 1;
        if (remaining > 0 && remaining % 3 == 0 && out < dest_size - 1) {
            dest[out++] = ',';
        }
    }

    if (dot && out < dest_size - 1) {
        dest[out++] = '.';
        const char *frac = dot + 1;
        while (*frac && out < dest_size - 1) {
            dest[out++] = *frac++;
        }
    }

    dest[out] = '\0';
}

static void format_number_with_commas(char *buf, size_t size, double num) {
    char raw[64];
    format_number(raw, sizeof(raw), num);
    insert_commas(raw, buf, size);
}

static void format_integer_with_commas(char *buf, size_t size, long long value) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%lld", value);
    insert_commas(raw, buf, size);
}

// Translate an enum period into a user-facing label.
static const char* period_label(Period period) {
    switch (period) {
        case PERIOD_1MIN: return "1 Minute";
        case PERIOD_15MIN: return "15 Minutes";
        case PERIOD_1HOUR: return "1 Hour";
        case PERIOD_4HOUR: return "4 Hours";
        case PERIOD_1DAY: return "1 Day";
        case PERIOD_1WEEK: return "1 Week";
        case PERIOD_1MONTH: return "1 Month";
        default: return "Unknown";
    }
}

// Convert a price into a y-coordinate on the chart grid.
static int price_to_row(double price, double min_price, double max_price,
                        int chart_height, int chart_y) {
    double range = max_price - min_price;
    if (range <= 0.0000001) {
        range = 1.0;
    }
    double normalized = (price - min_price) / range;
    if (normalized < 0.0) normalized = 0.0;
    if (normalized > 1.0) normalized = 1.0;
    int usable_height = chart_height - 1;
    if (usable_height < 1) usable_height = 1;
    return chart_y + chart_height - 1 - (int)(normalized * usable_height);
}

// Draw the ticker board listing all configured symbols along with their latest
// price, change, and a transient flicker for updated rows.
void draw_main_screen(TickerData *tickers, int count, int selected) {
    werase(main_win);
    // Layout (screen coordinates):
    //   row 0: title + timestamp
    //   row 2: column headers
    //   row 3: separator line
    //   row 4..N: scrollable ticker list (this is the viewport)
    //   last rows: footer/help text
    //
    // IMPORTANT:
    // The ticker list MUST NOT render over the footer. Therefore we compute
    // the maximum list height dynamically from the current terminal size.
    const int board_start_y = 4;
    const int footer_reserved_rows = 2;  // Footer/help row + safety buffer
    int max_board_height = LINES - footer_reserved_rows - board_start_y;
    if (max_board_height < 1) {
        max_board_height = 1;
    }
    int visible_rows = max_board_height;

    bool show_high = (COLS > HIGH_COL + 10);
    bool show_low = (COLS > LOW_COL + 10);
    bool show_volume = (COLS > VOLUME_COL + 12);
    bool show_trades = (COLS > TRADES_COL + 6);
    bool show_quote = (COLS > QUOTE_COL + 12);

    // Compute and clamp the viewport window (price_board_scroll_offset .. + visible_rows).
    // Policy:
    //   - Clamp selected index within [0, count-1].
    //   - Clamp scroll offset within [0, count-visible_rows].
    //   - Adjust scroll offset so the selected row is always visible.
    if (count <= 0) {
        // No rows; keep state consistent.
        price_board_scroll_offset = 0;
    } else {
        if (selected < 0) {
            selected = 0;
        } else if (selected >= count) {
            selected = count - 1;
        }
        int max_scroll = count - visible_rows;
        if (max_scroll < 0) {
            max_scroll = 0;
        }
        // Ensure the offset is within the legal scrolling range.
        if (price_board_scroll_offset > max_scroll) {
            price_board_scroll_offset = max_scroll;
        }
        // Keep selection visible:
        // - If selection is above the viewport, scroll up to it.
        // - If selection is below the viewport, scroll down until it's the last visible row.
        if (selected < price_board_scroll_offset) {
            price_board_scroll_offset = selected;
        } else if (selected >= price_board_scroll_offset + visible_rows) {
            price_board_scroll_offset = selected - visible_rows + 1;
        }
        if (price_board_scroll_offset < 0) {
            price_board_scroll_offset = 0;
        }
    }

    // Track cells that need to be redrawn after the flicker animation completes.
    typedef struct {
        int y;
        char price_text[32];
        chtype arrow;
        bool daily_up;
        bool row_selected;
        bool price_went_up;
    } PriceFlickerInfo;
    PriceFlickerInfo flicker_queue[MAX_SYMBOLS];
    int flicker_count = 0;
    if (count < last_visible_count) {
        for (int i = count; i < last_visible_count && i < MAX_SYMBOLS; ++i) {
            last_prices[i] = NAN;
        }
    }
    last_visible_count = count;
    
    // Title bar communicates the app name.
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR) | A_BOLD);
    mvwprintw(main_win, 0, 2, "CTICKER >> PRICE BOARD");
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR) | A_BOLD);
    
    // Timestamp on the right keeps the board anchored in real time.
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    mvwprintw(main_win, 0, COLS - strlen(time_str) - 2, "%s", time_str);
    
    // Column headers and a horizontal rule to separate the board.
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER));
    mvwprintw(main_win, 2, 2, "%-15s %15s %15s", "SYMBOL", "PRICE", "CHANGE 24H");
    if (show_high) {
        mvwprintw(main_win, 2, HIGH_COL, "%12s", "HIGH");
    }
    if (show_low) {
        mvwprintw(main_win, 2, LOW_COL, "%12s", "LOW");
    }
    if (show_volume) {
        mvwprintw(main_win, 2, VOLUME_COL, "%14s", "VOLUME");
    }
    if (show_trades) {
        mvwprintw(main_win, 2, TRADES_COL, "%10s", "TRADES");
    }
    if (show_quote) {
        mvwprintw(main_win, 2, QUOTE_COL, "%14s", "QUOTE VOL");
    }
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_HEADER));
    mvwhline(main_win, 3, 2, ACS_HLINE, COLS - 4);
    
    // Draw each ticker row along with optional flicker effects on price updates.
    //
    // NOTE: Even if a row is currently outside the viewport, we still update
    // last_prices[i]. This keeps the price-change detection correct when the
    // user scrolls and the row becomes visible again.
    for (int i = 0; i < count; i++) {
        double previous_price = (i < MAX_SYMBOLS) ? last_prices[i] : NAN;
        bool had_previous = !isnan(previous_price);
        bool price_went_up = had_previous ? (tickers[i].price > previous_price) : true;
        bool price_changed = had_previous &&
            fabs(tickers[i].price - previous_price) > PRICE_CHANGE_EPSILON;
        bool in_view = i >= price_board_scroll_offset &&
                       i < price_board_scroll_offset + visible_rows;

        if (!in_view) {
            // Off-screen: update history only, then skip drawing.
            if (i < MAX_SYMBOLS) {
                last_prices[i] = tickers[i].price;
            }
            continue;
        }

        // Translate model index -> screen row within viewport.
        int y = board_start_y + (i - price_board_scroll_offset);
        
        // Selected row is rendered inverted for easy navigation.
        if (i == selected) {
            wattron(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
            mvwhline(main_win, y, 0, ' ', COLS);
        }
        
        // Determine selection before coloring symbol/price.
        bool row_selected = (i == selected);

        // Trading pair in yellow; keep contrast when selected.
        if (colors_available) {
            int sym_pair = row_selected ? COLOR_PAIR_SYMBOL_SELECTED : COLOR_PAIR_SYMBOL;
            wattron(main_win, COLOR_PAIR(sym_pair) | A_BOLD);
            mvwprintw(main_win, y, 2, "%-15s", tickers[i].symbol);
            wattroff(main_win, COLOR_PAIR(sym_pair) | A_BOLD);
        } else {
            mvwprintw(main_win, y, 2, "%-15s", tickers[i].symbol);
        }
        
        // Price column with color coded trend and optional flicker on change.
        char price_str[32];
        if (tickers[i].price_text[0]) {
            snprintf(price_str, sizeof(price_str), "%s", tickers[i].price_text);
        } else {
            format_number(price_str, sizeof(price_str), tickers[i].price);
        }
        trim_trailing_zeros(price_str);
        bool daily_up = tickers[i].change_24h >= 0.0;
        chtype price_arrow = (price_changed
            ? (price_went_up ? ACS_UARROW : ACS_DARROW)
            : ' ');
        draw_price_cell(y, price_str, price_arrow, daily_up, row_selected,
                        price_changed, price_went_up);
        if (colors_available && price_changed && flicker_count < MAX_SYMBOLS) {
            flicker_queue[flicker_count].y = y;
            snprintf(flicker_queue[flicker_count].price_text,
                     sizeof(flicker_queue[flicker_count].price_text), "%s",
                     price_str);
            flicker_queue[flicker_count].arrow = price_arrow;
            flicker_queue[flicker_count].daily_up = daily_up;
            flicker_queue[flicker_count].row_selected = row_selected;
            flicker_queue[flicker_count].price_went_up = price_went_up;
            flicker_count++;
        }
        if (i < MAX_SYMBOLS) {
            last_prices[i] = tickers[i].price;
        }
        
        // 24h percentage change inherits the same palette logic.
        char change_str[32];
        snprintf(change_str, sizeof(change_str), "%+14.2f%%", tickers[i].change_24h);
        bool change_up = tickers[i].change_24h >= 0;
        draw_change_cell(y, change_str, change_up, row_selected);

        if (row_selected && colors_available) {
            wattron(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        }

        char number_buf[32];
        if (show_high) {
            if (tickers[i].high_text[0]) {
                snprintf(number_buf, sizeof(number_buf), "%s", tickers[i].high_text);
            } else {
                format_number(number_buf, sizeof(number_buf), tickers[i].high_price);
            }
            trim_trailing_zeros(number_buf);
            mvwprintw(main_win, y, HIGH_COL, "%12s", number_buf);
        }
        if (show_low) {
            if (tickers[i].low_text[0]) {
                snprintf(number_buf, sizeof(number_buf), "%s", tickers[i].low_text);
            } else {
                format_number(number_buf, sizeof(number_buf), tickers[i].low_price);
            }
            trim_trailing_zeros(number_buf);
            mvwprintw(main_win, y, LOW_COL, "%12s", number_buf);
        }
        if (show_volume) {
            format_number_with_commas(number_buf, sizeof(number_buf), tickers[i].volume_base);
            mvwprintw(main_win, y, VOLUME_COL, "%14s", number_buf);
        }
        if (show_trades) {
            format_integer_with_commas(number_buf, sizeof(number_buf), tickers[i].trade_count);
            mvwprintw(main_win, y, TRADES_COL, "%10s", number_buf);
        }
        if (show_quote) {
            format_number_with_commas(number_buf, sizeof(number_buf), tickers[i].volume_quote);
            mvwprintw(main_win, y, QUOTE_COL, "%14s", number_buf);
        }
        
        if (i == selected) {
            wattroff(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        }
    }
    
    // Interaction hint anchored to the footer.
    // Scroll indicators: show arrows when there are hidden rows above/below.
    // These are placed at column 0 to avoid colliding with the content columns.
    bool can_scroll_up = price_board_scroll_offset > 0;
    bool can_scroll_down = (price_board_scroll_offset + visible_rows) < count;
    if (can_scroll_up) {
        mvwaddch(main_win, board_start_y, 0, ACS_UARROW);
    }
    if (can_scroll_down) {
        mvwaddch(main_win, board_start_y + visible_rows - 1, 0, ACS_DARROW);
    }

    mvwprintw(main_win, LINES - 2, 2,
              "Keys: Up/Down Navigate/Scroll | Enter: View Chart | q: Quit");
    
    wrefresh(main_win);
    // Run the flicker animation after the frame is painted so the color swap is
    // visible without blocking the drawing loop for every row.
    if (colors_available && flicker_count > 0) {
        napms(PRICE_FLICKER_DURATION_MS);
        for (int i = 0; i < flicker_count; ++i) {
            // After the flicker window, hide the arrow and revert to normal colors.
            draw_price_cell(flicker_queue[i].y, flicker_queue[i].price_text,
                            ' ', flicker_queue[i].daily_up,
                            flicker_queue[i].row_selected, false,
                            flicker_queue[i].price_went_up);
        }
        wrefresh(main_win);
    }
}

// Draw the interactive candlestick chart along with axis labels, cursor, and
// metadata for the currently selected candle.
void draw_chart(const char *restrict symbol, PricePoint *restrict points, int count,
                Period period, int selected_index) {
    werase(main_win);

    if (count == 0) {
        mvwprintw(main_win, LINES / 2, COLS / 2 - 10, "No data available");
        wrefresh(main_win);
        return;
    }

    const char *period_str = period_label(period);
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR) | A_BOLD);
    mvwprintw(main_win, 0, 2, "%s - %s Candlestick Chart", symbol, period_str);
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR) | A_BOLD);

    // Compute min/max for scaling the y-axis.
    double min_price = points[0].low;
    double max_price = points[0].high;
    for (int i = 1; i < count; i++) {
        if (points[i].low < min_price) min_price = points[i].low;
        if (points[i].high > max_price) max_price = points[i].high;
    }
    if (max_price - min_price < 0.000001) {
        min_price -= 1.0;
        max_price += 1.0;
    }

    // Chart occupies everything below the header row.
    int chart_y = 2;
    int chart_height = LINES - 6;
    if (chart_height < 4) chart_height = 4;
    int axis_width = 12;
    int chart_x = axis_width + 2;
    int available_width = COLS - chart_x - 2;
    if (available_width < 1) available_width = 1;
    // Reserve room for the candle detail box while ensuring the chart keeps
    // at least one column of drawing space on narrow terminals.
    int info_gap = 2;
    const int preferred_info_width = 36;
    const int min_info_width = 22;
    int info_width = preferred_info_width;
    int max_width_share = (available_width * 2) / 3;  // keep majority for chart.
    if (info_width > max_width_share) info_width = max_width_share;
    if (info_width < min_info_width) info_width = min_info_width;
    if (info_width > available_width - info_gap - 1) {
        info_width = available_width - info_gap - 1;
    }
    if (info_width < min_info_width) {
        info_width = (available_width > min_info_width)
            ? min_info_width
            : available_width / 2;
    }
    if (info_width < 10) {
        info_width = 0;
        info_gap = 0;
    }
    int chart_width = available_width - info_width - info_gap;
    if (chart_width < 1) chart_width = 1;
    int info_x = chart_x + chart_width + info_gap;
    int info_y = 1;
    int info_height = 14;

    double price_range = max_price - min_price;

    // Draw faint grid lines inside the chart area for better price context.
    if (chart_width > 2 && chart_height > 2) {
        int grid_divisions = 4;
        wattron(main_win, A_DIM);
        for (int i = 1; i < grid_divisions; ++i) {
            int y = chart_y + (chart_height * i / grid_divisions);
            mvwhline(main_win, y, chart_x, ACS_HLINE, chart_width);
        }
        for (int i = 1; i < grid_divisions; ++i) {
            int x = chart_x + (chart_width * i / grid_divisions);
            mvwvline(main_win, chart_y, x, ACS_VLINE, chart_height);
        }
        wattroff(main_win, A_DIM);
    }

    // Draw Y-axis line first (within main window)
    mvwvline(main_win, chart_y, axis_width, ACS_VLINE, chart_height);

    // Y-axis labels with tick marks every 25% of the range.
    for (int i = 0; i <= 4; i++) {
        double price = max_price - (price_range * i / 4.0);
        char price_str[24];
        format_axis_price(price_str, sizeof(price_str), price, price_range);
        int y = chart_y + (chart_height * i / 4);
        mvwprintw(main_win, y, 2, "%10s", price_str);
        mvwaddch(main_win, y, axis_width, ACS_PLUS);
    }

    // Each candle consumes one column and one column of spacing for legibility.
    const int candle_stride = 2;  // 1 column for body + 1 column gap for readability
    int max_columns = chart_width / candle_stride;
    if (max_columns < 1) max_columns = 1;
    int visible_points = count < max_columns ? count : max_columns;
    if (visible_points < 1) visible_points = (count > 0) ? 1 : 0;

    // Guard the selected index. Default to the newest candle.
    int selection_idx = (selected_index >= 0 && selected_index < count)
        ? selected_index
        : (count - 1);
    if (selection_idx < 0) selection_idx = 0;

    int start_idx = 0;
    if (visible_points < count) {
        start_idx = count - visible_points;
        if (selection_idx < start_idx) {
            start_idx = selection_idx;
        } else if (selection_idx >= start_idx + visible_points) {
            start_idx = selection_idx - visible_points + 1;
        }
        int max_start = count - visible_points;
        if (start_idx > max_start) start_idx = max_start;
        if (start_idx < 0) start_idx = 0;
    }

    int selected_column = -1;
    if (selection_idx >= start_idx && selection_idx < start_idx + visible_points) {
        selected_column = selection_idx - start_idx;
    }

    // Draw every visible candle using ASCII wick/body glyphs.
    for (int col = 0; col < visible_points; col++) {
        PricePoint *pt = &points[start_idx + col];
        int screen_x = chart_x + col * candle_stride;

        int high_y = price_to_row(pt->high, min_price, max_price, chart_height, chart_y);
        int low_y = price_to_row(pt->low, min_price, max_price, chart_height, chart_y);
        int open_y = price_to_row(pt->open, min_price, max_price, chart_height, chart_y);
        int close_y = price_to_row(pt->close, min_price, max_price, chart_height, chart_y);

        int wick_top = high_y < low_y ? high_y : low_y;
        int wick_bottom = high_y > low_y ? high_y : low_y;
        int body_top = open_y < close_y ? open_y : close_y;
        int body_bottom = open_y > close_y ? open_y : close_y;

        int color_pair = (pt->close >= pt->open) ? COLOR_PAIR(COLOR_PAIR_GREEN) : COLOR_PAIR(COLOR_PAIR_RED);
        wattron(main_win, color_pair);

        for (int y = wick_top; y <= wick_bottom; y++) {
            mvwaddch(main_win, y, screen_x, '|');
        }

        if (body_top == body_bottom) {
            mvwaddch(main_win, body_top, screen_x, ACS_CKBOARD);
        } else {
            for (int y = body_top; y <= body_bottom; y++) {
                mvwaddch(main_win, y, screen_x, ACS_CKBOARD);
            }
        }

        wattroff(main_win, color_pair);
    }

    PricePoint *selected_point = (count > 0) ? &points[selection_idx] : NULL;
    int chart_draw_width = visible_points * candle_stride;
    if (chart_draw_width < 1) chart_draw_width = chart_width;

    // Overlay a vertical cursor line that frames the selected candle but does
    // not obscure its wick/body glyphs.
    if (selected_point && selected_column >= 0) {
        int cross_x = chart_x + selected_column * candle_stride;
        int skip_top = price_to_row(selected_point->high, min_price, max_price,
                                    chart_height, chart_y);
        int skip_bottom = price_to_row(selected_point->low, min_price, max_price,
                                       chart_height, chart_y);
        if (skip_top > skip_bottom) {
            int tmp = skip_top;
            skip_top = skip_bottom;
            skip_bottom = tmp;
        }

        wattron(main_win, A_DIM);
        for (int y = chart_y; y < chart_y + chart_height; ++y) {
            if (y >= skip_top && y <= skip_bottom) {
                continue;
            }
            mvwaddch(main_win, y, cross_x, ACS_VLINE);
        }
        wattroff(main_win, A_DIM);
    }

    // X-axis at the bottom of the chart
    int axis_y = chart_y + chart_height;
    if (axis_y < LINES - 3) {
        mvwhline(main_win, axis_y, axis_width, ACS_HLINE, chart_width);
        mvwaddch(main_win, axis_y, axis_width, ACS_PLUS);
        mvwaddch(main_win, axis_y, axis_width + chart_width - 1, ACS_RTEE);

        // Time labels with arrows pointing to the exact candle column
        struct tm tm_buf;
        char time_str[32];
        int axis_left = axis_width;
        int axis_right = axis_width + chart_width - 1;
        int label_row = axis_y + 1;

        PricePoint *start_pt = &points[start_idx];
        PricePoint *end_pt = &points[start_idx + visible_points - 1];
        int mid_idx = start_idx + visible_points / 2;
        if (mid_idx >= count) mid_idx = count - 1;
        if (mid_idx < start_idx) mid_idx = start_idx;
        PricePoint *mid_pt = &points[mid_idx];

        struct AxisMarker {
            PricePoint *point;
            int column;
        } markers[3] = {
            { start_pt, 0 },
            { mid_pt, mid_idx - start_idx },
            { end_pt, visible_points - 1 }
        };

        for (int m = 0; m < 3; ++m) {
            PricePoint *pt = markers[m].point;
            if (!pt) continue;
            int col = markers[m].column;
            if (col < 0) col = 0;
            if (col > visible_points - 1) col = visible_points - 1;

            int arrow_x = chart_x + col * candle_stride;
            if (arrow_x < axis_left) arrow_x = axis_left;
            if (arrow_x > axis_right) arrow_x = axis_right;

            time_t ts = (time_t)pt->timestamp;
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M",
                     localtime_r(&ts, &tm_buf));
            int label_len = (int)strlen(time_str);
            int print_len = label_len;
            if (print_len > chart_width) print_len = chart_width;
            int label_x = arrow_x - print_len / 2;
            if (label_x < axis_left) label_x = axis_left;
            if (label_x + print_len > axis_left + chart_width) {
                label_x = axis_left + chart_width - print_len;
            }

            mvwaddch(main_win, axis_y, arrow_x, ACS_UARROW);
            mvwaddnstr(main_win, label_row, label_x, time_str, print_len);
        }
    }

    // Draw the floating info box that summarizes the selected candle.
    if (info_width >= 10 && selected_point) {
        int max_info_height = LINES - 4 - info_y;
        if (max_info_height < info_height) info_height = max_info_height;
        const int min_info_height = 14;
        if (info_height < min_info_height) info_height = min_info_height;
        if (info_height >= min_info_height) {
            if (info_x + info_width >= COLS) {
                info_x = COLS - info_width - 1;
            }
            draw_info_box(info_x, info_y, info_width, info_height, selected_point);
        }
    }

    mvwprintw(main_win, LINES - 3, 2, "Interval: %s (Space to cycle)", period_str);
    mvwprintw(main_win, LINES - 2, 2,
              "Keys: Left/Right Move Cursor | Space Next Interval | ESC/q: Back");

    wrefresh(main_win);
}

// Proxy to wgetch so the UI layer can remain decoupled from ncurses details.
int handle_input(void) {
    return wgetch(main_win);
}
