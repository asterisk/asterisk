/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia/event.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/list.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/string.h>

#define THIS_FILE	"event.c"

#define MAX_EVENTS 16

typedef struct esub esub;

struct esub
{
    PJ_DECL_LIST_MEMBER(esub);

    pjmedia_event_cb    *cb;
    void                *user_data;
    void                *epub;
};

typedef struct event_queue
{
    pjmedia_event   events[MAX_EVENTS]; /**< array of events.           */
    int             head, tail;
    pj_bool_t       is_full;
} event_queue;

struct pjmedia_event_mgr
{
    pj_pool_t      *pool;
    pj_thread_t    *thread;             /**< worker thread.             */
    pj_bool_t       is_quitting;
    pj_sem_t       *sem;
    pj_mutex_t     *mutex;
    event_queue     ev_queue;
    event_queue    *pub_ev_queue;       /**< publish() event queue.     */
    esub            esub_list;          /**< list of subscribers.       */
    esub            free_esub_list;     /**< list of subscribers.       */
    esub           *th_next_sub,        /**< worker thread's next sub.  */
                   *pub_next_sub;       /**< publish() next sub.        */
};

static pjmedia_event_mgr *event_manager_instance;

static pj_status_t event_queue_add_event(event_queue* ev_queue,
                                         pjmedia_event *event)
{
    if (ev_queue->is_full) {
        char ev_name[5];

        /* This event will be ignored. */
        PJ_LOG(4, (THIS_FILE, "Lost event %s from publisher [0x%p] "
                              "due to full queue.",
                              pjmedia_fourcc_name(event->type, ev_name),
                              event->epub));

        return PJ_ETOOMANY;
    }

    pj_memcpy(&ev_queue->events[ev_queue->tail], event, sizeof(*event));
    ev_queue->tail = (ev_queue->tail + 1) % MAX_EVENTS;
    if (ev_queue->tail == ev_queue->head)
        ev_queue->is_full = PJ_TRUE;

    return PJ_SUCCESS;
}

static pj_status_t event_mgr_distribute_events(pjmedia_event_mgr *mgr,
                                               event_queue *ev_queue,
                                               esub **next_sub,
                                               pj_bool_t rls_lock)
{
    pj_status_t err = PJ_SUCCESS;
    esub * sub = mgr->esub_list.next;
    pjmedia_event *ev = &ev_queue->events[ev_queue->head];

    while (sub != &mgr->esub_list) {
        *next_sub = sub->next;

        /* Check if the subscriber is interested in 
         * receiving the event from the publisher.
         */
        if (sub->epub == ev->epub || !sub->epub) {
            pjmedia_event_cb *cb = sub->cb;
            void *user_data = sub->user_data;
            pj_status_t status;
            
            if (rls_lock)
                pj_mutex_unlock(mgr->mutex);

            status = (*cb)(ev, user_data);
            if (status != PJ_SUCCESS && err == PJ_SUCCESS)
	        err = status;

            if (rls_lock)
                pj_mutex_lock(mgr->mutex);
        }
	sub = *next_sub;
    }
    *next_sub = NULL;

    ev_queue->head = (ev_queue->head + 1) % MAX_EVENTS;
    ev_queue->is_full = PJ_FALSE;

    return err;
}

/* Event worker thread function. */
static int event_worker_thread(void *arg)
{
    pjmedia_event_mgr *mgr = (pjmedia_event_mgr *)arg;

    while (1) {
	/* Wait until there is an event. */
        pj_sem_wait(mgr->sem);

        if (mgr->is_quitting)
            break;

        pj_mutex_lock(mgr->mutex);
        event_mgr_distribute_events(mgr, &mgr->ev_queue,
                                    &mgr->th_next_sub, PJ_TRUE);
        pj_mutex_unlock(mgr->mutex);
    }

    return 0;
}

PJ_DEF(pj_status_t) pjmedia_event_mgr_create(pj_pool_t *pool,
                                             unsigned options,
				             pjmedia_event_mgr **p_mgr)
{
    pjmedia_event_mgr *mgr;
    pj_status_t status;

    mgr = PJ_POOL_ZALLOC_T(pool, pjmedia_event_mgr);
    mgr->pool = pj_pool_create(pool->factory, "evt mgr", 500, 500, NULL);
    pj_list_init(&mgr->esub_list);
    pj_list_init(&mgr->free_esub_list);

    if (!(options & PJMEDIA_EVENT_MGR_NO_THREAD)) {
        status = pj_sem_create(mgr->pool, "ev_sem", 0, MAX_EVENTS + 1,
                               &mgr->sem);
        if (status != PJ_SUCCESS)
            return status;

        status = pj_thread_create(mgr->pool, "ev_thread",
                                  &event_worker_thread,
                                  mgr, 0, 0, &mgr->thread);
        if (status != PJ_SUCCESS) {
            pjmedia_event_mgr_destroy(mgr);
            return status;
        }
    }

    status = pj_mutex_create_recursive(mgr->pool, "ev_mutex", &mgr->mutex);
    if (status != PJ_SUCCESS) {
        pjmedia_event_mgr_destroy(mgr);
        return status;
    }

    if (!event_manager_instance)
	event_manager_instance = mgr;

    if (p_mgr)
	*p_mgr = mgr;

    return PJ_SUCCESS;
}

PJ_DEF(pjmedia_event_mgr*) pjmedia_event_mgr_instance(void)
{
    return event_manager_instance;
}

PJ_DEF(void) pjmedia_event_mgr_set_instance(pjmedia_event_mgr *mgr)
{
    event_manager_instance = mgr;
}

PJ_DEF(void) pjmedia_event_mgr_destroy(pjmedia_event_mgr *mgr)
{
    if (!mgr) mgr = pjmedia_event_mgr_instance();
    PJ_ASSERT_ON_FAIL(mgr != NULL, return);

    if (mgr->thread) {
        mgr->is_quitting = PJ_TRUE;
        pj_sem_post(mgr->sem);
        pj_thread_join(mgr->thread);
    }

    if (mgr->sem) {
        pj_sem_destroy(mgr->sem);
        mgr->sem = NULL;
    }

    if (mgr->mutex) {
        pj_mutex_destroy(mgr->mutex);
        mgr->mutex = NULL;
    }

    if (mgr->pool)
        pj_pool_release(mgr->pool);

    if (event_manager_instance == mgr)
	event_manager_instance = NULL;
}

PJ_DEF(void) pjmedia_event_init( pjmedia_event *event,
                                 pjmedia_event_type type,
                                 const pj_timestamp *ts,
                                 const void *src)
{
    pj_bzero(event, sizeof(*event));
    event->type = type;
    if (ts)
	event->timestamp.u64 = ts->u64;
    event->epub = event->src = src;
}

PJ_DEF(pj_status_t) pjmedia_event_subscribe( pjmedia_event_mgr *mgr,
                                             pjmedia_event_cb *cb,
                                             void *user_data,
                                             void *epub)
{
    esub *sub;

    PJ_ASSERT_RETURN(cb, PJ_EINVAL);

    if (!mgr) mgr = pjmedia_event_mgr_instance();
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);
    /* Check whether callback function with the same user data is already
     * subscribed to the publisher. This is to prevent the callback function
     * receiving the same event from the same publisher more than once.
     */
    sub = mgr->esub_list.next;
    while (sub != &mgr->esub_list) {
	esub *next = sub->next;
        if (sub->cb == cb && sub->user_data == user_data &&
            sub->epub == epub)
        {
            pj_mutex_unlock(mgr->mutex);
            return PJ_SUCCESS;
        }
	sub = next;
    }

    if (mgr->free_esub_list.next != &mgr->free_esub_list) {
        sub = mgr->free_esub_list.next;
        pj_list_erase(sub);
    } else
        sub = PJ_POOL_ZALLOC_T(mgr->pool, esub);
    sub->cb = cb;
    sub->user_data = user_data;
    sub->epub = epub;
    pj_list_push_back(&mgr->esub_list, sub);
    pj_mutex_unlock(mgr->mutex);

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t)
pjmedia_event_unsubscribe(pjmedia_event_mgr *mgr,
                          pjmedia_event_cb *cb,
                          void *user_data,
                          void *epub)
{
    esub *sub;

    PJ_ASSERT_RETURN(cb, PJ_EINVAL);

    if (!mgr) mgr = pjmedia_event_mgr_instance();
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);
    sub = mgr->esub_list.next;
    while (sub != &mgr->esub_list) {
	esub *next = sub->next;
        if (sub->cb == cb && (sub->user_data == user_data || !user_data) &&
            (sub->epub == epub || !epub))
        {
            /* If the worker thread or pjmedia_event_publish() API is
             * in the process of distributing events, make sure that
             * its pointer to the next subscriber stays valid.
             */
            if (mgr->th_next_sub == sub)
                mgr->th_next_sub = sub->next;
            if (mgr->pub_next_sub == sub)
                mgr->pub_next_sub = sub->next;
            pj_list_erase(sub);
            pj_list_push_back(&mgr->free_esub_list, sub);
            if (user_data && epub)
                break;
        }
	sub = next;
    }
    pj_mutex_unlock(mgr->mutex);

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_event_publish( pjmedia_event_mgr *mgr,
                                           void *epub,
                                           pjmedia_event *event,
                                           pjmedia_event_publish_flag flag)
{
    pj_status_t err = PJ_SUCCESS;

    PJ_ASSERT_RETURN(epub && event, PJ_EINVAL);

    if (!mgr) mgr = pjmedia_event_mgr_instance();
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    event->epub = epub;

    pj_mutex_lock(mgr->mutex);
    if (flag & PJMEDIA_EVENT_PUBLISH_POST_EVENT) {
        if (event_queue_add_event(&mgr->ev_queue, event) == PJ_SUCCESS)
            pj_sem_post(mgr->sem);
    } else {
        /* For nested pjmedia_event_publish() calls, i.e. calling publish()
         * inside the subscriber's callback, the function will only add
         * the event to the event queue of the first publish() call. It
         * is the first publish() call that will be responsible to
         * distribute the events.
         */
        if (mgr->pub_ev_queue) {
            event_queue_add_event(mgr->pub_ev_queue, event);
        } else {
            static event_queue ev_queue;
            pj_status_t status;

            ev_queue.head = ev_queue.tail = 0;
            ev_queue.is_full = PJ_FALSE;
            mgr->pub_ev_queue = &ev_queue;

            event_queue_add_event(mgr->pub_ev_queue, event);

            do {
                status = event_mgr_distribute_events(mgr, mgr->pub_ev_queue,
                                                     &mgr->pub_next_sub,
                                                     PJ_FALSE);
                if (status != PJ_SUCCESS && err == PJ_SUCCESS)
	            err = status;
            } while(ev_queue.head != ev_queue.tail || ev_queue.is_full);

            mgr->pub_ev_queue = NULL;
        }
    }
    pj_mutex_unlock(mgr->mutex);

    return err;
}
