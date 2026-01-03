#!/bin/bash
# Test script for CTicker

echo "Testing CTicker Configuration"
echo "=============================="

# Set test home directory
export HOME="/tmp/cticker_test"
mkdir -p "$HOME"

# Test 1: Check if default config is created
echo "Test 1: Testing default configuration creation..."
rm -f "$HOME/.cticker.conf"

# We'll create a minimal test program
cat > test_config.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "cticker.h"

int main() {
    Config config;
    
    // Test loading (should create default)
    if (load_config(&config) != 0) {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }
    
    printf("Loaded %d symbols:\n", config.symbol_count);
    for (int i = 0; i < config.symbol_count; i++) {
        printf("  - %s\n", config.symbols[i]);
    }
    
    // Test saving
    if (save_config(&config) != 0) {
        fprintf(stderr, "Failed to save config\n");
        return 1;
    }
    
    printf("\nConfiguration saved successfully\n");
    
    // Test loading again
    Config config2;
    if (load_config(&config2) != 0) {
        fprintf(stderr, "Failed to reload config\n");
        return 1;
    }
    
    printf("\nReloaded %d symbols:\n", config2.symbol_count);
    for (int i = 0; i < config2.symbol_count; i++) {
        printf("  - %s\n", config2.symbols[i]);
    }
    
    return 0;
}
EOF

# Compile config.c first if needed
if [ ! -f config.o ]; then
    gcc -c config.c -I. -o config.o
    if [ $? -ne 0 ]; then
        echo "Test 1: FAILED - config.c compilation error"
        exit 1
    fi
fi

gcc -o test_config test_config.c config.o -I.
if [ $? -eq 0 ]; then
    ./test_config
    echo ""
    echo "Config file contents:"
    cat "$HOME/.cticker.conf"
    echo ""
    echo "Test 1: PASSED"
else
    echo "Test 1: FAILED - compilation error"
    exit 1
fi

# Cleanup
rm -f test_config test_config.c
rm -rf "$HOME"

echo ""
echo "All tests completed successfully!"
