#
# Makefile for chan_misdn support
#
all:

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

portinfo: portinfo.o
	$(CC) -o $@ $^ -lisdnnet -lmISDN -lpthread

clean: 
	rm -rf *.a *.o *.so portinfo
