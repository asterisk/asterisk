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

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/named_locks.h"
#include "asterisk/utils.h"

struct ao2_container *named_locks;
#define NAMED_LOCKS_BUCKETS 101

struct named_lock_proxy {
	AO2_WEAKPROXY();
	char key[0];
};

struct ast_named_lock {
};

AO2_STRING_FIELD_HASH_FN(named_lock_proxy, key)
AO2_STRING_FIELD_CMP_FN(named_lock_proxy, key)

static void named_locks_shutdown(void)
{
	ao2_cleanup(named_locks);
}

int ast_named_locks_init(void)
{
	named_locks = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NAMED_LOCKS_BUCKETS, named_lock_proxy_hash_fn, NULL, named_lock_proxy_cmp_fn);
	if (!named_locks) {
		return -1;
	}

	ast_register_cleanup(named_locks_shutdown);

	return 0;
}

static void named_lock_proxy_cb(void *weakproxy, void *data)
{
	ao2_unlink(named_locks, weakproxy);
}

struct ast_named_lock *__ast_named_lock_get(const char *filename, int lineno, const char *func,
	enum ast_named_lock_type lock_type, const char *keyspace, const char *key)
{
	struct named_lock_proxy *proxy;
	struct ast_named_lock *lock;
	int keylen = strlen(keyspace) + strlen(key) + 2;
	char *concat_key = ast_alloca(keylen);

	sprintf(concat_key, "%s-%s", keyspace, key); /* Safe */

	ao2_lock(named_locks);
	lock = __ao2_weakproxy_find(named_locks, concat_key, OBJ_SEARCH_KEY | OBJ_NOLOCK,
		__PRETTY_FUNCTION__, filename, lineno, func);
	if (lock) {
		ast_assert((ao2_options_get(lock) & AO2_ALLOC_OPT_LOCK_MASK) == lock_type);
		ao2_unlock(named_locks);

		return lock;
	}

	proxy = ao2_t_weakproxy_alloc(sizeof(*proxy) + keylen, NULL, concat_key);
	if (!proxy) {
		goto failure_cleanup;
	}

	lock = __ao2_alloc(sizeof(*lock) + keylen, NULL, lock_type, concat_key, filename, lineno, func);
	if (!lock) {
		goto failure_cleanup;
	}

	/* We have exclusive access to proxy and lock, no need for locking here. */
	if (ao2_weakproxy_set_object(proxy, lock, OBJ_NOLOCK)) {
		goto failure_cleanup;
	}

	if (ao2_weakproxy_subscribe(proxy, named_lock_proxy_cb, NULL, OBJ_NOLOCK)) {
		goto failure_cleanup;
	}

	strcpy(proxy->key, concat_key); /* Safe */
	ao2_link_flags(named_locks, proxy, OBJ_NOLOCK);
	ao2_unlock(named_locks);
	ao2_t_ref(proxy, -1, "Release allocation reference");

	return lock;

failure_cleanup:
	ao2_unlock(named_locks);

	ao2_cleanup(proxy);
	ao2_cleanup(lock);

	return NULL;
}
