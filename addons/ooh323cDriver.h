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
#ifndef _OO_H323CDRIVER_H_
#define _OO_H323CDRIVER_H_
#include "chan_ooh323.h"
#include "ootypes.h"
#include "ooh323ep.h"
#include "oochannels.h"
#include "ooCalls.h"
#include "ooCapability.h"
#include "ooStackCmds.h"
#include "asterisk/format.h"
#define H323_DTMF_RFC2833          (1 << 0)
#define H323_DTMF_Q931             (1 << 1)
#define H323_DTMF_H245ALPHANUMERIC (1 << 2)
#define H323_DTMF_H245SIGNAL       (1 << 3)
#define H323_DTMF_INBAND           (1 << 4)
#define H323_DTMF_CISCO		   (1 << 5)
#define H323_DTMF_INBANDRELAX	   (1 << 8)

struct h323_pvt;
int ooh323c_start_stack_thread(void);
int ooh323c_stop_stack_thread(void);
int ooh323c_start_call_thread(ooCallData *call);
int ooh323c_stop_call_thread(ooCallData *call);
int ooh323c_set_capability
   (struct ast_format_cap *cap, int dtmf, int dtmfcodec);
struct ast_format *convertH323CapToAsteriskCap(int cap);
int ooh323c_set_capability_for_call
   (ooCallData *call, struct ast_format_cap *cap, int dtmf, int dtmfcodec,
	int t38support, int g729onlyA);
#endif
