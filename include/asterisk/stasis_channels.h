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
 * threads, and should be threated as an immutable object.
 */
struct ast_channel_snapshot {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);			/*!< ASCII unique channel name */
		AST_STRING_FIELD(accountcode);		/*!< Account code for billing */
		AST_STRING_FIELD(peeraccount);		/*!< Peer account code for billing */
		AST_STRING_FIELD(userfield);		/*!< Userfield for CEL billing */
		AST_STRING_FIELD(uniqueid);		/*!< Unique Channel Identifier */
		AST_STRING_FIELD(linkedid);		/*!< Linked Channel Identifier -- gets propagated by linkage */
		AST_STRING_FIELD(parkinglot);		/*!< Default parking lot, if empty, default parking lot */
		AST_STRING_FIELD(hangupsource);		/*!< Who is responsible for hanging up this channel */
		AST_STRING_FIELD(appl);			/*!< Current application */
		AST_STRING_FIELD(data);			/*!< Data passed to current application */
		AST_STRING_FIELD(context);		/*!< Dialplan: Current extension context */
		AST_STRING_FIELD(exten);		/*!< Dialplan: Current extension number */
		AST_STRING_FIELD(caller_name);		/*!< Caller ID Name */
		AST_STRING_FIELD(caller_number);	/*!< Caller ID Number */
		AST_STRING_FIELD(connected_name);	/*!< Connected Line Name */
		AST_STRING_FIELD(connected_number);	/*!< Connected Line Number */
	);

	struct timeval creationtime;	/*!< The time of channel creation */
	enum ast_channel_state state;	/*!< State of line */
	int priority;			/*!< Dialplan: Current extension priority */
	int amaflags;			/*!< AMA flags for billing */
	int hangupcause;		/*!< Why is the channel hanged up. See causes.h */
	int caller_pres;		/*!< Caller ID presentation. */
	struct ast_flags flags;		/*!< channel flags of AST_FLAG_ type */
	struct varshead *manager_vars;	/*!< Variables to be appended to manager events */
};

/*!
 * \since 12
 * \brief Blob of data associated with a channel.
 *
 * The \c blob is actually a JSON object of structured data. It has a "type" field
 * which contains the type string describing this blob.
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
struct stasis_caching_topic *ast_channel_topic_all_cached(void);

/*!
 * \since 12
 * \brief Message type for \ref ast_channel_snapshot.
 *
 * \retval Message type for \ref ast_channel_snapshot.
 */
struct stasis_message_type *ast_channel_snapshot_type(void);

/*!
 * \since 12
 * \brief Message type for \ref ast_channel_blob messages.
 *
 * \retval Message type for \ref ast_channel_blob messages.
 */
struct stasis_message_type *ast_channel_blob_type(void);

/*!
 * \since 12
 * \brief Generate a snapshot of the channel state. This is an ao2 object, so
 * ao2_cleanup() to deallocate.
 *
 * \param chan The channel from which to generate a snapshot
 *
 * \retval pointer on success (must be ast_freed)
 * \retval NULL on error
 */
struct ast_channel_snapshot *ast_channel_snapshot_create(struct ast_channel *chan);

/*!
 * \since 12
 * \brief Creates a \ref ast_channel_blob message.
 *
 * The \a blob JSON object requires a \c "type" field describing the blob. It
 * should also be treated as immutable and not modified after it is put into the
 * message.
 *
 * \param chan Channel blob is associated with, or NULL for global/all channels.
 * \param blob JSON object representing the data.
 * \return \ref ast_channel_blob message.
 * \return \c NULL on error
 */
struct stasis_message *ast_channel_blob_create(struct ast_channel *chan,
					       struct ast_json *blob);

/*!
 * \since 12
 * \brief Extracts the type field from a \ref ast_channel_blob.
 * Returned \c char* is still owned by \a obj
 * \param obj Channel blob object.
 * \return Type field value from the blob.
 * \return \c NULL on error.
 */
const char *ast_channel_blob_json_type(struct ast_channel_blob *obj);

/*!
 * \since 12
 * \brief Create a \ref ast_multi_channel_blob suitable for a \ref stasis_message
 *
 * \note Similar to a \ref ast_channel_blob, the \ref ast_multi_channel_blob requires
 * a \a blob JSON object containing a \c "type" field describing the blob. It
 * should also be treated as immutable and not modified after it is put into the
 * message.
 *
 * \param blob The JSON blob that defines the type of this \ref ast_multi_channel_blob
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
					       struct ast_multi_channel_blob *obj,
					       const char *role);

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
					       struct ast_multi_channel_blob *obj,
					       const char *role);

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
 * \brief Extracts the type field from a \ref ast_multi_channel_blob.
 * Returned \c char* is still owned by \a obj
 *
 * \param obj Channel blob object.
 * \return Type field value from the blob.
 * \return \c NULL on error.
 */
const char *ast_multi_channel_blob_get_type(struct ast_multi_channel_blob *obj);

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
					       const char *role,
					       struct ast_channel_snapshot *snapshot);

/*!
 * \since 12
 * \brief Publish a \ref ast_channel_varset for a channel.
 *
 * \param chan Channel to pulish the event for, or \c NULL for 'none'.
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

/*! @} */

/*!
 * \brief Dispose of the stasis channel topics and message types
 */
void ast_stasis_channels_shutdown(void);

/*!
 * \brief Initialize the stasis channel topic and message types
 */
void ast_stasis_channels_init(void);

#endif /* STASIS_CHANNELS_H_ */
