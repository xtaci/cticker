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
 * @file config.c
 * @brief Load/save the user's symbol list from $HOME/.cticker.conf.
 *
 * File format:
 * - One symbol per line (e.g. BTCUSDT)
 * - Empty lines are ignored
 * - Lines starting with '#' are treated as comments
 *
 * If the config file is missing, we create a small default set.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "cticker.h"

/**
 * @brief Resolve the home directory.
 *
 * Falls back to /tmp if HOME is not set (best-effort).
 */
static const char* get_home_dir(void) {
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}

/**
 * @brief Trim whitespace from a string in-place.
 * @return A pointer inside the original buffer.
 *
 * @note This mutates the input string; callers should pass a mutable buffer.
 */
static char* trim_whitespace(char str[static 1]) {
    char *end;
    
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    
    if(*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

/**
 * @brief Load configuration from $HOME/.cticker.conf.
 *
 * On first run (no config file present), creates a default config file and
 * returns success.
 */
int load_config(Config config[static 1]) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", get_home_dir(), CONFIG_FILE);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        /* No config file yet: create a simple default watchlist. */
        config->symbol_count = 3;
        snprintf(config->symbols[0], MAX_SYMBOL_LEN, "%s", "BTCUSDT");
        snprintf(config->symbols[1], MAX_SYMBOL_LEN, "%s", "ETHUSDT");
        snprintf(config->symbols[2], MAX_SYMBOL_LEN, "%s", "BNBUSDT");
        save_config(config);
        return 0;
    }
    
    config->symbol_count = 0;
    char line[MAX_SYMBOL_LEN + 2];
    
    while (fgets(line, sizeof(line), fp) && config->symbol_count < MAX_SYMBOLS) {
        /* Trim whitespace and newline. */
        char *trimmed = trim_whitespace(line);
        
        /* Skip empty lines and comments. */
        if (strlen(trimmed) == 0 || trimmed[0] == '#') {
            continue;
        }
        
        snprintf(config->symbols[config->symbol_count], MAX_SYMBOL_LEN, "%s", trimmed);
        config->symbol_count++;
    }
    
    fclose(fp);
    return 0;
}

/**
 * @brief Save configuration to $HOME/.cticker.conf.
 *
 * Overwrites the file; the config is intentionally simple and human-editable.
 */
int save_config(const Config config[static 1]) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", get_home_dir(), CONFIG_FILE);
    
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        return -1;
    }
    
    for (int i = 0; i < config->symbol_count; i++) {
        fprintf(fp, "%s\n", config->symbols[i]);
    }
    
    fclose(fp);
    return 0;
}
