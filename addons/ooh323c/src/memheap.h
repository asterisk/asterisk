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

#ifndef __RTMEMHEAP_HH__
#define __RTMEMHEAP_HH__

#include "ooasn1.h"

/* internal heap flags */
#define RT_MH_INTERNALMASK 0xF0000000u
#define RT_MH_FREEHEAPDESC 0x10000000u

typedef struct OSMemLink {
   struct OSMemLink* pnext;
   struct OSMemLink* pprev;
   struct OSMemLink* pnextRaw;  /* next RAW block                           */
   void*           pMemBlk;
   ASN1OCTET       blockType;   /* 1 = standard, 2 = raw (see RTMEM* flags) */
} OSMemLink;

/* MemLink blockTypes */
#define RTMEMSTD        0x0001
#define RTMEMRAW        0x0002
#define RTMEMMALLOC     0x0004
#define RTMEMSAVED      0x0008
#define RTMEMLINK       0x0010  /* contains MemLink */

/* ASN.1 memory allocation structures */

typedef struct OSMemHeap {
   OSMemLink*      phead;
   ASN1UINT        usedUnits;
   ASN1UINT        usedBlocks;
   ASN1UINT        freeUnits;
   ASN1UINT        freeBlocks;
   ASN1UINT        keepFreeUnits;
   ASN1UINT        defBlkSize;
   ASN1UINT        refCnt;
   ASN1UINT        flags;
} OSMemHeap;

/* see rtMemDefs.c file */
extern ASN1UINT      g_defBlkSize;
extern OSMallocFunc  g_malloc_func;
extern OSReallocFunc g_realloc_func;
extern OSFreeFunc    g_free_func;

#endif /* __RTMEMHEAP_HH__ */
