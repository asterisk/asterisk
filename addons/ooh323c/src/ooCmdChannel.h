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
 * @file ooCmdChannel.h 
 * This file contains stack commands which an user application can use to make
 * call, hang call etc. 
 */

#ifndef OO_CMDCHANNEL_H
#define OO_CMDCHANNEL_H

#include "ootypes.h"
#include "ooStackCmds.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#if defined (MAKE_DLL)
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */


#define OO_DEFAULT_CMDLISTENER_PORT 7575

/**
 * @addtogroup channels 
 * @{
 */

/**
 * This function is used to setup a command connection with the main stack 
 * thread. The application commands are sent over this connection to stack 
 * thread.
 *
 * @return          OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooCreateCmdConnection();

/**
 * This function is used to close a command channel setup with the stack 
 * thread.
 *
 * @return          OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooCloseCmdConnection();


/**
 * This function is used by stack api to write stack commands to command 
 * channel.
 *
 * @return          OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooWriteStackCommand(OOStackCommand *cmd);

/**
 * This function is used by stack thread to read and process stack commands 
 * received over command channel.
 *
 * @return          OO_OK, on success; OO_FAILED, on failure
 */
EXTERN int ooReadAndProcessStackCommand();


/** 
 * @} 
 */

#ifdef __cplusplus
}
#endif

#endif
