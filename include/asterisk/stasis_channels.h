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
#include "asterisk/json.h"
#include "asterisk/channel.h"

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \since 12
 * \brief Structure representing a snapshot of channel state.
 *
 * While not enforced programmatically, this object is shared across multiple
 * threads, and should be treated as an immutable object.
 */
struct ast_channel_snapshot {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);             /*!< ASCII unique channel name */
		AST_STRING_FIELD(uniqueid);         /*!< Unique Channel Identifier */
		AST_STRING_FIELD(linkedid);         /*!< Linked Channel Identifier -- gets propagated by linkage */
		AST_STRING_FIELD(appl);             /*!< Current application */
		AST_STRING_FIELD(data);             /*!< Data passed to current application */
		AST_STRING_FIELD(context);          /*!< Dialplan: Current extension context */
		AST_STRING_FIELD(exten);            /*!< Dialplan: Current extension number */
		AST_STRING_FIELD(accountcode);      /*!< Account code for billing */
		AST_STRING_FIELD(peeraccount);      /*!< Peer account code for billing */
		AST_STRING_FIELD(userfield);        /*!< Userfield for CEL billing */
		AST_STRING_FIELD(hangupsource);     /*!< Who is responsible for hanging up this channel */
		AST_STRING_FIELD(caller_name);      /*!< Caller ID Name */
		AST_STRING_FIELD(caller_number);    /*!< Caller ID Number */
		AST_STRING_FIELD(caller_dnid);      /*!< Dialed ID Number */
		AST_STRING_FIELD(caller_ani);       /*!< Caller ID ANI Number */
		AST_STRING_FIELD(caller_rdnis);     /*!< Caller ID RDNIS Number */
		AST_STRING_FIELD(caller_subaddr);   /*!< Caller subaddress */
		AST_STRING_FIELD(dialed_subaddr);   /*!< Dialed subaddress */
		AST_STRING_FIELD(connected_name);   /*!< Connected Line Name */
		AST_STRING_FIELD(connected_number); /*!< Connected Line Number */
		AST_STRING_FIELD(language);         /*!< The default spoken language for the channel */
		AST_STRING_FIELD(bridgeid);         /*!< Unique Bridge Identifier */
		AST_STRING_FIELD(type);             /*!< Type of channel technology */
	);

	struct timeval creationtime;            /*!< The time of channel creation */
	enum ast_channel_state state;           /*!< State of line */
	int priority;                           /*!< Dialplan: Current extension priority */
	int amaflags;                           /*!< AMA flags for billing */
	int hangupcause;                        /*!< Why is the channel hanged up. See causes.h */
	int caller_pres;                        /*!< Caller ID presentation. */
	struct ast_flags flags;                 /*!< channel flags of AST_FLAG_ type */
	struct ast_flags softhangup_flags;      /*!< softhangup channel flags */
	struct varshead *manager_vars;          /*!< Variables to be appended to manager events */
	int tech_properties;                    /*!< Properties of the channel's technology */
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

struct stasis_cp_all *ast_channel_cache_all(void);

/*!
 * \since 12
 * \brief A topic which publishes the events for all channels.
 * \retval Topic for all channel events.
 */
struct stasis_topic *ast_channel_topic_all(void);

/*!
 * \since 12
 * \brief A caching topic which caches \ref ast_channel_snapshot messages from
 * ast_channel_events_all(void).
 *
 * \retval Topic for all channel events.
 */
struct stasis_topic *ast_channel_topic_all_cached(void);

/*!
 * \since 12
 * \brief Primary channel cache, indexed by Uniqueid.
 *
 * \retval Cache of \ref ast_channel_snapshot.
 */
struct stasis_cache *ast_channel_cache(void);

/*!
 * \since 12
 * \brief Secondary channel cache, indexed by name.
 *
 * \retval Cache of \ref ast_channel_snapshot.
 */
struct stasis_cache *ast_channel_cache_by_name(void);

/*!
 * \since 12
 * \brief Message type for \ref ast_channel_snapshot.
 *
 * \retval Message type for \ref ast_channel_snapshot.
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
 * \retval pointer on success (must be unreffed)
 * \retval NULL on error
 */
struct ast_channel_snapshot *ast_channel_snapshot_create(
	struct ast_channel *chan);

/*!
 * \since 12
 * \brief Obtain the latest \ref ast_channel_snapshot from the \ref stasis cache. This is
 * an ao2 object, so use \ref ao2_cleanup() to deallocate.
 *
 * \param unique_id The channel's unique ID
 *
 * \retval A \ref ast_channel_snapshot on success
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
 * \retval A \ref ast_channel_snapshot on success
 * \retval NULL on error
 */
struct ast_channel_snapshot *ast_channel_snapshot_get_latest_by_name(const char *name);

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
 * \return \c NULL on error
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
 * \return \c NULL on error
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
 * \return \c NULL on error
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
 * \retval \ref ast_channel_snapshot matching the role on success
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
 * \retval A container containing all \ref ast_channel_snapshot objects matching
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
 * \return \c NULL on error.
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
 * \return Nothing
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
 *
 * \return Nothing
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
 * \brief Publish a \ref ast_channel_varset for a channel.
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
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_dial_type(void);

/*!
 * \since 12
 * \brief Message type for when a variable is set on a channel.
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_varset_type(void);

/*!
 * \since 12
 * \brief Message type for when a hangup is requested on a channel.
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_hangup_request_type(void);

/*!
 * \since 12
 * \brief Message type for when DTMF begins on a channel.
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_dtmf_begin_type(void);

/*!
 * \since 12
 * \brief Message type for when DTMF ends on a channel.
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_dtmf_end_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel is placed on hold.
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_hold_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel is removed from hold.
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_unhold_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel starts spying on another channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_chanspy_start_type(void);

/*!
 * \since 12
 * \brief Message type for when a channel stops spying on another channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_chanspy_stop_type(void);

/*!
 * \since 12
 * \brief Message type for a fax operation
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_fax_type(void);

/*!
 * \since 12
 * \brief Message type for hangup handler related actions
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_hangup_handler_type(void);

/*!
 * \since 12
 * \brief Message type for starting monitor on a channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_monitor_start_type(void);

/*!
 * \since 12
 * \brief Message type for stopping monitor on a channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_monitor_stop_type(void);

/*!
 * \since 12.0.0
 * \brief Message type for agent login on a channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_agent_login_type(void);

/*!
 * \since 12.0.0
 * \brief Message type for agent logoff on a channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_agent_logoff_type(void);

/*!
 * \since 12
 * \brief Message type for starting music on hold on a channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_moh_start_type(void);

/*!
 * \since 12
 * \brief Message type for stopping music on hold on a channel
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_moh_stop_type(void);

/*!
 * \since 12.4.0
 * \brief Message type for a channel starting talking
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_channel_talking_start(void);

/*!
 * \since 12.4.0
 * \brief Message type for a channel stopping talking
 *
 * \retval A stasis message type
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

/*!
 * \since 12
 * \brief Publish in the \ref ast_channel_topic a \ref ast_channel_snapshot
 * message indicating a change in channel state
 *
 * \pre chan is locked
 *
 * \param chan The channel whose state has changed
 */
void ast_publish_channel_state(struct ast_channel *chan);

/*! @} */

/*!
 * \brief Build a JSON object from a \ref ast_channel_snapshot.
 *
 * \param snapshot The snapshot to convert to JSON
 * \param sanitize The message sanitizer to use on the snapshot
 *
 * \return JSON object representing channel snapshot.
 * \return \c NULL on error
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
 * \return True (non-zero) if context, exten or priority are identical.
 * \return False (zero) if context, exten and priority changed.
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
 * \return True (non-zero) if callerid are identical.
 * \return False (zero) if callerid changed.
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
 * \return True (non-zero) if callerid are identical.
 * \return False (zero) if callerid changed.
 */
int ast_channel_snapshot_connected_line_equal(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot);

/*!
 * \brief Initialize the stasis channel topic and message types
 * \return 0 on success
 * \return Non-zero on error
 */
int ast_stasis_channels_init(void);

#endif /* STASIS_CHANNELS_H_ */
