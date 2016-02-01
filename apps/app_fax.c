/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Simple fax applications
 * 
 * 2007-2008, Dmitry Andrianov <asterisk@dima.spb.ru>
 *
 * Code based on original implementation by Steve Underwood <steveu@coppice.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<depend>spandsp</depend>
	<conflict>res_fax</conflict>
	<support_level>extended</support_level>
***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <tiffio.h>

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>
#include <spandsp/version.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/module.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="SendFAX" language="en_US" module="app_fax">
		<synopsis>
			Send a Fax
		</synopsis>
		<syntax>
			<parameter name="filename" required="true">
				<para>Filename of TIFF file to fax</para>
			</parameter>
			<parameter name="a" required="false">
				<para>Makes the application behave as the answering machine</para>
				<para>(Default behavior is as calling machine)</para>
			</parameter>
		</syntax>
		<description>
			<para>Send a given TIFF file to the channel as a FAX.</para>
			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="LOCALSTATIONID">
					<para>To identify itself to the remote end</para>
				</variable>
				<variable name="LOCALHEADERINFO">
					<para>To generate a header line on each page</para>
				</variable>
				<variable name="FAXSTATUS">
					<value name="SUCCESS"/>
					<value name="FAILED"/>
				</variable>
				<variable name="FAXERROR">
					<para>Cause of failure</para>
				</variable>
				<variable name="REMOTESTATIONID">
					<para>The CSID of the remote side</para>
				</variable>
				<variable name="FAXPAGES">
					<para>Number of pages sent</para>
				</variable>
				<variable name="FAXBITRATE">
					<para>Transmission rate</para>
				</variable>
				<variable name="FAXRESOLUTION">
					<para>Resolution of sent fax</para>
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="ReceiveFAX" language="en_US" module="app_fax">
		<synopsis>
			Receive a Fax
		</synopsis>
		<syntax>
			<parameter name="filename" required="true">
				<para>Filename of TIFF file save incoming fax</para>
			</parameter>
			<parameter name="c" required="false">
				<para>Makes the application behave as the calling machine</para> 
				<para>(Default behavior is as answering machine)</para>
			</parameter>
		</syntax>
		<description>
			<para>Receives a FAX from the channel into the given filename 
			overwriting the file if it already exists.</para>
			<para>File created will be in TIFF format.</para>

			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="LOCALSTATIONID">
					<para>To identify itself to the remote end</para>
				</variable>
				<variable name="LOCALHEADERINFO">
					<para>To generate a header line on each page</para>
				</variable>
				<variable name="FAXSTATUS">
					<value name="SUCCESS"/>
					<value name="FAILED"/>
				</variable>
				<variable name="FAXERROR">
					<para>Cause of failure</para>
				</variable>
				<variable name="REMOTESTATIONID">
					<para>The CSID of the remote side</para>
				</variable>
				<variable name="FAXPAGES">
					<para>Number of pages sent</para>
				</variable>
				<variable name="FAXBITRATE">
					<para>Transmission rate</para>
				</variable>
				<variable name="FAXRESOLUTION">
					<para>Resolution of sent fax</para>
				</variable>
			</variablelist>
		</description>
	</application>

 ***/

static const char app_sndfax_name[] = "SendFAX";
static const char app_rcvfax_name[] = "ReceiveFAX";

#define MAX_SAMPLES 240

/* Watchdog. I have seen situations when remote fax disconnects (because of poor line
   quality) while SpanDSP continues staying in T30_STATE_IV_CTC state forever.
   To avoid this, we terminate when we see that T30 state does not change for 5 minutes.
   We also terminate application when more than 30 minutes passed regardless of
   state changes. This is just a precaution measure - no fax should take that long */

#define WATCHDOG_TOTAL_TIMEOUT	30 * 60
#define WATCHDOG_STATE_TIMEOUT	5 * 60

typedef struct {
	struct ast_channel *chan;
	enum ast_t38_state t38state;	/* T38 state of the channel */
	int direction;			/* Fax direction: 0 - receiving, 1 - sending */
	int caller_mode;
	char *file_name;
	struct ast_control_t38_parameters t38parameters;
	volatile int finished;
} fax_session;

static void span_message(int level, const char *msg)
{
	if (level == SPAN_LOG_ERROR) {
		ast_log(LOG_ERROR, "%s", msg);
	} else if (level == SPAN_LOG_WARNING) {
		ast_log(LOG_WARNING, "%s", msg);
	} else {
		ast_debug(1, "%s", msg);
	}
}

static int t38_tx_packet_handler(t38_core_state_t *s, void *user_data, const uint8_t *buf, int len, int count)
{
	struct ast_channel *chan = (struct ast_channel *) user_data;

	struct ast_frame outf = {
		.frametype = AST_FRAME_MODEM,
		.subclass.integer = AST_MODEM_T38,
		.src = __FUNCTION__,
	};

	/* TODO: Asterisk does not provide means of resending the same packet multiple
	  times so count is ignored at the moment */

	AST_FRAME_SET_BUFFER(&outf, buf, 0, len);

	if (ast_write(chan, &outf) < 0) {
		ast_log(LOG_WARNING, "Unable to write frame to channel; %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void phase_e_handler(t30_state_t *f, void *user_data, int result)
{
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, json_filenames, NULL, ast_json_unref);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	const char *local_ident;
	const char *far_ident;
	char buf[20];
	fax_session *s = (fax_session *) user_data;
	t30_stats_t stat;
	int pages_transferred;

	ast_debug(1, "Fax phase E handler. result=%d\n", result);

	t30_get_transfer_statistics(f, &stat);

	s = (fax_session *) user_data;

	if (result != T30_ERR_OK) {
		s->finished = -1;

		/* FAXSTATUS is already set to FAILED */
		pbx_builtin_setvar_helper(s->chan, "FAXERROR", t30_completion_code_to_str(result));

		ast_log(LOG_WARNING, "Error transmitting fax. result=%d: %s.\n", result, t30_completion_code_to_str(result));

		return;
	}

	s->finished = 1;

	local_ident = S_OR(t30_get_tx_ident(f), "");
	far_ident = S_OR(t30_get_rx_ident(f), "");
	pbx_builtin_setvar_helper(s->chan, "FAXSTATUS", "SUCCESS");
	pbx_builtin_setvar_helper(s->chan, "FAXERROR", NULL);
	pbx_builtin_setvar_helper(s->chan, "REMOTESTATIONID", far_ident);
#if SPANDSP_RELEASE_DATE >= 20090220
	pages_transferred = (s->direction) ? stat.pages_tx : stat.pages_rx;
#else
	pages_transferred = stat.pages_transferred;
#endif
	snprintf(buf, sizeof(buf), "%d", pages_transferred);
	pbx_builtin_setvar_helper(s->chan, "FAXPAGES", buf);
	snprintf(buf, sizeof(buf), "%d", stat.y_resolution);
	pbx_builtin_setvar_helper(s->chan, "FAXRESOLUTION", buf);
	snprintf(buf, sizeof(buf), "%d", stat.bit_rate);
	pbx_builtin_setvar_helper(s->chan, "FAXBITRATE", buf);

	ast_debug(1, "Fax transmitted successfully.\n");
	ast_debug(1, "  Remote station ID: %s\n", far_ident);
	ast_debug(1, "  Pages transferred: %d\n", pages_transferred);
	ast_debug(1, "  Image resolution:  %d x %d\n", stat.x_resolution, stat.y_resolution);
	ast_debug(1, "  Transfer Rate:     %d\n", stat.bit_rate);

	json_filenames = ast_json_pack("[s]", s->file_name);
	if (!json_filenames) {
		return;
	}
	ast_json_ref(json_filenames);
	json_object = ast_json_pack("{s: s, s: s, s: s, s: i, s: i, s: i, s: o}",
			"type", s->direction ? "send" : "receive",
			"remote_station_id", far_ident,
			"local_station_id", local_ident,
			"fax_pages", pages_transferred,
			"fax_resolution", stat.y_resolution,
			"fax_bitrate", stat.bit_rate,
			"filenames", json_filenames);
	message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(s->chan), ast_channel_fax_type(), json_object);
	if (!message) {
		return;
	}
	stasis_publish(ast_channel_topic(s->chan), message);
}

/* === Helper functions to configure fax === */

/* Setup SPAN logging according to Asterisk debug level */
static int set_logging(logging_state_t *state)
{
	int level = SPAN_LOG_WARNING + option_debug;

	span_log_set_message_handler(state, span_message);
	span_log_set_level(state, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | level); 

	return 0;
}

static void set_local_info(t30_state_t *state, fax_session *s)
{
	const char *x;

	x = pbx_builtin_getvar_helper(s->chan, "LOCALSTATIONID");
	if (!ast_strlen_zero(x))
		t30_set_tx_ident(state, x);

	x = pbx_builtin_getvar_helper(s->chan, "LOCALHEADERINFO");
	if (!ast_strlen_zero(x))
		t30_set_tx_page_header_info(state, x);
}

static void set_file(t30_state_t *state, fax_session *s)
{
	if (s->direction)
		t30_set_tx_file(state, s->file_name, -1, -1);
	else
		t30_set_rx_file(state, s->file_name, -1);
}

static void set_ecm(t30_state_t *state, int ecm)
{
	t30_set_ecm_capability(state, ecm);
	t30_set_supported_compressions(state, T30_SUPPORT_T4_1D_COMPRESSION | T30_SUPPORT_T4_2D_COMPRESSION | T30_SUPPORT_T6_COMPRESSION);
}

/* === Generator === */

/* This function is only needed to return passed params so
   generator_activate will save it to channel's generatordata */
static void *fax_generator_alloc(struct ast_channel *chan, void *params)
{
	return params;
}

static int fax_generator_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	fax_state_t *fax = (fax_state_t*) data;
	uint8_t buffer[AST_FRIENDLY_OFFSET + MAX_SAMPLES * sizeof(uint16_t)];
	int16_t *buf = (int16_t *) (buffer + AST_FRIENDLY_OFFSET);
    
	struct ast_frame outf = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_slin,
		.src = __FUNCTION__,
	};

	if (samples > MAX_SAMPLES) {
		ast_log(LOG_WARNING, "Only generating %d samples, where %d requested\n", MAX_SAMPLES, samples);
		samples = MAX_SAMPLES;
	}
	
	if ((len = fax_tx(fax, buf, samples)) > 0) {
		outf.samples = len;
		AST_FRAME_SET_BUFFER(&outf, buffer, AST_FRIENDLY_OFFSET, len * sizeof(int16_t));

		if (ast_write(chan, &outf) < 0) {
			ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", ast_channel_name(chan), strerror(errno));
			return -1;
		}
	}

	return 0;
}

static struct ast_generator generator = {
	.alloc = fax_generator_alloc,
	.generate = fax_generator_generate,
};


/* === Transmission === */

static int transmit_audio(fax_session *s)
{
	int res = -1;
	struct ast_format *original_read_fmt;
	struct ast_format *original_write_fmt = NULL;
	fax_state_t fax;
	t30_state_t *t30state;
	struct ast_frame *inf = NULL;
	int last_state = 0;
	struct timeval now, start, state_change;
	enum ast_t38_state t38_state;
	struct ast_control_t38_parameters t38_parameters = { .version = 0,
							     .max_ifp = 800,
							     .rate = AST_T38_RATE_14400,
							     .rate_management = AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF,
							     .fill_bit_removal = 1,
/*
 * spandsp has API calls to support MMR and JBIG transcoding, but they aren't
 * implemented quite yet... so don't offer them to the remote endpoint
 *							     .transcoding_mmr = 1,
 *							     .transcoding_jbig = 1,
*/
	};

	/* if in called party mode, try to use T.38 */
	if (s->caller_mode == FALSE) {
		/* check if we are already in T.38 mode (unlikely), or if we can request
		 * a switch... if so, request it now and wait for the result, rather
		 * than starting an audio FAX session that will have to be cancelled
		 */
		if ((t38_state = ast_channel_get_t38_state(s->chan)) == T38_STATE_NEGOTIATED) {
			return 1;
		} else if ((t38_state != T38_STATE_UNAVAILABLE) &&
			   (t38_parameters.request_response = AST_T38_REQUEST_NEGOTIATE,
			    (ast_indicate_data(s->chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) == 0))) {
			/* wait up to five seconds for negotiation to complete */
			unsigned int timeout = 5000;
			int ms;

			ast_debug(1, "Negotiating T.38 for receive on %s\n", ast_channel_name(s->chan));
			while (timeout > 0) {
				ms = ast_waitfor(s->chan, 1000);
				if (ms < 0) {
					ast_log(LOG_WARNING, "something bad happened while channel '%s' was polling.\n", ast_channel_name(s->chan));
					return -1;
				}
				if (!ms) {
					/* nothing happened */
					if (timeout > 0) {
						timeout -= 1000;
						continue;
					} else {
						ast_log(LOG_WARNING, "channel '%s' timed-out during the T.38 negotiation.\n", ast_channel_name(s->chan));
						break;
					}
				}
				if (!(inf = ast_read(s->chan))) {
					return -1;
				}
				if ((inf->frametype == AST_FRAME_CONTROL) &&
				    (inf->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
				    (inf->datalen == sizeof(t38_parameters))) {
					struct ast_control_t38_parameters *parameters = inf->data.ptr;

					switch (parameters->request_response) {
					case AST_T38_NEGOTIATED:
						ast_debug(1, "Negotiated T.38 for receive on %s\n", ast_channel_name(s->chan));
						res = 1;
						break;
					case AST_T38_REFUSED:
						ast_log(LOG_WARNING, "channel '%s' refused to negotiate T.38\n", ast_channel_name(s->chan));
						break;
					default:
						ast_log(LOG_ERROR, "channel '%s' failed to negotiate T.38\n", ast_channel_name(s->chan));
						break;
					}
					ast_frfree(inf);
					if (res == 1) {
						return 1;
					} else {
						break;
					}
				}
				ast_frfree(inf);
			}
		}
	}

#if SPANDSP_RELEASE_DATE >= 20080725
        /* for spandsp shaphots 0.0.6 and higher */
        t30state = &fax.t30;
#else
        /* for spandsp release 0.0.5 */
        t30state = &fax.t30_state;
#endif

    original_read_fmt = ao2_bump(ast_channel_readformat(s->chan));
	res = ast_set_read_format(s->chan, ast_format_slin);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set to linear read mode, giving up\n");
		goto done;
	}

	original_write_fmt = ao2_bump(ast_channel_writeformat(s->chan));
	res = ast_set_write_format(s->chan, ast_format_slin);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set to linear write mode, giving up\n");
		goto done;
	}

	/* Initialize T30 terminal */
	fax_init(&fax, s->caller_mode);

	/* Setup logging */
	set_logging(&fax.logging);
	set_logging(&t30state->logging);

	/* Configure terminal */
	set_local_info(t30state, s);
	set_file(t30state, s);
	set_ecm(t30state, TRUE);

	fax_set_transmit_on_idle(&fax, TRUE);

	t30_set_phase_e_handler(t30state, phase_e_handler, s);

	start = state_change = ast_tvnow();

	ast_activate_generator(s->chan, &generator, &fax);

	while (!s->finished) {
		inf = NULL;

		if ((res = ast_waitfor(s->chan, 25)) < 0) {
			ast_debug(1, "Error waiting for a frame\n");
			break;
		}

		/* Watchdog */
		now = ast_tvnow();
		if (ast_tvdiff_sec(now, start) > WATCHDOG_TOTAL_TIMEOUT || ast_tvdiff_sec(now, state_change) > WATCHDOG_STATE_TIMEOUT) {
			ast_log(LOG_WARNING, "It looks like we hung. Aborting.\n");
			res = -1;
			break;
		}

		if (!res) {
			/* There was timeout waiting for a frame. Loop around and wait again */
			continue;
		}

		/* There is a frame available. Get it */
		res = 0;

		if (!(inf = ast_read(s->chan))) {
			ast_debug(1, "Channel hangup\n");
			res = -1;
			break;
		}

		ast_debug(10, "frame %d/%s, len=%d\n", inf->frametype, ast_format_get_name(inf->subclass.format), inf->datalen);

		/* Check the frame type. Format also must be checked because there is a chance
		   that a frame in old format was already queued before we set channel format
		   to slinear so it will still be received by ast_read */
		if (inf->frametype == AST_FRAME_VOICE &&
			(ast_format_cmp(inf->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)) {
			if (fax_rx(&fax, inf->data.ptr, inf->samples) < 0) {
				/* I know fax_rx never returns errors. The check here is for good style only */
				ast_log(LOG_WARNING, "fax_rx returned error\n");
				res = -1;
				break;
			}
			if (last_state != t30state->state) {
				state_change = ast_tvnow();
				last_state = t30state->state;
			}
		} else if ((inf->frametype == AST_FRAME_CONTROL) &&
			   (inf->subclass.integer == AST_CONTROL_T38_PARAMETERS)) {
			struct ast_control_t38_parameters *parameters = inf->data.ptr;

			if (parameters->request_response == AST_T38_NEGOTIATED) {
				/* T38 switchover completed */
				s->t38parameters = *parameters;
				ast_debug(1, "T38 negotiated, finishing audio loop\n");
				res = 1;
				break;
			} else if (parameters->request_response == AST_T38_REQUEST_NEGOTIATE) {
				t38_parameters.request_response = AST_T38_NEGOTIATED;
				ast_debug(1, "T38 request received, accepting\n");
				/* Complete T38 switchover */
				ast_indicate_data(s->chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters));
				/* Do not break audio loop, wait until channel driver finally acks switchover
				 * with AST_T38_NEGOTIATED
				 */
			}
		}

		ast_frfree(inf);
		inf = NULL;
	}

	ast_debug(1, "Loop finished, res=%d\n", res);

	if (inf)
		ast_frfree(inf);

	ast_deactivate_generator(s->chan);

	/* If we are switching to T38, remove phase E handler. Otherwise it will be executed
	   by t30_terminate, display diagnostics and set status variables although no transmittion
	   has taken place yet. */
	if (res > 0) {
		t30_set_phase_e_handler(t30state, NULL, NULL);
	}

	t30_terminate(t30state);
	fax_release(&fax);

done:
	if (original_write_fmt) {
		if (ast_set_write_format(s->chan, original_write_fmt) < 0)
			ast_log(LOG_WARNING, "Unable to restore write format on '%s'\n", ast_channel_name(s->chan));
		ao2_ref(original_write_fmt, -1);
	}

	if (original_read_fmt) {
		if (ast_set_read_format(s->chan, original_read_fmt) < 0)
			ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", ast_channel_name(s->chan));
		ao2_ref(original_read_fmt, -1);
	}

	return res;

}

static int transmit_t38(fax_session *s)
{
	int res = 0;
	t38_terminal_state_t t38;
	struct ast_frame *inf = NULL;
	int last_state = 0;
	struct timeval now, start, state_change, last_frame;
	t30_state_t *t30state;
	t38_core_state_t *t38state;

#if SPANDSP_RELEASE_DATE >= 20080725
	/* for spandsp shaphots 0.0.6 and higher */
	t30state = &t38.t30;
	t38state = &t38.t38_fe.t38;
#else
	/* for spandsp releases 0.0.5 */
	t30state = &t38.t30_state;
	t38state = &t38.t38;
#endif

	/* Initialize terminal */
	memset(&t38, 0, sizeof(t38));
	if (t38_terminal_init(&t38, s->caller_mode, t38_tx_packet_handler, s->chan) == NULL) {
		ast_log(LOG_WARNING, "Unable to start T.38 termination.\n");
		res = -1;
		goto disable_t38;
	}

	t38_set_max_datagram_size(t38state, s->t38parameters.max_ifp);

	if (s->t38parameters.fill_bit_removal) {
		t38_set_fill_bit_removal(t38state, TRUE);
	}
	if (s->t38parameters.transcoding_mmr) {
		t38_set_mmr_transcoding(t38state, TRUE);
	}
	if (s->t38parameters.transcoding_jbig) {
		t38_set_jbig_transcoding(t38state, TRUE);
	}

	/* Setup logging */
	set_logging(&t38.logging);
	set_logging(&t30state->logging);
	set_logging(&t38state->logging);

	/* Configure terminal */
	set_local_info(t30state, s);
	set_file(t30state, s);
	set_ecm(t30state, TRUE);

	t30_set_phase_e_handler(t30state, phase_e_handler, s);

	now = start = state_change = ast_tvnow();

	while (!s->finished) {
		inf = NULL;

		if ((res = ast_waitfor(s->chan, 25)) < 0) {
			ast_debug(1, "Error waiting for a frame\n");
			break;
		}

		last_frame = now;

		/* Watchdog */
		now = ast_tvnow();
		if (ast_tvdiff_sec(now, start) > WATCHDOG_TOTAL_TIMEOUT || ast_tvdiff_sec(now, state_change) > WATCHDOG_STATE_TIMEOUT) {
			ast_log(LOG_WARNING, "It looks like we hung. Aborting.\n");
			res = -1;
			break;
		}

		t38_terminal_send_timeout(&t38, ast_tvdiff_us(now, last_frame) / (1000000 / 8000));

		if (!res) {
			/* There was timeout waiting for a frame. Loop around and wait again */
			continue;
		}

		/* There is a frame available. Get it */
		res = 0;

		if (!(inf = ast_read(s->chan))) {
			ast_debug(1, "Channel hangup\n");
			res = -1;
			break;
		}

		ast_debug(10, "frame %d/%d, len=%d\n", inf->frametype, inf->subclass.integer, inf->datalen);

		if (inf->frametype == AST_FRAME_MODEM && inf->subclass.integer == AST_MODEM_T38) {
			t38_core_rx_ifp_packet(t38state, inf->data.ptr, inf->datalen, inf->seqno);
			if (last_state != t30state->state) {
				state_change = ast_tvnow();
				last_state = t30state->state;
			}
		} else if (inf->frametype == AST_FRAME_CONTROL && inf->subclass.integer == AST_CONTROL_T38_PARAMETERS) {
			struct ast_control_t38_parameters *parameters = inf->data.ptr;
			if (parameters->request_response == AST_T38_TERMINATED) {
				ast_debug(1, "T38 down, finishing\n");
				break;
			}
		}

		ast_frfree(inf);
		inf = NULL;
	}

	ast_debug(1, "Loop finished, res=%d\n", res);

	if (inf)
		ast_frfree(inf);

	t30_terminate(t30state);
	t38_terminal_release(&t38);

disable_t38:
	/* if we are not the caller, it's our job to shut down the T.38
	 * session when the FAX transmisson is complete.
	 */
	if ((s->caller_mode == FALSE) &&
	    (ast_channel_get_t38_state(s->chan) == T38_STATE_NEGOTIATED)) {
		struct ast_control_t38_parameters t38_parameters = { .request_response = AST_T38_REQUEST_TERMINATE, };

		if (ast_indicate_data(s->chan, AST_CONTROL_T38_PARAMETERS, &t38_parameters, sizeof(t38_parameters)) == 0) {
			/* wait up to five seconds for negotiation to complete */
			unsigned int timeout = 5000;
			int ms;

			ast_debug(1, "Shutting down T.38 on %s\n", ast_channel_name(s->chan));
			while (timeout > 0) {
				ms = ast_waitfor(s->chan, 1000);
				if (ms < 0) {
					ast_log(LOG_WARNING, "something bad happened while channel '%s' was polling.\n", ast_channel_name(s->chan));
					return -1;
				}
				if (!ms) {
					/* nothing happened */
					if (timeout > 0) {
						timeout -= 1000;
						continue;
					} else {
						ast_log(LOG_WARNING, "channel '%s' timed-out during the T.38 shutdown.\n", ast_channel_name(s->chan));
						break;
					}
				}
				if (!(inf = ast_read(s->chan))) {
					return -1;
				}
				if ((inf->frametype == AST_FRAME_CONTROL) &&
				    (inf->subclass.integer == AST_CONTROL_T38_PARAMETERS) &&
				    (inf->datalen == sizeof(t38_parameters))) {
					struct ast_control_t38_parameters *parameters = inf->data.ptr;

					switch (parameters->request_response) {
					case AST_T38_TERMINATED:
						ast_debug(1, "Shut down T.38 on %s\n", ast_channel_name(s->chan));
						break;
					case AST_T38_REFUSED:
						ast_log(LOG_WARNING, "channel '%s' refused to disable T.38\n", ast_channel_name(s->chan));
						break;
					default:
						ast_log(LOG_ERROR, "channel '%s' failed to disable T.38\n", ast_channel_name(s->chan));
						break;
					}
					ast_frfree(inf);
					break;
				}
				ast_frfree(inf);
			}
		}
	}

	return res;
}

static int transmit(fax_session *s)
{
	int res = 0;

	/* Clear all channel variables which to be set by the application.
	   Pre-set status to error so in case of any problems we can just leave */
	pbx_builtin_setvar_helper(s->chan, "FAXSTATUS", "FAILED"); 
	pbx_builtin_setvar_helper(s->chan, "FAXERROR", "Channel problems"); 

	pbx_builtin_setvar_helper(s->chan, "FAXMODE", NULL);
	pbx_builtin_setvar_helper(s->chan, "REMOTESTATIONID", NULL);
	pbx_builtin_setvar_helper(s->chan, "FAXPAGES", "0");
	pbx_builtin_setvar_helper(s->chan, "FAXRESOLUTION", NULL);
	pbx_builtin_setvar_helper(s->chan, "FAXBITRATE", NULL); 

	if (ast_channel_state(s->chan) != AST_STATE_UP) {
		/* Shouldn't need this, but checking to see if channel is already answered
		 * Theoretically asterisk should already have answered before running the app */
		res = ast_answer(s->chan);
		if (res) {
			ast_log(LOG_WARNING, "Could not answer channel '%s'\n", ast_channel_name(s->chan));
			return res;
		}
	}

	s->t38state = ast_channel_get_t38_state(s->chan);
	if (s->t38state != T38_STATE_NEGOTIATED) {
		/* T38 is not negotiated on the channel yet. First start regular transmission. If it switches to T38, follow */	
		pbx_builtin_setvar_helper(s->chan, "FAXMODE", "audio"); 
		res = transmit_audio(s);
		if (res > 0) {
			/* transmit_audio reports switchover to T38. Update t38state */
			s->t38state = ast_channel_get_t38_state(s->chan);
			if (s->t38state != T38_STATE_NEGOTIATED) {
				ast_log(LOG_ERROR, "Audio loop reports T38 switchover but t38state != T38_STATE_NEGOTIATED\n");
			}
		}
	}

	if (s->t38state == T38_STATE_NEGOTIATED) {
		pbx_builtin_setvar_helper(s->chan, "FAXMODE", "T38"); 
		res = transmit_t38(s);
	}

	if (res) {
		ast_log(LOG_WARNING, "Transmission error\n");
		res = -1;
	} else if (s->finished < 0) {
		ast_log(LOG_WARNING, "Transmission failed\n");
	} else if (s->finished > 0) {
		ast_debug(1, "Transmission finished Ok\n");
	}

	return res;
}

/* === Application functions === */

static int sndfax_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *parse;
	fax_session session = { 0, };
	char restore_digit_detect = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(file_name);
		AST_APP_ARG(options);
	);

	if (chan == NULL) {
		ast_log(LOG_ERROR, "Fax channel is NULL. Giving up.\n");
		return -1;
	}

	/* The next few lines of code parse out the filename and header from the input string */
	if (ast_strlen_zero(data)) {
		/* No data implies no filename or anything is present */
		ast_log(LOG_ERROR, "SendFAX requires an argument (filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	
	session.caller_mode = TRUE;

	if (args.options) {
		if (strchr(args.options, 'a'))
			session.caller_mode = FALSE;
	}

	/* Done parsing */
	session.direction = 1;
	session.file_name = args.file_name;
	session.chan = chan;
	session.finished = 0;

	/* get current digit detection mode, then disable digit detection if enabled */
	{
		int dummy = sizeof(restore_digit_detect);

		ast_channel_queryoption(chan, AST_OPTION_DIGIT_DETECT, &restore_digit_detect, &dummy, 0);
	}

	if (restore_digit_detect) {
		char new_digit_detect = 0;

		ast_channel_setoption(chan, AST_OPTION_DIGIT_DETECT, &new_digit_detect, sizeof(new_digit_detect), 0);
	}

	/* disable FAX tone detection if enabled */
	{
		char new_fax_detect = 0;

		ast_channel_setoption(chan, AST_OPTION_FAX_DETECT, &new_fax_detect, sizeof(new_fax_detect), 0);
	}

	res = transmit(&session);

	if (restore_digit_detect) {
		ast_channel_setoption(chan, AST_OPTION_DIGIT_DETECT, &restore_digit_detect, sizeof(restore_digit_detect), 0);
	}

	return res;
}

static int rcvfax_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *parse;
	fax_session session;
	char restore_digit_detect = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(file_name);
		AST_APP_ARG(options);
	);

	if (chan == NULL) {
		ast_log(LOG_ERROR, "Fax channel is NULL. Giving up.\n");
		return -1;
	}

	/* The next few lines of code parse out the filename and header from the input string */
	if (ast_strlen_zero(data)) {
		/* No data implies no filename or anything is present */
		ast_log(LOG_ERROR, "ReceiveFAX requires an argument (filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	
	session.caller_mode = FALSE;

	if (args.options) {
		if (strchr(args.options, 'c'))
			session.caller_mode = TRUE;
	}

	/* Done parsing */
	session.direction = 0;
	session.file_name = args.file_name;
	session.chan = chan;
	session.finished = 0;

	/* get current digit detection mode, then disable digit detection if enabled */
	{
		int dummy = sizeof(restore_digit_detect);

		ast_channel_queryoption(chan, AST_OPTION_DIGIT_DETECT, &restore_digit_detect, &dummy, 0);
	}

	if (restore_digit_detect) {
		char new_digit_detect = 0;

		ast_channel_setoption(chan, AST_OPTION_DIGIT_DETECT, &new_digit_detect, sizeof(new_digit_detect), 0);
	}

	/* disable FAX tone detection if enabled */
	{
		char new_fax_detect = 0;

		ast_channel_setoption(chan, AST_OPTION_FAX_DETECT, &new_fax_detect, sizeof(new_fax_detect), 0);
	}

	res = transmit(&session);

	if (restore_digit_detect) {
		ast_channel_setoption(chan, AST_OPTION_DIGIT_DETECT, &restore_digit_detect, sizeof(restore_digit_detect), 0);
	}

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_sndfax_name);	
	res |= ast_unregister_application(app_rcvfax_name);	

	return res;
}

static int load_module(void)
{
	int res ;

	res = ast_register_application_xml(app_sndfax_name, sndfax_exec);
	res |= ast_register_application_xml(app_rcvfax_name, rcvfax_exec);

	/* The default SPAN message handler prints to stderr. It is something we do not want */
	span_set_message_handler(NULL);

	return res;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Simple FAX Application",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
);


