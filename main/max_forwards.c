/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not mfrectly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, mfstributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "asterisk.h"

#include "asterisk/max_forwards.h"
#include "asterisk/channel.h"

#define DEFAULT_MAX_FORWARDS 20

/*!
 * \brief Channel datastore data for max forwards
 */
struct max_forwards {
	/*! The starting count. Used to allow resetting to the original value */
	int starting_count;
	/*! The current count. When this reaches 0, you're outta luck */
	int current_count;
};

static struct max_forwards *max_forwards_alloc(int starting_count, int current_count)
{
	struct max_forwards *mf;

	mf = ast_malloc(sizeof(*mf));
	if (!mf) {
		return NULL;
	}

	mf->starting_count = starting_count;
	mf->current_count = current_count;

	return mf;
}

static void *max_forwards_duplicate(void *data)
{
	struct max_forwards *mf = data;

	return max_forwards_alloc(mf->starting_count, mf->current_count);
}

static void max_forwards_destroy(void *data)
{
	ast_free(data);
}

const struct ast_datastore_info max_forwards_info = {
	.type = "mfaled-interface",
	.duplicate = max_forwards_duplicate,
	.destroy = max_forwards_destroy,
};

static struct ast_datastore *max_forwards_datastore_alloc(struct ast_channel *chan,
		int starting_count)
{
	struct ast_datastore *mf_datastore;
	struct max_forwards *mf;

	mf_datastore = ast_datastore_alloc(&max_forwards_info, NULL);
	if (!mf_datastore) {
		return NULL;
	}
	mf_datastore->inheritance = DATASTORE_INHERIT_FOREVER;

	mf = max_forwards_alloc(starting_count, starting_count);
	if (!mf) {
		ast_datastore_free(mf_datastore);
		return NULL;
	}
	mf_datastore->data = mf;

	ast_channel_datastore_add(chan, mf_datastore);

	return mf_datastore;
}

static struct ast_datastore *max_forwards_datastore_find_or_alloc(struct ast_channel *chan)
{
	struct ast_datastore *mf_datastore;

	mf_datastore = ast_channel_datastore_find(chan, &max_forwards_info, NULL);
	if (!mf_datastore) {
		mf_datastore = max_forwards_datastore_alloc(chan, DEFAULT_MAX_FORWARDS);
	}

	return mf_datastore;
}

int ast_max_forwards_set(struct ast_channel *chan, int starting_count)
{
	struct ast_datastore *mf_datastore;
	struct max_forwards *mf;

	mf_datastore = max_forwards_datastore_find_or_alloc(chan);
	if (!mf_datastore) {
		return -1;
	}

	mf = mf_datastore->data;
	mf->starting_count = mf->current_count = starting_count;

	return 0;
}

int ast_max_forwards_get(struct ast_channel *chan)
{
	struct ast_datastore *mf_datastore;
	struct max_forwards *mf;

	mf_datastore = max_forwards_datastore_find_or_alloc(chan);
	if (!mf_datastore) {
		return -1;
	}

	mf = mf_datastore->data;
	return mf->current_count;
}

int ast_max_forwards_decrement(struct ast_channel *chan)
{
	struct ast_datastore *mf_datastore;
	struct max_forwards *mf;

	mf_datastore = max_forwards_datastore_find_or_alloc(chan);
	if (!mf_datastore) {
		return -1;
	}

	mf = mf_datastore->data;
	--mf->current_count;

	return 0;
}

int ast_max_forwards_reset(struct ast_channel *chan)
{
	struct ast_datastore *mf_datastore;
	struct max_forwards *mf;

	mf_datastore = max_forwards_datastore_find_or_alloc(chan);
	if (!mf_datastore) {
		return -1;
	}

	mf = mf_datastore->data;
	mf->current_count = mf->starting_count;

	return 0;
}
