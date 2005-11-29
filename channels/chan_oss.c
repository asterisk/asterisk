/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * FreeBSD changes and multiple device support by Luigi Rizzo, 2005.05.25
 * note-this code best seen with ts=8 (8-spaces tabs) in the editor
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
 * \brief Channel driver for OSS sound cards
 *
 * \par See also
 * \arg \ref Config_oss
 *
 * \ingroup channel_drivers
 */

#include <stdio.h>
#include <ctype.h>	/* for isalnum */
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>


#ifdef __linux
#include <linux/soundcard.h>
#elif defined(__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"

#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/endian.h"

/* ringtones we use */
#include "busy.h"
#include "ringtone.h"
#include "ring10.h"
#include "answer.h"

/*
 * Basic mode of operation:
 *
 * we have one keyboard (which receives commands from the keyboard)
 * and multiple headset's connected to audio cards.
 * Cards/Headsets are named as the sections of oss.conf.
 * The section called [general] contains the default parameters.
 *
 * At any time, the keyboard is attached to one card, and you
 * can switch among them using the command 'console foo'
 * where 'foo' is the name of the card you want.
 *
 * oss.conf parameters are

[general]
; general config options, default values are shown
; all but debug can go also in the device-specific sections.
; debug=0x0		; misc debug flags, default is 0

[card1]
; autoanswer = no	; no autoanswer on call
; autohangup = yes	; hangup when other party closes
; extension=s		; default extension to call
; context=default	; default context
; language=""		; default language
; overridecontext=yes	; the whole dial string is considered an extension.
			; if no, the last @ will start the context

; device=/dev/dsp	; device to open
; mixer="-f /dev/mixer0 pcm 80 ; mixer command to run on start
; queuesize=10		; frames in device driver
; frags=8		; argument to SETFRAGMENT

.. and so on for the other cards.

 */

/*
 * Helper macros to parse config arguments. They will go in a common
 * header file if their usage is globally accepted. In the meantime,
 * we define them here. Typical usage is as below.
 * Remember to open a block right before M_START (as it declares
 * some variables) and use the M_* macros WITHOUT A SEMICOLON:
 *
 *	{
 *		M_START(v->name, v->value) 
 *
 *		M_BOOL("dothis", x->flag1)
 *		M_STR("name", x->somestring)
 *		M_F("bar", some_c_code)
 *		M_END(some_final_statement)
 *		... other code in the block
 *	}
 *
 * XXX NOTE these macros should NOT be replicated in other parts of asterisk. 
 * Likely we will come up with a better way of doing config file parsing.
 */
#define M_START(var, val) \
        char *__s = var; char *__val = val;
#define M_END(x)   x;
#define M_F(tag, f)			if (!strcasecmp((__s), tag)) { f; } else
#define M_BOOL(tag, dst)	M_F(tag, (dst) = ast_true(__val) )
#define M_UINT(tag, dst)	M_F(tag, (dst) = strtoul(__val, NULL, 0) )
#define M_STR(tag, dst)		M_F(tag, ast_copy_string(dst, __val, sizeof(dst)))

/*
 * The following parameters are used in the driver:
 *
 *  FRAME_SIZE	the size of an audio frame, in samples.
 *		160 is used almost universally, so you should not change it.
 *
 *  FRAGS	the argument for the SETFRAGMENT ioctl.
 *		Overridden by the 'frags' parameter in oss.conf
 *
 *		Bits 0-7 are the base-2 log of the device's block size,
 *		bits 16-31 are the number of blocks in the driver's queue.
 *		There are a lot of differences in the way this parameter
 *		is supported by different drivers, so you may need to
 *		experiment a bit with the value.
 *		A good default for linux is 30 blocks of 64 bytes, which
 *		results in 6 frames of 320 bytes (160 samples).
 *		FreeBSD works decently with blocks of 256 or 512 bytes,
 *		leaving the number unspecified.
 *		Note that this only refers to the device buffer size,
 *		this module will then try to keep the lenght of audio
 *		buffered within small constraints.
 *
 *  QUEUE_SIZE	The max number of blocks actually allowed in the device
 *		driver's buffer, irrespective of the available number.
 *		Overridden by the 'queuesize' parameter in oss.conf
 *
 *		Should be >=2, and at most as large as the hw queue above
 *		(otherwise it will never be full).
 */

#define FRAME_SIZE	160
#define	QUEUE_SIZE	10

#if defined(__FreeBSD__)
#define	FRAGS	0x8
#else
#define	FRAGS	( ( (6 * 5) << 16 ) | 0x6 )
#endif

/*
 * XXX text message sizes are probably 256 chars, but i am
 * not sure if there is a suitable definition anywhere.
 */
#define TEXT_SIZE	256

#if 0
#define	TRYOPEN	1	/* try to open on startup */
#endif
#define	O_CLOSE	0x444	/* special 'close' mode for device */
/* Which device to use */
#if defined( __OpenBSD__ ) || defined( __NetBSD__ )
#define DEV_DSP "/dev/audio"
#else
#define DEV_DSP "/dev/dsp"
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char *config = "oss.conf";	/* default config file */

static int oss_debug;

/*
 * Each sound is made of 'datalen' samples of sound, repeated as needed to
 * generate 'samplen' samples of data, then followed by 'silencelen' samples
 * of silence. The loop is repeated if 'repeat' is set.
 */
struct sound {
	int ind;
	char *desc;
	short *data;
	int datalen;
	int samplen;
	int silencelen;
	int repeat;
};

static struct sound sounds[] = {
	{ AST_CONTROL_RINGING, "RINGING", ringtone, sizeof(ringtone)/2, 16000, 32000, 1 },
	{ AST_CONTROL_BUSY, "BUSY", busy, sizeof(busy)/2, 4000, 4000, 1 },
	{ AST_CONTROL_CONGESTION, "CONGESTION", busy, sizeof(busy)/2, 2000, 2000, 1 },
	{ AST_CONTROL_RING, "RING10", ring10, sizeof(ring10)/2, 16000, 32000, 1 },
	{ AST_CONTROL_ANSWER, "ANSWER", answer, sizeof(answer)/2, 2200, 0, 0 },
	{ -1, NULL, 0, 0, 0, 0 },	/* end marker */
};


/*
 * descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_oss_pvt {
	struct chan_oss_pvt *next;

	char *type;	/* XXX maybe take the one from oss_tech */
	char *name;
	/*
	 * cursound indicates which in struct sound we play. -1 means nothing,
	 * any other value is a valid sound, in which case sampsent indicates
	 * the next sample to send in [0..samplen + silencelen]
	 * nosound is set to disable the audio data from the channel
	 * (so we can play the tones etc.).
	 */
	int sndcmd[2]; /* Sound command pipe */
	int cursound;	/* index of sound to send */
	int sampsent;	/* # of sound samples sent	*/
	int nosound;	/* set to block audio from the PBX */

	int total_blocks;	/* total blocks in the output device */
	int sounddev;
	enum { M_UNSET, M_FULL, M_READ, M_WRITE } duplex;
	int autoanswer;
	int autohangup;
	int hookstate;
	char *mixer_cmd;		/* initial command to issue to the mixer */
	unsigned int	queuesize;	/* max fragments in queue */
	unsigned int	frags;		/* parameter for SETFRAGMENT */

	int warned;		/* various flags used for warnings */
#define WARN_used_blocks	1
#define WARN_speed		2
#define WARN_frag		4
	int w_errors;	/* overfull in the write path */
	struct timeval lastopen;

	int overridecontext;
	int mute;
	char device[64];	/* device to open */

	pthread_t sthread;

	struct ast_channel *owner;
	char ext[AST_MAX_EXTENSION];
	char ctx[AST_MAX_CONTEXT];
	char language[MAX_LANGUAGE];

	/* buffers used in oss_write */
	char oss_write_buf[FRAME_SIZE*2];
	int oss_write_dst;
	/* buffers used in oss_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char oss_read_buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	int readpos; /* read position above */
	struct ast_frame read_f;	/* returned by oss_read */
};

static struct chan_oss_pvt oss_default = {
	.type = "Console",
	.cursound = -1,
	.sounddev = -1,
	.duplex = M_UNSET, /* XXX check this */
	.autoanswer = 1,
	.autohangup = 1,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.ext = "s",
	.ctx = "default",
	.readpos = AST_FRIENDLY_OFFSET,	/* start here on reads */
	.lastopen = { 0, 0 },
};

static char *oss_active;	 /* the active device */

static int setformat(struct chan_oss_pvt *o, int mode);

static struct ast_channel *oss_request(const char *type, int format, void *data
, int *cause);
static int oss_digit(struct ast_channel *c, char digit);
static int oss_text(struct ast_channel *c, const char *text);
static int oss_hangup(struct ast_channel *c);
static int oss_answer(struct ast_channel *c);
static struct ast_frame *oss_read(struct ast_channel *chan);
static int oss_call(struct ast_channel *c, char *dest, int timeout);
static int oss_write(struct ast_channel *chan, struct ast_frame *f);
static int oss_indicate(struct ast_channel *chan, int cond);
static int oss_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static const struct ast_channel_tech oss_tech = {
	.type =			"Console",
	.description =	"OSS Console Channel Driver",
	.capabilities =	AST_FORMAT_SLINEAR,
	.requester =	oss_request,
	.send_digit =	oss_digit,
	.send_text =	oss_text,
	.hangup =		oss_hangup,
	.answer =		oss_answer,
	.read =			oss_read,
	.call =			oss_call,
	.write =		oss_write,
	.indicate =		oss_indicate,
	.fixup =		oss_fixup,
};

/*
 * returns a pointer to the descriptor with the given name
 */
static struct chan_oss_pvt *find_desc(char *dev)
{
	struct chan_oss_pvt *o;

	for (o = oss_default.next; o && strcmp(o->name, dev) != 0; o = o->next)
		;
	if (o == NULL)
		ast_log(LOG_WARNING, "could not find <%s>\n", dev);
	return o;
}

/*
 * split a string in extension-context, returns pointers to malloc'ed
 * strings.
 * If we do not have 'overridecontext' then the last @ is considered as
 * a context separator, and the context is overridden.
 * This is usually not very necessary as you can play with the dialplan,
 * and it is nice not to need it because you have '@' in SIP addresses.
 * Return value is the buffer address.
 */
static char *ast_ext_ctx(const char *src, char **ext, char **ctx)
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (ext == NULL || ctx == NULL)
		return NULL;	/* error */
	*ext = *ctx = NULL;
	if (src && *src != '\0')
		*ext = strdup(src);
	if (*ext == NULL)
		return NULL;
	if (!o->overridecontext) {
		/* parse from the right */
		*ctx = strrchr(*ext, '@');
		if (*ctx)
			*(*ctx)++ = '\0';
	}
	return *ext;
}

/*
 * Returns the number of blocks used in the audio output channel
 */
static int used_blocks(struct chan_oss_pvt *o)
{
	struct audio_buf_info info;

	if (ioctl(o->sounddev, SNDCTL_DSP_GETOSPACE, &info)) {
		if (! (o->warned & WARN_used_blocks)) {
			ast_log(LOG_WARNING, "Error reading output space\n");
			o->warned |= WARN_used_blocks;
		}
		return 1;
	}
	if (o->total_blocks == 0) {
		if (0) /* debugging */
			ast_log(LOG_WARNING, "fragtotal %d size %d avail %d\n",
			    info.fragstotal,
			    info.fragsize,
			    info.fragments);
		o->total_blocks = info.fragments;
	}
	return o->total_blocks - info.fragments;
}

/* Write an exactly FRAME_SIZE sized frame */
static int soundcard_writeframe(struct chan_oss_pvt *o, short *data)
{	
	int res;

	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	if (o->sounddev < 0)
		return 0;	/* not fatal */
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * XXX in some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) {	/* no room to write a block */
		if (o->w_errors++ == 0 && (oss_debug & 0x4))
			ast_log(LOG_WARNING, "write: used %d blocks (%d)\n",
			    res, o->w_errors);
		return 0;
	}
	o->w_errors = 0;
	return write(o->sounddev, ((void *)data), FRAME_SIZE * 2);
}

/*
 * Handler for 'sound writable' events from the sound thread.
 * Builds a frame from the high level description of the sounds,
 * and passes it to the audio device.
 * The actual sound is made of 1 or more sequences of sound samples
 * (s->datalen, repeated to make s->samplen samples) followed by
 * s->silencelen samples of silence. The position in the sequence is stored
 * in o->sampsent, which goes between 0 .. s->samplen+s->silencelen.
 * In case we fail to write a frame, don't update o->sampsent.
 */
static void send_sound(struct chan_oss_pvt *o)
{
	short myframe[FRAME_SIZE];
	int ofs, l, start;
	int l_sampsent = o->sampsent;
	struct sound *s;

	if (o->cursound < 0)	/* no sound to send */
		return;
	s = &sounds[o->cursound];
	for (ofs = 0; ofs < FRAME_SIZE; ofs += l) {
		l = s->samplen - l_sampsent;	/* # of available samples */
		if (l > 0) {
			start = l_sampsent % s->datalen; /* source offset */
			if (l > FRAME_SIZE - ofs)	/* don't overflow the frame */
				l = FRAME_SIZE - ofs;
			if (l > s->datalen - start)	/* don't overflow the source */
				l = s->datalen - start;
			bcopy(s->data + start, myframe + ofs, l*2);
			if (0)
				ast_log(LOG_WARNING, "send_sound sound %d/%d of %d into %d\n",
				    l_sampsent, l, s->samplen, ofs);
			l_sampsent += l;
		} else { /* end of samples, maybe some silence */
			static const short silence[FRAME_SIZE] = {0, };

			l += s->silencelen;
			if (l > 0) {
				if (l > FRAME_SIZE - ofs)
					l = FRAME_SIZE - ofs;
				bcopy(silence, myframe + ofs, l*2);
				l_sampsent += l;
			} else { /* silence is over, restart sound if loop */
				if (s->repeat == 0) {	/* last block */
					o->cursound = -1;
					o->nosound = 0;	/* allow audio data */
					if (ofs < FRAME_SIZE)	/* pad with silence */
						bcopy(silence, myframe + ofs, (FRAME_SIZE - ofs)*2);
				}
				l_sampsent = 0;
			}
		}
	}
	l = soundcard_writeframe(o, myframe);
	if (l > 0)
		o->sampsent = l_sampsent;	/* update status */
}

static void *sound_thread(void *arg)
{
	char ign[4096];
	struct chan_oss_pvt *o = (struct chan_oss_pvt *)arg;

	/*
	 * Just in case, kick the driver by trying to read from it.
	 * Ignore errors - this read is almost guaranteed to fail.
	 */
	read(o->sounddev, ign, sizeof(ign));
	for (;;) {
		fd_set rfds, wfds;
		int maxfd, res;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(o->sndcmd[0], &rfds);
		maxfd = o->sndcmd[0];	/* pipe from the main process */
		if (o->cursound > -1 && o->sounddev < 0)
			setformat(o, O_RDWR);   /* need the channel, try to reopen */
		else if (o->cursound == -1 && o->owner == NULL)
			setformat(o, O_CLOSE);  /* can close */
		if (o->sounddev > -1) {
			if (!o->owner) { /* no one owns the audio, so we must drain it */
				FD_SET(o->sounddev, &rfds);
				maxfd = MAX(o->sounddev, maxfd);
			}
			if (o->cursound > -1) {
				FD_SET(o->sounddev, &wfds);
				maxfd = MAX(o->sounddev, maxfd);
			}
		}
		/* ast_select emulates linux behaviour in terms of timeout handling */
		res = ast_select(maxfd + 1, &rfds, &wfds, NULL, NULL);
		if (res < 1) {
			ast_log(LOG_WARNING, "select failed: %s\n", strerror(errno));
			sleep(1);
			continue;
		}
		if (FD_ISSET(o->sndcmd[0], &rfds)) {
			/* read which sound to play from the pipe */
			int i, what = -1;

			read(o->sndcmd[0], &what, sizeof(what));
			for (i = 0; sounds[i].ind != -1; i++) {
				if (sounds[i].ind == what) {
					o->cursound = i;
					o->sampsent = 0;
					o->nosound = 1; /* block audio from pbx */
					break;
				}
			}
			if (sounds[i].ind == -1)
				ast_log(LOG_WARNING, "invalid sound index: %d\n", what);
		}
		if (o->sounddev > -1) {
			if (FD_ISSET(o->sounddev, &rfds)) /* read and ignore errors */
				read(o->sounddev, ign, sizeof(ign));
			if (FD_ISSET(o->sounddev, &wfds))
				send_sound(o);
		}
	}
	return NULL; /* Never reached */
}

/*
 * reset and close the device if opened,
 * then open and initialize it in the desired mode,
 * trigger reads and writes so we can start using it.
 */
static int setformat(struct chan_oss_pvt *o, int mode)
{
	int fmt, desired, res, fd;

	if (o->sounddev >= 0) {
		ioctl(o->sounddev, SNDCTL_DSP_RESET, 0);
		close(o->sounddev);
		o->duplex = M_UNSET;
		o->sounddev = -1;
	}
	if (mode == O_CLOSE)	/* we are done */
		return 0;
	if (ast_tvdiff_ms(ast_tvnow(), o->lastopen) < 1000)
		return -1;	/* don't open too often */
	o->lastopen = ast_tvnow();
	fd = o->sounddev = open(o->device, mode |O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to re-open DSP device %s: %s\n",
		    o->device, strerror(errno));
		return -1;
	}
	if (o->owner)
		o->owner->fds[0] = fd;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	fmt = AFMT_S16_LE;
#else
	fmt = AFMT_S16_BE;
#endif
	res = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set format to 16-bit signed\n");
		return -1;
	}
	switch (mode) {
	case O_RDWR:
		res = ioctl(fd, SNDCTL_DSP_SETDUPLEX, 0);
		/* Check to see if duplex set (FreeBSD Bug)*/
		res = ioctl(fd, SNDCTL_DSP_GETCAPS, &fmt);
		if (res == 0 && (fmt & DSP_CAP_DUPLEX)) {
			if (option_verbose > 1) 
				ast_verbose(VERBOSE_PREFIX_2 "Console is full duplex\n");
			o->duplex = M_FULL;
		};
		break;
	case O_WRONLY:
		o->duplex = M_WRITE;
		break;
	case O_RDONLY:
		o->duplex = M_READ;
		break;
	}

	fmt = 0;
	res = ioctl(fd, SNDCTL_DSP_STEREO, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	fmt = desired = 8000; /* 8000 Hz desired */
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	if (fmt != desired) {
		if (!(o->warned & WARN_speed)) {
			ast_log(LOG_WARNING,
			    "Requested %d Hz, got %d Hz -- sound may be choppy\n",
			    desired, fmt);
			o->warned |= WARN_speed;
		}
	}
	/*
	 * on Freebsd, SETFRAGMENT does not work very well on some cards.
	 * Default to use 256 bytes, let the user override
	 */
	if (o->frags) {
		fmt = o->frags;
		res = ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
		if (res < 0) {
			if (!(o->warned & WARN_frag)) {
				ast_log(LOG_WARNING,
					"Unable to set fragment size -- sound may be choppy\n");
				o->warned |= WARN_frag;
			}
		}
	}
	/* on some cards, we need SNDCTL_DSP_SETTRIGGER to start outputting */
	res = PCM_ENABLE_INPUT | PCM_ENABLE_OUTPUT;
	res = ioctl(fd, SNDCTL_DSP_SETTRIGGER, &res);
	/* it may fail if we are in half duplex, never mind */
	return 0;
}

/*
 * some of the standard methods supported by channels.
 */
static int oss_digit(struct ast_channel *c, char digit)
{
	/* no better use for received digits than print them */
	ast_verbose( " << Console Received digit %c >> \n", digit);
	return 0;
}

static int oss_text(struct ast_channel *c, const char *text)
{
	/* print received messages */
	ast_verbose( " << Console Received text %s >> \n", text);
	return 0;
}

/* Play ringtone 'x' on device 'o' */
static void ring(struct chan_oss_pvt *o, int x)
{
	write(o->sndcmd[1], &x, sizeof(x));
}


/*
 * handler for incoming calls. Either autoanswer, or start ringing
 */
static int oss_call(struct ast_channel *c, char *dest, int timeout)
{
	struct chan_oss_pvt *o = c->tech_pvt;
	struct ast_frame f = { 0, };

	ast_verbose(" << Call to '%s' on console from <%s><%s><%s> >>\n",
		dest, c->cid.cid_dnid, c->cid.cid_num, c->cid.cid_name);
	if (o->autoanswer) {
		ast_verbose( " << Auto-answered >> \n" );
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_ANSWER;
		ast_queue_frame(c, &f);
	} else {
		ast_verbose("<< Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_RINGING;
		ast_queue_frame(c, &f);
		ring(o, AST_CONTROL_RING);
	}
	return 0;
}

/*
 * remote side answered the phone
 */
static int oss_answer(struct ast_channel *c)
{
	struct chan_oss_pvt *o = c->tech_pvt;

	ast_verbose( " << Console call has been answered >> \n");
#if 0
	/* play an answer tone (XXX do we really need it ?) */
	ring(o, AST_CONTROL_ANSWER);
#endif
	ast_setstate(c, AST_STATE_UP);
	o->cursound = -1;
	o->nosound=0;
	return 0;
}

static int oss_hangup(struct ast_channel *c)
{
	struct chan_oss_pvt *o = c->tech_pvt;

	o->cursound = -1;
	o->nosound = 0;
	c->tech_pvt = NULL;
	o->owner = NULL;
	ast_verbose( " << Hangup on console >> \n");
	ast_mutex_lock(&usecnt_lock);	/* XXX not sure why */
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
	if (o->hookstate) {
		if (o->autoanswer || o->autohangup) {
			/* Assume auto-hangup too */
			o->hookstate = 0;
			setformat(o, O_CLOSE);
		} else {
			/* Make congestion noise */
			ring(o, AST_CONTROL_CONGESTION);
		}
	}
	return 0;
}

/* used for data coming from the network */
static int oss_write(struct ast_channel *c, struct ast_frame *f)
{
	int src;
	struct chan_oss_pvt *o = c->tech_pvt;

	/* Immediately return if no sound is enabled */
	if (o->nosound)
		return 0;
	/* Stop any currently playing sound */
	o->cursound = -1;
	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */
	src = 0; /* read position into f->data */
	while ( src < f->datalen ) {
		/* Compute spare room in the buffer */
		int l = sizeof(o->oss_write_buf) - o->oss_write_dst;

		if (f->datalen - src >= l) {	/* enough to fill a frame */
			memcpy(o->oss_write_buf + o->oss_write_dst,
				f->data + src, l);
			soundcard_writeframe(o, (short *)o->oss_write_buf);
			src += l;
			o->oss_write_dst = 0;
		} else { /* copy residue */
			l = f->datalen - src;
			memcpy(o->oss_write_buf + o->oss_write_dst,
				f->data + src, l);
			src += l;	/* but really, we are done */
			o->oss_write_dst += l;
		}
	}
	return 0;
}

static struct ast_frame *oss_read(struct ast_channel *c)
{
	int res;
	struct chan_oss_pvt *o = c->tech_pvt;
	struct ast_frame *f = &o->read_f;

	/* prepare a NULL frame in case we don't have enough data to return */
	bzero(f, sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = o->type;

	res = read(o->sounddev, o->oss_read_buf + o->readpos,
	sizeof(o->oss_read_buf) - o->readpos);
	if (res < 0)	/* audio data not ready, return a NULL frame */
		return f;

	o->readpos += res;
	if (o->readpos < sizeof(o->oss_read_buf))	/* not enough samples */
		return f;

	if (o->mute)
		return f;

	o->readpos = AST_FRIENDLY_OFFSET;	/* reset read pointer for next frame */
	if (c->_state != AST_STATE_UP)	/* drop data if frame is not up */
		return f;
	/* ok we can build and deliver the frame to the caller */
	f->frametype = AST_FRAME_VOICE;
	f->subclass = AST_FORMAT_SLINEAR;
	f->samples = FRAME_SIZE;
	f->datalen = FRAME_SIZE * 2;
	f->data = o->oss_read_buf + AST_FRIENDLY_OFFSET;
	f->offset = AST_FRIENDLY_OFFSET;
	return f;
}

static int oss_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_oss_pvt *o = newchan->tech_pvt;
	o->owner = newchan;
	return 0;
}

static int oss_indicate(struct ast_channel *c, int cond)
{
	struct chan_oss_pvt *o = c->tech_pvt;
	int res;

	switch(cond) {
	case AST_CONTROL_BUSY:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_RINGING:
		res = cond;
		break;

	case -1:
		o->cursound = -1;
		o->nosound = 0; /* when cursound is -1 nosound must be 0 */
		return 0;

	case AST_CONTROL_VIDUPDATE:
		res = -1;
		break;
	default:
		ast_log(LOG_WARNING,
		    "Don't know how to display condition %d on %s\n",
		    cond, c->name);
		return -1;
	}
	if (res > -1)
		ring(o, res);
	return 0;	
}

/*
 * allocate a new channel.
 */
static struct ast_channel *oss_new(struct chan_oss_pvt *o,
	char *ext, char *ctx, int state)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1);
	if (c == NULL)
		return NULL;
	c->tech = &oss_tech;
	snprintf(c->name, sizeof(c->name), "OSS/%s", o->device + 5);
	c->type = o->type;
	c->fds[0] = o->sounddev; /* -1 if device closed, override later */
	c->nativeformats = AST_FORMAT_SLINEAR;
	c->readformat = AST_FORMAT_SLINEAR;
	c->writeformat = AST_FORMAT_SLINEAR;
	c->tech_pvt = o;

	if (!ast_strlen_zero(ctx))
		ast_copy_string(c->context, ctx, sizeof(c->context));
	if (!ast_strlen_zero(ext))
		ast_copy_string(c->exten, ext, sizeof(c->exten));
	if (!ast_strlen_zero(o->language))
		ast_copy_string(c->language, o->language, sizeof(c->language));

	o->owner = c;
	ast_setstate(c, state);
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", c->name);
			ast_hangup(c);
			o->owner = c = NULL;
			/* XXX what about the channel itself ? */
			/* XXX what about usecnt ? */
		}
	}
	return c;
}

static struct ast_channel *oss_request(const char *type,
	int format, void *data, int *cause)
{
	struct ast_channel *c;
	struct chan_oss_pvt *o = find_desc(data);

	ast_log(LOG_WARNING, "oss_request ty <%s> data 0x%p <%s>\n",
		type, data, (char *)data);
	if (o == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", (char *)data);
		/* XXX we could default to 'dsp' perhaps ? */
		return NULL;
	}
	if ((format & AST_FORMAT_SLINEAR) == 0) {
		ast_log(LOG_NOTICE, "Format 0x%x unsupported\n", format);
		return NULL;
	}
	if (o->owner) {
		ast_log(LOG_NOTICE, "Already have a call (chan %p) on the OSS channel\n", o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c= oss_new(o, NULL, NULL, AST_STATE_DOWN);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new OSS channel\n");
		return NULL;
	}
	return c;
}

static int console_autoanswer(int fd, int argc, char *argv[])
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc == 1) {
		ast_cli(fd, "Auto answer is %s.\n", o->autoanswer ? "on" : "off");
		return RESULT_SUCCESS;
	}
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	if (o == NULL) {
		ast_log(LOG_WARNING, "Cannot find device %s (should not happen!)\n",
		    oss_active);
		return RESULT_FAILURE;
	}
	if (!strcasecmp(argv[1], "on"))
		o->autoanswer = -1;
	else if (!strcasecmp(argv[1], "off"))
		o->autoanswer = 0;
	else
		return RESULT_SHOWUSAGE;
	return RESULT_SUCCESS;
}

static char *autoanswer_complete(char *line, char *word, int pos, int state)
{
	int l = strlen(word);

	switch(state) {
	case 0:
		if (l && !strncasecmp(word, "on", MIN(l, 2)))
			return strdup("on");
	case 1:
		if (l && !strncasecmp(word, "off", MIN(l, 3)))
			return strdup("off");
	default:
		return NULL;
	}
	return NULL;
}

static char autoanswer_usage[] =
"Usage: autoanswer [on|off]\n"
"       Enables or disables autoanswer feature.  If used without\n"
"       argument, displays the current on/off status of autoanswer.\n"
"       The default value of autoanswer is in 'oss.conf'.\n";

/*
 * answer command from the console
 */
static int console_answer(int fd, int argc, char *argv[])
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc != 1)
		return RESULT_SHOWUSAGE;
	if (!o->owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	o->hookstate = 1;
	o->cursound = -1;
	o->nosound = 0;
	ast_queue_frame(o->owner, &f);
#if 0
	/* XXX do we really need it ? considering we shut down immediately... */
	ring(o, AST_CONTROL_ANSWER);
#endif
	return RESULT_SUCCESS;
}

static char sendtext_usage[] =
"Usage: send text <message>\n"
"       Sends a text message for display on the remote terminal.\n";

/*
 * concatenate all arguments into a single string
 */
static int console_sendtext(int fd, int argc, char *argv[])
{
	struct chan_oss_pvt *o = find_desc(oss_active);
	int tmparg = 2;
	char text2send[TEXT_SIZE] = "";
	struct ast_frame f = { 0, };

	if (argc < 2)
		return RESULT_SHOWUSAGE;
	if (!o->owner) {
		ast_cli(fd, "Not in a call\n");
		return RESULT_FAILURE;
	}
	while (tmparg < argc) {
		strncat(text2send, argv[tmparg++],
			sizeof(text2send) - strlen(text2send) - 1);
		strncat(text2send, " ",
			sizeof(text2send) - strlen(text2send) - 1);
	}
	if (!ast_strlen_zero(text2send)) {
		text2send[strlen(text2send) - 1] = '\n';
		f.frametype = AST_FRAME_TEXT;
		f.subclass = 0;
		f.data = text2send;
		f.datalen = strlen(text2send);
		ast_queue_frame(o->owner, &f);
	}
	return RESULT_SUCCESS;
}

static char answer_usage[] =
"Usage: answer\n"
"       Answers an incoming call on the console (OSS) channel.\n";

static int console_hangup(int fd, int argc, char *argv[])
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc != 1)
		return RESULT_SHOWUSAGE;
	o->cursound = -1;
	o->nosound = 0;
	if (!o->owner && !o->hookstate) { /* XXX maybe only one ? */
		ast_cli(fd, "No call to hangup up\n");
		return RESULT_FAILURE;
	}
	o->hookstate = 0;
	if (o->owner)
		ast_queue_hangup(o->owner);
	setformat(o, O_CLOSE);
	return RESULT_SUCCESS;
}

static char hangup_usage[] =
"Usage: hangup\n"
"       Hangs up any call currently placed on the console.\n";


static int console_flash(int fd, int argc, char *argv[])
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_FLASH };
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc != 1)
		return RESULT_SHOWUSAGE;
	o->cursound = -1;
	o->nosound = 0; /* when cursound is -1 nosound must be 0 */
	if (!o->owner) { /* XXX maybe !o->hookstate too ? */
		ast_cli(fd, "No call to flash\n");
		return RESULT_FAILURE;
	}
	o->hookstate = 0;
	if (o->owner) /* XXX must be true, right ? */
		ast_queue_frame(o->owner, &f);
	return RESULT_SUCCESS;
}


static char flash_usage[] =
"Usage: flash\n"
"       Flashes the call currently placed on the console.\n";



static int console_dial(int fd, int argc, char *argv[])
{
	char *s = NULL, *mye = NULL, *myc = NULL;
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc != 1 && argc != 2)
		return RESULT_SHOWUSAGE;
	if (o->owner) {	/* already in a call */
		int i;
		struct ast_frame f = { AST_FRAME_DTMF, 0 };

		if (argc == 1) {	/* argument is mandatory here */
			ast_cli(fd, "Already in a call. You can only dial digits until you hangup.\n");
			return RESULT_FAILURE;
		}
		s = argv[1];
		/* send the string one char at a time */
		for (i=0; i<strlen(s); i++) {
			f.subclass = s[i];
			ast_queue_frame(o->owner, &f);
		}
		return RESULT_SUCCESS;
	}
	/* if we have an argument split it into extension and context */
	if (argc == 2)
		s = ast_ext_ctx(argv[1], &mye, &myc);
	/* supply default values if needed */
	if (mye == NULL)
		mye = o->ext;
	if (myc == NULL)
		myc = o->ctx;
	if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
		o->hookstate = 1;
		oss_new(o, mye, myc, AST_STATE_RINGING);
	} else
		ast_cli(fd, "No such extension '%s' in context '%s'\n", mye, myc);
	if (s)
		free(s);
	return RESULT_SUCCESS;
}

static char dial_usage[] =
"Usage: dial [extension[@context]]\n"
"       Dials a given extensison (and context if specified)\n";

static char mute_usage[] =
"Usage: mute\nMutes the microphone\n";

static char unmute_usage[] =
"Usage: unmute\nUnmutes the microphone\n";

static int console_mute(int fd, int argc, char *argv[])
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc != 1)
		return RESULT_SHOWUSAGE;
	o->mute = 1;
	return RESULT_SUCCESS;
}

static int console_unmute(int fd, int argc, char *argv[])
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (argc != 1)
		return RESULT_SHOWUSAGE;
	o->mute = 0;
	return RESULT_SUCCESS;
}

static int console_transfer(int fd, int argc, char *argv[])
{
	struct chan_oss_pvt *o = find_desc(oss_active);
	struct ast_channel *b = NULL;
	char *tmp, *ext, *ctx;

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	if (o == NULL)
		return RESULT_FAILURE;
	if (o->owner ==NULL || (b = ast_bridged_channel(o->owner)) == NULL) {
		ast_cli(fd, "There is no call to transfer\n");
		return RESULT_SUCCESS;
	}

	tmp = ast_ext_ctx(argv[1], &ext, &ctx);
	if (ctx == NULL)		/* supply default context if needed */
		ctx = o->owner->context;
	if (!ast_exists_extension(b, ctx, ext, 1, b->cid.cid_num))
		ast_cli(fd, "No such extension exists\n");
	else {
		ast_cli(fd, "Whee, transferring %s to %s@%s.\n",
			b->name, ext, ctx);
		if (ast_async_goto(b, ctx, ext, 1))
			ast_cli(fd, "Failed to transfer :(\n");
	}
	if (tmp)
		free(tmp);
	return RESULT_SUCCESS;
}

static char transfer_usage[] =
"Usage: transfer <extension>[@context]\n"
"       Transfers the currently connected call to the given extension (and\n"
"context if specified)\n";

static char console_usage[] =
"Usage: console [device]\n"
"       If used without a parameter, displays which device is the current\n"
"console.  If a device is specified, the console sound device is changed to\n"
"the device specified.\n";

static int console_active(int fd, int argc, char *argv[])
{
	if (argc == 1)
		ast_cli(fd, "active console is [%s]\n", oss_active);
	else if (argc != 2)
		return RESULT_SHOWUSAGE;
	else {
		struct chan_oss_pvt *o;
		if (strcmp(argv[1], "show") == 0) {
			for (o = oss_default.next; o ; o = o->next)
			    ast_cli(fd, "device [%s] exists\n", o->name);
			return RESULT_SUCCESS;
		}
		o = find_desc(argv[1]);
		if (o == NULL)
			ast_cli(fd, "No device [%s] exists\n", argv[1]);
		else
			oss_active = o->name;
	}
	return RESULT_SUCCESS;
}

static struct ast_cli_entry myclis[] = {
	{ { "answer", NULL }, console_answer, "Answer an incoming console call", answer_usage },
	{ { "hangup", NULL }, console_hangup, "Hangup a call on the console", hangup_usage },
	{ { "flash", NULL }, console_flash, "Flash a call on the console", flash_usage },
	{ { "dial", NULL }, console_dial, "Dial an extension on the console", dial_usage },
	{ { "mute", NULL }, console_mute, "Disable mic input", mute_usage },
	{ { "unmute", NULL }, console_unmute, "Enable mic input", unmute_usage },
	{ { "transfer", NULL }, console_transfer, "Transfer a call to a different extension", transfer_usage },
	{ { "send", "text", NULL }, console_sendtext, "Send text to the remote device", sendtext_usage },
	{ { "autoanswer", NULL }, console_autoanswer, "Sets/displays autoanswer", autoanswer_usage, autoanswer_complete },
	{ { "console", NULL }, console_active, "Sets/displays active console", console_usage },
};

/*
 * store the mixer argument from the config file, filtering possibly
 * invalid or dangerous values (the string is used as argument for
 * system("mixer %s")
 */
static void store_mixer(struct chan_oss_pvt *o, char *s)
{
	int i;

	for (i=0; i < strlen(s); i++) {
		if (!isalnum(s[i]) && index(" \t-/", s[i]) == NULL) {
			ast_log(LOG_WARNING,
				"Suspect char %c in mixer cmd, ignoring:\n\t%s\n", s[i], s);
			return;
		}
	}
	if (o->mixer_cmd)
		free(o->mixer_cmd);
	o->mixer_cmd = strdup(s);
	ast_log(LOG_WARNING, "setting mixer %s\n", s);
}

/*
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_oss_pvt * store_config(struct ast_config *cfg, char *ctg)
{
	struct ast_variable *v;
	struct chan_oss_pvt *o;

	if (ctg == NULL) {
		o = &oss_default;
		ctg = "general";
	} else {
		o = (struct chan_oss_pvt *)malloc(sizeof *o);
		if (o == NULL)		/* fail */
			return NULL;
		*o = oss_default;
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o->name = strdup("dsp");
			oss_active = o->name;
			goto openit;
		}
		o->name = strdup(ctg);
	}

	o->lastopen = ast_tvnow(); /* don't leave it 0 or tvdiff may wrap */
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg);v; v=v->next) {
		M_START(v->name, v->value);

		M_BOOL("autoanswer", o->autoanswer)
		M_BOOL("autohangup", o->autohangup)
		M_BOOL("overridecontext", o->overridecontext)
		M_STR("device", o->device)
		M_UINT("frags", o->frags)
		M_UINT("debug", oss_debug)
		M_UINT("queuesize", o->queuesize)
		M_STR("context", o->ctx)
		M_STR("language", o->language)
		M_STR("extension", o->ext)
		M_F("mixer", store_mixer(o, v->value))
		M_END(;);
	}
	if (ast_strlen_zero(o->device))
		ast_copy_string(o->device, DEV_DSP, sizeof(o->device));
	if (o->mixer_cmd) {
		char *cmd;

		asprintf(&cmd, "mixer %s", o->mixer_cmd);
		ast_log(LOG_WARNING, "running [%s]\n", cmd);
		system(cmd);
		free(cmd);
	}
	if (o == &oss_default)	/* we are done with the default */
		return NULL;

openit:
#if TRYOPEN
	if (setformat(o, O_RDWR) < 0) {	/* open device */
		if (option_verbose > 0) {
			ast_verbose(VERBOSE_PREFIX_2 "Device %s not detected\n", ctg);
			ast_verbose(VERBOSE_PREFIX_2 "Turn off OSS support by adding "
			    "'noload=chan_oss.so' in /etc/asterisk/modules.conf\n");
		}
		goto error;
	}
	if (o->duplex != M_FULL)
		ast_log(LOG_WARNING, "XXX I don't work right with non "
			"full-duplex sound cards XXX\n");
#endif /* TRYOPEN */
	if (pipe(o->sndcmd) != 0) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		goto error;
	}
	ast_pthread_create(&o->sthread, NULL, sound_thread, o);
	/* link into list of devices */
	if (o != &oss_default) {
		o->next = oss_default.next;
		oss_default.next = o;
	}
	return o;

error:
	if (o != &oss_default)
		free(o);
	return NULL;
}

int load_module(void)
{
	int i;
	struct ast_config *cfg;

	/* load config file */
	cfg = ast_config_load(config);
	if (cfg != NULL) {
		char *ctg = NULL;	/* first pass is 'general' */

		do {
			store_config(cfg, ctg);
		} while ( (ctg = ast_category_browse(cfg, ctg)) != NULL);
		ast_config_destroy(cfg);
	}
	if (find_desc(oss_active) == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", oss_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return -1;
	}
	i = ast_channel_register(&oss_tech);
	if (i < 0) {
		ast_log(LOG_ERROR, "Unable to register channel class '%s'\n",
			oss_default.type);
		/* XXX should cleanup allocated memory etc. */
		return -1;
	}
	ast_cli_register_multiple(myclis, sizeof(myclis)/sizeof(struct ast_cli_entry));
	return 0;
}


int unload_module()
{
	struct chan_oss_pvt *o;

	ast_channel_unregister(&oss_tech);
	ast_cli_unregister_multiple(myclis,
		sizeof(myclis)/sizeof(struct ast_cli_entry));

	for (o = oss_default.next; o ; o = o->next) {
		close(o->sounddev);
		if (o->sndcmd[0] > 0) {
			close(o->sndcmd[0]);
			close(o->sndcmd[1]);
		}
		if (o->owner)
			ast_softhangup(o->owner, AST_SOFTHANGUP_APPUNLOAD);
		if (o->owner) /* XXX how ??? */
			return -1;
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}
	return 0;
}

char *description()
{
	return (char *)oss_tech.description;
}

int usecount()
{
	return usecnt;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
