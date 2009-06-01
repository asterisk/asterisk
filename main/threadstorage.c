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
#include "asterisk/_private.h"

#if !defined(DEBUG_THREADLOCALS)

void threadstorage_init(void)
{
}

#else /* !defined(DEBUG_THREADLOCALS) */

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

/* Allow direct use of pthread_mutex_t and friends */
#undef pthread_mutex_t
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_init
#undef pthread_mutex_destroy

/*!
 * \brief lock for the tls_objects list
 *
 * \note We can not use an ast_mutex_t for this.  The reason is that this
 *       lock is used within the context of thread-local data destructors,
 *       and the ast_mutex_* API uses thread-local data.  Allocating more
 *       thread-local data at that point just causes a memory leak.
 */
static pthread_mutex_t threadstoragelock;

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

	pthread_mutex_lock(&threadstoragelock);
	AST_LIST_INSERT_TAIL(&tls_objects, to, entry);
	pthread_mutex_unlock(&threadstoragelock);
}

void __ast_threadstorage_object_remove(void *key)
{
	struct tls_object *to;

	pthread_mutex_lock(&threadstoragelock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&tls_objects, to, entry) {
		if (to->key == key) {
			AST_LIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	pthread_mutex_unlock(&threadstoragelock);
	if (to)
		ast_free(to);
}

void __ast_threadstorage_object_replace(void *key_old, void *key_new, size_t len)
{
	struct tls_object *to;

	pthread_mutex_lock(&threadstoragelock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&tls_objects, to, entry) {
		if (to->key == key_old) {
			to->key = key_new;
			to->size = len;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	pthread_mutex_unlock(&threadstoragelock);
}

static char *handle_cli_threadstorage_show_allocations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *fn = NULL;
	size_t len = 0;
	unsigned int count = 0;
	struct tls_object *to;

	switch (cmd) {
	case CLI_INIT:
		e->command = "threadstorage show allocations";
		e->usage =
			"Usage: threadstorage show allocations [<file>]\n"
			"       Dumps a list of all thread-specific memory allocations,\n"
			"       optionally limited to those from a specific file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 4)
		return CLI_SHOWUSAGE;

	if (a->argc > 3)
		fn = a->argv[3];

	pthread_mutex_lock(&threadstoragelock);

	AST_LIST_TRAVERSE(&tls_objects, to, entry) {
		if (fn && strcasecmp(to->file, fn))
			continue;

		ast_cli(a->fd, "%10d bytes allocated in %20s at line %5d of %25s (thread %p)\n",
			(int) to->size, to->function, to->line, to->file, (void *) to->thread);
		len += to->size;
		count++;
	}

	pthread_mutex_unlock(&threadstoragelock);

	ast_cli(a->fd, "%10d bytes allocated in %d allocation%s\n", (int) len, count, count > 1 ? "s" : "");
	
	return CLI_SUCCESS;
}

static char *handle_cli_threadstorage_show_summary(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *fn = NULL;
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

	switch (cmd) {
	case CLI_INIT:
		e->command = "threadstorage show summary";
		e->usage =
			"Usage: threadstorage show summary [<file>]\n"
			"       Summarizes thread-specific memory allocations by file, or optionally\n"
			"       by function, if a file is specified\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 4)
		return CLI_SHOWUSAGE;

	if (a->argc > 3)
		fn = a->argv[3];

	pthread_mutex_lock(&threadstoragelock);

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

	pthread_mutex_unlock(&threadstoragelock);
	
	AST_LIST_TRAVERSE(&file_summary, file, entry) {
		len += file->len;
		count += file->count;
		if (fn) {
			ast_cli(a->fd, "%10d bytes in %d allocation%ss in function %s\n",
				(int) file->len, file->count, file->count > 1 ? "s" : "", file->name);
		} else {
			ast_cli(a->fd, "%10d bytes in %d allocation%s in file %s\n",
				(int) file->len, file->count, file->count > 1 ? "s" : "", file->name);
		}
	}

	ast_cli(a->fd, "%10d bytes allocated in %d allocation%s\n", (int) len, count, count > 1 ? "s" : "");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_cli_threadstorage_show_allocations, "Display outstanding thread local storage allocations"),
	AST_CLI_DEFINE(handle_cli_threadstorage_show_summary,     "Summarize outstanding memory allocations")
};

void threadstorage_init(void)
{
	pthread_mutex_init(&threadstoragelock, NULL);
	ast_cli_register_multiple(cli, ARRAY_LEN(cli));
}

#endif /* !defined(DEBUG_THREADLOCALS) */

