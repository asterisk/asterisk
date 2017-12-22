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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "ootypes.h"
#include "ootrace.h"
#include "ooCommon.h"
#include "ooCapability.h"
#include "ooq931.h"
#include "ooh245.h"
#include "ooh323ep.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

static OOUINT32 gs_traceLevel = TRACELVL;

void ooSetTraceThreshold(OOUINT32 traceLevel)
{
   gs_traceLevel = traceLevel;
}

void ooTrace(OOUINT32 traceLevel, const char * fmtspec, ...) __attribute__((format(printf, 2, 3)));

void ooTrace(OOUINT32 traceLevel, const char * fmtspec, ...) {
   va_list arglist;
   char logMessage[MAXLOGMSGLEN];
   if(traceLevel > gs_traceLevel) return;
   va_start (arglist, fmtspec);
   /*   memset(logMessage, 0, MAXLOGMSGLEN);*/
   vsprintf(logMessage, fmtspec, arglist);
   va_end(arglist);
   ooTraceLogMessage(logMessage);
}

void ooTraceLogMessage(const char * logMessage)
{
   char timeString[100];
   char currtime[3];
   static int lasttime=25;
   int printDate =0;
   static int printTime=1;

#ifdef _WIN32

   SYSTEMTIME systemTime;
   GetLocalTime(&systemTime);
   GetTimeFormat(LOCALE_SYSTEM_DEFAULT,0, &systemTime, "HH':'mm':'ss",
                                                              timeString, 100);
   GetTimeFormat(LOCALE_SYSTEM_DEFAULT,0, &systemTime, "H", currtime, 3);
   if(lasttime> atoi(currtime))
      printDate=1;
   lasttime = atoi(currtime);

#else
   struct tm *ptime;
   char dateString[10];
   time_t t = time(NULL);
   ptime = localtime(&t);
   strftime(timeString, 100, "%H:%M:%S", ptime);
   strftime(currtime, 3, "%H", ptime);
   if(lasttime>atoi(currtime))
       printDate = 1;
   lasttime = atoi(currtime);
#endif


#ifdef _WIN32
   if(printDate)
   {
      printDate = 0;
      fprintf(gH323ep.fptraceFile, "---------Date %d/%d/%d---------\n",
                      systemTime.wMonth, systemTime.wDay, systemTime.wYear);
   }
   if(printTime) {
      fprintf(gH323ep.fptraceFile, "%s:%03d  %s", timeString,
              systemTime.wMilliseconds, logMessage);
   }
   else
      fprintf(gH323ep.fptraceFile, "%s", logMessage);

   fflush(gH323ep.fptraceFile);
#else
   if(printDate)
   {
      printDate = 0;
      strftime(dateString, 10, "%d", ptime);
      fprintf(gH323ep.fptraceFile, "---------Date %s---------\n",
              dateString);
   }
   if(printTime) {
      struct timeval systemTime;
      gettimeofday(&systemTime, NULL);
      fprintf(gH323ep.fptraceFile, "%s:%03ld  %s", timeString,
               (long) systemTime.tv_usec/1000, logMessage);
   }
   else
      fprintf(gH323ep.fptraceFile, "%s", logMessage);

   fflush(gH323ep.fptraceFile);
#endif

   if(strchr(logMessage, '\n'))
      printTime = 1;
   else
      printTime = 0;

}

int ooLogAsn1Error(int stat, const char * fname, int lno)
{
   OOTRACEERR4("Asn1Error: %d at %s:%d\n", stat, fname, lno);
   return stat;
}
