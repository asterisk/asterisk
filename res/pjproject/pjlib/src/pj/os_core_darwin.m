/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pj/os.h>
#include "TargetConditionals.h"

#if TARGET_OS_IPHONE

PJ_DEF(int) pj_run_app(pj_main_func_ptr main_func, int argc, char *argv[],
                       unsigned flags)
{
    return (*main_func)(argc, argv);
}

#else

#include <pthread.h>
#include <AppKit/AppKit.h>
#include <CoreFoundation/CFRunLoop.h>
#include <Foundation/Foundation.h>

#define THIS_FILE   "os_core_darwin.m"

typedef struct run_app_t {
    pj_main_func_ptr  main_func;
    int               argc;
    char            **argv;
    int               retval;
} run_app_t;

@interface DeadThread: NSObject { ;; }
+ (void)enterMultiThreadedMode;
+ (void)emptyThreadMethod:(id)obj;
@end

@implementation DeadThread
+ (void)enterMultiThreadedMode
{
    [NSThread detachNewThreadSelector:@selector(emptyThreadMethod:)
              toTarget:[DeadThread class] withObject:nil];
}

+ (void)emptyThreadMethod:(id)obj { ; }
@end

static void* main_thread(void *data)
{
    run_app_t *param = (run_app_t *)data;
    
    param->retval = (*param->main_func)(param->argc, param->argv);
    CFRunLoopStop(CFRunLoopGetMain());
    
    return NULL;
}

/*
 * pj_run_app()
 * This function has to be called from the main thread. The purpose of
 * this function is to initialize the application's memory pool, event
 * loop management, and multi-threading environment.
 */
PJ_DEF(int) pj_run_app(pj_main_func_ptr main_func, int argc, char *argv[],
                       unsigned flags)
{
    pthread_t thread;
    run_app_t param;
    NSAutoreleasePool *pool;
    
    pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    [DeadThread enterMultiThreadedMode];

    param.argc = argc;
    param.argv = (char **)argv;
    param.main_func = main_func;
    if (pthread_create(&thread, NULL, &main_thread, &param) == 0) {
        CFRunLoopRun();
    }
    
    PJ_UNUSED_ARG(pool);
    
    return param.retval;
}

#endif
