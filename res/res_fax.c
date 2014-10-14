/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008-2009, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 * Matthew Nicholson <mnicholson@digium.com>
 *
 * Initial T.38-gateway code
 * 2008, Daniel Ferenci <daniel.ferenci@nethemba.com>
 * Created by Nethemba s.r.o. http://www.nethemba.com
 * Sponsored by IPEX a.s. http://www.ipex.cz
 *
 * T.38-gateway integration into asterisk app_fax and rework
 * 2008-2011, Gregory Hinton Nietsky <gregory@distrotech.co.za>
 * dns Telecom http://www.dnstelecom.co.za
 *
 * Modified to make T.38-gateway compatible with Asterisk 1.6.2
 * 2010, Anton Verevkin <mymail@verevkin.it>
 * ViaNetTV http://www.vianettv.com
 *
 * Modified to make T.38-gateway work
 * 2010, Klaus Darilion, IPCom GmbH, www.ipcom.at
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

/*** MODULEINFO
	<conflict>app_fax</conflict>
	<support_level>core</support_level>
***/

/*! \file
 *
 * \brief Generic FAX Resource for FAX technology resource modules
 *
 * \author Dwayne M. Hubbard <dhubbard@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Matthew Nicholson <mnicholson@digium.com>
 * \author Gregory H. Nietsky  <gregory@distrotech.co.za>
 *
 * A generic FAX resource module that provides SendFAX and ReceiveFAX applications.
 * This module requires FAX technology modules, like res_fax_spandsp, to register with it
 * so it can use the technology modules to perform the actual FAX transmissions.
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/io.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/strings.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/astobj2.h"
#include "asterisk/res_fax.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/manager.h"
#include "asterisk/dsp.h"
#include "asterisk/indications.h"
#include "asterisk/ast_version.h"
#include "asterisk/translate.h"

/*** DOCUMENTATION
	<application name="ReceiveFAX" language="en_US" module="res_fax">
		<synopsis>
			Receive a FAX and save as a TIFF/F file.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
			<parameter name="options">
				<optionlist>
					<option name="d">
						<para>Enable FAX debugging.</para>
					</option>
					<option name="f">
						<para>Allow audio fallback FAX transfer on T.38 capable channels.</para>
					</option>
					<option name="F">
						<para>Force usage of audio mode on T.38 capable channels.</para>
					</option>
					<option name="s">
						<para>Send progress Manager events (overrides statusevents setting in res_fax.conf).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
 			<para>This application is provided by res_fax, which is a FAX technology agnostic module
 			that utilizes FAX technology resource modules to complete a FAX transmission.</para>
 			<para>Session arguments can be set by the FAXOPT function and to check results of the ReceiveFax() application.</para>
		</description>
		<see-also>
			<ref type="function">FAXOPT</ref>
		</see-also>
	</application>
	<application name="SendFAX" language="en_US" module="res_fax">
		<synopsis>
			Sends a specified TIFF/F file as a FAX.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" argsep="&amp;">
				<argument name="filename2" multiple="true">
					<para>TIFF file to send as a FAX.</para>
				</argument>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="d">
						<para>Enable FAX debugging.</para>
					</option>
					<option name="f">
						<para>Allow audio fallback FAX transfer on T.38 capable channels.</para>
					</option>
					<option name="F">
						<para>Force usage of audio mode on T.38 capable channels.</para>
					</option>
					<option name="s">
						<para>Send progress Manager events (overrides statusevents setting in res_fax.conf).</para>
					</option>
					<option name="z">
						<para>Initiate a T.38 reinvite on the channel if the remote end does not.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
 			<para>This application is provided by res_fax, which is a FAX technology agnostic module
 			that utilizes FAX technology resource modules to complete a FAX transmission.</para>
 			<para>Session arguments can be set by the FAXOPT function and to check results of the SendFax() application.</para>
		</description>
		<see-also>
			<ref type="function">FAXOPT</ref>
		</see-also>
	</application>
	<function name="FAXOPT" language="en_US" module="res_fax">
		<synopsis>
			Gets/sets various pieces of information about a fax session.
		</synopsis>
		<syntax>
			<parameter name="item" required="true">
				<enumlist>
					<enum name="ecm">
						<para>R/W Error Correction Mode (ECM) enable with 'yes', disable with 'no'.</para>
					</enum>
					<enum name="error">
						<para>R/O FAX transmission error code upon failure.</para>
					</enum>
					<enum name="filename">
						<para>R/O Filename of the first file of the FAX transmission.</para>
					</enum>
					<enum name="filenames">
						<para>R/O Filenames of all of the files in the FAX transmission (comma separated).</para>
					</enum>
					<enum name="headerinfo">
						<para>R/W FAX header information.</para>
					</enum>
					<enum name="localstationid">
						<para>R/W Local Station Identification.</para>
					</enum>
					<enum name="minrate">
						<para>R/W Minimum transfer rate set before transmission.</para>
					</enum>
					<enum name="maxrate">
						<para>R/W Maximum transfer rate set before transmission.</para>
					</enum>
					<enum name="modem">
						<para>R/W Modem type (v17/v27/v29).</para>
					</enum>
					<enum name="gateway">
						<para>R/W T38 fax gateway, with optional fax activity timeout in seconds (yes[,timeout]/no)</para>
					</enum>
					<enum name="faxdetect">
						<para>R/W Enable FAX detect with optional timeout in seconds (yes,t38,cng[,timeout]/no)</para>
					</enum>
					<enum name="pages">
						<para>R/O Number of pages transferred.</para>
					</enum>
					<enum name="rate">
						<para>R/O Negotiated transmission rate.</para>
					</enum>
					<enum name="remotestationid">
						<para>R/O Remote Station Identification after transmission.</para>
					</enum>
					<enum name="resolution">
						<para>R/O Negotiated image resolution after transmission.</para>
					</enum>
					<enum name="sessionid">
						<para>R/O Session ID of the FAX transmission.</para>
					</enum>
					<enum name="status">
						<para>R/O Result Status of the FAX transmission.</para>
					</enum>
					<enum name="statusstr">
						<para>R/O Verbose Result Status of the FAX transmission.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>FAXOPT can be used to override the settings for a FAX session listed in <filename>res_fax.conf</filename>,
		   	it can also be used to retreive information about a FAX session that has finished eg. pages/status.</para>
		</description>
		<see-also>
			<ref type="application">ReceiveFax</ref>
			<ref type="application">SendFax</ref>
		</see-also>
	</function>
***/

static const char app_receivefax[] = "ReceiveFAX";
static const char app_sendfax[] = "SendFAX";

struct debug_info_history {
	unsigned int consec_frames;
	unsigned int consec_ms;
	unsigned char silence;
};

struct ast_fax_debug_info {
	struct timeval base_tv;
	struct debug_info_history c2s, s2c;
	struct ast_dsp *dsp;
};

/*! \brief used for gateway framehook */
struct fax_gateway {
	/*! \brief FAX Session */
	struct ast_fax_session *s;
	struct ast_fax_session *peer_v21_session;
	struct ast_fax_session *chan_v21_session;
	/*! \brief reserved fax session token */
	struct ast_fax_tech_token *token;
	/*! \brief the start of our timeout counter */
	struct timeval timeout_start;
	/*! \brief framehook used in gateway mode */
	int framehook;
	/*! \brief bridged */
	int bridged:1;
	/*! \brief 1 if a v21 preamble has been detected */
	int detected_v21:1;
	/*! \brief a flag to track the state of our negotiation */
	enum ast_t38_state t38_state;
	/*! \brief original audio formats */
	struct ast_format chan_read_format;
	struct ast_format chan_write_format;
	struct ast_format peer_read_format;
	struct ast_format peer_write_format;
};

/*! \brief used for fax detect framehook */
struct fax_detect {
	/*! \brief the start of our timeout counter */
	struct timeval timeout_start;
	/*! \brief faxdetect timeout */
	int timeout;
	/*! \brief DSP Processor */
	struct ast_dsp *dsp;
	/*! \brief original audio formats */
	struct ast_format orig_format;
	/*! \brief fax session details */
	struct ast_fax_session_details *details;
	/*! \brief mode */
	int flags;
};

/*! \brief FAX Detect flags */
#define FAX_DETECT_MODE_CNG	(1 << 0)
#define FAX_DETECT_MODE_T38	(1 << 1)
#define FAX_DETECT_MODE_BOTH	(FAX_DETECT_MODE_CNG | FAX_DETECT_MODE_T38)

static int fax_logger_level = -1;

/*! \brief maximum buckets for res_fax ao2 containers */
#define FAX_MAXBUCKETS 10

#define RES_FAX_TIMEOUT 10000
#define FAX_GATEWAY_TIMEOUT RES_FAX_TIMEOUT

/*! \brief The faxregistry is used to manage information and statistics for all FAX sessions. */
static struct {
	/*! The number of active FAX sessions */
	int active_sessions;
	/*! The number of reserved FAX sessions */
	int reserved_sessions;
	/*! active sessions are astobj2 objects */
	struct ao2_container *container;
	/*! Total number of Tx FAX attempts */
	int fax_tx_attempts;
	/*! Total number of Rx FAX attempts */
	int fax_rx_attempts;
	/*! Number of successful FAX transmissions */
	int fax_complete;
	/*! Number of failed FAX transmissions */
	int fax_failures;
	/*! the next unique session name */
	int nextsessionname;
} faxregistry;

/*! \brief registered FAX technology modules are put into this list */
struct fax_module {
	const struct ast_fax_tech *tech;
	AST_RWLIST_ENTRY(fax_module) list;
};
static AST_RWLIST_HEAD_STATIC(faxmodules, fax_module);

#define RES_FAX_MINRATE 4800
#define RES_FAX_MAXRATE 14400
#define RES_FAX_STATUSEVENTS 0
#define RES_FAX_MODEM (AST_FAX_MODEM_V17 | AST_FAX_MODEM_V27 | AST_FAX_MODEM_V29)

struct fax_options {
	enum ast_fax_modems modems;
	uint32_t statusevents:1;
	uint32_t ecm:1;
	unsigned int minrate;
	unsigned int maxrate;
};

static struct fax_options general_options;

static const struct fax_options default_options = {
	.minrate = RES_FAX_MINRATE,
	.maxrate = RES_FAX_MAXRATE,
	.statusevents = RES_FAX_STATUSEVENTS,
	.modems = RES_FAX_MODEM,
	.ecm = AST_FAX_OPTFLAG_TRUE,
};

AST_RWLOCK_DEFINE_STATIC(options_lock);

static void get_general_options(struct fax_options* options);
static void set_general_options(const struct fax_options* options);

static const char *config = "res_fax.conf";

static int global_fax_debug = 0;

enum {
	OPT_CALLEDMODE  = (1 << 0),
	OPT_CALLERMODE  = (1 << 1),
	OPT_DEBUG       = (1 << 2),
	OPT_STATUS      = (1 << 3),
	OPT_ALLOWAUDIO  = (1 << 5),
	OPT_REQUEST_T38 = (1 << 6),
	OPT_FORCE_AUDIO = (1 << 7),
};

AST_APP_OPTIONS(fax_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('a', OPT_CALLEDMODE),
	AST_APP_OPTION('c', OPT_CALLERMODE),
	AST_APP_OPTION('d', OPT_DEBUG),
	AST_APP_OPTION('f', OPT_ALLOWAUDIO),
	AST_APP_OPTION('F', OPT_FORCE_AUDIO),
	AST_APP_OPTION('s', OPT_STATUS),
	AST_APP_OPTION('z', OPT_REQUEST_T38),
END_OPTIONS);

struct manager_event_info {
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	char cid[128];
};

static void debug_check_frame_for_silence(struct ast_fax_session *s, unsigned int c2s, struct ast_frame *frame)
{
	struct debug_info_history *history = c2s ? &s->debug_info->c2s : &s->debug_info->s2c;
	int dspsilence;
	unsigned int last_consec_frames, last_consec_ms;
	unsigned char wassil;
	struct timeval diff;

	diff = ast_tvsub(ast_tvnow(), s->debug_info->base_tv);

	ast_dsp_reset(s->debug_info->dsp);
	ast_dsp_silence(s->debug_info->dsp, frame, &dspsilence);

	wassil = history->silence;
	history->silence = (dspsilence != 0) ? 1 : 0;
	if (history->silence != wassil) {
		last_consec_frames = history->consec_frames;
		last_consec_ms = history->consec_ms;
		history->consec_frames = 0;
		history->consec_ms = 0;

		if ((last_consec_frames != 0)) {
			ast_verb(6, "Channel '%s' fax session '%u', [ %.3ld.%.6ld ], %s sent %u frames (%u ms) of %s.\n",
				 s->channame, s->id, (long) diff.tv_sec, (long int) diff.tv_usec,
				 (c2s) ? "channel" : "stack", last_consec_frames, last_consec_ms,
				 (wassil) ? "silence" : "energy");
		}
	}

	history->consec_frames++;
	history->consec_ms += (frame->samples / 8);
}

static void destroy_callback(void *data)
{
	if (data) {
		ao2_ref(data, -1);
	}
}

static const struct ast_datastore_info fax_datastore = {
	.type = "res_fax",
	.destroy = destroy_callback,
};

/*! \brief returns a reference counted pointer to a fax datastore, if it exists */
static struct ast_fax_session_details *find_details(struct ast_channel *chan)
{
	struct ast_fax_session_details *details;
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &fax_datastore, NULL))) {
		ast_channel_unlock(chan);
		return NULL;
	}
	if (!(details = datastore->data)) {
		ast_log(LOG_WARNING, "Huh?  channel '%s' has a FAX datastore without data!\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return NULL;
	}
	ao2_ref(details, 1);
	ast_channel_unlock(chan);

	return details;
}

/*! \brief destroy a FAX session details structure */
static void destroy_session_details(void *details)
{
	struct ast_fax_session_details *d = details;
	struct ast_fax_document *doc;

	while ((doc = AST_LIST_REMOVE_HEAD(&d->documents, next))) {
		ast_free(doc);
	}
	ast_string_field_free_memory(d);
}

/*! \brief create a FAX session details structure */
static struct ast_fax_session_details *session_details_new(void)
{
	struct ast_fax_session_details *d;
	struct fax_options options;

	if (!(d = ao2_alloc(sizeof(*d), destroy_session_details))) {
		return NULL;
	}

	if (ast_string_field_init(d, 512)) {
		ao2_ref(d, -1);
		return NULL;
	}

	get_general_options(&options);

	AST_LIST_HEAD_INIT_NOLOCK(&d->documents);

	/* These options need to be set to the configured default and may be overridden by
 	 * SendFAX, ReceiveFAX, or FAXOPT */
	d->option.request_t38 = AST_FAX_OPTFLAG_FALSE;
	d->option.send_cng = AST_FAX_OPTFLAG_FALSE;
	d->option.send_ced = AST_FAX_OPTFLAG_FALSE;
	d->option.ecm = options.ecm;
	d->option.statusevents = options.statusevents;
	d->modems = options.modems;
	d->minrate = options.minrate;
	d->maxrate = options.maxrate;
	d->gateway_id = -1;
	d->faxdetect_id = -1;
	d->gateway_timeout = 0;

	return d;
}

static struct ast_control_t38_parameters our_t38_parameters = {
	.version = 0,
	.max_ifp = 400,
	.rate = AST_T38_RATE_14400,
	.rate_management = AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF,
};

static void t38_parameters_ast_to_fax(struct ast_fax_t38_parameters *dst, const struct ast_control_t38_parameters *src)
{
	dst->version = src->version;
	dst->max_ifp = src->max_ifp;
	dst->rate = src->rate;
	dst->rate_management = src->rate_management;
	dst->fill_bit_removal = src->fill_bit_removal;
	dst->transcoding_mmr = src->transcoding_mmr;
	dst->transcoding_jbig = src->transcoding_jbig;
}

static void t38_parameters_fax_to_ast(struct ast_control_t38_parameters *dst, const struct ast_fax_t38_parameters *src)
{
	dst->version = src->version;
	dst->max_ifp = src->max_ifp;
	dst->rate = src->rate;
	dst->rate_management = src->rate_management;
	dst->fill_bit_removal = src->fill_bit_removal;
	dst->transcoding_mmr = src->transcoding_mmr;
	dst->transcoding_jbig = src->transcoding_jbig;
}

/*! \brief returns a reference counted details structure from the channel's fax datastore.  If the datastore
 * does not exist it will be created */
static struct ast_fax_session_details *find_or_create_details(struct ast_channel *chan)
{
	struct ast_fax_session_details *details;
	struct ast_datastore *datastore;

	if ((details = find_details(chan))) {
		return details;
	}
	/* channel does not have one so we must create one */
	if (!(details = session_details_new())) {
		ast_log(LOG_WARNING, "channel '%s' can't get a FAX details structure for the datastore!\n", ast_channel_name(chan));
		return NULL;
	}
	if (!(datastore = ast_datastore_alloc(&fax_datastore, NULL))) {
		ao2_ref(details, -1);
		ast_log(LOG_WARNING, "channel '%s' can't get a datastore!\n", ast_channel_name(chan));
		return NULL;
	}
	/* add the datastore to the channel and increment the refcount */
	datastore->data = details;

	/* initialize default T.38 parameters */
	t38_parameters_ast_to_fax(&details->our_t38_parameters, &our_t38_parameters);
	t38_parameters_ast_to_fax(&details->their_t38_parameters, &our_t38_parameters);

	ao2_ref(details, 1);
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
	return details;
}

unsigned int ast_fax_maxrate(void)
{
	struct fax_options options;
	get_general_options(&options);

	return options.maxrate;
}

unsigned int ast_fax_minrate(void)
{
	struct fax_options options;
	get_general_options(&options);

	return options.minrate;
}

static int update_modem_bits(enum ast_fax_modems *bits, const char *value)
{
	char *m[5], *tok, *v = (char *)value;
	int i = 0, j;

	if (!strchr(v, ',')) {
		m[i++] = v;
		m[i] = NULL;
	} else {
		tok = strtok(v, ", ");
		while (tok && i < ARRAY_LEN(m) - 1) {
			m[i++] = tok;
			tok = strtok(NULL, ", ");
		}
		m[i] = NULL;
	}

	*bits = 0;
	for (j = 0; j < i; j++) {
		if (!strcasecmp(m[j], "v17")) {
			*bits |= AST_FAX_MODEM_V17;
		} else if (!strcasecmp(m[j], "v27")) {
			*bits |= AST_FAX_MODEM_V27;
		} else if (!strcasecmp(m[j], "v29")) {
			*bits |= AST_FAX_MODEM_V29;
		} else if (!strcasecmp(m[j], "v34")) {
			*bits |= AST_FAX_MODEM_V34;
		} else {
			ast_log(LOG_WARNING, "ignoring invalid modem setting: '%s', valid options {v17 | v27 | v29 | v34}\n", m[j]);
		}
	}
	return 0;
}
static char *ast_fax_caps_to_str(enum ast_fax_capabilities caps, char *buf, size_t bufsize)
{
	char *out = buf;
	size_t size = bufsize;
	int first = 1;

	if (caps & AST_FAX_TECH_SEND) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "SEND");
		first = 0;
	}
	if (caps & AST_FAX_TECH_RECEIVE) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "RECEIVE");
		first = 0;
	}
	if (caps & AST_FAX_TECH_AUDIO) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "AUDIO");
		first = 0;
	}
	if (caps & AST_FAX_TECH_T38) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "T38");
		first = 0;
	}
	if (caps & AST_FAX_TECH_MULTI_DOC) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "MULTI_DOC");
		first = 0;
	}
	if (caps & AST_FAX_TECH_GATEWAY) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "GATEWAY");
		first = 0;
	}
	if (caps & AST_FAX_TECH_V21_DETECT) {
		if (!first) {
			ast_build_string(&buf, &size, ",");
		}
		ast_build_string(&buf, &size, "V21");
		first = 0;
	}

	return out;
}

static int ast_fax_modem_to_str(enum ast_fax_modems bits, char *tbuf, size_t bufsize)
{
	int count = 0;

	if (bits & AST_FAX_MODEM_V17) {
		strcat(tbuf, "V17");
		count++;
	}
	if (bits & AST_FAX_MODEM_V27) {
		if (count) {
			strcat(tbuf, ",");
		}
		strcat(tbuf, "V27");
		count++;
	}
	if (bits & AST_FAX_MODEM_V29) {
		if (count) {
			strcat(tbuf, ",");
		}
		strcat(tbuf, "V29");
		count++;
	}
	if (bits & AST_FAX_MODEM_V34) {
		if (count) {
			strcat(tbuf, ",");
		}
		strcat(tbuf, "V34");
		count++;
	}

	return 0;
}

static int check_modem_rate(enum ast_fax_modems modems, unsigned int rate)
{
	switch (rate) {
	case 2400:
		if (!(modems & (AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 4800:
		if (!(modems & (AST_FAX_MODEM_V27 | AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 7200:
		if (!(modems & (AST_FAX_MODEM_V17 | AST_FAX_MODEM_V29 | AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 9600:
		if (!(modems & (AST_FAX_MODEM_V17 | AST_FAX_MODEM_V27 | AST_FAX_MODEM_V29 | AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 12000:
	case 14400:
		if (!(modems & (AST_FAX_MODEM_V17 | AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 28800:
	case 33600:
		if (!(modems & AST_FAX_MODEM_V34)) {
			return 1;
		}
		break;
	default:
		/* this should never happen */
		return 1;
	}

	return 0;
}

/*! \brief register a FAX technology module */
int ast_fax_tech_register(struct ast_fax_tech *tech)
{
	struct fax_module *fax;

	if (!(fax = ast_calloc(1, sizeof(*fax)))) {
		return -1;
	}
	fax->tech = tech;
	AST_RWLIST_WRLOCK(&faxmodules);
	AST_RWLIST_INSERT_TAIL(&faxmodules, fax, list);
	AST_RWLIST_UNLOCK(&faxmodules);
	ast_module_ref(ast_module_info->self);

	ast_verb(3, "Registered handler for '%s' (%s)\n", fax->tech->type, fax->tech->description);

	return 0;
}

/*! \brief unregister a FAX technology module */
void ast_fax_tech_unregister(struct ast_fax_tech *tech)
{
	struct fax_module *fax;

	ast_verb(3, "Unregistering FAX module type '%s'\n", tech->type);

	AST_RWLIST_WRLOCK(&faxmodules);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&faxmodules, fax, list) {
		if (fax->tech != tech) {
			continue;
		}
		AST_RWLIST_REMOVE_CURRENT(list);
		ast_module_unref(ast_module_info->self);
		ast_free(fax);
		ast_verb(4, "Unregistered FAX module type '%s'\n", tech->type);
		break;
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&faxmodules);
}

/*! \brief convert a ast_fax_state to a string */
const char *ast_fax_state_to_str(enum ast_fax_state state)
{
	switch (state) {
	case AST_FAX_STATE_UNINITIALIZED:
		return "Uninitialized";
	case AST_FAX_STATE_INITIALIZED:
		return "Initialized";
	case AST_FAX_STATE_OPEN:
		return "Open";
	case AST_FAX_STATE_ACTIVE:
		return "Active";
	case AST_FAX_STATE_COMPLETE:
		return "Complete";
	case AST_FAX_STATE_RESERVED:
		return "Reserved";
	case AST_FAX_STATE_INACTIVE:
		return "Inactive";
	default:
		ast_log(LOG_WARNING, "unhandled FAX state: %u\n", state);
		return "Unknown";
	}
}

void ast_fax_log(int level, const char *file, const int line, const char *function, const char *msg)
{
	if (fax_logger_level != -1) {
		ast_log_dynamic_level(fax_logger_level, "%s", msg);
	} else {
		ast_log(level, file, line, function, "%s", msg);
	}
}

/*! \brief convert a rate string to a rate */
static unsigned int fax_rate_str_to_int(const char *ratestr)
{
	int rate;

	if (sscanf(ratestr, "%d", &rate) != 1) {
		ast_log(LOG_ERROR, "failed to sscanf '%s' to rate\n", ratestr);
		return 0;
	}
	switch (rate) {
	case 2400:
	case 4800:
	case 7200:
	case 9600:
	case 12000:
	case 14400:
	case 28800:
	case 33600:
		return rate;
	default:
		ast_log(LOG_WARNING, "ignoring invalid rate '%s'.  Valid options are {2400 | 4800 | 7200 | 9600 | 12000 | 14400 | 28800 | 33600}\n", ratestr);
		return 0;
	}
}

/*! \brief Release a session token.
 * \param s a session returned from fax_session_reserve()
 * \param token a token generated from fax_session_reserve()
 *
 * This function releases the given token and marks the given session as no
 * longer reserved. It is safe to call on a session that is not actually
 * reserved and with a NULL token. This is so that sessions returned by
 * technologies that do not support reserved sessions don't require extra logic
 * to handle.
 *
 * \note This function DOES NOT release the given fax session, only the given
 * token.
 */
static void fax_session_release(struct ast_fax_session *s, struct ast_fax_tech_token *token)
{
	if (token) {
		s->tech->release_token(token);
	}

	if (s->state == AST_FAX_STATE_RESERVED) {
		ast_atomic_fetchadd_int(&faxregistry.reserved_sessions, -1);
		s->state = AST_FAX_STATE_INACTIVE;
	}
}

/*! \brief destroy a FAX session structure */
static void destroy_session(void *session)
{
	struct ast_fax_session *s = session;

	if (s->tech) {
		fax_session_release(s, NULL);
		if (s->tech_pvt) {
			s->tech->destroy_session(s);
		}
		ast_module_unref(s->tech->module);
	}

	if (s->details) {
		if (s->details->caps & AST_FAX_TECH_GATEWAY) {
			s->details->caps &= ~AST_FAX_TECH_GATEWAY;
		}
		ao2_ref(s->details, -1);
	}

	if (s->debug_info) {
		ast_dsp_free(s->debug_info->dsp);
		ast_free(s->debug_info);
	}

	if (s->smoother) {
		ast_smoother_free(s->smoother);
	}

	if (s->state != AST_FAX_STATE_INACTIVE) {
		ast_atomic_fetchadd_int(&faxregistry.active_sessions, -1);
	}

	ast_free(s->channame);
	ast_free(s->chan_uniqueid);
}

/*! \brief Reserve a fax session.
 * \param details the fax session details
 * \param token a pointer to a place to store a token to be passed to fax_session_new() later
 *
 * This function reserves a fax session for use later. If the selected fax
 * technology does not support reserving sessions a session will still be
 * returned but token will not be set.
 *
 * \note The reference returned by this function does not get consumed by
 * fax_session_new() and must always be dereferenced separately.
 *
 * \return NULL or an uninitialized and possibly reserved session
 */
static struct ast_fax_session *fax_session_reserve(struct ast_fax_session_details *details, struct ast_fax_tech_token **token)
{
	struct ast_fax_session *s;
	struct fax_module *faxmod;

	if (!(s = ao2_alloc(sizeof(*s), destroy_session))) {
		return NULL;
	}

	s->state = AST_FAX_STATE_INACTIVE;
	s->details = details;
	ao2_ref(s->details, 1);

	/* locate a FAX technology module that can handle said requirements
	 * Note: the requirements have not yet been finalized as T.38
	 * negotiation has not yet occured. */
	AST_RWLIST_RDLOCK(&faxmodules);
	AST_RWLIST_TRAVERSE(&faxmodules, faxmod, list) {
		if ((faxmod->tech->caps & details->caps) != details->caps) {
			continue;
		}
		ast_debug(4, "Reserving a FAX session from '%s'.\n", faxmod->tech->description);
		ast_module_ref(faxmod->tech->module);
		s->tech = faxmod->tech;
		break;
	}
	AST_RWLIST_UNLOCK(&faxmodules);

	if (!faxmod) {
		char caps[128] = "";
		ast_log(LOG_ERROR, "Could not locate a FAX technology module with capabilities (%s)\n", ast_fax_caps_to_str(details->caps, caps, sizeof(caps)));
		ao2_ref(s, -1);
		return NULL;
	}

	if (!s->tech->reserve_session) {
		ast_debug(1, "Selected FAX technology module (%s) does not support reserving sessions.\n", s->tech->description);
		return s;
	}

	if (!(*token = s->tech->reserve_session(s))) {
		ao2_ref(s, -1);
		return NULL;
	}

	s->state = AST_FAX_STATE_RESERVED;
	ast_atomic_fetchadd_int(&faxregistry.reserved_sessions, 1);

	return s;
}

/*! \brief create a FAX session
 *
 * \param details details for the session
 * \param chan the channel the session will run on
 * \param reserved a reserved session to base this session on (can be NULL)
 * \param token the token for a reserved session (can be NULL)
 *
 * Create a new fax session based on the given details structure.
 *
 * \note The given token is always consumed (by tech->new_session() or by
 * fax_session_release() in the event of a failure). The given reference to a
 * reserved session is never consumed and must be dereferenced separately from
 * the reference returned by this function.
 *
 * \return NULL or a reference to a new fax session
 */
static struct ast_fax_session *fax_session_new(struct ast_fax_session_details *details, struct ast_channel *chan, struct ast_fax_session *reserved, struct ast_fax_tech_token *token)
{
	struct ast_fax_session *s = NULL;
	struct fax_module *faxmod;

	if (reserved) {
		s = reserved;
		ao2_ref(reserved, +1);
		ao2_unlink(faxregistry.container, reserved);

		/* NOTE: we don't consume the reference to the reserved
		 * session. The session returned from fax_session_new() is a
		 * new reference and must be derefed in addition to the
		 * reserved session.
		 */

		if (s->state == AST_FAX_STATE_RESERVED) {
			ast_atomic_fetchadd_int(&faxregistry.reserved_sessions, -1);
			s->state = AST_FAX_STATE_UNINITIALIZED;
		}
	}

	if (!s && !(s = ao2_alloc(sizeof(*s), destroy_session))) {
		return NULL;
	}

	ast_atomic_fetchadd_int(&faxregistry.active_sessions, 1);
	s->state = AST_FAX_STATE_UNINITIALIZED;

	if (details->option.debug && (details->caps & AST_FAX_TECH_AUDIO)) {
		if (!(s->debug_info = ast_calloc(1, sizeof(*(s->debug_info))))) {
			fax_session_release(s, token);
			ao2_ref(s, -1);
			return NULL;
		}
		if (!(s->debug_info->dsp = ast_dsp_new())) {
			ast_free(s->debug_info);
			s->debug_info = NULL;
			fax_session_release(s, token);
			ao2_ref(s, -1);
			return NULL;
		}
		ast_dsp_set_threshold(s->debug_info->dsp, 128);
	}

	if (!(s->channame = ast_strdup(ast_channel_name(chan)))) {
		fax_session_release(s, token);
		ao2_ref(s, -1);
		return NULL;
	}

	if (!(s->chan_uniqueid = ast_strdup(ast_channel_uniqueid(chan)))) {
		fax_session_release(s, token);
		ao2_ref(s, -1);
		return NULL;
	}

	s->chan = chan;
	if (!s->details) {
		s->details = details;
		ao2_ref(s->details, 1);
	}

	details->id = s->id = ast_atomic_fetchadd_int(&faxregistry.nextsessionname, 1);

	if (!token) {
		/* locate a FAX technology module that can handle said requirements */
		AST_RWLIST_RDLOCK(&faxmodules);
		AST_RWLIST_TRAVERSE(&faxmodules, faxmod, list) {
			if ((faxmod->tech->caps & details->caps) != details->caps) {
				continue;
			}
			ast_debug(4, "Requesting a new FAX session from '%s'.\n", faxmod->tech->description);
			ast_module_ref(faxmod->tech->module);
			if (reserved) {
				/* Balance module ref from reserved session */
				ast_module_unref(reserved->tech->module);
			}
			s->tech = faxmod->tech;
			break;
		}
		AST_RWLIST_UNLOCK(&faxmodules);

		if (!faxmod) {
			char caps[128] = "";
			ast_log(LOG_ERROR, "Could not locate a FAX technology module with capabilities (%s)\n", ast_fax_caps_to_str(details->caps, caps, sizeof(caps)));
			ao2_ref(s, -1);
			return NULL;
		}
	}

	if (!(s->tech_pvt = s->tech->new_session(s, token))) {
		ast_log(LOG_ERROR, "FAX session failed to initialize.\n");
		ao2_ref(s, -1);
		return NULL;
	}
	/* link the session to the session container */
	if (!(ao2_link(faxregistry.container, s))) {
		ast_log(LOG_ERROR, "failed to add FAX session '%u' to container.\n", s->id);
		ao2_ref(s, -1);
		return NULL;
	}
	ast_debug(4, "channel '%s' using FAX session '%u'\n", s->channame, s->id);

	return s;
}

static void get_manager_event_info(struct ast_channel *chan, struct manager_event_info *info)
{
	pbx_substitute_variables_helper(chan, "${CONTEXT}", info->context, sizeof(info->context));
	pbx_substitute_variables_helper(chan, "${EXTEN}", info->exten, sizeof(info->exten));
	pbx_substitute_variables_helper(chan, "${CALLERID(num)}", info->cid, sizeof(info->cid));
}


/* \brief Generate a string of filenames using the given prefix and separator.
 * \param details the fax session details
 * \param prefix the prefix to each filename
 * \param separator the separator between filenames
 *
 * This function generates a string of filenames from the given details
 * structure and using the given prefix and separator.
 *
 * \retval NULL there was an error generating the string
 * \return the string generated string
 */
static char *generate_filenames_string(struct ast_fax_session_details *details, char *prefix, char *separator)
{
	char *filenames, *c;
	size_t size = 0;
	int first = 1;
	struct ast_fax_document *doc;

	/* don't process empty lists */
	if (AST_LIST_EMPTY(&details->documents)) {
		return NULL;
	}

	/* Calculate the total length of all of the file names */
	AST_LIST_TRAVERSE(&details->documents, doc, next) {
		size += strlen(separator) + strlen(prefix) + strlen(doc->filename);
	}
	size += 1; /* add space for the terminating null */

	if (!(filenames = ast_malloc(size))) {
		return NULL;
	}
	c = filenames;

	ast_build_string(&c, &size, "%s%s", prefix, AST_LIST_FIRST(&details->documents)->filename);
	AST_LIST_TRAVERSE(&details->documents, doc, next) {
		if (first) {
			first = 0;
			continue;
		}

		ast_build_string(&c, &size, "%s%s%s", separator, prefix, doc->filename);
	}

	return filenames;
}

/*! \brief send a FAX status manager event */
static int report_fax_status(struct ast_channel *chan, struct ast_fax_session_details *details, const char *status)
{
	char *filenames = generate_filenames_string(details, "FileName: ", "\r\n");

	ast_channel_lock(chan);
	if (details->option.statusevents) {
		struct manager_event_info info;

		get_manager_event_info(chan, &info);
		manager_event(EVENT_FLAG_CALL,
			      "FAXStatus",
			      "Operation: %s\r\n"
			      "Status: %s\r\n"
			      "Channel: %s\r\n"
			      "Context: %s\r\n"
			      "Exten: %s\r\n"
			      "CallerID: %s\r\n"
			      "LocalStationID: %s\r\n"
			      "%s%s",
			      (details->caps & AST_FAX_TECH_GATEWAY) ? "gateway" : (details->caps & AST_FAX_TECH_RECEIVE) ? "receive" : "send",
			      status,
			      ast_channel_name(chan),
			      info.context,
			      info.exten,
			      info.cid,
			      details->localstationid,
			      S_OR(filenames, ""),
			      filenames ? "\r\n" : "");
	}
	ast_channel_unlock(chan);

	if (filenames) {
		ast_free(filenames);
	}

	return 0;
}

/*! \brief Set fax related channel variables. */
static void set_channel_variables(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	char buf[10];
	pbx_builtin_setvar_helper(chan, "FAXSTATUS", S_OR(details->result, NULL));
	pbx_builtin_setvar_helper(chan, "FAXERROR", S_OR(details->error, NULL));
	pbx_builtin_setvar_helper(chan, "FAXSTATUSSTRING", S_OR(details->resultstr, NULL));
	pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", S_OR(details->remotestationid, NULL));
	pbx_builtin_setvar_helper(chan, "LOCALSTATIONID", S_OR(details->localstationid, NULL));
	pbx_builtin_setvar_helper(chan, "FAXBITRATE", S_OR(details->transfer_rate, NULL));
	pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", S_OR(details->resolution, NULL));

	snprintf(buf, sizeof(buf), "%u", details->pages_transferred);
	pbx_builtin_setvar_helper(chan, "FAXPAGES", buf);
}

#define GENERIC_FAX_EXEC_SET_VARS(fax, chan, errorstr, reason) \
	do {	\
		if (ast_strlen_zero(fax->details->result)) \
			ast_string_field_set(fax->details, result, "FAILED"); \
		if (ast_strlen_zero(fax->details->resultstr)) \
			ast_string_field_set(fax->details, resultstr, reason); \
		if (ast_strlen_zero(fax->details->error)) \
			ast_string_field_set(fax->details, error, errorstr); \
		set_channel_variables(chan, fax->details); \
	} while (0)

#define GENERIC_FAX_EXEC_ERROR_QUIET(fax, chan, errorstr, reason) \
	do {	\
		GENERIC_FAX_EXEC_SET_VARS(fax, chan, errorstr, reason); \
	} while (0)

#define GENERIC_FAX_EXEC_ERROR(fax, chan, errorstr, reason)	\
	do {	\
		ast_log(LOG_ERROR, "channel '%s' FAX session '%u' failure, reason: '%s' (%s)\n", ast_channel_name(chan), fax->id, reason, errorstr); \
		GENERIC_FAX_EXEC_ERROR_QUIET(fax, chan, errorstr, reason); \
	} while (0)

static int set_fax_t38_caps(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	switch (ast_channel_get_t38_state(chan)) {
	case T38_STATE_UNKNOWN:
		details->caps |= AST_FAX_TECH_T38;
		break;
	case T38_STATE_REJECTED:
	case T38_STATE_UNAVAILABLE:
		details->caps |= AST_FAX_TECH_AUDIO;
		break;
	case T38_STATE_NEGOTIATED:
		/* already in T.38 mode? This should not happen. */
	case T38_STATE_NEGOTIATING: {
		/* the other end already sent us a T.38 reinvite, so we need to prod the channel
		 * driver into resending their parameters to us if it supports doing so... if
		 * not, we can't proceed, because we can't create a proper reply without them.
		 * if it does work, the channel driver will send an AST_CONTROL_T38_PARAMETERS
		 * with a request of AST_T38_REQUEST_NEGOTIATE, which will be read by the function
		 * that gets called after this one completes
		 */
		struct ast_control_t38_parameters parameters = { .request_response = AST_T38_REQUEST_PARMS, };
		if (ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &parameters, sizeof(parameters)) != AST_T38_REQUEST_PARMS) {
			ast_log(LOG_ERROR, "channel '%s' is in an unsupported T.38 negotiation state, cannot continue.\n", ast_channel_name(chan));
			return -1;
		}
		details->caps |= AST_FAX_TECH_T38;
		break;
	}
	default:
		ast_log(LOG_ERROR, "channel '%s' is in an unsupported T.38 negotiation state, cannot continue.\n", ast_channel_name(chan));
		return -1;
	}

	return 0;
}

static int disable_t38(struct ast_channel *chan)
{
	int timeout_ms;
	struct ast_frame *frame = NULL;
	struct ast_control_t38_parameters t38_parameters = { .request_response = AST_T38_REQUEST_TERMINATE, };
	struct timeval start;
	int ms;

	ast_debug(1, "Shutting down T.38 on %s\n", ast_channel_name(chan));
	if (ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) != 0) {
		ast_debug(1, "error while disabling T.38 on channel '%s'\n", ast_channel_name(chan));
		return -1;
	}

	/* wait up to five seconds for negotiation to complete */
	timeout_ms = 5000;
	start = ast_tvnow();
	while ((ms = ast_remaining_ms(start, timeout_ms))) {
		ms = ast_waitfor(chan, ms);

		if (ms == 0) {
			break;
		}
		if (ms < 0) {
			ast_debug(1, "error while disabling T.38 on channel '%s'\n", ast_channel_name(chan));
			return -1;
		}

		if (!(frame = ast_read(chan))) {
			return -1;
		}
		if ((frame->frametype == AST_FRAME_CONTROL) &&
		    (frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
		    (frame->datalen == sizeof(t38_parameters))) {
			struct ast_control_t38_parameters *parameters = frame->data.ptr;

			switch (parameters->request_response) {
			case AST_T38_TERMINATED:
				ast_debug(1, "Shut down T.38 on %s\n", ast_channel_name(chan));
				break;
			case AST_T38_REFUSED:
				ast_log(LOG_WARNING, "channel '%s' refused to disable T.38\n", ast_channel_name(chan));
				ast_frfree(frame);
				return -1;
			default:
				ast_log(LOG_ERROR, "channel '%s' failed to disable T.38\n", ast_channel_name(chan));
				ast_frfree(frame);
				return -1;
			}
			ast_frfree(frame);
			break;
		}
		ast_frfree(frame);
	}

	if (ms == 0) { /* all done, nothing happened */
		ast_debug(1, "channel '%s' timed-out during T.38 shutdown\n", ast_channel_name(chan));
	}

	return 0;
}

/*! \brief this is the generic FAX session handling function */
static int generic_fax_exec(struct ast_channel *chan, struct ast_fax_session_details *details, struct ast_fax_session *reserved, struct ast_fax_tech_token *token)
{
	int ms;
	int timeout = RES_FAX_TIMEOUT;
	int chancount;
	unsigned int expected_frametype = -1;
	union ast_frame_subclass expected_framesubclass = { .integer = -1 };
	unsigned int t38negotiated = (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED);
	struct ast_control_t38_parameters t38_parameters;
	const char *tempvar;
	struct ast_fax_session *fax = NULL;
	struct ast_frame *frame = NULL;
	struct ast_channel *c = chan;
	struct ast_format orig_write_format;
	struct ast_format orig_read_format;
	int remaining_time;
	struct timeval start;

	ast_format_clear(&orig_write_format);
	ast_format_clear(&orig_read_format);
	chancount = 1;

	/* create the FAX session */
	if (!(fax = fax_session_new(details, chan, reserved, token))) {
		ast_log(LOG_ERROR, "Can't create a FAX session, FAX attempt failed.\n");
		report_fax_status(chan, details, "No Available Resource");
		return -1;
	}

	ast_channel_lock(chan);
	/* update session details */
	if (ast_strlen_zero(details->headerinfo) && (tempvar = pbx_builtin_getvar_helper(chan, "LOCALHEADERINFO"))) {
		ast_string_field_set(details, headerinfo, tempvar);
	}
	if (ast_strlen_zero(details->localstationid)) {
		tempvar = pbx_builtin_getvar_helper(chan, "LOCALSTATIONID");
		ast_string_field_set(details, localstationid, tempvar ? tempvar : "unknown");
	}
	ast_channel_unlock(chan);

	report_fax_status(chan, details, "Allocating Resources");

	if (details->caps & AST_FAX_TECH_AUDIO) {
		expected_frametype = AST_FRAME_VOICE;;
		ast_format_set(&expected_framesubclass.format, AST_FORMAT_SLINEAR, 0);
		ast_format_copy(&orig_write_format, ast_channel_writeformat(chan));
		if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR) < 0) {
			ast_log(LOG_ERROR, "channel '%s' failed to set write format to signed linear'.\n", ast_channel_name(chan));
 			ao2_lock(faxregistry.container);
 			ao2_unlink(faxregistry.container, fax);
 			ao2_unlock(faxregistry.container);
 			ao2_ref(fax, -1);
			ast_channel_unlock(chan);
			return -1;
		}
		ast_format_copy(&orig_read_format, ast_channel_readformat(chan));
		if (ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR) < 0) {
			ast_log(LOG_ERROR, "channel '%s' failed to set read format to signed linear.\n", ast_channel_name(chan));
 			ao2_lock(faxregistry.container);
 			ao2_unlink(faxregistry.container, fax);
 			ao2_unlock(faxregistry.container);
 			ao2_ref(fax, -1);
			ast_channel_unlock(chan);
			return -1;
		}
		if (fax->smoother) {
			ast_smoother_free(fax->smoother);
			fax->smoother = NULL;
		}
		if (!(fax->smoother = ast_smoother_new(320))) {
			ast_log(LOG_WARNING, "Channel '%s' FAX session '%u' failed to obtain a smoother.\n", ast_channel_name(chan), fax->id);
		}
	} else {
		expected_frametype = AST_FRAME_MODEM;
		expected_framesubclass.integer = AST_MODEM_T38;
	}

	if (fax->debug_info) {
		fax->debug_info->base_tv = ast_tvnow();
	}

	/* reset our result fields just in case the fax tech driver wants to
	 * set custom error messages */
	ast_string_field_set(details, result, "");
	ast_string_field_set(details, resultstr, "");
	ast_string_field_set(details, error, "");
	set_channel_variables(chan, details);

	if (fax->tech->start_session(fax) < 0) {
		GENERIC_FAX_EXEC_ERROR(fax, chan, "INIT_ERROR", "failed to start FAX session");
	}

	report_fax_status(chan, details, "FAX Transmission In Progress");

	ast_debug(5, "channel %s will wait on FAX fd %d\n", ast_channel_name(chan), fax->fd);

	/* handle frames for the session */
	remaining_time = timeout;
	start = ast_tvnow();
	while (remaining_time > 0) {
		struct ast_channel *ready_chan;
		int ofd, exception;

		ms = 1000;
		errno = 0;
		ready_chan = ast_waitfor_nandfds(&c, chancount, &fax->fd, 1, &exception, &ofd, &ms);
		if (ready_chan) {
			if (!(frame = ast_read(chan))) {
				/* the channel is probably gone, so lets stop polling on it and let the
 				 * FAX session complete before we exit the application.  if needed,
 				 * send the FAX stack silence so the modems can finish their session without
 				 * any problems */
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", ast_channel_name(chan));
				GENERIC_FAX_EXEC_SET_VARS(fax, chan, "HANGUP", "remote channel hungup");
				c = NULL;
				chancount = 0;
				remaining_time = ast_remaining_ms(start, timeout);
				fax->tech->cancel_session(fax);
				if (fax->tech->generate_silence) {
					fax->tech->generate_silence(fax);
				}
				continue;
			}

			if ((frame->frametype == AST_FRAME_CONTROL) &&
			    (frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
			    (frame->datalen == sizeof(t38_parameters))) {
				unsigned int was_t38 = t38negotiated;
				struct ast_control_t38_parameters *parameters = frame->data.ptr;

				switch (parameters->request_response) {
				case AST_T38_REQUEST_NEGOTIATE:
					/* the other end has requested a switch to T.38, so reply that we are willing, if we can
					 * do T.38 as well
					 */
					t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
					t38_parameters.request_response = (details->caps & AST_FAX_TECH_T38) ? AST_T38_NEGOTIATED : AST_T38_REFUSED;
					ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
					break;
				case AST_T38_NEGOTIATED:
					t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
					t38negotiated = 1;
					break;
				default:
					break;
				}
				if (t38negotiated && !was_t38) {
					fax->tech->switch_to_t38(fax);
					details->caps &= ~AST_FAX_TECH_AUDIO;
					expected_frametype = AST_FRAME_MODEM;
					expected_framesubclass.integer = AST_MODEM_T38;
					if (fax->smoother) {
						ast_smoother_free(fax->smoother);
						fax->smoother = NULL;
					}

					report_fax_status(chan, details, "T.38 Negotiated");

					ast_verb(3, "Channel '%s' switched to T.38 FAX session '%u'.\n", ast_channel_name(chan), fax->id);
				}
			} else if ((frame->frametype == expected_frametype) &&
				   (!memcmp(&frame->subclass, &expected_framesubclass, sizeof(frame->subclass)))) {
				struct ast_frame *f;

				if (fax->smoother) {
					/* push the frame into a smoother */
					if (ast_smoother_feed(fax->smoother, frame) < 0) {
						GENERIC_FAX_EXEC_ERROR(fax, chan, "UNKNOWN", "Failed to feed the smoother");
					}
					while ((f = ast_smoother_read(fax->smoother)) && (f->data.ptr)) {
						if (fax->debug_info) {
							debug_check_frame_for_silence(fax, 1, f);
						}
						/* write the frame to the FAX stack */
						fax->tech->write(fax, f);
						fax->frames_received++;
						if (f != frame) {
							ast_frfree(f);
						}
					}
				} else {
					/* write the frame to the FAX stack */
					fax->tech->write(fax, frame);
					fax->frames_received++;
				}
				start = ast_tvnow();
			}
			ast_frfree(frame);
		} else if (ofd == fax->fd) {
			/* read a frame from the FAX stack and send it out the channel.
 			 * the FAX stack will return a NULL if the FAX session has already completed */
			if (!(frame = fax->tech->read(fax))) {
				break;
			}

			if (fax->debug_info && (frame->frametype == AST_FRAME_VOICE)) {
				debug_check_frame_for_silence(fax, 0, frame);
			}

			ast_write(chan, frame);
			fax->frames_sent++;
			ast_frfree(frame);
			start = ast_tvnow();
		} else {
			if (ms && (ofd < 0)) {
				if ((errno == 0) || (errno == EINTR)) {
					remaining_time = ast_remaining_ms(start, timeout);
					if (remaining_time <= 0)
						GENERIC_FAX_EXEC_ERROR(fax, chan, "TIMEOUT", "fax session timed-out");
					continue;
				} else {
					ast_log(LOG_WARNING, "something bad happened while channel '%s' was polling.\n", ast_channel_name(chan));
					GENERIC_FAX_EXEC_ERROR(fax, chan, "UNKNOWN", "error polling data");
					break;
				}
			} else {
				/* nothing happened */
				remaining_time = ast_remaining_ms(start, timeout);
				if (remaining_time <= 0) {
					GENERIC_FAX_EXEC_ERROR(fax, chan, "TIMEOUT", "fax session timed-out");
					break;
				}
			}
		}
	}
	ast_debug(3, "channel '%s' - event loop stopped { timeout: %d, remaining_time: %d }\n", ast_channel_name(chan), timeout, remaining_time);

	set_channel_variables(chan, details);

	ast_atomic_fetchadd_int(&faxregistry.fax_complete, 1);
	if (!strcasecmp(details->result, "FAILED")) {
		ast_atomic_fetchadd_int(&faxregistry.fax_failures, 1);
	}

	if (fax) {
		ao2_lock(faxregistry.container);
		ao2_unlink(faxregistry.container, fax);
		ao2_unlock(faxregistry.container);
		ao2_ref(fax, -1);
	}

	/* if the channel is still alive, and we changed its read/write formats,
	 * restore them now
	 */
	if (chancount) {
		if (orig_read_format.id) {
			ast_set_read_format(chan, &orig_read_format);
		}
		if (orig_write_format.id) {
			ast_set_write_format(chan, &orig_write_format);
		}
	}

	/* return the chancount so the calling function can determine if the channel hungup during this FAX session or not */
	return chancount;
}

static int receivefax_t38_init(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	int timeout_ms;
	struct ast_frame *frame = NULL;
	struct ast_control_t38_parameters t38_parameters;
	struct timeval start;
	int ms;

	/* don't send any audio if we've already received a T.38 reinvite */
	if (ast_channel_get_t38_state(chan) != T38_STATE_NEGOTIATING) {
		/* generate 3 seconds of CED */
		if (ast_playtones_start(chan, 1024, "!2100/3000", 1)) {
			ast_log(LOG_ERROR, "error generating CED tone on %s\n", ast_channel_name(chan));
			return -1;
		}

		timeout_ms = 3000;
		start = ast_tvnow();
		while ((ms = ast_remaining_ms(start, timeout_ms))) {
			ms = ast_waitfor(chan, ms);

			if (ms < 0) {
				ast_log(LOG_ERROR, "error while generating CED tone on %s\n", ast_channel_name(chan));
				ast_playtones_stop(chan);
				return -1;
			}

			if (ms == 0) { /* all done, nothing happened */
				break;
			}

			if (!(frame = ast_read(chan))) {
				ast_log(LOG_ERROR, "error reading frame while generating CED tone on %s\n", ast_channel_name(chan));
				ast_playtones_stop(chan);
				return -1;
			}

			if ((frame->frametype == AST_FRAME_CONTROL) &&
			    (frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
			    (frame->datalen == sizeof(t38_parameters))) {
				struct ast_control_t38_parameters *parameters = frame->data.ptr;

				switch (parameters->request_response) {
				case AST_T38_REQUEST_NEGOTIATE:
					/* the other end has requested a switch to T.38, so reply that we are willing, if we can
					 * do T.38 as well
					 */
					t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
					t38_parameters.request_response = (details->caps & AST_FAX_TECH_T38) ? AST_T38_NEGOTIATED : AST_T38_REFUSED;
					ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
					ast_playtones_stop(chan);
					break;
				case AST_T38_NEGOTIATED:
					ast_debug(1, "Negotiated T.38 for receive on %s\n", ast_channel_name(chan));
					t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
					details->caps &= ~AST_FAX_TECH_AUDIO;
					report_fax_status(chan, details, "T.38 Negotiated");
					break;
				default:
					break;
				}
			}
			ast_frfree(frame);
		}

		ast_playtones_stop(chan);
	}

	/* if T.38 was negotiated, we are done initializing */
	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		return 0;
	}

	/* request T.38 */
	ast_debug(1, "Negotiating T.38 for receive on %s\n", ast_channel_name(chan));

	/* wait up to five seconds for negotiation to complete */
	timeout_ms = 5000;

	/* set parameters based on the session's parameters */
	t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
	t38_parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
	if ((ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) != 0)) {
		return -1;
	}

	start = ast_tvnow();
	while ((ms = ast_remaining_ms(start, timeout_ms))) {
		int break_loop = 0;

		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", ast_channel_name(chan));
			return -1;
		}
		if (ms == 0) { /* all done, nothing happened */
			ast_log(LOG_WARNING, "channel '%s' timed-out during the T.38 negotiation.\n", ast_channel_name(chan));
			details->caps &= ~AST_FAX_TECH_T38;
			break;
		}

		if (!(frame = ast_read(chan))) {
			ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", ast_channel_name(chan));
			return -1;
		}

		if ((frame->frametype == AST_FRAME_CONTROL) &&
				(frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
				(frame->datalen == sizeof(t38_parameters))) {
			struct ast_control_t38_parameters *parameters = frame->data.ptr;

			switch (parameters->request_response) {
			case AST_T38_REQUEST_NEGOTIATE:
				t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
				t38_parameters.request_response = AST_T38_NEGOTIATED;
				ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
				break;
			case AST_T38_NEGOTIATED:
				ast_debug(1, "Negotiated T.38 for receive on %s\n", ast_channel_name(chan));
				t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
				details->caps &= ~AST_FAX_TECH_AUDIO;
				report_fax_status(chan, details, "T.38 Negotiated");
				break_loop = 1;
				break;
			case AST_T38_REFUSED:
				ast_log(LOG_WARNING, "channel '%s' refused to negotiate T.38\n", ast_channel_name(chan));
				details->caps &= ~AST_FAX_TECH_T38;
				break_loop = 1;
				break;
			default:
				ast_log(LOG_ERROR, "channel '%s' failed to negotiate T.38\n", ast_channel_name(chan));
				details->caps &= ~AST_FAX_TECH_T38;
				break_loop = 1;
				break;
			}
		}
		ast_frfree(frame);
		if (break_loop) {
			break;
		}
	}

	/* if T.38 was negotiated, we are done initializing */
	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		return 0;
	}

	/* if we made it here, then T.38 failed, check the 'f' flag */
	if (details->option.allow_audio != AST_FAX_OPTFLAG_TRUE) {
		ast_log(LOG_WARNING, "Audio FAX not allowed on channel '%s' and T.38 negotiation failed; aborting.\n", ast_channel_name(chan));
		return -1;
	}

	/* ok, audio fallback is allowed */
	details->caps |= AST_FAX_TECH_AUDIO;

	return 0;
}

/*! \brief initiate a receive FAX session */
static int receivefax_exec(struct ast_channel *chan, const char *data)
{
	char *parse, modems[128] = "";
	int channel_alive;
	struct ast_fax_session_details *details;
	struct ast_fax_session *s;
	struct ast_fax_tech_token *token = NULL;
	struct ast_fax_document *doc;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
	);
	struct ast_flags opts = { 0, };
	struct manager_event_info info;
	enum ast_t38_state t38state;

	/* initialize output channel variables */
	pbx_builtin_setvar_helper(chan, "FAXSTATUS", "FAILED");
	pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", NULL);
	pbx_builtin_setvar_helper(chan, "FAXPAGES", "0");
	pbx_builtin_setvar_helper(chan, "FAXBITRATE", NULL);
	pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", NULL);

	/* Get a FAX session details structure from the channel's FAX datastore and create one if
	 * it does not already exist. */
	if (!(details = find_or_create_details(chan))) {
		pbx_builtin_setvar_helper(chan, "FAXERROR", "MEMORY_ERROR");
		pbx_builtin_setvar_helper(chan, "FAXSTATUSSTRING", "error allocating memory");
		ast_log(LOG_ERROR, "System cannot provide memory for session requirements.\n");
		return -1;
	}

	ast_string_field_set(details, result, "FAILED");
	ast_string_field_set(details, resultstr, "error starting fax session");
	ast_string_field_set(details, error, "INIT_ERROR");
	set_channel_variables(chan, details);

	if (details->gateway_id > 0) {
		ast_string_field_set(details, resultstr, "can't receive a fax on a channel with a T.38 gateway");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "executing ReceiveFAX on a channel with a T.38 Gateway is not supported\n");
		ao2_ref(details, -1);
		return -1;
	}

	if (details->maxrate < details->minrate) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "maxrate is less than minrate");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "maxrate %u is less than minrate %u\n", details->maxrate, details->minrate);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->minrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'minrate' setting %u\n", modems, details->minrate);
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "incompatible 'modems' and 'minrate' settings");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->maxrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'maxrate' setting %u\n", modems, details->maxrate);
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "incompatible 'modems' and 'maxrate' settings");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ast_log(LOG_WARNING, "%s requires an argument (filename[,options])\n", app_receivefax);
		ao2_ref(details, -1);
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options) &&
	    ast_app_parse_options(fax_exec_options, &opts, NULL, args.options)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}
	if (ast_strlen_zero(args.filename)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ast_log(LOG_WARNING, "%s requires an argument (filename[,options])\n", app_receivefax);
		ao2_ref(details, -1);
		return -1;
	}

	/* check for unsupported FAX application options */
	if (ast_test_flag(&opts, OPT_CALLERMODE) || ast_test_flag(&opts, OPT_CALLEDMODE)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ast_log(LOG_WARNING, "%s does not support polling\n", app_receivefax);
		ao2_ref(details, -1);
		return -1;
	}

	ast_atomic_fetchadd_int(&faxregistry.fax_rx_attempts, 1);

	pbx_builtin_setvar_helper(chan, "FAXERROR", "Channel Problems");
	pbx_builtin_setvar_helper(chan, "FAXSTATUSSTRING", "Error before FAX transmission started.");

	if (!(doc = ast_calloc(1, sizeof(*doc) + strlen(args.filename) + 1))) {
		ast_string_field_set(details, error, "MEMORY_ERROR");
		ast_string_field_set(details, resultstr, "error allocating memory");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "System cannot provide memory for session requirements.\n");
		ao2_ref(details, -1);
		return -1;
	}

	strcpy(doc->filename, args.filename);
	AST_LIST_INSERT_TAIL(&details->documents, doc, next);

	ast_verb(3, "Channel '%s' receiving FAX '%s'\n", ast_channel_name(chan), args.filename);

	details->caps = AST_FAX_TECH_RECEIVE;
	details->option.send_ced = AST_FAX_OPTFLAG_TRUE;

	/* check for debug */
	if (ast_test_flag(&opts, OPT_DEBUG) || global_fax_debug) {
		details->option.debug = AST_FAX_OPTFLAG_TRUE;
	}

	/* check for request for status events */
	if (ast_test_flag(&opts, OPT_STATUS)) {
		details->option.statusevents = AST_FAX_OPTFLAG_TRUE;
	}

	t38state = ast_channel_get_t38_state(chan);
	if ((t38state == T38_STATE_UNAVAILABLE) || (t38state == T38_STATE_REJECTED) ||
	    ast_test_flag(&opts, OPT_ALLOWAUDIO) ||
	    ast_test_flag(&opts, OPT_FORCE_AUDIO)) {
		details->option.allow_audio = AST_FAX_OPTFLAG_TRUE;
	}

	if (!(s = fax_session_reserve(details, &token))) {
		ast_string_field_set(details, resultstr, "error reserving fax session");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "Unable to reserve FAX session.\n");
		ao2_ref(details, -1);
		return -1;
	}

	/* make sure the channel is up */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_answer(chan)) {
			ast_string_field_set(details, resultstr, "error answering channel");
			set_channel_variables(chan, details);
			ast_log(LOG_WARNING, "Channel '%s' failed answer attempt.\n", ast_channel_name(chan));
			fax_session_release(s, token);
			ao2_ref(s, -1);
			ao2_ref(details, -1);
			return -1;
		}
	}

	if (!ast_test_flag(&opts, OPT_FORCE_AUDIO)) {
		if (set_fax_t38_caps(chan, details)) {
			ast_string_field_set(details, error, "T38_NEG_ERROR");
			ast_string_field_set(details, resultstr, "error negotiating T.38");
			set_channel_variables(chan, details);
			fax_session_release(s, token);
			ao2_ref(s, -1);
			ao2_ref(details, -1);
			return -1;
		}
	} else {
		details->caps |= AST_FAX_TECH_AUDIO;
	}

	if (!ast_test_flag(&opts, OPT_FORCE_AUDIO) && (details->caps & AST_FAX_TECH_T38)) {
		if (receivefax_t38_init(chan, details)) {
			ast_string_field_set(details, error, "T38_NEG_ERROR");
			ast_string_field_set(details, resultstr, "error negotiating T.38");
			set_channel_variables(chan, details);
			fax_session_release(s, token);
			ao2_ref(s, -1);
			ao2_ref(details, -1);
			ast_log(LOG_ERROR, "error initializing channel '%s' in T.38 mode\n", ast_channel_name(chan));
			return -1;
		}
	}

	if ((channel_alive = generic_fax_exec(chan, details, s, token)) < 0) {
		ast_atomic_fetchadd_int(&faxregistry.fax_failures, 1);
	}

	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		if (disable_t38(chan)) {
			ast_debug(1, "error disabling T.38 mode on %s\n", ast_channel_name(chan));
		}
	}

	/* send out the AMI completion event */
	ast_channel_lock(chan);

	get_manager_event_info(chan, &info);
	manager_event(EVENT_FLAG_CALL,
		      "ReceiveFAX",
		      "Channel: %s\r\n"
		      "Context: %s\r\n"
		      "Exten: %s\r\n"
		      "CallerID: %s\r\n"
		      "RemoteStationID: %s\r\n"
		      "LocalStationID: %s\r\n"
		      "PagesTransferred: %s\r\n"
		      "Resolution: %s\r\n"
		      "TransferRate: %s\r\n"
		      "FileName: %s\r\n",
		      ast_channel_name(chan),
		      info.context,
		      info.exten,
		      info.cid,
		      S_OR(pbx_builtin_getvar_helper(chan, "REMOTESTATIONID"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "LOCALSTATIONID"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "FAXPAGES"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "FAXRESOLUTION"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "FAXBITRATE"), ""),
		      args.filename);
	ast_channel_unlock(chan);

	ao2_ref(s, -1);
	ao2_ref(details, -1);

	/* If the channel hungup return -1; otherwise, return 0 to continue in the dialplan */
	return (!channel_alive) ? -1 : 0;
}

static int sendfax_t38_init(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	int timeout_ms;
	struct ast_frame *frame = NULL;
	struct ast_control_t38_parameters t38_parameters;
	struct timeval start;
	int ms;

	/* send CNG tone while listening for the receiver to initiate a switch
	 * to T.38 mode; if they do, stop sending the CNG tone and proceed with
	 * the switch.
	 *
	 * 10500 is enough time for 3 CNG tones
	 */
	timeout_ms = 10500;

	/* don't send any audio if we've already received a T.38 reinvite */
	if (ast_channel_get_t38_state(chan) != T38_STATE_NEGOTIATING) {
		if (ast_playtones_start(chan, 1024, "!1100/500,!0/3000,!1100/500,!0/3000,!1100/500,!0/3000", 1)) {
			ast_log(LOG_ERROR, "error generating CNG tone on %s\n", ast_channel_name(chan));
			return -1;
		}
	}

	start = ast_tvnow();
	while ((ms = ast_remaining_ms(start, timeout_ms))) {
		int break_loop = 0;
		ms = ast_waitfor(chan, ms);

		if (ms < 0) {
			ast_log(LOG_ERROR, "error while generating CNG tone on %s\n", ast_channel_name(chan));
			ast_playtones_stop(chan);
			return -1;
		}

		if (ms == 0) { /* all done, nothing happened */
			break;
		}

		if (!(frame = ast_read(chan))) {
			ast_log(LOG_ERROR, "error reading frame while generating CNG tone on %s\n", ast_channel_name(chan));
			ast_playtones_stop(chan);
			return -1;
		}

		if ((frame->frametype == AST_FRAME_CONTROL) &&
				(frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
				(frame->datalen == sizeof(t38_parameters))) {
			struct ast_control_t38_parameters *parameters = frame->data.ptr;

			switch (parameters->request_response) {
			case AST_T38_REQUEST_NEGOTIATE:
				/* the other end has requested a switch to T.38, so reply that we are willing, if we can
				 * do T.38 as well
				 */
				t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
				t38_parameters.request_response = (details->caps & AST_FAX_TECH_T38) ? AST_T38_NEGOTIATED : AST_T38_REFUSED;
				ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
				ast_playtones_stop(chan);
				break;
			case AST_T38_NEGOTIATED:
				ast_debug(1, "Negotiated T.38 for send on %s\n", ast_channel_name(chan));
				t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
				details->caps &= ~AST_FAX_TECH_AUDIO;
				report_fax_status(chan, details, "T.38 Negotiated");
				break_loop = 1;
				break;
			default:
				break;
			}
		}
		ast_frfree(frame);
		if (break_loop) {
			break;
		}
	}

	ast_playtones_stop(chan);

	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		return 0;
	}

	/* T.38 negotiation did not happen, initiate a switch if requested */
	if (details->option.request_t38 == AST_FAX_OPTFLAG_TRUE) {
		ast_debug(1, "Negotiating T.38 for send on %s\n", ast_channel_name(chan));

		/* wait up to five seconds for negotiation to complete */
		timeout_ms = 5000;

		/* set parameters based on the session's parameters */
		t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
		t38_parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
		if ((ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) != 0)) {
			return -1;
		}

		start = ast_tvnow();
		while ((ms = ast_remaining_ms(start, timeout_ms))) {
			int break_loop = 0;

			ms = ast_waitfor(chan, ms);
			if (ms < 0) {
				ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", ast_channel_name(chan));
				return -1;
			}
			if (ms == 0) { /* all done, nothing happened */
				ast_log(LOG_WARNING, "channel '%s' timed-out during the T.38 negotiation.\n", ast_channel_name(chan));
				details->caps &= ~AST_FAX_TECH_T38;
				break;
			}

			if (!(frame = ast_read(chan))) {
				ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", ast_channel_name(chan));
				return -1;
			}

			if ((frame->frametype == AST_FRAME_CONTROL) &&
					(frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
					(frame->datalen == sizeof(t38_parameters))) {
				struct ast_control_t38_parameters *parameters = frame->data.ptr;

				switch (parameters->request_response) {
				case AST_T38_REQUEST_NEGOTIATE:
					t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
					t38_parameters.request_response = AST_T38_NEGOTIATED;
					ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
					break;
				case AST_T38_NEGOTIATED:
					ast_debug(1, "Negotiated T.38 for receive on %s\n", ast_channel_name(chan));
					t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
					details->caps &= ~AST_FAX_TECH_AUDIO;
					report_fax_status(chan, details, "T.38 Negotiated");
					break_loop = 1;
					break;
				case AST_T38_REFUSED:
					ast_log(LOG_WARNING, "channel '%s' refused to negotiate T.38\n", ast_channel_name(chan));
					details->caps &= ~AST_FAX_TECH_T38;
					break_loop = 1;
					break;
				default:
					ast_log(LOG_ERROR, "channel '%s' failed to negotiate T.38\n", ast_channel_name(chan));
					details->caps &= ~AST_FAX_TECH_T38;
					break_loop = 1;
					break;
				}
			}
			ast_frfree(frame);
			if (break_loop) {
				break;
			}
		}

		/* if T.38 was negotiated, we are done initializing */
		if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
			return 0;
		}

		/* send one more CNG tone to get audio going again for some
		 * carriers if we are going to fall back to audio mode */
		if (details->option.allow_audio == AST_FAX_OPTFLAG_TRUE) {
			if (ast_playtones_start(chan, 1024, "!1100/500,!0/3000", 1)) {
				ast_log(LOG_ERROR, "error generating second CNG tone on %s\n", ast_channel_name(chan));
				return -1;
			}

			timeout_ms = 3500;
			start = ast_tvnow();
			while ((ms = ast_remaining_ms(start, timeout_ms))) {
				int break_loop = 0;

				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_log(LOG_ERROR, "error while generating second CNG tone on %s\n", ast_channel_name(chan));
					ast_playtones_stop(chan);
					return -1;
				}
				if (ms == 0) { /* all done, nothing happened */
					break;
				}

				if (!(frame = ast_read(chan))) {
					ast_log(LOG_ERROR, "error reading frame while generating second CNG tone on %s\n", ast_channel_name(chan));
					ast_playtones_stop(chan);
					return -1;
				}

				if ((frame->frametype == AST_FRAME_CONTROL) &&
						(frame->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
						(frame->datalen == sizeof(t38_parameters))) {
					struct ast_control_t38_parameters *parameters = frame->data.ptr;

					switch (parameters->request_response) {
					case AST_T38_REQUEST_NEGOTIATE:
						/* the other end has requested a switch to T.38, so reply that we are willing, if we can
						 * do T.38 as well
						 */
						t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
						t38_parameters.request_response = (details->caps & AST_FAX_TECH_T38) ? AST_T38_NEGOTIATED : AST_T38_REFUSED;
						ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
						ast_playtones_stop(chan);
						break;
					case AST_T38_NEGOTIATED:
						ast_debug(1, "Negotiated T.38 for send on %s\n", ast_channel_name(chan));
						t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
						details->caps &= ~AST_FAX_TECH_AUDIO;
						report_fax_status(chan, details, "T.38 Negotiated");
						break_loop = 1;
						break;
					default:
						break;
					}
				}
				ast_frfree(frame);
				if (break_loop) {
					break;
				}
			}

			ast_playtones_stop(chan);

			/* if T.38 was negotiated, we are done initializing */
			if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
				return 0;
			}
		}
	}

	/* if we made it here, then T.38 failed, check the 'f' flag */
	if (details->option.allow_audio == AST_FAX_OPTFLAG_FALSE) {
		ast_log(LOG_WARNING, "Audio FAX not allowed on channel '%s' and T.38 negotiation failed; aborting.\n", ast_channel_name(chan));
		return -1;
	}

	/* ok, audio fallback is allowed */
	details->caps |= AST_FAX_TECH_AUDIO;

	return 0;
}


/*! \brief initiate a send FAX session */
static int sendfax_exec(struct ast_channel *chan, const char *data)
{
	char *parse, *filenames, *c, modems[128] = "";
	int channel_alive, file_count;
	struct ast_fax_session_details *details;
	struct ast_fax_session *s;
	struct ast_fax_tech_token *token = NULL;
	struct ast_fax_document *doc;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filenames);
		AST_APP_ARG(options);
	);
	struct ast_flags opts = { 0, };
	struct manager_event_info info;
	enum ast_t38_state t38state;

	/* initialize output channel variables */
	pbx_builtin_setvar_helper(chan, "FAXSTATUS", "FAILED");
	pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", NULL);
	pbx_builtin_setvar_helper(chan, "FAXPAGES", "0");
	pbx_builtin_setvar_helper(chan, "FAXBITRATE", NULL);
	pbx_builtin_setvar_helper(chan, "FAXRESOLUTION", NULL);

	/* Get a requirement structure and set it.  This structure is used
	 * to tell the FAX technology module about the higher level FAX session */
	if (!(details = find_or_create_details(chan))) {
		pbx_builtin_setvar_helper(chan, "FAXERROR", "MEMORY_ERROR");
		pbx_builtin_setvar_helper(chan, "FAXSTATUSSTRING", "error allocating memory");
		ast_log(LOG_ERROR, "System cannot provide memory for session requirements.\n");
		return -1;
	}

	ast_string_field_set(details, result, "FAILED");
	ast_string_field_set(details, resultstr, "error starting fax session");
	ast_string_field_set(details, error, "INIT_ERROR");
	set_channel_variables(chan, details);

	if (details->gateway_id > 0) {
		ast_string_field_set(details, resultstr, "can't send a fax on a channel with a T.38 gateway");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "executing SendFAX on a channel with a T.38 Gateway is not supported\n");
		ao2_ref(details, -1);
		return -1;
	}

	if (details->maxrate < details->minrate) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "maxrate is less than minrate");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "maxrate %u is less than minrate %u\n", details->maxrate, details->minrate);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->minrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'minrate' setting %u\n", modems, details->minrate);
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "incompatible 'modems' and 'minrate' settings");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->maxrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'maxrate' setting %u\n", modems, details->maxrate);
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "incompatible 'modems' and 'maxrate' settings");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ast_log(LOG_WARNING, "%s requires an argument (filename[&filename[&filename]][,options])\n", app_sendfax);
		ao2_ref(details, -1);
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);


	if (!ast_strlen_zero(args.options) &&
	    ast_app_parse_options(fax_exec_options, &opts, NULL, args.options)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}
	if (ast_strlen_zero(args.filenames)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ast_log(LOG_WARNING, "%s requires an argument (filename[&filename[&filename]],options])\n", app_sendfax);
		ao2_ref(details, -1);
		return -1;
	}

	/* check for unsupported FAX application options */
	if (ast_test_flag(&opts, OPT_CALLERMODE) || ast_test_flag(&opts, OPT_CALLEDMODE)) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "invalid arguments");
		set_channel_variables(chan, details);
		ast_log(LOG_WARNING, "%s does not support polling\n", app_sendfax);
		ao2_ref(details, -1);
		return -1;
	}

	ast_atomic_fetchadd_int(&faxregistry.fax_tx_attempts, 1);

	file_count = 0;
	filenames = args.filenames;
	while ((c = strsep(&filenames, "&"))) {
		if (access(c, (F_OK | R_OK)) < 0) {
			ast_string_field_set(details, error, "FILE_ERROR");
			ast_string_field_set(details, resultstr, "error reading file");
			set_channel_variables(chan, details);
			ast_log(LOG_ERROR, "access failure.  Verify '%s' exists and check permissions.\n", args.filenames);
			ao2_ref(details, -1);
			return -1;
		}

		if (!(doc = ast_calloc(1, sizeof(*doc) + strlen(c) + 1))) {
			ast_string_field_set(details, error, "MEMORY_ERROR");
			ast_string_field_set(details, resultstr, "error allocating memory");
			set_channel_variables(chan, details);
			ast_log(LOG_ERROR, "System cannot provide memory for session requirements.\n");
			ao2_ref(details, -1);
			return -1;
		}

		strcpy(doc->filename, c);
		AST_LIST_INSERT_TAIL(&details->documents, doc, next);
		file_count++;
	}

	ast_verb(3, "Channel '%s' sending FAX:\n", ast_channel_name(chan));
	AST_LIST_TRAVERSE(&details->documents, doc, next) {
		ast_verb(3, "   %s\n", doc->filename);
	}

	details->caps = AST_FAX_TECH_SEND;

	if (file_count > 1) {
		details->caps |= AST_FAX_TECH_MULTI_DOC;
	}

	/* check for debug */
	if (ast_test_flag(&opts, OPT_DEBUG) || global_fax_debug) {
		details->option.debug = AST_FAX_OPTFLAG_TRUE;
	}

	/* check for request for status events */
	if (ast_test_flag(&opts, OPT_STATUS)) {
		details->option.statusevents = AST_FAX_OPTFLAG_TRUE;
	}

	t38state = ast_channel_get_t38_state(chan);
	if ((t38state == T38_STATE_UNAVAILABLE) || (t38state == T38_STATE_REJECTED) ||
	    ast_test_flag(&opts, OPT_ALLOWAUDIO) ||
	    ast_test_flag(&opts, OPT_FORCE_AUDIO)) {
		details->option.allow_audio = AST_FAX_OPTFLAG_TRUE;
	}

	if (ast_test_flag(&opts, OPT_REQUEST_T38)) {
		details->option.request_t38 = AST_FAX_OPTFLAG_TRUE;
	}

	if (!(s = fax_session_reserve(details, &token))) {
		ast_string_field_set(details, resultstr, "error reserving fax session");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "Unable to reserve FAX session.\n");
		ao2_ref(details, -1);
		return -1;
	}

	/* make sure the channel is up */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_answer(chan)) {
			ast_string_field_set(details, resultstr, "error answering channel");
			set_channel_variables(chan, details);
			ast_log(LOG_WARNING, "Channel '%s' failed answer attempt.\n", ast_channel_name(chan));
			fax_session_release(s, token);
			ao2_ref(s, -1);
			ao2_ref(details, -1);
			return -1;
		}
	}

	if (!ast_test_flag(&opts, OPT_FORCE_AUDIO)) {
		if (set_fax_t38_caps(chan, details)) {
			ast_string_field_set(details, error, "T38_NEG_ERROR");
			ast_string_field_set(details, resultstr, "error negotiating T.38");
			set_channel_variables(chan, details);
			fax_session_release(s, token);
			ao2_ref(s, -1);
			ao2_ref(details, -1);
			return -1;
		}
	} else {
		details->caps |= AST_FAX_TECH_AUDIO;
	}

	if (!ast_test_flag(&opts, OPT_FORCE_AUDIO) && (details->caps & AST_FAX_TECH_T38)) {
		if (sendfax_t38_init(chan, details)) {
			ast_string_field_set(details, error, "T38_NEG_ERROR");
			ast_string_field_set(details, resultstr, "error negotiating T.38");
			set_channel_variables(chan, details);
			fax_session_release(s, token);
			ao2_ref(s, -1);
			ao2_ref(details, -1);
			ast_log(LOG_ERROR, "error initializing channel '%s' in T.38 mode\n", ast_channel_name(chan));
			return -1;
		}
	} else {
		details->option.send_cng = 1;
	}

	if ((channel_alive = generic_fax_exec(chan, details, s, token)) < 0) {
		ast_atomic_fetchadd_int(&faxregistry.fax_failures, 1);
	}

	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		if (disable_t38(chan)) {
			ast_debug(1, "error disabling T.38 mode on %s\n", ast_channel_name(chan));
		}
	}

	if (!(filenames = generate_filenames_string(details, "FileName: ", "\r\n"))) {
		ast_log(LOG_ERROR, "Error generating SendFAX manager event\n");
		ao2_ref(s, -1);
		ao2_ref(details, -1);
		return (!channel_alive) ? -1 : 0;
	}

	/* send out the AMI completion event */
	ast_channel_lock(chan);
	get_manager_event_info(chan, &info);
	manager_event(EVENT_FLAG_CALL,
		      "SendFAX",
		      "Channel: %s\r\n"
		      "Context: %s\r\n"
		      "Exten: %s\r\n"
		      "CallerID: %s\r\n"
		      "RemoteStationID: %s\r\n"
		      "LocalStationID: %s\r\n"
		      "PagesTransferred: %s\r\n"
		      "Resolution: %s\r\n"
		      "TransferRate: %s\r\n"
		      "%s\r\n",
		      ast_channel_name(chan),
		      info.context,
		      info.exten,
		      info.cid,
		      S_OR(pbx_builtin_getvar_helper(chan, "REMOTESTATIONID"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "LOCALSTATIONID"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "FAXPAGES"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "FAXRESOLUTION"), ""),
		      S_OR(pbx_builtin_getvar_helper(chan, "FAXBITRATE"), ""),
		      filenames);
	ast_channel_unlock(chan);

	ast_free(filenames);

	ao2_ref(s, -1);
	ao2_ref(details, -1);

	/* If the channel hungup return -1; otherwise, return 0 to continue in the dialplan */
	return (!channel_alive) ? -1 : 0;
}

/*! \brief destroy the v21 detection parts of a fax gateway session */
static void destroy_v21_sessions(struct fax_gateway *gateway)
{
	if (gateway->chan_v21_session) {
		ao2_lock(faxregistry.container);
		ao2_unlink(faxregistry.container, gateway->chan_v21_session);
		ao2_unlock(faxregistry.container);

		ao2_ref(gateway->chan_v21_session, -1);
		gateway->chan_v21_session = NULL;
	}

	if (gateway->peer_v21_session) {
		ao2_lock(faxregistry.container);
		ao2_unlink(faxregistry.container, gateway->peer_v21_session);
		ao2_unlock(faxregistry.container);

		ao2_ref(gateway->peer_v21_session, -1);
		gateway->peer_v21_session = NULL;
	}
}

/*! \brief destroy a FAX gateway session structure */
static void destroy_gateway(void *data)
{
	struct fax_gateway *gateway = data;

	destroy_v21_sessions(gateway);

	if (gateway->s) {
		fax_session_release(gateway->s, gateway->token);
		gateway->token = NULL;

		ao2_lock(faxregistry.container);
		ao2_unlink(faxregistry.container, gateway->s);
		ao2_unlock(faxregistry.container);

		ao2_ref(gateway->s, -1);
		gateway->s = NULL;
	}
}

/*! \brief Create a new fax gateway object.
 * \param chan the channel the gateway object will be attached to
 * \param details the fax session details
 * \return NULL or a fax gateway object
 */
static struct fax_gateway *fax_gateway_new(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	struct fax_gateway *gateway = ao2_alloc(sizeof(*gateway), destroy_gateway);
	struct ast_fax_session_details *v21_details;
	if (!gateway) {
		return NULL;
	}

	if (!(v21_details = session_details_new())) {
		ao2_ref(gateway, -1);
		return NULL;
	}

	v21_details->caps = AST_FAX_TECH_V21_DETECT;
	if (!(gateway->chan_v21_session = fax_session_new(v21_details, chan, NULL, NULL))) {
		ao2_ref(v21_details, -1);
		ao2_ref(gateway, -1);
		return NULL;
	}

	if (!(gateway->peer_v21_session = fax_session_new(v21_details, chan, NULL, NULL))) {
		ao2_ref(v21_details, -1);
		ao2_ref(gateway, -1);
		return NULL;
	}
	ao2_ref(v21_details, -1);

	gateway->framehook = -1;

	details->caps = AST_FAX_TECH_GATEWAY;
	if (details->gateway_timeout && !(gateway->s = fax_session_reserve(details, &gateway->token))) {
		details->caps &= ~AST_FAX_TECH_GATEWAY;
		ast_log(LOG_ERROR, "Can't reserve a FAX session, gateway attempt failed.\n");
		ao2_ref(gateway, -1);
		return NULL;
	}

	return gateway;
}

/*! \brief Create a fax session and start T.30<->T.38 gateway mode
 * \param gateway a fax gateway object
 * \param details fax session details
 * \param chan active channel
 * \return 0 on error 1 on success*/
static int fax_gateway_start(struct fax_gateway *gateway, struct ast_fax_session_details *details, struct ast_channel *chan)
{
	struct ast_fax_session *s;

	/* create the FAX session */
	if (!(s = fax_session_new(details, chan, gateway->s, gateway->token))) {
		gateway->token = NULL;
		ast_string_field_set(details, result, "FAILED");
		ast_string_field_set(details, resultstr, "error starting gateway session");
		ast_string_field_set(details, error, "INIT_ERROR");
		set_channel_variables(chan, details);
		report_fax_status(chan, details, "No Available Resource");
		ast_log(LOG_ERROR, "Can't create a FAX session, gateway attempt failed.\n");
		return -1;
	}
	/* release the reference for the reserved session and replace it with
	 * the real session */
	if (gateway->s) {
		ao2_ref(gateway->s, -1);
	}
	gateway->s = s;
	gateway->token = NULL;

	if (gateway->s->tech->start_session(gateway->s) < 0) {
		ast_string_field_set(details, result, "FAILED");
		ast_string_field_set(details, resultstr, "error starting gateway session");
		ast_string_field_set(details, error, "INIT_ERROR");
		set_channel_variables(chan, details);
		return -1;
	}

	gateway->timeout_start.tv_sec = 0;
	gateway->timeout_start.tv_usec = 0;

	report_fax_status(chan, details, "FAX Transmission In Progress");

	return 0;
}

static struct ast_frame *fax_gateway_request_t38(struct fax_gateway *gateway, struct ast_channel *chan, struct ast_frame *f)
{
	struct ast_frame *fp;
	struct ast_control_t38_parameters t38_parameters = {
		.request_response = AST_T38_REQUEST_NEGOTIATE,
	};
	struct ast_frame control_frame = {
		.src = "res_fax",
		.frametype = AST_FRAME_CONTROL,
		.datalen = sizeof(t38_parameters),
		.subclass.integer = AST_CONTROL_T38_PARAMETERS,
		.data.ptr = &t38_parameters,
	};

	struct ast_fax_session_details *details = find_details(chan);

	if (!details) {
		ast_log(LOG_ERROR, "no FAX session details found on chan %s for T.38 gateway session, odd\n", ast_channel_name(chan));
		ast_framehook_detach(chan, gateway->framehook);
		return f;
	}

	t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
	ao2_ref(details, -1);

	if (!(fp = ast_frisolate(&control_frame))) {
		ast_log(LOG_ERROR, "error generating T.38 request control frame on chan %s for T.38 gateway session\n", ast_channel_name(chan));
		return f;
	}

	gateway->t38_state = T38_STATE_NEGOTIATING;
	gateway->timeout_start = ast_tvnow();
	details->gateway_timeout = FAX_GATEWAY_TIMEOUT;

	ast_debug(1, "requesting T.38 for gateway session for %s\n", ast_channel_name(chan));
	return fp;
}

static struct ast_frame *fax_gateway_detect_v21(struct fax_gateway *gateway, struct ast_channel *chan, struct ast_channel *peer, struct ast_channel *active, struct ast_frame *f)
{
	struct ast_channel *other = (active == chan) ? peer : chan;
	struct ast_fax_session *active_v21_session = (active == chan) ? gateway->chan_v21_session : gateway->peer_v21_session;

	if (!active_v21_session || gateway->detected_v21) {
		return f;
	}

	if (active_v21_session->tech->write(active_v21_session, f) == 0 &&
	    active_v21_session->details->option.v21_detected) {
		gateway->detected_v21 = 1;
	}

	if (gateway->detected_v21) {
		destroy_v21_sessions(gateway);
		if (ast_channel_get_t38_state(other) == T38_STATE_UNKNOWN) {
			ast_debug(1, "detected v21 preamble from %s\n", ast_channel_name(active));
			return fax_gateway_request_t38(gateway, chan, f);
		} else {
			ast_debug(1, "detected v21 preamble on %s, but %s does not support T.38 for T.38 gateway session\n", ast_channel_name(active), ast_channel_name(other));
		}
	}

	return f;
}

static int fax_gateway_indicate_t38(struct ast_channel *chan, struct ast_channel *active, struct ast_control_t38_parameters *control_params)
{
	if (active == chan) {
		return ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, control_params, sizeof(*control_params));
	} else {
		return ast_queue_control_data(chan, AST_CONTROL_T38_PARAMETERS, control_params, sizeof(*control_params));
	}
}

/*! \brief T38 Gateway Negotiate t38 parameters
 * \param gateway gateway object
 * \param chan channel running the gateway
 * \param peer channel im bridged too
 * \param active channel the frame originated on
 * \param f the control frame to process
 * \return processed control frame or null frame
 */
static struct ast_frame *fax_gateway_detect_t38(struct fax_gateway *gateway, struct ast_channel *chan, struct ast_channel *peer, struct ast_channel *active, struct ast_frame *f)
{
	struct ast_control_t38_parameters *control_params = f->data.ptr;
	struct ast_channel *other = (active == chan) ? peer : chan;
	struct ast_fax_session_details *details;

	if (f->datalen != sizeof(struct ast_control_t38_parameters)) {
		/* invalaid AST_CONTROL_T38_PARAMETERS frame, we can't
		 * do anything with it, pass it on */
		return f;
	}

	/* ignore frames from ourselves */
	if ((gateway->t38_state == T38_STATE_NEGOTIATED && control_params->request_response == AST_T38_NEGOTIATED)
		|| (gateway->t38_state == T38_STATE_REJECTED && control_params->request_response == AST_T38_REFUSED)
		|| (gateway->t38_state == T38_STATE_NEGOTIATING && control_params->request_response == AST_T38_REQUEST_TERMINATE)) {

		return f;
	}

	if (!(details = find_details(chan))) {
		ast_log(LOG_ERROR, "no FAX session details found on chan %s for T.38 gateway session, odd\n", ast_channel_name(chan));
		ast_framehook_detach(chan, gateway->framehook);
		return f;
	}

	if (control_params->request_response == AST_T38_REQUEST_NEGOTIATE) {
		enum ast_t38_state state = ast_channel_get_t38_state(other);

		if (state == T38_STATE_UNKNOWN) {
			/* we detected a request to negotiate T.38 and the
			 * other channel appears to support T.38, we'll pass
			 * the request through and only step in if the other
			 * channel rejects the request */
			ast_debug(1, "%s is attempting to negotiate T.38 with %s, we'll see what happens\n", ast_channel_name(active), ast_channel_name(other));
			t38_parameters_ast_to_fax(&details->their_t38_parameters, control_params);
			gateway->t38_state = T38_STATE_UNKNOWN;
			gateway->timeout_start = ast_tvnow();
			details->gateway_timeout = FAX_GATEWAY_TIMEOUT;
			ao2_ref(details, -1);
			return f;
		} else if (state == T38_STATE_UNAVAILABLE || state == T38_STATE_REJECTED) {
			/* the other channel does not support T.38, we need to
			 * step in here */
			ast_debug(1, "%s is attempting to negotiate T.38 but %s does not support it\n", ast_channel_name(active), ast_channel_name(other));
			ast_debug(1, "starting T.38 gateway for T.38 channel %s and G.711 channel %s\n", ast_channel_name(active), ast_channel_name(other));

			t38_parameters_ast_to_fax(&details->their_t38_parameters, control_params);
			t38_parameters_fax_to_ast(control_params, &details->our_t38_parameters);

			if (fax_gateway_start(gateway, details, chan)) {
				ast_log(LOG_ERROR, "error starting T.38 gateway for T.38 channel %s and G.711 channel %s\n", ast_channel_name(active), ast_channel_name(other));
				gateway->t38_state = T38_STATE_REJECTED;
				control_params->request_response = AST_T38_REFUSED;

				ast_framehook_detach(chan, details->gateway_id);
				details->gateway_id = -1;
			} else {
				gateway->t38_state = T38_STATE_NEGOTIATED;
				control_params->request_response = AST_T38_NEGOTIATED;
				report_fax_status(chan, details, "T.38 Negotiated");
			}

			fax_gateway_indicate_t38(chan, active, control_params);

			ao2_ref(details, -1);
			return &ast_null_frame;
		} else if (gateway->t38_state == T38_STATE_NEGOTIATING) {
			/* we got a request to negotiate T.38 after we already
			 * sent one to the other party based on v21 preamble
			 * detection. We'll just pretend we passed this request
			 * through in the first place. */

			t38_parameters_ast_to_fax(&details->their_t38_parameters, control_params);
			gateway->t38_state = T38_STATE_UNKNOWN;
			gateway->timeout_start = ast_tvnow();
			details->gateway_timeout = FAX_GATEWAY_TIMEOUT;

			ast_debug(1, "%s is attempting to negotiate T.38 after we already sent a negotiation request based on v21 preamble detection\n", ast_channel_name(active));
			ao2_ref(details, -1);
			return &ast_null_frame;
		} else if (gateway->t38_state == T38_STATE_NEGOTIATED) {
			/* we got a request to negotiate T.38 after we already
			 * sent one to the other party based on v21 preamble
			 * detection and received a response. We need to
			 * respond to this and shut down the gateway. */

			t38_parameters_fax_to_ast(control_params, &details->their_t38_parameters);
			ast_framehook_detach(chan, details->gateway_id);
			details->gateway_id = -1;

			control_params->request_response = AST_T38_NEGOTIATED;

			fax_gateway_indicate_t38(chan, active, control_params);

			ast_string_field_set(details, result, "SUCCESS");
			ast_string_field_set(details, resultstr, "no gateway necessary");
			ast_string_field_set(details, error, "NATIVE_T38");
			set_channel_variables(chan, details);

			ast_debug(1, "%s is attempting to negotiate T.38 after we already negotiated T.38 with %s, disabling the gateway\n", ast_channel_name(active), ast_channel_name(other));
			ao2_ref(details, -1);
			return &ast_null_frame;
		} else {
			ast_log(LOG_WARNING, "%s is attempting to negotiate T.38 while %s is in an unsupported state\n", ast_channel_name(active), ast_channel_name(other));
			ao2_ref(details, -1);
			return f;
		}
	} else if (gateway->t38_state == T38_STATE_NEGOTIATING
		&& control_params->request_response == AST_T38_REFUSED) {

		ast_debug(1, "unable to negotiate T.38 on %s for fax gateway\n", ast_channel_name(active));

		/* our request to negotiate T.38 was refused, if the other
		 * channel supports T.38, they might still reinvite and save
		 * the day.  Otherwise disable the gateway. */
		if (ast_channel_get_t38_state(other) == T38_STATE_UNKNOWN) {
			gateway->t38_state = T38_STATE_UNAVAILABLE;
		} else {
			ast_framehook_detach(chan, details->gateway_id);
			details->gateway_id = -1;

			ast_string_field_set(details, result, "FAILED");
			ast_string_field_set(details, resultstr, "unable to negotiate T.38");
			ast_string_field_set(details, error, "T38_NEG_ERROR");
			set_channel_variables(chan, details);
		}

		ao2_ref(details, -1);
		return &ast_null_frame;
	} else if (gateway->t38_state == T38_STATE_NEGOTIATING
		&& control_params->request_response == AST_T38_NEGOTIATED) {

		ast_debug(1, "starting T.38 gateway for T.38 channel %s and G.711 channel %s\n", ast_channel_name(active), ast_channel_name(other));

		t38_parameters_ast_to_fax(&details->their_t38_parameters, control_params);

		if (fax_gateway_start(gateway, details, chan)) {
			ast_log(LOG_ERROR, "error starting T.38 gateway for T.38 channel %s and G.711 channel %s\n", ast_channel_name(active), ast_channel_name(other));
			gateway->t38_state = T38_STATE_NEGOTIATING;
			control_params->request_response = AST_T38_REQUEST_TERMINATE;

			fax_gateway_indicate_t38(chan, active, control_params);
		} else {
			gateway->t38_state = T38_STATE_NEGOTIATED;
			report_fax_status(chan, details, "T.38 Negotiated");
		}

		ao2_ref(details, -1);
		return &ast_null_frame;
	} else if (control_params->request_response == AST_T38_REFUSED) {
		/* the other channel refused the request to negotiate T.38,
		 * we'll step in here and pretend the request was accepted */

		ast_debug(1, "%s attempted to negotiate T.38 but %s refused the request\n", ast_channel_name(other), ast_channel_name(active));
		ast_debug(1, "starting T.38 gateway for T.38 channel %s and G.711 channel %s\n", ast_channel_name(other), ast_channel_name(active));

		t38_parameters_fax_to_ast(control_params, &details->our_t38_parameters);

		if (fax_gateway_start(gateway, details, chan)) {
			ast_log(LOG_ERROR, "error starting T.38 gateway for T.38 channel %s and G.711 channel %s\n", ast_channel_name(active), ast_channel_name(other));
			gateway->t38_state = T38_STATE_REJECTED;
			control_params->request_response = AST_T38_REFUSED;

			ast_framehook_detach(chan, details->gateway_id);
			details->gateway_id = -1;
		} else {
			gateway->t38_state = T38_STATE_NEGOTIATED;
			control_params->request_response = AST_T38_NEGOTIATED;
		}

		ao2_ref(details, -1);
		return f;
	} else if (control_params->request_response == AST_T38_REQUEST_TERMINATE) {
		/* the channel wishes to end our short relationship, we shall
		 * oblige */

		ast_debug(1, "T.38 channel %s is requesting a shutdown of T.38, disabling the gateway\n", ast_channel_name(active));

		ast_framehook_detach(chan, details->gateway_id);
		details->gateway_id = -1;

		gateway->t38_state = T38_STATE_REJECTED;
		control_params->request_response = AST_T38_TERMINATED;

		fax_gateway_indicate_t38(chan, active, control_params);

		ao2_ref(details, -1);
		return &ast_null_frame;
	} else if (control_params->request_response == AST_T38_NEGOTIATED) {
		ast_debug(1, "T.38 successfully negotiated between %s and %s, no gateway necessary\n", ast_channel_name(active), ast_channel_name(other));

		ast_framehook_detach(chan, details->gateway_id);
		details->gateway_id = -1;

		ast_string_field_set(details, result, "SUCCESS");
		ast_string_field_set(details, resultstr, "no gateway necessary");
		ast_string_field_set(details, error, "NATIVE_T38");
		set_channel_variables(chan, details);

		ao2_ref(details, -1);
		return f;
	} else if (control_params->request_response == AST_T38_TERMINATED) {
		ast_debug(1, "T.38 disabled on channel %s\n", ast_channel_name(active));

		ast_framehook_detach(chan, details->gateway_id);
		details->gateway_id = -1;

		ao2_ref(details, -1);
		return &ast_null_frame;
	}

	ao2_ref(details, -1);
	return f;
}

/*! \brief Destroy the gateway data structure when the framehook is detached
 * \param data framehook data (gateway data)*/
static void fax_gateway_framehook_destroy(void *data) {
	struct fax_gateway *gateway = data;

	if (gateway->s) {
		switch (gateway->s->state) {
		case AST_FAX_STATE_INITIALIZED:
		case AST_FAX_STATE_OPEN:
		case AST_FAX_STATE_ACTIVE:
		case AST_FAX_STATE_COMPLETE:
			if (gateway->s->tech->cancel_session) {
				gateway->s->tech->cancel_session(gateway->s);
			}
			/* fall through */
		default:
			break;
		}
	}

	ao2_ref(gateway, -1);
}

/*! \brief T.30<->T.38 gateway framehook.
 *
 * Intercept packets on bridged channels and determine if a T.38 gateway is
 * required. If a gateway is required, start a gateway and handle T.38
 * negotiation if necessary.
 *
 * \param chan channel running the gateway
 * \param f frame to handle may be NULL
 * \param event framehook event
 * \param data framehook data (struct fax_gateway *)
 *
 * \return processed frame or NULL when f is NULL or a null frame
 */
static struct ast_frame *fax_gateway_framehook(struct ast_channel *chan, struct ast_frame *f, enum ast_framehook_event event, void *data) {
	struct fax_gateway *gateway = data;
	struct ast_channel *peer, *active;
	struct ast_fax_session_details *details;

	if (gateway->s) {
		details = gateway->s->details;
		ao2_ref(details, 1);
	} else {
		if (!(details = find_details(chan))) {
			ast_log(LOG_ERROR, "no FAX session details found on chan %s for T.38 gateway session, odd\n", ast_channel_name(chan));
			ast_framehook_detach(chan, gateway->framehook);
			return f;
		}
	}

	/* restore audio formats when we are detached */
	if (event == AST_FRAMEHOOK_EVENT_DETACHED) {
		set_channel_variables(chan, details);

		if (gateway->bridged) {
			ast_set_read_format(chan, &gateway->chan_read_format);
			ast_set_read_format(chan, &gateway->chan_write_format);

			if ((peer = ast_bridged_channel(chan))) {
				ast_set_read_format(peer, &gateway->peer_read_format);
				ast_set_read_format(peer, &gateway->peer_write_format);
				ast_channel_make_compatible(chan, peer);
			}
		}

		ao2_ref(details, -1);
		return NULL;
	}

	if (!f || (event == AST_FRAMEHOOK_EVENT_ATTACHED)) {
		ao2_ref(details, -1);
		return NULL;
	};

	/* this frame was generated by the fax gateway, pass it on */
	if (ast_test_flag(f, AST_FAX_FRFLAG_GATEWAY)) {
		ao2_ref(details, -1);
		return f;
	}

	if (!(peer = ast_bridged_channel(chan))) {
		/* not bridged, don't do anything */
		ao2_ref(details, -1);
		return f;
	}

	if (!gateway->bridged && peer) {
		/* don't start a gateway if neither channel can handle T.38 */
		if (ast_channel_get_t38_state(chan) == T38_STATE_UNAVAILABLE && ast_channel_get_t38_state(peer) == T38_STATE_UNAVAILABLE) {
			ast_debug(1, "not starting gateway for %s and %s; neither channel supports T.38\n", ast_channel_name(chan), ast_channel_name(peer));
			ast_framehook_detach(chan, gateway->framehook);
			details->gateway_id = -1;

			ast_string_field_set(details, result, "FAILED");
			ast_string_field_set(details, resultstr, "neither channel supports T.38");
			ast_string_field_set(details, error, "T38_NEG_ERROR");
			set_channel_variables(chan, details);
			ao2_ref(details, -1);
			return f;
		}

		if (details->gateway_timeout) {
			gateway->timeout_start = ast_tvnow();
		}

		/* we are bridged, change r/w formats to SLIN for v21 preamble
		 * detection and T.30 */
		ast_format_copy(&gateway->chan_read_format, ast_channel_readformat(chan));
		ast_format_copy(&gateway->chan_write_format, ast_channel_readformat(chan));

		ast_format_copy(&gateway->peer_read_format, ast_channel_readformat(peer));
		ast_format_copy(&gateway->peer_write_format, ast_channel_readformat(peer));

		ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR);
		ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR);

		ast_set_read_format_by_id(peer, AST_FORMAT_SLINEAR);
		ast_set_write_format_by_id(peer, AST_FORMAT_SLINEAR);

		ast_channel_make_compatible(chan, peer);
		gateway->bridged = 1;
	}

	if (gateway->bridged && !ast_tvzero(gateway->timeout_start)) {
		if (ast_tvdiff_ms(ast_tvnow(), gateway->timeout_start) > details->gateway_timeout) {
			ast_debug(1, "no fax activity between %s and %s after %d ms, disabling gateway\n", ast_channel_name(chan), ast_channel_name(peer), details->gateway_timeout);
			ast_framehook_detach(chan, gateway->framehook);
			details->gateway_id = -1;

			ast_string_field_set(details, result, "FAILED");
			ast_string_field_build(details, resultstr, "no fax activity after %d ms", details->gateway_timeout);
			ast_string_field_set(details, error, "TIMEOUT");
			set_channel_variables(chan, details);
			ao2_ref(details, -1);
			return f;
		}
	}

	/* only handle VOICE, MODEM, and CONTROL frames*/
	switch (f->frametype) {
	case AST_FRAME_VOICE:
		switch (f->subclass.format.id) {
		case AST_FORMAT_SLINEAR:
		case AST_FORMAT_ALAW:
		case AST_FORMAT_ULAW:
			break;
		default:
			ao2_ref(details, -1);
			return f;
		}
		break;
	case AST_FRAME_MODEM:
		if (f->subclass.integer == AST_MODEM_T38) {
			break;
		}
		ao2_ref(details, -1);
		return f;
	case AST_FRAME_CONTROL:
		if (f->subclass.integer == AST_CONTROL_T38_PARAMETERS) {
			break;
		}
		ao2_ref(details, -1);
		return f;
	default:
		ao2_ref(details, -1);
		return f;
	}

	/* detect the active channel */
	switch (event) {
	case AST_FRAMEHOOK_EVENT_WRITE:
		active = peer;
		break;
	case AST_FRAMEHOOK_EVENT_READ:
		active = chan;
		break;
	default:
		ast_log(LOG_WARNING, "unhandled framehook event %u\n", event);
		ao2_ref(details, -1);
		return f;
	}

	/* handle control frames */
	if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_T38_PARAMETERS) {
		ao2_ref(details, -1);
		return fax_gateway_detect_t38(gateway, chan, peer, active, f);
	}

	if (!gateway->detected_v21 && gateway->t38_state == T38_STATE_UNAVAILABLE && f->frametype == AST_FRAME_VOICE) {
		/* not in gateway mode and have not detected v21 yet, listen
		 * for v21 */
		ao2_ref(details, -1);
		return fax_gateway_detect_v21(gateway, chan, peer, active, f);
	}

	/* in gateway mode, gateway some packets */
	if (gateway->t38_state == T38_STATE_NEGOTIATED) {
		/* framehooks are called in __ast_read() before frame format
		 * translation is done, so we need to translate here */
		if ((f->frametype == AST_FRAME_VOICE) && (f->subclass.format.id != AST_FORMAT_SLINEAR)) {
			if (ast_channel_readtrans(active) && (f = ast_translate(ast_channel_readtrans(active), f, 1)) == NULL) {
				f = &ast_null_frame;
				ao2_ref(details, -1);
				return f;
			}
		}

		/* XXX we ignore the return value here, perhaps we should
		 * disable the gateway if a write fails. I am not sure how a
		 * write would fail, or even if a failure would be fatal so for
		 * now we'll just ignore the return value. */
		gateway->s->tech->write(gateway->s, f);
		if ((f->frametype == AST_FRAME_VOICE) && (f->subclass.format.id != AST_FORMAT_SLINEAR) && ast_channel_readtrans(active)) {
			/* Only free the frame if we translated / duplicated it - otherwise,
			 * let whatever is outside the frame hook do it */
			ast_frfree(f);
		}
		f = &ast_null_frame;
		ao2_ref(details, -1);
		return f;
	}

	/* force silence on the line if T.38 negotiation might be taking place */
	if (gateway->t38_state != T38_STATE_UNAVAILABLE && gateway->t38_state != T38_STATE_REJECTED) {
		if (f->frametype == AST_FRAME_VOICE && f->subclass.format.id == AST_FORMAT_SLINEAR) {
			short silence_buf[f->samples];
			struct ast_frame silence_frame = {
				.frametype = AST_FRAME_VOICE,
				.data.ptr = silence_buf,
				.samples = f->samples,
				.datalen = sizeof(silence_buf),
			};
			ast_format_set(&silence_frame.subclass.format, AST_FORMAT_SLINEAR, 0);
			memset(silence_buf, 0, sizeof(silence_buf));

			ao2_ref(details, -1);
			return ast_frisolate(&silence_frame);
		} else {
			ao2_ref(details, -1);
			return &ast_null_frame;
		}
	}

	ao2_ref(details, -1);
	return f;
}

/*! \brief Attach a gateway framehook object to a channel.
 * \param chan the channel to attach to
 * \param details fax session details
 * \return the framehook id of the attached framehook or -1 on error
 * \retval -1 error
 */
static int fax_gateway_attach(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	struct fax_gateway *gateway;
	struct ast_framehook_interface fr_hook = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = fax_gateway_framehook,
		.destroy_cb = fax_gateway_framehook_destroy,
	};

	ast_string_field_set(details, result, "SUCCESS");
	ast_string_field_set(details, resultstr, "gateway operation started successfully");
	ast_string_field_set(details, error, "NO_ERROR");
	set_channel_variables(chan, details);

	/* set up the frame hook*/
	gateway = fax_gateway_new(chan, details);
	if (!gateway) {
		ast_string_field_set(details, result, "FAILED");
		ast_string_field_set(details, resultstr, "error initializing gateway session");
		ast_string_field_set(details, error, "INIT_ERROR");
		set_channel_variables(chan, details);
		report_fax_status(chan, details, "No Available Resource");
		return -1;
	}

	fr_hook.data = gateway;
	ast_channel_lock(chan);
	gateway->framehook = ast_framehook_attach(chan, &fr_hook);
	ast_channel_unlock(chan);

	if (gateway->framehook < 0) {
		ao2_ref(gateway, -1);
		ast_string_field_set(details, result, "FAILED");
		ast_string_field_set(details, resultstr, "error attaching gateway to channel");
		ast_string_field_set(details, error, "INIT_ERROR");
		set_channel_variables(chan, details);
		return -1;
	}

	return gateway->framehook;
}

/*! \brief destroy a FAX detect structure */
static void destroy_faxdetect(void *data)
{
	struct fax_detect *faxdetect = data;

	if (faxdetect->dsp) {
		ast_dsp_free(faxdetect->dsp);
		faxdetect->dsp = NULL;
	}
	ao2_ref(faxdetect->details, -1);
}

/*! \brief Create a new fax detect object.
 * \param chan the channel attaching to
 * \param timeout remove framehook in this time if set
 * \param flags required options
 * \return NULL or a fax gateway object
 */
static struct fax_detect *fax_detect_new(struct ast_channel *chan, int timeout, int flags)
{
	struct fax_detect *faxdetect = ao2_alloc(sizeof(*faxdetect), destroy_faxdetect);
	if (!faxdetect) {
		return NULL;
	}

	faxdetect->flags = flags;

	if (timeout) {
		faxdetect->timeout_start = ast_tvnow();
	} else {
		faxdetect->timeout_start.tv_sec = 0;
		faxdetect->timeout_start.tv_usec = 0;
	}

	if (faxdetect->flags & FAX_DETECT_MODE_CNG) {
		faxdetect->dsp = ast_dsp_new();
		if (!faxdetect->dsp) {
			ao2_ref(faxdetect, -1);
			return NULL;
		}
		ast_dsp_set_features(faxdetect->dsp, DSP_FEATURE_FAX_DETECT);
		ast_dsp_set_faxmode(faxdetect->dsp, DSP_FAXMODE_DETECT_CNG | DSP_FAXMODE_DETECT_SQUELCH);
	} else {
		faxdetect->dsp = NULL;
	}

	return faxdetect;
}

/*! \brief Deref the faxdetect data structure when the faxdetect framehook is detached
 * \param data framehook data (faxdetect data)*/
static void fax_detect_framehook_destroy(void *data) {
	struct fax_detect *faxdetect = data;

	ao2_ref(faxdetect, -1);
}

/*! \brief Fax Detect Framehook
 *
 * Listen for fax tones in audio path and enable jumping to a extension when detected.
 *
 * \param chan channel
 * \param f frame to handle may be NULL
 * \param event framehook event
 * \param data framehook data (struct fax_detect *)
 *
 * \return processed frame or NULL when f is NULL or a null frame
 */
static struct ast_frame *fax_detect_framehook(struct ast_channel *chan, struct ast_frame *f, enum ast_framehook_event event, void *data) {
	struct fax_detect *faxdetect = data;
	struct ast_fax_session_details *details;
	struct ast_control_t38_parameters *control_params;
	struct ast_channel *peer;
	int result = 0;

	details = faxdetect->details;

	switch (event) {
	case AST_FRAMEHOOK_EVENT_ATTACHED:
		/* Setup format for DSP on ATTACH*/
		ast_format_copy(&faxdetect->orig_format, ast_channel_readformat(chan));
		switch (ast_channel_readformat(chan)->id) {
			case AST_FORMAT_SLINEAR:
			case AST_FORMAT_ALAW:
			case AST_FORMAT_ULAW:
				break;
			default:
				if (ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR)) {
					ast_framehook_detach(chan, details->faxdetect_id);
					details->faxdetect_id = -1;
					return f;
				}
		}
		return NULL;
	case AST_FRAMEHOOK_EVENT_DETACHED:
		/* restore audio formats when we are detached */
		ast_set_read_format(chan, &faxdetect->orig_format);
		if ((peer = ast_bridged_channel(chan))) {
			ast_channel_make_compatible(chan, peer);
		}
		return NULL;
	case AST_FRAMEHOOK_EVENT_READ:
		if (f) {
			break;
		}
	default:
		return f;
	};

	if (details->faxdetect_id < 0) {
		return f;
	}

	if ((!ast_tvzero(faxdetect->timeout_start) &&
	    (ast_tvdiff_ms(ast_tvnow(), faxdetect->timeout_start) > faxdetect->timeout))) {
		ast_framehook_detach(chan, details->faxdetect_id);
		details->faxdetect_id = -1;
		return f;
	}

	/* only handle VOICE and CONTROL frames*/
	switch (f->frametype) {
	case AST_FRAME_VOICE:
		/* we have no DSP this means we not detecting CNG */
		if (!faxdetect->dsp) {
			return f;
		}
		/* We can only process some formats*/
		switch (f->subclass.format.id) {
			case AST_FORMAT_SLINEAR:
			case AST_FORMAT_ALAW:
			case AST_FORMAT_ULAW:
				break;
			default:
				return f;
		}
		break;
	case AST_FRAME_CONTROL:
		if ((f->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
		    (faxdetect->flags & FAX_DETECT_MODE_T38)) {
			break;
		}
		return f;
	default:
		return f;
	}

	if (f->frametype == AST_FRAME_VOICE) {
		f = ast_dsp_process(chan, faxdetect->dsp, f);
		if (f->frametype == AST_FRAME_DTMF) {
			result = f->subclass.integer;
		}
	} else if ((f->frametype == AST_FRAME_CONTROL) && (f->datalen == sizeof(struct ast_control_t38_parameters))) {
		control_params = f->data.ptr;
		switch (control_params->request_response) {
		case AST_T38_NEGOTIATED:
		case AST_T38_REQUEST_NEGOTIATE:
			result = 't';
			break;
		default:
			break;
		}
	}

	if (result) {
		const char *target_context = S_OR(ast_channel_macrocontext(chan), ast_channel_context(chan));
		switch (result) {
		case 'f':
		case 't':
			ast_channel_unlock(chan);
			if (ast_exists_extension(chan, target_context, "fax", 1,
			    S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
				ast_channel_lock(chan);
				ast_verb(2, "Redirecting '%s' to fax extension due to %s detection\n",
					ast_channel_name(chan), (result == 'f') ? "CNG" : "T38");
				pbx_builtin_setvar_helper(chan, "FAXEXTEN", ast_channel_exten(chan));
				if (ast_async_goto(chan, target_context, "fax", 1)) {
					ast_log(LOG_NOTICE, "Failed to async goto '%s' into fax of '%s'\n", ast_channel_name(chan), target_context);
				}
				ast_frfree(f);
				f = &ast_null_frame;
			} else {
				ast_channel_lock(chan);
				ast_log(LOG_NOTICE, "FAX %s detected but no fax extension in context (%s)\n",
					(result == 'f') ? "CNG" : "T38", target_context);
			}
		}
		ast_framehook_detach(chan, details->faxdetect_id);
		details->faxdetect_id = -1;
	}

	return f;
}

/*! \brief Attach a faxdetect framehook object to a channel.
 * \param chan the channel to attach to
 * \param timeout remove framehook in this time if set
 * \return the faxdetect structure or NULL on error
 * \param flags required options
 * \retval -1 error
 */
static int fax_detect_attach(struct ast_channel *chan, int timeout, int flags)
{
	struct fax_detect *faxdetect;
	struct ast_fax_session_details *details;
	struct ast_framehook_interface fr_hook = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = fax_detect_framehook,
		.destroy_cb = fax_detect_framehook_destroy,
	};

	if (!(details = find_or_create_details(chan))) {
		ast_log(LOG_ERROR, "System cannot provide memory for session requirements.\n");
		return -1;
	}

	/* set up the frame hook*/
	faxdetect = fax_detect_new(chan, timeout, flags);
	if (!faxdetect) {
		ao2_ref(details, -1);
		return -1;
	}

	fr_hook.data = faxdetect;
	faxdetect->details = details;
	ast_channel_lock(chan);
	details->faxdetect_id = ast_framehook_attach(chan, &fr_hook);
	ast_channel_unlock(chan);

	if (details->faxdetect_id < 0) {
		ao2_ref(faxdetect, -1);
	}

	return details->faxdetect_id;
}

/*! \brief hash callback for ao2 */
static int session_hash_cb(const void *obj, const int flags)
{
	const struct ast_fax_session *s = obj;

	return s->id;
}

/*! \brief compare callback for ao2 */
static int session_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_fax_session *lhs = obj, *rhs = arg;

	return (lhs->id == rhs->id) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief fax session tab completion */
static char *fax_session_tab_complete(struct ast_cli_args *a)
{
	int tklen;
	int wordnum = 0;
	char *name = NULL;
	struct ao2_iterator i;
	struct ast_fax_session *s;
	char tbuf[5];

	if (a->pos != 3) {
		return NULL;
	}

	tklen = strlen(a->word);
	i = ao2_iterator_init(faxregistry.container, 0);
	while ((s = ao2_iterator_next(&i))) {
		snprintf(tbuf, sizeof(tbuf), "%u", s->id);
		if (!strncasecmp(a->word, tbuf, tklen) && ++wordnum > a->n) {
			name = ast_strdup(tbuf);
			ao2_ref(s, -1);
			break;
		}
		ao2_ref(s, -1);
	}
	ao2_iterator_destroy(&i);
	return name;
}

static char *cli_fax_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct fax_module *fax;

	switch(cmd) {
	case CLI_INIT:
		e->command = "fax show version";
		e->usage =
			"Usage: fax show version\n"
			"       Show versions of FAX For Asterisk components.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "FAX For Asterisk Components:\n");
	ast_cli(a->fd, "\tApplications: %s\n", ast_get_version());
	AST_RWLIST_RDLOCK(&faxmodules);
	AST_RWLIST_TRAVERSE(&faxmodules, fax, list) {
		ast_cli(a->fd, "\t%s: %s\n", fax->tech->description, fax->tech->version);
	}
	AST_RWLIST_UNLOCK(&faxmodules);
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

/*! \brief enable FAX debugging */
static char *cli_fax_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int flag;
	const char *what;

	switch (cmd) {
	case CLI_INIT:
		e->command = "fax set debug {on|off}";
		e->usage =
			"Usage: fax set debug { on | off }\n"
			"       Enable/Disable FAX debugging on new FAX sessions.  The basic FAX debugging will result in\n"
			"       additional events sent to manager sessions with 'call' class permissions.  When\n"
			"       verbosity is greater than '5' events will be displayed to the console and audio versus\n"
			"       energy analysis will be performed and displayed to the console.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	what = a->argv[e->args-1];      /* guaranteed to exist */
	if (!strcasecmp(what, "on")) {
		flag = 1;
	} else if (!strcasecmp(what, "off")) {
		flag = 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	global_fax_debug = flag;
	ast_cli(a->fd, "\n\nFAX Debug %s\n\n", (flag) ? "Enabled" : "Disabled");

	return CLI_SUCCESS;
}

/*! \brief display registered FAX capabilities */
static char *cli_fax_show_capabilities(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct fax_module *fax;
	unsigned int num_modules = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "fax show capabilities";
		e->usage =
			"Usage: fax show capabilities\n"
			"       Shows the capabilities of the registered FAX technology modules\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\n\nRegistered FAX Technology Modules:\n\n");
	AST_RWLIST_RDLOCK(&faxmodules);
	AST_RWLIST_TRAVERSE(&faxmodules, fax, list) {
		ast_cli(a->fd, "%-15s : %s\n%-15s : %s\n%-15s : ", "Type", fax->tech->type, "Description", fax->tech->description, "Capabilities");
		fax->tech->cli_show_capabilities(a->fd);
		num_modules++;
	}
	AST_RWLIST_UNLOCK(&faxmodules);
	ast_cli(a->fd, "%u registered modules\n\n", num_modules);

	return CLI_SUCCESS;
}

/*! \brief display global defaults and settings */
static char *cli_fax_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct fax_module *fax;
	char modems[128] = "";
	struct fax_options options;

	switch (cmd) {
	case CLI_INIT:
		e->command = "fax show settings";
		e->usage =
			"Usage: fax show settings\n"
			"       Show the global settings and defaults of both the FAX core and technology modules\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	get_general_options(&options);

	ast_cli(a->fd, "FAX For Asterisk Settings:\n");
	ast_cli(a->fd, "\tECM: %s\n", options.ecm ? "Enabled" : "Disabled");
	ast_cli(a->fd, "\tStatus Events: %s\n",  options.statusevents ? "On" : "Off");
	ast_cli(a->fd, "\tMinimum Bit Rate: %u\n", options.minrate);
	ast_cli(a->fd, "\tMaximum Bit Rate: %u\n", options.maxrate);
	ast_fax_modem_to_str(options.modems, modems, sizeof(modems));
	ast_cli(a->fd, "\tModem Modulations Allowed: %s\n", modems);
	ast_cli(a->fd, "\n\nFAX Technology Modules:\n\n");
	AST_RWLIST_RDLOCK(&faxmodules);
	AST_RWLIST_TRAVERSE(&faxmodules, fax, list) {
		ast_cli(a->fd, "%s (%s) Settings:\n", fax->tech->type, fax->tech->description);
		fax->tech->cli_show_settings(a->fd);
	}
	AST_RWLIST_UNLOCK(&faxmodules);

	return CLI_SUCCESS;
}

/*! \brief display details of a specified fax session */
static char *cli_fax_show_session(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_fax_session *s, tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "fax show session";
		e->usage =
			"Usage: fax show session <session number>\n"
			"       Shows status of the named FAX session\n";
		return NULL;
	case CLI_GENERATE:
		return fax_session_tab_complete(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[3], "%u", &tmp.id) != 1) {
		ast_log(LOG_ERROR, "invalid session id: '%s'\n", a->argv[3]);
		return RESULT_SUCCESS;
	}

	ast_cli(a->fd, "\nFAX Session Details:\n--------------------\n\n");
	s = ao2_find(faxregistry.container, &tmp, OBJ_POINTER);
	if (s) {
		s->tech->cli_show_session(s, a->fd);
		ao2_ref(s, -1);
	}
	ast_cli(a->fd, "\n\n");

	return CLI_SUCCESS;
}

/*! \brief display fax stats */
static char *cli_fax_show_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct fax_module *fax;

	switch (cmd) {
	case CLI_INIT:
		e->command = "fax show stats";
		e->usage =
			"Usage: fax show stats\n"
			"       Shows a statistical summary of FAX transmissions\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\nFAX Statistics:\n---------------\n\n");
	ast_cli(a->fd, "%-20.20s : %d\n", "Current Sessions", faxregistry.active_sessions);
	ast_cli(a->fd, "%-20.20s : %d\n", "Reserved Sessions", faxregistry.reserved_sessions);
	ast_cli(a->fd, "%-20.20s : %d\n", "Transmit Attempts", faxregistry.fax_tx_attempts);
	ast_cli(a->fd, "%-20.20s : %d\n", "Receive Attempts", faxregistry.fax_rx_attempts);
	ast_cli(a->fd, "%-20.20s : %d\n", "Completed FAXes", faxregistry.fax_complete);
	ast_cli(a->fd, "%-20.20s : %d\n", "Failed FAXes", faxregistry.fax_failures);
	AST_RWLIST_RDLOCK(&faxmodules);
	AST_RWLIST_TRAVERSE(&faxmodules, fax, list) {
		fax->tech->cli_show_stats(a->fd);
	}
	AST_RWLIST_UNLOCK(&faxmodules);
	ast_cli(a->fd, "\n\n");

	return CLI_SUCCESS;
}

static const char *cli_session_type(struct ast_fax_session *s)
{
	if (s->details->caps & AST_FAX_TECH_AUDIO) {
		return "G.711";
	}
	if (s->details->caps & AST_FAX_TECH_T38) {
		return "T.38";
	}

	return "none";
}

static const char *cli_session_operation(struct ast_fax_session *s)
{
	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		return "gateway";
	}
	if (s->details->caps & AST_FAX_TECH_SEND) {
		return "send";
	}
	if (s->details->caps & AST_FAX_TECH_RECEIVE) {
		return "receive";
	}
	if (s->details->caps & AST_FAX_TECH_V21_DETECT) {
		return "V.21";
	}

	return "none";
}

/*! \brief display fax sessions */
static char *cli_fax_show_sessions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_fax_session *s;
	struct ao2_iterator i;
	int session_count;
	char *filenames;

	switch (cmd) {
	case CLI_INIT:
		e->command = "fax show sessions";
		e->usage =
			"Usage: fax show sessions\n"
			"       Shows the current FAX sessions\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\nCurrent FAX Sessions:\n\n");
	ast_cli(a->fd, "%-20.20s %-10.10s %-10.10s %-5.5s %-10.10s %-15.15s %-30.30s\n",
		"Channel", "Tech", "FAXID", "Type", "Operation", "State", "File(s)");
	i = ao2_iterator_init(faxregistry.container, 0);
	while ((s = ao2_iterator_next(&i))) {
		ao2_lock(s);

		filenames = generate_filenames_string(s->details, "", ", ");

		ast_cli(a->fd, "%-20.20s %-10.10s %-10u %-5.5s %-10.10s %-15.15s %-30s\n",
			s->channame, s->tech->type, s->id,
			cli_session_type(s),
			cli_session_operation(s),
			ast_fax_state_to_str(s->state), S_OR(filenames, ""));

		ast_free(filenames);
		ao2_unlock(s);
		ao2_ref(s, -1);
	}
	ao2_iterator_destroy(&i);
	session_count = ao2_container_count(faxregistry.container);
	ast_cli(a->fd, "\n%d FAX sessions\n\n", session_count);

	return CLI_SUCCESS;
}

static struct ast_cli_entry fax_cli[] = {
	AST_CLI_DEFINE(cli_fax_show_version, "Show versions of FAX For Asterisk components"),
	AST_CLI_DEFINE(cli_fax_set_debug, "Enable/Disable FAX debugging on new FAX sessions"),
	AST_CLI_DEFINE(cli_fax_show_capabilities, "Show the capabilities of the registered FAX technology modules"),
	AST_CLI_DEFINE(cli_fax_show_settings, "Show the global settings and defaults of both the FAX core and technology modules"),
	AST_CLI_DEFINE(cli_fax_show_session, "Show the status of the named FAX sessions"),
	AST_CLI_DEFINE(cli_fax_show_sessions, "Show the current FAX sessions"),
	AST_CLI_DEFINE(cli_fax_show_stats, "Summarize FAX session history"),
};

static void set_general_options(const struct fax_options *options)
{
	ast_rwlock_wrlock(&options_lock);
	general_options = *options;
	ast_rwlock_unlock(&options_lock);
}

static void get_general_options(struct fax_options *options)
{
	ast_rwlock_rdlock(&options_lock);
	*options = general_options;
	ast_rwlock_unlock(&options_lock);
}

/*! \brief configure res_fax */
static int set_config(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	char modems[128] = "";
	struct fax_options options;
	int res = 0;

	options = default_options;

	/* When we're not reloading, we have to be certain to set the general options
	 * to the defaults in case config loading goes wrong at some point. On a reload,
	 * the general options need to stay the same as what they were prior to the
	 * reload rather than being reset to the defaults.
	 */
	if (!reload) {
		set_general_options(&options);
	}

	/* read configuration */
	if (!(cfg = ast_config_load2(config, "res_fax", config_flags))) {
		ast_log(LOG_NOTICE, "Configuration file '%s' not found, %s options.\n",
				config, reload ? "not changing" : "using default");
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_NOTICE, "Configuration file '%s' is invalid, %s options.\n",
				config, reload ? "not changing" : "using default");
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (reload) {
		options = default_options;
	}

	/* create configuration */
	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		int rate;

		if (!strcasecmp(v->name, "minrate")) {
			ast_debug(3, "reading minrate '%s' from configuration file\n", v->value);
			if ((rate = fax_rate_str_to_int(v->value)) == 0) {
				res = -1;
				goto end;
			}
			options.minrate = rate;
		} else if (!strcasecmp(v->name, "maxrate")) {
			ast_debug(3, "reading maxrate '%s' from configuration file\n", v->value);
			if ((rate = fax_rate_str_to_int(v->value)) == 0) {
				res = -1;
				goto end;
			}
			options.maxrate = rate;
		} else if (!strcasecmp(v->name, "statusevents")) {
			ast_debug(3, "reading statusevents '%s' from configuration file\n", v->value);
			options.statusevents = ast_true(v->value);
		} else if (!strcasecmp(v->name, "ecm")) {
			ast_debug(3, "reading ecm '%s' from configuration file\n", v->value);
			options.ecm = ast_true(v->value);
		} else if ((!strcasecmp(v->name, "modem")) || (!strcasecmp(v->name, "modems"))) {
			options.modems = 0;
			update_modem_bits(&options.modems, v->value);
		}
	}

	if (options.maxrate < options.minrate) {
		ast_log(LOG_ERROR, "maxrate %u is less than minrate %u\n", options.maxrate, options.minrate);
		res = -1;
		goto end;
	}

	if (options.minrate == 2400 && (options.modems & AST_FAX_MODEM_V27) && !(options.modems & (AST_FAX_MODEM_V34))) {
		ast_fax_modem_to_str(options.modems, modems, sizeof(modems));
		ast_log(LOG_WARNING, "'modems' setting '%s' is no longer accepted with 'minrate' setting %u\n", modems, options.minrate);
		ast_log(LOG_WARNING, "'minrate' has been reset to 4800, please update res_fax.conf.\n");
		options.minrate = 4800;
	}

	if (check_modem_rate(options.modems, options.minrate)) {
		ast_fax_modem_to_str(options.modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'minrate' setting %u\n", modems, options.minrate);
		res = -1;
		goto end;
	}

	if (check_modem_rate(options.modems, options.maxrate)) {
		ast_fax_modem_to_str(options.modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'maxrate' setting %u\n", modems, options.maxrate);
		res = -1;
		goto end;
	}

	set_general_options(&options);

end:
	ast_config_destroy(cfg);
	return res;
}

/*! \brief FAXOPT read function returns the contents of a FAX option */
static int acf_faxopt_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_fax_session_details *details = find_details(chan);
	int res = 0;
	char *filenames;

	if (!details) {
		ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s) because it has never been written.\n", ast_channel_name(chan), data);
		return -1;
	}
	if (!strcasecmp(data, "ecm")) {
		ast_copy_string(buf, details->option.ecm ? "yes" : "no", len);
	} else if (!strcasecmp(data, "t38gateway") || !strcasecmp(data, "gateway") ||
		   !strcasecmp(data, "t38_gateway") || !strcasecmp(data, "faxgateway")) {
		ast_copy_string(buf, details->gateway_id != -1 ? "yes" : "no", len);
	} else if (!strcasecmp(data, "faxdetect")) {
		ast_copy_string(buf, details->faxdetect_id != -1 ? "yes" : "no", len);
	} else if (!strcasecmp(data, "error")) {
		ast_copy_string(buf, details->error, len);
	} else if (!strcasecmp(data, "filename")) {
		if (AST_LIST_EMPTY(&details->documents)) {
			ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s) because it has never been written.\n", ast_channel_name(chan), data);
			res = -1;
		} else {
			ast_copy_string(buf, AST_LIST_FIRST(&details->documents)->filename, len);
		}
	} else if (!strcasecmp(data, "filenames")) {
		if (AST_LIST_EMPTY(&details->documents)) {
			ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s) because it has never been written.\n", ast_channel_name(chan), data);
			res = -1;
		} else if ((filenames = generate_filenames_string(details, "", ","))) {
			ast_copy_string(buf, filenames, len);
			ast_free(filenames);
		} else {
			ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s), there was an error generating the filenames list.\n", ast_channel_name(chan), data);
			res = -1;
		}
	} else if (!strcasecmp(data, "headerinfo")) {
		ast_copy_string(buf, details->headerinfo, len);
	} else if (!strcasecmp(data, "localstationid")) {
		ast_copy_string(buf, details->localstationid, len);
	} else if (!strcasecmp(data, "maxrate")) {
		snprintf(buf, len, "%u", details->maxrate);
	} else if (!strcasecmp(data, "minrate")) {
		snprintf(buf, len, "%u", details->minrate);
	} else if (!strcasecmp(data, "pages")) {
		snprintf(buf, len, "%u", details->pages_transferred);
	} else if (!strcasecmp(data, "rate")) {
		ast_copy_string(buf, details->transfer_rate, len);
	} else if (!strcasecmp(data, "remotestationid")) {
		ast_copy_string(buf, details->remotestationid, len);
	} else if (!strcasecmp(data, "resolution")) {
		ast_copy_string(buf, details->resolution, len);
	} else if (!strcasecmp(data, "sessionid")) {
		snprintf(buf, len, "%u", details->id);
	} else if (!strcasecmp(data, "status")) {
		ast_copy_string(buf, details->result, len);
	} else if (!strcasecmp(data, "statusstr")) {
		ast_copy_string(buf, details->resultstr, len);
	} else if ((!strcasecmp(data, "modem")) || (!strcasecmp(data, "modems"))) {
		ast_fax_modem_to_str(details->modems, buf, len);
	} else {
		ast_log(LOG_WARNING, "channel '%s' can't read FAXOPT(%s) because it is unhandled!\n", ast_channel_name(chan), data);
		res = -1;
	}
	ao2_ref(details, -1);

	return res;
}

/*! \brief FAXOPT write function modifies the contents of a FAX option */
static int acf_faxopt_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	int res = 0;
	struct ast_fax_session_details *details;

	if (!(details = find_or_create_details(chan))) {
		ast_log(LOG_WARNING, "channel '%s' can't set FAXOPT(%s) to '%s' because it failed to create a datastore.\n", ast_channel_name(chan), data, value);
		return -1;
	}
	ast_debug(3, "channel '%s' setting FAXOPT(%s) to '%s'\n", ast_channel_name(chan), data, value);

	if (!strcasecmp(data, "ecm")) {
		const char *val = ast_skip_blanks(value);
		if (ast_true(val)) {
			details->option.ecm = AST_FAX_OPTFLAG_TRUE;
		} else if (ast_false(val)) {
			details->option.ecm = AST_FAX_OPTFLAG_FALSE;
		} else {
			ast_log(LOG_WARNING, "Unsupported value '%s' passed to FAXOPT(ecm).\n", value);
		}
	} else if (!strcasecmp(data, "t38gateway") || !strcasecmp(data, "gateway") ||
		   !strcasecmp(data, "t38_gateway") || !strcasecmp(data, "faxgateway")) {
		const char *val = ast_skip_blanks(value);
		char *timeout = strchr(val, ',');

		if (timeout) {
			*timeout++ = '\0';
		}

		if (ast_true(val)) {
			if (details->gateway_id < 0) {
				details->gateway_timeout = 0;
				if (timeout) {
					unsigned int gwtimeout;
					if (sscanf(timeout, "%u", &gwtimeout) == 1) {
						details->gateway_timeout = gwtimeout * 1000;
					} else {
						ast_log(LOG_WARNING, "Unsupported timeout '%s' passed to FAXOPT(%s).\n", timeout, data);
					}
				}

				details->gateway_id = fax_gateway_attach(chan, details);
				if (details->gateway_id < 0) {
					ast_log(LOG_ERROR, "Error attaching T.38 gateway to channel %s.\n", ast_channel_name(chan));
					res = -1;
				} else {
					ast_debug(1, "Attached T.38 gateway to channel %s.\n", ast_channel_name(chan));
				}
			} else {
				ast_log(LOG_WARNING, "Attempt to attach a T.38 gateway on channel (%s) with gateway already running.\n", ast_channel_name(chan));
			}
		} else if (ast_false(val)) {
			ast_framehook_detach(chan, details->gateway_id);
			details->gateway_id = -1;
		} else {
			ast_log(LOG_WARNING, "Unsupported value '%s' passed to FAXOPT(%s).\n", value, data);
		}
	} else if (!strcasecmp(data, "faxdetect")) {
		const char *val = ast_skip_blanks(value);
		char *timeout = strchr(val, ',');
		unsigned int fdtimeout = 0;
		int flags;
		int faxdetect;

		if (timeout) {
			*timeout++ = '\0';
		}

		if (ast_true(val) || !strcasecmp(val, "t38") || !strcasecmp(val, "cng")) {
			if (details->faxdetect_id < 0) {
				if (timeout && (sscanf(timeout, "%u", &fdtimeout) == 1)) {
					if (fdtimeout > 0) {
						fdtimeout = fdtimeout * 1000;
					} else {
						ast_log(LOG_WARNING, "Timeout cannot be negative ignoring timeout\n");
					}
				}

				if (!strcasecmp(val, "t38")) {
					flags = FAX_DETECT_MODE_T38;
				} else if (!strcasecmp(val, "cng")) {
					flags = FAX_DETECT_MODE_CNG;
				} else {
					flags = FAX_DETECT_MODE_BOTH;
				}

				faxdetect = fax_detect_attach(chan, fdtimeout, flags);
				if (faxdetect < 0) {
					ast_log(LOG_ERROR, "Error attaching FAX detect to channel %s.\n", ast_channel_name(chan));
					res = -1;
				} else {
					ast_debug(1, "Attached FAX detect to channel %s.\n", ast_channel_name(chan));
				}
			} else {
				ast_log(LOG_WARNING, "Attempt to attach a FAX detect on channel (%s) with FAX detect already running.\n", ast_channel_name(chan));
			}
		} else if (ast_false(val)) {
			ast_framehook_detach(chan, details->faxdetect_id);
			details->faxdetect_id = -1;
		} else {
			ast_log(LOG_WARNING, "Unsupported value '%s' passed to FAXOPT(%s).\n", value, data);
		}
	} else if (!strcasecmp(data, "headerinfo")) {
		ast_string_field_set(details, headerinfo, value);
	} else if (!strcasecmp(data, "localstationid")) {
		ast_string_field_set(details, localstationid, value);
	} else if (!strcasecmp(data, "maxrate")) {
		details->maxrate = fax_rate_str_to_int(value);
		if (!details->maxrate) {
			details->maxrate = ast_fax_maxrate();
		}
	} else if (!strcasecmp(data, "minrate")) {
		details->minrate = fax_rate_str_to_int(value);
		if (!details->minrate) {
			details->minrate = ast_fax_minrate();
		}
	} else if ((!strcasecmp(data, "modem")) || (!strcasecmp(data, "modems"))) {
		update_modem_bits(&details->modems, value);
	} else {
		ast_log(LOG_WARNING, "channel '%s' set FAXOPT(%s) to '%s' is unhandled!\n", ast_channel_name(chan), data, value);
		res = -1;
	}

	ao2_ref(details, -1);

	return res;
}

/*! \brief FAXOPT dialplan function */
struct ast_custom_function acf_faxopt = {
	.name = "FAXOPT",
	.read = acf_faxopt_read,
	.write = acf_faxopt_write,
};

/*! \brief unload res_fax */
static int unload_module(void)
{
	ast_cli_unregister_multiple(fax_cli, ARRAY_LEN(fax_cli));

	if (ast_custom_function_unregister(&acf_faxopt) < 0) {
		ast_log(LOG_WARNING, "failed to unregister function '%s'\n", acf_faxopt.name);
	}

	if (ast_unregister_application(app_sendfax) < 0) {
		ast_log(LOG_WARNING, "failed to unregister '%s'\n", app_sendfax);
	}

	if (ast_unregister_application(app_receivefax) < 0) {
		ast_log(LOG_WARNING, "failed to unregister '%s'\n", app_receivefax);
	}

	if (fax_logger_level != -1) {
		ast_logger_unregister_level("FAX");
	}

	ao2_ref(faxregistry.container, -1);

	return 0;
}

/*! \brief load res_fax */
static int load_module(void)
{
	int res;

	/* initialize the registry */
	faxregistry.active_sessions = 0;
	faxregistry.reserved_sessions = 0;
	if (!(faxregistry.container = ao2_container_alloc(FAX_MAXBUCKETS, session_hash_cb, session_cmp_cb))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (set_config(0) < 0) {
		ast_log(LOG_ERROR, "failed to load configuration file '%s'\n", config);
		ao2_ref(faxregistry.container, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* register CLI operations and applications */
	if (ast_register_application_xml(app_sendfax, sendfax_exec) < 0) {
		ast_log(LOG_WARNING, "failed to register '%s'.\n", app_sendfax);
		ao2_ref(faxregistry.container, -1);
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_register_application_xml(app_receivefax, receivefax_exec) < 0) {
		ast_log(LOG_WARNING, "failed to register '%s'.\n", app_receivefax);
		ast_unregister_application(app_sendfax);
		ao2_ref(faxregistry.container, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(fax_cli, ARRAY_LEN(fax_cli));
	res = ast_custom_function_register(&acf_faxopt);
	fax_logger_level = ast_logger_register_level("FAX");

	return res;
}

static int reload_module(void)
{
	set_config(1);
	return 0;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Generic FAX Applications",
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
