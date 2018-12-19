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

/*
 * Block automatic include of asterisk/lock.h to allow use of pthread_mutex
 * functions directly.  We don't need or want the lock.h overhead.
 */
#define _ASTERISK_LOCK_H

/*
 * The astmm ast_ memory management functions can cause ast_bt_get_symbols
 * to be invoked so we must not use them.
 */
#define ASTMM_LIBC ASTMM_IGNORE

#include "asterisk.h"
#include "asterisk/backtrace.h"

/*
 * As stated above, the vector macros call the ast_ functions so
 * we need to remap those back to the libc ones.
 */
#undef ast_free
#undef ast_calloc
#undef ast_malloc
#define ast_free(x) free(x)
#define ast_calloc(n, x) calloc(n, x)
#define ast_malloc(x) malloc(x)

#include "asterisk/vector.h"

#ifdef HAVE_BKTR
#include <execinfo.h>
#if defined(HAVE_DLADDR) && defined(HAVE_BFD) && defined(BETTER_BACKTRACES)
#include <dlfcn.h>
#include <bfd.h>
#endif

#include <pthread.h>

/* simple definition of S_OR so we don't have include strings.h */
#define S_OR(a, b) (a && a[0] != '\0') ? a : b

struct ast_bt *__ast_bt_create(void)
{
	struct ast_bt *bt = calloc(1, sizeof(*bt));

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
		free(bt);
	}
	return NULL;
}

#ifdef BETTER_BACKTRACES

struct bfd_data {
	struct ast_vector_string *return_strings;
	bfd_vma pc;            /* bfd.h */
	asymbol **syms;        /* bfd.h */
	Dl_info dli;           /* dlfcn.h */
	const char *libname;
	int dynamic;
	int has_syms;
	char *msg;
	int found;
};

#define MSG_BUFF_LEN 1024

static void process_section(bfd *bfdobj, asection *section, void *obj)
{
	struct bfd_data *data = obj;
	const char *file, *func;
	unsigned int line;
	bfd_vma offset;
	bfd_vma vma;
	bfd_size_type size;
	bfd_boolean line_found = 0;
	char *fn;
	int inlined = 0;

	offset = data->pc - (data->dynamic ? (bfd_vma)(uintptr_t) data->dli.dli_fbase : 0);

	if (!(bfd_get_section_flags(bfdobj, section) & SEC_ALLOC)) {
		return;
	}

	vma = bfd_get_section_vma(bfdobj, section);
	size = bfd_get_section_size(section);

	if (offset < vma || offset >= vma + size) {
		/* Not in this section */
		return;
	}

	line_found = bfd_find_nearest_line(bfdobj, section, data->syms, offset - vma, &file,
		&func, &line);
	if (!line_found) {
		return;
	}

	/*
	 * If we find a line, we will want to continue calling bfd_find_inliner_info
	 * to capture any inlined functions that don't have their own stack frames.
	 */
	do {
		data->found++;
		/* file can possibly be null even with a success result from bfd_find_nearest_line */
		file = file ? file : "";
		fn = strrchr(file, '/');
#define FMT_INLINED     "[%s] %s %s:%u %s()"
#define FMT_NOT_INLINED "[%p] %s %s:%u %s()"

		snprintf(data->msg, MSG_BUFF_LEN, inlined ? FMT_INLINED : FMT_NOT_INLINED,
			inlined ? "inlined" : (char *)(uintptr_t) data->pc,
			data->libname,
			fn ? fn + 1 : file,
			line, S_OR(func, "???"));

		if (AST_VECTOR_APPEND(data->return_strings, strdup(data->msg))) {
			return;
		}

		inlined++;
		/* Let's see if there are any inlined functions */
	} while (bfd_find_inliner_info(bfdobj, &file, &func, &line));
}

struct ast_vector_string *__ast_bt_get_symbols(void **addresses, size_t num_frames)
{
	struct ast_vector_string *return_strings;
	int stackfr;
	bfd *bfdobj;
	long allocsize;
	char msg[MSG_BUFF_LEN];
	static pthread_mutex_t bfd_mutex = PTHREAD_MUTEX_INITIALIZER;

	return_strings = malloc(sizeof(struct ast_vector_string));
	if (!return_strings) {
		return NULL;
	}
	if (AST_VECTOR_INIT(return_strings, num_frames)) {
		free(return_strings);
		return NULL;
	}

	for (stackfr = 0; stackfr < num_frames; stackfr++) {
		int symbolcount;
		struct bfd_data data = {
			.return_strings = return_strings,
			.msg = msg,
			.pc = (bfd_vma)(uintptr_t) addresses[stackfr],
			.found = 0,
			.dynamic = 0,
		};

		msg[0] = '\0';

		if (!dladdr((void *)(uintptr_t) data.pc, &data.dli)) {
			continue;
		}
		data.libname = strrchr(data.dli.dli_fname, '/');
		if (!data.libname) {
			data.libname = data.dli.dli_fname;
		} else {
			data.libname++;
		}

		pthread_mutex_lock(&bfd_mutex);
		/* Using do while(0) here makes it easier to escape and clean up */
		do {
			bfdobj = bfd_openr(data.dli.dli_fname, NULL);
			if (!bfdobj) {
				break;
			}

			/* bfd_check_format does more than check.  It HAS to be called */
			if (!bfd_check_format(bfdobj, bfd_object)) {
				break;
			}

			data.has_syms = !!(bfd_get_file_flags(bfdobj) & HAS_SYMS);
			data.dynamic = !!(bfd_get_file_flags(bfdobj) & DYNAMIC);

			if (!data.has_syms) {
				break;
			}

			allocsize = data.dynamic ?
				bfd_get_dynamic_symtab_upper_bound(bfdobj) : bfd_get_symtab_upper_bound(bfdobj);
			if (allocsize < 0) {
				break;
			}

			data.syms = malloc(allocsize);
			if (!data.syms) {
				break;
			}

			symbolcount = data.dynamic ?
				bfd_canonicalize_dynamic_symtab(bfdobj, data.syms) : bfd_canonicalize_symtab(bfdobj, data.syms);
			if (symbolcount < 0) {
				break;
			}

			bfd_map_over_sections(bfdobj, process_section, &data);
		} while(0);

		if (bfdobj) {
			bfd_close(bfdobj);
			free(data.syms);
			data.syms = NULL;
		}
		pthread_mutex_unlock(&bfd_mutex);

		/* Default output, if we cannot find the information within BFD */
		if (!data.found) {
			snprintf(msg, sizeof(msg), "%s %s()",
				data.libname,
				S_OR(data.dli.dli_sname, "<unknown>"));
			AST_VECTOR_APPEND(return_strings, strdup(msg));
		}
	}

	return return_strings;
}

#else
struct ast_vector_string *__ast_bt_get_symbols(void **addresses, size_t num_frames)
{
	char **strings;
	struct ast_vector_string *return_strings;
	int i;

	return_strings = malloc(sizeof(struct ast_vector_string));
	if (!return_strings) {
		return NULL;
	}
	if (AST_VECTOR_INIT(return_strings, num_frames)) {
		free(return_strings);
		return NULL;
	}

	strings = backtrace_symbols(addresses, num_frames);
	if (strings) {
		for (i = 0; i < num_frames; i++) {
			AST_VECTOR_APPEND(return_strings, strdup(strings[i]));
		}
		free(strings);
	}

	return return_strings;
}
#endif /* BETTER_BACKTRACES */

void __ast_bt_free_symbols(struct ast_vector_string *symbols)
{
	AST_VECTOR_CALLBACK_VOID(symbols, free);
	AST_VECTOR_PTR_FREE(symbols);
}

#endif /* HAVE_BKTR */
