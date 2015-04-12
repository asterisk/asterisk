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
 *
 * \brief General Asterisk locking.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/utils.h"
#include "asterisk/lock.h"

/* Allow direct use of pthread_mutex_* / pthread_cond_* */
#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef pthread_mutex_lock
#undef pthread_mutex_trylock
#undef pthread_mutex_t
#undef pthread_mutex_unlock
#undef pthread_cond_init
#undef pthread_cond_signal
#undef pthread_cond_broadcast
#undef pthread_cond_destroy
#undef pthread_cond_wait
#undef pthread_cond_timedwait

#if defined(DEBUG_THREADS) && defined(HAVE_BKTR)
static void __dump_backtrace(struct ast_bt *bt, int canlog)
{
	char **strings;
	ssize_t i;

	strings = backtrace_symbols(bt->addresses, bt->num_frames);

	for (i = 0; i < bt->num_frames; i++) {
		__ast_mutex_logger("%s\n", strings[i]);
	}

	ast_std_free(strings);
}
#endif	/* defined(DEBUG_THREADS) && defined(HAVE_BKTR) */

#ifdef DEBUG_THREADS
AST_MUTEX_DEFINE_STATIC(reentrancy_lock);

static inline struct ast_lock_track *ast_get_reentrancy(struct ast_lock_track **plt)
{
	pthread_mutexattr_t reentr_attr;
	struct ast_lock_track *lt;

	/* It's a bit painful to lock a global mutex for every access to the
	 * reentrancy structure, but it's necessary to ensure that we don't
	 * double-allocate the structure or double-initialize the reentr_mutex.
	 *
	 * If you'd like to replace this with a double-checked lock, be sure to
	 * properly volatile-ize everything to avoid optimizer bugs.
	 *
	 * We also have to use the underlying pthread calls for manipulating
	 * the mutex, because this is called from the Asterisk mutex code.
	 */
	pthread_mutex_lock(&reentrancy_lock.mutex);

	if (*plt) {
		pthread_mutex_unlock(&reentrancy_lock.mutex);
		return *plt;
	}

	lt = *plt = ast_std_calloc(1, sizeof(*lt));
	if (!lt) {
		fprintf(stderr, "%s: Failed to allocate lock tracking\n", __func__);
#if defined(DO_CRASH) || defined(THREAD_CRASH)
		abort();
#else
		pthread_mutex_unlock(&reentrancy_lock.mutex);
		return NULL;
#endif
	}

	pthread_mutexattr_init(&reentr_attr);
	pthread_mutexattr_settype(&reentr_attr, AST_MUTEX_KIND);
	pthread_mutex_init(&lt->reentr_mutex, &reentr_attr);
	pthread_mutexattr_destroy(&reentr_attr);

	pthread_mutex_unlock(&reentrancy_lock.mutex);
	return lt;
}

static inline void delete_reentrancy_cs(struct ast_lock_track **plt)
{
	struct ast_lock_track *lt;

	if (*plt) {
		lt = *plt;
		*plt = NULL;

		pthread_mutex_destroy(&lt->reentr_mutex);
		ast_std_free(lt);
	}
}

#endif /* DEBUG_THREADS */

int __ast_pthread_mutex_init(int tracking, const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t)
{
	int res;
	pthread_mutexattr_t  attr;

#ifdef DEBUG_THREADS
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if ((t->mutex) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		int canlog = tracking && strcmp(filename, "logger.c");

		__ast_mutex_logger("%s line %d (%s): NOTICE: mutex '%s' is already initialized.\n",
				   filename, lineno, func, mutex_name);
		DO_THREAD_CRASH;
		return EBUSY;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	t->track = NULL;
	t->tracking = tracking;
#endif /* DEBUG_THREADS */

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, AST_MUTEX_KIND);
	res = pthread_mutex_init(&t->mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	return res;
}

int __ast_pthread_mutex_destroy(const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = t->track;
	int canlog = t->tracking && strcmp(filename, "logger.c");

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		/* Don't try to uninitialize an uninitialized mutex
		 * This may have no effect on linux
		 * but it always generates a core on *BSD when
		 * linked with libpthread.
		 * This is not an error condition if the mutex is created on the fly.
		 */
		__ast_mutex_logger("%s line %d (%s): NOTICE: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		DO_THREAD_CRASH;
		res = EINVAL;
		goto lt_cleanup;
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
		if (lt) {
			ast_reentrancy_lock(lt);
			__ast_mutex_logger("%s line %d (%s): Error: '%s' was locked here.\n",
				    lt->file[ROFFSET], lt->lineno[ROFFSET], lt->func[ROFFSET], mutex_name);
#ifdef HAVE_BKTR
			__dump_backtrace(&lt->backtrace[ROFFSET], canlog);
#endif
			ast_reentrancy_unlock(lt);
		}
		break;
	}
#endif /* DEBUG_THREADS */

	res = pthread_mutex_destroy(&t->mutex);

#ifdef DEBUG_THREADS
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error destroying mutex %s: %s\n",
				   filename, lineno, func, mutex_name, strerror(res));
	}
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
lt_cleanup:
#endif
	if (lt) {
		ast_reentrancy_lock(lt);
		lt->file[0] = filename;
		lt->lineno[0] = lineno;
		lt->func[0] = func;
		lt->reentrancy = 0;
		lt->thread[0] = 0;
#ifdef HAVE_BKTR
		memset(&lt->backtrace[0], 0, sizeof(lt->backtrace[0]));
#endif
		ast_reentrancy_unlock(lt);
		delete_reentrancy_cs(&t->track);
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_pthread_mutex_lock(const char *filename, int lineno, const char *func,
				const char* mutex_name, ast_mutex_t *t)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, t, bt);
#else
		ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, t);
#endif
	}
#endif /* DEBUG_THREADS */

#if defined(DETECT_DEADLOCKS) && defined(DEBUG_THREADS)
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
					ast_reentrancy_lock(lt);
#ifdef HAVE_BKTR
					__dump_backtrace(&lt->backtrace[lt->reentrancy], canlog);
#endif
					__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
							   lt->file[ROFFSET], lt->lineno[ROFFSET],
							   lt->func[ROFFSET], mutex_name);
#ifdef HAVE_BKTR
					__dump_backtrace(&lt->backtrace[ROFFSET], canlog);
#endif
					ast_reentrancy_unlock(lt);
					reported_wait = wait_time;
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#else /* !DETECT_DEADLOCKS || !DEBUG_THREADS */
#ifdef	HAVE_MTX_PROFILE
	ast_mark(mtx_prof, 1);
	res = pthread_mutex_trylock(&t->mutex);
	ast_mark(mtx_prof, 0);
	if (res)
#endif
	res = pthread_mutex_lock(&t->mutex);
#endif /* !DETECT_DEADLOCKS || !DEBUG_THREADS */

#ifdef DEBUG_THREADS
	if (lt && !res) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = lineno;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		} else {
			__ast_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
							   filename, lineno, func, mutex_name);
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
#ifdef HAVE_BKTR
		if (lt->reentrancy) {
			ast_reentrancy_lock(lt);
			bt = &lt->backtrace[lt->reentrancy-1];
			ast_reentrancy_unlock(lt);
		} else {
			bt = NULL;
		}
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error obtaining mutex: %s\n",
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_pthread_mutex_trylock(const char *filename, int lineno, const char *func,
				const char* mutex_name, ast_mutex_t *t)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, t, bt);
#else
		ast_store_lock_info(AST_MUTEX, filename, lineno, func, mutex_name, t);
#endif
	}
#endif /* DEBUG_THREADS */

	res = pthread_mutex_trylock(&t->mutex);

#ifdef DEBUG_THREADS
	if (lt && !res) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = lineno;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		} else {
			__ast_mutex_logger("%s line %d (%s): '%s' really deep reentrancy!\n",
					   filename, lineno, func, mutex_name);
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
		ast_mark_lock_failed(t);
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_pthread_mutex_unlock(const char *filename, int lineno, const char *func,
					     const char *mutex_name, ast_mutex_t *t)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		DO_THREAD_CRASH;
		return EINVAL;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy && (lt->thread[ROFFSET] != pthread_self())) {
			__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
					   filename, lineno, func, mutex_name);
			__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
					   lt->file[ROFFSET], lt->lineno[ROFFSET], lt->func[ROFFSET], mutex_name);
#ifdef HAVE_BKTR
			__dump_backtrace(&lt->backtrace[ROFFSET], canlog);
#endif
			DO_THREAD_CRASH;
		}

		if (--lt->reentrancy < 0) {
			__ast_mutex_logger("%s line %d (%s): mutex '%s' freed more times than we've locked!\n",
					   filename, lineno, func, mutex_name);
			lt->reentrancy = 0;
		}

		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = NULL;
			lt->lineno[lt->reentrancy] = 0;
			lt->func[lt->reentrancy] = NULL;
			lt->thread[lt->reentrancy] = 0;
		}

#ifdef HAVE_BKTR
		if (lt->reentrancy) {
			bt = &lt->backtrace[lt->reentrancy - 1];
		}
#endif
		ast_reentrancy_unlock(lt);

#ifdef HAVE_BKTR
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}
#endif /* DEBUG_THREADS */

	res = pthread_mutex_unlock(&t->mutex);

#ifdef DEBUG_THREADS
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error releasing mutex: %s\n",
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}


int __ast_cond_init(const char *filename, int lineno, const char *func,
				  const char *cond_name, ast_cond_t *cond, pthread_condattr_t *cond_attr)
{
	return pthread_cond_init(cond, cond_attr);
}

int __ast_cond_signal(const char *filename, int lineno, const char *func,
				    const char *cond_name, ast_cond_t *cond)
{
	return pthread_cond_signal(cond);
}

int __ast_cond_broadcast(const char *filename, int lineno, const char *func,
				       const char *cond_name, ast_cond_t *cond)
{
	return pthread_cond_broadcast(cond);
}

int __ast_cond_destroy(const char *filename, int lineno, const char *func,
				     const char *cond_name, ast_cond_t *cond)
{
	return pthread_cond_destroy(cond);
}

#ifdef DEBUG_THREADS
static void restore_lock_tracking(struct ast_lock_track *lt, struct ast_lock_track *lt_saved)
{
	ast_reentrancy_lock(lt);

	/*
	 * The following code must match the struct ast_lock_track
	 * definition with the explicit exception of the reentr_mutex
	 * member.
	 */
	memcpy(lt->file, lt_saved->file, sizeof(lt->file));
	memcpy(lt->lineno, lt_saved->lineno, sizeof(lt->lineno));
	lt->reentrancy = lt_saved->reentrancy;
	memcpy(lt->func, lt_saved->func, sizeof(lt->func));
	memcpy(lt->thread, lt_saved->thread, sizeof(lt->thread));
#ifdef HAVE_BKTR
	memcpy(lt->backtrace, lt_saved->backtrace, sizeof(lt->backtrace));
#endif

	ast_reentrancy_unlock(lt);
}
#endif /* DEBUG_THREADS */

int __ast_cond_wait(const char *filename, int lineno, const char *func,
				  const char *cond_name, const char *mutex_name,
				  ast_cond_t *cond, ast_mutex_t *t)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	struct ast_lock_track lt_orig;
	int canlog = t->tracking && strcmp(filename, "logger.c");

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		DO_THREAD_CRASH;
		return EINVAL;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy && (lt->thread[ROFFSET] != pthread_self())) {
			__ast_mutex_logger("%s line %d (%s): attempted wait using mutex '%s' without owning it!\n",
					   filename, lineno, func, mutex_name);
			__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
					   lt->file[ROFFSET], lt->lineno[ROFFSET], lt->func[ROFFSET], mutex_name);
#ifdef HAVE_BKTR
			__dump_backtrace(&lt->backtrace[ROFFSET], canlog);
#endif
			DO_THREAD_CRASH;
		} else if (lt->reentrancy <= 0) {
			__ast_mutex_logger("%s line %d (%s): attempted wait using an unlocked mutex '%s'\n",
					   filename, lineno, func, mutex_name);
			DO_THREAD_CRASH;
		}

		/* Waiting on a condition completely suspends a recursive mutex,
		 * even if it's been recursively locked multiple times. Make a
		 * copy of the lock tracking, and reset reentrancy to zero */
		lt_orig = *lt;
		lt->reentrancy = 0;
		ast_reentrancy_unlock(lt);

		ast_suspend_lock_info(t);
	}
#endif /* DEBUG_THREADS */

	res = pthread_cond_wait(cond, &t->mutex);

#ifdef DEBUG_THREADS
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error waiting on condition mutex '%s'\n",
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	} else if (lt) {
		restore_lock_tracking(lt, &lt_orig);
		ast_restore_lock_info(t);
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_cond_timedwait(const char *filename, int lineno, const char *func,
				       const char *cond_name, const char *mutex_name, ast_cond_t *cond,
				       ast_mutex_t *t, const struct timespec *abstime)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	struct ast_lock_track lt_orig;
	int canlog = t->tracking && strcmp(filename, "logger.c");

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		__ast_mutex_logger("%s line %d (%s): Error: mutex '%s' is uninitialized.\n",
				   filename, lineno, func, mutex_name);
		DO_THREAD_CRASH;
		return EINVAL;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy && (lt->thread[ROFFSET] != pthread_self())) {
			__ast_mutex_logger("%s line %d (%s): attempted wait using mutex '%s' without owning it!\n",
					   filename, lineno, func, mutex_name);
			__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
					   lt->file[ROFFSET], lt->lineno[ROFFSET], lt->func[ROFFSET], mutex_name);
#ifdef HAVE_BKTR
			__dump_backtrace(&lt->backtrace[ROFFSET], canlog);
#endif
			DO_THREAD_CRASH;
		} else if (lt->reentrancy <= 0) {
			__ast_mutex_logger("%s line %d (%s): attempted wait using an unlocked mutex '%s'\n",
					   filename, lineno, func, mutex_name);
			DO_THREAD_CRASH;
		}

		/* Waiting on a condition completely suspends a recursive mutex,
		 * even if it's been recursively locked multiple times. Make a
		 * copy of the lock tracking, and reset reentrancy to zero */
		lt_orig = *lt;
		lt->reentrancy = 0;
		ast_reentrancy_unlock(lt);

		ast_suspend_lock_info(t);
	}
#endif /* DEBUG_THREADS */

	res = pthread_cond_timedwait(cond, &t->mutex, abstime);

#ifdef DEBUG_THREADS
	if (res && (res != ETIMEDOUT)) {
		__ast_mutex_logger("%s line %d (%s): Error waiting on condition mutex '%s'\n",
				   filename, lineno, func, strerror(res));
		DO_THREAD_CRASH;
	} else if (lt) {
		restore_lock_tracking(lt, &lt_orig);
		ast_restore_lock_info(t);
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_init(int tracking, const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *t)
{
	int res;
	pthread_rwlockattr_t attr;

#ifdef DEBUG_THREADS
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if (t->lock != ((pthread_rwlock_t) __AST_RWLOCK_INIT_VALUE)) {
		int canlog = tracking && strcmp(filename, "logger.c");

		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is already initialized.\n",
				filename, lineno, func, rwlock_name);
		DO_THREAD_CRASH;
		return EBUSY;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	t->track = NULL;
	t->tracking = tracking;
#endif /* DEBUG_THREADS */

	pthread_rwlockattr_init(&attr);
#ifdef HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
#endif
	res = pthread_rwlock_init(&t->lock, &attr);
	pthread_rwlockattr_destroy(&attr);

	return res;
}

int __ast_rwlock_destroy(const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *t)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = t->track;
	int canlog = t->tracking && strcmp(filename, "logger.c");

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if (t->lock == ((pthread_rwlock_t) __AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is uninitialized.\n",
				   filename, lineno, func, rwlock_name);
		DO_THREAD_CRASH;
		res = EINVAL;
		goto lt_cleanup;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_destroy(&t->lock);

#ifdef DEBUG_THREADS
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error destroying rwlock %s: %s\n",
				filename, lineno, func, rwlock_name, strerror(res));
	}
#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
lt_cleanup:
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
	if (lt) {
		ast_reentrancy_lock(lt);
		lt->file[0] = filename;
		lt->lineno[0] = lineno;
		lt->func[0] = func;
		lt->reentrancy = 0;
		lt->thread[0] = 0;
#ifdef HAVE_BKTR
		memset(&lt->backtrace[0], 0, sizeof(lt->backtrace[0]));
#endif
		ast_reentrancy_unlock(lt);
		delete_reentrancy_cs(&t->track);
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_unlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif
	int lock_found = 0;

#if defined(AST_MUTEX_INIT_W_CONSTRUCTORS) && defined(CAN_COMPARE_MUTEX_TO_INIT_VALUE)
	if ((t->lock) == ((pthread_rwlock_t) __AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is uninitialized.\n",
				   filename, line, func, name);
		DO_THREAD_CRASH;
		return EINVAL;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy) {
			int i;
			pthread_t self = pthread_self();
			for (i = lt->reentrancy - 1; i >= 0; --i) {
				if (lt->thread[i] == self) {
					lock_found = 1;
					if (i != lt->reentrancy - 1) {
						lt->file[i] = lt->file[lt->reentrancy - 1];
						lt->lineno[i] = lt->lineno[lt->reentrancy - 1];
						lt->func[i] = lt->func[lt->reentrancy - 1];
						lt->thread[i] = lt->thread[lt->reentrancy - 1];
					}
#ifdef HAVE_BKTR
					bt = &lt->backtrace[i];
#endif
					lt->file[lt->reentrancy - 1] = NULL;
					lt->lineno[lt->reentrancy - 1] = 0;
					lt->func[lt->reentrancy - 1] = NULL;
					lt->thread[lt->reentrancy - 1] = AST_PTHREADT_NULL;
					break;
				}
			}
		}

		if (lock_found && --lt->reentrancy < 0) {
			__ast_mutex_logger("%s line %d (%s): rwlock '%s' freed more times than we've locked!\n",
					filename, line, func, name);
			lt->reentrancy = 0;
		}

		ast_reentrancy_unlock(lt);

#ifdef HAVE_BKTR
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_unlock(&t->lock);

#ifdef DEBUG_THREADS
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error releasing rwlock: %s\n",
				filename, line, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_rdlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_RDLOCK, filename, line, func, name, t, bt);
#else
		ast_store_lock_info(AST_RDLOCK, filename, line, func, name, t);
#endif
	}
#endif /* DEBUG_THREADS */

#if defined(DETECT_DEADLOCKS) && defined(DEBUG_THREADS)
	{
		time_t seconds = time(NULL);
		time_t wait_time, reported_wait = 0;
		do {
			res = pthread_rwlock_tryrdlock(&t->lock);
			if (res == EBUSY) {
				wait_time = time(NULL) - seconds;
				if (wait_time > reported_wait && (wait_time % 5) == 0) {
					__ast_mutex_logger("%s line %d (%s): Deadlock? waited %d sec for readlock '%s'?\n",
						filename, line, func, (int)wait_time, name);
					if (lt) {
						ast_reentrancy_lock(lt);
#ifdef HAVE_BKTR
						__dump_backtrace(&lt->backtrace[lt->reentrancy], canlog);
#endif
						__ast_mutex_logger("%s line %d (%s): '%s' was locked  here.\n",
								lt->file[lt->reentrancy-1], lt->lineno[lt->reentrancy-1],
								lt->func[lt->reentrancy-1], name);
#ifdef HAVE_BKTR
						__dump_backtrace(&lt->backtrace[lt->reentrancy-1], canlog);
#endif
						ast_reentrancy_unlock(lt);
					}
					reported_wait = wait_time;
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#else /* !DETECT_DEADLOCKS || !DEBUG_THREADS */
	res = pthread_rwlock_rdlock(&t->lock);
#endif /* !DETECT_DEADLOCKS || !DEBUG_THREADS */

#ifdef DEBUG_THREADS
	if (!res && lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = line;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
#ifdef HAVE_BKTR
		if (lt->reentrancy) {
			ast_reentrancy_lock(lt);
			bt = &lt->backtrace[lt->reentrancy-1];
			ast_reentrancy_unlock(lt);
		} else {
			bt = NULL;
		}
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}

	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error obtaining read lock: %s\n",
				filename, line, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_wrlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t, bt);
#else
		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t);
#endif
	}
#endif /* DEBUG_THREADS */

#if defined(DETECT_DEADLOCKS) && defined(DEBUG_THREADS)
	{
		time_t seconds = time(NULL);
		time_t wait_time, reported_wait = 0;
		do {
			res = pthread_rwlock_trywrlock(&t->lock);
			if (res == EBUSY) {
				wait_time = time(NULL) - seconds;
				if (wait_time > reported_wait && (wait_time % 5) == 0) {
					__ast_mutex_logger("%s line %d (%s): Deadlock? waited %d sec for writelock '%s'?\n",
						filename, line, func, (int)wait_time, name);
					if (lt) {
						ast_reentrancy_lock(lt);
#ifdef HAVE_BKTR
						__dump_backtrace(&lt->backtrace[lt->reentrancy], canlog);
#endif
						__ast_mutex_logger("%s line %d (%s): '%s' was locked  here.\n",
								lt->file[lt->reentrancy-1], lt->lineno[lt->reentrancy-1],
								lt->func[lt->reentrancy-1], name);
#ifdef HAVE_BKTR
						__dump_backtrace(&lt->backtrace[lt->reentrancy-1], canlog);
#endif
						ast_reentrancy_unlock(lt);
					}
					reported_wait = wait_time;
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#else /* !DETECT_DEADLOCKS || !DEBUG_THREADS */
	res = pthread_rwlock_wrlock(&t->lock);
#endif /* !DETECT_DEADLOCKS || !DEBUG_THREADS */

#ifdef DEBUG_THREADS
	if (!res && lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = line;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
#ifdef HAVE_BKTR
		if (lt->reentrancy) {
			ast_reentrancy_lock(lt);
			bt = &lt->backtrace[lt->reentrancy-1];
			ast_reentrancy_unlock(lt);
		} else {
			bt = NULL;
		}
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error obtaining write lock: %s\n",
				filename, line, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_timedrdlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name,
	const struct timespec *abs_timeout)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t, bt);
#else
		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t);
#endif
	}
#endif /* DEBUG_THREADS */

#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedrdlock(&t->lock, abs_timeout);
#else
	do {
		struct timeval _now;
		for (;;) {
			if (!(res = pthread_rwlock_tryrdlock(&t->lock))) {
				break;
			}
			_now = ast_tvnow();
			if (_now.tv_sec > abs_timeout->tv_sec || (_now.tv_sec == abs_timeout->tv_sec && _now.tv_usec * 1000 > abs_timeout->tv_nsec)) {
				break;
			}
			usleep(1);
		}
	} while (0);
#endif

#ifdef DEBUG_THREADS
	if (!res && lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = line;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
#ifdef HAVE_BKTR
		if (lt->reentrancy) {
			ast_reentrancy_lock(lt);
			bt = &lt->backtrace[lt->reentrancy-1];
			ast_reentrancy_unlock(lt);
		} else {
			bt = NULL;
		}
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error obtaining read lock: %s\n",
				filename, line, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_timedwrlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name,
	const struct timespec *abs_timeout)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
	int canlog = t->tracking && strcmp(filename, "logger.c");
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t, bt);
#else
		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t);
#endif
	}
#endif /* DEBUG_THREADS */

#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedwrlock(&t->lock, abs_timeout);
#else
	do {
		struct timeval _now;
		for (;;) {
			if (!(res = pthread_rwlock_trywrlock(&t->lock))) {
				break;
			}
			_now = ast_tvnow();
			if (_now.tv_sec > abs_timeout->tv_sec || (_now.tv_sec == abs_timeout->tv_sec && _now.tv_usec * 1000 > abs_timeout->tv_nsec)) {
				break;
			}
			usleep(1);
		}
	} while (0);
#endif

#ifdef DEBUG_THREADS
	if (!res && lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = line;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
#ifdef HAVE_BKTR
		if (lt->reentrancy) {
			ast_reentrancy_lock(lt);
			bt = &lt->backtrace[lt->reentrancy-1];
			ast_reentrancy_unlock(lt);
		} else {
			bt = NULL;
		}
		ast_remove_lock_info(t, bt);
#else
		ast_remove_lock_info(t);
#endif
	}
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error obtaining read lock: %s\n",
				filename, line, func, strerror(res));
		DO_THREAD_CRASH;
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_tryrdlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_RDLOCK, filename, line, func, name, t, bt);
#else
		ast_store_lock_info(AST_RDLOCK, filename, line, func, name, t);
#endif
	}
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_tryrdlock(&t->lock);

#ifdef DEBUG_THREADS
	if (!res && lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = line;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
		ast_mark_lock_failed(t);
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_trywrlock(const char *filename, int line, const char *func, ast_rwlock_t *t, const char *name)
{
	int res;

#ifdef DEBUG_THREADS
	struct ast_lock_track *lt = NULL;
#ifdef HAVE_BKTR
	struct ast_bt *bt = NULL;
#endif

	if (t->tracking) {
		lt = ast_get_reentrancy(&t->track);
	}

	if (lt) {
#ifdef HAVE_BKTR
		struct ast_bt tmp;

		/* The implementation of backtrace() may have its own locks.
		 * Capture the backtrace outside of the reentrancy lock to
		 * avoid deadlocks. See ASTERISK-22455. */
		ast_bt_get_addresses(&tmp);

		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->backtrace[lt->reentrancy] = tmp;
			bt = &lt->backtrace[lt->reentrancy];
		}
		ast_reentrancy_unlock(lt);

		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t, bt);
#else
		ast_store_lock_info(AST_WRLOCK, filename, line, func, name, t);
#endif
	}
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_trywrlock(&t->lock);

#ifdef DEBUG_THREADS
	if (!res && lt) {
		ast_reentrancy_lock(lt);
		if (lt->reentrancy < AST_MAX_REENTRANCY) {
			lt->file[lt->reentrancy] = filename;
			lt->lineno[lt->reentrancy] = line;
			lt->func[lt->reentrancy] = func;
			lt->thread[lt->reentrancy] = pthread_self();
			lt->reentrancy++;
		}
		ast_reentrancy_unlock(lt);
		ast_mark_lock_acquired(t);
	} else if (lt) {
		ast_mark_lock_failed(t);
	}
#endif /* DEBUG_THREADS */

	return res;
}
