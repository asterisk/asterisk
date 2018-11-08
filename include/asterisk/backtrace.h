/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Asterisk backtrace generation
 *
 * This file provides backtrace generation utilities
 */


#ifndef BACKTRACE_H_
#define BACKTRACE_H_

#define AST_MAX_BT_FRAMES 32

#ifdef HAVE_BKTR
#define ast_bt_get_addresses(bt) __ast_bt_get_addresses((bt))
#define ast_bt_create() __ast_bt_create()
#define ast_bt_destroy(bt) __ast_bt_destroy((bt))
#define ast_bt_get_symbols(addresses, num_frames) __ast_bt_get_symbols((addresses), (num_frames))
#define ast_bt_free_symbols(string_vector) __ast_bt_free_symbols((string_vector))
#else
#define ast_bt_get_addresses(bt) 0
#define ast_bt_create() NULL
#define ast_bt_destroy(bt) NULL
#define ast_bt_get_symbols(addresses, num_frames) NULL
#define ast_bt_free_symbols(string_vector) NULL
#endif

/* \brief
 *
 * A structure to hold backtrace information. This structure provides an easy means to
 * store backtrace information or pass backtraces to other functions.
 */
struct ast_bt {
	/*! The addresses of the stack frames. This is filled in by calling the glibc backtrace() function */
	void *addresses[AST_MAX_BT_FRAMES];
	/*! The number of stack frames in the backtrace */
	int num_frames;
	/*! Tells if the ast_bt structure was dynamically allocated */
	unsigned int alloced:1;
};

#ifdef HAVE_BKTR

/* \brief
 * Allocates memory for an ast_bt and stores addresses and symbols.
 *
 * \return Returns NULL on failure, or the allocated ast_bt on success
 * \since 1.6.1
 */
struct ast_bt *__ast_bt_create(void);

/* \brief
 * Fill an allocated ast_bt with addresses
 *
 * \retval 0 Success
 * \retval -1 Failure
 * \since 1.6.1
 */
int __ast_bt_get_addresses(struct ast_bt *bt);

/* \brief
 *
 * Free dynamically allocated portions of an ast_bt
 *
 * \retval NULL.
 * \since 1.6.1
 */
void *__ast_bt_destroy(struct ast_bt *bt);

/* \brief Retrieve symbols for a set of backtrace addresses
 *
 * \param addresses A list of addresses, such as the ->addresses structure element of struct ast_bt.
 * \param num_frames Number of addresses in the addresses list
 *
 * \retval NULL Unable to allocate memory
 * \return Vector of strings. Free with ast_bt_free_symbols
 *
 * \note The first frame in the addresses array will usually point to __ast_bt_create
 * so when printing the symbols you may wish to start at position 1 rather than 0.
 *
 * \since 1.6.2.16
 */
struct ast_vector_string *__ast_bt_get_symbols(void **addresses, size_t num_frames);

/* \brief Free symbols returned from ast_bt_get_symbols
 *
 * \param symbols The symbol string vector
 *
 * \since 13.24.0
 */
void __ast_bt_free_symbols(struct ast_vector_string *symbols);

#endif /* HAVE_BKTR */

#endif /* BACKTRACE_H_ */
