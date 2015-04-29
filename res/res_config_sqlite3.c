/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Terry Wilson
 *
 * Terry Wilson <twilson@digium.com>
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
 *
 * Please follow coding guidelines
 * http://svn.digium.com/view/asterisk/trunk/doc/CODING-GUIDELINES
 */

/*! \file
 *
 * \brief SQLite 3 configuration engine
 *
 * \author\verbatim Terry Wilson <twilson@digium.com> \endverbatim
 *
 * This is a realtime configuration engine for the SQLite 3 Database
 * \ingroup resources
 */

/*! \li \ref res_config_sqlite3.c uses the configuration file \ref res_config_sqlite3.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page res_config_sqlite3.conf res_config_sqlite3.conf
 * \verbinclude res_config_sqlite3.conf.sample
 */

/*** MODULEINFO
	<load_priority>realtime_driver</load_priority>
	<depend>sqlite3</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sqlite3.h>

#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/paths.h"
#include "asterisk/astobj2.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
 ***/

static struct ast_config *realtime_sqlite3_load(const char *database, const char *table, const char *configfile, struct ast_config *config, struct ast_flags flags, const char *suggested_include_file, const char *who_asked);
static struct ast_variable *realtime_sqlite3(const char *database, const char *table, const struct ast_variable *fields);
static struct ast_config *realtime_sqlite3_multi(const char *database, const char *table, const struct ast_variable *fields);
static int realtime_sqlite3_update(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields);
static int realtime_sqlite3_update2(const char *database, const char *table, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields);
static int realtime_sqlite3_store(const char *database, const char *table, const struct ast_variable *fields);
static int realtime_sqlite3_destroy(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields);
static int realtime_sqlite3_require(const char *database, const char *table, va_list ap);
static int realtime_sqlite3_unload(const char *database, const char *table);

struct ast_config_engine sqlite3_config_engine = {
	.name = "sqlite3",
	.load_func = realtime_sqlite3_load,
	.realtime_func = realtime_sqlite3,
	.realtime_multi_func = realtime_sqlite3_multi,
	.update_func = realtime_sqlite3_update,
	.update2_func = realtime_sqlite3_update2,
	.store_func = realtime_sqlite3_store,
	.destroy_func = realtime_sqlite3_destroy,
	.require_func = realtime_sqlite3_require,
	.unload_func = realtime_sqlite3_unload,
};

enum {
	REALTIME_SQLITE3_REQ_WARN,
	REALTIME_SQLITE3_REQ_CLOSE,
	REALTIME_SQLITE3_REQ_CHAR,
};

struct realtime_sqlite3_db {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(filename);
	);
	sqlite3 *handle;
	pthread_t syncthread;
	ast_cond_t cond;
	unsigned int requirements:2;
	unsigned int dirty:1;
	unsigned int debug:1;
	unsigned int exiting:1;
	unsigned int wakeup:1;
	unsigned int batch;
};

struct ao2_container *databases;
#define DB_BUCKETS 7

AST_MUTEX_DEFINE_STATIC(config_lock);

/* We need a separate buffer for each field we might use concurrently */
AST_THREADSTORAGE(escape_table_buf);
AST_THREADSTORAGE(escape_column_buf);
AST_THREADSTORAGE(escape_value_buf);

static int realtime_sqlite3_execute_handle(struct realtime_sqlite3_db *db, const char *sql, int (*callback)(void*, int, char **, char **), void *arg, int sync);
void db_start_batch(struct realtime_sqlite3_db *db);
void db_stop_batch(struct realtime_sqlite3_db *db);

static inline const char *sqlite3_escape_string_helper(struct ast_threadstorage *ts, const char *param)
{
	size_t maxlen = strlen(param) * 2 + sizeof("\"\"");
	/* It doesn't appear that sqlite3_snprintf will do more than double the
	 * length of a string with %q as an option. %Q could double and possibly
	 * add two quotes, and convert NULL pointers to the word "NULL", but we
	 * don't allow those anyway. Just going to use %q for now. */
	struct ast_str *buf = ast_str_thread_get(ts, maxlen);
	char *tmp = ast_str_buffer(buf);
	char q = ts == &escape_value_buf ? '\'' : '"';

	ast_str_reset(buf);
	*tmp++ = q; /* Initial quote */
	while ((*tmp++ = *param++)) {
		/* Did we just copy a quote? Then double it. */
		if (*(tmp - 1) == q) {
			*tmp++ = q;
		}
	}
	*tmp = '\0'; /* Terminate past NULL from copy */
	*(tmp - 1) = q; /* Replace original NULL with the quote */
	ast_str_update(buf);

	return ast_str_buffer(buf);
}

static inline const char *sqlite3_escape_table(const char *param)
{
	return sqlite3_escape_string_helper(&escape_table_buf, param);
}

static inline const char *sqlite3_escape_column(const char *param)
{
	return sqlite3_escape_string_helper(&escape_column_buf, param);
}

/* Not inlining this function because it uses strdupa and I don't know if the compiler would be dumb */
static const char *sqlite3_escape_column_op(const char *param)
{
	size_t maxlen = strlen(param) * 2 + sizeof("\"\" =");
	struct ast_str *buf = ast_str_thread_get(&escape_column_buf, maxlen);
	char *tmp = ast_str_buffer(buf);
	int space = 0;

	ast_str_reset(buf);
	*tmp++ = '"';
	while ((*tmp++ = *param++)) {
		/* If we have seen a space, don't double quotes. XXX If we ever make the column/op field
		 * available to users via an API, we will definitely need to avoid allowing special
		 * characters like ';' in the data past the space as it will be unquoted data */
		if (space) {
			continue;
		}
		if (*(tmp - 1) == ' ') {
			*(tmp - 1) = '"';
			*tmp++ = ' ';
			space = 1;
		} else if (*(tmp - 1) == '"') {
			*tmp++ = '"';
		}
	}
	if (!space) {
		strcpy(tmp - 1, "\" =");
	}

	ast_str_update(buf);

	return ast_str_buffer(buf);
}

static inline const char *sqlite3_escape_value(const char *param)
{
	return sqlite3_escape_string_helper(&escape_value_buf, param);
}

static int db_hash_fn(const void *obj, const int flags)
{
	const struct realtime_sqlite3_db *db = obj;

	return ast_str_hash(flags & OBJ_KEY ? (const char *) obj : db->name);
}

static int db_cmp_fn(void *obj, void *arg, int flags) {
	struct realtime_sqlite3_db *db = obj, *other = arg;
	const char *name = arg;

	return !strcasecmp(db->name, flags & OBJ_KEY ? name : other->name) ? CMP_MATCH | CMP_STOP : 0;
}

static void db_destructor(void *obj)
{
	struct realtime_sqlite3_db *db = obj;

	ast_debug(1, "Destroying db: %s\n", db->name);
	ast_string_field_free_memory(db);
	db_stop_batch(db);
	if (db->handle) {
		ao2_lock(db);
		sqlite3_close(db->handle);
		ao2_unlock(db);
	}
}

static struct realtime_sqlite3_db *find_database(const char *database)
{
	return ao2_find(databases, database, OBJ_KEY);
}

static void unref_db(struct realtime_sqlite3_db **db)
{
	ao2_ref(*db, -1);
	*db = NULL;
}

static int stop_batch_cb(void *obj, void *arg, int flags)
{
	struct realtime_sqlite3_db *db = obj;

	db_stop_batch(db);
	return CMP_MATCH;
}

static int mark_dirty_cb(void *obj, void *arg, int flags)
{
	struct realtime_sqlite3_db *db = obj;
	db->dirty = 1;
	return CMP_MATCH;
}

static void mark_all_databases_dirty(void)
{
	ao2_callback(databases, OBJ_MULTIPLE | OBJ_NODATA, mark_dirty_cb, NULL);
}

static int is_dirty_cb(void *obj, void *arg, int flags)
{
	struct realtime_sqlite3_db *db = obj;
	if (db->dirty) {
		db_stop_batch(db);
		return CMP_MATCH;
	}
	return 0;
}

static void unlink_dirty_databases(void)
{
	ao2_callback(databases, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK, is_dirty_cb, NULL);
}

static int str_to_requirements(const char *data)
{
	if (!strcasecmp(data, "createclose")) {
		return REALTIME_SQLITE3_REQ_CLOSE;
	} else if (!strcasecmp(data, "createchar")) {
		return REALTIME_SQLITE3_REQ_CHAR;
	}
	/* default */
	return REALTIME_SQLITE3_REQ_WARN;
}

/*! \note Since this is called while a query is executing, we should already hold the db lock */
static void trace_cb(void *arg, const char *sql)
{
	struct realtime_sqlite3_db *db = arg;
	ast_debug(3, "DB: %s SQL: %s\n", db->name, sql);
}

/*! \brief Wrap commands in transactions increased write performance */
static void *db_sync_thread(void *data)
{
	struct realtime_sqlite3_db *db = data;
	ao2_lock(db);
	realtime_sqlite3_execute_handle(db, "BEGIN TRANSACTION", NULL, NULL, 0);
	for (;;) {
		if (!db->wakeup) {
			ast_cond_wait(&db->cond, ao2_object_get_lockaddr(db));
		}
		db->wakeup = 0;
		if (realtime_sqlite3_execute_handle(db, "COMMIT", NULL, NULL, 0) < 0) {
			realtime_sqlite3_execute_handle(db, "ROLLBACK", NULL, NULL, 0);
		}
		if (db->exiting) {
			ao2_unlock(db);
			break;
		}
		realtime_sqlite3_execute_handle(db, "BEGIN TRANSACTION", NULL, NULL, 0);
		ao2_unlock(db);
		usleep(1000 * db->batch);
		ao2_lock(db);
	}

	unref_db(&db);

	return NULL;
}

/*! \brief Open a database and appropriately set debugging on the db handle */
static int db_open(struct realtime_sqlite3_db *db)
{
	ao2_lock(db);
	if (sqlite3_open(db->filename, &db->handle) != SQLITE_OK) {
		ast_log(LOG_WARNING, "Could not open %s: %s\n", db->filename, sqlite3_errmsg(db->handle));
		ao2_unlock(db);
		return -1;
	}
	sqlite3_busy_timeout(db->handle, 1000);

	if (db->debug) {
		sqlite3_trace(db->handle, trace_cb, db);
	} else {
		sqlite3_trace(db->handle, NULL, NULL);
	}

	ao2_unlock(db);

	return 0;
}

static void db_sync(struct realtime_sqlite3_db *db)
{
	db->wakeup = 1;
	ast_cond_signal(&db->cond);
}

void db_start_batch(struct realtime_sqlite3_db *db)
{
	if (db->batch) {
		ast_cond_init(&db->cond, NULL);
		ao2_ref(db, +1);
		ast_pthread_create_background(&db->syncthread, NULL, db_sync_thread, db);
	}
}

void db_stop_batch(struct realtime_sqlite3_db *db)
{
	if (db->batch) {
		db->exiting = 1;
		db_sync(db);
		pthread_join(db->syncthread, NULL);
	}
}

/*! \brief Create a db object based on a config category
 * \note Opening the db handle and linking to databases must be handled outside of this function
 */
static struct realtime_sqlite3_db *new_realtime_sqlite3_db(struct ast_config *config, const char *cat)
{
	struct ast_variable *var;
	struct realtime_sqlite3_db *db;

	if (!(db = ao2_alloc(sizeof(*db), db_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(db, 64)) {
		unref_db(&db);
		return NULL;
	}

	/* Set defaults */
	db->requirements = REALTIME_SQLITE3_REQ_WARN;
	db->batch = 100;
	ast_string_field_set(db, name, cat);

	for (var = ast_variable_browse(config, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "dbfile")) {
			ast_string_field_set(db, filename, var->value);
		} else if (!strcasecmp(var->name, "requirements")) {
			db->requirements = str_to_requirements(var->value);
		} else if (!strcasecmp(var->name, "batch")) {
			ast_app_parse_timelen(var->value, (int *) &db->batch, TIMELEN_MILLISECONDS);
		} else if (!strcasecmp(var->name, "debug")) {
			db->debug = ast_true(var->value);
		}
	}

	if (ast_strlen_zero(db->filename)) {
		ast_log(LOG_WARNING, "Must specify dbfile in res_config_sqlite3.conf\n");
		unref_db(&db);
		return NULL;
	}

	return db;
}

/*! \brief Update an existing db object based on config data
 * \param db The database object to update
 * \param config The configuration data with which to update the db
 * \param cat The config category (which becomes db->name)
 */
static int update_realtime_sqlite3_db(struct realtime_sqlite3_db *db, struct ast_config *config, const char *cat)
{
	struct realtime_sqlite3_db *new;

	if (!(new = new_realtime_sqlite3_db(config, cat))) {
		return -1;
	}

	/* Copy fields that don't need anything special done on change */
	db->requirements = new->requirements;

	/* Handle changes that require immediate behavior modification */
	if (db->debug != new->debug) {
		if (db->debug) {
			sqlite3_trace(db->handle, NULL, NULL);
		} else {
			sqlite3_trace(db->handle, trace_cb, db);
		}
		db->debug = new->debug;
	}

	if (strcmp(db->filename, new->filename)) {
		sqlite3_close(db->handle);
		ast_string_field_set(db, filename, new->filename);
		db_open(db); /* Also handles setting appropriate debug on new handle */
	}

	if (db->batch != new->batch) {
		if (db->batch == 0) {
			db->batch = new->batch;
			db_start_batch(db);
		} else if (new->batch == 0) {
			db->batch = new->batch;
			db_stop_batch(db);
		}
		db->batch = new->batch;
	}

	db->dirty = 0;
	unref_db(&new);

	return 0;
}

/*! \brief Create a varlist from a single sqlite3 result row */
static int row_to_varlist(void *arg, int num_columns, char **values, char **columns)
{
	struct ast_variable **head = arg, *tail;
	int i;
	struct ast_variable *new;

	if (!(new = ast_variable_new(columns[0], S_OR(values[0], ""), ""))) {
		return SQLITE_ABORT;
	}
	*head = tail = new;

	for (i = 1; i < num_columns; i++) {
		if (!(new = ast_variable_new(columns[i], S_OR(values[i], ""), ""))) {
			ast_variables_destroy(*head);
			*head = NULL;
			return SQLITE_ABORT;
		}
		tail->next = new;
		tail = new;
	}

	return 0;
}

/*! \brief Callback for creating an ast_config from a successive sqlite3 result rows */
static int append_row_to_cfg(void *arg, int num_columns, char **values, char **columns)
{
	struct ast_config *cfg = arg;
	struct ast_category *cat;
	int i;

	if (!(cat = ast_category_new("", "", 99999))) {
		return SQLITE_ABORT;
	}

	for (i = 0; i < num_columns; i++) {
		struct ast_variable *var;
		if (!(var = ast_variable_new(columns[i], S_OR(values[i], ""), ""))) {
			ast_log(LOG_ERROR, "Could not create new variable for '%s: %s', throwing away list\n", columns[i], values[i]);
			continue;
		}
		ast_variable_append(cat, var);
	}
	ast_category_append(cfg, cat);

	return 0;
}

/*!
 * Structure sent to the SQLite 3 callback function for static configuration.
 *
 * \see static_realtime_cb()
 */
struct cfg_entry_args {
	struct ast_config *cfg;
	struct ast_category *cat;
	char *cat_name;
	struct ast_flags flags;
	const char *who_asked;
};

/*! Exeute an SQL statement given the database object
 *
 * \retval -1 ERROR
 * \retval > -1 Number of rows changed
 */
static int realtime_sqlite3_execute_handle(struct realtime_sqlite3_db *db, const char *sql, int (*callback)(void*, int, char **, char **), void *arg, int sync)
{
	int res = 0;
	char *errmsg;

	ao2_lock(db);
	if (sqlite3_exec(db->handle, sql, callback, arg, &errmsg) != SQLITE_OK) {
		ast_log(LOG_WARNING, "Could not execute '%s': %s\n", sql, errmsg);
		sqlite3_free(errmsg);
		res = -1;
	} else {
		res = sqlite3_changes(db->handle);
	}
	ao2_unlock(db);

	if (sync) {
		db_sync(db);
	}

	return res;
}

/*! Exeute an SQL statement give the database name
 *
 * \retval -1 ERROR
 * \retval > -1 Number of rows changed
 */
static int realtime_sqlite3_execute(const char *database, const char *sql, int (*callback)(void*, int, char **, char **), void *arg, int sync)
{
	struct realtime_sqlite3_db *db;
	int res;

	if (!(db = find_database(database))) {
		ast_log(LOG_WARNING, "Could not find database: %s\n", database);
		return -1;
	}

	res = realtime_sqlite3_execute_handle(db, sql, callback, arg, sync);
	ao2_ref(db, -1);

	return res;
}

/*! \note It is important that the COL_* enum matches the order of the columns selected in static_sql */
static const char *static_sql = "SELECT category, var_name, var_val FROM \"%q\" WHERE filename = %Q AND commented = 0 ORDER BY cat_metric ASC, var_metric ASC";
enum {
	COL_CATEGORY,
	COL_VAR_NAME,
	COL_VAR_VAL,
	COL_COLUMNS,
};

static int static_realtime_cb(void *arg, int num_columns, char **values, char **columns)
{
	struct cfg_entry_args *args = arg;
	struct ast_variable *var;

	if (!strcmp(values[COL_VAR_NAME], "#include")) {
		struct ast_config *cfg;
		char *val;

		val = values[COL_VAR_VAL];
		if (!(cfg = ast_config_internal_load(val, args->cfg, args->flags, "", args->who_asked))) {
			ast_log(LOG_WARNING, "Unable to include %s\n", val);
			return SQLITE_ABORT;
		} else {
			args->cfg = cfg;
			return 0;
		}
	}

	if (!args->cat_name || strcmp(args->cat_name, values[COL_CATEGORY])) {
		if (!(args->cat = ast_category_new(values[COL_CATEGORY], "", 99999))) {
			ast_log(LOG_WARNING, "Unable to allocate category\n");
			return SQLITE_ABORT;
		}

		ast_free(args->cat_name);

		if (!(args->cat_name = ast_strdup(values[COL_CATEGORY]))) {
			ast_category_destroy(args->cat);
			return SQLITE_ABORT;
		}

		ast_category_append(args->cfg, args->cat);
	}

	if (!(var = ast_variable_new(values[COL_VAR_NAME], values[COL_VAR_VAL], ""))) {
		ast_log(LOG_WARNING, "Unable to allocate variable\n");
		return SQLITE_ABORT;
	}

	ast_variable_append(args->cat, var);

	return 0;
}

/*! \brief Realtime callback for static realtime
 * \return ast_config on success, NULL on failure
 */
static struct ast_config *realtime_sqlite3_load(const char *database, const char *table, const char *configfile, struct ast_config *config, struct ast_flags flags, const char *suggested_include_file, const char *who_asked)
{
	char *sql;
	struct cfg_entry_args args;

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return NULL;
	}

	if (!(sql = sqlite3_mprintf(static_sql, table, configfile))) {
		ast_log(LOG_WARNING, "Couldn't allocate query\n");
		return NULL;
	};

	args.cfg = config;
	args.cat = NULL;
	args.cat_name = NULL;
	args.flags = flags;
	args.who_asked = who_asked;

	realtime_sqlite3_execute(database, sql, static_realtime_cb, &args, 0);

	sqlite3_free(sql);

	return config;
}

/*! \brief Helper function for single and multi-row realtime load functions */
static int realtime_sqlite3_helper(const char *database, const char *table, const struct ast_variable *fields, int is_multi, void *arg)
{
	struct ast_str *sql;
	const struct ast_variable *field;
	int first = 1;

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return -1;
	}

	if (!(sql = ast_str_create(128))) {
		return -1;
	}

	for (field = fields; field; field = field->next) {
		if (first) {
			ast_str_set(&sql, 0, "SELECT * FROM %s WHERE %s %s", sqlite3_escape_table(table),
				    sqlite3_escape_column_op(field->name), sqlite3_escape_value(field->value));
			first = 0;
		} else {
			ast_str_append(&sql, 0, " AND %s %s", sqlite3_escape_column_op(field->name),
					sqlite3_escape_value(field->value));
		}
	}

	if (!is_multi) {
		ast_str_append(&sql, 0, "%s", " LIMIT 1");
	}

	if (realtime_sqlite3_execute(database, ast_str_buffer(sql), is_multi ? append_row_to_cfg : row_to_varlist, arg, 0) < 0) {
		ast_free(sql);
		return -1;
	}

	ast_free(sql);

	return 0;
}

/*! \brief Realtime callback for a single row query
 * \return ast_variable list for single result on success, NULL on empty/failure
 */
static struct ast_variable *realtime_sqlite3(const char *database, const char *table, const struct ast_variable *fields)
{
	struct ast_variable *result_row = NULL;

	realtime_sqlite3_helper(database, table, fields, 0, &result_row);

	return result_row;
}

/*! \brief Realtime callback for a multi-row query
 * \return ast_config containing possibly many results on success, NULL on empty/failure
 */
static struct ast_config *realtime_sqlite3_multi(const char *database, const char *table, const struct ast_variable *fields)
{
	struct ast_config *cfg;

	if (!(cfg = ast_config_new())) {
		return NULL;
	}

	if (realtime_sqlite3_helper(database, table, fields, 1, cfg)) {
		ast_config_destroy(cfg);
		return NULL;
	}

	return cfg;
}

/*! \brief Realtime callback for updating a row based on a single criteria
 * \return Number of rows affected or -1 on error
 */
static int realtime_sqlite3_update(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields)
{
	struct ast_str *sql;
	const struct ast_variable *field;
	int first = 1, res;

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return -1;
	}

	if (!(sql = ast_str_create(128))) {
		return -1;
	}

	for (field = fields; field; field = field->next) {
		if (first) {
			ast_str_set(&sql, 0, "UPDATE %s SET %s = %s",
					sqlite3_escape_table(table), sqlite3_escape_column(field->name), sqlite3_escape_value(field->value));
			first = 0;
		} else {
			ast_str_append(&sql, 0, ", %s = %s", sqlite3_escape_column(field->name), sqlite3_escape_value(field->value));
		}
	}

	ast_str_append(&sql, 0, " WHERE %s %s", sqlite3_escape_column_op(keyfield), sqlite3_escape_value(entity));

	res = realtime_sqlite3_execute(database, ast_str_buffer(sql), NULL, NULL, 1);
	ast_free(sql);

	return res;
}

/*! \brief Realtime callback for updating a row based on multiple criteria
 * \return Number of rows affected or -1 on error
 */
static int realtime_sqlite3_update2(const char *database, const char *table, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
{
	struct ast_str *sql;
	struct ast_str *where_clause;
	const struct ast_variable *field;
	int first = 1, res;

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return -1;
	}

	if (!(sql = ast_str_create(128))) {
		return -1;
	}

	if (!(where_clause = ast_str_create(128))) {
		ast_free(sql);
		return -1;
	}

	for (field = lookup_fields; field; field = field->next) {
		if (first) {
			ast_str_set(&where_clause, 0, " WHERE %s %s", sqlite3_escape_column_op(field->name), sqlite3_escape_value(field->value));
			first = 0;
		} else {
			ast_str_append(&where_clause, 0, " AND %s %s", sqlite3_escape_column_op(field->name), sqlite3_escape_value(field->value));
		}
	}

	first = 1;
	for (field = update_fields; field; field = field->next) {
		if (first) {
			ast_str_set(&sql, 0, "UPDATE %s SET %s = %s", sqlite3_escape_table(table), sqlite3_escape_column(field->name), sqlite3_escape_value(field->value));
			first = 0;
		} else {
			ast_str_append(&sql, 0, ", %s = %s", sqlite3_escape_column(field->name), sqlite3_escape_value(field->value));
		}
	}

	ast_str_append(&sql, 0, "%s", ast_str_buffer(where_clause));

	res = realtime_sqlite3_execute(database, ast_str_buffer(sql), NULL, NULL, 1);

	ast_free(sql);
	ast_free(where_clause);

	return res;
}

/*! \brief Realtime callback for inserting a row
 * \return Number of rows affected or -1 on error
 */
static int realtime_sqlite3_store(const char *database, const char *table, const struct ast_variable *fields)
{
	struct ast_str *sql, *values;
	const struct ast_variable *field;
	int first = 1, res;

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return -1;
	}

	if (!(sql = ast_str_create(128))) {
		return -1;
	}

	if (!(values = ast_str_create(128))) {
		ast_free(sql);
		return -1;
	}

	for (field = fields; field; field = field->next) {
		if (first) {
			ast_str_set(&sql, 0, "INSERT INTO %s (%s", sqlite3_escape_table(table), sqlite3_escape_column(field->name));
			ast_str_set(&values, 0, ") VALUES (%s", sqlite3_escape_value(field->value));
			first = 0;
		} else {
			ast_str_append(&sql, 0, ", %s", sqlite3_escape_column(field->name));
			ast_str_append(&values, 0, ", %s", sqlite3_escape_value(field->value));
		}
	}

	ast_str_append(&sql, 0, "%s)", ast_str_buffer(values));

	res = realtime_sqlite3_execute(database, ast_str_buffer(sql), NULL, NULL, 1);

	ast_free(sql);
	ast_free(values);

	return res;
}

/*! \brief Realtime callback for deleting a row
 * \return Number of rows affected or -1 on error
 */
static int realtime_sqlite3_destroy(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields)
{
	struct ast_str *sql;
	const struct ast_variable *field;
	int first = 1, res;

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return -1;
	}

	if (!(sql = ast_str_create(128))) {
		return -1;
	}

	for (field = fields; field; field = field->next) {
		if (first) {
			ast_str_set(&sql, 0, "DELETE FROM %s WHERE %s %s", sqlite3_escape_table(table),
					sqlite3_escape_column_op(field->name), sqlite3_escape_value(field->value));
			first = 0;
		} else {
			ast_str_append(&sql, 0, " AND %s %s", sqlite3_escape_column_op(field->name), sqlite3_escape_value(field->value));
		}
	}

	res = realtime_sqlite3_execute(database, ast_str_buffer(sql), NULL, NULL, 1);

	ast_free(sql);

	return res;
}

/*! \brief Convert Asterisk realtime types to SQLite 3 types
 * \note SQLite 3 has NULL, INTEGER, REAL, TEXT, and BLOB types. Any column other than
 * an INTEGER PRIMARY KEY will actually store any kind of data due to its dynamic
 * typing. When we create columns, we'll go ahead and use these base types instead
 * of messing with column widths, etc. */

static const char *get_sqlite_column_type(int type)
{
	switch(type) {
	case RQ_INTEGER1 :
	case RQ_UINTEGER1 :
	case RQ_INTEGER2 :
	case RQ_UINTEGER2 :
	case RQ_INTEGER3 :
	case RQ_UINTEGER3 :
	case RQ_INTEGER4 :
	case RQ_UINTEGER4 :
	case RQ_INTEGER8 :
		return "INTEGER";
	case RQ_UINTEGER8 : /* SQLite3 stores INTEGER as signed 8-byte */
	case RQ_CHAR :
	case RQ_DATE :
	case RQ_DATETIME :
		return "TEXT";
	case RQ_FLOAT :
		return "REAL";
	default :
		return "TEXT";
	}

	return "TEXT";
}

/*! \brief Create a table if ast_realtime_require shows that we are configured to handle the data
 */
static int handle_missing_table(struct realtime_sqlite3_db *db, const char *table, va_list ap)
{
	const char *column;
	int type, first = 1, res;
	size_t sz;
	struct ast_str *sql;

	if (!(sql = ast_str_create(128))) {
		return -1;
	}

	while ((column = va_arg(ap, typeof(column))) && (type = va_arg(ap, typeof(type))) && (sz = va_arg(ap, typeof(sz)))) {
		if (first) {
			ast_str_set(&sql, 0, "CREATE TABLE IF NOT EXISTS %s (%s %s", sqlite3_escape_table(table),
					sqlite3_escape_column(column), get_sqlite_column_type(type));
			first = 0;
		} else {
			ast_str_append(&sql, 0, ", %s %s", sqlite3_escape_column(column), get_sqlite_column_type(type));
		}
	}

	ast_str_append(&sql, 0, ")");

	res = realtime_sqlite3_execute_handle(db, ast_str_buffer(sql), NULL, NULL, 1) < 0 ? -1 : 0;
	ast_free(sql);

	return res;
}

/*! \brief If ast_realtime_require sends info about a column we don't have, create it
 */
static int handle_missing_column(struct realtime_sqlite3_db *db, const char *table, const char *column, int type, size_t sz)
{
	char *sql;
	const char *sqltype = get_sqlite_column_type(type);
	int res;

	if (db->requirements == REALTIME_SQLITE3_REQ_WARN) {
		ast_log(LOG_WARNING, "Missing column '%s' of type '%s' in %s.%s\n", column, sqltype, db->name, table);
		return -1;
	} else if (db->requirements == REALTIME_SQLITE3_REQ_CHAR) {
		sqltype = "TEXT";
	}

	if (!(sql = sqlite3_mprintf("ALTER TABLE \"%q\" ADD COLUMN \"%q\" %s", table, column, sqltype))) {
		return -1;
	}

	if (!(res = (realtime_sqlite3_execute_handle(db, sql, NULL, NULL, 1) < 0 ? -1 : 0))) {
		ast_log(LOG_NOTICE, "Creating column '%s' type %s for table %s\n", column, sqltype, table);
	}

	sqlite3_free(sql);

	return res;
}

static int str_hash_fn(const void *obj, const int flags)
{
	return ast_str_hash((const char *) obj);
}

static int str_cmp_fn(void *obj, void *arg, int flags) {
	return !strcasecmp((const char *) obj, (const char *) arg);
}

/*! \brief Callback for creating a hash of column names for comparison in realtime_sqlite3_require
 */
static int add_column_name(void *arg, int num_columns, char **values, char **columns)
{
	char *column;
	struct ao2_container *cnames = arg;


	if (!(column = ao2_alloc(strlen(values[1]) + 1, NULL))) {
		return -1;
	}

	strcpy(column, values[1]);

	ao2_link(cnames, column);
	ao2_ref(column, -1);

	return 0;
}

/*! \brief Callback for ast_realtime_require
 * \retval 0 Required fields met specified standards
 * \retval -1 One or more fields was missing or insufficient
 */
static int realtime_sqlite3_require(const char *database, const char *table, va_list ap)
{
	const char *column;
	char *sql;
	int type;
	int res;
	size_t sz;
	struct ao2_container *columns;
	struct realtime_sqlite3_db *db;

	/* SQLite3 columns are dynamically typed, with type affinity. Built-in functions will
	 * return the results as char * anyway. The only field that that cannot contain text
	 * data is an INTEGER PRIMARY KEY, which must be a 64-bit signed integer. So, for
	 * the purposes here we really only care whether the column exists and not what its
	 * type or length is. */

	if (ast_strlen_zero(table)) {
		ast_log(LOG_WARNING, "Must have a table to query!\n");
		return -1;
	}

	if (!(db = find_database(database))) {
		return -1;
	}

	if (!(columns = ao2_container_alloc(31, str_hash_fn, str_cmp_fn))) {
		unref_db(&db);
	   return -1;
	}

	if (!(sql = sqlite3_mprintf("PRAGMA table_info(\"%q\")", table))) {
		unref_db(&db);
		ao2_ref(columns, -1);
		return -1;
	}

	if ((res = realtime_sqlite3_execute_handle(db, sql, add_column_name, columns, 0)) < 0) {
		unref_db(&db);
		ao2_ref(columns, -1);
		sqlite3_free(sql);
		return -1;
	} else if (res == 0) {
		/* Table does not exist */
		sqlite3_free(sql);
		res = handle_missing_table(db, table, ap);
		ao2_ref(columns, -1);
		unref_db(&db);
		return res;
	}

	sqlite3_free(sql);

	while ((column = va_arg(ap, typeof(column))) && (type = va_arg(ap, typeof(type))) && (sz = va_arg(ap, typeof(sz)))) {
		char *found;
		if (!(found = ao2_find(columns, column, OBJ_POINTER | OBJ_UNLINK))) {
			if (handle_missing_column(db, table, column, type, sz)) {
				unref_db(&db);
				ao2_ref(columns, -1);
				return -1;
			}
		} else {
			ao2_ref(found, -1);
		}
	}

	ao2_ref(columns, -1);
	unref_db(&db);

	return 0;
}

/*! \brief Callback for clearing any cached info
 * \note We don't currently cache anything
 * \retval 0 If any cache was purged
 * \retval -1 If no cache was found
 */
static int realtime_sqlite3_unload(const char *database, const char *table)
{
	/* We currently do no caching */
	return -1;
}

/*! \brief Parse the res_config_sqlite3 config file
 */
static int parse_config(int reload)
{
	struct ast_config *config;
	struct ast_flags config_flags = { CONFIG_FLAG_NOREALTIME | (reload ? CONFIG_FLAG_FILEUNCHANGED : 0) };
	static const char *config_filename = "res_config_sqlite3.conf";

	config = ast_config_load(config_filename, config_flags);

	if (config == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "%s was unchanged, skipping parsing\n", config_filename);
		return 0;
	}

	ast_mutex_lock(&config_lock);

	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "%s config file '%s'\n",
			config == CONFIG_STATUS_FILEMISSING ? "Missing" : "Invalid", config_filename);
	} else {
		const char *cat;
		struct realtime_sqlite3_db *db;

		mark_all_databases_dirty();
		for (cat = ast_category_browse(config, NULL); cat; cat = ast_category_browse(config, cat)) {
			if (!strcasecmp(cat, "general")) {
				continue;
			}
			if (!(db = find_database(cat))) {
				if (!(db = new_realtime_sqlite3_db(config, cat))) {
					ast_log(LOG_WARNING, "Could not allocate new db for '%s' - skipping.\n", cat);
					continue;
				}
				if (db_open(db)) {
					unref_db(&db);
					continue;
				}
				db_start_batch(db);
				ao2_link(databases, db);
				unref_db(&db);
			} else  {
				if (update_realtime_sqlite3_db(db, config, cat)) {
					unref_db(&db);
					continue;
				}
				unref_db(&db);
			}
		}
		unlink_dirty_databases();
	}

	ast_mutex_unlock(&config_lock);

	ast_config_destroy(config);

	return 0;
}

static int reload_module(void)
{
	parse_config(1);
	return 0;
}

static void unload_module(void)
{
	ast_mutex_lock(&config_lock);
	ao2_callback(databases, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK, stop_batch_cb, NULL);
	ao2_ref(databases, -1);
	databases = NULL;
	ast_mutex_unlock(&config_lock);
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (!((databases = ao2_container_alloc(DB_BUCKETS, db_hash_fn, db_cmp_fn)))) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (parse_config(0)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_config_engine_register(&sqlite3_config_engine)) {
		ast_log(LOG_ERROR, "The config API must have changed, this shouldn't happen.\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_RELOADABLE(ASTERISK_GPL_KEY, "SQLite 3 realtime config engine");
