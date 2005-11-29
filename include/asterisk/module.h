/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Module definitions
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_MODULE_H
#define _ASTERISK_MODULE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Every module must provide these functions */

int load_module(void);			/* Initialize the module */
int unload_module(void);		/* Cleanup all module structures, 
					   sockets, etc */
int usecount(void);			/* How many channels provided by this module are in use? */
char *description(void);		/* Description of this module */
char *key(void);		/* Return the below mentioned key, unmodified */

int reload(void);		/* reload configs */

#define ASTERISK_GPL_KEY \
	"This paragraph is Copyright (C) 2000, Linux Support Services, Inc.  \
In order for your module to load, it must return this key via a function \
called \"key\".  Any code which includes this paragraph must be licensed under \
the GNU General Public License version 2 or later (at your option).   Linux \
Support Services, Inc. reserves the right to allow other parties to license \
this paragraph under other terms as well."

#define AST_MODULE_CONFIG "modules.conf" /* Module configuration file */

#define AST_FORCE_SOFT 0
#define AST_FORCE_FIRM 1
#define AST_FORCE_HARD 2

/* Load a module */
int ast_load_resource(char *resource_name);

/* Unload a module.  Force unloading a module is not recommended. */
int ast_unload_resource(char *resource_name, int force);

/* Notify when usecount has been changed */
void ast_update_use_count(void);

/* Ask for a list of modules, descriptions, and use counts */
int ast_update_module_list(int (*modentry)(char *module, char *description, int usecnt));

/* Ask this procedure to be run with modules have been updated */
int ast_loader_register(int (*updater)(void));

/* No longer run me when modules are updated */
int ast_loader_unregister(int (*updater)(void));

/* Reload all modules */
void ast_module_reload(void);

/* Local user routines keep track of which channels are using a given module resource.
   They can help make removing modules safer, particularly if they're in use at the time
   they have been requested to be removed */

#define STANDARD_LOCAL_USER struct localuser { \
								struct ast_channel *chan; \
								struct localuser *next; \
							}

#define LOCAL_USER_DECL static pthread_mutex_t localuser_lock = PTHREAD_MUTEX_INITIALIZER; \
						static struct localuser *localusers = NULL; \
						static int localusecnt = 0;

#define LOCAL_USER_ADD(u) { \
 \
	if (!(u=malloc(sizeof(struct localuser)))) { \
		ast_log(LOG_WARNING, "Out of memory\n"); \
		return -1; \
	} \
	pthread_mutex_lock(&localuser_lock); \
	u->chan = chan; \
	u->next = localusers; \
	localusers = u; \
	localusecnt++; \
	pthread_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

#define LOCAL_USER_REMOVE(u) { \
	struct localuser *uc, *ul = NULL; \
	pthread_mutex_lock(&localuser_lock); \
	uc = localusers; \
	while (uc) { \
		if (uc == u) { \
			if (ul) \
				ul->next = uc->next; \
			else \
				localusers = uc->next; \
			break; \
		} \
		ul = uc; \
		uc = uc->next; \
	}\
	free(u); \
	localusecnt--; \
	pthread_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

#define STANDARD_HANGUP_LOCALUSERS { \
	struct localuser *u, *ul; \
	pthread_mutex_lock(&localuser_lock); \
	u = localusers; \
	while(u) { \
		ast_softhangup(u->chan); \
		ul = u; \
		u = u->next; \
		free(ul); \
	} \
	pthread_mutex_unlock(&localuser_lock); \
	localusecnt=0; \
}

#define STANDARD_USECOUNT(res) { \
	pthread_mutex_lock(&localuser_lock); \
	res = localusecnt; \
	pthread_mutex_unlock(&localuser_lock); \
}
	
	

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif
#endif
