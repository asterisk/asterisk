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
#include "asterisk/acl.h"
#include "asterisk/manager.h"
#include "asterisk/cdr.h"
#include "asterisk/enum.h"
#include "asterisk/http.h"
#include "asterisk/lock.h"
#include "asterisk/features_config.h"
#include "asterisk/dsp.h"
#include "asterisk/udptl.h"
#include "asterisk/vector.h"
#include "asterisk/app.h"
#include "asterisk/test.h"
#include "asterisk/sounds_index.h"
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
	struct {
		/*! The module running and ready to accept requests. */
		unsigned int running:1;
		/*! The module has declined to start. */
		unsigned int declined:1;
		/*! This module is being held open until it's time to shutdown. */
		unsigned int keepuntilshutdown:1;
	} flags;
	AST_DLLIST_ENTRY(ast_module) entry;
	char resource[0];
};

static AST_DLLIST_HEAD_STATIC(module_list, ast_module);

static int module_vector_strcasecmp(struct ast_module *a, struct ast_module *b)
{
	return strcasecmp(a->resource, b->resource);
}

static int module_vector_cmp(struct ast_module *a, struct ast_module *b)
{
	/* if load_pri is not set, default is 128.  Lower is better */
	int a_pri = ast_test_flag(a->info, AST_MODFLAG_LOAD_ORDER)
		? a->info->load_pri : AST_MODPRI_DEFAULT;
	int b_pri = ast_test_flag(b->info, AST_MODFLAG_LOAD_ORDER)
		? b->info->load_pri : AST_MODPRI_DEFAULT;

	/*
	 * Returns comparison values for a vector sorted by priority.
	 * <0 a_pri < b_pri
	 * =0 a_pri == b_pri
	 * >0 a_pri > b_pri
	 */
	return a_pri - b_pri;
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

	AST_DLLIST_INSERT_TAIL(&module_list, mod, entry);
	AST_DLLIST_UNLOCK(&module_list);

	/* give the module a copy of its own handle, for later use in registrations and the like */
	*((struct ast_module **) &(info->self)) = mod;
}

static void module_destroy(struct ast_module *mod)
{
	AST_LIST_HEAD_DESTROY(&mod->users);
	ao2_cleanup(mod->ref_debug);
	ast_free(mod);
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

#define MODULE_LOCAL_ONLY (void *)-1

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
	if (resource_being_loaded) {
		resource_being_loaded = NULL;
		if (mod->lib) {
			ast_log(LOG_ERROR, "Module '%s' did not register itself during load\n", resource_in);
			logged_dlclose(resource_in, mod->lib);
		} else if (!suppress_logging) {
			ast_log(LOG_WARNING, "Error loading module '%s': %s\n", resource_in, dlerror());
		}
		ast_free(mod);

		return NULL;
	}

	return mod;
}

static struct ast_module *load_dynamic_module(const char *resource_in, unsigned int global_symbols_only, unsigned int suppress_logging)
{
	char fn[PATH_MAX];
	struct ast_module *mod;
	size_t resource_in_len = strlen(resource_in);
	int exports_globals;
	const char *so_ext = "";

	if (resource_in_len < 4 || strcasecmp(resource_in + resource_in_len - 3, ".so")) {
		so_ext = ".so";
	}

	snprintf(fn, sizeof(fn), "%s/%s%s", ast_config_AST_MODULE_DIR, resource_in, so_ext);

	/* Try loading in quiet mode first with flags to export global symbols.
	 * If the module does not want to export globals we will close and reopen. */
	mod = load_dlopen(resource_in, so_ext, fn,
		global_symbols_only ? RTLD_LAZY | RTLD_GLOBAL : RTLD_NOW | RTLD_LOCAL,
		suppress_logging);

	if (!mod) {
		return NULL;
	}

	exports_globals = ast_test_flag(mod->info, AST_MODFLAG_GLOBAL_SYMBOLS);
	if ((global_symbols_only && exports_globals) || (!global_symbols_only && !exports_globals)) {
		/* The first dlopen had the correct flags. */
		return mod;
	}

	/* Close the module so we can reopen with correct flags. */
	logged_dlclose(resource_in, mod->lib);
	if (global_symbols_only) {
		return MODULE_LOCAL_ONLY;
	}

	return load_dlopen(resource_in, so_ext, fn,
		exports_globals ? RTLD_LAZY | RTLD_GLOBAL : RTLD_NOW | RTLD_LOCAL,
		0);
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

char *ast_module_helper(const char *line, const char *word, int pos, int state, int rpos, int _type)
{
	enum ast_module_helper_type type = _type;
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

	if (type == AST_MODULE_HELPER_RELOAD) {
		int idx;

		for (idx = 0; reload_classes[idx].name; idx++) {
			if (!strncasecmp(word, reload_classes[idx].name, wordlen) && ++which > state) {
				return ast_strdup(reload_classes[idx].name);
			}
		}
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
	json_object = ast_json_pack("{s: s, s: i, s: o}",
			"type", "Reload",
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

enum ast_module_reload_result ast_module_reload(const char *name)
{
	struct ast_module *cur;
	enum ast_module_reload_result res = AST_MODULE_RELOAD_NOT_FOUND;
	int i;
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

	/* Call "predefined" reload here first */
	for (i = 0; reload_classes[i].name; i++) {
		if (!name || !strcasecmp(name, reload_classes[i].name)) {
			if (reload_classes[i].reload_fn() == AST_MODULE_LOAD_SUCCESS) {
				res = AST_MODULE_RELOAD_SUCCESS;
			} else if (res == AST_MODULE_RELOAD_NOT_FOUND) {
				res = AST_MODULE_RELOAD_ERROR;
			}
		}
	}

	if (name && res == AST_MODULE_RELOAD_SUCCESS) {
		if (ast_opt_lock_confdir) {
			ast_unlock_path(ast_config_AST_CONFIG_DIR);
		}
		goto module_reload_done;
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
 *  If the module_vector is provided (not NULL) the module is found and added to the
 *  vector without running the module's load() function.  By doing this, modules
 *  can be initialized later in order by priority and dependencies.
 *
 *  If the module_vector is not provided, the module's load function will be executed
 *  immediately */
static enum ast_module_load_result load_resource(const char *resource_name, unsigned int global_symbols_only, unsigned int suppress_logging, struct module_vector *resource_heap, int required)
{
	struct ast_module *mod;
	enum ast_module_load_result res = AST_MODULE_LOAD_SUCCESS;

	if ((mod = find_resource(resource_name, 0))) {
		if (mod->flags.running) {
			ast_log(LOG_WARNING, "Module '%s' already loaded and running.\n", resource_name);
			return AST_MODULE_LOAD_DECLINE;
		}
		if (global_symbols_only && !ast_test_flag(mod->info, AST_MODFLAG_GLOBAL_SYMBOLS))
			return AST_MODULE_LOAD_SKIP;
	} else {
		mod = load_dynamic_module(resource_name, global_symbols_only, suppress_logging);
		if (mod == MODULE_LOCAL_ONLY) {
				return AST_MODULE_LOAD_SKIP;
		}
		if (!mod) {
			if (!global_symbols_only) {
				ast_log(LOG_WARNING, "Module '%s' could not be loaded.\n", resource_name);
			}
			return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
		}
	}

	if (inspect_module(mod)) {
		goto prestart_error;
	}

	mod->flags.declined = 0;

	if (resource_heap) {
		if (AST_VECTOR_ADD_SORTED(resource_heap, mod, module_vector_cmp)) {
			goto prestart_error;
		}
		res = AST_MODULE_LOAD_PRIORITY;
	} else {
		res = start_resource(mod);
	}

	return res;

prestart_error:
	ast_log(LOG_WARNING, "Module '%s' could not be loaded.\n", resource_name);
	unload_dynamic_module(mod);
	return required ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_DECLINE;
}

int ast_load_resource(const char *resource_name)
{
	int res;
	AST_DLLIST_LOCK(&module_list);
	res = load_resource(resource_name, 0, 0, NULL, 0);
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
	size_t resource_baselen = resource_name_baselen(resource);

	AST_LIST_TRAVERSE(load_order, order, entry) {
		if (!resource_name_match(resource, resource_baselen, order->resource)) {
			/* Make sure we have the proper setting for the required field
			   (we might have both load= and required= lines in modules.conf) */
			order->required |= required;
			return NULL;
		}
	}

	if (!(order = ast_calloc(1, sizeof(*order))))
		return NULL;

	order->resource = ast_strdup(resource);
	if (!order->resource) {
		ast_free(order);

		return NULL;
	}
	order->required = required;
	AST_LIST_INSERT_TAIL(load_order, order, entry);

	return order;
}

AST_LIST_HEAD_NOLOCK(load_retries, load_order_entry);

/*! loads modules in order by load_pri, updates mod_count
	\return -1 on failure to load module, -2 on failure to load required module, otherwise 0
*/
static int load_resource_list(struct load_order *load_order, unsigned int global_symbols, int *mod_count)
{
	struct module_vector resource_heap;
	struct load_order_entry *order;
	struct load_retries load_retries;
	int count = 0;
	int res = 0;
	int i = 0;
#define LOAD_RETRIES 4

	AST_LIST_HEAD_INIT_NOLOCK(&load_retries);

	if (AST_VECTOR_INIT(&resource_heap, 500)) {
		ast_log(LOG_ERROR, "Failed to initialize module loader.\n");

		return -1;
	}

	/* first, add find and add modules to heap */
	AST_LIST_TRAVERSE_SAFE_BEGIN(load_order, order, entry) {
		enum ast_module_load_result lres;

		/* Suppress log messages unless this is the last pass */
		lres = load_resource(order->resource, global_symbols, 1, &resource_heap, order->required);
		ast_debug(3, "PASS 0: %-46s %d %d\n", order->resource, lres, global_symbols);
		switch (lres) {
		case AST_MODULE_LOAD_SUCCESS:
			/* We're supplying a heap so SUCCESS isn't possible but we still have to test for it. */
			break;
		case AST_MODULE_LOAD_FAILURE:
		case AST_MODULE_LOAD_DECLINE:
			/*
			 * DECLINE or FAILURE means there was an issue with dlopen or module_register
			 * which might be retryable.  LOAD_FAILURE only happens for required modules
			 * but we're still going to retry.  We need to remove the entry from the
			 * load_order list and add it to the load_retries list.
			 */
			AST_LIST_REMOVE_CURRENT(entry);
			AST_LIST_INSERT_TAIL(&load_retries, order, entry);
			break;
		case AST_MODULE_LOAD_SKIP:
			/*
			 * SKIP means that dlopen worked but global_symbols was set and this module doesn't qualify.
			 * Leave it in load_order for the next call of load_resource_list.
			 */
			break;
		case AST_MODULE_LOAD_PRIORITY:
			/* load_resource worked and the module was added to the priority vector */
			AST_LIST_REMOVE_CURRENT(entry);
			ast_free(order->resource);
			ast_free(order);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* Retry the failures until the list is empty or we reach LOAD_RETRIES */
	for (i = 0; !AST_LIST_EMPTY(&load_retries) && i < LOAD_RETRIES; i++) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&load_retries, order, entry) {
			enum ast_module_load_result lres;

			/* Suppress log messages unless this is the last pass */
			lres = load_resource(order->resource, global_symbols, (i < LOAD_RETRIES - 1), &resource_heap, order->required);
			ast_debug(3, "PASS %d %-46s %d %d\n", i + 1, order->resource, lres, global_symbols);
			switch (lres) {
			/* These are all retryable. */
			case AST_MODULE_LOAD_SUCCESS:
			case AST_MODULE_LOAD_DECLINE:
				break;
			case AST_MODULE_LOAD_FAILURE:
				/* LOAD_FAILURE only happens for required modules */
				if (i == LOAD_RETRIES - 1) {
					/* This was the last chance to load a required module*/
					ast_log(LOG_ERROR, "*** Failed to load module %s - Required\n", order->resource);
					fprintf(stderr, "*** Failed to load module %s - Required\n", order->resource);
					res =  -2;
					goto done;
				}
				break;;
			case AST_MODULE_LOAD_SKIP:
				/*
				 * SKIP means that dlopen worked but global_symbols was set and this module
				 * doesn't qualify.  Put it back in load_order for the next call of
				 * load_resource_list.
				 */
				AST_LIST_REMOVE_CURRENT(entry);
				AST_LIST_INSERT_TAIL(load_order, order, entry);
				break;
			case AST_MODULE_LOAD_PRIORITY:
				/* load_resource worked and the module was added to the priority heap */
				AST_LIST_REMOVE_CURRENT(entry);
				ast_free(order->resource);
				ast_free(order);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	/* second remove modules from heap sorted by priority */
	for (i = 0; i < AST_VECTOR_SIZE(&resource_heap); i++) {
		struct ast_module *mod = AST_VECTOR_GET(&resource_heap, i);
		enum ast_module_load_result lres;

		lres = start_resource(mod);
		ast_debug(3, "START: %-46s %d %d\n", mod->resource, lres, global_symbols);
		switch (lres) {
		case AST_MODULE_LOAD_SUCCESS:
			count++;
		case AST_MODULE_LOAD_DECLINE:
			break;
		case AST_MODULE_LOAD_FAILURE:
			ast_log(LOG_ERROR, "*** Failed to load module %s\n", mod->resource);
			res = -1;
			goto done;
		case AST_MODULE_LOAD_SKIP:
		case AST_MODULE_LOAD_PRIORITY:
			break;
		}
	}

done:

	while ((order = AST_LIST_REMOVE_HEAD(&load_retries, entry))) {
		ast_free(order->resource);
		ast_free(order);
	}

	if (mod_count) {
		*mod_count += count;
	}
	AST_VECTOR_FREE(&resource_heap);

	return res;
}

int load_modules(unsigned int preload_only)
{
	struct ast_config *cfg;
	struct load_order_entry *order;
	struct ast_variable *v;
	unsigned int load_count;
	struct load_order load_order;
	int res = 0;
	struct ast_flags config_flags = { 0 };
	int modulecount = 0;
	struct dirent *dirent;
	DIR *dir;

	ast_verb(1, "Asterisk Dynamic Loader Starting:\n");

	AST_LIST_HEAD_INIT_NOLOCK(&load_order);

	AST_DLLIST_LOCK(&module_list);

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
	}

	/* now scan the config for any modules we are prohibited from loading and
	   remove them from the load order */
	for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
		size_t baselen;

		if (strcasecmp(v->name, "noload")) {
			continue;
		}

		baselen = resource_name_baselen(v->value);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&load_order, order, entry) {
			if (!resource_name_match(v->value, baselen, order->resource)) {
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
