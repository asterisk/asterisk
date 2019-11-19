/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/serializer.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadpool.h"
#include "asterisk/utils.h"
#include "asterisk/vector.h"

struct ast_serializer_pool {
	/*! Shutdown group to monitor serializers. */
	struct ast_serializer_shutdown_group *shutdown_group;
	/*! Time to wait if using a shutdown group. */
	int shutdown_group_timeout;
	/*! A pool of taskprocessor(s) */
	AST_VECTOR_RW(, struct ast_taskprocessor *) serializers;
	/*! Base name for the pool */
	char name[];
};

int ast_serializer_pool_destroy(struct ast_serializer_pool *pool)
{
	if (!pool) {
		return 0;
	}

	/* Clear out the serializers */
	AST_VECTOR_RW_WRLOCK(&pool->serializers);
	AST_VECTOR_RESET(&pool->serializers, ast_taskprocessor_unreference);
	AST_VECTOR_RW_UNLOCK(&pool->serializers);

	/* If using a shutdown group then wait for all threads to complete */
	if (pool->shutdown_group) {
		int remaining;

		ast_debug(3, "Waiting on serializers before destroying pool '%s'\n", pool->name);

		remaining = ast_serializer_shutdown_group_join(
			pool->shutdown_group, pool->shutdown_group_timeout);

		if (remaining) {
			/* If we've timed out don't fully cleanup yet */
			ast_log(LOG_WARNING, "'%s' serializer pool destruction timeout. "
				"'%d' dependencies still processing.\n", pool->name, remaining);
			return remaining;
		}

		ao2_ref(pool->shutdown_group, -1);
		pool->shutdown_group = NULL;
	}

	AST_VECTOR_RW_FREE(&pool->serializers);
	ast_free(pool);

	return 0;
}

struct ast_serializer_pool *ast_serializer_pool_create(const char *name,
	unsigned int size, struct ast_threadpool *threadpool, int timeout)
{
	struct ast_serializer_pool *pool;
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];
	size_t idx;

	ast_assert(size > 0);

	pool = ast_malloc(sizeof(*pool) + strlen(name) + 1);
	if (!pool) {
		return NULL;
	}

	strcpy(pool->name, name); /* safe */

	pool->shutdown_group_timeout = timeout;
	pool->shutdown_group = timeout > -1 ? ast_serializer_shutdown_group_alloc() : NULL;

	AST_VECTOR_RW_INIT(&pool->serializers, size);

	for (idx = 0; idx < size; ++idx) {
		struct ast_taskprocessor *tps;

		/* Create name with seq number appended. */
		ast_taskprocessor_name_append(tps_name, sizeof(tps_name), name);

		tps = ast_threadpool_serializer_group(tps_name, threadpool, pool->shutdown_group);
		if (!tps) {
			ast_serializer_pool_destroy(pool);
			ast_log(LOG_ERROR, "Pool create: unable to create named serializer '%s'\n",
					tps_name);
			return NULL;
		}

		if (AST_VECTOR_APPEND(&pool->serializers, tps)) {
			ast_serializer_pool_destroy(pool);
			ast_log(LOG_ERROR, "Pool create: unable to append named serializer '%s'\n",
					tps_name);
			return NULL;
		}
	}

	return pool;
}

const char *ast_serializer_pool_name(const struct ast_serializer_pool *pool)
{
	return pool->name;
}

struct ast_taskprocessor *ast_serializer_pool_get(struct ast_serializer_pool *pool)
{
	struct ast_taskprocessor *res;
	size_t idx;

	if (!pool) {
		return NULL;
	}

	AST_VECTOR_RW_RDLOCK(&pool->serializers);
	if (AST_VECTOR_SIZE(&pool->serializers) == 0) {
		AST_VECTOR_RW_UNLOCK(&pool->serializers);
		return NULL;
	}

	res = AST_VECTOR_GET(&pool->serializers, 0);

	/* Choose the taskprocessor with the smallest queue */
	for (idx = 1; idx < AST_VECTOR_SIZE(&pool->serializers); ++idx) {
		struct ast_taskprocessor *cur = AST_VECTOR_GET(&pool->serializers, idx);
		if (ast_taskprocessor_size(cur) < ast_taskprocessor_size(res)) {
			res = cur;
		}
	}

	AST_VECTOR_RW_UNLOCK(&pool->serializers);
	return res;
}

int ast_serializer_pool_set_alerts(struct ast_serializer_pool *pool, long high, long low)
{
	size_t idx;
	long tps_queue_high;
	long tps_queue_low;

	if (!pool) {
		return 0;
	}

	tps_queue_high = high;
	if (tps_queue_high <= 0) {
		ast_log(AST_LOG_WARNING, "Invalid '%s-*' taskprocessor high water alert "
				"trigger level '%ld'\n", pool->name, tps_queue_high);
		tps_queue_high = AST_TASKPROCESSOR_HIGH_WATER_LEVEL;
	}

	tps_queue_low = low;
	if (tps_queue_low < -1 || tps_queue_high < tps_queue_low) {
		ast_log(AST_LOG_WARNING, "Invalid '%s-*' taskprocessor low water clear alert "
				"level '%ld'\n", pool->name, tps_queue_low);
		tps_queue_low = -1;
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&pool->serializers); ++idx) {
		struct ast_taskprocessor *cur = AST_VECTOR_GET(&pool->serializers, idx);
		if (ast_taskprocessor_alert_set_levels(cur, tps_queue_low, tps_queue_high)) {
			ast_log(AST_LOG_WARNING, "Failed to set alert levels for serializer '%s'.\n",
					ast_taskprocessor_name(cur));
		}
	}

	return 0;
}
