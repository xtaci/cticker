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
 * @file ui_format.c
 * @brief Formatting helpers used by UI rendering.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

// Apply thousands separators to a numeric string.
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

// Format a number with a precision that keeps small prices legible.
void ui_format_number(char *buf, size_t size, double num) {
    if (fabs(num) >= 1.0) {
        snprintf(buf, size, "%.2f", num);
    } else {
        snprintf(buf, size, "%.8f", num);
    }
}

// Trim useless trailing zeros (and the decimal point, if needed) from
// numeric strings. This is applied only at render time so we preserve the
// original API payload elsewhere.
void ui_trim_trailing_zeros(char *buf) {
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
// show meaningful precision.
void ui_format_axis_price(char *buf, size_t size, double num, double range) {
    int decimals = 2;
    if (range < 0.5) decimals = 4;
    if (range < 0.05) decimals = 6;
    if (range < 0.005) decimals = 8;
    if (range < 0.0005) decimals = 10;
    if (decimals > 10) decimals = 10;
    snprintf(buf, size, "%.*f", decimals, num);
    ui_trim_trailing_zeros(buf);
}

// Format a number then apply thousands separators.
void ui_format_number_with_commas(char *buf, size_t size, double num) {
    char raw[64];
    ui_format_number(raw, sizeof(raw), num);
    insert_commas(raw, buf, size);
}

// Format an integer with thousands separators.
void ui_format_integer_with_commas(char *buf, size_t size, long long value) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%lld", value);
    insert_commas(raw, buf, size);
}

// Translate an enum period into a user-facing label.
const char *ui_period_label(Period period) {
    switch (period) {
        case PERIOD_1MIN: return "1 MINUTE";
        case PERIOD_15MIN: return "15 MINUTES";
        case PERIOD_1HOUR: return "1 HOUR";
        case PERIOD_4HOUR: return "4 HOURS";
        case PERIOD_1DAY: return "1 DAY";
        case PERIOD_1WEEK: return "1 WEEK";
        case PERIOD_1MONTH: return "1 MONTH";
        default: return "Unknown";
    }
}
