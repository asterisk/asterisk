#include <asterisk/chanvars.h>
#include <malloc.h>
#include <string.h>

struct ast_var_t *ast_var_assign(char *name,char *value) {
	int i;
	struct ast_var_t *var;
	
	var=malloc(sizeof(struct ast_var_t));
	
	i=strlen(value);
	var->value=malloc(i+1);
	strncpy(var->value,value,i);
	var->value[i]='\0';
	
	i=strlen(name);
	var->name=malloc(i+1);
	strncpy(var->name,name,i); 
	var->name[i]='\0';
	return(var);
}	
	
void ast_var_delete(struct ast_var_t *var) {
	if (var!=NULL) {
		if (var->name!=NULL) free(var->name);
		if (var->value!=NULL) free(var->value);
		free(var);
	}
}

char *ast_var_name(struct ast_var_t *var) {
	return(var->name);
}

char *ast_var_value(struct ast_var_t *var) {
	return(var->value);
}

	