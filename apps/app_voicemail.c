/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Voicemail System (did you ever think it could be so easy?)
 * 
 * Copyright (C) 2003, Digium Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/say.h>
#include <asterisk/module.h>
#include <asterisk/adsi.h>
#include <asterisk/app.h>
#include <asterisk/manager.h>
#include <asterisk/dsp.h>
#include <asterisk/localtime.h>
#include <asterisk/cli.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <dirent.h>

/* we define USESQLVM when we have MySQL or POSTGRES */
#ifdef USEMYSQLVM
#include <mysql/mysql.h>
#define USESQLVM 1
#endif

#ifdef USEPOSTGRESVM
/*
 * PostgreSQL routines written by Otmar Lendl <lendl@nic.at>
 */
#include <postgresql/libpq-fe.h>
#define USESQLVM 1
#endif

#ifndef USESQLVM
static inline int sql_init(void) { return 0; }
static inline void sql_close(void) { }
#endif

#include <pthread.h>
#include "../asterisk.h"
#include "../astconf.h"

#define COMMAND_TIMEOUT 5000

#define VOICEMAIL_CONFIG "voicemail.conf"
#define ASTERISK_USERNAME "asterisk"

/* Default mail command to mail voicemail. Change it with the
    mailcmd= command in voicemail.conf */
#define SENDMAIL "/usr/sbin/sendmail -t"

#define INTRO "vm-intro"

#define MAXMSG 100
#define MAX_OTHER_FORMATS 10

#define VM_SPOOL_DIR AST_SPOOL_DIR "/vm"

#define BASEMAXINLINE 256
#define BASELINELEN 72
#define BASEMAXINLINE 256
#define eol "\r\n"

#define MAX_DATETIME_FORMAT	512
#define MAX_NUM_CID_CONTEXTS 10

#define DIGITS_DIR	AST_SOUNDS "/digits/"


struct baseio {
	int iocp;
	int iolen;
	int linelength;
	int ateof;
	unsigned char iobuf[BASEMAXINLINE];
};

/* Structure for linked list of users */
struct ast_vm_user {
	char context[80];
	char mailbox[80];
	char password[80];
	char fullname[80];
	char email[80];
	char pager[80];
	char serveremail[80];
	char mailcmd[160];	/* Configurable mail command */
	char zonetag[80];
	char callback[80];
	char dialout[80];
	char exit[80];
	int attach;
	int alloced;
	int saycid;
	int review;
	int operator;
	int envelope;
	struct ast_vm_user *next;
};

struct vm_zone {
	char name[80];
	char timezone[80];
	char msg_format[512];
	struct vm_zone *next;
};

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
static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg, int option);
static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context);
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int outsidecaller, struct ast_vm_user *vmu, int *duration);
static int vm_delete(char *file);



static char *tdesc = "Comedian Mail (Voicemail System)";

static char *adapp = "\x00\x00\x00\x0F";

static char *adsec = "\x9B\xDB\xF7\xAC";

static char *addesc = "Comedian Mail";

static int adver = 1;

static char *synopsis_vm =
"Leave a voicemail message";

static char *descrip_vm =
"  VoiceMail([s|u|b]extension[@context]):  Leaves voicemail for a given\n"
"extension (must be configured in voicemail.conf).\n"
" If the extension is preceded by \n"
"* 's' then instructions for leaving the message will be skipped.\n"
"* 'u' then the \"unavailable\" message will be played.\n"
"  (/var/lib/asterisk/sounds/vm/<exten>/unavail) if it exists.\n"
"* 'b' then the the busy message will be played (that is, busy instead of unavail).\n"
"If the caller presses '0' (zero) during the prompt, the call jumps to\n"
"priority 'o' in the current context.\n"
"If the requested mailbox does not exist, and there exists a priority\n"
"n + 101, then that priority will be taken next.\n"
"Returns -1 on error or mailbox not found, or if the user hangs up.\n"
"Otherwise, it returns 0.\n";

static char *synopsis_vmain =
"Enter voicemail system";

static char *descrip_vmain =
"  VoiceMailMain([[s]mailbox][@context]): Enters the main voicemail system\n"
"for the checking of voicemail.  The mailbox can be passed as the option,\n"
"which will stop the voicemail system from prompting the user for the mailbox.\n"
"If the mailbox is preceded by 's' then the password check will be skipped.  If\n"
"a context is specified, logins are considered in that voicemail context only.\n"
"Returns -1 if the user hangs up or 0 otherwise.\n";

static char *synopsis_vm_box_exists =
"Check if vmbox exists";

static char *descrip_vm_box_exists =
"  MailboxExists(mailbox[@context]): Conditionally branches to priority n+101\n"
"if the specified voice mailbox exists.\n";


/* Leave a message */
static char *capp = "VoiceMail2";
static char *app = "VoiceMail";

/* Check mail, control, etc */
static char *capp2 = "VoiceMailMain2";
static char *app2 = "VoiceMailMain";

static char *app3 = "MailboxExists";

static ast_mutex_t vmlock = AST_MUTEX_INITIALIZER;
struct ast_vm_user *users;
struct ast_vm_user *usersl;
struct vm_zone *zones = NULL;
struct vm_zone *zonesl = NULL;
static int attach_voicemail;
static int maxsilence;
static int silencethreshold = 128;
static char serveremail[80];
static char mailcmd[160];	/* Configurable mail cmd */
static char externnotify[160]; 

static char vmfmts[80];
static int vmminmessage;
static int vmmaxmessage;
static int maxgreet;
static int skipms;
static int maxlogins;

static int reviewvm;
static int calloper;
static int saycidinfo;
static int hearenv;
static char dialcontext[80];
static char callcontext[80];
static char exitcontext[80];

static char cidinternalcontexts[MAX_NUM_CID_CONTEXTS][64];


static char *emailbody = NULL;
static int pbxskip = 0;
static char *emailsubject = NULL;
static char fromstring[100];
static char emailtitle[100];
static char charset[32] = "ISO-8859-1";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static void populate_defaults(struct ast_vm_user *vmu)
{
	vmu->attach = -1;
	if (reviewvm)
		vmu->review = 1;
	if (calloper)
		vmu->operator = 1;
	if (saycidinfo)
		vmu->saycid = 1;
	if (hearenv)
		vmu->envelope = 1;
	if (callcontext)
		strncpy(vmu->callback, callcontext, sizeof(vmu->callback) -1);
	if (dialcontext)
		strncpy(vmu->dialout, dialcontext, sizeof(vmu->dialout) -1);
	if (exitcontext)
		strncpy(vmu->exit, exitcontext, sizeof(vmu->exit) -1);
}

static void apply_options(struct ast_vm_user *vmu, char *options)
{
	/* Destructively Parse options and apply */
	char *stringp = ast_strdupa(options);
	char *s;
	char *var, *value;
	
	while((s = strsep(&stringp, "|"))) {
		value = s;
		if ((var = strsep(&value, "=")) && value) {
			if (!strcasecmp(var, "attach")) {
				if (ast_true(value))
					vmu->attach = 1;
				else
					vmu->attach = 0;
			} else if (!strcasecmp(var, "serveremail")) {
				strncpy(vmu->serveremail, value, sizeof(vmu->serveremail) - 1);
			} else if (!strcasecmp(var, "tz")) {
				strncpy(vmu->zonetag, value, sizeof(vmu->zonetag) - 1);
			} else if (!strcasecmp(var, "saycid")){
				if(ast_true(value))
					vmu->saycid = 1;
				else
					vmu->saycid = 0;
			} else if (!strcasecmp(var, "review")){
				if(ast_true(value))
					vmu->review = 1;
				else
					vmu->review = 0;
			} else if (!strcasecmp(var, "operator")){
				if(ast_true(value))
					vmu->operator = 1;
				else
					vmu->operator = 0;
			} else if (!strcasecmp(var, "envelope")){
				if(ast_true(value))
					vmu->envelope = 1;
				else
					vmu->envelope = 0;
			} else if (!strcasecmp(var, "callback")) {
				strncpy(vmu->callback, value, sizeof(vmu->callback) -1);
			} else if (!strcasecmp(var, "dialout")) {
				strncpy(vmu->dialout, value, sizeof(vmu->dialout) -1);
			} else if (!strcasecmp(var, "exitcontext")) {
				strncpy(vmu->exit, value, sizeof(vmu->exit) -1);

			}
		}
	}
	
}

#ifdef USEMYSQLVM
#include "mysql-vm-routines.h"
#endif

#ifdef USEPOSTGRESVM

PGconn *dbhandler;
char	dboption[256];
ast_mutex_t postgreslock;

static int sql_init(void)
{
	ast_verbose( VERBOSE_PREFIX_3 "Logging into postgres database: %s\n", dboption);
/*	fprintf(stderr,"Logging into postgres database: %s\n", dboption); */

	dbhandler=PQconnectdb(dboption);
	if (PQstatus(dbhandler) == CONNECTION_BAD) {
		ast_log(LOG_WARNING, "Error Logging into database %s: %s\n",dboption,PQerrorMessage(dbhandler));
		return(-1);
	}
	ast_mutex_init(&postgreslock);

/*	fprintf(stderr,"postgres login OK\n"); */
	return(0);
}

static void sql_close(void)
{
	PQfinish(dbhandler);
}


static struct ast_vm_user *find_user(struct ast_vm_user *ivm, char *context, char *mailbox)
{
	PGresult *PGSQLres;


	int numFields, i;
	char *fname;
	char query[240];
	char options[160] = "";
	struct ast_vm_user *retval;

	retval=malloc(sizeof(struct ast_vm_user));

/*	fprintf(stderr,"postgres find_user:\n"); */

	if (retval) {
		memset(retval, 0, sizeof(struct ast_vm_user));
		retval->alloced=1;
		if (mailbox) {
			strcpy(retval->mailbox, mailbox);
		}
		if (context) {
			strcpy(retval->context, context);
		}
		else
		{
			strcpy(retval->context, "default");
		}
		populate_defaults(retval);
		sprintf(query, "SELECT password,fullname,email,pager,options FROM voicemail WHERE context='%s' AND mailbox='%s'", retval->context, mailbox);
		
/*	fprintf(stderr,"postgres find_user: query = %s\n",query); */
		ast_mutex_lock(&postgreslock);
		PGSQLres=PQexec(dbhandler,query);
		if (PGSQLres!=NULL) {
			if (PQresultStatus(PGSQLres) == PGRES_BAD_RESPONSE ||
				PQresultStatus(PGSQLres) == PGRES_NONFATAL_ERROR ||
				PQresultStatus(PGSQLres) == PGRES_FATAL_ERROR) {

				ast_log(LOG_WARNING,"PGSQL_query: Query Error (%s) Calling PQreset\n",PQcmdStatus(PGSQLres));
				PQclear(PGSQLres);
				PQreset(dbhandler);
				ast_mutex_unlock(&postgreslock);
				free(retval);
				return(NULL);
			} else {
			numFields = PQnfields(PGSQLres);
/*	fprintf(stderr,"postgres find_user: query found %d rows with %d fields\n",PQntuples(PGSQLres), numFields); */
			if (PQntuples(PGSQLres) != 1) {
				ast_log(LOG_WARNING,"PGSQL_query: Did not find a unique mailbox for %s\n",mailbox);
				PQclear(PGSQLres);
				ast_mutex_unlock(&postgreslock);
				free(retval);
				return(NULL);
			}
			for (i=0; i<numFields; i++) {
				fname = PQfname(PGSQLres,i);
				if (!strcmp(fname, "password") && !PQgetisnull (PGSQLres,0,i)) {
					strncpy(retval->password, PQgetvalue(PGSQLres,0,i),sizeof(retval->password) - 1);
				} else if (!strcmp(fname, "fullname")) {
					strncpy(retval->fullname, PQgetvalue(PGSQLres,0,i),sizeof(retval->fullname) - 1);
				} else if (!strcmp(fname, "email")) {
					strncpy(retval->email, PQgetvalue(PGSQLres,0,i),sizeof(retval->email) - 1);
				} else if (!strcmp(fname, "pager")) {
					strncpy(retval->pager, PQgetvalue(PGSQLres,0,i),sizeof(retval->pager) - 1);
				} else if (!strcmp(fname, "options")) {
					strncpy(options, PQgetvalue(PGSQLres,0,i), sizeof(options) - 1);
					apply_options(retval, options);
				}
			}
			}
			PQclear(PGSQLres);
			ast_mutex_unlock(&postgreslock);
			return(retval);
		}
		else {
			ast_log(LOG_WARNING,"PGSQL_query: Connection Error (%s)\n",PQerrorMessage(dbhandler));
			ast_mutex_unlock(&postgreslock);
			free(retval);
			return(NULL);
		}
		/* not reached */
	} /* malloc() retval */
	return(NULL);
}


static void vm_change_password(struct ast_vm_user *vmu, char *password)
{
	char query[400];

	if (*vmu->context) {
		sprintf(query, "UPDATE voicemail SET password='%s' WHERE context='%s' AND mailbox='%s' AND (password='%s' OR password IS NULL)", password, vmu->context, vmu->mailbox, vmu->password);
	} else {
		sprintf(query, "UPDATE voicemail SET password='%s' WHERE mailbox='%s' AND (password='%s' OR password IS NULL)", password, vmu->mailbox, vmu->password);
	}
/*	fprintf(stderr,"postgres change_password: query = %s\n",query); */
	ast_mutex_lock(&postgreslock);
	PQexec(dbhandler, query);
	strcpy(vmu->password, password);
	ast_mutex_unlock(&postgreslock);
}

static void reset_user_pw(char *context, char *mailbox, char *password)
{
	char query[320];

	if (context) {
		sprintf(query, "UPDATE voicemail SET password='%s' WHERE context='%s' AND mailbox='%s'", password, context, mailbox);
	} else {
		sprintf(query, "UPDATE voicemail SET password='%s' WHERE mailbox='%s'", password, mailbox);
	}
	ast_mutex_lock(&postgreslock);
/*	fprintf(stderr,"postgres reset_user_pw: query = %s\n",query); */
	PQexec(dbhandler, query);
	ast_mutex_unlock(&postgreslock);
}

#endif	/* Postgres */

#ifndef USESQLVM

static struct ast_vm_user *find_user(struct ast_vm_user *ivm, char *context, char *mailbox)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *vmu=NULL, *cur;
	ast_mutex_lock(&vmlock);
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
	ast_mutex_unlock(&vmlock);
	return vmu;
}

static int reset_user_pw(char *context, char *mailbox, char *newpass)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *cur;
	int res = -1;
	ast_mutex_lock(&vmlock);
	cur = users;
	while(cur) {
		if ((!context || !strcasecmp(context, cur->context)) &&
			(!strcasecmp(mailbox, cur->mailbox)))
				break;
		cur=cur->next;
	}
	if (cur) {
		strncpy(cur->password, newpass, sizeof(cur->password) - 1);
		res = 0;
	}
	ast_mutex_unlock(&vmlock);
	return res;
}

static void vm_change_password(struct ast_vm_user *vmu, char *newpassword)
{
        /*  There's probably a better way of doing this. */
        /*  That's why I've put the password change in a separate function. */
		/*  This could also be done with a database function */
	
        FILE *configin;
        FILE *configout;
		int linenum=0;
		char inbuf[256];
		char orig[256];
		char currcontext[256] ="";
		char tmpin[AST_CONFIG_MAX_PATH];
		char tmpout[AST_CONFIG_MAX_PATH];
		char *user, *pass, *rest, *trim, *tempcontext;
		tempcontext = NULL;
		snprintf((char *)tmpin, sizeof(tmpin)-1, "%s/voicemail.conf",(char *)ast_config_AST_CONFIG_DIR);
		snprintf((char *)tmpout, sizeof(tmpout)-1, "%s/voicemail.conf.new",(char *)ast_config_AST_CONFIG_DIR);
        configin = fopen((char *)tmpin,"r");
		if (configin)
	        configout = fopen((char *)tmpout,"w+");
		else
			configout = NULL;
		if(!configin || !configout) {
			if (configin)
				fclose(configin);
			else
				ast_log(LOG_WARNING, "Warning: Unable to open '%s' for reading: %s\n", tmpin, strerror(errno));
			if (configout)
				fclose(configout);
			else
				ast_log(LOG_WARNING, "Warning: Unable to open '%s' for writing: %s\n", tmpout, strerror(errno));
			return;
		}

        while (!feof(configin)) {
			/* Read in the line */
			fgets(inbuf, sizeof(inbuf), configin);
			linenum++;
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
				/* check for '[' (opening of context name ) */
				tempcontext = strchr(user, '[');
				if (tempcontext) {
					strncpy(currcontext, tempcontext +1,
						 sizeof(currcontext) - 1);
					/* now check for ']' */
					tempcontext = strchr(currcontext, ']');
					if (tempcontext) 
						*tempcontext = '\0';
					else
						currcontext[0] = '\0';
				}
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
				
				/* Compare user, pass AND context */
				if (user && *user && !strcmp(user, vmu->mailbox) &&
					 pass && *pass && !strcmp(pass, vmu->password) &&
					 currcontext && *currcontext && !strcmp(currcontext, vmu->context)) {
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
	reset_user_pw(vmu->context, vmu->mailbox, newpassword);
	strncpy(vmu->password, newpassword, sizeof(vmu->password) - 1);
}
#endif

static int make_dir(char *dest, int len, char *context, char *ext, char *mailbox)
{
	return snprintf(dest, len, "%s/voicemail/%s/%s/%s", (char *)ast_config_AST_SPOOL_DIR,context, ext, mailbox);
}

static int make_file(char *dest, int len, char *dir, int num)
{
	return snprintf(dest, len, "%s/msg%04d", dir, num);
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

static void prep_email_sub_vars(struct ast_channel *ast, struct ast_vm_user *vmu, int msgnum, char *mailbox, char *callerid, char *dur, char *date, char *passdata)
{
	/* Prepare variables for substition in email body and subject */
	pbx_builtin_setvar_helper(ast, "VM_NAME", vmu->fullname);
	pbx_builtin_setvar_helper(ast, "VM_DUR", dur);
	sprintf(passdata,"%d",msgnum);
	pbx_builtin_setvar_helper(ast, "VM_MSGNUM", passdata);
	pbx_builtin_setvar_helper(ast, "VM_MAILBOX", mailbox);
	pbx_builtin_setvar_helper(ast, "VM_CALLERID", (callerid ? callerid : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_DATE", date);
}

static int sendmail(char *srcemail, struct ast_vm_user *vmu, int msgnum, char *mailbox, char *callerid, char *attach, char *format, int duration, int attach_user_voicemail)
{
	FILE *p=NULL;
	int pfd;
	char date[256];
	char host[256];
	char who[256];
	char bound[256];
	char fname[256];
	char dur[256];
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[256];
	time_t t;
	struct tm tm;
	struct vm_zone *the_zone = NULL;

	if (!strcmp(format, "wav49"))
		format = "WAV";
	ast_log(LOG_DEBUG, "Attaching file '%s', format '%s', uservm is '%d', global is %d\n", attach, format, attach_user_voicemail, attach_voicemail);
	/* Make a temporary file instead of piping directly to sendmail, in case the mail
	   command hangs */
	pfd = mkstemp(tmp);
	if (pfd > -1) {
		p = fdopen(pfd, "w");
		if (!p) {
			close(pfd);
			pfd = -1;
		}
	}
	if (p) {
		gethostname(host, sizeof(host));
		if (strchr(srcemail, '@'))
			strncpy(who, srcemail, sizeof(who)-1);
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
		time(&t);

		/* Does this user have a timezone specified? */
		if (strlen(vmu->zonetag)) {
			/* Find the zone in the list */
			struct vm_zone *z;
			z = zones;
			while (z) {
				if (!strcmp(z->name, vmu->zonetag)) {
					the_zone = z;
					break;
				}
				z = z->next;
			}
		}

		if (the_zone)
			ast_localtime(&t,&tm,the_zone->timezone);
		else
			ast_localtime(&t,&tm,NULL);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
		fprintf(p, "Date: %s\n", date);

		if (*fromstring) {
			struct ast_channel *ast = ast_channel_alloc(0);
			if (ast) {
				char *passdata;
				int vmlen = strlen(fromstring)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast,vmu,msgnum + 1,mailbox,callerid,dur,date,passdata);
					pbx_substitute_variables_helper(ast,fromstring,passdata,vmlen);
					fprintf(p, "From: %s <%s>\n",passdata,who);
				} else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else
			fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s <%s>\n", vmu->fullname, vmu->email);

		if (emailsubject) {
			struct ast_channel *ast = ast_channel_alloc(0);
			if (ast) {
				char *passdata;
				int vmlen = strlen(emailsubject)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast,vmu,msgnum + 1,mailbox,callerid,dur,date,passdata);
					pbx_substitute_variables_helper(ast,emailsubject,passdata,vmlen);
					fprintf(p, "Subject: %s\n",passdata);
				} else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else
		if( *emailtitle)
		{
			fprintf(p, emailtitle, msgnum + 1, mailbox) ;
			fprintf(p,"\n") ;
		}
		else
		if (pbxskip)
			fprintf(p, "Subject: New message %d in mailbox %s\n", msgnum + 1, mailbox);
		else
			fprintf(p, "Subject: [PBX]: New message %d in mailbox %s\n", msgnum + 1, mailbox);
		fprintf(p, "Message-ID: <Asterisk-%d-%s-%d@%s>\n", msgnum, mailbox, getpid(), host);
		fprintf(p, "MIME-Version: 1.0\n");
		if (attach_user_voicemail) {
			/* Something unique. */
			snprintf(bound, sizeof(bound), "voicemail_%d%s%d", msgnum, mailbox, getpid());

			fprintf(p, "Content-Type: multipart/mixed; boundary=\"%s\"\n\n\n", bound);

			fprintf(p, "--%s\n", bound);
		}
		fprintf(p, "Content-Type: text/plain; charset=%s\nContent-Transfer-Encoding: 8bit\n\n", charset);
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", &tm);
		if (emailbody) {
			struct ast_channel *ast = ast_channel_alloc(0);
			if (ast) {
				char *passdata;
				int vmlen = strlen(emailbody)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast,vmu,msgnum + 1,mailbox,callerid,dur,date,passdata);
					pbx_substitute_variables_helper(ast,emailbody,passdata,vmlen);
					fprintf(p, "%s\n",passdata);
				} else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else {
			fprintf(p, "Dear %s:\n\n\tJust wanted to let you know you were just left a %s long message (number %d)\n"

			"in mailbox %s from %s, on %s so you might\n"
			"want to check it when you get a chance.  Thanks!\n\n\t\t\t\t--Asterisk\n\n", vmu->fullname, 
			dur, msgnum + 1, mailbox, (callerid ? callerid : "an unknown caller"), date);
		}
		if (attach_user_voicemail) {
			fprintf(p, "--%s\n", bound);
			fprintf(p, "Content-Type: audio/x-%s; name=\"msg%04d.%s\"\n", format, msgnum, format);
			fprintf(p, "Content-Transfer-Encoding: base64\n");
			fprintf(p, "Content-Description: Voicemail sound attachment.\n");
			fprintf(p, "Content-Disposition: attachment; filename=\"msg%04d.%s\"\n\n", msgnum, format);

			snprintf(fname, sizeof(fname), "%s.%s", attach, format);
			base_encode(fname, p);
			fprintf(p, "\n\n--%s--\n.\n", bound);
		}
		fclose(p);
		snprintf(tmp2, sizeof(tmp2), "( %s < %s ; rm -f %s ) &", mailcmd, tmp, tmp);
		ast_safe_system(tmp2);
		ast_log(LOG_DEBUG, "Sent mail to %s with command '%s'\n", who, mailcmd);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", mailcmd);
		return -1;
	}
	return 0;
}

static int sendpage(char *srcemail, char *pager, int msgnum, char *mailbox, char *callerid, int duration, struct ast_vm_user *vmu)
{
	FILE *p=NULL;
	int pfd;
	char date[256];
	char host[256];
	char who[256];
	char dur[256];
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[256];
	time_t t;
	struct tm tm;
	struct vm_zone *the_zone = NULL;
	pfd = mkstemp(tmp);

	if (pfd > -1) {
		p = fdopen(pfd, "w");
		if (!p) {
			close(pfd);
			pfd = -1;
		}
	}

	if (p) {
		gethostname(host, sizeof(host));
		if (strchr(srcemail, '@'))
			strncpy(who, srcemail, sizeof(who)-1);
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
		time(&t);

		/* Does this user have a timezone specified? */
		if (strlen(vmu->zonetag)) {
			/* Find the zone in the list */
			struct vm_zone *z;
			z = zones;
			while (z) {
				if (!strcmp(z->name, vmu->zonetag)) {
					the_zone = z;
					break;
				}
				z = z->next;
			}
		}

		if (the_zone)
			ast_localtime(&t,&tm,the_zone->timezone);
		else
			ast_localtime(&t,&tm,NULL);

		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);
		fprintf(p, "Date: %s\n", date);
		fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s\n", pager);
		fprintf(p, "Subject: New VM\n\n");
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", &tm);
		fprintf(p, "New %s long msg in box %s\n"
		           "from %s, on %s", dur, mailbox, (callerid ? callerid : "unknown"), date);
		fclose(p);
		snprintf(tmp2, sizeof(tmp2), "( %s < %s ; rm -f %s ) &", mailcmd, tmp, tmp);
		ast_safe_system(tmp2);
		ast_log(LOG_DEBUG, "Sent mail to %s with command '%s'\n", who, mailcmd);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", mailcmd);
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
	ast_stopstream(chan);
	return d;
}

static int play_and_prepend(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int *duration, int beep)
{
	char d = 0, *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct ast_frame *f;
	struct ast_filestream *others[MAX_OTHER_FORMATS];
	struct ast_filestream *realfiles[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	struct ast_dsp *sildet;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
	int rfmt=0;	
	char prependfile[80];
	
	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_and_prepend called without duration pointer\n");
		return -1;
	}

	ast_log(LOG_DEBUG,"play_and_prepend: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile || beep) {	
		if (!beep)
			d = play_and_wait(chan, playfile);
		if (d > -1)
			d = ast_streamfile(chan, "beep",chan->language);
		if (!d)
			d = ast_waitstream(chan,"");
		if (d < 0)
			return -1;
	}
	strncpy(prependfile, recordfile, sizeof(prependfile) -1);	
	strcat(prependfile, "-prepend");
			
	fmts = ast_strdupa(fmt);
	
	stringp=fmts;
	strsep(&stringp, "|");
	ast_log(LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);	
	sfmt[0] = ast_strdupa(fmts);
	
	while((fmt = strsep(&stringp, "|"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = ast_strdupa(fmt);
	}

	time(&start);
	end=start;  /* pre-initialize end to be same as start in case we never get into loop */
	for (x=0;x<fmtcnt;x++) {
		others[x] = ast_writefile(prependfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		ast_verbose( VERBOSE_PREFIX_3 "x=%i, open writing:  %s format: %s, %p\n", x, prependfile, sfmt[x], others[x]);
		if (!others[x]) {
			break;
		}
	}
	
	sildet = ast_dsp_new(); /* Create the silence detector */
	if (!sildet) {
		ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	ast_dsp_set_threshold(sildet, silencethreshold);

	if (maxsilence > 0) {
		rfmt = chan->readformat;
		res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			return -1;
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
					if (!others[x])
						break;
					res = ast_writestream(others[x], f);
				}
				
				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					ast_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence)
						totalsilence = dspsilence;
					else
						totalsilence = 0;
					
					if (totalsilence > maxsilence) {
					/* Ended happily with silence */
					ast_frfree(f);
					gotsilence = 1;
					outmsg=2;
					break;
					}
				}
				/* Exit on any error */
				if (res) {
					ast_log(LOG_WARNING, "Error writing frame\n");
					ast_frfree(f);
					break;
				}
			} else if (f->frametype == AST_FRAME_VIDEO) {
				/* Write only once */
				ast_writestream(others[0], f);
			} else if (f->frametype == AST_FRAME_DTMF) {
				/* stop recording with any digit */
				if (option_verbose > 2) 
					ast_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
				res = 't';
				outmsg = 2;
				ast_frfree(f);
				break;
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
		if (end == start) time(&end);
		if (!f) {
			if (option_verbose > 2) 
				ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
#if 0
			/* delete all the prepend files */
			for (x=0;x<fmtcnt;x++) {
				if (!others[x])
					break;
				ast_closestream(others[x]);
				ast_filedelete(prependfile, sfmt[x]);
			}
#endif
		}
	} else {
		ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", prependfile, sfmt[x]); 
	}
	*duration = end - start;
#if 0
	if (outmsg > 1) {
#else
	if (outmsg) {
#endif
		struct ast_frame *fr;
		for (x=0;x<fmtcnt;x++) {
			snprintf(comment, sizeof(comment), "Opening the real file %s.%s\n", recordfile, sfmt[x]);
			realfiles[x] = ast_readfile(recordfile, sfmt[x], comment, O_RDONLY, 0, 0);
			if (!others[x] || !realfiles[x])
				break;
			if (totalsilence)
				ast_stream_rewind(others[x], totalsilence-200);
			else
				ast_stream_rewind(others[x], 200);
			ast_truncstream(others[x]);
			/* add the original file too */
			while ((fr = ast_readframe(realfiles[x]))) {
				ast_writestream(others[x],fr);
			}
			ast_closestream(others[x]);
			ast_closestream(realfiles[x]);
			ast_filerename(prependfile, recordfile, sfmt[x]);
#if 0
			ast_verbose("Recording Format: sfmts=%s, prependfile %s, recordfile %s\n", sfmt[x],prependfile,recordfile);
#endif
			ast_filedelete(prependfile, sfmt[x]);
		}
	}
	if (rfmt) {
		if (ast_set_read_format(chan, rfmt)) {
			ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(rfmt), chan->name);
		}
	}
	if (outmsg) {
		if (outmsg > 1) {
			/* Let them know it worked */
			ast_streamfile(chan, "auth-thankyou", chan->language);
			ast_waitstream(chan, "");
		}
	}	
	return res;
}

static int play_and_record(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int *duration)
{
	char d, *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct ast_frame *f;
	struct ast_filestream *others[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	struct ast_dsp *sildet;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
	int rfmt=0;

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_and_record called without duration pointer\n");
		return -1;
	}

	ast_log(LOG_DEBUG,"play_and_record: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile) {
		d = play_and_wait(chan, playfile);
		if (d > -1)
			d = ast_streamfile(chan, "beep",chan->language);
		if (!d)
			d = ast_waitstream(chan,"");
		if (d < 0)
			return -1;
	}

	fmts = ast_strdupa(fmt);

	stringp=fmts;
	strsep(&stringp, "|");
	ast_log(LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);
	sfmt[0] = ast_strdupa(fmts);

	while((fmt = strsep(&stringp, "|"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = ast_strdupa(fmt);
	}

	time(&start);
	end=start;  /* pre-initialize end to be same as start in case we never get into loop */
	for (x=0;x<fmtcnt;x++) {
		others[x] = ast_writefile(recordfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		ast_verbose( VERBOSE_PREFIX_3 "x=%i, open writing:  %s format: %s, %p\n", x, recordfile, sfmt[x], others[x]);

		if (!others[x]) {
			break;
		}
	}

	sildet = ast_dsp_new(); /* Create the silence detector */
	if (!sildet) {
		ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	ast_dsp_set_threshold(sildet, silencethreshold);
	
	if (maxsilence > 0) {
		rfmt = chan->readformat;
		res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			return -1;
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

				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					ast_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence)
						totalsilence = dspsilence;
					else
						totalsilence = 0;

					if (totalsilence > maxsilence) {
					/* Ended happily with silence */
					ast_frfree(f);
					gotsilence = 1;
					outmsg=2;
					break;
					}
				}
				/* Exit on any error */
				if (res) {
					ast_log(LOG_WARNING, "Error writing frame\n");
					ast_frfree(f);
					break;
				}
			} else if (f->frametype == AST_FRAME_VIDEO) {
				/* Write only once */
				ast_writestream(others[0], f);
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
				if (f->subclass == '0') {
				/* Check for a '0' during message recording also, in case caller wants operator */
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "User cancelled by pressing %c\n", f->subclass);
					res = '0';
					outmsg = 0;
					ast_frfree(f);
					break;
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
		if (end == start) time(&end);
		if (!f) {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
		}
	} else {
		ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", recordfile, sfmt[x]);
	}

	*duration = end - start;

	for (x=0;x<fmtcnt;x++) {
		if (!others[x])
			break;
		if (totalsilence)
			ast_stream_rewind(others[x], totalsilence-200);
		else
			ast_stream_rewind(others[x], 200);
		ast_truncstream(others[x]);
		ast_closestream(others[x]);
	}
	if (rfmt) {
		if (ast_set_read_format(chan, rfmt)) {
			ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(rfmt), chan->name);
		}
	}
	if (outmsg) {
		if (outmsg > 1) {
		/* Let them know recording is stopped */
			ast_streamfile(chan, "auth-thankyou", chan->language);
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

static void free_zone(struct vm_zone *z)
{
	free(z);
}

static void run_externnotify(char *context, char *extension, int numvoicemails)
{
	char arguments[255];

	if(externnotify[0]) {
		strncpy(arguments, externnotify, sizeof(arguments));
		snprintf(arguments, sizeof(arguments)-1, "%s %s %s %d&", externnotify, context, extension, numvoicemails);
		ast_log(LOG_DEBUG,"Executing %s\n", arguments);
		ast_safe_system(arguments);
	}
}


static int leave_voicemail(struct ast_channel *chan, char *ext, int silent, int busy, int unavail)
{
	char comment[256];
	char txtfile[256];
	FILE *txt;
	int res = 0;
	int msgnum;
	int fd;
	int duration = 0;
	char date[256];
	char dir[256];
	char fn[256];
	char prefile[256]="";
	char ext_context[256] = "";
	char fmt[80];
	char *context;
	char *ecodes = "#";
	char *stringp;
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
		if (strcmp(vmu->context, "default"))
			snprintf(ext_context, sizeof(ext_context), "%s@%s", ext, vmu->context);
		else
			strncpy(ext_context, vmu->context, sizeof(ext_context) - 1);
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
			ecodes = "#0*";
		/* Play the beginning intro if desired */
		if (strlen(prefile)) {
			if (ast_fileexists(prefile, NULL, NULL) > 0) {
				if (ast_streamfile(chan, prefile, chan->language) > -1) 
				    res = ast_waitstream(chan, "#0*");
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
		if (res > 0)
			ast_stopstream(chan);
		/* Check for a '*' here in case the caller wants to escape from voicemail to something
		other than the operator -- an automated attendant or mailbox login for example */
		if (res == '*') {
			strncpy(chan->exten, "a", sizeof(chan->exten) - 1);
			if (strlen(vmu->exit)) {
				strncpy(chan->context, vmu->exit, sizeof(chan->context) - 1);
			} else if (strlen(chan->macrocontext)) {
				strncpy(chan->context, chan->macrocontext, sizeof(chan->context) - 1);
			}
			chan->priority = 0;
			free_user(vmu);
			return 0;
		}
		/* Check for a '0' here */
		if (res == '0') {
		transfer:
			strncpy(chan->exten, "o", sizeof(chan->exten) - 1);
			if (strlen(vmu->exit)) {
				strncpy(chan->context, vmu->exit, sizeof(chan->context) - 1);
			} else if (strlen(chan->macrocontext)) {
				strncpy(chan->context, chan->macrocontext, sizeof(chan->context) - 1);
			}
			chan->priority = 0;
			free_user(vmu);
			return 0;
		}
		if (res >= 0) {
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
					fprintf(txt, 
";\n"
"; Message Information file\n"
";\n"
"[message]\n"
"origmailbox=%s\n"
"context=%s\n"
"macrocontext=%s\n"
"exten=%s\n"
"priority=%d\n"
"callerchan=%s\n"
"callerid=%s\n"
"origdate=%s\n"
"origtime=%ld\n",
	ext,
	chan->context,
	chan->macrocontext, 
	chan->exten,
	chan->priority,
	chan->name,
	chan->callerid ? chan->callerid : "Unknown",
	date, (long)time(NULL));
					fclose(txt);
				} else
					ast_log(LOG_WARNING, "Error opening text file for output\n");
				res = play_record_review(chan, NULL, fn, vmmaxmessage, fmt, 1, vmu, &duration);
				if (res == '0')
					goto transfer;
				if (res > 0)
					res = 0;
				fd = open(txtfile, O_APPEND | O_WRONLY);
				if (fd > -1) {
					txt = fdopen(fd, "a");
					if (txt) {
						fprintf(txt, "duration=%d\n", duration);
						fclose(txt);
					} else
						close(fd);
				}
				if (duration < vmminmessage) {
					vm_delete(fn);
					goto leave_vm_out;
				}
				stringp = fmt;
				strsep(&stringp, "|");
				/* Send e-mail if applicable */
				if (strlen(vmu->email)) {
					int attach_user_voicemail = attach_voicemail;
					char *myserveremail = serveremail;
					if (vmu->attach > -1)
						attach_user_voicemail = vmu->attach;
					if (strlen(vmu->serveremail))
						myserveremail = vmu->serveremail;
						sendmail(myserveremail, vmu, msgnum, ext, chan->callerid, fn, fmt, duration, attach_user_voicemail);
				}
				if (strlen(vmu->pager)) {
					char *myserveremail = serveremail;
					if (strlen(vmu->serveremail))
						myserveremail = vmu->serveremail;
					sendpage(myserveremail, vmu->pager, msgnum, ext, chan->callerid, duration, vmu);
				}
			} else {
				res = ast_streamfile(chan, "vm-mailboxfull", chan->language);
				if (!res)
					res = ast_waitstream(chan, "");
				ast_log(LOG_WARNING, "No more messages possible\n");
			}
		} else
			ast_log(LOG_WARNING, "No format for saving voicemail?\n");					
leave_vm_out:
		free_user(vmu);
	} else {
		ast_log(LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
			/*Send the call to n+101 priority, where n is the current priority*/
			if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
				chan->priority+=100;
	}
	/* Leave voicemail for someone */
	manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext_context, ast_app_has_voicemail(ext_context));

	/* If an external program is specified to be run after leaving a voicemail */
	run_externnotify(chan->context, ext_context, ast_app_has_voicemail(ext_context));

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
	d = ast_say_number(chan, num, AST_DIGIT_ANY, chan->language, (char *) NULL);
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
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 3, "Options", "Options", "0", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 4, "Help", "Help", "*", 1);
	bytes += adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 5, "Exit", "Exit", "#", 1);
 	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ...", "");
      bytes += adsi_voice_mode(buf + bytes, 0);

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
	bytes += adsi_voice_mode(buf + bytes, 0);
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
	bytes += adsi_voice_mode(buf + bytes, 0);
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
	bytes += adsi_voice_mode(buf + bytes, 0);

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
      bytes += adsi_voice_mode(buf + bytes, 0);

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
	bytes += adsi_voice_mode(buf + bytes, 0);

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
	bytes += adsi_voice_mode(buf + bytes, 0);

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

	bytes += adsi_voice_mode(buf + bytes, 0);

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

	bytes += adsi_voice_mode(buf + bytes, 0);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	
}

/*
static void adsi_clear(struct ast_channel *chan)
{
	char buf[256];
	int bytes=0;
	if (!adsi_available(chan))
		return;
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_voice_mode(buf + bytes, 0);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}
*/

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
	bytes += adsi_voice_mode(buf + bytes, 0);

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
		if ((d = ast_say_number(chan, x, AST_DIGIT_ANY, chan->language, (char *) NULL)))
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
			(res != '#') && (res >= 0)) {
		res = get_folder(chan, 0);
	}
	return res;
}

static int vm_forwardoptions(struct ast_channel *chan, struct ast_vm_user *vmu, char *curdir, int curmsg, char *vmfts, char *context)
{
	int cmd = 0;
	int retries = 0;
	int duration = 0;

	while((cmd >= 0) && (cmd != 't') && (cmd != '*')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1': 
			/* prepend a message to the current message and return */
		{
			char file[200];
			snprintf(file, sizeof(file), "%s/msg%04d", curdir, curmsg);
			cmd = play_and_prepend(chan, NULL, file, 0, vmfmts, &duration, 1);
			break;
		}
		case '2': 
			cmd = 't';
			break;
		case '*':
			cmd = '*';
			break;
		default: 
			cmd = play_and_wait(chan,"vm-forwardoptions");
			if (!cmd)
				cmd = play_and_wait(chan,"vm-starmain");
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

static int forward_message(struct ast_channel *chan, char *context, char *dir, int curmsg, struct ast_vm_user *sender, char *fmt)
{
	char username[70];
	char sys[256];
	char todir[256];
	int todircount=0;
	int duration;
	struct ast_config *mif;
	char miffile[256];
	char fn[256];
	char callerid[512];
	char ext_context[256]="";
	int res = 0, cmd = 0;
	struct ast_vm_user *receiver, *extensions = NULL, *vmtmp = NULL, *vmfree;
	char tmp[256];
	char *stringp, *s;
	int saved_messages = 0, found = 0;
	int valid_extensions = 0;
	while (!res && !valid_extensions) {
		res = ast_streamfile(chan, "vm-extension", chan->language);
		if (res)
			break;
		if ((res = ast_readstring(chan, username, sizeof(username) - 1, 2000, 10000, "#") < 0))
			break;
		/* start all over if no username */
		if (!strlen(username))
			continue;
		stringp = username;
		s = strsep(&stringp, "*");
		/* start optimistic */
		valid_extensions = 1;
		while (s) {
			/* find_user is going to malloc since we have a NULL as first argument */
			if ((receiver = find_user(NULL, context, s))) {
				if (!extensions)
					vmtmp = extensions = receiver;
				else {
					vmtmp->next = receiver;
					vmtmp = receiver;
				}
				found++;
			} else {
				valid_extensions = 0;
				break;
			}
			s = strsep(&stringp, "*");
		}
		/* break from the loop of reading the extensions */
		if (valid_extensions)
			break;
		/* invalid extension, try again */
		res = play_and_wait(chan, "pbx-invalid");
	}
	/* check if we're clear to proceed */
	if (!extensions || !valid_extensions)
		return res;
	vmtmp = extensions;
	cmd = vm_forwardoptions(chan, sender, dir, curmsg, vmfmts, context);
	if (!cmd) {
		while(!res && vmtmp) {
			/* if (play_and_wait(chan, "vm-savedto"))
				break;
			*/
			snprintf(todir, sizeof(todir), "%s/voicemail/%s/%s/INBOX",  (char *)ast_config_AST_SPOOL_DIR, vmtmp->context, vmtmp->mailbox);
			snprintf(sys, sizeof(sys), "mkdir -p %s\n", todir);
			snprintf(ext_context, sizeof(ext_context), "%s@%s", vmtmp->mailbox, vmtmp->context);
			ast_log(LOG_DEBUG, sys);
			ast_safe_system(sys);
	
			todircount = count_messages(todir);
			strncpy(tmp, fmt, sizeof(tmp) - 1);
			stringp = tmp;
			while((s = strsep(&stringp, "|"))) {
				/* XXX This is a hack -- we should use build_filename or similar XXX */
				if (!strcasecmp(s, "wav49"))
					s = "WAV";
				snprintf(sys, sizeof(sys), "cp %s/msg%04d.%s %s/msg%04d.%s\n", dir, curmsg, s, todir, todircount, s);
				ast_log(LOG_DEBUG, sys);
				ast_safe_system(sys);
			}
			snprintf(sys, sizeof(sys), "cp %s/msg%04d.txt %s/msg%04d.txt\n", dir, curmsg, todir, todircount);
			ast_log(LOG_DEBUG, sys);
			ast_safe_system(sys);
			snprintf(fn, sizeof(fn), "%s/msg%04d", todir,todircount);
	
			/* load the information on the source message so we can send an e-mail like a new message */
			snprintf(miffile, sizeof(miffile), "%s/msg%04d.txt", dir, curmsg);
			if ((mif=ast_load(miffile))) {
	
				/* set callerid and duration variables */
				snprintf(callerid, sizeof(callerid), "FWD from: %s from %s", sender->fullname, ast_variable_retrieve(mif, NULL, "callerid"));
				s = ast_variable_retrieve(mif, NULL, "duration");
				if (s)
					duration = atoi(s);
				else
					duration = 0;
				if (strlen(vmtmp->email)) {
					int attach_user_voicemail = attach_voicemail;
					char *myserveremail = serveremail;
					if (vmtmp->attach > -1)
						attach_user_voicemail = vmtmp->attach;
					if (strlen(vmtmp->serveremail))
						myserveremail = vmtmp->serveremail;
					sendmail(myserveremail, vmtmp, todircount, vmtmp->mailbox, callerid, fn, tmp, duration, attach_user_voicemail);
				}
			     
				if (strlen(vmtmp->pager)) {
					char *myserveremail = serveremail;
					if (strlen(vmtmp->serveremail))
						myserveremail = vmtmp->serveremail;
					sendpage(myserveremail, vmtmp->pager, todircount, vmtmp->mailbox, callerid, duration, vmtmp);
				}
				  
				ast_destroy(mif); /* or here */
			}
			/* Leave voicemail for someone */
			manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext_context, ast_app_has_voicemail(ext_context));
			run_externnotify(chan->context, ext_context, ast_app_has_voicemail(ext_context));
	
			saved_messages++;
			vmfree = vmtmp;
			vmtmp = vmtmp->next;
			free_user(vmfree);
		}
		if (saved_messages > 0) {
			/* give confirmatopm that the message was saved */
			/* commented out since we can't forward batches yet
			if (saved_messages == 1)
				res = play_and_wait(chan, "vm-message");
			else
				res = play_and_wait(chan, "vm-messages");
			if (!res)
				res = play_and_wait(chan, "vm-saved"); */
			if (!res)
				res = play_and_wait(chan, "vm-msgsaved");
		}
	}
	return res ? res : cmd;
}


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
	return ast_control_streamfile(chan, file, "#", "*", "14679", "0", skipms);
}

static int play_message_datetime(struct ast_channel *chan, struct ast_vm_user *vmu, char *origtime, char *filename)
{
	int res = 0;
	struct vm_zone *the_zone = NULL;
	time_t t;
	long tin;

	if (sscanf(origtime,"%ld",&tin) < 1) {
		ast_log(LOG_WARNING, "Couldn't find origtime in %s\n", filename);
		return 0;
	}
	t = tin;

	/* Does this user have a timezone specified? */
	if (strlen(vmu->zonetag)) {
		/* Find the zone in the list */
		struct vm_zone *z;
		z = zones;
		while (z) {
			if (!strcmp(z->name, vmu->zonetag)) {
				the_zone = z;
				break;
			}
			z = z->next;
		}
	}

/* No internal variable parsing for now, so we'll comment it out for the time being */
#if 0
	/* Set the DIFF_* variables */
	localtime_r(&t, &time_now);
	gettimeofday(&tv_now,NULL);
	tnow = tv_now.tv_sec;
	localtime_r(&tnow,&time_then);

	/* Day difference */
	if (time_now.tm_year == time_then.tm_year)
		sprintf(temp,"%d",time_now.tm_yday);
	else
		sprintf(temp,"%d",(time_now.tm_year - time_then.tm_year) * 365 + (time_now.tm_yday - time_then.tm_yday));
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", temp);

	/* Can't think of how other diffs might be helpful, but I'm sure somebody will think of something. */
#endif
	if (the_zone)
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, the_zone->msg_format, the_zone->timezone);
	else
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q 'digits/at' IMp", NULL);
#if 0
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", NULL);
#endif
	return res;
}



static int play_message_callerid(struct ast_channel *chan, struct vm_state *vms, char *cid, char *context, int callback)
{
	int res = 0;
	int i;
	char *callerid, *name;
	char prefile[256]="";
	

	/* If voicemail cid is not enabled, or we didn't get cid or context from the attribute file, leave now. */
	/* BB: Still need to change this so that if this function is called by the message envelope (and someone is explicitly requesting to hear the CID), it does not check to see if CID is enabled in the config file */
	if((cid == NULL)||(context == NULL))
		return res;

	/* Strip off caller ID number from name */
	ast_log(LOG_DEBUG, "VM-CID: composite caller ID received: %s, context: %s\n", cid, context);
	ast_callerid_parse(cid, &name, &callerid);
	if((callerid != NULL)&&(!res)&&(strlen(callerid))){
		/* Check for internal contexts and only */
		/* say extension when the call didn't come from an internal context in the list */
		for(i = 0 ; i < MAX_NUM_CID_CONTEXTS ; i++){
			ast_log(LOG_DEBUG, "VM-CID: comparing internalcontext: %s\n", cidinternalcontexts[i]);
			if((strcmp(cidinternalcontexts[i], context) == 0))
				break;
		}
		if(i != MAX_NUM_CID_CONTEXTS){ /* internal context? */
			if(!res) {
				snprintf(prefile, sizeof(prefile), "voicemail/%s/%s/greet", context, callerid);
				if (strlen(prefile)) {
				/* See if we can find a recorded name for this person instead of their extension number */
					if (ast_fileexists(prefile, NULL, NULL) > 0) {
						ast_verbose(VERBOSE_PREFIX_3 "Playing envelope info: CID number '%s' matches mailbox number, playing recorded name\n", callerid);
						if (!callback)
							res = wait_file2(chan, vms, "vm-from");
						res = ast_streamfile(chan, prefile, chan->language) > -1;
						res = ast_waitstream(chan, "");
					} else {
						ast_verbose(VERBOSE_PREFIX_3 "Playing envelope info: message from '%s'\n", callerid);
						/* BB: Say "from extension" as one saying to sound smoother */
						if (!callback)
							res = wait_file2(chan, vms, "vm-from-extension");
						res = ast_say_digit_str(chan, callerid, "", chan->language);
					}
				}
			}
		}

		else if (!res){
			ast_log(LOG_DEBUG, "VM-CID: Numeric caller id: (%s)\n",callerid);
			/* BB: Since this is all nicely figured out, why not say "from phone number" in this case" */
			if (!callback)
				res = wait_file2(chan, vms, "vm-from-phonenumber");
			res = ast_say_digit_str(chan, callerid, AST_DIGIT_ANY, chan->language);
		}
	}
	else{
		/* Number unknown */
		ast_log(LOG_DEBUG, "VM-CID: From an unknown number");
		if(!res)
			/* BB: Say "from an unknown caller" as one phrase - it is already recorded by "the voice" anyhow */
			res = wait_file2(chan, vms, "vm-unknown-caller");
	}
	return res;                                                               
}

static int play_message(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg)
{
	int res = 0;
	char filename[256],*origtime, *cid, *context;
	struct ast_config *msg_cfg;

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
				res = ast_say_number(chan, msg + 1, AST_DIGIT_ANY, chan->language, (char *) NULL);
		}
	}

	/* Retrieve info from VM attribute file */

        make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg);
        snprintf(filename,sizeof(filename), "%s.txt", vms->fn2);
        msg_cfg = ast_load(filename);
        if (!msg_cfg) {
                ast_log(LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
                return 0;
        }
                                                                                                                                 
        if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime")))
                return 0;
                                                                                                                                 
        cid = ast_variable_retrieve(msg_cfg, "message", "callerid");

        context = ast_variable_retrieve(msg_cfg, "message", "context");
	if(!strncasecmp("macro",context,5)) /* Macro names in contexts are useless for our needs */
		context = ast_variable_retrieve(msg_cfg, "message","macrocontext");

	if ((!res)&&(vmu->envelope))
		res = play_message_datetime(chan, vmu, origtime, filename);
	if ((!res)&&(vmu->saycid))
		res = play_message_callerid(chan, vms, cid, context, 0);
	/* Allow pressing '1' to skip envelope / callerid */
	if (res == '1')
		res = 0;
	ast_destroy(msg_cfg);

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
		for (x=0;x < MAXMSG;x++) { 
			if (!vms->deleted[x] && (strcasecmp(vms->curbox, "INBOX") || !vms->heard[x])) { 
				/* Save this message.  It's not in INBOX or hasn't been heard */ 
				make_file(vms->fn, sizeof(vms->fn), vms->curdir, x); 
				if (ast_fileexists(vms->fn, NULL, NULL) < 1) 
					break;
				vms->curmsg++; 
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
		for (x = vms->curmsg + 1; x <= MAXMSG; x++) { 
			make_file(vms->fn, sizeof(vms->fn), vms->curdir, x); 
			if (ast_fileexists(vms->fn, NULL, NULL) < 1) 
				break;
			vm_delete(vms->fn);
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
				res = play_and_wait(chan, "vm-advopts");
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
	int duration = 0;
	char newpassword[80] = "";
	char newpassword2[80] = "";
	char prefile[256]="";
	char buf[256];
	int bytes=0;

	if (adsi_available(chan))
	{
		bytes += adsi_logo(buf + bytes);
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Options Menu", "");
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += adsi_voice_mode(buf + bytes, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}
	while((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1':
			snprintf(prefile,sizeof(prefile),"voicemail/%s/%s/unavail",vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-unv",prefile, maxgreet, fmtc, 0, vmu, &duration);
			break;
		case '2': 
			snprintf(prefile,sizeof(prefile),"voicemail/%s/%s/busy",vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-busy",prefile, maxgreet, fmtc, 0, vmu, &duration);
			break;
		case '3': 
			snprintf(prefile,sizeof(prefile),"voicemail/%s/%s/greet",vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-name",prefile, maxgreet, fmtc, 0, vmu, &duration);
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
			vm_change_password(vmu,newpassword);
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
	char ext_context[256]="";
	int box;
	int useadsi = 0;
	int skipuser = 0;
	char tmp[256], *ext;
	char fmtc[256] = "";
	char password[80];
	struct vm_state vms;
	int logretries = 0;
	struct ast_vm_user *vmu = NULL, vmus;
	char *context=NULL;
	int silentexit = 0;

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
	
	while (!valid && (logretries < maxlogins)) {
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
		logretries++;
	}
	if (!valid && (logretries >= maxlogins)) {
		ast_stopstream(chan);
		res = play_and_wait(chan, "vm-goodbye");
		if (res > 0)
			res = 0;
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
					cmd = play_message(chan, vmu, &vms, vms.curmsg);
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
				cmd = get_folder2(chan, "vm-changeto", 0);
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
			case '3': /* Advanced options */
				cmd = 0;
				vms.repeats = 0;
				while((cmd > -1) && (cmd != 't') && (cmd != '#')) {
					switch(cmd) {
					case '1': /* Reply */
						if(vms.lastmsg > -1)
							cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 1);
						else
							cmd = play_and_wait(chan, "vm-sorry");
						cmd = 't';
						break;
					case '2': /* Callback */
						ast_verbose( VERBOSE_PREFIX_3 "Callback Requested\n");
						if (strlen(vmu->callback) && vms.lastmsg > -1) {
							cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 2);
							if (cmd == 9) {
								silentexit = 1;
								goto out;
							}
						}
						else 
							cmd = play_and_wait(chan, "vm-sorry");
						cmd = 't';
						break;
					case '3': /* Envelope */
						if(vms.lastmsg > -1)
							cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 3);
						else
							cmd = play_and_wait(chan, "vm-sorry");
						cmd = 't';
						break;
					case '4': /* Dialout */
						if (strlen(vmu->dialout)) {
							cmd = dialout(chan, vmu, NULL, vmu->dialout);
							if (cmd == 9) {
								silentexit = 1;
								goto out;
							}
						}
						else 
							cmd = play_and_wait(chan, "vm-sorry");
						cmd = 't';
						break;

					case '*': /* Return to main menu */
						cmd = 't';
						break;

					default:
						cmd = 0;
						if (!vms.starting) {
							cmd = play_and_wait(chan, "vm-toreply");
						}
						if (strlen(vmu->callback) && !vms.starting && !cmd) {
							cmd = play_and_wait(chan, "vm-tocallback");
						}

						if (!cmd && !vms.starting) {
							cmd = play_and_wait(chan, "vm-tohearenv");
						}
						if (strlen(vmu->dialout) && !cmd) {
							cmd = play_and_wait(chan, "vm-tomakecall");
						}
						if (!cmd)
							cmd = play_and_wait(chan, "vm-starmain");
						if (!cmd)
							cmd = ast_waitfordigit(chan,6000);
						if (!cmd)
							vms.repeats++;
						if (vms.repeats > 3)
							cmd = 't';
					}
				}
				if (cmd == 't') {
					cmd = 0;
					vms.repeats = 0;
				}
				break;





			case '4':
				if (vms.curmsg) {
					vms.curmsg--;
					cmd = play_message(chan, vmu, &vms, vms.curmsg);
				} else {
					cmd = play_and_wait(chan, "vm-nomore");
				}
				break;
			case '6':
				if (vms.curmsg < vms.lastmsg) {
					vms.curmsg++;
					cmd = play_message(chan, vmu, &vms, vms.curmsg);
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
				else
					cmd = play_and_wait(chan, "vm-nomore");
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
				if (useadsi)
					adsi_status(chan, vms.newmessages, vms.oldmessages, vms.lastmsg);
				break;
			default:	/* Nothing */
				cmd = vm_instructions(chan, &vms);
				break;
			}
		}
		if ((cmd == 't') || (cmd == '#')) {
			/* Timeout */
			res = 0;
		} else {
			/* Hangup */
			res = -1;
		}
	}
out:
	if (res > -1) {
		ast_stopstream(chan);
		adsi_goodbye(chan);
		if(valid) {
			if (silentexit)
				res = play_and_wait(chan, "vm-dialout");
			else 
				res = play_and_wait(chan, "vm-goodbye");
			if (res > 0)
				res = 0;
		}
		if (useadsi)
			adsi_unload_session(chan);
	}
	if (vmu)
		close_mailbox(&vms, vmu);
	if (vmu)
		free_user(vmu);
	if (valid) {
		snprintf(ext_context, sizeof(ext_context), "%s@%s", vms.username, vmu->context);
		manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext_context, ast_app_has_voicemail(ext_context));
		run_externnotify(chan->context, ext_context, ast_app_has_voicemail(ext_context));

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
	if (data && strlen(data))
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
	strncpy(tmp, data, sizeof(tmp) - 1);
	vmu = malloc(sizeof(struct ast_vm_user));
	if (vmu) {
		memset(vmu, 0, sizeof(struct ast_vm_user));
		strncpy(vmu->context, context, sizeof(vmu->context) - 1);
		strncpy(vmu->mailbox, mbox, sizeof(vmu->mailbox) - 1);
		populate_defaults(vmu);
		stringp = tmp;
		if ((s = strsep(&stringp, ","))) 
			strncpy(vmu->password, s, sizeof(vmu->password) - 1);
		if (stringp && (s = strsep(&stringp, ","))) 
			strncpy(vmu->fullname, s, sizeof(vmu->fullname) - 1);
		if (stringp && (s = strsep(&stringp, ","))) 
			strncpy(vmu->email, s, sizeof(vmu->email) - 1);
		if (stringp && (s = strsep(&stringp, ","))) 
			strncpy(vmu->pager, s, sizeof(vmu->pager) - 1);
		if (stringp && (s = strsep(&stringp, ","))) 
			apply_options(vmu, s);
		vmu->next = NULL;
		if (usersl)
			usersl->next = vmu;
		else
			users = vmu;
		usersl = vmu;
	}
	return 0;
}

static int vm_box_exists(struct ast_channel *chan, void *data) {
	struct localuser *u;
	struct ast_vm_user svm;
	char *context, *box;
	char tmp[256];

	if (!data || !strlen(data)) {
		ast_log(LOG_ERROR, "MailboxExists requires an argument: (vmbox[@context])\n");
		return -1;
	} else {
		strncpy(tmp, data, sizeof(tmp) - 1);
	}

	LOCAL_USER_ADD(u);
	box = tmp;
	while(*box) {
		if ((*box == 's') || (*box == 'b') || (*box == 'u')) {
			box++;
		} else
			break;
	}

	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}

	if ((!find_user(&svm, context, box))) {
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid)) {
			chan->priority += 100;
		} else
			ast_log(LOG_WARNING, "VM box %s@%s exists, but extension %s, priority %d doesn't exist\n", box, context, chan->exten, chan->priority + 101);
	}
	LOCAL_USER_REMOVE(u);
	return 0;
}

#ifndef USEMYSQLVM
/* XXX TL Bug 690 */
static char show_voicemail_users_help[] =
"Usage: show voicemail users [for <context>]\n"
"       Lists all mailboxes currently set up\n";

static char show_voicemail_zones_help[] =
"Usage: show voicemail zones\n"
"       Lists zone message formats\n";

static int handle_show_voicemail_users(int fd, int argc, char *argv[])
{
	struct ast_vm_user *vmu = users;
	char *output_format = "%-10s %-5s %-25s %-10s %6s\n";

	if ((argc < 3) || (argc > 5) || (argc == 4)) return RESULT_SHOWUSAGE;
	else if ((argc == 5) && strcmp(argv[3],"for")) return RESULT_SHOWUSAGE;

	if (vmu) {
		if (argc == 3)
			ast_cli(fd, output_format, "Context", "Mbox", "User", "Zone", "NewMsg");
		else {
			int count = 0;
			while (vmu) {
				if (!strcmp(argv[4],vmu->context))
					count++;
				vmu = vmu->next;
			}
			if (count) {
				vmu = users;
				ast_cli(fd, output_format, "Context", "Mbox", "User", "Zone", "NewMsg");
			} else {
				ast_cli(fd, "No such voicemail context \"%s\"\n", argv[4]);
				return RESULT_FAILURE;
			}
		}
		while (vmu) {
			char dirname[256];
			DIR *vmdir;
			struct dirent *vment;
			int vmcount = 0;
			char count[12];

			if ((argc == 3) || ((argc == 5) && !strcmp(argv[4],vmu->context))) {
				make_dir(dirname, 255, vmu->context, vmu->mailbox, "INBOX");
				if ((vmdir = opendir(dirname))) {
					/* No matter what the format of VM, there will always be a .txt file for each message. */
					while ((vment = readdir(vmdir)))
						if (!strncmp(vment->d_name + 7,".txt",4))
							vmcount++;
					closedir(vmdir);
				}
				snprintf(count,11,"%d",vmcount);
				ast_cli(fd, output_format, vmu->context, vmu->mailbox, vmu->fullname, vmu->zonetag, count);
			}
			vmu = vmu->next;
		}
	} else {
		ast_cli(fd, "There are no voicemail users currently defined\n");
		return RESULT_FAILURE;
	}
	return RESULT_SUCCESS;
}

static int handle_show_voicemail_zones(int fd, int argc, char *argv[])
{
	struct vm_zone *zone = zones;
	char *output_format = "%-15s %-20s %-45s\n";

	if (argc != 3) return RESULT_SHOWUSAGE;

	if (zone) {
		ast_cli(fd, output_format, "Zone", "Timezone", "Message Format");
		while (zone) {
			ast_cli(fd, output_format, zone->name, zone->timezone, zone->msg_format);
			zone = zone->next;
		}
	} else {
		ast_cli(fd, "There are no voicemail zones currently defined\n");
		return RESULT_FAILURE;
	}
	return RESULT_SUCCESS;
}

static char *complete_show_voicemail_users(char *line, char *word, int pos, int state)
{
	int which = 0;
	struct ast_vm_user *vmu = users;
	char *context = "";

	/* 0 - show; 1 - voicemail; 2 - users; 3 - for; 4 - <context> */
	if (pos > 4)
		return NULL;
	if (pos == 3) {
		if (state == 0)
			return strdup("for");
		else
			return NULL;
	}
	while (vmu) {
		if (!strncasecmp(word, vmu->context, strlen(word))) {
			if (context && strcmp(context, vmu->context)) {
				if (++which > state) {
					return strdup(vmu->context);
				}
				context = vmu->context;
			}
		}
		vmu = vmu->next;
	}
	return NULL;
}

static struct ast_cli_entry show_voicemail_users_cli =
	{ { "show", "voicemail", "users", NULL },
	handle_show_voicemail_users, "List defined voicemail boxes",
	show_voicemail_users_help, complete_show_voicemail_users };

static struct ast_cli_entry show_voicemail_zones_cli =
	{ { "show", "voicemail", "zones", NULL },
	handle_show_voicemail_zones, "List zone message formats",
	show_voicemail_zones_help, NULL };

#endif

static int load_config(void)
{
	struct ast_vm_user *cur, *l;
	struct vm_zone *zcur, *zl;
	struct ast_config *cfg;
	char *cat;
	struct ast_variable *var;
	char *notifystr = NULL;
	char *astattach;
	char *astsaycid;
	char *astcallop;
	char *astreview;
	char *asthearenv;
	char *silencestr;
	char *thresholdstr;
	char *fmt;
	char *astemail;
 	char *astmailcmd = SENDMAIL;
	char *s,*q,*stringp;
	char *dialoutcxt = NULL;
	char *callbackcxt = NULL;	
	char *exitcxt = NULL;	
	
	int x;

	cfg = ast_load(VOICEMAIL_CONFIG);
	ast_mutex_lock(&vmlock);
	cur = users;
	while(cur) {
		l = cur;
		cur = cur->next;
		l->alloced = 1;
		free_user(l);
	}
	zcur = zones;
	while(zcur) {
		zl = zcur;
		zcur = zcur->next;
		free_zone(zl);
	}
	zones = NULL;
	zonesl = NULL;
	users = NULL;
	usersl = NULL;
	if (cfg) {
		/* General settings */

		/* Attach voice message to mail message ? */
		attach_voicemail = 1;
		if (!(astattach = ast_variable_retrieve(cfg, "general", "attach"))) 
			astattach = "yes";
		attach_voicemail = ast_true(astattach);

                /* Mail command */
                strncpy(mailcmd, SENDMAIL, sizeof(mailcmd) - 1); /* Default */
                if ((astmailcmd = ast_variable_retrieve(cfg, "general", "mailcmd")))
                   strncpy(mailcmd, astmailcmd, sizeof(mailcmd) - 1); /* User setting */

		maxsilence = 0;
		if ((silencestr = ast_variable_retrieve(cfg, "general", "maxsilence"))) {
			maxsilence = atoi(silencestr);
			if (maxsilence > 0)
				maxsilence *= 1000;
		}
		
		/* External voicemail notify application */
		
		if ((notifystr = ast_variable_retrieve(cfg, "general", "externnotify"))) {
			strncpy(externnotify, notifystr, sizeof(externnotify) - 1);
			ast_log(LOG_DEBUG, "found externnotify: %s\n", externnotify);
		} else {
			externnotify[0] = '\0';
		}

		/* Silence treshold */
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

		vmminmessage = 0;
		if ((s = ast_variable_retrieve(cfg, "general", "minmessage"))) {
			if (sscanf(s, "%d", &x) == 1) {
				vmminmessage = x;
			} else {
				ast_log(LOG_WARNING, "Invalid min message time length\n");
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

		maxlogins = 3;
		if ((s = ast_variable_retrieve(cfg, "general", "maxlogins"))) {
			if (sscanf(s, "%d", &x) == 1) {
				maxlogins = x;
			} else {
				ast_log(LOG_WARNING, "Invalid max failed login attempts\n");
			}
		}

		if ((s = ast_variable_retrieve(cfg, "general", "cidinternalcontexts"))){
			ast_log(LOG_DEBUG,"VM_CID Internal context string: %s\n",s);
			stringp = ast_strdupa(s);
			for (x = 0 ; x < MAX_NUM_CID_CONTEXTS ; x++){
				if ((stringp)&&(strlen(stringp))){
					q = strsep(&stringp,",");
					while ((*q == ' ')||(*q == '\t')) /* Eat white space between contexts */
						q++;
					strcpy(cidinternalcontexts[x],q);
					ast_log(LOG_DEBUG,"VM_CID Internal context %d: %s\n", x, cidinternalcontexts[x]);
				} else {
					cidinternalcontexts[x][0] = '\0';
				}
			}
		}
		reviewvm = 0;
		if (!(astreview = ast_variable_retrieve(cfg, "general", "review"))){
			ast_log(LOG_DEBUG,"VM Review Option disabled globally\n");
			astreview = "no";
		}
		reviewvm = ast_true(astreview);

		calloper = 0;
		if (!(astcallop = ast_variable_retrieve(cfg, "general", "operator"))){
			ast_log(LOG_DEBUG,"VM Operator break disabled globally\n"); 								     	     astcallop = "no";
		}
		calloper = ast_true(astcallop);

		saycidinfo = 0;
		if (!(astsaycid = ast_variable_retrieve(cfg, "general", "saycid"))) {
			ast_log(LOG_DEBUG,"VM CID Info before msg disabled globally\n");
			astsaycid = "no";
		} 
		saycidinfo = ast_true(astsaycid);

		hearenv = 1;
		if (!(asthearenv = ast_variable_retrieve(cfg, "general", "envelope"))) {
			ast_log(LOG_DEBUG,"ENVELOPE before msg enabled globally\n");
			asthearenv = "yes";
		}
		hearenv = ast_true(asthearenv);	
		
		if ((dialoutcxt = ast_variable_retrieve(cfg, "general", "dialout"))) {
                        strncpy(dialcontext, dialoutcxt, sizeof(dialcontext) - 1);
                        ast_log(LOG_DEBUG, "found dialout context: %s\n", dialcontext);
                } else {
                        dialcontext[0] = '\0';
                }
	 		
		if ((callbackcxt = ast_variable_retrieve(cfg, "general", "callback"))) {
			strncpy(callcontext, callbackcxt, sizeof(callcontext) -1);
			ast_log(LOG_DEBUG, "found callback context: %s\n", callcontext);
		} else {
			callcontext[0] = '\0';
		}

		if ((exitcxt = ast_variable_retrieve(cfg, "general", "exitcontext"))) {
			strncpy(exitcontext, exitcxt, sizeof(exitcontext) - 1);
			ast_log(LOG_DEBUG, "found operator context: %s\n", exitcontext);
		} else {
			exitcontext[0] = '\0';
		}

#ifdef USEMYSQLVM
		if (!(s=ast_variable_retrieve(cfg, "general", "dbuser"))) {
			strcpy(dbuser, "test");
		} else {
			strcpy(dbuser, s);
		}
		if (!(s=ast_variable_retrieve(cfg, "general", "dbpass"))) {
			strcpy(dbpass, "test");
		} else {
			strcpy(dbpass, s);
		}
		if (!(s=ast_variable_retrieve(cfg, "general", "dbhost"))) {
			strcpy(dbhost, "");
		} else {
			strcpy(dbhost, s);
		}
		if (!(s=ast_variable_retrieve(cfg, "general", "dbname"))) {
			strcpy(dbname, "vmdb");
		} else {
			strcpy(dbname, s);
		}
#endif

#ifdef USEPOSTGRESVM
		if (!(s=ast_variable_retrieve(cfg, "general", "dboption"))) {
			strcpy(dboption, "dboption not-specified in voicemail.conf");
		} else {
			strcpy(dboption, s);
		}
#endif
		cat = ast_category_browse(cfg, NULL);
		while(cat) {
			if (strcasecmp(cat, "general")) {
				var = ast_variable_browse(cfg, cat);
				if (strcasecmp(cat, "zonemessages")) {
#ifndef USESQLVM
					/* Process mailboxes in this context */
					while(var) {
						append_mailbox(cat, var->name, var->value);
						var = var->next;
					}
#endif
				} else {
					/* Timezones in this context */
					while(var) {
						struct vm_zone *z;
						z = malloc(sizeof(struct vm_zone));
						if (z != NULL) {
							char *msg_format, *timezone;
							msg_format = ast_strdupa(var->value);
							if (msg_format != NULL) {
								timezone = strsep(&msg_format, "|");
								if (msg_format) {
									strncpy(z->name, var->name, sizeof(z->name) - 1);
									strncpy(z->timezone, timezone, sizeof(z->timezone) - 1);
									strncpy(z->msg_format, msg_format, sizeof(z->msg_format) - 1);
									z->next = NULL;
									if (zones) {
										zonesl->next = z;
										zonesl = z;
									} else {
										zones = z;
										zonesl = z;
									}
								} else {
									ast_log(LOG_WARNING, "Invalid timezone definition at line %d\n", var->lineno);
									free(z);
								}
							} else {
								ast_log(LOG_WARNING, "Out of memory while reading voicemail config\n");
								free(z);
								return -1;
							}
						} else {
							ast_log(LOG_WARNING, "Out of memory while reading voicemail config\n");
							return -1;
						}
						var = var->next;
					}
				}
			}
			cat = ast_category_browse(cfg, cat);
		}
		memset(fromstring,0,sizeof(fromstring));
		memset(emailtitle,0,sizeof(emailtitle));
		strncpy(charset, "ISO-8859-1", sizeof(charset) - 1);
		if (emailbody) {
			free(emailbody);
			emailbody = NULL;
		}
		if (emailsubject) {
			free(emailsubject);
			emailsubject = NULL;
		}
		if ((s=ast_variable_retrieve(cfg, "general", "pbxskip")))
			pbxskip = ast_true(s);
		if ((s=ast_variable_retrieve(cfg, "general", "fromstring")))
			strncpy(fromstring,s,sizeof(fromstring)-1);
		if ((s=ast_variable_retrieve(cfg, "general", "charset")))
			strncpy(charset,s,sizeof(charset)-1);
		if ((s=ast_variable_retrieve(cfg, "general", "emailtitle")))
			strncpy(emailtitle,s,sizeof(emailtitle)-1);
		if ((s=ast_variable_retrieve(cfg, "general", "emailsubject")))
			emailsubject = strdup(s);
		if ((s=ast_variable_retrieve(cfg, "general", "emailbody"))) {
			char *tmpread, *tmpwrite;
			emailbody = strdup(s);

			/* substitute strings \t and \n into the apropriate characters */
			tmpread = tmpwrite = emailbody;
			while ((tmpwrite = strchr(tmpread,'\\'))) {
				int len = strlen("\n");
				switch (tmpwrite[1]) {
					case 'n':
						strncpy(tmpwrite+len,tmpwrite+2,strlen(tmpwrite+2)+1);
						strncpy(tmpwrite,"\n",len);
						break;
					case 't':
						strncpy(tmpwrite+len,tmpwrite+2,strlen(tmpwrite+2)+1);
						strncpy(tmpwrite,"\t",len);
						break;
					default:
						ast_log(LOG_NOTICE, "Substitution routine does not support this character: %c\n",tmpwrite[1]);
				}
				tmpread = tmpwrite+len;
			}
		}
		ast_destroy(cfg);
		ast_mutex_unlock(&vmlock);
		return 0;
	} else {
		ast_mutex_unlock(&vmlock);
		ast_log(LOG_WARNING, "Error reading voicemail config\n");
		return -1;
	}
}

int reload(void)
{
	return(load_config());
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app);
	res |= ast_unregister_application(capp);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(capp2);
	res |= ast_unregister_application(app3);
	sql_close();
#ifndef USEMYSQLVM
	ast_cli_unregister(&show_voicemail_users_cli);
	ast_cli_unregister(&show_voicemail_zones_cli);
#endif
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, vm_exec, synopsis_vm, descrip_vm);
	res |= ast_register_application(capp, vm_exec, synopsis_vm, descrip_vm);
	res |= ast_register_application(app2, vm_execmain, synopsis_vmain, descrip_vmain);
	res |= ast_register_application(capp2, vm_execmain, synopsis_vmain, descrip_vmain);
	res |= ast_register_application(app3, vm_box_exists, synopsis_vm_box_exists, descrip_vm_box_exists);
	if (res)
		return(res);

	if ((res=load_config())) {
		return(res);
	}

	if ((res = sql_init())) {
		ast_log(LOG_WARNING, "SQL init\n");
		return res;
	}
#ifndef USEMYSQLVM	
	ast_cli_register(&show_voicemail_users_cli);
	ast_cli_register(&show_voicemail_zones_cli);
#endif
	return res;
}

char *description(void)
{
	return tdesc;
}

static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context) 
{
	int cmd = 0;
	char destination[80] = "";
	int retries = 0;

	if (!num) {
		ast_verbose( VERBOSE_PREFIX_3 "Destination number will be entered manually\n");
		while (retries < 3 && cmd != 't') {
			destination[1] = '\0';
			destination[0] = cmd = play_and_wait(chan,"vm-enter-num-to-call");
			if (!cmd)
				destination[0] = cmd = play_and_wait(chan, "vm-then-pound");
			if (!cmd)
				destination[0] = cmd = play_and_wait(chan, "vm-star-cancel");
			if (!cmd) {
				cmd = ast_waitfordigit(chan, 6000);
				if (cmd)
					destination[0] = cmd;
			}
			if (!cmd) {
				retries++;
			} else {

				if (cmd < 0)
					return 0;
				if (cmd == '*') {
					ast_verbose( VERBOSE_PREFIX_3 "User hit '*' to cancel outgoing call\n");
					return 0;
				}
				if ((cmd = ast_readstring(chan,destination + strlen(destination),sizeof(destination)-1,6000,10000,"#")) < 0) 
					retries++;
				else
					cmd = 't';
			}
		}
		if (retries >= 3) {
			return 0;
		}
		
	} else {
		ast_verbose( VERBOSE_PREFIX_3 "Destination number is CID number '%s'\n", num);
		strncpy(destination, num, sizeof(destination) -1);
	}

	if (strlen(destination)) {
		if (destination[strlen(destination) -1 ] == '*')
			return 0; 
		ast_verbose( VERBOSE_PREFIX_3 "Placing outgoing call to extension '%s' in context '%s' from context '%s'\n", destination, outgoing_context, chan->context);
		strncpy(chan->exten, destination, sizeof(chan->exten) - 1);
		strncpy(chan->context, outgoing_context, sizeof(chan->context) - 1);
		chan->priority = 0;
		return 9;
	}
	return 0;
}

static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg, int option)
{
	int res = 0;
	char filename[256],*origtime, *cid, *context, *name, *num;
	struct ast_config *msg_cfg;
	int retries = 0;

	vms->starting = 0; 
	make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);

	/* Retrieve info from VM attribute file */

        make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg);
        snprintf(filename,sizeof(filename), "%s.txt", vms->fn2);
        msg_cfg = ast_load(filename);
        if (!msg_cfg) {
                ast_log(LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
                return 0;
        }
                                                                                                                                 
        if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime")))
                return 0;
                                                                                                                                 
        cid = ast_variable_retrieve(msg_cfg, "message", "callerid");

        context = ast_variable_retrieve(msg_cfg, "message", "context");
	if(!strncasecmp("macro",context,5)) /* Macro names in contexts are useless for our needs */
		context = ast_variable_retrieve(msg_cfg, "message","macrocontext");

	if (option == 3) {

		if (!res)
			res = play_message_datetime(chan, vmu, origtime, filename);
		if (!res)
			res = play_message_callerid(chan, vms, cid, context, 0);
	} else if (option == 2) { /* Call back */

		if (strlen(cid)) {
			ast_callerid_parse(cid, &name, &num);
			while ((res > -1) && (res != 't')) {
				switch(res) {
					case '1':
						if (num) {
							/* Dial the CID number */
							res = dialout(chan, vmu, num, vmu->callback);
							if (res)
								return 9;
						} else {
							res = '2';
						}
						break;

					case '2':
						/* Want to enter a different number, can only do this if there's a dialout context for this user */
						if (strlen(vmu->dialout)) {
							res = dialout(chan, vmu, NULL, vmu->dialout);
							if (res)
								return 9;
						} else {
							ast_verbose( VERBOSE_PREFIX_3 "Caller can not specify callback number - no dialout context available\n");
							res = play_and_wait(chan, "vm-sorry");
						}
						return res;
					case '*':
						res = 't';
						break;
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
					case '8':
					case '9':
					case '0':

						res = play_and_wait(chan, "vm-sorry");
						retries++;
						break;
					default:
						if (num) {
							ast_verbose( VERBOSE_PREFIX_3 "Confirm CID number '%s' is number to use for callback\n", num);
							res = play_and_wait(chan, "vm-num-i-have");
							if (!res)
								res = play_message_callerid(chan, vms, num, vmu->context, 1);
							if (!res)
								res = play_and_wait(chan, "vm-tocallnum");
							/* Only prompt for a caller-specified number if there is a dialout context specified */
							if (strlen(vmu->dialout)) {
								if (!res)
									res = play_and_wait(chan, "vm-calldiffnum");
							}
						} else  {
							res = play_and_wait(chan, "vm-nonumber");
							if (strlen(vmu->dialout)) {
								if (!res)
									res = play_and_wait(chan, "vm-toenternumber");
							}
						}
						if (!res)
							res = play_and_wait(chan, "vm-star-cancel");
						if (!res)
							res = ast_waitfordigit(chan, 6000);
						if (!res)
							retries++;
						if (retries > 3)
							res = 't';
							break; 

						}
					if (res == 't')
						res = 0;
					else if (res == '*')
						res = -1;
				}
			}

	}
	else if (option == 1) { /* Reply */
              		/* Send reply directly to sender */
		if (strlen(cid)) {
			ast_callerid_parse(cid, &name, &num);
			if (!num) {
				ast_verbose(VERBOSE_PREFIX_3 "No CID number available, no reply sent\n");
				if (!res)
					res = play_and_wait(chan, "vm-nonumber");
				return res;
			} else {
				if (find_user(NULL, vmu->context, num)) {
					ast_verbose(VERBOSE_PREFIX_3 "Leaving voicemail for '%s' in context '%s'\n", num, vmu->context);
					leave_voicemail(chan, num, 1, 0, 1);
					res = 't';
					return res;
				}
              				else {
					ast_verbose( VERBOSE_PREFIX_3 "No mailbox number '%s' in context '%s', no reply sent\n", num, vmu->context);
                      				/* Sender has no mailbox, can't reply */
                      				play_and_wait(chan, "vm-nobox");
                      				res = 't';
					return res;
				}
			} 
			res = 0;
		}
	}

	ast_destroy(msg_cfg);

	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);
		vms->heard[msg] = 1;
		res = wait_file(chan, vms, vms->fn);
	}
	return res;
}






 
 
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int outsidecaller, struct ast_vm_user *vmu, int *duration)
{
	/* Record message & let caller review or re-record it, or set options if applicable */
 	int res = 0;
 	int cmd = 0;
 	int max_attempts = 3;
 	int attempts = 0;
 	int recorded = 0;
 	int message_exists = 0;
 	/* Note that urgent and private are for flagging messages as such in the future */
 
	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_record_review called without duration pointer\n");
		return -1;
	}

 	cmd = '3';	 /* Want to start by recording */
 
        	while((cmd >= 0) && (cmd != 't')) {
        		switch (cmd) {
 	       	case '1':
 			if (!message_exists) {
 				/* In this case, 1 is to record a message */
 				cmd = '3';
 				break;
 			} else {
 				/* Otherwise 1 is to save the existing message */
 				ast_verbose(VERBOSE_PREFIX_3 "Saving message as is\n");
 				ast_streamfile(chan, "vm-msgsaved", chan->language);
 				ast_waitstream(chan, "");
 				cmd = 't';
 				return res;
 			}
 		case '2':
 			/* Review */
 			ast_verbose(VERBOSE_PREFIX_3 "Reviewing the message\n");
 			ast_streamfile(chan, recordfile, chan->language);
 			cmd = ast_waitstream(chan, AST_DIGIT_ANY);
 			break;
 		case '3':
 			message_exists = 0;
 			/* Record */
 			if (recorded == 1)
				ast_verbose(VERBOSE_PREFIX_3 "Re-recording the message\n");
 			else	
				ast_verbose(VERBOSE_PREFIX_3 "Recording the message\n");
			if (recorded && outsidecaller) {
 				cmd = play_and_wait(chan, INTRO);
 				cmd = play_and_wait(chan, "beep");
 			}
 			recorded = 1;
 			/* After an attempt has been made to record message, we have to take care of INTRO and beep for incoming messages, but not for greetings */
			cmd = play_and_record(chan, playfile, recordfile, maxtime, fmt, duration);
 			if (cmd == -1)
 			/* User has hung up, no options to give */
 				return res;
 			if (cmd == '0') {
 				/* Erase the message if 0 pushed during playback */
 				play_and_wait(chan, "vm-deleted");
 			 	vm_delete(recordfile);
 			} else if (cmd == '*') {
 				break;
 			} 
#if 0			
 			else if (vmu->review && (*duration < 5)) {
 				/* Message is too short */
 				ast_verbose(VERBOSE_PREFIX_3 "Message too short\n");
				cmd = play_and_wait(chan, "vm-tooshort");
 				cmd = vm_delete(recordfile);
 				break;
 			}
 			else if (vmu->review && (cmd == 2 && *duration < (maxsilence + 3))) {
 				/* Message is all silence */
 				ast_verbose(VERBOSE_PREFIX_3 "Nothing recorded\n");
 				cmd = vm_delete(recordfile);
	                        cmd = play_and_wait(chan, "vm-nothingrecorded");
                                if (!cmd)
 					cmd = play_and_wait(chan, "vm-speakup");
 				break;
 			}
#endif
 			else {
 				/* If all is well, a message exists */
 				message_exists = 1;
				cmd = 0;
 			}
 			break;
 		case '4':
 		case '5':
 		case '6':
 		case '7':
 		case '8':
 		case '9':
		case '*':
		case '#':
 			cmd = play_and_wait(chan, "vm-sorry");
 			break;
#if 0 
/*  XXX Commented out for the moment because of the dangers of deleting
    a message while recording (can put the message numbers out of sync) */
 		case '*':
 			/* Cancel recording, delete message, offer to take another message*/
 			cmd = play_and_wait(chan, "vm-deleted");
 			cmd = vm_delete(recordfile);
 			if (outsidecaller) {
 				res = vm_exec(chan, NULL);
 				return res;
 			}
 			else
 				return 1;
#endif
 		case '0':
 			if (outsidecaller && vmu->operator) {
 				if (message_exists)
 					play_and_wait(chan, "vm-msgsaved");
 				return cmd;
 			} else
 				cmd = play_and_wait(chan, "vm-sorry");
 			break;
 		default:
			/* If the caller is an ouside caller, and the review option is enabled,
			   allow them to review the message, but let the owner of the box review
			   their OGM's */
			if (outsidecaller && !vmu->review)
				return cmd;
 			if (message_exists) {
 				cmd = play_and_wait(chan, "vm-review");
 			}
 			else {
 				cmd = play_and_wait(chan, "vm-torerecord");
 				if (!cmd)
 					cmd = ast_waitfordigit(chan, 600);
 			}
 			
 			if (!cmd && outsidecaller && vmu->operator) {
 				cmd = play_and_wait(chan, "vm-reachoper");
 				if (!cmd)
 					cmd = ast_waitfordigit(chan, 600);
 			}
#if 0
			if (!cmd)
 				cmd = play_and_wait(chan, "vm-tocancelmsg");
#endif
 			if (!cmd)
 				cmd = ast_waitfordigit(chan, 6000);
 			if (!cmd) {
 				attempts++;
 			}
 			if (attempts > max_attempts) {
 				cmd = 't';
 			}
 		}
 	}
 	if (outsidecaller)  
 		play_and_wait(chan, "vm-goodbye");
 	if (cmd == 't')
 		cmd = 0;
 	return cmd;
 }
 

static int vm_delete(char *file)
{
	char *txt;
	txt = (char *)alloca((strlen(file) + 5)*sizeof(char));
	/* Sprintf here is safe because we alloca'd exactly the right length */
	sprintf(txt, "%s.txt", file);
	unlink(txt);
	return ast_filedelete(file, NULL);
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



	


