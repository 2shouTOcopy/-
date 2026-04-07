include ../algo_config.mk

TARGET := libethernetip.so

CFLAGS += -I../../../misc/net
CFLAGS += -I../../../utils/util
CFLAGS += -I../../../utils/fifo
CFLAGS += -I../../../iep/inc/ethernetip
CFLAGS += -I../../../misc/isp
CFLAGS += -I../../../misc/trigger
CFLAGS += -I../../../fwk/service
CFLAGS += -I../../../fwk/commproxy
CFLAGS += -I../../../../business_layer/commproxy/include
CFLAGS += -I../../../../business_layer/cfg_capacity/include
CFLAGS += -I../../../app/calibrateproto

CFLAGS += -fPIC -g -O2
CFLAGS += -Wall -Wno-format-truncation -Wno-unused-function -Werror

ifeq ($(PROD), sc7000x)
CFLAGS += -DOPC
endif

ifeq ($(PROD), sc3000)
CFLAGS += -DSC3000
endif

ifeq ($(PROD), sc2000)
CFLAGS += -DSC2000
endif

#log module
CFLAGS += -DLOG_ALGO_MODULE
CFLAGS += -DLOG_M_NAME=\"ethernetip\"

LDFLAGS := -L../../../../libs
LDFLAGS += -L../../../misc/libs
LDFLAGS += -L../../../iep/libs/$(PLAT)/ethernetip
LDFLAGS += -L../../../../package/hicore/vms_so
LDFLAGS += -L../algo

LIBS := -ladapter  -lpthread -lrt -lwrapper -lhiknanomsg -ljt -lmxml -leip -llog -lalgo

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

