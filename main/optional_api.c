/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/optional_api.h"
#include "asterisk/utils.h"

#if defined(OPTIONAL_API)

/*
 * \file Optional API innards.
 *
 * The calls to ast_optional_api_*() happen implicitly from \c __constructor__
 * calls which are defined in header files. This means that some number of them
 * happen before main() is called. This makes calling most Asterisk APIs
 * dangerous, since we could be called before they are initialized. This
 * includes things like AO2, malloc debug, and static mutexes.
 *
 * Another limitation is that most functions are called from the midst of
 * dlopen() or dlclose(), and there is no opportunity to return a failure code.
 * The best we can do is log an error, and call ast_do_crash().
 *
 * Fortunately, there are some constraints that help us out. The \c
 * ast_optional_api_*() are called during module loads, which happens either
 * before main(), or during dlopen() calls. These are already serialized, so we
 * don't have to lock ourselves.
 */

/*! \brief A user of an optional API */
struct optional_api_user {
	/*! Pointer to function pointer to link */
	ast_optional_fn *optional_ref;
	/*! Stub to use when impl is unavailable */
	ast_optional_fn stub;
	/*! Name of the module using the API */
	char module[];
};

/*! \brief An optional API */
struct optional_api {
	/*! Pointer to the implementation function; could be null */
	ast_optional_fn impl;
	/*! Variable length array of users of this API */
	struct optional_api_user **users;
	/*! Allocated size of the \a users array */
	size_t users_maxlen;
	/*! Number of entries in the \a users array */
	size_t users_len;
	/*! Name of the optional API function */
	char symname[];
};

/*!
 * \brief Free an \ref optional_api_user.
 *
 * \param user User struct to free.
 */
static void optional_api_user_destroy(struct optional_api_user *user)
{
	*user->optional_ref = user->stub;
	ast_std_free(user);
}

/*!
 * \brief Create an \ref optional_api_user.
 *
 * \param optional_ref Pointer-to-function-pointer to link to impl/stub.
 * \param stub Stub function to link to when impl is not available.
 * \param module Name of the module requesting the API.
 *
 * \return New \ref optional_api_user.
 * \return \c NULL on error.
 */
static struct optional_api_user *optional_api_user_create(
	ast_optional_fn *optional_ref, ast_optional_fn stub, const char *module)
{
	struct optional_api_user *user;
	size_t size = sizeof(*user) + strlen(module) + 1;

	user = ast_std_calloc(1, size);
	if (!user) {
		return NULL;
	}

	user->optional_ref = optional_ref;
	user->stub = stub;
	strcpy(user->module, module); /* SAFE */

	return user;
}

/*!
 * \brief Free an \ref optional_api.
 *
 * \param api API struct to free.
 */
static void optional_api_destroy(struct optional_api *api)
{
	while (api->users_len--) {
		optional_api_user_destroy(api->users[api->users_len]);
	}
	ast_std_free(api->users);
	api->users = NULL;
	api->users_maxlen = 0;
	ast_std_free(api);
}

/*!
 * \brief Create an \ref optional_api.
 *
 * \param symname Name of the optional function.
 * \return New \ref optional_api.
 * \return \c NULL on error.
 */
static struct optional_api *optional_api_create(const char *symname)
{
	struct optional_api *api;
	size_t size;

	size = sizeof(*api) + strlen(symname) + 1;
	api = ast_std_calloc(1, size);
	if (!api) {
		ast_log(LOG_ERROR, "Failed to allocate api\n");
		return NULL;
	}

	strcpy(api->symname, symname); /* SAFE */

	return api;
}

/*! Array of \ref optional_api functions */
struct {
	/*! Variable length array of API's */
	struct optional_api **list;
	/*! Allocated size of the \a list array */
	size_t maxlen;
	/*! Number of entries in the \a list array */
	size_t len;
} apis;

/*!
 * \brief Gets (or creates) the \ref optional_api for the given function.
 *
 * \param sysname Name of the function to look up.
 * \return Corresponding \ref optional_api.
 * \return \c NULL on error.
 */
static struct optional_api *get_api(const char *symname)
{
	struct optional_api *api;
	size_t i;

	/* Find one, if we already have it */
	if (apis.list) {
		for (i = 0; i < apis.len; ++i) {
			if (strcmp(symname, apis.list[i]->symname) == 0) {
				return apis.list[i];
			}
		}
	}

	/* API not found. Build one */
	api = optional_api_create(symname);
	if (!api) {
		return NULL;
	}

	/* Grow the list, if needed */
	if (apis.len + 1 > apis.maxlen) {
		size_t new_maxlen = apis.maxlen ? 2 * apis.maxlen : 1;
		struct optional_api **new_list;

		new_list = ast_std_realloc(apis.list, new_maxlen * sizeof(*new_list));
		if (!new_list) {
			optional_api_destroy(api);
			ast_log(LOG_ERROR, "Failed to allocate api list\n");
			return NULL;
		}

		apis.maxlen = new_maxlen;
		apis.list = new_list;
	}

	apis.list[apis.len++] = api;

	return api;
}

/*!
 * \brief Re-links a given \a user against its associated \a api.
 *
 * If the \a api has an implementation, the \a user is linked to that
 * implementation. Otherwise, the \a user is linked to its \a stub.
 *
 * \param user \ref optional_api_user to link.
 * \param api \ref optional_api to link.
 */
static void optional_api_user_relink(struct optional_api_user *user,
	struct optional_api *api)
{
	if (api->impl && *user->optional_ref != api->impl) {
		*user->optional_ref = api->impl;
	} else if (!api->impl && *user->optional_ref != user->stub) {
		*user->optional_ref = user->stub;
	}
}

/*!
 * \brief Sets the implementation function pointer for an \a api.
 *
 * \param api API to implement/stub out.
 * \param impl Pointer to implementation function. Can be 0 to remove
 *             implementation.
 */
static void optional_api_set_impl(struct optional_api *api,
	ast_optional_fn impl)
{
	size_t i;

	api->impl = impl;

	/* re-link all users */
	for (i = 0; i < api->users_len; ++i) {
		optional_api_user_relink(api->users[i], api);
	}
}

void ast_optional_api_provide(const char *symname, ast_optional_fn impl)
{
	struct optional_api *api;

	api = get_api(symname);
	if (!api) {
		ast_log(LOG_ERROR, "%s: Allocation failed\n", symname);
		ast_do_crash();
		return;
	}

	optional_api_set_impl(api, impl);
}

void ast_optional_api_unprovide(const char *symname, ast_optional_fn impl)
{
	struct optional_api *api;

	api = get_api(symname);
	if (!api) {
		ast_log(LOG_ERROR, "%s: Could not find api\n", symname);
		ast_do_crash();
		return;
	}

	optional_api_set_impl(api, 0);
}

void ast_optional_api_use(const char *symname, ast_optional_fn *optional_ref,
	ast_optional_fn stub, const char *module)
{
	struct optional_api_user *user;
	struct optional_api *api;


	api = get_api(symname);
	if (!api) {
		ast_log(LOG_ERROR, "%s: Allocation failed\n", symname);
		ast_do_crash();
		return;
	}

	user = optional_api_user_create(optional_ref, stub, module);
	if (!user) {
		ast_log(LOG_ERROR, "%s: Allocation failed\n", symname);
		ast_do_crash();
		return;
	}

	/* Add user to the API */
	if (api->users_len + 1 > api->users_maxlen) {
		size_t new_maxlen = api->users_maxlen ? 2 * api->users_maxlen : 1;
		struct optional_api_user **new_list;

		new_list = ast_std_realloc(api->users, new_maxlen * sizeof(*new_list));
		if (!new_list) {
			optional_api_user_destroy(user);
			ast_log(LOG_ERROR, "Failed to allocate api list\n");
			ast_do_crash();
			return;
		}

		api->users_maxlen = new_maxlen;
		api->users = new_list;
	}

	api->users[api->users_len++] = user;

	optional_api_user_relink(user, api);
}

void ast_optional_api_unuse(const char *symname, ast_optional_fn *optional_ref,
	const char *module)
{
	struct optional_api *api;
	size_t i;

	api = get_api(symname);
	if (!api) {
		ast_log(LOG_ERROR, "%s: Could not find api\n", symname);
		ast_do_crash();
		return;
	}

	for (i = 0; i < api->users_len; ++i) {
		struct optional_api_user *user = api->users[i];

		if (user->optional_ref == optional_ref) {
			if (*user->optional_ref != user->stub) {
				*user->optional_ref = user->stub;
			}

			/* Remove from the list */
			api->users[i] = api->users[--api->users_len];

			optional_api_user_destroy(user);
			return;
		}
	}

	ast_log(LOG_ERROR, "%s: Could not find user %s\n", symname, module);
}

#endif /* defined(OPTIONAL_API) */
