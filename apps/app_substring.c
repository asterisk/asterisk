/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief substr
 *
 * \todo Deprecate this application in 1.3dev
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/lock.h"

static char *tdesc = "(Deprecated) Save substring digits in a given variable";

static char *descrip =
"  (Deprecated, use ${variable:a:b} instead)\n"
"\n"
"  SubString(variable=string_of_digits|count1|count2): Assigns the substring\n"
"of string_of_digits to a given variable. Parameter count1 may be positive\n"
"or negative. If it's positive then we skip the first count1 digits from the\n"
"left. If it's negative, we move count1 digits counting from the end of\n"
"the string to the left. Parameter count2 implies how many digits we are\n"
"taking from the point that count1 placed us. If count2 is negative, then\n"
"that many digits are omitted from the end.\n"
"For example:\n"
"exten => _NXXXXXX,1,SubString,test=2564286161|0|3\n"
"assigns the area code (3 first digits) to variable test.\n"
"exten => _NXXXXXX,1,SubString,test=2564286161|-7|7\n"
"assigns the last 7 digits to variable test.\n"
"exten => _NXXXXXX,1,SubString,test=2564286161|0|-4\n" 
"assigns all but the last 4 digits to variable test.\n" 
"If there are no parameters it'll return with -1.\n"
"If there wrong parameters it go on and return with 0\n";

static char *app = "SubString";

static char *synopsis = "(Deprecated) Save substring digits in a given variable";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int substring_exec(struct ast_channel *chan, void *data)
{
  char newexten[AST_MAX_EXTENSION] = "";
  char *count1, *count2;
  char *first, *second, *stringp;
  struct localuser *u;

  LOCAL_USER_ADD(u);

  stringp=alloca(strlen(data)+1);
  ast_log(LOG_WARNING, "The use of Substring application is deprecated. Please use ${variable:a:b} instead\n");
  strncpy(stringp,data,strlen(data));
  if (strchr(stringp,'|')&&strchr(stringp,'=')) {
    int icount1,icount2;
    first=strsep(&stringp,"=");
    second=strsep(&stringp,"|");
    count1=strsep(&stringp,"|");
    count2=strsep(&stringp,"\0");
    if (!first || !second || !count1 || !count2) {
      ast_log(LOG_DEBUG, "Ignoring, since there is no argument: variable or string or count1 or count2\n");
      LOCAL_USER_REMOVE(u);
      return 0;
    }
    icount1=atoi(count1);
    icount2=atoi(count2);
	if (icount2<0) {
	  icount2 = icount2 + strlen(second);
    }
    if (abs(icount1)>strlen(second)) {
      ast_log(LOG_WARNING, "Limiting count1 parameter because it exceeds the length of the string\n");
      if (icount1>=0)
        icount1=strlen(second);
      else
        icount1=0;
    }
    if ((icount1<0 && icount2>-icount1) || (icount1>=0 && icount1+icount2>strlen(second))) {
      ast_log(LOG_WARNING, "Limiting count2 parameter because it exceeds the length of the string\n");
      if (icount1>=0)
      	icount2=strlen(second)-icount1;
      else
      	icount2=strlen(second)+icount1;
    }
    if (first&&second) {
      if (icount1>=0)
        strncpy(newexten,second+icount1,icount2);
      else
        strncpy(newexten,second+strlen(second)+icount1,icount2);
      pbx_builtin_setvar_helper(chan,first,newexten);
    }
  } else {
    ast_log(LOG_DEBUG, "Ignoring, no parameters\n");
  }

  LOCAL_USER_REMOVE(u);

  return 0;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

int load_module(void)
{
	return ast_register_application(app, substring_exec, synopsis, descrip);
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
