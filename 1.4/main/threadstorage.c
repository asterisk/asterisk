/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 *
 * \brief Debugging support for thread-local-storage objects
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 */

#include "asterisk.h"

#if defined(DEBUG_THREADLOCALS)

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asterisk/logger.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"
#include "asterisk/linkedlists.h"
#include "asterisk/cli.h"

struct tls_object {
	void *key;
	size_t size;
	const char *file;
	const char *function;
	unsigned int line;
	pthread_t thread;
	AST_LIST_ENTRY(tls_object) entry;
};

static AST_LIST_HEAD_NOLOCK_STATIC(tls_objects, tls_object);
AST_MUTEX_DEFINE_STATIC_NOTRACKING(threadstoragelock);


void __ast_threadstorage_object_add(void *key, size_t len, const char *file, const char *function, unsigned int line)
{
	struct tls_object *to;

	if (!(to = ast_calloc(1, sizeof(*to))))
		return;

	to->key = key;
	to->size = len;
	to->file = file;
	to->function = function;
	to->line = line;
	to->thread = pthread_self();

	ast_mutex_lock(&threadstoragelock);
	AST_LIST_INSERT_TAIL(&tls_objects, to, entry);
	ast_mutex_unlock(&threadstoragelock);
}

void __ast_threadstorage_object_remove(void *key)
{
	struct tls_object *to;

	ast_mutex_lock(&threadstoragelock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&tls_objects, to, entry) {
		if (to->key == key) {
			AST_LIST_REMOVE_CURRENT(&tls_objects, entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	ast_mutex_unlock(&threadstoragelock);
	if (to)
		free(to);
}

void __ast_threadstorage_object_replace(void *key_old, void *key_new, size_t len)
{
	struct tls_object *to;

	ast_mutex_lock(&threadstoragelock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&tls_objects, to, entry) {
		if (to->key == key_old) {
			to->key = key_new;
			to->size = len;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	ast_mutex_unlock(&threadstoragelock);
}

static int handle_show_allocations(int fd, int argc, char *argv[])
{
	char *fn = NULL;
	size_t len = 0;
	unsigned int count = 0;
	struct tls_object *to;

	if (argc > 3)
		fn = argv[3];

	ast_mutex_lock(&threadstoragelock);

	AST_LIST_TRAVERSE(&tls_objects, to, entry) {
		if (fn && strcasecmp(to->file, fn))
			continue;

		ast_cli(fd, "%10d bytes allocated in %20s at line %5d of %25s (thread %p)\n",
			(int) to->size, to->function, to->line, to->file, (void *) to->thread);
		len += to->size;
		count++;
	}

	ast_mutex_unlock(&threadstoragelock);

	ast_cli(fd, "%10d bytes allocated in %d allocation%s\n", (int) len, count, count > 1 ? "s" : "");
	
	return RESULT_SUCCESS;
}

static int handle_show_summary(int fd, int argc, char *argv[])
{
	char *fn = NULL;
	size_t len = 0;
	unsigned int count = 0;
	struct tls_object *to;
	struct file {
		const char *name;
		size_t len;
		unsigned int count;
		AST_LIST_ENTRY(file) entry;
	} *file;
	AST_LIST_HEAD_NOLOCK_STATIC(file_summary, file);

	if (argc > 3)
		fn = argv[3];

	ast_mutex_lock(&threadstoragelock);

	AST_LIST_TRAVERSE(&tls_objects, to, entry) {
		if (fn && strcasecmp(to->file, fn))
			continue;

		AST_LIST_TRAVERSE(&file_summary, file, entry) {
			if ((!fn && (file->name == to->file)) || (fn && (file->name == to->function)))
				break;
		}

		if (!file) {
			file = alloca(sizeof(*file));
			memset(file, 0, sizeof(*file));
			file->name = fn ? to->function : to->file;
			AST_LIST_INSERT_TAIL(&file_summary, file, entry);
		}

		file->len += to->size;
		file->count++;
	}

	ast_mutex_unlock(&threadstoragelock);
	
	AST_LIST_TRAVERSE(&file_summary, file, entry) {
		len += file->len;
		count += file->count;
		if (fn) {
			ast_cli(fd, "%10d bytes in %d allocation%ss in function %s\n",
				(int) file->len, file->count, file->count > 1 ? "s" : "", file->name);
		} else {
			ast_cli(fd, "%10d bytes in %d allocation%s in file %s\n",
				(int) file->len, file->count, file->count > 1 ? "s" : "", file->name);
		}
	}

	ast_cli(fd, "%10d bytes allocated in %d allocation%s\n", (int) len, count, count > 1 ? "s" : "");
	
	return RESULT_SUCCESS;
}

static struct ast_cli_entry cli[] = {
	{
		.cmda = { "threadstorage", "show", "allocations", NULL },
		.handler = handle_show_allocations,
		.summary = "Display outstanding thread local storage allocations",
		.usage =
		"Usage: threadstorage show allocations [<file>]\n"
		"       Dumps a list of all thread-specific memory allocations,\n"
		"optionally limited to those from a specific file\n",
	},
	{
		.cmda = { "threadstorage", "show", "summary", NULL },
		.handler = handle_show_summary,
		.summary = "Summarize outstanding memory allocations",
		.usage =
		"Usage: threadstorage show summary [<file>]\n"
		"       Summarizes thread-specific memory allocations by file, or optionally\n"
		"by function, if a file is specified\n",
	},
};

void threadstorage_init(void)
{
	ast_cli_register_multiple(cli, sizeof(cli) / sizeof(cli[0]));
}

#else /* !defined(DEBUG_THREADLOCALS) */

void threadstorage_init(void)
{
}

#endif /* !defined(DEBUG_THREADLOCALS) */

