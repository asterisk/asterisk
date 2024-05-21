/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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


#ifndef STASIS_CHANNELS_H_
#define STASIS_CHANNELS_H_

#include "asterisk/stringfields.h"
#include "asterisk/stasis.h"
#include "asterisk/channel.h"

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \since 17
 * \brief Channel snapshot invalidation flags, used to force generation of segments
 */
enum ast_channel_snapshot_segment_invalidation {
	/*! Invalidate the bridge segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_BRIDGE = (1 << 1),
	/*! Invalidate the dialplan segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN = (1 << 2),
	/*! Invalidate the connected segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_CONNECTED = (1 << 3),
	/*! Invalidate the caller segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_CALLER = (1 << 4),
	/*! Invalidate the hangup segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_HANGUP = (1 << 5),
	/*! Invalidate the peer segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_PEER = (1 << 6),
	/*! Invalidate the base segment */
	AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE = (1 << 7),
};

/*!
 * \since 17
 * \brief Structure containing bridge information for a channel snapshot.
 */
struct ast_channel_snapshot_bridge {
	char id[0]; /*!< Unique Bridge Identifier */
};

/*!
 * \since 17
 * \brief Structure containing dialplan information for a channel snapshot.
 */
struct ast_channel_snapshot_dialplan {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(appl);             /*!< Current application */
		AST_STRING_FIELD(data);             /*!< Data passed to current application */
		AST_STRING_FIELD(context);          /*!< Current extension context */
		AST_STRING_FIELD(exten);            /*!< Current extension number */
	);
	int priority; /*!< Current extension priority */
};

/*!
 * \since 17
 * \brief Structure containing caller information for a channel snapshot.
 */
struct ast_channel_snapshot_caller {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);           /*!< Caller ID Name */
		AST_STRING_FIELD(number);         /*!< Caller ID Number */
		AST_STRING_FIELD(dnid);           /*!< Dialed ID Number */
		AST_STRING_FIELD(dialed_subaddr); /*!< Dialed subaddress */
		AST_STRING_FIELD(ani);            /*!< Caller ID ANI Number */
		AST_STRING_FIELD(rdnis);          /*!< Caller ID RDNIS Number */
		AST_STRING_FIELD(subaddr);        /*!< Caller subaddress */
	);
	int pres; /*!< Caller ID presentation. */
};

/*!
 * \since 17
 * \brief Structure containing connected information for a channel snapshot.
 */
struct ast_channel_snapshot_connected {
	char *number; /*!< Connected Line Number */
	char name[0]; /*!< Connected Line Name */
};

/*!
 * \since 17
 * \brief Structure containing base information for a channel snapshot.
 */
struct ast_channel_snapshot_base {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);        /*!< ASCII unique channel name */
		AST_STRING_FIELD(uniqueid);    /*!< Unique Channel Identifier */
		AST_STRING_FIELD(accountcode); /*!< Account code for billing */
		AST_STRING_FIELD(userfield);   /*!< Userfield for CEL billing */
		AST_STRING_FIELD(language);    /*!< The default spoken language for the channel */
		AST_STRING_FIELD(type);        /*!< Type of channel technology */
		AST_STRING_FIELD(tenantid);    /*!< Channel tenant identifier */
	);
	struct timeval creationtime; /*!< The time of channel creation */
	int tech_properties;         /*!< Properties of the channel's technology */
	AST_STRING_FIELD_EXTENDED(protocol_id); /*!< Channel driver protocol id (i.e. Call-ID for chan_pjsip) */
};

/*!
 * \since 17
 * \brief Structure containing peer information for a channel snapshot.
 */
struct ast_channel_snapshot_peer {
	char *linkedid;   /*!< Linked Channel Identifier -- gets propagated by linkage */
	char account[0]; /*!< Peer account code for billing */
};

/*!
 * \since 17
 * \brief Structure containing hangup information for a channel snapshot.
 */
struct ast_channel_snapshot_hangup {
	int cause;      /*!< Why is the channel hanged up. See causes.h */
	char source[0]; /*!< Who is responsible for hanging up this channel */
};

/*!
 * \since 12
 * \brief Structure representing a snapshot of channel state.
 *
 * While not enforced programmatically, this object is shared across multiple
 * threads, and should be treated as an immutable object.
 *
 * It is guaranteed that the segments of this snapshot will always exist
 * when accessing the snapshot.
 */
struct ast_channel_snapshot {
	struct ast_channel_snapshot_base *base;           /*!< Base information about the channel */
	struct ast_channel_snapshot_peer *peer;           /*!< Peer information */
	struct ast_channel_snapshot_caller *caller;       /*!< Information about the caller */
	struct ast_channel_snapshot_connected *connected; /*!< Information about who this channel is connected to */
	struct ast_channel_snapshot_bridge *bridge;       /*!< Information about the bridge */
	struct ast_channel_snapshot_dialplan *dialplan;   /*!< Information about the dialplan */
	struct ast_channel_snapshot_hangup *hangup;       /*!< Hangup information */
	enum ast_channel_state state;                     /*!< State of line */
	int amaflags;                                     /*!< AMA flags for billing */
	struct ast_flags flags;                           /*!< channel flags of AST_FLAG_ type */
	struct ast_flags softhangup_flags;                /*!< softhangup channel flags */
	struct varshead *manager_vars;                    /*!< Variables to be appended to manager events */
	struct varshead *ari_vars;                        /*!< Variables to be appended to ARI events */
};

/*!
 * \since 17
 * \brief Structure representing a change of snapshot of channel state.
 *
 * While not enforced programmatically, this object is shared across multiple
 * threads, and should be treated as an immutable object.
 *
 * \note This structure will not have a transition of an old snapshot with no
 * new snapshot to indicate that a channel has gone away. A new snapshot will
 * always exist and a channel going away can be determined by checking for the
 * AST_FLAG_DEAD flag on the new snapshot.
 */
struct ast_channel_snapshot_update {
	struct ast_channel_snapshot *old_snapshot; /*!< The old channel snapshot */
	struct ast_channel_snapshot *new_snapshot; /*!< The new channel snapshot */
};

/*!
 * \since 12
 * \brief Blob of data associated with a channel.
 *
 * This blob is actually shared amongst several \ref stasis_message_type's.
 */
struct ast_channel_blob {
	/*! Channel blob is associated with (or NULL for global/all channels) */
	struct ast_channel_snapshot *snapshot;
	/*! JSON blob of data */
	struct ast_json *blob;
};

/*!
 * \since 12
 * \brief A set of channels with blob objects - see \ref ast_channel_blob
 */
struct ast_multi_channel_blob;

struct ao2_container *ast_channel_cache_all(void);

/*!
 * \since 12
 * \brief A topic which publishes the events for all channels.
 * \return Topic for all channel events.
 */
struct stasis_topic *ast_channel_topic_all(void);

/*!
 * \since 12
 * \brief Secondary channel cache, indexed by name.
 *
 * \return Cache of \ref ast_channel_snapshot.
 */
struct ao2_container *ast_channel_cache_by_name(void);

/*!
 * \since 12
 * \brief Message type for \ref ast_channel_snapshot_update.
 *
 * \return Message type for \ref ast_channel_snapshot_update.
 */
struct stasis_message_type *ast_channel_snapshot_type(void);

/*!
 * \since 12
 * \brief Generate a snapshot of the channel state. This is an ao2 object, so
 * ao2_cleanup() to deallocate.
 *
 * \pre chan is locked
 *
 * \param chan The channel from which to generate a snapshot
 *
 * \return pointer on success (must be unreffed)
 * \retval NULL on error
 */
struct ast_channel_snapshot *ast_channel_snapshot_create(
	struct ast_channel *chan);

/*!
 * \since 12
 * \brief Obtain the latest \ref ast_channel_snapshot from the \ref stasis cache. This is
 * an ao2 object, so use \ref ao2_cleanup() to deallocate.
 *
 * \param uniqueid The channel's unique ID
 *
 * \return A \ref ast_channel_snapshot on success
 * \retval NULL on error
 */
struct ast_channel_snapshot *ast_channel_snapshot_get_latest(const char *uniqueid);

/*!
 * \since 12
 * \brief Obtain the latest \ref ast_channel_snapshot from the \ref stasis cache. This is
 * an ao2 object, so use \ref ao2_cleanup() to deallocate.
 *
 * \param name The channel's name
 *
 * \return A \ref ast_channel_snapshot on success
 * \retval NULL on error
 */
struct ast_channel_snapshot *ast_channel_snapshot_get_latest_by_name(const char *name);

/*!
 * \since 17
 * \brief Send the final channel snapshot for a channel, thus removing it from cache
 *
 * \pre chan is locked
 *
 * \param chan The channel to send the final channel snapshot for
 *
 * \note This will also remove the cached snapshot from the channel itself
 */
void ast_channel_publish_final_snapshot(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Creates a \ref ast_channel_blob message.
 *
 * The given \a blob should be treated as immutable and not modified after it is
 * put into the message.
 *
 * \pre chan is locked
 *
 * \param chan Channel blob is associated with, or \c NULL for global/all channels.
 * \param type Message type for this blob.
 * \param blob JSON object representing the data, or \c NULL for no data. If
 *             \c NULL, ast_json_null() is put into the object.
 *
 * \return \ref ast_channel_blob message.
 * \retval NULL on error
 */
struct stasis_message *ast_channel_blob_create(struct ast_channel *chan,
	struct stasis_message_type *type, struct ast_json *blob);

/*!
 * \since 12
 * \brief Create a \ref ast_channel_blob message, pulling channel state from
 *        the cache.
 *
 * \param uniqueid Uniqueid of the channel.
 * \param type Message type for this blob.
 * \param blob JSON object representing the data, or \c NULL for no data. If
 *             \c NULL, ast_json_null() is put into the object.
 *
 * \return \ref ast_channel_blob message.
 * \retval NULL on error
 */
struct stasis_message *ast_channel_blob_create_from_cache(
	const char *uniqueid, struct stasis_message_type *type,
	struct ast_json *blob);

/*!
 * \since 12
 * \brief Create a \ref ast_multi_channel_blob suitable for a \ref stasis_message.
 *
 * The given \a blob should be treated as immutable and not modified after it is
 * put into the message.
 *
 * \param blob The JSON blob that defines the data of this \ref ast_multi_channel_blob
 *
 * \return \ref ast_multi_channel_blob object
 * \retval NULL on error
*/
struct ast_multi_channel_blob *ast_multi_channel_blob_create(struct ast_json *blob);

/*!
 * \since 12
 * \brief Retrieve a channel snapshot associated with a specific role from a
 * \ref ast_multi_channel_blob
 *
 * \note The reference count of the \ref ast_channel_snapshot returned from
 * this function is not changed. The caller of this function does not own the
 * reference to the snapshot.
 *
 * \param obj The \ref ast_multi_channel_blob containing the channel snapshot
 * to retrieve
 * \param role The role associated with the channel snapshot
 *
 * \return \ref ast_channel_snapshot matching the role on success
 * \retval NULL on error or not found for the role specified
 */
struct ast_channel_snapshot *ast_multi_channel_blob_get_channel(
	struct ast_multi_channel_blob *obj, const char *role);

/*!
 * \since 12
 * \brief Retrieve all channel snapshots associated with a specific role from
 * a \ref ast_multi_channel_blob
 *
 * \note Because this function returns an ao2_container (hashed by channel name)
 * of all channel snapshots that matched the passed in role, the reference of
 * the snapshots is increased by this function. The caller of this function must
 * release the reference to the snapshots by disposing of the container
 * appropriately.
 *
 * \param obj The \ref ast_multi_channel_blob containing the channel snapshots to
 * retrieve
 * \param role The role associated with the channel snapshots
 *
 * \return A container containing all \ref ast_channel_snapshot objects matching
 * the role on success.
 * \retval NULL on error or not found for the role specified
 */
struct ao2_container *ast_multi_channel_blob_get_channels(
	struct ast_multi_channel_blob *obj, const char *role);

/*!
 * \since 12
 * \brief Retrieve the JSON blob from a \ref ast_multi_channel_blob.
 * Returned \ref ast_json is still owned by \a obj
 *
 * \param obj Channel blob object.
 * \return Type field value from the blob.
 * \retval NULL on error.
 */
struct ast_json *ast_multi_channel_blob_get_json(struct ast_multi_channel_blob *obj);

/*!
 * \since 12
 * \brief Add a \ref ast_channel_snapshot to a \ref ast_multi_channel_blob object
 *
 * \note This will increase the reference count by 1 for the channel snapshot. It is
 * assumed that the \ref ast_multi_channel_blob will own a reference to the object.
 *
 * \param obj The \ref ast_multi_channel_blob object that will reference the snapshot
 * \param role A \a role that the snapshot has in the multi channel relationship
 * \param snapshot The \ref ast_channel_snapshot being added to the
 * \ref ast_multi_channel_blob object
 */
void ast_multi_channel_blob_add_channel(struct ast_multi_channel_blob *obj,
	const char *role, struct ast_channel_snapshot *snapshot);

/*!
 * \brief Publish a channel blob message.
 * \since 12.0.0
 *
 * \pre chan is locked
 *
 * \param chan Channel publishing the blob.
 * \param type Type of stasis message.
 * \param blob The blob being published. (NULL if no blob)
 *
 * \note This will use the current snapshot on the channel and will not generate a new one.
 */
void ast_channel_publish_blob(struct ast_channel *chan, struct stasis_message_type *type,
	struct ast_json *blob);

/*!
 * \brief Publish a channel blob message using the latest snapshot from the cache
 * \since 12.4.0
 *
 * \param chan Channel publishing the blob.
 * \param type Type of stasis message.
 * \param blob The blob being published. (NULL if no blob)
 *
 * \note As this only accesses the uniqueid and topic of the channel - neither of
 * which should ever be changed on a channel anyhow - a channel does not have to
 * be locked when calling this function.
 */
void ast_channel_publish_cached_blob(struct ast_channel *chan, struct stasis_message_type *type,
	struct ast_json *blob);

/*!
 * \since 12
 * \brief Set flag to indicate channel snapshot is being staged.
 *
 * \pre chan is locked
 *
 * \param chan Channel being staged.
 */
void ast_channel_stage_snapshot(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Clear flag to indicate channel snapshot is being staged, and publish snapshot.
 *
 * \pre chan is locked
 *
 * \param chan Channel being staged.
 */
void ast_channel_stage_snapshot_done(struct ast_channel *chan);

/*!
 * \since 17
 * \brief Invalidate a channel snapshot segment from being reused
 *
 * \pre chan is locked
 *
 * \param chan Channel to invalidate the segment on.
 * \param segment The segment to invalidate.
 */
void ast_channel_snapshot_invalidate_segment(struct ast_channel *chan,
	enum ast_channel_snapshot_segment_invalidation segment);

/*!
 * \since 12
 * \brief Publish a \ref ast_channel_snapshot for a channel.
 *
 * \pre chan is locked
 *
 * \param chan Channel to publish.
 */
void ast_channel_publish_snapshot(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Publish a \ref ast_channel_publish_varset for a channel.
 *
 * \pre chan is locked
 *
 * \param chan Channel to publish the event for, or \c NULL for 'none'.
 * \param variable Name of the variable being set
 * \param value Value.
 */
void ast_channel_publish_varset(struct ast_channel *chan,
				const char *variable, const char *value);

/*!
 * \since 12
 * \brief Message type for when a channel dials another channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_dial_type(void);

/*!
 * \since 12
 * \brief Message type for when a variable is set on a channel.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_varset_type(void);

/*!
 * \since 12
 * \brief Message type for when a hangup is requested on a channel.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_hangup_request_type(void);

/*!
 * \since 16
 * \brief Message type for when a channel is being masqueraded
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_masquerade_type(void);

/*!
 * \since 12
 * \brief Message type for when DTMF begins on a channel.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_dtmf_begin_type(void);

/*!
 * \since 12
 * \brief Message type for when DTMF ends on a channel.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_dtmf_end_type(void);

/*!
 * \brief Message type for when a hook flash occurs on a channel.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_flash_type(void);

/*!
 * \brief Message type for when a wink occurs on a channel.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_wink_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel is placed on hold.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_hold_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel is removed from hold.
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_unhold_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel starts spying on another channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_chanspy_start_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel stops spying on another channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_chanspy_stop_type(void);

/*!
 * \since 12
 * \brief Message type for a fax operation
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_fax_type(void);

/*!
 * \since 12
 * \brief Message type for hangup handler related actions
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_hangup_handler_type(void);

/*!
 * \since 18
 * \brief Message type for starting mixmonitor on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_mixmonitor_start_type(void);

/*!
 * \since 18
 * \brief Message type for stopping mixmonitor on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_mixmonitor_stop_type(void);

/*!
 * \since 18
 * \brief Message type for muting or unmuting mixmonitor on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_mixmonitor_mute_type(void);

/*!
 * \since 18.0.0
 * \brief Message type for agent login on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_agent_login_type(void);

/*!
 * \since 12.0.0
 * \brief Message type for agent logoff on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_agent_logoff_type(void);

/*!
 * \since 12
 * \brief Message type for starting music on hold on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_moh_start_type(void);

/*!
 * \since 12
 * \brief Message type for stopping music on hold on a channel
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_moh_stop_type(void);

/*!
 * \since 12.4.0
 * \brief Message type for a channel starting talking
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_talking_start(void);

/*!
 * \since 12.4.0
 * \brief Message type for a channel stopping talking
 *
 * \return A stasis message type
 */
struct stasis_message_type *ast_channel_talking_stop(void);

/*!
 * \since 12
 * \brief Publish in the \ref ast_channel_topic or \ref ast_channel_topic_all
 * topics a stasis message for the channels involved in a dial operation.
 *
 * \param caller The channel performing the dial operation
 * \param peer The channel being dialed
 * \param dialstring When beginning a dial, the information passed to the
 * dialing application
 * \param dialstatus The current status of the dial operation (NULL if no
 * status is known)
 */
void ast_channel_publish_dial(struct ast_channel *caller,
		struct ast_channel *peer,
		const char *dialstring,
		const char *dialstatus);

/*!
 * \since 12
 * \brief Publish in the \ref ast_channel_topic or \ref ast_channel_topic_all
 * topics a stasis message for the channels involved in a dial operation that
 * is forwarded.
 *
 * \param caller The channel performing the dial operation
 * \param peer The channel being dialed
 * \param forwarded The channel created as a result of the call forwarding
 * \param dialstring The information passed to the dialing application when beginning a dial
 * \param dialstatus The current status of the dial operation
 * \param forward The call forward string provided by the dialed channel
 */
void ast_channel_publish_dial_forward(struct ast_channel *caller,
		struct ast_channel *peer,
		struct ast_channel *forwarded,
		const char *dialstring,
		const char *dialstatus,
		const char *forward);

/*! @} */

/*!
 * \brief Build a JSON object from a \ref ast_channel_snapshot.
 *
 * \param snapshot The snapshot to convert to JSON
 * \param sanitize The message sanitizer to use on the snapshot
 *
 * \return JSON object representing channel snapshot.
 * \retval NULL on error
 */
struct ast_json *ast_channel_snapshot_to_json(const struct ast_channel_snapshot *snapshot,
	const struct stasis_message_sanitizer *sanitize);

/*!
 * \brief Compares the context, exten and priority of two snapshots.
 * \since 12
 *
 * \param old_snapshot Old snapshot
 * \param new_snapshot New snapshot
 *
 * \retval True (non-zero) if context, exten or priority are identical.
 * \retval False (zero) if context, exten and priority changed.
 */
int ast_channel_snapshot_cep_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot);

/*!
 * \brief Compares the callerid info of two snapshots.
 * \since 12
 *
 * \param old_snapshot Old snapshot
 * \param new_snapshot New snapshot
 *
 * \retval True (non-zero) if callerid are identical.
 * \retval False (zero) if callerid changed.
 */
int ast_channel_snapshot_caller_id_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot);

/*!
 * \brief Compares the connected line info of two snapshots.
 * \since 13.1.0
 *
 * \param old_snapshot Old snapshot
 * \param new_snapshot New snapshot
 *
 * \retval True (non-zero) if callerid are identical.
 * \retval False (zero) if callerid changed.
 */
int ast_channel_snapshot_connected_line_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot);

/*!
 * \brief Initialize the stasis channel topic and message types
 * \retval 0 on success
 * \retval Non-zero on error
 */
int ast_stasis_channels_init(void);

#endif /* STASIS_CHANNELS_H_ */
