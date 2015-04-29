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
 * \brief DAHDI timing interface 
 */

/*** MODULEINFO
	<load_priority>timing</load_priority>
	<depend>dahdi</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE();

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include <dahdi/user.h>

#include "asterisk/module.h"
#include "asterisk/timing.h"
#include "asterisk/utils.h"

static void *dahdi_timer_open(void);
static void dahdi_timer_close(void *data);
static int dahdi_timer_set_rate(void *data, unsigned int rate);
static int dahdi_timer_ack(void *data, unsigned int quantity);
static int dahdi_timer_enable_continuous(void *data);
static int dahdi_timer_disable_continuous(void *data);
static enum ast_timer_event dahdi_timer_get_event(void *data);
static unsigned int dahdi_timer_get_max_rate(void *data);
static int dahdi_timer_fd(void *data);

static struct ast_timing_interface dahdi_timing = {
	.name = "DAHDI",
	.priority = 100,
	.timer_open = dahdi_timer_open,
	.timer_close = dahdi_timer_close,
	.timer_set_rate = dahdi_timer_set_rate,
	.timer_ack = dahdi_timer_ack,
	.timer_enable_continuous = dahdi_timer_enable_continuous,
	.timer_disable_continuous = dahdi_timer_disable_continuous,
	.timer_get_event = dahdi_timer_get_event,
	.timer_get_max_rate = dahdi_timer_get_max_rate,
	.timer_fd = dahdi_timer_fd,
};

struct dahdi_timer {
	int fd;
};

static void *dahdi_timer_open(void)
{
	struct dahdi_timer *timer;

	if (!(timer = ast_calloc(1, sizeof(*timer)))) {
		return NULL;
	}

	if ((timer->fd = open("/dev/dahdi/timer", O_RDWR)) < 0) {
		ast_log(LOG_ERROR, "Failed to create dahdi timer: %s\n", strerror(errno));
		ast_free(timer);
		return NULL;
	}

	return timer;
}

static void dahdi_timer_close(void *data)
{
	struct dahdi_timer *timer = data;

	close(timer->fd);
	ast_free(timer);
}

static int dahdi_timer_set_rate(void *data, unsigned int rate)
{
	struct dahdi_timer *timer = data;
	int samples;

	/* DAHDI timers are configured using a number of samples,
	 * based on an 8 kHz sample rate. */
	samples = (unsigned int) roundf((8000.0 / ((float) rate)));

	if (ioctl(timer->fd, DAHDI_TIMERCONFIG, &samples)) {
		ast_log(LOG_ERROR, "Failed to configure DAHDI timing fd for %d sample timer ticks\n",
			samples);
		return -1;
	}

	return 0;
}

static int dahdi_timer_ack(void *data, unsigned int quantity)
{
	struct dahdi_timer *timer = data;

	return ioctl(timer->fd, DAHDI_TIMERACK, &quantity) ? -1 : 0;
}

static int dahdi_timer_enable_continuous(void *data)
{
	struct dahdi_timer *timer = data;
	int flags = 1;

	return ioctl(timer->fd, DAHDI_TIMERPING, &flags) ? -1 : 0;
}

static int dahdi_timer_disable_continuous(void *data)
{
	struct dahdi_timer *timer = data;
	int flags = -1;

	return ioctl(timer->fd, DAHDI_TIMERPONG, &flags) ? -1 : 0;
}

static enum ast_timer_event dahdi_timer_get_event(void *data)
{
	struct dahdi_timer *timer = data;
	int res;
	int event;

	res = ioctl(timer->fd, DAHDI_GETEVENT, &event);

	if (res) {
		event = DAHDI_EVENT_TIMER_EXPIRED;
	}

	switch (event) {
	case DAHDI_EVENT_TIMER_PING:
		return AST_TIMING_EVENT_CONTINUOUS;
	case DAHDI_EVENT_TIMER_EXPIRED:
	default:
		return AST_TIMING_EVENT_EXPIRED;	
	}
}

static unsigned int dahdi_timer_get_max_rate(void *data)
{
	return 1000;
}

static int dahdi_timer_fd(void *data)
{
	struct dahdi_timer *timer = data;

	return timer->fd;
}

#define SEE_TIMING "For more information on Asterisk timing modules, including ways to potentially fix this problem, please see https://wiki.asterisk.org/wiki/display/AST/Timing+Interfaces\n"

static int dahdi_test_timer(void)
{
	int fd;
	int x = 160;
	
	fd = open("/dev/dahdi/timer", O_RDWR);

	if (fd < 0) {
		return -1;
	}

	if (ioctl(fd, DAHDI_TIMERCONFIG, &x)) {
		ast_log(LOG_ERROR, "You have DAHDI built and drivers loaded, but the DAHDI timer test failed to set DAHDI_TIMERCONFIG to %d.\n" SEE_TIMING, x);
		close(fd);
		return -1;
	}

	if ((x = ast_wait_for_input(fd, 300)) < 0) {
		ast_log(LOG_ERROR, "You have DAHDI built and drivers loaded, but the DAHDI timer could not be polled during the DAHDI timer test.\n" SEE_TIMING);
		close(fd);
		return -1;
	}

	if (!x) {
		const char dahdi_timer_error[] = {
			"Asterisk has detected a problem with your DAHDI configuration and will shutdown for your protection.  You have options:"
			"\n\t1. You only have to compile DAHDI support into Asterisk if you need it.  One option is to recompile without DAHDI support."
			"\n\t2. You only have to load DAHDI drivers if you want to take advantage of DAHDI services.  One option is to unload DAHDI modules if you don't need them."
			"\n\t3. If you need DAHDI services, you must correctly configure DAHDI."
		};
		ast_log(LOG_ERROR, "%s\n" SEE_TIMING, dahdi_timer_error);
		usleep(100);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int load_module(void)
{
	if (dahdi_test_timer()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return !ast_register_timing_interface(&dahdi_timing) ?
		AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "DAHDI Timing Interface");
