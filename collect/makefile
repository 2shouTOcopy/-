# 主 Makefile
# Compiler and flags
TOOL_PREFIX ?=
CXX := $(TOOL_PREFIX)g++
CC := $(TOOL_PREFIX)gcc
# 全局编译选项 (可以被子Makefile继承或覆盖)
CXXFLAGS ?= -Wall -Wextra -fPIC -std=c++11
CFLAGS ?= -Wall -Wextra -fPIC
# 主程序链接选项
LDFLAGS_EXE ?= -ldl -lrt -lpthread -rdynamic
# 模块 .so 文件链接选项 (传递给子Makefile)
LDFLAGS_FOR_SO ?= -shared -fPIC

# Directories
SRC_DIR := .
DAEMON_DIR := $(SRC_DIR)/daemon
MODULE_DIR := $(SRC_DIR)/module
OCONFIG_DIR := $(SRC_DIR)/oconfig
SHARE_DIR_SRC := $(SRC_DIR)/share

BUILD_DIR := build
BIN_DIR := bin

# --- 获取绝对路径，方便传递给子 Makefile ---
ABS_SRC_DIR := $(abspath $(SRC_DIR))
ABS_BUILD_DIR := $(abspath $(BUILD_DIR))
ABS_BIN_DIR := $(abspath $(BIN_DIR))

# 模块 .so 文件的输出目录
MODULE_SO_OUTPUT_DIR := $(ABS_BIN_DIR)/modules
# 模块 .o 文件的基础构建目录
MODULE_OBJ_BUILD_DIR_BASE := $(ABS_BUILD_DIR)/module

# --- 全局头文件路径 (示例，请根据你的项目调整) ---
# 这些路径将传递给子 Makefile，子 Makefile 内部的 #include "xxx.h" 可以找到这些位置的文件
PARENT_INCLUDES := -I$(ABS_SRC_DIR)/daemon \
                   -I$(ABS_SRC_DIR)/daemon/utils \
                   -I$(ABS_SRC_DIR)/oconfig

# --- Daemon 和 OConfig 部分 (与之前类似，确保路径正确) ---
DAEMON_SRCS := $(wildcard $(DAEMON_DIR)/*.cpp) $(wildcard $(DAEMON_DIR)/utils/*.c)
DAEMON_OBJS := $(patsubst $(DAEMON_DIR)/%.cpp,$(BUILD_DIR)/daemon/%.o,$(filter %.cpp,$(DAEMON_SRCS))) \
               $(patsubst $(DAEMON_DIR)/%.c,$(BUILD_DIR)/daemon/%.o,$(filter %.c,$(DAEMON_SRCS)))

OCONFIG_SRCS := $(wildcard $(OCONFIG_DIR)/*.cpp)
OCONFIG_OBJS := $(patsubst $(OCONFIG_DIR)/%.cpp,$(BUILD_DIR)/oconfig/%.o,$(OCONFIG_SRCS))

# --- 主程序目标 ---
TARGET_EXE := $(ABS_BIN_DIR)/collect

# ================= TARGETS =================
.PHONY: all clean dirs build_modules copy_share

all: dirs $(TARGET_EXE) build_modules copy_share

dirs:
	@echo "Creating main directories..."
	@mkdir -p $(ABS_BUILD_DIR)/daemon/utils $(ABS_BUILD_DIR)/oconfig
	@mkdir -p $(ABS_BIN_DIR)
	@mkdir -p $(MODULE_SO_OUTPUT_DIR) # 确保模块 .so 输出目录存在
	@mkdir -p $(MODULE_OBJ_BUILD_DIR_BASE) # 确保模块 .o 基础构建目录存在


# --- 编译 Daemon 和 OConfig 的 .o 文件 ---
# 注意：$< 是相对于当前Makefile的路径，$(BUILD_DIR)也是
$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PARENT_INCLUDES) -c $< -o $@

$(BUILD_DIR)/daemon/%.o: $(DAEMON_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(PARENT_INCLUDES) -c $< -o $@

# 如果 utils 内的 .c 文件需要特定规则（通常上面的规则已通过 % 匹配路径覆盖）
# $(BUILD_DIR)/daemon/utils/%.o: $(DAEMON_DIR)/utils/%.c
# 	@mkdir -p $(@D)
# 	$(CC) $(CFLAGS) $(PARENT_INCLUDES) -c $< -o $@

$(BUILD_DIR)/oconfig/%.o: $(OCONFIG_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PARENT_INCLUDES) -c $< -o $@


# --- 链接主程序 ---
$(TARGET_EXE): $(DAEMON_OBJS) $(OCONFIG_OBJS)
	@echo "Linking executable $@"
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS_EXE)


# --- 构建模块 (通过调用子 Makefile) ---
build_modules:
	@echo "--- Building modules via sub-Makefile in [$(MODULE_DIR)] ---"
	$(MAKE) -C $(MODULE_DIR) \
		TOOL_PREFIX="$(TOOL_PREFIX)" \
		CXX="$(CXX)" \
		CC="$(CC)" \
		PARENT_CXXFLAGS="$(CXXFLAGS)" \
		PARENT_CFLAGS="$(CFLAGS)" \
		PARENT_LDFLAGS_FOR_SO="$(LDFLAGS_FOR_SO)" \
		PARENT_INCLUDES_ABS="$(PARENT_INCLUDES)" \
		OUTPUT_SO_DIR_ABS="$(MODULE_SO_OUTPUT_DIR)" \
		MODULE_BUILD_BASE_ABS="$(MODULE_OBJ_BUILD_DIR_BASE)"


# --- 复制 share 目录 ---
copy_share:
	@echo "Copying $(SHARE_DIR_SRC) to $(ABS_BIN_DIR)/"
	@cp -a $(SHARE_DIR_SRC) $(ABS_BIN_DIR)/


# --- 清理 ---
clean:
	@echo "Cleaning main build and bin directories..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "--- Cleaning modules via sub-Makefile ---"
	$(MAKE) -C $(MODULE_DIR) clean \
		OUTPUT_SO_DIR_ABS="$(MODULE_SO_OUTPUT_DIR)" \
		MODULE_BUILD_BASE_ABS="$(MODULE_OBJ_BUILD_DIR_BASE)"

