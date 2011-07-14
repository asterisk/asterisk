/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2010, Digium, Inc.
 *
 * Matthew Nicholson <mnicholson@digium.com>
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
 *
 * This module registers the Spandsp FAX technology with the res_fax module.
 */

/*** MODULEINFO
	<depend>spandsp</depend>
	<depend>res_fax</depend>
	<support_level>extended</support_level>
***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>
#include <spandsp/version.h>

#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/timing.h"
#include "asterisk/astobj2.h"
#include "asterisk/res_fax.h"

#define SPANDSP_FAX_SAMPLES 160
#define SPANDSP_FAX_TIMER_RATE 8000 / SPANDSP_FAX_SAMPLES	/* 50 ticks per second, 20ms, 160 samples per second */

static void *spandsp_fax_new(struct ast_fax_session *s, struct ast_fax_tech_token *token);
static void spandsp_fax_destroy(struct ast_fax_session *s);
static struct ast_frame *spandsp_fax_read(struct ast_fax_session *s);
static int spandsp_fax_write(struct ast_fax_session *s, const struct ast_frame *f);
static int spandsp_fax_start(struct ast_fax_session *s);
static int spandsp_fax_cancel(struct ast_fax_session *s);
static int spandsp_fax_switch_to_t38(struct ast_fax_session *s);

static char *spandsp_fax_cli_show_capabilities(int fd);
static char *spandsp_fax_cli_show_session(struct ast_fax_session *s, int fd);
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
	.caps = AST_FAX_TECH_AUDIO | AST_FAX_TECH_T38 | AST_FAX_TECH_SEND | AST_FAX_TECH_RECEIVE,
	.new_session = spandsp_fax_new,
	.destroy_session = spandsp_fax_destroy,
	.read = spandsp_fax_read,
	.write = spandsp_fax_write,
	.start_session = spandsp_fax_start,
	.cancel_session = spandsp_fax_cancel,
	.switch_to_t38 = spandsp_fax_switch_to_t38,
	.cli_show_capabilities = spandsp_fax_cli_show_capabilities,
	.cli_show_session = spandsp_fax_cli_show_session,
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
	fax_state_t fax_state;
	t38_terminal_state_t t38_state;
	t30_state_t *t30_state;
	t38_core_state_t *t38_core_state;

	struct spandsp_fax_stats *stats;

	struct ast_timer *timer;
	AST_LIST_HEAD(frame_queue, ast_frame) read_frames;
};

static void session_destroy(struct spandsp_pvt *p);
static int t38_tx_packet_handler(t38_core_state_t *t38_core_state, void *data, const uint8_t *buf, int len, int count);
static void t30_phase_e_handler(t30_state_t *t30_state, void *data, int completion_code);
static void spandsp_log(int level, const char *msg);
static int update_stats(struct spandsp_pvt *p, int completion_code);

static void set_logging(logging_state_t *state, struct ast_fax_session_details *details);
static void set_local_info(t30_state_t *t30_state, struct ast_fax_session_details *details);
static void set_file(t30_state_t *t30_state, struct ast_fax_session_details *details);
static void set_ecm(t30_state_t *t30_state, struct ast_fax_session_details *details);

static void session_destroy(struct spandsp_pvt *p)
{
	struct ast_frame *f;

	t30_terminate(p->t30_state);
	p->isdone = 1;

	ast_timer_close(p->timer);

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
	struct spandsp_pvt *p = data;
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
		return -1;
	}

	/* no need to lock, this all runs in the same thread */
	AST_LIST_INSERT_TAIL(&p->read_frames, f, frame_list);

	return 0;
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

	ast_debug(5, "FAX session '%d' entering phase E\n", s->id);

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

	ast_debug(5, "FAX session '%d' completed with result: %s (%s)\n", s->id, s->details->result, s->details->resultstr);

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

/*! \brief create an instance of the spandsp tech_pvt for a fax session */
static void *spandsp_fax_new(struct ast_fax_session *s, struct ast_fax_tech_token *token)
{
	struct spandsp_pvt *p;
	int caller_mode;

	if ((!(p = ast_calloc(1, sizeof(*p))))) {
		ast_log(LOG_ERROR, "Cannot initialize the spandsp private FAX technology structure.\n");
		goto e_return;
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
		ast_log(LOG_ERROR, "Channel '%s' FAX session '%d' failed to create timing source.\n", s->channame, s->id);
		goto e_free;
	}

	s->fd = ast_timer_fd(p->timer);

	p->stats = &spandsp_global_stats.g711;

	if (s->details->caps & AST_FAX_TECH_T38) {
		if ((s->details->caps & AST_FAX_TECH_AUDIO) == 0) {
			/* audio mode was not requested, start in T.38 mode */
			p->ist38 = 1;
			p->stats = &spandsp_global_stats.t38;
		}

		/* init t38 stuff */
		t38_terminal_init(&p->t38_state, caller_mode, t38_tx_packet_handler, p);
		set_logging(&p->t38_state.logging, s->details);
	}

	if (s->details->caps & AST_FAX_TECH_AUDIO) {
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

/*! \brief Destroy a spandsp fax session.
 */
static void spandsp_fax_destroy(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	session_destroy(p);
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
		.subclass.codec = AST_FORMAT_SLINEAR,
		.src = "res_fax_spandsp_g711",
	};

	struct ast_frame *f = &fax_frame;

	ast_timer_ack(p->timer, 1);

	/* XXX do we need to lock here? */
	if (p->isdone) {
		s->state = AST_FAX_STATE_COMPLETE;
		ast_debug(5, "FAX session '%d' is complete.\n", s->id);
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

	/* XXX do we need to lock here? */
	if (s->state == AST_FAX_STATE_COMPLETE) {
		ast_log(LOG_WARNING, "FAX session '%d' is in the '%s' state.\n", s->id, ast_fax_state_to_str(s->state));
		return -1;
	}

	if (p->ist38) {
		return t38_core_rx_ifp_packet(p->t38_core_state, f->data.ptr, f->datalen, f->seqno);
	} else {
		return fax_rx(&p->fax_state, f->data.ptr, f->samples);
	}
}

/*! \brief */
static int spandsp_fax_start(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;

	s->state = AST_FAX_STATE_OPEN;

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
		ast_log(LOG_ERROR, "FAX session '%d' error setting rate on timing source.\n", s->id);
		return -1;
	}

	s->state = AST_FAX_STATE_ACTIVE;

	return 0;
}

/*! \brief */
static int spandsp_fax_cancel(struct ast_fax_session *s)
{
	struct spandsp_pvt *p = s->tech_pvt;
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
	ast_cli(fd, "SEND RECEIVE T.38 G.711\n\n");
	return  CLI_SUCCESS;
}

/*! \brief */
static char *spandsp_fax_cli_show_session(struct ast_fax_session *s, int fd)
{
	struct spandsp_pvt *p = s->tech_pvt;
	t30_stats_t stats;

	ao2_lock(s);
	ast_cli(fd, "%-22s : %d\n", "session", s->id);
	ast_cli(fd, "%-22s : %s\n", "operation", (s->details->caps & AST_FAX_TECH_RECEIVE) ? "Receive" : "Transmit");
	ast_cli(fd, "%-22s : %s\n", "state", ast_fax_state_to_str(s->state));
	if (s->state != AST_FAX_STATE_UNINITIALIZED) {
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
	ao2_unlock(s);
	ast_cli(fd, "\n\n");
	return CLI_SUCCESS;
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
static int unload_module(void)
{
	ast_fax_tech_unregister(&spandsp_fax_tech);
	ast_mutex_destroy(&spandsp_global_stats.lock);
	return AST_MODULE_LOAD_SUCCESS;
}

/*! \brief load res_fax_spandsp */
static int load_module(void)
{
	ast_mutex_init(&spandsp_global_stats.lock);
	spandsp_fax_tech.module = ast_module_info->self;
	if (ast_fax_tech_register(&spandsp_fax_tech) < 0) {
		ast_log(LOG_ERROR, "failed to register FAX technology\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	/* prevent logging to stderr */
	span_set_message_handler(NULL);

	return AST_MODULE_LOAD_SUCCESS;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Spandsp G.711 and T.38 FAX Technologies",
		.load = load_module,
		.unload = unload_module,
	       );
