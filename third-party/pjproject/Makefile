.PHONY: _all all _install install clean distclean echo_cflags configure

.NOTPARALLEL:

include ../versions.mak
export PJDIR := $(shell pwd -P)/source

SPECIAL_TARGETS :=

ifneq ($(findstring configure,$(MAKECMDGOALS))$(findstring echo_cflags,$(MAKECMDGOALS)),)
# Run from $(ASTTOPDIR)/configure
    SPECIAL_TARGETS += configure
endif

ifeq ($(findstring echo_cflags,$(MAKECMDGOALS)),echo_cflags)
    -include build.mak
    ECHO_PREFIX=@\#
endif

ifeq ($(findstring clean,$(MAKECMDGOALS)),clean)
# clean or distclean
    SPECIAL_TARGETS += clean
endif

ifeq ($(findstring uninstall,$(MAKECMDGOALS)),uninstall)
    SPECIAL_TARGETS += uninstall
endif


ifneq ($(wildcard ../../makeopts),)
    include ../../makeopts
endif

TARGETS = build.mak

ifeq ($(SPECIAL_TARGETS),)
# Run locally or from $(ASTTOPDIR)/Makefile.  All include files should be present
    ifeq ($(wildcard ../../makeopts),)
        $(error ASTTOPDIR/configure hasn't been run)
    endif

    ifeq ($(PJPROJECT_BUNDLED),yes)
        ifneq ($(wildcard ../../menuselect.makeopts),)
            include ../../menuselect.makeopts
        else
            $(warning ASTTOPDIR/menuselect hasn't been run yet.  Can't find debug options.)
        endif

        all: _all
        install: _install

        include source/user.mak
        include source/version.mak
        include source/build.mak
        CF := $(filter-out -W%,$(CC_CFLAGS))
        CF := $(filter-out -I%,$(CF))
        ifeq ($(findstring TEST_FRAMEWORK,$(MENUSELECT_CFLAGS)),TEST_FRAMEWORK)
            apps := source/pjsip-apps/bin/pjsua-$(TARGET_NAME) source/pjsip-apps/bin/pjsystest-$(TARGET_NAME)
            TARGETS += $(apps)
            ifneq ($(PYTHONDEV_LIB),)
                TARGETS += source/pjsip-apps/src/python/_pjsua.so
            endif
        endif
        ifeq ($(findstring MALLOC_DEBUG,$(MENUSELECT_CFLAGS)),MALLOC_DEBUG)
            CF += -DMALLOC_DEBUG
            MALLOC_DEBUG_LIBS = source/pjsip-apps/lib/libasterisk_malloc_debug.a
            MALLOC_DEBUG_LDFLAGS = -L$(PJDIR)/pjsip-apps/lib -Wl,-whole-archive -lasterisk_malloc_debug -Wl,-no-whole-archive
        endif
        ifeq ($(findstring DONT_OPTIMIZE,$(MENUSELECT_CFLAGS)),)
            CF += -O3
        endif
        export CFLAGS += $(CF) -g3
        export LDFLAGS += $(CC_LDFLAGS)
        TARGETS += pjproject.symbols
    else
        all install:
    endif
endif

include ../../Makefile.rules
include ../Makefile.rules
include Makefile.rules

ECHO_PREFIX := $(ECHO_PREFIX) echo '[pjproject] '
SHELL_ECHO_PREFIX := echo '[pjproject] '

_all: $(TARGETS)

.DELETE_ON_ERROR:

$(DOWNLOAD_DIR)/$(TARBALL_FILE): ../versions.mak
	$(CMD_PREFIX) ($(TARBALL_EXISTS) && $(TARBALL_VERIFY) && touch $@) || (rm -rf $@ ;\
	$(TARBALL_DOWNLOAD)) || (rm -rf $@ ;\
	$(SHELL_ECHO_PREFIX) Retrying download ; $(TARBALL_DOWNLOAD))

source/.unpacked: $(DOWNLOAD_DIR)/$(TARBALL_FILE)
	$(CMD_PREFIX) $(TARBALL_VERIFY) || (rm -rf $@ ;\
	$(SHELL_ECHO_PREFIX) Retrying download ; $(TARBALL_DOWNLOAD))
	$(ECHO_PREFIX) Unpacking $<
	-@rm -rf source pjproject-*/ >/dev/null 2>&1
	$(CMD_PREFIX) $(TAR) -xjf $<
	@mv pjproject-$(PJPROJECT_VERSION) source
	$(ECHO_PREFIX) Applying patches "$(realpath patches)" "$(realpath .)/source"
	$(CMD_PREFIX) ../apply_patches $(QUIET_CONFIGURE) "$(realpath patches)" "$(realpath .)/source"
	-@touch source/.unpacked

source/version.mak: source/.unpacked

source/user.mak: source/.unpacked patches/user.mak
	$(ECHO_PREFIX) Applying user.mak
	$(CMD_PREFIX) cp -f patches/user.mak source/

source/pjlib/include/pj/%.h: patches/%.h
	$(ECHO_PREFIX) Applying custom include file $<
	$(CMD_PREFIX) cp -f $< source/pjlib/include/pj/

.rebuild_needed: $(wildcard ../../.lastclean)
	$(ECHO_PREFIX) Rebuilding
	$(CMD_PREFIX) $(MAKE) clean $(REALLY_QUIET)

source/build.mak: Makefile.rules source/version.mak source/user.mak $(addprefix source/pjlib/include/pj/,$(notdir $(wildcard patches/*.h))) .rebuild_needed
	$(ECHO_PREFIX) Configuring with $(PJPROJECT_CONFIG_OPTS)
	$(CMD_PREFIX) (cd source ; ./aconfigure $(QUIET_CONFIGURE) $(PJPROJECT_CONFIG_OPTS))

build.mak: source/build.mak
	$(CMD_PREFIX) $(GREP) -v -e prefix -e "export PJ_SHARED_LIBRARIES" -e MACHINE_NAME \
		-e OS_NAME -e HOST_NAME -e CC_NAME -e CROSS_COMPILE -e LINUX_POLL $< > $@

configure: source/build.mak

# We need to filter-out any -O and -g options in PJ_CFLAGS before echoing out
# the result so Asterisk modules don't have the optimization and symbolic debug
# options overridden by the PJPROJECT configure script selected settings.
echo_cflags: source/build.mak
	@echo $(filter-out -O% -g%,$(PJ_CFLAGS))

libpj%.a: source/build.mak
	$(ECHO_PREFIX) Compiling lib $(@F)
	$(CMD_PREFIX) $(MAKE) -C $(dir $(shell dirname $@))/build $(@F) $(REALLY_QUIET)

# pjsua needs resample and g711 to successfully run the testsuite
libresample%.a: source/build.mak
	$(ECHO_PREFIX) Compiling lib $(@F)
	$(CMD_PREFIX) $(MAKE) -C $(dir $(shell dirname $@))/build/resample all $(REALLY_QUIET)

# We need to compile pjlib, then pjlib-util, then the rest
# so we separate them out and create the dependencies
PJLIB_LIB_FILES = $(foreach lib,$(PJ_LIB_FILES),$(if $(findstring libpj-,$(lib)),$(lib),))
PJLIB_UTIL_LIB_FILES = $(foreach lib,$(PJ_LIB_FILES),$(if $(findstring libpjlib-util,$(lib)),$(lib),))
PJSIP_LIB_FILES = $(filter-out $(PJLIB_LIB_FILES) $(PJLIB_UTIL_LIB_FILES) $(APP_THIRD_PARTY_LIB_FILES),$(PJ_LIB_FILES))
ALL_LIB_FILES = $(PJLIB_LIB_FILES) $(PJLIB_UTIL_LIB_FILES) $(PJSIP_LIB_FILES)

$(PJLIB_UTIL_LIB_FILES): $(PJLIB_LIB_FILES)
$(PJSIP_LIB_FILES): $(PJLIB_UTIL_LIB_FILES)

pjproject.symbols: $(ALL_LIB_FILES)
	$(ECHO_PREFIX) Generating symbols
	$(CMD_PREFIX) $(NM) -Pog $(ALL_LIB_FILES) | $(SED) -n -r -e "s/.+: ([pP][jJ][^ ]+) .+/\1/gp" | sort -u > pjproject.symbols

source/pjsip-apps/src/asterisk_malloc_debug.c: patches/asterisk_malloc_debug.c
	$(ECHO_PREFIX) Copying $< to $@
	$(CMD_PREFIX) cp -f $< $@

source/pjsip-apps/lib/asterisk_malloc_debug.o: source/pjsip-apps/src/asterisk_malloc_debug.c .rebuild_needed
	$(ECHO_PREFIX) Compiling asterisk debug malloc stubs
	$(CMD_PREFIX) $(CC) -fPIC  $(PJ_CFLAGS) -c $< -o $@

source/pjsip-apps/lib/libasterisk_malloc_debug.a: source/pjsip-apps/lib/asterisk_malloc_debug.o
	$(ECHO_PREFIX) Creating archive $(@F)
	$(CMD_PREFIX) ar qs $@ $< >/dev/null 2>&1

$(apps): APP = $(filter pj%,$(subst -, ,$(notdir $@)))
$(apps): LDFLAGS += $(MALLOC_DEBUG_LDFLAGS)
$(apps): $(MALLOC_DEBUG_LIBS) pjproject.symbols $(APP_THIRD_PARTY_LIB_FILES)
	$(ECHO_PREFIX) Compiling $(APP)
	$(CMD_PREFIX) +$(MAKE) -C source/pjsip-apps/build $(filter pj%,$(subst -, ,$(notdir $@))) $(REALLY_QUIET)

source/pjsip-apps/src/python/_pjsua.o: source/pjsip-apps/src/python/_pjsua.c $(apps)
	$(ECHO_PREFIX) Compiling python bindings
	$(CMD_PREFIX) $(CC) -o $@ -c $< $(PYTHONDEV_INCLUDE) $(CFLAGS) $(PJ_CFLAGS)

source/pjsip-apps/src/python/_pjsua.so: LDFLAGS += $(MALLOC_DEBUG_LDFLAGS)
source/pjsip-apps/src/python/_pjsua.so: source/pjsip-apps/src/python/_pjsua.o
	$(ECHO_PREFIX) Linking python bindings $(@F)
	$(CMD_PREFIX) gcc -shared -pthread -o $@ $< $(LDFLAGS) $(PJ_LDFLAGS) $(APP_LDLIBS) $(PYTHONDEV_LIB) $(REALLY_QUIET)

_install: _all
	@if [ ! -d "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject" ]; then \
		$(INSTALL) -d "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"; \
	fi;
ifneq ($(findstring source/pjsip-apps/bin/pjsua-$(TARGET_NAME),$(TARGETS)),)
	$(ECHO_PREFIX) Installing apps
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/bin/pjsua-$(TARGET_NAME) "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/pjsua"
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/bin/pjsystest-$(TARGET_NAME) "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/pjsystest"
endif
ifneq ($(findstring _pjsua.so,$(TARGETS)),)
	$(ECHO_PREFIX) Installing python bindings
	$(CMD_PREFIX) $(INSTALL) -m 755 source/pjsip-apps/src/python/_pjsua.so "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/"
	$(CMD_PREFIX) $(INSTALL) -m 644 source/pjsip-apps/src/python/pjsua.py "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject/"
endif

uninstall:
	$(ECHO_PREFIX) Uninstalling apps and python bindings
	$(CMD_PREFIX) rm -rf "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"

clean:
	$(ECHO_PREFIX) Cleaning
	+-$(CMD_PREFIX) test -d source && ($(SUBMAKE) -C source clean || : ;\
		rm -rf source/pjsip-apps/bin/* || : ;\
		find source -name *.a | xargs rm -rf  ;\
		find source -name *.o | xargs rm -rf  ;\
		find source -name *.so  | xargs rm -rf ; ) || :
	-$(CMD_PREFIX) rm -rf pjproject.symbols

distclean:
	$(ECHO_PREFIX) Distcleaning
	-$(CMD_PREFIX) rm -rf source pjproject.symbols pjproject-*.tar.bz2 build.mak .rebuild_needed
