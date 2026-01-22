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
 * @file ui_core.c
 * @brief ncurses setup/teardown, shared UI state, and footer/status handling.
 */

#include <locale.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

// ncurses window for all rendering in this module.
WINDOW *main_win = NULL;
// Whether terminal supports colors (set during init_ui()).
bool colors_available = false;

// Price board history and viewport state.
double last_prices[MAX_SYMBOLS];
int last_visible_count = 0;
int price_board_view_start_y = 4;
int price_board_view_rows = 0;
int price_board_scroll_offset = 0;

// Chart viewport state used for hit-testing.
int chart_view_start_x = 0;
int chart_view_visible_points = 0;
int chart_view_start_idx = 0;
int chart_view_stride = 1;
int chart_view_total_points = 0;

// Atomic status shown in the footer bar.
static _Atomic StatusPanelState status_panel_state = STATUS_PANEL_NORMAL;

// Reset chart viewport to a neutral state (used on init and chart exit).
void reset_chart_view_state(void) {
    chart_view_start_x = 0;
    chart_view_visible_points = 0;
    chart_view_start_idx = 0;
    chart_view_stride = 1;
    chart_view_total_points = 0;
}

// Clear flicker history and reset viewport defaults.
void reset_price_history(void) {
    for (int i = 0; i < MAX_SYMBOLS; ++i) {
        last_prices[i] = NAN;
    }
    last_visible_count = 0;
    // Reset scroll to top when the UI is initialized.
    price_board_scroll_offset = 0;
    price_board_view_rows = 0;
    reset_chart_view_state();
}

// Map status enum to a user-facing label for the footer panel.
static const char *status_panel_label(StatusPanelState state) {
    switch (state) {
        case STATUS_PANEL_FETCHING:
            return "FETCHING";
        case STATUS_PANEL_NETWORK_ERROR:
            return "NETWORK ERROR";
        case STATUS_PANEL_NORMAL:
        default:
            return "NORMAL";
    }
}

// Return the ncurses color pair for the footer status panel.
static int status_panel_pair(StatusPanelState state) {
    if (!colors_available) {
        return 0;
    }
    switch (state) {
        case STATUS_PANEL_NETWORK_ERROR:
            return COLOR_PAIR_STATUS_PANEL_ALERT;
        case STATUS_PANEL_FETCHING:
            return COLOR_PAIR_STATUS_PANEL_FETCHING;
        case STATUS_PANEL_NORMAL:
        default:
            return COLOR_PAIR_STATUS_PANEL;
    }
}

// Set footer status atomically (used by fetch thread).
void ui_set_status_panel_state(StatusPanelState state) {
    atomic_store_explicit(&status_panel_state, state, memory_order_relaxed);
}

// Render a bottom footer bar with a contrasting background for interaction hints.
void draw_footer_bar(const char *text) {
    if (!main_win || LINES <= 0) {
        return;
    }

    int footer_y = LINES - 1;
    if (footer_y < 0) {
        return;
    }

    int start_x = (COLS >= 4) ? 2 : 0;
    int panel_width = COLS / 10;
    if (panel_width < 12) {
        panel_width = 12;
    }
    if (panel_width > COLS) {
        panel_width = COLS;
    }
    int panel_x = COLS - panel_width;
    if (panel_x < start_x) {
        panel_x = start_x;
        panel_width = COLS - start_x;
    }
    if (panel_width < 0) {
        panel_width = 0;
    }

    int text_width = panel_x - start_x - 1;
    if (text_width < 0) {
        text_width = 0;
    }

    if (colors_available) {
        wattron(main_win, COLOR_PAIR(COLOR_PAIR_FOOTER_BAR));
    }
    mvwhline(main_win, footer_y, 0, ' ', COLS);
    if (text && text_width > 0) {
        mvwaddnstr(main_win, footer_y, start_x, text, text_width);
    }
    if (colors_available) {
        wattroff(main_win, COLOR_PAIR(COLOR_PAIR_FOOTER_BAR));
    }

    StatusPanelState state = atomic_load_explicit(&status_panel_state, memory_order_relaxed);
    const char *label = status_panel_label(state);
    int label_len = (int)strlen(label);
    int label_max = panel_width - 2;
    if (label_max < 1) {
        label_max = panel_width;
    }
    if (label_len > label_max) {
        label_len = label_max;
    }
    int label_x = panel_x + (panel_width - label_len) / 2;
    if (label_x < panel_x) {
        label_x = panel_x;
    }

    if (panel_width > 0) {
        int pair = status_panel_pair(state);
        if (colors_available && pair > 0) {
            wattron(main_win, COLOR_PAIR(pair) | A_BOLD);
        } else if (colors_available) {
            wattron(main_win, COLOR_PAIR(COLOR_PAIR_FOOTER_BAR) | A_BOLD);
        }
        mvwhline(main_win, footer_y, panel_x, ' ', panel_width);
        if (label_len > 0) {
            mvwaddnstr(main_win, footer_y, label_x, label, label_len);
        }
        if (colors_available && pair > 0) {
            wattroff(main_win, COLOR_PAIR(pair) | A_BOLD);
        } else if (colors_available) {
            wattroff(main_win, COLOR_PAIR(COLOR_PAIR_FOOTER_BAR) | A_BOLD);
        }
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
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED |
              BUTTON3_PRESSED | BUTTON3_RELEASED | BUTTON3_CLICKED |
              BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
    mouseinterval(0);

    // Initialize colors
    colors_available = has_colors();
    if (colors_available) {
        start_color();
        short selection_bg = COLOR_BLUE;
        short footer_bg = COLOR_WHITE;
        short status_bg_normal = COLOR_GREEN;
        short status_bg_fetching = COLOR_BLUE;
        short status_bg_error = COLOR_RED;
        if (can_change_color() && COLORS >= 16) {
            short grey_index = COLORS - 1;
            short deep_red_index = COLORS - 2;
            short deep_blue_index = COLORS - 3;
            short deep_green_index = COLORS - 4;
            init_color(grey_index, 500, 500, 500);
            footer_bg = grey_index;
            init_color(deep_red_index, 600, 0, 0);
            status_bg_error = deep_red_index;
            init_color(deep_blue_index, 0, 0, 600);
            status_bg_fetching = deep_blue_index;
            init_color(deep_green_index, 0, 400, 0);
            status_bg_normal = deep_green_index;
        }
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
        init_pair(COLOR_PAIR_TITLE_BAR, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_PAIR_FOOTER_BAR, COLOR_BLACK, footer_bg);
        init_pair(COLOR_PAIR_STATUS_PANEL, COLOR_WHITE, status_bg_normal);
        init_pair(COLOR_PAIR_STATUS_PANEL_FETCHING, COLOR_WHITE, status_bg_fetching);
        init_pair(COLOR_PAIR_STATUS_PANEL_ALERT, COLOR_YELLOW, status_bg_error);
        init_pair(COLOR_PAIR_INFO_OPEN, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PAIR_INFO_HIGH, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_PAIR_INFO_LOW, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PAIR_INFO_CLOSE, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PAIR_INFO_CURRENT, COLOR_WHITE, COLOR_BLACK);
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

void ui_chart_reset_viewport(void) {
    reset_chart_view_state();
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

    const char *loading1 = "LOADING...";
    const char *loading2 = "FETCHING DATA FROM BINANCE API";

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

// Proxy to wgetch so the UI layer can remain decoupled from ncurses details.
int handle_input(void) {
    return wgetch(main_win);
}
