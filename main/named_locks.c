/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Named Locks
 *
 * \author George Joseph <george.joseph@fairview5.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/named_locks.h"
#include "asterisk/utils.h"

struct ao2_container *named_locks;
#define NAMED_LOCKS_BUCKETS 101

struct ast_named_lock {
	char key[0];
};

static int named_locks_hash(const void *obj, const int flags)
{
	const struct ast_named_lock *lock = obj;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		return ast_str_hash(obj);
	case OBJ_SEARCH_OBJECT:
		return ast_str_hash(lock->key);
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int named_locks_cmp(void *obj_left, void *obj_right, int flags)
{
	const struct ast_named_lock *object_left = obj_left;
	const struct ast_named_lock *object_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->key;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->key, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(object_left->key, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp ? 0 : CMP_MATCH;
}

static void named_locks_shutdown(void)
{
	ao2_cleanup(named_locks);
}

int ast_named_locks_init(void)
{
	named_locks = ao2_container_alloc_hash(0, 0, NAMED_LOCKS_BUCKETS, named_locks_hash, NULL,
		named_locks_cmp);
	if (!named_locks) {
		return -1;
	}

	ast_register_cleanup(named_locks_shutdown);

	return 0;
}

struct ast_named_lock *__ast_named_lock_get(const char *filename, int lineno, const char *func,
	enum ast_named_lock_type lock_type, const char *keyspace, const char *key)
{
	struct ast_named_lock *lock = NULL;
	int concat_key_buff_len = strlen(keyspace) + strlen(key) + 2;
	char *concat_key = ast_alloca(concat_key_buff_len);

	sprintf(concat_key, "%s-%s", keyspace, key); /* Safe */

	ao2_lock(named_locks);
	lock = ao2_find(named_locks, concat_key, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (lock) {
		ao2_unlock(named_locks);
		ast_assert((ao2_options_get(lock) & AO2_ALLOC_OPT_LOCK_MASK) == lock_type);
		return lock;
	}

	lock = ao2_alloc_options(sizeof(*lock) + concat_key_buff_len, NULL, lock_type);
	if (lock) {
		strcpy(lock->key, concat_key); /* Safe */
		ao2_link_flags(named_locks, lock, OBJ_NOLOCK);
	}
	ao2_unlock(named_locks);

	return lock;
}

int __ast_named_lock_put(const char *filename, int lineno, const char *func,
	struct ast_named_lock *lock)
{
	if (!lock) {
		return -1;
	}

	ao2_lock(named_locks);
	if (ao2_ref(lock, -1) == 2) {
		ao2_unlink_flags(named_locks, lock, OBJ_NOLOCK);
	}
	ao2_unlock(named_locks);

	return 0;
}
