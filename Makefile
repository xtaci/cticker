CC ?= cc
PKG_CONFIG ?= pkg-config

BASE_CPPFLAGS =
BASE_CFLAGS = -Wall -Wextra -O2 -pthread
BASE_LDFLAGS = -lm -lpthread

CPPFLAGS ?= $(BASE_CPPFLAGS)
CFLAGS ?= $(BASE_CFLAGS)
LDFLAGS ?= $(BASE_LDFLAGS)

# Shell-evaluated flags for pkg-config with macOS fallback when pkg-config is missing.
PKG_CFLAGS = `command -v $(PKG_CONFIG) >/dev/null 2>&1 && ( $(PKG_CONFIG) --cflags libcurl jansson ncursesw 2>/dev/null || $(PKG_CONFIG) --cflags libcurl jansson ncurses )`
PKG_LDFLAGS = `if command -v $(PKG_CONFIG) >/dev/null 2>&1; then ( $(PKG_CONFIG) --libs libcurl jansson ncursesw 2>/dev/null || $(PKG_CONFIG) --libs libcurl jansson ncurses ); else if [ "$$(uname -s)" = "Darwin" ]; then echo -lcurl -ljansson -lncurses; else echo -lcurl -ljansson -lncursesw; fi; fi`

TARGET = cticker
SOURCES = main.c config.c api.c ui_core.c ui_format.c ui_priceboard.c ui_chart.c priceboard.c chart.c runtime.c fetcher.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(PKG_LDFLAGS)

%.o: %.c cticker.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

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
