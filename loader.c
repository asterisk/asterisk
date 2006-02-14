/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 */

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include "asterisk/lock.h"
#ifdef DLFCNCOMPAT
#include "asterisk/dlfcn-compat.h"
#else
#include <dlfcn.h>
#endif
#include "asterisk/md5.h"

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif


static int modlistver = 0; /* increase whenever the list changes, to protect reload */

static unsigned char expected_key[] =
{ 0x8e, 0x93, 0x22, 0x83, 0xf5, 0xc3, 0xc0, 0x75,
  0xff, 0x8b, 0xa9, 0xbe, 0x7c, 0x43, 0x74, 0x63 };

/*
 * All module symbols are in module_symbols.
 * Modules are then linked in a list of struct module,
 * whereas updaters are in a list of struct loadupdate.
 *
 * Both lists (basically, the entire loader) are protected by
 * the lock in module_list.
 *
 * A second lock, reloadlock, is used to prevent concurrent reloads
 */

struct module_symbols {
	int (*load_module)(void);
	int (*unload_module)(void);
	int (*usecount)(void);
	char *(*description)(void);
	char *(*key)(void);
	int (*reload)(void);
};

struct module {
	AST_LIST_ENTRY(module) next;
	struct module_symbols cb;
	void *lib;		/* the shared lib */
	char resource[256];
};


struct loadupdate {
	AST_LIST_ENTRY(loadupdate) next;
	int (*updater)(void);
};

static AST_LIST_HEAD_STATIC(module_list, module);
static AST_LIST_HEAD_STATIC(updaters, loadupdate);
AST_MUTEX_DEFINE_STATIC(reloadlock);

/*
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

int ast_unload_resource(const char *resource_name, int force)
{
	struct module *cur;
	int res = -1;
	int error = 0;
	if (AST_LIST_LOCK(&module_list)) /* XXX should fail here ? */
		ast_log(LOG_WARNING, "Failed to lock\n");
	AST_LIST_TRAVERSE_SAFE_BEGIN(&module_list, cur, next) {
		struct module_symbols *m = &cur->cb;
		
		if (strcasecmp(cur->resource, resource_name))	/* not us */
			continue;
		if ((res = m->usecount()) > 0)  {
			if (force) 
				ast_log(LOG_WARNING, "Warning:  Forcing removal of module %s with use count %d\n", resource_name, res);
			else {
				ast_log(LOG_WARNING, "Soft unload failed, '%s' has use count %d\n", resource_name, res);
				error = 1;
				break;
			}
		}
		res = m->unload_module();
		if (res) {
			ast_log(LOG_WARNING, "Firm unload failed for %s\n", resource_name);
			if (force <= AST_FORCE_FIRM) {
				error = 1;
				break;
			} else
				ast_log(LOG_WARNING, "** Dangerous **: Unloading resource anyway, at user request\n");
		}
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
		if (!strncasecmp(word, cur->resource, l) && (cur->cb.reload || !needsreload) &&
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
	int (*reload)(void);

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
		struct module_symbols *m = &cur->cb;
		if (name && strcasecmp(name, cur->resource))	/* not ours */
			continue;
		reload = m->reload;
		if (!reload) {	/* cannot be reloaded */
			if (res < 1)	/* store result if possible */
				res = 1;	/* 1 = no reload() method */
			continue;
		}
		/* drop the lock and try a reload. if successful, break */
		AST_LIST_UNLOCK(&module_list);
		res = 2;
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Reloading module '%s' (%s)\n", cur->resource, m->description());
		reload();
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
	n1 = alloca(strlen(name)+2); /* room for leading '_' and final '\0' */
	if (n1 == NULL)
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
static int __load_resource(const char *resource_name, const struct ast_config *cfg)
{
	static char fn[256];
	int errors=0;
	int res;
	struct module *cur;
	struct module_symbols *m;
	int flags=RTLD_NOW;
	unsigned char *key;
	char tmp[80];

	if (strncasecmp(resource_name, "res_", 4)) {
#ifdef RTLD_GLOBAL
		if (cfg) {
			char *val;
			if ((val = ast_variable_retrieve(cfg, "global", resource_name))
					&& ast_true(val))
				flags |= RTLD_GLOBAL;
		}
#endif
	} else {
		/* Resource modules are always loaded global and lazy */
#ifdef RTLD_GLOBAL
		flags = (RTLD_GLOBAL | RTLD_LAZY);
#else
		flags = RTLD_LAZY;
#endif
	}
	
	if (AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock\n");
	if (resource_exists(resource_name, 0)) {
		ast_log(LOG_WARNING, "Module '%s' already exists\n", resource_name);
		AST_LIST_UNLOCK(&module_list);
		return -1;
	}
	cur = calloc(1, sizeof(struct module));	
	if (!cur) {
		ast_log(LOG_WARNING, "Out of memory\n");
		AST_LIST_UNLOCK(&module_list);
		return -1;
	}
	m = &cur->cb;
	ast_copy_string(cur->resource, resource_name, sizeof(cur->resource));
	if (resource_name[0] == '/')
		ast_copy_string(fn, resource_name, sizeof(fn));
	else
		snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_MODULE_DIR, resource_name);
	cur->lib = dlopen(fn, flags);
	if (!cur->lib) {
		ast_log(LOG_WARNING, "%s\n", dlerror());
		free(cur);
		AST_LIST_UNLOCK(&module_list);
		return -1;
	}
	m->load_module = find_symbol(cur, "load_module", 1);
	if (!m->load_module)
		errors++;
	m->unload_module = find_symbol(cur, "unload_module", 1);
	if (!m->unload_module)
		errors++;
	m->usecount = find_symbol(cur, "usecount", 1);
	if (!m->usecount)
		errors++;
	m->description = find_symbol(cur, "description", 1);
	if (!m->description)
		errors++;
	m->key = find_symbol(cur, "key", 1);
	if (!m->key)
		errors++;
	m->reload = find_symbol(cur, "reload", 0);
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
		return -1;
	}
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
	AST_LIST_UNLOCK(&module_list);
	if ((res = m->load_module())) {
		ast_log(LOG_WARNING, "%s: load_module failed, returning %d\n", resource_name, res);
		ast_unload_resource(resource_name, 0);
		return -1;
	}
	ast_update_use_count();
	return 0;
}

int ast_load_resource(const char *resource_name)
{
	int res, o = option_verbose;
	struct ast_config *cfg = NULL;

	option_verbose = 0;	/* Keep the module file parsing silent */
	cfg = ast_config_load(AST_MODULE_CONFIG);
	option_verbose = o;	/* restore verbosity */
	res = __load_resource(resource_name, cfg);
	if (cfg)
		ast_config_destroy(cfg);
	return res;
}	

/* if enabled, log and output on console the module's name, and try load it */
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
	if (!__load_resource(s, cfg))
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
	struct ast_variable *v;
	int x;

	if (option_verbose) {
		ast_verbose(preload_only ?
			"Asterisk Dynamic Loader loading preload modules:\n" :
			"Asterisk Dynamic Loader Starting:\n");
	}

	cfg = ast_config_load(AST_MODULE_CONFIG);
	if (cfg) {
		const char *cmd = preload_only ? "preload" : "load";
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

	if (preload_only) {
		ast_config_destroy(cfg);
		return 0;
	}

	if (cfg && !ast_true(ast_variable_retrieve(cfg, "modules", "autoload"))) {
		/* no autoload */
		ast_config_destroy(cfg);
		return 0;
	}
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
				 * (very inefficient, but oh well.
				 */
				if (cfg) {
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
	ast_config_destroy(cfg);
	return 0;
}

void ast_update_use_count(void)
{
	/* Notify any module monitors that the use count for a 
	   resource has changed */
	struct loadupdate *m;
	if (AST_LIST_LOCK(&module_list))
		ast_log(LOG_WARNING, "Failed to lock\n");
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
	AST_LIST_TRAVERSE(&module_list, cur, next)
		total_mod_loaded += modentry(cur->resource, cur->cb.description(), cur->cb.usecount(), like);
	if (unlock)
		AST_LIST_UNLOCK(&module_list);

	return total_mod_loaded;
}

int ast_loader_register(int (*v)(void)) 
{
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	struct loadupdate *tmp = malloc(sizeof (struct loadupdate));
	if (!tmp)
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
