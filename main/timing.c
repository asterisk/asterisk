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

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"

#include "asterisk/timing.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/time.h"
#include "asterisk/module.h"
#include "asterisk/poll-compat.h"
#include "asterisk/api_registry.h"


static int timing_interface_initialize(void *interface, struct ast_module *module)
{
	struct ast_timing_interface *funcs = interface;

	if (!funcs->timer_open
		|| !funcs->timer_close
		|| !funcs->timer_set_rate
		|| !funcs->timer_ack
		|| !funcs->timer_get_event
		|| !funcs->timer_get_max_rate
		|| !funcs->timer_enable_continuous
		|| !funcs->timer_disable_continuous
		|| !funcs->timer_fd) {
		return -1;
	}

	return 0;
}

static int timing_interface_cmp(struct ast_api_holder *h1, struct ast_api_holder *h2)
{
	struct ast_timing_interface *i1 = ast_api_get_interface(ast_timing_interface, h1);
	struct ast_timing_interface *i2 = ast_api_get_interface(ast_timing_interface, h2);

	if (i1->priority > i2->priority) {
		return 1;
	}

	if (i1->priority == i2->priority) {
		return 0;
	}

	return -1;
}

struct ast_api_registry ast_timing_interface = {
	.label = "Timing Interface",
	.initialize_interface = timing_interface_initialize,
	.holders_sort = timing_interface_cmp,
	.namecmp = strcasecmp,
};
AST_API_FN_REGISTER(ast_timing_interface, __ast_timing_interface)

struct ast_timer {
	void *data;
	AST_API_HOLDER(ast_timing_interface, interface);
};

static void timer_destructor(void *obj)
{
	struct ast_timer *t = obj;

	AST_API_HOLDER_CLEANUP(t->interface);
}

struct ast_timer *ast_timer_open(void)
{
	struct ast_timer *t = NULL;

	t = ao2_alloc(sizeof(*t), timer_destructor);
	if (!t) {
		ast_log(LOG_ERROR, "Failed to allocate timer\n");
		return NULL;
	}

	AST_API_HOLDER_SET(ast_timing_interface, t->interface,
		ast_api_registry_use_head(&ast_timing_interface));

	if (!t->interface) {
		ast_log(LOG_ERROR, "Failed to use timer module\n");
		goto returnerror;
	}

	t->data = t->interface->timer_open();
	if (!t->data) {
		ast_log(LOG_ERROR, "Failed to open timer\n");
		goto returnerror;
	}

	ast_log(LOG_NOTICE, "Opened timer provided by %s\n", t->interface->name);

	return t;

returnerror:
	ao2_ref(t, -1);
	return NULL;
}

void ast_timer_close(struct ast_timer *handle)
{
	handle->interface->timer_close(handle->data);
	ao2_ref(handle, -1);
}

int ast_timer_fd(const struct ast_timer *handle)
{
	return handle->interface->timer_fd(handle->data);
}

int ast_timer_set_rate(const struct ast_timer *handle, unsigned int rate)
{
	return handle->interface->timer_set_rate(handle->data, rate);
}

int ast_timer_ack(const struct ast_timer *handle, unsigned int quantity)
{
	return handle->interface->timer_ack(handle->data, quantity);
}

int ast_timer_enable_continuous(const struct ast_timer *handle)
{
	return handle->interface->timer_enable_continuous(handle->data);
}

int ast_timer_disable_continuous(const struct ast_timer *handle)
{
	return handle->interface->timer_disable_continuous(handle->data);
}

enum ast_timer_event ast_timer_get_event(const struct ast_timer *handle)
{
	return handle->interface->timer_get_event(handle->data);
}

unsigned int ast_timer_get_max_rate(const struct ast_timer *handle)
{
	return handle->interface->timer_get_max_rate(handle->data);
}

const char *ast_timer_get_name(const struct ast_timer *handle)
{
	return handle->interface->name;
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

	ast_cli(a->fd, "Using the '%s' timing module for this test.\n", timer->interface->name);

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
	ast_api_registry_cleanup(&ast_timing_interface);
}

int ast_timing_init(void)
{
	if (ast_api_registry_init(&ast_timing_interface, 2)) {
		return -1;
	}

	ast_register_cleanup(timing_shutdown);

	return ast_cli_register_multiple(cli_timing, ARRAY_LEN(cli_timing));
}
