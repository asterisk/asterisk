#ifndef _ASTERISK_CONFIG_PVT_H
#define _ASTERISK_CONFIG_PVT_H
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <stdarg.h>
#define CONFIG_KEYWORD_STRLEN 128
#define CONFIG_KEYWORD_ARRAYLEN 512
#include <asterisk/config.h>

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
	struct ast_category *prev;
};


struct ast_category;

struct ast_config_reg {
	char name[CONFIG_KEYWORD_STRLEN];
	struct ast_config *(*static_func)(const char *database, const char *table, const char *, struct ast_config *,struct ast_category **,struct ast_variable **,int);
	struct ast_variable *(*realtime_func)(const char *database, const char *table, va_list ap);
	struct ast_config *(*realtime_multi_func)(const char *database, const char *table, va_list ap);
	int (*update_func)(const char *database, const char *table, const char *keyfield, const char *entity, va_list ap);
	struct ast_config_reg *next;
};


int ast_config_register(struct ast_config_reg *new);
int ast_config_deregister(struct ast_config_reg *del);
void ast_cust_config_on(void);
void ast_cust_config_off(void);
int ast_cust_config_active(void);
void ast_config_destroy_all(void);


int ast_category_delete(struct ast_config *cfg, char *category);
int ast_variable_delete(struct ast_config *cfg, char *category, char *variable, char *value);
int ast_save(char *filename, struct ast_config *cfg, char *generator);

struct ast_config *ast_new_config(void);
struct ast_category *ast_new_category(char *name);
struct ast_variable *ast_new_variable(char *name,char *value);
void ast_category_append(struct ast_config *config, struct ast_category *cat);
void ast_category_destroy(struct ast_category *cat);
int ast_cust_config_register(struct ast_config_reg *new);
int ast_cust_config_deregister(struct ast_config_reg *new);
int register_config_cli(void);
int read_ast_cust_config(void);




#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
