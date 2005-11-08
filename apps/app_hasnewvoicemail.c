/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Changes Copyright (c) 2004 - 2005 Todd Freeman <freeman@andrews.edu>
 * 
 * 95% based on HasNewVoicemail by:
 * 
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
 * 
 * Tilghman Lesher <asterisk-hasnewvoicemail-app@the-tilghman.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief HasVoicemail application
 *
 * \ingroup applications
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

static char *tdesc = "Indicator for whether a voice mailbox has messages in a given folder.";
static char *app_hasvoicemail = "HasVoicemail";
static char *hasvoicemail_synopsis = "Conditionally branches to priority + 101 with the right options set";
static char *hasvoicemail_descrip =
"HasVoicemail(vmbox[/folder][@context][|varname[|options]])\n"
"  Optionally sets <varname> to the number of messages in that folder."
"  Assumes folder of INBOX if not specified.\n"
"  The option string may contain zero or the following character:\n"
"	'j' -- jump to priority n+101, if there is voicemail in the folder indicated.\n"
"  This application sets the following channel variable upon completion:\n"
"	HASVMSTATUS		The result of the voicemail check returned as a text string as follows\n"
"		<# of messages in the folder, 0 for NONE>\n";

static char *app_hasnewvoicemail = "HasNewVoicemail";
static char *hasnewvoicemail_synopsis = "Conditionally branches to priority + 101 with the right options set";
static char *hasnewvoicemail_descrip =
"HasNewVoicemail(vmbox[/folder][@context][|varname[|options]])\n"
"Assumes folder 'INBOX' if folder is not specified. Optionally sets <varname> to the number of messages\n" 
"in that folder.\n"
"  The option string may contain zero of the following character:\n"
"	'j' -- jump to priority n+101, if there is new voicemail in tolder 'folder' or INBOX\n"
"  This application sets the following channel variable upon completion:\n"
"	HASVMSTATUS		The result of the new voicemail check returned as a text string as follows\n"
"		<# of messages in the folder, 0 for NONE>\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int hasvoicemail_internal(char *context, char *box, char *folder)
{
	char vmpath[256];
	DIR *vmdir;
	struct dirent *vment;
	int count=0;

	snprintf(vmpath,sizeof(vmpath), "%s/voicemail/%s/%s/%s", (char *)ast_config_AST_SPOOL_DIR, context, box, folder);
	if ((vmdir = opendir(vmpath))) {
		/* No matter what the format of VM, there will always be a .txt file for each message. */
		while ((vment = readdir(vmdir))) {
			if (!strncmp(vment->d_name + 7, ".txt", 4)) {
				count++;
				break;
			}
		}
		closedir(vmdir);
	}
	return count;
}

static int hasvoicemail_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	char *input, *varname = NULL, *vmbox, *context = "default";
	char *vmfolder;
	int vmcount = 0;
	static int dep_warning = 0;
	int priority_jump = 0;
	char tmp[12];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vmbox);
		AST_APP_ARG(varname);
		AST_APP_ARG(options);
	);

	if (!dep_warning) {
		ast_log(LOG_WARNING, "The applications HasVoicemail and HasNewVoicemail have been deprecated.  Please use the VMCOUNT() function instead.\n");
		dep_warning = 1;
	}
	
	if (!data) {
		ast_log(LOG_WARNING, "HasVoicemail requires an argument (vm-box[/folder][@context][|varname[|options]])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	input = ast_strdupa((char *)data);
	if (! input) {
		ast_log(LOG_ERROR, "Out of memory error\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, input);

	if ((vmbox = strsep(&args.vmbox, "@")))
		if (!ast_strlen_zero(args.vmbox))
			context = args.vmbox;
	if (!vmbox)
		vmbox = args.vmbox;

	vmfolder = strchr(vmbox, '/');
	if (vmfolder) {
		*vmfolder = '\0';
		vmfolder++;
	} else {
		vmfolder = "INBOX";
	}

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	vmcount = hasvoicemail_internal(context, vmbox, vmfolder);
	/* Set the count in the channel variable */
	if (varname) {
		snprintf(tmp, sizeof(tmp), "%d", vmcount);
		pbx_builtin_setvar_helper(chan, varname, tmp);
	}

	if (vmcount > 0) {
		/* Branch to the next extension */
		if (priority_jump || option_priority_jumping) {
			if (!ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) 
				ast_log(LOG_WARNING, "VM box %s@%s has new voicemail, but extension %s, priority %d doesn't exist\n", vmbox, context, chan->exten, chan->priority + 101);
		}
	}

	snprintf(tmp, sizeof(tmp), "%d", vmcount);
	pbx_builtin_setvar_helper(chan, "HASVMSTATUS", tmp);
	
	LOCAL_USER_REMOVE(u);

	return 0;
}

static char *acf_vmcount_exec(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct localuser *u;
	char *args, *context, *box, *folder;

	LOCAL_USER_ACF_ADD(u);

	buf[0] = '\0';

	args = ast_strdupa(data);
	if (!args) {
		ast_log(LOG_ERROR, "Out of memory");
		LOCAL_USER_REMOVE(u);
		return buf;
	}

	box = strsep(&args, "|");
	if (strchr(box, '@')) {
		context = box;
		box = strsep(&context, "@");
	} else {
		context = "default";
	}

	if (args) {
		folder = args;
	} else {
		folder = "INBOX";
	}

	snprintf(buf, len, "%d", hasvoicemail_internal(context, box, folder));

	LOCAL_USER_REMOVE(u);
	
	return buf;
}

struct ast_custom_function acf_vmcount = {
	.name = "VMCOUNT",
	.synopsis = "Counts the voicemail in a specified mailbox",
	.syntax = "VMCOUNT(vmbox[@context][|folder])",
	.desc =
	"  context - defaults to \"default\"\n"
	"  folder  - defaults to \"INBOX\"\n",
	.read = acf_vmcount_exec,
};

int unload_module(void)
{
	int res;
	
	res = ast_custom_function_unregister(&acf_vmcount);
	res |= ast_unregister_application(app_hasvoicemail);
	res |= ast_unregister_application(app_hasnewvoicemail);
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_custom_function_register(&acf_vmcount);
	res |= ast_register_application(app_hasvoicemail, hasvoicemail_exec, hasvoicemail_synopsis, hasvoicemail_descrip);
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
