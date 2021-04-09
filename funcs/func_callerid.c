/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999-2010, Digium, Inc.
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
 * \brief Party ID related dialplan functions (Caller-ID, Connected-line, Redirecting)
 *
 * \ingroup functions
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/callerid.h"

/*
 * The CALLERID(pres) datatype is shorthand for getting/setting the
 * combined value of name-pres and num-pres.  Some channel drivers
 * don't make a distinction, so it makes sense to only use one property
 * to get/set it.  The same applies to CONNECTEDLINE(pres),
 * REDIRECTING(orig-pres), REDIRECTING(from-pres) and REDIRECTING(to-pres).
 *
 * Do not document the CALLERID(ton) datatype.
 * It is an alias for num-plan.
 *
 * Do not document the CALLERID(ANI-subaddr-...) datatype.
 * This is not used.
 *
 * Do not document the CONNECTEDLINE(source) datatype.
 * It has turned out to not be needed.  The source value is really
 * only useful as a possible tracing aid.
 *
 * Do not document the CONNECTEDLINE(ton) datatype.
 * It is an alias for num-plan.
 *
 * Do not document the REDIRECTING(pres) datatype.
 * It has turned out that the from-pres and to-pres values must be kept
 * separate.  They represent two different parties and there is a case when
 * they are active at the same time.  The plain pres option will simply
 * live on as a historical relic.
 *
 * Do not document the REDIRECTING(orig-ton), REDIRECTING(from-ton),
 * or REDIRECTING(to-ton) datatypes.
 * They are aliases for orig-num-plan, from-num-plan, and to-num-plan
 * respectively.
 */
/*** DOCUMENTATION
	<function name="CALLERID" language="en_US">
		<synopsis>
			Gets or sets Caller*ID data on the channel.
		</synopsis>
		<syntax>
			<parameter name="datatype" required="true">
				<para>The allowable datatypes are:</para>
				<enumlist>
					<enum name = "all" />
					<enum name = "name" />
					<enum name = "name-valid" />
					<enum name = "name-charset" />
					<enum name = "name-pres" />
					<enum name = "num" />
					<enum name = "num-valid" />
					<enum name = "num-plan" />
					<enum name = "num-pres" />
					<enum name = "pres" />
					<enum name = "subaddr" />
					<enum name = "subaddr-valid" />
					<enum name = "subaddr-type" />
					<enum name = "subaddr-odd" />
					<enum name = "tag" />
					<enum name = "priv-all" />
					<enum name = "priv-name" />
					<enum name = "priv-name-valid" />
					<enum name = "priv-name-charset" />
					<enum name = "priv-name-pres" />
					<enum name = "priv-num" />
					<enum name = "priv-num-valid" />
					<enum name = "priv-num-plan" />
					<enum name = "priv-num-pres" />
					<enum name = "priv-subaddr" />
					<enum name = "priv-subaddr-valid" />
					<enum name = "priv-subaddr-type" />
					<enum name = "priv-subaddr-odd" />
					<enum name = "priv-tag" />
					<enum name = "ANI-all" />
					<enum name = "ANI-name" />
					<enum name = "ANI-name-valid" />
					<enum name = "ANI-name-charset" />
					<enum name = "ANI-name-pres" />
					<enum name = "ANI-num" />
					<enum name = "ANI-num-valid" />
					<enum name = "ANI-num-plan" />
					<enum name = "ANI-num-pres" />
					<enum name = "ANI-tag" />
					<enum name = "RDNIS" />
					<enum name = "DNID" />
					<enum name = "dnid-num-plan" />
					<enum name = "dnid-subaddr" />
					<enum name = "dnid-subaddr-valid" />
					<enum name = "dnid-subaddr-type" />
					<enum name = "dnid-subaddr-odd" />
				</enumlist>
			</parameter>
			<parameter name="CID">
				<para>Optional Caller*ID to parse instead of using the Caller*ID from the
				channel. This parameter is only optional when reading the Caller*ID.</para>
			</parameter>
		</syntax>
		<description>
			<para>Gets or sets Caller*ID data on the channel. Uses channel callerid by
			default or optional callerid, if specified.</para>
			<para>The <replaceable>pres</replaceable> field gets/sets a combined value
			for <replaceable>name-pres</replaceable> and
			<replaceable>num-pres</replaceable>.</para>
			<para>The allowable values for the <replaceable>name-charset</replaceable>
			field are the following:</para>
			<enumlist>
				<enum name = "unknown"><para>Unknown</para></enum>
				<enum name = "iso8859-1"><para>ISO8859-1</para></enum>
				<enum name = "withdrawn"><para>Withdrawn</para></enum>
				<enum name = "iso8859-2"><para>ISO8859-2</para></enum>
				<enum name = "iso8859-3"><para>ISO8859-3</para></enum>
				<enum name = "iso8859-4"><para>ISO8859-4</para></enum>
				<enum name = "iso8859-5"><para>ISO8859-5</para></enum>
				<enum name = "iso8859-7"><para>ISO8859-7</para></enum>
				<enum name = "bmp"><para>ISO10646 Bmp String</para></enum>
				<enum name = "utf8"><para>ISO10646 UTF-8 String</para></enum>
			</enumlist>
			<para>The allowable values for the <replaceable>num-pres</replaceable>,
			<replaceable>name-pres</replaceable>, and <replaceable>pres</replaceable>
			fields are the following:</para>
			<enumlist>
				<enum name="allowed_not_screened">
					<para>Presentation Allowed, Not Screened.</para>
				</enum>
				<enum name="allowed_passed_screen">
					<para>Presentation Allowed, Passed Screen.</para>
				</enum>
				<enum name="allowed_failed_screen">
					<para>Presentation Allowed, Failed Screen.</para>
				</enum>
				<enum name="allowed">
					<para>Presentation Allowed, Network Number.</para>
				</enum>
				<enum name="prohib_not_screened">
					<para>Presentation Prohibited, Not Screened.</para>
				</enum>
				<enum name="prohib_passed_screen">
					<para>Presentation Prohibited, Passed Screen.</para>
				</enum>
				<enum name="prohib_failed_screen">
					<para>Presentation Prohibited, Failed Screen.</para>
				</enum>
				<enum name="prohib">
					<para>Presentation Prohibited, Network Number.</para>
				</enum>
				<enum name="unavailable">
					<para>Number Unavailable.</para>
				</enum>
			</enumlist>
		</description>
	</function>
	<function name="CONNECTEDLINE" language="en_US">
		<synopsis>
			Gets or sets Connected Line data on the channel.
		</synopsis>
		<syntax>
			<parameter name="datatype" required="true">
				<para>The allowable datatypes are:</para>
				<enumlist>
					<enum name = "all" />
					<enum name = "name" />
					<enum name = "name-valid" />
					<enum name = "name-charset" />
					<enum name = "name-pres" />
					<enum name = "num" />
					<enum name = "num-valid" />
					<enum name = "num-plan" />
					<enum name = "num-pres" />
					<enum name = "pres" />
					<enum name = "subaddr" />
					<enum name = "subaddr-valid" />
					<enum name = "subaddr-type" />
					<enum name = "subaddr-odd" />
					<enum name = "tag" />
					<enum name = "priv-all" />
					<enum name = "priv-name" />
					<enum name = "priv-name-valid" />
					<enum name = "priv-name-charset" />
					<enum name = "priv-name-pres" />
					<enum name = "priv-num" />
					<enum name = "priv-num-valid" />
					<enum name = "priv-num-plan" />
					<enum name = "priv-num-pres" />
					<enum name = "priv-subaddr" />
					<enum name = "priv-subaddr-valid" />
					<enum name = "priv-subaddr-type" />
					<enum name = "priv-subaddr-odd" />
					<enum name = "priv-tag" />
				</enumlist>
			</parameter>
			<parameter name="i">
				<para>If set, this will prevent the channel from sending out protocol
				messages because of the value being set</para>
			</parameter>
		</syntax>
		<description>
			<para>Gets or sets Connected Line data on the channel.</para>
			<para>The <replaceable>pres</replaceable> field gets/sets a combined value
			for <replaceable>name-pres</replaceable> and
			<replaceable>num-pres</replaceable>.</para>
			<para>The allowable values for the <replaceable>name-charset</replaceable>
			field are the following:</para>
			<enumlist>
				<enum name = "unknown"><para>Unknown</para></enum>
				<enum name = "iso8859-1"><para>ISO8859-1</para></enum>
				<enum name = "withdrawn"><para>Withdrawn</para></enum>
				<enum name = "iso8859-2"><para>ISO8859-2</para></enum>
				<enum name = "iso8859-3"><para>ISO8859-3</para></enum>
				<enum name = "iso8859-4"><para>ISO8859-4</para></enum>
				<enum name = "iso8859-5"><para>ISO8859-5</para></enum>
				<enum name = "iso8859-7"><para>ISO8859-7</para></enum>
				<enum name = "bmp"><para>ISO10646 Bmp String</para></enum>
				<enum name = "utf8"><para>ISO10646 UTF-8 String</para></enum>
			</enumlist>
			<para>The allowable values for the <replaceable>num-pres</replaceable>,
			<replaceable>name-pres</replaceable>, and <replaceable>pres</replaceable>
			fields are the following:</para>
			<enumlist>
				<enum name="allowed_not_screened">
					<para>Presentation Allowed, Not Screened.</para>
				</enum>
				<enum name="allowed_passed_screen">
					<para>Presentation Allowed, Passed Screen.</para>
				</enum>
				<enum name="allowed_failed_screen">
					<para>Presentation Allowed, Failed Screen.</para>
				</enum>
				<enum name="allowed">
					<para>Presentation Allowed, Network Number.</para>
				</enum>
				<enum name="prohib_not_screened">
					<para>Presentation Prohibited, Not Screened.</para>
				</enum>
				<enum name="prohib_passed_screen">
					<para>Presentation Prohibited, Passed Screen.</para>
				</enum>
				<enum name="prohib_failed_screen">
					<para>Presentation Prohibited, Failed Screen.</para>
				</enum>
				<enum name="prohib">
					<para>Presentation Prohibited, Network Number.</para>
				</enum>
				<enum name="unavailable">
					<para>Number Unavailable.</para>
				</enum>
			</enumlist>
		</description>
	</function>
	<function name="REDIRECTING" language="en_US">
		<synopsis>
			Gets or sets Redirecting data on the channel.
		</synopsis>
		<syntax>
			<parameter name="datatype" required="true">
				<para>The allowable datatypes are:</para>
				<enumlist>
					<enum name = "orig-all" />
					<enum name = "orig-name" />
					<enum name = "orig-name-valid" />
					<enum name = "orig-name-charset" />
					<enum name = "orig-name-pres" />
					<enum name = "orig-num" />
					<enum name = "orig-num-valid" />
					<enum name = "orig-num-plan" />
					<enum name = "orig-num-pres" />
					<enum name = "orig-pres" />
					<enum name = "orig-subaddr" />
					<enum name = "orig-subaddr-valid" />
					<enum name = "orig-subaddr-type" />
					<enum name = "orig-subaddr-odd" />
					<enum name = "orig-tag" />
					<enum name = "orig-reason" />
					<enum name = "from-all" />
					<enum name = "from-name" />
					<enum name = "from-name-valid" />
					<enum name = "from-name-charset" />
					<enum name = "from-name-pres" />
					<enum name = "from-num" />
					<enum name = "from-num-valid" />
					<enum name = "from-num-plan" />
					<enum name = "from-num-pres" />
					<enum name = "from-pres" />
					<enum name = "from-subaddr" />
					<enum name = "from-subaddr-valid" />
					<enum name = "from-subaddr-type" />
					<enum name = "from-subaddr-odd" />
					<enum name = "from-tag" />
					<enum name = "to-all" />
					<enum name = "to-name" />
					<enum name = "to-name-valid" />
					<enum name = "to-name-charset" />
					<enum name = "to-name-pres" />
					<enum name = "to-num" />
					<enum name = "to-num-valid" />
					<enum name = "to-num-plan" />
					<enum name = "to-num-pres" />
					<enum name = "to-pres" />
					<enum name = "to-subaddr" />
					<enum name = "to-subaddr-valid" />
					<enum name = "to-subaddr-type" />
					<enum name = "to-subaddr-odd" />
					<enum name = "to-tag" />
					<enum name = "priv-orig-all" />
					<enum name = "priv-orig-name" />
					<enum name = "priv-orig-name-valid" />
					<enum name = "priv-orig-name-charset" />
					<enum name = "priv-orig-name-pres" />
					<enum name = "priv-orig-num" />
					<enum name = "priv-orig-num-valid" />
					<enum name = "priv-orig-num-plan" />
					<enum name = "priv-orig-num-pres" />
					<enum name = "priv-orig-subaddr" />
					<enum name = "priv-orig-subaddr-valid" />
					<enum name = "priv-orig-subaddr-type" />
					<enum name = "priv-orig-subaddr-odd" />
					<enum name = "priv-orig-tag" />
					<enum name = "priv-from-all" />
					<enum name = "priv-from-name" />
					<enum name = "priv-from-name-valid" />
					<enum name = "priv-from-name-charset" />
					<enum name = "priv-from-name-pres" />
					<enum name = "priv-from-num" />
					<enum name = "priv-from-num-valid" />
					<enum name = "priv-from-num-plan" />
					<enum name = "priv-from-num-pres" />
					<enum name = "priv-from-subaddr" />
					<enum name = "priv-from-subaddr-valid" />
					<enum name = "priv-from-subaddr-type" />
					<enum name = "priv-from-subaddr-odd" />
					<enum name = "priv-from-tag" />
					<enum name = "priv-to-all" />
					<enum name = "priv-to-name" />
					<enum name = "priv-to-name-valid" />
					<enum name = "priv-to-name-charset" />
					<enum name = "priv-to-name-pres" />
					<enum name = "priv-to-num" />
					<enum name = "priv-to-num-valid" />
					<enum name = "priv-to-num-plan" />
					<enum name = "priv-to-num-pres" />
					<enum name = "priv-to-subaddr" />
					<enum name = "priv-to-subaddr-valid" />
					<enum name = "priv-to-subaddr-type" />
					<enum name = "priv-to-subaddr-odd" />
					<enum name = "priv-to-tag" />
					<enum name = "reason" />
					<enum name = "count" />
				</enumlist>
			</parameter>
			<parameter name="i">
				<para>If set, this will prevent the channel from sending out protocol
				messages because of the value being set</para>
			</parameter>
		</syntax>
		<description>
			<para>Gets or sets Redirecting data on the channel.</para>
			<para>The <replaceable>orig-pres</replaceable>,
			<replaceable>from-pres</replaceable> and <replaceable>to-pres</replaceable>
			fields get/set a combined value for the corresponding
			<replaceable>...-name-pres</replaceable> and <replaceable>...-num-pres</replaceable>
			fields.</para>
			<para>The recognized values for the <replaceable>reason</replaceable>
			and <replaceable>orig-reason</replaceable> fields are the following:</para>
			<enumlist>
				<enum name = "away"><para>Callee is Away</para></enum>
				<enum name = "cf_dte"><para>Call Forwarding By The Called DTE</para></enum>
				<enum name = "cfb"><para>Call Forwarding Busy</para></enum>
				<enum name = "cfnr"><para>Call Forwarding No Reply</para></enum>
				<enum name = "cfu"><para>Call Forwarding Unconditional</para></enum>
				<enum name = "deflection"><para>Call Deflection</para></enum>
				<enum name = "dnd"><para>Do Not Disturb</para></enum>
				<enum name = "follow_me"><para>Follow Me</para></enum>
				<enum name = "out_of_order"><para>Called DTE Out-Of-Order</para></enum>
				<enum name = "send_to_vm"><para>Send the call to voicemail</para></enum>
				<enum name = "time_of_day"><para>Time of Day</para></enum>
				<enum name = "unavailable"><para>Callee is Unavailable</para></enum>
				<enum name = "unknown"><para>Unknown</para></enum>
			</enumlist>
			<note><para>You can set a user defined reason string that SIP can
			send/receive instead.  The user defined reason string my need to be
			quoted depending upon SIP or the peer's requirements.  These strings
			are treated as unknown by the non-SIP channel drivers.</para></note>
			<para>The allowable values for the <replaceable>xxx-name-charset</replaceable>
			field are the following:</para>
			<enumlist>
				<enum name = "unknown"><para>Unknown</para></enum>
				<enum name = "iso8859-1"><para>ISO8859-1</para></enum>
				<enum name = "withdrawn"><para>Withdrawn</para></enum>
				<enum name = "iso8859-2"><para>ISO8859-2</para></enum>
				<enum name = "iso8859-3"><para>ISO8859-3</para></enum>
				<enum name = "iso8859-4"><para>ISO8859-4</para></enum>
				<enum name = "iso8859-5"><para>ISO8859-5</para></enum>
				<enum name = "iso8859-7"><para>ISO8859-7</para></enum>
				<enum name = "bmp"><para>ISO10646 Bmp String</para></enum>
				<enum name = "utf8"><para>ISO10646 UTF-8 String</para></enum>
			</enumlist>
		</description>
	</function>
 ***/

enum ID_FIELD_STATUS {
	ID_FIELD_VALID,
	ID_FIELD_INVALID,
	ID_FIELD_UNKNOWN
};

AST_DEFINE_APP_ARGS_TYPE(ast_party_func_args,
	AST_APP_ARG(member);	/*!< Member name */
	AST_APP_ARG(opts);		/*!< Options token */
	AST_APP_ARG(other);		/*!< Any remining unused arguments */
	);

AST_DEFINE_APP_ARGS_TYPE(ast_party_members,
	AST_APP_ARG(subnames[10]);	/*!< Option member subnames */
	);

enum CONNECTED_LINE_OPT_FLAGS {
	CONNECTED_LINE_OPT_INHIBIT = (1 << 0),
};
enum CONNECTED_LINE_OPT_ARGS {
	CONNECTED_LINE_OPT_DUMMY,	/*!< Delete this if CONNECTED_LINE ever gets an option with parameters. */

	/*! \note This entry _MUST_ be the last one in the enum */
	CONNECTED_LINE_OPT_ARG_ARRAY_SIZE
};

AST_APP_OPTIONS(connectedline_opts, BEGIN_OPTIONS
	AST_APP_OPTION('i', CONNECTED_LINE_OPT_INHIBIT),
END_OPTIONS);

enum REDIRECTING_OPT_FLAGS {
	REDIRECTING_OPT_INHIBIT = (1 << 0),
};
enum REDIRECTING_OPT_ARGS {
	REDIRECTING_OPT_DUMMY,	/*!< Delete this if REDIRECTING ever gets an option with parameters. */

	/*! \note This entry _MUST_ be the last one in the enum */
	REDIRECTING_OPT_ARG_ARRAY_SIZE
};

AST_APP_OPTIONS(redirecting_opts, BEGIN_OPTIONS
	AST_APP_OPTION('i', REDIRECTING_OPT_INHIBIT),
END_OPTIONS);

/*!
 * \internal
 * \brief Read values from the party name struct.
 * \since 1.8
 *
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer.
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param name Party name to get values from.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_name_read(char *buf, size_t len, int argc, char *argv[], const struct ast_party_name *name)
{
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (argc == 0) {
		/* We want the name string */
		if (name->valid && name->str) {
			ast_copy_string(buf, name->str, len);
		}
	} else if (argc == 1 && !strcasecmp("valid", argv[0])) {
		snprintf(buf, len, "%d", name->valid);
	} else if (argc == 1 && !strcasecmp("charset", argv[0])) {
		ast_copy_string(buf, ast_party_name_charset_str(name->char_set), len);
	} else if (argc == 1 && !strncasecmp("pres", argv[0], 4)) {
		/* Accept pres[entation] */
		ast_copy_string(buf, ast_named_caller_presentation(name->presentation), len);
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Read values from the party number struct.
 * \since 1.8
 *
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer.
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param number Party number to get values from.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_number_read(char *buf, size_t len, int argc, char *argv[], const struct ast_party_number *number)
{
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (argc == 0) {
		/* We want the number string */
		if (number->valid && number->str) {
			ast_copy_string(buf, number->str, len);
		}
	} else if (argc == 1 && !strcasecmp("valid", argv[0])) {
		snprintf(buf, len, "%d", number->valid);
	} else if (argc == 1 && !strcasecmp("plan", argv[0])) {
		snprintf(buf, len, "%d", number->plan);
	} else if (argc == 1 && !strncasecmp("pres", argv[0], 4)) {
		/* Accept pres[entation] */
		ast_copy_string(buf, ast_named_caller_presentation(number->presentation), len);
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Read values from the party subaddress struct.
 * \since 1.8
 *
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer.
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param subaddress Party subaddress to get values from.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_subaddress_read(char *buf, size_t len, int argc, char *argv[], const struct ast_party_subaddress *subaddress)
{
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (argc == 0) {
		/* We want the subaddress string */
		if (subaddress->str) {
			ast_copy_string(buf, subaddress->str, len);
		}
	} else if (argc == 1 && !strcasecmp("valid", argv[0])) {
		snprintf(buf, len, "%d", subaddress->valid);
	} else if (argc == 1 && !strcasecmp("type", argv[0])) {
		snprintf(buf, len, "%d", subaddress->type);
	} else if (argc == 1 && !strcasecmp("odd", argv[0])) {
		snprintf(buf, len, "%d", subaddress->odd_even_indicator);
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Read values from the party id struct.
 * \since 1.8
 *
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer.
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param id Party id to get values from.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_id_read(char *buf, size_t len, int argc, char *argv[], const struct ast_party_id *id)
{
	enum ID_FIELD_STATUS status;

	if (argc == 0) {
		/* Must have at least one subname. */
		return ID_FIELD_UNKNOWN;
	}

	status = ID_FIELD_VALID;

	if (argc == 1 && !strcasecmp("all", argv[0])) {
		snprintf(buf, len, "\"%s\" <%s>",
			 S_COR(id->name.valid, id->name.str, ""),
			 S_COR(id->number.valid, id->number.str, ""));
	} else if (!strcasecmp("name", argv[0])) {
		status = party_name_read(buf, len, argc - 1, argv + 1, &id->name);
	} else if (!strncasecmp("num", argv[0], 3)) {
		/* Accept num[ber] */
		status = party_number_read(buf, len, argc - 1, argv + 1, &id->number);
	} else if (!strncasecmp("subaddr", argv[0], 7)) {
		/* Accept subaddr[ess] */
		status = party_subaddress_read(buf, len, argc - 1, argv + 1, &id->subaddress);
	} else if (argc == 1 && !strcasecmp("tag", argv[0])) {
		if (id->tag) {
			ast_copy_string(buf, id->tag, len);
		}
	} else if (argc == 1 && !strcasecmp("ton", argv[0])) {
		/* ton is an alias for num-plan */
		snprintf(buf, len, "%d", id->number.plan);
	} else if (argc == 1 && !strncasecmp("pres", argv[0], 4)) {
		/*
		 * Accept pres[entation]
		 * This is the combined name/number presentation.
		 */
		ast_copy_string(buf,
			ast_named_caller_presentation(ast_party_id_presentation(id)), len);
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Write new values to the party name struct
 * \since 1.8
 *
 * \param name Party name struct to write values
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param value Value to assign to the party name.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_INVALID on error with field value.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_name_write(struct ast_party_name *name, int argc, char *argv[], const char *value)
{
	char *val;
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (argc == 0) {
		/* We are setting the name string */
		name->valid = 1;
		name->str = ast_strdup(value);
		ast_trim_blanks(name->str);
	} else if (argc == 1 && !strcasecmp("valid", argv[0])) {
		name->valid = atoi(value) ? 1 : 0;
	} else if (argc == 1 && !strcasecmp("charset", argv[0])) {
		int char_set;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			char_set = atoi(val);
		} else {
			char_set = ast_party_name_charset_parse(val);
		}

		if (char_set < 0) {
			ast_log(LOG_ERROR,
				"Unknown name char-set '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
		} else {
			name->char_set = char_set;
		}
	} else if (argc == 1 && !strncasecmp("pres", argv[0], 4)) {
		int pres;

		/* Accept pres[entation] */
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			pres = atoi(val);
		} else {
			pres = ast_parse_caller_presentation(val);
		}

		if (pres < 0) {
			ast_log(LOG_ERROR,
				"Unknown name presentation '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
		} else {
			name->presentation = pres;
		}
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Write new values to the party number struct
 * \since 1.8
 *
 * \param number Party number struct to write values
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param value Value to assign to the party number.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_INVALID on error with field value.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_number_write(struct ast_party_number *number, int argc, char *argv[], const char *value)
{
	char *val;
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (argc == 0) {
		/* We are setting the number string */
		number->valid = 1;
		number->str = ast_strdup(value);
		ast_trim_blanks(number->str);
	} else if (argc == 1 && !strcasecmp("valid", argv[0])) {
		number->valid = atoi(value) ? 1 : 0;
	} else if (argc == 1 && !strcasecmp("plan", argv[0])) {
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			number->plan = atoi(val);
		} else {
			ast_log(LOG_ERROR,
				"Unknown type-of-number/numbering-plan '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
		}
	} else if (argc == 1 && !strncasecmp("pres", argv[0], 4)) {
		int pres;

		/* Accept pres[entation] */
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			pres = atoi(val);
		} else {
			pres = ast_parse_caller_presentation(val);
		}

		if (pres < 0) {
			ast_log(LOG_ERROR,
				"Unknown number presentation '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
		} else {
			number->presentation = pres;
		}
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Write new values to the party subaddress struct
 * \since 1.8
 *
 * \param subaddress Party subaddress struct to write values
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param value Value to assign to the party subaddress.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_INVALID on error with field value.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_subaddress_write(struct ast_party_subaddress *subaddress, int argc, char *argv[], const char *value)
{
	enum ID_FIELD_STATUS status;

	status = ID_FIELD_VALID;

	if (argc == 0) {
		/* We are setting the subaddress string */
		subaddress->str = ast_strdup(value);
		ast_trim_blanks(subaddress->str);
	} else if (argc == 1 && !strcasecmp("valid", argv[0])) {
		subaddress->valid = atoi(value) ? 1 : 0;
	} else if (argc == 1 && !strcasecmp("type", argv[0])) {
		subaddress->type = atoi(value) ? 2 : 0;
	} else if (argc == 1 && !strcasecmp("odd", argv[0])) {
		subaddress->odd_even_indicator = atoi(value) ? 1 : 0;
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Write new values to the party id struct
 * \since 1.8
 *
 * \param id Party ID struct to write values
 * \param argc Number of party member subnames.
 * \param argv Party member subnames given.
 * \param value Value to assign to the party id.
 *
 * \retval ID_FIELD_VALID on success.
 * \retval ID_FIELD_INVALID on error with field value.
 * \retval ID_FIELD_UNKNOWN on unknown field name.
 */
static enum ID_FIELD_STATUS party_id_write(struct ast_party_id *id, int argc, char *argv[], const char *value)
{
	char *val;
	enum ID_FIELD_STATUS status;

	if (argc == 0) {
		/* Must have at least one subname. */
		return ID_FIELD_UNKNOWN;
	}

	status = ID_FIELD_VALID;

	if (argc == 1 && !strcasecmp("all", argv[0])) {
		char name[256];
		char num[256];

		ast_callerid_split(value, name, sizeof(name), num, sizeof(num));
		id->name.valid = 1;
		id->name.str = ast_strdup(name);
		if (!id->name.str) {
			return ID_FIELD_INVALID;
		}
		id->number.valid = 1;
		id->number.str = ast_strdup(num);
		if (!id->number.str) {
			return ID_FIELD_INVALID;
		}
	} else if (!strcasecmp("name", argv[0])) {
		status = party_name_write(&id->name, argc - 1, argv + 1, value);
	} else if (!strncasecmp("num", argv[0], 3)) {
		/* Accept num[ber] */
		status = party_number_write(&id->number, argc - 1, argv + 1, value);
	} else if (!strncasecmp("subaddr", argv[0], 7)) {
		/* Accept subaddr[ess] */
		status = party_subaddress_write(&id->subaddress, argc - 1, argv + 1, value);
	} else if (argc == 1 && !strcasecmp("tag", argv[0])) {
		id->tag = ast_strdup(value);
		ast_trim_blanks(id->tag);
	} else if (argc == 1 && !strcasecmp("ton", argv[0])) {
		/* ton is an alias for num-plan */
		argv[0] = "plan";
		status = party_number_write(&id->number, argc, argv, value);
	} else if (argc == 1 && !strncasecmp("pres", argv[0], 4)) {
		int pres;

		/*
		 * Accept pres[entation]
		 * This is the combined name/number presentation.
		 */
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			pres = atoi(val);
		} else {
			pres = ast_parse_caller_presentation(val);
		}

		if (pres < 0) {
			ast_log(LOG_ERROR,
				"Unknown combined presentation '%s', value unchanged\n", val);
			status = ID_FIELD_INVALID;
		} else {
			id->name.presentation = pres;
			id->number.presentation = pres;
		}
	} else {
		status = ID_FIELD_UNKNOWN;
	}

	return status;
}

/*!
 * \internal
 * \brief Read values from the caller-id information struct.
 *
 * \param chan Asterisk channel to read
 * \param cmd Not used
 * \param data Caller-id function datatype string
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int callerid_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parms;
	struct ast_party_members member = { 0, };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(member);	/*!< Member name */
		AST_APP_ARG(cid);		/*!< Optional caller id to parse instead of from the channel. */
		);

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan) {
		return -1;
	}

	parms = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parms);
	if (args.argc == 0) {
		/* Must have at least one argument. */
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(member, args.member, '-');
	if (member.argc == 0 || ARRAY_LEN(member.subnames) <= member.argc) {
		/* Too few or too many subnames */
		return -1;
	}

	if (args.argc == 2) {
		char name[80];
		char num[80];

		ast_callerid_split(args.cid, name, sizeof(name), num, sizeof(num));

		if (member.argc == 1 && !strcasecmp("all", member.subnames[0])) {
			snprintf(buf, len, "\"%s\" <%s>", name, num);
		} else if (member.argc == 1 && !strcasecmp("name", member.subnames[0])) {
			ast_copy_string(buf, name, len);
		} else if (member.argc == 1 && !strncasecmp("num", member.subnames[0], 3)) {
			/* Accept num[ber] */
			ast_copy_string(buf, num, len);
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}
	} else {
		enum ID_FIELD_STATUS status;
		ast_channel_lock(chan);

		if (member.argc == 1 && !strcasecmp("rdnis", member.subnames[0])) {
			if (ast_channel_redirecting(chan)->from.number.valid
				&& ast_channel_redirecting(chan)->from.number.str) {
				ast_copy_string(buf, ast_channel_redirecting(chan)->from.number.str, len);
			}
		} else if (!strcasecmp("dnid", member.subnames[0])) {
			if (member.argc == 1) {
				/* Setup as if user had given dnid-num instead. */
				member.argc = 2;
				member.subnames[1] = "num";
			}
			if (!strncasecmp("num", member.subnames[1], 3)) {
				/*
				 * Accept num[ber]
				 * dnid-num...
				 */
				if (member.argc == 2) {
					/* dnid-num */
					if (ast_channel_dialed(chan)->number.str) {
						ast_copy_string(buf, ast_channel_dialed(chan)->number.str, len);
					}
				} else if (member.argc == 3 && !strcasecmp("plan", member.subnames[2])) {
					/* dnid-num-plan */
					snprintf(buf, len, "%d", ast_channel_dialed(chan)->number.plan);
				} else {
					ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
				}
			} else if (!strncasecmp("subaddr", member.subnames[1], 7)) {
				/*
				 * Accept subaddr[ess]
				 * dnid-subaddr...
				 */
				status = party_subaddress_read(buf, len, member.argc - 2, member.subnames + 2,
					&ast_channel_dialed(chan)->subaddress);
				switch (status) {
				case ID_FIELD_VALID:
				case ID_FIELD_INVALID:
					break;
				default:
					ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
					break;
				}
			} else {
				ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
			}
		} else if (member.argc == 1 && !strcasecmp("ani2", member.subnames[0])) {
			snprintf(buf, len, "%d", ast_channel_caller(chan)->ani2);
		} else if (!strcasecmp("ani", member.subnames[0])) {
			if (member.argc == 1) {
				/* Setup as if user had given ani-num instead. */
				member.argc = 2;
				member.subnames[1] = "num";
			}
			status = party_id_read(buf, len, member.argc - 1, member.subnames + 1,
				&ast_channel_caller(chan)->ani);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
				break;
			}
		} else if (!strcasecmp("priv", member.subnames[0])) {
			status = party_id_read(buf, len, member.argc - 1, member.subnames + 1,
				&ast_channel_caller(chan)->priv);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
				break;
			}
		} else {
			status = party_id_read(buf, len, member.argc, member.subnames, &ast_channel_caller(chan)->id);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
				break;
			}
		}

		ast_channel_unlock(chan);
	}

	return 0;
}

/*!
 * \internal
 * \brief Write new values to the caller-id information struct.
 *
 * \param chan Asterisk channel to update
 * \param cmd Not used
 * \param data Caller-id function datatype string
 * \param value Value to assign to the caller-id information struct.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int callerid_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_party_caller caller;
	struct ast_party_dialed dialed;
	enum ID_FIELD_STATUS status;
	char *val;
	char *parms;
	struct ast_party_func_args args = { 0, };
	struct ast_party_members member = { 0, };

	if (!value || !chan) {
		return -1;
	}

	parms = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parms);
	if (args.argc == 0) {
		/* Must have at least one argument. */
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(member, args.member, '-');
	if (member.argc == 0 || ARRAY_LEN(member.subnames) <= member.argc) {
		/* Too few or too many subnames */
		return -1;
	}

	value = ast_skip_blanks(value);

	ast_channel_lock(chan);
	if (member.argc == 1 && !strcasecmp("rdnis", member.subnames[0])) {
		ast_channel_redirecting(chan)->from.number.valid = 1;
		ast_free(ast_channel_redirecting(chan)->from.number.str);
		ast_channel_redirecting(chan)->from.number.str = ast_strdup(value);
	} else if (!strcasecmp("dnid", member.subnames[0])) {
		ast_party_dialed_set_init(&dialed, ast_channel_dialed(chan));
		if (member.argc == 1) {
			/* Setup as if user had given dnid-num instead. */
			member.argc = 2;
			member.subnames[1] = "num";
		}
		if (!strncasecmp("num", member.subnames[1], 3)) {
			/*
			 * Accept num[ber]
			 * dnid-num...
			 */
			if (member.argc == 2) {
				/* dnid-num */
				dialed.number.str = ast_strdup(value);
				ast_trim_blanks(dialed.number.str);
				ast_party_dialed_set(ast_channel_dialed(chan), &dialed);
			} else if (member.argc == 3 && !strcasecmp("plan", member.subnames[2])) {
				/* dnid-num-plan */
				val = ast_strdupa(value);
				ast_trim_blanks(val);

				if (('0' <= val[0]) && (val[0] <= '9')) {
					ast_channel_dialed(chan)->number.plan = atoi(val);
				} else {
					ast_log(LOG_ERROR,
						"Unknown type-of-number/numbering-plan '%s', value unchanged\n", val);
				}
			} else {
				ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
			}
		} else if (!strncasecmp("subaddr", member.subnames[1], 7)) {
			/*
			 * Accept subaddr[ess]
			 * dnid-subaddr...
			 */
			status = party_subaddress_write(&dialed.subaddress, member.argc - 2,
				member.subnames + 2, value);
			switch (status) {
			case ID_FIELD_VALID:
				ast_party_dialed_set(ast_channel_dialed(chan), &dialed);
				break;
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
				break;
			}
		} else {
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
		}
		ast_party_dialed_free(&dialed);
	} else if (member.argc == 1 && !strcasecmp("ani2", member.subnames[0])) {
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			ast_channel_caller(chan)->ani2 = atoi(val);
		} else {
			ast_log(LOG_ERROR, "Unknown callerid ani2 '%s', value unchanged\n", val);
		}
	} else if (!strcasecmp("ani", member.subnames[0])) {
		ast_party_caller_set_init(&caller, ast_channel_caller(chan));
		if (member.argc == 1) {
			/* Setup as if user had given ani-num instead. */
			member.argc = 2;
			member.subnames[1] = "num";
		}
		status = party_id_write(&caller.ani, member.argc - 1, member.subnames + 1, value);
		switch (status) {
		case ID_FIELD_VALID:
			ast_party_caller_set(ast_channel_caller(chan), &caller, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
			break;
		}
		ast_party_caller_free(&caller);
	} else if (!strcasecmp("priv", member.subnames[0])) {
		ast_party_caller_set_init(&caller, ast_channel_caller(chan));
		status = party_id_write(&caller.priv, member.argc - 1, member.subnames + 1, value);
		switch (status) {
		case ID_FIELD_VALID:
			ast_party_caller_set(ast_channel_caller(chan), &caller, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
			break;
		}
		ast_party_caller_free(&caller);
	} else {
		ast_party_caller_set_init(&caller, ast_channel_caller(chan));
		status = party_id_write(&caller.id, member.argc, member.subnames, value);
		switch (status) {
		case ID_FIELD_VALID:
			ast_channel_set_caller_event(chan, &caller, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown callerid data type '%s'.\n", data);
			break;
		}
		ast_party_caller_free(&caller);
	}
	ast_channel_unlock(chan);

	return 0;
}

/*!
 * \internal
 * \brief Read values from the connected line information struct.
 *
 * \param chan Asterisk channel to read
 * \param cmd Not used
 * \param data Connected line function datatype string
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int connectedline_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_party_members member = { 0, };
	char *read_what;
	enum ID_FIELD_STATUS status;

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan) {
		return -1;
	}

	read_what = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(member, read_what, '-');
	if (member.argc == 0 || ARRAY_LEN(member.subnames) <= member.argc) {
		/* Too few or too many subnames */
		return -1;
	}

	ast_channel_lock(chan);

	if (member.argc == 1 && !strcasecmp("source", member.subnames[0])) {
		ast_copy_string(buf, ast_connected_line_source_name(ast_channel_connected(chan)->source), len);
	} else if (!strcasecmp("priv", member.subnames[0])) {
		status = party_id_read(buf, len, member.argc - 1, member.subnames + 1,
			&ast_channel_connected(chan)->priv);
		switch (status) {
		case ID_FIELD_VALID:
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown connectedline data type '%s'.\n", data);
			break;
		}
	} else {
		status = party_id_read(buf, len, member.argc, member.subnames, &ast_channel_connected(chan)->id);
		switch (status) {
		case ID_FIELD_VALID:
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown connectedline data type '%s'.\n", data);
			break;
		}
	}

	ast_channel_unlock(chan);

	return 0;
}

/*!
 * \internal
 * \brief Write new values to the connected line information struct.
 *
 * \param chan Asterisk channel to update
 * \param cmd Not used
 * \param data Connected line function datatype string
 * \param value Value to assign to the connected line information struct.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int connectedline_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_party_connected_line connected;
	char *val;
	char *parms;
	void (*set_it)(struct ast_channel *chan, const struct ast_party_connected_line *connected, const struct ast_set_party_connected_line *update);
	struct ast_party_func_args args = { 0, };
	struct ast_party_members member = { 0, };
	struct ast_flags opts;
	char *opt_args[CONNECTED_LINE_OPT_ARG_ARRAY_SIZE];
	enum ID_FIELD_STATUS status;

	if (!value || !chan) {
		return -1;
	}

	parms = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parms);
	if (args.argc == 0) {
		/* Must have at least one argument. */
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(member, args.member, '-');
	if (member.argc == 0 || ARRAY_LEN(member.subnames) <= member.argc) {
		/* Too few or too many subnames */
		return -1;
	}

	if (ast_app_parse_options(connectedline_opts, &opts, opt_args, args.opts)) {
		/* General invalid option syntax. */
		return -1;
	}

	/* Determine if the update indication inhibit option is present */
	if (ast_test_flag(&opts, CONNECTED_LINE_OPT_INHIBIT)) {
		set_it = ast_channel_set_connected_line;
	} else {
		set_it = ast_channel_update_connected_line;
	}

	ast_channel_lock(chan);
	ast_party_connected_line_set_init(&connected, ast_channel_connected(chan));
	ast_channel_unlock(chan);

	value = ast_skip_blanks(value);

	if (member.argc == 1 && !strcasecmp("source", member.subnames[0])) {
		int source;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			source = atoi(val);
		} else {
			source = ast_connected_line_source_parse(val);
		}

		if (source < 0) {
			ast_log(LOG_ERROR, "Unknown connectedline source '%s', value unchanged\n", val);
		} else {
			connected.source = source;
			set_it(chan, &connected, NULL);
		}
	} else if (!strcasecmp("priv", member.subnames[0])) {
		status = party_id_write(&connected.priv, member.argc - 1, member.subnames + 1, value);
		switch (status) {
		case ID_FIELD_VALID:
			set_it(chan, &connected, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown connectedline data type '%s'.\n", data);
			break;
		}
		ast_party_connected_line_free(&connected);
	} else {
		status = party_id_write(&connected.id, member.argc, member.subnames, value);
		switch (status) {
		case ID_FIELD_VALID:
			set_it(chan, &connected, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown connectedline data type '%s'.\n", data);
			break;
		}
		ast_party_connected_line_free(&connected);
	}

	return 0;
}

/*!
 * \internal
 * \brief Read values from the redirecting information struct.
 *
 * \param chan Asterisk channel to read
 * \param cmd Not used
 * \param data Redirecting function datatype string
 * \param buf Buffer to fill with read value.
 * \param len Length of the buffer
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int redirecting_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_party_members member = { 0, };
	char *read_what;
	const struct ast_party_redirecting *ast_redirecting;
	enum ID_FIELD_STATUS status;

	/* Ensure that the buffer is empty */
	*buf = 0;

	if (!chan) {
		return -1;
	}

	read_what = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(member, read_what, '-');
	if (member.argc == 0 || ARRAY_LEN(member.subnames) <= member.argc) {
		/* Too few or too many subnames */
		return -1;
	}

	ast_channel_lock(chan);

	ast_redirecting = ast_channel_redirecting(chan);
	if (!strcasecmp("orig", member.subnames[0])) {
		if (member.argc == 2 && !strcasecmp("reason", member.subnames[1])) {
			ast_copy_string(buf,
				ast_redirecting_reason_name(&ast_redirecting->orig_reason), len);
		} else {
			status = party_id_read(buf, len, member.argc - 1, member.subnames + 1,
				&ast_redirecting->orig);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
		}
	} else if (!strcasecmp("from", member.subnames[0])) {
		status = party_id_read(buf, len, member.argc - 1, member.subnames + 1,
			&ast_redirecting->from);
		switch (status) {
		case ID_FIELD_VALID:
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
	} else if (!strcasecmp("to", member.subnames[0])) {
		status = party_id_read(buf, len, member.argc - 1, member.subnames + 1,
			&ast_redirecting->to);
		switch (status) {
		case ID_FIELD_VALID:
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
	} else if (member.argc == 1 && !strncasecmp("pres", member.subnames[0], 4)) {
		/*
		 * Accept pres[entation]
		 * This is the combined from name/number presentation.
		 */
		ast_copy_string(buf,
			ast_named_caller_presentation(
				ast_party_id_presentation(&ast_redirecting->from)), len);
	} else if (member.argc == 1 && !strcasecmp("reason", member.subnames[0])) {
		ast_copy_string(buf, ast_redirecting_reason_name(&ast_redirecting->reason), len);
	} else if (member.argc == 1 && !strcasecmp("count", member.subnames[0])) {
		snprintf(buf, len, "%d", ast_redirecting->count);
	} else if (1 < member.argc && !strcasecmp("priv", member.subnames[0])) {
		if (!strcasecmp("orig", member.subnames[1])) {
			status = party_id_read(buf, len, member.argc - 2, member.subnames + 2,
				&ast_redirecting->priv_orig);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
		} else if (!strcasecmp("from", member.subnames[1])) {
			status = party_id_read(buf, len, member.argc - 2, member.subnames + 2,
				&ast_redirecting->priv_from);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
		} else if (!strcasecmp("to", member.subnames[1])) {
			status = party_id_read(buf, len, member.argc - 2, member.subnames + 2,
				&ast_redirecting->priv_to);
			switch (status) {
			case ID_FIELD_VALID:
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
		} else {
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
		}
	} else {
		ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
	}

	ast_channel_unlock(chan);

	return 0;
}

/*!
 * \internal
 * \brief Write new values to the redirecting information struct.
 *
 * \param chan Asterisk channel to update
 * \param cmd Not used
 * \param data Redirecting function datatype string
 * \param value Value to assign to the redirecting information struct.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int redirecting_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_party_redirecting redirecting;
	enum ID_FIELD_STATUS status;
	char *val;
	char *parms;
	void (*set_it)(struct ast_channel *chan, const struct ast_party_redirecting *redirecting, const struct ast_set_party_redirecting *update);
	struct ast_party_func_args args = { 0, };
	struct ast_party_members member = { 0, };
	struct ast_flags opts;
	char *opt_args[REDIRECTING_OPT_ARG_ARRAY_SIZE];

	if (!value || !chan) {
		return -1;
	}

	parms = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parms);
	if (args.argc == 0) {
		/* Must have at least one argument. */
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(member, args.member, '-');
	if (member.argc == 0 || ARRAY_LEN(member.subnames) <= member.argc) {
		/* Too few or too many subnames */
		return -1;
	}

	if (ast_app_parse_options(redirecting_opts, &opts, opt_args, args.opts)) {
		/* General invalid option syntax. */
		return -1;
	}

	/* Determine if the update indication inhibit option is present */
	if (ast_test_flag(&opts, REDIRECTING_OPT_INHIBIT)) {
		set_it = ast_channel_set_redirecting;
	} else {
		set_it = ast_channel_update_redirecting;
	}

	ast_channel_lock(chan);
	ast_party_redirecting_set_init(&redirecting, ast_channel_redirecting(chan));
	ast_channel_unlock(chan);

	value = ast_skip_blanks(value);

	if (!strcasecmp("orig", member.subnames[0])) {
		if (member.argc == 2 && !strcasecmp("reason", member.subnames[1])) {
			int reason;

			val = ast_strdupa(value);
			ast_trim_blanks(val);

			if (('0' <= val[0]) && (val[0] <= '9')) {
				reason = atoi(val);
			} else {
				reason = ast_redirecting_reason_parse(val);
			}

			if (reason < 0) {
			/* The argument passed into the function does not correspond to a pre-defined
			 * reason, so we can just set the reason string to what was given and set the
			 * code to be unknown
			 */
				redirecting.orig_reason.code = AST_REDIRECTING_REASON_UNKNOWN;
				redirecting.orig_reason.str = val;
				set_it(chan, &redirecting, NULL);
			} else {
				redirecting.orig_reason.code = reason;
				redirecting.orig_reason.str = "";
				set_it(chan, &redirecting, NULL);
			}
		} else {
			status = party_id_write(&redirecting.orig, member.argc - 1, member.subnames + 1,
				value);
			switch (status) {
			case ID_FIELD_VALID:
				set_it(chan, &redirecting, NULL);
				break;
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
			ast_party_redirecting_free(&redirecting);
		}
	} else if (!strcasecmp("from", member.subnames[0])) {
		status = party_id_write(&redirecting.from, member.argc - 1, member.subnames + 1,
			value);
		switch (status) {
		case ID_FIELD_VALID:
			set_it(chan, &redirecting, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
		ast_party_redirecting_free(&redirecting);
	} else if (!strcasecmp("to", member.subnames[0])) {
		status = party_id_write(&redirecting.to, member.argc - 1, member.subnames + 1, value);
		switch (status) {
		case ID_FIELD_VALID:
			set_it(chan, &redirecting, NULL);
			break;
		case ID_FIELD_INVALID:
			break;
		default:
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
			break;
		}
		ast_party_redirecting_free(&redirecting);
	} else if (member.argc == 1 && !strncasecmp("pres", member.subnames[0], 4)) {
		int pres;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			pres = atoi(val);
		} else {
			pres = ast_parse_caller_presentation(val);
		}

		if (pres < 0) {
			ast_log(LOG_ERROR,
				"Unknown redirecting combined presentation '%s', value unchanged\n", val);
		} else {
			redirecting.from.name.presentation = pres;
			redirecting.from.number.presentation = pres;
			redirecting.to.name.presentation = pres;
			redirecting.to.number.presentation = pres;
			set_it(chan, &redirecting, NULL);
		}
	} else if (member.argc == 1 && !strcasecmp("reason", member.subnames[0])) {
		int reason;

		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			reason = atoi(val);
		} else {
			reason = ast_redirecting_reason_parse(val);
		}

		if (reason < 0) {
			/* The argument passed into the function does not correspond to a pre-defined
			 * reason, so we can just set the reason string to what was given and set the
			 * code to be unknown
			 */
			redirecting.reason.code = AST_REDIRECTING_REASON_UNKNOWN;
			redirecting.reason.str = val;
			set_it(chan, &redirecting, NULL);
		} else {
			redirecting.reason.code = reason;
			redirecting.reason.str = "";
			set_it(chan, &redirecting, NULL);
		}
	} else if (member.argc == 1 && !strcasecmp("count", member.subnames[0])) {
		val = ast_strdupa(value);
		ast_trim_blanks(val);

		if (('0' <= val[0]) && (val[0] <= '9')) {
			redirecting.count = atoi(val);
			set_it(chan, &redirecting, NULL);
		} else {
			ast_log(LOG_ERROR, "Unknown redirecting count '%s', value unchanged\n", val);
		}
	} else if (1 < member.argc && !strcasecmp("priv", member.subnames[0])) {
		if (!strcasecmp("orig", member.subnames[1])) {
			status = party_id_write(&redirecting.priv_orig, member.argc - 2, member.subnames + 2,
				value);
			switch (status) {
			case ID_FIELD_VALID:
				set_it(chan, &redirecting, NULL);
				break;
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
			ast_party_redirecting_free(&redirecting);
		} else if (!strcasecmp("from", member.subnames[1])) {
			status = party_id_write(&redirecting.priv_from, member.argc - 2, member.subnames + 2,
				value);
			switch (status) {
			case ID_FIELD_VALID:
				set_it(chan, &redirecting, NULL);
				break;
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
			ast_party_redirecting_free(&redirecting);
		} else if (!strcasecmp("to", member.subnames[1])) {
			status = party_id_write(&redirecting.priv_to, member.argc - 2, member.subnames + 2, value);
			switch (status) {
			case ID_FIELD_VALID:
				set_it(chan, &redirecting, NULL);
				break;
			case ID_FIELD_INVALID:
				break;
			default:
				ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
				break;
			}
			ast_party_redirecting_free(&redirecting);
		} else {
			ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
		}
	} else {
		ast_log(LOG_ERROR, "Unknown redirecting data type '%s'.\n", data);
	}

	return 0;
}

static struct ast_custom_function callerid_function = {
	.name = "CALLERID",
	.read = callerid_read,
	.read_max = 256,
	.write = callerid_write,
};

static struct ast_custom_function connectedline_function = {
	.name = "CONNECTEDLINE",
	.read = connectedline_read,
	.write = connectedline_write,
};

static struct ast_custom_function redirecting_function = {
	.name = "REDIRECTING",
	.read = redirecting_read,
	.write = redirecting_write,
};

/*!
 * \internal
 * \brief Unload the function module
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int unload_module(void)
{
	ast_custom_function_unregister(&callerid_function);
	ast_custom_function_unregister(&connectedline_function);
	ast_custom_function_unregister(&redirecting_function);
	return 0;
}

/*!
 * \internal
 * \brief Load and initialize the function module.
 *
 * \retval AST_MODULE_LOAD_SUCCESS on success.
 * \retval AST_MODULE_LOAD_DECLINE on error.
 */
static int load_module(void)
{
	int res;

	res = ast_custom_function_register(&callerid_function);
	res |= ast_custom_function_register(&connectedline_function);
	res |= ast_custom_function_register(&redirecting_function);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

/* Do not wrap the following line. */
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Party ID related dialplan functions (Caller-ID, Connected-line, Redirecting)");
