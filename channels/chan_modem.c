/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * A/Open ITU-56/2 Voice Modem Driver (Rockwell, IS-101, and others)
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/vmodem.h>
#include <asterisk/utils.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/signal.h>
#include <ctype.h>

/* Up to 10 seconds for an echo to arrive */
#define ECHO_TIMEOUT 10

static char *desc = "Generic Voice Modem Driver";
static char *tdesc = "Generic Voice Modem Channel Driver";
static char *type = "Modem";
static char *config = "modem.conf";
static char dialtype = 'T';
static int gmode = MODEM_MODE_IMMEDIATE;

/* Default modem type */
static char mtype[80] = "autodetect";
/* Default context for incoming calls */
static char context[AST_MAX_EXTENSION]= "default";

/* Default language */
static char language[MAX_LANGUAGE] = "";

/* Initialization String */
static char initstr[AST_MAX_INIT_STR] = "ATE0Q0";

/* Default MSN */
static char msn[AST_MAX_EXTENSION]="";

/* Default Listen */
static char incomingmsn[AST_MAX_EXTENSION]="";

/* Default DTMF-detection mode (i4l/asterisk) */
static int dtmfmode = MODEM_DTMF_AST;
/* Default DTMF-generation mode (i4l (outband) / asterisk (inband) */
static int dtmfmodegen = MODEM_DTMF_AST;

struct ast_dsp *dsp = NULL;

/* Default valid outgoing MSN */
static char outgoingmsn[AST_MAX_EXTENSION]="";

/* Default group */
static unsigned int cur_group = 0;

static int usecnt =0;

static int baudrate = 115200;

static int stripmsd = 0;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of ast_modem_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

/* The private structures of the Phone Jack channels are linked for
   selecting outgoing channels */
   
static struct ast_modem_pvt  *iflist = NULL;

static int modem_digit(struct ast_channel *ast, char digit)
{
	struct ast_modem_pvt *p;
	p = ast->pvt->pvt;
	if (p->mc->dialdigit)
		return p->mc->dialdigit(p, digit);
	ast_log(LOG_DEBUG, "Channel %s lacks digit dialing\n", ast->name);
	return -1;
}

static struct ast_modem_driver *drivers = NULL;

static struct ast_frame *modem_read(struct ast_channel *);

static struct ast_modem_driver *find_capability(char *ident)
{
	struct ast_modem_driver *mc;
	int x;
	mc = drivers;
	while(mc) {
		for (x=0;mc->idents[x];x++) {
			if (!strcmp(ident, mc->idents[x])) 
				break;
		}
		if (mc->idents[x])
			break;
		mc = mc->next;
	}
	if (mc) {
		if (mc->incusecnt)
			mc->incusecnt();
	}
	return mc;
}

static struct ast_modem_driver *find_driver(char *drv)
{
	struct ast_modem_driver *mc;
	mc = drivers;
	while(mc) {
		if (!strcasecmp(mc->name, drv))	
			break;
		mc = mc->next;
	}
	if (mc) {
		if (mc->incusecnt)
			mc->incusecnt();
	}
	return mc;
}

int ast_register_modem_driver(struct ast_modem_driver *mc)
{
	mc->next = drivers;
	drivers = mc;
	return 0;
}

int ast_unregister_modem_driver(struct ast_modem_driver *mc)
{
	struct ast_modem_driver *last = NULL, *cur;
	cur = drivers;
	while(cur) {
		if (cur == mc) {
			if (last)
				last->next = mc->next;
			else
				drivers = mc->next;
			return 0;
		}
		cur = cur->next;
	}
	return -1;
}

static int modem_call(struct ast_channel *ast, char *idest, int timeout)
{
	struct ast_modem_pvt *p;
	int ms = timeout;
	char rdest[80], *where, dstr[100] = "";
	char *stringp=NULL;
	strncpy(rdest, idest, sizeof(rdest)-1);
	stringp=rdest;
	strsep(&stringp, ":");
	where = strsep(&stringp, ":");
	if (!where) {
		ast_log(LOG_WARNING, "Destination %s requres a real destination (device:destination)\n", idest);
		return -1;
	}
	p = ast->pvt->pvt;
	strncpy(dstr, where + p->stripmsd, sizeof(dstr) - 1);
	/* if not a transfer or just sending tones, must be in correct state */
	if (strcasecmp(rdest, "transfer") && strcasecmp(rdest,"sendtones")) {
		if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
			ast_log(LOG_WARNING, "modem_call called on %s, neither down nor reserved\n", ast->name);
			return -1;
		}
	} 
	if (!strcasecmp(rdest,"transfer")) /* if a transfer, put in transfer stuff */
	{
		snprintf(dstr, sizeof(dstr), "!,%s", where + p->stripmsd);
	}
	if (!strcasecmp(where, "handset")) {
		if (p->mc->setdev)
			if (p->mc->setdev(p, MODEM_DEV_HANDSET))
				return -1;
		/* Should be immediately up */
		ast_setstate(ast, AST_STATE_UP);
	} else {
		if (p->mc->setdev)
			if (p->mc->setdev(p, MODEM_DEV_TELCO_SPK))
				return -1;
		if (p->mc->dial)
			p->mc->dial(p, dstr);
		ast_setstate(ast, AST_STATE_DIALING);
		while((ast->_state != AST_STATE_UP) && (ms > 0)) {
			ms = ast_waitfor(ast, ms);
			/* Just read packets and watch what happens */
			if (ms > 0) {
				if (!modem_read(ast))
					return -1;
			}
		}
		if (ms < 0)	
			return -1;
	}
	return 0;
}

int ast_modem_send(struct ast_modem_pvt *p, char *cmd, int len)
{
	int i;
	usleep(5000);
	if (!len) {
		for(i = 0; cmd[i];)
		   {
			if (fwrite(cmd + i,1,1,p->f) != 1)
			   {
				if (errno == EWOULDBLOCK) continue;
				return -1;
			   }
			i++;
		   }
		tcdrain(fileno(p->f)); 
		fprintf(p->f,"\r\n");
		return 0;
	} else {
		if (fwrite(cmd, 1, len, p->f) < len)
			return -1;
		return 0;
	}
}

int ast_modem_read_response(struct ast_modem_pvt *p, int timeout)
{
	int res = -1,c,i;
	timeout *= 1000;
	p->response[0] = 0;
	c = i = 0;
	do {
		res = ast_waitfor_n_fd(&p->fd, 1, &timeout, NULL);
		if (res < 0) {
			strncpy(p->response, "(No Response)", sizeof(p->response)-1);
			return -1;
		}
		  /* get no more then buffer length */
		while(i < sizeof(p->response) - 1)
		{
			c = fgetc(p->f);  /* get a char */
			if (c < 1) /* if error */
			{
				  /* if nothing in buffer, go back into timeout stuff */
				if (errno == EWOULDBLOCK) break;
				/* return as error */
				strncpy(p->response, "(No Response)", sizeof(p->response)-1);
				return -1;
			}
		 	  /* save char */
			p->response[i++] = c;
			p->response[i] = 0;			
			  /* if end of input */
			if (c == '\n') break;
		}
		if (c >= 0)  /* if input terminated normally */
		{
			  /* ignore just CR/LF */
			if (!strcmp(p->response,"\r\n"))
			{
				  /* reset input buffer stuff */
				i = 0; 
				p->response[0] = 0;
			}
			else /* otherwise return with info in buffer */
			{
				return 0;
			}
		}
	} while(timeout > 0);
	strncpy(p->response, "(No Response)", sizeof(p->response)-1);
	return -1;
}

int ast_modem_expect(struct ast_modem_pvt *p, char *result, int timeout)
{
	int res = -1;
	timeout *= 1000;
	strncpy(p->response, "(No Response)", sizeof(p->response)-1);
	do {
		res = ast_waitfor_n_fd(&p->fd, 1, &timeout, NULL);
		if (res < 0) {
			return -1;
		}
		/* Read a response */
		fgets(p->response, sizeof(p->response), p->f);
#if	0
		fprintf(stderr, "Modem said: %s", p->response);
#endif
		if (!strncasecmp(p->response, result, strlen(result))) 
			return 0;
	} while(timeout > 0);
	return -1;
}

void ast_modem_trim(char *s)
{
	int x;
	x = strlen(s) - 1;
	while(x >= 0) {
		if ((s[x] != '\r') && (s[x] != '\n') && (s[x] != ' '))
			break;
		s[x] = '\0';
		x--;
	}
}

static int modem_setup(struct ast_modem_pvt *p, int baudrate)
{

	/* Make sure there's a modem there and that it's in a reasonable 
	   mode.  Set the baud rate, etc.  */
	char identity[256];
	char *ident = NULL;
	char etx[2] = { 0x10, '!' }; 
	if (option_debug)
		ast_log(LOG_DEBUG, "Setting up modem %s\n", p->dev);
	if (ast_modem_send(p, etx, 2)) {
		ast_log(LOG_WARNING, "Failed to send ETX?\n");
		return -1;
	}
	if (ast_modem_send(p, "\r\n", 2)) {
		ast_log(LOG_WARNING, "Failed to send enter?\n");
		return -1;
	}
	usleep(10000);
	/* Read any outstanding stuff */
	while(!ast_modem_read_response(p, 0));
	if (ast_modem_send(p, "ATZ", 0)) {
		ast_log(LOG_WARNING, "Modem not responding on %s\n", p->dev);
		return -1;
	}
	if (ast_modem_expect(p, "OK", ECHO_TIMEOUT)) {
		ast_log(LOG_WARNING, "Modem reset failed: %s\n", p->response);
		return -1;
	}
	if (ast_modem_send(p, p->initstr, 0)) {
		ast_log(LOG_WARNING, "Modem not responding on %s\n", p->dev);
		return -1;
	}
	if (ast_modem_expect(p, "OK", ECHO_TIMEOUT)) {
		ast_log(LOG_WARNING, "Modem initialization failed: %s\n", p->response);
		return -1;
	}
	if (ast_modem_send(p, "ATI3", 0)) {
		ast_log(LOG_WARNING, "Modem not responding on %s\n", p->dev);
		return -1;
	}
	if (ast_modem_read_response(p, ECHO_TIMEOUT)) {
		ast_log(LOG_WARNING, "Modem did not provide identification\n");
		return -1;
	}
	strncpy(identity, p->response, sizeof(identity)-1);
	ast_modem_trim(identity);
	if (ast_modem_expect(p, "OK", ECHO_TIMEOUT)) {
		ast_log(LOG_WARNING, "Modem did not provide identification\n");
		return -1;
	}
	if (!strcasecmp(mtype, "autodetect")) {
		p->mc = find_capability(identity);
		if (!p->mc) {
			ast_log(LOG_WARNING, "Unable to autodetect modem.  You'll need to specify a driver in modem.conf.  Please report modem identification (%s) and which driver works to markster@linux-support.net.\n", identity); 
			return -1;
		}
	} else {
		p->mc = find_driver(mtype);
		if (!p->mc) {
			ast_log(LOG_WARNING, "No driver for modem type '%s'\n", mtype);
			return -1;
		}
	}
	if (p->mc->init) {
		if (p->mc->init(p)) {
			ast_log(LOG_WARNING, "Modem Initialization Failed on '%s', driver %s.\n", p->dev, p->mc->name);
			p->mc->decusecnt();
			return -1;
		}
	}			
	if (option_verbose > 2) {
		ast_verbose(VERBOSE_PREFIX_3 "Configured modem %s with driver %s (%s)\n", p->dev, p->mc->name, p->mc->identify ? (ident = p->mc->identify(p)) : "No identification");
	}
	if (ident)
		free(ident);
	return 0;
}

static int modem_hangup(struct ast_channel *ast)
{
	struct ast_modem_pvt *p;
	if (option_debug)
		ast_log(LOG_DEBUG, "modem_hangup(%s)\n", ast->name);
	p = ast->pvt->pvt;
	/* Hang up */
	if (p->mc->hangup)
		p->mc->hangup(p);
	/* Re-initialize */
	if (p->mc->init)
		p->mc->init(p);
	ast_setstate(ast, AST_STATE_DOWN);
	memset(p->cid_num, 0, sizeof(p->cid_num));
	memset(p->cid_name, 0, sizeof(p->cid_name));
	memset(p->dnid, 0, sizeof(p->dnid));
	((struct ast_modem_pvt *)(ast->pvt->pvt))->owner = NULL;
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", ast->name);
	ast->pvt->pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	restart_monitor();
	return 0;
}

static int modem_answer(struct ast_channel *ast)
{
	struct ast_modem_pvt *p;
	int res=0;
	if (option_debug)
		ast_log(LOG_DEBUG, "modem_answer(%s)\n", ast->name);
	p = ast->pvt->pvt;
	if (p->mc->answer) {
		res = p->mc->answer(p);
	}
	if (!res) {
		ast->rings = 0;
		ast_setstate(ast, AST_STATE_UP);
	}
	return res;
}

#if	0
static char modem_2digit(char c)
{
	if (c == 12)
		return '#';
	else if (c == 11)
		return '*';
	else if ((c < 10) && (c >= 0))
		return '0' + c - 1;
	else
		return '?';
}
#endif
static struct ast_frame *modem_read(struct ast_channel *ast)
{
	struct ast_modem_pvt *p = ast->pvt->pvt;
	struct ast_frame *fr=NULL;
	if (p->mc->read)
		fr = p->mc->read(p);
	return fr;
}

static int modem_write(struct ast_channel *ast, struct ast_frame *frame)
{
	int res=0;
	long flags;
	struct ast_modem_pvt *p = ast->pvt->pvt;

	/* Modems tend to get upset when they receive data whilst in
	 * command mode. This makes esp. dial commands short lived.
	 *     Pauline Middelink - 2002-09-24 */
	if (ast->_state != AST_STATE_UP)
		return 0;

	/* Temporarily make non-blocking */
	flags = fcntl(ast->fds[0], F_GETFL);
	fcntl(ast->fds[0], F_SETFL, flags | O_NONBLOCK);

	if (p->mc->write)
		res = p->mc->write(p, frame);

	/* Block again */
	fcntl(ast->fds[0], F_SETFL, flags);
	return res;
}

static int modem_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
        struct ast_modem_pvt *p = newchan->pvt->pvt;
ast_log(LOG_WARNING, "fixup called\n");
	if (p->owner!=oldchan) {
	    ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n",oldchan,p->owner);
	    return -1;
	}
	p->owner = newchan;
        return 0; 
}

struct ast_channel *ast_modem_new(struct ast_modem_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "Modem[%s]/%s", i->mc->name, i->dev + 5);
		tmp->type = type;
		tmp->fds[0] = i->fd;
		tmp->nativeformats = i->mc->formats;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = modem_digit;
		tmp->pvt->call = modem_call;
		tmp->pvt->hangup = modem_hangup;
		tmp->pvt->answer = modem_answer;
		tmp->pvt->read = modem_read;
		tmp->pvt->write = modem_write;
		tmp->pvt->fixup = modem_fixup;
		strncpy(tmp->context, i->context, sizeof(tmp->context)-1);

		if (!ast_strlen_zero(i->cid_num))
			tmp->cid.cid_num = strdup(i->cid_num);
		if (!ast_strlen_zero(i->cid_name))
			tmp->cid.cid_name = strdup(i->cid_name);

		if (strlen(i->language))
			strncpy(tmp->language,i->language, sizeof(tmp->language)-1);
		if (strlen(i->dnid))
			strncpy(tmp->exten, i->dnid, sizeof(tmp->exten) - 1);
		i->owner = tmp;
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
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

static void modem_mini_packet(struct ast_modem_pvt *i)
{
	struct ast_frame *fr;
	fr = i->mc->read(i);
	if (!fr) return;
	if (fr->frametype == AST_FRAME_CONTROL) {
		if (fr->subclass == AST_CONTROL_RING) {
			ast_modem_new(i, AST_STATE_RING);
		}
	}
}

static void *do_monitor(void *data)
{
	fd_set rfds, efds;
	int n, res;
	struct ast_modem_pvt *i;
	/* This thread monitors all the frame relay interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
#if 0
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL)) {
		ast_log(LOG_WARNING, "Unable to set cancel type to asynchronous\n");
		return NULL;
	}
#endif
	for(;;) {
		/* Don't let anybody kill us right away.  Nobody should lock the interface list
		   and wait for the monitor list, but the other way around is okay. */
		if (ast_mutex_lock(&monlock)) {
			ast_log(LOG_ERROR, "Unable to grab monitor lock\n");
			return NULL;
		}
		/* Lock the interface list */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_ERROR, "Unable to grab interface lock\n");
			ast_mutex_unlock(&monlock);
			return NULL;
		}
		/* Build the stuff we're going to select on, that is the socket of every
		   ast_modem_pvt that does not have an associated owner channel */
		n = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&efds);
		i = iflist;
		while(i) {
			if (FD_ISSET(i->fd, &rfds)) 
				ast_log(LOG_WARNING, "Descriptor %d appears twice (%s)?\n", i->fd, i->dev);
			if (!i->owner) {
				/* This needs to be watched, as it lacks an owner */
				FD_SET(i->fd, &rfds);
				FD_SET(i->fd, &efds);
				if (i->fd > n)
					n = i->fd;
			}
			
			i = i->next;
		}
		/* Okay, now that we know what to do, release the interface lock */
		ast_mutex_unlock(&iflock);
		
		/* And from now on, we're okay to be killed, so release the monitor lock as well */
		ast_mutex_unlock(&monlock);
#if 0
		ast_log(LOG_DEBUG, "In monitor, n=%d, pid=%d\n", n, getpid());
#endif
		/* Wait indefinitely for something to happen */
		pthread_testcancel();
		res = ast_select(n + 1, &rfds, NULL, &efds, NULL);
		pthread_testcancel();
		/* Okay, select has finished.  Let's see what happened.  */
		if (res < 1) {
			if ((errno != EINTR) && (errno != EAGAIN))
				ast_log(LOG_WARNING, "select return %d: %s\n", res, strerror(errno));
			continue;
		}
		/* Alright, lock the interface list again, and let's look and see what has
		   happened */
		if (ast_mutex_lock(&iflock)) {
			ast_log(LOG_WARNING, "Unable to lock the interface list\n");
			continue;
		}
		i = iflist;
		while(i) {
			if (FD_ISSET(i->fd, &rfds) || FD_ISSET(i->fd, &efds)) {
				if (i->owner) {
					ast_log(LOG_WARNING, "Whoa....  I'm owned but found (%d, %s)...\n", i->fd, i->dev);
					i = i->next;
					continue;
				}
				modem_mini_packet(i);
			}
			i=i->next;
		}
		ast_mutex_unlock(&iflock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor()
{
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	if (ast_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread != AST_PTHREADT_NULL) {
		pthread_cancel(monitor_thread);
		/* Nudge it a little, as it's probably stuck in select */
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}
	/* Start a new monitor */
	if (ast_pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
		return -1;
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static void stty(struct ast_modem_pvt *p)
{
	struct termios mode;
	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(p->fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", p->dev, strerror(errno));
		return;
	}
#ifndef SOLARIS
	cfmakeraw(&mode);
#else
        mode.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                        |INLCR|IGNCR|ICRNL|IXON);
        mode.c_oflag &= ~OPOST;
        mode.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
        mode.c_cflag &= ~(CSIZE|PARENB);
        mode.c_cflag |= CS8;
#endif

	cfsetispeed(&mode, B115200);
	cfsetospeed(&mode, B115200);
	if (tcsetattr(p->fd, TCSANOW, &mode)) 
		ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", p->dev, strerror(errno));
	
}

static struct ast_modem_pvt *mkif(char *iface)
{
	/* Make a ast_modem_pvt structure for this interface */
	struct ast_modem_pvt *tmp;
#if 0
	int flags;	
#endif
	
	tmp = malloc(sizeof(struct ast_modem_pvt));
	if (tmp) {
		memset(tmp, 0, sizeof(struct ast_modem_pvt));
		tmp->fd = open(iface, O_RDWR | O_NONBLOCK);
		if (tmp->fd < 0) {
			ast_log(LOG_WARNING, "Unable to open '%s'\n", iface);
			free(tmp);
			return NULL;
		}
		strncpy(tmp->language, language, sizeof(tmp->language)-1);
		strncpy(tmp->msn, msn, sizeof(tmp->msn)-1);
		strncpy(tmp->incomingmsn, incomingmsn, sizeof(tmp->incomingmsn)-1);
		tmp->dtmfmode = dtmfmode;
		tmp->dtmfmodegen = dtmfmodegen;
		snprintf(tmp->outgoingmsn, sizeof(tmp->outgoingmsn), ",%s,", outgoingmsn);
		strncpy(tmp->dev, iface, sizeof(tmp->dev)-1);
		/* Maybe in the future we want to allow variable
		   serial settings */
		stty(tmp);
		tmp->f = fdopen(tmp->fd, "w+");
		/* Disable buffering */
		setvbuf(tmp->f, NULL, _IONBF,0);
		if (tmp->f < 0) {
			ast_log(LOG_WARNING, "Unable to fdopen '%s'\n", iface);
			free(tmp);
			return NULL;
		}
		tmp->owner = NULL;
		tmp->ministate = 0;
		tmp->stripmsd = stripmsd;
		tmp->dialtype = dialtype;
		tmp->mode = gmode;
		tmp->group = cur_group;
		memset(tmp->cid_num, 0, sizeof(tmp->cid_num));
		memset(tmp->cid_name, 0, sizeof(tmp->cid_name));
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		strncpy(tmp->initstr, initstr, sizeof(tmp->initstr)-1);
		tmp->next = NULL;
		tmp->obuflen = 0;
		
		if (modem_setup(tmp, baudrate) < 0) {
			ast_log(LOG_WARNING, "Unable to configure modem '%s'\n", iface);
			free(tmp);
			return NULL;
		}
	}
	return tmp;
}

static struct ast_channel *modem_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct ast_modem_pvt *p;
	struct ast_channel *tmp = NULL;
	char dev[80];
	unsigned int group = 0;
	char *stringp=NULL;
	strncpy(dev, (char *)data, sizeof(dev)-1);
	stringp=dev;
	strsep(&stringp, ":");
	oldformat = format;

	if (dev[0]=='g' && isdigit(dev[1])) {
		/* Retrieve the group number */
		if (sscanf(dev+1, "%u", &group) < 1) {
			ast_log(LOG_WARNING, "Unable to determine group from [%s]\n", (char *)data);
			return NULL;
		}
		group = 1 << group;
	}

	/* Search for an unowned channel */
	if (ast_mutex_lock(&iflock)) {
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return NULL;
	}
	p = iflist;
	while(p) {
		if (group) {
			/* if it belongs to the proper group, and the format matches
			 * and it is not in use, we found a candidate! */
			if (p->group & group &&
			    p->mc->formats & format &&
			    !p->owner) {
				/* XXX Not quite sure that not having an owner is
				 * sufficient evidence of beeing a free device XXX */
				tmp = ast_modem_new(p, AST_STATE_DOWN);
				restart_monitor();
				break;
			}
		} else {
			if (!strcmp(dev, p->dev + 5)) {
				if (p->mc->formats & format) {
					if (!p->owner) {
						tmp = ast_modem_new(p, AST_STATE_DOWN);
						restart_monitor();
						break;
					} else
						ast_log(LOG_WARNING, "Device '%s' is busy\n", p->dev);
				} else 
					ast_log(LOG_WARNING, "Asked for a format %s line on %s\n", ast_getformatname(format), p->dev);
				break;
			}
		}
		p = p->next;
	}
	if (!p) 
		ast_log(LOG_WARNING, "Requested device '%s' does not exist\n", dev);
	
	ast_mutex_unlock(&iflock);
	return tmp;
}

static unsigned int get_group(char *s)
{
	char *piece;
	int start, finish,x;
	unsigned int group = 0;
	char *copy = ast_strdupa(s);
	char *stringp=NULL;
	if (!copy) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return 0;
	}
	stringp=copy;
	piece = strsep(&stringp, ",");
	while(piece) {
		if (sscanf(piece, "%d-%d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(piece, "%d", &start)) {
			/* Just one */
			finish = start;
		} else {
			ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'.  Using '0'\n", s,piece);
			return 0;
		}
		piece = strsep(&stringp, ",");

		for (x=start;x<=finish;x++) {
			if ((x > 31) || (x < 0)) {
				ast_log(LOG_WARNING, "Ignoring invalid group %d\n", x);
				break;
			}
			group |= (1 << x);
		}
	}
	return group;
}

static int __unload_module(void)
{
	struct ast_modem_pvt *p, *pl;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	if (!ast_mutex_lock(&iflock)) {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if (monitor_thread != AST_PTHREADT_NULL && monitor_thread != AST_PTHREADT_STOP) {
			pthread_cancel(monitor_thread);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = AST_PTHREADT_STOP;
		ast_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

	if (!ast_mutex_lock(&iflock)) {
		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			/* Close the socket, assuming it's real */
			if (p->fd > -1)
				close(p->fd);
			pl = p;
			p = p->next;
			/* Free associated memory */
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
		
	return 0;
}

int unload_module()
{
	return __unload_module();
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_modem_pvt *tmp;
	char driver[80];
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}
	if (ast_mutex_lock(&iflock)) {
		/* It's a little silly to lock it, but we mind as well just to be sure */
		ast_log(LOG_ERROR, "Unable to lock interface list???\n");
		return -1;
	}
	v = ast_variable_browse(cfg, "interfaces");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "device")) {
				tmp = mkif(v->value);
				if (tmp) {
					tmp->next = iflist;
					iflist = tmp;
					
				} else {
					ast_log(LOG_ERROR, "Unable to register channel '%s'\n", v->value);
					ast_destroy(cfg);
					ast_mutex_unlock(&iflock);
					__unload_module();
					return -1;
				}
		} else if (!strcasecmp(v->name, "driver")) {
			snprintf(driver, sizeof(driver), "chan_modem_%s.so", v->value);
			if (option_verbose > 1) 
				ast_verbose(VERBOSE_PREFIX_2 "Loading modem driver %s", driver);
				
			if (ast_load_resource(driver)) {
				ast_log(LOG_ERROR, "Failed to load driver %s\n", driver);
				ast_destroy(cfg);
				ast_mutex_unlock(&iflock);
				__unload_module();
				return -1;
			}
		} else if (!strcasecmp(v->name, "mode")) {
			if (!strncasecmp(v->value, "ri", 2)) 
				gmode = MODEM_MODE_WAIT_RING;
			else if (!strncasecmp(v->value, "im", 2))
				gmode = MODEM_MODE_IMMEDIATE;
			else if (!strncasecmp(v->value, "an", 2))
				gmode = MODEM_MODE_WAIT_ANSWER;
			else
				ast_log(LOG_WARNING, "Unknown mode: %s\n", v->value);
		} else if (!strcasecmp(v->name, "stripmsd")) {
			stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "type")) {
			strncpy(mtype, v->value, sizeof(mtype)-1);
		} else if (!strcasecmp(v->name, "initstr")) {
			strncpy(initstr, v->value, sizeof(initstr)-1);
		} else if (!strcasecmp(v->name, "dialtype")) {
			dialtype = toupper(v->value[0]);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(context, v->value, sizeof(context)-1);
		} else if (!strcasecmp(v->name, "msn")) {
			strncpy(msn, v->value, sizeof(msn)-1);
		} else if (!strcasecmp(v->name, "incomingmsn")) {
			strncpy(incomingmsn, v->value, sizeof(incomingmsn)-1);
		} else if (!strcasecmp(v->name, "dtmfmode")) {
			char tmp[80];
			char *alt;
			strncpy(tmp, v->value, sizeof(tmp) - 1);
			alt = strchr(tmp, '/');
			if (!strcasecmp(tmp, "none"))
				dtmfmode=MODEM_DTMF_NONE;
			else if (!strcasecmp(tmp, "asterisk"))
				dtmfmode = MODEM_DTMF_AST;
			else if (!strcasecmp(tmp, "i4l"))
				dtmfmode = MODEM_DTMF_I4L;
			else {
				ast_log(LOG_WARNING, "Unknown dtmf detection mode '%s', using 'asterisk'\n", v->value);
				dtmfmode = MODEM_DTMF_AST;
			}
			if (alt) {
				if (!strcasecmp(alt, "none"))
					dtmfmodegen=MODEM_DTMF_NONE;
				else if (!strcasecmp(alt, "asterisk"))
					dtmfmodegen = MODEM_DTMF_AST;
				else if (!strcasecmp(alt, "i4l"))
					dtmfmodegen = MODEM_DTMF_I4L;
				else if (!strcasecmp(alt, "both"))
					dtmfmodegen = MODEM_DTMF_I4L | MODEM_DTMF_AST;
				else {
					ast_log(LOG_WARNING, "Unknown dtmf generation mode '%s', using 'asterisk'\n", v->value);
					dtmfmodegen = MODEM_DTMF_AST;
				}
			} else
				dtmfmodegen = dtmfmode;
		} else if (!strcasecmp(v->name, "outgoingmsn")) {
			strncpy(outgoingmsn, v->value, sizeof(outgoingmsn)-1);
		} else if (!strcasecmp(v->name, "language")) {
			strncpy(language, v->value, sizeof(language)-1);
		} else if (!strcasecmp(v->name, "group")) {
			cur_group = get_group(v->value);
		}
		v = v->next;
	}
	ast_mutex_unlock(&iflock);
	if (ast_channel_register(type, tdesc, /* XXX Don't know our types -- maybe we should register more than one XXX */ 
						AST_FORMAT_SLINEAR, modem_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_destroy(cfg);
		__unload_module();
		return -1;
	}
	ast_destroy(cfg);
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
}

int usecount(void)
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

