/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Persistant data storage (akin to *doze registry)
 */

#ifndef _ASTERISK_ASTDB_H
#define _ASTERISK_ASTDB_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_db_entry {
	struct ast_db_entry *next;
	char *key;
	char data[0];
};

/*!\brief Get key value specified by family/key */
int ast_db_get(const char *family, const char *key, char *out, int outlen);

/*!\brief Store value addressed by family/key*/
int ast_db_put(const char *family, const char *key, char *value);

/*!\brief Delete entry in astdb */
int ast_db_del(const char *family, const char *key);

/*!\brief Delete one or more entries in astdb
 * If both parameters are NULL, the entire database will be purged.  If
 * only keytree is NULL, all entries within the family will be purged.
 * It is an error for keytree to have a value when family is NULL.
 *
 * \retval 0 Entries were deleted
 * \retval -1 An error occurred
 */
int ast_db_deltree(const char *family, const char *keytree);

/*!\brief Get a list of values within the astdb tree
 * If family is specified, only those keys will be returned.  If keytree
 * is specified, subkeys are expected to exist (separated from the key with
 * a slash).  If subkeys do not exist and keytree is specified, the tree will
 * consist of either a single entry or NULL will be returned.
 *
 * Resulting tree should be freed by passing the return value to ast_db_freetree()
 * when usage is concluded.
 */
struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree);

/*!\brief Free structure created by ast_db_gettree() */
void ast_db_freetree(struct ast_db_entry *entry);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_ASTDB_H */
