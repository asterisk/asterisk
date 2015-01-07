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
	<defaultenabled>no</defaultenabled>
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
			DEPRECATED: Used to set whether an audiohook may be inherited to another
			channel. Due to architectural changes in Asterisk 12, audiohook inheritance
			is performed automatically and this function now lacks function.
		</synopsis>
		<description>
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
AST_MODULE_INFO_STANDARD_DEPRECATED(ASTERISK_GPL_KEY, "Audiohook inheritance placeholder function");

