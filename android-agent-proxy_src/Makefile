############################################################################
#                                                                          #
# Description : Agent proxy for android                     #
#                                                                          #
# Copyright (C) 2011 Sevencore, Inc.      #
# Author: Joohyun Kyong <joohyun0115@gmail.com>                                          #
# Copyright (c) 2005-2010 Wind River Systems Inc. All rights reserved      #
# Original design by Jason Wessel                                          #
############################################################################

AGENTVER=1.95

ifeq ($(shell uname -s),Linux) 
OSTYPE := linux
endif

ifeq ($(findstring solaris,$(OSTYPE)),solaris)
ARCH := solaris
LDLIBS = -lnsl -lsocket -lrt -lncurses -lpthread -lusb-1.0
endif

ifeq ($(findstring linux,$(OSTYPE)),linux)
ARCH := linux
LDLIBS =-lrt -lncurses -lpthread -lusb-1.0
endif

OBJS = android-agent-proxy.o android-agent-proxy-rs232.o android-agent-proxy-usb.o
SRCS = $(patsubst %.o,%.c,$(OBJS))
OBJS := $(patsubst %.o,$(CROSS_COMPILE)%.o,$(OBJS))
ifneq ($(extpath),)
OBJS := $(patsubst %.o,$(extpath)%.o,$(OBJS))
endif

## Windows options ##
ifeq ($(ARCH),)
CFLAGS = /GZ /Z7 /MTd -DDEBUG -nologo
LINKFLAGS = 
CC = cl -DAGENT_VER=$(AGENTVER)
#Change .o to .obj
OBJS:=$(patsubst %.o,%.obj,$(OBJS))
TLSPATH_INC=-I T:/rome_tls/x86-win/encap/sdk0204/include
## Unix Options ##
else
CFLAGS = -g -Wall -Wno-unused-parameter -D$(ARCH)
CC = $(CROSS_COMPILE)gcc -DAGENT_VER=$(AGENTVER)
AGENTPROXY = $(CROSS_COMPILE)android-agent-proxy
endif

all: $(CROSS_COMPILE)android-agent-proxy

## Build for win32 or unix
ifeq ($(ARCH),)
$(CROSS_COMPILE)android-agent-proxy: $(OBJS)
	$(CC) $(LINKFLAGS) $(CFLAGS) -o $(extpath)$@ $(OBJS) wsock32.lib
else
$(CROSS_COMPILE)android-agent-proxy: $(OBJS)
	$(CC) $(CFLAGS) -o $(extpath)$@ $(OBJS) $(LDLIBS)
endif


distclean: clean
	rm -f $(extpath).depend $(extpath).depend.bak $(extpath)*~ $(extpath)*.bak
clean:
	rm -f $(extpath)$(CROSS_COMPILE)android-agent-proxy $(extpath)android-agent-proxy $(extpath)*.o $(extpath)*.obj $(extpath)*.exp $(extpath)*.exe $(extpath)*.ilk $(extpath)*.pdb *~

$(extpath)$(CROSS_COMPILE)%.o::%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(extpath)%.obj:%.c
	$(CC) $(CFLAGS) -c -Fo$@ $(TLSPATH_INC) $<


