# include the Makefile of OpenH323 

ifndef OPENH323DIR
OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
PWLIBDIR=$(HOME)/pwlib
endif

ifndef ASTERISKDIR
ASTERISKDIR= /usr/lib/asterisk/modules
endif

ifndef ASTETCDIR
ASTETCDIR=/etc/asterisk
endif

#
# This needs to be updated to deal with more than just little endian machines
#
OSARCH=$(shell uname -s)
ifneq (${OSARCH},FreeBSD)
ifneq (${OSARCH},NetBSD)
CFLAGS += -march=$(shell uname -m)
endif
endif
CFLAGS += -DPBYTE_ORDER=PLITTLE_ENDIAN

ifeq (${OSARCH},Linux)
LDLIBS+=-ldl
endif

#############################################
#
# Only change below if you know WTF your doing
#
#
# Janus release directives, comment below if your brave enough for Janus
#OSARCH=$(shell uname -s)
#CFLAGS += -DNDEBUG -DDO_CRASH -DDEBUG_THREADS
#CFLAGS += -pipe -Wall -fPIC -Wmissing-prototypes -Wmissing-declarations 
#CFLAGS += -D_REENTRANT -D_GNU_SOURCE
#CFLAGS += -I../../include
#CFLAGS += -I$(PWLIBDIR)/include 
#CFLAGS += -I$(OPENH323DIR)/include -Wno-missing-prototypes -Wno-missing-declarations

# Pre Janus release directives
CFLAGS += -DNDEBUG -DDO_CRASH -DDEBUG_THREADS
CFLAGS += -pipe -Wall -fPIC
ifeq (${OSARCH},Linux)
CFLAGS += -DP_LINUX
LIBS+=-lpthread
endif
ifeq ($(findstring BSD,${OSARCH}),BSD)
CFLAGS += -pthread
endif
CFLAGS += -D_REENTRANT -D_GNU_SOURCE
CFLAGS += -DP_HAS_SEMAPHORES -DP_SSL -DP_PTHREADS
CFLAGS += -DPHAS_TEMPLATES -DPTRACING -DP_USE_PRAGMA
CFLAGS += -I../../include
CFLAGS += -I$(PWLIBDIR)/include/ptlib/unix -I$(PWLIBDIR)/include
CFLAGS += -I$(OPENH323DIR)/include
CFLAGS += -Wno-missing-prototypes -Wno-missing-declarations
LIBS+= -lcrypto -lssl -lexpat

all:   	libchanh323.a

samples: 
	if [ -f $(ASTETCDIR)/h323.conf ]; then \
		mv -f $(ASTETCDIR)/h323.conf $(ASTETCDIR)/h323.conf.old ; \
	fi ; 
	install h323.conf.sample $(ASTETCDIR)/h323.conf
 

ast_h323.o:	ast_h323.cpp
	$(CXX) -g -c -fno-rtti -o $@ $(CFLAGS) $<

libchanh323.a:	ast_h323.o
	ar cr libchanh323.a ast_h323.o

chan_h323.so:	
	$(CXX)  -g -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r -L$(OPENH323DIR)/lib -lh323_linux_x86_r -L/usr/lib $(CHANH323LIB)

chan_h323_d.so:	chan_h323.o ast_h323.o
	$(CXX)     -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_d -L$(OPENH323DIR)/lib -lh323_linux_x86_d -L/usr/lib $(CHANH323LIB)

chan_h323_s.so:	chan_h323.o ast_h323.o
	$(CXX)  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r_s -L$(OPENH323DIR)/lib -lh323_linux_x86_r_s -L/usr/lib $(CHANH323LIB)
clean:
	rm -f *.o *.so core.* libchanh323.a


