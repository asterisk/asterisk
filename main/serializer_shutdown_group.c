/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012-2013, Digium, Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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

#include "asterisk/serializer_shutdown_group.h"
#include "asterisk/astobj2.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

/*! Serializer group shutdown control object. */
struct ast_serializer_shutdown_group {
	/*! Shutdown thread waits on this conditional. */
	ast_cond_t cond;
	/*! Count of serializers needing to shutdown. */
	int count;
};

static void serializer_shutdown_group_dtor(void *vdoomed)
{
	struct ast_serializer_shutdown_group *doomed = vdoomed;

	ast_cond_destroy(&doomed->cond);
}

struct ast_serializer_shutdown_group *ast_serializer_shutdown_group_alloc(void)
{
	struct ast_serializer_shutdown_group *shutdown_group;

	shutdown_group = ao2_alloc(sizeof(*shutdown_group), serializer_shutdown_group_dtor);
	if (!shutdown_group) {
		return NULL;
	}
	ast_cond_init(&shutdown_group->cond, NULL);
	return shutdown_group;
}

int ast_serializer_shutdown_group_join(struct ast_serializer_shutdown_group *shutdown_group, int timeout)
{
	int remaining;
	ast_mutex_t *lock;

	if (!shutdown_group) {
		return 0;
	}

	lock = ao2_object_get_lockaddr(shutdown_group);
	ast_assert(lock != NULL);

	ao2_lock(shutdown_group);
	if (timeout) {
		struct timeval start;
		struct timespec end;

		start = ast_tvnow();
		end.tv_sec = start.tv_sec + timeout;
		end.tv_nsec = start.tv_usec * 1000;
		while (shutdown_group->count) {
			if (ast_cond_timedwait(&shutdown_group->cond, lock, &end)) {
				/* Error or timed out waiting for the count to reach zero. */
				break;
			}
		}
	} else {
		while (shutdown_group->count) {
			if (ast_cond_wait(&shutdown_group->cond, lock)) {
				/* Error */
				break;
			}
		}
	}
	remaining = shutdown_group->count;
	ao2_unlock(shutdown_group);
	return remaining;
}

void ast_serializer_shutdown_group_inc(struct ast_serializer_shutdown_group *shutdown_group)
{
	ao2_lock(shutdown_group);
	++shutdown_group->count;
	ao2_unlock(shutdown_group);
}

void ast_serializer_shutdown_group_dec(struct ast_serializer_shutdown_group *shutdown_group)
{
	ao2_lock(shutdown_group);
	--shutdown_group->count;
	if (!shutdown_group->count) {
		ast_cond_signal(&shutdown_group->cond);
	}
	ao2_unlock(shutdown_group);
}
