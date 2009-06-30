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

#include "ooasn1.h"

static int decode16BitConstrainedString 
(OOCTXT* pctxt, Asn116BitCharString* pString, Asn116BitCharSet* pCharSet);

static int decodeOctets 
(OOCTXT* pctxt, ASN1OCTET* pbuffer, ASN1UINT bufsiz, ASN1UINT nbits);

static int getComponentLength (OOCTXT* pctxt, ASN1UINT itemBits);

int decodeBits (OOCTXT* pctxt, ASN1UINT* pvalue, ASN1UINT nbits)
{ 
   unsigned char mask;

   if (nbits == 0) {
      *pvalue = 0;
      return ASN_OK;
   }

   /* If the number of bits is less than the current bit offset, mask   */
   /* off the required number of bits and return..                      */

   if (nbits < (unsigned)pctxt->buffer.bitOffset) {
      /* Check if buffer contains number of bits requested */

      if (pctxt->buffer.byteIndex >= pctxt->buffer.size)
         return LOG_ASN1ERR (pctxt, ASN_E_ENDOFBUF);

      pctxt->buffer.bitOffset -= nbits;

      *pvalue = ((pctxt->buffer.data[pctxt->buffer.byteIndex]) >> 
                 pctxt->buffer.bitOffset) & ((1 << nbits) - 1);

      return ASN_OK;
   }

   /* Otherwise, we first need to mask off the remaining bits in the    */
   /* current byte, followed by a loop to extract bits from full bytes, */
   /* followed by logic to mask of remaining bits from the start of     */
   /* of the last byte..                                                */

   else {
      /* Check if buffer contains number of bits requested */

      int nbytes = (((nbits - pctxt->buffer.bitOffset) + 7) / 8);
      
      if ((pctxt->buffer.byteIndex + nbytes) >= pctxt->buffer.size) {
         return LOG_ASN1ERR (pctxt, ASN_E_ENDOFBUF);
      }

      /* first read current byte remaining bits */
      mask = ((1 << pctxt->buffer.bitOffset) - 1);

      *pvalue = (pctxt->buffer.data[pctxt->buffer.byteIndex]) & mask;

      nbits -= pctxt->buffer.bitOffset;
      pctxt->buffer.bitOffset = 8;
      pctxt->buffer.byteIndex++;

      /* second read bytes from next byteIndex */
      while (nbits >= 8) {
         *pvalue = (*pvalue << 8) | 
            (pctxt->buffer.data[pctxt->buffer.byteIndex]);
         pctxt->buffer.byteIndex++;
         nbits -= 8;
      }

      /* third read bits & set bitoffset of the byteIndex */
      if (nbits > 0) {
         pctxt->buffer.bitOffset = 8 - nbits;
         *pvalue = (*pvalue << nbits) | 
            ((pctxt->buffer.data[pctxt->buffer.byteIndex]) >> 
             pctxt->buffer.bitOffset);
      }

      return ASN_OK;
   }
}

int decodeBitString 
(OOCTXT* pctxt, ASN1UINT* numbits_p, ASN1OCTET* buffer, ASN1UINT bufsiz)
{
   ASN1UINT bitcnt;
   int lstat, octidx = 0, stat;
   Asn1SizeCnst* pSizeList = pctxt->pSizeConstraint;
   ASN1BOOL doAlign;

   for (*numbits_p = 0;;) {
      lstat = decodeLength (pctxt, &bitcnt);
      if (lstat < 0) return LOG_ASN1ERR (pctxt, lstat);

      if (bitcnt > 0) {
         *numbits_p += bitcnt;

         stat = bitAndOctetStringAlignmentTest 
            (pSizeList, bitcnt, TRUE, &doAlign);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         if (doAlign) {
            stat = decodeByteAlign (pctxt);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
         }

         stat = decodeOctets (pctxt, &buffer[octidx], bufsiz - octidx, bitcnt);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
      }

      if (lstat == ASN_OK_FRAG) {
         octidx += (bitcnt / 8);
      }
      else break;
   }

   return ASN_OK;
}

int decodeBMPString 
(OOCTXT* pctxt, ASN1BMPString* pvalue, Asn116BitCharSet* permCharSet)
{
   Asn116BitCharSet charSet;
   int stat;

   /* Set character set */

   init16BitCharSet (&charSet, BMP_FIRST, BMP_LAST, BMP_ABITS, BMP_UBITS);

   if (permCharSet) {
      set16BitCharSet (pctxt, &charSet, permCharSet);
   }

   /* Decode constrained string */

   stat = decode16BitConstrainedString (pctxt, pvalue, &charSet);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   return (stat);
}

int decodeByteAlign (OOCTXT* pctxt)
{
   if (pctxt->buffer.bitOffset != 8) {
      pctxt->buffer.byteIndex++;
      pctxt->buffer.bitOffset = 8;
   }
   return ASN_OK;
}

int decodeConstrainedStringEx 
(OOCTXT* pctxt, const char** string, const char* charSet,
 ASN1UINT abits, ASN1UINT ubits, ASN1UINT canSetBits)
{
   int   stat;
   char* tmpstr;

   ASN1UINT i, idx, len, nbits = abits;

   /* note: need to save size constraint for use in alignCharStr     */
   /* because it will be cleared in decodeLength from the context..        */
   Asn1SizeCnst* psize = pctxt->pSizeConstraint;

   /* Decode length */

   stat = decodeLength (pctxt, &len);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* Byte-align */

   if (alignCharStr (pctxt, len, nbits, psize)) {
      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
   }

   /* Decode data */

   tmpstr = (char*) ASN1MALLOC (pctxt, len+1);
   if (0 != tmpstr) {
      if (nbits >= canSetBits && canSetBits > 4) {
         for (i = 0; i < len; i++) {
            if ((stat = decodeBits (pctxt, &idx, nbits)) == ASN_OK) {
               tmpstr[i] = (char) idx;
            }
            else break;
         }
      }
      else if (0 != charSet) {
         ASN1UINT nchars = strlen (charSet);
         for (i = 0; i < len; i++) {
            if ((stat = decodeBits (pctxt, &idx, nbits)) == ASN_OK) {
               if (idx < nchars) {
                  tmpstr[i] = charSet[idx];
               }
               else return LOG_ASN1ERR (pctxt, ASN_E_CONSVIO);
            }
            else break;
         }
      }
      else stat = ASN_E_INVPARAM;

      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      tmpstr[i] = '\0';  /* add null-terminator */
   }
   else
      return LOG_ASN1ERR (pctxt, ASN_E_NOMEM);

   *string = tmpstr;

   return ASN_OK;
}

int decodeConsInteger 
(OOCTXT* pctxt, ASN1INT* pvalue, ASN1INT lower, ASN1INT upper)
{ 
   ASN1UINT range_value = upper - lower;
   ASN1UINT adjusted_value;
   int stat = ASN_OK;

   if (range_value != ASN1UINT_MAX) { range_value += 1; }

   if (lower > upper)
      return ASN_E_RANGERR;
   else if (lower != upper) {
      stat = decodeConsWholeNumber (pctxt, &adjusted_value, range_value);
      if (stat == ASN_OK) {
         *pvalue = adjusted_value + lower;

         if (*pvalue < lower || *pvalue > upper)
            stat = ASN_E_CONSVIO;
      }
   }
   else {
      *pvalue = lower;
   }

   return stat;
}

int decodeConsUInt8 
(OOCTXT* pctxt, ASN1UINT8* pvalue, ASN1UINT lower, ASN1UINT upper)
{ 
   ASN1UINT range_value, value;
   ASN1UINT adjusted_value;
   int stat = ASN_OK;

   /* Check for special case: if lower is 0 and upper is ASN1UINT_MAX,  */
   /* set range to ASN1UINT_MAX; otherwise to upper - lower + 1         */

   range_value = (lower == 0 && upper == ASN1UINT_MAX) ?
      ASN1UINT_MAX : upper - lower + 1;

   if (lower != upper) {
      ASN1UINT range_bitcnt;

      /* If range is <= 255, bit-field case (10.5.7a) */

      if (range_value <= 255) {
         range_bitcnt = getUIntBitCount (range_value - 1);
      }

      /* If range is exactly 256, one-octet case (10.5.7b) */

      else if (range_value == 256) {
         stat = decodeByteAlign (pctxt);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         range_bitcnt = 8;
      }
      stat = decodeBits (pctxt, &adjusted_value, range_bitcnt);
      if (stat == ASN_OK) {
         value = adjusted_value + lower;

         if (value < lower || value > upper)
            stat = ASN_E_CONSVIO;

         *pvalue = (ASN1OCTET)value;
      }
   }
   else *pvalue = (ASN1OCTET)lower;

   return stat;
}

int decodeConsUInt16 
(OOCTXT* pctxt, ASN1USINT* pvalue, ASN1UINT lower, ASN1UINT upper)
{ 
   ASN1UINT range_value, value;
   ASN1UINT adjusted_value;
   int stat = ASN_OK;

   /* Check for special case: if lower is 0 and upper is ASN1UINT_MAX,  */
   /* set range to ASN1UINT_MAX; otherwise to upper - lower + 1         */

   range_value = (lower == 0 && upper == ASN1UINT_MAX) ?
      ASN1UINT_MAX : upper - lower + 1;

   if (lower != upper) {
      stat = decodeConsWholeNumber (pctxt, &adjusted_value, range_value);
      if (stat == ASN_OK) {
         value = adjusted_value + lower;

         /* Verify value is within given range (ED, 1/15/2002) */
         if (value < lower || value > upper)
            stat = ASN_E_CONSVIO;
         *pvalue = (ASN1USINT) value;
      }
   }
   else *pvalue = (ASN1USINT) lower;

   return stat;
}

int decodeConsUnsigned 
(OOCTXT* pctxt, ASN1UINT* pvalue, ASN1UINT lower, ASN1UINT upper)
{ 
   ASN1UINT range_value;
   ASN1UINT adjusted_value;
   int stat = ASN_OK;

   /* Check for special case: if lower is 0 and upper is ASN1UINT_MAX,  */
   /* set range to ASN1UINT_MAX; otherwise to upper - lower + 1         */

   range_value = (lower == 0 && upper == ASN1UINT_MAX) ?
      ASN1UINT_MAX : upper - lower + 1;

   if (lower != upper) {
      stat = decodeConsWholeNumber (pctxt, &adjusted_value, range_value);
      if (stat == ASN_OK) {
         *pvalue = adjusted_value + lower;
         if (*pvalue < lower || *pvalue > upper)
            stat = ASN_E_CONSVIO;
      }
   }
   else *pvalue = lower;

   return stat;
}

int decodeConsWholeNumber 
(OOCTXT* pctxt, ASN1UINT* padjusted_value, ASN1UINT range_value)
{ 
   ASN1UINT nocts, range_bitcnt;
   int stat;

   /* If unaligned, decode non-negative binary integer in the minimum   */
   /* number of bits necessary to represent the range (10.5.6)          */

   if (!TRUE) {
      range_bitcnt = getUIntBitCount (range_value - 1);
   }

   /* If aligned, encoding depended on range value (10.5.7) */

   else {  /* aligned */

      /* If range is <= 255, bit-field case (10.5.7a) */

      if (range_value <= 255) {
         range_bitcnt = getUIntBitCount (range_value - 1);
      }

      /* If range is exactly 256, one-octet case (10.5.7b) */

      else if (range_value == 256) {
         stat = decodeByteAlign (pctxt);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         range_bitcnt = 8;
      }

      /* If range > 256 and <= 64k (65535), two-octet case (10.5.7c) */

      else if (range_value <= 65536) {
         stat = decodeByteAlign (pctxt);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         range_bitcnt = 16;
      }

      /* If range > 64k, indefinite-length case (10.5.7d) */

      else {
         stat = decodeBits (pctxt, &nocts, 2);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         stat = decodeByteAlign (pctxt);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         range_bitcnt = (nocts + 1) * 8;
      }
   }

   return decodeBits (pctxt, padjusted_value, range_bitcnt);
}

int decodeDynBitString (OOCTXT* pctxt, ASN1DynBitStr* pBitStr)
{
   ASN1UINT nocts;
   ASN1OCTET* ptmp;
   int nbits, stat = ASN_OK;

   /* If "fast copy" option is not set (ASN1FATSCOPY) or if constructed,
    * copy the bit string value into a dynamic memory buffer;
    * otherwise, store the pointer to the value in the decode 
    * buffer in the data pointer argument. */
   
   if (pctxt->flags & ASN1FASTCOPY) {
      /* check is it possible to do optimized decoding */

      ASN1OCTET bit;
      ASN1UINT byteIndex = pctxt->buffer.byteIndex;  /* save byte index */
      ASN1USINT bitOffset = pctxt->buffer.bitOffset; /* save bit offset */

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      stat = DECODEBIT (pctxt, &bit); /* read first bit of length determinant */
      if (bit == 1 && stat == ASN_OK) 
         stat = DECODEBIT (pctxt, &bit); /* read second bit */

      pctxt->buffer.byteIndex = byteIndex;  /* restore byte index */
      pctxt->buffer.bitOffset = bitOffset;  /* restore bit offset */

      /* if either first or second bit != 0 - not fragmented */

      if (bit == 0 && stat == ASN_OK) { 
         ASN1UINT bitcnt;
         
         stat = decodeLength (pctxt, &bitcnt);
         if (stat != 0) return LOG_ASN1ERR (pctxt, stat);

         pBitStr->numbits = bitcnt;
         if (bitcnt > 0) {
            pBitStr->data = ASN1BUFPTR (pctxt);

            stat = moveBitCursor (pctxt, bitcnt);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
         }
         else 
            pBitStr->data = 0;
   
         return stat;
      }
   }

   nbits = getComponentLength (pctxt, 1);

   if (nbits < 0) return LOG_ASN1ERR (pctxt, nbits);
   else if (nbits == 0) {
      pBitStr->numbits = 0;
      ptmp = 0;
   }

   nocts = (nbits + 7) / 8;
      
   /* Allocate memory for the target string */

   if (nocts > 0) {
      ptmp = (ASN1OCTET*) ASN1MALLOC (pctxt, nocts);
      if (0 == ptmp) return LOG_ASN1ERR (pctxt, ASN_E_NOMEM);
      
      /* Call static bit string decode function */

      stat = decodeBitString (pctxt, &pBitStr->numbits, ptmp, nocts);
   }
   pBitStr->data = ptmp;

   return stat;
}

int decodeDynOctetString (OOCTXT* pctxt, ASN1DynOctStr* pOctStr)
{
   ASN1OCTET* ptmp;
   int nocts, stat;

   /* If "fast copy" option is not set (ASN1FASTCOPY) or if constructed,
    * copy the octet string value into a dynamic memory buffer;
    * otherwise, store the pointer to the value in the decode 
    * buffer in the data pointer argument. */

   if (pctxt->flags & ASN1FASTCOPY) {
      /* check if it is possible to do optimized decoding */

      ASN1OCTET bit;
      ASN1UINT byteIndex = pctxt->buffer.byteIndex;  /* save byte index */
      ASN1USINT bitOffset = pctxt->buffer.bitOffset; /* save bit offset */

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      stat = DECODEBIT (pctxt, &bit); /* read first bit of length determinant */
      if (bit == 1 && stat == ASN_OK) 
         stat = DECODEBIT (pctxt, &bit); /* read second bit */

      pctxt->buffer.byteIndex = byteIndex;  /* restore byte index */
      pctxt->buffer.bitOffset = bitOffset;  /* restore bit offset */

      /* if either first or second bit != 0 - not fragmented */

      if (bit == 0 && stat == ASN_OK) { 
         ASN1UINT octcnt;
         
         stat = decodeLength (pctxt, &octcnt);
         if (stat != 0) return LOG_ASN1ERR (pctxt, stat);

         pOctStr->numocts = octcnt;
         if (octcnt > 0) {
            pOctStr->data = ASN1BUFPTR (pctxt);

            stat = moveBitCursor (pctxt, octcnt * 8);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
         }
         else 
            pOctStr->data = 0;
         
         return stat;
      }
   }
   
   nocts = getComponentLength (pctxt, 8);

   if (nocts < 0) return LOG_ASN1ERR (pctxt, nocts);
   else if (nocts == 0) {
      pOctStr->numocts = 0;
      ptmp = 0;
   }

   /* Allocate memory for the target string */

   else {
      ptmp = (ASN1OCTET*) ASN1MALLOC (pctxt, nocts);
      if (0 == ptmp) return LOG_ASN1ERR (pctxt, ASN_E_NOMEM);
   }

   /* Call static octet string decode function */

   stat = decodeOctetString (pctxt, &pOctStr->numocts, ptmp, nocts);

   pOctStr->data = ptmp;

   return stat;
}

int decodeLength (OOCTXT* pctxt, ASN1UINT* pvalue)
{
   Asn1SizeCnst* pSize;
   ASN1UINT lower, upper;
   ASN1BOOL bitValue, extbit;
   int      stat;

   /* If size constraint is present and extendable, decode extension    */
   /* bit..                                                             */

   if (isExtendableSize(pctxt->pSizeConstraint)) {
      stat = DECODEBIT (pctxt, &extbit);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
   }
   else extbit = 0;

   /* Now use the value of the extension bit to select the proper       */
   /* size constraint range specification..                             */

   pSize = getSizeConstraint (pctxt, extbit);

   lower = (pSize) ? pSize->lower : 0;
   upper = (pSize) ? pSize->upper : ASN1UINT_MAX;

   /* Reset the size constraint in the context block structure */

   pctxt->pSizeConstraint = 0;

   /* If upper limit is less than 64k, constrained case */

   if (upper < 65536) {
      if (lower == upper) {
         *pvalue = 0;
         stat = ASN_OK;
      }
      else
         stat = decodeConsWholeNumber (pctxt, pvalue, (upper - lower + 1));

      if (stat == ASN_OK) *pvalue += lower;
   }
   else {
      /* unconstrained case OR constrained with upper bound >= 64K*/

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      stat = DECODEBIT (pctxt, &bitValue);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      if (bitValue == 0) {
         stat = decodeBits (pctxt, pvalue, 7);   /* 10.9.3.6 */
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
      }
      else {
         stat = DECODEBIT (pctxt, &bitValue);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         if (bitValue == 0) {
            stat = decodeBits (pctxt, pvalue, 14);  /* 10.9.3.7 */
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
         }
         else {
            ASN1UINT multiplier;

            stat = decodeBits (pctxt, &multiplier, 6);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

            *pvalue = 16384 * multiplier;

            stat = ASN_OK_FRAG;
         }
      }
   }

   return stat;
}

int decodeObjectIdentifier (OOCTXT* pctxt, ASN1OBJID* pvalue)
{
   ASN1UINT  len;
   int       stat, j;
   unsigned  subid;
   ASN1UINT  b;

   /* Decode unconstrained length */

   if ((stat = decodeLength (pctxt, &len)) < 0) {
      return LOG_ASN1ERR (pctxt, stat);
   }

   /* Copy contents to a byte-aligned local buffer */

   j = 0;
   while (len > 0 && stat == ASN_OK) {
      if (j < ASN_K_MAXSUBIDS) {

         /* Parse a subidentifier out of the contents field */

         pvalue->subid[j] = 0;
         do {
            if ((stat = decodeBits (pctxt, &b, 8)) == ASN_OK) {
               pvalue->subid[j] = (pvalue->subid[j] * 128) + (b & 0x7F);
               len--;
            }
         } while (b & 0x80 && stat == ASN_OK);

         /* Handle the first subidentifier special case: the first two   */
         /* sub-id's are encoded into one using the formula (x * 40) + y */

         if (j == 0) {
            subid = pvalue->subid[0];
            pvalue->subid[0] = ((subid / 40) >= 2) ? 2 : subid / 40;
            pvalue->subid[1] = (pvalue->subid[0] == 2) ? 
               subid - 80 : subid % 40;
            j = 2;
         }
         else j++;
      }
      else
         stat = ASN_E_INVOBJID; 
   }

   pvalue->numids = j;
   if (stat == ASN_OK && len != 0) stat = ASN_E_INVLEN;

   return (stat);
}

static int decodeOctets 
(OOCTXT* pctxt, ASN1OCTET* pbuffer, ASN1UINT bufsiz, ASN1UINT nbits)
{ 
   ASN1UINT nbytes = (nbits + 7) / 8 ;
   ASN1UINT i = 0, j;
   ASN1UINT rshift = pctxt->buffer.bitOffset;
   ASN1UINT lshift = 8 - rshift;
   ASN1UINT nbitsInLastOctet;
   ASN1OCTET mask;
   int stat;

   /* Check to make sure buffer contains number of bits requested */

   if ((pctxt->buffer.byteIndex + nbytes) > pctxt->buffer.size) {
      return LOG_ASN1ERR (pctxt, ASN_E_ENDOFBUF);
   }

   /* Check to make sure buffer is big enough to hold requested         */
   /* number of bits..                                                  */

   if (nbytes > bufsiz) {
      return LOG_ASN1ERR (pctxt, ASN_E_STROVFLW);
   }

   /* If on a byte boundary, can do a direct memcpy to target buffer */

   if (pctxt->buffer.bitOffset == 8) {
      memcpy (pbuffer, &pctxt->buffer.data[pctxt->buffer.byteIndex], nbytes);
      stat = moveBitCursor (pctxt, nbits);
      if (stat != ASN_OK) return stat;
      i = nbytes - 1; nbits %= 8;
   }
   else {
      while (nbits >= 8) {

         /* Transfer lower bits from stream octet to upper bits of      */
         /* target octet..                                              */

         pbuffer[i] = pctxt->buffer.data[pctxt->buffer.byteIndex++]
            << lshift;

         /* Transfer upper bits from next stream octet to lower bits    */
         /* target octet..                                              */

         pbuffer[i++] |= pctxt->buffer.data[pctxt->buffer.byteIndex]
            >> rshift;

         nbits -= 8;
      }

      /* Copy last partial byte */

      if (nbits >= rshift) {
         pbuffer[i] = 
            pctxt->buffer.data[pctxt->buffer.byteIndex++] << lshift;

         nbitsInLastOctet = nbits - rshift;

         if (nbitsInLastOctet > 0) {
            pbuffer[i] |= 
               pctxt->buffer.data[pctxt->buffer.byteIndex] >> rshift;
         }

         pctxt->buffer.bitOffset = 8 - nbitsInLastOctet;
      }
      else if (nbits > 0) {  /* nbits < rshift */
         pbuffer[i] = 
            pctxt->buffer.data[pctxt->buffer.byteIndex] << lshift;
         pctxt->buffer.bitOffset = rshift - nbits;
      }
   }

   /* Mask unused bits off of last byte */

   if (nbits > 0) {
      mask = 0;
      for (j = 0; j < nbits; j++) {
         mask >>= 1;
         mask |= 0x80;
      }
      pbuffer[i] &= mask;
   }

   return ASN_OK;
}

int decodeOctetString 
(OOCTXT* pctxt, ASN1UINT* numocts_p, ASN1OCTET* buffer, ASN1UINT bufsiz)
{
   ASN1UINT octcnt;
   int lstat, octidx = 0, stat;
   Asn1SizeCnst* pSizeList = pctxt->pSizeConstraint;

   for (*numocts_p = 0;;) {
      lstat = decodeLength (pctxt, &octcnt);
      if (lstat < 0) return LOG_ASN1ERR (pctxt, lstat);

      if (octcnt > 0) {
         *numocts_p += octcnt;

         if (TRUE) {
            ASN1BOOL doAlign;

            stat = bitAndOctetStringAlignmentTest 
               (pSizeList, octcnt, FALSE, &doAlign);
            if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

            if (doAlign) {
               stat = decodeByteAlign (pctxt);
               if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
            }
         }

         stat = decodeOctets (pctxt, &buffer[octidx], 
                           bufsiz - octidx, (octcnt * 8));

         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
      }

      if (lstat == ASN_OK_FRAG) {
         octidx += octcnt;
      }
      else break;
   }

   return ASN_OK;
}

int decodeOpenType 
(OOCTXT* pctxt, const ASN1OCTET** object_p2, ASN1UINT* numocts_p)
{
   ASN1DynOctStr octStr;
   int stat;

   stat = decodeDynOctetString (pctxt, &octStr);
   if (stat == ASN_OK) {
      *numocts_p = octStr.numocts;
      *object_p2 = octStr.data;
   }

   return stat;
}

int decodeSemiConsInteger (OOCTXT* pctxt, ASN1INT* pvalue, ASN1INT lower)
{
   signed char b;
   unsigned char ub;
   ASN1UINT nbytes;
   int stat;

   stat = decodeLength (pctxt, &nbytes);
   if (stat < 0) return LOG_ASN1ERR (pctxt, stat);

   if (nbytes > 0) {

      /* Align buffer */

      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      /* Decode first byte into a signed byte value and assign to integer. */
      /* This should handle sign extension..                               */

      stat = decodeOctets (pctxt, (ASN1OCTET*)&b, 1, 8);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      *pvalue = b;
      nbytes--;

      /* Decode remaining bytes and add to result */

      while (nbytes > 0) {
         stat = decodeOctets (pctxt, (ASN1OCTET*)&ub, 1, 8);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         *pvalue = (*pvalue * 256) + ub;
         nbytes--;
      }
   }
   else {  /* nbytes == 0 */
      *pvalue = 0;
   }
   if (lower > ASN1INT_MIN)
      *pvalue += lower;

   return ASN_OK;
}

int decodeSemiConsUnsigned (OOCTXT* pctxt, ASN1UINT* pvalue, ASN1UINT lower)
{
   ASN1UINT nbytes;
   int stat;

   stat = decodeLength (pctxt, &nbytes);
   if (stat < 0) return LOG_ASN1ERR (pctxt, stat);

   
   if (nbytes > 0) {
      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

      stat = decodeBits (pctxt, pvalue, nbytes * 8);
   }
   else
      *pvalue = 0;
   *pvalue += lower;

   return stat;
}

int decodeSmallNonNegWholeNumber (OOCTXT* pctxt, ASN1UINT* pvalue)
{ 
   ASN1BOOL bitValue;
   ASN1UINT len;
   int ret;

   if ((ret = DECODEBIT (pctxt, &bitValue)) != ASN_OK)
      return ret;

   if (bitValue == 0) {
      return decodeBits (pctxt, pvalue, 6);   /* 10.6.1 */
   }
   else {
      if ((ret = decodeLength (pctxt, &len)) < 0)
         return ret;

      if ((ret = decodeByteAlign (pctxt)) != ASN_OK)
         return ret;

      return decodeBits (pctxt, pvalue, len*8);
   }
}

int decodeVarWidthCharString (OOCTXT* pctxt, const char** pvalue)
{
   int        stat;
   ASN1OCTET* tmpstr;
   ASN1UINT   len;

   /* note: need to save size constraint for use in alignCharStr     */
   /* because it will be cleared in decodeLength from the context..        */
   Asn1SizeCnst* psize = pctxt->pSizeConstraint;

   /* Decode length */

   stat = decodeLength (pctxt, &len);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* Byte-align */

   if (alignCharStr (pctxt, len, 8, psize)) {
      stat = decodeByteAlign (pctxt);
      if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);
   }

   /* Decode data */

   tmpstr = (ASN1OCTET*) ASN1MALLOC (pctxt, len + 1);
   if (0 != tmpstr) {
      if ((stat = decodeOctets (pctxt, tmpstr, len, len * 8)) != ASN_OK)
         return LOG_ASN1ERR (pctxt, stat);

      tmpstr[len] = '\0';  /* add null-terminator */
   }
   else
      return LOG_ASN1ERR (pctxt, ASN_E_NOMEM);

   *pvalue = (char*)tmpstr;

   return ASN_OK;
}

static int decode16BitConstrainedString 
(OOCTXT* pctxt, Asn116BitCharString* pString, Asn116BitCharSet* pCharSet)
{
   ASN1UINT i, idx, nbits = pCharSet->alignedBits;
   int stat;

   /* Decode length */

   stat = decodeLength (pctxt, &pString->nchars);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* Byte-align */

   stat = decodeByteAlign (pctxt);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   /* Decode data */

   pString->data = (ASN116BITCHAR*)
      ASN1MALLOC (pctxt, pString->nchars*sizeof(ASN116BITCHAR));

   if (pString->data) {
      for (i = 0; i < pString->nchars; i++) {
         stat = decodeBits (pctxt, &idx, nbits);
         if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

         pString->data[i] = (pCharSet->charSet.data == 0) ? 
            idx + pCharSet->firstChar : pCharSet->charSet.data[idx];
      }
   }
   else
      return LOG_ASN1ERR (pctxt, ASN_E_NOMEM);

   return ASN_OK;
}

static int getComponentLength (OOCTXT* pctxt, ASN1UINT itemBits)
{
   OOCTXT lctxt;
   ASN1UINT len, totalLen = 0;
   int stat;

   stat = initSubContext (&lctxt, pctxt);
   if (stat != ASN_OK) return LOG_ASN1ERR (pctxt, stat);

   stat = setPERBufferUsingCtxt (&lctxt, pctxt);
   if (stat != ASN_OK) {
      freeContext (&lctxt);
      return LOG_ASN1ERR (pctxt, stat);
   }
   lctxt.pSizeConstraint = pctxt->pSizeConstraint;

   for (;;) {
      stat = decodeLength (&lctxt, &len);
      if (stat < 0) {
         freeContext (&lctxt);
         return LOG_ASN1ERR (pctxt, stat);
      }

      totalLen += len;

      if (stat == ASN_OK_FRAG) {
         stat = moveBitCursor (&lctxt, len * itemBits);
         if (stat != ASN_OK) {
            freeContext (&lctxt);
            return LOG_ASN1ERR (pctxt, stat);
         }
      }
      else break;
   }

   freeContext (&lctxt);

   return totalLen;
}

int moveBitCursor (OOCTXT* pctxt, int bitOffset)
{
   int currBitOffset =
      (pctxt->buffer.byteIndex * 8) + (8 - pctxt->buffer.bitOffset);

   currBitOffset += bitOffset;

   pctxt->buffer.byteIndex = (currBitOffset / 8);
   pctxt->buffer.bitOffset = 8 - (currBitOffset % 8);

   if (pctxt->buffer.byteIndex > pctxt->buffer.size) {
      return (ASN_E_ENDOFBUF);
   }
      
   return ASN_OK;
}
