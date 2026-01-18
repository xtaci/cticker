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
 * @file ui_priceboard.c
 * @brief Price board rendering and hit testing.
 */

#include <math.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"

// Timing and formatting constants.
#define PRICE_FLICKER_DURATION_MS 500
#define PRICE_CHANGE_EPSILON 1e-9
// Column anchors for the price board layout.
#define PRICE_COL 18
#define CHANGE_COL 35
#define HIGH_COL 52
#define LOW_COL 70
#define VOLUME_COL 88
#define TRADES_COL 108
#define QUOTE_COL 126

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

// Render the 24h change cell with color mapping and selection awareness.
static void draw_change_cell(int y, const char *change_str, bool change_up,
                             bool row_selected) {
    int pair = COLOR_PAIR_GREEN;
    if (colors_available) {
        if (row_selected) {
            pair = change_up ? COLOR_PAIR_GREEN_SELECTED : COLOR_PAIR_RED_SELECTED;
        } else {
            pair = change_up ? COLOR_PAIR_GREEN : COLOR_PAIR_RED;
        }
        wattron(main_win, COLOR_PAIR(pair) | A_BOLD);
    }

    mvwprintw(main_win, y, CHANGE_COL, "%15s", change_str);

    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(pair) | A_BOLD);
    }
}

// Draw the ticker board listing all configured symbols along with their latest
// price, change, and a transient flicker for updated rows.
void draw_main_screen(TickerData *tickers, int count, int selected,
                      const char *sort_hint_price, const char *sort_hint_change) {
    // Full-frame redraw to keep layout consistent after terminal resize.
    werase(main_win);
    // Layout (screen coordinates):
    //   row 0: title + timestamp
    //   row 2: column headers
    //   row 3: separator line
    //   row 4..N: scrollable ticker list (this is the viewport)
    //   last rows: footer/help text
    const int board_start_y = 4;
    const int footer_reserved_rows = 1;  // Reserved row for the footer bar
    int max_board_height = LINES - footer_reserved_rows - board_start_y;
    if (max_board_height < 1) {
        max_board_height = 1;
    }
    int visible_rows = max_board_height;
    price_board_view_start_y = board_start_y;
    price_board_view_rows = visible_rows;

    // Column visibility is responsive: hide columns on narrow terminals.
    bool show_high = (COLS > HIGH_COL + 10);
    bool show_low = (COLS > LOW_COL + 10);
    bool show_volume = (COLS > VOLUME_COL + 12);
    bool show_trades = (COLS > TRADES_COL + 6);
    bool show_quote = (COLS > QUOTE_COL + 12);

    // Compute and clamp the viewport window (price_board_scroll_offset .. + visible_rows).
    if (count <= 0) {
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
        if (price_board_scroll_offset > max_scroll) {
            price_board_scroll_offset = max_scroll;
        }
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

    // Unified title bar header keeps left label, centered board name, and right clock visually cohesive.
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    const char *left_text = "CTICKER";
    const char *title_text = "[P][R][I][C][E] [B][O][A][R][D]";
    int left_x = 2;
    int left_len = (int)strlen(left_text);
    int title_len = (int)strlen(title_text);
    int time_x = COLS - (int)strlen(time_str) - 2;
    if (time_x < 2) {
        time_x = 2;
    }
    int title_x = (COLS - title_len) / 2;
    if (title_x < 2) {
        title_x = 2;
    }
    int min_title_x = left_x + left_len + 2;
    if (title_x < min_title_x) {
        title_x = min_title_x;
    }
    if (title_x + title_len >= time_x) {
        title_x = time_x - title_len - 1;
        if (title_x < 2) {
            title_x = 2;
        }
    }

    wattron(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR));
    mvwhline(main_win, 0, 0, ' ', COLS);
    mvwprintw(main_win, 0, left_x, "%s", left_text);
    mvwprintw(main_win, 0, title_x, "%s", title_text);
    mvwprintw(main_win, 0, time_x, "%s", time_str);
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR));

    // Column headers and a horizontal rule to separate the board.
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER));
    mvwprintw(main_win, 2, 2, "%-15s", "SYMBOL");
    mvwprintw(main_win, 2, PRICE_COL, "%15s", "PRICE");
    mvwprintw(main_win, 2, CHANGE_COL, "%15s", "CHANGE 24H");
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
    for (int i = 0; i < count; i++) {
        double previous_price = (i < MAX_SYMBOLS) ? last_prices[i] : NAN;
        bool had_previous = !isnan(previous_price);
        bool price_went_up = had_previous ? (tickers[i].price > previous_price) : true;
        bool price_changed = had_previous &&
            fabs(tickers[i].price - previous_price) > PRICE_CHANGE_EPSILON;
        bool in_view = i >= price_board_scroll_offset &&
                       i < price_board_scroll_offset + visible_rows;

        if (!in_view) {
            if (i < MAX_SYMBOLS) {
                last_prices[i] = tickers[i].price;
            }
            continue;
        }

        int y = board_start_y + (i - price_board_scroll_offset);

        if (i == selected) {
            wattron(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
            mvwhline(main_win, y, 0, ' ', COLS);
        }

        bool row_selected = (i == selected);

        if (colors_available) {
            int sym_pair = row_selected ? COLOR_PAIR_SYMBOL_SELECTED : COLOR_PAIR_SYMBOL;
            wattron(main_win, COLOR_PAIR(sym_pair) | A_BOLD);
            mvwprintw(main_win, y, 2, "%-15s", tickers[i].symbol);
            wattroff(main_win, COLOR_PAIR(sym_pair) | A_BOLD);
        } else {
            mvwprintw(main_win, y, 2, "%-15s", tickers[i].symbol);
        }

        char price_str[32];
        if (tickers[i].price_text[0]) {
            snprintf(price_str, sizeof(price_str), "%s", tickers[i].price_text);
        } else {
            ui_format_number(price_str, sizeof(price_str), tickers[i].price);
        }
        ui_trim_trailing_zeros(price_str);
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
                ui_format_number(number_buf, sizeof(number_buf), tickers[i].high_price);
            }
            ui_trim_trailing_zeros(number_buf);
            mvwprintw(main_win, y, HIGH_COL, "%12s", number_buf);
        }
        if (show_low) {
            if (tickers[i].low_text[0]) {
                snprintf(number_buf, sizeof(number_buf), "%s", tickers[i].low_text);
            } else {
                ui_format_number(number_buf, sizeof(number_buf), tickers[i].low_price);
            }
            ui_trim_trailing_zeros(number_buf);
            mvwprintw(main_win, y, LOW_COL, "%12s", number_buf);
        }
        if (show_volume) {
            ui_format_number_with_commas(number_buf, sizeof(number_buf), tickers[i].volume_base);
            mvwprintw(main_win, y, VOLUME_COL, "%14s", number_buf);
        }
        if (show_trades) {
            ui_format_integer_with_commas(number_buf, sizeof(number_buf), tickers[i].trade_count);
            mvwprintw(main_win, y, TRADES_COL, "%10s", number_buf);
        }
        if (show_quote) {
            ui_format_number_with_commas(number_buf, sizeof(number_buf), tickers[i].volume_quote);
            mvwprintw(main_win, y, QUOTE_COL, "%14s", number_buf);
        }

        if (row_selected && colors_available) {
            wattroff(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        }
    }

    bool can_scroll_up = price_board_scroll_offset > 0;
    bool can_scroll_down = (price_board_scroll_offset + visible_rows) < count;
    if (can_scroll_up) {
        mvwaddch(main_win, board_start_y, 0, ACS_UARROW);
    }
    if (can_scroll_down) {
        mvwaddch(main_win, board_start_y + visible_rows - 1, 0, ACS_DARROW);
    }

    const char *price_hint = (sort_hint_price && sort_hint_price[0]) ? sort_hint_price : "=";
    const char *change_hint = (sort_hint_change && sort_hint_change[0]) ? sort_hint_change : "=";
    char footer_text[256];
    snprintf(footer_text, sizeof(footer_text),
             "KEYS: / NAVIGATE | ENTER/CLICK: VIEW CHART | F5: SORT BY PRICE %s | F6: SORT BY CHANGE %s | Q: QUIT",
             price_hint, change_hint);
    draw_footer_bar(footer_text);

    wrefresh(main_win);
    if (colors_available && flicker_count > 0) {
        napms(PRICE_FLICKER_DURATION_MS);
        for (int i = 0; i < flicker_count; ++i) {
            draw_price_cell(flicker_queue[i].y, flicker_queue[i].price_text,
                            ' ', flicker_queue[i].daily_up,
                            flicker_queue[i].row_selected, false,
                            flicker_queue[i].price_went_up);
        }
        wrefresh(main_win);
    }
}

// Convert mouse Y position to a ticker row index within the visible viewport.
int ui_price_board_hit_test_row(int mouse_y, int total_rows) {
    if (total_rows <= 0) {
        return -1;
    }
    if (mouse_y < price_board_view_start_y ||
        mouse_y >= price_board_view_start_y + price_board_view_rows) {
        return -1;
    }
    int index = price_board_scroll_offset + (mouse_y - price_board_view_start_y);
    if (index < 0 || index >= total_rows) {
        return -1;
    }
    return index;
}
