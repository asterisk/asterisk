/* -*- C -*-
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Implementation for stasis-http stubs.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "resource_endpoints.h"

void stasis_http_get_endpoint(struct ast_variable *headers, struct ast_get_endpoint_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_endpoint\n");
}
void stasis_http_get_endpoints(struct ast_variable *headers, struct ast_get_endpoints_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_endpoints\n");
}
