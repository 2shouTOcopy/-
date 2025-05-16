# Compiler and flags
CXX := $(TOOL_PREFIX)g++
CC := $(TOOL_PREFIX)gcc
CXXFLAGS := -Wall -Wextra -fPIC -std=c++11
CFLAGS := -Wall -Wextra -fPIC
LDFLAGS := -shared -fPIC
LDFLAGS_EXE := -ldl -lrt -lpthread -rdynamic

# Directories
SRC_DIR := .
DAEMON_DIR := $(SRC_DIR)/daemon
MODULE_DIR := $(SRC_DIR)/module
OCONFIG_DIR := $(SRC_DIR)/oconfig
SHARE_DIR_SRC := $(SRC_DIR)/share

BUILD_DIR := build
BIN_DIR := bin

# --- Daemon ---
DAEMON_SRCS := $(wildcard $(DAEMON_DIR)/*.cpp) $(wildcard $(DAEMON_DIR)/utils/*.c)
DAEMON_OBJS := $(patsubst $(DAEMON_DIR)/%.cpp,$(BUILD_DIR)/daemon/%.o,$(filter %.cpp,$(DAEMON_SRCS))) \
               $(patsubst $(DAEMON_DIR)/%.c,$(BUILD_DIR)/daemon/%.o,$(filter %.c,$(DAEMON_SRCS)))

# --- Modules ---
MODULE_ALL_SRCS := $(wildcard $(MODULE_DIR)/*/*.cpp)
MODULE_OBJS := $(patsubst $(MODULE_DIR)/%.cpp,$(BUILD_DIR)/module/%.o,$(MODULE_ALL_SRCS))
MODULE_NAMES := $(sort $(unique $(foreach src,$(MODULE_ALL_SRCS),$(notdir $(patsubst %/,%,$(dir $(src)))))))
MODULE_TARGETS := $(foreach modname,$(MODULE_NAMES),$(BIN_DIR)/modules/$(modname).so)

# --- OConfig ---
OCONFIG_SRCS := $(wildcard $(OCONFIG_DIR)/*.cpp)
OCONFIG_OBJS := $(patsubst $(OCONFIG_DIR)/%.cpp,$(BUILD_DIR)/oconfig/%.o,$(OCONFIG_SRCS))

# --- Main Target ---
TARGET := $(BIN_DIR)/collect

# --- Includes ---
INCLUDES := -I$(DAEMON_DIR) -I$(OCONFIG_DIR) -I$(MODULE_DIR) -I$(DAEMON_DIR)/utils

# +++ Add these $(info) lines for debugging +++
$(info --- Makefile Debug Info ---)
$(info TOOL_PREFIX is [$(TOOL_PREFIX)])
$(info MODULE_DIR is [$(MODULE_DIR)])
$(info MODULE_ALL_SRCS is [$(MODULE_ALL_SRCS)])
$(info MODULE_OBJS is [$(MODULE_OBJS)])
$(info MODULE_NAMES is [$(MODULE_NAMES)])
$(info MODULE_TARGETS is [$(MODULE_TARGETS)])
$(info --- End Debug Info ---)

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
$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/daemon/utils/%.o: $(DAEMON_DIR)/utils/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- Module Object Compilation ---
$(BUILD_DIR)/module/%.o: $(MODULE_DIR)/%.cpp
	@echo "Compiling module object $@"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Module Shared Library Linking ---
define MODULE_SO_template
# +++ Add these $(info) lines inside the define for debugging each module rule generation +++
$$(info --- Generating rule for module [$(1)] ---)
$$(info Target: [$(BIN_DIR)/modules/$(1).so])
$$(info Prerequisites for $(1).so: [$(filter $(BUILD_DIR)/module/$(1)/%.o,$(MODULE_OBJS))])

$(BIN_DIR)/modules/$(1).so: $(filter $(BUILD_DIR)/module/$(1)/%.o,$(MODULE_OBJS))
	@echo "Linking module $$@"
	@mkdir -p $$(@D)
	$$(CXX) $$(LDFLAGS) -o $$@ $$^
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
