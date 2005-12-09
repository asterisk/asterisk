#
# Makefile
#
# Make file for chan_misdn support
#

# Verify those options with main Makefile
CFLAGS		+= -pipe -c
SOURCES		= isdn_lib.c isdn_msg_parser.c 
OBJDIR		= .
OBJS		= isdn_lib.o isdn_msg_parser.o fac.o


all: chan_misdn_lib.a 


%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	

chan_misdn_lib.a:	$(OBJS)
	ar crv $@ $(OBJS)

misdn:
	@mkdir -p lib
	cd lib ; cvs -d:pserver:anonymous:readonly@cvs.isdn4linux.de:/i4ldev co -r mqueue mISDN mISDNuser ; 
	cd lib/mISDN ; make install
	cd lib/mISDNuser ; make install
	
FORCE:

clean: 
	rm -rf *.a *.o *.so
	rm -rf lib
