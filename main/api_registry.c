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
 * \brief API registry implementation functions
 *
 * This file contains implementation for code that accepts registration
 * of interfaces from modules.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/api_registry.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/vector.h"

struct ast_api_holder {
	/* interface must be the first member. */
	struct ast_api_interface *interface;
	struct ast_api_registry *registry;
	struct ast_module_lib *lib;
};

#define API_INTERFACE_NAME(interface) \
	(const char * const)((*(const char**)interface) + registry->name_offset)

static void api_holder_destructor(void *obj)
{
	struct ast_api_holder *holder = obj;

	if (holder->registry && holder->registry->clean_interface) {
		holder->registry->clean_interface(holder->interface);
	}

	ao2_cleanup(holder->lib);
}

static void module_unload_cb(void *weakproxy, void *data)
{
	struct ast_api_holder *holder = data;

	ast_api_registry_unregister(holder->registry, holder->interface);
}

/* call with registry->vec locked. */
static struct ast_api_holder *__ast_api_registry_find_by_name(struct ast_api_registry *registry, const char *search)
{
	struct ast_api_holder *holder = NULL;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&registry->vec); i++) {
		holder = AST_VECTOR_GET(&registry->vec, i);
		if (!registry->namecmp(API_INTERFACE_NAME(holder->interface), search)) {
			ao2_ref(holder, +1);
			return holder;
		}
		holder = NULL;
	}

	return holder;
}

#define API_SORT_HOLDER(holder1, holder2) \
	registry->sort_holders(registry, holder1, holder2)

int ast_api_registry_register(struct ast_api_registry *registry, void *interface,
	struct ast_module *module)
{
	struct ast_api_holder *holder = NULL;
	struct ast_module_lib *lib = NULL;

	if (!interface || ast_strlen_zero(API_INTERFACE_NAME(interface))) {
		ast_log(LOG_ERROR, "%s cannot register an interface without a name.\n",
			registry->label);
		return -1;
	}

	if (module) {
		lib = ast_module_get_lib_running(module);
	} else if (!registry->allow_core) {
		ast_log(LOG_ERROR, "%s requires a module but '%s' does not identify one.\n",
			registry->label, API_INTERFACE_NAME(interface));
		return -1;
	}

	AST_VECTOR_RW_WRLOCK(&registry->vec);

	holder = __ast_api_registry_find_by_name(registry, API_INTERFACE_NAME(interface));
	if (holder) {
		ast_log(LOG_ERROR, "%s already registered for '%s'.\n", registry->label, API_INTERFACE_NAME(interface));
		goto returnerror;
	}

	holder = ao2_t_alloc(sizeof(*holder), api_holder_destructor, API_INTERFACE_NAME(interface));
	if (!holder) {
		ast_log(LOG_ERROR, "%s registration failed for '%s'.\n", registry->label, API_INTERFACE_NAME(interface));
		goto returnerror;
	}

	if ((registry->initialize_interface && registry->initialize_interface(interface, module))) {
		ast_log(LOG_ERROR, "%s failed sanity check for '%s'.\n",
			registry->label, API_INTERFACE_NAME(interface));
		goto returnerror;
	}

	holder->interface = interface;
	holder->registry = registry;
	holder->lib = lib;

	AST_VECTOR_ADD_SORTED(&registry->vec, holder, registry->holders_sort);
	ao2_t_ref(holder, +1, "linked to vector");
	ast_verb(3, "%s registered '%s'.\n", registry->label, API_INTERFACE_NAME(interface));

	AST_VECTOR_RW_UNLOCK(&registry->vec);

	if (holder->lib) {
		ast_module_lib_subscribe_stop(holder->lib, module_unload_cb, holder);
	}
	ao2_t_ref(holder, -1, "drop allocation ref");

	return 0;

returnerror:
	ao2_cleanup(lib);
	ao2_cleanup(holder);
	AST_VECTOR_RW_UNLOCK(&registry->vec);

	return -1;
}

#define AST_API_HOLDER_EXTRACT_UNREG(vec_elem) { holder = vec_elem; }

#define AST_API_FIND_BY_INTERFACE(current, search) (current->interface == search)

int ast_api_registry_unregister(struct ast_api_registry *registry, void *interface)
{
	struct ast_api_holder *holder = NULL;
	struct ast_api_interface *interf = interface;

	AST_VECTOR_RW_WRLOCK(&registry->vec);
	AST_VECTOR_REMOVE_CMP_ORDERED(&registry->vec, interface,
		AST_API_FIND_BY_INTERFACE, AST_API_HOLDER_EXTRACT_UNREG);
	AST_VECTOR_RW_UNLOCK(&registry->vec);

	if (!holder) {
		ast_log(LOG_WARNING, "%s was not registered for '%s'.\n", registry->label, API_INTERFACE_NAME(interf));
		return -1;
	}

	ast_verb(3, "%s unregistered for '%s'.\n", registry->label, API_INTERFACE_NAME(interf));

	if (holder->lib) {
		/* This should normally be a noop, but things are sometimes unregistered before
		 * the module exits. */
		ast_module_lib_unsubscribe_stop(holder->lib, module_unload_cb, holder);
	}
	ao2_t_ref(holder, -1, "unlinked from vector");

	return 0;
}

struct ast_api_holder *ast_api_registry_find_by_name(struct ast_api_registry *registry,
	const char *search)
{
	struct ast_api_holder *holder;

	AST_VECTOR_RW_RDLOCK(&registry->vec);
	holder = __ast_api_registry_find_by_name(registry, search);
	AST_VECTOR_RW_UNLOCK(&registry->vec);

	return holder;
}

struct ast_api_holder *ast_api_registry_use_head(struct ast_api_registry *registry)
{
	struct ast_api_holder *holder = NULL;

	AST_VECTOR_RW_RDLOCK(&registry->vec);
	if (AST_VECTOR_SIZE(&registry->vec)) {
		holder = ao2_bump(AST_VECTOR_GET(&registry->vec, 0));
	}
	AST_VECTOR_RW_UNLOCK(&registry->vec);

	return ast_api_holder_use(holder);
}

struct ast_api_holder *ast_api_holder_use(struct ast_api_holder *holder)
{
	if (!holder) {
		return NULL;
	}

	if (!holder->lib) {
		if (!holder->registry->allow_core) {
			goto error;
		}
	} else if (ast_module_lib_ref_instance(holder->lib, +1) < 0) {
		goto error;
	}

	return holder;

error:
	ao2_ref(holder, -1);
	return NULL;
}

static const char * const ast_api_holder_name(const struct ast_api_holder *holder)
{
	return (const char * const)((*(const char**)holder->interface) + holder->registry->name_offset);
}

void ast_api_holder_release(struct ast_api_holder *holder)
{
	if (!holder) {
		/* Happens with AST_API_HOLDER_SCOPED. */
		return;
	}

	ast_module_lib_ref_instance(holder->lib, -1);
	ao2_ref(holder, -1);
}

int ast_api_registry_strcmp(struct ast_api_holder *h1, struct ast_api_holder *h2)
{
	return strcmp(ast_api_holder_name(h1), ast_api_holder_name(h2));
}

int ast_api_registry_strcasecmp(struct ast_api_holder *h1, struct ast_api_holder *h2)
{
	return strcasecmp(ast_api_holder_name(h1), ast_api_holder_name(h2));
}

int ast_api_registry_init(struct ast_api_registry *registry, size_t size)
{
	if (!registry->holders_sort) {
		registry->holders_sort = ast_api_registry_strcmp;
	}

	if (!registry->namecmp) {
		registry->namecmp = strcmp;
	}

	return AST_VECTOR_RW_INIT(&registry->vec, size);
}

void ast_api_registry_cleanup(struct ast_api_registry *registry)
{
	AST_VECTOR_RW_FREE(&registry->vec);
}

