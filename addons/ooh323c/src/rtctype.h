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

/**
 * @file rtctype.h
 */
#ifndef _RTCTYPE_H_
#define _RTCTYPE_H_

#include "ooasn1.h"

/* Ctype module constants */

#define  OS_CTYPE_UPPER  0x1
#define  OS_CTYPE_LOWER  0x2
#define  OS_CTYPE_NUMBER 0x4
#define  OS_CTYPE_SPACE  0x8
#define  OS_CTYPE_PUNCT  0x10
#define  OS_CTYPE_CTRL   0x20
#define  OS_CTYPE_HEX    0x40
#define  OS_CTYPE_BLANK  0x80

/* Ctype substitution macros */

#define  OS_ISALPHA(c) \
(rtCtypeTable[(unsigned)(c)]&(OS_CTYPE_UPPER|OS_CTYPE_LOWER))
#define  OS_ISUPPER(c)  \
(rtCtypeTable[(unsigned)(c)]&OS_CTYPE_UPPER)
#define  OS_ISLOWER(c)  \
(rtCtypeTable[(unsigned)(c)]&OS_CTYPE_LOWER)
#define  OS_ISDIGIT(c)  \
(rtCtypeTable[(unsigned)(c)]&OS_CTYPE_NUMBER)
#define  OS_ISXDIGIT(c) \
(rtCtypeTable[(unsigned)(c)]&(OS_CTYPE_HEX|OS_CTYPE_NUMBER))
#define  OS_ISSPACE(c)  \
(rtCtypeTable[(unsigned)(c)]&OS_CTYPE_SPACE)
#define  OS_ISPUNCT(c)  \
(rtCtypeTable[(unsigned)(c)]&OS_CTYPE_PUNCT)
#define  OS_ISALNUM(c)  \
(rtCtypeTable[(unsigned)(c)]&(OS_CTYPE_UPPER|OS_CTYPE_LOWER|OS_CTYPE_NUMBER))
#define  OS_ISPRINT(c)  \
(rtCtypeTable[(unsigned)(c)]& \
(OS_CTYPE_PUNCT|OS_CTYPE_UPPER|OS_CTYPE_LOWER|OS_CTYPE_NUMBER|OS_CTYPE_BLANK))
#define  OS_ISGRAPH(c)  \
(rtCtypeTable[(unsigned)(c)]& \
(OS_CTYPE_PUNCT|OS_CTYPE_UPPER|OS_CTYPE_LOWER|OS_CTYPE_NUMBER))
#define  OS_ISCNTRL(c)  \
(rtCtypeTable[(unsigned)(c)]&OS_CTYPE_CTRL)

#define  OS_TOLOWER(c) (OS_ISUPPER(c) ? (c) - 'A' + 'a' : (c))
#define  OS_TOUPPER(c) (OS_ISLOWER(c) ? (c) - 'a' + 'A' : (c))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#ifdef MAKE_DLL
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */
/* ctype module table */

extern EXTERN const ASN1OCTET rtCtypeTable[256];

#ifdef __cplusplus
}
#endif

#endif /* _RTCTYPE_H_ */
