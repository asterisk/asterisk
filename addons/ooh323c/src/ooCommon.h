/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be
 * used and copied only in accordance with the terms of this license.
 * The text of the license may generally be found in the root
 * directory of this installation in the LICENSE.txt file.  It
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must
 * maintain this copyright notice.
 *
 *****************************************************************************/
/**
 * @file ooCommon.h
 * Common runtime constant and type definitions.
 */
#ifndef _OOCOMMON_H_
#define _OOCOMMON_H_

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32_WCE
#include <winsock.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <sys/types.h>
#define INCL_WINSOCK_API_TYPEDEFS   1
#define INCL_WINSOCK_API_PROTOTYPES 0
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif
#include <semaphore.h>

/**
 * @ingroup cruntime C Runtime Common Constant and Type Definitions.
 * @{
 */

/* Basic type definitions */

typedef char            OOCHAR;
typedef unsigned char   OOUCHAR;
typedef signed char     OOINT8;
typedef unsigned char   OOUINT8;
typedef short           OOINT16;
typedef unsigned short  OOUINT16;
typedef int             OOINT32;
typedef unsigned int    OOUINT32;
typedef OOUINT8         OOBOOL;

#define OOUINT32_MAX    4294967295U
#define OOINT32_MAX     ((OOINT32)2147483647L)
#define OOINT32_MIN     ((OOINT32)(-OOINT32_MAX-1))

#ifndef FALSE
#define FALSE           0
#define TRUE            1
#endif

/* Common error codes */

#define OOERRINVPARAM   (-50)   /* Invalid parameter    */
#define OOERRBUFOVFLW   (-51)   /* Buffer overflow      */
#define OOERRNOMEM      (-52)   /* No dynamic memory available */

/* Message buffer: this is used for asynchronous transfers */

typedef struct _OOMsgBuf {
   OOUINT8* pdata;      /* Pointer to binary or text data               */
   OOUINT32 bufsiz;     /* Size of the buffer in bytes                  */
   OOUINT32 length;     /* # bytes to send (write) or # received (read) */
   OOUINT32 offset;     /* Offset into buffer of first byte to send     */
   OOBOOL   dynamic;    /* pdata is dynamic (allocated with OOMEMALLOC) */
} OOMsgBuf;

/* Memory allocation and free function definitions.  These definitions  */
/* can be changed if a non-standard allocation/free function is to be   */
/* used..                                                               */

#define OOMEMALLOC  malloc
#define OOMEMFREE   free

/* Min/max macros */

#ifndef OOMAX
#define OOMAX(a,b)  (((a)>(b))?(a):(b))
#endif

#ifndef OOMIN
#define OOMIN(a,b)  (((a)<(b))?(a):(b))
#endif

/* Get count of number of items in an array */

#define OONUMBEROF(items) (sizeof(items)/sizeof(items[0]))

/* This is used for creating a Windows DLL.  Specify -DMAKE_DLL to      */
/* compile code for inclusion in a DLL.                                 */

#ifndef EXTERN
#if defined (MAKE_DLL)
#define EXTERN __declspec(dllexport)
#elif defined (USE_DLL)
#define EXTERN __declspec(dllimport)
#else
#define EXTERN
#endif /* _DLL */
#endif /* EXTERN */

/**
 * @}
 */
#endif /* _OOCOMMON_H_ */
