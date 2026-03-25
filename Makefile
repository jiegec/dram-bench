# Makefile for DRAM bandwidth benchmark
# Builds a single statically-linked binary with proper object file management

CXX = g++
CXXFLAGS_COMMON = -std=c++20 -O2 -MMD -MP

# Paths
DRAMSIM3_DIR = ./submodules/DRAMSim3
RAMULATOR2_DIR = ./submodules/ramulator2
BUILD_DIR = ./build

# Include paths
INCLUDES = -I$(DRAMSIM3_DIR)/src \
           -I$(DRAMSIM3_DIR)/ext/fmt/include \
           -I$(DRAMSIM3_DIR)/ext/headers \
           -I$(RAMULATOR2_DIR)/src \
           -I$(RAMULATOR2_DIR)/ext/spdlog/include \
           -I$(RAMULATOR2_DIR)/ext/yaml-cpp/include

CXXFLAGS = $(CXXFLAGS_COMMON) $(INCLUDES) -DFMT_HEADER_ONLY=1

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
ALL_LIBS = $(RAMULATOR2_DIR)/build/libramulator.a $(RAMULATOR2_DIR)/build/_deps/yaml-cpp-build/libyaml-cpp.a
LDFLAGS = -Wl,--whole-archive $(RAMULATOR2_DIR)/build/libramulator.a -Wl,--no-whole-archive $(RAMULATOR2_DIR)/build/_deps/yaml-cpp-build/libyaml-cpp.a

# Dependency files
DEPS = $(ALL_OBJS:.o=.d)

# Target
TARGET = bandwidth_benchmark

.PHONY: all clean help directories

all: directories $(TARGET)

# Build Ramulator2 as a static library using cmake
$(RAMULATOR2_DIR)/build/libramulator.a:
	echo "Building Ramulator2 library..."
	sed -i "s/ramulator SHARED/ramulator STATIC/" $(RAMULATOR2_DIR)/CMakeLists.txt
	mkdir -p $(RAMULATOR2_DIR)/build
	cmake -S $(RAMULATOR2_DIR) -B $(RAMULATOR2_DIR)/build -DCMAKE_BUILD_TYPE=Release
	make -C $(RAMULATOR2_DIR)/build -j16
	sed -i "s/ramulator STATIC/ramulator SHARED/" $(RAMULATOR2_DIR)/CMakeLists.txt

# Create build directories
directories:
	mkdir -p $(BUILD_DIR)/dramsim3

# Main source compilation
$(BUILD_DIR)/main.o: $(MAIN_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# DRAMSim3 source compilation
$(BUILD_DIR)/dramsim3/%.o: $(DRAMSIM3_DIR)/src/%.cc
	$(CXX) $(CXXFLAGS) -std=c++17 -c $< -o $@

# Linking
$(TARGET): $(ALL_OBJS) $(ALL_LIBS)
	$(CXX) $(CXXFLAGS) -o $@ $(ALL_OBJS) $(LDFLAGS)

# Include dependency files
-include $(DEPS)

# Clean
clean:
	rm -rf $(TARGET)
	rm -rf $(BUILD_DIR)
	rm -rf results/
	rm -rf $(RAMULATOR2_DIR)/build
