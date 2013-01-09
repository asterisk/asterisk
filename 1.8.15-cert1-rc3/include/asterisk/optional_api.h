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
 * platform that supports special compiler and dynamic linker attributes.
 * If so the API function will actually be a weak symbol, which means if the
 * provider of the API is not loaded, the symbol can still be referenced (unlike a
 * strong symbol, which would cause an immediate fault if not defined when
 * referenced), but it will return NULL signifying the linker/loader was
 * not able to resolve the symbol. In addition, the macro defines a hidden
 * 'stub' version of the API call, using a provided function body, and uses
 * various methods to make the API function symbol actually resolve to
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
 *
 * \note If the platform does not provide adequate resources,
 * then the AST_OPTIONAL_API macro will result in a non-optional function
 * definition; this means that any consumers of the API functions so
 * defined will require that the provider of the API functions be
 * loaded before they can reference the symbols.
 *
 * WARNING WARNING WARNING WARNING WARNING
 *
 * You MUST add the AST_MODFLAG_GLOBAL_SYMBOLS to the module for which you
 * are enabling optional_api functionality, or it will fail to work.
 *
 * WARNING WARNING WARNING WARNING WARNING
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


#if defined(HAVE_ATTRIBUTE_weak_import) || defined(HAVE_ATTRIBUTE_weak)

/*
 * This is the Darwin (Mac OS/X) implementation, that only provides the 'weak'
 * or 'weak_import' compiler attribute for weak symbols. On this platform,
 *
 * - The module providing the API will only provide a '__' prefixed version
 *   of the API function to other modules (this will be hidden from the other
 *   modules by the macros), so any modules compiled against older versions
 *   of the module that provided a non-prefixed version of the API function
 *   will fail to link at runtime.
 * - In the API module itself, access to the API function without using a
 *   prefixed name is provided by a static pointer variable that holds the
 *   function address.
 * - 'Consumer' modules of the API will use a combination of a weak_import or
 *   weak symbol, a local stub function, a pointer variable and a constructor
 *   function (which initializes that pointer variable as the module is being
 *   loaded) to provide safe, optional access to the API function without any
 *   special code being required.
 */

#if defined(HAVE_ATTRIBUTE_weak_import)
#define	__default_attribute	weak_import /* pre-Lion */
#else
#define	__default_attribute	weak        /* Lion-onwards */
#endif

#define AST_OPTIONAL_API_NAME(name) __##name

#if defined(AST_API_MODULE)

#define AST_OPTIONAL_API(result, name, proto, stub) \
	result AST_OPTIONAL_API_NAME(name) proto; \
	static attribute_unused typeof(AST_OPTIONAL_API_NAME(name)) * const name = AST_OPTIONAL_API_NAME(name);

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub)	\
	result __attribute__((attr)) AST_OPTIONAL_API_NAME(name) proto; \
	static attribute_unused typeof(AST_OPTIONAL_API_NAME(name)) * const name = AST_OPTIONAL_API_NAME(name);

#else

#define AST_OPTIONAL_API(result, name, proto, stub) \
	static result __stub__##name proto stub; \
	__attribute__((__default_attribute)) typeof(__stub__##name) AST_OPTIONAL_API_NAME(name); \
	static attribute_unused typeof(__stub__##name) * name; \
	static void __attribute__((constructor)) __init__##name(void) { name = AST_OPTIONAL_API_NAME(name) ? : __stub__##name; }

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub) \
	static __attribute__((attr)) result __stub__##name proto stub; \
	__attribute__((attr, __default_attribute)) typeof(__stub__##name) AST_OPTIONAL_API_NAME(name); \
	static attribute_unused __attribute__((attr)) typeof(__stub__##name) * name; \
	static void __attribute__((constructor)) __init__##name(void) { name = AST_OPTIONAL_API_NAME(name) ? : __stub__##name; }

#endif

/* End of Darwin (Mac OS/X) implementation */

#elif defined(HAVE_ATTRIBUTE_weakref)

/*
 * This is the generic GCC implementation, used when the 'weakref'
 * compiler attribute is available. On these platforms:
 *
 * - The module providing the API will provide a '__' prefixed version
 *   of the API function to other modules (this will be hidden from the other
 *   modules by the macros), and also a non-prefixed alias so that modules
 *   compiled against older versions of the module that provided a non-prefixed
 *    version of the API function will continue to link properly.
 * - In the API module itself, access to the API function without using a
 *   prefixed name is provided by the non-prefixed alias described above.
 * - 'Consumer' modules of the API will use a combination of a weakref
 *   symbol, a local stub function, a pointer variable and a constructor function
 *   (which initializes that pointer variable as the module is being loaded)
 *   to provide safe, optional access to the API function without any special
 *   code being required.
 */

#define AST_OPTIONAL_API_NAME(name) __##name

#if defined(AST_API_MODULE)

#define AST_OPTIONAL_API(result, name, proto, stub) \
	result AST_OPTIONAL_API_NAME(name) proto; \
	static __attribute__((alias(__stringify(AST_OPTIONAL_API_NAME(name))))) typeof(AST_OPTIONAL_API_NAME(name)) name;

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub)	\
	result __attribute__((attr)) AST_OPTIONAL_API_NAME(name) proto; \
	static __attribute__((alias(__stringify(AST_OPTIONAL_API_NAME(name))))) typeof(AST_OPTIONAL_API_NAME(name)) name;

#else

#define AST_OPTIONAL_API(result, name, proto, stub) \
	static result __stub__##name proto stub; \
	static __attribute__((weakref(__stringify(AST_OPTIONAL_API_NAME(name))))) typeof(__stub__##name) __ref__##name; \
	static attribute_unused typeof(__stub__##name) * name; \
	static void __attribute__((constructor)) __init__##name(void) { name = __ref__##name ? : __stub__##name; }

#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub) \
	static __attribute__((attr)) result __stub__##name proto stub; \
	static __attribute__((attr, weakref(__stringify(AST_OPTIONAL_API_NAME(name))))) typeof(__stub__##name) __ref__##name; \
	static attribute_unused __attribute__((attr)) typeof(__stub__##name) * name; \
	static void __attribute__((constructor)) __init__##name(void) { name = __ref__##name ? : __stub__##name; }

#endif

/* End of GCC implementation */

#else

/* This is the non-optional implementation. */

#define AST_OPTIONAL_API_NAME(name) name

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
#define AST_OPTIONAL_API(result, name, proto, stub) result AST_OPTIONAL_API_NAME(name) proto

/*!
 * \brief Define an optional API function with compiler attributes
 *
 * \param result The type of result the function returns
 * \param attr Any compiler attributes to be applied to the function (without the __attribute__ wrapper)
 * \param name The name of the function
 * \param proto The prototype (arguments) of the function
 * \param stub The code block that will be used by the hidden stub when needed
 */
#define AST_OPTIONAL_API_ATTR(result, attr, name, proto, stub) result __attribute__((attr)) AST_OPTIONAL_API_NAME(name) proto

/* End of non-optional implementation */

#endif

#undef AST_API_MODULE

#endif /* __ASTERISK_OPTIONAL_API_H */
