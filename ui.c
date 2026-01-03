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
    if (num >= 1000000000) {
        snprintf(buf, size, "%.2fB", num / 1000000000);
    } else if (num >= 1000000) {
        snprintf(buf, size, "%.2fM", num / 1000000);
    } else if (num >= 1000) {
        snprintf(buf, size, "%.2fK", num / 1000);
    } else if (num >= 1) {
        snprintf(buf, size, "%.2f", num);
    } else {
        snprintf(buf, size, "%.8f", num);
    }
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
    mvwprintw(main_win, LINES - 2, 2, "Keys: ↑/↓ Navigate | Enter: View Chart | q: Quit");
    
    wrefresh(main_win);
}

// Draw ASCII chart
void draw_chart(const char *symbol, PricePoint *points, int count, Period period) {
    werase(main_win);
    
    if (count == 0) {
        mvwprintw(main_win, LINES / 2, COLS / 2 - 10, "No data available");
        wrefresh(main_win);
        return;
    }
    
    // Draw title
    const char *period_str = (period == PERIOD_1DAY) ? "1 Day" :
                             (period == PERIOD_1WEEK) ? "1 Week" : "1 Month";
    wattron(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(main_win, 0, 2, "%s - %s Chart", symbol, period_str);
    wattroff(main_win, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    
    // Find min and max prices
    double min_price = points[0].price;
    double max_price = points[0].price;
    for (int i = 1; i < count; i++) {
        if (points[i].price < min_price) min_price = points[i].price;
        if (points[i].price > max_price) max_price = points[i].price;
    }
    
    // Chart dimensions
    int chart_height = LINES - 6;
    int chart_width = COLS - 20;
    int chart_y = 2;
    int chart_x = 15;
    
    double price_range = max_price - min_price;
    if (price_range < 0.000001) price_range = 1.0;  // Avoid division by zero
    
    // Draw Y-axis labels
    for (int i = 0; i <= 4; i++) {
        double price = max_price - (price_range * i / 4.0);
        char price_str[16];
        format_number(price_str, sizeof(price_str), price);
        mvwprintw(main_win, chart_y + (chart_height * i / 4), 2, "%12s", price_str);
    }
    
    // Draw chart
    int prev_y = -1;
    for (int i = 0; i < count && i < chart_width; i++) {
        int x = chart_x + (i * chart_width / count);
        double normalized = (points[i].price - min_price) / price_range;
        int y = chart_y + chart_height - (int)(normalized * chart_height);
        
        if (y >= chart_y && y < chart_y + chart_height) {
            // Determine color based on price change
            int color = COLOR_PAIR_GREEN;
            if (i > 0 && points[i].price < points[i-1].price) {
                color = COLOR_PAIR_RED;
            }
            
            wattron(main_win, COLOR_PAIR(color));
            
            // Draw line from previous point
            if (prev_y >= 0) {
                int start_y = prev_y < y ? prev_y : y;
                int end_y = prev_y < y ? y : prev_y;
                for (int ly = start_y; ly <= end_y; ly++) {
                    mvwaddch(main_win, ly, x, ACS_VLINE);
                }
            } else {
                mvwaddch(main_win, y, x, ACS_DIAMOND);
            }
            
            wattroff(main_win, COLOR_PAIR(color));
            prev_y = y;
        }
    }
    
    // Draw current price
    char current_price[32];
    format_number(current_price, sizeof(current_price), points[count - 1].price);
    mvwprintw(main_win, LINES - 4, 2, "Current Price: %s", current_price);
    
    // Draw price change
    double change = ((points[count - 1].price - points[0].price) / points[0].price) * 100;
    int color = change >= 0 ? COLOR_PAIR_GREEN : COLOR_PAIR_RED;
    wattron(main_win, COLOR_PAIR(color));
    mvwprintw(main_win, LINES - 4, 30, "Change: %+.2f%%", change);
    wattroff(main_win, COLOR_PAIR(color));
    
    // Draw help text
    mvwprintw(main_win, LINES - 2, 2, "Keys: 1: 1 Day | 7: 1 Week | 30: 1 Month | ESC/q: Back");
    
    wrefresh(main_win);
}

// Handle keyboard input
int handle_input(void) {
    return wgetch(main_win);
}
