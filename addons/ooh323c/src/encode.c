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
#include <stdlib.h>

#include "ooasn1.h"

static int encode16BitConstrainedString
(OOCTXT* pctxt, Asn116BitCharString value, Asn116BitCharSet* pCharSet);

static int encode2sCompBinInt (OOCTXT* pctxt, ASN1INT value);
static int encodeNonNegBinInt (OOCTXT* pctxt, ASN1UINT value);
static int encodeUnconsLength (OOCTXT* pctxt, ASN1UINT value);
static int getIdentByteCount (ASN1UINT ident);

int encodeBitsFromOctet (OOCTXT* pctxt, ASN1OCTET value, ASN1UINT nbits);
int encodeGetMsgBitCnt (OOCTXT* pctxt);
int encodeIdent (OOCTXT* pctxt, ASN1UINT ident);


int encodeBit (OOCTXT* pctxt, ASN1BOOL value)
{
   int stat = ASN_OK;

   /* If start of new byte, init to zero */

   if (pctxt->buffer.bitOffset == 8) {
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
   }

   /* Adjust bit offset and determine if at end of current byte */

   if (--pctxt->buffer.bitOffset < 0) {
      if (++pctxt->buffer.byteIndex >= pctxt->buffer.size) {
         if ((stat = encodeExpandBuffer (pctxt, 1)) != ASN_OK) {
            return stat;
         }
      }
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
      pctxt->buffer.bitOffset = 7;
   }

   /* Set single-bit value */

   if (value) {
      pctxt->buffer.data[pctxt->buffer.byteIndex] |=
         ( 1 << pctxt->buffer.bitOffset );
   }

   /* If last bit in octet, set offsets to start new byte (ED, 9/7/01) */

   if (pctxt->buffer.bitOffset == 0) {
      pctxt->buffer.bitOffset = 8;
      pctxt->buffer.byteIndex++;
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
   }

   return stat;
}

int encodeBits (OOCTXT* pctxt, ASN1UINT value, ASN1UINT nbits)
{
   int nbytes = (nbits + 7)/ 8, stat = ASN_OK;

   if (nbits == 0) return stat;

   /* If start of new byte, init to zero */

   if (pctxt->buffer.bitOffset == 8) {
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
   }

   /* Mask off unused bits from the front of the value */

   if (nbits < (sizeof(ASN1UINT) * 8))
      value &= ((1 << nbits) - 1);

   /* If bits will fit in current byte, set them and return */

   if (nbits < (unsigned)pctxt->buffer.bitOffset) {
      pctxt->buffer.bitOffset -= nbits;
      pctxt->buffer.data[pctxt->buffer.byteIndex] |=
         ( value << pctxt->buffer.bitOffset );
      return stat;
   }

   /* Check buffer space and allocate more memory if necessary */

   stat = encodeCheckBuffer (pctxt, nbytes);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* Set bits in remainder of the current byte and then loop   */
   /* to set bits in subsequent bytes..                         */

   nbits -= pctxt->buffer.bitOffset;
   pctxt->buffer.data[pctxt->buffer.byteIndex++] |=
      (ASN1OCTET)( value >> nbits );
   pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;

   while (nbits >= 8) {
      nbits -= 8;
      pctxt->buffer.data[pctxt->buffer.byteIndex++] =
         (ASN1OCTET)( value >> nbits );
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
   }

   /* copy final partial byte */

   pctxt->buffer.bitOffset = 8 - nbits;
   if (nbits > 0) {
      pctxt->buffer.data[pctxt->buffer.byteIndex] =
         (ASN1OCTET)((value & ((1 << nbits)-1)) << pctxt->buffer.bitOffset);
   }
   else
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;

   return stat;
}

int encodeBitsFromOctet (OOCTXT* pctxt, ASN1OCTET value, ASN1UINT nbits)
{
   int lshift = pctxt->buffer.bitOffset;
   int rshift = 8 - pctxt->buffer.bitOffset;
   int stat = ASN_OK;
   ASN1OCTET mask = 0x0;

   if (nbits == 0) return ASN_OK;

   /* Mask off unused bits from the end of the value */

   if (nbits < 8) {
      switch (nbits) {
      case 1: mask = 0x80; break;
      case 2: mask = 0xC0; break;
      case 3: mask = 0xE0; break;
      case 4: mask = 0xF0; break;
      case 5: mask = 0xF8; break;
      case 6: mask = 0xFC; break;
      case 7: mask = 0xFE; break;
      default:;
      }
      value &= mask;
   }

   /* If we are on a byte boundary, we can do a direct assignment */

   if (pctxt->buffer.bitOffset == 8) {
      pctxt->buffer.data[pctxt->buffer.byteIndex] = value;
      if (nbits == 8) {
         pctxt->buffer.byteIndex++;
         pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
      }
      else
         pctxt->buffer.bitOffset -= nbits;
   }

   /* Otherwise, need to set some bits in the first octet and   */
   /* possibly some bits in the following octet..               */

   else {
      pctxt->buffer.data[pctxt->buffer.byteIndex] |=
         (ASN1OCTET)(value >> rshift);

      pctxt->buffer.bitOffset -= nbits;

      if (pctxt->buffer.bitOffset < 0) {
         pctxt->buffer.byteIndex++;
         pctxt->buffer.data[pctxt->buffer.byteIndex] =
            (ASN1OCTET)(value << lshift);
         pctxt->buffer.bitOffset += 8;
      }
   }

   return stat;
}

int encodeBitString (OOCTXT* pctxt, ASN1UINT numbits, const ASN1OCTET* data)
{
   int enclen, octidx = 0, stat;
   Asn1SizeCnst* pSizeList = pctxt->pSizeConstraint;

   for (;;) {
      if ((enclen = encodeLength (pctxt, numbits)) < 0) {
         return LOG_ASN1ERR (pctxt, enclen);
      }

      if (enclen > 0) {
         ASN1BOOL doAlign;

         stat = bitAndOctetStringAlignmentTest
            (pSizeList, numbits, TRUE, &doAlign);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         if (doAlign) {
            stat = encodeByteAlign (pctxt);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
         }

         stat = encodeOctets (pctxt, &data[octidx], enclen);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
      }

      if (enclen < (int)numbits) {
         numbits -= enclen;
         octidx += (enclen/8);
      }
      else break;
   }

   return ASN_OK;
}

int encodeBMPString
(OOCTXT* pctxt, ASN1BMPString value, Asn116BitCharSet* permCharSet)
{
   Asn116BitCharSet charSet;
   int stat;

   /* Set character set */

   init16BitCharSet (&charSet, BMP_FIRST, BMP_LAST, BMP_ABITS, BMP_UBITS);

   if (permCharSet) {
      set16BitCharSet (pctxt, &charSet, permCharSet);
   }

   /* Encode constrained string */

   stat = encode16BitConstrainedString (pctxt, value, &charSet);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   return stat;
}

int encodeByteAlign (OOCTXT* pctxt)
{
   if (pctxt->buffer.bitOffset != 8) {
      if ((pctxt->buffer.byteIndex + 1) >= pctxt->buffer.size) {
         int stat = encodeExpandBuffer (pctxt, 1);
         if (stat != ASN_OK) return (stat);
      }
      pctxt->buffer.byteIndex++;
      pctxt->buffer.bitOffset = 8;
      pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
   }

   return ASN_OK;
}

int encodeCheckBuffer (OOCTXT* pctxt, ASN1UINT nbytes)
{
   int stat = ASN_OK;

   /* Add one to required bytes because increment logic will always     */
   /* init the byte at the incremented index to zero..                  */

   if ( ( pctxt->buffer.byteIndex + nbytes + 1 ) >= pctxt->buffer.size ) {
      if ((stat = encodeExpandBuffer (pctxt, nbytes+1)) != ASN_OK) {
         return LOG_ASN1ERR (pctxt, stat);
      }
   }

   return (stat);
}

int encodeConsInteger
(OOCTXT* pctxt, ASN1INT value, ASN1INT lower, ASN1INT upper)
{
   ASN1UINT range_value;
   ASN1UINT adjusted_value;
   int stat;

   /* Check value against given range */

   if (value < lower || value > upper) {
      return ASN_E_CONSVIO;
   }

   /* Adjust range value based on lower/upper signed values and */
   /* other possible conflicts..                                */

   if ((upper > 0 && lower >= 0) || (upper <= 0 && lower < 0)) {
      range_value = upper - lower;
      adjusted_value = value - lower;
   }
   else {
      range_value = upper + abs(lower);
      adjusted_value = value + abs(lower);
   }

   if (range_value != ASN1UINT_MAX) { range_value += 1; }

   if (range_value == 0 || lower > upper)
      stat = ASN_E_RANGERR;
   else if (lower != upper) {
      stat = encodeConsWholeNumber (pctxt, adjusted_value, range_value);
   }
   else
      stat = ASN_OK;

   return stat;
}

int encodeConsUnsigned
(OOCTXT* pctxt, ASN1UINT value, ASN1UINT lower, ASN1UINT upper)
{
   ASN1UINT range_value;
   ASN1UINT adjusted_value;
   int stat;

   /* Check for special case: if lower is 0 and upper is ASN1UINT_MAX,  */
   /* set range to ASN1UINT_MAX; otherwise to upper - lower + 1         */

   range_value = (lower == 0 && upper == ASN1UINT_MAX) ?
      ASN1UINT_MAX : upper - lower + 1;

   adjusted_value = value - lower;

   if (lower != upper) {
      stat = encodeConsWholeNumber (pctxt, adjusted_value, range_value);
   }
   else
      stat = ASN_OK;

   return stat;
}

int encodeConsWholeNumber
(OOCTXT* pctxt, ASN1UINT adjusted_value, ASN1UINT range_value)
{
   ASN1UINT nocts, range_bitcnt = getUIntBitCount (range_value - 1);
   int stat;

   if (adjusted_value >= range_value && range_value != ASN1UINT_MAX) {
      return LOG_ASN1ERR (pctxt, ASN_E_RANGERR);
   }

   /* If range is <= 255, bit-field case (10.5.7a) */

   if (range_value <= 255) {
      return encodeBits (pctxt, adjusted_value, range_bitcnt);
   }

   /* If range is exactly 256, one-octet case (10.5.7b) */

   else if (range_value == 256) {
      stat = encodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      return encodeBits (pctxt, adjusted_value, 8);
   }

   /* If range > 256 and <= 64k (65536), two-octet case (10.5.7c) */

   else if (range_value <= 65536) {
      stat = encodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      return encodeBits (pctxt, adjusted_value, 16);
   }

   /* If range > 64k, indefinite-length case (10.5.7d) */

   else {
      /* Encode length determinant as a constrained whole number.    */
      /* Constraint is 1 to max number of bytes needed to hold       */
      /* the target integer value..                                  */

      if (adjusted_value < 256) nocts = 1;
      else if (adjusted_value < 65536) nocts = 2;
      else if (adjusted_value < 0x1000000) nocts = 3;
      else nocts = 4;

      stat = encodeBits (pctxt, nocts - 1, 2);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      stat = encodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      return encodeNonNegBinInt (pctxt, adjusted_value);
   }
}

int encodeConstrainedStringEx (OOCTXT* pctxt,
                            const char* string,
                            const char* charSet,
                            ASN1UINT abits,  /* aligned char bits */
                            ASN1UINT ubits,  /* unaligned char bits */
                            ASN1UINT canSetBits)
{
   ASN1UINT i, len = strlen(string);
   int      stat;
   /* note: need to save size constraint for use in alignCharStr     */
   /* because it will be cleared in encodeLength from the context..        */
   Asn1SizeCnst* psize = pctxt->pSizeConstraint;

   /* Encode length */

   stat = encodeLength (pctxt, len);
   if (stat < 0) return LOG_ASN1ERR (pctxt, stat);

   /* Byte align */

   if (alignCharStr (pctxt, len, abits, psize)) {
      stat = encodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
   }

   /* Encode data */

   if (abits >= canSetBits && canSetBits > 4) {
      for (i = 0; i < len; i++) {
         if ((stat = encodeBits (pctxt, string[i], abits)) != ASN_OK)
            return LOG_ASN1ERR (pctxt, stat);
      }
   }
   else if (0 != charSet) {
      ASN1UINT nchars = strlen(charSet), pos;
      const char* ptr;
      for (i = 0; i < len; i++) {
         ptr = memchr (charSet, string[i], nchars);

         if (0 == ptr)
            return LOG_ASN1ERR (pctxt, ASN_E_CONSVIO);
         else
            pos = ptr - charSet;

         if ((stat = encodeBits (pctxt, pos, abits)) != ASN_OK)
            return LOG_ASN1ERR (pctxt, stat);
      }
   }
   else return LOG_ASN1ERR (pctxt, ASN_E_INVPARAM);

   return stat;
}

int encodeExpandBuffer (OOCTXT* pctxt, ASN1UINT nbytes)
{
   if (pctxt->buffer.dynamic)
   {
      /* If dynamic encoding is enabled, expand the current buffer to   */
      /* allow encoding to continue.                                    */

      pctxt->buffer.size += ASN1MAX (ASN_K_ENCBUFSIZ, nbytes);

      pctxt->buffer.data = (ASN1OCTET*) memHeapRealloc
         (&pctxt->pMsgMemHeap, pctxt->buffer.data, pctxt->buffer.size);

      if (!pctxt->buffer.data) return (ASN_E_NOMEM);

      return (ASN_OK);
   }

   return (ASN_E_BUFOVFLW);
}

int encodeGetMsgBitCnt (OOCTXT* pctxt)
{
   int numBitsInLastByte = 8 - pctxt->buffer.bitOffset;
   return ((pctxt->buffer.byteIndex * 8) + numBitsInLastByte);
}

ASN1OCTET* encodeGetMsgPtr (OOCTXT* pctxt, int* pLength)
{
   if (pLength) *pLength = getPERMsgLen (pctxt);
   return pctxt->buffer.data;
}

int encodeIdent (OOCTXT* pctxt, ASN1UINT ident)
{
   ASN1UINT mask;
   int nshifts = 0, stat;

   if (ident !=0) {
      ASN1UINT lv;
      nshifts = getIdentByteCount (ident);
      while (nshifts > 0) {
         mask = ((ASN1UINT)0x7f) << (7 * (nshifts - 1));
         nshifts--;
         lv = (ASN1UINT)((ident & mask) >> (nshifts * 7));
         if (nshifts != 0) { lv |= 0x80; }
         if ((stat = encodeBits (pctxt, lv, 8)) != ASN_OK)
            return LOG_ASN1ERR (pctxt, stat);
      }
   }
   else {
      /* encode a single zero byte */
      if ((stat = encodeBits (pctxt, 0, 8)) != ASN_OK)
         return LOG_ASN1ERR (pctxt, stat);
   }

   return ASN_OK;
}

int encodeLength (OOCTXT* pctxt, ASN1UINT value)
{
   ASN1BOOL extendable;
   Asn1SizeCnst* pSize =
      checkSize (pctxt->pSizeConstraint, value, &extendable);
   ASN1UINT lower = (pSize) ? pSize->lower : 0;
   ASN1UINT upper = (pSize) ? pSize->upper : ASN1UINT_MAX;
   int enclen, stat;

   /* If size constraints exist and the given length did not fall       */
   /* within the range of any of them, signal constraint violation      */
   /* error..                                                           */

   if (pctxt->pSizeConstraint && !pSize)
      return LOG_ASN1ERR (pctxt, ASN_E_CONSVIO);

   /* Reset the size constraint in the context block structure */

   pctxt->pSizeConstraint = 0;

   /* If size constraint is present and extendable, encode extension    */
   /* bit..                                                             */

   if (extendable) {
      stat = (pSize) ?
         encodeBit (pctxt, pSize->extended) : encodeBit (pctxt, 1);

      if (stat != ASN_OK) return (stat);
   }

   /* If upper limit is less than 64k, constrained case */

   if (upper < 65536) {
      stat = (lower == upper) ? ASN_OK :
         encodeConsWholeNumber (pctxt, value - lower, upper - lower + 1);
      enclen = (stat == ASN_OK) ? value : stat;
   }
   else {
      /* unconstrained case or Constrained with upper bound >= 64K*/
      enclen = encodeUnconsLength (pctxt, value);
   }

   return enclen;

}

int encodeObjectIdentifier (OOCTXT* pctxt, ASN1OBJID* pvalue)
{
   int len, stat;
   ASN1UINT temp;
   register int numids, i;

   /* Calculate length in bytes and encode */

   len = 1;  /* 1st 2 arcs require 1 byte */
   numids = pvalue->numids;
   for (i = 2; i < numids; i++) {
      len += getIdentByteCount (pvalue->subid[i]);
   }

   /* PER encode length */

   if ((stat = encodeLength (pctxt, (ASN1UINT)len)) < 0) {
      return LOG_ASN1ERR (pctxt, stat);
   }

   /* Validate given object ID by applying ASN.1 rules */

   if (0 == pvalue) return LOG_ASN1ERR (pctxt, ASN_E_INVOBJID);
   if (numids < 2) return LOG_ASN1ERR (pctxt, ASN_E_INVOBJID);
   if (pvalue->subid[0] > 2) return LOG_ASN1ERR (pctxt, ASN_E_INVOBJID);
   if (pvalue->subid[0] != 2 && pvalue->subid[1] > 39)
      return LOG_ASN1ERR (pctxt, ASN_E_INVOBJID);

   /* Passed checks, encode object identifier */

   /* Munge first two sub ID's and encode */

   temp = ((pvalue->subid[0] * 40) + pvalue->subid[1]);
   if ((stat = encodeIdent (pctxt, temp)) != ASN_OK)
      return LOG_ASN1ERR (pctxt, stat);

   /* Encode the remainder of the OID value */

   for (i = 2; i < numids; i++) {
      if ((stat = encodeIdent (pctxt, pvalue->subid[i])) != ASN_OK)
         return LOG_ASN1ERR (pctxt, stat);
   }

   return ASN_OK;
}

int encodebitsFromOctet (OOCTXT* pctxt, ASN1OCTET value, ASN1UINT nbits)
{
   int lshift = pctxt->buffer.bitOffset;
   int rshift = 8 - pctxt->buffer.bitOffset;
   int stat = ASN_OK;
   ASN1OCTET mask = 0x0;

   if (nbits == 0) return ASN_OK;

   /* Mask off unused bits from the end of the value */

   if (nbits < 8) {
      switch (nbits) {
      case 1: mask = 0x80; break;
      case 2: mask = 0xC0; break;
      case 3: mask = 0xE0; break;
      case 4: mask = 0xF0; break;
      case 5: mask = 0xF8; break;
      case 6: mask = 0xFC; break;
      case 7: mask = 0xFE; break;
      default:;
      }
      value &= mask;
   }

   /* If we are on a byte boundary, we can do a direct assignment */

   if (pctxt->buffer.bitOffset == 8) {
      pctxt->buffer.data[pctxt->buffer.byteIndex] = value;
      if (nbits == 8) {
         pctxt->buffer.byteIndex++;
         pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
      }
      else
         pctxt->buffer.bitOffset -= nbits;
   }

   /* Otherwise, need to set some bits in the first octet and   */
   /* possibly some bits in the following octet..               */

   else {
      pctxt->buffer.data[pctxt->buffer.byteIndex] |=
         (ASN1OCTET)(value >> rshift);

      pctxt->buffer.bitOffset -= nbits;

      if (pctxt->buffer.bitOffset < 0) {
         pctxt->buffer.byteIndex++;
         pctxt->buffer.data[pctxt->buffer.byteIndex] =
            (ASN1OCTET)(value << lshift);
         pctxt->buffer.bitOffset += 8;
      }
   }

   return stat;
}

int encodeOctets (OOCTXT* pctxt, const ASN1OCTET* pvalue, ASN1UINT nbits)
{
   int i = 0, stat;
   int numFullOcts = nbits / 8;

   if (nbits == 0) return 0;

   /* Check buffer space and allocate more memory if necessary */

   stat = encodeCheckBuffer (pctxt, numFullOcts + 1);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   if (numFullOcts > 0) {

      /* If the current bit offset is 8 (i.e. we don't have a      */
      /* byte started), can copy the string directly to the        */
      /* encode buffer..                                           */

      if (pctxt->buffer.bitOffset == 8) {
         memcpy (&pctxt->buffer.data[pctxt->buffer.byteIndex], pvalue,
                 numFullOcts);
         pctxt->buffer.byteIndex += numFullOcts;
         pctxt->buffer.data[pctxt->buffer.byteIndex] = 0;
         i = numFullOcts;
      }

      /* Else, copy bits */

      else {
         for (i = 0; i < numFullOcts; i++) {
            stat = encodeBitsFromOctet (pctxt, pvalue[i], 8);
            if (stat != ASN_OK) return stat;
         }
      }
   }

   /* Move remaining bits from the last octet to the output buffer */

   if (nbits % 8 != 0) {
      stat = encodeBitsFromOctet (pctxt, pvalue[i], nbits % 8);
   }

   return stat;
}

int encodeOctetString (OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data)
{
   int enclen, octidx = 0, stat;
   Asn1SizeCnst* pSizeList = pctxt->pSizeConstraint;

   for (;;) {
      if ((enclen = encodeLength (pctxt, numocts)) < 0) {
         return LOG_ASN1ERR (pctxt, enclen);
      }

      if (enclen > 0) {
         ASN1BOOL doAlign;

         stat = bitAndOctetStringAlignmentTest
            (pSizeList, numocts, FALSE, &doAlign);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         if (doAlign) {
            stat = encodeByteAlign (pctxt);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
         }

         stat = encodeOctets (pctxt, &data[octidx], enclen * 8);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
      }

      if (enclen < (int)numocts) {
         numocts -= enclen;
         octidx += enclen;
      }
      else break;
   }

   return ASN_OK;
}

int encodeOpenType (OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data)
{
   int enclen, octidx = 0, stat;
   ASN1OCTET zeroByte = 0x00;
   ASN1OpenType openType;

   /* If open type contains length zero, add a single zero byte (10.1) */

   if (numocts == 0) {
      openType.numocts = 1;
      openType.data = &zeroByte;
   }
   else {
      openType.numocts = numocts;
      openType.data = data;
   }

   /* Encode the open type */

   for (;;) {
      if ((enclen = encodeLength (pctxt, openType.numocts)) < 0) {
         return LOG_ASN1ERR (pctxt, enclen);
      }

      stat = encodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      stat = encodeOctets (pctxt, &openType.data[octidx], enclen * 8);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      if (enclen < (int)openType.numocts) {
         openType.numocts -= enclen;
         octidx += enclen;
      }
      else break;
   }

   return ASN_OK;
}

int encodeOpenTypeExt (OOCTXT* pctxt, DList* pElemList)
{
   DListNode* pnode;
   ASN1OpenType* pOpenType;
   int stat;

   if (0 != pElemList) {
      pnode = pElemList->head;
      while (0 != pnode) {
         if (0 != pnode->data) {
            pOpenType = (ASN1OpenType*)pnode->data;

            if (pOpenType->numocts > 0) {
               stat = encodeByteAlign (pctxt);
               if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

               stat = encodeOpenType
                  (pctxt, pOpenType->numocts, pOpenType->data);

               if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
            }
         }
         pnode = pnode->next;
      }
   }

   return ASN_OK;
}

int encodeOpenTypeExtBits (OOCTXT* pctxt, DList* pElemList)
{
   DListNode* pnode;
   int stat;

   if (0 != pElemList) {
      pnode = pElemList->head;

      while (0 != pnode) {
         stat = encodeBit (pctxt, (ASN1BOOL)(0 != pnode->data));
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         pnode = pnode->next;
      }
   }

   return ASN_OK;
}

int encodeSemiConsInteger (OOCTXT* pctxt, ASN1INT value, ASN1INT lower)
{
   int nbytes, stat;
   int shift = ((sizeof(value) - 1) * 8) - 1;
   ASN1UINT tempValue;

   if (lower > ASN1INT_MIN)
      value -= lower;

   /* Calculate signed number value length */

   for ( ; shift > 0; shift -= 8) {
      tempValue = (value >> shift) & 0x1ff;
      if (tempValue == 0 || tempValue == 0x1ff) continue;
      else break;
   }

   nbytes = (shift + 9) / 8;

   /* Encode length */

   if ((stat = encodeLength (pctxt, nbytes)) < 0) {
      return stat;
   }

   if ((stat = encodeByteAlign (pctxt)) != ASN_OK)
      return stat;

   /* Encode signed value */

   stat = encode2sCompBinInt (pctxt, value);

   return stat;
}

int encodeSemiConsUnsigned (OOCTXT* pctxt, ASN1UINT value, ASN1UINT lower)
{
   int nbytes, stat;
   int shift = ((sizeof(value) - 1) * 8) - 1;
   ASN1UINT mask = 1UL << ((sizeof(value) * 8) - 1);
   ASN1UINT tempValue;

   value -= lower;

   /* Calculate unsigned number value length */

   for ( ; shift > 0; shift -= 8) {
      tempValue = (value >> shift) & 0x1ff;

      if (tempValue == 0) continue;
      else break;
   }

   nbytes = (shift + 9) / 8;

   /* If MS bit in unsigned number is set, add an extra zero byte */

   if ((value & mask) != 0) nbytes++;

   /* Encode length */

   if ((stat = encodeLength (pctxt, nbytes)) < 0) {
      return stat;
   }

   if ((stat = encodeByteAlign (pctxt)) != ASN_OK)
      return stat;

   /* Encode additional zero byte if necessary */

   if (nbytes > sizeof(value)) {
      stat = encodebitsFromOctet (pctxt, 0, 8);
      if (stat != ASN_OK) return (stat);
   }

   /* Encode unsigned value */

   stat = encodeNonNegBinInt (pctxt, value);

   return stat;
}

int encodeSmallNonNegWholeNumber (OOCTXT* pctxt, ASN1UINT value)
{
   int stat;

   if (value < 64) {
      stat = encodeBits (pctxt, value, 7);
   }
   else {
      ASN1UINT len;

      /* Encode a one-byte length determinant value */
      if (value < 256) len = 1;
      else if (value < 65536) len = 2;
      else if (value < 0x1000000) len = 3;
      else len = 4;

      stat = encodeBits (pctxt, len, 8);

      /* Byte-align and encode the value */
      if (stat == ASN_OK) {
         if ((stat = encodeByteAlign (pctxt)) == ASN_OK) {
            stat = encodeBits (pctxt, value, len*8);
         }
      }
   }

   return stat;
}

int encodeVarWidthCharString (OOCTXT* pctxt, const char* value)
{
   int         stat;
   ASN1UINT    len = strlen (value);
   /* note: need to save size constraint for use in alignCharStr     */
   /* because it will be cleared in encodeLength from the context..        */
   Asn1SizeCnst* psize = pctxt->pSizeConstraint;

   /* Encode length */

   stat = encodeLength (pctxt, len);
   if (stat < 0) return LOG_ASN1ERR (pctxt, stat);

   /* Byte align */

   if (alignCharStr (pctxt, len, 8, psize)) {
      stat = encodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
   }

   /* Encode data */

   stat = encodeOctets (pctxt, (const ASN1OCTET*)value, len * 8);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   return ASN_OK;
}

static int encode16BitConstrainedString
(OOCTXT* pctxt, Asn116BitCharString value, Asn116BitCharSet* pCharSet)
{
   ASN1UINT i, pos;
   ASN1UINT nbits = pCharSet->alignedBits;
   int stat;

   /* Encode length */

   stat = encodeLength (pctxt, value.nchars);
   if (stat < 0) return LOG_ASN1ERR (pctxt, stat);

   /* Byte align */

   stat = encodeByteAlign (pctxt);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* Encode data */

   for (i = 0; i < value.nchars; i++) {
      if (pCharSet->charSet.data == 0) {
         stat = encodeBits
            (pctxt, value.data[i] - pCharSet->firstChar, nbits);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
      }
      else {
         for (pos = 0; pos < pCharSet->charSet.nchars; pos++) {
            if (value.data[i] == pCharSet->charSet.data[pos]) {
               stat = encodeBits (pctxt, pos, nbits);
               if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
               break;
            }
         }
      }
   }

   return stat;
}

int encode2sCompBinInt (OOCTXT* pctxt, ASN1INT value)
{
   /* 10.4.6  A minimum octet 2's-complement-binary-integer encoding    */
   /* of the whole number has a field width that is a multiple of 8     */
   /* bits and also satisfies the condition that the leading 9 bits     */
   /* field shall not be all zeros and shall not be all ones.           */

   /* first encode integer value into a local buffer */

   ASN1OCTET lbuf[8], lb;
   ASN1INT   i = sizeof(lbuf), temp = value;

   memset (lbuf, 0, sizeof(lbuf));
   do {
      lb = temp % 256;
      temp /= 256;
      if (temp < 0 && lb != 0) temp--; /* two's complement adjustment */
      lbuf[--i] = lb;
   } while (temp != 0 && temp != -1);

   /* If the value is positive and bit 8 of the leading byte is set,    */
   /* copy a zero byte to the contents to signal a positive number..    */

   if (value > 0 && (lb & 0x80) != 0) {
      i--;
   }

   /* If the value is negative and bit 8 of the leading byte is clear,  */
   /* copy a -1 byte (0xFF) to the contents to signal a negative        */
   /* number..                                                          */

   else if (value < 0 && ((lb & 0x80) == 0)) {
      lbuf[--i] = 0xff;
   }

   /* Add the data to the encode buffer */

   return encodeOctets (pctxt, &lbuf[i], (sizeof(lbuf) - i) * 8);
}

static int encodeNonNegBinInt (OOCTXT* pctxt, ASN1UINT value)
{
   /* 10.3.6  A minimum octet non-negative binary integer encoding of   */
   /* the whole number (which does not predetermine the number of       */
   /* octets to be used for the encoding) has a field which is a        */
   /* multiple of 8 bits and also satisfies the condition that the      */
   /* leading eight bits of the field shall not be zero unless the      */
   /* field is precisely 8 bits long.                                   */

   ASN1UINT bitcnt = (value == 0) ? 1 : getUIntBitCount (value);

   /* round-up to nearest 8-bit boundary */

   bitcnt = (bitcnt + 7) & (~7);

   /* encode bits */

   return encodeBits (pctxt, value, bitcnt);
}

static int encodeUnconsLength (OOCTXT* pctxt, ASN1UINT value)
{
   int enclen, stat;

   stat = encodeByteAlign (pctxt);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* 1 octet case */
   if (value < 128) {
      stat = encodeBits (pctxt, value, 8);
      enclen = (stat == ASN_OK) ? value : stat;
   }
   /* 2 octet case */
   else if (value < 16384) {
      if ((stat = encodeBit (pctxt, 1)) == ASN_OK)
         stat = encodeBits (pctxt, value, 15);
      enclen = (stat == ASN_OK) ? value : stat;
   }
   /* fragmentation case */
   else {
      int multiplier = ASN1MIN (value/16384, 4);
      encodeBit (pctxt, 1);  /* set bit 8 of first octet */
      encodeBit (pctxt, 1);  /* set bit 7 of first octet */
      stat = encodeBits (pctxt, multiplier, 6);
      enclen = (stat == ASN_OK) ? 16384 * multiplier : stat;
   }

   return enclen;
}

static int getIdentByteCount (ASN1UINT ident)
{
   if (ident < (1u << 7)) {         /* 7 */
      return 1;
   }
   else if (ident < (1u << 14)) {   /* 14 */
      return 2;
   }
   else if (ident < (1u << 21)) {   /* 21 */
      return 3;
   }
   else if (ident < (1u << 28)) {   /* 28 */
      return 4;
   }
   return 5;
}
