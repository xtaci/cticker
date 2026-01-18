CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lcurl -ljansson -lncursesw -lm -lpthread

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
	@pkg-config --exists ncursesw || (echo "ncursesw not found" && exit 1)
	@echo "All dependencies are installed"
