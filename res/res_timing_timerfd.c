/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * \brief timerfd timing interface
 */

/*** MODULEINFO
	<depend>timerfd</depend>
	<conflict>res_timing_pthread</conflict>
	<conflict>res_timing_dahdi</conflict>
 ***/

#include "asterisk.h"

#include <sys/timerfd.h>

#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/timing.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/time.h"

static void *timing_funcs_handle;

static int timerfd_timer_open(void);
static void timerfd_timer_close(int handle);
static int timerfd_timer_set_rate(int handle, unsigned int rate);
static void timerfd_timer_ack(int handle, unsigned int quantity);
static int timerfd_timer_enable_continuous(int handle);
static int timerfd_timer_disable_continuous(int handle);
static enum ast_timing_event timerfd_timer_get_event(int handle);
static unsigned int timerfd_timer_get_max_rate(int handle);

static struct ast_timing_functions timerfd_timing_functions = {
	.timer_open = timerfd_timer_open,
	.timer_close = timerfd_timer_close,
	.timer_set_rate = timerfd_timer_set_rate,
	.timer_ack = timerfd_timer_ack,
	.timer_enable_continuous = timerfd_timer_enable_continuous,
	.timer_disable_continuous = timerfd_timer_disable_continuous,
	.timer_get_event = timerfd_timer_get_event,
	.timer_get_max_rate = timerfd_timer_get_max_rate,
};

static struct ao2_container *timerfd_timers;

#define TIMERFD_TIMER_BUCKETS 563
#define TIMERFD_MAX_RATE 1000

struct timerfd_timer {
	int handle;
	struct itimerspec saved_timer;
	unsigned int is_continuous:1;
};

static int timerfd_timer_hash(const void *obj, const int flags)
{
	const struct timerfd_timer *timer = obj;

	return timer->handle;
}

static int timerfd_timer_cmp(void *obj, void *args, int flags)
{
	struct timerfd_timer *timer1 = obj, *timer2 = args;
	return timer1->handle == timer2->handle ? CMP_MATCH | CMP_STOP : 0;
}

static void timer_destroy(void *obj)
{
	struct timerfd_timer *timer = obj;
	close(timer->handle);
}

static int timerfd_timer_open(void)
{
	struct timerfd_timer *timer;
	int handle;

	if (!(timer = ao2_alloc(sizeof(*timer), timer_destroy))) {
		ast_log(LOG_ERROR, "Could not allocate memory for timerfd_timer structure\n");
		return -1;
	}
	if ((handle = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
		ast_log(LOG_ERROR, "Failed to create timerfd timer: %s\n", strerror(errno));
		ao2_ref(timer, -1);
		return -1;
	}

	timer->handle = handle;
	ao2_link(timerfd_timers, timer);
	/* Get rid of the reference from the allocation */
	ao2_ref(timer, -1);
	return handle;
}

static void timerfd_timer_close(int handle)
{
	struct timerfd_timer *our_timer, find_helper = {
		.handle = handle,
	};

	if (!(our_timer = ao2_find(timerfd_timers, &find_helper, OBJ_POINTER))) {
		ast_log(LOG_ERROR, "Couldn't find timer with handle %d\n", handle);
		return;
	}

	ao2_unlink(timerfd_timers, our_timer);
	ao2_ref(our_timer, -1);
}

static int timerfd_timer_set_rate(int handle, unsigned int rate)
{
	struct itimerspec itspec;
	itspec.it_value.tv_sec = 0;
	itspec.it_value.tv_nsec = rate ? (long) (1000000000 / rate) : 0L;
	itspec.it_interval.tv_sec = itspec.it_value.tv_sec;
	itspec.it_interval.tv_nsec = itspec.it_value.tv_nsec;

	return timerfd_settime(handle, 0, &itspec, NULL);
}

static void timerfd_timer_ack(int handle, unsigned int quantity)
{
	uint64_t expirations;
	int read_result = 0;

	do {
		read_result = read(handle, &expirations, sizeof(expirations));
		if (read_result == -1) {
			if (errno == EINTR) {
				continue;
			} else {
				ast_log(LOG_ERROR, "Read error: %s\n", strerror(errno));
				break;
			}
		}
	} while (read_result != sizeof(expirations));

	if (expirations != quantity) {
		ast_debug(2, "Expected to acknowledge %u ticks but got %llu instead\n", quantity, (unsigned long long) expirations);
	}
}

static int timerfd_timer_enable_continuous(int handle)
{
	int res;
	struct itimerspec continuous_timer = {
		.it_value.tv_nsec = 1L,
	};
	struct timerfd_timer *our_timer, find_helper = {
		.handle = handle,
	};

	if (!(our_timer = ao2_find(timerfd_timers, &find_helper, OBJ_POINTER))) {
		ast_log(LOG_ERROR, "Couldn't find timer with handle %d\n", handle);
		return -1;
	}

	if (our_timer->is_continuous) {
		/*It's already in continous mode, no need to do
		 * anything further
		 */
		ao2_ref(our_timer, -1);
		return 0;
	}

	res = timerfd_settime(handle, 0, &continuous_timer, &our_timer->saved_timer);
	our_timer->is_continuous = 1;
	ao2_ref(our_timer, -1);
	return res;
}

static int timerfd_timer_disable_continuous(int handle)
{
	int res;
	struct timerfd_timer *our_timer, find_helper = {
		.handle = handle,
	};

	if (!(our_timer = ao2_find(timerfd_timers, &find_helper, OBJ_POINTER))) {
		ast_log(LOG_ERROR, "Couldn't find timer with handle %d\n", handle);
		return -1;
	}

	if(!our_timer->is_continuous) {
		/* No reason to do anything if we're not
		 * in continuous mode
		 */
		ao2_ref(our_timer, -1);
		return 0;
	}

	res = timerfd_settime(handle, 0, &our_timer->saved_timer, NULL);
	our_timer->is_continuous = 0;
	memset(&our_timer->saved_timer, 0, sizeof(our_timer->saved_timer));
	ao2_ref(our_timer, -1);
	return res;
}

static enum ast_timing_event timerfd_timer_get_event(int handle)
{
	enum ast_timing_event res;
	struct timerfd_timer *our_timer, find_helper = {
		.handle = handle,
	};

	if (!(our_timer = ao2_find(timerfd_timers, &find_helper, OBJ_POINTER))) {
		ast_log(LOG_ERROR, "Couldn't find timer with handle %d\n", handle);
		return -1;
	}

	if (our_timer->is_continuous) {
		res = AST_TIMING_EVENT_CONTINUOUS;
	} else {
		res = AST_TIMING_EVENT_EXPIRED;
	}

	ao2_ref(our_timer, -1);
	return res;
}

static unsigned int timerfd_timer_get_max_rate(int handle)
{
	return TIMERFD_MAX_RATE;
}

static int load_module(void)
{
	if (!(timerfd_timers = ao2_container_alloc(TIMERFD_TIMER_BUCKETS, timerfd_timer_hash, timerfd_timer_cmp))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(timing_funcs_handle = ast_install_timing_functions(&timerfd_timing_functions))) {
		ao2_ref(timerfd_timers, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	/* ast_uninstall_timing_functions(timing_funcs_handle); */

	/* This module can not currently be unloaded.  No use count handling is being done. */

	return -1;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Timerfd Timing Interface");
