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

/*! Initialize the module */
/*!
 * This function is called at module load time.  Put all code in here
 * that needs to set up your module's hardware, software, registrations,
 * etc.
 */
int load_module(void);

/*! Cleanup all module structures, sockets, etc */
/*!
 * This is called at exit.  Any registrations and memory allocations need
 * to be unregistered and free'd here.  Nothing else will do these for you (until exit).
 * Return 0 on success, or other than 0 if there is a problem.
 */
int unload_module(void);

/*! Provides a usecount */
/*!
 * This function will be called by various parts of asterisk.  Basically, all it has
 * to do is to return a usecount when called.  You will need to maintain your usecount
 * within the module somewhere.
 */
int usecount(void);			/*! How many channels provided by this module are in use? */

/*! Description */
/*!
 * Returns a short description of your module.
 */
char *description(void);		/*! Description of this module */

/*! Returns the ASTERISK_GPL_KEY */
/*!
 * This returns the ASTERISK_GPL_KEY, signifiying that you agree to the terms of
 * the GPL stated in the ASTERISK_GPL_KEY.  Your module will not load if it does
 * not return the EXACT message, i.e.  char *key(void){return ASTERISK_GPL_KEY;}
 */
char *key(void);		/*! Return the below mentioned key, unmodified */

/*! Reload stuff */
/*!
 * This function is where any reload routines take place.  Re-read config files,
 * change signalling, whatever is appropriate on a reload.
 * Return 0 on success, and other than 0 on problem.
 */
int reload(void);		/*! reload configs */

#define ASTERISK_GPL_KEY \
	"This paragraph is Copyright (C) 2000, Linux Support Services, Inc.  \
In order for your module to load, it must return this key via a function \
called \"key\".  Any code which includes this paragraph must be licensed under \
the GNU General Public License version 2 or later (at your option).   Linux \
Support Services, Inc. reserves the right to allow other parties to license \
this paragraph under other terms as well."

#define AST_MODULE_CONFIG "modules.conf" /*! Module configuration file */

#define AST_FORCE_SOFT 0
#define AST_FORCE_FIRM 1
#define AST_FORCE_HARD 2

/*! Loads a module */
/*! 
 * \param resource_name the filename of the module to load
 * This function is ran by the PBX to load the modules.  It performs
 * all loading, setting up of it's module related data structures, etc.
 * Basically, to load a module, you just give it the name of the module and
 * it will do the rest.
 * It returns 0 on success, -1 on error
 */
int ast_load_resource(const char *resource_name);

/*! Unloads a module */
/*! 
 * \param resourcename the name of the module to unload
 * \param force the force flag.  Setting this to non-zero will force the module to be unloaded
 * This function unloads a particular module.  If the force flag is not set,
 * it will not unload a module with a usecount > 0.  However, if it is set,
 * it will unload the module regardless of consequences (NOT_RECOMMENDED)
 */
int ast_unload_resource(const char *resource_name, int force);

/*! Notify when usecount has been changed */
/*!
 * This function goes through and calulates use counts.  It also notifies anybody
 * trying to keep track of them.
 */
void ast_update_use_count(void);

/*! Ask for a list of modules, descriptions, and use counts */
/*!
 * \param modentry a callback to an updater function
 * For each of the modules loaded, modentry will be executed with the resource, description,
 * and usecount values of each particular module.
 */
int ast_update_module_list(int (*modentry)(char *module, char *description, int usecnt, char *like), char *like);

/*! Ask this procedure to be run with modules have been updated */
/*!
 * \param updater the function to run when modules have been updated
 * This function adds the given function to a linked list of functions to be run
 * when the modules are updated. 
 * It returns 0 on success and -1 on failure.
 */
int ast_loader_register(int (*updater)(void));

/*! No longer run me when modules are updated */
/*!
 * \param updater function to unregister
 * This removes the given function from the updater list.
 * It returns 0 on success, -1 on failure.
 */
int ast_loader_unregister(int (*updater)(void));

/*! Reload all modules */
/*!
 * This reloads all modules set to load in asterisk.  It does NOT run the unload
 * routine and then loads them again, it runs the given reload routine.
 */
int ast_module_reload(const char *name);

char *ast_module_helper(char *line, char *word, int pos, int state, int rpos, int needsreload);

int ast_register_atexit(void (*func)(void));
void ast_unregister_atexit(void (*func)(void));

/* Local user routines keep track of which channels are using a given module resource.
   They can help make removing modules safer, particularly if they're in use at the time
   they have been requested to be removed */

#define STANDARD_LOCAL_USER struct localuser { \
								struct ast_channel *chan; \
								struct localuser *next; \
							}

#define LOCAL_USER_DECL AST_MUTEX_DEFINE_STATIC(localuser_lock); \
						static struct localuser *localusers = NULL; \
						static int localusecnt = 0;

#define LOCAL_USER_ADD(u) { \
 \
	if (!(u=(struct localuser *)malloc(sizeof(struct localuser)))) { \
		ast_log(LOG_WARNING, "Out of memory\n"); \
		return -1; \
	} \
	ast_mutex_lock(&localuser_lock); \
	u->chan = chan; \
	u->next = localusers; \
	localusers = u; \
	localusecnt++; \
	ast_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

#define LOCAL_USER_REMOVE(u) { \
	struct localuser *uc, *ul = NULL; \
	ast_mutex_lock(&localuser_lock); \
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
	ast_mutex_unlock(&localuser_lock); \
	ast_update_use_count(); \
}

#define STANDARD_HANGUP_LOCALUSERS { \
	struct localuser *u, *ul; \
	ast_mutex_lock(&localuser_lock); \
	u = localusers; \
	while(u) { \
		ast_softhangup(u->chan, AST_SOFTHANGUP_APPUNLOAD); \
		ul = u; \
		u = u->next; \
		free(ul); \
	} \
	ast_mutex_unlock(&localuser_lock); \
	localusecnt=0; \
}

#define STANDARD_USECOUNT(res) { \
	ast_mutex_lock(&localuser_lock); \
	res = localusecnt; \
	ast_mutex_unlock(&localuser_lock); \
}
	
	

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif
#endif
