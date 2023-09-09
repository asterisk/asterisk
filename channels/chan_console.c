/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006 - 2008, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \brief Cross-platform console channel driver
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \note Some of the code in this file came from chan_oss and chan_alsa.
 *       chan_oss,  Mark Spencer <markster@digium.com>
 *       chan_oss,  Luigi Rizzo
 *       chan_alsa, Matthew Fredrickson <creslin@digium.com>
 *
 * \ingroup channel_drivers
 *
 * Portaudio http://www.portaudio.com/
 *
 * To install portaudio v19 from svn, check it out using the following command:
 *  - svn co https://www.portaudio.com/repos/portaudio/branches/v19-devel
 *
 * \note Since this works with any audio system that libportaudio supports,
 * including ALSA and OSS, it has come to replace the deprecated chan_alsa and
 * chan_oss. However, the following features *at least* need to be implemented
 * here for this to be a full replacement:
 *
 * - Set Auto-answer from the dialplan
 * - transfer CLI command
 * - boost CLI command and .conf option
 * - console_video support
 */

/*! \li \ref chan_console.c uses the configuration file \ref console.conf
 * \addtogroup configuration_file
 */

/*! \page console.conf console.conf
 * \verbinclude console.conf.sample
 */

/*** MODULEINFO
	<depend>portaudio</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <signal.h>  /* SIGURG */

#include <portaudio.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/cli.h"
#include "asterisk/musiconhold.h"
#include "asterisk/callerid.h"
#include "asterisk/astobj2.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/format_cache.h"

/*!
 * \brief The sample rate to request from PortAudio
 *
 * \todo Make this optional.  If this is only going to talk to 8 kHz endpoints,
 *       then it makes sense to use 8 kHz natively.
 */
#define SAMPLE_RATE      16000

/*!
 * \brief The number of samples to configure the portaudio stream for
 *
 * 320 samples (20 ms) is the most common frame size in Asterisk.  So, the code
 * in this module reads 320 sample frames from the portaudio stream and queues
 * them up on the Asterisk channel.  Frames of any size can be written to a
 * portaudio stream, but the portaudio documentation does say that for high
 * performance applications, the data should be written to Pa_WriteStream in
 * the same size as what is used to initialize the stream.
 */
#define NUM_SAMPLES      320

/*! \brief Mono Input */
#define INPUT_CHANNELS   1

/*! \brief Mono Output */
#define OUTPUT_CHANNELS  1

/*!
 * \brief Maximum text message length
 * \note This should be changed if there is a common definition somewhere
 *       that defines the maximum length of a text message.
 */
#define TEXT_SIZE	256

/*! \brief Dance, Kirby, Dance! @{ */
#define V_BEGIN " --- <(\"<) --- "
#define V_END   " --- (>\")> ---\n"
/*! @} */

static const char config_file[] = "console.conf";

/*!
 * \brief Console pvt structure
 *
 * Currently, this is a singleton object.  However, multiple instances will be
 * needed when this module is updated for multiple device support.
 */
static struct console_pvt {
	AST_DECLARE_STRING_FIELDS(
		/*! Name of the device */
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(input_device);
		AST_STRING_FIELD(output_device);
		/*! Default context for outgoing calls */
		AST_STRING_FIELD(context);
		/*! Default extension for outgoing calls */
		AST_STRING_FIELD(exten);
		/*! Default CallerID number */
		AST_STRING_FIELD(cid_num);
		/*! Default CallerID name */
		AST_STRING_FIELD(cid_name);
		/*! Default MOH class to listen to, if:
		 *    - No MOH class set on the channel
		 *    - Peer channel putting this device on hold did not suggest a class */
		AST_STRING_FIELD(mohinterpret);
		/*! Default language */
		AST_STRING_FIELD(language);
		/*! Default parkinglot */
		AST_STRING_FIELD(parkinglot);
	);
	/*! Current channel for this device */
	struct ast_channel *owner;
	/*! Current PortAudio stream for this device */
	PaStream *stream;
	/*! A frame for preparing to queue on to the channel */
	struct ast_frame fr;
	/*! Running = 1, Not running = 0 */
	unsigned int streamstate:1;
	/*! Abort stream processing? */
	unsigned int abort:1;
	/*! On-hook = 0, Off-hook = 1 */
	unsigned int hookstate:1;
	/*! Unmuted = 0, Muted = 1 */
	unsigned int muted:1;
	/*! Automatically answer incoming calls */
	unsigned int autoanswer:1;
	/*! Ignore context in the console dial CLI command */
	unsigned int overridecontext:1;
	/*! Set during a reload so that we know to destroy this if it is no longer
	 *  in the configuration file. */
	unsigned int destroy:1;
	/*! ID for the stream monitor thread */
	pthread_t thread;
} globals;

AST_MUTEX_DEFINE_STATIC(globals_lock);

static struct ao2_container *pvts;
#define NUM_PVT_BUCKETS 7

static struct console_pvt *active_pvt;
AST_RWLOCK_DEFINE_STATIC(active_lock);

/*!
 * \brief Global jitterbuffer configuration
 *
 * \note Disabled by default.
 * \note Values shown here match the defaults shown in console.conf.sample
 */
static struct ast_jb_conf default_jbconf = {
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};
static struct ast_jb_conf global_jbconf;

/*! Channel Technology Callbacks @{ */
static struct ast_channel *console_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int console_digit_begin(struct ast_channel *c, char digit);
static int console_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int console_text(struct ast_channel *c, const char *text);
static int console_hangup(struct ast_channel *c);
static int console_answer(struct ast_channel *c);
static struct ast_frame *console_read(struct ast_channel *chan);
static int console_call(struct ast_channel *c, const char *dest, int timeout);
static int console_write(struct ast_channel *chan, struct ast_frame *f);
static int console_indicate(struct ast_channel *chan, int cond,
	const void *data, size_t datalen);
static int console_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
/*! @} */

static struct ast_channel_tech console_tech = {
	.type = "Console",
	.description = "Console Channel Driver",
	.requester = console_request,
	.send_digit_begin = console_digit_begin,
	.send_digit_end = console_digit_end,
	.send_text = console_text,
	.hangup = console_hangup,
	.answer = console_answer,
	.read = console_read,
	.call = console_call,
	.write = console_write,
	.indicate = console_indicate,
	.fixup = console_fixup,
};

/*! \brief lock a console_pvt struct */
#define console_pvt_lock(pvt) ao2_lock(pvt)

/*! \brief unlock a console_pvt struct */
#define console_pvt_unlock(pvt) ao2_unlock(pvt)

static inline struct console_pvt *ref_pvt(struct console_pvt *pvt)
{
	if (pvt)
		ao2_ref(pvt, +1);
	return pvt;
}

static inline struct console_pvt *unref_pvt(struct console_pvt *pvt)
{
	ao2_ref(pvt, -1);
	return NULL;
}

static struct console_pvt *find_pvt(const char *name)
{
	struct console_pvt tmp_pvt = {
		.name = name,
	};

	return ao2_find(pvts, &tmp_pvt, OBJ_POINTER);
}

/*!
 * \brief Stream monitor thread
 *
 * \arg data A pointer to the console_pvt structure that contains the portaudio
 *      stream that needs to be monitored.
 *
 * This function runs in its own thread to monitor data coming in from a
 * portaudio stream.  When enough data is available, it is queued up to
 * be read from the Asterisk channel.
 */
static void *stream_monitor(void *data)
{
	struct console_pvt *pvt = data;
	char buf[NUM_SAMPLES * sizeof(int16_t)];
	PaError res;
	struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_slin16,
		.src = "console_stream_monitor",
		.data.ptr = buf,
		.datalen = sizeof(buf),
		.samples = sizeof(buf) / sizeof(int16_t),
	};

	for (;;) {
		console_pvt_lock(pvt);
		res = Pa_ReadStream(pvt->stream, buf, sizeof(buf) / sizeof(int16_t));
		console_pvt_unlock(pvt);

		if (!pvt->owner || pvt->abort) {
			return NULL;
		}

		if (res == paNoError) {
			ast_queue_frame(pvt->owner, &f);
		} else {
			ast_log(LOG_WARNING, "Console ReadStream failed: %s\n", Pa_GetErrorText(res));
		}
	}

	return NULL;
}

static int open_stream(struct console_pvt *pvt)
{
	int res = paInternalError;

	if (!strcasecmp(pvt->input_device, "default") &&
		!strcasecmp(pvt->output_device, "default")) {
		res = Pa_OpenDefaultStream(&pvt->stream, INPUT_CHANNELS, OUTPUT_CHANNELS,
			paInt16, SAMPLE_RATE, NUM_SAMPLES, NULL, NULL);
	} else {
		PaStreamParameters input_params = {
			.channelCount = 1,
			.sampleFormat = paInt16,
			.suggestedLatency = (1.0 / 50.0), /* 20 ms */
			.device = paNoDevice,
		};
		PaStreamParameters output_params = {
			.channelCount = 1,
			.sampleFormat = paInt16,
			.suggestedLatency = (1.0 / 50.0), /* 20 ms */
			.device = paNoDevice,
		};
		PaDeviceIndex idx, num_devices, def_input, def_output;

		if (!(num_devices = Pa_GetDeviceCount()))
			return res;

		def_input = Pa_GetDefaultInputDevice();
		def_output = Pa_GetDefaultOutputDevice();

		for (idx = 0;
			idx < num_devices && (input_params.device == paNoDevice
				|| output_params.device == paNoDevice);
			idx++)
		{
			const PaDeviceInfo *dev = Pa_GetDeviceInfo(idx);

			if (dev->maxInputChannels) {
				if ( (idx == def_input && !strcasecmp(pvt->input_device, "default")) ||
					!strcasecmp(pvt->input_device, dev->name) )
					input_params.device = idx;
			}

			if (dev->maxOutputChannels) {
				if ( (idx == def_output && !strcasecmp(pvt->output_device, "default")) ||
					!strcasecmp(pvt->output_device, dev->name) )
					output_params.device = idx;
			}
		}

		if (input_params.device == paNoDevice)
			ast_log(LOG_ERROR, "No input device found for console device '%s'\n", pvt->name);
		if (output_params.device == paNoDevice)
			ast_log(LOG_ERROR, "No output device found for console device '%s'\n", pvt->name);

		res = Pa_OpenStream(&pvt->stream, &input_params, &output_params,
			SAMPLE_RATE, NUM_SAMPLES, paNoFlag, NULL, NULL);
	}

	return res;
}

static int start_stream(struct console_pvt *pvt)
{
	PaError res;
	int ret_val = 0;

	console_pvt_lock(pvt);

	/* It is possible for console_hangup to be called before the
	 * stream is started, if this is the case pvt->owner will be NULL
	 * and start_stream should be aborted. */
	if (pvt->streamstate || !pvt->owner)
		goto return_unlock;

	pvt->streamstate = 1;
	ast_debug(1, "Starting stream\n");

	res = open_stream(pvt);
	if (res != paNoError) {
		ast_log(LOG_WARNING, "Failed to open stream - (%d) %s\n",
			res, Pa_GetErrorText(res));
		ret_val = -1;
		goto return_unlock;
	}

	res = Pa_StartStream(pvt->stream);
	if (res != paNoError) {
		ast_log(LOG_WARNING, "Failed to start stream - (%d) %s\n",
			res, Pa_GetErrorText(res));
		ret_val = -1;
		goto return_unlock;
	}

	if (ast_pthread_create_background(&pvt->thread, NULL, stream_monitor, pvt)) {
		ast_log(LOG_ERROR, "Failed to start stream monitor thread\n");
		ret_val = -1;
	}

return_unlock:
	console_pvt_unlock(pvt);

	return ret_val;
}

static int stop_stream(struct console_pvt *pvt)
{
	if (!pvt->streamstate || pvt->thread == AST_PTHREADT_NULL)
		return 0;

	pvt->abort = 1;
	/* Wait for pvt->thread to exit cleanly, to avoid killing it while it's holding a lock. */
	pthread_kill(pvt->thread, SIGURG); /* Wake it up if needed, but don't cancel it */
	pthread_join(pvt->thread, NULL);

	console_pvt_lock(pvt);
	Pa_AbortStream(pvt->stream);
	Pa_CloseStream(pvt->stream);
	pvt->stream = NULL;
	pvt->streamstate = 0;
	console_pvt_unlock(pvt);

	return 0;
}

/*!
 * \note Called with the pvt struct locked
 */
static struct ast_channel *console_new(struct console_pvt *pvt, const char *ext, const char *ctx, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_format_cap *caps;
	struct ast_channel *chan;

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return NULL;
	}

	if (!(chan = ast_channel_alloc(1, state, pvt->cid_num, pvt->cid_name, NULL,
		ext, ctx, assignedids, requestor, 0, "Console/%s", pvt->name))) {
		ao2_ref(caps, -1);
		return NULL;
	}

	ast_channel_stage_snapshot(chan);

	ast_channel_tech_set(chan, &console_tech);
	ast_channel_set_readformat(chan, ast_format_slin16);
	ast_channel_set_writeformat(chan, ast_format_slin16);
	ast_format_cap_append(caps, ast_format_slin16, 0);
	ast_channel_nativeformats_set(chan, caps);
	ao2_ref(caps, -1);
	ast_channel_tech_pvt_set(chan, ref_pvt(pvt));

	pvt->owner = chan;

	if (!ast_strlen_zero(pvt->language))
		ast_channel_language_set(chan, pvt->language);

	ast_jb_configure(chan, &global_jbconf);

	ast_channel_stage_snapshot_done(chan);
	ast_channel_unlock(chan);

	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(chan)) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_SWITCH_CONGESTION);
			ast_hangup(chan);
			chan = NULL;
		} else
			start_stream(pvt);
	}

	return chan;
}

static struct ast_channel *console_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *chan = NULL;
	struct console_pvt *pvt;

	if (!(pvt = find_pvt(data))) {
		ast_log(LOG_ERROR, "Console device '%s' not found\n", data);
		return NULL;
	}

	if (!(ast_format_cap_iscompatible(cap, console_tech.capabilities))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel requested with unsupported format(s): '%s'\n",
			ast_format_cap_get_names(cap, &cap_buf));
		goto return_unref;
	}

	if (pvt->owner) {
		ast_log(LOG_NOTICE, "Console channel already active!\n");
		*cause = AST_CAUSE_BUSY;
		goto return_unref;
	}

	console_pvt_lock(pvt);
	chan = console_new(pvt, NULL, NULL, AST_STATE_DOWN, assignedids, requestor);
	console_pvt_unlock(pvt);

	if (!chan)
		ast_log(LOG_WARNING, "Unable to create new Console channel!\n");

return_unref:
	unref_pvt(pvt);

	return chan;
}

static int console_digit_begin(struct ast_channel *c, char digit)
{
	ast_verb(1, V_BEGIN "Console Received Beginning of Digit %c" V_END, digit);

	return -1; /* non-zero to request inband audio */
}

static int console_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	ast_verb(1, V_BEGIN "Console Received End of Digit %c (duration %u)" V_END,
		digit, duration);

	return -1; /* non-zero to request inband audio */
}

static int console_text(struct ast_channel *c, const char *text)
{
	ast_verb(1, V_BEGIN "Console Received Text '%s'" V_END, text);

	return 0;
}

static int console_hangup(struct ast_channel *c)
{
	struct console_pvt *pvt = ast_channel_tech_pvt(c);

	ast_verb(1, V_BEGIN "Hangup on Console" V_END);

	pvt->hookstate = 0;
	pvt->owner = NULL;
	stop_stream(pvt);

	ast_channel_tech_pvt_set(c, unref_pvt(pvt));

	return 0;
}

static int console_answer(struct ast_channel *c)
{
	struct console_pvt *pvt = ast_channel_tech_pvt(c);

	ast_verb(1, V_BEGIN "Call from Console has been Answered" V_END);

	ast_setstate(c, AST_STATE_UP);

	return start_stream(pvt);
}

/*!
 * \brief Implementation of the ast_channel_tech read() callback
 *
 * Calling this function is harmless.  However, if it does get called, it
 * is an indication that something weird happened that really shouldn't
 * have and is worth looking into.
 *
 * Why should this function not get called?  Well, let me explain.  There are
 * a couple of ways to pass on audio that has come from this channel.  The way
 * that this channel driver uses is that once the audio is available, it is
 * wrapped in an ast_frame and queued onto the channel using ast_queue_frame().
 *
 * The other method would be signalling to the core that there is audio waiting,
 * and that it needs to call the channel's read() callback to get it.  The way
 * the channel gets signalled is that one or more file descriptors are placed
 * in the fds array on the ast_channel which the core will poll() on.  When the
 * fd indicates that input is available, the read() callback is called.  This
 * is especially useful when there is a dedicated file descriptor where the
 * audio is read from.  An example would be the socket for an RTP stream.
 */
static struct ast_frame *console_read(struct ast_channel *chan)
{
	ast_debug(1, "I should not be called ...\n");

	return &ast_null_frame;
}

static int console_call(struct ast_channel *c, const char *dest, int timeout)
{
	struct console_pvt *pvt = ast_channel_tech_pvt(c);
	enum ast_control_frame_type ctrl;

	ast_verb(1, V_BEGIN "Call to device '%s' on console from '%s' <%s>" V_END,
		dest,
		S_COR(ast_channel_caller(c)->id.name.valid, ast_channel_caller(c)->id.name.str, ""),
		S_COR(ast_channel_caller(c)->id.number.valid, ast_channel_caller(c)->id.number.str, ""));

	console_pvt_lock(pvt);

	if (pvt->autoanswer) {
		pvt->hookstate = 1;
		console_pvt_unlock(pvt);
		ast_verb(1, V_BEGIN "Auto-answered" V_END);
		ctrl = AST_CONTROL_ANSWER;
	} else {
		console_pvt_unlock(pvt);
		ast_verb(1, V_BEGIN "Type 'console answer' to answer, or use the 'autoanswer' option "
				"for future calls" V_END);
		ctrl = AST_CONTROL_RINGING;
		ast_indicate(c, AST_CONTROL_RINGING);
	}

	ast_queue_control(c, ctrl);

	return start_stream(pvt);
}

static int console_write(struct ast_channel *chan, struct ast_frame *f)
{
	struct console_pvt *pvt = ast_channel_tech_pvt(chan);

	console_pvt_lock(pvt);
	Pa_WriteStream(pvt->stream, f->data.ptr, f->samples);
	console_pvt_unlock(pvt);

	return 0;
}

static int console_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen)
{
	struct console_pvt *pvt = ast_channel_tech_pvt(chan);
	int res = 0;

	switch (cond) {
	case AST_CONTROL_BUSY:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_INCOMPLETE:
	case AST_CONTROL_PVT_CAUSE_CODE:
	case -1:
		res = -1;  /* Ask for inband indications */
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_SRCUPDATE:
		break;
	case AST_CONTROL_HOLD:
		ast_verb(1, V_BEGIN "Console Has Been Placed on Hold" V_END);
		ast_moh_start(chan, data, pvt->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_verb(1, V_BEGIN "Console Has Been Retrieved from Hold" V_END);
		ast_moh_stop(chan);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n",
			cond, ast_channel_name(chan));
		/* The core will play inband indications for us if appropriate */
		res = -1;
	}

	return res;
}

static int console_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct console_pvt *pvt = ast_channel_tech_pvt(newchan);

	pvt->owner = newchan;

	return 0;
}

/*!
 * split a string in extension-context, returns pointers to malloc'ed
 * strings.
 * If we do not have 'overridecontext' then the last @ is considered as
 * a context separator, and the context is overridden.
 * This is usually not very necessary as you can play with the dialplan,
 * and it is nice not to need it because you have '@' in SIP addresses.
 * Return value is the buffer address.
 *
 * \note came from chan_oss
 */
static char *ast_ext_ctx(struct console_pvt *pvt, const char *src, char **ext, char **ctx)
{
	if (ext == NULL || ctx == NULL)
		return NULL;			/* error */

	*ext = *ctx = NULL;

	if (src && *src != '\0')
		*ext = ast_strdup(src);

	if (*ext == NULL)
		return NULL;

	if (!pvt->overridecontext) {
		/* parse from the right */
		*ctx = strrchr(*ext, '@');
		if (*ctx)
			*(*ctx)++ = '\0';
	}

	return *ext;
}

static struct console_pvt *get_active_pvt(void)
{
	struct console_pvt *pvt;

	ast_rwlock_rdlock(&active_lock);
	pvt = ref_pvt(active_pvt);
	ast_rwlock_unlock(&active_lock);

	return pvt;
}

static char *cli_console_autoanswer(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	struct console_pvt *pvt;
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console {set|show} autoanswer [on|off]";
		e->usage =
			"Usage: console {set|show} autoanswer [on|off]\n"
			"       Enables or disables autoanswer feature.  If used without\n"
			"       argument, displays the current on/off status of autoanswer.\n"
			"       The default value of autoanswer is in 'oss.conf'.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is set as active.\n");
		return CLI_FAILURE;
	}

	if (a->argc == e->args - 1) {
		ast_cli(a->fd, "Auto answer is %s.\n", pvt->autoanswer ? "on" : "off");
		unref_pvt(pvt);
		return CLI_SUCCESS;
	}

	if (a->argc != e->args) {
		unref_pvt(pvt);
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[e->args-1], "on"))
		pvt->autoanswer = 1;
	else if (!strcasecmp(a->argv[e->args - 1], "off"))
		pvt->autoanswer = 0;
	else
		res = CLI_SHOWUSAGE;

	unref_pvt(pvt);

	return res;
}

static char *cli_console_flash(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct console_pvt *pvt;

	if (cmd == CLI_INIT) {
		e->command = "console flash";
		e->usage =
			"Usage: console flash\n"
			"       Flashes the call currently placed on the console.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is set as active\n");
		return CLI_FAILURE;
	}

	if (!pvt->owner) {
		ast_cli(a->fd, "No call to flash\n");
		unref_pvt(pvt);
		return CLI_FAILURE;
	}

	pvt->hookstate = 0;

	ast_queue_control(pvt->owner, AST_CONTROL_FLASH);

	unref_pvt(pvt);

	return CLI_SUCCESS;
}

static char *cli_console_dial(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *s = NULL;
	const char *mye = NULL, *myc = NULL;
	struct console_pvt *pvt;

	if (cmd == CLI_INIT) {
		e->command = "console dial";
		e->usage =
			"Usage: console dial [extension[@context]]\n"
			"       Dials a given extension (and context if specified)\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc > e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is currently set as active\n");
		return CLI_FAILURE;
	}

	if (pvt->owner) {	/* already in a call */
		int i;
		struct ast_frame f = { AST_FRAME_DTMF };
		const char *s;

		if (a->argc == e->args) {	/* argument is mandatory here */
			ast_cli(a->fd, "Already in a call. You can only dial digits until you hangup.\n");
			unref_pvt(pvt);
			return CLI_FAILURE;
		}
		s = a->argv[e->args];
		/* send the string one char at a time */
		for (i = 0; i < strlen(s); i++) {
			f.subclass.integer = s[i];
			ast_queue_frame(pvt->owner, &f);
		}
		unref_pvt(pvt);
		return CLI_SUCCESS;
	}

	/* if we have an argument split it into extension and context */
	if (a->argc == e->args + 1) {
		char *ext = NULL, *con = NULL;
		s = ast_ext_ctx(pvt, a->argv[e->args], &ext, &con);
		mye = ext;
		myc = con;
		ast_debug(1, "provided '%s', exten '%s' context '%s'\n",
			a->argv[e->args], mye, myc);
	}

	/* supply default values if needed */
	if (ast_strlen_zero(mye))
		mye = pvt->exten;
	if (ast_strlen_zero(myc))
		myc = pvt->context;

	if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
		console_pvt_lock(pvt);
		pvt->hookstate = 1;
		console_new(pvt, mye, myc, AST_STATE_RINGING, NULL, NULL);
		console_pvt_unlock(pvt);
	} else
		ast_cli(a->fd, "No such extension '%s' in context '%s'\n", mye, myc);

	ast_free(s);

	unref_pvt(pvt);

	return CLI_SUCCESS;
}

static char *cli_console_hangup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct console_pvt *pvt;

	if (cmd == CLI_INIT) {
		e->command = "console hangup";
		e->usage =
			"Usage: console hangup\n"
			"       Hangs up any call currently placed on the console.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is set as active\n");
		return CLI_FAILURE;
	}

	if (!pvt->owner && !pvt->hookstate) {
		ast_cli(a->fd, "No call to hang up\n");
		unref_pvt(pvt);
		return CLI_FAILURE;
	}

	pvt->hookstate = 0;
	if (pvt->owner)
		ast_queue_hangup(pvt->owner);

	unref_pvt(pvt);

	return CLI_SUCCESS;
}

static char *cli_console_mute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *s;
	struct console_pvt *pvt;
	char *res = CLI_SUCCESS;

	if (cmd == CLI_INIT) {
		e->command = "console {mute|unmute}";
		e->usage =
			"Usage: console {mute|unmute}\n"
			"       Mute/unmute the microphone.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is set as active\n");
		return CLI_FAILURE;
	}

	s = a->argv[e->args-1];
	if (!strcasecmp(s, "mute"))
		pvt->muted = 1;
	else if (!strcasecmp(s, "unmute"))
		pvt->muted = 0;
	else
		res = CLI_SHOWUSAGE;

	ast_verb(1, V_BEGIN "The Console is now %s" V_END,
		pvt->muted ? "Muted" : "Unmuted");

	unref_pvt(pvt);

	return res;
}

static char *cli_list_available(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	PaDeviceIndex idx, num, def_input, def_output;

	if (cmd == CLI_INIT) {
		e->command = "console list available";
		e->usage =
			"Usage: console list available\n"
			"       List all available devices.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Available Devices =======================================\n"
	            "=============================================================\n"
	            "===\n");

	num = Pa_GetDeviceCount();
	if (!num) {
		ast_cli(a->fd, "(None)\n");
		return CLI_SUCCESS;
	}

	def_input = Pa_GetDefaultInputDevice();
	def_output = Pa_GetDefaultOutputDevice();
	for (idx = 0; idx < num; idx++) {
		const PaDeviceInfo *dev = Pa_GetDeviceInfo(idx);
		if (!dev)
			continue;
		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		               "=== Device Name: %s\n", dev->name);
		if (dev->maxInputChannels)
			ast_cli(a->fd, "=== ---> %sInput Device\n", (idx == def_input) ? "Default " : "");
		if (dev->maxOutputChannels)
			ast_cli(a->fd, "=== ---> %sOutput Device\n", (idx == def_output) ? "Default " : "");
		ast_cli(a->fd, "=== ---------------------------------------------------------\n===\n");
	}

	ast_cli(a->fd, "=============================================================\n\n");

	return CLI_SUCCESS;
}

static char *cli_list_devices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct console_pvt *pvt;

	if (cmd == CLI_INIT) {
		e->command = "console list devices";
		e->usage =
			"Usage: console list devices\n"
			"       List all configured devices.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Configured Devices ======================================\n"
	            "=============================================================\n"
	            "===\n");

	i = ao2_iterator_init(pvts, 0);
	while ((pvt = ao2_iterator_next(&i))) {
		console_pvt_lock(pvt);

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		               "=== Device Name: %s\n"
		               "=== ---> Active:           %s\n"
		               "=== ---> Input Device:     %s\n"
		               "=== ---> Output Device:    %s\n"
		               "=== ---> Context:          %s\n"
		               "=== ---> Extension:        %s\n"
		               "=== ---> CallerID Num:     %s\n"
		               "=== ---> CallerID Name:    %s\n"
		               "=== ---> MOH Interpret:    %s\n"
		               "=== ---> Language:         %s\n"
		               "=== ---> Parkinglot:       %s\n"
		               "=== ---> Muted:            %s\n"
		               "=== ---> Auto-Answer:      %s\n"
		               "=== ---> Override Context: %s\n"
		               "=== ---------------------------------------------------------\n===\n",
			pvt->name, (pvt == active_pvt) ? "Yes" : "No",
			pvt->input_device, pvt->output_device, pvt->context,
			pvt->exten, pvt->cid_num, pvt->cid_name, pvt->mohinterpret,
			pvt->language, pvt->parkinglot, pvt->muted ? "Yes" : "No", pvt->autoanswer ? "Yes" : "No",
			pvt->overridecontext ? "Yes" : "No");

		console_pvt_unlock(pvt);
		unref_pvt(pvt);
	}
	ao2_iterator_destroy(&i);

	ast_cli(a->fd, "=============================================================\n\n");

	return CLI_SUCCESS;
}
/*!
 * \brief answer command from the console
 */
static char *cli_console_answer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct console_pvt *pvt;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console answer";
		e->usage =
			"Usage: console answer\n"
			"       Answers an incoming call on the console channel.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is set as active\n");
		return CLI_FAILURE;
	}

	if (a->argc != e->args) {
		unref_pvt(pvt);
		return CLI_SHOWUSAGE;
	}

	if (!pvt->owner) {
		ast_cli(a->fd, "No one is calling us\n");
		unref_pvt(pvt);
		return CLI_FAILURE;
	}

	pvt->hookstate = 1;

	ast_indicate(pvt->owner, -1);

	ast_queue_control(pvt->owner, AST_CONTROL_ANSWER);

	unref_pvt(pvt);

	return CLI_SUCCESS;
}

/*!
 * \brief Console send text CLI command
 *
 * \note concatenate all arguments into a single string. argv is NULL-terminated
 * so we can use it right away
 */
static char *cli_console_sendtext(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char buf[TEXT_SIZE];
	struct console_pvt *pvt;
	struct ast_frame f = {
		.frametype = AST_FRAME_TEXT,
		.data.ptr = buf,
		.src = "console_send_text",
	};
	int len;

	if (cmd == CLI_INIT) {
		e->command = "console send text";
		e->usage =
			"Usage: console send text <message>\n"
			"       Sends a text message for display on the remote terminal.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	pvt = get_active_pvt();
	if (!pvt) {
		ast_cli(a->fd, "No console device is set as active\n");
		return CLI_FAILURE;
	}

	if (a->argc < e->args + 1) {
		unref_pvt(pvt);
		return CLI_SHOWUSAGE;
	}

	if (!pvt->owner) {
		ast_cli(a->fd, "Not in a call\n");
		unref_pvt(pvt);
		return CLI_FAILURE;
	}

	ast_join(buf, sizeof(buf) - 1, a->argv + e->args);
	if (ast_strlen_zero(buf)) {
		unref_pvt(pvt);
		return CLI_SHOWUSAGE;
	}

	len = strlen(buf);
	buf[len] = '\n';
	f.datalen = len + 1;

	ast_queue_frame(pvt->owner, &f);

	unref_pvt(pvt);

	return CLI_SUCCESS;
}

static void set_active(struct console_pvt *pvt, const char *value)
{
	if (pvt == &globals) {
		ast_log(LOG_ERROR, "active is only valid as a per-device setting\n");
		return;
	}

	if (!ast_true(value))
		return;

	ast_rwlock_wrlock(&active_lock);
	if (active_pvt)
		unref_pvt(active_pvt);
	active_pvt = ref_pvt(pvt);
	ast_rwlock_unlock(&active_lock);
}

static char *cli_console_active(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct console_pvt *pvt;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console {set|show} active";
		e->usage =
			"Usage: console {set|show} active [<device>]\n"
			"       Set or show the active console device for the Asterisk CLI.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == e->args) {
			struct ao2_iterator i;
			int x = 0;
			char *res = NULL;
			i = ao2_iterator_init(pvts, 0);
			while ((pvt = ao2_iterator_next(&i))) {
				if (++x > a->n && !strncasecmp(pvt->name, a->word, strlen(a->word)))
					res = ast_strdup(pvt->name);
				unref_pvt(pvt);
				if (res) {
					ao2_iterator_destroy(&i);
					return res;
				}
			}
			ao2_iterator_destroy(&i);
		}
		return NULL;
	}

	if (a->argc < e->args)
		return CLI_SHOWUSAGE;

	if (a->argc == 3) {
		pvt = get_active_pvt();

		if (!pvt)
			ast_cli(a->fd, "No device is currently set as the active console device.\n");
		else {
			console_pvt_lock(pvt);
			ast_cli(a->fd, "The active console device is '%s'.\n", pvt->name);
			console_pvt_unlock(pvt);
			pvt = unref_pvt(pvt);
		}

		return CLI_SUCCESS;
	}

	if (!(pvt = find_pvt(a->argv[e->args]))) {
		ast_cli(a->fd, "Could not find a device called '%s'.\n", a->argv[e->args]);
		return CLI_FAILURE;
	}

	set_active(pvt, "yes");

	console_pvt_lock(pvt);
	ast_cli(a->fd, "The active console device has been set to '%s'\n", pvt->name);
	console_pvt_unlock(pvt);

	unref_pvt(pvt);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_console[] = {
	AST_CLI_DEFINE(cli_console_dial,       "Dial an extension from the console"),
	AST_CLI_DEFINE(cli_console_hangup,     "Hangup a call on the console"),
	AST_CLI_DEFINE(cli_console_mute,       "Disable/Enable mic input"),
	AST_CLI_DEFINE(cli_console_answer,     "Answer an incoming console call"),
	AST_CLI_DEFINE(cli_console_sendtext,   "Send text to a connected party"),
	AST_CLI_DEFINE(cli_console_flash,      "Send a flash to the connected party"),
	AST_CLI_DEFINE(cli_console_autoanswer, "Turn autoanswer on or off"),
	AST_CLI_DEFINE(cli_list_available,     "List available devices"),
	AST_CLI_DEFINE(cli_list_devices,       "List configured devices"),
	AST_CLI_DEFINE(cli_console_active,     "View or Set the active console device"),
};

/*!
 * \brief Set default values for a pvt struct
 *
 * \note This function expects the pvt lock to be held.
 */
static void set_pvt_defaults(struct console_pvt *pvt)
{
	if (pvt == &globals) {
		ast_string_field_set(pvt, mohinterpret, "default");
		ast_string_field_set(pvt, context, "default");
		ast_string_field_set(pvt, exten, "s");
		ast_string_field_set(pvt, language, "");
		ast_string_field_set(pvt, cid_num, "");
		ast_string_field_set(pvt, cid_name, "");
		ast_string_field_set(pvt, parkinglot, "");

		pvt->overridecontext = 0;
		pvt->autoanswer = 0;
	} else {
		ast_mutex_lock(&globals_lock);

		ast_string_field_set(pvt, mohinterpret, globals.mohinterpret);
		ast_string_field_set(pvt, context, globals.context);
		ast_string_field_set(pvt, exten, globals.exten);
		ast_string_field_set(pvt, language, globals.language);
		ast_string_field_set(pvt, cid_num, globals.cid_num);
		ast_string_field_set(pvt, cid_name, globals.cid_name);
		ast_string_field_set(pvt, parkinglot, globals.parkinglot);

		pvt->overridecontext = globals.overridecontext;
		pvt->autoanswer = globals.autoanswer;

		ast_mutex_unlock(&globals_lock);
	}
}

static void store_callerid(struct console_pvt *pvt, const char *value)
{
	char cid_name[256];
	char cid_num[256];

	ast_callerid_split(value, cid_name, sizeof(cid_name),
		cid_num, sizeof(cid_num));

	ast_string_field_set(pvt, cid_name, cid_name);
	ast_string_field_set(pvt, cid_num, cid_num);
}

/*!
 * \brief Store a configuration parameter in a pvt struct
 *
 * \note This function expects the pvt lock to be held.
 */
static void store_config_core(struct console_pvt *pvt, const char *var, const char *value)
{
	if (pvt == &globals && !ast_jb_read_conf(&global_jbconf, var, value))
		return;

	CV_START(var, value);

	CV_STRFIELD("context", pvt, context);
	CV_STRFIELD("extension", pvt, exten);
	CV_STRFIELD("mohinterpret", pvt, mohinterpret);
	CV_STRFIELD("language", pvt, language);
	CV_F("callerid", store_callerid(pvt, value));
	CV_BOOL("overridecontext", pvt->overridecontext);
	CV_BOOL("autoanswer", pvt->autoanswer);
	CV_STRFIELD("parkinglot", pvt, parkinglot);

	if (pvt != &globals) {
		CV_F("active", set_active(pvt, value))
		CV_STRFIELD("input_device", pvt, input_device);
		CV_STRFIELD("output_device", pvt, output_device);
	}

	ast_log(LOG_WARNING, "Unknown option '%s'\n", var);

	CV_END;
}

static void pvt_destructor(void *obj)
{
	struct console_pvt *pvt = obj;

	ast_string_field_free_memory(pvt);
}

static int init_pvt(struct console_pvt *pvt, const char *name)
{
	pvt->thread = AST_PTHREADT_NULL;

	if (ast_string_field_init(pvt, 32))
		return -1;

	ast_string_field_set(pvt, name, S_OR(name, ""));

	return 0;
}

static void build_device(struct ast_config *cfg, const char *name)
{
	struct ast_variable *v;
	struct console_pvt *pvt;
	int new = 0;

	if ((pvt = find_pvt(name))) {
		console_pvt_lock(pvt);
		set_pvt_defaults(pvt);
		pvt->destroy = 0;
	} else {
		if (!(pvt = ao2_alloc(sizeof(*pvt), pvt_destructor)))
			return;
		init_pvt(pvt, name);
		set_pvt_defaults(pvt);
		new = 1;
	}

	for (v = ast_variable_browse(cfg, name); v; v = v->next)
		store_config_core(pvt, v->name, v->value);

	if (new)
		ao2_link(pvts, pvt);
	else
		console_pvt_unlock(pvt);

	unref_pvt(pvt);
}

static int pvt_mark_destroy_cb(void *obj, void *arg, int flags)
{
	struct console_pvt *pvt = obj;
	pvt->destroy = 1;
	return 0;
}

static void destroy_pvts(void)
{
	struct ao2_iterator i;
	struct console_pvt *pvt;

	i = ao2_iterator_init(pvts, 0);
	while ((pvt = ao2_iterator_next(&i))) {
		if (pvt->destroy) {
			ao2_unlink(pvts, pvt);
			ast_rwlock_wrlock(&active_lock);
			if (active_pvt == pvt)
				active_pvt = unref_pvt(pvt);
			ast_rwlock_unlock(&active_lock);
		}
		unref_pvt(pvt);
	}
	ao2_iterator_destroy(&i);
}

/*!
 * \brief Load the configuration
 * \param reload if this was called due to a reload
 * \retval 0 success
 * \retval -1 failure
 */
static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { 0 };
	char *context = NULL;

	/* default values */
	memcpy(&global_jbconf, &default_jbconf, sizeof(global_jbconf));
	ast_mutex_lock(&globals_lock);
	set_pvt_defaults(&globals);
	ast_mutex_unlock(&globals_lock);

	if (!(cfg = ast_config_load(config_file, config_flags))) {
		ast_log(LOG_NOTICE, "Unable to open configuration file %s!\n", config_file);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_NOTICE, "Config file %s has an invalid format\n", config_file);
		return -1;
	}

	ao2_callback(pvts, OBJ_NODATA, pvt_mark_destroy_cb, NULL);

	ast_mutex_lock(&globals_lock);
	for (v = ast_variable_browse(cfg, "general"); v; v = v->next)
		store_config_core(&globals, v->name, v->value);
	ast_mutex_unlock(&globals_lock);

	while ((context = ast_category_browse(cfg, context))) {
		if (strcasecmp(context, "general"))
			build_device(cfg, context);
	}

	ast_config_destroy(cfg);

	destroy_pvts();

	return 0;
}

static int pvt_hash_cb(const void *obj, const int flags)
{
	const struct console_pvt *pvt = obj;

	return ast_str_case_hash(pvt->name);
}

static int pvt_cmp_cb(void *obj, void *arg, int flags)
{
	struct console_pvt *pvt = obj, *pvt2 = arg;

	return !strcasecmp(pvt->name, pvt2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static void stop_streams(void)
{
	struct console_pvt *pvt;
	struct ao2_iterator i;

	i = ao2_iterator_init(pvts, 0);
	while ((pvt = ao2_iterator_next(&i))) {
		if (pvt->hookstate)
			stop_stream(pvt);
		unref_pvt(pvt);
	}
	ao2_iterator_destroy(&i);
}

static int unload_module(void)
{
	ao2_ref(console_tech.capabilities, -1);
	console_tech.capabilities = NULL;
	ast_channel_unregister(&console_tech);
	ast_cli_unregister_multiple(cli_console, ARRAY_LEN(cli_console));

	stop_streams();

	Pa_Terminate();

	/* Will unref all the pvts so they will get destroyed, too */
	ao2_ref(pvts, -1);

	pvt_destructor(&globals);

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	PaError res;

	if (!(console_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(console_tech.capabilities, ast_format_slin16, 0);

	init_pvt(&globals, NULL);

	pvts = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NUM_PVT_BUCKETS,
		pvt_hash_cb, NULL, pvt_cmp_cb);
	if (!pvts)
		goto return_error;

	if (load_config(0))
		goto return_error;

	res = Pa_Initialize();
	if (res != paNoError) {
		ast_log(LOG_WARNING, "Failed to initialize audio system - (%d) %s\n",
			res, Pa_GetErrorText(res));
		goto return_error_pa_init;
	}

	if (ast_channel_register(&console_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'Console'\n");
		goto return_error_chan_reg;
	}

	if (ast_cli_register_multiple(cli_console, ARRAY_LEN(cli_console)))
		goto return_error_cli_reg;

	return AST_MODULE_LOAD_SUCCESS;

return_error_cli_reg:
	ast_cli_unregister_multiple(cli_console, ARRAY_LEN(cli_console));
return_error_chan_reg:
	ast_channel_unregister(&console_tech);
return_error_pa_init:
	Pa_Terminate();
return_error:
	if (pvts)
		ao2_ref(pvts, -1);
	pvts = NULL;
	ao2_ref(console_tech.capabilities, -1);
	console_tech.capabilities = NULL;
	pvt_destructor(&globals);

	return AST_MODULE_LOAD_DECLINE;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Console Channel Driver",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
