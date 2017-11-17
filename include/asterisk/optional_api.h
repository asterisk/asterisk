/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008-2013, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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

#ifndef __ASTERISK_OPTIONAL_API_H
#define __ASTERISK_OPTIONAL_API_H

/*!
 * \file
 * \brief Optional API function macros
 *
 * Some Asterisk API functions are provided by loadable modules, thus,
 * they may or may not be available at run time depending on whether the
 * providing module has been loaded or not. In addition, there are some
 * modules that are consumers of these APIs that *optionally* use them; they
 * have only a part of their functionality dependent on the APIs, and can
 * provide the remainder even if the APIs are not available.
 *
 * An example can be found in agi.h:
 *
 * \code
 * AST_OPTIONAL_API(int, ast_agi_register, (struct ast_module *mod, agi_command *cmd),
 *                  { return AST_OPTIONAL_API_UNAVAILABLE; });
 * \endcode
 *
 * This defines the 'ast_agi_register' function as an optional API; if a
 * consumer of this API is loaded when there is no provider of it, then
 * calling this function will actually call the hidden stub, and return
 * the value AST_OPTIONAL_API_UNAVAILABLE. This allows the consumer to
 * safely know that the API is not available, and to avoid using any
 * other APIs from the not-present provider.
 *
 * In addition to this declaration in the header file, the actual definition of
 * the API function must use the AST_OPTIONAL_API_NAME macro to (possibly)
 * modify the real name of the API function, depending on the specific
 * implementation requirements. The corresponding example from res_agi.c:
 *
 * \code
 * int AST_OPTIONAL_API_NAME(ast_agi_register)(struct ast_module *mod, agi_command *cmd)
 * {
 * ...
 * }
 * \endcode
 *
 * In the module providing the API, the AST_OPTIONAL_API macro must
 * be informed that it should not build the hidden stub function or
 * apply special aliases to the function prototype; this can be done
 * by defining AST_API_MODULE just before including the header file
 * containing the AST_OPTIONAL_API macro calls.
 */

/*!
 * \brief A common value for optional API stub functions to return
 *
 * This value is defined as INT_MIN, the minimum value for an integer
 * (maximum negative value), which can be used by any optional API
 * functions that return a signed integer value and would not be
 * able to return such a value under normal circumstances.
 */
#define AST_OPTIONAL_API_UNAVAILABLE	INT_MIN

/*!
 * \def AST_OPTIONAL_API_NAME(name)
 * \brief Expands to the name of the implementation function.
 */

/*!
 * \def AST_OPTIONAL_API(result, name, proto, stub)
 * \brief Declare an optional API function
 *
 * \param result The type of result the function returns
 * \param name The name of the function
 * \param proto The prototype (arguments) of the function
 * \param stub The code block that will be used by the hidden stub when needed
 *
 * Example usage:
 * \code
 * AST_OPTIONAL_API(int, ast_agi_register, (struct ast_module *mod, agi_command *cmd),
 *                  { return AST_OPTIONAL_API_UNAVAILABLE; });
 * \endcode
 */

/*!
 * \def AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub)
 * \brief Declare an optional API function with compiler attributes
 *
 * \param result The type of result the function returns
 * \param attr Any compiler attributes to be applied to the function (without the __attribute__ wrapper)
 * \param name The name of the function
 * \param proto The prototype (arguments) of the function
 * \param stub The code block that will be used by the hidden stub when needed
 */

#if defined(OPTIONAL_API)

/*!
 * \internal
 * \brief Function pointer to an optional API function.
 *
 * Functions that are declared as optional may have any signature they want;
 * they are cast to this type as needed. We don't use a \c void pointer, because
 * technically data and function pointers are incompatible.
 *
 * \note
 * The may_alias attribute is to avoid type punning/strict aliasing warnings
 * with older GCC's.
 */
typedef void (*ast_optional_fn)(void) attribute_may_alias;

/*!
 * \internal
 * \brief Provide an implementation of an optional API.
 *
 * Any declared usages of this function are linked.
 *
 * \param symname Name of the provided function.
 * \param impl Function pointer to the implementation function.
 */
void ast_optional_api_provide(const char *symname, ast_optional_fn impl);

/*!
 * \internal
 * \brief Remove an implementation of an optional API.
 *
 * Any declared usages of this function are unlinked.
 *
 * \param symname Name of the provided function.
 * \param impl Function pointer to the implementation function.
 */
void ast_optional_api_unprovide(const char *symname, ast_optional_fn impl);

/*!
 * \internal
 * \brief Define a usage of an optional API.
 *
 * If the API has been provided, it will be linked into \a optional_ref.
 * Otherwise, it will be linked to \a stub until an implementation is provided.
 *
 * \param symname Name of the function to use.
 * \param optional_ref Pointer-to-function-pointer to link to impl/stub.
 * \param stub Stub function to link to when impl is not available.
 * \param module Name of the module requesting the API.
 */
void ast_optional_api_use(const char *symname, ast_optional_fn *optional_ref,
	ast_optional_fn stub, const char *module);

/*!
 * \internal
 * \brief Remove a usage of an optional API.
 *
 * The \a optional_ref will be linked to the \a stub provided at use time,
 * will no longer be updated if the API is provided/removed.
 *
 * \param symname Name of the function to use.
 * \param optional_ref Pointer-to-function-pointer to link to impl/stub.
 * \param module Name of the module requesting the API.
 */
void ast_optional_api_unuse(const char *symname, ast_optional_fn *optional_ref,
	const char *module);

#define AST_OPTIONAL_API_NAME(name) __##name

#define AST_OPTIONAL_API_INIT_IMPL(name)				\
	static void __attribute__((constructor)) __init__##name##_impl(void) { \
		ast_optional_api_provide(#name,				\
			(ast_optional_fn)AST_OPTIONAL_API_NAME(name));	\
	}								\
	static void __attribute__((destructor)) __dtor__##name##_impl(void) { \
		ast_optional_api_unprovide(#name,			\
			(ast_optional_fn)AST_OPTIONAL_API_NAME(name));	\
	}

#define AST_OPTIONAL_API_INIT_USER(name)				\
	static void __attribute__((constructor)) __init__##name(void) {	\
		ast_optional_api_use(#name, (ast_optional_fn *)&name,	\
			(ast_optional_fn)__stub__##name,		\
			AST_MODULE);					\
	}								\
	static void __attribute__((destructor)) __dtor__##name(void) {	\
		ast_optional_api_unuse(#name, (ast_optional_fn *)&name, \
			AST_MODULE);					\
	}

#define AST_OPTIONAL_API_IMPL(result, name, proto, stub)			\
	result AST_OPTIONAL_API_NAME(name) proto;			\
	static attribute_unused typeof(AST_OPTIONAL_API_NAME(name)) * const \
	     name = AST_OPTIONAL_API_NAME(name);			\
	AST_OPTIONAL_API_INIT_IMPL(name)

#define AST_OPTIONAL_API_USER(result, name, proto, stub)			\
	static result __stub__##name proto stub;			\
	static attribute_unused						\
		typeof(__stub__##name) * name;				\
	AST_OPTIONAL_API_INIT_USER(name)


/* AST_OPTIONAL_API_ATTR */
#define AST_OPTIONAL_API_ATTR_IMPL(result, attr, name, proto, stub)		\
	result  __attribute__((attr)) AST_OPTIONAL_API_NAME(name) proto; \
	static attribute_unused typeof(AST_OPTIONAL_API_NAME(name)) * const \
	     name = AST_OPTIONAL_API_NAME(name);			\
	AST_OPTIONAL_API_INIT_IMPL(name)

#define AST_OPTIONAL_API_ATTR_USER(result, attr, name, proto, stub)		\
	static __attribute__((attr)) result __stub__##name proto stub;	\
	static attribute_unused	__attribute__((attr))			\
		typeof(__stub__##name) * name;				\
	AST_OPTIONAL_API_INIT_USER(name)

#else /* defined(OPTIONAL_API) */

/* Non-optional API */

#define AST_OPTIONAL_API_NAME(name) name

#define AST_OPTIONAL_API(result, name, proto, stub)	\
	result AST_OPTIONAL_API_NAME(name) proto

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub)	\
	result __attribute__((attr)) AST_OPTIONAL_API_NAME(name) proto

#endif /* defined(OPTIONAL_API) */

#endif /* __ASTERISK_OPTIONAL_API_H */

/*
 * Some Asterisk sources are both consumer and provider of optional API's.  The
 * following definitons are intentionally outside the include protected portion
 * of this header so AST_OPTIONAL_API and AST_OPTIONAL_API_ATTR can be redefined
 * each time the header is included.  This also ensures that AST_API_MODULE is
 * undefined after every include of this header.
 */
#if defined(OPTIONAL_API)

#undef AST_OPTIONAL_API
#undef AST_OPTIONAL_API_ATTR

#if defined(AST_API_MODULE)

#define AST_OPTIONAL_API(result, name, proto, stub) \
	AST_OPTIONAL_API_IMPL(result, name, proto, stub)

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub) \
	AST_OPTIONAL_API_ATTR_IMPL(result, attr, name, proto, stub)

#else

#define AST_OPTIONAL_API(result, name, proto, stub) \
	AST_OPTIONAL_API_USER(result, name, proto, stub)

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub) \
	AST_OPTIONAL_API_ATTR_USER(result, attr, name, proto, stub)

#endif /* defined(AST_API_MODULE) */

#endif /* defined(OPTIONAL_API) */

#undef AST_API_MODULE
