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
/* This is an implementation of a simple print handler.  It outputs 
   the fields of an encoded PER message to stdout in a structured output 
   format..
*/
#include "asterisk.h"
#include "asterisk/lock.h"

#include <stdlib.h>
/* #ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif */
#include "printHandler.h"
#include "ootypes.h"
#include "rtctype.h"
#include "ootrace.h"

EventHandler printHandler;
AST_MUTEX_DEFINE_STATIC(printlock);

static const char* pVarName;
static int gIndentSpaces;

static const char* bitStrToString 
(ASN1UINT numbits, const ASN1OCTET* data, char* buffer, size_t bufsiz);

static const char* octStrToString 
(ASN1UINT numocts, const ASN1OCTET* data, char* buffer, size_t bufsiz);

void printCharStr32BitValue (ASN1UINT nchars, ASN132BITCHAR* data);
void ooPrintOIDValue (ASN1OBJID* pOID);
void printRealValue (double value);

void initializePrintHandler(EventHandler *printHandler, char * varname)
{
   printHandler->startElement = &printStartElement;
   printHandler->endElement = &printEndElement;
   printHandler->boolValue = &printBoolValue;
   printHandler->intValue = &printIntValue;
   printHandler->uIntValue = &printuIntValue ;
   printHandler->bitStrValue = &printBitStrValue;
   printHandler->octStrValue = &printOctStrValue;
   printHandler->charStrValue = &printCharStrValue;
   printHandler->charStr16BitValue = &printCharStr16BitValue ;
   printHandler->nullValue = &printNullValue;
   printHandler->oidValue = &printOidValue;
   printHandler->enumValue = &printEnumValue;
   printHandler->openTypeValue = &printOpenTypeValue;
   pVarName = varname;
   ast_mutex_lock(&printlock);
   OOTRACEDBGB2("%s = {\n", pVarName);
   gIndentSpaces += 3;

}

void finishPrint()
{
   OOTRACEDBGB1 ("}\n");
   gIndentSpaces -= 3;
   if (gIndentSpaces != 0) {
      OOTRACEDBGB1 ("ERROR: unbalanced structure\n");
   }
   gIndentSpaces = 0;
   ast_mutex_unlock(&printlock);
}

void indent ()
{
   int i=0;
   for (i = 0; i < gIndentSpaces; i++) OOTRACEDBGB1 (" ");
}

void printStartElement (const char* name, int index)
{
   indent ();
   OOTRACEDBGB1 (name);
   if (index >= 0) OOTRACEDBGB2 ("[%d]", index);
   OOTRACEDBGB1 (" = {\n");
   gIndentSpaces += 3;
}

void printEndElement (const char* name, int index)
{
   gIndentSpaces -= 3;
   indent ();
   OOTRACEDBGB1 ("}\n");
}

void printBoolValue (ASN1BOOL value)
{
   const char* s = value ? "TRUE" : "FALSE";
   indent ();
   OOTRACEDBGB2 ("%s\n", s);
}

void printIntValue (ASN1INT value)
{
   indent ();
   OOTRACEDBGB2 ("%d\n", value);
}

void printuIntValue (ASN1UINT value)
{
   indent ();
   OOTRACEDBGB2 ("%u\n", value);
}

void printBitStrValue (ASN1UINT numbits, const ASN1OCTET* data)
{
#ifdef __MINGW32__
   char s[numbits + 8];
   indent ();
   OOTRACEDBGB2("%s\n", bitStrToString (numbits, data, s, numbits+8));
#else
   char* s = (char*)malloc(numbits + 8);
   indent ();
   OOTRACEDBGB2("%s\n", bitStrToString (numbits, data, s, numbits+8));
   free(s);
#endif
}

void printOctStrValue (ASN1UINT numocts, const ASN1OCTET* data)
{
   int bufsiz = (numocts * 2) + 8;
#ifdef __MINGW32__
   char s[bufsiz];
   indent ();
   OOTRACEDBGB2 ("%s\n", octStrToString (numocts, data, s, bufsiz));
#else
   char* s = (char*)malloc(bufsiz);
   indent ();
   OOTRACEDBGB2 ("%s\n", octStrToString (numocts, data, s, bufsiz));
   free(s);
#endif
}

void printCharStrValue (const char* value)
{
   indent ();
   OOTRACEDBGB2 ("\"%s\"\n", value);
}

void printCharStr16BitValue (ASN1UINT nchars, ASN116BITCHAR* data)
{
   ASN1UINT ui;
   indent ();

   for (ui = 0; ui < nchars; ui++) {
      if (data[ui] >= 32 && data[ui] <= 127)
         OOTRACEDBGB2 ("%c", (char)data[ui]);
      else
         OOTRACEDBGB1 ("?");
   }

   OOTRACEDBGB1 ("\n");
}

void printCharStr32BitValue (ASN1UINT nchars, ASN132BITCHAR* data)
{
   ASN1UINT ui;
   indent ();

   for ( ui = 0; ui < nchars; ui++) {
      if (data[ui] >= 32 && data[ui] <= 127)
         OOTRACEDBGB2 ("%c", (char)data[ui]);
      else
         OOTRACEDBGB2 ("\\%d", data[ui]);
   }

   OOTRACEDBGB1 ("\n");
}

void printNullValue ()
{
   indent ();
   OOTRACEDBGB1 ("NULL\n");
}

void ooPrintOIDValue (ASN1OBJID* pOID)
{
   ASN1UINT ui;
   OOTRACEDBGB1 ("{ \n");
   for (ui = 0; ui < pOID->numids; ui++) {
      OOTRACEDBGB2 ("%d ", pOID->subid[ui]);
   }
   OOTRACEDBGB1 ("}\n");
}

void printOidValue (ASN1UINT numSubIds, ASN1UINT* pSubIds)
{
   ASN1UINT ui;
   ASN1OBJID oid;
   oid.numids = numSubIds;

   for ( ui = 0; ui < numSubIds; ui++)
      oid.subid[ui] = pSubIds[ui];

   indent ();
   ooPrintOIDValue (&oid);
}

void printRealValue (double value)
{
   indent ();
   OOTRACEDBGB2 ("%f\n", value);
}

void printEnumValue (ASN1UINT value)
{
   indent ();
   OOTRACEDBGB2 ("%u\n", value);
}

void printOpenTypeValue (ASN1UINT numocts, const ASN1OCTET* data)
{
   indent ();
   OOTRACEDBGB1 ("< encoded data >\n");
}

static const char* bitStrToString 
(ASN1UINT numbits, const ASN1OCTET* data, char* buffer, size_t bufsiz)
{
   size_t i;
   unsigned char mask = 0x80;

   if (bufsiz > 0) {
      buffer[0] = '\'';
      for (i = 0; i < numbits; i++) {
         if (i < bufsiz - 1) {
            buffer[i+1] = (char) (((data[i/8] & mask) != 0) ? '1' : '0');
            mask >>= 1;
            if (0 == mask) mask = 0x80;
         }
         else break;
      }
     i++;
      if (i < bufsiz - 1) buffer[i++] = '\'';
      if (i < bufsiz - 1) buffer[i++] = 'B';
      if (i < bufsiz - 1) buffer[i] = '\0';
      else buffer[bufsiz - 1] = '\0';
   }

   return buffer;
}

static const char* octStrToString 
(ASN1UINT numocts, const ASN1OCTET* data, char* buffer, size_t bufsiz)
{
   size_t i;
   char lbuf[4];

   if (bufsiz > 0) {
      buffer[0] = '\'';
      if (bufsiz > 1) buffer[1] = '\0';
      for (i = 0; i < numocts; i++) {
         if (i < bufsiz - 1) {
            sprintf (lbuf, "%02hhx", (unsigned char)data[i]);
            strcat (&buffer[(i*2)+1], lbuf);
         }
         else break;
      }
     i = i*2 + 1;
      if (i < bufsiz - 1) buffer[i++] = '\'';
      if (i < bufsiz - 1) buffer[i++] = 'H';
      if (i < bufsiz - 1) buffer[i] = '\0';
      else buffer[bufsiz - 1] = '\0';
   }

   return buffer;
}
