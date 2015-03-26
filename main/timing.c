/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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

/*! \file
 *
 * \brief Timing source management
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include "asterisk/timing.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/time.h"
#include "asterisk/heap.h"
#include "asterisk/module.h"
#include "asterisk/poll-compat.h"

struct timing_holder {
	/*! Do _not_ move this from the beginning of the struct. */
	ssize_t __heap_index;
	struct ast_module *mod;
	struct ast_timing_interface *iface;
};

static struct ast_heap *timing_interfaces;

struct ast_timer {
	int fd;
	struct timing_holder *holder;
};

static int timing_holder_cmp(void *_h1, void *_h2)
{
	struct timing_holder *h1 = _h1;
	struct timing_holder *h2 = _h2;

	if (h1->iface->priority > h2->iface->priority) {
		return 1;
	} else if (h1->iface->priority == h2->iface->priority) {
		return 0;
	} else {
		return -1;
	}
}

void *_ast_register_timing_interface(struct ast_timing_interface *funcs,
				     struct ast_module *mod)
{
	struct timing_holder *h;

	if (!funcs->timer_open ||
	    !funcs->timer_close ||
	    !funcs->timer_set_rate ||
	    !funcs->timer_ack ||
	    !funcs->timer_get_event ||
	    !funcs->timer_get_max_rate ||
	    !funcs->timer_enable_continuous ||
	    !funcs->timer_disable_continuous) {
		return NULL;
	}

	if (!(h = ast_calloc(1, sizeof(*h)))) {
		return NULL;
	}

	h->iface = funcs;
	h->mod = mod;

	ast_heap_wrlock(timing_interfaces);
	ast_heap_push(timing_interfaces, h);
	ast_heap_unlock(timing_interfaces);

	return h;
}

int ast_unregister_timing_interface(void *handle)
{
	struct timing_holder *h = handle;
	int res = -1;

	ast_heap_wrlock(timing_interfaces);
	h = ast_heap_remove(timing_interfaces, h);
	ast_heap_unlock(timing_interfaces);

	if (h) {
		ast_free(h);
		h = NULL;
		res = 0;
	}

	return res;
}

struct ast_timer *ast_timer_open(void)
{
	int fd = -1;
	struct timing_holder *h;
	struct ast_timer *t = NULL;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		fd = h->iface->timer_open();
		ast_module_ref(h->mod);
	}

	if (fd != -1) {
		if (!(t = ast_calloc(1, sizeof(*t)))) {
			h->iface->timer_close(fd);
		} else {
			t->fd = fd;
			t->holder = h;
		}
	}

	ast_heap_unlock(timing_interfaces);

	return t;
}

void ast_timer_close(struct ast_timer *handle)
{
	handle->holder->iface->timer_close(handle->fd);
	handle->fd = -1;
	ast_module_unref(handle->holder->mod);
	ast_free(handle);
}

int ast_timer_fd(const struct ast_timer *handle)
{
	return handle->fd;
}

int ast_timer_set_rate(const struct ast_timer *handle, unsigned int rate)
{
	int res = -1;

	res = handle->holder->iface->timer_set_rate(handle->fd, rate);

	return res;
}

int ast_timer_ack(const struct ast_timer *handle, unsigned int quantity)
{
	int res = -1;

	res = handle->holder->iface->timer_ack(handle->fd, quantity);

	return res;
}

int ast_timer_enable_continuous(const struct ast_timer *handle)
{
	int res = -1;

	res = handle->holder->iface->timer_enable_continuous(handle->fd);

	return res;
}

int ast_timer_disable_continuous(const struct ast_timer *handle)
{
	int res = -1;

	res = handle->holder->iface->timer_disable_continuous(handle->fd);

	return res;
}

enum ast_timer_event ast_timer_get_event(const struct ast_timer *handle)
{
	enum ast_timer_event res = -1;

	res = handle->holder->iface->timer_get_event(handle->fd);

	return res;
}

unsigned int ast_timer_get_max_rate(const struct ast_timer *handle)
{
	unsigned int res = 0;

	res = handle->holder->iface->timer_get_max_rate(handle->fd);

	return res;
}

const char *ast_timer_get_name(const struct ast_timer *handle)
{
	return handle->holder->iface->name;
}

static char *timing_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_timer *timer;
	int count = 0;
	struct timeval start, end;
	unsigned int test_rate = 50;

	switch (cmd) {
	case CLI_INIT:
		e->command = "timing test";
		e->usage = "Usage: timing test <rate>\n"
		           "   Test a timer with a specified rate, 50/sec by default.\n"
		           "";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2 && a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 3) {
		unsigned int rate;
		if (sscanf(a->argv[2], "%30u", &rate) == 1) {
			test_rate = rate;
		} else {
			ast_cli(a->fd, "Invalid rate '%s', using default of %u\n", a->argv[2], test_rate);
		}
	}

	ast_cli(a->fd, "Attempting to test a timer with %u ticks per second.\n", test_rate);

	if (!(timer = ast_timer_open())) {
		ast_cli(a->fd, "Failed to open timing fd\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Using the '%s' timing module for this test.\n", timer->holder->iface->name);

	start = ast_tvnow();

	ast_timer_set_rate(timer, test_rate);

	while (ast_tvdiff_ms((end = ast_tvnow()), start) < 1000) {
		int res;
		struct pollfd pfd = {
			.fd = ast_timer_fd(timer),
			.events = POLLIN | POLLPRI,
		};

		res = ast_poll(&pfd, 1, 100);

		if (res == 1) {
			count++;
			if (ast_timer_ack(timer, 1) < 0) {
				ast_cli(a->fd, "Timer failed to acknowledge.\n");
				ast_timer_close(timer);
				return CLI_FAILURE;
			}
		} else if (!res) {
			ast_cli(a->fd, "poll() timed out!  This is bad.\n");
		} else if (errno != EAGAIN && errno != EINTR) {
			ast_cli(a->fd, "poll() returned error: %s\n", strerror(errno));
		}
	}

	ast_timer_close(timer);
	timer = NULL;

	ast_cli(a->fd, "It has been %" PRIi64 " milliseconds, and we got %d timer ticks\n",
		ast_tvdiff_ms(end, start), count);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_timing[] = {
	AST_CLI_DEFINE(timing_test, "Run a timing test"),
};

static void timing_shutdown(void)
{
	ast_cli_unregister_multiple(cli_timing, ARRAY_LEN(cli_timing));

	ast_heap_destroy(timing_interfaces);
	timing_interfaces = NULL;
}

int ast_timing_init(void)
{
	if (!(timing_interfaces = ast_heap_create(2, timing_holder_cmp, 0))) {
		return -1;
	}

	ast_register_cleanup(timing_shutdown);

	return ast_cli_register_multiple(cli_timing, ARRAY_LEN(cli_timing));
}
