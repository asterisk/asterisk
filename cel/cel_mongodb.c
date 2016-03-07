 /*
  * Asterisk -- An open source telephony toolkit.
  *
  * Copyright (C) 2015 Catalin [catacs] Stanciu <catacsdev@gmail.com>
  *
  * Catalin Stanciu - adapted to MongoDB backend, from:
  * Steve Murphy <murf@digium.com>
  * Adapted from the PostgreSQL CEL logger
  *
  *
  * Modified March 2016
  * Catalin Stanciu <catacsdev@gmail.com>
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
  * \brief MongoDB CEL logger
  *
  * \author Catalin Stanciu <catacsdev@gmail.com>
  * MongoDB https://www.mongodb.org/
  *
  * See also
  * \arg \ref Config_cel
  * MongoDB https://www.mongodb.org/
  * \ingroup cel_drivers
  */

/*** MODULEINFO
    <depend>mongoclient</depend>
    <support_level>extended</support_level>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <bson.h>
#include <bcon.h>
#include <mongoc.h>

#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk.h"

#define MONGODB_BACKEND_NAME "CEL MongoDB backend"

static char *config = "cel_mongodb.conf";

static struct ast_event_sub *event_sub = NULL;

static char *hostname = NULL;
static char *dbname = NULL;
static char *dbcollection = NULL;
static char *dbuser = NULL;
static char *password = NULL;
static char *dbport = 0;

static int connected = 0;
static int MAXSIZE = 512;

/*! \brief show_user_def is off by default */
#define CEL_SHOW_USERDEF_DEFAULT	0

/*! TRUE if we should set the eventtype field to USER_DEFINED on user events. */
static unsigned char cel_show_user_def;

AST_MUTEX_DEFINE_STATIC(mongodb_lock);

static mongoc_client_t *client;

static void mongodb_reconnect(void) {
    struct ast_str *dburi = ast_str_create(MAXSIZE);
    if (!dburi) {
        ast_log(LOG_ERROR, "Failed to allocate memory for connection string.\n");
        return;
    }

    if (client) {
        mongoc_client_destroy(client);
        client = NULL;
        mongoc_cleanup();
    }

    if (ast_str_strlen(dbuser) && ast_str_strlen(password)) {
        ast_str_set(&dburi, 0, "mongodb://%s:%s@%s:%s", dbuser, password, hostname, dbport);
    } else {
        ast_str_set(&dburi, 0, "mongodb://%s:%s", hostname, dbport);
    }
    ast_debug(1, "mongodb_reconnect: Using mongo uri %s.\n", ast_str_buffer(dburi));

    mongoc_init ();
    client = mongoc_client_new(ast_str_buffer(dburi));
    ast_free(dburi);
}


static void mongodb_log(struct ast_event *event)
{
    mongoc_collection_t *collection;
    bson_error_t error;
    bson_oid_t oid;
    bson_t *doc;

    struct ast_cel_event_record record = {
        .version = AST_CEL_EVENT_RECORD_VERSION,
    };

    if (ast_cel_fill_record(event, &record)) {
        return;
    }

    ast_debug(1, "mongodb_log: Locking mongodb_lock.\n");
    ast_mutex_lock(&mongodb_lock);

    if ((!connected) && hostname && dbport) {
        mongodb_reconnect();
        if (client) {
            connected = 1;
        } else {
            ast_log(LOG_ERROR, "cel_mongodb: Unable to connect to database server %s.  Calls will not be logged!\n", hostname);
            client = NULL;
            mongoc_cleanup();
        }
    }
    if (connected) {
        collection = mongoc_client_get_collection (client, dbname, dbcollection);

        if (!collection) {
            ast_log(LOG_ERROR, "mongodb_log: Unable to connect to database server %s at colection %s.  Calls will not be logged!\n", hostname, dbcollection);
            return;
        }

        ast_log(LOG_NOTICE,  "mongodb_log: MongoDB event from cell.\n");
        ast_debug(1, "mongodb_log: Got connection, Preparing document.\n");

        doc = bson_new ();
        bson_oid_init (&oid, NULL);
        BSON_APPEND_OID (doc, "_id", &oid);
        BSON_APPEND_INT32 (doc, "eventtype", record.event_type);
        BSON_APPEND_DATE_TIME (doc, "eventtime", record.event_time.tv_sec*1000);
        BSON_APPEND_UTF8 (doc, "cid_name", record.caller_id_name);
        BSON_APPEND_UTF8 (doc, "cid_num", record.caller_id_num);
        BSON_APPEND_UTF8 (doc, "cid_ani", record.caller_id_ani);
        BSON_APPEND_UTF8 (doc, "cid_rdnis", record.caller_id_rdnis);
        BSON_APPEND_UTF8 (doc, "cid_dnid", record.caller_id_dnid);
        BSON_APPEND_UTF8 (doc, "exten", record.extension);
        BSON_APPEND_UTF8 (doc, "context", record.context);
        BSON_APPEND_UTF8 (doc, "channame", record.channel_name);
        BSON_APPEND_UTF8 (doc, "appname", record.application_name);
        BSON_APPEND_UTF8 (doc, "appdata", record.application_data);
        BSON_APPEND_INT64 (doc, "amaflags", record.amaflag);
        BSON_APPEND_UTF8 (doc, "accountcode", record.account_code);
        BSON_APPEND_UTF8 (doc, "peeraccount", record.peer_account);
        BSON_APPEND_UTF8 (doc, "uniqueid", record.unique_id);
        BSON_APPEND_UTF8 (doc, "linkedid", record.linked_id);
        BSON_APPEND_UTF8 (doc, "userfield", record.user_field);
        BSON_APPEND_UTF8 (doc, "peer", record.peer);
        if (record.event_type == 0) {
            BSON_APPEND_INT32 (doc, "userdeftype", record.user_defined_name);
        }
        BSON_APPEND_UTF8 (doc, "extra", record.extra);
        BSON_APPEND_INT32 (doc, "version", record.version);

        ast_debug(1, "Inserting a CEL record.\n");
        if (!mongoc_collection_insert (collection, MONGOC_INSERT_NONE, doc, NULL, &error)) {
            fprintf (stderr, "%s\n", error.message);
            ast_log(LOG_ERROR, "mongodb_log: MongoDB failed to insert to %s!\n", error.message);
        }

        bson_destroy (doc);
        mongoc_collection_destroy(collection);
    }

    ast_mutex_unlock(&mongodb_lock);
}


static int process_load_module(struct ast_config *cfg)
{
    struct ast_variable *var;
    struct ast_str *dburi = ast_str_create(MAXSIZE);
    const char *tmp;

    mongoc_collection_t  *collection;
    bson_error_t    error;

    if (!(var = ast_variable_browse(cfg, "global"))) {
        ast_log(LOG_WARNING,"CEL mongodb config file missing global section.\n");
        return AST_MODULE_LOAD_DECLINE;
    }
    if (!(tmp = ast_variable_retrieve(cfg,"global","hostname"))) {
        ast_log(LOG_WARNING,"MongoDB server hostname not specified.  Assuming unix socket connection\n");
        tmp = "";	/* connect via UNIX-socket by default */
    }
    if (hostname) {
        ast_free(hostname);
    }
    if (!(hostname = ast_strdup(tmp))) {
        ast_log(LOG_WARNING,"MongoDB Ran out of memory copying host info\n");
        return AST_MODULE_LOAD_DECLINE;
    }
    if (!(tmp = ast_variable_retrieve(cfg, "global", "dbname"))) {
        ast_log(LOG_WARNING,"MongoDB database not specified.  Assuming asterisk\n");
        tmp = "asteriskceldb";
    }
    if (dbname) {
        ast_free(dbname);
    }
    if (!(dbname = ast_strdup(tmp))) {
        ast_log(LOG_WARNING,"MongoDB Ran out of memory copying dbname info\n");
        return AST_MODULE_LOAD_DECLINE;
    }
    if (!(tmp = ast_variable_retrieve(cfg, "global", "username"))) {
        ast_log(LOG_WARNING,"MongoDB database user not specified.  Assuming blank\n");
        tmp = "";
    }
    if (dbuser) {
        ast_free(dbuser);
    }
    if (!(dbuser = ast_strdup(tmp))) {
        ast_log(LOG_WARNING,"MongoDB Ran out of memory copying user info\n");
        return AST_MODULE_LOAD_DECLINE;
    }
    if (!(tmp = ast_variable_retrieve(cfg, "global", "password"))) {
        ast_log(LOG_WARNING, "MongoDB database password not specified.  Assuming blank\n");
        tmp = "";
    }
    if (password) {
        ast_free(password);
    }
    if (!(password = ast_strdup(tmp))) {
        ast_log(LOG_WARNING,"MongoDB Ran out of memory copying password info\n");
        return AST_MODULE_LOAD_DECLINE;
    }
    if (!(tmp = ast_variable_retrieve(cfg,"global","port"))) {
        ast_log(LOG_WARNING,"MongoDB database port not specified.  Using default 27017.\n");
        tmp = "27017";
    }
    if (dbport) {
        ast_free(dbport);
    }
    if (!(dbport = ast_strdup(tmp))) {
        ast_log(LOG_WARNING,"MongoDB Ran out of memory copying port info\n");
        return AST_MODULE_LOAD_DECLINE;
    }
    if (!(tmp = ast_variable_retrieve(cfg, "global", "collection"))) {
        ast_log(LOG_WARNING,"CEL table not specified.  Assuming cel\n");
        tmp = "cel";
    }
    if (dbcollection)
        ast_free(dbcollection);
    if (!(dbcollection = ast_strdup(tmp))) {
        return AST_MODULE_LOAD_DECLINE;
    }
    cel_show_user_def = CEL_SHOW_USERDEF_DEFAULT;
    if ((tmp = ast_variable_retrieve(cfg, "global", "show_user_defined"))) {
        cel_show_user_def = ast_true(tmp) ? 1 : 0;
    }

    if (option_debug) {
        if (ast_strlen_zero(hostname)) {
            ast_debug(3, "cel_mongodb: using default unix socket\n");
        } else {
            ast_debug(3, "cel_mongodb: got hostname of %s\n", hostname);
        }
        ast_debug(3, "cel_mongodb: got port of %s\n", dbport);
        ast_debug(3, "cel_mongodb: got user of %s\n", dbuser);
        ast_debug(3, "cel_mongodb: got dbname of %s\n", dbname);
        ast_debug(3, "cel_mongodb: got password of %s\n", password);
        ast_debug(3, "cel_mongodb: got collection name of %s\n", dbcollection);
        ast_debug(3, "cel_mongodb: got show_user_defined of %s\n",
            cel_show_user_def ? "Yes" : "No");
    }

    mongodb_reconnect();
    if (client) {
        collection = mongoc_client_get_collection (client, dbname, dbcollection);
        if (mongoc_collection_count (collection, MONGOC_QUERY_NONE, NULL, 0, 0, NULL, &error) < 0) {
            ast_log(LOG_ERROR, "Method: process_load_module, MongoDB failed to connect to %s!\n", ast_str_buffer(dburi));
            ast_log(LOG_ERROR, "Method: process_load_module, Error %s \n", error.message);
        } else {
            ast_debug(1, "Successfully connected to MongoDB database.\n");
        }
        mongoc_collection_destroy(collection);
    } else {
        ast_log(LOG_ERROR, "cel_mongodb: Unable to connect to database server %s.  Calls will not be logged!\n", hostname);
        client = NULL;
        connected = 0;
        mongoc_cleanup();
    }
    return AST_MODULE_LOAD_SUCCESS;
}


static int _load_module(int reload)
{

    struct ast_config *cfg;
    struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
    ast_log(LOG_WARNING, "_load_module: Start\n");

    if ((cfg = ast_config_load(config, config_flags)) == NULL || cfg == CONFIG_STATUS_FILEINVALID) {
        ast_log(LOG_WARNING, "Unable to load config for MongoDB CEL's: %s\n", config);
        return AST_MODULE_LOAD_DECLINE;
    } else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
        return AST_MODULE_LOAD_SUCCESS;
    }

    process_load_module(cfg);
    ast_config_destroy(cfg);

    if (ast_cel_backend_register(MONGODB_BACKEND_NAME, mongodb_log)) {
        ast_log(LOG_WARNING, "Unable to subscribe to CEL events for mongodb\n");
        return AST_MODULE_LOAD_DECLINE;
    }

    return AST_MODULE_LOAD_SUCCESS;
}


static int load_module(void)
{
	return _load_module(0);
}


static int _unload_module(void)
{
    ast_cel_backend_unregister(MONGODB_BACKEND_NAME);

    if (client) {
        mongoc_client_destroy(client);
        client = NULL;
        mongoc_cleanup();
    }

    mongoc_cleanup();

    if (hostname) {
        ast_free(hostname);
        hostname = NULL;
    }
    if (dbname) {
        ast_free(dbname);
        dbname = NULL;
    }
    if (dbuser) {
        ast_free(dbuser);
        dbuser = NULL;
    }
    if (password) {
        ast_free(password);
        password = NULL;
    }
    if (dbport) {
        ast_free(dbport);
        dbport = NULL;
    }
    if (dbcollection) {
        ast_free(dbcollection);
        dbcollection = NULL;
    }
    return 0;
}


static int unload_module(void)
{
    return _unload_module();
}


static int reload(void)
{
	return _load_module(1);
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MongoDB CEL Backend",
    .support_level = AST_MODULE_SUPPORT_EXTENDED,
    .load = load_module,
    .unload = unload_module,
    .reload = reload,
    .load_pri = AST_MODPRI_CDR_DRIVER,
);
