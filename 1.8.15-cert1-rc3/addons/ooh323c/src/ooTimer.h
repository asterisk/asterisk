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
 * @file ooTimer.h 
 * Timer structures and functions.
 */
#ifndef _OOTIMER_H_
#define _OOTIMER_H_

#include "ooasn1.h"
#include "ooSocket.h"   /* needed for timeval */

struct _OOTimer;

typedef int (*OOTimerCbFunc)(void *data);

typedef struct _OOTimer {
   struct timeval expireTime, timeout;
   void*        cbData;
   OOBOOL       reRegister;

   /* Callback functions */
   OOTimerCbFunc timeoutCB;
} OOTimer;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This function computes the relative expiration time from the current 
 * time for the given timer object.
 *
 * @param pTimer       Pointer to timer object.
 */
EXTERN void ooTimerComputeExpireTime (OOTimer* pTimer);

/**
 * This function creates and initializes a new timer object.
 * @param pctxt        OOCTXT structure used for timer memory allocation.
 * @param pList        Pointer to timer list in which newly created timer will
 *                     be inserted.
 * @param cb           Timer callback function.
 * @param deltaSecs    Time in seconds to timer expiration.
 * @param data         Callback user data argument.
 * @param reRegister   Should timer be re-registered after it expires?
 * @return             Pointer to created timer object.
 */
EXTERN OOTimer* ooTimerCreate 
(OOCTXT* pctxt, DList *pList, OOTimerCbFunc cb, OOUINT32 deltaSecs, void *data,
 OOBOOL reRegister);

/**
 * This function deletes the given timer object.
 * @param pctxt        Handle to OOCTXT structure used for timer memory.
 * @param pList        timer list to operate on
 * @param pTimer       Pointer to timer object.
 */
EXTERN void ooTimerDelete (OOCTXT* pctxt, DList* pList, OOTimer* pTimer);

/**
 * This function checks a timer to determine if it is expired.
 *
 * @param pTimer       Pointer to timer object.
 * @return             True if timer expired, false if not.
 */
EXTERN OOBOOL ooTimerExpired (OOTimer* pTimer);

/**
 * This function loops through the global timer list and fires all 
 * expired timers by calling the registered callback functions.
 */
EXTERN void ooTimerFireExpired (OOCTXT* pctxt, DList* pList);

/**
 * This function inserts the given timer object into the correct 
 * chronological position in the global timer list.
 * @param pctxt        Pointer to OOCTXT structure used for memory allocation.
 * @param pList        List in which timer has to be inserted.
 * @param pTimer       Pointer to timer object.
 * @return             Index to position where inserted in list.
 */
EXTERN int ooTimerInsertEntry (OOCTXT* pctxt, DList* pList, OOTimer* pTimer);

/**
 * This function calculates the relative time from the current time 
 * that the first timer in global timer list will expire.
 * @param pList         Handle to timer list
 * @param ptimeout      timeval structure to receive timeout value.
 * @return              ptimeout
 */
EXTERN struct timeval* ooTimerNextTimeout (DList* pList, struct timeval* ptimeout);

/**
 * This function resets the given timer object if its reregister flag 
 * is set.  Otherwise, it is deleted.
 * @param pctxt        Pointer to OOCTXT structre used for memory allocation.
 * @param pList        Pointer to timer list.
 * @param pTimer       Pointer to timer object.
 */
EXTERN void ooTimerReset (OOCTXT* pctxt, DList* pList, OOTimer* pTimer);


/**
 * This function is used to compare two timeout values.
 * @param to1          First timeout value.
 * @param to2          Second timeout value.
 *
 * @return             1, if to1 > to2; 0, if to1 == to2; -1, if to1 < to2
 */
int ooCompareTimeouts(struct timeval *to1, struct timeval *to2);
#ifdef __cplusplus
}
#endif
#endif
