.PHONY: all install clean distclean configure

.NOTPARALLEL:

include ../versions.mak
export JANSSON_DIR := $(shell pwd -P)

SPECIAL_TARGETS :=

ifneq ($(findstring configure,$(MAKECMDGOALS)),)
# Run from $(ASTTOPDIR)/configure
    SPECIAL_TARGETS += configure
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

ifeq ($(SPECIAL_TARGETS),)
# Run locally or from $(ASTTOPDIR)/Makefile.  All include files should be present
    ifeq ($(wildcard ../../makeopts),)
        $(error ASTTOPDIR/configure hasn't been run)
    endif

    ifeq ($(JANSSON_BUNDLED),yes)
        ifneq ($(wildcard ../../menuselect.makeopts),)
            include ../../menuselect.makeopts
        else
            $(warning ASTTOPDIR/menuselect hasn't been run yet.  Can't find debug options.)
        endif

        all: dest/include/jansson.h
    else
        all:
    endif
endif

include ../../Makefile.rules
include ../Makefile.rules
include Makefile.rules

ECHO_PREFIX := $(ECHO_PREFIX) echo '[jansson] '
SHELL_ECHO_PREFIX := echo '[jansson] '

dest/include/jansson.h: source/config.status
	$(ECHO_PREFIX) Building bundled jansson.
	$(CMD_PREFIX) (cd source; make $(REALLY_QUIET))
	$(CMD_PREFIX) (cd source; make install DESTDIR= $(REALLY_QUIET))

.DELETE_ON_ERROR:

$(DOWNLOAD_DIR)/$(TARBALL_FILE): ../versions.mak
	$(CMD_PREFIX) ($(TARBALL_EXISTS) && $(TARBALL_VERIFY) && touch $@) || (rm -rf $@ ;\
	$(TARBALL_DOWNLOAD)) || (rm -rf $@ ;\
	$(SHELL_ECHO_PREFIX) Retrying download ; $(TARBALL_DOWNLOAD))

source/.unpacked: $(DOWNLOAD_DIR)/$(TARBALL_FILE)
	$(CMD_PREFIX) $(TARBALL_VERIFY) || (rm -rf $@ ;\
	$(SHELL_ECHO_PREFIX) Retrying download ; $(TARBALL_DOWNLOAD))
	$(ECHO_PREFIX) Unpacking $<
	-@rm -rf source jansson-*/ >/dev/null 2>&1
	$(CMD_PREFIX) $(TAR) -xjf $<
	@mv jansson-$(JANSSON_VERSION) source
	$(ECHO_PREFIX) Applying patches "$(realpath patches)" "$(realpath .)/source"
	$(CMD_PREFIX) ../apply_patches $(QUIET_CONFIGURE) "$(realpath patches)" "$(realpath .)/source"
	-@touch source/.unpacked

.rebuild_needed: $(wildcard ../../.lastclean)
	$(ECHO_PREFIX) Rebuilding
	$(CMD_PREFIX) $(MAKE) clean $(REALLY_QUIET)

source/config.status: source/.unpacked Makefile.rules .rebuild_needed
	$(ECHO_PREFIX) Configuring
	$(CMD_PREFIX) (cd source ; ./configure $(QUIET_CONFIGURE) $(JANSSON_CONFIG_OPTS) --disable-shared \
		--enable-static --prefix=$(JANSSON_DIR)/dest --libdir=$(JANSSON_DIR)/dest/lib)

configure: source/config.status

install:
uninstall:

clean:
	$(ECHO_PREFIX) Cleaning
	+-$(CMD_PREFIX) test -d source dest && $(SUBMAKE) -C source clean || :

distclean:
	$(ECHO_PREFIX) Distcleaning
	-$(CMD_PREFIX) rm -rf source jansson-*.tar.bz2 .rebuild_needed
