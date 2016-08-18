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

#ifndef INCLUDE_ASTERISK_NAMED_LOCKS_H_
#define INCLUDE_ASTERISK_NAMED_LOCKS_H_

#include "asterisk/astobj2.h"

/*!
 * \defgroup named_locks Named mutex and read-write locks
 * @{
 * \page NamedLocks Named mutex and read-write locks
 * \since 13.9.0
 *
 * Locking some objects like sorcery objects can be tricky because the underlying
 * ao2 object may not be the same for all callers.  For instance, two threads that
 * call ast_sorcery_retrieve_by_id on the same aor name might actually get 2 different
 * ao2 objects if the underlying wizard had to rehydrate the aor from a database.
 * Locking one ao2 object doesn't have any effect on the other even if those objects
 * had locks in the first place
 *
 * Named locks allow access control by name.  Now an aor named "1000" can be locked and
 * any other thread attempting to lock the aor named "1000" will wait regardless of whether
 * the underlying ao2 object is the same or not.
 *
 * To use a named lock:
 * 	Call ast_named_lock_get with the appropriate keyspace and key.
 * 	Use the standard ao2 lock/unlock functions as needed.
 * 	Call ao2_cleanup when you're finished with it.
 */

/*!
 * \brief Which type of lock to request.
 */
enum ast_named_lock_type {
	/*! Request a named mutex. */
	AST_NAMED_LOCK_TYPE_MUTEX = AO2_ALLOC_OPT_LOCK_MUTEX,
	/*! Request a named read/write lock. */
	AST_NAMED_LOCK_TYPE_RWLOCK = AO2_ALLOC_OPT_LOCK_RWLOCK,
};

struct ast_named_lock;

struct ast_named_lock *__ast_named_lock_get(const char *filename, int lineno, const char *func,
	enum ast_named_lock_type lock_type, const char *keyspace, const char *key);

/*!
 * \brief Geta named lock handle
 * \since 13.9.0
 *
 * \param lock_type One of ast_named_lock_type
 * \param keyspace
 * \param key
 * \retval A pointer to an ast_named_lock structure
 * \retval NULL on error
 *
 * \note
 * keyspace and key can be anything.  For sorcery objects, keyspace could be the object type
 * and key could be the object id.
 */
#define ast_named_lock_get(lock_type, keyspace, key) \
	__ast_named_lock_get(__FILE__, __LINE__, __PRETTY_FUNCTION__, lock_type, \
		keyspace, key)

/*!
 * \brief Put a named lock handle away
 * \since 13.9.0
 *
 * \param lock The pointer to the ast_named_lock structure returned by ast_named_lock_get
 */
#define ast_named_lock_put(lock) ao2_cleanup(lock)

/*!
 * @}
 */

#endif /* INCLUDE_ASTERISK_NAMED_LOCKS_H_ */
