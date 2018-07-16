
include Makefile.rules

TP_SUBDIRS := pjproject jansson
# Sub directories that contain special install/uninstall targets must be explicitly listed
# to prevent accidentally running the package's default install target.
TP_INSTALL_SUBDIRS := pjproject jansson

.PHONY: all dist-clean distclean install clean moduleinfo makeopts uninstall $(TP_SUBDIRS)

override MAKECMDGOALS?=all

MAKECMDGOALS:=$(subst dist-clean,distclean,$(MAKECMDGOALS))
MAKECMDGOALS:=$(subst tpclean,clean,$(MAKECMDGOALS))

all distclean dist-clean install uninstall tpclean : $(TP_SUBDIRS)
install uninstall: $(TP_INSTALL_SUBDIRS)

$(TP_SUBDIRS):
	+$(CMD_PREFIX) $(SUBMAKE) -C $@ $(MAKECMDGOALS)
