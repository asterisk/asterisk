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
 * @file ooasn1.h
 * Common ASN.1 runtime constants, data structure definitions, and run-time
 * functions to support ASN.1 PER encoding/decoding as defined in the
 * ITU-T standards.
 */
#ifndef _OOASN1_H_
#define _OOASN1_H_

#include "asterisk.h"
#include "asterisk/lock.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "dlist.h"
#include "ootrace.h"
/**
 * @defgroup cruntime C Runtime Common Functions
 * @{
 */

/* Error Code Constants */

#define ASN_OK            0      /* normal completion status             */
#define ASN_OK_FRAG       2      /* message fragment detected            */
#define ASN_E_BUFOVFLW   -1      /* encode buffer overflow               */
#define ASN_E_ENDOFBUF   -2      /* unexpected end of buffer on decode   */
#define ASN_E_IDNOTFOU   -3      /* identifer not found                  */
#define ASN_E_INVOBJID   -4      /* invalid object identifier            */
#define ASN_E_INVLEN     -5      /* invalid field length                 */
#define ASN_E_INVENUM    -6      /* enumerated value not in defined set  */
#define ASN_E_SETDUPL    -7      /* duplicate element in set             */
#define ASN_E_SETMISRQ   -8      /* missing required element in set      */
#define ASN_E_NOTINSET   -9      /* element not part of set              */
#define ASN_E_SEQOVFLW   -10     /* sequence of field overflow           */
#define ASN_E_INVOPT     -11     /* invalid option encountered in choice */
#define ASN_E_NOMEM      -12     /* no dynamic memory available          */
#define ASN_E_INVHEXS    -14     /* invalid hex string                   */
#define ASN_E_INVBINS    -15     /* invalid binary string                */
#define ASN_E_INVREAL    -16     /* invalid real value                   */
#define ASN_E_STROVFLW   -17     /* octet or bit string field overflow   */
#define ASN_E_BADVALUE   -18     /* invalid value specification          */
#define ASN_E_UNDEFVAL   -19     /* no def found for ref'd defined value */
#define ASN_E_UNDEFTYP   -20     /* no def found for ref'd defined type  */
#define ASN_E_BADTAG     -21     /* invalid tag value                    */
#define ASN_E_TOODEEP    -22     /* nesting level is too deep            */
#define ASN_E_CONSVIO    -23     /* value constraint violation           */
#define ASN_E_RANGERR    -24     /* invalid range (lower > upper)        */
#define ASN_E_ENDOFFILE  -25     /* end of file on file decode           */
#define ASN_E_INVUTF8    -26     /* invalid UTF-8 encoding               */
#define ASN_E_CONCMODF   -27     /* Concurrent list modification         */
#define ASN_E_ILLSTATE   -28     /* Illegal state error                  */
#define ASN_E_OUTOFBND   -29     /* out of bounds (of array, etc)        */
#define ASN_E_INVPARAM   -30     /* invalid parameter                    */
#define ASN_E_INVFORMAT  -31     /* invalid time string format           */
#define ASN_E_NOTINIT    -32     /* not initialized                      */
#define ASN_E_TOOBIG     -33     /* value is too big for given data type */
#define ASN_E_INVCHAR    -34     /* invalid character (not in char set)  */
#define ASN_E_XMLSTATE   -35     /* XML state error                      */
#define ASN_E_XMLPARSE   -36     /* XML parse error                      */
#define ASN_E_SEQORDER   -37     /* SEQUENCE elements not in order       */
#define ASN_E_INVINDEX   -38     /* invalid index for TC id              */
#define ASN_E_INVTCVAL   -39     /* invalid value for TC field           */
#define ASN_E_FILNOTFOU  -40     /* file not found                       */
#define ASN_E_FILEREAD   -41     /* error occurred reading file          */
#define ASN_E_FILEWRITE  -42     /* error occurred writing file          */
#define ASN_E_INVBASE64  -43     /* invalid base64 encoding              */
#define ASN_E_INVSOCKET  -44     /* invalid socket operation             */
#define ASN_E_XMLLIBNFOU -45     /* XML library is not found             */
#define ASN_E_XMLLIBINV  -46     /* XML library is invalid               */
#define ASN_E_NOTSUPP    -99     /* non-supported ASN construct          */
#define ASN_K_INDEFLEN   -9999   /* indefinite length message indicator  */

/* universal built-in type ID code value constants */

#define ASN_ID_EOC      0       /* end of contents              */
#define ASN_ID_BOOL     1       /* boolean                      */
#define ASN_ID_INT      2       /* integer                      */
#define ASN_ID_BITSTR   3       /* bit string                   */
#define ASN_ID_OCTSTR   4       /* byte (octet) string          */
#define ASN_ID_NULL     5       /* null                         */
#define ASN_ID_OBJID    6       /* object ID                    */
#define ASN_ID_OBJDSC   7       /* object descriptor            */
#define ASN_ID_EXTERN   8       /* external type                */
#define ASN_ID_REAL     9       /* real                         */
#define ASN_ID_ENUM     10      /* enumerated value             */
#define ASN_ID_EPDV     11      /* EmbeddedPDV type             */
#define ASN_ID_RELOID   13      /* relative object ID           */
#define ASN_ID_SEQ      16      /* sequence, sequence of        */
#define ASN_ID_SET      17      /* set, set of                  */

#define ASN_SEQ_TAG     0x30    /* SEQUENCE universal tag byte  */
#define ASN_SET_TAG     0x31    /* SET universal tag byte       */

/* Restricted character string type ID's */

#define ASN_ID_NumericString    18
#define ASN_ID_PrintableString  19
#define ASN_ID_TeletexString    20
#define ASN_ID_T61String        ASN_ID_TeletexString
#define ASN_ID_VideotexString   21
#define ASN_ID_IA5String        22
#define ASN_ID_UTCTime          23
#define ASN_ID_GeneralTime      24
#define ASN_ID_GraphicString    25
#define ASN_ID_VisibleString    26
#define ASN_ID_GeneralString    27
#define ASN_ID_UniversalString  28
#define ASN_ID_BMPString        30

/* flag mask values */

#define XM_SEEK         0x01    /* seek match until found or end-of-buf */
#define XM_ADVANCE      0x02    /* advance pointer to contents on match */
#define XM_DYNAMIC      0x04    /* alloc dyn mem for decoded variable   */
#define XM_SKIP         0x08    /* skip to next field after parsing tag */

/* Sizing Constants */

#define ASN_K_MAXDEPTH  32      /* maximum nesting depth for messages   */
#define ASN_K_MAXSUBIDS 128     /* maximum sub-id's in an object ID     */
#define ASN_K_MAXENUM   100     /* maximum enum values in an enum type  */
#define ASN_K_MAXERRP   5       /* maximum error parameters             */
#define ASN_K_MAXERRSTK 8       /* maximum levels on error ctxt stack   */
#define ASN_K_ENCBUFSIZ 2*1024 /* dynamic encode buffer extent size    */
#define ASN_K_MEMBUFSEG 1024    /* memory buffer extent size            */

/* Canonical character set definitions */

#define NUM_ABITS  4
#define NUM_UBITS  4
#define NUM_CANSET \
" 0123456789"

#define PRN_ABITS  8
#define PRN_UBITS  7
#define PRN_CANSET \
" '()+,-./0123456789:=?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

#define VIS_ABITS  8
#define VIS_UBITS  7
#define VIS_CANSET \
" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]"\
"^_`abcdefghijklmnopqrstuvwxyz{|}~"

#define T61_ABITS  8
#define T61_UBITS  7
#define T61_CANSET \
" !\"%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]"\
"_abcdefghijklmnopqrstuvwxyz"

#define IA5_ABITS  8
#define IA5_UBITS  7
#define IA5_CANSET \
"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017"\
"\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"\
" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]"\
"^_`abcdefghijklmnopqrstuvwxyz{|}~"

#define IA5_RANGE1_LOWER 0
#define IA5_RANGE2_LOWER 0x5f

#define GEN_ABITS  8
#define GEN_UBITS  7
#define GEN_CANSET \
"\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017"\
"\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"\
" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"\
"`abcdefghijklmnopqrstuvwxyz{|}~\177\200\201\202\203\204\205\206\207"\
"\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237"\
"\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257"\
"\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277"\
"\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317"\
"\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337"\
"\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357"\
"\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377"

#define BMP_ABITS  16
#define BMP_UBITS  16
#define BMP_FIRST  0
#define BMP_LAST   0xffff

#define UCS_ABITS  32
#define UCS_UBITS  32
#define UCS_FIRST  0
#define UCS_LAST   0xfffffffful

/* ASN.1 Primitive Type Definitions */

typedef char            ASN1CHAR;
typedef unsigned char   ASN1OCTET;
typedef ASN1OCTET       ASN1BOOL;
typedef signed char     ASN1INT8;
typedef unsigned char   ASN1UINT8;
typedef int             ASN1INT;
typedef unsigned int    ASN1UINT;
typedef ASN1INT         ASN1ENUM;
typedef double          ASN1REAL;

typedef short           ASN1SINT;
typedef unsigned short  ASN1USINT;
typedef ASN1UINT        ASN1TAG;
#define ASN1TAG_LSHIFT  24
typedef ASN1USINT       ASN116BITCHAR;
typedef ASN1UINT        ASN132BITCHAR;
typedef void*           ASN1ANY;

#define ASN1UINT_MAX    4294967295U
#define ASN1INT_MAX     ((ASN1INT)2147483647L)
#define ASN1INT_MIN     ((ASN1INT)(-ASN1INT_MAX-1))

#define ASN1UINTCNT(val) val##u


#ifndef ASN1INT64

#if defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__) || \
defined(_WIN32)
#define ASN1INT64 __int64

#elif defined(__IBMC__) || defined(__GNUC__) || defined(__SUNPRO_C) || \
defined(__SUNPRO_CC) || defined(__CC_ARM) || \
defined(__HPUX_CC__) || defined(__HP_aCC)
#define ASN1INT64 long long

#else  /* !MSC_VER && !__IBMC__ etc */
#define ASN1INT64 long

#endif
#endif /* ASN1INT64 */

#ifndef FALSE
#define FALSE           0
#define TRUE            1
#endif

typedef struct {        /* object identifier */
   ASN1UINT     numids;
   ASN1UINT     subid[ASN_K_MAXSUBIDS];
} ASN1OBJID;

typedef struct {        /* generic octet string structure */
   ASN1UINT     numocts;
   ASN1OCTET    data[1];
} ASN1OctStr;

typedef struct {        /* generic octet string structure (dynamic) */
   ASN1UINT     numocts;
   const ASN1OCTET* data;
} ASN1DynOctStr;

typedef struct {        /* generic bit string structure (dynamic) */
   ASN1UINT     numbits;
   const ASN1OCTET* data;
} ASN1DynBitStr;

typedef struct {                /* generic sequence of structure        */
   ASN1UINT     n;
   void*        elem;
} ASN1SeqOf;

typedef struct {                /* sequence of OCTET STRING             */
   ASN1UINT     n;
   ASN1DynOctStr* elem;
} ASN1SeqOfOctStr;

typedef struct {                /* generic open type data structure     */
   ASN1UINT     numocts;
   const ASN1OCTET* data;
} ASN1OpenType;

/* ASN.1 useful type definitions */

typedef struct {
   ASN1UINT       nchars;
   ASN116BITCHAR* data;
} Asn116BitCharString;

typedef struct {
   ASN1UINT       nchars;
   ASN132BITCHAR* data;
} Asn132BitCharString;

typedef const char*   ASN1GeneralizedTime;
typedef const char*   ASN1GeneralString;
typedef const char*   ASN1GraphicString;
typedef const char*   ASN1IA5String;
typedef const char*   ASN1ISO646String;
typedef const char*   ASN1NumericString;
typedef const char*   ASN1ObjectDescriptor;
typedef const char*   ASN1PrintableString;
typedef const char*   ASN1TeletexString;
typedef const char*   ASN1T61String;
typedef const char*   ASN1UTCTime;
typedef const char*   ASN1UTF8String;
typedef const char*   ASN1VideotexString;
typedef const char*   ASN1VisibleString;

typedef Asn116BitCharString ASN1BMPString;
typedef Asn132BitCharString ASN1UniversalString;

/* ASN.1 constrained string structures */

typedef struct {
   int          nchars;
   char         data[255];
} Asn1CharArray;

typedef struct {
   Asn1CharArray charSet;
   const char* canonicalSet;
   int          canonicalSetSize;
   unsigned     canonicalSetBits;
   unsigned     charSetUnalignedBits;
   unsigned     charSetAlignedBits;
} Asn1CharSet;

typedef struct {
   Asn116BitCharString charSet;
   ASN1USINT    firstChar, lastChar;
   unsigned     unalignedBits;
   unsigned     alignedBits;
} Asn116BitCharSet;

/* ASN.1 size constraint structure */

typedef struct _Asn1SizeCnst {
   ASN1BOOL     extended;
   ASN1UINT     lower;
   ASN1UINT     upper;
   struct _Asn1SizeCnst* next;
} Asn1SizeCnst;

/* ASN.1 encode/decode buffer info structure */

typedef struct {
   ASN1OCTET*   data;           /* pointer to start of data buffer      */
   ASN1UINT     byteIndex;      /* byte index                           */
   ASN1UINT     size;           /* current buffer size                  */
   ASN1SINT     bitOffset;      /* current bit offset (8 - 1)           */
   ASN1BOOL     dynamic;        /* is buffer dynamic?                   */
} ASN1BUFFER;

/* This structure is used to save the current state of the buffer */

typedef struct {
   ASN1UINT     byteIndex;      /* byte index                           */
   ASN1SINT     bitOffset;      /* current bit offset (8 - 1)           */
   ASN1USINT    flags;          /* flag bits                            */
} ASN1BUFSAVE;

/* ASN.1 run-time error info structures */

typedef struct {
   const char* module;
   int          lineno;
} ASN1ErrLocn;

typedef struct {
   ASN1ErrLocn  stack[ASN_K_MAXERRSTK];
   int          stkx;
   int          status;
   int          parmcnt;
   const char* parms[ASN_K_MAXERRP];
} ASN1ErrInfo;

#define XM_K_MEMBLKSIZ  (4*1024)

/* Flag mask constant values */

#define ASN1DYNCTXT     0x8000
#define ASN1INDEFLEN    0x4000
#define ASN1TRACE       0x2000
#define ASN1LASTEOC     0x1000
#define ASN1FASTCOPY    0x0800  /* turns on the "fast copy" mode        */
#define ASN1CONSTAG     0x0400  /* form of last parsed tag              */
#define ASN1CANXER      0x0200  /* canonical XER                        */
#define ASN1SAVEBUF     0x0100  /* do not free dynamic encode buffer    */
#define ASN1OPENTYPE    0x0080  /* item is an open type field           */

/* ASN.1 encode/decode context block structure */

struct EventHandler;

typedef struct OOCTXT {         /* context block                        */
   void*        pMsgMemHeap;    /* internal message memory heap         */
   void*        pTypeMemHeap;   /* memory heap                          */
   ASN1BUFFER   buffer;         /* data buffer                          */
   ASN1ErrInfo  errInfo;        /* run-time error info                  */
   Asn1SizeCnst* pSizeConstraint;  /* Size constraint list              */
   const char* pCharSet;        /* String of permitted characters       */
   struct EventHandler* pEventHandler; /* event handler object          */
   ASN1USINT    flags;          /* flag bits                            */
   ASN1OCTET    spare[2];
   ast_mutex_t pLock;
} OOCTXT;

/* macros and function prototypes */

#ifndef ASN1MAX
#define ASN1MAX(a,b)        (((a)>(b))?(a):(b))
#endif

#ifndef ASN1MIN
#define ASN1MIN(a,b)        (((a)<(b))?(a):(b))
#endif

/**
 * @defgroup mem Memory Allocation Macros and Functions
 * @ingroup cruntime
 *
 * Memory allocation functions and macros handle memory management for the
 * ASN1C run-time. Special algorithms are used for allocation and deallocation
 * of memory to improve the run-time performance. @{
 */
/**
 * Allocate a dynamic array. This macro allocates a dynamic array of records of
 * the given type. This version of the macro will return the ASN_E_NOMEM error
 * status if the memory request cannot be fulfilled.
 *
 * @param pctxt        - Pointer to a context block
 * @param pseqof       - Pointer to a generated SEQUENCE OF array structure.
 *                       The <i>n</i> member variable must be set to the number
 *                       of records to allocate.
 * @param type         - Data type of an array record
 */
#define ALLOC_ASN1ARRAY(pctxt,pseqof,type) do {\
if (sizeof(type)*(pseqof)->n < (pseqof)->n) return ASN_E_NOMEM; \
if (((pseqof)->elem = (type*) memHeapAlloc \
(&(pctxt)->pTypeMemHeap, sizeof(type)*(pseqof)->n)) == 0) return ASN_E_NOMEM; \
} while (0)

/**
 * Allocate and zero an ASN.1 element. This macro allocates and zeros a single
 * element of the given type.
 *
 * @param pctxt        - Pointer to a context block
 * @param type         - Data type of record to allocate
 */
#define ALLOC_ASN1ELEM(pctxt,type) \
(type*) memHeapAllocZ (&(pctxt)->pTypeMemHeap, sizeof(type))

/**
 * Allocate memory. This macro allocates the given number of bytes. It is
 * similar to the C \c malloc run-time function.
 *
 * @param pctxt        - Pointer to a context block
 * @param nbytes       - Number of bytes of memory to allocate
 * @return             - Void pointer to allocated memory or NULL if
 *                       insufficient memory was available to fulfill the
 *                       request.
 */
#define ASN1MALLOC(pctxt,nbytes) \
memHeapAlloc(&(pctxt)->pTypeMemHeap, nbytes)

/**
 * Free memory associated with a context. This macro frees all memory held
 * within a context. This is all memory allocated using the ASN1MALLOC (and
 * similar macros) and the mem memory allocation functions using the given
 * context variable.
 *
 * @param pctxt        - Pointer to a context block
 */
#define ASN1MEMFREE(pctxt) \
memHeapFreeAll(&(pctxt)->pTypeMemHeap)

/**
 * Free memory pointer. This macro frees memory at the given pointer. The
 * memory must have been allocated using the ASN1MALLOC (or similar) macros or
 * the mem memory allocation functions. This macro is similar to the C \c
 * free function.
 *
 * @param pctxt        - Pointer to a context block
 * @param pmem         - Pointer to memory block to free. This must have been
 *                       allocated using the ASN1MALLOC macro or the
 *                       memHeapAlloc function.
 */
#define ASN1MEMFREEPTR(pctxt,pmem)  \
memHeapFreePtr(&(pctxt)->pTypeMemHeap, (void*)pmem)

/**
 * @}
 */
#define ASN1BUFCUR(cp)          (cp)->buffer.data[(cp)->buffer.byteIndex]
#define ASN1BUFPTR(cp)          &(cp)->buffer.data[(cp)->buffer.byteIndex]

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#ifdef MAKE_DLL
#define EXTERN __declspec(dllexport)
#elif defined (USEASN1DLL)
#define EXTERN __declspec(dllimport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */

#ifndef _NO_MALLOC
/*#define ASN1CRTMALLOC0(nbytes)       malloc(nbytes)
#define ASN1CRTFREE0(ptr)            free(ptr) */
#else

#ifdef _NO_THREADS
extern EXTERN OOCTXT g_ctxt;

#define ASN1CRTMALLOC0(nbytes)       memAlloc(&g_ctxt,(nbytes))
#define ASN1CRTFREE0(ptr)            memFreePtr(&g_ctxt,(ptr))
#else
#define ASN1CRTMALLOC0(nbytes)       (void*)0
#define ASN1CRTFREE0(ptr)            (void*)0

#endif /* _NO_THREADS */
#endif /* _NO_MALLOC */

#define ASN1CRTMALLOC memHeapAlloc
#define ASN1CRTFREE   ASN1MEMFREEPTR

/* Function prototypes */

#define DE_INCRBITIDX(pctxt) \
((--(pctxt)->buffer.bitOffset < 0) ? \
((++(pctxt)->buffer.byteIndex >= (pctxt)->buffer.size) ? ASN_E_ENDOFBUF : \
((pctxt)->buffer.bitOffset = 7, ASN_OK)) : ASN_OK)


#define DE_BIT(pctxt,pvalue) \
((DE_INCRBITIDX (pctxt) != ASN_OK) ? ASN_E_ENDOFBUF : ( \
((*(pvalue) = (((pctxt)->buffer.data[(pctxt)->buffer.byteIndex]) & \
(1 << (pctxt)->buffer.bitOffset)) != 0), ASN_OK) ))
/*
#define DE_BIT(pctxt,pvalue) \
((DE_INCRBITIDX (pctxt) != ASN_OK) ? ASN_E_ENDOFBUF : ((pvalue) ? \
((*(pvalue) = (((pctxt)->buffer.data[(pctxt)->buffer.byteIndex]) & \
(1 << (pctxt)->buffer.bitOffset)) != 0), ASN_OK) : ASN_OK ))
*/


#define encodeIA5String(pctxt,value,permCharSet) \
encodeConstrainedStringEx (pctxt, value, permCharSet, 8, 7, 7)

#define encodeGeneralizedTime   encodeIA5String

#define decodeIA5String(pctxt,pvalue,permCharSet) \
decodeConstrainedStringEx (pctxt, pvalue, permCharSet, 8, 7, 7)

#define decodeGeneralizedTime   decodeIA5String

/* run-time error and diagnostic functions */

/* Context management functions */

/**
 * @defgroup cmfun Context Management Functions
 * @{
 *
 * Context initialization functions handle the allocation, initialization, and
 * destruction of ASN.1 context variables (variables of type OOCTXT). These
 * variables hold all of the working data used during the process of encoding
 * or decoding a message. The context provides thread safe operation by
 * isolating what would otherwise be global variables within this structure.
 * The context variable is passed from function to function as a message is
 * encoded or decoded and maintains state information on the encoding or
 * decoding process.
 */

/**
 * This function assigns a buffer to a context block. The block should have
 * been previously initialized by initContext.
 *
 * @param pctxt        The pointer to the context structure variable to be
 *                     initialized.
 * @param bufaddr      For encoding, the address of a memory buffer to receive
 *                     and encode a message. For decoding the address of a
 *                     buffer that contains the message data to be decoded.
 *                     This address will be stored within the context
 *                     structure. For encoding it might be zero, the dynamic
 *                     buffer will be used in this case.
 * @param bufsiz       The size of the memory buffer. For encoding, it might be
 *                     zero; the dynamic buffer will be used in this case.
 * @return             Completion status of operation:
 *                     - 0 (ASN_OK) = success,
 *                     - negative return value is error.
 */
EXTERN int initContextBuffer
(OOCTXT* pctxt, const ASN1OCTET* bufaddr, ASN1UINT bufsiz);

/**
 * This function initializes a context block. It makes sure that if the block
 * was not previosly initialized, that all key working parameters are set to
 * their correct initial state values (i.e. declared within a function as a
 * normal working variable), it is required that they invoke this function
 * before using it.
 *
 * @param pctxt        The pointer to the context structure variable to be
 *                     initialized.
 * @return             Completion status of operation:
 *                     - 0 (ASN_OK) = success,
 *                     - negative return value is error.
 */
EXTERN int initContext (OOCTXT* pctxt);

/**
 * This function frees all dynamic memory associated with a context. This
 * includes all memory inside the block (in particular, the list of memory
 * blocks used by the mem functions).
 *
 * @param pctxt       A pointer to a context structure.
 */
EXTERN void freeContext (OOCTXT* pctxt);

/**
 * This function allocates a new OOCTXT block and initializes it. Although
 * the block is allocated from the standard heap, it should not be freed using
 * free. The freeContext function should be used because this frees items
 * allocated within the block before freeing the block itself.
 *
 * @return             Pointer to newly created context
 */
EXTERN OOCTXT* newContext (void);

EXTERN void copyContext (OOCTXT* pdest, OOCTXT* psrc);
EXTERN int  initSubContext (OOCTXT* pctxt, OOCTXT* psrc);
EXTERN void setCtxtFlag (OOCTXT* pctxt, ASN1USINT mask);
EXTERN void clearCtxtFlag (OOCTXT* pctxt, ASN1USINT mask);

EXTERN int setPERBuffer
(OOCTXT* pctxt, ASN1OCTET* bufaddr, ASN1UINT bufsiz, ASN1BOOL aligned);

EXTERN int setPERBufferUsingCtxt (OOCTXT* pTarget, OOCTXT* pSource);

#define ZEROCONTEXT(pctxt) memset(pctxt,0,sizeof(OOCTXT))
/**
 * @} cmfun
 */
/**
 * @defgroup errfp Error Formatting and Print Functions
 * @{
 *
 * Error formatting and print functions allow information about the
 * encode/decode errors to be added to a context block structure and then
 * printed out when the error is propagated to the top level.
 */

/**
 * This function adds an integer parameter to an error information structure.
 * Parameter substitution is done in much the same way as it is done in C
 * printf statements. The base error message specification that goes along with
 * a particular status code may have variable fields built in using '%'
 * modifiers. These would be replaced with actual parameter data.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 * @param errParm      The typed error parameter.
 * @return             The status of the operation.
 */
EXTERN int errAddIntParm (ASN1ErrInfo* pErrInfo, int errParm);

/**
 * This function adds an string parameter to an error information structure.
 * Parameter substitution is done in much the same way as it is done in C
 * printf statements. The base error message specification that goes along with
 * a particular status code may have variable fields built in using '%'
 * modifiers. These would be replaced with actual parameter data.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 * @param errprm_p     The typed error parameter.
 * @return             The status of the operation.
 */
EXTERN int errAddStrParm (ASN1ErrInfo* pErrInfo,
                            const char* errprm_p);

/**
 * This function adds an unsigned integer parameter to an error information
 * structure. Parameter substitution is done in much the same way as it is done
 * in C printf statements. The base error message specification that goes along
 * with a particular status code may have variable fields built in using '%'
 * modifiers. These would be replaced with actual parameter data.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 * @param errParm      The typed error parameter.
 * @return             The status of the operation.
 */
EXTERN int errAddUIntParm (ASN1ErrInfo* pErrInfo, unsigned int errParm);


EXTERN int errCopyData (ASN1ErrInfo* pSrcErrInfo,
                        ASN1ErrInfo* pDestErrInfo);

/**
 * This function frees memory associated with the storage of parameters
 * associated with an error message. These parameters are maintained on an
 * internal linked list maintained within the error information structure. The
 * list memory must be freed when error processing is complete. This function
 * is called from within errPrint after the error has been printed out. It is
 * also called in the freeContext function.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 */
EXTERN void  errFreeParms (ASN1ErrInfo* pErrInfo);


EXTERN char* errFmtMsg (ASN1ErrInfo* pErrInfo, char* bufp);

/**
 * This function gets the text of the error
 *
 * @param pctxt       A pointer to a context structure.
 */
EXTERN char* errGetText (OOCTXT* pctxt);

/**
 * This function prints error information to the standard output device. The
 * error information is stored in a structure of type ASN1ErrInfo. A structure
 * of the this type is part f the OOCTXT structure. This is where error
 * information is stored within the ASN1C generated and low-level encode/decode
 * functions.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 */
EXTERN void errPrint (ASN1ErrInfo* pErrInfo);

/**
 * This function resets the error information in the error information
 * sturcture.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 */
EXTERN int errReset (ASN1ErrInfo* pErrInfo);

/**
 * This function sets error information in an error information structure. The
 * information set includes status code, module name, and line number. Location
 * information (i.e. module name and line number) is pushed onto a stack within
 * the error information structure to provide a complete stack trace when the
 * information is printed out.
 *
 * @param pErrInfo     A pointer to a structure containing information on the
 *                     error to be printed. Typically, the error info
 *                     structure referred to is the one inside the OOCTXT
 *                     structure. (i.e. &pctxt->errInfo).
 * @param status       The error status code. This is one of the negative error
 *                     status codes.
 * @param module       The name of the module (C or C++ source file) in which
 *                     the module occurred. This is typically obtained by
 *                     using the _FILE_ macro.
 * @param lno          The line number at which the error occurred. This is
 *                     typically obtained by using the _LINE_ macro.
 * @return             The status value passed to the operation in the third
 *                     argument. This makes it possible to set the error
 *                     information and return the status value in one line of
 *                     code.
 */
EXTERN int errSetData (ASN1ErrInfo* pErrInfo, int status,
                       const char* module, int lno);

#ifndef _COMPACT
#define LOG_ASN1ERR(ctxt,stat) \
errSetData(&(ctxt)->errInfo,stat,__FILE__,__LINE__)
#else
#define LOG_ASN1ERR(ctxt,stat) \
((ctxt)->errInfo.status = stat, stat)
#endif



#define LOG_ASN1ERR_AND_FREE(pctxt,stat,lctxt) \
freeContext ((lctxt)), LOG_ASN1ERR(pctxt, stat)
/**
 * @}
 */

#define RT_MH_DONTKEEPFREE 0x1

#define OSRTMH_PROPID_DEFBLKSIZE   1
#define OSRTMH_PROPID_SETFLAGS     2
#define OSRTMH_PROPID_CLEARFLAGS   3

#define OSRTMH_PROPID_USER         10

/**
 * @addtogroup rtmem
 * @{
 */
/**
 * Allocate memory.  This macro allocates the given number of bytes.  It is
 * similar to the C \c malloc run-time function.
 *
 * @param pctxt - Pointer to a context block
 * @param nbytes - Number of bytes of memory to allocate
 * @return - Void pointer to allocated memory or NULL if insufficient memory
 *   was available to fulfill the request.
 */
#define memAlloc(pctxt,nbytes) \
memHeapAlloc(&(pctxt)->pTypeMemHeap,nbytes)

/**
 * Allocate and zero memory.  This macro allocates the given number of bytes
 * and then initializes the memory block to zero.
 *
 * @param pctxt - Pointer to a context block
 * @param nbytes - Number of bytes of memory to allocate
 * @return - Void pointer to allocated memory or NULL if insufficient memory
 *   was available to fulfill the request.
 */
#define memAllocZ(pctxt,nbytes) \
memHeapAllocZ(&(pctxt)->pTypeMemHeap,nbytes)

/**
 * Reallocate memory.  This macro reallocates a memory block (either
 * expands or contracts) to the given number of bytes.  It is
 * similar to the C \c realloc run-time function.
 *
 * @param pctxt - Pointer to a context block
 * @param mem_p - Pointer to memory block to reallocate.  This must have been
 *   allocated using the memHeapAlloc macro or the memHeapAlloc function.
 * @param nbytes - Number of bytes of memory to which the block is to be
 *   resized.
 * @return - Void pointer to allocated memory or NULL if insufficient memory
 *   was available to fulfill the request.  This may be the same as the pmem
 *   pointer that was passed in if the block did not need to be relocated.
 */
#define memRealloc(pctxt,mem_p,nbytes) \
memHeapRealloc(&(pctxt)->pTypeMemHeap, (void*)mem_p, nbytes)

/**
 * Free memory pointer.  This macro frees memory at the given pointer.
 * The memory must have been allocated using the memHeapAlloc (or similar)
 * macros or the mem memory allocation macros.  This macro is
 * similar to the C \c free function.
 *
 * @param pctxt - Pointer to a context block
 * @param mem_p - Pointer to memory block to free.  This must have
 *   been allocated using the memHeapAlloc or memAlloc macro or the
 *   memHeapAlloc function.
 */
#define memFreePtr(pctxt,mem_p) \
if (memHeapCheckPtr (&(pctxt)->pTypeMemHeap, (void*)mem_p)) \
memHeapFreePtr(&(pctxt)->pTypeMemHeap, (void*)mem_p)

/**
 * Free memory associated with a context.  This macro frees all memory
 * held within a context.  This is all memory allocated using the
 * memHeapAlloc (and similar macros) and the mem memory allocation
 * functions using the given context variable.
 *
 * @param pctxt - Pointer to a context block
 */
#define memFree(pctxt) \
memHeapFreeAll(&(pctxt)->pTypeMemHeap)

/**
 * Reset memory associated with a context.  This macro resets all memory
 * held within a context.  This is all memory allocated using the memHeapAlloc
 * (and similar macros) and the mem memory allocation functions using the
 * given context variable.
 *
 * <p>The difference between this and the ASN1MEMFREE macro is that the
 * memory blocks held within the context are not actually freed.  Internal
 * pointers are reset so the existing blocks can be reused.  This can
 * provide a performace improvement for repetitive tasks such as decoding
 * messages in a loop.
 *
 * @param pctxt - Pointer to a context block
 */
#define memReset(pctxt) \
memHeapReset(&(pctxt)->pTypeMemHeap)

/* Alias for __cdecl modifier; if __cdecl keyword is not supported,
 * redefine it as empty macro. */

#if !defined(OSCDECL)
#if defined(_MSC_VER) || defined(__BORLANDC__)
#define OSCDECL __cdecl
#else
#define OSCDECL
#endif
#endif /* OSCDECL */

/* Pointers to C Run-Time memory allocation functions *
 * (See memSetAllocFuncs)                           */

typedef void *(OSCDECL *OSMallocFunc ) (size_t size);
typedef void *(OSCDECL *OSReallocFunc) (void *ptr, size_t size);
typedef void  (OSCDECL *OSFreeFunc   ) (void *ptr);

EXTERN void  memHeapAddRef (void** ppvMemHeap);
EXTERN void* memHeapAlloc (void** ppvMemHeap, int nbytes);
EXTERN void* memHeapAllocZ (void** ppvMemHeap, int nbytes);
EXTERN int   memHeapCheckPtr (void** ppvMemHeap, void* mem_p);
EXTERN int   memHeapCreate (void** ppvMemHeap);
EXTERN void  memHeapFreeAll (void** ppvMemHeap);
EXTERN void  memHeapFreePtr (void** ppvMemHeap, void* mem_p);
EXTERN void* memHeapRealloc (void** ppvMemHeap, void* mem_p, int nbytes_);
EXTERN void  memHeapRelease (void** ppvMemHeap);
EXTERN void  memHeapReset (void** ppvMemHeap);

EXTERN void* memHeapMarkSaved
(void** ppvMemHeap, const void* mem_p, ASN1BOOL saved);

EXTERN void  memHeapSetProperty
(void** ppvMemHeap, ASN1UINT propId, void* pProp);


/**
 * This function sets the pointers to standard allocation functions. These
 * functions are used to allocate/reallocate/free the memory blocks. By
 * default, standard C functions - 'malloc', 'realloc' and 'free' - are used.
 * But if some platforms do not support these functions (or some other reasons
 * exist) they can be overloaded. The functions being overloaded should have
 * the same prototypes as standard ones.
 *
 * @param malloc_func Pointer to the memory allocation function ('malloc' by
 *    default).
 * @param realloc_func Pointer to the memory reallocation function ('realloc'
 *    by default).
 * @param free_func Pointer to the memory deallocation function ('free' by
 *    default).
 */
EXTERN void  memSetAllocFuncs (OSMallocFunc malloc_func,
                               OSReallocFunc realloc_func,
                               OSFreeFunc free_func);

EXTERN void  memFreeOpenSeqExt (OOCTXT* pctxt, DList *pElemList);

/*
 * This function sets flags to a heap. May be used to control the heap's
 * behavior.
 *
 * @param pctxt        Pointer to a memory block structure that contains the
 *                     list of dynamic memory block maintained by these
 *                     functions.
 * @param flags        The flags.
 */
EXTERN void  memHeapSetFlags (OOCTXT* pctxt, ASN1UINT flags);

/*
 * This function clears memory heap flags.
 *
 * @param pctxt        Pointer to a memory block structure that contains the
 *                     list of dynamic memory block maintained by these
 *                     functions.
 * @param flags        The flags
 */
EXTERN void  memHeapClearFlags (OOCTXT* pctxt, ASN1UINT flags);

/**
 * This function sets the pointer to standard allocation functions. These
 * functions are used to allocate/reallocate/free the memory blocks. By
 * default, standard C functions - malloc, realloc, and free - are used. But if
 * some platforms do not support these functions or some other reasons exist)
 * they can be overloaded. The functions being overloaded should have the same
 * prototypes as standard ones.
 *
 * @param pctxt        Pointer to a context block.
 * @param blkSize      The currently used minimum size and the granularity of
 *                     memory blocks.
 */

EXTERN void  memHeapSetDefBlkSize (OOCTXT* pctxt, ASN1UINT blkSize);

/**
 * This function returns the actual granularity of memory blocks.
 *
 * @param pctxt        Pointer to a context block.
 */
EXTERN ASN1UINT memHeapGetDefBlkSize (OOCTXT* pctxt);

#ifdef _STATIC_HEAP
EXTERN void memSetStaticBuf (void* memHeapBuf, ASN1UINT blkSize);
#endif

/* PER encode/decode related items */

#define INCRBITIDX(pctxt) \
((--(pctxt)->buffer.bitOffset < 0) ? \
((++(pctxt)->buffer.byteIndex >= (pctxt)->buffer.size) ? ASN_E_ENDOFBUF : \
((pctxt)->buffer.bitOffset = 7, ASN_OK)) : ASN_OK)

#define DECODEBIT(pctxt,pvalue) \
((INCRBITIDX (pctxt) != ASN_OK) ? ASN_E_ENDOFBUF : ( \
((*(pvalue) = (((pctxt)->buffer.data[(pctxt)->buffer.byteIndex]) & \
(1 << (pctxt)->buffer.bitOffset)) != 0), ASN_OK) ))
/*
#define DECODEBIT(pctxt,pvalue) \
((INCRBITIDX (pctxt) != ASN_OK) ? ASN_E_ENDOFBUF : ((pvalue) ? \
((*(pvalue) = (((pctxt)->buffer.data[(pctxt)->buffer.byteIndex]) & \
(1 << (pctxt)->buffer.bitOffset)) != 0), ASN_OK) : ASN_OK ))
*/

/*
#define SETCHARSET(csetvar, canset, abits, ubits) \
csetvar.charSet.nchars = 0; \
csetvar.canonicalSet = canset; \
csetvar.canonicalSetSize = sizeof(canset)-1; \
csetvar.canonicalSetBits = getUIntBitCount(csetvar.canonicalSetSize); \
csetvar.charSetUnalignedBits = ubits; \
csetvar.charSetAlignedBits = abits;
*/

/**
 * This function will decode a series of multiple bits and place the results in
 * an unsigned integer variable.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                    storage area for the function to store all working
 *                    variables that must be maintained between function
 *                    calls.
 * @param pvalue      A pointer to an unsigned integer variable to receive the
 *                    decoded result.
 * @param nbits       The number of bits to decode.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeBits
(OOCTXT* pctxt, ASN1UINT* pvalue, ASN1UINT nbits);

/**
 * This function will decode a value of the ASN.1 bit string type whose maximum
 * size is is known in advance. The ASN1C complier generates a call to this
 * function to decode bit string productions or elements that contain a size
 * constraint.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                    storage area for the function to store all working
 *                    variables that must be maintained between function
 *                    calls.
 * @param numbits_p   Pointer to an unsigned integer variable to receive
 *                    decoded number of bits.
 * @param buffer      Pointer to a fixed-size or pre-allocated array of bufsiz
 *                    octets to receive a decoded bit string.
 * @param bufsiz      Length (in octets) of the buffer to receive the decoded
 *                    bit string.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeBitString
(OOCTXT* pctxt, ASN1UINT* numbits_p, ASN1OCTET* buffer,
 ASN1UINT bufsiz);

/**
 * This function will decode a variable of the ASN.1 BMP character string. This
 * differs from the decode routines for the character strings previously
 * described in that the BMP string type is based on 16-bit characters. A
 * 16-bit character string is modeled using an array of unsigned short
 * integers.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                    storage area for the function to store all working
 *                    variables that must be maintained between function
 *                    calls.
 * @param pvalue      Pointer to character string structure to receive the
 *                    decoded result The structure includes a count field
 *                    containing the number of characters and an array of
 *                    unsigned short integers to hold the 16-bit character
 *                    values.
 * @param permCharSet A pointer to the constraining character set. This
 *                    contains an array containing all valid characters in
 *                    the set as well as the aligned and unaligned bit
 *                    counts required to encode the characters.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeBMPString
(OOCTXT* pctxt, ASN1BMPString* pvalue, Asn116BitCharSet* permCharSet);

/**
 * This function will position the decode bit cursor on the next byte boundary.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                    storage area for the function to store all working
 *                    variables that must be maintained between function
 *                    calls.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeByteAlign (OOCTXT* pctxt);

/**
 * This function will decode an integer constrained either by a value or value
 * range constraint.
 *
 * @param pctxt       Pointer to context block structure.
 * @param pvalue      Pointer to integer variable to receive decoded value.
 * @param lower       Lower range value.
 * @param upper       Upper range value.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeConsInteger
(OOCTXT* pctxt, ASN1INT* pvalue, ASN1INT lower, ASN1INT upper);

/**
 * This function will decode an unsigned integer constrained either by a value
 * or value range constraint.
 *
 * @param pctxt       Pointer to context block structure.
 * @param pvalue      Pointer to unsigned integer variable to receive decoded
 *                    value.
 * @param lower       Lower range value.
 * @param upper       Upper range value.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeConsUnsigned
(OOCTXT* pctxt, ASN1UINT* pvalue, ASN1UINT lower, ASN1UINT upper);

/**
 * This function will decode an 8-bit unsigned integer constrained either by a
 * value or value range constraint.
 *
 * @param pctxt       Pointer to context block structure.
 * @param pvalue      Pointer to 8-bit unsigned integer variable to receive
 *                    decoded value.
 * @param lower       Lower range value.
 * @param upper       Upper range value.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeConsUInt8 (OOCTXT* pctxt,
                            ASN1UINT8* pvalue, ASN1UINT lower, ASN1UINT upper);

/**
 * This function will decode a 16-bit unsigned integer constrained either by a
 * value or value range constraint.
 *
 * @param pctxt       Pointer to context block structure.
 * @param pvalue      Pointer to 16-bit unsigned integer variable to receive
 *                    decoded value.
 * @param lower       Lower range value.
 * @param upper       Upper range value.
 * @return            Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeConsUInt16
(OOCTXT* pctxt, ASN1USINT* pvalue, ASN1UINT lower, ASN1UINT upper);

/**
 * This function decodes a constrained whole number as specified in Section
 * 10.5 of the X.691 standard.
 *
 * @param pctxt             Pointer to context block structure.
 * @param padjusted_value   Pointer to unsigned adjusted integer value to
 *                          receive decoded result. To get the final value,
 *                          this value is added to the lower boundary of the
 *                          range.
 * @param range_value       Unsigned integer value specifying the total size
 *                          of the range. This is obtained by subtracting
 *                          the lower range value from the upper range
 *                          value.
 * @return                  Completion status of operation:
 *                             - 0 (ASN_OK) = success,
 *                             - negative return value is error.
 */
EXTERN int decodeConsWholeNumber
(OOCTXT* pctxt, ASN1UINT* padjusted_value, ASN1UINT range_value);

/**
 * This function decodes a constrained string value. This version of the
 * function allows all of the required permitted alphabet constraint parameters
 * to be passed in as arguments.
 *
 * @param pctxt        Pointer to context block structure.
 * @param string       Pointer to const char* to receive decoded string. Memory
 *                     will be allocated for this variable using internal
 *                     memory management functions.
 * @param charSet      String containing permitted alphabet character set. Can
 *                     be null if no character set was specified.
 * @param abits        Number of bits in a character set character (aligned).
 * @param ubits        Number of bits in a character set character (unaligned).
 * @param canSetBits   Number of bits in a character from the canonical set
 *                     representing this string.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeConstrainedStringEx
(OOCTXT* pctxt, const char** string, const char* charSet,
 ASN1UINT abits, ASN1UINT ubits, ASN1UINT canSetBits);

/**
 * This function will decode a variable of thr ASN.1 BIT STRING type. This
 * function allocates dynamic memory t store the decoded result. The ASN1C
 * complier generates a call to this function to decode an unconstrained bit
 * string production or element.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param pBitStr      Pointer to a dynamic bit string structure to receive the
 *                       decoded result. This structure contains a field to
 *                       hold the number of decoded bits and a pointer to an
 *                       octet string to hold the decoded data. Memory is
 *                       allocated by the decoder using the memAlloc
 *                       function. This memory is tracked within the context
 *                       and released when the freeContext function is
 *                       invoked.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeDynBitString (OOCTXT* pctxt, ASN1DynBitStr* pBitStr);

/**
 * This function will decode a value of the ASN.1 octet string type whose
 * maximum size is known in advance. The ASN1C complier generates a call to
 * this function to decode octet string productions or elements that contain a
 * size constraint.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param pOctStr      A pointer to a dynamic octet string to receive the
 *                       decoded result.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeDynOctetString
(OOCTXT* pctxt, ASN1DynOctStr* pOctStr);

/**
 * This function will decode a length determinant value.
 *
 * @param pctxt       A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param pvalue       A pointer to an unsigned integer variable to receive the
 *                       decoded length value.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeLength (OOCTXT* pctxt, ASN1UINT* pvalue);

/**
 * @param pctxt       A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param bitOffset    The bit offset inside the message buffer.
 */
EXTERN int moveBitCursor (OOCTXT* pctxt, int bitOffset);

/**
 * This function decodes a value of the ASN.1 object identifier type.
 *
 * @param pctxt        Pointer to context block structure.
 * @param pvalue       Pointer to value to receive decoded result. The
 *                       ASN1OBJID structure contains an integer to hold the
 *                       number of subidentifiers and an array to hold the
 *                       subidentifier values.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeObjectIdentifier (OOCTXT* pctxt, ASN1OBJID* pvalue);

/**
 * This function will decode a value of the ASN.1 octet string type whose
 * maximun size is known in advance. The ASN1C compiler generates a call to
 * this function to decode octet string productions or elements that contain a
 * size constraint.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param numocts_p    A pointer to an unsigned buffer of bufsiz octets to
 *                       receive decoded data.
 * @param buffer       A pointer to a pre-allocated buffer of size octets to
 *                       receive the decoded data.
 * @param bufsiz       The size of the buffer to receive the decoded result.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeOctetString
(OOCTXT* pctxt, ASN1UINT* numocts_p, ASN1OCTET* buffer,
 ASN1UINT bufsiz);

/**
 * This function will decode an ASN.1 open type. This used to be the ASN.1 ANY
 * type, but now is used in a variety of applications requiring an encoding
 * that can be interpreted by a decoder without prior knowledge of the type
 * of the variable.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param numocts_p    A pointer to an unsigned buffer of bufsiz octets to
 *                       receive decoded data.
 * @param object_p2    A pointer to an open type variable to receive the
 *                       decoded data.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeOpenType
(OOCTXT* pctxt, const ASN1OCTET** object_p2, ASN1UINT* numocts_p);

/**
 * This function will decode a small non-negative whole number as specified in
 * Section 10.6 of the X.691 standard. This is a number that is expected to be
 * small, but whose size is potentially unlimited due to the presence of an
 * extension maker.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all workings variables
 *                       that must be maintained between function calls.
 * @param pvalue       Pointer to an unsigned integer value t receive decoded
 *                       results.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeSmallNonNegWholeNumber
(OOCTXT* pctxt, ASN1UINT* pvalue);

/**
 * This function will decode a semi-constrained integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param pvalue       Pointer to integer variable to receive decoded value.
 * @param lower        Lower range value, represented as signed
 *                       integer.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeSemiConsInteger
   (OOCTXT* pctxt, ASN1INT* pvalue, ASN1INT lower);

/**
 * This function will decode a semi-constrained unsigned integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param pvalue       Pointer to unsigned integer variable to receive decoded
 *                       value.
 * @param lower        Lower range value, represented as unsigned
 *                       integer.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int decodeSemiConsUnsigned
   (OOCTXT* pctxt, ASN1UINT* pvalue, ASN1UINT lower);

/**
 * This function will decode an unconstrained integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param pvalue       Pointer to integer variable to receive decoded value.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
#define decodeUnconsInteger(pctxt,pvalue) \
decodeSemiConsInteger(pctxt, pvalue, ASN1INT_MIN)

/**
 * This function will decode an unconstrained unsigned integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param pvalue       Pointer to unsigned integer variable to receive decoded
 *                       value.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
#define decodeUnconsUnsigned(pctxt,pvalue) \
decodeSemiConsUnsigned(pctxt, pvalue, 0U)

EXTERN int decodeVarWidthCharString (OOCTXT* pctxt, const char** pvalue);

/**
 * This function will encode a variable of the ASN.1 BOOLEAN type in
 * a single bit.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param value        The BOOLEAN value to be encoded.
 */
EXTERN int encodeBit (OOCTXT* pctxt, ASN1BOOL value);

/**
 * This function encodes multiple bits.
 *
 * @param pctxt        Pointer to context block structure.
 * @param value        Unsigned integer containing the bits to be encoded.
 * @param nbits        Number of bits in value to encode.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeBits
(OOCTXT* pctxt, ASN1UINT value, ASN1UINT nbits);

/**
 * This function will encode a value of the ASN.1 bit string type.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param numocts      The number of bits n the string to be encoded.
 * @param data         Pointer to the bit string data to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeBitString
(OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data);

/**
 * This function will encode a variable of the ASN.1 BMP character string. This
 * differs from the encode routines for the character strings previously
 * described in that the BMP string type is based on 16-bit characters. A
 * 16-bit character string is modeled using an array of unsigned short
 * integers.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param value        Character string to be encoded. This structure includes
 *                       a count field containing the number of characters to
 *                       encode and an array of unsigned short integers to hold
 *                       the 16-bit characters to be encoded.
 * @param permCharSet  Pointer to the constraining character set. This contains
 *                       an array containing all valid characters in the set as
 *                       well as the aligned and unaligned bit counts required
 *                       to encode the characters.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeBMPString
(OOCTXT* pctxt, ASN1BMPString value, Asn116BitCharSet* permCharSet);

/**
 * This function will position the encode bit cursor on the next byte boundry.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeByteAlign (OOCTXT* pctxt);

/**
 * This function will determine if the given number of bytes will fit in the
 * encode buffer. If not, either the buffer is expanded (if it is a dynamic
 * buffer) or an error is signaled.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                       storage area for the function to store all working
 *                       variables that must be maintained between function
 *                       calls.
 * @param nbytes       Number of bytes of space required to hold the variable
 *                       to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeCheckBuffer (OOCTXT* pctxt, ASN1UINT nbytes);

/**
 * This function encodes a constrained string value. This version of the
 * function allows all of the required permitted alphabet constraint parameters
 * to be passed in as arguments.
 *
 * @param pctxt        Pointer to context block structure.
 * @param string       Pointer to string to be encoded.
 * @param charSet      String containing permitted alphabet character set. Can
 *                       be null if no character set was specified.
 * @param abits        Number of bits in a character set character (aligned).
 * @param ubits        Number of bits in a character set character (unaligned).
 * @param canSetBits   Number of bits in a character from the canonical set
 *                       representing this string.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeConstrainedStringEx
(OOCTXT* pctxt, const char* string, const char* charSet,
 ASN1UINT abits, ASN1UINT ubits, ASN1UINT canSetBits);

/**
 * This function encodes an integer constrained either by a value or value
 * range constraint.
 *
 * @param pctxt        Pointer to context block structure.
 * @param value        Value to be encoded.
 * @param lower        Lower range value.
 * @param upper        Upper range value.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeConsInteger
(OOCTXT* pctxt, ASN1INT value, ASN1INT lower, ASN1INT upper);

/**
 * This function encodes an unsigned integer constrained either by a value or
 * value range constraint. The constrained unsigned integer option is used if:
 *
 * 1. The lower value of the range is >= 0, and 2. The upper value of the range
 * is >= MAXINT
 *
 * @param pctxt        Pointer to context block structure.
 * @param value        Value to be encoded.
 * @param lower        Lower range value.
 * @param upper        Upper range value.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeConsUnsigned
(OOCTXT* pctxt, ASN1UINT value, ASN1UINT lower, ASN1UINT upper);

/**
 * This function encodes a constrained whole number as specified in Section
 * 10.5 of the X.691 standard.
 *
 * @param pctxt             Pointer to context block structure.
 * @param adjusted_value    Unsigned adjusted integer value to be encoded. The
 *                            adjustment is done by subtracting the lower value
 *                            of the range from the value to be encoded.
 * @param range_value       Unsigned integer value specifying the total size of
 *                            the range. This is obtained by subtracting the
 *                            lower range value from the upper range value.
 * @return                  Completion status of operation:
 *                            - 0 (ASN_OK) = success,
 *                            - negative return value is error.
 */
EXTERN int encodeConsWholeNumber
(OOCTXT* pctxt, ASN1UINT adjusted_value, ASN1UINT range_value);

/**
 * This function will expand the buffer to hold the given number of bytes.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param nbytes       The number of bytes the buffer is to be expanded by.
 *                       Note that the buffer will be expanded by
 *                       ASN_K_ENCBIFXIZ or nbytes (whichever is larger.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeExpandBuffer (OOCTXT* pctxt, ASN1UINT nbytes);

/**
 * This function will return the message pointer and length of an encoded
 * message. This function is called after a complier generated encode function
 * to get the pointer and length of the message. It is normally used when
 * dynamic encoding is specified because the message pointer is not known until
 * encoding is complete. If static encoding is used, the message starts at the
 * beginning of the specified buffer adn the encodeGetMsgLen function can be
 * used to obtain the lenght of the message.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param pLength      Pointer to variable to receive length of the encoded
 *                       message.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN ASN1OCTET* encodeGetMsgPtr (OOCTXT* pctxt, int* pLength);

/**
 * This function will encode a length determinant value.
 *
 * @param pctxt        Pointer to a context structure. This provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param value        Length value to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeLength (OOCTXT* pctxt, ASN1UINT value);

/**
 * This function encodes a value of the ASN.1 object identifier type.
 *
 * @param pctxt        Pointer to context block structure.
 * @param pvalue       Pointer to value to be encoded. The ASN1OBJID structure
 *                       contains a numids fields to hold the number of
 *                       subidentifiers and an array to hold the subidentifier
 *                       values.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeObjectIdentifier (OOCTXT* pctxt, ASN1OBJID* pvalue);


/**
 *
 *  This function encodes bits from a given octet to the output buffer.
 *
 *  @param pctxt      Pointer to ASN.1 PER context structure
 *  @param value      Value of bits to be encoded
 *  @param nbits      Number of bits to be encoded
 *
 *  @return           Status of operation
 */
EXTERN int encodebitsFromOctet (OOCTXT* pctxt, ASN1OCTET value, ASN1UINT nbits);

/**
 * This fuction will encode an array of octets. The Octets will be encoded
 * unaligned starting at the current bit offset within the encode buffer.
 *
 * @param pctxt        A pointer to a context structure. The provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param pvalue       A pointer to an array of octets to encode
 * @param nbits        The number of Octets to encode
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeOctets
(OOCTXT* pctxt, const ASN1OCTET* pvalue, ASN1UINT nbits);

/**
 * This function will encode a value of the ASN.1 octet string type.
 *
 * @param pctxt        A pointer to a context structure. The provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param numocts      Number of octets in the string to be encoded.
 * @param data         Pointer to octet string data to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeOctetString
(OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data);

/**
 * This function will encode an ASN.1 open type. This used to be the ANY type,
 * but now is used in a variety of applications requiring an encoding that can
 * be interpreted by a decoder without a prior knowledge of the type of the
 * variable.
 *
 * @param pctxt        A pointer to a context structure. The provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param numocts      Number of octets in the string to be encoded.
 * @param data         Pointer to octet string data to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeOpenType
(OOCTXT* pctxt, ASN1UINT numocts, const ASN1OCTET* data);

/**
 * This function will encode an ASN.1 open type extension. An open type
 * extension field is the data that potentially resides after the ... marker in
 * a version-1 message. The open type structure contains a complete encoded bit
 * set including option element bits or choice index, length, and data.
 * Typically, this data is populated when a version-1 system decodes a
 * version-2 message. The extension fields are retained and can then be
 * re-encoded if a new message is to be sent out (for example, in a store and
 * forward system).
 *
 * @param pctxt        A pointer to a context structure. The provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param pElemList    A pointer to the open type to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeOpenTypeExt
(OOCTXT* pctxt, DList* pElemList);

EXTERN int encodeOpenTypeExtBits
(OOCTXT* pctxt, DList* pElemList);

/**
 * This function will endcode a small, non-negative whole number as specified
 * in Section 10.6 of the X.691 standard. This is a number that is expected to
 * be small, but whose size is potentially unlimited due to the presence of an
 * extension marker.
 *
 * @param pctxt        A pointer to a context structure. The provides a storage
 *                       area for the function to store all working variables
 *                       that must be maintained between function calls.
 * @param value        An unsigned integer value to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeSmallNonNegWholeNumber (OOCTXT* pctxt, ASN1UINT value);

/**
 * This function encodes a semi-constrained integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param value        Value to be encoded.
 * @param lower        Lower range value, represented as signed
 *                       integer.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeSemiConsInteger
   (OOCTXT* pctxt, ASN1INT value, ASN1INT lower);

/**
 * This function encodes an semi-constrained unsigned integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param value        Value to be encoded.
 * @param lower        Lower range value, represented as unsigned
 *                       integer.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
EXTERN int encodeSemiConsUnsigned
   (OOCTXT* pctxt, ASN1UINT value, ASN1UINT lower);

/**
 * This function encodes an unconstrained integer.
 *
 * @param pctxt        Pointer to context block structure.
 * @param value        Value to be encoded.
 * @return             Completion status of operation:
 *                       - 0 (ASN_OK) = success,
 *                       - negative return value is error.
 */
#define encodeUnconsInteger(pctxt,value) \
encodeSemiConsInteger(pctxt,value,ASN1INT_MIN)

EXTERN int encodeVarWidthCharString (OOCTXT* pctxt, const char* value);

EXTERN int addSizeConstraint (OOCTXT* pctxt, Asn1SizeCnst* pSize);

EXTERN ASN1BOOL alignCharStr
(OOCTXT* pctxt, ASN1UINT len, ASN1UINT nbits, Asn1SizeCnst* pSize);

EXTERN int bitAndOctetStringAlignmentTest
(Asn1SizeCnst* pSizeList, ASN1UINT itemCount,
 ASN1BOOL bitStrFlag, ASN1BOOL* pAlignFlag);

EXTERN int getPERMsgLen (OOCTXT* pctxt);

EXTERN int addSizeConstraint (OOCTXT* pctxt, Asn1SizeCnst* pSize);

EXTERN Asn1SizeCnst* getSizeConstraint (OOCTXT* pctxt, ASN1BOOL extbit);

EXTERN int checkSizeConstraint(OOCTXT* pctxt, int size);
EXTERN ASN1UINT getUIntBitCount (ASN1UINT value);

EXTERN Asn1SizeCnst* checkSize
(Asn1SizeCnst* pSizeList, ASN1UINT value, ASN1BOOL* pExtendable);

EXTERN void init16BitCharSet
(Asn116BitCharSet* pCharSet, ASN116BITCHAR first,
 ASN116BITCHAR last, ASN1UINT abits, ASN1UINT ubits);

EXTERN ASN1BOOL isExtendableSize (Asn1SizeCnst* pSizeList);

EXTERN void set16BitCharSet
(OOCTXT* pctxt, Asn116BitCharSet* pCharSet, Asn116BitCharSet* pAlphabet);

#ifdef __cplusplus
}
#endif

#endif
