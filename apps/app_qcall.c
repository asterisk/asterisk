/** @file app_qcall.c 
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Call back a party and connect them to a running pbx thread
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * Call a user from a file contained within a queue (/var/spool/asterisk/qcall)
 * 
 * The queue is a directory containing files with the call request information
 * as a single line of text as follows:
 * 
 * Dialstring Caller-ID Extension Maxsecs [Identifier] [Required-response]
 *
 *  Dialstring -- A Dial String (The number to be called) in the
 *  format Technology/Number, such IAX/mysys/1234 or Zap/g1/1234
 * 
 *  Caller-ID -- A Standard nomalized representation of the Caller-ID of
 *  the number being dialed (generally 10 digits in the US). Leave as
 *  "asreceived" to use the default Caller*ID
 *
 *  Extension -- The Extension (optionally Extension@context) that the
 *  user should be "transferred" to after acceptance of the call.
 *
 *  Maxsecs -- The Maximum time of the call in seconds. Specify 0 for infinite.
 *
 *  Identifier -- The "Identifier" of the request. This is used to determine
 *  the names of the audio prompt files played. The first prompt, the one that
 *  asks for the input, is just the exact string specified as the identifier.
 *  The second prompt, the one that is played after the correct input is given,
 *  (generally a "thank you" recording), is the specified string with "-ok" 
 *  added to the end. So, if you specify "foo" as the identifier, your first
 *  prompt file that will be played will be "foo" and the second one will be
 *  "foo-ok".  If omitted no prompt is given
 *
 *  Required-Response (Optional) -- Specify a digit string to be used as the
 *  acceptance "code" if you desire it to be something other then "1". This
 *  can be used to implement some sort of PIN or security system. It may be
 *  more then a single character.
 *
 * NOTE: It is important to remember that the process that creates these
 * files needs keep and maintain a write lock (using flock with the LOCK_EX
 * option) when writing these files.
 *
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include "../astconf.h"

static char qdir[255];
static  char *tdesc = "Call from Queue";
static  pthread_t qcall_thread;
static int debug = 0;
STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

#define	OLDESTOK	14400		/* not any more then this number of secs old */
#define	INITIALONE	1		/* initial wait before the first one in secs */
#define	NEXTONE		600		/* wait before trying it again in secs */
#define	MAXWAITFORANSWER 45000		/* max call time before answer */
/* define either one or both of these two if your application requires it */
#if	0
#define	ACCTCODE	"SOMETHING"	/* Account code */
#define	AMAFLAGS AST_CDR_BILLING	/* AMA flags */
#endif
/* define this if you want to have a particular CLID display on the user's
   phone when they receive the call */
#if	0
#define	OURCLID	"2564286275"		/* The callerid to be displayed when calling */
#endif

static void *qcall_do(void *arg);

static void *qcall(void *ignore)
{
pthread_t dialer_thread;
DIR *dirp;
FILE *fp;
struct dirent *dp;
char fname[80];
struct stat mystat;
time_t	t;
void *arg;
pthread_attr_t attr;

	time(&t);
	if (debug) printf("@@@@ qcall starting at %s",ctime(&t));
	for(;;)
	   {
		time(&t);
		dirp = opendir(qdir);
		if (!dirp)
		   {
			perror("app_qcall:Cannot open queue directory");
			break;
		   }
		while((dp = readdir(dirp)) != NULL)
		   {
			if (dp->d_name[0] == '.') continue;
			snprintf(fname, sizeof(fname), "%s/%s", qdir, dp->d_name);
			if (stat(fname,&mystat) == -1)
			   {
				perror("app_qcall:stat");
				fprintf(stderr,"%s\n",fname);
				continue;
			   }
			  /* if not a regular file, skip it */
			if ((mystat.st_mode & S_IFMT) != S_IFREG) continue;
			  /* if not yet .... */
			if (mystat.st_atime == mystat.st_mtime)
			   {  /* first time */
				if ((mystat.st_atime + INITIALONE) > t) 
					continue;
			   }
			else
			   { /* already looked at once */
				if ((mystat.st_atime + NEXTONE) > t) continue;
			   }
			  /* if too old */
			if (mystat.st_mtime < (t - OLDESTOK))
			   {
				/* kill it, its too old */
				unlink(fname);
				continue;
			   }				
			 /* "touch" file's access time */
			fp = fopen(fname,"r");
			if (fp) fclose(fp);
			/* make a copy of the filename string, so that we
				may go on and use the buffer */
			arg = (void *) strdup(fname);
		        pthread_attr_init(&attr);
 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			if (pthread_create(&dialer_thread,&attr,qcall_do,arg) == -1)
			   {
				perror("qcall: Cannot create thread");
				continue;
			   }
		   }
		closedir(dirp);
		sleep(1);
	   }
	pthread_exit(NULL);
}
	
/* single thread with one file (request) to dial */
static void *qcall_do(void *arg)
{
	char fname[300] = "";
	char dialstr[300];
	char extstr[300];
	char ident[300] = "";
	char reqinp[300] = "";
	char buf[300];
	char clid[300],*tele,*context;
	FILE *fp;
	int ms = MAXWAITFORANSWER,maxsecs;
	struct ast_channel *channel;
	time_t	t;

	  /* get the filename from the arg */
	strncpy(fname,(char *)arg, sizeof(fname) - 1);
	free(arg);
	time(&t);
	fp = fopen(fname,"r");
	if (!fp) /* if cannot open request file */
	   {
		perror("qcall_do:fopen");
		fprintf(stderr,"%s\n",fname);
		unlink(fname);
		pthread_exit(NULL);
	   }
	/* lock the file */
	if (flock(fileno(fp),LOCK_EX) == -1)
	   {
		perror("qcall_do:flock");
		fprintf(stderr,"%s\n",fname);
		pthread_exit(NULL);
	   }
	/* default required input for acknowledgement */
	reqinp[0] = '1';
	reqinp[1] = '\0';
	/* default no ident */
	ident[0] = '\0';  /* default no ident */
	if (fscanf(fp,"%s %s %s %d %s %s",dialstr,clid,
		extstr,&maxsecs,ident,reqinp) < 4)
	   {
		fprintf(stderr,"qcall_do:file line invalid in file %s:\n",fname);
		pthread_exit(NULL);
	   }
	flock(fileno(fp),LOCK_UN);
	fclose(fp);
	tele = strchr(dialstr,'/');
	if (!tele)
	   {
		fprintf(stderr,"qcall_do:Dial number must be in format tech/number\n");
		unlink(fname);
		pthread_exit(NULL);
	   }
	*tele++ = 0;
	channel = ast_request(dialstr,AST_FORMAT_SLINEAR,tele);
	if (channel)
	   {
		ast_set_read_format(channel,AST_FORMAT_SLINEAR);
		ast_set_write_format(channel,AST_FORMAT_SLINEAR);
#ifdef	OURCLID
		if (channel->callerid)
			free(channel->callerid);
		channel->callerid = strdup(OURCLID);
		if (channel->ani)
			free(channel->ani);
		channel->ani = strdup(OURCLID);
#endif		
		channel->whentohangup = 0;
		channel->appl = "AppQcall";
		channel->data = "(Outgoing Line)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Qcall initiating call to %s/%s on %s (%s)\n",
				dialstr,tele,channel->name,fname);
		ast_call(channel,tele,MAXWAITFORANSWER);
	   }
	else
	   {
		fprintf(stderr,"qcall_do:Sorry unable to obtain channel\n");
		pthread_exit(NULL);
	   }
	if (strcasecmp(clid, "asreceived")) {
		if (channel->callerid) free(channel->callerid);
		channel->callerid = NULL;
		if (channel->ani) free(channel->ani);
		channel->ani = NULL;
	}
	if (channel->_state == AST_STATE_UP)
	if (debug) printf("@@@@ Autodial:Line is Up\n");
	if (option_verbose > 2)
	ast_verbose(VERBOSE_PREFIX_3 "Qcall waiting for answer on %s\n",
		channel->name);
	while(ms > 0){
		struct ast_frame *f;
		ms = ast_waitfor(channel,ms);
		f = ast_read(channel);
		if (!f)
		   {
			if (debug) printf("@@@@ qcall_do:Hung Up\n");
			unlink(fname);
			break;
		   }
		if (f->frametype == AST_FRAME_CONTROL)
		   {
			if (f->subclass == AST_CONTROL_HANGUP)
			   {
				if (debug) printf("@@@@ qcall_do:Hung Up\n");
				unlink(fname);
				ast_frfree(f);
				break;
			   }
			if (f->subclass == AST_CONTROL_ANSWER)
			   {
				if (debug) printf("@@@@ qcall_do:Phone Answered\n");
				if (channel->_state == AST_STATE_UP)
				   {
					unlink(fname);
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Qcall got answer on %s\n",
							channel->name);
					usleep(1500000);
					if (strlen(ident)) {
						ast_streamfile(channel,ident,0);
						if (ast_readstring(channel,buf,strlen(reqinp),10000,5000,"#"))
						{
							ast_stopstream(channel);
							if (debug) printf("@@@@ qcall_do: timeout or hangup in dtmf read\n");
							ast_frfree(f);
							break;
						}
						ast_stopstream(channel);
						if (strcmp(buf,reqinp)) /* if not match */
						{
							if (debug) printf("@@@@ qcall_do: response (%s) does not match required (%s)\n",buf,reqinp);
							ast_frfree(f);
							break;
						}
						ast_frfree(f);
					}
					/* okay, now we go for it */
					context = strchr(extstr,'@');
					if (!context) context = "default";
					else *context++ = 0;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Qcall got accept, now putting through to %s@%s on %s\n",
							extstr,context,channel->name);
					if (strlen(ident)) {
						strncat(ident,"-ok", sizeof(ident) - strlen(ident) - 1);
						/* if file existant, play it */
						if (!ast_streamfile(channel,ident,0))
						{
							ast_waitstream(channel,"");
							ast_stopstream(channel);
						}
					}
					if (strcasecmp(clid, "asreceived")) {
						channel->callerid = strdup(clid);
						channel->ani = strdup(clid);
					}
					channel->language[0] = 0;
					channel->dnid = strdup(extstr);
#ifdef	AMAFLAGS
					channel->amaflags = AMAFLAGS;
#endif
#ifdef	ACCTCODE
					strncpy(channel->accountcode, ACCTCODE, sizeof(chan->accountcode) - 1);
#else
					channel->accountcode[0] = 0;
#endif
					if (maxsecs)  /* if finite length call */
					   {
						time(&channel->whentohangup);
						channel->whentohangup += maxsecs;
					   }
					strncpy(channel->exten, extstr, sizeof(channel->exten) - 1);
					strncpy(channel->context, context, sizeof(channel->context) - 1);
					channel->priority = 1;
					if(debug) printf("Caller ID is %s\n", channel->callerid);
					ast_pbx_run(channel);
					pthread_exit(NULL);
				}
			}
			else if(f->subclass==AST_CONTROL_RINGING)
				if (debug) printf("@@@@ qcall_do:Phone Ringing end\n");
		}
		ast_frfree(f);
	}
	ast_hangup(channel);
	if (debug) printf("@@@@ qcall_do:Hung up channel\n");
	pthread_exit(NULL);
	return NULL;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return 0;
}

int load_module(void)
{
	snprintf(qdir, sizeof(qdir), "%s/%s", ast_config_AST_SPOOL_DIR, "qcall");
	mkdir(qdir,0760);
	pthread_create(&qcall_thread,NULL,qcall,NULL);
	return 0;
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
