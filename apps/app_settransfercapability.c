/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to set the ISDN Transfer Capability
 * 
 * Copyright (C) 2005, Frank Sautter, levigo holding gmbh, www.levigo.de
 *
 * Frank Sautter - asterisk+at+sautter+dot+com 
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/transcap.h"
#include <string.h>
#include <stdlib.h>
#include "asterisk/transcap.h"


static char *app = "SetTransferCapability";

static char *synopsis = "Set ISDN Transfer Capability";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct {	int val; char *name; } transcaps[] = {
	{ AST_TRANS_CAP_SPEECH,				"SPEECH" },
	{ AST_TRANS_CAP_DIGITAL,			"DIGITAL" },
	{ AST_TRANS_CAP_RESTRICTED_DIGITAL,	"RESTRICTED_DIGITAL" },
	{ AST_TRANS_CAP_3_1K_AUDIO,			"3K1AUDIO" },
	{ AST_TRANS_CAP_DIGITAL_W_TONES,	"DIGITAL_W_TONES" },
	{ AST_TRANS_CAP_VIDEO,				"VIDEO" },
};

static char *descrip = 
"  SetTransferCapability(transfercapability): Set the ISDN Transfer \n"
"Capability of a call to a new value.\n"
"Always returns 0.  Valid Transfer Capabilities are:\n"
"\n"
"  SPEECH             : 0x00 - Speech (default, voice calls)\n"
"  DIGITAL            : 0x08 - Unrestricted digital information (data calls)\n"
"  RESTRICTED_DIGITAL : 0x09 - Restricted digital information\n"
"  3K1AUDIO           : 0x10 - 3.1kHz Audio (fax calls)\n"
"  DIGITAL_W_TONES    : 0x11 - Unrestricted digital information with tones/announcements\n"
"  VIDEO              : 0x18 - Video:\n"
"\n"
;

static int settransfercapability_exec(struct ast_channel *chan, void *data)
{
	char tmp[256] = "";
	struct localuser *u;
	int x;
	char *opts;
	int transfercapability = -1;
	
	if (data)
		strncpy(tmp, (char *)data, sizeof(tmp) - 1);
	opts = strchr(tmp, '|');
	if (opts)
		*opts = '\0';
	for (x=0;x<sizeof(transcaps) / sizeof(transcaps[0]);x++) {
		if (!strcasecmp(transcaps[x].name, tmp)) {
			transfercapability = transcaps[x].val;
			break;
		}
	}
	if (transfercapability < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid transfer capability (see 'show application SetTransferCapability')\n", tmp);
		return 0;
	} else {
		LOCAL_USER_ADD(u);
		chan->transfercapability = (unsigned short)transfercapability;
		LOCAL_USER_REMOVE(u);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Setting transfer capability to: 0x%.2x - %s.\n", transfercapability, tmp);			
		return 0;
	}
}


int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, settransfercapability_exec, synopsis, descrip);
}

char *description(void)
{
	return synopsis;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
