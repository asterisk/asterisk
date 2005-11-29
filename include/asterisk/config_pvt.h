#ifndef _ASTERISK_CONFIG_PVT_H
#define _ASTERISK_CONFIG_PVT_H
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define CONFIG_KEYWORD_STRLEN 128
#define CONFIG_KEYWORD_ARRAYLEN 512
#include <asterisk/config.h>

#define MAX_INCLUDE_LEVEL 10

struct ast_category {
	char name[80];
	struct ast_variable *root;
	struct ast_category *next;
#ifdef PRESERVE_COMMENTS
	struct ast_comment *precomments;
	struct ast_comment *sameline;
#endif	
};

struct ast_config {
	/* Maybe this structure isn't necessary but we'll keep it
	   for now */
	struct ast_category *root;
	struct ast_category *prev;
#ifdef PRESERVE_COMMENTS
	struct ast_comment *trailingcomments;
#endif	
};

#ifdef PRESERVE_COMMENTS
struct ast_comment_struct
{
	struct ast_comment *root;
	struct ast_comment *prev;
};
#endif

struct ast_category;

struct ast_config_reg {
	char name[CONFIG_KEYWORD_STRLEN];
	struct ast_config *(*func)(char *, struct ast_config *,struct ast_category **,struct ast_variable **,int
#ifdef PRESERVE_COMMENTS
,struct ast_comment_struct *
#endif
);
	char keywords[CONFIG_KEYWORD_STRLEN][CONFIG_KEYWORD_ARRAYLEN];
	int keycount;
	struct ast_config_reg *next;
};



int ast_config_register(struct ast_config_reg *new);
int ast_config_deregister(struct ast_config_reg *del);
void ast_cust_config_on(void);
void ast_cust_config_off(void);
int ast_cust_config_active(void);
struct ast_config_reg *get_config_registrations(void);
struct ast_config_reg *get_ast_cust_config(char *name);
struct ast_config_reg *get_ast_cust_config_keyword(char *name);
void ast_config_destroy_all(void);


int ast_category_delete(struct ast_config *cfg, char *category);
int ast_variable_delete(struct ast_config *cfg, char *category, char *variable, char *value);
int ast_save(char *filename, struct ast_config *cfg, char *generator);

struct ast_config *ast_new_config(void);
struct ast_category *ast_new_category(char *name);
struct ast_variable *ast_new_variable(char *name,char *value);
int ast_cust_config_register(struct ast_config_reg *new);
int ast_cust_config_deregister(struct ast_config_reg *new);
int register_config_cli(void);
int read_ast_cust_config(void);




#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
