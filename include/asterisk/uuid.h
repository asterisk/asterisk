/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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
 * \brief Universally unique identifier support
 */

#ifndef _ASTERISK_UUID_H
#define _ASTERISK_UUID_H

/* Size of an RFC 4122 UUID string plus terminating null byte */
#define AST_UUID_STR_LEN (36 + 1)

struct ast_uuid;

/*!
 * \brief Initialize the UUID system
 */
void ast_uuid_init(void);

/*!
 * \brief Generate a UUID
 *
 * This function allocates memory on the heap. The returned
 * pointer must be freed using ast_free()
 *
 * \retval NULL Generation failed
 * \retval non-NULL heap-allocated UUID
 */
struct ast_uuid *ast_uuid_generate(void);

/*!
 * \brief Convert a UUID to a string
 *
 * \param uuid The UUID to convert to a string
 * \param[out] buf The buffer where the UUID string will be stored
 * \param size The size of the buffer. Must be at least AST_UUID_STR_LEN.
 * \return The UUID string (a pointer to buf)
 */
char *ast_uuid_to_str(struct ast_uuid *uuid, char *buf, size_t size);

/*!
 * \brief Generate a UUID string.
 * \since 12.0.0
 *
 * \param buf The buffer where the UUID string will be stored
 * \param size The size of the buffer. Must be at least AST_UUID_STR_LEN.
 *
 * \return The UUID string (a pointer to buf)
 */
char *ast_uuid_generate_str(char *buf, size_t size);

/*!
 * \brief Convert a string to a UUID
 *
 * This function allocates memory on the heap. The returned
 * pointer must be freed using ast_free()
 *
 * \param str The string to convert to a UUID
 * \retval NULL Failed to convert
 * \retval non-NULL The heap-allocated converted UUID
 */
struct ast_uuid *ast_str_to_uuid(char *str);

/*!
 * \brief Make a copy of a UUID
 *
 * This function allocates memory on the heap. The returned
 * pointer must be freed using ast_free()
 *
 * \param src The source UUID to copy
 * \retval NULL Failed to copy
 * \retval non-NULL The heap-allocated duplicate UUID
 */
struct ast_uuid *ast_uuid_copy(struct ast_uuid *src);

/*!
 * \brief Compare two UUIDs
 *
 * \param left First UUID to compare
 * \param right Second UUID to compare
 * \retval <0 left is lexicographically less than right
 * \retval 0 left and right are the same
 * \retval >0 left is lexicographically greater than right
 */
int ast_uuid_compare(struct ast_uuid *left, struct ast_uuid *right);

/*!
 * \brief Clear a UUID by setting it to be a nil UUID (all 0s)
 *
 * \param uuid UUID to clear
 */
void ast_uuid_clear(struct ast_uuid *uuid);

/*!
 * \brief Check if a UUID is a nil UUID (all 0s)
 *
 * \param uuid UUID to check
 * \retval 0 The UUID is not nil
 * \retval non-zero The UUID is nil
 */
int ast_uuid_is_nil(struct ast_uuid *uuid);
#endif
