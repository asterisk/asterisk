/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Asterisk channel definitions.
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_LOCK_H
#define _ASTERISK_LOCK_H

#include <pthread.h>
#include <netdb.h>
#include <time.h>
#include <sys/param.h>

#define AST_PTHREADT_NULL (pthread_t) -1
#define AST_PTHREADT_STOP (pthread_t) -2

#ifdef __APPLE__
/* Provide the Linux initializers for MacOS X */
#define PTHREAD_MUTEX_RECURSIVE_NP					PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP		 { 0x4d555458, \
													   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
														 0x20 } }
#endif

#ifdef BSD
#ifdef __GNUC__
#define AST_MUTEX_INIT_W_CONSTRUCTORS
#else
#define AST_MUTEX_INIT_ON_FIRST_USE
#endif
#endif /* BSD */

/* From now on, Asterisk REQUIRES Recursive (not error checking) mutexes
   and will not run without them. */
#ifdef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define AST_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INITIALIZER
#define AST_MUTEX_KIND			PTHREAD_MUTEX_RECURSIVE
#endif /* PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */

#ifdef SOLARIS
#define AST_MUTEX_INIT_W_CONSTRUCTORS
#endif

#ifdef DEBUG_THREADS

#ifdef THREAD_CRASH
#define DO_THREAD_CRASH do { *((int *)(0)) = 1; } while(0)
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define AST_MUTEX_INIT_VALUE      { PTHREAD_MUTEX_INIT_VALUE, NULL, 0, 0, NULL, 0 }

struct ast_mutex_info {
	pthread_mutex_t mutex;
	char *file;
	int lineno;
	int reentrancy;
	char *func;
	pthread_t thread;
};

typedef struct ast_mutex_info ast_mutex_t;

static inline int __ast_pthread_mutex_init_attr(char *filename, int lineno, char *func,
						char* mutex_name, ast_mutex_t *t,
						pthread_mutexattr_t *attr) 
{
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) != ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER)) {
		fprintf(stderr, "%s line %d (%s): Error: mutex '%s' is already initialized.\n",
			filename, lineno, func, mutex_name);
		fprintf(stderr, "%s line %d (%s): Error: previously initialization of mutex '%s'.\n",
			t->file, t->lineno, t->func, mutex_name);
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
		return 0;
	}
#endif
	t->file = filename;
	t->lineno = lineno;
	t->func = func;
	t->thread  = 0;
	t->reentrancy = 0;
	return pthread_mutex_init(&t->mutex, attr);
}

static inline int __ast_pthread_mutex_init(char *filename, int lineno, char *func,
						char *mutex_name, ast_mutex_t *t)
{
	static pthread_mutexattr_t  attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);
	return __ast_pthread_mutex_init_attr(filename, lineno, func, mutex_name, t, &attr);
}

#define ast_mutex_init(pmutex) __ast_pthread_mutex_init(__FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)
#define ast_pthread_mutex_init(pmutex,attr) __ast_pthread_mutex_init_attr(__FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex, attr)

static inline int __ast_pthread_mutex_destroy(char *filename, int lineno, char *func,
						char *mutex_name, ast_mutex_t *t)
{
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER)) {
		fprintf(stderr, "%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
			filename, lineno, func, mutex_name);
	}
#endif
	res = pthread_mutex_trylock(&t->mutex);
	switch (res) {
	case 0:
		pthread_mutex_unlock(&t->mutex);
		break;
	case EINVAL:
		fprintf(stderr, "%s line %d (%s): Error: attempt to destroy invalid mutex '%s'.\n",
			filename, lineno, func, mutex_name);
		break;
	case EBUSY:
		fprintf(stderr, "%s line %d (%s): Error: attemp to destroy locked mutex '%s'.\n",
			filename, lineno, func, mutex_name);
		fprintf(stderr, "%s line %d (%s): Error: '%s' was locked here.\n",
			t->file, t->lineno, t->func, mutex_name);
		break;
	}
	res = pthread_mutex_destroy(&t->mutex);
	if (res) 
		fprintf(stderr, "%s line %d (%s): Error destroying mutex: %s\n",
				filename, lineno, func, strerror(res));
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
	else
		t->mutex = PTHREAD_MUTEX_INIT_VALUE;
#endif
	t->file = filename;
	t->lineno = lineno;
	t->func = func;
	return res;
}

#define ast_mutex_destroy(a) __ast_pthread_mutex_destroy(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
/* if AST_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constrictors/destructors to create/destroy mutexes.  */
#define __AST_MUTEX_DEFINE(scope,mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE; \
static void  __attribute__ ((constructor)) init_##mutex(void) \
{ \
	ast_mutex_init(&mutex); \
} \
static void  __attribute__ ((destructor)) fini_##mutex(void) \
{ \
	ast_mutex_destroy(&mutex); \
}
#elif defined(AST_MUTEX_INIT_ON_FIRST_USE)
/* if AST_MUTEX_INIT_ON_FIRST_USE is defined, mutexes are created on
 first use.  The performance impact on FreeBSD should be small since
 the pthreads library does this itself to initialize errror checking
 (defaulty type) mutexes.  If nither is defined, the pthreads librariy
 does the initialization itself on first use. */ 
#define __AST_MUTEX_DEFINE(scope,mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE
#else /* AST_MUTEX_INIT_W_CONSTRUCTORS */
/* By default, use static initialization of mutexes.*/ 
#define __AST_MUTEX_DEFINE(scope,mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */



static inline int __ast_pthread_mutex_lock(char *filename, int lineno, char *func,
                                           char* mutex_name, ast_mutex_t *t)
{
	int res;
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) || defined(AST_MUTEX_INIT_ON_FIRST_USE)
	if ((t->mutex) == ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER)) {
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
		fprintf(stderr, "%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
			filename, lineno, func, mutex_name);
#endif
		ast_mutex_init(t);
	}
#endif /* definded(AST_MUTEX_INIT_W_CONSTRUCTORS) || defined(AST_MUTEX_INIT_ON_FIRST_USE) */
#ifdef DETECT_DEADLOCKS
	{
		time_t seconds = time(NULL);
		time_t current;
		do {
			res = pthread_mutex_trylock(&t->mutex);
			if (res == EBUSY) {
				current = time(NULL);
				if ((current - seconds) && (!((current - seconds) % 5))) {
					fprintf(stderr, "%s line %d (%s): Deadlock? waited %d sec for mutex '%s'?\n",
						filename, lineno, func, (int)(current - seconds), mutex_name);
					fprintf(stderr, "%s line %d (%s): '%s' was locked here.\n",
						t->file, t->lineno, t->func, mutex_name);
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#else
	res = pthread_mutex_lock(&t->mutex);
#endif /*  DETECT_DEADLOCKS */
	if (!res) {
		t->reentrancy++;
		t->file = filename;
		t->lineno = lineno;
		t->func = func;
		t->thread = pthread_self();
	} else {
		fprintf(stderr, "%s line %d (%s): Error obtaining mutex: %s\n",
			filename, lineno, func, strerror(errno));
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}
	return res;
}

#define ast_mutex_lock(a) __ast_pthread_mutex_lock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)

static inline int __ast_pthread_mutex_trylock(char *filename, int lineno, char *func,
                                              char* mutex_name, ast_mutex_t *t)
{
	int res;
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) || defined(AST_MUTEX_INIT_ON_FIRST_USE)
	if ((t->mutex) == ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER)) {
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
		fprintf(stderr, "%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
			filename, lineno, func, mutex_name);
#endif
		ast_mutex_init(t);
	}
#endif /* definded(AST_MUTEX_INIT_W_CONSTRUCTORS) || defined(AST_MUTEX_INIT_ON_FIRST_USE) */
	res = pthread_mutex_trylock(&t->mutex);
	if (!res) {
		t->reentrancy++;
		t->file = filename;
		t->lineno = lineno;
		t->func = func;
		t->thread = pthread_self();
	}
	return res;
}

#define ast_mutex_trylock(a) __ast_pthread_mutex_trylock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)

static inline int __ast_pthread_mutex_unlock(char *filename, int lineno, char *func,
			char* mutex_name, ast_mutex_t *t) {
	int res;
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER)) {
		fprintf(stderr, "%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
			filename, lineno, func, mutex_name);
	}
#endif
	--t->reentrancy;
	if (t->reentrancy < 0) {
		fprintf(stderr, "%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
			filename, lineno, func, mutex_name);
		t->reentrancy = 0;
	}
	if (!t->reentrancy) {
		t->file = NULL;
		t->lineno = 0;
		t->func = NULL;
		t->thread = 0;
	}
	res = pthread_mutex_unlock(&t->mutex);
	if (res) {
		fprintf(stderr, "%s line %d (%s): Error releasing mutex: %s\n", 
				filename, lineno, func, strerror(res));
#ifdef THREAD_CRASH
		DO_THREAD_CRASH;
#endif
	}
	return res;
}

#define ast_mutex_unlock(a) __ast_pthread_mutex_unlock(__FILE__, __LINE__, __PRETTY_FUNCTION__, #a, a)

#define pthread_mutex_t use_ast_mutex_t_instead_of_pthread_mutex_t
#define pthread_mutex_lock use_ast_mutex_lock_instead_of_pthread_mutex_lock
#define pthread_mutex_unlock use_ast_mutex_unlock_instead_of_pthread_mutex_unlock
#define pthread_mutex_trylock use_ast_mutex_trylock_instead_of_pthread_mutex_trylock
#define pthread_mutex_init use_ast_pthread_mutex_init_instead_of_pthread_mutex_init
#define pthread_mutex_destroy use_ast_pthread_mutex_destroy_instead_of_pthread_mutex_destroy

#else /* DEBUG_THREADS */


#define AST_MUTEX_INIT_VALUE	PTHREAD_MUTEX_INIT_VALUE


typedef pthread_mutex_t ast_mutex_t;

static inline int ast_mutex_init(ast_mutex_t *pmutex)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);
	return pthread_mutex_init(pmutex, &attr);
}
#define ast_pthread_mutex_init(pmutex,a) pthread_mutex_init(pmutex,a)
#define ast_mutex_unlock(pmutex) pthread_mutex_unlock(pmutex)
#define ast_mutex_destroy(pmutex) pthread_mutex_destroy(pmutex)

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
/* if AST_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constrictors/destructors to create/destroy mutexes.  */ 
#define __AST_MUTEX_DEFINE(scope,mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE; \
static void  __attribute__ ((constructor)) init_##mutex(void) \
{ \
	ast_mutex_init(&mutex); \
} \
static void  __attribute__ ((destructor)) fini_##mutex(void) \
{ \
	ast_mutex_destroy(&mutex); \
}

#define ast_mutex_lock(pmutex) pthread_mutex_lock(pmutex)
#define ast_mutex_trylock(pmutex) pthread_mutex_trylock(pmutex)

#elif defined(AST_MUTEX_INIT_ON_FIRST_USE)
/* if AST_MUTEX_INIT_ON_FIRST_USE is defined, mutexes are created on
 first use.  The performance impact on FreeBSD should be small since
 the pthreads library does this itself to initialize errror checking
 (defaulty type) mutexes.*/ 
#define __AST_MUTEX_DEFINE(scope,mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE

static inline int ast_mutex_lock(ast_mutex_t *pmutex)
{
	if (*pmutex == (ast_mutex_t)AST_MUTEX_KIND)
		ast_mutex_init(pmutex);
	return pthread_mutex_lock(pmutex);
}
static inline int ast_mutex_trylock(ast_mutex_t *pmutex)
{
	if (*pmutex == (ast_mutex_t)AST_MUTEX_KIND)
		ast_mutex_init(pmutex);
	return pthread_mutex_trylock(pmutex);
}
#else
/* By default, use static initialization of mutexes.*/ 
#define __AST_MUTEX_DEFINE(scope,mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE
#define ast_mutex_lock(pmutex) pthread_mutex_lock(pmutex)
#define ast_mutex_trylock(pmutex) pthread_mutex_trylock(pmutex)
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

#endif /* DEBUG_THREADS */

#define AST_MUTEX_DEFINE_STATIC(mutex) __AST_MUTEX_DEFINE(static,mutex)
#define AST_MUTEX_DEFINE_EXPORTED(mutex) __AST_MUTEX_DEFINE(/**/,mutex)

#define AST_MUTEX_INITIALIZER __use_AST_MUTEX_DEFINE_STATIC_rather_than_AST_MUTEX_INITIALIZER__

#define gethostbyname __gethostbyname__is__not__reentrant__use__ast_gethostbyname__instead__
#ifndef __linux__
#define pthread_create __use_ast_pthread_create_instead__
#endif

#endif
