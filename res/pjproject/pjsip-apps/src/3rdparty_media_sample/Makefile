
include ../../../build.mak


###############################################################################
# Gather all flags.
#
export _CFLAGS 	:= $(PJ_CFLAGS) $(CFLAGS)
export _CXXFLAGS:= $(PJ_CXXFLAGS)
export _LDFLAGS := $(PJ_LDFLAGS) $(PJ_LDLIBS) $(LDFLAGS)

OBJS = alt_pjsua_aud.o alt_pjsua_vid.o pjsua_app.o main.o

all: alt_pjsua

alt_pjsua: $(OBJS)
	$(PJ_CC) -o $@ $(OBJS) $(_CFLAGS) $(_LDFLAGS)

pjsua_app.o: ../pjsua/pjsua_app.c
	$(PJ_CC) $(_CFLAGS) -c -o $@ $<

main.o: ../pjsua/main.c
	$(PJ_CC) $(_CFLAGS) -c -o $@ $<

%.o: %.c
	$(PJ_CC) $(_CFLAGS) -c -o $@ $<

depend:

clean:
	rm -f *.o alt_pjsua 

distclean realclean: clean

