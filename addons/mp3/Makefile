MP3OBJS=common.o dct64_i386.o decode_ntom.o layer3.o tabinit.o interface.o

ifeq ($(OSARCH),FreeBSD)
OSVERSION=$(shell make -V OSVERSION -f /usr/share/mk/bsd.port.subdir.mk)
CFLAGS+=$(if $(OSVERSION)<500016,-D_THREAD_SAFE)
LIBS+=$(if $(OSVERSION)<502102,-lc_r,-pthread)
INCLUDE+=-I/usr/local/include
CFLAGS+=$(shell if [ -d /usr/local/include/spandsp ]; then echo "-I/usr/local/include/spandsp"; fi)
endif # FreeBSD

ifeq ($(OSARCH),NetBSD)
CFLAGS+=-pthread
INCLUDE+=-I/usr/local/include
endif

ifeq ($(OSARCH),OpenBSD)
CFLAGS+=-pthread
endif

all: $(MP3OBJS)

clean:
	rm -f *.o *.so *~
	rm -f .*.o.d
