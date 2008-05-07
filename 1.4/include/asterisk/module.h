/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 * Luigi Rizzo <rizzo@icir.org>
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
 * \brief Asterisk module definitions.
 *
 * This file contains the definitons for functions Asterisk modules should
 * provide and some other module related functions.
 */

#ifndef _ASTERISK_MODULE_H
#define _ASTERISK_MODULE_H

#include "asterisk/utils.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief The text the key() function should return. */
#define ASTERISK_GPL_KEY \
"This paragraph is copyright (c) 2006 by Digium, Inc. \
In order for your module to load, it must return this \
key via a function called \"key\".  Any code which \
includes this paragraph must be licensed under the GNU \
General Public License version 2 or later (at your \
option).  In addition to Digium's general reservations \
of rights, Digium expressly reserves the right to \
allow other parties to license this paragraph under \
different terms. Any use of Digium, Inc. trademarks or \
logos (including \"Asterisk\" or \"Digium\") without \
express written permission of Digium, Inc. is prohibited.\n"

#define AST_MODULE_CONFIG "modules.conf" /*!< \brief Module configuration file */

enum ast_module_unload_mode {
	AST_FORCE_SOFT = 0, /*!< Softly unload a module, only if not in use */
	AST_FORCE_FIRM = 1, /*!< Firmly unload a module, even if in use */
	AST_FORCE_HARD = 2, /*!< as FIRM, plus dlclose() on the module. Not recommended
				as it may cause crashes */
};

enum ast_module_load_result {
	AST_MODULE_LOAD_SUCCESS = 0,	/*!< Module loaded and configured */
	AST_MODULE_LOAD_DECLINE = 1,	/*!< Module is not configured */
	AST_MODULE_LOAD_SKIP = 2,	/*!< Module was skipped for some reason */
	AST_MODULE_LOAD_FAILURE = -1,	/*!< Module could not be loaded properly */
};

/*! 
 * \brief Load a module.
 * \param resource_name The name of the module to load.
 *
 * This function is run by the PBX to load the modules.  It performs
 * all loading and initilization tasks.   Basically, to load a module, just
 * give it the name of the module and it will do the rest.
 *
 * \return See possible enum values for ast_module_load_result.
 */
enum ast_module_load_result ast_load_resource(const char *resource_name);

/*! 
 * \brief Unload a module.
 * \param resource_name The name of the module to unload.
 * \param ast_module_unload_mode The force flag. This should be set using one of the AST_FORCE flags.
 *
 * This function unloads a module.  It will only unload modules that are not in
 * use (usecount not zero), unless #AST_FORCE_FIRM or #AST_FORCE_HARD is 
 * specified.  Setting #AST_FORCE_FIRM or #AST_FORCE_HARD will unload the
 * module regardless of consequences (NOT RECOMMENDED).
 *
 * \return Zero on success, -1 on error.
 */
int ast_unload_resource(const char *resource_name, enum ast_module_unload_mode);

/*! 
 * \brief Notify when usecount has been changed.
 *
 * This function calulates use counts and notifies anyone trying to keep track
 * of them.  It should be called whenever your module's usecount changes.
 *
 * \note The LOCAL_USER macros take care of calling this function for you.
 */
void ast_update_use_count(void);

/*! 
 * \brief Ask for a list of modules, descriptions, and use counts.
 * \param modentry A callback to an updater function.
 * \param like
 *
 * For each of the modules loaded, modentry will be executed with the resource,
 * description, and usecount values of each particular module.
 * 
 * \return the number of modules loaded
 */
int ast_update_module_list(int (*modentry)(const char *module, const char *description, int usecnt, const char *like),
			   const char *like);

/*! 
 * \brief Add a procedure to be run when modules have been updated.
 * \param updater The function to run when modules have been updated.
 *
 * This function adds the given function to a linked list of functions to be
 * run when the modules are updated. 
 *
 * \return Zero on success and -1 on failure.
 */
int ast_loader_register(int (*updater)(void));

/*! 
 * \brief Remove a procedure to be run when modules are updated.
 * \param updater The updater function to unregister.
 *
 * This removes the given function from the updater list.
 * 
 * \return Zero on success, -1 on failure.
 */
int ast_loader_unregister(int (*updater)(void));

/*!
 * \brief Run the unload() callback for all loaded modules
 *
 * This function should be called when Asterisk is shutting down gracefully.
 */
void ast_module_shutdown(void);

/*! 
 * \brief Match modules names for the Asterisk cli.
 * \param line Unused by this function, but this should be the line we are
 *        matching.
 * \param word The partial name to match. 
 * \param pos The position the word we are completing is in.
 * \param state The possible match to return.
 * \param rpos The position we should be matching.  This should be the same as
 *        pos.
 * \param needsreload This should be 1 if we need to reload this module and 0
 *        otherwise.  This function will only return modules that are reloadble
 *        if this is 1.
 *
 * \return A possible completion of the partial match, or NULL if no matches
 * were found.
 */
char *ast_module_helper(const char *line, const char *word, int pos, int state, int rpos, int needsreload);

/* Opaque type for module handles generated by the loader */

struct ast_module;

/* User count routines keep track of which channels are using a given module
   resource.  They can help make removing modules safer, particularly if
   they're in use at the time they have been requested to be removed */

struct ast_module_user;
struct ast_module_user_list;

/*! \page ModMngmnt The Asterisk Module management interface
 *
 * All modules must implement the module API (load, unload...)
 * whose functions are exported through fields of a "struct module_symbol";
 */

enum ast_module_flags {
	AST_MODFLAG_DEFAULT = 0,
	AST_MODFLAG_GLOBAL_SYMBOLS = (1 << 0),
	AST_MODFLAG_BUILDSUM = (1 << 1),
};

struct ast_module_info {

	/*!
	 * The 'self' pointer for a module; it will be set by the loader before
	 * it calls the module's load_module() entrypoint, and used by various
	 * other macros that need to identify the module.
	 */

	struct ast_module *self;
	enum ast_module_load_result (*load)(void);	/* register stuff etc. Optional. */
	int (*reload)(void);			/* config etc. Optional. */
	int (*unload)(void);			/* unload. called with the module locked */
	const char *name;			/* name of the module for loader reference and CLI commands */
	const char *description;		/* user friendly description of the module. */

	/*! 
	 * This holds the ASTERISK_GPL_KEY, signifiying that you agree to the terms of
	 * the Asterisk license as stated in the ASTERISK_GPL_KEY.  Your module will not
	 * load if it does not return the EXACT key string.
	 */

	const char *key;
	unsigned int flags;

	/*! The value of AST_BUILDOPT_SUM when this module was compiled */
	const char buildopt_sum[33];
};

void ast_module_register(const struct ast_module_info *);
void ast_module_unregister(const struct ast_module_info *);

struct ast_module_user *__ast_module_user_add(struct ast_module *, struct ast_channel *);
void __ast_module_user_remove(struct ast_module *, struct ast_module_user *);
void __ast_module_user_hangup_all(struct ast_module *);

#define ast_module_user_add(chan) __ast_module_user_add(ast_module_info->self, chan)
#define ast_module_user_remove(user) __ast_module_user_remove(ast_module_info->self, user)
#define ast_module_user_hangup_all() __ast_module_user_hangup_all(ast_module_info->self)

struct ast_module *ast_module_ref(struct ast_module *);
void ast_module_unref(struct ast_module *);

#if defined(__cplusplus) || defined(c_plusplus)
#define AST_MODULE_INFO(keystr, flags_to_set, desc, load_func, unload_func, reload_func)	\
	static struct ast_module_info __mod_info = {	\
		NULL,					\
		load_func,				\
		reload_func,				\
		unload_func,				\
		AST_MODULE,				\
		desc,					\
		keystr,					\
		flags_to_set | AST_MODFLAG_BUILDSUM,	\
		AST_BUILDOPT_SUM,			\
	};						\
	static void  __attribute__ ((constructor)) __reg_module(void) \
	{ \
		ast_module_register(&__mod_info); \
	} \
	static void  __attribute__ ((destructor)) __unreg_module(void) \
	{ \
		ast_module_unregister(&__mod_info); \
	} \
	const static __attribute__((unused)) struct ast_module_info *ast_module_info = &__mod_info

#define AST_MODULE_INFO_STANDARD(keystr, desc)		\
	AST_MODULE_INFO(keystr, AST_MODFLAG_DEFAULT, desc,	\
			load_module,			\
			unload_module,		\
			NULL			\
		       )
#else
/* forward declare this pointer in modules, so that macro/function
   calls that need it can get it, since it will actually be declared
   and populated at the end of the module's source file... */
const static __attribute__((unused)) struct ast_module_info *ast_module_info;

#define AST_MODULE_INFO(keystr, flags_to_set, desc, fields...)	\
	static struct ast_module_info __mod_info = {		\
		.name = AST_MODULE,				\
		.flags = flags_to_set | AST_MODFLAG_BUILDSUM,	\
		.description = desc,				\
		.key = keystr,					\
		.buildopt_sum = AST_BUILDOPT_SUM,		\
		fields						\
	};							\
	static void  __attribute__ ((constructor)) __reg_module(void) \
	{ \
		ast_module_register(&__mod_info); \
	} \
	static void  __attribute__ ((destructor)) __unreg_module(void) \
	{ \
		ast_module_unregister(&__mod_info); \
	} \
	const static struct ast_module_info *ast_module_info = &__mod_info

#define AST_MODULE_INFO_STANDARD(keystr, desc)		\
	AST_MODULE_INFO(keystr, AST_MODFLAG_DEFAULT, desc,	\
			.load = load_module,			\
			.unload = unload_module,		\
		       )
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MODULE_H */
