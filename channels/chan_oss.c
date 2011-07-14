/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
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

// #define HAVE_VIDEO_CONSOLE	// uncomment to enable video
/*! \file
 *
 * \brief Channel driver for OSS sound cards
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Luigi Rizzo
 *
 * \par See also
 * \arg \ref Config_oss
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>oss</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>		/* isalnum() used here */
#include <math.h>
#include <sys/ioctl.h>		

#ifdef __linux
#include <linux/soundcard.h>
#elif defined(__FreeBSD__) || defined(__CYGWIN__) || defined(__GLIBC__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"

#include "console_video.h"

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in oss.conf.sample */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};
static struct ast_jb_conf global_jbconf;

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
START_CONFIG

[general]
    ; General config options, with default values shown.
    ; You should use one section per device, with [general] being used
    ; for the first device and also as a template for other devices.
    ;
    ; All but 'debug' can go also in the device-specific sections.
    ;
    ; debug = 0x0		; misc debug flags, default is 0

    ; Set the device to use for I/O
    ; device = /dev/dsp

    ; Optional mixer command to run upon startup (e.g. to set
    ; volume levels, mutes, etc.
    ; mixer =

    ; Software mic volume booster (or attenuator), useful for sound
    ; cards or microphones with poor sensitivity. The volume level
    ; is in dB, ranging from -20.0 to +20.0
    ; boost = n			; mic volume boost in dB

    ; Set the callerid for outgoing calls
    ; callerid = John Doe <555-1234>

    ; autoanswer = no		; no autoanswer on call
    ; autohangup = yes		; hangup when other party closes
    ; extension = s		; default extension to call
    ; context = default		; default context for outgoing calls
    ; language = ""		; default language

    ; Default Music on Hold class to use when this channel is placed on hold in
    ; the case that the music class is not set on the channel with
    ; Set(CHANNEL(musicclass)=whatever) in the dialplan and the peer channel
    ; putting this one on hold did not suggest a class to use.
    ;
    ; mohinterpret=default

    ; If you set overridecontext to 'yes', then the whole dial string
    ; will be interpreted as an extension, which is extremely useful
    ; to dial SIP, IAX and other extensions which use the '@' character.
    ; The default is 'no' just for backward compatibility, but the
    ; suggestion is to change it.
    ; overridecontext = no	; if 'no', the last @ will start the context
				; if 'yes' the whole string is an extension.

    ; low level device parameters in case you have problems with the
    ; device driver on your operating system. You should not touch these
    ; unless you know what you are doing.
    ; queuesize = 10		; frames in device driver
    ; frags = 8			; argument to SETFRAGMENT

    ;------------------------------ JITTER BUFFER CONFIGURATION --------------------------
    ; jbenable = yes              ; Enables the use of a jitterbuffer on the receiving side of an
                                  ; OSS channel. Defaults to "no". An enabled jitterbuffer will
                                  ; be used only if the sending side can create and the receiving
                                  ; side can not accept jitter. The OSS channel can't accept jitter,
                                  ; thus an enabled jitterbuffer on the receive OSS side will always
                                  ; be used if the sending side can create jitter.

    ; jbmaxsize = 200             ; Max length of the jitterbuffer in milliseconds.

    ; jbresyncthreshold = 1000    ; Jump in the frame timestamps over which the jitterbuffer is
                                  ; resynchronized. Useful to improve the quality of the voice, with
                                  ; big jumps in/broken timestamps, usualy sent from exotic devices
                                  ; and programs. Defaults to 1000.

    ; jbimpl = fixed              ; Jitterbuffer implementation, used on the receiving side of an OSS
                                  ; channel. Two implementations are currenlty available - "fixed"
                                  ; (with size always equals to jbmax-size) and "adaptive" (with
                                  ; variable size, actually the new jb of IAX2). Defaults to fixed.

    ; jblog = no                  ; Enables jitterbuffer frame logging. Defaults to "no".
    ;-----------------------------------------------------------------------------------

[card1]
    ; device = /dev/dsp1	; alternate device

END_CONFIG

.. and so on for the other cards.

 */

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
#define	TRYOPEN	1				/* try to open on startup */
#endif
#define	O_CLOSE	0x444			/* special 'close' mode for device */
/* Which device to use */
#if defined( __OpenBSD__ ) || defined( __NetBSD__ )
#define DEV_DSP "/dev/audio"
#else
#define DEV_DSP "/dev/dsp"
#endif

static char *config = "oss.conf";	/* default config file */

static int oss_debug;

/*!
 * \brief descriptor for one of our channels.
 *
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_oss_pvt {
	struct chan_oss_pvt *next;

	char *name;
	int total_blocks;			/*!< total blocks in the output device */
	int sounddev;
	enum { M_UNSET, M_FULL, M_READ, M_WRITE } duplex;
	int autoanswer;             /*!< Boolean: whether to answer the immediately upon calling */
	int autohangup;             /*!< Boolean: whether to hangup the call when the remote end hangs up */
	int hookstate;              /*!< Boolean: 1 if offhook; 0 if onhook */
	char *mixer_cmd;			/*!< initial command to issue to the mixer */
	unsigned int queuesize;		/*!< max fragments in queue */
	unsigned int frags;			/*!< parameter for SETFRAGMENT */

	int warned;					/*!< various flags used for warnings */
#define WARN_used_blocks	1
#define WARN_speed		2
#define WARN_frag		4
	int w_errors;				/*!< overfull in the write path */
	struct timeval lastopen;

	int overridecontext;
	int mute;

	/*! boost support. BOOST_SCALE * 10 ^(BOOST_MAX/20) must
	 *  be representable in 16 bits to avoid overflows.
	 */
#define	BOOST_SCALE	(1<<9)
#define	BOOST_MAX	40			/*!< slightly less than 7 bits */
	int boost;					/*!< input boost, scaled by BOOST_SCALE */
	char device[64];			/*!< device to open */

	pthread_t sthread;

	struct ast_channel *owner;

	struct video_desc *env;			/*!< parameters for video support */

	char ext[AST_MAX_EXTENSION];
	char ctx[AST_MAX_CONTEXT];
	char language[MAX_LANGUAGE];
	char cid_name[256];         /*!< Initial CallerID name */
	char cid_num[256];          /*!< Initial CallerID number  */
	char mohinterpret[MAX_MUSICCLASS];

	/*! buffers used in oss_write */
	char oss_write_buf[FRAME_SIZE * 2];
	int oss_write_dst;
	/*! buffers used in oss_read - AST_FRIENDLY_OFFSET space for headers
	 *  plus enough room for a full frame
	 */
	char oss_read_buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	int readpos;				/*!< read position above */
	struct ast_frame read_f;	/*!< returned by oss_read */
};

/*! forward declaration */
static struct chan_oss_pvt *find_desc(const char *dev);

static char *oss_active;	 /*!< the active device */

/*! \brief return the pointer to the video descriptor */
struct video_desc *get_video_desc(struct ast_channel *c)
{
	struct chan_oss_pvt *o = c ? c->tech_pvt : find_desc(oss_active);
	return o ? o->env : NULL;
}
static struct chan_oss_pvt oss_default = {
	.sounddev = -1,
	.duplex = M_UNSET,			/* XXX check this */
	.autoanswer = 1,
	.autohangup = 1,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.ext = "s",
	.ctx = "default",
	.readpos = AST_FRIENDLY_OFFSET,	/* start here on reads */
	.lastopen = { 0, 0 },
	.boost = BOOST_SCALE,
};


static int setformat(struct chan_oss_pvt *o, int mode);

static struct ast_channel *oss_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor,
									   void *data, int *cause);
static int oss_digit_begin(struct ast_channel *c, char digit);
static int oss_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int oss_text(struct ast_channel *c, const char *text);
static int oss_hangup(struct ast_channel *c);
static int oss_answer(struct ast_channel *c);
static struct ast_frame *oss_read(struct ast_channel *chan);
static int oss_call(struct ast_channel *c, char *dest, int timeout);
static int oss_write(struct ast_channel *chan, struct ast_frame *f);
static int oss_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int oss_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static char tdesc[] = "OSS Console Channel Driver";

/* cannot do const because need to update some fields at runtime */
static struct ast_channel_tech oss_tech = {
	.type = "Console",
	.description = tdesc,
	.requester = oss_request,
	.send_digit_begin = oss_digit_begin,
	.send_digit_end = oss_digit_end,
	.send_text = oss_text,
	.hangup = oss_hangup,
	.answer = oss_answer,
	.read = oss_read,
	.call = oss_call,
	.write = oss_write,
	.write_video = console_write_video,
	.indicate = oss_indicate,
	.fixup = oss_fixup,
};

/*!
 * \brief returns a pointer to the descriptor with the given name
 */
static struct chan_oss_pvt *find_desc(const char *dev)
{
	struct chan_oss_pvt *o = NULL;

	if (!dev)
		ast_log(LOG_WARNING, "null dev\n");

	for (o = oss_default.next; o && o->name && dev && strcmp(o->name, dev) != 0; o = o->next);

	if (!o)
		ast_log(LOG_WARNING, "could not find <%s>\n", dev ? dev : "--no-device--");

	return o;
}

/* !
 * \brief split a string in extension-context, returns pointers to malloc'ed
 *        strings.
 *
 * If we do not have 'overridecontext' then the last @ is considered as
 * a context separator, and the context is overridden.
 * This is usually not very necessary as you can play with the dialplan,
 * and it is nice not to need it because you have '@' in SIP addresses.
 *
 * \return the buffer address.
 */
static char *ast_ext_ctx(const char *src, char **ext, char **ctx)
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (ext == NULL || ctx == NULL)
		return NULL;			/* error */

	*ext = *ctx = NULL;

	if (src && *src != '\0')
		*ext = ast_strdup(src);

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

/*!
 * \brief Returns the number of blocks used in the audio output channel
 */
static int used_blocks(struct chan_oss_pvt *o)
{
	struct audio_buf_info info;

	if (ioctl(o->sounddev, SNDCTL_DSP_GETOSPACE, &info)) {
		if (!(o->warned & WARN_used_blocks)) {
			ast_log(LOG_WARNING, "Error reading output space\n");
			o->warned |= WARN_used_blocks;
		}
		return 1;
	}

	if (o->total_blocks == 0) {
		if (0)					/* debugging */
			ast_log(LOG_WARNING, "fragtotal %d size %d avail %d\n", info.fragstotal, info.fragsize, info.fragments);
		o->total_blocks = info.fragments;
	}

	return o->total_blocks - info.fragments;
}

/*! Write an exactly FRAME_SIZE sized frame */
static int soundcard_writeframe(struct chan_oss_pvt *o, short *data)
{
	int res;

	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	if (o->sounddev < 0)
		return 0;				/* not fatal */
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * XXX in some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) {	/* no room to write a block */
		if (o->w_errors++ == 0 && (oss_debug & 0x4))
			ast_log(LOG_WARNING, "write: used %d blocks (%d)\n", res, o->w_errors);
		return 0;
	}
	o->w_errors = 0;
	return write(o->sounddev, (void *)data, FRAME_SIZE * 2);
}

/*!
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
	if (mode == O_CLOSE)		/* we are done */
		return 0;
	if (ast_tvdiff_ms(ast_tvnow(), o->lastopen) < 1000)
		return -1;				/* don't open too often */
	o->lastopen = ast_tvnow();
	fd = o->sounddev = open(o->device, mode | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to re-open DSP device %s: %s\n", o->device, strerror(errno));
		return -1;
	}
	if (o->owner)
		ast_channel_set_fd(o->owner, 0, fd);

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
		/* Check to see if duplex set (FreeBSD Bug) */
		res = ioctl(fd, SNDCTL_DSP_GETCAPS, &fmt);
		if (res == 0 && (fmt & DSP_CAP_DUPLEX)) {
			ast_verb(2, "Console is full duplex\n");
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
	fmt = desired = DEFAULT_SAMPLE_RATE;	/* 8000 Hz desired */
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
static int oss_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

static int oss_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", 
		digit, duration);
	return 0;
}

static int oss_text(struct ast_channel *c, const char *text)
{
	/* print received messages */
	ast_verbose(" << Console Received text %s >> \n", text);
	return 0;
}

/*!
 * \brief handler for incoming calls. Either autoanswer, or start ringing
 */
static int oss_call(struct ast_channel *c, char *dest, int timeout)
{
	struct chan_oss_pvt *o = c->tech_pvt;
	struct ast_frame f = { AST_FRAME_CONTROL, };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(name);
		AST_APP_ARG(flags);
	);
	char *parse = ast_strdupa(dest);

	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	ast_verbose(" << Call to device '%s' dnid '%s' rdnis '%s' on console from '%s' <%s> >>\n",
		dest,
		S_OR(c->dialed.number.str, ""),
		S_COR(c->redirecting.from.number.valid, c->redirecting.from.number.str, ""),
		S_COR(c->caller.id.name.valid, c->caller.id.name.str, ""),
		S_COR(c->caller.id.number.valid, c->caller.id.number.str, ""));
	if (!ast_strlen_zero(args.flags) && strcasecmp(args.flags, "answer") == 0) {
		f.subclass.integer = AST_CONTROL_ANSWER;
		ast_queue_frame(c, &f);
	} else if (!ast_strlen_zero(args.flags) && strcasecmp(args.flags, "noanswer") == 0) {
		f.subclass.integer = AST_CONTROL_RINGING;
		ast_queue_frame(c, &f);
		ast_indicate(c, AST_CONTROL_RINGING);
	} else if (o->autoanswer) {
		ast_verbose(" << Auto-answered >> \n");
		f.subclass.integer = AST_CONTROL_ANSWER;
		ast_queue_frame(c, &f);
		o->hookstate = 1;
	} else {
		ast_verbose("<< Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
		f.subclass.integer = AST_CONTROL_RINGING;
		ast_queue_frame(c, &f);
		ast_indicate(c, AST_CONTROL_RINGING);
	}
	return 0;
}

/*!
 * \brief remote side answered the phone
 */
static int oss_answer(struct ast_channel *c)
{
	struct chan_oss_pvt *o = c->tech_pvt;
	ast_verbose(" << Console call has been answered >> \n");
	ast_setstate(c, AST_STATE_UP);
	o->hookstate = 1;
	return 0;
}

static int oss_hangup(struct ast_channel *c)
{
	struct chan_oss_pvt *o = c->tech_pvt;

	c->tech_pvt = NULL;
	o->owner = NULL;
	ast_verbose(" << Hangup on console >> \n");
	console_video_uninit(o->env);
	ast_module_unref(ast_module_info->self);
	if (o->hookstate) {
		if (o->autoanswer || o->autohangup) {
			/* Assume auto-hangup too */
			o->hookstate = 0;
			setformat(o, O_CLOSE);
		}
	}
	return 0;
}

/*! \brief used for data coming from the network */
static int oss_write(struct ast_channel *c, struct ast_frame *f)
{
	int src;
	struct chan_oss_pvt *o = c->tech_pvt;

	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */
	src = 0;					/* read position into f->data */
	while (src < f->datalen) {
		/* Compute spare room in the buffer */
		int l = sizeof(o->oss_write_buf) - o->oss_write_dst;

		if (f->datalen - src >= l) {	/* enough to fill a frame */
			memcpy(o->oss_write_buf + o->oss_write_dst, f->data.ptr + src, l);
			soundcard_writeframe(o, (short *) o->oss_write_buf);
			src += l;
			o->oss_write_dst = 0;
		} else {				/* copy residue */
			l = f->datalen - src;
			memcpy(o->oss_write_buf + o->oss_write_dst, f->data.ptr + src, l);
			src += l;			/* but really, we are done */
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

	/* XXX can be simplified returning &ast_null_frame */
	/* prepare a NULL frame in case we don't have enough data to return */
	memset(f, '\0', sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = oss_tech.type;

	res = read(o->sounddev, o->oss_read_buf + o->readpos, sizeof(o->oss_read_buf) - o->readpos);
	if (res < 0)				/* audio data not ready, return a NULL frame */
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
	ast_format_set(&f->subclass.format, AST_FORMAT_SLINEAR, 0);
	f->samples = FRAME_SIZE;
	f->datalen = FRAME_SIZE * 2;
	f->data.ptr = o->oss_read_buf + AST_FRIENDLY_OFFSET;
	if (o->boost != BOOST_SCALE) {	/* scale and clip values */
		int i, x;
		int16_t *p = (int16_t *) f->data.ptr;
		for (i = 0; i < f->samples; i++) {
			x = (p[i] * o->boost) / BOOST_SCALE;
			if (x > 32767)
				x = 32767;
			else if (x < -32768)
				x = -32768;
			p[i] = x;
		}
	}

	f->offset = AST_FRIENDLY_OFFSET;
	return f;
}

static int oss_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_oss_pvt *o = newchan->tech_pvt;
	o->owner = newchan;
	return 0;
}

static int oss_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_oss_pvt *o = c->tech_pvt;
	int res = 0;

	switch (cond) {
	case AST_CONTROL_BUSY:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_RINGING:
	case -1:
		res = -1;
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_SRCUPDATE:
		break;
	case AST_CONTROL_HOLD:
		ast_verbose(" << Console Has Been Placed on Hold >> \n");
		ast_moh_start(c, data, o->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_verbose(" << Console Has Been Retrieved from Hold >> \n");
		ast_moh_stop(c);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, c->name);
		return -1;
	}

	return res;
}

/*!
 * \brief allocate a new channel.
 */
static struct ast_channel *oss_new(struct chan_oss_pvt *o, char *ext, char *ctx, int state, const char *linkedid)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, o->cid_num, o->cid_name, "", ext, ctx, linkedid, 0, "Console/%s", o->device + 5);
	if (c == NULL)
		return NULL;
	c->tech = &oss_tech;
	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	ast_channel_set_fd(c, 0, o->sounddev); /* -1 if device closed, override later */

	ast_format_set(&c->readformat, AST_FORMAT_SLINEAR, 0);
	ast_format_set(&c->writeformat, AST_FORMAT_SLINEAR, 0);
	ast_format_cap_add(c->nativeformats, &c->readformat);

	/* if the console makes the call, add video to the offer */
	/* if (state == AST_STATE_RINGING) TODO XXX CONSOLE VIDEO IS DISABLED UNTIL IT GETS A MAINTAINER
		c->nativeformats |= console_video_formats; */

	c->tech_pvt = o;

	if (!ast_strlen_zero(o->language))
		ast_string_field_set(c, language, o->language);
	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
	if (!ast_strlen_zero(o->cid_num)) {
		c->caller.ani.number.valid = 1;
		c->caller.ani.number.str = ast_strdup(o->cid_num);
	}
	if (!ast_strlen_zero(ext)) {
		c->dialed.number.str = ast_strdup(ext);
	}

	o->owner = c;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(c, &global_jbconf);
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", c->name);
			ast_hangup(c);
			o->owner = c = NULL;
		}
	}
	console_video_start(get_video_desc(c), c); /* XXX cleanup */

	return c;
}

static struct ast_channel *oss_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause)
{
	struct ast_channel *c;
	struct chan_oss_pvt *o;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(name);
		AST_APP_ARG(flags);
	);
	char *parse = ast_strdupa(data);
	char buf[256];
	struct ast_format tmpfmt;

	AST_NONSTANDARD_APP_ARGS(args, parse, '/');
	o = find_desc(args.name);

	ast_log(LOG_WARNING, "oss_request ty <%s> data 0x%p <%s>\n", type, data, (char *) data);
	if (o == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", args.name);
		/* XXX we could default to 'dsp' perhaps ? */
		return NULL;
	}
	if (!(ast_format_cap_iscompatible(cap, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0)))) {
		ast_log(LOG_NOTICE, "Format %s unsupported\n", ast_getformatname_multiple(buf, sizeof(buf), cap));
		return NULL;
	}
	if (o->owner) {
		ast_log(LOG_NOTICE, "Already have a call (chan %p) on the OSS channel\n", o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = oss_new(o, NULL, NULL, AST_STATE_DOWN, requestor ? requestor->linkedid : NULL);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new OSS channel\n");
		return NULL;
	}
	return c;
}

static void store_config_core(struct chan_oss_pvt *o, const char *var, const char *value);

/*! Generic console command handler. Basically a wrapper for a subset
 *  of config file options which are also available from the CLI
 */
static char *console_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);
	const char *var, *value;
	switch (cmd) {
	case CLI_INIT:
		e->command = CONSOLE_VIDEO_CMDS;
		e->usage = 
			"Usage: " CONSOLE_VIDEO_CMDS "...\n"
			"       Generic handler for console commands.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < e->args)
		return CLI_SHOWUSAGE;
	if (o == NULL) {
		ast_log(LOG_WARNING, "Cannot find device %s (should not happen!)\n",
			oss_active);
		return CLI_FAILURE;
	}
	var = a->argv[e->args-1];
	value = a->argc > e->args ? a->argv[e->args] : NULL;
	if (value)      /* handle setting */
		store_config_core(o, var, value);
	if (!console_video_cli(o->env, var, a->fd))	/* print video-related values */
		return CLI_SUCCESS;
	/* handle other values */
	if (!strcasecmp(var, "device")) {
		ast_cli(a->fd, "device is [%s]\n", o->device);
	}
	return CLI_SUCCESS;
}

static char *console_autoanswer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);

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

	if (a->argc == e->args - 1) {
		ast_cli(a->fd, "Auto answer is %s.\n", o->autoanswer ? "on" : "off");
		return CLI_SUCCESS;
	}
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;
	if (o == NULL) {
		ast_log(LOG_WARNING, "Cannot find device %s (should not happen!)\n",
		    oss_active);
		return CLI_FAILURE;
	}
	if (!strcasecmp(a->argv[e->args-1], "on"))
		o->autoanswer = 1;
	else if (!strcasecmp(a->argv[e->args - 1], "off"))
		o->autoanswer = 0;
	else
		return CLI_SHOWUSAGE;
	return CLI_SUCCESS;
}

/*! \brief helper function for the answer key/cli command */
static char *console_do_answer(int fd)
{
	struct ast_frame f = { AST_FRAME_CONTROL, { AST_CONTROL_ANSWER } };
	struct chan_oss_pvt *o = find_desc(oss_active);
	if (!o->owner) {
		if (fd > -1)
			ast_cli(fd, "No one is calling us\n");
		return CLI_FAILURE;
	}
	o->hookstate = 1;
	ast_queue_frame(o->owner, &f);
	return CLI_SUCCESS;
}

/*!
 * \brief answer command from the console
 */
static char *console_answer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "console answer";
		e->usage =
			"Usage: console answer\n"
			"       Answers an incoming call on the console (OSS) channel.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;
	return console_do_answer(a->fd);
}

/*!
 * \brief Console send text CLI command
 *
 * \note concatenate all arguments into a single string. argv is NULL-terminated
 * so we can use it right away
 */
static char *console_sendtext(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);
	char buf[TEXT_SIZE];

	if (cmd == CLI_INIT) {
		e->command = "console send text";
		e->usage =
			"Usage: console send text <message>\n"
			"       Sends a text message for display on the remote terminal.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc < e->args + 1)
		return CLI_SHOWUSAGE;
	if (!o->owner) {
		ast_cli(a->fd, "Not in a call\n");
		return CLI_FAILURE;
	}
	ast_join(buf, sizeof(buf) - 1, a->argv + e->args);
	if (!ast_strlen_zero(buf)) {
		struct ast_frame f = { 0, };
		int i = strlen(buf);
		buf[i] = '\n';
		f.frametype = AST_FRAME_TEXT;
		f.subclass.integer = 0;
		f.data.ptr = buf;
		f.datalen = i + 1;
		ast_queue_frame(o->owner, &f);
	}
	return CLI_SUCCESS;
}

static char *console_hangup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (cmd == CLI_INIT) {
		e->command = "console hangup";
		e->usage =
			"Usage: console hangup\n"
			"       Hangs up any call currently placed on the console.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;
	if (!o->owner && !o->hookstate) { /* XXX maybe only one ? */
		ast_cli(a->fd, "No call to hang up\n");
		return CLI_FAILURE;
	}
	o->hookstate = 0;
	if (o->owner)
		ast_queue_hangup_with_cause(o->owner, AST_CAUSE_NORMAL_CLEARING);
	setformat(o, O_CLOSE);
	return CLI_SUCCESS;
}

static char *console_flash(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_frame f = { AST_FRAME_CONTROL, { AST_CONTROL_FLASH } };
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (cmd == CLI_INIT) {
		e->command = "console flash";
		e->usage =
			"Usage: console flash\n"
			"       Flashes the call currently placed on the console.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;
	if (!o->owner) {			/* XXX maybe !o->hookstate too ? */
		ast_cli(a->fd, "No call to flash\n");
		return CLI_FAILURE;
	}
	o->hookstate = 0;
	if (o->owner)
		ast_queue_frame(o->owner, &f);
	return CLI_SUCCESS;
}

static char *console_dial(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *s = NULL;
	char *mye = NULL, *myc = NULL;
	struct chan_oss_pvt *o = find_desc(oss_active);

	if (cmd == CLI_INIT) {
		e->command = "console dial";
		e->usage =
			"Usage: console dial [extension[@context]]\n"
			"       Dials a given extension (and context if specified)\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc > e->args + 1)
		return CLI_SHOWUSAGE;
	if (o->owner) {	/* already in a call */
		int i;
		struct ast_frame f = { AST_FRAME_DTMF, { 0 } };
		const char *s;

		if (a->argc == e->args) {	/* argument is mandatory here */
			ast_cli(a->fd, "Already in a call. You can only dial digits until you hangup.\n");
			return CLI_FAILURE;
		}
		s = a->argv[e->args];
		/* send the string one char at a time */
		for (i = 0; i < strlen(s); i++) {
			f.subclass.integer = s[i];
			ast_queue_frame(o->owner, &f);
		}
		return CLI_SUCCESS;
	}
	/* if we have an argument split it into extension and context */
	if (a->argc == e->args + 1)
		s = ast_ext_ctx(a->argv[e->args], &mye, &myc);
	/* supply default values if needed */
	if (mye == NULL)
		mye = o->ext;
	if (myc == NULL)
		myc = o->ctx;
	if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
		o->hookstate = 1;
		oss_new(o, mye, myc, AST_STATE_RINGING, NULL);
	} else
		ast_cli(a->fd, "No such extension '%s' in context '%s'\n", mye, myc);
	if (s)
		ast_free(s);
	return CLI_SUCCESS;
}

static char *console_mute(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);
	const char *s;
	int toggle = 0;
	
	if (cmd == CLI_INIT) {
		e->command = "console {mute|unmute} [toggle]";
		e->usage =
			"Usage: console {mute|unmute} [toggle]\n"
			"       Mute/unmute the microphone.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE)
		return NULL;

	if (a->argc > e->args)
		return CLI_SHOWUSAGE;
	if (a->argc == e->args) {
		if (strcasecmp(a->argv[e->args-1], "toggle"))
			return CLI_SHOWUSAGE;
		toggle = 1;
	}
	s = a->argv[e->args-2];
	if (!strcasecmp(s, "mute"))
		o->mute = toggle ? !o->mute : 1;
	else if (!strcasecmp(s, "unmute"))
		o->mute = toggle ? !o->mute : 0;
	else
		return CLI_SHOWUSAGE;
	ast_cli(a->fd, "Console mic is %s\n", o->mute ? "off" : "on");
	return CLI_SUCCESS;
}

static char *console_transfer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);
	struct ast_channel *b = NULL;
	char *tmp, *ext, *ctx;

	switch (cmd) {
	case CLI_INIT:
		e->command = "console transfer";
		e->usage =
			"Usage: console transfer <extension>[@context]\n"
			"       Transfers the currently connected call to the given extension (and\n"
			"       context if specified)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;
	if (o == NULL)
		return CLI_FAILURE;
	if (o->owner == NULL || (b = ast_bridged_channel(o->owner)) == NULL) {
		ast_cli(a->fd, "There is no call to transfer\n");
		return CLI_SUCCESS;
	}

	tmp = ast_ext_ctx(a->argv[2], &ext, &ctx);
	if (ctx == NULL)			/* supply default context if needed */
		ctx = o->owner->context;
	if (!ast_exists_extension(b, ctx, ext, 1,
		S_COR(b->caller.id.number.valid, b->caller.id.number.str, NULL))) {
		ast_cli(a->fd, "No such extension exists\n");
	} else {
		ast_cli(a->fd, "Whee, transferring %s to %s@%s.\n", b->name, ext, ctx);
		if (ast_async_goto(b, ctx, ext, 1))
			ast_cli(a->fd, "Failed to transfer :(\n");
	}
	if (tmp)
		ast_free(tmp);
	return CLI_SUCCESS;
}

static char *console_active(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "console {set|show} active [<device>]";
		e->usage =
			"Usage: console active [device]\n"
			"       If used without a parameter, displays which device is the current\n"
			"       console.  If a device is specified, the console sound device is changed to\n"
			"       the device specified.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 3)
		ast_cli(a->fd, "active console is [%s]\n", oss_active);
	else if (a->argc != 4)
		return CLI_SHOWUSAGE;
	else {
		struct chan_oss_pvt *o;
		if (strcmp(a->argv[3], "show") == 0) {
			for (o = oss_default.next; o; o = o->next)
				ast_cli(a->fd, "device [%s] exists\n", o->name);
			return CLI_SUCCESS;
		}
		o = find_desc(a->argv[3]);
		if (o == NULL)
			ast_cli(a->fd, "No device [%s] exists\n", a->argv[3]);
		else
			oss_active = o->name;
	}
	return CLI_SUCCESS;
}

/*!
 * \brief store the boost factor
 */
static void store_boost(struct chan_oss_pvt *o, const char *s)
{
	double boost = 0;
	if (sscanf(s, "%30lf", &boost) != 1) {
		ast_log(LOG_WARNING, "invalid boost <%s>\n", s);
		return;
	}
	if (boost < -BOOST_MAX) {
		ast_log(LOG_WARNING, "boost %s too small, using %d\n", s, -BOOST_MAX);
		boost = -BOOST_MAX;
	} else if (boost > BOOST_MAX) {
		ast_log(LOG_WARNING, "boost %s too large, using %d\n", s, BOOST_MAX);
		boost = BOOST_MAX;
	}
	boost = exp(log(10) * boost / 20) * BOOST_SCALE;
	o->boost = boost;
	ast_log(LOG_WARNING, "setting boost %s to %d\n", s, o->boost);
}

static char *console_boost(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_oss_pvt *o = find_desc(oss_active);

	switch (cmd) {
	case CLI_INIT:
		e->command = "console boost";
		e->usage =
			"Usage: console boost [boost in dB]\n"
			"       Sets or display mic boost in dB\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 2)
		ast_cli(a->fd, "boost currently %5.1f\n", 20 * log10(((double) o->boost / (double) BOOST_SCALE)));
	else if (a->argc == 3)
		store_boost(o, a->argv[2]);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_oss[] = {
	AST_CLI_DEFINE(console_answer, "Answer an incoming console call"),
	AST_CLI_DEFINE(console_hangup, "Hangup a call on the console"),
	AST_CLI_DEFINE(console_flash, "Flash a call on the console"),
	AST_CLI_DEFINE(console_dial, "Dial an extension on the console"),
	AST_CLI_DEFINE(console_mute, "Disable/Enable mic input"),
	AST_CLI_DEFINE(console_transfer, "Transfer a call to a different extension"),	
	AST_CLI_DEFINE(console_cmd, "Generic console command"),
	AST_CLI_DEFINE(console_sendtext, "Send text to the remote device"),
	AST_CLI_DEFINE(console_autoanswer, "Sets/displays autoanswer"),
	AST_CLI_DEFINE(console_boost, "Sets/displays mic boost in dB"),
	AST_CLI_DEFINE(console_active, "Sets/displays active console"),
};

/*!
 * store the mixer argument from the config file, filtering possibly
 * invalid or dangerous values (the string is used as argument for
 * system("mixer %s")
 */
static void store_mixer(struct chan_oss_pvt *o, const char *s)
{
	int i;

	for (i = 0; i < strlen(s); i++) {
		if (!isalnum(s[i]) && strchr(" \t-/", s[i]) == NULL) {
			ast_log(LOG_WARNING, "Suspect char %c in mixer cmd, ignoring:\n\t%s\n", s[i], s);
			return;
		}
	}
	if (o->mixer_cmd)
		ast_free(o->mixer_cmd);
	o->mixer_cmd = ast_strdup(s);
	ast_log(LOG_WARNING, "setting mixer %s\n", s);
}

/*!
 * store the callerid components
 */
static void store_callerid(struct chan_oss_pvt *o, const char *s)
{
	ast_callerid_split(s, o->cid_name, sizeof(o->cid_name), o->cid_num, sizeof(o->cid_num));
}

static void store_config_core(struct chan_oss_pvt *o, const char *var, const char *value)
{
	CV_START(var, value);

	/* handle jb conf */
	if (!ast_jb_read_conf(&global_jbconf, var, value))
		return;

	if (!console_video_config(&o->env, var, value))
		return;	/* matched there */
	CV_BOOL("autoanswer", o->autoanswer);
	CV_BOOL("autohangup", o->autohangup);
	CV_BOOL("overridecontext", o->overridecontext);
	CV_STR("device", o->device);
	CV_UINT("frags", o->frags);
	CV_UINT("debug", oss_debug);
	CV_UINT("queuesize", o->queuesize);
	CV_STR("context", o->ctx);
	CV_STR("language", o->language);
	CV_STR("mohinterpret", o->mohinterpret);
	CV_STR("extension", o->ext);
	CV_F("mixer", store_mixer(o, value));
	CV_F("callerid", store_callerid(o, value))  ;
	CV_F("boost", store_boost(o, value));

	CV_END;
}

/*!
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_oss_pvt *store_config(struct ast_config *cfg, char *ctg)
{
	struct ast_variable *v;
	struct chan_oss_pvt *o;

	if (ctg == NULL) {
		o = &oss_default;
		ctg = "general";
	} else {
		if (!(o = ast_calloc(1, sizeof(*o))))
			return NULL;
		*o = oss_default;
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o->name = ast_strdup("dsp");
			oss_active = o->name;
			goto openit;
		}
		o->name = ast_strdup(ctg);
	}

	strcpy(o->mohinterpret, "default");

	o->lastopen = ast_tvnow();	/* don't leave it 0 or tvdiff may wrap */
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
		store_config_core(o, v->name, v->value);
	}
	if (ast_strlen_zero(o->device))
		ast_copy_string(o->device, DEV_DSP, sizeof(o->device));
	if (o->mixer_cmd) {
		char *cmd;

		if (asprintf(&cmd, "mixer %s", o->mixer_cmd) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
		} else {
			ast_log(LOG_WARNING, "running [%s]\n", cmd);
			if (system(cmd) < 0) {
				ast_log(LOG_WARNING, "system() failed: %s\n", strerror(errno));
			}
			ast_free(cmd);
		}
	}

	/* if the config file requested to start the GUI, do it */
	if (get_gui_startup(o->env))
		console_video_start(o->env, NULL);

	if (o == &oss_default)		/* we are done with the default */
		return NULL;

openit:
#ifdef TRYOPEN
	if (setformat(o, O_RDWR) < 0) {	/* open device */
		ast_verb(1, "Device %s not detected\n", ctg);
		ast_verb(1, "Turn off OSS support by adding " "'noload=chan_oss.so' in /etc/asterisk/modules.conf\n");
		goto error;
	}
	if (o->duplex != M_FULL)
		ast_log(LOG_WARNING, "XXX I don't work right with non " "full-duplex sound cards XXX\n");
#endif /* TRYOPEN */

	/* link into list of devices */
	if (o != &oss_default) {
		o->next = oss_default.next;
		oss_default.next = o;
	}
	return o;

#ifdef TRYOPEN
error:
	if (o != &oss_default)
		ast_free(o);
	return NULL;
#endif
}

static int load_module(void)
{
	struct ast_config *cfg = NULL;
	char *ctg = NULL;
	struct ast_flags config_flags = { 0 };
	struct ast_format tmpfmt;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* load config file */
	if (!(cfg = ast_config_load(config, config_flags))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	do {
		store_config(cfg, ctg);
	} while ( (ctg = ast_category_browse(cfg, ctg)) != NULL);

	ast_config_destroy(cfg);

	if (find_desc(oss_active) == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", oss_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(oss_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_add(oss_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));

	/* TODO XXX CONSOLE VIDEO IS DISABLE UNTIL IT HAS A MAINTAINER
	 * add console_video_formats to oss_tech.capabilities once this occurs. */

	if (ast_channel_register(&oss_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'OSS'\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cli_oss, ARRAY_LEN(cli_oss));

	return AST_MODULE_LOAD_SUCCESS;
}


static int unload_module(void)
{
	struct chan_oss_pvt *o, *next;

	ast_channel_unregister(&oss_tech);
	ast_cli_unregister_multiple(cli_oss, ARRAY_LEN(cli_oss));

	o = oss_default.next;
	while (o) {
		close(o->sounddev);
		if (o->owner)
			ast_softhangup(o->owner, AST_SOFTHANGUP_APPUNLOAD);
		if (o->owner)
			return -1;
		next = o->next;
		ast_free(o->name);
		ast_free(o);
		o = next;
	}
	oss_tech.capabilities = ast_format_cap_destroy(oss_tech.capabilities);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "OSS Console Channel Driver");
