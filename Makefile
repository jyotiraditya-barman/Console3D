
CC = gcc

TARGET = bin/textured_rasterizer
SRC = src/obj.c

CFLAGS = -O2 -Wall -Wextra -pthread
LIBS = $(shell pkg-config --cflags --libs notcurses) -lm

.PHONY: all run clean debug rebuild

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	@echo "Building TUI3D..."
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)
	@echo "Done: $(TARGET)"

run: $(TARGET)
	./$(TARGET) models/fish.obj

debug:
	@mkdir -p bin
	$(CC) -g -O0 -Wall -Wextra -pthread \
		$(SRC) -o $(TARGET) \
		$(shell pkg-config --cflags --libs notcurses) -lm

rebuild: clean all

clean:
	rm -f $(TARGET)

help:
	@echo "TUI3D build commands:"
	@echo "  make                         Build"
	@echo "  make run                     Build and run fish.obj"
	@echo "  make debug                   Debug build"
	@echo "  make rebuild                 Clean and rebuild"
	@echo "  make clean                   Remove executable"
