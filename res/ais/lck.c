/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*! 
 * \file
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Usage of the SAForum AIS (Application Interface Specification)
 *
 * \arg http://www.openais.org/
 *
 * This file contains the code specific to the use of the LCK 
 * (Distributed Locks) Service.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ais.h"

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/channel.h"

SaLckHandleT lck_handle;

/*!
 * \brief Callbacks available in the Lock Service
 *
 * None of these are actually required if only synchronous locking is used.
 * However, some of them must be implemented should the asynchronous locks
 * be used.
 */
static SaLckCallbacksT lck_callbacks = {
	/*! Get notified when a cluster-wide lock gets created */
	.saLckResourceOpenCallback =   NULL,
	/*! Get notified when an asynchronous lock request gets granted */
	.saLckLockGrantCallback =      NULL,
	/*! Be informed when a currently held lock is blocking another node */
	.saLckLockWaiterCallback =     NULL,
	/*! Get notified when an asynchronous unlock request is done */
	.saLckResourceUnlockCallback = NULL,
};

enum lock_type {
	RDLOCK,
	WRLOCK,
	TRY_RDLOCK,
	TRY_WRLOCK,
};

#define LOCK_BUCKETS 101

/*!
 * Every thread that wants to use a distributed lock must open its own handle
 * to the lock.  So, a thread-local container of opened locks is used to keep
 * track of what locks have been opened.
 *
 * \todo It would be nice to be able to have a thread-local container, instead
 * of using a thread-local wrapper like this.
 */
struct lock_resources {
	struct ao2_container *locks;
};

static int lock_resources_init(void *);
static void lock_resources_destroy(void *);

AST_THREADSTORAGE_CUSTOM(locks_ts_key, 
	lock_resources_init, lock_resources_destroy);

struct lock_resource {
	SaLckResourceHandleT handle;
	SaLckLockIdT id;
	SaNameT ais_name;
	const char *name;
};

static int lock_hash_cb(const void *obj, int flags)
{
	const struct lock_resource *lock = obj;

	return ast_str_hash(lock->name);
}

static int lock_cmp_cb(void *obj, void *arg, int flags)
{
	struct lock_resource *lock1 = obj, *lock2 = arg;

	return !strcasecmp(lock1->name, lock2->name) ? CMP_MATCH : 0;
}

static int lock_resources_init(void *data)
{
	struct lock_resources *lock_resources = data;

	if (!(lock_resources->locks = ao2_container_alloc(LOCK_BUCKETS,
			lock_hash_cb, lock_cmp_cb))) {
		return -1;
	}

	return 0;

}

static void lock_resources_destroy(void *data)
{
	struct lock_resources *lock_resources = data;

	ao2_ref(lock_resources->locks, -1);

	ast_free(lock_resources);
}

static void lock_destructor(void *obj)
{
	struct lock_resource *lock = obj;

	if (lock->name)
		ast_free((void *) lock->name);
}

static inline struct lock_resource *lock_ref(struct lock_resource *lock)
{
	ao2_ref(lock, +1);
	return lock;
}

static inline struct lock_resource *lock_unref(struct lock_resource *lock)
{
	ao2_ref(lock, -1);
	return NULL;
}

static void lock_datastore_destroy(void *data)
{
	struct lock_resource *lock = data;
	SaAisErrorT ais_res;

	ais_res = saLckResourceUnlock(lock->id, SA_TIME_ONE_SECOND * 3);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error unlocking '%s': %s\n", lock->name, 
			ais_err2str(ais_res));
	}

	lock_unref(lock);
}

static struct lock_resource *find_lock(const char *name)
{
	struct lock_resource *lock, tmp_lock = {
		.name = name,
	};
	SaAisErrorT ais_res;
	struct lock_resources *lock_resources;

	if (!(lock_resources = ast_threadstorage_get(&locks_ts_key, 
		sizeof(*lock_resources)))) {
		return NULL;
	}

	/* Return the lock if it has already been opened by this thread */
	if ((lock = ao2_find(lock_resources->locks, &tmp_lock, OBJ_POINTER)))
		return lock;

	/* Allocate and open the lock */
	if (!(lock = ao2_alloc(sizeof(*lock), lock_destructor)))
		return NULL;

	if (!(lock->name = ast_strdup(name)))
		return lock_unref(lock);

	/* Map the name into the SaNameT for convenience */
	ast_copy_string((char *) lock->ais_name.value, lock->name,
		sizeof(lock->ais_name.value));
	lock->ais_name.length = strlen(lock->name);

	ais_res = saLckResourceOpen(lck_handle, &lock->ais_name,
		SA_LCK_RESOURCE_CREATE, SA_TIME_ONE_SECOND * 3, &lock->handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Failed to open lock: %s\n", ais_err2str(ais_res));
		return lock_unref(lock);
	}

	return lock;
}

const struct ast_datastore_info dlock_datastore_info = {
	.type = "DLOCK",
	.destroy = lock_datastore_destroy,
};

static void add_lock_to_chan(struct ast_channel *chan, struct lock_resource *lock,
	enum lock_type lock_type, double timeout, char *buf, size_t len)
{
	struct ast_datastore *datastore;
	SaAisErrorT ais_res;
	SaLckLockModeT mode = 0;
	SaLckLockFlagsT flags = 0;
	SaLckLockStatusT status;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &dlock_datastore_info, lock->name);

	if (datastore) {
		ast_log(LOG_ERROR, "The DLOCK '%s' is already locked by channel '%s'\n",
			lock->name, chan->name);
		ast_channel_unlock(chan);
		ast_copy_string(buf, "FAILURE", len);
		return;
	}
	ast_channel_unlock(chan);

	switch (lock_type) {
	case TRY_RDLOCK:
		flags = SA_LCK_LOCK_NO_QUEUE;
		mode = SA_LCK_PR_LOCK_MODE;
		break;
	case RDLOCK:
		flags = SA_LCK_LOCK_NO_QUEUE;
		mode = SA_LCK_PR_LOCK_MODE;
		break;
	case TRY_WRLOCK:
		flags = SA_LCK_LOCK_NO_QUEUE;
		mode = SA_LCK_EX_LOCK_MODE;
		break;
	case WRLOCK:
		flags = SA_LCK_LOCK_NO_QUEUE;
		mode = SA_LCK_EX_LOCK_MODE;
		break;
	}

	/* Actually acquire the lock now */
	ais_res = saLckResourceLock(lock->handle, &lock->id, mode, flags, 0,
		(SaTimeT) timeout * SA_TIME_ONE_SECOND, &status);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Problem acquiring lock '%s': %s\n",
			lock->name, ais_err2str(ais_res));
		ast_copy_string(buf, (ais_res == SA_AIS_ERR_TIMEOUT) ? "TIMEOUT" : 
			"FAILURE", len);
		return;
	}

	switch (status) {
	case SA_LCK_LOCK_GRANTED:
		ast_copy_string(buf, "SUCCESS", len);
		break;
	case SA_LCK_LOCK_DEADLOCK:
		ast_copy_string(buf, "DEADLOCK", len);
		return;
	/*! XXX \todo Need to look at handling these other cases in a different way */
	case SA_LCK_LOCK_NOT_QUEUED:
	case SA_LCK_LOCK_ORPHANED:
	case SA_LCK_LOCK_NO_MORE:
	case SA_LCK_LOCK_DUPLICATE_EX:
		ast_copy_string(buf, "FAILURE", len);
		return;
	}

	if (!(datastore = ast_channel_datastore_alloc(&dlock_datastore_info, 
		lock->name))) {
		ast_copy_string(buf, "FAILURE", len);
		return;
	}

	datastore->data = lock_ref(lock);

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
}

static int handle_lock(struct ast_channel *chan, enum lock_type lock_type,
	char *data, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(name);
		AST_APP_ARG(timeout);
	);
	int res = 0;
	double timeout = 3;
	struct lock_resource *lock = NULL;

	ast_autoservice_start(chan);

	AST_STANDARD_APP_ARGS(args, data);
	if (ast_strlen_zero(args.name)) {
		ast_log(LOG_ERROR, "The DLOCK functions require a lock name\n");
		res = -1;
		goto return_cleanup;
	}
	switch (lock_type) {
	case RDLOCK:
	case WRLOCK:
		if (!ast_strlen_zero(args.timeout) && ((timeout = atof(args.timeout)) < 0)) {
			ast_log(LOG_ERROR, "Timeout value '%s' not valid\n", args.timeout);
			res = -1;
			goto return_cleanup;
		}
		break;
	case TRY_RDLOCK:
	case TRY_WRLOCK:
		if (!ast_strlen_zero(args.timeout)) {
			ast_log(LOG_ERROR, "The trylock functions only take one argument\n");
			res = -1;
			goto return_cleanup;
		}
	}

	if (!(lock = find_lock(args.name))) {
		ast_copy_string(buf, "FAILURE", len);
		res = -1;
		goto return_cleanup;
	}

	add_lock_to_chan(chan, lock, lock_type, timeout, buf, len);

	lock = lock_unref(lock);

return_cleanup:
	ast_autoservice_stop(chan);

	return res;
}

static int handle_rdlock(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	return handle_lock(chan, RDLOCK, data, buf, len);
}

static int handle_wrlock(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	return handle_lock(chan, WRLOCK, data, buf, len);
}

static int handle_tryrdlock(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	return handle_lock(chan, TRY_RDLOCK, data, buf, len);
}

static int handle_trywrlock(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	return handle_lock(chan, TRY_WRLOCK, data, buf, len);
}

static int handle_unlock(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	struct ast_datastore *datastore;
	struct lock_resource *lock;
	SaAisErrorT ais_res;
	int res = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "DLOCK_UNLOCK requires a lock name\n");
		ast_copy_string(buf, "FAILURE", len);
		return -1;
	}

	ast_autoservice_start(chan);
	
	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &dlock_datastore_info, data);
	if (!datastore) {
		ast_log(LOG_ERROR, "The DLOCK '%s' is not locked by channel '%s'\n",
			data, chan->name);
		ast_channel_unlock(chan);
		ast_copy_string(buf, "FAILURE", len);
		return -1;
	}
	ast_channel_datastore_remove(chan, datastore);
	ast_channel_unlock(chan);

	lock = datastore->data;
	ais_res = saLckResourceUnlock(lock->id, SA_TIME_ONE_SECOND * 3);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Error unlocking '%s': %s\n", lock->name, 
			ais_err2str(ais_res));
		res = -1;
		ast_copy_string(buf, (ais_res == SA_AIS_ERR_TIMEOUT) ? "TIMEOUT" : 
			"FAILURE", len);
	} else {
		ast_copy_string(buf, "SUCCESS", len);
	}

	datastore->data = lock_unref(lock);
	ast_channel_datastore_free(datastore);

	ast_autoservice_stop(chan);

	return res;
}

#define LOCK_DESC_COMMON \
"  The name of the lock can be anything.  The first time a named lock gets\n" \
"used, it will be automatically created and maintained amongst the cluster.\n" \
"  The result of this function will be one of the following:\n" \
"     SUCCESS | TIMEOUT | FAILURE | DEADLOCK\n" DEADLOCK_DESC

#define DEADLOCK_DESC \
"  The result, DEADLOCK, can only be provided if the AIS implementation in\n" \
"use provides the optional feature of deadlock detection.  If the lock fails\n" \
"with the result of DEADLOCK, it means that the AIS implementation has\n" \
"determined that if this lock were acquired, it would cause a deadlock.\n"

static struct ast_custom_function dlock_rdlock = {
	.name = "DLOCK_RDLOCK",
	.synopsis = "Read-lock a distributed lock",
	.desc = 
"  This function will read-lock a distributed lock provided by the locking\n"
"service of AIS.  This is a blocking operation.  However, a timeout can be\n"
"specified to avoid deadlocks.  The default timeout used if one is not\n"
"provided as an argument is 3 seconds.\n"
LOCK_DESC_COMMON
"",
	.syntax = "DLOCK_RDLOCK(<lock_name>,[timeout])",
	.read = handle_rdlock,
};

static struct ast_custom_function dlock_wrlock = {
	.name = "DLOCK_WRLOCK",
	.synopsis = "Write-lock a distributed lock",
	.desc = 
"  This function will write-lock a distributed lock provided by the locking\n"
"service of AIS.  This is a blocking operation.  However, a timeout can be\n"
"specified to avoid deadlocks.  The default timeout used if one is not\n"
"provided as an argument is 3 seconds.\n"
LOCK_DESC_COMMON
"",
	.syntax = "DLOCK_WRLOCK(<lock_name>,[timeout])",
	.read = handle_wrlock,
};

static struct ast_custom_function dlock_tryrdlock = {
	.name = "DLOCK_TRYRDLOCK",
	.synopsis = "Try to read-lock a distributed lock",
	.desc =
"  This function will attempt to read-lock a distributed lock provided by the\n"
"locking service of AIS.  This is a non-blocking operation.\n"
"  The name of the lock can be anything.  The first time a named lock gets\n"
"used, it will be automatically created and maintained amongst the cluster.\n"
"  The result of this function will be one of the following:\n"
"     SUCCESS | FAILURE | DEADLOCK\n"
DEADLOCK_DESC
"",
	.syntax = "DLOCK_TRYRDLOCK(<lock_name>)",
	.read = handle_tryrdlock,
};

static struct ast_custom_function dlock_trywrlock = {
	.name = "DLOCK_TRYWRLOCK",
	.synopsis = "Try to write-lock a distributed lock",
	.desc =
"  This function will attempt to write-lock a distributed lock provided by\n"
"the locking service of AIS.  This is a non-blocking operation.\n"
"  The name of the lock can be anything.  The first time a named lock gets\n"
"used, it will be automatically created and maintained amongst the cluster.\n"
"  The result of this function will be one of the following:\n"
"     SUCCESS | FAILURE | DEADLOCK\n"
DEADLOCK_DESC
"",
	.syntax = "DLOCK_TRYWRLOCK(<lock_name>)",
	.read = handle_trywrlock,
};

static struct ast_custom_function dlock_unlock = {
	.name = "DLOCK_UNLOCK",
	.synopsis = "Unlock a distributed lock",
	.desc =
"  This function will unlock a currently held distributed lock.  This should\n"
"be used regardless of the lock was read or write locked.  The result of\n"
"this funtion will be one of the following:\n"
"      SUCCESS | TIMEOUT | FAILURE\n"
"",
	.syntax = "DLOCK_UNLOCK(<lock_name>)",
	.read = handle_unlock,
};

int ast_ais_lck_load_module(void)
{
	SaAisErrorT ais_res;
	int res;

	ais_res = saLckInitialize(&lck_handle, &lck_callbacks, &ais_version);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Could not initialize distributed locking service: %s\n",
			ais_err2str(ais_res));
		return -1;
	}

	res = ast_custom_function_register(&dlock_rdlock);
	res |= ast_custom_function_register(&dlock_wrlock);
	res |= ast_custom_function_register(&dlock_tryrdlock);
	res |= ast_custom_function_register(&dlock_trywrlock);
	res |= ast_custom_function_register(&dlock_unlock);

	return res;
}

int ast_ais_lck_unload_module(void)
{
	SaAisErrorT ais_res;
	int res = 0;

	ast_custom_function_unregister(&dlock_rdlock);
	ast_custom_function_unregister(&dlock_wrlock);
	ast_custom_function_unregister(&dlock_tryrdlock);
	ast_custom_function_unregister(&dlock_trywrlock);
	ast_custom_function_unregister(&dlock_unlock);

	ais_res = saLckFinalize(lck_handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Problem stopping distributed locking service: %s\n", 
			ais_err2str(ais_res));
		res = -1;
	}

	return res;
}
