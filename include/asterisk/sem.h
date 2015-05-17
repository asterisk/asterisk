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

#ifndef ASTERISK_SEMAPHORE_H
#define ASTERISK_SEMAPHORE_H

/*!
 * \file
 *
 * \brief Asterisk semaphore API
 *
 * This API is a thin wrapper around the POSIX semaphore API (when available),
 * so see the POSIX documentation for further details.
 */

#ifdef HAS_WORKING_SEMAPHORE
/* Working semaphore implementation detected */

#include <semaphore.h>

struct ast_sem {
	sem_t real_sem;
};

#define AST_SEM_VALUE_MAX SEM_VALUE_MAX

/* These are thin wrappers; might as well inline them */

static force_inline int ast_sem_init(struct ast_sem *sem, int pshared, unsigned int value)
{
	return sem_init(&sem->real_sem, pshared, value);
}

static force_inline int ast_sem_destroy(struct ast_sem *sem)
{
	return sem_destroy(&sem->real_sem);
}

static force_inline int ast_sem_post(struct ast_sem *sem)
{
	return sem_post(&sem->real_sem);
}

static force_inline int ast_sem_wait(struct ast_sem *sem)
{
	return sem_wait(&sem->real_sem);
}

static force_inline int ast_sem_timedwait(struct ast_sem *sem, const struct timespec *abs_timeout)
{
	return sem_timedwait(&sem->real_sem, abs_timeout);
}

static force_inline int ast_sem_getvalue(struct ast_sem *sem, int *sval)
{
	return sem_getvalue(&sem->real_sem, sval);
}

#else
/* Unnamed semaphores don't work. Rolling our own, I guess... */

#include "asterisk/lock.h"

#include <limits.h>

struct ast_sem {
	/*! Current count of this semaphore */
	int count;
	/*! Number of threads currently waiting for this semaphore */
	int waiters;
	/*! Mutual exclusion */
	ast_mutex_t mutex;
	/*! Condition for singalling waiters */
	ast_cond_t cond;
};

#define AST_SEM_VALUE_MAX INT_MAX

/*!
 * \brief Initialize a semaphore.
 *
 * \param sem Semaphore to initialize.
 * \param pshared Pass true (nonzero) to share this thread between processes.
 *                Not be supported on all platforms, so be wary!
 *                But leave the parameter, to be compatible with the POSIX ABI
 *                in case we need to add support in the future.
 * \param value Initial value of the semaphore.
 *
 * \return 0 on success.
 * \return -1 on error, errno set to indicate error.
 */
int ast_sem_init(struct ast_sem *sem, int pshared, unsigned int value);

/*!
 * \brief Destroy a semaphore.
 *
 * Destroying a semaphore that other threads are currently blocked on produces
 * undefined behavior.
 *
 * \param sem Semaphore to destroy.
 *
 * \return 0 on success.
 * \return -1 on error, errno set to indicate error.
 */
int ast_sem_destroy(struct ast_sem *sem);

/*!
 * \brief Increments the semaphore, unblocking a waiter if necessary.
 *
 * \param sem Semaphore to increment.
 *
 * \return 0 on success.
 * \return -1 on error, errno set to indicate error.
 */
int ast_sem_post(struct ast_sem *sem);

/*!
 * \brief Decrements the semaphore.
 *
 * If the semaphore's current value is zero, this function blocks until another
 * thread posts (ast_sem_post()) to the semaphore (or is interrupted by a signal
 * handler, which sets errno to EINTR).
 *
 * \param sem Semaphore to decrement.
 *
 * \return 0 on success.
 * \return -1 on error, errno set to indicate error.
 */
int ast_sem_wait(struct ast_sem *sem);

/*!
 * \brief Decrements the semaphore, waiting until abs_timeout.
 *
 * If the semaphore's current value is zero, this function blocks until another
 * thread posts (ast_sem_post()) to the semaphore (or is interrupted by a signal
 * handler, which sets errno to EINTR).
 *
 * \param sem Semaphore to decrement.
 *
 * \return 0 on success.
 * \return -1 on error, errno set to indicate error.
 */
int ast_sem_timedwait(struct ast_sem *sem, const struct timespec *abs_timeout);

/*!
 * \brief Gets the current value of the semaphore.
 *
 * If threads are blocked on this semaphore, POSIX allows the return value to be
 * either 0 or a negative number whose absolute value is the number of threads
 * blocked. Don't assume that it will give you one or the other; Asterisk has
 * been ported to just about everything.
 *
 * \param sem Semaphore to query.
 * \param[out] sval Output value.
 *
 * \return 0 on success.
 * \return -1 on error, errno set to indicate error.
 */
int ast_sem_getvalue(struct ast_sem *sem, int *sval);

#endif

#endif /* ASTERISK_SEMAPHORE_H */
