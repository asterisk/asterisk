/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Use /dev/dsp as an intercom.
 * 
 */
 
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <netinet/in.h>

#if defined(__linux__)
#include <linux/soundcard.h>
#elif defined(__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"

#ifdef __OpenBSD__
#define DEV_DSP "/dev/audio"
#else
#define DEV_DSP "/dev/dsp"
#endif

/* Number of 32 byte buffers -- each buffer is 2 ms */
#define BUFFER_SIZE 32

static char *tdesc = "Intercom using /dev/dsp for output";

static char *app = "Intercom";

static char *synopsis = "(Obsolete) Send to Intercom";
static char *descrip = 
"  Intercom(): Sends the user to the intercom (i.e. /dev/dsp).  This program\n"
"is generally considered  obselete by the chan_oss module.  Returns 0 if the\n"
"user exits with a DTMF tone, or -1 if they hangup.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

AST_MUTEX_DEFINE_STATIC(sound_lock);
static int sound = -1;

static int write_audio(short *data, int len)
{
	int res;
	struct audio_buf_info info;
	ast_mutex_lock(&sound_lock);
	if (sound < 0) {
		ast_log(LOG_WARNING, "Sound device closed?\n");
		ast_mutex_unlock(&sound_lock);
		return -1;
	}
    if (ioctl(sound, SNDCTL_DSP_GETOSPACE, &info)) {
		ast_log(LOG_WARNING, "Unable to read output space\n");
		ast_mutex_unlock(&sound_lock);
        return -1;
    }
	res = write(sound, data, len);
	ast_mutex_unlock(&sound_lock);
	return res;
}

static int create_audio(void)
{
	int fmt, desired, res, fd;
	fd = open(DEV_DSP, O_WRONLY);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to open %s: %s\n", DEV_DSP, strerror(errno));
		close(fd);
		return -1;
	}
	fmt = AFMT_S16_LE;
	res = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set format to 16-bit signed\n");
		close(fd);
		return -1;
	}
	fmt = 0;
	res = ioctl(fd, SNDCTL_DSP_STEREO, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		close(fd);
		return -1;
	}
	/* 8000 Hz desired */
	desired = 8000;
	fmt = desired;
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		close(fd);
		return -1;
	}
	if (fmt != desired) {
		ast_log(LOG_WARNING, "Requested %d Hz, got %d Hz -- sound may be choppy\n", desired, fmt);
	}
#if 1
	/* 2 bytes * 15 units of 2^5 = 32 bytes per buffer */
	fmt = ((BUFFER_SIZE) << 16) | (0x0005);
	res = ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set fragment size -- sound may be choppy\n");
	}
#endif
	sound = fd;
	return 0;
}

static int intercom_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	struct ast_frame *f;
	int oreadformat;
	LOCAL_USER_ADD(u);
	/* Remember original read format */
	oreadformat = chan->readformat;
	/* Set mode to signed linear */
	res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set format to signed linear on channel %s\n", chan->name);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	/* Read packets from the channel */
	while(!res) {
		res = ast_waitfor(chan, -1);
		if (res > 0) {
			res = 0;
			f = ast_read(chan);
			if (f) {
				if (f->frametype == AST_FRAME_DTMF) {
					ast_frfree(f);
					break;
				} else {
					if (f->frametype == AST_FRAME_VOICE) {
						if (f->subclass == AST_FORMAT_SLINEAR) {
							res = write_audio(f->data, f->datalen);
							if (res > 0)
								res = 0;
						} else
							ast_log(LOG_DEBUG, "Unable to handle non-signed linear frame (%d)\n", f->subclass);
					} 
				}
				ast_frfree(f);
			} else
				res = -1;
		}
	}
	
	if (!res)
		ast_set_read_format(chan, oreadformat);

	LOCAL_USER_REMOVE(u);

	return res;
}

int unload_module(void)
{
	int res;

	if (sound > -1)
		close(sound);

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	if (create_audio())
		return -1;
	return ast_register_application(app, intercom_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
