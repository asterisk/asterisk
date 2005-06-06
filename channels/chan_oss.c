/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Use /dev/dsp as a channel, and the console to command it :).
 *
 * The full-duplex "simulation" is pretty weak.  This is generally a 
 * VERY BADLY WRITTEN DRIVER so please don't use it as a model for
 * writing a driver.
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

#include "busy.h"
#include "ringtone.h"
#include "ring10.h"
#include "answer.h"

/* Which device to use */
#if defined( __OpenBSD__ ) || defined( __NetBSD__ )
#define DEV_DSP "/dev/audio"
#else
#define DEV_DSP "/dev/dsp"
#endif

/* Lets use 160 sample frames, just like GSM.  */
#define FRAME_SIZE 160

/* When you set the frame size, you have to come up with
   the right buffer format as well. */
/* 5 64-byte frames = one frame */
#define BUFFER_FMT ((buffersize * 10) << 16) | (0x0006);

/* Don't switch between read/write modes faster than every 300 ms */
#define MIN_SWITCH_TIME 600

static struct timeval lasttime;

static int usecnt;
static int silencesuppression = 0;
static int silencethreshold = 1000;
static int playbackonly = 0;


AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static const char type[] = "Console";
static const char desc[] = "OSS Console Channel Driver";
static const char tdesc[] = "OSS Console Channel Driver";
static const char config[] = "oss.conf";

static char context[AST_MAX_EXTENSION] = "default";
static char language[MAX_LANGUAGE] = "";
static char exten[AST_MAX_EXTENSION] = "s";

static int hookstate=0;

static short silence[FRAME_SIZE] = {0, };

struct sound {
	int ind;
	short *data;
	int datalen;
	int samplen;
	int silencelen;
	int repeat;
};

static struct sound sounds[] = {
	{ AST_CONTROL_RINGING, ringtone, sizeof(ringtone)/2, 16000, 32000, 1 },
	{ AST_CONTROL_BUSY, busy, sizeof(busy)/2, 4000, 4000, 1 },
	{ AST_CONTROL_CONGESTION, busy, sizeof(busy)/2, 2000, 2000, 1 },
	{ AST_CONTROL_RING, ring10, sizeof(ring10)/2, 16000, 32000, 1 },
	{ AST_CONTROL_ANSWER, answer, sizeof(answer)/2, 2200, 0, 0 },
};

/* Sound command pipe */
static int sndcmd[2];

static struct chan_oss_pvt {
	/* We only have one OSS structure -- near sighted perhaps, but it
	   keeps this driver as simple as possible -- as it should be. */
	struct ast_channel *owner;
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_EXTENSION];
} oss;

static struct ast_channel *oss_request(const char *type, int format, void *data, int *cause);
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
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = oss_request,
	.send_digit = oss_digit,
	.send_text = oss_text,
	.hangup = oss_hangup,
	.answer = oss_answer,
	.read = oss_read,
	.call = oss_call,
	.write = oss_write,
	.indicate = oss_indicate,
	.fixup = oss_fixup,
};

static int time_has_passed(void)
{
	struct timeval tv;
	int ms;
	gettimeofday(&tv, NULL);
	ms = (tv.tv_sec - lasttime.tv_sec) * 1000 +
			(tv.tv_usec - lasttime.tv_usec) / 1000;
	if (ms > MIN_SWITCH_TIME)
		return -1;
	return 0;
}

/* Number of buffers...  Each is FRAMESIZE/8 ms long.  For example
   with 160 sample frames, and a buffer size of 3, we have a 60ms buffer, 
   usually plenty. */

static pthread_t sthread;

#define MAX_BUFFER_SIZE 100
static int buffersize = 3;

static int full_duplex = 0;

/* Are we reading or writing (simulated full duplex) */
static int readmode = 1;

/* File descriptor for sound device */
static int sounddev = -1;

static int autoanswer = 1;
 
#if 0
static int calc_loudness(short *frame)
{
	int sum = 0;
	int x;
	for (x=0;x<FRAME_SIZE;x++) {
		if (frame[x] < 0)
			sum -= frame[x];
		else
			sum += frame[x];
	}
	sum = sum/FRAME_SIZE;
	return sum;
}
#endif

static int cursound = -1;
static int sampsent = 0;
static int silencelen=0;
static int offset=0;
static int nosound=0;

static int send_sound(void)
{
	short myframe[FRAME_SIZE];
	int total = FRAME_SIZE;
	short *frame = NULL;
	int amt=0;
	int res;
	int myoff;
	audio_buf_info abi;
	if (cursound > -1) {
		res = ioctl(sounddev, SNDCTL_DSP_GETOSPACE ,&abi);
		if (res) {
			ast_log(LOG_WARNING, "Unable to read output space\n");
			return -1;
		}
		/* Calculate how many samples we can send, max */
		if (total > (abi.fragments * abi.fragsize / 2)) 
			total = abi.fragments * abi.fragsize / 2;
		res = total;
		if (sampsent < sounds[cursound].samplen) {
			myoff=0;
			while(total) {
				amt = total;
				if (amt > (sounds[cursound].datalen - offset)) 
					amt = sounds[cursound].datalen - offset;
				memcpy(myframe + myoff, sounds[cursound].data + offset, amt * 2);
				total -= amt;
				offset += amt;
				sampsent += amt;
				myoff += amt;
				if (offset >= sounds[cursound].datalen)
					offset = 0;
			}
			/* Set it up for silence */
			if (sampsent >= sounds[cursound].samplen) 
				silencelen = sounds[cursound].silencelen;
			frame = myframe;
		} else {
			if (silencelen > 0) {
				frame = silence;
				silencelen -= res;
			} else {
				if (sounds[cursound].repeat) {
					/* Start over */
					sampsent = 0;
					offset = 0;
				} else {
					cursound = -1;
					nosound = 0;
				}
			}
		}
		if (frame)
			res = write(sounddev, frame, res * 2);
		if (res > 0)
			return 0;
		return res;
	}
	return 0;
}

static void *sound_thread(void *unused)
{
	fd_set rfds;
	fd_set wfds;
	int max;
	int res;
	char ign[4096];
	if (read(sounddev, ign, sizeof(sounddev)) < 0)
		ast_log(LOG_WARNING, "Read error on sound device: %s\n", strerror(errno));
	for(;;) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		max = sndcmd[0];
		FD_SET(sndcmd[0], &rfds);
		if (!oss.owner) {
			FD_SET(sounddev, &rfds);
			if (sounddev > max)
				max = sounddev;
		}
		if (cursound > -1) {
			FD_SET(sounddev, &wfds);
			if (sounddev > max)
				max = sounddev;
		}
		res = ast_select(max + 1, &rfds, &wfds, NULL, NULL);
		if (res < 1) {
			ast_log(LOG_WARNING, "select failed: %s\n", strerror(errno));
			continue;
		}
		if (FD_ISSET(sndcmd[0], &rfds)) {
			read(sndcmd[0], &cursound, sizeof(cursound));
			silencelen = 0;
			offset = 0;
			sampsent = 0;
		}
		if (FD_ISSET(sounddev, &rfds)) {
			/* Ignore read */
			if (read(sounddev, ign, sizeof(ign)) < 0)
				ast_log(LOG_WARNING, "Read error on sound device: %s\n", strerror(errno));
		}
		if (FD_ISSET(sounddev, &wfds))
			if (send_sound())
				ast_log(LOG_WARNING, "Failed to write sound\n");
	}
	/* Never reached */
	return NULL;
}

#if 0
static int silence_suppress(short *buf)
{
#define SILBUF 3
	int loudness;
	static int silentframes = 0;
	static char silbuf[FRAME_SIZE * 2 * SILBUF];
	static int silbufcnt=0;
	if (!silencesuppression)
		return 0;
	loudness = calc_loudness((short *)(buf));
	if (option_debug)
		ast_log(LOG_DEBUG, "loudness is %d\n", loudness);
	if (loudness < silencethreshold) {
		silentframes++;
		silbufcnt++;
		/* Keep track of the last few bits of silence so we can play
		   them as lead-in when the time is right */
		if (silbufcnt >= SILBUF) {
			/* Make way for more buffer */
			memmove(silbuf, silbuf + FRAME_SIZE * 2, FRAME_SIZE * 2 * (SILBUF - 1));
			silbufcnt--;
		}
		memcpy(silbuf + FRAME_SIZE * 2 * silbufcnt, buf, FRAME_SIZE * 2);
		if (silentframes > 10) {
			/* We've had plenty of silence, so compress it now */
			return 1;
		}
	} else {
		silentframes=0;
		/* Write any buffered silence we have, it may have something
		   important */
		if (silbufcnt) {
			write(sounddev, silbuf, silbufcnt * FRAME_SIZE);
			silbufcnt = 0;
		}
	}
	return 0;
}
#endif

static int setformat(void)
{
	int fmt, desired, res, fd = sounddev;
	static int warnedalready = 0;
	static int warnedalready2 = 0;

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
	res = ioctl(fd, SNDCTL_DSP_SETDUPLEX, 0);
	
	/* Check to see if duplex set (FreeBSD Bug)*/
	res = ioctl(fd, SNDCTL_DSP_GETCAPS, &fmt);
	
	if ((fmt & DSP_CAP_DUPLEX) && !res) {
		if (option_verbose > 1) 
			ast_verbose(VERBOSE_PREFIX_2 "Console is full duplex\n");
		full_duplex = -1;
	}
	fmt = 0;
	res = ioctl(fd, SNDCTL_DSP_STEREO, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	/* 8000 Hz desired */
	desired = 8000;
	fmt = desired;
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	if (fmt != desired) {
		if (!warnedalready++)
			ast_log(LOG_WARNING, "Requested %d Hz, got %d Hz -- sound may be choppy\n", desired, fmt);
	}
#if 1
	fmt = BUFFER_FMT;
	res = ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
	if (res < 0) {
		if (!warnedalready2++)
			ast_log(LOG_WARNING, "Unable to set fragment size -- sound may be choppy\n");
	}
#endif
	return 0;
}

static int soundcard_setoutput(int force)
{
	/* Make sure the soundcard is in output mode.  */
	int fd = sounddev;
	if (full_duplex || (!readmode && !force))
		return 0;
	readmode = 0;
	if (force || time_has_passed()) {
		ioctl(sounddev, SNDCTL_DSP_RESET, 0);
		/* Keep the same fd reserved by closing the sound device and copying stdin at the same
		   time. */
		/* dup2(0, sound); */ 
		close(sounddev);
		fd = open(DEV_DSP, O_WRONLY |O_NONBLOCK);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to re-open DSP device: %s\n", strerror(errno));
			return -1;
		}
		/* dup2 will close the original and make fd be sound */
		if (dup2(fd, sounddev) < 0) {
			ast_log(LOG_WARNING, "dup2() failed: %s\n", strerror(errno));
			return -1;
		}
		if (setformat()) {
			return -1;
		}
		return 0;
	}
	return 1;
}

static int soundcard_setinput(int force)
{
	int fd = sounddev;
	if (full_duplex || (readmode && !force))
		return 0;
	readmode = -1;
	if (force || time_has_passed()) {
		ioctl(sounddev, SNDCTL_DSP_RESET, 0);
		close(sounddev);
		/* dup2(0, sound); */
		fd = open(DEV_DSP, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to re-open DSP device: %s\n", strerror(errno));
			return -1;
		}
		/* dup2 will close the original and make fd be sound */
		if (dup2(fd, sounddev) < 0) {
			ast_log(LOG_WARNING, "dup2() failed: %s\n", strerror(errno));
			return -1;
		}
		if (setformat()) {
			return -1;
		}
		return 0;
	}
	return 1;
}

static int soundcard_init(void)
{
	/* Assume it's full duplex for starters */
	int fd = open(DEV_DSP, 	O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to open %s: %s\n", DEV_DSP, strerror(errno));
		return fd;
	}
	gettimeofday(&lasttime, NULL);
	sounddev = fd;
	setformat();
	if (!full_duplex) 
		soundcard_setinput(1);
	return sounddev;
}

static int oss_digit(struct ast_channel *c, char digit)
{
	ast_verbose( " << Console Received digit %c >> \n", digit);
	return 0;
}

static int oss_text(struct ast_channel *c, const char *text)
{
	ast_verbose( " << Console Received text %s >> \n", text);
	return 0;
}

static int oss_call(struct ast_channel *c, char *dest, int timeout)
{
	int res = 3;
	struct ast_frame f = { 0, };
	ast_verbose( " << Call placed to '%s' on console >> \n", dest);
	if (autoanswer) {
		ast_verbose( " << Auto-answered >> \n" );
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_ANSWER;
		ast_queue_frame(c, &f);
	} else {
		nosound = 1;
		ast_verbose( " << Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_RINGING;
		ast_queue_frame(c, &f);
		write(sndcmd[1], &res, sizeof(res));
	}
	return 0;
}

static void answer_sound(void)
{
	int res;
	nosound = 1;
	res = 4;
	write(sndcmd[1], &res, sizeof(res));
	
}

static int oss_answer(struct ast_channel *c)
{
	ast_verbose( " << Console call has been answered >> \n");
	answer_sound();
	ast_setstate(c, AST_STATE_UP);
	cursound = -1;
	nosound=0;
	return 0;
}

static int oss_hangup(struct ast_channel *c)
{
	int res = 0;
	cursound = -1;
	c->tech_pvt = NULL;
	oss.owner = NULL;
	ast_verbose( " << Hangup on console >> \n");
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
	if (hookstate) {
		if (autoanswer) {
			/* Assume auto-hangup too */
			hookstate = 0;
		} else {
			/* Make congestion noise */
			res = 2;
			write(sndcmd[1], &res, sizeof(res));
		}
	}
	return 0;
}

static int soundcard_writeframe(short *data)
{	
	/* Write an exactly FRAME_SIZE sized of frame */
	static int bufcnt = 0;
	static short buffer[FRAME_SIZE * MAX_BUFFER_SIZE * 5];
	struct audio_buf_info info;
	int res;
	int fd = sounddev;
	static int warned=0;
	if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info)) {
		if (!warned)
			ast_log(LOG_WARNING, "Error reading output space\n");
		bufcnt = buffersize;
		warned++;
	}
	if ((info.fragments >= buffersize * 5) && (bufcnt == buffersize)) {
		/* We've run out of stuff, buffer again */
		bufcnt = 0;
	}
	if (bufcnt == buffersize) {
		/* Write sample immediately */
		res = write(fd, ((void *)data), FRAME_SIZE * 2);
	} else {
		/* Copy the data into our buffer */
		res = FRAME_SIZE * 2;
		memcpy(buffer + (bufcnt * FRAME_SIZE), data, FRAME_SIZE * 2);
		bufcnt++;
		if (bufcnt == buffersize) {
			res = write(fd, ((void *)buffer), FRAME_SIZE * 2 * buffersize);
		}
	}
	return res;
}


static int oss_write(struct ast_channel *chan, struct ast_frame *f)
{
	int res;
	static char sizbuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int pos;
	/* Immediately return if no sound is enabled */
	if (nosound)
		return 0;
	/* Stop any currently playing sound */
	cursound = -1;
	if (!full_duplex && !playbackonly) {
		/* If we're half duplex, we have to switch to read mode
		   to honor immediate needs if necessary.  But if we are in play
		   back only mode, then we don't switch because the console
		   is only being used one way -- just to playback something. */
		res = soundcard_setinput(1);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set device to input mode\n");
			return -1;
		}
		return 0;
	}
	res = soundcard_setoutput(0);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set output device\n");
		return -1;
	} else if (res > 0) {
		/* The device is still in read mode, and it's too soon to change it,
		   so just pretend we wrote it */
		return 0;
	}
	/* We have to digest the frame in 160-byte portions */
	if (f->datalen > sizeof(sizbuf) - sizpos) {
		ast_log(LOG_WARNING, "Frame too large\n");
		return -1;
	}
	memcpy(sizbuf + sizpos, f->data, f->datalen);
	len += f->datalen;
	pos = 0;
	while(len - pos > FRAME_SIZE * 2) {
		soundcard_writeframe((short *)(sizbuf + pos));
		pos += FRAME_SIZE * 2;
	}
	if (len - pos) 
		memmove(sizbuf, sizbuf + pos, len - pos);
	sizpos = len - pos;
	return 0;
}

static struct ast_frame *oss_read(struct ast_channel *chan)
{
	static struct ast_frame f;
	static char buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	static int readpos = 0;
	int res;
	
#if 0
	ast_log(LOG_DEBUG, "oss_read()\n");
#endif
		
	f.frametype = AST_FRAME_NULL;
	f.subclass = 0;
	f.samples = 0;
	f.datalen = 0;
	f.data = NULL;
	f.offset = 0;
	f.src = type;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;
	
	res = soundcard_setinput(0);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set input mode\n");
		return NULL;
	}
	if (res > 0) {
		/* Theoretically shouldn't happen, but anyway, return a NULL frame */
		return &f;
	}
	res = read(sounddev, buf + AST_FRIENDLY_OFFSET + readpos, FRAME_SIZE * 2 - readpos);
	if (res < 0) {
		ast_log(LOG_WARNING, "Error reading from sound device (If you're running 'artsd' then kill it): %s\n", strerror(errno));
#if 0
		CRASH;
#endif		
		return NULL;
	}
	readpos += res;
	
	if (readpos >= FRAME_SIZE * 2) {
		/* A real frame */
		readpos = 0;
		if (chan->_state != AST_STATE_UP) {
			/* Don't transmit unless it's up */
			return &f;
		}
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.samples = FRAME_SIZE;
		f.datalen = FRAME_SIZE * 2;
		f.data = buf + AST_FRIENDLY_OFFSET;
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = type;
		f.mallocd = 0;
		f.delivery.tv_sec = 0;
		f.delivery.tv_usec = 0;
#if 0
		{ static int fd = -1;
		  if (fd < 0)
		  	fd = open("output.raw", O_RDWR | O_TRUNC | O_CREAT);
		  write(fd, f.data, f.datalen);
		}
#endif		
	}
	return &f;
}

static int oss_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_oss_pvt *p = newchan->tech_pvt;
	p->owner = newchan;
	return 0;
}

static int oss_indicate(struct ast_channel *chan, int cond)
{
	int res;
	switch(cond) {
	case AST_CONTROL_BUSY:
		res = 1;
		break;
	case AST_CONTROL_CONGESTION:
		res = 2;
		break;
	case AST_CONTROL_RINGING:
		res = 0;
		break;
	case -1:
		cursound = -1;
		return 0;
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, chan->name);
		return -1;
	}
	if (res > -1) {
		write(sndcmd[1], &res, sizeof(res));
	}
	return 0;	
}

static struct ast_channel *oss_new(struct chan_oss_pvt *p, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		tmp->tech = &oss_tech;
		snprintf(tmp->name, sizeof(tmp->name), "OSS/%s", DEV_DSP + 5);
		tmp->type = type;
		tmp->fds[0] = sounddev;
		tmp->nativeformats = AST_FORMAT_SLINEAR;
		tmp->readformat = AST_FORMAT_SLINEAR;
		tmp->writeformat = AST_FORMAT_SLINEAR;
		tmp->tech_pvt = p;
		if (strlen(p->context))
			strncpy(tmp->context, p->context, sizeof(tmp->context)-1);
		if (strlen(p->exten))
			strncpy(tmp->exten, p->exten, sizeof(tmp->exten)-1);
		if (strlen(language))
			strncpy(tmp->language, language, sizeof(tmp->language)-1);
		p->owner = tmp;
		ast_setstate(tmp, state);
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	}
	return tmp;
}

static struct ast_channel *oss_request(const char *type, int format, void *data, int *cause)
{
	int oldformat = format;
	struct ast_channel *tmp;
	format &= AST_FORMAT_SLINEAR;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of format '%d'\n", oldformat);
		return NULL;
	}
	if (oss.owner) {
		ast_log(LOG_NOTICE, "Already have a call on the OSS channel\n");
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	tmp= oss_new(&oss, AST_STATE_DOWN);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to create new OSS channel\n");
	}
	return tmp;
}

static int console_autoanswer(int fd, int argc, char *argv[])
{
	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;
	if (argc == 1) {
		ast_cli(fd, "Auto answer is %s.\n", autoanswer ? "on" : "off");
		return RESULT_SUCCESS;
	} else {
		if (!strcasecmp(argv[1], "on"))
			autoanswer = -1;
		else if (!strcasecmp(argv[1], "off"))
			autoanswer = 0;
		else
			return RESULT_SHOWUSAGE;
	}
	return RESULT_SUCCESS;
}

static char *autoanswer_complete(char *line, char *word, int pos, int state)
{
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
	switch(state) {
	case 0:
		if (strlen(word) && !strncasecmp(word, "on", MIN(strlen(word), 2)))
			return strdup("on");
	case 1:
		if (strlen(word) && !strncasecmp(word, "off", MIN(strlen(word), 3)))
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

static int console_answer(int fd, int argc, char *argv[])
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	if (!oss.owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	hookstate = 1;
	cursound = -1;
	ast_queue_frame(oss.owner, &f);
	answer_sound();
	return RESULT_SUCCESS;
}

static char sendtext_usage[] =
"Usage: send text <message>\n"
"       Sends a text message for display on the remote terminal.\n";

static int console_sendtext(int fd, int argc, char *argv[])
{
	int tmparg = 2;
	char text2send[256] = "";
	struct ast_frame f = { 0, };
	if (argc < 2)
		return RESULT_SHOWUSAGE;
	if (!oss.owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	if (strlen(text2send))
		ast_cli(fd, "Warning: message already waiting to be sent, overwriting\n");
	text2send[0] = '\0';
	while(tmparg < argc) {
		strncat(text2send, argv[tmparg++], sizeof(text2send) - strlen(text2send) - 1);
		strncat(text2send, " ", sizeof(text2send) - strlen(text2send) - 1);
	}
	if (strlen(text2send)) {
		f.frametype = AST_FRAME_TEXT;
		f.subclass = 0;
		f.data = text2send;
		f.datalen = strlen(text2send);
		ast_queue_frame(oss.owner, &f);
	}
	return RESULT_SUCCESS;
}

static char answer_usage[] =
"Usage: answer\n"
"       Answers an incoming call on the console (OSS) channel.\n";

static int console_hangup(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	cursound = -1;
	if (!oss.owner && !hookstate) {
		ast_cli(fd, "No call to hangup up\n");
		return RESULT_FAILURE;
	}
	hookstate = 0;
	if (oss.owner) {
		ast_queue_hangup(oss.owner);
	}
	return RESULT_SUCCESS;
}

static int console_flash(int fd, int argc, char *argv[])
{
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_FLASH };
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	cursound = -1;
	if (!oss.owner) {
		ast_cli(fd, "No call to flash\n");
		return RESULT_FAILURE;
	}
	hookstate = 0;
	if (oss.owner) {
		ast_queue_frame(oss.owner, &f);
	}
	return RESULT_SUCCESS;
}

static char hangup_usage[] =
"Usage: hangup\n"
"       Hangs up any call currently placed on the console.\n";


static char flash_usage[] =
"Usage: flash\n"
"       Flashes the call currently placed on the console.\n";

static int console_dial(int fd, int argc, char *argv[])
{
	char tmp[256], *tmp2;
	char *mye, *myc;
	int x;
	struct ast_frame f = { AST_FRAME_DTMF, 0 };
	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;
	if (oss.owner) {
		if (argc == 2) {
			for (x=0;x<strlen(argv[1]);x++) {
				f.subclass = argv[1][x];
				ast_queue_frame(oss.owner, &f);
			}
		} else {
			ast_cli(fd, "You're already in a call.  You can use this only to dial digits until you hangup\n");
			return RESULT_FAILURE;
		}
		return RESULT_SUCCESS;
	}
	mye = exten;
	myc = context;
	if (argc == 2) {
		char *stringp=NULL;
		strncpy(tmp, argv[1], sizeof(tmp)-1);
		stringp=tmp;
		strsep(&stringp, "@");
		tmp2 = strsep(&stringp, "@");
		if (strlen(tmp))
			mye = tmp;
		if (tmp2 && strlen(tmp2))
			myc = tmp2;
	}
	if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
		strncpy(oss.exten, mye, sizeof(oss.exten)-1);
		strncpy(oss.context, myc, sizeof(oss.context)-1);
		hookstate = 1;
		oss_new(&oss, AST_STATE_RINGING);
	} else
		ast_cli(fd, "No such extension '%s' in context '%s'\n", mye, myc);
	return RESULT_SUCCESS;
}

static char dial_usage[] =
"Usage: dial [extension[@context]]\n"
"       Dials a given extensison (and context if specified)\n";

static int console_transfer(int fd, int argc, char *argv[])
{
	char tmp[256];
	char *context;
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	if (oss.owner && ast_bridged_channel(oss.owner)) {
		strncpy(tmp, argv[1], sizeof(tmp) - 1);
		context = strchr(tmp, '@');
		if (context) {
			*context = '\0';
			context++;
		} else
			context = oss.owner->context;
		if (ast_exists_extension(ast_bridged_channel(oss.owner), context, tmp, 1, ast_bridged_channel(oss.owner)->cid.cid_num)) {
			ast_cli(fd, "Whee, transferring %s to %s@%s.\n", 
					ast_bridged_channel(oss.owner)->name, tmp, context);
			if (ast_async_goto(ast_bridged_channel(oss.owner), context, tmp, 1))
				ast_cli(fd, "Failed to transfer :(\n");
		} else {
			ast_cli(fd, "No such extension exists\n");
		}
	} else {
		ast_cli(fd, "There is no call to transfer\n");
	}
	return RESULT_SUCCESS;
}

static char transfer_usage[] =
"Usage: transfer <extension>[@context]\n"
"       Transfers the currently connected call to the given extension (and\n"
"context if specified)\n";

static struct ast_cli_entry myclis[] = {
	{ { "answer", NULL }, console_answer, "Answer an incoming console call", answer_usage },
	{ { "hangup", NULL }, console_hangup, "Hangup a call on the console", hangup_usage },
	{ { "flash", NULL }, console_flash, "Flash a call on the console", flash_usage },
	{ { "dial", NULL }, console_dial, "Dial an extension on the console", dial_usage },
	{ { "transfer", NULL }, console_transfer, "Transfer a call to a different extension", transfer_usage },
	{ { "send", "text", NULL }, console_sendtext, "Send text to the remote device", sendtext_usage },
	{ { "autoanswer", NULL }, console_autoanswer, "Sets/displays autoanswer", autoanswer_usage, autoanswer_complete }
};

int load_module()
{
	int res;
	int x;
	struct ast_config *cfg;
	struct ast_variable *v;
	res = pipe(sndcmd);
	if (res) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		return -1;
	}
	res = soundcard_init();
	if (res < 0) {
		if (option_verbose > 1) {
			ast_verbose(VERBOSE_PREFIX_2 "No sound card detected -- console channel will be unavailable\n");
			ast_verbose(VERBOSE_PREFIX_2 "Turn off OSS support by adding 'noload=chan_oss.so' in /etc/asterisk/modules.conf\n");
		}
		return 0;
	}
	if (!full_duplex)
		ast_log(LOG_WARNING, "XXX I don't work right with non-full duplex sound cards XXX\n");
	res = ast_channel_register(&oss_tech);
	if (res < 0) {
		ast_log(LOG_ERROR, "Unable to register channel class '%s'\n", type);
		return -1;
	}
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_register(myclis + x);
	if ((cfg = ast_config_load(config))) {
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "autoanswer"))
				autoanswer = ast_true(v->value);
			else if (!strcasecmp(v->name, "silencesuppression"))
				silencesuppression = ast_true(v->value);
			else if (!strcasecmp(v->name, "silencethreshold"))
				silencethreshold = atoi(v->value);
			else if (!strcasecmp(v->name, "context"))
				strncpy(context, v->value, sizeof(context)-1);
			else if (!strcasecmp(v->name, "language"))
				strncpy(language, v->value, sizeof(language)-1);
			else if (!strcasecmp(v->name, "extension"))
				strncpy(exten, v->value, sizeof(exten)-1);
			else if (!strcasecmp(v->name, "playbackonly"))
				playbackonly = ast_true(v->value);
			v=v->next;
		}
		ast_config_destroy(cfg);
	}
	ast_pthread_create(&sthread, NULL, sound_thread, NULL);
	return 0;
}



int unload_module()
{
	int x;

	ast_channel_unregister(&oss_tech);
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_unregister(myclis + x);
	close(sounddev);
	if (sndcmd[0] > 0) {
		close(sndcmd[0]);
		close(sndcmd[1]);
	}
	if (oss.owner)
		ast_softhangup(oss.owner, AST_SOFTHANGUP_APPUNLOAD);
	if (oss.owner)
		return -1;
	return 0;
}

char *description()
{
	return (char *) desc;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
