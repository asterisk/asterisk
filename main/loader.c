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
 *
 * \brief Module Loader
 * \author Mark Spencer <markster@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Luigi Rizzo <rizzo@icir.org>
 * - See ModMngMnt
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_MODULE_DIR */
#include <dirent.h>

#include "asterisk/dlinkedlists.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/io.h"
#include "asterisk/lock.h"
#include "asterisk/vector.h"
#include "asterisk/app.h"
#include "asterisk/test.h"
#include "asterisk/cli.h"

#include <dlfcn.h>

#include "asterisk/md5.h"
#include "asterisk/utils.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="Reload">
		<managerEventInstance class="EVENT_FLAG_SYSTEM">
			<synopsis>Raised when a module has been reloaded in Asterisk.</synopsis>
			<syntax>
				<parameter name="Module">
					<para>The name of the module that was reloaded, or
					<literal>All</literal> if all modules were reloaded</para>
				</parameter>
				<parameter name="Status">
					<para>The numeric status code denoting the success or failure
					of the reload request.</para>
					<enumlist>
						<enum name="0"><para>Success</para></enum>
						<enum name="1"><para>Request queued</para></enum>
						<enum name="2"><para>Module not found</para></enum>
						<enum name="3"><para>Error</para></enum>
						<enum name="4"><para>Reload already in progress</para></enum>
						<enum name="5"><para>Module uninitialized</para></enum>
						<enum name="6"><para>Reload not supported</para></enum>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Load">
		<managerEventInstance class="EVENT_FLAG_SYSTEM">
			<synopsis>Raised when a module has been loaded in Asterisk.</synopsis>
			<syntax>
				<parameter name="Module">
					<para>The name of the module that was loaded</para>
				</parameter>
				<parameter name="Status">
					<para>The result of the load request.</para>
					<enumlist>
						<enum name="Failure"><para>Module could not be loaded properly</para></enum>
						<enum name="Success"><para>Module loaded and configured</para></enum>
						<enum name="Decline"><para>Module is not configured</para></enum>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Unload">
		<managerEventInstance class="EVENT_FLAG_SYSTEM">
			<synopsis>Raised when a module has been unloaded in Asterisk.</synopsis>
			<syntax>
				<parameter name="Module">
					<para>The name of the module that was unloaded</para>
				</parameter>
				<parameter name="Status">
					<para>The result of the unload request.</para>
					<enumlist>
						<enum name="Success"><para>Module unloaded successfully</para></enum>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif

struct ast_module_user {
	struct ast_channel *chan;
	AST_LIST_ENTRY(ast_module_user) entry;
};

AST_DLLIST_HEAD(module_user_list, ast_module_user);

static const unsigned char expected_key[] =
{ 0x87, 0x76, 0x79, 0x35, 0x23, 0xea, 0x3a, 0xd3,
  0x25, 0x2a, 0xbb, 0x35, 0x87, 0xe4, 0x22, 0x24 };

static char buildopt_sum[33] = AST_BUILDOPT_SUM;

AST_VECTOR(module_vector, struct ast_module *);

/*! Used with AST_VECTOR_CALLBACK_VOID to create a
 * comma separated list of module names for error messages. */
#define STR_APPEND_TEXT(txt, str) \
	ast_str_append(str, 0, "%s%s", \
		ast_str_strlen(*(str)) > 0 ? ", " : "", \
		txt)

/* Built-in module registrations need special handling at startup */
static unsigned int loader_ready;

/*! String container for deferring output of startup errors. */
static struct ast_vector_string startup_errors;
static struct ast_str *startup_error_builder;

#if defined(HAVE_PERMANENT_DLOPEN)
#define FIRST_DLOPEN 999

struct ao2_container *info_list = NULL;

struct info_list_obj {
	const struct ast_module_info *info;
	int dlopened;
	char name[0];
};

static struct info_list_obj *info_list_obj_alloc(const char *name,
	const struct ast_module_info *info)
{
	struct info_list_obj *new_entry;

	new_entry = ao2_alloc(sizeof(*new_entry) + strlen(name) + 1, NULL);

	if (!new_entry) {
		return NULL;
	}

	strcpy(new_entry->name, name); /* SAFE */
	new_entry->info = info;
	new_entry->dlopened = FIRST_DLOPEN;

	return new_entry;
}

AO2_STRING_FIELD_CMP_FN(info_list_obj, name)

static char *get_name_from_resource(const char *resource)
{
	int len;
	const char *last_three;
	char *mod_name;

	if (!resource) {
		return NULL;
	}

	len = strlen(resource);
	if (len > 3) {
		last_three = &resource[len-3];
		if (!strcasecmp(last_three, ".so")) {
			mod_name = ast_calloc(1, len - 2);
			if (mod_name) {
				ast_copy_string(mod_name, resource, len - 2);
				return mod_name;
			} else {
				/* Unable to allocate memory. */
				return NULL;
			}
		}
	}

	/* Resource is the name - happens when manually unloading a module. */
	mod_name = ast_calloc(1, len + 1);
	if (mod_name) {
		ast_copy_string(mod_name, resource, len + 1);
		return mod_name;
	}

	/* Unable to allocate memory. */
	return NULL;
}

static void manual_mod_reg(const void *lib, const char *resource)
{
	struct info_list_obj *obj_tmp;
	char *mod_name;

	if (lib) {
		mod_name = get_name_from_resource(resource);
		if (mod_name) {
			obj_tmp = ao2_find(info_list, mod_name, OBJ_SEARCH_KEY);
			if (obj_tmp) {
				if (obj_tmp->dlopened == FIRST_DLOPEN) {
					obj_tmp->dlopened = 1;
				} else {
					ast_module_register(obj_tmp->info);
				}
				ao2_ref(obj_tmp, -1);
			}
			ast_free(mod_name);
		}
	}
}

static void manual_mod_unreg(const char *resource)
{
	struct info_list_obj *obj_tmp;
	char *mod_name;

	/* When Asterisk shuts down the destructor is called automatically. */
	if (ast_shutdown_final()) {
		return;
	}

	mod_name = get_name_from_resource(resource);
	if (mod_name) {
		obj_tmp = ao2_find(info_list, mod_name, OBJ_SEARCH_KEY);
		if (obj_tmp) {
			ast_module_unregister(obj_tmp->info);
			ao2_ref(obj_tmp, -1);
		}
		ast_free(mod_name);
	}
}
#endif

static __attribute__((format(printf, 1, 2))) void module_load_error(const char *fmt, ...)
{
	char *copy = NULL;
	va_list ap;

	va_start(ap, fmt);
	if (startup_error_builder) {
		ast_str_set_va(&startup_error_builder, 0, fmt, ap);
		copy = ast_strdup(ast_str_buffer(startup_error_builder));
		if (!copy || AST_VECTOR_APPEND(&startup_errors, copy)) {
			ast_log(LOG_ERROR, "%s", ast_str_buffer(startup_error_builder));
			ast_free(copy);
		}
	} else {
		ast_log_ap(LOG_ERROR, fmt, ap);
	}
	va_end(ap);
}

/*!
 * \brief Internal flag to indicate all modules have been initially loaded.
 */
static int modules_loaded;

struct ast_module {
	const struct ast_module_info *info;
	/*! Used to get module references into refs log */
	void *ref_debug;
	/*! The shared lib. */
	void *lib;
	/*! Number of 'users' and other references currently holding the module. */
	int usecount;
	/*! List of users holding the module. */
	struct module_user_list users;

	/*! List of required module names. */
	struct ast_vector_string requires;
	/*! List of optional api modules. */
	struct ast_vector_string optional_modules;
	/*! List of modules this enhances. */
	struct ast_vector_string enhances;

	/*!
	 * \brief Vector holding pointers to modules we have a reference to.
	 *
	 * When one module requires another, the required module gets added
	 * to this list with a reference.
	 */
	struct module_vector reffed_deps;
	struct {
		/*! The module running and ready to accept requests. */
		unsigned int running:1;
		/*! The module has declined to start. */
		unsigned int declined:1;
		/*! This module is being held open until it's time to shutdown. */
		unsigned int keepuntilshutdown:1;
		/*! The module is built-in. */
		unsigned int builtin:1;
		/*! The admin has declared this module is required. */
		unsigned int required:1;
		/*! This module is marked for preload. */
		unsigned int preload:1;
	} flags;
	AST_DLLIST_ENTRY(ast_module) entry;
	char resource[0];
};

static AST_DLLIST_HEAD_STATIC(module_list, ast_module);


struct load_results_map {
	int result;
	const char *name;
};

static const struct load_results_map load_results[] = {
	{ AST_MODULE_LOAD_SUCCESS, "Success" },
	{ AST_MODULE_LOAD_DECLINE, "Decline" },
	{ AST_MODULE_LOAD_SKIP, "Skip" },
	{ AST_MODULE_LOAD_PRIORITY, "Priority" },
	{ AST_MODULE_LOAD_FAILURE, "Failure" },
};
#define AST_MODULE_LOAD_UNKNOWN_STRING		"Unknown"		/* Status string for unknown load status */

static void publish_load_message_type(const char* type, const char *name, const char *status);
static void publish_reload_message(const char *name, enum ast_module_reload_result result);
static void publish_load_message(const char *name, enum ast_module_load_result result);
static void publish_unload_message(const char *name, const char* status);


/*
 * module_list is cleared by its constructor possibly after
 * we start accumulating built-in modules, so we need to
 * use another list (without the lock) to accumulate them.
 */
static struct module_list builtin_module_list;

static int module_vector_strcasecmp(struct ast_module *a, struct ast_module *b)
{
	return strcasecmp(a->resource, b->resource);
}

static int module_vector_cmp(struct ast_module *a, struct ast_module *b)
{
	int preload_diff = (int)b->flags.preload - (int)a->flags.preload;
	/* if load_pri is not set, default is 128.  Lower is better */
	int a_pri = ast_test_flag(a->info, AST_MODFLAG_LOAD_ORDER)
		? a->info->load_pri : AST_MODPRI_DEFAULT;
	int b_pri = ast_test_flag(b->info, AST_MODFLAG_LOAD_ORDER)
		? b->info->load_pri : AST_MODPRI_DEFAULT;

	if (preload_diff) {
		/* -1 preload a but not b */
		/*  0 preload both or neither */
		/*  1 preload b but not a */
		return preload_diff;
	}

	/*
	 * Returns comparison values for a vector sorted by priority.
	 * <0 a_pri < b_pri
	 * =0 a_pri == b_pri
	 * >0 a_pri > b_pri
	 */
	return a_pri - b_pri;
}

static struct ast_module *find_resource(const char *resource, int do_lock);

/*!
 * \internal
 * \brief Add a reference from mod to dep.
 *
 * \param mod Owner of the new reference.
 * \param dep Module to reference
 * \param missing Vector to store name of \a dep if it is not running.
 *
 * This function returns failure if \a dep is not running and \a missing
 * is NULL.  If \a missing is not NULL errors will only be returned for
 * allocation failures.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note Adding a second reference to the same dep will return success
 *       without doing anything.
 */
static int module_reffed_deps_add(struct ast_module *mod, struct ast_module *dep,
	struct ast_vector_const_string *missing)
{
	if (!dep->flags.running) {
		return !missing ? -1 : AST_VECTOR_APPEND(missing, dep->info->name);
	}

	if (AST_VECTOR_GET_CMP(&mod->reffed_deps, dep, AST_VECTOR_ELEM_DEFAULT_CMP)) {
		/* Skip duplicate. */
		return 0;
	}

	if (AST_VECTOR_APPEND(&mod->reffed_deps, dep)) {
		return -1;
	}

	ast_module_ref(dep);

	return 0;
}

/*!
 * \internal
 * \brief Add references for modules that enhance a dependency.
 *
 * \param mod Owner of the new references.
 * \param dep Module to check for enhancers.
 * \param missing Vector to store name of any enhancer that is not running or declined.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
static int module_reffed_deps_add_dep_enhancers(struct ast_module *mod,
	struct ast_module *dep, struct ast_vector_const_string *missing)
{
	struct ast_module *cur;

	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		if (cur->flags.declined) {
			continue;
		}

		if (!AST_VECTOR_GET_CMP(&cur->enhances, dep->info->name, !strcasecmp)) {
			/* dep is not enhanced by cur. */
			continue;
		}

		/* dep is enhanced by cur, therefore mod requires cur. */
		if (module_reffed_deps_add(mod, cur, missing)) {
			return -1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Add references to a list of dependencies.
 *
 * \param mod Owner of the new references.
 * \param vec List of required modules to process
 * \param missing Vector to store names of modules that are not running.
 * \param ref_enhancers Reference all enhancers of each required module.
 * \param isoptional Modules that are not loaded can be ignored.
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
static int module_deps_process_reqlist(struct ast_module *mod,
	struct ast_vector_string *vec, struct ast_vector_const_string *missing,
	int ref_enhancers, int isoptional)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(vec); idx++) {
		const char *depname = AST_VECTOR_GET(vec, idx);
		struct ast_module *dep = find_resource(depname, 0);

		if (!dep || !dep->flags.running) {
			if (isoptional && !dep) {
				continue;
			}

			if (missing && !AST_VECTOR_APPEND(missing, depname)) {
				continue;
			}

			return -1;
		}

		if (module_reffed_deps_add(mod, dep, missing)) {
			return -1;
		}

		if (ref_enhancers && module_reffed_deps_add_dep_enhancers(mod, dep, missing)) {
			return -1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Grab all references required to start the module.
 *
 * \param mod The module we're trying to start.
 * \param missing Vector to store a list of missing dependencies.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note module_list must be locked.
 *
 * \note Caller is responsible for initializing and freeing the vector.
 *       Elements are safely read only while module_list remains locked.
 */
static int module_deps_reference(struct ast_module *mod, struct ast_vector_const_string *missing)
{
	int res = 0;

	/* Grab references to modules we enhance but not other enhancements. */
	res |= module_deps_process_reqlist(mod, &mod->enhances, missing, 0, 0);

	/* Grab references to modules we require plus enhancements. */
	res |= module_deps_process_reqlist(mod, &mod->requires, missing, 1, 0);

	/* Grab references to optional modules including enhancements. */
	res |= module_deps_process_reqlist(mod, &mod->optional_modules, missing, 1, 1);

	return res;
}

/*!
 * \brief Recursively find required dependencies that are not running.
 *
 * \param mod Module to scan for dependencies.
 * \param missingdeps Vector listing modules that must be started first.
 *
 * \retval 0 All dependencies resolved.
 * \retval -1 Failed to resolve some dependencies.
 *
 * An error from this function usually means a required module is not even
 * loaded.  This function is safe from infinite recursion, but dependency
 * loops are not reported as an error from here.  On success missingdeps
 * will contain a list of every module that needs to be running before this
 * module can start.  missingdeps is sorted by load priority so any missing
 * dependencies can be started if needed.
 */
static int module_deps_missing_recursive(struct ast_module *mod, struct module_vector *missingdeps)
{
	int i = 0;
	int res = -1;
	struct ast_vector_const_string localdeps;
	struct ast_module *dep;

	/*
	 * localdeps stores a copy of all dependencies that mod could not reference.
	 * First we discard modules that we've already found. We add all newly found
	 * modules to the missingdeps vector then scan them recursively.  This will
	 * ensure we quickly run out of stuff to do.
	 */
	AST_VECTOR_INIT(&localdeps, 0);
	if (module_deps_reference(mod, &localdeps)) {
		goto clean_return;
	}

	while (i < AST_VECTOR_SIZE(&localdeps)) {
		dep = find_resource(AST_VECTOR_GET(&localdeps, i), 0);
		if (!dep) {
			goto clean_return;
		}

		if (AST_VECTOR_GET_CMP(missingdeps, dep, AST_VECTOR_ELEM_DEFAULT_CMP)) {
			/* Skip common dependency.  We have already searched it. */
			AST_VECTOR_REMOVE(&localdeps, i, 0);
		} else {
			/* missingdeps is the real list so keep it sorted. */
			if (AST_VECTOR_ADD_SORTED(missingdeps, dep, module_vector_cmp)) {
				goto clean_return;
			}
			i++;
		}
	}

	res = 0;
	for (i = 0; !res && i < AST_VECTOR_SIZE(&localdeps); i++) {
		dep = find_resource(AST_VECTOR_GET(&localdeps, i), 0);
		/* We've already confirmed dep is loaded in the first loop. */
		res = module_deps_missing_recursive(dep, missingdeps);
	}

clean_return:
	AST_VECTOR_FREE(&localdeps);

	return res;
}

const char *ast_module_name(const struct ast_module *mod)
{
	if (!mod || !mod->info) {
		return NULL;
	}

	return mod->info->name;
}

struct loadupdate {
	int (*updater)(void);
	AST_LIST_ENTRY(loadupdate) entry;
};

static AST_DLLIST_HEAD_STATIC(updaters, loadupdate);

AST_MUTEX_DEFINE_STATIC(reloadlock);

struct reload_queue_item {
	AST_LIST_ENTRY(reload_queue_item) entry;
	char module[0];
};

static int do_full_reload = 0;

static AST_DLLIST_HEAD_STATIC(reload_queue, reload_queue_item);

/*!
 * \internal
 *
 * This variable is set by load_dynamic_module so ast_module_register
 * can know what pointer is being registered.
 *
 * This is protected by the module_list lock.
 */
static struct ast_module * volatile resource_being_loaded;

/*!
 * \internal
 * \brief Used by AST_MODULE_INFO to register with the module loader.
 *
 * This function is automatically called when each module is opened.
 * It must never be used from outside AST_MODULE_INFO.
 */
void ast_module_register(const struct ast_module_info *info)
{
	struct ast_module *mod;

	if (!loader_ready) {
		mod = ast_std_calloc(1, sizeof(*mod) + strlen(info->name) + 1);
		if (!mod) {
			/* We haven't even reached main() yet, if we can't
			 * allocate memory at this point just give up. */
			fprintf(stderr, "Allocation failure during startup.\n");
			exit(2);
		}
		strcpy(mod->resource, info->name); /* safe */
		mod->info = info;
		mod->flags.builtin = 1;
		AST_DLLIST_INSERT_TAIL(&builtin_module_list, mod, entry);

		/* ast_module_register for built-in modules is run again during module preload. */
		return;
	}

	/*
	 * This lock protects resource_being_loaded as well as the module
	 * list.  Normally we already have a lock on module_list when we
	 * begin the load but locking again from here prevents corruption
	 * if an asterisk module is dlopen'ed from outside the module loader.
	 */
	AST_DLLIST_LOCK(&module_list);
	mod = resource_being_loaded;
	if (!mod) {
		AST_DLLIST_UNLOCK(&module_list);
		return;
	}

	ast_debug(5, "Registering module %s\n", info->name);

	/* This tells load_dynamic_module that we're registered. */
	resource_being_loaded = NULL;

	mod->info = info;
	if (ast_opt_ref_debug) {
		mod->ref_debug = ao2_t_alloc_options(0, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK, info->name);
	}
	AST_LIST_HEAD_INIT(&mod->users);
	AST_VECTOR_INIT(&mod->requires, 0);
	AST_VECTOR_INIT(&mod->optional_modules, 0);
	AST_VECTOR_INIT(&mod->enhances, 0);
	AST_VECTOR_INIT(&mod->reffed_deps, 0);

	AST_DLLIST_INSERT_TAIL(&module_list, mod, entry);
	AST_DLLIST_UNLOCK(&module_list);

	/* give the module a copy of its own handle, for later use in registrations and the like */
	*((struct ast_module **) &(info->self)) = mod;

#if defined(HAVE_PERMANENT_DLOPEN)
	if (mod->flags.builtin != 1) {
		struct info_list_obj *obj_tmp = ao2_find(info_list, info->name,
			OBJ_SEARCH_KEY);

		if (!obj_tmp) {
			obj_tmp = info_list_obj_alloc(info->name, info);
			if (obj_tmp) {
				ao2_link(info_list, obj_tmp);
				ao2_ref(obj_tmp, -1);
			}
		} else {
			ao2_ref(obj_tmp, -1);
		}
	}
#endif
}

static int module_post_register(struct ast_module *mod)
{
	int res;

	/* Split lists from mod->info. */
	res  = ast_vector_string_split(&mod->requires, mod->info->requires, ",", 0, strcasecmp);
	res |= ast_vector_string_split(&mod->optional_modules, mod->info->optional_modules, ",", 0, strcasecmp);
	res |= ast_vector_string_split(&mod->enhances, mod->info->enhances, ",", 0, strcasecmp);

	return res;
}

static void module_destroy(struct ast_module *mod)
{
	AST_VECTOR_CALLBACK_VOID(&mod->requires, ast_free);
	AST_VECTOR_FREE(&mod->requires);

	AST_VECTOR_CALLBACK_VOID(&mod->optional_modules, ast_free);
	AST_VECTOR_FREE(&mod->optional_modules);

	AST_VECTOR_CALLBACK_VOID(&mod->enhances, ast_free);
	AST_VECTOR_FREE(&mod->enhances);

	/* Release references to all dependencies. */
	AST_VECTOR_CALLBACK_VOID(&mod->reffed_deps, ast_module_unref);
	AST_VECTOR_FREE(&mod->reffed_deps);

	AST_LIST_HEAD_DESTROY(&mod->users);
	ao2_cleanup(mod->ref_debug);
	if (mod->flags.builtin) {
		ast_std_free(mod);
	} else {
		ast_free(mod);
	}
}

void ast_module_unregister(const struct ast_module_info *info)
{
	struct ast_module *mod = NULL;

	/* it is assumed that the users list in the module structure
	   will already be empty, or we cannot have gotten to this
	   point
	*/
	AST_DLLIST_LOCK(&module_list);
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&module_list, mod, entry) {
		if (mod->info == info) {
			AST_DLLIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
	AST_DLLIST_UNLOCK(&module_list);

	if (mod && !mod->usecount) {
		/*
		 * We are intentionally leaking mod if usecount is not zero.
		 * This is necessary if the module is being forcefully unloaded.
		 * In addition module_destroy is not safe to run after exit()
		 * is called.  ast_module_unregister is run during cleanup of
		 * the process when libc releases each module's shared object
		 * library.
		 */
		ast_debug(5, "Unregistering module %s\n", info->name);
		module_destroy(mod);
	}
}

struct ast_module_user *__ast_module_user_add(struct ast_module *mod, struct ast_channel *chan)
{
	struct ast_module_user *u;

	u = ast_calloc(1, sizeof(*u));
	if (!u) {
		return NULL;
	}

	u->chan = chan;

	AST_LIST_LOCK(&mod->users);
	AST_LIST_INSERT_HEAD(&mod->users, u, entry);
	AST_LIST_UNLOCK(&mod->users);

	if (mod->ref_debug) {
		ao2_ref(mod->ref_debug, +1);
	}

	ast_atomic_fetchadd_int(&mod->usecount, +1);

	ast_update_use_count();

	return u;
}

void __ast_module_user_remove(struct ast_module *mod, struct ast_module_user *u)
{
	if (!u) {
		return;
	}

	AST_LIST_LOCK(&mod->users);
	u = AST_LIST_REMOVE(&mod->users, u, entry);
	AST_LIST_UNLOCK(&mod->users);
	if (!u) {
		/*
		 * Was not in the list.  Either a bad pointer or
		 * __ast_module_user_hangup_all() has been called.
		 */
		return;
	}

	if (mod->ref_debug) {
		ao2_ref(mod->ref_debug, -1);
	}

	ast_atomic_fetchadd_int(&mod->usecount, -1);
	ast_free(u);

	ast_update_use_count();
}

void __ast_module_user_hangup_all(struct ast_module *mod)
{
	struct ast_module_user *u;

	AST_LIST_LOCK(&mod->users);
	while ((u = AST_LIST_REMOVE_HEAD(&mod->users, entry))) {
		if (u->chan) {
			ast_softhangup(u->chan, AST_SOFTHANGUP_APPUNLOAD);
		}

		if (mod->ref_debug) {
			ao2_ref(mod->ref_debug, -1);
		}

		ast_atomic_fetchadd_int(&mod->usecount, -1);
		ast_free(u);
	}
	AST_LIST_UNLOCK(&mod->users);

	ast_update_use_count();
}

static int printdigest(const unsigned char *d)
{
	int x, pos;
	char buf[256]; /* large enough so we don't have to worry */

	for (pos = 0, x = 0; x < 16; x++)
		pos += sprintf(buf + pos, " %02hhx", *d++);

	ast_debug(1, "Unexpected signature:%s\n", buf);

	return 0;
}

static int key_matches(const unsigned char *key1, const unsigned char *key2)
{
	int x;

	for (x = 0; x < 16; x++) {
		if (key1[x] != key2[x])
			return 0;
	}

	return 1;
}

static int verify_key(const unsigned char *key)
{
	struct MD5Context c;
	unsigned char digest[16];

	MD5Init(&c);
	MD5Update(&c, key, strlen((char *)key));
	MD5Final(digest, &c);

	if (key_matches(expected_key, digest))
		return 0;

	printdigest(digest);

	return -1;
}

static size_t resource_name_baselen(const char *name)
{
	size_t len = strlen(name);

	if (len > 3 && !strcasecmp(name + len - 3, ".so")) {
		return len - 3;
	}

	return len;
}

static int resource_name_match(const char *name1, size_t baselen1, const char *name2)
{
	if (baselen1 != resource_name_baselen(name2)) {
		return -1;
	}

	return strncasecmp(name1, name2, baselen1);
}

static struct ast_module *find_resource(const char *resource, int do_lock)
{
	struct ast_module *cur;
	size_t resource_baselen = resource_name_baselen(resource);

	if (do_lock) {
		AST_DLLIST_LOCK(&module_list);
	}

	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		if (!resource_name_match(resource, resource_baselen, cur->resource)) {
			break;
		}
	}

	if (do_lock) {
		AST_DLLIST_UNLOCK(&module_list);
	}

	return cur;
}

/*!
 * \brief dlclose(), with failure logging.
 */
static void logged_dlclose(const char *name, void *lib)
{
	char *error;

	if (!lib) {
		return;
	}

	/* Clear any existing error */
	dlerror();
	if (dlclose(lib)) {
		error = dlerror();
		ast_log(AST_LOG_ERROR, "Failure in dlclose for module '%s': %s\n",
			S_OR(name, "unknown"), S_OR(error, "Unknown error"));
#if defined(HAVE_PERMANENT_DLOPEN)
	} else {
		manual_mod_unreg(name);
#endif
	}
}

#if defined(HAVE_RTLD_NOLOAD)
/*!
 * \brief Check to see if the given resource is loaded.
 *
 * \param resource_name Name of the resource, including .so suffix.
 * \return False (0) if module is not loaded.
 * \return True (non-zero) if module is loaded.
 */
static int is_module_loaded(const char *resource_name)
{
	char fn[PATH_MAX] = "";
	void *lib;

	snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_MODULE_DIR,
		resource_name);

	lib = dlopen(fn, RTLD_LAZY | RTLD_NOLOAD);

	if (lib) {
		logged_dlclose(resource_name, lib);
		return 1;
	}

	return 0;
}
#endif

static void unload_dynamic_module(struct ast_module *mod)
{
#if defined(HAVE_RTLD_NOLOAD)
	char *name = ast_strdupa(ast_module_name(mod));
#endif
	void *lib = mod->lib;

	/* WARNING: the structure pointed to by mod is going to
	   disappear when this operation succeeds, so we can't
	   dereference it */
	logged_dlclose(ast_module_name(mod), lib);

	/* There are several situations where the module might still be resident
	 * in memory.
	 *
	 * If somehow there was another dlopen() on the same module (unlikely,
	 * since that all is supposed to happen in loader.c).
	 *
	 * Avoid the temptation of repeating the dlclose(). The other code that
	 * dlopened the module still has its module reference, and should close
	 * it itself. In other situations, dlclose() will happily return success
	 * for as many times as you wish to call it.
	 */
#if defined(HAVE_RTLD_NOLOAD)
	if (is_module_loaded(name)) {
		ast_log(LOG_WARNING, "Module '%s' could not be completely unloaded\n", name);
	}
#endif
}

static int load_dlopen_missing(struct ast_str **list, struct ast_vector_string *deps)
{
	int i;
	int c = 0;

	for (i = 0; i < AST_VECTOR_SIZE(deps); i++) {
		const char *dep = AST_VECTOR_GET(deps, i);
		if (!find_resource(dep, 0)) {
			STR_APPEND_TEXT(dep, list);
			c++;
		}
	}

	return c;
}

/*!
 * \internal
 * \brief Attempt to dlopen a module.
 *
 * \param resource_in The module name to load.
 * \param so_ext ".so" or blank if ".so" is already part of resource_in.
 * \param filename Passed directly to dlopen.
 * \param flags Passed directly to dlopen.
 * \param suppress_logging Do not log any error from dlopen.
 *
 * \return Pointer to opened module, NULL on error.
 *
 * \warning module_list must be locked before calling this function.
 */
static struct ast_module *load_dlopen(const char *resource_in, const char *so_ext,
	const char *filename, int flags, unsigned int suppress_logging)
{
	struct ast_module *mod;

	ast_assert(!resource_being_loaded);

	mod = ast_calloc(1, sizeof(*mod) + strlen(resource_in) + strlen(so_ext) + 1);
	if (!mod) {
		return NULL;
	}

	sprintf(mod->resource, "%s%s", resource_in, so_ext); /* safe */

	resource_being_loaded = mod;
	mod->lib = dlopen(filename, flags);
#if defined(HAVE_PERMANENT_DLOPEN)
	manual_mod_reg(mod->lib, mod->resource);
#endif
	if (resource_being_loaded) {
		struct ast_str *list;
		int c = 0;
		const char *dlerror_msg = ast_strdupa(S_OR(dlerror(), ""));

		resource_being_loaded = NULL;
		if (mod->lib) {
			module_load_error("Module '%s' did not register itself during load\n", resource_in);
			logged_dlclose(resource_in, mod->lib);

			goto error_return;
		}

		if (suppress_logging) {
			goto error_return;
		}

		resource_being_loaded = mod;
		mod->lib = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
#if defined(HAVE_PERMANENT_DLOPEN)
		manual_mod_reg(mod->lib, mod->resource);
#endif
		if (resource_being_loaded) {
			resource_being_loaded = NULL;

			module_load_error("Error loading module '%s': %s\n", resource_in, dlerror_msg);
			logged_dlclose(resource_in, mod->lib);

			goto error_return;
		}

		list = ast_str_create(64);
		if (list) {
			if (module_post_register(mod)) {
				goto loaded_error;
			}

			c = load_dlopen_missing(&list, &mod->requires);
			c += load_dlopen_missing(&list, &mod->enhances);
#ifndef OPTIONAL_API
			c += load_dlopen_missing(&list, &mod->optional_modules);
#endif
		}

		if (list && ast_str_strlen(list)) {
			module_load_error("Error loading module '%s', missing %s: %s\n",
				resource_in, c == 1 ? "dependency" : "dependencies", ast_str_buffer(list));
		} else {
			module_load_error("Error loading module '%s': %s\n", resource_in, dlerror_msg);
		}

loaded_error:
		ast_free(list);
		unload_dynamic_module(mod);

		return NULL;

error_return:
		ast_free(mod);

		return NULL;
	}

	return mod;
}

static struct ast_module *load_dynamic_module(const char *resource_in, unsigned int suppress_logging)
{
	char fn[PATH_MAX];
	struct ast_module *mod;
	size_t resource_in_len = strlen(resource_in);
	const char *so_ext = "";

	if (resource_in_len < 4 || strcasecmp(resource_in + resource_in_len - 3, ".so")) {
		so_ext = ".so";
	}

	snprintf(fn, sizeof(fn), "%s/%s%s", ast_config_AST_MODULE_DIR, resource_in, so_ext);

	/* Try loading in quiet mode first with RTLD_LOCAL.  The majority of modules do not
	 * export symbols so this allows the least number of calls to dlopen. */
	mod = load_dlopen(resource_in, so_ext, fn, RTLD_NOW | RTLD_LOCAL, suppress_logging);

	if (!mod || !ast_test_flag(mod->info, AST_MODFLAG_GLOBAL_SYMBOLS)) {
		return mod;
	}

	/* Close the module so we can reopen with correct flags. */
	logged_dlclose(resource_in, mod->lib);

	return load_dlopen(resource_in, so_ext, fn, RTLD_NOW | RTLD_GLOBAL, 0);
}

int modules_shutdown(void)
{
	struct ast_module *mod;
	int somethingchanged;
	int res;

	AST_DLLIST_LOCK(&module_list);

	/*!\note Some resources, like timers, are started up dynamically, and thus
	 * may be still in use, even if all channels are dead.  We must therefore
	 * check the usecount before asking modules to unload. */
	do {
		/* Reset flag before traversing the list */
		somethingchanged = 0;

		AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&module_list, mod, entry) {
			if (mod->usecount) {
				ast_debug(1, "Passing on %s: its use count is %d\n",
					mod->resource, mod->usecount);
				continue;
			}
			AST_DLLIST_REMOVE_CURRENT(entry);
			if (mod->flags.running && !mod->flags.declined && mod->info->unload) {
				ast_verb(1, "Unloading %s\n", mod->resource);
				mod->info->unload();
			}
			module_destroy(mod);
			somethingchanged = 1;
		}
		AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
		if (!somethingchanged) {
			AST_DLLIST_TRAVERSE(&module_list, mod, entry) {
				if (mod->flags.keepuntilshutdown) {
					ast_module_unref(mod);
					mod->flags.keepuntilshutdown = 0;
					somethingchanged = 1;
				}
			}
		}
	} while (somethingchanged);

	res = AST_DLLIST_EMPTY(&module_list);
	AST_DLLIST_UNLOCK(&module_list);

	return !res;
}

int ast_unload_resource(const char *resource_name, enum ast_module_unload_mode force)
{
	struct ast_module *mod;
	int res = -1;
	int error = 0;

	AST_DLLIST_LOCK(&module_list);

	if (!(mod = find_resource(resource_name, 0))) {
		AST_DLLIST_UNLOCK(&module_list);
		ast_log(LOG_WARNING, "Unload failed, '%s' could not be found\n", resource_name);
		return -1;
	}

	if (!mod->flags.running || mod->flags.declined) {
		ast_log(LOG_WARNING, "Unload failed, '%s' is not loaded.\n", resource_name);
		error = 1;
	}

	if (!error && (mod->usecount > 0)) {
		if (force)
			ast_log(LOG_WARNING, "Warning:  Forcing removal of module '%s' with use count %d\n",
				resource_name, mod->usecount);
		else {
			ast_log(LOG_WARNING, "Soft unload failed, '%s' has use count %d\n", resource_name,
				mod->usecount);
			error = 1;
		}
	}

	if (!error) {
		/* Request any channels attached to the module to hangup. */
		__ast_module_user_hangup_all(mod);

		ast_verb(1, "Unloading %s\n", mod->resource);
		res = mod->info->unload();
		if (res) {
			ast_log(LOG_WARNING, "Firm unload failed for %s\n", resource_name);
			if (force <= AST_FORCE_FIRM) {
				error = 1;
			} else {
				ast_log(LOG_WARNING, "** Dangerous **: Unloading resource anyway, at user request\n");
			}
		}

		if (!error) {
			/*
			 * Request hangup on any channels that managed to get attached
			 * while we called the module unload function.
			 */
			__ast_module_user_hangup_all(mod);
			sched_yield();
		}
	}

	if (!error)
		mod->flags.running = mod->flags.declined = 0;

	AST_DLLIST_UNLOCK(&module_list);

	if (!error) {
		unload_dynamic_module(mod);
		ast_test_suite_event_notify("MODULE_UNLOAD", "Message: %s", resource_name);
		ast_update_use_count();
		publish_unload_message(resource_name, "Success");
	}

	return res;
}

static int module_matches_helper_type(struct ast_module *mod, enum ast_module_helper_type type)
{
	switch (type) {
	case AST_MODULE_HELPER_UNLOAD:
		return !mod->usecount && mod->flags.running && !mod->flags.declined;

	case AST_MODULE_HELPER_RELOAD:
		return mod->flags.running && mod->info->reload;

	case AST_MODULE_HELPER_RUNNING:
		return mod->flags.running;

	case AST_MODULE_HELPER_LOADED:
		/* if we have a 'struct ast_module' then we're loaded. */
		return 1;
	default:
		/* This function is not called for AST_MODULE_HELPER_LOAD. */
		/* Unknown ast_module_helper_type. Assume it doesn't match. */
		ast_assert(0);

		return 0;
	}
}

struct module_load_word {
	const char *word;
	size_t len;
	size_t moddir_len;
};

static int module_load_helper_on_file(const char *dir_name, const char *filename, void *obj)
{
	struct module_load_word *word = obj;
	struct ast_module *mod;
	char *filename_merged = NULL;

	/* dir_name will never be shorter than word->moddir_len. */
	dir_name += word->moddir_len;
	if (!ast_strlen_zero(dir_name)) {
		ast_assert(dir_name[0] == '/');

		dir_name += 1;
		if (ast_asprintf(&filename_merged, "%s/%s", dir_name, filename) < 0) {
			/* If we can't allocate the string just give up! */
			return -1;
		}
		filename = filename_merged;
	}

	if (!strncasecmp(filename, word->word, word->len)) {
		/* Don't list files that are already loaded! */
		mod = find_resource(filename, 0);
		if (!mod || !mod->flags.running) {
			ast_cli_completion_add(ast_strdup(filename));
		}
	}

	ast_free(filename_merged);

	return 0;
}

static void module_load_helper(const char *word)
{
	struct module_load_word word_l = {
		.word = word,
		.len = strlen(word),
		.moddir_len = strlen(ast_config_AST_MODULE_DIR),
	};

	AST_DLLIST_LOCK(&module_list);
	ast_file_read_dirs(ast_config_AST_MODULE_DIR, module_load_helper_on_file, &word_l, -1);
	AST_DLLIST_UNLOCK(&module_list);
}

char *ast_module_helper(const char *line, const char *word, int pos, int state, int rpos, enum ast_module_helper_type type)
{
	struct ast_module *mod;
	int which = 0;
	int wordlen = strlen(word);
	char *ret = NULL;

	if (pos != rpos) {
		return NULL;
	}

	if (type == AST_MODULE_HELPER_LOAD) {
		module_load_helper(word);

		return NULL;
	}

	AST_DLLIST_LOCK(&module_list);
	AST_DLLIST_TRAVERSE(&module_list, mod, entry) {
		if (!module_matches_helper_type(mod, type)) {
			continue;
		}

		if (!strncasecmp(word, mod->resource, wordlen) && ++which > state) {
			ret = ast_strdup(mod->resource);
			break;
		}
	}
	AST_DLLIST_UNLOCK(&module_list);

	return ret;
}

void ast_process_pending_reloads(void)
{
	struct reload_queue_item *item;

	modules_loaded = 1;

	AST_LIST_LOCK(&reload_queue);

	if (do_full_reload) {
		do_full_reload = 0;
		AST_LIST_UNLOCK(&reload_queue);
		ast_log(LOG_NOTICE, "Executing deferred reload request.\n");
		ast_module_reload(NULL);
		return;
	}

	while ((item = AST_LIST_REMOVE_HEAD(&reload_queue, entry))) {
		ast_log(LOG_NOTICE, "Executing deferred reload request for module '%s'.\n", item->module);
		ast_module_reload(item->module);
		ast_free(item);
	}

	AST_LIST_UNLOCK(&reload_queue);
}

static void queue_reload_request(const char *module)
{
	struct reload_queue_item *item;

	AST_LIST_LOCK(&reload_queue);

	if (do_full_reload) {
		AST_LIST_UNLOCK(&reload_queue);
		return;
	}

	if (ast_strlen_zero(module)) {
		/* A full reload request (when module is NULL) wipes out any previous
		   reload requests and causes the queue to ignore future ones */
		while ((item = AST_LIST_REMOVE_HEAD(&reload_queue, entry))) {
			ast_free(item);
		}
		do_full_reload = 1;
	} else {
		/* No reason to add the same module twice */
		AST_LIST_TRAVERSE(&reload_queue, item, entry) {
			if (!strcasecmp(item->module, module)) {
				AST_LIST_UNLOCK(&reload_queue);
				return;
			}
		}
		item = ast_calloc(1, sizeof(*item) + strlen(module) + 1);
		if (!item) {
			ast_log(LOG_ERROR, "Failed to allocate reload queue item.\n");
			AST_LIST_UNLOCK(&reload_queue);
			return;
		}
		strcpy(item->module, module);
		AST_LIST_INSERT_TAIL(&reload_queue, item, entry);
	}
	AST_LIST_UNLOCK(&reload_queue);
}

/*!
 * \since 12
 * \internal
 * \brief Publish a \ref stasis message regarding the type.
 */
static void publish_load_message_type(const char* type, const char *name, const char *status)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, event_object, NULL, ast_json_unref);

	ast_assert(type != NULL);
	ast_assert(!ast_strlen_zero(name));
	ast_assert(!ast_strlen_zero(status));

	if (!ast_manager_get_generic_type()) {
		return;
	}

	event_object = ast_json_pack("{s:s, s:s}",
			"Module", name,
			"Status", status);
	json_object = ast_json_pack("{s:s, s:i, s:o}",
			"type", type,
			"class_type", EVENT_FLAG_SYSTEM,
			"event", ast_json_ref(event_object));
	if (!json_object) {
		return;
	}

	payload = ast_json_payload_create(json_object);
	if (!payload) {
		return;
	}

	message = stasis_message_create(ast_manager_get_generic_type(), payload);
	if (!message) {
		return;
	}

	stasis_publish(ast_manager_get_topic(), message);
}

static const char* loadresult2str(enum ast_module_load_result result)
{
	int i;
	for (i = 0; i < ARRAY_LEN(load_results); i++) {
		if (load_results[i].result == result) {
			return load_results[i].name;
		}
	}

	ast_log(LOG_WARNING, "Failed to find correct load result status. result %d\n", result);
	return AST_MODULE_LOAD_UNKNOWN_STRING;
}

/*!
 * \internal
 * \brief Publish a \ref stasis message regarding the load result
 */
static void publish_load_message(const char *name, enum ast_module_load_result result)
{
	const char *status;

	status = loadresult2str(result);

	publish_load_message_type("Load", name, status);
}

/*!
 * \internal
 * \brief Publish a \ref stasis message regarding the unload result
 */
static void publish_unload_message(const char *name, const char* status)
{
	publish_load_message_type("Unload", name, status);
}

/*!
 * \since 12
 * \internal
 * \brief Publish a \ref stasis message regarding the reload result
 */
static void publish_reload_message(const char *name, enum ast_module_reload_result result)
{
	char res_buffer[8];

	snprintf(res_buffer, sizeof(res_buffer), "%u", result);
	publish_load_message_type("Reload", S_OR(name, "All"), res_buffer);
}

enum ast_module_reload_result ast_module_reload(const char *name)
{
	struct ast_module *cur;
	enum ast_module_reload_result res = AST_MODULE_RELOAD_NOT_FOUND;
	size_t name_baselen = name ? resource_name_baselen(name) : 0;

	/* If we aren't fully booted, we just pretend we reloaded but we queue this
	   up to run once we are booted up. */
	if (!modules_loaded) {
		queue_reload_request(name);
		res = AST_MODULE_RELOAD_QUEUED;
		goto module_reload_exit;
	}

	if (ast_mutex_trylock(&reloadlock)) {
		ast_verb(3, "The previous reload command didn't finish yet\n");
		res = AST_MODULE_RELOAD_IN_PROGRESS;
		goto module_reload_exit;
	}
	ast_sd_notify("RELOAD=1");
	ast_lastreloadtime = ast_tvnow();

	if (ast_opt_lock_confdir) {
		int try;
		int lockres;
		for (try = 1, lockres = AST_LOCK_TIMEOUT; try < 6 && (lockres == AST_LOCK_TIMEOUT); try++) {
			lockres = ast_lock_path(ast_config_AST_CONFIG_DIR);
			if (lockres == AST_LOCK_TIMEOUT) {
				ast_log(LOG_WARNING, "Failed to grab lock on %s, try %d\n", ast_config_AST_CONFIG_DIR, try);
			}
		}
		if (lockres != AST_LOCK_SUCCESS) {
			ast_log(AST_LOG_WARNING, "Cannot grab lock on %s\n", ast_config_AST_CONFIG_DIR);
			res = AST_MODULE_RELOAD_ERROR;
			goto module_reload_done;
		}
	}

	AST_DLLIST_LOCK(&module_list);
	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		const struct ast_module_info *info = cur->info;

		if (name && resource_name_match(name, name_baselen, cur->resource)) {
			continue;
		}

		if (!cur->flags.running || cur->flags.declined) {
			if (res == AST_MODULE_RELOAD_NOT_FOUND) {
				res = AST_MODULE_RELOAD_UNINITIALIZED;
			}
			if (!name) {
				continue;
			}
			break;
		}

		if (!info->reload) {	/* cannot be reloaded */
			if (res == AST_MODULE_RELOAD_NOT_FOUND) {
				res = AST_MODULE_RELOAD_NOT_IMPLEMENTED;
			}
			if (!name) {
				continue;
			}
			break;
		}
		ast_verb(3, "Reloading module '%s' (%s)\n", cur->resource, info->description);
		if (info->reload() == AST_MODULE_LOAD_SUCCESS) {
			res = AST_MODULE_RELOAD_SUCCESS;
		} else if (res == AST_MODULE_RELOAD_NOT_FOUND) {
			res = AST_MODULE_RELOAD_ERROR;
		}
		if (name) {
			break;
		}
	}
	AST_DLLIST_UNLOCK(&module_list);

	if (ast_opt_lock_confdir) {
		ast_unlock_path(ast_config_AST_CONFIG_DIR);
	}
module_reload_done:
	ast_mutex_unlock(&reloadlock);
	ast_sd_notify("READY=1");

module_reload_exit:
	publish_reload_message(name, res);
	return res;
}

static unsigned int inspect_module(const struct ast_module *mod)
{
	if (!mod->info->description) {
		module_load_error("Module '%s' does not provide a description.\n", mod->resource);
		return 1;
	}

	if (!mod->info->key) {
		module_load_error("Module '%s' does not provide a license key.\n", mod->resource);
		return 1;
	}

	if (verify_key((unsigned char *) mod->info->key)) {
		module_load_error("Module '%s' did not provide a valid license key.\n", mod->resource);
		return 1;
	}

	if (!ast_strlen_zero(mod->info->buildopt_sum) &&
	    strcmp(buildopt_sum, mod->info->buildopt_sum)) {
		module_load_error("Module '%s' was not compiled with the same compile-time options as this version of Asterisk.\n", mod->resource);
		module_load_error("Module '%s' will not be initialized as it may cause instability.\n", mod->resource);
		return 1;
	}

	return 0;
}

static enum ast_module_load_result start_resource(struct ast_module *mod)
{
	char tmp[256];
	enum ast_module_load_result res;

	if (mod->flags.running) {
		return AST_MODULE_LOAD_SUCCESS;
	}

	if (!mod->info->load) {
		mod->flags.declined = 1;

		return mod->flags.required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
	}

	if (module_deps_reference(mod, NULL)) {
		struct module_vector missing;
		int i;

		AST_VECTOR_INIT(&missing, 0);
		if (module_deps_missing_recursive(mod, &missing)) {
			module_load_error("%s has one or more unknown dependencies.\n", mod->info->name);
		}
		for (i = 0; i < AST_VECTOR_SIZE(&missing); i++) {
			module_load_error("%s loaded before dependency %s!\n", mod->info->name,
				AST_VECTOR_GET(&missing, i)->info->name);
		}
		AST_VECTOR_FREE(&missing);

		return AST_MODULE_LOAD_DECLINE;
	}

	if (!ast_fully_booted) {
		ast_verb(1, "Loading %s.\n", mod->resource);
	}
	res = mod->info->load();

	switch (res) {
	case AST_MODULE_LOAD_SUCCESS:
		if (!ast_fully_booted) {
			ast_verb(2, "%s => (%s)\n", mod->resource, term_color(tmp, mod->info->description, COLOR_BROWN, COLOR_BLACK, sizeof(tmp)));
		} else {
			ast_verb(1, "Loaded %s => (%s)\n", mod->resource, mod->info->description);
		}

		mod->flags.running = 1;
		if (mod->flags.builtin) {
			/* Built-in modules cannot be unloaded. */
			ast_module_shutdown_ref(mod);
		}

		ast_update_use_count();
		break;
	case AST_MODULE_LOAD_DECLINE:
		mod->flags.declined = 1;
		if (mod->flags.required) {
			res = AST_MODULE_LOAD_FAILURE;
		}
		break;
	case AST_MODULE_LOAD_FAILURE:
		mod->flags.declined = 1;
		break;
	case AST_MODULE_LOAD_SKIP: /* modules should never return this value */
	case AST_MODULE_LOAD_PRIORITY:
		break;
	}

	/* Make sure the newly started module is at the end of the list */
	AST_DLLIST_LOCK(&module_list);
	AST_DLLIST_REMOVE(&module_list, mod, entry);
	AST_DLLIST_INSERT_TAIL(&module_list, mod, entry);
	AST_DLLIST_UNLOCK(&module_list);

	return res;
}

/*! loads a resource based upon resource_name. If global_symbols_only is set
 *  only modules with global symbols will be loaded.
 *
 *  If the module_vector is provided (not NULL) the module is found and added to the
 *  vector without running the module's load() function.  By doing this, modules
 *  can be initialized later in order by priority and dependencies.
 *
 *  If the module_vector is not provided, the module's load function will be executed
 *  immediately */
static enum ast_module_load_result load_resource(const char *resource_name, unsigned int suppress_logging,
	struct module_vector *module_priorities, int required, int preload)
{
	struct ast_module *mod;
	enum ast_module_load_result res = AST_MODULE_LOAD_SUCCESS;

	if ((mod = find_resource(resource_name, 0))) {
		if (mod->flags.running) {
			ast_log(LOG_WARNING, "Module '%s' already loaded and running.\n", resource_name);
			return AST_MODULE_LOAD_DECLINE;
		}
	} else {
		mod = load_dynamic_module(resource_name, suppress_logging);
		if (!mod) {
			return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
		}

		if (module_post_register(mod)) {
			goto prestart_error;
		}
	}

	mod->flags.required |= required;
	mod->flags.preload |= preload;

	if (inspect_module(mod)) {
		goto prestart_error;
	}

	mod->flags.declined = 0;

	if (module_priorities) {
		if (AST_VECTOR_ADD_SORTED(module_priorities, mod, module_vector_cmp)) {
			goto prestart_error;
		}
		res = AST_MODULE_LOAD_PRIORITY;
	} else {
		res = start_resource(mod);
	}

	if (ast_fully_booted && !ast_shutdown_final()) {
		publish_load_message(resource_name, res);
	}

	return res;

prestart_error:
	module_load_error("Module '%s' could not be loaded.\n", resource_name);
	unload_dynamic_module(mod);
	res = required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
	if (ast_fully_booted && !ast_shutdown_final()) {
		publish_load_message(resource_name, res);
	}
	return res;
}

int ast_load_resource(const char *resource_name)
{
	int res;
	AST_DLLIST_LOCK(&module_list);
	res = load_resource(resource_name, 0, NULL, 0, 0);
	if (!res) {
		ast_test_suite_event_notify("MODULE_LOAD", "Message: %s", resource_name);
	}
	AST_DLLIST_UNLOCK(&module_list);

	return res;
}

struct load_order_entry {
	char *resource;
	int required;
	int preload;
	int builtin;
	AST_LIST_ENTRY(load_order_entry) entry;
};

AST_LIST_HEAD_NOLOCK(load_order, load_order_entry);

static struct load_order_entry *add_to_load_order(const char *resource, struct load_order *load_order, int required, int preload, int builtin)
{
	struct load_order_entry *order;
	size_t resource_baselen = resource_name_baselen(resource);

	AST_LIST_TRAVERSE(load_order, order, entry) {
		if (!resource_name_match(resource, resource_baselen, order->resource)) {
			/* Make sure we have the proper setting for the required field
			   (we might have both load= and required= lines in modules.conf) */
			order->required |= required;
			order->preload |= preload;
			return order;
		}
	}

	order = ast_calloc(1, sizeof(*order));
	if (!order) {
		return NULL;
	}

	order->resource = ast_strdup(resource);
	if (!order->resource) {
		ast_free(order);

		return NULL;
	}
	order->required = required;
	order->preload = preload;
	order->builtin = builtin;
	AST_LIST_INSERT_TAIL(load_order, order, entry);

	return order;
}

AST_LIST_HEAD_NOLOCK(load_retries, load_order_entry);

static enum ast_module_load_result start_resource_attempt(struct ast_module *mod, int *count)
{
	enum ast_module_load_result lres;

	/* Try to grab required references. */
	if (module_deps_reference(mod, NULL)) {
		/* We're likely to retry so not an error. */
		ast_debug(1, "Module %s is missing dependencies\n", mod->resource);
		return AST_MODULE_LOAD_SKIP;
	}

	lres = start_resource(mod);
	ast_debug(3, "START: %-46s[%d] %d\n",
		mod->resource,
		ast_test_flag(mod->info, AST_MODFLAG_LOAD_ORDER) ? mod->info->load_pri : AST_MODPRI_DEFAULT,
		lres);

	if (lres == AST_MODULE_LOAD_SUCCESS) {
		(*count)++;
	} else if (lres == AST_MODULE_LOAD_FAILURE) {
		module_load_error("*** Failed to load %smodule %s\n",
			mod->flags.required ? "required " : "",
			mod->resource);
	}

	return lres;
}

static int resource_list_recursive_decline(struct module_vector *resources, struct ast_module *mod,
	struct ast_str **printmissing)
{
	struct module_vector missingdeps;
	struct ast_vector_const_string localdeps;
	int i = 0;
	int res = -1;

	mod->flags.declined = 1;
	if (mod->flags.required) {
		module_load_error("Required module %s declined to load.\n", ast_module_name(mod));

		return -2;
	}

	module_load_error("%s declined to load.\n", ast_module_name(mod));

	if (!*printmissing) {
		*printmissing = ast_str_create(64);
		if (!*printmissing) {
			return -1;
		}
	} else {
		ast_str_reset(*printmissing);
	}

	AST_VECTOR_INIT(&missingdeps, 0);
	AST_VECTOR_INIT(&localdeps, 0);

	/* Decline everything that depends on 'mod' from resources so we can
	 * print a concise list. */
	while (res != -2 && i < AST_VECTOR_SIZE(resources)) {
		struct ast_module *dep = AST_VECTOR_GET(resources, i);
		i++;

		AST_VECTOR_RESET(&missingdeps, AST_VECTOR_ELEM_CLEANUP_NOOP);
		if (dep->flags.declined || module_deps_missing_recursive(dep, &missingdeps)) {
			continue;
		}

		if (AST_VECTOR_GET_CMP(&missingdeps, mod, AST_VECTOR_ELEM_DEFAULT_CMP)) {
			dep->flags.declined = 1;
			if (dep->flags.required) {
				module_load_error("Cannot load required module %s that depends on %s\n",
					ast_module_name(dep), ast_module_name(mod));
				res = -2;
			} else {
				AST_VECTOR_APPEND(&localdeps, ast_module_name(dep));
			}
		}
	}
	AST_VECTOR_FREE(&missingdeps);

	if (res != -2 && AST_VECTOR_SIZE(&localdeps)) {
		AST_VECTOR_CALLBACK_VOID(&localdeps, STR_APPEND_TEXT, printmissing);
		module_load_error("Declined modules which depend on %s: %s\n",
			ast_module_name(mod), ast_str_buffer(*printmissing));
	}
	AST_VECTOR_FREE(&localdeps);

	return res;
}

static int start_resource_list(struct module_vector *resources, int *mod_count)
{
	struct module_vector missingdeps;
	int res = 0;
	struct ast_str *printmissing = NULL;

	AST_VECTOR_INIT(&missingdeps, 0);
	while (res != -2 && AST_VECTOR_SIZE(resources)) {
		struct ast_module *mod = AST_VECTOR_REMOVE(resources, 0, 1);
		enum ast_module_load_result lres;

		if (mod->flags.declined) {
			ast_debug(1, "%s is already declined, skipping\n", ast_module_name(mod));
			continue;
		}

retry_load:
		lres = start_resource_attempt(mod, mod_count);
		if (lres == AST_MODULE_LOAD_SUCCESS) {
			/* No missing dependencies, successful. */
			continue;
		}

		if (lres == AST_MODULE_LOAD_FAILURE) {
			res = -2;
			break;
		}

		if (lres == AST_MODULE_LOAD_DECLINE) {
			res = resource_list_recursive_decline(resources, mod, &printmissing);
			continue;
		}

		if (module_deps_missing_recursive(mod, &missingdeps)) {
			AST_VECTOR_RESET(&missingdeps, AST_VECTOR_ELEM_CLEANUP_NOOP);
			module_load_error("Failed to resolve dependencies for %s\n", ast_module_name(mod));
			res = resource_list_recursive_decline(resources, mod, &printmissing);
			continue;
		}

		if (!AST_VECTOR_SIZE(&missingdeps)) {
			module_load_error("%s load function returned an invalid result. "
				"This is a bug in the module.\n", ast_module_name(mod));
			/* Dependencies were met but the module failed to start and the result
			 * code was not AST_MODULE_LOAD_FAILURE or AST_MODULE_LOAD_DECLINE. */
			res = resource_list_recursive_decline(resources, mod, &printmissing);
			continue;
		}

		ast_debug(1, "%s has %d dependencies\n",
			ast_module_name(mod), (int)AST_VECTOR_SIZE(&missingdeps));
		while (AST_VECTOR_SIZE(&missingdeps)) {
			int didwork = 0;
			int i = 0;

			while (i < AST_VECTOR_SIZE(&missingdeps)) {
				struct ast_module *dep = AST_VECTOR_GET(&missingdeps, i);

				if (dep->flags.declined) {
					ast_debug(1, "%s tried to start %s but it's already declined\n",
						ast_module_name(mod), ast_module_name(dep));
					i++;
					continue;
				}

				ast_debug(1, "%s trying to start %s\n", ast_module_name(mod), ast_module_name(dep));
				lres = start_resource_attempt(dep, mod_count);
				if (lres == AST_MODULE_LOAD_SUCCESS) {
					ast_debug(1, "%s started %s\n", ast_module_name(mod), ast_module_name(dep));
					AST_VECTOR_REMOVE(&missingdeps, i, 1);
					AST_VECTOR_REMOVE_CMP_ORDERED(resources, dep,
						AST_VECTOR_ELEM_DEFAULT_CMP, AST_VECTOR_ELEM_CLEANUP_NOOP);
					didwork++;
					continue;
				}

				if (lres == AST_MODULE_LOAD_FAILURE) {
					module_load_error("Failed to load %s.\n", ast_module_name(dep));
					res = -2;
					goto exitpoint;
				}

				ast_debug(1, "%s failed to start %s\n", ast_module_name(mod), ast_module_name(dep));
				i++;
			}

			if (!didwork) {
				break;
			}
		}

		if (AST_VECTOR_SIZE(&missingdeps)) {
			if (!printmissing) {
				printmissing = ast_str_create(64);
			} else {
				ast_str_reset(printmissing);
			}

			if (printmissing) {
				struct ast_vector_const_string localdeps;

				AST_VECTOR_INIT(&localdeps, 0);
				module_deps_reference(mod, &localdeps);
				AST_VECTOR_CALLBACK_VOID(&localdeps, STR_APPEND_TEXT, &printmissing);
				AST_VECTOR_FREE(&localdeps);
			}

			module_load_error("Failed to load %s due to dependencies: %s.\n",
				ast_module_name(mod),
				printmissing ? ast_str_buffer(printmissing) : "allocation failure creating list");
			res = resource_list_recursive_decline(resources, mod, &printmissing);

			AST_VECTOR_RESET(&missingdeps, AST_VECTOR_ELEM_CLEANUP_NOOP);

			continue;
		}

		/* If we're here it means that we started with missingdeps and they're all loaded
		 * now.  It's impossible to reach this point a second time for the same module. */
		goto retry_load;
	}

exitpoint:
	ast_free(printmissing);
	AST_VECTOR_FREE(&missingdeps);

	return res;
}

/*! loads modules in order by load_pri, updates mod_count
	\return -1 on failure to load module, -2 on failure to load required module, otherwise 0
*/
static int load_resource_list(struct load_order *load_order, int *mod_count)
{
	struct module_vector module_priorities;
	struct load_order_entry *order;
	int attempt = 0;
	int count = 0;
	int res = 0;
	int didwork;
	int lasttry = 0;

	if (AST_VECTOR_INIT(&module_priorities, 500)) {
		ast_log(LOG_ERROR, "Failed to initialize module loader.\n");

		return -1;
	}

	while (res != -2) {
		didwork = 0;

		AST_LIST_TRAVERSE_SAFE_BEGIN(load_order, order, entry) {
			enum ast_module_load_result lres;

			/* Suppress log messages unless this is the last pass */
			lres = load_resource(order->resource, !lasttry, &module_priorities, order->required, order->preload);
			ast_debug(3, "PASS %d: %-46s %d\n", attempt, order->resource, lres);
			switch (lres) {
			case AST_MODULE_LOAD_SUCCESS:
			case AST_MODULE_LOAD_SKIP:
				/* We're supplying module_priorities so SUCCESS isn't possible but we
				 * still have to test for it.  SKIP is only used when we try to start a
				 * module that is missing dependencies. */
				break;
			case AST_MODULE_LOAD_DECLINE:
				res = -1;
				break;
			case AST_MODULE_LOAD_FAILURE:
				/* LOAD_FAILURE only happens for required modules */
				if (lasttry) {
					/* This run is just to print errors. */
					module_load_error("*** Failed to load module %s - Required\n", order->resource);
					fprintf(stderr, "*** Failed to load module %s - Required\n", order->resource);
					res =  -2;
				}
				break;
			case AST_MODULE_LOAD_PRIORITY:
				/* load_resource worked and the module was added to module_priorities */
				AST_LIST_REMOVE_CURRENT(entry);
				ast_free(order->resource);
				ast_free(order);
				didwork = 1;
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (!didwork) {
			if (lasttry) {
				break;
			}
			/* We know the next try is going to fail, it's only being performed
			 * so we can print errors. */
			lasttry = 1;
		}
		attempt++;
	}

	if (res != -2) {
		res = start_resource_list(&module_priorities, &count);
	}

	if (mod_count) {
		*mod_count += count;
	}
	AST_VECTOR_FREE(&module_priorities);

	return res;
}

static int loader_builtin_init(struct load_order *load_order)
{
	struct ast_module *mod;

	/*
	 * All built-in modules have registered the first time, now it's time to complete
	 * the registration and add them to the priority list.
	 */
	loader_ready = 1;

	while ((resource_being_loaded = AST_DLLIST_REMOVE_HEAD(&builtin_module_list, entry))) {
		/* ast_module_register doesn't finish when first run by built-in modules. */
		ast_module_register(resource_being_loaded->info);
	}

	/* Add all built-in modules to the load order. */
	AST_DLLIST_TRAVERSE(&module_list, mod, entry) {
		if (!mod->flags.builtin) {
			continue;
		}

		/* Parse dependendencies from mod->info. */
		if (module_post_register(mod)) {
			return -1;
		}

		/* Built-in modules are not preloaded, most have an early load priority. */
		if (!add_to_load_order(mod->resource, load_order, 0, 0, 1)) {
			return -1;
		}
	}

	return 0;
}

static int loader_config_init(struct load_order *load_order)
{
	int res = -1;
	struct load_order_entry *order;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { 0 };

	cfg = ast_config_load2(AST_MODULE_CONFIG, "" /* core, can't reload */, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "'%s' invalid or missing.\n", AST_MODULE_CONFIG);

		return -1;
	}

	/* first, find all the modules we have been explicitly requested to load */
	for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
		int required;
		int preload = 0;

		if (!strncasecmp(v->name, "preload", strlen("preload"))) {
			preload = 1;
			if (!strcasecmp(v->name, "preload")) {
				required = 0;
			} else if (!strcasecmp(v->name, "preload-require")) {
				required = 1;
			} else {
				ast_log(LOG_ERROR, "Unknown configuration option '%s'", v->name);
				goto done;
			}
		} else if (!strcasecmp(v->name, "load")) {
			required = 0;
		} else if (!strcasecmp(v->name, "require")) {
			required = 1;
		} else if (!strcasecmp(v->name, "noload") || !strcasecmp(v->name, "autoload")) {
			continue;
		} else {
			ast_log(LOG_ERROR, "Unknown configuration option '%s'", v->name);
			goto done;
		}

		if (required) {
			ast_debug(2, "Adding module to required list: %s (%s)\n", v->value, v->name);
		}

		if (!add_to_load_order(v->value, load_order, required, preload, 0)) {
			goto done;
		}
	}

	/* check if 'autoload' is on */
	if (ast_true(ast_variable_retrieve(cfg, "modules", "autoload"))) {
		/* if we are allowed to load dynamic modules, scan the directory for
		   for all available modules and add them as well */
		DIR *dir = opendir(ast_config_AST_MODULE_DIR);
		struct dirent *dirent;

		if (dir) {
			while ((dirent = readdir(dir))) {
				int ld = strlen(dirent->d_name);

				/* Must end in .so to load it.  */

				if (ld < 4)
					continue;

				if (strcasecmp(dirent->d_name + ld - 3, ".so"))
					continue;

				/* if there is already a module by this name in the module_list,
				   skip this file */
				if (find_resource(dirent->d_name, 0))
					continue;

				if (!add_to_load_order(dirent->d_name, load_order, 0, 0, 0)) {
					closedir(dir);
					goto done;
				}
			}

			closedir(dir);
		} else {
			ast_log(LOG_ERROR, "Unable to open modules directory '%s'.\n", ast_config_AST_MODULE_DIR);
			goto done;
		}
	}

	/* now scan the config for any modules we are prohibited from loading and
	   remove them from the load order */
	for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
		size_t baselen;

		if (strcasecmp(v->name, "noload")) {
			continue;
		}

		baselen = resource_name_baselen(v->value);
		AST_LIST_TRAVERSE_SAFE_BEGIN(load_order, order, entry) {
			if (!resource_name_match(v->value, baselen, order->resource)) {
				if (order->builtin) {
					ast_log(LOG_ERROR, "%s is a built-in module, you cannot specify 'noload'.\n", v->value);
					goto done;
				}

				if (order->required) {
					ast_log(LOG_ERROR, "%s is configured with '%s' and 'noload', this is impossible.\n",
						v->value, order->preload ? "preload-require" : "require");
					goto done;
				}
				AST_LIST_REMOVE_CURRENT(entry);
				ast_free(order->resource);
				ast_free(order);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	res = 0;
done:
	ast_config_destroy(cfg);

	return res;
}

int load_modules(void)
{
	struct load_order_entry *order;
	unsigned int load_count;
	struct load_order load_order;
	int res = 0;
	int modulecount = 0;
	int i;

	ast_verb(1, "Asterisk Dynamic Loader Starting:\n");

#if defined(HAVE_PERMANENT_DLOPEN)
	info_list = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL,
		info_list_obj_cmp_fn); /* must not be cleaned at shutdown */
	if (!info_list) {
		fprintf(stderr, "Module info list allocation failure.\n");
		return 1;
	}
#endif

	AST_LIST_HEAD_INIT_NOLOCK(&load_order);
	AST_DLLIST_LOCK(&module_list);

	AST_VECTOR_INIT(&startup_errors, 0);
	startup_error_builder = ast_str_create(64);

	res = loader_builtin_init(&load_order);
	if (res) {
		goto done;
	}

	res = loader_config_init(&load_order);
	if (res) {
		goto done;
	}

	load_count = 0;
	AST_LIST_TRAVERSE(&load_order, order, entry)
		load_count++;

	if (load_count)
		ast_log(LOG_NOTICE, "%u modules will be loaded.\n", load_count);

	res = load_resource_list(&load_order, &modulecount);
	if (res == -1) {
		ast_log(LOG_WARNING, "Some non-required modules failed to load.\n");
		res = 0;
	}

done:
	while ((order = AST_LIST_REMOVE_HEAD(&load_order, entry))) {
		ast_free(order->resource);
		ast_free(order);
	}

	AST_DLLIST_UNLOCK(&module_list);

	for (i = 0; i < AST_VECTOR_SIZE(&startup_errors); i++) {
		char *str = AST_VECTOR_GET(&startup_errors, i);

		ast_log(LOG_ERROR, "%s", str);
		ast_free(str);
	}
	AST_VECTOR_FREE(&startup_errors);

	ast_free(startup_error_builder);
	startup_error_builder = NULL;

	return res;
}

void ast_update_use_count(void)
{
	/* Notify any module monitors that the use count for a
	   resource has changed */
	struct loadupdate *m;

	AST_LIST_LOCK(&updaters);
	AST_LIST_TRAVERSE(&updaters, m, entry)
		m->updater();
	AST_LIST_UNLOCK(&updaters);
}

/*!
 * \internal
 * \brief Build an alpha sorted list of modules.
 *
 * \param alpha_module_list Pointer to uninitialized module_vector.
 *
 * This function always initializes alpha_module_list.
 *
 * \pre module_list must be locked.
 */
static int alpha_module_list_create(struct module_vector *alpha_module_list)
{
	struct ast_module *cur;

	if (AST_VECTOR_INIT(alpha_module_list, 32)) {
		return -1;
	}

	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		if (AST_VECTOR_ADD_SORTED(alpha_module_list, cur, module_vector_strcasecmp)) {
			return -1;
		}
	}

	return 0;
}

int ast_update_module_list(int (*modentry)(const char *module, const char *description,
                                           int usecnt, const char *status, const char *like,
										   enum ast_module_support_level support_level),
                           const char *like)
{
	int total_mod_loaded = 0;
	struct module_vector alpha_module_list;

	AST_DLLIST_LOCK(&module_list);

	if (!alpha_module_list_create(&alpha_module_list)) {
		int idx;

		for (idx = 0; idx < AST_VECTOR_SIZE(&alpha_module_list); idx++) {
			struct ast_module *cur = AST_VECTOR_GET(&alpha_module_list, idx);

			total_mod_loaded += modentry(cur->resource, cur->info->description, cur->usecount,
				cur->flags.running ? "Running" : "Not Running", like, cur->info->support_level);
		}
	}

	AST_DLLIST_UNLOCK(&module_list);
	AST_VECTOR_FREE(&alpha_module_list);

	return total_mod_loaded;
}

int ast_update_module_list_data(int (*modentry)(const char *module, const char *description,
                                                int usecnt, const char *status, const char *like,
                                                enum ast_module_support_level support_level,
                                                void *data),
                                const char *like, void *data)
{
	int total_mod_loaded = 0;
	struct module_vector alpha_module_list;

	AST_DLLIST_LOCK(&module_list);

	if (!alpha_module_list_create(&alpha_module_list)) {
		int idx;

		for (idx = 0; idx < AST_VECTOR_SIZE(&alpha_module_list); idx++) {
			struct ast_module *cur = AST_VECTOR_GET(&alpha_module_list, idx);

			total_mod_loaded += modentry(cur->resource, cur->info->description, cur->usecount,
				cur->flags.running? "Running" : "Not Running", like, cur->info->support_level, data);
		}
	}

	AST_DLLIST_UNLOCK(&module_list);
	AST_VECTOR_FREE(&alpha_module_list);

	return total_mod_loaded;
}

int ast_update_module_list_condition(int (*modentry)(const char *module, const char *description,
                                                     int usecnt, const char *status,
                                                     const char *like,
                                                     enum ast_module_support_level support_level,
                                                     void *data, const char *condition),
                                     const char *like, void *data, const char *condition)
{
	int conditions_met = 0;
	struct module_vector alpha_module_list;

	AST_DLLIST_LOCK(&module_list);

	if (!alpha_module_list_create(&alpha_module_list)) {
		int idx;

		for (idx = 0; idx < AST_VECTOR_SIZE(&alpha_module_list); idx++) {
			struct ast_module *cur = AST_VECTOR_GET(&alpha_module_list, idx);

			conditions_met += modentry(cur->resource, cur->info->description, cur->usecount,
				cur->flags.running? "Running" : "Not Running", like, cur->info->support_level, data,
				condition);
		}
	}

	AST_DLLIST_UNLOCK(&module_list);
	AST_VECTOR_FREE(&alpha_module_list);

	return conditions_met;
}

/*! \brief Check if module exists */
int ast_module_check(const char *name)
{
	struct ast_module *cur;

	if (ast_strlen_zero(name))
		return 0;       /* FALSE */

	cur = find_resource(name, 1);

	return (cur != NULL);
}


int ast_loader_register(int (*v)(void))
{
	struct loadupdate *tmp;

	if (!(tmp = ast_malloc(sizeof(*tmp))))
		return -1;

	tmp->updater = v;
	AST_LIST_LOCK(&updaters);
	AST_LIST_INSERT_HEAD(&updaters, tmp, entry);
	AST_LIST_UNLOCK(&updaters);

	return 0;
}

int ast_loader_unregister(int (*v)(void))
{
	struct loadupdate *cur;

	AST_LIST_LOCK(&updaters);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&updaters, cur, entry) {
		if (cur->updater == v)	{
			AST_LIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&updaters);

	return cur ? 0 : -1;
}

struct ast_module *__ast_module_ref(struct ast_module *mod, const char *file, int line, const char *func)
{
	if (!mod) {
		return NULL;
	}

	if (mod->ref_debug) {
		__ao2_ref(mod->ref_debug, +1, "", file, line, func);
	}

	ast_atomic_fetchadd_int(&mod->usecount, +1);
	ast_update_use_count();

	return mod;
}

struct ast_module *__ast_module_running_ref(struct ast_module *mod,
	const char *file, int line, const char *func)
{
	if (!mod || !mod->flags.running) {
		return NULL;
	}

	return __ast_module_ref(mod, file, line, func);
}

void __ast_module_shutdown_ref(struct ast_module *mod, const char *file, int line, const char *func)
{
	if (!mod || mod->flags.keepuntilshutdown) {
		return;
	}

	__ast_module_ref(mod, file, line, func);
	mod->flags.keepuntilshutdown = 1;
}

void __ast_module_unref(struct ast_module *mod, const char *file, int line, const char *func)
{
	if (!mod) {
		return;
	}

	if (mod->ref_debug) {
		__ao2_ref(mod->ref_debug, -1, "", file, line, func);
	}

	ast_atomic_fetchadd_int(&mod->usecount, -1);
	ast_update_use_count();
}

const char *support_level_map [] = {
	[AST_MODULE_SUPPORT_UNKNOWN] = "unknown",
	[AST_MODULE_SUPPORT_CORE] = "core",
	[AST_MODULE_SUPPORT_EXTENDED] = "extended",
	[AST_MODULE_SUPPORT_DEPRECATED] = "deprecated",
};

const char *ast_module_support_level_to_string(enum ast_module_support_level support_level)
{
	return support_level_map[support_level];
}
