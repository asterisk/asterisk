#
# Makefile for chan_misdn support
#
ifneq ($(wildcard /usr/include/linux/mISDNdsp.h),)
CFLAGS+=-DMISDN_1_2
endif

all:

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

portinfo: portinfo.o
	$(CC) -o $@ $^ -lisdnnet -lmISDN -lpthread

clean: 
	rm -rf *.a *.o *.so portinfo *.i
