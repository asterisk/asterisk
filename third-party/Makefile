
-include ../makeopts
include Makefile.rules

TP_ALL_SUBDIRS := jansson pjproject libjwt

TP_SUBDIRS :=
# Sub directories that contain special install/uninstall targets must be explicitly listed
# to prevent accidentally running the package's default install target.
TP_INSTALL_SUBDIRS :=

ifeq ($(PJPROJECT_BUNDLED),yes)
TP_SUBDIRS += pjproject
TP_INSTALL_SUBDIRS += pjproject
endif

ifeq ($(JANSSON_BUNDLED),yes)
TP_SUBDIRS += jansson
TP_INSTALL_SUBDIRS += jansson
endif

ifeq ($(LIBJWT_BUNDLED),yes)
TP_SUBDIRS += libjwt
TP_INSTALL_SUBDIRS += libjwt
libjwt: jansson
export JANSSON_CFLAGS=$(JANSSON_INCLUDE)
export JANSSON_LIBS=$(JANSSON_LIB)
endif

override MAKECMDGOALS?=all
MAKECMDGOALS:=$(subst dist-clean,distclean,$(MAKECMDGOALS))

.PHONY: all dist-clean distclean install clean moduleinfo makeopts uninstall $(TP_ALL_SUBDIRS)

ifeq ($(findstring clean,$(MAKECMDGOALS)),clean)
distclean dist-clean clean : $(TP_ALL_SUBDIRS)
TP_RUN_SUBDIRS := $(TP_ALL_SUBDIRS)
else
all install uninstall : $(TP_SUBDIRS)
install uninstall: $(TP_INSTALL_SUBDIRS)
TP_RUN_SUBDIRS := $(TP_SUBDIRS)
endif

ifneq ($(TP_RUN_SUBDIRS),)

$(TP_RUN_SUBDIRS):
	+$(CMD_PREFIX) $(SUBMAKE) -C $@ $(MAKECMDGOALS)

endif