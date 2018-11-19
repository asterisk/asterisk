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

#include <regex.h>

#include "asterisk/_private.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/stringfields.h"
#include "asterisk/acl.h"
#include "asterisk/app.h"
#include "asterisk/frame.h"
#include "asterisk/xmldoc.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/format_cap.h"

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
	unsigned int no_doc:1;
#ifdef AST_DEVMODE
	unsigned int doc_unavailable:1;
#endif
	unsigned char deprecated:1;
	size_t argc;
	intptr_t args[0];
};

#ifdef AST_XML_DOCS
static struct ao2_container *xmldocs;

/*! \brief Value of the aco_option_type enum as strings */
static char *aco_option_type_string[] = {
	"ACL",				/* OPT_ACL_T, */
	"Boolean",			/* OPT_BOOL_T, */
	"Boolean",			/* OPT_BOOLFLAG_T, */
	"String",			/* OPT_CHAR_ARRAY_T, */
	"Codec",			/* OPT_CODEC_T, */
	"Custom",			/* OPT_CUSTOM_T, */
	"Double",			/* OPT_DOUBLE_T, */
	"Integer",			/* OPT_INT_T, */
	"None",				/* OPT_NOOP_T, */
	"IP Address",		/* OPT_SOCKADDR_T, */
	"String",			/* OPT_STRINGFIELD_T, */
	"Unsigned Integer",	/* OPT_UINT_T, */
	"Boolean",			/* OPT_YESNO_T, */
	"Time Length",		/* OPT_TIMELEN_T, */
};
#endif /* AST_XML_DOCS */

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
static int timelen_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int double_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int sockaddr_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int stringfield_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int bool_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int boolflag_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int acl_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int codec_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int noop_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);
static int chararray_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj);

#ifdef AST_XML_DOCS
static int xmldoc_update_config_type(const char *module, const char *name, const char *category, const char *matchfield, const char *matchvalue, enum aco_category_op category_match);
static int xmldoc_update_config_option(struct aco_type **types, const char *module, const char *name, const char *object_name, const char *default_value, unsigned int regex, enum aco_option_type type);
#endif

static aco_option_handler ast_config_option_default_handler(enum aco_option_type type)
{
	switch(type) {
	case OPT_ACL_T: return acl_handler_fn;
	case OPT_BOOL_T: return bool_handler_fn;
	/* Reading from config files, BOOL and YESNO are handled exactly the
	 * same. Their difference is in how they are rendered to users
	 */
	case OPT_YESNO_T: return bool_handler_fn;
	case OPT_BOOLFLAG_T: return boolflag_handler_fn;
	case OPT_CHAR_ARRAY_T: return chararray_handler_fn;
	case OPT_CODEC_T: return codec_handler_fn;
	case OPT_DOUBLE_T: return double_handler_fn;
	case OPT_INT_T: return int_handler_fn;
	case OPT_NOOP_T: return noop_handler_fn;
	case OPT_SOCKADDR_T: return sockaddr_handler_fn;
	case OPT_STRINGFIELD_T: return stringfield_handler_fn;
	case OPT_UINT_T: return uint_handler_fn;
	case OPT_TIMELEN_T: return timelen_handler_fn;

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

static int link_option_to_types(struct aco_info *info, struct aco_type **types, struct aco_option *opt)
{
	size_t idx = 0;
	struct aco_type *type;

	while ((type = types[idx++])) {
		if (!type->internal) {
			ast_log(LOG_ERROR, "Attempting to register option using uninitialized type\n");
			return -1;
		}
		if (!ao2_link(type->internal->opts, opt)) {
			do {
				ao2_unlink(types[idx - 1]->internal->opts, opt);
			} while (--idx);
			return -1;
		}
#ifdef AST_XML_DOCS
		if (!info->hidden && !opt->no_doc &&
			xmldoc_update_config_option(types, info->module, opt->name, type->name, opt->default_val, opt->match_type == ACO_REGEX, opt->type)) {
#ifdef AST_DEVMODE
			opt->doc_unavailable = 1;
#endif
		}
#endif
	}
	/* The container(s) should hold the only ref to opt */
	ao2_ref(opt, -1);

	return 0;
}

int aco_option_register_deprecated(struct aco_info *info, const char *name, struct aco_type **types, const char *aliased_to)
{
	struct aco_option *opt;

	if (!info || ast_strlen_zero(name) || ast_strlen_zero(aliased_to)) {
		return -1;
	}

	opt = ao2_alloc_options(sizeof(*opt), config_option_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!opt) {
		return -1;
	}

	opt->name = name;
	opt->aliased_to = aliased_to;
	opt->deprecated = 1;
	opt->match_type = ACO_EXACT;

	if (link_option_to_types(info, types, opt)) {
		ao2_ref(opt, -1);
		return -1;
	}

	return 0;
}

unsigned int aco_option_get_flags(const struct aco_option *option)
{
	return option->flags;
}

intptr_t aco_option_get_argument(const struct aco_option *option, unsigned int position)
{
	return option->args[position];
}

#ifdef AST_XML_DOCS
/*! \internal
 * \brief Find a particular ast_xml_doc_item from it's parent config_info, types, and name
 */
static struct ast_xml_doc_item *find_xmldoc_option(struct ast_xml_doc_item *config_info, struct aco_type **types, const char *name)
{
	struct ast_xml_doc_item *iter = config_info;

	if (!iter) {
		return NULL;
	}
	/* First is just the configInfo, we can skip it */
	while ((iter = AST_LIST_NEXT(iter, next))) {
		size_t x;
		if (strcasecmp(iter->name, name)) {
			continue;
		}
		for (x = 0; types[x]; x++) {
			/* All we care about is that at least one type has the option */
			if (!strcasecmp(types[x]->name, iter->ref)) {
				return iter;
			}
		}
	}
	return NULL;
}

/*! \internal
 * \brief Find a particular ast_xml_doc_item from it's parent config_info and name
 */
static struct ast_xml_doc_item *find_xmldoc_type(struct ast_xml_doc_item *config_info, const char *name)
{
	struct ast_xml_doc_item *iter = config_info;
	if (!iter) {
		return NULL;
	}
	/* First is just the config Info, skip it */
	while ((iter = AST_LIST_NEXT(iter, next))) {
		if (!strcasecmp(iter->type, "configObject") && !strcasecmp(iter->name, name)) {
			break;
		}
	}
	return iter;
}

#endif /* AST_XML_DOCS */

int __aco_option_register(struct aco_info *info, const char *name, enum aco_matchtype matchtype, struct aco_type **types,
	const char *default_val, enum aco_option_type kind, aco_option_handler handler, unsigned int flags,
	unsigned int no_doc, size_t argc, ...)
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

	opt = ao2_alloc_options(sizeof(*opt) + argc * sizeof(opt->args[0]),
		config_option_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!opt) {
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
	opt->no_doc = no_doc;

	if (!opt->handler && !(opt->handler = ast_config_option_default_handler(opt->type))) {
		/* This should never happen */
		ast_log(LOG_ERROR, "No handler provided, and no default handler exists for type %u\n", opt->type);
		ao2_ref(opt, -1);
		return -1;
	};

	if (link_option_to_types(info, types, opt)) {
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
	case ACO_PREFIX:
		return strncasecmp(name, match->name, strlen(match->name)) ? 0 : CMP_MATCH | CMP_STOP;
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
	return ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, CONFIG_OPT_BUCKETS,
		config_opt_hash, NULL, config_opt_cmp);
}

static int internal_aco_type_category_check(struct aco_type *match, const char *category)
{
	const char **categories = (const char **)match->category;

	switch (match->category_match) {
	case ACO_WHITELIST:
		return regexec(match->internal->regex, category, 0, NULL, 0);

	case ACO_BLACKLIST:
		return !regexec(match->internal->regex, category, 0, NULL, 0);

	case ACO_WHITELIST_EXACT:
		return strcasecmp(match->category, category);

	case ACO_BLACKLIST_EXACT:
		return !strcasecmp(match->category, category);

	case ACO_WHITELIST_ARRAY:
		while (*categories) {
			if (!strcasecmp(*categories, category)) {
				return 0;
			}
			categories++;
		}
		return -1;

	case ACO_BLACKLIST_ARRAY:
		while (*categories) {
			if (!strcasecmp(*categories, category)) {
				return -1;
			}
			categories++;
		}
		return 0;
	}

	return -1;
}

static struct aco_type *internal_aco_type_find(struct aco_file *file, struct ast_config *cfg, const char *category)
{
	size_t x;
	struct aco_type *match;
	const char *val;

	for (x = 0, match = file->types[x]; match; match = file->types[++x]) {
		/* First make sure we are an object that can service this category */
		if (internal_aco_type_category_check(match, category)) {
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
	regex_t *regex_skip;

	/* Skip preloaded categories if we aren't preloading */
	if (!preload && is_preload(file, cat)) {
		return 0;
	}

	/* Skip the category if we've been told to ignore it */
	if (!ast_strlen_zero(file->skip_category)) {
		regex_skip = build_regex(file->skip_category);
		if (!regexec(regex_skip, cat, 0, NULL, 0)) {
			regfree(regex_skip);
			ast_free(regex_skip);
			return 0;
		}
		regfree(regex_skip);
		ast_free(regex_skip);
	}

	/* Find aco_type by category, if not found it is an error */
	if (!(type = internal_aco_type_find(file, cfg, cat))) {
		ast_log(LOG_ERROR, "Could not find config type for category '%s' in '%s'\n", cat, file->filename);
		return -1;
	}

	if (type->type == ACO_IGNORE) {
		return 0;
	}

	field = info->internal->pending + type->item_offset;
	if (!*field) {
		ast_log(LOG_ERROR, "In %s: %s - No object to update!\n", file->filename, cat);
		return -1;
	}

	if (type->type == ACO_GLOBAL && *field) {
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
		return ACO_PROCESS_ERROR;
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
	info->internal->pending = NULL;
	return ACO_PROCESS_OK;

error:
	ao2_cleanup(info->internal->pending);
	info->internal->pending = NULL;

	return ACO_PROCESS_ERROR;
}

enum aco_process_status aco_process_config(struct aco_info *info, int reload)
{
	struct ast_config *cfg;
	struct ast_flags cfg_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0, };
	int res = ACO_PROCESS_OK;
	int file_count = 0;
	struct aco_file *file;

	if (!info->internal) {
		ast_log(LOG_ERROR, "Attempting to process uninitialized aco_info\n");
		return ACO_PROCESS_ERROR;
	}

	if (!(info->files[0])) {
		ast_log(LOG_ERROR, "No filename given, cannot proceed!\n");
		return ACO_PROCESS_ERROR;
	}

	if (!(info->internal->pending = info->snapshot_alloc())) {
		ast_log(LOG_ERROR, "In %s: Could not allocate temporary objects\n", info->module);
		return ACO_PROCESS_ERROR;
	}

	while (res != ACO_PROCESS_ERROR && (file = info->files[file_count++])) {
		const char *filename = file->filename;
		struct aco_type *match;
		int i;

		/* set defaults for global objects */
		for (i = 0, match = file->types[i]; match; match = file->types[++i]) {
			void **field = info->internal->pending + match->item_offset;

			if (match->type == ACO_IGNORE) {
				continue;
			}

			if (match->type != ACO_GLOBAL || !*field) {
				continue;
			}

			if (aco_set_defaults(match, match->category, *field)) {
				ast_log(LOG_ERROR, "In %s: Setting defaults for %s failed\n", file->filename, match->category);
				res = ACO_PROCESS_ERROR;
				break;
			}
		}

		if (res == ACO_PROCESS_ERROR) {
			break;
		}

try_alias:
		cfg = ast_config_load(filename, cfg_flags);
		if (!cfg || cfg == CONFIG_STATUS_FILEMISSING) {
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
			ast_log(LOG_ERROR, "Contents of %s are invalid and cannot be parsed\n",
				file->filename);
			res = ACO_PROCESS_ERROR;
			break;
		}

		/* A file got loaded. */
		if (reload) {
			/* Must do any subsequent file loads unconditionally. */
			reload = 0;
			ast_clear_flag(&cfg_flags, CONFIG_FLAG_FILEUNCHANGED);

			if (file_count != 1) {
				/*
				 * Must restart loading to load all config files since a file
				 * after the first one changed.
				 */
				file_count = 0;
			} else {
				res = internal_process_ast_config(info, file, cfg);
			}
		} else {
			res = internal_process_ast_config(info, file, cfg);
		}
		ast_config_destroy(cfg);
	}

	if (res != ACO_PROCESS_OK) {
		goto end;
	}

	if (info->pre_apply_config && info->pre_apply_config()) {
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
	info->internal->pending = NULL;

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

#ifdef AST_DEVMODE
	if (opt->doc_unavailable) {
		ast_log(LOG_ERROR, "Config option '%s' of type '%s' is not completely documented and can not be set\n", var->name, type->name);
		return -1;
	}
#endif

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

	switch (type->category_match) {
	case ACO_BLACKLIST:
	case ACO_WHITELIST:
		if (!(type->internal->regex = build_regex(type->category))) {
			internal_type_destroy(type);
			return -1;
		}
		break;
	case ACO_BLACKLIST_EXACT:
	case ACO_WHITELIST_EXACT:
	case ACO_BLACKLIST_ARRAY:
	case ACO_WHITELIST_ARRAY:
		break;
	}

	if (!(type->internal->opts = aco_option_container_alloc())) {
		internal_type_destroy(type);
		return -1;
	}

	return 0;
}

int aco_info_init(struct aco_info *info)
{
	size_t x = 0, y = 0;
	struct aco_file *file;
	struct aco_type *type;

	if (!(info->internal = ast_calloc(1, sizeof(*info->internal)))) {
		return -1;
	}

	while ((file = info->files[x++])) {
		while ((type = file->types[y++])) {
			if (internal_type_init(type)) {
				goto error;
			}
#ifdef AST_XML_DOCS
			if (!info->hidden &&
				!type->hidden &&
				type->type != ACO_IGNORE &&
				xmldoc_update_config_type(info->module, type->name, type->category, type->matchfield, type->matchvalue, type->category_match)) {
				goto error;
			}
#endif /* AST_XML_DOCS */
		}
		y = 0;
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

	if (!type->internal) {
		return -1;
	}

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

#ifdef AST_XML_DOCS

/*! \internal
 * \brief Complete the name of the module the user is looking for
 */
static char *complete_config_module(const char *word)
{
	size_t wordlen = strlen(word);
	struct ao2_iterator i;
	struct ast_xml_doc_item *cur;

	i = ao2_iterator_init(xmldocs, 0);
	while ((cur = ao2_iterator_next(&i))) {
		if (!strncasecmp(word, cur->name, wordlen)) {
			if (ast_cli_completion_add(ast_strdup(cur->name))) {
				ao2_ref(cur, -1);
				break;
			}
		}
		ao2_ref(cur, -1);
	}
	ao2_iterator_destroy(&i);

	return NULL;
}

/*! \internal
 * \brief Complete the name of the configuration type the user is looking for
 */
static char *complete_config_type(const char *module, const char *word)
{
	size_t wordlen = strlen(word);
	struct ast_xml_doc_item *info;
	struct ast_xml_doc_item *cur;

	info = ao2_find(xmldocs, module, OBJ_KEY);
	if (!info) {
		return NULL;
	}

	cur = info;
	while ((cur = AST_LIST_NEXT(cur, next))) {
		if (!strcasecmp(cur->type, "configObject") && !strncasecmp(word, cur->name, wordlen)) {
			if (ast_cli_completion_add(ast_strdup(cur->name))) {
				break;
			}
		}
	}
	ao2_ref(info, -1);

	return NULL;
}

/*! \internal
 * \brief Complete the name of the configuration option the user is looking for
 */
static char *complete_config_option(const char *module, const char *option, const char *word)
{
	size_t wordlen = strlen(word);
	struct ast_xml_doc_item *info;
	struct ast_xml_doc_item *cur;

	info = ao2_find(xmldocs, module, OBJ_KEY);
	if (!info) {
		return NULL;
	}

	cur = info;
	while ((cur = AST_LIST_NEXT(cur, next))) {
		if (!strcasecmp(cur->type, "configOption") && !strcasecmp(cur->ref, option) && !strncasecmp(word, cur->name, wordlen)) {
			if (ast_cli_completion_add(ast_strdup(cur->name))) {
				break;
			}
		}
	}
	ao2_ref(info, -1);

	return NULL;
}

/* Define as 0 if we want to allow configurations to be registered without
 * documentation
 */
#define XMLDOC_STRICT 1

/*! \internal
 * \brief Update the XML documentation for a config type based on its registration
 */
static int xmldoc_update_config_type(const char *module, const char *name, const char *category, const char *matchfield, const char *matchvalue, enum aco_category_op category_match)
{
	RAII_VAR(struct ast_xml_xpath_results *, results, NULL, ast_xml_xpath_results_free);
	RAII_VAR(struct ast_xml_doc_item *, config_info, ao2_find(xmldocs, module, OBJ_KEY), ao2_cleanup);
	struct ast_xml_doc_item *config_type;
	struct ast_xml_node *type, *syntax, *matchinfo, *tmp;

	/* If we already have a syntax element, bail. This isn't an error, since we may unload a module which
	 * has updated the docs and then load it again. */
	if ((results = ast_xmldoc_query("/docs/configInfo[@name='%s']/configFile/configObject[@name='%s']/syntax", module, name))) {
		return 0;
	}

	if (!(results = ast_xmldoc_query("/docs/configInfo[@name='%s']/configFile/configObject[@name='%s']", module, name))) {
		ast_log(LOG_WARNING, "Cannot update type '%s' in module '%s' because it has no existing documentation!\n", name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (!(type = ast_xml_xpath_get_first_result(results))) {
		ast_log(LOG_WARNING, "Could not retrieve documentation for type '%s' in module '%s'\n", name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (!(syntax = ast_xml_new_child(type, "syntax"))) {
		ast_log(LOG_WARNING, "Could not create syntax node for type '%s' in module '%s'\n", name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (!(matchinfo = ast_xml_new_child(syntax, "matchInfo"))) {
		ast_log(LOG_WARNING, "Could not create matchInfo node for type '%s' in module '%s'\n", name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (!(tmp = ast_xml_new_child(matchinfo, "category"))) {
		ast_log(LOG_WARNING, "Could not create category node for type '%s' in module '%s'\n", name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	ast_xml_set_text(tmp, category);
	switch (category_match) {
	case ACO_WHITELIST:
	case ACO_WHITELIST_EXACT:
	case ACO_WHITELIST_ARRAY:
		ast_xml_set_attribute(tmp, "match", "true");
		break;
	case ACO_BLACKLIST:
	case ACO_BLACKLIST_EXACT:
	case ACO_BLACKLIST_ARRAY:
		ast_xml_set_attribute(tmp, "match", "false");
		break;
	}

	if (!ast_strlen_zero(matchfield) && !(tmp = ast_xml_new_child(matchinfo, "field"))) {
		ast_log(LOG_WARNING, "Could not add %s attribute for type '%s' in module '%s'\n", matchfield, name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	ast_xml_set_attribute(tmp, "name", matchfield);
	ast_xml_set_text(tmp, matchvalue);

	if (!config_info || !(config_type = find_xmldoc_type(config_info, name))) {
		ast_log(LOG_WARNING, "Could not obtain XML documentation item for config type %s\n", name);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (ast_xmldoc_regenerate_doc_item(config_type)) {
		ast_log(LOG_WARNING, "Could not update type '%s' with values from config type registration\n", name);
		return XMLDOC_STRICT ? -1 : 0;
	}

	return 0;
}

/*! \internal
 * \brief Update the XML documentation for a config option based on its registration
 */
static int xmldoc_update_config_option(struct aco_type **types, const char *module, const char *name, const char *object_name, const char *default_value, unsigned int regex, enum aco_option_type type)
{
	RAII_VAR(struct ast_xml_xpath_results *, results, NULL, ast_xml_xpath_results_free);
	RAII_VAR(struct ast_xml_doc_item *, config_info, ao2_find(xmldocs, module, OBJ_KEY), ao2_cleanup);
	struct ast_xml_doc_item * config_option;
	struct ast_xml_node *option;

	ast_assert(ARRAY_LEN(aco_option_type_string) > type);

	if (!config_info || !(config_option = find_xmldoc_option(config_info, types, name))) {
		ast_log(LOG_ERROR, "XML Documentation for option '%s' in modules '%s' not found!\n", name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (!(results = ast_xmldoc_query("/docs/configInfo[@name='%s']/configFile/configObject[@name='%s']/configOption[@name='%s']", module, object_name, name))) {
		ast_log(LOG_WARNING, "Could not find option '%s' with type '%s' in module '%s'\n", name, object_name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}

	if (!(option = ast_xml_xpath_get_first_result(results))) {
		ast_log(LOG_WARNING, "Could not obtain results for option '%s' with type '%s' in module '%s'\n", name, object_name, module);
		return XMLDOC_STRICT ? -1 : 0;
	}
	ast_xml_set_attribute(option, "regex", regex ? "true" : "false");
	ast_xml_set_attribute(option, "default", default_value);
	ast_xml_set_attribute(option, "type", aco_option_type_string[type]);

	if (ast_xmldoc_regenerate_doc_item(config_option)) {
		ast_log(LOG_WARNING, "Could not update option '%s' with values from config option registration\n", name);
		return XMLDOC_STRICT ? -1 : 0;
	}

	return 0;
}

/*! \internal
 * \brief Show the modules with configuration information
 */
static void cli_show_modules(struct ast_cli_args *a)
{
	struct ast_xml_doc_item *item;
	struct ao2_iterator it_items;

	ast_assert(a->argc == 3);

	if (ao2_container_count(xmldocs) == 0) {
		ast_cli(a->fd, "No modules found.\n");
		return;
	}

	it_items = ao2_iterator_init(xmldocs, 0);
	ast_cli(a->fd, "The following modules have configuration information:\n");
	while ((item = ao2_iterator_next(&it_items))) {
		ast_cli(a->fd, "\t%s\n", item->name);
		ao2_ref(item, -1);
	}
	ao2_iterator_destroy(&it_items);
}

/*! \internal
 * \brief Show the configuration types for a module
 */
static void cli_show_module_types(struct ast_cli_args *a)
{
	RAII_VAR(struct ast_xml_doc_item *, item, NULL, ao2_cleanup);
	struct ast_xml_doc_item *tmp;

	ast_assert(a->argc == 4);

	if (!(item = ao2_find(xmldocs, a->argv[3], OBJ_KEY))) {
		ast_cli(a->fd, "Module %s not found.\n", a->argv[3]);
		return;
	}

	if (ast_str_strlen(item->synopsis)) {
		ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(ast_str_buffer(item->synopsis), 1));
	}
	if (ast_str_strlen(item->description)) {
		ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(ast_str_buffer(item->description), 1));
	}

	tmp = item;
	ast_cli(a->fd, "Configuration option types for %s:\n", tmp->name);
	while ((tmp = AST_LIST_NEXT(tmp, next))) {
		if (!strcasecmp(tmp->type, "configObject")) {
			ast_cli(a->fd, "%-25s -- %-65.65s\n", tmp->name,
				ast_str_buffer(tmp->synopsis));
		}
	}
}

/*! \internal
 * \brief Show the information for a configuration type
 */
static void cli_show_module_type(struct ast_cli_args *a)
{
	RAII_VAR(struct ast_xml_doc_item *, item, NULL, ao2_cleanup);
	struct ast_xml_doc_item *tmp;
	char option_type[64];
	int match = 0;

	ast_assert(a->argc == 5);

	if (!(item = ao2_find(xmldocs, a->argv[3], OBJ_KEY))) {
		ast_cli(a->fd, "Unknown module %s\n", a->argv[3]);
		return;
	}

	tmp = item;
	while ((tmp = AST_LIST_NEXT(tmp, next))) {
		if (!strcasecmp(tmp->type, "configObject") && !strcasecmp(tmp->name, a->argv[4])) {
			match = 1;
			term_color(option_type, tmp->name, COLOR_MAGENTA, COLOR_BLACK, sizeof(option_type));
			ast_cli(a->fd, "%s", option_type);
			if (ast_str_strlen(tmp->syntax)) {
				ast_cli(a->fd, ": [%s]\n\n", ast_xmldoc_printable(ast_str_buffer(tmp->syntax), 1));
			} else {
				ast_cli(a->fd, "\n\n");
			}
			if (ast_str_strlen(tmp->synopsis)) {
				ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(ast_str_buffer(tmp->synopsis), 1));
			}
			if (ast_str_strlen(tmp->description)) {
				ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(ast_str_buffer(tmp->description), 1));
			}
		}
	}

	if (!match) {
		ast_cli(a->fd, "Unknown configuration type %s\n", a->argv[4]);
		return;
	}

	/* Now iterate over the options for the type */
	tmp = item;
	while ((tmp = AST_LIST_NEXT(tmp, next))) {
		if (!strcasecmp(tmp->type, "configOption") && !strcasecmp(tmp->ref, a->argv[4])) {
			ast_cli(a->fd, "%-25s -- %-65.65s\n", tmp->name,
					ast_str_buffer(tmp->synopsis));
		}
	}
}

/*! \internal
 * \brief Show detailed information for an option
 */
static void cli_show_module_options(struct ast_cli_args *a)
{
	RAII_VAR(struct ast_xml_doc_item *, item, NULL, ao2_cleanup);
	struct ast_xml_doc_item *tmp;
	char option_name[64];
	int match = 0;

	ast_assert(a->argc == 6);

	if (!(item = ao2_find(xmldocs, a->argv[3], OBJ_KEY))) {
		ast_cli(a->fd, "Unknown module %s\n", a->argv[3]);
		return;
	}
	tmp = item;
	while ((tmp = AST_LIST_NEXT(tmp, next))) {
		if (!strcasecmp(tmp->type, "configOption") && !strcasecmp(tmp->ref, a->argv[4]) && !strcasecmp(tmp->name, a->argv[5])) {
			if (match) {
				ast_cli(a->fd, "\n");
			}
			term_color(option_name, tmp->ref, COLOR_MAGENTA, COLOR_BLACK, sizeof(option_name));
			ast_cli(a->fd, "[%s%s]\n", option_name, ast_term_reset());
			if (ast_str_strlen(tmp->syntax)) {
				ast_cli(a->fd, "%s\n", ast_xmldoc_printable(ast_str_buffer(tmp->syntax), 1));
			}
			ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(AS_OR(tmp->synopsis, "No information available"), 1));
			if (ast_str_strlen(tmp->description)) {
				ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(ast_str_buffer(tmp->description), 1));
			}

			if (ast_str_strlen(tmp->seealso)) {
				ast_cli(a->fd, "See Also:\n");
				ast_cli(a->fd, "%s\n\n", ast_xmldoc_printable(ast_str_buffer(tmp->seealso), 1));
			}

			match = 1;
		}
	}

	if (!match) {
		ast_cli(a->fd, "No option %s found for %s:%s\n", a->argv[5], a->argv[3], a->argv[4]);
	}
}

static char *cli_show_help(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "config show help";
		e->usage =
			"Usage: config show help [<module> [<type> [<option>]]]\n"
			"   Display detailed information about module configuration.\n"
			"     * If nothing is specified, the modules that have\n"
			"       configuration information are listed.\n"
			"     * If <module> is specified, the configuration types\n"
			"       for that module will be listed, along with brief\n"
			"       information about that type.\n"
			"     * If <module> and <type> are specified, detailed\n"
			"       information about the type is displayed, as well\n"
			"       as the available options.\n"
			"     * If <module>, <type>, and <option> are specified,\n"
			"       detailed information will be displayed about that\n"
			"       option.\n"
			"   NOTE: the help documentation is partially generated at run\n"
			"     time when a module is loaded. If a module is not loaded,\n"
			"     configuration help for that module may be incomplete.\n";
		return NULL;
	case CLI_GENERATE:
		switch(a->pos) {
		case 3:
			return complete_config_module(a->word);
		case 4:
			return complete_config_type(a->argv[3], a->word);
		case 5:
			return complete_config_option(a->argv[3], a->argv[4], a->word);
		default:
			return NULL;
		}
	}

	switch (a->argc) {
	case 3:
		cli_show_modules(a);
		break;
	case 4:
		cli_show_module_types(a);
		break;
	case 5:
		cli_show_module_type(a);
		break;
	case 6:
		cli_show_module_options(a);
		break;
	default:
		return CLI_SHOWUSAGE;
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_aco[] = {
	AST_CLI_DEFINE(cli_show_help, "Show configuration help for a module"),
};

static void aco_deinit(void)
{
	ast_cli_unregister(cli_aco);
	ao2_cleanup(xmldocs);
}
#endif /* AST_XML_DOCS */

int aco_init(void)
{
#ifdef AST_XML_DOCS
	ast_register_cleanup(aco_deinit);
	if (!(xmldocs = ast_xmldoc_build_documentation("configInfo"))) {
		ast_log(LOG_ERROR, "Couldn't build config documentation\n");
		return -1;
	}
	ast_cli_register_multiple(cli_aco, ARRAY_LEN(cli_aco));
#endif /* AST_XML_DOCS */
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
				ast_log(LOG_WARNING, "Failed to set %s=%s. Set to %d instead due to range limit (%d, %d)\n", var->name, var->value, *field, (int) opt->args[1], (int) opt->args[2]);
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
	unsigned int flags = PARSE_UINT32 | opt->flags;
	int res = 0;
	if (opt->flags & PARSE_IN_RANGE) {
		res = opt->flags & PARSE_DEFAULT ?
			ast_parse_arg(var->value, flags, field, (unsigned int) opt->args[1], (unsigned int) opt->args[2], opt->args[3]) :
			ast_parse_arg(var->value, flags, field, (unsigned int) opt->args[1], (unsigned int) opt->args[2]);
		if (res) {
			if (opt->flags & PARSE_RANGE_DEFAULTS) {
				ast_log(LOG_WARNING, "Failed to set %s=%s. Set to %u instead due to range limit (%d, %d)\n", var->name, var->value, *field, (int) opt->args[1], (int) opt->args[2]);
				res = 0;
			} else if (opt->flags & PARSE_DEFAULT) {
				ast_log(LOG_WARNING, "Failed to set %s=%s, Set to default value %u instead.\n", var->name, var->value, *field);
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

/*! \brief Default option handler for timelen signed integers
 * \note For a description of the opt->flags and opt->args values, see the documentation for
 * enum aco_option_type in config_options.h
 */
static int timelen_handler_fn(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	int *field = (int *)(obj + opt->args[0]);
	unsigned int flags = PARSE_TIMELEN | opt->flags;
	int res = 0;
	if (opt->flags & PARSE_IN_RANGE) {
		if (opt->flags & PARSE_DEFAULT) {
			res = ast_parse_arg(var->value, flags, field, (enum ast_timelen) opt->args[1], (int) opt->args[2], (int) opt->args[3], opt->args[4]);
		} else {
			res = ast_parse_arg(var->value, flags, field, (enum ast_timelen) opt->args[1], (int) opt->args[2], (int) opt->args[3]);
		}
		if (res) {
			if (opt->flags & PARSE_RANGE_DEFAULTS) {
				ast_log(LOG_WARNING, "Failed to set %s=%s. Set to %d instead due to range limit (%d, %d)\n", var->name, var->value, *field, (int) opt->args[2], (int) opt->args[3]);
				res = 0;
			} else if (opt->flags & PARSE_DEFAULT) {
				ast_log(LOG_WARNING, "Failed to set %s=%s, Set to default value %d instead.\n", var->name, var->value, *field);
				res = 0;
			}
		}
	} else if ((opt->flags & PARSE_DEFAULT) && ast_parse_arg(var->value, flags, field, (enum ast_timelen) opt->args[1], (int) opt->args[2])) {
		ast_log(LOG_WARNING, "Attempted to set %s=%s, but set it to %d instead due to default)\n", var->name, var->value, *field);
	} else {
		res = ast_parse_arg(var->value, flags, field, (enum ast_timelen) opt->args[1]);
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
	struct ast_format_cap **cap = (struct ast_format_cap **)(obj + opt->args[0]);
	return ast_format_cap_update_by_allow_disallow(*cap, var->value, opt->flags);
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

	if (opt->flags && ast_strlen_zero(var->value)) {
		return -1;
	}
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

/*! \brief Default handler for doing nothing
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

	if (opt->flags && ast_strlen_zero(var->value)) {
		return -1;
	}
	ast_copy_string(field, var->value, len);
	return 0;
}
