
-include ../makeopts
include Makefile.rules

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

.PHONY: all dist-clean distclean install clean moduleinfo makeopts uninstall $(TP_SUBDIRS)

ifneq ($(TP_SUBDIRS),)

override MAKECMDGOALS?=all

MAKECMDGOALS:=$(subst dist-clean,distclean,$(MAKECMDGOALS))

all distclean dist-clean install uninstall clean : $(TP_SUBDIRS)
install uninstall: $(TP_INSTALL_SUBDIRS)

$(TP_SUBDIRS):
	+$(CMD_PREFIX) $(SUBMAKE) -C $@ $(MAKECMDGOALS)

endif