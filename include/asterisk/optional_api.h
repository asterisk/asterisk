/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
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
 * To accomodate this situation, the AST_OPTIONAL_API macro allows an API
 * function to be declared in a special way, if Asterisk being built on a
 * platform that supports the GCC 'weak' and 'alias' attributes. If so,
 * the API function is actually a weak symbol, which means if the provider
 * of the API is not loaded, the symbol can still be referenced (unlike a
 * strong symbol, which would cause an immediate fault if not defined when
 * referenced), but it will return NULL signifying the linker/loader was
 * not able to resolve the symbol. In addition, the macro defines a hidden
 * 'stub' version of the API call, using a provided function body, and uses
 * the alias attribute to make the API function symbol actually resolve to
 * that hidden stub, but only when the *real* provider of the symbol has
 * not been found.
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
 * In the module providing the API, the AST_OPTIONAL_API macro must
 * be informed that it should not build the hidden stub function or
 * apply special aliases to the function prototype; this can be done
 * by defining AST_API_MODULE just before including the header file
 * containing the AST_OPTIONAL_API macro calls.
 *
 * \note If the GCC 'weak' and 'alias' attributes are not available,
 * then the AST_OPTIONAL_API macro will result in a non-optional function
 * definition; this means that any consumers of the API functions so
 * defined will require that the provider of the API functions be
 * loaded before they can reference the symbols.
 */

#define __stringify_1(x)	#x
#define __stringify(x)		__stringify_1(x)

/*!
 * \brief A common value for optional API stub functions to return
 *
 * This value is defined as INT_MIN, the minimum value for an integer
 * (maximum negative value), which can be used by any optional API
 * functions that return a signed integer value and would not be
 * able to return such a value under normal circumstances.
 */
#define AST_OPTIONAL_API_UNAVAILABLE	INT_MIN

#if defined(HAVE_ATTRIBUTE_weak) && defined(HAVE_ATTRIBUTE_alias) && !defined(AST_API_MODULE)
#define AST_OPTIONAL_API(result, name, proto, stub)	\
	static result __##name proto stub;		\
	result __attribute__((weak, alias("__" __stringify(name)))) name proto;
#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub)		\
	static result __attribute__((attr)) __##name proto stub;	\
	result __attribute__((weak, alias("__" __stringify(name)), attr)) name proto;
#else
/*!
 * \brief Define an optional API function
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
#define AST_OPTIONAL_API(result, name, proto, stub) result name proto;
/*!
 * \brief Define an optional API function with compiler attributes
 *
 * \param result The type of result the function returns
 * \param attr Any compiler attributes to be applied to the function (without the __attribute__ wrapper)
 * \param name The name of the function
 * \param proto The prototype (arguments) of the function
 * \param stub The code block that will be used by the hidden stub when needed
 */
#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub) result __attribute__((attr)) name proto;
#endif

#undef AST_API_MODULE

#endif /* __ASTERISK_OPTIONAL_API_H */
