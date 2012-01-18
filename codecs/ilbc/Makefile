LIB=libilbc.a
CFLAGS+=-fPIC

OBJS=anaFilter.o constants.o createCB.o \
     doCPLC.o enhancer.o filter.o FrameClassify.o \
     gainquant.o getCBvec.o helpfun.o hpInput.o hpOutput.o \
     iCBConstruct.o iCBSearch.o iLBC_decode.o iLBC_encode.o iLBC_test.o \
     LPCdecode.o LPCencode.o lsf.o packing.o StateConstructW.o StateSearchW.o \
     syntFilter.o \

include $(ASTTOPDIR)/Makefile.rules

all: $(LIB)

$(LIB): $(OBJS)
	$(ECHO_PREFIX) echo "   [AR] $^ -> $@"
	$(CMD_PREFIX) $(AR) cr $@ $^
	$(CMD_PREFIX) $(RANLIB) $@

clean:
	rm -f $(LIB) *.o .*.d
	rm -f *.s *.i
