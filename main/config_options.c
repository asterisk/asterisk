/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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
 * \brief Configuration Option-handling
 * \author Terry Wilson <twilson@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <regex.h>

#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/stringfields.h"
#include "asterisk/acl.h"
#include "asterisk/frame.h"

#ifdef LOW_MEMORY
#define CONFIG_OPT_BUCKETS 5
#else
#define CONFIG_OPT_BUCKETS 53
#endif /* LOW_MEMORY */

/*! \brief Bits of aco_info that shouldn't be assigned outside this file
 * \internal
 */
struct aco_info_internal {
	void *pending;              /*!< The user-defined config object awaiting application */
};

struct aco_type_internal {
	regex_t *regex;
	struct ao2_container *opts; /*!< The container of options registered to the aco_info */
};

struct aco_option {
	const char *name;
	const char *aliased_to;
	const char *default_val;
	enum aco_matchtype match_type;
	regex_t *name_regex;
	struct aco_type **obj;
	enum aco_option_type type;
	aco_option_handler handler;
	unsigned int flags;
	unsigned char deprecated:1;
	size_t argc;
	intptr_t args[0];
};

void *aco_pending_config(struct aco_info *info)
{
	if (!(info && info->internal)) {
		ast_log(LOG_ERROR, "This may not be called without an initialized aco_info!\n");
		return NULL;
	}
	return info->internal->pending;
}

static void config_option_destroy(void *obj)
{
	struct aco_option *opt = obj;
	if (opt->match_type == ACO_REGEX && opt->name_regex) {
		regfree(opt->name_regex);
		ast_free(opt->name_regex);
	}
}

static int int_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int uint_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int double_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int sockaddr_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int stringfield_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int bool_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int boolflag_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int acl_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int codec_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int noop_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int chararray_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);

static aco_option_handler ast_config_option_default_handler(enum aco_option_type type)
{
	switch(type) {
	case OPT_ACL_T: return acl_handler_fn;
	case OPT_BOOL_T: return bool_handler_fn;
	case OPT_BOOLFLAG_T: return boolflag_handler_fn;
	case OPT_CHAR_ARRAY_T: return chararray_handler_fn;
	case OPT_CODEC_T: return codec_handler_fn;
	case OPT_DOUBLE_T: return double_handler_fn;
	case OPT_INT_T: return int_handler_fn;
	case OPT_NOOP_T: return noop_handler_fn;
	case OPT_SOCKADDR_T: return sockaddr_handler_fn;
	case OPT_STRINGFIELD_T: return stringfield_handler_fn;
	case OPT_UINT_T: return uint_handler_fn;

	case OPT_CUSTOM_T: return NULL;
	}

	return NULL;
}

static regex_t *build_regex(const char *text)
{
	int res;
	regex_t *regex;

	if (!(regex = ast_malloc(sizeof(*regex)))) {
		return NULL;
	}

	if ((res = regcomp(regex, text, REG_EXTENDED | REG_ICASE | REG_NOSUB))) {
		size_t len = regerror(res, regex, NULL, 0);
		char buf[len];
		regerror(res, regex, buf, len);
		ast_log(LOG_ERROR, "Could not compile regex '%s': %s\n", text, buf);
		ast_free(regex);
		return NULL;
	}

	return regex;
}

static int link_option_to_types(struct aco_type **types, struct aco_option *opt)
{
	size_t idx = 0;
	struct aco_type *type;

	while ((type = types[idx++])) {
		if (!type->internal) {
			ast_log(LOG_ERROR, "Attempting to register option using uninitialized type\n");
			return -1;
		}
		if (!ao2_link(type->internal->opts, opt)) {
			while (--idx) {
				ao2_unlink(types[idx]->internal->opts, opt);
			}
			return -1;
		}
		/* The container should hold the only ref to opt */
		ao2_ref(opt, -1);
	}
	return 0;
}

int aco_option_register_deprecated(struct aco_info *info, const char *name, struct aco_type **types, const char *aliased_to)
{
	struct aco_option *opt;

	if (!info || ast_strlen_zero(name) || ast_strlen_zero(aliased_to)) {
		return -1;
	}

	if (!(opt = ao2_alloc(sizeof(*opt), config_option_destroy))) {
		return -1;
	}

	opt->name = name;
	opt->aliased_to = aliased_to;
	opt->deprecated = 1;
	opt->match_type = ACO_EXACT;

	if (link_option_to_types(types, opt)) {
		ao2_ref(opt, -1);
		return -1;
	}

	return 0;
}

int __aco_option_register(struct aco_info *info, const char *name, enum aco_matchtype matchtype, struct aco_type **types,
	const char *default_val, enum aco_option_type kind, aco_option_handler handler, unsigned int flags, size_t argc, ...)
{
	struct aco_option *opt;
	va_list ap;
	int tmp;

	/* Custom option types require a handler */
	if (!handler && kind == OPT_CUSTOM_T) {
		return -1;
	}

	if (!(types && types[0])) {
		return -1;
	}

	if (!(opt = ao2_alloc(sizeof(*opt) + argc * sizeof(opt->args[0]), config_option_destroy))) {
		return -1;
	}

	if (matchtype == ACO_REGEX && !(opt->name_regex = build_regex(name))) {
		ao2_ref(opt, -1);
		return -1;
	}

	va_start(ap, argc);
	for (tmp = 0; tmp < argc; tmp++) {
		opt->args[tmp] = va_arg(ap, size_t);
	}
	va_end(ap);

	opt->name = name;
	opt->match_type = matchtype;
	opt->default_val = default_val;
	opt->type = kind;
	opt->handler = handler;
	opt->flags = flags;
	opt->argc = argc;

	if (!opt->handler && !(opt->handler = ast_config_option_default_handler(opt->type))) {
		/* This should never happen */
		ast_log(LOG_ERROR, "No handler provided, and no default handler exists for type %d\n", opt->type);
		ao2_ref(opt, -1);
		return -1;
	};

	if (link_option_to_types(types, opt)) {
		ao2_ref(opt, -1);
		return -1;
	}

	return 0;
}

static int config_opt_hash(const void *obj, const int flags)
{
	const struct aco_option *opt = obj;
	const char *name = (flags & OBJ_KEY) ? obj : opt->name;
	return ast_str_case_hash(name);
}

static int config_opt_cmp(void *obj, void *arg, int flags)
{
	struct aco_option *opt1 = obj, *opt2 = arg;
	const char *name = (flags & OBJ_KEY) ? arg : opt2->name;
	return strcasecmp(opt1->name, name) ? 0 : CMP_MATCH | CMP_STOP;
}

static int find_option_cb(void *obj, void *arg, int flags)
{
	struct aco_option *match = obj;
	const char *name = arg;

	switch (match->match_type) {
	case ACO_EXACT:
		return strcasecmp(name, match->name) ? 0 : CMP_MATCH | CMP_STOP;
	case ACO_REGEX:
		return regexec(match->name_regex, name, 0, NULL, 0) ? 0 : CMP_MATCH | CMP_STOP;
	}
	ast_log(LOG_ERROR, "Unknown match type. This should not be possible.\n");
	return CMP_STOP;
}

static struct aco_option *aco_option_find(struct aco_type *type, const char *name)
{
	struct aco_option *opt;

	if (!type || !type->internal || !type->internal->opts) {
		ast_log(LOG_NOTICE, "Attempting to use NULL or unitialized config type\n");
		return NULL;
	}

	/* Try an exact match with OBJ_KEY for the common/fast case, then iterate through
	 * all options for the regex cases */
	if (!(opt = ao2_callback(type->internal->opts, OBJ_KEY, find_option_cb, (void *) name))) {
		opt = ao2_callback(type->internal->opts, 0, find_option_cb, (void *) name);
	}
	return opt;
}

struct ao2_container *aco_option_container_alloc(void)
{
	return ao2_container_alloc(CONFIG_OPT_BUCKETS, config_opt_hash, config_opt_cmp);
}

static struct aco_type *internal_aco_type_find(struct aco_file *file, struct ast_config *cfg, const char *category)
{
	size_t x;
	struct aco_type *match;
	const char *val;

	for (x = 0, match = file->types[x]; match; match = file->types[++x]) {
		/* First make sure we are an object that can service this category */
		if (!regexec(match->internal->regex, category, 0, NULL, 0) == !match->category_match) {
			continue;
		}

		/* Then, see if we need to match a particular field */
		if (!ast_strlen_zero(match->matchfield) && (!ast_strlen_zero(match->matchvalue) || match->matchfunc)) {
			if (!(val = ast_variable_retrieve(cfg, category, match->matchfield))) {
				ast_log(LOG_ERROR, "Required match field '%s' not found\n", match->matchfield);
				return NULL;
			}
			if (match->matchfunc) {
				if (!match->matchfunc(val)) {
					continue;
				}
			} else if (strcasecmp(val, match->matchvalue)) {
				continue;
			}
		}
		/* If we get this far, we're a match */
		break;
	}

	return match;
}

static int is_preload(struct aco_file *file, const char *cat)
{
	int i;

	if (!file->preload) {
		return 0;
	}

	for (i = 0; !ast_strlen_zero(file->preload[i]); i++) {
		if (!strcasecmp(cat, file->preload[i])) {
			return 1;
		}
	}
	return 0;
}

static int process_category(struct ast_config *cfg, struct aco_info *info, struct aco_file *file, const char *cat, int preload) {
	RAII_VAR(void *, new_item, NULL, ao2_cleanup);
	struct aco_type *type;
	/* For global types, field is the global option struct. For non-global, it is the container for items.
	 * We do not grab a reference to these objects, as the info already holds references to them. This
	 * pointer is just a convenience. Do not actually store it somewhere. */
	void **field;

	/* Skip preloaded categories if we aren't preloading */
	if (!preload && is_preload(file, cat)) {
		return 0;
	}

	/* Find aco_type by category, if not found it is an error */
	if (!(type = internal_aco_type_find(file, cfg, cat))) {
		ast_log(LOG_ERROR, "Could not find config type for category '%s' in '%s'\n", cat, file->filename);
		return -1;
	}

	field = info->internal->pending + type->item_offset;
	if (!*field) {
		ast_log(LOG_ERROR, "No object to update!\n");
		return -1;
	}

	if (type->type == ACO_GLOBAL && *field) {
		if (aco_set_defaults(type, cat, *field)) {
			ast_log(LOG_ERROR, "In %s: Setting defaults for %s failed\n", file->filename, cat);
			return -1;
		}
		if (aco_process_category_options(type, cfg, cat, *field)) {
			ast_log(LOG_ERROR, "In %s: Processing options for %s failed\n", file->filename, cat);
			return -1;
		}
	} else if (type->type == ACO_ITEM) {
		int new = 0;
		/* If we have multiple definitions of a category in a file, or can set the values from multiple
		 * files, look up the entry if we've already added it so we can merge the values together.
		 * Otherwise, alloc a new item. */
		if (*field) {
			if (!(new_item = type->item_find(*field, cat))) {
				if (!(new_item = type->item_alloc(cat))) {
					ast_log(LOG_ERROR, "In %s: Could not create item for %s\n", file->filename, cat);
					return -1;
				}
				if (aco_set_defaults(type, cat, new_item)) {
					ast_log(LOG_ERROR, "In %s: Setting defaults for %s failed\n", file->filename, cat);
					return -1;
				}
				new = 1;
			}
		}

		if (type->item_pre_process && type->item_pre_process(new_item)) {
			ast_log(LOG_ERROR, "In %s: Preprocess callback for %s failed\n", file->filename, cat);
			return -1;
		}

		if (aco_process_category_options(type, cfg, cat, new_item)) {
			ast_log(LOG_ERROR, "In %s: Processing options for %s failed\n", file->filename, cat);
			return -1;
		}

		if (type->item_prelink && type->item_prelink(new_item)) {
			ast_log(LOG_ERROR, "In %s: Pre-link callback for %s failed\n", file->filename, cat);
			return -1;
		}

		if (new && !ao2_link(*field, new_item)) {
			ast_log(LOG_ERROR, "In %s: Linking config for %s failed\n", file->filename, cat);
			return -1;
		}
	}
	return 0;
}

static int apply_config(struct aco_info *info)
{
	ao2_global_obj_replace_unref(*info->global_obj, info->internal->pending);

	return 0;
}

static enum aco_process_status internal_process_ast_config(struct aco_info *info, struct aco_file *file, struct ast_config *cfg)
{
	const char *cat = NULL;

	if (file->preload) {
		int i;
		for (i = 0; !ast_strlen_zero(file->preload[i]); i++) {
			if (process_category(cfg, info, file, file->preload[i], 1)) {
				return ACO_PROCESS_ERROR;
			}
		}
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (process_category(cfg, info, file, cat, 0)) {
			return ACO_PROCESS_ERROR;
		}
	}
	return ACO_PROCESS_OK;
}

enum aco_process_status aco_process_ast_config(struct aco_info *info, struct aco_file *file, struct ast_config *cfg)
{
	if (!info->internal) {
		ast_log(LOG_ERROR, "Attempt to process %s with uninitialized aco_info\n", file->filename);
		goto error;
	}

	if (!(info->internal->pending = info->snapshot_alloc())) {
		ast_log(LOG_ERROR, "In %s: Could not allocate temporary objects\n", file->filename);
		goto error;
	}

	if (internal_process_ast_config(info, file, cfg)) {
		goto error;
	}

	if (info->pre_apply_config && info->pre_apply_config()) {
		goto error;
	}

	if (apply_config(info)) {
		goto error;
	};

	ao2_cleanup(info->internal->pending);
	return ACO_PROCESS_OK;

error:
	ao2_cleanup(info->internal->pending);
	return ACO_PROCESS_ERROR;
}

enum aco_process_status aco_process_config(struct aco_info *info, int reload)
{
	struct ast_config *cfg;
	struct ast_flags cfg_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0, };
	int res = ACO_PROCESS_OK, x = 0;
	struct aco_file *file;

	if (!(info->files[0])) {
		ast_log(LOG_ERROR, "No filename given, cannot proceed!\n");
		return ACO_PROCESS_ERROR;
	}

	if (!info->internal) {
		ast_log(LOG_ERROR, "Attempting to process uninitialized aco_info\n");
		return ACO_PROCESS_ERROR;
	}

	if (!(info->internal->pending = info->snapshot_alloc())) {
		ast_log(LOG_ERROR, "In %s: Could not allocate temporary objects\n", info->module);
		return ACO_PROCESS_ERROR;
	}

	while (res != ACO_PROCESS_ERROR && (file = info->files[x++])) {
		const char *filename = file->filename;
try_alias:
		if (!(cfg = ast_config_load(filename, cfg_flags))) {
			if (file->alias && strcmp(file->alias, filename)) {
				filename = file->alias;
				goto try_alias;
			}
			ast_log(LOG_ERROR, "Unable to load config file '%s'\n", file->filename);
			res = ACO_PROCESS_ERROR;
			break;
		} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
			ast_debug(1, "%s was unchanged\n", file->filename);
			res = ACO_PROCESS_UNCHANGED;
			continue;
		} else if (cfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "Contents of %s are invalid and cannot be parsed\n", file->filename);
			res = ACO_PROCESS_ERROR;
			break;
		} else if (cfg == CONFIG_STATUS_FILEMISSING) {
			if (file->alias && strcmp(file->alias, filename)) {
				filename = file->alias;
				goto try_alias;
			}
			ast_log(LOG_ERROR, "%s is missing! Cannot load %s\n", file->filename, info->module);
			res = ACO_PROCESS_ERROR;
			break;
		}

		res = internal_process_ast_config(info, file, cfg);
		ast_config_destroy(cfg);
	}

	if (res != ACO_PROCESS_OK) {
	   goto end;
	}

	if (info->pre_apply_config && (info->pre_apply_config()))  {
		res = ACO_PROCESS_ERROR;
		goto end;
	}

	if (apply_config(info)) {
		res = ACO_PROCESS_ERROR;
		goto end;
	}

	if (info->post_apply_config) {
		info->post_apply_config();
	}

end:
	ao2_cleanup(info->internal->pending);
	return res;
}
int aco_process_var(struct aco_type *type, const char *cat, struct ast_variable *var, void *obj)
{
	RAII_VAR(struct aco_option *, opt, aco_option_find(type, var->name), ao2_cleanup);
	if (opt && opt->deprecated && !ast_strlen_zero(opt->aliased_to)) {
		const char *alias = ast_strdupa(opt->aliased_to);
		ast_log(LOG_WARNING, "At line %d of %s option '%s' is deprecated. Use '%s' instead\n", var->lineno, var->file, var->name, alias);
		ao2_ref(opt, -1);
		opt = aco_option_find(type, alias);
	}

	if (!opt) {
		ast_log(LOG_ERROR, "Could not find option suitable for category '%s' named '%s' at line %d of %s\n", cat, var->name, var->lineno, var->file);
		return -1;
	}

	if (!opt->handler) {
		/* It should be impossible for an option to not have a handler */
		ast_log(LOG_ERROR, "BUG! Somehow a config option for %s/%s was created with no handler!\n", cat, var->name);
		return -1;
	}
	if (opt->handler(opt, var, obj)) {
		ast_log(LOG_ERROR, "Error parsing %s=%s at line %d of %s\n", var->name, var->value, var->lineno, var->file);
		return -1;
	}

	return 0;
}

int aco_process_category_options(struct aco_type *type, struct ast_config *cfg, const char *cat, void *obj)
{
	struct ast_variable *var;

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (aco_process_var(type, cat, var, obj)) {
			return -1;
		}
	}

	return 0;
}

static void internal_type_destroy(struct aco_type *type)
{
	/* If we've already had all our internal data cleared out,
	 * then there's no need to proceed further
	 */
	if (!type->internal) {
		return;
	}

	if (type->internal->regex) {
		regfree(type->internal->regex);
		ast_free(type->internal->regex);
	}
	ao2_cleanup(type->internal->opts);
	type->internal->opts = NULL;
	ast_free(type->internal);
	type->internal = NULL;
}

static void internal_file_types_destroy(struct aco_file *file)
{
	size_t x;
	struct aco_type *t;

	for (x = 0, t = file->types[x]; t; t = file->types[++x]) {
		internal_type_destroy(t);
		t = NULL;
	}
}

static int internal_type_init(struct aco_type *type)
{
	if (!(type->internal = ast_calloc(1, sizeof(*type->internal)))) {
		return -1;
	}

	if (!(type->internal->regex = build_regex(type->category))) {
		internal_type_destroy(type);
		return -1;
	}

	if (!(type->internal->opts = aco_option_container_alloc())) {
		internal_type_destroy(type);
		return -1;
	}

	return 0;
}

int aco_info_init(struct aco_info *info)
{
	size_t x, y;

	if (!(info->internal = ast_calloc(1, sizeof(*info->internal)))) {
		return -1;
	}

	for (x = 0; info->files[x]; x++) {
		for (y = 0; info->files[x]->types[y]; y++) {
			if (internal_type_init(info->files[x]->types[y])) {
				goto error;
			}
		}
	}

	return 0;
error:
	aco_info_destroy(info);
	return -1;
}

void aco_info_destroy(struct aco_info *info)
{
	int x;
	/* It shouldn't be possible for internal->pending to be in use when this is called because
	 * of the locks in loader.c around reloads and unloads and the fact that internal->pending
	 * only exists while those locks are held */
	ast_free(info->internal);
	info->internal = NULL;

	for (x = 0; info->files[x]; x++) {
		internal_file_types_destroy(info->files[x]);
	}
}

int aco_set_defaults(struct aco_type *type, const char *category, void *obj)
{
	struct aco_option *opt;
	struct ao2_iterator iter;

	iter = ao2_iterator_init(type->internal->opts, 0);

	while ((opt = ao2_iterator_next(&iter))) {
		RAII_VAR(struct ast_variable *, var, NULL, ast_variables_destroy);

		if (ast_strlen_zero(opt->default_val)) {
			ao2_ref(opt, -1);
			continue;
		}
		if (!(var = ast_variable_new(opt->name, opt->default_val, ""))) {
			ao2_ref(opt, -1);
			ao2_iterator_destroy(&iter);
			return -1;
		}
		if (opt->handler(opt, var, obj)) {
			ast_log(LOG_ERROR, "Unable to set default for %s, %s=%s\n", category, var->name, var->value);
			ao2_ref(opt, -1);
			ao2_iterator_destroy(&iter);
			return -1;
		}
		ao2_ref(opt, -1);
	}
	ao2_iterator_destroy(&iter);

	return 0;
}

/* Default config option handlers */

/*! \brief Default option handler for signed integers
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int int_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj) {
	int *field = (int *)(obj + opt->args[0]);
	unsigned int flags = PARSE_INT32 | opt->flags;
	int res = 0;
	if (opt->flags & PARSE_IN_RANGE) {
		res = opt->flags & PARSE_DEFAULT ?
			ast_parse_arg(var->value, flags, field, (int) opt->args[1], (int) opt->args[2], opt->args[3]) :
			ast_parse_arg(var->value, flags, field, (int) opt->args[1], (int) opt->args[2]);
		if (res) {
			if (opt->flags & PARSE_RANGE_DEFAULTS) {
				ast_log(LOG_WARNING, "Failed to set %s=%s. Set to  %d instead due to range limit (%d, %d)\n", var->name, var->value, *field, (int) opt->args[1], (int) opt->args[2]);
				res = 0;
			} else if (opt->flags & PARSE_DEFAULT) {
				ast_log(LOG_WARNING, "Failed to set %s=%s, Set to default value %d instead.\n", var->name, var->value, *field);
				res = 0;
			}
		}
	} else if ((opt->flags & PARSE_DEFAULT) && ast_parse_arg(var->value, flags, field, (int) opt->args[1])) {
		ast_log(LOG_WARNING, "Attempted to set %s=%s, but set it to %d instead due to default)\n", var->name, var->value, *field);
	} else {
		res = ast_parse_arg(var->value, flags, field);
	}

	return res;
}

/*! \brief Default option handler for unsigned integers
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int uint_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj) {
	unsigned int *field = (unsigned int *)(obj + opt->args[0]);
	unsigned int flags = PARSE_INT32 | opt->flags;
	int res = 0;
	if (opt->flags & PARSE_IN_RANGE) {
		res = opt->flags & PARSE_DEFAULT ?
			ast_parse_arg(var->value, flags, field, (unsigned int) opt->args[1], (unsigned int) opt->args[2], opt->args[3]) :
			ast_parse_arg(var->value, flags, field, (unsigned int) opt->args[1], (unsigned int) opt->args[2]);
		if (res) {
			if (opt->flags & PARSE_RANGE_DEFAULTS) {
				ast_log(LOG_WARNING, "Failed to set %s=%s. Set to  %d instead due to range limit (%d, %d)\n", var->name, var->value, *field, (int) opt->args[1], (int) opt->args[2]);
				res = 0;
			} else if (opt->flags & PARSE_DEFAULT) {
				ast_log(LOG_WARNING, "Failed to set %s=%s, Set to default value %d instead.\n", var->name, var->value, *field);
				res = 0;
			}
		}
	} else if ((opt->flags & PARSE_DEFAULT) && ast_parse_arg(var->value, flags, field, (unsigned int) opt->args[1])) {
		ast_log(LOG_WARNING, "Attempted to set %s=%s, but set it to %u instead due to default)\n", var->name, var->value, *field);
	} else {
		res = ast_parse_arg(var->value, flags, field);
	}

	return res;
}

/*! \brief Default option handler for doubles
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int double_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj) {
	double *field = (double *)(obj + opt->args[0]);
	return ast_parse_arg(var->value, PARSE_DOUBLE | opt->flags, field);
}

/*! \brief Default handler for ACLs
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int acl_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj) {
	struct ast_ha **ha = (struct ast_ha **)(obj + opt->args[0]);
	int error = 0;
	*ha = ast_append_ha(opt->flags ? "permit" : "deny", var->value, *ha, &error);
	return error;
}

/*! \brief Default option handler for codec preferences/capabilities
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int codec_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj) {
	struct ast_codec_pref *pref = (struct ast_codec_pref *)(obj + opt->args[0]);
	struct ast_format_cap **cap = (struct ast_format_cap **)(obj + opt->args[1]);
	return ast_parse_allow_disallow(pref, *cap, var->value, opt->flags);
}

/*! \brief Default option handler for stringfields
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int stringfield_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	ast_string_field *field = (const char **)(obj + opt->args[0]);
	struct ast_string_field_pool **pool = (struct ast_string_field_pool **)(obj + opt->args[1]);
	struct ast_string_field_mgr *mgr = (struct ast_string_field_mgr *)(obj + opt->args[2]);
	ast_string_field_ptr_set_by_fields(*pool, *mgr, field, var->value);
	return 0;
}

/*! \brief Default option handler for bools (ast_true/ast_false)
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int bool_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	unsigned int *field = (unsigned int *)(obj + opt->args[0]);
	*field = opt->flags ? ast_true(var->value) : ast_false(var->value);
	return 0;
}

/*! \brief Default option handler for bools (ast_true/ast_false) that are stored as flags
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int boolflag_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	unsigned int *flags_field = (unsigned int *)(obj + opt->args[0]);
	unsigned int val = opt->flags ? ast_true(var->value) : ast_false(var->value);
	unsigned int flag = opt->args[1];
	if (val) {
		*flags_field |= flag;
	} else {
		*flags_field &= ~flag;
	}
	return 0;
}

/*! \brief Default handler for ast_sockaddrs
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int sockaddr_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sockaddr *field = (struct ast_sockaddr *)(obj + opt->args[0]);
	return ast_parse_arg(var->value, PARSE_ADDR | opt->flags, field);
}

/*! \brief Default handler for doing noithing
 */
static int noop_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	return 0;
}

/*! \brief Default handler for character arrays
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int chararray_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	char *field = (char *)(obj + opt->args[0]);
	size_t len = opt->args[1];

	ast_copy_string(field, var->value, len);
	return 0;
}
