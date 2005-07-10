/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * simple math application
 * 
 * Copyright (C) 2004 - 2005, Andy Powell 
 *
 * Updated by Mark Spencer <markster@digium.com>
 *
 */

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/file.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/localtime.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"

static char *tdesc = "Basic Math Functions";

static char *app_math = "Math";

static char *math_synopsis = "Performs Mathematical Functions";

static char *math_descrip =
"Math(returnvar,<number1><op><number 2>\n\n"
"Perform floating point calculation on number 1 to number 2 and \n"
"store the result in returnvar.  Valid ops are: \n"
"    +,-,/,*,%,<,>,>=,<=,==\n"
"and behave as their C equivalents.  Always returns 0.\n";

#define ADDFUNCTION 0
#define DIVIDEFUNCTION 1
#define MULTIPLYFUNCTION 2
#define SUBTRACTFUNCTION 3
#define MODULUSFUNCTION 4

#define GTFUNCTION 5
#define LTFUNCTION 6
#define GTEFUNCTION 7
#define LTEFUNCTION 8
#define EQFUNCTION 9

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int math_exec(struct ast_channel *chan, void *data) 
{
	float fnum1;
	float fnum2;
	float ftmp = 0;
	char *op;
	int iaction=-1;
	static int deprecation_warning = 0;

	/* dunno, big calulations :D */
	char user_result[30];

	char *s;
	char *mvar, *mvalue1, *mvalue2=NULL;
		
	struct localuser *u;

	if (!deprecation_warning) {
		ast_log(LOG_WARNING, "Math() is deprecated, please use Set(var=${MATH(...)} instead.\n");
		deprecation_warning = 1;
	}

	if (!data) {
		ast_log(LOG_WARNING, "No parameters passed. !\n");
		return -1;
	}

	LOCAL_USER_ADD(u);
		
	s = ast_strdupa((void *) data);

	mvar = strsep(&s, "|");
	mvalue1 = strsep(&s, "|");
	
	if ((op = strchr(mvalue1, '+'))) {
		iaction = ADDFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '-'))) {
		iaction = SUBTRACTFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '*'))) {
		iaction = MULTIPLYFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '/'))) {
		iaction = DIVIDEFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '>'))) {
		iaction = GTFUNCTION;
		*op = '\0';
		if (*(op+1) == '=') {
			op++;
			*op = '\0';
			iaction = GTEFUNCTION;
		}
	} else if ((op = strchr(mvalue1, '<'))) {
		iaction = LTFUNCTION;
		*op = '\0';
		if (*(op+1) == '=') {
			op++;
			*op = '\0';
			iaction = LTEFUNCTION;
		}
	} else if ((op = strchr(mvalue1, '='))) {
		iaction = GTFUNCTION;
		*op = '\0';
		if (*(op+1) == '=') {
			op++;
			*op = '\0';
			iaction = EQFUNCTION;
		} else
			op = NULL;
	} 
	
	if (op) 
		mvalue2 = op + 1;
		
	if (!mvar || !mvalue1 || !mvalue2) {
		ast_log(LOG_WARNING, "Supply all the parameters - just this once, please\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (!strcmp(mvar,"")) {
		ast_log(LOG_WARNING, "No return variable set.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (sscanf(mvalue1, "%f", &fnum1) != 1) {
		ast_log(LOG_WARNING, "'%s' is not a valid number\n", mvalue1);
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (sscanf(mvalue2, "%f", &fnum2) != 1) {
		ast_log(LOG_WARNING, "'%s' is not a valid number\n", mvalue2);
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	switch (iaction) {
	case ADDFUNCTION :
		ftmp = fnum1 + fnum2;
		break;
	case DIVIDEFUNCTION :
		if (fnum2 <=0)
			ftmp = 0; /* can't do a divide by 0 */
		else
			ftmp = (fnum1 / fnum2);
		break;
	case MULTIPLYFUNCTION :
		ftmp = (fnum1 * fnum2);
		break;
	case SUBTRACTFUNCTION :
		ftmp = (fnum1 - fnum2);
		break;
	case MODULUSFUNCTION : {
		int inum1 = fnum1;
		int inum2 = fnum2;
			
		ftmp = (inum1 % inum2);
		
		break;
		}
	case GTFUNCTION :
		if (fnum1 > fnum2)
			strcpy(user_result, "TRUE");
		else
			strcpy(user_result, "FALSE");
		break;
	case LTFUNCTION :
		if (fnum1 < fnum2)
			strcpy(user_result, "TRUE");
		else
			strcpy(user_result, "FALSE");
		break;
	case GTEFUNCTION :
		if (fnum1 >= fnum2)
			strcpy(user_result, "TRUE");
		else
			strcpy(user_result, "FALSE");
		break;
	case LTEFUNCTION :
		if (fnum1 <= fnum2)
			strcpy(user_result, "TRUE");
		else
			strcpy(user_result, "FALSE");
		break;					
	case EQFUNCTION :
		if (fnum1 == fnum2)
			strcpy(user_result, "TRUE");
		else
			strcpy(user_result, "FALSE");
		break;
	default :
		ast_log(LOG_WARNING, "Something happened that neither of us should be proud of %d\n", iaction);
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (iaction < GTFUNCTION || iaction > EQFUNCTION) 
		snprintf(user_result,sizeof(user_result),"%f",ftmp);
		
	pbx_builtin_setvar_helper(chan, mvar, user_result);	
	
	LOCAL_USER_REMOVE(u);
	return 0;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;

	res  = ast_unregister_application(app_math);
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app_math, math_exec, math_synopsis, math_descrip);
	return res;
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

/* Fading everything to black and blue... */
