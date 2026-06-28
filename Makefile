CC := gcc
CFLAGS := -std=c99 -O2 -Wall -Wextra $(shell pkg-config --cflags sdl3)
LDFLAGS := $(shell pkg-config --libs sdl3) -lm

SRCS := $(wildcard src/*.c)
purplegb: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f purplegb

.PHONY: clean
