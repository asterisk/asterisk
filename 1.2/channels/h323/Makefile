#
# Makefile
#
# Make file for OpenH323 support layer
#

# Verify those options with main Makefile
STDCCFLAGS	+= -DNDEBUG
STDCCFLAGS	+= $(shell grep ^DEBUG_THREADS ../../Makefile | sed -e "s/^DEBUG_THREADS[ 	]*=//" -e "s/\([^\#]*\)\#.*/\1/")
STDCCFLAGS	+= -I../../include
STDCCFLAGS	+= -Wmissing-prototypes
STDCCFLAGS	+= -fPIC
#OPTCCFLAGS	+=
CFLAGS		+= -pipe
TARGET		+= libchanh323.a
TARGET		+= Makefile.ast
SOURCES		= ast_h323.cxx
OBJDIR		= .

ifndef OPENH323DIR
OPENH323DIR=$(HOME)/openh323
endif

include $(OPENH323DIR)/openh323u.mak

$(SOURCES)::	$(SOURCES:.cxx=.cpp)
	ln -f $< $@

$(SOURCES)::	Makefile ../../Makefile
	touch $(SOURCES)

libchanh323.a:	$(OBJS)
	ar crv $@ $(OBJS)

Makefile.ast:	FORCE
	@echo H323CFLAGS  = $(STDCCFLAGS) $(OPTCCFLAGS) $(CFLAGS) >$@.tmp
	@echo H323LDFLAGS = $(CFLAGS) $(LDFLAGS) >>$@.tmp
	@echo H323LDLIBS  = $(LDLIBS) $(ENDLDLIBS) $(ENDLDFLAGS) >>$@.tmp
	@if [ -r $@ ] && cmp -s $@ $@.tmp; then rm -f $@.tmp; else mv -f $@.tmp $@; fi

FORCE:
