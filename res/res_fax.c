/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008-2009, Digium, Inc.
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
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
***/

/*! \file
 *
 * \brief Generic FAX Resource for FAX technology resource modules
 *
 * \author Dwayne M. Hubbard <dhubbard@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
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

/*** DOCUMENTATION
	<application name="ReceiveFax" language="en_US">
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
	<application name="SendFax" language="en_US">
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
	<function name="FAXOPT" language="en_US">
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

static int fax_logger_level = -1;

/*! \brief maximum buckets for res_fax ao2 containers */
#define FAX_MAXBUCKETS 10

#define RES_FAX_TIMEOUT 10000

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

#define RES_FAX_MINRATE 2400
#define RES_FAX_MAXRATE 14400
#define RES_FAX_STATUSEVENTS 0
#define RES_FAX_MODEM (AST_FAX_MODEM_V17 | AST_FAX_MODEM_V27 | AST_FAX_MODEM_V29)

static struct {
	enum ast_fax_modems modems;
	uint32_t statusevents:1;
	uint32_t ecm:1;
	unsigned int minrate;	
	unsigned int maxrate;
} general_options;

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
			ast_verb(6, "Channel '%s' fax session '%d', [ %.3ld.%.6ld ], %s sent %d frames (%d ms) of %s.\n",
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
		ast_log(LOG_WARNING, "Huh?  channel '%s' has a FAX datastore without data!\n", chan->name);
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

	if (!(d = ao2_alloc(sizeof(*d), destroy_session_details))) {
		return NULL;
	}
	
	if (ast_string_field_init(d, 512)) {
		ao2_ref(d, -1);
		return NULL;
	}

	AST_LIST_HEAD_INIT_NOLOCK(&d->documents);

	/* These options need to be set to the configured default and may be overridden by
 	 * SendFAX, ReceiveFAX, or FAXOPT */
	d->option.request_t38 = AST_FAX_OPTFLAG_FALSE;
	d->option.send_cng = AST_FAX_OPTFLAG_FALSE;
	d->option.send_ced = AST_FAX_OPTFLAG_FALSE;
	d->option.ecm = general_options.ecm;
	d->option.statusevents = general_options.statusevents;
	d->modems = general_options.modems;
	d->minrate = general_options.minrate;
	d->maxrate = general_options.maxrate;

	return d;
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
		ast_log(LOG_WARNING, "channel '%s' can't get a FAX details structure for the datastore!\n", chan->name);
		return NULL;
	}
	if (!(datastore = ast_datastore_alloc(&fax_datastore, NULL))) {
		ao2_ref(details, -1);
		ast_log(LOG_WARNING, "channel '%s' can't get a datastore!\n", chan->name);
		return NULL;
	}
	/* add the datastore to the channel and increment the refcount */
	datastore->data = details;
	ao2_ref(details, 1);
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
	return details;
}

unsigned int ast_fax_maxrate(void)
{
	return general_options.maxrate;
}

unsigned int ast_fax_minrate(void)
{
	return general_options.minrate;
}

static int update_modem_bits(enum ast_fax_modems *bits, const char *value)
{		
	char *m[5], *tok, *v = (char *)value;
	int i = 0, j;

	if (!(tok = strchr(v, ','))) {
		m[i++] = v;
		m[i] = NULL;
	} else {
		tok = strtok(v, ", ");
		while (tok && (i < 5)) {
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
		if (!(modems & (AST_FAX_MODEM_V27 | AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 4800:
		if (!(modems & (AST_FAX_MODEM_V27 | AST_FAX_MODEM_V34))) {
			return 1;
		}
		break;
	case 7200:
	case 9600:
		if (!(modems & (AST_FAX_MODEM_V17 | AST_FAX_MODEM_V29 | AST_FAX_MODEM_V34))) {
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
		ast_log(LOG_WARNING, "unhandled FAX state: %d\n", state);
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

static struct ast_fax_session *fax_session_reserve(struct ast_fax_session_details *details, struct ast_fax_tech_token **token)
{
	struct ast_fax_session *s;
	struct fax_module *faxmod;

	if (!(s = ao2_alloc(sizeof(*s), destroy_session))) {
		return NULL;
	}

	s->state = AST_FAX_STATE_INACTIVE;

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
		ast_log(LOG_ERROR, "Could not locate a FAX technology module with capabilities (0x%X)\n", details->caps);
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

/*! \brief create a FAX session */
static struct ast_fax_session *fax_session_new(struct ast_fax_session_details *details, struct ast_channel *chan, struct ast_fax_session *reserved, struct ast_fax_tech_token *token)
{
	struct ast_fax_session *s = NULL;
	struct fax_module *faxmod;

	if (reserved) {
		s = reserved;
		ao2_ref(reserved, +1);

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

	if (!(s->channame = ast_strdup(chan->name))) {
		fax_session_release(s, token);
		ao2_ref(s, -1);
		return NULL;
	}

	if (!(s->chan_uniqueid = ast_strdup(chan->uniqueid))) {
		fax_session_release(s, token);
		ao2_ref(s, -1);
		return NULL;
	}

	s->chan = chan;
	s->details = details;
	ao2_ref(s->details, 1);

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
			s->tech = faxmod->tech;
			break;
		}
		AST_RWLIST_UNLOCK(&faxmodules);

		if (!faxmod) {
			ast_log(LOG_ERROR, "Could not locate a FAX technology module with capabilities (0x%X)\n", details->caps);
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
		ast_log(LOG_ERROR, "failed to add FAX session '%d' to container.\n", s->id);
		ao2_ref(s, -1);
		return NULL;
	}
	ast_debug(4, "channel '%s' using FAX session '%d'\n", s->channame, s->id);

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
	if (!filenames) {
		return 1;
	}

	ast_channel_lock(chan);
	if (details->option.statusevents) {
		struct manager_event_info info;

		get_manager_event_info(chan, &info);
		manager_event(EVENT_FLAG_CALL,
			      (details->caps & AST_FAX_TECH_RECEIVE) ? "ReceiveFAXStatus" : "SendFAXStatus",
			      "Status: %s\r\n"
			      "Channel: %s\r\n"
			      "Context: %s\r\n"
			      "Exten: %s\r\n"
			      "CallerID: %s\r\n"
			      "LocalStationID: %s\r\n"
			      "%s\r\n",
			      status,
			      chan->name,
			      info.context,
			      info.exten,
			      info.cid,
			      details->localstationid,
			      filenames);
	}
	ast_channel_unlock(chan);
	ast_free(filenames);

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

	snprintf(buf, sizeof(buf), "%d", details->pages_transferred);
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
		res = ms = -1; \
	} while (0)

#define GENERIC_FAX_EXEC_ERROR(fax, chan, errorstr, reason)	\
	do {	\
		ast_log(LOG_ERROR, "channel '%s' FAX session '%d' failure, reason: '%s' (%s)\n", chan->name, fax->id, reason, errorstr); \
		GENERIC_FAX_EXEC_ERROR_QUIET(fax, chan, errorstr, reason); \
	} while (0)

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

static int set_fax_t38_caps(struct ast_channel *chan, struct ast_fax_session_details *details)
{
	switch (ast_channel_get_t38_state(chan)) {
	case T38_STATE_UNKNOWN:
		details->caps |= AST_FAX_TECH_T38;
		break;
	case T38_STATE_UNAVAILABLE:
		details->caps |= AST_FAX_TECH_AUDIO;
		break;
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
			ast_log(LOG_ERROR, "channel '%s' is in an unsupported T.38 negotiation state, cannot continue.\n", chan->name);
			return -1;
		}
		details->caps |= AST_FAX_TECH_T38;
		break;
	}
	default:
		ast_log(LOG_ERROR, "channel '%s' is in an unsupported T.38 negotiation state, cannot continue.\n", chan->name);
		return -1;
	}

	return 0;
}

static int disable_t38(struct ast_channel *chan)
{
	int ms;
	struct ast_frame *frame = NULL;
	struct ast_control_t38_parameters t38_parameters = { .request_response = AST_T38_REQUEST_TERMINATE, };

	ast_debug(1, "Shutting down T.38 on %s\n", chan->name);
	if (ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) != 0) {
		ast_debug(1, "error while disabling T.38 on channel '%s'\n", chan->name);
		return -1;
	}

	/* wait up to five seconds for negotiation to complete */
	ms = 5000;

	while (ms > 0) {
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			ast_debug(1, "error while disabling T.38 on channel '%s'\n", chan->name);
			return -1;
		}

		if (ms == 0) { /* all done, nothing happened */
			ast_debug(1, "channel '%s' timed-out during T.38 shutdown\n", chan->name);
			break;
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
				ast_debug(1, "Shut down T.38 on %s\n", chan->name);
				break;
			case AST_T38_REFUSED:
				ast_log(LOG_WARNING, "channel '%s' refused to disable T.38\n", chan->name);
				ast_frfree(frame);
				return -1;
			default:
				ast_log(LOG_ERROR, "channel '%s' failed to disable T.38\n", chan->name);
				ast_frfree(frame);
				return -1;
			}
			ast_frfree(frame);
			break;
		}
		ast_frfree(frame);
	}

	return 0;
}

static struct ast_control_t38_parameters our_t38_parameters = {
	.version = 0,
	.max_ifp = 400,
	.rate = AST_T38_RATE_14400,
	.rate_management = AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF,
};

/*! \brief this is the generic FAX session handling function */
static int generic_fax_exec(struct ast_channel *chan, struct ast_fax_session_details *details, struct ast_fax_session *reserved, struct ast_fax_tech_token *token)
{
	int ms;
	int timeout = RES_FAX_TIMEOUT;
	int res = 0, chancount;
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
		ast_format_copy(&orig_write_format, &chan->writeformat);
		if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR) < 0) {
			ast_log(LOG_ERROR, "channel '%s' failed to set write format to signed linear'.\n", chan->name);
 			ao2_lock(faxregistry.container);
 			ao2_unlink(faxregistry.container, fax);
 			ao2_unlock(faxregistry.container);
 			ao2_ref(fax, -1);
			ast_channel_unlock(chan);
			return -1;
		}
		ast_format_copy(&orig_read_format, &chan->readformat);
		if (ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR) < 0) {
			ast_log(LOG_ERROR, "channel '%s' failed to set read format to signed linear.\n", chan->name);
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
			ast_log(LOG_WARNING, "Channel '%s' FAX session '%d' failed to obtain a smoother.\n", chan->name, fax->id);
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

	ast_debug(5, "channel %s will wait on FAX fd %d\n", chan->name, fax->fd);

	/* handle frames for the session */
	ms = 1000;
	while ((res > -1) && (ms > -1) && (timeout > 0)) {
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
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", chan->name);
				GENERIC_FAX_EXEC_SET_VARS(fax, chan, "HANGUP", "remote channel hungup");
				c = NULL;
				chancount = 0;
				timeout -= (1000 - ms);
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
					
					ast_verb(3, "Channel '%s' switched to T.38 FAX session '%d'.\n", chan->name, fax->id);
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
				timeout = RES_FAX_TIMEOUT;
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
			timeout = RES_FAX_TIMEOUT;
		} else {
			if (ms && (ofd < 0)) {
				if ((errno == 0) || (errno == EINTR)) {
					timeout -= (1000 - ms);
					if (timeout <= 0)
						GENERIC_FAX_EXEC_ERROR(fax, chan, "TIMEOUT", "fax session timed-out");
					continue;
				} else {
					ast_log(LOG_WARNING, "something bad happened while channel '%s' was polling.\n", chan->name);
					GENERIC_FAX_EXEC_ERROR(fax, chan, "UNKNOWN", "error polling data");
					res = ms;
					break;
				}
			} else {
				/* nothing happened */
				if (timeout > 0) {
					timeout -= 1000;
					if (timeout <= 0)
						GENERIC_FAX_EXEC_ERROR(fax, chan, "TIMEOUT", "fax session timed-out");
					continue;
				} else {
					ast_log(LOG_WARNING, "channel '%s' timed-out during the FAX transmission.\n", chan->name);
					GENERIC_FAX_EXEC_ERROR(fax, chan, "TIMEOUT", "fax session timed-out");
					break;
				}
			}
		}
	}
	ast_debug(3, "channel '%s' - event loop stopped { timeout: %d, ms: %d, res: %d }\n", chan->name, timeout, ms, res);

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
	int ms;
	struct ast_frame *frame = NULL;
	struct ast_control_t38_parameters t38_parameters;

	t38_parameters_ast_to_fax(&details->our_t38_parameters, &our_t38_parameters);

	/* don't send any audio if we've already received a T.38 reinvite */
	if (ast_channel_get_t38_state(chan) != T38_STATE_NEGOTIATING) {
		/* generate 3 seconds of CED */
		if (ast_playtones_start(chan, 1024, "!2100/3000", 1)) {
			ast_log(LOG_ERROR, "error generating CED tone on %s\n", chan->name);
			return -1;
		}

		ms = 3000;
		while (ms > 0) {
			ms = ast_waitfor(chan, ms);
			if (ms < 0) {
				ast_log(LOG_ERROR, "error while generating CED tone on %s\n", chan->name);
				ast_playtones_stop(chan);
				return -1;
			}

			if (ms == 0) { /* all done, nothing happened */
				break;
			}

			if (!(frame = ast_read(chan))) {
				ast_log(LOG_ERROR, "error reading frame while generating CED tone on %s\n", chan->name);
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
					ast_debug(1, "Negotiated T.38 for receive on %s\n", chan->name);
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
	ast_debug(1, "Negotiating T.38 for receive on %s\n", chan->name);

	/* wait up to five seconds for negotiation to complete */
	ms = 5000;

	/* set parameters based on the session's parameters */
	t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
	t38_parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
	if ((ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) != 0)) {
		return -1;
	}

	while (ms > 0) {
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", chan->name);
			return -1;
		}

		if (ms == 0) { /* all done, nothing happened */
			ast_log(LOG_WARNING, "channel '%s' timed-out during the T.38 negotiation.\n", chan->name);
			details->caps &= ~AST_FAX_TECH_T38;
			break;
		}

		if (!(frame = ast_read(chan))) {
			ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", chan->name);
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
				ast_debug(1, "Negotiated T.38 for receive on %s\n", chan->name);
				t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
				details->caps &= ~AST_FAX_TECH_AUDIO;
				report_fax_status(chan, details, "T.38 Negotiated");
				ms = 0;
				break;
			case AST_T38_REFUSED:
				ast_log(LOG_WARNING, "channel '%s' refused to negotiate T.38\n", chan->name);
				details->caps &= ~AST_FAX_TECH_T38;
				ms = 0;
				break;
			default:
				ast_log(LOG_ERROR, "channel '%s' failed to negotiate T.38\n", chan->name);
				details->caps &= ~AST_FAX_TECH_T38;
				ms = 0;
				break;
			}
		}
		ast_frfree(frame);
	}

	/* if T.38 was negotiated, we are done initializing */
	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		return 0;
	}

	/* if we made it here, then T.38 failed, check the 'f' flag */
	if (details->option.allow_audio != AST_FAX_OPTFLAG_TRUE) {
		ast_log(LOG_WARNING, "Audio FAX not allowed on channel '%s' and T.38 negotiation failed; aborting.\n", chan->name);
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

	/* initialize output channel variables */
	pbx_builtin_setvar_helper(chan, "FAXSTATUS", "FAILED");
	pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", NULL);
	pbx_builtin_setvar_helper(chan, "LOCALSTATIONID", NULL);
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

	if (details->maxrate < details->minrate) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "maxrate is less than minrate");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "maxrate %d is less than minrate %d\n", details->maxrate, details->minrate);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->minrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'minrate' setting %d\n", modems, details->minrate);
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "incompatible 'modems' and 'minrate' settings");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->maxrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'maxrate' setting %d\n", modems, details->maxrate);
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

	ast_verb(3, "Channel '%s' receiving FAX '%s'\n", chan->name, args.filename);

	details->caps = AST_FAX_TECH_RECEIVE;

	/* check for debug */
	if (ast_test_flag(&opts, OPT_DEBUG) || global_fax_debug) {
		details->option.debug = AST_FAX_OPTFLAG_TRUE;
	}

	/* check for request for status events */
	if (ast_test_flag(&opts, OPT_STATUS)) {
		details->option.statusevents = AST_FAX_OPTFLAG_TRUE;
	}

	if ((ast_channel_get_t38_state(chan) == T38_STATE_UNAVAILABLE) ||
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
	if (chan->_state != AST_STATE_UP) {
		if (ast_answer(chan)) {
			ast_string_field_set(details, resultstr, "error answering channel");
			set_channel_variables(chan, details);
			ast_log(LOG_WARNING, "Channel '%s' failed answer attempt.\n", chan->name);
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
			ast_log(LOG_ERROR, "error initializing channel '%s' in T.38 mode\n", chan->name);
			return -1;
		}
	} else {
		details->option.send_ced = 1;
	}

	if ((channel_alive = generic_fax_exec(chan, details, s, token)) < 0) {
		ast_atomic_fetchadd_int(&faxregistry.fax_failures, 1);
	}

	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		if (disable_t38(chan)) {
			ast_debug(1, "error disabling T.38 mode on %s\n", chan->name);
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
		      chan->name,
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
	int ms;
	struct ast_frame *frame = NULL;
	struct ast_control_t38_parameters t38_parameters;

	t38_parameters_ast_to_fax(&details->our_t38_parameters, &our_t38_parameters);

	/* send CNG tone while listening for the receiver to initiate a switch
	 * to T.38 mode; if they do, stop sending the CNG tone and proceed with
	 * the switch.
	 *
	 * 10500 is enough time for 3 CNG tones
	 */
	ms = 10500;

	/* don't send any audio if we've already received a T.38 reinvite */
	if (ast_channel_get_t38_state(chan) != T38_STATE_NEGOTIATING) {
		if (ast_playtones_start(chan, 1024, "!1100/500,!0/3000,!1100/500,!0/3000,!1100/500,!0/3000", 1)) {
			ast_log(LOG_ERROR, "error generating CNG tone on %s\n", chan->name);
			return -1;
		}
	}

	while (ms > 0) {
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			ast_log(LOG_ERROR, "error while generating CNG tone on %s\n", chan->name);
			ast_playtones_stop(chan);
			return -1;
		}

		if (ms == 0) { /* all done, nothing happened */
			break;
		}

		if (!(frame = ast_read(chan))) {
			ast_log(LOG_ERROR, "error reading frame while generating CNG tone on %s\n", chan->name);
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
				ast_debug(1, "Negotiated T.38 for send on %s\n", chan->name);
				t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
				details->caps &= ~AST_FAX_TECH_AUDIO;
				report_fax_status(chan, details, "T.38 Negotiated");
				ms = 0;
				break;
			default:
				break;
			}
		}
		ast_frfree(frame);
	}

	ast_playtones_stop(chan);

	if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
		return 0;
	}

	/* T.38 negotiation did not happen, initiate a switch if requested */
	if (details->option.request_t38 == AST_FAX_OPTFLAG_TRUE) {
		ast_debug(1, "Negotiating T.38 for send on %s\n", chan->name);

		/* wait up to five seconds for negotiation to complete */
		ms = 5000;

		/* set parameters based on the session's parameters */
		t38_parameters_fax_to_ast(&t38_parameters, &details->our_t38_parameters);
		t38_parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
		if ((ast_indicate_data(chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) != 0)) {
			return -1;
		}

		while (ms > 0) {
			ms = ast_waitfor(chan, ms);
			if (ms < 0) {
				ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", chan->name);
				return -1;
			}

			if (ms == 0) { /* all done, nothing happened */
				ast_log(LOG_WARNING, "channel '%s' timed-out during the T.38 negotiation.\n", chan->name);
				details->caps &= ~AST_FAX_TECH_T38;
				break;
			}

			if (!(frame = ast_read(chan))) {
				ast_log(LOG_WARNING, "error on '%s' while waiting for T.38 negotiation.\n", chan->name);
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
					ast_debug(1, "Negotiated T.38 for receive on %s\n", chan->name);
					t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
					details->caps &= ~AST_FAX_TECH_AUDIO;
					report_fax_status(chan, details, "T.38 Negotiated");
					ms = 0;
					break;
				case AST_T38_REFUSED:
					ast_log(LOG_WARNING, "channel '%s' refused to negotiate T.38\n", chan->name);
					details->caps &= ~AST_FAX_TECH_T38;
					ms = 0;
					break;
				default:
					ast_log(LOG_ERROR, "channel '%s' failed to negotiate T.38\n", chan->name);
					details->caps &= ~AST_FAX_TECH_T38;
					ms = 0;
					break;
				}
			}
			ast_frfree(frame);
		}

		/* if T.38 was negotiated, we are done initializing */
		if (ast_channel_get_t38_state(chan) == T38_STATE_NEGOTIATED) {
			return 0;
		}

		/* send one more CNG tone to get audio going again for some
		 * carriers if we are going to fall back to audio mode */
		if (details->option.allow_audio == AST_FAX_OPTFLAG_TRUE) {
			if (ast_playtones_start(chan, 1024, "!1100/500,!0/3000", 1)) {
				ast_log(LOG_ERROR, "error generating second CNG tone on %s\n", chan->name);
				return -1;
			}

			ms = 3500;
			while (ms > 0) {
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_log(LOG_ERROR, "error while generating second CNG tone on %s\n", chan->name);
					ast_playtones_stop(chan);
					return -1;
				}

				if (ms == 0) { /* all done, nothing happened */
					break;
				}

				if (!(frame = ast_read(chan))) {
					ast_log(LOG_ERROR, "error reading frame while generating second CNG tone on %s\n", chan->name);
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
						ast_debug(1, "Negotiated T.38 for send on %s\n", chan->name);
						t38_parameters_ast_to_fax(&details->their_t38_parameters, parameters);
						details->caps &= ~AST_FAX_TECH_AUDIO;
						report_fax_status(chan, details, "T.38 Negotiated");
						ms = 0;
						break;
					default:
						break;
					}
				}
				ast_frfree(frame);
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
		ast_log(LOG_WARNING, "Audio FAX not allowed on channel '%s' and T.38 negotiation failed; aborting.\n", chan->name);
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

	/* initialize output channel variables */
	pbx_builtin_setvar_helper(chan, "FAXSTATUS", "FAILED");
	pbx_builtin_setvar_helper(chan, "REMOTESTATIONID", NULL);
	pbx_builtin_setvar_helper(chan, "LOCALSTATIONID", NULL);
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

	if (details->maxrate < details->minrate) {
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "maxrate is less than minrate");
		set_channel_variables(chan, details);
		ast_log(LOG_ERROR, "maxrate %d is less than minrate %d\n", details->maxrate, details->minrate);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->minrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'minrate' setting %d\n", modems, details->minrate);
		ast_string_field_set(details, error, "INVALID_ARGUMENTS");
		ast_string_field_set(details, resultstr, "incompatible 'modems' and 'minrate' settings");
		set_channel_variables(chan, details);
		ao2_ref(details, -1);
		return -1;
	}

	if (check_modem_rate(details->modems, details->maxrate)) {
		ast_fax_modem_to_str(details->modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'maxrate' setting %d\n", modems, details->maxrate);
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

	if (file_count > 1) {
		details->caps |= AST_FAX_TECH_MULTI_DOC;
	}

	ast_verb(3, "Channel '%s' sending FAX:\n", chan->name);
	AST_LIST_TRAVERSE(&details->documents, doc, next) {
		ast_verb(3, "   %s\n", doc->filename);
	}

	details->caps = AST_FAX_TECH_SEND;

	/* check for debug */
	if (ast_test_flag(&opts, OPT_DEBUG) || global_fax_debug) {
		details->option.debug = AST_FAX_OPTFLAG_TRUE;
	}

	/* check for request for status events */
	if (ast_test_flag(&opts, OPT_STATUS)) {
		details->option.statusevents = AST_FAX_OPTFLAG_TRUE;
	}

	if ((ast_channel_get_t38_state(chan) == T38_STATE_UNAVAILABLE) ||
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
	if (chan->_state != AST_STATE_UP) {
		if (ast_answer(chan)) {
			ast_string_field_set(details, resultstr, "error answering channel");
			set_channel_variables(chan, details);
			ast_log(LOG_WARNING, "Channel '%s' failed answer attempt.\n", chan->name);
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
			ast_log(LOG_ERROR, "error initializing channel '%s' in T.38 mode\n", chan->name);
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
			ast_debug(1, "error disabling T.38 mode on %s\n", chan->name);
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
		      chan->name,
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
		snprintf(tbuf, sizeof(tbuf), "%d", s->id);
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
	ast_cli(a->fd, "%d registered modules\n\n", num_modules);

	return CLI_SUCCESS;
}

/*! \brief display global defaults and settings */
static char *cli_fax_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct fax_module *fax;
	char modems[128] = "";

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

	ast_cli(a->fd, "FAX For Asterisk Settings:\n");
	ast_cli(a->fd, "\tECM: %s\n", general_options.ecm ? "Enabled" : "Disabled");
	ast_cli(a->fd, "\tStatus Events: %s\n",  general_options.statusevents ? "On" : "Off");
	ast_cli(a->fd, "\tMinimum Bit Rate: %d\n", general_options.minrate);
	ast_cli(a->fd, "\tMaximum Bit Rate: %d\n", general_options.maxrate);
	ast_fax_modem_to_str(general_options.modems, modems, sizeof(modems));
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

	if (sscanf(a->argv[3], "%d", &tmp.id) != 1) {
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

		if (!(filenames = generate_filenames_string(s->details, "", ", "))) {
			ast_log(LOG_ERROR, "error printing filenames for 'fax show sessions' command");
			ao2_unlock(s);
			ao2_ref(s, -1);
			ao2_iterator_destroy(&i);
			return CLI_FAILURE;
		}

		ast_cli(a->fd, "%-20.20s %-10.10s %-10d %-5.5s %-10.10s %-15.15s %-30s\n",
			s->channame, s->tech->type, s->id,
			(s->details->caps & AST_FAX_TECH_AUDIO) ? "G.711" : "T.38",
			(s->details->caps & AST_FAX_TECH_SEND) ? "send" : "receive",
			ast_fax_state_to_str(s->state), filenames);

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

/*! \brief configure res_fax */
static int set_config(const char *config_file)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { 0 };
	char modems[128] = "";

	/* set defaults */	
	general_options.minrate = RES_FAX_MINRATE;
	general_options.maxrate = RES_FAX_MAXRATE;	
	general_options.statusevents = RES_FAX_STATUSEVENTS;
	general_options.modems = RES_FAX_MODEM;
	general_options.ecm = AST_FAX_OPTFLAG_TRUE;

	/* read configuration */
	if (!(cfg = ast_config_load2(config_file, "res_fax", config_flags))) {
		ast_log(LOG_NOTICE, "Configuration file '%s' not found, using default options.\n", config_file);
		return 0;
	}
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		cfg = ast_config_load2(config_file, "res_fax", config_flags);
	}

	/* create configuration */
	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		int rate;

		if (!strcasecmp(v->name, "minrate")) {
			ast_debug(3, "reading minrate '%s' from configuration file\n", v->value);
			if ((rate = fax_rate_str_to_int(v->value)) == 0) {
				ast_config_destroy(cfg);
				return -1;
			}
			general_options.minrate = rate;
		} else if (!strcasecmp(v->name, "maxrate")) {
			ast_debug(3, "reading maxrate '%s' from configuration file\n", v->value);
			if ((rate = fax_rate_str_to_int(v->value)) == 0) {
				ast_config_destroy(cfg);
				return -1;
			}
			general_options.maxrate = rate;
		} else if (!strcasecmp(v->name, "statusevents")) {
			ast_debug(3, "reading statusevents '%s' from configuration file\n", v->value);
			general_options.statusevents = ast_true(v->value);
		} else if (!strcasecmp(v->name, "ecm")) {
			ast_debug(3, "reading ecm '%s' from configuration file\n", v->value);
			general_options.ecm = ast_true(v->value);
		} else if ((!strcasecmp(v->name, "modem")) || (!strcasecmp(v->name, "modems"))) {
			general_options.modems = 0;
			update_modem_bits(&general_options.modems, v->value);
		}
	}

	ast_config_destroy(cfg);

	if (general_options.maxrate < general_options.minrate) {
		ast_log(LOG_ERROR, "maxrate %d is less than minrate %d\n", general_options.maxrate, general_options.minrate);
		return -1;
	}

	if (check_modem_rate(general_options.modems, general_options.minrate)) {
		ast_fax_modem_to_str(general_options.modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'minrate' setting %d\n", modems, general_options.minrate);
		return -1;
	}

	if (check_modem_rate(general_options.modems, general_options.maxrate)) {
		ast_fax_modem_to_str(general_options.modems, modems, sizeof(modems));
		ast_log(LOG_ERROR, "'modems' setting '%s' is incompatible with 'maxrate' setting %d\n", modems, general_options.maxrate);
		return -1;
	}

	return 0;
}

/*! \brief FAXOPT read function returns the contents of a FAX option */
static int acf_faxopt_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_fax_session_details *details = find_details(chan);
	int res = 0;
	char *filenames;

	if (!details) {
		ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s) because it has never been written.\n", chan->name, data);
		return -1;
	}
	if (!strcasecmp(data, "ecm")) {
		ast_copy_string(buf, details->option.ecm ? "yes" : "no", len);
	} else if (!strcasecmp(data, "error")) {
		ast_copy_string(buf, details->error, len);
	} else if (!strcasecmp(data, "filename")) {
		if (AST_LIST_EMPTY(&details->documents)) {
			ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s) because it has never been written.\n", chan->name, data);
			res = -1;
		} else {
			ast_copy_string(buf, AST_LIST_FIRST(&details->documents)->filename, len);
		}
	} else if (!strcasecmp(data, "filenames")) {
		if (AST_LIST_EMPTY(&details->documents)) {
			ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s) because it has never been written.\n", chan->name, data);
			res = -1;
		} else if ((filenames = generate_filenames_string(details, "", ","))) {
			ast_copy_string(buf, filenames, len);
			ast_free(filenames);
		} else {
			ast_log(LOG_ERROR, "channel '%s' can't read FAXOPT(%s), there was an error generating the filenames list.\n", chan->name, data);
			res = -1;
		}
	} else if (!strcasecmp(data, "headerinfo")) {
		ast_copy_string(buf, details->headerinfo, len);
	} else if (!strcasecmp(data, "localstationid")) {
		ast_copy_string(buf, details->localstationid, len);
	} else if (!strcasecmp(data, "maxrate")) {
		snprintf(buf, len, "%d", details->maxrate);
	} else if (!strcasecmp(data, "minrate")) {
		snprintf(buf, len, "%d", details->minrate);
	} else if (!strcasecmp(data, "pages")) {
		snprintf(buf, len, "%d", details->pages_transferred);
	} else if (!strcasecmp(data, "rate")) {
		ast_copy_string(buf, details->transfer_rate, len);
	} else if (!strcasecmp(data, "remotestationid")) {
		ast_copy_string(buf, details->remotestationid, len);
	} else if (!strcasecmp(data, "resolution")) {
		ast_copy_string(buf, details->resolution, len);
	} else if (!strcasecmp(data, "sessionid")) {
		snprintf(buf, len, "%d", details->id);
	} else if (!strcasecmp(data, "status")) {
		ast_copy_string(buf, details->result, len);
	} else if (!strcasecmp(data, "statusstr")) {
		ast_copy_string(buf, details->resultstr, len);
	} else if ((!strcasecmp(data, "modem")) || (!strcasecmp(data, "modems"))) {
		ast_fax_modem_to_str(details->modems, buf, len);
	} else {
		ast_log(LOG_WARNING, "channel '%s' can't read FAXOPT(%s) because it is unhandled!\n", chan->name, data);
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
		ast_log(LOG_WARNING, "channel '%s' can't set FAXOPT(%s) to '%s' because it failed to create a datastore.\n", chan->name, data, value);
		return -1;
	}
	ast_debug(3, "channel '%s' setting FAXOPT(%s) to '%s'\n", chan->name, data, value);

	if (!strcasecmp(data, "ecm")) {
		const char *val = ast_skip_blanks(value);
		if (ast_true(val)) {
			details->option.ecm = AST_FAX_OPTFLAG_TRUE;
		} else if (ast_false(val)) {
			details->option.ecm = AST_FAX_OPTFLAG_FALSE;
		} else {
			ast_log(LOG_WARNING, "Unsupported value '%s' passed to FAXOPT(ecm).\n", value);
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
		ast_log(LOG_WARNING, "channel '%s' set FAXOPT(%s) to '%s' is unhandled!\n", chan->name, data, value);
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
	
	if (set_config(config) < 0) {
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


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Generic FAX Applications",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
