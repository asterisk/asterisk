/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Module Loader
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
 *
 * Mark Spencer <markster@linux-support.net>
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
#include <asterisk/logger.h>
#include <dlfcn.h>
#define __USE_GNU
#include <pthread.h>
#include "asterisk.h"

struct module {
	int (*load_module)(void);
	int (*unload_module)(void);
	int (*usecount)(void);
	char *(*description)(void);
	void *lib;
	char resource[256];
	struct module *next;
};

static struct loadupdate {
	int (*updater)(void);
	struct loadupdate *next;
} *updaters = NULL;

static pthread_mutex_t modlock = PTHREAD_MUTEX_INITIALIZER;

static struct module *module_list=NULL;

int ast_unload_resource(char *resource_name, int force)
{
	struct module *m, *ml = NULL;
	int res = -1;
	if (pthread_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = module_list;
	while(m) {
		if (!strcasecmp(m->resource, resource_name)) {
			if ((res = m->usecount()) > 0)  {
				if (force) 
					ast_log(LOG_WARNING, "Warning:  Forcing removal of module %s with use count %d\n", resource_name, res);
				else {
					ast_log(LOG_WARNING, "Soft unload failed, '%s' has use count %d\n", resource_name, res);
					pthread_mutex_unlock(&modlock);
					return -1;
				}
			}
			res = m->unload_module();
			if (res) {
				ast_log(LOG_WARNING, "Firm unload failed for %s\n", resource_name);
				if (force <= AST_FORCE_FIRM) {
					pthread_mutex_unlock(&modlock);
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
		}
		ml = m;
		m = m->next;
	}
	pthread_mutex_unlock(&modlock);
	ast_update_use_count();
	return res;
}

int ast_load_resource(char *resource_name)
{
	static char fn[256];
	int errors=0;
	int res;
	struct module *m = malloc(sizeof(struct module));
	if (!m) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	strncpy(m->resource, resource_name, sizeof(m->resource));
	if (resource_name[0] == '/') {
		strncpy(fn, resource_name, sizeof(fn));
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", AST_MODULE_DIR, resource_name);
	}
	m->lib = dlopen(fn, RTLD_NOW  | RTLD_GLOBAL);
	if (!m->lib) {
		ast_log(LOG_WARNING, "%s\n", dlerror());
		free(m);
		return -1;
	}
	m->load_module = dlsym(m->lib, "load_module");
	if (!m->load_module) {
		ast_log(LOG_WARNING, "No load_module in module %s\n", fn);
		errors++;
	}
	m->unload_module = dlsym(m->lib, "unload_module");
	if (!m->unload_module) {
		ast_log(LOG_WARNING, "No unload_module in module %s\n", fn);
		errors++;
	}
	m->usecount = dlsym(m->lib, "usecount");
	if (!m->usecount) {
		ast_log(LOG_WARNING, "No usecount in module %s\n", fn);
		errors++;
	}
	m->description = dlsym(m->lib, "description");
	if (!m->description) {
		ast_log(LOG_WARNING, "No description in module %s\n", fn);
		errors++;
	}
	if (errors) {
		ast_log(LOG_WARNING, "%d error(s) loading module %s, aborted\n", errors, fn);
		dlclose(m->lib);
		free(m);
		return -1;
	}
	if (option_verbose) 
		ast_verbose( " => (%s)\n", m->description());
	if ((res = m->load_module())) {
		ast_log(LOG_WARNING, "%s: load_module failed, returning %d\n", m->resource, fn, res);
		free(m);
		return -1;
	}
	if (pthread_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m->next = module_list;
	module_list = m;
	pthread_mutex_unlock(&modlock);
	ast_update_use_count();
	return 0;
}	

int ast_resource_exists(char *resource)
{
	struct module *m;
	if (pthread_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = module_list;
	while(m) {
		if (!strcasecmp(resource, m->resource))
			break;
		m = m->next;
	}
	pthread_mutex_unlock(&modlock);
	if (m)
		return -1;
	else
		return 0;
}

int load_modules()
{
	struct ast_config *cfg;
	struct ast_variable *v;
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
					ast_verbose( " [%s]", v->value);
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
		mods = opendir(AST_MODULE_DIR);
		if (mods) {
			while((d = readdir(mods))) {
				/* Must end in .so to load it.  */
				if ((strlen(d->d_name) > 3) && 
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
						ast_verbose( VERBOSE_PREFIX_1 "[%s]", d->d_name);
						fflush(stdout);
					}
					if (ast_load_resource(d->d_name)) {
						ast_log(LOG_WARNING, "Loading module %s failed!\n", d->d_name);
						if (cfg)
							ast_destroy(cfg);
						return -1;
					}
				}
			};
		} else {
			if (!option_quiet)
				ast_log(LOG_WARNING, "Unable to open modules directory " AST_MODULE_DIR ".\n");
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
	if (pthread_mutex_lock(&modlock))
		ast_log(LOG_WARNING, "Failed to lock\n");
	m = updaters;
	while(m) {
		m->updater();
		m = m->next;
	}
	pthread_mutex_unlock(&modlock);
	
}

int ast_update_module_list(int (*modentry)(char *module, char *description, int usecnt))
{
	struct module *m;
	int unlock = -1;
	if (pthread_mutex_trylock(&modlock))
		unlock = 0;
	m = module_list;
	while(m) {
		modentry(m->resource, m->description(), m->usecount());
		m = m->next;
	}
	if (unlock)
		pthread_mutex_unlock(&modlock);
	return 0;
}

int ast_loader_register(int (*v)(void)) 
{
	struct loadupdate *tmp;
	/* XXX Should be more flexible here, taking > 1 verboser XXX */
	if ((tmp = malloc(sizeof (struct loadupdate)))) {
		tmp->updater = v;
		if (pthread_mutex_lock(&modlock))
			ast_log(LOG_WARNING, "Failed to lock\n");
		tmp->next = updaters;
		updaters = tmp;
		pthread_mutex_unlock(&modlock);
		return 0;
	}
	return -1;
}

int ast_loader_unregister(int (*v)(void))
{
	int res = -1;
	struct loadupdate *tmp, *tmpl=NULL;
	if (pthread_mutex_lock(&modlock))
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
	pthread_mutex_unlock(&modlock);
	return res;
}
