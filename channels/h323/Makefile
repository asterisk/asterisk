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
CFLAGS += -march=$(shell uname -m) -DPBYTE_ORDER=PLITTLE_ENDIAN


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
CFLAGS += -pipe -Wall -fPIC -Wmissing-prototypes -Wmissing-declarations
ifeq (${OSARCH},Linux)
CFLAGS += -DP_LINUX
endif
CFLAGS += -D_REENTRANT -D_GNU_SOURCE
CFLAGS += -DP_HAS_SEMAPHORES -DP_SSL -DP_PTHREADS
CFLAGS += -DPHAS_TEMPLATES -DPTRACING -DP_USE_PRAGMA
CFLAGS += -I../../include
CFLAGS += -I$(PWLIBDIR)/include/ptlib/unix -I$(PWLIBDIR)/include
CFLAGS += -I$(OPENH323DIR)/include -Wno-missing-prototypes -Wno-missing-declarations

all:   	libchanh323.a

samples: 
	if [ -f $(ASTETCDIR)/h323.conf ]; then \
		mv -f $(ASTETCDIR)/h323.conf $(ASTETCDIR)/h323.conf.old ; \
	fi ; 
	install h323.conf.sample $(ASTETCDIR)/h323.conf
 

ast_h323.o:	ast_h323.cpp
	g++ -g -c -fno-rtti -o $@ $(CFLAGS) $<

libchanh323.a:	ast_h323.o
	ar cr libchanh323.a ast_h323.o

chan_h323.so:	
	g++  -g -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r -L$(OPENH323DIR)/lib -lh323_linux_x86_r -L/usr/lib -lpthread -ldl -lcrypto -lssl -lexpat

chan_h323_d.so:	chan_h323.o ast_h323.o
	g++  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_d -L$(OPENH323DIR)/lib -lh323_linux_x86_d -L/usr/lib -lpthread -ldl -lcrypto -lssl -lexpat

chan_h323_s.so:	chan_h323.o ast_h323.o
	g++  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r_s -L$(OPENH323DIR)/lib -lh323_linux_x86_r_s -L/usr/lib -lpthread -ldl -lcrypto -lssl -lexpat
clean:
	rm -f *.o *.so core.* libchanh323.a


