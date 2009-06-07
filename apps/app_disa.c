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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>
#include <sys/time.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/ulaw.h"
#include "asterisk/callerid.h"
#include "asterisk/stringfields.h"

/*** DOCUMENTATION
	<application name="DISA" language="en_US">
		<synopsis>
			Direct Inward System Access.
		</synopsis>
		<syntax>
			<parameter name="passcode|filename" required="true">
				<para>If you need to present a DISA dialtone without entering a password,
				simply set <replaceable>passcode</replaceable> to <literal>no-password</literal></para>
				<para>You may specified a <replaceable>filename</replaceable> instead of a
				<replaceable>passcode</replaceable>, this filename must contain individual passcodes</para>
			</parameter>
			<parameter name="context">
				<para>Specifies the dialplan context in which the user-entered extension
				will be matched. If no context is specified, the DISA application defaults
				to the <literal>disa</literal> context. Presumably a normal system will have a special
				context set up for DISA use with some or a lot of restrictions.</para>
			</parameter>
			<parameter name="cid">
				<para>Specifies a new (different) callerid to be used for this call.</para>
			</parameter>
			<parameter name="mailbox" argsep="@">
				<para>Will cause a stutter-dialtone (indication <emphasis>dialrecall</emphasis>)
				to be used, if the specified mailbox contains any new messages.</para>
				<argument name="mailbox" required="true" />
				<argument name="context" required="false" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="n">
						<para>The DISA application will not answer initially.</para>
					</option>
					<option name="p">
						<para>The extension entered will be considered complete when a <literal>#</literal>
						is entered.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>The DISA, Direct Inward System Access, application allows someone from
			outside the telephone switch (PBX) to obtain an <emphasis>internal</emphasis> system
			dialtone and to place calls from it as if they were placing a call from
			within the switch.
			DISA plays a dialtone. The user enters their numeric passcode, followed by
			the pound sign <literal>#</literal>. If the passcode is correct, the user is then given
			system dialtone within <replaceable>context</replaceable> on which a call may be placed.
			If the user enters an invalid extension and extension <literal>i</literal> exists in the specified
			<replaceable>context</replaceable>, it will be used.
			</para>
			<para>Be aware that using this may compromise the security of your PBX.</para>
			<para>The arguments to this application (in <filename>extensions.conf</filename>) allow either
			specification of a single global <replaceable>passcode</replaceable> (that everyone uses), or
			individual passcodes contained in a file (<replaceable>filename</replaceable>).</para>
			<para>The file that contains the passcodes (if used) allows a complete
			specification of all of the same arguments available on the command
			line, with the sole exception of the options. The file may contain blank
			lines, or comments starting with <literal>#</literal> or <literal>;</literal>.</para>
		</description>
		<see-also>
			<ref type="application">Authenticate</ref>
			<ref type="application">VMAuthenticate</ref>
		</see-also>
	</application>
 ***/
static const char app[] = "DISA";

enum {
	NOANSWER_FLAG = (1 << 0),
	POUND_TO_END_FLAG = (1 << 1),
};

AST_APP_OPTIONS(app_opts, {
	AST_APP_OPTION('n', NOANSWER_FLAG),
	AST_APP_OPTION('p', POUND_TO_END_FLAG),
});

static void play_dialtone(struct ast_channel *chan, char *mailbox)
{
	struct ast_tone_zone_sound *ts = NULL;

	if (ast_app_has_voicemail(mailbox, NULL)) {
		ts = ast_get_indication_tone(chan->zone, "dialrecall");
	} else {
		ts = ast_get_indication_tone(chan->zone, "dial");
	}

	if (ts) {
		ast_playtones_start(chan, 0, ts->data, 0);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		ast_tonepair_start(chan, 350, 440, 0, 0);
	}
}

static int disa_exec(struct ast_channel *chan, const char *data)
{
	int i = 0, j, k = 0, did_ignore = 0, special_noanswer = 0;
	int firstdigittimeout = (chan->pbx ? chan->pbx->rtimeoutms : 20000);
	int digittimeout = (chan->pbx ? chan->pbx->dtimeoutms : 10000);
	struct ast_flags flags;
	char *tmp, exten[AST_MAX_EXTENSION] = "", acctcode[20]="";
	char pwline[256];
	char ourcidname[256],ourcidnum[256];
	struct ast_frame *f;
	struct timeval lastdigittime;
	int res;
	FILE *fp;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(passcode);
		AST_APP_ARG(context);
		AST_APP_ARG(cid);
		AST_APP_ARG(mailbox);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "DISA requires an argument (passcode/passcode file)\n");
		return -1;
	}

	ast_debug(1, "Digittimeout: %d\n", digittimeout);
	ast_debug(1, "Responsetimeout: %d\n", firstdigittimeout);

	tmp = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, tmp);

	if (ast_strlen_zero(args.context))
		args.context = "disa";
	if (ast_strlen_zero(args.mailbox))
		args.mailbox = "";
	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(app_opts, &flags, NULL, args.options);

	ast_debug(1, "Mailbox: %s\n",args.mailbox);

	if (!ast_test_flag(&flags, NOANSWER_FLAG)) {
		if (chan->_state != AST_STATE_UP) {
			/* answer */
			ast_answer(chan);
		}
	} else
		special_noanswer = 1;

	ast_debug(1, "Context: %s\n",args.context);

	if (!strcasecmp(args.passcode, "no-password")) {
		k |= 1; /* We have the password */
		ast_debug(1, "DISA no-password login success\n");
	}

	lastdigittime = ast_tvnow();

	play_dialtone(chan, args.mailbox);

	ast_set_flag(chan, AST_FLAG_END_DTMF_ONLY);

	for (;;) {
		  /* if outa time, give em reorder */
		if (ast_tvdiff_ms(ast_tvnow(), lastdigittime) > ((k&2) ? digittimeout : firstdigittimeout)) {
			ast_debug(1,"DISA %s entry timeout on chan %s\n",
				((k&1) ? "extension" : "password"),chan->name);
			break;
		}

		if ((res = ast_waitfor(chan, -1) < 0)) {
			ast_debug(1, "Waitfor returned %d\n", res);
			continue;
		}

		if (!(f = ast_read(chan))) {
			ast_clear_flag(chan, AST_FLAG_END_DTMF_ONLY);
			return -1;
		}

		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
			if (f->data.uint32)
				chan->hangupcause = f->data.uint32;
			ast_frfree(f);
			ast_clear_flag(chan, AST_FLAG_END_DTMF_ONLY);
			return -1;
		}

		/* If the frame coming in is not DTMF, just drop it and continue */
		if (f->frametype != AST_FRAME_DTMF) {
			ast_frfree(f);
			continue;
		}

		j = f->subclass;  /* save digit */
		ast_frfree(f);

		if (!i) {
			k |= 2; /* We have the first digit */
			ast_playtones_stop(chan);
		}

		lastdigittime = ast_tvnow();

		/* got a DTMF tone */
		if (i < AST_MAX_EXTENSION) { /* if still valid number of digits */
			if (!(k&1)) { /* if in password state */
				if (j == '#') { /* end of password */
					  /* see if this is an integer */
					if (sscanf(args.passcode,"%d",&j) < 1) { /* nope, it must be a filename */
						fp = fopen(args.passcode,"r");
						if (!fp) {
							ast_log(LOG_WARNING,"DISA password file %s not found on chan %s\n",args.passcode,chan->name);
							ast_clear_flag(chan, AST_FLAG_END_DTMF_ONLY);
							return -1;
						}
						pwline[0] = 0;
						while(fgets(pwline,sizeof(pwline) - 1,fp)) {
							if (!pwline[0])
								continue;
							if (pwline[strlen(pwline) - 1] == '\n')
								pwline[strlen(pwline) - 1] = 0;
							if (!pwline[0])
								continue;
							 /* skip comments */
							if (pwline[0] == '#')
								continue;
							if (pwline[0] == ';')
								continue;

							AST_STANDARD_APP_ARGS(args, pwline);

							ast_debug(1, "Mailbox: %s\n",args.mailbox);

							/* password must be in valid format (numeric) */
							if (sscanf(args.passcode,"%d", &j) < 1)
								continue;
							 /* if we got it */
							if (!strcmp(exten,args.passcode)) {
								if (ast_strlen_zero(args.context))
									args.context = "disa";
								if (ast_strlen_zero(args.mailbox))
									args.mailbox = "";
								break;
							}
						}
						fclose(fp);
					}
					/* compare the two */
					if (strcmp(exten,args.passcode)) {
						ast_log(LOG_WARNING,"DISA on chan %s got bad password %s\n",chan->name,exten);
						goto reorder;

					}
					 /* password good, set to dial state */
					ast_debug(1,"DISA on chan %s password is good\n",chan->name);
					play_dialtone(chan, args.mailbox);

					k|=1; /* In number mode */
					i = 0;  /* re-set buffer pointer */
					exten[sizeof(acctcode)] = 0;
					ast_copy_string(acctcode, exten, sizeof(acctcode));
					exten[0] = 0;
					ast_debug(1,"Successful DISA log-in on chan %s\n", chan->name);
					continue;
				}
			} else {
				if (j == '#') { /* end of extension .. maybe */
					if (i == 0 && 
							(ast_matchmore_extension(chan, args.context, "#", 1, chan->cid.cid_num) ||
							 ast_exists_extension(chan, args.context, "#", 1, chan->cid.cid_num)) ) {
						/* Let the # be the part of, or the entire extension */
					} else {
						break;
					}
				}
			}

			exten[i++] = j;  /* save digit */
			exten[i] = 0;
			if (!(k&1))
				continue; /* if getting password, continue doing it */
			/* if this exists */

			/* user wants end of number, remove # */
			if (ast_test_flag(&flags, POUND_TO_END_FLAG) && j == '#') {
				exten[--i] = 0;
				break;
			}

			if (ast_ignore_pattern(args.context, exten)) {
				play_dialtone(chan, "");
				did_ignore = 1;
			} else
				if (did_ignore) {
					ast_playtones_stop(chan);
					did_ignore = 0;
				}

			/* if can do some more, do it */
			if (!ast_matchmore_extension(chan,args.context,exten,1, chan->cid.cid_num)) {
				break;
			}
		}
	}

	ast_clear_flag(chan, AST_FLAG_END_DTMF_ONLY);

	if (k == 3) {
		int recheck = 0;
		struct ast_flags cdr_flags = { AST_CDR_FLAG_POSTED };

		if (!ast_exists_extension(chan, args.context, exten, 1, chan->cid.cid_num)) {
			pbx_builtin_setvar_helper(chan, "INVALID_EXTEN", exten);
			exten[0] = 'i';
			exten[1] = '\0';
			recheck = 1;
		}
		if (!recheck || ast_exists_extension(chan, args.context, exten, 1, chan->cid.cid_num)) {
			ast_playtones_stop(chan);
			/* We're authenticated and have a target extension */
			if (!ast_strlen_zero(args.cid)) {
				ast_callerid_split(args.cid, ourcidname, sizeof(ourcidname), ourcidnum, sizeof(ourcidnum));
				ast_set_callerid(chan, ourcidnum, ourcidname, ourcidnum);
			}

			if (!ast_strlen_zero(acctcode))
				ast_string_field_set(chan, accountcode, acctcode);

			if (special_noanswer) cdr_flags.flags = 0;
			ast_cdr_reset(chan->cdr, &cdr_flags);
			ast_explicit_goto(chan, args.context, exten, 1);
			return 0;
		}
	}

	/* Received invalid, but no "i" extension exists in the given context */

reorder:
	/* Play congestion for a bit */
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	ast_safe_sleep(chan, 10*1000);

	ast_playtones_stop(chan);

	return -1;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, disa_exec) ?
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "DISA (Direct Inward System Access) Application");
