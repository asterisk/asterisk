include ../../../build.mak
include ../../../build/common.mak

export LIBDIR := ../../lib

RULES_MAK := $(PJDIR)/build/rules.mak


###############################################################################
# Gather all flags.
#
export _CFLAGS 	:= $(CC_CFLAGS) $(OS_CFLAGS) $(HOST_CFLAGS) $(M_CFLAGS) \
		   $(CFLAGS) $(CC_INC). $(CC_INC)../../resample/include \
		   $(CC_INC)../../../pjlib/include
export _CXXFLAGS:= $(_CFLAGS) $(CC_CXXFLAGS) $(OS_CXXFLAGS) $(M_CXXFLAGS) \
		   $(HOST_CXXFLAGS) $(CXXFLAGS)
export _LDFLAGS := $(CC_LDFLAGS) $(OS_LDFLAGS) $(M_LDFLAGS) $(HOST_LDFLAGS) \
		   $(LDFLAGS) 

export RESAMPLE_SRCDIR = ../../resample/src
export RESAMPLE_OBJS = resamplesubs.o
export RESAMPLE_CFLAGS = $(_CFLAGS)

SHLIB_NAME := libresample.$(SHLIB_SUFFIX)
export RESAMPLE_SHLIB := ../../lib/$(SHLIB_NAME).$(PJ_VERSION_MAJOR)
export RESAMPLE_LIB := ../../lib/libresample-$(TARGET_NAME)$(LIBEXT)

export CC_OUT CC AR RANLIB HOST_MV HOST_RM HOST_RMDIR HOST_MKDIR OBJEXT LD LDOUT 
###############################################################################
# Main entry
#
# $(TARGET) is defined in os-$(OS_NAME).mak file in current directory.
#
ifeq ($(PJ_RESAMPLE_DLL),1)
TARGETS := ../../lib/$(SHLIB_NAME)
ifeq ($(SHLIB_SUFFIX),so)
SHLIB_OPT := -Wl,-soname,$(SHLIB_NAME).$(PJ_VERSION_MAJOR)
else
SHLIB_OPT := 
endif
export RESAMPLE_CFLAGS := -fPIC $(RESAMPLE_CFLAGS)
export RESAMPLE_LDFLAGS := -shared $(SHLIB_OPT) $(RESAMPLE_LDFLAGS)
else
TARGETS := libresample
endif

all: $(TARGETS)

doc:
	cd .. && doxygen docs/doxygen.cfg

dep: depend
distclean: realclean

.PHONY: dep depend libresample clean realclean distclean

libresample:
	$(MAKE) -f $(RULES_MAK) APP=RESAMPLE app=libresample $(RESAMPLE_LIB)

../../lib/$(SHLIB_NAME): $(RESAMPLE_SHLIB)
	ln -s $(SHLIB_NAME).$(PJ_VERSION_MAJOR) $@

$(RESAMPLE_SHLIB):
	$(MAKE) -f $(RULES_MAK) APP=RESAMPLE app=libresample $(RESAMPLE_SHLIB)
	
clean print_lib:
	$(MAKE) -f $(RULES_MAK) APP=RESAMPLE app=libresample $@

realclean:
	$(subst @@,$(subst /,$(HOST_PSEP),../../lib/$(SHLIB_NAME)),$(HOST_RMR))
	$(subst @@,$(subst /,$(HOST_PSEP),$(RESAMPLE_SHLIB)),$(HOST_RMR))
	$(MAKE) -f $(RULES_MAK) APP=RESAMPLE app=libresample $@

depend:
	$(MAKE) -f $(RULES_MAK) APP=RESAMPLE app=libresample $@


