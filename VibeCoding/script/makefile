include ../algo_config.mk

TARGET := libscript.so

CFLAGS += -fPIC -g -O2
CFLAGS += -Wall -Wno-format-truncation -Wno-unused-function -Werror

#log module
CFLAGS += -DLOG_ALGO_MODULE
CFLAGS += -DLOG_M_NAME=\"script\"

LDFLAGS := -L../../../../libs
LDFLAGS += -L../algo
LDFLAGS += -L../../plugins/align/src
LDFLAGS += -L../../plugins/mask/src
LDFLAGS += -L../../../../modules/lua
LIBS := -ladapter  -lalgo -lalign -lmask -llua -llog

all: $(TARGET)
$(TARGET) : $(OBJS)
	@echo $(LIBS) $(LIBS2)
	@$(CPP) -fPIC -shared -o $(TARGET) $(OBJS) $(LDFLAGS) $(LIBS) $(LIBS2) -ldsp_utils

%.o:%.cpp 
	@echo "$(CPP)   : $<"
	@$(CPP) $(CFLAGS) -c $^ -o $@ -pipe
	
%.o:%.c 
	@echo "$(CC)   : $<"
	@$(CC) $(CFLAGS) -c $^ -o $@ -pipe
	
clean:
	rm -rf $(OBJS) ${TARGET}
