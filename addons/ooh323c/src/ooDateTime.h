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
 * @file ooDateTime.h
 * Time functions that reconcile differences between Windows and UNIX.
 */
#ifndef _OOTIME_H_
#define _OOTIME_H_

#include "ooCommon.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This function provides an abstraction for the UNIX 'gettimeofday'
 * function which is not available on Windows.
 *
 * @param tv           Pointer to time value structure to receive
 *                     current time value.
 * @param tz           Point to time zone information.
 * @return             Completion status of operation: 0 = success,
 *                     negative return value is error.
 */
EXTERN int ooGetTimeOfDay (struct timeval *tv, struct timezone *tz);

/**
 * This function subtracts first timeval parameter from second and provides
 * the difference in milliseconds.
 * @param tv1          Pointer to timeval value.
 * @param tv2          Pointer to timeval value.
 *
 * @return             Difference between two timevals in milliseconds.
 */
EXTERN long ooGetTimeDiff(struct timeval *tv1, struct timeval *tv2);

EXTERN int ooGetTimeOfDay (struct timeval *tv, struct timezone *tz);
EXTERN long ooGetTimeDiff(struct timeval *tv1, struct timeval *tv2);

#ifdef __cplusplus
}
#endif
#endif
