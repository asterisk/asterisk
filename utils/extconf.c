/*  
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com>
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


/*!
 * \file extconf
 * A condensation of the pbx_config stuff, to read into exensions.conf, and provide an interface to the data there,
 * for operations outside of asterisk. A huge, awful hack.
 *
 */

/*!
 * \li \ref extconf.c uses the configuration file \ref extconfig.conf and \ref extensions.conf and \ref asterisk.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page extconfig.conf extconfig.conf
 * \verbinclude extconfig.conf.sample
 */

/*!
 * \page extensions.conf extensions.conf
 * \verbinclude extensions.conf.sample
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#define WRAP_LIBC_MALLOC
#include "asterisk.h"

#undef DEBUG_THREADS

#include "asterisk/compat.h"
#include "asterisk/paths.h"	/* we use AST_CONFIG_DIR */

#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#if !defined(SOLARIS) && !defined(__CYGWIN__)
#include <err.h>
#endif
#include <regex.h>
#include <limits.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/param.h>
#include <signal.h>

static void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
void ast_verbose(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define ASINCLUDE_GLOB 1
#ifdef AST_INCLUDE_GLOB

#if !defined(GLOB_ABORTED)
#define GLOB_ABORTED GLOB_ABEND
#endif

# include <glob.h>
#endif

#define AST_API_MODULE  1 /* gimme the inline defs! */
struct ast_channel 
{
	char x; /* basically empty! */
};



#include "asterisk/inline_api.h"
#include "asterisk/endian.h"
#include "asterisk/ast_expr.h"
#include "asterisk/extconf.h"

/* logger.h */

#define EVENTLOG "event_log"
#define	QUEUELOG	"queue_log"

#define DEBUG_M(a) { \
	a; \
}

#define VERBOSE_PREFIX_1 " "
#define VERBOSE_PREFIX_2 "  == "
#define VERBOSE_PREFIX_3 "    -- "
#define VERBOSE_PREFIX_4 "       > "

void ast_log_backtrace(void);

void ast_queue_log(const char *queuename, const char *callid, const char *agent, const char *event, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/* IN CONFLICT: void ast_verbose(const char *fmt, ...)
   __attribute__((format(printf, 1, 2))); */

int ast_register_verbose(void (*verboser)(const char *string));
int ast_unregister_verbose(void (*verboser)(const char *string));

void ast_console_puts(const char *string);

#define _A_ __FILE__, __LINE__, __PRETTY_FUNCTION__

#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#define __LOG_DEBUG    0
#define LOG_DEBUG      __LOG_DEBUG, _A_

#ifdef LOG_EVENT
#undef LOG_EVENT
#endif
#define __LOG_EVENT    1
#define LOG_EVENT      __LOG_EVENT, _A_

#ifdef LOG_NOTICE
#undef LOG_NOTICE
#endif
#define __LOG_NOTICE   2
#define LOG_NOTICE     __LOG_NOTICE, _A_

#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#define __LOG_WARNING  3
#define LOG_WARNING    __LOG_WARNING, _A_

#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#define __LOG_ERROR    4
#define LOG_ERROR      __LOG_ERROR, _A_

#ifdef LOG_VERBOSE
#undef LOG_VERBOSE
#endif
#define __LOG_VERBOSE  5
#define LOG_VERBOSE    __LOG_VERBOSE, _A_

#ifdef LOG_DTMF
#undef LOG_DTMF
#endif
#define __LOG_DTMF  6
#define LOG_DTMF    __LOG_DTMF, _A_

/* lock.h */
#define _ASTERISK_LOCK_H /* A small indication that this is horribly wrong. */

#ifndef	HAVE_MTX_PROFILE
#define	__MTX_PROF(a)	return pthread_mutex_lock((a))
#else
int mtx_prof = -1;

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
#if defined(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP) && defined(PTHREAD_MUTEX_RECURSIVE_NP)
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

#define AST_MUTEX_INIT_VALUE { PTHREAD_MUTEX_INIT_VALUE, { NULL }, { 0 }, 0, { NULL }, { 0 } }

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
};

typedef struct ast_mutex_info ast_mutex_t;

typedef pthread_cond_t ast_cond_t;

static pthread_mutex_t empty_mutex;

static void __attribute__((constructor)) init_empty_mutex(void)
{
	memset(&empty_mutex, 0, sizeof(empty_mutex));
}

static inline int __ast_pthread_mutex_init_attr(const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t,
						pthread_mutexattr_t *attr) 
{
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	int canlog = strcmp(filename, "logger.c");

	if ((t->mutex) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		if ((t->mutex) != (empty_mutex)) {
			__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is already initialized.\n",
					   filename, lineno, func, mutex_name);
			__ast_mutex_logger("%s line %d (%s): Error: previously initialization of mutex '%s'.\n",
					   t->file[0], t->lineno[0], t->func[0], mutex_name);
			DO_THREAD_CRASH;
			return 0;
		}
	}
#endif

	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;
	t->thread[0]  = 0;
	t->reentrancy = 0;

	return pthread_mutex_init(&t->mutex, attr);
}

static inline int __ast_pthread_mutex_init(const char *filename, int lineno, const char *func,
					   const char *mutex_name, ast_mutex_t *t)
{
	static pthread_mutexattr_t  attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);

	return __ast_pthread_mutex_init_attr(filename, lineno, func, mutex_name, t, &attr);
}
#define ast_mutex_init(pmutex) __ast_pthread_mutex_init(__FILE__, __LINE__, __PRETTY_FUNCTION__, #pmutex, pmutex)

static inline int __ast_pthread_mutex_destroy(const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
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
		__ast_mutex_logger("%s line %d (%s): Error: '%s' was locked here.\n",
				   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		break;
	}

	if ((res = pthread_mutex_destroy(&t->mutex)))
		__ast_mutex_logger("%s line %d (%s): Error destroying mutex: %s\n",
				   filename, lineno, func, strerror(res));
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
	else
		t->mutex = PTHREAD_MUTEX_INIT_VALUE;
#endif
	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;

	return res;
}

static inline int __ast_pthread_mutex_lock(const char *filename, int lineno, const char *func,
                                           const char* mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				 filename, lineno, func, mutex_name);
		ast_mutex_init(t);
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

#ifdef DETECT_DEADLOCKS
	{
		time_t seconds = time(NULL);
		time_t current;
		do {
#ifdef	HAVE_MTX_PROFILE
			ast_mark(mtx_prof, 1);
#endif
			res = pthread_mutex_trylock(&t->mutex);
#ifdef	HAVE_MTX_PROFILE
			ast_mark(mtx_prof, 0);
#endif
			if (res == EBUSY) {
				current = time(NULL);
				if ((current - seconds) && (!((current - seconds) % 5))) {
					__ast_mutex_logger("%s line %d (%s): Deadlock? waited %d sec for mutex '%s'?\n",
							   filename, lineno, func, (int)(current - seconds), mutex_name);
					__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
							   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1],
							   t->func[t->reentrancy-1], mutex_name);
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
	} else {
		__ast_mutex_logger("%s line %d (%s): Error obtaining mutex: %s\n",
				   filename, lineno, func, strerror(errno));
		DO_THREAD_CRASH;
	}

	return res;
}

static inline int __ast_pthread_mutex_trylock(const char *filename, int lineno, const char *func,
                                              const char* mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		ast_mutex_init(t);
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (!(res = pthread_mutex_trylock(&t->mutex))) {
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
	} else {
		__ast_mutex_logger("%s line %d (%s): Warning: '%s' was locked here.\n",
                                   t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
	}

	return res;
}

static inline int __ast_pthread_mutex_unlock(const char *filename, int lineno, const char *func,
					     const char *mutex_name, ast_mutex_t *t)
{
	int res;
	int canlog = strcmp(filename, "logger.c");

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
	}
#endif

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

	if ((res = pthread_mutex_unlock(&t->mutex))) {
		__ast_mutex_logger("%s line %d (%s): Error releasing mutex: %s\n", 
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	}

	return res;
}

#else /* !DEBUG_THREADS */


typedef pthread_mutex_t ast_mutex_t;

#define AST_MUTEX_INIT_VALUE	((ast_mutex_t) PTHREAD_MUTEX_INIT_VALUE)

static inline int ast_mutex_init(ast_mutex_t *pmutex)
{
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);

	return pthread_mutex_init(pmutex, &attr);
}

#define ast_pthread_mutex_init(pmutex,a) pthread_mutex_init(pmutex,a)

typedef pthread_cond_t ast_cond_t;

#endif /* !DEBUG_THREADS */

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
/* If AST_MUTEX_INIT_W_CONSTRUCTORS is defined, use file scope
 constructors/destructors to create/destroy mutexes.  */
#define __AST_MUTEX_DEFINE(scope, mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE; \
static void  __attribute__((constructor)) init_##mutex(void) \
{ \
	ast_mutex_init(&mutex); \
}
#else /* !AST_MUTEX_INIT_W_CONSTRUCTORS */
/* By default, use static initialization of mutexes. */ 
#define __AST_MUTEX_DEFINE(scope, mutex) \
	scope ast_mutex_t mutex = AST_MUTEX_INIT_VALUE
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

#define pthread_mutex_t use_ast_mutex_t_instead_of_pthread_mutex_t
#define pthread_mutex_init use_ast_mutex_init_instead_of_pthread_mutex_init
#define pthread_cond_t use_ast_cond_t_instead_of_pthread_cond_t

#define AST_MUTEX_DEFINE_STATIC(mutex) __AST_MUTEX_DEFINE(static, mutex)

#define AST_MUTEX_INITIALIZER __use_AST_MUTEX_DEFINE_STATIC_rather_than_AST_MUTEX_INITIALIZER__

#define gethostbyname __gethostbyname__is__not__reentrant__use__ast_gethostbyname__instead__

#ifndef __linux__
#define pthread_create __use_ast_pthread_create_instead__
#endif

typedef pthread_rwlock_t ast_rwlock_t;

static inline int ast_rwlock_init(ast_rwlock_t *prwlock)
{
	pthread_rwlockattr_t attr;

	pthread_rwlockattr_init(&attr);

#ifdef HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
#endif

	return pthread_rwlock_init(prwlock, &attr);
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

static inline int ast_rwlock_wrlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_wrlock(prwlock);
}

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
#define AST_RWLOCK_INIT_VALUE PTHREAD_RWLOCK_INITIALIZER
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
	return OSAtomicAdd32(v, (int32_t *) p);
})
#elif defined(HAVE_OSX_ATOMICS) && (SIZEOF_INT == 8)
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	return OSAtomicAdd64(v, (int64_t *) p);
#elif defined (__i386__) || defined(__x86_64__)
AST_INLINE_API(int ast_atomic_fetchadd_int(volatile int *p, int v),
{
	__asm __volatile (
	"       lock   xaddl   %0, %1 ;        "
	: "+r" (v),                     /* 0 (result) */   
	  "=m" (*p)                     /* 1 */
	: "m" (*p));                    /* 2 */
	return (v);
})
#else
static int ast_atomic_fetchadd_int_slow(volatile int *p, int v)
{
	int ret;
	ret = *p;
	*p += v;
	return ret;
}
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

#ifdef DEBUG_CHANNEL_LOCKS
/*! \brief Lock AST channel (and print debugging output)
\note You need to enable DEBUG_CHANNEL_LOCKS for this function */
int ast_channel_lock(struct ast_channel *chan);

/*! \brief Unlock AST channel (and print debugging output)
\note You need to enable DEBUG_CHANNEL_LOCKS for this function
*/
int ast_channel_unlock(struct ast_channel *chan);

/*! \brief Lock AST channel (and print debugging output)
\note   You need to enable DEBUG_CHANNEL_LOCKS for this function */
int ast_channel_trylock(struct ast_channel *chan);
#endif


#include "asterisk/hashtab.h"
#include "asterisk/ael_structs.h"
#include "asterisk/pval.h"

/* from utils.h */

#define ast_free free
#define ast_free_ptr free

#define MALLOC_FAILURE_MSG \
	ast_log(LOG_ERROR, "Memory Allocation Failure in function %s at line %d of %s\n", func, lineno, file);

/*!
 * \brief A wrapper for malloc()
 *
 * ast_malloc() is a wrapper for malloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The argument and return value are the same as malloc()
 */
#define ast_malloc(len) \
	_ast_malloc((len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_calloc(num, len) \
	_ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_calloc_cache(num, len) \
	_ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_realloc(p, len) \
	_ast_realloc((p), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_strdup(str) \
	_ast_strdup((str), __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_strndup(str, len) \
	_ast_strndup((str), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ast_asprintf(ret, fmt, ...) \
	_ast_asprintf((ret), __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, __VA_ARGS__)

#define ast_vasprintf(ret, fmt, ap) \
	_ast_vasprintf((ret), __FILE__, __LINE__, __PRETTY_FUNCTION__, (fmt), (ap))

struct ast_flags {  /* stolen from utils.h */
	unsigned int flags;
};
#define ast_test_flag(p,flag) 		({ \
					typeof ((p)->flags) __p = (p)->flags; \
					unsigned int __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags & (flag)); \
					})

#define ast_set2_flag(p,value,flag)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					unsigned int __x = 0; \
					(void) (&__p == &__x); \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)



#define MALLOC_FAILURE_MSG \
	ast_log(LOG_ERROR, "Memory Allocation Failure in function %s at line %d of %s\n", func, lineno, file);
/*!
 * \brief A wrapper for malloc()
 *
 * ast_malloc() is a wrapper for malloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The argument and return value are the same as malloc()
 */
#define ast_malloc(len) \
	_ast_malloc((len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
void * attribute_malloc _ast_malloc(size_t len, const char *file, int lineno, const char *func),
{
	void *p;

	if (!(p = malloc(len)))
		MALLOC_FAILURE_MSG;

	return p;
}
)

/*!
 * \brief A wrapper for calloc()
 *
 * ast_calloc() is a wrapper for calloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as calloc()
 */
#define ast_calloc(num, len) \
	_ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
void * attribute_malloc _ast_calloc(size_t num, size_t len, const char *file, int lineno, const char *func),
{
	void *p;

	if (!(p = calloc(num, len)))
		MALLOC_FAILURE_MSG;

	return p;
}
)

/*!
 * \brief A wrapper for calloc() for use in cache pools
 *
 * ast_calloc_cache() is a wrapper for calloc() that will generate an Asterisk log
 * message in the case that the allocation fails. When memory debugging is in use,
 * the memory allocated by this function will be marked as 'cache' so it can be
 * distinguished from normal memory allocations.
 *
 * The arguments and return value are the same as calloc()
 */
#define ast_calloc_cache(num, len) \
	_ast_calloc((num), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief A wrapper for realloc()
 *
 * ast_realloc() is a wrapper for realloc() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as realloc()
 */
#define ast_realloc(p, len) \
	_ast_realloc((p), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
void * attribute_malloc _ast_realloc(void *p, size_t len, const char *file, int lineno, const char *func),
{
	void *newp;

	if (!(newp = realloc(p, len)))
		MALLOC_FAILURE_MSG;

	return newp;
}
)

/*!
 * \brief A wrapper for strdup()
 *
 * ast_strdup() is a wrapper for strdup() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * ast_strdup(), unlike strdup(), can safely accept a NULL argument. If a NULL
 * argument is provided, ast_strdup will return NULL without generating any
 * kind of error log message.
 *
 * The argument and return value are the same as strdup()
 */
#define ast_strdup(str) \
	_ast_strdup((str), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
char * attribute_malloc _ast_strdup(const char *str, const char *file, int lineno, const char *func),
{
	char *newstr = NULL;

	if (str) {
		if (!(newstr = strdup(str)))
			MALLOC_FAILURE_MSG;
	}

	return newstr;
}
)

/*!
 * \brief A wrapper for strndup()
 *
 * ast_strndup() is a wrapper for strndup() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * ast_strndup(), unlike strndup(), can safely accept a NULL argument for the
 * string to duplicate. If a NULL argument is provided, ast_strdup will return  
 * NULL without generating any kind of error log message.
 *
 * The arguments and return value are the same as strndup()
 */
#define ast_strndup(str, len) \
	_ast_strndup((str), (len), __FILE__, __LINE__, __PRETTY_FUNCTION__)

AST_INLINE_API(
char * attribute_malloc _ast_strndup(const char *str, size_t len, const char *file, int lineno, const char *func),
{
	char *newstr = NULL;

	if (str) {
		if (!(newstr = strndup(str, len)))
			MALLOC_FAILURE_MSG;
	}

	return newstr;
}
)

/*!
 * \brief A wrapper for asprintf()
 *
 * ast_asprintf() is a wrapper for asprintf() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as asprintf()
 */
#define ast_asprintf(ret, fmt, ...) \
	_ast_asprintf((ret), __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, __VA_ARGS__)

AST_INLINE_API(
__attribute__((format(printf, 5, 6)))
int _ast_asprintf(char **ret, const char *file, int lineno, const char *func, const char *fmt, ...),
{
	int res;
	va_list ap;

	va_start(ap, fmt);
	if ((res = vasprintf(ret, fmt, ap)) == -1)
		MALLOC_FAILURE_MSG;
	va_end(ap);

	return res;
}
)

/*!
 * \brief A wrapper for vasprintf()
 *
 * ast_vasprintf() is a wrapper for vasprintf() that will generate an Asterisk log
 * message in the case that the allocation fails.
 *
 * The arguments and return value are the same as vasprintf()
 */
#define ast_vasprintf(ret, fmt, ap) \
	_ast_vasprintf((ret), __FILE__, __LINE__, __PRETTY_FUNCTION__, (fmt), (ap))

AST_INLINE_API(
__attribute__((format(printf, 5, 0)))
int _ast_vasprintf(char **ret, const char *file, int lineno, const char *func, const char *fmt, va_list ap),
{
	int res;

	if ((res = vasprintf(ret, fmt, ap)) == -1)
		MALLOC_FAILURE_MSG;

	return res;
}
)

#if !defined(ast_strdupa) && defined(__GNUC__)
/*!
  \brief duplicate a string in memory from the stack
  \param s The string to duplicate

  This macro will duplicate the given string.  It returns a pointer to the stack
  allocatted memory for the new string.
*/
#define ast_strdupa(s)                                                    \
	(__extension__                                                    \
	({                                                                \
		const char *__old = (s);                                  \
		size_t __len = strlen(__old) + 1;                         \
		char *__new = __builtin_alloca(__len);                    \
		memcpy (__new, __old, __len);                             \
		__new;                                                    \
	}))
#endif


/* from config.c */

#define MAX_NESTED_COMMENTS 128
#define COMMENT_START ";--"
#define COMMENT_END "--;"
#define COMMENT_META ';'
#define COMMENT_TAG '-'

static char *extconfig_conf = "extconfig.conf";

/*! Growable string buffer */
static char *comment_buffer;   /*!< this will be a comment collector.*/
static int   comment_buffer_size;  /*!< the amount of storage so far alloc'd for the comment_buffer */

static char *lline_buffer;    /*!< A buffer for stuff behind the ; */
static int  lline_buffer_size;

#define CB_INCR 250

struct ast_comment {
	struct ast_comment *next;
	char cmt[0];
};

static void CB_INIT(void)
{
	if (!comment_buffer) {
		comment_buffer = ast_malloc(CB_INCR);
		if (!comment_buffer)
			return;
		comment_buffer[0] = 0;
		comment_buffer_size = CB_INCR;
		lline_buffer = ast_malloc(CB_INCR);
		if (!lline_buffer)
			return;
		lline_buffer[0] = 0;
		lline_buffer_size = CB_INCR;
	} else {
		comment_buffer[0] = 0;
		lline_buffer[0] = 0;
	}
}

static void  CB_ADD(char *str)
{
	int rem = comment_buffer_size - strlen(comment_buffer) - 1;
	int siz = strlen(str);
	if (rem < siz+1) {
		comment_buffer = ast_realloc(comment_buffer, comment_buffer_size + CB_INCR + siz + 1);
		if (!comment_buffer)
			return;
		comment_buffer_size += CB_INCR+siz+1;
	}
	strcat(comment_buffer,str);
}

static void  CB_ADD_LEN(char *str, int len)
{
	int cbl = strlen(comment_buffer) + 1;
	int rem = comment_buffer_size - cbl;
	if (rem < len+1) {
		comment_buffer = ast_realloc(comment_buffer, comment_buffer_size + CB_INCR + len + 1);
		if (!comment_buffer)
			return;
		comment_buffer_size += CB_INCR+len+1;
	}
	strncat(comment_buffer,str,len); /* safe */
	comment_buffer[cbl+len-1] = 0;
}

static void  LLB_ADD(char *str)
{
	int rem = lline_buffer_size - strlen(lline_buffer) - 1;
	int siz = strlen(str);
	if (rem < siz+1) {
		lline_buffer = ast_realloc(lline_buffer, lline_buffer_size + CB_INCR + siz + 1);
		if (!lline_buffer) 
			return;
		lline_buffer_size += CB_INCR + siz + 1;
	}
	strcat(lline_buffer,str);
}

static void CB_RESET(void )  
{ 
	comment_buffer[0] = 0; 
	lline_buffer[0] = 0;
}
		
/*! \brief Keep track of how many threads are currently trying to wait*() on
 *  a child process */
static unsigned int safe_system_level = 0;
static struct sigaction safe_system_prev_handler;

/*! \brief NULL handler so we can collect the child exit status */
static void _null_sig_handler(int sig)
{

}

static struct sigaction null_sig_handler = {
	.sa_handler = _null_sig_handler,
	.sa_flags = SA_RESTART,
};

void ast_replace_sigchld(void);

void ast_replace_sigchld(void)
{
	unsigned int level;

	level = safe_system_level++;

	/* only replace the handler if it has not already been done */
	if (level == 0) {
		sigaction(SIGCHLD, &null_sig_handler, &safe_system_prev_handler);
	}
}

void ast_unreplace_sigchld(void);

void ast_unreplace_sigchld(void)
{
	unsigned int level;

	level = --safe_system_level;

	/* only restore the handler if we are the last one */
	if (level == 0) {
		sigaction(SIGCHLD, &safe_system_prev_handler, NULL);
	}
}

int ast_safe_system(const char *s);

int ast_safe_system(const char *s)
{
	pid_t pid;
#ifdef HAVE_WORKING_FORK
	int x;
#endif
	int res;
	int status;

#if defined(HAVE_WORKING_FORK) || defined(HAVE_WORKING_VFORK)
	ast_replace_sigchld();

#ifdef HAVE_WORKING_FORK
	pid = fork();
#else
	pid = vfork();
#endif	

	if (pid == 0) {
#ifdef HAVE_WORKING_FORK
		/* Close file descriptors and launch system command */
		for (x = STDERR_FILENO + 1; x < 4096; x++)
			close(x);
#endif
		execl("/bin/sh", "/bin/sh", "-c", s, (char *) NULL);
		_exit(1);
	} else if (pid > 0) {
		for(;;) {
			res = waitpid(pid, &status, 0);
			if (res > -1) {
				res = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
				break;
			} else if (errno != EINTR) 
				break;
		}
	} else {
		ast_log(LOG_WARNING, "Fork failed: %s\n", strerror(errno));
		res = -1;
	}

	ast_unreplace_sigchld();
#else
	res = -1;
#endif

	return res;
}

static struct ast_comment *ALLOC_COMMENT(const char *buffer)
{ 
	struct ast_comment *x = ast_calloc(1,sizeof(struct ast_comment)+strlen(buffer)+1);
	strcpy(x->cmt, buffer);
	return x;
}

static struct ast_config_map {
	struct ast_config_map *next;
	char *name;
	char *driver;
	char *database;
	char *table;
	char stuff[0];
} *config_maps = NULL;

static struct ast_config_engine *config_engine_list;

#define MAX_INCLUDE_LEVEL 10


struct ast_category {
	char name[80];
	int ignored;			/*!< do not let user of the config see this category */
	int include_level;	
    char *file;                /*!< the file name from whence this declaration was read */
    int lineno;
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_variable *root;
	struct ast_variable *last;
	struct ast_category *next;
};

struct ast_config {
	struct ast_category *root;
	struct ast_category *last;
	struct ast_category *current;
	struct ast_category *last_browse;		/*!< used to cache the last category supplied via category_browse */
	int include_level;
	int max_include_level;
    struct ast_config_include *includes;  /*!< a list of inclusions, which should describe the entire tree */
};

struct ast_config_include {
	char *include_location_file;     /*!< file name in which the include occurs */
	int  include_location_lineno;    /*!< lineno where include occurred */
	int  exec;                       /*!< set to non-zero if itsa #exec statement */
	char *exec_file;                 /*!< if it's an exec, you'll have both the /var/tmp to read, and the original script */
	char *included_file;             /*!< file name included */
	int inclusion_count;             /*!< if the file is included more than once, a running count thereof -- but, worry not,
									   we explode the instances and will include those-- so all entries will be unique */
	int output;                      /*!< a flag to indicate if the inclusion has been output */
	struct ast_config_include *next; /*!< ptr to next inclusion in the list */
};

typedef struct ast_config *config_load_func(const char *database, const char *table, const char *configfile, struct ast_config *config, int withcomments, const char *suggested_include_file);
typedef struct ast_variable *realtime_var_get(const char *database, const char *table, va_list ap);
typedef struct ast_config *realtime_multi_get(const char *database, const char *table, va_list ap);
typedef int realtime_update(const char *database, const char *table, const char *keyfield, const char *entity, va_list ap);

/*! \brief Configuration engine structure, used to define realtime drivers */
struct ast_config_engine {
	char *name;
	config_load_func *load_func;
	realtime_var_get *realtime_func;
	realtime_multi_get *realtime_multi_func;
	realtime_update *update_func;
	struct ast_config_engine *next;
};

static struct ast_config_engine *config_engine_list;

/* taken from strings.h */

static force_inline int ast_strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}

#define S_OR(a, b)	(!ast_strlen_zero(a) ? (a) : (b))

AST_INLINE_API(
void ast_copy_string(char *dst, const char *src, size_t size),
{
	while (*src && size) {
		*dst++ = *src++;
		size--;
	}
	if (__builtin_expect(!size, 0))
		dst--;
	*dst = '\0';
}
)

AST_INLINE_API(
char *ast_skip_blanks(const char *str),
{
	while (*str && *str < 33)
		str++;
	return (char *)str;
}
)

/*!
  \brief Trims trailing whitespace characters from a string.
  \param ast_trim_blanks function being used
  \param str the input string
  \return a pointer to the modified string
 */
AST_INLINE_API(
char *ast_trim_blanks(char *str),
{
	char *work = str;

	if (work) {
		work += strlen(work) - 1;
		/* It's tempting to only want to erase after we exit this loop, 
		   but since ast_trim_blanks *could* receive a constant string
		   (which we presumably wouldn't have to touch), we shouldn't
		   actually set anything unless we must, and it's easier just
		   to set each position to \0 than to keep track of a variable
		   for it */
		while ((work >= str) && *work < 33)
			*(work--) = '\0';
	}
	return str;
}
)

/*!
  \brief Strip leading/trailing whitespace from a string.
  \param s The string to be stripped (will be modified).
  \return The stripped string.

  This functions strips all leading and trailing whitespace
  characters from the input string, and returns a pointer to
  the resulting string. The string is modified in place.
*/
AST_INLINE_API(
char *ast_strip(char *s),
{
	s = ast_skip_blanks(s);
	if (s)
		ast_trim_blanks(s);
	return s;
} 
)


/* from config.h */

struct ast_variable {
	char *name;
	char *value;
	char *file;
	int lineno;
	int object;		/*!< 0 for variable, 1 for object */
	int blanklines; 	/*!< Number of blanklines following entry */
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_variable *next;
	char stuff[0];
};

static const char *ast_variable_retrieve(const struct ast_config *config, const char *category, const char *variable);
static struct ast_config *config_text_file_load(const char *database, const char *table, const char *filename, struct ast_config *cfg, int withcomments, const char *suggested_include_file);

struct ast_config *localized_config_load_with_comments(const char *filename);
static char *ast_category_browse(struct ast_config *config, const char *prev);
static struct ast_variable *ast_variable_browse(const struct ast_config *config, const char *category);
static void ast_variables_destroy(struct ast_variable *v);
static void ast_config_destroy(struct ast_config *cfg);
static struct ast_config_include *ast_include_new(struct ast_config *conf, const char *from_file, const char *included_file, int is_exec, const char *exec_file, int from_lineno, char *real_included_file_name, int real_included_file_name_size);
static struct ast_config_include *ast_include_find(struct ast_config *conf, const char *included_file);
void localized_ast_include_rename(struct ast_config *conf, const char *from_file, const char *to_file);

static struct ast_variable *ast_variable_new(const char *name, const char *value, const char *filename);

static struct ast_variable *ast_variable_new(const char *name, const char *value, const char *filename) 
{
	struct ast_variable *variable;
	int name_len = strlen(name) + 1;	

	if ((variable = ast_calloc(1, name_len + strlen(value) + 1 + strlen(filename) + 1 + sizeof(*variable)))) {
		variable->name = variable->stuff;
		variable->value = variable->stuff + name_len;		
		variable->file = variable->value + strlen(value) + 1;		
		strcpy(variable->name,name);
		strcpy(variable->value,value);
		strcpy(variable->file,filename);
	}

	return variable;
}

static struct ast_config_include *ast_include_new(struct ast_config *conf, const char *from_file, const char *included_file, int is_exec, const char *exec_file, int from_lineno, char *real_included_file_name, int real_included_file_name_size)
{
	/* a file should be included ONCE. Otherwise, if one of the instances is changed,
       then all be changed. -- how do we know to include it? -- Handling modified 
       instances is possible, I'd have
       to create a new master for each instance. */
	struct ast_config_include *inc;
    
	inc = ast_include_find(conf, included_file);
	if (inc)
	{
		inc->inclusion_count++;
		snprintf(real_included_file_name, real_included_file_name_size, "%s~~%d", included_file, inc->inclusion_count);
		ast_log(LOG_WARNING,"'%s', line %d:  Same File included more than once! This data will be saved in %s if saved back to disk.\n", from_file, from_lineno, real_included_file_name);
	} else
		*real_included_file_name = 0;
	
	inc = ast_calloc(1,sizeof(struct ast_config_include));
	inc->include_location_file = ast_strdup(from_file);
	inc->include_location_lineno = from_lineno;
	if (!ast_strlen_zero(real_included_file_name))
		inc->included_file = ast_strdup(real_included_file_name);
	else
		inc->included_file = ast_strdup(included_file);
	
	inc->exec = is_exec;
	if (is_exec)
		inc->exec_file = ast_strdup(exec_file);
	
	/* attach this new struct to the conf struct */
	inc->next = conf->includes;
	conf->includes = inc;
    
	return inc;
}

void localized_ast_include_rename(struct ast_config *conf, const char *from_file, const char *to_file)
{
	struct ast_config_include *incl;
	struct ast_category *cat;
	struct ast_variable *v;
    
	int from_len = strlen(from_file);
	int to_len = strlen(to_file);
    
	if (strcmp(from_file, to_file) == 0) /* no use wasting time if the name is the same */
		return;
	
	/* the manager code allows you to read in one config file, then
       write it back out under a different name. But, the new arrangement
	   ties output lines to the file name. So, before you try to write
       the config file to disk, better riffle thru the data and make sure
       the file names are changed.
	*/
	/* file names are on categories, includes (of course), and on variables. So,
	   traverse all this and swap names */
	
	for (incl = conf->includes; incl; incl=incl->next) {
		if (strcmp(incl->include_location_file,from_file) == 0) {
			if (from_len >= to_len)
				strcpy(incl->include_location_file, to_file);
			else {
				free(incl->include_location_file);
				incl->include_location_file = strdup(to_file);
			}
		}
	}
	for (cat = conf->root; cat; cat = cat->next) {
		if (strcmp(cat->file,from_file) == 0) {
			if (from_len >= to_len)
				strcpy(cat->file, to_file);
			else {
				free(cat->file);
				cat->file = strdup(to_file);
			}
		}
		for (v = cat->root; v; v = v->next) {
			if (strcmp(v->file,from_file) == 0) {
				if (from_len >= to_len)
					strcpy(v->file, to_file);
				else {
					free(v->file);
					v->file = strdup(to_file);
				}
			}
		}
	}
}

static struct ast_config_include *ast_include_find(struct ast_config *conf, const char *included_file)
{
	struct ast_config_include *x;
	for (x=conf->includes;x;x=x->next)
	{
		if (strcmp(x->included_file,included_file) == 0)
			return x;
	}
	return 0;
}


static void ast_variable_append(struct ast_category *category, struct ast_variable *variable);

static void ast_variable_append(struct ast_category *category, struct ast_variable *variable)
{
	if (!variable)
		return;
	if (category->last)
		category->last->next = variable;
	else
		category->root = variable;
	category->last = variable;
	while (category->last->next)
		category->last = category->last->next;
}

static struct ast_category *category_get(const struct ast_config *config, const char *category_name, int ignored);

static struct ast_category *category_get(const struct ast_config *config, const char *category_name, int ignored)
{
	struct ast_category *cat;

	/* try exact match first, then case-insensitive match */
	for (cat = config->root; cat; cat = cat->next) {
		if (cat->name == category_name && (ignored || !cat->ignored))
			return cat;
	}

	for (cat = config->root; cat; cat = cat->next) {
		if (!strcasecmp(cat->name, category_name) && (ignored || !cat->ignored))
			return cat;
	}

	return NULL;
}

static struct ast_category *ast_category_get(const struct ast_config *config, const char *category_name)
{
	return category_get(config, category_name, 0);
}

static struct ast_variable *ast_variable_browse(const struct ast_config *config, const char *category)
{
	struct ast_category *cat = NULL;

	if (category && config->last_browse && (config->last_browse->name == category))
		cat = config->last_browse;
	else
		cat = ast_category_get(config, category);

	return (cat) ? cat->root : NULL;
}

static const char *ast_variable_retrieve(const struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category) {
		for (v = ast_variable_browse(config, category); v; v = v->next) {
			if (!strcasecmp(variable, v->name))
				return v->value;
		}
	} else {
		struct ast_category *cat;

		for (cat = config->root; cat; cat = cat->next)
			for (v = cat->root; v; v = v->next)
				if (!strcasecmp(variable, v->name))
					return v->value;
	}

	return NULL;
}

static struct ast_variable *variable_clone(const struct ast_variable *old)
{
	struct ast_variable *new = ast_variable_new(old->name, old->value, old->file);

	if (new) {
		new->lineno = old->lineno;
		new->object = old->object;
		new->blanklines = old->blanklines;
		/* TODO: clone comments? */
	}

	return new;
}
 
static void ast_variables_destroy(struct ast_variable *v)
{
	struct ast_variable *vn;

	while (v) {
		vn = v;
		v = v->next;
		free(vn);
	}
}

static void ast_includes_destroy(struct ast_config_include *incls)
{
	struct ast_config_include *incl,*inclnext;
    
	for (incl=incls; incl; incl = inclnext) {
		inclnext = incl->next;
		if (incl->include_location_file)
			free(incl->include_location_file);
		if (incl->exec_file)
			free(incl->exec_file);
		if (incl->included_file)
			free(incl->included_file);
		free(incl);
	}
}

static void ast_config_destroy(struct ast_config *cfg)
{
	struct ast_category *cat, *catn;

	if (!cfg)
		return;

	ast_includes_destroy(cfg->includes);
	
	cat = cfg->root;
	while (cat) {
		ast_variables_destroy(cat->root);
		catn = cat;
		cat = cat->next;
		free(catn);
	}
	free(cfg);
}

enum ast_option_flags {
	/*! Allow \#exec in config files */
	AST_OPT_FLAG_EXEC_INCLUDES = (1 << 0),
	/*! Do not fork() */
	AST_OPT_FLAG_NO_FORK = (1 << 1),
	/*! Keep quiet */
	AST_OPT_FLAG_QUIET = (1 << 2),
	/*! Console mode */
	AST_OPT_FLAG_CONSOLE = (1 << 3),
	/*! Run in realtime Linux priority */
	AST_OPT_FLAG_HIGH_PRIORITY = (1 << 4),
	/*! Initialize keys for RSA authentication */
	AST_OPT_FLAG_INIT_KEYS = (1 << 5),
	/*! Remote console */
	AST_OPT_FLAG_REMOTE = (1 << 6),
	/*! Execute an asterisk CLI command upon startup */
	AST_OPT_FLAG_EXEC = (1 << 7),
	/*! Don't use termcap colors */
	AST_OPT_FLAG_NO_COLOR = (1 << 8),
	/*! Are we fully started yet? */
	AST_OPT_FLAG_FULLY_BOOTED = (1 << 9),
	/*! Trascode via signed linear */
	AST_OPT_FLAG_TRANSCODE_VIA_SLIN = (1 << 10),
	/*! Dump core on a seg fault */
	AST_OPT_FLAG_DUMP_CORE = (1 << 12),
	/*! Cache sound files */
	AST_OPT_FLAG_CACHE_RECORD_FILES = (1 << 13),
	/*! Display timestamp in CLI verbose output */
	AST_OPT_FLAG_TIMESTAMP = (1 << 14),
	/*! Override config */
	AST_OPT_FLAG_OVERRIDE_CONFIG = (1 << 15),
	/*! Reconnect */
	AST_OPT_FLAG_RECONNECT = (1 << 16),
	/*! Transmit Silence during Record() and DTMF Generation */
	AST_OPT_FLAG_TRANSMIT_SILENCE = (1 << 17),
	/*! Suppress some warnings */
	AST_OPT_FLAG_DONT_WARN = (1 << 18),
	/*! End CDRs before the 'h' extension */
	AST_OPT_FLAG_END_CDR_BEFORE_H_EXTEN = (1 << 19),
	/*! Always fork, even if verbose or debug settings are non-zero */
	AST_OPT_FLAG_ALWAYS_FORK = (1 << 21),
	/*! Disable log/verbose output to remote consoles */
	AST_OPT_FLAG_MUTE = (1 << 22),
	/*! There is a per-file debug setting */
	AST_OPT_FLAG_DEBUG_FILE = (1 << 23),
	/*! There is a per-file verbose setting */
	AST_OPT_FLAG_VERBOSE_FILE = (1 << 24),
	/*! Terminal colors should be adjusted for a light-colored background */
	AST_OPT_FLAG_LIGHT_BACKGROUND = (1 << 25),
	/*! Count Initiated seconds in CDR's */
	AST_OPT_FLAG_INITIATED_SECONDS = (1 << 26),
	/*! Force black background */
	AST_OPT_FLAG_FORCE_BLACK_BACKGROUND = (1 << 27),
};

/* options.h declares ast_options extern; I need it static? */
#define AST_CACHE_DIR_LEN 	512
#define AST_FILENAME_MAX	80

/*! These are the options that set by default when Asterisk starts */
#define AST_DEFAULT_OPTIONS AST_OPT_FLAG_TRANSCODE_VIA_SLIN

struct ast_flags ast_options = { AST_DEFAULT_OPTIONS };

#define ast_opt_exec_includes		ast_test_flag(&ast_options, AST_OPT_FLAG_EXEC_INCLUDES)
#define ast_opt_no_fork			ast_test_flag(&ast_options, AST_OPT_FLAG_NO_FORK)
#define ast_opt_quiet			ast_test_flag(&ast_options, AST_OPT_FLAG_QUIET)
#define ast_opt_console			ast_test_flag(&ast_options, AST_OPT_FLAG_CONSOLE)
#define ast_opt_high_priority		ast_test_flag(&ast_options, AST_OPT_FLAG_HIGH_PRIORITY)
#define ast_opt_init_keys		ast_test_flag(&ast_options, AST_OPT_FLAG_INIT_KEYS)
#define ast_opt_remote			ast_test_flag(&ast_options, AST_OPT_FLAG_REMOTE)
#define ast_opt_exec			ast_test_flag(&ast_options, AST_OPT_FLAG_EXEC)
#define ast_opt_no_color		ast_test_flag(&ast_options, AST_OPT_FLAG_NO_COLOR)
#define ast_fully_booted		ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)
#define ast_opt_transcode_via_slin	ast_test_flag(&ast_options, AST_OPT_FLAG_TRANSCODE_VIA_SLIN)
#define ast_opt_priority_jumping	ast_test_flag(&ast_options, AST_OPT_FLAG_PRIORITY_JUMPING)
#define ast_opt_dump_core		ast_test_flag(&ast_options, AST_OPT_FLAG_DUMP_CORE)
#define ast_opt_cache_record_files	ast_test_flag(&ast_options, AST_OPT_FLAG_CACHE_RECORD_FILES)
#define ast_opt_timestamp		ast_test_flag(&ast_options, AST_OPT_FLAG_TIMESTAMP)
#define ast_opt_override_config		ast_test_flag(&ast_options, AST_OPT_FLAG_OVERRIDE_CONFIG)
#define ast_opt_reconnect		ast_test_flag(&ast_options, AST_OPT_FLAG_RECONNECT)
#define ast_opt_transmit_silence	ast_test_flag(&ast_options, AST_OPT_FLAG_TRANSMIT_SILENCE)
#define ast_opt_dont_warn		ast_test_flag(&ast_options, AST_OPT_FLAG_DONT_WARN)
#define ast_opt_end_cdr_before_h_exten	ast_test_flag(&ast_options, AST_OPT_FLAG_END_CDR_BEFORE_H_EXTEN)
#define ast_opt_always_fork		ast_test_flag(&ast_options, AST_OPT_FLAG_ALWAYS_FORK)
#define ast_opt_mute			ast_test_flag(&ast_options, AST_OPT_FLAG_MUTE)

extern int option_verbose;
extern int option_debug;		/*!< Debugging */
extern int ast_option_maxcalls;		/*!< Maximum number of simultaneous channels */
extern double ast_option_maxload;
extern char ast_defaultlanguage[];

extern pid_t ast_mainpid;

extern char record_cache_dir[AST_CACHE_DIR_LEN];
extern char debug_filename[AST_FILENAME_MAX];

extern int ast_language_is_prefix;



/* linkedlists.h */

/*!
  \brief Write locks a list.
  \param head This is a pointer to the list head structure

  This macro attempts to place an exclusive write lock in the
  list head structure pointed to by head.
  Returns non-zero on success, 0 on failure
*/
#define AST_RWLIST_WRLOCK(head)                                         \
        ast_rwlock_wrlock(&(head)->lock)

/*!
  \brief Read locks a list.
  \param head This is a pointer to the list head structure

  This macro attempts to place a read lock in the
  list head structure pointed to by head.
  Returns non-zero on success, 0 on failure
*/
#define AST_RWLIST_RDLOCK(head)                                         \
        ast_rwlock_rdlock(&(head)->lock)
	
/*!
  \brief Attempts to unlock a read/write based list.
  \param head This is a pointer to the list head structure

  This macro attempts to remove a read or write lock from the
  list head structure pointed to by head. If the list
  was not locked by this thread, this macro has no effect.
*/
#define AST_RWLIST_UNLOCK(head)                                         \
        ast_rwlock_unlock(&(head)->lock)

/*!
  \brief Defines a structure to be used to hold a list of specified type.
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type. It does not actually
  declare (allocate) a structure; to do that, either follow this
  macro with the desired name of the instance you wish to declare,
  or use the specified \a name to declare instances elsewhere.

  Example usage:
  \code
  static AST_LIST_HEAD(entry_list, entry) entries;
  \endcode

  This would define \c struct \c entry_list, and declare an instance of it named
  \a entries, all intended to hold a list of type \c struct \c entry.
*/
#define AST_LIST_HEAD(name, type)					\
struct name {								\
	struct type *first;						\
	struct type *last;						\
	ast_mutex_t lock;						\
}

/*!
  \brief Defines a structure to be used to hold a read/write list of specified type.
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type. It does not actually
  declare (allocate) a structure; to do that, either follow this
  macro with the desired name of the instance you wish to declare,
  or use the specified \a name to declare instances elsewhere.

  Example usage:
  \code
  static AST_RWLIST_HEAD(entry_list, entry) entries;
  \endcode

  This would define \c struct \c entry_list, and declare an instance of it named
  \a entries, all intended to hold a list of type \c struct \c entry.
*/
#define AST_RWLIST_HEAD(name, type)                                     \
struct name {                                                           \
        struct type *first;                                             \
        struct type *last;                                              \
        ast_rwlock_t lock;                                              \
}

/*!
  \brief Defines a structure to be used to hold a list of specified type (with no lock).
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type. It does not actually
  declare (allocate) a structure; to do that, either follow this
  macro with the desired name of the instance you wish to declare,
  or use the specified \a name to declare instances elsewhere.

  Example usage:
  \code
  static AST_LIST_HEAD_NOLOCK(entry_list, entry) entries;
  \endcode

  This would define \c struct \c entry_list, and declare an instance of it named
  \a entries, all intended to hold a list of type \c struct \c entry.
*/
#define AST_LIST_HEAD_NOLOCK(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
}

/*!
  \brief Defines initial values for a declaration of AST_LIST_HEAD
*/
#define AST_LIST_HEAD_INIT_VALUE	{		\
	.first = NULL,					\
	.last = NULL,					\
	.lock = AST_MUTEX_INIT_VALUE,			\
	}

/*!
  \brief Defines initial values for a declaration of AST_RWLIST_HEAD
*/
#define AST_RWLIST_HEAD_INIT_VALUE      {               \
        .first = NULL,                                  \
        .last = NULL,                                   \
        .lock = AST_RWLOCK_INIT_VALUE,                  \
        }

/*!
  \brief Defines initial values for a declaration of AST_LIST_HEAD_NOLOCK
*/
#define AST_LIST_HEAD_NOLOCK_INIT_VALUE	{	\
	.first = NULL,					\
	.last = NULL,					\
	}

/*!
  \brief Defines a structure to be used to hold a list of specified type, statically initialized.
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type, and allocates an instance
  of it, initialized to be empty.

  Example usage:
  \code
  static AST_LIST_HEAD_STATIC(entry_list, entry);
  \endcode

  This would define \c struct \c entry_list, intended to hold a list of
  type \c struct \c entry.
*/
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS)
#define AST_LIST_HEAD_STATIC(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
	ast_mutex_t lock;						\
} name;									\
static void  __attribute__((constructor)) init_##name(void)		\
{									\
        AST_LIST_HEAD_INIT(&name);					\
}									\
static void  __attribute__((destructor)) fini_##name(void)		\
{									\
        AST_LIST_HEAD_DESTROY(&name);					\
}									\
struct __dummy_##name
#else
#define AST_LIST_HEAD_STATIC(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
	ast_mutex_t lock;						\
} name = AST_LIST_HEAD_INIT_VALUE
#endif

/*!
  \brief Defines a structure to be used to hold a read/write list of specified type, statically initialized.
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type, and allocates an instance
  of it, initialized to be empty.

  Example usage:
  \code
  static AST_RWLIST_HEAD_STATIC(entry_list, entry);
  \endcode

  This would define \c struct \c entry_list, intended to hold a list of
  type \c struct \c entry.
*/
#ifndef AST_RWLOCK_INIT_VALUE
#define AST_RWLIST_HEAD_STATIC(name, type)                              \
struct name {                                                           \
        struct type *first;                                             \
        struct type *last;                                              \
        ast_rwlock_t lock;                                              \
} name;                                                                 \
static void  __attribute__((constructor)) init_##name(void)            \
{                                                                       \
        AST_RWLIST_HEAD_INIT(&name);                                    \
}                                                                       \
static void  __attribute__((destructor)) fini_##name(void)             \
{                                                                       \
        AST_RWLIST_HEAD_DESTROY(&name);                                 \
}                                                                       \
struct __dummy_##name
#else
#define AST_RWLIST_HEAD_STATIC(name, type)                              \
struct name {                                                           \
        struct type *first;                                             \
        struct type *last;                                              \
        ast_rwlock_t lock;                                              \
} name = AST_RWLIST_HEAD_INIT_VALUE
#endif

/*!
  \brief Defines a structure to be used to hold a list of specified type, statically initialized.

  This is the same as AST_LIST_HEAD_STATIC, except without the lock included.
*/
#define AST_LIST_HEAD_NOLOCK_STATIC(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
} name = AST_LIST_HEAD_NOLOCK_INIT_VALUE

/*!
  \brief Initializes a list head structure with a specified first entry.
  \param head This is a pointer to the list head structure
  \param entry pointer to the list entry that will become the head of the list

  This macro initializes a list head structure by setting the head
  entry to the supplied value and recreating the embedded lock.
*/
#define AST_LIST_HEAD_SET(head, entry) do {				\
	(head)->first = (entry);					\
	(head)->last = (entry);						\
	ast_mutex_init(&(head)->lock);					\
} while (0)

/*!
  \brief Initializes an rwlist head structure with a specified first entry.
  \param head This is a pointer to the list head structure
  \param entry pointer to the list entry that will become the head of the list

  This macro initializes a list head structure by setting the head
  entry to the supplied value and recreating the embedded lock.
*/
#define AST_RWLIST_HEAD_SET(head, entry) do {                           \
        (head)->first = (entry);                                        \
        (head)->last = (entry);                                         \
        ast_rwlock_init(&(head)->lock);                                 \
} while (0)

/*!
  \brief Initializes a list head structure with a specified first entry.
  \param head This is a pointer to the list head structure
  \param entry pointer to the list entry that will become the head of the list

  This macro initializes a list head structure by setting the head
  entry to the supplied value.
*/
#define AST_LIST_HEAD_SET_NOLOCK(head, entry) do {			\
	(head)->first = (entry);					\
	(head)->last = (entry);						\
} while (0)

/*!
  \brief Declare a forward link structure inside a list entry.
  \param type This is the type of each list entry.

  This macro declares a structure to be used to link list entries together.
  It must be used inside the definition of the structure named in
  \a type, as follows:

  \code
  struct list_entry {
  	...
  	AST_LIST_ENTRY(list_entry) list;
  }
  \endcode

  The field name \a list here is arbitrary, and can be anything you wish.
*/
#define AST_LIST_ENTRY(type)						\
struct {								\
	struct type *next;						\
}

#define AST_RWLIST_ENTRY AST_LIST_ENTRY
 
/*!
  \brief Returns the first entry contained in a list.
  \param head This is a pointer to the list head structure
 */
#define	AST_LIST_FIRST(head)	((head)->first)

#define AST_RWLIST_FIRST AST_LIST_FIRST

/*!
  \brief Returns the last entry contained in a list.
  \param head This is a pointer to the list head structure
 */
#define	AST_LIST_LAST(head)	((head)->last)

#define AST_RWLIST_LAST AST_LIST_LAST

/*!
  \brief Returns the next entry in the list after the given entry.
  \param elm This is a pointer to the current entry.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
*/
#define AST_LIST_NEXT(elm, field)	((elm)->field.next)

#define AST_RWLIST_NEXT AST_LIST_NEXT

/*!
  \brief Checks whether the specified list contains any entries.
  \param head This is a pointer to the list head structure

  Returns non-zero if the list has entries, zero if not.
 */
#define	AST_LIST_EMPTY(head)	(AST_LIST_FIRST(head) == NULL)

#define AST_RWLIST_EMPTY AST_LIST_EMPTY

/*!
  \brief Loops over (traverses) the entries in a list.
  \param head This is a pointer to the list head structure
  \param var This is the name of the variable that will hold a pointer to the
  current list entry on each iteration. It must be declared before calling
  this macro.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  This macro is use to loop over (traverse) the entries in a list. It uses a
  \a for loop, and supplies the enclosed code with a pointer to each list
  entry as it loops. It is typically used as follows:
  \code
  static AST_LIST_HEAD(entry_list, list_entry) entries;
  ...
  struct list_entry {
  	...
  	AST_LIST_ENTRY(list_entry) list;
  }
  ...
  struct list_entry *current;
  ...
  AST_LIST_TRAVERSE(&entries, current, list) {
     (do something with current here)
  }
  \endcode
  \warning If you modify the forward-link pointer contained in the \a current entry while
  inside the loop, the behavior will be unpredictable. At a minimum, the following
  macros will modify the forward-link pointer, and should not be used inside
  AST_LIST_TRAVERSE() against the entry pointed to by the \a current pointer without
  careful consideration of their consequences:
  \li AST_LIST_NEXT() (when used as an lvalue)
  \li AST_LIST_INSERT_AFTER()
  \li AST_LIST_INSERT_HEAD()
  \li AST_LIST_INSERT_TAIL()
*/
#define AST_LIST_TRAVERSE(head,var,field) 				\
	for((var) = (head)->first; (var); (var) = (var)->field.next)

#define AST_RWLIST_TRAVERSE AST_LIST_TRAVERSE

/*!
  \brief Loops safely over (traverses) the entries in a list.
  \param head This is a pointer to the list head structure
  \param var This is the name of the variable that will hold a pointer to the
  current list entry on each iteration. It must be declared before calling
  this macro.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  This macro is used to safely loop over (traverse) the entries in a list. It
  uses a \a for loop, and supplies the enclosed code with a pointer to each list
  entry as it loops. It is typically used as follows:

  \code
  static AST_LIST_HEAD(entry_list, list_entry) entries;
  ...
  struct list_entry {
  	...
  	AST_LIST_ENTRY(list_entry) list;
  }
  ...
  struct list_entry *current;
  ...
  AST_LIST_TRAVERSE_SAFE_BEGIN(&entries, current, list) {
     (do something with current here)
  }
  AST_LIST_TRAVERSE_SAFE_END;
  \endcode

  It differs from AST_LIST_TRAVERSE() in that the code inside the loop can modify
  (or even free, after calling AST_LIST_REMOVE_CURRENT()) the entry pointed to by
  the \a current pointer without affecting the loop traversal.
*/
#define AST_LIST_TRAVERSE_SAFE_BEGIN(head, var, field) {				\
	typeof((head)->first) __list_next;						\
	typeof((head)->first) __list_prev = NULL;					\
	typeof((head)->first) __new_prev = NULL;					\
	for ((var) = (head)->first, __new_prev = (var),					\
	      __list_next = (var) ? (var)->field.next : NULL;				\
	     (var);									\
	     __list_prev = __new_prev, (var) = __list_next,				\
	     __new_prev = (var),							\
	     __list_next = (var) ? (var)->field.next : NULL				\
	    )

#define AST_RWLIST_TRAVERSE_SAFE_BEGIN AST_LIST_TRAVERSE_SAFE_BEGIN

/*!
  \brief Removes the \a current entry from a list during a traversal.
  \param head This is a pointer to the list head structure
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  \note This macro can \b only be used inside an AST_LIST_TRAVERSE_SAFE_BEGIN()
  block; it is used to unlink the current entry from the list without affecting
  the list traversal (and without having to re-traverse the list to modify the
  previous entry, if any).
 */
#define AST_LIST_REMOVE_CURRENT(head, field)						\
	__new_prev->field.next = NULL;							\
	__new_prev = __list_prev;							\
	if (__list_prev)								\
		__list_prev->field.next = __list_next;					\
	else										\
		(head)->first = __list_next;						\
	if (!__list_next)								\
		(head)->last = __list_prev;

#define AST_RWLIST_REMOVE_CURRENT AST_LIST_REMOVE_CURRENT

/*!
  \brief Inserts a list entry before the current entry during a traversal.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be inserted.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  \note This macro can \b only be used inside an AST_LIST_TRAVERSE_SAFE_BEGIN()
  block.
 */
#define AST_LIST_INSERT_BEFORE_CURRENT(head, elm, field) do {		\
	if (__list_prev) {						\
		(elm)->field.next = __list_prev->field.next;		\
		__list_prev->field.next = elm;				\
	} else {							\
		(elm)->field.next = (head)->first;			\
		(head)->first = (elm);					\
	}								\
	__new_prev = (elm);						\
} while (0)

#define AST_RWLIST_INSERT_BEFORE_CURRENT AST_LIST_INSERT_BEFORE_CURRENT

/*!
  \brief Closes a safe loop traversal block.
 */
#define AST_LIST_TRAVERSE_SAFE_END  }

#define AST_RWLIST_TRAVERSE_SAFE_END AST_LIST_TRAVERSE_SAFE_END

/*!
  \brief Initializes a list head structure.
  \param head This is a pointer to the list head structure

  This macro initializes a list head structure by setting the head
  entry to \a NULL (empty list) and recreating the embedded lock.
*/
#define AST_LIST_HEAD_INIT(head) {					\
	(head)->first = NULL;						\
	(head)->last = NULL;						\
	ast_mutex_init(&(head)->lock);					\
}

/*!
  \brief Initializes an rwlist head structure.
  \param head This is a pointer to the list head structure

  This macro initializes a list head structure by setting the head
  entry to \a NULL (empty list) and recreating the embedded lock.
*/
#define AST_RWLIST_HEAD_INIT(head) {                                    \
        (head)->first = NULL;                                           \
        (head)->last = NULL;                                            \
        ast_rwlock_init(&(head)->lock);                                 \
}

/*!
  \brief Destroys an rwlist head structure.
  \param head This is a pointer to the list head structure

  This macro destroys a list head structure by setting the head
  entry to \a NULL (empty list) and destroying the embedded lock.
  It does not free the structure from memory.
*/
#define AST_RWLIST_HEAD_DESTROY(head) {                                 \
        (head)->first = NULL;                                           \
        (head)->last = NULL;                                            \
        ast_rwlock_destroy(&(head)->lock);                              \
}

/*!
  \brief Initializes a list head structure.
  \param head This is a pointer to the list head structure

  This macro initializes a list head structure by setting the head
  entry to \a NULL (empty list). There is no embedded lock handling
  with this macro.
*/
#define AST_LIST_HEAD_INIT_NOLOCK(head) {				\
	(head)->first = NULL;						\
	(head)->last = NULL;						\
}

/*!
  \brief Inserts a list entry after a given entry.
  \param head This is a pointer to the list head structure
  \param listelm This is a pointer to the entry after which the new entry should
  be inserted.
  \param elm This is a pointer to the entry to be inserted.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
 */
#define AST_LIST_INSERT_AFTER(head, listelm, elm, field) do {		\
	(elm)->field.next = (listelm)->field.next;			\
	(listelm)->field.next = (elm);					\
	if ((head)->last == (listelm))					\
		(head)->last = (elm);					\
} while (0)

#define AST_RWLIST_INSERT_AFTER AST_LIST_INSERT_AFTER

/*!
  \brief Inserts a list entry at the head of a list.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be inserted.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
 */
#define AST_LIST_INSERT_HEAD(head, elm, field) do {			\
		(elm)->field.next = (head)->first;			\
		(head)->first = (elm);					\
		if (!(head)->last)					\
			(head)->last = (elm);				\
} while (0)

#define AST_RWLIST_INSERT_HEAD AST_LIST_INSERT_HEAD

/*!
  \brief Appends a list entry to the tail of a list.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be appended.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  Note: The link field in the appended entry is \b not modified, so if it is
  actually the head of a list itself, the entire list will be appended
  temporarily (until the next AST_LIST_INSERT_TAIL is performed).
 */
#define AST_LIST_INSERT_TAIL(head, elm, field) do {			\
      if (!(head)->first) {						\
		(head)->first = (elm);					\
		(head)->last = (elm);					\
      } else {								\
		(head)->last->field.next = (elm);			\
		(head)->last = (elm);					\
      }									\
} while (0)

#define AST_RWLIST_INSERT_TAIL AST_LIST_INSERT_TAIL

/*!
  \brief Appends a whole list to the tail of a list.
  \param head This is a pointer to the list head structure
  \param list This is a pointer to the list to be appended.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
 */
#define AST_LIST_APPEND_LIST(head, list, field) do {			\
      if (!(head)->first) {						\
		(head)->first = (list)->first;				\
		(head)->last = (list)->last;				\
      } else {								\
		(head)->last->field.next = (list)->first;		\
		(head)->last = (list)->last;				\
      }									\
} while (0)

#define AST_RWLIST_APPEND_LIST AST_LIST_APPEND_LIST

/*!
  \brief Removes and returns the head entry from a list.
  \param head This is a pointer to the list head structure
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  Removes the head entry from the list, and returns a pointer to it.
  This macro is safe to call on an empty list.
 */
#define AST_LIST_REMOVE_HEAD(head, field) ({				\
		typeof((head)->first) cur = (head)->first;		\
		if (cur) {						\
			(head)->first = cur->field.next;		\
			cur->field.next = NULL;				\
			if ((head)->last == cur)			\
				(head)->last = NULL;			\
		}							\
		cur;							\
	})

#define AST_RWLIST_REMOVE_HEAD AST_LIST_REMOVE_HEAD

/*!
  \brief Removes a specific entry from a list.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be removed.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
  \warning The removed entry is \b not freed nor modified in any way.
 */
#define AST_LIST_REMOVE(head, elm, field) do {			        \
	if ((head)->first == (elm)) {					\
		(head)->first = (elm)->field.next;			\
		if ((head)->last == (elm))			\
			(head)->last = NULL;			\
	} else {								\
		typeof(elm) curelm = (head)->first;			\
		while (curelm && (curelm->field.next != (elm)))			\
			curelm = curelm->field.next;			\
		if (curelm) { \
			curelm->field.next = (elm)->field.next;			\
			if ((head)->last == (elm))				\
				(head)->last = curelm;				\
		} \
	}								\
        (elm)->field.next = NULL;                                       \
} while (0)

#define AST_RWLIST_REMOVE AST_LIST_REMOVE

/* chanvars.h */

struct ast_var_t {
	AST_LIST_ENTRY(ast_var_t) entries;
	char *value;
	char name[0];
};

AST_LIST_HEAD_NOLOCK(varshead, ast_var_t);

AST_RWLOCK_DEFINE_STATIC(globalslock);
static struct varshead globals = AST_LIST_HEAD_NOLOCK_INIT_VALUE;


/* IN CONFLICT: struct ast_var_t *ast_var_assign(const char *name, const char *value); */

static struct ast_var_t *ast_var_assign(const char *name, const char *value);

static void ast_var_delete(struct ast_var_t *var);

/*from channel.h */
#define AST_MAX_EXTENSION  80      /*!< Max length of an extension */


/* from pbx.h */
#define PRIORITY_HINT	-1	/*!< Special Priority for a hint */

enum ast_extension_states {
	AST_EXTENSION_REMOVED = -2,	/*!< Extension removed */
	AST_EXTENSION_DEACTIVATED = -1,	/*!< Extension hint removed */
	AST_EXTENSION_NOT_INUSE = 0,	/*!< No device INUSE or BUSY  */
	AST_EXTENSION_INUSE = 1 << 0,	/*!< One or more devices INUSE */
	AST_EXTENSION_BUSY = 1 << 1,	/*!< All devices BUSY */
	AST_EXTENSION_UNAVAILABLE = 1 << 2, /*!< All devices UNAVAILABLE/UNREGISTERED */
	AST_EXTENSION_RINGING = 1 << 3,	/*!< All devices RINGING */
	AST_EXTENSION_ONHOLD = 1 << 4,	/*!< All devices ONHOLD */
};

struct ast_custom_function {
	const char *name;		/*!< Name */
	const char *synopsis;		/*!< Short description for "show functions" */
	const char *desc;		/*!< Help text that explains it all */
	const char *syntax;		/*!< Syntax description */
	int (*read)(struct ast_channel *, const char *, char *, char *, size_t);	/*!< Read function, if read is supported */
	int (*write)(struct ast_channel *, const char *, char *, const char *);		/*!< Write function, if write is supported */
	AST_RWLIST_ENTRY(ast_custom_function) acflist;
};

typedef int (ast_switch_f)(struct ast_channel *chan, const char *context,
	const char *exten, int priority, const char *callerid, const char *data);

struct ast_switch {
	AST_LIST_ENTRY(ast_switch) list;
	const char *name;			/*!< Name of the switch */
	const char *description;		/*!< Description of the switch */
	
	ast_switch_f *exists;
	ast_switch_f *canmatch;
	ast_switch_f *exec;
	ast_switch_f *matchmore;
};


static char *config_filename = "extensions.conf";
static char *global_registrar = "conf2ael";
static char userscontext[AST_MAX_EXTENSION] = "default";
static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 0;
static int clearglobalvars_config = 0;
static void pbx_substitute_variables_helper(struct ast_channel *c,const char *cp1,char *cp2,int count);


/* stolen from callerid.c */

/*! \brief Clean up phone string
 * remove '(', ' ', ')', non-trailing '.', and '-' not in square brackets.
 * Basically, remove anything that could be invalid in a pattern.
 */
static void ast_shrink_phone_number(char *n)
{
	int x, y=0;
	int bracketed = 0;

	for (x=0; n[x]; x++) {
		switch(n[x]) {
		case '[':
			bracketed++;
			n[y++] = n[x];
			break;
		case ']':
			bracketed--;
			n[y++] = n[x];
			break;
		case '-':
			if (bracketed)
				n[y++] = n[x];
			break;
		case '.':
			if (!n[x+1])
				n[y++] = n[x];
			break;
		default:
			if (!strchr("()", n[x]))
				n[y++] = n[x];
		}
	}
	n[y] = '\0';
}


/* stolen from chanvars.c */

static const char *ast_var_name(const struct ast_var_t *var)
{
	const char *name;

	if (var == NULL || (name = var->name) == NULL)
		return NULL;
	/* Return the name without the initial underscores */
	if (name[0] == '_') {
		name++;
		if (name[0] == '_')
			name++;
	}
	return name;
}

/* experiment 1: see if it's easier just to use existing config code
 *               to read in the extensions.conf file. In this scenario, 
                 I have to rip/copy code from other modules, because they
                 are staticly declared as-is. A solution would be to move
                 the ripped code to another location and make them available
                 to other modules and standalones */

/* Our own version of ast_log, since the expr parser uses it. -- stolen from utils/check_expr.c */

static void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	va_list vars;
	va_start(vars,fmt);
	
	printf("LOG: lev:%d file:%s  line:%d func: %s  ",
		   level, file, line, function);
	vprintf(fmt, vars);
	fflush(stdout);
	va_end(vars);
}

void __attribute__((format(printf, 1, 2))) ast_verbose(const char *fmt, ...)
{
	va_list vars;
	va_start(vars,fmt);
	
	printf("VERBOSE: ");
	vprintf(fmt, vars);
	fflush(stdout);
	va_end(vars);
}

/* stolen from main/utils.c */
static char *ast_process_quotes_and_slashes(char *start, char find, char replace_with)
{
 	char *dataPut = start;
	int inEscape = 0;
	int inQuotes = 0;

	for (; *start; start++) {
		if (inEscape) {
			*dataPut++ = *start;       /* Always goes verbatim */
			inEscape = 0;
		} else {
			if (*start == '\\') {
				inEscape = 1;      /* Do not copy \ into the data */
			} else if (*start == '\'') {
				inQuotes = 1 - inQuotes;   /* Do not copy ' into the data */
			} else {
				/* Replace , with |, unless in quotes */
				*dataPut++ = inQuotes ? *start : ((*start == find) ? replace_with : *start);
			}
		}
	}
	if (start != dataPut)
		*dataPut = 0;
	return dataPut;
}

static int ast_true(const char *s)
{
	if (ast_strlen_zero(s))
		return 0;

	/* Determine if this is a true value */
	if (!strcasecmp(s, "yes") ||
	    !strcasecmp(s, "true") ||
	    !strcasecmp(s, "y") ||
	    !strcasecmp(s, "t") ||
	    !strcasecmp(s, "1") ||
	    !strcasecmp(s, "on"))
		return -1;

	return 0;
}

#define ONE_MILLION	1000000
/*
 * put timeval in a valid range. usec is 0..999999
 * negative values are not allowed and truncated.
 */
static struct timeval tvfix(struct timeval a)
{
	if (a.tv_usec >= ONE_MILLION) {
		ast_log(LOG_WARNING, "warning too large timestamp %ld.%ld\n",
			(long)a.tv_sec, (long int) a.tv_usec);
		a.tv_sec += a.tv_usec / ONE_MILLION;
		a.tv_usec %= ONE_MILLION;
	} else if (a.tv_usec < 0) {
		ast_log(LOG_WARNING, "warning negative timestamp %ld.%ld\n",
			(long)a.tv_sec, (long int) a.tv_usec);
		a.tv_usec = 0;
	}
	return a;
}

struct timeval ast_tvadd(struct timeval a, struct timeval b);
struct timeval ast_tvadd(struct timeval a, struct timeval b)
{
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec += b.tv_sec;
	a.tv_usec += b.tv_usec;
	if (a.tv_usec >= ONE_MILLION) {
		a.tv_sec++;
		a.tv_usec -= ONE_MILLION;
	}
	return a;
}

struct timeval ast_tvsub(struct timeval a, struct timeval b);
struct timeval ast_tvsub(struct timeval a, struct timeval b)
{
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec -= b.tv_sec;
	a.tv_usec -= b.tv_usec;
	if (a.tv_usec < 0) {
		a.tv_sec-- ;
		a.tv_usec += ONE_MILLION;
	}
	return a;
}
#undef ONE_MILLION

void ast_mark_lock_failed(void *lock_addr);
void ast_mark_lock_failed(void *lock_addr)
{
	/* Pretend to do something. */
}

/* stolen from pbx.c */
#define VAR_BUF_SIZE 4096

#define	VAR_NORMAL		1
#define	VAR_SOFTTRAN	2
#define	VAR_HARDTRAN	3

#define BACKGROUND_SKIP		(1 << 0)
#define BACKGROUND_NOANSWER	(1 << 1)
#define BACKGROUND_MATCHEXTEN	(1 << 2)
#define BACKGROUND_PLAYBACK	(1 << 3)

/*!
   \brief ast_exten: An extension
	The dialplan is saved as a linked list with each context
	having it's own linked list of extensions - one item per
	priority.
*/
struct ast_exten {
	char *exten;			/*!< Extension name */
	int matchcid;			/*!< Match caller id ? */
	const char *cidmatch;		/*!< Caller id to match for this extension */
	int priority;			/*!< Priority */
	const char *label;		/*!< Label */
	struct ast_context *parent;	/*!< The context this extension belongs to  */
	const char *app; 		/*!< Application to execute */
	struct ast_app *cached_app;     /*!< Cached location of application */
	void *data;			/*!< Data to use (arguments) */
	void (*datad)(void *);		/*!< Data destructor */
	struct ast_exten *peer;		/*!< Next higher priority with our extension */
	const char *registrar;		/*!< Registrar */
	struct ast_exten *next;		/*!< Extension with a greater ID */
	char stuff[0];
};
/* from pbx.h */
typedef int (*ast_state_cb_type)(char *context, char* id, enum ast_extension_states state, void *data);
struct ast_timing {
	int hastime;				/*!< If time construct exists */
	unsigned int monthmask;			/*!< Mask for month */
	unsigned int daymask;			/*!< Mask for date */
	unsigned int dowmask;			/*!< Mask for day of week (mon-sun) */
	unsigned int minmask[48];		/*!< Mask for minute */
	char *timezone;                 /*!< NULL, or zoneinfo style timezone */
};
/* end of pbx.h */
/*! \brief ast_include: include= support in extensions.conf */
struct ast_include {
	const char *name;
	const char *rname;			/*!< Context to include */
	const char *registrar;			/*!< Registrar */
	int hastime;				/*!< If time construct exists */
	struct ast_timing timing;               /*!< time construct */
	struct ast_include *next;		/*!< Link them together */
	char stuff[0];
};

/*! \brief ast_sw: Switch statement in extensions.conf */
struct ast_sw {
	char *name;
	const char *registrar;			/*!< Registrar */
	char *data;				/*!< Data load */
	int eval;
	AST_LIST_ENTRY(ast_sw) list;
	char *tmpdata;
	char stuff[0];
};

/*! \brief ast_ignorepat: Ignore patterns in dial plan */
struct ast_ignorepat {
	const char *registrar;
	struct ast_ignorepat *next;
	char pattern[0];
};

/*! \brief ast_context: An extension context */
struct ast_context {
	ast_rwlock_t lock; 			/*!< A lock to prevent multiple threads from clobbering the context */
	struct ast_exten *root;			/*!< The root of the list of extensions */
	struct ast_context *next;		/*!< Link them together */
	struct ast_include *includes;		/*!< Include other contexts */
	struct ast_ignorepat *ignorepats;	/*!< Patterns for which to continue playing dialtone */
	const char *registrar;			/*!< Registrar */
	AST_LIST_HEAD_NOLOCK(, ast_sw) alts;	/*!< Alternative switches */
	ast_mutex_t macrolock;			/*!< A lock to implement "exclusive" macros - held whilst a call is executing in the macro */
	char name[0];				/*!< Name of the context */
};


/*! \brief ast_app: A registered application */
struct ast_app {
	int (*execute)(struct ast_channel *chan, void *data);
	const char *synopsis;			/*!< Synopsis text for 'show applications' */
	const char *description;		/*!< Description (help text) for 'show application &lt;name&gt;' */
	AST_RWLIST_ENTRY(ast_app) list;		/*!< Next app in list */
	void *module;			/*!< Module this app belongs to */
	char name[0];				/*!< Name of the application */
};


/*! \brief ast_state_cb: An extension state notify register item */
struct ast_state_cb {
	int id;
	void *data;
	ast_state_cb_type callback;
	struct ast_state_cb *next;
};

/*! \brief Structure for dial plan hints

  \note Hints are pointers from an extension in the dialplan to one or
  more devices (tech/name) 
	- See \ref AstExtState
*/
struct ast_hint {
	struct ast_exten *exten;	/*!< Extension */
	int laststate; 			/*!< Last known state */
	struct ast_state_cb *callbacks;	/*!< Callback list for this extension */
	AST_RWLIST_ENTRY(ast_hint) list;/*!< Pointer to next hint in list */
};

struct store_hint {
	char *context;
	char *exten;
	struct ast_state_cb *callbacks;
	int laststate;
	AST_LIST_ENTRY(store_hint) list;
	char data[1];
};

AST_LIST_HEAD(store_hints, store_hint);

#define STATUS_NO_CONTEXT	1
#define STATUS_NO_EXTENSION	2
#define STATUS_NO_PRIORITY	3
#define STATUS_NO_LABEL		4
#define STATUS_SUCCESS		5

static struct ast_var_t *ast_var_assign(const char *name, const char *value)
{	
	struct ast_var_t *var;
	int name_len = strlen(name) + 1;
	int value_len = strlen(value) + 1;

	if (!(var = ast_calloc(sizeof(*var) + name_len + value_len, sizeof(char)))) {
		return NULL;
	}

	ast_copy_string(var->name, name, name_len);
	var->value = var->name + name_len;
	ast_copy_string(var->value, value, value_len);
	
	return var;
}	
	
static void ast_var_delete(struct ast_var_t *var)
{
	free(var);
}


/* chopped this one off at the knees! */
static int ast_func_write(struct ast_channel *chan, const char *function, const char *value)
{

	/* ast_log(LOG_ERROR, "Function %s not registered\n", function); we are not interested in the details here */

	return -1;
}

static unsigned int ast_app_separate_args(char *buf, char delim, char **array, int arraylen)
{
	int argc;
	char *scan;
	int paren = 0, quote = 0;

	if (!buf || !array || !arraylen)
		return 0;

	memset(array, 0, arraylen * sizeof(*array));

	scan = buf;

	for (argc = 0; *scan && (argc < arraylen - 1); argc++) {
		array[argc] = scan;
		for (; *scan; scan++) {
			if (*scan == '(')
				paren++;
			else if (*scan == ')') {
				if (paren)
					paren--;
			} else if (*scan == '"' && delim != '"') {
				quote = quote ? 0 : 1;
				/* Remove quote character from argument */
				memmove(scan, scan + 1, strlen(scan));
				scan--;
			} else if (*scan == '\\') {
				/* Literal character, don't parse */
				memmove(scan, scan + 1, strlen(scan));
			} else if ((*scan == delim) && !paren && !quote) {
				*scan++ = '\0';
				break;
			}
		}
	}

	if (*scan)
		array[argc++] = scan;

	return argc;
}

static void pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *nametail = name;

	/* XXX may need locking on the channel ? */
	if (name[strlen(name)-1] == ')') {
		char *function = ast_strdupa(name);

		ast_func_write(chan, function, value);
		return;
	}

	headp = &globals;

	/* For comparison purposes, we have to strip leading underscores */
	if (*nametail == '_') {
		nametail++;
		if (*nametail == '_')
			nametail++;
	}

	AST_LIST_TRAVERSE (headp, newvariable, entries) {
		if (strcasecmp(ast_var_name(newvariable), nametail) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE(headp, newvariable, entries);
			ast_var_delete(newvariable);
			break;
		}
	}

	if (value && (newvariable = ast_var_assign(name, value))) {
		if ((option_verbose > 1) && (headp == &globals))
			ast_verbose(VERBOSE_PREFIX_2 "Setting global variable '%s' to '%s'\n", name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

}

static int pbx_builtin_setvar(struct ast_channel *chan, const void *data)
{
	char *name, *value, *mydata;
	int argc;
	char *argv[24];		/* this will only support a maximum of 24 variables being set in a single operation */
	int global = 0;
	int x;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Set requires at least one variable name/value pair.\n");
		return 0;
	}

	mydata = ast_strdupa(data);
	argc = ast_app_separate_args(mydata, '|', argv, sizeof(argv) / sizeof(argv[0]));

	/* check for a trailing flags argument */
	if ((argc > 1) && !strchr(argv[argc-1], '=')) {
		argc--;
		if (strchr(argv[argc], 'g'))
			global = 1;
	}

	for (x = 0; x < argc; x++) {
		name = argv[x];
		if ((value = strchr(name, '='))) {
			*value++ = '\0';
			pbx_builtin_setvar_helper((global) ? NULL : chan, name, value);
		} else
			ast_log(LOG_WARNING, "Ignoring entry '%s' with no = (and not last 'options' entry)\n", name);
	}

	return(0);
}

int localized_pbx_builtin_setvar(struct ast_channel *chan, const void *data);

int localized_pbx_builtin_setvar(struct ast_channel *chan, const void *data)
{
	return pbx_builtin_setvar(chan, data);
}


/*! \brief Helper for get_range.
 * return the index of the matching entry, starting from 1.
 * If names is not supplied, try numeric values.
 */
static int lookup_name(const char *s, char *const names[], int max)
{
	int i;

	if (names && *s > '9') {
		for (i = 0; names[i]; i++) {
			if (!strcasecmp(s, names[i])) {
				return i;
			}
		}
	}

	/* Allow months and weekdays to be specified as numbers, as well */
	if (sscanf(s, "%2d", &i) == 1 && i >= 1 && i <= max) {
		/* What the array offset would have been: "1" would be at offset 0 */
		return i - 1;
	}
	return -1; /* error return */
}

/*! \brief helper function to return a range up to max (7, 12, 31 respectively).
 * names, if supplied, is an array of names that should be mapped to numbers.
 */
static unsigned get_range(char *src, int max, char *const names[], const char *msg)
{
	int start, end; /* start and ending position */
	unsigned int mask = 0;
	char *part;

	/* Check for whole range */
	if (ast_strlen_zero(src) || !strcmp(src, "*")) {
		return (1 << max) - 1;
	}

	while ((part = strsep(&src, "&"))) {
		/* Get start and ending position */
		char *endpart = strchr(part, '-');
		if (endpart) {
			*endpart++ = '\0';
		}
		/* Find the start */
		if ((start = lookup_name(part, names, max)) < 0) {
			ast_log(LOG_WARNING, "Invalid %s '%s', skipping element\n", msg, part);
			continue;
		}
		if (endpart) { /* find end of range */
			if ((end = lookup_name(endpart, names, max)) < 0) {
				ast_log(LOG_WARNING, "Invalid end %s '%s', skipping element\n", msg, endpart);
				continue;
			}
		} else {
			end = start;
		}
		/* Fill the mask. Remember that ranges are cyclic */
		mask |= (1 << end);   /* initialize with last element */
		while (start != end) {
			if (start >= max) {
				start = 0;
			}
			mask |= (1 << start);
			start++;
		}
	}
	return mask;
}

/*! \brief store a bitmask of valid times, one bit each 2 minute */
static void get_timerange(struct ast_timing *i, char *times)
{
	char *endpart, *part;
	int x;
	int st_h, st_m;
	int endh, endm;
	int minute_start, minute_end;

	/* start disabling all times, fill the fields with 0's, as they may contain garbage */
	memset(i->minmask, 0, sizeof(i->minmask));

	/* 1-minute per bit */
	/* Star is all times */
	if (ast_strlen_zero(times) || !strcmp(times, "*")) {
		/* 48, because each hour takes 2 integers; 30 bits each */
		for (x = 0; x < 48; x++) {
			i->minmask[x] = 0x3fffffff; /* 30 bits */
		}
		return;
	}
	/* Otherwise expect a range */
	while ((part = strsep(&times, "&"))) {
		if (!(endpart = strchr(part, '-'))) {
			if (sscanf(part, "%2d:%2d", &st_h, &st_m) != 2 || st_h < 0 || st_h > 23 || st_m < 0 || st_m > 59) {
				ast_log(LOG_WARNING, "%s isn't a valid time.\n", part);
				continue;
			}
			i->minmask[st_h * 2 + (st_m >= 30 ? 1 : 0)] |= (1 << (st_m % 30));
			continue;
		}
		*endpart++ = '\0';
		/* why skip non digits? Mostly to skip spaces */
		while (*endpart && !isdigit(*endpart)) {
			endpart++;
		}
		if (!*endpart) {
			ast_log(LOG_WARNING, "Invalid time range starting with '%s-'.\n", part);
			continue;
		}
		if (sscanf(part, "%2d:%2d", &st_h, &st_m) != 2 || st_h < 0 || st_h > 23 || st_m < 0 || st_m > 59) {
			ast_log(LOG_WARNING, "'%s' isn't a valid start time.\n", part);
			continue;
		}
		if (sscanf(endpart, "%2d:%2d", &endh, &endm) != 2 || endh < 0 || endh > 23 || endm < 0 || endm > 59) {
			ast_log(LOG_WARNING, "'%s' isn't a valid end time.\n", endpart);
			continue;
		}
		minute_start = st_h * 60 + st_m;
		minute_end = endh * 60 + endm;
		/* Go through the time and enable each appropriate bit */
		for (x = minute_start; x != minute_end; x = (x + 1) % (24 * 60)) {
			i->minmask[x / 30] |= (1 << (x % 30));
		}
		/* Do the last one */
		i->minmask[x / 30] |= (1 << (x % 30));
	}
	/* All done */
	return;
}

static void null_datad(void *foo)
{
}

/*! \brief Find realtime engine for realtime family */
static struct ast_config_engine *find_engine(const char *family, char *database, int dbsiz, char *table, int tabsiz) 
{
	struct ast_config_engine *eng, *ret = NULL;
	struct ast_config_map *map;


	for (map = config_maps; map; map = map->next) {
		if (!strcasecmp(family, map->name)) {
			if (database)
				ast_copy_string(database, map->database, dbsiz);
			if (table)
				ast_copy_string(table, map->table ? map->table : family, tabsiz);
			break;
		}
	}

	/* Check if the required driver (engine) exist */
	if (map) {
		for (eng = config_engine_list; !ret && eng; eng = eng->next) {
			if (!strcasecmp(eng->name, map->driver))
				ret = eng;
		}
	}
	
	
	/* if we found a mapping, but the engine is not available, then issue a warning */
	if (map && !ret)
		ast_log(LOG_WARNING, "Realtime mapping for '%s' found to engine '%s', but the engine is not available\n", map->name, map->driver);
	
	return ret;
}

struct ast_category *ast_config_get_current_category(const struct ast_config *cfg);

struct ast_category *ast_config_get_current_category(const struct ast_config *cfg)
{
	return cfg->current;
}

static struct ast_category *ast_category_new(const char *name, const char *in_file, int lineno);

static struct ast_category *ast_category_new(const char *name, const char *in_file, int lineno)
{
	struct ast_category *category;

	if ((category = ast_calloc(1, sizeof(*category))))
		ast_copy_string(category->name, name, sizeof(category->name));
	category->file = strdup(in_file);
	category->lineno = lineno; /* if you don't know the lineno, set it to 999999 or something real big */
 	return category;
}

struct ast_category *localized_category_get(const struct ast_config *config, const char *category_name);

struct ast_category *localized_category_get(const struct ast_config *config, const char *category_name)
{
	return category_get(config, category_name, 0);
}

static void move_variables(struct ast_category *old, struct ast_category *new)
{
	struct ast_variable *var = old->root;
	old->root = NULL;
#if 1
	/* we can just move the entire list in a single op */
	ast_variable_append(new, var);
#else
	while (var) {
		struct ast_variable *next = var->next;
		var->next = NULL;
		ast_variable_append(new, var);
		var = next;
	}
#endif
}

static void inherit_category(struct ast_category *new, const struct ast_category *base)
{
	struct ast_variable *var;

	for (var = base->root; var; var = var->next)
		ast_variable_append(new, variable_clone(var));
}

static void ast_category_append(struct ast_config *config, struct ast_category *category);

static void ast_category_append(struct ast_config *config, struct ast_category *category)
{
	if (config->last)
		config->last->next = category;
	else
		config->root = category;
	config->last = category;
	config->current = category;
}

static void ast_category_destroy(struct ast_category *cat);

static void ast_category_destroy(struct ast_category *cat)
{
	ast_variables_destroy(cat->root);
	if (cat->file)
		free(cat->file);
	
	free(cat);
}

static struct ast_config_engine text_file_engine = {
	.name = "text",
	.load_func = config_text_file_load,
};


static struct ast_config *ast_config_internal_load(const char *filename, struct ast_config *cfg, int withcomments, const char *suggested_incl_file);

static struct ast_config *ast_config_internal_load(const char *filename, struct ast_config *cfg, int withcomments, const char *suggested_incl_file)
{
	char db[256];
	char table[256];
	struct ast_config_engine *loader = &text_file_engine;
	struct ast_config *result; 

	if (cfg->include_level == cfg->max_include_level) {
		ast_log(LOG_WARNING, "Maximum Include level (%d) exceeded\n", cfg->max_include_level);
		return NULL;
	}

	cfg->include_level++;
	/*  silence is golden!
		ast_log(LOG_WARNING, "internal loading file %s level=%d\n", filename, cfg->include_level);
	*/

	if (strcmp(filename, extconfig_conf) && strcmp(filename, "asterisk.conf") && config_engine_list) {
		struct ast_config_engine *eng;

		eng = find_engine(filename, db, sizeof(db), table, sizeof(table));


		if (eng && eng->load_func) {
			loader = eng;
		} else {
			eng = find_engine("global", db, sizeof(db), table, sizeof(table));
			if (eng && eng->load_func)
				loader = eng;
		}
	}

	result = loader->load_func(db, table, filename, cfg, withcomments, suggested_incl_file);
	/* silence is golden 
	   ast_log(LOG_WARNING, "finished internal loading file %s level=%d\n", filename, cfg->include_level);
	*/

	if (result)
		result->include_level--;

	return result;
}


static int process_text_line(struct ast_config *cfg, struct ast_category **cat, char *buf, int lineno, const char *configfile, int withcomments, const char *suggested_include_file)
{
	char *c;
	char *cur = buf;
	struct ast_variable *v;
	char cmd[512], exec_file[512];
	int object, do_exec, do_include;

	/* Actually parse the entry */
	if (cur[0] == '[') {
		struct ast_category *newcat = NULL;
		char *catname;

		/* A category header */
		c = strchr(cur, ']');
		if (!c) {
			ast_log(LOG_WARNING, "parse error: no closing ']', line %d of %s\n", lineno, configfile);
			return -1;
		}
		*c++ = '\0';
		cur++;
 		if (*c++ != '(')
 			c = NULL;
		catname = cur;
		if (!(*cat = newcat = ast_category_new(catname, ast_strlen_zero(suggested_include_file)?configfile:suggested_include_file, lineno))) {
			return -1;
		}
		(*cat)->lineno = lineno;
        
		/* add comments */
		if (withcomments && comment_buffer && comment_buffer[0] ) {
			newcat->precomments = ALLOC_COMMENT(comment_buffer);
		}
		if (withcomments && lline_buffer && lline_buffer[0] ) {
			newcat->sameline = ALLOC_COMMENT(lline_buffer);
		}
		if( withcomments )
			CB_RESET();
		
 		/* If there are options or categories to inherit from, process them now */
 		if (c) {
 			if (!(cur = strchr(c, ')'))) {
 				ast_log(LOG_WARNING, "parse error: no closing ')', line %d of %s\n", lineno, configfile);
 				return -1;
 			}
 			*cur = '\0';
 			while ((cur = strsep(&c, ","))) {
				if (!strcasecmp(cur, "!")) {
					(*cat)->ignored = 1;
				} else if (!strcasecmp(cur, "+")) {
					*cat = category_get(cfg, catname, 1);
					if (!*cat) {
						ast_config_destroy(cfg);
						if (newcat)
							ast_category_destroy(newcat);
						ast_log(LOG_WARNING, "Category addition requested, but category '%s' does not exist, line %d of %s\n", catname, lineno, configfile);
						return -1;
					}
					if (newcat) {
						move_variables(newcat, *cat);
						ast_category_destroy(newcat);
						newcat = NULL;
					}
				} else {
					struct ast_category *base;
 				
					base = category_get(cfg, cur, 1);
					if (!base) {
						ast_log(LOG_WARNING, "Inheritance requested, but category '%s' does not exist, line %d of %s\n", cur, lineno, configfile);
						return -1;
					}
					inherit_category(*cat, base);
				}
 			}
 		}
		if (newcat)
			ast_category_append(cfg, *cat);
	} else if (cur[0] == '#') {
		/* A directive */
		cur++;
		c = cur;
		while(*c && (*c > 32)) c++;
		if (*c) {
			*c = '\0';
			/* Find real argument */
			c = ast_skip_blanks(c + 1);
			if (!*c)
				c = NULL;
		} else 
			c = NULL;
		do_include = !strcasecmp(cur, "include");
		if(!do_include)
			do_exec = !strcasecmp(cur, "exec");
		else
			do_exec = 0;
		if (do_exec && !ast_opt_exec_includes) {
			ast_log(LOG_WARNING, "Cannot perform #exec unless execincludes option is enabled in asterisk.conf (options section)!\n");
			do_exec = 0;
		}
		if (do_include || do_exec) {
			if (c) {
				char *cur2;
				char real_inclusion_name[256];
                
				/* Strip off leading and trailing "'s and <>'s */
				while((*c == '<') || (*c == '>') || (*c == '\"')) c++;
				/* Get rid of leading mess */
				cur = c;
				cur2 = cur;
				while (!ast_strlen_zero(cur)) {
					c = cur + strlen(cur) - 1;
					if ((*c == '>') || (*c == '<') || (*c == '\"'))
						*c = '\0';
					else
						break;
				}
				/* #exec </path/to/executable>
				   We create a tmp file, then we #include it, then we delete it. */
				if (do_exec) { 
					snprintf(exec_file, sizeof(exec_file), "/var/tmp/exec.%d.%ld", (int)time(NULL), (long)pthread_self());
					snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", cur, exec_file);
					ast_safe_system(cmd);
					cur = exec_file;
				} else
					exec_file[0] = '\0';
				/* A #include */
				/* ast_log(LOG_WARNING, "Reading in included file %s withcomments=%d\n", cur, withcomments); */
				
				/* record this inclusion */
				ast_include_new(cfg, configfile, cur, do_exec, cur2, lineno, real_inclusion_name, sizeof(real_inclusion_name));
				
				do_include = ast_config_internal_load(cur, cfg, withcomments, real_inclusion_name) ? 1 : 0;
				if(!ast_strlen_zero(exec_file))
					unlink(exec_file);
				if(!do_include)
					return 0;
				/* ast_log(LOG_WARNING, "Done reading in included file %s withcomments=%d\n", cur, withcomments); */
				
			} else {
				ast_log(LOG_WARNING, "Directive '#%s' needs an argument (%s) at line %d of %s\n", 
						do_exec ? "exec" : "include",
						do_exec ? "/path/to/executable" : "filename",
						lineno,
						configfile);
			}
		}
		else 
			ast_log(LOG_WARNING, "Unknown directive '%s' at line %d of %s\n", cur, lineno, configfile);
	} else {
		/* Just a line (variable = value) */
		if (!*cat) {
			ast_log(LOG_WARNING,
				"parse error: No category context for line %d of %s\n", lineno, configfile);
			return -1;
		}
		c = strchr(cur, '=');
		if (c) {
			*c = 0;
			c++;
			/* Ignore > in => */
			if (*c== '>') {
				object = 1;
				c++;
			} else
				object = 0;
			if ((v = ast_variable_new(ast_strip(cur), ast_strip(c), configfile))) {
				v->lineno = lineno;
				v->object = object;
				/* Put and reset comments */
				v->blanklines = 0;
				ast_variable_append(*cat, v);
				/* add comments */
				if (withcomments && comment_buffer && comment_buffer[0] ) {
					v->precomments = ALLOC_COMMENT(comment_buffer);
				}
				if (withcomments && lline_buffer && lline_buffer[0] ) {
					v->sameline = ALLOC_COMMENT(lline_buffer);
				}
				if( withcomments )
					CB_RESET();
				
			} else {
				return -1;
			}
		} else {
			ast_log(LOG_WARNING, "EXTENSIONS.CONF: No '=' (equal sign) in line %d of %s\n", lineno, configfile);
		}
	}
	return 0;
}

static int use_local_dir = 1;

void localized_use_local_dir(void);
void localized_use_conf_dir(void);

void localized_use_local_dir(void)
{
	use_local_dir = 1;
}

void localized_use_conf_dir(void)
{
	use_local_dir = 0;
}


static struct ast_config *config_text_file_load(const char *database, const char *table, const char *filename, struct ast_config *cfg, int withcomments, const char *suggested_include_file)
{
	char fn[256];
	char buf[8192];
	char *new_buf, *comment_p, *process_buf;
	FILE *f;
	int lineno=0;
	int comment = 0, nest[MAX_NESTED_COMMENTS];
	struct ast_category *cat = NULL;
	int count = 0;
	struct stat statbuf;
	
	cat = ast_config_get_current_category(cfg);

	if (filename[0] == '/') {
		ast_copy_string(fn, filename, sizeof(fn));
	} else {
		if (use_local_dir)
			snprintf(fn, sizeof(fn), "./%s", filename);
		else
			snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_CONFIG_DIR, filename);
	}

	if (withcomments && cfg && cfg->include_level < 2 ) {
		CB_INIT();
	}
	
#ifdef AST_INCLUDE_GLOB
	{
		int glob_ret;
		glob_t globbuf;

		globbuf.gl_offs = 0;	/* initialize it to silence gcc */
#ifdef SOLARIS
		glob_ret = glob(fn, GLOB_NOCHECK, NULL, &globbuf);
#else
		glob_ret = glob(fn, GLOB_NOMAGIC|GLOB_BRACE, NULL, &globbuf);
#endif
		if (glob_ret == GLOB_NOSPACE)
			ast_log(LOG_WARNING,
				"Glob Expansion of pattern '%s' failed: Not enough memory\n", fn);
		else if (glob_ret  == GLOB_ABORTED)
			ast_log(LOG_WARNING,
				"Glob Expansion of pattern '%s' failed: Read error\n", fn);
		else  {
			/* loop over expanded files */
			int i;
			for (i=0; i<globbuf.gl_pathc; i++) {
				ast_copy_string(fn, globbuf.gl_pathv[i], sizeof(fn));
#endif
	do {
		if (stat(fn, &statbuf))
			continue;

		if (!S_ISREG(statbuf.st_mode)) {
			ast_log(LOG_WARNING, "'%s' is not a regular file, ignoring\n", fn);
			continue;
		}
		if (option_verbose > 1) {
			ast_verbose(VERBOSE_PREFIX_2 "Parsing '%s': ", fn);
			fflush(stdout);
		}
		if (!(f = fopen(fn, "r"))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "No file to parse: %s\n", fn);
			if (option_verbose > 1)
				ast_verbose( "Not found (%s)\n", strerror(errno));
			continue;
		}
		count++;
		if (option_debug)
			ast_log(LOG_DEBUG, "Parsing %s\n", fn);
		if (option_verbose > 1)
			ast_verbose("Found\n");
		while(!feof(f)) {
			lineno++;
			if (fgets(buf, sizeof(buf), f)) {
				if ( withcomments ) {    
					CB_ADD(lline_buffer);       /* add the current lline buffer to the comment buffer */
					lline_buffer[0] = 0;        /* erase the lline buffer */
				}
				
				new_buf = buf;
				if (comment) 
					process_buf = NULL;
				else
					process_buf = buf;
				
				while ((comment_p = strchr(new_buf, COMMENT_META))) {
					if ((comment_p > new_buf) && (*(comment_p-1) == '\\')) {
						/* Yuck, gotta memmove */
						memmove(comment_p - 1, comment_p, strlen(comment_p) + 1);
						new_buf = comment_p;
					} else if(comment_p[1] == COMMENT_TAG && comment_p[2] == COMMENT_TAG && (comment_p[3] != '-')) {
						/* Meta-Comment start detected ";--" */
						if (comment < MAX_NESTED_COMMENTS) {
							*comment_p = '\0';
							new_buf = comment_p + 3;
							comment++;
							nest[comment-1] = lineno;
						} else {
							ast_log(LOG_ERROR, "Maximum nest limit of %d reached.\n", MAX_NESTED_COMMENTS);
						}
					} else if ((comment_p >= new_buf + 2) &&
						   (*(comment_p - 1) == COMMENT_TAG) &&
						   (*(comment_p - 2) == COMMENT_TAG)) {
						/* Meta-Comment end detected */
						comment--;
						new_buf = comment_p + 1;
						if (!comment) {
							/* Back to non-comment now */
							if (process_buf) {
								/* Actually have to move what's left over the top, then continue */
								char *oldptr;
								oldptr = process_buf + strlen(process_buf);
								if ( withcomments ) {
									CB_ADD(";");
									CB_ADD_LEN(oldptr+1,new_buf-oldptr-1);
								}
								
								memmove(oldptr, new_buf, strlen(new_buf) + 1);
								new_buf = oldptr;
							} else
								process_buf = new_buf;
						}
					} else {
						if (!comment) {
							/* If ; is found, and we are not nested in a comment, 
							   we immediately stop all comment processing */
							if ( withcomments ) {
								LLB_ADD(comment_p);
							}
							*comment_p = '\0'; 
							new_buf = comment_p;
						} else
							new_buf = comment_p + 1;
					}
				}
				if( withcomments && comment && !process_buf )
				{
					CB_ADD(buf);  /* the whole line is a comment, store it */
				}
				
				if (process_buf) {
					char *stripped_process_buf = ast_strip(process_buf);
					if (!ast_strlen_zero(stripped_process_buf)) {
						if (process_text_line(cfg, &cat, stripped_process_buf, lineno, filename, withcomments, suggested_include_file)) {
							cfg = NULL;
							break;
						}
					}
				}
			}
		}
		fclose(f);		
	} while(0);
	if (comment) {
		ast_log(LOG_WARNING,"Unterminated comment detected beginning on line %d\n", nest[comment]);
	}
#ifdef AST_INCLUDE_GLOB
					if (!cfg)
						break;
				}
				globfree(&globbuf);
			}
		}
#endif
	if (cfg && cfg->include_level == 1 && withcomments && comment_buffer) {
		if (comment_buffer) { 
			free(comment_buffer);
			free(lline_buffer);
			comment_buffer=0; 
			lline_buffer=0; 
			comment_buffer_size=0; 
			lline_buffer_size=0;
		}
	}
	if (count == 0)
		return NULL;

	return cfg;
}


static struct ast_config *ast_config_new(void) ;

static struct ast_config *ast_config_new(void) 
{
	struct ast_config *config;

	if ((config = ast_calloc(1, sizeof(*config))))
		config->max_include_level = MAX_INCLUDE_LEVEL;
	return config;
}

struct ast_config *localized_config_load(const char *filename);

struct ast_config *localized_config_load(const char *filename)
{
	struct ast_config *cfg;
	struct ast_config *result;

	cfg = ast_config_new();
	if (!cfg)
		return NULL;

	result = ast_config_internal_load(filename, cfg, 0, "");
	if (!result)
		ast_config_destroy(cfg);

	return result;
}

struct ast_config *localized_config_load_with_comments(const char *filename);

struct ast_config *localized_config_load_with_comments(const char *filename)
{
	struct ast_config *cfg;
	struct ast_config *result;

	cfg = ast_config_new();
	if (!cfg)
		return NULL;

	result = ast_config_internal_load(filename, cfg, 1, "");
	if (!result)
		ast_config_destroy(cfg);

	return result;
}

static struct ast_category *next_available_category(struct ast_category *cat)
{
	for (; cat && cat->ignored; cat = cat->next);

	return cat;
}

static char *ast_category_browse(struct ast_config *config, const char *prev)
{	
	struct ast_category *cat = NULL;

	if (prev && config->last_browse && (config->last_browse->name == prev))
		cat = config->last_browse->next;
	else if (!prev && config->root)
		cat = config->root;
	else if (prev) {
		for (cat = config->root; cat; cat = cat->next) {
			if (cat->name == prev) {
				cat = cat->next;
				break;
			}
		}
		if (!cat) {
			for (cat = config->root; cat; cat = cat->next) {
				if (!strcasecmp(cat->name, prev)) {
					cat = cat->next;
					break;
				}
			}
		}
	}
	
	if (cat)
		cat = next_available_category(cat);

	config->last_browse = cat;
	return (cat) ? cat->name : NULL;
}



void ast_config_set_current_category(struct ast_config *cfg, const struct ast_category *cat);

void ast_config_set_current_category(struct ast_config *cfg, const struct ast_category *cat)
{
	/* cast below is just to silence compiler warning about dropping "const" */
	cfg->current = (struct ast_category *) cat;
}

/* NOTE: categories and variables each have a file and lineno attribute. On a save operation, these are used to determine
   which file and line number to write out to. Thus, an entire hierarchy of config files (via #include statements) can be
   recreated. BUT, care must be taken to make sure that every cat and var has the proper file name stored, or you may
   be shocked and mystified as to why things are not showing up in the files! 
   
   Also, All #include/#exec statements are recorded in the "includes" LL in the ast_config structure. The file name
   and line number are stored for each include, plus the name of the file included, so that these statements may be
   included in the output files on a file_save operation. 
   
   The lineno's are really just for relative placement in the file. There is no attempt to make sure that blank lines
   are included to keep the lineno's the same between input and output. The lineno fields are used mainly to determine
   the position of the #include and #exec directives. So, blank lines tend to disappear from a read/rewrite operation,
   and a header gets added.
   
   vars and category heads are output in the order they are stored in the config file. So, if the software
   shuffles these at all, then the placement of #include directives might get a little mixed up, because the
   file/lineno data probably won't get changed.
   
*/

static void gen_header(FILE *f1, const char *configfile, const char *fn, const char *generator)
{
	char date[256]="";
	time_t t;
	time(&t);
	ast_copy_string(date, ctime(&t), sizeof(date));
	
	fprintf(f1, ";!\n");
	fprintf(f1, ";! Automatically generated configuration file\n");
	if (strcmp(configfile, fn))
		fprintf(f1, ";! Filename: %s (%s)\n", configfile, fn);
	else
		fprintf(f1, ";! Filename: %s\n", configfile);
	fprintf(f1, ";! Generator: %s\n", generator);
	fprintf(f1, ";! Creation Date: %s", date);
	fprintf(f1, ";!\n");
}

static void set_fn(char *fn, int fn_size, const char *file, const char *configfile)
{
	if (!file || file[0] == 0) {
		if (configfile[0] == '/')
			ast_copy_string(fn, configfile, fn_size);
		else
			snprintf(fn, fn_size, "%s/%s", ast_config_AST_CONFIG_DIR, configfile);
	} else if (file[0] == '/') 
		ast_copy_string(fn, file, fn_size);
	else
		snprintf(fn, fn_size, "%s/%s", ast_config_AST_CONFIG_DIR, file);
}

int localized_config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator);

int localized_config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator)
{
	FILE *f;
	char fn[256];
	struct ast_variable *var;
	struct ast_category *cat;
	struct ast_comment *cmt;
	struct ast_config_include *incl;
	int blanklines = 0;
	
	/* reset all the output flags, in case this isn't our first time saving this data */
	
	for (incl=cfg->includes; incl; incl = incl->next)
		incl->output = 0;
	
	/* go thru all the inclusions and make sure all the files involved (configfile plus all its inclusions)
	   are all truncated to zero bytes and have that nice header*/
	
	for (incl=cfg->includes; incl; incl = incl->next)
	{
		if (!incl->exec) { /* leave the execs alone -- we'll write out the #exec directives, but won't zero out the include files or exec files*/
			FILE *f1;
			
			set_fn(fn, sizeof(fn), incl->included_file, configfile); /* normally, fn is just set to incl->included_file, prepended with config dir if relative */
			f1 = fopen(fn,"w");
			if (f1) {
				gen_header(f1, configfile, fn, generator);
				fclose(f1); /* this should zero out the file */
			} else {
				ast_verbose(VERBOSE_PREFIX_2 "Unable to write %s (%s)", fn, strerror(errno));
			}
		}
	}
	
	set_fn(fn, sizeof(fn), 0, configfile); /* just set fn to absolute ver of configfile */
#ifdef __CYGWIN__	
	if ((f = fopen(fn, "w+"))) {
#else
	if ((f = fopen(fn, "w"))) {
#endif	    
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Saving '%s': ", fn);

		gen_header(f, configfile, fn, generator);
		cat = cfg->root;
		fclose(f);
        
		/* from here out, we open each involved file and concat the stuff we need to add to the end and immediately close... */
		/* since each var, cat, and associated comments can come from any file, we have to be 
		   mobile, and open each file, print, and close it on an entry-by-entry basis */
		
		while(cat) {
			set_fn(fn, sizeof(fn), cat->file, configfile);
			f = fopen(fn, "a");
			if (!f)
			{
				ast_verbose(VERBOSE_PREFIX_2 "Unable to write %s (%s)", fn, strerror(errno));
				return -1;
			}
			
			/* dump any includes that happen before this category header */
			for (incl=cfg->includes; incl; incl = incl->next) {
				if (strcmp(incl->include_location_file, cat->file) == 0){
					if (cat->lineno > incl->include_location_lineno && !incl->output) {
						if (incl->exec)
							fprintf(f,"#exec \"%s\"\n", incl->exec_file);
						else
							fprintf(f,"#include \"%s\"\n", incl->included_file);
						incl->output = 1;
					}
				}
			}
            
			/* Dump section with any appropriate comment */
			for (cmt = cat->precomments; cmt; cmt=cmt->next) {
				if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
					fprintf(f,"%s", cmt->cmt);
			}
			if (!cat->precomments)
				fprintf(f,"\n");
			fprintf(f, "[%s]", cat->name);
			for(cmt = cat->sameline; cmt; cmt=cmt->next) {
				fprintf(f,"%s", cmt->cmt);
			}
			if (!cat->sameline)
				fprintf(f,"\n");
			fclose(f);
            
			var = cat->root;
			while(var) {
				set_fn(fn, sizeof(fn), var->file, configfile);
				f = fopen(fn, "a");
				if (!f)
				{
					ast_verbose(VERBOSE_PREFIX_2 "Unable to write %s (%s)", fn, strerror(errno));
					return -1;
				}
                
				/* dump any includes that happen before this category header */
				for (incl=cfg->includes; incl; incl = incl->next) {
					if (strcmp(incl->include_location_file, var->file) == 0){
						if (var->lineno > incl->include_location_lineno && !incl->output) {
							if (incl->exec)
								fprintf(f,"#exec \"%s\"\n", incl->exec_file);
							else
								fprintf(f,"#include \"%s\"\n", incl->included_file);
							incl->output = 1;
						}
					}
				}
                
				for (cmt = var->precomments; cmt; cmt=cmt->next) {
					if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
						fprintf(f,"%s", cmt->cmt);
				}
				if (var->sameline) 
					fprintf(f, "%s %s %s  %s", var->name, (var->object ? "=>" : "="), var->value, var->sameline->cmt);
				else	
					fprintf(f, "%s %s %s\n", var->name, (var->object ? "=>" : "="), var->value);
				if (var->blanklines) {
					blanklines = var->blanklines;
					while (blanklines--)
						fprintf(f, "\n");
				}
				
				fclose(f);
                
				
				var = var->next;
			}
			cat = cat->next;
		}
		if ((option_verbose > 1) && !option_debug)
			ast_verbose("Saved\n");
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Unable to open for writing: %s\n", fn);
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Unable to write (%s)", strerror(errno));
		return -1;
	}

	/* Now, for files with trailing #include/#exec statements,
	   we have to make sure every entry is output */
	
	for (incl=cfg->includes; incl; incl = incl->next) {
		if (!incl->output) {
			/* open the respective file */
			set_fn(fn, sizeof(fn), incl->include_location_file, configfile);
			f = fopen(fn, "a");
			if (!f)
			{
				ast_verbose(VERBOSE_PREFIX_2 "Unable to write %s (%s)", fn, strerror(errno));
				return -1;
			}
            
			/* output the respective include */
			if (incl->exec)
				fprintf(f,"#exec \"%s\"\n", incl->exec_file);
			else
				fprintf(f,"#include \"%s\"\n", incl->included_file);
			fclose(f);
			incl->output = 1;
		}
	}
	
	return 0;
}

/* ================ the Line ========================================
   above this line, you have what you need to load a config file,
   and below it, you have what you need to process the extensions.conf
   file into the context/exten/prio stuff. They are both in one file
   to make things simpler */

static struct ast_context *local_contexts = NULL;
static struct ast_context *contexts = NULL;
struct ast_context;
struct ast_app;
#ifdef LOW_MEMORY
#define EXT_DATA_SIZE 256
#else
#define EXT_DATA_SIZE 8192
#endif

#ifdef NOT_ANYMORE
static AST_RWLIST_HEAD_STATIC(switches, ast_switch);
#endif

#define SWITCH_DATA_LENGTH 256

static const char *ast_get_extension_app(struct ast_exten *e)
{
	return e ? e->app : NULL;
}

static const char *ast_get_extension_name(struct ast_exten *exten)
{
	return exten ? exten->exten : NULL;
}

static AST_RWLIST_HEAD_STATIC(hints, ast_hint);

/*! \brief  ast_change_hint: Change hint for an extension */
static int ast_change_hint(struct ast_exten *oe, struct ast_exten *ne)
{
	struct ast_hint *hint;
	int res = -1;

	AST_RWLIST_TRAVERSE(&hints, hint, list) {
		if (hint->exten == oe) {
	    		hint->exten = ne;
			res = 0;
			break;
		}
	}

	return res;
}

/*! \brief  ast_add_hint: Add hint to hint list, check initial extension state */
static int ast_add_hint(struct ast_exten *e)
{
	struct ast_hint *hint;

	if (!e)
		return -1;


	/* Search if hint exists, do nothing */
	AST_RWLIST_TRAVERSE(&hints, hint, list) {
		if (hint->exten == e) {
			if (option_debug > 1)
				ast_log(LOG_DEBUG, "HINTS: Not re-adding existing hint %s: %s\n", ast_get_extension_name(e), ast_get_extension_app(e));
			return -1;
		}
	}

	if (option_debug > 1)
		ast_log(LOG_DEBUG, "HINTS: Adding hint %s: %s\n", ast_get_extension_name(e), ast_get_extension_app(e));

	if (!(hint = ast_calloc(1, sizeof(*hint)))) {
		return -1;
	}
	/* Initialize and insert new item at the top */
	hint->exten = e;
	AST_RWLIST_INSERT_HEAD(&hints, hint, list);

	return 0;
}

/*! \brief add the extension in the priority chain.
 * returns 0 on success, -1 on failure
 */
static int add_pri(struct ast_context *con, struct ast_exten *tmp,
	struct ast_exten *el, struct ast_exten *e, int replace)
{
	struct ast_exten *ep;

	for (ep = NULL; e ; ep = e, e = e->peer) {
		if (e->priority >= tmp->priority)
			break;
	}
	if (!e) {	/* go at the end, and ep is surely set because the list is not empty */
		ep->peer = tmp;
		return 0;	/* success */
	}
	if (e->priority == tmp->priority) {
		/* Can't have something exactly the same.  Is this a
		   replacement?  If so, replace, otherwise, bonk. */
		if (!replace) {
			ast_log(LOG_WARNING, "Unable to register extension '%s', priority %d in '%s', already in use\n", tmp->exten, tmp->priority, con->name);
			tmp->datad(tmp->data);
			free(tmp);
			return -1;
		}
		/* we are replacing e, so copy the link fields and then update
		 * whoever pointed to e to point to us
		 */
		tmp->next = e->next;	/* not meaningful if we are not first in the peer list */
		tmp->peer = e->peer;	/* always meaningful */
		if (ep)			/* We're in the peer list, just insert ourselves */
			ep->peer = tmp;
		else if (el)		/* We're the first extension. Take over e's functions */
			el->next = tmp;
		else			/* We're the very first extension.  */
			con->root = tmp;
		if (tmp->priority == PRIORITY_HINT)
			ast_change_hint(e,tmp);
		/* Destroy the old one */
		e->datad(e->data);
		free(e);
	} else {	/* Slip ourselves in just before e */
		tmp->peer = e;
		tmp->next = e->next;	/* extension chain, or NULL if e is not the first extension */
		if (ep)			/* Easy enough, we're just in the peer list */
			ep->peer = tmp;
		else {			/* we are the first in some peer list, so link in the ext list */
			if (el)
				el->next = tmp;	/* in the middle... */
			else
				con->root = tmp; /* ... or at the head */
			e->next = NULL;	/* e is no more at the head, so e->next must be reset */
		}
		/* And immediately return success. */
		if (tmp->priority == PRIORITY_HINT)
			 ast_add_hint(tmp);
	}
	return 0;
}

/*! \brief  ast_remove_hint: Remove hint from extension */
static int ast_remove_hint(struct ast_exten *e)
{
	/* Cleanup the Notifys if hint is removed */
	struct ast_hint *hint;
	struct ast_state_cb *cblist, *cbprev;
	int res = -1;

	if (!e)
		return -1;

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&hints, hint, list) {
		if (hint->exten == e) {
			cbprev = NULL;
			cblist = hint->callbacks;
			while (cblist) {
				/* Notify with -1 and remove all callbacks */
				cbprev = cblist;
				cblist = cblist->next;
				free(cbprev);
			}
			hint->callbacks = NULL;
			AST_RWLIST_REMOVE_CURRENT(&hints, list);
			free(hint);
	   		res = 0;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END

	return res;
}

static void destroy_exten(struct ast_exten *e)
{
	if (e->priority == PRIORITY_HINT)
		ast_remove_hint(e);

	if (e->datad)
		e->datad(e->data);
	free(e);
}

char *days[] =
{
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",
	NULL,
};

char *months[] =
{
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",
	NULL,
};

int ast_build_timing(struct ast_timing *i, const char *info_in);

int ast_build_timing(struct ast_timing *i, const char *info_in)
{
	char *info;
	int j, num_fields, last_sep = -1;

	i->timezone = NULL;

	/* Check for empty just in case */
	if (ast_strlen_zero(info_in)) {
		return 0;
	}

	/* make a copy just in case we were passed a static string */
	info = ast_strdupa(info_in);

	/* count the number of fields in the timespec */
	for (j = 0, num_fields = 1; info[j] != '\0'; j++) {
		if (info[j] == ',') {
			last_sep = j;
			num_fields++;
		}
	}

	/* save the timezone, if it is specified */
	if (num_fields == 5) {
		i->timezone = ast_strdup(info + last_sep + 1);
	}

	/* Assume everything except time */
	i->monthmask = 0xfff;	/* 12 bits */
	i->daymask = 0x7fffffffU; /* 31 bits */
	i->dowmask = 0x7f; /* 7 bits */
	/* on each call, use strsep() to move info to the next argument */
	get_timerange(i, strsep(&info, "|,"));
	if (info)
		i->dowmask = get_range(strsep(&info, "|,"), 7, days, "day of week");
	if (info)
		i->daymask = get_range(strsep(&info, "|,"), 31, NULL, "day");
	if (info)
		i->monthmask = get_range(strsep(&info, "|,"), 12, months, "month");
	return 1;
}

/*!
 * \brief helper functions to sort extensions and patterns in the desired way,
 * so that more specific patterns appear first.
 *
 * ext_cmp1 compares individual characters (or sets of), returning
 * an int where bits 0-7 are the ASCII code of the first char in the set,
 * while bit 8-15 are the cardinality of the set minus 1.
 * This way more specific patterns (smaller cardinality) appear first.
 * Wildcards have a special value, so that we can directly compare them to
 * sets by subtracting the two values. In particular:
 * 	0x000xx		one character, xx
 * 	0x0yyxx		yy character set starting with xx
 * 	0x10000		'.' (one or more of anything)
 * 	0x20000		'!' (zero or more of anything)
 * 	0x30000		NUL (end of string)
 * 	0x40000		error in set.
 * The pointer to the string is advanced according to needs.
 * NOTES:
 *	1. the empty set is equivalent to NUL.
 *	2. given that a full set has always 0 as the first element,
 *	   we could encode the special cases as 0xffXX where XX
 *	   is 1, 2, 3, 4 as used above.
 */
static int ext_cmp1(const char **p)
{
	uint32_t chars[8];
	int c, cmin = 0xff, count = 0;
	const char *end;

	/* load, sign extend and advance pointer until we find
	 * a valid character.
	 */
	while ( (c = *(*p)++) && (c == ' ' || c == '-') )
		;	/* ignore some characters */

	/* always return unless we have a set of chars */
	switch (c) {
	default:	/* ordinary character */
		return 0x0000 | (c & 0xff);

	case 'N':	/* 2..9 */
		return 0x0700 | '2' ;

	case 'X':	/* 0..9 */
		return 0x0900 | '0';

	case 'Z':	/* 1..9 */
		return 0x0800 | '1';

	case '.':	/* wildcard */
		return 0x10000;

	case '!':	/* earlymatch */
		return 0x20000;	/* less specific than NULL */

	case '\0':	/* empty string */
		*p = NULL;
		return 0x30000;

	case '[':	/* pattern */
		break;
	}
	/* locate end of set */
	end = strchr(*p, ']');	

	if (end == NULL) {
		ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
		return 0x40000;	/* XXX make this entry go last... */
	}

	memset(chars, '\0', sizeof(chars));	/* clear all chars in the set */
	for (; *p < end  ; (*p)++) {
		unsigned char c1, c2;	/* first-last char in range */
		c1 = (unsigned char)((*p)[0]);
		if (*p + 2 < end && (*p)[1] == '-') { /* this is a range */
			c2 = (unsigned char)((*p)[2]);
			*p += 2;	/* skip a total of 3 chars */
		} else			/* individual character */
			c2 = c1;
		if (c1 < cmin)
			cmin = c1;
		for (; c1 <= c2; c1++) {
			uint32_t mask = 1 << (c1 % 32);
			if ( (chars[ c1 / 32 ] & mask) == 0)
				count += 0x100;
			chars[ c1 / 32 ] |= mask;
		}
	}
	(*p)++;
	return count == 0 ? 0x30000 : (count | cmin);
}

/*!
 * \brief the full routine to compare extensions in rules.
 */
static int ext_cmp(const char *a, const char *b)
{
	/* make sure non-patterns come first.
	 * If a is not a pattern, it either comes first or
	 * we use strcmp to compare the strings.
	 */
	int ret = 0;

	if (a[0] != '_')
		return (b[0] == '_') ? -1 : strcmp(a, b);

	/* Now we know a is a pattern; if b is not, a comes first */
	if (b[0] != '_')
		return 1;
#if 0	/* old mode for ext matching */
	return strcmp(a, b);
#endif
	/* ok we need full pattern sorting routine */
	while (!ret && a && b)
		ret = ext_cmp1(&a) - ext_cmp1(&b);
	if (ret == 0)
		return 0;
	else
		return (ret > 0) ? 1 : -1;
}

/*! \brief copy a string skipping whitespace */
static int ext_strncpy(char *dst, const char *src, int len)
{
	int count=0;

	while (*src && (count < len - 1)) {
		switch(*src) {
		case ' ':
			/*	otherwise exten => [a-b],1,... doesn't work */
			/*		case '-': */
			/* Ignore */
			break;
		default:
			*dst = *src;
			dst++;
		}
		src++;
		count++;
	}
	*dst = '\0';

	return count;
}

/*
 * Wrapper around _extension_match_core() to do performance measurement
 * using the profiling code.
 */
int ast_check_timing(const struct ast_timing *i);

int ast_check_timing(const struct ast_timing *i)
{
	/* sorry, but this feature will NOT be available
	   in the standalone version */
	return 0;
}

#ifdef NOT_ANYMORE
static struct ast_switch *pbx_findswitch(const char *sw)
{
	struct ast_switch *asw;

	AST_RWLIST_TRAVERSE(&switches, asw, list) {
		if (!strcasecmp(asw->name, sw))
			break;
	}

	return asw;
}
#endif


static struct ast_context *ast_walk_contexts(struct ast_context *con);

static struct ast_context *ast_walk_contexts(struct ast_context *con)
{
	return con ? con->next : contexts;
}

struct ast_context *localized_walk_contexts(struct ast_context *con);
struct ast_context *localized_walk_contexts(struct ast_context *con)
{
	return ast_walk_contexts(con);
}



static struct ast_exten *ast_walk_context_extensions(struct ast_context *con,
											  struct ast_exten *exten);

static struct ast_exten *ast_walk_context_extensions(struct ast_context *con,
	struct ast_exten *exten)
{
	if (!exten)
		return con ? con->root : NULL;
	else
		return exten->next;
}

struct ast_exten *localized_walk_context_extensions(struct ast_context *con,
													struct ast_exten *exten);
struct ast_exten *localized_walk_context_extensions(struct ast_context *con,
													struct ast_exten *exten)
{
	return ast_walk_context_extensions(con,exten);
}


static struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
												struct ast_exten *priority);

static struct ast_exten *ast_walk_extension_priorities(struct ast_exten *exten,
	struct ast_exten *priority)
{
	return priority ? priority->peer : exten;
}

struct ast_exten *localized_walk_extension_priorities(struct ast_exten *exten,
													  struct ast_exten *priority);
struct ast_exten *localized_walk_extension_priorities(struct ast_exten *exten,
													  struct ast_exten *priority)
{
	return ast_walk_extension_priorities(exten, priority);
}



static struct ast_include *ast_walk_context_includes(struct ast_context *con,
											  struct ast_include *inc);

static struct ast_include *ast_walk_context_includes(struct ast_context *con,
	struct ast_include *inc)
{
	if (!inc)
		return con ? con->includes : NULL;
	else
		return inc->next;
}

struct ast_include *localized_walk_context_includes(struct ast_context *con,
													struct ast_include *inc);
struct ast_include *localized_walk_context_includes(struct ast_context *con,
													struct ast_include *inc)
{
	return ast_walk_context_includes(con, inc);
}


static struct ast_sw *ast_walk_context_switches(struct ast_context *con,
													 struct ast_sw *sw);

static struct ast_sw *ast_walk_context_switches(struct ast_context *con,
													 struct ast_sw *sw)
{
	if (!sw)
		return con ? AST_LIST_FIRST(&con->alts) : NULL;
	else
		return AST_LIST_NEXT(sw, list);
}

struct ast_sw *localized_walk_context_switches(struct ast_context *con,
													struct ast_sw *sw);
struct ast_sw *localized_walk_context_switches(struct ast_context *con,
													struct ast_sw *sw)
{
	return ast_walk_context_switches(con, sw);
}


static struct ast_context *ast_context_find(const char *name);

static struct ast_context *ast_context_find(const char *name)
{
	struct ast_context *tmp = NULL;
	while ( (tmp = ast_walk_contexts(tmp)) ) {
		if (!name || !strcasecmp(name, tmp->name))
			break;
	}
	return tmp;
}

/*
 * Internal function for ast_extension_{match|close}
 * return 0 on no-match, 1 on match, 2 on early match.
 * mode is as follows:
 *	E_MATCH		success only on exact match
 *	E_MATCHMORE	success only on partial match (i.e. leftover digits in pattern)
 *	E_CANMATCH	either of the above.
 */

static int _extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	mode &= E_MATCH_MASK;	/* only consider the relevant bits */

	if ( (mode == E_MATCH) && (pattern[0] == '_') && (strcasecmp(pattern,data)==0) ) /* note: if this test is left out, then _x. will not match _x. !!! */
		return 1;

	if (pattern[0] != '_') { /* not a pattern, try exact or partial match */
		int ld = strlen(data), lp = strlen(pattern);

		if (lp < ld)		/* pattern too short, cannot match */
			return 0;
		/* depending on the mode, accept full or partial match or both */
		if (mode == E_MATCH)
			return !strcmp(pattern, data); /* 1 on match, 0 on fail */
		if (ld == 0 || !strncasecmp(pattern, data, ld)) /* partial or full match */
			return (mode == E_MATCHMORE) ? lp > ld : 1; /* XXX should consider '!' and '/' ? */
		else
			return 0;
	}
	pattern++; /* skip leading _ */
	/*
	 * XXX below we stop at '/' which is a separator for the CID info. However we should
	 * not store '/' in the pattern at all. When we insure it, we can remove the checks.
	 */
	while (*data && *pattern && *pattern != '/') {
		const char *end;

		if (*data == '-') { /* skip '-' in data (just a separator) */
			data++;
			continue;
		}
		switch (toupper(*pattern)) {
		case '[':	/* a range */
			end = strchr(pattern+1, ']'); /* XXX should deal with escapes ? */
			if (end == NULL) {
				ast_log(LOG_WARNING, "Wrong usage of [] in the extension\n");
				return 0;	/* unconditional failure */
			}
			for (pattern++; pattern != end; pattern++) {
				if (pattern+2 < end && pattern[1] == '-') { /* this is a range */
					if (*data >= pattern[0] && *data <= pattern[2])
						break;	/* match found */
					else {
						pattern += 2; /* skip a total of 3 chars */
						continue;
					}
				} else if (*data == pattern[0])
					break;	/* match found */
			}
			if (pattern == end)
				return 0;
			pattern = end;	/* skip and continue */
			break;
		case 'N':
			if (*data < '2' || *data > '9')
				return 0;
			break;
		case 'X':
			if (*data < '0' || *data > '9')
				return 0;
			break;
		case 'Z':
			if (*data < '1' || *data > '9')
				return 0;
			break;
		case '.':	/* Must match, even with more digits */
			return 1;
		case '!':	/* Early match */
			return 2;
		case ' ':
		case '-':	/* Ignore these in patterns */
			data--; /* compensate the final data++ */
			break;
		default:
			if (*data != *pattern)
				return 0;
		}
		data++;
		pattern++;
	}
	if (*data)			/* data longer than pattern, no match */
		return 0;
	/*
	 * match so far, but ran off the end of the data.
	 * Depending on what is next, determine match or not.
	 */
	if (*pattern == '\0' || *pattern == '/')	/* exact match */
		return (mode == E_MATCHMORE) ? 0 : 1;	/* this is a failure for E_MATCHMORE */
	else if (*pattern == '!')			/* early match */
		return 2;
	else						/* partial match */
		return (mode == E_MATCH) ? 0 : 1;	/* this is a failure for E_MATCH */
}

static int extension_match_core(const char *pattern, const char *data, enum ext_match_t mode)
{
	int i;
	i = _extension_match_core(pattern, data, mode);
	return i;
}

static int ast_extension_match(const char *pattern, const char *data);

static int ast_extension_match(const char *pattern, const char *data)
{
	return extension_match_core(pattern, data, E_MATCH);
}

static int matchcid(const char *cidpattern, const char *callerid)
{
	/* If the Caller*ID pattern is empty, then we're matching NO Caller*ID, so
	   failing to get a number should count as a match, otherwise not */

	if (ast_strlen_zero(callerid))
		return ast_strlen_zero(cidpattern) ? 1 : 0;

	return ast_extension_match(cidpattern, callerid);
}

static inline int include_valid(struct ast_include *i)
{
	if (!i->hastime)
		return 1;

	return ast_check_timing(&(i->timing));
}



static struct ast_exten *pbx_find_extension(struct ast_channel *chan,
											struct ast_context *bypass, 
											struct pbx_find_info *q,
											const char *context, 
											const char *exten, 
											int priority,
											const char *label, 
											const char *callerid, 
											enum ext_match_t action);


static struct ast_exten *pbx_find_extension(struct ast_channel *chan,
											struct ast_context *bypass, 
											struct pbx_find_info *q,
											const char *context, 
											const char *exten, 
											int priority,
											const char *label, 
											const char *callerid, 
											enum ext_match_t action)
{
	int x;
	struct ast_context *tmp;
	struct ast_exten *e, *eroot;
	struct ast_include *i;

	/* Initialize status if appropriate */
	if (q->stacklen == 0) {
		q->status = STATUS_NO_CONTEXT;
		q->swo = NULL;
		q->data = NULL;
		q->foundcontext = NULL;
	} else if (q->stacklen >= AST_PBX_MAX_STACK) {
		ast_log(LOG_WARNING, "Maximum PBX stack exceeded\n");
		return NULL;
	}
	/* Check first to see if we've already been checked */
	for (x = 0; x < q->stacklen; x++) {
		if (!strcasecmp(q->incstack[x], context))
			return NULL;
	}
	if (bypass)	/* bypass means we only look there */
		tmp = bypass;
	else {	/* look in contexts */
		tmp = NULL;
		while ((tmp = ast_walk_contexts(tmp)) ) {
			if (!strcmp(tmp->name, context))
				break;
		}
		if (!tmp)
			return NULL;
	}
	if (q->status < STATUS_NO_EXTENSION)
		q->status = STATUS_NO_EXTENSION;

	/* scan the list trying to match extension and CID */
	eroot = NULL;
	while ( (eroot = ast_walk_context_extensions(tmp, eroot)) ) {
		int match = extension_match_core(eroot->exten, exten, action);
		/* 0 on fail, 1 on match, 2 on earlymatch */

		if (!match || (eroot->matchcid && !matchcid(eroot->cidmatch, callerid)))
			continue;	/* keep trying */
		if (match == 2 && action == E_MATCHMORE) {
			/* We match an extension ending in '!'.
			 * The decision in this case is final and is NULL (no match).
			 */
			return NULL;
		}
		/* found entry, now look for the right priority */
		if (q->status < STATUS_NO_PRIORITY)
			q->status = STATUS_NO_PRIORITY;
		e = NULL;
		while ( (e = ast_walk_extension_priorities(eroot, e)) ) {
			/* Match label or priority */
			if (action == E_FINDLABEL) {
				if (q->status < STATUS_NO_LABEL)
					q->status = STATUS_NO_LABEL;
				if (label && e->label && !strcmp(label, e->label))
					break;	/* found it */
			} else if (e->priority == priority) {
				break;	/* found it */
			} /* else keep searching */
		}
		if (e) {	/* found a valid match */
			q->status = STATUS_SUCCESS;
			q->foundcontext = context;
			return e;
		}
	}
#ifdef NOT_RIGHT_NOW
	/* Check alternative switches???  */
	AST_LIST_TRAVERSE(&tmp->alts, sw, list) {
		struct ast_switch *asw = pbx_findswitch(sw->name);
		ast_switch_f *aswf = NULL;
		char *datap;

		if (!asw) {
			ast_log(LOG_WARNING, "No such switch '%s'\n", sw->name);
			continue;
		}
		/* No need to Substitute variables now; we shouldn't be here if there's any  */
		
		/* equivalent of extension_match_core() at the switch level */
		if (action == E_CANMATCH)
			aswf = asw->canmatch;
		else if (action == E_MATCHMORE)
			aswf = asw->matchmore;
		else /* action == E_MATCH */
			aswf = asw->exists;
		datap = sw->eval ? sw->tmpdata : sw->data;
		res = !aswf ? 0 : aswf(chan, context, exten, priority, callerid, datap);
		if (res) {	/* Got a match */
			q->swo = asw;
			q->data = datap;
			q->foundcontext = context;
			/* XXX keep status = STATUS_NO_CONTEXT ? */
			return NULL;
		}
	}
#endif
	q->incstack[q->stacklen++] = tmp->name;	/* Setup the stack */
	/* Now try any includes we have in this context */
	for (i = tmp->includes; i; i = i->next) {
		if (include_valid(i)) {
			if ((e = pbx_find_extension(NULL, bypass, q, i->rname, exten, priority, label, callerid, action)))
				return e;
			if (q->swo)
				return NULL;
		}
	}
	return NULL;
}

struct ast_exten *localized_find_extension(struct ast_context *bypass,
										  struct pbx_find_info *q,
										  const char *context, 
										  const char *exten, 
										  int priority,
										  const char *label, 
										  const char *callerid, 
										  enum ext_match_t action);

struct ast_exten *localized_find_extension(struct ast_context *bypass,
										  struct pbx_find_info *q,
										  const char *context, 
										  const char *exten, 
										  int priority,
										  const char *label, 
										  const char *callerid, 
										   enum ext_match_t action)
{
	return pbx_find_extension(NULL, bypass, q, context, exten, priority, label, callerid, action);
}


static struct ast_context *contexts;
AST_RWLOCK_DEFINE_STATIC(conlock); 		/*!< Lock for the ast_context list */

static const char *ast_get_context_name(struct ast_context *con);

static const char *ast_get_context_name(struct ast_context *con)
{
	return con ? con->name : NULL;
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
static int ast_context_add_include2(struct ast_context *con, const char *value,
							 const char *registrar);

static int ast_context_add_include2(struct ast_context *con, const char *value,
									const char *registrar)
{
	struct ast_include *new_include;
	char *c;
	struct ast_include *i, *il = NULL; /* include, include_last */
	int length;
	char *p;

	length = sizeof(struct ast_include);
	length += 2 * (strlen(value) + 1);

	/* allocate new include structure ... */
	if (!(new_include = ast_calloc(1, length)))
		return -1;
	/* Fill in this structure. Use 'p' for assignments, as the fields
	 * in the structure are 'const char *'
	 */
	p = new_include->stuff;
	new_include->name = p;
	strcpy(p, value);
	p += strlen(value) + 1;
	new_include->rname = p;
	strcpy(p, value);
	/* Strip off timing info, and process if it is there */
	if ( (c = strchr(p, '|')) ) {
		*c++ = '\0';
		new_include->hastime = ast_build_timing(&(new_include->timing), c);
	}
	new_include->next      = NULL;
	new_include->registrar = registrar;


	/* ... go to last include and check if context is already included too... */
	for (i = con->includes; i; i = i->next) {
		if (!strcasecmp(i->name, new_include->name)) {
			free(new_include);
			errno = EEXIST;
			return -1;
		}
		il = i;
	}

	/* ... include new context into context list, unlock, return */
	if (il)
		il->next = new_include;
	else
		con->includes = new_include;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Including context '%s' in context '%s'\n", new_include->name, ast_get_context_name(con));

	return 0;
}

int localized_context_add_include2(struct ast_context *con, const char *value,
							 const char *registrar);
int localized_context_add_include2(struct ast_context *con, const char *value,
								   const char *registrar)
{
	return  ast_context_add_include2(con, value, registrar);
}



static int ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar);

static int ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	struct ast_ignorepat *ignorepat, *ignorepatc, *ignorepatl = NULL;
	int length;
	length = sizeof(struct ast_ignorepat);
	length += strlen(value) + 1;
	if (!(ignorepat = ast_calloc(1, length)))
		return -1;
	/* The cast to char * is because we need to write the initial value.
	 * The field is not supposed to be modified otherwise
	 */
	strcpy((char *)ignorepat->pattern, value);
	ignorepat->next = NULL;
	ignorepat->registrar = registrar;
	for (ignorepatc = con->ignorepats; ignorepatc; ignorepatc = ignorepatc->next) {
		ignorepatl = ignorepatc;
		if (!strcasecmp(ignorepatc->pattern, value)) {
			/* Already there */
			errno = EEXIST;
			return -1;
		}
	}
	if (ignorepatl)
		ignorepatl->next = ignorepat;
	else
		con->ignorepats = ignorepat;
	return 0;

}

int localized_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar);

int localized_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	return ast_context_add_ignorepat2(con, value, registrar);
}


/*
 * Lock context list functions ...
 */

static int ast_wrlock_contexts(void)
{
	return ast_rwlock_wrlock(&conlock);
}

static int ast_unlock_contexts(void)
{
	return ast_rwlock_unlock(&conlock);
}

static int ast_wrlock_context(struct ast_context *con)
{
	return ast_rwlock_wrlock(&con->lock);
}

static int ast_unlock_context(struct ast_context *con)
{
	return ast_rwlock_unlock(&con->lock);
}

/*
 * errno values
 *  ENOMEM - out of memory
 *  EBUSY  - can't lock
 *  EEXIST - already included
 *  EINVAL - there is no existence of context for inclusion
 */
static int ast_context_add_switch2(struct ast_context *con, const char *value,
							const char *data, int eval, const char *registrar);

static int ast_context_add_switch2(struct ast_context *con, const char *value,
	const char *data, int eval, const char *registrar)
{
	struct ast_sw *new_sw;
	struct ast_sw *i;
	int length;
	char *p;

	length = sizeof(struct ast_sw);
	length += strlen(value) + 1;
	if (data)
		length += strlen(data);
	length++;
	if (eval) {
		/* Create buffer for evaluation of variables */
		length += SWITCH_DATA_LENGTH;
		length++;
	}

	/* allocate new sw structure ... */
	if (!(new_sw = ast_calloc(1, length)))
		return -1;
	/* ... fill in this structure ... */
	p = new_sw->stuff;
	new_sw->name = p;
	strcpy(new_sw->name, value);
	p += strlen(value) + 1;
	new_sw->data = p;
	if (data) {
		strcpy(new_sw->data, data);
		p += strlen(data) + 1;
	} else {
		strcpy(new_sw->data, "");
		p++;
	}
	if (eval)
		new_sw->tmpdata = p;
	new_sw->eval	  = eval;
	new_sw->registrar = registrar;

	/* ... go to last sw and check if context is already swd too... */
	AST_LIST_TRAVERSE(&con->alts, i, list) {
		if (!strcasecmp(i->name, new_sw->name) && !strcasecmp(i->data, new_sw->data)) {
			free(new_sw);
			errno = EEXIST;
			return -1;
		}
	}

	/* ... sw new context into context list, unlock, return */
	AST_LIST_INSERT_TAIL(&con->alts, new_sw, list);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Including switch '%s/%s' in context '%s'\n", new_sw->name, new_sw->data, ast_get_context_name(con));

	return 0;
}

int localized_context_add_switch2(struct ast_context *con, const char *value,
										 const char *data, int eval, const char *registrar);

int localized_context_add_switch2(struct ast_context *con, const char *value,
										 const char *data, int eval, const char *registrar)
{
	return ast_context_add_switch2(con, value, data, eval, registrar);
}

static struct ast_context *__ast_context_create(struct ast_context **extcontexts, const char *name, const char *registrar, int existsokay)
{
	struct ast_context *tmp, **loc_contexts;
	int length = sizeof(struct ast_context) + strlen(name) + 1;

	if (!extcontexts) {
		ast_wrlock_contexts();
		loc_contexts = &contexts;
	} else
		loc_contexts = extcontexts;

	for (tmp = *loc_contexts; tmp; tmp = tmp->next) {
		if (!strcasecmp(tmp->name, name)) {
			if (!existsokay) {
				ast_log(LOG_WARNING, "Tried to register context '%s', already in use\n", name);
				tmp = NULL;
			}
			if (!extcontexts)
				ast_unlock_contexts();
			return tmp;
		}
	}
	if ((tmp = ast_calloc(1, length))) {
		ast_rwlock_init(&tmp->lock);
		ast_mutex_init(&tmp->macrolock);
		strcpy(tmp->name, name);
		tmp->root = NULL;
		tmp->registrar = registrar;
		tmp->next = *loc_contexts;
		tmp->includes = NULL;
		tmp->ignorepats = NULL;
		*loc_contexts = tmp;
		if (option_debug)
			ast_log(LOG_DEBUG, "Registered context '%s'\n", tmp->name);
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Registered extension context '%s'\n", tmp->name);
	}

	if (!extcontexts)
		ast_unlock_contexts();
	return tmp;
}

/*! \brief
 * Main interface to add extensions to the list for out context.
 *
 * We sort extensions in order of matching preference, so that we can
 * stop the search as soon as we find a suitable match.
 * This ordering also takes care of wildcards such as '.' (meaning
 * "one or more of any character") and '!' (which is 'earlymatch',
 * meaning "zero or more of any character" but also impacts the
 * return value from CANMATCH and EARLYMATCH.
 *
 * The extension match rules defined in the devmeeting 2006.05.05 are
 * quite simple: WE SELECT THE LONGEST MATCH.
 * In detail, "longest" means the number of matched characters in
 * the extension. In case of ties (e.g. _XXX and 333) in the length
 * of a pattern, we give priority to entries with the smallest cardinality
 * (e.g, [5-9] comes before [2-8] before the former has only 5 elements,
 * while the latter has 7, etc.
 * In case of same cardinality, the first element in the range counts.
 * If we still have a tie, any final '!' will make this as a possibly
 * less specific pattern.
 *
 * EBUSY - can't lock
 * EEXIST - extension with the same priority exist and no replace is set
 *
 */
static int ast_add_extension2(struct ast_context *con,
	int replace, const char *extension, int priority, const char *label, const char *callerid,
	const char *application, void *data, void (*datad)(void *),
	const char *registrar)
{
	/*
	 * Sort extensions (or patterns) according to the rules indicated above.
	 * These are implemented by the function ext_cmp()).
	 * All priorities for the same ext/pattern/cid are kept in a list,
	 * using the 'peer' field  as a link field..
	 */
	struct ast_exten *tmp, *e, *el = NULL;
	int res;
	int length;
	char *p;

	/* if we are adding a hint, and there are global variables, and the hint
	   contains variable references, then expand them --- NOT In this situation!!!
	*/

	length = sizeof(struct ast_exten);
	length += strlen(extension) + 1;
	length += strlen(application) + 1;
	if (label)
		length += strlen(label) + 1;
	if (callerid)
		length += strlen(callerid) + 1;
	else
		length ++;	/* just the '\0' */

	/* Be optimistic:  Build the extension structure first */
	if (datad == NULL)
		datad = null_datad;
	if (!(tmp = ast_calloc(1, length)))
		return -1;

	/* use p as dst in assignments, as the fields are const char * */
	p = tmp->stuff;
	if (label) {
		tmp->label = p;
		strcpy(p, label);
		p += strlen(label) + 1;
	}
	tmp->exten = p;
	p += ext_strncpy(p, extension, strlen(extension) + 1) + 1;
	tmp->priority = priority;
	tmp->cidmatch = p;	/* but use p for assignments below */
	if (callerid) {
		p += ext_strncpy(p, callerid, strlen(callerid) + 1) + 1;
		tmp->matchcid = 1;
	} else {
		*p++ = '\0';
		tmp->matchcid = 0;
	}
	tmp->app = p;
	strcpy(p, application);
	tmp->parent = con;
	tmp->data = data;
	tmp->datad = datad;
	tmp->registrar = registrar;

	res = 0; /* some compilers will think it is uninitialized otherwise */
	for (e = con->root; e; el = e, e = e->next) {   /* scan the extension list */
		res = ext_cmp(e->exten, extension);
		if (res == 0) { /* extension match, now look at cidmatch */
			if (!e->matchcid && !tmp->matchcid)
				res = 0;
			else if (tmp->matchcid && !e->matchcid)
				res = 1;
			else if (e->matchcid && !tmp->matchcid)
				res = -1;
			else
				res = strcasecmp(e->cidmatch, tmp->cidmatch);
		}
		if (res >= 0)
			break;
	}
	if (e && res == 0) { /* exact match, insert in the pri chain */
		res = add_pri(con, tmp, el, e, replace);
		if (res < 0) {
			errno = EEXIST;	/* XXX do we care ? */
			return 0; /* XXX should we return -1 maybe ? */
		}
	} else {
		/*
		 * not an exact match, this is the first entry with this pattern,
		 * so insert in the main list right before 'e' (if any)
		 */
		tmp->next = e;
		if (el)
			el->next = tmp;
		else
			con->root = tmp;
		if (tmp->priority == PRIORITY_HINT)
			ast_add_hint(tmp);
	}
	if (option_debug) {
		if (tmp->matchcid) {
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d (CID match '%s') to %s\n",
				tmp->exten, tmp->priority, tmp->cidmatch, con->name);
		} else {
			ast_log(LOG_DEBUG, "Added extension '%s' priority %d to %s\n",
				tmp->exten, tmp->priority, con->name);
		}
	}
	if (option_verbose > 2) {
		if (tmp->matchcid) {
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d (CID match '%s')to %s\n",
				tmp->exten, tmp->priority, tmp->cidmatch, con->name);
		} else {
			ast_verbose( VERBOSE_PREFIX_3 "Added extension '%s' priority %d to %s\n",
				tmp->exten, tmp->priority, con->name);
		}
	}
	return 0;
}

int localized_add_extension2(struct ast_context *con,
							 int replace, const char *extension, int priority, const char *label, const char *callerid,
							 const char *application, void *data, void (*datad)(void *),
							 const char *registrar);

int localized_add_extension2(struct ast_context *con,
							 int replace, const char *extension, int priority, const char *label, const char *callerid,
							 const char *application, void *data, void (*datad)(void *),
							 const char *registrar)
{
	return ast_add_extension2(con, replace, extension, priority, label, callerid, application, data, datad, registrar);
}



/*! \brief The return value depends on the action:
 *
 * E_MATCH, E_CANMATCH, E_MATCHMORE require a real match,
 *	and return 0 on failure, -1 on match;
 * E_FINDLABEL maps the label to a priority, and returns
 *	the priority on success, ... XXX
 * E_SPAWN, spawn an application,
 *	and return 0 on success, -1 on failure.
 */
static int pbx_extension_helper(struct ast_channel *c, struct ast_context *con,
								const char *context, const char *exten, int priority,
								const char *label, const char *callerid, enum ext_match_t action)
{
	struct ast_exten *e;
	int res;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */

	int matching_action = (action == E_MATCH || action == E_CANMATCH || action == E_MATCHMORE);

	e = pbx_find_extension(NULL, con, &q, context, exten, priority, label, callerid, action);
	if (e) {
		if (matching_action) {
			return -1;	/* success, we found it */
		} else if (action == E_FINDLABEL) { /* map the label to a priority */
			res = e->priority;
			return res;	/* the priority we were looking for */
		} else {	/* spawn */

			/* NOT!!!!! */
			return 0;
		}
	} else if (q.swo) {	/* not found here, but in another switch */
		if (matching_action)
			return -1;
		else {
			if (!q.swo->exec) {
				ast_log(LOG_WARNING, "No execution engine for switch %s\n", q.swo->name);
				res = -1;
			}
			return q.swo->exec(c, q.foundcontext ? q.foundcontext : context, exten, priority, callerid, q.data);
		}
	} else {	/* not found anywhere, see what happened */
		switch (q.status) {
		case STATUS_NO_CONTEXT:
			if (!matching_action)
				ast_log(LOG_NOTICE, "Cannot find extension context '%s'\n", context);
			break;
		case STATUS_NO_EXTENSION:
			if (!matching_action)
				ast_log(LOG_NOTICE, "Cannot find extension '%s' in context '%s'\n", exten, context);
			break;
		case STATUS_NO_PRIORITY:
			if (!matching_action)
				ast_log(LOG_NOTICE, "No such priority %d in extension '%s' in context '%s'\n", priority, exten, context);
			break;
		case STATUS_NO_LABEL:
			if (context)
				ast_log(LOG_NOTICE, "No such label '%s' in extension '%s' in context '%s'\n", label, exten, context);
			break;
		default:
			if (option_debug)
				ast_log(LOG_DEBUG, "Shouldn't happen!\n");
		}

		return (matching_action) ? 0 : -1;
	}
}

static int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, const char *exten, const char *label, const char *callerid);

static int ast_findlabel_extension2(struct ast_channel *c, struct ast_context *con, const char *exten, const char *label, const char *callerid)
{
	return pbx_extension_helper(c, con, NULL, exten, 0, label, callerid, E_FINDLABEL);
}

static struct ast_context *ast_context_find_or_create(struct ast_context **extcontexts, void *tab, const char *name, const char *registrar)
{
	return __ast_context_create(extcontexts, name, registrar, 1);
}

struct ast_context *localized_context_find_or_create(struct ast_context **extcontexts, void *tab, const char *name, const char *registrar);
struct ast_context *localized_context_find_or_create(struct ast_context **extcontexts, void *tab, const char *name, const char *registrar)
{
	return __ast_context_create(extcontexts, name, registrar, 1);
}


/* chopped this one off at the knees */
static int ast_func_read(struct ast_channel *chan, const char *function, char *workspace, size_t len)
{
	ast_log(LOG_ERROR, "Function %s not registered\n", function);
	return -1;
}

/*! \brief extract offset:length from variable name.
 * Returns 1 if there is a offset:length part, which is
 * trimmed off (values go into variables)
 */
static int parse_variable_name(char *var, int *offset, int *length, int *isfunc)
{
	int parens=0;

	*offset = 0;
	*length = INT_MAX;
	*isfunc = 0;
	for (; *var; var++) {
		if (*var == '(') {
			(*isfunc)++;
			parens++;
		} else if (*var == ')') {
			parens--;
		} else if (*var == ':' && parens == 0) {
			*var++ = '\0';
			sscanf(var, "%30d:%30d", offset, length);
			return 1; /* offset:length valid */
		}
	}
	return 0;
}

static const char *ast_var_value(const struct ast_var_t *var)
{
	return (var ? var->value : NULL);
}

/*! \brief takes a substring. It is ok to call with value == workspace.
 *
 * offset < 0 means start from the end of the string and set the beginning
 *   to be that many characters back.
 * length is the length of the substring.  A value less than 0 means to leave
 * that many off the end.
 * Always return a copy in workspace.
 */
static char *substring(const char *value, int offset, int length, char *workspace, size_t workspace_len)
{
	char *ret = workspace;
	int lr;	/* length of the input string after the copy */

	ast_copy_string(workspace, value, workspace_len); /* always make a copy */

	lr = strlen(ret); /* compute length after copy, so we never go out of the workspace */

	/* Quick check if no need to do anything */
	if (offset == 0 && length >= lr)	/* take the whole string */
		return ret;

	if (offset < 0)	{	/* translate negative offset into positive ones */
		offset = lr + offset;
		if (offset < 0) /* If the negative offset was greater than the length of the string, just start at the beginning */
			offset = 0;
	}

	/* too large offset result in empty string so we know what to return */
	if (offset >= lr)
		return ret + lr;	/* the final '\0' */

	ret += offset;		/* move to the start position */
	if (length >= 0 && length < lr - offset)	/* truncate if necessary */
		ret[length] = '\0';
	else if (length < 0) {
		if (lr > offset - length) /* After we remove from the front and from the rear, is there anything left? */
			ret[lr + length - offset] = '\0';
		else
			ret[0] = '\0';
	}

	return ret;
}

/*! \brief  Support for Asterisk built-in variables in the dialplan
\note	See also
	- \ref AstVar	Channel variables
	- \ref AstCauses The HANGUPCAUSE variable
 */
static void pbx_retrieve_variable(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp)
{
	const char not_found = '\0';
	char *tmpvar;
	const char *s;	/* the result */
	int offset, length;
	int i, need_substring;
	struct varshead *places[2] = { headp, &globals };	/* list of places where we may look */
	
	/*
	 * Make a copy of var because parse_variable_name() modifies the string.
	 * Then if called directly, we might need to run substring() on the result;
	 * remember this for later in 'need_substring', 'offset' and 'length'
	 */
	tmpvar = ast_strdupa(var);	/* parse_variable_name modifies the string */
	need_substring = parse_variable_name(tmpvar, &offset, &length, &i /* ignored */);
	
	/*
	 * Look first into predefined variables, then into variable lists.
	 * Variable 's' points to the result, according to the following rules:
	 * s == &not_found (set at the beginning) means that we did not find a
	 *	matching variable and need to look into more places.
	 * If s != &not_found, s is a valid result string as follows:
	 * s = NULL if the variable does not have a value;
	 *	you typically do this when looking for an unset predefined variable.
	 * s = workspace if the result has been assembled there;
	 *	typically done when the result is built e.g. with an snprintf(),
	 *	so we don't need to do an additional copy.
	 * s != workspace in case we have a string, that needs to be copied
	 *	(the ast_copy_string is done once for all at the end).
	 *	Typically done when the result is already available in some string.
	 */
	s = &not_found;	/* default value */
	if (s == &not_found) { /* look for more */
		if (!strcmp(var, "EPOCH")) {
			snprintf(workspace, workspacelen, "%u",(int)time(NULL));
		}
		
		s = workspace;
	}
	/* if not found, look into chanvars or global vars */
	for (i = 0; s == &not_found && i < (sizeof(places) / sizeof(places[0])); i++) {
		struct ast_var_t *variables;
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_rwlock_rdlock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (strcasecmp(ast_var_name(variables), var)==0) {
				s = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_rwlock_unlock(&globalslock);
	}
	if (s == &not_found || s == NULL)
		*ret = NULL;
	else {
		if (s != workspace)
			ast_copy_string(workspace, s, workspacelen);
		*ret = workspace;
		if (need_substring)
			*ret = substring(*ret, offset, length, workspace, workspacelen);
	}
}

static void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int count)
{
	/* Substitutes variables into cp2, based on string cp1, and assuming cp2 to be
	   zero-filled */
	char *cp4;
	const char *tmp, *whereweare;
	int length, offset, offset2, isfunction;
	char *workspace = NULL;
	char *ltmp = NULL, *var = NULL;
	char *nextvar, *nextexp, *nextthing;
	char *vars, *vare;
	int pos, brackets, needsub, len;

	*cp2 = 0; /* just in case there's nothing to do */
	whereweare=tmp=cp1;
	while (!ast_strlen_zero(whereweare) && count) {
		/* Assume we're copying the whole remaining string */
		pos = strlen(whereweare);
		nextvar = NULL;
		nextexp = NULL;
		nextthing = strchr(whereweare, '$');
		if (nextthing) {
			switch (nextthing[1]) {
			case '{':
				nextvar = nextthing;
				pos = nextvar - whereweare;
				break;
			case '[':
				nextexp = nextthing;
				pos = nextexp - whereweare;
				break;
			}
		}

		if (pos) {
			/* Can't copy more than 'count' bytes */
			if (pos > count)
				pos = count;

			/* Copy that many bytes */
			memcpy(cp2, whereweare, pos);

			count -= pos;
			cp2 += pos;
			whereweare += pos;
			*cp2 = 0;
		}

		if (nextvar) {
			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
				} else if (vare[0] == '{') {
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '['))
					needsub++;
				vare++;
			}
			if (brackets)
				ast_log(LOG_NOTICE, "Error in extension logic (missing '}' in '%s')\n", cp1);
			len = vare - vars - 1;

			/* Skip totally over variable string */
			whereweare += (len + 3);

			if (!var)
				var = alloca(VAR_BUF_SIZE);

			/* Store variable name (and truncate) */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				if (!ltmp)
					ltmp = alloca(VAR_BUF_SIZE);

				memset(ltmp, 0, VAR_BUF_SIZE);
				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1);
				vars = ltmp;
			} else {
				vars = var;
			}

			if (!workspace)
				workspace = alloca(VAR_BUF_SIZE);

			workspace[0] = '\0';

			parse_variable_name(vars, &offset, &offset2, &isfunction);
			if (isfunction) {
				/* Evaluate function */
				cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
				if (option_debug)
					ast_log(LOG_DEBUG, "Function result is '%s'\n", cp4 ? cp4 : "(null)");
			} else {
				/* Retrieve variable value */
				pbx_retrieve_variable(c, vars, &cp4, workspace, VAR_BUF_SIZE, headp);
			}
			if (cp4) {
				cp4 = substring(cp4, offset, offset2, workspace, VAR_BUF_SIZE);

				length = strlen(cp4);
				if (length > count)
					length = count;
				memcpy(cp2, cp4, length);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '[') {
					brackets++;
				} else if (vare[0] == ']') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			if (brackets)
				ast_log(LOG_NOTICE, "Error in extension logic (missing ']')\n");
			len = vare - vars - 1;

			/* Skip totally over expression */
			whereweare += (len + 3);

			if (!var)
				var = alloca(VAR_BUF_SIZE);

			/* Store variable name (and truncate) */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				if (!ltmp)
					ltmp = alloca(VAR_BUF_SIZE);

				memset(ltmp, 0, VAR_BUF_SIZE);
				pbx_substitute_variables_helper_full(c, headp, var, ltmp, VAR_BUF_SIZE - 1);
				vars = ltmp;
			} else {
				vars = var;
			}

			length = ast_expr(vars, cp2, count, NULL);

			if (length) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Expression result is '%s'\n", cp2);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		} else
			break;
	}
}

static void pbx_substitute_variables_helper(struct ast_channel *c, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(c, NULL, cp1, cp2, count);
}


static int pbx_load_config(const char *config_file);

static int pbx_load_config(const char *config_file)
{
	struct ast_config *cfg;
	char *end;
	char *label;
	char realvalue[256];
	int lastpri = -2;
	struct ast_context *con;
	struct ast_variable *v;
	const char *cxt;
	const char *aft;

	cfg = localized_config_load(config_file);
	if (!cfg)
		return 0;

	/* Use existing config to populate the PBX table */
	static_config = ast_true(ast_variable_retrieve(cfg, "general", "static"));
	write_protect_config = ast_true(ast_variable_retrieve(cfg, "general", "writeprotect"));
	if ((aft = ast_variable_retrieve(cfg, "general", "autofallthrough")))
		autofallthrough_config = ast_true(aft);
	clearglobalvars_config = ast_true(ast_variable_retrieve(cfg, "general", "clearglobalvars"));

	if ((cxt = ast_variable_retrieve(cfg, "general", "userscontext"))) 
		ast_copy_string(userscontext, cxt, sizeof(userscontext));
	else
		ast_copy_string(userscontext, "default", sizeof(userscontext));
								    
	for (v = ast_variable_browse(cfg, "globals"); v; v = v->next) {
		memset(realvalue, 0, sizeof(realvalue));
		pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
		pbx_builtin_setvar_helper(NULL, v->name, realvalue);
	}
	for (cxt = NULL; (cxt = ast_category_browse(cfg, cxt)); ) {
		/* All categories but "general" or "globals" are considered contexts */
		if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals"))
			continue;
		con=ast_context_find_or_create(&local_contexts,NULL,cxt, global_registrar);
		if (con == NULL)
			continue;

		for (v = ast_variable_browse(cfg, cxt); v; v = v->next) {
			if (!strcasecmp(v->name, "exten")) {
				char *tc = ast_strdup(v->value);
				if (tc) {
					int ipri = -2;
					char realext[256]="";
					char *plus, *firstp, *firstc;
					char *pri, *appl, *data, *cidmatch;
					char *stringp = tc;
					char *ext = strsep(&stringp, ",");
					if (!ext)
						ext="";
					pbx_substitute_variables_helper(NULL, ext, realext, sizeof(realext) - 1);
					cidmatch = strchr(realext, '/');
					if (cidmatch) {
						*cidmatch++ = '\0';
						ast_shrink_phone_number(cidmatch);
					}
					pri = strsep(&stringp, ",");
					if (!pri)
						pri="";
					label = strchr(pri, '(');
					if (label) {
						*label++ = '\0';
						end = strchr(label, ')');
						if (end)
							*end = '\0';
						else
							ast_log(LOG_WARNING, "Label missing trailing ')' at line %d\n", v->lineno);
					}
					plus = strchr(pri, '+');
					if (plus)
						*plus++ = '\0';
					if (!strcmp(pri,"hint"))
						ipri=PRIORITY_HINT;
					else if (!strcmp(pri, "next") || !strcmp(pri, "n")) {
						if (lastpri > -2)
							ipri = lastpri + 1;
						else
							ast_log(LOG_WARNING, "Can't use 'next' priority on the first entry!\n");
					} else if (!strcmp(pri, "same") || !strcmp(pri, "s")) {
						if (lastpri > -2)
							ipri = lastpri;
						else
							ast_log(LOG_WARNING, "Can't use 'same' priority on the first entry!\n");
					} else if (sscanf(pri, "%30d", &ipri) != 1 &&
					    (ipri = ast_findlabel_extension2(NULL, con, realext, pri, cidmatch)) < 1) {
						ast_log(LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
						ipri = 0;
					}
					appl = S_OR(stringp, "");
					/* Find the first occurrence of either '(' or ',' */
					firstc = strchr(appl, ',');
					firstp = strchr(appl, '(');
					if (firstc && (!firstp || firstc < firstp)) {
						/* comma found, no parenthesis */
						/* or both found, but comma found first */
						appl = strsep(&stringp, ",");
						data = stringp;
					} else if (!firstc && !firstp) {
						/* Neither found */
						data = "";
					} else {
						/* Final remaining case is parenthesis found first */
						appl = strsep(&stringp, "(");
						data = stringp;
						end = strrchr(data, ')');
						if ((end = strrchr(data, ')'))) {
							*end = '\0';
						} else {
							ast_log(LOG_WARNING, "No closing parenthesis found? '%s(%s'\n", appl, data);
						}
						ast_process_quotes_and_slashes(data, ',', '|');
					}

					if (!data)
						data="";
					appl = ast_skip_blanks(appl);
					if (ipri) {
						if (plus)
							ipri += atoi(plus);
						lastpri = ipri;
						if (!ast_opt_dont_warn && !strcmp(realext, "_."))
							ast_log(LOG_WARNING, "The use of '_.' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X.' instead at line %d\n", v->lineno);
						if (ast_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, strdup(data), ast_free_ptr, global_registrar)) {
							ast_log(LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
						}
					}
					free(tc);
				}
			} else if (!strcasecmp(v->name, "include")) {
				memset(realvalue, 0, sizeof(realvalue));
				pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				if (ast_context_add_include2(con, realvalue, global_registrar))
					ast_log(LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
			} else if (!strcasecmp(v->name, "ignorepat")) {
				memset(realvalue, 0, sizeof(realvalue));
				pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				if (ast_context_add_ignorepat2(con, realvalue, global_registrar))
					ast_log(LOG_WARNING, "Unable to include ignorepat '%s' in context '%s'\n", v->value, cxt);
			} else if (!strcasecmp(v->name, "switch") || !strcasecmp(v->name, "lswitch") || !strcasecmp(v->name, "eswitch")) {
				char *stringp= realvalue;
				char *appl, *data;

				memset(realvalue, 0, sizeof(realvalue));
				if (!strcasecmp(v->name, "switch"))
					pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				else
					ast_copy_string(realvalue, v->value, sizeof(realvalue));
				appl = strsep(&stringp, "/");
				data = strsep(&stringp, ""); /* XXX what for ? */
				if (!data)
					data = "";
				if (ast_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), global_registrar))
					ast_log(LOG_WARNING, "Unable to include switch '%s' in context '%s'\n", v->value, cxt);
			} else {
				ast_log(LOG_WARNING, "==!!== Unknown directive: %s at line %d -- IGNORING!!!\n", v->name, v->lineno);
			}
		}
	}
	ast_config_destroy(cfg);
	return 1;
}

static void __ast_context_destroy(struct ast_context *con, const char *registrar)
{
	struct ast_context *tmp, *tmpl=NULL;
	struct ast_include *tmpi;
	struct ast_sw *sw;
	struct ast_exten *e, *el, *en;
	struct ast_ignorepat *ipi;

	for (tmp = contexts; tmp; ) {
		struct ast_context *next;	/* next starting point */
		for (; tmp; tmpl = tmp, tmp = tmp->next) {
			if (option_debug)
				ast_log(LOG_DEBUG, "check ctx %s %s\n", tmp->name, tmp->registrar);
			if ( (!registrar || !strcasecmp(registrar, tmp->registrar)) &&
			     (!con || !strcasecmp(tmp->name, con->name)) )
				break;	/* found it */
		}
		if (!tmp)	/* not found, we are done */
			break;
		ast_wrlock_context(tmp);
		if (option_debug)
			ast_log(LOG_DEBUG, "delete ctx %s %s\n", tmp->name, tmp->registrar);
		next = tmp->next;
		if (tmpl)
			tmpl->next = next;
		else
			contexts = next;
		/* Okay, now we're safe to let it go -- in a sense, we were
		   ready to let it go as soon as we locked it. */
		ast_unlock_context(tmp);
		for (tmpi = tmp->includes; tmpi; ) { /* Free includes */
			struct ast_include *tmpil = tmpi;
			tmpi = tmpi->next;
			free(tmpil);
		}
		for (ipi = tmp->ignorepats; ipi; ) { /* Free ignorepats */
			struct ast_ignorepat *ipl = ipi;
			ipi = ipi->next;
			free(ipl);
		}
		while ((sw = AST_LIST_REMOVE_HEAD(&tmp->alts, list)))
			free(sw);
		for (e = tmp->root; e;) {
			for (en = e->peer; en;) {
				el = en;
				en = en->peer;
				destroy_exten(el);
			}
			el = e;
			e = e->next;
			destroy_exten(el);
		}
		ast_rwlock_destroy(&tmp->lock);
		free(tmp);
		/* if we have a specific match, we are done, otherwise continue */
		tmp = con ? NULL : next;
	}
}

void localized_context_destroy(struct ast_context *con, const char *registrar);

void localized_context_destroy(struct ast_context *con, const char *registrar)
{
	ast_wrlock_contexts();
	__ast_context_destroy(con,registrar);
	ast_unlock_contexts();
}


static void ast_merge_contexts_and_delete(struct ast_context **extcontexts, const char *registrar)
{
	struct ast_context *tmp, *lasttmp = NULL;

	/* it is very important that this function hold the hint list lock _and_ the conlock
	   during its operation; not only do we need to ensure that the list of contexts
	   and extensions does not change, but also that no hint callbacks (watchers) are
	   added or removed during the merge/delete process

	   in addition, the locks _must_ be taken in this order, because there are already
	   other code paths that use this order
	*/
	ast_wrlock_contexts();

	tmp = *extcontexts;
	if (registrar) {
		/* XXX remove previous contexts from same registrar */
		if (option_debug)
			ast_log(LOG_DEBUG, "must remove any reg %s\n", registrar);
		__ast_context_destroy(NULL,registrar);
		while (tmp) {
			lasttmp = tmp;
			tmp = tmp->next;
		}
	} else {
		/* XXX remove contexts with the same name */
		while (tmp) {
			ast_log(LOG_WARNING, "must remove %s  reg %s\n", tmp->name, tmp->registrar);
			__ast_context_destroy(tmp,tmp->registrar);
			lasttmp = tmp;
			tmp = tmp->next;
		}
	}
	if (lasttmp) {
		lasttmp->next = contexts;
		contexts = *extcontexts;
		*extcontexts = NULL;
	} else
		ast_log(LOG_WARNING, "Requested contexts didn't get merged\n");

	ast_unlock_contexts();

	return;
}

void localized_merge_contexts_and_delete(struct ast_context **extcontexts, void *tab, const char *registrar)
{
	ast_merge_contexts_and_delete(extcontexts, registrar);
}

static int ast_context_verify_includes(struct ast_context *con)
{
	struct ast_include *inc = NULL;
	int res = 0;

	while ( (inc = ast_walk_context_includes(con, inc)) )
		if (!ast_context_find(inc->rname)) {
			res = -1;
			if (strcasecmp(inc->rname,"parkedcalls")!=0)
				ast_log(LOG_WARNING, "Context '%s' tries to include the nonexistent context '%s'\n",
						ast_get_context_name(con), inc->rname);
		}
	return res;
}

int localized_context_verify_includes(struct ast_context *con);

int localized_context_verify_includes(struct ast_context *con)
{
	return ast_context_verify_includes(con);
}

int localized_pbx_load_module(void);

int localized_pbx_load_module(void)
{
	struct ast_context *con;

	if(!pbx_load_config(config_filename))
		return -1 /* AST_MODULE_LOAD_DECLINE*/;

	/* pbx_load_users(); */ /* does this affect the dialplan? */

	ast_merge_contexts_and_delete(&local_contexts, global_registrar);

	for (con = NULL; (con = ast_walk_contexts(con));)
		ast_context_verify_includes(con);

	printf("=== Loading extensions.conf ===\n");
	con = 0;
	while ((con = ast_walk_contexts(con)) ) {
		printf("Context: %s\n", con->name);
	}
	printf("=========\n");
	
	return 0;
}

/* For platforms which don't have pthread_rwlock_timedrdlock() */
struct timeval ast_tvnow(void);

struct timeval ast_tvnow(void)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return t;
}

