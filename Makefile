
# ============================================================
# TUI3D - Terminal OBJ Viewer
# ============================================================

CC := gcc

TARGET := bin/tui3d
SRC    := src/obj.c

CFLAGS := -std=c11 \
          -O2 \
          -Wall \
          -Wextra \
          -Wpedantic \
          -pthread \
          -Isrc

LDLIBS := -lnotcurses -lnotcurses-core -lm -pthread


# ------------------------------------------------------------
# Default
# ------------------------------------------------------------

.PHONY: all
all: $(TARGET)


# ------------------------------------------------------------
# Build
# ------------------------------------------------------------

$(TARGET): $(SRC)
	@mkdir -p bin
	@echo "CC  $<"
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
	@echo
	@echo "Built: $(TARGET)"


# ------------------------------------------------------------
# Run
#
# Example:
#   make run MODEL=models/fish.obj
# ------------------------------------------------------------

.PHONY: run
run: $(TARGET)
	@if [ -z "$(MODEL)" ]; then \
		echo "Usage: make run MODEL=models/model.obj"; \
		exit 1; \
	fi
	./$(TARGET) "$(MODEL)"


# ------------------------------------------------------------
# Run multiple models
#
# Example:
#   make multi MODELS="models/fish.obj models/cube.obj"
# ------------------------------------------------------------

.PHONY: multi
multi: $(TARGET)
	@if [ -z "$(MODELS)" ]; then \
		echo "Usage: make multi MODELS=\"models/a.obj models/b.obj\""; \
		exit 1; \
	fi
	./$(TARGET) $(MODELS)


# ------------------------------------------------------------
# Debug build
# ------------------------------------------------------------

.PHONY: debug
debug:
	@mkdir -p bin
	$(CC) -std=c11 \
	      -O0 \
	      -g3 \
	      -Wall \
	      -Wextra \
	      -Wpedantic \
	      -pthread \
	      -Isrc \
	      src/obj.c \
	      -o bin/tui3d \
	      -lnotcurses -lnotcurses-core -lm -pthread


# ------------------------------------------------------------
# Release build
# ------------------------------------------------------------

.PHONY: release
release:
	@mkdir -p bin
	$(CC) -std=c11 \
	      -O3 \
	      -DNDEBUG \
	      -Wall \
	      -Wextra \
	      -Wpedantic \
	      -pthread \
	      -Isrc \
	      src/obj.c \
	      -o bin/tui3d \
	      -lnotcurses -lnotcurses-core -lm -pthread


# ------------------------------------------------------------
# Clean
# ------------------------------------------------------------

.PHONY: clean
clean:
	rm -f bin/tui3d


# ------------------------------------------------------------
# Rebuild
# ------------------------------------------------------------

.PHONY: rebuild
rebuild: clean all


# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------

.PHONY: deps
deps:
	sudo apt install build-essential libnotcurses-dev


# ------------------------------------------------------------
# Information
# ------------------------------------------------------------

.PHONY: info
info:
	@echo "TUI3D"
	@echo "--------------------------"
	@echo "Compiler : $(CC)"
	@echo "Source   : $(SRC)"
	@echo "Target   : $(TARGET)"
	@echo "Models   : models/"


# ------------------------------------------------------------
# Help
# ------------------------------------------------------------

.PHONY: help
help:
	@echo
	@echo "TUI3D"
	@echo
	@echo "  make"
	@echo "      Build the viewer"
	@echo
	@echo "  make run MODEL=models/fish.obj"
	@echo "      Build and run one model"
	@echo
	@echo "  make multi MODELS=\"models/a.obj models/b.obj\""
	@echo "      Run multiple models"
	@echo
	@echo "  make debug"
	@echo "      Debug build"
	@echo
	@echo "  make release"
	@echo "      Optimized build"
	@echo
	@echo "  make clean"
	@echo "      Remove executable"
	@echo
	@echo "  make rebuild"
	@echo "      Clean and rebuild"
	@echo

