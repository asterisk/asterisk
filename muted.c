/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Updated for Mac OSX CoreAudio 
 * by Josh Roberson <josh@asteriasgi.com>
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
 * \brief Mute Daemon
 *
 * Specially written for Malcolm Davenport, but I think I'll use it too
 *
 */

#ifndef __Darwin__
#include <linux/soundcard.h>
#else
#include <CoreAudio/AudioHardware.h> 
#endif
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static char *config = "/etc/muted.conf";

static char host[256] = "";
static char user[256] = "";
static char pass[256] = "";
static int smoothfade = 0;
static int mutelevel = 20;
static int muted = 0;
static int needfork = 1;
static int debug = 0;
static int stepsize = 3;
#ifndef __Darwin__
static int mixchan = SOUND_MIXER_VOLUME;
#endif

struct subchannel {
	char *name;
	struct subchannel *next;
};

static struct channel {
	char *tech;
	char *location;
	struct channel *next;
	struct subchannel *subs;
} *channels;

static void add_channel(char *tech, char *location)
{
	struct channel *chan;
	chan = malloc(sizeof(struct channel));
	if (chan) {
		memset(chan, 0, sizeof(struct channel));
		chan->tech = strdup(tech);
		chan->location = strdup(location);
		chan->next = channels;
		channels = chan;
	}
	
}

static int load_config(void)
{
	FILE *f;
	char buf[1024];
	char *val;
	char *val2;
	int lineno=0;
	int x;
	f = fopen(config, "r");
	if (!f) {
		fprintf(stderr, "Unable to open config file '%s': %s\n", config, strerror(errno));
		return -1;
	}
	while(!feof(f)) {
		fgets(buf, sizeof(buf), f);
		if (!feof(f)) {
			lineno++;
			val = strchr(buf, '#');
			if (val) *val = '\0';
			while(strlen(buf) && (buf[strlen(buf) - 1] < 33))
				buf[strlen(buf) - 1] = '\0';
			if (!strlen(buf))
				continue;
			val = buf;
			while(*val) {
				if (*val < 33)
					break;
				val++;
			}
			if (*val) {
				*val = '\0';
				val++;
				while(*val && (*val < 33)) val++;
			}
			if (!strcasecmp(buf, "host")) {
				if (val && strlen(val))
					strncpy(host, val, sizeof(host) - 1);
				else
					fprintf(stderr, "host needs an argument (the host) at line %d\n", lineno);
			} else if (!strcasecmp(buf, "user")) {
				if (val && strlen(val))
					strncpy(user, val, sizeof(user) - 1);
				else
					fprintf(stderr, "user needs an argument (the user) at line %d\n", lineno);
			} else if (!strcasecmp(buf, "pass")) {
				if (val && strlen(val))
					strncpy(pass, val, sizeof(pass) - 1);
				else
					fprintf(stderr, "pass needs an argument (the password) at line %d\n", lineno);
			} else if (!strcasecmp(buf, "smoothfade")) {
				smoothfade = 1;
			} else if (!strcasecmp(buf, "mutelevel")) {
				if (val && (sscanf(val, "%d", &x) == 1) && (x > -1) && (x < 101)) {
					mutelevel = x;
				} else 
					fprintf(stderr, "mutelevel must be a number from 0 (most muted) to 100 (no mute) at line %d\n", lineno);
			} else if (!strcasecmp(buf, "channel")) {
				if (val && strlen(val)) {
					val2 = strchr(val, '/');
					if (val2) {
						*val2 = '\0';
						val2++;
						add_channel(val, val2);
					} else
						fprintf(stderr, "channel needs to be of the format Tech/Location at line %d\n", lineno);
				} else
					fprintf(stderr, "channel needs an argument (the channel) at line %d\n", lineno);
			} else {
				fprintf(stderr, "ignoring unknown keyword '%s'\n", buf);
			}
		}
	}
	fclose(f);
	if (!strlen(host))
		fprintf(stderr, "no 'host' specification in config file\n");
	else if (!strlen(user))
		fprintf(stderr, "no 'user' specification in config file\n");
	else if (!channels) 
		fprintf(stderr, "no 'channel' specifications in config file\n");
	else
		return 0;
	return -1;
}

static FILE *astf;
#ifndef __Darwin__
static int mixfd;

static int open_mixer(void)
{
	mixfd = open("/dev/mixer", O_RDWR);
	if (mixfd < 0) {
		fprintf(stderr, "Unable to open /dev/mixer: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}
#endif /* !__Darwin */

static int connect_asterisk(void)
{
	int sock;
	struct hostent *hp;
	char *ports;
	int port = 5038;
	struct sockaddr_in sin;
	ports = strchr(host, ':');
	if (ports) {
		*ports = '\0';
		ports++;
		if ((sscanf(ports, "%d", &port) != 1) || (port < 1) || (port > 65535)) {
			fprintf(stderr, "'%s' is not a valid port number in the hostname\n", ports);
			return -1;
		}
	}
	hp = gethostbyname(host);
	if (!hp) {
		fprintf(stderr, "Can't find host '%s'\n", host);
		return -1;
	}
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
		return -1;
	}
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	if (connect(sock, &sin, sizeof(sin))) {
		fprintf(stderr, "Failed to connect to '%s' port '%d': %s\n", host, port, strerror(errno));
		close(sock);
		return -1;
	}
	astf = fdopen(sock, "r+");
	if (!astf) {
		fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
		close(sock);
		return -1;
	}
	return 0;
}

static char *get_line(void)
{
	static char buf[1024];
	if (fgets(buf, sizeof(buf), astf)) {
		while(strlen(buf) && (buf[strlen(buf) - 1] < 33))
			buf[strlen(buf) - 1] = '\0';
		return buf;
	} else
		return NULL;
}

static int login_asterisk(void)
{
	char *welcome;
	char *resp;
	if (!(welcome = get_line())) {
		fprintf(stderr, "disconnected (1)\n");
		return -1;
	}
	fprintf(astf, 
		"Action: Login\r\n"
		"Username: %s\r\n"
		"Secret: %s\r\n\r\n", user, pass);
	if (!(welcome = get_line())) {
		fprintf(stderr, "disconnected (2)\n");
		return -1;
	}
	if (strcasecmp(welcome, "Response: Success")) {
		fprintf(stderr, "login failed ('%s')\n", welcome);
		return -1;
	}
	/* Eat the rest of the event */
	while((resp = get_line()) && strlen(resp));
	if (!resp) {
		fprintf(stderr, "disconnected (3)\n");
		return -1;
	}
	fprintf(astf, 
		"Action: Status\r\n\r\n");
	if (!(welcome = get_line())) {
		fprintf(stderr, "disconnected (4)\n");
		return -1;
	}
	if (strcasecmp(welcome, "Response: Success")) {
		fprintf(stderr, "status failed ('%s')\n", welcome);
		return -1;
	}
	/* Eat the rest of the event */
	while((resp = get_line()) && strlen(resp));
	if (!resp) {
		fprintf(stderr, "disconnected (5)\n");
		return -1;
	}
	return 0;
}

static struct channel *find_channel(char *channel)
{
	char tmp[256] = "";
	char *s, *t;
	struct channel *chan;
	strncpy(tmp, channel, sizeof(tmp) - 1);
	s = strchr(tmp, '/');
	if (s) {
		*s = '\0';
		s++;
		t = strrchr(s, '-');
		if (t) {
			*t = '\0';
		}
		if (debug)
			printf("Searching for '%s' tech, '%s' location\n", tmp, s);
		chan = channels;
		while(chan) {
			if (!strcasecmp(chan->tech, tmp) && !strcasecmp(chan->location, s)) {
				if (debug)
					printf("Found '%s'/'%s'\n", chan->tech, chan->location);
				break;
			}
			chan = chan->next;
		}
	} else
		chan = NULL;
	return chan;
}

#ifndef __Darwin__
static int getvol(void)
{
	int vol;

	if (ioctl(mixfd, MIXER_READ(mixchan), &vol)) {
#else
static float getvol(void)
{
	float volumeL, volumeR, vol;
	OSStatus err;
	AudioDeviceID device;
	UInt32 size;
	UInt32 channels[2];

	size = sizeof(device);
	err = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice, &size, &device);
	size = sizeof(channels);
	if (!err) 
		err = AudioDeviceGetProperty(device, 0, false, kAudioDevicePropertyPreferredChannelsForStereo, &size, &channels);
	size = sizeof(vol);
	if (!err)
		err = AudioDeviceGetProperty(device, channels[0], false, kAudioDevicePropertyVolumeScalar, &size, &volumeL);
	if (!err)
		err = AudioDeviceGetProperty(device, channels[1], false, kAudioDevicePropertyVolumeScalar, &size, &volumeR);
	if (!err)
		vol = (volumeL < volumeR) ? volumeR : volumeL;
	else {
#endif
		fprintf(stderr, "Unable to read mixer volume: %s\n", strerror(errno));
		return -1;
	}
	return vol;
}

#ifndef __Darwin__
static int setvol(int vol)
#else
static int setvol(float vol)
#endif
{
#ifndef __Darwin__
	if (ioctl(mixfd, MIXER_WRITE(mixchan), &vol)) {
#else	
	float volumeL = vol;
	float volumeR = vol;
	OSStatus err;
	AudioDeviceID device;
	UInt32 size;
	UInt32 channels[2];

	size = sizeof(device);
	err = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice, &size, &device);
	size = sizeof(channels);
	err = AudioDeviceGetProperty(device, 0, false, kAudioDevicePropertyPreferredChannelsForStereo, &size, &channels);
	size = sizeof(vol);
	if (!err)
		err = AudioDeviceSetProperty(device, 0, channels[0], false, kAudioDevicePropertyVolumeScalar, size, &volumeL);
	if (!err)
		err = AudioDeviceSetProperty(device, 0, channels[1], false, kAudioDevicePropertyVolumeScalar, size, &volumeR); 
	if (err) {
#endif

		fprintf(stderr, "Unable to write mixer volume: %s\n", strerror(errno));
		return -1;

	}
	return 0;
}

#ifndef __Darwin__
static int oldvol = 0;
static int mutevol = 0;
#else
static float oldvol = 0;
static float mutevol = 0;
#endif

#ifndef __Darwin__
static int mutedlevel(int orig, int mutelevel)
{
	int l = orig >> 8;
	int r = orig & 0xff;
	l = (float)(mutelevel) * (float)(l) / 100.0;
	r = (float)(mutelevel) * (float)(r) / 100.0;

	return (l << 8) | r;
#else
static float mutedlevel(float orig, float mutelevel)
{
	float master = orig;
	master = mutelevel * master / 100.0;
	return master;
#endif
	
}

static void mute(void)
{
#ifndef __Darwin__
	int vol;
	int start;
	int x;
#else
	float vol;
	float start = 1.0;
	float x;
#endif
	vol = getvol();
	oldvol = vol;
	if (smoothfade)
#ifdef __Darwin__ 
		start = mutelevel;
#else
		start = 100;
	else
		start = mutelevel;
#endif
	for (x=start;x>=mutelevel;x-=stepsize) {
		mutevol = mutedlevel(vol, x);
		setvol(mutevol);
		/* Wait 0.01 sec */
		usleep(10000);
	}
	mutevol = mutedlevel(vol, mutelevel);
	setvol(mutevol);
	if (debug)
#ifdef __Darwin__
		printf("Mute from '%f' to '%f'!\n", oldvol, mutevol);
#else
		printf("Mute from '%04x' to '%04x'!\n", oldvol, mutevol);
#endif
	muted = 1;
}

static void unmute(void)
{
#ifdef __Darwin__
	float vol;
	float start;
	float x;
#else
	int vol;
	int start;
	int x;
#endif
	vol = getvol();
	if (debug)
#ifdef __Darwin__
		printf("Unmute from '%f' (should be '%f') to '%f'!\n", vol, mutevol, oldvol);
	mutevol = vol;
	if (vol == mutevol) {
#else
		printf("Unmute from '%04x' (should be '%04x') to '%04x'!\n", vol, mutevol, oldvol);
	if ((int)vol == mutevol) {
#endif
		if (smoothfade)
			start = mutelevel;
		else
#ifdef __Darwin__
			start = 1.0;
#else
			start = 100;
#endif
		for (x=start;x<100;x+=stepsize) {
			mutevol = mutedlevel(oldvol, x);
			setvol(mutevol);
			/* Wait 0.01 sec */
			usleep(10000);
		}
		setvol(oldvol);
	} else
		printf("Whoops, it's already been changed!\n");
	muted = 0;
}

static void check_mute(void)
{
	int offhook = 0;
	struct channel *chan;
	chan = channels;
	while(chan) {
		if (chan->subs) {
			offhook++;
			break;
		}
		chan = chan->next;
	}
	if (offhook && !muted)
		mute();
	else if (!offhook && muted)
		unmute();
}

static void delete_sub(struct channel *chan, char *name)
{
	struct subchannel *sub, *prev;
	prev = NULL;
	sub = chan->subs;
	while(sub) {
		if (!strcasecmp(sub->name, name)) {
			if (prev)
				prev->next = sub->next;
			else
				chan->subs = sub->next;
			free(sub->name);
			free(sub);
			return;
		}
		prev = sub;
		sub = sub->next;
	}
}

static void append_sub(struct channel *chan, char *name)
{
	struct subchannel *sub;
	sub = chan->subs;
	while(sub) {
		if (!strcasecmp(sub->name, name)) 
			return;
		sub = sub->next;
	}
	sub = malloc(sizeof(struct subchannel));
	if (sub) {
		memset(sub, 0, sizeof(struct subchannel));
		sub->name = strdup(name);
		sub->next = chan->subs;
		chan->subs = sub;
	}
}

static void hangup_chan(char *channel)
{
	struct channel *chan;
	if (debug)
		printf("Hangup '%s'\n", channel);
	chan = find_channel(channel);
	if (chan)
		delete_sub(chan, channel);
	check_mute();
}

static void offhook_chan(char *channel)
{
	struct channel *chan;
	if (debug)
		printf("Offhook '%s'\n", channel);
	chan = find_channel(channel);
	if (chan)
		append_sub(chan, channel);
	check_mute();
}

static int wait_event(void)
{
	char *resp;
	char event[120]="";
	char channel[120]="";
	char oldname[120]="";
	char newname[120]="";
	resp = get_line();
	if (!resp) {
		fprintf(stderr, "disconnected (6)\n");
		return -1;
	}
	if (!strncasecmp(resp, "Event: ", strlen("Event: "))) {
		strncpy(event, resp + strlen("Event: "), sizeof(event) - 1);
		/* Consume the rest of the non-event */
		while((resp = get_line()) && strlen(resp)) {
			if (!strncasecmp(resp, "Channel: ", strlen("Channel: ")))
				strncpy(channel, resp + strlen("Channel: "), sizeof(channel) - 1);
			if (!strncasecmp(resp, "Newname: ", strlen("Newname: ")))
				strncpy(newname, resp + strlen("Newname: "), sizeof(newname) - 1);
			if (!strncasecmp(resp, "Oldname: ", strlen("Oldname: ")))
				strncpy(oldname, resp + strlen("Oldname: "), sizeof(oldname) - 1);
		}
		if (strlen(channel)) {
			if (!strcasecmp(event, "Hangup")) 
				hangup_chan(channel);
			else
				offhook_chan(channel);
		}
		if (strlen(newname) && strlen(oldname)) {
			if (!strcasecmp(event, "Rename")) {
				hangup_chan(oldname);
				offhook_chan(newname);
			}
		}
	} else {
		/* Consume the rest of the non-event */
		while((resp = get_line()) && strlen(resp));
	}
	if (!resp) {
		fprintf(stderr, "disconnected (7)\n");
		return -1;
	}
	return 0;
}

static void usage(void)
{
	printf("Usage: muted [-f] [-d]\n"
	       "        -f : Do not fork\n"
		   "        -d : Debug (implies -f)\n");
}

int main(int argc, char *argv[])
{
	int x;
	while((x = getopt(argc, argv, "fhd")) > 0) {
		switch(x) {
		case 'd':
			debug = 1;
			needfork = 0;
			break;
		case 'f':
			needfork = 0;
			break;
		case 'h':
			/* Fall through */
		default:
			usage();
			exit(1);
		}
	}
	if (load_config())
		exit(1);
#ifndef __Darwin__
	if (open_mixer())
		exit(1);
#endif
	if (connect_asterisk()) {
#ifndef __Darwin__
		close(mixfd);
#endif
		exit(1);
	}
	if (login_asterisk()) {
#ifndef __Darwin__		
		close(mixfd);
#endif
		fclose(astf);
		exit(1);
	}
	if (needfork)
		daemon(0,0);
	for(;;) {
		if (wait_event()) {
			fclose(astf);
			while(connect_asterisk()) {
				sleep(5);
			}
			if (login_asterisk()) {
				fclose(astf);
				exit(1);
			}
		}
	}
	exit(0);
}
