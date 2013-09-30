/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Wait support for Stasis topics.
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"

static struct stasis_message_type *cache_guarantee_type(void);
STASIS_MESSAGE_TYPE_DEFN(cache_guarantee_type);

/*! \internal */
struct caching_guarantee {
	ast_mutex_t lock;
	ast_cond_t cond;
	unsigned int done:1;
};

static void caching_guarantee_dtor(void *obj)
{
	struct caching_guarantee *guarantee = obj;

	ast_assert(guarantee->done == 1);

	ast_mutex_destroy(&guarantee->lock);
	ast_cond_destroy(&guarantee->cond);
}

static void guarantee_handler(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	/* Wait for our particular message */
	if (data == message) {
		struct caching_guarantee *guarantee;
		ast_assert(cache_guarantee_type() == stasis_message_type(message));
		guarantee = stasis_message_data(message);

		ast_mutex_lock(&guarantee->lock);
		guarantee->done = 1;
		ast_cond_signal(&guarantee->cond);
		ast_mutex_unlock(&guarantee->lock);
	}
}

static struct stasis_message *caching_guarantee_create(void)
{
	RAII_VAR(struct caching_guarantee *, guarantee, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!(guarantee = ao2_alloc(sizeof(*guarantee), caching_guarantee_dtor))) {
		return NULL;
	}

	ast_mutex_init(&guarantee->lock);
	ast_cond_init(&guarantee->cond, NULL);

	if (!(msg = stasis_message_create(cache_guarantee_type(), guarantee))) {
		return NULL;
	}

	ao2_ref(msg, +1);
	return msg;
}

int stasis_topic_wait(struct stasis_topic *topic)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_subscription *, sub, NULL, stasis_unsubscribe);
	struct caching_guarantee *guarantee;

	msg = caching_guarantee_create();
	if (!msg) {
		return -1;
	}

	sub = stasis_subscribe(topic, guarantee_handler, msg);
	if (!sub) {
		return -1;
	}

	guarantee = stasis_message_data(msg);

	ast_mutex_lock(&guarantee->lock);
	stasis_publish(topic, msg);
	while (!guarantee->done) {
		ast_cond_wait(&guarantee->cond, &guarantee->lock);
	}
	ast_mutex_unlock(&guarantee->lock);
	return 0;
}

static void wait_cleanup(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(cache_guarantee_type);
}

int stasis_wait_init(void)
{
	ast_register_cleanup(wait_cleanup);

	if (STASIS_MESSAGE_TYPE_INIT(cache_guarantee_type) != 0) {
		return -1;
	}
	return 0;
}
