# 
# Makefile for LPC-10 speech coder library (unix)
# 

# default C compiler
CC?= gcc

# 
# These definitions for CFLAGS and LIB_TARGET_DIR are used when one
# runs make in the lpc10 directory, without environment variables that
# override them.  When make is run in this directory from a makefile
# for an application that uses the LPC10 coder, there are environment
# variables set for CFLAGS and LIB_TARGET_DIR that override these
# definitions.
# 

LIB_TARGET_DIR = .

# 
# -I$(LIB_TARGET_DIR) option needed so that #include "machine.h"
# directives can find the machine.h file.
# 

WARNINGS = -Wall -Wno-comment -Wno-error
CFLAGS += $(OPTIMIZE) -I$(LIB_TARGET_DIR) $(WARNINGS) -fPIC
#CFLAGS+= $(shell if uname -m | grep -q 86; then echo "-mpentium" ; fi)

#fix for PPC processors and ALPHA too
ifneq ($(OSARCH),Darwin)
ifneq ($(findstring BSD,${OSARCH}),BSD)
ifneq ($(PROC),ppc)
ifneq ($(PROC),x86_64)
ifneq ($(PROC),alpha)
	CFLAGS+= -march=$(PROC)
endif
endif
endif
endif
endif

LIB = $(LIB_TARGET_DIR)/liblpc10.a

.PHONY: all clean

all:	$(LIB)

OBJ=	f2clib.o analys.o bsynz.o chanwr.o dcbias.o decode.o \
		deemp.o difmag.o dyptrk.o encode.o energy.o ham84.o \
		hp100.o invert.o irc2pc.o ivfilt.o lpcdec.o lpcenc.o \
		lpcini.o lpfilt.o median.o mload.o onset.o pitsyn.o \
		placea.o placev.o preemp.o prepro.o random.o rcchk.o \
		synths.o tbdm.o voicin.o vparms.o

$(LIB): $(OBJ)
	$(AR) cr $@ $(OBJ)
	ranlib $@

clean:
	-rm -f *.o $(LIB)
