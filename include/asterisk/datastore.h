/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
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
 * \brief Asterisk datastore objects
 */

#ifndef _ASTERISK_DATASTORE_H
#define _ASTERISK_DATASTORE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/linkedlists.h"

/*! \brief Structure for a data store type */
struct ast_datastore_info {
	const char *type;			/*!< Type of data store */
	void *(*duplicate)(void *data);		/*!< Duplicate item data (used for inheritance) */
	void (*destroy)(void *data);		/*!< Destroy function */

	/*!
	 * \brief Fix up channel references on the masquerading channel
	 *
	 * \arg data The datastore data
	 * \arg old_chan The old channel owning the datastore
	 * \arg new_chan The new channel owning the datastore
	 *
	 * This is exactly like the fixup callback of the channel technology interface.
	 * It allows a datastore to fix any pointers it saved to the owning channel
	 * in case that the owning channel has changed.  Generally, this would happen
	 * when the datastore is set to be inherited, and a masquerade occurs.
	 *
	 * \return nothing.
	 */
	void (*chan_fixup)(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan);

	/*!
	 * \brief Fix up channel references on the channel being masqueraded into
	 *
	 * \arg data The datastore data
	 * \arg old_chan The old channel owning the datastore
	 * \arg new_chan The new channel owning the datastore
	 *
	 * This is the same as the above callback, except it is called for the channel
	 * being masqueraded into instead of the channel that is masquerading.
	 *
	 * \return nothing.
	 */
	void (*chan_breakdown)(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan);
};

/*! \brief Structure for a data store object */
struct ast_datastore {
	const char *uid;			/*!< Unique data store identifier */
	void *data;				/*!< Contained data */
	const struct ast_datastore_info *info;	/*!< Data store type information */
	struct ast_module *mod;			/*!< Module referenced by this datastore */
	unsigned int inheritance;		/*!< Number of levels this item will continue to be inherited */
	AST_LIST_ENTRY(ast_datastore) entry; 	/*!< Used for easy linking */
};

/*!
 * \brief Create a data store object
 * \param[in] info information describing the data store object
 * \param[in] uid unique identifer
 * \param[in] mod The module to hold until this datastore is freed.
 * \param file, line, function
 * \version 1.6.1 moved here and renamed from ast_channel_datastore_alloc
 */
struct ast_datastore *__ast_datastore_alloc(
	const struct ast_datastore_info *info, const char *uid, struct ast_module *mod,
	const char *file, int line, const char *function);

#define ast_datastore_alloc(info, uid) \
	__ast_datastore_alloc(info, uid, AST_MODULE_SELF, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Free a data store object
 * \param[in] datastore datastore to free
 * \version 1.6.1 moved here and renamed from ast_channel_datastore_free
 */
int ast_datastore_free(struct ast_datastore *datastore);

/*!
 * \brief Allocate a specialized data stores container
 *
 * \return a container for storing data stores
 *
 * \since 14.0.0
 */
struct ao2_container *ast_datastores_alloc(void);

/*!
 * \brief Add a data store to a container
 *
 * \param[in] datastores container to store datastore in
 * \param[in] datastore datastore to add
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \since 14.0.0
 */
int ast_datastores_add(struct ao2_container *datastores, struct ast_datastore *datastore);

/*!
 * \brief Remove a data store from a container
 *
 * \param[in] datastores container to remove datastore from
 * \param[in] name name of the data store to remove
 *
 * \since 14.0.0
 */
void ast_datastores_remove(struct ao2_container *datastores, const char *name);

/*!
 * \brief Find a data store in a container
 *
 * \param[in] datastores container to find datastore in
 * \param[in] name name of the data store to find
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 14.0.0
 */
struct ast_datastore *ast_datastores_find(struct ao2_container *datastores, const char *name);

/*!
 * \brief Allocate a datastore for use with the datastores container
 *
 * \param[in] info information about the datastore
 * \param[in] uid unique identifier for the datastore
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 14.0.0
 */
struct ast_datastore *ast_datastores_alloc_datastore(const struct ast_datastore_info *info, const char *uid);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DATASTORE_H */
