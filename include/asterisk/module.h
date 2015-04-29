/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2015, Digium, Inc.
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

/*! \li \ref module.h uses the configuration file \ref modules.conf
 * \addtogroup configuration_file
 */

/*! \page modules.conf modules.conf
 * \verbinclude modules.conf.sample
 */

#ifndef _ASTERISK_MODULE_H
#define _ASTERISK_MODULE_H

#include "asterisk/utils.h"
#include "asterisk/vector.h"

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

struct ast_module_instance;

enum ast_module_load_result {
	AST_MODULE_LOAD_SUCCESS = 0,    /*!< Module loaded and configured */
	AST_MODULE_LOAD_DECLINE = 1,    /*!< Module is not configured */
	AST_MODULE_LOAD_FAILURE = -1,   /*!< Module could not be loaded properly */
};

/*!
 * \since 12
 * \brief Possible return types for \ref ast_module_reload
 */
enum ast_module_reload_result {
	AST_MODULE_RELOAD_SUCCESS = 0,      /*!< The module was reloaded succesfully */
	AST_MODULE_RELOAD_QUEUED,           /*!< The module reload request was queued */
	AST_MODULE_RELOAD_NOT_FOUND,        /*!< The requested module was not found */
	AST_MODULE_RELOAD_ERROR,            /*!< An error occurred while reloading the module */
	AST_MODULE_RELOAD_IN_PROGRESS,      /*!< A module reload request is already in progress */
	AST_MODULE_RELOAD_UNINITIALIZED,    /*!< The module has not been initialized */
	AST_MODULE_RELOAD_NOT_IMPLEMENTED,  /*!< This module doesn't support reloading */
};

enum ast_module_support_level {
	AST_MODULE_SUPPORT_UNKNOWN,
	AST_MODULE_SUPPORT_CORE,
	AST_MODULE_SUPPORT_EXTENDED,
	AST_MODULE_SUPPORT_DEPRECATED,
};

/*!
 * \brief Run the unload() callback for all loaded modules
 *
 * This function should be called when Asterisk is shutting down gracefully.
 */
void ast_module_shutdown(void);

AST_VECTOR(ast_string_vector, char *);

enum ast_module_complete_filter {
	AST_MODULE_COMPLETE_NONE = 0,
	AST_MODULE_COMPLETE_UNLOADED = 0x01,
	AST_MODULE_COMPLETE_LOADED = 0x02,
	AST_MODULE_COMPLETE_RELOADABLE = 0x04,
	AST_MODULE_COMPLETE_ADMINLOADED = 0x08,
	AST_MODULE_COMPLETE_CANLOAD = 0x10,
	AST_MODULE_COMPLETE_ALL = 0x1F,
};

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
 * \retval A possible completion of the partial match.
 * \retval NULL if no matches were found.
 */
char *ast_module_complete(const char *line, const char *word, int pos, int state, int rpos,
	enum ast_module_complete_filter filter);

/*!
 * \brief Get the name of a module.
 * \param module A pointer to the module.
 * \return the name of the module
 * \retval NULL if module is NULL
 */
const char *ast_module_name(const struct ast_module *module);

enum ast_module_load_priority {
	AST_MODPRI_REALTIME_DEPEND =    10,  /*!< Dependency for a realtime driver */
	AST_MODPRI_REALTIME_DEPEND2 =   20,  /*!< Second level dependency for a realtime driver (func_curl needs res_curl, but is needed by res_config_curl) */
	AST_MODPRI_REALTIME_DRIVER =    30,  /*!< A realtime driver, which provides configuration services for other modules */
	AST_MODPRI_TIMING =             40,  /*!< Dependency for a channel (MOH needs timing interfaces to be fully loaded) */
	AST_MODPRI_CHANNEL_DEPEND =     50,  /*!< Channel driver dependency (may depend upon realtime, e.g. MOH) */
	AST_MODPRI_CHANNEL_DRIVER =     60,  /*!< Channel drivers (provide devicestate) */
	AST_MODPRI_APP_DEPEND =         70,  /*!< Dependency for an application */
	AST_MODPRI_DEVSTATE_PROVIDER =  80,  /*!< Applications and other modules that _provide_ devicestate (e.g. meetme) */
	AST_MODPRI_DEVSTATE_PLUGIN =    90,  /*!< Plugin for a module that provides devstate (e.g. res_calendar_*) */
	AST_MODPRI_CDR_DRIVER =        100,  /*!< CDR or CEL backend */
	AST_MODPRI_DEFAULT =           128,  /*!< Modules not otherwise defined (such as most apps) will load here */
	AST_MODPRI_DEVSTATE_CONSUMER = 150,  /*!< Certain modules, which consume devstate, need to load after all others (e.g. app_queue) */
};

/*! Make the module crash safe before the instance is made available. */
typedef int (*ast_module_init_fn)(void);

/*! Error returns should be avoided here, the instance is already available. */
typedef int (*ast_module_start_fn)(void);

/*! Module reload callback. */
typedef int (*ast_module_reload_fn)(void);

/*! Called when the module is stopping, before dlclose is run. */
typedef void (*ast_module_stop_fn)(void);

/*!
 * \brief Called by module to try clearing a disposer.
 *
 * \retval 0 Remove the disposer from the list (does not release the alloc reference).
 * \retval non-zero Leave the disposer in the users list.
 *
 * \note It's safe to return 0 even if \ref ast_module_disposer_destroy will be called
 * later.  Returning 0 will prevent the callback from being used again.
 */
typedef int (*ast_module_dispose_cb)(void *userdata, int level);

/*! Opaque */
struct ast_module_disposer;

/*! Shared type for disposer vector. */
AST_VECTOR_RW(ast_module_disposers_rw, struct ast_module_disposer *);

/*!
 * \brief Allocate and register a disposer callback with a module instance.
 *
 * \param instance Module instance to accept dispose requests for.
 * \param userdata Pointer passed to the callback.
 * \param cb Function to be called when the module wants to unload.
 *
 * \return Reference counted disposer object.
 *
 * \note This should be matched with a call to ast_module_disposer_destroy.  This
 * clears the allocation reference and removes the item from the list.
 */
struct ast_module_disposer *ast_module_disposer_alloc(struct ast_module_instance *instance, void *userdata, ast_module_dispose_cb cb);

/*! Release the allocation reference, remove from users vector if still listed. */
void ast_module_disposer_destroy(struct ast_module_disposer *disposer);

/*!
 * \brief Prevent unload of the module before shutdown
 * \param mod Module to hold
 */
void ast_module_block_unload(struct ast_module *module);

int ast_module_instance_refs(struct ast_module *module);

/*!
 * \internal Used by AST_MODULE_INFO macros for module initialization.
 *
 * \note None of these symbols should be directly used.
 * @{
 */
#define MODULE_INFO_INIT_FN0 NULL
#define MODULE_INFO_INIT_FN1 init_module
#define MODULE_INFO_START_FN0 NULL
#define MODULE_INFO_START_FN1 load_module
#define MODULE_INFO_RELOAD_FN0 NULL
#define MODULE_INFO_RELOAD_FN1 reload_module
#define MODULE_INFO_STOP_FN0 NULL
#define MODULE_INFO_STOP_FN1 unload_module

int __ast_module_register(struct ast_module **self, const char *name,
	const char *buildopt_sum, const char *manifest_checksum,
	const char *keystr, const char *desc,
	ast_module_init_fn init_fn,
	ast_module_start_fn start_fn,
	ast_module_reload_fn reload_fn,
	ast_module_stop_fn stop_fn
);

void __ast_module_unregister(struct ast_module **self);

#define AST_MODULE_INFO(keystr, desc, has_init, has_start, has_reload, has_stop) \
	static struct ast_module *__module_self = NULL; \
	struct ast_module *AST_MODULE_SELF_SYM(void) \
	{ \
		return __module_self; \
	} \
	static void __attribute__((constructor)) __module_reg(void) \
	{ \
		__ast_module_register(&__module_self, \
			AST_MODULE, AST_BUILDOPT_SUM, AST_MODULE_CHECKSUM, keystr, desc, \
			MODULE_INFO_INIT_FN ## has_init, \
			MODULE_INFO_START_FN ## has_start, \
			MODULE_INFO_RELOAD_FN ## has_reload, \
			MODULE_INFO_STOP_FN ## has_stop); \
	} \
	static void __attribute__((destructor)) __module_unreg(void) \
	{ \
		__ast_module_unregister(&__module_self); \
	}
/*! @} */

#define AST_MODULE_INFO_SYMBOLS_ONLY(keystr, desc) \
	AST_MODULE_INFO(keystr, desc, 0, 0, 0, 0)

#define AST_MODULE_INFO_AUTOCLEAN(keystr, desc) \
	AST_MODULE_INFO(keystr, desc, 0, 1, 0, 0)

#define AST_MODULE_INFO_STANDARD(keystr, desc) \
	AST_MODULE_INFO(keystr, desc, 0, 1, 0, 1)

#define AST_MODULE_INFO_RELOADABLE(keystr, desc) \
	AST_MODULE_INFO(keystr, desc, 0, 1, 1, 1)

#define AST_MODULE_INFO_AUTOCLEAN_RELOADABLE(keystr, desc) \
	AST_MODULE_INFO(keystr, desc, 0, 1, 1, 0)


/*! 
 * \brief Register an application.
 *
 * \param app Short name of the application
 * \param execute a function callback to execute the application. It should return
 *                non-zero if the channel needs to be hung up.
 * \param synopsis a short description (one line synopsis) of the application
 * \param description long description with all of the details about the use of 
 *                    the application
 * 
 * This registers an application with Asterisk's internal application list. 
 * \note The individual applications themselves are responsible for registering and unregistering
 *       and unregistering their own CLI commands.
 * 
 * \retval 0 success 
 * \retval -1 failure.
 */
#define ast_register_application(app, execute, synopsis, description) ast_register_application2(app, execute, synopsis, description, AST_MODULE_SELF)

/*! 
 * \brief Register an application using XML documentation.
 *
 * \param app Short name of the application
 * \param execute a function callback to execute the application. It should return
 *                non-zero if the channel needs to be hung up.
 * 
 * This registers an application with Asterisk's internal application list. 
 * \note The individual applications themselves are responsible for registering and unregistering
 *       and unregistering their own CLI commands.
 * 
 * \retval 0 success 
 * \retval -1 failure.
 */
#define ast_register_application_xml(app, execute) ast_register_application(app, execute, NULL, NULL)

/*!
 * \brief Register an application.
 *
 * \param app Short name of the application
 * \param execute a function callback to execute the application. It should return
 *                non-zero if the channel needs to be hung up.
 * \param synopsis a short description (one line synopsis) of the application
 * \param description long description with all of the details about the use of
 *                    the application
 * \param mod module this application belongs to
 *
 * This registers an application with Asterisk's internal application list.
 * \note The individual applications themselves are responsible for registering and unregistering
 *       and unregistering their own CLI commands.
 *
 * \retval 0 success
 * \retval -1 failure.
 */
int ast_register_application2(const char *app, int (*execute)(struct ast_channel *, const char *),
				     const char *synopsis, const char *description, struct ast_module *module);

/*! 
 * \brief Unregister an application
 * 
 * \param app name of the application (does not have to be the same string as the one that was registered)
 * 
 * This unregisters an application from Asterisk's internal application list.
 * 
 * \retval 0 success 
 * \retval -1 failure
 */
int ast_unregister_application(const char *app);

const char *ast_module_support_level_to_string(const struct ast_module *module);

/*!
 * \brief Find an ast_module by name.
 *
 * \return A reference to the module or NULL.
 */
#define ast_module_find(name) \
	__ast_module_find(name, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_module *__ast_module_find(const char *name,
	const char *file, int line, const char *func);

/*!
 * \brief Find the ast_module that provides an API.
 *
 * \param type The type of API to find, for example "application" for dialplan apps.
 * \param id The name of the item to find, for example "Gosub".
 *
 * \return A reference to the module that provides the API or NULL.
 */
#define ast_module_find_provider(type, id) \
	__ast_module_find_provider(type, id, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_module *__ast_module_find_provider(const char *type, const char *id,
	const char *file, int line, const char *func);



const char *ast_module_description(const struct ast_module *module);
enum ast_module_support_level ast_module_support_level(const struct ast_module *module);
enum ast_module_load_priority ast_module_load_priority(const struct ast_module *module);

/*! Check if a module exports global symbols.  This does not require a lock. */
int ast_module_exports_globals(const struct ast_module *module);

/*! Check if the module is blocked from unload. */
int ast_module_unload_is_blocked(const struct ast_module *module);

/*!
 * \brief Check if a module is "currently" running.
 *
 * \note The value returned may be lagged if the module is currently starting or
 * stopping.  This should not be used for logic, it is meant for output to users
 * or monitoring systems.
 */
int ast_module_is_running(struct ast_module *module);

/*!
 * \brief Manipulate the reference count of an instance using it's module.
 *
 * \param module The module
 * \param delta Number of references to add/subtract from the instance.
 *
 * \retval -2 Invalid module object.
 * \retval -1 The module is not running.
 * \retval >1 Number of references to the instance before this operation.
 *
 * A delta of 0 can be used to check the reference count.
 */
#define ast_module_ref_instance(module, delta) \
	__ast_module_ref_instance(module, delta, __FILE__, __LINE__, __PRETTY_FUNCTION__)
int __ast_module_ref_instance(struct ast_module *module, int delta, const char *file, int line, const char *func);

/*! Get a reference the module's instance. */
#define ast_module_get_instance(module) \
	__ast_module_get_instance(module, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_module_instance *__ast_module_get_instance(struct ast_module *module, const char *file, int line, const char *func);

/*! Get a reference the module's lib. */
#define ast_module_get_lib_loaded(module) \
	__ast_module_get_lib_loaded(module, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_module_lib *__ast_module_get_lib_loaded(struct ast_module *module, const char *file, int line, const char *func);

/*! Get a reference the module's lib if it's running. */
#define ast_module_get_lib_running(module) \
	__ast_module_get_lib_running(module, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_module_lib *__ast_module_get_lib_running(struct ast_module *module, const char *file, int line, const char *func);

/*! Get a reference the module's instance from it's lib. */
#define ast_module_lib_get_instance(lib) \
	__ast_module_lib_get_instance(lib, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_module_instance *__ast_module_lib_get_instance(struct ast_module_lib *lib, const char *file, int line, const char *func);

/*! Manipulate the reference count of an instance using it's lib. */
#define ast_module_lib_ref_instance(lib, delta) \
	__ast_module_lib_ref_instance(lib, delta, __FILE__, __LINE__, __PRETTY_FUNCTION__)
int __ast_module_lib_ref_instance(struct ast_module_lib *lib, int delta, const char *file, int line, const char *func);

/*!
 * \brief Subscribe to notification that the module is not running.
 *
 * \param lib ast_module_lib for the running module.
 * \param callback ao2_weakproxy_notification_cb to be called when the module will stop.
 * \param userdata Any pointer, passed to the callback.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note callback will run immediately if the module is not currently running, even if it
 * is loaded.
 *
 * \note callback is run once and discarded.  See ao2_weakproxy_subscribe for details.
 *
 * \warning lib will be locked.  It is only safe to run this with the instance locked
 * if the lib was already locked first.
 */
#define ast_module_lib_subscribe_stop(lib, callback, userdata) ({ \
	struct ast_module_lib *__lib = (lib); \
	ao2_weakproxy_subscribe(__lib, (callback), (userdata), 0); \
})

/*!
 * \brief Unsubscribe to notification that the module is not running.
 *
 * Parameters should be identical to those used with ast_module_lib_subscribe_stop.
 *
 * \retval 1 Success (1 subscription removed)
 * \retval 0 Subscription not found
 * \retval -1 Failure
 *
 * \warning lib will be locked.  It is only safe to run this with the instance locked
 * if the lib was already locked first.
 */
#define ast_module_lib_unsubscribe_stop(lib, callback, userdata) ({ \
	struct ast_module_lib *__lib = (lib); \
	ao2_weakproxy_unsubscribe(__lib, (callback), (userdata), 0); \
})

/*!
 * \brief Subscribe to notification that the module is going to unload.
 *
 * \param module The module
 * \param callback ao2_weakproxy_notification_cb to be called before unloading the module.
 * \param userdata Any pointer, passed to the callback.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note callback will run immediately if the module is not currently loaded.
 *
 * \note callback is run once and discarded.  See ao2_weakproxy_subscribe for details.
 *
 * \warning module will be locked.  It is only safe to run this with the lib or instance
 * locked if the module was already locked first.
 */
#define ast_module_subscribe_unload(module, callback, userdata) ({ \
	struct ast_module *__module = (module); \
	ao2_weakproxy_subscribe(__module, (callback), (userdata), 0); \
})

/*!
 * \brief Unsubscribe to notification that the module is going to unload.
 *
 * Parameters should be identical to those used with ast_module_subscribe_unload.
 *
 * \retval 1 Success (1 subscription removed)
 * \retval 0 Subscription not found
 * \retval -1 Failure
 *
 * \warning module will be locked.  It is only safe to run this with the lib or instance
 * locked if the module was already locked first.
 */
#define ast_module_unsubscribe_unload(module, callback, userdata) ({ \
	struct ast_module *__module = (module); \
	ao2_weakproxy_unsubscribe(__module, (callback), (userdata), 0); \
}) \

/* These 3 do not bump the returned reference. */
struct ast_module *ast_module_from_lib(struct ast_module_lib *lib);
struct ast_module *ast_module_from_instance(struct ast_module_instance *instance);
struct ast_module_lib *ast_module_lib_from_instance(struct ast_module_instance *instance);

int ast_module_count_running(void);

int ast_module_load(struct ast_module *module);
void ast_module_unload(struct ast_module *module, int force);

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
 * \retval The \ref ast_module_reload_result status of the module load request
 */
enum ast_module_reload_result ast_module_reload(struct ast_module *module);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
