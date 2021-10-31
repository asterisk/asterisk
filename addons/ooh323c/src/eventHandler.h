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
 * @file eventHandler.h
 * C event handler structure.  This structure holds event handler function
 * callbacks for use by the generated code.
 */
/**
 * @defgroup EventHandler event handler
 * Event handler structures and callback function definitions.
 * @{
 */
#ifndef _EVENTHANDLER_H_
#define _EVENTHANDLER_H_

#include <stdio.h>
#include "ooasn1.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifndef EXTERN
#if define (MAKE_DLL)
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */


/**
 * This is a function pointer for a callback function which is invoked
 * from within a decode function when an element of a SEQUENCE, SET,
 * SEQUENCE OF, SET OF, or CHOICE construct is parsed.
 *
 * @param name         For SEQUENCE, SET, or CHOICE, this is the name of the
 *                       element as defined in the ASN.1 definition. For
 *                       SEQUENCE OF or SET OF, this is set to the name
 *                       "element".
 * @param index        For SEQUENCE, SET, or CHOICE, this is not used and is
 *                       set to the value
 *                       -1. For SEQUENCE OF or SET OF, this contains the
 *                       zero-based index of the element in the conceptual
 *                       array associated with the construct.
 * @return             - none
 */
typedef void (*StartElement) (const char* name, int index) ;


/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when parsing is complete on an element of a
 * SEQUENCE, SET, SEQUENCE OF, SET OF, or CHOICE construct.
 *
 * @param name         For SEQUENCE, SET, or CHOICE, this is the name of the
 *                       element as defined in the ASN.1 definition. For
 *                       SEQUENCE OF or SET OF, this is set to the name
 *                       "element".
 * @param index        For SEQUENCE, SET, or CHOICE, this is not used and is
 *                       set to the value
 *                       -1. For SEQUENCE OF or SET OF, this contains the
 *                       zero-based index of the element in the conceptual
 *                       array associated with the construct.
 * @return             - none
 */
typedef void (*EndElement) (const char* name, int index) ;


/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of the BOOLEAN ASN.1 type is parsed.
 *
 * @param value        Parsed value.
 * @return             - none
 */
typedef void (*BoolValue) (ASN1BOOL value);

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of the INTERGER ASN.1 type is parsed.
 *
 * @param value        Parsed value.
 * @return             - none
 */
typedef void (*IntValue) (ASN1INT value);

/**
 * This is a function pointer for a callback function which is invoked
 * from within a decode function when a value of the INTEGER ASN.1 type
 * is parsed. In this case, constraints on the integer value forced the
 * use of unsigned integer C type to represent the value.
 *
 * @param value        Parsed value.
 * @return             - none
 */
typedef void (*UIntValue) (ASN1UINT value);

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of the BIT STRING ASN.1 type is
 * parsed.
 *
 * @param numbits      - Number of bits in the parsed value.
 * @param data         - Pointer to a byte array that contains the bit
 *                         string data.
 * @return             - none
 */
typedef void (*BitStrValue) (ASN1UINT numbits, const ASN1OCTET* data);

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of one of the OCTET STRING ASN.1 type
 * is parsed.
 *
 * @param numocts      Number of octets in the parsed value.
 * @param data         Pointer to byte array containing the octet string
 *                       data.
 * @return             - none
 */
typedef void (*OctStrValue) (ASN1UINT numocts, const ASN1OCTET* data) ;

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of one of the 8-bit ASN.1 character
 * string types is parsed.
 *
 * @param value        Null terminated character string value.
 * @return             - none
 */
typedef void (*CharStrValue) (const char* value) ;

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of one of the 16-bit ASN.1 character
 * string types is parsed.
 *
 * This is used for the ASN.1 BmpString type.
 *
 * @param nchars       Number of characters in the parsed value.
 * @param data         Pointer to an array containing 16-bit values.
 *                       These are represented using unsigned short integer
 *                       values.
 * @return             - none
 */
typedef void (*CharStrValue16Bit) (ASN1UINT nchars, ASN116BITCHAR* data) ;

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of the NULL ASN.1 type is parsed.
 *
 * @param             - none
 * @return             - none
 */
typedef void (*NullValue) (void) ;

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function whn a value the OBJECT IDENTIFIER ASN.1 type is
 * parsed.
 *
 * @param numSubIds    Number of subidentifiers in the object identifier.
 * @param pSubIds      Pointer to array containing the subidentifier values.
 * @return             -none
 */
typedef void (*OidValue) (ASN1UINT numSubIds, ASN1UINT* pSubIds) ;

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when a value of the ENUMERATED ASN.1 type is
 * parsed.
 *
 * @param value        - Parsed enumerated value
 * @return             - none
 */
typedef void (*EnumValue) (ASN1UINT value) ;

/**
 * This is a function pointer for a callback function which is invoked from
 * within a decode function when an ASN.1 open type is parsed.
 *
 * @param numocts      Number of octets in the parsed value.
 * @param data         Pointer to byte array contain in tencoded ASN.1
 *                       value.
 * @return             - none
 */
typedef void (*OpenTypeValue) (ASN1UINT numocts, const ASN1OCTET* data) ;


/**
 * This is a basic C based event handler structure, which can be used
 * to define user-defined event handlers.
 */
typedef struct EventHandler {
   StartElement      startElement;
   EndElement        endElement;
   BoolValue         boolValue;
   IntValue          intValue;
   UIntValue         uIntValue;
   BitStrValue       bitStrValue;
   OctStrValue       octStrValue;
   CharStrValue      charStrValue;
   CharStrValue16Bit charStr16BitValue;
   NullValue         nullValue;
   OidValue          oidValue;
   EnumValue         enumValue;
   OpenTypeValue     openTypeValue;
} EventHandler;


/**
 * This function sets the event handler object within the context.  It
 * will overwrite the definition of any handler that was set previously.
 *
 * @param pctxt       Context to which event handler has to be added.
 * @param pHandler    Pointer to the event handler structure.
 * @return            none
 */
EXTERN void setEventHandler (OOCTXT* pctxt, EventHandler* pHandler);

/**
 * This function is called to remove the event handler current defined
 * in the context.  This is done by setting the event handler object
 * pointer to NULL.
 *
 * @param pctxt       Context from which event handler has to be removed.
 * @return            none
 */
EXTERN void removeEventHandler (OOCTXT* pctxt);

/**
 * The following functions are invoked from within the generated
 * code to call the various user-defined event handler methods ..
 */
EXTERN void invokeStartElement (OOCTXT* pctxt, const char* name, int index);
EXTERN void invokeEndElement (OOCTXT* pctxt, const char* name, int index);
EXTERN void invokeBoolValue (OOCTXT* pctxt, ASN1BOOL value);
EXTERN void invokeIntValue (OOCTXT* pctxt, ASN1INT value);
EXTERN void invokeUIntValue (OOCTXT* pctxt, ASN1UINT value);

EXTERN void invokeBitStrValue
(OOCTXT* pctxt, ASN1UINT numbits, const ASN1OCTET* data);

EXTERN void invokeOctStrValue
(OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data);

EXTERN void invokeCharStrValue (OOCTXT* pctxt, const char* value);

EXTERN void invokeCharStr16BitValue
(OOCTXT* pctxt, ASN1UINT nchars, ASN116BITCHAR* data);

EXTERN void invokeNullValue (OOCTXT* pctxt);

EXTERN void invokeOidValue
(OOCTXT* pctxt, ASN1UINT numSubIds, ASN1UINT* pSubIds);

EXTERN void invokeEnumValue (OOCTXT* pctxt, ASN1UINT value);

EXTERN void invokeOpenTypeValue
(OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
