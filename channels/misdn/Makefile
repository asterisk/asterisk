#
# Makefile
#
# Make file for chan_misdn support
#

# Verify those options with main Makefile
CFLAGS		= -pipe -c -Wall -ggdb
ifeq ($(shell uname -m),x86_64)
CFLAGS	       += -fPIC
endif
SOURCES		= isdn_lib.c isdn_msg_parser.c 
OBJDIR		= .
OBJS		= isdn_lib.o isdn_msg_parser.o fac.o

all: chan_misdn_lib.a 

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<

chan_misdn_lib.a:	$(OBJS)
	ar crv $@ $(OBJS)


portinfo: portinfo.o
	$(CC) -L/usr/lib -o $@ $^ -lisdnnet -lmISDN -lpthread


clean: 
	rm -rf *.a *.o *.so portinfo
