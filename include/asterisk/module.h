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

#ifdef STATIC_MODULE
#error STATIC_MODULE should not be defined
#endif
#define STATIC_MODULE --- this is an error
#define	LOCAL_USER_DECL	/* --- this is an error --- */

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

enum unload_mode {
	AST_FORCE_SOFT = 0, /*! Softly unload a module, only if not in use */
	AST_FORCE_FIRM = 1, /*! Firmly unload a module, even if in use */
	AST_FORCE_HARD = 2, /*! as FIRM, plus dlclose() on the module. Not recommended
				as it may cause crashes */
};

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
int ast_unload_resource(const char *resource_name, enum unload_mode);

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

/* Local user routines keep track of which channels are using a given module
   resource.  They can help make removing modules safer, particularly if
   they're in use at the time they have been requested to be removed */

struct localuser {
	struct localuser *next;
	struct ast_channel *chan;
};

struct module_symbols;	/* forward declaration */
struct localuser *ast_localuser_add(struct module_symbols *, struct ast_channel *);
void ast_localuser_remove(struct module_symbols *, struct localuser *);
void ast_hangup_localusers(struct module_symbols *);

/* XXX deprecated macros, only for backward compatibility */
#define	LOCAL_USER_ADD(u) do { u = ast_localuser_add(__mod_desc, chan); } while (0)
#define	LOCAL_USER_REMOVE(u) ast_localuser_remove(__mod_desc, u)
#define	STANDARD_HANGUP_LOCALUSERS ast_hangup_localusers(__mod_desc)

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
 * The array in turn is referenced by struct module_symbols.
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
 * required_symbols is also pointed by through struct module_symbols.
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
	/* load, reload and unload receive as argument a pointer to a module descriptor
	 * to be stored locally and used for local calls and so on.
	 * They all return 0 on success, non zero (-1) on failure.
	 */

	int (*load_module)(void *);	/* register stuff etc. Optional. */

	int (*reload)(void *);	/* reload config etc. Optional. */

	int (*unload_module)(void *);	/* unload. called with the module locked */

	const char *(*description)(void);	/* textual id of the module. */

	/*! 
	 * This returns the ASTERISK_GPL_KEY, signifiying that you agree to the terms of
	 * the GPL stated in the ASTERISK_GPL_KEY.  Your module will not load if it does
	 * not return the EXACT message:
	 */
	const char *(*key)(void);	/*! the asterisk key */

	enum module_flags {
		MOD_0 = 0x0,	/* old module style */
		MOD_1 = 0x1,	/* old style, but symbols here */
		MOD_2 = 0x2,	/* new style, exported symbols */
		MOD_MASK = 0xf,	/* mask for module types */
		NO_USECOUNT = 0x10,	/* do not track usecount */
		NO_UNLOAD = 0x20,	/* only forced unload allowed */
		DO_LOCALUSERS = 0x40,	/* track localusers */
	} flags;
	/* the following two fields should go in the astobj. */
	ast_mutex_t lock;		
	int usecnt;	/* number of active clients */

	/* list of clients */
	struct localuser *lu_head;
	struct ast_registry *reg;		/* list of things to register. */
	struct symbol_entry *exported_symbols;
	struct symbol_entry *required_symbols;
};

#ifndef MOD_LOADER	/* the loader does not use these */
struct module_symbols mod_data;	/* forward declaration */
static struct module_symbols *__mod_desc __attribute__ ((__unused__)) = &mod_data; /* used by localuser */

#define STD_MOD(t, reload_fn, exp, req)			\
struct module_symbols mod_data = {			\
        .load_module = load_module,			\
        .unload_module = unload_module,			\
        .description = description,			\
        .key = key,					\
        .reload = reload_fn,				\
	.flags = t,					\
	.exported_symbols = exp,			\
	.required_symbols = req				\
};

#define	STD_MOD1	STD_MOD(MOD_1, NULL, NULL, NULL)
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MODULE_H */
