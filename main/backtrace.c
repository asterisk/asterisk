/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Asterisk backtrace generation
 *
 * This file provides backtrace generation utilities
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
ASTERISK_REGISTER_FILE();

#include "asterisk/backtrace.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"

#ifdef HAVE_BKTR
#include <execinfo.h>
#if defined(HAVE_DLADDR) && defined(HAVE_BFD) && defined(BETTER_BACKTRACES)
#include <dlfcn.h>
#include <bfd.h>
#endif

struct ast_bt *__ast_bt_create(void)
{
	struct ast_bt *bt = ast_std_calloc(1, sizeof(*bt));

	if (!bt) {
		return NULL;
	}
	bt->alloced = 1;

	ast_bt_get_addresses(bt);

	return bt;
}

int __ast_bt_get_addresses(struct ast_bt *bt)
{
	bt->num_frames = backtrace(bt->addresses, AST_MAX_BT_FRAMES);
	return 0;
}

void *__ast_bt_destroy(struct ast_bt *bt)
{
	if (bt && bt->alloced) {
		ast_std_free(bt);
	}
	return NULL;
}

char **__ast_bt_get_symbols(void **addresses, size_t num_frames)
{
	char **strings;
#if defined(BETTER_BACKTRACES)
	int stackfr;
	bfd *bfdobj;           /* bfd.h */
	Dl_info dli;           /* dlfcn.h */
	long allocsize;
	asymbol **syms = NULL; /* bfd.h */
	bfd_vma offset;        /* bfd.h */
	const char *lastslash;
	asection *section;
	const char *file, *func;
	unsigned int line;
	char address_str[128];
	char msg[1024];
	size_t strings_size;
	size_t *eachlen;
#endif

#if defined(BETTER_BACKTRACES)
	strings_size = num_frames * sizeof(*strings);

	eachlen = ast_std_calloc(num_frames, sizeof(*eachlen));
	strings = ast_std_calloc(num_frames, sizeof(*strings));
	if (!eachlen || !strings) {
		ast_std_free(eachlen);
		ast_std_free(strings);
		return NULL;
	}

	for (stackfr = 0; stackfr < num_frames; stackfr++) {
		int found = 0, symbolcount;

		msg[0] = '\0';

		if (!dladdr(addresses[stackfr], &dli)) {
			continue;
		}

		if (strcmp(dli.dli_fname, "asterisk") == 0) {
			char asteriskpath[256];

			if (!(dli.dli_fname = ast_utils_which("asterisk", asteriskpath, sizeof(asteriskpath)))) {
				/* This will fail to find symbols */
				dli.dli_fname = "asterisk";
			}
		}

		lastslash = strrchr(dli.dli_fname, '/');
		if ((bfdobj = bfd_openr(dli.dli_fname, NULL)) &&
			bfd_check_format(bfdobj, bfd_object) &&
			(allocsize = bfd_get_symtab_upper_bound(bfdobj)) > 0 &&
			(syms = ast_std_malloc(allocsize)) &&
			(symbolcount = bfd_canonicalize_symtab(bfdobj, syms))) {

			if (bfdobj->flags & DYNAMIC) {
				offset = addresses[stackfr] - dli.dli_fbase;
			} else {
				offset = addresses[stackfr] - (void *) 0;
			}

			for (section = bfdobj->sections; section; section = section->next) {
				if (!bfd_get_section_flags(bfdobj, section) & SEC_ALLOC ||
					section->vma > offset ||
					section->size + section->vma < offset) {
					continue;
				}

				if (!bfd_find_nearest_line(bfdobj, section, syms, offset - section->vma, &file, &func, &line)) {
					continue;
				}

				/* file can possibly be null even with a success result from bfd_find_nearest_line */
				file = file ? file : "";

				/* Stack trace output */
				found++;
				if ((lastslash = strrchr(file, '/'))) {
					const char *prevslash;

					for (prevslash = lastslash - 1; *prevslash != '/' && prevslash >= file; prevslash--) {
					}
					if (prevslash >= file) {
						lastslash = prevslash;
					}
				}
				if (dli.dli_saddr == NULL) {
					address_str[0] = '\0';
				} else {
					snprintf(address_str, sizeof(address_str), " (%p+%lX)",
						dli.dli_saddr,
						(unsigned long) (addresses[stackfr] - dli.dli_saddr));
				}
				snprintf(msg, sizeof(msg), "%s:%u %s()%s",
					lastslash ? lastslash + 1 : file, line,
					S_OR(func, "???"),
					address_str);

				break; /* out of section iteration */
			}
		}
		if (bfdobj) {
			bfd_close(bfdobj);
			ast_std_free(syms);
		}

		/* Default output, if we cannot find the information within BFD */
		if (!found) {
			if (dli.dli_saddr == NULL) {
				address_str[0] = '\0';
			} else {
				snprintf(address_str, sizeof(address_str), " (%p+%lX)",
					dli.dli_saddr,
					(unsigned long) (addresses[stackfr] - dli.dli_saddr));
			}
			snprintf(msg, sizeof(msg), "%s %s()%s",
				lastslash ? lastslash + 1 : dli.dli_fname,
				S_OR(dli.dli_sname, "<unknown>"),
				address_str);
		}

		if (!ast_strlen_zero(msg)) {
			char **tmp;

			eachlen[stackfr] = strlen(msg) + 1;
			if (!(tmp = ast_std_realloc(strings, strings_size + eachlen[stackfr]))) {
				ast_std_free(strings);
				strings = NULL;
				break; /* out of stack frame iteration */
			}
			strings = tmp;
			strings[stackfr] = (char *) strings + strings_size;
			strcpy(strings[stackfr], msg);/* Safe since we just allocated the room. */
			strings_size += eachlen[stackfr];
		}
	}

	if (strings) {
		/* Recalculate the offset pointers because of the reallocs. */
		strings[0] = (char *) strings + num_frames * sizeof(*strings);
		for (stackfr = 1; stackfr < num_frames; stackfr++) {
			strings[stackfr] = strings[stackfr - 1] + eachlen[stackfr - 1];
		}
	}
	ast_std_free(eachlen);

#else /* !defined(BETTER_BACKTRACES) */

	strings = backtrace_symbols(addresses, num_frames);
#endif /* defined(BETTER_BACKTRACES) */
	return strings;
}

#endif /* HAVE_BKTR */
