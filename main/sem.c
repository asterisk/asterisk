/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Asterisk semaphore support.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/sem.h"
#include "asterisk/utils.h"

#ifndef HAS_WORKING_SEMAPHORE

/* DIY semaphores! */

int ast_sem_init(struct ast_sem *sem, int pshared, unsigned int value)
{
	if (pshared) {
		/* Don't need it... yet */
		errno = ENOSYS;
		return -1;
	}

	/* Since value is unsigned, this will also catch attempts to init with
	 * a negative value */
	if (value > AST_SEM_VALUE_MAX) {
		errno = EINVAL;
		return -1;
	}

	sem->count = value;
	sem->waiters = 0;
	ast_mutex_init(&sem->mutex);
	ast_cond_init(&sem->cond, NULL);
	return 0;
}

int ast_sem_destroy(struct ast_sem *sem)
{
	ast_mutex_destroy(&sem->mutex);
	ast_cond_destroy(&sem->cond);
	return 0;
}

int ast_sem_post(struct ast_sem *sem)
{
	SCOPED_MUTEX(lock, &sem->mutex);

	ast_assert(sem->count >= 0);

	if (sem->count == AST_SEM_VALUE_MAX) {
		errno = EOVERFLOW;
		return -1;
	}

	/* Give it up! */
	++sem->count;

	/* Release a waiter, if needed */
	if (sem->waiters) {
		ast_cond_signal(&sem->cond);
	}

	return 0;
}

int ast_sem_wait(struct ast_sem *sem)
{
	int res;
	SCOPED_MUTEX(lock, &sem->mutex);

	ast_assert(sem->count >= 0);

	/* Wait for a non-zero count */
	++sem->waiters;
	while (sem->count == 0) {
		res = ast_cond_wait(&sem->cond, &sem->mutex);
		/* Give up on error */
		if (res != 0) {
			--sem->waiters;
			return res;
		}
	}
	--sem->waiters;

	/* Take it! */
	--sem->count;

	return 0;
}

int ast_sem_timedwait(struct ast_sem *sem, const struct timespec *abs_timeout)
{
	int res;
	SCOPED_MUTEX(lock, &sem->mutex);

	ast_assert(sem->count >= 0);

	/* Wait for a non-zero count */
	++sem->waiters;
	while (sem->count == 0) {
		res = ast_cond_timedwait(&sem->cond, &sem->mutex, abs_timeout);
		/* Give up on error */
		if (res != 0) {
			--sem->waiters;
			return res;
		}
	}
	--sem->waiters;

	/* Take it! */
	--sem->count;

	return 0;
}

int ast_sem_getvalue(struct ast_sem *sem, int *sval)
{
	SCOPED_MUTEX(lock, &sem->mutex);

	ast_assert(sem->count >= 0);

	*sval = sem->count;

	return 0;
}

#endif
