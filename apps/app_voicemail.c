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

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/say.h>
#include <asterisk/module.h>
#include <asterisk/adsi.h>
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

int iocp;
int iolen;
int linelength;
int ateof;
unsigned char iobuf[BASEMAXINLINE];

static char *tdesc = "Comedian Mail (Voicemail System)";

static char *adapp = "CoMa";

static char *adsec = "_AST";

static char *addesc = "Comedian Mail";

static int adver = 1;

static char *synopsis_vm =
"Leave a voicemail message";

static char *descrip_vm =
"  VoiceMail([s|u|b]extension): Leaves voicemail for a given  extension (must\n"
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
"  VoiceMailMain(): Enters the main voicemail system for the checking of voicemail.  Returns\n"
"  -1 if the user hangs up or 0 otherwise.\n";

/* Leave a message */
static char *app = "VoiceMail";

/* Check mail, control, etc */
static char *app2 = "VoiceMailMain";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int make_dir(char *dest, int len, char *ext, char *mailbox)
{
	return snprintf(dest, len, "%s/%s/%s", VM_SPOOL_DIR, ext, mailbox);
}

static int make_file(char *dest, int len, char *dir, int num)
{
	return snprintf(dest, len, "%s/msg%04d", dir, num);
}

#if 0

static int announce_message(struct ast_channel *chan, char *dir, int msgcnt)
{
	char *fn;
	int res;

	res = ast_streamfile(chan, "vm-message", chan->language);
	if (!res) {
		res = ast_waitstream(chan, AST_DIGIT_ANY);
		if (!res) {
			res = ast_say_number(chan, msgcnt+1, chan->language);
			if (!res) {
				fn = get_fn(dir, msgcnt);
				if (fn) {
					res = ast_streamfile(chan, fn, chan->language);
					free(fn);
				}
			}
		}
	}
	if (res < 0)
		ast_log(LOG_WARNING, "Unable to announce message\n");
	return res;
}
#endif

static int
inbuf(FILE *fi)
{
	int l;

	if(ateof)
		return 0;

	if ( (l = fread(iobuf,1,BASEMAXINLINE,fi)) <= 0) {
		if(ferror(fi))
			return -1;

		ateof = 1;
		return 0;
	}

	iolen= l;
	iocp= 0;

	return 1;
}

static int 
inchar(FILE *fi)
{
	if(iocp>=iolen)
		if(!inbuf(fi))
			return EOF;

	return iobuf[iocp++];
}

static int
ochar(int c, FILE *so)
{
	if(linelength>=BASELINELEN) {
		if(fputs(eol,so)==EOF)
			return -1;

		linelength= 0;
	}

	if(putc(((unsigned char)c),so)==EOF)
		return -1;

	linelength++;

	return 1;
}

static int base_encode(char *filename, FILE *so)
{
	unsigned char dtable[BASEMAXINLINE];
	int i,hiteof= 0;
	FILE *fi;

	linelength = 0;
	iocp = BASEMAXINLINE;
	iolen = 0;
	ateof = 0;

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
			if ( (c = inchar(fi)) == EOF) {
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
				ochar(ogroup[i], so);
		}
	}

	if(fputs(eol,so)==EOF)
		return 0;

	fclose(fi);

	return 1;
}

static int sendmail(char *srcemail, char *email, char *name, int msgnum, char *mailbox, char *callerid, char *attach, char *format)
{
	FILE *p;
	char date[256];
	char host[256];
	char who[256];
	char bound[256];
	char fname[256];
	time_t t;
	struct tm *tm;
	p = popen(SENDMAIL, "w");

	if (p) {
		if (strchr(srcemail, '@'))
			strncpy(who, srcemail, sizeof(who)-1);
		else {
			gethostname(host, sizeof(host));
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		time(&t);
		tm = localtime(&t);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", tm);
		fprintf(p, "Date: %s\n", date);
		fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s <%s>\n", name, email);
		fprintf(p, "Subject: [PBX]: New message %d in mailbox %s\n", msgnum, mailbox);
		fprintf(p, "Message-ID: <Asterisk-%d-%s-%d@%s>\n", msgnum, mailbox, getpid(), host);
		fprintf(p, "MIME-Version: 1.0\n");

		// Something unique.
		snprintf(bound, sizeof(bound), "Boundary=%d%s%d", msgnum, mailbox, getpid());

		fprintf(p, "Content-Type: MULTIPART/MIXED; BOUNDARY=\"%s\"\n\n\n", bound);

		fprintf(p, "--%s\n", bound);
		fprintf(p, "Content-Type: TEXT/PLAIN; charset=US-ASCII\n\n");
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", tm);
		fprintf(p, "Dear %s:\n\n\tJust wanted to let you know you were just left a message (number %d)\n"

		           "in mailbox %s from %s, on %s so you might\n"
				   "want to check it when you get a chance.  Thanks!\n\n\t\t\t\t--Asterisk\n\n", name, 
			msgnum, mailbox, (callerid ? callerid : "an unknown caller"), date);

		fprintf(p, "--%s\n", bound);
		fprintf(p, "Content-Type: TEXT/PLAIN; charset=US-ASCII; name=\"msg%04d\"\n", msgnum);
		fprintf(p, "Content-Transfer-Encoding: BASE64\n");
		fprintf(p, "Content-Description: Voicemail sound attachment.\n");
		fprintf(p, "Content-Disposition: attachment; filename=\"msg%04d.%s\"\n\n", msgnum, format);

		snprintf(fname, sizeof(fname), "%s.%s", attach, format);
		base_encode(fname, p);
		fprintf(p, "\n\n--%s--\n.\n", bound);
		pclose(p);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", SENDMAIL);
		return -1;
	}
	return 0;
}

static int get_date(char *s, int len)
{
	struct tm *tm;
	time_t t;
	t = time(0);
	tm = localtime(&t);
	return strftime(s, len, "%a %b %e %r %Z %Y", tm);
}

static int invent_message(struct ast_channel *chan, char *ext, int busy)
{
	int res;
	res = ast_streamfile(chan, "vm-theperson", chan->language);
	if (res)
		return -1;
	res = ast_waitstream(chan, "#");
	if (res)
		return res;
	res = ast_say_digit_str(chan, ext, "#", chan->language);
	if (res)
		return res;
	if (busy)
		res = ast_streamfile(chan, "vm-isonphone", chan->language);
	else
		res = ast_streamfile(chan, "vm-isunavail", chan->language);
	if (res)
		return -1;
	res = ast_waitstream(chan, "#");
	return res;
}

static int leave_voicemail(struct ast_channel *chan, char *ext, int silent, int busy, int unavail)
{
	struct ast_config *cfg;
	char *copy, *name, *passwd, *email, *fmt, *fmts;
	char comment[256];
	struct ast_filestream *writer=NULL, *others[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char txtfile[256];
	FILE *txt;
	int res = -1, fmtcnt=0, x;
	int msgnum;
	int outmsg=0;
	int wavother=0;
	struct ast_frame *f;
	char date[256];
	char dir[256];
	char fn[256];
	char prefile[256]="";
	char *astemail;

	cfg = ast_load(VOICEMAIL_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", VOICEMAIL_CONFIG);
		return -1;
	}
	if (!(astemail = ast_variable_retrieve(cfg, "general", "serveremail"))) 
		astemail = ASTERISK_USERNAME;
	if ((copy = ast_variable_retrieve(cfg, NULL, ext))) {
		/* Setup pre-file if appropriate */
		if (busy)
			snprintf(prefile, sizeof(prefile), "vm/%s/busy", ext);
		else if (unavail)
			snprintf(prefile, sizeof(prefile), "vm/%s/unavail", ext);
		/* Make sure they have an entry in the config */
		copy = strdup(copy);
		passwd = strtok(copy, ",");
		name = strtok(NULL, ",");
		email = strtok(NULL, ",");
		make_dir(dir, sizeof(dir), ext, "");
		/* It's easier just to try to make it than to check for its existence */
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		make_dir(dir, sizeof(dir), ext, "INBOX");
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		/* Play the beginning intro if desired */
		if (strlen(prefile)) {
			if (ast_fileexists(prefile, NULL, NULL) > 0) {
				if (ast_streamfile(chan, prefile, chan->language) > -1) 
				    silent = ast_waitstream(chan, "#");
			} else {
				ast_log(LOG_DEBUG, "%s doesn't exist, doing what we can\n", prefile);
				silent = invent_message(chan, ext, busy);
			}
			if (silent < 0) {
				ast_log(LOG_DEBUG, "Hang up during prefile playback\n");
				free(copy);
				return -1;
			}
		}
		/* If they hit "#" we should still play the beep sound */
		if (silent == '#') {
			if (!ast_streamfile(chan, "beep", chan->language) < 0)
				silent = 1;
			ast_waitstream(chan, "");
		}
		/* Stream an info message */
		if (silent || !ast_streamfile(chan, INTRO, chan->language)) {
			/* Wait for the message to finish */
			if (silent || !ast_waitstream(chan, "")) {
				fmt = ast_variable_retrieve(cfg, "general", "format");
				if (fmt) {
					fmts = strdup(fmt);
					fmt = strtok(fmts, "|");
					msgnum = 0;
					do {
						make_file(fn, sizeof(fn), dir, msgnum);
						snprintf(comment, sizeof(comment), "Voicemail from %s to %s (%s) on %s\n",
											(chan->callerid ? chan->callerid : "Unknown"), 
											name, ext, chan->name);
						if (ast_fileexists(fn, NULL, chan->language) > 0) {
							msgnum++;
							continue;
						}
						writer = ast_writefile(fn, fmt, comment, O_EXCL, 1 /* check for other formats */, 0700);
						if (!writer)
							break;
						msgnum++;
					} while(!writer && (msgnum < MAXMSG));
					if (writer) {
						/* Store information */
						snprintf(txtfile, sizeof(txtfile), "%s.txt", fn);
						txt = fopen(txtfile, "w+");
						if (txt) {
							get_date(date, sizeof(date));
							fprintf(txt, 
"#\n"
"# Message Information file\n"
"#\n"
"origmailbox=%s\n"
"context=%s\n"
"exten=%s\n"
"priority=%d\n"
"callerchan=%s\n"
"callerid=%s\n"
"origdate=%s\n",
	ext,
	chan->context,
	chan->exten,
	chan->priority,
	chan->name,
	chan->callerid ? chan->callerid : "Unknown",
	date);
							fclose(txt);
						} else
							ast_log(LOG_WARNING, "Error opening text file for output\n");
	
						/* We need to reset these values */
						free(fmts);
						fmt = ast_variable_retrieve(cfg, "general", "format");
						fmts = strdup(fmt);
						strtok(fmts, "|");
						while((fmt = strtok(NULL, "|"))) {
							if (fmtcnt > MAX_OTHER_FORMATS - 1) {
								ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
								break;
							}
							sfmt[fmtcnt++] = strdup(fmt);
						}
						for (x=0;x<fmtcnt;x++) {
							others[x] = ast_writefile(fn, sfmt[x], comment, 0, 0, 0700);
							if (!others[x]) {
								/* Ick, the other format didn't work, but be sure not
								   to leak memory here */
								int y;
								for(y=x+1;y < fmtcnt;y++)
									free(sfmt[y]);
								break;
							}
							if(!strcasecmp(sfmt[x], "wav"))
								wavother++;
							free(sfmt[x]);
						}
						if (x == fmtcnt) {
							/* Loop forever, writing the packets we read to the writer(s), until
							   we read a # or get a hangup */
							if (option_verbose > 2) 
								ast_verbose( VERBOSE_PREFIX_3 "Recording to %s\n", fn);
							f = NULL;
							for(;;) {
								res = ast_waitfor(chan, 2000);
								if (!res) {
									ast_log(LOG_WARNING, "No audio available on %s??\n", chan->name);
									res = -1;
								}
								
								if (res < 0) {
									f = NULL;
									break;
								}

								f = ast_read(chan);
								if (!f)
									break;
								if (f->frametype == AST_FRAME_VOICE) {
									/* Write the primary format */
									res = ast_writestream(writer, f);
									if (res) {
										ast_log(LOG_WARNING, "Error writing primary frame\n");
										break;
									}
									/* And each of the others */
									for (x=0;x<fmtcnt;x++) {
										res |= ast_writestream(others[x], f);
									}
									ast_frfree(f);
									/* Exit on any error */
									if (res) {
										ast_log(LOG_WARNING, "Error writing frame\n");
										break;
									}
								}
								if (f->frametype == AST_FRAME_DTMF) {
									if (f->subclass == '#') {
										if (option_verbose > 2) 
											ast_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
										outmsg=2;
										break;
									}
								}
							}
							if (!f) {
								if (option_verbose > 2) 
									ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
								res = -1;
								outmsg=1;
							}
						} else {
							ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", fn, sfmt[x]); 
							free(sfmt[x]);
						}

						ast_closestream(writer);
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
							/* Send e-mail if applicable */
								sendmail(astemail, email, name, msgnum, ext, chan->callerid, fn, wavother ? "wav" : fmts);
						}
					} else {
						if (msgnum < MAXMSG)
							ast_log(LOG_WARNING, "Error writing to mailbox %s\n", ext);
						else
							ast_log(LOG_WARNING, "Too many messages in mailbox %s\n", ext);
					}
					free(fmts);
				} else 
					ast_log(LOG_WARNING, "No format to save messages in \n");
			}
		} else
			ast_log(LOG_WARNING, "Unable to playback instructions\n");
			
		free(copy);
	} else
		ast_log(LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
	ast_destroy(cfg);
	/* Leave voicemail for someone */
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

static int play_and_wait(struct ast_channel *chan, char *fn)
{
	int d;
	d = ast_streamfile(chan, fn, chan->language);
	if (d)
		return d;
	d = ast_waitstream(chan, AST_DIGIT_ANY);
	return d;
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

static int save_to_folder(char *dir, int msg, char *username, int box)
{
	char sfn[256];
	char dfn[256];
	char ddir[256];
	char txt[256];
	char ntxt[256];
	char *dbox = mbox(box);
	int x;
	make_file(sfn, sizeof(sfn), dir, msg);
	make_dir(ddir, sizeof(ddir), username, dbox);
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
	unsigned char keys[6];
	int x;
	if (!adsi_available(chan))
		return;

	for (x=0;x<6;x++)
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
	unsigned char keys[6];
	int x;
	if (!adsi_available(chan))
		return;

	for (x=0;x<6;x++)
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
	unsigned char keys[6];
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

	unsigned char keys[6];

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
				strtok(buf, "=");
				val = strtok(NULL, "=");
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
	unsigned char keys[6];

	int x;

	if (!adsi_available(chan))
		return;

	/* New meaning for keys */
	for (x=0;x<5;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 6 + x);

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
	unsigned char keys[6];
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
	unsigned char keys[6];
	int x;

	char *mess = (messages == 1) ? "message" : "messages";

	if (!adsi_available(chan))
		return;

	/* Original command keys */
	for (x=0;x<6;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + x);

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

static int
forward_message(struct ast_channel *chan, struct ast_config *cfg, char *dir, int curmsg)
{
	char username[70];
	char sys[256];
	char todir[256];
	int todircount=0;

	while(1) {
		ast_streamfile(chan, "vm-extension", chan->language);

		if (ast_readstring(chan, username, sizeof(username) - 1, 2000, 10000, "#") < 0)
			return 0;
		if (ast_variable_retrieve(cfg, NULL, username)) {
			printf("Got %d\n", atoi(username));
			if (play_and_wait(chan, "vm-savedto"))
				break;

			snprintf(todir, sizeof(todir), "%s/%s/INBOX", VM_SPOOL_DIR, username);
			snprintf(sys, sizeof(sys), "mkdir -p %s\n", todir);
			puts(sys);
			system(sys);

			todircount = count_messages(todir);

			snprintf(sys, sizeof(sys), "cp %s/msg%04d.gsm %s/msg%04d.gsm\n", dir, curmsg, todir, todircount);
			puts(sys);
			system(sys);

			break;
		} else {
			if ( play_and_wait(chan, "pbx-invalid"))
				break;
		}
	}

	return 0;
}

#define WAITCMD(a) do { \
	d = (a); \
	if (d < 0) \
		goto out; \
	if (d) \
		goto cmd; \
} while(0)

#define WAITFILE2(file) do { \
	if (ast_streamfile(chan, file, chan->language)) \
		ast_log(LOG_WARNING, "Unable to play message %s\n", file); \
	d = ast_waitstream(chan, AST_DIGIT_ANY); \
	if (d < 0) { \
		goto out; \
	}\
} while(0)

#define WAITFILE(file) do { \
	if (ast_streamfile(chan, file, chan->language)) \
		ast_log(LOG_WARNING, "Unable to play message %s\n", file); \
	d = ast_waitstream(chan, AST_DIGIT_ANY); \
	if (!d) { \
		repeats = 0; \
		goto instructions; \
	} else if (d < 0) { \
		goto out; \
	} else goto cmd;\
} while(0)

#define PLAYMSG(a) do { \
	starting = 0; \
	make_file(fn, sizeof(fn), curdir, a); \
	adsi_message(chan, curbox, a, lastmsg, deleted[a], fn); \
	if (!a) \
		WAITFILE2("vm-first"); \
	else if (a == lastmsg) \
		WAITFILE2("vm-last"); \
	WAITFILE2("vm-message"); \
	if (a && (a != lastmsg)) { \
		d = ast_say_number(chan, a + 1, AST_DIGIT_ANY, chan->language); \
		if (d < 0) goto out; \
		if (d) goto cmd; \
	} \
	make_file(fn, sizeof(fn), curdir, a); \
	heard[a] = 1; \
	WAITFILE(fn); \
} while(0)

#define CLOSE_MAILBOX do { \
	if (lastmsg > -1) { \
		/* Get the deleted messages fixed */ \
		curmsg = -1; \
		for (x=0;x<=lastmsg;x++) { \
			if (!deleted[x] && (strcasecmp(curbox, "INBOX") || !heard[x])) { \
				/* Save this message.  It's not in INBOX or hasn't been heard */ \
				curmsg++; \
				make_file(fn, sizeof(fn), curdir, x); \
				make_file(fn2, sizeof(fn2), curdir, curmsg); \
				if (strcmp(fn, fn2)) { \
					snprintf(txt, sizeof(txt), "%s.txt", fn); \
					snprintf(ntxt, sizeof(ntxt), "%s.txt", fn2); \
					ast_filerename(fn, fn2, NULL); \
					rename(txt, ntxt); \
				} \
			} else if (!strcasecmp(curbox, "INBOX") && heard[x] && !deleted[x]) { \
				/* Move to old folder before deleting */ \
				save_to_folder(curdir, x, username, 1); \
			} \
		} \
		for (x = curmsg + 1; x<=lastmsg; x++) { \
			make_file(fn, sizeof(fn), curdir, x); \
			snprintf(txt, sizeof(txt), "%s.txt", fn); \
			ast_filedelete(fn, NULL); \
			unlink(txt); \
		} \
	} \
	memset(deleted, 0, sizeof(deleted)); \
	memset(heard, 0, sizeof(heard)); \
} while(0)

#define OPEN_MAILBOX(a) do { \
	strcpy(curbox, mbox(a)); \
	make_dir(curdir, sizeof(curdir), username, curbox); \
	lastmsg = count_messages(curdir) - 1; \
	snprintf(vmbox, sizeof(vmbox), "vm-%s", curbox); \
} while (0)


static int vm_execmain(struct ast_channel *chan, void *data)
{
	/* XXX This is, admittedly, some pretty horrendus code.  For some
	   reason it just seemed a lot easier to do with GOTO's.  I feel
	   like I'm back in my GWBASIC days. XXX */
	int res=-1;
	int valid = 0;
	char d;
	struct localuser *u;
	char username[80];
	char password[80], *copy;
	char curbox[80];
	char curdir[256];
	char vmbox[256];
	char fn[256];
	char fn2[256];
	int x;
	char ntxt[256];
	char txt[256];
	int deleted[MAXMSG] = { 0, };
	int heard[MAXMSG] = { 0, };
	int newmessages;
	int oldmessages;
	int repeats = 0;
	int curmsg = 0;
	int lastmsg = 0;
	int starting = 1;
	int box;
	int useadsi = 0;
	struct ast_config *cfg;

	LOCAL_USER_ADD(u);
	cfg = ast_load(VOICEMAIL_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "No voicemail configuration\n");
		goto out;
	}
	if (chan->state != AST_STATE_UP)
		ast_answer(chan);

	/* If ADSI is supported, setup login screen */
	adsi_begin(chan, &useadsi);
	if (useadsi)
		adsi_login(chan);
	if (ast_streamfile(chan, "vm-login", chan->language)) {
		ast_log(LOG_WARNING, "Couldn't stream login file\n");
		goto out;
	}
	
	/* Authenticate them and get their mailbox/password */
	
	do {
		/* Prompt for, and read in the username */
		if (ast_readstring(chan, username, sizeof(username) - 1, 2000, 10000, "#") < 0) {
			ast_log(LOG_WARNING, "Couldn't read username\n");
			goto out;
		}			
		if (!strlen(username)) {
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
		copy = ast_variable_retrieve(cfg, NULL, username);
		if (copy) {
			copy = strdup(copy);
			strtok(copy, ",");
			if (!strcmp(password,copy))
				valid++;
			else if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Incorrect password '%s' for user '%s'\n", password, username);
			free(copy);
		} else if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "No such user '%s' in config file\n", username);
		if (!valid) {
			if (useadsi)
				adsi_login(chan);
			if (ast_streamfile(chan, "vm-incorrect", chan->language))
				break;
#if 0
			if (ast_waitstream(chan, ""))
				break;
#endif
		}
	} while (!valid);

	if (valid) {
		OPEN_MAILBOX(1);
		oldmessages = lastmsg + 1;
		/* Start in INBOX */
		OPEN_MAILBOX(0);
		newmessages = lastmsg + 1;
		

		/* Select proper mailbox FIRST!! */
		if (!newmessages && oldmessages) {
			/* If we only have old messages start here */
			OPEN_MAILBOX(1);
		}

		if (useadsi)
			adsi_status(chan, newmessages, oldmessages, lastmsg);

		WAITCMD(play_and_wait(chan, "vm-youhave"));
		if (newmessages) {
			WAITCMD(say_and_wait(chan, newmessages));
			WAITCMD(play_and_wait(chan, "vm-INBOX"));

			if (oldmessages)
				WAITCMD(play_and_wait(chan, "vm-and"));
			else {
				if (newmessages == 1)
					WAITCMD(play_and_wait(chan, "vm-message"));
				else
					WAITCMD(play_and_wait(chan, "vm-messages"));
			}
				
		}
		if (oldmessages) {
			WAITCMD(say_and_wait(chan, oldmessages));
			WAITCMD(play_and_wait(chan, "vm-Old"));
			if (oldmessages == 1)
				WAITCMD(play_and_wait(chan, "vm-message"));
			else
				WAITCMD(play_and_wait(chan, "vm-messages"));
		}
		if (!oldmessages && !newmessages) {
			WAITCMD(play_and_wait(chan, "vm-no"));
			WAITCMD(play_and_wait(chan, "vm-messages"));
		}
		repeats = 0;
		starting = 1;
instructions:
		if (starting) {
			if (lastmsg > -1) {
				WAITCMD(play_and_wait(chan, "vm-onefor"));
				WAITCMD(play_and_wait(chan, vmbox));
				WAITCMD(play_and_wait(chan, "vm-messages"));
			}
			WAITCMD(play_and_wait(chan, "vm-opts"));
		} else {
			if (curmsg)
				WAITCMD(play_and_wait(chan, "vm-prev"));
			WAITCMD(play_and_wait(chan, "vm-repeat"));
			if (curmsg != lastmsg)
				WAITCMD(play_and_wait(chan, "vm-next"));
			if (!deleted[curmsg])
				WAITCMD(play_and_wait(chan, "vm-delete"));
			else
				WAITCMD(play_and_wait(chan, "vm-undelete"));
			WAITCMD(play_and_wait(chan, "vm-toforward"));
			WAITCMD(play_and_wait(chan, "vm-savemessage"));
		}
		WAITCMD(play_and_wait(chan, "vm-helpexit"));
		d = ast_waitfordigit(chan, 6000);
		if (d < 0)
			goto out;
		if (!d) {
			repeats++;
			if (repeats > 2) {
				play_and_wait(chan, "vm-goodbye");
				goto out;
			}
			goto instructions;
		}
cmd:
		switch(d) {
		case '2':
			if (useadsi)
				adsi_folders(chan, 0, "Change to folder...");
			box = play_and_wait(chan, "vm-changeto");
			if (box < 0)
				goto out;
			while((box < '0') || (box > '9')) {
				box = get_folder(chan, 0);
				if (box < 0)
					goto out;
				if (box == '#')
					goto instructions;
			} 
			box = box - '0';
			CLOSE_MAILBOX;
			OPEN_MAILBOX(box);
			if (useadsi)
				adsi_status2(chan, curbox, lastmsg + 1);
			WAITCMD(play_and_wait(chan, vmbox));
			WAITCMD(play_and_wait(chan, "vm-messages"));
			starting = 1;
			goto instructions;
		case '4':
			if (curmsg) {
				curmsg--;
				PLAYMSG(curmsg);
			} else {
				WAITCMD(play_and_wait(chan, "vm-nomore"));
				goto instructions;
			}
		case '1':
				curmsg = 0;
				/* Fall through */
		case '5':
			if (lastmsg > -1) {
				PLAYMSG(curmsg);
			} else {
				WAITCMD(play_and_wait(chan, "vm-youhave"));
				WAITCMD(play_and_wait(chan, "vm-no"));
				snprintf(fn, sizeof(fn), "vm-%s", curbox);
				WAITCMD(play_and_wait(chan, fn));
				WAITCMD(play_and_wait(chan, "vm-messages"));
				goto instructions;
			}
		case '6':
			if (curmsg < lastmsg) {
				curmsg++;
				PLAYMSG(curmsg);
			} else {
				WAITCMD(play_and_wait(chan, "vm-nomore"));
				goto instructions;
			}
		case '7':
			deleted[curmsg] = !deleted[curmsg];
			if (useadsi)
				adsi_delete(chan, curmsg, lastmsg, deleted[curmsg]);
			if (deleted[curmsg]) 
				WAITCMD(play_and_wait(chan, "vm-deleted"));
			else
				WAITCMD(play_and_wait(chan, "vm-undeleted"));
			goto instructions;
		case '8':
			if(lastmsg > -1)
				if(forward_message(chan, cfg, curdir, curmsg) < 0)
					goto out;
			goto instructions;
		case '9':
			if (useadsi)
				adsi_folders(chan, 1, "Save to folder...");
			box = play_and_wait(chan, "vm-savefolder");
			if (box < 0)
				goto out;
			while((box < '1') || (box > '9')) {
				box = get_folder(chan, 1);
				if (box < 0)
					goto out;
				if (box == '#')
					goto instructions;
			} 
			box = box - '0';
			if (option_debug)
				ast_log(LOG_DEBUG, "Save to folder: %s (%d)\n", mbox(box), box);
			if (save_to_folder(curdir, curmsg, username, box))
				goto out;
			deleted[curmsg]=1;
			make_file(fn, sizeof(fn), curdir, curmsg);
			if (useadsi)
				adsi_message(chan, curbox, curmsg, lastmsg, deleted[curmsg], fn);
			WAITCMD(play_and_wait(chan, "vm-message"));
			WAITCMD(say_and_wait(chan, curmsg + 1) );
			WAITCMD(play_and_wait(chan, "vm-savedto"));
			snprintf(fn, sizeof(fn), "vm-%s", mbox(box));
			WAITCMD(play_and_wait(chan, fn));
			WAITCMD(play_and_wait(chan, "vm-messages"));
			goto instructions;
		case '*':
			if (!starting) {
				WAITCMD(play_and_wait(chan, "vm-onefor"));
				WAITCMD(play_and_wait(chan, vmbox));
				WAITCMD(play_and_wait(chan, "vm-messages"));
				WAITCMD(play_and_wait(chan, "vm-opts"));
			}
			goto instructions;
		case '#':
			ast_stopstream(chan);
			adsi_goodbye(chan);
			play_and_wait(chan, "vm-goodbye");
			res = 0;
			goto out2;
		default:
			goto instructions;
		}
	}
out:
	adsi_goodbye(chan);
out2:
	CLOSE_MAILBOX;
	ast_stopstream(chan);
	if (cfg)
		ast_destroy(cfg);
	if (useadsi)
		adsi_unload_session(chan);
	adsi_channel_init(chan);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int vm_exec(struct ast_channel *chan, void *data)
{
	int res=0, silent=0, busy=0, unavail=0;
	struct localuser *u;
	char *ext = (char *)data;
	

	if (!data) {
		ast_log(LOG_WARNING, "vm requires an argument (extension)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	if (*ext == 's') {
		silent++;
		ext++;
	} else if (*ext == 'b') {
		busy++;
		ext++;
	} else if (*ext == 'u') {
		unavail++;
		ext++;
	}
	if (chan->state != AST_STATE_UP)
		ast_answer(chan);
	res = leave_voicemail(chan, ext, silent, busy, unavail);
	LOCAL_USER_REMOVE(u);
	return res;
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
