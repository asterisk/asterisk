/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>
#include "asterisk.h"
#include "astconf.h"

#define MAX_INCLUDE_LEVEL 10

struct ast_category {
	char name[80];
	struct ast_variable *root;
	struct ast_category *next;
};

struct ast_config {
	/* Maybe this structure isn't necessary but we'll keep it
	   for now */
	struct ast_category *root;
};

static char *strip(char *buf)
{
	char *start;
	/* Strip off trailing whitespace, returns, etc */
	while(strlen(buf) && (buf[strlen(buf)-1]<33))
		buf[strlen(buf)-1] = '\0';
	start = buf;
	/* Strip off leading whitespace, returns, etc */
	while(*start && (*start < 33))
		*start++ = '\0';
	return start;
}

void ast_destroy(struct ast_config *ast)
{
	struct ast_category *cat, *catn;
	struct ast_variable *v, *vn;

	if (!ast)
		return;

	cat = ast->root;
	while(cat) {
		v = cat->root;
		while(v) {
			vn = v;
			free(v->name);
			free(v->value);
			v = v->next;
			free(vn);
		}
		catn = cat;
		cat = cat->next;
		free(catn);
	}
	free(ast);
}

int ast_true(char *s)
{
	if (!s)
		return 0;
	/* Determine if this is a true value */
	if (!strcasecmp(s, "yes") ||
	    !strcasecmp(s, "true") ||
		!strcasecmp(s, "y") ||
		!strcasecmp(s, "t") ||
		!strcasecmp(s, "1"))
			return -1;
	return 0;
}

struct ast_variable *ast_variable_browse(struct ast_config *config, char *category)
{
	struct ast_category *cat;
	cat = config->root;
	while(cat) {
		if (cat->name == category)
			return cat->root;
		cat = cat->next;
	}
	cat = config->root;
	while(cat) {
		if (!strcasecmp(cat->name, category))
			return cat->root;
		cat = cat->next;
	}
	return NULL;
}

char *ast_variable_retrieve(struct ast_config *config, char *category, char *value)
{
	struct ast_variable *v;
	if (category) {
		v = ast_variable_browse(config, category);
		while (v) {
			if (value == v->name)
				return v->value;
			v=v->next;
		}
		v = ast_variable_browse(config, category);
		while (v) {
			if (!strcasecmp(value, v->name))
				return v->value;
			v=v->next;
		}
	} else {
		struct ast_category *cat;
		cat = config->root;
		while(cat) {
			v = cat->root;
			while (v) {
				if (!strcasecmp(value, v->name))
					return v->value;
				v=v->next;
			}
			cat = cat->next;
		}
	}
	return NULL;
}

int ast_category_exist(struct ast_config *config, char *category_name)
{
	struct ast_category *category = NULL;

	category = config->root;

	while(category) {
		if (!strcasecmp(category->name,category_name)) 
			return 1;
		category = category->next;
	} 

	return 0;
}

static struct ast_config *__ast_load(char *configfile, struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, int includelevel);
static int cfg_process(struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, char *buf, int lineno, char *configfile, int includelevel)
{
	char *c;
	char *cur;
	struct ast_variable *v;
	/* Strip off lines using ; as comment */
	c = strchr(buf, ';');
	if (c)
		*c = '\0';
	cur = strip(buf);
	if (strlen(cur)) {
		/* Actually parse the entry */
		if (cur[0] == '[') {
			/* A category header */
			c = strchr(cur, ']');
			if (c) {
				*c = 0;
				*_tmpc = malloc(sizeof(struct ast_category));
				if (!*_tmpc) {
					ast_destroy(tmp);
					ast_log(LOG_WARNING,
						"Out of memory, line %d\n", lineno);
					return -1;
				}
				memset(*_tmpc, 0, sizeof(struct ast_category));
				strncpy((*_tmpc)->name, cur+1, sizeof((*_tmpc)->name) - 1);
				(*_tmpc)->root =  NULL;
				(*_tmpc)->next = tmp->root;
				tmp->root = *_tmpc;
				*_last =  NULL;
			} else {
				ast_log(LOG_WARNING, 
					"parse error: no closing ']', line %d of %s\n", lineno, configfile);
			}
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
			if (!strcasecmp(cur, "include")) {
				/* A #include */
				if (c) {
					while((*c == '<') || (*c == '>') || (*c == '\"')) c++;
					/* Get rid of leading mess */
					cur = c;
					while(strlen(cur)) {
						c = cur + strlen(cur) - 1;
						if ((*c == '>') || (*c == '<') || (*c == '\"'))
							*c = '\0';
						else
							break;
					}
					if (includelevel < MAX_INCLUDE_LEVEL) {
						__ast_load(cur, tmp, _tmpc, _last, includelevel + 1);
					} else 
						ast_log(LOG_WARNING, "Maximum Include level (%d) exceeded\n", includelevel);
				} else
					ast_log(LOG_WARNING, "Directive '#include' needs an argument (filename) at line %d of %s\n", lineno, configfile);
				/* Strip off leading and trailing "'s and <>'s */
			} else 
				ast_log(LOG_WARNING, "Unknown directive '%s' at line %d of %s\n", cur, lineno, configfile);
		} else {
			/* Just a line (variable = value) */
			if (!*_tmpc) {
				ast_log(LOG_WARNING,
					"parse error: No category context for line %d of %s\n", lineno, configfile);
				ast_destroy(tmp);
				return -1;
			}
			c = strchr(cur, '=');
			if (c) {
				*c = 0;
				c++;
				/* Ignore > in => */
				if (*c== '>')
					c++;
				v = malloc(sizeof(struct ast_variable));
				if (v) {
					memset(v, 0, sizeof(struct ast_variable));
					v->next = NULL;
					v->name = strdup(strip(cur));
					v->value = strdup(strip(c));
					v->lineno = lineno;
					if (*_last)  
						(*_last)->next = v;
					else
						(*_tmpc)->root = v;
					*_last = v;
				} else {
					ast_destroy(tmp);
					ast_log(LOG_WARNING, "Out of memory, line %d\n", lineno);
					return -1;
				}
			} else {
				ast_log(LOG_WARNING, "No '=' (equal sign) in line %d of %s\n", lineno, configfile);
			}
														
		}
	}
	return 0;
}

static struct ast_config *__ast_load(char *configfile, struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, int includelevel)
{
	char fn[256];
	char buf[256];
	FILE *f;
	int lineno=0;

	if (configfile[0] == '/') {
		strncpy(fn, configfile, sizeof(fn)-1);
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", (char *)ast_config_AST_CONFIG_DIR, configfile);
	}
	if ((option_verbose > 1) && !option_debug) {
		ast_verbose(  VERBOSE_PREFIX_2 "Parsing '%s': ", fn);
		fflush(stdout);
	}
	if ((f = fopen(fn, "r"))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Parsing %s\n", fn);
		else if (option_verbose > 1)
			ast_verbose( "Found\n");
		if (!tmp) {
			tmp = malloc(sizeof(struct ast_config));
			if (tmp)
				memset(tmp, 0, sizeof(struct ast_config));
		}
		if (!tmp) {
			ast_log(LOG_WARNING, "Out of memory\n");
			fclose(f);
			return NULL;
		}
		while(!feof(f)) {
			fgets(buf, sizeof(buf), f);
			lineno++;
			if (!feof(f)) {
				if (cfg_process(tmp, _tmpc, _last, buf, lineno, configfile, includelevel)) {
					fclose(f);
					return NULL;
				}
			}
		}
		fclose(f);		
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "No file to parse: %s\n", fn);
		else if (option_verbose > 1)
			ast_verbose( "Not found (%s)", strerror(errno));
	}
	return tmp;
}

struct ast_config *ast_load(char *configfile)
{
	struct ast_category *tmpc=NULL;
	struct ast_variable *last = NULL;
	return __ast_load(configfile, NULL, &tmpc, &last, 0);
}

char *ast_category_browse(struct ast_config *config, char *prev)
{	
	struct ast_category *cat;
	if (!prev) {
		if (config->root)
			return config->root->name;
		else
			return NULL;
	}
	cat = config->root;
	while(cat) {
		if (cat->name == prev) {
			if (cat->next)
				return cat->next->name;
			else
				return NULL;
		}
		cat = cat->next;
	}
	cat = config->root;
	while(cat) {
		if (!strcasecmp(cat->name, prev)) {
			if (cat->next)
				return cat->next->name;
			else
				return NULL;
		}
		cat = cat->next;
	}
	return NULL;
}
