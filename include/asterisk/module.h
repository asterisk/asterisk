/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Every module should provide these functions */

/*! 
 * \brief Initialize the module.
 * 
 * This function is called at module load time.  Put all code in here
 * that needs to set up your module's hardware, software, registrations,
 * etc.
 *
 * \return This function should return 0 on success and non-zero on failure.
 * If the module is not loaded successfully, Asterisk will call its
 * unload_module() function.
 */
int load_module(void);

/*! 
 * \brief Cleanup all module structures, sockets, etc.
 *
 * This is called at exit.  Any registrations and memory allocations need to be
 * unregistered and free'd here.  Nothing else will do these for you (until
 * exit).
 *
 * \return Zero on success, or non-zero on error.
 */
int unload_module(void);

/*! 
 * \brief Provides a usecount.
 *
 * This function will be called by various parts of asterisk.  Basically, all
 * it has to do is to return a usecount when called.  You will need to maintain
 * your usecount within the module somewhere.  The usecount should be how many
 * channels provided by this module are in use.
 *
 * \return The module's usecount.
 */
int usecount(void);			/* How many channels provided by this module are in use? */

/*! \brief Provides a description of the module.
 *
 * \return a short description of your module
 */
char *description(void);		/* Description of this module */

/*! 
 * \brief Returns the ASTERISK_GPL_KEY
 *
 * This returns the ASTERISK_GPL_KEY, signifiying that you agree to the terms of
 * the GPL stated in the ASTERISK_GPL_KEY.  Your module will not load if it does
 * not return the EXACT message:
 *
 * \code
 * char *key(void) {
 *         return ASTERISK_GPL_KEY;
 * }
 * \endcode
 *
 * \return ASTERISK_GPL_KEY
 */
char *key(void);		/* Return the below mentioned key, unmodified */

/*! 
 * \brief Reload stuff.
 *
 * This function is where any reload routines take place.  Re-read config files,
 * change signalling, whatever is appropriate on a reload.
 *
 * \return The return value is not used.
 */
int reload(void);		/* reload configs */

/*! \brief The text the key() function should return. */
#define ASTERISK_GPL_KEY \
	"This paragraph is Copyright (C) 2000, Linux Support Services, Inc.  \
In order for your module to load, it must return this key via a function \
called \"key\".  Any code which includes this paragraph must be licensed under \
the GNU General Public License version 2 or later (at your option).   Linux \
Support Services, Inc. reserves the right to allow other parties to license \
this paragraph under other terms as well."

#define AST_MODULE_CONFIG "modules.conf" /*!< \brief Module configuration file */

/*! 
 * \brief Softly unload a module.
 *
 * This flag signals ast_unload_resource() to unload a module only if it is not
 * in use, according to the module's usecount.
 */
#define AST_FORCE_SOFT 0

/*! 
 * \brief Firmly unload a module.
 *
 * This flag signals ast_unload_resource() to attempt to unload a module even
 * if it is in use.  It will attempt to use the module's unload_module
 * function.
 */
#define AST_FORCE_FIRM 1

/*! 
 * \brief Unconditionally unload a module.
 *
 * This flag signals ast_unload_resource() to first attempt to unload a module
 * using the module's unload_module function, then if that fails to unload the
 * module using dlclose.  The module will be unloaded even if it is still in
 * use.  Use of this flag is not recommended.
 */
#define AST_FORCE_HARD 2

/*! 
 * \brief Load a module.
 * \param resource_name The filename of the module to load.
 *
 * This function is run by the PBX to load the modules.  It performs
 * all loading and initilization tasks.   Basically, to load a module, just
 * give it the name of the module and it will do the rest.
 *
 * \return Zero on success, -1 on error.
 */
int ast_load_resource(const char *resource_name);

/*! 
 * \brief Unloads a module.
 * \param resource_name The name of the module to unload.
 * \param force The force flag.  This should be set using one of the AST_FORCE*
 *        flags.
 *
 * This function unloads a module.  It will only unload modules that are not in
 * use (usecount not zero), unless #AST_FORCE_FIRM or #AST_FORCE_HARD is 
 * specified.  Setting #AST_FORCE_FIRM or #AST_FORCE_HARD will unload the
 * module regardless of consequences (NOT_RECOMMENDED).
 *
 * \return Zero on success, -1 on error.
 */
int ast_unload_resource(const char *resource_name, int force);

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
 * \param The updater function to unregister.
 *
 * This removes the given function from the updater list.
 * 
 * \return Zero on success, -1 on failure.
 */
int ast_loader_unregister(int (*updater)(void));

/*! 
 * \brief Reload asterisk modules.
 * \param name the name of the module to reload
 *
 * This function reloads the specified module, or if no modules are specified,
 * it will reload all loaded modules.
 *
 * \note Modules are reloaded using their reload() functions, not unloading
 * them and loading them again.
 *
 * \return Zero if the specified module was not found, 1 if the module was
 * found but cannot be reloaded, -1 if a reload operation is already in
 * progress, and 2 if the specfied module was found and reloaded.
 */
int ast_module_reload(const char *name);

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
char *ast_module_helper(char *line, char *word, int pos, int state, int rpos, int needsreload);

/*! 
 * \brief Register a function to be executed before Asterisk exits.
 * \param func The callback function to use.
 *
 * \return Zero on success, -1 on error.
 */
int ast_register_atexit(void (*func)(void));

/*! 
 * \brief Unregister a function registered with ast_register_atexit().
 * \param func The callback function to unregister.
 */
void ast_unregister_atexit(void (*func)(void));

/* Local user routines keep track of which channels are using a given module
   resource.  They can help make removing modules safer, particularly if
   they're in use at the time they have been requested to be removed */

/*! 
 * \brief Standard localuser struct definition.
 *
 * This macro defines a localuser struct.  The channel.h file must be included
 * to use this macro because it refrences ast_channel.
 */
#define STANDARD_LOCAL_USER struct localuser { \
						struct ast_channel *chan; \
						struct localuser *next; \
					     }

/*! 
 * \brief The localuser declaration.
 *
 * This macro should be used in combination with #STANDARD_LOCAL_USER.  It
 * creates a localuser mutex and several other variables used for keeping the
 * use count.
 *
 * <b>Sample Usage:</b>
 * \code
 * STANDARD_LOCAL_USER;
 * LOCAL_USER_DECL;
 * \endcode
 */
#define LOCAL_USER_DECL AST_MUTEX_DEFINE_STATIC(localuser_lock); \
						static struct localuser *localusers = NULL; \
						static int localusecnt = 0;

/*! 
 * \brief Add a localuser.
 * \param u a pointer to a localuser struct
 *
 * This macro adds a localuser to the list of users and increments the
 * usecount.  It expects a variable named \p chan of type \p ast_channel in the
 * current scope.
 *
 * \note This function dynamically allocates memory.  If this operation fails
 * it will cause your function to return -1 to the caller.
 */
#define LOCAL_USER_ADD(u) { \
 \
	if (!(u=calloc(1,sizeof(*u)))) { \
		ast_log(LOG_WARNING, "Out of memory\n"); \
		return -1; \
	} \
	ast_mutex_lock(&localuser_lock); \
	u->chan = chan; \
	u->next = localusers; \
	localusers = u; \
	localusecnt++; \
	ast_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

#define LOCAL_USER_ACF_ADD(u) { \
 \
	if (!(u=calloc(1,sizeof(*u)))) { \
		ast_log(LOG_WARNING, "Out of memory\n"); \
		return ""; \
	} \
	ast_mutex_lock(&localuser_lock); \
	u->chan = chan; \
	u->next = localusers; \
	localusers = u; \
	localusecnt++; \
	ast_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

/*! 
 * \brief Remove a localuser.
 * \param u the user to add, should be of type struct localuser
 *
 * This macro removes a localuser from the list of users and decrements the
 * usecount.
 */
#define LOCAL_USER_REMOVE(u) { \
	struct localuser *uc, *ul = NULL; \
	ast_mutex_lock(&localuser_lock); \
	uc = localusers; \
	while (uc) { \
		if (uc == u) { \
			if (ul) \
				ul->next = uc->next; \
			else \
				localusers = uc->next; \
			break; \
		} \
		ul = uc; \
		uc = uc->next; \
	}\
	free(u); \
	localusecnt--; \
	ast_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

/*! 
 * \brief Hangup all localusers.
 *
 * This macro hangs up on all current localusers and sets the usecount to zero
 * when finished.
 */
#define STANDARD_HANGUP_LOCALUSERS { \
	struct localuser *u, *ul; \
	ast_mutex_lock(&localuser_lock); \
	u = localusers; \
	while(u) { \
		ast_softhangup(u->chan, AST_SOFTHANGUP_APPUNLOAD); \
		ul = u; \
		u = u->next; \
		free(ul); \
	} \
	ast_mutex_unlock(&localuser_lock); \
	localusecnt=0; \
}

/*!
 * \brief Set the specfied integer to the current usecount.
 * \param res the integer variable to set.
 *
 * This macro sets the specfied integer variable to the local usecount.  It
 * handles all the necessary thread synchronization.
 *
 * <b>Sample Usage:</b>
 * \code
 * int usecount(void)
 * {
 *    int res;
 *    STANDARD_USECOUNT(res);
 *    return res;
 * }
 * \endcode
 */
#define STANDARD_USECOUNT(res) { \
	res = localusecnt; \
}
	
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MODULE_H */
