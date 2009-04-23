/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief General Asterisk channel locking definitions.
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
#include <netdb.h>
#include <time.h>
#include <sys/param.h>
#include <unistd.h>

#ifndef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
#include "asterisk/time.h"
#endif
#include "asterisk/logger.h"

/* internal macro to profile mutexes. Only computes the delay on
 * non-blocking calls.
 */
#ifndef	HAVE_MTX_PROFILE
#define	__MTX_PROF(a)	return pthread_mutex_lock((a))
#else
#define	__MTX_PROF(a)	do {			\
	int i;					\
	/* profile only non-blocking events */	\
	ast_mark(mtx_prof, 1);			\
	i = pthread_mutex_trylock((a));		\
	ast_mark(mtx_prof, 0);			\
	if (!i)					\
		return i;			\
	else					\
		return pthread_mutex_lock((a)); \
	} while (0)
#endif	/* HAVE_MTX_PROFILE */

#define AST_PTHREADT_NULL (pthread_t) -1
#define AST_PTHREADT_STOP (pthread_t) -2

#if defined(SOLARIS) || defined(BSD)
#define AST_MUTEX_INIT_W_CONSTRUCTORS
#endif /* SOLARIS || BSD */

/* Asterisk REQUIRES recursive (not error checking) mutexes
   and will not run without them. */
#if defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define AST_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INITIALIZER
#define AST_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE
#endif /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */

#ifdef DEBUG_THREADS

#define __ast_mutex_logger(...)  do { if (canlog) ast_log(LOG_ERROR, __VA_ARGS__); else fprintf(stderr, __VA_ARGS__); } while (0)

#ifdef THREAD_CRASH
#define DO_THREAD_CRASH do { *((int *)(0)) = 1; } while(0)
#else
#define DO_THREAD_CRASH do { } while (0)
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>

#define AST_MUTEX_INIT_VALUE { PTHREAD_MUTEX_INIT_VALUE, 1, { NULL }, { 0 }, 0, { NULL }, { 0 }, PTHREAD_MUTEX_INIT_VALUE }
#define AST_MUTEX_INIT_VALUE_NOTRACKING \
                             { PTHREAD_MUTEX_INIT_VALUE, 0, { NULL }, { 0 }, 0, { NULL }, { 0 }, PTHREAD_MUTEX_INIT_VALUE }

#define AST_MAX_REENTRANCY 10

struct ast_mutex_info {
	pthread_mutex_t mutex;
	/*! Track which thread holds this lock */
	unsigned int track:1;
	const char *file[AST_MAX_REENTRANCY];
	int lineno[AST_MAX_REENTRANCY];
	int reentrancy;
	const char *func[AST_MAX_REENTRANCY];
	pthread_t thread[AST_MAX_REENTRANCY];
	pthread_mutex_t reentr_mutex;
};

typedef struct ast_mutex_info ast_mutex_t;

typedef pthread_cond_t ast_cond_t;

static pthread_mutex_t empty_mutex;

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
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
	int line_num, const char *func, const char *lock_name, void *lock_addr);
#else
#define ast_store_lock_info(I,DONT,CARE,ABOUT,THE,PARAMETERS)
#endif


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
void ast_remove_lock_info(void *lock_addr);
#else
#define ast_remove_lock_info(ignore)
#endif

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
#define DEADLOCK_AVOIDANCE(lock) \
	do { \
		char __filename[80], __func[80], __mutex_name[80]; \
		int __lineno; \
		int __res = ast_find_lock_info(lock, __filename, sizeof(__filename), &__lineno, __func, sizeof(__func), __mutex_name, sizeof(__mutex_name)); \
		ast_mutex_unlock(lock); \
		usleep(1); \
		if (__res < 0) { /* Shouldn't ever happen, but just in case... */ \
			ast_mutex_lock(lock); \
		} else { \
			__ast_pthread_mutex_lock(__filename, __lineno, __func, __mutex_name, lock); \
		} \
	} while (0)

static void __attribute__((constructor)) init_empty_mutex(void)
{
	memset(&empty_mutex, 0, sizeof(empty_mutex));
}

static inline void ast_reentrancy_lock(ast_mutex_t *p_ast_mutex)
{
	pthread_mutex_lock(&p_ast_mutex->reentr_mutex);
}

static inline void ast_reentrancy_unlock(ast_mutex_t *p_ast_mutex)
{
	pthread_mutex_unlock(&p_ast_mutex->reentr_mutex);
}

static inline void ast_reentrancy_init(ast_mutex_t *p_ast_mutex)
{
	int i;
	pthread_mutexattr_t reentr_attr;

	for (i = 0; i < AST_MAX_REENTRANCY; i++) {
		p_ast_mutex->file[i] = NULL;
		p_ast_mutex->lineno[i] = 0;
		p_ast_mutex->func[i] = NULL;
		p_ast_mutex->thread[i] = 0;
	}

	p_ast_mutex->reentrancy = 0;

	pthread_mutexattr_init(&reentr_attr);
	pthread_mutexattr_settype(&reentr_attr, AST_MUTEX_KIND);
	pthread_mutex_init(&p_ast_mutex->reentr_mutex, &reentr_attr);
	pthread_mutexattr_destroy(&reentr_attr);
}

static inline void delete_reentrancy_cs(ast_mutex_t * p_ast_mutex)
{
	pthread_mutex_destroy(&p_ast_mutex->reentr_mutex);
}

static inline int __ast_pthread_mutex_init(int track, const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t) 
{
	int res;
	pthread_mutexattr_t  attr;

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS

	if ((t->mutex) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
/*
		int canlog = strcmp(filename, "logger.c") & track;
		__ast_mutex_logger("%s line %d (%s): NOTICE: mutex '%s' is already initialized.\n",
				   filename, lineno, func, mutex_name);
		DO_THREAD_CRASH;
*/
		return 0;
	}

#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_reentrancy_init(t);
	t->track = track;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);

	res = pthread_mutex_init(&t->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	return res;
}

#define ast_mutex_init(pmutex) __ast_pthread_mutex_init(1, __FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)
#define ast_mutex_init_notracking(pmutex) \
	__ast_pthread_mutex_init(0, __FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)

static inline int __ast_pthread_mutex_destroy(const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c") & t->track;

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		/* Don't try to uninitialize non initialized mutex
		 * This may no effect on linux
		 * And always ganerate core on *BSD with 
		 * linked libpthread
		 * This not error condition if the mutex created on the fly.
		 */
		__ast_mutex_logger("%s line %d (%s): NOTICE: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		return 0;
	}
#endif

	res = pthread_mutex_trylock(&t->mutex);
	switch (res) {
	case 0:
		pthread_mutex_unlock(&t->mutex);
		break;
	case EINVAL:
		__ast_mutex_logger("%s line %d (%s): Error: attempt to destroy invalid mutex '%s'.\n",
				  filename, lineno, func, mutex_name);
		break;
	case EBUSY:
		__ast_mutex_logger("%s line %d (%s): Error: attempt to destroy locked mutex '%s'.\n",
				   filename, lineno, func, mutex_name);
		ast_reentrancy_lock(t);
		__ast_mutex_logger("%s line %d (%s): Error: '%s' was locked here.\n",
			    t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		ast_reentrancy_unlock(t);
		break;
	}

	if ((res = pthread_mutex_destroy(&t->mutex)))
		__ast_mutex_logger("%s line %d (%s): Error destroying mutex %s: %s\n",
				   filename, lineno, func, mutex_name, strerror(res));
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
	else
		t->mutex = PTHREAD_MUTEX_INIT_VALUE;
#endif
	ast_reentrancy_lock(t);
	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;
	t->reentrancy = 0;
	t->thread[0] = 0;
	ast_reentrancy_unlock(t);
	delete_reentrancy_cs(t);

	return res;
}

static inline int __ast_pthread_mutex_lock(const char *filename, int lineno, const char *func,
                                           const char* mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c") & t->track;

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		/* Don't warn abount uninitialized mutex.
		 * Simple try to initialize it.
		 * May be not needed in linux system.
		 */
		res = __ast_pthread_mutex_init(t->track, filename, lineno, func, mutex_name, t);
		if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
			__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized and unable to initialize.\n",
					 filename, lineno, func, mutex_name);
			return res;
		}		
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (t->track)
		ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, &t->mutex);

#ifdef DETECT_DEADLOCKS
	{
		time_t seconds = time(NULL);
		time_t wait_time, reported_wait = 0;
		do {
#ifdef	HAVE_MTX_PROFILE
			ast_mark(mtx_prof, 1);
#endif
			res = pthread_mutex_trylock(&t->mutex);
#ifdef	HAVE_MTX_PROFILE
			ast_mark(mtx_prof, 0);
#endif
			if (res == EBUSY) {
				wait_time = time(NULL) - seconds;
				if (wait_time > reported_wait && (wait_time % 5) == 0) {
					__ast_mutex_logger("%s line %d (%s): Deadlock? waited %d sec for mutex '%s'?\n",
							   filename, lineno, func, (int) wait_time, mutex_name);
					ast_reentrancy_lock(t);
					__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
							   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1],
							   t->func[t->reentrancy-1], mutex_name);
					ast_reentrancy_unlock(t);
					reported_wait = wait_time;
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#else
#ifdef	HAVE_MTX_PROFILE
	ast_mark(mtx_prof, 1);
	res = pthread_mutex_trylock(&t->mutex);
	ast_mark(mtx_prof, 0);
	if (res)
#endif
	res = pthread_mutex_lock(&t->mutex);
#endif /* DETECT_DEADLOCKS */

	if (!res) {
		ast_reentrancy_lock(t);
		if (t->reentrancy < AST_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__ast_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
		ast_reentrancy_unlock(t);
		if (t->track)
			ast_mark_lock_acquired(&t->mutex);
	} else {
		if (t->track)
			ast_remove_lock_info(&t->mutex);
		__ast_mutex_logger("%s line %d (%s): Error obtaining mutex: %s\n",
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	}

	return res;
}

static inline int __ast_pthread_mutex_trylock(const char *filename, int lineno, const char *func,
                                              const char* mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c") & t->track;

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		/* Don't warn abount uninitialized mutex.
		 * Simple try to initialize it.
		 * May be not needed in linux system.
		 */
		res = __ast_pthread_mutex_init(t->track, filename, lineno, func, mutex_name, t);
		if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
			__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized and unable to initialize.\n",
					 filename, lineno, func, mutex_name);
			return res;
		}		
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (t->track)
		ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, &t->mutex);

	if (!(res = pthread_mutex_trylock(&t->mutex))) {
		ast_reentrancy_lock(t);
		if (t->reentrancy < AST_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__ast_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
					   filename, lineno, func, mutex_name);
		}
		ast_reentrancy_unlock(t);
		if (t->track)
			ast_mark_lock_acquired(&t->mutex);
	} else if (t->track) {
		ast_mark_lock_failed(&t->mutex);
	}

	return res;
}

static inline int __ast_pthread_mutex_unlock(const char *filename, int lineno, const char *func,
					     const char *mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c") & t->track;

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		res = __ast_pthread_mutex_init(t->track, filename, lineno, func, mutex_name, t);
		if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
			__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized and unable to initialize.\n",
					 filename, lineno, func, mutex_name);
		}
		return res;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_reentrancy_lock(t);
	if (t->reentrancy && (t->thread[t->reentrancy-1] != pthread_self())) {
		__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		DO_THREAD_CRASH;
	}

	if (--t->reentrancy < 0) {
		__ast_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
				   filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}

	if (t->reentrancy < AST_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		t->thread[t->reentrancy] = 0;
	}
	ast_reentrancy_unlock(t);

	if (t->track)
		ast_remove_lock_info(&t->mutex);

	if ((res = pthread_mutex_unlock(&t->mutex))) {
		__ast_mutex_logger("%s line %d (%s): Error releasing mutex: %s\n", 
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	}

	return res;
}

static inline int __ast_cond_init(const char *filename, int lineno, const char *func,
				  const char *cond_name, ast_cond_t *cond, pthread_condattr_t *cond_attr)
{
	return pthread_cond_init(cond, cond_attr);
}

static inline int __ast_cond_signal(const char *filename, int lineno, const char *func,
				    const char *cond_name, ast_cond_t *cond)
{
	return pthread_cond_signal(cond);
}

static inline int __ast_cond_broadcast(const char *filename, int lineno, const char *func,
				       const char *cond_name, ast_cond_t *cond)
{
	return pthread_cond_broadcast(cond);
}

static inline int __ast_cond_destroy(const char *filename, int lineno, const char *func,
				     const char *cond_name, ast_cond_t *cond)
{
	return pthread_cond_destroy(cond);
}

static inline int __ast_cond_wait(const char *filename, int lineno, const char *func,
				  const char *cond_name, const char *mutex_name,
				  ast_cond_t *cond, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c") & t->track;

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		res = __ast_pthread_mutex_init(t->track, filename, lineno, func, mutex_name, t);
		if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
			__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized and unable to initialize.\n",
					 filename, lineno, func, mutex_name);
		}
		return res;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_reentrancy_lock(t);
	if (t->reentrancy && (t->thread[t->reentrancy-1] != pthread_self())) {
		__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		DO_THREAD_CRASH;
	}

	if (--t->reentrancy < 0) {
		__ast_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
				   filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}

	if (t->reentrancy < AST_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		t->thread[t->reentrancy] = 0;
	}
	ast_reentrancy_unlock(t);

	if (t->track)
		ast_remove_lock_info(&t->mutex);

	if ((res = pthread_cond_wait(cond, &t->mutex))) {
		__ast_mutex_logger("%s line %d (%s): Error waiting on condition mutex '%s'\n", 
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	} else {
		ast_reentrancy_lock(t);
		if (t->reentrancy < AST_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__ast_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
		ast_reentrancy_unlock(t);

		if (t->track)
			ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, &t->mutex);
	}

	return res;
}

static inline int __ast_cond_timedwait(const char *filename, int lineno, const char *func,
				       const char *cond_name, const char *mutex_name, ast_cond_t *cond,
				       ast_mutex_t *t, const struct timespec *abstime)
{
	int res;
	int canlog = strcmp(filename, "logger.c") & t->track;

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		res = __ast_pthread_mutex_init(t->track, filename, lineno, func, mutex_name, t);
		if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
			__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized and unable to initialize.\n",
					 filename, lineno, func, mutex_name);
		}
		return res;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_reentrancy_lock(t);
	if (t->reentrancy && (t->thread[t->reentrancy-1] != pthread_self())) {
		__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		DO_THREAD_CRASH;
	}

	if (--t->reentrancy < 0) {
		__ast_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
				   filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}

	if (t->reentrancy < AST_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		t->thread[t->reentrancy] = 0;
	}
	ast_reentrancy_unlock(t);

	if (t->track)
		ast_remove_lock_info(&t->mutex);

	if ((res = pthread_cond_timedwait(cond, &t->mutex, abstime)) && (res != ETIMEDOUT)) {
		__ast_mutex_logger("%s line %d (%s): Error waiting on condition mutex '%s'\n", 
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	} else {
		ast_reentrancy_lock(t);
		if (t->reentrancy < AST_MAX_REENTRANCY) {
			t->file[t->reentrancy] = filename;
			t->lineno[t->reentrancy] = lineno;
			t->func[t->reentrancy] = func;
			t->thread[t->reentrancy] = pthread_self();
			t->reentrancy++;
		} else {
			__ast_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
		ast_reentrancy_unlock(t);

		if (t->track)
			ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, &t->mutex);
	}

	return res;
}

#define ast_mutex_destroy(a) __ast_pthread_mutex_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_mutex_lock(a) __ast_pthread_mutex_lock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_mutex_unlock(a) __ast_pthread_mutex_unlock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_mutex_trylock(a) __ast_pthread_mutex_trylock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)
#define ast_cond_init(cond, attr) __ast_cond_init(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond, attr)
#define ast_cond_destroy(cond) __ast_cond_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond)
#define ast_cond_signal(cond) __ast_cond_signal(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond)
#define ast_cond_broadcast(cond) __ast_cond_broadcast(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, cond)
#define ast_cond_wait(cond, mutex) __ast_cond_wait(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex, cond, mutex)
#define ast_cond_timedwait(cond, mutex, time) __ast_cond_timedwait(__FILE__, __LINE__, __PRETTY_FUNCTION__, #cond, #mutex, cond, mutex, time)

#else /* !DEBUG_THREADS */

#define	DEADLOCK_AVOIDANCE(lock) \
	ast_mutex_unlock(lock); \
	usleep(1); \
	ast_mutex_lock(lock);


typedef pthread_mutex_t ast_mutex_t;

#define AST_MUTEX_INIT_VALUE	((ast_mutex_t) PTHREAD_MUTEX_INIT_VALUE)
#define AST_MUTEX_INIT_VALUE_NOTRACKING \
	((ast_mutex_t) PTHREAD_MUTEX_INIT_VALUE)

#define ast_mutex_init_notracking(m) ast_mutex_init(m)

static inline int ast_mutex_init(ast_mutex_t *pmutex)
{
	int res;
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);

	res = pthread_mutex_init(pmutex, &attr);
	pthread_mutexattr_destroy(&attr);
	return res;
}

#define ast_pthread_mutex_init(pmutex,a) pthread_mutex_init(pmutex,a)

static inline int ast_mutex_unlock(ast_mutex_t *pmutex)
{
	return pthread_mutex_unlock(pmutex);
}

static inline int ast_mutex_destroy(ast_mutex_t *pmutex)
{
	return pthread_mutex_destroy(pmutex);
}

static inline int ast_mutex_lock(ast_mutex_t *pmutex)
{
	__MTX_PROF(pmutex);
}

static inline int ast_mutex_trylock(ast_mutex_t *pmutex)
{
	return pthread_mutex_trylock(pmutex);
}

typedef pthread_cond_t ast_cond_t;

static inline int ast_cond_init(ast_cond_t *cond, pthread_condattr_t *cond_attr)
{
	return pthread_cond_init(cond, cond_attr);
}

static inline int ast_cond_signal(ast_cond_t *cond)
{
	return pthread_cond_signal(cond);
}

static inline int ast_cond_broadcast(ast_cond_t *cond)
{
	return pthread_cond_broadcast(cond);
}

static inline int ast_cond_destroy(ast_cond_t *cond)
{
	return pthread_cond_destroy(cond);
}

static inline int ast_cond_wait(ast_cond_t *cond, ast_mutex_t *t)
{
	return pthread_cond_wait(cond, t);
}

static inline int ast_cond_timedwait(ast_cond_t *cond, ast_mutex_t *t, const struct timespec *abstime)
{
	return pthread_cond_timedwait(cond, t, abstime);
}

#endif /* !DEBUG_THREADS */

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
/* If AST_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 destructors to destroy mutexes and create it on the fly.  */
#define __AST_MUTEX_DEFINE(scope, mutex, init_val, track) \
	scope ast_mutex_t mutex = init_val; \
static void  __attribute__((constructor)) init_##mutex(void) \
{ \
	if (track) \
		ast_mutex_init(&mutex); \
	else \
		ast_mutex_init_notracking(&mutex); \
} \
static void  __attribute__((destructor)) fini_##mutex(void) \
{ \
	ast_mutex_destroy(&mutex); \
}
#else /* !AST_MUTEX_INIT_W_CONSTRUCTORS */
/* By default, use static initialization of mutexes. */ 
#define __AST_MUTEX_DEFINE(scope, mutex, init_val, track) \
	scope ast_mutex_t mutex = init_val
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

#define pthread_mutex_t use_ast_mutex_t_instead_of_pthread_mutex_t
#define pthread_mutex_lock use_ast_mutex_lock_instead_of_pthread_mutex_lock
#define pthread_mutex_unlock use_ast_mutex_unlock_instead_of_pthread_mutex_unlock
#define pthread_mutex_trylock use_ast_mutex_trylock_instead_of_pthread_mutex_trylock
#define pthread_mutex_init use_ast_mutex_init_instead_of_pthread_mutex_init
#define pthread_mutex_destroy use_ast_mutex_destroy_instead_of_pthread_mutex_destroy
#define pthread_cond_t use_ast_cond_t_instead_of_pthread_cond_t
#define pthread_cond_init use_ast_cond_init_instead_of_pthread_cond_init
#define pthread_cond_destroy use_ast_cond_destroy_instead_of_pthread_cond_destroy
#define pthread_cond_signal use_ast_cond_signal_instead_of_pthread_cond_signal
#define pthread_cond_broadcast use_ast_cond_broadcast_instead_of_pthread_cond_broadcast
#define pthread_cond_wait use_ast_cond_wait_instead_of_pthread_cond_wait
#define pthread_cond_timedwait use_ast_cond_timedwait_instead_of_pthread_cond_timedwait

#define AST_MUTEX_DEFINE_STATIC(mutex) __AST_MUTEX_DEFINE(static, mutex, AST_MUTEX_INIT_VALUE, 1)
#define AST_MUTEX_DEFINE_STATIC_NOTRACKING(mutex) __AST_MUTEX_DEFINE(static, mutex, AST_MUTEX_INIT_VALUE_NOTRACKING, 0)

#define AST_MUTEX_INITIALIZER __use_AST_MUTEX_DEFINE_STATIC_rather_than_AST_MUTEX_INITIALIZER__

#define gethostbyname __gethostbyname__is__not__reentrant__use__ast_gethostbyname__instead__

#ifndef __linux__
#define pthread_create __use_ast_pthread_create_instead__
#endif

typedef pthread_rwlock_t ast_rwlock_t;

#ifdef HAVE_PTHREAD_RWLOCK_INITIALIZER
#define AST_RWLOCK_INIT_VALUE PTHREAD_RWLOCK_INITIALIZER
#else
#define AST_RWLOCK_INIT_VALUE NULL
#endif

#ifdef DEBUG_THREADS

#define ast_rwlock_init(rwlock) __ast_rwlock_init(__FILE__, __LINE__, __PRETTY_FUNCTION__, #rwlock, rwlock)


static inline int __ast_rwlock_init(const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *prwlock)
{
	int res;
	pthread_rwlockattr_t attr;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
        int canlog = strcmp(filename, "logger.c");

        if (*prwlock != ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is already initialized.\n",
				filename, lineno, func, rwlock_name);
		return 0;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
	pthread_rwlockattr_init(&attr);

#ifdef HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
#endif

	res = pthread_rwlock_init(prwlock, &attr);
	pthread_rwlockattr_destroy(&attr);
	return res;
}

#define ast_rwlock_destroy(rwlock) __ast_rwlock_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #rwlock, rwlock)

static inline int __ast_rwlock_destroy(const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *prwlock)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if (*prwlock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is uninitialized.\n",
				   filename, lineno, func, rwlock_name);
		return 0;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
	
	if ((res = pthread_rwlock_destroy(prwlock)))
		__ast_mutex_logger("%s line %d (%s): Error destroying rwlock %s: %s\n",
				filename, lineno, func, rwlock_name, strerror(res));

	return res;
}

#define ast_rwlock_unlock(a) \
	_ast_rwlock_unlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_unlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");

	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is uninitialized.\n",
				   file, line, func, name);
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
		}
		return res;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
	
	res = pthread_rwlock_unlock(lock);
	ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_rdlock(a) \
	_ast_rwlock_rdlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_rdlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");
	
	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		 /* Don't warn abount uninitialized lock.
		  * Simple try to initialize it.
		  * May be not needed in linux system.
		  */
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
			return res;
		}
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
	
	ast_store_lock_info(AST_RDLOCK, file, line, func, name, lock);
	res = pthread_rwlock_rdlock(lock);
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_wrlock(a) \
	_ast_rwlock_wrlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_wrlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");
	
	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		 /* Don't warn abount uninitialized lock.
		  * Simple try to initialize it.
		  * May be not needed in linux system.
		  */
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
			return res;
		}
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_store_lock_info(AST_WRLOCK, file, line, func, name, lock);
	res = pthread_rwlock_wrlock(lock);
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_timedrdlock(a,b) \
	_ast_rwlock_timedrdlock(a, # a, b, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_timedrdlock(ast_rwlock_t *lock, const char *name,
	const struct timespec *abs_timeout, const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");
	
	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		 /* Don't warn abount uninitialized lock.
		  * Simple try to initialize it.
		  * May be not needed in linux system.
		  */
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
			return res;
		}
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
	
	ast_store_lock_info(AST_RDLOCK, file, line, func, name, lock);
#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedrdlock(lock, abs_timeout);
#else
	do {
		struct timeval _start = ast_tvnow(), _diff;
		for (;;) {
			if (!(res = pthread_rwlock_tryrdlock(&t->lock))) {
				break;
			}
			_diff = ast_tvsub(ast_tvnow(), _start);
			if (_diff.tv_sec > abs_timeout->tv_sec || (_diff.tv_sec == abs_timeout->tv_sec && _diff.tv_usec * 1000 > abs_timeout->tv_nsec)) {
				break;
			}
			usleep(1);
		}
	} while (0);
#endif
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_timedwrlock(a,b) \
	_ast_rwlock_timedwrlock(a, # a, b, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_timedwrlock(ast_rwlock_t *lock, const char *name,
	const struct timespec *abs_timeout, const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");
	
	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		 /* Don't warn abount uninitialized lock.
		  * Simple try to initialize it.
		  * May be not needed in linux system.
		  */
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
			return res;
		}
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_store_lock_info(AST_WRLOCK, file, line, func, name, lock);
#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedwrlock(lock, abs_timeout);
#else
	do {
		struct timeval _start = ast_tvnow(), _diff;
		for (;;) {
			if (!(res = pthread_rwlock_trywrlock(&t->lock))) {
				break;
			}
			_diff = ast_tvsub(ast_tvnow(), _start);
			if (_diff.tv_sec > abs_timeout->tv_sec || (_diff.tv_sec == abs_timeout->tv_sec && _diff.tv_usec * 1000 > abs_timeout->tv_nsec)) {
				break;
			}
			usleep(1);
		}
	} while (0);
#endif
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_tryrdlock(a) \
	_ast_rwlock_tryrdlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_tryrdlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");
	
	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		 /* Don't warn abount uninitialized lock.
		  * Simple try to initialize it.
		  * May be not needed in linux system.
		  */
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
			return res;
		}
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_store_lock_info(AST_RDLOCK, file, line, func, name, lock);
	res = pthread_rwlock_tryrdlock(lock);
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_trywrlock(a) \
	_ast_rwlock_trywrlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_trywrlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(file, "logger.c");
	
	if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		 /* Don't warn abount uninitialized lock.
		  * Simple try to initialize it.
		  * May be not needed in linux system.
		  */
		res = __ast_rwlock_init(file, line, func, name, lock);
		if (*lock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
			__ast_mutex_logger("%s line %d (%s): Error: rwlock '%s' is uninitialized and unable to initialize.\n",
					file, line, func, name);
			return res;
		}
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	ast_store_lock_info(AST_WRLOCK, file, line, func, name, lock);
	res = pthread_rwlock_trywrlock(lock);
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
	return res;
}

#else /* !DEBUG_THREADS */

static inline int ast_rwlock_init(ast_rwlock_t *prwlock)
{
	int res;
	pthread_rwlockattr_t attr;

	pthread_rwlockattr_init(&attr);

#ifdef HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
#endif

	res = pthread_rwlock_init(prwlock, &attr);
	pthread_rwlockattr_destroy(&attr);
	return res;
}

static inline int ast_rwlock_destroy(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_destroy(prwlock);
}

static inline int ast_rwlock_unlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_unlock(prwlock);
}

static inline int ast_rwlock_rdlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_rdlock(prwlock);
}

static inline int ast_rwlock_timedrdlock(ast_rwlock_t *prwlock, const struct timespec *abs_timeout)
{
	int res;
#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedrdlock(prwlock, abs_timeout);
#else
	struct timeval _start = ast_tvnow(), _diff;
	for (;;) {
		if (!(res = pthread_rwlock_tryrdlock(prwlock))) {
			break;
		}
		_diff = ast_tvsub(ast_tvnow(), _start);
		if (_diff.tv_sec > abs_timeout->tv_sec || (_diff.tv_sec == abs_timeout->tv_sec && _diff.tv_usec * 1000 > abs_timeout->tv_nsec)) {
			break;
		}
		usleep(1);
	}
#endif
	return res;
}

static inline int ast_rwlock_tryrdlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_tryrdlock(prwlock);
}

static inline int ast_rwlock_wrlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_wrlock(prwlock);
}

static inline int ast_rwlock_timedwrlock(ast_rwlock_t *prwlock, const struct timespec *abs_timeout)
{
	int res;
#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedwrlock(prwlock, abs_timeout);
#else
	do {
		struct timeval _start = ast_tvnow(), _diff;
		for (;;) {
			if (!(res = pthread_rwlock_trywrlock(prwlock))) {
				break;
			}
			_diff = ast_tvsub(ast_tvnow(), _start);
			if (_diff.tv_sec > abs_timeout->tv_sec || (_diff.tv_sec == abs_timeout->tv_sec && _diff.tv_usec * 1000 > abs_timeout->tv_nsec)) {
				break;
			}
			usleep(1);
		}
	} while (0);
#endif
	return res;
}

static inline int ast_rwlock_trywrlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_trywrlock(prwlock);
}
#endif /* !DEBUG_THREADS */

/* Statically declared read/write locks */

#ifndef HAVE_PTHREAD_RWLOCK_INITIALIZER
#define __AST_RWLOCK_DEFINE(scope, rwlock) \
        scope ast_rwlock_t rwlock; \
static void  __attribute__((constructor)) init_##rwlock(void) \
{ \
        ast_rwlock_init(&rwlock); \
} \
static void  __attribute__((destructor)) fini_##rwlock(void) \
{ \
        ast_rwlock_destroy(&rwlock); \
}
#else
#define __AST_RWLOCK_DEFINE(scope, rwlock) \
        scope ast_rwlock_t rwlock = AST_RWLOCK_INIT_VALUE
#endif

#define AST_RWLOCK_DEFINE_STATIC(rwlock) __AST_RWLOCK_DEFINE(static, rwlock)

/*
 * Initial support for atomic instructions.
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
#elif defined (__i386__)
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

#ifndef DEBUG_CHANNEL_LOCKS
/*! \brief Lock a channel. If DEBUG_CHANNEL_LOCKS is defined 
	in the Makefile, print relevant output for debugging */
#define ast_channel_lock(x)		ast_mutex_lock(&x->lock)
/*! \brief Unlock a channel. If DEBUG_CHANNEL_LOCKS is defined 
	in the Makefile, print relevant output for debugging */
#define ast_channel_unlock(x)		ast_mutex_unlock(&x->lock)
/*! \brief Try locking a channel. If DEBUG_CHANNEL_LOCKS is defined 
	in the Makefile, print relevant output for debugging */
#define ast_channel_trylock(x)		ast_mutex_trylock(&x->lock)
#else

struct ast_channel;

#define ast_channel_lock(a) __ast_channel_lock(a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
/*! \brief Lock AST channel (and print debugging output)
\note You need to enable DEBUG_CHANNEL_LOCKS for this function */
int __ast_channel_lock(struct ast_channel *chan, const char *file, int lineno, const char *func);

#define ast_channel_unlock(a) __ast_channel_unlock(a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
/*! \brief Unlock AST channel (and print debugging output)
\note You need to enable DEBUG_CHANNEL_LOCKS for this function
*/
int __ast_channel_unlock(struct ast_channel *chan, const char *file, int lineno, const char *func);

#define ast_channel_trylock(a) __ast_channel_trylock(a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
/*! \brief Lock AST channel (and print debugging output)
\note   You need to enable DEBUG_CHANNEL_LOCKS for this function */
int __ast_channel_trylock(struct ast_channel *chan, const char *file, int lineno, const char *func);
#endif

#endif /* _ASTERISK_LOCK_H */
