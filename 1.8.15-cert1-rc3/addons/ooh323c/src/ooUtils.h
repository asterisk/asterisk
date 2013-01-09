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
/**
 * @file ooUtils.h 
 * This file contains general utility functions. 
 */
#ifndef _OOUTILS_H_
#define _OOUTILS_H_
#include "ootypes.h"

/**
 * Get text from a text table for a given enumeration index.
 *
 * @param idx    Index of item in table
 * @param table  The table containing the text items
 * @param tabsiz Number of items in the table
 * @return  Text for item or '?' if idx outside bounds of table
 */
EXTERN const char* ooUtilsGetText 
(OOUINT32 idx, const char** table, size_t tabsiz);

/**
 * Test if given string value is empty.  String is considered to empty 
 * if value is NULL or there are no characters in string (strlen == 0).
 *
 * @param str    String to test
 * @return       TRUE if string empty; FALSE otherwise
 */
EXTERN OOBOOL ooUtilsIsStrEmpty (const char * str);


/**
 * Test if given string value is digit string.  
 *
 * @param str    String to test
 * @return       TRUE if string contains all digits; FALSE otherwise
 */
EXTERN OOBOOL ooIsDailedDigit(const char* str);

#endif
