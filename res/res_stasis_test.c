/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \file \brief Test infrastructure for dealing with Stasis.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__);

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/stasis_test.h"

STASIS_MESSAGE_TYPE_DEFN(stasis_test_message_type);

static void stasis_message_sink_dtor(void *obj)
{
	struct stasis_message_sink *sink = obj;

	{
		SCOPED_MUTEX(lock, &sink->lock);
		while (!sink->is_done) {
			/* Normally waiting forever is bad, but if we're not
			 * done, we're not done. */
			ast_cond_wait(&sink->cond, &sink->lock);
		}
	}

	ast_mutex_destroy(&sink->lock);
	ast_cond_destroy(&sink->cond);

	while (sink->num_messages > 0) {
		ao2_cleanup(sink->messages[--sink->num_messages]);
	}
	ast_free(sink->messages);
	sink->messages = NULL;
	sink->max_messages = 0;
}

static struct timespec make_deadline(int timeout_millis)
{
	struct timeval start = ast_tvnow();
	struct timeval delta = {
		.tv_sec = timeout_millis / 1000,
		.tv_usec = (timeout_millis % 1000) * 1000,
	};
	struct timeval deadline_tv = ast_tvadd(start, delta);
	struct timespec deadline = {
		.tv_sec = deadline_tv.tv_sec,
		.tv_nsec = 1000 * deadline_tv.tv_usec,
	};

	return deadline;
}

struct stasis_message_sink *stasis_message_sink_create(void)
{
	RAII_VAR(struct stasis_message_sink *, sink, NULL, ao2_cleanup);

	sink = ao2_alloc(sizeof(*sink), stasis_message_sink_dtor);
	if (!sink) {
		return NULL;
	}
	ast_mutex_init(&sink->lock);
	ast_cond_init(&sink->cond, NULL);
	sink->max_messages = 4;
	sink->messages =
		ast_malloc(sizeof(*sink->messages) * sink->max_messages);
	if (!sink->messages) {
		return NULL;
	}
	ao2_ref(sink, +1);
	return sink;
}

/*!
 * \brief Implementation of the stasis_message_sink_cb() callback.
 *
 * Why the roundabout way of exposing this via stasis_message_sink_cb()? Well,
 * it has to do with how we load modules.
 *
 * Modules have their own metadata compiled into them in the module info block
 * at the end of the file.  This includes dependency information in the
 * \c nonoptreq field.
 *
 * Asterisk loads the module, inspects the field, then loads any needed
 * dependencies. This works because Asterisk passes \c RTLD_LAZY to the initial
 * dlopen(), which defers binding function references until they are called.
 *
 * But when you take the address of a function, that function needs to be
 * available at load time. So if some module used the address of
 * message_sink_cb() directly, and \c res_stasis_test.so wasn't loaded yet, then
 * that module would fail to load.
 *
 * The stasis_message_sink_cb() function gives us a layer of indirection so that
 * the initial lazy binding will still work as expected.
 */
static void message_sink_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct stasis_message_sink *sink = data;

	SCOPED_MUTEX(lock, &sink->lock);

	if (stasis_subscription_final_message(sub, message)) {
		sink->is_done = 1;
		ast_cond_signal(&sink->cond);
		return;
	}

	if (stasis_subscription_change_type() == stasis_message_type(message)) {
		/* Ignore subscription changes */
		return;
	}

	if (sink->num_messages == sink->max_messages) {
		size_t new_max_messages = sink->max_messages * 2;
		struct stasis_message **new_messages = ast_realloc(
			sink->messages,
			sizeof(*new_messages) * new_max_messages);
		if (!new_messages) {
			return;
		}
		sink->max_messages = new_max_messages;
		sink->messages = new_messages;
	}

	ao2_ref(message, +1);
	sink->messages[sink->num_messages++] = message;
	ast_cond_signal(&sink->cond);
}

stasis_subscription_cb stasis_message_sink_cb(void)
{
	return message_sink_cb;
}


int stasis_message_sink_wait_for_count(struct stasis_message_sink *sink,
	int num_messages, int timeout_millis)
{
	struct timespec deadline = make_deadline(timeout_millis);

	SCOPED_MUTEX(lock, &sink->lock);
	while (sink->num_messages < num_messages) {
		int r = ast_cond_timedwait(&sink->cond, &sink->lock, &deadline);

		if (r == ETIMEDOUT) {
			break;
		}
		if (r != 0) {
			ast_log(LOG_ERROR, "Unexpected condition error: %s\n",
				strerror(r));
			break;
		}
	}
	return sink->num_messages;
}

int stasis_message_sink_should_stay(struct stasis_message_sink *sink,
	int num_messages, int timeout_millis)
{
	struct timespec deadline = make_deadline(timeout_millis);

	SCOPED_MUTEX(lock, &sink->lock);
	while (sink->num_messages == num_messages) {
		int r = ast_cond_timedwait(&sink->cond, &sink->lock, &deadline);

		if (r == ETIMEDOUT) {
			break;
		}
		if (r != 0) {
			ast_log(LOG_ERROR, "Unexpected condition error: %s\n",
				strerror(r));
			break;
		}
	}
	return sink->num_messages;
}

int stasis_message_sink_wait_for(struct stasis_message_sink *sink, int start,
	stasis_wait_cb cmp_cb, const void *data, int timeout_millis)
{
	struct timespec deadline = make_deadline(timeout_millis);

	SCOPED_MUTEX(lock, &sink->lock);

	/* wait for the start */
	while (sink->num_messages < start + 1) {
		int r = ast_cond_timedwait(&sink->cond, &sink->lock, &deadline);

		if (r == ETIMEDOUT) {
			/* Timed out waiting for the start */
			return -1;
		}
		if (r != 0) {
			ast_log(LOG_ERROR, "Unexpected condition error: %s\n",
				strerror(r));
			return -2;
		}
	}


	while (!cmp_cb(sink->messages[start], data)) {
		++start;

		while (sink->num_messages < start + 1) {
			int r = ast_cond_timedwait(&sink->cond,
				&sink->lock, &deadline);

			if (r == ETIMEDOUT) {
				return -1;
			}
			if (r != 0) {
				ast_log(LOG_ERROR,
					"Unexpected condition error: %s\n",
					strerror(r));
				return -2;
			}
		}
	}

	return start;
}

struct stasis_message *stasis_test_message_create(void)
{
	RAII_VAR(void *, data, NULL, ao2_cleanup);

	if (!stasis_test_message_type()) {
		return NULL;
	}

	/* We just need the unique pointer; don't care what's in it */
	data = ao2_alloc(1, NULL);
	if (!data) {
		return NULL;
	}

	return stasis_message_create(stasis_test_message_type(), data);
}

static int unload_module(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(stasis_test_message_type);
	return 0;
}

static int load_module(void)
{
	if (STASIS_MESSAGE_TYPE_INIT(stasis_test_message_type) != 0) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Stasis test utilities",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	);
