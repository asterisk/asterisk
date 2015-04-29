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
 * \brief Module Loader
 * \author Mark Spencer <markster@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Luigi Rizzo <rizzo@icir.org>
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
#include "asterisk/linkedlists.h"
#include "asterisk/md5.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/strings.h"
#include "asterisk/term.h"

#include "module_private.h"

#include <dlfcn.h>

static enum {
	LOADER_RUNLEVEL_EMBEDDING,
	LOADER_RUNLEVEL_LOADING,
	LOADER_RUNLEVEL_NORMAL,
} loader_runlevel = LOADER_RUNLEVEL_EMBEDDING;

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif

static const unsigned char expected_key[] =
{ 0x87, 0x76, 0x79, 0x35, 0x23, 0xea, 0x3a, 0xd3,
  0x25, 0x2a, 0xbb, 0x35, 0x87, 0xe4, 0x22, 0x24 };

/*
 * We always start out by registering embedded modules,
 * since they are here before we dlopen() any
 */
static AST_VECTOR(, struct ast_module *) embedded_module_list;



/* BEGIN: ast_module private functions */
static int module_key_verify(const char *name, const char *key)
{
	struct MD5Context c;
	unsigned char digest[16];
	int x;
	int ret = 0;

	MD5Init(&c);
	MD5Update(&c, (const unsigned char *)key, strlen(key));
	MD5Final(digest, &c);

	for (x = 0; x < 16; x++) {
		if (expected_key[x] != digest[x]) {
			ret = -1;
			break;
		}
	}

	if (ret) {
		int x, pos;
		unsigned char *d = digest;
		char buf[256]; /* large enough so we don't have to worry */

		for (pos = 0, x = 0; x < 16; x++) {
			pos += sprintf(buf + pos, " %02hhx", *d++);
		}

		ast_debug(1, "Module '%s' unexpected signature: %s\n", name, buf);
		return ret;
	}

	return ret;
}

static void module_dlclose(struct ast_module_lib *lib)
{
#ifdef LOADABLE_MODULES
	if (!lib->lib) {
		return;
	}

	/* Clear any existing error */
	dlerror();
	if (dlclose(lib->lib)) {
		char *error = dlerror();
		ast_log(AST_LOG_ERROR, "Failure in dlclose for module '%s': %s\n",
			lib->module->name, S_OR(error, "Unknown error"));
	}
	lib->lib = NULL;
#endif
}

static int module_dlopen(struct ast_module *module, struct ast_module_lib *lib)
{
#ifdef LOADABLE_MODULES
	char fn[PATH_MAX] = "";

	snprintf(fn, sizeof(fn), "%s/%s.so", ast_config_AST_MODULE_DIR, module->name);
	lib->lib = dlopen(fn, module->export_globals ? RTLD_NOW | RTLD_GLOBAL : RTLD_NOW | RTLD_LOCAL);

	if (!lib->lib) {
		ast_log(LOG_ERROR, "Error loading module '%s': %s\n", module->name, dlerror());
		return -1;
	}

	return module->self ? 0 : -1;
#else
	if (module->self) {
		return 0;
	}

	ast_log(LOG_ERROR, "Module not found.\n");
	return -1;
#endif
}

struct reload_queue_item {
	struct ast_module *module;
	AST_LIST_ENTRY(reload_queue_item) entry;
};

static int do_full_reload = 0;
static AST_LIST_HEAD_STATIC(reload_queue, reload_queue_item);

static enum ast_module_reload_result module_reload(struct ast_module *module)
{
	enum ast_module_reload_result ret = AST_MODULE_RELOAD_NOT_FOUND;
	struct ast_module_libs_rw *copy;
	struct ast_module_lib *lib;
	struct ast_module_instance *instance;
	int i;
	int success = 0;

	/* reload_queue is locked and loader_runlevel == LOADER_RUNLEVEL_NORMAL */
	if (!module) {
		AST_VECTOR_RW_RDLOCK(&modules_running);
		copy = AST_VECTOR_DUP_AO2(&modules_running);
		AST_VECTOR_RW_UNLOCK(&modules_running);
	} else {
		lib = ast_module_get_lib_running(module);
		if (!lib) {
			return AST_MODULE_RELOAD_NOT_FOUND;
		}

		copy = ast_malloc(sizeof(*copy));
		AST_VECTOR_INIT(copy, 1);
		AST_VECTOR_APPEND(copy, lib);
	}

	for (i = 0; i < AST_VECTOR_SIZE(copy); i++) {
		lib = AST_VECTOR_GET(copy, i);
		if (!lib->reload_fn) {
			ret = AST_MODULE_RELOAD_NOT_IMPLEMENTED;
			continue;
		}

		instance = ast_module_lib_get_instance(lib);
		if (!instance) {
			ret = AST_MODULE_RELOAD_NOT_FOUND;
			continue;
		}

		ast_verb(3, "Reloading module '%s' (%s)\n", module->name, module->description);
		ret = module->lib->reload_fn();
		if (ret == AST_MODULE_RELOAD_SUCCESS) {
			success++;
		}

		ao2_ref(instance, -1);
	}

	if (AST_VECTOR_SIZE(copy) > 1) {
		ret = success ? AST_MODULE_RELOAD_SUCCESS : AST_MODULE_RELOAD_ERROR;
	}

	AST_VECTOR_CALLBACK_VOID(copy, ao2_t_ref, -1, "clear copy");
	AST_VECTOR_PTR_FREE(copy);

	return ret;
}

static enum ast_module_reload_result module_reload_queue_request(struct ast_module *module)
{
	/* reload_queue is locked and ast_fully_booted is false */
	struct reload_queue_item *item;

	if (do_full_reload) {
		return AST_MODULE_RELOAD_QUEUED;
	}

	if (!module) {
		do_full_reload = 1;
		while ((item = AST_LIST_REMOVE_HEAD(&reload_queue, entry))) {
			ao2_cleanup(item->module);
			ast_free(item);
		}
		return AST_MODULE_RELOAD_QUEUED;
	}

	/* No reason to add the same module twice */
	AST_LIST_TRAVERSE(&reload_queue, item, entry) {
		if (item->module == module) {
			return AST_MODULE_RELOAD_QUEUED;
		}
	}

	item = ast_calloc(1, sizeof(*item));
	if (!item) {
		ast_log(LOG_ERROR, "Failed to allocate reload queue item.\n");
		return AST_MODULE_RELOAD_ERROR;
	}
	item->module = module;
	ao2_ref(module, +1);
	AST_LIST_INSERT_TAIL(&reload_queue, item, entry);

	return AST_MODULE_RELOAD_QUEUED;
}
/* END: ast_module private functions */



int __ast_module_register(struct ast_module **self, const char *name,
	const char *buildopt_sum, const char *manifest_checksum,
	const char *keystr, const char *desc,
	ast_module_init_fn init_fn,
	ast_module_start_fn start_fn,
	ast_module_reload_fn reload_fn,
	ast_module_stop_fn stop_fn)
{
	struct ast_module *module;
	struct ast_module_lib *lib;

	/* Ensure *self is NULL if we have an error. */
	*self = NULL;

	if (!keystr) {
		ast_log(LOG_ERROR, "Module '%s' does not provide a license key.\n", name);
		return -1;
	}

	if (module_key_verify(name, keystr)) {
		return -1;
	}

	if (!ast_strlen_zero(buildopt_sum) && strcmp(buildopt_sum, AST_BUILDOPT_SUM)) {
		ast_log(LOG_WARNING, "Module '%s' was not compiled with the same compile-time options as this version of Asterisk.\n", name);
		ast_log(LOG_WARNING, "Module '%s' will not be initialized as it may cause instability.\n", name);
		return -1;
	}

	if (loader_runlevel == LOADER_RUNLEVEL_EMBEDDING) {
		/* astmm is not initialized yet, do not use MALLOC_DEBUG variants
		 * during embedding. */
		module = ast_std_calloc(1, sizeof(*module));
		if (!module) {
			return -1;
		}

		module->name = ast_std_strdup(name);
		module->checksum = ast_std_strdup(manifest_checksum);
		module->description = ast_std_strdup(desc);
		lib = module->lib = ast_std_calloc(1, sizeof(*module->lib));
		AST_VECTOR_APPEND(&embedded_module_list, module);
	} else {
		struct ast_module_lib_proxy *lproxy;

		module = ast_module_find(name);
		if (!module) {
			return -1;
		}

		if (strcmp(manifest_checksum, module->checksum)) {
			ast_log(LOG_WARNING, "Module '%s' checksum does not match it's manifest.\n", name);
			ast_log(LOG_WARNING, "Module '%s' may experience failed to loads due to unknown dependencies.\n", name);
			ao2_ref(module, -1);
			return -1;
		}

		/* Module is locked during dlopen. */
		lproxy = ao2_t_weakproxy_get_object(module, OBJ_NOLOCK, "retrieve lproxy from module");
		if (!lproxy) {
			ast_log(LOG_ERROR, "Failure finding lib proxy object for %s\n", module->name);
			ao2_ref(module, -1);
			return -1;
		}

		/* no need for reference to lib, one is being held by module_instance_load. */
		lib = lproxy->lib;
		ao2_t_cleanup(lproxy, "done with lproxy");

		if (ast_opt_ref_debug) {
			ao2_t_ref(module, +1, "save to *module->self");
			ao2_t_ref(module, -1, "release ast_module_find");
		}

		/* BUGBUG: can we move this to the manifest? */
		module->description = ast_strdup(desc);

		astobj2_ref_log_ref();
	}

	*self = module;
	module->self = self;
	lib->init_fn = init_fn;
	lib->start_fn = start_fn;
	lib->reload_fn = reload_fn;
	lib->stop_fn = stop_fn;

	return 0;
}

void __ast_module_unregister(struct ast_module **self)
{
	struct ast_module *module;

	if (!*self) {
		return;
	}

	module = *self;
	if (module->self != self) {
		ast_log(LOG_ERROR, "Invalid pointer to __ast_module_unregister.\n");
		ast_log_backtrace();
		ast_assert(0);
		return;
	}

	if (ao2_weakproxy_ref_object(module, 0, 0) > 0) {
		if (AST_VECTOR_SIZE(&modules)) {
			/* The only way this should happen is if some other code in Asterisk runs
			 * dlopen on one of our modules, then runs dlclose twice.  Maybe we'll get
			 * the offender in the backtrace. */
			ast_log(LOG_ERROR, "Active module %s unregistered, expect problems.\n", module->name);
			ast_log_backtrace();
			ast_assert(0);
		} else {
			/* BUGBUG: Logger is probably already shutdown. */
			ast_log(LOG_ERROR, "Module %s did not cleanly shutdown.\n", module->name);
		}
	}

	ast_free(module->description);
	module->description = NULL;

	*self = NULL;
	ao2_t_ref(module, -1, "clear *module->self");
	if (module->block_unload) {
		ao2_t_ref(module->lib, -1, "clear block_unload");
	}

	/* Last.. */
	astobj2_ref_log_unref();
}


void ast_process_pending_reloads(void)
{
	struct reload_queue_item *item;

	loader_runlevel = LOADER_RUNLEVEL_NORMAL;

	AST_LIST_LOCK(&reload_queue);

	if (do_full_reload) {
		do_full_reload = 0;
		ast_log(LOG_NOTICE, "Executing deferred reload request.\n");
		module_reload(NULL);
		AST_LIST_UNLOCK(&reload_queue);
		return;
	}

	while ((item = AST_LIST_REMOVE_HEAD(&reload_queue, entry))) {
		ast_log(LOG_NOTICE, "Executing deferred reload request for module '%s'.\n", item->module->name);
		ast_module_reload(item->module);
		ao2_ref(item->module, -1);
		ast_free(item);
	}

	AST_LIST_UNLOCK(&reload_queue);
}

static void module_instance_dtor(void *obj)
{
	struct ast_module_instance *instance = obj;
	struct ast_module_lib *lib = instance->lib_proxy->lib;
	int ret;

	if (ast_fully_booted) {
		ast_verb(1, "Unloading Module: %s\n", instance->name);
	}

	if (lib->stop_fn) {
		lib->stop_fn();
	}

	ao2_t_cleanup(instance->module, "instance->module");
	ao2_t_cleanup(instance->lib_proxy, "instance->lib_proxy");

	/* We're destroying and nobody has a reference to us, don't bother locking. */
	AST_VECTOR_CALLBACK_VOID(&instance->using, ast_module_disposer_destroy);
	AST_VECTOR_RW_FREE(&instance->using);

	/* If we're still in use then something is wrong. */
	ast_assert(!AST_VECTOR_SIZE(&instance->users));
	AST_VECTOR_RW_FREE(&instance->users);

	AST_VECTOR_RW_WRLOCK(&modules_running);
	ret = AST_VECTOR_REMOVE_ELEM_ORDERED(&modules_running, lib, AST_VECTOR_ELEM_CLEANUP_NOOP);
	AST_VECTOR_RW_UNLOCK(&modules_running);

	if (!ret) {
		ao2_t_ref(lib, -1, "modules_running");
	}
}

static struct ast_module_instance *module_instance_alloc(struct ast_module *module, struct ast_module_lib_proxy *lib_proxy)
{
	struct ast_module_instance *instance = ao2_t_alloc(sizeof(*instance), module_instance_dtor, module->name);

	if (!instance) {
		ao2_ref(lib_proxy, -1);
		return NULL;
	}

	ao2_t_ref(module, +1, "instance->module");
	instance->name = module->name;
	instance->module = module;

	if (ast_opt_ref_debug) {
		/* just for clearer refs log. */
		ao2_t_ref(lib_proxy, +1, "instance->lib_proxy");
		ao2_t_ref(lib_proxy, -1, "release constructor reference");
	}
	instance->lib_proxy = lib_proxy;

	AST_VECTOR_INIT(&instance->users, 0);
	AST_VECTOR_INIT(&instance->using, 0);

	return instance;
}

static void ast_module_instance_run_disposers(struct ast_module_instance *instance, int strength);
static struct ast_module_instance *module_instance_load(struct ast_module *module);

static int module_instance_usersout_cb(void *userdata, int level)
{
	ao2_ref(userdata, +1);
	ast_module_instance_run_disposers(userdata, level);
	ao2_ref(userdata, -1);
	return 0;
}

static int module_instance_alldeps_usersout(struct ast_module_instance *instance)
{
	int i;
	struct ast_module *module = instance->module;
	struct ast_module_lib *lib = instance->lib_proxy->lib;
	struct ast_module_disposer *disposer;

	for (i = 0; i < AST_VECTOR_SIZE(&module->alldeps); i++) {
		struct ast_module *newmodule = ast_module_find(AST_VECTOR_GET(&module->alldeps, i));
		struct ast_module_instance *newinstance;

		if (!newmodule) {
			return -1;
		}

		ao2_lock(newmodule);
		newinstance = module_instance_load(newmodule);
		ao2_unlock(newmodule);
		ao2_ref(newmodule, -1);

		if (!newinstance) {
			return -1;
		}

		ao2_t_ref(newinstance->lib_proxy->lib, +1, lib->module->name);
		AST_VECTOR_APPEND(&lib->using, newinstance->lib_proxy->lib);
		disposer = ast_module_disposer_alloc(newinstance, instance, module_instance_usersout_cb);
		if (!disposer) {
			return -1;
		}

		AST_VECTOR_RW_WRLOCK(&instance->using);
		/* this vector holds the allocation reference. */
		AST_VECTOR_APPEND(&instance->using, disposer);
		if (ast_opt_ref_debug) {
			ao2_t_ref(newinstance, +1, "disposer");
			ao2_t_ref(newinstance, -1, "drop module_instance_load");
		}
		AST_VECTOR_RW_UNLOCK(&instance->using);
	}

	return 0;
}

static void module_lib_dtor(void *obj)
{
	struct ast_module_lib *lib = obj;
	int ret;

	module_dlclose(lib);
	AST_VECTOR_RW_WRLOCK(&modules_loaded);
	ret = AST_VECTOR_REMOVE_ELEM_ORDERED(&modules_loaded, lib->module, AST_VECTOR_ELEM_CLEANUP_NOOP);
	AST_VECTOR_RW_UNLOCK(&modules_loaded);
	if (!ret) {
		ao2_t_ref(lib->module, -1, "remove from modules_loaded");
	}
	ao2_t_ref(lib->module, -1, "lib->module");

	AST_VECTOR_CALLBACK_VOID(&lib->using, ao2_t_ref, -1, lib->module->name);
}

static void module_lib_proxy_dtor(void *obj)
{
	struct ast_module_lib_proxy *p = obj;

	ao2_t_cleanup(p->lib, "constructor");
}

static struct ast_module_instance *module_instance_load(struct ast_module *module)
{
	struct ast_module_lib_proxy *lproxy = NULL;
	struct ast_module_instance *instance = NULL;

	if (module->neverload) {
		ast_log(LOG_ERROR, "%s is flagged to neverload!\n", module->name);
		ast_log(LOG_ERROR, "To use this module you must update modules.conf and restart.\n");
		return NULL;
	}

	if (module->lib) {
		return ao2_t_weakproxy_get_object(module->lib, 0, "already running");
	}

	lproxy = ao2_weakproxy_get_object(module, OBJ_NOLOCK);
	if (lproxy) {
		/*
		 * This can happen if a module instance stops, but ast_module_lib is still held
		 * open.  We cannot reopen the module until all previous ast_module_lib references
		 * are released and dlclose is run.
		 */
		ast_log(LOG_WARNING, "%s: Library has not yet completed unload, try again later.\n", module->name);
		ao2_t_ref(lproxy, -1, "still unloading, try later");
		return NULL;
	}

	lproxy = ao2_t_alloc(sizeof(*lproxy), module_lib_proxy_dtor, module->name);
	if (!lproxy) {
		ast_log(LOG_ERROR, "%s: Allocation Error\n", module->name);
		return NULL;
	}

	lproxy->lib = ao2_t_weakproxy_alloc(sizeof(*lproxy->lib), module_lib_dtor, module->name);
	if (!lproxy->lib) {
		ast_log(LOG_ERROR, "%s: Allocation Error\n", module->name);
		ao2_t_ref(lproxy, -1, "allocation error");
		return NULL;
	}

	lproxy->lib->module = module;
	ao2_t_ref(module, +1, "set lib->module");
	AST_VECTOR_INIT(&lproxy->lib->using, AST_VECTOR_SIZE(&module->alldeps));

	/* This eats the lproxy ref. */
	instance = module_instance_alloc(module, lproxy);
	if (!instance) {
		return NULL;
	}

	if (module_instance_alldeps_usersout(instance)) {
		ast_log(LOG_ERROR, "%s => Dependency failure.\n", module->name);
		ao2_t_ref(instance, -1, "dependency failure");
		return NULL;
	}

	if (loader_runlevel != LOADER_RUNLEVEL_NORMAL) {
		ast_verb(1, "Loading %s.\n", module->name);
	}

	/* This is needed by __ast_module_register, is not available to other threads until
	 * we unlock. */
	ao2_t_weakproxy_set_object(module, lproxy, OBJ_NOLOCK, "set module weakproxy");

	if (module_dlopen(module, lproxy->lib)) {
		ast_log(LOG_ERROR, "module_dlopen returned error for %s\n", module->name);
		ao2_ref(instance, -1);
		return NULL;
	}

	AST_VECTOR_RW_WRLOCK(&modules_loaded);
	AST_VECTOR_ADD_SORTED(&modules_loaded, module, MODULES_VECTOR_SORT);
	AST_VECTOR_RW_UNLOCK(&modules_loaded);
	ao2_t_ref(module, +1, "add to modules_loaded");

	/* init library - ast_module_instance is not yet linked to ast_module_lib. */
	if (lproxy->lib->init_fn && lproxy->lib->init_fn()) {
		ast_log(LOG_ERROR, "%s => Initialization Failed\n", module->name);
		ao2_ref(instance, -1);
		return NULL;
	}

	/* BUGBUG: locking order ast_module then ast_module_instance */
	ao2_t_weakproxy_set_object(lproxy->lib, instance, 0, "set lproxy->lib weakproxy");
	module->lib = lproxy->lib;
	if (ast_opt_ref_debug) {
		ao2_t_ref(module->lib, +1, "save to module->lib");
		ao2_t_ref(module->lib, -1, "clear constructor ref");
	}
	AST_VECTOR_RW_WRLOCK(&modules_running);
	AST_VECTOR_ADD_SORTED(&modules_running, module->lib, MODULES_LIB_VECTOR_SORT);
	AST_VECTOR_RW_UNLOCK(&modules_running);
	ao2_t_ref(module->lib, +1, "modules_running");


	if (!module->lib->start_fn || !module->lib->start_fn()) {
		if (loader_runlevel != LOADER_RUNLEVEL_NORMAL) {
			char tmp[256];
			ast_verb(2, "%s => (%s)\n", module->name, term_color(tmp, module->description, COLOR_BROWN, COLOR_BLACK, sizeof(tmp)));
		} else {
			ast_verb(1, "Started %s => (%s)\n", module->name, module->description);
		}
	} else {
		ast_log(LOG_ERROR, "%s => Start Failed\n", module->name);
		ao2_t_ref(instance, -1, "start failed");
		return NULL;
	}

	return instance;
}

int ast_module_load(struct ast_module *module)
{
	struct ast_module_instance *instance;
	int ret = -1;

	ao2_lock(module);
	if (module->admin_user) {
		ret = 0;
	} else {
		instance = module_instance_load(module);
		if (instance) {
			module->admin_user = instance;
			if (ast_opt_ref_debug) {
				ao2_t_ref(instance, +1, "module->admin_user");
				ao2_t_ref(instance, -1, "from module_instance_load");
			}
			ret = 0;
		}
	}
	ao2_unlock(module);

	return ret;
}

void ast_module_unload(struct ast_module *module, int force)
{
	struct ast_module_instance *instance;

	ao2_lock(module);
	instance = module->admin_user;
	module->admin_user = NULL;
	if (!instance) {
		instance = ao2_weakproxy_get_object(module, OBJ_NOLOCK);
	}
	ao2_unlock(module);

	if (instance) {
		ast_module_instance_run_disposers(instance, force);
		ao2_ref(instance, -1);
	}
}

enum ast_module_reload_result ast_module_reload(struct ast_module *module)
{
	enum ast_module_reload_result ret;

	AST_LIST_LOCK(&reload_queue);
	/* If we aren't fully loaded, we just pretend we reloaded but we queue this
	 * up to run once we are fully loaded. */
	if (loader_runlevel != LOADER_RUNLEVEL_NORMAL) {
		ret = module_reload_queue_request(module);
	} else {
		ret = module_reload(module);
	}
	AST_LIST_UNLOCK(&reload_queue);

	return ret;
}


static void module_disposer_dtor(void *obj)
{
	struct ast_module_disposer *disposer = obj;

	ao2_cleanup(disposer->instance);
}

struct ast_module_disposer *ast_module_disposer_alloc(struct ast_module_instance *instance, void *userdata, ast_module_dispose_cb cb)
{
	struct ast_module_disposer *disposer;

	if (!instance) {
		return NULL;
	}

	disposer = ao2_alloc(sizeof(*disposer), module_disposer_dtor);
	if (!disposer) {
		ao2_ref(instance, -1);
		return NULL;
	}

	/* Eats the reference */
	disposer->instance = instance;
	disposer->userdata = userdata;
	disposer->cb = cb;
	ao2_t_ref(disposer, +1, "add to &instance->users");
	AST_VECTOR_RW_WRLOCK(&instance->users);
	AST_VECTOR_APPEND(&instance->users, disposer);
	AST_VECTOR_RW_UNLOCK(&instance->users);

	return disposer;
}

static void module_disposer_delist(struct ast_module_disposer *disposer)
{
	int res = -1;

	ao2_lock(disposer);
	if (disposer->donotcall)  {
		ao2_unlock(disposer);
		return;
	}

	AST_VECTOR_RW_WRLOCK(&disposer->instance->users);
	res = AST_VECTOR_REMOVE_ELEM_UNORDERED(&disposer->instance->users, disposer, AST_VECTOR_ELEM_CLEANUP_NOOP);
	AST_VECTOR_RW_UNLOCK(&disposer->instance->users);

	if (!res) {
		disposer->donotcall = 1;
		/* Caller has a reference so disposer will not be destroyed here. */
		ao2_t_ref(disposer, -1, "remove from &instance->users");
	}

	ao2_unlock(disposer);
}

void ast_module_disposer_destroy(struct ast_module_disposer *disposer)
{
	module_disposer_delist(disposer);
	ao2_ref(disposer, -1);
}

static void module_disposer_stop(struct ast_module_disposer *disposer, int level)
{
	int res;

	ao2_lock(disposer);
	if (disposer->donotcall || disposer->inprogress) {
		ao2_unlock(disposer);
		return;
	}
	disposer->inprogress = 1;
	ao2_unlock(disposer);

	/* Do not run cb within a lock. */
	res = disposer->cb(disposer->userdata, level);

	ao2_lock(disposer);
	disposer->inprogress = 0;
	if (!res) {
		module_disposer_delist(disposer);
	}
	ao2_unlock(disposer);
}

static void ast_module_instance_run_disposers(struct ast_module_instance *instance, int level)
{
	int i;
	struct ast_module_disposers_rw *disposers;

	AST_VECTOR_RW_RDLOCK(&instance->users);
	disposers = AST_VECTOR_DUP_AO2(&instance->users);
	AST_VECTOR_RW_UNLOCK(&instance->users);

	for (i = 0; i < AST_VECTOR_SIZE(disposers); i++) {
		struct ast_module_disposer *disposer = AST_VECTOR_GET(disposers, i);

		module_disposer_stop(disposer, level);
	}

	AST_VECTOR_CALLBACK_VOID(disposers, ao2_ref, -1);
	AST_VECTOR_PTR_FREE(disposers);
}

static void ast_module_lib_run_disposers(struct ast_module_lib *lib, int level)
{
	struct ast_module_instance *instance;

	instance = ao2_weakproxy_get_object(lib, 0);
	if (instance) {
		ast_module_instance_run_disposers(instance, level);
		ao2_ref(instance, -1);
	}
}


void ast_module_shutdown(void)
{
	int i = 0;
	struct ast_module_lib *lib = NULL;
	struct ast_module_libs *copy;
	struct ast_module *module;
	struct ast_modules *modules_copy = NULL;
	struct ast_module_instance *instance;

	/* BUGBUG: set loader_runlevel to prevent future loads. */
	AST_VECTOR_RW_RDLOCK(&modules);
	for (i = 0; i < AST_VECTOR_SIZE(&modules); i++) {
		module = AST_VECTOR_GET(&modules, i);

		ao2_lock(module);
		instance = module->admin_user;
		module->admin_user = NULL;
		ao2_unlock(module);

		ao2_t_cleanup(instance, "clear module->admin_user");
	}
	AST_VECTOR_RW_UNLOCK(&modules);

	copy = NULL;
	AST_VECTOR_RW_RDLOCK(&modules_running);
	if (AST_VECTOR_SIZE(&modules_running)) {
		copy = AST_VECTOR_DUP_AO2((struct ast_module_libs *)&modules_running);
	}
	AST_VECTOR_RW_UNLOCK(&modules_running);

	if (!copy) {
		/* Nothing running. */
		goto done;
	}

	for (i = 0; i < AST_VECTOR_SIZE(copy); i++) {
		ast_module_lib_run_disposers(AST_VECTOR_GET(copy, i), 6);
	}

	for (i = 0; i < AST_VECTOR_SIZE(copy); i++) {
		lib = AST_VECTOR_GET(copy, i);

		/* This is just to avoid unneeded reference to instance if we can help it. */
		if (!lib->module->block_unload) {
			ast_log(LOG_NOTICE, "%s module not marked block_unload.\n", lib->module->name);
			continue;
		}

		instance = ast_module_lib_get_instance(lib);
		if (!instance) {
			ast_log(LOG_NOTICE, "%s instance not found.\n", lib->module->name);
			/* This could happen during a retry. */
			continue;
		}

		if (instance->block_unload) {
			instance->block_unload = 0;
			ao2_t_ref(instance, -1, "block_unload");
		}
		ao2_t_ref(instance, -1, "release from ast_module_lib_get_instance");

		ast_module_lib_run_disposers(lib, 6);
	}

	AST_VECTOR_CALLBACK_VOID(copy, ao2_t_ref, -1, "uncopy modules_running vector");
	AST_VECTOR_PTR_FREE(copy);

done:
	if (0) {
	AST_VECTOR_RW_RDLOCK(&modules_loaded);
	if (AST_VECTOR_SIZE(&modules_loaded)) {
		modules_copy = AST_VECTOR_DUP_AO2((struct ast_modules *)&modules_loaded);
	}
	AST_VECTOR_RW_UNLOCK(&modules_loaded);

	if (modules_copy) {
		for (i = 0; i < AST_VECTOR_SIZE(modules_copy); i++) {
			lib = ast_module_get_lib_loaded(AST_VECTOR_GET(modules_copy, i));
			if (lib) {
				ao2_t_ref(lib, -1, "from ast_module_get_lib_loaded");
				ao2_t_ref(lib, -1, "block_unload");
			}
		}
	}

	AST_VECTOR_CALLBACK_VOID(modules_copy, ao2_t_ref, -1, "uncopy modules_loading vector");
	AST_VECTOR_PTR_FREE(modules_copy);
}


	AST_VECTOR_CALLBACK_VOID(&modules, ao2_t_ref, -1, "remove from vector");
	AST_VECTOR_RW_FREE(&modules);

	AST_VECTOR_CALLBACK_VOID(&providertypes, module_providertype_dtor);
	AST_VECTOR_RW_FREE(&providertypes);

	AST_VECTOR_CALLBACK_VOID(&neverload, ao2_cleanup);
	AST_VECTOR_FREE(&neverload);
}

static void module_embedded_reregister(struct ast_module *item)
{
	/* AST_BUILDOPT_SUM and ASTERISK_GPL_KEY were already verified at startup */
	__ast_module_register(item->self, item->name, AST_BUILDOPT_SUM,
		item->checksum, ASTERISK_GPL_KEY, item->description,
		item->lib->init_fn, item->lib->start_fn, item->lib->reload_fn, item->lib->stop_fn);
	/* item is not an ao2 object, it was just used for temporary storage to register
	 * with the object initialized from the manifest.  It was also created before
	 * astmm was initialized, free with standard allocators. */
	ast_std_free(item->name);
	ast_std_free(item->description);
	ast_std_free(item->checksum);
	ast_std_free(item->lib);
	ast_std_free(item);
}

static void module_embedded_init(void)
{
	AST_VECTOR_CALLBACK_VOID(&embedded_module_list, module_embedded_reregister);
	AST_VECTOR_FREE(&embedded_module_list);
}

static void module_strip_extension(char *name)
{
	static int warned_strip_extension = 0;
	size_t len = strlen(name);

	if (len && !strcmp(&name[len - 3], ".so")) {
		if (!warned_strip_extension) {
			warned_strip_extension = 1;
			ast_log(LOG_WARNING, "Use of '.so' with module names is deprecated.\n");
		}
		name[len - 3] = '\0';
	}
}

struct module_load {
	int required;
	struct ast_module *module;
};

AST_VECTOR(module_load_list,struct module_load *);

static int module_load_list_sort(struct module_load *l1, struct module_load *l2)
{
	return l1->module->load_priority - l2->module->load_priority;
}

static int module_load_list(struct module_load_list *vec)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(vec); i++) {
		struct module_load *item = AST_VECTOR_GET(vec, i);

		if (ast_module_load(item->module)) {
			if (item->required) {
				ast_log(LOG_WARNING, "Could not load required module %s\n", item->module->name);
				return -1;
			}
			ast_log(LOG_WARNING, "Could not load module %s\n", item->module->name);
		}
	}

	return 0;
}

#define MODULE_LOAD_LIST_CMP(elem, value) !strcmp(elem->module->name, value)
static int module_load_list_append(struct module_load_list *vec, const char *name, int required)
{
	struct ast_module *module;
	struct module_load **ptr;
	struct module_load *item;

	ptr = AST_VECTOR_GET_CMP(vec, name, MODULE_LOAD_LIST_CMP);
	if (ptr) {
		(*ptr)->required |= required;
		return 0;
	}

	module = ast_module_find(name);
	if (!module) {
		return -1;
	}

	item = ast_calloc(1, sizeof(*item));
	if (!item) {
		ao2_cleanup(module);
		return -1;
	}

	item->required = required;
	item->module = module;
	AST_VECTOR_ADD_SORTED(vec, item, module_load_list_sort);

	return 0;
}

static void module_load_dtor(struct module_load *obj) {
	ao2_cleanup(obj->module);
	ast_free(obj);
}

static int module_load_list_remove(struct module_load_list *vec, const char *module, int forbid)
{
	struct module_load **ptr;
	struct module_load *item;

	ptr = AST_VECTOR_GET_CMP(vec, module, MODULE_LOAD_LIST_CMP);
	if (!ptr) {
		return 0;
	}

	item = *ptr;
	if (item->required) {
		return -1;
	}

	AST_VECTOR_REMOVE_ELEM_ORDERED(vec, item, module_load_dtor);
	return 0;
}

int modules_init(void)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	struct ast_variable *v;
	char *value;
	const char *use_type = "module";
	int autoload = 0;
	struct module_load_list load;
	struct ast_string_vector noload;
	int i;

	loader_runlevel = LOADER_RUNLEVEL_LOADING;

	if (AST_VECTOR_INIT(&load, 0)
		|| AST_VECTOR_INIT(&noload, 0)
		|| AST_VECTOR_INIT(&neverload, 0)) {
		return -1;
	}

	if (module_manifest_init()) {
		return -1;
	}

	cfg = ast_config_load2("modules.conf", "" /* core, can't reload */, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Failed to load 'modules.conf'.\n");
		return -1;
	}

	for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
		if (!strcmp(v->name, "autoload")) {
			autoload = ast_true(v->value) ? 1 : 0;
		} else {
			value = ast_strdup(v->value);

			module_strip_extension(value);
			if (!strcmp(v->name, "load")) {
				AST_VECTOR_REMOVE_CMP_ORDERED(&noload, value, !strcmp, ast_free);
				module_load_list_append(&load, value, 0);
			} else if (!strcmp(v->name, "noload")) {
				module_load_list_remove(&load, value, 0);
				if (!AST_VECTOR_GET_CMP(&noload, value, !strcmp)) {
					AST_VECTOR_APPEND(&noload, value);
					value = NULL;
				}
			} else if (!strcmp(v->name, "require")) {
				if (AST_VECTOR_GET_CMP(&neverload, value, !strcmp)) {
					ast_log(LOG_ERROR, "%s configured as require and neverload, startup cannot continue.\n", value);
					return -1;
				} else {
					AST_VECTOR_REMOVE_CMP_ORDERED(&noload, value, !strcmp, ast_free);
					module_load_list_append(&load, value, 1);
				}
			} else if (!strcmp(v->name, "neverload")) {
				if (module_load_list_remove(&load, value, 1)) {
					ast_log(LOG_ERROR, "%s configured as require and neverload, startup cannot continue.\n", value);
					return -1;
				} else if (!AST_VECTOR_GET_CMP(&neverload, value, !strcmp)) {
					struct ast_module *module = ast_module_find(value);

					module->neverload = 1;
					ao2_cleanup(module);

					AST_VECTOR_APPEND(&neverload, value);
					value = NULL;
				}
			} else {
				ast_log(LOG_ERROR, "Unknown property '%s' in modules.conf\n", v->name);
				return -1;
			}
			ast_free(value);
		}
	}

	module_embedded_init();

	for (v = ast_variable_browse(cfg, "uses"); v; v = v->next) {
		if (!strcmp(v->name, "type")) {
			use_type = v->value;
		} else {
			struct ast_module *mod;
			char *name = ast_strdup(v->name);
			char *value = NULL;

			module_strip_extension(name);

			mod = ast_module_find(name);
			if (!mod) {
				ast_log(LOG_ERROR, "Failed to find provider %s of type %s\n", value, use_type);
				return -1;
			}

			if (!strcmp(use_type, "module")) {
				value = ast_strdup(v->value);
				module_strip_extension(value);
			}

			if (module_manifest_uses_add(mod, use_type, value ?: v->value)) {
				return -1;
			}

			ao2_cleanup(mod);
			ast_free(name);
			ast_free(value);
		}
	}

	if (module_manifest_build_alldeps()) {
		ast_log(LOG_ERROR, "Module dependency graph failed, aborting startup.\n");
		return -1;
	}

	if (autoload) {
		for (i = 0; i < AST_VECTOR_SIZE(&modules); i++) {
			struct ast_module *mod = AST_VECTOR_GET(&modules, i);

			if (!AST_VECTOR_GET_CMP(&neverload, mod->name, !strcmp)
				&& !AST_VECTOR_GET_CMP(&noload, mod->name, !strcmp)) {
				module_load_list_append(&load, mod->name, 0);
			}
		}
	}

	if (module_load_list(&load)) {
		return -1;
	}

	ast_config_destroy(cfg);

	AST_VECTOR_CALLBACK_VOID(&noload, ast_free);
	AST_VECTOR_FREE(&noload);

	AST_VECTOR_CALLBACK_VOID(&load, module_load_dtor);
	AST_VECTOR_FREE(&load);

	return module_cli_init();
}
