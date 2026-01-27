/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
 * \author George Joseph <gjoseph@sangoma.com>
 *
 * \brief CDR/CEL field registry
 *
 */

#include "cdrel.h"

#include "asterisk/json.h"
#include "asterisk/cdr.h"
#include "asterisk/cel.h"

/*!
 * \internal
 * \brief Helper macro that populates the static array of cdrel_fields.
 *
 * \param _record_type The type of record: CDR or CEL.
 * \param _field_id For CEL, it's the event field.  For CDR it's one of cdr_field_id.
 * \param _name The field name.
 * \param _input_type The input data type.  Drives the getters.
 * \param output_types An array of types, one each for dsv, json and sql.  Drives the formatters.
 * \param _mallocd Not used.
 */
#define REGISTER_FIELD(_record_type, _field_id, _name, _input_type, _output_type) \
	{ _record_type, _field_id, _name, _input_type, _output_type, { 0 } }

static const struct cdrel_field cdrel_field_registry[] = {
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_EVENT_ENUM, "eventenum", cdrel_type_event_enum, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_EVENT_TYPE, "eventtype", cdrel_type_event_type, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_EVENT_TIME, "eventtime", cdrel_type_timeval, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_EVENT_TIME_USEC, "eventtimeusec", cdrel_type_uint32, cdrel_type_uint32),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_USEREVENT_NAME, "usereventname", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_USEREVENT_NAME, "userdeftype", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CIDNAME, "name", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CIDNUM, "num", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_EXTEN, "exten", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CONTEXT, "context", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CHANNAME, "channame", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_APPNAME, "appname", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_APPDATA, "appdata", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_AMAFLAGS, "amaflags", cdrel_type_uint32, cdrel_type_uint32),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_ACCTCODE, "accountcode", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_UNIQUEID, "uniqueid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_USERFIELD, "userfield", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CIDANI, "ani", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CIDRDNIS, "rdnis", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_CIDDNID, "dnid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_PEER, "peer", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_PEER, "bridgepeer", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_LINKEDID, "linkedid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_PEERACCT, "peeraccount", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_EXTRA, "eventextra", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_TENANTID, "tenantid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cel, AST_EVENT_IE_CEL_LITERAL, "literal", cdrel_type_literal, cdrel_type_string),

	REGISTER_FIELD(cdrel_record_cdr, cdr_field_clid, "clid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_src, "src", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_dst, "dst", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_dcontext, "dcontext", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_channel, "channel", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_dstchannel, "dstchannel", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_lastapp, "lastapp", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_lastdata, "lastdata", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_start, "start", cdrel_type_timeval, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_answer, "answer", cdrel_type_timeval, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_end, "end", cdrel_type_timeval, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_duration, "duration", cdrel_type_int64, cdrel_type_int64),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_billsec, "billsec", cdrel_type_int64, cdrel_type_int64),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_disposition, "disposition", cdrel_type_int64, cdrel_type_disposition),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_amaflags, "amaflags", cdrel_type_int64, cdrel_type_amaflags),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_accountcode, "accountcode", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_peeraccount, "peeraccount", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_flags, "flags", cdrel_type_uint32, cdrel_type_uint32),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_uniqueid, "uniqueid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_linkedid, "linkedid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_tenantid, "tenantid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_peertenantid, "peertenantid", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_userfield, "userfield", cdrel_type_string, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_sequence, "sequence", cdrel_type_int32, cdrel_type_int32),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_varshead, "uservar", cdrel_type_uservar, cdrel_type_string),
	REGISTER_FIELD(cdrel_record_cdr, cdr_field_literal, "literal", cdrel_type_literal, cdrel_type_string),
};

/*
* \internal
* \brief Get a cdrel_field structure by record type and field name.
*
* \param record_type The cdrel_record_type to search.
* \param name The field name to search for.
* \returns A pointer to a constant cdrel_field structure or NULL if not found.
*          This pointer must never be freed.
*/
const struct cdrel_field *get_registered_field_by_name(enum cdrel_record_type type, const char *name)
{
	int ix = 0;

	for (ix = 0; ix < ARRAY_LEN(cdrel_field_registry); ix++) {
		if (cdrel_field_registry[ix].record_type == type && strcasecmp(cdrel_field_registry[ix].name, name) == 0) {
			return &cdrel_field_registry[ix];
		}
	}
	return NULL;
}
