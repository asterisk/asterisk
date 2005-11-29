/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Asterisk channel definitions.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_LOCK_H
#define _ASTERISK_LOCK_H

#include <pthread.h>

#ifdef DEBUG_THREADS

#define TRIES 50

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// #define AST_MUTEX_INITIALIZER      PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
// #define AST_MUTEX_KIND             PTHREAD_MUTEX_RECURSIVE_NP
#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
#define AST_MUTEX_INITIALIZER         PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
#define AST_MUTEX_KIND                PTHREAD_MUTEX_ERRORCHECK_NP
#else
#define AST_MUTEX_INITIALIZER      PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define AST_MUTEX_KIND             PTHREAD_MUTEX_RECURSIVE_NP
#endif

struct mutex_info {
	pthread_mutex_t *mutex;
	char *file;
	int lineno;
	char *func;
	struct mutex_info *next;
};

static inline int ast_pthread_mutex_init(pthread_mutex_t *t) {
	static pthread_mutexattr_t  attr;
	static int  init = 1;
	extern int  pthread_mutexattr_setkind_np(pthread_mutexattr_t *, int);

	if (init) {
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_setkind_np(&attr, AST_MUTEX_KIND);
		init = 0;
	}
	return pthread_mutex_init(t, &attr);
}

static inline int __ast_pthread_mutex_lock(char *filename, int lineno, char *func, pthread_mutex_t *t) {
	int res;
	int tries = TRIES;
	do {
		res = pthread_mutex_trylock(t);
		/* If we can't run, yield */
		if (res) {
			sched_yield();
			usleep(1);
		}
	} while(res && tries--);
	if (res) {
		fprintf(stderr, "%s line %d (%s): Error obtaining mutex: %s\n", 
				filename, lineno, func, strerror(res));
		if ((res = pthread_mutex_lock(t)))
                        fprintf(stderr, "%s line %d (%s): Error waiting for mutex: %s\n", 
                               filename, lineno, func, strerror(res));
                else
		        fprintf(stderr, "%s line %d (%s): Got it eventually...\n",
			       filename, lineno, func);
	}
	return res;
}

#define ast_pthread_mutex_lock(a) __ast_pthread_mutex_lock(__FILE__, __LINE__, __PRETTY_FUNCTION__, a)

static inline int __ast_pthread_mutex_unlock(char *filename, int lineno, char *func, pthread_mutex_t *t) {
	int res;
	res = pthread_mutex_unlock(t);
	if (res) 
		fprintf(stderr, "%s line %d (%s): Error releasing mutex: %s\n", 
				filename, lineno, func, strerror(res));
	return res;
}

#define ast_pthread_mutex_unlock(a) __ast_pthread_mutex_unlock(__FILE__, __LINE__, __PRETTY_FUNCTION__, a)

#else

#define AST_MUTEX_INITIALIZER      PTHREAD_MUTEX_INITIALIZER
#define AST_MUTEX_KIND             PTHREAD_MUTEX_FAST_NP

#define ast_pthread_mutex_init(mutex) pthread_mutex_init(mutex, NULL)
#define ast_pthread_mutex_lock pthread_mutex_lock
#define ast_pthread_mutex_unlock pthread_mutex_unlock

#endif

#endif
