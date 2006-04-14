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
 *
 * \brief Module Loader
 *
 * \author Mark Spencer <markster@digium.com> 
 * - See ModMngMnt
 */

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define MOD_LOADER	/* prevent some module-specific stuff from being compiled */
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/cdr.h"
#include "asterisk/enum.h"
#include "asterisk/rtp.h"
#include "asterisk/http.h"
#include "asterisk/lock.h"
#ifdef DLFCNCOMPAT
#include "asterisk/dlfcn-compat.h"
#else
#include <dlfcn.h>
#endif
#include "asterisk/md5.h"
#include "asterisk/utils.h"

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

static int modlistver = 0; /* increase whenever the list changes, to protect reload */

static unsigned char expected_key[] =
{ 0x87, 0x76, 0x79, 0x35, 0x23, 0xea, 0x3a, 0xd3,
  0x25, 0x2a, 0xbb, 0x35, 0x87, 0xe4, 0x22, 0x24 };

/*
 * Modules can be in a number of different states, as below:
 * MS_FAILED	attempt to load failed. This is final.
 * MS_NEW	just added to the list, symbols unresolved.
 * MS_RESOLVED	all symbols resolved, but supplier modules not active yet.
 * MS_CANLOAD	all symbols resolved and suppliers are all active
 *		(or we are in a cyclic dependency and we are breaking a loop)
 * MS_ACTIVE	load() returned successfully.
 */
enum st_t {  /* possible states of a module */
	MS_FAILED = 0,              /*!< cannot load */
	MS_NEW = 1,                 /*!< nothing known */
	MS_RESOLVED = 2,            /*!< all required resolved */
	MS_CANLOAD = 3,             /*!< as above, plus cyclic depend.*/
	MS_ACTIVE = 4,              /*!< all done */
};

/*! \note
 * All module symbols are in module_symbols.
 * Modules are then linked in a list of struct module,
 * whereas updaters are in a list of struct loadupdate.
 *
 * Both lists (basically, the entire loader) are protected by
 * the lock in module_list.
 *
 * A second lock, reloadlock, is used to prevent concurrent reloads
 */
struct module {
	AST_LIST_ENTRY(module) next;
	struct module_symbols *cb;
	void *lib;		/* the shared lib */
	char resource[256];

	enum st_t state;
	int export_refcount;	/* how many users of exported symbols */
};

struct loadupdate {
	AST_LIST_ENTRY(loadupdate) next;
	int (*updater)(void);
};

static AST_LIST_HEAD_STATIC(module_list, module);
static AST_LIST_HEAD_STATIC(updaters, loadupdate);
AST_MUTEX_DEFINE_STATIC(reloadlock);

/*! \note
 * helper localuser routines.
 * All of these routines are extremely expensive, so the use of
 * macros is totally unnecessary from the point of view of performance:
 * the extra function call will be totally negligible in all cases.
 */

struct localuser *ast_localuser_add(struct module_symbols *me,
	struct ast_channel *chan)
{
	struct localuser *u = ast_calloc(1, sizeof(*u));
	if (u == NULL)
		return NULL;
	u->chan = chan;
	ast_mutex_lock(&me->lock);
	u->next = me->lu_head;
	me->lu_head = u;
	ast_mutex_unlock(&me->lock);
	ast_atomic_fetchadd_int(&me->usecnt, +1);
	ast_update_use_count();
	return u;
}

void ast_localuser_remove(struct module_symbols *me, struct localuser *u)
{
	struct localuser *x, *prev = NULL;
	ast_mutex_lock(&me->lock);
	/* unlink from the list */
	for (x = me->lu_head; x; prev = x, x = x->next) {
		if (x == u) {
			if (prev)
				prev->next = x->next;
			else
				me->lu_head = x->next;
			break;
		}
	}
	ast_mutex_unlock(&me->lock);
	ast_atomic_fetchadd_int(&me->usecnt, -1);
	free(u);
	ast_update_use_count();
}

void ast_hangup_localusers(struct module_symbols *me)
{
	struct localuser *u, *next;
	ast_mutex_lock(&me->lock);
	for (u = me->lu_head; u; u = next) {
		next = u->next;
		ast_softhangup(u->chan, AST_SOFTHANGUP_APPUNLOAD);
		ast_atomic_fetchadd_int(&me->usecnt, -1);
		free(u);
	}
	ast_mutex_unlock(&me->lock);
        ast_update_use_count();
}

/*--- new-style loader routines ---*/

/*
 * For backward compatibility, we have 3 types of loadable modules:
 *
 * MOD_0 these are the 'old style' modules, which export a number
 *       of callbacks, and their full interface, as globally visible
 *       symbols. The module needs to be loaded with RTLD_LAZY and
 *       RTLD_GLOBAL to make symbols visible to other modules, and
 *       to avoid load failures due to cross dependencies.
 *
 * MOD_1 The generic callbacks are all into a structure, mod_data.
 *
 * MOD_2 this is the 'new style' format for modules. The module must
 *       explictly declare which simbols are exported and which
 *       symbols from other modules are used, and the code in this
 *       loader will implement appropriate checks to load the modules
 *       in the correct order. Also this allows to load modules
 *       with RTLD_NOW and RTLD_LOCAL so there is no chance of run-time
 *       bugs due to unresolved symbols or name conflicts.
 */

/*
 * helper routine to print the symbolic name associated to a state
 */
static const char *st_name(enum st_t state)
{
	/* try to resolve required symbols */
	const char *st;
	switch (state) {
#define ST(x)  case x: st = # x; break;
	ST(MS_NEW);
	ST(MS_FAILED);
	ST(MS_RESOLVED);
	ST(MS_ACTIVE);
	ST(MS_CANLOAD);
	default:
		st = "unknown";
	}
	return st;
#undef ST
}

/*! \brief
 * Fetch/release an exported symbol - modify export_refcount by delta
 * \param delta 1 to fetch a symbol, -1 to release it.
 * \return on success, return symbol value.
 * \note Note, modules in MS_FAIL will never match in a 'get' request.
 * If src is non-NULL, on exit *src points to the source module.
 *
 * Must be called with the lock held.
 */
static void *module_symbol_helper(const char *name,
		int delta, struct module **src)
{
	void *ret = NULL;
	struct module *m;

	AST_LIST_TRAVERSE(&module_list, m, next) {
		struct symbol_entry *es;
		if (delta > 0 && m->state == MS_FAILED)
			continue; /* cannot 'get' a symbol from a failed module */
		for (es = m->cb->exported_symbols; ret == NULL && es && es->name; es++) {
			if (!strcmp(es->name, name)) {
				ret = es->value;
				m->export_refcount += delta;
				if (src)
					*src = m;
				break;
			}
		}
		if (ret)
			break;
	}
	if (ret == NULL)
		ast_log(LOG_WARNING, "symbol %s not found\n", name);
	return ret;
}

static void *release_module_symbol(const char *name)
{
	return module_symbol_helper(name, -1, NULL);
}

static void *get_module_symbol(const char *name, struct module **src)
{
	return module_symbol_helper(name, +1, src);
}

/*!
 * \brief Release refcounts to all imported symbols,
 * and change module state to MS_FAILED.
 */
static void release_module(struct module *m)
{
	struct symbol_entry *s;

	for (s = m->cb->required_symbols; s && s->name != NULL; s++) {
		if (s->value != NULL) {
			release_module_symbol(s->name);
			s->value = NULL;
		}
	}
	m->state = MS_FAILED;
}

/*! \brief check that no NULL symbols are exported  - the algorithms rely on that. */
static int check_exported(struct module *m)
{
	struct symbol_entry *es = m->cb->exported_symbols;
	int errors = 0;

	if (es == NULL)
		return 0;
	ast_log(LOG_WARNING, "module %s exports the following symbols\n",
		es->name);
	for (; es->name; es++) {
		void **p = es->value;
		int i;

		ast_log(LOG_WARNING, "\taddr %p size %8d %s\n",
			es->value, es->size, es->name);
		for (i = 0; i <  es->size / sizeof(void *); i++, p++) {
			if (*p == NULL) {
				ast_log(LOG_WARNING, "\t *** null field at offset %d\n", i);
					errors++;
			}
		}
	}
	return errors;
}

/*!
 * \brief Resolve symbols and change state accordingly.
 * \return Return 1 if state changed, 0 otherwise.
 * \note If MS_FAILED, MS_ACTIVE or MS_CANLOAD there is nothing to do.
 * If a symbol cannot be resolved (no supplier or supplier in MS_FAIL),
 * move to MS_FAIL and release all symbols;
 * If all suppliers are MS_ACTIVE, move to MS_CANLOAD
 * otherwise move to MS_RESOLVED.
 */
static int resolve(struct module *m)
{
	struct symbol_entry *s;

	if (m->state == MS_FAILED || m->state == MS_ACTIVE || m->state == MS_CANLOAD)
		return 0;	/* already decided what to do */
	/* now it's either MS_NEW or MS_RESOLVED.
	 * Be optimistic and put it in MS_CANLOAD, then try to
	 * resolve and verify symbols, and downgrade as appropriate.
	 */
	m->state = MS_CANLOAD;
	for (s = m->cb->required_symbols; s && s->name != NULL; s++) {
		void **p = (void **)(s->value);

		if (*p == NULL)		/* symbol not resolved yet */
			*p = get_module_symbol(s->name, &s->src);
		if (*p == NULL || s->src->state == MS_FAILED) {        /* fail */
			ast_log(LOG_WARNING,
				"Unresolved symbol %s for module %s\n",
				s->name, m->resource);
			release_module(m); /* and set to MS_FAILED */
                        break;
		}
		if (s->src->state != MS_ACTIVE)
			m->state = MS_RESOLVED; /* downgrade */
	}
	return 1;
}

/*!
 * \brief Fixup references and load modules according to their dependency order.
 * Called when new modules are added to the list.
 * The algorithm is as follows:
 * - all modules MS_FAILED are changed to MS_NEW, in case something
 *      happened that could help them.
 * - first try to resolve symbols. If successful, change the
 *   module's state to MS_RESOLVED otherwise to MS_FAILED
 * - repeat on all modules until there is progress:
 *    - if it is MS_ACTIVE or MS_FAILED, continue (no progress)
 *    - if one has all required modules in MS_ACTIVE, try to load it.
 *      If successful it becomes MS_ACTIVE itself, otherwise
 *             MS_FAILED and releases all symbols.
 *             In any case, we have progress.
 *    - if one of the dependencies is MS_FAILED, release and set to
 *      MS_FAILED here too. We have progress.
 * - if we have no progress there is a cyclic dependency.
 *      Take first and change to MS_CANLOAD, i.e. as if all required are
 *      MS_ACTIVE. we have progress, so repeat.
 * \par NOTE:
 *   - must be called with lock held
 *   - recursive calls simply return success.
 */
static int fixup(const char *caller)
{
	struct module *m;
	int total = 0, new = 0, cycle = 0;
	static int in_fixup = 0;        /* disable recursive calls */

	if (in_fixup)
		return 0;
	in_fixup++;
	AST_LIST_TRAVERSE(&module_list, m, next) {
		total++;
		if (m->state == MS_FAILED)
			m->state = MS_NEW;
		if (m->state == MS_NEW)
			new++;
		/* print some debugging info for new modules */
		if (m->state == MS_NEW &&
		    (m->cb->exported_symbols || m->cb->required_symbols))
			ast_log(LOG_NOTICE,
			    "module %-30s exports %p requires %p state %s(%d)\n",
				m->resource, m->cb->exported_symbols,
				m->cb->required_symbols,
				st_name(m->state), m->state);
	}
	ast_log(LOG_DEBUG, "---- fixup (%s): %d modules, %d new ---\n",
		caller, total, new);
	for (;;cycle++) {
		int again = 0;	/* set if we need another round */
		
		ast_log(LOG_DEBUG, "---- fixup: cycle %d ---\n", cycle);
		AST_LIST_TRAVERSE(&module_list, m, next) {
			if (resolve(m))
				again = 1;	/* something changed */
			if (m->state != MS_CANLOAD)	/* for now, done with this module */
				continue;
			/* try to run the load routine */
			if (m->cb->load_module(m)) { /* error */
				ast_log(LOG_WARNING, "load_module %s fail\n",
					m->resource);
				release_module(m); /* and set to MS_FAIL */
			} else {
				ast_log(LOG_WARNING, "load_module %s success\n",
					m->resource);
				m->state = MS_ACTIVE;
			}
			again = 1;	/* something has changed */
		}
		/* Modules in MS_RESOLVED mean a possible cyclic dependency.
		 * Break the indecision by setting one to CANLOAD, and repeat.
		 */
		AST_LIST_TRAVERSE(&module_list, m, next) {
			if (m->state == MS_RESOLVED) {
				m->state = MS_CANLOAD;
				again = 1;
				break;
			}
		}
		if (!again)	/* we are done */
			break;
	}
	ast_log(LOG_DEBUG, "---- fixup complete ---\n");
	in_fixup--;
	return 0;
}

/* test routines to see which modules depend on global symbols
 * exported by other modules.
 */
static void check_symbols(void)
{
	struct dirent *d;
	DIR *mods = opendir(ast_config_AST_MODULE_DIR);
	void *lib;
	char buf[1024];

	ast_log(LOG_WARNING, "module dir <%s>\n", ast_config_AST_MODULE_DIR);
	if (!mods)
		return;
	while((d = readdir(mods))) {
		int ld = strlen(d->d_name);
		/* Must end in .so to load it.  */
		if (ld <= 3 || strcasecmp(d->d_name + ld - 3, ".so"))
			continue;
		snprintf(buf, sizeof(buf), "%s/%s", ast_config_AST_MODULE_DIR, d->d_name);
		lib = dlopen(buf, RTLD_NOW | RTLD_LOCAL);
		if (lib == NULL) {
			ast_log(LOG_WARNING, "(notice only) module %s error %s\n", d->d_name, dlerror());
		}
		dlclose(lib);
	}
}
/*--- end new-style routines ---*/

/*! \note
 * In addition to modules, the reload command handles some extra keywords
 * which are listed here together with the corresponding handlers.
 * This table is also used by the command completion code.
 */
static struct reload_classes_t {
	const char *name;
	int (*reload_fn)(void);
} reload_classes[] = {	/* list in alpha order, longest match first */
	{ "cdr",	ast_cdr_engine_reload },
	{ "dnsmgr",	dnsmgr_reload },
	{ "extconfig",	read_config_maps },
	{ "enum",	ast_enum_reload },
	{ "manager",	reload_manager },
	{ "rtp",	ast_rtp_reload },
	{ "http",	ast_http_reload },
	{ NULL, NULL }
};

static int printdigest(const unsigned char *d)
{
	int x, pos;
	char buf[256]; /* large enough so we don't have to worry */

	for (pos = 0, x=0; x<16; x++)
		pos += sprintf(buf + pos, " %02x", *d++);
	ast_log(LOG_DEBUG, "Unexpected signature:%s\n", buf);
	return 0;
}

static int key_matches(const unsigned char *key1, const unsigned char *key2)
{
	int x;
	for (x=0; x<16; x++) {
		if (key1[x] != key2[x])	/* mismatch - fail now. */
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

int ast_unload_resource(const char *resource_name, enum unload_mode force)
{
	struct module *cur;
	int res = -1;
	int error = 0;
	if (AST_LIST_LOCK(&module_list)) /* XXX should fail here ? */
		ast_log(LOG_WARNING, "Failed to lock\n");
	AST_LIST_TRAVERSE_SAFE_BEGIN(&module_list, cur, next) {
		struct module_symbols *m = cur->cb;
		
		if (strcasecmp(cur->resource, resource_name))	/* not us */
			continue;
		if (m->usecnt > 0 || m->flags & NO_UNLOAD)  {
			if (force) 
				ast_log(LOG_WARNING, "Warning:  Forcing removal of module %s with use count %d\n", resource_name, res);
			else {
				ast_log(LOG_WARNING, "Soft unload failed, '%s' has use count %d\n", resource_name, res);
				error = 1;
				break;
			}
		}
		ast_hangup_localusers(m);
		res = m->unload_module(m);
		if (res) {
			ast_log(LOG_WARNING, "Firm unload failed for %s\n", resource_name);
			if (force <= AST_FORCE_FIRM) {
				error = 1;
				break;
			} else
				ast_log(LOG_WARNING, "** Dangerous **: Unloading resource anyway, at user request\n");
		}
		release_module(cur);	/* XXX */
		AST_LIST_REMOVE_CURRENT(&module_list, next);
		dlclose(cur->lib);
		free(cur);
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;
	if (!error)
		modlistver++;
	AST_LIST_UNLOCK(&module_list);
	if (!error)	/* XXX maybe within the lock ? */
		ast_update_use_count();
	return res;
}

char *ast_module_helper(const char *line, const char *word, int pos, int state, int rpos, int needsreload)
{
	struct module *cur;
	int i, which=0, l = strlen(word);
	char *ret = NULL;

	if (pos != rpos)
		return NULL;
	AST_LIST_LOCK(&module_list);
	AST_LIST_TRAVERSE(&module_list, cur, next) {
		if (!strncasecmp(word, cur->resource, l) && (cur->cb->reload || !needsreload) &&
				++which > state) {
			ret = strdup(cur->resource);
			break;
		}
	}
	AST_LIST_UNLOCK(&module_list);
	if (!ret) {
		for (i=0; !ret && reload_classes[i].name; i++) {
			if (!strncasecmp(word, reload_classes[i].name, l) && ++which > state)
				ret = strdup(reload_classes[i].name);
		}
	}
	return ret;
}

int ast_module_reload(const char *name)
{
	struct module *cur;
	int res = 0; /* return value. 0 = not found, others, see below */
	int i, oldversion;

	if (ast_mutex_trylock(&reloadlock)) {
		ast_verbose("The previous reload command didn't finish yet\n");
		return -1;	/* reload already in progress */
	}
	/* Call "predefined" reload here first */
	for (i = 0; reload_classes[i].name; i++) {
		if (!name || !strcasecmp(name, reload_classes[i].name)) {
			reload_classes[i].reload_fn();	/* XXX should check error ? */
			res = 2;	/* found and reloaded */
		}
	}
	ast_lastreloadtime = time(NULL);

	AST_LIST_LOCK(&module_list);
	oldversion = modlistver;
	AST_LIST_TRAVERSE(&module_list, cur, next) {
		struct module_symbols *m = cur->cb;
		if (name && strcasecmp(name, cur->resource))	/* not ours */
			continue;
		if (!m->reload) {	/* cannot be reloaded */
			if (res < 1)	/* store result if possible */
				res = 1;	/* 1 = no reload() method */
			continue;
		}
		/* drop the lock and try a reload. if successful, break */
		AST_LIST_UNLOCK(&module_list);
		res = 2;
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Reloading module '%s' (%s)\n", cur->resource, m->description());
		m->reload(m);
		AST_LIST_LOCK(&module_list);
		if (oldversion != modlistver) /* something changed, abort */
			break;
	}
	AST_LIST_UNLOCK(&module_list);
	ast_mutex_unlock(&reloadlock);
	return res;
}

static int resource_exists(const char *resource, int do_lock)
{
	struct module *cur;
	if (do_lock && AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock\n");
	AST_LIST_TRAVERSE(&module_list, cur, next) {
		if (!strcasecmp(resource, cur->resource))
			break;
	}
	if (do_lock)
		AST_LIST_UNLOCK(&module_list);
	return cur ? -1 : 0;
}

/* lookup a symbol with or without leading '_', accept either form in input */
static void *find_symbol(struct module *m, const char *name, int verbose)
{
	char *n1;
	void *s;

	if (name[0] == '_')
		name++;
	if (!(n1 = alloca(strlen(name) + 2))) /* room for leading '_' and final '\0' */
		return NULL;
	n1[0] = '_';
	strcpy(n1+1, name);
	s = dlsym(m->lib, n1+1);	/* try without '_' */
	if (s == NULL)
		s = dlsym(m->lib, n1);
	if (verbose && s == NULL)
		ast_log(LOG_WARNING, "No symbol '%s' in module '%s\n",
			n1, m->resource);
	return s;
}

/* XXX cfg is only used for !res_* and #ifdef RTLD_GLOBAL */
static struct module * __load_resource(const char *resource_name,
	const struct ast_config *cfg)
{
	static char fn[256];
	int errors=0;
	int res;
	struct module *cur;
	struct module_symbols *m = NULL;
	int flags = RTLD_NOW;
	unsigned char *key;
	char tmp[80];

#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL	0	/* so it is a No-op */
#endif
	if (strncasecmp(resource_name, "res_", 4) && cfg) {
		char *val = ast_variable_retrieve(cfg, "global", resource_name);
		if (val && ast_true(val))
			flags |= RTLD_GLOBAL;
	} else {
		/* Resource modules are always loaded global and lazy */
		flags = (RTLD_GLOBAL | RTLD_LAZY);
	}
	
	if (AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock\n");
	if (resource_exists(resource_name, 0)) {
		ast_log(LOG_WARNING, "Module '%s' already exists\n", resource_name);
		AST_LIST_UNLOCK(&module_list);
		return NULL;
	}
	if (!(cur = ast_calloc(1, sizeof(*cur)))) {
		AST_LIST_UNLOCK(&module_list);
		return NULL;
	}
	ast_copy_string(cur->resource, resource_name, sizeof(cur->resource));
	if (resource_name[0] == '/')
		ast_copy_string(fn, resource_name, sizeof(fn));
	else
		snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_MODULE_DIR, resource_name);

	/* open in a sane way */
	cur->lib = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (cur->lib) {
		if ((m = find_symbol(cur, "mod_data", 0)) == NULL ||
			(m->flags & MOD_MASK) == MOD_0) {
		/* old-style module, close and reload with standard flags */
			dlclose(cur->lib);
			cur->lib = NULL;
		}
		m = NULL;
	}
	if (cur->lib == NULL)	/* try reopen with the old style */
		cur->lib = dlopen(fn, flags);

	if (!cur->lib) {
		ast_log(LOG_WARNING, "%s\n", dlerror());
		free(cur);
		AST_LIST_UNLOCK(&module_list);
		return NULL;
	}
	if (m == NULL)	/* MOD_0 modules may still have a mod_data entry */
		m = find_symbol(cur, "mod_data", 0);
	if (m != NULL) {	/* new style module */
		ast_log(LOG_WARNING, "new style %s (0x%x) loaded RTLD_LOCAL\n",
			resource_name, m->flags);
		cur->cb = m;	/* use the mod_data from the module itself */
		errors = check_exported(cur);
	} else {
		ast_log(LOG_WARNING, "misstng mod_data for %s\n",
			resource_name);
		errors++;
	}
	if (!m->load_module)
		errors++;
	if (!m->unload_module && !(m->flags & NO_UNLOAD) )
		errors++;
	if (!m->description)
		errors++;
	if (!m->key)
		errors++;
	if (!m->key || !(key = (unsigned char *) m->key())) {
		ast_log(LOG_WARNING, "Key routine returned NULL in module %s\n", fn);
		key = NULL;
		errors++;
	}
	if (key && verify_key(key)) {
		ast_log(LOG_WARNING, "Unexpected key returned by module %s\n", fn);
		errors++;
	}
	if (errors) {
		ast_log(LOG_WARNING, "%d error%s loading module %s, aborted\n", errors, (errors != 1) ? "s" : "", fn);
		dlclose(cur->lib);
		free(cur);
		AST_LIST_UNLOCK(&module_list);
		return NULL;
	}
	/* init mutex and usecount */
	ast_mutex_init(&cur->cb->lock);
	cur->cb->lu_head = NULL;

	if (!ast_fully_booted) {
		if (option_verbose) 
			ast_verbose( " => (%s)\n", term_color(tmp, m->description(), COLOR_BROWN, COLOR_BLACK, sizeof(tmp)));
		if (ast_opt_console && !option_verbose)
			ast_verbose( ".");
	} else {
		if (option_verbose)
			ast_verbose(VERBOSE_PREFIX_1 "Loaded %s => (%s)\n", fn, m->description());
	}

	AST_LIST_INSERT_TAIL(&module_list, cur, next);
	/* add module to end of module_list chain
  	   so reload commands will be issued in same order modules were loaded */
	
	modlistver++;
	if ( (m->flags & MOD_MASK) == MOD_2) {
		ast_log(LOG_WARNING, "new-style module %s, deferring load()\n",
			resource_name);
		cur->state = MS_NEW;
	} else
		cur->state = MS_CANLOAD;
	/* XXX make sure the usecount is 1 before releasing the lock */
	AST_LIST_UNLOCK(&module_list);
	
	if (cur->state == MS_CANLOAD && (res = m->load_module(m))) {
		ast_log(LOG_WARNING, "%s: load_module failed, returning %d\n", resource_name, res);
		ast_unload_resource(resource_name, 0);
		return NULL;
	}
	cur->state = MS_ACTIVE;
	ast_update_use_count();
	return cur;
}

/*!
 * \brief load a single module (API call).
 * (recursive calls from load_module() succeed.
 * \return Returns 0 on success, -1 on error.
 */
int ast_load_resource(const char *resource_name)
{
	int o = option_verbose;
	struct ast_config *cfg = NULL;
	struct module *m;

	option_verbose = 0;	/* Keep the module file parsing silent */
	cfg = ast_config_load(AST_MODULE_CONFIG);
	option_verbose = o;	/* restore verbosity */
	m = __load_resource(resource_name, cfg);
	if (cfg)
		ast_config_destroy(cfg);
	return m ? 0 : -1;
}	

#if 0
/*
 * load a single module (API call).
 * (recursive calls from load_module() succeed.
 */
int ast_load_resource(const char *resource_name)
{
       struct module *m;
       int ret;

       ast_mutex_lock(&modlock);
       m = __load_resource(resource_name, 0);
       fixup(resource_name);
       ret = (m->state == MS_FAILED) ? -1 : 0;
       ast_mutex_unlock(&modlock);
       return ret;
}
#endif

/*! \brief if enabled, log and output on console the module's name, and try load it */
static int print_and_load(const char *s, struct ast_config *cfg)
{
	char tmp[80];

	if (option_debug && !option_verbose)
		ast_log(LOG_DEBUG, "Loading module %s\n", s);
	if (option_verbose) {
		ast_verbose(VERBOSE_PREFIX_1 "[%s]",
			term_color(tmp, s, COLOR_BRWHITE, 0, sizeof(tmp)));
		fflush(stdout);
	}
	if (__load_resource(s, cfg))
		return 0; /* success */
	ast_log(LOG_WARNING, "Loading module %s failed!\n", s);
	return -1;
}

static const char *loadorder[] =
{
	"res_",
	"pbx_",
	"chan_",
	NULL,
};

int load_modules(const int preload_only)
{
	struct ast_config *cfg;
	int x;

	if (option_verbose) {
		ast_verbose(preload_only ?
			"Asterisk Dynamic Loader loading preload modules:\n" :
			"Asterisk Dynamic Loader Starting:\n");
	}

	if (0)
		check_symbols();

	cfg = ast_config_load(AST_MODULE_CONFIG);

	if (cfg) {
		const char *cmd = preload_only ? "preload" : "load";
		struct ast_variable *v;
		/* Load explicitly defined modules */
		for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
			if (strcasecmp(v->name, cmd)) /* not what we are looking for */
				continue;
			if (print_and_load(v->value, cfg)) {	/* XXX really fatal ? */
				ast_config_destroy(cfg);
				return -1;
			}
		}
	}

	if (preload_only)
		goto done;

	if (cfg && !ast_true(ast_variable_retrieve(cfg, "modules", "autoload")))
		/* no autoload */
		goto done;
	/*
	 * Load all modules. To help resolving dependencies, we load modules
	 * in the order defined by loadorder[], with the final step for
	 * all modules with other prefixes.
	 * (XXX the new loader does not need this).
	 */

	for (x=0; x<sizeof(loadorder) / sizeof(loadorder[0]); x++) {
		struct dirent *d;
		DIR *mods = opendir(ast_config_AST_MODULE_DIR);
		const char *base = loadorder[x];
		int lx = base ? strlen(base) : 0;

		if (!mods) {
			if (!ast_opt_quiet)
				ast_log(LOG_WARNING, "Unable to open modules directory %s.\n",
					ast_config_AST_MODULE_DIR);
			break; /* suffices to try once! */
		}
		while((d = readdir(mods))) {
			int ld = strlen(d->d_name);
			/* Must end in .so to load it.  */
			if (ld > 3 && (!base || !strncasecmp(d->d_name, base, lx)) && 
					!strcasecmp(d->d_name + ld - 3, ".so") &&
					!resource_exists(d->d_name, 1)) {
				/* It's a shared library, check if we are allowed to load it
				 * (very inefficient, but oh well).
				 */
				if (cfg) {
					struct ast_variable *v;
					for (v = ast_variable_browse(cfg, "modules"); v; v = v->next) {
						if (!strcasecmp(v->name, "noload") &&
								!strcasecmp(v->value, d->d_name)) 
							break;
					}
					if (v) {
						if (option_verbose) {
							ast_verbose( VERBOSE_PREFIX_1 "[skipping %s]\n",
									d->d_name);
							fflush(stdout);
						}
						continue;
					}
					
				}
				if (print_and_load(d->d_name, cfg)) {
					ast_config_destroy(cfg);
					return -1;
				}
			}
		}
		closedir(mods);
	}
done:
	fixup("load_modules");
	ast_config_destroy(cfg);
	return 0;
}

#include <errno.h>	/* for errno... */

void ast_update_use_count(void)
{
	/* Notify any module monitors that the use count for a 
	   resource has changed */
	struct loadupdate *m;
	if (AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock, errno %d\n", errno);
	AST_LIST_TRAVERSE(&updaters, m, next)
		m->updater();
	AST_LIST_UNLOCK(&module_list);
}

int ast_update_module_list(int (*modentry)(const char *module, const char *description, int usecnt, const char *like),
			   const char *like)
{
	struct module *cur;
	int unlock = -1;
	int total_mod_loaded = 0;

	if (ast_mutex_trylock(&module_list.lock))
		unlock = 0;
	AST_LIST_TRAVERSE(&module_list, cur, next) {
		total_mod_loaded += modentry(cur->resource, cur->cb->description(), cur->cb->usecnt, like);
	}
	if (unlock)
		AST_LIST_UNLOCK(&module_list);

	return total_mod_loaded;
}

int ast_loader_register(int (*v)(void)) 
{
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	struct loadupdate *tmp;	
	if (!(tmp = ast_malloc(sizeof(*tmp))))
		return -1;
	tmp->updater = v;
	if (AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock\n");
	AST_LIST_INSERT_HEAD(&updaters, tmp, next);
	AST_LIST_UNLOCK(&module_list);
	return 0;
}

int ast_loader_unregister(int (*v)(void))
{
	struct loadupdate *cur;

	if (AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock\n");
	AST_LIST_TRAVERSE_SAFE_BEGIN(&updaters, cur, next) {
		if (cur->updater == v)	{
			AST_LIST_REMOVE_CURRENT(&updaters, next);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&module_list);
	return cur ? 0 : -1;
}
