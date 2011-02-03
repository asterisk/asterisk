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
#ifndef _OO_CHAN_H323_H_
#define _OO_CHAN_H323_H_

#include "asterisk.h"
#undef PACKAGE_NAME
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_STRING
#undef PACKAGE_BUGREPORT

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/signal.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/causes.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/dsp.h"
#include "asterisk/stringfields.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/udptl.h"

#include "ootypes.h"
#include "ooUtils.h"
#include "ooCapability.h"
#include "oochannels.h"
#include "ooh323ep.h"
#include "ooh323cDriver.h"
#include "ooCalls.h"
#include "ooq931.h"
#include "ooStackCmds.h"
#include "ooCapability.h"
#include "ooGkClient.h"


struct ooh323_pvt;
struct ooh323_user;
struct ooh323_peer;
/* Helper functions */
struct ooh323_user *find_user(const char * name, const char *ip);
struct ooh323_peer *find_peer(const char * name, int port);
void ooh323_delete_peer(struct ooh323_peer *peer);   

int delete_users(void);
int delete_peers(void);

int ooh323_destroy(struct ooh323_pvt *p);
int reload_config(int reload);
int restart_monitor(void);

int configure_local_rtp(struct ooh323_pvt *p, ooCallData* call);
void setup_rtp_connection(ooCallData *call, const char *remoteIp, 
                          int remotePort);
void close_rtp_connection(ooCallData *call);
struct ast_frame *ooh323_rtp_read
         (struct ast_channel *ast, struct ooh323_pvt *p);

void ooh323_set_write_format(ooCallData *call, struct ast_format *fmt, int txframes);
void ooh323_set_read_format(ooCallData *call, struct ast_format *fmt);

int ooh323_update_capPrefsOrderForCall
   (ooCallData *call, struct ast_codec_pref *prefs);

int ooh323_convertAsteriskCapToH323Cap(struct ast_format *format);

int ooh323_convert_hangupcause_asteriskToH323(int cause);
int ooh323_convert_hangupcause_h323ToAsterisk(int cause);
int update_our_aliases(ooCallData *call, struct ooh323_pvt *p);

/* h323 msg callbacks */
int ooh323_onReceivedSetup(ooCallData *call, Q931Message *pmsg);
int ooh323_onReceivedDigit(OOH323CallData *call, const char* digit);

void setup_udptl_connection(ooCallData *call, const char *remoteIp, int remotePort);
void close_udptl_connection(ooCallData *call);

EXTERN char *handle_cli_ooh323_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

#endif
