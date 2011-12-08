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
 * \author Tilghman Lesher <tlesher AT digium DOT com>
 *
 * \brief kqueue timing interface
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

static int kqueue_timer_open(void);
static void kqueue_timer_close(int handle);
static int kqueue_timer_set_rate(int handle, unsigned int rate);
static void kqueue_timer_ack(int handle, unsigned int quantity);
static int kqueue_timer_enable_continuous(int handle);
static int kqueue_timer_disable_continuous(int handle);
static enum ast_timer_event kqueue_timer_get_event(int handle);
static unsigned int kqueue_timer_get_max_rate(int handle);

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
};

static struct ao2_container *kqueue_timers;

struct kqueue_timer {
	int handle;
	uint64_t nsecs;
	uint64_t unacked;
	unsigned int is_continuous:1;
};

static int kqueue_timer_hash(const void *obj, const int flags)
{
	const struct kqueue_timer *timer = obj;

	return timer->handle;
}

static int kqueue_timer_cmp(void *obj, void *args, int flags)
{
	struct kqueue_timer *timer1 = obj, *timer2 = args;
	return timer1->handle == timer2->handle ? CMP_MATCH | CMP_STOP : 0;
}

static void timer_destroy(void *obj)
{
	struct kqueue_timer *timer = obj;
	close(timer->handle);
}

#define lookup_timer(a)	_lookup_timer(a, __FILE__, __LINE__, __PRETTY_FUNCTION__)
static struct kqueue_timer *_lookup_timer(int handle, const char *file, int line, const char *func)
{
	struct kqueue_timer *our_timer, find_helper = {
		.handle = handle,
	};

	if (!(our_timer = ao2_find(kqueue_timers, &find_helper, OBJ_POINTER))) {
		ast_log(__LOG_ERROR, file, line, func, "Couldn't find timer with handle %d\n", handle);
		/* API says we set errno */
		errno = ESRCH;
		return NULL;
	}
	return our_timer;
}

static int kqueue_timer_open(void)
{
	struct kqueue_timer *timer;
	int handle;

	if (!(timer = ao2_alloc(sizeof(*timer), timer_destroy))) {
		ast_log(LOG_ERROR, "Could not allocate memory for kqueue_timer structure\n");
		return -1;
	}
	if ((handle = kqueue()) < 0) {
		ast_log(LOG_ERROR, "Failed to create kqueue timer: %s\n", strerror(errno));
		ao2_ref(timer, -1);
		return -1;
	}

	timer->handle = handle;
	ao2_link(kqueue_timers, timer);
	/* Get rid of the reference from the allocation */
	ao2_ref(timer, -1);
	return handle;
}

static void kqueue_timer_close(int handle)
{
	struct kqueue_timer *our_timer;

	if (!(our_timer = lookup_timer(handle))) {
		return;
	}

	ao2_unlink(kqueue_timers, our_timer);
	ao2_ref(our_timer, -1);
}

static void kqueue_set_nsecs(struct kqueue_timer *our_timer, uint64_t nsecs)
{
	struct timespec nowait = { 0, 1 };
#ifdef HAVE_KEVENT64
	struct kevent64_s kev;

	EV_SET64(&kev, our_timer->handle, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS,
		nsecs, 0, 0, 0);
	kevent64(our_timer->handle, &kev, 1, NULL, 0, 0, &nowait);
#else
	struct kevent kev;

	EV_SET(&kev, our_timer->handle, EVFILT_TIMER, EV_ADD | EV_ENABLE,
#ifdef NOTE_NSECONDS
		nsecs <= 0xFFffFFff ? NOTE_NSECONDS :
#endif
#ifdef NOTE_USECONDS
		NOTE_USECONDS
#else /* Milliseconds, if no constants are defined */
		0
#endif
		,
#ifdef NOTE_NSECONDS
		nsecs <= 0xFFffFFff ? nsecs :
#endif
#ifdef NOTE_USECONDS
	nsecs / 1000
#else /* Milliseconds, if nothing else is defined */
	nsecs / 1000000
#endif
	, NULL);
	kevent(our_timer->handle, &kev, 1, NULL, 0, &nowait);
#endif
}

static int kqueue_timer_set_rate(int handle, unsigned int rate)
{
	struct kqueue_timer *our_timer;

	if (!(our_timer = lookup_timer(handle))) {
		return -1;
	}

	kqueue_set_nsecs(our_timer, (our_timer->nsecs = rate ? (long) (1000000000 / rate) : 0L));
	ao2_ref(our_timer, -1);

	return 0;
}

static void kqueue_timer_ack(int handle, unsigned int quantity)
{
	struct kqueue_timer *our_timer;

	if (!(our_timer = lookup_timer(handle))) {
		return;
	}

	if (our_timer->unacked < quantity) {
		ast_debug(1, "Acking more events than have expired?!!\n");
		our_timer->unacked = 0;
	} else {
		our_timer->unacked -= quantity;
	}
}

static int kqueue_timer_enable_continuous(int handle)
{
	struct kqueue_timer *our_timer;

	if (!(our_timer = lookup_timer(handle))) {
		return -1;
	}

	kqueue_set_nsecs(our_timer, 1);
	our_timer->is_continuous = 1;
	our_timer->unacked = 0;
	ao2_ref(our_timer, -1);
	return 0;
}

static int kqueue_timer_disable_continuous(int handle)
{
	struct kqueue_timer *our_timer;

	if (!(our_timer = lookup_timer(handle))) {
		return -1;
	}

	kqueue_set_nsecs(our_timer, our_timer->nsecs);
	our_timer->is_continuous = 0;
	our_timer->unacked = 0;
	ao2_ref(our_timer, -1);
	return 0;
}

static enum ast_timer_event kqueue_timer_get_event(int handle)
{
	enum ast_timer_event res = -1;
	struct kqueue_timer *our_timer;
	struct timespec sixty_seconds = { 60, 0 };
	struct kevent kev;

	if (!(our_timer = lookup_timer(handle))) {
		return -1;
	}

	/* If we have non-ACKed events, just return immediately */
	if (our_timer->unacked == 0) {
		if (kevent(handle, NULL, 0, &kev, 1, &sixty_seconds) > 0) {
			our_timer->unacked += kev.data;
		}
	}

	if (our_timer->unacked > 0) {
		res = our_timer->is_continuous ? AST_TIMING_EVENT_CONTINUOUS : AST_TIMING_EVENT_EXPIRED;
	}

	ao2_ref(our_timer, -1);
	return res;
}

static unsigned int kqueue_timer_get_max_rate(int handle)
{
	/* Actually, the max rate is 2^64-1 seconds, but that's not representable in a 32-bit integer. */
	return UINT_MAX;
}

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_kqueue_timing)
{
	int res = AST_TEST_PASS, handle, i;
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

	if (!(handle = kqueue_timer_open())) {
		ast_test_status_update(test, "Cannot open timer!\n");
		return AST_TEST_FAIL;
	}

	do {
		pfd.fd = handle;
		if (kqueue_timer_set_rate(handle, 1000)) {
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
		if (!(kt = lookup_timer(handle))) {
			ast_test_status_update(test, "Could not find timer structure in container?!!\n");
			res = AST_TEST_FAIL;
			break;
		}
		if (kqueue_timer_get_event(handle) <= 0) {
			ast_test_status_update(test, "No events generated after a poll returned successfully?!!\n");
			res = AST_TEST_FAIL;
			break;
		}
#if 0
		if (kt->unacked == 0) {
			ast_test_status_update(test, "Unacked events is 0, but there should be at least 1.\n");
			res = AST_TEST_FAIL;
			break;
		}
#endif
		kqueue_timer_enable_continuous(handle);
		start = ast_tvnow();
		for (i = 0; i < 100; i++) {
			if (ast_poll(&pfd, 1, 1000) < 1) {
				ast_test_status_update(test, "Polling on a kqueue doesn't work\n");
				res = AST_TEST_FAIL;
				break;
			}
			if (kqueue_timer_get_event(handle) <= 0) {
				ast_test_status_update(test, "No events generated in continuous mode after 1 microsecond?!!\n");
				res = AST_TEST_FAIL;
				break;
			}
		}
		diff = ast_tvdiff_us(ast_tvnow(), start);
		ast_test_status_update(test, "diff is %llu\n", diff);
		/*
		if (abs(diff - kt->unacked) == 0) {
			ast_test_status_update(test, "Unacked events should be around 1000, not %llu\n", kt->unacked);
			res = AST_TEST_FAIL;
		}
		*/
	} while (0);
	kqueue_timer_close(handle);
	return res;
}
#endif

static int load_module(void)
{
	if (!(kqueue_timers = ao2_container_alloc(563, kqueue_timer_hash, kqueue_timer_cmp))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(timing_funcs_handle = ast_register_timing_interface(&kqueue_timing))) {
		ao2_ref(kqueue_timers, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(test_kqueue_timing);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res;

	AST_TEST_UNREGISTER(test_kqueue_timing);
	if (!(res = ast_unregister_timing_interface(timing_funcs_handle))) {
		ao2_ref(kqueue_timers, -1);
		kqueue_timers = NULL;
	}

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "KQueue Timing Interface",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
		);
