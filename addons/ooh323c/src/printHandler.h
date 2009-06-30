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
   @file printHandler.h  
   This is an implementation of a simple print handler.  It outputs 
   the fields of an encoded PER message to stdout in a structured output 
   format..
*/

#ifndef _PRINTHANDLER_H_
#define _PRINTHANDLER_H_

#include "eventHandler.h"

extern EventHandler printHandler;

void initializePrintHandler(EventHandler *printHandler, char * varname);
void finishPrint();
void indent ();
void printStartElement (const char* name, int index );
void printEndElement (const char* name, int index );
void printBoolValue (ASN1BOOL value);
void printIntValue (ASN1INT value);
void printuIntValue (ASN1UINT value);
void printBitStrValue (ASN1UINT numbits, const ASN1OCTET* data);
void printOctStrValue (ASN1UINT numocts, const ASN1OCTET* data);
void printCharStrValue (const char* value);
void printCharStr16BitValue (ASN1UINT nchars, ASN116BITCHAR* data);
void printNullValue ();
void printOidValue (ASN1UINT numSubIds, ASN1UINT* pSubIds);
void printEnumValue (ASN1UINT value);
void printOpenTypeValue (ASN1UINT numocts, const ASN1OCTET* data);

#endif
