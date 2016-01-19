include ../versions.mak
include ../Makefile.rules
include Makefile.rules

ifeq ($(MAKECMDGOALS),install)
include ../../makeopts
else
-include ../../makeopts
endif

ECHO_PREFIX := $(ECHO_PREFIX) echo '[pjproject] '

.PHONY: _all all _install install clean distclean echo_cflags configure

ifeq ($(MAKECMDGOALS),echo_cflags)
ECHO_PREFIX=@\#
endif

ifneq ($(PJPROJECT_BUNDLED),yes)
install all:
	@echo '[pjproject] Not enabled'
else

ifneq ($(MAKECMDGOALS),distclean)
-include source/build.mak
endif

all: _all
install: _install

endif

pjproject-$(PJPROJECT_VERSION).tar.bz2 : ../versions.mak
	$(ECHO_PREFIX) Downloading $@
	$(CMD_PREFIX) curl -L -O http://www.pjsip.org/release/$(PJPROJECT_VERSION)/$@

source/version.mak: pjproject-$(PJPROJECT_VERSION).tar.bz2
	$(ECHO_PREFIX) Unpacking $<
	-@rm -rf source &>/dev/null
	-@mkdir source &>/dev/null
	$(CMD_PREFIX) tar --strip-components=1 -C source -xjf $<
	$(ECHO_PREFIX) Applying patches
	$(CMD_PREFIX) ./apply_patches $(QUIET_CONFIGURE) ./patches ./source
	@touch $@

source/build.mak: source/version.mak Makefile.rules
	$(ECHO_PREFIX) Configuring with $(PJPROJECT_CONFIG_OPTS) CFLAGS="$(PJPROJECT_BUILD_CFLAGS)"
	$(CMD_PREFIX) (cd source ; ./aconfigure $(QUIET_CONFIGURE) $(PJPROJECT_CONFIG_OPTS) CFLAGS="$(PJPROJECT_BUILD_CFLAGS)")
	@sed -r -e "/prefix|export PJ_SHARED_LIBRARIES|MACHINE_NAME|OS_NAME|HOST_NAME|CC_NAME|CROSS_COMPILE|LINUX_POLL/d" source/build.mak > build.mak

configure: source/build.mak

echo_cflags: source/build.mak
	@echo $(PJ_CFLAGS)

source/pjlib/build/.pjlib-$(TARGET_NAME).depend: source/build.mak
	$(ECHO_PREFIX) "Making dependencies"
	+$(CMD_PREFIX) $(SUBMAKE) -C source dep

source/pjlib/lib/libpj-$(TARGET_NAME).a: source/pjlib/build/.pjlib-$(TARGET_NAME).depend
	$(ECHO_PREFIX) Compiling libs
	+$(CMD_PREFIX) $(SUBMAKE) -C source lib $(REALLY_QUIET)

pjproject.symbols: source/pjlib/lib/libpj-$(TARGET_NAME).a
	$(ECHO_PREFIX) Generating symbols
	$(CMD_PREFIX) nm -Pog $(PJ_LIB_FILES) | sed -n -r -e "s/.+: ([pP][jJ][^ ]+) .+/\1/gp" | sort -u > pjproject.symbols

source/pjsip-apps/bin/pjsua-$(TARGET_NAME): source/pjlib/lib/libpj-$(TARGET_NAME).a
	$(ECHO_PREFIX) Compiling apps
	$(CMD_PREFIX) $(SUBMAKE) -C source/pjsip-apps/build pjsua pjsystest $(REALLY_QUIET)

source/pjsip-apps/src/python/build/_pjsua.so: source/pjlib/lib/libpj-$(TARGET_NAME).a
	$(ECHO_PREFIX) Compiling python bindings
	$(CMD_PREFIX) (cd source/pjsip-apps/src/python ; python setup.py build --build-platlib=./build $(REALLY_QUIET))


_all: pjproject.symbols source/pjsip-apps/bin/pjsua-$(TARGET_NAME) source/pjsip-apps/src/python/build/_pjsua.so

_install: _all
	$(ECHO_PREFIX) Installing apps and python bindings
	@if [ ! -d "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject" ]; then \
		$(INSTALL) -d "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"; \
	fi;
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/bin/pjsua-$(TARGET_NAME) "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/pjsua"
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/bin/pjsystest-$(TARGET_NAME) "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/pjsystest"
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/src/python/build/_pjsua.so "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/"
	$(CMD_PREFIX) $(INSTALL) -m 644 source/pjsip-apps/src/python/build/pjsua.py "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/"

uninstall:
	$(ECHO_PREFIX) Uninstalling apps and python bindings
	$(CMD_PREFIX) rm -rf "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"

clean:
	$(ECHO_PREFIX) Cleaning
	-$(CMD_PREFIX) if [ -d source ] ; then $(SUBMAKE) -C source clean ; find source -name *.a -delete ; rm -rf source/pjsip-apps/src/python/build ; fi
	-@rm pjproject.symbols &>/dev/null

distclean:
	$(ECHO_PREFIX) Distcleaning
	$(CMD_PREFIX) rm -rf source pjproject.symbols pjproject-*.tar.bz2 build.mak
