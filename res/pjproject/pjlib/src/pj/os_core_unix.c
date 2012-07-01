/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
/*
 * Contributors:
 * - Thanks for Zetron, Inc. (Phil Torre, ptorre@zetron.com) for donating
 *   the RTEMS port.
 */
#ifndef _GNU_SOURCE
#   define _GNU_SOURCE
#endif
#include <pj/os.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/log.h>
#include <pj/rand.h>
#include <pj/string.h>
#include <pj/guid.h>
#include <pj/except.h>
#include <pj/errno.h>

#if defined(PJ_HAS_SEMAPHORE_H) && PJ_HAS_SEMAPHORE_H != 0
#  include <semaphore.h>
#endif

#include <unistd.h>	    // getpid()
#include <errno.h>	    // errno

#include <pthread.h>

#define THIS_FILE   "os_core_unix.c"

#define SIGNATURE1  0xDEAFBEEF
#define SIGNATURE2  0xDEADC0DE

struct pj_thread_t
{
    char	    obj_name[PJ_MAX_OBJ_NAME];
    pthread_t	    thread;
    pj_thread_proc *proc;
    void	   *arg;
    pj_uint32_t	    signature1;
    pj_uint32_t	    signature2;

    pj_mutex_t	   *suspended_mutex;

#if defined(PJ_OS_HAS_CHECK_STACK) && PJ_OS_HAS_CHECK_STACK!=0
    pj_uint32_t	    stk_size;
    pj_uint32_t	    stk_max_usage;
    char	   *stk_start;
    const char	   *caller_file;
    int		    caller_line;
#endif
};

struct pj_atomic_t
{
    pj_mutex_t	       *mutex;
    pj_atomic_value_t	value;
};

struct pj_mutex_t
{
    pthread_mutex_t     mutex;
    char		obj_name[PJ_MAX_OBJ_NAME];
#if PJ_DEBUG
    int		        nesting_level;
    pj_thread_t	       *owner;
    char		owner_name[PJ_MAX_OBJ_NAME];
#endif
};

#if defined(PJ_HAS_SEMAPHORE) && PJ_HAS_SEMAPHORE != 0
struct pj_sem_t
{
    sem_t	       *sem;
    char		obj_name[PJ_MAX_OBJ_NAME];
};
#endif /* PJ_HAS_SEMAPHORE */

#if defined(PJ_HAS_EVENT_OBJ) && PJ_HAS_EVENT_OBJ != 0
struct pj_event_t
{
    char		obj_name[PJ_MAX_OBJ_NAME];
};
#endif	/* PJ_HAS_EVENT_OBJ */


/*
 * Flag and reference counter for PJLIB instance.
 */
static int initialized;

#if PJ_HAS_THREADS
    static pj_thread_t main_thread;
    static long thread_tls_id;
    static pj_mutex_t critical_section;
#else
#   define MAX_THREADS 32
    static int tls_flag[MAX_THREADS];
    static void *tls[MAX_THREADS];
#endif

static unsigned atexit_count;
static void (*atexit_func[32])(void);

static pj_status_t init_mutex(pj_mutex_t *mutex, const char *name, int type);

/*
 * pj_init(void).
 * Init PJLIB!
 */
PJ_DEF(pj_status_t) pj_init(void)
{
    char dummy_guid[PJ_GUID_MAX_LENGTH];
    pj_str_t guid;
    pj_status_t rc;

    /* Check if PJLIB have been initialized */
    if (initialized) {
	++initialized;
	return PJ_SUCCESS;
    }

#if PJ_HAS_THREADS
    /* Init this thread's TLS. */
    if ((rc=pj_thread_init()) != 0) {
	return rc;
    }

    /* Critical section. */
    if ((rc=init_mutex(&critical_section, "critsec", PJ_MUTEX_RECURSE)) != 0)
	return rc;

#endif

    /* Init logging */
    pj_log_init();

    /* Initialize exception ID for the pool. 
     * Must do so after critical section is configured.
     */
    rc = pj_exception_id_alloc("PJLIB/No memory", &PJ_NO_MEMORY_EXCEPTION);
    if (rc != PJ_SUCCESS)
        return rc;
    
    /* Init random seed. */
    /* Or probably not. Let application in charge of this */
    /* pj_srand( clock() ); */

    /* Startup GUID. */
    guid.ptr = dummy_guid;
    pj_generate_unique_string( &guid );

    /* Startup timestamp */
#if defined(PJ_HAS_HIGH_RES_TIMER) && PJ_HAS_HIGH_RES_TIMER != 0
    {
	pj_timestamp dummy_ts;
	if ((rc=pj_get_timestamp(&dummy_ts)) != 0) {
	    return rc;
	}
    }
#endif   

    /* Flag PJLIB as initialized */
    ++initialized;
    pj_assert(initialized == 1);

    PJ_LOG(4,(THIS_FILE, "pjlib %s for POSIX initialized",
	      PJ_VERSION));

    return PJ_SUCCESS;
}

/*
 * pj_atexit()
 */
PJ_DEF(pj_status_t) pj_atexit(void (*func)(void))
{
    if (atexit_count >= PJ_ARRAY_SIZE(atexit_func))
	return PJ_ETOOMANY;

    atexit_func[atexit_count++] = func;
    return PJ_SUCCESS;
}

/*
 * pj_shutdown(void)
 */
PJ_DEF(void) pj_shutdown()
{
    int i;

    /* Only perform shutdown operation when 'initialized' reaches zero */
    pj_assert(initialized > 0);
    if (--initialized != 0)
	return;

    /* Call atexit() functions */
    for (i=atexit_count-1; i>=0; --i) {
	(*atexit_func[i])();
    }
    atexit_count = 0;

    /* Free exception ID */
    if (PJ_NO_MEMORY_EXCEPTION != -1) {
	pj_exception_id_free(PJ_NO_MEMORY_EXCEPTION);
	PJ_NO_MEMORY_EXCEPTION = -1;
    }

#if PJ_HAS_THREADS
    /* Destroy PJLIB critical section */
    pj_mutex_destroy(&critical_section);

    /* Free PJLIB TLS */
    if (thread_tls_id != -1) {
	pj_thread_local_free(thread_tls_id);
	thread_tls_id = -1;
    }

    /* Ticket #1132: Assertion when (re)starting PJLIB on different thread */
    pj_bzero(&main_thread, sizeof(main_thread));
#endif

    /* Clear static variables */
    pj_errno_clear_handlers();
}


/*
 * pj_getpid(void)
 */
PJ_DEF(pj_uint32_t) pj_getpid(void)
{
    PJ_CHECK_STACK();
    return getpid();
}

/*
 * Check if this thread has been registered to PJLIB.
 */
PJ_DEF(pj_bool_t) pj_thread_is_registered(void)
{
#if PJ_HAS_THREADS
    return pj_thread_local_get(thread_tls_id) != 0;
#else
    pj_assert("pj_thread_is_registered() called in non-threading mode!");
    return PJ_TRUE;
#endif
}


/*
 * Get thread priority value for the thread.
 */
PJ_DEF(int) pj_thread_get_prio(pj_thread_t *thread)
{
#if PJ_HAS_THREADS
    struct sched_param param;
    int policy;
    int rc;

    rc = pthread_getschedparam (thread->thread, &policy, &param);
    if (rc != 0)
	return -1;

    return param.sched_priority;
#else
    PJ_UNUSED_ARG(thread);
    return 1;
#endif
}


/*
 * Set the thread priority.
 */
PJ_DEF(pj_status_t) pj_thread_set_prio(pj_thread_t *thread,  int prio)
{
#if PJ_HAS_THREADS
    struct sched_param param;
    int policy;
    int rc;

    rc = pthread_getschedparam (thread->thread, &policy, &param);
    if (rc != 0)
	return PJ_RETURN_OS_ERROR(rc);

    param.sched_priority = prio;

    rc = pthread_setschedparam(thread->thread, policy, &param);
    if (rc != 0)
	return PJ_RETURN_OS_ERROR(rc);

    return PJ_SUCCESS;
#else
    PJ_UNUSED_ARG(thread);
    PJ_UNUSED_ARG(prio);
    pj_assert("pj_thread_set_prio() called in non-threading mode!");
    return 1;
#endif
}


/*
 * Get the lowest priority value available on this system.
 */
PJ_DEF(int) pj_thread_get_prio_min(pj_thread_t *thread)
{
    struct sched_param param;
    int policy;
    int rc;

    rc = pthread_getschedparam(thread->thread, &policy, &param);
    if (rc != 0)
	return -1;

#if defined(_POSIX_PRIORITY_SCHEDULING)
    return sched_get_priority_min(policy);
#elif defined __OpenBSD__
    /* Thread prio min/max are declared in OpenBSD private hdr */
    return 0;
#else
    pj_assert("pj_thread_get_prio_min() not supported!");
    return 0;
#endif
}


/*
 * Get the highest priority value available on this system.
 */
PJ_DEF(int) pj_thread_get_prio_max(pj_thread_t *thread)
{
    struct sched_param param;
    int policy;
    int rc;

    rc = pthread_getschedparam(thread->thread, &policy, &param);
    if (rc != 0)
	return -1;

#if defined(_POSIX_PRIORITY_SCHEDULING)
    return sched_get_priority_max(policy);
#elif defined __OpenBSD__
    /* Thread prio min/max are declared in OpenBSD private hdr */
    return 31;
#else
    pj_assert("pj_thread_get_prio_max() not supported!");
    return 0;
#endif
}


/*
 * Get native thread handle
 */
PJ_DEF(void*) pj_thread_get_os_handle(pj_thread_t *thread) 
{
    PJ_ASSERT_RETURN(thread, NULL);

#if PJ_HAS_THREADS
    return &thread->thread;
#else
    pj_assert("pj_thread_is_registered() called in non-threading mode!");
    return NULL;
#endif
}

/*
 * pj_thread_register(..)
 */
PJ_DEF(pj_status_t) pj_thread_register ( const char *cstr_thread_name,
					 pj_thread_desc desc,
					 pj_thread_t **ptr_thread)
{
#if PJ_HAS_THREADS
    char stack_ptr;
    pj_status_t rc;
    pj_thread_t *thread = (pj_thread_t *)desc;
    pj_str_t thread_name = pj_str((char*)cstr_thread_name);

    /* Size sanity check. */
    if (sizeof(pj_thread_desc) < sizeof(pj_thread_t)) {
	pj_assert(!"Not enough pj_thread_desc size!");
	return PJ_EBUG;
    }

    /* Warn if this thread has been registered before */
    if (pj_thread_local_get (thread_tls_id) != 0) {
	// 2006-02-26 bennylp:
	//  This wouldn't work in all cases!.
	//  If thread is created by external module (e.g. sound thread),
	//  thread may be reused while the pool used for the thread descriptor
	//  has been deleted by application.
	//*thread_ptr = (pj_thread_t*)pj_thread_local_get (thread_tls_id);
        //return PJ_SUCCESS;
	PJ_LOG(4,(THIS_FILE, "Info: possibly re-registering existing "
			     "thread"));
    }

    /* On the other hand, also warn if the thread descriptor buffer seem to
     * have been used to register other threads.
     */
    pj_assert(thread->signature1 != SIGNATURE1 ||
	      thread->signature2 != SIGNATURE2 ||
	      (thread->thread == pthread_self()));

    /* Initialize and set the thread entry. */
    pj_bzero(desc, sizeof(struct pj_thread_t));
    thread->thread = pthread_self();
    thread->signature1 = SIGNATURE1;
    thread->signature2 = SIGNATURE2;

    if(cstr_thread_name && pj_strlen(&thread_name) < sizeof(thread->obj_name)-1)
	pj_ansi_snprintf(thread->obj_name, sizeof(thread->obj_name), 
			 cstr_thread_name, thread->thread);
    else
	pj_ansi_snprintf(thread->obj_name, sizeof(thread->obj_name), 
			 "thr%p", (void*)thread->thread);
    
    rc = pj_thread_local_set(thread_tls_id, thread);
    if (rc != PJ_SUCCESS) {
	pj_bzero(desc, sizeof(struct pj_thread_t));
	return rc;
    }

#if defined(PJ_OS_HAS_CHECK_STACK) && PJ_OS_HAS_CHECK_STACK!=0
    thread->stk_start = &stack_ptr;
    thread->stk_size = 0xFFFFFFFFUL;
    thread->stk_max_usage = 0;
#else
    stack_ptr = '\0';
#endif

    *ptr_thread = thread;
    return PJ_SUCCESS;
#else
    pj_thread_t *thread = (pj_thread_t*)desc;
    *ptr_thread = thread;
    return PJ_SUCCESS;
#endif
}

/*
 * pj_thread_init(void)
 */
pj_status_t pj_thread_init(void)
{
#if PJ_HAS_THREADS
    pj_status_t rc;
    pj_thread_t *dummy;

    rc = pj_thread_local_alloc(&thread_tls_id );
    if (rc != PJ_SUCCESS) {
	return rc;
    }
    return pj_thread_register("thr%p", (long*)&main_thread, &dummy);
#else
    PJ_LOG(2,(THIS_FILE, "Thread init error. Threading is not enabled!"));
    return PJ_EINVALIDOP;
#endif
}

#if PJ_HAS_THREADS
/*
 * thread_main()
 *
 * This is the main entry for all threads.
 */
static void *thread_main(void *param)
{
    pj_thread_t *rec = (pj_thread_t*)param;
    void *result;
    pj_status_t rc;

#if defined(PJ_OS_HAS_CHECK_STACK) && PJ_OS_HAS_CHECK_STACK!=0
    rec->stk_start = (char*)&rec;
#endif

    /* Set current thread id. */
    rc = pj_thread_local_set(thread_tls_id, rec);
    if (rc != PJ_SUCCESS) {
	pj_assert(!"Thread TLS ID is not set (pj_init() error?)");
    }

    /* Check if suspension is required. */
    if (rec->suspended_mutex) {
	pj_mutex_lock(rec->suspended_mutex);
	pj_mutex_unlock(rec->suspended_mutex);
    }

    PJ_LOG(6,(rec->obj_name, "Thread started"));

    /* Call user's entry! */
    result = (void*)(long)(*rec->proc)(rec->arg);

    /* Done. */
    PJ_LOG(6,(rec->obj_name, "Thread quitting"));

    return result;
}
#endif

/*
 * pj_thread_create(...)
 */
PJ_DEF(pj_status_t) pj_thread_create( pj_pool_t *pool, 
				      const char *thread_name,
				      pj_thread_proc *proc, 
				      void *arg,
				      pj_size_t stack_size, 
				      unsigned flags,
				      pj_thread_t **ptr_thread)
{
#if PJ_HAS_THREADS
    pj_thread_t *rec;
    pthread_attr_t thread_attr;
    void *stack_addr;
    int rc;

    PJ_UNUSED_ARG(stack_addr);

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(pool && proc && ptr_thread, PJ_EINVAL);

    /* Create thread record and assign name for the thread */
    rec = (struct pj_thread_t*) pj_pool_zalloc(pool, sizeof(pj_thread_t));
    PJ_ASSERT_RETURN(rec, PJ_ENOMEM);
    
    /* Set name. */
    if (!thread_name) 
	thread_name = "thr%p";
    
    if (strchr(thread_name, '%')) {
	pj_ansi_snprintf(rec->obj_name, PJ_MAX_OBJ_NAME, thread_name, rec);
    } else {
	strncpy(rec->obj_name, thread_name, PJ_MAX_OBJ_NAME);
	rec->obj_name[PJ_MAX_OBJ_NAME-1] = '\0';
    }

    /* Set default stack size */
    if (stack_size == 0)
	stack_size = PJ_THREAD_DEFAULT_STACK_SIZE;

#if defined(PJ_OS_HAS_CHECK_STACK) && PJ_OS_HAS_CHECK_STACK!=0
    rec->stk_size = stack_size;
    rec->stk_max_usage = 0;
#endif

    /* Emulate suspended thread with mutex. */
    if (flags & PJ_THREAD_SUSPENDED) {
	rc = pj_mutex_create_simple(pool, NULL, &rec->suspended_mutex);
	if (rc != PJ_SUCCESS) {
	    return rc;
	}

	pj_mutex_lock(rec->suspended_mutex);
    } else {
	pj_assert(rec->suspended_mutex == NULL);
    }
    

    /* Init thread attributes */
    pthread_attr_init(&thread_attr);

#if defined(PJ_THREAD_SET_STACK_SIZE) && PJ_THREAD_SET_STACK_SIZE!=0
    /* Set thread's stack size */
    rc = pthread_attr_setstacksize(&thread_attr, stack_size);
    if (rc != 0)
	return PJ_RETURN_OS_ERROR(rc);
#endif	/* PJ_THREAD_SET_STACK_SIZE */


#if defined(PJ_THREAD_ALLOCATE_STACK) && PJ_THREAD_ALLOCATE_STACK!=0
    /* Allocate memory for the stack */
    stack_addr = pj_pool_alloc(pool, stack_size);
    PJ_ASSERT_RETURN(stack_addr, PJ_ENOMEM);

    rc = pthread_attr_setstackaddr(&thread_attr, stack_addr);
    if (rc != 0)
	return PJ_RETURN_OS_ERROR(rc);
#endif	/* PJ_THREAD_ALLOCATE_STACK */


    /* Create the thread. */
    rec->proc = proc;
    rec->arg = arg;
    rc = pthread_create( &rec->thread, &thread_attr, &thread_main, rec);
    if (rc != 0) {
	return PJ_RETURN_OS_ERROR(rc);
    }

    *ptr_thread = rec;

    PJ_LOG(6, (rec->obj_name, "Thread created"));
    return PJ_SUCCESS;
#else
    pj_assert(!"Threading is disabled!");
    return PJ_EINVALIDOP;
#endif
}

/*
 * pj_thread-get_name()
 */
PJ_DEF(const char*) pj_thread_get_name(pj_thread_t *p)
{
#if PJ_HAS_THREADS
    pj_thread_t *rec = (pj_thread_t*)p;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(p, "");

    return rec->obj_name;
#else
    return "";
#endif
}

/*
 * pj_thread_resume()
 */
PJ_DEF(pj_status_t) pj_thread_resume(pj_thread_t *p)
{
    pj_status_t rc;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(p, PJ_EINVAL);

    rc = pj_mutex_unlock(p->suspended_mutex);

    return rc;
}

/*
 * pj_thread_this()
 */
PJ_DEF(pj_thread_t*) pj_thread_this(void)
{
#if PJ_HAS_THREADS
    pj_thread_t *rec = (pj_thread_t*)pj_thread_local_get(thread_tls_id);
    
    if (rec == NULL) {
	pj_assert(!"Calling pjlib from unknown/external thread. You must "
		   "register external threads with pj_thread_register() "
		   "before calling any pjlib functions.");
    }

    /*
     * MUST NOT check stack because this function is called
     * by PJ_CHECK_STACK() itself!!!
     *
     */

    return rec;
#else
    pj_assert(!"Threading is not enabled!");
    return NULL;
#endif
}

/*
 * pj_thread_join()
 */
PJ_DEF(pj_status_t) pj_thread_join(pj_thread_t *p)
{
#if PJ_HAS_THREADS
    pj_thread_t *rec = (pj_thread_t *)p;
    void *ret;
    int result;

    PJ_CHECK_STACK();

    PJ_LOG(6, (pj_thread_this()->obj_name, "Joining thread %s", p->obj_name));
    result = pthread_join( rec->thread, &ret);

    if (result == 0)
	return PJ_SUCCESS;
    else {
	/* Calling pthread_join() on a thread that no longer exists and 
	 * getting back ESRCH isn't an error (in this context). 
	 * Thanks Phil Torre <ptorre@zetron.com>.
	 */
	return result==ESRCH ? PJ_SUCCESS : PJ_RETURN_OS_ERROR(result);
    }
#else
    PJ_CHECK_STACK();
    pj_assert(!"No multithreading support!");
    return PJ_EINVALIDOP;
#endif
}

/*
 * pj_thread_destroy()
 */
PJ_DEF(pj_status_t) pj_thread_destroy(pj_thread_t *p)
{
    PJ_CHECK_STACK();

    /* Destroy mutex used to suspend thread */
    if (p->suspended_mutex) {
	pj_mutex_destroy(p->suspended_mutex);
	p->suspended_mutex = NULL;
    }

    return PJ_SUCCESS;
}

/*
 * pj_thread_sleep()
 */
PJ_DEF(pj_status_t) pj_thread_sleep(unsigned msec)
{
/* TODO: should change this to something like PJ_OS_HAS_NANOSLEEP */
#if defined(PJ_RTEMS) && PJ_RTEMS!=0
    enum { NANOSEC_PER_MSEC = 1000000 };
    struct timespec req;

    PJ_CHECK_STACK();
    req.tv_sec = msec / 1000;
    req.tv_nsec = (msec % 1000) * NANOSEC_PER_MSEC;

    if (nanosleep(&req, NULL) == 0)
	return PJ_SUCCESS;

    return PJ_RETURN_OS_ERROR(pj_get_native_os_error());
#else
    PJ_CHECK_STACK();

    pj_set_os_error(0);

    usleep(msec * 1000);

    /* MacOS X (reported on 10.5) seems to always set errno to ETIMEDOUT.
     * It does so because usleep() is declared to return int, and we're
     * supposed to check for errno only when usleep() returns non-zero. 
     * Unfortunately, usleep() is declared to return void in other platforms
     * so it's not possible to always check for the return value (unless 
     * we add a detection routine in autoconf).
     *
     * As a workaround, here we check if ETIMEDOUT is returned and
     * return successfully if it is.
     */
    if (pj_get_native_os_error() == ETIMEDOUT)
	return PJ_SUCCESS;

    return pj_get_os_error();

#endif	/* PJ_RTEMS */
}

#if defined(PJ_OS_HAS_CHECK_STACK) && PJ_OS_HAS_CHECK_STACK!=0
/*
 * pj_thread_check_stack()
 * Implementation for PJ_CHECK_STACK()
 */
PJ_DEF(void) pj_thread_check_stack(const char *file, int line)
{
    char stk_ptr;
    pj_uint32_t usage;
    pj_thread_t *thread = pj_thread_this();

    /* Calculate current usage. */
    usage = (&stk_ptr > thread->stk_start) ? &stk_ptr - thread->stk_start :
		thread->stk_start - &stk_ptr;

    /* Assert if stack usage is dangerously high. */
    pj_assert("STACK OVERFLOW!! " && (usage <= thread->stk_size - 128));

    /* Keep statistic. */
    if (usage > thread->stk_max_usage) {
	thread->stk_max_usage = usage;
	thread->caller_file = file;
	thread->caller_line = line;
    }
}

/*
 * pj_thread_get_stack_max_usage()
 */
PJ_DEF(pj_uint32_t) pj_thread_get_stack_max_usage(pj_thread_t *thread)
{
    return thread->stk_max_usage;
}

/*
 * pj_thread_get_stack_info()
 */
PJ_DEF(pj_status_t) pj_thread_get_stack_info( pj_thread_t *thread,
					      const char **file,
					      int *line )
{
    pj_assert(thread);

    *file = thread->caller_file;
    *line = thread->caller_line;
    return 0;
}

#endif	/* PJ_OS_HAS_CHECK_STACK */

///////////////////////////////////////////////////////////////////////////////
/*
 * pj_atomic_create()
 */
PJ_DEF(pj_status_t) pj_atomic_create( pj_pool_t *pool, 
				      pj_atomic_value_t initial,
				      pj_atomic_t **ptr_atomic)
{
    pj_status_t rc;
    pj_atomic_t *atomic_var;

    atomic_var = PJ_POOL_ZALLOC_T(pool, pj_atomic_t);

    PJ_ASSERT_RETURN(atomic_var, PJ_ENOMEM);
    
#if PJ_HAS_THREADS
    rc = pj_mutex_create(pool, "atm%p", PJ_MUTEX_SIMPLE, &atomic_var->mutex);
    if (rc != PJ_SUCCESS)
	return rc;
#endif
    atomic_var->value = initial;

    *ptr_atomic = atomic_var;
    return PJ_SUCCESS;
}

/*
 * pj_atomic_destroy()
 */
PJ_DEF(pj_status_t) pj_atomic_destroy( pj_atomic_t *atomic_var )
{
    PJ_ASSERT_RETURN(atomic_var, PJ_EINVAL);
#if PJ_HAS_THREADS
    return pj_mutex_destroy( atomic_var->mutex );
#else
    return 0;
#endif
}

/*
 * pj_atomic_set()
 */
PJ_DEF(void) pj_atomic_set(pj_atomic_t *atomic_var, pj_atomic_value_t value)
{
    PJ_CHECK_STACK();

#if PJ_HAS_THREADS
    pj_mutex_lock( atomic_var->mutex );
#endif
    atomic_var->value = value;
#if PJ_HAS_THREADS
    pj_mutex_unlock( atomic_var->mutex);
#endif 
}

/*
 * pj_atomic_get()
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_get(pj_atomic_t *atomic_var)
{
    pj_atomic_value_t oldval;
    
    PJ_CHECK_STACK();

#if PJ_HAS_THREADS
    pj_mutex_lock( atomic_var->mutex );
#endif
    oldval = atomic_var->value;
#if PJ_HAS_THREADS
    pj_mutex_unlock( atomic_var->mutex);
#endif
    return oldval;
}

/*
 * pj_atomic_inc_and_get()
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_inc_and_get(pj_atomic_t *atomic_var)
{
    pj_atomic_value_t new_value;

    PJ_CHECK_STACK();

#if PJ_HAS_THREADS
    pj_mutex_lock( atomic_var->mutex );
#endif
    new_value = ++atomic_var->value;
#if PJ_HAS_THREADS
    pj_mutex_unlock( atomic_var->mutex);
#endif

    return new_value;
}
/*
 * pj_atomic_inc()
 */
PJ_DEF(void) pj_atomic_inc(pj_atomic_t *atomic_var)
{
    pj_atomic_inc_and_get(atomic_var);
}

/*
 * pj_atomic_dec_and_get()
 */
PJ_DEF(pj_atomic_value_t) pj_atomic_dec_and_get(pj_atomic_t *atomic_var)
{
    pj_atomic_value_t new_value;

    PJ_CHECK_STACK();

#if PJ_HAS_THREADS
    pj_mutex_lock( atomic_var->mutex );
#endif
    new_value = --atomic_var->value;
#if PJ_HAS_THREADS
    pj_mutex_unlock( atomic_var->mutex);
#endif

    return new_value;
}

/*
 * pj_atomic_dec()
 */
PJ_DEF(void) pj_atomic_dec(pj_atomic_t *atomic_var)
{
    pj_atomic_dec_and_get(atomic_var);
}

/*
 * pj_atomic_add_and_get()
 */ 
PJ_DEF(pj_atomic_value_t) pj_atomic_add_and_get( pj_atomic_t *atomic_var, 
                                                 pj_atomic_value_t value )
{
    pj_atomic_value_t new_value;

#if PJ_HAS_THREADS
    pj_mutex_lock(atomic_var->mutex);
#endif
    
    atomic_var->value += value;
    new_value = atomic_var->value;

#if PJ_HAS_THREADS
    pj_mutex_unlock(atomic_var->mutex);
#endif

    return new_value;
}

/*
 * pj_atomic_add()
 */ 
PJ_DEF(void) pj_atomic_add( pj_atomic_t *atomic_var, 
                            pj_atomic_value_t value )
{
    pj_atomic_add_and_get(atomic_var, value);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * pj_thread_local_alloc()
 */
PJ_DEF(pj_status_t) pj_thread_local_alloc(long *p_index)
{
#if PJ_HAS_THREADS
    pthread_key_t key;
    int rc;

    PJ_ASSERT_RETURN(p_index != NULL, PJ_EINVAL);

    pj_assert( sizeof(pthread_key_t) <= sizeof(long));
    if ((rc=pthread_key_create(&key, NULL)) != 0)
	return PJ_RETURN_OS_ERROR(rc);

    *p_index = key;
    return PJ_SUCCESS;
#else
    int i;
    for (i=0; i<MAX_THREADS; ++i) {
	if (tls_flag[i] == 0)
	    break;
    }
    if (i == MAX_THREADS) 
	return PJ_ETOOMANY;
    
    tls_flag[i] = 1;
    tls[i] = NULL;

    *p_index = i;
    return PJ_SUCCESS;
#endif
}

/*
 * pj_thread_local_free()
 */
PJ_DEF(void) pj_thread_local_free(long index)
{
    PJ_CHECK_STACK();
#if PJ_HAS_THREADS
    pthread_key_delete(index);
#else
    tls_flag[index] = 0;
#endif
}

/*
 * pj_thread_local_set()
 */
PJ_DEF(pj_status_t) pj_thread_local_set(long index, void *value)
{
    //Can't check stack because this function is called in the
    //beginning before main thread is initialized.
    //PJ_CHECK_STACK();
#if PJ_HAS_THREADS
    int rc=pthread_setspecific(index, value);
    return rc==0 ? PJ_SUCCESS : PJ_RETURN_OS_ERROR(rc);
#else
    pj_assert(index >= 0 && index < MAX_THREADS);
    tls[index] = value;
    return PJ_SUCCESS;
#endif
}

PJ_DEF(void*) pj_thread_local_get(long index)
{
    //Can't check stack because this function is called
    //by PJ_CHECK_STACK() itself!!!
    //PJ_CHECK_STACK();
#if PJ_HAS_THREADS
    return pthread_getspecific(index);
#else
    pj_assert(index >= 0 && index < MAX_THREADS);
    return tls[index];
#endif
}

///////////////////////////////////////////////////////////////////////////////
PJ_DEF(void) pj_enter_critical_section(void)
{
#if PJ_HAS_THREADS
    pj_mutex_lock(&critical_section);
#endif
}

PJ_DEF(void) pj_leave_critical_section(void)
{
#if PJ_HAS_THREADS
    pj_mutex_unlock(&critical_section);
#endif
}


///////////////////////////////////////////////////////////////////////////////
#if defined(PJ_LINUX) && PJ_LINUX!=0
PJ_BEGIN_DECL
PJ_DECL(int) pthread_mutexattr_settype(pthread_mutexattr_t*,int);
PJ_END_DECL
#endif

static pj_status_t init_mutex(pj_mutex_t *mutex, const char *name, int type)
{
#if PJ_HAS_THREADS
    pthread_mutexattr_t attr;
    int rc;

    PJ_CHECK_STACK();

    rc = pthread_mutexattr_init(&attr);
    if (rc != 0)
	return PJ_RETURN_OS_ERROR(rc);

    if (type == PJ_MUTEX_SIMPLE) {
#if (defined(PJ_LINUX) && PJ_LINUX!=0) || \
    defined(PJ_HAS_PTHREAD_MUTEXATTR_SETTYPE)
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_FAST_NP);
#elif (defined(PJ_RTEMS) && PJ_RTEMS!=0) || \
       defined(PJ_PTHREAD_MUTEXATTR_T_HAS_RECURSIVE)
	/* Nothing to do, default is simple */
#else
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
#endif
    } else {
#if (defined(PJ_LINUX) && PJ_LINUX!=0) || \
     defined(PJ_HAS_PTHREAD_MUTEXATTR_SETTYPE)
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
#elif (defined(PJ_RTEMS) && PJ_RTEMS!=0) || \
       defined(PJ_PTHREAD_MUTEXATTR_T_HAS_RECURSIVE)
	// Phil Torre <ptorre@zetron.com>:
	// The RTEMS implementation of POSIX mutexes doesn't include 
	// pthread_mutexattr_settype(), so what follows is a hack
	// until I get RTEMS patched to support the set/get functions.
	//
	// More info:
	//   newlib's pthread also lacks pthread_mutexattr_settype(),
	//   but it seems to have mutexattr.recursive.
	PJ_TODO(FIX_RTEMS_RECURSIVE_MUTEX_TYPE)
	attr.recursive = 1;
#else
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#endif
    }
    
    if (rc != 0) {
	return PJ_RETURN_OS_ERROR(rc);
    }

    rc = pthread_mutex_init(&mutex->mutex, &attr);
    if (rc != 0) {
	return PJ_RETURN_OS_ERROR(rc);
    }
    
    rc = pthread_mutexattr_destroy(&attr);
    if (rc != 0) {
	pj_status_t status = PJ_RETURN_OS_ERROR(rc);
	pthread_mutex_destroy(&mutex->mutex);
	return status;
    }

#if PJ_DEBUG
    /* Set owner. */
    mutex->nesting_level = 0;
    mutex->owner = NULL;
#endif

    /* Set name. */
    if (!name) {
	name = "mtx%p";
    }
    if (strchr(name, '%')) {
	pj_ansi_snprintf(mutex->obj_name, PJ_MAX_OBJ_NAME, name, mutex);
    } else {
	strncpy(mutex->obj_name, name, PJ_MAX_OBJ_NAME);
	mutex->obj_name[PJ_MAX_OBJ_NAME-1] = '\0';
    }

    PJ_LOG(6, (mutex->obj_name, "Mutex created"));
    return PJ_SUCCESS;
#else /* PJ_HAS_THREADS */
    return PJ_SUCCESS;
#endif
}

/*
 * pj_mutex_create()
 */
PJ_DEF(pj_status_t) pj_mutex_create(pj_pool_t *pool, 
				    const char *name, 
				    int type,
				    pj_mutex_t **ptr_mutex)
{
#if PJ_HAS_THREADS
    pj_status_t rc;
    pj_mutex_t *mutex;

    PJ_ASSERT_RETURN(pool && ptr_mutex, PJ_EINVAL);

    mutex = PJ_POOL_ALLOC_T(pool, pj_mutex_t);
    PJ_ASSERT_RETURN(mutex, PJ_ENOMEM);

    if ((rc=init_mutex(mutex, name, type)) != PJ_SUCCESS)
	return rc;
    
    *ptr_mutex = mutex;
    return PJ_SUCCESS;
#else /* PJ_HAS_THREADS */
    *ptr_mutex = (pj_mutex_t*)1;
    return PJ_SUCCESS;
#endif
}

/*
 * pj_mutex_create_simple()
 */
PJ_DEF(pj_status_t) pj_mutex_create_simple( pj_pool_t *pool, 
                                            const char *name,
					    pj_mutex_t **mutex )
{
    return pj_mutex_create(pool, name, PJ_MUTEX_SIMPLE, mutex);
}

/*
 * pj_mutex_create_recursive()
 */
PJ_DEF(pj_status_t) pj_mutex_create_recursive( pj_pool_t *pool,
					       const char *name,
					       pj_mutex_t **mutex )
{
    return pj_mutex_create(pool, name, PJ_MUTEX_RECURSE, mutex);
}

/*
 * pj_mutex_lock()
 */
PJ_DEF(pj_status_t) pj_mutex_lock(pj_mutex_t *mutex)
{
#if PJ_HAS_THREADS
    pj_status_t status;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(mutex, PJ_EINVAL);

#if PJ_DEBUG
    PJ_LOG(6,(mutex->obj_name, "Mutex: thread %s is waiting (mutex owner=%s)", 
				pj_thread_this()->obj_name,
				mutex->owner_name));
#else
    PJ_LOG(6,(mutex->obj_name, "Mutex: thread %s is waiting", 
				pj_thread_this()->obj_name));
#endif

    status = pthread_mutex_lock( &mutex->mutex );


#if PJ_DEBUG
    if (status == PJ_SUCCESS) {
	mutex->owner = pj_thread_this();
	pj_ansi_strcpy(mutex->owner_name, mutex->owner->obj_name);
	++mutex->nesting_level;
    }

    PJ_LOG(6,(mutex->obj_name, 
	      (status==0 ? 
		"Mutex acquired by thread %s (level=%d)" : 
		"Mutex acquisition FAILED by %s (level=%d)"),
	      pj_thread_this()->obj_name,
	      mutex->nesting_level));
#else
    PJ_LOG(6,(mutex->obj_name, 
	      (status==0 ? "Mutex acquired by thread %s" : "FAILED by %s"),
	      pj_thread_this()->obj_name));
#endif

    if (status == 0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(status);
#else	/* PJ_HAS_THREADS */
    pj_assert( mutex == (pj_mutex_t*)1 );
    return PJ_SUCCESS;
#endif
}

/*
 * pj_mutex_unlock()
 */
PJ_DEF(pj_status_t) pj_mutex_unlock(pj_mutex_t *mutex)
{
#if PJ_HAS_THREADS
    pj_status_t status;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(mutex, PJ_EINVAL);

#if PJ_DEBUG
    pj_assert(mutex->owner == pj_thread_this());
    if (--mutex->nesting_level == 0) {
	mutex->owner = NULL;
	mutex->owner_name[0] = '\0';
    }

    PJ_LOG(6,(mutex->obj_name, "Mutex released by thread %s (level=%d)", 
				pj_thread_this()->obj_name, 
				mutex->nesting_level));
#else
    PJ_LOG(6,(mutex->obj_name, "Mutex released by thread %s", 
				pj_thread_this()->obj_name));
#endif

    status = pthread_mutex_unlock( &mutex->mutex );
    if (status == 0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(status);

#else /* PJ_HAS_THREADS */
    pj_assert( mutex == (pj_mutex_t*)1 );
    return PJ_SUCCESS;
#endif
}

/*
 * pj_mutex_trylock()
 */
PJ_DEF(pj_status_t) pj_mutex_trylock(pj_mutex_t *mutex)
{
#if PJ_HAS_THREADS
    int status;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(mutex, PJ_EINVAL);

    PJ_LOG(6,(mutex->obj_name, "Mutex: thread %s is trying", 
				pj_thread_this()->obj_name));

    status = pthread_mutex_trylock( &mutex->mutex );

    if (status==0) {
#if PJ_DEBUG
	mutex->owner = pj_thread_this();
	pj_ansi_strcpy(mutex->owner_name, mutex->owner->obj_name);
	++mutex->nesting_level;

	PJ_LOG(6,(mutex->obj_name, "Mutex acquired by thread %s (level=%d)", 
				   pj_thread_this()->obj_name,
				   mutex->nesting_level));
#else
	PJ_LOG(6,(mutex->obj_name, "Mutex acquired by thread %s", 
				  pj_thread_this()->obj_name));
#endif
    } else {
	PJ_LOG(6,(mutex->obj_name, "Mutex: thread %s's trylock() failed", 
				    pj_thread_this()->obj_name));
    }
    
    if (status==0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(status);
#else	/* PJ_HAS_THREADS */
    pj_assert( mutex == (pj_mutex_t*)1);
    return PJ_SUCCESS;
#endif
}

/*
 * pj_mutex_destroy()
 */
PJ_DEF(pj_status_t) pj_mutex_destroy(pj_mutex_t *mutex)
{
    enum { RETRY = 4 };
    int status = 0;
    unsigned retry;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(mutex, PJ_EINVAL);

#if PJ_HAS_THREADS
    PJ_LOG(6,(mutex->obj_name, "Mutex destroyed by thread %s",
			       pj_thread_this()->obj_name));

    for (retry=0; retry<RETRY; ++retry) {
	status = pthread_mutex_destroy( &mutex->mutex );
	if (status == PJ_SUCCESS)
	    break;
	else if (retry<RETRY-1 && status == EBUSY)
	    pthread_mutex_unlock(&mutex->mutex);
    }

    if (status == 0)
	return PJ_SUCCESS;
    else {
	return PJ_RETURN_OS_ERROR(status);
    }
#else
    pj_assert( mutex == (pj_mutex_t*)1 );
    status = PJ_SUCCESS;
    return status;
#endif
}

#if PJ_DEBUG
PJ_DEF(pj_bool_t) pj_mutex_is_locked(pj_mutex_t *mutex)
{
#if PJ_HAS_THREADS
    return mutex->owner == pj_thread_this();
#else
    return 1;
#endif
}
#endif

///////////////////////////////////////////////////////////////////////////////
/*
 * Include Read/Write mutex emulation for POSIX platforms that lack it (e.g.
 * RTEMS). Otherwise use POSIX rwlock.
 */
#if defined(PJ_EMULATE_RWMUTEX) && PJ_EMULATE_RWMUTEX!=0
    /* We need semaphore functionality to emulate rwmutex */
#   if !defined(PJ_HAS_SEMAPHORE) || PJ_HAS_SEMAPHORE==0
#	error "Semaphore support needs to be enabled to emulate rwmutex"
#   endif
#   include "os_rwmutex.c"
#else
struct pj_rwmutex_t
{
    pthread_rwlock_t rwlock;
};

PJ_DEF(pj_status_t) pj_rwmutex_create(pj_pool_t *pool, const char *name,
				      pj_rwmutex_t **p_mutex)
{
    pj_rwmutex_t *rwm;
    pj_status_t status;

    PJ_UNUSED_ARG(name);
    
    rwm = PJ_POOL_ALLOC_T(pool, pj_rwmutex_t);
    PJ_ASSERT_RETURN(rwm, PJ_ENOMEM);

    status = pthread_rwlock_init(&rwm->rwlock, NULL);
    if (status != 0)
	return PJ_RETURN_OS_ERROR(status);

    *p_mutex = rwm;
    return PJ_SUCCESS;
}

/*
 * Lock the mutex for reading.
 *
 */
PJ_DEF(pj_status_t) pj_rwmutex_lock_read(pj_rwmutex_t *mutex)
{
    pj_status_t status;

    status = pthread_rwlock_rdlock(&mutex->rwlock);
    if (status != 0)
	return PJ_RETURN_OS_ERROR(status);

    return PJ_SUCCESS;
}

/*
 * Lock the mutex for writing.
 *
 */
PJ_DEF(pj_status_t) pj_rwmutex_lock_write(pj_rwmutex_t *mutex)
{
    pj_status_t status;

    status = pthread_rwlock_wrlock(&mutex->rwlock);
    if (status != 0)
	return PJ_RETURN_OS_ERROR(status);

    return PJ_SUCCESS;
}

/*
 * Release read lock.
 *
 */
PJ_DEF(pj_status_t) pj_rwmutex_unlock_read(pj_rwmutex_t *mutex)
{
    return pj_rwmutex_unlock_write(mutex);
}

/*
 * Release write lock.
 *
 */
PJ_DEF(pj_status_t) pj_rwmutex_unlock_write(pj_rwmutex_t *mutex)
{
    pj_status_t status;

    status = pthread_rwlock_unlock(&mutex->rwlock);
    if (status != 0)
	return PJ_RETURN_OS_ERROR(status);

    return PJ_SUCCESS;
}

/*
 * Destroy reader/writer mutex.
 *
 */
PJ_DEF(pj_status_t) pj_rwmutex_destroy(pj_rwmutex_t *mutex)
{
    pj_status_t status;

    status = pthread_rwlock_destroy(&mutex->rwlock);
    if (status != 0)
	return PJ_RETURN_OS_ERROR(status);

    return PJ_SUCCESS;
}

#endif	/* PJ_EMULATE_RWMUTEX */


///////////////////////////////////////////////////////////////////////////////
#if defined(PJ_HAS_SEMAPHORE) && PJ_HAS_SEMAPHORE != 0

/*
 * pj_sem_create()
 */
PJ_DEF(pj_status_t) pj_sem_create( pj_pool_t *pool, 
				   const char *name,
				   unsigned initial, 
				   unsigned max,
				   pj_sem_t **ptr_sem)
{
#if PJ_HAS_THREADS
    pj_sem_t *sem;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(pool != NULL && ptr_sem != NULL, PJ_EINVAL);

    sem = PJ_POOL_ALLOC_T(pool, pj_sem_t);
    PJ_ASSERT_RETURN(sem, PJ_ENOMEM);

#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
    /* MacOS X doesn't support anonymous semaphore */
    {
	char sem_name[PJ_GUID_MAX_LENGTH+1];
	pj_str_t nam;

	/* We should use SEM_NAME_LEN, but this doesn't seem to be 
	 * declared anywhere? The value here is just from trial and error
	 * to get the longest name supported.
	 */
#	define MAX_SEM_NAME_LEN	23

	/* Create a unique name for the semaphore. */
	if (PJ_GUID_STRING_LENGTH <= MAX_SEM_NAME_LEN) {
	    nam.ptr = sem_name;
	    pj_generate_unique_string(&nam);
	    sem_name[nam.slen] = '\0';
	} else {
	    pj_create_random_string(sem_name, MAX_SEM_NAME_LEN);
	    sem_name[MAX_SEM_NAME_LEN] = '\0';
	}

	/* Create semaphore */
	sem->sem = sem_open(sem_name, O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, 
			    initial);
	if (sem->sem == SEM_FAILED)
	    return PJ_RETURN_OS_ERROR(pj_get_native_os_error());

	/* And immediately release the name as we don't need it */
	sem_unlink(sem_name);
    }
#else
    sem->sem = PJ_POOL_ALLOC_T(pool, sem_t);
    if (sem_init( sem->sem, 0, initial) != 0) 
	return PJ_RETURN_OS_ERROR(pj_get_native_os_error());
#endif
    
    /* Set name. */
    if (!name) {
	name = "sem%p";
    }
    if (strchr(name, '%')) {
	pj_ansi_snprintf(sem->obj_name, PJ_MAX_OBJ_NAME, name, sem);
    } else {
	strncpy(sem->obj_name, name, PJ_MAX_OBJ_NAME);
	sem->obj_name[PJ_MAX_OBJ_NAME-1] = '\0';
    }

    PJ_LOG(6, (sem->obj_name, "Semaphore created"));

    *ptr_sem = sem;
    return PJ_SUCCESS;
#else
    *ptr_sem = (pj_sem_t*)1;
    return PJ_SUCCESS;
#endif
}

/*
 * pj_sem_wait()
 */
PJ_DEF(pj_status_t) pj_sem_wait(pj_sem_t *sem)
{
#if PJ_HAS_THREADS
    int result;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(sem, PJ_EINVAL);

    PJ_LOG(6, (sem->obj_name, "Semaphore: thread %s is waiting", 
			      pj_thread_this()->obj_name));

    result = sem_wait( sem->sem );
    
    if (result == 0) {
	PJ_LOG(6, (sem->obj_name, "Semaphore acquired by thread %s", 
				  pj_thread_this()->obj_name));
    } else {
	PJ_LOG(6, (sem->obj_name, "Semaphore: thread %s FAILED to acquire", 
				  pj_thread_this()->obj_name));
    }

    if (result == 0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(pj_get_native_os_error());
#else
    pj_assert( sem == (pj_sem_t*) 1 );
    return PJ_SUCCESS;
#endif
}

/*
 * pj_sem_trywait()
 */
PJ_DEF(pj_status_t) pj_sem_trywait(pj_sem_t *sem)
{
#if PJ_HAS_THREADS
    int result;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(sem, PJ_EINVAL);

    result = sem_trywait( sem->sem );
    
    if (result == 0) {
	PJ_LOG(6, (sem->obj_name, "Semaphore acquired by thread %s", 
				  pj_thread_this()->obj_name));
    } 
    if (result == 0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(pj_get_native_os_error());
#else
    pj_assert( sem == (pj_sem_t*)1 );
    return PJ_SUCCESS;
#endif
}

/*
 * pj_sem_post()
 */
PJ_DEF(pj_status_t) pj_sem_post(pj_sem_t *sem)
{
#if PJ_HAS_THREADS
    int result;
    PJ_LOG(6, (sem->obj_name, "Semaphore released by thread %s",
			      pj_thread_this()->obj_name));
    result = sem_post( sem->sem );

    if (result == 0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(pj_get_native_os_error());
#else
    pj_assert( sem == (pj_sem_t*) 1);
    return PJ_SUCCESS;
#endif
}

/*
 * pj_sem_destroy()
 */
PJ_DEF(pj_status_t) pj_sem_destroy(pj_sem_t *sem)
{
#if PJ_HAS_THREADS
    int result;

    PJ_CHECK_STACK();
    PJ_ASSERT_RETURN(sem, PJ_EINVAL);

    PJ_LOG(6, (sem->obj_name, "Semaphore destroyed by thread %s",
			      pj_thread_this()->obj_name));
#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
    result = sem_close( sem->sem );
#else
    result = sem_destroy( sem->sem );
#endif

    if (result == 0)
	return PJ_SUCCESS;
    else
	return PJ_RETURN_OS_ERROR(pj_get_native_os_error());
#else
    pj_assert( sem == (pj_sem_t*) 1 );
    return PJ_SUCCESS;
#endif
}

#endif	/* PJ_HAS_SEMAPHORE */

///////////////////////////////////////////////////////////////////////////////
#if defined(PJ_HAS_EVENT_OBJ) && PJ_HAS_EVENT_OBJ != 0

/*
 * pj_event_create()
 */
PJ_DEF(pj_status_t) pj_event_create(pj_pool_t *pool, const char *name,
				    pj_bool_t manual_reset, pj_bool_t initial,
				    pj_event_t **ptr_event)
{
    pj_assert(!"Not supported!");
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(name);
    PJ_UNUSED_ARG(manual_reset);
    PJ_UNUSED_ARG(initial);
    PJ_UNUSED_ARG(ptr_event);
    return PJ_EINVALIDOP;
}

/*
 * pj_event_wait()
 */
PJ_DEF(pj_status_t) pj_event_wait(pj_event_t *event)
{
    PJ_UNUSED_ARG(event);
    return PJ_EINVALIDOP;
}

/*
 * pj_event_trywait()
 */
PJ_DEF(pj_status_t) pj_event_trywait(pj_event_t *event)
{
    PJ_UNUSED_ARG(event);
    return PJ_EINVALIDOP;
}

/*
 * pj_event_set()
 */
PJ_DEF(pj_status_t) pj_event_set(pj_event_t *event)
{
    PJ_UNUSED_ARG(event);
    return PJ_EINVALIDOP;
}

/*
 * pj_event_pulse()
 */
PJ_DEF(pj_status_t) pj_event_pulse(pj_event_t *event)
{
    PJ_UNUSED_ARG(event);
    return PJ_EINVALIDOP;
}

/*
 * pj_event_reset()
 */
PJ_DEF(pj_status_t) pj_event_reset(pj_event_t *event)
{
    PJ_UNUSED_ARG(event);
    return PJ_EINVALIDOP;
}

/*
 * pj_event_destroy()
 */
PJ_DEF(pj_status_t) pj_event_destroy(pj_event_t *event)
{
    PJ_UNUSED_ARG(event);
    return PJ_EINVALIDOP;
}

#endif	/* PJ_HAS_EVENT_OBJ */

///////////////////////////////////////////////////////////////////////////////
#if defined(PJ_TERM_HAS_COLOR) && PJ_TERM_HAS_COLOR != 0
/*
 * Terminal
 */

/**
 * Set terminal color.
 */
PJ_DEF(pj_status_t) pj_term_set_color(pj_color_t color)
{
    /* put bright prefix to ansi_color */
    char ansi_color[12] = "\033[01;3";

    if (color & PJ_TERM_COLOR_BRIGHT) {
	color ^= PJ_TERM_COLOR_BRIGHT;
    } else {
	strcpy(ansi_color, "\033[00;3");
    }

    switch (color) {
    case 0:
	/* black color */
	strcat(ansi_color, "0m");
	break;
    case PJ_TERM_COLOR_R:
	/* red color */
	strcat(ansi_color, "1m");
	break;
    case PJ_TERM_COLOR_G:
	/* green color */
	strcat(ansi_color, "2m");
	break;
    case PJ_TERM_COLOR_B:
	/* blue color */
	strcat(ansi_color, "4m");
	break;
    case PJ_TERM_COLOR_R | PJ_TERM_COLOR_G:
	/* yellow color */
	strcat(ansi_color, "3m");
	break;
    case PJ_TERM_COLOR_R | PJ_TERM_COLOR_B:
	/* magenta color */
	strcat(ansi_color, "5m");
	break;
    case PJ_TERM_COLOR_G | PJ_TERM_COLOR_B:
	/* cyan color */
	strcat(ansi_color, "6m");
	break;
    case PJ_TERM_COLOR_R | PJ_TERM_COLOR_G | PJ_TERM_COLOR_B:
	/* white color */
	strcat(ansi_color, "7m");
	break;
    default:
	/* default console color */
	strcpy(ansi_color, "\033[00m");
	break;
    }

    fputs(ansi_color, stdout);

    return PJ_SUCCESS;
}

/**
 * Get current terminal foreground color.
 */
PJ_DEF(pj_color_t) pj_term_get_color(void)
{
    return 0;
}

#endif	/* PJ_TERM_HAS_COLOR */

#if !defined(PJ_DARWINOS) || PJ_DARWINOS == 0
/*
 * pj_run_app()
 */
PJ_DEF(int) pj_run_app(pj_main_func_ptr main_func, int argc, char *argv[],
                       unsigned flags)
{
    return (*main_func)(argc, argv);
}
#endif
