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

/*! \brief Get key value specified by family/key */
int ast_db_get(const char *family, const char *key, char *out, int outlen);

/*! \brief Store value addressed by family/key*/
int ast_db_put(const char *family, const char *key, const char *value);

/*! \brief Delete entry in astdb */
int ast_db_del(const char *family, const char *key);

/*! \brief Delete a whole family (for some reason also called "tree" */
int ast_db_deltree(const char *family, const char *keytree);

/*! \brief Get a whole family */
struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree);

/*! \brief Free in-memory data */
void ast_db_freetree(struct ast_db_entry *entry);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_ASTDB_H */
