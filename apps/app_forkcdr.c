/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Fork CDR application
 * Copyright Anthony Minessale anthmct@yahoo.com
 * Development of this app Sponsered/Funded  by TAAN Softworks Corp
 * 
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/cdr.h>
#include <asterisk/module.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

static char *tdesc = "Fork The CDR into 2 separate entities.";
static char *app = "ForkCDR";
static char *synopsis = 
"Forks the Call Data Record";
static char *descrip = 
"  ForkCDR([options]):  Causes the Call Data Record to fork an additional\n"
	"cdr record starting from the time of the fork call\n"
"If the option 'v' is passed all cdr variables will be passed along also.\n"
"";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;


static void ast_cdr_clone(struct ast_cdr *cdr) 
{
	struct ast_cdr *newcdr = ast_cdr_alloc();
	memcpy(newcdr,cdr,sizeof(struct ast_cdr));
	ast_cdr_append(cdr,newcdr);
	gettimeofday(&newcdr->start, NULL);
	memset(&newcdr->answer, 0, sizeof(newcdr->answer));
	memset(&newcdr->varshead, 0, sizeof(newcdr->varshead));
	ast_cdr_copy_vars(newcdr, cdr);
	if (!ast_test_flag(cdr, AST_CDR_FLAG_KEEP_VARS)) {
		ast_cdr_free_vars(cdr, 0);
	}
	newcdr->disposition = AST_CDR_NOANSWER;
	ast_set_flag(cdr, AST_CDR_FLAG_CHILD|AST_CDR_FLAG_LOCKED);
}

static void ast_cdr_fork(struct ast_channel *chan) 
{
	if(chan && chan->cdr) {
		ast_cdr_clone(chan->cdr);
	}
}

static int forkcdr_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	LOCAL_USER_ADD(u);
	ast_set2_flag(chan->cdr, strchr((char *)data, 'v'), AST_CDR_FLAG_KEEP_VARS);
	
	ast_cdr_fork(chan);

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, forkcdr_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
