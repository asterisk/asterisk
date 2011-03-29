/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 Digium
 *
 * Adapted from cdr_adaptive_odbc:
 * Tilghman Lesher <tlesher AT digium DOT com>
 * by Steve Murphy
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
 *
 * \brief ODBC CEL backend
 *
 * \author Tilghman Lesher <tlesher AT digium DOT com>
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<depend>res_odbc</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include "asterisk/cel.h"
#include "asterisk/module.h"

#define	CONFIG	"cel_odbc.conf"
static struct ast_event_sub *event_sub = NULL;

/* Optimization to reduce number of memory allocations */
static int maxsize = 512, maxsize2 = 512;

struct columns {
	char *name;
	char *celname;
	char *filtervalue;
	char *staticvalue;
	SQLSMALLINT type;
	SQLINTEGER size;
	SQLSMALLINT decimals;
	SQLSMALLINT radix;
	SQLSMALLINT nullable;
	SQLINTEGER octetlen;
	AST_LIST_ENTRY(columns) list;
};

struct tables {
	char *connection;
	char *table;
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
	int lenconnection, lentable, usegmtime = 0;
	SQLLEN sqlptr;
	int res = 0;
	SQLHSTMT stmt = NULL;
	struct ast_flags config_flags = { 0 }; /* Part of our config comes from the database */

	cfg = ast_config_load(CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load " CONFIG ".  No ODBC CEL records!\n");
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
			ast_log(LOG_NOTICE, "No table name found.  Assuming 'cel'.\n");
			tmp = "cel";
		}
		ast_copy_string(table, tmp, sizeof(table));
		lentable = strlen(table);

		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed on connection '%s'!\n", connection);
			ast_odbc_release_obj(obj);
			continue;
		}

		res = SQLColumns(stmt, NULL, 0, NULL, 0, (unsigned char *)table, SQL_NTS, (unsigned char *)"%", SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_ERROR, "Unable to query database columns on connection '%s'.  Skipping.\n", connection);
			ast_odbc_release_obj(obj);
			continue;
		}

		tableptr = ast_calloc(sizeof(char), sizeof(*tableptr) + lenconnection + 1 + lentable + 1);
		if (!tableptr) {
			ast_log(LOG_ERROR, "Out of memory creating entry for table '%s' on connection '%s'\n", table, connection);
			ast_odbc_release_obj(obj);
			res = -1;
			break;
		}

		tableptr->usegmtime = usegmtime;
		tableptr->connection = (char *)tableptr + sizeof(*tableptr);
		tableptr->table = (char *)tableptr + sizeof(*tableptr) + lenconnection + 1;
		ast_copy_string(tableptr->connection, connection, lenconnection + 1);
		ast_copy_string(tableptr->table, table, lentable + 1);

		ast_verb(3, "Found CEL table %s@%s.\n", tableptr->table, tableptr->connection);

		/* Check for filters first */
		for (var = ast_variable_browse(cfg, catg); var; var = var->next) {
			if (strncmp(var->name, "filter", 6) == 0) {
				char *celvar = ast_strdupa(var->name + 6);
				celvar = ast_strip(celvar);
				ast_verb(3, "Found filter %s for cel variable %s in %s@%s\n", var->value, celvar, tableptr->table, tableptr->connection);

				entry = ast_calloc(sizeof(char), sizeof(*entry) + strlen(celvar) + 1 + strlen(var->value) + 1);
				if (!entry) {
					ast_log(LOG_ERROR, "Out of memory creating filter entry for CEL variable '%s' in table '%s' on connection '%s'\n", celvar, table, connection);
					res = -1;
					break;
				}

				/* NULL column entry means this isn't a column in the database */
				entry->name = NULL;
				entry->celname = (char *)entry + sizeof(*entry);
				entry->filtervalue = (char *)entry + sizeof(*entry) + strlen(celvar) + 1;
				strcpy(entry->celname, celvar);
				strcpy(entry->filtervalue, var->value);

				AST_LIST_INSERT_TAIL(&(tableptr->columns), entry, list);
			}
		}

		while ((res = SQLFetch(stmt)) != SQL_NO_DATA && res != SQL_ERROR) {
			char *celvar = "", *staticvalue = "";

			SQLGetData(stmt,  4, SQL_C_CHAR, columnname, sizeof(columnname), &sqlptr);

			/* Is there an alias for this column? */

			/* NOTE: This seems like a non-optimal parse method, but I'm going
			 * for user configuration readability, rather than fast parsing. We
			 * really don't parse this file all that often, anyway.
			 */
			for (var = ast_variable_browse(cfg, catg); var; var = var->next) {
				if (strncmp(var->name, "alias", 5) == 0 && strcasecmp(var->value, columnname) == 0) {
					char *alias = ast_strdupa(var->name + 5);
					celvar = ast_strip(alias);
					ast_verb(3, "Found alias %s for column %s in %s@%s\n", celvar, columnname, tableptr->table, tableptr->connection);
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

			entry = ast_calloc(sizeof(char), sizeof(*entry) + strlen(columnname) + 1 + strlen(celvar) + 1 + strlen(staticvalue) + 1);
			if (!entry) {
				ast_log(LOG_ERROR, "Out of memory creating entry for column '%s' in table '%s' on connection '%s'\n", columnname, table, connection);
				res = -1;
				break;
			}
			entry->name = (char *)entry + sizeof(*entry);
			strcpy(entry->name, columnname);

			if (!ast_strlen_zero(celvar)) {
				entry->celname = entry->name + strlen(columnname) + 1;
				strcpy(entry->celname, celvar);
			} else { /* Point to same place as the column name */
				entry->celname = (char *)entry + sizeof(*entry);
			}

			if (!ast_strlen_zero(staticvalue)) {
				entry->staticvalue = entry->celname + strlen(entry->celname) + 1;
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

			ast_verb(10, "Found %s column with type %hd with len %ld, octetlen %ld, and numlen (%hd,%hd)\n", entry->name, entry->type, (long) entry->size, (long) entry->octetlen, entry->decimals, entry->radix);
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
	char *sql = data;
	SQLHSTMT stmt;
	SQLINTEGER nativeerror = 0, numfields = 0;
	SQLSMALLINT diagbytes = 0;
	unsigned char state[10], diagnostic[256];

	res = SQLAllocHandle (SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	res = SQLPrepare(stmt, (unsigned char *)sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
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
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CEL '%s:%s' failed.\n", tableptr->connection, tableptr->table); \
						ast_free(sql);											\
						ast_free(sql2);											\
						AST_RWLIST_UNLOCK(&odbc_tables);						\
						return;													\
					}															\
				}																\
			} while (0)

#define LENGTHEN_BUF2(size)														\
			do {																\
				if (ast_str_strlen(sql2) + size + 1 > ast_str_size(sql2)) {		\
					if (ast_str_make_space(&sql2, ((ast_str_size(sql2) + size + 3) / 512 + 1) * 512) != 0) { \
						ast_log(LOG_ERROR, "Unable to allocate sufficient memory.  Insert CEL '%s:%s' failed.\n", tableptr->connection, tableptr->table); \
						ast_free(sql);											\
						ast_free(sql2);											\
						AST_RWLIST_UNLOCK(&odbc_tables);						\
						return;													\
					}															\
				}																\
			} while (0)

static void odbc_log(const struct ast_event *event, void *userdata)
{
	struct tables *tableptr;
	struct columns *entry;
	struct odbc_obj *obj;
	struct ast_str *sql = ast_str_create(maxsize), *sql2 = ast_str_create(maxsize2);
	char *tmp;
	char colbuf[1024], *colptr;
	SQLHSTMT stmt = NULL;
	SQLLEN rows = 0;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	if (!sql || !sql2) {
		if (sql)
			ast_free(sql);
		if (sql2)
			ast_free(sql2);
		return;
	}

	if (AST_RWLIST_RDLOCK(&odbc_tables)) {
		ast_log(LOG_ERROR, "Unable to lock table list.  Insert CEL(s) failed.\n");
		ast_free(sql);
		ast_free(sql2);
		return;
	}

	AST_LIST_TRAVERSE(&odbc_tables, tableptr, list) {
		int first = 1;
		ast_str_set(&sql, 0, "INSERT INTO %s (", tableptr->table);
		ast_str_set(&sql2, 0, " VALUES (");

		/* No need to check the connection now; we'll handle any failure in prepare_and_execute */
		if (!(obj = ast_odbc_request_obj(tableptr->connection, 0))) {
			ast_log(LOG_WARNING, "Unable to retrieve database handle for '%s:%s'.  CEL failed: %s\n", tableptr->connection, tableptr->table, ast_str_buffer(sql));
			continue;
		}

		AST_LIST_TRAVERSE(&(tableptr->columns), entry, list) {
			int datefield = 0;
			if (strcasecmp(entry->celname, "eventtime") == 0) {
				datefield = 1;
			}

			/* Check if we have a similarly named variable */
			if (entry->staticvalue) {
				colptr = ast_strdupa(entry->staticvalue);
			} else if (datefield) {
				struct timeval date_tv = record.event_time;
				struct ast_tm tm = { 0, };
				ast_localtime(&date_tv, &tm, tableptr->usegmtime ? "UTC" : NULL);
				ast_strftime(colbuf, sizeof(colbuf), "%Y-%m-%d %H:%M:%S", &tm);
				colptr = colbuf;
			} else {
				if (strcmp(entry->celname, "userdeftype") == 0) {
					ast_copy_string(colbuf, record.user_defined_name, sizeof(colbuf));
				} else if (strcmp(entry->celname, "cid_name") == 0) {
					ast_copy_string(colbuf, record.caller_id_name, sizeof(colbuf));
				} else if (strcmp(entry->celname, "cid_num") == 0) {
					ast_copy_string(colbuf, record.caller_id_num, sizeof(colbuf));
				} else if (strcmp(entry->celname, "cid_ani") == 0) {
					ast_copy_string(colbuf, record.caller_id_ani, sizeof(colbuf));
				} else if (strcmp(entry->celname, "cid_rdnis") == 0) {
					ast_copy_string(colbuf, record.caller_id_rdnis, sizeof(colbuf));
				} else if (strcmp(entry->celname, "cid_dnid") == 0) {
					ast_copy_string(colbuf, record.caller_id_dnid, sizeof(colbuf));
				} else if (strcmp(entry->celname, "exten") == 0) {
					ast_copy_string(colbuf, record.extension, sizeof(colbuf));
				} else if (strcmp(entry->celname, "context") == 0) {
					ast_copy_string(colbuf, record.context, sizeof(colbuf));
				} else if (strcmp(entry->celname, "channame") == 0) {
					ast_copy_string(colbuf, record.channel_name, sizeof(colbuf));
				} else if (strcmp(entry->celname, "appname") == 0) {
					ast_copy_string(colbuf, record.application_name, sizeof(colbuf));
				} else if (strcmp(entry->celname, "appdata") == 0) {
					ast_copy_string(colbuf, record.application_data, sizeof(colbuf));
				} else if (strcmp(entry->celname, "accountcode") == 0) {
					ast_copy_string(colbuf, record.account_code, sizeof(colbuf));
				} else if (strcmp(entry->celname, "peeraccount") == 0) {
					ast_copy_string(colbuf, record.peer_account, sizeof(colbuf));
				} else if (strcmp(entry->celname, "uniqueid") == 0) {
					ast_copy_string(colbuf, record.unique_id, sizeof(colbuf));
				} else if (strcmp(entry->celname, "linkedid") == 0) {
					ast_copy_string(colbuf, record.linked_id, sizeof(colbuf));
				} else if (strcmp(entry->celname, "userfield") == 0) {
					ast_copy_string(colbuf, record.user_field, sizeof(colbuf));
				} else if (strcmp(entry->celname, "peer") == 0) {
					ast_copy_string(colbuf, record.peer, sizeof(colbuf));
				} else if (strcmp(entry->celname, "amaflags") == 0) {
					snprintf(colbuf, sizeof(colbuf), "%d", record.amaflag);
				} else {
					colbuf[0] = 0;
				}
				colptr = colbuf;
			}

			if (colptr) {
				/* Check first if the column filters this entry.  Note that this
				 * is very specifically NOT ast_strlen_zero(), because the filter
				 * could legitimately specify that the field is blank, which is
				 * different from the field being unspecified (NULL). */
				if (entry->filtervalue && strcasecmp(colptr, entry->filtervalue) != 0) {
					ast_verb(4, "CEL column '%s' with value '%s' does not match filter of"
						" '%s'.  Cancelling this CEL.\n",
						entry->celname, colptr, entry->filtervalue);
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
				case SQL_BINARY:
				case SQL_VARBINARY:
				case SQL_LONGVARBINARY:
				case SQL_GUID:
					/* For these two field names, get the rendered form, instead of the raw
					 * form (but only when we're dealing with a character-based field).
					 */
					if (strcasecmp(entry->name, "eventtype") == 0) {
						snprintf(colbuf, sizeof(colbuf), "%s", record.event_name);
					}

					/* Truncate too-long fields */
					if (entry->type != SQL_GUID) {
						if (strlen(colptr) > entry->octetlen) {
							colptr[entry->octetlen] = '\0';
						}
					}

					ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
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
							ast_log(LOG_WARNING, "CEL variable %s is not a valid date ('%s').\n", entry->name, colptr);
							continue;
						}

						if (year > 0 && year < 100) {
							year += 2000;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
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
							ast_log(LOG_WARNING, "CEL variable %s is not a valid time ('%s').\n", entry->name, colptr);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
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
							ast_log(LOG_WARNING, "CEL variable %s is not a valid timestamp ('%s').\n", entry->name, colptr);
							continue;
						}

						if (year > 0 && year < 100) {
							year += 2000;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(26);
						ast_str_append(&sql2, 0, "%s{ ts '%04d-%02d-%02d %02d:%02d:%02d' }", first ? "" : ",", year, month, day, hour, minute, second);
					}
					break;
				case SQL_INTEGER:
					{
						int integer = 0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							integer = (int) record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30d", &integer) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an integer.\n", entry->name);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(12);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_BIGINT:
					{
						long long integer = 0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							integer = (long long) record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30lld", &integer) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an integer.\n", entry->name);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(24);
						ast_str_append(&sql2, 0, "%s%lld", first ? "" : ",", integer);
					}
					break;
				case SQL_SMALLINT:
					{
						short integer = 0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							integer = (short) record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30hd", &integer) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an integer.\n", entry->name);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(6);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_TINYINT:
					{
						char integer = 0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							integer = (char) record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30hhd", &integer) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an integer.\n", entry->name);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(4);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_BIT:
					{
						char integer = 0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							integer = (char) record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30hhd", &integer) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an integer.\n", entry->name);
							continue;
						}
						if (integer != 0)
							integer = 1;

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(2);
						ast_str_append(&sql2, 0, "%s%d", first ? "" : ",", integer);
					}
					break;
				case SQL_NUMERIC:
				case SQL_DECIMAL:
					{
						double number = 0.0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							number = (double)record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30lf", &number) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an numeric type.\n", entry->name);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(entry->decimals);
						ast_str_append(&sql2, 0, "%s%*.*lf", first ? "" : ",", entry->decimals, entry->radix, number);
					}
					break;
				case SQL_FLOAT:
				case SQL_REAL:
				case SQL_DOUBLE:
					{
						double number = 0.0;
						if (strcasecmp(entry->name, "eventtype") == 0) {
							number = (double) record.event_type;
						} else if (ast_strlen_zero(colptr)) {
							continue;
						} else if (sscanf(colptr, "%30lf", &number) != 1) {
							ast_log(LOG_WARNING, "CEL variable %s is not an numeric type.\n", entry->name);
							continue;
						}

						ast_str_append(&sql, 0, "%s%s", first ? "" : ",", entry->name);
						LENGTHEN_BUF2(entry->decimals);
						ast_str_append(&sql2, 0, "%s%lf", first ? "" : ",", number);
					}
					break;
				default:
					ast_log(LOG_WARNING, "Column type %d (field '%s:%s:%s') is unsupported at this time.\n", entry->type, tableptr->connection, tableptr->table, entry->name);
					continue;
				}
				first = 0;
			}
		}

		/* Concatenate the two constructed buffers */
		LENGTHEN_BUF1(ast_str_strlen(sql2));
		ast_str_append(&sql, 0, ")");
		ast_str_append(&sql2, 0, ")");
		ast_str_append(&sql, 0, "%s", ast_str_buffer(sql2));

		ast_verb(11, "[%s]\n", ast_str_buffer(sql));

		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, ast_str_buffer(sql));
		if (stmt) {
			SQLRowCount(stmt, &rows);
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		}
		if (rows == 0) {
			ast_log(LOG_WARNING, "Insert failed on '%s:%s'.  CEL failed: %s\n", tableptr->connection, tableptr->table, ast_str_buffer(sql));
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
}

static int unload_module(void)
{
	if (event_sub) {
		event_sub = ast_event_unsubscribe(event_sub);
	}
	if (AST_RWLIST_WRLOCK(&odbc_tables)) {
		event_sub = ast_event_subscribe(AST_EVENT_CEL, odbc_log, "ODBC CEL backend", NULL, AST_EVENT_IE_END);
		if (!event_sub) {
			ast_log(LOG_ERROR, "Unable to subscribe to CEL events\n");
		}
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
	event_sub = ast_event_subscribe(AST_EVENT_CEL, odbc_log, "ODBC CEL backend", NULL, AST_EVENT_IE_END);
	if (!event_sub) {
		ast_log(LOG_ERROR, "Unable to subscribe to CEL events\n");
	}
	return AST_MODULE_LOAD_SUCCESS;
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
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "ODBC CEL backend",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);

