/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
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
 * \brief Application to dump channel variables
 *
 * \author Anthony Minessale <anthmct@yahoo.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/translate.h"
#include "asterisk/bridge.h"

/*** DOCUMENTATION
	<application name="DumpChan" language="en_US">
		<synopsis>
			Dump Info About The Calling Channel.
		</synopsis>
		<syntax>
			<parameter name="level">
				<para>Minimum verbose level</para>
			</parameter>
		</syntax>
		<description>
			<para>Displays information on channel and listing of all channel
			variables. If <replaceable>level</replaceable> is specified, output is only
			displayed when the verbose level is currently set to that number
			or greater.</para>
		</description>
		<see-also>
			<ref type="application">NoOp</ref>
			<ref type="application">Verbose</ref>
		</see-also>
	</application>
 ***/

static const char app[] = "DumpChan";

static int serialize_showchan(struct ast_channel *c, char *buf, size_t size)
{
	long elapsed_seconds = 0;
	int hour = 0, min = 0, sec = 0;
	char nf[256];
	char cgrp[256];
	char pgrp[256];
	struct ast_str *write_transpath = ast_str_alloca(256);
	struct ast_str *read_transpath = ast_str_alloca(256);
	struct ast_bridge *bridge;

	memset(buf, 0, size);
	if (!c)
		return 0;

	elapsed_seconds = ast_channel_get_duration(c);
	hour = elapsed_seconds / 3600;
	min = (elapsed_seconds % 3600) / 60;
	sec = elapsed_seconds % 60;

	ast_channel_lock(c);
	bridge = ast_channel_get_bridge(c);
	ast_channel_unlock(c);

	snprintf(buf,size,
		"Name=               %s\n"
		"Type=               %s\n"
		"UniqueID=           %s\n"
		"LinkedID=           %s\n"
		"CallerIDNum=        %s\n"
		"CallerIDName=       %s\n"
		"ConnectedLineIDNum= %s\n"
		"ConnectedLineIDName=%s\n"
		"DNIDDigits=         %s\n"
		"RDNIS=              %s\n"
		"Parkinglot=         %s\n"
		"Language=           %s\n"
		"State=              %s (%d)\n"
		"Rings=              %d\n"
		"NativeFormat=       %s\n"
		"WriteFormat=        %s\n"
		"ReadFormat=         %s\n"
		"RawWriteFormat=     %s\n"
		"RawReadFormat=      %s\n"
		"WriteTranscode=     %s %s\n"
		"ReadTranscode=      %s %s\n"
		"1stFileDescriptor=  %d\n"
		"Framesin=           %d %s\n"
		"Framesout=          %d %s\n"
		"TimetoHangup=       %ld\n"
		"ElapsedTime=        %dh%dm%ds\n"
		"BridgeID=           %s\n"
		"Context=            %s\n"
		"Extension=          %s\n"
		"Priority=           %d\n"
		"CallGroup=          %s\n"
		"PickupGroup=        %s\n"
		"Application=        %s\n"
		"Data=               %s\n"
		"Blocking_in=        %s\n",
		ast_channel_name(c),
		ast_channel_tech(c)->type,
		ast_channel_uniqueid(c),
		ast_channel_linkedid(c),
		S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, "(N/A)"),
		S_COR(ast_channel_caller(c)->id.name.valid, ast_channel_caller(c)->id.name.str, "(N/A)"),
		S_COR(ast_channel_connected(c)->id.number.valid, ast_channel_connected(c)->id.number.str, "(N/A)"),
		S_COR(ast_channel_connected(c)->id.name.valid, ast_channel_connected(c)->id.name.str, "(N/A)"),
		S_OR(ast_channel_dialed(c)->number.str, "(N/A)"),
		S_COR(ast_channel_redirecting(c)->from.number.valid, ast_channel_redirecting(c)->from.number.str, "(N/A)"),
		ast_channel_parkinglot(c),
		ast_channel_language(c),
		ast_state2str(ast_channel_state(c)),
		ast_channel_state(c),
		ast_channel_rings(c),
		ast_getformatname_multiple(nf, sizeof(nf), ast_channel_nativeformats(c)),
		ast_getformatname(ast_channel_writeformat(c)),
		ast_getformatname(ast_channel_readformat(c)),
		ast_getformatname(ast_channel_rawwriteformat(c)),
		ast_getformatname(ast_channel_rawreadformat(c)),
		ast_channel_writetrans(c) ? "Yes" : "No",
		ast_translate_path_to_str(ast_channel_writetrans(c), &write_transpath),
		ast_channel_readtrans(c) ? "Yes" : "No",
		ast_translate_path_to_str(ast_channel_readtrans(c), &read_transpath),
		ast_channel_fd(c, 0),
		ast_channel_fin(c) & ~DEBUGCHAN_FLAG, (ast_channel_fin(c) & DEBUGCHAN_FLAG) ? " (DEBUGGED)" : "",
		ast_channel_fout(c) & ~DEBUGCHAN_FLAG, (ast_channel_fout(c) & DEBUGCHAN_FLAG) ? " (DEBUGGED)" : "",
		(long)ast_channel_whentohangup(c)->tv_sec,
		hour,
		min,
		sec,
		bridge ? bridge->uniqueid : "(Not bridged)",
		ast_channel_context(c),
		ast_channel_exten(c),
		ast_channel_priority(c),
		ast_print_group(cgrp, sizeof(cgrp), ast_channel_callgroup(c)),
		ast_print_group(pgrp, sizeof(pgrp), ast_channel_pickupgroup(c)),
		ast_channel_appl(c) ? ast_channel_appl(c) : "(N/A)",
		ast_channel_data(c) ? S_OR(ast_channel_data(c), "(Empty)") : "(None)",
		(ast_test_flag(ast_channel_flags(c), AST_FLAG_BLOCKING) ? ast_channel_blockproc(c) : "(Not Blocking)"));

	ao2_cleanup(bridge);
	return 0;
}

static int dumpchan_exec(struct ast_channel *chan, const char *data)
{
	struct ast_str *vars = ast_str_thread_get(&ast_str_thread_global_buf, 16);
	char info[2048];
	int level = 0;
	static char *line = "================================================================================";

	if (!ast_strlen_zero(data))
		level = atoi(data);

	serialize_showchan(chan, info, sizeof(info));
	pbx_builtin_serialize_variables(chan, &vars);
	ast_verb(level, "\n"
		 "Dumping Info For Channel: %s:\n"
		 "%s\n"
		 "Info:\n"
		 "%s\n"
		 "Variables:\n"
		 "%s%s\n", ast_channel_name(chan), line, info, ast_str_buffer(vars), line);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, dumpchan_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dump Info About The Calling Channel");
