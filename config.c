/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
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
#include <asterisk/config.h>
#include <asterisk/config_pvt.h>
#include <asterisk/cli.h>
#include <asterisk/lock.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>
#include <asterisk/utils.h>
#include "asterisk.h"
#include "astconf.h"


static int ast_cust_config=0;
struct ast_config *(*global_load_func)(const char *dbname, const char *table, const char *, struct ast_config *,struct ast_category **,struct ast_variable **,int);

static struct ast_config_map {
	struct ast_config_map *next;
	char *name;
	char *driver;
	char *database;
	char *table;
	char stuff[0];
} *maps = NULL;

AST_MUTEX_DEFINE_STATIC(ast_cust_config_lock);
static struct ast_config_reg *ast_cust_config_list;
static char *config_conf_file = "extconfig.conf";

void ast_destroy_realtime(struct ast_variable *v)
{
	struct ast_variable *vn;
	while(v) {
		vn = v;
		v = v->next;
		free(vn);
	}
}

void ast_category_destroy(struct ast_category *cat)
{
	ast_destroy_realtime(cat->root);
	free(cat);
}

void ast_destroy(struct ast_config *ast)
{
	struct ast_category *cat, *catn;

	if (!ast)
		return;

	cat = ast->root;
	while(cat) {
		ast_destroy_realtime(cat->root);
		catn = cat;
		cat = cat->next;
		free(catn);
	}
	free(ast);
}

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


static struct ast_config_reg *get_ast_cust_config_keyword(const char *name, char *database, int dbsiz, char *table, int tabsiz) 
{
	struct ast_config_reg *reg,*ret=NULL;
	struct ast_config_map *map;

	ast_mutex_lock(&ast_cust_config_lock);
	map = maps;
	while(map) {
		if (!strcasecmp(name, map->name)) {
			strncpy(database, map->database, dbsiz - 1);
			if (map->table)
				strncpy(table, map->table, tabsiz - 1);
			else
				strncpy(table, name, tabsiz - 1);
			break;
		}
		map = map->next;
	}
	if (map) {
		for (reg=ast_cust_config_list;reg && !ret;reg=reg->next) {
			if (!strcmp(reg->name,map->driver))
				ret=reg;
		}
	}
	ast_mutex_unlock(&ast_cust_config_lock);
	return ret;
}

void ast_config_destroy_all(void) 
{
	struct ast_config_reg *key;
	ast_mutex_lock(&ast_cust_config_lock);
	for (key=ast_cust_config_list;key;key=key->next) {
		ast_config_deregister(key);
	}
	ast_cust_config_list = NULL;
	ast_mutex_unlock(&ast_cust_config_lock);
}

static struct ast_config_reg *get_config_registrations(void) 
{
	return ast_cust_config_list;
}


static struct ast_config *__ast_load(const char *configfile, struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, int includelevel);

static int cfg_process(struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, char *buf, int lineno, const char *configfile, int includelevel )
{
	char *c;
	char *cur;
	char *arg=NULL;
	struct ast_variable *v;
	int object;
	/* Strip off lines using ; as comment */
	c = strchr(buf, ';');
	while (c) {
		if ((c == buf) || (*(c-1) != '\\')) {
			*c = '\0';
		} else {
			*(c-1) = ';';
			memmove(c, c + 1, strlen(c + 1));
		}
		c = strchr(c + 1, ';');
	}
	cur = ast_strip(buf);
	if (!ast_strlen_zero(cur)) {
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
				if (!tmp->prev)
					tmp->root = *_tmpc;
				else
					tmp->prev->next = *_tmpc;

				tmp->prev = *_tmpc;
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
					while (!ast_strlen_zero(cur)) {
						c = cur + strlen(cur) - 1;
						if ((*c == '>') || (*c == '<') || (*c == '\"'))
							*c = '\0';
						else
							break;
					}
					
					if((c = strchr(cur,':'))) {
						*c = '\0';
						c++;
						arg = c;
					}
					
					if (includelevel < MAX_INCLUDE_LEVEL) {
						if(arg && cur) {
							ast_log(LOG_WARNING, "Including files with explicit config engine no longer permitted.  Please use extconfig.conf to specify all mappings\n");
						} else {
							__ast_load(cur, tmp, _tmpc, _last, includelevel + 1);
						}
					} else
						ast_log(LOG_WARNING, "Maximum Include level (%d) exceeded\n", includelevel);
				} else
					ast_log(LOG_WARNING, "Directive '#include' needs an argument (filename) at line %d of %s\n", lineno, configfile);
				/* Strip off leading and trailing "'s and <>'s */
			}
			else 
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
				v = ast_new_variable(ast_strip(cur), ast_strip(c));
				if (v) {
					v->next = NULL;
					v->lineno = lineno;
					v->object = object;
					/* Put and reset comments */
					v->blanklines = 0;
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

int ast_save(char *configfile, struct ast_config *cfg, char *generator)
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
		snprintf(fn, sizeof(fn), "%s/%s", AST_CONFIG_DIR, configfile);
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


struct ast_variable *ast_load_realtime(const char *family, ...)
{
	struct ast_config_reg *reg;
	char db[256]="";
	char table[256]="";
	struct ast_variable *res=NULL;
	va_list ap;
	va_start(ap, family);
	reg = get_ast_cust_config_keyword(family, db, sizeof(db), table, sizeof(table));
	if (reg && reg->realtime_func) 
		res = reg->realtime_func(db, table, ap);
	va_end(ap);
	return res;
}

struct ast_config *ast_load_realtime_multientry(const char *family, ...)
{
	struct ast_config_reg *reg;
	char db[256]="";
	char table[256]="";
	struct ast_config *res=NULL;
	va_list ap;
	va_start(ap, family);
	reg = get_ast_cust_config_keyword(family, db, sizeof(db), table, sizeof(table));
	if (reg && reg->realtime_multi_func) 
		res = reg->realtime_multi_func(db, table, ap);
	va_end(ap);
	return res;
}

int ast_update_realtime(const char *family, const char *keyfield, const char *lookup, ...)
{
	struct ast_config_reg *reg;
	int res = -1;
	char db[256]="";
	char table[256]="";
	va_list ap;
	va_start(ap, lookup);
	reg = get_ast_cust_config_keyword(family, db, sizeof(db), table, sizeof(table));
	if (reg && reg->update_func) 
		res = reg->update_func(db, table, keyfield, lookup, ap);
	va_end(ap);
	return res;
}

static struct ast_config *__ast_load(const char *configfile, struct ast_config *tmp, struct ast_category **_tmpc, struct ast_variable **_last, int includelevel)
{
	char fn[256];
	char buf[8192];
	char db[256];
	char table[256];
	FILE *f;
	int lineno=0;
	int master=0;
	struct ast_config_reg *reg=NULL;
	struct ast_config *(*load_func)(const char *database, const char *table, const char *, struct ast_config *,struct ast_category **,struct ast_variable **,int);

	load_func=NULL;
	if (strcmp(configfile,config_conf_file) && strcmp(configfile,"asterisk.conf") && ast_cust_config_list) {
		if (global_load_func) {
			load_func = global_load_func;
		} else {
			reg = get_ast_cust_config_keyword(configfile, db, sizeof(db), table, sizeof(table));
			if (reg && reg->static_func) {
				load_func = reg->static_func;
			} else {
				reg = get_ast_cust_config_keyword(configfile, db, sizeof(db), table, sizeof(table));
				if (reg && reg->static_func)
					global_load_func = load_func = reg->static_func;
			}
		}

		if (load_func) {
			ast_log(LOG_NOTICE,"Loading Config %s via %s engine\n",configfile,reg && reg->name ? reg->name : "global");
			tmp = load_func(db, table, configfile,tmp, _tmpc, _last, includelevel);
	    
			if (tmp)
				return tmp;
		}
	}

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
			lineno++;
			if (fgets(buf, sizeof(buf), f)) {
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
			ast_verbose( "Not found (%s)\n", strerror(errno));
	}
	return tmp;
}

int ast_config_register(struct ast_config_reg *new) 
{
	struct ast_config_reg *ptr;
	ast_mutex_lock(&ast_cust_config_lock);
	if (!ast_cust_config_list) {
		ast_cust_config_list = new;
	} else {
		for(ptr=ast_cust_config_list;ptr->next;ptr=ptr->next);
			ptr->next = new;
	}
	ast_mutex_unlock(&ast_cust_config_lock);
	ast_log(LOG_NOTICE,"Registered Config Engine %s\n",new->name);
	return 1;
}

int ast_config_deregister(struct ast_config_reg *del) 
{
	struct ast_config_reg *ptr=NULL,*last=NULL;
	ast_mutex_lock(&ast_cust_config_lock);
	for (ptr=ast_cust_config_list;ptr;ptr=ptr->next) {
		if (ptr == del) {
			if (last && ptr->next) {
				last->next = ptr->next;
			} else if (last && ! ptr->next) {
				last->next = NULL;
			} else if (!last && ptr->next) {
				ast_cust_config_list = ptr->next;
			} else if (!last && !ptr->next) {
				ast_cust_config_list = NULL;
			}
		}
		last = ptr;
	}
	ast_mutex_unlock(&ast_cust_config_lock);
	return 0;
}

int ast_cust_config_active(void) {
	return (ast_cust_config >0) ? 1 : 0;
}

struct ast_config *ast_load(char *configfile)
{
	struct ast_category *tmpc=NULL;
	struct ast_variable *last = NULL;

	return __ast_load(configfile, NULL, &tmpc, &last, 0);
}

void ast_category_append(struct ast_config *config, struct ast_category *cat)
{
	struct ast_category *prev = NULL;
	cat->next = NULL;
	if (config->root) {
		prev = config->root;
		while(prev->next) prev = prev->next;
		prev->next = cat;
	} else
		config->root = cat;
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


struct ast_config *ast_new_config(void) 
{
	struct ast_config *config;
	config = malloc(sizeof(struct ast_config));
	memset(config,0,sizeof(struct ast_config));
	return config;
}



struct ast_category *ast_new_category(char *name) 
{
	struct ast_category *category;
	category = malloc(sizeof(struct ast_category));
	if (category) {
		memset(category,0,sizeof(struct ast_category));
		strncpy(category->name,name,sizeof(category->name) - 1);
	}
	return category;
}


struct ast_variable *ast_new_variable(char *name, char *value) 
{
	struct ast_variable *variable;
	int length = strlen(name) + strlen(value) + 2 + sizeof(struct ast_variable);
	variable = malloc(length);
	if (variable) {
		memset(variable, 0, length);
		variable->name = variable->stuff;
		variable->value = variable->stuff + strlen(name) + 1;		
		variable->object=0;
		strcpy(variable->name,name);
		strcpy(variable->value,value);
	}
	return variable;
}

int ast_cust_config_register(struct ast_config_reg *new) 
{
	ast_config_register(new);
	read_ast_cust_config();
	return 1;
}
int ast_cust_config_deregister(struct ast_config_reg *new) 
{
	ast_config_deregister(new);
	read_ast_cust_config();
	return 1;
}

static void clear_cust_keywords(void) 
{
	struct ast_config_map *map, *prev;
	ast_mutex_lock(&ast_cust_config_lock);
	map = maps;
	while(map) {
		prev = map;
		map = map->next;
		free(prev);
	}
	maps = NULL;
	ast_mutex_unlock(&ast_cust_config_lock);
}

static int config_command(int fd, int argc, char **argv) 
{
	struct ast_config_reg *key;
	struct ast_config_map *map;
	
	ast_cli(fd,"\n\n");
	ast_mutex_lock(&ast_cust_config_lock);
	for (key=get_config_registrations();key;key=key->next) {
		ast_cli(fd,"\nConfig Engine: %s\n",key->name);
		map = maps;
		while(map) {
			if (!strcasecmp(map->driver, key->name))
				ast_cli(fd,"===> %s (db=%s, table=%s)\n",map->name, map->database, map->table ? map->table : map->name);
			map = map->next;
		}
	}
	ast_mutex_unlock(&ast_cust_config_lock);
	ast_cli(fd,"\n\n");
	
	return 0;
}

static struct ast_cli_entry config_command_struct = {
  { "show","config","handles", NULL }, config_command,
  "Show Config Handles", NULL };

int register_config_cli() 
{
	return ast_cli_register(&config_command_struct);
}

int read_ast_cust_config(void) 
{
	char *cfg = config_conf_file;
	struct ast_config *config;
	struct ast_variable *v;
	struct ast_config_map *map;
	int length;
	char *driver, *table, *database, *stringp;

	clear_cust_keywords();
	config = ast_load(cfg);
	if (config) {
		for (v = ast_variable_browse(config,"settings");v;v=v->next) {
			stringp = v->value;
			driver = strsep(&stringp, ",");
			database = strsep(&stringp, ",");
			table = strsep(&stringp, ",");
			
			if (!strcmp(v->name,config_conf_file) || !strcmp(v->name,"asterisk.conf")) {
				ast_log(LOG_WARNING, "Cannot bind asterisk.conf or extconfig.conf!\n");
			} else if (driver && database) {
				length = sizeof(struct ast_config_map);
				length += strlen(v->name) + 1;
				length += strlen(driver) + 1;
				length += strlen(database) + 1;
				if (table)
					length += strlen(table) + 1;
				map = malloc(length);
				if (map) {
					memset(map, 0, length);
					map->name = map->stuff;
					strcpy(map->name, v->name);
					map->driver = map->name + strlen(map->name) + 1;
					strcpy(map->driver, driver);
					map->database = map->driver + strlen(map->driver) + 1;
					strcpy(map->database, database);
					if (table) {
						map->table = map->database + strlen(map->database) + 1;
						strcpy(map->table, table);
					} else
						map->table = NULL;
					map->next = maps;
					if (option_verbose > 1)
						ast_verbose(VERBOSE_PREFIX_2 "Binding %s to %s/%s/%s\n",map->name,map->driver, map->database, map->table ? map->table : map->name);
					maps = map;
				}
			}
		}
		
		ast_destroy(config);
	}

	return 0;
}
