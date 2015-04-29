/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2015, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 * Luigi Rizzo <rizzo@icir.org>
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
 *
 * \brief Module Field Accessors
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"

#include "asterisk/module.h"
#include "module_private.h"

const char *ast_module_name(const struct ast_module *module)
{
	return module ? module->name : "Core";
}

const char *ast_module_description(const struct ast_module *module)
{
	return module ? module->description : "Asterisk Core";
}

enum ast_module_support_level ast_module_support_level(const struct ast_module *module)
{
	return module ? module->support_level : AST_MODULE_SUPPORT_CORE;
}

static const char *support_level_map [] = {
	[AST_MODULE_SUPPORT_UNKNOWN] = "unknown",
	[AST_MODULE_SUPPORT_CORE] = "core",
	[AST_MODULE_SUPPORT_EXTENDED] = "extended",
	[AST_MODULE_SUPPORT_DEPRECATED] = "deprecated",
};

const char *ast_module_support_level_to_string(const struct ast_module *module)
{
	return support_level_map[module->support_level];
}

enum ast_module_load_priority ast_module_load_priority(const struct ast_module *module)
{
	return module ? module->load_priority : AST_MODPRI_DEFAULT;
}

int ast_module_exports_globals(const struct ast_module *module)
{
	return module ? module->export_globals : 1;
}

int ast_module_unload_is_blocked(const struct ast_module *module)
{
	return module ? abs(module->block_unload) : 1;
}

void ast_module_block_unload(struct ast_module *module)
{
	struct ast_module_lib *lib;

	/* This needs to be NULL-safe for things like threadstorage.h. */
	if (!module || module->block_unload) {
		return;
	}

	ao2_lock(module);
	if (module->block_unload) {
		ao2_unlock(module);
		return;
	}
	module->block_unload = 1;
	ao2_unlock(module);

	/* if there is no lib this will happen on load. */
	lib = ast_module_get_lib_loaded(module);

	if (lib) {
		struct ast_module_instance *instance = ao2_t_weakproxy_get_object(lib, 0, "block_unload");

		if (instance) {
			ao2_lock(instance);
			if (instance->block_unload) {
				ao2_t_ref(instance, -1, "clear extra block_unload");
			} else {
				instance->block_unload = 1;
			}
			ao2_unlock(instance);
		}
	}
}

int ast_module_is_running(struct ast_module *module)
{
	if (module) {
		int ret;

		ao2_lock(module);
		ret = module->lib ? 1 : 0;
		ao2_unlock(module);

		return ret;
	}

	return ast_fully_booted ? 1 : 0;
}

int ast_module_count_running(void)
{
	int ret;

	AST_VECTOR_RW_RDLOCK(&modules_running);
	ret = AST_VECTOR_SIZE(&modules_running);
	AST_VECTOR_RW_UNLOCK(&modules_running);

	return ret;
}

struct ast_module_instance *__ast_module_get_instance(struct ast_module *module, const char *file, int line, const char *func)
{
	struct ast_module_instance *instance = NULL;

	ao2_lock(module);
	if (module->lib) {
		instance = __ao2_weakproxy_get_object(module->lib, 0, "ast_module_get_instance", file, line, func);
	}
	ao2_unlock(module);

	return instance;
}

int __ast_module_ref_instance(struct ast_module *module, int delta, const char *file, int line, const char *func)
{
	int i = -1;

	ao2_lock(module);
	if (module->lib) {
		i = __ao2_weakproxy_ref_object(module->lib, delta, 0, "ast_module_ref_instance", file, line, func);
	}
	ao2_unlock(module);

	return i;
}

struct ast_module_lib *__ast_module_get_lib_loaded(struct ast_module *module, const char *file, int line, const char *func)
{
	struct ast_module_lib_proxy *lproxy = ao2_weakproxy_get_object(module, 0);
	struct ast_module_lib *lib;

	if (!lproxy) {
		return NULL;
	}

	/* No lock needed here. */
	lib = lproxy->lib;
	__ao2_ref(lib, +1, "ast_module_get_lib_loaded", file, line, func);
	ao2_ref(lproxy, -1);

	return lib;
}

struct ast_module_lib *__ast_module_get_lib_running(struct ast_module *module, const char *file, int line, const char *func)
{
	struct ast_module_lib *lib;

	ao2_lock(module);
	lib = module->lib;
	if (lib) {
		__ao2_ref(lib, +1, "ast_module_get_lib_running", file, line, func);
	}
	ao2_unlock(module);

	return lib;
}

struct ast_module *ast_module_from_lib(struct ast_module_lib *lib)
{
	return lib->module;
}

struct ast_module_instance *__ast_module_lib_get_instance(struct ast_module_lib *lib, const char *file, int line, const char *func)
{
	return __ao2_weakproxy_get_object(lib, 0, "ast_module_lib_get_instance", file, line, func);
}

int __ast_module_lib_ref_instance(struct ast_module_lib *lib, int delta, const char *file, int line, const char *func)
{
	return __ao2_weakproxy_ref_object(lib, delta, 0, "ast_module_lib_ref_instance", file, line, func);
}

struct ast_module *ast_module_from_instance(struct ast_module_instance *instance)
{
	return instance->module;
}

struct ast_module_lib *ast_module_lib_from_instance(struct ast_module_instance *instance)
{
	return instance->lib_proxy->lib;
}

int ast_module_instance_refs(struct ast_module *module)
{
	if (!module) {
		return 0;
	}

	return ao2_weakproxy_ref_object(module, 0, 0);
}

#define MODULE_CMP(module, search) !strcmp(module->name, search)

struct ast_module *__ast_module_find(const char *name,
	const char *file, int line, const char *func)
{
	struct ast_module **mod;

	AST_VECTOR_RW_RDLOCK(&modules);
	mod = AST_VECTOR_GET_CMP(&modules, name, MODULE_CMP);
	if (mod) {
		__ao2_ref(*mod, +1, "ast_module_find", file, line, func);
	}
	AST_VECTOR_RW_UNLOCK(&modules);

	return mod ? *mod : NULL;
}

struct ast_module *__ast_module_find_provider(const char *type, const char *id,
	const char *file, int line, const char *func)
{
	struct ast_module_providertype *ptyp;
	struct ast_module_provider *prov;
	struct ast_module *module = NULL;

	if (!strcmp(type, "module")) {
		return __ast_module_find(id, file, line, func);
	}

	ptyp = module_providertype_find(type);
	if (!ptyp) {
		return NULL;
	}

	prov = module_providertype_find_provider(ptyp, id);
	if (prov) {
		module = prov->module;
		__ao2_ref(module, +1, "ast_module_find_provider", file, line, func);
	}
	return module;
}

/* BUGBUG: split into multiple functions for different purposes. */
char *ast_module_complete(const char *line, const char *word, int pos, int state, int rpos,
	enum ast_module_complete_filter filter)
{
	int which = 0;
	int l = strlen(word);
	char *ret = NULL;
	int i;
	struct ast_module *module;

	if (pos != rpos || filter == AST_MODULE_COMPLETE_NONE) {
		return NULL;
	}

	AST_VECTOR_RW_RDLOCK(&modules);
	for (i = 0; i < AST_VECTOR_SIZE(&modules); i++) {
		module = AST_VECTOR_GET(&modules, i);

		if (!strncasecmp(word, module->name, l) && ++which > state) {
			int use = 0;
			struct ast_module_instance *instance = NULL;

			ao2_lock(module);
			if (filter == AST_MODULE_COMPLETE_ALL
				|| (filter == AST_MODULE_COMPLETE_ADMINLOADED && module->admin_user)) {
				use = 1;
			} else {
				if (module->lib) {
					instance = ao2_weakproxy_get_object(module->lib, 0);
				}
			}
			ao2_unlock(module);

			if (instance) {
				/* module->lib cannot be cleared while we hold a reference to instance. */
				if ((filter & AST_MODULE_COMPLETE_LOADED)
					|| (filter & AST_MODULE_COMPLETE_RELOADABLE && module->lib->reload_fn)) {
					use = 1;
				}

				ao2_ref(instance, -1);
			} else if ((filter & AST_MODULE_COMPLETE_UNLOADED)
				|| (filter & AST_MODULE_COMPLETE_CANLOAD && !module->neverload)) {
				use = 1;
			}

			if (use) {
				ret = ast_strdup(module->name);
			}
		}
	}
	AST_VECTOR_RW_UNLOCK(&modules);

	return ret;
}

