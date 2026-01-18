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
 * @file ui_chart.c
 * @brief Chart rendering, info boxes, and chart hit testing.
 */

#include <math.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"

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

// Draw the floating info box in the top-right corner that mirrors the
// currently selected candle values.
static void draw_info_box(int x, int y, int width, int height,
                          const PricePoint *point) {
    if (!point || width < 10 || height < 6) {
        return;
    }

    int right = x + width - 1;
    int bottom = y + height - 1;

    wattron(main_win, A_REVERSE);
    for (int row = y; row <= bottom; ++row) {
        mvwhline(main_win, row, x, ' ', width);
    }

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
        ui_format_number(open_str, sizeof(open_str), point->open);
    }
    ui_trim_trailing_zeros(open_str);
    if (point->high_text[0]) {
        snprintf(high_str, sizeof(high_str), "%s", point->high_text);
    } else {
        ui_format_number(high_str, sizeof(high_str), point->high);
    }
    ui_trim_trailing_zeros(high_str);
    if (point->low_text[0]) {
        snprintf(low_str, sizeof(low_str), "%s", point->low_text);
    } else {
        ui_format_number(low_str, sizeof(low_str), point->low);
    }
    ui_trim_trailing_zeros(low_str);
    if (point->close_text[0]) {
        snprintf(close_str, sizeof(close_str), "%s", point->close_text);
    } else {
        ui_format_number(close_str, sizeof(close_str), point->close);
    }
    ui_trim_trailing_zeros(close_str);
    ui_format_number(volume_str, sizeof(volume_str), point->volume);
    ui_format_number(quote_volume_str, sizeof(quote_volume_str), point->quote_volume);
    ui_format_number(taker_buy_base_str, sizeof(taker_buy_base_str), point->taker_buy_base_volume);
    ui_format_number(taker_buy_quote_str, sizeof(taker_buy_quote_str), point->taker_buy_quote_volume);

    double change = (point->open != 0.0)
        ? ((point->close - point->open) / point->open) * 100.0
        : 0.0;
    char change_str[16];
    snprintf(change_str, sizeof(change_str), "%+.2f%%", change);
    bool change_up = (point->close >= point->open);

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
    if (colors_available) {
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_INFO_OPEN) | A_BOLD);
    }
    mvwprintw(main_win, line++, content_x, "Open : %s", open_str);
    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_INFO_OPEN) | A_BOLD);
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_INFO_HIGH) | A_BOLD);
    }
    mvwprintw(main_win, line++, content_x, "High : %s", high_str);
    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_INFO_HIGH) | A_BOLD);
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_INFO_LOW) | A_BOLD);
    }
    mvwprintw(main_win, line++, content_x, "Low  : %s", low_str);
    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_INFO_LOW) | A_BOLD);
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_INFO_CLOSE) | A_BOLD);
    }
    mvwprintw(main_win, line, content_x, "Close: %s", close_str);
    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_INFO_CLOSE) | A_BOLD);
    }
    line++;
    mvwprintw(main_win, line++, content_x, "Vol  : %s", volume_str);
    mvwprintw(main_win, line++, content_x, "Quote Vol: %s", quote_volume_str);
    mvwprintw(main_win, line++, content_x, "Trades   : %d", point->trade_count);
    mvwprintw(main_win, line++, content_x, "Taker Buy (B): %s", taker_buy_base_str);
    mvwprintw(main_win, line++, content_x, "Taker Buy (Q): %s", taker_buy_quote_str);

    if (colors_available) {
        int color = change_up
            ? COLOR_PAIR(COLOR_PAIR_GREEN)
            : COLOR_PAIR(COLOR_PAIR_RED);
        wattron(main_win, color | A_BOLD);
        mvwprintw(main_win, line, content_x, "Change: %s", change_str);
        wattroff(main_win, color | A_BOLD);
    } else {
        mvwprintw(main_win, line, content_x, "Change: %s", change_str);
    }

    wattroff(main_win, A_REVERSE);
}

static void draw_current_price_box(int x, int y, int width, int height,
                                   const PricePoint *point) {
    if (!point || width < 10 || height < 4) {
        return;
    }

    int right = x + width - 1;
    int bottom = y + height - 1;

    wattron(main_win, A_REVERSE);
    for (int row = y; row <= bottom; ++row) {
        mvwhline(main_win, row, x, ' ', width);
    }

    mvwaddch(main_win, y, x, ACS_ULCORNER);
    mvwaddch(main_win, y, right, ACS_URCORNER);
    mvwaddch(main_win, bottom, x, ACS_LLCORNER);
    mvwaddch(main_win, bottom, right, ACS_LRCORNER);
    mvwhline(main_win, y, x + 1, ACS_HLINE, width - 2);
    mvwhline(main_win, bottom, x + 1, ACS_HLINE, width - 2);
    mvwvline(main_win, y + 1, x, ACS_VLINE, height - 2);
    mvwvline(main_win, y + 1, right, ACS_VLINE, height - 2);

    char price_str[32];
    if (point->close_text[0]) {
        snprintf(price_str, sizeof(price_str), "%s", point->close_text);
    } else {
        ui_format_number(price_str, sizeof(price_str), point->close);
    }
    ui_trim_trailing_zeros(price_str);

    int content_x = x + 2;
    int label_y = y + 1;
    int value_y = y + 2;

    mvwprintw(main_win, label_y, content_x, "Current Price:");
    if (colors_available) {
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_INFO_CURRENT) | A_BOLD);
    }
    mvwprintw(main_win, value_y, content_x, "%s", price_str);
    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_INFO_CURRENT) | A_BOLD);
    }

    wattroff(main_win, A_REVERSE);
}

// Draw the interactive candlestick chart along with axis labels, cursor, and
// metadata for the currently selected candle.
void draw_chart(const char *restrict symbol, int count, PricePoint points[count],
                Period period, int selected_index) {
    werase(main_win);

    if (count == 0) {
        mvwprintw(main_win, LINES / 2, COLS / 2 - 10, "No data available");
        wrefresh(main_win);
        return;
    }

    const char *period_str = ui_period_label(period);
    char header_text[128];
    snprintf(header_text, sizeof(header_text), "%s - %s CANDLESTICK CHART", symbol, period_str);
    int header_len = (int)strlen(header_text);
    int header_x = (COLS - header_len) / 2;
    if (header_x < 0) {
        header_x = 0;
    }

    wattron(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR));
    mvwhline(main_win, 0, 0, ' ', COLS);
    mvwprintw(main_win, 0, header_x, "%s", header_text);
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_TITLE_BAR));

    // Compute min/max for scaling the y-axis.
    double min_price = points[0].low;
    double max_price = points[0].high;
    for (int i = 1; i < count; ++i) {
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
    const int preferred_info_width = 37;
    const int min_info_width = 23;
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
    int info_right_target = COLS - info_width;
    if (info_x < info_right_target) {
        info_x = info_right_target;
    }
    int info_y = 2;
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
        ui_format_axis_price(price_str, sizeof(price_str), price, price_range);
        int y = price_to_row(price, min_price, max_price, chart_height, chart_y);
        mvwprintw(main_win, y, 1, "%10s", price_str);
    }

    // Compute how many candles can fit (respecting spacing between candles).
    // Use stride=2 so each candle is 1 column wide with a 1-column gap.
    int candle_stride = 2;
    int visible_points = chart_width / candle_stride;
    if (visible_points < 1) {
        visible_points = 1;
    }

    bool viewport_changed = (chart_view_visible_points != visible_points) ||
                            (chart_view_total_points != count);
    chart_view_start_x = chart_x;
    chart_view_visible_points = visible_points;
    chart_view_stride = candle_stride;
    chart_view_total_points = count;

    int start_idx = chart_view_start_idx;
    if (viewport_changed || start_idx < 0 || start_idx >= count) {
        start_idx = (count > visible_points) ? count - visible_points : 0;
    }

    if (selected_index < start_idx) {
        start_idx = selected_index;
    } else if (selected_index >= start_idx + visible_points) {
        start_idx = selected_index - visible_points + 1;
    }

    if (start_idx < 0) {
        start_idx = 0;
    }
    if (start_idx > count - 1) {
        start_idx = count - 1;
    }

    if (start_idx + visible_points > count) {
        start_idx = count - visible_points;
    }
    if (start_idx < 0) {
        start_idx = 0;
    }

    chart_view_start_idx = start_idx;

    // Draw candlesticks within the visible window.
    for (int i = 0; i < visible_points; ++i) {
        int idx = start_idx + i;
        if (idx < 0 || idx >= count) {
            continue;
        }

        int x = chart_x + i * candle_stride;
        PricePoint *point = &points[idx];
        bool up = point->close >= point->open;

        int open_y = price_to_row(point->open, min_price, max_price, chart_height, chart_y);
        int close_y = price_to_row(point->close, min_price, max_price, chart_height, chart_y);
        int high_y = price_to_row(point->high, min_price, max_price, chart_height, chart_y);
        int low_y = price_to_row(point->low, min_price, max_price, chart_height, chart_y);

        int top_y = up ? close_y : open_y;
        int bottom_y = up ? open_y : close_y;
        int color = up ? COLOR_PAIR_GREEN : COLOR_PAIR_RED;

        if (colors_available) {
            wattron(main_win, COLOR_PAIR(color));
        }

        mvwvline(main_win, high_y, x, ACS_VLINE, low_y - high_y + 1);
        mvwvline(main_win, top_y, x, ACS_CKBOARD, bottom_y - top_y + 1);

        if (colors_available) {
            wattroff(main_win, COLOR_PAIR(color));
        }
    }

    // Draw X-axis line and time labels below the chart area.
    int axis_y = chart_y + chart_height;
    if (axis_y < LINES - 2) {
        int axis_len = chart_x + chart_width - axis_width;
        if (axis_len < 1) {
            axis_len = 1;
        }
        mvwhline(main_win, axis_y, axis_width, ACS_HLINE, axis_len);
        mvwaddch(main_win, axis_y, axis_width, ACS_LLCORNER);
        int arrow_row = axis_y - 1;
        int label_row = axis_y + 1;
        if (label_row < LINES - 1) {
            int ticks = chart_width / 12;
            if (ticks < 3) {
                ticks = 3;
            }
            if (ticks > 7) {
                ticks = 7;
            }
            int step = (visible_points > 1) ? (visible_points - 1) / (ticks - 1) : 1;
            if (step < 1) {
                step = 1;
            }
            int label_width = chart_width / (ticks - 1);
            if (label_width < 6) {
                label_width = 6;
            }
            for (int t = 0; t < ticks; ++t) {
                int col_idx = (t == ticks - 1) ? (visible_points - 1) : t * step;
                int idx = start_idx + col_idx;
                if (idx < 0 || idx >= count) {
                    continue;
                }
                time_t ts = (time_t)points[idx].timestamp;
                struct tm tm_buf;
                char time_str[32];
                const char *fmt = "%m-%d";
                if (period <= PERIOD_1HOUR) {
                    fmt = (label_width >= 8) ? "%H:%M" : "%H";
                } else if (period <= PERIOD_4HOUR) {
                    fmt = (label_width >= 10) ? "%m-%d %H:%M" : "%m-%d";
                } else if (period <= PERIOD_1DAY) {
                    fmt = "%m-%d";
                } else {
                    fmt = "%y-%m";
                }
                strftime(time_str, sizeof(time_str), fmt, localtime_r(&ts, &tm_buf));
                int x = chart_x + col_idx * candle_stride;
                int label_x = x - (int)strlen(time_str) / 2;
                if (label_x < chart_x) {
                    label_x = chart_x;
                }
                int max_x = chart_x + chart_width - 1;
                int print_len = (int)strlen(time_str);
                if (label_x + print_len > max_x) {
                    print_len = max_x - label_x + 1;
                }
                if (print_len > 0) {
                    mvwaddnstr(main_win, label_row, label_x, time_str, print_len);
                }
                if (arrow_row >= chart_y && arrow_row < axis_y) {
                    mvwaddch(main_win, arrow_row, x, ACS_UARROW);
                }
            }
        }
    }

    // Highlight selected candle and display info panels.
    const PricePoint *selected_point = NULL;
    const PricePoint *latest_point = NULL;
    if (selected_index >= 0 && selected_index < count) {
        selected_point = &points[selected_index];
    }
    if (count > 0) {
        latest_point = &points[count - 1];
    }

    if (selected_point) {
        int highlight_idx = selected_index - start_idx;
        if (highlight_idx >= 0 && highlight_idx < visible_points) {
            int highlight_x = chart_x + highlight_idx * candle_stride;
            int line_bottom = axis_y - 2;
            if (line_bottom > chart_y) {
                int open_y = price_to_row(selected_point->open, min_price, max_price,
                                          chart_height, chart_y);
                int close_y = price_to_row(selected_point->close, min_price, max_price,
                                           chart_height, chart_y);
                int high_y = price_to_row(selected_point->high, min_price, max_price,
                                          chart_height, chart_y);
                int low_y = price_to_row(selected_point->low, min_price, max_price,
                                         chart_height, chart_y);
                int candle_top = (open_y < close_y) ? open_y : close_y;
                int candle_bottom = (open_y > close_y) ? open_y : close_y;
                if (high_y > low_y) {
                    int tmp = high_y;
                    high_y = low_y;
                    low_y = tmp;
                }

                wattron(main_win, A_DIM);
                for (int y = chart_y; y <= line_bottom; ++y) {
                    if (((y - chart_y) % 2) != 0) {
                        continue;  // Draw dashed line (every other row).
                    }
                    if (y >= high_y && y <= low_y) {
                        continue;  // Skip wick/body so we don't cover candles.
                    }
                    if (y >= candle_top && y <= candle_bottom) {
                        continue;  // Skip body explicitly (redundant but clear).
                    }
                    mvwaddch(main_win, y, highlight_x, ACS_VLINE);
                }
                wattroff(main_win, A_DIM);
            }
        }
    }

    if (info_width >= 10 && selected_point) {
        int max_info_height = LINES - 4;
        if (max_info_height < info_height) info_height = max_info_height;
        const int min_info_height = 14;
        if (info_height < min_info_height) info_height = min_info_height;
        if (info_height >= min_info_height) {
            if (info_x + info_width > COLS) {
                info_x = COLS - info_width;
            }
            draw_info_box(info_x, info_y, info_width, info_height, selected_point);
        }
    }

    if (info_width >= 10 && latest_point) {
        int price_box_y = info_y + info_height + 1;
        int price_box_height = 5;
        int max_price_height = LINES - 2 - price_box_y;
        if (max_price_height < price_box_height) {
            price_box_height = max_price_height;
        }
        if (price_box_height >= 4) {
            if (info_x + info_width > COLS) {
                info_x = COLS - info_width;
            }
            draw_current_price_box(info_x, price_box_y, info_width, price_box_height,
                                   latest_point);
        }
    }

    draw_footer_bar("KEYS: / CURSOR | /: CHANGE INTERVAL | F: FOLLOW LATEST | R: REFRESH | LEFT CLICK: PICK CANDLE | RIGHT CLICK/ESC/Q: BACK");

    wrefresh(main_win);
}

// Convert mouse X position to a candle index in the current chart viewport.
int ui_chart_hit_test_index(int mouse_x, int total_points) {
    if (total_points <= 0) {
        return -1;
    }
    if (chart_view_visible_points <= 0 || chart_view_stride <= 0) {
        return -1;
    }
    int chart_width_pixels = chart_view_visible_points * chart_view_stride;
    if (mouse_x < chart_view_start_x || mouse_x >= chart_view_start_x + chart_width_pixels) {
        return -1;
    }
    int col = (mouse_x - chart_view_start_x) / chart_view_stride;
    int idx = chart_view_start_idx + col;
    if (idx < 0 || idx >= total_points) {
        return -1;
    }
    return idx;
}
