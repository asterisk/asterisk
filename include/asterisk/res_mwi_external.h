/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Core external MWI support.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_RES_MWI_EXTERNAL_H
#define _ASTERISK_RES_MWI_EXTERNAL_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C"
{
#endif

/* ------------------------------------------------------------------- */

struct ast_mwi_mailbox_object;

/*! \brief Convienience unref function for mailbox object. */
#define ast_mwi_mailbox_unref(mailbox) ao2_ref((struct ast_mwi_mailbox_object *) mailbox, -1)

/*!
 * \brief Allocate an external MWI object.
 * \since 12.1.0
 *
 * \param mailbox_id Name of mailbox.
 *
 * \retval object on success.  The object is an ao2 object.
 * \retval NULL on error.
 */
struct ast_mwi_mailbox_object *ast_mwi_mailbox_alloc(const char *mailbox_id);

/*!
 * \brief Get mailbox id.
 * \since 12.1.0
 *
 * \param mailbox Object to get id.
 *
 * \return mailbox_id of the object.
 *
 * \note This should never return NULL unless there is a bug in sorcery.
 */
const char *ast_mwi_mailbox_get_id(const struct ast_mwi_mailbox_object *mailbox);

/*!
 * \brief Get the number of new messages.
 * \since 12.1.0
 *
 * \param mailbox Object to get number of new messages.
 *
 * \return Number of new messages.
 */
unsigned int ast_mwi_mailbox_get_msgs_new(const struct ast_mwi_mailbox_object *mailbox);

/*!
 * \brief Get the number of old messages.
 * \since 12.1.0
 *
 * \param mailbox Object to get number of old messages.
 *
 * \return Number of old messages.
 */
unsigned int ast_mwi_mailbox_get_msgs_old(const struct ast_mwi_mailbox_object *mailbox);

/*!
 * \brief Copy the external MWI counts object.
 * \since 12.1.0
 *
 * \param mailbox What to copy.
 *
 * \retval copy on success.  The object is an ao2 object.
 * \retval NULL on error.
 */
struct ast_mwi_mailbox_object *ast_mwi_mailbox_copy(const struct ast_mwi_mailbox_object *mailbox);

/*!
 * \brief Set the number of new messages.
 * \since 12.1.0
 *
 * \param mailbox Object to set number of new messages.
 * \param num_msgs Number of messages to set.
 *
 * \return Nothing
 */
void ast_mwi_mailbox_set_msgs_new(struct ast_mwi_mailbox_object *mailbox, unsigned int num_msgs);

/*!
 * \brief Set the number of old messages.
 * \since 12.1.0
 *
 * \param mailbox Object to set number of old messages.
 * \param num_msgs Number of messages to set.
 *
 * \return Nothing
 */
void ast_mwi_mailbox_set_msgs_old(struct ast_mwi_mailbox_object *mailbox, unsigned int num_msgs);

/*!
 * \brief Update the external MWI counts with the given object.
 * \since 12.1.0
 *
 * \param mailbox What to update.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_mwi_mailbox_update(struct ast_mwi_mailbox_object *mailbox);

/*!
 * \brief Delete matching external MWI object.
 * \since 12.1.0
 *
 * \param mailbox_id Name of mailbox to delete.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_mwi_mailbox_delete(const char *mailbox_id);

/*!
 * \brief Delete all external MWI objects selected by the regular expression.
 * \since 12.1.0
 *
 * \param regex Regular expression in extended syntax.  (NULL is same as "")
 *
 * \note The provided regex is treated as extended case sensitive.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_mwi_mailbox_delete_by_regex(const char *regex);

/*!
 * \brief Delete all external MWI objects.
 * \since 12.1.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_mwi_mailbox_delete_all(void);

/*!
 * \brief Get matching external MWI object.
 * \since 12.1.0
 *
 * \param mailbox_id Name of mailbox to retrieve.
 *
 * \retval requested mailbox on success.  The object is an ao2 object.
 * \retval NULL on error or no mailbox.
 *
 * \note The object must be treated as read-only.
 */
const struct ast_mwi_mailbox_object *ast_mwi_mailbox_get(const char *mailbox_id);

/*!
 * \brief Get all external MWI objects selected by the regular expression.
 * \since 12.1.0
 *
 * \param regex Regular expression in extended syntax.  (NULL is same as "")
 *
 * \note The provided regex is treated as extended case sensitive.
 *
 * \retval container of struct ast_mwi_mailbox_object on success.
 * \retval NULL on error.
 *
 * \note The objects in the container must be treated as read-only.
 */
struct ao2_container *ast_mwi_mailbox_get_by_regex(const char *regex);

/*!
 * \brief Get all external MWI objects.
 * \since 12.1.0
 *
 * \retval container of struct ast_mwi_mailbox_object on success.
 * \retval NULL on error.
 *
 * \note The objects in the container must be treated as read-only.
 */
struct ao2_container *ast_mwi_mailbox_get_all(void);


/* ------------------------------------------------------------------- */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_RES_MWI_EXTERNAL_H */
