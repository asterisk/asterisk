/*
 * Copyright (C) 1997-2005 by Objective Systems, Inc.
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

/* Error management functions */

#include <stdlib.h>
#include "ooasn1.h"

/* Error status text */
static const char* g_status_text[] = {
    "Encode buffer overflow",
    "Unexpected end of buffer on decode",
    "Unexpected tag encountered: expected = %s, parsed = %s",
    "Invalid object identifier",
    "Invalid field length detected",
    "Enumerated value %s not in defined set",
    "Duplicate element in SET",
    "Missing required element in SET",
    "Element with tag %s not part of SET",
    "Max elements defined for SEQUENCE field exceeded",
    "Element with tag %s is an invalid option in choice",
    "No dynamic memory available",
    "Invalid string type",
    "Invalid hex string",
    "Invalid binary string",
    "Invalid real value",
    "Max items in sized BIT or OCTET STRING field exceeded",
    "Invalid value specification",
    "No definition found for referenced defined value",
    "No definition found for referenced defined type",
    "Invalid tag value",
    "Nesting level too deep",
    "Value constraint violation: field %s, value %s",
    "Value range error: lower bound is greater than upper",
    "Unexpected end of file detected",
    "Invalid UTF-8 character at index %d", 
    "List error: concurrent modification attempt while iterating", 
    "List error: illegal state for attempted operation",
    "Array index out of bounds",
    "Invalid parameter passed to function or method",
    "Invalid time string format",
    "Context is not initialized", 
    "ASN.1 value will not fit in target variable", 
    "Character is not within the defined character set", 
    "Invalid XML state for attempted operation", 
    "Error condition returned from XML parser:\n%s", 
    "SEQUENCE elements not in correct order",
    "Invalid index for table constraint identifier",
    "Invalid value for relational table constraint fixed type field", 
    "File not found", 
    "File read error",
    "File write error",
    "Invalid Base64 string",
    "Socket error",
    "XML interface library not found",
    "Invalid XML interface library"
} ;

#define ASN1_K_MAX_STAT (sizeof(g_status_text)/sizeof(char *))

/* Add an integer parameter to an error message */

int errAddIntParm (ASN1ErrInfo* pErrInfo, int errParm)
{
   char lbuf[16];
   sprintf (lbuf, "%d", errParm);
   return errAddStrParm (pErrInfo, lbuf);
}

/* Add a character string parameter to an error message */

int errAddStrParm (ASN1ErrInfo* pErrInfo, const char* errprm_p)
{
#if defined(_NO_THREADS) || !defined(_NO_MALLOC)
   if (pErrInfo->parmcnt < ASN_K_MAXERRP) {
      char* tmpstr = (char*) ASN1CRTMALLOC0 (strlen(errprm_p)+1);
      strcpy (tmpstr, errprm_p);
      pErrInfo->parms[pErrInfo->parmcnt] = tmpstr;
      pErrInfo->parmcnt++;
      return TRUE;
   }
   else
#endif
      return FALSE;
}

/* Add an unsigned integer parameter to an error message */

int errAddUIntParm (ASN1ErrInfo* pErrInfo, unsigned int errParm)
{
   char lbuf[16];
   sprintf (lbuf, "%u", errParm);
   return errAddStrParm (pErrInfo, lbuf);
}

/* Free error parameter memory */

void errFreeParms (ASN1ErrInfo* pErrInfo)
{
#if defined(_NO_THREADS) || !defined(_NO_MALLOC)
   int i;

   for (i = 0; i < pErrInfo->parmcnt; i++)
      ASN1CRTFREE0 ((char*)pErrInfo->parms[i]);
#endif

   pErrInfo->parmcnt = 0;
   pErrInfo->status = 0;
}

/* Reset error */

int errReset (ASN1ErrInfo* pErrInfo)
{
   errFreeParms (pErrInfo);
   pErrInfo->stkx = 0;
   return ASN_OK;
}

/* Format error message */

char* errFmtMsg (ASN1ErrInfo* pErrInfo, char* bufp)
{
   const char* tp;
   int  i, j, pcnt;

   if (pErrInfo->status < 0)
   {
      i = abs (pErrInfo->status + 1);

      if (i >= 0 && i < ASN1_K_MAX_STAT)
      {
         /* Substitute error parameters into error message */

         j  = pcnt = 0;
         tp = g_status_text[i];

         while (*tp) 
         {
            if (*tp == '%' && *(tp+1) == 's')
            {
               /* Plug in error parameter */

               if (pcnt < pErrInfo->parmcnt && pErrInfo->parms[pcnt])
               {
                  strcpy (&bufp[j], pErrInfo->parms[pcnt]);
                  j += strlen (pErrInfo->parms[pcnt++]);
               }
               else
                  bufp[j++] = '?';

               tp += 2;
            }
            else
               bufp[j++] = *tp++;
         }

         bufp[j] = '\0';        /* null terminate string */
      }
      else
         strcpy (bufp, "unrecognized completion status");
   }    
   else strcpy (bufp, "normal completion status");

   return (bufp);
}

/* Get error text in a dynamic memory buffer.  This allocates memory    */
/* using the 'memAlloc' function.  This memory is automatically freed */ 
/* at the time the 'memFree' function is called.                      */

char* errGetText (OOCTXT* pctxt)
{
   char lbuf[500];
   char* pBuf = (char*) ASN1MALLOC (pctxt,
      (sizeof(lbuf) + 100 * (2 + pctxt->errInfo.stkx)) * sizeof(char));

   sprintf (pBuf, "ASN.1 ERROR: Status %d\n", pctxt->errInfo.status);
   sprintf (lbuf, "%s\nStack trace:", errFmtMsg (&pctxt->errInfo, lbuf));
   strcat(pBuf, lbuf);

   while (pctxt->errInfo.stkx > 0) {
      pctxt->errInfo.stkx--;
      sprintf (lbuf, "  Module: %s, Line %d\n", 
               pctxt->errInfo.stack[pctxt->errInfo.stkx].module,
               pctxt->errInfo.stack[pctxt->errInfo.stkx].lineno);
      strcat(pBuf, lbuf);
   }

   errFreeParms (&pctxt->errInfo);

   return pBuf;
}

/* Print error information to the standard output */

void errPrint (ASN1ErrInfo* pErrInfo)
{
   char lbuf[200];
   printf ("ASN.1 ERROR: Status %d\n", pErrInfo->status);
   printf ("%s\n", errFmtMsg (pErrInfo, lbuf));
   printf ("Stack trace:");
   while (pErrInfo->stkx > 0) {
      pErrInfo->stkx--;
      printf ("  Module: %s, Line %d\n", 
              pErrInfo->stack[pErrInfo->stkx].module,
              pErrInfo->stack[pErrInfo->stkx].lineno);
   }
   errFreeParms (pErrInfo);
}

/* Copy error data from one error structure to another */

int errCopyData (ASN1ErrInfo* pSrcErrInfo, ASN1ErrInfo* pDestErrInfo)
{
   int i;
   pDestErrInfo->status = pSrcErrInfo->status;

   /* copy error parameters */

   for (i = 0; i < pSrcErrInfo->parmcnt; i++) {
      errAddStrParm (pDestErrInfo, pSrcErrInfo->parms[i]);
   }

   /* copy stack info */

   for (i = 0; i < pSrcErrInfo->stkx; i++) {
      if (pDestErrInfo->stkx < ASN_K_MAXERRSTK) {
         pDestErrInfo->stack[pDestErrInfo->stkx].module = 
            pSrcErrInfo->stack[i].module;
         pDestErrInfo->stack[pDestErrInfo->stkx++].lineno = 
            pSrcErrInfo->stack[i].lineno;
      }
   }

   return (pSrcErrInfo->status);
}


int errSetData (ASN1ErrInfo* pErrInfo, int status, 
                  const char* module, int lno) 
{ 
   if (pErrInfo->status == 0) {
      pErrInfo->status = status;
   }
   ooLogAsn1Error(status, module, lno);
   return status; 
}
