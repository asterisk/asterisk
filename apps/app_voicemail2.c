/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Voicemail System (did you ever think it could be so easy?)
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/say.h>
#include <asterisk/module.h>
#include <asterisk/adsi.h>
#include <asterisk/app.h>
#include <asterisk/manager.h>
#include <asterisk/dsp.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#include <pthread.h>
#include "../asterisk.h"
#include "../astconf.h"

#define COMMAND_TIMEOUT 5000

#define VOICEMAIL_CONFIG "voicemail.conf"
#define ASTERISK_USERNAME "asterisk"

#define SENDMAIL "/usr/sbin/sendmail -t"

#define INTRO "vm-intro"

#define MAXMSG 100

#define MAX_OTHER_FORMATS 10

#define VM_SPOOL_DIR AST_SPOOL_DIR "/vm"

#define BASEMAXINLINE 256

#define BASELINELEN 72

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BASEMAXINLINE 256
#define BASELINELEN 72
#define eol "\r\n"

struct baseio {
	int iocp;
	int iolen;
	int linelength;
	int ateof;
	unsigned char iobuf[BASEMAXINLINE];
};

struct ast_vm_user {
	char context[80];
	char mailbox[80];
	char password[80];
	char fullname[80];
	char email[80];
	char pager[80];
	int alloced;
	struct ast_vm_user *next;
};

static char *tdesc = "Comedian Mail (Voicemail System)";

static char *adapp = "CoMa";

static char *adsec = "_AST";

static char *addesc = "Comedian Mail";

static int adver = 1;

static char *synopsis_vm =
"Leave a voicemail message";

static char *descrip_vm =
"  VoiceMail([s|u|b]extension[@context]): Leaves voicemail for a given  extension (must\n"
"be configured in voicemail.conf). If the extension is preceeded by an 's'"
"then instructions for leaving the message will be skipped.  If the extension\n"
"is preceeded by 'u' then the \"unavailable\" message will be played (that is, \n"
"/var/lib/asterisk/sounds/vm/<exten>/unavail) if it exists.  If the extension\n"
"is preceeded by a 'b' then the the busy message will be played (that is,\n"
"busy instead of unavail).  At most one of 's', 'u', or 'b' may be specified.\n"
"Returns  -1 on  error or mailbox not found, or if the user hangs up. \n"
"Otherwise, it returns 0. \n";

static char *synopsis_vmain =
"Enter voicemail system";

static char *descrip_vmain =
"  VoiceMailMain([[s]mailbox][@context]): Enters the main voicemail system for the checking of\n"
"voicemail.  The mailbox can be passed as the option, which will stop the\n"
"voicemail system from prompting the user for the mailbox.  If the mailbox\n"
"is preceded by 's' then the password check will be skipped.  If a context is\n"
"specified, logins are considered in that context only. Returns -1 if\n"
"the user hangs up or 0 otherwise.\n";

/* Leave a message */
static char *app = "VoiceMail2";

/* Check mail, control, etc */
static char *app2 = "VoiceMailMain2";

static pthread_mutex_t vmlock = AST_MUTEX_INITIALIZER;
struct ast_vm_user *users;
struct ast_vm_user *usersl;
static int attach_voicemail;
static int maxsilence;
static int silencethreshold;
static char serveremail[80];
static char vmfmts[80];
static int vmmaxmessage;
static int maxgreet;
static int skipms;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int make_dir(char *dest, int len, char *context, char *ext, char *mailbox)
{
	return snprintf(dest, len, "%s/voicemail/%s/%s/%s", (char *)ast_config_AST_SPOOL_DIR,context, ext, mailbox);
}

static int make_file(char *dest, int len, char *dir, int num)
{
	return snprintf(dest, len, "%s/msg%04d", dir, num);
}

static struct ast_vm_user *find_user(struct ast_vm_user *ivm, char *context, char *mailbox)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *vmu=NULL, *cur;
	ast_pthread_mutex_lock(&vmlock);
	cur = users;
	while(cur) {
		if ((!context || !strcasecmp(context, cur->context)) &&
			(!strcasecmp(mailbox, cur->mailbox)))
				break;
		cur=cur->next;
	}
	if (cur) {
		if (ivm)
			vmu = ivm;
		else
			/* Make a copy, so that on a reload, we have no race */
			vmu = malloc(sizeof(struct ast_vm_user));
		if (vmu) {
			memcpy(vmu, cur, sizeof(struct ast_vm_user));
			if (ivm)
				vmu->alloced = 0;
			else
				vmu->alloced = 1;
			vmu->next = NULL;
		}
	}
	ast_pthread_mutex_unlock(&vmlock);
	return vmu;
}

static int vm_change_password(struct ast_vm_user *vmu, char *newpassword)
{
        /*  There's probably a better way of doing this. */
        /*  That's why I've put the password change in a separate function. */
		/*  This could also be done with a database function */
	
        FILE *configin;
        FILE *configout;
		char inbuf[256];
		char orig[256];
		char tmpin[AST_CONFIG_MAX_PATH];
		char tmpout[AST_CONFIG_MAX_PATH];
		char *user, *pass, *rest, *trim;
	snprintf((char *)tmpin, sizeof(tmpin)-1, "%s/voicemail.conf",(char *)ast_config_AST_CONFIG_DIR);
	snprintf((char *)tmpout, sizeof(tmpout)-1, "%s/voicemail.conf.new",(char *)ast_config_AST_CONFIG_DIR);
        configin = fopen((char *)tmpin,"r");
        configout = fopen((char *)tmpout,"w+");

        while (!feof(configin)) {
			/* Read in the line */
			fgets(inbuf, sizeof(inbuf), configin);
			if (!feof(configin)) {
				/* Make a backup of it */
				memcpy(orig, inbuf, sizeof(orig));
				/* Strip trailing \n and comment */
				inbuf[strlen(inbuf) - 1] = '\0';
				user = strchr(inbuf, ';');
				if (user)
					*user = '\0';
				user=inbuf;
				while(*user < 33)
					user++;
				pass = strchr(user, '=');
				if (pass > user) {
					trim = pass - 1;
					while(*trim && *trim < 33) {
						*trim = '\0';
						trim--;
					}
				}
				if (pass) {
					*pass = '\0';
					pass++;
					if (*pass == '>')
						pass++;
					while(*pass && *pass < 33)
						pass++;
				}
				if (pass) {
					rest = strchr(pass,',');
					if (rest) {
						*rest = '\0';
						rest++;
					}
				} else
					rest = NULL;
				if (user && pass && *user && *pass && !strcmp(user, vmu->mailbox) && !strcmp(pass, vmu->password)) {
					/* This is the line */
					if (rest) {
						fprintf(configout, "%s => %s,%s\n", vmu->mailbox,newpassword,rest);
					} else {
						fprintf(configout, "%s => %s\n", vmu->mailbox,newpassword);
					}
				} else {
					/* Put it back like it was */
					fprintf(configout, orig);
				}
			}
        }
        fclose(configin);
        fclose(configout);

        unlink((char *)tmpin);
        rename((char *)tmpout,(char *)tmpin);
	return(1);
}

static int
inbuf(struct baseio *bio, FILE *fi)
{
	int l;

	if(bio->ateof)
		return 0;

	if ( (l = fread(bio->iobuf,1,BASEMAXINLINE,fi)) <= 0) {
		if(ferror(fi))
			return -1;

		bio->ateof = 1;
		return 0;
	}

	bio->iolen= l;
	bio->iocp= 0;

	return 1;
}

static int 
inchar(struct baseio *bio, FILE *fi)
{
	if(bio->iocp>=bio->iolen)
		if(!inbuf(bio, fi))
			return EOF;

	return bio->iobuf[bio->iocp++];
}

static int
ochar(struct baseio *bio, int c, FILE *so)
{
	if(bio->linelength>=BASELINELEN) {
		if(fputs(eol,so)==EOF)
			return -1;

		bio->linelength= 0;
	}

	if(putc(((unsigned char)c),so)==EOF)
		return -1;

	bio->linelength++;

	return 1;
}

static int base_encode(char *filename, FILE *so)
{
	unsigned char dtable[BASEMAXINLINE];
	int i,hiteof= 0;
	FILE *fi;
	struct baseio bio;

	memset(&bio, 0, sizeof(bio));
	bio.iocp = BASEMAXINLINE;

	if ( !(fi = fopen(filename, "rb"))) {
		ast_log(LOG_WARNING, "Failed to open log file: %s: %s\n", filename, strerror(errno));
		return -1;
	}

	for(i= 0;i<9;i++){
		dtable[i]= 'A'+i;
		dtable[i+9]= 'J'+i;
		dtable[26+i]= 'a'+i;
		dtable[26+i+9]= 'j'+i;
	}
	for(i= 0;i<8;i++){
		dtable[i+18]= 'S'+i;
		dtable[26+i+18]= 's'+i;
	}
	for(i= 0;i<10;i++){
		dtable[52+i]= '0'+i;
	}
	dtable[62]= '+';
	dtable[63]= '/';

	while(!hiteof){
		unsigned char igroup[3],ogroup[4];
		int c,n;

		igroup[0]= igroup[1]= igroup[2]= 0;

		for(n= 0;n<3;n++){
			if ( (c = inchar(&bio, fi)) == EOF) {
				hiteof= 1;
				break;
			}

			igroup[n]= (unsigned char)c;
		}

		if(n> 0){
			ogroup[0]= dtable[igroup[0]>>2];
			ogroup[1]= dtable[((igroup[0]&3)<<4)|(igroup[1]>>4)];
			ogroup[2]= dtable[((igroup[1]&0xF)<<2)|(igroup[2]>>6)];
			ogroup[3]= dtable[igroup[2]&0x3F];

			if(n<3) {
				ogroup[3]= '=';

				if(n<2)
					ogroup[2]= '=';
			}

			for(i= 0;i<4;i++)
				ochar(&bio, ogroup[i], so);
		}
	}

	if(fputs(eol,so)==EOF)
		return 0;

	fclose(fi);

	return 1;
}

static int sendmail(char *srcemail, char *email, char *name, int msgnum, char *mailbox, char *callerid, char *attach, char *format, long duration)
{
	FILE *p;
	char date[256];
	char host[256];
	char who[256];
	char bound[256];
	char fname[256];
	char dur[256];
	time_t t;
	struct tm tm;
	p = popen(SENDMAIL, "w");
	if (p) {
		gethostname(host, sizeof(host));
		if (strchr(srcemail, '@'))
			strncpy(who, srcemail, sizeof(who)-1);
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%ld:%02ld", duration / 60, duration % 60);
		time(&t);
		localtime_r(&t,&tm);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
		fprintf(p, "Date: %s\n", date);
		fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s <%s>\n", name, email);
		fprintf(p, "Subject: [PBX]: New message %d in mailbox %s\n", msgnum, mailbox);
		fprintf(p, "Message-ID: <Asterisk-%d-%s-%d@%s>\n", msgnum, mailbox, getpid(), host);
		fprintf(p, "MIME-Version: 1.0\n");
		if (attach_voicemail) {
			// Something unique.
			snprintf(bound, sizeof(bound), "Boundary=%d%s%d", msgnum, mailbox, getpid());

			fprintf(p, "Content-Type: MULTIPART/MIXED; BOUNDARY=\"%s\"\n\n\n", bound);

			fprintf(p, "--%s\n", bound);
		}
			fprintf(p, "Content-Type: TEXT/PLAIN; charset=US-ASCII\n\n");
			strftime(date, sizeof(date), "%A, %B %d, %Y at %r", &tm);
			fprintf(p, "Dear %s:\n\n\tJust wanted to let you know you were just left a %s long message (number %d)\n"

		           "in mailbox %s from %s, on %s so you might\n"
				   "want to check it when you get a chance.  Thanks!\n\n\t\t\t\t--Asterisk\n\n", name, 
				dur, msgnum, mailbox, (callerid ? callerid : "an unknown caller"), date);
		if (attach_voicemail) {
			fprintf(p, "--%s\n", bound);
			fprintf(p, "Content-Type: audio/x-wav; name=\"msg%04d.%s\"\n", msgnum, format);
			fprintf(p, "Content-Transfer-Encoding: BASE64\n");
			fprintf(p, "Content-Description: Voicemail sound attachment.\n");
			fprintf(p, "Content-Disposition: attachment; filename=\"msg%04d.%s\"\n\n", msgnum, format);

			snprintf(fname, sizeof(fname), "%s.%s", attach, format);
			base_encode(fname, p);
			fprintf(p, "\n\n--%s--\n.\n", bound);
		}
		pclose(p);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", SENDMAIL);
		return -1;
	}
	return 0;
}

static int sendpage(char *srcemail, char *pager, int msgnum, char *mailbox, char *callerid, long duration)
{
	FILE *p;
	char date[256];
	char host[256];
	char who[256];
	char dur[256];
	time_t t;
	struct tm tm;
	p = popen(SENDMAIL, "w");

	if (p) {
		gethostname(host, sizeof(host));
		if (strchr(srcemail, '@'))
			strncpy(who, srcemail, sizeof(who)-1);
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%ld:%02ld", duration / 60, duration % 60);
		time(&t);
		localtime_r(&t,&tm);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
		fprintf(p, "Date: %s\n", date);
		fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s\n", pager);
		fprintf(p, "Subject: New VM\n\n");
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", &tm);
		fprintf(p, "New %s long msg in box %s\n"
		           "from %s, on %s", dur, mailbox, (callerid ? callerid : "unknown"), date);
		pclose(p);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", SENDMAIL);
		return -1;
	}
	return 0;
}

static int get_date(char *s, int len)
{
	struct tm tm;
	time_t t;
	t = time(0);
	localtime_r(&t,&tm);
	return strftime(s, len, "%a %b %e %r %Z %Y", &tm);
}

static int invent_message(struct ast_channel *chan, char *context, char *ext, int busy, char *ecodes)
{
	int res;
	char fn[256];
	snprintf(fn, sizeof(fn), "voicemail/%s/%s/greet", context, ext);
	if (ast_fileexists(fn, NULL, NULL) > 0) {
		res = ast_streamfile(chan, fn, chan->language);
		if (res)
			return -1;
		res = ast_waitstream(chan, ecodes);
		if (res)
			return res;
	} else {
		res = ast_streamfile(chan, "vm-theperson", chan->language);
		if (res)
			return -1;
		res = ast_waitstream(chan, ecodes);
		if (res)
			return res;
		res = ast_say_digit_str(chan, ext, ecodes, chan->language);
		if (res)
			return res;
	}
	if (busy)
		res = ast_streamfile(chan, "vm-isonphone", chan->language);
	else
		res = ast_streamfile(chan, "vm-isunavail", chan->language);
	if (res)
		return -1;
	res = ast_waitstream(chan, ecodes);
	return res;
}

static int play_and_wait(struct ast_channel *chan, char *fn)
{
	int d;
	d = ast_streamfile(chan, fn, chan->language);
	if (d)
		return d;
	d = ast_waitstream(chan, AST_DIGIT_ANY);
	return d;
}

static int play_and_record(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt)
{
	char d, *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct ast_frame *f;
	struct ast_filestream *others[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	
	
	ast_log(LOG_DEBUG,"play_and_record: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile) {	
		d = play_and_wait(chan, playfile);
		if (!d)
			d = ast_streamfile(chan, "beep",chan->language);
		if (!d)
			d = ast_waitstream(chan,"");
		if (d < 0)
			return -1;
	}
	
	fmts = strdupa(fmt);
	
	stringp=fmts;
	strsep(&stringp, "|");
	ast_log(LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);	
	sfmt[0] = strdupa(fmts);
	
	while((fmt = strsep(&stringp, "|"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = strdupa(fmt);
	}

	if (maxtime)
		time(&start);
	for (x=0;x<fmtcnt;x++) {
		others[x] = ast_writefile(recordfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		ast_verbose( VERBOSE_PREFIX_3 "x=%i, open writing:  %s format: %s\n", x, recordfile, sfmt[x]);
			
		if (!others[x]) {
			break;
		}
	}
	if (x == fmtcnt) {
	/* Loop forever, writing the packets we read to the writer(s), until
	   we read a # or get a hangup */
		f = NULL;
		for(;;) {
		 	res = ast_waitfor(chan, 2000);
			if (!res) {
				ast_log(LOG_DEBUG, "One waitfor failed, trying another\n");
				/* Try one more time in case of masq */
			 	res = ast_waitfor(chan, 2000);
				if (!res) {
					ast_log(LOG_WARNING, "No audio available on %s??\n", chan->name);
					res = -1;
				}
			}
			
			if (res < 0) {
				f = NULL;
				break;
			}
			f = ast_read(chan);
			if (!f)
				break;
			if (f->frametype == AST_FRAME_VOICE) {
				/* write each format */
				for (x=0;x<fmtcnt;x++) {
					res = ast_writestream(others[x], f);
				}
				/* Exit on any error */
				if (res) {
					ast_log(LOG_WARNING, "Error writing frame\n");
					ast_frfree(f);
					break;
				}
			} else if (f->frametype == AST_FRAME_DTMF) {
				if (f->subclass == '#') {
					if (option_verbose > 2) 
						ast_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
					res = '#';
					outmsg = 2;
					ast_frfree(f);
					break;
				}
			}
			if (maxtime) {
				time(&end);
				if (maxtime < (end - start)) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Took too long, cutting it short...\n");
					res = 't';
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
		}
		if (!f) {
			if (option_verbose > 2) 
				ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
		}
	} else {
		ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", recordfile, sfmt[x]); 
	}

	for (x=0;x<fmtcnt;x++) {
		if (!others[x])
			break;
		ast_closestream(others[x]);
	}
	if (outmsg) {
		if (outmsg > 1) {
		/* Let them know it worked */
			ast_streamfile(chan, "vm-msgsaved", chan->language);
			ast_waitstream(chan, "");
		}
	}	

	
	return res;
}

static void free_user(struct ast_vm_user *vmu)
{
	if (vmu->alloced)
		free(vmu);
}

static int leave_voicemail(struct ast_channel *chan, char *ext, int silent, int busy, int unavail)
{
	char comment[256];
	char txtfile[256];
	FILE *txt;
	int res = 0;
	int msgnum;
	int maxmessage=0;
	char date[256];
	char dir[256];
	char fn[256];
	char prefile[256]="";
	char fmt[80];
	char *context;
	char *ecodes = "#";
	char *stringp;
	time_t start;
	time_t end;
#if 0
	/* XXX Need to be moved to play_and_record */
	struct ast_dsp *sildet;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
#endif	

	char tmp[256] = "";
	struct ast_vm_user *vmu;
	struct ast_vm_user svm;
	
	strncpy(tmp, ext, sizeof(tmp) - 1);
	ext = tmp;
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}

	if ((vmu = find_user(&svm, context, ext))) {
		/* Setup pre-file if appropriate */
		if (busy)
			snprintf(prefile, sizeof(prefile), "voicemail/%s/%s/busy", vmu->context, ext);
		else if (unavail)
			snprintf(prefile, sizeof(prefile), "voicemail/%s/%s/unavail", vmu->context, ext);
		make_dir(dir, sizeof(dir), vmu->context, "", "");
		/* It's easier just to try to make it than to check for its existence */
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		make_dir(dir, sizeof(dir), vmu->context, ext, "");
		/* It's easier just to try to make it than to check for its existence */
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		make_dir(dir, sizeof(dir), vmu->context, ext, "INBOX");
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		if (ast_exists_extension(chan, strlen(chan->macrocontext) ? chan->macrocontext : chan->context, "o", 1, chan->callerid))
			ecodes = "#0";
		/* Play the beginning intro if desired */
		if (strlen(prefile)) {
			if (ast_fileexists(prefile, NULL, NULL) > 0) {
				if (ast_streamfile(chan, prefile, chan->language) > -1) 
				    res = ast_waitstream(chan, "#0");
			} else {
				ast_log(LOG_DEBUG, "%s doesn't exist, doing what we can\n", prefile);
				res = invent_message(chan, vmu->context, ext, busy, ecodes);
			}
			if (res < 0) {
				ast_log(LOG_DEBUG, "Hang up during prefile playback\n");
				free_user(vmu);
				return -1;
			}
		}
		if (res == '#') {
			/* On a '#' we skip the instructions */
			silent = 1;
			res = 0;
		}
		if (!res && !silent) {
			res = ast_streamfile(chan, INTRO, chan->language);
			if (!res)
				res = ast_waitstream(chan, ecodes);
			if (res == '#') {
				silent = 1;
				res = 0;
			}
		}
		/* Check for a '0' here */
		if (res == '0') {
			strncpy(chan->exten, "o", sizeof(chan->exten) - 1);
			if (strlen(chan->macrocontext))
				strncpy(chan->context, chan->macrocontext, sizeof(chan->context) - 1);
			chan->priority = 0;
			free_user(vmu);
			return 0;
		}
		if (!res && (silent < 2)) {
			/* Unless we're *really* silent, try to send the beep */
			res = ast_streamfile(chan, "beep", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
		}
		if (res < 0) {
			free_user(vmu);
			return -1;
		}
		/* The meat of recording the message...  All the announcements and beeps have been played*/
		strncpy(fmt, vmfmts, sizeof(fmt) - 1);
		if (strlen(fmt)) {
			msgnum = 0;
			do {
				make_file(fn, sizeof(fn), dir, msgnum);
				snprintf(comment, sizeof(comment), "Voicemail from %s to %s (%s) on %s\n",
									(chan->callerid ? chan->callerid : "Unknown"), 
									vmu->fullname, ext, chan->name);
				if (ast_fileexists(fn, NULL, chan->language) <= 0) 
					break;
				msgnum++;
			} while(msgnum < MAXMSG);
			if (msgnum < MAXMSG) {
				/* Store information */
				snprintf(txtfile, sizeof(txtfile), "%s.txt", fn);
 				txt = fopen(txtfile, "w+");
				if (txt) {
					get_date(date, sizeof(date));
					time(&start);
					fprintf(txt, 
";\n"
"; Message Information file\n"
";\n"
"[message]\n"
"origmailbox=%s\n"
"context=%s\n"
"exten=%s\n"
"priority=%d\n"
"callerchan=%s\n"
"callerid=%s\n"
"origdate=%s\n"
"origtime=%ld\n",
	ext,
	chan->context,
	chan->exten,
	chan->priority,
	chan->name,
	chan->callerid ? chan->callerid : "Unknown",
	date, time(NULL));
					fclose(txt);
				} else
					ast_log(LOG_WARNING, "Error opening text file for output\n");
				res = play_and_record(chan, NULL, fn, maxmessage, fmt);
				txt = fopen(txtfile, "a");
				if (txt) {
					time(&end);
					fprintf(txt, "duration=%ld\n", end-start);
					fclose(txt);
				}
				stringp = fmt;
				strsep(&stringp, "|");
				/* Send e-mail if applicable */
				if (strlen(vmu->email))
					sendmail(serveremail, vmu->email, vmu->fullname, msgnum, ext, chan->callerid, fn, fmt, end - start);
				if (strlen(vmu->pager))
					sendpage(serveremail, vmu->pager, msgnum, ext, chan->callerid, end - start);
			} else
				ast_log(LOG_WARNING, "No more messages possible\n");
		} else
			ast_log(LOG_WARNING, "No format for saving voicemail?\n");
	
#if 0
						sildet = ast_dsp_new(); //Create the silence detector
						if (silence > 0) {
							rfmt = chan->readformat;
							res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
							if (res < 0) {
								ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
								return -1;
							}
							if (!sildet) {
								ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
								return -1;
							}
							ast_dsp_set_threshold(sildet, 50);
						}
#endif						
		free_user(vmu);
	} else
		ast_log(LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
	/* Leave voicemail for someone */
	manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext, ast_app_has_voicemail(ext));
	return res;
}

static char *mbox(int id)
{
	switch(id) {
	case 0:
		return "INBOX";
	case 1:
		return "Old";
	case 2:
		return "Work";
	case 3:
		return "Family";
	case 4:
		return "Friends";
	case 5:
		return "Cust1";
	case 6:
		return "Cust2";
	case 7:
		return "Cust3";
	case 8:
		return "Cust4";
	case 9:
		return "Cust5";
	default:
		return "Unknown";
	}
}

static int count_messages(char *dir)
{
	int x;
	char fn[256];
	for (x=0;x<MAXMSG;x++) {
		make_file(fn, sizeof(fn), dir, x);
		if (ast_fileexists(fn, NULL, NULL) < 1)
			break;
	}
	return x;
}

static int say_and_wait(struct ast_channel *chan, int num)
{
	int d;
	d = ast_say_number(chan, num, AST_DIGIT_ANY, chan->language);
	return d;
}

static int copy(char *infile, char *outfile)
{
	int ifd;
	int ofd;
	int res;
	int len;
	char buf[4096];
	if ((ifd = open(infile, O_RDONLY)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in read-only mode\n", infile);
		return -1;
	}
	if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in write-only mode\n", outfile);
		close(ifd);
		return -1;
	}
	do {
		len = read(ifd, buf, sizeof(buf));
		if (len < 0) {
			ast_log(LOG_WARNING, "Read failed on %s: %s\n", infile, strerror(errno));
			close(ifd);
			close(ofd);
			unlink(outfile);
		}
		if (len) {
			res = write(ofd, buf, len);
			if (res != len) {
				ast_log(LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, res, len, strerror(errno));
				close(ifd);
				close(ofd);
				unlink(outfile);
			}
		}
	} while(len);
	close(ifd);
	close(ofd);
	return 0;
}

static int save_to_folder(char *dir, int msg, char *context, char *username, int box)
{
	char sfn[256];
	char dfn[256];
	char ddir[256];
	char txt[256];
	char ntxt[256];
	char *dbox = mbox(box);
	int x;
	make_file(sfn, sizeof(sfn), dir, msg);
	make_dir(ddir, sizeof(ddir), context, username, dbox);
	mkdir(ddir, 0700);
	for (x=0;x<MAXMSG;x++) {
		make_file(dfn, sizeof(dfn), ddir, x);
		if (ast_fileexists(dfn, NULL, NULL) < 0)
			break;
	}
	if (x >= MAXMSG)
		return -1;
	ast_filecopy(sfn, dfn, NULL);
	if (strcmp(sfn, dfn)) {
		snprintf(txt, sizeof(txt), "%s.txt", sfn);
		snprintf(ntxt, sizeof(ntxt), "%s.txt", dfn);
		copy(txt, ntxt);
	}
	return 0;
}

static int adsi_logo(unsigned char *buf)
{
	int bytes = 0;
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_CENT, 0, "Comedian Mail", "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_CENT, 0, "(C)2002 LSS, Inc.", "");
	return bytes;
}

static int adsi_load_vmail(struct ast_channel *chan, int *useadsi)
{
	char buf[256];
	int bytes=0;
	int x;
	char num[5];

	*useadsi = 0;
	bytes += adsi_data_mode(buf + bytes);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	bytes = 0;
	bytes += adsi_logo(buf);
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Downloading Scripts", "");
#ifdef DISPLAY
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   .", "");
#endif
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_data_mode(buf + bytes);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	if (adsi_begin_download(chan, addesc, adapp, adsec, adver)) {
		bytes = 0;
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Load Cancelled.", "");
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "ADSI Unavailable", "");
		bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += adsi_voice_mode(buf + bytes, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
		return 0;
	}

#ifdef DISPLAY
	/* Add a dot */
	bytes = 0;
	bytes += adsi_logo(buf);
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Downloading Scripts", "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ..", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif
	bytes = 0;
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 0, "Listen", "Listen", "1", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 1, "Folder", "Folder", "2", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 2, "Advanced", "Advnced", "3", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 3, "Options", "Options", "4", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 4, "Help", "Help", "*", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 5, "Exit", "Exit", "#", 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ...", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif

	bytes = 0;
	/* These buttons we load but don't use yet */
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 6, "Previous", "Prev", "4", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 8, "Repeat", "Repeat", "5", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 7, "Delete", "Delete", "7", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 9, "Next", "Next", "6", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 10, "Save", "Save", "9", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 11, "Undelete", "Restore", "7", 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ....", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif

	bytes = 0;
	for (x=0;x<5;x++) {
		snprintf(num, sizeof(num), "%d", x);
		bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 12 + x, mbox(x), mbox(x), num, 1);
	}
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 12 + 5, "Cancel", "Cancel", "#", 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   .....", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif

	if (adsi_end_download(chan)) {
		bytes = 0;
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Download Unsuccessful.", "");
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "ADSI Unavailable", "");
		bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += adsi_voice_mode(buf + bytes, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
		return 0;
	}
	bytes = 0;
	bytes += adsi_download_disconnect(buf + bytes);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

	ast_log(LOG_DEBUG, "Done downloading scripts...\n");

#ifdef DISPLAY
	/* Add last dot */
	bytes = 0;
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "   ......", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
#endif
	ast_log(LOG_DEBUG, "Restarting session...\n");

	bytes = 0;
	/* Load the session now */
	if (adsi_load_session(chan, adapp, adver, 1) == 1) {
		*useadsi = 1;
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Scripts Loaded!", "");
	} else
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Load Failed!", "");

 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	return 0;
}

static void adsi_begin(struct ast_channel *chan, int *useadsi)
{
	int x;
	if (!adsi_available(chan))
          return;
	x = adsi_load_session(chan, adapp, adver, 1);
	if (x < 0)
		return;
	if (!x) {
		if (adsi_load_vmail(chan, useadsi)) {
			ast_log(LOG_WARNING, "Unable to upload voicemail scripts\n");
			return;
		}
	} else
		*useadsi = 1;
}

static void adsi_login(struct ast_channel *chan)
{
	char buf[256];
	int bytes=0;
	unsigned char keys[8];
	int x;
	if (!adsi_available(chan))
		return;

	for (x=0;x<8;x++)
		keys[x] = 0;
	/* Set one key for next */
	keys[3] = ADSI_KEY_APPS + 3;

	bytes += adsi_logo(buf + bytes);
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, " ", "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, " ", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_input_format(buf + bytes, 1, ADSI_DIR_FROM_LEFT, 0, "Mailbox: ******", "");
	bytes += adsi_input_control(buf + bytes, ADSI_COMM_PAGE, 4, 1, 1, ADSI_JUST_LEFT);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 3, "Enter", "Enter", "#", 1);
	bytes += adsi_set_keys(buf + bytes, keys);
	bytes += adsi_voice_mode(buf + bytes, 0);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_password(struct ast_channel *chan)
{
	char buf[256];
	int bytes=0;
	unsigned char keys[8];
	int x;
	if (!adsi_available(chan))
		return;

	for (x=0;x<8;x++)
		keys[x] = 0;
	/* Set one key for next */
	keys[3] = ADSI_KEY_APPS + 3;

	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_input_format(buf + bytes, 1, ADSI_DIR_FROM_LEFT, 0, "Password: ******", "");
	bytes += adsi_input_control(buf + bytes, ADSI_COMM_PAGE, 4, 0, 1, ADSI_JUST_LEFT);
	bytes += adsi_set_keys(buf + bytes, keys);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_folders(struct ast_channel *chan, int start, char *label)
{
	char buf[256];
	int bytes=0;
	unsigned char keys[8];
	int x,y;

	if (!adsi_available(chan))
		return;

	for (x=0;x<5;x++) {
		y = ADSI_KEY_APPS + 12 + start + x;
		if (y > ADSI_KEY_APPS + 12 + 4)
			y = 0;
		keys[x] = ADSI_KEY_SKT | y;
	}
	keys[5] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 17);
	keys[6] = 0;
	keys[7] = 0;

	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_CENT, 0, label, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_CENT, 0, " ", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_set_keys(buf + bytes, keys);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_message(struct ast_channel *chan, char *folder, int msg, int last, int deleted, char *fn)
{
	int bytes=0;
	char buf[256], buf1[256], buf2[256];
	char fn2[256];
	char cid[256]="";
	char *val;
	char *name, *num;
	char datetime[21]="";
	FILE *f;

	unsigned char keys[8];

	int x;

	if (!adsi_available(chan))
		return;

	/* Retrieve important info */
	snprintf(fn2, sizeof(fn2), "%s.txt", fn);
	f = fopen(fn2, "r");
	if (f) {
		while(!feof(f)) {	
			fgets(buf, sizeof(buf), f);
			if (!feof(f)) {
				char *stringp=NULL;
				stringp=buf;
				strsep(&stringp, "=");
				val = strsep(&stringp, "=");
				if (val && strlen(val)) {
					if (!strcmp(buf, "callerid"))
						strncpy(cid, val, sizeof(cid) - 1);
					if (!strcmp(buf, "origdate"))
						strncpy(datetime, val, sizeof(datetime) - 1);
				}
			}
		}
		fclose(f);
	}
	/* New meaning for keys */
	for (x=0;x<5;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 6 + x);
	keys[6] = 0x0;
	keys[7] = 0x0;

	if (!msg) {
		/* No prev key, provide "Folder" instead */
		keys[0] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
	}
	if (msg >= last) {
		/* If last message ... */
		if (msg) {
			/* but not only message, provide "Folder" instead */
			keys[3] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
		} else {
			/* Otherwise if only message, leave blank */
			keys[3] = 1;
		}
	}

	if (strlen(cid)) {
		ast_callerid_parse(cid, &name, &num);
		if (!name)
			name = num;
	} else
		name = "Unknown Caller";

	/* If deleted, show "undeleted" */
	if (deleted)
		keys[1] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 11);

	/* Except "Exit" */
	keys[5] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 5);
	snprintf(buf1, sizeof(buf1), "%s%s", folder,
		 strcasecmp(folder, "INBOX") ? " Messages" : "");
 	snprintf(buf2, sizeof(buf2), "Message %d of %d", msg + 1, last + 1);

 	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, name, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, datetime, "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_set_keys(buf + bytes, keys);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_delete(struct ast_channel *chan, int msg, int last, int deleted)
{
	int bytes=0;
	char buf[256];
	unsigned char keys[8];

	int x;

	if (!adsi_available(chan))
		return;

	/* New meaning for keys */
	for (x=0;x<5;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 6 + x);

	keys[6] = 0x0;
	keys[7] = 0x0;

	if (!msg) {
		/* No prev key, provide "Folder" instead */
		keys[0] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
	}
	if (msg >= last) {
		/* If last message ... */
		if (msg) {
			/* but not only message, provide "Folder" instead */
			keys[3] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
		} else {
			/* Otherwise if only message, leave blank */
			keys[3] = 1;
		}
	}

	/* If deleted, show "undeleted" */
	if (deleted) 
		keys[1] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 11);

	/* Except "Exit" */
	keys[5] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 5);
	bytes += adsi_set_keys(buf + bytes, keys);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_status(struct ast_channel *chan, int new, int old, int lastmsg)
{
	char buf[256], buf1[256], buf2[256];
	int bytes=0;
	unsigned char keys[8];
	int x;

	char *newm = (new == 1) ? "message" : "messages";
	char *oldm = (old == 1) ? "message" : "messages";
	if (!adsi_available(chan))
		return;
	if (new) {
		snprintf(buf1, sizeof(buf1), "You have %d new", new);
		if (old) {
			strcat(buf1, " and");
			snprintf(buf2, sizeof(buf2), "%d old %s.", old, oldm);
		} else {
			snprintf(buf2, sizeof(buf2), "%s.", newm);
		}
	} else if (old) {
		snprintf(buf1, sizeof(buf1), "You have %d old", old);
		snprintf(buf2, sizeof(buf2), "%s.", oldm);
	} else {
		strcpy(buf1, "You have no messages.");
		strcpy(buf2, " ");
	}
 	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);

	for (x=0;x<6;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + x);
	keys[6] = 0;
	keys[7] = 0;

	/* Don't let them listen if there are none */
	if (lastmsg < 0)
		keys[0] = 1;
	bytes += adsi_set_keys(buf + bytes, keys);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_status2(struct ast_channel *chan, char *folder, int messages)
{
	char buf[256], buf1[256], buf2[256];
	int bytes=0;
	unsigned char keys[8];
	int x;

	char *mess = (messages == 1) ? "message" : "messages";

	if (!adsi_available(chan))
		return;

	/* Original command keys */
	for (x=0;x<6;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + x);

	keys[6] = 0;
	keys[7] = 0;

	if (messages < 1)
		keys[0] = 0;

	snprintf(buf1, sizeof(buf1), "%s%s has", folder,
			strcasecmp(folder, "INBOX") ? " folder" : "");

	if (messages)
		snprintf(buf2, sizeof(buf2), "%d %s.", messages, mess);
	else
		strcpy(buf2, "no messages.");
 	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, "", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_set_keys(buf + bytes, keys);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	
}

static void adsi_clear(struct ast_channel *chan)
{
	char buf[256];
	int bytes=0;
	if (!adsi_available(chan))
		return;
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_goodbye(struct ast_channel *chan)
{
	char buf[256];
	int bytes=0;

	if (!adsi_available(chan))
		return;
	bytes += adsi_logo(buf + bytes);
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, " ", "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Goodbye", "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static int get_folder(struct ast_channel *chan, int start)
{
	int x;
	int d;
	char fn[256];
	d = play_and_wait(chan, "vm-press");
	if (d)
		return d;
	for (x = start; x< 5; x++) {
		if ((d = ast_say_number(chan, x, AST_DIGIT_ANY, chan->language)))
			return d;
		d = play_and_wait(chan, "vm-for");
		if (d)
			return d;
		snprintf(fn, sizeof(fn), "vm-%s", mbox(x));
		d = play_and_wait(chan, fn);
		if (d)
			return d;
		d = play_and_wait(chan, "vm-messages");
		if (d)
			return d;
		d = ast_waitfordigit(chan, 500);
		if (d)
			return d;
	}
	d = play_and_wait(chan, "vm-tocancel");
	if (d)
		return d;
	d = ast_waitfordigit(chan, 4000);
	return d;
}

static int get_folder2(struct ast_channel *chan, char *fn, int start)
{
	int res = 0;
	res = play_and_wait(chan, fn);
	while (((res < '0') || (res > '9')) &&
			(res != '#') && (res > 0)) {
		res = get_folder(chan, 0);
	}
	return res;
}

static int
forward_message(struct ast_channel *chan, char *context, char *dir, int curmsg, struct ast_vm_user *sender, char *fmt)
{
	char username[70];
	char sys[256];
	char todir[256];
	int todircount=0;
	long duration;
	struct ast_config *mif;
	char miffile[256];
	char fn[256];
	char callerid[512];
	int res = 0;
	struct ast_vm_user *receiver, srec;
	char tmp[256];
	char *stringp, *s;
	
	while(!res) {
		res = ast_streamfile(chan, "vm-extension", chan->language);
		if (res)
			break;
		if ((res = ast_readstring(chan, username, sizeof(username) - 1, 2000, 10000, "#") < 0))
			break;
		if ((receiver = find_user(&srec, context, username))) {
			printf("Got %d\n", atoi(username));
			/* if (play_and_wait(chan, "vm-savedto"))
				break;
			*/

			snprintf(todir, sizeof(todir), "%s/voicemail/%s/%s/INBOX",  (char *)ast_config_AST_SPOOL_DIR, receiver->context, username);
			snprintf(sys, sizeof(sys), "mkdir -p %s\n", todir);
			ast_log(LOG_DEBUG, sys);
			system(sys);

			todircount = count_messages(todir);
			strncpy(tmp, fmt, sizeof(tmp));
			stringp = tmp;
			while((s = strsep(&stringp, "|"))) {
				snprintf(sys, sizeof(sys), "cp %s/msg%04d.%s %s/msg%04d.%s\n", dir, curmsg, s, todir, todircount, s);
				ast_log(LOG_DEBUG, sys);
				system(sys);
			}
			snprintf(fn, sizeof(fn), "%s/msg%04d", todir,todircount);

			/* load the information on the source message so we can send an e-mail like a new message */
			snprintf(miffile, sizeof(miffile), "%s/msg%04d.txt", dir, curmsg);
			if ((mif=ast_load(miffile))) {

              /* set callerid and duration variables */
              snprintf(callerid, sizeof(callerid), "FWD from: %s from %s", sender->fullname, ast_variable_retrieve(mif, NULL, "callerid"));
              duration = atol(ast_variable_retrieve(mif, NULL, "duration"));
              		
			  if (strlen(receiver->email))
			    sendmail(serveremail, receiver->email, receiver->fullname, todircount, username, callerid, fn, tmp, atol(ast_variable_retrieve(mif, NULL, "duration")));
				     
			  if (strlen(receiver->pager))
				sendpage(serveremail, receiver->pager, todircount, username, callerid, duration);
			  
			  ast_destroy(mif); /* or here */
			}

			/* give confirmatopm that the message was saved */
			res = play_and_wait(chan, "vm-message");
			if (!res)
				res = play_and_wait(chan, "vm-saved");
			free_user(receiver);
			break;
		} else {
			res = play_and_wait(chan, "pbx-invalid");
		}
	}
	return res;
}

struct vm_state {
	char curbox[80];
	char username[80];
	char curdir[256];
	char vmbox[256];
	char fn[256];
	char fn2[256];
	int deleted[MAXMSG];
	int heard[MAXMSG];
	int curmsg;
	int lastmsg;
	int newmessages;
	int oldmessages;
	int starting;
	int repeats;
};


static int wait_file2(struct ast_channel *chan, struct vm_state *vms, char *file)
{
	int res;
	if ((res = ast_streamfile(chan, file, chan->language))) 
		ast_log(LOG_WARNING, "Unable to play message %s\n", file); 
	if (!res)
		res = ast_waitstream(chan, AST_DIGIT_ANY);
	return res;
}

static int wait_file(struct ast_channel *chan, struct vm_state *vms, char *file) 
{
	int res;
	if ((res = ast_streamfile(chan, file, chan->language)))
		ast_log(LOG_WARNING, "Unable to play message %s\n", file);
	if (!res)
		res = ast_waitstream_fr(chan, AST_DIGIT_ANY, "#", "*",skipms);
	return res;
}

static int play_message(struct ast_channel *chan, struct vm_state *vms, int msg)
{
	int res = 0;
	vms->starting = 0; 
	make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);
	adsi_message(chan, vms->curbox, msg, vms->lastmsg, vms->deleted[msg], vms->fn);
	if (!msg)
		res = wait_file2(chan, vms, "vm-first");
	else if (msg == vms->lastmsg)
		res = wait_file2(chan, vms, "vm-last");
	if (!res) {
		res = wait_file2(chan, vms, "vm-message");
		if (msg && (msg != vms->lastmsg)) {
			if (!res)
				res = ast_say_number(chan, msg + 1, AST_DIGIT_ANY, chan->language);
		}
	}
	
	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);
		vms->heard[msg] = 1;
		res = wait_file(chan, vms, vms->fn);
	}
	return res;
}

static void open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu,int box)
{
	strncpy(vms->curbox, mbox(box), sizeof(vms->curbox) - 1);
	make_dir(vms->curdir, sizeof(vms->curdir), vmu->context, vms->username, vms->curbox);
	vms->lastmsg = count_messages(vms->curdir) - 1;
	snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", vms->curbox);
}

static void close_mailbox(struct vm_state *vms, struct ast_vm_user *vmu)
{
	int x;
	char ntxt[256] = "";
	char txt[256] = "";
	if (vms->lastmsg > -1) { 
		/* Get the deleted messages fixed */ 
		vms->curmsg = -1; 
		for (x=0;x<=vms->lastmsg;x++) { 
			if (!vms->deleted[x] && (strcasecmp(vms->curbox, "INBOX") || !vms->heard[x])) { 
				/* Save this message.  It's not in INBOX or hasn't been heard */ 
				vms->curmsg++; 
				make_file(vms->fn, sizeof(vms->fn), vms->curdir, x); 
				make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg); 
				if (strcmp(vms->fn, vms->fn2)) { 
					snprintf(txt, sizeof(txt), "%s.txt", vms->fn); 
					snprintf(ntxt, sizeof(ntxt), "%s.txt", vms->fn2); 
					ast_filerename(vms->fn, vms->fn2, NULL); 
					rename(txt, ntxt); 
				} 
			} else if (!strcasecmp(vms->curbox, "INBOX") && vms->heard[x] && !vms->deleted[x]) { 
				/* Move to old folder before deleting */ 
				save_to_folder(vms->curdir, x, vmu->context, vms->username, 1); 
			} 
		} 
		for (x = vms->curmsg + 1; x<=vms->lastmsg; x++) { 
			make_file(vms->fn, sizeof(vms->fn), vms->curdir, x); 
			snprintf(txt, sizeof(txt), "%s.txt", vms->fn); 
			ast_filedelete(vms->fn, NULL); 
			unlink(txt); 
		} 
	} 
	memset(vms->deleted, 0, sizeof(vms->deleted)); 
	memset(vms->heard, 0, sizeof(vms->heard)); 
}

static int vm_intro(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages);
			if (!res)
				res = play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = play_and_wait(chan, "vm-and");
			else if (!res) {
				if ((vms->newmessages == 1))
					res = play_and_wait(chan, "vm-message");
				else
					res = play_and_wait(chan, "vm-messages");
			}
				
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages);
			if (!res)
				res = play_and_wait(chan, "vm-Old");
			if (!res) {
				if (vms->oldmessages == 1)
					res = play_and_wait(chan, "vm-message");
				else
					res = play_and_wait(chan, "vm-messages");
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = play_and_wait(chan, "vm-no");
				if (!res)
					res = play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

static int vm_instructions(struct ast_channel *chan, struct vm_state *vms)
{
	int res = 0;
	/* Play instructions and wait for new command */
	while(!res) {
		if (vms->starting) {
			if (vms->lastmsg > -1) {
				res = play_and_wait(chan, "vm-onefor");
				if (!res)
					res = play_and_wait(chan, vms->vmbox);
				if (!res)
					res = play_and_wait(chan, "vm-messages");
			}
			if (!res)
				res = play_and_wait(chan, "vm-opts");
		} else {
			if (vms->curmsg)
				res = play_and_wait(chan, "vm-prev");
			if (!res)
				res = play_and_wait(chan, "vm-repeat");
			if (!res && (vms->curmsg != vms->lastmsg))
				res = play_and_wait(chan, "vm-next");
			if (!res) {
				if (!vms->deleted[vms->curmsg])
					res = play_and_wait(chan, "vm-delete");
				else
					res = play_and_wait(chan, "vm-undelete");
				if (!res)
					res = play_and_wait(chan, "vm-toforward");
				if (!res)
					res = play_and_wait(chan, "vm-savemessage");
			}
		}
		if (!res)
			res = play_and_wait(chan, "vm-helpexit");
		if (!res)
			res = ast_waitfordigit(chan, 6000);
		if (!res) {
			vms->repeats++;
			if (vms->repeats > 2) {
				res = play_and_wait(chan, "vm-goodbye");
				if (!res)
					res = 't';
			}
		}
	}
	return res;
}

static int vm_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc)
{
	int cmd = 0;
	int retries = 0;
	char newpassword[80] = "";
	char newpassword2[80] = "";
	char prefile[256]="";
	while((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1':
			snprintf(prefile,sizeof(prefile),"voicemail/%s/%s/unavail",vmu->context, vms->username);
			cmd = play_and_record(chan,"vm-rec-unv",prefile, maxgreet, fmtc);
			break;
		case '2': 
			snprintf(prefile,sizeof(prefile),"voicemail/%s/%s/busy",vmu->context, vms->username);
			cmd = play_and_record(chan,"vm-rec-busy",prefile, maxgreet, fmtc);
			break;
		case '3': 
			snprintf(prefile,sizeof(prefile),"voicemail/%s/%s/greet",vmu->context, vms->username);
			cmd = play_and_record(chan,"vm-rec-name",prefile, maxgreet, fmtc);
			break;
		case '4':
			newpassword[1] = '\0';
			newpassword[0] = cmd = play_and_wait(chan,"vm-newpassword");
			if (cmd < 0)
				break;
			if ((cmd = ast_readstring(chan,newpassword + strlen(newpassword),sizeof(newpassword)-1,2000,10000,"#")) < 0) {
				break;
            }
			newpassword2[1] = '\0';
			newpassword2[0] = cmd = play_and_wait(chan,"vm-reenterpassword");
			if (cmd < 0)
				break;

			if ((cmd = ast_readstring(chan,newpassword2 + strlen(newpassword2),sizeof(newpassword2)-1,2000,10000,"#"))) {
				break;
            }
			if (strcmp(newpassword, newpassword2)) {
				ast_log(LOG_NOTICE,"Password mismatch for user %s (%s != %s)\n", vms->username, newpassword, newpassword2);
				cmd = play_and_wait(chan, "vm-mismatch");
				break;
			}
			if (vm_change_password(vmu,newpassword) < 0)
			{
				ast_log(LOG_DEBUG,"Failed to set new password of user %s\n",vms->username);
			} else
                ast_log(LOG_DEBUG,"User %s set password to %s of length %i\n",vms->username,newpassword,strlen(newpassword));
			cmd = play_and_wait(chan,"vm-passchanged");
			break;
		case '*': 
			cmd = 't';
			break;
		default: 
			cmd = play_and_wait(chan,"vm-options");
			if (!cmd)
				cmd = ast_waitfordigit(chan,6000);
			if (!cmd)
				retries++;
			if (retries > 3)
				cmd = 't';
		 }
	}
	if (cmd == 't')
		cmd = 0;
	return cmd;
}

static int vm_execmain(struct ast_channel *chan, void *data)
{
	/* XXX This is, admittedly, some pretty horrendus code.  For some
	   reason it just seemed a lot easier to do with GOTO's.  I feel
	   like I'm back in my GWBASIC days. XXX */
	int res=-1;
	int valid = 0;
	int prefix = 0;
	int cmd=0;
	struct localuser *u;
	char prefixstr[80] ="";
	char empty[80] = "";
	int box;
	int useadsi = 0;
	int skipuser = 0;
	char tmp[256], *ext;
	char fmtc[256] = "";
	char password[80];
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	char *context=NULL;

	LOCAL_USER_ADD(u);
	memset(&vms, 0, sizeof(vms));
	strncpy(fmtc, vmfmts, sizeof(fmtc) - 1);
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	if (data && strlen(data)) {
		strncpy(tmp, data, sizeof(tmp) - 1);
		ext = tmp;

		switch (*ext) {
			case 's':
		 /* We should skip the user's password */
				valid++;
				ext++;
				break;
			case 'p':
		 /* We should prefix the mailbox with the supplied data */
				prefix++;
				ext++;
				break;
		}

		context = strchr(ext, '@');
		if (context) {
			*context = '\0';
			context++;
		}

		if (prefix)
			strncpy(prefixstr, ext, sizeof(prefixstr) - 1);
		else
			strncpy(vms.username, ext, sizeof(vms.username) - 1);
		if (strlen(vms.username) && (vmu = find_user(&vmus, context ,vms.username)))
			skipuser++;
		else
			valid = 0;

	}

	/* If ADSI is supported, setup login screen */
	adsi_begin(chan, &useadsi);
	if (!skipuser && useadsi)
		adsi_login(chan);
	if (!skipuser && ast_streamfile(chan, "vm-login", chan->language)) {
		ast_log(LOG_WARNING, "Couldn't stream login file\n");
		goto out;
	}
	
	/* Authenticate them and get their mailbox/password */
	
	while (!valid) {
		/* Prompt for, and read in the username */
		if (!skipuser && ast_readstring(chan, vms.username, sizeof(vms.username) - 1, 2000, 10000, "#") < 0) {
			ast_log(LOG_WARNING, "Couldn't read username\n");
			goto out;
		}
		if (!strlen(vms.username)) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Username not entered\n");
			res = 0;
			goto out;
		}
		if (useadsi)
			adsi_password(chan);
		if (ast_streamfile(chan, "vm-password", chan->language)) {
			ast_log(LOG_WARNING, "Unable to stream password file\n");
			goto out;
		}
		if (ast_readstring(chan, password, sizeof(password) - 1, 2000, 10000, "#") < 0) {
			ast_log(LOG_WARNING, "Unable to read password\n");
			goto out;
		}
		if (prefix) {
			char fullusername[80] = "";
			strncpy(fullusername, prefixstr, sizeof(fullusername) - 1);
			strncat(fullusername, vms.username, sizeof(fullusername) - 1);
			strncpy(vms.username, fullusername, sizeof(vms.username) - 1);
		}
		if (!skipuser) 
			vmu = find_user(&vmus, context, vms.username);
		if (vmu && !strcmp(vmu->password, password)) 
			valid++;
		else {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Incorrect password '%s' for user '%s' (context = %s)\n", password, vms.username, context ? context : "<any>");
			if (prefix)
				strncpy(vms.username, empty, sizeof(vms.username) -1);
		}
		if (!valid) {
			if (useadsi)
				adsi_login(chan);
			if (ast_streamfile(chan, "vm-incorrect", chan->language))
				break;
		}
	}

	if (valid) {
		snprintf(vms.curdir, sizeof(vms.curdir), "%s/voicemail/%s", (char *)ast_config_AST_SPOOL_DIR, vmu->context);
		mkdir(vms.curdir, 0700);
		snprintf(vms.curdir, sizeof(vms.curdir), "%s/voicemail/%s/%s", (char *)ast_config_AST_SPOOL_DIR, vmu->context, vms.username);
		mkdir(vms.curdir, 0700);
		/* Retrieve old and new message counts */
		open_mailbox(&vms, vmu, 1);
		vms.oldmessages = vms.lastmsg + 1;
		/* Start in INBOX */
		open_mailbox(&vms, vmu, 0);
		vms.newmessages = vms.lastmsg + 1;
		

		/* Select proper mailbox FIRST!! */
		if (!vms.newmessages && vms.oldmessages) {
			/* If we only have old messages start here */
			open_mailbox(&vms, vmu, 1);
		}

		if (useadsi)
			adsi_status(chan, vms.newmessages, vms.oldmessages, vms.lastmsg);
		res = 0;
		cmd = vm_intro(chan, &vms);
		vms.repeats = 0;
		vms.starting = 1;
		while((cmd > -1) && (cmd != 't') && (cmd != '#')) {
			/* Run main menu */
			switch(cmd) {
			case '1':
				vms.curmsg = 0;
				/* Fall through */
			case '5':
				if (vms.lastmsg > -1) {
					cmd = play_message(chan, &vms, vms.curmsg);
				} else {
					cmd = play_and_wait(chan, "vm-youhave");
					if (!cmd) 
						cmd = play_and_wait(chan, "vm-no");
					if (!cmd) {
						snprintf(vms.fn, sizeof(vms.fn), "vm-%s", vms.curbox);
						cmd = play_and_wait(chan, vms.fn);
					}
					if (!cmd)
						cmd = play_and_wait(chan, "vm-messages");
				}
				break;
			case '2': /* Change folders */
				if (useadsi)
					adsi_folders(chan, 0, "Change to folder...");
				if (cmd == '#') {
					cmd = 0;
				} else if (cmd > 0) {
					cmd = cmd - '0';
					close_mailbox(&vms, vmu);
					open_mailbox(&vms, vmu, cmd);
					cmd = 0;
				}
				if (useadsi)
					adsi_status2(chan, vms.curbox, vms.lastmsg + 1);
				if (!cmd)
					cmd = play_and_wait(chan, vms.vmbox);
				if (!cmd)
					cmd = play_and_wait(chan, "vm-messages");
				vms.starting = 1;
				break;
			case '4':
				if (vms.curmsg) {
					vms.curmsg--;
					cmd = play_message(chan, &vms, vms.curmsg);
				} else {
					cmd = play_and_wait(chan, "vm-nomore");
				}
				break;
			case '6':
				if (vms.curmsg < vms.lastmsg) {
					vms.curmsg++;
					cmd = play_message(chan, &vms, vms.curmsg);
				} else {
					cmd = play_and_wait(chan, "vm-nomore");
				}
				break;
			case '7':
				vms.deleted[vms.curmsg] = !vms.deleted[vms.curmsg];
				if (useadsi)
					adsi_delete(chan, vms.curmsg, vms.lastmsg, vms.deleted[vms.curmsg]);
				if (vms.deleted[vms.curmsg]) 
					cmd = play_and_wait(chan, "vm-deleted");
				else
					cmd = play_and_wait(chan, "vm-undeleted");
				break;
			case '8':
				if(vms.lastmsg > -1)
					cmd = forward_message(chan, context, vms.curdir, vms.curmsg, vmu, vmfmts);
				break;
			case '9':
				if (useadsi)
					adsi_folders(chan, 1, "Save to folder...");
				cmd = get_folder2(chan, "vm-savefolder", 1);
				box = 0;	/* Shut up compiler */
				if (cmd == '#') {
					cmd = 0;
					break;
				} else if (cmd > 0) {
					box = cmd = cmd - '0';
					cmd = save_to_folder(vms.curdir, vms.curmsg, vmu->context, vms.username, cmd);
					vms.deleted[vms.curmsg]=1;
				}
				make_file(vms.fn, sizeof(vms.fn), vms.curdir, vms.curmsg);
				if (useadsi)
					adsi_message(chan, vms.curbox, vms.curmsg, vms.lastmsg, vms.deleted[vms.curmsg], vms.fn);
				if (!cmd)
					cmd = play_and_wait(chan, "vm-message");
				if (!cmd)
					cmd = say_and_wait(chan, vms.curmsg + 1);
				if (!cmd)
					cmd = play_and_wait(chan, "vm-savedto");
				if (!cmd) {
					snprintf(vms.fn, sizeof(vms.fn), "vm-%s", mbox(box));
					cmd = play_and_wait(chan, vms.fn);
				}
				if (!cmd)
					cmd = play_and_wait(chan, "vm-messages");
				break;
			case '*':
				if (!vms.starting) {
					cmd = play_and_wait(chan, "vm-onefor");
					if (!cmd)
						cmd = play_and_wait(chan, vms.vmbox);
					if (!cmd)
						cmd = play_and_wait(chan, "vm-messages");
					if (!cmd)
						cmd = play_and_wait(chan, "vm-opts");
				} else
					cmd = 0;
				break;
			case '0':
				cmd = vm_options(chan, vmu, &vms, vmfmts);
				break;
			case '#':
				ast_stopstream(chan);
				adsi_goodbye(chan);
				cmd = play_and_wait(chan, "vm-goodbye");
				if (cmd > 0)
					cmd = '#';
				break;
			default:	/* Nothing */
				cmd = vm_instructions(chan, &vms);
				break;
			}
		}
		if (cmd == 't') {
			/* Timeout */
			res = 0;
		} else {
			/* Hangup */
			res = -1;
		}
	}
out:
	if ((res > -1) && (cmd != '#')) {
		ast_stopstream(chan);
		adsi_goodbye(chan);
		if (useadsi)
			adsi_unload_session(chan);
	}
	if (vmu)
		close_mailbox(&vms, vmu);
	if (vmu)
		free_user(vmu);
	if (valid) {
		manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", vms.username, ast_app_has_voicemail(vms.username));
	}
	LOCAL_USER_REMOVE(u);
	return res;

}

static int vm_exec(struct ast_channel *chan, void *data)
{
	int res=0, silent=0, busy=0, unavail=0;
	struct localuser *u;
	char tmp[256], *ext;
	
	LOCAL_USER_ADD(u);
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	if (data)
		strncpy(tmp, data, sizeof(tmp) - 1);
	else {
		res = ast_app_getdata(chan, "vm-whichbox", tmp, sizeof(tmp) - 1, 0);
		if (res < 0)
			return res;
		if (!strlen(tmp))
			return 0;
	}
	ext = tmp;
	while(*ext) {
		if (*ext == 's') {
			silent = 2;
			ext++;
		} else if (*ext == 'b') {
			busy=1;
			ext++;
		} else if (*ext == 'u') {
			unavail=1;
			ext++;
		} else 
			break;
	}
	res = leave_voicemail(chan, ext, silent, busy, unavail);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int append_mailbox(char *context, char *mbox, char *data)
{
	/* Assumes lock is already held */
	char tmp[256] = "";
	char *stringp;
	char *s;
	struct ast_vm_user *vmu;
	strncpy(tmp, data, sizeof(tmp));
	vmu = malloc(sizeof(struct ast_vm_user));
	if (vmu) {
		memset(vmu, 0, sizeof(struct ast_vm_user));
		strncpy(vmu->context, context, sizeof(vmu->context));
		strncpy(vmu->mailbox, mbox, sizeof(vmu->mailbox));
		stringp = tmp;
		if ((s = strsep(&stringp, ","))) 
			strncpy(vmu->password, s, sizeof(vmu->password));
		if ((s = strsep(&stringp, ","))) 
			strncpy(vmu->fullname, s, sizeof(vmu->fullname));
		if ((s = strsep(&stringp, ","))) 
			strncpy(vmu->email, s, sizeof(vmu->email));
		if ((s = strsep(&stringp, ","))) 
			strncpy(vmu->pager, s, sizeof(vmu->pager));
		vmu->next = NULL;
		if (usersl)
			usersl->next = vmu;
		else
			users = vmu;
		usersl = vmu;
	}
	return 0;
}

static int load_users(void)
{
	struct ast_vm_user *cur, *l;
	struct ast_config *cfg;
	char *cat;
	struct ast_variable *var;
	char *astattach;
	char *silencestr;
	char *thresholdstr;
	char *fmt;
	char *astemail;
	char *s;
	int x;
	cfg = ast_load(VOICEMAIL_CONFIG);
	ast_pthread_mutex_lock(&vmlock);
	cur = users;
	while(cur) {
		l = cur;
		cur = cur->next;
		free_user(l);
	}
	users = NULL;
	usersl = NULL;
	if (cfg) {
		/* General settings */
		attach_voicemail = 1;
		if (!(astattach = ast_variable_retrieve(cfg, "general", "attach"))) 
			astattach = "yes";
		attach_voicemail = ast_true(astattach);
		maxsilence = 0;
		if ((silencestr = ast_variable_retrieve(cfg, "general", "maxsilence"))) {
			maxsilence = atoi(silencestr);
			if (maxsilence > 0)
				maxsilence *= 1000;
		}
		
		silencethreshold = 256;
		if ((thresholdstr = ast_variable_retrieve(cfg, "general", "silencethreshold")))
			silencethreshold = atoi(thresholdstr);
		
		if (!(astemail = ast_variable_retrieve(cfg, "general", "serveremail"))) 
			astemail = ASTERISK_USERNAME;
		strncpy(serveremail, astemail, sizeof(serveremail) - 1);
		
		vmmaxmessage = 0;
		if ((s = ast_variable_retrieve(cfg, "general", "maxmessage"))) {
			if (sscanf(s, "%d", &x) == 1) {
				vmmaxmessage = x;
			} else {
				ast_log(LOG_WARNING, "Invalid max message time length\n");
			}
		}
		fmt = ast_variable_retrieve(cfg, "general", "format");
		if (!fmt)
			fmt = "wav";	
		strncpy(vmfmts, fmt, sizeof(vmfmts) - 1);

		skipms = 3000;
		if ((s = ast_variable_retrieve(cfg, "general", "maxgreet"))) {
			if (sscanf(s, "%d", &x) == 1) {
				maxgreet = x;
			} else {
				ast_log(LOG_WARNING, "Invalid max message greeting length\n");
			}
		}

		if ((s = ast_variable_retrieve(cfg, "general", "skipms"))) {
			if (sscanf(s, "%d", &x) == 1) {
				skipms = x;
			} else {
				ast_log(LOG_WARNING, "Invalid skipms value\n");
			}
		}

		cat = ast_category_browse(cfg, NULL);
		while(cat) {
			if (strcasecmp(cat, "general")) {
				/* Process mailboxes in this context */
				var = ast_variable_browse(cfg, cat);
				while(var) {
					append_mailbox(cat, var->name, var->value);
					var = var->next;
				}
			}
			cat = ast_category_browse(cfg, cat);
		}
		ast_destroy(cfg);
	}
	ast_pthread_mutex_unlock(&vmlock);
	return 0;
}

int reload(void)
{
	load_users();
	return 0;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);
	return res;
}

int load_module(void)
{
	int res;
	load_users();
	res = ast_register_application(app, vm_exec, synopsis_vm, descrip_vm);
	if (!res)
		res = ast_register_application(app2, vm_execmain, synopsis_vmain, descrip_vmain);
	return res;
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
