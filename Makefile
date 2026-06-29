CC := gcc
CFLAGS := -std=c99 -O2 -Wall -Wextra $(shell pkg-config --cflags sdl3)
CFLAGS_DEBUG := -std=c99 -g -O0 -Wall -Wextra -DDEBUG $(shell pkg-config --cflags sdl3)
LDFLAGS := $(shell pkg-config --libs sdl3)

SRCS := $(wildcard src/*.c)
purplegb: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug: $(SRCS)
	$(CC) $(CFLAGS_DEBUG) -o purplegb-debug $^ $(LDFLAGS)

clean:
	rm -f purplegb purplegb-debug

.PHONY: all clean debug
