/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \file
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief pthread timing interface
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE();

#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

#include "asterisk/module.h"
#include "asterisk/timing.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/time.h"
#include "asterisk/lock.h"

static void *timing_funcs_handle;

static void *pthread_timer_open(void);
static void pthread_timer_close(void *data);
static int pthread_timer_set_rate(void *data, unsigned int rate);
static int pthread_timer_ack(void *data, unsigned int quantity);
static int pthread_timer_enable_continuous(void *data);
static int pthread_timer_disable_continuous(void *data);
static enum ast_timer_event pthread_timer_get_event(void *data);
static unsigned int pthread_timer_get_max_rate(void *data);
static int pthread_timer_fd(void *data);

static struct ast_timing_interface pthread_timing = {
	.name = "pthread",
	.priority = 0, /* use this as a last resort */
	.timer_open = pthread_timer_open,
	.timer_close = pthread_timer_close,
	.timer_set_rate = pthread_timer_set_rate,
	.timer_ack = pthread_timer_ack,
	.timer_enable_continuous = pthread_timer_enable_continuous,
	.timer_disable_continuous = pthread_timer_disable_continuous,
	.timer_get_event = pthread_timer_get_event,
	.timer_get_max_rate = pthread_timer_get_max_rate,
	.timer_fd = pthread_timer_fd,
};

/* 1 tick / 10 ms */
#define MAX_RATE 100

static struct ao2_container *pthread_timers;
#define PTHREAD_TIMER_BUCKETS 563

enum {
	PIPE_READ =  0,
	PIPE_WRITE = 1
};

enum pthread_timer_state {
	TIMER_STATE_IDLE,
	TIMER_STATE_TICKING,
};

struct pthread_timer {
	int pipe[2];
	enum pthread_timer_state state;
	unsigned int rate;
	/*! Interval in ms for current rate */
	unsigned int interval;
	unsigned int tick_count;
	unsigned int pending_ticks;
	struct timeval start;
	bool continuous:1;
	bool pipe_signaled:1;
};

static void pthread_timer_destructor(void *obj);
static void signal_pipe(struct pthread_timer *timer);
static void unsignal_pipe(struct pthread_timer *timer);
static void ack_ticks(struct pthread_timer *timer, unsigned int num);

/*!
 * \brief Data for the timing thread
 */
static struct {
	pthread_t thread;
	ast_mutex_t lock;
	ast_cond_t cond;
	unsigned int stop:1;
} timing_thread;

static void *pthread_timer_open(void)
{
	struct pthread_timer *timer;
	int i;

	if (!(timer = ao2_alloc(sizeof(*timer), pthread_timer_destructor))) {
		errno = ENOMEM;
		return NULL;
	}

	timer->pipe[PIPE_READ] = timer->pipe[PIPE_WRITE] = -1;
	timer->state = TIMER_STATE_IDLE;

	if (pipe(timer->pipe)) {
		ao2_ref(timer, -1);
		return NULL;
	}

	for (i = 0; i < ARRAY_LEN(timer->pipe); ++i) {
		int flags = fcntl(timer->pipe[i], F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(timer->pipe[i], F_SETFL, flags);
	}
	
	ao2_lock(pthread_timers);
	if (!ao2_container_count(pthread_timers)) {
		ast_mutex_lock(&timing_thread.lock);
		ast_cond_signal(&timing_thread.cond);
		ast_mutex_unlock(&timing_thread.lock);
	}
	ao2_link_flags(pthread_timers, timer, OBJ_NOLOCK);
	ao2_unlock(pthread_timers);

	return timer;
}

static void pthread_timer_close(void *data)
{
	struct pthread_timer *timer = data;

	ao2_unlink(pthread_timers, timer);
	ao2_ref(timer, -1);
}

static int pthread_timer_set_rate(void *data, unsigned int rate)
{
	struct pthread_timer *timer = data;

	if (rate > MAX_RATE) {
		ast_log(LOG_ERROR, "res_timing_pthread only supports timers at a "
				"max rate of %d / sec\n", MAX_RATE);
		errno = EINVAL;
		return -1;
	}

	ao2_lock(timer);

	if ((timer->rate = rate)) {
		timer->interval = roundf(1000.0 / ((float) rate));
		timer->start = ast_tvnow();
		timer->state = TIMER_STATE_TICKING;
	} else {
		timer->interval = 0;
		timer->start = ast_tv(0, 0);
		timer->state = TIMER_STATE_IDLE;
	}
	timer->tick_count = 0;

	ao2_unlock(timer);

	return 0;
}

static int pthread_timer_ack(void *data, unsigned int quantity)
{
	struct pthread_timer *timer = data;

	ast_assert(quantity > 0);

	ao2_lock(timer);
	ack_ticks(timer, quantity);
	ao2_unlock(timer);

	return 0;
}

static int pthread_timer_enable_continuous(void *data)
{
	struct pthread_timer *timer = data;

	ao2_lock(timer);
	if (!timer->continuous) {
		timer->continuous = true;
		signal_pipe(timer);
	}
	ao2_unlock(timer);

	return 0;
}

static int pthread_timer_disable_continuous(void *data)
{
	struct pthread_timer *timer = data;

	ao2_lock(timer);
	if (timer->continuous) {
		timer->continuous = false;
		unsignal_pipe(timer);
	}
	ao2_unlock(timer);

	return 0;
}

static enum ast_timer_event pthread_timer_get_event(void *data)
{
	struct pthread_timer *timer = data;
	enum ast_timer_event res = AST_TIMING_EVENT_EXPIRED;

	ao2_lock(timer);
	if (timer->continuous) {
		res = AST_TIMING_EVENT_CONTINUOUS;
	}
	ao2_unlock(timer);

	return res;
}

static unsigned int pthread_timer_get_max_rate(void *data)
{
	return MAX_RATE;
}

static int pthread_timer_fd(void *data)
{
	struct pthread_timer *timer = data;

	return timer->pipe[PIPE_READ];
}

static void pthread_timer_destructor(void *obj)
{
	struct pthread_timer *timer = obj;

	if (timer->pipe[PIPE_READ] > -1) {
		close(timer->pipe[PIPE_READ]);
		timer->pipe[PIPE_READ] = -1;
	}

	if (timer->pipe[PIPE_WRITE] > -1) {
		close(timer->pipe[PIPE_WRITE]);
		timer->pipe[PIPE_WRITE] = -1;
	}
}

/*!
 * \note only PIPE_READ is guaranteed valid
 */
static int pthread_timer_hash(const void *obj, const int flags)
{
	const struct pthread_timer *timer = obj;

	return timer->pipe[PIPE_READ];
}

/*!
 * \note only PIPE_READ is guaranteed valid
 */
static int pthread_timer_cmp(void *obj, void *arg, int flags)
{
	struct pthread_timer *timer1 = obj, *timer2 = arg;

	return (timer1->pipe[PIPE_READ] == timer2->pipe[PIPE_READ]) ? CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \retval 0 no timer tick needed
 * \retval non-zero write to the timing pipe needed
 */
static int check_timer(struct pthread_timer *timer)
{
	struct timeval now;

	if (timer->state == TIMER_STATE_IDLE) {
		return 0;
	}

	now = ast_tvnow();

	if (timer->tick_count < (ast_tvdiff_ms(now, timer->start) / timer->interval)) {
		timer->tick_count++;
		if (!timer->tick_count) {
			/* Handle overflow. */
			timer->start = now;
		}
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \pre timer is locked
 */
static void ack_ticks(struct pthread_timer *timer, unsigned int quantity)
{
	int pending_ticks = timer->pending_ticks;

	ast_assert(quantity);

	if (quantity > pending_ticks) {
		quantity = pending_ticks;
	}

	if (!quantity) {
		return;
	}

	timer->pending_ticks -= quantity;

	if ((0 == timer->pending_ticks) && !timer->continuous) {
		unsignal_pipe(timer);
	}
}

/*!
 * \internal
 * \pre timer is locked
 */
static void signal_pipe(struct pthread_timer *timer)
{
	ssize_t res;
	unsigned char x = 42;

	if (timer->pipe_signaled) {
		return;
	}

	res = write(timer->pipe[PIPE_WRITE], &x, 1);
	if (-1 == res) {
		ast_log(LOG_ERROR, "Error writing to timing pipe: %s\n",
				strerror(errno));
	} else {
		timer->pipe_signaled = true;
	}
}

/*!
 * \internal
 * \pre timer is locked
 */
static void unsignal_pipe(struct pthread_timer *timer)
{
	ssize_t res;
	unsigned long buffer;

	if (!timer->pipe_signaled) {
		return;
	}

	res = read(timer->pipe[PIPE_READ], &buffer, sizeof(buffer));
	if (-1 == res) {
		ast_log(LOG_ERROR, "Error reading from pipe: %s\n",
				strerror(errno));
	} else {
		timer->pipe_signaled = false;
	}
}

static int run_timer(void *obj, void *arg, int flags)
{
	struct pthread_timer *timer = obj;

	if (timer->state == TIMER_STATE_IDLE) {
		return 0;
	}

	ao2_lock(timer);
	if (check_timer(timer)) {
		timer->pending_ticks++;
		signal_pipe(timer);
	}
	ao2_unlock(timer);

	return 0;
}

static void *do_timing(void *arg)
{
	struct timeval next_wakeup = ast_tvnow();

	while (!timing_thread.stop) {
		struct timespec ts = { 0, };

		ao2_callback(pthread_timers, OBJ_NODATA, run_timer, NULL);

		next_wakeup = ast_tvadd(next_wakeup, ast_tv(0, 5000));

		ts.tv_sec = next_wakeup.tv_sec;
		ts.tv_nsec = next_wakeup.tv_usec * 1000;

		ast_mutex_lock(&timing_thread.lock);
		if (!timing_thread.stop) {
			if (ao2_container_count(pthread_timers)) {
				ast_cond_timedwait(&timing_thread.cond, &timing_thread.lock, &ts);
			} else {
				ast_cond_wait(&timing_thread.cond, &timing_thread.lock);
			}
		}
		ast_mutex_unlock(&timing_thread.lock);
	}

	return NULL;
}

static int init_timing_thread(void)
{
	ast_mutex_init(&timing_thread.lock);
	ast_cond_init(&timing_thread.cond, NULL);

	if (ast_pthread_create_background(&timing_thread.thread, NULL, do_timing, NULL)) {
		ast_log(LOG_ERROR, "Unable to start timing thread.\n");
		return -1;
	}

	return 0;
}

static int load_module(void)
{
	if (!(pthread_timers = ao2_container_alloc(PTHREAD_TIMER_BUCKETS,
		pthread_timer_hash, pthread_timer_cmp))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (init_timing_thread()) {
		ao2_ref(pthread_timers, -1);
		pthread_timers = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	return (timing_funcs_handle = ast_register_timing_interface(&pthread_timing)) ?
		AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

static int unload_module(void)
{
	int res;

	ast_mutex_lock(&timing_thread.lock);
	timing_thread.stop = 1;
	ast_cond_signal(&timing_thread.cond);
	ast_mutex_unlock(&timing_thread.lock);
	pthread_join(timing_thread.thread, NULL);

	if (!(res = ast_unregister_timing_interface(timing_funcs_handle))) {
		ao2_ref(pthread_timers, -1);
		pthread_timers = NULL;
	}

	return res;
}
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "pthread Timing Interface",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_TIMING,
);
