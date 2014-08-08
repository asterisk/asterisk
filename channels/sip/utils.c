/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
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

/*!
 * \file
 * \brief Utility functions for chan_sip
 *
 * \author Terry Wilson <twilson@digium.com>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "include/sip.h"
#include "include/sip_utils.h"

const char *force_rport_string(struct ast_flags *flags)
{
	if (ast_test_flag(&flags[2], SIP_PAGE3_NAT_AUTO_RPORT)) {
		return ast_test_flag(&flags[0], SIP_NAT_FORCE_RPORT) ? "Auto (Yes)" : "Auto (No)";
	}
	return AST_CLI_YESNO(ast_test_flag(&flags[0], SIP_NAT_FORCE_RPORT));
}

const char *comedia_string(struct ast_flags *flags)
{
	if (ast_test_flag(&flags[2], SIP_PAGE3_NAT_AUTO_COMEDIA)) {
		return ast_test_flag(&flags[1], SIP_PAGE2_SYMMETRICRTP) ? "Auto (Yes)" : "Auto (No)";
	}
	return AST_CLI_YESNO(ast_test_flag(&flags[1], SIP_PAGE2_SYMMETRICRTP));
}
