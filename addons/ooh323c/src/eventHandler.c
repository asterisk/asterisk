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

#include "eventHandler.h"

void setEventHandler (OOCTXT* pctxt, EventHandler* pHandler)
{
   pctxt->pEventHandler = pHandler;
}

void removeEventHandler (OOCTXT* pctxt)
{
   pctxt->pEventHandler = 0;
}

void invokeStartElement (OOCTXT* pctxt, const char* name, int index)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->startElement (name, index);
   }
}

void invokeEndElement (OOCTXT* pctxt, const char* name, int index)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->endElement (name, index);
   }
}

void invokeBoolValue (OOCTXT* pctxt, ASN1BOOL value)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->boolValue (value);
   }
}

void invokeIntValue (OOCTXT* pctxt, ASN1INT value)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->intValue (value);
   }
}

void invokeUIntValue (OOCTXT* pctxt, ASN1UINT value)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->uIntValue (value);
   }
}

void invokeBitStrValue (OOCTXT* pctxt, ASN1UINT numbits,
                        const ASN1OCTET* data)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->bitStrValue (numbits, data);
   }
}

void invokeOctStrValue (OOCTXT* pctxt, ASN1UINT numocts,
                        const ASN1OCTET* data)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->octStrValue (numocts, data);
   }
}

void invokeCharStrValue (OOCTXT* pctxt, const char* value)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->charStrValue (value);
   }
}

void invokeCharStr16BitValue (OOCTXT* pctxt, ASN1UINT nchars,
                              ASN116BITCHAR* data)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->charStr16BitValue (nchars, data);
   }
}

void invokeNullValue (OOCTXT* pctxt)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->nullValue ();
   }
}

void invokeOidValue (OOCTXT* pctxt, ASN1UINT numSubIds, ASN1UINT* pSubIds)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->oidValue (numSubIds, pSubIds);
   }
}

void invokeEnumValue (OOCTXT* pctxt, ASN1UINT value)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->enumValue (value);
   }
}

void invokeOpenTypeValue (OOCTXT* pctxt, ASN1UINT numocts,
                          const ASN1OCTET* data)
{
   if (0 != pctxt->pEventHandler) {
      pctxt->pEventHandler->openTypeValue (numocts, data);
   }
}
