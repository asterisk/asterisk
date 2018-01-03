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

AO2_STRING_FIELD_HASH_FN(ast_named_lock, key)
AO2_STRING_FIELD_CMP_FN(ast_named_lock, key)

static void named_locks_shutdown(void)
{
	ao2_cleanup(named_locks);
}

int ast_named_locks_init(void)
{
	named_locks = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NAMED_LOCKS_BUCKETS, ast_named_lock_hash_fn, NULL, ast_named_lock_cmp_fn);
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
