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
 * \brief Common logic for the CDR and CEL Custom Backends
 *
 * All source files are in the res/cdrel_custom directory.
 *
 * "config.c": Contains common configuration file parsing the ultimate goal
 * of which is to create a vector of cdrel_config structures for each of
 * the cdr_custom, cdr_sqlite3_custom, cel_custom and cel_sqlite3_custom
 * modules. Each cdrel_config object represents an output file defined in
 * their respective config files.  Each one contains a vector of cdrel_field
 * objects, one for each field in the output record, plus settings like
 * the output file name, backend type (text file or database), config
 * type ((legacy or advanced), the field separator and quote character to
 * use.
 *
 * Each cdrel_field object contains an abstract field id that points to
 * a ast_cdr structure member or CEL event field id along with an input
 * type and an output type.  The registry of cdrel_fields is located in
 * registry.c.
 *
 * "loggers.c": Contains the common "cdrel_logger" entrypoint that the
 * individual modules call to log a record.  It takes the module's
 * cdrel_configs vector and the record to log it got from the core
 * cel.c and cdr.c.  It then looks up and runs the logger implementation
 * based on the backend type (text file or database) and config type
 * (legacy or advanced).
 *
 * "getters_cdr.c", "getters_cel.c": Contain the getters that retrieve
 * values from the ast_cdr or ast_event structures based on the field
 * id and input type defined for that field and create a cdrel_value
 * wrapper object for it.
 *
 * "writers.c": Contains common backend writers for the text file and
 * database backends.
 *
 * The load-time flow...
 *
 * Each of the individual cdr/cel custom modules call the common
 * cdrel_load_module function with their backend_type, record_type
 * (cdr or cel), config file name, and the logging callback that
 * should be registered with the core cdr or cel facility.
 *
 * cdrel_load_module calls the config load function appropriate for
 * the backend type, each of which parses the config file and, if
 * successful, registers the calling module with the cdr or cel core
 * and creates a vector of cdrel_config objects that is passed
 * back to the calling module.  That vector contains the context for
 * all future operations.
 *
 * The run-time flow...
 *
 * The core cdr and cel modules use their registries of backends and call
 * the callback function registered by the 4 cdr and cel custom modules.
 * No changes there.
 *
 * Each of those modules call the common cdrel_logger function with their
 * cdrel_configs vector and the actual ast_cdr or ast_event structure to log.
 * The cdrel_logger function iterates over the cdrel_configs vector and for
 * each invokes the logger implementation specific to the backend type
 * (text file or database) and config type (legacy or advanced).
 *
 * For legacy config types, the logger implementation simply calls
 * ast_str_substitute_variables() on the whole opaque format and writes
 * the result to the text file or database.
 *
 * For advanced configs, the logger implementation iterates over each field
 * in the cdrel_config's fields vector and for each, calls the appropriate
 * getter based on the record type (cdr or cel) and field id. Each getter
 * call returns a cdrel_value object which is then passed to a field formatter
 * looked up based on the field's data type (string, int32, etc). The formatter
 * is also passed the cdrel_config object and the desired output type and
 * returns the final value in another cdrel_value object formatted with any
 * quoting, etc. needed.  The logger accumulates the output cdrel_values
 * (which are all now strings) in another vector and after all fields have
 * been processed, hands the vector over to one of the backend writers.
 *
 * The backend writer concatenates the cdrel_values into an output record
 * using the config's separator setting and writes it to the text file
 * or database.  For the JSON output format, it creates a simple
 * name/value pair output record.
 *
 * The identification of field data types, field ids, record types and
 * backend types is all done at config load time and saved in the
 * cdrel_config and cdrel_field objects. The callbacks for getters, formatters
 * and writers are also loaded when the res_cdrel_custom module loads
 * and stored in arrays indexed by their enum values.  The result is that
 * at run time, simple array indexing is all that's needed to get the
 * proper getter, formatter and writer for any logging request.
 *
 */

/*** MODULEINFO
	<depend>sqlite3</depend>
	<support_level>core</support_level>
 ***/


#include "asterisk.h"

#include "asterisk/module.h"
#include "cdrel_custom/cdrel.h"

/*
 * Populated by cdrel_custom/getters_cdr.c and cdrel_custom/getters_cel.c.
 */
cdrel_field_getter cdrel_field_getters[cdrel_record_type_end][cdrel_data_type_end];

/*
 * Populated by cdrel_custom/formatters.c.
 */
cdrel_field_formatter cdrel_field_formatters[cdrel_data_type_end];

/*
 * Populated by cdrel_custom/writers.c.
 */
cdrel_backend_writer cdrel_backend_writers[cdrel_format_type_end];

/*
 * Populated by cdrel_custom/getters_cdr.c and cdrel_custom/getters_cel.c.
 */
cdrel_dummy_channel_alloc cdrel_dummy_channel_allocators[cdrel_format_type_end];

/*
 * You must ensure that there's an entry for every value in the enum.
 */

const char *cdrel_record_type_map[] = {
	[cdrel_record_cdr] = "CDR",
	[cdrel_record_cel] = "CEL",
	[cdrel_record_type_end] = "!!END!!",
};

const char *cdrel_module_type_map[] = {
	[cdrel_backend_text] = "Custom ",
	[cdrel_backend_db] = "SQLITE3 Custom",
	[cdrel_backend_type_end] = "!!END!!",
};

const char *cdrel_data_type_map[] = {
	[cdrel_type_string] = "string",
	[cdrel_type_timeval] = "timeval",
	[cdrel_type_literal] = "literal",
	[cdrel_type_amaflags] = "amaflags",
	[cdrel_type_disposition] = "disposition",
	[cdrel_type_uservar] = "uservar",
	[cdrel_type_event_type] = "event_type",
	[cdrel_type_event_enum] = "event_enum",
	[cdrel_data_type_strings_end] = "!!STRINGS END!!",
	[cdrel_type_int32] = "int32",
	[cdrel_type_uint32] = "uint32",
	[cdrel_type_int64] = "int64",
	[cdrel_type_uint64] = "uint64",
	[cdrel_type_float] = "float",
	[cdrel_data_type_end] = "!!END!!",
};

enum cdrel_data_type cdrel_data_type_from_str(const char *str)
{
	enum cdrel_data_type data_type = 0;
	for (data_type = 0; data_type < cdrel_data_type_end; data_type++) {
		if (strcasecmp(cdrel_data_type_map[data_type], str) == 0) {
			return data_type;
		}

	}
	return cdrel_data_type_end;
}

static const char *cdrel_field_flags_map[] = {
	[CDREL_FIELD_FLAG_QUOTE] = "quote",
	[CDREL_FIELD_FLAG_NOQUOTE] = "noquote",
	[CDREL_FIELD_FLAG_TYPE_FORCED] = "type_forced",
	[CDREL_FIELD_FLAG_USERVAR] = "uservar",
	[CDREL_FIELD_FLAG_LITERAL] = "literal",
	[CDREL_FIELD_FLAG_FORMAT_SPEC] = "format_spec",
	[CDREL_FIELD_FLAG_LAST] = "LAST",
};

const char *cdrel_get_field_flags(struct ast_flags *flags, struct ast_str **str)
{
	int ix = 0;
	int res = 0;
	int trues = 0;

	for (ix = 0; ix < CDREL_FIELD_FLAG_LAST; ix++) {
		if (ast_test_flag(flags, (1 << ix))) {
			res = ast_str_append(str, -1, "%s%s", trues++ ? "," : "", cdrel_field_flags_map[ix]);
			if (res < 0) {
				return "";
			}
		}
	}
	return ast_str_buffer(*str);
}


const char *cdrel_basename(const char *path)
{
	int i = 0;
	const char *basename = path;

	if (ast_strlen_zero(path)) {
		return path;
	}
	i = strlen(path) - 1;
	while(i >= 0) {
		if (path[i] == '/') {
			basename = &path[i + 1];
			break;
		}
		i--;
	}
	return basename;
}

static int unload_module(void)
{
	return 0;
}

static enum ast_module_load_result load_module(void)
{
	load_cdr();
	load_cel();
	load_formatters();
	load_writers();
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
	"Combined logic for CDR/CEL Custom modules",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
