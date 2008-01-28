LIB=libilbc.a
CFLAGS+=-fPIC

OBJS=anaFilter.o iCBSearch.o packing.o \
     constants.o gainquant.o iLBC_decode.o StateConstructW.o \
     createCB.o getCBvec.o iLBC_encode.o StateSearchW.o doCPLC.o \
     helpfun.o syntFilter.o enhancer.o hpInput.o LPCdecode.o \
     filter.o hpOutput.o LPCencode.o FrameClassify.o iCBConstruct.o lsf.o

include $(ASTTOPDIR)/Makefile.rules

all: $(LIB)

$(LIB): $(OBJS)
	$(ECHO_PREFIX) echo "   [AR] $^ -> $@"
	$(CMD_PREFIX) $(AR) cr $@ $^
	$(CMD_PREFIX) $(RANLIB) $@

clean:
	rm -f $(LIB) *.o .*.d
	rm -f *.s *.i
