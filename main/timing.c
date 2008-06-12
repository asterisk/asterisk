/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/timing.h"
#include "asterisk/lock.h"

AST_MUTEX_DEFINE_STATIC(lock);

static struct ast_timing_functions timer_funcs;

void *ast_install_timing_functions(struct ast_timing_functions *funcs)
{
	if (!funcs->timer_open ||
	    !funcs->timer_close ||
	    !funcs->timer_ack ||
	    !funcs->timer_get_event ||
	    !funcs->timer_enable_continuous ||
	    !funcs->timer_disable_continuous) {
		return NULL;
	}

	ast_mutex_lock(&lock);

	if (timer_funcs.timer_open) {
		ast_mutex_unlock(&lock);
		return NULL;
	}
	
	timer_funcs = *funcs;

	ast_mutex_unlock(&lock);

	return &timer_funcs;
}

void ast_uninstall_timing_functions(void *handle)
{
	ast_mutex_lock(&lock);

	if (handle != &timer_funcs) {
		ast_mutex_unlock(&lock);
		return;
	}

	memset(&timer_funcs, 0, sizeof(timer_funcs));

	ast_mutex_unlock(&lock);
}

int ast_timer_open(unsigned int rate)
{
	int timer;

	ast_mutex_lock(&lock);

	if (!timer_funcs.timer_open) {
		ast_mutex_unlock(&lock);
		return -1;
	}

	timer = timer_funcs.timer_open(rate);

	ast_mutex_unlock(&lock);

	return timer;
}

void ast_timer_close(int timer)
{
	ast_mutex_lock(&lock);

	if (!timer_funcs.timer_close) {
		ast_mutex_unlock(&lock);
		return;
	}

	timer_funcs.timer_close(timer);

	ast_mutex_unlock(&lock);
}

void ast_timer_ack(int handle, unsigned int quantity)
{
	ast_mutex_lock(&lock);

	if (!timer_funcs.timer_ack) {
		ast_mutex_unlock(&lock);
		return;
	}

	timer_funcs.timer_ack(handle, quantity);

	ast_mutex_unlock(&lock);
}

int ast_timer_enable_continuous(int handle)
{
	int result;

	ast_mutex_lock(&lock);

	if (!timer_funcs.timer_enable_continuous) {
		ast_mutex_unlock(&lock);
		return -1;
	}

	result = timer_funcs.timer_enable_continuous(handle);

	ast_mutex_unlock(&lock);

	return result;
}

int ast_timer_disable_continous(int handle)
{
	int result;

	ast_mutex_lock(&lock);

	if (!timer_funcs.timer_disable_continuous) {
		ast_mutex_unlock(&lock);
		return -1;
	}

	result = timer_funcs.timer_disable_continuous(handle);

	ast_mutex_unlock(&lock);

	return result;
}

enum ast_timing_event ast_timer_get_event(int handle)
{
	enum ast_timing_event result;

	ast_mutex_lock(&lock);

	if (!timer_funcs.timer_get_event) {
		ast_mutex_unlock(&lock);
		return -1;
	}

	result = timer_funcs.timer_get_event(handle);

	ast_mutex_unlock(&lock);

	return result;
}
