#
# Makefile
#
# Make file for OpenH323 support layer
#

.PHONY: Makefile.ast clean

default::	opt

# Verify those options with main Makefile
STDCCFLAGS	= -DNDEBUG
STDCCFLAGS	+= -I../../include -include ../../include/asterisk/autoconfig.h
STDCCFLAGS	+= -fPIC
#OPTCCFLAGS	+=
CFLAGS		= -pipe
TARGET		= libchanh323.a
TARGET		+= Makefile.ast
SOURCES		= ast_h323.cxx compat_h323.cxx cisco-h225.cxx
OBJDIR		= .
OBJS		=

ifndef OPENH323DIR
OPENH323DIR=/usr/src/OpenH323/openh323
endif

include $(OPENH323DIR)/openh323u.mak

notrace::
	$(MAKE) NOTRACE=1 opt

define module_cxx_template
$(1)::	$(2)
	ln -f $(2) $(1)
endef

$(foreach mod,$(SOURCES),$(eval $(call module_cxx_template,$(mod),$(mod:.cxx=.cpp))))
#$(SOURCES)::	$(SOURCES:.cxx=.cpp)
#	ln -f $(patsubst %.cxx, %.cpp, $@) $@

$(SOURCES)::	Makefile ../../Makefile
	touch $@

libchanh323.a:	$(OBJS)
	ar crv $@ $(OBJS)

cisco-h225.cpp::	cisco-h225.asn
	asnparser -m CISCO_H225 -c $<
	mv -f cisco-h225.cxx cisco-h225.cpp

Makefile.ast:
	@echo H323CFLAGS  = $(STDCCFLAGS) $(OPTCCFLAGS) $(CFLAGS) >$@.tmp
	@echo H323LDFLAGS = $(CFLAGS) $(LDFLAGS) >>$@.tmp
	@echo H323LDLIBS  = $(LDLIBS) $(ENDLDLIBS) $(ENDLDFLAGS) >>$@.tmp
	@if [ -r $@ ] && cmp -s $@ $@.tmp; then rm -f $@.tmp; else mv -f $@.tmp $@; fi

clean::
	rm -f $(SOURCES) $(TARGET) $(OBJS) Makefile.ast *.dep
