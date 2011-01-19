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
 * See doc/realtime.txt and doc/extconfig.txt
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/paths.h"	/* use ast_config_AST_CONFIG_DIR */
#include "asterisk/network.h"	/* we do some sockaddr manipulation here */
#include <time.h>
#include <sys/stat.h>

#include <math.h>	/* HUGE_VAL */

#define AST_INCLUDE_GLOB 1

#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"	/* for the ast_str_*() API */
#include "asterisk/netsock2.h"

#define MAX_NESTED_COMMENTS 128
#define COMMENT_START ";--"
#define COMMENT_END "--;"
#define COMMENT_META ';'
#define COMMENT_TAG '-'

static char *extconfig_conf = "extconfig.conf";


/*! \brief Structure to keep comments for rewriting configuration files */
struct ast_comment {
	struct ast_comment *next;
	char cmt[0];
};

/*! \brief Hold the mtime for config files, so if we don't need to reread our config, don't. */
struct cache_file_include {
	AST_LIST_ENTRY(cache_file_include) list;
	char include[0];
};

struct cache_file_mtime {
	AST_LIST_ENTRY(cache_file_mtime) list;
	AST_LIST_HEAD(includes, cache_file_include) includes;
	unsigned int has_exec:1;
	time_t mtime;
	char *who_asked;
	char filename[0];
};

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
	char *s = alloca(len + 1);
	ast_copy_string(s, str, len);
	ast_str_append(cb, 0, "%s", str);
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
	char *name;
	char *driver;
	char *database;
	char *table;
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
	char *file;	           /*!< the file name from whence this declaration was read */
	int lineno;
	AST_LIST_HEAD_NOLOCK(template_instance_list, ast_category_template_instance) template_instances;
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_comment *trailing; /*!< the last object in the list will get assigned any trailing comments when EOF is hit */
	struct ast_variable *root;
	struct ast_variable *last;
	struct ast_category *next;
};

struct ast_config {
	struct ast_category *root;
	struct ast_category *last;
	struct ast_category *current;
	struct ast_category *last_browse;     /*!< used to cache the last category supplied via category_browse */
	int include_level;
	int max_include_level;
	struct ast_config_include *includes;  /*!< a list of inclusions, which should describe the entire tree */
};

struct ast_config_include {
	char *include_location_file;     /*!< file name in which the include occurs */
	int  include_location_lineno;    /*!< lineno where include occurred */
	int  exec;                       /*!< set to non-zero if itsa #exec statement */
	char *exec_file;                 /*!< if it's an exec, you'll have both the /var/tmp to read, and the original script */
	char *included_file;             /*!< file name included */
	int inclusion_count;             /*!< if the file is included more than once, a running count thereof -- but, worry not,
	                                      we explode the instances and will include those-- so all entries will be unique */
	int output;                      /*!< a flag to indicate if the inclusion has been output */
	struct ast_config_include *next; /*!< ptr to next inclusion in the list */
};

#ifdef MALLOC_DEBUG
struct ast_variable *_ast_variable_new(const char *name, const char *value, const char *filename, const char *file, const char *func, int lineno) 
#else
struct ast_variable *ast_variable_new(const char *name, const char *value, const char *filename) 
#endif
{
	struct ast_variable *variable;
	int name_len = strlen(name) + 1;	
	int val_len = strlen(value) + 1;	
	int fn_len = strlen(filename) + 1;	

#ifdef MALLOC_DEBUG
	if ((variable = __ast_calloc(1, name_len + val_len + fn_len + sizeof(*variable), file, lineno, func))) {
#else
	if ((variable = ast_calloc(1, name_len + val_len + fn_len + sizeof(*variable)))) {
#endif
		char *dst = variable->stuff;	/* writable space starts here */
		variable->name = strcpy(dst, name);
		dst += name_len;
		variable->value = strcpy(dst, value);
		dst += val_len;
		variable->file = strcpy(dst, filename);
	}
	return variable;
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
	inc->include_location_file = ast_strdup(from_file);
	inc->include_location_lineno = from_lineno;
	if (!ast_strlen_zero(real_included_file_name))
		inc->included_file = ast_strdup(real_included_file_name);
	else
		inc->included_file = ast_strdup(included_file);
	
	inc->exec = is_exec;
	if (is_exec)
		inc->exec_file = ast_strdup(exec_file);
	
	/* attach this new struct to the conf struct */
	inc->next = conf->includes;
	conf->includes = inc;
	
	return inc;
}

void ast_include_rename(struct ast_config *conf, const char *from_file, const char *to_file)
{
	struct ast_config_include *incl;
	struct ast_category *cat;
	struct ast_variable *v;
	
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
				free(incl->include_location_file);
				incl->include_location_file = strdup(to_file);
			}
		}
	}
	for (cat = conf->root; cat; cat = cat->next) {
		if (strcmp(cat->file,from_file) == 0) {
			if (from_len >= to_len)
				strcpy(cat->file, to_file);
			else {
				free(cat->file);
				cat->file = strdup(to_file);
			}
		}
		for (v = cat->root; v; v = v->next) {
			if (strcmp(v->file,from_file) == 0) {
				if (from_len >= to_len)
					strcpy(v->file, to_file);
				else {
					free(v->file);
					v->file = strdup(to_file);
				}
			}
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

void ast_variables_destroy(struct ast_variable *v)
{
	struct ast_variable *vn;

	while (v) {
		vn = v;
		v = v->next;
		ast_comment_destroy(&vn->precomments);
		ast_comment_destroy(&vn->sameline);
		ast_comment_destroy(&vn->trailing);
		ast_free(vn);
	}
}

struct ast_variable *ast_variable_browse(const struct ast_config *config, const char *category)
{
	struct ast_category *cat = NULL;

	if (category && config->last_browse && (config->last_browse->name == category)) {
		cat = config->last_browse;
	} else {
		cat = ast_category_get(config, category);
	}

	return (cat) ? cat->root : NULL;
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


const char *ast_variable_retrieve(const struct ast_config *config, const char *category, const char *variable)
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

struct ast_category *ast_category_new(const char *name, const char *in_file, int lineno) 
{
	struct ast_category *category;

	if ((category = ast_calloc(1, sizeof(*category))))
		ast_copy_string(category->name, name, sizeof(category->name));
	category->file = strdup(in_file);
	category->lineno = lineno; /* if you don't know the lineno, set it to 999999 or something real big */
	return category;
}

static struct ast_category *category_get(const struct ast_config *config, const char *category_name, int ignored)
{
	struct ast_category *cat;

	/* try exact match first, then case-insensitive match */
	for (cat = config->root; cat; cat = cat->next) {
		if (cat->name == category_name && (ignored || !cat->ignored))
			return cat;
	}

	for (cat = config->root; cat; cat = cat->next) {
		if (!strcasecmp(cat->name, category_name) && (ignored || !cat->ignored))
			return cat;
	}

	return NULL;
}

struct ast_category *ast_category_get(const struct ast_config *config, const char *category_name)
{
	return category_get(config, category_name, 0);
}

int ast_category_exist(const struct ast_config *config, const char *category_name)
{
	return !!ast_category_get(config, category_name);
}

void ast_category_append(struct ast_config *config, struct ast_category *category)
{
	if (config->last)
		config->last->next = category;
	else
		config->root = category;
	category->include_level = config->include_level;
	config->last = category;
	config->current = category;
}

void ast_category_insert(struct ast_config *config, struct ast_category *cat, const char *match)
{
	struct ast_category *cur_category;

	if (!cat || !match)
		return;
	if (!strcasecmp(config->root->name, match)) {
		cat->next = config->root;
		config->root = cat;
		return;
	} 
	for (cur_category = config->root; cur_category; cur_category = cur_category->next) {
		if (!strcasecmp(cur_category->next->name, match)) {
			cat->next = cur_category->next;
			cur_category->next = cat;
			break;
		}
	}
}

static void ast_destroy_template_list(struct ast_category *cat)
{
	struct ast_category_template_instance *x;

	while ((x = AST_LIST_REMOVE_HEAD(&cat->template_instances, next)))
		free(x);
}

void ast_category_destroy(struct ast_category *cat)
{
	ast_variables_destroy(cat->root);
	if (cat->file) {
		free(cat->file);
		cat->file = 0;
	}
	ast_comment_destroy(&cat->precomments);
	ast_comment_destroy(&cat->sameline);
	ast_comment_destroy(&cat->trailing);
	ast_destroy_template_list(cat);
	ast_free(cat);
}

static void ast_includes_destroy(struct ast_config_include *incls)
{
	struct ast_config_include *incl,*inclnext;
	
	for (incl=incls; incl; incl = inclnext) {
		inclnext = incl->next;
		if (incl->include_location_file)
			free(incl->include_location_file);
		if (incl->exec_file)
			free(incl->exec_file);
		if (incl->included_file)
			free(incl->included_file);
		free(incl);
	}
}

static struct ast_category *next_available_category(struct ast_category *cat)
{
	for (; cat && cat->ignored; cat = cat->next);

	return cat;
}

/*! return the first var of a category */
struct ast_variable *ast_category_first(struct ast_category *cat)
{
	return (cat) ? cat->root : NULL;
}

struct ast_variable *ast_category_root(struct ast_config *config, char *cat)
{
	struct ast_category *category = ast_category_get(config, cat);

	if (category)
		return category->root;
	return NULL;
}

char *ast_category_browse(struct ast_config *config, const char *prev)
{	
	struct ast_category *cat = NULL;

	if (prev && config->last_browse && (config->last_browse->name == prev))
		cat = config->last_browse->next;
	else if (!prev && config->root)
		cat = config->root;
	else if (prev) {
		for (cat = config->root; cat; cat = cat->next) {
			if (cat->name == prev) {
				cat = cat->next;
				break;
			}
		}
		if (!cat) {
			for (cat = config->root; cat; cat = cat->next) {
				if (!strcasecmp(cat->name, prev)) {
					cat = cat->next;
					break;
				}
			}
		}
	}
	
	if (cat)
		cat = next_available_category(cat);

	config->last_browse = cat;
	return (cat) ? cat->name : NULL;
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

static void inherit_category(struct ast_category *new, const struct ast_category *base)
{
	struct ast_variable *var;
	struct ast_category_template_instance *x = ast_calloc(1,sizeof(struct ast_category_template_instance));

	strcpy(x->name, base->name);
	x->inst = base;
	AST_LIST_INSERT_TAIL(&new->template_instances, x, next);
	for (var = base->root; var; var = var->next)
		ast_variable_append(new, variable_clone(var));
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
	int lineno = 0;

	cur = category->root;
	while (cur) {
		if (cur->name == variable) {
			if (prev) {
				prev->next = cur->next;
				if (cur == category->last)
					category->last = prev;
			} else {
				category->root = cur->next;
				if (cur == category->last)
					category->last = NULL;
			}
			cur->next = NULL;
			ast_variables_destroy(cur);
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}

	prev = NULL;
	cur = category->root;
	while (cur) {
		curn = cur->next;
		if ((!ast_strlen_zero(line) && lineno == atoi(line)) || (ast_strlen_zero(line) && !strcasecmp(cur->name, variable) && (ast_strlen_zero(match) || !strcasecmp(cur->value, match)))) {
			if (prev) {
				prev->next = cur->next;
				if (cur == category->last)
					category->last = prev;
			} else {
				category->root = cur->next;
				if (cur == category->last)
					category->last = NULL;
			}
			cur->next = NULL;
			ast_variables_destroy(cur);
			res = 0;
		} else
			prev = cur;

		cur = curn;
		lineno++;
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
	
		newer->next = cur->next;
		newer->object = cur->object || object;

		/* Preserve everything */
		newer->lineno = cur->lineno;
		newer->blanklines = cur->blanklines;
		newer->precomments = cur->precomments; cur->precomments = NULL;
		newer->sameline = cur->sameline; cur->sameline = NULL;
		newer->trailing = cur->trailing; cur->trailing = NULL;

		if (prev)
			prev->next = newer;
		else
			category->root = newer;
		if (category->last == cur)
			category->last = newer;

		cur->next = NULL;
		ast_variables_destroy(cur);

		return 0;
	}

	/* Could not find variable to update */
	return -1;
}

int ast_category_delete(struct ast_config *cfg, const char *category)
{
	struct ast_category *prev=NULL, *cat;

	cat = cfg->root;
	while (cat) {
		if (cat->name == category) {
			if (prev) {
				prev->next = cat->next;
				if (cat == cfg->last)
					cfg->last = prev;
			} else {
				cfg->root = cat->next;
				if (cat == cfg->last)
					cfg->last = NULL;
			}
			ast_category_destroy(cat);
			return 0;
		}
		prev = cat;
		cat = cat->next;
	}

	prev = NULL;
	cat = cfg->root;
	while (cat) {
		if (!strcasecmp(cat->name, category)) {
			if (prev) {
				prev->next = cat->next;
				if (cat == cfg->last)
					cfg->last = prev;
			} else {
				cfg->root = cat->next;
				if (cat == cfg->last)
					cfg->last = NULL;
			}
			ast_category_destroy(cat);
			return 0;
		}
		prev = cat;
		cat = cat->next;
	}
	return -1;
}

int ast_category_empty(struct ast_config *cfg, const char *category)
{
	struct ast_category *cat;

	for (cat = cfg->root; cat; cat = cat->next) {
		if (!strcasecmp(cat->name, category))
			continue;
		ast_variables_destroy(cat->root);
		cat->root = NULL;
		cat->last = NULL;
		return 0;
	}

	return -1;
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

enum config_cache_attribute_enum {
	ATTRIBUTE_INCLUDE = 0,
	ATTRIBUTE_EXEC = 1,
};

static void config_cache_attribute(const char *configfile, enum config_cache_attribute_enum attrtype, const char *filename, const char *who_asked)
{
	struct cache_file_mtime *cfmtime;
	struct cache_file_include *cfinclude;
	struct stat statbuf = { 0, };

	/* Find our cached entry for this configuration file */
	AST_LIST_LOCK(&cfmtime_head);
	AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
		if (!strcmp(cfmtime->filename, configfile) && !strcmp(cfmtime->who_asked, who_asked))
			break;
	}
	if (!cfmtime) {
		cfmtime = ast_calloc(1, sizeof(*cfmtime) + strlen(configfile) + 1 + strlen(who_asked) + 1);
		if (!cfmtime) {
			AST_LIST_UNLOCK(&cfmtime_head);
			return;
		}
		AST_LIST_HEAD_INIT(&cfmtime->includes);
		strcpy(cfmtime->filename, configfile);
		cfmtime->who_asked = cfmtime->filename + strlen(configfile) + 1;
		strcpy(cfmtime->who_asked, who_asked);
		/* Note that the file mtime is initialized to 0, i.e. 1970 */
		AST_LIST_INSERT_SORTALPHA(&cfmtime_head, cfmtime, list, filename);
	}

	if (!stat(configfile, &statbuf))
		cfmtime->mtime = 0;
	else
		cfmtime->mtime = statbuf.st_mtime;

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
		strcpy(cfinclude->include, filename);
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
	char cmd[512], exec_file[512];

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
		struct ast_category *newcat = NULL;
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
		if (!(*cat = newcat = ast_category_new(catname,
				S_OR(suggested_include_file, cfg->include_level == 1 ? "" : configfile),
				lineno))) {
			return -1;
		}
		(*cat)->lineno = lineno;
		*last_var = 0;
		*last_cat = newcat;
		
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
 				ast_log(LOG_WARNING, "parse error: no closing ')', line %d of %s\n", lineno, configfile);
 				return -1;
 			}
 			*cur = '\0';
 			while ((cur = strsep(&c, ","))) {
				if (!strcasecmp(cur, "!")) {
					(*cat)->ignored = 1;
				} else if (!strcasecmp(cur, "+")) {
					*cat = category_get(cfg, catname, 1);
					if (!(*cat)) {
						if (newcat)
							ast_category_destroy(newcat);
						ast_log(LOG_WARNING, "Category addition requested, but category '%s' does not exist, line %d of %s\n", catname, lineno, configfile);
						return -1;
					}
					if (newcat) {
						move_variables(newcat, *cat);
						ast_category_destroy(newcat);
						newcat = NULL;
					}
				} else {
					struct ast_category *base;
 				
					base = category_get(cfg, cur, 1);
					if (!base) {
						ast_log(LOG_WARNING, "Inheritance requested, but category '%s' does not exist, line %d of %s\n", cur, lineno, configfile);
						return -1;
					}
					inherit_category(*cat, base);
				}
 			}
 		}
		if (newcat)
			ast_category_append(cfg, *cat);
	} else if (cur[0] == '#') { /* A directive - #include or #exec */
		char *cur2;
		char real_inclusion_name[256];
		struct ast_config_include *inclu;
		int do_include = 0;	/* otherwise, it is exec */

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
					do_include ? "include" : "exec",
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
			if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE))
				config_cache_attribute(configfile, ATTRIBUTE_EXEC, NULL, who_asked);
			snprintf(exec_file, sizeof(exec_file), "/var/tmp/exec.%d%d.%ld", (int)now.tv_sec, (int)now.tv_usec, (long)pthread_self());
			snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", cur, exec_file);
			ast_safe_system(cmd);
			cur = exec_file;
		} else {
			if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE))
				config_cache_attribute(configfile, ATTRIBUTE_INCLUDE, cur, who_asked);
			exec_file[0] = '\0';
		}
		/* A #include */
		/* record this inclusion */
		inclu = ast_include_new(cfg, cfg->include_level == 1 ? "" : configfile, cur, !do_include, cur2, lineno, real_inclusion_name, sizeof(real_inclusion_name));

		do_include = ast_config_internal_load(cur, cfg, flags, real_inclusion_name, who_asked) ? 1 : 0;
		if (!ast_strlen_zero(exec_file))
			unlink(exec_file);
		if (!do_include) {
			ast_log(LOG_ERROR, "The file '%s' was listed as a #include but it does not exist.\n", cur);
			return -1;
		}
		/* XXX otherwise what ? the default return is 0 anyways */

	} else {
		/* Just a line (variable = value) */
		int object = 0;
		if (!(*cat)) {
			ast_log(LOG_WARNING,
				"parse error: No category context for line %d of %s\n", lineno, configfile);
			return -1;
		}
		c = strchr(cur, '=');

		if (c && c > cur && (*(c - 1) == '+')) {
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
set_new_variable:
			if ((v = ast_variable_new(ast_strip(cur), ast_strip(c), S_OR(suggested_include_file, cfg->include_level == 1 ? "" : configfile)))) {
				v->lineno = lineno;
				v->object = object;
				*last_cat = 0;
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
	struct ast_variable *last_var = 0;
	struct ast_category *last_cat = 0;
	/*! Growable string buffer */
	struct ast_str *comment_buffer = NULL;	/*!< this will be a comment collector.*/
	struct ast_str *lline_buffer = NULL;	/*!< A buffer for stuff behind the ; */

	if (cfg)
		cat = ast_config_get_current_category(cfg);

	if (filename[0] == '/') {
		ast_copy_string(fn, filename, sizeof(fn));
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_CONFIG_DIR, filename);
	}

	if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)) {
		comment_buffer = ast_str_create(CB_SIZE);
		if (comment_buffer)
			lline_buffer = ast_str_create(CB_SIZE);
		if (!lline_buffer) {
			if (comment_buffer)
				ast_free(comment_buffer);
			ast_log(LOG_ERROR, "Failed to initialize the comment buffer!\n");
			return NULL;
		}
	}
#ifdef AST_INCLUDE_GLOB
	{
		int glob_ret;
		glob_t globbuf;
		globbuf.gl_offs = 0;	/* initialize it to silence gcc */
		glob_ret = glob(fn, MY_GLOB_FLAGS, NULL, &globbuf);
		if (glob_ret == GLOB_NOSPACE)
			ast_log(LOG_WARNING,
				"Glob Expansion of pattern '%s' failed: Not enough memory\n", fn);
		else if (glob_ret  == GLOB_ABORTED)
			ast_log(LOG_WARNING,
				"Glob Expansion of pattern '%s' failed: Read error\n", fn);
		else  {
			/* loop over expanded files */
			int i;
			for (i=0; i<globbuf.gl_pathc; i++) {
				ast_copy_string(fn, globbuf.gl_pathv[i], sizeof(fn));
#endif
	/*
	 * The following is not a loop, but just a convenient way to define a block
	 * (using do { } while(0) ), and be able to exit from it with 'continue'
	 * or 'break' in case of errors. Nice trick.
	 */
	do {
		if (stat(fn, &statbuf))
			continue;

		if (!S_ISREG(statbuf.st_mode)) {
			ast_log(LOG_WARNING, "'%s' is not a regular file, ignoring\n", fn);
			continue;
		}

		if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE)) {
			/* Find our cached entry for this configuration file */
			AST_LIST_LOCK(&cfmtime_head);
			AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
				if (!strcmp(cfmtime->filename, fn) && !strcmp(cfmtime->who_asked, who_asked))
					break;
			}
			if (!cfmtime) {
				cfmtime = ast_calloc(1, sizeof(*cfmtime) + strlen(fn) + 1 + strlen(who_asked) + 1);
				if (!cfmtime)
					continue;
				AST_LIST_HEAD_INIT(&cfmtime->includes);
				strcpy(cfmtime->filename, fn);
				cfmtime->who_asked = cfmtime->filename + strlen(fn) + 1;
				strcpy(cfmtime->who_asked, who_asked);
				/* Note that the file mtime is initialized to 0, i.e. 1970 */
				AST_LIST_INSERT_SORTALPHA(&cfmtime_head, cfmtime, list, filename);
			}
		}

		if (cfmtime && (!cfmtime->has_exec) && (cfmtime->mtime == statbuf.st_mtime) && ast_test_flag(&flags, CONFIG_FLAG_FILEUNCHANGED)) {
			/* File is unchanged, what about the (cached) includes (if any)? */
			int unchanged = 1;
			AST_LIST_TRAVERSE(&cfmtime->includes, cfinclude, list) {
				/* We must glob here, because if we did not, then adding a file to globbed directory would
				 * incorrectly cause no reload to be necessary. */
				char fn2[256];
#ifdef AST_INCLUDE_GLOB
				int glob_return;
				glob_t glob_buf = { .gl_offs = 0 };
				glob_return = glob(cfinclude->include, MY_GLOB_FLAGS, NULL, &glob_buf);
				/* On error, we reparse */
				if (glob_return == GLOB_NOSPACE || glob_return  == GLOB_ABORTED)
					unchanged = 0;
				else  {
					/* loop over expanded files */
					int j;
					for (j = 0; j < glob_buf.gl_pathc; j++) {
						ast_copy_string(fn2, glob_buf.gl_pathv[j], sizeof(fn2));
#else
						ast_copy_string(fn2, cfinclude->include);
#endif
						if (config_text_file_load(NULL, NULL, fn2, NULL, flags, "", who_asked) == NULL) {
							/* that second-to-last field needs to be looked at in this case... TODO */
							unchanged = 0;
							/* One change is enough to short-circuit and reload the whole shebang */
							break;
						}
#ifdef AST_INCLUDE_GLOB
					}
				}
#endif
			}

			if (unchanged) {
				AST_LIST_UNLOCK(&cfmtime_head);
				return CONFIG_STATUS_FILEUNCHANGED;
			}
		}
		if (!ast_test_flag(&flags, CONFIG_FLAG_NOCACHE))
			AST_LIST_UNLOCK(&cfmtime_head);

		/* If cfg is NULL, then we just want an answer */
		if (cfg == NULL)
			return NULL;

		if (cfmtime)
			cfmtime->mtime = statbuf.st_mtime;

		ast_verb(2, "Parsing '%s': ", fn);
			fflush(stdout);
		if (!(f = fopen(fn, "r"))) {
			ast_debug(1, "No file to parse: %s\n", fn);
			ast_verb(2, "Not found (%s)\n", strerror(errno));
			continue;
		}
		count++;
		/* If we get to this point, then we're loading regardless */
		ast_clear_flag(&flags, CONFIG_FLAG_FILEUNCHANGED);
		ast_debug(1, "Parsing %s\n", fn);
		ast_verb(2, "Found\n");
		while (!feof(f)) {
			lineno++;
			if (fgets(buf, sizeof(buf), f)) {
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && lline_buffer && ast_str_strlen(lline_buffer)) {
					CB_ADD(&comment_buffer, ast_str_buffer(lline_buffer));       /* add the current lline buffer to the comment buffer */
					ast_str_reset(lline_buffer);        /* erase the lline buffer */
				}
				
				new_buf = buf;
				if (comment) 
					process_buf = NULL;
				else
					process_buf = buf;
				
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment_buffer && ast_str_strlen(comment_buffer) && (ast_strlen_zero(buf) || strlen(buf) == strspn(buf," \t\n\r"))) {
					/* blank line? really? Can we add it to an existing comment and maybe preserve inter- and post- comment spacing? */
					CB_ADD(&comment_buffer, "\n");       /* add a newline to the comment buffer */
					continue; /* go get a new line, then */
				}
				
				while ((comment_p = strchr(new_buf, COMMENT_META))) {
					if ((comment_p > new_buf) && (*(comment_p - 1) == '\\')) {
						/* Escaped semicolons aren't comments. */
						new_buf = comment_p + 1;
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
						/* Meta-Comment end detected */
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
							} else
								process_buf = new_buf;
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
						} else
							new_buf = comment_p + 1;
					}
				}
				if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment && !process_buf ) {
					CB_ADD(&comment_buffer, buf);  /* the whole line is a comment, store it */
				}
				
				if (process_buf) {
					char *buffer = ast_strip(process_buf);
					if (!ast_strlen_zero(buffer)) {
						if (process_text_line(cfg, &cat, buffer, lineno, fn, flags, comment_buffer, lline_buffer, suggested_include_file, &last_cat, &last_var, who_asked)) {
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
					CB_ADD(&comment_buffer, ast_str_buffer(lline_buffer));       /* add the current lline buffer to the comment buffer */
					ast_str_reset(lline_buffer);        /* erase the lline buffer */
				}
				last_cat->trailing = ALLOC_COMMENT(comment_buffer);
			}
		} else if (last_var) {
			if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment_buffer && ast_str_strlen(comment_buffer)) {
				if (lline_buffer && ast_str_strlen(lline_buffer)) {
					CB_ADD(&comment_buffer, ast_str_buffer(lline_buffer));       /* add the current lline buffer to the comment buffer */
					ast_str_reset(lline_buffer);        /* erase the lline buffer */
				}
				last_var->trailing = ALLOC_COMMENT(comment_buffer);
			}
		} else {
			if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS) && comment_buffer && ast_str_strlen(comment_buffer)) {
				ast_debug(1, "Nothing to attach comments to, discarded: %s\n", ast_str_buffer(comment_buffer));
			}
		}
		if (ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS))
			CB_RESET(comment_buffer, lline_buffer);

		fclose(f);
	} while (0);
	if (comment) {
		ast_log(LOG_WARNING,"Unterminated comment detected beginning on line %d\n", nest[comment - 1]);
	}
#ifdef AST_INCLUDE_GLOB
					if (cfg == NULL || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
						break;
					}
				}
				globfree(&globbuf);
			}
		}
#endif

	if (cfg && cfg != CONFIG_STATUS_FILEUNCHANGED && cfg != CONFIG_STATUS_FILEINVALID && cfg->include_level == 1 && ast_test_flag(&flags, CONFIG_FLAG_WITHCOMMENTS)) {
		if (comment_buffer)
			ast_free(comment_buffer);
		if (lline_buffer)
			ast_free(lline_buffer);
		comment_buffer = NULL;
		lline_buffer = NULL;
	}
	
	if (count == 0)
		return NULL;

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

static void   inclfile_destroy(void *obj)
{
	const struct inclfile *o = obj;

	if (o->fname)
		free(o->fname);
}


static void set_fn(char *fn, int fn_size, const char *file, const char *configfile, struct ao2_container *fileset, struct inclfile **fi)
{
	struct inclfile lookup;
	
	if (!file || file[0] == 0) {
		if (configfile[0] == '/')
			ast_copy_string(fn, configfile, fn_size);
		else
			snprintf(fn, fn_size, "%s/%s", ast_config_AST_CONFIG_DIR, configfile);
	} else if (file[0] == '/') 
		ast_copy_string(fn, file, fn_size);
	else
		snprintf(fn, fn_size, "%s/%s", ast_config_AST_CONFIG_DIR, file);
	lookup.fname = fn;
	*fi = ao2_find(fileset, &lookup, OBJ_POINTER);
	if (!(*fi)) {
		/* set up a file scratch pad */
		struct inclfile *fx = ao2_alloc(sizeof(struct inclfile), inclfile_destroy);
		fx->fname = ast_strdup(fn);
		fx->lineno = 1;
		*fi = fx;
		ao2_link(fileset, fx);
	}
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
	int precomment_lines = count_linefeeds_in_comments(precomments);
	int i;

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

int config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator)
{
	return ast_config_text_file_save(configfile, cfg, generator);
}

int ast_config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator)
{
	FILE *f;
	char fn[256];
	struct ast_variable *var;
	struct ast_category *cat;
	struct ast_comment *cmt;
	struct ast_config_include *incl;
	int blanklines = 0;
	struct ao2_container *fileset = ao2_container_alloc(180000, hash_string, hashtab_compare_strings);
	struct inclfile *fi = 0;

	/* reset all the output flags, in case this isn't our first time saving this data */

	for (incl=cfg->includes; incl; incl = incl->next)
		incl->output = 0;

	/* go thru all the inclusions and make sure all the files involved (configfile plus all its inclusions)
	   are all truncated to zero bytes and have that nice header*/

	for (incl=cfg->includes; incl; incl = incl->next)
	{
		if (!incl->exec) { /* leave the execs alone -- we'll write out the #exec directives, but won't zero out the include files or exec files*/
			FILE *f1;

			set_fn(fn, sizeof(fn), incl->included_file, configfile, fileset, &fi); /* normally, fn is just set to incl->included_file, prepended with config dir if relative */
			f1 = fopen(fn,"w");
			if (f1) {
				gen_header(f1, configfile, fn, generator);
				fclose(f1); /* this should zero out the file */
			} else {
				ast_debug(1, "Unable to open for writing: %s\n", fn);
				ast_verb(2, "Unable to write %s (%s)", fn, strerror(errno));
			}
			ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
			fi = 0;
		}
	}

	set_fn(fn, sizeof(fn), 0, configfile, fileset, &fi); /* just set fn to absolute ver of configfile */
#ifdef __CYGWIN__	
	if ((f = fopen(fn, "w+"))) {
#else
	if ((f = fopen(fn, "w"))) {
#endif	    
		ast_verb(2, "Saving '%s': ", fn);
		gen_header(f, configfile, fn, generator);
		cat = cfg->root;
		fclose(f);
		ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
		
		/* from here out, we open each involved file and concat the stuff we need to add to the end and immediately close... */
		/* since each var, cat, and associated comments can come from any file, we have to be 
		   mobile, and open each file, print, and close it on an entry-by-entry basis */

		while (cat) {
			set_fn(fn, sizeof(fn), cat->file, configfile, fileset, &fi);
			f = fopen(fn, "a");
			if (!f)
			{
				ast_debug(1, "Unable to open for writing: %s\n", fn);
				ast_verb(2, "Unable to write %s (%s)", fn, strerror(errno));
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
				while (*cmtp == ';' && *(cmtp+1) == '!') {
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
			ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
			fi = 0;
			
			var = cat->root;
			while (var) {
				struct ast_category_template_instance *x;
				int found = 0;
				AST_LIST_TRAVERSE(&cat->template_instances, x, next) {
					struct ast_variable *v;
					for (v = x->inst->root; v; v = v->next) {
						if (!strcasecmp(var->name, v->name) && !strcmp(var->value, v->value)) {
							found = 1;
							break;
						}
					}
					if (found)
						break;
				}
				if (found) {
					var = var->next;
					continue;
				}
				set_fn(fn, sizeof(fn), var->file, configfile, fileset, &fi);
				f = fopen(fn, "a");
				if (!f)
				{
					ast_debug(1, "Unable to open for writing: %s\n", fn);
					ast_verb(2, "Unable to write %s (%s)", fn, strerror(errno));
					ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
					fi = 0;
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
				if (var->sameline) 
					fprintf(f, "%s %s %s  %s", var->name, (var->object ? "=>" : "="), var->value, var->sameline->cmt);
				else	
					fprintf(f, "%s %s %s\n", var->name, (var->object ? "=>" : "="), var->value);
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
				ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
				fi = 0;
				
				var = var->next;
			}
			cat = cat->next;
		}
		if (!option_debug)
			ast_verb(2, "Saved\n");
	} else {
		ast_debug(1, "Unable to open for writing: %s\n", fn);
		ast_verb(2, "Unable to write (%s)", strerror(errno));
		ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
		ao2_ref(fileset, -1);
		return -1;
	}

	/* Now, for files with trailing #include/#exec statements,
	   we have to make sure every entry is output */

	for (incl=cfg->includes; incl; incl = incl->next) {
		if (!incl->output) {
			/* open the respective file */
			set_fn(fn, sizeof(fn), incl->include_location_file, configfile, fileset, &fi);
			f = fopen(fn, "a");
			if (!f)
			{
				ast_debug(1, "Unable to open for writing: %s\n", fn);
				ast_verb(2, "Unable to write %s (%s)", fn, strerror(errno));
				ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
				fi = 0;
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
			ao2_ref(fi,-1); /* we are giving up this reference to the object ptd to by fi */
			fi = 0;
		}
	}
	ao2_ref(fileset, -1); /* this should destroy the hash container */
				
	return 0;
}

static void clear_config_maps(void) 
{
	struct ast_config_map *map;

	ast_mutex_lock(&config_lock);

	while (config_maps) {
		map = config_maps;
		config_maps = config_maps->next;
		ast_free(map);
	}
		
	ast_mutex_unlock(&config_lock);
}

static int append_mapping(const char *name, const char *driver, const char *database, const char *table, int priority)
{
	struct ast_config_map *map;
	int length;

	length = sizeof(*map);
	length += strlen(name) + 1;
	length += strlen(driver) + 1;
	length += strlen(database) + 1;
	if (table)
		length += strlen(table) + 1;

	if (!(map = ast_calloc(1, length)))
		return -1;

	map->name = map->stuff;
	strcpy(map->name, name);
	map->driver = map->name + strlen(map->name) + 1;
	strcpy(map->driver, driver);
	map->database = map->driver + strlen(map->driver) + 1;
	strcpy(map->database, database);
	if (table) {
		map->table = map->database + strlen(map->database) + 1;
		strcpy(map->table, table);
	}
	map->priority = priority;
	map->next = config_maps;

	ast_verb(2, "Binding %s to %s/%s/%s\n", map->name, map->driver, map->database, map->table ? map->table : map->name);

	config_maps = map;
	return 0;
}

int read_config_maps(void) 
{
	struct ast_config *config, *configtmp;
	struct ast_variable *v;
	char *driver, *table, *database, *textpri, *stringp, *tmp;
	struct ast_flags flags = { CONFIG_FLAG_NOREALTIME };
	int pri;

	clear_config_maps();

	configtmp = ast_config_new();
	configtmp->max_include_level = 1;
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
			ast_log(LOG_WARNING, "The 'sipfriends' table is obsolete, update your config to use sipusers and sippeers, though they can point to the same table.\n");
			append_mapping("sipusers", driver, database, table ? table : "sipfriends", pri);
			append_mapping("sippeers", driver, database, table ? table : "sipfriends", pri);
		} else if (!strcasecmp(v->name, "iaxfriends")) {
			ast_log(LOG_WARNING, "The 'iaxfriends' table is obsolete, update your config to use iaxusers and iaxpeers, though they can point to the same table.\n");
			append_mapping("iaxusers", driver, database, table ? table : "iaxfriends", pri);
			append_mapping("iaxpeers", driver, database, table ? table : "iaxfriends", pri);
		} else 
			append_mapping(v->name, driver, database, table, pri);
	}
		
	ast_config_destroy(config);
	return 0;
}

int ast_config_engine_register(struct ast_config_engine *new) 
{
	struct ast_config_engine *ptr;

	ast_mutex_lock(&config_lock);

	if (!config_engine_list) {
		config_engine_list = new;
	} else {
		for (ptr = config_engine_list; ptr->next; ptr=ptr->next);
		ptr->next = new;
	}

	ast_mutex_unlock(&config_lock);
	ast_log(LOG_NOTICE,"Registered Config Engine %s\n", new->name);

	return 1;
}

int ast_config_engine_deregister(struct ast_config_engine *del) 
{
	struct ast_config_engine *ptr, *last=NULL;

	ast_mutex_lock(&config_lock);

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

	ast_mutex_unlock(&config_lock);

	return 0;
}

/*! \brief Find realtime engine for realtime family */
static struct ast_config_engine *find_engine(const char *family, int priority, char *database, int dbsiz, char *table, int tabsiz) 
{
	struct ast_config_engine *eng, *ret = NULL;
	struct ast_config_map *map;

	ast_mutex_lock(&config_lock);

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

	ast_mutex_unlock(&config_lock);
	
	/* if we found a mapping, but the engine is not available, then issue a warning */
	if (map && !ret)
		ast_log(LOG_WARNING, "Realtime mapping for '%s' found to engine '%s', but the engine is not available\n", map->name, map->driver);

	return ret;
}

static struct ast_config_engine text_file_engine = {
	.name = "text",
	.load_func = config_text_file_load,
};

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

	if (result && result != CONFIG_STATUS_FILEINVALID && result != CONFIG_STATUS_FILEUNCHANGED)
		result->include_level--;
	else if (result != CONFIG_STATUS_FILEINVALID)
		cfg->include_level--;

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

static struct ast_variable *ast_load_realtime_helper(const char *family, va_list ap)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	struct ast_variable *res=NULL;
	int i;

	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->realtime_func && (res = eng->realtime_func(db, table, ap))) {
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
	struct ast_variable *res;
	va_list ap;

	va_start(ap, family);
	res = ast_load_realtime_helper(family, ap);
	va_end(ap);

	return res;
}

struct ast_variable *ast_load_realtime(const char *family, ...)
{
	struct ast_variable *res, *cur, *prev = NULL, *freeme = NULL;
	va_list ap;

	va_start(ap, family);
	res = ast_load_realtime_helper(family, ap);
	va_end(ap);

	/* Eliminate blank entries */
	for (cur = res; cur; cur = cur->next) {
		if (freeme) {
			ast_free(freeme);
			freeme = NULL;
		}

		if (ast_strlen_zero(cur->value)) {
			if (prev)
				prev->next = cur->next;
			else
				res = cur->next;
			freeme = cur;
		} else if (cur->value[0] == ' ' && cur->value[1] == '\0') {
			char *vptr = (char *) cur->value;
			vptr[0] = '\0';
			prev = cur;
		} else {
			prev = cur;
		}
	}
	return res;
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
int ast_realtime_enabled()
{
	return config_maps ? 1 : 0;
}

int ast_realtime_require_field(const char *family, ...)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	va_list ap;
	int res = -1, i;

	va_start(ap, family);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			/* If the require succeeds, it returns 0. */
			if (eng->require_func && !(res = eng->require_func(db, table, ap))) {
				break;
			}
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

struct ast_config *ast_load_realtime_multientry(const char *family, ...)
{
	struct ast_config_engine *eng;
	char db[256];
	char table[256];
	struct ast_config *res = NULL;
	va_list ap;
	int i;

	va_start(ap, family);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->realtime_multi_func && (res = eng->realtime_multi_func(db, table, ap))) {
				break;
			}
		} else {
			break;
		}
	}
	va_end(ap);

	return res;
}

int ast_update_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];
	va_list ap;

	va_start(ap, lookup);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			/* If the update succeeds, it returns 0. */
			if (eng->update_func && !(res = eng->update_func(db, table, keyfield, lookup, ap))) {
				break;
			}
		} else {
			break;
		}
	}
	va_end(ap);

	return res;
}

int ast_update2_realtime(const char *family, ...)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];
	va_list ap;

	va_start(ap, family);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->update2_func && !(res = eng->update2_func(db, table, ap))) {
				break;
			}
		} else {
			break;
		}
	}
	va_end(ap);

	return res;
}

int ast_store_realtime(const char *family, ...)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];
	va_list ap;

	va_start(ap, family);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			/* If the store succeeds, it returns 0. */
			if (eng->store_func && !(res = eng->store_func(db, table, ap))) {
				break;
			}
		} else {
			break;
		}
	}
	va_end(ap);

	return res;
}

int ast_destroy_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	struct ast_config_engine *eng;
	int res = -1, i;
	char db[256];
	char table[256];
	va_list ap;

	va_start(ap, lookup);
	for (i = 1; ; i++) {
		if ((eng = find_engine(family, i, db, sizeof(db), table, sizeof(table)))) {
			if (eng->destroy_func && !(res = eng->destroy_func(db, table, keyfield, lookup, ap))) {
				break;
			}
		} else {
			break;
		}
	}
	va_end(ap);

	return res;
}

char *ast_realtime_decode_chunk(char *chunk)
{
	char *orig = chunk;
	for (; *chunk; chunk++) {
		if (*chunk == '^' && strchr("0123456789ABCDEFabcdef", chunk[1]) && strchr("0123456789ABCDEFabcdef", chunk[2])) {
			sscanf(chunk + 1, "%02hhX", chunk);
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
		int32_t *result = p_result;
		int32_t x, def = result ? *result : 0,
			high = (int32_t)0x7fffffff,
			low  = (int32_t)0x80000000;
		/* optional argument: first default value, then range */
		if (flags & PARSE_DEFAULT)
			def = va_arg(ap, int32_t);
		if (flags & (PARSE_IN_RANGE|PARSE_OUT_RANGE)) {
			/* range requested, update bounds */
			low = va_arg(ap, int32_t);
			high = va_arg(ap, int32_t);
		}
		x = strtol(arg, NULL, 0);
		error = (x < low) || (x > high);
		if (flags & PARSE_OUT_RANGE)
			error = !error;
		if (result)
			*result  = error ? def : x;
		ast_debug(3,
			"extract int from [%s] in [%d, %d] gives [%d](%d)\n",
			arg, low, high,
			result ? *result : x, error);
		break;
	    }

	case PARSE_UINT32:
	    {
		uint32_t *result = p_result;
		uint32_t x, def = result ? *result : 0,
			low = 0, high = (uint32_t)~0;
		/* optional argument: first default value, then range */
		if (flags & PARSE_DEFAULT)
			def = va_arg(ap, uint32_t);
		if (flags & (PARSE_IN_RANGE|PARSE_OUT_RANGE)) {
			/* range requested, update bounds */
			low = va_arg(ap, uint32_t);
			high = va_arg(ap, uint32_t);
		}
		x = strtoul(arg, NULL, 0);
		error = (x < low) || (x > high);
		if (flags & PARSE_OUT_RANGE)
			error = !error;
		if (result)
			*result  = error ? def : x;
		ast_debug(3,
			"extract uint from [%s] in [%u, %u] gives [%u](%d)\n",
			arg, low, high,
			result ? *result : x, error);
		break;
	    }

	case PARSE_DOUBLE:
	    {
		double *result = p_result;
		double x, def = result ? *result : 0,
			low = -HUGE_VAL, high = HUGE_VAL;

		/* optional argument: first default value, then range */
		if (flags & PARSE_DEFAULT)
			def = va_arg(ap, double);
		if (flags & (PARSE_IN_RANGE|PARSE_OUT_RANGE)) {
			/* range requested, update bounds */
			low = va_arg(ap, double);
			high = va_arg(ap, double);
		}
		x = strtod(arg, NULL);
		error = (x < low) || (x > high);
		if (flags & PARSE_OUT_RANGE)
			error = !error;
		if (result)
			*result  = error ? def : x;
		ast_debug(3,
			"extract double from [%s] in [%f, %f] gives [%f](%d)\n",
			arg, low, high,
			result ? *result : x, error);
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
		struct hostent *hp;
		struct ast_hostent ahp;

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
		hp = ast_gethostbyname(buf, &ahp);
		if (hp)	/* resolved successfully */
			memcpy(&sa->sin_addr, hp->h_addr, sizeof(sa->sin_addr));
		else {
			error = 1;
			sa->sin_addr = def->sin_addr;
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
	
	ast_mutex_lock(&config_lock);

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
	
	ast_mutex_unlock(&config_lock);

	return CLI_SUCCESS;
}

static char *handle_cli_config_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cache_file_mtime *cfmtime;
	char *prev = "", *completion_value = NULL;
	int wordlen, which = 0;

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
			/* Skip duplicates - this only works because the list is sorted by filename */
			if (strcmp(cfmtime->filename, prev) == 0) {
				continue;
			}

			/* Core configs cannot be reloaded */
			if (ast_strlen_zero(cfmtime->who_asked)) {
				continue;
			}

			if (++which > a->n && strncmp(cfmtime->filename, a->word, wordlen) == 0) {
				completion_value = ast_strdup(cfmtime->filename);
				break;
			}

			/* Otherwise save that we've seen this filename */
			prev = cfmtime->filename;
		}
		AST_LIST_UNLOCK(&cfmtime_head);

		return completion_value;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&cfmtime_head);
	AST_LIST_TRAVERSE(&cfmtime_head, cfmtime, list) {
		if (!strcmp(cfmtime->filename, a->argv[2])) {
			char *buf = alloca(strlen("module reload ") + strlen(cfmtime->who_asked) + 1);
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

int register_config_cli() 
{
	ast_cli_register_multiple(cli_config, ARRAY_LEN(cli_config));
	return 0;
}
