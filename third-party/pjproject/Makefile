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
        all: _all
        install: _install

        ifneq ($(wildcard ../../menuselect.makeopts),)
            include ../../menuselect.makeopts
        else
            $(warning ASTTOPDIR/menuselect hasn't been run yet.  Can't find debug options.)
        endif
		include ../../Makefile.rules
        include ../Makefile.rules
		include Makefile.rules

        -include source/user.mak
        -include source/version.mak
        -include source/build.mak
        CF := $(filter-out -W%,$(CC_CFLAGS))
        CF := $(filter-out -I%,$(CF))
        ifeq ($(AST_DEVMODE),yes)
            apps := source/pjsip-apps/bin/pjsua-$(TARGET_NAME) source/pjsip-apps/bin/pjsystest-$(TARGET_NAME)
            TARGETS += $(apps)
            ifneq ($(PYTHONDEV_LIB),)
                TARGETS += source/pjsip-apps/src/python/_pjsua.so
            endif
            CF += -DPJPROJECT_BUNDLED_ASSERTIONS=yes
        endif
        MALLOC_DEBUG_LIBS = source/pjsip-apps/lib/libasterisk_malloc_debug.a
        ifneq ($(findstring darwin,$(OSARCH)),)
            MALLOC_DEBUG_LDFLAGS = -L$(PJDIR)/pjsip-apps/lib -Wl,-all_load -lasterisk_malloc_debug -Wl,-noall_load
        else
             # These are used for all but Darwin
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
else
	include ../../Makefile.rules
	include ../Makefile.rules
	include Makefile.rules
endif

export PJ_CFLAGS := $(filter-out -O% -g%,$(PJ_CFLAGS))
export CFLAGS += $(CF) $(OPENSSL_INCLUDE)
export LDFLAGS += $(CC_LDFLAGS) $(OPENSSL_LIB)

ECHO_PREFIX := $(ECHO_PREFIX) echo '[pjproject] '
ECHO_PREFIX_NONL := $(ECHO_PREFIX) echo -n '[pjproject] '
SHELL_ECHO_PREFIX := echo '[pjproject] '

_all: $(TARGETS)

$(DOWNLOAD_DIR)/$(TARBALL_FILE): ../versions.mak
	$(CMD_PREFIX) ($(TARBALL_EXISTS) && $(TARBALL_VERIFY) && touch $@) || (rm -rf $@ ;\
	$(TARBALL_DOWNLOAD)) || (rm -rf $@ ;\
	$(SHELL_ECHO_PREFIX) Retrying download ; $(TARBALL_DOWNLOAD))

source/.unpacked: $(DOWNLOAD_DIR)/$(TARBALL_FILE)
	$(CMD_PREFIX) \
		$(TARBALL_VERIFY) || (rm -rf $@ ; $(SHELL_ECHO_PREFIX) Retrying download ; $(TARBALL_DOWNLOAD)) ;\
		$(SHELL_ECHO_PREFIX) Unpacking $< ;\
		rm -rf source pjproject-*/ $(REALLY_QUIET) || : ;\
		$(TAR) -xjf $< ;\
		mv pjproject-$(PJPROJECT_VERSION) source
	$(ECHO_PREFIX) Applying patches "$(realpath patches)" "$(realpath .)/source"
	$(CMD_PREFIX) ../apply_patches $(QUIET_CONFIGURE) "$(realpath patches)" "$(realpath .)/source"
	-@touch source/.unpacked

source/user.mak: $(if $(PJPROJECT_BUNDLED_OOT),,source/.unpacked) patches/user.mak
	$(ECHO_PREFIX) Applying user.mak
	$(CMD_PREFIX) cp -f patches/user.mak source/

source/pjlib/include/pj/%.h: patches/%.h
	$(ECHO_PREFIX) Applying custom include file $<
	$(CMD_PREFIX) cp -f $< source/pjlib/include/pj/

.rebuild_needed: $(wildcard ../../.lastclean)
	$(ECHO_PREFIX) Rebuilding
	$(CMD_PREFIX) $(MAKE) clean $(REALLY_QUIET)

source/build.mak: Makefile.rules source/user.mak $(if $(PJPROJECT_BUNDLED_OOT),,.rebuild_needed) $(subst patches,source/pjlib/include/pj,$(wildcard patches/*.h))
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

# The dependency_utils script needs TARGET_NAME in the environment
export TARGET_NAME

# PJ_LIB_FILES is set by the pjproject build.mak and contains the libs we
# need, but not in the order they need to be built.  We need to compile
# pjlib, then pjlib-util, then the rest so we separate them out and create
# the dependencies.  First though, we shorten all file paths by making them
# relative to the current directory.
SHORTENED_PJ_LIB_FILES = $(subst $(CURDIR)/,,$(PJ_LIB_FILES))
# Now separate them
PJLIB_LIB_FILES = $(filter %/libpj-$(TARGET_NAME).a,$(SHORTENED_PJ_LIB_FILES))
PJLIB_UTIL_LIB_FILES = $(filter %/libpjlib-util-$(TARGET_NAME).a,$(SHORTENED_PJ_LIB_FILES))
RESAMPLE_LIB_FILE = $(filter %/libresample-$(TARGET_NAME).a,$(SHORTENED_PJ_LIB_FILES))
# The rest.
PJSIP_LIB_FILES = $(filter-out $(PJLIB_LIB_FILES) $(PJLIB_UTIL_LIB_FILES) $(RESAMPLE_LIB_FILE),$(SHORTENED_PJ_LIB_FILES))

# Create the dependency order we need
$(PJLIB_UTIL_LIB_FILES): $(PJLIB_LIB_FILES)
$(PJSIP_LIB_FILES): $(PJLIB_UTIL_LIB_FILES)

# and here's the full list
ALL_LIB_FILES = $(PJLIB_LIB_FILES) $(PJLIB_UTIL_LIB_FILES) $(PJSIP_LIB_FILES) $(RESAMPLE_LIB_FILE)

# Assuming that since you're doing an out-of-tree build you're modifying
# the pjproject source files, we need to create dependencies between
# the libraries and their respective source files.  Pjproject does
# create dependency files if you run 'make dep' but those files include
# the system include files and the paths are relative to the lib's
# "build" directory.  Neither really works for us.
# So...
# We create our own "astdep" files with dependency paths relative
# to "this" directory and strip out all the system includes.
# The dependency_utils script does all the heavy lifting.
#
# REMINDER: None of this gets invoked unless you're doing an out-of-tree
# pjproject build.

ifdef PJPROJECT_BUNDLED_OOT
    ALL_PJDEP_FILES = $(shell TARGET_NAME=$(TARGET_NAME) ./dependency_utils getpjdepname $(ALL_LIB_FILES))
    ALL_ASTDEP_FILES = $(ALL_PJDEP_FILES:.depend=.astdep)

    $(ALL_PJDEP_FILES): build.mak
		$(ECHO_PREFIX) Generating pjproject dependency file $(@F)
		$(CMD_PREFIX) $(MAKE) -C $(@D) dep $(REALLY_QUIET)

    $(ALL_ASTDEP_FILES): %.astdep: %.depend
		$(ECHO_PREFIX) Generating asterisk dependency file $(@F)
		$(CMD_PREFIX) ./dependency_utils gendepfile $<
    ifeq ($(SPECIAL_TARGETS),)
        ifneq ($(ALL_ASTDEP_FILES),)
            include $(ALL_ASTDEP_FILES)
            depends: $(ALL_ASTDEP_FILES)
        endif
    endif
endif

# resample's a little odd in that it's build directory is one directory
# level down from the other libraries.  We set the RESAMPLE_OPTS
# variable for it then let the ALL_LIB_FILE build rules take over.
$(RESAMPLE_LIB_FILE): RESAMPLE_OPTS=/resample all

.PRECIOUS: $(ALL_LIB_FILES)

$(ALL_LIB_FILES): BUILD_DIR=$(dir $(@D))
$(ALL_LIB_FILES): source/build.mak source/pjlib/include/pj/config_site.h
	$(ECHO_PREFIX) Compiling $(@F)
	$(CMD_PREFIX) ( $(MAKE) -C $(BUILD_DIR)build$(if $(RESAMPLE_OPTS),$(RESAMPLE_OPTS), $(@F)) >/dev/null ) $(if $(PJPROJECT_BUNDLED_OOT),2>&1 | ($(GREP) -E -v "^(r - output|ar:)" || : ),$(REALLY_QUIET))

pjproject.symbols: $(ALL_LIB_FILES)
	$(ECHO_PREFIX) Generating symbols
	$(CMD_PREFIX) $(NM) -Pog $(ALL_LIB_FILES) | $(SED) -n -E -e "s/.+: ([_]?[pP][jJ][^ ]+) .+/\1/gp" | sort -u > pjproject.symbols

source/pjsip-apps/src/asterisk_malloc_debug.c: patches/asterisk_malloc_debug.c
	$(ECHO_PREFIX) Copying $< to $@
	$(CMD_PREFIX) cp -f $< $@
	-$(CMD_PREFIX) mkdir source/pjsip-apps/lib/ $(REALLY_QUIET)

source/pjsip-apps/lib/asterisk_malloc_debug.o: source/pjsip-apps/src/asterisk_malloc_debug.c | source/pjlib/include/pj/config_site.h source/pjlib/include/pj/asterisk_malloc_debug.h
	$(ECHO_PREFIX) Compiling asterisk debug malloc stubs
	$(CMD_PREFIX) $(CC) -fPIC  $(PJ_CFLAGS) -c $< -o $@

source/pjsip-apps/lib/libasterisk_malloc_debug.a: source/pjsip-apps/lib/asterisk_malloc_debug.o
	$(ECHO_PREFIX) Creating archive $(@F)
	$(CMD_PREFIX) ar qs $@ $< $(REALLY_QUIET)

$(apps): APP = $(filter pj%,$(subst -, ,$(notdir $@)))
$(apps): LDFLAGS += $(MALLOC_DEBUG_LDFLAGS)
$(apps): $(MALLOC_DEBUG_LIBS) pjproject.symbols
	$(ECHO_PREFIX) Compiling $(APP)
	$(CMD_PREFIX) +$(MAKE) -C source/pjsip-apps/build $(APP) $(REALLY_QUIET)
	$(CMD_PREFIX) touch $@

source/pjsip-apps/src/python/_pjsua.o: source/pjsip-apps/src/python/_pjsua.c $(apps)
	$(ECHO_PREFIX) Compiling python bindings
	$(CMD_PREFIX) $(CC) -o $@ -c $< $(PYTHONDEV_INCLUDE) $(CFLAGS) $(PJ_CFLAGS)

source/pjsip-apps/src/python/_pjsua.so: LDFLAGS += $(MALLOC_DEBUG_LDFLAGS)
source/pjsip-apps/src/python/_pjsua.so: source/pjsip-apps/src/python/_pjsua.o
	$(ECHO_PREFIX) Linking python bindings $(@F)
	$(CMD_PREFIX) $(CC) -shared -pthread -o $@ $< $(LDFLAGS) $(PJ_LDFLAGS) $(APP_LDLIBS) $(PYTHONDEV_LIB) $(REALLY_QUIET)

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

all: _all

uninstall:
	$(ECHO_PREFIX) Uninstalling apps and python bindings
	$(CMD_PREFIX) rm -rf "$(DESTDIR)$(ASTDATADIR)/third-party/pjproject"

clean:
	$(ECHO_PREFIX) Cleaning
	+-$(CMD_PREFIX) {\
		if [ -d source ] ; then  \
			$(SUBMAKE) -C source clean ;\
			rm -rf source/pjsip-apps/bin/* ;\
			$(FIND) source/ '(' -name *.a -or -name *.o -or -name *.so ')' -delete ;\
		fi ;\
		rm -rf pjproject.symbols ;\
		} $(REALLY_QUIET) || :

distclean: clean
	$(ECHO_PREFIX) Distcleaning
	+-$(CMD_PREFIX) {\
		rm -rf build.mak .rebuild_needed ;\
		if [ "x$(PJPROJECT_BUNDLED_OOT)" = "x" -a ! -d source/.git ] ; then \
			rm -rf source pjproject-*.tar.bz2 ;\
		else \
			$(SUBMAKE) -C source distclean ;\
			rm -rf source/build.mak source/user.mak ;\
			$(FIND) source/ -name '*asterisk_malloc_debug*' -delete ;\
			$(FIND) source/ '(' -name '.*.depend' -or -name '.*.astdep' ')' -delete ;\
		fi \
		} $(REALLY_QUIET) || :
