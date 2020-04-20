/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2018, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * Ben Ford <bford@digium.com>
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
 * \brief Data Buffer API
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Ben Ford <bford@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/strings.h"
#include "asterisk/data_buffer.h"
#include "asterisk/linkedlists.h"

/*!
 * \brief The number of payloads to increment the cache by
 */
#define CACHED_PAYLOAD_MAX 5

/*!
 * \brief Payload entry placed inside of the data buffer list
 */
struct data_buffer_payload_entry {
	/*! \brief The payload for this position */
	void *payload;
	/*! \brief The provided position for this */
	size_t pos;
	/*! \brief Linked list information */
	AST_LIST_ENTRY(data_buffer_payload_entry) list;
};

/*!
 * \brief Data buffer containing fixed number of data payloads
 */
struct ast_data_buffer {
	/*! \brief Callback function to free a data payload */
	ast_data_buffer_free_callback free_fn;
	/*! \brief A linked list of data payloads */
	AST_LIST_HEAD_NOLOCK(, data_buffer_payload_entry) payloads;
	/*! \brief A linked list of unused cached data payloads */
	AST_LIST_HEAD_NOLOCK(, data_buffer_payload_entry) cached_payloads;
	/*! \brief The current number of data payloads in the buffer */
	size_t count;
	/*! \brief Maximum number of data payloads in the buffer */
	size_t max;
	/*! \brief The current number of data payloads in the cache */
	size_t cache_count;
};

static void free_fn_do_nothing(void *data)
{
	return;
}

/*!
 * \brief Helper function to allocate a data payload
 */
static struct data_buffer_payload_entry *data_buffer_payload_alloc(void *payload, size_t pos)
{
	struct data_buffer_payload_entry *data_payload;

	data_payload = ast_calloc(1, sizeof(*data_payload));
	if (!data_payload) {
		return NULL;
	}

	data_payload->payload = payload;
	data_payload->pos = pos;

	return data_payload;
}

/*!
 * \brief Helper function that sets the cache to its maximum number of payloads
 */
static void ast_data_buffer_cache_adjust(struct ast_data_buffer *buffer)
{
	int buffer_space;

	ast_assert(buffer != NULL);

	buffer_space = buffer->max - buffer->count;

	if (buffer->cache_count == buffer_space) {
		return;
	}

	if (buffer->cache_count < buffer_space) {
		/* Add payloads to the cache, if able */
		while (buffer->cache_count < CACHED_PAYLOAD_MAX && buffer->cache_count < buffer_space) {
			struct data_buffer_payload_entry *buffer_payload;

			buffer_payload = data_buffer_payload_alloc(NULL, -1);
			if (buffer_payload) {
				AST_LIST_INSERT_TAIL(&buffer->cached_payloads, buffer_payload, list);
				buffer->cache_count++;
				continue;
			}

			ast_log(LOG_ERROR, "Failed to allocate memory to the cache.");
			break;
		}
	} else if (buffer->cache_count > buffer_space) {
		/* Remove payloads from the cache */
		while (buffer->cache_count > buffer_space) {
			struct data_buffer_payload_entry *buffer_payload;

			buffer_payload = AST_LIST_REMOVE_HEAD(&buffer->cached_payloads, list);
			if (buffer_payload) {
				ast_free(buffer_payload);
				buffer->cache_count--;
				continue;
			}

			ast_log(LOG_ERROR, "Failed to remove memory from the cache.");
			break;
		}
	}
}

struct ast_data_buffer *ast_data_buffer_alloc(ast_data_buffer_free_callback free_fn, size_t size)
{
	struct ast_data_buffer *buffer;

	ast_assert(size != 0);

	buffer = ast_calloc(1, sizeof(*buffer));
	if (!buffer) {
		return NULL;
	}

	AST_LIST_HEAD_INIT_NOLOCK(&buffer->payloads);
	AST_LIST_HEAD_INIT_NOLOCK(&buffer->cached_payloads);

	/* If free_fn is NULL, just use free_fn_do_nothing as a default */
	buffer->free_fn = free_fn ? free_fn : free_fn_do_nothing;
	buffer->max = size;

	ast_data_buffer_cache_adjust(buffer);

	return buffer;
}

void ast_data_buffer_resize(struct ast_data_buffer *buffer, size_t size)
{
	struct data_buffer_payload_entry *existing_payload;

	ast_assert(buffer != NULL);

	/* The buffer must have at least a size of 1 */
	ast_assert(size > 0);

	if (buffer->max == size) {
		return;
	}

	/* If the size is decreasing, some payloads will need to be freed */
	if (buffer->max > size) {
		int remove = buffer->max - size;

		AST_LIST_TRAVERSE_SAFE_BEGIN(&buffer->payloads, existing_payload, list) {
			if (remove) {
				AST_LIST_REMOVE_HEAD(&buffer->payloads, list);
				buffer->free_fn(existing_payload->payload);
				ast_free(existing_payload);
				buffer->count--;
				remove--;
				continue;
			}
			break;
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	buffer->max = size;
	ast_data_buffer_cache_adjust(buffer);
}

int ast_data_buffer_put(struct ast_data_buffer *buffer, size_t pos, void *payload)
{
	struct data_buffer_payload_entry *buffer_payload = NULL;
	struct data_buffer_payload_entry *existing_payload;
	int inserted = 0;

	ast_assert(buffer != NULL);
	ast_assert(payload != NULL);

	/* If the data buffer has reached its maximum size then the head goes away and
	 * we will reuse its buffer payload
	 */
	if (buffer->count == buffer->max) {
		buffer_payload = AST_LIST_REMOVE_HEAD(&buffer->payloads, list);
		buffer->free_fn(buffer_payload->payload);
		buffer->count--;

		/* Update this buffer payload with its new information */
		buffer_payload->payload = payload;
		buffer_payload->pos = pos;
	}
	if (!buffer_payload) {
		if (!buffer->cache_count) {
			ast_data_buffer_cache_adjust(buffer);
		}
		buffer_payload = AST_LIST_REMOVE_HEAD(&buffer->cached_payloads, list);
		buffer->cache_count--;

		/* Update the payload from the cache with its new information */
		buffer_payload->payload = payload;
		buffer_payload->pos = pos;
	}
	if (!buffer_payload) {
		return -1;
	}

	/* Given the position find its ideal spot within the buffer */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&buffer->payloads, existing_payload, list) {
		/* If it's already in the buffer, drop it */
		if (existing_payload->pos == pos) {
			ast_debug(3, "Packet with position %zu is already in buffer. Not inserting.\n", pos);
			inserted = -1;
			break;
		}

		if (existing_payload->pos > pos) {
			AST_LIST_INSERT_BEFORE_CURRENT(buffer_payload, list);
			inserted = 1;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (inserted == -1) {
		return 0;
	}

	if (!inserted) {
		AST_LIST_INSERT_TAIL(&buffer->payloads, buffer_payload, list);
	}

	buffer->count++;

	return 0;
}

void *ast_data_buffer_get(const struct ast_data_buffer *buffer, size_t pos)
{
	struct data_buffer_payload_entry *buffer_payload;

	ast_assert(buffer != NULL);

	AST_LIST_TRAVERSE(&buffer->payloads, buffer_payload, list) {
		if (buffer_payload->pos == pos) {
			return buffer_payload->payload;
		}
	}

	return NULL;
}

static void data_buffer_free_buffer_payload(struct ast_data_buffer *buffer,
	struct data_buffer_payload_entry *buffer_payload)
{
	buffer_payload->payload = NULL;
	buffer->count--;

	if (buffer->cache_count < CACHED_PAYLOAD_MAX
			&& buffer->cache_count < (buffer->max - buffer->count)) {
		AST_LIST_INSERT_TAIL(&buffer->cached_payloads, buffer_payload, list);
		buffer->cache_count++;
	} else {
		ast_free(buffer_payload);
	}
}

void *ast_data_buffer_remove(struct ast_data_buffer *buffer, size_t pos)
{
	struct data_buffer_payload_entry *buffer_payload;

	ast_assert(buffer != NULL);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&buffer->payloads, buffer_payload, list) {
		if (buffer_payload->pos == pos) {
			void *payload = buffer_payload->payload;

			AST_LIST_REMOVE_CURRENT(list);
			data_buffer_free_buffer_payload(buffer, buffer_payload);

			return payload;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return NULL;
}

void *ast_data_buffer_remove_head(struct ast_data_buffer *buffer)
{
	ast_assert(buffer != NULL);

	if (buffer->count > 0) {
		struct data_buffer_payload_entry *buffer_payload;
		void *payload;

		buffer_payload = AST_LIST_REMOVE_HEAD(&buffer->payloads, list);
		payload = buffer_payload->payload;
		data_buffer_free_buffer_payload(buffer, buffer_payload);

		return payload;
	}

	return NULL;
}

void ast_data_buffer_free(struct ast_data_buffer *buffer)
{
	struct data_buffer_payload_entry *buffer_payload;

	ast_assert(buffer != NULL);

	while ((buffer_payload = AST_LIST_REMOVE_HEAD(&buffer->payloads, list))) {
		buffer->free_fn(buffer_payload->payload);
		ast_free(buffer_payload);
	}

	while ((buffer_payload = AST_LIST_REMOVE_HEAD(&buffer->cached_payloads, list))) {
		ast_free(buffer_payload);
	}

	ast_free(buffer);
}

size_t ast_data_buffer_count(const struct ast_data_buffer *buffer)
{
	ast_assert(buffer != NULL);

	return buffer->count;
}

size_t ast_data_buffer_max(const struct ast_data_buffer *buffer)
{
	ast_assert(buffer != NULL);

	return buffer->max;
}
