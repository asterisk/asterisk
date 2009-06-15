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

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"

/*** DOCUMENTATION
	<function name="LOCK" language="en_US">
		<synopsis>
			Attempt to obtain a named mutex.
		</synopsis>
		<syntax>
			<parameter name="lockname" required="true" />
		</syntax>
		<description>
			<para>Attempts to grab a named lock exclusively, and prevents other channels from
			obtaining the same lock.  LOCK will wait for the lock to become available.
			Returns <literal>1</literal> if the lock was obtained or <literal>0</literal> on error.</para>
			<note><para>To avoid the possibility of a deadlock, LOCK will only attempt to
			obtain the lock for 3 seconds if the channel already has another lock.</para></note>
		</description>
	</function>
	<function name="TRYLOCK" language="en_US">
		<synopsis>
			Attempt to obtain a named mutex.
		</synopsis>
		<syntax>
			<parameter name="lockname" required="true" />
		</syntax>
		<description>
			<para>Attempts to grab a named lock exclusively, and prevents other channels
			from obtaining the same lock.  Returns <literal>1</literal> if the lock was 
			available or <literal>0</literal> otherwise.</para>
		</description>
	</function>
	<function name="UNLOCK" language="en_US">
		<synopsis>
			Unlocks a named mutex.
		</synopsis>
		<syntax>
			<parameter name="lockname" required="true" />
		</syntax>
		<description>
			<para>Unlocks a previously locked mutex. Returns <literal>1</literal> if the channel 
			had a lock or <literal>0</literal> otherwise.</para>
			<note><para>It is generally unnecessary to unlock in a hangup routine, as any locks 
			held are automatically freed when the channel is destroyed.</para></note>
		</description>
	</function>
 ***/



static AST_LIST_HEAD_STATIC(locklist, lock_frame);

static void lock_free(void *data);
static int unloading = 0;

static struct ast_datastore_info lock_info = {
	.type = "MUTEX",
	.destroy = lock_free,
};

struct lock_frame {
	AST_LIST_ENTRY(lock_frame) entries;
	ast_mutex_t mutex;
	/*! count is needed so if a recursive mutex exits early, we know how many times to unlock it. */
	unsigned int count;
	/*! who owns us */
	struct ast_channel *channel;
	/*! name of the lock */
	char name[0];
};

struct channel_lock_frame {
	AST_LIST_ENTRY(channel_lock_frame) list;
	/*! Need to save channel pointer here, because during destruction, we won't have it. */
	struct ast_channel *channel;
	struct lock_frame *lock_frame;
};

static void lock_free(void *data)
{
	AST_LIST_HEAD(, channel_lock_frame) *oldlist = data;
	struct channel_lock_frame *clframe;
	AST_LIST_LOCK(oldlist);
	while ((clframe = AST_LIST_REMOVE_HEAD(oldlist, list))) {
		/* Only unlock if we own the lock */
		if (clframe->channel == clframe->lock_frame->channel) {
			clframe->lock_frame->channel = NULL;
			while (clframe->lock_frame->count > 0) {
				clframe->lock_frame->count--;
				ast_mutex_unlock(&clframe->lock_frame->mutex);
			}
		}
		ast_free(clframe);
	}
	AST_LIST_UNLOCK(oldlist);
	AST_LIST_HEAD_DESTROY(oldlist);
	ast_free(oldlist);
}

static int get_lock(struct ast_channel *chan, char *lockname, int try)
{
	struct ast_datastore *lock_store = ast_channel_datastore_find(chan, &lock_info, NULL);
	struct lock_frame *current;
	struct channel_lock_frame *clframe = NULL, *save_clframe = NULL;
	AST_LIST_HEAD(, channel_lock_frame) *list;
	int res, count_channel_locks = 0;

	if (!lock_store) {
		ast_debug(1, "Channel %s has no lock datastore, so we're allocating one.\n", chan->name);
		lock_store = ast_datastore_alloc(&lock_info, NULL);
		if (!lock_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  No locks will be obtained.\n");
			return -1;
		}

		list = ast_calloc(1, sizeof(*list));
		if (!list) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  %sLOCK will fail.\n", try ? "TRY" : "");
			ast_datastore_free(lock_store);
			return -1;
		}

		lock_store->data = list;
		AST_LIST_HEAD_INIT(list);
		ast_channel_datastore_add(chan, lock_store);
	} else
		list = lock_store->data;

	/* Lock already exists? */
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
	AST_LIST_UNLOCK(&locklist);

	/* Found lock or created one - now find or create the corresponding link in the channel */
	AST_LIST_LOCK(list);
	AST_LIST_TRAVERSE(list, clframe, list) {
		if (clframe->lock_frame == current)
			save_clframe = clframe;

		/* Only count mutexes that we currently hold */
		if (clframe->lock_frame->channel == chan)
			count_channel_locks++;
	}

	if (save_clframe) {
		clframe = save_clframe;
	} else {
		if (unloading) {
			/* Don't bother */
			AST_LIST_UNLOCK(list);
			return -1;
		}

		clframe = ast_calloc(1, sizeof(*clframe));
		if (!clframe) {
			ast_log(LOG_ERROR, "Unable to allocate channel lock frame.  %sLOCK will fail.\n", try ? "TRY" : "");
			AST_LIST_UNLOCK(list);
			return -1;
		}

		clframe->lock_frame = current;
		clframe->channel = chan;
		/* Count the lock just created */
		count_channel_locks++;
		AST_LIST_INSERT_TAIL(list, clframe, list);
	}
	AST_LIST_UNLOCK(list);

	/* Okay, we have both frames, so now we need to try to lock the mutex. */
	if (count_channel_locks > 1) {
		struct timeval start = ast_tvnow();
		for (;;) {
			if ((res = ast_mutex_trylock(&current->mutex)) == 0)
				break;
			if (ast_tvdiff_ms(ast_tvnow(), start) > 3000)
				break; /* bail after 3 seconds of waiting */
			usleep(1);
		}
	} else {
		/* If the channel doesn't have any locks so far, then there's no possible deadlock. */
		res = try ? ast_mutex_trylock(&current->mutex) : ast_mutex_lock(&current->mutex);
	}

	if (res == 0) {
		current->count++;
		current->channel = chan;
	}

	return res;
}

static int unlock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *lock_store = ast_channel_datastore_find(chan, &lock_info, NULL);
	struct channel_lock_frame *clframe;
	AST_LIST_HEAD(, channel_lock_frame) *list;

	if (!lock_store) {
		ast_log(LOG_WARNING, "No datastore for dialplan locks.  Nothing was ever locked!\n");
		ast_copy_string(buf, "0", len);
		return 0;
	}

	if (!(list = lock_store->data)) {
		ast_debug(1, "This should NEVER happen\n");
		ast_copy_string(buf, "0", len);
		return 0;
	}

	/* Find item in the channel list */
	AST_LIST_LOCK(list);
	AST_LIST_TRAVERSE(list, clframe, list) {
		if (clframe->lock_frame && clframe->lock_frame->channel == chan && strcmp(clframe->lock_frame->name, data) == 0) {
			break;
		}
	}
	/* We never destroy anything until channel destruction, which will never
	 * happen while this routine is executing, so we don't need to hold the
	 * lock beyond this point. */
	AST_LIST_UNLOCK(list);

	if (!clframe) {
		/* We didn't have this lock in the first place */
		ast_copy_string(buf, "0", len);
		return 0;
	}

	/* Decrement before we release, because if a channel is waiting on the
	 * mutex, there's otherwise a race to alter count. */
	clframe->lock_frame->count--;
	/* If we get another lock, this one shouldn't count against us for deadlock avoidance. */
	clframe->lock_frame->channel = NULL;
	ast_mutex_unlock(&clframe->lock_frame->mutex);

	ast_copy_string(buf, "1", len);
	return 0;
}

static int lock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{	
	if (chan)
		ast_autoservice_start(chan);

	ast_copy_string(buf, get_lock(chan, data, 0) ? "0" : "1", len);

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static int trylock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	if (chan)
		ast_autoservice_start(chan);

	ast_copy_string(buf, get_lock(chan, data, 1) ? "0" : "1", len);

	if (chan)
		ast_autoservice_stop(chan);

	return 0;
}

static struct ast_custom_function lock_function = {
	.name = "LOCK",
	.read = lock_read,
	.read_max = 2,
};

static struct ast_custom_function trylock_function = {
	.name = "TRYLOCK",
	.read = trylock_read,
	.read_max = 2,
};

static struct ast_custom_function unlock_function = {
	.name = "UNLOCK",
	.read = unlock_read,
	.read_max = 2,
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
