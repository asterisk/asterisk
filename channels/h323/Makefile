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

PWLIB_MAJOR_VERSION=$(shell cat ${PWLIBDIR}/version.h | grep MAJOR_VERSION | cut -f3 -d' ')
PWLIB_MINOR_VERSION=$(shell cat ${PWLIBDIR}/version.h | grep MINOR_VERSION | cut -f3 -d' ')
PWLIB_BUILD_NUMBER=$(shell cat ${PWLIBDIR}/version.h | grep BUILD_NUMBER | cut -f3 -d' ')
PWLIB_VERSION="${PWLIB_MAJOR_VERSION}.${PWLIB_MINOR_VERSION}.${PWLIB_BUILD_NUMBER}"

OPENH323_MAJOR_VERSION=$(shell cat ${OPENH323DIR}/version.h | grep MAJOR_VERSION | cut -f3 -d' ')
OPENH323_MINOR_VERSION=$(shell cat ${OPENH323DIR}/version.h | grep MINOR_VERSION | cut -f3 -d' ')
OPENH323_BUILD_NUMBER=$(shell cat ${OPENH323DIR}/version.h | grep BUILD_NUMBER | cut -f3 -d' ')
OPENH323_VERSION="${OPENH323_MAJOR_VERSION}.${OPENH323_MINOR_VERSION}.${OPENH323_BUILD_NUMBER}"

#
# This needs to be updated to deal with more than just little endian machines
#
OSARCH=$(shell uname -s)
PROC=$(shell uname -m)
ifneq (${OSARCH},FreeBSD)
ifneq (${OSARCH},NetBSD)
ifneq (${PROC},ppc)
CFLAGS += -march=$(PROC)
endif
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
OSARCH=$(shell uname -s)
CFLAGS += -DNDEBUG -DDO_CRASH -DDEBUG_THREADS
CFLAGS += -pipe -Wall -fPIC -Wmissing-prototypes
CFLAGS += -D_REENTRANT -D_GNU_SOURCE
CFLAGS += -I../../include
CFLAGS += -I$(PWLIBDIR)/include 
CFLAGS += -I$(OPENH323DIR)/include -Wno-missing-prototypes

all:	checkversion depend libchanh323.a

samples: 
	if [ -f $(ASTETCDIR)/h323.conf ]; then \
		mv -f $(ASTETCDIR)/h323.conf $(ASTETCDIR)/h323.conf.old ; \
	fi ; 
	install h323.conf.sample $(ASTETCDIR)/h323.conf
 

libchanh323.a:	ast_h323.o
	ar cr libchanh323.a ast_h323.o
	touch ../chan_h323.c

ast_h323.o:	ast_h323.cpp
	$(CXX) -g -c -o $@ $(CFLAGS) $<

ifneq ($(wildcard .depend),)
include .depend
endif

chan_h323.so:	
	$(CXX)  -g -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r -L$(OPENH323DIR)/lib -lh323_linux_x86_r -L/usr/lib $(CHANH323LIB)

chan_h323_d.so:	chan_h323.o ast_h323.o
	$(CXX)     -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_d -L$(OPENH323DIR)/lib -lh323_linux_x86_d -L/usr/lib $(CHANH323LIB)

chan_h323_s.so:	chan_h323.o ast_h323.o
	$(CXX)  -shared -Xlinker -x -o chan_h323.so chan_h323.o ast_h323.o -L$(PWLIBDIR)/lib  -lpt_linux_x86_r_s -L$(OPENH323DIR)/lib -lh323_linux_x86_r_s -L/usr/lib $(CHANH323LIB)
clean:
	rm -f *.o *.so core.* libchanh323.a .depend

depend: .depend

.depend:
	../../mkdep $(CFLAGS) `ls *.cpp`

checkversion:
	@echo -n "PWLib version is ${PWLIB_VERSION}... "
	@if [ ${PWLIB_VERSION} == "1.8.1" ]; then \
		echo "ok" ; \
	else \
		echo "BAD" ; \
		echo ; \
		echo "Please read README for further details!" ; \
		echo ; \
		exit 1 ; \
	fi
	@echo -n "OpenH323 version is ${OPENH323_VERSION}... "
	@if [ ${OPENH323_VERSION} == "1.15.1" ]; then \
		echo "ok" ; \
	else \
		echo "BAD" ; \
		echo ; \
		echo "Please read README for further details!" ; \
		echo ; \
		exit 1 ; \
	fi
