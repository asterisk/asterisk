
include Makefile.rules

TP_SUBDIRS := pjproject
# Sub directories that contain special install/uninstall targets must be explicitly listed
# to prevent accidentally running the package's default install target.
TP_INSTALL_SUBDIRS := pjproject

.PHONY: all dist-clean distclean install clean moduleinfo makeopts uninstall __embed_libs __embed_ldscript __embed_ldflags $(TP_SUBDIRS)

override MAKECMDGOALS?=all

MAKECMDGOALS:=$(subst dist-clean,distclean,$(MAKECMDGOALS))
MAKECMDGOALS:=$(subst tpclean,clean,$(MAKECMDGOALS))

all distclean dist-clean install tpclean : $(TP_SUBDIRS)
install uninstall: $(TP_INSTALL_SUBDIRS)

$(TP_SUBDIRS):
	+$(CMD_PREFIX) $(SUBMAKE) -C $@ $(MAKECMDGOALS)

