/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Matthew Nicholson <mnicholson@digium.com>
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

/*! 
 * \file
 *
 * \author Matthew Nicholson <mnicholson@digium.com>
 * \brief Lua PBX Switch
 *
 */

/*** MODULEINFO
	<depend>lua</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/term.h"
#include "asterisk/paths.h"
#include "asterisk/hashtab.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static char *config = "extensions.lua";
static char *registrar = "pbx_lua";

#define LUA_EXT_DATA_SIZE 256
#define LUA_BUF_SIZE 4096

static char *lua_read_extensions_file(lua_State *L, long *size);
static int lua_load_extensions(lua_State *L, struct ast_channel *chan);
static int lua_reload_extensions(lua_State *L);
static void lua_free_extensions(void);
static int lua_sort_extensions(lua_State *L);
static int lua_register_switches(lua_State *L);
static int lua_register_hints(lua_State *L);
static int lua_extension_cmp(lua_State *L);
static int lua_find_extension(lua_State *L, const char *context, const char *exten, int priority, ast_switch_f *func, int push_func);
static int lua_pbx_findapp(lua_State *L);
static int lua_pbx_exec(lua_State *L);

static int lua_get_variable_value(lua_State *L);
static int lua_set_variable_value(lua_State *L);
static int lua_get_variable(lua_State *L);
static int lua_set_variable(lua_State *L);
static int lua_func_read(lua_State *L);

static int lua_autoservice_start(lua_State *L);
static int lua_autoservice_stop(lua_State *L);
static int lua_autoservice_status(lua_State *L);
static int lua_check_hangup(lua_State *L);
static int lua_error_function(lua_State *L);

static void lua_update_registry(lua_State *L, const char *context, const char *exten, int priority);
static void lua_push_variable_table(lua_State *L, const char *name);
static void lua_create_app_table(lua_State *L);
static void lua_create_channel_table(lua_State *L);
static void lua_create_variable_metatable(lua_State *L);
static void lua_create_application_metatable(lua_State *L);
static void lua_create_autoservice_functions(lua_State *L);
static void lua_create_hangup_function(lua_State *L);

static void lua_state_destroy(void *data);
static void lua_datastore_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan);
static lua_State *lua_get_state(struct ast_channel *chan);

static int exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
static int canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
static int matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);
static int exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data);

AST_MUTEX_DEFINE_STATIC(config_file_lock);
static char *config_file_data = NULL;
static long config_file_size = 0;

static struct ast_context *local_contexts = NULL;
static struct ast_hashtab *local_table = NULL;

static const struct ast_datastore_info lua_datastore = {
	.type = "lua",
	.destroy = lua_state_destroy,
	.chan_fixup = lua_datastore_fixup,
};


/*!
 * \brief The destructor for lua_datastore
 */
static void lua_state_destroy(void *data)
{
	if (data)
		lua_close(data);
}

/*!
 * \brief The fixup function for the lua_datastore.
 * \param data the datastore data, in this case it will be a lua_State
 * \param old_chan the channel we are moving from
 * \param new_chan the channel we are moving to
 *
 * This function updates our internal channel pointer.
 */
static void lua_datastore_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	lua_State *L = data;
	lua_pushlightuserdata(L, new_chan);
	lua_setfield(L, LUA_REGISTRYINDEX, "channel");
}

/*!
 * \brief [lua_CFunction] Find an app and return it in a lua table (for access from lua, don't
 * call directly)
 *
 * This function would be called in the following example as it would be found
 * in extensions.lua.
 *
 * \code
 * app.dial
 * \endcode
 */
static int lua_pbx_findapp(lua_State *L)
{
	const char *app_name = luaL_checkstring(L, 2);
	
	lua_newtable(L);

	lua_pushstring(L, "name");
	lua_pushstring(L, app_name);
	lua_settable(L, -3);

	luaL_getmetatable(L, "application");
	lua_setmetatable(L, -2);

	return 1;
}

/*!
 * \brief [lua_CFunction] This function is part of the 'application' metatable
 * and is used to execute applications similar to pbx_exec() (for access from
 * lua, don't call directly)
 *
 * \param L the lua_State to use
 * \return nothing
 *
 * This funciton is executed as the '()' operator for apps accessed through the
 * 'app' table.
 *
 * \code
 * app.playback('demo-congrats')
 * \endcode
 */
static int lua_pbx_exec(lua_State *L)
{
	int res, nargs = lua_gettop(L);
	char data[LUA_EXT_DATA_SIZE] = "";
	char *data_next = data, *app_name;
	char *context, *exten;
	char tmp[80], tmp2[80], tmp3[LUA_EXT_DATA_SIZE];
	int priority, autoservice;
	size_t data_left = sizeof(data);
	struct ast_app *app;
	struct ast_channel *chan;
	
	lua_getfield(L, 1, "name");
	app_name = ast_strdupa(lua_tostring(L, -1));
	lua_pop(L, 1);
	
	if (!(app = pbx_findapp(app_name))) {
		lua_pushstring(L, "application '");
		lua_pushstring(L, app_name);
		lua_pushstring(L, "' not found");
		lua_concat(L, 3);
		return lua_error(L);
	}
	

	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);
	
	
	lua_getfield(L, LUA_REGISTRYINDEX, "context");
	context = ast_strdupa(lua_tostring(L, -1));
	lua_pop(L, 1);
	
	lua_getfield(L, LUA_REGISTRYINDEX, "exten");
	exten = ast_strdupa(lua_tostring(L, -1));
	lua_pop(L, 1);
	
	lua_getfield(L, LUA_REGISTRYINDEX, "priority");
	priority = lua_tointeger(L, -1);
	lua_pop(L, 1);


	if (nargs > 1) {
		int i;

		if (!lua_isnil(L, 2))
			ast_build_string(&data_next, &data_left, "%s", luaL_checkstring(L, 2));

		for (i = 3; i <= nargs; i++) {
			if (lua_isnil(L, i))
				ast_build_string(&data_next, &data_left, ",");
			else
				ast_build_string(&data_next, &data_left, ",%s", luaL_checkstring(L, i));
		}
	}
	
	ast_verb(3, "Executing [%s@%s:%d] %s(\"%s\", \"%s\")\n",
			exten, context, priority,
			term_color(tmp, app_name, COLOR_BRCYAN, 0, sizeof(tmp)),
			term_color(tmp2, chan->name, COLOR_BRMAGENTA, 0, sizeof(tmp2)),
			term_color(tmp3, data, COLOR_BRMAGENTA, 0, sizeof(tmp3)));

	lua_getfield(L, LUA_REGISTRYINDEX, "autoservice");
	autoservice = lua_toboolean(L, -1);
	lua_pop(L, 1);

	if (autoservice)
		ast_autoservice_stop(chan);

	res = pbx_exec(chan, app, data);
	
	if (autoservice)
		ast_autoservice_start(chan);

	/* error executing an application, report it */
	if (res) {
		lua_pushinteger(L, res);
		return lua_error(L);
	}
	return 0;
}

/*!
 * \brief [lua_CFunction] Used to get the value of a variable or dialplan
 * function (for access from lua, don't call directly)
 * 
 * The value of the variable or function is returned.  This function is the
 * 'get()' function in the following example as would be seen in
 * extensions.lua.
 *
 * \code
 * channel.variable:get()
 * \endcode
 */
static int lua_get_variable_value(lua_State *L)
{
	struct ast_channel *chan;
	char *value = NULL, *name;
	char *workspace = alloca(LUA_BUF_SIZE);
	int autoservice;

	workspace[0] = '\0';

	if (!lua_istable(L, 1)) {
		lua_pushstring(L, "User probably used '.' instead of ':' for retrieving a channel variable value");
		return lua_error(L);
	}
	
	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, 1, "name");
	name = ast_strdupa(lua_tostring(L, -1));
	lua_pop(L, 1);
	
	lua_getfield(L, LUA_REGISTRYINDEX, "autoservice");
	autoservice = lua_toboolean(L, -1);
	lua_pop(L, 1);

	if (autoservice)
		ast_autoservice_stop(chan);
	
	/* if this is a dialplan function then use ast_func_read(), otherwise
	 * use pbx_retrieve_variable() */
	if (!ast_strlen_zero(name) && name[strlen(name) - 1] == ')') {
		value = ast_func_read(chan, name, workspace, LUA_BUF_SIZE) ? NULL : workspace;
	} else {
		pbx_retrieve_variable(chan, name, &value, workspace, LUA_BUF_SIZE, &chan->varshead);
	}
	
	if (autoservice)
		ast_autoservice_start(chan);

	if (value) {
		lua_pushstring(L, value);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

/*!
 * \brief [lua_CFunction] Used to set the value of a variable or dialplan
 * function (for access from lua, don't call directly)
 * 
 * This function is the 'set()' function in the following example as would be
 * seen in extensions.lua.
 *
 * \code
 * channel.variable:set("value")
 * \endcode
 */
static int lua_set_variable_value(lua_State *L)
{
	const char *name, *value;
	struct ast_channel *chan;
	int autoservice;

	if (!lua_istable(L, 1)) {
		lua_pushstring(L, "User probably used '.' instead of ':' for setting a channel variable");
		return lua_error(L);
	}

	lua_getfield(L, 1, "name");
	name = ast_strdupa(lua_tostring(L, -1));
	lua_pop(L, 1);

	value = luaL_checkstring(L, 2);
	
	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, LUA_REGISTRYINDEX, "autoservice");
	autoservice = lua_toboolean(L, -1);
	lua_pop(L, 1);

	if (autoservice)
		ast_autoservice_stop(chan);

	pbx_builtin_setvar_helper(chan, name, value);
	
	if (autoservice)
		ast_autoservice_start(chan);

	return 0;
}

/*!
 * \brief Update the lua registry with the given context, exten, and priority.
 *
 * \param L the lua_State to use
 * \param context the new context
 * \param exten the new exten
 * \param priority the new priority
 */
static void lua_update_registry(lua_State *L, const char *context, const char *exten, int priority)
{
	lua_pushstring(L, context);
	lua_setfield(L, LUA_REGISTRYINDEX, "context");

	lua_pushstring(L, exten);
	lua_setfield(L, LUA_REGISTRYINDEX, "exten");

	lua_pushinteger(L, priority);
	lua_setfield(L, LUA_REGISTRYINDEX, "priority");
}

/*!
 * \brief Push a 'variable' table on the stack for access the channel variable
 * with the given name.
 *
 * \param L the lua_State to use
 * \param name the name of the variable
 */
static void lua_push_variable_table(lua_State *L, const char *name)
{
	lua_newtable(L);
	luaL_getmetatable(L, "variable");
	lua_setmetatable(L, -2);

	lua_pushstring(L, name);
	lua_setfield(L, -2, "name");
	
	lua_pushcfunction(L, &lua_get_variable_value);
	lua_setfield(L, -2, "get");
	
	lua_pushcfunction(L, &lua_set_variable_value);
	lua_setfield(L, -2, "set");
}

/*!
 * \brief Create the global 'app' table for executing applications
 *
 * \param L the lua_State to use
 */
static void lua_create_app_table(lua_State *L)
{
	lua_newtable(L);
	luaL_newmetatable(L, "app");

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, &lua_pbx_findapp);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	lua_setglobal(L, "app");
}

/*!
 * \brief Create the global 'channel' table for accesing channel variables
 *
 * \param L the lua_State to use
 */
static void lua_create_channel_table(lua_State *L)
{
	lua_newtable(L);
	luaL_newmetatable(L, "channel_data");

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, &lua_get_variable);
	lua_settable(L, -3);

	lua_pushstring(L, "__newindex");
	lua_pushcfunction(L, &lua_set_variable);
	lua_settable(L, -3);

	lua_setmetatable(L, -2);
	lua_setglobal(L, "channel");
}

/*!
 * \brief Create the 'variable' metatable, used to retrieve channel variables
 *
 * \param L the lua_State to use
 */
static void lua_create_variable_metatable(lua_State *L)
{
	luaL_newmetatable(L, "variable");

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, &lua_func_read);
	lua_settable(L, -3);

	lua_pop(L, 1);
}

/*!
 * \brief Create the 'application' metatable, used to execute asterisk
 * applications from lua 
 *
 * \param L the lua_State to use
 */
static void lua_create_application_metatable(lua_State *L)
{
	luaL_newmetatable(L, "application");

	lua_pushstring(L, "__call");
	lua_pushcfunction(L, &lua_pbx_exec);
	lua_settable(L, -3);

	lua_pop(L, 1);
}

/*!
 * \brief Create the autoservice functions
 *
 * \param L the lua_State to use
 */
static void lua_create_autoservice_functions(lua_State *L)
{
	lua_pushcfunction(L, &lua_autoservice_start);
	lua_setglobal(L, "autoservice_start");
	
	lua_pushcfunction(L, &lua_autoservice_stop);
	lua_setglobal(L, "autoservice_stop");

	lua_pushcfunction(L, &lua_autoservice_status);
	lua_setglobal(L, "autoservice_status");

	lua_pushboolean(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, "autoservice");
}

/*!
 * \brief Create the hangup check function
 *
 * \param L the lua_State to use
 */
static void lua_create_hangup_function(lua_State *L)
{
	lua_pushcfunction(L, &lua_check_hangup);
	lua_setglobal(L, "check_hangup");
}

/*!
 * \brief [lua_CFunction] Return a lua 'variable' object (for access from lua, don't call
 * directly)
 * 
 * This function is called to lookup a variable construct a 'variable' object.
 * It would be called in the following example as would be seen in
 * extensions.lua.
 *
 * \code
 * channel.variable
 * \endcode
 */
static int lua_get_variable(lua_State *L)
{
	struct ast_channel *chan;
	char *name = ast_strdupa(luaL_checkstring(L, 2));
	char *value = NULL;
	char *workspace = alloca(LUA_BUF_SIZE);
	workspace[0] = '\0';
	
	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	lua_push_variable_table(L, name);
	
	/* if this is not a request for a dialplan funciton attempt to retrieve
	 * the value of the variable */
	if (!ast_strlen_zero(name) && name[strlen(name) - 1] != ')') {
		pbx_retrieve_variable(chan, name, &value, workspace, LUA_BUF_SIZE, &chan->varshead);
	}

	if (value) {
		lua_pushstring(L, value);
		lua_setfield(L, -2, "value");
	}

	return 1;	
}

/*!
 * \brief [lua_CFunction] Set the value of a channel variable or dialplan
 * function (for access from lua, don't call directly)
 * 
 * This function is called to set a variable or dialplan function.  It would be
 * called in the following example as would be seen in extensions.lua.
 *
 * \code
 * channel.variable = "value"
 * \endcode
 */
static int lua_set_variable(lua_State *L)
{
	struct ast_channel *chan;
	int autoservice;
	const char *name = luaL_checkstring(L, 2);
	const char *value = luaL_checkstring(L, 3);

	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, LUA_REGISTRYINDEX, "autoservice");
	autoservice = lua_toboolean(L, -1);
	lua_pop(L, 1);

	if (autoservice)
		ast_autoservice_stop(chan);

	pbx_builtin_setvar_helper(chan, name, value);
	
	if (autoservice)
		ast_autoservice_start(chan);

	return 0;
}

/*!
 * \brief [lua_CFunction] Create a 'variable' object for accessing a dialplan
 * function (for access from lua, don't call directly)
 * 
 * This function is called to create a 'variable' object to access a dialplan
 * function.  It would be called in the following example as would be seen in
 * extensions.lua.
 *
 * \code
 * channel.func("arg1", "arg2", "arg3")
 * \endcode
 *
 * To actually do anything with the resulting value you must use the 'get()'
 * and 'set()' methods (the reason is the resulting value is not a value, but
 * an object in the form of a lua table).
 */
static int lua_func_read(lua_State *L)
{
	int nargs = lua_gettop(L);
	char fullname[LUA_EXT_DATA_SIZE] = "";
	char *fullname_next = fullname, *name;
	size_t fullname_left = sizeof(fullname);
	
	lua_getfield(L, 1, "name");
	name = ast_strdupa(lua_tostring(L, -1));
	lua_pop(L, 1);

	ast_build_string(&fullname_next, &fullname_left, "%s(", name);
	
	if (nargs > 1) {
		int i;

		if (!lua_isnil(L, 2))
			ast_build_string(&fullname_next, &fullname_left, "%s", luaL_checkstring(L, 2));

		for (i = 3; i <= nargs; i++) {
			if (lua_isnil(L, i))
				ast_build_string(&fullname_next, &fullname_left, ",");
			else
				ast_build_string(&fullname_next, &fullname_left, ",%s", luaL_checkstring(L, i));
		}
	}

	ast_build_string(&fullname_next, &fullname_left, ")");
	
	lua_push_variable_table(L, fullname);
	
	return 1;
}

/*!
 * \brief [lua_CFunction] Tell pbx_lua to maintain an autoservice on this
 * channel (for access from lua, don't call directly)
 *
 * \param L the lua_State to use
 *
 * This function will set a flag that will cause pbx_lua to maintain an
 * autoservice on this channel.  The autoservice will automatically be stopped
 * and restarted before calling applications and functions.
 *
 * \return This function returns the result of the ast_autoservice_start()
 * function as a boolean to its lua caller.
 */
static int lua_autoservice_start(lua_State *L)
{
	struct ast_channel *chan;
	int res;

	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	res = ast_autoservice_start(chan);

	lua_pushboolean(L, !res);
	lua_setfield(L, LUA_REGISTRYINDEX, "autoservice");

	lua_pushboolean(L, !res);
	return 1;
}

/*!
 * \brief [lua_CFunction] Tell pbx_lua to stop maintaning an autoservice on
 * this channel (for access from lua, don't call directly)
 *
 * \param L the lua_State to use
 *
 * This function will stop any autoservice running and turn off the autoservice
 * flag.  If this function returns false, it's probably because no autoservice
 * was running to begin with.
 *
 * \return This function returns the result of the ast_autoservice_stop()
 * function as a boolean to its lua caller.
 */
static int lua_autoservice_stop(lua_State *L)
{
	struct ast_channel *chan;
	int res;

	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	res = ast_autoservice_stop(chan);

	lua_pushboolean(L, 0);
	lua_setfield(L, LUA_REGISTRYINDEX, "autoservice");

	lua_pushboolean(L, !res);
	return 1;
}

/*!
 * \brief [lua_CFunction] Get the status of the autoservice flag (for access
 * from lua, don't call directly)
 *
 * \param L the lua_State to use
 *
 * \return This function returns the status of the autoservice flag as a
 * boolean to its lua caller.
 */
static int lua_autoservice_status(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "autoservice");
	return 1;
}

/*!
 * \brief [lua_CFunction] Check if this channel has been hungup or not (for
 * access from lua, don't call directly)
 *
 * \param L the lua_State to use
 *
 * \return This function returns true if the channel was hungup
 */
static int lua_check_hangup(lua_State *L)
{
	struct ast_channel *chan;
	lua_getfield(L, LUA_REGISTRYINDEX, "channel");
	chan = lua_touserdata(L, -1);
	lua_pop(L, 1);

	lua_pushboolean(L, ast_check_hangup(chan));
	return 1;
}

/*!
 * \brief [lua_CFunction] Handle lua errors (for access from lua, don't call
 * directly)
 *
 * \param L the lua_State to use
 */
static int lua_error_function(lua_State *L)
{
	int message_index;

	/* pass number arguments right through back to asterisk*/
	if (lua_isnumber(L, -1)) {
		return 1;
	}

	/* if we are here then we have a string error message, let's attach a
	 * backtrace to it */
	message_index = lua_gettop(L);

	/* prepare to prepend a new line to the traceback */
	lua_pushliteral(L, "\n");

	lua_getglobal(L, "debug");
	lua_getfield(L, -1, "traceback");
	lua_remove(L, -2); /* remove the 'debug' table */

	lua_pushvalue(L, message_index);
	lua_remove(L, message_index);

	lua_pushnumber(L, 2);

	lua_call(L, 2, 1);

	/* prepend the new line we prepared above */
	lua_concat(L, 2);

	return 1;
}

/*!
 * \brief Store the sort order of each context
 
 * In the event of an error, an error string will be pushed onto the lua stack.
 *
 * \retval 0 success
 * \retval 1 failure
 */
static int lua_sort_extensions(lua_State *L)
{
	int extensions, extensions_order;

	/* create the extensions_order table */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "extensions_order");
	lua_getfield(L, LUA_REGISTRYINDEX, "extensions_order");
	extensions_order = lua_gettop(L);

	/* sort each context in the extensions table */
	/* load the 'extensions' table */
	lua_getglobal(L, "extensions");
	extensions = lua_gettop(L);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushstring(L, "Unable to find 'extensions' table in extensions.lua\n");
		return 1;
	}

	/* iterate through the extensions table and create a
	 * matching table (holding the sort order) in the
	 * extensions_order table for each context that is found
	 */
	for (lua_pushnil(L); lua_next(L, extensions); lua_pop(L, 1)) {
		int context = lua_gettop(L);
		int context_name = context - 1;
		int context_order;

		/* copy the context_name to be used as the key for the
		 * context_order table in the extensions_order table later */
		lua_pushvalue(L, context_name);

		/* create the context_order table */
		lua_newtable(L);
		context_order = lua_gettop(L);

		/* iterate through this context an popluate the corrisponding
		 * table in the extensions_order table */
		for (lua_pushnil(L); lua_next(L, context); lua_pop(L, 1)) {
			int exten = lua_gettop(L) - 1;

			lua_pushinteger(L, lua_objlen(L, context_order) + 1);
			lua_pushvalue(L, exten);
			lua_settable(L, context_order);
		}
		lua_settable(L, extensions_order); /* put the context_order table in the extensions_order table */

		/* now sort the new table */

		/* push the table.sort function */
		lua_getglobal(L, "table");
		lua_getfield(L, -1, "sort");
		lua_remove(L, -2); /* remove the 'table' table */

		/* push the context_order table */
		lua_pushvalue(L, context_name);
		lua_gettable(L, extensions_order);

		/* push the comp function */
		lua_pushcfunction(L, &lua_extension_cmp);

		if (lua_pcall(L, 2, 0, 0)) {
			lua_insert(L, -5);
			lua_pop(L, 4);
			return 1;
		}
	}
	
	/* remove the extensions table and the extensions_order table */
	lua_pop(L, 2);
	return 0;
}

/*!
 * \brief Register dialplan switches for our pbx_lua contexs.
 *
 * In the event of an error, an error string will be pushed onto the lua stack.
 *
 * \retval 0 success
 * \retval 1 failure
 */
static int lua_register_switches(lua_State *L)
{
	int extensions;
	struct ast_context *con = NULL;

	/* create the hash table for our contexts */
	/* XXX do we ever need to destroy this? pbx_config does not */
	if (!local_table)
		local_table = ast_hashtab_create(17, ast_hashtab_compare_contexts, ast_hashtab_resize_java, ast_hashtab_newsize_java, ast_hashtab_hash_contexts, 0);

	/* load the 'extensions' table */
	lua_getglobal(L, "extensions");
	extensions = lua_gettop(L);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushstring(L, "Unable to find 'extensions' table in extensions.lua\n");
		return 1;
	}

	/* iterate through the extensions table and register a context and
	 * dialplan switch for each lua context
	 */
	for (lua_pushnil(L); lua_next(L, extensions); lua_pop(L, 1)) {
		int context = lua_gettop(L);
		int context_name = context - 1;
		const char *context_str = lua_tostring(L, context_name);

		/* find or create this context */
		con = ast_context_find_or_create(&local_contexts, local_table, context_str, registrar);
		if (!con) {
			/* remove extensions table and context key and value */
			lua_pop(L, 3);
			lua_pushstring(L, "Failed to find or create context\n");
			return 1;
		}

		/* register the switch */
		if (ast_context_add_switch2(con, "Lua", "", 0, registrar)) {
			/* remove extensions table and context key and value */
			lua_pop(L, 3);
			lua_pushstring(L, "Unable to create switch for context\n");
			return 1;
		}
	}
	
	/* remove the extensions table */
	lua_pop(L, 1);
	return 0;
}

/*!
 * \brief Register dialplan hints for our pbx_lua contexs.
 *
 * In the event of an error, an error string will be pushed onto the lua stack.
 *
 * \retval 0 success
 * \retval 1 failure
 */
static int lua_register_hints(lua_State *L)
{
	int hints;
	struct ast_context *con = NULL;

	/* create the hash table for our contexts */
	/* XXX do we ever need to destroy this? pbx_config does not */
	if (!local_table)
		local_table = ast_hashtab_create(17, ast_hashtab_compare_contexts, ast_hashtab_resize_java, ast_hashtab_newsize_java, ast_hashtab_hash_contexts, 0);

	/* load the 'hints' table */
	lua_getglobal(L, "hints");
	hints = lua_gettop(L);
	if (lua_isnil(L, -1)) {
		/* hints table not found, move along */
		lua_pop(L, 1);
		return 0;
	}

	/* iterate through the hints table and register each context and
	 * the hints that go along with it
	 */
	for (lua_pushnil(L); lua_next(L, hints); lua_pop(L, 1)) {
		int context = lua_gettop(L);
		int context_name = context - 1;
		const char *context_str = lua_tostring(L, context_name);

		/* find or create this context */
		con = ast_context_find_or_create(&local_contexts, local_table, context_str, registrar);
		if (!con) {
			/* remove hints table and context key and value */
			lua_pop(L, 3);
			lua_pushstring(L, "Failed to find or create context\n");
			return 1;
		}

		/* register each hint */
		for (lua_pushnil(L); lua_next(L, context); lua_pop(L, 1)) {
			const char *hint_value = lua_tostring(L, -1);
			const char *hint_name;

			/* the hint value is not a string, ignore it */
			if (!hint_value) {
				continue;
			}

			/* copy the name then convert it to a string */
			lua_pushvalue(L, -2);
			if (!(hint_name = lua_tostring(L, -1))) {
				/* ignore non-string value */
				lua_pop(L, 1);
				continue;
			}

			if (ast_add_extension2(con, 0, hint_name, PRIORITY_HINT, NULL, NULL, hint_value, NULL, NULL, registrar)) {
				/* remove hints table, hint name, hint value,
				 * key copy, context name, and contex table */
				lua_pop(L, 6);
				lua_pushstring(L, "Error creating hint\n");
				return 1;
			}

			/* pop the name copy */
			lua_pop(L, 1);
		}
	}

	/* remove the hints table */
	lua_pop(L, 1);

	return 0;
}

/*!
 * \brief [lua_CFunction] Compare two extensions (for access from lua, don't
 * call directly)
 *
 * This function returns true if the first extension passed should match after
 * the second.  It behaves like the '<' operator.
 */
static int lua_extension_cmp(lua_State *L)
{
	const char *a = luaL_checkstring(L, -2);
	const char *b = luaL_checkstring(L, -1);

	if (ast_extension_cmp(a, b) == -1)
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);

	return 1;
}

/*!
 * \brief Load the extensions.lua file in to a buffer and execute the file
 *
 * \param L the lua_State to use
 * \param size a pointer to store the size of the buffer
 *
 * \note The caller is expected to free the buffer at some point.
 *
 * \return a pointer to the buffer
 */
static char *lua_read_extensions_file(lua_State *L, long *size)
{
	FILE *f;
	int error_func;
	char *data;
	char *path = alloca(strlen(config) + strlen(ast_config_AST_CONFIG_DIR) + 2);
	sprintf(path, "%s/%s", ast_config_AST_CONFIG_DIR, config);

	if (!(f = fopen(path, "r"))) {
		lua_pushstring(L, "cannot open '");
		lua_pushstring(L, path);
		lua_pushstring(L, "' for reading: ");
		lua_pushstring(L, strerror(errno));
		lua_concat(L, 4);

		return NULL;
	}

	if (fseek(f, 0l, SEEK_END)) {
		fclose(f);
		lua_pushliteral(L, "error determining the size of the config file");
		return NULL;
	}

	*size = ftell(f);

	if (fseek(f, 0l, SEEK_SET)) {
		*size = 0;
		fclose(f);
		lua_pushliteral(L, "error reading config file");
		return NULL;
	}

	if (!(data = ast_malloc(*size))) {
		*size = 0;
		fclose(f);
		lua_pushstring(L, "not enough memory");
		return NULL;
	}

	if (fread(data, sizeof(char), *size, f) != *size) {
		*size = 0;
		fclose(f);
		lua_pushliteral(L, "problem reading configuration file");
		return NULL;
	}
	fclose(f);

	lua_pushcfunction(L, &lua_error_function);
	error_func = lua_gettop(L);

	if (luaL_loadbuffer(L, data, *size, "extensions.lua")
			|| lua_pcall(L, 0, LUA_MULTRET, error_func)
			|| lua_sort_extensions(L)
			|| lua_register_switches(L)
			|| lua_register_hints(L)) {
		ast_free(data);
		data = NULL;
		*size = 0;
	}

	lua_remove(L, error_func);
	return data;
}

/*!
 * \brief Load the extensions.lua file from the internal buffer
 *
 * \param L the lua_State to use
 * \param chan channel to work on
 *
 * This function also sets up some constructs used by the extensions.lua file.
 * In the event of an error, an error string will be pushed onto the lua stack.
 *
 * \retval 0 success
 * \retval 1 failure
 */
static int lua_load_extensions(lua_State *L, struct ast_channel *chan)
{
	
	/* store a pointer to this channel */
	lua_pushlightuserdata(L, chan);
	lua_setfield(L, LUA_REGISTRYINDEX, "channel");
	
	luaL_openlibs(L);

	/* load and sort extensions */
	ast_mutex_lock(&config_file_lock);
	if (luaL_loadbuffer(L, config_file_data, config_file_size, "extensions.lua")
			|| lua_pcall(L, 0, LUA_MULTRET, 0)
			|| lua_sort_extensions(L)) {
		ast_mutex_unlock(&config_file_lock);
		return 1;
	}
	ast_mutex_unlock(&config_file_lock);

	/* now we setup special tables and functions */

	lua_create_app_table(L);
	lua_create_channel_table(L);

	lua_create_variable_metatable(L);
	lua_create_application_metatable(L);

	lua_create_autoservice_functions(L);
	lua_create_hangup_function(L);

	return 0;
}

/*!
 * \brief Reload the extensions file and update the internal buffers if it
 * loads correctly.
 *
 * \warning This function should not be called on a lua_State returned from
 * lua_get_state().
 *
 * \param L the lua_State to use (must be freshly allocated with
 * luaL_newstate(), don't use lua_get_state())
 */
static int lua_reload_extensions(lua_State *L)
{
	long size = 0;
	char *data = NULL;

	luaL_openlibs(L);

	if (!(data = lua_read_extensions_file(L, &size))) {
		return 1;
	}

	ast_mutex_lock(&config_file_lock);

	if (config_file_data)
		ast_free(config_file_data);

	config_file_data = data;
	config_file_size = size;
	
	/* merge our new contexts */
	ast_merge_contexts_and_delete(&local_contexts, local_table, registrar);
	/* merge_contexts_and_delete will actually, at the correct moment, 
	   set the global dialplan pointers to your local_contexts and local_table.
	   It then will free up the old tables itself. Just be sure not to
	   hang onto the pointers. */
	local_table = NULL;
	local_contexts = NULL;

	ast_mutex_unlock(&config_file_lock);
	return 0;
}

/*!
 * \brief Free the internal extensions buffer.
 */
static void lua_free_extensions()
{
	ast_mutex_lock(&config_file_lock);
	config_file_size = 0;
	ast_free(config_file_data);
	ast_mutex_unlock(&config_file_lock);
}

/*!
 * \brief Get the lua_State for this channel
 *
 * If no channel is passed then a new state is allocated.  States with no
 * channel assocatied with them should only be used for matching extensions.
 * If the channel does not yet have a lua state associated with it, one will be
 * created.
 *
 * \note If no channel was passed then the caller is expected to free the state
 * using lua_close().
 *
 * \return a lua_State
 */
static lua_State *lua_get_state(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	lua_State *L;

	if (!chan) {
		lua_State *L = luaL_newstate();
		if (!L) {
			ast_log(LOG_ERROR, "Error allocating lua_State, no memory\n");
			return NULL;
		}

		if (lua_load_extensions(L, NULL)) {
			const char *error = lua_tostring(L, -1);
			ast_log(LOG_ERROR, "Error loading extensions.lua: %s\n", error);
			lua_close(L);
			return NULL;
		}
		return L;
	} else {
		ast_channel_lock(chan);
		datastore = ast_channel_datastore_find(chan, &lua_datastore, NULL);
		ast_channel_unlock(chan);

		if (!datastore) {
			/* nothing found, allocate a new lua state */
			datastore = ast_datastore_alloc(&lua_datastore, NULL);
			if (!datastore) {
				ast_log(LOG_ERROR, "Error allocation channel datastore for lua_State\n");
				return NULL;
			}

			datastore->data = luaL_newstate();
			if (!datastore->data) {
				ast_datastore_free(datastore);
				ast_log(LOG_ERROR, "Error allocating lua_State, no memory\n");
				return NULL;
			}

			ast_channel_lock(chan);
			ast_channel_datastore_add(chan, datastore);
			ast_channel_unlock(chan);

			L = datastore->data;

			if (lua_load_extensions(L, chan)) {
				const char *error = lua_tostring(L, -1);
				ast_log(LOG_ERROR, "Error loading extensions.lua for %s: %s\n", chan->name, error);

				ast_channel_lock(chan);
				ast_channel_datastore_remove(chan, datastore);
				ast_channel_unlock(chan);

				ast_datastore_free(datastore);
				return NULL;
			}
		}

		return datastore->data;
	}
}

static int exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res;
	lua_State *L;
	struct ast_module_user *u = ast_module_user_add(chan);
	if (!u) {
		ast_log(LOG_ERROR, "Error adjusting use count, probably could not allocate memory\n");
		return 0;
	}

	L = lua_get_state(chan);
	if (!L) {
		ast_module_user_remove(u);
		return 0;
	}

	res = lua_find_extension(L, context, exten, priority, &exists, 0);

	if (!chan) lua_close(L);
	ast_module_user_remove(u);
	return res;
}

static int canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res;
	lua_State *L;
	struct ast_module_user *u = ast_module_user_add(chan);
	if (!u) {
		ast_log(LOG_ERROR, "Error adjusting use count, probably could not allocate memory\n");
		return 0;
	}

	L = lua_get_state(chan);
	if (!L) {
		ast_module_user_remove(u);
		return 0;
	}

	res = lua_find_extension(L, context, exten, priority, &canmatch, 0);

	if (!chan) lua_close(L);
	ast_module_user_remove(u);
	return res;
}

static int matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res;
	lua_State *L;
	struct ast_module_user *u = ast_module_user_add(chan);
	if (!u) {
		ast_log(LOG_ERROR, "Error adjusting use count, probably could not allocate memory\n");
		return 0;
	}

	L = lua_get_state(chan);
	if (!L) {
		ast_module_user_remove(u);
		return 0;
	}
	
	res = lua_find_extension(L, context, exten, priority, &matchmore, 0);

	if (!chan) lua_close(L);
	ast_module_user_remove(u);
	return res;
}


static int exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res, error_func;
	lua_State *L;
	struct ast_module_user *u = ast_module_user_add(chan);
	if (!u) {
		ast_log(LOG_ERROR, "Error adjusting use count, probably could not allocate memory\n");
		return -1;
	}
	
	L = lua_get_state(chan);
	if (!L) {
		ast_module_user_remove(u);
		return -1;
	}

	lua_pushcfunction(L, &lua_error_function);
	error_func = lua_gettop(L);

	/* push the extension function onto the stack */
	if (!lua_find_extension(L, context, exten, priority, &exists, 1)) {
		lua_pop(L, 1); /* pop the debug function */
		ast_log(LOG_ERROR, "Could not find extension %s in context %s\n", exten, context);
		if (!chan) lua_close(L);
		ast_module_user_remove(u);
		return -1;
	}
		
	lua_update_registry(L, context, exten, priority);
	
	lua_pushstring(L, context);
	lua_pushstring(L, exten);
	
	res = lua_pcall(L, 2, 0, error_func);
	if (res) {
		if (res == LUA_ERRRUN) {
			res = -1;
			if (lua_isnumber(L, -1)) {
				res = lua_tointeger(L, -1);
			} else if (lua_isstring(L, -1)) {
				const char *error = lua_tostring(L, -1);
				ast_log(LOG_ERROR, "Error executing lua extension: %s\n", error);
			}
		} else if (res == LUA_ERRERR) {
			res = -1;
			ast_log(LOG_ERROR, "Error in the lua error handler (this is probably a bug in pbx_lua)\n");
		} else if (res == LUA_ERRMEM) {
			res = -1;
			ast_log(LOG_ERROR, "Memory allocation error\n");
		}
		lua_pop(L, 1);
	}
	lua_remove(L, error_func);
	if (!chan) lua_close(L);
	ast_module_user_remove(u);
	return res;
}

/*!
 * \brief Locate an extensions and optionally push the matching function on the
 * stack
 *
 * \param L the lua_State to use
 * \param context the context to look in
 * \param exten the extension to look up
 * \param priority the priority to check, '1' is the only valid priority
 * \param func the calling func, used to adjust matching behavior between,
 * match, canmatch, and matchmore
 * \param push_func whether or not to push the lua function for the given
 * extension onto the stack
 */
static int lua_find_extension(lua_State *L, const char *context, const char *exten, int priority, ast_switch_f *func, int push_func)
{
	int context_table, context_order_table, i;

	ast_debug(2, "Looking up %s@%s:%i\n", exten, context, priority);
	if (priority != 1)
		return 0;

	/* load the 'extensions' table */
	lua_getglobal(L, "extensions");
	if (lua_isnil(L, -1)) {
		ast_log(LOG_ERROR, "Unable to find 'extensions' table in extensions.lua\n");
		lua_pop(L, 1);
		return 0;
	}

	/* load the given context */
	lua_getfield(L, -1, context);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 2);
		return 0;
	}

	/* remove the extensions table */
	lua_remove(L, -2);

	context_table = lua_gettop(L);

	/* load the extensions order table for this context */
	lua_getfield(L, LUA_REGISTRYINDEX, "extensions_order");
	lua_getfield(L, -1, context);

	lua_remove(L, -2);  /* remove the extensions order table */

	context_order_table = lua_gettop(L);
	
	/* step through the extensions looking for a match */
	for (i = 1; i < lua_objlen(L, context_order_table) + 1; i++) {
		int e_index_copy, match = 0;
		const char *e;

		lua_pushinteger(L, i);
		lua_gettable(L, context_order_table);
		lua_gettop(L);

		/* copy the key at the top of the stack for use later */
		lua_pushvalue(L, -1);
		e_index_copy = lua_gettop(L);

		if (!(e = lua_tostring(L, e_index_copy))) {
			lua_pop(L, 2);
			continue;
		}

		/* make sure this is not the 'include' extension */
		if (!strcasecmp(e, "include")) {
			lua_pop(L, 2);
			continue;
		}

		if (func == &matchmore)
			match = ast_extension_close(e, exten, E_MATCHMORE);
		else if (func == &canmatch)
			match = ast_extension_close(e, exten, E_CANMATCH);
		else
			match = ast_extension_match(e, exten);

		/* the extension matching functions return 0 on fail, 1 on
		 * match, 2 on earlymatch */

		if (!match) {
			/* pop the copy and the extension */
			lua_pop(L, 2);
			continue;	/* keep trying */
		}

		if (func == &matchmore && match == 2) {
			/* We match an extension ending in '!'. The decision in
			 * this case is final and counts as no match. */
			lua_pop(L, 4);
			return 0;
		}

		/* remove the context table, the context order table, the
		 * extension, and the extension copy (or replace the extension
		 * with the corresponding function) */
		if (push_func) {
			lua_pop(L, 1);  /* pop the copy */
			lua_gettable(L, context_table);
			lua_insert(L, -3);
			lua_pop(L, 2);
		} else {
			lua_pop(L, 4);
		}

		return 1;
	}

	/* load the includes for this context */
	lua_getfield(L, context_table, "include");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 3);
		return 0;
	}

	/* remove the context and the order table*/
	lua_remove(L, context_order_table);
	lua_remove(L, context_table);

	/* Now try any includes we have in this context */
	for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
		const char *c = lua_tostring(L, -1);
		if (!c)
			continue;

		if (lua_find_extension(L, c, exten, priority, func, push_func)) {
			/* remove the value, the key, and the includes table
			 * from the stack.  Leave the function behind if
			 * necessary */

			if (push_func)
				lua_insert(L, -4);

			lua_pop(L, 3);
			return 1;
		}
	}

	/* pop the includes table */
	lua_pop(L, 1);
	return 0;
}

static struct ast_switch lua_switch = {
        .name		= "Lua",
        .description	= "Lua PBX Switch",
        .exists		= exists,
        .canmatch	= canmatch,
        .exec		= exec,
        .matchmore	= matchmore,
};


static int load_or_reload_lua_stuff(void)
{
	int res = AST_MODULE_LOAD_SUCCESS;

	lua_State *L = luaL_newstate();
	if (!L) {
		ast_log(LOG_ERROR, "Error allocating lua_State, no memory\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (lua_reload_extensions(L)) {
		const char *error = lua_tostring(L, -1);
		ast_log(LOG_ERROR, "Error loading extensions.lua: %s\n", error);
		res = AST_MODULE_LOAD_DECLINE;
	}

	lua_close(L);
	return res;
}

static int unload_module(void)
{
	ast_context_destroy(NULL, registrar);
	ast_unregister_switch(&lua_switch);
	lua_free_extensions();
	return 0;
}

static int reload(void)
{
	return load_or_reload_lua_stuff();
}

static int load_module(void)
{
	int res;

	if ((res = load_or_reload_lua_stuff()))
		return res;

	if (ast_register_switch(&lua_switch)) {
		ast_log(LOG_ERROR, "Unable to register LUA PBX switch\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Lua PBX Switch",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

