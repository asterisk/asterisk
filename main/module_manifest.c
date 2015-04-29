/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, CFWare, LLC
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
 *
 * \brief Module Manifest Routines
 * \author Corey Farrell <git@cfware.com>
 * - See ModMngMnt
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"

#include "asterisk/astobj2.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"

#include "module_private.h"

struct ast_module_providertypes providertypes;
struct ast_modules_rw modules;
struct ast_modules_rw modules_loaded;
struct ast_module_libs_rw modules_running;
struct ast_string_vector neverload;


/* START: ast_module_provider */
static void module_provider_dtor(struct ast_module_provider *provider)
{
	ao2_t_ref(provider->module, -1, provider->id ?: "");
	ast_free(provider->id);
}

static struct ast_module_provider *module_provider_alloc(struct ast_module_providertype *type, const char *id, struct ast_module *module)
{
	struct ast_module_provider *obj;

	obj = ast_calloc(1, sizeof(*obj));
	if (!obj) {
		return NULL;
	}

	obj->id = ast_strdup(id);
	obj->module = module;
	ao2_t_ref(module, +1, id);

	return obj;
}
/* END: ast_module_provider */



/* START: ast_module_providertype */
void module_providertype_dtor(struct ast_module_providertype *ptyp)
{
	ast_free(ptyp->id);
	AST_VECTOR_CALLBACK_VOID(&ptyp->providers, module_provider_dtor);
	AST_VECTOR_FREE(&ptyp->providers);
}

static struct ast_module_providertype *module_providertype_alloc(const char *id)
{
	struct ast_module_providertype *ret;

	ret = ast_calloc(1, sizeof(*ret));
	if (!ret) {
		return NULL;
	}

	ret->id = ast_strdup(id);
	AST_VECTOR_INIT(&ret->providers, 1);

	AST_VECTOR_APPEND(&providertypes, ret);
	return ret;
}

#define PROVIDERTYPE_CMP(elem, value) !strcmp(elem->id, value)
struct ast_module_providertype *module_providertype_find(const char *id)
{
	struct ast_module_providertype **type;

	type = AST_VECTOR_GET_CMP(&providertypes, id, PROVIDERTYPE_CMP);
	if (type) {
		return *type;
	}

	return NULL;
}

struct ast_module_provider *module_providertype_find_provider(struct ast_module_providertype *ptyp, const char *id)
{
	struct ast_module_provider **type;

	type = AST_VECTOR_GET_CMP(&ptyp->providers, id, PROVIDERTYPE_CMP);
	if (type) {
		return *type;
	}

	return NULL;
}
#undef PROVIDERTYPE_CMP

static void module_provider_add(struct ast_module *module,
	const char *name, const char *value)
{
	struct ast_module_providertype *type;
	struct ast_module_provider *prov;

	type = module_providertype_find(name);
	if (!type) {
		type = module_providertype_alloc(name);
		if (!type) {
			return;
		}
	}

	prov = module_provider_alloc(type, value, module);
	if (prov) {
		AST_VECTOR_APPEND(&type->providers, prov);
	}
}
/* END: ast_module_providertype */



/* START: ast_module_manifest_uses */
static void module_manifest_uses_destroy(struct ast_module_uses *uses)
{
	ast_free(uses->type);
	AST_VECTOR_FREE(&uses->values);
}

static struct ast_module_uses *module_manifest_uses_alloc(const char *type)
{
	struct ast_module_uses *uses;

	uses = ast_calloc(1, sizeof(*uses));
	if (!uses) {
		return NULL;
	}

	uses->type = ast_strdup(type);
	AST_VECTOR_INIT(&uses->values, 1);

	return uses;
}

static struct ast_module_uses *module_manifest_uses_find(struct ast_module *module, const char *type)
{
	struct ast_module_uses **uses;

#define MODULE_USES_CMP(elem, search) !strcmp(elem->type, search)
	uses = AST_VECTOR_GET_CMP(&module->uses, type, MODULE_USES_CMP);

	if (uses) {
		return *uses;
	}

	return NULL;
}

int module_manifest_uses_add(struct ast_module *module, const char *type, const char *name)
{
	struct ast_module_uses *uses = module_manifest_uses_find(module, type);

	if (!uses) {
		uses = module_manifest_uses_alloc(type);
		if (!uses) {
			return -1;
		}
		AST_VECTOR_APPEND(&module->uses, uses);
	}

	if (!AST_VECTOR_GET_CMP(&uses->values, name, !strcmp)) {
		AST_VECTOR_APPEND(&uses->values, ast_strdup(name));
	}

	return 0;
}
/* END: ast_module_uses */




static void module_manifest_dtor(void *obj)
{
	struct ast_module *module = obj;

	ao2_cleanup(module->name);
	ast_free(module->description);
	ast_free(module->checksum);

	AST_VECTOR_CALLBACK_VOID(&module->alldeps, ao2_t_ref, -1, "remove from module->alldeps");
	AST_VECTOR_FREE(&module->alldeps);

	AST_VECTOR_CALLBACK_VOID(&module->configs, ao2_t_ref, -1, "remove from module->configs");
	AST_VECTOR_FREE(&module->configs);

	AST_VECTOR_CALLBACK_VOID(&module->uses, module_manifest_uses_destroy);
	AST_VECTOR_FREE(&module->uses);
}

static struct ast_module *module_manifest_alloc(const char *config, const char *name)
{
	struct ast_module *module;

	if (!name) {
		ast_log(LOG_ERROR, "Missing name in '%s'.\n", config);
		return NULL;
	}

	module = ao2_t_weakproxy_alloc(sizeof(*module), module_manifest_dtor, name ?: "");
	if (!module) {
		ast_log(LOG_ERROR, "Failed to allocate module\n");
		return NULL;
	}

	module->name = ast_str_ao2_alloc(name);
	AST_VECTOR_INIT(&module->alldeps, 0);
	AST_VECTOR_INIT(&module->configs, 0);
	AST_VECTOR_INIT(&module->uses, 0);
	module->load_priority = AST_MODPRI_DEFAULT;
	if (AST_VECTOR_GET_CMP(&neverload, name, strcasecmp)) {
		module->neverload = 1;
	}

	return module;
}



static int module_manifest_load(char *filename)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE | CONFIG_FLAG_NOREALTIME };
	struct ast_variable *v;
	struct ast_module *module;

	cfg = ast_config_load2(filename, "" /* core, can't reload */, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to read '%s'.\n", filename);
		return -1;
	}

	module = module_manifest_alloc(filename, ast_variable_retrieve(cfg, "module", "name"));
	if (!module) {
		ast_config_destroy(cfg);
		return -1;
	}

	for (v = ast_variable_browse(cfg, "module"); v; v = v->next) {
		if (!strcmp(v->name, "name")) {
			/* Already given to module_alloc. */
		} else if (!strcmp(v->name, "checksum")) {
			if (!module->checksum) {
				module->checksum = ast_strdup(v->value);
			}
		} else if (!strcmp(v->name, "support_level")) {
			if (!strcmp(v->value, "core")) {
				module->support_level = AST_MODULE_SUPPORT_CORE;
			} else if (!strcmp(v->value, "extended")) {
				module->support_level = AST_MODULE_SUPPORT_EXTENDED;
			} else if (!strcmp(v->value, "deprecated")) {
				module->support_level = AST_MODULE_SUPPORT_DEPRECATED;
			} else {
				module->support_level = AST_MODULE_SUPPORT_UNKNOWN;
			}
		} else if (!strcmp(v->name, "load_priority")) {
			if (!strcmp(v->value, "realtime_depend")) {
				module->load_priority = AST_MODPRI_REALTIME_DEPEND;
			} else if (!strcmp(v->value, "realtime_depend2")) {
				module->load_priority = AST_MODPRI_REALTIME_DEPEND2;
			} else if (!strcmp(v->value, "realtime_driver")) {
				module->load_priority = AST_MODPRI_REALTIME_DRIVER;
			} else if (!strcmp(v->value, "timing")) {
				module->load_priority = AST_MODPRI_TIMING;
			} else if (!strcmp(v->value, "channel_depend")) {
				module->load_priority = AST_MODPRI_CHANNEL_DEPEND;
			} else if (!strcmp(v->value, "channel_driver")) {
				module->load_priority = AST_MODPRI_CHANNEL_DRIVER;
			} else if (!strcmp(v->value, "app_depend")) {
				module->load_priority = AST_MODPRI_APP_DEPEND;
			} else if (!strcmp(v->value, "devstate_provider")) {
				module->load_priority = AST_MODPRI_DEVSTATE_PROVIDER;
			} else if (!strcmp(v->value, "devstate_plugin")) {
				module->load_priority = AST_MODPRI_DEVSTATE_PLUGIN;
			} else if (!strcmp(v->value, "cdr_driver")) {
				module->load_priority = AST_MODPRI_CDR_DRIVER;
			} else if (!strcmp(v->value, "default")) {
				module->load_priority = AST_MODPRI_DEFAULT;
			} else if (!strcmp(v->value, "devstate_consumer")) {
				module->load_priority = AST_MODPRI_DEVSTATE_CONSUMER;
			}
		} else if (!strcmp(v->name, "export_globals")) {
			module->export_globals = ast_true(v->value) ? 1 : 0;
		} else if (!strcmp(v->name, "config")) {
			if (!AST_VECTOR_GET_CMP(&module->configs, v->value, !strcmp)) {
				AST_VECTOR_APPEND(&module->configs, ast_str_ao2_alloc(v->value));
			}
		} else {
			ast_log(LOG_ERROR, "Unknown property '%s' in manifest: %s\n", v->name, filename);
		}
	}

	for (v = ast_variable_browse(cfg, "uses"); v; v = v->next) {
		if (module_manifest_uses_add(module, v->name, v->value)) {
			goto cleanup;
		}
	}

	for (v = ast_variable_browse(cfg, "provides"); v; v = v->next) {
		module_provider_add(module, v->name, v->value);
	}

	AST_VECTOR_ADD_SORTED(&modules, module, MODULES_VECTOR_SORT);
	if (ast_opt_ref_debug) {
		ao2_t_ref(module, +1, "add to vector");
		ao2_t_ref(module, -1, "drop constructor ref");
	}
	ast_config_destroy(cfg);

	return 0;

cleanup:
	ao2_ref(module, -1);

	ast_config_destroy(cfg);

	return -1;
}

int module_manifest_init(void)
{
	char searchpattern[PATH_MAX];
	int ret;
	int i;
	glob_t globbuf;

	snprintf(searchpattern, sizeof(searchpattern), "%s/*.manifest", ast_config_AST_MODULE_DIR);
	ret = glob(searchpattern, GLOB_NOCHECK, NULL, &globbuf);
	if (ret == GLOB_NOSPACE || ret == GLOB_ABORTED) {
		ast_log(LOG_ERROR, "Module Manifest load failure, glob expansion of pattern '%s' failed\n", searchpattern);
		return -1;
	}

	AST_VECTOR_RW_INIT(&modules, globbuf.gl_pathc);
	AST_VECTOR_RW_INIT(&providertypes, 10);

	for (i = 0; i < globbuf.gl_pathc; i++) {
		/* check for duplicates (if we already [try to] open the same file. */
		int dup, duplicate = 0;

		for (dup = 0; dup < i; dup++) {
			if (!strcmp(globbuf.gl_pathv[i], globbuf.gl_pathv[dup])) {
				duplicate = 1;
				break;
			}
		}
		if (duplicate || strchr(globbuf.gl_pathv[i], '*')) {
			/* skip duplicates as well as pathnames not found
			 * (due to use of GLOB_NOCHECK) */
			continue;
		}

		if (module_manifest_load(globbuf.gl_pathv[i])) {
			/* BUGBUG: abort startup here? */
			ast_log(LOG_WARNING, "Failed to load '%s'\n", globbuf.gl_pathv[i]);
		}
	}

	globfree(&globbuf);

	return 0;
}

/* Ensure module is listed in alldeps*/
static int module_manifest_scan_alldeps(struct ast_module *checkroot, struct ast_module *module)
{
	int i;
	int ret = 0;

	if (module == NULL) {
		module = checkroot;
		if (checkroot->alldeps_error) {
			return -1;
		}
		if (checkroot->alldeps_inited) {
			return 0;
		}
	} else if (checkroot == module) {
		ast_log(LOG_ERROR, "Circular dependency for '%s', cannot proceed.\n", checkroot->name);
		checkroot->alldeps_error = 1;
		return -1;
	}

	if (module->alldeps_error) {
		return -1;
	}

	/* If alldeps has already been built, no need to resolve uses again */
	if (module->alldeps_inited) {
		for (i = 0; i < AST_VECTOR_SIZE(&module->alldeps); i++) {
			char *value = AST_VECTOR_GET(&module->alldeps, i);

			if (!strcmp(checkroot->name, value)) {
				ast_log(LOG_ERROR, "Circular dependency with '%s'\n", checkroot->name);
				return -1;
			}

			if (!AST_VECTOR_GET_CMP(&checkroot->alldeps, value, !strcmp)) {
				AST_VECTOR_APPEND(&checkroot->alldeps, value);
				ao2_ref(value, +1);
			}
		}

		return ret;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&module->uses); i++) {
		int li;
		struct ast_module_uses *uses = AST_VECTOR_GET(&module->uses, i);
		int is_module = !strcmp(uses->type, "module");

		for (li = 0; li < AST_VECTOR_SIZE(&uses->values); li++) {
			char *value = AST_VECTOR_GET(&uses->values, li);
			struct ast_module *dep;

			if (is_module) {
				dep = ast_module_find(value);
			} else {
				dep = ast_module_find_provider(uses->type, value);
			}

			if (!dep) {
				ast_log(LOG_ERROR, "Cannot find dependency for module %s: %s:%s\n", module->name, uses->type, value);
				ret = -1;
				goto inner_clean;
			}

			if (dep == checkroot || dep == module) {
				ast_log(LOG_ERROR, "Module %s cannot be loaded due to a circular dependency loop", module->name);
				ret = -1;
				goto inner_clean;
			}

			if (!AST_VECTOR_GET_CMP(&checkroot->alldeps, dep->name, !strcmp)) {
				AST_VECTOR_APPEND(&checkroot->alldeps, dep->name);
				ao2_t_ref(dep->name, +1, "add to module->alldeps");
				/* We haven't already encountered dep, so add it's deps to the list */
				if (module_manifest_scan_alldeps(checkroot, dep)) {
					ast_log(LOG_ERROR, "Dependency scan of module %s cannot be completed due to an error with %s\n", module->name, dep->name);
					/* Scan of dep failed, so we've failed. */
					dep->alldeps_error = 1;
					ret = -1;
				}
			}

inner_clean:
			ao2_t_ref(dep, -1, "drop ast_module_find");
			if (ret) {
				module->alldeps_error = 1;
				goto doreturn;
			}
		}
	}

doreturn:
	return ret;
}

int module_manifest_build_alldeps(void)
{
	int ret = 0;
	int i;
	struct ast_module *module;

	/*! First set alldeps_inited on all modules with no dependencies. */
	for (i = 0; i < AST_VECTOR_SIZE(&modules); i++) {
		module = AST_VECTOR_GET(&modules, i);
		if (!AST_VECTOR_SIZE(&module->uses)) {
			module->alldeps_inited = 1;
		}
	}

	/*! Now initialize modules that do have dependencies. */
	for (i = 0; i < AST_VECTOR_SIZE(&modules); i++) {
		module = AST_VECTOR_GET(&modules, i);
		if (module->alldeps_inited || module->alldeps_error) {
			continue;
		}

		ret = module_manifest_scan_alldeps(module, NULL);
		if (ret) {
			module->alldeps_error = 1;
			AST_VECTOR_RESET(&module->alldeps, ao2_cleanup);
		} else {
			module->alldeps_inited = 1;
		}
	}

	return ret;
}

