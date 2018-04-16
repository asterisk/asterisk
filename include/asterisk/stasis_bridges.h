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
#include "asterisk/bridge.h"
#include "asterisk/pbx.h"

/*!
 * \brief Structure that contains a snapshot of information about a bridge
 */
struct ast_bridge_snapshot {
	AST_DECLARE_STRING_FIELDS(
		/*! Immutable bridge UUID. */
		AST_STRING_FIELD(uniqueid);
		/*! Bridge technology that is handling the bridge */
		AST_STRING_FIELD(technology);
		/*! Bridge subclass that is handling the bridge */
		AST_STRING_FIELD(subclass);
		/*! Creator of the bridge */
		AST_STRING_FIELD(creator);
		/*! Name given to the bridge by its creator */
		AST_STRING_FIELD(name);
		/*! Unique ID of the channel providing video, if one exists */
		AST_STRING_FIELD(video_source_id);
	);
	/*! AO2 container of bare channel uniqueid strings participating in the bridge.
	 * Allocated from ast_str_container_alloc() */
	struct ao2_container *channels;
	/*! Bridge flags to tweak behavior */
	struct ast_flags feature_flags;
	/*! Bridge capabilities */
	uint32_t capabilities;
	/*! Number of channels participating in the bridge */
	unsigned int num_channels;
	/*! Number of active channels in the bridge. */
	unsigned int num_active;
	/*! The video mode of the bridge */
	enum ast_bridge_video_mode_type video_mode;
};

/*!
 * \since 12
 * \brief Generate a snapshot of the bridge state. This is an ao2 object, so
 * ao2_cleanup() to deallocate.
 *
 * \pre Bridge is locked
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
 * \brief A topic which publishes the events for a particular bridge.
 *
 * \ref ast_bridge_snapshot messages are replaced with stasis_cache_update
 * messages.
 *
 * If the given \a bridge is \c NULL, ast_bridge_topic_all_cached() is returned.
 *
 * \param bridge Bridge for which to get a topic or \c NULL.
 *
 * \retval Topic for bridge's events.
 * \retval ast_bridge_topic_all() if \a bridge is \c NULL.
 */
struct stasis_topic *ast_bridge_topic_cached(struct ast_bridge *bridge);

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
struct stasis_topic *ast_bridge_topic_all_cached(void);

/*!
 * \since 12
 * \brief Backend cache for ast_bridge_topic_all_cached().
 * \retval Cache of \ref ast_bridge_snapshot.
 */
struct stasis_cache *ast_bridge_cache(void);

/*!
 * \since 12
 * \brief Publish the state of a bridge
 *
 * \pre Bridge is locked
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
 * \pre Bridges involved are locked
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
 * \pre bridge is locked.
 * \pre No channels are locked.
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
 * \brief Publish a bridge channel enter event
 *
 * \pre bridge is locked.
 * \pre No channels are locked.
 *
 * \param bridge The bridge a channel entered
 * \param chan The channel that entered the bridge
 * \param swap The channel being swapped out of the bridge
 */
void ast_bridge_publish_enter(struct ast_bridge *bridge, struct ast_channel *chan,
		struct ast_channel *swap);

/*!
 * \since 12
 * \brief Publish a bridge channel leave event
 *
 * \pre bridge is locked.
 * \pre No channels are locked.
 *
 * \param bridge The bridge a channel left
 * \param chan The channel that left the bridge
 */
void ast_bridge_publish_leave(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Build a JSON object from a \ref ast_bridge_snapshot.
 *
 * \param snapshot The bridge snapshot to convert to JSON
 * \param sanitize The message sanitizer to use on the snapshot
 *
 * \return JSON object representing bridge snapshot.
 * \return \c NULL on error
 */
struct ast_json *ast_bridge_snapshot_to_json(const struct ast_bridge_snapshot *snapshot,
	const struct stasis_message_sanitizer *sanitize);

/*!
 * \brief Pair showing a bridge snapshot and a specific channel snapshot belonging to the bridge
 */
struct ast_bridge_channel_snapshot_pair {
	struct ast_bridge_snapshot *bridge_snapshot;
	struct ast_channel_snapshot *channel_snapshot;
};

/*!
 * \since 12
 * \brief Message type for \ref ast_blind_transfer_message.
 *
 * \retval Message type for \ref ast_blind_transfer_message.
 */
struct stasis_message_type *ast_blind_transfer_type(void);

/*!
 * \brief Message published during a blind transfer
 */
struct ast_blind_transfer_message {
	/*! Result of the transfer */
	enum ast_transfer_result result;
	/*! True if the transfer was initiated by an external source (i.e. not DTMF-initiated) */
	int is_external;
	/*! The transferring channel */
	struct ast_channel_snapshot *transferer;
	/*! The bridge between the transferer and the transferee */
	struct ast_bridge_snapshot *bridge;
	/*! Destination context */
	char context[AST_MAX_CONTEXT];
	/*! Destination extension */
	char exten[AST_MAX_EXTENSION];
	/*! Transferee channel. NULL if there were multiple transferee channels */
	struct ast_channel_snapshot *transferee;
	/*! The channel replacing the transferer when multiple parties are being transferred */
	struct ast_channel_snapshot *replace_channel;
};

/*!
 * \brief Create a blind transfer message to be published
 *
 * \param is_external Whether the blind transfer was initiated externally (e.g. via AMI or native protocol)
 * \param transferer The transferer's channel that is bridged to the transferee
 * \param bridge The bridge the transferer and transferee are in
 * \param context The destination context for the blind transfer
 * \param exten The destination extension for the blind transfer
 *
 * \retval NULL Failure to allocate or create snapshots
 * \retval non-NULL The created blind transfer message
 */
struct ast_blind_transfer_message *ast_blind_transfer_message_create(int is_external,
		struct ast_channel *transferer, const char *exten, const char *context);

/*!
 * \brief Publish a blind transfer event
 *
 * \pre Bridges involved are locked. Channels involved are not locked.
 *
 * \param is_external Whether the blind transfer was initiated externally (e.g. via AMI or native protocol)
 * \param result The success or failure of the transfer
 * \param to_transferee The bridge between the transferer and transferee plus the transferer channel
 * \param context The destination context for the blind transfer
 * \param exten The destination extension for the blind transfer
 * \param transferee_channel If a single channel is being transferred, this is it. If
 *                           multiple parties are being transferred, this is NULL.
 * \param replace_channel If multiple parties are being transferred or the transfer
 *                        cannot reach across the bridge due to bridge flags, this is
 *                        the channel connecting their bridge to the destination.
 */
void ast_bridge_publish_blind_transfer(struct ast_blind_transfer_message *transfer_message);

enum ast_attended_transfer_dest_type {
	/*! The transfer failed, so there is no appropriate final state */
	AST_ATTENDED_TRANSFER_DEST_FAIL,
	/*! The transfer results in a single bridge remaining due to a merge or swap */
	AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE,
	/*! The transfer results in a channel or bridge running an application */
	AST_ATTENDED_TRANSFER_DEST_APP,
	/*! The transfer results in a channel or bridge running an application via a local channel */
	AST_ATTENDED_TRANSFER_DEST_LOCAL_APP,
	/*! The transfer results in both bridges remaining with a local channel linking them */
	AST_ATTENDED_TRANSFER_DEST_LINK,
	/*! The transfer results in a threeway call between transferer, transferee, and transfer target */
	AST_ATTENDED_TRANSFER_DEST_THREEWAY,
};

/*!
 * \brief Message representing attended transfer
 */
struct ast_attended_transfer_message {
	/*! Result of the attended transfer */
	enum ast_transfer_result result;
	/*! Indicates if the transfer was initiated externally*/
	int is_external;
	/*! Bridge between transferer <-> transferee and the transferer channel in that bridge. May be NULL */
	struct ast_bridge_channel_snapshot_pair to_transferee;
	/*! Bridge between transferer <-> transfer target and the transferer channel in that bridge. May be NULL */
	struct ast_bridge_channel_snapshot_pair to_transfer_target;
	/*! Local channel connecting transferee bridge to application */
	struct ast_channel_snapshot *replace_channel;
	/*! Transferee channel. Will be NULL if there were multiple channels transferred. */
	struct ast_channel_snapshot *transferee;
	/*! Transfer target channel. Will be NULL if there were multiple channels targeted. */
	struct ast_channel_snapshot *target;
	/*! Indicates the final state of the transfer */
	enum ast_attended_transfer_dest_type dest_type;
	union {
		/*! ID of the surviving bridge. Applicable for AST_ATTENDED_TRANSFER_DEST_BRIDGE_MERGE */
		char bridge[AST_UUID_STR_LEN];
		/*! Destination application of transfer. Applicable for AST_ATTENDED_TRANSFER_DEST_APP */
		char app[AST_MAX_APP];
		/*! Pair of local channels linking the bridges. Applicable for AST_ATTENDED_TRANSFER_DEST_LINK */
		struct ast_channel_snapshot *links[2];
		/*! Transferer channel and bridge that survived the transition to a threeway call. Applicable for AST_ATTENDED_TRANSFER_DEST_THREEWAY */
		struct ast_bridge_channel_snapshot_pair threeway;
	} dest;
};

/*!
 * \brief Create an Attended transfer message to be published.
 *
 * The parameters to this function are the basic necessities in order to create the
 * initial attended transfer message.
 *
 * The transferee and transfer_target parameters are optional. If not provided, then this
 * function will attempt to determine who the transferee and transfer target are based on
 * the input transferer channels and bridges. You typically will not need to provide an
 * explicit transferee and transfer target channel unless your attended transfer is implemented
 * in a strange way.
 *
 * \param is_external Non-zero if the transfer was initiated by a native channel driver protocol.
 * \param to_transferee The transferer channel that is bridged to the transferee channel.
 * \param transferee_bridge The bridge between the transferer and transferee. May be NULL.
 * \param to_transfer_target The transferer channel that is bridged to the transfer target.
 * \param target_bridge The bridge between the transferer and transfer target. May be NULL.
 * \param transferee The channel that is being transferred. Optional.
 * \param transfer_target The channel that is being transferred to. Optional.
 *
 * \retval NULL Failure to allocate or create snapshots
 * \retval non-NULL The created attended transfer message
 */
struct ast_attended_transfer_message *ast_attended_transfer_message_create(
		int is_external, struct ast_channel *to_transferee, struct ast_bridge *transferee_bridge,
		struct ast_channel *to_transfer_target, struct ast_bridge *target_bridge,
		struct ast_channel *transferee, struct ast_channel *transfer_target);

/*!
 * \brief Add details for a bridge merge to an attended transfer message.
 *
 * If the transfer is accomplished by a bridge merge (or swap optimization), then this should
 * be called on the created attended transfer message to have the appropriate details added on.
 *
 * \param transfer_msg The transfer message to add details to
 * \param final_bridge The bridge where the surviving parties reside
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_attended_transfer_message_add_merge(struct ast_attended_transfer_message *transfer_msg,
		struct ast_bridge *final_bridge);

/*!
 * \brief Add details for an attended transfer that was resolved as a three-way call
 *
 * If the transfer results in a three-way call between the transferer, the transferee, and the
 * transfer target, then this should be called in order to add appropriate details to the
 * transfer message to be published.
 *
 * \param transfer_msg The message to add details to
 * \param survivor_channel The transferer channel that exists in the three-way call
 * \param survivor_bridge The bridge where the three-way call takes place.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_attended_transfer_message_add_threeway(struct ast_attended_transfer_message *transfer_msg,
		struct ast_channel *survivor_channel, struct ast_bridge *survivor_bridge);

/*!
 * \brief Add details for an attended transfer to an application
 *
 * If the transfer is sending one or more parties into an application, then this should be called
 * to add appropriate details to the transfer message being published.
 *
 * \param transfer_msg The message to add details to
 * \param app The name of the application that the parties are being transferred to
 * \param replace_channel The local channel that is in the bridge and running the application
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_attended_transfer_message_add_app(struct ast_attended_transfer_message *transfer_msg,
		const char *app, struct ast_channel *replace_channel);

/*!
 * \brief Add details for an attended transfer that has a link between bridges.
 *
 * An attended transfer may be accomplished by linking two bridges together with local channels.
 * If this is how the transfer is to be completed, call this function in order to fill in details
 * about the transfer.
 *
 * \param transfer_msg The message to add details to.
 * \param locals An array of local channel halves that each are in one of the involved bridges.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_attended_transfer_message_add_link(struct ast_attended_transfer_message *transfer_msg,
		struct ast_channel *locals[2]);

/*!
 * \brief Publish an attended transfer
 *
 * \param transfer_msg The transfer message to publish
 */
void ast_bridge_publish_attended_transfer(struct ast_attended_transfer_message *transfer_msg);

/*!
 * \since 12
 * \brief Message type for \ref ast_attended_transfer_message.
 *
 * \retval Message type for \ref ast_attended_transfer_message.
 */
struct stasis_message_type *ast_attended_transfer_type(void);

/*!
 * \brief Returns the most recent snapshot for the bridge.
 *
 * The returned pointer is AO2 managed, so ao2_cleanup() when you're done.
 *
 * \param bridge_id Uniqueid of the bridge for which to get the snapshot.
 * \return Most recent snapshot. ao2_cleanup() when done.
 * \return \c NULL if channel isn't in cache.
 */
struct ast_bridge_snapshot *ast_bridge_snapshot_get_latest(
	const char *bridge_id);

/*!
 * \internal
 * \brief Initialize the topics for a single bridge.
 * \return 0 on success.
 * \return Non-zero on error.
 */
int bridge_topics_init(struct ast_bridge *bridge);

/*!
 * \internal
 * \brief Initialize the stasis bridging topic and message types
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_stasis_bridging_init(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _STASIS_BRIDGING_H */
