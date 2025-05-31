# Compiler and flags
CXX := $(TOOL_PREFIX)g++
CC := $(TOOL_PREFIX)gcc
CXXFLAGS := -Wall -Wextra -fPIC
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

# Source files
DAEMON_SRCS := $(wildcard $(DAEMON_DIR)/*.cpp) $(wildcard $(DAEMON_DIR)/utils/*.c)
DAEMON_OBJS := $(patsubst $(DAEMON_DIR)/%.cpp,$(BUILD_DIR)/daemon/%.o,$(filter %.cpp,$(DAEMON_SRCS))) \
               $(patsubst $(DAEMON_DIR)/%.c,$(BUILD_DIR)/daemon/%.o,$(filter %.c,$(DAEMON_SRCS)))

MODULE_SRCS := $(wildcard $(MODULE_DIR)/*/*.cpp)
MODULE_OBJS := $(patsubst $(MODULE_DIR)/%.cpp,$(BUILD_DIR)/module/%.o,$(MODULE_SRCS))
MODULE_TARGETS := $(patsubst $(MODULE_DIR)/%.cpp,$(BIN_DIR)/modules/%.so,$(MODULE_SRCS))

OCONFIG_SRCS := $(wildcard $(OCONFIG_DIR)/*.cpp)
OCONFIG_OBJS := $(patsubst $(OCONFIG_DIR)/%.cpp,$(BUILD_DIR)/oconfig/%.o,$(OCONFIG_SRCS))

# Targets
TARGET := $(BIN_DIR)/collect

# Includes
INCLUDES := -I$(DAEMON_DIR) -I$(OCONFIG_DIR) -I$(MODULE_DIR) -I$(DAEMON_DIR)/utils

# Rules
all: dirs $(TARGET) $(MODULE_TARGETS) copy_share

dirs:
	@mkdir -p $(BUILD_DIR)/daemon $(BUILD_DIR)/daemon/utils $(BUILD_DIR)/module $(BUILD_DIR)/oconfig
	@mkdir -p $(BIN_DIR)/modules

$(TARGET): $(DAEMON_OBJS) $(OCONFIG_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS_EXE)

# Module shared libraries - flatten directory structure
$(BIN_DIR)/modules/%.so: $(BUILD_DIR)/module/%.o
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $<

# Daemon compilation
$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/daemon/utils/%.o: $(DAEMON_DIR)/utils/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Module compilation
$(BUILD_DIR)/module/%.o: $(MODULE_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# OConfig compilation
$(BUILD_DIR)/oconfig/%.o: $(OCONFIG_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

copy_share:
	@echo "Copying $(SHARE_DIR_SRC) to $(BIN_DIR)/"
	@cp -a $(SHARE_DIR_SRC) $(BIN_DIR)/

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

.PHONY: all clean dirs copy_share
