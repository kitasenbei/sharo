CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g $(shell pkg-config --cflags sdl3 sdl3-ttf 2>/dev/null)
LDFLAGS = $(shell pkg-config --libs sdl3 sdl3-ttf 2>/dev/null)

SRC_DIR = src
BUILD_DIR = build

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = sharo

.PHONY: all clean run test

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: all
	./$(TARGET)

test: all
	./$(TARGET) examples/test.sharo
