CC = gcc
PKG_CONFIG ?= pkg-config

BASE_CFLAGS = -Wall -Wextra -O2 -pthread
BASE_LDFLAGS = -lm -lpthread

UNAME_S := $(shell uname -s)
HAVE_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) 2>/dev/null)

ifeq ($(HAVE_PKG_CONFIG),)
	# Fallback when pkg-config is unavailable
	ifeq ($(UNAME_S),Darwin)
		NCURSES_LIB = -lncurses
	else
		NCURSES_LIB = -lncursesw
	endif
	CFLAGS = $(BASE_CFLAGS)
	LDFLAGS = $(BASE_LDFLAGS) -lcurl -ljansson $(NCURSES_LIB)
else
	# Prefer ncursesw when available; fall back to ncurses (macOS Homebrew)
	NCURSES_PKG := $(shell $(PKG_CONFIG) --exists ncursesw && echo ncursesw || echo ncurses)
	PKGS = libcurl jansson $(NCURSES_PKG)
	CFLAGS = $(BASE_CFLAGS) $(shell $(PKG_CONFIG) --cflags $(PKGS))
	LDFLAGS = $(BASE_LDFLAGS) $(shell $(PKG_CONFIG) --libs $(PKGS))
endif

TARGET = cticker
SOURCES = main.c config.c api.c ui.c ui_core.c ui_format.c ui_priceboard.c ui_chart.c priceboard.c chart.c runtime.c fetcher.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c cticker.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@which pkg-config > /dev/null || (echo "pkg-config not found" && exit 1)
	@pkg-config --exists libcurl || (echo "libcurl not found" && exit 1)
	@pkg-config --exists jansson || (echo "jansson not found" && exit 1)
	@pkg-config --exists ncursesw || pkg-config --exists ncurses || (echo "ncursesw or ncurses not found" && exit 1)
	@echo "All dependencies are installed"
