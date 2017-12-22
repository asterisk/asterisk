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

#include "asterisk.h"
#include "asterisk/lock.h"

#include "ooasn1.h"
#include "ooper.h"

ASN1BOOL isExtendableSize (Asn1SizeCnst* pSizeList);
static ASN1BOOL isFixedSize (Asn1SizeCnst* pSizeList);

ASN1BOOL alignCharStr
(OOCTXT* pctxt, ASN1UINT len, ASN1UINT nbits, Asn1SizeCnst* pSize)
{
   if (TRUE) {
      ASN1UINT lower, upper;
      ASN1BOOL doAlign = (len > 0), extendable;

      pSize = checkSize (pSize, len, &extendable);

      if (0 != pSize) {
         lower = pSize->lower;
         upper = pSize->upper;
      }
      else {
         lower = 0;
         upper = ASN1UINT_MAX;
      }

      if (!extendable && upper < 65536) {
         ASN1UINT bitRange = upper * nbits;
         if (upper == lower) {
            /* X.691, clause 26.5.6 */
            if (bitRange <= 16) doAlign = FALSE;
         }
         else {
            /* X.691, clause 26.5.7 */
            if (bitRange < 16) doAlign = FALSE;
         }
      }

      return doAlign;
   }
   else
      return FALSE;
}

int bitAndOctetStringAlignmentTest (Asn1SizeCnst* pSizeList,
                                    ASN1UINT itemCount,
                                    ASN1BOOL bitStrFlag,
                                    ASN1BOOL* pAlignFlag)
{
   ASN1UINT threshold = (bitStrFlag) ? 16 : 2;

   if (pSizeList == 0 || itemCount > threshold)
      *pAlignFlag = TRUE;
   else if (isFixedSize(pSizeList))
      *pAlignFlag = FALSE;
   else {

      /* Variable length case: check size.. no alignment required if    */
      /* lower == upper and not extended..                              */

      ASN1BOOL extended;
      Asn1SizeCnst* pSize = checkSize (pSizeList, itemCount, &extended);

      if (pSize != 0)
         *pAlignFlag = ((pSize->upper != pSize->lower) || pSize->extended);
      else {
         /* Note: we never should get here because constraint           */
         /* violation should have been caught when length was encoded   */
         /* or decoded..                                                */
         return (ASN_E_CONSVIO);
      }
   }

   return (ASN_OK);
}

Asn1SizeCnst* checkSize (Asn1SizeCnst* pSizeList,
                         ASN1UINT value,
                         ASN1BOOL* pExtendable)
{
   Asn1SizeCnst* lpSize = pSizeList;
   *pExtendable = isExtendableSize (lpSize);

   while (lpSize) {
      if (value >= lpSize->lower && value <= lpSize->upper) {
         return (lpSize);
      }
      else lpSize = lpSize->next;
   }

   return 0;
}

int getPERMsgLen (OOCTXT* pctxt)
{
   return (pctxt->buffer.bitOffset == 8) ?
      pctxt->buffer.byteIndex : pctxt->buffer.byteIndex + 1;
}

int addSizeConstraint (OOCTXT* pctxt, Asn1SizeCnst* pSize)
{
   Asn1SizeCnst* lpSize;
   int stat = ASN_OK;

   /* If constraint does not already exist, add it */

   if (!pctxt->pSizeConstraint) {
      pctxt->pSizeConstraint = pSize;
   }

   /* Otherwise, check to make sure given constraint is larger than     */
   /* the existing constraint..                                         */

   else {
      lpSize = pSize;
      while (lpSize) {
         if (pctxt->pSizeConstraint->lower <= lpSize->lower ||
             pctxt->pSizeConstraint->upper >= lpSize->upper)
         {
            /* Set the extension flag to the value of the size          */
            /* constraint structure that the item falls within..        */

            /* pctxt->pSizeConstraint->extended = lpSize->extended; */

            break;
         }
         lpSize = lpSize->next;
      }

      if (!lpSize)
         stat = ASN_E_CONSVIO;
   }

   return stat;
}

Asn1SizeCnst* getSizeConstraint (OOCTXT* pctxt, ASN1BOOL extbit)
{
   Asn1SizeCnst* lpSize = pctxt->pSizeConstraint;

   while (lpSize) {
      if (lpSize->extended == extbit)
         return lpSize;
      else
         lpSize = lpSize->next;
   }

   return NULL;
}

int checkSizeConstraint(OOCTXT* pctxt, int size)
{
   Asn1SizeCnst* pSize;
   ASN1UINT upper;
   ASN1BOOL extbit;
   int      stat;

   /* If size constraint is present and extendable, decode extension    */
   /* bit..                                                             */

   if (isExtendableSize(pctxt->pSizeConstraint)) {
      stat = DE_BIT (pctxt, &extbit);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
   }
   else extbit = 0;

   /* Now use the value of the extension bit to select the proper       */
   /* size constraint range specification..                             */

   pSize = getSizeConstraint (pctxt, extbit);

   upper = (pSize) ? pSize->upper : ASN1UINT_MAX;

   if (upper < (ASN1UINT)size) {
      return LOG_ASN1ERR (pctxt, ASN_E_CONSVIO);
   }

   return ASN_OK;
}

ASN1UINT getUIntBitCount (ASN1UINT value)
{
   /* Binary search - decision tree (5 tests, rarely 6) */
   return
      ((value < 1<<15) ?
       ((value < 1<<7) ?
        ((value < 1<<3) ?
         ((value < 1<<1) ? ((value < 1<<0) ? 0 : 1) :
          ((value < 1<<2) ? 2 : 3)) :
         ((value < 1<<5) ? ((value < 1<<4) ? 4 : 5) :
          ((value < 1<<6) ? 6 : 7))) :
        ((value < 1<<11) ?
         ((value < 1<<9) ? ((value < 1<<8) ? 8 : 9) :
          ((value < 1<<10) ? 10 : 11)) :
         ((value < 1<<13) ? ((value < 1<<12) ? 12 : 13) :
          ((value < 1<<14) ? 14 : 15)))) :
       ((value < 1<<23) ?
        ((value < 1<<19) ?
         ((value < 1<<17) ? ((value < 1<<16) ? 16 : 17) :
          ((value < 1<<18) ? 18 : 19)) :
         ((value < 1<<21) ? ((value < 1<<20) ? 20 : 21) :
          ((value < 1<<22) ? 22 : 23))) :
        ((value < 1<<27) ?
         ((value < 1<<25) ? ((value < 1<<24) ? 24 : 25) :
          ((value < 1<<26) ? 26 : 27)) :
         ((value < 1<<29) ? ((value < 1<<28) ? 28 : 29) :
          ((value < 1<<30) ? 30 :
           ((value < 1UL<<31) ? 31 : 32))))));
}

void init16BitCharSet (Asn116BitCharSet* pCharSet, ASN116BITCHAR first,
                       ASN116BITCHAR last, ASN1UINT abits, ASN1UINT ubits)
{
   pCharSet->charSet.nchars = 0;
   pCharSet->charSet.data = 0;
   pCharSet->firstChar = first;
   pCharSet->lastChar  = last;
   pCharSet->unalignedBits = ubits;
   pCharSet->alignedBits = abits;
}

ASN1BOOL isExtendableSize (Asn1SizeCnst* pSizeList)
{
   Asn1SizeCnst* lpSize = pSizeList;
   while (lpSize) {
      if (lpSize->extended)
         return TRUE;
      else
         lpSize = lpSize->next;
   }
   return FALSE;
}

static ASN1BOOL isFixedSize (Asn1SizeCnst* pSizeList)
{
   Asn1SizeCnst* lpSize = pSizeList;
   if (lpSize && !lpSize->extended && !lpSize->next) {
      return (ASN1BOOL) (lpSize->lower == lpSize->upper);
   }
   return FALSE;
}

void set16BitCharSet
(OOCTXT* pctxt, Asn116BitCharSet* pCharSet, Asn116BitCharSet* pAlphabet)
{
   /* Permitted alphabet range can either be specified as a range of    */
   /* characters or as a discrete set..                                 */

   if (pAlphabet->charSet.data) {
      int nocts = pAlphabet->charSet.nchars * 2;
      pCharSet->charSet.nchars = pAlphabet->charSet.nchars;

      pCharSet->charSet.data =
         (ASN116BITCHAR*) ASN1MALLOC (pctxt, nocts);

      if (pCharSet->charSet.data != NULL)
         memcpy (pCharSet->charSet.data, pAlphabet->charSet.data, nocts);
   }
   else {
      pCharSet->firstChar = pAlphabet->firstChar;
      pCharSet->lastChar  = pAlphabet->lastChar;
      pCharSet->charSet.nchars = pCharSet->lastChar - pCharSet->firstChar;
   }

   pCharSet->unalignedBits = getUIntBitCount (pCharSet->charSet.nchars);

   pCharSet->alignedBits = 1;
   while (pCharSet->unalignedBits > pCharSet->alignedBits)
      pCharSet->alignedBits <<= 1;

}
