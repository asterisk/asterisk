/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 *
 * Made only slightly more sane by Mark Spencer <markster@digium.com>
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
 * \brief DISA -- Direct Inward System Access Application
 *
 * \author Jim Dixon <jim@lambdatel.com>
 * 
 * \ingroup applications
 */
 
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/ulaw.h"
#include "asterisk/callerid.h"

static char *tdesc = "DISA (Direct Inward System Access) Application";

static char *app = "DISA";

static char *synopsis = "DISA (Direct Inward System Access)";

static char *descrip = 
	"DISA(<numeric passcode>[|<context>]) or disa(<filename>)\n"
	"The DISA, Direct Inward System Access, application allows someone from \n"
	"outside the telephone switch (PBX) to obtain an \"internal\" system \n"
	"dialtone and to place calls from it as if they were placing a call from \n"
	"within the switch.\n"
	"DISA plays a dialtone. The user enters their numeric passcode, followed by\n"
	"the pound sign (#). If the passcode is correct, the user is then given\n"
	"system dialtone on which a call may be placed. Obviously, this type\n"
	"of access has SERIOUS security implications, and GREAT care must be\n"
	"taken NOT to compromise your security.\n\n"
	"There is a possibility of accessing DISA without password. Simply\n"
	"exchange your password with \"no-password\".\n\n"
	"    Example: exten => s,1,DISA(no-password|local)\n\n"
	"Be aware that using this compromises the security of your PBX.\n\n"
	"The arguments to this application (in extensions.conf) allow either\n"
	"specification of a single global passcode (that everyone uses), or\n"
	"individual passcodes contained in a file. It also allow specification\n"
	"of the context on which the user will be dialing. If no context is\n"
	"specified, the DISA application defaults the context to \"disa\".\n"
	"Presumably a normal system will have a special context set up\n"
	"for DISA use with some or a lot of restrictions. \n\n"
	"The file that contains the passcodes (if used) allows specification\n"
	"of either just a passcode (defaulting to the \"disa\" context, or\n"
	"passcode|context on each line of the file. The file may contain blank\n"
	"lines, or comments starting with \"#\" or \";\". In addition, the\n"
	"above arguments may have |new-callerid-string appended to them, to\n"
	"specify a new (different) callerid to be used for this call, for\n"
	"example: numeric-passcode|context|\"My Phone\" <(234) 123-4567> or \n"
	"full-pathname-of-passcode-file|\"My Phone\" <(234) 123-4567>.  Last\n"
	"but not least, |mailbox[@context] may be appended, which will cause\n"
	"a stutter-dialtone (indication \"dialrecall\") to be used, if the\n"
	"specified mailbox contains any new messages, for example:\n"
	"numeric-passcode|context||1234 (w/a changing callerid).  Note that\n"
	"in the case of specifying the numeric-passcode, the context must be\n"
	"specified if the callerid is specified also.\n\n"
	"If login is successful, the application looks up the dialed number in\n"
	"the specified (or default) context, and executes it if found.\n"
	"If the user enters an invalid extension and extension \"i\" (invalid) \n"
	"exists in the context, it will be used.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static void play_dialtone(struct ast_channel *chan, char *mailbox)
{
	const struct tone_zone_sound *ts = NULL;
	if(ast_app_has_voicemail(mailbox, NULL))
		ts = ast_get_indication_tone(chan->zone, "dialrecall");
	else
		ts = ast_get_indication_tone(chan->zone, "dial");
	if (ts)
		ast_playtones_start(chan, 0, ts->data, 0);
	else
		ast_tonepair_start(chan, 350, 440, 0, 0);
}

static int disa_exec(struct ast_channel *chan, void *data)
{
	int i,j,k,x,did_ignore;
	int firstdigittimeout = 20000;
	int digittimeout = 10000;
	struct localuser *u;
	char *tmp, arg2[256]="",exten[AST_MAX_EXTENSION],acctcode[20]="";
	char pwline[256];
	char *ourcontext,*ourcallerid,ourcidname[256],ourcidnum[256],*mailbox;
	struct ast_frame *f;
	struct timeval lastdigittime;
	int res;
	time_t rstart;
	FILE *fp;
	char *stringp=NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "disa requires an argument (passcode/passcode file)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);
	
	if (chan->pbx) {
		firstdigittimeout = chan->pbx->rtimeout*1000;
		digittimeout = chan->pbx->dtimeout*1000;
	}
	
	if (ast_set_write_format(chan,AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "Unable to set write format to Mu-law on %s\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	if (ast_set_read_format(chan,AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "Unable to set read format to Mu-law on %s\n",chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	
	ast_log(LOG_DEBUG, "Digittimeout: %d\n", digittimeout);
	ast_log(LOG_DEBUG, "Responsetimeout: %d\n", firstdigittimeout);

	tmp = ast_strdupa(data);
	if (!tmp) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}	

	stringp=tmp;
	strsep(&stringp, "|");
	ourcontext = strsep(&stringp, "|");
	/* if context specified, save 2nd arg and parse third */
	if (ourcontext) {
		ast_copy_string(arg2, ourcontext, sizeof(arg2));
		ourcallerid = strsep(&stringp,"|");
	}
	  /* if context not specified, use "disa" */
	else {
		arg2[0] = 0;
		ourcallerid = NULL;
		ourcontext = "disa";
	}
	mailbox = strsep(&stringp, "|");
	if (!mailbox)
		mailbox = "";
	ast_log(LOG_DEBUG, "Mailbox: %s\n",mailbox);
	
	if (chan->_state != AST_STATE_UP) {
		/* answer */
		ast_answer(chan);
	}
	i = k = x = 0; /* k is 0 for pswd entry, 1 for ext entry */
	did_ignore = 0;
	exten[0] = 0;
	acctcode[0] = 0;
	/* can we access DISA without password? */ 

	ast_log(LOG_DEBUG, "Context: %s\n",ourcontext);

	if (!strcasecmp(tmp, "no-password")) {
		k |= 1; /* We have the password */
		ast_log(LOG_DEBUG, "DISA no-password login success\n");
	}
	lastdigittime = ast_tvnow();

	play_dialtone(chan, mailbox);

	for (;;) {
		  /* if outa time, give em reorder */
		if (ast_tvdiff_ms(ast_tvnow(), lastdigittime) > 
		    ((k&2) ? digittimeout : firstdigittimeout))
		{
			ast_log(LOG_DEBUG,"DISA %s entry timeout on chan %s\n",
				((k&1) ? "extension" : "password"),chan->name);
			break;
		}
		if ((res = ast_waitfor(chan, -1) < 0)) {
			ast_log(LOG_DEBUG, "Waitfor returned %d\n", res);
			continue;
		}
			
		f = ast_read(chan);
		if (f == NULL) 
		{
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_HANGUP))
		{
			ast_frfree(f);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			ast_frfree(f);
			continue;
		}
		  /* if not DTMF, just do it again */
		if (f->frametype != AST_FRAME_DTMF) 
		{
			ast_frfree(f);
			continue;
		}

		j = f->subclass;  /* save digit */
		ast_frfree(f);
		if (i == 0) 
		{
			k|=2; /* We have the first digit */ 
			ast_playtones_stop(chan);
		}
		lastdigittime = ast_tvnow();
		  /* got a DTMF tone */
		if (i < AST_MAX_EXTENSION) /* if still valid number of digits */
		{
			if (!(k&1)) /* if in password state */
			{
				if (j == '#') /* end of password */
				{
					  /* see if this is an integer */
					if (sscanf(tmp,"%d",&j) < 1)
					   { /* nope, it must be a filename */
						fp = fopen(tmp,"r");
						if (!fp)
						   {
							ast_log(LOG_WARNING,"DISA password file %s not found on chan %s\n",tmp,chan->name);
							LOCAL_USER_REMOVE(u);
							return -1;
						   }
						pwline[0] = 0;
						while(fgets(pwline,sizeof(pwline) - 1,fp))
						   {
							char *stringp=NULL,*stringp2;
							if (!pwline[0]) continue;
							if (pwline[strlen(pwline) - 1] == '\n') 
								pwline[strlen(pwline) - 1] = 0;
							if (!pwline[0]) continue;
							  /* skip comments */
							if (pwline[0] == '#') continue;
							if (pwline[0] == ';') continue;
							stringp=pwline;
							strsep(&stringp, "|");
							stringp2=strsep(&stringp, "|");
							if (stringp2) {
								ourcontext=stringp2;
								stringp2=strsep(&stringp, "|");
								if (stringp2) ourcallerid=stringp2;
							}
							mailbox = strsep(&stringp, "|");
							if (!mailbox)
								mailbox = "";
							ast_log(LOG_DEBUG, "Mailbox: %s\n",mailbox);

							  /* password must be in valid format (numeric) */
							if (sscanf(pwline,"%d",&j) < 1) continue;
							  /* if we got it */
							if (!strcmp(exten,pwline)) break;
						   }
						fclose(fp);
					   }
					  /* compare the two */
					if (strcmp(exten,pwline))
					{
						ast_log(LOG_WARNING,"DISA on chan %s got bad password %s\n",chan->name,exten);
						goto reorder;

					}
					 /* password good, set to dial state */
					ast_log(LOG_DEBUG,"DISA on chan %s password is good\n",chan->name);
					play_dialtone(chan, mailbox);

					k|=1; /* In number mode */
					i = 0;  /* re-set buffer pointer */
					exten[sizeof(acctcode)] = 0;
					ast_copy_string(acctcode, exten, sizeof(acctcode));
					exten[0] = 0;
					ast_log(LOG_DEBUG,"Successful DISA log-in on chan %s\n",chan->name);
					continue;
				}
			}

			exten[i++] = j;  /* save digit */
			exten[i] = 0;
			if (!(k&1)) continue; /* if getting password, continue doing it */
			  /* if this exists */

			if (ast_ignore_pattern(ourcontext, exten)) {
				play_dialtone(chan, "");
				did_ignore = 1;
			} else
				if (did_ignore) {
					ast_playtones_stop(chan);
					did_ignore = 0;
				}

			  /* if can do some more, do it */
			if (!ast_matchmore_extension(chan,ourcontext,exten,1, chan->cid.cid_num)) {
				break;
			}
		}
	}

	if (k == 3) {
		int recheck = 0;
		struct ast_flags flags = { AST_CDR_FLAG_POSTED };

		if (!ast_exists_extension(chan, ourcontext, exten, 1, chan->cid.cid_num)) {
			pbx_builtin_setvar_helper(chan, "INVALID_EXTEN", exten);
			exten[0] = 'i';
			exten[1] = '\0';
			recheck = 1;
		}
		if (!recheck || ast_exists_extension(chan, ourcontext, exten, 1, chan->cid.cid_num)) {
			ast_playtones_stop(chan);
			/* We're authenticated and have a target extension */
			if (ourcallerid && *ourcallerid)
			{
				ast_callerid_split(ourcallerid, ourcidname, sizeof(ourcidname), ourcidnum, sizeof(ourcidnum));
				ast_set_callerid(chan, ourcidnum, ourcidname, ourcidnum);
			}

			if (!ast_strlen_zero(acctcode))
				ast_copy_string(chan->accountcode, acctcode, sizeof(chan->accountcode));

			ast_cdr_reset(chan->cdr, &flags);
			ast_explicit_goto(chan, ourcontext, exten, 1);
			LOCAL_USER_REMOVE(u);
			return 0;
		}
	}

	/* Received invalid, but no "i" extension exists in the given context */

reorder:

	ast_indicate(chan,AST_CONTROL_CONGESTION);
	/* something is invalid, give em reorder for several seconds */
	time(&rstart);
	while(time(NULL) < rstart + 10)
	{
		if (ast_waitfor(chan, -1) < 0)
			break;
		f = ast_read(chan);
		if (!f)
			break;
		ast_frfree(f);
	}
	ast_playtones_stop(chan);
	LOCAL_USER_REMOVE(u);
	return -1;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return ast_register_application(app, disa_exec, synopsis, descrip);
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

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
