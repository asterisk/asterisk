#
# Asterisk -- A telephony toolkit for Linux.
# 
# Top level Makefile
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

# All Makefiles use the following variables:
#
# LDFLAGS - linker flags (not libraries), used for all links
# LIBS - additional libraries, at top-level for all links,
#      on a single object just for that object
# SOLINK - linker flags used only for creating shared objects (.so files),
#      used for all .so links
#

.EXPORT_ALL_VARIABLES:

include makeopts

#Uncomment this to see all build commands instead of 'quiet' output
#NOISY_BUILD=yes

# Create OPTIONS variable
OPTIONS=

ASTTOPDIR:=$(shell pwd)

# Overwite config files on "make samples"
OVERWRITE=y

# Include debug and macro symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g3

# Staging directory
# Files are copied here temporarily during the install process
# For example, make DESTDIR=/tmp/asterisk woud put things in
# /tmp/asterisk/etc/asterisk
# !!! Watch out, put no spaces or comments after the value !!!
#DESTDIR?=/tmp/asterisk

# Define standard directories for various platforms
# These apply if they are not redefined in asterisk.conf 
ifeq ($(OSARCH),SunOS)
  ASTETCDIR=/var/etc/asterisk
  ASTLIBDIR=/opt/asterisk/lib
  ASTVARLIBDIR=/var/opt/asterisk
  ASTSPOOLDIR=/var/spool/asterisk
  ASTLOGDIR=/var/log/asterisk
  ASTHEADERDIR=/opt/asterisk/include
  ASTBINDIR=/opt/asterisk/bin
  ASTSBINDIR=/opt/asterisk/sbin
  ASTVARRUNDIR=/var/run/asterisk
  ASTMANDIR=/opt/asterisk/man
else
  ASTETCDIR=$(sysconfdir)/asterisk
  ASTLIBDIR=$(libdir)/asterisk
  ASTHEADERDIR=$(includedir)/asterisk
  ASTBINDIR=$(bindir)
  ASTSBINDIR=$(sbindir)
  ASTSPOOLDIR=$(localstatedir)/spool/asterisk
  ASTLOGDIR=$(localstatedir)/log/asterisk
  ASTVARRUNDIR=$(localstatedir)/run
  ASTMANDIR=$(mandir)
ifeq ($(OSARCH),FreeBSD)
  ASTVARLIBDIR=$(prefix)/share/asterisk
else
  ASTVARLIBDIR=$(localstatedir)/lib/asterisk
endif
endif
ASTDATADIR?=$(ASTVARLIBDIR)

# Asterisk.conf is located in ASTETCDIR or by using the -C flag
# when starting Asterisk
ASTCONFPATH=$(ASTETCDIR)/asterisk.conf
MODULES_DIR=$(ASTLIBDIR)/modules
AGI_DIR=$(ASTDATADIR)/agi-bin

# If you use Apache, you may determine by a grep 'DocumentRoot' of your httpd.conf file
HTTP_DOCSDIR=/var/www/html
# Determine by a grep 'ScriptAlias' of your Apache httpd.conf file
HTTP_CGIDIR=/var/www/cgi-bin

ASTCFLAGS=

# Uncomment this to use the older DSP routines
#ASTCFLAGS+=-DOLD_DSP_ROUTINES

# If the file .asterisk.makeopts is present in your home directory, you can
# include all of your favorite menuselect options so that every time you download
# a new version of Asterisk, you don't have to run menuselect to set them. 
# The file /etc/asterisk.makeopts will also be included but can be overridden
# by the file in your home directory.

GLOBAL_MAKEOPTS=$(wildcard /etc/asterisk.makeopts)
USER_MAKEOPTS=$(wildcard ~/.asterisk.makeopts)

MOD_SUBDIR_CFLAGS=-I../include -I../main
OTHER_SUBDIR_CFLAGS=-I../include

ifeq ($(OSARCH),linux-gnu)
  ifeq ($(PROC),x86_64)
    # You must have GCC 3.4 to use k8, otherwise use athlon
    PROC=k8
    #PROC=athlon
  endif

  ifeq ($(PROC),sparc64)
    #The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
    #This works for even old (2.96) versions of gcc and provides a small boost either way.
    #A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn't support it.
    #So we go lowest common available by gcc and go a step down, still a step up from
    #the default as we now have a better instruction set to work with. - Belgarath
    PROC=ultrasparc
    OPTIONS+=$(shell if $(CC) -mtune=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mtune=$(PROC)"; fi)
    OPTIONS+=$(shell if $(CC) -mcpu=v8 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mcpu=v8"; fi)
    OPTIONS+=-fomit-frame-pointer
  endif

  ifeq ($(PROC),arm)
    # The Cirrus logic is the only heavily shipping arm processor with a real floating point unit
    ifeq ($(SUB_PROC),maverick)
      OPTIONS+=-fsigned-char -mcpu=ep9312
    else
      ifeq ($(SUB_PROC),xscale)
        OPTIONS+=-fsigned-char -mcpu=xscale
      else
        OPTIONS+=-fsigned-char 
      endif
    endif
  endif
endif

ID=id

ifeq ($(OSARCH),SunOS)
  M4=/usr/local/bin/m4
  ID=/usr/xpg4/bin/id
endif

ASTCFLAGS+=-pipe -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG)

ifeq ($(AST_DEVMODE),yes)
  ASTCFLAGS+=-Werror -Wunused
endif

ifneq ($(findstring BSD,$(OSARCH)),)
  ASTCFLAGS+=-I/usr/local/include -L/usr/local/lib
endif

ifneq ($(PROC),ultrasparc)
  ASTCFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
endif

ifeq ($(PROC),ppc)
  ASTCFLAGS+=-fsigned-char
endif

ifeq ($(OSARCH),FreeBSD)
  BSDVERSION=$(shell $(MAKE) -V OSVERSION -f /usr/share/mk/bsd.port.subdir.mk)
  ASTCFLAGS+=$(shell if test $(BSDVERSION) -lt 500016 ; then echo "-D_THREAD_SAFE"; fi)
  AST_LIBS+=$(shell if test  $(BSDVERSION) -lt 502102 ; then echo "-lc_r"; else echo "-pthread"; fi)
endif

ifeq ($(OSARCH),NetBSD)
  ASTCFLAGS+=-pthread -I/usr/pkg/include
endif

ifeq ($(OSARCH),OpenBSD)
  ASTCFLAGS+=-pthread
endif

ifeq ($(OSARCH),SunOS)
  ASTCFLAGS+=-Wcast-align -DSOLARIS -Iinclude/solaris-compat -I/opt/ssl/include -I/usr/local/ssl/include
endif

ASTERISKVERSION:=$(shell build_tools/make_version .)

ifneq ($(wildcard .version),)
  ASTERISKVERSIONNUM:=$(shell awk -F. '{printf "%02d%02d%02d", $$1, $$2, $$3}' .version)
  RPMVERSION:=$(shell sed 's/[-\/:]/_/g' .version)
else
  RPMVERSION=unknown
endif

ifneq ($(wildcard .svn),)
  ASTERISKVERSIONNUM=999999
endif

ASTCFLAGS+=$(MALLOC_DEBUG)$(BUSYDETECT)$(OPTIONS)

MOD_SUBDIRS:=res channels pbx apps codecs formats cdr funcs main
OTHER_SUBDIRS:=utils agi
SUBDIRS:=$(OTHER_SUBDIRS) $(MOD_SUBDIRS)
SUBDIRS_INSTALL:=$(SUBDIRS:%=%-install)
SUBDIRS_CLEAN:=$(SUBDIRS:%=%-clean)
SUBDIRS_CLEAN_DEPEND:=$(SUBDIRS:%=%-clean-depend)
MOD_SUBDIRS_DEPEND:=$(MOD_SUBDIRS:%=%-depend)
OTHER_SUBDIRS_DEPEND:=$(OTHER_SUBDIRS:%=%-depend)
SUBDIRS_DEPEND:=$(OTHER_SUBDIRS_DEPEND) $(MOD_SUBDIRS_DEPEND)
SUBDIRS_UNINSTALL:=$(SUBDIRS:%=%-uninstall)
MOD_SUBDIRS_EMBED_LDSCRIPT:=$(MOD_SUBDIRS:%=%-embed-ldscript)
MOD_SUBDIRS_EMBED_LDFLAGS:=$(MOD_SUBDIRS:%=%-embed-ldflags)
MOD_SUBDIRS_EMBED_LIBS:=$(MOD_SUBDIRS:%=%-embed-libs)

ifneq ($(findstring darwin,$(OSARCH)),)
  ASTCFLAGS+=-D__Darwin__
  AUDIO_LIBS=-framework CoreAudio
  SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace
else
# These are used for all but Darwin
  SOLINK=-shared -Xlinker -x
  ifneq ($(findstring BSD,$(OSARCH)),)
    LDFLAGS+=-L/usr/local/lib
  endif
endif

ifeq ($(OSARCH),SunOS)
  SOLINK=-shared -fpic -L/usr/local/ssl/lib
endif

# This is used when generating the doxygen documentation
ifneq ($(DOT),:)
  HAVEDOT=yes
else
  HAVEDOT=no
endif

all: cleantest $(SUBDIRS)
	@echo " +--------- Asterisk Build Complete ---------+"  
	@echo " + Asterisk has successfully been built, but +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               make install                +"  
	@echo " +-------------------------------------------+"  

makeopts: configure
	@echo "****"
	@echo "**** The configure script must be executed before running 'make'."
	@echo "****"
	@exit 1

menuselect.makeopts: menuselect/menuselect menuselect-tree
	menuselect/menuselect --check-deps $(GLOBAL_MAKEOPTS) $(USER_MAKEOPTS) menuselect.makeopts

$(MOD_SUBDIRS_EMBED_LDSCRIPT):
	@echo "EMBED_LDSCRIPTS+="`$(MAKE) --quiet --no-print-directory -C $(@:-embed-ldscript=) SUBDIR=$(@:-embed-ldscript=) __embed_ldscript` >> makeopts.embed_rules

$(MOD_SUBDIRS_EMBED_LDFLAGS):
	@echo "EMBED_LDFLAGS+="`$(MAKE) --quiet --no-print-directory -C $(@:-embed-ldflags=) SUBDIR=$(@:-embed-ldflags=) __embed_ldflags` >> makeopts.embed_rules

$(MOD_SUBDIRS_EMBED_LIBS):
	@echo "EMBED_LIBS+="`$(MAKE) --quiet --no-print-directory -C $(@:-embed-libs=) SUBDIR=$(@:-embed-libs=) __embed_libs` >> makeopts.embed_rules

makeopts.embed_rules: menuselect.makeopts
	@echo "Generating embedded module rules ..."
	@rm -f $@
	@$(MAKE) --no-print-directory $(MOD_SUBDIRS_EMBED_LDSCRIPT)
	@$(MAKE) --no-print-directory $(MOD_SUBDIRS_EMBED_LDFLAGS)
	@$(MAKE) --no-print-directory $(MOD_SUBDIRS_EMBED_LIBS)

$(SUBDIRS): depend makeopts.embed_rules

# ensure that all module subdirectories are processed before 'main' during
# a parallel build, since if there are modules selected to be embedded the
# directories containing them must be completed before the main Asterisk
# binary can be built
main: $(filter-out main,$(MOD_SUBDIRS))

$(MOD_SUBDIRS):
	@CFLAGS="$(MOD_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) --no-print-directory -C $@ SUBDIR=$@ all

$(OTHER_SUBDIRS):
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) --no-print-directory -C $@ SUBDIR=$@ all

defaults.h: makeopts
	@build_tools/make_defaults_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

include/asterisk/version.h:
	@build_tools/make_version_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

include/asterisk/buildopts.h: menuselect.makeopts
	@build_tools/make_buildopts_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

$(SUBDIRS_CLEAN_DEPEND):
	@$(MAKE) --no-print-directory -C $(@:-clean-depend=) clean-depend

$(SUBDIRS_CLEAN):
	@$(MAKE) --no-print-directory -C $(@:-clean=) clean

clean-depend: $(SUBDIRS_CLEAN_DEPEND)

clean: $(SUBDIRS_CLEAN) clean-depend
	rm -f defaults.h
	rm -f include/asterisk/build.h
	rm -f include/asterisk/version.h
	rm -f .depend
	@$(MAKE) -C menuselect clean

dist-clean: distclean

distclean: clean
	@$(MAKE) -C mxml clean
	@$(MAKE) -C menuselect dist-clean
	@$(MAKE) -C sounds dist-clean
	rm -f menuselect.makeopts makeopts makeopts.xml menuselect.makedeps
	rm -f makeopts.embed_rules
	rm -f config.log config.status
	rm -rf autom4te.cache
	rm -f include/asterisk/autoconfig.h
	rm -f include/asterisk/buildopts.h
	rm -rf doc/api
	rm -f build_tools/menuselect-deps

datafiles: all
	if [ x`$(ID) -un` = xroot ]; then sh build_tools/mkpkgconfig $(DESTDIR)/usr/lib/pkgconfig; fi
# Should static HTTP be installed during make samples or even with its own target ala
# webvoicemail?  There are portions here that *could* be customized but might also be
# improved a lot.  I'll put it here for now.
	mkdir -p $(DESTDIR)$(ASTDATADIR)/static-http
	for x in static-http/*; do \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTDATADIR)/static-http ; \
	done
	mkdir -p $(DESTDIR)$(ASTDATADIR)/images
	for x in images/*.jpg; do \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTDATADIR)/images ; \
	done
	mkdir -p $(DESTDIR)$(AGI_DIR)
	$(MAKE) -C sounds install

update: 
	@if [ -d .svn ]; then \
		echo "Updating from Subversion..." ; \
		svn update | tee update.out; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -b4- ; \
		fi ; \
		rm -f update.out; \
		$(MAKE) clean-depend; \
	else \
		echo "Not under version control";  \
	fi

NEWHEADERS=$(notdir $(wildcard include/asterisk/*.h))
OLDHEADERS=$(filter-out $(NEWHEADERS),$(notdir $(wildcard $(DESTDIR)$(ASTHEADERDIR)/*.h)))

bininstall: all
	mkdir -p $(DESTDIR)$(MODULES_DIR)
	mkdir -p $(DESTDIR)$(ASTSBINDIR)
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	mkdir -p $(DESTDIR)$(ASTBINDIR)
	mkdir -p $(DESTDIR)$(ASTVARRUNDIR)
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/dictate
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/system
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/tmp
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/meetme
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/monitor
	$(INSTALL) -m 755 main/asterisk $(DESTDIR)$(ASTSBINDIR)/
	$(LN) -sf asterisk $(DESTDIR)$(ASTSBINDIR)/rasterisk
	$(INSTALL) -m 755 contrib/scripts/astgenkey $(DESTDIR)$(ASTSBINDIR)/
	$(INSTALL) -m 755 contrib/scripts/autosupport $(DESTDIR)$(ASTSBINDIR)/
	if [ ! -f $(DESTDIR)$(ASTSBINDIR)/safe_asterisk ]; then \
		cat contrib/scripts/safe_asterisk | sed 's|__ASTERISK_SBIN_DIR__|$(ASTSBINDIR)|;' > $(DESTDIR)$(ASTSBINDIR)/safe_asterisk ;\
		chmod 755 $(DESTDIR)$(ASTSBINDIR)/safe_asterisk;\
	fi
	$(INSTALL) -d $(DESTDIR)$(ASTHEADERDIR)
	$(INSTALL) -m 644 include/asterisk.h $(DESTDIR)$(includedir)
	$(INSTALL) -m 644 include/asterisk/*.h $(DESTDIR)$(ASTHEADERDIR)
	if [ -n "$(OLDHEADERS)" ]; then \
		rm -f $(addprefix $(DESTDIR)$(ASTHEADERDIR)/,$(OLDHEADERS)) ;\
	fi
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-csv
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-custom
	mkdir -p $(DESTDIR)$(ASTDATADIR)/keys
	mkdir -p $(DESTDIR)$(ASTDATADIR)/firmware
	mkdir -p $(DESTDIR)$(ASTDATADIR)/firmware/iax
	mkdir -p $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 keys/iaxtel.pub $(DESTDIR)$(ASTDATADIR)/keys
	$(INSTALL) -m 644 keys/freeworlddialup.pub $(DESTDIR)$(ASTDATADIR)/keys
	$(INSTALL) -m 644 doc/asterisk.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/scripts/astgenkey.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/scripts/autosupport.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/scripts/safe_asterisk.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/firmware/iax/iaxy.bin $(DESTDIR)$(ASTDATADIR)/firmware/iax/iaxy.bin; \

$(SUBDIRS_INSTALL):
	@$(MAKE) -C $(@:-install=) install

NEWMODS=$(notdir $(wildcard */*.so))
OLDMODS=$(filter-out $(NEWMODS),$(notdir $(wildcard $(DESTDIR)$(MODULES_DIR)/*.so)))

oldmodcheck:
	@if [ -n "$(OLDMODS)" ]; then \
		echo " WARNING WARNING WARNING" ;\
		echo "" ;\
		echo " Your Asterisk modules directory, located at" ;\
		echo " $(DESTDIR)$(MODULES_DIR)" ;\
		echo " contains modules that were not installed by this " ;\
		echo " version of Asterisk. Please ensure that these" ;\
		echo " modules are compatible with this version before" ;\
		echo " attempting to run Asterisk." ;\
		echo "" ;\
		for f in $(OLDMODS); do \
			echo "    $$f" ;\
		done ;\
		echo "" ;\
		echo " WARNING WARNING WARNING" ;\
	fi

install: all datafiles bininstall $(SUBDIRS_INSTALL)
	@if [ -x /usr/sbin/asterisk-post-install ]; then \
		/usr/sbin/asterisk-post-install $(DESTDIR) . ; \
	fi
	@echo " +---- Asterisk Installation Complete -------+"  
	@echo " +                                           +"
	@echo " +    YOU MUST READ THE SECURITY DOCUMENT    +"
	@echo " +                                           +"
	@echo " + Asterisk has successfully been installed. +"  
	@echo " + If you would like to install the sample   +"  
	@echo " + configuration files (overwriting any      +"
	@echo " + existing config files), run:              +"  
	@echo " +                                           +"
	@echo " +               $(MAKE) samples                +"
	@echo " +                                           +"
	@echo " +-----------------  or ---------------------+"
	@echo " +                                           +"
	@echo " + You can go ahead and install the asterisk +"
	@echo " + program documentation now or later run:   +"
	@echo " +                                           +"
	@echo " +              $(MAKE) progdocs                +"
	@echo " +                                           +"
	@echo " + **Note** This requires that you have      +"
	@echo " + doxygen installed on your local system    +"
	@echo " +-------------------------------------------+"
	@$(MAKE) -s oldmodcheck

upgrade: all bininstall

adsi:
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.adsi; do \
		if [ ! -f $(DESTDIR)$(ASTETCDIR)/$$x ]; then \
			$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x` ; \
		fi ; \
	done

samples: adsi
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.sample; do \
		if [ -f $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` ]; then \
			if [ "$(OVERWRITE)" = "y" ]; then \
				if cmp -s $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` $$x ; then \
					echo "Config file $$x is unchanged"; \
					continue; \
				fi ; \
				mv -f $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample`.old ; \
			else \
				echo "Skipping config file $$x"; \
				continue; \
			fi ;\
		fi ; \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` ;\
	done
	if [ "$(OVERWRITE)" = "y" ] || [ ! -f $(DESTDIR)$(ASTCONFPATH) ]; then \
		( \
		echo "[directories]" ; \
		echo "astetcdir => $(ASTETCDIR)" ; \
		echo "astmoddir => $(MODULES_DIR)" ; \
		echo "astvarlibdir => $(ASTVARLIBDIR)" ; \
		echo "astdatadir => $(ASTDATADIR)" ; \
		echo "astagidir => $(AGI_DIR)" ; \
		echo "astspooldir => $(ASTSPOOLDIR)" ; \
		echo "astrundir => $(ASTVARRUNDIR)" ; \
		echo "astlogdir => $(ASTLOGDIR)" ; \
		echo "" ; \
		echo "; Changing the following lines may compromise your security." ; \
		echo ";[files]" ; \
		echo ";astctlpermissions = 0660" ; \
		echo ";astctlowner = root" ; \
		echo ";astctlgroup = apache" ; \
		echo ";astctl = asterisk.ctl" ; \
		echo ";[options]" ; \
		echo ";internal_timing = yes" ; \
		) > $(DESTDIR)$(ASTCONFPATH) ; \
	else \
		echo "Skipping asterisk.conf creation"; \
	fi
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/INBOX
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat $(DESTDIR)$(ASTDATADIR)/sounds/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm ; \
	done
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat $(DESTDIR)$(ASTDATADIR)/sounds/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm ; \
	done

webvmail:
	@[ -d $(DESTDIR)$(HTTP_DOCSDIR)/ ] || ( printf "http docs directory not found.\nUpdate assignment of variable HTTP_DOCSDIR in Makefile!\n" && exit 1 )
	@[ -d $(DESTDIR)$(HTTP_CGIDIR) ] || ( printf "cgi-bin directory not found.\nUpdate assignment of variable HTTP_CGIDIR in Makefile!\n" && exit 1 )
	$(INSTALL) -m 4755 -o root -g root contrib/scripts/vmail.cgi $(DESTDIR)$(HTTP_CGIDIR)/vmail.cgi
	mkdir -p $(DESTDIR)$(HTTP_DOCSDIR)/_asterisk
	for x in images/*.gif; do \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(HTTP_DOCSDIR)/_asterisk/; \
	done
	@echo " +--------- Asterisk Web Voicemail ----------+"  
	@echo " +                                           +"
	@echo " + Asterisk Web Voicemail is installed in    +"
	@echo " + your cgi-bin directory:                   +"
	@echo " + $(DESTDIR)$(HTTP_CGIDIR)"
	@echo " + IT USES A SETUID ROOT PERL SCRIPT, SO     +"
	@echo " + IF YOU DON'T LIKE THAT, UNINSTALL IT!     +"
	@echo " +                                           +"
	@echo " + Other static items have been stored in:   +"
	@echo " + $(DESTDIR)$(HTTP_DOCSDIR)"
	@echo " +                                           +"
	@echo " + If these paths do not match your httpd    +"
	@echo " + installation, correct the definitions     +"
	@echo " + in your Makefile of HTTP_CGIDIR and       +"
	@echo " + HTTP_DOCSDIR                              +"
	@echo " +                                           +"
	@echo " +-------------------------------------------+"  

spec: 
	sed "s/^Version:.*/Version: $(RPMVERSION)/g" redhat/asterisk.spec > asterisk.spec ; \

rpm: __rpm

__rpm: include/asterisk/version.h include/asterisk/buildopts.h spec
	rm -rf /tmp/asterisk ; \
	mkdir -p /tmp/asterisk/redhat/RPMS/i386 ; \
	$(MAKE) DESTDIR=/tmp/asterisk install ; \
	$(MAKE) DESTDIR=/tmp/asterisk samples ; \
	mkdir -p /tmp/asterisk/etc/rc.d/init.d ; \
	cp -f contrib/init.d/rc.redhat.asterisk /tmp/asterisk/etc/rc.d/init.d/asterisk ; \
	rpmbuild --rcfile /usr/lib/rpm/rpmrc:redhat/rpmrc -bb asterisk.spec

progdocs:
	(cat contrib/asterisk-ng-doxygen; echo "HAVE_DOT=$(HAVEDOT)"; \
	echo "PROJECT_NUMBER=$(ASTERISKVERSION)") | doxygen - 

config:
	@if [ "${OSARCH}" = "linux-gnu" ]; then \
		if [ -f /etc/redhat-release -o -f /etc/fedora-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.redhat.asterisk /etc/rc.d/init.d/asterisk; \
			/sbin/chkconfig --add asterisk; \
		elif [ -f /etc/debian_version ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.debian.asterisk /etc/init.d/asterisk; \
			/usr/sbin/update-rc.d asterisk start 10 2 3 4 5 . stop 91 2 3 4 5 .; \
		elif [ -f /etc/gentoo-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.gentoo.asterisk /etc/init.d/asterisk; \
			/sbin/rc-update add asterisk default; \
		elif [ -f /etc/mandrake-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.mandrake.asterisk /etc/rc.d/init.d/asterisk; \
			/sbin/chkconfig --add asterisk; \
		elif [ -f /etc/SuSE-release -o -f /etc/novell-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.suse.asterisk /etc/init.d/asterisk; \
			/sbin/chkconfig --add asterisk; \
		elif [ -f /etc/slackware-version ]; then \
			echo "Slackware is not currently supported, although an init script does exist for it." \
		else \
			echo "We could not install init scripts for your distribution."; \
		fi \
	else \
		echo "We could not install init scripts for your operating system."; \
	fi

$(MOD_SUBDIRS_DEPEND):
	@CFLAGS="$(MOD_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) --no-print-directory -C $(@:-depend=) depend

$(OTHER_SUBDIRS_DEPEND):
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) --no-print-directory -C $(@:-depend=) depend

depend: include/asterisk/version.h include/asterisk/buildopts.h defaults.h $(SUBDIRS_DEPEND)

sounds:
	$(MAKE) -C sounds all

# If the cleancount has been changed, force a make clean.
# .cleancount is the global clean count, and .lastclean is the 
# last clean count we had

cleantest:
	@if ! cmp -s .cleancount .lastclean ; then \
		$(MAKE) clean; cp -f .cleancount .lastclean;\
		$(MAKE) defaults.h;\
	fi

$(SUBDIRS_UNINSTALL):
	@$(MAKE) --no-print-directory -C $(@:-uninstall=) uninstall

_uninstall: $(SUBDIRS_UNINSTALL)
	rm -f $(DESTDIR)$(MODULES_DIR)/*
	rm -f $(DESTDIR)$(ASTSBINDIR)/*asterisk*
	rm -f $(DESTDIR)$(ASTSBINDIR)/astgenkey
	rm -f $(DESTDIR)$(ASTSBINDIR)/autosupport
	rm -rf $(DESTDIR)$(ASTHEADERDIR)
	rm -rf $(DESTDIR)$(ASTDATADIR)/firmware
	rm -rf $(DESTDIR)$(ASTMANDIR)/man8
	$(MAKE) -C sounds uninstall

uninstall: _uninstall
	@echo " +--------- Asterisk Uninstall Complete -----+"  
	@echo " + Asterisk binaries, sounds, man pages,     +"  
	@echo " + headers, modules, and firmware builds,    +"  
	@echo " + have all been uninstalled.                +"  
	@echo " +                                           +"
	@echo " + To remove ALL traces of Asterisk,         +"
	@echo " + including configuration, spool            +"
	@echo " + directories, and logs, run the following  +"
	@echo " + command:                                  +"
	@echo " +                                           +"
	@echo " +            $(MAKE) uninstall-all             +"  
	@echo " +-------------------------------------------+"  

uninstall-all: _uninstall
	rm -rf $(DESTDIR)$(ASTLIBDIR)
	rm -rf $(DESTDIR)$(ASTVARLIBDIR)
	rm -rf $(DESTDIR)$(ASTDATADIR)
	rm -rf $(DESTDIR)$(ASTSPOOLDIR)
	rm -rf $(DESTDIR)$(ASTETCDIR)
	rm -rf $(DESTDIR)$(ASTLOGDIR)

menuselect: menuselect/menuselect menuselect-tree
	-@menuselect/menuselect $(GLOBAL_MAKEOPTS) $(USER_MAKEOPTS) menuselect.makeopts && echo "menuselect changes saved!" || echo "menuselect changes NOT saved!"

menuselect/menuselect: makeopts menuselect/menuselect.c menuselect/menuselect_curses.c menuselect/menuselect_stub.c menuselect/menuselect.h menuselect/linkedlists.h makeopts mxml/libmxml.a
	@CFLAGS="-include $(ASTTOPDIR)/include/asterisk/autoconfig.h -I$(ASTTOPDIR)/include" PARENTSRC="$(ASTTOPDIR)" $(MAKE) -C menuselect CC="$(HOST_CC)" menuselect

mxml/libmxml.a:
	@cd mxml && unset CC CFLAGS AST_LIBS && test -f config.h || ./configure --build=$(BUILD_PLATFORM) --host=$(BUILD_PLATFORM)
	$(MAKE) -C mxml libmxml.a

menuselect-tree: $(foreach dir,$(filter-out main,$(MOD_SUBDIRS)),$(wildcard $(dir)/*.c) $(wildcard $(dir)/*.cc)) build_tools/cflags.xml sounds/sounds.xml build_tools/embed_modules.xml
	@echo "Generating input for menuselect ..."
	@build_tools/prep_moduledeps > $@

.PHONY: menuselect main sounds clean clean-depend dist-clean distclean all prereqs depend cleantest uninstall _uninstall uninstall-all dont-optimize $(SUBDIRS_INSTALL) $(SUBDIRS_CLEAN) $(SUBDIRS_CLEAN_DEPEND) $(SUBDIRS_DEPEND) $(SUBDIRS_UNINSTALL) $(SUBDIRS) $(MOD_SUBDIRS_EMBED_LDSCRIPT) $(MOD_SUBDIRS_EMBED_LDFLAGS) $(MOD_SUBDIRS_EMBED_LIBS)
