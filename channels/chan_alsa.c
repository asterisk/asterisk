/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * By Matthew Fredrickson <creslin@digium.com>
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
 * \brief ALSA sound card channel driver
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 *
 * \ingroup channel_drivers
 */

/*! \li \ref chan_alsa.c uses the configuration file \ref alsa.conf
 * \addtogroup configuration_file
 */

/*! \page alsa.conf alsa.conf
 * \verbinclude alsa.conf.sample
 */

/*** MODULEINFO
	<depend>alsa</depend>
	<support_level>extended</support_level>
	<deprecated_in>19</deprecated_in>
	<removed_in>21</removed_in>
 ***/

#include "asterisk.h"

#include <errno.h>
#ifndef ESTRPIPE
#define ESTRPIPE EPIPE
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/endian.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/poll-compat.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/format_cache.h"

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in alsa.conf.sample */
static struct ast_jb_conf default_jbconf = {
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};
static struct ast_jb_conf global_jbconf;

#define DEBUG 0
/* Which device to use */
#define ALSA_INDEV "default"
#define ALSA_OUTDEV "default"
#define DESIRED_RATE 8000

/* Lets use 160 sample frames, just like GSM.  */
#define FRAME_SIZE 160
#define PERIOD_FRAMES 80		/* 80 Frames, at 2 bytes each */

/* When you set the frame size, you have to come up with
   the right buffer format as well. */
/* 5 64-byte frames = one frame */
#define BUFFER_FMT ((buffersize * 10) << 16) | (0x0006);

/* Don't switch between read/write modes faster than every 300 ms */
#define MIN_SWITCH_TIME 600

#if __BYTE_ORDER == __LITTLE_ENDIAN
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
#else
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_BE;
#endif

static char indevname[50] = ALSA_INDEV;
static char outdevname[50] = ALSA_OUTDEV;

static int silencesuppression = 0;
static int silencethreshold = 1000;

AST_MUTEX_DEFINE_STATIC(alsalock);

static const char tdesc[] = "ALSA Console Channel Driver";
static const char config[] = "alsa.conf";

static char context[AST_MAX_CONTEXT] = "default";
static char language[MAX_LANGUAGE] = "";
static char exten[AST_MAX_EXTENSION] = "s";
static char mohinterpret[MAX_MUSICCLASS];

static int hookstate = 0;

static struct chan_alsa_pvt {
	/* We only have one ALSA structure -- near sighted perhaps, but it
	   keeps this driver as simple as possible -- as it should be. */
	struct ast_channel *owner;
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_CONTEXT];
	snd_pcm_t *icard, *ocard;

} alsa;

/* Number of buffers...  Each is FRAMESIZE/8 ms long.  For example
   with 160 sample frames, and a buffer size of 3, we have a 60ms buffer,
   usually plenty. */

#define MAX_BUFFER_SIZE 100

/* File descriptors for sound device */
static int readdev = -1;
static int writedev = -1;

static int autoanswer = 1;
static int mute = 0;
static int noaudiocapture = 0;

static struct ast_channel *alsa_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int alsa_digit(struct ast_channel *c, char digit, unsigned int duration);
static int alsa_text(struct ast_channel *c, const char *text);
static int alsa_hangup(struct ast_channel *c);
static int alsa_answer(struct ast_channel *c);
static struct ast_frame *alsa_read(struct ast_channel *chan);
static int alsa_call(struct ast_channel *c, const char *dest, int timeout);
static int alsa_write(struct ast_channel *chan, struct ast_frame *f);
static int alsa_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int alsa_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static struct ast_channel_tech alsa_tech = {
	.type = "Console",
	.description = tdesc,
	.requester = alsa_request,
	.send_digit_end = alsa_digit,
	.send_text = alsa_text,
	.hangup = alsa_hangup,
	.answer = alsa_answer,
	.read = alsa_read,
	.call = alsa_call,
	.write = alsa_write,
	.indicate = alsa_indicate,
	.fixup = alsa_fixup,
};

static snd_pcm_t *alsa_card_init(char *dev, snd_pcm_stream_t stream)
{
	int err;
	int direction;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *hwparams = NULL;
	snd_pcm_sw_params_t *swparams = NULL;
	struct pollfd pfd;
	snd_pcm_uframes_t period_size = PERIOD_FRAMES * 4;
	snd_pcm_uframes_t buffer_size = 0;
	unsigned int rate = DESIRED_RATE;
	snd_pcm_uframes_t start_threshold, stop_threshold;

	err = snd_pcm_open(&handle, dev, stream, SND_PCM_NONBLOCK);
	if (err < 0) {
		ast_log(LOG_ERROR, "snd_pcm_open failed: %s\n", snd_strerror(err));
		return NULL;
	} else {
		ast_debug(1, "Opening device %s in %s mode\n", dev, (stream == SND_PCM_STREAM_CAPTURE) ? "read" : "write");
	}

	hwparams = ast_alloca(snd_pcm_hw_params_sizeof());
	memset(hwparams, 0, snd_pcm_hw_params_sizeof());
	snd_pcm_hw_params_any(handle, hwparams);

	err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
		ast_log(LOG_ERROR, "set_access failed: %s\n", snd_strerror(err));

	err = snd_pcm_hw_params_set_format(handle, hwparams, format);
	if (err < 0)
		ast_log(LOG_ERROR, "set_format failed: %s\n", snd_strerror(err));

	err = snd_pcm_hw_params_set_channels(handle, hwparams, 1);
	if (err < 0)
		ast_log(LOG_ERROR, "set_channels failed: %s\n", snd_strerror(err));

	direction = 0;
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &rate, &direction);
	if (rate != DESIRED_RATE)
		ast_log(LOG_WARNING, "Rate not correct, requested %d, got %u\n", DESIRED_RATE, rate);

	direction = 0;
	err = snd_pcm_hw_params_set_period_size_near(handle, hwparams, &period_size, &direction);
	if (err < 0)
		ast_log(LOG_ERROR, "period_size(%lu frames) is bad: %s\n", period_size, snd_strerror(err));
	else {
		ast_debug(1, "Period size is %d\n", err);
	}

	buffer_size = 4096 * 2;		/* period_size * 16; */
	err = snd_pcm_hw_params_set_buffer_size_near(handle, hwparams, &buffer_size);
	if (err < 0)
		ast_log(LOG_WARNING, "Problem setting buffer size of %lu: %s\n", buffer_size, snd_strerror(err));
	else {
		ast_debug(1, "Buffer size is set to %d frames\n", err);
	}

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0)
		ast_log(LOG_ERROR, "Couldn't set the new hw params: %s\n", snd_strerror(err));

	swparams = ast_alloca(snd_pcm_sw_params_sizeof());
	memset(swparams, 0, snd_pcm_sw_params_sizeof());
	snd_pcm_sw_params_current(handle, swparams);

	if (stream == SND_PCM_STREAM_PLAYBACK)
		start_threshold = period_size;
	else
		start_threshold = 1;

	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	if (err < 0)
		ast_log(LOG_ERROR, "start threshold: %s\n", snd_strerror(err));

	if (stream == SND_PCM_STREAM_PLAYBACK)
		stop_threshold = buffer_size;
	else
		stop_threshold = buffer_size;

	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	if (err < 0)
		ast_log(LOG_ERROR, "stop threshold: %s\n", snd_strerror(err));

	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0)
		ast_log(LOG_ERROR, "sw_params: %s\n", snd_strerror(err));

	err = snd_pcm_poll_descriptors_count(handle);
	if (err <= 0)
		ast_log(LOG_ERROR, "Unable to get a poll descriptors count, error is %s\n", snd_strerror(err));
	if (err != 1) {
		ast_debug(1, "Can't handle more than one device\n");
	}

	snd_pcm_poll_descriptors(handle, &pfd, err);
	ast_debug(1, "Acquired fd %d from the poll descriptor\n", pfd.fd);

	if (stream == SND_PCM_STREAM_CAPTURE)
		readdev = pfd.fd;
	else
		writedev = pfd.fd;

	return handle;
}

static int soundcard_init(void)
{
	if (!noaudiocapture) {
		alsa.icard = alsa_card_init(indevname, SND_PCM_STREAM_CAPTURE);
		if (!alsa.icard) {
			ast_log(LOG_ERROR, "Problem opening alsa capture device\n");
			return -1;
		}
	}

	alsa.ocard = alsa_card_init(outdevname, SND_PCM_STREAM_PLAYBACK);

	if (!alsa.ocard) {
		ast_log(LOG_ERROR, "Problem opening ALSA playback device\n");
		return -1;
	}

	return writedev;
}

static int alsa_digit(struct ast_channel *c, char digit, unsigned int duration)
{
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n",
		digit, duration);
	ast_mutex_unlock(&alsalock);

	return 0;
}

static int alsa_text(struct ast_channel *c, const char *text)
{
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Console Received text %s >> \n", text);
	ast_mutex_unlock(&alsalock);

	return 0;
}

static void grab_owner(void)
{
	while (alsa.owner && ast_channel_trylock(alsa.owner)) {
		DEADLOCK_AVOIDANCE(&alsalock);
	}
}

static int alsa_call(struct ast_channel *c, const char *dest, int timeout)
{
	struct ast_frame f = { AST_FRAME_CONTROL };

	ast_mutex_lock(&alsalock);
	ast_verbose(" << Call placed to '%s' on console >> \n", dest);
	if (autoanswer) {
		ast_verbose(" << Auto-answered >> \n");
		if (mute) {
			ast_verbose( " << Muted >> \n" );
		}
		grab_owner();
		if (alsa.owner) {
			f.subclass.integer = AST_CONTROL_ANSWER;
			ast_queue_frame(alsa.owner, &f);
			ast_channel_unlock(alsa.owner);
		}
	} else {
		ast_verbose(" << Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
		grab_owner();
		if (alsa.owner) {
			f.subclass.integer = AST_CONTROL_RINGING;
			ast_queue_frame(alsa.owner, &f);
			ast_channel_unlock(alsa.owner);
			ast_indicate(alsa.owner, AST_CONTROL_RINGING);
		}
	}
	if (!noaudiocapture) {
		snd_pcm_prepare(alsa.icard);
		snd_pcm_start(alsa.icard);
	}
	ast_mutex_unlock(&alsalock);

	return 0;
}

static int alsa_answer(struct ast_channel *c)
{
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Console call has been answered >> \n");
	ast_setstate(c, AST_STATE_UP);
	if (!noaudiocapture) {
		snd_pcm_prepare(alsa.icard);
		snd_pcm_start(alsa.icard);
	}
	ast_mutex_unlock(&alsalock);

	return 0;
}

static int alsa_hangup(struct ast_channel *c)
{
	ast_mutex_lock(&alsalock);
	ast_channel_tech_pvt_set(c, NULL);
	alsa.owner = NULL;
	ast_verbose(" << Hangup on console >> \n");
	ast_module_unref(ast_module_info->self);
	hookstate = 0;
	if (!noaudiocapture) {
		snd_pcm_drop(alsa.icard);
	}
	ast_mutex_unlock(&alsalock);

	return 0;
}

static int alsa_write(struct ast_channel *chan, struct ast_frame *f)
{
	static char sizbuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int res = 0;
	/* size_t frames = 0; */
	snd_pcm_state_t state;

	ast_mutex_lock(&alsalock);

	/* We have to digest the frame in 160-byte portions */
	if (f->datalen > sizeof(sizbuf) - sizpos) {
		ast_log(LOG_WARNING, "Frame too large\n");
		res = -1;
	} else {
		memcpy(sizbuf + sizpos, f->data.ptr, f->datalen);
		len += f->datalen;
		state = snd_pcm_state(alsa.ocard);
		if (state == SND_PCM_STATE_XRUN)
			snd_pcm_prepare(alsa.ocard);
		while ((res = snd_pcm_writei(alsa.ocard, sizbuf, len / 2)) == -EAGAIN) {
			usleep(1);
		}
		if (res == -EPIPE) {
#if DEBUG
			ast_debug(1, "XRUN write\n");
#endif
			snd_pcm_prepare(alsa.ocard);
			while ((res = snd_pcm_writei(alsa.ocard, sizbuf, len / 2)) == -EAGAIN) {
				usleep(1);
			}
			if (res != len / 2) {
				ast_log(LOG_ERROR, "Write error: %s\n", snd_strerror(res));
				res = -1;
			} else if (res < 0) {
				ast_log(LOG_ERROR, "Write error %s\n", snd_strerror(res));
				res = -1;
			}
		} else {
			if (res == -ESTRPIPE)
				ast_log(LOG_ERROR, "You've got some big problems\n");
			else if (res < 0)
				ast_log(LOG_NOTICE, "Error %d on write\n", res);
		}
	}
	ast_mutex_unlock(&alsalock);

	return res >= 0 ? 0 : res;
}


static struct ast_frame *alsa_read(struct ast_channel *chan)
{
	static struct ast_frame f;
	static short __buf[FRAME_SIZE + AST_FRIENDLY_OFFSET / 2];
	short *buf;
	static int readpos = 0;
	static int left = FRAME_SIZE;
	snd_pcm_state_t state;
	int r = 0;

	ast_mutex_lock(&alsalock);
	f.frametype = AST_FRAME_NULL;
	f.subclass.integer = 0;
	f.samples = 0;
	f.datalen = 0;
	f.data.ptr = NULL;
	f.offset = 0;
	f.src = "Console";
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	if (noaudiocapture) {
		/* Return null frame to asterisk*/
		ast_mutex_unlock(&alsalock);
		return &f;
	}

	state = snd_pcm_state(alsa.icard);
	if ((state != SND_PCM_STATE_PREPARED) && (state != SND_PCM_STATE_RUNNING)) {
		snd_pcm_prepare(alsa.icard);
	}

	buf = __buf + AST_FRIENDLY_OFFSET / 2;

	r = snd_pcm_readi(alsa.icard, buf + readpos, left);
	if (r == -EPIPE) {
#if DEBUG
		ast_log(LOG_ERROR, "XRUN read\n");
#endif
		snd_pcm_prepare(alsa.icard);
	} else if (r == -ESTRPIPE) {
		ast_log(LOG_ERROR, "-ESTRPIPE\n");
		snd_pcm_prepare(alsa.icard);
	} else if (r < 0) {
		ast_log(LOG_ERROR, "Read error: %s\n", snd_strerror(r));
	}

	/* Return NULL frame on error */
	if (r < 0) {
		ast_mutex_unlock(&alsalock);
		return &f;
	}

	/* Update positions */
	readpos += r;
	left -= r;

	if (readpos >= FRAME_SIZE) {
		/* A real frame */
		readpos = 0;
		left = FRAME_SIZE;
		if (ast_channel_state(chan) != AST_STATE_UP) {
			/* Don't transmit unless it's up */
			ast_mutex_unlock(&alsalock);
			return &f;
		}
		if (mute) {
			/* Don't transmit if muted */
			ast_mutex_unlock(&alsalock);
			return &f;
		}

		f.frametype = AST_FRAME_VOICE;
		f.subclass.format = ast_format_slin;
		f.samples = FRAME_SIZE;
		f.datalen = FRAME_SIZE * 2;
		f.data.ptr = buf;
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = "Console";
		f.mallocd = 0;

	}
	ast_mutex_unlock(&alsalock);

	return &f;
}

static int alsa_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_alsa_pvt *p = ast_channel_tech_pvt(newchan);

	ast_mutex_lock(&alsalock);
	p->owner = newchan;
	ast_mutex_unlock(&alsalock);

	return 0;
}

static int alsa_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen)
{
	int res = 0;

	ast_mutex_lock(&alsalock);

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
		ast_verbose(" << Console Has Been Placed on Hold >> \n");
		ast_moh_start(chan, data, mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_verbose(" << Console Has Been Retrieved from Hold >> \n");
		ast_moh_stop(chan);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, ast_channel_name(chan));
		res = -1;
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static struct ast_channel *alsa_new(struct chan_alsa_pvt *p, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *tmp = NULL;

	if (!(tmp = ast_channel_alloc(1, state, 0, 0, "", p->exten, p->context, assignedids, requestor, 0, "ALSA/%s", indevname)))
		return NULL;

	ast_channel_stage_snapshot(tmp);

	ast_channel_tech_set(tmp, &alsa_tech);
	ast_channel_set_fd(tmp, 0, readdev);
	ast_channel_set_readformat(tmp, ast_format_slin);
	ast_channel_set_writeformat(tmp, ast_format_slin);
	ast_channel_nativeformats_set(tmp, alsa_tech.capabilities);

	ast_channel_tech_pvt_set(tmp, p);
	if (!ast_strlen_zero(p->context))
		ast_channel_context_set(tmp, p->context);
	if (!ast_strlen_zero(p->exten))
		ast_channel_exten_set(tmp, p->exten);
	if (!ast_strlen_zero(language))
		ast_channel_language_set(tmp, language);
	p->owner = tmp;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(tmp, &global_jbconf);

	ast_channel_stage_snapshot_done(tmp);
	ast_channel_unlock(tmp);

	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
			ast_hangup(tmp);
			tmp = NULL;
		}
	}

	return tmp;
}

static struct ast_channel *alsa_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *tmp = NULL;

	if (ast_format_cap_iscompatible_format(cap, ast_format_slin) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Asked to get a channel of format '%s'\n", ast_format_cap_get_names(cap, &codec_buf));
		return NULL;
	}

	ast_mutex_lock(&alsalock);

	if (alsa.owner) {
		ast_log(LOG_NOTICE, "Already have a call on the ALSA channel\n");
		*cause = AST_CAUSE_BUSY;
	} else if (!(tmp = alsa_new(&alsa, AST_STATE_DOWN, assignedids, requestor))) {
		ast_log(LOG_WARNING, "Unable to create new ALSA channel\n");
	}

	ast_mutex_unlock(&alsalock);

	return tmp;
}

static char *console_autoanswer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console autoanswer [on|off]";
		e->usage =
			"Usage: console autoanswer [on|off]\n"
			"       Enables or disables autoanswer feature.  If used without\n"
			"       argument, displays the current on/off status of autoanswer.\n"
			"       The default value of autoanswer is in 'alsa.conf'.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc != 2) && (a->argc != 3))
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&alsalock);
	if (a->argc == 2) {
		ast_cli(a->fd, "Auto answer is %s.\n", autoanswer ? "on" : "off");
	} else {
		if (!strcasecmp(a->argv[2], "on"))
			autoanswer = -1;
		else if (!strcasecmp(a->argv[2], "off"))
			autoanswer = 0;
		else
			res = CLI_SHOWUSAGE;
	}
	ast_mutex_unlock(&alsalock);

	return res;
}

static char *console_answer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console answer";
		e->usage =
			"Usage: console answer\n"
			"       Answers an incoming call on the console (ALSA) channel.\n";

		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner) {
		ast_cli(a->fd, "No one is calling us\n");
		res = CLI_FAILURE;
	} else {
		if (mute) {
			ast_verbose( " << Muted >> \n" );
		}
		hookstate = 1;
		grab_owner();
		if (alsa.owner) {
			ast_queue_control(alsa.owner, AST_CONTROL_ANSWER);
			ast_channel_unlock(alsa.owner);
		}
	}

	if (!noaudiocapture) {
		snd_pcm_prepare(alsa.icard);
		snd_pcm_start(alsa.icard);
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char *console_sendtext(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int tmparg = 3;
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console send text";
		e->usage =
			"Usage: console send text <message>\n"
			"       Sends a text message for display on the remote terminal.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner) {
		ast_cli(a->fd, "No channel active\n");
		res = CLI_FAILURE;
	} else {
		struct ast_frame f = { AST_FRAME_TEXT };
		char text2send[256] = "";

		while (tmparg < a->argc) {
			strncat(text2send, a->argv[tmparg++], sizeof(text2send) - strlen(text2send) - 1);
			strncat(text2send, " ", sizeof(text2send) - strlen(text2send) - 1);
		}

		text2send[strlen(text2send) - 1] = '\n';
		f.data.ptr = text2send;
		f.datalen = strlen(text2send) + 1;
		grab_owner();
		if (alsa.owner) {
			ast_queue_frame(alsa.owner, &f);
			ast_queue_control(alsa.owner, AST_CONTROL_ANSWER);
			ast_channel_unlock(alsa.owner);
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char *console_hangup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console hangup";
		e->usage =
			"Usage: console hangup\n"
			"       Hangs up any call currently placed on the console.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}


	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner && !hookstate) {
		ast_cli(a->fd, "No call to hangup\n");
		res = CLI_FAILURE;
	} else {
		hookstate = 0;
		grab_owner();
		if (alsa.owner) {
			ast_queue_hangup_with_cause(alsa.owner, AST_CAUSE_NORMAL_CLEARING);
			ast_channel_unlock(alsa.owner);
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char *console_dial(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char tmp[256], *tmp2;
	char *mye, *myc;
	const char *d;
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console dial";
		e->usage =
			"Usage: console dial [extension[@context]]\n"
			"       Dials a given extension (and context if specified)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc != 2) && (a->argc != 3))
		return CLI_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (alsa.owner) {
		if (a->argc == 3) {
			if (alsa.owner) {
				for (d = a->argv[2]; *d; d++) {
					struct ast_frame f = { .frametype = AST_FRAME_DTMF, .subclass.integer = *d };

					ast_queue_frame(alsa.owner, &f);
				}
			}
		} else {
			ast_cli(a->fd, "You're already in a call.  You can use this only to dial digits until you hangup\n");
			res = CLI_FAILURE;
		}
	} else {
		mye = exten;
		myc = context;
		if (a->argc == 3) {
			char *stringp = NULL;

			ast_copy_string(tmp, a->argv[2], sizeof(tmp));
			stringp = tmp;
			strsep(&stringp, "@");
			tmp2 = strsep(&stringp, "@");
			if (!ast_strlen_zero(tmp))
				mye = tmp;
			if (!ast_strlen_zero(tmp2))
				myc = tmp2;
		}
		if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
			ast_copy_string(alsa.exten, mye, sizeof(alsa.exten));
			ast_copy_string(alsa.context, myc, sizeof(alsa.context));
			hookstate = 1;
			alsa_new(&alsa, AST_STATE_RINGING, NULL, NULL);
		} else
			ast_cli(a->fd, "No such extension '%s' in context '%s'\n", mye, myc);
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char *console_mute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int toggle = 0;
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console {mute|unmute} [toggle]";
		e->usage =
			"Usage: console {mute|unmute} [toggle]\n"
			"       Mute/unmute the microphone.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}


	if (a->argc > 3) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 3) {
		if (strcasecmp(a->argv[2], "toggle"))
			return CLI_SHOWUSAGE;
		toggle = 1;
	}

	if (a->argc < 2) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[1], "mute")) {
		mute = toggle ? !mute : 1;
	} else if (!strcasecmp(a->argv[1], "unmute")) {
		mute = toggle ? !mute : 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "Console mic is %s\n", mute ? "off" : "on");

	return res;
}

static struct ast_cli_entry cli_alsa[] = {
	AST_CLI_DEFINE(console_answer, "Answer an incoming console call"),
	AST_CLI_DEFINE(console_hangup, "Hangup a call on the console"),
	AST_CLI_DEFINE(console_dial, "Dial an extension on the console"),
	AST_CLI_DEFINE(console_sendtext, "Send text to the remote device"),
	AST_CLI_DEFINE(console_autoanswer, "Sets/displays autoanswer"),
	AST_CLI_DEFINE(console_mute, "Disable/Enable mic input"),
};

static int unload_module(void)
{
	ast_channel_unregister(&alsa_tech);
	ast_cli_unregister_multiple(cli_alsa, ARRAY_LEN(cli_alsa));

	if (alsa.icard)
		snd_pcm_close(alsa.icard);
	if (alsa.ocard)
		snd_pcm_close(alsa.ocard);
	if (alsa.owner)
		ast_softhangup(alsa.owner, AST_SOFTHANGUP_APPUNLOAD);
	if (alsa.owner)
		return -1;

	ao2_cleanup(alsa_tech.capabilities);
	alsa_tech.capabilities = NULL;

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
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { 0 };

	if (!(alsa_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(alsa_tech.capabilities, ast_format_slin, 0);

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	strcpy(mohinterpret, "default");

	if (!(cfg = ast_config_load(config, config_flags))) {
		ast_log(LOG_ERROR, "Unable to read ALSA configuration file %s.  Aborting.\n", config);
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "%s is in an invalid format.  Aborting.\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	v = ast_variable_browse(cfg, "general");
	for (; v; v = v->next) {
		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
			continue;
		}

		if (!strcasecmp(v->name, "autoanswer")) {
			autoanswer = ast_true(v->value);
		} else if (!strcasecmp(v->name, "mute")) {
			mute = ast_true(v->value);
		} else if (!strcasecmp(v->name, "noaudiocapture")) {
			noaudiocapture = ast_true(v->value);
		} else if (!strcasecmp(v->name, "silencesuppression")) {
			silencesuppression = ast_true(v->value);
		} else if (!strcasecmp(v->name, "silencethreshold")) {
			silencethreshold = atoi(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(context, v->value, sizeof(context));
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(language, v->value, sizeof(language));
		} else if (!strcasecmp(v->name, "extension")) {
			ast_copy_string(exten, v->value, sizeof(exten));
		} else if (!strcasecmp(v->name, "input_device")) {
			ast_copy_string(indevname, v->value, sizeof(indevname));
		} else if (!strcasecmp(v->name, "output_device")) {
			ast_copy_string(outdevname, v->value, sizeof(outdevname));
		} else if (!strcasecmp(v->name, "mohinterpret")) {
			ast_copy_string(mohinterpret, v->value, sizeof(mohinterpret));
		}
	}
	ast_config_destroy(cfg);

	if (soundcard_init() < 0) {
		ast_verb(2, "No sound card detected -- console channel will be unavailable\n");
		ast_verb(2, "Turn off ALSA support by adding 'noload=chan_alsa.so' in /etc/asterisk/modules.conf\n");
		unload_module();

		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_channel_register(&alsa_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Console'\n");
		unload_module();

		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cli_alsa, ARRAY_LEN(cli_alsa));

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "ALSA Console Channel Driver",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
