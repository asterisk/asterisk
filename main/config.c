/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
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
 *
 * \brief Configuration File Parser
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * Includes the Asterisk Realtime API - ARA
 * See http://wiki.asterisk.org
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/* This maintains the original "module reload extconfig" CLI command instead
 * of replacing it with "module reload config". */
#undef AST_MODULE
#define AST_MODULE "extconfig"

#include "asterisk.h"

#include "asterisk/paths.h"	/* use ast_config_AST_CONFIG_DIR */
#include "asterisk/network.h"	/* we do some sockaddr manipulation here */

#include <string.h>
#include <libgen.h>
#include <time.h>
#include <sys/stat.h>

#include <math.h>	/* HUGE_VAL */
#include <regex.h>

#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"	/* for the ast_str_*() API */
#include "asterisk/netsock2.h"
#include "asterisk/module.h"

#define MAX_NESTED_COMMENTS 128
#define COMMENT_START ";--"
#define COMMENT_END "--;"
#define COMMENT_META ';'
#define COMMENT_TAG '-'

/*!
 * Define the minimum filename space to reserve for each
 * ast_variable in case the filename is renamed later by
 * ast_include_rename().
 */
#define MIN_VARIABLE_FNAME_SPACE	40

static char *extconfig_conf = "extconfig.conf";

static struct ao2_container *cfg_hooks;
static void config_hook_exec(const char *filename, const char *module, const struct ast_config *cfg);
static inline struct ast_variable *variable_list_switch(struct ast_variable *l1, struct ast_variable *l2);
static int does_category_match(struct ast_category *cat, const char *category_name,
	const char *match, char sep);

/*! \brief Structure to keep comments for rewriting configuration files */
struct ast_comment {
	struct ast_comment *next;
	/*! Comment body allocated after struct. */
	char cmt[0];
};

/*! \brief Hold the mtime for config files, so if we don't need to reread our config, don't. */
struct cache_file_include {
	AST_LIST_ENTRY(cache_file_include) list;
	/*! Filename or wildcard pattern as specified by the including file. */
	char include[0];
};

struct cache_file_mtime {
	AST_LIST_ENTRY(cache_file_mtime) list;
	AST_LIST_HEAD_NOLOCK(includes, cache_file_include) includes;
	unsigned int has_exec:1;
	/*! stat() file size */
	unsigned long stat_size;
	/*! stat() file modtime nanoseconds */
	unsigned long stat_mtime_nsec;
	/*! stat() file modtime seconds since epoc */
	time_t stat_mtime;

	/*! String stuffed in filename[] after the filename string. */
	const char *who_asked;
	/*! Filename and who_asked stuffed after it. */
	char filename[0];
};

/*! Cached file mtime list. */
static AST_LIST_HEAD_STATIC(cfmtime_head, cache_file_mtime);

static int init_appendbuf(void *data)
{
	struct ast_str **str = data;
	*str = ast_str_create(16);
	return *str ? 0 : -1;
}

AST_THREADSTORAGE_CUSTOM(appendbuf, init_appendbuf, ast_free_ptr);

/* comment buffers are better implemented using the ast_str_*() API */
#define CB_SIZE 250	/* initial size of comment buffers */

static void  CB_ADD(struct ast_str **cb, const char *str)
{
	ast_str_append(cb, 0, "%s", str);
}

static void  CB_ADD_LEN(struct ast_str **cb, const char *str, int len)
{
	char *s = ast_alloca(len + 1);

	memcpy(s, str, len);
	s[len] = '\0';
	ast_str_append(cb, 0, "%s", s);
}

static void CB_RESET(struct ast_str *cb, struct ast_str *llb)
{
	if (cb) {
		ast_str_reset(cb);
	}
	if (llb) {
		ast_str_reset(llb);
	}
}

static struct ast_comment *ALLOC_COMMENT(struct ast_str *buffer)
{
	struct ast_comment *x = NULL;
	if (!buffer || !ast_str_strlen(buffer)) {
		return NULL;
	}
	if ((x = ast_calloc(1, sizeof(*x) + ast_str_strlen(buffer) + 1))) {
		strcpy(x->cmt, ast_str_buffer(buffer)); /* SAFE */
	}
	return x;
}

/* I need to keep track of each config file, and all its inclusions,
   so that we can track blank lines in each */

struct inclfile {
	char *fname;
	int lineno;
};

static int hash_string(const void *obj, const int flags)
{
	char *str = ((struct inclfile *) obj)->fname;
	int total;

	for (total = 0; *str; str++) {
		unsigned int tmp = total;
		total <<= 1; /* multiply by 2 */
		total += tmp; /* multiply by 3 */
		total <<= 2; /* multiply by 12 */
		total += tmp; /* multiply by 13 */

		total += ((unsigned int) (*str));
	}
	if (total < 0) {
		total = -total;
	}
	return total;
}

static int hashtab_compare_strings(void *a, void *b, int flags)
{
	const struct inclfile *ae = a, *be = b;
	return !strcmp(ae->fname, be->fname) ? CMP_MATCH | CMP_STOP : 0;
}

static struct ast_config_map {
	struct ast_config_map *next;
	int priority;
	/*! Stored in stuff[] at struct end. */
	const char *name;
	/*! Stored in stuff[] at struct end. */
	const char *driver;
	/*! Stored in stuff[] at struct end. */
	const char *database;
	/*! Stored in stuff[] at struct end. */
	const char *table;
	/*! Contents of name, driver, database, and table in that order stuffed here. */
	char stuff[0];
} *config_maps = NULL;

AST_MUTEX_DEFINE_STATIC(config_lock);
static struct ast_config_engine *config_engine_list;

#define MAX_INCLUDE_LEVEL 10

struct ast_category_template_instance {
	char name[80]; /* redundant? */
	const struct ast_category *inst;
	AST_LIST_ENTRY(ast_category_template_instance) next;
};

struct ast_category {
	char name[80];
	int ignored;			/*!< do not let user of the config see this category -- set by (!) after the category decl; a template */
	int include_level;
	/*!
	 * \brief The file name from whence this declaration was read
	 * \note Will never be NULL
	 */
	char *file;
	int lineno;
	AST_LIST_HEAD_NOLOCK(template_instance_list, ast_category_template_instance) template_instances;
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_comment *trailing; /*!< the last object in the list will get assigned any trailing comments when EOF is hit */
	/*! First category variable in the list. */
	struct ast_variable *root;
	/*! Last category variable in the list. */
	struct ast_variable *last;
	/*! Previous node in the list. */
	struct ast_category *prev;
	/*! Next node in the list. */
	struct ast_category *next;
};

struct ast_config {
	/*! First config category in the list. */
	struct ast_category *root;
	/*! Last config category in the list. */
	struct ast_category *last;
	struct ast_category *current;
	struct ast_category *last_browse;     /*!< used to cache the last category supplied via category_browse */
	int include_level;
	int max_include_level;
	struct ast_config_include *includes;  /*!< a list of inclusions, which should describe the entire tree */
};

struct ast_config_include {
	/*!
	 * \brief file name in which the include occurs
	 * \note Will never be NULL
	 */
	char *include_location_file;
	int  include_location_lineno;    /*!< lineno where include occurred */
	int  exec;                       /*!< set to non-zero if its a #exec statement */
	/*!
	 * \brief if it's an exec, you'll have both the /var/tmp to read, and the original script
	 * \note Will never be NULL if exec is non-zero
	 */
	char *exec_file;
	/*!
	 * \brief file name included
	 * \note Will never be NULL
	 */
	char *included_file;
	int inclusion_count;             /*!< if the file is included more than once, a running count thereof -- but, worry not,
	                                      we explode the instances and will include those-- so all entries will be unique */
	int output;                      /*!< a flag to indicate if the inclusion has been output */
	struct ast_config_include *next; /*!< ptr to next inclusion in the list */
};

static void ast_variable_destroy(struct ast_variable *doomed);
static void ast_includes_destroy(struct ast_config_include *incls);

struct ast_variable *_ast_variable_new(const char *name, const char *value, const char *filename, const char *file, const char *func, int lineno)
{
	struct ast_variable *variable;
	int name_len = strlen(name) + 1;
	int val_len = strlen(value) + 1;
	int fn_len = strlen(filename) + 1;

	/* Ensure a minimum length in case the filename is changed later. */
	if (fn_len < MIN_VARIABLE_FNAME_SPACE) {
		fn_len = MIN_VARIABLE_FNAME_SPACE;
	}

	variable = __ast_calloc(1, fn_len + name_len + val_len + sizeof(*variable),
		file, lineno, func);
	if (variable) {
		char *dst = variable->stuff;	/* writable space starts here */

		/* Put file first so ast_include_rename() can calculate space available. */
		variable->file = strcpy(dst, filename);
		dst += fn_len;
		variable->name = strcpy(dst, name);
		dst += name_len;
		variable->value = strcpy(dst, value);
	}
	return variable;
}

/*!
 * \internal
 * \brief Move the contents from the source to the destination variable.
 *
 * \param dst_var Destination variable node
 * \param src_var Source variable node
 */
static void ast_variable_move(struct ast_variable *dst_var, struct ast_variable *src_var)
{
	dst_var->lineno = src_var->lineno;
	dst_var->object = src_var->object;
	dst_var->blanklines = src_var->blanklines;
	dst_var->precomments = src_var->precomments;
	src_var->precomments = NULL;
	dst_var->sameline = src_var->sameline;
	src_var->sameline = NULL;
	dst_var->trailing = src_var->trailing;
	src_var->trailing = NULL;
}

struct ast_config_include *ast_include_new(struct ast_config *conf, const char *from_file, const char *included_file, int is_exec, const char *exec_file, int from_lineno, char *real_included_file_name, int real_included_file_name_size)
{
	/* a file should be included ONCE. Otherwise, if one of the instances is changed,
	 * then all be changed. -- how do we know to include it? -- Handling modified
	 * instances is possible, I'd have
	 * to create a new master for each instance. */
	struct ast_config_include *inc;
	struct stat statbuf;

	inc = ast_include_find(conf, included_file);
	if (inc) {
		do {
			inc->inclusion_count++;
			snprintf(real_included_file_name, real_included_file_name_size, "%s~~%d", included_file, inc->inclusion_count);
		} while (stat(real_included_file_name, &statbuf) == 0);
		ast_log(LOG_WARNING,"'%s', line %d:  Same File included more than once! This data will be saved in %s if saved back to disk.\n", from_file, from_lineno, real_included_file_name);
	} else
		*real_included_file_name = 0;

	inc = ast_calloc(1,sizeof(struct ast_config_include));
	if (!inc) {
		return NULL;
	}
	inc->include_location_file = ast_strdup(from_file);
	inc->include_location_lineno = from_lineno;
	if (!ast_strlen_zero(real_included_file_name))
		inc->included_file = ast_strdup(real_included_file_name);
	else
		inc->included_file = ast_strdup(included_file);

	inc->exec = is_exec;
	if (is_exec)
		inc->exec_file = ast_strdup(exec_file);

	if (!inc->include_location_file
		|| !inc->included_file
		|| (is_exec && !inc->exec_file)) {
		ast_includes_destroy(inc);
		return NULL;
	}

	/* attach this new struct to the conf struct */
	inc->next = conf->includes;
	conf->includes = inc;

	return inc;
}

void ast_include_rename(struct ast_config *conf, const char *from_file, const char *to_file)
{
	struct ast_config_include *incl;
	struct ast_category *cat;
	char *str;

	int from_len = strlen(from_file);
	int to_len = strlen(to_file);

	if (strcmp(from_file, to_file) == 0) /* no use wasting time if the name is the same */
		return;

	/* the manager code allows you to read in one config file, then
	 * write it back out under a different name. But, the new arrangement
	 * ties output lines to the file name. So, before you try to write
	 * the config file to disk, better riffle thru the data and make sure
	 * the file names are changed.
	 */
	/* file names are on categories, includes (of course), and on variables. So,
	 * traverse all this and swap names */

	for (incl = conf->includes; incl; incl=incl->next) {
		if (strcmp(incl->include_location_file,from_file) == 0) {
			if (from_len >= to_len)
				strcpy(incl->include_location_file, to_file);
			else {
				/* Keep the old filename if the allocation fails. */
				str = ast_strdup(to_file);
				if (str) {
					ast_free(incl->include_location_file);
					incl->include_location_file = str;
				}
			}
		}
	}
	for (cat = conf->root; cat; cat = cat->next) {
		struct ast_variable **prev;
		struct ast_variable *v;
		struct ast_variable *new_var;

		if (strcmp(cat->file,from_file) == 0) {
			if (from_len >= to_len)
				strcpy(cat->file, to_file);
			else {
				/* Keep the old filename if the allocation fails. */
				str = ast_strdup(to_file);
				if (str) {
					ast_free(cat->file);
					cat->file = str;
				}
			}
		}
		for (prev = &cat->root, v = cat->root; v; prev = &v->next, v = v->next) {
			if (strcmp(v->file, from_file)) {
				continue;
			}

			/*
			 * Calculate actual space available.  The file string is
			 * intentionally stuffed before the name string just so we can
			 * do this.
			 */
			if (to_len < v->name - v->file) {
				/* The new name will fit in the available space. */
				str = (char *) v->file;/* Stupid compiler complains about discarding qualifiers even though I used a cast. */
				strcpy(str, to_file);/* SAFE */
				continue;
			}

			/* Keep the old filename if the allocation fails. */
			new_var = ast_variable_new(v->name, v->value, to_file);
			if (!new_var) {
				continue;
			}

			/* Move items from the old list node to the replacement node. */
			ast_variable_move(new_var, v);

			/* Replace the old node in the list with the new node. */
			new_var->next = v->next;
			if (cat->last == v) {
				cat->last = new_var;
			}
			*prev = new_var;

			ast_variable_destroy(v);

			v = new_var;
		}
	}
}

struct ast_config_include *ast_include_find(struct ast_config *conf, const char *included_file)
{
	struct ast_config_include *x;
	for (x=conf->includes;x;x=x->next) {
		if (strcmp(x->included_file,included_file) == 0)
			return x;
	}
	return 0;
}


void ast_variable_append(struct ast_category *category, struct ast_variable *variable)
{
	if (!variable)
		return;
	if (category->last)
		category->last->next = variable;
	else
		category->root = variable;
	category->last = variable;
	while (category->last->next)
		category->last = category->last->next;
}

void ast_variable_insert(struct ast_category *category, struct ast_variable *variable, const char *line)
{
	struct ast_variable *cur = category->root;
	int lineno;
	int insertline;

	if (!variable || sscanf(line, "%30d", &insertline) != 1) {
		return;
	}
	if (!insertline) {
		variable->next = category->root;
		category->root = variable;
	} else {
		for (lineno = 1; lineno < insertline; lineno++) {
			cur = cur->next;
			if (!cur->next) {
				break;
			}
		}
		variable->next = cur->next;
		cur->next = variable;
	}
}

static void ast_comment_destroy(struct ast_comment **comment)
{
	struct ast_comment *n, *p;

	for (p = *comment; p; p = n) {
		n = p->next;
		ast_free(p);
	}

	*comment = NULL;
}

static void ast_variable_destroy(struct ast_variable *doomed)
{
	ast_comment_destroy(&doomed->precomments);
	ast_comment_destroy(&doomed->sameline);
	ast_comment_destroy(&doomed->trailing);
	ast_free(doomed);
}

struct ast_variable *ast_variables_dup(struct ast_variable *var)
{
	struct ast_variable *cloned;
	struct ast_variable *tmp;

	if (!(cloned = ast_variable_new(var->name, var->value, var->file))) {
		return NULL;
	}

	tmp = cloned;

	while ((var = var->next)) {
		if (!(tmp->next = ast_variable_new(var->name, var->value, var->file))) {
			ast_variables_destroy(cloned);
			return NULL;
		}
		tmp = tmp->next;
	}

	return cloned;
}

struct ast_variable *ast_variables_reverse(struct ast_variable *var)
{
	struct ast_variable *var1, *var2;

	var1 = var;

	if (!var1 || !var1->next) {
		return var1;
	}

	var2 = var1->next;
	var1->next = NULL;

	while (var2) {
		struct ast_variable *next = var2->next;

		var2->next = var1;
		var1 = var2;
		var2 = next;
	}

	return var1;
}

void ast_variables_destroy(struct ast_variable *var)
{
	struct ast_variable *vn;

	while (var) {
		vn = var;
		var = var->next;
		ast_variable_destroy(vn);
	}
}

struct ast_variable *ast_variable_browse(const struct ast_config *config, const char *category)
{
	struct ast_category *cat;

	if (config->last_browse && (config->last_browse->name == category)) {
		cat = config->last_browse;
	} else {
		cat = ast_category_get(config, category, NULL);
	}

	return (cat) ? cat->root : NULL;
}

static inline struct ast_variable *variable_list_switch(struct ast_variable *l1, struct ast_variable *l2)
{
    l1->next = l2->next;
    l2->next = l1;
    return l2;
}

struct ast_variable *ast_variable_list_sort(struct ast_variable *start)
{
	struct ast_variable *p, *q;
	struct ast_variable top;
	int changed = 1;
	memset(&top, 0, sizeof(top));
	top.next = start;
	if (start != NULL && start->next != NULL) {
		while (changed) {
			changed = 0;
			q = &top;
			p = top.next;
			while (p->next != NULL) {
				if (p->next != NULL && strcmp(p->name, p->next->name) > 0) {
					q->next = variable_list_switch(p, p->next);
					changed = 1;
				}
				q = p;
				if (p->next != NULL)
					p = p->next;
			}
		}
	}
	return top.next;
}

struct ast_variable *ast_variable_list_append_hint(struct ast_variable **head, struct ast_variable *search_hint, struct ast_variable *newvar)
{
	struct ast_variable *curr;
	struct ast_variable *sh = search_hint;
	ast_assert(head != NULL);

	if (!*head) {
		*head = newvar;
	} else {
		if (sh == NULL) {
			sh = *head;
		}
		for (curr = sh; curr->next; curr = curr->next);
		curr->next = newvar;
	}

	for (curr = newvar; curr->next; curr = curr->next);

	return curr;
}

int ast_variable_list_replace(struct ast_variable **head, struct ast_variable *replacement)
{
	struct ast_variable *v, **prev = head;

	for (v = *head; v; prev = &v->next, v = v->next) {
		if (!strcmp(v->name, replacement->name)) {
			replacement->next = v->next;
			*prev = replacement;
			ast_free(v);
			return 0;
		}
	}

	return -1;
}

int ast_variable_list_replace_variable(struct ast_variable **head, struct ast_variable *old,
	struct ast_variable *new)
{
	struct ast_variable *v, **prev = head;

	for (v = *head; v; prev = &v->next, v = v->next) {
		if (v == old) {
			new->next = v->next;
			*prev = new;
			ast_free(v);
			return 0;
		}
	}

	return -1;
}

struct ast_str *ast_variable_list_join(const struct ast_variable *head, const char *item_separator,
	const char *name_value_separator, const char *quote_char, struct ast_str **str)
{
	struct ast_variable *var = (struct ast_variable *)head;
	struct ast_str *local_str = NULL;

	if (str == NULL || *str == NULL) {
		local_str = ast_str_create(AST_MAX_USER_FIELD);
		if (!local_str) {
			return NULL;
		}
	} else {
		local_str = *str;
	}

	for (; var; var = var->next) {
		ast_str_append(&local_str, 0, "%s%s%s%s%s%s", var->name, name_value_separator, S_OR(quote_char, ""),
			var->value, S_OR(quote_char, ""), var->next ? item_separator : "");
	}

	if (str != NULL) {
		*str = local_str;
	}
	return local_str;
}

struct ast_variable *ast_variable_list_from_quoted_string(const char *input, const char *item_separator,
	const char *name_value_separator, const char *quote_str)
{
	char item_sep;
	char nv_sep;
	char quote;
	struct ast_variable *new_list = NULL;
	struct ast_variable *new_var = NULL;
	char *item_string;
	char *item;
	char *item_name;
	char *item_value;

	if (ast_strlen_zero(input)) {
		return NULL;
	}

	item_sep = ast_strlen_zero(item_separator) ? ',' : item_separator[0];
	nv_sep = ast_strlen_zero(name_value_separator) ? '=' : name_value_separator[0];
	quote = ast_strlen_zero(quote_str) ? '"' : quote_str[0];
	item_string = ast_strip(ast_strdupa(input));

	while ((item = ast_strsep_quoted(&item_string, item_sep, quote, AST_STRSEP_ALL))) {
		item_name = ast_strsep_quoted(&item, nv_sep, quote, AST_STRSEP_ALL);
		if (!item_name) {
			ast_variables_destroy(new_list);
			return NULL;
		}

		item_value = ast_strsep_quoted(&item, nv_sep, quote, AST_STRSEP_ALL);

		new_var = ast_variable_new(item_name, item_value ?: "", "");
		if (!new_var) {
			ast_variables_destroy(new_list);
			return NULL;
		}
		ast_variable_list_append(&new_list, new_var);
	}
	return new_list;
}

struct ast_variable *ast_variable_list_from_string(const char *input, const char *item_separator,
	const char *name_value_separator)
{
	return ast_variable_list_from_quoted_string(input, item_separator, name_value_separator, NULL);
}

const char *ast_config_option(struct ast_config *cfg, const char *cat, const char *var)
{
	const char *tmp;
	tmp = ast_variable_retrieve(cfg, cat, var);
	if (!tmp) {
		tmp = ast_variable_retrieve(cfg, "general", var);
	}
	return tmp;
}

const char *ast_variable_retrieve(struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category) {
		for (v = ast_variable_browse(config, category); v; v = v->next) {
			if (!strcasecmp(variable, v->name)) {
				return v->value;
			}
		}
	} else {
		struct ast_category *cat;

		for (cat = config->root; cat; cat = cat->next) {
			for (v = cat->root; v; v = v->next) {
				if (!strcasecmp(variable, v->name)) {
					return v->value;
				}
			}
		}
	}

	return NULL;
}

const char *ast_variable_retrieve_filtered(struct ast_config *config,
	const char *category, const char *variable, const char *filter)
{
	struct ast_category *cat = NULL;
	const char *value;

	while ((cat = ast_category_browse_filtered(config, category, cat, filter))) {
		value = ast_variable_find(cat, variable);
		if (value) {
			return value;
		}
	}

	return NULL;
}

const char *ast_variable_find(const struct ast_category *category, const char *variable)
{
	return ast_variable_find_in_list(category->root, variable);
}

const struct ast_variable *ast_variable_find_variable_in_list(const struct ast_variable *list, const char *variable_name)
{
	const struct ast_variable *v;

	for (v = list; v; v = v->next) {
		if (!strcasecmp(variable_name, v->name)) {
			return v;
		}
	}
	return NULL;
}

int ast_variables_match(const struct ast_variable *left, const struct ast_variable *right)
{
	char *op;

	if (left == right) {
		return 1;
	}

	if (!(left && right)) {
		return 0;
	}

	op = strrchr(right->name, ' ');
	if (op) {
		op++;
	}

	return ast_strings_match(left->value, op ? ast_strdupa(op) : NULL, right->value);
}

int ast_variable_lists_match(const struct ast_variable *left, const struct ast_variable *right, int exact_match)
{
	const struct ast_variable *field;
	int right_count = 0;
	int left_count = 0;

	if (left == right) {
		return 1;
	}

	if (!(left && right)) {
		return 0;
	}

	for (field = right; field; field = field->next) {
		char *space = strrchr(field->name, ' ');
		const struct ast_variable *old;
		char * name = (char *)field->name;

		if (space) {
			name = ast_strdup(field->name);
			if (!name) {
				return 0;
			}
			name[space - field->name] = '\0';
		}

		old = ast_variable_find_variable_in_list(left, name);
		if (name != field->name) {
			ast_free(name);
		}

		if (exact_match) {
			if (!old || strcmp(old->value, field->value)) {
				return 0;
			}
		} else {
			if (!ast_variables_match(old, field)) {
				return 0;
			}
		}

		right_count++;
	}

	if (exact_match) {
		for (field = left; field; field = field->next) {
			left_count++;
		}

		if (right_count != left_count) {
			return 0;
		}
	}

	return 1;
}

const char *ast_variable_find_in_list(const struct ast_variable *list, const char *variable)
{
	const struct ast_variable *v;

	for (v = list; v; v = v->next) {
		if (!strcasecmp(variable, v->name)) {
			return v->value;
		}
	}
	return NULL;
}

const char *ast_variable_find_last_in_list(const struct ast_variable *list, const char *variable)
{
	const struct ast_variable *v;
	const char *found = NULL;

	for (v = list; v; v = v->next) {
		if (!strcasecmp(variable, v->name)) {
			found = v->value;
		}
	}
	return found;
}

static struct ast_variable *variable_clone(const struct ast_variable *old)
{
	struct ast_variable *new = ast_variable_new(old->name, old->value, old->file);

	if (new) {
		new->lineno = old->lineno;
		new->object = old->object;
		new->blanklines = old->blanklines;
		/* TODO: clone comments? */
	}

	return new;
}

static void move_variables(struct ast_category *old, struct ast_category *new)
{
	struct ast_variable *var = old->root;

	old->root = NULL;
	/* we can just move the entire list in a single op */
	ast_variable_append(new, var);
}

/*! \brief Returns true if ALL of the regex expressions and category name match.
 * Both can be NULL (I.E. no predicate) which results in a true return;
 */
static int does_category_match(struct ast_category *cat, const char *category_name,
	const char *match, char sep)
{
	char *dupmatch;
	char *nvp = NULL;
	int match_found = 0, match_expressions = 0;
	int template_ok = 0;

	/* Only match on category name if it's not a NULL or empty string */
	if (!ast_strlen_zero(category_name) && strcasecmp(cat->name, category_name)) {
		return 0;
	}

	/* If match is NULL or empty, automatically match if not a template */
	if (ast_strlen_zero(match)) {
		return !cat->ignored;
	}

	dupmatch = ast_strdupa(match);

	while ((nvp = ast_strsep(&dupmatch, sep, AST_STRSEP_STRIP))) {
		struct ast_variable *v;
		char *match_name;
		char *match_value = NULL;
		char *regerr;
		int rc;
		regex_t r_name, r_value;

		match_expressions++;

		match_name = ast_strsep(&nvp, '=', AST_STRSEP_STRIP);
		match_value = ast_strsep(&nvp, '=', AST_STRSEP_STRIP);

		/* an empty match value is OK.  A NULL match value (no =) is NOT. */
		if (match_value == NULL) {
			break;
		}

		if (!strcmp("TEMPLATES", match_name)) {
			if (!strcasecmp("include", match_value)) {
				if (cat->ignored) {
					template_ok = 1;
				}
				match_found++;
			} else if (!strcasecmp("restrict", match_value)) {
				if (cat->ignored) {
					match_found++;
					template_ok = 1;
				} else {
					break;
				}
			}
			continue;
		}

		if ((rc = regcomp(&r_name, match_name, REG_EXTENDED | REG_NOSUB))) {
			regerr = ast_alloca(128);
			regerror(rc, &r_name, regerr, 128);
			ast_log(LOG_ERROR, "Regular expression '%s' failed to compile: %s\n",
				match_name, regerr);
			regfree(&r_name);
			return 0;
		}
		if ((rc = regcomp(&r_value, match_value, REG_EXTENDED | REG_NOSUB))) {
			regerr = ast_alloca(128);
			regerror(rc, &r_value, regerr, 128);
			ast_log(LOG_ERROR, "Regular expression '%s' failed to compile: %s\n",
				match_value, regerr);
			regfree(&r_name);
			regfree(&r_value);
			return 0;
		}

		for (v = cat->root; v; v = v->next) {
			if (!regexec(&r_name, v->name, 0, NULL, 0)
				&& !regexec(&r_value, v->value, 0, NULL, 0)) {
				match_found++;
				break;
			}
		}
		regfree(&r_name);
		regfree(&r_value);
	}
	if (match_found == match_expressions && (!cat->ignored || template_ok)) {
		return 1;
	}
	return 0;
}


static struct ast_category *new_category(const char *name, const char *in_file, int lineno, int template)
{
	struct ast_category *category;

	category = ast_calloc(1, sizeof(*category));
	if (!category) {
		return NULL;
	}
	category->file = ast_strdup(in_file);
	if (!category->file) {
		ast_category_destroy(category);
		return NULL;
	}
	ast_copy_string(category->name, name, sizeof(category->name));
	category->lineno = lineno; /* if you don't know the lineno, set it to 999999 or something real big */
	category->ignored = template;
	return category;
}

struct ast_category *ast_category_new(const char *name, const char *in_file, int lineno)
{
	return new_category(name, in_file, lineno, 0);
}

struct ast_category *ast_category_new_template(const char *name, const char *in_file, int lineno)
{
	return new_category(name, in_file, lineno, 1);
}

static struct ast_category *category_get_sep(const struct ast_config *config,
	const char *category_name, const char *filter, char sep, char pointer_match_possible)
{
	struct ast_category *cat;

	if (pointer_match_possible) {
		for (cat = config->root; cat; cat = cat->next) {
			if (cat->name == category_name && does_category_match(cat, category_name, filter, sep)) {
				return cat;
			}
		}
	}

	for (cat = config->root; cat; cat = cat->next) {
		if (does_category_match(cat, category_name, filter, sep)) {
			return cat;
		}
	}

	return NULL;
}

struct ast_category *ast_category_get(const struct ast_config *config,
	const char *category_name, const char *filter)
{
	return category_get_sep(config, category_name, filter, ',', 1);
}

const char *ast_category_get_name(const struct ast_category *category)
{
	return category->name;
}

int ast_category_is_template(const struct ast_category *category)
{
	return category->ignored;
}

struct ast_str *ast_category_get_templates(const struct ast_category *category)
{
	struct ast_category_template_instance *template;
	struct ast_str *str;
	int first = 1;

	if (AST_LIST_EMPTY(&category->template_instances)) {
		return NULL;
	}

	str = ast_str_create(128);
	if (!str) {
		return NULL;
	}

	AST_LIST_TRAVERSE(&category->template_instances, template, next) {
		ast_str_append(&str, 0, "%s%s", first ? "" : ",", template->name);
		first = 0;
	}

	return str;
}

int ast_category_exist(const struct ast_config *config, const char *category_name,
	const char *filter)
{
	return !!ast_category_get(config, category_name, filter);
}

void ast_category_append(struct ast_config *config, struct ast_category *category)
{
	if (config->last) {
		config->last->next = category;
		category->prev = config->last;
	} else {
		config->root = category;
		category->prev = NULL;
	}
	category->next = NULL;
	category->include_level = config->include_level;

	config->last = category;
	config->current = category;
}

int ast_category_insert(struct ast_config *config, struct ast_category *cat, const char *match)
{
	struct ast_category *cur_category;

	if (!config || !config->root || !cat || !match) {
		return -1;
	}

	if (!strcasecmp(config->root->name, match)) {
		cat->next = config->root;
		cat->prev = NULL;
		config->root->prev = cat;
		config->root = cat;
		return 0;
	}

	for (cur_category = config->root->next; cur_category; cur_category = cur_category->next) {
		if (!strcasecmp(cur_category->name, match)) {
			cat->prev = cur_category->prev;
			cat->prev->next = cat;

			cat->next = cur_category;
			cur_category->prev = cat;

			return 0;
		}
	}

	return -1;
}

static void ast_destroy_template_list(struct ast_category *cat)
{
	struct ast_category_template_instance *x;

	while ((x = AST_LIST_REMOVE_HEAD(&cat->template_instances, next)))
		ast_free(x);
}

void ast_category_destroy(struct ast_category *cat)
{
	ast_variables_destroy(cat->root);
	cat->root = NULL;
	cat->last = NULL;
	ast_comment_destroy(&cat->precomments);
	ast_comment_destroy(&cat->sameline);
	ast_comment_destroy(&cat->trailing);
	ast_destroy_template_list(cat);
	ast_free(cat->file);
	ast_free(cat);
}

static void ast_includes_destroy(struct ast_config_include *incls)
{
	struct ast_config_include *incl,*inclnext;

	for (incl=incls; incl; incl = inclnext) {
		inclnext = incl->next;
		ast_free(incl->include_location_file);
		ast_free(incl->exec_file);
		ast_free(incl->included_file);
		ast_free(incl);
	}
}

static struct ast_category *next_available_category(struct ast_category *cat,
	const char *name, const char *filter)
{
	for (; cat && !does_category_match(cat, name, filter, ','); cat = cat->next);

	return cat;
}

/*! return the first var of a category */
struct ast_variable *ast_category_first(struct ast_category *cat)
{
	return (cat) ? cat->root : NULL;
}

struct ast_variable *ast_category_root(struct ast_config *config, char *cat)
{
	struct ast_category *category = ast_category_get(config, cat, NULL);

	if (category)
		return category->root;
	return NULL;
}

void ast_config_sort_categories(struct ast_config *config, int descending,
								int (*comparator)(struct ast_category *p, struct ast_category *q))
{
	/*
	 * The contents of this function are adapted from
	 * an example of linked list merge sorting
	 * copyright 2001 Simon Tatham.
	 *
	 * Permission is hereby granted, free of charge, to any person
	 * obtaining a copy of this software and associated documentation
	 * files (the "Software"), to deal in the Software without
	 * restriction, including without limitation the rights to use,
	 * copy, modify, merge, publish, distribute, sublicense, and/or
	 * sell copies of the Software, and to permit persons to whom the
	 * Software is furnished to do so, subject to the following
	 * conditions:
	 *
	 * The above copyright notice and this permission notice shall be
	 * included in all copies or substantial portions of the Software.
	 *
	 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
	 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
	 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
	 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
	 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
	 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	 * SOFTWARE.
	 */

	int insize = 1;
	struct ast_category *p, *q, *e, *tail;
	int nmerges, psize, qsize, i;

	/* If the descending flag was sent, we'll apply inversion to the comparison function's return. */
	if (descending) {
		descending = -1;
	} else {
		descending = 1;
	}

	if (!config->root) {
		return;
	}

	while (1) {
		p = config->root;
		config->root = NULL;
		tail = NULL;

		nmerges = 0; /* count number of merges we do in this pass */

		while (p) {
			nmerges++; /* there exists a merge to be done */

			/* step `insize' places along from p */
			q = p;
			psize = 0;
			for (i = 0; i < insize; i++) {
				psize++;
				q = q->next;
				if (!q) {
					break;
				}
			}

			/* if q hasn't fallen off end, we have two lists to merge */
			qsize = insize;

			/* now we have two lists; merge them */
			while (psize > 0 || (qsize > 0 && q)) {
				/* decide whether next element of merge comes from p or q */
				if (psize == 0) {
					/* p is empty; e must come from q. */
					e = q;
					q = q->next;
					qsize--;
				} else if (qsize == 0 || !q) {
					/* q is empty; e must come from p. */
					e = p; p = p->next; psize--;
				} else if ((comparator(p,q) * descending) <= 0) {
					/* First element of p is lower (or same) e must come from p. */
					e = p;
					p = p->next;
					psize--;
				} else {
					/* First element of q is lower; e must come from q. */
					e = q;
					q = q->next;
					qsize--;
				}

				/* add the next element to the merged list */
				if (tail) {
					tail->next = e;
				} else {
					config->root = e;
				}
				tail = e;
			}

			/* now p has stepped `insize' places along, and q has too */
			p = q;
		}

		tail->next = NULL;

		/* If we have done only one merge, we're finished. */
		if (nmerges <= 1) { /* allow for nmerges==0, the empty list case */
			return;
		}

		/* Otherwise repeat, merging lists twice the size */
		insize *= 2;
	}

}

char *ast_category_browse(struct ast_config *config, const char *prev_name)
{
	struct ast_category *cat;

	if (!prev_name) {
		/* First time browse. */
		cat = config->root;
	} else if (config->last_browse && (config->last_browse->name == prev_name)) {
		/* Simple last browse found. */
		cat = config->last_browse->next;
	} else {
		/*
		 * Config changed since last browse.
		 *
		 * First try cheap last browse search. (Rebrowsing a different
		 * previous category?)
		 */
		for (cat = config->root; cat; cat = cat->next) {
			if (cat->name == prev_name) {
				/* Found it. */
				cat = cat->next;
				break;
			}
		}
		if (!cat) {
			/*
			 * Have to do it the hard way. (Last category was deleted and
			 * re-added?)
			 */
			for (cat = config->root; cat; cat = cat->next) {
				if (!strcasecmp(cat->name, prev_name)) {
					/* Found it. */
					cat = cat->next;
					break;
				}
			}
		}
	}

	if (cat)
		cat = next_available_category(cat, NULL, NULL);

	config->last_browse = cat;
	return (cat) ? cat->name : NULL;
}

struct ast_category *ast_category_browse_filtered(struct ast_config *config,
	const char *category_name, struct ast_category *prev, const char *filter)
{
	struct ast_category *cat;

	if (!prev) {
		prev = config->root;
	} else {
		prev = prev->next;
	}

	cat = next_available_category(prev, category_name, filter);

	return cat;
}

struct ast_variable *ast_category_detach_variables(struct ast_category *cat)
{
	struct ast_variable *v;

	v = cat->root;
	cat->root = NULL;
	cat->last = NULL;

	return v;
}

void ast_category_rename(struct ast_category *cat, const char *name)
{
	ast_copy_string(cat->name, name, sizeof(cat->name));
}

int ast_category_inherit(struct ast_category *new, const struct ast_category *base)
{
	struct ast_variable *var;
	struct ast_category_template_instance *x;

	x = ast_calloc(1, sizeof(*x));
	if (!x) {
		return -1;
	}
	strcpy(x->name, base->name);
	x->inst = base;
	AST_LIST_INSERT_TAIL(&new->template_instances, x, next);
	for (var = base->root; var; var = var->next) {
		struct ast_variable *cloned = variable_clone(var);
		if (!cloned) {
			return -1;
		}
		cloned->inherited = 1;
		ast_variable_append(new, cloned);
	}
	return 0;
}

struct ast_config *ast_config_new(void)
{
	struct ast_config *config;

	if ((config = ast_calloc(1, sizeof(*config))))
		config->max_include_level = MAX_INCLUDE_LEVEL;
	return config;
}

int ast_variable_delete(struct ast_category *category, const char *variable, const char *match, const char *line)
{
	struct ast_variable *cur, *prev=NULL, *curn;
	int res = -1;
	int num_item = 0;
	int req_item;

	req_item = -1;
	if (!ast_strlen_zero(line)) {
		/* Requesting to delete by item number. */
		if (sscanf(line, "%30d", &req_item) != 1
			|| req_item < 0) {
			/* Invalid item number to delete. */
			return -1;
		}
	}

	prev = NULL;
	cur = category->root;
	while (cur) {
		curn = cur->next;
		/* Delete by item number or by variable name with optional value. */
		if ((0 <= req_item && num_item == req_item)
			|| (req_item < 0 && !strcasecmp(cur->name, variable)
				&& (ast_strlen_zero(match) || !strcasecmp(cur->value, match)))) {
			if (prev) {
				prev->next = cur->next;
				if (cur == category->last)
					category->last = prev;
			} else {
				category->root = cur->next;
				if (cur == category->last)
					category->last = NULL;
			}
			ast_variable_destroy(cur);
			res = 0;
		} else
			prev = cur;

		cur = curn;
		++num_item;
	}
	return res;
}

int ast_variable_update(struct ast_category *category, const char *variable,
						const char *value, const char *match, unsigned int object)
{
	struct ast_variable *cur, *prev=NULL, *newer=NULL;

	for (cur = category->root; cur; prev = cur, cur = cur->next) {
		if (strcasecmp(cur->name, variable) ||
			(!ast_strlen_zero(match) && strcasecmp(cur->value, match)))
			continue;

		if (!(newer = ast_variable_new(variable, value, cur->file)))
			return -1;

		ast_variable_move(newer, cur);
		newer->object = newer->object || object;

		/* Replace the old node in the list with the new node. */
		newer->next = cur->next;
		if (prev)
			prev->next = newer;
		else
			category->root = newer;
		if (category->last == cur)
			category->last = newer;

		ast_variable_destroy(cur);

		return 0;
	}

	/* Could not find variable to update */
	return -1;
}

struct ast_category *ast_category_delete(struct ast_config *config,
	struct ast_category *category)
{
	struct ast_category *prev;

	if (!config || !category) {
		return NULL;
	}

	if (category->prev) {
		category->prev->next = category->next;
	} else {
		config->root = category->next;
	}

	if (category->next) {
		category->next->prev = category->prev;
	} else {
		config->last = category->prev;
	}

	prev = category->prev;

	if (config->last_browse == category) {
		config->last_browse = prev;
	}

	ast_category_destroy(category);

	return prev;
}

int ast_category_empty(struct ast_category *category)
{
	if (!category) {
		return -1;
	}

	ast_variables_destroy(category->root);
	category->root = NULL;
	category->last = NULL;

	return 0;
}

void ast_config_destroy(struct ast_config *cfg)
{
	struct ast_category *cat, *catn;

	if (!cfg)
		return;

	ast_includes_destroy(cfg->includes);

	cat = cfg->root;
	while (cat) {
		catn = cat;
		cat = cat->next;
		ast_category_destroy(catn);
	}
	ast_free(cfg);
}

struct ast_category *ast_config_get_current_category(const struct ast_config *cfg)
{
	return cfg->current;
}

void ast_config_set_current_category(struct ast_config *cfg, const struct ast_category *cat)
{
	/* cast below is just to silence compiler warning about dropping "const" */
	cfg->current = (struct ast_category *) cat;
}

/*!
 * \internal
 * \brief Create a new cfmtime list node.
 *
 * \param filename Config filename caching.
 * \param who_asked Who wanted to know.
 *
 * \retval cfmtime New node on success.
 * \retval NULL on error.
 */
static struct cache_file_mtime *cfmtime_new(const char *filename, const char *who_asked)
{
	struct cache_file_mtime *cfmtime;
	char *dst;

	cfmtime = ast_calloc(1,
		sizeof(*cfmtime) + strlen(filename) + 1 + strlen(who_asked) + 1);
	if (!cfmtime) {
		return NULL;
	}
	dst = cfmtime->filename;	/* writable space starts here */
	strcpy(dst, filename); /* Safe */
	dst += strlen(dst) + 1;
	cfmtime->who_asked = strcpy(dst, who_asked); /* Safe */

	return cfmtime;
}

enum config_cache_attribute_enum {
	ATTRIBUTE_INCLUDE = 0,
	ATTRIBUTE_EXEC = 1,
};

/*!
 * \internal
 * \brief Save the stat() data to the cached file modtime struct.
 *
 * \param cfmtime Cached file modtime.
 * \param statbuf Buffer filled in by stat().
 */
static void cfmstat_save(struct cache_file_mtime *cfmtime, struct stat *statbuf)
{
	cfmtime->stat_size = statbuf->st_size;
#if defined(HAVE_STRUCT_STAT_ST_MTIM)
	cfmtime->stat_mtime_nsec = statbuf->st_mtim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	cfmtime->stat_mtime_nsec = statbuf->st_mtimensec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
	cfmtime->stat_mtime_nsec = statbuf->st_mtimespec.tv_nsec;
#else
	cfmtime->stat_mtime_nsec = 0;
#endif
	cfmtime->stat_mtime = statbuf->st_mtime;
}

/*!
 * \internal
 * \brief Compare the stat() data with the cached file modtime struct.
 *
 * \param cfmtime Cached file modtime.
 * \param statbuf Buffer filled in by stat().
 *
 * \retval non-zero if different.
 */
static int cfmstat_cmp(struct cache_file_mtime *cfmtime, struct stat *statbuf)
{
	struct cache_file_mtime cfm_buf;

	cfmstat_save(&cfm_buf, statbuf);

	return cfmtime->stat_size != cfm_buf.stat_size
		|| cfmtime->stat_mtime != cfm_buf.stat_mtime
		|| cfmtime->stat_mtime_nsec != cfm_buf.stat_mtime_nsec;
}

/*!
 * \internal
 * \brief Clear the cached file modtime include list.
 *
 * \param cfmtime Cached file modtime.
 *
 * \note cfmtime_head is assumed already locked.
 */
static void config_cache_flush_includes(struct cache_file_mtime *cfmtime)
{
	struct cache_file_include *cfinclude;

	while ((cfinclude = AST_LIST_REMOVE_HEAD(&cfmtime->includes, list))) {
		ast_free(cfinclude);
	}
}

/*!
 * \internal
 * \brief Destroy the given cached file modtime entry.
 *
 * \param cfmtime Cached file modtime.
 *
 * \note cfmtime_head is assumed already locked.
 */
static void config_cache_destroy_entry(struct cache_file_mtime *cfmtime)
{
	config_cache_flush_includes(cfmtime);
	ast_free(cfmtime);
}

/*!
 * \internal
 * \brief Remove and destroy the config cache entry for the filename and who_asked.
 *
 * \param filename Config filename.
 * \param who_asked Which module asked.
 */
static void config_cache_remove(const char *filename, const char *who_asked)
{
	struct cache_file_mtime *cfmtime;

	AST_LIST_LOCK(&cfmtime_head);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&cfmtime_head, cfmtime, list) {
		if (!strcmp(cfmtime->filename, filename)
			&& !strcmp(cfmtime->who_asked, who_asked)) {
			AST_LIST_REMOVE_CURRENT(list);
			config_cache_destroy_entry(cfmtime);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&cfmtime_head);
}

static void config_cache_attribute(const char *configfile, enum config_cache_attribute_enum attrtype, const char *filename, const char *who_asked)
{
	struct cache_file_mtime *cfmtime;
	struct cache_file_include *cfinclude;

	/* Find our cached entry for this configuration file */
	AST_LIST_LOCK(&cfmtime_head);
	AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
		if (!strcmp(cfmtime->filename, configfile) && !strcmp(cfmtime->who_asked, who_asked))
			break;
	}
	if (!cfmtime) {
		cfmtime = cfmtime_new(configfile, who_asked);
		if (!cfmtime) {
			AST_LIST_UNLOCK(&cfmtime_head);
			return;
		}
		/* Note that the file mtime is initialized to 0, i.e. 1970 */
		AST_LIST_INSERT_SORTALPHA(&cfmtime_head, cfmtime, list, filename);
	}

	switch (attrtype) {
	case ATTRIBUTE_INCLUDE:
		AST_LIST_TRAVERSE(&cfmtime->includes, cfinclude, list) {
			if (!strcmp(cfinclude->include, filename)) {
				AST_LIST_UNLOCK(&cfmtime_head);
				return;
			}
		}
		cfinclude = ast_calloc(1, sizeof(*cfinclude) + strlen(filename) + 1);
		if (!cfinclude) {
			AST_LIST_UNLOCK(&cfmtime_head);
			return;
		}
		strcpy(cfinclude->include, filename); /* Safe */
		AST_LIST_INSERT_TAIL(&cfmtime->includes, cfinclude, list);
		break;
	case ATTRIBUTE_EXEC:
		cfmtime->has_exec = 1;
		break;
	}
	AST_LIST_UNLOCK(&cfmtime_head);
}

/*! \brief parse one line in the configuration.
 * \verbatim
 * We can have a category header	[foo](...)
 * a directive				#include / #exec
 * or a regular line			name = value
 * \endverbatim
 */
static int process_text_line(struct ast_config *cfg, struct ast_category **cat,
	char *buf, int lineno, const char *configfile, struct ast_flags flags,
	struct ast_str *comment_buffer,
	struct ast_str *lline_buffer,
	const char *suggested_include_file,
	struct ast_category **last_cat, struct ast_variable **last_var, const char *who_asked)
{
	char *c;
	char *cur = buf;
	struct ast_variable *v;
	char exec_file[512];

	/* Actually parse the entry */
	if (cur[0] == '[') { /* A category header */
		/* format is one of the following:
		 * [foo]	define a new category named 'foo'
		 * [foo](!)	define a new template category named 'foo'
		 * [foo](+)	append to category 'foo', error if foo does not exist.
		 * [foo](a)	define a new category and inherit from category or template a.
		 *		You can put a comma-separated list of categories and templates
		 *		and '!' and '+' between parentheses, with obvious meaning.
		 */
		struct ast_category *newcat;
		char *catname;

		c = strchr(cur, ']');
		if (!c) {
			ast_log(LOG_WARNING, "parse error: no closing ']', line %d of %s\n", lineno, configfile);
			return -1;
		}
		*c++ = '\0';
		cur++;
		if (*c++ != '(')
			c = NULL;
		catname = cur;
		*cat = newcat = ast_category_new(catname,
			S_OR(suggested_include_file, cfg->include_level == 1 ? "" : configfile),
			lineno);
		if (!newcat) {
			return -1;
		}
		(*cat)->lineno = lineno;

		/* add comments */
		if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
			newcat->precomments = ALLOC_COMMENT(comment_buffer);
		if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
			newcat->sameline = ALLOC_COMMENT(lline_buffer);
		if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
			CB_RESET(comment_buffer, lline_buffer);

		/* If there are options or categories to inherit from, process them now */
		if (c) {
			if (!(cur = strchr(c, ')'))) {
				ast_category_destroy(newcat);
				ast_log(LOG_WARNING, "parse error: no closing ')', line %d of %s\n", lineno, configfile);
				return -1;
			}
			*cur = '\0';
			while ((cur = strsep(&c, ","))) {
				if (!strcasecmp(cur, "!")) {
					(*cat)->ignored = 1;
				} else if (cur[0] == '+') {
					char *filter = NULL;

					if (cur[1] != ',') {
						filter = &cur[1];
					}
					*cat = category_get_sep(cfg, catname, filter, '&', 0);
					if (!(*cat)) {
						if (newcat) {
							ast_category_destroy(newcat);
						}
						ast_log(LOG_WARNING, "Category addition requested, but category '%s' does not exist, line %d of %s\n", catname, lineno, configfile);
						return -1;
					}
					if (newcat) {
						ast_config_set_current_category(cfg, *cat);
						(*cat)->ignored |= newcat->ignored;
						move_variables(newcat, *cat);
						ast_category_destroy(newcat);
						newcat = NULL;
					}
				} else {
					struct ast_category *base;

					base = category_get_sep(cfg, cur, "TEMPLATES=include", ',', 0);
					if (!base) {
						if (newcat) {
							ast_category_destroy(newcat);
						}
						ast_log(LOG_WARNING, "Inheritance requested, but category '%s' does not exist, line %d of %s\n", cur, lineno, configfile);
						return -1;
					}
					if (ast_category_inherit(*cat, base)) {
						if (newcat) {
							ast_category_destroy(newcat);
						}
						ast_log(LOG_ERROR, "Inheritance requested, but allocation failed\n");
						return -1;
					}
				}
			}
		}

		/*
		 * We need to set *last_cat to newcat here regardless.  If the
		 * category is being appended to we have no place for trailing
		 * comments on the appended category.  The appended category
		 * may be in another file or it already has trailing comments
		 * that we would then leak.
		 */
		*last_var = NULL;
		*last_cat = newcat;
		if (newcat) {
			ast_category_append(cfg, newcat);
		}
	} else if (cur[0] == '#') { /* A directive - #include or #exec */
		char *cur2;
		char real_inclusion_name[256];
		int do_include = 0;	/* otherwise, it is exec */
		int try_include = 0;

		cur++;
		c = cur;
		while (*c && (*c > 32)) {
			c++;
		}

		if (*c) {
			*c = '\0';
			/* Find real argument */
			c = ast_strip(c + 1);
			if (!(*c)) {
				c = NULL;
			}
		} else {
			c = NULL;
		}
		if (!strcasecmp(cur, "include")) {
			do_include = 1;
		} else if (!strcasecmp(cur, "tryinclude")) {
			do_include = 1;
			try_include = 1;
		} else if (!strcasecmp(cur, "exec")) {
			if (!ast_opt_exec_includes) {
				ast_log(LOG_WARNING, "Cannot perform #exec unless execincludes option is enabled in asterisk.conf (options section)!\n");
				return 0;	/* XXX is this correct ? or we should return -1 ? */
			}
		} else {
			ast_log(LOG_WARNING, "Unknown directive '#%s' at line %d of %s\n", cur, lineno, configfile);
			return 0;	/* XXX is this correct ? or we should return -1 ? */
		}

		if (c == NULL) {
			ast_log(LOG_WARNING, "Directive '#%s' needs an argument (%s) at line %d of %s\n",
					do_include ? "include / tryinclude" : "exec",
					do_include ? "filename" : "/path/to/executable",
					lineno,
					configfile);
			return 0;	/* XXX is this correct ? or we should return -1 ? */
		}

		cur = c;
		/* Strip off leading and trailing "'s and <>'s */
		/* Dequote */
		if ((*c == '"') || (*c == '<')) {
			char quote_char = *c;
			if (quote_char == '<') {
				quote_char = '>';
			}

			if (*(c + strlen(c) - 1) == quote_char) {
				cur++;
				*(c + strlen(c) - 1) = '\0';
			}
		}
		cur2 = cur;

		/* #exec </path/to/executable>
		   We create a tmp file, then we #include it, then we delete it. */
		if (!do_include) {
			struct timeval now = ast_tvnow();
			char cmd[1024];

			if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE))
				config_cache_attribute(configfile, ATTRIBUTE_EXEC, NULL, who_asked);
			snprintf(exec_file, sizeof(exec_file), "/var/tmp/exec.%d%d.%ld", (int)now.tv_sec, (int)now.tv_usec, (long)pthread_self());
			if (snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", cur, exec_file) >= sizeof(cmd)) {
				ast_log(LOG_ERROR, "Failed to construct command string to execute %s.\n", cur);

				return -1;
			}
			ast_safe_system(cmd);
			cur = exec_file;
		} else {
			if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE))
				config_cache_attribute(configfile, ATTRIBUTE_INCLUDE, cur, who_asked);
			exec_file[0] = '\0';
		}
		/* A #include */
		/* record this inclusion */
		ast_include_new(cfg, cfg->include_level == 1 ? "" : configfile, cur, !do_include, cur2, lineno, real_inclusion_name, sizeof(real_inclusion_name));

		do_include = ast_config_internal_load(cur, cfg, flags, real_inclusion_name, who_asked) ? 1 : 0;
		if (!ast_strlen_zero(exec_file))
			unlink(exec_file);
		if (!do_include && !try_include) {
			ast_log(LOG_ERROR, "The file '%s' was listed as a #include but it does not exist.\n", cur);
			return -1;
		}
		/* XXX otherwise what ? the default return is 0 anyways */

	} else {
		/* Just a line (variable = value) */
		int object = 0;
		int is_escaped;

		if (!(*cat)) {
			ast_log(LOG_WARNING,
				"parse error: No category context for line %d of %s\n", lineno, configfile);
			return -1;
		}

		is_escaped = cur[0] == '\\';
		if (is_escaped) {
			/* First character is escaped. */
			++cur;
			if (cur[0] < 33) {
				ast_log(LOG_ERROR, "Invalid escape in line %d of %s\n", lineno, configfile);
				return -1;
			}
		}
		c = strchr(cur + is_escaped, '=');

		if (c && c > cur + is_escaped && (*(c - 1) == '+')) {
			struct ast_variable *var, *replace = NULL;
			struct ast_str **str = ast_threadstorage_get(&appendbuf, sizeof(*str));

			if (!str || !*str) {
				return -1;
			}

			*(c - 1) = '\0';
			c++;
			cur = ast_strip(cur);

			/* Must iterate through category until we find last variable of same name (since there could be multiple) */
			for (var = ast_category_first(*cat); var; var = var->next) {
				if (!strcmp(var->name, cur)) {
					replace = var;
				}
			}

			if (!replace) {
				/* Nothing to replace; just set a variable normally. */
				goto set_new_variable;
			}

			ast_str_set(str, 0, "%s", replace->value);
			ast_str_append(str, 0, "%s", c);
			ast_str_trim_blanks(*str);
			ast_variable_update(*cat, replace->name, ast_skip_blanks(ast_str_buffer(*str)), replace->value, object);
		} else if (c) {
			*c = 0;
			c++;
			/* Ignore > in => */
			if (*c== '>') {
				object = 1;
				c++;
			}
			cur = ast_strip(cur);
set_new_variable:
			if (ast_strlen_zero(cur)) {
				ast_log(LOG_WARNING, "No variable name in line %d of %s\n", lineno, configfile);
			} else if ((v = ast_variable_new(cur, ast_strip(c), S_OR(suggested_include_file, cfg->include_level == 1 ? "" : configfile)))) {
				v->lineno = lineno;
				v->object = object;
				*last_cat = NULL;
				*last_var = v;
				/* Put and reset comments */
				v->blanklines = 0;
				ast_variable_append(*cat, v);
				/* add comments */
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
					v->precomments = ALLOC_COMMENT(comment_buffer);
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
					v->sameline = ALLOC_COMMENT(lline_buffer);
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
					CB_RESET(comment_buffer, lline_buffer);

			} else {
				return -1;
			}
		} else {
			ast_log(LOG_WARNING, "No '=' (equal sign) in line %d of %s\n", lineno, configfile);
		}
	}
	return 0;
}

static struct ast_config *config_text_file_load(const char *database, const char *table, const char *filename, struct ast_config *cfg, struct ast_flags flags, const char *suggested_include_file, const char *who_asked)
{
	char fn[256];
#if defined(LOW_MEMORY)
	char buf[512];
#else
	char buf[8192];
#endif
	char *new_buf, *comment_p, *process_buf;
	FILE *f;
	int lineno=0;
	int comment = 0, nest[MAX_NESTED_COMMENTS];
	struct ast_category *cat = NULL;
	int count = 0;
	struct stat statbuf;
	struct cache_file_mtime *cfmtime = NULL;
	struct cache_file_include *cfinclude;
	struct ast_variable *last_var = NULL;
	struct ast_category *last_cat = NULL;
	/*! Growable string buffer */
	struct ast_str *comment_buffer = NULL;	/*!< this will be a comment collector.*/
	struct ast_str *lline_buffer = NULL;	/*!< A buffer for stuff behind the ; */
	int glob_ret;
	glob_t globbuf;

	if (cfg) {
		cat = ast_config_get_current_category(cfg);
	}

	if (filename[0] == '/') {
		ast_copy_string(fn, filename, sizeof(fn));
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_CONFIG_DIR, filename);
	}

	if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)) {
		comment_buffer = ast_str_create(CB_SIZE);
		if (comment_buffer) {
			lline_buffer = ast_str_create(CB_SIZE);
		}
		if (!lline_buffer) {
			ast_free(comment_buffer);
			ast_log(LOG_ERROR, "Failed to initialize the comment buffer!\n");
			return NULL;
		}
	}

	globbuf.gl_offs = 0;	/* initialize it to silence gcc */
	glob_ret = glob(fn, MY_GLOB_FLAGS, NULL, &globbuf);
	if (glob_ret == GLOB_NOSPACE) {
		ast_log(LOG_WARNING,
			"Glob Expansion of pattern '%s' failed: Not enough memory\n", fn);
	} else if (glob_ret  == GLOB_ABORTED) {
		ast_log(LOG_WARNING,
			"Glob Expansion of pattern '%s' failed: Read error\n", fn);
	} else {
		/* loop over expanded files */
		int i;

		if (!cfg && (globbuf.gl_pathc != 1 || strcmp(fn, globbuf.gl_pathv[0]))) {
			/*
			 * We just want a file changed answer and since we cannot
			 * tell if a file was deleted with wildcard matching we will
			 * assume that something has always changed.  Also without
			 * a lot of refactoring we couldn't check more than one file
			 * for changes in the glob loop anyway.
			 */
			globfree(&globbuf);
			ast_free(comment_buffer);
			ast_free(lline_buffer);
			return NULL;
		}
		for (i=0; i<globbuf.gl_pathc; i++) {
			ast_copy_string(fn, globbuf.gl_pathv[i], sizeof(fn));

			/*
			 * The following is not a loop, but just a convenient way to define a block
			 * (using do { } while(0) ), and be able to exit from it with 'continue'
			 * or 'break' in case of errors. Nice trick.
			 */
			do {
				if (stat(fn, &statbuf)) {
					if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE)) {
						config_cache_remove(fn, who_asked);
					}
					continue;
				}

				if (!S_ISREG(statbuf.st_mode)) {
					ast_log(LOG_WARNING, "'%s' is not a regular file, ignoring\n", fn);
					if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE)) {
						config_cache_remove(fn, who_asked);
					}
					continue;
				}

				if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE)) {
					/* Find our cached entry for this configuration file */
					AST_LIST_LOCK(&cfmtime_head);
					AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
						if (!strcmp(cfmtime->filename, fn) && !strcmp(cfmtime->who_asked, who_asked)) {
							break;
						}
					}
					if (!cfmtime) {
						cfmtime = cfmtime_new(fn, who_asked);
						if (!cfmtime) {
							AST_LIST_UNLOCK(&cfmtime_head);
							continue;
						}
						/* Note that the file mtime is initialized to 0, i.e. 1970 */
						AST_LIST_INSERT_SORTALPHA(&cfmtime_head, cfmtime, list, filename);
					}
				}

				if (cfmtime
					&& !cfmtime->has_exec
					&& !cfmstat_cmp(cfmtime, &statbuf)
					&& ast_test_flag(&flags, CONFIG_FLAG_FILEUNCHANGED)) {
					int unchanged = 1;

					/* File is unchanged, what about the (cached) includes (if any)? */
					AST_LIST_TRAVERSE(&cfmtime->includes, cfinclude, list) {
						if (!config_text_file_load(NULL, NULL, cfinclude->include,
							NULL, flags, "", who_asked)) {
							/* One change is enough to short-circuit and reload the whole shebang */
							unchanged = 0;
							break;
						}
					}

					if (unchanged) {
						AST_LIST_UNLOCK(&cfmtime_head);
						globfree(&globbuf);
						ast_free(comment_buffer);
						ast_free(lline_buffer);
						return CONFIG_STATUS_FILEUNCHANGED;
					}
				}

				/* If cfg is NULL, then we just want a file changed answer. */
				if (cfg == NULL) {
					if (cfmtime) {
						AST_LIST_UNLOCK(&cfmtime_head);
					}
					continue;
				}

				if (cfmtime) {
					/* Forget about what we thought we knew about this file's includes. */
					cfmtime->has_exec = 0;
					config_cache_flush_includes(cfmtime);

					cfmstat_save(cfmtime, &statbuf);
					AST_LIST_UNLOCK(&cfmtime_head);
				}

				if (!(f = fopen(fn, "r"))) {
					ast_debug(1, "No file to parse: %s\n", fn);
					ast_verb(2, "Parsing '%s': Not found (%s)\n", fn, strerror(errno));
					continue;
				}
				count++;
				/* If we get to this point, then we're loading regardless */
				ast_clear_flag(&flags, CONFIG_FLAG_FILEUNCHANGED);
				ast_debug(1, "Parsing %s\n", fn);
				while (!feof(f)) {
					lineno++;
					if (fgets(buf, sizeof(buf), f)) {
						/* Skip lines that are too long */
						if (strlen(buf) == sizeof(buf) - 1 && buf[sizeof(buf) - 2] != '\n') {
							ast_log(LOG_WARNING, "Line %d too long, skipping. It begins with: %.32s...\n", lineno, buf);
							while (fgets(buf, sizeof(buf), f)) {
								if (strlen(buf) != sizeof(buf) - 1 || buf[sizeof(buf) - 2] == '\n') {
									break;
								}
							}
							continue;
						}

						/* If there is a UTF-8 BOM, skip over it */
						if (lineno == 1) {
#define UTF8_BOM "\xEF\xBB\xBF"
							size_t line_bytes = strlen(buf);
							size_t bom_bytes = sizeof(UTF8_BOM) - 1;
							if (line_bytes >= bom_bytes
							   && !memcmp(buf, UTF8_BOM, bom_bytes)) {
								memmove(buf, &buf[bom_bytes], line_bytes - bom_bytes + 1);
							}
#undef UTF8_BOM
						}

						if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)
							&& lline_buffer
							&& ast_str_strlen(lline_buffer)) {
							CB_ADD(&comment_buffer, ast_str_buffer(lline_buffer)); /* add the current lline buffer to the comment buffer */
							ast_str_reset(lline_buffer);        /* erase the lline buffer */
						}

						new_buf = buf;
						if (comment) {
							process_buf = NULL;
						} else {
							process_buf = buf;
						}

						if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)
							&& comment_buffer
							&& ast_str_strlen(comment_buffer)
							&& (ast_strlen_zero(buf) || strlen(buf) == strspn(buf," \t\n\r"))) {
							/* blank line? really? Can we add it to an existing comment and maybe preserve inter- and post- comment spacing? */
							CB_ADD(&comment_buffer, "\n"); /* add a newline to the comment buffer */
							continue; /* go get a new line, then */
						}

						while ((comment_p = strchr(new_buf, COMMENT_META))) {
							if ((comment_p > new_buf) && (*(comment_p - 1) == '\\')) {
								/* Escaped semicolons aren't comments. */
								new_buf = comment_p;
								/* write over the \ and bring the null terminator with us */
								memmove(comment_p - 1, comment_p, strlen(comment_p) + 1);
							} else if (comment_p[1] == COMMENT_TAG && comment_p[2] == COMMENT_TAG && (comment_p[3] != '-')) {
								/* Meta-Comment start detected ";--" */
								if (comment < MAX_NESTED_COMMENTS) {
									*comment_p = '\0';
									new_buf = comment_p + 3;
									comment++;
									nest[comment-1] = lineno;
								} else {
									ast_log(LOG_ERROR, "Maximum nest limit of %d reached.\n", MAX_NESTED_COMMENTS);
								}
							} else if ((comment_p >= new_buf + 2) &&
								   (*(comment_p - 1) == COMMENT_TAG) &&
								   (*(comment_p - 2) == COMMENT_TAG)) {
								/* Meta-Comment end detected "--;" */
								comment--;
								new_buf = comment_p + 1;
								if (!comment) {
									/* Back to non-comment now */
									if (process_buf) {
										/* Actually have to move what's left over the top, then continue */
										char *oldptr;

										oldptr = process_buf + strlen(process_buf);
										if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)) {
											CB_ADD(&comment_buffer, ";");
											CB_ADD_LEN(&comment_buffer, oldptr+1, new_buf-oldptr-1);
										}

										memmove(oldptr, new_buf, strlen(new_buf) + 1);
										new_buf = oldptr;
									} else {
										process_buf = new_buf;
									}
								}
							} else {
								if (!comment) {
									/* If ; is found, and we are not nested in a comment,
									   we immediately stop all comment processing */
									if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)) {
										CB_ADD(&lline_buffer, comment_p);
									}
									*comment_p = '\0';
									new_buf = comment_p;
								} else {
									new_buf = comment_p + 1;
								}
							}
						}
						if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment && !process_buf ) {
							CB_ADD(&comment_buffer, buf); /* the whole line is a comment, store it */
						}

						if (process_buf) {
							char *buffer = ast_strip(process_buf);

							if (!ast_strlen_zero(buffer)) {
								if (process_text_line(cfg, &cat, buffer, lineno, fn,
									flags, comment_buffer, lline_buffer,
									suggested_include_file, &last_cat, &last_var,
									who_asked)) {
									cfg = CONFIG_STATUS_FILEINVALID;
									break;
								}
							}
						}
					}
				}
				/* end of file-- anything in a comment buffer? */
				if (last_cat) {
					if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment_buffer && ast_str_strlen(comment_buffer)) {
						if (lline_buffer && ast_str_strlen(lline_buffer)) {
							CB_ADD(&comment_buffer, ast_str_buffer(lline_buffer)); /* add the current lline buffer to the comment buffer */
							ast_str_reset(lline_buffer); /* erase the lline buffer */
						}
						last_cat->trailing = ALLOC_COMMENT(comment_buffer);
					}
				} else if (last_var) {
					if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment_buffer && ast_str_strlen(comment_buffer)) {
						if (lline_buffer && ast_str_strlen(lline_buffer)) {
							CB_ADD(&comment_buffer, ast_str_buffer(lline_buffer)); /* add the current lline buffer to the comment buffer */
							ast_str_reset(lline_buffer); /* erase the lline buffer */
						}
						last_var->trailing = ALLOC_COMMENT(comment_buffer);
					}
				} else {
					if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment_buffer && ast_str_strlen(comment_buffer)) {
						ast_debug(1, "Nothing to attach comments to, discarded: %s\n", ast_str_buffer(comment_buffer));
					}
				}
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)) {
					CB_RESET(comment_buffer, lline_buffer);
				}

				fclose(f);
			} while (0);
			if (comment) {
				ast_log(LOG_WARNING,"Unterminated comment detected beginning on line %d\n", nest[comment - 1]);
			}
			if (cfg == NULL || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
				break;
			}
		}
		globfree(&globbuf);
	}

	ast_free(comment_buffer);
	ast_free(lline_buffer);

	if (count == 0) {
		return NULL;
	}

	return cfg;
}


/* NOTE: categories and variables each have a file and lineno attribute. On a save operation, these are used to determine
   which file and line number to write out to. Thus, an entire hierarchy of config files (via #include statements) can be
   recreated. BUT, care must be taken to make sure that every cat and var has the proper file name stored, or you may
   be shocked and mystified as to why things are not showing up in the files!

   Also, All #include/#exec statements are recorded in the "includes" LL in the ast_config structure. The file name
   and line number are stored for each include, plus the name of the file included, so that these statements may be
   included in the output files on a file_save operation.

   The lineno's are really just for relative placement in the file. There is no attempt to make sure that blank lines
   are included to keep the lineno's the same between input and output. The lineno fields are used mainly to determine
   the position of the #include and #exec directives. So, blank lines tend to disappear from a read/rewrite operation,
   and a header gets added.

   vars and category heads are output in the order they are stored in the config file. So, if the software
   shuffles these at all, then the placement of #include directives might get a little mixed up, because the
   file/lineno data probably won't get changed.

*/

static void gen_header(FILE *f1, const char *configfile, const char *fn, const char *generator)
{
	char date[256]="";
	time_t t;

	time(&t);
	ast_copy_string(date, ctime(&t), sizeof(date));

	fprintf(f1, ";!\n");
	fprintf(f1, ";! Automatically generated configuration file\n");
	if (strcmp(configfile, fn))
		fprintf(f1, ";! Filename: %s (%s)\n", configfile, fn);
	else
		fprintf(f1, ";! Filename: %s\n", configfile);
	fprintf(f1, ";! Generator: %s\n", generator);
	fprintf(f1, ";! Creation Date: %s", date);
	fprintf(f1, ";!\n");
}

static void inclfile_destroy(void *obj)
{
	const struct inclfile *o = obj;

	ast_free(o->fname);
}

static void make_fn(char *fn, size_t fn_size, const char *file, const char *configfile)
{
	if (ast_strlen_zero(file)) {
		if (configfile[0] == '/') {
			ast_copy_string(fn, configfile, fn_size);
		} else {
			snprintf(fn, fn_size, "%s/%s", ast_config_AST_CONFIG_DIR, configfile);
		}
	} else if (file[0] == '/') {
		ast_copy_string(fn, file, fn_size);
	} else {
		snprintf(fn, fn_size, "%s/%s", ast_config_AST_CONFIG_DIR, file);
	}
}

static struct inclfile *set_fn(char *fn, size_t fn_size, const char *file, const char *configfile, struct ao2_container *fileset)
{
	struct inclfile lookup;
	struct inclfile *fi;

	make_fn(fn, fn_size, file, configfile);
	lookup.fname = fn;
	fi = ao2_find(fileset, &lookup, OBJ_POINTER);
	if (fi) {
		/* Found existing include file scratch pad. */
		return fi;
	}

	/* set up a file scratch pad */
	fi = ao2_alloc(sizeof(struct inclfile), inclfile_destroy);
	if (!fi) {
		/* Scratch pad creation failed. */
		return NULL;
	}
	fi->fname = ast_strdup(fn);
	if (!fi->fname) {
		/* Scratch pad creation failed. */
		ao2_ref(fi, -1);
		return NULL;
	}
	fi->lineno = 1;

	ao2_link(fileset, fi);

	return fi;
}

static int count_linefeeds(char *str)
{
	int count = 0;

	while (*str) {
		if (*str =='\n')
			count++;
		str++;
	}
	return count;
}

static int count_linefeeds_in_comments(struct ast_comment *x)
{
	int count = 0;

	while (x) {
		count += count_linefeeds(x->cmt);
		x = x->next;
	}
	return count;
}

static void insert_leading_blank_lines(FILE *fp, struct inclfile *fi, struct ast_comment *precomments, int lineno)
{
	int precomment_lines;
	int i;

	if (!fi) {
		/* No file scratch pad object so insert no blank lines. */
		return;
	}

	precomment_lines = count_linefeeds_in_comments(precomments);

	/* I don't have to worry about those ;! comments, they are
	   stored in the precomments, but not printed back out.
	   I did have to make sure that comments following
	   the ;! header comments were not also deleted in the process */
	if (lineno - precomment_lines - fi->lineno < 0) { /* insertions can mess up the line numbering and produce negative numbers that mess things up */
		return;
	} else if (lineno == 0) {
		/* Line replacements also mess things up */
		return;
	} else if (lineno - precomment_lines - fi->lineno < 5) {
		/* Only insert less than 5 blank lines; if anything more occurs,
		 * it's probably due to context deletion. */
		for (i = fi->lineno; i < lineno - precomment_lines; i++) {
			fprintf(fp, "\n");
		}
	} else {
		/* Deletion occurred - insert a single blank line, for separation of
		 * contexts. */
		fprintf(fp, "\n");
	}

	fi->lineno = lineno + 1; /* Advance the file lineno */
}

int ast_config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator)
{
	return ast_config_text_file_save2(configfile, cfg, generator, CONFIG_SAVE_FLAG_PRESERVE_EFFECTIVE_CONTEXT);
}

static int is_writable(const char *fn)
{
	if (access(fn, F_OK)) {
		char *dn = dirname(ast_strdupa(fn));

		if (access(dn, R_OK | W_OK)) {
			ast_log(LOG_ERROR, "Unable to write to directory %s (%s)\n", dn, strerror(errno));
			return 0;
		}
	} else {
		if (access(fn, R_OK | W_OK)) {
			ast_log(LOG_ERROR, "Unable to write %s (%s)\n", fn, strerror(errno));
			return 0;
		}
	}

	return 1;
}

int ast_config_text_file_save2(const char *configfile, const struct ast_config *cfg, const char *generator, uint32_t flags)
{
	FILE *f;
	char fn[PATH_MAX];
	struct ast_variable *var;
	struct ast_category *cat;
	struct ast_comment *cmt;
	struct ast_config_include *incl;
	int blanklines = 0;
	struct ao2_container *fileset;
	struct inclfile *fi;

	fileset = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 1023,
		hash_string, NULL, hashtab_compare_strings);
	if (!fileset) {
		/* Container creation failed. */
		return -1;
	}

	/* Check all the files for write access before attempting to modify any of them */
	for (incl = cfg->includes; incl; incl = incl->next) {
		/* reset all the output flags in case this isn't our first time saving this data */
		incl->output = 0;

		if (!incl->exec) {
			/* now make sure we have write access to the include file or its parent directory */
			make_fn(fn, sizeof(fn), incl->included_file, configfile);
			/* If the file itself doesn't exist, make sure we have write access to the directory */
			if (!is_writable(fn)) {
				return -1;
			}
		}
	}

	/* now make sure we have write access to the main config file or its parent directory */
	make_fn(fn, sizeof(fn), 0, configfile);
	if (!is_writable(fn)) {
		return -1;
	}

	/* Now that we know we have write access to all files, it's safe to start truncating them */

	/* go thru all the inclusions and make sure all the files involved (configfile plus all its inclusions)
	   are all truncated to zero bytes and have that nice header*/
	for (incl = cfg->includes; incl; incl = incl->next) {
		if (!incl->exec) { /* leave the execs alone -- we'll write out the #exec directives, but won't zero out the include files or exec files*/
			/* normally, fn is just set to incl->included_file, prepended with config dir if relative */
			fi = set_fn(fn, sizeof(fn), incl->included_file, configfile, fileset);
			f = fopen(fn, "w");
			if (f) {
				gen_header(f, configfile, fn, generator);
				fclose(f); /* this should zero out the file */
			} else {
				ast_log(LOG_ERROR, "Unable to write %s (%s)\n", fn, strerror(errno));
			}
			if (fi) {
				ao2_ref(fi, -1);
			}
		}
	}

	/* just set fn to absolute ver of configfile */
	fi = set_fn(fn, sizeof(fn), 0, configfile, fileset);
	if (
#ifdef __CYGWIN__
		(f = fopen(fn, "w+"))
#else
		(f = fopen(fn, "w"))
#endif
		) {
		ast_verb(2, "Saving '%s'\n", fn);
		gen_header(f, configfile, fn, generator);
		cat = cfg->root;
		fclose(f);
		if (fi) {
			ao2_ref(fi, -1);
		}

		/* from here out, we open each involved file and concat the stuff we need to add to the end and immediately close... */
		/* since each var, cat, and associated comments can come from any file, we have to be
		   mobile, and open each file, print, and close it on an entry-by-entry basis */

		while (cat) {
			fi = set_fn(fn, sizeof(fn), cat->file, configfile, fileset);
			f = fopen(fn, "a");
			if (!f) {
				ast_log(LOG_ERROR, "Unable to write %s (%s)\n", fn, strerror(errno));
				if (fi) {
					ao2_ref(fi, -1);
				}
				ao2_ref(fileset, -1);
				return -1;
			}

			/* dump any includes that happen before this category header */
			for (incl=cfg->includes; incl; incl = incl->next) {
				if (strcmp(incl->include_location_file, cat->file) == 0){
					if (cat->lineno > incl->include_location_lineno && !incl->output) {
						if (incl->exec)
							fprintf(f,"#exec \"%s\"\n", incl->exec_file);
						else
							fprintf(f,"#include \"%s\"\n", incl->included_file);
						incl->output = 1;
					}
				}
			}

			insert_leading_blank_lines(f, fi, cat->precomments, cat->lineno);
			/* Dump section with any appropriate comment */
			for (cmt = cat->precomments; cmt; cmt=cmt->next) {
				char *cmtp = cmt->cmt;
				while (cmtp && *cmtp == ';' && *(cmtp+1) == '!') {
					char *cmtp2 = strchr(cmtp+1, '\n');
					if (cmtp2)
						cmtp = cmtp2+1;
					else cmtp = 0;
				}
				if (cmtp)
					fprintf(f,"%s", cmtp);
			}
			fprintf(f, "[%s]", cat->name);
			if (cat->ignored || !AST_LIST_EMPTY(&cat->template_instances)) {
				fprintf(f, "(");
				if (cat->ignored) {
					fprintf(f, "!");
				}
				if (cat->ignored && !AST_LIST_EMPTY(&cat->template_instances)) {
					fprintf(f, ",");
				}
				if (!AST_LIST_EMPTY(&cat->template_instances)) {
					struct ast_category_template_instance *x;
					AST_LIST_TRAVERSE(&cat->template_instances, x, next) {
						fprintf(f,"%s",x->name);
						if (x != AST_LIST_LAST(&cat->template_instances))
							fprintf(f,",");
					}
				}
				fprintf(f, ")");
			}
			for(cmt = cat->sameline; cmt; cmt=cmt->next)
			{
				fprintf(f,"%s", cmt->cmt);
			}
			if (!cat->sameline)
				fprintf(f,"\n");
			for (cmt = cat->trailing; cmt; cmt=cmt->next) {
				if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
					fprintf(f,"%s", cmt->cmt);
			}
			fclose(f);
			if (fi) {
				ao2_ref(fi, -1);
			}

			var = cat->root;
			while (var) {
				struct ast_category_template_instance *x;
				int found = 0;

				AST_LIST_TRAVERSE(&cat->template_instances, x, next) {
					struct ast_variable *v;
					for (v = x->inst->root; v; v = v->next) {

						if (flags & CONFIG_SAVE_FLAG_PRESERVE_EFFECTIVE_CONTEXT) {
							if (!strcasecmp(var->name, v->name) && !strcmp(var->value, v->value)) {
								found = 1;
								break;
							}
						} else {
							if (var->inherited) {
								found = 1;
								break;
							} else {
								if (!strcasecmp(var->name, v->name) && !strcmp(var->value, v->value)) {
									found = 1;
									break;
								}
							}
						}
					}
					if (found) {
						break;
					}
				}
				if (found) {
					var = var->next;
					continue;
				}
				fi = set_fn(fn, sizeof(fn), var->file, configfile, fileset);
				f = fopen(fn, "a");
				if (!f) {
					ast_debug(1, "Unable to open for writing: %s\n", fn);
					ast_verb(2, "Unable to write %s (%s)\n", fn, strerror(errno));
					if (fi) {
						ao2_ref(fi, -1);
					}
					ao2_ref(fileset, -1);
					return -1;
				}

				/* dump any includes that happen before this category header */
				for (incl=cfg->includes; incl; incl = incl->next) {
					if (strcmp(incl->include_location_file, var->file) == 0){
						if (var->lineno > incl->include_location_lineno && !incl->output) {
							if (incl->exec)
								fprintf(f,"#exec \"%s\"\n", incl->exec_file);
							else
								fprintf(f,"#include \"%s\"\n", incl->included_file);
							incl->output = 1;
						}
					}
				}

				insert_leading_blank_lines(f, fi, var->precomments, var->lineno);
				for (cmt = var->precomments; cmt; cmt=cmt->next) {
					if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
						fprintf(f,"%s", cmt->cmt);
				}

				{ /* Block for 'escaped' scope */
					int escaped_len = 2 * strlen(var->value) + 1;
					char escaped[escaped_len];

					ast_escape_semicolons(var->value, escaped, escaped_len);

					if (var->sameline) {
						fprintf(f, "%s %s %s  %s", var->name, (var->object ? "=>" : "="),
							escaped, var->sameline->cmt);
					} else {
						fprintf(f, "%s %s %s\n", var->name, (var->object ? "=>" : "="),
							escaped);
					}
				}

				for (cmt = var->trailing; cmt; cmt=cmt->next) {
					if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
						fprintf(f,"%s", cmt->cmt);
				}
				if (var->blanklines) {
					blanklines = var->blanklines;
					while (blanklines--)
						fprintf(f, "\n");
				}

				fclose(f);
				if (fi) {
					ao2_ref(fi, -1);
				}

				var = var->next;
			}
			cat = cat->next;
		}
		ast_verb(2, "Saving '%s': saved\n", fn);
	} else {
		ast_debug(1, "Unable to open for writing: %s\n", fn);
		ast_verb(2, "Unable to write '%s' (%s)\n", fn, strerror(errno));
		if (fi) {
			ao2_ref(fi, -1);
		}
		ao2_ref(fileset, -1);
		return -1;
	}

	/* Now, for files with trailing #include/#exec statements,
	   we have to make sure every entry is output */
	for (incl=cfg->includes; incl; incl = incl->next) {
		if (!incl->output) {
			/* open the respective file */
			fi = set_fn(fn, sizeof(fn), incl->include_location_file, configfile, fileset);
			f = fopen(fn, "a");
			if (!f) {
				ast_debug(1, "Unable to open for writing: %s\n", fn);
				ast_verb(2, "Unable to write %s (%s)\n", fn, strerror(errno));
				if (fi) {
					ao2_ref(fi, -1);
				}
				ao2_ref(fileset, -1);
				return -1;
			}

			/* output the respective include */
			if (incl->exec)
				fprintf(f,"#exec \"%s\"\n", incl->exec_file);
			else
				fprintf(f,"#include \"%s\"\n", incl->included_file);
			fclose(f);
			incl->output = 1;
			if (fi) {
				ao2_ref(fi, -1);
			}
		}
	}
	ao2_ref(fileset, -1); /* this should destroy the hash container */

	/* pass new configuration to any config hooks */
	config_hook_exec(configfile, generator, cfg);

	return 0;
}

static void clear_config_maps(void)
{
	struct ast_config_map *map;

	while (config_maps) {
		map = config_maps;
		config_maps = config_maps->next;
		ast_free(map);
	}
}

#ifdef TEST_FRAMEWORK
int ast_realtime_append_mapping(const char *name, const char *driver, const char *database, const char *table, int priority)
#else
static int ast_realtime_append_mapping(const char *name, const char *driver, const char *database, const char *table, int priority)
#endif
{
	struct ast_config_map *map;
	char *dst;
	int length;

	length = sizeof(*map);
	length += strlen(name) + 1;
	length += strlen(driver) + 1;
	length += strlen(database) + 1;
	if (table)
		length += strlen(table) + 1;

	if (!(map = ast_calloc(1, length)))
		return -1;

	dst = map->stuff;	/* writable space starts here */
	map->name = strcpy(dst, name);
	dst += strlen(dst) + 1;
	map->driver = strcpy(dst, driver);
	dst += strlen(dst) + 1;
	map->database = strcpy(dst, database);
	if (table) {
		dst += strlen(dst) + 1;
		map->table = strcpy(dst, table);
	}
	map->priority = priority;
	map->next = config_maps;
	config_maps = map;

	ast_verb(2, "Binding %s to %s/%s/%s\n", map->name, map->driver, map->database, map->table ? map->table : map->name);

	return 0;
}

static int reload_module(void)
{
	struct ast_config *config, *configtmp;
	struct ast_variable *v;
	char *driver, *table, *database, *textpri, *stringp, *tmp;
	struct ast_flags flags = { CONFIG_FLAG_NOREALTIME };
	int pri;
	SCOPED_MUTEX(lock, &config_lock);

	clear_config_maps();

	configtmp = ast_config_new();
	if (!configtmp) {
		ast_log(LOG_ERROR, "Unable to allocate memory for new config\n");
		return -1;
	}
	config = ast_config_internal_load(extconfig_conf, configtmp, flags, "", "extconfig");
	if (config == CONFIG_STATUS_FILEINVALID) {
		return -1;
	} else if (!config) {
		ast_config_destroy(configtmp);
		return 0;
	}

	for (v = ast_variable_browse(config, "settings"); v; v = v->next) {
		char buf[512];
		ast_copy_string(buf, v->value, sizeof(buf));
		stringp = buf;
		driver = strsep(&stringp, ",");
		if (!stringp) {
			ast_log(LOG_WARNING, "extconfig.conf: value '%s' ignored due to wrong format\n", v->value);
			continue;
		}
		if ((tmp = strchr(stringp, '\"')))
			stringp = tmp;

		/* check if the database text starts with a double quote */
		if (*stringp == '"') {
			stringp++;
			database = strsep(&stringp, "\"");
			strsep(&stringp, ",");
		} else {
			/* apparently this text has no quotes */
			database = strsep(&stringp, ",");
		}

		table = strsep(&stringp, ",");
		textpri = strsep(&stringp, ",");
		if (!textpri || !(pri = atoi(textpri))) {
			pri = 1;
		}

		if (!strcmp(v->name, extconfig_conf)) {
			ast_log(LOG_WARNING, "Cannot bind '%s'!\n", extconfig_conf);
			continue;
		}

		if (!strcmp(v->name, "asterisk.conf")) {
			ast_log(LOG_WARNING, "Cannot bind 'asterisk.conf'!\n");
			continue;
		}

		if (!strcmp(v->name, "logger.conf")) {
			ast_log(LOG_WARNING, "Cannot bind 'logger.conf'!\n");
			continue;
		}

		if (!driver || !database)
			continue;
		if (!strcasecmp(v->name, "sipfriends")) {
			ast_log(LOG_WARNING, "The 'sipfriends' table is obsolete, update your config to use sippeers instead.\n");
			ast_realtime_append_mapping("sippeers", driver, database, table ? table : "sipfriends", pri);
		} else if (!strcasecmp(v->name, "iaxfriends")) {
			ast_log(LOG_WARNING, "The 'iaxfriends' table is obsolete, update your config to use iaxusers and iaxpeers, though they can point to the same table.\n");
			ast_realtime_append_mapping("iaxusers", driver, database, table ? table : "iaxfriends", pri);
			ast_realtime_append_mapping("iaxpeers", driver, database, table ? table : "iaxfriends", pri);
		} else
			ast_realtime_append_mapping(v->name, driver, database, table, pri);
	}

	ast_config_destroy(config);
	return 0;
}

int ast_config_engine_register(struct ast_config_engine *new)
{
	struct ast_config_engine *ptr;

	SCOPED_MUTEX(lock, &config_lock);

	if (!config_engine_list) {
		config_engine_list = new;
	} else {
		for (ptr = config_engine_list; ptr->next; ptr=ptr->next);
		ptr->next = new;
	}

	return 1;
}

int ast_config_engine_deregister(struct ast_config_engine *del)
{
	struct ast_config_engine *ptr, *last=NULL;

	SCOPED_MUTEX(lock, &config_lock);

	for (ptr = config_engine_list; ptr; ptr=ptr->next) {
		if (ptr == del) {
			if (last)
				last->next = ptr->next;
			else
				config_engine_list = ptr->next;
			break;
		}
		last = ptr;
	}

	return 0;
}

int ast_realtime_is_mapping_defined(const char *family)
{
	struct ast_config_map *map;
	SCOPED_MUTEX(lock, &config_lock);

	for (map = config_maps; map; map = map->next) {
		if (!strcasecmp(family, map->name)) {
			return 1;
		}
	}
	ast_debug(5, "Failed to find a realtime mapping for %s\n", family);

	return 0;
}

/*! \brief Find realtime engine for realtime family */
static struct ast_config_engine *find_engine(const char *family, int priority, char *database, int dbsiz, char *table, int tabsiz)
{
	struct ast_config_engine *eng, *ret = NULL;
	struct ast_config_map *map;

	SCOPED_MUTEX(lock, &config_lock);

	for (map = config_maps; map; map = map->next) {
		if (!strcasecmp(family, map->name) && (priority == map->priority)) {
			if (database)
				ast_copy_string(database, map->database, dbsiz);
			if (table)
				ast_copy_string(table, map->table ? map->table : family, tabsiz);
			break;
		}
	}

	/* Check if the required driver (engine) exist */
	if (map) {
		for (eng = config_engine_list; !ret && eng; eng = eng->next) {
			if (!strcasecmp(eng->name, map->driver))
				ret = eng;
		}
	}

	/* if we found a mapping, but the engine is not available, then issue a warning */
	if (map && !ret)
		ast_log(LOG_WARNING, "Realtime mapping for '%s' found to engine '%s', but the engine is not available\n", map->name, map->driver);

	return ret;
}

static struct ast_config_engine text_file_engine = {
	.name = "text",
	.load_func = config_text_file_load,
};

struct ast_config *ast_config_copy(const struct ast_config *old)
{
	struct ast_config *new_config = ast_config_new();
	struct ast_category *cat_iter;

	if (!new_config) {
		return NULL;
	}

	for (cat_iter = old->root; cat_iter; cat_iter = cat_iter->next) {
		struct ast_category *new_cat =
			ast_category_new(cat_iter->name, cat_iter->file, cat_iter->lineno);
		if (!new_cat) {
			goto fail;
		}
		ast_category_append(new_config, new_cat);
		if (cat_iter->root) {
			new_cat->root = ast_variables_dup(cat_iter->root);
			if (!new_cat->root) {
				goto fail;
			}
			new_cat->last = cat_iter->last;
		}
	}

	return new_config;

fail:
	ast_config_destroy(new_config);
	return NULL;
}


struct ast_config *ast_config_internal_load(const char *filename, struct ast_config *cfg, struct ast_flags flags, const char *suggested_include_file, const char *who_asked)
{
	char db[256];
	char table[256];
	struct ast_config_engine *loader = &text_file_engine;
	struct ast_config *result;

	/* The config file itself bumps include_level by 1 */
	if (cfg->max_include_level > 0 && cfg->include_level == cfg->max_include_level + 1) {
		ast_log(LOG_WARNING, "Maximum Include level (%d) exceeded\n", cfg->max_include_level);
		return NULL;
	}

	cfg->include_level++;

	if (!ast_test_flag(&flags, CONFIG_FLAG_NOREALTIME) && config_engine_list) {
		struct ast_config_engine *eng;

		eng = find_engine(filename, 1, db, sizeof(db), table, sizeof(table));


		if (eng && eng->load_func) {
			loader = eng;
		} else {
			eng = find_engine("global", 1, db, sizeof(db), table, sizeof(table));
			if (eng && eng->load_func)
				loader = eng;
		}
	}

	result = loader->load_func(db, table, filename, cfg, flags, suggested_include_file, who_asked);

	if (result && result != CONFIG_STATUS_FILEINVALID && result != CONFIG_STATUS_FILEUNCHANGED) {
		result->include_level--;
		config_hook_exec(filename, who_asked, result);
	} else if (result != CONFIG_STATUS_FILEINVALID) {
		cfg->include_level--;
	}

	return result;
}

struct ast_config *ast_config_load2(const char *filename, const char *who_asked, struct ast_flags flags)
{
	struct ast_config *cfg;
	struct ast_config *result;

	cfg = ast_config_new();
	if (!cfg)
		return NULL;

	result = ast_config_internal_load(filename, cfg, flags, "", who_asked);
	if (!result || result == CONFIG_STATUS_FILEUNCHANGED || result == CONFIG_STATUS_FILEINVALID)
		ast_config_destroy(cfg);

	return result;
}

#define realtime_arguments_to_fields(ap, result) realtime_arguments_to_fields2(ap, 0, result)

/*!
 * \internal
 * \brief
 *
 * \param ap list of variable arguments
 * \param skip Skip argument pairs for this number of variables
 * \param result Address of a variables pointer to store the results
 *               May be NULL if no arguments are parsed
 *               Will be NULL on failure.
 *
 * \retval 0 on success or empty ap list
 * \retval -1 on failure
 */
static int realtime_arguments_to_fields2(va_list ap, int skip, struct ast_variable **result)
{
	struct ast_variable *first, *fields = NULL;
	const char *newparam;
	const char *newval;

	/*
	 * Previously we would do:
	 *
	 *     va_start(ap, last);
	 *     x = realtime_arguments_to_fields(ap);
	 *     y = realtime_arguments_to_fields(ap);
	 *     va_end(ap);
	 *
	 * While this works on generic amd64 machines (2014), it doesn't on the
	 * raspberry PI. The va_arg() manpage says:
	 *
	 *     If ap is passed to a function that uses va_arg(ap,type) then
	 *     the value of ap is undefined after the return of that function.
	 *
	 * On the raspberry, ap seems to get reset after the call: the contents
	 * of y would be equal to the contents of x.
	 *
	 * So, instead we allow the caller to skip past earlier argument sets
	 * using the skip parameter:
	 *
	 *     va_start(ap, last);
	 *     if (realtime_arguments_to_fields(ap, &x)) {
	 *         // FAILURE CONDITIONS
	 *     }
	 *     va_end(ap);
	 *     va_start(ap, last);
	 *     if (realtime_arguments_to_fields2(ap, 1, &y)) {
	 *         // FAILURE CONDITIONS
	 *     }
	 *     va_end(ap);
	 */
	while (skip--) {
		/* There must be at least one argument. */
		newparam = va_arg(ap, const char *);
		newval = va_arg(ap, const char *);
		while ((newparam = va_arg(ap, const char *))) {
			newval = va_arg(ap, const char *);
		}
	}

	/* Load up the first vars. */
	newparam = va_arg(ap, const char *);
	if (!newparam) {
		*result = NULL;
		return 0;
	}
	newval = va_arg(ap, const char *);

	if (!(first = ast_variable_new(newparam, newval, ""))) {
		*result = NULL;
		return -1;
	}

	while ((newparam = va_arg(ap, const char *))) {
		struct ast_variable *field;

		newval = va_arg(ap, const char *);
		if (!(field = ast_variable_new(newparam, newval, ""))) {
			ast_variables_destroy(fields);
			ast_variables_destroy(first);
			*result = NULL;
			return -1;
		}

		field->next = fields;
		fields = field;
	}

	first->next = fields;
	fields = first;

	*result = fields;
	return 0;
}

struct ast_variable *ast_load_realtime_all_fields(const char *family, const struct ast_variable *fields)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	struct ast_variable *res=NULL;
	int i;

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->realtime_func && (res = eng->realtime_func(db, table, fields))) {
				return res;
			}
		} else {
			return NULL;
		}
	}

	return res;
}

struct ast_variable *ast_load_realtime_all(const char *family, ...)
{
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);
	struct ast_variable *res = NULL;
	va_list ap;

	va_start(ap, family);
	realtime_arguments_to_fields(ap, &fields);
	va_end(ap);

	if (fields) {
		res = ast_load_realtime_all_fields(family, fields);
	}

	return res;
}

struct ast_variable *ast_load_realtime_fields(const char *family, const struct ast_variable *fields)
{
	struct ast_variable *res;
	struct ast_variable *cur;
	struct ast_variable **prev;

	res = ast_load_realtime_all_fields(family, fields);

	/* Filter the list. */
	prev = &res;
	cur = res;
	while (cur) {
		if (ast_strlen_zero(cur->value)) {
			/* Eliminate empty entries */
			struct ast_variable *next;

			next = cur->next;
			*prev = next;
			ast_variable_destroy(cur);
			cur = next;
		} else {
			/* Make blank entries empty and keep them. */
			if (cur->value[0] == ' ' && cur->value[1] == '\0') {
				char *vptr = (char *) cur->value;

				vptr[0] = '\0';
			}

			prev = &cur->next;
			cur = cur->next;
		}
	}
	return res;
}

struct ast_variable *ast_load_realtime(const char *family, ...)
{
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);
	int field_res = 0;
	va_list ap;

	va_start(ap, family);
	if (realtime_arguments_to_fields(ap, &fields)) {
		field_res = -1;
	}
	va_end(ap);

	if (field_res) {
		return NULL;
	}

	if (!fields) {
		return NULL;
	}

	return ast_load_realtime_fields(family, fields);
}

/*! \brief Check if realtime engine is configured for family */
int ast_check_realtime(const char *family)
{
	struct ast_config_engine *eng;
	if (!ast_realtime_enabled()) {
		return 0;	/* There are no engines at all so fail early */
	}

	eng = find_engine(family, 1, NULL, 0, NULL, 0);
	if (eng)
		return 1;
	return 0;
}

/*! \brief Check if there's any realtime engines loaded */
int ast_realtime_enabled(void)
{
	return config_maps ? 1 : 0;
}

int ast_realtime_require_field(const char *family, ...)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	va_list ap, aq;
	int res = -1, i;

	va_start(ap, family);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			va_copy(aq, ap);
			/* If the require succeeds, it returns 0. */
			if (eng->require_func && !(res = eng->require_func(db, table, aq))) {
				va_end(aq);
				break;
			}
			va_end(aq);
		} else {
			break;
		}
	}
	va_end(ap);

	return res;
}

int ast_unload_realtime(const char *family)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	int res = -1, i;

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->unload_func) {
				/* Do this for ALL engines */
				res = eng->unload_func(db, table);
			}
		} else {
			break;
		}
	}
	return res;
}

struct ast_config *ast_load_realtime_multientry_fields(const char *family, const struct ast_variable *fields)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	struct ast_config *res = NULL;
	int i;

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->realtime_multi_func && (res = eng->realtime_multi_func(db, table, fields))) {
				/* If we were returned an empty cfg, destroy it and return NULL */
				if (!res->root) {
					ast_config_destroy(res);
					res = NULL;
				}
				break;
			}
		} else {
			break;
		}
	}

	return res;
}

struct ast_config *ast_load_realtime_multientry(const char *family, ...)
{
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);
	va_list ap;

	va_start(ap, family);
	realtime_arguments_to_fields(ap, &fields);
	va_end(ap);

	if (!fields) {
		return NULL;
	}

	return ast_load_realtime_multientry_fields(family, fields);
}

int ast_update_realtime_fields(const char *family, const char *keyfield, const char *lookup, const struct ast_variable *fields)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			/* If the update succeeds, it returns >= 0. */
			if (eng->update_func && ((res = eng->update_func(db, table, keyfield, lookup, fields)) >= 0)) {
				break;
			}
		} else {
			break;
		}
	}

	return res;
}

int ast_update_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);
	va_list ap;

	va_start(ap, lookup);
	realtime_arguments_to_fields(ap, &fields);
	va_end(ap);

	if (!fields) {
		return -1;
	}

	return ast_update_realtime_fields(family, keyfield, lookup, fields);
}

int ast_update2_realtime_fields(const char *family, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->update2_func && !(res = eng->update2_func(db, table, lookup_fields, update_fields))) {
				break;
			}
		} else {
			break;
		}
	}

	return res;
}

int ast_update2_realtime(const char *family, ...)
{
	RAII_VAR(struct ast_variable *, lookup_fields, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, update_fields, NULL, ast_variables_destroy);
	va_list ap;

	va_start(ap, family);
	/* XXX: If we wanted to pass no lookup fields (select all), we'd be
	 * out of luck. realtime_arguments_to_fields expects at least one key
	 * value pair. */
	realtime_arguments_to_fields(ap, &lookup_fields);
	va_end(ap);

	va_start(ap, family);
	realtime_arguments_to_fields2(ap, 1, &update_fields);
	va_end(ap);

	if (!lookup_fields || !update_fields) {
		return -1;
	}

	return ast_update2_realtime_fields(family, lookup_fields, update_fields);
}

int ast_store_realtime_fields(const char *family, const struct ast_variable *fields)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			/* If the store succeeds, it returns >= 0*/
			if (eng->store_func && ((res = eng->store_func(db, table, fields)) >= 0)) {
				break;
			}
		} else {
			break;
		}
	}

	return res;
}

int ast_store_realtime(const char *family, ...)
{
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);
	va_list ap;

	va_start(ap, family);
	realtime_arguments_to_fields(ap, &fields);
	va_end(ap);

	if (!fields) {
		return -1;
	}

	return ast_store_realtime_fields(family, fields);
}

int ast_destroy_realtime_fields(const char *family, const char *keyfield, const char *lookup, const struct ast_variable *fields)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->destroy_func && ((res = eng->destroy_func(db, table, keyfield, lookup, fields)) >= 0)) {
				break;
			}
		} else {
			break;
		}
	}

	return res;
}

int ast_destroy_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);
	int res = 0;
	va_list ap;

	va_start(ap, lookup);
	if (realtime_arguments_to_fields(ap, &fields)) {
		res = -1;
	}
	va_end(ap);

	if (res) {
		return -1;
	}

	return ast_destroy_realtime_fields(family, keyfield, lookup, fields);
}

char *ast_realtime_decode_chunk(char *chunk)
{
	char *orig = chunk;
	for (; *chunk; chunk++) {
		if (*chunk == '^' && strchr("0123456789ABCDEFabcdef", chunk[1]) && strchr("0123456789ABCDEFabcdef", chunk[2])) {
			sscanf(chunk + 1, "%02hhX", (unsigned char *)chunk);
			memmove(chunk + 1, chunk + 3, strlen(chunk + 3) + 1);
		}
	}
	return orig;
}

char *ast_realtime_encode_chunk(struct ast_str **dest, ssize_t maxlen, const char *chunk)
{
	if (!strchr(chunk, ';') && !strchr(chunk, '^')) {
		ast_str_set(dest, maxlen, "%s", chunk);
	} else {
		ast_str_reset(*dest);
		for (; *chunk; chunk++) {
			if (strchr(";^", *chunk)) {
				ast_str_append(dest, maxlen, "^%02hhX", *chunk);
			} else {
				ast_str_append(dest, maxlen, "%c", *chunk);
			}
		}
	}
	return ast_str_buffer(*dest);
}

/*! \brief Helper function to parse arguments
 * See documentation in config.h
 */
int ast_parse_arg(const char *arg, enum ast_parse_flags flags,
	void *p_result, ...)
{
	va_list ap;
	int error = 0;

	va_start(ap, p_result);
	switch (flags & PARSE_TYPE) {
	case PARSE_INT32:
	{
		long int x = 0;
		int32_t *result = p_result;
		int32_t def = result ? *result : 0, high = INT32_MAX, low = INT32_MIN;
		char *endptr = NULL;

		/* optional arguments: default value and/or (low, high) */
		if (flags & PARSE_DEFAULT) {
			def = va_arg(ap, int32_t);
		}
		if (flags & (PARSE_IN_RANGE | PARSE_OUT_RANGE)) {
			low = va_arg(ap, int32_t);
			high = va_arg(ap, int32_t);
		}
		if (ast_strlen_zero(arg)) {
			error = 1;
			goto int32_done;
		}
		errno = 0;
		x = strtol(arg, &endptr, 0);
		if (*endptr || errno || x < INT32_MIN || x > INT32_MAX) {
			/* Parse error, or type out of int32_t bounds */
			error = 1;
			goto int32_done;
		}
		error = (x < low) || (x > high);
		if (flags & PARSE_RANGE_DEFAULTS) {
			if (x < low) {
				def = low;
			} else if (x > high) {
				def = high;
			}
		}
		if (flags & PARSE_OUT_RANGE) {
			error = !error;
		}
int32_done:
		if (result) {
			*result  = error ? def : x;
		}

		ast_debug(3, "extract int from [%s] in [%d, %d] gives [%ld](%d)\n",
				arg, low, high, result ? *result : x, error);
		break;
	}

	case PARSE_UINT32:
	{
		unsigned long int x = 0;
		uint32_t *result = p_result;
		uint32_t def = result ? *result : 0, low = 0, high = UINT32_MAX;
		char *endptr = NULL;

		/* optional argument: first default value, then range */
		if (flags & PARSE_DEFAULT) {
			def = va_arg(ap, uint32_t);
		}
		if (flags & (PARSE_IN_RANGE|PARSE_OUT_RANGE)) {
			/* range requested, update bounds */
			low = va_arg(ap, uint32_t);
			high = va_arg(ap, uint32_t);
		}

		if (ast_strlen_zero(arg)) {
			error = 1;
			goto uint32_done;
		}
		/* strtoul will happily and silently negate negative numbers */
		arg = ast_skip_blanks(arg);
		if (*arg == '-') {
			error = 1;
			goto uint32_done;
		}
		errno = 0;
		x = strtoul(arg, &endptr, 0);
		if (*endptr || errno || x > UINT32_MAX) {
			error = 1;
			goto uint32_done;
		}
		error = (x < low) || (x > high);
		if (flags & PARSE_RANGE_DEFAULTS) {
			if (x < low) {
				def = low;
			} else if (x > high) {
				def = high;
			}
		}
		if (flags & PARSE_OUT_RANGE) {
			error = !error;
		}
uint32_done:
		if (result) {
			*result  = error ? def : x;
		}
		ast_debug(3, "extract uint from [%s] in [%u, %u] gives [%lu](%d)\n",
				arg, low, high, result ? *result : x, error);
		break;
	}

	case PARSE_TIMELEN:
	{
		int x = 0;
		int *result = p_result;
		int def = result ? *result : 0;
		int high = INT_MAX;
		int low = INT_MIN;
		enum ast_timelen defunit;

		defunit = va_arg(ap, enum ast_timelen);
		/* optional arguments: default value and/or (low, high) */
		if (flags & PARSE_DEFAULT) {
			def = va_arg(ap, int);
		}
		if (flags & (PARSE_IN_RANGE | PARSE_OUT_RANGE)) {
			low = va_arg(ap, int);
			high = va_arg(ap, int);
		}
		if (ast_strlen_zero(arg)) {
			error = 1;
			goto timelen_done;
		}
		error = ast_app_parse_timelen(arg, &x, defunit);
		if (error || x < INT_MIN || x > INT_MAX) {
			/* Parse error, or type out of int bounds */
			error = 1;
			goto timelen_done;
		}
		error = (x < low) || (x > high);
		if (flags & PARSE_RANGE_DEFAULTS) {
			if (x < low) {
				def = low;
			} else if (x > high) {
				def = high;
			}
		}
		if (flags & PARSE_OUT_RANGE) {
			error = !error;
		}
timelen_done:
		if (result) {
			*result  = error ? def : x;
		}

		ast_debug(3, "extract timelen from [%s] in [%d, %d] gives [%d](%d)\n",
				arg, low, high, result ? *result : x, error);
		break;
	}

	case PARSE_DOUBLE:
	{
		double *result = p_result;
		double x = 0, def = result ? *result : 0, low = -HUGE_VAL, high = HUGE_VAL;
		char *endptr = NULL;

		/* optional argument: first default value, then range */
		if (flags & PARSE_DEFAULT) {
			def = va_arg(ap, double);
		}
		if (flags & (PARSE_IN_RANGE | PARSE_OUT_RANGE)) {
			/* range requested, update bounds */
			low = va_arg(ap, double);
			high = va_arg(ap, double);
		}
		if (ast_strlen_zero(arg)) {
			error = 1;
			goto double_done;
		}
		errno = 0;
		x = strtod(arg, &endptr);
		if (*endptr || errno == ERANGE) {
			error = 1;
			goto double_done;
		}
		error = (x < low) || (x > high);
		if (flags & PARSE_OUT_RANGE) {
			error = !error;
		}
double_done:
		if (result) {
			*result = error ? def : x;
		}
		ast_debug(3, "extract double from [%s] in [%f, %f] gives [%f](%d)\n",
				arg, low, high, result ? *result : x, error);
		break;
	}
	case PARSE_ADDR:
	    {
		struct ast_sockaddr *addr = (struct ast_sockaddr *)p_result;

		if (!ast_sockaddr_parse(addr, arg, flags & PARSE_PORT_MASK)) {
			error = 1;
		}

		ast_debug(3, "extract addr from %s gives %s(%d)\n",
			  arg, ast_sockaddr_stringify(addr), error);

		break;
	    }
	case PARSE_INADDR:	/* TODO Remove this (use PARSE_ADDR instead). */
	    {
		char *port, *buf;
		struct sockaddr_in _sa_buf;	/* buffer for the result */
		struct sockaddr_in *sa = p_result ?
			(struct sockaddr_in *)p_result : &_sa_buf;
		/* default is either the supplied value or the result itself */
		struct sockaddr_in *def = (flags & PARSE_DEFAULT) ?
			va_arg(ap, struct sockaddr_in *) : sa;
		struct ast_sockaddr addr = { {0,} };

		memset(&_sa_buf, '\0', sizeof(_sa_buf)); /* clear buffer */
		/* duplicate the string to strip away the :port */
		port = ast_strdupa(arg);
		buf = strsep(&port, ":");
		sa->sin_family = AF_INET;	/* assign family */
		/*
		 * honor the ports flag setting, assign default value
		 * in case of errors or field unset.
		 */
		flags &= PARSE_PORT_MASK; /* the only flags left to process */
		if (port) {
			if (flags == PARSE_PORT_FORBID) {
				error = 1;	/* port was forbidden */
				sa->sin_port = def->sin_port;
			} else if (flags == PARSE_PORT_IGNORE)
				sa->sin_port = def->sin_port;
			else /* accept or require */
				sa->sin_port = htons(strtol(port, NULL, 0));
		} else {
			sa->sin_port = def->sin_port;
			if (flags == PARSE_PORT_REQUIRE)
				error = 1;
		}
		/* Now deal with host part, even if we have errors before. */
		if (ast_sockaddr_resolve_first_af(&addr, buf, PARSE_PORT_FORBID, AF_INET)) {
			error = 1;
			sa->sin_addr = def->sin_addr;
		} else {
			struct sockaddr_in tmp;
			ast_sockaddr_to_sin(&addr, &tmp);
			sa->sin_addr = tmp.sin_addr;
		}
		ast_debug(3,
			"extract inaddr from [%s] gives [%s:%d](%d)\n",
			arg, ast_inet_ntoa(sa->sin_addr),
			ntohs(sa->sin_port), error);
		break;
	    }
	}
	va_end(ap);
	return error;
}

static char *handle_cli_core_show_config_mappings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_config_engine *eng;
	struct ast_config_map *map;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show config mappings";
		e->usage =
			"Usage: core show config mappings\n"
			"	Shows the filenames to config engines.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	{
		SCOPED_MUTEX(lock, &config_lock);

		if (!config_engine_list) {
			ast_cli(a->fd, "No config mappings found.\n");
		} else {
			for (eng = config_engine_list; eng; eng = eng->next) {
				ast_cli(a->fd, "Config Engine: %s\n", eng->name);
				for (map = config_maps; map; map = map->next) {
					if (!strcasecmp(map->driver, eng->name)) {
						ast_cli(a->fd, "===> %s (db=%s, table=%s)\n", map->name, map->database,
								map->table ? map->table : map->name);
					}
				}
			}
		}
	}

	return CLI_SUCCESS;
}

static char *handle_cli_config_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cache_file_mtime *cfmtime;
	char *prev = "";
	int wordlen;

	switch (cmd) {
	case CLI_INIT:
		e->command = "config reload";
		e->usage =
			"Usage: config reload <filename.conf>\n"
			"   Reloads all modules that reference <filename.conf>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos > 2) {
			return NULL;
		}

		wordlen = strlen(a->word);

		AST_LIST_LOCK(&cfmtime_head);
		AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
			/* Core configs cannot be reloaded */
			if (ast_strlen_zero(cfmtime->who_asked)) {
				continue;
			}

			/* Skip duplicates - this only works because the list is sorted by filename */
			if (!strcmp(cfmtime->filename, prev)) {
				continue;
			}

			if (!strncmp(cfmtime->filename, a->word, wordlen)) {
				if (ast_cli_completion_add(ast_strdup(cfmtime->filename))) {
					break;
				}
			}

			/* Otherwise save that we've seen this filename */
			prev = cfmtime->filename;
		}
		AST_LIST_UNLOCK(&cfmtime_head);

		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&cfmtime_head);
	AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
		if (!strcmp(cfmtime->filename, a->argv[2])) {
			char *buf = ast_alloca(strlen("module reload ") + strlen(cfmtime->who_asked) + 1);
			sprintf(buf, "module reload %s", cfmtime->who_asked);
			ast_cli_command(a->fd, buf);
		}
	}
	AST_LIST_UNLOCK(&cfmtime_head);

	return CLI_SUCCESS;
}

static char *handle_cli_config_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cache_file_mtime *cfmtime;

	switch (cmd) {
	case CLI_INIT:
		e->command = "config list";
		e->usage =
			"Usage: config list\n"
			"   Show all modules that have loaded a configuration file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_LIST_LOCK(&cfmtime_head);
	AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
		ast_cli(a->fd, "%-20.20s %-50s\n", S_OR(cfmtime->who_asked, "core"), cfmtime->filename);
	}
	AST_LIST_UNLOCK(&cfmtime_head);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_config[] = {
	AST_CLI_DEFINE(handle_cli_core_show_config_mappings, "Display config mappings (file names to config engines)"),
	AST_CLI_DEFINE(handle_cli_config_reload, "Force a reload on modules using a particular configuration file"),
	AST_CLI_DEFINE(handle_cli_config_list, "Show all files that have loaded a configuration file"),
};

static void config_shutdown(void)
{
	struct cache_file_mtime *cfmtime;

	AST_LIST_LOCK(&cfmtime_head);
	while ((cfmtime = AST_LIST_REMOVE_HEAD(&cfmtime_head, list))) {
		config_cache_destroy_entry(cfmtime);
	}
	AST_LIST_UNLOCK(&cfmtime_head);

	ast_cli_unregister_multiple(cli_config, ARRAY_LEN(cli_config));

	clear_config_maps();

	ao2_cleanup(cfg_hooks);
	cfg_hooks = NULL;
}

int register_config_cli(void)
{
	ast_cli_register_multiple(cli_config, ARRAY_LEN(cli_config));
	/* This is separate from the module load so cleanup can happen very late. */
	ast_register_cleanup(config_shutdown);
	return 0;
}

struct cfg_hook {
	const char *name;
	const char *filename;
	const char *module;
	config_hook_cb hook_cb;
};

static void hook_destroy(void *obj)
{
	struct cfg_hook *hook = obj;
	ast_free((void *) hook->name);
	ast_free((void *) hook->filename);
	ast_free((void *) hook->module);
}

static int hook_cmp(void *obj, void *arg, int flags)
{
	struct cfg_hook *hook1 = obj;
	struct cfg_hook *hook2 = arg;

	return !(strcasecmp(hook1->name, hook2->name)) ? CMP_MATCH | CMP_STOP : 0;
}

static int hook_hash(const void *obj, const int flags)
{
	const struct cfg_hook *hook = obj;

	return ast_str_hash(hook->name);
}

void ast_config_hook_unregister(const char *name)
{
	struct cfg_hook tmp;

	tmp.name = ast_strdupa(name);

	ao2_find(cfg_hooks, &tmp, OBJ_POINTER | OBJ_UNLINK | OBJ_NODATA);
}

static void config_hook_exec(const char *filename, const char *module, const struct ast_config *cfg)
{
	struct ao2_iterator it;
	struct cfg_hook *hook;
	if (!(cfg_hooks)) {
		return;
	}
	it = ao2_iterator_init(cfg_hooks, 0);
	while ((hook = ao2_iterator_next(&it))) {
		if (!strcasecmp(hook->filename, filename) &&
				!strcasecmp(hook->module, module)) {
			struct ast_config *copy = ast_config_copy(cfg);
			hook->hook_cb(copy);
		}
		ao2_ref(hook, -1);
	}
	ao2_iterator_destroy(&it);
}

int ast_config_hook_register(const char *name,
		const char *filename,
		const char *module,
		enum config_hook_flags flags,
		config_hook_cb hook_cb)
{
	struct cfg_hook *hook;
	if (!cfg_hooks) {
		cfg_hooks = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 17,
			hook_hash, NULL, hook_cmp);
		if (!cfg_hooks) {
			return -1;
		}
	}

	if (!(hook = ao2_alloc(sizeof(*hook), hook_destroy))) {
		return -1;
	}

	hook->hook_cb = hook_cb;
	hook->filename = ast_strdup(filename);
	hook->name = ast_strdup(name);
	hook->module = ast_strdup(module);

	ao2_link(cfg_hooks, hook);
	ao2_ref(hook, -1);
	return 0;
}

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	if (ast_opt_console) {
		ast_verb(0, "[ Initializing Custom Configuration Options ]\n");
	}

	return reload_module() ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

/* This module explicitly loads before realtime drivers. */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Configuration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = 0,
);
