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

#include "ooCommon.h"
#include "ooDateTime.h"

#if defined(_WIN32) || defined(_MSC_VER) || defined(__MINGW32__)

#ifndef __MINGW32__
#include <SYS\TIMEB.H>
int gettimeofday (struct timeval *tv, struct timezone *tz)
{
   struct _timeb currSysTime;
   _ftime(&currSysTime);

   tv->tv_sec = currSysTime.time;
   tv->tv_usec = currSysTime.millitm * 1000;

   return 0;
}

#else
/* Mingw specific logic..probably works with regular winders
 * too, but cannot test. --Ben
 */
#define uint64 unsigned long long
#define uint32 unsigned long

static uint64 calcEpocOffset() {
   SYSTEMTIME st;
   FILETIME ft;

   memset(&st, 0, sizeof(st));
   memset(&ft, 0, sizeof(ft));

   st.wYear = 1970;
   st.wMonth = 1;
   st.wDayOfWeek = 0;
   st.wDay = 1;
   st.wHour = 0;
   st.wMinute = 0;
   st.wSecond = 0;
   st.wMilliseconds = 0;

   if (!SystemTimeToFileTime(&st, &ft)) {
      //cout << "ERROR:  SystemTimeToFileTime failed, err: "
      //       << GetLastError() << endl;
   }

   uint64 t = ft.dwHighDateTime;
   t = t << 32;
   t |= ft.dwLowDateTime;

   return t;
}

// Gets high resolution by spinning up to 15ms.  Don't call this often!!!
static uint64 getRawCurMsSpin() {
   FILETIME tm;
   uint64 t_now;

   static uint64 epocOffset = 0;
   static int do_once = 1;
   if (do_once) {
      epocOffset = calcEpocOffset();
      do_once = 0;
   }

   GetSystemTimeAsFileTime(&tm);
   uint64 t_start = tm.dwHighDateTime;
   t_start = t_start << 32;
   t_start |= tm.dwLowDateTime;
   while (1) {
      GetSystemTimeAsFileTime(&tm);
      t_now = tm.dwHighDateTime;
      t_now = t_now << 32;
      t_now |= tm.dwLowDateTime;

      if (t_now != t_start) {
         // Hit a boundary...as accurate as we are going to get.
         break;
      }
   }


   t_now -= epocOffset;

   // t is in 100ns units, convert to usecs
   t_now = t_now / 10000; //msecs

   return t_now;
}

static uint64 baselineMs = 0;
static uint32 tickBaseline = 0;

int gettimeofday(struct timeval* tv, void* null) {

   // Get baseline time, in seconds.
   if (baselineMs == 0) {
      baselineMs = getRawCurMsSpin();
      tickBaseline = timeGetTime();
   }

   uint32 curTicks = timeGetTime();
   if (curTicks < tickBaseline) {
      // Wrap!
      baselineMs = getRawCurMsSpin();
      tickBaseline = timeGetTime();
   }

   uint64 now_ms = (baselineMs + (curTicks - tickBaseline));
   *tv = oo_ms_to_tv(now_ms);
   return 0;
};


/* Correctly synched with the 'real' time/date clock, but only exact to
 * about 15ms.  Set foo to non NULL if you want to recalculate the
 */
uint64 getCurMsFromClock() {
   // This has resolution of only about 15ms
   FILETIME tm;
   // The Windows epoc is January 1, 1601 (UTC).
   static uint64 offset = 0;
   static int do_once = 1;
   if (do_once) {
      offset = calcEpocOffset();
      do_once = 0;
   }


   GetSystemTimeAsFileTime(&tm);
   uint64 t = tm.dwHighDateTime;
   t = t << 32;
   t |= tm.dwLowDateTime;

   /*cout << "file-time: " << t << " offset: " << offset
          << " normalized: " << (t - offset) << " hi: "
          << tm.dwHighDateTime << " lo: " << tm.dwLowDateTime
          << " calc-offset:\n" << calcEpocOffset() << "\n\n";
   */
   t -= offset;

   // t is in 100ns units, convert to ms
   t = t / 10000;
   return t;
}
#endif
#endif

int ooGetTimeOfDay (struct timeval *tv, struct timezone *tz)
{
   return gettimeofday (tv, tz);
}


long ooGetTimeDiff(struct timeval *tv1, struct timeval *tv2)
{
   return ( ((tv2->tv_sec-tv1->tv_sec)*1000) +
            ((tv2->tv_usec-tv1->tv_usec)/1000) );
}
