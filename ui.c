#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "cticker.h"

#define COLOR_PAIR_GREEN 1
#define COLOR_PAIR_RED 2
#define COLOR_PAIR_HEADER 3
#define COLOR_PAIR_SELECTED 4

static WINDOW *main_win = NULL;

// Initialize UI
void init_ui(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(1000);  // Non-blocking getch with 1 second timeout
    
    // Initialize colors
    if (has_colors()) {
        start_color();
        init_pair(COLOR_PAIR_GREEN, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_PAIR_RED, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PAIR_HEADER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK, COLOR_WHITE);
    }
    
    main_win = newwin(LINES, COLS, 0, 0);
    keypad(main_win, TRUE);
    wtimeout(main_win, 1000);
}

// Cleanup UI
void cleanup_ui(void) {
    if (main_win) {
        delwin(main_win);
    }
    endwin();
}

// Format large numbers with K, M, B suffixes
static void format_number(char *buf, size_t size, double num) {
    if (fabs(num) >= 1.0) {
        snprintf(buf, size, "%.2f", num);
    } else {
        snprintf(buf, size, "%.8f", num);
    }
}

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

// Draw main screen with ticker board
void draw_main_screen(TickerData *tickers, int count, int selected) {
    werase(main_win);
    
    // Draw title
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(main_win, 0, 2, "CTicker - Cryptocurrency Price Board");
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    
    // Draw time
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    mvwprintw(main_win, 0, COLS - strlen(time_str) - 2, "%s", time_str);
    
    // Draw header
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER));
    mvwprintw(main_win, 2, 2, "%-15s %15s %15s", "Symbol", "Price", "Change 24h");
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_HEADER));
    mvwhline(main_win, 3, 2, ACS_HLINE, COLS - 4);
    
    // Draw ticker data
    for (int i = 0; i < count; i++) {
        int y = 4 + i;
        
        // Highlight selected row
        if (i == selected) {
            wattron(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
            mvwhline(main_win, y, 0, ' ', COLS);
        }
        
        // Symbol
        mvwprintw(main_win, y, 2, "%-15s", tickers[i].symbol);
        
        // Price
        char price_str[32];
        format_number(price_str, sizeof(price_str), tickers[i].price);
        mvwprintw(main_win, y, 18, "%15s", price_str);
        
        // Change 24h
        int color = tickers[i].change_24h >= 0 ? COLOR_PAIR_GREEN : COLOR_PAIR_RED;
        if (i != selected) {
            wattron(main_win, COLOR_PAIR(color));
        }
        mvwprintw(main_win, y, 35, "%+14.2f%%", tickers[i].change_24h);
        if (i != selected) {
            wattroff(main_win, COLOR_PAIR(color));
        }
        
        if (i == selected) {
            wattroff(main_win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        }
    }
    
    // Draw help text
    mvwprintw(main_win, LINES - 2, 2, "Keys: Up/Down Navigate | Enter: View Chart | q: Quit");
    
    wrefresh(main_win);
}

// Draw candlestick chart that fills the screen width
void draw_chart(const char *symbol, PricePoint *points, int count, Period period) {
    werase(main_win);

    if (count == 0) {
        mvwprintw(main_win, LINES / 2, COLS / 2 - 10, "No data available");
        wrefresh(main_win);
        return;
    }

    const char *period_str = period_label(period);
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(main_win, 0, 2, "%s - %s Candlestick Chart", symbol, period_str);
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

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

    int chart_y = 2;
    int chart_height = LINES - 6;
    if (chart_height < 4) chart_height = 4;
    int axis_width = 12;
    int chart_x = axis_width + 2;
    int chart_width = COLS - chart_x - 2;
    if (chart_width < 1) chart_width = 1;

    double price_range = max_price - min_price;

    // Y-axis labels
    for (int i = 0; i <= 4; i++) {
        double price = max_price - (price_range * i / 4.0);
        char price_str[16];
        format_number(price_str, sizeof(price_str), price);
        mvwprintw(main_win, chart_y + (chart_height * i / 4), 2, "%10s", price_str);
    }

    double step = (count > 1 && chart_width > 1) ? (double)(count - 1) / (double)(chart_width - 1) : 0.0;

    for (int x = 0; x < chart_width; x++) {
        size_t idx = (size_t)(x * step + 0.5);
        if (idx >= (size_t)count) idx = count - 1;
        PricePoint *pt = &points[idx];
        int screen_x = chart_x + x;

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

    PricePoint *last = &points[count - 1];
    char open_str[16], high_str[16], low_str[16], close_str[16], change_str[16];
    format_number(open_str, sizeof(open_str), last->open);
    format_number(high_str, sizeof(high_str), last->high);
    format_number(low_str, sizeof(low_str), last->low);
    format_number(close_str, sizeof(close_str), last->close);

    double change = ((last->close - last->open) / last->open) * 100.0;
    snprintf(change_str, sizeof(change_str), "%+.2f%%", change);

    mvwprintw(main_win, LINES - 5, 2, "O:%s H:%s L:%s C:%s", open_str, high_str, low_str, close_str);
    int color = change >= 0 ? COLOR_PAIR(COLOR_PAIR_GREEN) : COLOR_PAIR(COLOR_PAIR_RED);
    wattron(main_win, color);
    mvwprintw(main_win, LINES - 4, 2, "Change: %s", change_str);
    wattroff(main_win, color);

    mvwprintw(main_win, LINES - 3, 2, "Interval: %s (Left/Right to change)", period_str);
    mvwprintw(main_win, LINES - 2, 2, "Keys: Left/Right Interval | ESC/q: Back");

    wrefresh(main_win);
}

// Handle keyboard input
int handle_input(void) {
    return wgetch(main_win);
}
