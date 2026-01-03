#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cticker.h"

#define HOME_DIR getenv("HOME")

// Load configuration from home directory
int load_config(Config *config) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", HOME_DIR, CONFIG_FILE);
    
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
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) {
            strcpy(config->symbols[config->symbol_count], line);
            config->symbol_count++;
        }
    }
    
    fclose(fp);
    return 0;
}

// Save configuration to home directory
int save_config(const Config *config) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", HOME_DIR, CONFIG_FILE);
    
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
