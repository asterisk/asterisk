/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Technology independent asynchronous DTMF collection
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup functions
 *
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/framehook.h"
#include "asterisk/app.h"
#include "asterisk/conversions.h"

/*** DOCUMENTATION
	<application name="StoreDTMF" language="en_US">
		<since>
			<version>16.20.0</version>
			<version>18.6.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Stores DTMF digits transmitted or received on a channel.
		</synopsis>
		<syntax>
			<parameter name="direction" required="true">
				<para>Must be <literal>TX</literal> or <literal>RX</literal> to
				store digits, or <literal>remove</literal> to disable.</para>
			</parameter>
		</syntax>
		<description>
			<para>The StoreDTMF function can be used to obtain digits sent in the
			<literal>TX</literal> or <literal>RX</literal> direction of any channel.</para>
			<para>The arguments are:</para>
			<para><replaceable>var_name</replaceable>: Name of variable to which to append
			digits.</para>
			<para><replaceable>max_digits</replaceable>: The maximum number of digits to
			store in the variable. Defaults to 0 (no maximum). After reading <literal>
			maximum</literal> digits, no more digits will be stored.</para>
			<example title="Store digits in CDR variable">
			same => n,StoreDTMF(TX,CDR(digits))
			</example>
			<example title="Store up to 24 digits">
			same => n,StoreDTMF(RX,testvar,24)
			</example>
			<example title="Disable digit collection">
			same => n,StoreDTMF(remove)
			</example>
		</description>
	</application>
 ***/

static char *app = "StoreDTMF";

/*! \brief Private data structure used with the function's datastore */
struct dtmf_store_data {
	int framehook_id;
	char *rx_var;
	char *tx_var;
	int maxdigits;
};

static void datastore_destroy_cb(void *data) {
	struct dtmf_store_data *d;
	d = data;
	if (d) {
		if (d->rx_var) {
			ast_free(d->rx_var);
		}
		if (d->tx_var) {
			ast_free(d->tx_var);
		}
		ast_free(data);
	}
}

/*! \brief The channel datastore the function uses to store state */
static const struct ast_datastore_info dtmf_store_datastore = {
	.type = "dtmf_store",
	.destroy = datastore_destroy_cb
};

/*! \internal \brief Store digits tx/rx on the channel */
static int remove_dtmf_store(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct dtmf_store_data *data;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &dtmf_store_datastore, NULL);
	if (!datastore) {
		ast_log(AST_LOG_WARNING, "Cannot remove StoreDTMF from %s: StoreDTMF not currently enabled\n",
		        ast_channel_name(chan));
		return -1;
	}
	data = datastore->data;

	if (ast_framehook_detach(chan, data->framehook_id)) {
		ast_log(AST_LOG_WARNING, "Failed to remove StoreDTMF framehook from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}

	if (ast_channel_datastore_remove(chan, datastore)) {
		ast_log(AST_LOG_WARNING, "Failed to remove StoreDTMF datastore from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}
	ast_datastore_free(datastore);

	return 0;
}

/*! \brief Frame hook that is called to intercept digit/undigit */
static struct ast_frame *dtmf_store_framehook(struct ast_channel *chan,
	struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	char currentdata[512];
	char varnamesub[64];
	char *varname = NULL;
	struct dtmf_store_data *framedata = data;
	int len;

	if (!f || !framedata) {
		return f;
	}

	if ((event != AST_FRAMEHOOK_EVENT_WRITE) && (event != AST_FRAMEHOOK_EVENT_READ)) {
		return f;
	}

	if (f->frametype != AST_FRAME_DTMF_END) {
		return f;
	}

	/* If this is DTMF then store the digits */
	if (event == AST_FRAMEHOOK_EVENT_READ && framedata->rx_var) { /* coming from source */
		varname = framedata->rx_var;
	} else if (event == AST_FRAMEHOOK_EVENT_WRITE && framedata->tx_var) { /* going to source */
		varname = framedata->tx_var;
	}

	if (!varname) {
		return f;
	}

	sprintf(varnamesub, "${%s}", varname);
	pbx_substitute_variables_helper(chan, varnamesub, currentdata, 511);
	/* pbx_builtin_getvar_helper works for regular vars but not CDR vars */
	if (ast_strlen_zero(currentdata)) { /* var doesn't exist yet */
		ast_debug(3, "Creating new digit store: %s\n", varname);
	}
	len = strlen(currentdata);
	if (framedata->maxdigits > 0 && len >= framedata->maxdigits) {
		ast_debug(3, "Reached digit limit: %d\n", framedata->maxdigits);
		remove_dtmf_store(chan); /* reached max digit count, stop now */
		return f;
	} else {
		char newdata[len + 2]; /* one more char + terminator */
		if (len > 0) {
			ast_copy_string(newdata, currentdata, len + 2);
		}
		newdata[len] = (unsigned) f->subclass.integer;
		newdata[len + 1] = '\0';
		ast_debug(3, "Appending to digit store: now %s\n", newdata);
		pbx_builtin_setvar_helper(chan, varname, newdata);
	}
	return f;
}

/*! \internal \brief Enable digit interception on the channel */
static int dtmfstore_exec(struct ast_channel *chan, const char *appdata)
{
	struct ast_datastore *datastore;
	struct dtmf_store_data *data;
	static struct ast_framehook_interface digit_framehook_interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = dtmf_store_framehook,
		.disable_inheritance = 1,
	};
	char *parse = ast_strdupa(appdata);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(direction);
		AST_APP_ARG(varname);
		AST_APP_ARG(maxdigits);
	);
	SCOPED_CHANNELLOCK(chan_lock, chan);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(appdata)) {
		ast_log(AST_LOG_WARNING, "StoreDTMF requires an argument\n");
		return -1;
	}

	if (!strcasecmp(args.direction, "remove")) {
		return remove_dtmf_store(chan);
	}

	datastore = ast_channel_datastore_find(chan, &dtmf_store_datastore, NULL);
	if (datastore) {
		ast_log(AST_LOG_WARNING, "StoreDTMF already set on '%s'\n",
		        ast_channel_name(chan));
		return 0;
	}

	datastore = ast_datastore_alloc(&dtmf_store_datastore, NULL);
	if (!datastore) {
		return -1;
	}

	data = ast_calloc(1, sizeof(*data));
	if (!data) {
		ast_datastore_free(datastore);
		return -1;
	}

	digit_framehook_interface.data = data;

	data->rx_var = NULL;
	data->tx_var = NULL;
	data->maxdigits = 0;

	if (!strcasecmp(args.direction, "tx")) {
		data->tx_var = ast_strdup(args.varname);
	} else if (!strcasecmp(args.direction, "rx")) {
		data->rx_var = ast_strdup(args.varname);
	} else {
		ast_log(LOG_ERROR, "Direction must be either RX or TX\n");
		return -1;
	}

	if (!ast_strlen_zero(args.maxdigits)) {
		if (ast_str_to_int(args.maxdigits,&(data->maxdigits))) {
			ast_log(LOG_ERROR, "Invalid integer: %s\n", args.maxdigits);
			return -1;
		}
		if (data->maxdigits < 0) {
			ast_log(LOG_ERROR, "Invalid natural number: %d\n", data->maxdigits);
			return -1;
		} else if (data->maxdigits == 0) {
			ast_log(LOG_WARNING, "No maximum digit count set\n");
		}
	}

	data->framehook_id = ast_framehook_attach(chan, &digit_framehook_interface);
	if (data->framehook_id < 0) {
		ast_log(AST_LOG_WARNING, "Failed to attach StoreDTMF framehook to '%s'\n",
		        ast_channel_name(chan));
		ast_datastore_free(datastore);
		ast_free(data);
		return -1;
	}
	datastore->data = data;

	ast_channel_datastore_add(chan, datastore);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, dtmfstore_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Technology independent async DTMF storage");
