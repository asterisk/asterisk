.PHONY: _all all _install install clean distclean echo_cflags configure

include ../versions.mak

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

ifneq ($(wildcard ../../makeopts),)
    include ../../makeopts
endif

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
            MALLOC_DEBUG = yes
            $(apps): export LDFLAGS += -L$(PJDIR)/pjsip-apps/lib -Wl,-whole-archive -lasterisk_malloc_debug -Wl,-no-whole-archive
            $(apps): source/pjsip-apps/lib/libasterisk_malloc_debug.a
            source/pjsip-apps/src/python/_pjsua.so: LDFLAGS += -L$(PJDIR)/pjsip-apps/lib -Wl,-whole-archive -lasterisk_malloc_debug -Wl,-no-whole-archive
            source/pjsip-apps/src/python/_pjsua.so: source/pjsip-apps/lib/libasterisk_malloc_debug.a
        endif
        TARGETS += pjproject.symbols
        export CFLAGS += $(CF)
        export LDFLAGS += $(CC_LDFLAGS)
    else
        all install:
    endif
endif

include ../../Makefile.rules
include ../Makefile.rules
include Makefile.rules

ECHO_PREFIX := $(ECHO_PREFIX) echo '[pjproject] '

_all: $(TARGETS)

EXTERNALS_CACHE_DIR ?= $(or $(TMPDIR),$(wildcard /tmp),.)

$(EXTERNALS_CACHE_DIR)/pjproject-$(PJPROJECT_VERSION).tar.bz2 : ../versions.mak
	$(ECHO_PREFIX) Downloading $(PJPROJECT_URL)/$(@F) to $@
	$(CMD_PREFIX) $(DOWNLOAD_TO_STDOUT) $(PJPROJECT_URL)/$(@F) > $@

source/.unpacked: $(EXTERNALS_CACHE_DIR)/pjproject-$(PJPROJECT_VERSION).tar.bz2
	$(ECHO_PREFIX) Unpacking $<
	-@rm -rf source >/dev/null 2>&1
	-@mkdir source >/dev/null 2>&1
	$(CMD_PREFIX) $(TAR) --strip-components=1 -C source -xjf $<
	$(ECHO_PREFIX) Applying patches
	$(CMD_PREFIX) ./apply_patches $(QUIET_CONFIGURE) ./patches ./source
	-@touch source/.unpacked

source/user.mak: source/.unpacked ./patches/user.mak
	$(ECHO_PREFIX) Applying user.mak
	$(CMD_PREFIX) cp -f ./patches/user.mak ./source/

source/pjlib/include/pj/%.h : ./patches/%.h
	$(ECHO_PREFIX) Applying custom include file $<
	$(CMD_PREFIX) cp -f $< ./source/pjlib/include/pj/

build.mak: source/.unpacked $(addprefix source/pjlib/include/pj/,$(notdir $(wildcard ./patches/*.h))) source/user.mak Makefile.rules
	$(ECHO_PREFIX) Configuring with $(PJPROJECT_CONFIG_OPTS)
	$(CMD_PREFIX) (cd source ; ./aconfigure $(QUIET_CONFIGURE) $(PJPROJECT_CONFIG_OPTS))
	$(SED) -r -e "/prefix|export PJ_SHARED_LIBRARIES|MACHINE_NAME|OS_NAME|HOST_NAME|CC_NAME|CROSS_COMPILE|LINUX_POLL/d" source/build.mak > build.mak

configure: build.mak

echo_cflags: build.mak
	@echo $(PJ_CFLAGS)

.rebuild_needed: ../../menuselect.makeopts
	$(ECHO_PREFIX) Rebuilding
	$(CMD_PREFIX)$(MAKE) clean $(REALLY_QUIET)
	@touch  .rebuild_needed

libpj%.a:  .rebuild_needed  build.mak
	$(ECHO_PREFIX) Compiling lib $(@F)
	$(CMD_PREFIX)$(MAKE) -C $(dir $(shell dirname $@))/build $(@F) $(REALLY_QUIET)

# We need to compile pjlib, then pjlib-util, then the rest
# so we separate them out and create the dependencies
PJLIB_LIB_FILES = $(foreach lib,$(PJ_LIB_FILES),$(if $(findstring libpj-,$(lib)),$(lib),))
PJLIB_UTIL_LIB_FILES = $(foreach lib,$(PJ_LIB_FILES),$(if $(findstring libpjlib-util,$(lib)),$(lib),))
LIB_FILES = $(filter-out $(PJLIB_LIB_FILES) $(PJLIB_UTIL_LIB_FILES),$(PJ_LIB_FILES))
ALL_LIB_FILES = $(PJLIB_LIB_FILES) $(PJLIB_UTIL_LIB_FILES) $(LIB_FILES)

$(PJLIB_UTIL_LIB_FILES): $(PJLIB_LIB_FILES)
$(LIB_FILES): $(PJLIB_UTIL_LIB_FILES)

pjproject.symbols: $(ALL_LIB_FILES)
	$(ECHO_PREFIX) Generating symbols
	$(CMD_PREFIX) $(NM) -Pog $(PJ_LIB_FILES) | $(SED) -n -r -e "s/.+: ([pP][jJ][^ ]+) .+/\1/gp" | sort -u > pjproject.symbols

source/pjsip-apps/src/asterisk_malloc_debug.c: patches/asterisk_malloc_debug.c
	$(ECHO_PREFIX) Copying $< to $@
	$(CMD_PREFIX) cp -f $< $@

source/pjsip-apps/lib/asterisk_malloc_debug.o: source/pjsip-apps/src/asterisk_malloc_debug.c  .rebuild_needed
	$(ECHO_PREFIX) Compiling asterisk debug malloc stubs
	$(CMD_PREFIX) $(CC) -fPIC  $(PJ_CFLAGS) -c $< -o $@

source/pjsip-apps/lib/libasterisk_malloc_debug.a: source/pjsip-apps/lib/asterisk_malloc_debug.o
	$(ECHO_PREFIX) Creating archive $(@F)
	$(CMD_PREFIX) ar qs $@ $< >/dev/null 2>&1

$(apps): APP = $(filter pj%,$(subst -, ,$(notdir $@)))
$(apps): pjproject.symbols
	$(ECHO_PREFIX) Compiling $(APP)
	$(CMD_PREFIX) +$(MAKE) -C source/pjsip-apps/build $(filter pj%,$(subst -, ,$(notdir $@))) $(REALLY_QUIET)

source/pjsip-apps/src/python/_pjsua.o: source/pjsip-apps/src/python/_pjsua.c $(apps)
	$(ECHO_PREFIX) Compiling python bindings
	$(CMD_PREFIX) $(CC) -o $@ -c $< $(PYTHONDEV_INCLUDE) $(CFLAGS) $(PJ_CFLAGS)

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
		find source -name *.a -delete ;\
		find source -name *.o -delete ;\
		find source -name *.so -delete ; ) || :
	-$(CMD_PREFIX) rm -rf pjproject.symbols

distclean:
	$(ECHO_PREFIX) Distcleaning
	-$(CMD_PREFIX) rm -rf source pjproject.symbols pjproject-*.tar.bz2 build.mak .rebuild_needed
