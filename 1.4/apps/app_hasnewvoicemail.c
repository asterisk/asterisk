/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Changes Copyright (c) 2004 - 2006 Todd Freeman <freeman@andrews.edu>
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
 * \author Todd Freeman <freeman@andrews.edu>
 *
 * \note 95% based on HasNewVoicemail by
 * Tilghman Lesher <asterisk-hasnewvoicemail-app@the-tilghman.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

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
"		<# of messages in the folder, 0 for NONE>\n"
"\nThis application has been deprecated in favor of the VMCOUNT() function\n";

static char *app_hasnewvoicemail = "HasNewVoicemail";
static char *hasnewvoicemail_synopsis = "Conditionally branches to priority + 101 with the right options set";
static char *hasnewvoicemail_descrip =
"HasNewVoicemail(vmbox[/folder][@context][|varname[|options]])\n"
"Assumes folder 'INBOX' if folder is not specified. Optionally sets <varname> to the number of messages\n" 
"in that folder.\n"
"  The option string may contain zero of the following character:\n"
"	'j' -- jump to priority n+101, if there is new voicemail in folder 'folder' or INBOX\n"
"  This application sets the following channel variable upon completion:\n"
"	HASVMSTATUS		The result of the new voicemail check returned as a text string as follows\n"
"		<# of messages in the folder, 0 for NONE>\n"
"\nThis application has been deprecated in favor of the VMCOUNT() function\n";


static int hasvoicemail_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
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

	u = ast_module_user_add(chan);

	input = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, input);

	vmbox = strsep(&args.vmbox, "@");

	if (!ast_strlen_zero(args.vmbox))
		context = args.vmbox;

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

	vmcount = ast_app_messagecount(context, vmbox, vmfolder);
	/* Set the count in the channel variable */
	if (varname) {
		snprintf(tmp, sizeof(tmp), "%d", vmcount);
		pbx_builtin_setvar_helper(chan, varname, tmp);
	}

	if (vmcount > 0) {
		/* Branch to the next extension */
		if (priority_jump || ast_opt_priority_jumping) {
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) 
				ast_log(LOG_WARNING, "VM box %s@%s has new voicemail, but extension %s, priority %d doesn't exist\n", vmbox, context, chan->exten, chan->priority + 101);
		}
	}

	snprintf(tmp, sizeof(tmp), "%d", vmcount);
	pbx_builtin_setvar_helper(chan, "HASVMSTATUS", tmp);
	
	ast_module_user_remove(u);

	return 0;
}

static int acf_vmcount_exec(struct ast_channel *chan, char *cmd, char *argsstr, char *buf, size_t len)
{
	struct ast_module_user *u;
	char *context;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vmbox);
		AST_APP_ARG(folder);
	);

	if (ast_strlen_zero(argsstr))
		return -1;

	u = ast_module_user_add(chan);

	buf[0] = '\0';

	AST_STANDARD_APP_ARGS(args, argsstr);

	if (strchr(args.vmbox, '@')) {
		context = args.vmbox;
		args.vmbox = strsep(&context, "@");
	} else {
		context = "default";
	}

	if (ast_strlen_zero(args.folder)) {
		args.folder = "INBOX";
	}

	snprintf(buf, len, "%d", ast_app_messagecount(context, args.vmbox, args.folder));

	ast_module_user_remove(u);
	
	return 0;
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

static int unload_module(void)
{
	int res;
	
	res = ast_custom_function_unregister(&acf_vmcount);
	res |= ast_unregister_application(app_hasvoicemail);
	res |= ast_unregister_application(app_hasnewvoicemail);
	
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_custom_function_register(&acf_vmcount);
	res |= ast_register_application(app_hasvoicemail, hasvoicemail_exec, hasvoicemail_synopsis, hasvoicemail_descrip);
	res |= ast_register_application(app_hasnewvoicemail, hasvoicemail_exec, hasnewvoicemail_synopsis, hasnewvoicemail_descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Indicator for whether a voice mailbox has messages in a given folder.");
