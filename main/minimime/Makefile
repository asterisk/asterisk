#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for resource modules
#
# Copyright (C) 2007, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

include $(ASTTOPDIR)/Makefile.moddir_rules

LIBMMIME:=libmmime.a
MM_SRCS= \
	mimeparser.tab.c \
	mimeparser.yy.c \
	mm_init.c \
	mm_base64.c \
	mm_codecs.c \
	mm_contenttype.c \
	mm_context.c \
	mm_envelope.c \
	mm_error.c \
	mm_header.c \
	mm_mem.c \
	mm_mimepart.c \
	mm_mimeutil.c \
	mm_param.c \
	mm_parse.c \
	mm_util.c

MM_OBJS:=$(MM_SRCS:%.c=%.o)
MM_HDRS:=mm.h mm_util.h

# Use weaker error checking because we have some automatically generated
# files. However just mask out -Werror, because other warnings below:
#	-Wundef -Wstrict-prototypes -Wmissing-declarations
#	-Wmissing-prototypes
# may actually be important and spot out real bugs.
ASTCFLAGS:=$(filter-out -Werror,$(ASTCFLAGS))

ASTCFLAGS+=-std=c99

all: $(LIBMMIME)

$(LIBMMIME): $(MM_OBJS)
	$(ECHO_PREFIX) echo "   [AR] $^ -> $@"
	$(CMD_PREFIX) $(AR) cr $@ $^
	$(CMD_PREFIX) $(RANLIB) $@

mimeparser.yy.c:
	flex -Pmimeparser_yy -omimeparser.yy.c mimeparser.l

mimeparser.tab.c:
	bison -d -pmimeparser_yy -omimeparser.tab.c mimeparser.y

clean::
	rm -f $(LIBMMIME) *.o

.PHONY: clean all

ifneq ($(wildcard .*.d),)
   include .*.d
endif
