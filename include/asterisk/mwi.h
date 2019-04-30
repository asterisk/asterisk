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

#ifndef _ASTERISK_MWI_H
#define _ASTERISK_MWI_H

#include "asterisk/utils.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_json;
struct stasis_message_type;

/*!
 * \since 12
 * \brief Publish a MWI state update via stasis
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_publish_mwi_state(mailbox, context, new_msgs, old_msgs) \
	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, NULL, NULL)

/*!
 * \since 12
 * \brief Publish a MWI state update associated with some channel
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_publish_mwi_state_channel(mailbox, context, new_msgs, old_msgs, channel_id) \
	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, channel_id, NULL)

/*!
 * \since 12
 * \brief Publish a MWI state update via stasis with all parameters
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_publish_mwi_state_full(
	const char *mailbox,
	const char *context,
	int new_msgs,
	int old_msgs,
	const char *channel_id,
	struct ast_eid *eid);

/*!
 * \since 12.2.0
 * \brief Delete MWI state cached by stasis
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_delete_mwi_state(mailbox, context) \
	ast_delete_mwi_state_full(mailbox, context, NULL)

/*!
 * \since 12.2.0
 * \brief Delete MWI state cached by stasis with all parameters
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_delete_mwi_state_full(const char *mailbox, const char *context, struct ast_eid *eid);

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief The structure that contains MWI state
 * \since 12
 */
struct ast_mwi_state {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uniqueid);  /*!< Unique identifier for this mailbox */
	);
	int new_msgs;                    /*!< The current number of new messages for this mailbox */
	int old_msgs;                    /*!< The current number of old messages for this mailbox */
	/*! If applicable, a snapshot of the channel that caused this MWI change */
	struct ast_channel_snapshot *snapshot;
	struct ast_eid eid;              /*!< The EID of the server where this message originated */
};

/*!
 * \brief Object that represents an MWI update with some additional application
 * defined data
 */
struct ast_mwi_blob {
	struct ast_mwi_state *mwi_state;    /*!< MWI state */
	struct ast_json *blob;              /*!< JSON blob of data */
};

/*!
 * \since 12
 * \brief Create a \ref ast_mwi_state object
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 *
 * \retval \ref ast_mwi_state object on success
 * \retval NULL on error
 */
struct ast_mwi_state *ast_mwi_create(const char *mailbox, const char *context);

/*!
 * \since 12
 * \brief Creates a \ref ast_mwi_blob message.
 *
 * The \a blob JSON object requires a \c "type" field describing the blob. It
 * should also be treated as immutable and not modified after it is put into the
 * message.
 *
 * \param mwi_state MWI state associated with the update
 * \param message_type The type of message to create
 * \param blob JSON object representing the data.
 * \return \ref ast_mwi_blob message.
 * \return \c NULL on error
 */
struct stasis_message *ast_mwi_blob_create(struct ast_mwi_state *mwi_state,
					   struct stasis_message_type *message_type,
					   struct ast_json *blob);

/*!
 * \brief Get the \ref stasis topic for MWI messages
 * \retval The topic structure for MWI messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic_all(void);

/*!
 * \brief Get the \ref stasis topic for MWI messages on a unique ID
 * \param uniqueid The unique id for which to get the topic
 * \retval The topic structure for MWI messages for a given uniqueid
 * \retval NULL if it failed to be found or allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic(const char *uniqueid);

/*!
 * \brief Get the \ref stasis caching topic for MWI messages
 * \retval The caching topic structure for MWI messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic_cached(void);

/*!
 * \brief Backend cache for ast_mwi_topic_cached().
 * \retval Cache of \ref ast_mwi_state.
 */
struct stasis_cache *ast_mwi_state_cache(void);

/*!
 * \brief Get the \ref stasis message type for MWI messages
 * \retval The message type structure for MWI messages
 * \retval NULL on error
 * \since 12
 */
struct stasis_message_type *ast_mwi_state_type(void);

/*!
 * \brief Get the \ref stasis message type for voicemail application specific messages
 *
 * This message type exists for those messages a voicemail application may wish to send
 * that have no logical relationship with other voicemail applications. Voicemail apps
 * that use this message type must pass a \ref ast_mwi_blob. Any extraneous information
 * in the JSON blob must be packed as key/value pair tuples of strings.
 *
 * At least one key/value tuple must have a key value of "Event".
 *
 * \retval The \ref stasis_message_type for voicemail application specific messages
 * \retval NULL on error
 * \since 12
 */
struct stasis_message_type *ast_mwi_vm_app_type(void);

/*!
 * \brief Initialize the mwi core
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \since 13.26.0
 * \since 16.4.0
 */
int mwi_init(void);

#define AST_MAX_MAILBOX_UNIQUEID (AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2)

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MWI_H */
