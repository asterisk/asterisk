/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 2002, Linux Support Services
 *
 * By Matthew Fredrickson <creslin@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/frame.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/module.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/options.h>
#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/cli.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

#include "busy.h"
#include "ringtone.h"
#include "ring10.h"
#include "answer.h"

#ifdef ALSA_MONITOR
#include "alsa-monitor.h"
#endif

#define DEBUG 0
/* Which device to use */
#define ALSA_INDEV "default"
#define ALSA_OUTDEV "default"
#define DESIRED_RATE 8000

/* Lets use 160 sample frames, just like GSM.  */
#define FRAME_SIZE 160
#define PERIOD_FRAMES 80 /* 80 Frames, at 2 bytes each */

/* When you set the frame size, you have to come up with
   the right buffer format as well. */
/* 5 64-byte frames = one frame */
#define BUFFER_FMT ((buffersize * 10) << 16) | (0x0006);

/* Don't switch between read/write modes faster than every 300 ms */
#define MIN_SWITCH_TIME 600

static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
//static int block = O_NONBLOCK;
static char indevname[50] = ALSA_INDEV;
static char outdevname[50] = ALSA_OUTDEV;

#if 0
static struct timeval lasttime;
#endif

static int usecnt;
static int needanswer = 0;
static int needringing = 0;
static int needhangup = 0;
static int silencesuppression = 0;
static int silencethreshold = 1000;

static char digits[80] = "";
static char text2send[80] = "";

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char *type = "Console";
static char *desc = "ALSA Console Channel Driver";
static char *tdesc = "ALSA Console Channel Driver";
static char *config = "alsa.conf";

static char context[AST_MAX_EXTENSION] = "default";
static char language[MAX_LANGUAGE] = "";
static char exten[AST_MAX_EXTENSION] = "s";

/* Command pipe */
static int cmd[2];

int hookstate=0;

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

static struct chan_alsa_pvt {
	/* We only have one ALSA structure -- near sighted perhaps, but it
	   keeps this driver as simple as possible -- as it should be. */
	struct ast_channel *owner;
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_EXTENSION];
#if 0
	snd_pcm_t *card;
#endif
	snd_pcm_t *icard, *ocard;
	
} alsa;

#if 0
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
#endif

/* Number of buffers...  Each is FRAMESIZE/8 ms long.  For example
   with 160 sample frames, and a buffer size of 3, we have a 60ms buffer, 
   usually plenty. */

pthread_t sthread;

#define MAX_BUFFER_SIZE 100
//static int buffersize = 3;

//static int full_duplex = 0;

/* Are we reading or writing (simulated full duplex) */
//static int readmode = 1;

/* File descriptors for sound device */
static int readdev = -1;
static int writedev = -1;

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
	snd_pcm_state_t state;

	if (cursound > -1) {
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
			return 0;
			}
		}
		
		if (res == 0 || !frame) {
			return 0;
		}
#ifdef ALSA_MONITOR
		alsa_monitor_write((char *)frame, res * 2);
#endif		
		state = snd_pcm_state(alsa.ocard);
		if (state == SND_PCM_STATE_XRUN) {
			snd_pcm_prepare(alsa.ocard);
		}
		res = snd_pcm_writei(alsa.ocard, frame, res);
		if (res > 0)
			return 0;
		return 0;
	}
	return 0;
}

static void *sound_thread(void *unused)
{
	fd_set rfds;
	fd_set wfds;
	int max;
	int res;
	for(;;) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		max = sndcmd[0];
		FD_SET(sndcmd[0], &rfds);
		if (cursound > -1) {
			FD_SET(writedev, &wfds);
			if (writedev > max)
				max = writedev;
		}
#ifdef ALSA_MONITOR
		if (!alsa.owner) {
			FD_SET(readdev, &rfds);
			if (readdev > max)
				max = readdev;
		}
#endif
		res = ast_select(max + 1, &rfds, &wfds, NULL, NULL);
		if (res < 1) {
			ast_log(LOG_WARNING, "select failed: %s\n", strerror(errno));
			continue;
		}
#ifdef ALSA_MONITOR
		if (FD_ISSET(readdev, &rfds)) {
			/* Keep the pipe going with read audio */
			snd_pcm_state_t state;
			short buf[FRAME_SIZE];
			int r;
			
			state = snd_pcm_state(alsa.ocard);
			if (state == SND_PCM_STATE_XRUN) {
				snd_pcm_prepare(alsa.ocard);
			}
			r = snd_pcm_readi(alsa.icard, buf, FRAME_SIZE);
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
			} else
				alsa_monitor_read((char *)buf, r * 2);
		}		
#endif		
		if (FD_ISSET(sndcmd[0], &rfds)) {
			read(sndcmd[0], &cursound, sizeof(cursound));
			silencelen = 0;
			offset = 0;
			sampsent = 0;
		}
		if (FD_ISSET(writedev, &wfds))
			if (send_sound())
				ast_log(LOG_WARNING, "Failed to write sound\n");
	}
	/* Never reached */
	return NULL;
}

static snd_pcm_t *alsa_card_init(char *dev, snd_pcm_stream_t stream)
{
	int err;
	int direction;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *hwparams = NULL;
	snd_pcm_sw_params_t *swparams = NULL;
	struct pollfd pfd;
	snd_pcm_uframes_t period_size = PERIOD_FRAMES * 4;
	//int period_bytes = 0;
	snd_pcm_uframes_t buffer_size = 0;

	unsigned int rate = DESIRED_RATE;
	unsigned int per_min = 1;
	//unsigned int per_max = 8;
	snd_pcm_uframes_t start_threshold, stop_threshold;

	err = snd_pcm_open(&handle, dev, stream, O_NONBLOCK);
	if (err < 0) {
		ast_log(LOG_ERROR, "snd_pcm_open failed: %s\n", snd_strerror(err));
		return NULL;
	} else {
		ast_log(LOG_DEBUG, "Opening device %s in %s mode\n", dev, (stream == SND_PCM_STREAM_CAPTURE) ? "read" : "write");
	}

	snd_pcm_hw_params_alloca(&hwparams);
	snd_pcm_hw_params_any(handle, hwparams);

	err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		ast_log(LOG_ERROR, "set_access failed: %s\n", snd_strerror(err));
	}

	err = snd_pcm_hw_params_set_format(handle, hwparams, format);
	if (err < 0) {
		ast_log(LOG_ERROR, "set_format failed: %s\n", snd_strerror(err));
	}

	err = snd_pcm_hw_params_set_channels(handle, hwparams, 1);
	if (err < 0) {
		ast_log(LOG_ERROR, "set_channels failed: %s\n", snd_strerror(err));
	}

	direction = 0;
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &rate, &direction);
	if (rate != DESIRED_RATE) {
		ast_log(LOG_WARNING, "Rate not correct, requested %d, got %d\n", DESIRED_RATE, rate);
	}

	direction = 0;
	err = snd_pcm_hw_params_set_period_size_near(handle, hwparams, &period_size, &direction);
	if (err < 0) {
		ast_log(LOG_ERROR, "period_size(%ld frames) is bad: %s\n", period_size, snd_strerror(err));
	} else {
		ast_log(LOG_DEBUG, "Period size is %d\n", err);
	}

	buffer_size = 4096 * 2; //period_size * 16;
	err = snd_pcm_hw_params_set_buffer_size_near(handle, hwparams, &buffer_size);
	if (err < 0) {
		ast_log(LOG_WARNING, "Problem setting buffer size of %ld: %s\n", buffer_size, snd_strerror(err));
	} else {
		ast_log(LOG_DEBUG, "Buffer size is set to %d frames\n", err);
	}

#if 0
	direction = 0;
	err = snd_pcm_hw_params_set_periods_min(handle, hwparams, &per_min, &direction);
	if (err < 0) {
		ast_log(LOG_ERROR, "periods_min: %s\n", snd_strerror(err));
	}

	err = snd_pcm_hw_params_set_periods_max(handle, hwparams, &per_max, 0);
	if (err < 0) {
		ast_log(LOG_ERROR, "periods_max: %s\n", snd_strerror(err));
	}
#endif

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0) {
		ast_log(LOG_ERROR, "Couldn't set the new hw params: %s\n", snd_strerror(err));
	}

	snd_pcm_sw_params_alloca(&swparams);
	snd_pcm_sw_params_current(handle, swparams);

#if 1
	if (stream == SND_PCM_STREAM_PLAYBACK) {
		start_threshold = period_size;
	} else {
		start_threshold = 1;
	}

	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	if (err < 0) {
		ast_log(LOG_ERROR, "start threshold: %s\n", snd_strerror(err));
	}
#endif

#if 1
	if (stream == SND_PCM_STREAM_PLAYBACK) {
		stop_threshold = buffer_size;
	} else {
		stop_threshold = buffer_size;
	}
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	if (err < 0) {
		ast_log(LOG_ERROR, "stop threshold: %s\n", snd_strerror(err));
	}
#endif
#if 0
	err = snd_pcm_sw_params_set_xfer_align(handle, swparams, PERIOD_FRAMES);
	if (err < 0) {
		ast_log(LOG_ERROR, "Unable to set xfer alignment: %s\n", snd_strerror(err));
	}
#endif

#if 0
	err = snd_pcm_sw_params_set_silence_threshold(handle, swparams, silencethreshold);
	if (err < 0) {
		ast_log(LOG_ERROR, "Unable to set silence threshold: %s\n", snd_strerror(err));
	}
#endif
	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0) {
		ast_log(LOG_ERROR, "sw_params: %s\n", snd_strerror(err));
	}

	err = snd_pcm_poll_descriptors_count(handle);
	if (err <= 0) {
		ast_log(LOG_ERROR, "Unable to get a poll descriptors count, error is %s\n", snd_strerror(err));
	}

	if (err != 1) {
		ast_log(LOG_DEBUG, "Can't handle more than one device\n");
	}

	snd_pcm_poll_descriptors(handle, &pfd, err);
	ast_log(LOG_DEBUG, "Acquired fd %d from the poll descriptor\n", pfd.fd);

	if (stream == SND_PCM_STREAM_CAPTURE)
		readdev = pfd.fd;
	else
		writedev = pfd.fd;

	return handle;
}

static int soundcard_init(void)
{
	alsa.icard = alsa_card_init(indevname, SND_PCM_STREAM_CAPTURE);
	alsa.ocard = alsa_card_init(outdevname, SND_PCM_STREAM_PLAYBACK);

	if (!alsa.icard || !alsa.ocard) {
		ast_log(LOG_ERROR, "Problem opening alsa I/O devices\n");
		return -1;
	}

	return readdev;
}

static int alsa_digit(struct ast_channel *c, char digit)
{
	ast_verbose( " << Console Received digit %c >> \n", digit);
	return 0;
}

static int alsa_text(struct ast_channel *c, char *text)
{
	ast_verbose( " << Console Received text %s >> \n", text);
	return 0;
}

static int alsa_call(struct ast_channel *c, char *dest, int timeout)
{
	int res = 3;
	ast_verbose( " << Call placed to '%s' on console >> \n", dest);
	if (autoanswer) {
		ast_verbose( " << Auto-answered >> \n" );
		needanswer = 1;
	} else {
		ast_verbose( " << Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
		needringing = 1;
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

static int alsa_answer(struct ast_channel *c)
{
	ast_verbose( " << Console call has been answered >> \n");
	answer_sound();
	ast_setstate(c, AST_STATE_UP);
	cursound = -1;
	return 0;
}

static int alsa_hangup(struct ast_channel *c)
{
	int res;
	cursound = -1;
	c->pvt->pvt = NULL;
	alsa.owner = NULL;
	ast_verbose( " << Hangup on console >> \n");
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
	needhangup = 0;
	needanswer = 0;
	if (hookstate) {
		res = 2;
		write(sndcmd[1], &res, sizeof(res));
	}
	return 0;
}

#if 0
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
#endif

static int alsa_write(struct ast_channel *chan, struct ast_frame *f)
{
	int res;
	static char sizbuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int pos;
	//size_t frames = 0;
	snd_pcm_state_t state;
	/* Immediately return if no sound is enabled */
	if (nosound)
		return 0;
	/* Stop any currently playing sound */
	if (cursound != -1) {
		snd_pcm_drop(alsa.ocard);
		snd_pcm_prepare(alsa.ocard);
		cursound = -1;
	}
	

	/* We have to digest the frame in 160-byte portions */
	if (f->datalen > sizeof(sizbuf) - sizpos) {
		ast_log(LOG_WARNING, "Frame too large\n");
		return -1;
	}
	memcpy(sizbuf + sizpos, f->data, f->datalen);
	len += f->datalen;
	pos = 0;
#ifdef ALSA_MONITOR
	alsa_monitor_write(sizbuf, len);
#endif
	state = snd_pcm_state(alsa.ocard);
	if (state == SND_PCM_STATE_XRUN) {
		snd_pcm_prepare(alsa.ocard);
	}
	res = snd_pcm_writei(alsa.ocard, sizbuf, len/2);
	if (res == -EPIPE) {
#if DEBUG
		ast_log(LOG_DEBUG, "XRUN write\n");
#endif
		snd_pcm_prepare(alsa.ocard);
		res = snd_pcm_writei(alsa.ocard, sizbuf, len/2);
		if (res != len/2) {
			ast_log(LOG_ERROR, "Write error: %s\n", snd_strerror(res));
			return -1;
		} else if (res < 0) {
			ast_log(LOG_ERROR, "Write error %s\n", snd_strerror(res));
			return -1;
		}
	} else {
		if (res == -ESTRPIPE) {
			ast_log(LOG_ERROR, "You've got some big problems\n");
		}
	}

	return 0;
}


static struct ast_frame *alsa_read(struct ast_channel *chan)
{
	static struct ast_frame f;
	static short __buf[FRAME_SIZE + AST_FRIENDLY_OFFSET/2];
	short *buf;
	static int readpos = 0;
	static int left = FRAME_SIZE;
	int res;
	int b;
	int nonull=0;
	snd_pcm_state_t state;
	int r = 0;
	int off = 0;

	/* Acknowledge any pending cmd */
	res = read(cmd[0], &b, sizeof(b));
	if (res > 0)
		nonull = 1;
	
	f.frametype = AST_FRAME_NULL;
	f.subclass = 0;
	f.samples = 0;
	f.datalen = 0;
	f.data = NULL;
	f.offset = 0;
	f.src = type;
	f.mallocd = 0;
	
	if (needringing) {
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_RINGING;
		needringing = 0;
		return &f;
	}
	
	if (needhangup) {
		needhangup = 0;
		return NULL;
	}
	if (strlen(text2send)) {
		f.frametype = AST_FRAME_TEXT;
		f.subclass = 0;
		f.data = text2send;
		f.datalen = strlen(text2send);
		strcpy(text2send,"");
		return &f;
	}
	if (strlen(digits)) {
		f.frametype = AST_FRAME_DTMF;
		f.subclass = digits[0];
		for (res=0;res<strlen(digits);res++)
			digits[res] = digits[res + 1];
		return &f;
	}
	
	if (needanswer) {
		needanswer = 0;
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_ANSWER;
		ast_setstate(chan, AST_STATE_UP);
		return &f;
	}
	
	if (nonull)
		return &f;
		
	
	state = snd_pcm_state(alsa.ocard);
	if (state == SND_PCM_STATE_XRUN) {
		snd_pcm_prepare(alsa.ocard);
	}

	buf = __buf + AST_FRIENDLY_OFFSET/2;

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
		return NULL;
	} else if (r >= 0) {
		off -= r;
	}
	/* Update positions */
	readpos += r;
	left -= r;

	if (readpos >= FRAME_SIZE) {
		/* A real frame */
		readpos = 0;
		left = FRAME_SIZE;
		if (chan->_state != AST_STATE_UP) {
			/* Don't transmit unless it's up */
			return &f;
		}
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.samples = FRAME_SIZE;
		f.datalen = FRAME_SIZE * 2;
		f.data = buf;
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = type;
		f.mallocd = 0;
#ifdef ALSA_MONITOR
		alsa_monitor_read((char *)buf, FRAME_SIZE * 2);
#endif		

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

static int alsa_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_alsa_pvt *p = newchan->pvt->pvt;
	p->owner = newchan;
	return 0;
}

static int alsa_indicate(struct ast_channel *chan, int cond)
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
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, chan->name);
		return -1;
	}
	if (res > -1) {
		write(sndcmd[1], &res, sizeof(res));
	}
	return 0;	
}

static struct ast_channel *alsa_new(struct chan_alsa_pvt *p, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "ALSA/%s", indevname);
		tmp->type = type;
		tmp->fds[0] = readdev;
		tmp->fds[1] = cmd[0];
		tmp->nativeformats = AST_FORMAT_SLINEAR;
		tmp->pvt->pvt = p;
		tmp->pvt->send_digit = alsa_digit;
		tmp->pvt->send_text = alsa_text;
		tmp->pvt->hangup = alsa_hangup;
		tmp->pvt->answer = alsa_answer;
		tmp->pvt->read = alsa_read;
		tmp->pvt->call = alsa_call;
		tmp->pvt->write = alsa_write;
		tmp->pvt->indicate = alsa_indicate;
		tmp->pvt->fixup = alsa_fixup;
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

static struct ast_channel *alsa_request(char *type, int format, void *data)
{
	int oldformat = format;
	struct ast_channel *tmp;
	format &= AST_FORMAT_SLINEAR;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of format '%d'\n", oldformat);
		return NULL;
	}
	if (alsa.owner) {
		ast_log(LOG_NOTICE, "Already have a call on the ALSA channel\n");
		return NULL;
	}
	tmp= alsa_new(&alsa, AST_STATE_DOWN);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to create new ALSA channel\n");
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
"       The default value of autoanswer is in 'alsa.conf'.\n";

static int console_answer(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	hookstate = 1;
	cursound = -1;
	needanswer++;
	answer_sound();
	return RESULT_SUCCESS;
}

static char sendtext_usage[] =
"Usage: send text <message>\n"
"       Sends a text message for display on the remote terminal.\n";

static int console_sendtext(int fd, int argc, char *argv[])
{
	int tmparg = 2;
	if (argc < 2)
		return RESULT_SHOWUSAGE;
	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	if (strlen(text2send))
		ast_cli(fd, "Warning: message already waiting to be sent, overwriting\n");
	strcpy(text2send, "");
	while(tmparg <= argc) {
		strncat(text2send, argv[tmparg++], sizeof(text2send) - strlen(text2send));
		strncat(text2send, " ", sizeof(text2send) - strlen(text2send));
	}
	needanswer++;
	return RESULT_SUCCESS;
}

static char answer_usage[] =
"Usage: answer\n"
"       Answers an incoming call on the console (ALSA) channel.\n";

static int console_hangup(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	cursound = -1;
	if (!alsa.owner && !hookstate) {
		ast_cli(fd, "No call to hangup up\n");
		return RESULT_FAILURE;
	}
	hookstate = 0;
	if (alsa.owner) {
		ast_queue_hangup(alsa.owner);
	}
	return RESULT_SUCCESS;
}

static char hangup_usage[] =
"Usage: hangup\n"
"       Hangs up any call currently placed on the console.\n";


static int console_dial(int fd, int argc, char *argv[])
{
	char tmp[256], *tmp2;
	char *mye, *myc;
	int b = 0;
	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;
	if (alsa.owner) {
		if (argc == 2) {
			strncat(digits, argv[1], sizeof(digits) - strlen(digits));
			/* Wake up the polling thread */
			write(cmd[1], &b, sizeof(b));
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
		strncpy(alsa.exten, mye, sizeof(alsa.exten)-1);
		strncpy(alsa.context, myc, sizeof(alsa.context)-1);
		hookstate = 1;
		alsa_new(&alsa, AST_STATE_RINGING);
	} else
		ast_cli(fd, "No such extension '%s' in context '%s'\n", mye, myc);
	return RESULT_SUCCESS;
}

static char dial_usage[] =
"Usage: dial [extension[@context]]\n"
"       Dials a given extensison (";


static struct ast_cli_entry myclis[] = {
	{ { "answer", NULL }, console_answer, "Answer an incoming console call", answer_usage },
	{ { "hangup", NULL }, console_hangup, "Hangup a call on the console", hangup_usage },
	{ { "dial", NULL }, console_dial, "Dial an extension on the console", dial_usage },
	{ { "send", "text", NULL }, console_sendtext, "Send text to the remote device", sendtext_usage },
	{ { "autoanswer", NULL }, console_autoanswer, "Sets/displays autoanswer", autoanswer_usage, autoanswer_complete }
};

int load_module()
{
	int res;
	int x;
	int flags;
	struct ast_config *cfg;
	struct ast_variable *v;
	res = pipe(cmd);
	res = pipe(sndcmd);
	if (res) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		return -1;
	}
	flags = fcntl(cmd[0], F_GETFL);
	fcntl(cmd[0], F_SETFL, flags | O_NONBLOCK);
	flags = fcntl(cmd[1], F_GETFL);
	fcntl(cmd[1], F_SETFL, flags | O_NONBLOCK);
	res = soundcard_init();
	if (res < 0) {
		close(cmd[1]);
		close(cmd[0]);
		if (option_verbose > 1) {
			ast_verbose(VERBOSE_PREFIX_2 "No sound card detected -- console channel will be unavailable\n");
			ast_verbose(VERBOSE_PREFIX_2 "Turn off ALSA support by adding 'noload=chan_alsa.so' in /etc/asterisk/modules.conf\n");
		}
		return 0;
	}
#if 0
	if (!full_duplex)
		ast_log(LOG_WARNING, "XXX I don't work right with non-full duplex sound cards XXX\n");
#endif
	res = ast_channel_register(type, tdesc, AST_FORMAT_SLINEAR, alsa_request);
	if (res < 0) {
		ast_log(LOG_ERROR, "Unable to register channel class '%s'\n", type);
		return -1;
	}
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_register(myclis + x);
	if ((cfg = ast_load(config))) {
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
			else if (!strcasecmp(v->name, "input_device"))
				strncpy(indevname, v->value, sizeof(indevname)-1);
			else if (!strcasecmp(v->name, "output_device"))
				strncpy(outdevname, v->value, sizeof(outdevname)-1);
			v=v->next;
		}
		ast_destroy(cfg);
	}
	pthread_create(&sthread, NULL, sound_thread, NULL);
#ifdef ALSA_MONITOR
	if (alsa_monitor_start()) {
		ast_log(LOG_ERROR, "Problem starting Monitoring\n");
	}
#endif	 
	return 0;
}



int unload_module()
{
	int x;
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_unregister(myclis + x);
	close(readdev);
	close(writedev);
	if (cmd[0] > 0) {
		close(cmd[0]);
		close(cmd[1]);
	}
	if (sndcmd[0] > 0) {
		close(sndcmd[0]);
		close(sndcmd[1]);
	}
	if (alsa.owner)
		ast_softhangup(alsa.owner, AST_SOFTHANGUP_APPUNLOAD);
	if (alsa.owner)
		return -1;
	return 0;
}

char *description()
{
	return desc;
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
