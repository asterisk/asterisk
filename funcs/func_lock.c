/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Tilghman Lesher
 *
 * Tilghman Lesher <func_lock_2007@the-tilghman.com>
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
 * \brief Dialplan mutexes
 *
 * \author Tilghman Lesher <func_lock_2007@the-tilghman.com>
 *
 * \ingroup functions
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/linkedlists.h"

AST_LIST_HEAD_STATIC(locklist, lock_frame);

static void lock_free(void *data);
static int unloading = 0;

static struct ast_datastore_info lock_info = {
	.type = "MUTEX",
	.destroy = lock_free,
};

struct lock_frame {
	AST_LIST_ENTRY(lock_frame) entries;
	ast_mutex_t mutex;
	struct ast_channel *channel;
	char name[0];
};

static void lock_free(void *data)
{
	struct lock_frame *frame = data;
	if (!frame)
		return;
	frame->channel = NULL;
	ast_mutex_unlock(&frame->mutex);
}

static int get_lock(struct ast_channel *chan, char *lockname, int try)
{
	struct ast_datastore *lock_store = ast_channel_datastore_find(chan, &lock_info, NULL);
	struct lock_frame *current;
	int res;

	if (!lock_store) {
		ast_debug(1, "Channel %s has no lock datastore, so we're allocating one.\n", chan->name);
		lock_store = ast_channel_datastore_alloc(&lock_info, NULL);
		if (!lock_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  No locks will be obtained.\n");
			return -1;
		}
		ast_channel_datastore_add(chan, lock_store);
	}

	/* If the channel already has a lock, then free the existing lock */
	if (lock_store->data) {
		struct lock_frame *old = lock_store->data;
		old->channel = NULL;
		ast_mutex_unlock(&old->mutex);
	}

	/* Lock already exist? */
	AST_LIST_LOCK(&locklist);
	AST_LIST_TRAVERSE(&locklist, current, entries) {
		if (strcmp(current->name, lockname) == 0) {
			break;
		}
	}

	if (!current) {
		if (unloading) {
			/* Don't bother */
			AST_LIST_UNLOCK(&locklist);
			return -1;
		}

		/* Create new lock entry */
		current = ast_calloc(1, sizeof(*current) + strlen(lockname) + 1);
		if (!current) {
			AST_LIST_UNLOCK(&locklist);
			return -1;
		}

		strcpy((char *)current + sizeof(*current), lockname);
		ast_mutex_init(&current->mutex);
		AST_LIST_INSERT_TAIL(&locklist, current, entries);
	}

	res = try ? ast_mutex_trylock(&current->mutex) : ast_mutex_lock(&current->mutex);
	if (res == 0) {
		lock_store->data = current;
		current->channel = chan;
	}

	AST_LIST_UNLOCK(&locklist);
	return res;
}

static int unlock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *lock_store = ast_channel_datastore_find(chan, &lock_info, NULL);
	struct lock_frame *current;

	if (!lock_store) {
		ast_log(LOG_WARNING, "No datastore for dialplan locks.  Nothing was ever locked!\n");
		ast_copy_string(buf, "0", len);
		return 0;
	}

	current = lock_store->data;

	if (!current) {
		ast_copy_string(buf, "0", len);
		return 0;
	}

	current->channel = NULL;
	ast_mutex_unlock(&current->mutex);
	ast_copy_string(buf, "1", len);
	return 0;
}

static int lock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	ast_copy_string(buf, get_lock(chan, data, 0) ? "0" : "1", len);
	return 0;
}

static int trylock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	ast_copy_string(buf, get_lock(chan, data, 1) ? "0" : "1", len);
	return 0;
}

static struct ast_custom_function lock_function = {
	.name = "LOCK",
	.synopsis = "Attempt to obtain a named mutex",
	.desc =
"Attempts to grab a named lock exclusively, and prevents other channels\n"
"from obtaining the same lock.  LOCK will wait for the lock to become\n"
"available.  Returns 1 if the lock was obtained or 0 on error.\n\n"
"Note: to avoid the possibility of a deadlock, LOCK will only attempt to\n"
"grab a single lock.  If you have a lock already and you attempt to lock\n"
"another name, LOCK will unlock the first name before attempting to lock\n"
"the second name.\n",
	.syntax = "LOCK(<lockname>)",
	.read = lock_read,
};

static struct ast_custom_function trylock_function = {
	.name = "TRYLOCK",
	.synopsis = "Attempt to obtain a named mutex",
	.desc =
"Attempts to grab a named lock exclusively, and prevents other channels\n"
"from obtaining the same lock.  Returns 1 if the lock was available or 0\n"
"otherwise.\n\n"
"Note: to avoid the possibility of a deadlock, TRYLOCK will only attempt to\n"
"grab a single lock.  If you have a lock already and you attempt to lock\n"
"another name, TRYLOCK will unlock the first name before attempting to lock\n"
"the second name.\n",
	.syntax = "TRYLOCK(<lockname>)",
	.read = trylock_read,
};

static struct ast_custom_function unlock_function = {
	.name = "UNLOCK",
	.synopsis = "Unlocks a named mutex",
	.desc =
"Unlocks a previously locked mutex.  Note that it is generally unnecessary to\n"
"unlock in a hangup routine, as any lock held is automatically freed when the\n"
"channel is destroyed.  Returns 1 if the channel had a lock or 0 otherwise.\n",
	.syntax = "UNLOCK()",
	.read = unlock_read,
};

static int unload_module(void)
{
	struct lock_frame *current;

	/* Module flag */
	unloading = 1;

	AST_LIST_LOCK(&locklist);
	while ((current = AST_LIST_REMOVE_HEAD(&locklist, entries))) {
		/* If any locks are currently in use, then we cannot unload this module */
		if (current->channel) {
			/* Put it back */
			AST_LIST_INSERT_HEAD(&locklist, current, entries);
			AST_LIST_UNLOCK(&locklist);
			unloading = 0;
			return -1;
		}
		ast_mutex_destroy(&current->mutex);
		ast_free(current);
	}

	/* No locks left, unregister functions */
	ast_custom_function_unregister(&lock_function);
	ast_custom_function_unregister(&trylock_function);
	ast_custom_function_unregister(&unlock_function);

	AST_LIST_UNLOCK(&locklist);
	return 0;
}

static int load_module(void)
{
	int res = ast_custom_function_register(&lock_function);
	res |= ast_custom_function_register(&trylock_function);
	res |= ast_custom_function_register(&unlock_function);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialplan mutexes");
