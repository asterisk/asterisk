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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"

#define	ROFFSET	((t->reentrancy > 0) ? (t->reentrancy-1) : 0)

/* Allow direct use of pthread_mutex_* / pthread_cond_* */
#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef pthread_mutex_lock
#undef pthread_mutex_trylock
#undef pthread_mutex_unlock
#undef pthread_cond_init
#undef pthread_cond_signal
#undef pthread_cond_broadcast
#undef pthread_cond_destroy
#undef pthread_cond_wait
#undef pthread_cond_timedwait

int __ast_pthread_mutex_init(int track, const char *filename, int lineno, const char *func,
						const char *mutex_name, ast_mutex_t *t)
{
	int res;
	pthread_mutexattr_t  attr;

#ifdef DEBUG_THREADS
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
			    t->file[ROFFSET], t->lineno[ROFFSET], t->func[ROFFSET], mutex_name);
		ast_reentrancy_unlock(t);
		break;
	}
#endif /* DEBUG_THREADS */

	res = pthread_mutex_destroy(&t->mutex);

#ifdef DEBUG_THREADS
	if (res)
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
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_pthread_mutex_lock(const char *filename, int lineno, const char *func,
                                           const char* mutex_name, ast_mutex_t *t)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

#ifdef DETECT_DEADLOCKS
#ifdef DEBUG_THREADS
	{
		time_t seconds = time(NULL);
		time_t wait_time, reported_wait = 0;
		do {
#ifdef HAVE_MTX_PROFILE
			ast_mark(mtx_prof, 1);
#endif
			res = pthread_mutex_trylock(&t->mutex);
#ifdef HAVE_MTX_PROFILE
			ast_mark(mtx_prof, 0);
#endif
			if (res == EBUSY) {
				wait_time = time(NULL) - seconds;
				if (wait_time > reported_wait && (wait_time % 5) == 0) {
					__ast_mutex_logger("%s line %d (%s): Deadlock? waited %d sec for mutex '%s'?\n",
							   filename, lineno, func, (int) wait_time, mutex_name);
					ast_reentrancy_lock(t);
					__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
							   t->file[ROFFSET], t->lineno[ROFFSET],
							   t->func[ROFFSET], mutex_name);
					ast_reentrancy_unlock(t);
					reported_wait = wait_time;
				}
				usleep(200);
			}
		} while (res == EBUSY);
	}
#endif /* DEBUG_THREADS */
#else /* !DETECT_DEADLOCKS */
#ifdef HAVE_MTX_PROFILE
	ast_mark(mtx_prof, 1);
	res = pthread_mutex_trylock(&t->mutex);
	ast_mark(mtx_prof, 0);
	if (res)
#endif
	res = pthread_mutex_lock(&t->mutex);
#endif /* !DETECT_DEADLOCKS */

#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */
	return res;
}

int __ast_pthread_mutex_trylock(const char *filename, int lineno, const char *func,
                                              const char* mutex_name, ast_mutex_t *t)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

	res = pthread_mutex_trylock(&t->mutex);

#ifdef DEBUG_THREADS
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
	} else if (t->track) {
		ast_mark_lock_failed(&t->mutex);
	}
#endif /* DEBUG_THREADS */
	return res;
}

int __ast_pthread_mutex_unlock(const char *filename, int lineno, const char *func,
					     const char *mutex_name, ast_mutex_t *t)
{
	int res;
#ifdef DEBUG_THREADS
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
	if (t->reentrancy && (t->thread[ROFFSET] != pthread_self())) {
		__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[ROFFSET], t->lineno[ROFFSET], t->func[ROFFSET], mutex_name);
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

int __ast_cond_wait(const char *filename, int lineno, const char *func,
				  const char *cond_name, const char *mutex_name,
				  ast_cond_t *cond, ast_mutex_t *t)
{
	int res;
#ifdef DEBUG_THREADS
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
	if (t->reentrancy && (t->thread[ROFFSET] != pthread_self())) {
		__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[ROFFSET], t->lineno[ROFFSET], t->func[ROFFSET], mutex_name);
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
#endif /* DEBUG_THREADS */

	res = pthread_cond_wait(cond, &t->mutex);

#ifdef DEBUG_THREADS
	if (res) {
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
#endif /* DEBUG_THREADS */
	return res;
}

int __ast_cond_timedwait(const char *filename, int lineno, const char *func,
				       const char *cond_name, const char *mutex_name, ast_cond_t *cond,
				       ast_mutex_t *t, const struct timespec *abstime)
{
	int res;
#ifdef DEBUG_THREADS
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
	if (t->reentrancy && (t->thread[ROFFSET] != pthread_self())) {
		__ast_mutex_logger("%s line %d (%s): attempted unlock mutex '%s' without owning it!\n",
				   filename, lineno, func, mutex_name);
		__ast_mutex_logger("%s line %d (%s): '%s' was locked here.\n",
				   t->file[ROFFSET], t->lineno[ROFFSET], t->func[ROFFSET], mutex_name);
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
#endif /* DEBUG_THREADS */

	res = pthread_cond_timedwait(cond, &t->mutex, abstime);

#ifdef DEBUG_THREADS
	if (res && (res != ETIMEDOUT)) {
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
#endif /* DEBUG_THREADS */
	return res;
}



int __ast_rwlock_init(const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *prwlock)
{
	int res;
	pthread_rwlockattr_t attr;
#ifdef DEBUG_THREADS
#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
        int canlog = strcmp(filename, "logger.c");

        if (*prwlock != ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is already initialized.\n",
				filename, lineno, func, rwlock_name);
		return 0;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
#endif /* DEBUG_THREADS */
	pthread_rwlockattr_init(&attr);

#ifdef HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
#endif

	res = pthread_rwlock_init(prwlock, &attr);
	pthread_rwlockattr_destroy(&attr);
	return res;
}

int __ast_rwlock_destroy(const char *filename, int lineno, const char *func, const char *rwlock_name, ast_rwlock_t *prwlock)
{
	int res;
#ifdef DEBUG_THREADS
	int canlog = strcmp(filename, "logger.c");

#ifdef AST_MUTEX_INIT_W_CONSTRUCTORS
	if (*prwlock == ((ast_rwlock_t) AST_RWLOCK_INIT_VALUE)) {
		__ast_mutex_logger("%s line %d (%s): Warning: rwlock '%s' is uninitialized.\n",
				   filename, lineno, func, rwlock_name);
		return 0;
	}
#endif /* AST_MUTEX_INIT_W_CONSTRUCTORS */
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_destroy(prwlock);

#ifdef DEBUG_THREADS
	if (res) {
		__ast_mutex_logger("%s line %d (%s): Error destroying rwlock %s: %s\n",
				filename, lineno, func, rwlock_name, strerror(res));
	}
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_rdlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_rdlock(lock);

#ifdef DEBUG_THREADS
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */
	return res;
}

int __ast_rwlock_wrlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_wrlock(lock);

#ifdef DEBUG_THREADS
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_tryrdlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_tryrdlock(lock);

#ifdef DEBUG_THREADS
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_trywrlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_trywrlock(lock);

#ifdef DEBUG_THREADS
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_unlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

	res = pthread_rwlock_unlock(lock);

#ifdef DEBUG_THREADS
	ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */
	return res;
}

int __ast_rwlock_timedrdlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock, const struct timespec *abs_timeout)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedrdlock(lock, abs_timeout);
#else
	do {
		struct timeval _start = ast_tvnow(), _diff;
		for (;;) {
			if (!(res = pthread_rwlock_tryrdlock(lock))) {
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

#ifdef DEBUG_THREADS
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */

	return res;
}

int __ast_rwlock_timedwrlock(const char *file, int line, const char *func, const char *name, ast_rwlock_t *lock, const struct timespec *abs_timeout)
{
	int res;
#ifdef DEBUG_THREADS
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
#endif /* DEBUG_THREADS */

#ifdef HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK
	res = pthread_rwlock_timedwrlock(lock, abs_timeout);
#else
	do {
		struct timeval _start = ast_tvnow(), _diff;
		for (;;) {
			if (!(res = pthread_rwlock_trywrlock(lock))) {
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

#ifdef DEBUG_THREADS
	if (!res)
		ast_mark_lock_acquired(lock);
	else
		ast_remove_lock_info(lock);
#endif /* DEBUG_THREADS */

	return res;
}
