/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
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
		if (!strcasecmp(cat->name, category))
			return cat->root;
		cat = cat->next;
	}
	return NULL;
}

char *ast_variable_retrieve(struct ast_config *config, char *category, char *value)
{
	struct ast_variable *v;
	v = ast_variable_browse(config, category);
	while (v) {
		if (!strcasecmp(value, v->name))
			return v->value;
		v=v->next;
	}
	return NULL;
}

struct ast_config *ast_load(char *configfile)
{
	char fn[256];
	char buf[256];
	struct ast_config *tmp=NULL;
	struct ast_category *tmpc=NULL;
	struct ast_variable *v, *last=NULL;
	FILE *f;
	char *c, *cur;
	int lineno=0;
	if (configfile[0] == '/') {
		strncpy(fn, configfile, sizeof(fn));
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", AST_CONFIG_DIR, configfile);
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
		tmp = malloc(sizeof(struct ast_config));
		if (!tmp) {
			ast_log(LOG_WARNING, "Out of memory\n");
			fclose(f);
			return NULL;
		}
		tmp->root = NULL;
		while(!feof(f)) {
			fgets(buf, sizeof(buf), f);
			lineno++;
			if (!feof(f)) {
				/* Strip off lines using ; as comment */
				c = strchr(buf, ';');
				if (c)
					*c = '\0';
				cur = strip(buf);
				if (strlen(cur)) {
					/* Actually parse the entry */
					if (cur[0] == '[') {
						/* A category header */
						/* XXX Don't let them use the same category twice XXX */
						c = strchr(cur, ']');
						if (c) {
							*c = 0;
							tmpc = malloc(sizeof(struct ast_category));
							if (!tmpc) {
								ast_destroy(tmp);
								ast_log(LOG_WARNING,
									"Out of memory, line %d\n", lineno);
								fclose(f);
								return NULL;
							}
							strncpy(tmpc->name, cur+1, sizeof(tmpc->name));
							tmpc->root =  NULL;
							tmpc->next = tmp->root;
							tmp->root = tmpc;
							last =  NULL;
						} else {
							ast_log(LOG_WARNING, 
								"parse error: no closing ']', line %d\n", lineno);
							ast_destroy(tmp);
							fclose(f);
							return NULL;
						}
					} else {
						/* Just a line (variable = value) */
						if (!tmpc) {
							ast_log(LOG_WARNING,
								"parse error: No category context for line %d\n", lineno);
							ast_destroy(tmp);
							fclose(f);
							return NULL;
						}
						c = strchr(cur, '=');
						if (c) {
							*c = 0;
							c++;
							v = malloc(sizeof(struct ast_variable));
							if (v) {
								v->next = NULL;
								v->name = strdup(strip(cur));
								v->value = strdup(strip(c));
								if (last)  
									last->next = v;
								else
									tmpc->root = v;
								last = v;
							} else {
								ast_log(LOG_WARNING, "Out of memory, line %d\n", lineno);
								fclose(f);
								ast_destroy(tmp);
							}
						} else {
							ast_log(LOG_WARNING, "No = in line %d\n", lineno);
							fclose(f);
							ast_destroy(tmp);
						}
														
					}
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
