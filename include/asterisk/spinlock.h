/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Fairview 5 Engineering, LLC
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
 * \brief Spin Locks.
 *
 * In some atomic operation circumstances the __atomic calls are not quite
 * flexible enough but a full fledged mutex or rwlock is too expensive.
 *
 * Spin locks should be used only for protecting short blocks of critical
 * code such as simple compares and assignments.  Operations that may block,
 * hold a lock, or cause the thread to give up it's timeslice should NEVER
 * be attempted in a spin lock.
 *
 * Because spinlocks must be as lightweight as possible, there are no
 * recursion or deadlock checks.
 *
 */

#ifndef _ASTERISK_SPINLOCK_H
#define _ASTERISK_SPINLOCK_H

#include <pthread.h>
#include "asterisk/compiler.h"

/*!
 * \brief Spinlock Implementation Types
 *
 * Not all implementations will be available on all platforms.
 *
 */
enum ast_spinlock_type {
	AST_SPINLOCK_TYPE_GCC_ATOMICS,
	AST_SPINLOCK_TYPE_GAS_X86,
	AST_SPINLOCK_TYPE_GAS_ARM,
	AST_SPINLOCK_TYPE_GAS_SPARC,
	AST_SPINLOCK_TYPE_OSX_ATOMICS,
	AST_SPINLOCK_TYPE_PTHREAD_SPINLOCK,
	AST_SPINLOCK_TYPE_PTHREAD_MUTEX,
};

/*!
 * \brief Implementation using GCC Atomics
 *
 * Specifically, __sync_lock_test_and_set is used to atomically
 * set/unset the lock variable.
 *
 * Most recent gcc implementations support this method and it's performance
 * is equal to or better than the assembly implementations hence it is the
 * most preferred implementation on all platforms.
 *
 */
#ifdef HAVE_GCC_ATOMICS
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_GCC_ATOMICS
#define AST_SPINLOCK_TYPE_LABEL "gcc_atomics"
typedef volatile unsigned int ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	*lock = 0;
	return 0;
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	while (__sync_lock_test_and_set(lock, 1)) {
		while(*lock) {
		}
	}
	return 0;
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	return __sync_lock_test_and_set(lock, 1);
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	__sync_lock_release(lock);
	return 0;
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return 0;
}
#endif

/*!
 * \brief Implementation using x86 Assembly
 *
 * For x86 implementations that don't support gcc atomics,
 * this is the next best method.
 *
 */
#if (defined(__x86_64__) || defined(__i386__)) && !defined(AST_SPINLOCK_TYPE)
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_GAS_X86
#define AST_SPINLOCK_TYPE_LABEL "gas_x86"
typedef volatile unsigned int ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	*lock = 0;
	return 0;
}

static force_inline int x86chgl(ast_spinlock_t *p, unsigned int v)
{
	__asm __volatile (
		"	xchg   %0, %1 ;"
		: "+r" (v), "=m" (*p)
		: "m" (*p)
	);

	return (v);
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	while (x86chgl(lock, 1)) {
		while(*lock) {
		}
	}
	return 0;
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	return x86chgl(lock, 1);
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	x86chgl(lock, 0);
	return 0;
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return 0;
}
#endif

/*!
 * \brief Implementation using ARM Assembly
 *
 * For ARM implementations that don't support gcc atomics,
 * this is the next best method.
 *
 */
#if defined(__arm__) && !defined(AST_SPINLOCK_TYPE)
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_GAS_ARM
#define AST_SPINLOCK_TYPE_LABEL "gas_arm"
typedef volatile unsigned int ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	*lock = 0;
	return 0;
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	unsigned int tmp;

	__asm __volatile (
		"1:	ldrex %[tmp], %[lock];"
		"	teq %[tmp], #0;"
#if defined __ARM_ARCH && __ARM_ARCH >= 7
		"	wfene;"
#endif
		"	strexeq %[tmp], %[c1], %[lock];"
		"	teqeq %[tmp], #0;"
		"	bne 1b;"
		: [tmp] "=&r" (tmp)
		: [lock] "m" (*lock) [c1] "r" (1)
		: "cc"
	);

	return tmp;
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	unsigned int tmp;

	__asm __volatile (
		"	ldrex %[tmp], %[lock];"
		"	teq %[tmp], #0;"
#if defined __ARM_ARCH && __ARM_ARCH >= 7
		"	wfene;"
#endif
		"	strexeq %[tmp], %[c1], %[lock];"
		: [tmp] "=&r" (tmp)
		: [lock] "m" (*lock) [c1] "r" (1)
		: "cc"
	);

	return tmp;
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	__asm __volatile (
		"	dmb;"
		"	str %[c0], %[lock];"
#if defined __ARM_ARCH && __ARM_ARCH >= 7
		"	dsb;"
		"	sev;"
#endif
		:
		: [lock] "m" (*lock) [c0] "r" (0)
		: "cc"
	);

	return 0;
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return 0;
}
#endif

/*!
 * \brief Implementation using Sparc Assembly
 *
 * For Sparc implementations that don't support gcc atomics,
 * this is the next best method.
 *
 */
#if defined(__sparc__) && !defined(AST_SPINLOCK_TYPE)
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_GAS_SPARC
#define AST_SPINLOCK_TYPE_LABEL "gas_sparc"
typedef volatile unsigned char ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	*lock = 0;
	return 0;
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	unsigned char tmp;

	__asm__ __volatile__(
		"1:	ldstub		%[lock], %[tmp]\n"
		"	brnz,pn		%[tmp], 2f\n"
		"	 nop\n"
		"	.subsection	2\n"
		"2:	ldub		%[lock], %[tmp]\n"
		"	brnz,pt		%[tmp], 2b\n"
		"	 nop\n"
		"	ba,a,pt		%%xcc, 1b\n"
		"	.previous"
		: [tmp] "=&r" (tmp)
		: [lock] "m" (*lock)
		: "memory"
	);

	return 0;
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	unsigned long result = 1;

	__asm__ __volatile__(
		"	ldstub		%[lock], %[result]\n"
		: [result] "=&r" (result)
		: [lock] "m" (*lock)
		: "memory", "cc"
	);

	return (result != 0);
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	__asm__ __volatile__(
		"	stb		%%g0, %[lock]"
		:
		: [lock] "m" (*lock)
		: "memory", "cc"
	);

	return 0;
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return 0;
}
#endif

/*!
 * \brief Implementation using pthread_spinlock
 *
 * pthread_spinlocks are not supported on all platforms
 * but if for some reason none of the previous implementations are
 * available, it can be used with reasonable performance.
 *
 */
#if defined (HAVE_PTHREAD_SPINLOCK) && !defined(AST_SPINLOCK_TYPE)
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_PTHREAD_SPINLOCK
#define AST_SPINLOCK_TYPE_LABEL "pthread_spinlock"
typedef pthread_spinlock_t ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	return pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE);
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	return pthread_spin_lock(lock);
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	return pthread_spin_trylock(lock);
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	return pthread_spin_unlock(lock);
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return pthread_spin_destroy(lock);
}
#endif

/*!
 * \brief Implementation using OSX Atomics
 *
 * The Darwin/Mac OSX platform has its own atomics
 * implementation but it uses more kernel time than
 * GCC atomics and x86 assembly.  It is included
 * as an unlikely fallback.
 *
 */
#if defined(HAVE_OSX_ATOMICS) && !defined(AST_SPINLOCK_TYPE)
#include <libkern/OSAtomic.h>
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_OSX_ATOMICS
#define AST_SPINLOCK_TYPE_LABEL "osx_atomics"
typedef OSSpinLock ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	*lock = OS_SPINLOCK_INIT;
	return 0;
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	OSSpinLockLock(lock);
	return 0;
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	return !OSSpinLockTry(lock);
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	OSSpinLockUnlock(lock);
	return 0;
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return 0;
}
#endif

/*!
 * \brief Implementation using pthread_mutex
 *
 * pthread_mutex is supported on all platforms but
 * it is also the worst performing. It is included
 * as an unlikely fallback.
 *
 */
#if !defined(AST_SPINLOCK_TYPE)
#define AST_SPINLOCK_TYPE AST_SPINLOCK_TYPE_PTHREAD_MUTEX
#define AST_SPINLOCK_TYPE_LABEL "pthread_mutex"
typedef pthread_mutex_t ast_spinlock_t;

static force_inline int ast_spinlock_init(ast_spinlock_t *lock)
{
	pthread_mutex_init(lock, NULL);
	return 0;
}

static force_inline int ast_spinlock_lock(ast_spinlock_t *lock)
{
	return pthread_mutex_lock(lock);
}

static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock)
{
	return pthread_mutex_trylock(lock);
}

static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock)
{
	return pthread_mutex_unlock(lock);
}

static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock)
{
	return pthread_mutex_destroy(lock);
}
#endif

#if !defined(AST_SPINLOCK_TYPE)
#error "No spinlock implementation could be found."
#endif

/* Prototypes are declared here to insure that each implementation provides
 * the same API and to act as placeholders for the documentation.
 */

/*!
 * \brief Initialize a spin lock
 * \param lock Address of the lock
 * \retval 0 Success
 * \retval other Failure
 */
static force_inline int ast_spinlock_init(ast_spinlock_t *lock);

/*!
 * \brief Lock a spin lock
 * \param lock Address of the lock
 * \retval 0 Success
 * \retval other Failure
 */
static force_inline int ast_spinlock_lock(ast_spinlock_t *lock);

/*!
 * \brief Try to lock a spin lock
 *
 * Attempt to gain a lock.  Return immediately
 * regardless of result.
 *
 * \param lock Address of the lock
 * \retval 0 Success
 * \retval other Lock was not obtained
 */
static force_inline int ast_spinlock_trylock(ast_spinlock_t *lock);

/*!
 * \brief Unlock a spin lock
 * \param lock Address of the lock
 * \retval 0 Success
 * \retval other Failure
 */
static force_inline int ast_spinlock_unlock(ast_spinlock_t *lock);

/*!
 * \brief Destroy a spin lock
 * \param lock Address of the lock
 * \retval 0 Success
 * \retval other Failure
 */
static force_inline int ast_spinlock_destroy(ast_spinlock_t *lock);

#endif /* _ASTERISK_SPINLOCK_H */
