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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <signal.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"

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
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
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
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
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
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
		</description>
	</function>
 ***/



static AST_LIST_HEAD_STATIC(locklist, lock_frame);

static void lock_free(void *data);
static void lock_fixup(void *data, struct ast_channel *oldchan, struct ast_channel *newchan);
static int unloading = 0;
static pthread_t broker_tid = AST_PTHREADT_NULL;

static const struct ast_datastore_info lock_info = {
	.type = "MUTEX",
	.destroy = lock_free,
	.chan_fixup = lock_fixup,
};

struct lock_frame {
	AST_LIST_ENTRY(lock_frame) entries;
	ast_mutex_t mutex;
	ast_cond_t cond;
	/*! count is needed so if a recursive mutex exits early, we know how many times to unlock it. */
	unsigned int count;
	/*! Container of requesters for the named lock */
	struct ao2_container *requesters;
	/*! who owns us */
	struct ast_channel *owner;
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
		if (clframe->channel == clframe->lock_frame->owner) {
			clframe->lock_frame->count = 0;
			clframe->lock_frame->owner = NULL;
		}
		ast_free(clframe);
	}
	AST_LIST_UNLOCK(oldlist);
	AST_LIST_HEAD_DESTROY(oldlist);
	ast_free(oldlist);
}

static void lock_fixup(void *data, struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct ast_datastore *lock_store = ast_channel_datastore_find(oldchan, &lock_info, NULL);
	AST_LIST_HEAD(, channel_lock_frame) *list;
	struct channel_lock_frame *clframe = NULL;

	if (!lock_store) {
		return;
	}
	list = lock_store->data;

	AST_LIST_LOCK(list);
	AST_LIST_TRAVERSE(list, clframe, list) {
		if (clframe->lock_frame->owner == oldchan) {
			clframe->lock_frame->owner = newchan;
		}
		/* We don't move requesters, because the thread stack is different */
		clframe->channel = newchan;
	}
	AST_LIST_UNLOCK(list);
}

static void *lock_broker(void *unused)
{
	struct lock_frame *frame;
	struct timespec forever = { 1000000, 0 };
	for (;;) {
		int found_requester = 0;

		/* Test for cancel outside of the lock */
		pthread_testcancel();
		AST_LIST_LOCK(&locklist);

		AST_LIST_TRAVERSE(&locklist, frame, entries) {
			if (ao2_container_count(frame->requesters)) {
				found_requester++;
				ast_mutex_lock(&frame->mutex);
				if (!frame->owner) {
					ast_cond_signal(&frame->cond);
				}
				ast_mutex_unlock(&frame->mutex);
			}
		}

		AST_LIST_UNLOCK(&locklist);
		pthread_testcancel();

		/* If there are no requesters, then wait for a signal */
		if (!found_requester) {
			nanosleep(&forever, NULL);
		} else {
			sched_yield();
		}
	}
	/* Not reached */
	return NULL;
}

static int ast_channel_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_channel *chan = obj, *cmp_args = arg;
	return strcasecmp(ast_channel_name(chan), ast_channel_name(cmp_args)) ? 0 : CMP_MATCH;
}

static int get_lock(struct ast_channel *chan, char *lockname, int trylock)
{
	struct ast_datastore *lock_store = ast_channel_datastore_find(chan, &lock_info, NULL);
	struct lock_frame *current;
	struct channel_lock_frame *clframe = NULL;
	AST_LIST_HEAD(, channel_lock_frame) *list;
	int res = 0;
	struct timespec timeout = { 0, };
	struct timeval now;

	if (!lock_store) {
		ast_debug(1, "Channel %s has no lock datastore, so we're allocating one.\n", ast_channel_name(chan));
		lock_store = ast_datastore_alloc(&lock_info, NULL);
		if (!lock_store) {
			ast_log(LOG_ERROR, "Unable to allocate new datastore.  No locks will be obtained.\n");
			return -1;
		}

		list = ast_calloc(1, sizeof(*list));
		if (!list) {
			ast_log(LOG_ERROR,
				"Unable to allocate datastore list head.  %sLOCK will fail.\n",
				trylock ? "TRY" : "");
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

		strcpy(current->name, lockname); /* SAFE */
		if ((res = ast_mutex_init(&current->mutex))) {
			ast_log(LOG_ERROR, "Unable to initialize mutex: %s\n", strerror(res));
			ast_free(current);
			AST_LIST_UNLOCK(&locklist);
			return -1;
		}
		if ((res = ast_cond_init(&current->cond, NULL))) {
			ast_log(LOG_ERROR, "Unable to initialize condition variable: %s\n", strerror(res));
			ast_mutex_destroy(&current->mutex);
			ast_free(current);
			AST_LIST_UNLOCK(&locklist);
			return -1;
		}
		current->requesters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
			NULL, ast_channel_cmp_cb);
		if (!current->requesters) {
			ast_mutex_destroy(&current->mutex);
			ast_cond_destroy(&current->cond);
			ast_free(current);
			AST_LIST_UNLOCK(&locklist);
			return -1;
		}
		AST_LIST_INSERT_TAIL(&locklist, current, entries);
	}
	AST_LIST_UNLOCK(&locklist);

	/* Found lock or created one - now find or create the corresponding link in the channel */
	AST_LIST_LOCK(list);
	AST_LIST_TRAVERSE(list, clframe, list) {
		if (clframe->lock_frame == current) {
			break;
		}
	}

	if (!clframe) {
		if (unloading) {
			/* Don't bother */
			AST_LIST_UNLOCK(list);
			return -1;
		}

		if (!(clframe = ast_calloc(1, sizeof(*clframe)))) {
			ast_log(LOG_ERROR,
				"Unable to allocate channel lock frame.  %sLOCK will fail.\n",
				trylock ? "TRY" : "");
			AST_LIST_UNLOCK(list);
			return -1;
		}

		clframe->lock_frame = current;
		clframe->channel = chan;
		AST_LIST_INSERT_TAIL(list, clframe, list);
	}
	AST_LIST_UNLOCK(list);

	/* If we already own the lock, then we're being called recursively.
	 * Keep track of how many times that is, because we need to unlock
	 * the same amount, before we'll release this one.
	 */
	if (current->owner == chan) {
		current->count++;
		return 0;
	}

	/* Okay, we have both frames, so now we need to try to lock.
	 *
	 * Locking order: always lock locklist first.  We need the
	 * locklist lock because the broker thread counts whether
	 * there are requesters with the locklist lock held, and we
	 * need to hold it, so that when we send our signal, below,
	 * to wake up the broker thread, it definitely will see that
	 * a requester exists at that point in time.  Otherwise, we
	 * could add to the requesters after it has already seen that
	 * that lock is unoccupied and wait forever for another signal.
	 */
	AST_LIST_LOCK(&locklist);
	ast_mutex_lock(&current->mutex);
	/* Add to requester list */
	ao2_link(current->requesters, chan);
	pthread_kill(broker_tid, SIGURG);
	AST_LIST_UNLOCK(&locklist);

	/* Wait up to three seconds from now for LOCK. */
	now = ast_tvnow();
	timeout.tv_sec = now.tv_sec + 3;
	timeout.tv_nsec = now.tv_usec * 1000;

	if (!current->owner
		|| (!trylock
			&& !(res = ast_cond_timedwait(&current->cond, &current->mutex, &timeout)))) {
		res = 0;
		current->owner = chan;
		current->count++;
	} else {
		res = -1;
	}
	/* Remove from requester list */
	ao2_unlink(current->requesters, chan);
	ast_mutex_unlock(&current->mutex);

	return res;
}

static int unlock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *lock_store;
	struct channel_lock_frame *clframe;
	AST_LIST_HEAD(, channel_lock_frame) *list;

	if (!chan) {
		return -1;
	}

	lock_store = ast_channel_datastore_find(chan, &lock_info, NULL);
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
		if (clframe->lock_frame && clframe->lock_frame->owner == chan && strcmp(clframe->lock_frame->name, data) == 0) {
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

	if (--clframe->lock_frame->count == 0) {
		clframe->lock_frame->owner = NULL;
	}

	ast_copy_string(buf, "1", len);
	return 0;
}

static int lock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	if (!chan) {
		return -1;
	}
	ast_autoservice_start(chan);
	ast_copy_string(buf, get_lock(chan, data, 0) ? "0" : "1", len);
	ast_autoservice_stop(chan);

	return 0;
}

static int trylock_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	if (!chan) {
		return -1;
	}
	ast_autoservice_start(chan);
	ast_copy_string(buf, get_lock(chan, data, 1) ? "0" : "1", len);
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
		if (current->owner || ao2_container_count(current->requesters)) {
			/* Put it back */
			AST_LIST_INSERT_HEAD(&locklist, current, entries);
			AST_LIST_UNLOCK(&locklist);
			unloading = 0;
			return -1;
		}
		ast_mutex_destroy(&current->mutex);
		ao2_ref(current->requesters, -1);
		ast_free(current);
	}

	/* No locks left, unregister functions */
	ast_custom_function_unregister(&lock_function);
	ast_custom_function_unregister(&trylock_function);
	ast_custom_function_unregister(&unlock_function);

	if (broker_tid != AST_PTHREADT_NULL) {
		pthread_cancel(broker_tid);
		pthread_kill(broker_tid, SIGURG);
		pthread_join(broker_tid, NULL);
	}

	AST_LIST_UNLOCK(&locklist);

	return 0;
}

static int load_module(void)
{
	int res = ast_custom_function_register_escalating(&lock_function, AST_CFE_READ);
	res |= ast_custom_function_register_escalating(&trylock_function, AST_CFE_READ);
	res |= ast_custom_function_register_escalating(&unlock_function, AST_CFE_READ);

	if (ast_pthread_create_background(&broker_tid, NULL, lock_broker, NULL)) {
		ast_log(LOG_ERROR, "Failed to start lock broker thread. Unloading func_lock module.\n");
		broker_tid = AST_PTHREADT_NULL;
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialplan mutexes");
