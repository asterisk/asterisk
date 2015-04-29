/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, CFWare, LLC
 *
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
 * \brief Asterisk module internal definitions.
 */

#ifndef _ASTERISK_MODULE_PRIVATE_H
#define _ASTERISK_MODULE_PRIVATE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_module_instance;
struct ast_module_lib;

#define MODULES_VECTOR_SORT(m1, m2) strcmp(m1->name, m2->name)
#define MODULES_LIB_VECTOR_SORT(l1, l2) MODULES_VECTOR_SORT(l1->module, l2->module)

AST_VECTOR(ast_modules, struct ast_module *);
AST_VECTOR_RW(ast_modules_rw, struct ast_module *);

AST_VECTOR(ast_module_libs, struct ast_module_lib *);
AST_VECTOR_RW(ast_module_libs_rw, struct ast_module_lib *);

struct ast_module_disposer {
	/*! This prevents the module from closing, lets us to release the module. */
	struct ast_module_instance *instance;
	/*! Pointer to user data. */
	void *userdata;
	/*! Callback pointer called when module wants to unload. */
	ast_module_dispose_cb cb;

	/*! Already running callback. */
	unsigned int inprogress:1;
	/*! Do not call. */
	unsigned int donotcall:1;
};


struct ast_module_provider {
	char *id;
	struct ast_module *module;
};

AST_VECTOR(ast_module_providers, struct ast_module_provider *);

struct ast_module_providertype {
	char *id;
	struct ast_module_providers providers;
};

AST_VECTOR_RW(ast_module_providertypes, struct ast_module_providertype *);

struct ast_module_uses {
	char *type;
	AST_VECTOR(, char *) values;
};

AST_VECTOR(ast_module_uses_list, struct ast_module_uses *);

enum ast_module_status {
	AST_MODULE_STATUS_CLOSED,
	AST_MODULE_STATUS_DLOPENING,
	AST_MODULE_STATUS_DLOPENED,
	AST_MODULE_STATUS_INITING,
	AST_MODULE_STATUS_INITED,
	AST_MODULE_STATUS_STARTING,
	AST_MODULE_STATUS_RUNNING,
	AST_MODULE_STATUS_RELOADING,
	AST_MODULE_STATUS_UNLOADING,
	AST_MODULE_STATUS_DLCLOSING,
	AST_MODULE_STATUS_DLCLOSED
};

/*!
 * The linkage is as follows:
 *  ast_module proxies to an ast_module_lib_proxy.
 *  ast_module_lib proxies to an ast_module_instance.
 *  ast_module_lib_proxy links to an ast_module_lib.
 *
 * All ast_module objects are linked to \ref modules.
 * All loaded ast_module's are listed in modules_loaded.
 * All running ast_module_lib's are referenced by ast_module->lib.
 * All running ast_module_lib's are listed in modules_running.
 *
 * References to ast_module_instance prevents module_unload.
 * References to ast_module_lib prevents dlclose.
 * References to ast_module don't currently do much, eventually these
 * objects should be reloadable.
 */
struct ast_module {
	/*! The weakproxy is set to ast_module_lib_proxy. */
	AO2_WEAKPROXY();
	/*! Module Name */
	char *name;
	/*! Module Description. */
	char *description;
	/*! Checksum from manifest. */
	char *checksum;
	/*!
	 * \brief Running module
	 *
	 * \note This is temporarily filled while dlopen is running.  This value must not
	 * be used by __attribute__((constructor)) methods in modules, except from within
	 * __ast_module_register.
	 */
	struct ast_module_lib *lib;
	/*! Location to store variable that backs AST_MODULE_SELF. */
	struct ast_module **self;
	/*! Location that we store the reference held by admin (CLI, config or AMI) */
	struct ast_module_instance *admin_user;

	/*! List of strings */
	struct ast_string_vector alldeps;
	/*! List of configs used by this module */
	struct ast_string_vector configs;
	/*! List of ast_module_uses */
	struct ast_module_uses_list uses;

	/*! Support level for the module. */
	enum ast_module_support_level support_level;
	/*! BUGBUG: Implement Load priorities */
	enum ast_module_load_priority load_priority;
	/*! Status of the module. */
	enum ast_module_status status;

	/*! Set after alldeps list is initialized. */
	unsigned int alldeps_inited:1;
	/*! Set if there is a dependency error. */
	unsigned int alldeps_error:1;
	/*! Export Global Symbols. */
	unsigned int export_globals:1;
	/*! No unload before shutdown and no dlclose. */
	unsigned int block_unload:1;
	/*! Banned by administrator in modules.conf. */
	unsigned int neverload:1;
};

struct ast_module_lib {
	/*! The weakproxy is set to ast_module_instance. */
	AO2_WEAKPROXY();
#ifdef LOADABLE_MODULES
	/*! Library handle. */
	void *lib;
#endif
	/*! Hold a reference to the module for full lifetime. */
	struct ast_module *module;

	/*! List of libs from alldeps, hold them open until we dlclose. */
	struct ast_module_libs using;

	/*! Initialize the module. */
	ast_module_init_fn init_fn;
	/*! Start the module. */
	ast_module_start_fn start_fn;
	/*! Called upon request for reload. */
	ast_module_reload_fn reload_fn;
	/*! \brief Called when the function is already being destroyed.
	 *
	 * This function should be reasonable fail-safe.  If a failure occurs it is
	 * important to call \ref ast_module_block_unload.  This will prevent dlclose
	 * from being run and hopefully avoid a segmentation fault.
	 */
	ast_module_stop_fn stop_fn;
};

struct ast_module_lib_proxy {
	struct ast_module_lib *lib;
};

struct ast_module_instance {
	char *name;
	/*! Hold reference to lib for full lifetime and provide lock-free access. */
	struct ast_module_lib_proxy *lib_proxy;
	/*! Hold reference to the module for full lifetime and provide lock-free access. */
	struct ast_module *module;

	/*! List of Outbound uses */
	struct ast_module_disposers_rw using;
	/*! List of disposable users. */
	struct ast_module_disposers_rw users;

	unsigned int running:1;
	/*! This instance can only be released by shutdown. */
	unsigned int block_unload:1;
};

#define AST_VECTOR_DUP_AO2_MATCH_ALL(elem) ({ ao2_ref(elem, +1); (CMP_MATCH); })
#define AST_VECTOR_DUP_AO2(vec) \
	AST_VECTOR_CALLBACK_MULTIPLE(vec, AST_VECTOR_DUP_AO2_MATCH_ALL)

/*! List of all provider types except modules. */
extern struct ast_module_providertypes providertypes;
/*! List of all known modules, loaded or not. */
extern struct ast_modules_rw modules;
/*! List of loaded modules.  This list can be lagged. */
extern struct ast_modules_rw modules_loaded;
/*! List of running modules.  This list can be lagged. */
extern struct ast_module_libs_rw modules_running;

/*! List of module names that may never be loaded. */
extern struct ast_string_vector neverload;

int module_cli_init(void);
int module_manifest_init(void);
int module_manifest_build_alldeps(void);
void module_providertype_dtor(struct ast_module_providertype *ptyp);


struct ast_module_providertype *module_providertype_find(const char *id);
struct ast_module_provider *module_providertype_find_provider(struct ast_module_providertype *ptyp, const char *id);

int module_manifest_uses_add(struct ast_module *module, const char *type, const char *name);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
