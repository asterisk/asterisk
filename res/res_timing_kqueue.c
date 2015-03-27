/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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
 * \author Tilghman Lesher \verbatim <tlesher AT digium DOT com> \endverbatim
 *
 * \brief kqueue timing interface
 *
 * \ingroup resource
 */

/*** MODULEINFO
	<depend>kqueue</depend>
	<conflict>launchd</conflict>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/timing.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/time.h"
#include "asterisk/test.h"
#include "asterisk/poll-compat.h"       /* for ast_poll() */

static void *timing_funcs_handle;

static void *kqueue_timer_open(void);
static void kqueue_timer_close(void *data);
static int kqueue_timer_set_rate(void *data, unsigned int rate);
static int kqueue_timer_ack(void *data, unsigned int quantity);
static int kqueue_timer_enable_continuous(void *data);
static int kqueue_timer_disable_continuous(void *data);
static enum ast_timer_event kqueue_timer_get_event(void *data);
static unsigned int kqueue_timer_get_max_rate(void *data);
static int kqueue_timer_fd(void *data);

static struct ast_timing_interface kqueue_timing = {
	.name = "kqueue",
	.priority = 150,
	.timer_open = kqueue_timer_open,
	.timer_close = kqueue_timer_close,
	.timer_set_rate = kqueue_timer_set_rate,
	.timer_ack = kqueue_timer_ack,
	.timer_enable_continuous = kqueue_timer_enable_continuous,
	.timer_disable_continuous = kqueue_timer_disable_continuous,
	.timer_get_event = kqueue_timer_get_event,
	.timer_get_max_rate = kqueue_timer_get_max_rate,
	.timer_fd = kqueue_timer_fd,
};

struct kqueue_timer {
	intptr_t period;
	int handle;
#ifndef EVFILT_USER
	int continuous_fd;
	unsigned int continuous_fd_valid:1;
#endif
	unsigned int is_continuous:1;
};

#ifdef EVFILT_USER
#define CONTINUOUS_EVFILT_TYPE EVFILT_USER
static int kqueue_timer_init_continuous_event(struct kqueue_timer *timer)
{
	return 0;
}

static int kqueue_timer_enable_continuous_event(struct kqueue_timer *timer)
{
	struct kevent kev[2];

	EV_SET(&kev[0], (uintptr_t)timer, EVFILT_USER, EV_ADD | EV_ENABLE,
		0, 0, NULL);
	EV_SET(&kev[1], (uintptr_t)timer, EVFILT_USER, 0, NOTE_TRIGGER,
		0, NULL);
	return kevent(timer->handle, kev, 2, NULL, 0, NULL);
}

static int kqueue_timer_disable_continuous_event(struct kqueue_timer *timer)
{
	struct kevent kev;

	EV_SET(&kev, (uintptr_t)timer, EVFILT_USER, EV_DELETE, 0, 0, NULL);
	return kevent(timer->handle, &kev, 1, NULL, 0, NULL);
}

static void kqueue_timer_fini_continuous_event(struct kqueue_timer *timer)
{
}

#else /* EVFILT_USER */

#define CONTINUOUS_EVFILT_TYPE EVFILT_READ
static int kqueue_timer_init_continuous_event(struct kqueue_timer *timer)
{
	int pipefds[2];
	int retval;

	retval = pipe(pipefds);
	if (retval == 0) {
		timer->continuous_fd = pipefds[0];
		timer->continuous_fd_valid = 1;
		close(pipefds[1]);
	}
	return retval;
}

static void kqueue_timer_fini_continuous_event(struct kqueue_timer *timer)
{
	if (timer->continuous_fd_valid) {
		close(timer->continuous_fd);
	}
}

static int kqueue_timer_enable_continuous_event(struct kqueue_timer *timer)
{
	struct kevent kev;

	EV_SET(&kev, timer->continuous_fd, EVFILT_READ, EV_ADD | EV_ENABLE,
		0, 0, NULL);
	return kevent(timer->handle, &kev, 1, NULL, 0, NULL);
}

static int kqueue_timer_disable_continuous_event(struct kqueue_timer *timer)
{
	struct kevent kev;

	EV_SET(&kev, timer->continuous_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	return kevent(timer->handle, &kev, 1, NULL, 0, NULL);
}
#endif

static void timer_destroy(void *obj)
{
	struct kqueue_timer *timer = obj;
	ast_debug(5, "[%d]: Timer Destroy\n", timer->handle);
	kqueue_timer_fini_continuous_event(timer);
	close(timer->handle);
}

static void *kqueue_timer_open(void)
{
	struct kqueue_timer *timer;

	if (!(timer = ao2_alloc(sizeof(*timer), timer_destroy))) {
		ast_log(LOG_ERROR, "Alloc failed for kqueue_timer structure\n");
		return NULL;
	}

	if ((timer->handle = kqueue()) < 0) {
		ast_log(LOG_ERROR, "Failed to create kqueue fd: %s\n",
			strerror(errno));
		ao2_ref(timer, -1);
		return NULL;
	}

	if (kqueue_timer_init_continuous_event(timer) != 0) {
		ast_log(LOG_ERROR, "Failed to create continuous event: %s\n",
			strerror(errno));
		ao2_ref(timer, -1);
		return NULL;
	}
	ast_debug(5, "[%d]: Create timer\n", timer->handle);
	return timer;
}

static void kqueue_timer_close(void *data)
{
	struct kqueue_timer *timer = data;

	ast_debug(5, "[%d]: Timer Close\n", timer->handle);
	ao2_ref(timer, -1);
}

/*
 * Use the highest precision available that does not overflow
 * the datatype kevent is using for time.
 */
static intptr_t kqueue_scale_period(unsigned int period_ns, int *units)
{
	uint64_t period = period_ns;
	*units = 0;
#ifdef NOTE_NSECONDS
	if (period < INTPTR_MAX) {
		*units = NOTE_NSECONDS;
	} else {
#ifdef NOTE_USECONDS
		period /= 1000;
		if (period < INTPTR_MAX) {
			*units = NOTE_USECONDS;
		} else {
			period /= 1000;
#ifdef NOTE_MSECONDS
			*units = NOTE_MSECONDS;
#endif	/* NOTE_MSECONDS */
		}
#else	/* NOTE_USECONDS */
		period /= 1000000;
#ifdef NOTE_MSECONDS
		*units = NOTE_MSECONDS;
#endif	/* NOTE_MSECONDS */
#endif	/* NOTE_USECONDS */
	}
#else	/* NOTE_NSECONDS */
	period /= 1000000;
#endif
	if (period > INTPTR_MAX) {
		period = INTPTR_MAX;
	}
	return period;
}

static int kqueue_timer_set_rate(void *data, unsigned int rate)
{
	struct kevent kev;
	struct kqueue_timer *timer = data;
	uint64_t period_ns;
	int flags;
	int units;
	int retval;

	ao2_lock(timer);

	if (rate == 0) {
		if (timer->period == 0) {
			ao2_unlock(timer);
			return (0);
		}
		flags = EV_DELETE;
		timer->period = 0;
		units = 0;
	} else  {
		flags = EV_ADD | EV_ENABLE;
		period_ns = (uint64_t)1000000000 / rate;
		timer->period = kqueue_scale_period(period_ns, &units);
	}
	ast_debug(5, "[%d]: Set rate %u:%ju\n",
		timer->handle, units, (uintmax_t)timer->period);
	EV_SET(&kev, timer->handle, EVFILT_TIMER, flags, units,
		timer->period, NULL);
	retval = kevent(timer->handle, &kev, 1, NULL, 0, NULL);

	if (retval == -1) {
		ast_log(LOG_ERROR, "[%d]: Error queing timer: %s\n",
			timer->handle, strerror(errno));
	}

	ao2_unlock(timer);

	return 0;
}

static int kqueue_timer_ack(void *data, unsigned int quantity)
{
	static struct timespec ts_nowait = { 0, 0 };
	struct kqueue_timer *timer = data;
	struct kevent kev[2];
	int i, retval;

	ao2_lock(timer);

	retval = kevent(timer->handle, NULL, 0, kev, 2, &ts_nowait);
	if (retval == -1) {
		ast_log(LOG_ERROR, "[%d]: Error sampling kqueue: %s\n",
			timer->handle, strerror(errno));
		ao2_unlock(timer);
		return -1;
	}

	for (i = 0; i < retval; i++) {
		switch (kev[i].filter) {
		case EVFILT_TIMER:
			if (kev[i].data > quantity) {
				ast_log(LOG_ERROR, "[%d]: Missed %ju\n",
					timer->handle,
					(uintmax_t)kev[i].data - quantity);
			}
			break;
		case CONTINUOUS_EVFILT_TYPE:
			if (!timer->is_continuous) {
				ast_log(LOG_ERROR,
					"[%d]: Spurious user event\n",
					timer->handle);
			}
			break;
		default:
			ast_log(LOG_ERROR, "[%d]: Spurious kevent type %d.\n",
				timer->handle, kev[i].filter);
		}
	}

	ao2_unlock(timer);

	return 0;
}

static int kqueue_timer_enable_continuous(void *data)
{
	struct kqueue_timer *timer = data;
	int retval;

	ao2_lock(timer);

	if (!timer->is_continuous) {
		ast_debug(5, "[%d]: Enable Continuous\n", timer->handle);
		retval = kqueue_timer_enable_continuous_event(timer);
		if (retval == -1) {
			ast_log(LOG_ERROR,
				"[%d]: Error signaling continuous event: %s\n",
				timer->handle, strerror(errno));
		}
		timer->is_continuous = 1;
	}

	ao2_unlock(timer);

	return 0;
}

static int kqueue_timer_disable_continuous(void *data)
{
	struct kqueue_timer *timer = data;
	int retval;

	ao2_lock(timer);

	if (timer->is_continuous) {
		ast_debug(5, "[%d]: Disable Continuous\n", timer->handle);
		retval = kqueue_timer_disable_continuous_event(timer);
		if (retval == -1) {
			ast_log(LOG_ERROR,
				"[%d]: Error clearing continuous event: %s\n",
				timer->handle, strerror(errno));
		}
		timer->is_continuous = 0;
	}

	ao2_unlock(timer);

	return 0;
}

static enum ast_timer_event kqueue_timer_get_event(void *data)
{
	struct kqueue_timer *timer = data;
	enum ast_timer_event res;

	if (timer->is_continuous) {
		res = AST_TIMING_EVENT_CONTINUOUS;
	} else {
		res = AST_TIMING_EVENT_EXPIRED;
	}

	return res;
}

static unsigned int kqueue_timer_get_max_rate(void *data)
{
	return INTPTR_MAX > UINT_MAX ? UINT_MAX : INTPTR_MAX;
}

static int kqueue_timer_fd(void *data)
{
	struct kqueue_timer *timer = data;

	return timer->handle;
}

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_kqueue_timing)
{
	int res = AST_TEST_PASS, i;
	uint64_t diff;
	struct pollfd pfd = { 0, POLLIN, 0 };
	struct kqueue_timer *kt;
	struct timeval start;

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_kqueue_timing";
		info->category = "/res/res_timing_kqueue/";
		info->summary = "Test KQueue timing interface";
		info->description = "Verify that the KQueue timing interface correctly generates timing events";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(kt = kqueue_timer_open())) {
		ast_test_status_update(test, "Cannot open timer!\n");
		return AST_TEST_FAIL;
	}

	do {
		pfd.fd = kqueue_timer_fd(kt);
		if (kqueue_timer_set_rate(kt, 1000)) {
			ast_test_status_update(test, "Cannot set timer rate to 1000/s\n");
			res = AST_TEST_FAIL;
			break;
		}
		if (ast_poll(&pfd, 1, 1000) < 1) {
			ast_test_status_update(test, "Polling on a kqueue doesn't work\n");
			res = AST_TEST_FAIL;
			break;
		}
		if (pfd.revents != POLLIN) {
			ast_test_status_update(test, "poll() should have returned POLLIN, but instead returned %hd\n", pfd.revents);
			res = AST_TEST_FAIL;
			break;
		}
		if (kqueue_timer_get_event(kt) <= 0) {
			ast_test_status_update(test, "No events generated after a poll returned successfully?!!\n");
			res = AST_TEST_FAIL;
			break;
		}
		if (kqueue_timer_ack(kt, 1) != 0) {
			ast_test_status_update(test, "Acking event failed.\n");
			res = AST_TEST_FAIL;
			break;
		}

		kqueue_timer_enable_continuous(kt);
		start = ast_tvnow();
		for (i = 0; i < 100; i++) {
			if (ast_poll(&pfd, 1, 1000) < 1) {
				ast_test_status_update(test, "Polling on a kqueue doesn't work\n");
				res = AST_TEST_FAIL;
				break;
			}
			if (kqueue_timer_get_event(kt) <= 0) {
				ast_test_status_update(test, "No events generated in continuous mode after 1 microsecond?!!\n");
				res = AST_TEST_FAIL;
				break;
			}
			if (kqueue_timer_ack(kt, 1) != 0) {
				ast_test_status_update(test, "Acking event failed.\n");
				res = AST_TEST_FAIL;
				break;
			}

		}
		diff = ast_tvdiff_us(ast_tvnow(), start);
		ast_test_status_update(test, "diff is %llu\n", diff);
	} while (0);
	kqueue_timer_close(kt);
	return res;
}
#endif

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (!(timing_funcs_handle = ast_register_timing_interface(&kqueue_timing))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(test_kqueue_timing);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_kqueue_timing);

	return ast_unregister_timing_interface(timing_funcs_handle);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "KQueue Timing Interface",
		.support_level = AST_MODULE_SUPPORT_EXTENDED,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
		);
