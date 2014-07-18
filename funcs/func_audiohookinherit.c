/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 *
 * Please follow coding guidelines
 * http://svn.digium.com/view/asterisk/trunk/doc/CODING-GUIDELINES
 */

/*! \file
 *
 * \brief Audiohook inheritance function
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>deprecated</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

/*** DOCUMENTATION
	<function name = "AUDIOHOOK_INHERIT" language="en_US">
		<synopsis>
			Set whether an audiohook may be inherited to another channel
		</synopsis>
		<syntax>
			<parameter name="source" required="true">
				<para>The built-in sources in Asterisk are</para>
				<enumlist>
					<enum name="MixMonitor" />
					<enum name="Chanspy" />
					<enum name="Volume" />
					<enum name="Speex" />
					<enum name="pitch_shift" />
					<enum name="JACK_HOOK" />
					<enum name="Mute" />
				</enumlist>
				<para>Note that the names are not case-sensitive</para>
			</parameter>
		</syntax>
		<description>
			<para>By enabling audiohook inheritance on the channel, you are giving
			permission for an audiohook to be inherited by a descendent channel.
			Inheritance may be be disabled at any point as well.</para>

			<para>Example scenario:</para>
			<para>exten => 2000,1,MixMonitor(blah.wav)</para>
			<para>exten => 2000,n,Set(AUDIOHOOK_INHERIT(MixMonitor)=yes)</para>
			<para>exten => 2000,n,Dial(SIP/2000)</para>
			<para>
			</para>
			<para>exten => 4000,1,Dial(SIP/4000)</para>
			<para>
			</para>
			<para>exten => 5000,1,MixMonitor(blah2.wav)</para>
			<para>exten => 5000,n,Dial(SIP/5000)</para>
			<para>
			</para>
			<para>In this basic dialplan scenario, let's consider the following sample calls</para>
			<para>Call 1: Caller dials 2000. The person who answers then executes an attended</para>
			<para>        transfer to 4000.</para>
			<para>Result: Since extension 2000 set MixMonitor to be inheritable, after the</para>
			<para>        transfer to 4000 has completed, the call will continue to be recorded
			to blah.wav</para>
			<para>
			</para>
			<para>Call 2: Caller dials 5000. The person who answers then executes an attended</para>
			<para>        transfer to 4000.</para>
			<para>Result: Since extension 5000 did not set MixMonitor to be inheritable, the</para>
			<para>        recording will stop once the call has been transferred to 4000.</para>
			<para>Prior to Asterisk 12, masquerades would occur under all sorts of
			situations which were hard to predict.  In Asterisk 12, masquerades only
			occur as a result of a small set of operations for which inheriting all
			audiohooks from the original channel is now safe.  So in Asterisk 12.5+,
			all audiohooks are inherited without needing other controls expressing
			which audiohooks should be inherited under which conditions.</para>
		</description>
	</function>
 ***/

static int func_inheritance_write(struct ast_channel *chan, const char *function, char *data, const char *value)
{
	static int warned = 0;

	if (!warned) {
		ast_log(LOG_NOTICE, "AUDIOHOOK_INHERIT is deprecated and now does nothing.\n");
		warned++;
	}

	return 0;
}

static struct ast_custom_function inheritance_function = {
	.name = "AUDIOHOOK_INHERIT",
	.write = func_inheritance_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&inheritance_function);
}

static int load_module(void)
{
	if (ast_custom_function_register(&inheritance_function)) {
		return AST_MODULE_LOAD_DECLINE;
	} else {
		return AST_MODULE_LOAD_SUCCESS;
	}
}
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Audiohook inheritance placeholder function");
