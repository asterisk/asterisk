#
# Makefile
#
# Make file for chan_misdn support
#

# Verify those options with main Makefile
ifndef LINUX
LINUX=/lib/modules/$(shell uname -r)/build
endif

CFLAGS		+= -pipe -c
SOURCES		= isdn_lib.c isdn_msg_parser.c 
OBJDIR		= .
OBJS		= isdn_lib.o isdn_msg_parser.o

ifndef MISDNUSER
MISDNUSER=/usr/src/install-misdn/mISDNuser
endif

MISDNCFLAGS	+= -I$(MISDNUSER)/include -I$(MISDNUSER)/i4lnet -I$(MISDNUSER)/lib
MISDNCFLAGS	+= -DMISDNUSER_JOLLY -I$(LINUX)/include


all: chan_misdn_lib.a Makefile.ast


%.o: %.c
	$(CC) $(MISDNCFLAGS) $(CFLAGS) -o $@ $<
	

chan_misdn_lib.a:	$(OBJS)
	ar crv $@ $(OBJS)

Makefile.ast:	FORCE
	@echo CFLAGS+=$(MISDNCFLAGS) -Imisdn/ -DCHAN_MISDN_VERSION=\\\"0.1.1\\\" >$@.tmp
	@echo MISDNUSER = $(MISDNUSER) >>$@.tmp
	@if [ -r $@ ] && cmp -s $@ $@.tmp; then rm -f $@.tmp; else mv -f $@.tmp $@; fi


FORCE:


clean: 
	rm *.a *.o Makefile.ast
