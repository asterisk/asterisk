#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for dialplan functions
#
# Copyright (C) 2005, Digium, Inc.
#
# Kevin P. Fleming <kpfleming@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

FUNCS=pbx_functions.so

BUILTINS=func_md5.o func_groupcount.o func_strings.o func_cdr.o func_logic.o func_env.o

STANDALONE_FUNCS=$(filter-out $(BUILTINS),$(patsubst %.c,%.o,$(wildcard func*.c)))

FUNCS+=$(STANDALONE_FUNCS:.o=.so)

FUNC_STRUCTS=$(shell grep 'struct ast_custom_function' $(BUILTINS:.o=.c) | awk '{print $$3};')

CFLAGS+=-fPIC

all: $(FUNCS)

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

$(BUILTINS) : CFLAGS += -DBUILTIN_FUNC

pbx_functions.h: $(BUILTINS:.o=.c)
	@echo "/* Automatically generated - do not edit */" > $@
	@for f in $(FUNC_STRUCTS); do echo "extern struct ast_custom_function $$f;" >> $@; done
	@echo "static struct ast_custom_function *builtins[] = {" >> $@
	@for f in $(FUNC_STRUCTS); do echo "&$$f," >> $@; done
	@echo "};" >> $@

pbx_functions.so: pbx_functions.o $(BUILTINS)
	$(CC) $(SOLINK) -o $@ $(BUILTINS) $<
	strip $(foreach f,$(FUNC_STRUCTS),-N $(f)) $@

install: all
	for x in $(FUNCS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend: pbx_functions.h
	../mkdep $(CFLAGS) `ls *.c`

env:
	env
