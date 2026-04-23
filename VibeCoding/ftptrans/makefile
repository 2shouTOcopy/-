include ../algo_config.mk

TARGET := libftptrans.so

CFLAGS += -I../../../misc/net
CFLAGS += -I../../../utils/util
CFLAGS += -I../../../utils/fifo
CFLAGS += -I../../../misc/isp
CFLAGS += -I../../../misc/trigger
CFLAGS += -I../../../fwk/service
CFLAGS += -I../../plugins/ftp/inc
CFLAGS += -I../../../../open_library_layer/asio/include
CFLAGS += -I../../../../service_layer/socket/include
CFLAGS += -I../../../../service_layer/log_record/include
CFLAGS += -I../../../../business_layer/parameter/include

CFLAGS += -fPIC -g -O2
CFLAGS += -Wall -Wno-format-truncation -Wno-unused-function -Werror

#log module
CFLAGS += -DLOG_ALGO_MODULE
CFLAGS += -DLOG_M_NAME=\"ftptrans\"

LDFLAGS := -L../../../../libs
LDFLAGS += -L../../../misc/libs
LDFLAGS += -L../../plugins/ftp/src
LDFLAGS += -L../algo

LIBS := -ladapter  -lftp -llog -lalgo

all: $(TARGET)
$(TARGET) : $(OBJS)
	@echo $(LIBS) $(LIBS2)
	$(CPP) -fPIC -shared -o $(TARGET) $(OBJS) $(LDFLAGS) $(LIBS) $(LIBS2) -ldsp_utils
	cp $(TARGET) ../../../../output/libs-with-symbols

%.o:%.cpp 
	@echo "$(CPP)   : $<"
	$(CPP) $(CFLAGS) -c $^ -o $@ -pipe
	
%.o:%.c 
	@echo "$(CC)   : $<"
	@$(CC) $(CFLAGS) -c $^ -o $@ -pipe
	
clean:
	rm -rf $(OBJS) ${TARGET}

