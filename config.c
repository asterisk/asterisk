/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#define AST_INCLUDE_GLOB 1
#ifdef AST_INCLUDE_GLOB
#ifdef __OSX__
#define GLOB_ABORTED GLOB_ABEND
#endif
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
#include "asterisk.h"
#include "astconf.h"

#define MAX_NESTED_COMMENTS 128
#define COMMENT_START ";--"
#define COMMENT_END "--;"
#define COMMENT_META ';'
#define COMMENT_TAG '-'

static char *extconfig_conf = "extconfig.conf";

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

struct ast_comment {
	struct ast_comment *next;
	char cmt[0];
};

struct ast_category {
	char name[80];
	int ignored;			/* do not let user of the config see this category */
	struct ast_variable *root;
	struct ast_variable *last;
	struct ast_category *next;
};

struct ast_config {
	struct ast_category *root;
	struct ast_category *last;
	struct ast_category *current;
	struct ast_category *last_browse;		/* used to cache the last category supplied via category_browse */
	int include_level;
	int max_include_level;
};

int ast_true(const char *s)
{
	if (!s)
		return 0;
	/* Determine if this is a true value */
	if (!strcasecmp(s, "yes") ||
	    !strcasecmp(s, "true") ||
	    !strcasecmp(s, "y") ||
	    !strcasecmp(s, "t") ||
	    !strcasecmp(s, "1") ||
	    !strcasecmp(s, "on"))
		return -1;
	return 0;
}

int ast_false(const char *s)
{
	if (!s)
		return 0;
	/* Determine if this is a false value */
	if (!strcasecmp(s, "no") ||
	    !strcasecmp(s, "false") ||
	    !strcasecmp(s, "n") ||
	    !strcasecmp(s, "f") ||
	    !strcasecmp(s, "0") ||
	    !strcasecmp(s, "off"))
		return -1;
	return 0;
}

struct ast_variable *ast_variable_new(const char *name, const char *value) 
{
	struct ast_variable *variable;

	int length = strlen(name) + strlen(value) + 2 + sizeof(struct ast_variable);
	variable = malloc(length);
	if (variable) {
		memset(variable, 0, length);
		variable->name = variable->stuff;
		variable->value = variable->stuff + strlen(name) + 1;		
		strcpy(variable->name,name);
		strcpy(variable->value,value);
	}

	return variable;
}

void ast_variable_append(struct ast_category *category, struct ast_variable *variable)
{
	if (category->last)
		category->last->next = variable;
	else
		category->root = variable;
	category->last = variable;
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

	if (cat)
		return cat->root;
	else
		return NULL;
}

char *ast_variable_retrieve(const struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category) {
		for (v = ast_variable_browse(config, category); v; v = v->next)
			if (variable == v->name)
				return v->value;
		for (v = ast_variable_browse(config, category); v; v = v->next)
			if (!strcasecmp(variable, v->name))
				return v->value;
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
	struct ast_variable *var;
	struct ast_variable *next;

	next = old->root;
	old->root = NULL;
	for (var = next; var; var = next) {
		next = var->next;
		var->next = NULL;
		ast_variable_append(new, var);
	}
}

struct ast_category *ast_category_new(const char *name) 
{
	struct ast_category *category;

	category = malloc(sizeof(struct ast_category));
	if (category) {
		memset(category, 0, sizeof(struct ast_category));
		strncpy(category->name, name, sizeof(category->name) - 1);
	}

	return category;
}

static struct ast_category *category_get(const struct ast_config *config, const char *category_name, int ignored)
{
	struct ast_category *cat;

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
	config->last = category;
	config->current = category;
}

void ast_category_destroy(struct ast_category *cat)
{
	ast_variables_destroy(cat->root);
	free(cat);
}

static struct ast_category *next_available_category(struct ast_category *cat)
{
	for (; cat && cat->ignored; cat = cat->next);

	return cat;
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
	if (cat)
		return cat->name;
	else
		return NULL;
}

struct ast_variable *ast_category_detach_variables(struct ast_category *cat)
{
	struct ast_variable *v;

	v = cat->root;
	cat->root = NULL;

	return v;
}

void ast_category_rename(struct ast_category *cat, const char *name)
{
	strncpy(cat->name, name, sizeof(cat->name) - 1);
}

static void inherit_category(struct ast_category *new, const struct ast_category *base)
{
	struct ast_variable *var;

	for (var = base->root; var; var = var->next) {
		struct ast_variable *v;
		
		v = variable_clone(var);
		if (v)
			ast_variable_append(new, v);
	}
}

struct ast_config *ast_config_new(void) 
{
	struct ast_config *config;

	config = malloc(sizeof(*config));
	if (config) {
		memset(config, 0, sizeof(*config));
		config->max_include_level = MAX_INCLUDE_LEVEL;
	}

	return config;
}

void ast_config_destroy(struct ast_config *cfg)
{
	struct ast_category *cat, *catn;

	if (!cfg)
		return;

	cat = cfg->root;
	while(cat) {
		ast_variables_destroy(cat->root);
		catn = cat;
		cat = cat->next;
		free(catn);
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

static int process_text_line(struct ast_config *cfg, struct ast_category **cat, char *buf, int lineno, const char *configfile)
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
		*cat = newcat = ast_category_new(catname);
		if (!newcat) {
			ast_log(LOG_WARNING, "Out of memory, line %d of %s\n", lineno, configfile);
			return -1;
		}
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
					if (!*cat) {
						ast_destroy(cfg);
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
			c++;
			/* Find real argument */
			while(*c  && (*c < 33)) c++;
			if (!*c)
				c = NULL;
		} else 
			c = NULL;
		do_include = !strcasecmp(cur, "include");
		if(!do_include)
			do_exec = !strcasecmp(cur, "exec");
		else
			do_exec = 0;
		if (do_exec && !option_exec_includes) {
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
					snprintf(exec_file, sizeof(exec_file), "/var/tmp/exec.%ld.%ld", time(NULL), (long)pthread_self());
					snprintf(cmd, sizeof(cmd), "%s > %s 2>&1", cur, exec_file);
					ast_safe_system(cmd);
					cur = exec_file;
				} else
					exec_file[0] = '\0';
				/* A #include */
				do_include = ast_config_internal_load(cur, cfg) ? 1 : 0;
				if(!ast_strlen_zero(exec_file))
					unlink(exec_file);
				if(!do_include)
					return -1;

			} else {
				ast_log(LOG_WARNING, "Directive '#%s' needs an argument (%s) at line %d of %s\n", 
						do_exec ? "exec" : "include",
						do_exec ? "/path/to/executable" : "filename",
						lineno,
						configfile);
			}
		}
		else 
			ast_log(LOG_WARNING, "Unknown directive '%s' at line %d of %s\n", cur, lineno, configfile);
	} else {
		/* Just a line (variable = value) */
		if (!*cat) {
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
			v = ast_variable_new(ast_strip(cur), ast_strip(c));
			if (v) {
				v->lineno = lineno;
				v->object = object;
				/* Put and reset comments */
				v->blanklines = 0;
				ast_variable_append(*cat, v);
			} else {
				ast_log(LOG_WARNING, "Out of memory, line %d\n", lineno);
				return -1;
			}
		} else {
			ast_log(LOG_WARNING, "No '=' (equal sign) in line %d of %s\n", lineno, configfile);
		}

	}
	return 0;
}

static struct ast_config *config_text_file_load(const char *database, const char *table, const char *filename, struct ast_config *cfg)
{
	char fn[256];
	char buf[8192];
	char *new_buf, *comment_p, *process_buf;
	FILE *f;
	int lineno=0;
	int comment = 0, nest[MAX_NESTED_COMMENTS];
	struct ast_category *cat = NULL;
	
	cat = ast_config_get_current_category(cfg);

	if (filename[0] == '/') {
		strncpy(fn, filename, sizeof(fn)-1);
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", (char *)ast_config_AST_CONFIG_DIR, filename);
	}

#ifdef AST_INCLUDE_GLOB
	{
		int glob_ret;
		glob_t globbuf;
		globbuf.gl_offs = 0;	/* initialize it to silence gcc */
#ifdef SOLARIS
		glob_ret = glob(fn, GLOB_NOCHECK, NULL, &globbuf);
#else
		glob_ret = glob(fn, GLOB_NOMAGIC|GLOB_BRACE, NULL, &globbuf);
#endif
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
				strncpy(fn, globbuf.gl_pathv[i], sizeof(fn)-1);
#endif
	if ((option_verbose > 1) && !option_debug) {
		ast_verbose(  VERBOSE_PREFIX_2 "Parsing '%s': ", fn);
		fflush(stdout);
	}
	if ((f = fopen(fn, "r"))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Parsing %s\n", fn);
		else if (option_verbose > 1)
			ast_verbose("Found\n");
		while(!feof(f)) {
			lineno++;
			if (fgets(buf, sizeof(buf), f)) {
				new_buf = buf;
				if (comment)
					process_buf = NULL;
				else
					process_buf = buf;
				while ((comment_p = strchr(new_buf, COMMENT_META))) {
					if ((comment_p > new_buf) && (*(comment_p-1) == '\\')) {
						/* Yuck, gotta memmove */
						memmove(comment_p - 1, comment_p, strlen(comment_p) + 1);
						new_buf = comment_p;
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
								memmove(oldptr, new_buf, strlen(new_buf) + 1);
								new_buf = oldptr;
							} else
								process_buf = new_buf;
						}
					} else {
						if (!comment) {
							/* If ; is found, and we are not nested in a comment, 
							   we immediately stop all comment processing */
							*comment_p = '\0'; 
							new_buf = comment_p;
						} else
							new_buf = comment_p + 1;
					}
				}
				if (process_buf) {
					char *buf = ast_strip(process_buf);
					if (!ast_strlen_zero(buf))
						if (process_text_line(cfg, &cat, buf, lineno, filename)) {
							cfg = NULL;
							break;
						}
				}
			}
		}
		fclose(f);		
	} else { /* can't open file */
		if (option_debug)
			ast_log(LOG_DEBUG, "No file to parse: %s\n", fn);
		else if (option_verbose > 1)
			ast_verbose( "Not found (%s)\n", strerror(errno));
	}
	if (comment) {
		ast_log(LOG_WARNING,"Unterminated comment detected beginning on line %d\n", nest[comment]);
	}
#ifdef AST_INCLUDE_GLOB
					if (!cfg)
						break;
				}
				globfree(&globbuf);
			}
		}
#endif

	return cfg;
}

int config_text_file_save(const char *configfile, const struct ast_config *cfg, const char *generator)
{
	FILE *f;
	char fn[256];
	char date[256]="";
	time_t t;
	struct ast_variable *var;
	struct ast_category *cat;
	int blanklines = 0;

	if (configfile[0] == '/') {
		strncpy(fn, configfile, sizeof(fn)-1);
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", ast_config_AST_CONFIG_DIR, configfile);
	}
	time(&t);
	strncpy(date, ctime(&t), sizeof(date) - 1);
	if ((f = fopen(fn, "w"))) {
		if ((option_verbose > 1) && !option_debug)
			ast_verbose(  VERBOSE_PREFIX_2 "Saving '%s': ", fn);
		fprintf(f, ";!\n");
		fprintf(f, ";! Automatically generated configuration file\n");
		fprintf(f, ";! Filename: %s (%s)\n", configfile, fn);
		fprintf(f, ";! Generator: %s\n", generator);
		fprintf(f, ";! Creation Date: %s", date);
		fprintf(f, ";!\n");
		cat = cfg->root;
		while(cat) {
			/* Dump section with any appropriate comment */
			fprintf(f, "[%s]\n", cat->name);
			var = cat->root;
			while(var) {
				if (var->sameline) 
					fprintf(f, "%s %s %s  ; %s\n", var->name, (var->object ? "=>" : "="), var->value, var->sameline->cmt);
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
	} else {
		if (option_debug)
			printf("Unable to open for writing: %s\n", fn);
		else if (option_verbose > 1)
			printf( "Unable to write (%s)", strerror(errno));
		return -1;
	}
	fclose(f);
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
	map = malloc(length);

	if (!map)
		return -1;

	memset(map, 0, length);
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

void read_config_maps(void) 
{
	struct ast_config *config;
	struct ast_variable *v;
	char *driver, *table, *database, *stringp;

	clear_config_maps();

	config = ast_config_new();
	config->max_include_level = 1;
	config = ast_config_internal_load(extconfig_conf, config);
	if (!config)
		return;

	for (v = ast_variable_browse(config, "settings"); v; v = v->next) {
		stringp = v->value;
		driver = strsep(&stringp, ",");
		database = strsep(&stringp, ",");
		table = strsep(&stringp, ",");
			
		if (!strcmp(v->name, extconfig_conf) || !strcmp(v->name, "asterisk.conf")) {
			ast_log(LOG_WARNING, "Cannot bind asterisk.conf or extconfig.conf!\n");
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

static struct ast_config_engine *find_engine(const char *filename, char *database, int dbsiz, char *table, int tabsiz) 
{
	struct ast_config_engine *eng, *ret=NULL;
	struct ast_config_map *map;

	ast_mutex_lock(&config_lock);

	map = config_maps;
	while (map) {
		if (!strcasecmp(filename, map->name)) {
			strncpy(database, map->database, dbsiz-1);
			if (map->table)
				strncpy(table, map->table, tabsiz-1);
			else
				strncpy(table, filename, tabsiz-1);
			break;
		}
		map = map->next;
	}
	if (map) {
		for (eng = config_engine_list; eng; eng = eng->next) {
			if (!strcmp(eng->name, map->driver)) {
				ret = eng;
				break;
			}
		}
	}
	ast_mutex_unlock(&config_lock);

	return ret;
}

static struct ast_config_engine text_file_engine = {
	.name = "text",
	.load_func = config_text_file_load,
};

struct ast_config *ast_config_internal_load(const char *filename, struct ast_config *cfg)
{
	char db[256];
	char table[256];
	struct ast_config_engine *loader = &text_file_engine;
	struct ast_config *result;

	if (cfg->include_level == cfg->max_include_level) {
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

	result = loader->load_func(db, table, filename, cfg);

	if (result)
		result->include_level--;

	return result;
}

struct ast_config *ast_config_load(const char *filename)
{
	struct ast_config *cfg;
	struct ast_config *result;

	cfg = ast_config_new();
	if (!cfg)
		return NULL;

	result = ast_config_internal_load(filename, cfg);
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
				break;
			}
	}
	ast_cli(fd,"\n\n");
	
	ast_mutex_unlock(&config_lock);

	return 0;
}

static char show_config_help[] =
	"Usage: show config mappings\n"
	"	Shows the filenames to config engines.\n";

static struct ast_cli_entry config_command_struct = {
	{ "show", "config", "mappings", NULL }, config_command, "Show Config mappings (file names to config engines)", show_config_help, NULL
};

int register_config_cli() 
{
	return ast_cli_register(&config_command_struct);
}
