/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * HasVoicemail application
 * Changes Copyright (c) 2004 - 2005 Todd Freeman <freeman@andrews.edu>
 * 
 * 95% based on HasNewVoicemail by:
 * 
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
 * 
 * Tilghman Lesher <asterisk-hasnewvoicemail-app@the-tilghman.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

static char *tdesc = "Indicator for whether a voice mailbox has messages in a given folder.";
static char *app_hasvoicemail = "HasVoicemail";
static char *hasvoicemail_synopsis = "Conditionally branches to priority + 101";
static char *hasvoicemail_descrip =
"HasVoicemail(vmbox[@context][:folder][|varname])\n"
"  Branches to priority + 101, if there is voicemail in folder indicated."
"  Optionally sets <varname> to the number of messages in that folder."
"  Assumes folder of INBOX if not specified.\n";

static char *app_hasnewvoicemail = "HasNewVoicemail";
static char *hasnewvoicemail_synopsis = "Conditionally branches to priority + 101";
static char *hasnewvoicemail_descrip =
"HasNewVoicemail(vmbox[/folder][@context][|varname])\n"
"  Branches to priority + 101, if there is voicemail in folder 'folder' or INBOX.\n"
"if folder is not specified. Optionally sets <varname> to the number of messages\n" 
"in that folder.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int hasvoicemail_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char vmpath[256], *temps, *input, *varname = NULL, *vmbox, *context = "default";
	char *vmfolder;
	DIR *vmdir;
	struct dirent *vment;
	int vmcount = 0;

	if (!data) {
		ast_log(LOG_WARNING, "HasVoicemail requires an argument (vm-box[@context][:folder]|varname)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);

	input = ast_strdupa((char *)data);
	if (input) {
		temps = input;
		if ((temps = strsep(&input, "|"))) {
			if (input && !ast_strlen_zero(input))
				varname = input;
			input = temps;
		}
		if ((temps = strsep(&input, ":"))) {
			if (input && !ast_strlen_zero(input))
				vmfolder = input;
			input = temps;
		}
		if ((vmbox = strsep(&input, "@")))
			if (input && !ast_strlen_zero(input))
				context = input;
		if (!vmbox)
			vmbox = input;
		vmfolder = strchr(vmbox, '/');
		if (vmfolder) {
			*vmfolder = '\0';
			vmfolder++;
		} else
			vmfolder = "INBOX";
		snprintf(vmpath,sizeof(vmpath), "%s/voicemail/%s/%s/%s", (char *)ast_config_AST_SPOOL_DIR, context, vmbox, vmfolder);
		if (!(vmdir = opendir(vmpath))) {
			ast_log(LOG_NOTICE, "Voice mailbox %s at %s does not exist\n", vmbox, vmpath);
		} else {

			/* No matter what the format of VM, there will always be a .txt file for each message. */
			while ((vment = readdir(vmdir)))
				if (!strncmp(vment->d_name + 7,".txt",4))
					vmcount++;
			closedir(vmdir);
		}
		/* Set the count in the channel variable */
		if (varname) {
			char tmp[12];
			snprintf(tmp, sizeof(tmp), "%d", vmcount);
			pbx_builtin_setvar_helper(chan, varname, tmp);
		}

		if (vmcount > 0) {
			/* Branch to the next extension */
			if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num)) {
				chan->priority += 100;
			} else
				ast_log(LOG_WARNING, "VM box %s@%s has new voicemail, but extension %s, priority %d doesn't exist\n", vmbox, context, chan->exten, chan->priority + 101);
		}
	} else {
		ast_log(LOG_ERROR, "Out of memory error\n");
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app_hasvoicemail);
	res |= ast_unregister_application(app_hasnewvoicemail);
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app_hasvoicemail, hasvoicemail_exec, hasvoicemail_synopsis, hasvoicemail_descrip);
	res |= ast_register_application(app_hasnewvoicemail, hasvoicemail_exec, hasnewvoicemail_synopsis, hasnewvoicemail_descrip);
	return res;
}

char *description(void)
{
	return tdesc;
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
