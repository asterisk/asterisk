/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Tilghman Lesher
 *
 * Tilghman Lesher <cdr_adaptive_odbc__v1@the-tilghman.com>
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

/*!
 * \file
 * \brief Adaptive ODBC CDR backend
 *
 * \author Tilghman Lesher <cdr_adaptive_odbc__v1@the-tilghman.com>
 * \ingroup cdr_drivers
 */

/*! \li \ref cdr_adaptive_odbc.c uses the configuration file \ref cdr_adaptive_odbc.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr_adaptive_odbc.conf cdr_adaptive_odbc.conf
 * \verbinclude cdr_adaptive_odbc.conf.sample
 */

/*** MODULEINFO
	<depend>res_odbc</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/types.h>
#include <time.h>

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/res_odbc.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"

#define	CONFIG	"cdr_adaptive_odbc.conf"

static const char name[] = "Adaptive ODBC";
/* Optimization to reduce number of memory allocations */
static int maxsize = 512, maxsize2 = 512;

struct columns {
	char *name;
	char *cdrname;
	char *filtervalue;
	char *staticvalue;
	SQLSMALLINT type;
	SQLINTEGER size;
	SQLSMALLINT decimals;
	SQLSMALLINT radix;
	SQLSMALLINT nullable;
	SQLINTEGER octetlen;
	AST_LIST_ENTRY(columns) list;
	unsigned int negatefiltervalue:1;
};

struct tables {
	char *connection;
	char *table;
	char *schema;
	char quoted_identifiers;
	unsigned int usegmtime:1;
	AST_LIST_HEAD_NOLOCK(odbc_columns, columns) columns;
	AST_RWLIST_ENTRY(tables) list;
};

static AST_RWLIST_HEAD_STATIC(odbc_tables, tables);

static int load_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *tmp, *catg;
	struct tables *tableptr;
	struct columns *entry;
	struct odbc_obj *obj;
	char columnname[80];
	char connection[40];
	char table[40];
	char schema[40];
	char quoted_identifiers;
	int lenconnection, lentable, lenschema, usegmtime = 0;
	SQLLEN sqlptr;
	int res = 0;
	SQLHSTMT stmt = NULL;
	struct ast_flags config_flags = { 0 }; /* Part of our config comes from the database */

	cfg = ast_config_load(CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load " CONFIG ".  No adaptive ODBC CDRs.\n");
		return -1;
	}

	for (catg = ast_category_browse(cfg, NULL); catg; catg = ast_category_browse(cfg, catg)) {
		var = ast_variable_browse(cfg, catg);
		if (!var)
			continue;

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "connection"))) {
			ast_log(LOG_WARNING, "No connection parameter found in '%s'.  Skipping.\n", catg);
			continue;
		}
		ast_copy_string(connection, tmp, sizeof(connection));
		lenconnection = strlen(connection);

		if (!ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "usegmtime"))) {
			usegmtime = ast_true(tmp);
		}

		/* When loading, we want to be sure we can connect. */
		obj = ast_odbc_request_obj(connection, 1);
		if (!obj) {
			ast_log(LOG_WARNING, "No such connection '%s' in the '%s' section of " CONFIG ".  Check res_odbc.conf.\n", connection, catg);
			continue;
		}

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "table"))) {
			ast_log(LOG_NOTICE, "No table name found.  Assuming 'cdr'.\n");
			tmp = "cdr";
		}
		ast_copy_string(table, tmp, sizeof(table));
		lentable = strlen(table);

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "schema"))) {
			tmp = "";
		}
		ast_copy_string(schema, tmp, sizeof(schema));
		lenschema = strlen(schema);

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "quoted_identifiers"))) {
			tmp = "";
		}
		quoted_identifiers = tmp[0];
		if (strlen(tmp) > 1) {
			ast_log(LOG_ERROR, "The quoted_identifiers setting only accepts a single character,"
				" while a value of '%s' was provided. This option has been disabled as a result.\n", tmp);
			quoted_identifiers = '\0';
		}

		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed on connection '%s'!\n", connection);
			ast_odbc_release_obj(obj);
			continue;
		}

		res = SQLColumns(stmt, NULL, 0, lenschema == 0 ? NULL : (unsigned char *)schema, SQL_NTS, (unsigned char *)table, SQL_NTS, (unsigned char *)"%", SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_ERROR, "Unable to query database columns on connection '%s'.  Skipping.\n", connection);
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			continue;
		}

		tableptr = ast_calloc(sizeof(char), sizeof(*tableptr) + lenconnection + 1 + lentable + 1 + lenschema + 1 + 1);
		if (!tableptr) {
			ast_log(LOG_ERROR, "Out of memory creating entry for table '%s' on connection '%s'%s%s%s\n", table, connection,
				lenschema ? " (schema '" : "", lenschema ? schema : "", lenschema ? "')" : "");
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			res = -1;
			break;
		}

		tableptr->usegmtime = usegmtime;
		tableptr->connection = (char *)tableptr + sizeof(*tableptr);
		tableptr->table = (char *)tableptr + sizeof(*tableptr) + lenconnection + 1;
		tableptr->schema = (char *)tableptr + sizeof(*tableptr) + lenconnection + 1 + lentable + 1;
		ast_copy_string(tableptr->connection, connection, lenconnection + 1);
		ast_copy_string(tableptr->table, table, lentable + 1);
		ast_copy_string(tableptr->schema, schema, lenschema + 1);
		tableptr->quoted_identifiers = quoted_identifiers;

		ast_verb(3, "Found adaptive CDR table %s@%s.\n", tableptr->table, tableptr->connection);

		/* Check for filters first */
		for (var = ast_variable_browse(cfg, catg); var; var = var->next) {
			if (strncmp(var->name, "filter", 6) == 0) {
				int negate = 0;
				char *cdrvar = ast_strdupa(var->name + 6);
				cdrvar = ast_strip(cdrvar);
				if (cdrvar[strlen(cdrvar) - 1] == '!') {
					negate = 1;
					cdrvar[strlen(cdrvar) - 1] = '\0';
					ast_trim_blanks(cdrvar);
				}

				ast_verb(3, "Found filter %s'%s' for CDR variable %s in %s@%s\n", negate ? "!" : "", var->value, cdrvar, tableptr->table, tableptr->connection);

				entry = ast_calloc(sizeof(char), sizeof(*entry) + strlen(cdrvar) + 1 + strlen(var->value) + 1);
				if (!entry) {
					ast_log(LOG_ERROR, "Out of memory creating filter entry for CDR variable '%s' in table '%s' on connection '%s'\n", cdrvar, table, connection);
					res = -1;
					break;
				}

				/* NULL column entry means this isn't a column in the database */
				entry->name = NULL;
				entry->cdrname = (char *)entry + sizeof(*entry);
				entry->filtervalue = (char *)entry + sizeof(*entry) + strlen(cdrvar) + 1;
				strcpy(entry->cdrname, cdrvar);
				strcpy(entry->filtervalue, var->value);
				entry->negatefiltervalue = negate;

				AST_LIST_INSERT_TAIL(&(tableptr->columns), entry, list);
			}
		}

		while ((res = SQLFetch(stmt)) != SQL_NO_DATA && res != SQL_ERROR) {
			char *cdrvar = "", *staticvalue = "";

			SQLGetData(stmt,  4, SQL_C_CHAR, columnname, sizeof(columnname), &sqlptr);

			/* Is there an alias for this column? */

			/* NOTE: This seems like a non-optimal parse method, but I'm going
			 * for user configuration readability, rather than fast parsing. We
			 * really don't parse this file all that often, anyway.
			 */
			for (var = ast_variable_browse(cfg, catg); var; var = var->next) {
				if (strncmp(var->name, "alias", 5) == 0 && strcasecmp(var->value, columnname) == 0) {
					char *alias = ast_strdupa(var->name + 5);
					cdrvar = ast_strip(alias);
					ast_verb(3, "Found alias %s for column %s in %s@%s\n", cdrvar, columnname, tableptr->table, tableptr->connection);
					break;
				} else if (strncmp(var->name, "static", 6) == 0 && strcasecmp(var->value, columnname) == 0) {
					char *item = ast_strdupa(var->name + 6);
					item = ast_strip(item);
					if (item[0] == '"' && item[strlen(item) - 1] == '"') {
						/* Remove surrounding quotes */
						item[strlen(item) - 1] = '\0';
						item++;
					}
					staticvalue = item;
				}
			}

			entry = ast_calloc(sizeof(char), sizeof(*entry) + strlen(columnname) + 1 + strlen(cdrvar) + 1 + strlen(staticvalue) + 1);
			if (!entry) {
				ast_log(LOG_ERROR, "Out of memory creating entry for column '%s' in table '%s' on connection '%s'\n", columnname, table, connection);
				res = -1;
				SQLFreeHandle(SQL_HANDLE_STMT, stmt);
				break;
			}
			entry->name = (char *)entry + sizeof(*entry);
			strcpy(entry->name, columnname);

			if (!ast_strlen_zero(cdrvar)) {
				entry->cdrname = entry->name + strlen(columnname) + 1;
				strcpy(entry->cdrname, cdrvar);
			} else { /* Point to same place as the column name */
				entry->cdrname = (char *)entry + sizeof(*entry);
			}

			if (!ast_strlen_zero(staticvalue)) {
				entry->staticvalue = entry->cdrname + strlen(entry->cdrname) + 1;
				strcpy(entry->staticvalue, staticvalue);
			}

			SQLGetData(stmt,  5, SQL_C_SHORT, &entry->type, sizeof(entry->type), NULL);
			SQLGetData(stmt,  7, SQL_C_LONG, &entry->size, sizeof(entry->size), NULL);
			SQLGetData(stmt,  9, SQL_C_SHORT, &entry->decimals, sizeof(entry->decimals), NULL);
			SQLGetData(stmt, 10, SQL_C_SHORT, &entry->radix, sizeof(entry->radix), NULL);
			SQLGetData(stmt, 11, SQL_C_SHORT, &entry->nullable, sizeof(entry->nullable), NULL);
			SQLGetData(stmt, 16, SQL_C_LONG, &entry->octetlen, sizeof(entry->octetlen), NULL);

			/* Specification states that the octenlen should be the maximum number of bytes
			 * returned in a char or binary column, but it seems that some drivers just set
			 * it to NULL. (Bad Postgres! No biscuit!) */
			if (entry->octetlen == 0)
				entry->octetlen = entry->size;

			ast_verb(4, "Found %s column with type %hd with len %ld, octetlen %ld, and numlen (%hd,%hd)\n", entry->name, entry->type, (long) entry->size, (long) entry->octetlen, entry->decimals, entry->radix);
			/* Insert column info into column list */
			AST_LIST_INSERT_TAIL(&(tableptr->columns), entry, list);
			res = 0;
		}

		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);

		if (AST_LIST_FIRST(&(tableptr->columns)))
			AST_RWLIST_INSERT_TAIL(&odbc_tables, tableptr, list);
		else
			ast_free(tableptr);
	}
	ast_config_destroy(cfg);
	return res;
}

static int free_config(void)
{
	struct tables *table;
	struct columns *entry;
	while ((table = AST_RWLIST_REMOVE_HEAD(&odbc_tables, list))) {
		while ((entry = AST_LIST_REMOVE_HEAD(&(table->columns), list))) {
			ast_free(entry);
		}
		ast_free(table);
	}
	return 0;
}

static SQLHSTMT generic_prepare(struct odbc_obj *obj, void *data)
{
	int res, i;
	SQLHSTMT stmt;
	SQLINTEGER nativeerror = 0, numfields = 0;
	SQLSMALLINT diagbytes = 0;
	unsigned char state[10], diagnostic[256];

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	res = SQLPrepare(stmt, (unsigned char *) data, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", (char *) data);
		SQLGetDiagField(SQL_HANDLE_STMT, stmt, 1, SQL_DIAG_NUMBER, &numfields, SQL_IS_INTEGER, &diagbytes);
		for (i = 0; i < numfields; i++) {
			SQLGetDiagRec(SQL_HANDLE_STMT, stmt, i + 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
			ast_log(LOG_WARNING, "SQL Execute returned an error %d: %s: %s (%d)\n", res, state, diagnostic, diagbytes);
			if (i > 10) {
				ast_log(LOG_WARNING, "Oh, that was good.  There are really %d diagnostics?\n", (int)numfields);
				break;
			}
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	return stmt;
}

#define LENGTHEN_BUF1(size)														\
			do {																\
				/* Lengthen buffer, if necessary */								\
				if (ast_str_strlen(sql) + size + 1 > ast_str_size(sql)) {		\
					if (ast_str_make_space(&sql, ((ast_str_size(sql) + size + 1) / 512 + 1) * 512) != 0) { \
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CDR '%s:%s' failed.\n", tableptr->connection, tableptr->table); \
						ast_free(sql);											\
						ast_free(sql2);											\
						AST_RWLIST_UNLOCK(&odbc_tables);						\
						return -1;												\
					}															\
				}																\
			} while (0)

#define LENGTHEN_BUF2(size)														\
			do {																\
				if (ast_str_strlen(sql2) + size + 1 > ast_str_size(sql2)) {		\
					if (ast_str_make_space(&sql2, ((ast_str_size(sql2) + size + 3) / 512 + 1) * 512) != 0) { \
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CDR '%s:%s' failed.\n", tableptr->connection, tableptr->table); \
						ast_free(sql);											\
						ast_free(sql2);											\
						AST_RWLIST_UNLOCK(&odbc_tables);						\
						return -1;												\
					}															\
				}																\
			} while (0)

static int odbc_log(struct ast_cdr *cdr)
{
	struct tables *tableptr;
	struct columns *entry;
	struct odbc_obj *obj;
	struct ast_str *sql = ast_str_create(maxsize), *sql2 = ast_str_create(maxsize2);
	char *tmp;
	char colbuf[1024], *colptr;
	SQLHSTMT stmt = NULL;
	SQLLEN rows = 0;

	if (!sql || !sql2) {
		if (sql)
			ast_free(sql);
		if (sql2)
			ast_free(sql2);
		return -1;
	}

	if (AST_RWLIST_RDLOCK(&odbc_tables)) {
		ast_log(LOG_ERROR, "Unable to lock table list.  Insert CDR(s) failed.\n");
		ast_free(sql);
		ast_free(sql2);
		return -1;
	}

	AST_LIST_TRAVERSE(&odbc_tables, tableptr, list) {
		int first = 1;
		int quoted = 0;

		if (tableptr->quoted_identifiers != '\0'){
			quoted = 1;
		}

		if (ast_strlen_zero(tableptr->schema)) {
			if (quoted) {
				ast_str_set(&sql, 0, "INSERT INTO %c%s%c (",
					tableptr->quoted_identifiers, tableptr->table, tableptr->quoted_identifiers );
			}else{
				ast_str_set(&sql, 0, "INSERT INTO %s (", tableptr->table);
			}
		} else {
			if (quoted) {
				ast_str_set(&sql, 0, "INSERT INTO %c%s%c.%c%s%c (",
						tableptr->quoted_identifiers, tableptr->schema, tableptr->quoted_identifiers,
						tableptr->quoted_identifiers, tableptr->table,  tableptr->quoted_identifiers);
			}else{
				ast_str_set(&sql, 0, "INSERT INTO %s.%s (", tableptr->schema, tableptr->table);
			}
		}
		ast_str_set(&sql2, 0, " VALUES (");

		/* No need to check the connection now; we'll handle any failure in prepare_and_execute */
		if (!(obj = ast_odbc_request_obj(tableptr->connection, 0))) {
			ast_log(LOG_WARNING, "cdr_adaptive_odbc: Unable to retrieve database handle for '%s:%s'.  CDR failed: %s\n", tableptr->connection, tableptr->table, ast_str_buffer(sql));
			continue;
		}

		AST_LIST_TRAVERSE(&(tableptr->columns), entry, list) {
			int datefield = 0;
			if (strcasecmp(entry->cdrname, "start") == 0) {
				datefield = 1;
			} else if (strcasecmp(entry->cdrname, "answer") == 0) {
				datefield = 2;
			} else if (strcasecmp(entry->cdrname, "end") == 0) {
				datefield = 3;
			}

			/* Check if we have a similarly named variable */
			if (entry->staticvalue) {
				colptr = ast_strdupa(entry->staticvalue);
			} else if (datefield && tableptr->usegmtime) {
				struct timeval date_tv = (datefield == 1) ? cdr->start : (datefield == 2) ? cdr->answer : cdr->end;
				struct ast_tm tm = { 0, };
				ast_localtime(&date_tv, &tm, "UTC");
				ast_strftime(colbuf, sizeof(colbuf), "%Y-%m-%d %H:%M:%S", &tm);
				colptr = colbuf;
			} else {
				ast_cdr_format_var(cdr, entry->cdrname, &colptr, colbuf, sizeof(colbuf), datefield ? 0 : 1);
			}

			if (colptr) {
				/* Check first if the column filters this entry.  Note that this
				 * is very specifically NOT ast_strlen_zero(), because the filter
				 * could legitimately specify that the field is blank, which is
				 * different from the field being unspecified (NULL). */
				if ((entry->filtervalue && !entry->negatefiltervalue && strcasecmp(colptr, entry->filtervalue) != 0) ||
					(entry->filtervalue && entry->negatefiltervalue && strcasecmp(colptr, entry->filtervalue) == 0)) {
					ast_verb(4, "CDR column '%s' with value '%s' does not match filter of"
						" %s'%s'.  Cancelling this CDR.\n",
						entry->cdrname, colptr, entry->negatefiltervalue ? "!" : "", entry->filtervalue);
					goto early_release;
				}

				/* Only a filter? */
				if (ast_strlen_zero(entry->name))
					continue;

				LENGTHEN_BUF1(strlen(entry->name));

				switch (entry->type) {
				case SQL_CHAR:
				case SQL_VARCHAR:
				case SQL_LONGVARCHAR:
#ifdef HAVE_ODBC_WCHAR
				case SQL_WCHAR:
				case SQL_WVARCHAR:
				case SQL_WLONGVARCHAR:
#endif
				case SQL_BINARY:
				case SQL_VARBINARY:
				case SQL_LONGVARBINARY:
				case SQL_GUID:
					/* For these two field names, get the rendered form, instead of the raw
					 * form (but only when we're dealing with a character-based field).
					 */
					if (strcasecmp(entry->name, "disposition") == 0) {
						ast_cdr_format_var(cdr, entry->name, &colptr, colbuf, sizeof(colbuf), 0);
					} else if (strcasecmp(entry->name, "amaflags") == 0) {
						ast_cdr_format_var(cdr, entry->name, &colptr, colbuf, sizeof(colbuf), 0);
					}

					/* Truncate too-long fields */
					if (entry->type != SQL_GUID) {
						if (strlen(colptr) > entry->octetlen) {
							colptr[entry->octetlen] = '\0';
						}
					}

					LENGTHEN_BUF2(strlen(colptr));

					/* Encode value, with escaping */
					ast_str_append(&sql2, 0, "%s'", first ? "" : ",");
					for (tmp = colptr; *tmp; tmp++) {
						if (*tmp == '\'') {
							ast_str_append(&sql2, 0, "''");
						} else if (*tmp == '\\' && ast_odbc_backslash_is_escape(obj)) {
							ast_str_append(&sql2, 0, "\\\\");
						} else {
							ast_str_append(&sql2, 0, "%c", *tmp);
						}
					}
					ast_str_append(&sql2, 0, "'");
					break;
				case SQL_TYPE_DATE:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						int year = 0, month = 0, day = 0;
						if (sscanf(colptr, "%4d-%2d-%2d", &year, &month, &day) != 3 || year <= 0 ||
							month <= 0 || month > 12 || day < 0 || day > 31 ||
							((month == 4 || month == 6 || month == 9 || month == 11) && day == 31) ||
							(month == 2 && year % 400 == 0 && day > 29) ||
							(month == 2 && year % 100 == 0 && day > 28) ||
							(month == 2 && year % 4 == 0 && day > 29) ||
							(month == 2 && year % 4 != 0 && day > 28)) {
							ast_log(LOG_WARNING, "CDR variable %s is not a valid date ('%s').\n", entry->name, colptr);
							continue;
						}

						if (year > 0 && year < 100) {
							year += 2000;
						}

						LENGTHEN_BUF2(17);
						ast_str_append(&sql2, 0, "%s{ d '%04d-%02d-%02d' }", first ? "" : ",", year, month, day);
					}
					break;
				case SQL_TYPE_TIME:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						int hour = 0, minute = 0, second = 0;
						int count = sscanf(colptr, "%2d:%2d:%2d", &hour, &minute, &second);

						if ((count != 2 && count != 3) || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
							ast_log(LOG_WARNING, "CDR variable %s is not a valid time ('%s').\n", entry->name, colptr);
							continue;
						}

						LENGTHEN_BUF2(15);
						ast_str_append(&sql2, 0, "%s{ t '%02d:%02d:%02d' }", first ? "" : ",", hour, minute, second);
					}
					break;
				case SQL_TYPE_TIMESTAMP:
				case SQL_TIMESTAMP:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
						int count = sscanf(colptr, "%4d-%2d-%2d %2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second);

						if ((count != 3 && count != 5 && count != 6) || year <= 0 ||
							month <= 0 || month > 12 || day < 0 || day > 31 ||
							((month == 4 || month == 6 || month == 9 || month == 11) && day == 31) ||
							(month == 2 && year % 400 == 0 && day > 29) ||
							(month == 2 && year % 100 == 0 && day > 28) ||
							(month == 2 && year % 4 == 0 && day > 29) ||
							(month == 2 && year % 4 != 0 && day > 28) ||
							hour > 23 || minute > 59 || second > 59 || hour < 0 || minute < 0 || second < 0) {
							ast_log(LOG_WARNING, "CDR variable %s is not a valid timestamp ('%s').\n", entry->name, colptr);
							continue;
						}

						if (year > 0 && year < 100) {
							year += 2000;
						}

						LENGTHEN_BUF2(26);
						ast_str_append(&sql2, 0, "%s{ ts '%04d-%02d-%02d %02d:%02d:%02d' }", first ? "" : ",", year, month, day, hour, minute, second);
					}
					break;
				case SQL_INTEGER:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						int integer = 0;
						if (sscanf(colptr, "%30d", &integer) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an integer.\n", entry->name);
							continue;
						}

						LENGTHEN_BUF2(12);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_BIGINT:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						long long integer = 0;
						if (sscanf(colptr, "%30lld", &integer) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an integer.\n", entry->name);
							continue;
						}

						LENGTHEN_BUF2(24);
						ast_str_append(&sql2, 0, "%s%lld", first ? "" : ",", integer);
					}
					break;
				case SQL_SMALLINT:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						short integer = 0;
						if (sscanf(colptr, "%30hd", &integer) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an integer.\n", entry->name);
							continue;
						}

						LENGTHEN_BUF2(6);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_TINYINT:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						signed char integer = 0;
						if (sscanf(colptr, "%30hhd", &integer) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an integer.\n", entry->name);
							continue;
						}

						LENGTHEN_BUF2(4);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_BIT:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						signed char integer = 0;
						if (sscanf(colptr, "%30hhd", &integer) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an integer.\n", entry->name);
							continue;
						}
						if (integer != 0)
							integer = 1;

						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_NUMERIC:
				case SQL_DECIMAL:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						double number = 0.0;

						if (!strcasecmp(entry->cdrname, "billsec")) {
							if (!ast_tvzero(cdr->answer)) {
								snprintf(colbuf, sizeof(colbuf), "%lf",
											(double) (ast_tvdiff_us(cdr->end, cdr->answer) / 1000000.0));
							} else {
								ast_copy_string(colbuf, "0", sizeof(colbuf));
							}
						} else if (!strcasecmp(entry->cdrname, "duration")) {
							snprintf(colbuf, sizeof(colbuf), "%lf",
										(double) (ast_tvdiff_us(cdr->end, cdr->start) / 1000000.0));

							if (!ast_strlen_zero(colbuf)) {
								colptr = colbuf;
							}
						}

						if (sscanf(colptr, "%30lf", &number) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an numeric type.\n", entry->name);
							continue;
						}

						LENGTHEN_BUF2(entry->decimals);
						ast_str_append(&sql2, 0, "%s%*.*lf", first ? "" : ",", entry->decimals, entry->radix, number);
					}
					break;
				case SQL_FLOAT:
				case SQL_REAL:
				case SQL_DOUBLE:
					if (ast_strlen_zero(colptr)) {
						continue;
					} else {
						double number = 0.0;

						if (!strcasecmp(entry->cdrname, "billsec")) {
							if (!ast_tvzero(cdr->answer)) {
								snprintf(colbuf, sizeof(colbuf), "%lf",
											(double) (ast_tvdiff_us(cdr->end, cdr->answer) / 1000000.0));
							} else {
								ast_copy_string(colbuf, "0", sizeof(colbuf));
							}
						} else if (!strcasecmp(entry->cdrname, "duration")) {
							snprintf(colbuf, sizeof(colbuf), "%lf",
										(double) (ast_tvdiff_us(cdr->end, cdr->start) / 1000000.0));

							if (!ast_strlen_zero(colbuf)) {
								colptr = colbuf;
							}
						}

						if (sscanf(colptr, "%30lf", &number) != 1) {
							ast_log(LOG_WARNING, "CDR variable %s is not an numeric type.\n", entry->name);
							continue;
						}

						LENGTHEN_BUF2(entry->decimals);
						ast_str_append(&sql2, 0, "%s%lf", first ? "" : ",", number);
					}
					break;
				default:
					ast_log(LOG_WARNING, "Column type %d (field '%s:%s:%s') is unsupported at this time.\n", entry->type, tableptr->connection, tableptr->table, entry->name);
					continue;
				}
				if (quoted) {
					ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
				} else {
					ast_str_append(&sql, 0, "%s%c%s%c", first ? "" : ",", tableptr->quoted_identifiers, entry->name, tableptr->quoted_identifiers);
				}
				first = 0;
			} else if (entry->filtervalue
				&& ((!entry->negatefiltervalue && entry->filtervalue[0] != '\0')
					|| (entry->negatefiltervalue && entry->filtervalue[0] == '\0'))) {
				ast_log(AST_LOG_WARNING, "CDR column '%s' was not set and does not match filter of"
					" %s'%s'.  Cancelling this CDR.\n",
					entry->cdrname, entry->negatefiltervalue ? "!" : "",
					entry->filtervalue);
				goto early_release;
			}
		}

		/* Concatenate the two constructed buffers */
		LENGTHEN_BUF1(ast_str_strlen(sql2));
		ast_str_append(&sql, 0, ")");
		ast_str_append(&sql2, 0, ")");
		ast_str_append(&sql, 0, "%s", ast_str_buffer(sql2));

		ast_debug(3, "Executing [%s]\n", ast_str_buffer(sql));

		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, ast_str_buffer(sql));
		if (stmt) {
			SQLRowCount(stmt, &rows);
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		}
		if (rows == 0) {
			ast_log(LOG_WARNING, "cdr_adaptive_odbc: Insert failed on '%s:%s'.  CDR failed: %s\n", tableptr->connection, tableptr->table, ast_str_buffer(sql));
		}
early_release:
		ast_odbc_release_obj(obj);
	}
	AST_RWLIST_UNLOCK(&odbc_tables);

	/* Next time, just allocate buffers that are that big to start with. */
	if (ast_str_strlen(sql) > maxsize) {
		maxsize = ast_str_strlen(sql);
	}
	if (ast_str_strlen(sql2) > maxsize2) {
		maxsize2 = ast_str_strlen(sql2);
	}

	ast_free(sql);
	ast_free(sql2);
	return 0;
}

static int unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	if (AST_RWLIST_WRLOCK(&odbc_tables)) {
		ast_cdr_register(name, ast_module_info->description, odbc_log);
		ast_log(LOG_ERROR, "Unable to lock column list.  Unload failed.\n");
		return -1;
	}

	free_config();
	AST_RWLIST_UNLOCK(&odbc_tables);
	return 0;
}

static int load_module(void)
{
	if (AST_RWLIST_WRLOCK(&odbc_tables)) {
		ast_log(LOG_ERROR, "Unable to lock column list.  Load failed.\n");
		return 0;
	}

	load_config();
	AST_RWLIST_UNLOCK(&odbc_tables);
	ast_cdr_register(name, ast_module_info->description, odbc_log);
	return 0;
}

static int reload(void)
{
	if (AST_RWLIST_WRLOCK(&odbc_tables)) {
		ast_log(LOG_ERROR, "Unable to lock column list.  Reload failed.\n");
		return -1;
	}

	free_config();
	load_config();
	AST_RWLIST_UNLOCK(&odbc_tables);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Adaptive ODBC CDR backend",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);

