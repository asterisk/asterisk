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
#include <pjmedia/clock.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/compat/high_precision.h>

/* API: Init clock source */
PJ_DEF(pj_status_t) pjmedia_clock_src_init( pjmedia_clock_src *clocksrc,
                                            pjmedia_type media_type,
                                            unsigned clock_rate,
                                            unsigned ptime_usec )
{
    PJ_ASSERT_RETURN(clocksrc, PJ_EINVAL);

    clocksrc->media_type = media_type;
    clocksrc->clock_rate = clock_rate;
    clocksrc->ptime_usec = ptime_usec;
    pj_set_timestamp32(&clocksrc->timestamp, 0, 0);
    pj_get_timestamp(&clocksrc->last_update);

    return PJ_SUCCESS;
}

/* API: Update clock source */
PJ_DECL(pj_status_t) pjmedia_clock_src_update( pjmedia_clock_src *clocksrc,
                                               const pj_timestamp *timestamp )
{
    PJ_ASSERT_RETURN(clocksrc, PJ_EINVAL);

    if (timestamp)
        pj_memcpy(&clocksrc->timestamp, timestamp, sizeof(pj_timestamp));
    pj_get_timestamp(&clocksrc->last_update);

    return PJ_SUCCESS;
}

/* API: Get clock source's current timestamp */
PJ_DEF(pj_status_t)
pjmedia_clock_src_get_current_timestamp( const pjmedia_clock_src *clocksrc,
                                         pj_timestamp *timestamp)
{
    pj_timestamp now;
    unsigned elapsed_ms;
    
    PJ_ASSERT_RETURN(clocksrc && timestamp, PJ_EINVAL);

    pj_get_timestamp(&now);
    elapsed_ms = pj_elapsed_msec(&clocksrc->last_update, &now);
    pj_memcpy(timestamp, &clocksrc->timestamp, sizeof(pj_timestamp));
    pj_add_timestamp32(timestamp, elapsed_ms * clocksrc->clock_rate / 1000);

    return PJ_SUCCESS;
}

/* API: Get clock source's time (in ms) */
PJ_DEF(pj_uint32_t)
pjmedia_clock_src_get_time_msec( const pjmedia_clock_src *clocksrc )
{
    pj_timestamp ts;

    pjmedia_clock_src_get_current_timestamp(clocksrc, &ts);

#if PJ_HAS_INT64
    if (ts.u64 > PJ_UINT64(0x3FFFFFFFFFFFFF))
        return (pj_uint32_t)(ts.u64 / clocksrc->clock_rate * 1000);
    else
        return (pj_uint32_t)(ts.u64 * 1000 / clocksrc->clock_rate);
#elif PJ_HAS_FLOATING_POINT
    return (pj_uint32_t)((1.0 * ts.u32.hi * 0xFFFFFFFFUL + ts.u32.lo)
                         * 1000.0 / clocksrc->clock_rate);
#else
    if (ts.u32.lo > 0x3FFFFFUL)
        return (pj_uint32_t)(0xFFFFFFFFUL / clocksrc->clock_rate * ts.u32.hi 
                             * 1000UL + ts.u32.lo / clocksrc->clock_rate *
                             1000UL);
    else
        return (pj_uint32_t)(0xFFFFFFFFUL / clocksrc->clock_rate * ts.u32.hi 
                             * 1000UL + ts.u32.lo * 1000UL /
                             clocksrc->clock_rate);
#endif
}


/*
 * Implementation of media clock with OS thread.
 */

struct pjmedia_clock
{
    pj_pool_t		    *pool;
    pj_timestamp	     freq;
    pj_timestamp	     interval;
    pj_timestamp	     next_tick;
    pj_timestamp	     timestamp;
    unsigned		     timestamp_inc;
    unsigned		     options;
    pj_uint64_t		     max_jump;
    pjmedia_clock_callback  *cb;
    void		    *user_data;
    pj_thread_t		    *thread;
    pj_bool_t		     running;
    pj_bool_t		     quitting;
    pj_lock_t		    *lock;
};


static int clock_thread(void *arg);

#define MAX_JUMP_MSEC	500
#define USEC_IN_SEC	(pj_uint64_t)1000000

/*
 * Create media clock.
 */
PJ_DEF(pj_status_t) pjmedia_clock_create( pj_pool_t *pool,
					  unsigned clock_rate,
					  unsigned channel_count,
					  unsigned samples_per_frame,
					  unsigned options,
					  pjmedia_clock_callback *cb,
					  void *user_data,
					  pjmedia_clock **p_clock)
{
    pjmedia_clock_param param;

    param.usec_interval = (unsigned)(samples_per_frame * USEC_IN_SEC /
			             channel_count / clock_rate);
    param.clock_rate = clock_rate;
    return pjmedia_clock_create2(pool, &param, options, cb,
                                 user_data, p_clock);
}

PJ_DEF(pj_status_t) pjmedia_clock_create2(pj_pool_t *pool,
                                          const pjmedia_clock_param *param,
				          unsigned options,
				          pjmedia_clock_callback *cb,
				          void *user_data,
				          pjmedia_clock **p_clock)
{
    pjmedia_clock *clock;
    pj_status_t status;

    PJ_ASSERT_RETURN(pool && param->usec_interval && param->clock_rate &&
                     p_clock, PJ_EINVAL);

    pool = pj_pool_create(pool->factory, "clock%p", 512, 512, NULL);

    clock = PJ_POOL_ALLOC_T(pool, pjmedia_clock);
    clock->pool = pool;
    
    status = pj_get_timestamp_freq(&clock->freq);
    if (status != PJ_SUCCESS)
	return status;

    clock->interval.u64 = param->usec_interval * clock->freq.u64 /
                          USEC_IN_SEC;
    clock->next_tick.u64 = 0;
    clock->timestamp.u64 = 0;
    clock->max_jump = MAX_JUMP_MSEC * clock->freq.u64 / 1000;
    clock->timestamp_inc = (unsigned)(param->usec_interval *
                                      param->clock_rate /
				      USEC_IN_SEC);
    clock->options = options;
    clock->cb = cb;
    clock->user_data = user_data;
    clock->thread = NULL;
    clock->running = PJ_FALSE;
    clock->quitting = PJ_FALSE;
    
    /* I don't think we need a mutex, so we'll use null. */
    status = pj_lock_create_null_mutex(pool, "clock", &clock->lock);
    if (status != PJ_SUCCESS)
	return status;

    *p_clock = clock;

    return PJ_SUCCESS;
}


/*
 * Start the clock. 
 */
PJ_DEF(pj_status_t) pjmedia_clock_start(pjmedia_clock *clock)
{
    pj_timestamp now;
    pj_status_t status;

    PJ_ASSERT_RETURN(clock != NULL, PJ_EINVAL);

    if (clock->running)
	return PJ_SUCCESS;

    status = pj_get_timestamp(&now);
    if (status != PJ_SUCCESS)
	return status;

    clock->next_tick.u64 = now.u64 + clock->interval.u64;
    clock->running = PJ_TRUE;
    clock->quitting = PJ_FALSE;

    if ((clock->options & PJMEDIA_CLOCK_NO_ASYNC) == 0 && !clock->thread) {
	status = pj_thread_create(clock->pool, "clock", &clock_thread, clock,
				  0, 0, &clock->thread);
	if (status != PJ_SUCCESS) {
	    pj_lock_destroy(clock->lock);
	    return status;
	}
    }

    return PJ_SUCCESS;
}


/*
 * Stop the clock. 
 */
PJ_DEF(pj_status_t) pjmedia_clock_stop(pjmedia_clock *clock)
{
    PJ_ASSERT_RETURN(clock != NULL, PJ_EINVAL);

    clock->running = PJ_FALSE;
    clock->quitting = PJ_TRUE;

    if (clock->thread) {
	if (pj_thread_join(clock->thread) == PJ_SUCCESS) {
	    clock->thread = NULL;
	} else {
	    clock->quitting = PJ_FALSE;
	}
    }

    return PJ_SUCCESS;
}


/*
 * Update the clock. 
 */
PJ_DEF(pj_status_t) pjmedia_clock_modify(pjmedia_clock *clock,
                                         const pjmedia_clock_param *param)
{
    clock->interval.u64 = param->usec_interval * clock->freq.u64 /
                          USEC_IN_SEC;
    clock->timestamp_inc = (unsigned)(param->usec_interval *
                                      param->clock_rate /
				      USEC_IN_SEC);

    return PJ_SUCCESS;
}


/* Calculate next tick */
PJ_INLINE(void) clock_calc_next_tick(pjmedia_clock *clock,
				     pj_timestamp *now)
{
    if (clock->next_tick.u64+clock->max_jump < now->u64) {
	/* Timestamp has made large jump, adjust next_tick */
	clock->next_tick.u64 = now->u64;
    }
    clock->next_tick.u64 += clock->interval.u64;

}

/*
 * Poll the clock. 
 */
PJ_DEF(pj_bool_t) pjmedia_clock_wait( pjmedia_clock *clock,
				      pj_bool_t wait,
				      pj_timestamp *ts)
{
    pj_timestamp now;
    pj_status_t status;

    PJ_ASSERT_RETURN(clock != NULL, PJ_FALSE);
    PJ_ASSERT_RETURN((clock->options & PJMEDIA_CLOCK_NO_ASYNC) != 0,
		     PJ_FALSE);
    PJ_ASSERT_RETURN(clock->running, PJ_FALSE);

    status = pj_get_timestamp(&now);
    if (status != PJ_SUCCESS)
	return PJ_FALSE;

    /* Wait for the next tick to happen */
    if (now.u64 < clock->next_tick.u64) {
	unsigned msec;

	if (!wait)
	    return PJ_FALSE;

	msec = pj_elapsed_msec(&now, &clock->next_tick);
	pj_thread_sleep(msec);
    }

    /* Call callback, if any */
    if (clock->cb)
	(*clock->cb)(&clock->timestamp, clock->user_data);

    /* Report timestamp to caller */
    if (ts)
	ts->u64 = clock->timestamp.u64;

    /* Increment timestamp */
    clock->timestamp.u64 += clock->timestamp_inc;

    /* Calculate next tick */
    clock_calc_next_tick(clock, &now);

    /* Done */
    return PJ_TRUE;
}


/*
 * Clock thread
 */
static int clock_thread(void *arg)
{
    pj_timestamp now;
    pjmedia_clock *clock = (pjmedia_clock*) arg;

    /* Set thread priority to maximum unless not wanted. */
    if ((clock->options & PJMEDIA_CLOCK_NO_HIGHEST_PRIO) == 0) {
	int max = pj_thread_get_prio_max(pj_thread_this());
	if (max > 0)
	    pj_thread_set_prio(pj_thread_this(), max);
    }

    /* Get the first tick */
    pj_get_timestamp(&clock->next_tick);
    clock->next_tick.u64 += clock->interval.u64;


    while (!clock->quitting) {

	pj_get_timestamp(&now);

	/* Wait for the next tick to happen */
	if (now.u64 < clock->next_tick.u64) {
	    unsigned msec;
	    msec = pj_elapsed_msec(&now, &clock->next_tick);
	    pj_thread_sleep(msec);
	}

	/* Skip if not running */
	if (!clock->running) {
	    /* Calculate next tick */
	    clock_calc_next_tick(clock, &now);
	    continue;
	}

	pj_lock_acquire(clock->lock);

	/* Call callback, if any */
	if (clock->cb)
	    (*clock->cb)(&clock->timestamp, clock->user_data);

	/* Best effort way to detect if we've been destroyed in the callback */
	if (clock->quitting)
	    break;

	/* Increment timestamp */
	clock->timestamp.u64 += clock->timestamp_inc;

	/* Calculate next tick */
	clock_calc_next_tick(clock, &now);

	pj_lock_release(clock->lock);
    }

    return 0;
}


/*
 * Destroy the clock. 
 */
PJ_DEF(pj_status_t) pjmedia_clock_destroy(pjmedia_clock *clock)
{
    PJ_ASSERT_RETURN(clock != NULL, PJ_EINVAL);

    clock->running = PJ_FALSE;
    clock->quitting = PJ_TRUE;

    if (clock->thread) {
	pj_thread_join(clock->thread);
	pj_thread_destroy(clock->thread);
	clock->thread = NULL;
    }

    if (clock->lock) {
	pj_lock_destroy(clock->lock);
	clock->lock = NULL;
    }

    if (clock->pool) {
	pj_pool_t *pool = clock->pool;
	clock->pool = NULL;
	pj_pool_release(pool);
    }
    return PJ_SUCCESS;
}


