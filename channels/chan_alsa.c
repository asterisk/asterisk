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

#define DEBUG 0
/* Which device to use */
#define ALSA_INDEV "default"
#define ALSA_OUTDEV "default"
#define DESIRED_RATE 8000

/* Lets use 160 sample frames, just like GSM.  */
#define PERIOD_SIZE 160
#define ALSA_MAX_BUF PERIOD_SIZE*4 + AST_FRIENDLY_OFFSET

static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
static char indevname[50] = ALSA_INDEV;
static char outdevname[50] = ALSA_OUTDEV;

static int usecnt;
static int silencesuppression = 0;
static int silencethreshold = 1000;

#if 0
static struct timeval lasttime;
#endif

static char digits[80] = "";
static char text2send[80] = "";

static ast_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

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

static short silence[PERIOD_SIZE] = {0, };

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

typedef struct chan_alsa_pvt chan_alsa_pvt_t;
struct chan_alsa_pvt {
	/* We only have one ALSA structure -- near sighted perhaps, but it
	   keeps this driver as simple as possible -- as it should be. */
	struct ast_channel *owner;
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_EXTENSION];
	struct pollfd                *pfd;
	unsigned int  playback_nfds;
	unsigned int  capture_nfds;
	snd_pcm_t *playback_handle;
	snd_pcm_t *capture_handle;
	snd_pcm_uframes_t capture_period_size;
        snd_pcm_uframes_t capture_buffer_size;
	
	pthread_t sound_thread;
	char buf[ALSA_MAX_BUF];          /* buffer for reading frames */
	char *capture_buf;             /* malloc buffer for reading frames */
	struct ast_frame fr;
	int cursound;
	int cursound_offset;
	int nosound;
};

static chan_alsa_pvt_t alsa;

#define MAX_BUFFER_SIZE 100

static int autoanswer = 1;

/* Send a announcement */
static int send_sound(chan_alsa_pvt_t *driver)
{
	int res;
	int frames;
	int cursound=driver->cursound;
	snd_pcm_state_t state;

	if (cursound > -1) {
		driver->nosound=1;
		state = snd_pcm_state(alsa.playback_handle);
		if (state == SND_PCM_STATE_XRUN) {
			snd_pcm_prepare(alsa.playback_handle);
		}
		frames = sounds[cursound].samplen - driver->cursound_offset;
		if (frames >= PERIOD_SIZE)  {
			res = snd_pcm_writei(driver->playback_handle,sounds[cursound].data + (driver->cursound_offset*2), PERIOD_SIZE);
			driver->cursound_offset+=PERIOD_SIZE;
		} else if (frames > 0) {
			res = snd_pcm_writei(driver->playback_handle,sounds[cursound].data + (driver->cursound_offset*2), frames);
			res = snd_pcm_writei(driver->playback_handle,silence, PERIOD_SIZE - frames);
			driver->cursound_offset+=PERIOD_SIZE;
			} else {
			res = snd_pcm_writei(driver->playback_handle,silence, PERIOD_SIZE);
			driver->cursound_offset+=PERIOD_SIZE;
		}
		if (driver->cursound_offset > ( sounds[cursound].samplen + sounds[cursound].silencelen ) ) {
				if (sounds[cursound].repeat) {
				driver->cursound_offset=0;
				} else {
				driver->cursound = -1;
				driver->nosound=0;
				}
			}
		}
			return 0;
}

static int sound_capture(chan_alsa_pvt_t *driver)
{
	struct ast_frame *fr = &driver->fr;
	char *readbuf = ((char *)driver->buf) + AST_FRIENDLY_OFFSET;
	snd_pcm_sframes_t err;
	snd_pcm_sframes_t avail;
	snd_pcm_state_t alsa_state;
	
	/* Update positions */
	while ((avail = snd_pcm_avail_update (driver->capture_handle)) >= PERIOD_SIZE) {
	
		/* capture samples from sound card */
		err = snd_pcm_readi(driver->capture_handle, readbuf, PERIOD_SIZE);
		if (err == -EPIPE) {
			ast_log(LOG_ERROR, "XRUN read avail=%ld\n", avail);
			snd_pcm_prepare(driver->capture_handle);
			alsa_state = snd_pcm_state(driver->capture_handle);
                	if (alsa_state == SND_PCM_STATE_PREPARED) {
                        	snd_pcm_start(driver->capture_handle);
		}
			continue;
		} else if (err == -ESTRPIPE) {
			ast_log(LOG_ERROR, "-ESTRPIPE\n");
			snd_pcm_prepare(driver->capture_handle);
			alsa_state = snd_pcm_state(driver->capture_handle);
                	if (alsa_state == SND_PCM_STATE_PREPARED) {
                        	snd_pcm_start(driver->capture_handle);
		}
			continue;
		} else if (err < 0) {
			ast_log(LOG_ERROR, "Read error: %s\n", snd_strerror(err));
			return -1;
	}

		/* Now send captures samples */
		fr->frametype = AST_FRAME_VOICE;
		fr->src = type;
		fr->mallocd = 0;

		fr->subclass = AST_FORMAT_SLINEAR;
		fr->samples = PERIOD_SIZE;
		fr->datalen = PERIOD_SIZE * 2 ; /* 16bit = X * 2 */
		fr->data = readbuf;
		fr->offset = AST_FRIENDLY_OFFSET;

		if (driver->owner) ast_queue_frame(driver->owner, fr);
	}
	return 0; /* 0 = OK, !=0 -> Error */
}

static void *sound_thread(void *pvt)
{
	chan_alsa_pvt_t *driver = (chan_alsa_pvt_t *)pvt;
        unsigned int nfds;
        unsigned int ci;
	unsigned short revents;
	snd_pcm_state_t alsa_state;
	int res;
	if (driver->playback_handle) {
                driver->playback_nfds =
                        snd_pcm_poll_descriptors_count (
                                driver->playback_handle);
        } else {
                driver->playback_nfds = 0;
		}

        if (driver->capture_handle) {
                driver->capture_nfds =
                        snd_pcm_poll_descriptors_count (driver->capture_handle);
        } else {
                driver->capture_nfds = 0;
		}

        if (driver->pfd) {
                free (driver->pfd);
		}
			
        driver->pfd = (struct pollfd *)
                malloc (sizeof (struct pollfd) *
                        (driver->playback_nfds + driver->capture_nfds + 2));

        nfds = 0;
        if (driver->playback_handle) {
		snd_pcm_poll_descriptors (driver->playback_handle,
                                          &driver->pfd[0],
                                          driver->playback_nfds);
                nfds += driver->playback_nfds;
			}
        ci = nfds;

        if (driver->capture_handle) {
                snd_pcm_poll_descriptors (driver->capture_handle,
                                          &driver->pfd[ci],
                                          driver->capture_nfds);
                nfds += driver->capture_nfds;
		}		
	
	while (hookstate) {
		/* When no doing announcements */
		if (driver->cursound > -1) {
			res = poll(&driver->pfd[0], driver->playback_nfds, -1);
		} else {
			res = poll(&driver->pfd[ci], driver->capture_nfds, -1);
		}

		/* When doing announcements */
		if (driver->cursound > -1) {
			snd_pcm_poll_descriptors_revents(driver->playback_handle, &driver->pfd[0], driver->playback_nfds, &revents);
		        if (revents & POLLOUT) {
				if (send_sound(driver)) {
				ast_log(LOG_WARNING, "Failed to write sound\n");
	}
			}
		} else {
		snd_pcm_poll_descriptors_revents(driver->capture_handle, &driver->pfd[ci], driver->capture_nfds, &revents);
	        if (revents & POLLERR) {
			alsa_state = snd_pcm_state(driver->capture_handle);
			if (alsa_state == SND_PCM_STATE_XRUN) {
				snd_pcm_prepare(driver->capture_handle);
			}
			alsa_state = snd_pcm_state(driver->capture_handle);
			if (alsa_state == SND_PCM_STATE_PREPARED) {
				snd_pcm_start(driver->capture_handle);
			}
		}
	        if (revents & POLLIN) {
			if (sound_capture(driver)) {
				ast_log(LOG_WARNING, "Failed to read sound\n");
			}
		}
		}
	}
	/* Never reached */
	return NULL;
}

static snd_pcm_t *alsa_card_init(chan_alsa_pvt_t *driver, char *dev, snd_pcm_stream_t stream)
{
	int err;
	int direction;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *hwparams = NULL;
	snd_pcm_sw_params_t *swparams = NULL;
	snd_pcm_uframes_t period_size = PERIOD_SIZE;
	snd_pcm_uframes_t buffer_size = 0;

	unsigned int rate = DESIRED_RATE;
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
	buffer_size = 4096 * 2; /* period_size * 16; */
	err = snd_pcm_hw_params_set_period_size_near(handle, hwparams, &period_size, &direction);
	if (err < 0) {
		ast_log(LOG_ERROR, "period_size(%ld frames) is bad: %s\n", period_size, snd_strerror(err));
	} else {
		ast_log(LOG_DEBUG, "Period size is %d\n", err);
	}

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

	if (stream == SND_PCM_STREAM_CAPTURE) {
		driver->capture_period_size=period_size;
        	driver->capture_buffer_size=buffer_size;
	}

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0) {
		ast_log(LOG_ERROR, "Couldn't set the new hw params: %s\n", snd_strerror(err));
		return NULL;
	}

	snd_pcm_sw_params_alloca(&swparams);
	snd_pcm_sw_params_current(handle, swparams);

#if 1
	if (stream == SND_PCM_STREAM_PLAYBACK) {
		start_threshold = period_size*3;
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
		stop_threshold = buffer_size+1;
	}
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	if (err < 0) {
		ast_log(LOG_ERROR, "stop threshold: %s\n", snd_strerror(err));
	}
#endif
#if 0
	err = snd_pcm_sw_params_set_xfer_align(handle, swparams, PERIOD_SIZE);
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

	return handle;
}

static int soundcard_init(void)
{
	alsa.capture_handle = alsa_card_init(&alsa, indevname, SND_PCM_STREAM_CAPTURE);
	alsa.playback_handle = alsa_card_init(&alsa, outdevname, SND_PCM_STREAM_PLAYBACK);
	if (!alsa.capture_buf) alsa.capture_buf=malloc(alsa.capture_buffer_size * 2);

	if (!alsa.capture_handle || !alsa.playback_handle) {
		ast_log(LOG_ERROR, "Problem opening alsa I/O devices\n");
		if (alsa.capture_buf) {
			free (alsa.capture_buf);
			alsa.capture_buf=0;
		}
		return -1;
	}

	return 0; /* Success */
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
	chan_alsa_pvt_t *driver = (chan_alsa_pvt_t *)c->pvt->pvt;
	int res = 3;
        struct ast_frame f = { 0, };
	ast_verbose( " << Call placed to '%s' on console >> \n", dest);
	if (autoanswer) {
		ast_verbose( " << Auto-answered >> \n" );
                f.frametype = AST_FRAME_CONTROL;
                f.subclass = AST_CONTROL_ANSWER;
                ast_queue_frame(c, &f);
	} else {
                driver->nosound = 1;
		ast_verbose( " << Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
                f.frametype = AST_FRAME_CONTROL;
                f.subclass = AST_CONTROL_RINGING;
                ast_queue_frame(c, &f);
		driver->cursound = res;
	}
	return 0;
}

static void answer_sound(chan_alsa_pvt_t *driver)
{
	int res;
	driver->nosound = 1;
	driver->cursound = 4;
	driver->cursound_offset = 0;
	
}

static int alsa_answer(struct ast_channel *c)
{
	chan_alsa_pvt_t *driver = (chan_alsa_pvt_t *)c->pvt->pvt;
	ast_verbose( " << Console call has been answered >> \n");
	answer_sound(driver);
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/* The new_channel is now freed. */
static int alsa_hangup(struct ast_channel *c)
{
	int res;
	chan_alsa_pvt_t *driver = (chan_alsa_pvt_t *)c->pvt->pvt;
	
	driver->cursound = -1;
	driver->nosound = 0;
        if (hookstate) {
                hookstate = 0;
        }
	pthread_join(driver->sound_thread, NULL);
/*	snd_pcm_drain(driver->capture_handle); */
	driver->owner = NULL;
	c->pvt->pvt = NULL;
	ast_verbose( " << Hangup on console >> \n");
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
	return 0;
}

static int alsa_write(struct ast_channel *chan, struct ast_frame *f)
{
	chan_alsa_pvt_t *driver = (chan_alsa_pvt_t *)chan->pvt->pvt;
	int res;
	static char sizbuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int pos;
	snd_pcm_state_t state;
	snd_pcm_sframes_t delay = 0;

	if (driver->nosound) {
		return 0;
	}
	state = snd_pcm_state(driver->playback_handle);
	if (state == SND_PCM_STATE_XRUN) {
		snd_pcm_prepare(driver->playback_handle);
	}
	res = snd_pcm_delay( driver->playback_handle, &delay );
	if (delay > 4 * PERIOD_SIZE) {
		return 0;
	}
	res = snd_pcm_writei(driver->playback_handle, f->data, f->samples);
	if (res == -EPIPE) {
#if DEBUG
		ast_log(LOG_DEBUG, "XRUN write\n");
#endif
		snd_pcm_prepare(driver->playback_handle);
		res = snd_pcm_writei(driver->playback_handle, f->data, f->samples);
		if (res != f->samples) {
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
	static short __buf[PERIOD_SIZE + AST_FRIENDLY_OFFSET/2];
	short *buf;
	static int readpos = 0;
	static int left = PERIOD_SIZE;
	int res;
	int b;
	int nonull=0;
	snd_pcm_state_t state;
	int r = 0;
	int off = 0;
	/* FIXME: This should never been called */
        ast_log(LOG_WARNING, "ALSA_READ!!!!!\n");
	return NULL;
}
#if 0
	/* Acknowledge any pending cmd */
	res = read(cmd[0], &b, sizeof(b));
	if (res > 0)
		nonull = 1;
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	
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
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	
	if (needhangup) {
		needhangup = 0;
		return NULL;
	}
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	if (strlen(text2send)) {
		f.frametype = AST_FRAME_TEXT;
		f.subclass = 0;
		f.data = text2send;
		f.datalen = strlen(text2send);
		strcpy(text2send,"");
		return &f;
	}
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	if (strlen(digits)) {
		f.frametype = AST_FRAME_DTMF;
		f.subclass = digits[0];
		for (res=0;res<strlen(digits);res++)
			digits[res] = digits[res + 1];
		return &f;
	}
	
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	if (needanswer) {
		needanswer = 0;
		f.frametype = AST_FRAME_CONTROL;
		f.subclass = AST_CONTROL_ANSWER;
		ast_setstate(chan, AST_STATE_UP);
		return &f;
	}
	
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	if (nonull)
		return &f;
		
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);
	
	state = snd_pcm_state(alsa.playback_handle);
	if (state == SND_PCM_STATE_XRUN) {
		snd_pcm_prepare(alsa.playback_handle);
	}
        ast_log(LOG_WARNING, "alsa: %s:%d\n", __FUNCTION__, __LINE__);

	buf = __buf + AST_FRIENDLY_OFFSET/2;

	r = snd_pcm_readi(alsa.capture_handle, buf + readpos, left);
	if (r == -EPIPE) {
#if DEBUG
		ast_log(LOG_ERROR, "XRUN read\n");
#endif
		snd_pcm_prepare(alsa.capture_handle);
	} else if (r == -ESTRPIPE) {
		ast_log(LOG_ERROR, "-ESTRPIPE\n");
		snd_pcm_prepare(alsa.capture_handle);
	} else if (r < 0) {
		ast_log(LOG_ERROR, "Read error: %s\n", snd_strerror(r));
		return NULL;
	} else if (r >= 0) {
		off -= r;
	}
	/* Update positions */
	readpos += r;
	left -= r;

	if (readpos >= PERIOD_SIZE) {
		/* A real frame */
		readpos = 0;
		left = PERIOD_SIZE;
		if (chan->_state != AST_STATE_UP) {
			/* Don't transmit unless it's up */
			return &f;
		}
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.samples = PERIOD_SIZE;
		f.datalen = PERIOD_SIZE * 2;
		f.data = buf;
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = type;
		f.mallocd = 0;

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
#endif

static int alsa_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_alsa_pvt *p = newchan->pvt->pvt;
	p->owner = newchan;
	return 0;
}

static int alsa_indicate(struct ast_channel *chan, int cond)
{
	chan_alsa_pvt_t *driver = (chan_alsa_pvt_t *)chan->pvt->pvt;
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
		driver->cursound = res;
		driver->cursound_offset = 0;
		driver->nosound = 1;
	}
	return 0;	
}

/* New channel is about to be used */
static struct ast_channel *alsa_new(struct chan_alsa_pvt *p, int state)
{
	struct ast_channel *tmp;
	snd_pcm_state_t alsa_state;
	if (!p->capture_handle || !p->playback_handle) {
		return 0;
	}
	tmp = ast_channel_alloc(1);
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "ALSA/%s", indevname);
		tmp->type = type;
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
		p->pfd = NULL;
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
		pthread_create(&p->sound_thread, NULL, sound_thread, (void *) p);
		alsa_state = snd_pcm_state(p->capture_handle);
		if (alsa_state == SND_PCM_STATE_XRUN) {
			snd_pcm_prepare(p->capture_handle);
		}
		alsa_state = snd_pcm_state(p->capture_handle);
		if (alsa_state == SND_PCM_STATE_PREPARED) {
			snd_pcm_start(p->capture_handle);
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
        struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	hookstate = 1;
	ast_queue_frame(alsa.owner, &f);
	answer_sound(&alsa);
	return RESULT_SUCCESS;
}

static char sendtext_usage[] =
"Usage: send text <message>\n"
"       Sends a text message for display on the remote terminal.\n";

static int console_sendtext(int fd, int argc, char *argv[])
{
	int tmparg = 2;
	struct ast_frame f = { 0, };
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
       if (strlen(text2send)) {
                f.frametype = AST_FRAME_TEXT;
                f.subclass = 0;
                f.data = text2send;
                f.datalen = strlen(text2send);
                ast_queue_frame(alsa.owner, &f);
        }
	return RESULT_SUCCESS;
}

static char answer_usage[] =
"Usage: answer\n"
"       Answers an incoming call on the console (ALSA) channel.\n";

static int console_hangup(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	alsa.cursound = -1;
	alsa.nosound = 0;
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
	int x;
	struct ast_frame f = { AST_FRAME_DTMF, 0 };
	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;
	if (alsa.owner) {
		if (argc == 2) {
			for (x=0;x<strlen(argv[1]);x++) {
                                f.subclass = argv[1][x];
                                ast_queue_frame(alsa.owner, &f);
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
#if 0
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
#endif
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
			else if (!strcasecmp(v->name, "input_device")) {
				strncpy(indevname, v->value, sizeof(indevname)-1);
			} else if (!strcasecmp(v->name, "output_device"))
				strncpy(outdevname, v->value, sizeof(outdevname)-1);
			v=v->next;
		}
		ast_destroy(cfg);
	}
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
	return 0;
}



int unload_module()
{
	int x;
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_unregister(myclis + x);
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
	if (alsa.capture_buf) {
		free (alsa.capture_buf);
		alsa.capture_buf=0;
	}
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
