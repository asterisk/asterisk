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

#include "asterisk/optional_api.h"
#include "asterisk/utils.h"
#include "asterisk/vector.h"

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
	/*! Users of the API */
	AST_VECTOR(, struct optional_api_user *) users;
	/*! Name of the optional API function */
	char symname[];
};

/*! Vector of \ref optional_api functions */
AST_VECTOR(, struct optional_api *) apis;

#define USER_OPTIONAL_REF_CMP(ele, value) (ele->optional_ref == value)
#define OPTIONAL_API_SYMNAME_CMP(ele, value) (!strcmp(ele->symname, value))

/*!
 * \brief Free an \ref optional_api_user.
 *
 * \param user User struct to free.
 */
static void optional_api_user_destroy(struct optional_api_user *user)
{
	*user->optional_ref = user->stub;
	ast_free(user);
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

	user = ast_calloc(1, size);
	if (!user) {
		ast_do_crash();

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
	AST_VECTOR_REMOVE_CMP_UNORDERED(&apis, api,
		AST_VECTOR_ELEM_DEFAULT_CMP, AST_VECTOR_ELEM_CLEANUP_NOOP);
	AST_VECTOR_CALLBACK_VOID(&api->users, optional_api_user_destroy);
	AST_VECTOR_FREE(&api->users);
	ast_free(api);
}

/*!
 * \brief Create and link an \ref optional_api.
 *
 * \param symname Name of the optional function.
 * \return New \ref optional_api.
 * \return \c NULL on error.
 */
static struct optional_api *optional_api_create(const char *symname)
{
	struct optional_api *api;

	api = ast_calloc(1, sizeof(*api) + strlen(symname) + 1);
	if (!api || AST_VECTOR_APPEND(&apis, api)) {
		ast_free(api);
		ast_do_crash();

		return NULL;
	}

	strcpy(api->symname, symname); /* SAFE */

	return api;
}

/*!
 * \brief Gets (or creates) the \ref optional_api for the given function.
 *
 * \param sysname Name of the function to look up.
 * \return Corresponding \ref optional_api.
 * \return \c NULL on error.
 */
static struct optional_api *get_api(const char *symname)
{
	struct optional_api **api;

	/* Find one, if we already have it */
	api = AST_VECTOR_GET_CMP(&apis, symname, OPTIONAL_API_SYMNAME_CMP);
	if (api) {
		return *api;
	}

	/* API not found. Build one */
	return optional_api_create(symname);
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
	api->impl = impl;

	/* re-link all users */
	if (AST_VECTOR_SIZE(&api->users)) {
		AST_VECTOR_CALLBACK_VOID(&api->users, optional_api_user_relink, api);
	} else if (!impl) {
		/* No users or impl means we should delete this api. */
		optional_api_destroy(api);
	}
}

void ast_optional_api_provide(const char *symname, ast_optional_fn impl)
{
	struct optional_api *api;

	api = get_api(symname);
	if (api) {
		optional_api_set_impl(api, impl);
	}
}

void ast_optional_api_unprovide(const char *symname, ast_optional_fn impl)
{
	struct optional_api *api;

	api = get_api(symname);
	if (api) {
		optional_api_set_impl(api, 0);
	}
}

void ast_optional_api_use(const char *symname, ast_optional_fn *optional_ref,
	ast_optional_fn stub, const char *module)
{
	struct optional_api_user *user;
	struct optional_api *api;

	api = get_api(symname);
	if (!api) {
		return;
	}

	user = optional_api_user_create(optional_ref, stub, module);
	if (!user) {
		return;
	}

	/* Add user to the API */
	if (!AST_VECTOR_APPEND(&api->users, user)) {
		optional_api_user_relink(user, api);
	} else {
		optional_api_user_destroy(user);
		ast_do_crash();
	}
}

void ast_optional_api_unuse(const char *symname, ast_optional_fn *optional_ref,
	const char *module)
{
	struct optional_api *api;

	api = get_api(symname);
	if (api) {
		AST_VECTOR_REMOVE_CMP_UNORDERED(&api->users, optional_ref, USER_OPTIONAL_REF_CMP, optional_api_user_destroy);
		if (!api->impl && !AST_VECTOR_SIZE(&api->users)) {
			optional_api_destroy(api);
		}
	}
}
#endif /* defined(OPTIONAL_API) */
