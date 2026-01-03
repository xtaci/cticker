#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "cticker.h"

// Get home directory with fallback
static const char* get_home_dir(void) {
    const char *home = getenv("HOME");
    return home ? home : "/tmp";
}

// Trim whitespace from string
static char* trim_whitespace(char *str) {
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

// Load configuration from home directory
int load_config(Config *config) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", get_home_dir(), CONFIG_FILE);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        // Create default config
        config->symbol_count = 3;
        strcpy(config->symbols[0], "BTCUSDT");
        strcpy(config->symbols[1], "ETHUSDT");
        strcpy(config->symbols[2], "BNBUSDT");
        return 0;
    }
    
    config->symbol_count = 0;
    char line[MAX_SYMBOL_LEN + 2];
    
    while (fgets(line, sizeof(line), fp) && config->symbol_count < MAX_SYMBOLS) {
        // Trim whitespace
        char *trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (strlen(trimmed) == 0 || trimmed[0] == '#') {
            continue;
        }
        
        strcpy(config->symbols[config->symbol_count], trimmed);
        config->symbol_count++;
    }
    
    fclose(fp);
    return 0;
}

// Save configuration to home directory
int save_config(const Config *config) {
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
