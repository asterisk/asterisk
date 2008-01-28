LIB=libg722.a
CFLAGS+=-fPIC

include $(ASTTOPDIR)/Makefile.rules

OBJS=g722_encode.o g722_decode.o

all: $(LIB)

$(LIB): $(OBJS)
	$(ECHO_PREFIX) echo "   [AR] $^ -> $@"
	$(CMD_PREFIX) $(AR) cr $@ $^
	$(CMD_PREFIX) $(RANLIB) $@

clean:
	rm -f $(LIB) *.o
	rm -f .*.o.d
	rm -f *.s *.i
