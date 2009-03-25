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

int ast_timer_open(void)
{
	int fd = -1;
	struct timing_holder *h;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		fd = h->iface->timer_open();
		ast_module_ref(h->mod);
	}

	ast_heap_unlock(timing_interfaces);

	return fd;
}

void ast_timer_close(int timer)
{
	struct timing_holder *h;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		h->iface->timer_close(timer);
		ast_module_unref(h->mod);
	}

	ast_heap_unlock(timing_interfaces);
}

int ast_timer_set_rate(int handle, unsigned int rate)
{
	struct timing_holder *h;
	int res = -1;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		res = h->iface->timer_set_rate(handle, rate);
	}

	ast_heap_unlock(timing_interfaces);

	return res;
}

void ast_timer_ack(int handle, unsigned int quantity)
{
	struct timing_holder *h;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		h->iface->timer_ack(handle, quantity);
	}

	ast_heap_unlock(timing_interfaces);
}

int ast_timer_enable_continuous(int handle)
{
	struct timing_holder *h;
	int res = -1;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		res = h->iface->timer_enable_continuous(handle);
	}

	ast_heap_unlock(timing_interfaces);

	return res;
}

int ast_timer_disable_continuous(int handle)
{
	struct timing_holder *h;
	int res = -1;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		res = h->iface->timer_disable_continuous(handle);
	}

	ast_heap_unlock(timing_interfaces);

	return res;
}

enum ast_timer_event ast_timer_get_event(int handle)
{
	struct timing_holder *h;
	enum ast_timer_event res = -1;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		res = h->iface->timer_get_event(handle);
	}

	ast_heap_unlock(timing_interfaces);

	return res;
}

unsigned int ast_timer_get_max_rate(int handle)
{
	struct timing_holder *h;
	unsigned int res = 0;

	ast_heap_rdlock(timing_interfaces);

	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		res = h->iface->timer_get_max_rate(handle);
	}

	ast_heap_unlock(timing_interfaces);

	return res;
}

static char *timing_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int fd, count = 0;
	struct timeval start, end;
	unsigned int test_rate = 50;
	struct timing_holder *h;

	switch (cmd) {
	case CLI_INIT:
		e->command = "timing test";
		e->usage = "Usage: timing test <rate>\n"
		           "   Test a timer with a specified rate, 100/sec by default.\n"
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
		if (sscanf(a->argv[2], "%u", &rate) == 1) {
			test_rate = rate;
		} else {
			ast_cli(a->fd, "Invalid rate '%s', using default of %u\n", a->argv[2], test_rate);	
		}
	}

	ast_cli(a->fd, "Attempting to test a timer with %u ticks per second.\n", test_rate);

	if ((fd = ast_timer_open()) == -1) {
		ast_cli(a->fd, "Failed to open timing fd\n");
		return CLI_FAILURE;
	}

	ast_heap_rdlock(timing_interfaces);
	if ((h = ast_heap_peek(timing_interfaces, 1))) {
		ast_cli(a->fd, "Using the '%s' timing module for this test.\n", h->iface->name);
		h = NULL;
	}
	ast_heap_unlock(timing_interfaces);

	start = ast_tvnow();

	ast_timer_set_rate(fd, test_rate);

	while (ast_tvdiff_ms((end = ast_tvnow()), start) < 1000) {
		int res;
		struct pollfd pfd = {
			.fd = fd,
			.events = POLLIN | POLLPRI,
		};

		res = ast_poll(&pfd, 1, 100);

		if (res == 1) {
			count++;
			ast_timer_ack(fd, 1);
		} else if (!res) {
			ast_cli(a->fd, "poll() timed out!  This is bad.\n");
		} else if (errno != EAGAIN && errno != EINTR) {
			ast_cli(a->fd, "poll() returned error: %s\n", strerror(errno));
		}
	}

	ast_timer_close(fd);

	ast_cli(a->fd, "It has been %d milliseconds, and we got %d timer ticks\n", 
		ast_tvdiff_ms(end, start), count);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_timing[] = {
	AST_CLI_DEFINE(timing_test, "Run a timing test"),
};

int ast_timing_init(void)
{
	if (!(timing_interfaces = ast_heap_create(2, timing_holder_cmp, 0))) {
		return -1;
	}

	return ast_cli_register_multiple(cli_timing, ARRAY_LEN(cli_timing));
}
