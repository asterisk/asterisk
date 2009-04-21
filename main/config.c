/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#define AST_INCLUDE_GLOB 1
#ifdef AST_INCLUDE_GLOB
# include <glob.h>
#endif

#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"

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

#define CB_INCR 250

static void CB_INIT(char **comment_buffer, int *comment_buffer_size, char **lline_buffer, int *lline_buffer_size)
{
	if (!(*comment_buffer)) {
		*comment_buffer = ast_malloc(CB_INCR);
		if (!(*comment_buffer))
			return;
		(*comment_buffer)[0] = 0;
		*comment_buffer_size = CB_INCR;
		*lline_buffer = ast_malloc(CB_INCR);
		if (!(*lline_buffer))
			return;
		(*lline_buffer)[0] = 0;
		*lline_buffer_size = CB_INCR;
	} else {
		(*comment_buffer)[0] = 0;
		(*lline_buffer)[0] = 0;
	}
}

static void  CB_ADD(char **comment_buffer, int *comment_buffer_size, char *str)
{
	int rem = *comment_buffer_size - strlen(*comment_buffer) - 1;
	int siz = strlen(str);
	if (rem < siz+1) {
		*comment_buffer = ast_realloc(*comment_buffer, *comment_buffer_size + CB_INCR + siz + 1);
		if (!(*comment_buffer))
			return;
		*comment_buffer_size += CB_INCR+siz+1;
	}
	strcat(*comment_buffer,str);
}

static void  CB_ADD_LEN(char **comment_buffer, int *comment_buffer_size, char *str, int len)
{
	int cbl = strlen(*comment_buffer) + 1;
	int rem = *comment_buffer_size - cbl;
	if (rem < len+1) {
		*comment_buffer = ast_realloc(*comment_buffer, *comment_buffer_size + CB_INCR + len + 1);
		if (!(*comment_buffer))
			return;
		*comment_buffer_size += CB_INCR+len+1;
	}
	strncat(*comment_buffer,str,len);
	(*comment_buffer)[cbl+len-1] = 0;
}

static void  LLB_ADD(char **lline_buffer, int *lline_buffer_size, char *str)
{
	int rem = *lline_buffer_size - strlen(*lline_buffer) - 1;
	int siz = strlen(str);
	if (rem < siz+1) {
		*lline_buffer = ast_realloc(*lline_buffer, *lline_buffer_size + CB_INCR + siz + 1);
		if (!(*lline_buffer)) 
			return;
		*lline_buffer_size += CB_INCR + siz + 1;
	}
	strcat(*lline_buffer,str);
}

static void CB_RESET(char **comment_buffer, char **lline_buffer)  
{ 
	(*comment_buffer)[0] = 0; 
	(*lline_buffer)[0] = 0;
}


static struct ast_comment *ALLOC_COMMENT(const char *buffer)
{ 
	struct ast_comment *x = ast_calloc(1,sizeof(struct ast_comment)+strlen(buffer)+1);
	strcpy(x->cmt, buffer);
	return x;
}


static struct ast_config_map {
	struct ast_config_map *next;
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
	int ignored;			/*!< do not let user of the config see this category */
	int include_level;	
	AST_LIST_HEAD_NOLOCK(template_instance_list, ast_category_template_instance) template_instances;
	struct ast_comment *precomments;
	struct ast_comment *sameline;
	struct ast_variable *root;
	struct ast_variable *last;
	struct ast_category *next;
};

struct ast_config {
	struct ast_category *root;
	struct ast_category *last;
	struct ast_category *current;
	struct ast_category *last_browse;		/*!< used to cache the last category supplied via category_browse */
	int include_level;
	int max_include_level;
};

struct ast_variable *ast_variable_new(const char *name, const char *value) 
{
	struct ast_variable *variable;
	int name_len = strlen(name) + 1;	

	if ((variable = ast_calloc(1, name_len + strlen(value) + 1 + sizeof(*variable)))) {
		variable->name = variable->stuff;
		variable->value = variable->stuff + name_len;		
		strcpy(variable->name,name);
		strcpy(variable->value,value);
	}

	return variable;
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

void ast_variables_destroy(struct ast_variable *v)
{
	struct ast_variable *vn;

	while(v) {
		vn = v;
		v = v->next;
		free(vn);
	}
}

struct ast_variable *ast_variable_browse(const struct ast_config *config, const char *category)
{
	struct ast_category *cat = NULL;

	if (category && config->last_browse && (config->last_browse->name == category))
		cat = config->last_browse;
	else
		cat = ast_category_get(config, category);

	return (cat) ? cat->root : NULL;
}

const char *ast_config_option(struct ast_config *cfg, const char *cat, const char *var)
{
	const char *tmp;
	tmp = ast_variable_retrieve(cfg, cat, var);
	if (!tmp)
		tmp = ast_variable_retrieve(cfg, "general", var);
	return tmp;
}


const char *ast_variable_retrieve(const struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category) {
		for (v = ast_variable_browse(config, category); v; v = v->next) {
			if (!strcasecmp(variable, v->name))
				return v->value;
		}
	} else {
		struct ast_category *cat;

		for (cat = config->root; cat; cat = cat->next)
			for (v = cat->root; v; v = v->next)
				if (!strcasecmp(variable, v->name))
					return v->value;
	}

	return NULL;
}

static struct ast_variable *variable_clone(const struct ast_variable *old)
{
	struct ast_variable *new = ast_variable_new(old->name, old->value);

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
#if 1
	/* we can just move the entire list in a single op */
	ast_variable_append(new, var);
#else
	while (var) {
		struct ast_variable *next = var->next;
		var->next = NULL;
		ast_variable_append(new, var);
		var = next;
	}
#endif
}

struct ast_category *ast_category_new(const char *name) 
{
	struct ast_category *category;

	if ((category = ast_calloc(1, sizeof(*category))))
		ast_copy_string(category->name, name, sizeof(category->name));
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

static void ast_destroy_comments(struct ast_category *cat)
{
	struct ast_comment *n, *p;
	for (p=cat->precomments; p; p=n) {
		n = p->next;
		free(p);
	}
	for (p=cat->sameline; p; p=n) {
		n = p->next;
		free(p);
	}
	cat->precomments = NULL;
	cat->sameline = NULL;
}

static void ast_destroy_template_list(struct ast_category *cat)
{
	struct ast_category_template_instance *x;
	AST_LIST_TRAVERSE_SAFE_BEGIN(&cat->template_instances, x, next) {
		AST_LIST_REMOVE_CURRENT(&cat->template_instances, next);
		free(x);
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

void ast_category_destroy(struct ast_category *cat)
{
	ast_variables_destroy(cat->root);
	ast_destroy_comments(cat);
	ast_destroy_template_list(cat);
	free(cat);
}

static struct ast_category *next_available_category(struct ast_category *cat)
{
	for (; cat && cat->ignored; cat = cat->next);

	return cat;
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

int ast_variable_delete(struct ast_category *category, char *variable, char *match)
{
	struct ast_variable *cur, *prev=NULL, *curn;
	int res = -1;
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
		if (!strcasecmp(cur->name, variable) && (ast_strlen_zero(match) || !strcasecmp(cur->value, match))) {
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
	}
	return res;
}

int ast_variable_update(struct ast_category *category, const char *variable, 
	const char *value, const char *match, unsigned int object)
{
	struct ast_variable *cur, *prev=NULL, *newer;

	if (!(newer = ast_variable_new(variable, value)))
		return -1;
	
	newer->object = object;

	for (cur = category->root; cur; prev = cur, cur = cur->next) {
		if (strcasecmp(cur->name, variable) ||
			(!ast_strlen_zero(match) && strcasecmp(cur->value, match)))
			continue;

		newer->next = cur->next;
		newer->object = cur->object || object;
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

	if (prev)
		prev->next = newer;
	else
		category->root = newer;

	return 0;
}

int ast_category_delete(struct ast_config *cfg, char *category)
{
	struct ast_category *prev=NULL, *cat;
	cat = cfg->root;
	while(cat) {
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
	while(cat) {
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

void ast_config_destroy(struct ast_config *cfg)
{
	struct ast_category *cat, *catn;

	if (!cfg)
		return;

	cat = cfg->root;
	while(cat) {
		catn = cat;
		cat = cat->next;
		ast_category_destroy(catn);
	}
	free(cfg);
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

static int process_text_line(struct ast_config *cfg, struct ast_category **cat, char *buf, int lineno, const char *configfile, int withcomments,
				char **comment_buffer, int *comment_buffer_size, char **lline_buffer, int *lline_buffer_size)
{
	char *c;
	char *cur = buf;
	struct ast_variable *v;
	char cmd[512], exec_file[512];
	int object, do_exec, do_include;

	/* Actually parse the entry */
	if (cur[0] == '[') {
		struct ast_category *newcat = NULL;
		char *catname;

		/* A category header */
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
		if (!(*cat = newcat = ast_category_new(catname))) {
			return -1;
		}
		/* add comments */
		if (withcomments && *comment_buffer && (*comment_buffer)[0] ) {
			newcat->precomments = ALLOC_COMMENT(*comment_buffer);
		}
		if (withcomments && *lline_buffer && (*lline_buffer)[0] ) {
			newcat->sameline = ALLOC_COMMENT(*lline_buffer);
		}
		if( withcomments )
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
	} else if (cur[0] == '#') {
		/* A directive */
		cur++;
		c = cur;
		while(*c && (*c > 32)) c++;
		if (*c) {
			*c = '\0';
			/* Find real argument */
			c = ast_skip_blanks(c + 1);
			if (!(*c))
				c = NULL;
		} else 
			c = NULL;
		do_include = !strcasecmp(cur, "include");
		if(!do_include)
			do_exec = !strcasecmp(cur, "exec");
		else
			do_exec = 0;
		if (do_exec && !ast_opt_exec_includes) {
			ast_log(LOG_WARNING, "Cannot perform #exec unless execincludes option is enabled in asterisk.conf (options section)!\n");
			do_exec = 0;
		}
		if (do_include || do_exec) {
			if (c) {
				/* Strip off leading and trailing "'s and <>'s */
				while((*c == '<') || (*c == '>') || (*c == '\"')) c++;
				/* Get rid of leading mess */
				cur = c;
				while (!ast_strlen_zero(cur)) {
					c = cur + strlen(cur) - 1;
					if ((*c == '>') || (*c == '<') || (*c == '\"'))
						*c = '\0';
					else
						break;
				}
				/* #exec </path/to/executable>
				   We create a tmp file, then we #include it, then we delete it. */
				if (do_exec) { 
					snprintf(exec_file, sizeof(exec_file), "/var/tmp/exec.%d.%ld", (int)time(NULL), (long)pthread_self());
					snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", cur, exec_file);
					ast_safe_system(cmd);
					cur = exec_file;
				} else
					exec_file[0] = '\0';
				/* A #include */
				do_include = ast_config_internal_load(cur, cfg, withcomments) ? 1 : 0;
				if(!ast_strlen_zero(exec_file))
					unlink(exec_file);
				if (!do_include) {
					ast_log(LOG_ERROR, "*********************************************************\n");
					ast_log(LOG_ERROR, "*********** YOU SHOULD REALLY READ THIS ERROR ***********\n");
					ast_log(LOG_ERROR, "Future versions of Asterisk will treat a #include of a "
					                   "file that does not exist as an error, and will fail to "
					                   "load that configuration file.  Please ensure that the "
					                   "file '%s' exists, even if it is empty.\n", cur);
					ast_log(LOG_ERROR, "*********** YOU SHOULD REALLY READ THIS ERROR ***********\n");
					ast_log(LOG_ERROR, "*********************************************************\n");
					return 0;
				}

			} else {
				ast_log(LOG_WARNING, "Directive '#%s' needs an argument (%s) at line %d of %s\n", 
						do_exec ? "exec" : "include",
						do_exec ? "/path/to/executable" : "filename",
						lineno,
						configfile);
			}
		}
		else 
			ast_log(LOG_WARNING, "Unknown directive '#%s' at line %d of %s\n", cur, lineno, configfile);
	} else {
		/* Just a line (variable = value) */
		if (!(*cat)) {
			ast_log(LOG_WARNING,
				"parse error: No category context for line %d of %s\n", lineno, configfile);
			return -1;
		}
		c = strchr(cur, '=');
		if (c) {
			*c = 0;
			c++;
			/* Ignore > in => */
			if (*c== '>') {
				object = 1;
				c++;
			} else
				object = 0;
			if ((v = ast_variable_new(ast_strip(cur), ast_strip(c)))) {
				v->lineno = lineno;
				v->object = object;
				/* Put and reset comments */
				v->blanklines = 0;
				ast_variable_append(*cat, v);
				/* add comments */
				if (withcomments && *comment_buffer && (*comment_buffer)[0] ) {
					v->precomments = ALLOC_COMMENT(*comment_buffer);
				}
				if (withcomments && *lline_buffer && (*lline_buffer)[0] ) {
					v->sameline = ALLOC_COMMENT(*lline_buffer);
				}
				if( withcomments )
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

static struct ast_config *config_text_file_load(const char *database, const char *table, const char *filename, struct ast_config *cfg, int withcomments)
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
	/*! Growable string buffer */
	char *comment_buffer=0;   /*!< this will be a comment collector.*/
	int   comment_buffer_size=0;  /*!< the amount of storage so far alloc'd for the comment_buffer */

	char *lline_buffer=0;    /*!< A buffer for stuff behind the ; */
	int  lline_buffer_size=0;

	
	cat = ast_config_get_current_category(cfg);

	if (filename[0] == '/') {
		ast_copy_string(fn, filename, sizeof(fn));
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", (char *)ast_config_AST_CONFIG_DIR, filename);
	}

	if (withcomments) {
		CB_INIT(&comment_buffer, &comment_buffer_size, &lline_buffer, &lline_buffer_size);
		if (!lline_buffer || !comment_buffer) {
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
	do {
		if (stat(fn, &statbuf))
			continue;

		if (!S_ISREG(statbuf.st_mode)) {
			ast_log(LOG_WARNING, "'%s' is not a regular file, ignoring\n", fn);
			continue;
		}
		if (option_verbose > 1) {
			ast_verbose(VERBOSE_PREFIX_2 "Parsing '%s': ", fn);
			fflush(stdout);
		}
		if (!(f = fopen(fn, "r"))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "No file to parse: %s\n", fn);
			if (option_verbose > 1)
				ast_verbose( "Not found (%s)\n", strerror(errno));
			continue;
		}
		count++;
		if (option_debug)
			ast_log(LOG_DEBUG, "Parsing %s\n", fn);
		if (option_verbose > 1)
			ast_verbose("Found\n");
		while(!feof(f)) {
			lineno++;
			if (fgets(buf, sizeof(buf), f)) {
				if ( withcomments ) {
					CB_ADD(&comment_buffer, &comment_buffer_size, lline_buffer);       /* add the current lline buffer to the comment buffer */
					lline_buffer[0] = 0;        /* erase the lline buffer */
				}
				
				new_buf = buf;
				if (comment) 
					process_buf = NULL;
				else
					process_buf = buf;
				
				while ((comment_p = strchr(new_buf, COMMENT_META))) {
					if ((comment_p > new_buf) && (*(comment_p-1) == '\\')) {
						/* Escaped semicolons aren't comments. */
						new_buf = comment_p + 1;
					} else if(comment_p[1] == COMMENT_TAG && comment_p[2] == COMMENT_TAG && (comment_p[3] != '-')) {
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
								if ( withcomments ) {
									CB_ADD(&comment_buffer, &comment_buffer_size, ";");
									CB_ADD_LEN(&comment_buffer, &comment_buffer_size, oldptr+1, new_buf-oldptr-1);
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
							if ( withcomments ) {
								LLB_ADD(&lline_buffer, &lline_buffer_size, comment_p);
							}
							*comment_p = '\0'; 
							new_buf = comment_p;
						} else
							new_buf = comment_p + 1;
					}
				}
				if( withcomments && comment && !process_buf )
				{
					CB_ADD(&comment_buffer, &comment_buffer_size, buf);  /* the whole line is a comment, store it */
				}
				
				if (process_buf) {
					char *buf = ast_strip(process_buf);
					if (!ast_strlen_zero(buf)) {
						if (process_text_line(cfg, &cat, buf, lineno, fn, withcomments, &comment_buffer, &comment_buffer_size, &lline_buffer, &lline_buffer_size)) {
							cfg = NULL;
							break;
						}
					}
				}
			}
		}
		fclose(f);		
	} while(0);
	if (comment) {
		ast_log(LOG_WARNING,"Unterminated comment detected beginning on line %d\n", nest[comment - 1]);
	}
#ifdef AST_INCLUDE_GLOB
					if (!cfg)
						break;
				}
				globfree(&globbuf);
			}
		}
#endif

	if (cfg && cfg->include_level == 1 && withcomments && comment_buffer) {
		free(comment_buffer);
		free(lline_buffer);
		comment_buffer = NULL;
		lline_buffer = NULL;
		comment_buffer_size = 0;
		lline_buffer_size = 0;
	}
	
	if (count == 0)
		return NULL;

	return cfg;
}

int config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator)
{
	FILE *f = NULL;
	int fd = -1;
	char fn[256], fntmp[256];
	char date[256]="";
	time_t t;
	struct ast_variable *var;
	struct ast_category *cat;
	struct ast_comment *cmt;
	struct stat s;
	int blanklines = 0;
	int stat_result = 0;

	if (configfile[0] == '/') {
		snprintf(fntmp, sizeof(fntmp), "%s.XXXXXX", configfile);
		ast_copy_string(fn, configfile, sizeof(fn));
	} else {
		snprintf(fntmp, sizeof(fntmp), "%s/%s.XXXXXX", ast_config_AST_CONFIG_DIR, configfile);
		snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_CONFIG_DIR, configfile);
	}
	time(&t);
	ast_copy_string(date, ctime(&t), sizeof(date));
	if ((fd = mkstemp(fntmp)) > 0 && (f = fdopen(fd, "w")) != NULL) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Saving '%s': ", fn);
		fprintf(f, ";!\n");
		fprintf(f, ";! Automatically generated configuration file\n");
		if (strcmp(configfile, fn))
			fprintf(f, ";! Filename: %s (%s)\n", configfile, fn);
		else
			fprintf(f, ";! Filename: %s\n", configfile);
		fprintf(f, ";! Generator: %s\n", generator);
		fprintf(f, ";! Creation Date: %s", date);
		fprintf(f, ";!\n");
		cat = cfg->root;
		while(cat) {
			/* Dump section with any appropriate comment */
			for (cmt = cat->precomments; cmt; cmt=cmt->next)
			{
				if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
					fprintf(f,"%s", cmt->cmt);
			}
			if (!cat->precomments)
				fprintf(f,"\n");
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
			var = cat->root;
			while(var) {
				struct ast_category_template_instance *x;
				struct ast_variable *v2;
				int found = 0;
				AST_LIST_TRAVERSE(&cat->template_instances, x, next) {
					
					for (v2 = x->inst->root; v2; v2 = v2->next) {
						if (!strcasecmp(var->name, v2->name))
							break;
					}
					if (v2 && v2->value && !strcmp(v2->value, var->value)) {
						found = 1;
						break;
					}
				}
				if (found) {
					var = var->next;
					continue;
				}
				for (cmt = var->precomments; cmt; cmt=cmt->next)
				{
					if (cmt->cmt[0] != ';' || cmt->cmt[1] != '!')
						fprintf(f,"%s", cmt->cmt);
				}
				if (var->sameline) 
					fprintf(f, "%s %s %s  %s", var->name, (var->object ? "=>" : "="), var->value, var->sameline->cmt);
				else	
					fprintf(f, "%s %s %s\n", var->name, (var->object ? "=>" : "="), var->value);
				if (var->blanklines) {
					blanklines = var->blanklines;
					while (blanklines--)
						fprintf(f, "\n");
				}
					
				var = var->next;
			}
#if 0
			/* Put an empty line */
			fprintf(f, "\n");
#endif
			cat = cat->next;
		}
		if ((option_verbose > 1) && !option_debug)
			ast_verbose("Saved\n");
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Unable to open for writing: %s (%s)\n", fn, strerror(errno));
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Unable to write %s (%s)", fn, strerror(errno));
		if (fd > -1)
			close(fd);
		return -1;
	}
	if (!(stat_result = stat(fn, &s))) {
		fchmod(fd, s.st_mode);
	}
	fclose(f);
	if ((!stat_result && unlink(fn)) || link(fntmp, fn)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Unable to open for writing: %s (%s)\n", fn, strerror(errno));
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Unable to write %s (%s)", fn, strerror(errno));
		unlink(fntmp);
		return -1;
	}
	unlink(fntmp);
	return 0;
}

static void clear_config_maps(void) 
{
	struct ast_config_map *map;

	ast_mutex_lock(&config_lock);

	while (config_maps) {
		map = config_maps;
		config_maps = config_maps->next;
		free(map);
	}
		
	ast_mutex_unlock(&config_lock);
}

static int append_mapping(char *name, char *driver, char *database, char *table)
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
	map->next = config_maps;

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Binding %s to %s/%s/%s\n",
			    map->name, map->driver, map->database, map->table ? map->table : map->name);

	config_maps = map;
	return 0;
}

int read_config_maps(void) 
{
	struct ast_config *config, *configtmp;
	struct ast_variable *v;
	char *driver, *table, *database, *stringp, *tmp;

	clear_config_maps();

	configtmp = ast_config_new();
	configtmp->max_include_level = 1;
	config = ast_config_internal_load(extconfig_conf, configtmp, 0);
	if (!config) {
		ast_config_destroy(configtmp);
		return 0;
	}

	for (v = ast_variable_browse(config, "settings"); v; v = v->next) {
		stringp = v->value;
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
			append_mapping("sipusers", driver, database, table ? table : "sipfriends");
			append_mapping("sippeers", driver, database, table ? table : "sipfriends");
		} else if (!strcasecmp(v->name, "iaxfriends")) {
			ast_log(LOG_WARNING, "The 'iaxfriends' table is obsolete, update your config to use iaxusers and iaxpeers, though they can point to the same table.\n");
			append_mapping("iaxusers", driver, database, table ? table : "iaxfriends");
			append_mapping("iaxpeers", driver, database, table ? table : "iaxfriends");
		} else 
			append_mapping(v->name, driver, database, table);
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
static struct ast_config_engine *find_engine(const char *family, char *database, int dbsiz, char *table, int tabsiz) 
{
	struct ast_config_engine *eng, *ret = NULL;
	struct ast_config_map *map;

	ast_mutex_lock(&config_lock);

	for (map = config_maps; map; map = map->next) {
		if (!strcasecmp(family, map->name)) {
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

struct ast_config *ast_config_internal_load(const char *filename, struct ast_config *cfg, int withcomments)
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

	if (strcmp(filename, extconfig_conf) && strcmp(filename, "asterisk.conf") && config_engine_list) {
		struct ast_config_engine *eng;

		eng = find_engine(filename, db, sizeof(db), table, sizeof(table));


		if (eng && eng->load_func) {
			loader = eng;
		} else {
			eng = find_engine("global", db, sizeof(db), table, sizeof(table));
			if (eng && eng->load_func)
				loader = eng;
		}
	}

	result = loader->load_func(db, table, filename, cfg, withcomments);

	if (result)
		result->include_level--;
	else
		cfg->include_level--;

	return result;
}

struct ast_config *ast_config_load(const char *filename)
{
	struct ast_config *cfg;
	struct ast_config *result;

	cfg = ast_config_new();
	if (!cfg)
		return NULL;

	result = ast_config_internal_load(filename, cfg, 0);
	if (!result)
		ast_config_destroy(cfg);

	return result;
}

struct ast_config *ast_config_load_with_comments(const char *filename)
{
	struct ast_config *cfg;
	struct ast_config *result;

	cfg = ast_config_new();
	if (!cfg)
		return NULL;

	result = ast_config_internal_load(filename, cfg, 1);
	if (!result)
		ast_config_destroy(cfg);

	return result;
}

struct ast_variable *ast_load_realtime(const char *family, ...)
{
	struct ast_config_engine *eng;
	char db[256]="";
	char table[256]="";
	struct ast_variable *res=NULL;
	va_list ap;

	va_start(ap, family);
	eng = find_engine(family, db, sizeof(db), table, sizeof(table));
	if (eng && eng->realtime_func) 
		res = eng->realtime_func(db, table, ap);
	va_end(ap);

	return res;
}

/*! \brief Check if realtime engine is configured for family */
int ast_check_realtime(const char *family)
{
	struct ast_config_engine *eng;

	eng = find_engine(family, NULL, 0, NULL, 0);
	if (eng)
		return 1;
	return 0;

}

struct ast_config *ast_load_realtime_multientry(const char *family, ...)
{
	struct ast_config_engine *eng;
	char db[256]="";
	char table[256]="";
	struct ast_config *res=NULL;
	va_list ap;

	va_start(ap, family);
	eng = find_engine(family, db, sizeof(db), table, sizeof(table));
	if (eng && eng->realtime_multi_func) 
		res = eng->realtime_multi_func(db, table, ap);
	va_end(ap);

	return res;
}

int ast_update_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	struct ast_config_engine *eng;
	int res = -1;
	char db[256]="";
	char table[256]="";
	va_list ap;

	va_start(ap, lookup);
	eng = find_engine(family, db, sizeof(db), table, sizeof(table));
	if (eng && eng->update_func) 
		res = eng->update_func(db, table, keyfield, lookup, ap);
	va_end(ap);

	return res;
}

static int config_command(int fd, int argc, char **argv) 
{
	struct ast_config_engine *eng;
	struct ast_config_map *map;
	
	ast_mutex_lock(&config_lock);

	ast_cli(fd, "\n\n");
	for (eng = config_engine_list; eng; eng = eng->next) {
		ast_cli(fd, "\nConfig Engine: %s\n", eng->name);
		for (map = config_maps; map; map = map->next)
			if (!strcasecmp(map->driver, eng->name)) {
				ast_cli(fd, "===> %s (db=%s, table=%s)\n", map->name, map->database,
					map->table ? map->table : map->name);
			}
	}
	ast_cli(fd,"\n\n");
	
	ast_mutex_unlock(&config_lock);

	return 0;
}

static char show_config_help[] =
	"Usage: core show config mappings\n"
	"	Shows the filenames to config engines.\n";

static struct ast_cli_entry cli_show_config_mappings_deprecated = {
	{ "show", "config", "mappings", NULL },
	config_command, NULL,
	NULL };

static struct ast_cli_entry cli_config[] = {
	{ { "core", "show", "config", "mappings", NULL },
	config_command, "Display config mappings (file names to config engines)",
	show_config_help, NULL, &cli_show_config_mappings_deprecated },
};

int register_config_cli() 
{
	ast_cli_register_multiple(cli_config, sizeof(cli_config) / sizeof(struct ast_cli_entry));
	return 0;
}
