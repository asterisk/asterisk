/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Module Loader
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/config_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/term.h>
#include <asterisk/manager.h>
#include <asterisk/enum.h>
#include <asterisk/rtp.h>
#include <asterisk/lock.h>
#ifdef __APPLE__
#include <asterisk/dlfcn-compat.h>
#else
#include <dlfcn.h>
#endif
#include <asterisk/md5.h>
#include "asterisk.h"
#include "astconf.h"

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

static char expected_key[] =
{ 0x8e, 0x93, 0x22, 0x83, 0xf5, 0xc3, 0xc0, 0x75,
  0xff, 0x8b, 0xa9, 0xbe, 0x7c, 0x43, 0x74, 0x63 };

struct module {
	int (*load_module)(void);
	int (*unload_module)(void);
	int (*usecount)(void);
	char *(*description)(void);
	char *(*key)(void);
	int (*reload)(void);
	void *lib;
	char resource[256];
	struct module *next;
};

static int printdigest(unsigned char *d)
{
	int x;
	char buf[256];
	char buf2[16];
	snprintf(buf, sizeof(buf), "Unexpected signature:");
	for (x=0;x<16;x++) {
		snprintf(buf2, sizeof(buf2), " %02x", *(d++));
		strcat(buf, buf2);
	}
	strcat(buf, "\n");
	ast_log(LOG_DEBUG, buf);
	return 0;
}

static int key_matches(char *key1, char *key2)
{
	int match = 1;
	int x;
	for (x=0;x<16;x++) {
		match &= (key1[x] == key2[x]);
	}
	return match;
}

static int verify_key(char *key)
{
	struct MD5Context c;
	char digest[16];
	MD5Init(&c);
	MD5Update(&c, key, strlen(key));
	MD5Final(digest, &c);
	if (key_matches(expected_key, digest))
		return 0;
	printdigest(digest);
	return -1;
}

static struct loadupdate {
	int (*updater)(void);
	struct loadupdate *next;
} *updaters = NULL;

AST_MUTEX_DEFINE_STATIC(modlock);
AST_MUTEX_DEFINE_STATIC(reloadlock);

static struct module *module_list=NULL;

int ast_unload_resource(char *resource_name, int force)
{
	struct module *m, *ml = NULL;
	int res = -1;
	if (ast_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = module_list;
	while(m) {
		if (!strcasecmp(m->resource, resource_name)) {
			if ((res = m->usecount()) > 0)  {
				if (force) 
					ast_log(LOG_WARNING, "Warning:  Forcing removal of module %s with use count %d\n", resource_name, res);
				else {
					ast_log(LOG_WARNING, "Soft unload failed, '%s' has use count %d\n", resource_name, res);
					ast_mutex_unlock(&modlock);
					return -1;
				}
			}
			res = m->unload_module();
			if (res) {
				ast_log(LOG_WARNING, "Firm unload failed for %s\n", resource_name);
				if (force <= AST_FORCE_FIRM) {
					ast_mutex_unlock(&modlock);
					return -1;
				} else
					ast_log(LOG_WARNING, "** Dangerous **: Unloading resource anyway, at user request\n");
			}
			if (ml)
				ml->next = m->next;
			else
				module_list = m->next;
			dlclose(m->lib);
			free(m);
			break;
		}
		ml = m;
		m = m->next;
	}
	ast_mutex_unlock(&modlock);
	ast_update_use_count();
	return res;
}

void ast_module_reload(const char *name)
{
	struct module *m;

	/* We'll do the logger and manager the favor of calling its reload here first */

	if (ast_mutex_trylock(&reloadlock)) {
		ast_verbose("The previous reload command didn't finish yet\n");
		return;
	}
	read_ast_cust_config();
	reload_manager();
	ast_enum_reload();
	ast_rtp_reload();
	time(&ast_lastreloadtime);

	ast_mutex_lock(&modlock);
	m = module_list;
	while(m) {
		if (!name || !strcasecmp(name, m->resource)) {
			if (m->reload) {
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "Reloading module '%s' (%s)\n", m->resource, m->description());
				m->reload();
			}
		}
		m = m->next;
	}
	ast_mutex_unlock(&modlock);
	ast_mutex_unlock(&reloadlock);
}

int ast_load_resource(char *resource_name)
{
	static char fn[256];
	int errors=0;
	int res;
	struct module *m;
	int flags=RTLD_NOW;
#ifdef RTLD_GLOBAL
	char *val;
#endif
	char *key;
	int o;
	struct ast_config *cfg;
	char tmp[80];
	/* Keep the module file parsing silent */
	o = option_verbose;
	if (strncasecmp(resource_name, "res_", 4)) {
		option_verbose = 0;
		cfg = ast_load(AST_MODULE_CONFIG);
		option_verbose = o;
		if (cfg) {
#ifdef RTLD_GLOBAL
			if ((val = ast_variable_retrieve(cfg, "global", resource_name))
					&& ast_true(val))
				flags |= RTLD_GLOBAL;
#endif
			ast_destroy(cfg);
		}
	} else {
		/* Resource modules are always loaded global and lazy */
#ifdef RTLD_GLOBAL
		flags = (RTLD_GLOBAL | RTLD_LAZY);
#else
		flags = RTLD_LAZY;
#endif
	}
	
	if (ast_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = module_list;
	while(m) {
		if (!strcasecmp(m->resource, resource_name)) {
			ast_log(LOG_WARNING, "Module '%s' already exists\n", resource_name);
			ast_mutex_unlock(&modlock);
			return -1;
		}
		m = m->next;
	}
	m = malloc(sizeof(struct module));	
	if (!m) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_mutex_unlock(&modlock);
		return -1;
	}
	strncpy(m->resource, resource_name, sizeof(m->resource)-1);
	if (resource_name[0] == '/') {
		strncpy(fn, resource_name, sizeof(fn)-1);
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", (char *)ast_config_AST_MODULE_DIR, resource_name);
	}
	m->lib = dlopen(fn, flags);
	if (!m->lib) {
		ast_log(LOG_WARNING, "%s\n", dlerror());
		free(m);
		ast_mutex_unlock(&modlock);
		return -1;
	}
	m->load_module = dlsym(m->lib, "load_module");
	if (m->load_module == NULL)
		m->load_module = dlsym(m->lib, "_load_module");
	if (!m->load_module) {
		ast_log(LOG_WARNING, "No load_module in module %s\n", fn);
		errors++;
	}
	m->unload_module = dlsym(m->lib, "unload_module");
	if (m->unload_module == NULL)
		m->unload_module = dlsym(m->lib, "_unload_module");
	if (!m->unload_module) {
		ast_log(LOG_WARNING, "No unload_module in module %s\n", fn);
		errors++;
	}
	m->usecount = dlsym(m->lib, "usecount");
	if (m->usecount == NULL)
		m->usecount = dlsym(m->lib, "_usecount");
	if (!m->usecount) {
		ast_log(LOG_WARNING, "No usecount in module %s\n", fn);
		errors++;
	}
	m->description = dlsym(m->lib, "description");
	if (m->description == NULL)
		m->description = dlsym(m->lib, "_description");
	if (!m->description) {
		ast_log(LOG_WARNING, "No description in module %s\n", fn);
		errors++;
	}
	m->key = dlsym(m->lib, "key");
	if (m->key == NULL)
		m->key = dlsym(m->lib, "_key");
	if (!m->key) {
		ast_log(LOG_WARNING, "No key routine in module %s\n", fn);
		errors++;
	}
	m->reload = dlsym(m->lib, "reload");
	if (m->reload == NULL)
		m->reload = dlsym(m->lib, "_reload");
	if (!m->key || !(key = m->key())) {
		ast_log(LOG_WARNING, "Key routine returned NULL in module %s\n", fn);
		key = NULL;
		errors++;
	}
	if (key && verify_key(key)) {
		ast_log(LOG_WARNING, "Unexpected key returned by module %s\n", fn);
		errors++;
	}
	if (errors) {
		ast_log(LOG_WARNING, "%d error(s) loading module %s, aborted\n", errors, fn);
		dlclose(m->lib);
		free(m);
		ast_mutex_unlock(&modlock);
		return -1;
	}
	if (!fully_booted) {
		if (option_verbose) 
			ast_verbose( " => (%s)\n", term_color(tmp, m->description(), COLOR_BROWN, COLOR_BLACK, sizeof(tmp)));
		if (option_console && !option_verbose)
			ast_verbose( ".");
	} else {
		if (option_verbose)
			ast_verbose(VERBOSE_PREFIX_1 "Loaded %s => (%s)\n", fn, m->description());
	}

	// add module 'm' to end of module_list chain
	// so reload commands will be issued in same order modules were loaded
	m->next = NULL;
	if (module_list == NULL) {
		// empty list so far, add at front
		module_list = m;
	}
	else {
		struct module *i;
		// find end of chain, and add there
		for (i = module_list; i->next; i = i->next)
			;
		i->next = m;
	}
	
	ast_mutex_unlock(&modlock);
	if ((res = m->load_module())) {
		ast_log(LOG_WARNING, "%s: load_module failed, returning %d\n", m->resource, res);
		ast_unload_resource(resource_name, 0);
		return -1;
	}
	ast_update_use_count();
	return 0;
}	

static int ast_resource_exists(char *resource)
{
	struct module *m;
	if (ast_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = module_list;
	while(m) {
		if (!strcasecmp(resource, m->resource))
			break;
		m = m->next;
	}
	ast_mutex_unlock(&modlock);
	if (m)
		return -1;
	else
		return 0;
}

int load_modules()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char tmp[80];
	if (option_verbose) 
		ast_verbose( "Asterisk Dynamic Loader Starting:\n");
	cfg = ast_load(AST_MODULE_CONFIG);
	if (cfg) {
		/* Load explicitly defined modules */
		v = ast_variable_browse(cfg, "modules");
		while(v) {
			if (!strcasecmp(v->name, "load")) {
				if (option_debug && !option_verbose)
					ast_log(LOG_DEBUG, "Loading module %s\n", v->value);
				if (option_verbose) {
					ast_verbose( VERBOSE_PREFIX_1 "[%s]", term_color(tmp, v->value, COLOR_BRWHITE, 0, sizeof(tmp)));
					fflush(stdout);
				}
				if (ast_load_resource(v->value)) {
					ast_log(LOG_WARNING, "Loading module %s failed!\n", v->value);
					if (cfg)
						ast_destroy(cfg);
					return -1;
				}
			}
			v=v->next;
		}
	}
	if (!cfg || ast_true(ast_variable_retrieve(cfg, "modules", "autoload"))) {
		/* Load all modules */
		DIR *mods;
		struct dirent *d;
		int x;
		/* Make two passes.  First, load any resource modules, then load the others. */
		for (x=0;x<2;x++) {
			mods = opendir((char *)ast_config_AST_MODULE_DIR);
			if (mods) {
				while((d = readdir(mods))) {
					/* Must end in .so to load it.  */
					if ((strlen(d->d_name) > 3) && (x || !strncasecmp(d->d_name, "res_", 4)) && 
					    !strcasecmp(d->d_name + strlen(d->d_name) - 3, ".so") &&
						!ast_resource_exists(d->d_name)) {
						/* It's a shared library -- Just be sure we're allowed to load it -- kinda
						   an inefficient way to do it, but oh well. */
						if (cfg) {
							v = ast_variable_browse(cfg, "modules");
							while(v) {
								if (!strcasecmp(v->name, "noload") &&
								    !strcasecmp(v->value, d->d_name)) 
									break;
								v = v->next;
							}
							if (v) {
								if (option_verbose) {
									ast_verbose( VERBOSE_PREFIX_1 "[skipping %s]\n", d->d_name);
									fflush(stdout);
								}
								continue;
							}
							
						}
					    if (option_debug && !option_verbose)
							ast_log(LOG_DEBUG, "Loading module %s\n", d->d_name);
						if (option_verbose) {
							ast_verbose( VERBOSE_PREFIX_1 "[%s]", term_color(tmp, d->d_name, COLOR_BRWHITE, 0, sizeof(tmp)));
							fflush(stdout);
						}
						if (ast_load_resource(d->d_name)) {
							ast_log(LOG_WARNING, "Loading module %s failed!\n", d->d_name);
							if (cfg)
								ast_destroy(cfg);
							return -1;
						}
					}
				}
				closedir(mods);
			} else {
				if (!option_quiet)
					ast_log(LOG_WARNING, "Unable to open modules directory %s.\n", (char *)ast_config_AST_MODULE_DIR);
			}
		}
	} 
	ast_destroy(cfg);
	return 0;
}

void ast_update_use_count(void)
{
	/* Notify any module monitors that the use count for a 
	   resource has changed */
	struct loadupdate *m;
	if (ast_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = updaters;
	while(m) {
		m->updater();
		m = m->next;
	}
	ast_mutex_unlock(&modlock);
	
}

int ast_update_module_list(int (*modentry)(char *module, char *description, int usecnt))
{
	struct module *m;
	int unlock = -1;
	if (ast_mutex_trylock(&modlock))
		unlock = 0;
	m = module_list;
	while(m) {
		modentry(m->resource, m->description(), m->usecount());
		m = m->next;
	}
	if (unlock)
		ast_mutex_unlock(&modlock);
	return 0;
}

int ast_loader_register(int (*v)(void)) 
{
	struct loadupdate *tmp;
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	if ((tmp = malloc(sizeof (struct loadupdate)))) {
		tmp->updater = v;
		if (ast_mutex_lock(&modlock))
			ast_log(LOG_WARNING, "Failed to lock\n");
		tmp->next = updaters;
		updaters = tmp;
		ast_mutex_unlock(&modlock);
		return 0;
	}
	return -1;
}

int ast_loader_unregister(int (*v)(void))
{
	int res = -1;
	struct loadupdate *tmp, *tmpl=NULL;
	if (ast_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	tmp = updaters;
	while(tmp) {
		if (tmp->updater == v)	{
			if (tmpl)
				tmpl->next = tmp->next;
			else
				updaters = tmp->next;
			break;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	if (tmp)
		res = 0;
	ast_mutex_unlock(&modlock);
	return res;
}
