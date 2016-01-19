include ../versions.mak
include ../Makefile.rules

include Makefile.rules

ECHO_PREFIX+= echo -n '[pjproject] ' ;

.PHONY: all clean distclean echo_cflags configure

ifneq ($(MAKECMDGOALS),distclean)
-include source/build.mak
endif

ifeq ($(MAKECMDGOALS),echo_cflags)
ECHO_PREFIX=@\#
endif

all: pjproject.symbols

pjproject-$(PJPROJECT_VERSION).tar.bz2 : ../versions.mak
	$(ECHO_PREFIX) echo Downloading $@
	$(CMD_PREFIX) curl -L -O http://www.pjsip.org/release/$(PJPROJECT_VERSION)/$@ $(REALLY_QUIET)

source/version.mak: pjproject-$(PJPROJECT_VERSION).tar.bz2
	$(ECHO_PREFIX) echo Unpacking $<
	-@mkdir source &>/dev/null
	$(CMD_PREFIX) tar --strip-components=1 -C source -xjf pjproject-$(PJPROJECT_VERSION).tar.bz2
	$(ECHO_PREFIX) echo Applying patches
	$(CMD_PREFIX) ./apply_patches $(QUIET_CONFIGURE) ./patches ./source
	@touch source/version.mak

source/build.mak: source/version.mak Makefile.rules
	$(ECHO_PREFIX) echo Configuring with $(CONFIG_OPTS) CFLAGS="$(PJPROJECT_CFLAGS)"
	$(CMD_PREFIX) (cd source ; ./aconfigure $(QUIET_CONFIGURE) $(CONFIG_OPTS) CFLAGS="$(PJPROJECT_CFLAGS)")
	@sed -r -e "/prefix|export PJ_SHARED_LIBRARIES|MACHINE_NAME|OS_NAME|HOST_NAME|CC_NAME|CROSS_COMPILE|LINUX_POLL/d" source/build.mak > build.mak

configure: source/build.mak

echo_cflags: source/build.mak
	@echo $(PJ_CFLAGS)

source/pjlib/build/.pjlib-x86_64-unknown-linux-gnu.depend: source/build.mak
	$(ECHO_PREFIX) echo "Making dependencies"
	$(CMD_PREFIX) $(SUBMAKE) -C source dep

$(PJ_LIB_FILES): source/pjlib/build/.pjlib-x86_64-unknown-linux-gnu.depend
	$(ECHO_PREFIX) echo Compiling
	$(CMD_PREFIX) $(SUBMAKE) -C source $(REALLY_QUIET)

pjproject.symbols: $(PJ_LIB_FILES)
	$(ECHO_PREFIX) echo Generating symbols
	$(CMD_PREFIX) nm -Pog $(PJ_LIB_FILES) | sed -n -r -e "s/.+: ([pP][jJ][^ ]+) .+/\1/gp" | sort -u > pjproject.symbols

clean:
	$(ECHO_PREFIX) echo Cleaning
	$(CMD_PREFIX) if [ -d source ] ; then $(SUBMAKE) -C source clean ; find source -name *.a -delete ; fi

distclean:
	$(ECHO_PREFIX) echo Distcleaning
	$(CMD_PREFIX) rm -rf source pjproject.symbols
