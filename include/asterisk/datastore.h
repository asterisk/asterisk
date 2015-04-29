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
	struct ast_module_instance *instance;	/*!< The module that provides the datastore */
	const struct ast_datastore_info *info;	/*!< Data store type information */
	unsigned int inheritance;		/*!< Number of levels this item will continue to be inherited */
	AST_LIST_ENTRY(ast_datastore) entry; 	/*!< Used for easy linking */
};

/*!
 * \brief Create a data store object
 * \param[in] info information describing the data store object
 * \param[in] uid unique identifer
 * \param file, line, function
 * \version 1.6.1 moved here and renamed from ast_channel_datastore_alloc
 */
struct ast_datastore * attribute_malloc __ast_datastore_alloc(
	const struct ast_datastore_info *info, const char *uid, struct ast_module *module,
	const char *file, int line, const char *function);

#define ast_datastore_alloc(info, uid) \
	__ast_datastore_alloc(info, uid, AST_MODULE_SELF, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Free a data store object
 * \param[in] datastore datastore to free
 * \version 1.6.1 moved here and renamed from ast_channel_datastore_free
 */
int ast_datastore_free(struct ast_datastore *datastore);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DATASTORE_H */
