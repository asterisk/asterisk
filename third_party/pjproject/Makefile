#
# This Makefile can use some work!!
#
PJ_VERSION=2.4.5

CONFIG_OPTS = --prefix= --with-external-speex --with-external-gsm --with-external-srtp \
	--with-external-pa --disable-video --disable-v4l2 --disable-sound --disable-resample \
	--disable-opencore-amr --disable-ilbc-codec --without-libyuv --disable-g7221-codec

.PHONY: all clean distclean compile source configure

all: compile

pjproject-$(PJ_VERSION).tar.bz2 :
	@curl -L -R http://www.pjsip.org/release/$(PJ_VERSION)/pjproject-$(PJ_VERSION).tar.bz2 -o pjproject-$(PJ_VERSION).tar.bz2 -z pjproject-$(PJ_VERSION).tar.bz2 

source/version.mak: pjproject-$(PJ_VERSION).tar.bz2
	@echo Unpacking $<
	-@mkdir source
	@tar --strip-components=1 -C source -xjf pjproject-$(PJ_VERSION).tar.bz2
	@touch source/version.mak
	
source/patched: source/version.mak
	@./apply_patches ./patches ./source

source/Makefile: source/patched 
	@echo Configuring
	@(cd source ; ./configure -q $(CONFIG_OPTS) CFLAGS="-DNDEBUG -fPIC" && echo "Making dependencies" ; make dep > /dev/null)
	@touch source/Makefile

configure: source/Makefile

compile: configure
	@echo Compiling
	@$(MAKE) -C source >/dev/null

clean:
	@$(MAKE) -C source clean

distclean:
	-@rm -rf source
