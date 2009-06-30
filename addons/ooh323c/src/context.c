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
#include <stdlib.h>

int initContext (OOCTXT* pctxt)
{
   memset (pctxt, 0, sizeof(OOCTXT));

   memHeapCreate (&pctxt->pTypeMemHeap);
   pctxt->pMsgMemHeap = pctxt->pTypeMemHeap;
   memHeapAddRef (&pctxt->pMsgMemHeap);

   return ASN_OK;
}

int initContextBuffer 
(OOCTXT* pctxt, const ASN1OCTET* bufaddr, ASN1UINT bufsiz)
{
   if (bufaddr == 0) {
      /* dynamic buffer */
      if (bufsiz == 0) bufsiz = ASN_K_ENCBUFSIZ;
      pctxt->buffer.data = (ASN1OCTET*) 
         memHeapAlloc (&pctxt->pMsgMemHeap, bufsiz);
      if (!pctxt->buffer.data) return ASN_E_NOMEM;
      pctxt->buffer.size = bufsiz;
      pctxt->buffer.dynamic = TRUE;
   }
   else {
      /* static buffer */
      pctxt->buffer.data = (ASN1OCTET*) bufaddr;
      pctxt->buffer.size = bufsiz;
      pctxt->buffer.dynamic = FALSE;
   }

   pctxt->buffer.byteIndex = 0;
   pctxt->buffer.bitOffset = 8;

   return ASN_OK;
}

int initSubContext (OOCTXT* pctxt, OOCTXT* psrc) 
{
   int stat = ASN_OK;
   memset (pctxt, 0, sizeof(OOCTXT));
   pctxt->pTypeMemHeap = psrc->pTypeMemHeap;
   memHeapAddRef (&pctxt->pTypeMemHeap);
   pctxt->pMsgMemHeap = psrc->pMsgMemHeap;
   memHeapAddRef (&pctxt->pMsgMemHeap);
   pctxt->flags = psrc->flags;
   pctxt->buffer.dynamic = TRUE;
   pctxt->buffer.byteIndex = 0;
   pctxt->buffer.bitOffset = 8;
   return stat;
}

void freeContext (OOCTXT* pctxt)
{
   ASN1BOOL saveBuf = (pctxt->flags & ASN1SAVEBUF) != 0;
   
   if (pctxt->buffer.dynamic && pctxt->buffer.data) {
      if (saveBuf) {
         memHeapMarkSaved (&pctxt->pMsgMemHeap, pctxt->buffer.data, TRUE);
      }
      else {
         memHeapFreePtr (&pctxt->pMsgMemHeap, pctxt->buffer.data);
      }
   }

   errFreeParms (&pctxt->errInfo);

   memHeapRelease (&pctxt->pTypeMemHeap);
   memHeapRelease (&pctxt->pMsgMemHeap);
}

void copyContext (OOCTXT* pdest, OOCTXT* psrc)
{
   memcpy (&pdest->buffer, &psrc->buffer, sizeof(ASN1BUFFER));
   pdest->flags = psrc->flags;
}

void setCtxtFlag (OOCTXT* pctxt, ASN1USINT mask)
{
   pctxt->flags |= mask;
}

void clearCtxtFlag (OOCTXT* pctxt, ASN1USINT mask)
{
   pctxt->flags &= ~mask;
}

int setPERBufferUsingCtxt (OOCTXT* pTarget, OOCTXT* pSource)
{
   int stat = initContextBuffer 
      (pTarget, pSource->buffer.data, pSource->buffer.size);

   if (ASN_OK == stat) {
      pTarget->buffer.byteIndex = pSource->buffer.byteIndex;
      pTarget->buffer.bitOffset = pSource->buffer.bitOffset;
   }

   return stat;
}

int setPERBuffer (OOCTXT* pctxt,
                  ASN1OCTET* bufaddr, ASN1UINT bufsiz, ASN1BOOL aligned)
{
   int stat = initContextBuffer (pctxt, bufaddr, bufsiz);
   if(stat != ASN_OK) return stat;

   
   return ASN_OK;
}

OOCTXT* newContext () 
{
   OOCTXT* pctxt = (OOCTXT*) ASN1CRTMALLOC0 (sizeof(OOCTXT));
   if (pctxt) {
      if (initContext(pctxt) != ASN_OK) {
         ASN1CRTFREE0 (pctxt);
         pctxt = 0;
      }
      pctxt->flags |= ASN1DYNCTXT;
   }
   return (pctxt);
}
