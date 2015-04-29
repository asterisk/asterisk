/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, CFWare, LLC.
 *
 * Corey Farrell <git@cfware.com>
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
 * \brief API registry code templates
 *
 * This file contains templates for use by code that accepts registration
 * of module API's.
 */

#ifndef _ASTERISK_API_REGISTRY_H
#define _ASTERISK_API_REGISTRY_H

#include "asterisk/vector.h"

struct ast_api_holder;

typedef int (*ast_api_interface_initialize)(void *interface, struct ast_module *module);
typedef void (*ast_api_interface_clean)(void *interface);
typedef int (*ast_api_holders_sort)(struct ast_api_holder *i1, struct ast_api_holder *i2);
typedef int (*ast_api_namecmp)(const char *s1, const char *s2);

AST_VECTOR_RW(ast_api_vector,struct ast_api_holder *);

/*!
 * \brief Structure containing information and callbacks to control a registry.
 *
 * \note Variables declared of this type should not be shared.
 */
struct ast_api_registry {
	/*! Text label used by logging. */
	const char * const label;
	/*!
	 * \brief Check and initialize an interface.
	 *
	 * \retval 0 Interface is acceptable, initialized.
	 * \retval non-zero Interface is rejected, not initialized.
	 */
	ast_api_interface_initialize initialize_interface;
	/*!
	 * \brief Compare two holders to determine sort order.
	 *
	 * \warning It is not safe to register with this callback NULL.
	 * \ref ast_api_registry_init will set this to ast_api_registry_strcmp by
	 * default.  If it's possible for registration attempts to happen before
	 * initialization this field must be statically initialized.
	 */
	ast_api_holders_sort holders_sort;
	/*!
	 * \brief Clean memory allocated by initialize_interface.
	 *
	 * \note This optional callback is not run unless initialize_interface
	 * succeeds.
	 */
	ast_api_interface_clean clean_interface;
	/*!
	 * \brief Compare two interface names to determine if they are duplicate.
	 *
	 * \note This is not used for sorting purposes, it is to check for matches.
	 *
	 * \warning It is not safe to register with this callback NULL.
	 * \ref ast_api_registry_init will set this to strcmp by default.  If it's
	 * possible for registration attempts to happen before initialization this
	 * field must be statically initialized.
	 */
	ast_api_namecmp namecmp;
	/*!
	 * \brief Vector that holds registrations.
	 *
	 * \note Locking is required for all use of this vector.  Elements
	 * must be referenced within the lock if they are to be used outside
	 * the lock.
	 */
	struct ast_api_vector vec;
	/*!
	 * \brief Offset to name in the interface structures.
	 *
	 * If the name is the first member of the structures the default of 0 will work.
	 * If it can not be the first field or is part of a structure declared by an
	 * outside header, you should use offsetof macro to set this value.
	 *
	 * This is used for logger messages that include an interface name and for
	 * \ref ast_api_registry_find_by_name.  It is used by the default interface
	 * sorting methods.
	 */
	size_t name_offset;
	/*! This must be set to allow module==NULL. */
	unsigned int allow_core:1;
};

/*! \brief Generate a stub function for interfaces to register. */
#define AST_API_FN_REGISTER(name, prefix) \
	int prefix ## _register(struct name *interface, struct ast_module *module) \
	{ \
		return ast_api_registry_register(&name, interface, module); \
	}

/*! \brief Generate a stub function for multiple interfaces to register. */
#define AST_API_FN_REGISTER_MULTIPLE(name, prefix) \
	int prefix ## _register_multiple(struct name *interface, int len, struct ast_module *module) \
	{ \
		int i, res = 0; \
		\
		for (i = 0; i < len; i++) { \
			res |= ast_api_registry_register(&name, interface[i], module); \
		} \
		\
		return res; \
	}

/*!
 * \brief Generate a stub function for interfaces to unregister.
 *
 * \note This is often unneeded for registries that do not allow_core.  Unregister
 * happens automatically, before the unload_module function runs.
 */
#define AST_API_FN_UNREGISTER(name, prefix) \
	int prefix ## _unregister(struct name *interface) \
	{ \
		return ast_api_registry_unregister(&name, interface); \
	}

/*!
 * \brief Generate a stub function for multiple interfaces to unregister.
 *
 * \note This is often unneeded for registries that do not allow_core.  Unregister
 * happens automatically, before the unload_module function runs.
 */
#define AST_API_FN_UNREGISTER_MULTIPLE(name, prefix) \
	int prefix ## _unregister_multiple(struct name *interface, int len) \
	{ \
		int i, res = 0; \
		\
		for (i = 0; i < len; i++) { \
			res |= ast_api_registry_unregister(&name, interface[i]); \
		} \
		\
		return res; \
	}

/*!
 * \brief Generate a function to find and use an interface holder by name.
 *
 * \return An API holder that should be released with ast_api_holder_release.
 *
 * A non-NULL return will prevent the module from being stopped or unloaded until
 * ast_api_holder_release is run.
 */
#define AST_API_FN_USE_BY_NAME(name, prefix) \
	struct ast_api_holder *prefix ## _use_by_name(const char *search) \
	{ \
		return ast_api_holder_use(ast_api_registry_find_by_name(&name, search)); \
	}

/*!
 * \brief Lock the registry for reading.
 *
 * \note The locking order is registry lock last.
 */
#define AST_API_REGISTRY_RDLOCK(name) \
	AST_VECTOR_RW_RDLOCK(&name.vec)

/*!
 * \brief Lock the registry for writing.
 *
 * \note The locking order is registry lock last.
 */
#define AST_API_REGISTRY_WRLOCK(name) \
	AST_VECTOR_RW_WRLOCK(&name.vec)

/*!
 * \brief How many interfaces are registered?
 *
 * \note The registry must be locked while this macro runs.
 */
#define AST_API_REGISTRY_EMPTY(name) \
	!AST_VECTOR_SIZE(&name.vec)

/*!
 * \brief Iterate the registered interfaces.
 *
 * \param name The registry definition.
 * \param varname Name of variable holding a pointer to the interface.
 * \param code A block of code that is run within a for block.
 *
 * \note The registry must be locked while this function runs.  It must
 * not be unlocked, even temporarily by the code block.
 */
#define AST_API_REGISTRY_ITERATE_INTERFACES(name, varname, code) { \
		int __ast_api_idx; \
		for (__ast_api_idx = 0; __ast_api_idx < AST_VECTOR_SIZE(&name.vec); __ast_api_idx++) { \
			struct name *varname = ast_api_get_interface(name, \
				AST_VECTOR_GET(&name.vec, __ast_api_idx)); \
			\
			code; \
		} \
	}

/*! Unlock the registry. */
#define AST_API_REGISTRY_UNLOCK(name) \
	AST_VECTOR_RW_UNLOCK(&name.vec)

#define __AST_API_HOLDER(name, varname, init1, init2) \
	struct ast_api_holder *varname ## _holder init1; \
	struct name *varname init2

#define AST_API_HOLDER(name, varname) \
	__AST_API_HOLDER(name, varname,,)

#define AST_API_HOLDER_INIT(name, varname, initval) \
	__AST_API_HOLDER(name, varname, \
		= (initval), \
		= (varname ## _holder ? ast_api_get_interface(name, varname ## _holder) : NULL) \
	)

#define AST_API_HOLDER_SCOPED(name, varname, initval) \
	RAII_VAR(struct ast_api_holder *, varname ## _holder, initval, ast_api_holder_release); \
	struct name *varname = varname ## _holder ? ast_api_get_interface(name, varname ## _holder) : NULL

#define AST_API_HOLDER_SET(name, varname, value) { \
		varname ## _holder = value; \
		varname = varname ## _holder ? ast_api_get_interface(name, varname ## _holder) : NULL; \
	}

#define AST_API_HOLDER_CLEANUP(varname) { \
		if (varname ## _holder) { \
			ast_api_holder_release(varname ## _holder); \
			varname ## _holder = NULL; \
			varname = NULL; \
		} \
	}

#define AST_API_HOLDER_REPLACE(name, varname, value) { \
		AST_API_HOLDER_CLEANUP(varname); \
		AST_API_HOLDER_SET(name, varname, value); \
	}

struct ast_api_holder;

/*! Register an interface to an API. */
int ast_api_registry_register(struct ast_api_registry *registry, void *interface,
	struct ast_module *module);

/*!
 * \brief Unregister an interface from an API.
 *
 * \note Modules should not call this function during unload, it is done automatically.
 */
int ast_api_registry_unregister(struct ast_api_registry *registry, void *interface);

/*! Use the first registered interface. */
struct ast_api_holder *ast_api_registry_use_head(struct ast_api_registry *registry);

/*!
 * \brief Find a registered interface by name.
 *
 * \param search api_label to find.
 *
 * \return An AO2 reference to the holder of an interface.
 *
 * \note This holder must not be passed to ast_api_holder_release unless it
 * is first passed to and returned by ast_api_holder_use.  This reference only
 * prevents dlclose from being run against the module, it does not prevent the
 * module from stopping.  Generally it's safe to read fields of the interface.
 * The default assumption should be that it is unsafe to run any callback
 * without an ast_api_holder_use reference.  A registry may declare that certain
 * callbacks must be safe after module_unload.
 */
struct ast_api_holder *ast_api_registry_find_by_name(struct ast_api_registry *registry,
	const char *search);

/*!
 * \brief Use an interface holder.
 *
 * \param holder The interface holder to use.
 *
 * \return An 'in use' reference to the holder of an interface.
 *
 * \note This function eats a reference to holder.  The return
 * value should be cleaned with ast_api_holder_release, not ao2_cleanup.
 */
struct ast_api_holder *ast_api_holder_use(struct ast_api_holder *holder);

/*!
 * \brief Release an interface aquired by ast_api_holder_use.
 *
 * \param holder The interface holder to release.
 *
 * \note This may result in the immediate shutdown and unload of the
 * module that provides the interface.
 */
void ast_api_holder_release(struct ast_api_holder *holder);

/*! Compare two holders by name. */
int ast_api_registry_strcmp(struct ast_api_holder *h1, struct ast_api_holder *h2);

/*! Compare two holders by case insensitive name. */
int ast_api_registry_strcasecmp(struct ast_api_holder *h1, struct ast_api_holder *h2);

/*!
 * \brief Initialize the registry structure.
 *
 * This sets required callbacks to defaults if they are NULL, initializes the
 * vector.
 */
int ast_api_registry_init(struct ast_api_registry *registry, size_t size);

/*!
 * \brief Free the vector.
 *
 * \note This does not remove interfaces that remain.
 */
void ast_api_registry_cleanup(struct ast_api_registry *registry);

/*!
 * \brief Borrow the interface structure from a holder.
 *
 * The interface should only be used while the holder is in use
 * by ast_api_holder_use, or while the registry is locked.
 */
#define ast_api_get_interface(name, holder) (*(struct name **)holder)

/*!
 * \brief Run a callback through the holder.
 *
 * \note This macro should be only used internally by registries.
 */
#define ast_api_run_interface(name, holder, func, ...) \
	ast_api_get_interface(name, holder)->func(__VA_ARGS__)

#endif
