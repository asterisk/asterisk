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

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"
#include "asterisk/paths.h"	/* use ast_config_AST_MODULE_DIR */
#include <dirent.h>

#include "asterisk/dlinkedlists.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/term.h"
#include "asterisk/acl.h"
#include "asterisk/manager.h"
#include "asterisk/cdr.h"
#include "asterisk/enum.h"
#include "asterisk/http.h"
#include "asterisk/lock.h"
#include "asterisk/features_config.h"
#include "asterisk/dsp.h"
#include "asterisk/udptl.h"
#include "asterisk/heap.h"
#include "asterisk/app.h"
#include "asterisk/test.h"
#include "asterisk/sounds_index.h"

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

static unsigned int embedding = 1; /* we always start out by registering embedded modules,
				      since they are here before we dlopen() any
				   */

/*!
 * \brief Internal flag to indicate all modules have been initially loaded.
 */
static int modules_loaded;

struct ast_module {
	const struct ast_module_info *info;
	/* Used to get module references into refs log */
	void *ref_debug;
	void *lib;					/* the shared lib, or NULL if embedded */
	int usecount;					/* the number of 'users' currently in this module */
	struct module_user_list users;			/* the list of users in the module */
	struct {
		unsigned int running:1;
		unsigned int declined:1;
		unsigned int keepuntilshutdown:1;
	} flags;
	AST_LIST_ENTRY(ast_module) list_entry;
	AST_DLLIST_ENTRY(ast_module) entry;
	char resource[0];
};

static AST_DLLIST_HEAD_STATIC(module_list, ast_module);

const char *ast_module_name(const struct ast_module *mod)
{
	if (!mod || !mod->info) {
		return NULL;
	}

	return mod->info->name;
}

/*
 * module_list is cleared by its constructor possibly after
 * we start accumulating embedded modules, so we need to
 * use another list (without the lock) to accumulate them.
 * Then we update the main list when embedding is done.
 */
static struct module_list embedded_module_list;

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

/* when dynamic modules are being loaded, ast_module_register() will
   need to know what filename the module was loaded from while it
   is being registered
*/
static struct ast_module *resource_being_loaded;

/* XXX: should we check for duplicate resource names here? */

void ast_module_register(const struct ast_module_info *info)
{
	struct ast_module *mod;

	if (embedding) {
		if (!(mod = ast_calloc(1, sizeof(*mod) + strlen(info->name) + 1)))
			return;
		strcpy(mod->resource, info->name);
	} else {
		mod = resource_being_loaded;
	}

	ast_debug(5, "Registering module %s\n", info->name);

	mod->info = info;
	if (ast_opt_ref_debug) {
		mod->ref_debug = ao2_t_alloc(0, NULL, info->name);
	}
	AST_LIST_HEAD_INIT(&mod->users);

	/* during startup, before the loader has been initialized,
	   there are no threads, so there is no need to take the lock
	   on this list to manipulate it. it is also possible that it
	   might be unsafe to use the list lock at that point... so
	   let's avoid it altogether
	*/
	if (embedding) {
		AST_DLLIST_INSERT_TAIL(&embedded_module_list, mod, entry);
	} else {
		AST_DLLIST_LOCK(&module_list);
		/* it is paramount that the new entry be placed at the tail of
		   the list, otherwise the code that uses dlopen() to load
		   dynamic modules won't be able to find out if the module it
		   just opened was registered or failed to load
		*/
		AST_DLLIST_INSERT_TAIL(&module_list, mod, entry);
		AST_DLLIST_UNLOCK(&module_list);
	}

	/* give the module a copy of its own handle, for later use in registrations and the like */
	*((struct ast_module **) &(info->self)) = mod;
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

	if (mod) {
		ast_debug(5, "Unregistering module %s\n", info->name);
		AST_LIST_HEAD_DESTROY(&mod->users);
		ao2_cleanup(mod->ref_debug);
		ast_free(mod);
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

/*! \note
 * In addition to modules, the reload command handles some extra keywords
 * which are listed here together with the corresponding handlers.
 * This table is also used by the command completion code.
 */
static struct reload_classes {
	const char *name;
	int (*reload_fn)(void);
} reload_classes[] = {	/* list in alpha order, longest match first for cli completion */
	{ "acl",         ast_named_acl_reload },
	{ "cdr",         ast_cdr_engine_reload },
	{ "cel",         ast_cel_engine_reload },
	{ "dnsmgr",      dnsmgr_reload },
	{ "dsp",         ast_dsp_reload},
	{ "extconfig",   read_config_maps },
	{ "enum",        ast_enum_reload },
	{ "features",    ast_features_config_reload },
	{ "http",        ast_http_reload },
	{ "indications", ast_indications_reload },
	{ "logger",      logger_reload },
	{ "manager",     reload_manager },
	{ "plc",         ast_plc_reload },
	{ "sounds",      ast_sounds_reindex },
	{ "udptl",       ast_udptl_reload },
	{ NULL,          NULL }
};

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

static int resource_name_match(const char *name1_in, const char *name2_in)
{
	char *name1 = (char *) name1_in;
	char *name2 = (char *) name2_in;

	/* trim off any .so extensions */
	if (!strcasecmp(name1 + strlen(name1) - 3, ".so")) {
		name1 = ast_strdupa(name1);
		name1[strlen(name1) - 3] = '\0';
	}
	if (!strcasecmp(name2 + strlen(name2) - 3, ".so")) {
		name2 = ast_strdupa(name2);
		name2[strlen(name2) - 3] = '\0';
	}

	return strcasecmp(name1, name2);
}

static struct ast_module *find_resource(const char *resource, int do_lock)
{
	struct ast_module *cur;

	if (do_lock) {
		AST_DLLIST_LOCK(&module_list);
	}

	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		if (!resource_name_match(resource, cur->resource))
			break;
	}

	if (do_lock) {
		AST_DLLIST_UNLOCK(&module_list);
	}

	return cur;
}

#ifdef LOADABLE_MODULES

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
	 * Or the lazy resolution of a global symbol (very likely, since that is
	 * how we load all of our modules that export global symbols).
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

static enum ast_module_load_result load_resource(const char *resource_name, unsigned int global_symbols_only, struct ast_heap *resource_heap, int required);

static struct ast_module *load_dynamic_module(const char *resource_in, unsigned int global_symbols_only, struct ast_heap *resource_heap)
{
	char fn[PATH_MAX] = "";
	void *lib = NULL;
	struct ast_module *mod;
	unsigned int wants_global;
	int space;	/* room needed for the descriptor */
	int missing_so = 0;

	space = sizeof(*resource_being_loaded) + strlen(resource_in) + 1;
	if (strcasecmp(resource_in + strlen(resource_in) - 3, ".so")) {
		missing_so = 1;
		space += 3;	/* room for the extra ".so" */
	}

	snprintf(fn, sizeof(fn), "%s/%s%s", ast_config_AST_MODULE_DIR, resource_in, missing_so ? ".so" : "");

	/* make a first load of the module in 'quiet' mode... don't try to resolve
	   any symbols, and don't export any symbols. this will allow us to peek into
	   the module's info block (if available) to see what flags it has set */

	resource_being_loaded = ast_calloc(1, space);
	if (!resource_being_loaded)
		return NULL;
	strcpy(resource_being_loaded->resource, resource_in);
	if (missing_so)
		strcat(resource_being_loaded->resource, ".so");

	if (!(lib = dlopen(fn, RTLD_LAZY | RTLD_LOCAL))) {
		ast_log(LOG_WARNING, "Error loading module '%s': %s\n", resource_in, dlerror());
		ast_free(resource_being_loaded);
		return NULL;
	}

	/* the dlopen() succeeded, let's find out if the module
	   registered itself */
	/* note that this will only work properly as long as
	   ast_module_register() (which is called by the module's
	   constructor) places the new module at the tail of the
	   module_list
	*/
	if (resource_being_loaded != (mod = AST_DLLIST_LAST(&module_list))) {
		ast_log(LOG_WARNING, "Module '%s' did not register itself during load\n", resource_in);
		/* no, it did not, so close it and return */
		logged_dlclose(resource_in, lib);
		/* note that the module's destructor will call ast_module_unregister(),
		   which will free the structure we allocated in resource_being_loaded */
		return NULL;
	}

	wants_global = ast_test_flag(mod->info, AST_MODFLAG_GLOBAL_SYMBOLS);

	/* if we are being asked only to load modules that provide global symbols,
	   and this one does not, then close it and return */
	if (global_symbols_only && !wants_global) {
		logged_dlclose(resource_in, lib);
		return NULL;
	}

	logged_dlclose(resource_in, lib);
	resource_being_loaded = NULL;

	/* start the load process again */
	resource_being_loaded = ast_calloc(1, space);
	if (!resource_being_loaded)
		return NULL;
	strcpy(resource_being_loaded->resource, resource_in);
	if (missing_so)
		strcat(resource_being_loaded->resource, ".so");

	if (!(lib = dlopen(fn, wants_global ? RTLD_LAZY | RTLD_GLOBAL : RTLD_NOW | RTLD_LOCAL))) {
		ast_log(LOG_WARNING, "Error loading module '%s': %s\n", resource_in, dlerror());
		ast_free(resource_being_loaded);
		return NULL;
	}

	/* since the module was successfully opened, and it registered itself
	   the previous time we did that, we're going to assume it worked this
	   time too :) */

	AST_DLLIST_LAST(&module_list)->lib = lib;
	resource_being_loaded = NULL;

	return AST_DLLIST_LAST(&module_list);
}

#endif

void ast_module_shutdown(void)
{
	struct ast_module *mod;
	int somethingchanged = 1, final = 0;

	AST_DLLIST_LOCK(&module_list);

	/*!\note Some resources, like timers, are started up dynamically, and thus
	 * may be still in use, even if all channels are dead.  We must therefore
	 * check the usecount before asking modules to unload. */
	do {
		if (!somethingchanged) {
			/*!\note If we go through the entire list without changing
			 * anything, ignore the usecounts and unload, then exit. */
			final = 1;
		}

		/* Reset flag before traversing the list */
		somethingchanged = 0;

		AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&module_list, mod, entry) {
			if (!final && mod->usecount) {
				ast_debug(1, "Passing on %s: its use count is %d\n",
					mod->resource, mod->usecount);
				continue;
			}
			AST_DLLIST_REMOVE_CURRENT(entry);
			if (mod->flags.running && !mod->flags.declined && mod->info->unload) {
				ast_verb(1, "Unloading %s\n", mod->resource);
				mod->info->unload();
			}
			AST_LIST_HEAD_DESTROY(&mod->users);
			ao2_cleanup(mod->ref_debug);
			ast_free(mod);
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
	} while (somethingchanged && !final);

	AST_DLLIST_UNLOCK(&module_list);
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

	if (!error && !mod->lib && mod->info && mod->info->restore_globals)
		mod->info->restore_globals();

#ifdef LOADABLE_MODULES
	if (!error) {
		unload_dynamic_module(mod);
		ast_test_suite_event_notify("MODULE_UNLOAD", "Message: %s", resource_name);
	}
#endif

	if (!error)
		ast_update_use_count();

	return res;
}

char *ast_module_helper(const char *line, const char *word, int pos, int state, int rpos, int needsreload)
{
	struct ast_module *cur;
	int i, which=0, l = strlen(word);
	char *ret = NULL;

	if (pos != rpos)
		return NULL;

	AST_DLLIST_LOCK(&module_list);
	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		if (!strncasecmp(word, cur->resource, l) &&
		    (cur->info->reload || !needsreload) &&
		    ++which > state) {
			ret = ast_strdup(cur->resource);
			break;
		}
	}
	AST_DLLIST_UNLOCK(&module_list);

	if (!ret) {
		for (i=0; !ret && reload_classes[i].name; i++) {
			if (!strncasecmp(word, reload_classes[i].name, l) && ++which > state)
				ret = ast_strdup(reload_classes[i].name);
		}
	}

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
 * \brief Publish a \ref stasis message regarding the reload result
 */
static void publish_reload_message(const char *name, enum ast_module_reload_result result)
{
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, event_object, NULL, ast_json_unref);
	char res_buffer[8];

	if (!ast_manager_get_generic_type()) {
		return;
	}

	snprintf(res_buffer, sizeof(res_buffer), "%u", result);
	event_object = ast_json_pack("{s: s, s: s}",
			"Module", S_OR(name, "All"),
			"Status", res_buffer);
	json_object = ast_json_pack("{s: s, s: i, s: O}",
			"type", "Reload",
			"class_type", EVENT_FLAG_SYSTEM,
			"event", event_object);

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

enum ast_module_reload_result ast_module_reload(const char *name)
{
	struct ast_module *cur;
	enum ast_module_reload_result res = AST_MODULE_RELOAD_NOT_FOUND;
	int i;

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
	ast_lastreloadtime = ast_tvnow();

	if (ast_opt_lock_confdir) {
		int try;
		int res;
		for (try = 1, res = AST_LOCK_TIMEOUT; try < 6 && (res == AST_LOCK_TIMEOUT); try++) {
			res = ast_lock_path(ast_config_AST_CONFIG_DIR);
			if (res == AST_LOCK_TIMEOUT) {
				ast_log(LOG_WARNING, "Failed to grab lock on %s, try %d\n", ast_config_AST_CONFIG_DIR, try);
			}
		}
		if (res != AST_LOCK_SUCCESS) {
			ast_log(AST_LOG_WARNING, "Cannot grab lock on %s\n", ast_config_AST_CONFIG_DIR);
			ast_mutex_unlock(&reloadlock);
			res = AST_MODULE_RELOAD_ERROR;
			goto module_reload_exit;
		}
	}

	/* Call "predefined" reload here first */
	for (i = 0; reload_classes[i].name; i++) {
		if (!name || !strcasecmp(name, reload_classes[i].name)) {
			if (reload_classes[i].reload_fn() == AST_MODULE_LOAD_SUCCESS) {
				res = AST_MODULE_RELOAD_SUCCESS;
			}
		}
	}

	if (name && res == AST_MODULE_RELOAD_SUCCESS) {
		if (ast_opt_lock_confdir) {
			ast_unlock_path(ast_config_AST_CONFIG_DIR);
		}
		ast_mutex_unlock(&reloadlock);
		goto module_reload_exit;
	}

	AST_DLLIST_LOCK(&module_list);
	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		const struct ast_module_info *info = cur->info;

		if (name && resource_name_match(name, cur->resource))
			continue;

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
		}
		if (name) {
			break;
		}
	}
	AST_DLLIST_UNLOCK(&module_list);

	if (ast_opt_lock_confdir) {
		ast_unlock_path(ast_config_AST_CONFIG_DIR);
	}
	ast_mutex_unlock(&reloadlock);

module_reload_exit:
	publish_reload_message(name, res);
	return res;
}

static unsigned int inspect_module(const struct ast_module *mod)
{
	if (!mod->info->description) {
		ast_log(LOG_WARNING, "Module '%s' does not provide a description.\n", mod->resource);
		return 1;
	}

	if (!mod->info->key) {
		ast_log(LOG_WARNING, "Module '%s' does not provide a license key.\n", mod->resource);
		return 1;
	}

	if (verify_key((unsigned char *) mod->info->key)) {
		ast_log(LOG_WARNING, "Module '%s' did not provide a valid license key.\n", mod->resource);
		return 1;
	}

	if (!ast_strlen_zero(mod->info->buildopt_sum) &&
	    strcmp(buildopt_sum, mod->info->buildopt_sum)) {
		ast_log(LOG_WARNING, "Module '%s' was not compiled with the same compile-time options as this version of Asterisk.\n", mod->resource);
		ast_log(LOG_WARNING, "Module '%s' will not be initialized as it may cause instability.\n", mod->resource);
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
		return AST_MODULE_LOAD_FAILURE;
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

		ast_update_use_count();
		break;
	case AST_MODULE_LOAD_DECLINE:
		mod->flags.declined = 1;
		break;
	case AST_MODULE_LOAD_FAILURE:
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
 *  If the ast_heap is provided (not NULL) the module is found and added to the
 *  heap without running the module's load() function.  By doing this, modules
 *  added to the resource_heap can be initialized later in order by priority.
 *
 *  If the ast_heap is not provided, the module's load function will be executed
 *  immediately */
static enum ast_module_load_result load_resource(const char *resource_name, unsigned int global_symbols_only, struct ast_heap *resource_heap, int required)
{
	struct ast_module *mod;
	enum ast_module_load_result res = AST_MODULE_LOAD_SUCCESS;

	if ((mod = find_resource(resource_name, 0))) {
		if (mod->flags.running) {
			ast_log(LOG_WARNING, "Module '%s' already exists.\n", resource_name);
			return AST_MODULE_LOAD_DECLINE;
		}
		if (global_symbols_only && !ast_test_flag(mod->info, AST_MODFLAG_GLOBAL_SYMBOLS))
			return AST_MODULE_LOAD_SKIP;
	} else {
#ifdef LOADABLE_MODULES
		if (!(mod = load_dynamic_module(resource_name, global_symbols_only, resource_heap))) {
			/* don't generate a warning message during load_modules() */
			if (!global_symbols_only) {
				ast_log(LOG_WARNING, "Module '%s' could not be loaded.\n", resource_name);
				return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
			} else {
				return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SKIP;
			}
		}
#else
		ast_log(LOG_WARNING, "Module support is not available. Module '%s' could not be loaded.\n", resource_name);
		return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
#endif
	}

	if (inspect_module(mod)) {
		ast_log(LOG_WARNING, "Module '%s' could not be loaded.\n", resource_name);
#ifdef LOADABLE_MODULES
		unload_dynamic_module(mod);
#endif
		return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
	}

	if (!mod->lib && mod->info->backup_globals && mod->info->backup_globals()) {
		ast_log(LOG_WARNING, "Module '%s' was unable to backup its global data.\n", resource_name);
		return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
	}

	mod->flags.declined = 0;

	if (resource_heap) {
		ast_heap_push(resource_heap, mod);
		res = AST_MODULE_LOAD_PRIORITY;
	} else {
		res = start_resource(mod);
	}

	return res;
}

int ast_load_resource(const char *resource_name)
{
	int res;
	AST_DLLIST_LOCK(&module_list);
	res = load_resource(resource_name, 0, NULL, 0);
	if (!res) {
		ast_test_suite_event_notify("MODULE_LOAD", "Message: %s", resource_name);
	}
	AST_DLLIST_UNLOCK(&module_list);

	return res;
}

struct load_order_entry {
	char *resource;
	int required;
	AST_LIST_ENTRY(load_order_entry) entry;
};

AST_LIST_HEAD_NOLOCK(load_order, load_order_entry);

static struct load_order_entry *add_to_load_order(const char *resource, struct load_order *load_order, int required)
{
	struct load_order_entry *order;

	AST_LIST_TRAVERSE(load_order, order, entry) {
		if (!resource_name_match(order->resource, resource)) {
			/* Make sure we have the proper setting for the required field
			   (we might have both load= and required= lines in modules.conf) */
			order->required |= required;
			return NULL;
		}
	}

	if (!(order = ast_calloc(1, sizeof(*order))))
		return NULL;

	order->resource = ast_strdup(resource);
	order->required = required;
	AST_LIST_INSERT_TAIL(load_order, order, entry);

	return order;
}

static int mod_load_cmp(void *a, void *b)
{
	struct ast_module *a_mod = (struct ast_module *) a;
	struct ast_module *b_mod = (struct ast_module *) b;
	/* if load_pri is not set, default is 128.  Lower is better */
	int a_pri = ast_test_flag(a_mod->info, AST_MODFLAG_LOAD_ORDER) ? a_mod->info->load_pri : 128;
	int b_pri = ast_test_flag(b_mod->info, AST_MODFLAG_LOAD_ORDER) ? b_mod->info->load_pri : 128;

	/*
	 * Returns comparison values for a min-heap
	 * <0 a_pri > b_pri
	 * =0 a_pri == b_pri
	 * >0 a_pri < b_pri
	 */
	return b_pri - a_pri;
}

/*! loads modules in order by load_pri, updates mod_count
	\return -1 on failure to load module, -2 on failure to load required module, otherwise 0
*/
static int load_resource_list(struct load_order *load_order, unsigned int global_symbols, int *mod_count)
{
	struct ast_heap *resource_heap;
	struct load_order_entry *order;
	struct ast_module *mod;
	int count = 0;
	int res = 0;

	if(!(resource_heap = ast_heap_create(8, mod_load_cmp, -1))) {
		return -1;
	}

	/* first, add find and add modules to heap */
	AST_LIST_TRAVERSE_SAFE_BEGIN(load_order, order, entry) {
		switch (load_resource(order->resource, global_symbols, resource_heap, order->required)) {
		case AST_MODULE_LOAD_SUCCESS:
		case AST_MODULE_LOAD_DECLINE:
			AST_LIST_REMOVE_CURRENT(entry);
			ast_free(order->resource);
			ast_free(order);
			break;
		case AST_MODULE_LOAD_FAILURE:
			ast_log(LOG_ERROR, "*** Failed to load module %s - %s\n", order->resource, order->required ? "Required" : "Not required");
			fprintf(stderr, "*** Failed to load module %s - %s\n", order->resource, order->required ? "Required" : "Not required");
			res = order->required ? -2 : -1;
			goto done;
		case AST_MODULE_LOAD_SKIP:
			break;
		case AST_MODULE_LOAD_PRIORITY:
			AST_LIST_REMOVE_CURRENT(entry);
			ast_free(order->resource);
			ast_free(order);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* second remove modules from heap sorted by priority */
	while ((mod = ast_heap_pop(resource_heap))) {
		switch (start_resource(mod)) {
		case AST_MODULE_LOAD_SUCCESS:
			count++;
		case AST_MODULE_LOAD_DECLINE:
			break;
		case AST_MODULE_LOAD_FAILURE:
			res = -1;
			goto done;
		case AST_MODULE_LOAD_SKIP:
		case AST_MODULE_LOAD_PRIORITY:
			break;
		}
	}

done:
	if (mod_count) {
		*mod_count += count;
	}
	ast_heap_destroy(resource_heap);

	return res;
}

int load_modules(unsigned int preload_only)
{
	struct ast_config *cfg;
	struct ast_module *mod;
	struct load_order_entry *order;
	struct ast_variable *v;
	unsigned int load_count;
	struct load_order load_order;
	int res = 0;
	struct ast_flags config_flags = { 0 };
	int modulecount = 0;

#ifdef LOADABLE_MODULES
	struct dirent *dirent;
	DIR *dir;
#endif

	/* all embedded modules have registered themselves by now */
	embedding = 0;

	ast_verb(1, "Asterisk Dynamic Loader Starting:\n");

	AST_LIST_HEAD_INIT_NOLOCK(&load_order);

	AST_DLLIST_LOCK(&module_list);

	if (embedded_module_list.first) {
		module_list.first = embedded_module_list.first;
		module_list.last = embedded_module_list.last;
		embedded_module_list.first = NULL;
	}

	cfg = ast_config_load2(AST_MODULE_CONFIG, "" /* core, can't reload */, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "No '%s' found, no modules will be loaded.\n", AST_MODULE_CONFIG);
		goto done;
	}

	/* first, find all the modules we have been explicitly requested to load */
	for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
		if (!strcasecmp(v->name, preload_only ? "preload" : "load")) {
			add_to_load_order(v->value, &load_order, 0);
		}
		if (!strcasecmp(v->name, preload_only ? "preload-require" : "require")) {
			/* Add the module to the list and make sure it's required */
			add_to_load_order(v->value, &load_order, 1);
			ast_debug(2, "Adding module to required list: %s (%s)\n", v->value, v->name);
		}

	}

	/* check if 'autoload' is on */
	if (!preload_only && ast_true(ast_variable_retrieve(cfg, "modules", "autoload"))) {
		/* if so, first add all the embedded modules that are not already running to the load order */
		AST_DLLIST_TRAVERSE(&module_list, mod, entry) {
			/* if it's not embedded, skip it */
			if (mod->lib)
				continue;

			if (mod->flags.running)
				continue;

			add_to_load_order(mod->resource, &load_order, 0);
		}

#ifdef LOADABLE_MODULES
		/* if we are allowed to load dynamic modules, scan the directory for
		   for all available modules and add them as well */
		if ((dir = opendir(ast_config_AST_MODULE_DIR))) {
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

				add_to_load_order(dirent->d_name, &load_order, 0);
			}

			closedir(dir);
		} else {
			if (!ast_opt_quiet)
				ast_log(LOG_WARNING, "Unable to open modules directory '%s'.\n",
					ast_config_AST_MODULE_DIR);
		}
#endif
	}

	/* now scan the config for any modules we are prohibited from loading and
	   remove them from the load order */
	for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
		if (strcasecmp(v->name, "noload"))
			continue;

		AST_LIST_TRAVERSE_SAFE_BEGIN(&load_order, order, entry) {
			if (!resource_name_match(order->resource, v->value)) {
				AST_LIST_REMOVE_CURRENT(entry);
				ast_free(order->resource);
				ast_free(order);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	/* we are done with the config now, all the information we need is in the
	   load_order list */
	ast_config_destroy(cfg);

	load_count = 0;
	AST_LIST_TRAVERSE(&load_order, order, entry)
		load_count++;

	if (load_count)
		ast_log(LOG_NOTICE, "%u modules will be loaded.\n", load_count);

	/* first, load only modules that provide global symbols */
	if ((res = load_resource_list(&load_order, 1, &modulecount)) < 0) {
		goto done;
	}

	/* now load everything else */
	if ((res = load_resource_list(&load_order, 0, &modulecount)) < 0) {
		goto done;
	}

done:
	while ((order = AST_LIST_REMOVE_HEAD(&load_order, entry))) {
		ast_free(order->resource);
		ast_free(order);
	}

	AST_DLLIST_UNLOCK(&module_list);
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

int ast_update_module_list(int (*modentry)(const char *module, const char *description,
                                           int usecnt, const char *status, const char *like,
										   enum ast_module_support_level support_level),
                           const char *like)
{
	struct ast_module *cur;
	int unlock = -1;
	int total_mod_loaded = 0;
	AST_LIST_HEAD_NOLOCK(, ast_module) alpha_module_list = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

	if (AST_DLLIST_TRYLOCK(&module_list)) {
		unlock = 0;
	}

	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		AST_LIST_INSERT_SORTALPHA(&alpha_module_list, cur, list_entry, resource);
	}

	while ((cur = AST_LIST_REMOVE_HEAD(&alpha_module_list, list_entry))) {
		total_mod_loaded += modentry(cur->resource, cur->info->description, cur->usecount,
						cur->flags.running ? "Running" : "Not Running", like, cur->info->support_level);
	}

	if (unlock) {
		AST_DLLIST_UNLOCK(&module_list);
	}

	return total_mod_loaded;
}

int ast_update_module_list_data(int (*modentry)(const char *module, const char *description,
                                                int usecnt, const char *status,
                                                enum ast_module_support_level support_level,
                                                void *data),
                                void *data)
{
	struct ast_module *cur;
	int total_mod_loaded = 0;
	AST_LIST_HEAD_NOLOCK(, ast_module) alpha_module_list = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

	AST_DLLIST_LOCK(&module_list);

	AST_DLLIST_TRAVERSE(&module_list, cur, entry) {
		AST_LIST_INSERT_SORTALPHA(&alpha_module_list, cur, list_entry, resource);
	}

	while ((cur = AST_LIST_REMOVE_HEAD(&alpha_module_list, list_entry))) {
		total_mod_loaded += modentry(cur->resource, cur->info->description, cur->usecount,
		        cur->flags.running? "Running" : "Not Running", cur->info->support_level, data);
	}

	AST_DLLIST_UNLOCK(&module_list);

	return total_mod_loaded;
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
