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
#include <time.h>
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
	struct ast_comment *precomments;
	struct ast_comment *sameline;
};

struct ast_config {
	/* Maybe this structure isn't necessary but we'll keep it
	   for now */
	struct ast_category *root;
	struct ast_category *prev;
	struct ast_comment *trailingcomments;
};

struct ast_comment_struct
{
	struct ast_comment *root;
	struct ast_comment *prev;
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

static void free_comments(struct ast_comment *com)
{
	struct ast_comment *l;
	while (com) {
		l = com;
		com = com->next;
		free(l->comment);
		free(l);
	}
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
			free_comments(v->precomments);
			free_comments(v->sameline);
			v = v->next;
			free(vn);
		}
		catn = cat;
		free_comments(cat->precomments);
		free_comments(cat->sameline);
		cat = cat->next;
		free(catn);
	}
	free_comments(ast->trailingcomments);
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

int ast_variable_delete(struct ast_config *cfg, char *category, char *variable, char *value)
{
	struct ast_variable *v, *pv, *bv, *bpv;
	struct ast_category *cat;
	cat = cfg->root;
	while(cat) {
		if (cat->name == category) {
			break;
		}
		cat = cat->next;
	}
	if (!cat) {
		cat = cfg->root;
		while(cat) {
			if (!strcasecmp(cat->name, category)) {
				break;
			}
			cat = cat->next;
		}
	}
	if (!cat)
		return -1;
	v = cat->root;
	pv = NULL;
	while (v) {
		if ((variable == v->name) && (!value || !strcmp(v->value, value)))
			break;
		pv = v;
		v=v->next;
	}
	if (!v) {
		/* Get the last one that looks like it */
		bv = NULL;
		bpv = NULL;
		v = cat->root;
		pv = NULL;
		while (v) {
			if (!strcasecmp(variable, v->name) && (!value || !strcmp(v->value, value))) {
				bv = v;
				bpv = pv;
			}
			pv = v;
			v=v->next;
		}
		v = bv;
	}

	if (v) {
		/* Unlink from original position */
		if (pv) 
			pv->next = v->next;
		else
			cat->root = v->next;
		v->next = NULL;
		free(v->name);
		if (v->value)
			free(v->value);
		free_comments(v->sameline);
		free_comments(v->precomments);
		return 0;
	}
	return -1;
}

int ast_category_delete(struct ast_config *cfg, char *category)
{
	struct ast_variable *v, *pv;
	struct ast_category *cat, *cprev;
	cat = cfg->root;
	cprev = NULL;
	while(cat) {
		if (cat->name == category) {
			break;
		}
		cprev = cat;
		cat = cat->next;
	}
	if (!cat) {
		cat = cfg->root;
		cprev = NULL;
		while(cat) {
			if (!strcasecmp(cat->name, category)) {
				break;
			}
			cprev = cat;
			cat = cat->next;
		}
	}
	if (!cat)
		return -1;
	/* Unlink it */
	if (cprev)
		cprev->next = cat->next;
	else
		cfg->root = cat->next;
	v = cat->root;
	while (v) {
		pv = v;
		v=v->next;
		if (pv->value)
			free(pv->value);
		if (pv->name)
			free(pv->name);
		free_comments(pv->sameline);
		free_comments(pv->precomments);
		free(pv);
	}
	free_comments(cat->sameline);
	free_comments(cat->precomments);
	free(cat);
	return 0;
}

struct ast_variable *ast_variable_append_modify(struct ast_config *config, char *category, char *variable, char *value, int newcat, int newvar, int move)
{
	struct ast_variable *v, *pv, *bv, *bpv;
	struct ast_category *cat, *pcat;
	cat = config->root;
	if (!newcat) {
		while(cat) {
			if (cat->name == category) {
				break;
			}
			cat = cat->next;
		}
		if (!cat) {
			cat = config->root;
			while(cat) {
				if (!strcasecmp(cat->name, category)) {
					break;
				}
				cat = cat->next;
			}
		}
	}
	if (!cat) {
		cat = malloc(sizeof(struct ast_category));
		if (!cat)
			return NULL;
		memset(cat, 0, sizeof(struct ast_category));
		strncpy(cat->name, category, sizeof(cat->name));
		if (config->root) {
			/* Put us at the end */
			pcat = config->root;
			while(pcat->next)
				pcat = pcat->next;
			pcat->next = cat;
		} else {
			/* We're the first one */
			config->root = cat;
		}
			
	}
	if (!newvar) {
		v = cat->root;
		pv = NULL;
		while (v) {
			if (variable == v->name)
				break;
			pv = v;
			v=v->next;
		}
		if (!v) {
			/* Get the last one that looks like it */
			bv = NULL;
			bpv = NULL;
			v = cat->root;
			pv = NULL;
			while (v) {
				if (!strcasecmp(variable, v->name)) {
					bv = v;
					bpv = pv;
				}
				pv = v;
				v=v->next;
			}
			v = bv;
		}
	} else v = NULL;
	if (v && move) {
		/* Unlink from original position */
		if (pv) 
			pv->next = v->next;
		else
			cat->root = v->next;
		v->next = NULL;
	}
	if (!v) {
		v = malloc(sizeof(struct ast_variable));
		if (!v)
			return NULL;
		memset(v, 0, sizeof(struct ast_variable));
		v->name = strdup(variable);
		move = 1;
	}
	if (v->value)
		free(v->value);
	if (value)
		v->value = strdup(value);
	else
		v->value = strdup("");
	if (move) {
		if (cat->root) {
			pv = cat->root;
			while (pv->next) 
				pv = pv->next;
			pv->next = v;
		} else {
			cat->root = v;
		}
	}
	return v;
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

static struct ast_comment *build_comment(char *cmt)
{
	struct ast_comment *c;
	c = malloc(sizeof(struct ast_comment));
	if (c) {
		memset(c, 0, sizeof(struct ast_comment));
		c->comment = strdup(cmt);
	}
	return c;
}

static struct ast_config *__ast_load(char *configfile, struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, int includelevel, struct ast_comment_struct *acs);
static int cfg_process(struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, char *buf, int lineno, char *configfile, int includelevel, struct ast_comment_struct *acs)
{
	char *c;
	char *cur;
	struct ast_variable *v;
	struct ast_comment *com = NULL;
	int object;
	/* Strip off lines using ; as comment */
	c = strchr(buf, ';');
	if (c) {
		*c = '\0';
		c++;
		if (*c != '!')
			com = build_comment(c);
	}
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
				(*_tmpc)->precomments = acs->root;
				(*_tmpc)->sameline = com;
				if (!tmp->prev)
					tmp->root = *_tmpc;
				else
					tmp->prev->next = *_tmpc;

				tmp->prev = *_tmpc;
				acs->root = NULL;
				acs->prev = NULL;
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
						__ast_load(cur, tmp, _tmpc, _last, includelevel + 1, acs);
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
				if (*c== '>') {
					object = 1;
					c++;
				} else
					object = 0;
				v = malloc(sizeof(struct ast_variable));
				if (v) {
					memset(v, 0, sizeof(struct ast_variable));
					v->next = NULL;
					v->name = strdup(strip(cur));
					v->value = strdup(strip(c));
					v->lineno = lineno;
					v->object = object;
					/* Put and reset comments */
					v->precomments = acs->root;
					v->blanklines = 0;
					acs->prev = NULL;
					acs->root = NULL;
					v->sameline = com;
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
	} else {
		/* store any comments if there are any */
		if (com) {
			if (acs->prev)
				acs->prev->next = com;
			else
				acs->root = com;
			acs->prev = com;
		} else {
		if (*_last) 
			(*_last)->blanklines++;

		}
	}
	return 0;
}

static void dump_comments(FILE *f, struct ast_comment *comment)
{
	while (comment) {
		fprintf(f, ";%s", comment->comment);
		comment = comment->next;
	}
}

int ast_save(char *configfile, struct ast_config *cfg, char *generator)
{
	FILE *f;
	char fn[256];
	char date[256];
	time_t t;
	struct ast_variable *var;
	struct ast_category *cat;
	int blanklines = 0;
	if (configfile[0] == '/') {
		strncpy(fn, configfile, sizeof(fn)-1);
	} else {
		snprintf(fn, sizeof(fn), "%s/%s", AST_CONFIG_DIR, configfile);
	}
	time(&t);
	strncpy(date, ctime(&t), sizeof(date));
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
			/* Dump any precomments */
			dump_comments(f, cat->precomments);
			/* Dump section with any appropriate comment */
			if (cat->sameline) 
				fprintf(f, "[%s]  ; %s\n", cat->name, cat->sameline->comment);
			else
				fprintf(f, "[%s]\n", cat->name);
			var = cat->root;
			while(var) {
				dump_comments(f, var->precomments);
				if (var->sameline) 
					fprintf(f, "%s %s %s  ; %s\n", var->name, (var->object ? "=>" : "="), var->value, var->sameline->comment);
				else	
					fprintf(f, "%s %s %s\n", var->name, (var->object ? "=>" : "="), var->value);
				if (var->blanklines) {
					blanklines = var->blanklines;
					while (blanklines) {
						fprintf(f, "\n");
						blanklines--;
					}
				}
					
				var = var->next;
			}
#if 0
			/* Put an empty line */
			fprintf(f, "\n");
#endif
			cat = cat->next;
		}
		dump_comments(f, cfg->trailingcomments);
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

static struct ast_config *__ast_load(char *configfile, struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, int includelevel, struct ast_comment_struct *acs)
{
	char fn[256];
	char buf[256];
	FILE *f;
	int lineno=0;
	int master=0;

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

			master = 1;
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
				if (cfg_process(tmp, _tmpc, _last, buf, lineno, configfile, includelevel, acs)) {
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
	if (master) {
		/* Keep trailing comments */
		tmp->trailingcomments = acs->root;
		acs->root = NULL;
		acs->prev = NULL;
	}
	return tmp;
}

struct ast_config *ast_load(char *configfile)
{
	struct ast_category *tmpc=NULL;
	struct ast_variable *last = NULL;
	struct ast_comment_struct acs = { NULL, NULL };
	return __ast_load(configfile, NULL, &tmpc, &last, 0, &acs);
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
