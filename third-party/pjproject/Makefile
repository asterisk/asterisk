.SUFFIXES:
.PHONY: _all all _install install clean distclean echo_cflags configure

ifeq ($(MAKECMDGOALS),install)
include ../../makeopts
else
-include ../../makeopts
endif

include ../versions.mak
include ../Makefile.rules
include Makefile.rules

ECHO_PREFIX := $(ECHO_PREFIX) echo '[pjproject] '

ifeq ($(MAKECMDGOALS),echo_cflags)
-include build.mak
ECHO_PREFIX=@\#
endif

ifneq ($(PJPROJECT_BUNDLED),yes)
all install:
	@echo '[pjproject] Not enabled'
else

ifneq ($(findstring clean,$(MAKECMDGOALS)),clean)
include build.mak
endif

all: _all
install: _install
endif

ifndef $(TMPDIR)
ifneq ($(wildcard /tmp),)
TMPDIR=/tmp
else
TMPDIR=.
endif
endif

$(TMPDIR)/pjproject-$(PJPROJECT_VERSION).tar.bz2 : ../versions.mak
	$(ECHO_PREFIX) Downloading $@ with $(DOWNLOAD)
	$(CMD_PREFIX) $(DOWNLOAD) $(PJPROJECT_URL)/$(@F) > $@

source/user.mak source/pjlib/include/pj/config_site.h: $(TMPDIR)/pjproject-$(PJPROJECT_VERSION).tar.bz2 patches/config_site.h patches/user.mak 
	$(ECHO_PREFIX) Unpacking $<
	-@rm -rf source &>/dev/null
	-@mkdir source &>/dev/null
	$(CMD_PREFIX) tar --strip-components=1 -C source -xjf $<
	$(ECHO_PREFIX) Applying patches and custom files
	$(CMD_PREFIX) ./apply_patches $(QUIET_CONFIGURE) ./patches ./source
	$(CMD_PREFIX) cp -f ./patches/config_site.h ./source/pjlib/include/pj/
	$(CMD_PREFIX) cp -f ./patches/user.mak ./source/

build.mak: source/pjlib/include/pj/config_site.h source/user.mak Makefile.rules
	$(ECHO_PREFIX) Configuring with $(PJPROJECT_CONFIG_OPTS)
	$(CMD_PREFIX) (cd source ; autoconf aconfigure.ac > aconfigure && ./aconfigure $(QUIET_CONFIGURE) $(PJPROJECT_CONFIG_OPTS))
	@sed -r -e "/prefix|export PJ_SHARED_LIBRARIES|MACHINE_NAME|OS_NAME|HOST_NAME|CC_NAME|CROSS_COMPILE|LINUX_POLL/d" source/build.mak > build.mak

configure: build.mak

echo_cflags: build.mak
	@echo $(PJ_CFLAGS)

source/pjlib/build/.pjlib-$(TARGET_NAME).depend: build.mak
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
	-$(CMD_PREFIX) test -d source && ($(SUBMAKE) -C source clean ; find source -name *.a -delete ; rm -rf source/pjsip-apps/src/python/build) || :
	-$(CMD_PREFIX) rm -rf pjproject.symbols

distclean:
	$(ECHO_PREFIX) Distcleaning
	-$(CMD_PREFIX) rm -rf source pjproject.symbols pjproject-*.tar.bz2 build.mak

