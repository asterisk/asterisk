#
# This Makefile can use some work!!
#
include ../versions.mak

include makeopts

.PHONY: all clean distclean compile configure

all: compile

pjproject-$(PJPROJECT_VERSION).tar.bz2 : ../versions.mak
	@curl -L -R http://www.pjsip.org/release/$(PJPROJECT_VERSION)/pjproject-$(PJPROJECT_VERSION).tar.bz2 -o pjproject-$(PJPROJECT_VERSION).tar.bz2

source/version.mak: pjproject-$(PJPROJECT_VERSION).tar.bz2
	@echo Unpacking $<
	@-mkdir source &>/dev/null
	@tar --strip-components=1 -C source -xjf pjproject-$(PJPROJECT_VERSION).tar.bz2
	@./apply_patches ./patches ./source
	@touch source/version.mak

configure source/build.mak: source/version.mak makeopts
	@echo Configuring with $(CONFIG_OPTS) CFLAGS="$(PJPROJECT_CFLAGS)"
	@(cd source ; ./aconfigure -q $(CONFIG_OPTS) CFLAGS="$(PJPROJECT_CFLAGS)")
	@echo "Making dependencies"
	@(cd source ; make dep > /dev/null)

compile: source/build.mak
	@echo Compiling
	@$(MAKE) -C source

clean:
	@$(MAKE) -C source clean

distclean:
	-@rm -rf source
