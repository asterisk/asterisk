/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
 * \author George Joseph <gjoseph@sangoma.com>
 *
 * \brief Private header for res_cdrel_custom.
 *
 */

#ifndef _CDREL_H
#define _CDREL_H

#include <sqlite3.h>

#include "asterisk.h"
#include "asterisk/cdr.h"
#include "asterisk/cel.h"
#include "asterisk/event.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"
#include "asterisk/res_cdrel_custom.h"

extern const char *cdrel_record_type_map[];
#define RECORD_TYPE_STR(_rt) (cdrel_record_type_map[_rt])

extern const char *cdrel_module_type_map[];
#define MODULE_TYPE_STR(_mt) (cdrel_module_type_map[_mt])

enum cdrel_text_format_type {
	cdrel_format_dsv = 0,
	cdrel_format_json,
	cdrel_format_sql,
	cdrel_format_type_end,
};

enum cdrel_config_type {
	cdrel_config_legacy = 0,
	cdrel_config_advanced,
	cdrel_config_type_end,
};

enum cdrel_quoting_method {
	cdrel_quoting_method_none = 0,
	cdrel_quoting_method_all,
	cdrel_quoting_method_minimal,
	cdrel_quoting_method_non_numeric,
	cdrel_quoting_method_end,
};

/*
 * ORDER IS IMPORTANT!
 * The string output data types need to be first.
 */
enum cdrel_data_type {
	cdrel_type_string = 0,
	cdrel_type_timeval,
	cdrel_type_literal,
	cdrel_type_amaflags,
	cdrel_type_disposition,
	cdrel_type_uservar,
	cdrel_type_event_type,
	cdrel_type_event_enum,
	cdrel_data_type_strings_end,
	cdrel_type_int32,
	cdrel_type_uint32,
	cdrel_type_int64,
	cdrel_type_uint64,
	cdrel_type_float,
	cdrel_data_type_end
};

extern const char *cdrel_data_type_map[];
#define DATA_TYPE_STR(_dt) (_dt < cdrel_data_type_end ? cdrel_data_type_map[_dt] : NULL)
enum cdrel_data_type cdrel_data_type_from_str(const char *str);

#define CDREL_FIELD_FLAG_QUOTE (0)
#define CDREL_FIELD_FLAG_NOQUOTE (1)
#define CDREL_FIELD_FLAG_TYPE_FORCED (2)
#define CDREL_FIELD_FLAG_USERVAR (3)
#define CDREL_FIELD_FLAG_LITERAL (4)
#define CDREL_FIELD_FLAG_FORMAT_SPEC (5)
#define CDREL_FIELD_FLAG_LAST (6)

enum cdrel_field_flags {
	cdrel_flag_quote = (1 << CDREL_FIELD_FLAG_QUOTE),
	cdrel_flag_noquote = (1 << CDREL_FIELD_FLAG_NOQUOTE),
	cdrel_flag_type_forced = (1 << CDREL_FIELD_FLAG_TYPE_FORCED),
	cdrel_flag_uservar = (1 << CDREL_FIELD_FLAG_USERVAR),
	cdrel_flag_literal = (1 << CDREL_FIELD_FLAG_LITERAL),
	cdrel_flag_format_spec = (1 << CDREL_FIELD_FLAG_FORMAT_SPEC),
	cdrel_field_flags_end = (1 << CDREL_FIELD_FLAG_LAST),
};

/*
 * CEL has a few synthetic fields that aren't defined
 * in event.h so we'll define them ourselves after the
 * last official id.
 */
#define AST_EVENT_IE_CEL_LITERAL (AST_EVENT_IE_TOTAL + 1)
#define AST_EVENT_IE_CEL_EVENT_ENUM (AST_EVENT_IE_TOTAL + 2)
#define LAST_CEL_ID AST_EVENT_IE_CEL_EVENT_ENUM

/*!
 * While CEL event fields are published as a generic AST_EVENT with
 * a field id assigned to each field, CDRs are published as a fixed
 * ast_cdr structure.  To make it easier to share lower level code,
 * we assign pseudo-field-ids to each CDR field that are really the offset
 * of the field in the structure.  This allows us to generically get
 * any field using its id just like we do for CEL.
 *
 * To avoid conflicts with the existing CEL field ids, we'll start these
 * after the last one.
 */
#define CDR_OFFSET_SHIFT (LAST_CEL_ID + 1)
#define CDR_OFFSETOF(_field) (offsetof(struct ast_cdr, _field) + CDR_OFFSET_SHIFT)
#define CDR_FIELD(__cdr, __offset) (((void *)__cdr) + __offset - CDR_OFFSET_SHIFT)

enum cdr_field_id {
	cdr_field_literal = -1,
	cdr_field_clid = CDR_OFFSETOF(clid),
	cdr_field_src = CDR_OFFSETOF(src),
	cdr_field_dst = CDR_OFFSETOF(dst),
	cdr_field_dcontext = CDR_OFFSETOF(dcontext),
	cdr_field_channel = CDR_OFFSETOF(channel),
	cdr_field_dstchannel = CDR_OFFSETOF(dstchannel),
	cdr_field_lastapp = CDR_OFFSETOF(lastapp),
	cdr_field_lastdata = CDR_OFFSETOF(lastdata),
	cdr_field_start = CDR_OFFSETOF(start),
	cdr_field_answer = CDR_OFFSETOF(answer),
	cdr_field_end = CDR_OFFSETOF(end),
	cdr_field_duration = CDR_OFFSETOF(duration),
	cdr_field_billsec = CDR_OFFSETOF(billsec),
	cdr_field_disposition = CDR_OFFSETOF(disposition),
	cdr_field_amaflags = CDR_OFFSETOF(amaflags),
	cdr_field_accountcode = CDR_OFFSETOF(accountcode),
	cdr_field_peeraccount = CDR_OFFSETOF(peeraccount),
	cdr_field_flags = CDR_OFFSETOF(flags),
	cdr_field_uniqueid = CDR_OFFSETOF(uniqueid),
	cdr_field_linkedid = CDR_OFFSETOF(linkedid),
	cdr_field_tenantid = CDR_OFFSETOF(tenantid),
	cdr_field_peertenantid = CDR_OFFSETOF(peertenantid),
	cdr_field_userfield = CDR_OFFSETOF(userfield),
	cdr_field_sequence = CDR_OFFSETOF(sequence),
	cdr_field_varshead = CDR_OFFSETOF(varshead),
};


struct cdrel_field;

/*!
 * \internal
 * \brief A generic value wrapper structure.
 */
struct cdrel_value {
	char *field_name;
	enum cdrel_data_type data_type;
	int mallocd;
	union {
		char *string;
		int32_t int32;
		uint32_t uint32;
		int64_t int64;
		uint64_t uint64;
		struct timeval tv;
		float floater;
	} values;
};

/*!
 * \internal
 * \brief A vector of cdrel_values.
 */
AST_VECTOR(cdrel_values, struct cdrel_value *);

/*!
 * \internal
 * \brief Getter callbacks.
 *
 * \param record An ast_cdr or ast_event structure.
 * \param config Config object.
 * \param field Field object.
 * \param value A pointer to a cdrel_value structure to populate.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
typedef int (*cdrel_field_getter)(void *record, struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *value);
/*!
 * \internal
 * \brief The table of getter callbacks.  Populated by getters_cdr.c and getters_cel.c.
 *
 * Defined in res_cdrel_custom.c
 */
extern cdrel_field_getter cdrel_field_getters[cdrel_record_type_end][cdrel_data_type_end];

/*!
 * \internal
 * \brief Data type formatters.
 *
 * \param config Config object.
 * \param field Field object.
 * \param input_value A pointer to a cdrel_value structure with the data to format.
 * \param output_value A pointer to a cdrel_value structure to receive the formatted data.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
typedef int (*cdrel_field_formatter)(struct cdrel_config *config,
	struct cdrel_field *field, struct cdrel_value *input_value, struct cdrel_value *output_value);
/*!
 * \internal
 * \brief The table of formatter callbacks.  Populated by formatters.c.
 *
 * Defined in res_cdrel_custom.c
 */
extern cdrel_field_formatter cdrel_field_formatters[cdrel_data_type_end];

/*!
 * \internal
 * \brief Backend writers.
 *
 * \param config Config object.
 * \param values A vector of cdrel_values to write.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
typedef int (*cdrel_backend_writer)(struct cdrel_config *config, struct cdrel_values *values);
/*!
 * \internal
 * \brief The table of writer callbacks.  Populated by writers.c.
 *
 * Defined in res_cdrel_custom.c
 */
extern cdrel_backend_writer cdrel_backend_writers[cdrel_format_type_end];

/*!
 * \internal
 * \brief Dummy channel allocators.
 *
 * Legacy configurations use dialplan functions so they need a dummy channel
 * to operate on. CDR and CEL each have their own allocator.
 *
 * \param config Config object.
 * \param record An ast_cdr or ast_event structure.
 * \return an ast_channel or NULL on failure.
 */
typedef struct ast_channel * (*cdrel_dummy_channel_alloc)(struct cdrel_config *config, void *record);
extern cdrel_dummy_channel_alloc cdrel_dummy_channel_allocators[cdrel_format_type_end];


/*! Represents a field definition */
struct cdrel_field {
	enum cdrel_record_type record_type;     /*!< CDR or CEL */
	int field_id;                           /*!< May be an AST_EVENT_IE_CEL or a cdr_field_id */
	char *name;                             /*!< The name of the field */
	enum cdrel_data_type input_data_type;   /*!< The data type of the field in the source record */
	enum cdrel_data_type output_data_type;
	struct ast_flags flags;                 /*!< Flags used during config parsing */
	char data[1];                           /*!< Could be literal data, a user variable name, etc */
};

/*! Represents an output definition from a config file */
struct cdrel_config {
	enum cdrel_record_type record_type;            /*!< CDR or CEL */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(config_filename);         /*!< Input configuration filename */
		AST_STRING_FIELD(output_filename);         /*!< Output text file or database */
		AST_STRING_FIELD(template);                /*!< Input template */
		AST_STRING_FIELD(db_columns);              /*!< List of columns for database backends */
		AST_STRING_FIELD(db_table);                /*!< Table name for database backends */
	);
	sqlite3 *db;                                   /*!< sqlite3 database handle */
	sqlite3_stmt *insert;                          /*!< sqlite3 prepared statement for insert */
	int busy_timeout;                              /*!< sqlite3 query timeout value */
	cdrel_dummy_channel_alloc dummy_channel_alloc; /*!< Legacy config types need a dummy channel */
	enum cdrel_backend_type backend_type;          /*!< Text file or database */
	enum cdrel_config_type config_type;            /*!< Legacy or advanced */
	enum cdrel_text_format_type format_type;       /*!< For text files, CSV or JSON */
	enum cdrel_quoting_method quoting_method;      /*!< When to quote */
	char separator[2];                             /*!< For text files, the field separator */
	char quote[2];                                 /*!< For text files, the quote character */
	char quote_escape[2];                          /*!< For text files, character to use to escape embedded quotes */
	AST_VECTOR(, struct cdrel_field *) fields;     /*!< Vector of fields for this config */
	ast_mutex_t lock;                              /*!< Lock that serializes filesystem writes */
};

/*!
 * \internal
 * \brief Get a cdrel_field structure by record type and field name.
 *
 * \param record_type The cdrel_record_type to search.
 * \param name The field name to search for.
 * \returns A pointer to a constant cdrel_field structure or NULL if not found.
 *          This pointer must never be freed.
 */
const struct cdrel_field *get_registered_field_by_name(enum cdrel_record_type record_type,
	const char *name);

/*!
 * \internal
 * \brief Write a record to a text file
 *
 * \param config The configuration object.
 * \param record The data to write.
 * \retval 0 on success
 * \retval -1 on failure
 */
int write_record_to_file(struct cdrel_config *config, struct ast_str *record);

/*!
 * \internal
 * \brief Write a record to a database
 *
 * \param config The configuration object.
 * \param values The values to write.
 * \retval 0 on success
 * \retval -1 on failure
 */
int write_record_to_database(struct cdrel_config *config, struct cdrel_values *values);

/*!
 * \internal
 * \brief Return the basename of a path
 *
 * \param path
 * \returns Pointer to last '/' or path if no '/' was found.
 */
const char *cdrel_basename(const char *path);

/*!
 * \internal
 * \brief Get a string representing a field's flags
 *
 * \param flags An ast_flags structure
 * \param str Pointer to ast_str* buffer
 * \returns A string of field flag names
 */
const char *cdrel_get_field_flags(struct ast_flags *flags, struct ast_str **str);


int load_cdr(void);
int load_cel(void);
int load_formatters(void);
int load_writers(void);

#endif /* _CDREL_H */
