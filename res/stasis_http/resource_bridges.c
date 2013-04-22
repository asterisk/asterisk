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

#include "resource_bridges.h"

void stasis_http_add_channel_to_bridge(struct ast_variable *headers, struct ast_add_channel_to_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_add_channel_to_bridge\n");
}
void stasis_http_remove_channel_from_bridge(struct ast_variable *headers, struct ast_remove_channel_from_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_remove_channel_from_bridge\n");
}
void stasis_http_record_bridge(struct ast_variable *headers, struct ast_record_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_record_bridge\n");
}
void stasis_http_get_bridge(struct ast_variable *headers, struct ast_get_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_bridge\n");
}
void stasis_http_delete_bridge(struct ast_variable *headers, struct ast_delete_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_delete_bridge\n");
}
void stasis_http_get_bridges(struct ast_variable *headers, struct ast_get_bridges_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_bridges\n");
}
void stasis_http_new_bridge(struct ast_variable *headers, struct ast_new_bridge_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_new_bridge\n");
}
