/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Use /dev/dsp as a channel, and the console to command it :).
 *
 * The full-duplex "simulation" is pretty weak.  This is generally a 
 * VERY BADLY WRITTEN DRIVER so please don't use it as a model for
 * writing a driver.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
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
#include <linux/soundcard.h>

/* Which device to use */
#define DEV_DSP "/dev/dsp"

/* Lets use 160 sample frames, just like GSM.  */
#define FRAME_SIZE 160

/* When you set the frame size, you have to come up with
   the right buffer format as well. */
/* 5 64-byte frames = one frame */
#define BUFFER_FMT ((buffersize * 5) << 16) | (0x0006);

/* Don't switch between read/write modes faster than every 300 ms */
#define MIN_SWITCH_TIME 600

static struct timeval lasttime;

static int usecnt;
static int needanswer = 0;
static int needhangup = 0;
static int silencesuppression = 0;
static int silencethreshold = 1000;

static char digits[80] = "";
static char text2send[80] = "";

static pthread_mutex_t usecnt_lock = PTHREAD_MUTEX_INITIALIZER;

static char *type = "Console";
static char *desc = "OSS Console Channel Driver";
static char *tdesc = "OSS Console Channel Driver";
static char *config = "oss.conf";

static char context[AST_MAX_EXTENSION] = "default";
static char language[MAX_LANGUAGE] = "";
static char exten[AST_MAX_EXTENSION] = "s";

/* Some pipes to prevent overflow */
static int funnel[2];
static pthread_mutex_t sound_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t silly;

static struct chan_oss_pvt {
	/* We only have one OSS structure -- near sighted perhaps, but it
	   keeps this driver as simple as possible -- as it should be. */
	struct ast_channel *owner;
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_EXTENSION];
} oss;

static int time_has_passed()
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


#define MAX_BUFFER_SIZE 100
static int buffersize = 3;

static int full_duplex = 0;

/* Are we reading or writing (simulated full duplex) */
static int readmode = 1;

/* File descriptor for sound device */
static int sounddev = -1;

static int autoanswer = 1;
 
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
			write(funnel[1], silbuf, silbufcnt * FRAME_SIZE);
			silbufcnt = 0;
		}
	}
	return 0;
}

static void *silly_thread(void *ignore)
{
	char buf[FRAME_SIZE * 2];
	int pos=0;
	int res=0;
	/* Read from the sound device, and write to the pipe. */
	for (;;) {
		/* Give the writer a better shot at the lock */
#if 0
		usleep(1000);
#endif		
		pthread_testcancel();
		pthread_mutex_lock(&sound_lock);
		res = read(sounddev, buf + pos, FRAME_SIZE * 2 - pos);
		pthread_mutex_unlock(&sound_lock);
		if (res > 0) {
			pos += res;
			if (pos == FRAME_SIZE * 2) {
				if (needhangup || needanswer || strlen(digits) || 
				    !silence_suppress((short *)buf)) {
					res = write(funnel[1], buf, sizeof(buf));
				}
				pos = 0;
			}
		} else {
			close(funnel[1]);
			break;
		}
		pthread_testcancel();
	}
	return NULL;
}

static int setformat(void)
{
	int fmt, desired, res, fd = sounddev;
	static int warnedalready = 0;
	static int warnedalready2 = 0;
	pthread_mutex_lock(&sound_lock);
	fmt = AFMT_S16_LE;
	res = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set format to 16-bit signed\n");
		pthread_mutex_unlock(&sound_lock);
		return -1;
	}
	res = ioctl(fd, SNDCTL_DSP_SETDUPLEX, 0);
	if (res >= 0) {
		if (option_verbose > 1) 
			ast_verbose(VERBOSE_PREFIX_2 "Console is full duplex\n");
		full_duplex = -1;
	}
	fmt = 0;
	res = ioctl(fd, SNDCTL_DSP_STEREO, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		pthread_mutex_unlock(&sound_lock);
		return -1;
	}
	/* 8000 Hz desired */
	desired = 8000;
	fmt = desired;
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		pthread_mutex_unlock(&sound_lock);
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
	pthread_mutex_unlock(&sound_lock);
	return 0;
}

static int soundcard_setoutput(int force)
{
	/* Make sure the soundcard is in output mode.  */
	int fd = sounddev;
	if (full_duplex || (!readmode && !force))
		return 0;
	pthread_mutex_lock(&sound_lock);
	readmode = 0;
	if (force || time_has_passed()) {
		ioctl(sounddev, SNDCTL_DSP_RESET);
		/* Keep the same fd reserved by closing the sound device and copying stdin at the same
		   time. */
		/* dup2(0, sound); */ 
		close(sounddev);
		fd = open(DEV_DSP, O_WRONLY);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to re-open DSP device: %s\n", strerror(errno));
			pthread_mutex_unlock(&sound_lock);
			return -1;
		}
		/* dup2 will close the original and make fd be sound */
		if (dup2(fd, sounddev) < 0) {
			ast_log(LOG_WARNING, "dup2() failed: %s\n", strerror(errno));
			pthread_mutex_unlock(&sound_lock);
			return -1;
		}
		if (setformat()) {
			pthread_mutex_unlock(&sound_lock);
			return -1;
		}
		pthread_mutex_unlock(&sound_lock);
		return 0;
	}
	pthread_mutex_unlock(&sound_lock);
	return 1;
}

static int soundcard_setinput(int force)
{
	int fd = sounddev;
	if (full_duplex || (readmode && !force))
		return 0;
	pthread_mutex_lock(&sound_lock);
	readmode = -1;
	if (force || time_has_passed()) {
		ioctl(sounddev, SNDCTL_DSP_RESET);
		close(sounddev);
		/* dup2(0, sound); */
		fd = open(DEV_DSP, O_RDONLY);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to re-open DSP device: %s\n", strerror(errno));
			pthread_mutex_unlock(&sound_lock);
			return -1;
		}
		/* dup2 will close the original and make fd be sound */
		if (dup2(fd, sounddev) < 0) {
			ast_log(LOG_WARNING, "dup2() failed: %s\n", strerror(errno));
			pthread_mutex_unlock(&sound_lock);
			return -1;
		}
		if (setformat()) {
			pthread_mutex_unlock(&sound_lock);
			return -1;
		}
		pthread_mutex_unlock(&sound_lock);
		return 0;
	}
	pthread_mutex_unlock(&sound_lock);
	return 1;
}

static int soundcard_init()
{
	/* Assume it's full duplex for starters */
	int fd = open(DEV_DSP, 	O_RDWR);
	if (fd < 0) {
		ast_log(LOG_ERROR, "Unable to open %s: %s\n", DEV_DSP, strerror(errno));
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

static int oss_text(struct ast_channel *c, char *text)
{
	ast_verbose( " << Console Received text %s >> \n", text);
	return 0;
}

static int oss_call(struct ast_channel *c, char *dest, int timeout)
{
	ast_verbose( " << Call placed to '%s' on console >> \n", dest);
	if (autoanswer) {
		ast_verbose( " << Auto-answered >> \n" );
		needanswer = 1;
	} else {
		ast_verbose( " << Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
	}
	return 0;
}

static int oss_answer(struct ast_channel *c)
{
	ast_verbose( " << Console call has been answered >> \n");
	c->state = AST_STATE_UP;
	return 0;
}

static int oss_hangup(struct ast_channel *c)
{
	c->pvt->pvt = NULL;
	oss.owner = NULL;
	ast_verbose( " << Hangup on console >> \n");
	pthread_mutex_lock(&usecnt_lock);
	usecnt--;
	pthread_mutex_unlock(&usecnt_lock);
	needhangup = 0;
	needanswer = 0;
	return 0;
}

static int soundcard_writeframe(short *data)
{	
	/* Write an exactly FRAME_SIZE sized of frame */
	static int bufcnt = 0;
	static char buffer[FRAME_SIZE * 2 * MAX_BUFFER_SIZE * 5];
	struct audio_buf_info info;
	int res;
	int fd = sounddev;
	static int warned=0;
	pthread_mutex_lock(&sound_lock);
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
		memcpy(buffer + (bufcnt * FRAME_SIZE * 2), data, FRAME_SIZE * 2);
		bufcnt++;
		if (bufcnt == buffersize) {
			res = write(fd, ((void *)buffer), FRAME_SIZE * 2 * buffersize);
		}
	}
	pthread_mutex_unlock(&sound_lock);
	return res;
}


static int oss_write(struct ast_channel *chan, struct ast_frame *f)
{
	int res;
	static char sizbuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int pos;
	if (!full_duplex && (strlen(digits) || needhangup || needanswer)) {
		/* If we're half duplex, we have to switch to read mode
		   to honor immediate needs if necessary */
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
	f.timelen = 0;
	f.datalen = 0;
	f.data = NULL;
	f.offset = 0;
	f.src = type;
	f.mallocd = 0;
	
	if (needhangup) {
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
		chan->state = AST_STATE_UP;
		return &f;
	}
	
	res = soundcard_setinput(0);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set input mode\n");
		return NULL;
	}
	if (res > 0) {
		/* Theoretically shouldn't happen, but anyway, return a NULL frame */
		return &f;
	}
	res = read(funnel[0], buf + AST_FRIENDLY_OFFSET + readpos, FRAME_SIZE * 2 - readpos);
	if (res < 0) {
		ast_log(LOG_WARNING, "Error reading from sound device: %s\n", strerror(errno));
		return NULL;
	}
	readpos += res;
	
	if (readpos == FRAME_SIZE * 2) {
		/* A real frame */
		readpos = 0;
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.timelen = FRAME_SIZE / 8;
		f.datalen = FRAME_SIZE * 2;
		f.data = buf + AST_FRIENDLY_OFFSET;
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = type;
		f.mallocd = 0;
	}
	return &f;
}

static struct ast_channel *oss_new(struct chan_oss_pvt *p, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc();
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "OSS/%s", DEV_DSP + 5);
		tmp->type = type;
		tmp->fd = funnel[0];
		tmp->format = AST_FORMAT_SLINEAR;
		tmp->pvt->pvt = p;
		tmp->pvt->send_digit = oss_digit;
		tmp->pvt->send_text = oss_text;
		tmp->pvt->hangup = oss_hangup;
		tmp->pvt->answer = oss_answer;
		tmp->pvt->read = oss_read;
		tmp->pvt->call = oss_call;
		tmp->pvt->write = oss_write;
		if (strlen(p->context))
			strncpy(tmp->context, p->context, sizeof(tmp->context));
		if (strlen(p->exten))
			strncpy(tmp->exten, p->exten, sizeof(tmp->exten));
		if (strlen(language))
			strncpy(tmp->language, language, sizeof(tmp->language));
		p->owner = tmp;
		tmp->state = state;
		pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		pthread_mutex_unlock(&usecnt_lock);
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

static struct ast_channel *oss_request(char *type, int format, void *data)
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
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	if (!oss.owner) {
		ast_cli(fd, "No one is calling us\n");
		return RESULT_FAILURE;
	}
	needanswer++;
	return RESULT_SUCCESS;
}

static char sendtext_usage[] =
"Usage: send text <message>\n"
"       Sends a text message for display on the remote terminal.\n";

static int console_sendtext(int fd, int argc, char *argv[])
{
	int tmparg = 1;
	if (argc < 1)
		return RESULT_SHOWUSAGE;
	if (!oss.owner) {
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
"       Answers an incoming call on the console (OSS) channel.\n";

static int console_hangup(int fd, int argc, char *argv[])
{
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	if (!oss.owner) {
		ast_cli(fd, "No call to hangup up\n");
		return RESULT_FAILURE;
	}
	needhangup++;
	return RESULT_SUCCESS;
}

static char hangup_usage[] =
"Usage: hangup\n"
"       Hangs up any call currently placed on the console.\n";


static int console_dial(int fd, int argc, char *argv[])
{
	char tmp[256], *tmp2;
	char *mye, *myc;
	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;
	if (oss.owner) {
		if (argc == 2)
			strncat(digits, argv[1], sizeof(digits) - strlen(digits));
		else {
			ast_cli(fd, "You're already in a call.  You can use this only to dial digits until you hangup\n");
			return RESULT_FAILURE;
		}
		return RESULT_SUCCESS;
	}
	mye = exten;
	myc = context;
	if (argc == 2) {
		strncpy(tmp, argv[1], sizeof(tmp));
		strtok(tmp, "@");
		tmp2 = strtok(NULL, "@");
		if (strlen(tmp))
			mye = tmp;
		if (tmp2 && strlen(tmp2))
			myc = tmp2;
	}
	if (ast_exists_extension(NULL, myc, mye, 1)) {
		strncpy(oss.exten, mye, sizeof(oss.exten));
		strncpy(oss.context, myc, sizeof(oss.context));
		oss_new(&oss, AST_STATE_UP);
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
	{ { "send text", NULL }, console_sendtext, "Send text to the remote device", sendtext_usage },
	{ { "autoanswer", NULL }, console_autoanswer, "Sets/displays autoanswer", autoanswer_usage, autoanswer_complete }
};

int load_module()
{
	int res;
	int x;
	int flags;
	struct ast_config *cfg = ast_load(config);
	struct ast_variable *v;
	res = pipe(funnel);
	if (res) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		return -1;
	}
	/* We make the funnel so that writes to the funnel don't block...
	   Our "silly" thread can read to its heart content, preventing
	   recording overruns */
	flags = fcntl(funnel[1], F_GETFL);
#if 0
	fcntl(funnel[0], F_SETFL, flags | O_NONBLOCK);
#endif
	fcntl(funnel[1], F_SETFL, flags | O_NONBLOCK);
	res = soundcard_init();
	if (res < 0) {
		close(funnel[1]);
		close(funnel[0]);
		return -1;
	}
	if (!full_duplex)
		ast_log(LOG_WARNING, "XXX I don't work right with non-full duplex sound cards XXX\n");
	pthread_create(&silly, NULL, silly_thread, NULL);
	res = ast_channel_register(type, tdesc, AST_FORMAT_SLINEAR, oss_request);
	if (res < 0) {
		ast_log(LOG_ERROR, "Unable to register channel class '%s'\n", type);
		return -1;
	}
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_register(myclis + x);
	if (cfg) {
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "autoanswer"))
				autoanswer = ast_true(v->value);
			else if (!strcasecmp(v->name, "silencesuppression"))
				silencesuppression = ast_true(v->value);
			else if (!strcasecmp(v->name, "silencethreshold"))
				silencethreshold = atoi(v->value);
			else if (!strcasecmp(v->name, "context"))
				strncpy(context, v->value, sizeof(context));
			else if (!strcasecmp(v->name, "language"))
				strncpy(language, v->value, sizeof(language));
			else if (!strcasecmp(v->name, "extension"))
				strncpy(exten, v->value, sizeof(exten));
			v=v->next;
		}
		ast_destroy(cfg);
	}
	return 0;
}



int unload_module()
{
	int x;
	for (x=0;x<sizeof(myclis)/sizeof(struct ast_cli_entry); x++)
		ast_cli_unregister(myclis + x);
	close(sounddev);
	if (funnel[0] > 0) {
		close(funnel[0]);
		close(funnel[1]);
	}
	if (silly) {
		pthread_cancel(silly);
		pthread_join(silly, NULL);
	}
	if (oss.owner)
		ast_softhangup(oss.owner);
	if (oss.owner)
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
	pthread_mutex_lock(&usecnt_lock);
	res = usecnt;
	pthread_mutex_unlock(&usecnt_lock);
	return res;
}
