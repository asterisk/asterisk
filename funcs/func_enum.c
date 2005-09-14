/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Enum Functions
 *
 * Copyright (C) 2005
 *
 * Oleksiy Krivoshey <oleksiyk@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>

#include "asterisk.h"

#ifndef BUILTIN_FUNC
#include "asterisk/module.h"
#endif /* BUILTIN_FUNC */
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"

#include "asterisk/pbx.h"
#include "asterisk/options.h"

#include "asterisk/enum.h"

static char* synopsis = "Syntax: ENUMLOOKUP(number[,Method-type[,options|record#[,zone-suffix]]])\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *function_enum(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
       int res=0;
       char tech[80];
       char dest[80] = "";
       char *zone;
       char *options;
       struct localuser *u;
       char *params[4];
       char *p = data;
       char *s;
       int i = 0;


       if (!data || ast_strlen_zero(data)) {
               ast_log(LOG_WARNING, synopsis);
               return "";
       }

       do {
               if(i>3){
                       ast_log(LOG_WARNING, synopsis);
                       return "";
               }
               params[i++] = p;
               p = strchr(p, '|');
               if(p){
                       *p = '\0';
                       p++;
               }
       } while(p);

       if(i < 1){
               ast_log(LOG_WARNING, synopsis);
               return "";
       }

       if( (i > 1 && strlen(params[1]) == 0) || i < 2){
               ast_copy_string(tech, "sip", sizeof(tech));
       } else {
               ast_copy_string(tech, params[1], sizeof(tech));
       }

       if( (i > 3 && strlen(params[3]) == 0) || i<4){
               zone = "e164.arpa";
       } else {
               zone = params[3];
       }

       if( (i > 2 && strlen(params[2]) == 0) || i<3){
               options = "1";
       } else {
               options = params[2];
       }

       /* strip any '-' signs from number */
       p = params[0];
       /*
       while(*p == '+'){
               p++;
       }
       */
       s = p;
       i = 0;
       while(*p && *s){
               if(*s == '-'){
                       s++;
               } else {
                       p[i++] = *s++;
               }
       }
       p[i] = 0;

       LOCAL_USER_ACF_ADD(u);

       res = ast_get_enum(chan, p, dest, sizeof(dest), tech, sizeof(tech), zone, options);

       LOCAL_USER_REMOVE(u);

       p = strchr(dest, ':');
       if(p && strncasecmp(tech, "ALL", sizeof(tech))) {
               ast_copy_string(buf, p+1, sizeof(dest));
       } else {
               ast_copy_string(buf, dest, sizeof(dest));
       }

       return buf;
}


#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function enum_function = {
       .name = "ENUMLOOKUP",
       .synopsis = "ENUMLOOKUP allows for general or specific querying of NAPTR records"
       " or counts of NAPTR types for ENUM or ENUM-like DNS pointers",
       .syntax = "ENUMLOOKUP(number[,Method-type[,options|record#[,zone-suffix]]])",
       .desc = "Option 'c' returns an integer count of the number of NAPTRs of a certain RR type.\n"
       "Combination of 'c' and Method-type of 'ALL' will return a count of all NAPTRs for the record.\n"
       "Defaults are: Method-type=sip, no options, record=1, zone-suffix=e164.arpa\n\n"
       "For more information, see README.enum",
       .read = function_enum,
};

#ifndef BUILTIN_FUNC

static char *tdesc = "ENUMLOOKUP allows for general or specific querying of NAPTR records or counts of NAPTR types for ENUM or ENUM-like DNS pointers";

int unload_module(void)
{
       return ast_custom_function_unregister(&enum_function);
}

int load_module(void)
{
       return ast_custom_function_register(&enum_function);
}

char *description(void)
{
       return tdesc;
}

int usecount(void)
{
       return 0;
}

char *key()
{
       return ASTERISK_GPL_KEY;
}
#endif /* BUILTIN_FUNC */

