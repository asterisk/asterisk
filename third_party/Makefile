
include Makefile.rules

TP_SUBDIRS := pjproject

.PHONY : all dist-clean distclean install clean moduleinfo makeopts uninstall __embed_libs __embed_ldscript __embed_ldflags $(TP_SUBDIRS)

override MAKECMDGOALS?=all

MAKECMDGOALS:=$(subst dist-clean,distclean,$(MAKECMDGOALS))
MAKECMDGOALS:=$(subst tpclean,clean,$(MAKECMDGOALS))

all: $(TP_SUBDIRS)

distclean dist-clean tpclean : $(TP_SUBDIRS)

$(TP_SUBDIRS):
	$(CMD_PREFIX) $(SUBMAKE) -C $@ $(MAKECMDGOALS)

