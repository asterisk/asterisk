/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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

#include "asterisk/linkedlists.h"	/* XXX needed here */

#include "asterisk/utils.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#ifdef STATIC_MODULE	/* symbols are static */
#define _HAVE_STATIC_MODULE
#undef STATIC_MODULE
#define STATIC_MODULE	static /* symbols are static */
#else	/* !STATIC_MODULE, symbols are global */
#define STATIC_MODULE	/* empty - symbols are global */

/*! \note Every module should provide these functions */
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
int usecount(void);		/* How many channels provided by this module are in use? */

/*! \brief Provides a description of the module.
 *
 * \return a short description of your module
 */
const char *description(void);		/* Description of this module */

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
const char *key(void);		/* Return the below mentioned key, unmodified */

/*! 
 * \brief Reload stuff.
 *
 * This function is where any reload routines take place.  Re-read config files,
 * change signalling, whatever is appropriate on a reload.
 *
 * \return The return value is not used.
 */
int reload(void);		/* reload configs */
#endif	/* !STATIC_MODULE case */

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
char *ast_module_helper(const char *line, const char *word, int pos, int state, int rpos, int needsreload);

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

/*!
 * \brief Given a function address, find the corresponding module.
 * This is required as a workaround to the fact that we do not
 * have a module argument to the load_module() function.
 * Hopefully the performance implications are small.
 */
struct module *ast_find_module(int (*load_fn)(void));

/* Local user routines keep track of which channels are using a given module
   resource.  They can help make removing modules safer, particularly if
   they're in use at the time they have been requested to be removed */

struct localuser {
	struct ast_channel *chan;
	AST_LIST_ENTRY(localuser) next;
};

/*! \brief structure used for lock and refcount of module users.
 * \note The mutex protects the usecnt field and whatever needs to be
 * protected (typically, a list of struct localuser).
 * As a trick, if usecnt is initialized with -1,
 * ast_format_register will init the mutex for you.
 */
struct ast_module_lock {
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(localuser_head, localuser) u;
	int usecnt;	/* number of active clients */
};

struct localuser *ast_localuser_add(struct ast_module_lock *m, struct ast_channel *chan);
void ast_localuser_remove(struct ast_module_lock *m, struct localuser *u);
void ast_hangup_localusers(struct ast_module_lock *m);

/*! 
 * \brief create a localuser mutex and several other variables used for keeping the
 * use count.
 *
 * <b>Sample Usage:</b>
 * \code
 * LOCAL_USER_DECL;
 * \endcode
 */
#define LOCAL_USER_DECL					\
	static struct ast_module_lock me = {		\
		.u = AST_LIST_HEAD_NOLOCK_INIT_VALUE,	\
		.usecnt = 0,				\
		.lock = AST_MUTEX_INIT_VALUE }

#define STANDARD_USECOUNT_DECL LOCAL_USER_DECL	/* XXX lock remains unused */

/*! \brief run 'x' protected by lock, then call ast_update_use_count() */
#define __MOD_PROTECT(x) do {			\
	ast_mutex_lock(&me.lock);		\
	x;					\
	ast_mutex_unlock(&me.lock);		\
	ast_update_use_count();			\
	} while (0)

#define STANDARD_INCREMENT_USECOUNT __MOD_PROTECT(me.usecnt++)
#define STANDARD_DECREMENT_USECOUNT __MOD_PROTECT(me.usecnt--)

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
#define LOCAL_USER_ADD(u) do {			\
	u = ast_localuser_add(&me, chan);	\
	if (!u)					\
		return -1;			\
	} while (0)

/*! 
 * \brief Remove a localuser.
 * \param u the user to add, should be of type struct localuser
 *
 * This macro removes a localuser from the list of users and decrements the
 * usecount.
 */
#define LOCAL_USER_REMOVE(u) ast_localuser_remove(&me, u)

/*! 
 * \brief Hangup all localusers.
 *
 * This macro hangs up on all current localusers and sets the usecount to zero
 * when finished.
 */
#define STANDARD_HANGUP_LOCALUSERS ast_hangup_localusers(&me)

/*!
 * \brief Set the specfied integer to the current usecount.
 * \param res the integer variable to set.
 *
 * This macro sets the specfied integer variable to the local usecount.
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
#define STANDARD_USECOUNT(res) do { res = me.usecnt; } while (0)

/*! \brief Old usecount macro
 * \note XXX The following macro is deprecated, and only used by modules
 * in codecs/ and a few other places which do their own manipulation
 * of the usecount variable.
 * Its use is supposed to be gradually phased away as those modules
 * are updated to use the standard mechanism.
 */
#define OLD_STANDARD_USECOUNT(res) do { res = localusecnt; } while (0)

/*! \page ModMngmnt The Asterisk Module management interface
 * \par The following is part of the new module management code.
 *
 * All modules must implement the module API (load, unload...)
 * whose functions are exported through fields of a "struct module_symbol";
 *
 * Modules exporting extra symbols (data or functions), should list
 * them into an array of struct symbol_entry: \r
 *     struct symbol_entry exported_symbols[]
 * \r
 * of symbols, with a NULL name on the last entry
 *
 * Functions should be added with MOD_FUNC(name),
 * data structures with MOD_DATA(_name).
 * The array in turn is referenced by struct module_symbol.
 * (Typically, a module will export only a single symbol, which points
 * to a record containing all the methods. This is the API of the module,
 * and should be known to the module's clients as well.
 *
 * \par Connections to symbols in other modules
 * Modules that require symbols supplied by other modules should
 * provide an array
 *     struct symbol_entry required_symbols[]
 * of symbols, with a NULL name on the last entry, containing the
 * name of the desired symbol.
 * For good measure, we also provide the size in both caller and calle
 * to figure out if there is a mismatch (not terribly useful because most
 * objects are a single word, but still... )
 * The symbol can be added to the array with MOD_WANT(symbol) macro.
 * required_symbols is also pointed by through struct module_symbol.
 *
 * Typically, the whole interface exported by a module should be
 * in a single structure named after the module, as follows.
 * Say the module high level name is 'foo', then we should have
 * - in include/asterisk/foo.h
 *     struct foo_interface {
 *		int (*f)(int, char *); -- first function exported 
 *		const char (*g)(int); -- second function exported 
 *		char *buf;
 *		...		-- other fields
 *     }
 * - in the module exporting the interface, e.g. res/res_foo.c
 *	static int f(int, char *);
 *	static const char *g(int);
 *	const char buf[BUFSZ];
 *     struct foo_interface foo = {
 *	.f = f,
 *	.g = g,
 *	.buf = buf,
 *     }
 *
 * \note NOTE: symbol names are 'global' in this module namespace, so it
 * will be wiser to name exported symbols with a prefix indicating the module
 * supplying it, e.g. foo_f, foo_g, foo_buf. Internally to the module,
 * symbols are still static so they can keep short and meaningful names.
 * The macros MOD_FIELD and METHOD_BASE() below help setting these entries.
 *
 *	MOD_FIELD(f1),		-- field and function name are the same
 *	METHOD_BASE(foo_, f1),  -- field and function name differ by a prefix
 *	.f1 = function_name,    -- generic case
 *     }
 *
 * Note that the loader requires that no fields of exported_symbols
 * are NULL, because that is used as an indication of the end of the array.
 *
 * \par Module states
 * Modules can be in a number of different states, as below:
 * - \b MS_FAILED    attempt to load failed. This is final.
 * - \b MS_NEW       just added to the list, symbols unresolved.
 * - \b MS_RESOLVED  all symbols resolved, but supplier modules not active yet.
 * - \b MS_CANLOAD   all symbols resolved and suppliers are all active
 *              (or we are in a cyclic dependency and we are breaking a loop)
 * - \b MS_ACTIVE    load() returned successfully.
 *
 * 
 * \par Module Types
 * For backward compatibility, we have 3 types of loadable modules:
 *
 * - \b MOD_0 these are the 'old style' modules, which export a number
 *       of callbacks, and their full interface, as globally visible
 *       symbols. The module needs to be loaded with RTLD_LAZY and
 *       RTLD_GLOBAL to make symbols visible to other modules, and
 *       to avoid load failures due to cross dependencies.
 *
 * - \b MOD_1 almost as above, but the generic callbacks are all into a
 *       a structure, mod_data. Same load requirements as above.
 *
 * - \b MOD_2 this is the 'new style' format for modules. The module must
 *       explictly declare which simbols are exported and which
 *       symbols from other modules are used, and the code in this
 *       loader will implement appropriate checks to load the modules
 *       in the correct order. Also this allows to load modules
 *       with RTLD_NOW and RTLD_LOCAL so there is no chance of run-time
 *       bugs due to unresolved symbols or name conflicts.
 */

struct symbol_entry {
	const char *name;
	void *value;
	int size;
	struct module *src;	/* module sourcing it, filled by loader */
};

/*
 * Constructors for symbol_entry values
 */
#define	MOD_FUNC(f)	{ .name = #f, .value = f, .size = sizeof(f) }
#define	MOD_DATA(d)	{ .name = #d, .value = &d, .size = sizeof(_name) }
#define	MOD_WANT(s)	{ .name = #s, .value = &s, 0 }   /* required symbols */

/*
 * Constructors for fields of foo_interface
 */
#define	MOD_FIELD(f)    . ## f = f
#define	METHOD_BASE(_base, _name)       . ## _name = _base ## _name

/*
 * Each 'registerable' entity has a pointer in the
 * struct ast_registry, which points to an array of objects of
 * the same type. The ast_*_register() function will be able to
 * derive the size of these entries.
 */
struct ast_registry {
	struct ast_cli_entry *clis;
};

struct module_symbols {
	int (*load_module)(void);
	int (*unload_module)(void);
	int (*usecount)(void);   
	const char *(*description)(void);
	const char *(*key)(void);
	int (*reload)(void);
	enum module_type {
		MOD_0,	/* old module style */
		MOD_1,	/* old style, but symbols here */
		MOD_2,	/* new style, exported symbols */
	} type;
	struct ast_registry *reg;
	struct symbol_entry *exported_symbols;
	struct symbol_entry *required_symbols;
};

#ifndef _HAVE_STATIC_MODULE
#define STD_MOD(t, reload_fn, exp, req)
#else
#define STD_MOD(t, reload_fn, exp, req)			\
struct module_symbols mod_data = {			\
        .load_module = load_module,			\
        .unload_module = unload_module,			\
        .description = description,			\
        .key = key,					\
        .reload = reload_fn,				\
        .usecount = usecount,				\
	.type = t,					\
	.exported_symbols = exp,			\
	.required_symbols = req				\
};
#endif /* _HAVE_STATIC_MODULE */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MODULE_H */
