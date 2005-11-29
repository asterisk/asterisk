#
# LMC section

CFLAGS+= -I../include -Iinclude -O6 -funroll-loops -finline-functions -Wall -Wno-missing-prototypes -Wno-missing-declarations -g -fPIC
RANLIB=ranlib

# the XING decoder objs and dependencies:
# This is kinda nasty, since there's C, C++, and asm, oh my!
# of course, each needs different compilation methods. grr.
XINGOBJX86 = src/x86gas.o

XINGOBJS = src/cdct.o src/cupl3.o \
  src/hwin.o src/iup.o src/l3init.o \
  src/msis.o src/wavep.o src/csbt.o \
  src/cwinm.o src/icdct.o src/mdct.o \
  src/uph.o src/cup.o src/dec8.o \
  src/isbt.o src/l3dq.o src/mhead.o \
  src/upsf.o src/iwinm.o

LIBMP3=libmp3.a
ARFLAGS=cr

XINGLMCOBJC += $(shell if uname -m | grep -q i.86; then echo src/x86gas.o; fi)

#assembly lang code, if we need it

XINGLMCOBJ = $(XINGOBJS)

all: $(LIBMP3)

$(LIBMP3): $(XINGOBJS)
	$(AR) $(ARFLAGS) $(LIBMP3) $(XINGLMCOBJ)
	$(RANLIB) $(LIBMP3)

clean:
	rm -f $(XINGOBJS)
	rm -f $(LIBMP3)
