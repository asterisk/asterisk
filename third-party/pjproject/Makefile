.SUFFIXES:
.PHONY: _all all _install install clean distclean echo_cflags configure

include ../versions.mak

SPECIAL_TARGETS :=

ifneq ($(findstring configure,$(MAKECMDGOALS))$(findstring echo_cflags,$(MAKECMDGOALS)),)
# Run from $(ASTTOPDIR)/configure
    SPECIAL_TARGETS += configure
    include ../Makefile.rules
    include Makefile.rules
endif

ifeq ($(findstring echo_cflags,$(MAKECMDGOALS)),echo_cflags)
    -include build.mak
    ECHO_PREFIX=@\#
endif

ifeq ($(findstring clean,$(MAKECMDGOALS)),clean)
# clean or distclean
    SPECIAL_TARGETS += clean
    include ../Makefile.rules
    include Makefile.rules
endif

ifeq ($(SPECIAL_TARGETS),)
# Run locally or from $(ASTTOPDIR)/Makefile.  All include files should be present
    ifeq ($(wildcard ../../makeopts),)
        $(error ASTTOPDIR/configure hasn't been run)
    endif
    include ../../makeopts

    ifeq ($(PJPROJECT_BUNDLED),yes)
        -include ../../menuselect.makeopts
        include ../Makefile.rules

        all: _all
        install: _install

        include ../../Makefile.rules
        include Makefile.rules
        include build.mak
        CF := $(filter-out -W%,$(CC_CFLAGS))
        CF := $(filter-out -I%,$(CF))
        export CFLAGS += $(CF)
        export LDFLAGS += $(CC_LDFLAGS)
        TARGETS := pjproject.symbols
        ifeq ($(findstring TEST_FRAMEWORK,$(MENUSELECT_CFLAGS)),TEST_FRAMEWORK)
            TARGETS += source/pjsip-apps/bin/pjsua-$(TARGET_NAME)
            ifneq ($(PYTHONDEV_LIB),)
                TARGETS += source/pjsip-apps/src/python/build/_pjsua.so
            endif
        endif
    else
        all install:
    endif
endif

ECHO_PREFIX := $(ECHO_PREFIX) echo '[pjproject] '

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

source/.unpacked: $(TMPDIR)/pjproject-$(PJPROJECT_VERSION).tar.bz2
	$(ECHO_PREFIX) Unpacking $<
	-@rm -rf source &>/dev/null
	-@mkdir source &>/dev/null
	$(CMD_PREFIX) tar --strip-components=1 -C source -xjf $<
	$(ECHO_PREFIX) Applying patches
	$(CMD_PREFIX) ./apply_patches $(QUIET_CONFIGURE) ./patches ./source
	-@touch source/.unpacked

source/user.mak: source/.unpacked ./patches/user.mak
	$(ECHO_PREFIX) Applying user.mak
	$(CMD_PREFIX) cp -f ./patches/user.mak ./source/

source/pjlib/include/pj/config_site.h: source/.unpacked ./patches/config_site.h
	$(ECHO_PREFIX) Applying config_site.h
	$(CMD_PREFIX) cp -f ./patches/config_site.h ./source/pjlib/include/pj/

build.mak: source/.unpacked source/pjlib/include/pj/config_site.h source/user.mak Makefile.rules
	$(ECHO_PREFIX) Configuring with $(PJPROJECT_CONFIG_OPTS)
	$(CMD_PREFIX) (cd source ; autoconf aconfigure.ac > aconfigure && ./aconfigure $(QUIET_CONFIGURE) $(PJPROJECT_CONFIG_OPTS))
	@sed -r -e "/prefix|export PJ_SHARED_LIBRARIES|MACHINE_NAME|OS_NAME|HOST_NAME|CC_NAME|CROSS_COMPILE|LINUX_POLL/d" source/build.mak > build.mak

configure: build.mak

echo_cflags: build.mak
	@echo $(PJ_CFLAGS)

source/pjlib/build/.pjlib-$(TARGET_NAME).depend: build.mak
	$(ECHO_PREFIX) "Making dependencies"
	+$(CMD_PREFIX) $(SUBMAKE) -C source dep

menuselect: ../../menuselect.makeopts ../../makeopts
	-$(CMD_PREFIX) test -d source && ($(SUBMAKE) -C source clean ; find source -name *.a -delete ; rm -rf source/pjsip-apps/src/python/build) || :
	-$(CMD_PREFIX) rm -rf pjproject.symbols

source/pjlib/lib/libpj-$(TARGET_NAME).a: menuselect source/pjlib/build/.pjlib-$(TARGET_NAME).depend
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
	$(CMD_PREFIX) (cd source/pjsip-apps/src/python ; MAKE=$(MAKE) python setup.py build --build-platlib=./build $(REALLY_QUIET))

_all: $(TARGETS)

_install: _all
	@if [ ! -d "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject" ]; then \
		$(INSTALL) -d "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"; \
	fi;
ifneq ($(findstring source/pjsip-apps/bin/pjsua-$(TARGET_NAME),$(TARGETS)),)
	$(ECHO_PREFIX) Installing apps
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/bin/pjsua-$(TARGET_NAME) "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/pjsua"
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/bin/pjsystest-$(TARGET_NAME) "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/pjsystest"
endif
ifneq ($(findstring source/pjsip-apps/src/python/build/_pjsua.so,$(TARGETS)),)
	$(ECHO_PREFIX) Installing python bindings
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/src/python/build/_pjsua.so "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/"
	$(CMD_PREFIX) $(INSTALL) -m 644 source/pjsip-apps/src/python/build/pjsua.py "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/"
endif

uninstall:
	$(ECHO_PREFIX) Uninstalling apps and python bindings
	$(CMD_PREFIX) rm -rf "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"

clean:
	$(ECHO_PREFIX) Cleaning
	-$(CMD_PREFIX) test -d source && ($(SUBMAKE) -C source clean ; find source -name *.a -delete ; rm -rf source/pjsip-apps/src/python/build ; rm -rf source/pjsip-apps/bin/* ) || :
	-$(CMD_PREFIX) rm -rf pjproject.symbols

distclean:
	$(ECHO_PREFIX) Distcleaning
	-$(CMD_PREFIX) rm -rf source pjproject.symbols pjproject-*.tar.bz2 build.mak

