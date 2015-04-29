/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2010, Digium, Inc.
 *
 * Matthew Nicholson <mnicholson@digium.com>
 *
 * Initial T.38-gateway code
 * 2008, Daniel Ferenci <daniel.ferenci@nethemba.com>
 * Created by Nethemba s.r.o. http://www.nethemba.com
 * Sponsored by IPEX a.s. http://www.ipex.cz
 *
 * T.38-gateway integration into asterisk app_fax and rework
 * 2008, Gregory Hinton Nietsky <gregory@dnstelecom.co.za>
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

/*! \file
 *
 * \brief Spandsp T.38 and G.711 FAX Resource
 *
 * \author Matthew Nicholson <mnicholson@digium.com>
 * \author Gregory H. Nietsky <gregory@distrotech.co.za>
 *
 * This module registers the Spandsp FAX technology with the res_fax module.
 */

/*** MODULEINFO
	<depend>spandsp</depend>
	<use type="module">res_fax</use>
	<support_level>extended</support_level>
***/

/* Needed for spandsp headers */
#define ASTMM_LIBC ASTMM_IGNORE
#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/timing.h"
#include "asterisk/astobj2.h"
#include "asterisk/res_fax.h"
#include "asterisk/channel.h"
#include "asterisk/format_cache.h"

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>
#include <spandsp/version.h>

#define SPANDSP_FAX_SAMPLES 160
#define SPANDSP_FAX_TIMER_RATE 8000 / SPANDSP_FAX_SAMPLES	/* 50 ticks per second, 20ms, 160 samples per second */
#define SPANDSP_ENGAGE_UDPTL_NAT_RETRY 3

static void *spandsp_fax_new(struct ast_fax_session *s, struct ast_fax_tech_token *token);
static void spandsp_fax_destroy(struct ast_fax_session *s);
static struct ast_frame *spandsp_fax_read(struct ast_fax_session *s);
static int spandsp_fax_write(struct ast_fax_session *s, const struct ast_frame *f);
static int spandsp_fax_start(struct ast_fax_session *s);
static int spandsp_fax_cancel(struct ast_fax_session *s);
static int spandsp_fax_switch_to_t38(struct ast_fax_session *s);
static int spandsp_fax_gateway_start(struct ast_fax_session *s);
static int spandsp_fax_gateway_process(struct ast_fax_session *s, const struct ast_frame *f);
static void spandsp_fax_gateway_cleanup(struct ast_fax_session *s);
static int spandsp_v21_detect(struct ast_fax_session *s, const struct ast_frame *f);
static void spandsp_v21_cleanup(struct ast_fax_session *s);
static void spandsp_v21_tone(void *data, int code, int level, int delay);

static char *spandsp_fax_cli_show_capabilities(int fd);
static char *spandsp_fax_cli_show_session(struct ast_fax_session *s, int fd);
static void spandsp_manager_fax_session(struct mansession *s,
	const char *id_text, struct ast_fax_session *session);
static char *spandsp_fax_cli_show_stats(int fd);
static char *spandsp_fax_cli_show_settings(int fd);

static struct ast_fax_tech spandsp_fax_tech = {
	.type = "Spandsp",
	.description = "Spandsp FAX Driver",
#if SPANDSP_RELEASE_DATE >= 20090220
	/* spandsp 0.0.6 */
	.version = SPANDSP_RELEASE_DATETIME_STRING,
#else
	/* spandsp 0.0.5
	 * TODO: maybe we should determine the version better way
	 */
	.version = "pre-20090220",
#endif
	.caps = AST_FAX_TECH_AUDIO | AST_FAX_TECH_T38 | AST_FAX_TECH_SEND
		| AST_FAX_TECH_RECEIVE | AST_FAX_TECH_GATEWAY
		| AST_FAX_TECH_V21_DETECT,
	.new_session = spandsp_fax_new,
	.destroy_session = spandsp_fax_destroy,
	.read = spandsp_fax_read,
	.write = spandsp_fax_write,
	.start_session = spandsp_fax_start,
	.cancel_session = spandsp_fax_cancel,
	.switch_to_t38 = spandsp_fax_switch_to_t38,
	.cli_show_capabilities = spandsp_fax_cli_show_capabilities,
	.cli_show_session = spandsp_fax_cli_show_session,
	.manager_fax_session = spandsp_manager_fax_session,
	.cli_show_stats = spandsp_fax_cli_show_stats,
	.cli_show_settings = spandsp_fax_cli_show_settings,
};

struct spandsp_fax_stats {
	int success;
	int nofax;
	int neg_failed;
	int failed_to_train;
	int rx_protocol_error;
	int tx_protocol_error;
	int protocol_error;
	int retries_exceeded;
	int file_error;
	int mem_error;
	int call_dropped;
	int unknown_error;
	int switched;
};

static struct {
	ast_mutex_t lock;
	struct spandsp_fax_stats g711;
	struct spandsp_fax_stats t38;
} spandsp_global_stats;

struct spandsp_pvt {
	unsigned int ist38:1;
	unsigned int isdone:1;
	enum ast_t38_state ast_t38_state;
	fax_state_t fax_state;
	t38_terminal_state_t t38_state;
	t30_state_t *t30_state;
	t38_core_state_t *t38_core_state;

	struct spandsp_fax_stats *stats;

	struct spandsp_fax_gw_stats *t38stats;
	t38_gateway_state_t t38_gw_state;

	struct ast_timer *timer;
	AST_LIST_HEAD(frame_queue, ast_frame) read_frames;

	int v21_detected;
	modem_connect_tones_rx_state_t *tone_state;
};

static int spandsp_v21_new(struct spandsp_pvt *p);
static void session_destroy(struct spandsp_pvt *p);
static int t38_tx_packet_handler(t38_core_state_t *t38_core_state, void *data, const uint8_t *buf, int len, int count);
static void t30_phase_e_handler(t30_state_t *t30_state, void *data, int completion_code);
static void spandsp_log(int level, const char *msg);
static int update_stats(struct spandsp_pvt *p, int completion_code);
static int spandsp_modems(struct ast_fax_session_details *details);

static void set_logging(logging_state_t *state, struct ast_fax_session_details *details);
static void set_local_info(t30_state_t *t30_state, struct ast_fax_session_details *details);
static void set_file(t30_state_t *t30_state, struct ast_fax_session_details *details);
static void set_ecm(t30_state_t *t30_state, struct ast_fax_session_details *details);

static void session_destroy(struct spandsp_pvt *p)
{
	struct ast_frame *f;
	t30_state_t *t30_to_terminate;

	if (p->t30_state) {
		t30_to_terminate = p->t30_state;
	} else if (p->ist38) {
#if SPANDSP_RELEASE_DATE >= 20080725
		t30_to_terminate = &p->t38_state.t30;
#else
		t30_to_terminate = &p->t38_state.t30_state;
#endif
	} else {
#if SPANDSP_RELEASE_DATE >= 20080725
		t30_to_terminate = &p->fax_state.t30;
#else
		t30_to_terminate = &p->fax_state.t30_state;
#endif
	}

	t30_terminate(t30_to_terminate);
	p->isdone = 1;

	ast_timer_close(p->timer);
	p->timer = NULL;
	fax_release(&p->fax_state);
	t38_terminal_release(&p->t38_state);

	while ((f = AST_LIST_REMOVE_HEAD(&p->read_frames, frame_list))) {
		ast_frfree(f);
	}
}

/*! \brief
 *
 */
static int t38_tx_packet_handler(t38_core_state_t *t38_core_state, void *data, const uint8_t *buf, int len, int count)
{
	int res = -1;
	struct ast_fax_session *s = data;
	struct spandsp_pvt *p = s->tech_pvt;
	struct ast_frame fax_frame = {
		.frametype = AST_FRAME_MODEM,
		.subclass.integer = AST_MODEM_T38,
		.src = "res_fax_spandsp_t38",
	};

	struct ast_frame *f = &fax_frame;


	/* TODO: Asterisk does not provide means of resending the same packet multiple
	  times so count is ignored at the moment */

	AST_FRAME_SET_BUFFER(f, buf, 0, len);

	if (!(f = ast_frisolate(f))) {
		return res;
	}

	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		ast_set_flag(f, AST_FAX_FRFLAG_GATEWAY);
		if (p->ast_t38_state == T38_STATE_NEGOTIATED) {
			res = ast_write(s->chan, f);
		} else {
			res = ast_queue_frame(s->chan, f);
		}
		ast_frfree(f);
	} else {
		/* no need to lock, this all runs in the same thread */
		AST_LIST_INSERT_TAIL(&p->read_frames, f, frame_list);
		res = 0;
	}

	return res;
}

static int update_stats(struct spandsp_pvt *p, int completion_code)
{
	switch (completion_code) {
	case T30_ERR_OK:
		ast_atomic_fetchadd_int(&p->stats->success, 1);
		break;

	/* Link problems */
	case T30_ERR_CEDTONE:            /*! The CED tone exceeded 5s */
	case T30_ERR_T0_EXPIRED:         /*! Timed out waiting for initial communication */
	case T30_ERR_T1_EXPIRED:         /*! Timed out waiting for the first message */
	case T30_ERR_T3_EXPIRED:         /*! Timed out waiting for procedural interrupt */
	case T30_ERR_HDLC_CARRIER:       /*! The HDLC carrier did not stop in a timely manner */
	case T30_ERR_CANNOT_TRAIN:       /*! Failed to train with any of the compatible modems */
		ast_atomic_fetchadd_int(&p->stats->failed_to_train, 1);
		break;

	case T30_ERR_OPER_INT_FAIL:      /*! Operator intervention failed */
	case T30_ERR_INCOMPATIBLE:       /*! Far end is not compatible */
	case T30_ERR_RX_INCAPABLE:       /*! Far end is not able to receive */
	case T30_ERR_TX_INCAPABLE:       /*! Far end is not able to transmit */
	case T30_ERR_NORESSUPPORT:       /*! Far end cannot receive at the resolution of the image */
	case T30_ERR_NOSIZESUPPORT:      /*! Far end cannot receive at the size of image */
		ast_atomic_fetchadd_int(&p->stats->neg_failed, 1);
		break;

	case T30_ERR_UNEXPECTED:         /*! Unexpected message received */
		ast_atomic_fetchadd_int(&p->stats->protocol_error, 1);
		break;

	/* Phase E status values returned to a transmitter */
	case T30_ERR_TX_BADDCS:          /*! Received bad response to DCS or training */
	case T30_ERR_TX_BADPG:           /*! Received a DCN from remote after sending a page */
	case T30_ERR_TX_ECMPHD:          /*! Invalid ECM response received from receiver */
	case T30_ERR_TX_GOTDCN:          /*! Received a DCN while waiting for a DIS */
	case T30_ERR_TX_INVALRSP:        /*! Invalid response after sending a page */
	case T30_ERR_TX_NODIS:           /*! Received other than DIS while waiting for DIS */
	case T30_ERR_TX_PHBDEAD:         /*! Received no response to DCS, training or TCF */
	case T30_ERR_TX_PHDDEAD:         /*! No response after sending a page */
	case T30_ERR_TX_T5EXP:           /*! Timed out waiting for receiver ready (ECM mode) */
		ast_atomic_fetchadd_int(&p->stats->tx_protocol_error, 1);
		break;

	/* Phase E status values returned to a receiver */
	case T30_ERR_RX_ECMPHD:          /*! Invalid ECM response received from transmitter */
	case T30_ERR_RX_GOTDCS:          /*! DCS received while waiting for DTC */
	case T30_ERR_RX_INVALCMD:        /*! Unexpected command after page received */
	case T30_ERR_RX_NOCARRIER:       /*! Carrier lost during fax receive */
	case T30_ERR_RX_NOEOL:           /*! Timed out while waiting for EOL (end Of line) */
		ast_atomic_fetchadd_int(&p->stats->rx_protocol_error, 1);
		break;
	case T30_ERR_RX_NOFAX:           /*! Timed out while waiting for first line */
		ast_atomic_fetchadd_int(&p->stats->nofax, 1);
		break;
	case T30_ERR_RX_T2EXPDCN:        /*! Timer T2 expired while waiting for DCN */
	case T30_ERR_RX_T2EXPD:          /*! Timer T2 expired while waiting for phase D */
	case T30_ERR_RX_T2EXPFAX:        /*! Timer T2 expired while waiting for fax page */
	case T30_ERR_RX_T2EXPMPS:        /*! Timer T2 expired while waiting for next fax page */
	case T30_ERR_RX_T2EXPRR:         /*! Timer T2 expired while waiting for RR command */
	case T30_ERR_RX_T2EXP:           /*! Timer T2 expired while waiting for NSS, DCS or MCF */
	case T30_ERR_RX_DCNWHY:          /*! Unexpected DCN while waiting for DCS or DIS */
	case T30_ERR_RX_DCNDATA:         /*! Unexpected DCN while waiting for image data */
	case T30_ERR_RX_DCNFAX:          /*! Unexpected DCN while waiting for EOM, EOP or MPS */
	case T30_ERR_RX_DCNPHD:          /*! Unexpected DCN after EOM or MPS sequence */
	case T30_ERR_RX_DCNRRD:          /*! Unexpected DCN after RR/RNR sequence */
	case T30_ERR_RX_DCNNORTN:        /*! Unexpected DCN after requested retransmission */
		ast_atomic_fetchadd_int(&p->stats->rx_protocol_error, 1);
		break;

	/* TIFF file problems */
	case T30_ERR_FILEERROR:          /*! TIFF/F file cannot be opened */
	case T30_ERR_NOPAGE:             /*! TIFF/F page not found */
	case T30_ERR_BADTIFF:            /*! TIFF/F format is not compatible */
	case T30_ERR_BADPAGE:            /*! TIFF/F page number tag missing */
	case T30_ERR_BADTAG:             /*! Incorrect values for TIFF/F tags */
	case T30_ERR_BADTIFFHDR:         /*! Bad TIFF/F header - incorrect values in fields */
		ast_atomic_fetchadd_int(&p->stats->file_error, 1);
		break;
	case T30_ERR_NOMEM:              /*! Cannot allocate memory for more pages */
		ast_atomic_fetchadd_int(&p->stats->mem_error, 1);
		break;

	/* General problems */
	case T30_ERR_RETRYDCN:           /*! Disconnected after permitted retries */
		ast_atomic_fetchadd_int(&p->stats->retries_exceeded, 1);
		break;
	case T30_ERR_CALLDROPPED:        /*! The call dropped prematurely */
		ast_atomic_fetchadd_int(&p->stats->call_dropped, 1);
		break;

	/* Feature negotiation issues */
	case T30_ERR_NOPOLL:             /*! Poll not accepted */
	case T30_ERR_IDENT_UNACCEPTABLE: /*! Far end's ident is not acceptable */
	case T30_ERR_SUB_UNACCEPTABLE:   /*! Far end's sub-address is not acceptable */
	case T30_ERR_SEP_UNACCEPTABLE:   /*! Far end's selective polling address is not acceptable */
	case T30_ERR_PSA_UNACCEPTABLE:   /*! Far end's polled sub-address is not acceptable */
	case T30_ERR_SID_UNACCEPTABLE:   /*! Far end's sender identification is not acceptable */
	case T30_ERR_PWD_UNACCEPTABLE:   /*! Far end's password is not acceptable */
	case T30_ERR_TSA_UNACCEPTABLE:   /*! Far end's transmitting subscriber internet address is not acceptable */
	case T30_ERR_IRA_UNACCEPTABLE:   /*! Far end's internet routing address is not acceptable */
	case T30_ERR_CIA_UNACCEPTABLE:   /*! Far end's calling subscriber internet address is not acceptable */
	case T30_ERR_ISP_UNACCEPTABLE:   /*! Far end's internet selective polling address is not acceptable */
	case T30_ERR_CSA_UNACCEPTABLE:   /*! Far end's called subscriber internet address is not acceptable */
		ast_atomic_fetchadd_int(&p->stats->neg_failed, 1);
		break;
	default:
		ast_atomic_fetchadd_int(&p->stats->unknown_error, 1);
		ast_log(LOG_WARNING, "unknown FAX session result '%d' (%s)\n", completion_code, t30_completion_code_to_str(completion_code));
		return -1;
	}
	return 0;
}

/*! \brief Phase E handler callback.
 * \param t30_state the span t30 state
 * \param data this will be the ast_fax_session
 * \param completion_code the result of the fax session
 *
 * This function pulls stats from the spandsp stack and stores them for res_fax
 * to use later.
 */
static void t30_phase_e_handler(t30_state_t *t30_state, void *data, int completion_code)
{
	struct ast_fax_session *s = data;
	struct spandsp_pvt *p = s->tech_pvt;
	char headerinfo[T30_MAX_PAGE_HEADER_INFO + 1];
	const char *c;
	t30_stats_t stats;

	ast_debug(5, "FAX session '%u' entering phase E\n", s->id);

	p->isdone = 1;

	update_stats(p, completion_code);

	t30_get_transfer_statistics(t30_state, &stats);

	if (completion_code == T30_ERR_OK) {
		ast_string_field_set(s->details, result, "SUCCESS");
	} else {
		ast_string_field_set(s->details, result, "FAILED");
		ast_string_field_set(s->details, error, t30_completion_code_to_str(completion_code));
	}

	ast_string_field_set(s->details, resultstr, t30_completion_code_to_str(completion_code));

	ast_debug(5, "FAX session '%u' completed with result: %s (%s)\n", s->id, s->details->result, s->details->resultstr);

	if ((c = t30_get_tx_ident(t30_state))) {
		ast_string_field_set(s->details, localstationid, c);
	}

	if ((c = t30_get_rx_ident(t30_state))) {
		ast_string_field_set(s->details, remotestationid, c);
	}

#if SPANDSP_RELEASE_DATE >= 20090220
	s->details->pages_transferred = (s->details->caps & AST_FAX_TECH_RECEIVE) ? stats.pages_rx : stats.pages_tx;
#else
	s->details->pages_transferred = stats.pages_transferred;
#endif

	ast_string_field_build(s->details, transfer_rate, "%d", stats.bit_rate);

	ast_string_field_build(s->details, resolution, "%dx%d", stats.x_resolution, stats.y_resolution);

	t30_get_tx_page_header_info(t30_state, headerinfo);
	ast_string_field_set(s->details, headerinfo, headerinfo);
}

/*! \brief Send spandsp log messages to asterisk.
 * \param level the spandsp logging level
 * \param msg the log message
 *
 * \note This function is a callback function called by spandsp.
 */
static void spandsp_log(int level, const char *msg)
{
	if (level == SPAN_LOG_ERROR) {
		ast_log(LOG_ERROR, "%s", msg);
	} else if (level == SPAN_LOG_WARNING) {
		ast_log(LOG_WARNING, "%s", msg);
	} else {
		ast_fax_log(LOG_DEBUG, msg);
	}
}

static void set_logging(logging_state_t *state, struct ast_fax_session_details *details)
{
	int level = SPAN_LOG_WARNING;

        if (details->option.debug) {
		level = SPAN_LOG_DEBUG_3;
	}

	span_log_set_message_handler(state, spandsp_log);
	span_log_set_level(state, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | level);
}

static void set_local_info(t30_state_t *t30_state, struct ast_fax_session_details *details)
{
	if (!ast_strlen_zero(details->localstationid)) {
		t30_set_tx_ident(t30_state, details->localstationid);
	}

	if (!ast_strlen_zero(details->headerinfo)) {
		t30_set_tx_page_header_info(t30_state, details->headerinfo);
	}
}

static void set_file(t30_state_t *t30_state, struct ast_fax_session_details *details)
{
	if (details->caps & AST_FAX_TECH_RECEIVE) {
		t30_set_rx_file(t30_state, AST_LIST_FIRST(&details->documents)->filename, -1);
	} else {
		/* if not AST_FAX_TECH_RECEIVE, assume AST_FAX_TECH_SEND, this
		 * should be safe because we ensure either RECEIVE or SEND is
		 * indicated in spandsp_fax_new() */
		t30_set_tx_file(t30_state, AST_LIST_FIRST(&details->documents)->filename, -1, -1);
	}
}

static void set_ecm(t30_state_t *t30_state, struct ast_fax_session_details *details)
{
	t30_set_ecm_capability(t30_state, details->option.ecm);
	t30_set_supported_compressions(t30_state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
}

static int spandsp_v21_new(struct spandsp_pvt *p)
{
	/* XXX Here we use MODEM_CONNECT_TONES_FAX_CED_OR_PREAMBLE even though
	 * we don't care about CED tones. Using MODEM_CONNECT_TONES_PREAMBLE
	 * doesn't seem to work right all the time.
	 */
	p->tone_state = modem_connect_tones_rx_init(NULL, MODEM_CONNECT_TONES_FAX_CED_OR_PREAMBLE, spandsp_v21_tone, p);
	if (!p->tone_state) {
		return -1;
	}

	return 0;
}

static int spandsp_modems(struct ast_fax_session_details *details)
{
	int modems = 0;
	if (AST_FAX_MODEM_V17 & details->modems) {
		modems |= T30_SUPPORT_V17;
	}
	if (AST_FAX_MODEM_V27TER & details->modems) {
		modems |= T30_SUPPORT_V27TER;
	}
	if (AST_FAX_MODEM_V29 & details->modems) {
		modems |= T30_SUPPORT_V29;
	}
	if (AST_FAX_MODEM_V34 & details->modems) {
#if defined(T30_SUPPORT_V34)
		modems |= T30_SUPPORT_V34;
#elif defined(T30_SUPPORT_V34HDX)
		modems |= T30_SUPPORT_V34HDX;
#else
		ast_log(LOG_WARNING, "v34 not supported in this version of spandsp\n");
#endif
	}

	return modems;
}

/*! \brief create an instance of the spandsp tech_pvt for a fax session */
static void *spandsp_fax_new(struct ast_fax_session *s, struct ast_fax_tech_token *token)
{
	struct spandsp_pvt *p;
	int caller_mode;

	if ((!(p = ast_calloc(1, sizeof(*p))))) {
		ast_log(LOG_ERROR, "Cannot initialize the spandsp private FAX technology structure.\n");
		goto e_return;
	}

	if (s->details->caps & AST_FAX_TECH_V21_DETECT) {
		if (spandsp_v21_new(p)) {
			ast_log(LOG_ERROR, "Cannot initialize the spandsp private v21 technology structure.\n");
			goto e_return;
		}
		s->state = AST_FAX_STATE_ACTIVE;
		return p;
	}

	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		s->state = AST_FAX_STATE_INITIALIZED;
		return p;
	}

	AST_LIST_HEAD_INIT(&p->read_frames);

	if (s->details->caps & AST_FAX_TECH_RECEIVE) {
		caller_mode = 0;
	} else if (s->details->caps & AST_FAX_TECH_SEND) {
		caller_mode = 1;
	} else {
		ast_log(LOG_ERROR, "Are we sending or receiving? The FAX requirements (capabilities: 0x%X) were not properly set.\n", s->details->caps);
		goto e_free;
	}

	if (!(p->timer = ast_timer_open())) {
		ast_log(LOG_ERROR, "Channel '%s' FAX session '%u' failed to create timing source.\n", s->channame, s->id);
		goto e_free;
	}

	s->fd = ast_timer_fd(p->timer);

	p->stats = &spandsp_global_stats.g711;

	if (s->details->caps & (AST_FAX_TECH_T38 | AST_FAX_TECH_AUDIO)) {
		if ((s->details->caps & AST_FAX_TECH_AUDIO) == 0) {
			/* audio mode was not requested, start in T.38 mode */
			p->ist38 = 1;
			p->stats = &spandsp_global_stats.t38;
		}

		/* init t38 stuff */
		t38_terminal_init(&p->t38_state, caller_mode, t38_tx_packet_handler, s);
		set_logging(&p->t38_state.logging, s->details);

		/* init audio stuff */
		fax_init(&p->fax_state, caller_mode);
		set_logging(&p->fax_state.logging, s->details);
	}

	s->state = AST_FAX_STATE_INITIALIZED;
	return p;

e_free:
	ast_free(p);
e_return:
	return NULL;
}

static void spandsp_v21_cleanup(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	modem_connect_tones_rx_free(p->tone_state);
}

/*! \brief Destroy a spandsp fax session.
 */
static void spandsp_fax_destroy(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		spandsp_fax_gateway_cleanup(s);
	} else if (s->details->caps & AST_FAX_TECH_V21_DETECT) {
		spandsp_v21_cleanup(s);
	} else {
		session_destroy(p);
	}

	ast_free(p);
	s->tech_pvt = NULL;
	s->fd = -1;
}

/*! \brief Read a frame from the spandsp fax stack.
 */
static struct ast_frame *spandsp_fax_read(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;
	uint8_t buffer[AST_FRIENDLY_OFFSET + SPANDSP_FAX_SAMPLES * sizeof(uint16_t)];
	int16_t *buf = (int16_t *) (buffer + AST_FRIENDLY_OFFSET);
	int samples;

	struct ast_frame fax_frame = {
		.frametype = AST_FRAME_VOICE,
		.src = "res_fax_spandsp_g711",
		.subclass.format = ast_format_slin,
	};
	struct ast_frame *f = &fax_frame;

	if (ast_timer_ack(p->timer, 1) < 0) {
		ast_log(LOG_ERROR, "Failed to acknowledge timer for FAX session '%u'\n", s->id);
		return NULL;
	}

	/* XXX do we need to lock here? */
	if (p->isdone) {
		s->state = AST_FAX_STATE_COMPLETE;
		ast_debug(5, "FAX session '%u' is complete.\n", s->id);
		return NULL;
	}

	if (p->ist38) {
		t38_terminal_send_timeout(&p->t38_state, SPANDSP_FAX_SAMPLES);
		if ((f = AST_LIST_REMOVE_HEAD(&p->read_frames, frame_list))) {
			return f;
		}
	} else {
		if ((samples = fax_tx(&p->fax_state, buf, SPANDSP_FAX_SAMPLES)) > 0) {
			f->samples = samples;
			AST_FRAME_SET_BUFFER(f, buffer, AST_FRIENDLY_OFFSET, samples * sizeof(int16_t));
			return ast_frisolate(f);
		}
	}

	return &ast_null_frame;
}

static void spandsp_v21_tone(void *data, int code, int level, int delay)
{
	struct spandsp_pvt *p = data;

	if (code == MODEM_CONNECT_TONES_FAX_PREAMBLE) {
		p->v21_detected = 1;
	}
}

static int spandsp_v21_detect(struct ast_fax_session *s, const struct ast_frame *f)
{
	struct spandsp_pvt *p = s->tech_pvt;
	int16_t *slndata;
	g711_state_t *decoder;

	if (p->v21_detected) {
		return 0;
	}

	/*invalid frame*/
	if (!f->data.ptr || !f->datalen) {
		return -1;
	}

	ast_debug(5, "frame={ datalen=%d, samples=%d, mallocd=%d, src=%s, flags=%u, ts=%ld, len=%ld, seqno=%d, data.ptr=%p, subclass.format=%s  }\n", f->datalen, f->samples, f->mallocd, f->src, f->flags, f->ts, f->len, f->seqno, f->data.ptr, ast_format_get_name(f->subclass.format));

	/* slinear frame can be passed to spandsp */
	if (ast_format_cmp(f->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL) {
		modem_connect_tones_rx(p->tone_state, f->data.ptr, f->samples);

	/* alaw/ulaw frame must be converted to slinear before passing to spandsp */
	} else if (ast_format_cmp(f->subclass.format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL ||
	           ast_format_cmp(f->subclass.format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		if (!(slndata = ast_malloc(sizeof(*slndata) * f->samples))) {
			return -1;
		}
		decoder = g711_init(NULL, (ast_format_cmp(f->subclass.format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL ? G711_ALAW : G711_ULAW));
		g711_decode(decoder, slndata, f->data.ptr, f->samples);
		ast_debug(5, "spandsp transcoding frame from %s to slinear for v21 detection\n", ast_format_get_name(f->subclass.format));
		modem_connect_tones_rx(p->tone_state, slndata, f->samples);
		g711_release(decoder);
#if SPANDSP_RELEASE_DATE >= 20090220
		g711_free(decoder);
#endif
		ast_free(slndata);

	/* frame in other formats cannot be passed to spandsp, it could cause segfault */
	} else {
		ast_log(LOG_WARNING, "Frame format %s not supported, v.21 detection skipped\n", ast_format_get_name(f->subclass.format));
		return -1;
	}

	if (p->v21_detected) {
		s->details->option.v21_detected = 1;
		ast_debug(5, "v.21 detected\n");
	}

	return 0;
}

/*! \brief Write a frame to the spandsp fax stack.
 * \param s a fax session
 * \param f the frame to write
 *
 * \note res_fax does not currently use the return value of this function.
 * Also the fax_rx() function never fails.
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int spandsp_fax_write(struct ast_fax_session *s, const struct ast_frame *f)
{
	struct spandsp_pvt *p = s->tech_pvt;

	if (s->details->caps & AST_FAX_TECH_V21_DETECT) {
		return spandsp_v21_detect(s, f);
	}

	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		return spandsp_fax_gateway_process(s, f);
	}

	/* XXX do we need to lock here? */
	if (s->state == AST_FAX_STATE_COMPLETE) {
		ast_log(LOG_WARNING, "FAX session '%u' is in the '%s' state.\n", s->id, ast_fax_state_to_str(s->state));
		return -1;
	}

	if (p->ist38) {
		return t38_core_rx_ifp_packet(p->t38_core_state, f->data.ptr, f->datalen, f->seqno);
	} else {
		return fax_rx(&p->fax_state, f->data.ptr, f->samples);
	}
}

/*! \brief generate T.30 packets sent to the T.30 leg of gateway
 * \param chan T.30 channel
 * \param data fax session structure
 * \param len not used
 * \param samples no of samples generated
 * \return -1 on failure or 0 on sucess*/
static int spandsp_fax_gw_t30_gen(struct ast_channel *chan, void *data, int len, int samples)
{
	int res = -1;
	struct ast_fax_session *s = data;
	struct spandsp_pvt *p = s->tech_pvt;
	uint8_t buffer[AST_FRIENDLY_OFFSET + samples * sizeof(uint16_t)];
	struct ast_frame *f;
	struct ast_frame t30_frame = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_slin,
		.src = "res_fax_spandsp_g711",
		.samples = samples,
		.flags = AST_FAX_FRFLAG_GATEWAY,
	};

	AST_FRAME_SET_BUFFER(&t30_frame, buffer, AST_FRIENDLY_OFFSET, t30_frame.samples * sizeof(int16_t));

	if (!(f = ast_frisolate(&t30_frame))) {
		return p->isdone ? -1 : res;
	}

	/* generate a T.30 packet */
	if ((f->samples = t38_gateway_tx(&p->t38_gw_state, f->data.ptr, f->samples))) {
		f->datalen = f->samples * sizeof(int16_t);
		res = ast_write(chan, f);
	}
	ast_frfree(f);
	return p->isdone ? -1 : res;
}

/*! \brief simple routine to allocate data to generator
 * \param chan channel
 * \param params generator data
 * \return data to use in generator call*/
static void *spandsp_fax_gw_gen_alloc(struct ast_channel *chan, void *params)
{
	ao2_ref(params, +1);
	return params;
}

static void spandsp_fax_gw_gen_release(struct ast_channel *chan, void *data)
{
	ao2_ref(data, -1);
}

/*! \brief activate a spandsp gateway based on the information in the given fax session
 * \param s fax session
 * \return -1 on error 0 on sucess*/
static int spandsp_fax_gateway_start(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;
	struct ast_fax_t38_parameters *t38_param;
	int i;
	RAII_VAR(struct ast_channel *, peer, NULL, ao2_cleanup);
	static struct ast_generator t30_gen = {
		.alloc = spandsp_fax_gw_gen_alloc,
		.release = spandsp_fax_gw_gen_release,
		.generate = spandsp_fax_gw_t30_gen,
	};

#if SPANDSP_RELEASE_DATE >= 20081012
	/* for spandsp shaphots 0.0.6 and higher */
	p->t38_core_state=&p->t38_gw_state.t38x.t38;
#else
	/* for spandsp release 0.0.5 */
	p->t38_core_state=&p->t38_gw_state.t38;
#endif

	if (!t38_gateway_init(&p->t38_gw_state, t38_tx_packet_handler, s)) {
		return -1;
	}

	p->ist38 = 1;
	p->ast_t38_state = ast_channel_get_t38_state(s->chan);
	peer = ast_channel_bridge_peer(s->chan);
	if (!peer) {
		return -1;
	}

	/* we can be in T38_STATE_NEGOTIATING or T38_STATE_NEGOTIATED when the
	 * gateway is started. We treat both states the same. */
	if (p->ast_t38_state == T38_STATE_NEGOTIATING) {
		p->ast_t38_state = T38_STATE_NEGOTIATED;
	}

	ast_activate_generator(p->ast_t38_state == T38_STATE_NEGOTIATED ? peer : s->chan, &t30_gen , s);

	set_logging(&p->t38_gw_state.logging, s->details);
	set_logging(&p->t38_core_state->logging, s->details);

	t38_param = (p->ast_t38_state == T38_STATE_NEGOTIATED) ? &s->details->our_t38_parameters : &s->details->their_t38_parameters;
	t38_set_t38_version(p->t38_core_state, t38_param->version);
	t38_gateway_set_ecm_capability(&p->t38_gw_state, s->details->option.ecm);
	t38_set_max_datagram_size(p->t38_core_state, t38_param->max_ifp);
	t38_set_fill_bit_removal(p->t38_core_state, t38_param->fill_bit_removal);
	t38_set_mmr_transcoding(p->t38_core_state, t38_param->transcoding_mmr);
	t38_set_jbig_transcoding(p->t38_core_state, t38_param->transcoding_jbig);
	t38_set_data_rate_management_method(p->t38_core_state, 
			(t38_param->rate_management == AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF)? 1 : 2);

	t38_gateway_set_transmit_on_idle(&p->t38_gw_state, TRUE);
	t38_set_sequence_number_handling(p->t38_core_state, TRUE);


	t38_gateway_set_supported_modems(&p->t38_gw_state, spandsp_modems(s->details));

	/* engage udptl nat on other side of T38 line 
	 * (Asterisk changes media ports thus we send a few packets to reinitialize
	 * pinholes in NATs and FWs
	 */
	for (i=0; i < SPANDSP_ENGAGE_UDPTL_NAT_RETRY; i++) {
#if SPANDSP_RELEASE_DATE >= 20091228
		t38_core_send_indicator(&p->t38_gw_state.t38x.t38, T38_IND_NO_SIGNAL);
#elif SPANDSP_RELEASE_DATE >= 20081012
		t38_core_send_indicator(&p->t38_gw_state.t38x.t38, T38_IND_NO_SIGNAL, p->t38_gw_state.t38x.t38.indicator_tx_count);
#else
		t38_core_send_indicator(&p->t38_gw_state.t38, T38_IND_NO_SIGNAL, p->t38_gw_state.t38.indicator_tx_count);
#endif
	}

	s->state = AST_FAX_STATE_ACTIVE;

	return 0;
}

/*! \brief process a frame from the bridge
 * \param s fax session
 * \param f frame to process
 * \return 1 on sucess 0 on incorect packet*/
static int spandsp_fax_gateway_process(struct ast_fax_session *s, const struct ast_frame *f)
{
	struct spandsp_pvt *p = s->tech_pvt;

	/*invalid frame*/
	if (!f->data.ptr || !f->datalen) {
		return -1;
	}

	/* Process a IFP packet */
	if ((f->frametype == AST_FRAME_MODEM) && (f->subclass.integer == AST_MODEM_T38)) {
		return t38_core_rx_ifp_packet(p->t38_core_state, f->data.ptr, f->datalen, f->seqno);
	} else if ((f->frametype == AST_FRAME_VOICE) &&
		(ast_format_cmp(f->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)) {
		return t38_gateway_rx(&p->t38_gw_state, f->data.ptr, f->samples);
	}

	return -1;
}

/*! \brief gather data and clean up after gateway ends
 * \param s fax session*/
static void spandsp_fax_gateway_cleanup(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;
	t38_stats_t t38_stats;

	t38_gateway_get_transfer_statistics(&p->t38_gw_state, &t38_stats);

	s->details->option.ecm = t38_stats.error_correcting_mode ? AST_FAX_OPTFLAG_TRUE : AST_FAX_OPTFLAG_FALSE;
	s->details->pages_transferred = t38_stats.pages_transferred;
	ast_string_field_build(s->details, transfer_rate, "%d", t38_stats.bit_rate);
}

/*! \brief */
static int spandsp_fax_start(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	s->state = AST_FAX_STATE_OPEN;

	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		return spandsp_fax_gateway_start(s);
	}

	if (p->ist38) {
#if SPANDSP_RELEASE_DATE >= 20080725
		/* for spandsp shaphots 0.0.6 and higher */
		p->t30_state = &p->t38_state.t30;
		p->t38_core_state = &p->t38_state.t38_fe.t38;
#else
		/* for spandsp releases 0.0.5 */
		p->t30_state = &p->t38_state.t30_state;
		p->t38_core_state = &p->t38_state.t38;
#endif
	} else {
#if SPANDSP_RELEASE_DATE >= 20080725
		/* for spandsp shaphots 0.0.6 and higher */
		p->t30_state = &p->fax_state.t30;
#else
		/* for spandsp release 0.0.5 */
		p->t30_state = &p->fax_state.t30_state;
#endif
	}

	set_logging(&p->t30_state->logging, s->details);

	/* set some parameters */
	set_local_info(p->t30_state, s->details);
	set_file(p->t30_state, s->details);
	set_ecm(p->t30_state, s->details);
	t30_set_supported_modems(p->t30_state, spandsp_modems(s->details));

	/* perhaps set_transmit_on_idle() should be called */

	t30_set_phase_e_handler(p->t30_state, t30_phase_e_handler, s);

	/* set T.38 parameters */
	if (p->ist38) {
		set_logging(&p->t38_core_state->logging, s->details);

		t38_set_max_datagram_size(p->t38_core_state, s->details->their_t38_parameters.max_ifp);

		if (s->details->their_t38_parameters.fill_bit_removal) {
			t38_set_fill_bit_removal(p->t38_core_state, TRUE);
		}

		if (s->details->their_t38_parameters.transcoding_mmr) {
			t38_set_mmr_transcoding(p->t38_core_state, TRUE);
		}

		if (s->details->their_t38_parameters.transcoding_jbig) {
			t38_set_jbig_transcoding(p->t38_core_state, TRUE);
		}
	} else {
		/* have the fax stack generate silence if it has no data to send */
		fax_set_transmit_on_idle(&p->fax_state, 1);
	}


	/* start the timer */
	if (ast_timer_set_rate(p->timer, SPANDSP_FAX_TIMER_RATE)) {
		ast_log(LOG_ERROR, "FAX session '%u' error setting rate on timing source.\n", s->id);
		return -1;
	}

	s->state = AST_FAX_STATE_ACTIVE;

	return 0;
}

/*! \brief */
static int spandsp_fax_cancel(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		p->isdone = 1;
		return 0;
	}

	t30_terminate(p->t30_state);
	p->isdone = 1;
	return 0;
}

/*! \brief */
static int spandsp_fax_switch_to_t38(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	/* prevent the phase E handler from running, this is not a real termination */
	t30_set_phase_e_handler(p->t30_state, NULL, NULL);

	t30_terminate(p->t30_state);

	s->details->option.switch_to_t38 = 1;
	ast_atomic_fetchadd_int(&p->stats->switched, 1);

	p->ist38 = 1;
	p->stats = &spandsp_global_stats.t38;
	spandsp_fax_start(s);

	return 0;
}

/*! \brief */
static char *spandsp_fax_cli_show_capabilities(int fd)
{
	ast_cli(fd, "SEND RECEIVE T.38 G.711 GATEWAY\n\n");
	return  CLI_SUCCESS;
}

/*! \brief */
static char *spandsp_fax_cli_show_session(struct ast_fax_session *s, int fd)
{
	ao2_lock(s);
	if (s->details->caps & AST_FAX_TECH_GATEWAY) {
		struct spandsp_pvt *p = s->tech_pvt;

		ast_cli(fd, "%-22s : %u\n", "session", s->id);
		ast_cli(fd, "%-22s : %s\n", "operation", "Gateway");
		ast_cli(fd, "%-22s : %s\n", "state", ast_fax_state_to_str(s->state));
		if (s->state != AST_FAX_STATE_UNINITIALIZED) {
			t38_stats_t stats;
			t38_gateway_get_transfer_statistics(&p->t38_gw_state, &stats);
			ast_cli(fd, "%-22s : %s\n", "ECM Mode", stats.error_correcting_mode ? "Yes" : "No");
			ast_cli(fd, "%-22s : %d\n", "Data Rate", stats.bit_rate);
			ast_cli(fd, "%-22s : %d\n", "Page Number", stats.pages_transferred + 1);
		}
	} else if (s->details->caps & AST_FAX_TECH_V21_DETECT) {
		ast_cli(fd, "%-22s : %u\n", "session", s->id);
		ast_cli(fd, "%-22s : %s\n", "operation", "V.21 Detect");
		ast_cli(fd, "%-22s : %s\n", "state", ast_fax_state_to_str(s->state));
	} else {
		struct spandsp_pvt *p = s->tech_pvt;

		ast_cli(fd, "%-22s : %u\n", "session", s->id);
		ast_cli(fd, "%-22s : %s\n", "operation", (s->details->caps & AST_FAX_TECH_RECEIVE) ? "Receive" : "Transmit");
		ast_cli(fd, "%-22s : %s\n", "state", ast_fax_state_to_str(s->state));
		if (s->state != AST_FAX_STATE_UNINITIALIZED) {
			t30_stats_t stats;
			t30_get_transfer_statistics(p->t30_state, &stats);
			ast_cli(fd, "%-22s : %s\n", "Last Status", t30_completion_code_to_str(stats.current_status));
			ast_cli(fd, "%-22s : %s\n", "ECM Mode", stats.error_correcting_mode ? "Yes" : "No");
			ast_cli(fd, "%-22s : %d\n", "Data Rate", stats.bit_rate);
			ast_cli(fd, "%-22s : %dx%d\n", "Image Resolution", stats.x_resolution, stats.y_resolution);
#if SPANDSP_RELEASE_DATE >= 20090220
			ast_cli(fd, "%-22s : %d\n", "Page Number", ((s->details->caps & AST_FAX_TECH_RECEIVE) ? stats.pages_rx : stats.pages_tx) + 1);
#else
			ast_cli(fd, "%-22s : %d\n", "Page Number", stats.pages_transferred + 1);
#endif
			ast_cli(fd, "%-22s : %s\n", "File Name", s->details->caps & AST_FAX_TECH_RECEIVE ? p->t30_state->rx_file : p->t30_state->tx_file);

			ast_cli(fd, "\nData Statistics:\n");
#if SPANDSP_RELEASE_DATE >= 20090220
			ast_cli(fd, "%-22s : %d\n", "Tx Pages", stats.pages_tx);
			ast_cli(fd, "%-22s : %d\n", "Rx Pages", stats.pages_rx);
#else
			ast_cli(fd, "%-22s : %d\n", "Tx Pages", (s->details->caps & AST_FAX_TECH_SEND) ? stats.pages_transferred : 0);
			ast_cli(fd, "%-22s : %d\n", "Rx Pages", (s->details->caps & AST_FAX_TECH_RECEIVE) ? stats.pages_transferred : 0);
#endif
			ast_cli(fd, "%-22s : %d\n", "Longest Bad Line Run", stats.longest_bad_row_run);
			ast_cli(fd, "%-22s : %d\n", "Total Bad Lines", stats.bad_rows);
		}
	}
	ao2_unlock(s);
	ast_cli(fd, "\n\n");
	return CLI_SUCCESS;
}

static void spandsp_manager_fax_session(struct mansession *s,
	const char *id_text, struct ast_fax_session *session)
{
	struct ast_str *message_string;
	struct spandsp_pvt *span_pvt = session->tech_pvt;
	int res;

	message_string = ast_str_create(128);

	if (!message_string) {
		return;
	}

	ao2_lock(session);
	res = ast_str_append(&message_string, 0, "SessionNumber: %u\r\n", session->id);
	res |= ast_str_append(&message_string, 0, "Operation: %s\r\n", ast_fax_session_operation_str(session));
	res |= ast_str_append(&message_string, 0, "State: %s\r\n", ast_fax_state_to_str(session->state));

	if (session->details->caps & AST_FAX_TECH_GATEWAY) {
		t38_stats_t stats;

		if (session->state == AST_FAX_STATE_UNINITIALIZED) {
			goto skip_cap_additions;
		}

		t38_gateway_get_transfer_statistics(&span_pvt->t38_gw_state, &stats);
		res |= ast_str_append(&message_string, 0, "ErrorCorrectionMode: %s\r\n",
			stats.error_correcting_mode ? "yes" : "no");
		res |= ast_str_append(&message_string, 0, "DataRate: %d\r\n",
			stats.bit_rate);
		res |= ast_str_append(&message_string, 0, "PageNumber: %d\r\n",
			stats.pages_transferred + 1);
	} else if (!(session->details->caps & AST_FAX_TECH_V21_DETECT)) { /* caps is SEND/RECEIVE */
		t30_stats_t stats;

		if (session->state == AST_FAX_STATE_UNINITIALIZED) {
			goto skip_cap_additions;
		}

		t30_get_transfer_statistics(span_pvt->t30_state, &stats);
		res |= ast_str_append(&message_string, 0, "ErrorCorrectionMode: %s\r\n",
			stats.error_correcting_mode ? "Yes" : "No");
		res |= ast_str_append(&message_string, 0, "DataRate: %d\r\n",
			stats.bit_rate);
		res |= ast_str_append(&message_string, 0, "ImageResolution: %dx%d\r\n",
			stats.x_resolution, stats.y_resolution);
#if SPANDSP_RELEASE_DATE >= 20090220
		res |= ast_str_append(&message_string, 0, "PageNumber: %d\r\n",
			((session->details->caps & AST_FAX_TECH_RECEIVE) ? stats.pages_rx : stats.pages_tx) + 1);
#else
		res |= ast_str_append(&message_string, 0, "PageNumber: %d\r\n",
			stats.pages_transferred + 1);
#endif
		res |= ast_str_append(&message_string, 0, "FileName: %s\r\n",
			session->details->caps & AST_FAX_TECH_RECEIVE ? span_pvt->t30_state->rx_file :
			span_pvt->t30_state->tx_file);
#if SPANDSP_RELEASE_DATE >= 20090220
		res |= ast_str_append(&message_string, 0, "PagesTransmitted: %d\r\n",
			stats.pages_tx);
		res |= ast_str_append(&message_string, 0, "PagesReceived: %d\r\n",
			stats.pages_rx);
#else
		res |= ast_str_append(&message_string, 0, "PagesTransmitted: %d\r\n",
			(session->details->caps & AST_FAX_TECH_SEND) ? stats.pages_transferred : 0);
		res |= ast_str_append(&message_string, 0, "PagesReceived: %d\r\n",
			(session->details->caps & AST_FAX_TECH_RECEIVE) ? stats.pages_transferred : 0);
#endif
		res |= ast_str_append(&message_string, 0, "TotalBadLines: %d\r\n",
			stats.bad_rows);
	}

skip_cap_additions:

	ao2_unlock(session);

	if (res < 0) {
		/* One or more of the ast_str_append attempts failed, cancel the message */
		ast_free(message_string);
		return;
	}

	astman_append(s, "Event: FAXSession\r\n"
		"%s"
		"%s"
		"\r\n",
		id_text,
		ast_str_buffer(message_string));

	ast_free(message_string);
}

/*! \brief */
static char *spandsp_fax_cli_show_stats(int fd)
{
	ast_mutex_lock(&spandsp_global_stats.lock);
	ast_cli(fd, "\n%-20.20s\n", "Spandsp G.711");
	ast_cli(fd, "%-20.20s : %d\n", "Success", spandsp_global_stats.g711.success);
	ast_cli(fd, "%-20.20s : %d\n", "Switched to T.38", spandsp_global_stats.g711.switched);
	ast_cli(fd, "%-20.20s : %d\n", "Call Dropped", spandsp_global_stats.g711.call_dropped);
	ast_cli(fd, "%-20.20s : %d\n", "No FAX", spandsp_global_stats.g711.nofax);
	ast_cli(fd, "%-20.20s : %d\n", "Negotiation Failed", spandsp_global_stats.g711.neg_failed);
	ast_cli(fd, "%-20.20s : %d\n", "Train Failure", spandsp_global_stats.g711.failed_to_train);
	ast_cli(fd, "%-20.20s : %d\n", "Retries Exceeded", spandsp_global_stats.g711.retries_exceeded);
	ast_cli(fd, "%-20.20s : %d\n", "Protocol Error", spandsp_global_stats.g711.protocol_error);
	ast_cli(fd, "%-20.20s : %d\n", "TX Protocol Error", spandsp_global_stats.g711.tx_protocol_error);
	ast_cli(fd, "%-20.20s : %d\n", "RX Protocol Error", spandsp_global_stats.g711.rx_protocol_error);
	ast_cli(fd, "%-20.20s : %d\n", "File Error", spandsp_global_stats.g711.file_error);
	ast_cli(fd, "%-20.20s : %d\n", "Memory Error", spandsp_global_stats.g711.mem_error);
	ast_cli(fd, "%-20.20s : %d\n", "Unknown Error", spandsp_global_stats.g711.unknown_error);

	ast_cli(fd, "\n%-20.20s\n", "Spandsp T.38");
	ast_cli(fd, "%-20.20s : %d\n", "Success", spandsp_global_stats.t38.success);
	ast_cli(fd, "%-20.20s : %d\n", "Call Dropped", spandsp_global_stats.t38.call_dropped);
	ast_cli(fd, "%-20.20s : %d\n", "No FAX", spandsp_global_stats.t38.nofax);
	ast_cli(fd, "%-20.20s : %d\n", "Negotiation Failed", spandsp_global_stats.t38.neg_failed);
	ast_cli(fd, "%-20.20s : %d\n", "Train Failure", spandsp_global_stats.t38.failed_to_train);
	ast_cli(fd, "%-20.20s : %d\n", "Retries Exceeded", spandsp_global_stats.t38.retries_exceeded);
	ast_cli(fd, "%-20.20s : %d\n", "Protocol Error", spandsp_global_stats.t38.protocol_error);
	ast_cli(fd, "%-20.20s : %d\n", "TX Protocol Error", spandsp_global_stats.t38.tx_protocol_error);
	ast_cli(fd, "%-20.20s : %d\n", "RX Protocol Error", spandsp_global_stats.t38.rx_protocol_error);
	ast_cli(fd, "%-20.20s : %d\n", "File Error", spandsp_global_stats.t38.file_error);
	ast_cli(fd, "%-20.20s : %d\n", "Memory Error", spandsp_global_stats.t38.mem_error);
	ast_cli(fd, "%-20.20s : %d\n", "Unknown Error", spandsp_global_stats.t38.unknown_error);
	ast_mutex_unlock(&spandsp_global_stats.lock);

	return CLI_SUCCESS;
}

/*! \brief Show res_fax_spandsp settings */
static char *spandsp_fax_cli_show_settings(int fd)
{
	/* no settings at the moment */
	return CLI_SUCCESS;
}

/*! \brief unload res_fax_spandsp */
static void unload_module(void)
{
	ast_fax_tech_unregister(&spandsp_fax_tech);
	ao2_cleanup(spandsp_fax_tech.lib);
	ast_mutex_destroy(&spandsp_global_stats.lock);
}

/*! \brief load res_fax_spandsp */
static int load_module(void)
{
	ast_mutex_init(&spandsp_global_stats.lock);
	spandsp_fax_tech.lib = ast_module_get_lib_running(AST_MODULE_SELF);
	if (ast_fax_tech_register(&spandsp_fax_tech) < 0) {
		ast_log(LOG_ERROR, "failed to register FAX technology\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	/* prevent logging to stderr */
	span_set_message_handler(NULL);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Spandsp G.711 and T.38 FAX Technologies");
