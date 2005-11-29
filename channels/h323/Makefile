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

CFLAGS += -DNDEBUG -DDO_CRASH -DDEBUG_THREADS
CFLAGS  = -pipe -Wall -fPIC -Wmissing-prototypes -Wmissing-declarations 
CFLAGS += -DP_LINUX  -D_REENTRANT -D_GNU_SOURCE
# This needs to be updated to deal with more than just little endian machines
CFLAGS += -march=$(shell uname -m) -DPBYTE_ORDER=PLITTLE_ENDIAN
CFLAGS += -DP_HAS_SEMAPHORES -DP_SSL -DP_PTHREADS
CFLAGS += -DPHAS_TEMPLATES -DPTRACING -DP_USE_PRAGMA 
CFLAGS += -I$(PWLIBDIR)/include/ptlib/unix -I$(PWLIBDIR)/include 
CFLAGS += -I$(OPENH323DIR)/include -Wno-missing-prototypes -Wno-missing-declarations


all: chan_h323.so

noshared: chan_h323_s.so
optnoshared: chan_h323_s.so
debug:	chan_h323_d.so


install: all
	install -m 755 chan_h323.so $(ASTERISKDIR)
	@echo " +----- chan_h323 Installation Complete ----- +"
	@echo " +                                            +"
	@echo " + chan_h323 has successfully been installed. +"
	@echo " + If you would like to install the sample    +"
	@echo " + configuration file (overwriting existing   +"
	@echo " + config file), run:                         +"
	@echo " +                                            +"
	@echo " +               make samples                 +"
	@echo " +                                            +"
	@echo " +                                            +"
 

samples: 
	if [ -f $(ASTETCDIR)/h323.conf ]; then \
		mv -f $(ASTETCDIR)/h323.conf $(ASTETCDIR)/h323.conf.old ; \
	fi ; 
	install h323.conf.sample $(ASTETCDIR)/h323.conf
 

chan_h323.o:	chan_h323.c
	$(CC) -g -c -o $@ $(CFLAGS) $<
ast_h323.o:	ast_h323.cpp
	g++ -g -c -o $@ $(CFLAGS) $<

chan_h323.so:	chan_h323.o ast_h323.o
	g++  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r -L$(OPENH323DIR)/lib -lh323_linux_x86_r -L/usr/lib -lpthread -ldl -lcrypto -lssl -lexpat

chan_h323_d.so:	chan_h323.o ast_h323.o
	g++  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_d -L$(OPENH323DIR)/lib -lh323_linux_x86_d -L/usr/lib -lpthread -ldl -lcrypto -lssl -lexpat

chan_h323_s.so:	chan_h323.o ast_h323.o
	g++  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r_s -L$(OPENH323DIR)/lib -lh323_linux_x86_r_s -L/usr/lib -lpthread -ldl -lcrypto -lssl -lexpat
clean:
	rm -f *.o *.so core.*


