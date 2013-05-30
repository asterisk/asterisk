/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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

#ifndef _STASIS_BRIDGING_H
#define _STASIS_BRIDGING_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/stringfields.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"

/*!
 * \brief Structure that contains a snapshot of information about a bridge
 */
struct ast_bridge_snapshot {
	AST_DECLARE_STRING_FIELDS(
		/*! Immutable bridge UUID. */
		AST_STRING_FIELD(uniqueid);
		/*! Bridge technology that is handling the bridge */
		AST_STRING_FIELD(technology);
	);
	/*! AO2 container of bare channel uniqueid strings participating in the bridge.
	 * Allocated from ast_str_container_alloc() */
	struct ao2_container *channels;
	/*! Bridge flags to tweak behavior */
	struct ast_flags feature_flags;
	/*! Number of channels participating in the bridge */
	unsigned int num_channels;
	/*! Number of active channels in the bridge. */
	unsigned int num_active;
};

/*!
 * \since 12
 * \brief Generate a snapshot of the bridge state. This is an ao2 object, so
 * ao2_cleanup() to deallocate.
 *
 * \param bridge The bridge from which to generate a snapshot
 *
 * \retval AO2 refcounted snapshot on success
 * \retval NULL on error
 */
struct ast_bridge_snapshot *ast_bridge_snapshot_create(struct ast_bridge *bridge);

/*!
 * \since 12
 * \brief Message type for \ref ast_bridge_snapshot.
 *
 * \retval Message type for \ref ast_bridge_snapshot.
 */
struct stasis_message_type *ast_bridge_snapshot_type(void);

/*!
 * \since 12
 * \brief A topic which publishes the events for a particular bridge.
 *
 * If the given \a bridge is \c NULL, ast_bridge_topic_all() is returned.
 *
 * \param bridge Bridge for which to get a topic or \c NULL.
 *
 * \retval Topic for bridge's events.
 * \retval ast_bridge_topic_all() if \a bridge is \c NULL.
 */
struct stasis_topic *ast_bridge_topic(struct ast_bridge *bridge);

/*!
 * \since 12
 * \brief A topic which publishes the events for all bridges.
 * \retval Topic for all bridge events.
 */
struct stasis_topic *ast_bridge_topic_all(void);

/*!
 * \since 12
 * \brief A caching topic which caches \ref ast_bridge_snapshot messages from
 * ast_bridge_events_all(void).
 *
 * \retval Caching topic for all bridge events.
 */
struct stasis_caching_topic *ast_bridge_topic_all_cached(void);

/*!
 * \since 12
 * \brief Publish the state of a bridge
 *
 * \param bridge The bridge for which to publish state
 */
void ast_bridge_publish_state(struct ast_bridge *bridge);

/*! \brief Message representing the merge of two bridges */
struct ast_bridge_merge_message {
	struct ast_bridge_snapshot *from;	/*!< Bridge from which channels will be removed during the merge */
	struct ast_bridge_snapshot *to;		/*!< Bridge to which channels will be added during the merge */
};

/*!
 * \since 12
 * \brief Message type for \ref ast_bridge_merge_message.
 *
 * \retval Message type for \ref ast_bridge_merge_message.
 */
struct stasis_message_type *ast_bridge_merge_message_type(void);

/*!
 * \since 12
 * \brief Publish a bridge merge
 *
 * \param to The bridge to which channels are being added
 * \param from The bridge from which channels are being removed
 */
void ast_bridge_publish_merge(struct ast_bridge *to, struct ast_bridge *from);

/*!
 * \since 12
 * \brief Blob of data associated with a bridge.
 *
 * The \c blob is actually a JSON object of structured data. It has a "type" field
 * which contains the type string describing this blob.
 */
struct ast_bridge_blob {
	/*! Bridge blob is associated with (or NULL for global/all bridges) */
	struct ast_bridge_snapshot *bridge;
	/*! Channel blob is associated with (may be NULL for some messages) */
	struct ast_channel_snapshot *channel;
	/*! JSON blob of data */
	struct ast_json *blob;
};

/*!
 * \since 12
 * \brief Message type for \ref channel enter bridge blob messages.
 *
 * \retval Message type for \ref channel enter bridge blob messages.
 */
struct stasis_message_type *ast_channel_entered_bridge_type(void);

/*!
 * \since 12
 * \brief Message type for \ref channel leave bridge blob messages.
 *
 * \retval Message type for \ref channel leave bridge blob messages.
 */
struct stasis_message_type *ast_channel_left_bridge_type(void);

/*!
 * \since 12
 * \brief Creates a \ref ast_bridge_blob message.
 *
 * The \a blob JSON object requires a \c "type" field describing the blob. It
 * should also be treated as immutable and not modified after it is put into the
 * message.
 *
 * \param bridge Channel blob is associated with, or NULL for global/all bridges.
 * \param blob JSON object representing the data.
 * \return \ref ast_bridge_blob message.
 * \return \c NULL on error
 */
struct stasis_message *ast_bridge_blob_create(struct stasis_message_type *type,
	struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_json *blob);

/*!
 * \since 12
 * \brief Extracts the type field from a \ref ast_bridge_blob.
 *
 * Returned \c char* is still owned by \a obj
 *
 * \param obj Channel blob object.
 *
 * \retval Type field value from the blob.
 * \retval \c NULL on error.
 */
const char *ast_bridge_blob_json_type(struct ast_bridge_blob *obj);

/*!
 * \since 12
 * \brief Publish a bridge channel enter event
 *
 * \param bridge The bridge a channel entered
 * \param chan The channel that entered the bridge
 */
void ast_bridge_publish_enter(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \since 12
 * \brief Publish a bridge channel leave event
 *
 * \param bridge The bridge a channel left
 * \param chan The channel that left the bridge
 */
void ast_bridge_publish_leave(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Build a JSON object from a \ref ast_bridge_snapshot.
 * \return JSON object representing bridge snapshot.
 * \return \c NULL on error
 */
struct ast_json *ast_bridge_snapshot_to_json(const struct ast_bridge_snapshot *snapshot);

/*!
 * \brief Initialize the stasis bridging topic and message types
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_stasis_bridging_init(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _STASIS_BRIDGING_H */
