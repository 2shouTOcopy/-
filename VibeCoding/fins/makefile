include ../algo_config.mk

TARGET := libfins.so

CFLAGS += -I../../../misc/inc/app_dev_cfg
CFLAGS += -I../../../misc/devcfg
CFLAGS += -I../../../misc/net
CFLAGS += -I../../../utils/util
CFLAGS += -I../../../utils/fifo
CFLAGS += -I../../../iep/inc/fins
CFLAGS += -I../../../misc/isp
CFLAGS += -I../../../misc/trigger
CFLAGS += -I../../../fwk/service
CFLAGS += -I../../../fwk/commproxy
CFLAGS += -I../../../../business_layer/commproxy/include

CFLAGS += -fPIC -g -O2
CFLAGS += -Wall -Wno-format-truncation -Werror

ifeq ($(PROD), sc7000x)
CFLAGS += -DOPC
endif

#log module
CFLAGS += -DLOG_ALGO_MODULE
CFLAGS += -DLOG_M_NAME=\"fins\"

LDFLAGS := -L../../../../libs
LDFLAGS += -L../../../misc/libs
LDFLAGS += -L../../../../package/hicore/vms_so
LDFLAGS += -L../../../iep/libs/$(PLAT)/fins
LDFLAGS += -L../algo

LIBS := -ladapter  -lpthread -lwrapper -lrealfins -llog -lalgo

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

