/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Asterisk locking-related definitions:
 * - ast_mutext_t, ast_rwlock_t and related functions;
 * - atomic arithmetic instructions;
 * - wrappers for channel locking.
 *
 * - See \ref LockDef
 */

/*! \page LockDef Asterisk thread locking models
 *
 * This file provides different implementation of the functions,
 * depending on the platform, the use of DEBUG_THREADS, and the way
 * module-level mutexes are initialized.
 *
 *  - \b static: the mutex is assigned the value AST_MUTEX_INIT_VALUE
 *        this is done at compile time, and is the way used on Linux.
 *        This method is not applicable to all platforms e.g. when the
 *        initialization needs that some code is run.
 *
 *  - \b through constructors: for each mutex, a constructor function is
 *        defined, which then runs when the program (or the module)
 *        starts. The problem with this approach is that there is a
 *        lot of code duplication (a new block of code is created for
 *        each mutex). Also, it does not prevent a user from declaring
 *        a global mutex without going through the wrapper macros,
 *        so sane programming practices are still required.
 */

#ifndef _ASTERISK_LOCK_H
#define _ASTERISK_LOCK_H

#include <pthread.h>
#include <time.h>
#include <sys/param.h>
#ifdef HAVE_BKTR
#include <execinfo.h>
#endif

#ifndef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
#include "asterisk/time.h"
#endif

#include "asterisk/logger.h"
#include "asterisk/compiler.h"

#define AST_PTHREADT_NULL (pthread_t) -1
#define AST_PTHREADT_STOP (pthread_t) -2

#if (defined(SOLARIS) || defined(BSD))
#define AST_MUTEX_INIT_W_CONSTRUCTORS
#endif /* SOLARIS || BSD */

/* Asterisk REQUIRES recursive (not error checking) mutexes
   and will not run without them. */
#if defined(HAVE_PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP) && defined(HAVE_PTHREAD_MUTEX_RECURSIVE_NP)
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define AST_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INITIALIZER
#define AST_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE
#endif /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */

#ifdef HAVE_PTHREAD_RWLOCK_INITIALIZER
#define __AST_RWLOCK_INIT_VALUE		PTHREAD_RWLOCK_INITIALIZER
#else  /* HAVE_PTHREAD_RWLOCK_INITIALIZER */
#define __AST_RWLOCK_INIT_VALUE		{0}
#endif /* HAVE_PTHREAD_RWLOCK_INITIALIZER */

#ifdef HAVE_BKTR
#define AST_LOCK_TRACK_INIT_VALUE { { NULL }, { 0 }, 0, { NULL }, { 0 }, {{{ 0 }}}, PTHREAD_MUTEX_INIT_VALUE }
#else
#define AST_LOCK_TRACK_INIT_VALUE { { NULL }, { 0 }, 0, { NULL }, { 0 }, PTHREAD_MUTEX_INIT_VALUE }
#endif

#define AST_MUTEX_INIT_VALUE { AST_LOCK_TRACK_INIT_VALUE, 1, PTHREAD_MUTEX_INIT_VALUE }
#define AST_MUTEX_INIT_VALUE_NOTRACKING { AST_LOCK_TRACK_INIT_VALUE, 0, PTHREAD_MUTEX_INIT_VALUE }

#define AST_RWLOCK_INIT_VALUE { AST_LOCK_TRACK_INIT_VALUE, 1, __AST_RWLOCK_INIT_VALUE }
#define AST_RWLOCK_INIT_VALUE_NOTRACKING { AST_LOCK_TRACK_INIT_VALUE, 0, __AST_RWLOCK_INIT_VALUE }

#define AST_MAX_REENTRANCY 10

struct ast_channel;

struct ast_lock_track {
	const char *file[AST_MAX_REENTRANCY];
	int lineno[AST_MAX_REENTRANCY];
	int reentrancy;
	const char *func[AST_MAX_REENTRANCY];
	pthread_t thread[AST_MAX_REENTRANCY];
#ifdef HAVE_BKTR
	struct ast_bt backtrace[AST_MAX_REENTRANCY];
#endif
	pthread_mutex_t reentr_mutex;
};

/*! \brief Structure for mutex and tracking information.
 *
 * We have tracking information in this structure regardless of DEBUG_THREADS being enabled.
 * The information will just be ignored in the core if a module does not request it..
 */
struct ast_mutex_info {
	/*! Track which thread holds this mutex */
	struct ast_lock_track track;
	unsigned int tracking:1;
	pthread_mutex_t mutex;
};

/*! \brief Structure for rwlock and tracking information.
 *
 * We have tracking information in this structure regardless of DEBUG_THREADS being enabled.
 * The information will just be ignored in the core if a module does not request it..
 */
struct ast_rwlock_info {
	/*! Track which thread holds this lock */
	struct ast_lock_track track;
	unsigned int tracking:1;
	pthread_rwlock_t lock;
};

typedef struct ast_mutex_info ast_mutex_t;

typedef struct ast_rwlock_info ast_rwlock_t;

typedef pthread_cond_t ast_cond_t;

int __ast_pthread_mutex_init(int tracking, const char *filename, int lineno, const char *func, const char *mutex_name, ast_mutex_t *t);
int __ast_pthread_mutex_destroy(const char *filename, int lineno, const char *func, const char *mutex_name, ast_mutex_t *t);
int __ast_pthread_mutex_lock(const char *filename, int lineno, const char *func, const char* mutex_name, ast_mutex_t *t);
int __ast_pthread_mutex_trylock(const char *filename, int lineno, const char *func, const char* mutex_name, ast_mutex_t *t);
int __ast_pthread_mutex_unlock(const char *filename, int lineno, const char *func, const char *mutex_name, ast_mutex_t *t);

#define ast_mutex_init(pmutex)            __ast_pthread_mutex_init(1, __FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)
#define ast_mutex_init_notracking(pmutex) __ast_pthread_mutex_init(0, __FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)
#define ast_mutex_destroy(a)              __ast_pthread_mutex_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_mutex_lock(a)                 __ast_pthread_mutex_lock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_mutex_unlock(a)               __ast_pthread_mutex_unlock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_mutex_trylock(a)              __ast_pthread_mutex_trylock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)


int __ast_cond_init(const char *filename, int lineno, const char *func, const char *cond_name, ast_cond_t *cond, pthread_condattr_t *cond_attr);
int __ast_cond_signal(const char *filename, int lineno, const char *func, const char *cond_name, ast_cond_t *cond);
int __ast_cond_broadcast(const char *filename, int lineno, const char *func, const char *cond_name, ast_cond_t *cond);
int __ast_cond_destroy(const char *filename, int lineno, const char *func, const char *cond_name, ast_cond_t *cond);
int __ast_cond_wait(const char *filename, int lineno, const char *func, const char *cond_name, const char *mutex_name, ast_cond_t *cond, ast_mutex_t *t);
int __ast_cond_timedwait(const char *filename, int lineno, const char *func, const char *cond_name, const char *mutex_name, ast_cond_t *cond, ast_mutex_t *t, const struct timespec *abstime);

#define ast_cond_init(cond, attr)             __ast_cond_init(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond, attr)
#define ast_cond_destroy(cond)                __ast_cond_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond)
#define ast_cond_signal(cond)                 __ast_cond_signal(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond)
#define ast_cond_broadcast(cond)              __ast_cond_broadcast(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond)
#define ast_cond_wait(cond, mutex)            __ast_cond_wait(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex, cond, mutex)
#define ast_cond_timedwait(cond, mutex, time) __ast_cond_timedwait(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex, cond, mutex, time)


int __ast_rwlock_init(int tracking, const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *t);
int __ast_rwlock_destroy(const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *t);
int __ast_rwlock_unlock(ast_rwlock_t *t, const char *name, const char *filename, int line, const char *func);
int __ast_rwlock_rdlock(ast_rwlock_t *t, const char *name, const char *filename, int line, const char *func);
int __ast_rwlock_wrlock(ast_rwlock_t *t, const char *name, const char *filename, int line, const char *func);
int __ast_rwlock_timedrdlock(ast_rwlock_t *t, const char *name, const struct timespec *abs_timeout, const char *filename, int line, const char *func);
int __ast_rwlock_timedwrlock(ast_rwlock_t *t, const char *name, const struct timespec *abs_timeout, const char *filename, int line, const char *func);
int __ast_rwlock_tryrdlock(ast_rwlock_t *t, const char *name, const char *filename, int line, const char *func);
int __ast_rwlock_trywrlock(ast_rwlock_t *t, const char *name, const char *filename, int line, const char *func);

/*!
 * \brief wrapper for rwlock with tracking enabled
 * \return 0 on success, non zero for error
 * \since 1.6.1
 */
#define ast_rwlock_init(rwlock)            __ast_rwlock_init(1, __FILE__, __LINE__, __PRETTY_FUNCTION__, #rwlock, rwlock)

/*!
 * \brief wrapper for ast_rwlock_init with tracking disabled
 * \return 0 on success, non zero for error
 * \since 1.6.1
 */
#define ast_rwlock_init_notracking(rwlock) __ast_rwlock_init(0, __FILE__, __LINE__, __PRETTY_FUNCTION__, #rwlock, rwlock)

#define ast_rwlock_destroy(rwlock)         __ast_rwlock_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #rwlock, rwlock)
#define ast_rwlock_unlock(a)               __ast_rwlock_unlock(a, #a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_rwlock_rdlock(a)               __ast_rwlock_rdlock(a, #a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_rwlock_wrlock(a)               __ast_rwlock_wrlock(a, #a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_rwlock_tryrdlock(a)            __ast_rwlock_tryrdlock(a, #a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_rwlock_trywrlock(a)            __ast_rwlock_trywrlock(a, #a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_rwlock_timedrdlock(a, b)       __ast_rwlock_timedrdlock(a, #a, b, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_rwlock_timedwrlock(a, b)       __ast_rwlock_timedwrlock(a, #a, b, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define	ROFFSET	((lt->reentrancy > 0) ? (lt->reentrancy-1) : 0)

#ifdef DEBUG_THREADS

#define __ast_mutex_logger(...)  do { if (canlog) ast_log(LOG_ERROR, __VA_ARGS__); else fprintf(stderr, __VA_ARGS__); } while (0)

#ifdef THREAD_CRASH
#define DO_THREAD_CRASH do { *((int *)(0)) = 1; } while(0)
#else
#define DO_THREAD_CRASH do { } while (0)
#endif

#include <errno.h>

enum ast_lock_type {
	AST_MUTEX,
	AST_RDLOCK,
	AST_WRLOCK,
};

/*!
 * \brief Store lock info for the current thread
 *
 * This function gets called in ast_mutex_lock() and ast_mutex_trylock() so
 * that information about this lock can be stored in this thread's
 * lock info struct.  The lock is marked as pending as the thread is waiting
 * on the lock.  ast_mark_lock_acquired() will mark it as held by this thread.
 */
#if !defined(LOW_MEMORY)
#ifdef HAVE_BKTR
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
	int line_num, const char *func, const char *lock_name, void *lock_addr, struct ast_bt *bt);
#else
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
	int line_num, const char *func, const char *lock_name, void *lock_addr);
#endif /* HAVE_BKTR */

#else

#ifdef HAVE_BKTR
#define ast_store_lock_info(I,DONT,CARE,ABOUT,THE,PARAMETERS,BUD)
#else
#define ast_store_lock_info(I,DONT,CARE,ABOUT,THE,PARAMETERS)
#endif /* HAVE_BKTR */
#endif /* !defined(LOW_MEMORY) */

/*!
 * \brief Mark the last lock as acquired
 */
#if !defined(LOW_MEMORY)
void ast_mark_lock_acquired(void *lock_addr);
#else
#define ast_mark_lock_acquired(ignore)
#endif

/*!
 * \brief Mark the last lock as failed (trylock)
 */
#if !defined(LOW_MEMORY)
void ast_mark_lock_failed(void *lock_addr);
#else
#define ast_mark_lock_failed(ignore)
#endif

/*!
 * \brief remove lock info for the current thread
 *
 * this gets called by ast_mutex_unlock so that information on the lock can
 * be removed from the current thread's lock info struct.
 */
#if !defined(LOW_MEMORY)
#ifdef HAVE_BKTR
void ast_remove_lock_info(void *lock_addr, struct ast_bt *bt);
#else
void ast_remove_lock_info(void *lock_addr);
#endif /* HAVE_BKTR */
#else
#ifdef HAVE_BKTR
#define ast_remove_lock_info(ignore,me)
#else
#define ast_remove_lock_info(ignore)
#endif /* HAVE_BKTR */
#endif /* !defined(LOW_MEMORY) */

#ifdef HAVE_BKTR
static inline void __dump_backtrace(struct ast_bt *bt, int canlog)
{
	char **strings;

	ssize_t i;

	strings = backtrace_symbols(bt->addresses, bt->num_frames);

	for (i = 0; i < bt->num_frames; i++)
		__ast_mutex_logger("%s\n", strings[i]);

	free(strings);
}
#endif

/*!
 * \brief log info for the current lock with ast_log().
 *
 * this function would be mostly for debug. If you come across a lock
 * that is unexpectedly but momentarily locked, and you wonder who
 * are fighting with for the lock, this routine could be called, IF
 * you have the thread debugging stuff turned on.
 * \param this_lock_addr lock address to return lock information
 * \since 1.6.1
 */
void log_show_lock(void *this_lock_addr);

/*!
 * \brief retrieve lock info for the specified mutex
 *
 * this gets called during deadlock avoidance, so that the information may
 * be preserved as to what location originally acquired the lock.
 */
#if !defined(LOW_MEMORY)
int ast_find_lock_info(void *lock_addr, char *filename, size_t filename_size, int *lineno, char *func, size_t func_size, char *mutex_name, size_t mutex_name_size);
#else
#define ast_find_lock_info(a,b,c,d,e,f,g,h) -1
#endif

/*!
 * \brief Unlock a lock briefly
 *
 * used during deadlock avoidance, to preserve the original location where
 * a lock was originally acquired.
 */
#define CHANNEL_DEADLOCK_AVOIDANCE(chan) \
	do { \
		char __filename[80], __func[80], __mutex_name[80]; \
		int __lineno; \
		int __res = ast_find_lock_info(ao2_object_get_lockaddr(chan), __filename, sizeof(__filename), &__lineno, __func, sizeof(__func), __mutex_name, sizeof(__mutex_name)); \
		int __res2 = ast_channel_unlock(chan); \
		usleep(1); \
		if (__res < 0) { /* Shouldn't ever happen, but just in case... */ \
			if (__res2) { \
				ast_log(LOG_WARNING, "Could not unlock channel '%s': %s and no lock info found!  I will NOT try to relock.\n", #chan, strerror(__res2)); \
			} else { \
				ast_channel_lock(chan); \
			} \
		} else { \
			if (__res2) { \
				ast_log(LOG_WARNING, "Could not unlock channel '%s': %s.  {{{Originally locked at %s line %d: (%s) '%s'}}}  I will NOT try to relock.\n", #chan, strerror(__res2), __filename, __lineno, __func, __mutex_name); \
			} else { \
				__ao2_lock(chan, __filename, __func, __lineno, __mutex_name); \
			} \
		} \
	} while (0)

#define DEADLOCK_AVOIDANCE(lock) \
	do { \
		char __filename[80], __func[80], __mutex_name[80]; \
		int __lineno; \
		int __res = ast_find_lock_info(lock, __filename, sizeof(__filename), &__lineno, __func, sizeof(__func), __mutex_name, sizeof(__mutex_name)); \
		int __res2 = ast_mutex_unlock(lock); \
		usleep(1); \
		if (__res < 0) { /* Shouldn't ever happen, but just in case... */ \
			if (__res2 == 0) { \
				ast_mutex_lock(lock); \
			} else { \
				ast_log(LOG_WARNING, "Could not unlock mutex '%s': %s and no lock info found!  I will NOT try to relock.\n", #lock, strerror(__res2)); \
			} \
		} else { \
			if (__res2 == 0) { \
				__ast_pthread_mutex_lock(__filename, __lineno, __func, __mutex_name, lock); \
			} else { \
				ast_log(LOG_WARNING, "Could not unlock mutex '%s': %s.  {{{Originally locked at %s line %d: (%s) '%s'}}}  I will NOT try to relock.\n", #lock, strerror(__res2), __filename, __lineno, __func, __mutex_name); \
			} \
		} \
	} while (0)

/*!
 * \brief Deadlock avoidance unlock
 *
 * In certain deadlock avoidance scenarios, there is more than one lock to be
 * unlocked and relocked.  Therefore, this pair of macros is provided for that
 * purpose.  Note that every DLA_UNLOCK _MUST_ be paired with a matching
 * DLA_LOCK.  The intent of this pair of macros is to be used around another
 * set of deadlock avoidance code, mainly CHANNEL_DEADLOCK_AVOIDANCE, as the
 * locking order specifies that we may safely lock a channel, followed by its
 * pvt, with no worries about a deadlock.  In any other scenario, this macro
 * may not be safe to use.
 */
#define DLA_UNLOCK(lock) \
	do { \
		char __filename[80], __func[80], __mutex_name[80]; \
		int __lineno; \
		int __res = ast_find_lock_info(lock, __filename, sizeof(__filename), &__lineno, __func, sizeof(__func), __mutex_name, sizeof(__mutex_name)); \
		int __res2 = ast_mutex_unlock(lock);

/*!
 * \brief Deadlock avoidance lock
 *
 * In certain deadlock avoidance scenarios, there is more than one lock to be
 * unlocked and relocked.  Therefore, this pair of macros is provided for that
 * purpose.  Note that every DLA_UNLOCK _MUST_ be paired with a matching
 * DLA_LOCK.  The intent of this pair of macros is to be used around another
 * set of deadlock avoidance code, mainly CHANNEL_DEADLOCK_AVOIDANCE, as the
 * locking order specifies that we may safely lock a channel, followed by its
 * pvt, with no worries about a deadlock.  In any other scenario, this macro
 * may not be safe to use.
 */
#define DLA_LOCK(lock) \
		if (__res < 0) { /* Shouldn't ever happen, but just in case... */ \
			if (__res2) { \
				ast_log(LOG_WARNING, "Could not unlock mutex '%s': %s and no lock info found!  I will NOT try to relock.\n", #lock, strerror(__res2)); \
			} else { \
				ast_mutex_lock(lock); \
			} \
		} else { \
			if (__res2) { \
				ast_log(LOG_WARNING, "Could not unlock mutex '%s': %s.  {{{Originally locked at %s line %d: (%s) '%s'}}}  I will NOT try to relock.\n", #lock, strerror(__res2), __filename, __lineno, __func, __mutex_name); \
			} else { \
				__ast_pthread_mutex_lock(__filename, __lineno, __func, __mutex_name, lock); \
			} \
		} \
	} while (0)

static inline void ast_reentrancy_lock(struct ast_lock_track *lt)
{
	pthread_mutex_lock(&lt->reentr_mutex);
}

static inline void ast_reentrancy_unlock(struct ast_lock_track *lt)
{
	pthread_mutex_unlock(&lt->reentr_mutex);
}

static inline void ast_reentrancy_init(struct ast_lock_track *lt)
{
	int i;
	pthread_mutexattr_t reentr_attr;

	for (i = 0; i < AST_MAX_REENTRANCY; i++) {
		lt->file[i] = NULL;
		lt->lineno[i] = 0;
		lt->func[i] = NULL;
		lt->thread[i] = 0;
#ifdef HAVE_BKTR
		memset(&lt->backtrace[i], 0, sizeof(lt->backtrace[i]));
#endif
	}

	lt->reentrancy = 0;

	pthread_mutexattr_init(&reentr_attr);
	pthread_mutexattr_settype(&reentr_attr, AST_MUTEX_KIND);
	pthread_mutex_init(&lt->reentr_mutex, &reentr_attr);
	pthread_mutexattr_destroy(&reentr_attr);
}

static inline void delete_reentrancy_cs(struct ast_lock_track *lt)
{
	pthread_mutex_destroy(&lt->reentr_mutex);
}

#else /* !DEBUG_THREADS */

#define	CHANNEL_DEADLOCK_AVOIDANCE(chan) \
	ast_channel_unlock(chan); \
	usleep(1); \
	ast_channel_lock(chan);

#define	DEADLOCK_AVOIDANCE(lock) \
	do { \
		int __res; \
		if (!(__res = ast_mutex_unlock(lock))) { \
			usleep(1); \
			ast_mutex_lock(lock); \
		} else { \
			ast_log(LOG_WARNING, "Failed to unlock mutex '%s' (%s).  I will NOT try to relock. {{{ THIS IS A BUG. }}}\n", #lock, strerror(__res)); \
		} \
	} while (0)

#define DLA_UNLOCK(lock) ast_mutex_unlock(lock)

#define DLA_LOCK(lock) ast_mutex_lock(lock)

#endif /* !DEBUG_THREADS */

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
/*
 * If AST_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope constructors
 * and destructors to create/destroy global mutexes.
 */
#define __AST_MUTEX_DEFINE(scope, mutex, init_val, track)	\
	scope ast_mutex_t mutex = init_val;			\
static void  __attribute__((constructor)) init_##mutex(void)	\
{								\
	if (track)						\
		ast_mutex_init(&mutex);				\
	else							\
		ast_mutex_init_notracking(&mutex);		\
}								\
								\
static void  __attribute__((destructor)) fini_##mutex(void)	\
{								\
	ast_mutex_destroy(&mutex);				\
}
#else /* !AST_MUTEX_INIT_W_CONSTRUCTORS */
/* By default, use static initialization of mutexes. */
#define __AST_MUTEX_DEFINE(scope, mutex, init_val, track) scope ast_mutex_t mutex = init_val
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

#define AST_MUTEX_DEFINE_STATIC(mutex) __AST_MUTEX_DEFINE(static, mutex, AST_MUTEX_INIT_VALUE, 1)
#define AST_MUTEX_DEFINE_STATIC_NOTRACKING(mutex) __AST_MUTEX_DEFINE(static, mutex, AST_MUTEX_INIT_VALUE_NOTRACKING, 0)


/* Statically declared read/write locks */
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
#define __AST_RWLOCK_DEFINE(scope, rwlock, init_val, track) \
        scope ast_rwlock_t rwlock = init_val; \
static void  __attribute__((constructor)) init_##rwlock(void) \
{ \
	if (track) \
        	ast_rwlock_init(&rwlock); \
	else \
		ast_rwlock_init_notracking(&rwlock); \
} \
static void  __attribute__((destructor)) fini_##rwlock(void) \
{ \
        ast_rwlock_destroy(&rwlock); \
}
#else
#define __AST_RWLOCK_DEFINE(scope, rwlock, init_val, track) scope ast_rwlock_t rwlock = init_val
#endif

#define AST_RWLOCK_DEFINE_STATIC(rwlock) __AST_RWLOCK_DEFINE(static, rwlock, AST_RWLOCK_INIT_VALUE, 1)
#define AST_RWLOCK_DEFINE_STATIC_NOTRACKING(rwlock) __AST_RWLOCK_DEFINE(static, rwlock, AST_RWLOCK_INIT_VALUE_NOTRACKING, 0)

#ifndef __CYGWIN__	/* temporary disabled for cygwin */
#define pthread_mutex_t		use_ast_mutex_t_instead_of_pthread_mutex_t
#define pthread_cond_t		use_ast_cond_t_instead_of_pthread_cond_t
#endif
#define pthread_mutex_lock	use_ast_mutex_lock_instead_of_pthread_mutex_lock
#define pthread_mutex_unlock	use_ast_mutex_unlock_instead_of_pthread_mutex_unlock
#define pthread_mutex_trylock	use_ast_mutex_trylock_instead_of_pthread_mutex_trylock
#define pthread_mutex_init	use_ast_mutex_init_instead_of_pthread_mutex_init
#define pthread_mutex_destroy	use_ast_mutex_destroy_instead_of_pthread_mutex_destroy
#define pthread_cond_init	use_ast_cond_init_instead_of_pthread_cond_init
#define pthread_cond_destroy	use_ast_cond_destroy_instead_of_pthread_cond_destroy
#define pthread_cond_signal	use_ast_cond_signal_instead_of_pthread_cond_signal
#define pthread_cond_broadcast	use_ast_cond_broadcast_instead_of_pthread_cond_broadcast
#define pthread_cond_wait	use_ast_cond_wait_instead_of_pthread_cond_wait
#define pthread_cond_timedwait	use_ast_cond_timedwait_instead_of_pthread_cond_timedwait

#define AST_MUTEX_INITIALIZER __use_AST_MUTEX_DEFINE_STATIC_rather_than_AST_MUTEX_INITIALIZER__

#define gethostbyname __gethostbyname__is__not__reentrant__use__ast_gethostbyname__instead__

#ifndef __linux__
#define pthread_create __use_ast_pthread_create_instead__
#endif

/*
 * Support for atomic instructions.
 * For platforms that have it, use the native cpu instruction to
 * implement them. For other platforms, resort to a 'slow' version
 * (defined in utils.c) that protects the atomic instruction with
 * a single lock.
 * The slow versions is always available, for testing purposes,
 * as ast_atomic_fetchadd_int_slow()
 */

int ast_atomic_fetchadd_int_slow(volatile int *p, int v);

#include "asterisk/inline_api.h"

#if defined(HAVE_OSX_ATOMICS)
#include "libkern/OSAtomic.h"
#endif

/*! \brief Atomically add v to *p and return * the previous value of *p.
 * This can be used to handle reference counts, and the return value
 * can be used to generate unique identifiers.
 */

#if defined(HAVE_GCC_ATOMICS)
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	return __sync_fetch_and_add(p, v);
})
#elif defined(HAVE_OSX_ATOMICS) && (SIZEOF_INT == 4)
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	return OSAtomicAdd32(v, (int32_t *) p) - v;
})
#elif defined(HAVE_OSX_ATOMICS) && (SIZEOF_INT == 8)
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	return OSAtomicAdd64(v, (int64_t *) p) - v;
#elif defined (__i386__) || defined(__x86_64__)
#ifdef sun
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	__asm __volatile (
	"       lock;  xaddl   %0, %1 ;        "
	: "+r" (v),                     /* 0 (result) */
	  "=m" (*p)                     /* 1 */
	: "m" (*p));                    /* 2 */
	return (v);
})
#else /* ifndef sun */
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	__asm __volatile (
	"       lock   xaddl   %0, %1 ;        "
	: "+r" (v),                     /* 0 (result) */
	  "=m" (*p)                     /* 1 */
	: "m" (*p));                    /* 2 */
	return (v);
})
#endif
#else   /* low performance version in utils.c */
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	return ast_atomic_fetchadd_int_slow(p, v);
})
#endif

/*! \brief decrement *p by 1 and return true if the variable has reached 0.
 * Useful e.g. to check if a refcount has reached 0.
 */
#if defined(HAVE_GCC_ATOMICS)
AST_INLINE_API(int ast_atomic_dec_and_test(volatile int *p),
{
	return __sync_sub_and_fetch(p, 1) == 0;
})
#elif defined(HAVE_OSX_ATOMICS) && (SIZEOF_INT == 4)
AST_INLINE_API(int ast_atomic_dec_and_test(volatile int *p),
{
	return OSAtomicAdd32( -1, (int32_t *) p) == 0;
})
#elif defined(HAVE_OSX_ATOMICS) && (SIZEOF_INT == 8)
AST_INLINE_API(int ast_atomic_dec_and_test(volatile int *p),
{
	return OSAtomicAdd64( -1, (int64_t *) p) == 0;
#else
AST_INLINE_API(int ast_atomic_dec_and_test(volatile int *p),
{
	int a = ast_atomic_fetchadd_int(p, -1);
	return a == 1; /* true if the value is 0 now (so it was 1 previously) */
})
#endif

#endif /* _ASTERISK_LOCK_H */
