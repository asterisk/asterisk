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
	<support_level>core</support_level>
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

static void *timerfd_timer_open(void);
static void timerfd_timer_close(void *data);
static int timerfd_timer_set_rate(void *data, unsigned int rate);
static int timerfd_timer_ack(void *data, unsigned int quantity);
static int timerfd_timer_enable_continuous(void *data);
static int timerfd_timer_disable_continuous(void *data);
static enum ast_timer_event timerfd_timer_get_event(void *data);
static unsigned int timerfd_timer_get_max_rate(void *data);
static int timerfd_timer_fd(void *data);

static struct ast_timing_interface timerfd_timing = {
	.name = "timerfd",
	.priority = 200,
	.timer_open = timerfd_timer_open,
	.timer_close = timerfd_timer_close,
	.timer_set_rate = timerfd_timer_set_rate,
	.timer_ack = timerfd_timer_ack,
	.timer_enable_continuous = timerfd_timer_enable_continuous,
	.timer_disable_continuous = timerfd_timer_disable_continuous,
	.timer_get_event = timerfd_timer_get_event,
	.timer_get_max_rate = timerfd_timer_get_max_rate,
	.timer_fd = timerfd_timer_fd,
};

#define TIMERFD_MAX_RATE 1000

struct timerfd_timer {
	int fd;
	struct itimerspec saved_timer;
	unsigned int is_continuous:1;
};

static void timer_destroy(void *obj)
{
	struct timerfd_timer *timer = obj;
	if (timer->fd > -1) {
		close(timer->fd);
	}
}

static void *timerfd_timer_open(void)
{
	struct timerfd_timer *timer;

	if (!(timer = ao2_alloc(sizeof(*timer), timer_destroy))) {
		ast_log(LOG_ERROR, "Could not allocate memory for timerfd_timer structure\n");
		return NULL;
	}
	if ((timer->fd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
		ast_log(LOG_ERROR, "Failed to create timerfd timer: %s\n", strerror(errno));
		ao2_ref(timer, -1);
		return NULL;
	}

	return timer;
}

static void timerfd_timer_close(void *data)
{
	ao2_ref(data, -1);
}

static int timerfd_timer_set_rate(void *data, unsigned int rate)
{
	struct timerfd_timer *timer = data;
	int res = 0;

	ao2_lock(timer);

	timer->saved_timer.it_value.tv_sec = 0;
	timer->saved_timer.it_value.tv_nsec = rate ? (long) (1000000000 / rate) : 0L;
	timer->saved_timer.it_interval.tv_sec = timer->saved_timer.it_value.tv_sec;
	timer->saved_timer.it_interval.tv_nsec = timer->saved_timer.it_value.tv_nsec;

	if (!timer->is_continuous) {
		res = timerfd_settime(timer->fd, 0, &timer->saved_timer, NULL);
	}

	ao2_unlock(timer);

	return res;
}

static int timerfd_timer_ack(void *data, unsigned int quantity)
{
	struct timerfd_timer *timer = data;
	uint64_t expirations;
	int read_result = 0;
	int res = 0;

	ao2_lock(timer);

	do {
		struct itimerspec timer_status;

		if (timerfd_gettime(timer->fd, &timer_status)) {
			ast_log(LOG_ERROR, "Call to timerfd_gettime() using handle %d error: %s\n", timer->fd, strerror(errno));
			expirations = 0;
			res = -1;
			break;
		}

		if (timer_status.it_value.tv_sec == 0 && timer_status.it_value.tv_nsec == 0) {
			ast_debug(1, "Avoiding read on disarmed timerfd %d\n", timer->fd);
			expirations = 0;
			break;
		}

		read_result = read(timer->fd, &expirations, sizeof(expirations));
		if (read_result == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			} else {
				ast_log(LOG_ERROR, "Read error: %s\n", strerror(errno));
				res = -1;
				break;
			}
		}
	} while (read_result != sizeof(expirations));

	ao2_unlock(timer);

	if (expirations != quantity) {
		ast_debug(2, "Expected to acknowledge %u ticks but got %llu instead\n", quantity, (unsigned long long) expirations);
	}

	return res;
}

static int timerfd_timer_enable_continuous(void *data)
{
	struct timerfd_timer *timer = data;
	int res;
	static const struct itimerspec continuous_timer = {
		.it_value.tv_nsec = 1L,
	};

	ao2_lock(timer);

	if (timer->is_continuous) {
		/*It's already in continous mode, no need to do
		 * anything further
		 */
		ao2_unlock(timer);
		return 0;
	}

	res = timerfd_settime(timer->fd, 0, &continuous_timer, &timer->saved_timer);
	timer->is_continuous = 1;
	ao2_unlock(timer);

	return res;
}

static int timerfd_timer_disable_continuous(void *data)
{
	struct timerfd_timer *timer = data;
	int res;

	ao2_lock(timer);

	if (!timer->is_continuous) {
		/* No reason to do anything if we're not
		 * in continuous mode
		 */
		ao2_unlock(timer);
		return 0;
	}

	res = timerfd_settime(timer->fd, 0, &timer->saved_timer, NULL);
	timer->is_continuous = 0;
	memset(&timer->saved_timer, 0, sizeof(timer->saved_timer));
	ao2_unlock(timer);

	return res;
}

static enum ast_timer_event timerfd_timer_get_event(void *data)
{
	struct timerfd_timer *timer = data;
	enum ast_timer_event res;

	ao2_lock(timer);

	if (timer->is_continuous) {
		res = AST_TIMING_EVENT_CONTINUOUS;
	} else {
		res = AST_TIMING_EVENT_EXPIRED;
	}

	ao2_unlock(timer);

	return res;
}

static unsigned int timerfd_timer_get_max_rate(void *data)
{
	return TIMERFD_MAX_RATE;
}

static int timerfd_timer_fd(void *data)
{
	struct timerfd_timer *timer = data;

	return timer->fd;
}

static int load_module(void)
{
	int fd;

	/* Make sure we support the necessary clock type */
	if ((fd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
		ast_log(LOG_ERROR, "timerfd_create() not supported by the kernel.  Not loading.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	close(fd);

	if (!(timing_funcs_handle = ast_register_timing_interface(&timerfd_timing))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_unregister_timing_interface(timing_funcs_handle);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Timerfd Timing Interface",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_TIMING,
);
