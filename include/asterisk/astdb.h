/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Persistant data storage (akin to *doze registry)
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
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

int ast_db_get(const char *family, const char *key, char *out, int outlen);

int ast_db_put(const char *family, const char *key, char *value);

int ast_db_del(const char *family, const char *key);

int ast_db_deltree(const char *family, const char *keytree);

struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree);

void ast_db_freetree(struct ast_db_entry *entry);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
