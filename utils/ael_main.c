#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#if !defined(SOLARIS) && !defined(__CYGWIN__)
#include <err.h>
#endif
#include <errno.h>
#include <regex.h>
#include <limits.h>

/* ast_copy_string */
#define AST_API_MODULE
#include "asterisk/strings.h"

/* ensure that _ast_calloc works */
#define AST_API_MODULE 
#include "asterisk/utils.h"

#include "asterisk/ast_expr.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/ael_structs.h"

#define AST_CONFIG_MAX_PATH 255

int conts=0, extens=0, priors=0;
char last_exten[18000];
char ast_config_AST_CONFIG_DIR[AST_CONFIG_MAX_PATH];
char ast_config_AST_VAR_DIR[AST_CONFIG_MAX_PATH];

void ast_cli_register_multiple(void);
void ast_register_file_version(void);
void ast_unregister_file_version(void);
int ast_add_extension2(void *con,
					   int replace, const char *extension, int priority, const char *label, const char *callerid,
						const char *application, void *data, void (*datad)(void *),
					   const char *registrar);
void pbx_builtin_setvar(void *chan, void *data);
void ast_context_create(void **extcontexts, const char *name, const char *registrar);
void ast_context_add_ignorepat2(void *con, const char *value, const char *registrar);
void ast_context_add_include2(void *con, const char *value, const char *registrar);
void ast_context_add_switch2(void *con, const char *value, const char *data, int eval, const char *registrar);
void ast_merge_contexts_and_delete(void);
void ast_context_verify_includes(void);
struct ast_context * ast_walk_contexts(void);
void ast_cli_unregister_multiple(void);
void ast_context_destroy(void);
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...);
char *ast_process_quotes_and_slashes(char *start, char find, char replace_with);
void ast_verbose(const char *fmt, ...);
struct ast_app *pbx_findapp(const char *app);
static int no_comp = 0;
static int use_curr_dir = 0;


struct ast_app *pbx_findapp(const char *app)
{
	return (struct ast_app*)1; /* so as not to trigger an error */
}

void ast_cli_register_multiple(void)
{
	if(!no_comp)
        printf("Executed ast_cli_register_multiple();\n");
}

void ast_register_file_version(void)
{
	if(!no_comp)
	printf("Executed ast_register_file_version();\n");
}

void ast_unregister_file_version(void)
{
	if(!no_comp)
	printf("Executed ast_unregister_file_version();\n");

}
int ast_add_extension2(void *con,
						int replace, const char *extension, int priority, const char *label, const char *callerid,
						const char *application, void *data, void (*datad)(void *),
						const char *registrar)
{
	priors++;
	if (strcmp(extension,last_exten) != 0) {
		extens++;
		strcpy(last_exten, extension);
	}
	if (!label) {
		label = "(null)";
	}
	if (!callerid) {
		callerid = "(null)";
	}
	if (!application) {
		application = "(null)";
	}
	if(!no_comp)
	printf("Executed ast_add_extension2(con, rep=%d, exten=%s, priority=%d, label=%s, callerid=%s, appl=%s, data=%s, FREE, registrar=%s);\n",
		replace, extension, priority, label, callerid, application, (data?(char*)data:"(null)"), registrar);

	/* since add_extension2 is responsible for the malloc'd data stuff */
	if( data )
		free(data);
	return 0;
}

void pbx_builtin_setvar(void *chan, void *data)
{
	if(!no_comp)
	printf("Executed pbx_builtin_setvar(chan, data=%s);\n", (char*)data);
}
	

void ast_context_create(void **extcontexts, const char *name, const char *registrar)
{
	if(!no_comp)
	printf("Executed ast_context_create(conts, name=%s, registrar=%s);\n", name, registrar);
	conts++;
}

void ast_context_add_ignorepat2(void *con, const char *value, const char *registrar)
{
	if(!no_comp)
	printf("Executed ast_context_add_ignorepat2(con, value=%s, registrar=%s);\n", value, registrar);
}

void ast_context_add_include2(void *con, const char *value, const char *registrar)
{
	if(!no_comp)
	printf("Executed ast_context_add_include2(con, value=%s, registrar=%s);\n", value, registrar);
}

void ast_context_add_switch2(void *con, const char *value, const char *data, int eval, const char *registrar)
{
	if(!no_comp)
	printf("Executed ast_context_add_switch2(con, value=%s, data=%s, eval=%d, registrar=%s);\n", value, data, eval, registrar);
}

void ast_merge_contexts_and_delete(void)
{
	if(!no_comp)
	printf("Executed ast_merge_contexts_and_delete();\n");
}

void ast_context_verify_includes(void)
{
	if(!no_comp)
	printf("Executed ast_context_verify_includes();\n");
}

struct ast_context * ast_walk_contexts(void)
{
	if(!no_comp)
	printf("Executed ast_walk_contexts();\n");
	return 0;
}

void ast_cli_unregister_multiple(void)
{
	if(!no_comp)
	printf("Executed ast_cli_unregister_multiple();\n");
}

void ast_context_destroy(void)
{
	printf("Executed ast_context_destroy();\n");
}

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
        va_list vars;
        va_start(vars,fmt);

        printf("LOG: lev:%d file:%s  line:%d func: %s  ",
                   level, file, line, function);
        vprintf(fmt, vars);
        fflush(stdout);
        va_end(vars);
}

void ast_verbose(const char *fmt, ...)
{
        va_list vars;
        va_start(vars,fmt);

        printf("VERBOSE: ");
        vprintf(fmt, vars);
        fflush(stdout);
        va_end(vars);
}

char *ast_process_quotes_and_slashes(char *start, char find, char replace_with)
{
        char *dataPut = start;
        int inEscape = 0;
        int inQuotes = 0;

        for (; *start; start++) {
                if (inEscape) {
                        *dataPut++ = *start;       /* Always goes verbatim */
                        inEscape = 0;
                } else {
                        if (*start == '\\') {
                                inEscape = 1;      /* Do not copy \ into the data */
                        } else if (*start == '\'') {
                                inQuotes = 1-inQuotes;   /* Do not copy ' into the data */
                        } else {
                                /* Replace , with |, unless in quotes */
                                *dataPut++ = inQuotes ? *start : ((*start==find) ? replace_with : *start);
                        }
                }
        }
        if (start != dataPut)
                *dataPut = 0;
        return dataPut;
}

extern struct module_symbols mod_data;


int main(int argc, char **argv)
{
	int i;
	
	for(i=1;i<argc;i++)
	{
		if( argv[i][0] == '-' && argv[i][1] == 'n' )
			no_comp =1;
		if( argv[i][0] == '-' && argv[i][1] == 'd' )
			use_curr_dir =1;
	}
	
		
	if( !no_comp )
		printf("\n(You can use the -n option if you aren't interested in seeing all the instructions generated by the compiler)\n\n");
	if( !use_curr_dir )
		printf("\n(You can use the -d option if you want to use the current working directory as the CONFIG_DIR. I will look in this dir for extensions.ael* and its included files)\n\n");
	
	if( use_curr_dir )
	{
		strcpy(ast_config_AST_CONFIG_DIR, ".");
	}
	else
	{
		strcpy(ast_config_AST_CONFIG_DIR, "/etc/asterisk");
	}
	strcpy(ast_config_AST_VAR_DIR, "/var/lib/asterisk");
	
	mod_data.load_module(0);
	
	ast_log(4, "ael2_parse", __LINE__, "main", "%d contexts, %d extensions, %d priorities\n", conts, extens, priors);
	
    return 0;
}
