# Makefile for DRAM bandwidth benchmark
# Builds a single statically-linked binary with proper object file management

CXX = g++
CXXFLAGS_COMMON = -std=c++11 -O2 -Wall -MMD -MP

# Paths
DRAMSIM3_DIR = ./submodules/DRAMSim3
BUILD_DIR = ./build

# Include paths
INCLUDES = -I$(DRAMSIM3_DIR)/src \
           -I$(DRAMSIM3_DIR)/ext/fmt/include \
           -I$(DRAMSIM3_DIR)/ext/headers \
           -DFMT_HEADER_ONLY=1

CXXFLAGS = $(CXXFLAGS_COMMON) $(INCLUDES)

# Source files
MAIN_SRC = bandwidth_benchmark.cpp

DRAMSIM3_SRCS = $(DRAMSIM3_DIR)/src/bankstate.cc \
                $(DRAMSIM3_DIR)/src/channel_state.cc \
                $(DRAMSIM3_DIR)/src/command_queue.cc \
                $(DRAMSIM3_DIR)/src/common.cc \
                $(DRAMSIM3_DIR)/src/configuration.cc \
                $(DRAMSIM3_DIR)/src/controller.cc \
                $(DRAMSIM3_DIR)/src/dram_system.cc \
                $(DRAMSIM3_DIR)/src/hmc.cc \
                $(DRAMSIM3_DIR)/src/memory_system.cc \
                $(DRAMSIM3_DIR)/src/refresh.cc \
                $(DRAMSIM3_DIR)/src/simple_stats.cc \
                $(DRAMSIM3_DIR)/src/timing.cc

# Object files
MAIN_OBJ = $(BUILD_DIR)/main.o
DRAMSIM3_OBJS = $(patsubst $(DRAMSIM3_DIR)/src/%.cc,$(BUILD_DIR)/dramsim3/%.o,$(DRAMSIM3_SRCS))

ALL_OBJS = $(MAIN_OBJ) $(DRAMSIM3_OBJS)

# Dependency files
DEPS = $(ALL_OBJS:.o=.d)

# Target
TARGET = bandwidth_benchmark

.PHONY: all clean help directories

all: directories $(TARGET)

# Create build directories
directories:
	@mkdir -p $(BUILD_DIR)/dramsim3

# Main source compilation
$(BUILD_DIR)/main.o: $(MAIN_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# DRAMSim3 source compilation
$(BUILD_DIR)/dramsim3/%.o: $(DRAMSIM3_DIR)/src/%.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Linking
$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Include dependency files
-include $(DEPS)

# Clean
clean:
	rm -f $(TARGET)
	rm -rf $(BUILD_DIR)
	rm -rf results/
