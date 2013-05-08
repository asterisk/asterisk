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

#ifndef _ASTERISK_STASIS_ENDPOINTS_H
#define _ASTERISK_STASIS_ENDPOINTS_H

/*! \file
 *
 * \brief Endpoint abstractions.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

#include "asterisk/endpoints.h"
#include "asterisk/json.h"
#include "asterisk/stasis.h"
#include "asterisk/stringfields.h"

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief A snapshot of an endpoint's state.
 *
 * The id for an endpoint is tech/resource. The duplication is needed because
 * there are several cases where any of the three values would be needed, and
 * constantly splitting or reassembling would be a pain.
 *
 * \since 12
 */
struct ast_endpoint_snapshot {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(id);	/*!< unique id for this endpoint. */
		AST_STRING_FIELD(tech);	/*!< Channel technology */
		AST_STRING_FIELD(resource);	/*!< Tech-unique name */
		);

	/*! Endpoint state */
	enum ast_endpoint_state state;
	/*!
	 * Maximum number of channels this endpoint supports. If the upper limit
	 * for an endpoint is unknown, this field is set to -1.
	 */
	int max_channels;
	/*! Number of channels currently active on this endpoint */
	int num_channels;
	/*! Channel ids */
	char *channel_ids[];
};

/*!
 * \brief Blob of data associated with an endpoint.
 *
 * The blob is actually a JSON object of structured data. It has a "type" field
 * which contains the type string describing this blob.
 *
 * \since 12
 */
struct ast_endpoint_blob {
	struct ast_endpoint_snapshot *snapshot;
	struct ast_json *blob;
};

/*!
 * \brief Message type for \ref ast_endpoint_snapshot.
 * \since 12
 */
struct stasis_message_type *ast_endpoint_snapshot_type(void);

/*!
 * \brief Create a snapshot of an endpoint
 * \param endpoint Endpoint to snap a shot of.
 * \return Snapshot of the endpoint.
 * \return \c NULL on error.
 * \since 12
 */
struct ast_endpoint_snapshot *ast_endpoint_snapshot_create(
	struct ast_endpoint *endpoint);

/*!
 * \brief Returns the topic for a specific endpoint.
 *
 * \param endpoint The endpoint.
 * \return The topic for the given endpoint.
 * \return ast_endpoint_topic_all() if endpoint is \c NULL.
 * \since 12
 */
struct stasis_topic *ast_endpoint_topic(struct ast_endpoint *endpoint);

/*!
 * \brief Topic for all endpoint releated messages.
 * \since 12
 */
struct stasis_topic *ast_endpoint_topic_all(void);

/*!
 * \brief Cached topic for all endpoint related messages.
 * \since 12
 */
struct stasis_caching_topic *ast_endpoint_topic_all_cached(void);

/*!
 * \brief Retrieve the most recent snapshot for the endpoint with the given
 * name.
 *
 * \param tech Name of the endpoint's technology.
 * \param resource Resource name of the endpoint.
 * \return Snapshot of the endpoint with the given name.
 * \return \c NULL if endpoint is not found, or on error.
 * \since 12
 */
struct ast_endpoint_snapshot *ast_endpoint_latest_snapshot(const char *tech,
	const char *resource
);

/*! @} */

/*!
 * \brief Build a JSON object from a \ref ast_endpoint_snapshot.
 *
 * \param snapshot Endpoint snapshot.
 * \return JSON object representing endpoint snapshot.
 * \return \c NULL on error
 */
struct ast_json *ast_endpoint_snapshot_to_json(
	const struct ast_endpoint_snapshot *snapshot);

/*!
 * \brief Initialization function for endpoint stasis support.
 *
 * \return 0 on success.
 * \return non-zero on error.
 * \since 12
 */
int ast_endpoint_stasis_init(void);

#endif /* _ASTERISK_STASIS_ENDPOINTS_H */
