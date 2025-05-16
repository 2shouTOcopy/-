# Compiler and flags
CXX := $(TOOL_PREFIX)g++
CC := $(TOOL_PREFIX)gcc
CXXFLAGS := -Wall -Wextra -fPIC -std=c++11 # 建议添加 C++ 标准
CFLAGS := -Wall -Wextra -fPIC
LDFLAGS := -shared -fPIC
LDFLAGS_EXE := -ldl -lrt -lpthread -rdynamic

# Directories
SRC_DIR := .
DAEMON_DIR := $(SRC_DIR)/daemon
MODULE_DIR := $(SRC_DIR)/module
OCONFIG_DIR := $(SRC_DIR)/oconfig
SHARE_DIR_SRC := $(SRC_DIR)/share # 源 share 目录

BUILD_DIR := build
BIN_DIR := bin

# --- Daemon ---
DAEMON_SRCS := $(wildcard $(DAEMON_DIR)/*.cpp) $(wildcard $(DAEMON_DIR)/utils/*.c)
# Correctly maps daemon/foo.cpp to build/daemon/foo.o and daemon/utils/bar.c to build/daemon/utils/bar.o
DAEMON_OBJS := $(patsubst $(DAEMON_DIR)/%.cpp,$(BUILD_DIR)/daemon/%.o,$(filter %.cpp,$(DAEMON_SRCS))) \
               $(patsubst $(DAEMON_DIR)/%.c,$(BUILD_DIR)/daemon/%.o,$(filter %.c,$(DAEMON_SRCS)))

# --- Modules ---
# Find all .cpp files in module subdirectories (e.g., module/cpu/cpu.cpp, module/cpu/helper.cpp)
MODULE_ALL_SRCS := $(wildcard $(MODULE_DIR)/*/*.cpp)

# Create corresponding object file paths (e.g., build/module/cpu/cpu.o, build/module/cpu/helper.o)
# '%' will match 'subdir/file', so 'module/cpu/cpu.cpp' -> 'build/module/cpu/cpu.o'
MODULE_OBJS := $(patsubst $(MODULE_DIR)/%.cpp,$(BUILD_DIR)/module/%.o,$(MODULE_ALL_SRCS))

# Extract unique module names (e.g., cpu, csv) from the source file paths
# $(dir $(MODULE_ALL_SRCS)) -> module/cpu/ module/cpu/ module/csv/
# $(notdir $(patsubst %/,%, ...)) -> cpu cpu csv
MODULE_NAMES := $(sort $(unique $(foreach src,$(MODULE_ALL_SRCS),$(notdir $(patsubst %/,%,$(dir $(src)))))))

# Define target .so files (e.g., bin/modules/cpu.so, bin/modules/csv.so)
MODULE_TARGETS := $(foreach modname,$(MODULE_NAMES),$(BIN_DIR)/modules/$(modname).so)

# --- OConfig ---
OCONFIG_SRCS := $(wildcard $(OCONFIG_DIR)/*.cpp)
OCONFIG_OBJS := $(patsubst $(OCONFIG_DIR)/%.cpp,$(BUILD_DIR)/oconfig/%.o,$(OCONFIG_SRCS))

# --- Main Target ---
TARGET := $(BIN_DIR)/collect

# --- Includes ---
INCLUDES := -I$(DAEMON_DIR) -I$(OCONFIG_DIR) -I$(MODULE_DIR) -I$(DAEMON_DIR)/utils

# ================= RULES =================

all: dirs $(TARGET) $(MODULE_TARGETS) copy_share

dirs:
	@echo "Creating directories..."
	@mkdir -p $(BUILD_DIR)/daemon $(BUILD_DIR)/module $(BUILD_DIR)/oconfig
	@mkdir -p $(BIN_DIR)/modules $(BIN_DIR)

$(TARGET): $(DAEMON_OBJS) $(OCONFIG_OBJS)
	@echo "Linking executable $@"
	$(CXX) -o $@ $^ $(LDFLAGS_EXE)

# --- Daemon Compilation ---
# For .cpp files in DAEMON_DIR (e.g., daemon/Cmd.cpp -> build/daemon/Cmd.o)
$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# For .c files directly under DAEMON_DIR (if any)
# This rule handles cases where '%' contains a path part, e.g. 'utils/utils'
# So daemon/utils/utils.c -> build/daemon/utils/utils.o
$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# More specific rule for daemon/utils C files (takes precedence if applicable)
# Matches daemon/utils/foo.c -> build/daemon/utils/foo.o
$(BUILD_DIR)/daemon/utils/%.o: $(DAEMON_DIR)/utils/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- Module Object Compilation ---
# For .cpp files in module subdirectories (e.g., module/cpu/cpu.cpp -> build/module/cpu/cpu.o)
# '%' will match 'modulename/filename', e.g., 'cpu/cpu' or 'cpu/helper'
$(BUILD_DIR)/module/%.o: $(MODULE_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Module Shared Library Linking ---
# This uses a 'define' and 'foreach-eval-call' to create specific rules for each module.
# For each 'modname' (e.g., cpu):
# Target: bin/modules/cpu.so
# Prerequisites: All .o files in build/module/cpu/ (e.g. build/module/cpu/cpu.o, build/module/cpu/helper.o)
# These .o files are selected from the global MODULE_OBJS list.
define MODULE_SO_template
$(BIN_DIR)/modules/$(1).so: $(filter $(BUILD_DIR)/module/$(1)/%.o,$(MODULE_OBJS))
	@echo "Linking module $$@"
	@mkdir -p $$(@D)
	$$(CXX) $$(LDFLAGS) -o $$@ $$^

# Ensure that the prerequisite .o files are themselves targets that Make knows how to build.
# This is implicitly handled by the $(BUILD_DIR)/module/%.o rule and MODULE_OBJS dependency.
endef

$(foreach modname,$(MODULE_NAMES),$(eval $(call MODULE_SO_template,$(modname))))

# --- OConfig Compilation ---
$(BUILD_DIR)/oconfig/%.o: $(OCONFIG_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Copy Share Directory ---
copy_share:
	@echo "Copying $(SHARE_DIR_SRC) to $(BIN_DIR)/"
	@cp -a $(SHARE_DIR_SRC) $(BIN_DIR)/

# --- Clean Rule ---
clean:
	@echo "Cleaning build and bin directories..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)

.PHONY: all clean dirs copy_share
