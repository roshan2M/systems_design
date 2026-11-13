CXX      := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -g

BUILD_DIR := build
SRCS      := $(wildcard *.cc)
HDRS      := $(wildcard *.h)
BINS      := $(patsubst %.cc,$(BUILD_DIR)/%,$(SRCS))

# Default: compile and run lsm_tree (or pass TARGET=foo to run another file)
TARGET ?= lsm_tree

.PHONY: all run clean

run: $(BUILD_DIR)/$(TARGET)
	./$(BUILD_DIR)/$(TARGET)

all: $(BINS)

$(BUILD_DIR)/%: %.cc $(HDRS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

