LIB=libilbc.a
CFLAGS+=-fPIC

OBJS=anaFilter.o iCBSearch.o packing.o \
     constants.o gainquant.o iLBC_decode.o StateConstructW.o \
     createCB.o getCBvec.o iLBC_encode.o StateSearchW.o doCPLC.o \
     helpfun.o syntFilter.o enhancer.o hpInput.o LPCdecode.o \
     filter.o hpOutput.o LPCencode.o FrameClassify.o iCBConstruct.o lsf.o

include $(ASTTOPDIR)/Makefile.rules

all: $(LIB)

$(eval $(call ast_make_a_o,$(LIB),$(OBJS)))

clean:
	rm -f $(LIB) *.o
