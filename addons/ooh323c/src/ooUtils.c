/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be
 * used and copied only in accordance with the terms of this license.
 * The text of the license may generally be found in the root
 * directory of this installation in the COPYING file.  It
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must
 * maintain this copyright notice.
 *
 *****************************************************************************/

#include "asterisk.h"
#include "asterisk/lock.h"
#include "ooUtils.h"
#include <ctype.h>

const char* ooUtilsGetText (OOUINT32 idx, const char** table, size_t tabsiz)
{
   return (idx < tabsiz) ? table[idx] : "?";
}

OOBOOL ooUtilsIsStrEmpty (const char* str)
{
   return (str == NULL || *str =='\0');
}


OOBOOL ooIsDialedDigit(const char* str)
{
   if(str == NULL || *str =='\0') { return FALSE; }
   while(*str != '\0')
   {
      if(!isdigit(*str) &&
         *str != '#' && *str != '*' && *str != ',') { return FALSE; }
      str++;
   }
   return TRUE;
}
