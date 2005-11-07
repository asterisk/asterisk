/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * 12-16-2004 : Support for Greek added by InAccess Networks (work funded by HOL, www.hol.gr)
 *				 George Konstantoulakis <gkon@inaccessnetworks.com>
 *
 * 05-10-2005 : Support for Swedish and Norwegian added by Daniel Nylander, http://www.danielnylander.se/
 *
 * 05-11-2005 : An option for maximum number of messsages per mailbox added by GDS Partners (www.gdspartners.com)
 * 07-11-2005 : An issue with voicemail synchronization has been fixed by GDS Partners (www.gdspartners.com)
 *				 Stojan Sljivic <stojan.sljivic@gdspartners.com>
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
 *
 * \brief Comedian Mail - Voicemail System
 * 
 * \par See also
 * \arg \ref Config_vm
 * \ingroup applications
 */

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <dirent.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/module.h"
#include "asterisk/adsi.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/dsp.h"
#include "asterisk/localtime.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#ifdef USE_ODBC_STORAGE
#include "asterisk/res_odbc.h"
#endif

#define COMMAND_TIMEOUT 5000

#define VOICEMAIL_CONFIG "voicemail.conf"
#define ASTERISK_USERNAME "asterisk"

/* Default mail command to mail voicemail. Change it with the
    mailcmd= command in voicemail.conf */
#define SENDMAIL "/usr/sbin/sendmail -t"

#define INTRO "vm-intro"

#define MAXMSG 100
#define MAXMSGLIMIT 9999

#define BASEMAXINLINE 256
#define BASELINELEN 72
#define BASEMAXINLINE 256
#define eol "\r\n"

#define MAX_DATETIME_FORMAT	512
#define MAX_NUM_CID_CONTEXTS 10

#define VM_REVIEW		(1 << 0)
#define VM_OPERATOR		(1 << 1)
#define VM_SAYCID		(1 << 2)
#define VM_SVMAIL		(1 << 3)
#define VM_ENVELOPE		(1 << 4)
#define VM_SAYDURATION		(1 << 5)
#define VM_SKIPAFTERCMD 	(1 << 6)
#define VM_FORCENAME		(1 << 7)	/* Have new users record their name */
#define VM_FORCEGREET		(1 << 8)	/* Have new users record their greetings */
#define VM_PBXSKIP		(1 << 9)
#define VM_DIRECFORWARD 	(1 << 10)	/* directory_forward */
#define VM_ATTACH		(1 << 11)
#define VM_DELETE		(1 << 12)
#define VM_ALLOCED		(1 << 13)

#define ERROR_LOCK_PATH		-100

enum {
	OPT_SILENT =(1 << 0),
	OPT_BUSY_GREETING = (1 << 1),
	OPT_UNAVAIL_GREETING = (1 << 2),
	OPT_RECORDGAIN = (1 << 3),
	OPT_PREPEND_MAILBOX = (1 << 4),
} vm_option_flags;

enum {
	OPT_ARG_RECORDGAIN = 0,
	OPT_ARG_ARRAY_SIZE = 1,
} vm_option_args;

AST_APP_OPTIONS(vm_app_options, {
	AST_APP_OPTION('s', OPT_SILENT),
	AST_APP_OPTION('b', OPT_BUSY_GREETING),
	AST_APP_OPTION('u', OPT_UNAVAIL_GREETING),
	AST_APP_OPTION_ARG('g', OPT_RECORDGAIN, OPT_ARG_RECORDGAIN),
	AST_APP_OPTION('p', OPT_PREPEND_MAILBOX),
});

static int load_config(void);

/*! \page vmlang Voicemail Language Syntaxes Supported

	\par Syntaxes supported, not really language codes.
	\arg \b en - English
	\arg \b de - German
	\arg \b es - Spanish
	\arg \b fr - French
	\arg \b it = Italian
	\arg \b nl - Dutch
	\arg \b pt - Portuguese
	\arg \b gr - Greek
	\arg \b no - Norwegian
	\arg \b se - Swedish

German requires the following additional soundfile:
\arg \b 1F	einE (feminine)

Spanish requires the following additional soundfile:
\arg \b 1M      un (masculine)

Dutch, Portuguese & Spanish require the following additional soundfiles:
\arg \b vm-INBOXs	singular of 'new'
\arg \b vm-Olds		singular of 'old/heard/read'

NB these are plural:
\arg \b vm-INBOX	nieuwe (nl)
\arg \b vm-Old		oude (nl)

Swedish uses:
\arg \b vm-nytt		singular of 'new'
\arg \b vm-nya		plural of 'new'
\arg \b vm-gammalt	singular of 'old'
\arg \b vm-gamla	plural of 'old'
\arg \b digits/ett	'one', not always same as 'digits/1'

Norwegian uses:
\arg \b vm-ny		singular of 'new'
\arg \b vm-nye		plural of 'new'
\arg \b vm-gammel	singular of 'old'
\arg \b vm-gamle	plural of 'old'

Dutch also uses:
\arg \b nl-om		'at'?

Spanish also uses:
\arg \b vm-youhaveno

Italian requires the following additional soundfile:

For vm_intro_it:
\arg \b vm-nuovo	new
\arg \b vm-nuovi	new plural
\arg \b vm-vecchio	old
\arg \b vm-vecchi	old plural

\note Don't use vm-INBOX or vm-Old, because they are the name of the INBOX and Old folders,
spelled among others when you have to change folder. For the above reasons, vm-INBOX
and vm-Old are spelled plural, to make them sound more as folder name than an adjective.

*/

struct baseio {
	int iocp;
	int iolen;
	int linelength;
	int ateof;
	unsigned char iobuf[BASEMAXINLINE];
};

/*! Structure for linked list of users */
struct ast_vm_user {
	char context[AST_MAX_CONTEXT];	/*!< Voicemail context */
	char mailbox[AST_MAX_EXTENSION];/*!< Mailbox id, unique within vm context */
	char password[80];		/*!< Secret pin code, numbers only */
	char fullname[80];		/*!< Full name, for directory app */
	char email[80];			/*!< E-mail address */
	char pager[80];			/*!< E-mail address to pager (no attachment) */
	char serveremail[80];		/*!< From: Mail address */
	char mailcmd[160];		/*!< Configurable mail command */
	char language[MAX_LANGUAGE];    /*!< Config: Language setting */
	char zonetag[80];		/*!< Time zone */
	char callback[80];
	char dialout[80];
	char uniqueid[20];		/*!< Unique integer identifier */
	char exit[80];
	unsigned int flags;		/*!< VM_ flags */	
	int saydurationm;
	int maxmsg;			/*!< Maximum number of msgs per folder for this mailbox */
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
	int *deleted;
	int *heard;
	int curmsg;
	int lastmsg;
	int newmessages;
	int oldmessages;
	int starting;
	int repeats;
};
static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg,
			    int option, signed char record_gain);
static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context);
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime,
			      char *fmt, int outsidecaller, struct ast_vm_user *vmu, int *duration, const char *unlockdir,
			      signed char record_gain);
static int vm_tempgreeting(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain);
static int vm_play_folder_name(struct ast_channel *chan, char *mbox);

static void apply_options(struct ast_vm_user *vmu, const char *options);

#ifdef USE_ODBC_STORAGE
static char odbc_database[80];
static char odbc_table[80];
#define RETRIEVE(a,b) retrieve_file(a,b)
#define DISPOSE(a,b) remove_file(a,b)
#define STORE(a,b,c,d) store_file(a,b,c,d)
#define EXISTS(a,b,c,d) (message_exists(a,b))
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(a,b,c,d,e,f))
#define COPY(a,b,c,d,e,f,g,h) (copy_file(a,b,c,d,e,f))
#define DELETE(a,b,c) (delete_file(a,b))
#else
#define RETRIEVE(a,b)
#define DISPOSE(a,b)
#define STORE(a,b,c,d)
#define EXISTS(a,b,c,d) (ast_fileexists(c,NULL,d) > 0)
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(g,h));
#define COPY(a,b,c,d,e,f,g,h) (copy_file(g,h));
#define DELETE(a,b,c) (vm_delete(c))
#endif

static char VM_SPOOL_DIR[AST_CONFIG_MAX_PATH];

static char ext_pass_cmd[128];

static char *tdesc = "Comedian Mail (Voicemail System)";

static char *addesc = "Comedian Mail";

static char *synopsis_vm =
"Leave a voicemail message";

static char *descrip_vm =
"  VoiceMail(mailbox[@context][&mailbox[@context]][...][|options]):  Leaves"
"voicemail for a given mailbox (must be configured in voicemail.conf).\n"
" If the options contain: \n"
"* 'b'    the \"busy\" greeting will be played.\n"
"* 'g(#)' the specified amount of gain will be requested during message\n"
"         recording (units are whole-number decibels (dB))\n"
"* 's'    instructions for leaving the message will be skipped.\n"
"* 'u'    the \"unavailable\" greeting will be played.\n"
"If the caller presses '0' (zero) during the prompt, the call jumps to\n"
"extension 'o' in the current context.\n"
"If the caller presses '*' during the prompt, the call jumps to\n"
"extension 'a' in the current context.\n"
"If the requested mailbox does not exist, and there exists a priority\n"
"n + 101, then that priority will be taken next.\n"
"If an error occur in the voicemail application resulting in that the message cannot be left,\n" 
"and there exists a priority n + 101, then that priority will be taken next.\n"
"When multiple mailboxes are specified, the unavailable or busy message\n"
"will be taken from the first mailbox specified.\n"
"Execution will fail if the mailbox does not exist, or if the user disconnects.\n";

static char *synopsis_vmain =
"Enter voicemail system";

static char *descrip_vmain =
"  VoiceMailMain([mailbox][@context][|options]): Enters the main voicemail system\n"
"for the checking of voicemail. The mailbox can be passed in,\n"
"which will stop the voicemail system from prompting the user for the mailbox.\n"
"If the options contain: \n"
"* 'p'    the supplied mailbox is prepended to the user's entry and\n"
"         the resulting string is used as the mailbox number. This can\n"
"         be useful for virtual hosting of voicemail boxes.\n"
"* 'g(#)' the specified amount of gain will be requested during message\n"
"         recording (units are whole-number decibels (dB))\n"
"* 's'    the password check will be skipped.\n"
"If a context is specified, mailboxes are considered in that voicemail context only.\n"
"Returns -1 if the user hangs up or 0 otherwise.\n";

static char *synopsis_vm_box_exists =
"Check if vmbox exists";

static char *descrip_vm_box_exists =
"  MailboxExists(mailbox[@context]): Conditionally branches to priority n+101\n"
"if the specified voice mailbox exists.\n";

static char *synopsis_vmauthenticate =
"Authenticate off voicemail passwords";

static char *descrip_vmauthenticate =
"  VMAuthenticate([mailbox][@context][|options]): Behaves identically to\n"
"the Authenticate application, with the exception that the passwords are\n"
"taken from voicemail.conf.\n"
"  If the mailbox is specified, only that mailbox's password will be considered\n"
"valid. If the mailbox is not specified, the channel variable AUTH_MAILBOX will\n"
"be set with the authenticated mailbox.\n"
"If the options contain 's' then no initial prompts will be played.\n";

/* Leave a message */
static char *app = "VoiceMail";

/* Check mail, control, etc */
static char *app2 = "VoiceMailMain";

static char *app3 = "MailboxExists";
static char *app4 = "VMAuthenticate";

AST_MUTEX_DEFINE_STATIC(vmlock);
struct ast_vm_user *users;
struct ast_vm_user *usersl;
struct vm_zone *zones = NULL;
struct vm_zone *zonesl = NULL;
static int maxsilence;
static int maxmsg;
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

static struct ast_flags globalflags = {0};

static int saydurationminfo;

static char dialcontext[AST_MAX_CONTEXT];
static char callcontext[AST_MAX_CONTEXT];
static char exitcontext[AST_MAX_CONTEXT];

static char cidinternalcontexts[MAX_NUM_CID_CONTEXTS][64];


static char *emailbody = NULL;
static char *emailsubject = NULL;
static char *pagerbody = NULL;
static char *pagersubject = NULL;
static char fromstring[100];
static char pagerfromstring[100];
static char emailtitle[100];
static char charset[32] = "ISO-8859-1";

static unsigned char adsifdn[4] = "\x00\x00\x00\x0F";
static unsigned char adsisec[4] = "\x9B\xDB\xF7\xAC";
static int adsiver = 1;
static char emaildateformat[32] = "%A, %B %d, %Y at %r";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static void populate_defaults(struct ast_vm_user *vmu)
{
	ast_copy_flags(vmu, (&globalflags), AST_FLAGS_ALL);	
	if (saydurationminfo)
		vmu->saydurationm = saydurationminfo;
	if (callcontext)
		ast_copy_string(vmu->callback, callcontext, sizeof(vmu->callback));
	if (dialcontext)
		ast_copy_string(vmu->dialout, dialcontext, sizeof(vmu->dialout));
	if (exitcontext)
		ast_copy_string(vmu->exit, exitcontext, sizeof(vmu->exit));
	if (maxmsg)
		vmu->maxmsg = maxmsg;
}

static void apply_option(struct ast_vm_user *vmu, const char *var, const char *value)
{
	int x;
	if (!strcasecmp(var, "attach")) {
		ast_set2_flag(vmu, ast_true(value), VM_ATTACH);	
	} else if (!strcasecmp(var, "serveremail")) {
		ast_copy_string(vmu->serveremail, value, sizeof(vmu->serveremail));
	} else if (!strcasecmp(var, "language")) {
		ast_copy_string(vmu->language, value, sizeof(vmu->language));
	} else if (!strcasecmp(var, "tz")) {
		ast_copy_string(vmu->zonetag, value, sizeof(vmu->zonetag));
	} else if (!strcasecmp(var, "delete")) {
		ast_set2_flag(vmu, ast_true(value), VM_DELETE);	
	} else if (!strcasecmp(var, "saycid")){
		ast_set2_flag(vmu, ast_true(value), VM_SAYCID);	
	} else if (!strcasecmp(var,"sendvoicemail")){
		ast_set2_flag(vmu, ast_true(value), VM_SVMAIL);	
	} else if (!strcasecmp(var, "review")){
		ast_set2_flag(vmu, ast_true(value), VM_REVIEW);	
	} else if (!strcasecmp(var, "operator")){
		ast_set2_flag(vmu, ast_true(value), VM_OPERATOR);	
	} else if (!strcasecmp(var, "envelope")){
		ast_set2_flag(vmu, ast_true(value), VM_ENVELOPE);	
	} else if (!strcasecmp(var, "sayduration")){
		ast_set2_flag(vmu, ast_true(value), VM_SAYDURATION);	
	} else if (!strcasecmp(var, "saydurationm")){
		if (sscanf(value, "%d", &x) == 1) {
			vmu->saydurationm = x;
		} else {
			ast_log(LOG_WARNING, "Invalid min duration for say duration\n");
		}
	} else if (!strcasecmp(var, "forcename")){
		ast_set2_flag(vmu, ast_true(value), VM_FORCENAME);	
	} else if (!strcasecmp(var, "forcegreetings")){
		ast_set2_flag(vmu, ast_true(value), VM_FORCEGREET);	
	} else if (!strcasecmp(var, "callback")) {
		ast_copy_string(vmu->callback, value, sizeof(vmu->callback));
	} else if (!strcasecmp(var, "dialout")) {
		ast_copy_string(vmu->dialout, value, sizeof(vmu->dialout));
	} else if (!strcasecmp(var, "exitcontext")) {
		ast_copy_string(vmu->exit, value, sizeof(vmu->exit));
	} else if (!strcasecmp(var, "maxmsg")) {
		vmu->maxmsg = atoi(value);
 		if (vmu->maxmsg <= 0) {
			ast_log(LOG_WARNING, "Invalid number of messages per folder maxmsg=%s. Using default value %i\n", value, MAXMSG);
			vmu->maxmsg = MAXMSG;
		} else if (vmu->maxmsg > MAXMSGLIMIT) {
			ast_log(LOG_WARNING, "Maximum number of messages per folder is %i. Cannot accept value maxmsg=%s\n", MAXMSGLIMIT, value);
			vmu->maxmsg = MAXMSGLIMIT;
		}
	} else if (!strcasecmp(var, "options")) {
		apply_options(vmu, value);
	}
}

static int change_password_realtime(struct ast_vm_user *vmu, const char *password)
{
	int res;
	if (!ast_strlen_zero(vmu->uniqueid)) {
		res = ast_update_realtime("voicemail", "uniqueid", vmu->uniqueid, "password", password, NULL);
		if (res > 0) {
			ast_copy_string(vmu->password, password, sizeof(vmu->password));
			res = 0;
		} else if (!res) {
			res = -1;
		}
		return res;
	}
	return -1;
}

static void apply_options(struct ast_vm_user *vmu, const char *options)
{	/* Destructively Parse options and apply */
	char *stringp;
	char *s;
	char *var, *value;
	stringp = ast_strdupa(options);
	while ((s = strsep(&stringp, "|"))) {
		value = s;
		if ((var = strsep(&value, "=")) && value) {
			apply_option(vmu, var, value);
		}
	}	
}

static struct ast_vm_user *find_user_realtime(struct ast_vm_user *ivm, const char *context, const char *mailbox)
{
	struct ast_variable *var, *tmp;
	struct ast_vm_user *retval;

	if (ivm)
		retval=ivm;
	else
		retval=malloc(sizeof(struct ast_vm_user));

	if (retval) {
		memset(retval, 0, sizeof(struct ast_vm_user));
		if (!ivm)
			ast_set_flag(retval, VM_ALLOCED);	
		if (mailbox) 
			ast_copy_string(retval->mailbox, mailbox, sizeof(retval->mailbox));
		if (context) 
			ast_copy_string(retval->context, context, sizeof(retval->context));
		else
			strcpy(retval->context, "default");
		populate_defaults(retval);
		var = ast_load_realtime("voicemail", "mailbox", mailbox, "context", retval->context, NULL);
		if (var) {
			tmp = var;
			while(tmp) {
				printf("%s => %s\n", tmp->name, tmp->value);
				if (!strcasecmp(tmp->name, "password")) {
					ast_copy_string(retval->password, tmp->value, sizeof(retval->password));
				} else if (!strcasecmp(tmp->name, "uniqueid")) {
					ast_copy_string(retval->uniqueid, tmp->value, sizeof(retval->uniqueid));
				} else if (!strcasecmp(tmp->name, "pager")) {
					ast_copy_string(retval->pager, tmp->value, sizeof(retval->pager));
				} else if (!strcasecmp(tmp->name, "email")) {
					ast_copy_string(retval->email, tmp->value, sizeof(retval->email));
				} else if (!strcasecmp(tmp->name, "fullname")) {
					ast_copy_string(retval->fullname, tmp->value, sizeof(retval->fullname));
				} else
					apply_option(retval, tmp->name, tmp->value);
				tmp = tmp->next;
			} 
		} else { 
			if (!ivm) 
				free(retval);
			retval = NULL;
		}	
	} 
	return retval;
}

static struct ast_vm_user *find_user(struct ast_vm_user *ivm, const char *context, const char *mailbox)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *vmu=NULL, *cur;
	ast_mutex_lock(&vmlock);
	cur = users;
	while (cur) {
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
			ast_set2_flag(vmu, !ivm, VM_ALLOCED);	
			vmu->next = NULL;
		}
	} else
		vmu = find_user_realtime(ivm, context, mailbox);
	ast_mutex_unlock(&vmlock);
	return vmu;
}

static int reset_user_pw(const char *context, const char *mailbox, const char *newpass)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *cur;
	int res = -1;
	ast_mutex_lock(&vmlock);
	cur = users;
	while (cur) {
		if ((!context || !strcasecmp(context, cur->context)) &&
			(!strcasecmp(mailbox, cur->mailbox)))
				break;
		cur=cur->next;
	}
	if (cur) {
		ast_copy_string(cur->password, newpass, sizeof(cur->password));
		res = 0;
	}
	ast_mutex_unlock(&vmlock);
	return res;
}

static void vm_change_password(struct ast_vm_user *vmu, const char *newpassword)
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
	struct stat statbuf;

	if (!change_password_realtime(vmu, newpassword))
		return;

	snprintf(tmpin, sizeof(tmpin), "%s/voicemail.conf", ast_config_AST_CONFIG_DIR);
	snprintf(tmpout, sizeof(tmpout), "%s/voicemail.conf.new", ast_config_AST_CONFIG_DIR);
	configin = fopen(tmpin,"r");
	if (configin)
		configout = fopen(tmpout,"w+");
	else
		configout = NULL;
	if (!configin || !configout) {
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
			char *user = NULL, *pass = NULL, *rest = NULL, *trim = NULL,
				*comment = NULL, *tmpctx = NULL, *tmpctxend = NULL;
			
			/* Make a backup of it */
			ast_copy_string(orig, inbuf, sizeof(orig));
			
			/*
				Read the file line by line, split each line into a comment and command section
				only parse the command portion of the line
			*/
			if (inbuf[strlen(inbuf) - 1] == '\n')
				inbuf[strlen(inbuf) - 1] = '\0';
			comment = strchr(inbuf, ';');
			if (comment) {
				*comment = '\0'; /* Now inbuf is terminated just before the comment */
				comment++;
			}
			
			if (inbuf[0] != '\0') { /* skip over parsing for lines starting with a comment character and empty lines */
				/* Check for a context, first '[' to first ']' */
				tmpctx = strchr(inbuf, '[');
				if (tmpctx) {
					tmpctxend = strchr(inbuf, ']');
					if (tmpctxend && (tmpctxend > tmpctx)) {
						/* Valid context */
						ast_copy_string(currcontext, tmpctx + 1, tmpctxend - tmpctx - 1);
						currcontext[tmpctxend - tmpctx - 1] = '\0';
					} else {
						tmpctx = NULL;
					}
				}
				
				if (!tmpctx) {
					/* This isn't a context line, check for MBX => PSWD... */
					user = inbuf;
					pass = strchr(user, '=');
					if(pass > user) {
						/* We have a line in the form of aaaaa=aaaaaa */
						*pass = '\0';
						pass++;
						
						/* Trim whitespace from user */
						trim = pass - 2;
						while (*trim && *trim < 33) {
							*trim = '\0';
							trim--;
						}
						
						/* Trim whitespace and '>' from pass */
						if (*pass == '>') {
							*pass = '\0';
							pass++;
						}
						while (*pass && *pass < 33) {
							*pass = '\0';
							pass++;
						}
						
						/* 
						   Since no whitespace allowed in fields, or more correctly white space
						   inside the fields is there for a purpose, we can just terminate pass
						   at the comma or EOL whichever comes first.
						*/
						trim = strchr(pass, ',');
						if (trim) {
							*trim = '\0';
							rest = trim + 1;
						} else {
							rest = NULL;
						}
					} else {
						user = NULL;
						pass = NULL;
						rest = NULL;
					}
				}
			}			
			
			/* Compare user, pass AND context */
			if (user && *user && !strcmp(user, vmu->mailbox) &&
				 pass && !strcmp(pass, vmu->password) &&
				 currcontext && *currcontext && !strcmp(currcontext, vmu->context)) {
				
				/* Write */
				/* This is the line */
				if (rest) {
					fprintf(configout, "%s => %s,%s", vmu->mailbox,newpassword,rest);
				} else {
					fprintf(configout, "%s => %s", vmu->mailbox,newpassword);
				}
				/* If there was a comment on the line print it out */
				if (comment) {
					fprintf(configout, ";%s\n", comment);
				} else {
					fprintf(configout, "\n");
				}
			} else {
				/* Put it back like it was */
				fprintf(configout, "%s", orig);
			}
		}
	}
	fclose(configin);
	fclose(configout);

	stat((char *)tmpin, &statbuf);
	chmod((char *)tmpout, statbuf.st_mode);
	chown((char *)tmpout, statbuf.st_uid, statbuf.st_gid);
	unlink((char *)tmpin);
	rename((char *)tmpout,(char *)tmpin);
	reset_user_pw(vmu->context, vmu->mailbox, newpassword);
	ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
}

static void vm_change_password_shell(struct ast_vm_user *vmu, char *newpassword)
{
	char buf[255];
	snprintf(buf,255,"%s %s %s %s",ext_pass_cmd,vmu->context,vmu->mailbox,newpassword);
	if (!ast_safe_system(buf))
		ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
}

static int make_dir(char *dest, int len, char *context, char *ext, char *mailbox)
{
	return snprintf(dest, len, "%s%s/%s/%s", VM_SPOOL_DIR, context, ext, mailbox);
}

static int make_file(char *dest, int len, char *dir, int num)
{
	return snprintf(dest, len, "%s/msg%04d", dir, num);
}

/* only return failure if ast_lock_path returns 'timeout',
   not if the path does not exist or any other reason
*/
static int vm_lock_path(const char *path)
{
	switch (ast_lock_path(path)) {
	case AST_LOCK_TIMEOUT:
		return -1;
	default:
		return 0;
	}
}


#ifdef USE_ODBC_STORAGE
static int retrieve_file(char *dir, int msgnum)
{
	int x = 0;
	int res;
	int fd=-1;
	size_t fdlen = 0;
	void *fdm=NULL;
	SQLSMALLINT colcount=0;
	SQLHSTMT stmt;
	char sql[256];
	char fmt[80]="";
	char *c;
	char coltitle[256];
	SQLSMALLINT collen;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	SQLULEN colsize;
	FILE *f=NULL;
	char rowdata[80];
	char fn[256];
	char full_fn[256];
	char msgnums[80];
	
	odbc_obj *obj;
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		ast_copy_string(fmt, vmfmts, sizeof(fmt));
		c = strchr(fmt, '|');
		if (c)
			*c = '\0';
		if (!strcasecmp(fmt, "wav49"))
			strcpy(fmt, "WAV");
		snprintf(msgnums, sizeof(msgnums),"%d", msgnum);
		if (msgnum > -1)
			make_file(fn, sizeof(fn), dir, msgnum);
		else
			ast_copy_string(fn, dir, sizeof(fn));
		snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
		f = fopen(full_fn, "w+");
		snprintf(full_fn, sizeof(full_fn), "%s.%s", fn, fmt);
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
		snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE dir=? AND msgnum=?",odbc_table);
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(dir), 0, (void *)dir, 0, NULL);
		SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if (res == SQL_NO_DATA) {
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		else if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		fd = open(full_fn, O_RDWR | O_CREAT | O_TRUNC);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Failed to write '%s': %s\n", full_fn, strerror(errno));
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLNumResultCols(stmt, &colcount);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {	
			ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		if (f) 
			fprintf(f, "[message]\n");
		for (x=0;x<colcount;x++) {
			rowdata[0] = '\0';
			collen = sizeof(coltitle);
			res = SQLDescribeCol(stmt, x + 1, coltitle, sizeof(coltitle), &collen, 
						&datatype, &colsize, &decimaldigits, &nullable);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
				SQLFreeHandle (SQL_HANDLE_STMT, stmt);
				goto yuck;
			}
			if (!strcasecmp(coltitle, "recording")) {
				res = SQLGetData(stmt, x + 1, SQL_BINARY, NULL, 0, &colsize);
				fdlen = colsize;
				fd = open(full_fn, O_RDWR | O_TRUNC | O_CREAT, 0770);
				if (fd > -1) {
					char tmp[1]="";
					lseek(fd, fdlen - 1, SEEK_SET);
					if (write(fd, tmp, 1) != 1) {
						close(fd);
						fd = -1;
					}
					if (fd > -1)
						fdm = mmap(NULL, fdlen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
				}
				if (fdm) {
					memset(fdm, 0, fdlen);
					res = SQLGetData(stmt, x + 1, SQL_BINARY, fdm, fdlen, &colsize);
					if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
						ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
						SQLFreeHandle (SQL_HANDLE_STMT, stmt);
						goto yuck;
					}
				}
			} else {
				res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
				if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
					ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
					SQLFreeHandle (SQL_HANDLE_STMT, stmt);
					goto yuck;
				}
				if (strcasecmp(coltitle, "msgnum") && strcasecmp(coltitle, "dir") && f)
					fprintf(f, "%s=%s\n", coltitle, rowdata);
			}
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:	
	if (f)
		fclose(f);
	if (fdm)
		munmap(fdm, fdlen);
	if (fd > -1)
		close(fd);
	return x - 1;
}

static int remove_file(char *dir, int msgnum)
{
	char fn[256];
	char full_fn[256];
	char msgnums[80];
	
	if (msgnum > -1) {
		snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
		make_file(fn, sizeof(fn), dir, msgnum);
	} else
		ast_copy_string(fn, dir, sizeof(fn));
	ast_filedelete(fn, NULL);	
	snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
	unlink(full_fn);
	return 0;
}

static int last_message_index(struct ast_vm_user *vmu, char *dir)
{
	int x = 0;
	int res;
	SQLHSTMT stmt;
	char sql[256];
	char rowdata[20];
	
	odbc_obj *obj;
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir=?",odbc_table);
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(dir), 0, (void *)dir, 0, NULL);
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		if (sscanf(rowdata, "%d", &x) != 1)
			ast_log(LOG_WARNING, "Failed to read message count!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:	
	return x - 1;
}

static int message_exists(char *dir, int msgnum)
{
	int x = 0;
	int res;
	SQLHSTMT stmt;
	char sql[256];
	char rowdata[20];
	char msgnums[20];
	
	odbc_obj *obj;
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir=? AND msgnum=?",odbc_table);
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(dir), 0, (void *)dir, 0, NULL);
		SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		if (sscanf(rowdata, "%d", &x) != 1)
			ast_log(LOG_WARNING, "Failed to read message count!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:	
	return x;
}

static int count_messages(struct ast_vm_user *vmu, char *dir)
{
	return last_message_index(vmu, dir) + 1;
}

static void delete_file(char *sdir, int smsg)
{
	int res;
	SQLHSTMT stmt;
	char sql[256];
	char msgnums[20];
	
	odbc_obj *obj;
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", smsg);
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
		snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE dir=? AND msgnum=?",odbc_table);
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(sdir), 0, (void *)sdir, 0, NULL);
		SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	return;	
}

static void copy_file(char *sdir, int smsg, char *ddir, int dmsg, char *dmailboxuser, char *dmailboxcontext)
{
	int res;
	SQLHSTMT stmt;
	char sql[512];
	char msgnums[20];
	char msgnumd[20];
	odbc_obj *obj;

	delete_file(ddir, dmsg);
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", smsg);
		snprintf(msgnumd, sizeof(msgnumd), "%d", dmsg);
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
#ifdef EXTENDED_ODBC_STORAGE
		snprintf(sql, sizeof(sql), "INSERT INTO %s (dir, msgnum, context, macrocontext, callerid, origtime, duration, recording, mailboxuser, mailboxcontext) SELECT ?,?,context,macrocontext,callerid,origtime,duration,recording,?,? FROM %s WHERE dir=? AND msgnum=?",odbc_table,odbc_table); 
#else
 		snprintf(sql, sizeof(sql), "INSERT INTO %s (dir, msgnum, context, macrocontext, callerid, origtime, duration, recording) SELECT ?,?,context,macrocontext,callerid,origtime,duration,recording FROM %s WHERE dir=? AND msgnum=?",odbc_table,odbc_table); 
#endif
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(ddir), 0, (void *)ddir, 0, NULL);
		SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnumd), 0, (void *)msgnumd, 0, NULL);
#ifdef EXTENDED_ODBC_STORAGE
		SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(dmailboxuser), 0, (void *)dmailboxuser, 0, NULL);
		SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(dmailboxcontext), 0, (void *)dmailboxcontext, 0, NULL);
		SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(sdir), 0, (void *)sdir, 0, NULL);
		SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
#else
 		SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(sdir), 0, (void *)sdir, 0, NULL);
 		SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
#endif		 
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s] (You probably don't have MySQL 4.1 or later installed)\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	return;	
}

static int store_file(char *dir, char *mailboxuser, char *mailboxcontext, int msgnum)
{
	int x = 0;
	int res;
	int fd = -1;
	void *fdm=NULL;
	size_t fdlen = -1;
	SQLHSTMT stmt;
	SQLINTEGER len;
	char sql[256];
	char msgnums[20];
	char fn[256];
	char full_fn[256];
	char fmt[80]="";
	char *c;
	char *context="", *macrocontext="", *callerid="", *origtime="", *duration="";
	char *category = "";
	struct ast_config *cfg=NULL;
	odbc_obj *obj;

	delete_file(dir, msgnum);
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		ast_copy_string(fmt, vmfmts, sizeof(fmt));
		c = strchr(fmt, '|');
		if (c)
			*c = '\0';
		if (!strcasecmp(fmt, "wav49"))
			strcpy(fmt, "WAV");
		snprintf(msgnums, sizeof(msgnums),"%d", msgnum);
		if (msgnum > -1)
			make_file(fn, sizeof(fn), dir, msgnum);
		else
			ast_copy_string(fn, dir, sizeof(fn));
		snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
		cfg = ast_config_load(full_fn);
		snprintf(full_fn, sizeof(full_fn), "%s.%s", fn, fmt);
		fd = open(full_fn, O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Open of sound file '%s' failed: %s\n", full_fn, strerror(errno));
			goto yuck;
		}
		if (cfg) {
			context = ast_variable_retrieve(cfg, "message", "context");
			if (!context) context = "";
			macrocontext = ast_variable_retrieve(cfg, "message", "macrocontext");
			if (!macrocontext) macrocontext = "";
			callerid = ast_variable_retrieve(cfg, "message", "callerid");
			if (!callerid) callerid = "";
			origtime = ast_variable_retrieve(cfg, "message", "origtime");
			if (!origtime) origtime = "";
			duration = ast_variable_retrieve(cfg, "message", "duration");
			if (!duration) duration = "";
			category = ast_variable_retrieve(cfg, "message", "category");
			if (!category) category = "";
		}
		fdlen = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		printf("Length is %d\n", fdlen);
		fdm = mmap(NULL, fdlen, PROT_READ | PROT_WRITE, MAP_SHARED,fd, 0);
		if (!fdm) {
			ast_log(LOG_WARNING, "Memory map failed!\n");
			goto yuck;
		} 
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
 		if (!ast_strlen_zero(category)) 
#ifdef EXTENDED_ODBC_STORAGE
			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,macrocontext,callerid,origtime,duration,mailboxuser,mailboxcontext,category) VALUES (?,?,?,?,?,?,?,?,?,?,?)",odbc_table); 
#else
 			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,macrocontext,callerid,origtime,duration,category) VALUES (?,?,?,?,?,?,?,?,?)",odbc_table);
#endif
 		else
#ifdef EXTENDED_ODBC_STORAGE
			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,macrocontext,callerid,origtime,duration,mailboxuser,mailboxcontext) VALUES (?,?,?,?,?,?,?,?,?,?)",odbc_table);
#else
 			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,macrocontext,callerid,origtime,duration) VALUES (?,?,?,?,?,?,?,?)",odbc_table);
#endif
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		len = fdlen; /* SQL_LEN_DATA_AT_EXEC(fdlen); */
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(dir), 0, (void *)dir, 0, NULL);
		SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
		SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_BINARY, fdlen, 0, (void *)fdm, fdlen, &len);
		SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(context), 0, (void *)context, 0, NULL);
		SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(macrocontext), 0, (void *)macrocontext, 0, NULL);
		SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(callerid), 0, (void *)callerid, 0, NULL);
		SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(origtime), 0, (void *)origtime, 0, NULL);
		SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(duration), 0, (void *)duration, 0, NULL);
#ifdef EXTENDED_ODBC_STORAGE
		SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(mailboxuser), 0, (void *)mailboxuser, 0, NULL);
		SQLBindParameter(stmt, 10, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(mailboxcontext), 0, (void *)mailboxcontext, 0, NULL);
		if (!ast_strlen_zero(category))
			SQLBindParameter(stmt, 11, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(category), 0, (void *)category, 0, NULL);
#else
 		if (!ast_strlen_zero(category))
 			SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(category), 0, (void *)category, 0, NULL);
#endif
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:	
	if (cfg)
		ast_config_destroy(cfg);
	if (fdm)
		munmap(fdm, fdlen);
	if (fd > -1)
		close(fd);
	return x;
}

static void rename_file(char *sdir, int smsg, char *mailboxuser, char *mailboxcontext, char *ddir, int dmsg)
{
	int res;
	SQLHSTMT stmt;
	char sql[256];
	char msgnums[20];
	char msgnumd[20];
	odbc_obj *obj;

	delete_file(ddir, dmsg);
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", smsg);
		snprintf(msgnumd, sizeof(msgnumd), "%d", dmsg);
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
#ifdef EXTENDED_ODBC_STORAGE
		snprintf(sql, sizeof(sql), "UPDATE %s SET dir=?, msgnum=?, mailboxuser=?, mailboxcontext=? WHERE dir=? AND msgnum=?",odbc_table);
#else
 		snprintf(sql, sizeof(sql), "UPDATE %s SET dir=?, msgnum=? WHERE dir=? AND msgnum=?",odbc_table);
#endif
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(ddir), 0, (void *)ddir, 0, NULL);
		SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnumd), 0, (void *)msgnumd, 0, NULL);
#ifdef EXTENDED_ODBC_STORAGE
		SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(mailboxuser), 0, (void *)mailboxuser, 0, NULL);
		SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(mailboxcontext), 0, (void *)mailboxcontext, 0, NULL);
		SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(sdir), 0, (void *)sdir, 0, NULL);
		SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
#else
 		SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(sdir), 0, (void *)sdir, 0, NULL);
 		SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(msgnums), 0, (void *)msgnums, 0, NULL);
#endif		 
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	return;	
}

#else

static int count_messages(struct ast_vm_user *vmu, char *dir)
{
	/* Find all .txt files - even if they are not in sequence from 0000 */

	int vmcount = 0;
	DIR *vmdir = NULL;
	struct dirent *vment = NULL;

	if (vm_lock_path(dir))
		return ERROR_LOCK_PATH;

	if ((vmdir = opendir(dir))) {
		while ((vment = readdir(vmdir))) {
			if (strlen(vment->d_name) > 7 && !strncmp(vment->d_name + 7, ".txt", 4)) 
				vmcount++;
		}
		closedir(vmdir);
	}
	ast_unlock_path(dir);
	
	return vmcount;
}

static void rename_file(char *sfn, char *dfn)
{
	char stxt[256];
	char dtxt[256];
	ast_filerename(sfn,dfn,NULL);
	snprintf(stxt, sizeof(stxt), "%s.txt", sfn);
	snprintf(dtxt, sizeof(dtxt), "%s.txt", dfn);
	rename(stxt, dtxt);
}

static int copy(char *infile, char *outfile)
{
	int ifd;
	int ofd;
	int res;
	int len;
	char buf[4096];

#ifdef HARDLINK_WHEN_POSSIBLE
	/* Hard link if possible; saves disk space & is faster */
	if (link(infile, outfile)) {
#endif
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
				if (errno == ENOMEM || errno == ENOSPC || res != len) {
					ast_log(LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, res, len, strerror(errno));
					close(ifd);
					close(ofd);
					unlink(outfile);
				}
			}
		} while (len);
		close(ifd);
		close(ofd);
		return 0;
#ifdef HARDLINK_WHEN_POSSIBLE
	} else {
		/* Hard link succeeded */
		return 0;
	}
#endif
}

static void copy_file(char *frompath, char *topath)
{
	char frompath2[256],topath2[256];
	ast_filecopy(frompath, topath, NULL);
	snprintf(frompath2, sizeof(frompath2), "%s.txt", frompath);
	snprintf(topath2, sizeof(topath2), "%s.txt", topath);
	copy(frompath2, topath2);
}

/*
 * A negative return value indicates an error.
 */
static int last_message_index(struct ast_vm_user *vmu, char *dir)
{
	int x;
	char fn[256];

	if (vm_lock_path(dir))
		return ERROR_LOCK_PATH;

	for (x = 0; x < vmu->maxmsg; x++) {
		make_file(fn, sizeof(fn), dir, x);
		if (ast_fileexists(fn, NULL, NULL) < 1)
			break;
	}
	ast_unlock_path(dir);

	return x - 1;
}

static int vm_delete(char *file)
{
	char *txt;
	int txtsize = 0;

	txtsize = (strlen(file) + 5)*sizeof(char);
	txt = (char *)alloca(txtsize);
	/* Sprintf here would safe because we alloca'd exactly the right length,
	 * but trying to eliminate all sprintf's anyhow
	 */
	snprintf(txt, txtsize, "%s.txt", file);
	unlink(txt);
	return ast_filedelete(file, NULL);
}


#endif
static int
inbuf(struct baseio *bio, FILE *fi)
{
	int l;

	if (bio->ateof)
		return 0;

	if ((l = fread(bio->iobuf,1,BASEMAXINLINE,fi)) <= 0) {
		if (ferror(fi))
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
	if (bio->iocp>=bio->iolen) {
		if (!inbuf(bio, fi))
			return EOF;
	}

	return bio->iobuf[bio->iocp++];
}

static int
ochar(struct baseio *bio, int c, FILE *so)
{
	if (bio->linelength>=BASELINELEN) {
		if (fputs(eol,so)==EOF)
			return -1;

		bio->linelength= 0;
	}

	if (putc(((unsigned char)c),so)==EOF)
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

	if (!(fi = fopen(filename, "rb"))) {
		ast_log(LOG_WARNING, "Failed to open log file: %s: %s\n", filename, strerror(errno));
		return -1;
	}

	for (i= 0;i<9;i++) {
		dtable[i]= 'A'+i;
		dtable[i+9]= 'J'+i;
		dtable[26+i]= 'a'+i;
		dtable[26+i+9]= 'j'+i;
	}
	for (i= 0;i<8;i++) {
		dtable[i+18]= 'S'+i;
		dtable[26+i+18]= 's'+i;
	}
	for (i= 0;i<10;i++) {
		dtable[52+i]= '0'+i;
	}
	dtable[62]= '+';
	dtable[63]= '/';

	while (!hiteof){
		unsigned char igroup[3],ogroup[4];
		int c,n;

		igroup[0]= igroup[1]= igroup[2]= 0;

		for (n= 0;n<3;n++) {
			if ((c = inchar(&bio, fi)) == EOF) {
				hiteof= 1;
				break;
			}

			igroup[n]= (unsigned char)c;
		}

		if (n> 0) {
			ogroup[0]= dtable[igroup[0]>>2];
			ogroup[1]= dtable[((igroup[0]&3)<<4)|(igroup[1]>>4)];
			ogroup[2]= dtable[((igroup[1]&0xF)<<2)|(igroup[2]>>6)];
			ogroup[3]= dtable[igroup[2]&0x3F];

			if (n<3) {
				ogroup[3]= '=';

				if (n<2)
					ogroup[2]= '=';
			}

			for (i= 0;i<4;i++)
				ochar(&bio, ogroup[i], so);
		}
	}

	if (fputs(eol,so)==EOF)
		return 0;

	fclose(fi);

	return 1;
}

static void prep_email_sub_vars(struct ast_channel *ast, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, char *cidnum, char *cidname, char *dur, char *date, char *passdata, size_t passdatasize)
{
	char callerid[256];
	/* Prepare variables for substition in email body and subject */
	pbx_builtin_setvar_helper(ast, "VM_NAME", vmu->fullname);
	pbx_builtin_setvar_helper(ast, "VM_DUR", dur);
	snprintf(passdata, passdatasize, "%d", msgnum);
	pbx_builtin_setvar_helper(ast, "VM_MSGNUM", passdata);
	pbx_builtin_setvar_helper(ast, "VM_CONTEXT", context);
	pbx_builtin_setvar_helper(ast, "VM_MAILBOX", mailbox);
	pbx_builtin_setvar_helper(ast, "VM_CALLERID", ast_callerid_merge(callerid, sizeof(callerid), cidname, cidnum, "Unknown Caller"));
	pbx_builtin_setvar_helper(ast, "VM_CIDNAME", (cidname ? cidname : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_CIDNUM", (cidnum ? cidnum : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_DATE", date);
}

static int sendmail(char *srcemail, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, char *cidnum, char *cidname, char *attach, char *format, int duration, int attach_user_voicemail)
{
	FILE *p=NULL;
	int pfd;
	char date[256];
	char host[MAXHOSTNAMELEN] = "";
	char who[256];
	char bound[256];
	char fname[256];
	char dur[256];
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[256];
	time_t t;
	struct tm tm;
	struct vm_zone *the_zone = NULL;
	if (vmu && ast_strlen_zero(vmu->email)) {
		ast_log(LOG_WARNING, "E-mail address missing for mailbox [%s].  E-mail will not be sent.\n", vmu->mailbox);
		return(0);
	}
	if (!strcmp(format, "wav49"))
		format = "WAV";
	ast_log(LOG_DEBUG, "Attaching file '%s', format '%s', uservm is '%d', global is %d\n", attach, format, attach_user_voicemail, ast_test_flag((&globalflags), VM_ATTACH));
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
		gethostname(host, sizeof(host)-1);
		if (strchr(srcemail, '@'))
			ast_copy_string(who, srcemail, sizeof(who));
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
		time(&t);

		/* Does this user have a timezone specified? */
		if (!ast_strlen_zero(vmu->zonetag)) {
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

		/* Set date format for voicemail mail */
		strftime(date, sizeof(date), emaildateformat, &tm);

		if (*fromstring) {
			struct ast_channel *ast = ast_channel_alloc(0);
			if (ast) {
				char *passdata;
				int vmlen = strlen(fromstring)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast,vmu,msgnum + 1,context,mailbox,cidnum, cidname,dur,date,passdata, vmlen);
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
					prep_email_sub_vars(ast,vmu,msgnum + 1,context,mailbox,cidnum, cidname,dur,date,passdata, vmlen);
					pbx_substitute_variables_helper(ast,emailsubject,passdata,vmlen);
					fprintf(p, "Subject: %s\n",passdata);
				} else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else
		if (*emailtitle) {
			fprintf(p, emailtitle, msgnum + 1, mailbox) ;
			fprintf(p,"\n") ;
		} else if (ast_test_flag((&globalflags), VM_PBXSKIP))
			fprintf(p, "Subject: New message %d in mailbox %s\n", msgnum + 1, mailbox);
		else
			fprintf(p, "Subject: [PBX]: New message %d in mailbox %s\n", msgnum + 1, mailbox);
		fprintf(p, "Message-ID: <Asterisk-%d-%d-%s-%d@%s>\n", msgnum, (unsigned int)rand(), mailbox, getpid(), host);
		fprintf(p, "MIME-Version: 1.0\n");
		if (attach_user_voicemail) {
			/* Something unique. */
			snprintf(bound, sizeof(bound), "voicemail_%d%s%d%d", msgnum, mailbox, getpid(), (unsigned int)rand());

			fprintf(p, "Content-Type: multipart/mixed; boundary=\"%s\"\n\n\n", bound);

			fprintf(p, "--%s\n", bound);
		}
		fprintf(p, "Content-Type: text/plain; charset=%s\nContent-Transfer-Encoding: 8bit\n\n", charset);
		if (emailbody) {
			struct ast_channel *ast = ast_channel_alloc(0);
			if (ast) {
				char *passdata;
				int vmlen = strlen(emailbody)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast,vmu,msgnum + 1,context,mailbox,cidnum, cidname,dur,date,passdata, vmlen);
					pbx_substitute_variables_helper(ast,emailbody,passdata,vmlen);
					fprintf(p, "%s\n",passdata);
				} else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else {
			fprintf(p, "Dear %s:\n\n\tJust wanted to let you know you were just left a %s long message (number %d)\n"

			"in mailbox %s from %s, on %s so you might\n"
			"want to check it when you get a chance.  Thanks!\n\n\t\t\t\t--Asterisk\n\n", vmu->fullname, 
			dur, msgnum + 1, mailbox, (cidname ? cidname : (cidnum ? cidnum : "an unknown caller")), date);
		}
		if (attach_user_voicemail) {
			/* Eww. We want formats to tell us their own MIME type */
			char *ctype = "audio/x-";
			if (!strcasecmp(format, "ogg"))
				ctype = "application/";
		
			fprintf(p, "--%s\n", bound);
			fprintf(p, "Content-Type: %s%s; name=\"msg%04d.%s\"\n", ctype, format, msgnum, format);
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
		ast_log(LOG_DEBUG, "Sent mail to %s with command '%s'\n", vmu->email, mailcmd);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", mailcmd);
		return -1;
	}
	return 0;
}

static int sendpage(char *srcemail, char *pager, int msgnum, char *context, char *mailbox, char *cidnum, char *cidname, int duration, struct ast_vm_user *vmu)
{
	FILE *p=NULL;
	int pfd;
	char date[256];
	char host[MAXHOSTNAMELEN]="";
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
		gethostname(host, sizeof(host)-1);
		if (strchr(srcemail, '@'))
			ast_copy_string(who, srcemail, sizeof(who));
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
		time(&t);

		/* Does this user have a timezone specified? */
		if (!ast_strlen_zero(vmu->zonetag)) {
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

		if (*pagerfromstring) {
			struct ast_channel *ast = ast_channel_alloc(0);
			if (ast) {
				char *passdata;
				int vmlen = strlen(fromstring)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast,vmu,msgnum + 1,context,mailbox,cidnum, cidname,dur,date,passdata, vmlen);
					pbx_substitute_variables_helper(ast,pagerfromstring,passdata,vmlen);
					fprintf(p, "From: %s <%s>\n",passdata,who);
				} else 
					ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else
			fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s\n", pager);
               if (pagersubject) {
                       struct ast_channel *ast = ast_channel_alloc(0);
                       if (ast) {
                               char *passdata;
                               int vmlen = strlen(pagersubject)*3 + 200;
                               if ((passdata = alloca(vmlen))) {
                                       memset(passdata, 0, vmlen);
                                       prep_email_sub_vars(ast,vmu,msgnum + 1,context,mailbox,cidnum, cidname,dur,date,passdata, vmlen);
                                       pbx_substitute_variables_helper(ast,pagersubject,passdata,vmlen);
                                       fprintf(p, "Subject: %s\n\n",passdata);
                               } else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
                               ast_channel_free(ast);
                       } else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
               } else
                       fprintf(p, "Subject: New VM\n\n");
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", &tm);
               if (pagerbody) {
                       struct ast_channel *ast = ast_channel_alloc(0);
                       if (ast) {
                               char *passdata;
                               int vmlen = strlen(pagerbody)*3 + 200;
                               if ((passdata = alloca(vmlen))) {
                                       memset(passdata, 0, vmlen);
                                       prep_email_sub_vars(ast,vmu,msgnum + 1,context,mailbox,cidnum, cidname,dur,date,passdata, vmlen);
                                       pbx_substitute_variables_helper(ast,pagerbody,passdata,vmlen);
                                       fprintf(p, "%s\n",passdata);
                               } else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
                               ast_channel_free(ast);
                       } else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
               } else {
                       fprintf(p, "New %s long msg in box %s\n"
                                       "from %s, on %s", dur, mailbox, (cidname ? cidname : (cidnum ? cidnum : "unknown")), date);
               }
		fclose(p);
		snprintf(tmp2, sizeof(tmp2), "( %s < %s ; rm -f %s ) &", mailcmd, tmp, tmp);
		ast_safe_system(tmp2);
		ast_log(LOG_DEBUG, "Sent page to %s with command '%s'\n", pager, mailcmd);
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
	snprintf(fn, sizeof(fn), "%s%s/%s/greet", VM_SPOOL_DIR, context, ext);
	RETRIEVE(fn, -1);
	if (ast_fileexists(fn, NULL, NULL) > 0) {
		res = ast_streamfile(chan, fn, chan->language);
		if (res) {
			DISPOSE(fn, -1);
			return -1;
		}
		res = ast_waitstream(chan, ecodes);
		if (res) {
			DISPOSE(fn, -1);
			return res;
		}
	} else {
		/* Dispose just in case */
		DISPOSE(fn, -1);
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

static void free_user(struct ast_vm_user *vmu)
{
	if (ast_test_flag(vmu, VM_ALLOCED))
		free(vmu);
}

static void free_zone(struct vm_zone *z)
{
	free(z);
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

#ifdef USE_ODBC_STORAGE
static int messagecount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	int x = 0;
	int res;
	SQLHSTMT stmt;
	char sql[256];
	char rowdata[20];
	char tmp[256]="";
        char *context;

        if (newmsgs)
                *newmsgs = 0;
        if (oldmsgs)
                *oldmsgs = 0;
        /* If no mailbox, return immediately */
        if (ast_strlen_zero(mailbox))
                return 0;

        ast_copy_string(tmp, mailbox, sizeof(tmp));
        
	context = strchr(tmp, '@');
        if (context) {   
                *context = '\0';
                context++;
        } else  
                context = "default";
	
	odbc_obj *obj;
	obj = fetch_odbc_obj(odbc_database, 0);
	if (obj) {
		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir like \"%%%s/%s/%s\"%c", odbc_table, context, tmp, "INBOX", '\0');
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		*newmsgs = atoi(rowdata);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);

		res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
			goto yuck;
		}
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir like \"%%%s/%s/%s\"%c", odbc_table, context, tmp, "Old", '\0');
		res = SQLPrepare(stmt, sql, SQL_NTS);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = odbc_smart_execute(obj, stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			goto yuck;
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		*oldmsgs = atoi(rowdata);
		x = 1;
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		
yuck:	
	return x;
}

static int has_voicemail(const char *mailbox, const char *folder)
{
	int nummsgs = 0;
        int res;
        SQLHSTMT stmt;
        char sql[256];
        char rowdata[20];
        char tmp[256]="";
        char *context;
	if (!folder)
                folder = "INBOX";
	/* If no mailbox, return immediately */
        if (ast_strlen_zero(mailbox))
                return 0;

	ast_copy_string(tmp, mailbox, sizeof(tmp));
                        
        context = strchr(tmp, '@');
        if (context) {
                *context = '\0';
                context++;
        } else
                context = "default";

        odbc_obj *obj;
        obj = fetch_odbc_obj(odbc_database, 0);
        if (obj) {
                res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
                if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
                        ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
                        goto yuck;
                }
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir like \"%%%s/%s/%s\"%c", odbc_table, context, tmp, "INBOX", '\0');
                res = SQLPrepare(stmt, sql, SQL_NTS);
                if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {  
                        ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", sql);
                        SQLFreeHandle (SQL_HANDLE_STMT, stmt);
                        goto yuck;
                }
                res = odbc_smart_execute(obj, stmt);
                if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
                        ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
                        SQLFreeHandle (SQL_HANDLE_STMT, stmt);
                        goto yuck;
                }
                res = SQLFetch(stmt);
                if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
                        ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
                        SQLFreeHandle (SQL_HANDLE_STMT, stmt);
                        goto yuck;
                }
                res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
                if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
                        ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
                        SQLFreeHandle (SQL_HANDLE_STMT, stmt);
                        goto yuck;
                }
                nummsgs = atoi(rowdata);
                SQLFreeHandle (SQL_HANDLE_STMT, stmt);
       } else
                ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);

yuck:
	if (nummsgs>=1)
		return 1;
	else
		return 0;
}

#else

static int has_voicemail(const char *mailbox, const char *folder)
{
	DIR *dir;
	struct dirent *de;
	char fn[256];
	char tmp[256]="";
	char *mb, *cur;
	char *context;
	int ret;
	if (!folder)
		folder = "INBOX";
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;
	if (strchr(mailbox, ',')) {
		ast_copy_string(tmp, mailbox, sizeof(tmp));
		mb = tmp;
		ret = 0;
		while((cur = strsep(&mb, ","))) {
			if (!ast_strlen_zero(cur)) {
				if (has_voicemail(cur, folder))
					return 1; 
			}
		}
		return 0;
	}
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	} else
		context = "default";
	snprintf(fn, sizeof(fn), "%s/%s/%s/%s", VM_SPOOL_DIR, context, tmp, folder);
	dir = opendir(fn);
	if (!dir)
		return 0;
	while ((de = readdir(dir))) {
		if (!strncasecmp(de->d_name, "msg", 3))
			break;
	}
	closedir(dir);
	if (de)
		return 1;
	return 0;
}


static int messagecount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	DIR *dir;
	struct dirent *de;
	char fn[256];
	char tmp[256]="";
	char *mb, *cur;
	char *context;
	int ret;
	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;
	if (strchr(mailbox, ',')) {
		int tmpnew, tmpold;
		ast_copy_string(tmp, mailbox, sizeof(tmp));
		mb = tmp;
		ret = 0;
		while((cur = strsep(&mb, ", "))) {
			if (!ast_strlen_zero(cur)) {
				if (messagecount(cur, newmsgs ? &tmpnew : NULL, oldmsgs ? &tmpold : NULL))
					return -1;
				else {
					if (newmsgs)
						*newmsgs += tmpnew; 
					if (oldmsgs)
						*oldmsgs += tmpold;
				}
			}
		}
		return 0;
	}
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	} else
		context = "default";
	if (newmsgs) {
		snprintf(fn, sizeof(fn), "%s/%s/%s/INBOX", VM_SPOOL_DIR, context, tmp);
		dir = opendir(fn);
		if (dir) {
			while ((de = readdir(dir))) {
				if ((strlen(de->d_name) > 3) && !strncasecmp(de->d_name, "msg", 3) &&
					!strcasecmp(de->d_name + strlen(de->d_name) - 3, "txt"))
						(*newmsgs)++;
					
			}
			closedir(dir);
		}
	}
	if (oldmsgs) {
		snprintf(fn, sizeof(fn), "%s/%s/%s/Old", VM_SPOOL_DIR, context, tmp);
		dir = opendir(fn);
		if (dir) {
			while ((de = readdir(dir))) {
				if ((strlen(de->d_name) > 3) && !strncasecmp(de->d_name, "msg", 3) &&
					!strcasecmp(de->d_name + strlen(de->d_name) - 3, "txt"))
						(*oldmsgs)++;
					
			}
			closedir(dir);
		}
	}
	return 0;
}

#endif

static int notify_new_message(struct ast_channel *chan, struct ast_vm_user *vmu, int msgnum, long duration, char *fmt, char *cidnum, char *cidname);

static int copy_message(struct ast_channel *chan, struct ast_vm_user *vmu, int imbox, int msgnum, long duration, struct ast_vm_user *recip, char *fmt)
{
	char fromdir[256], todir[256], frompath[256], topath[256];
	char *frombox = mbox(imbox);
	int recipmsgnum;

	ast_log(LOG_NOTICE, "Copying message from %s@%s to %s@%s\n", vmu->mailbox, vmu->context, recip->mailbox, recip->context);

	make_dir(todir, sizeof(todir), recip->context, "", "");
	/* It's easier just to try to make it than to check for its existence */
	if (mkdir(todir, 0700) && (errno != EEXIST))
		ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", todir, strerror(errno));
	make_dir(todir, sizeof(todir), recip->context, recip->mailbox, "");
	/* It's easier just to try to make it than to check for its existence */
	if (mkdir(todir, 0700) && (errno != EEXIST))
		ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", todir, strerror(errno));
	make_dir(todir, sizeof(todir), recip->context, recip->mailbox, "INBOX");
	if (mkdir(todir, 0700) && (errno != EEXIST))
		ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", todir, strerror(errno));

	make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, frombox);
	make_file(frompath, sizeof(frompath), fromdir, msgnum);

	if (vm_lock_path(topath))
		return ERROR_LOCK_PATH;

	recipmsgnum = 0;
	do {
		make_file(topath, sizeof(topath), todir, recipmsgnum);
		if (!EXISTS(todir, recipmsgnum, topath, chan->language))
			break;
		recipmsgnum++;
	} while (recipmsgnum < recip->maxmsg);
	if (recipmsgnum < recip->maxmsg) {
		COPY(fromdir, msgnum, todir, recipmsgnum, recip->mailbox, recip->context, frompath, topath);
	} else {
		ast_log(LOG_ERROR, "Recipient mailbox %s@%s is full\n", recip->mailbox, recip->context);
	}
	ast_unlock_path(topath);
	notify_new_message(chan, recip, recipmsgnum, duration, fmt, chan->cid.cid_num, chan->cid.cid_name);
	
	return 0;
}

static void run_externnotify(char *context, char *extension)
{
	char arguments[255];
	char ext_context[256] = "";
	int newvoicemails = 0, oldvoicemails = 0;

	if (!ast_strlen_zero(context))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", extension, context);
	else
		ast_copy_string(ext_context, extension, sizeof(ext_context));

	if (!ast_strlen_zero(externnotify)) {
		if (messagecount(ext_context, &newvoicemails, &oldvoicemails)) {
			ast_log(LOG_ERROR, "Problem in calculating number of voicemail messages available for extension %s\n", extension);
		} else {
			snprintf(arguments, sizeof(arguments), "%s %s %s %d&", externnotify, context, extension, newvoicemails);
			ast_log(LOG_DEBUG, "Executing %s\n", arguments);
	  		ast_safe_system(arguments);
		}
	}
}

struct leave_vm_options {
	unsigned int flags;
	signed char record_gain;
};

static int leave_voicemail(struct ast_channel *chan, char *ext, struct leave_vm_options *options)
{
	char txtfile[256];
	char callerid[256];
	FILE *txt;
	int res = 0;
	int msgnum;
	int duration = 0;
	int ausemacro = 0;
	int ousemacro = 0;
	char date[256];
	char dir[256];
	char fn[256];
	char prefile[256]="";
	char tempfile[256]="";
	char ext_context[256] = "";
	char fmt[80];
	char *context;
	char ecodes[16] = "#";
	char tmp[256] = "", *tmpptr;
	struct ast_vm_user *vmu;
	struct ast_vm_user svm;
	char *category = NULL;

	ast_copy_string(tmp, ext, sizeof(tmp));
	ext = tmp;
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
		tmpptr = strchr(context, '&');
	} else {
		tmpptr = strchr(ext, '&');
	}

	if (tmpptr) {
		*tmpptr = '\0';
		tmpptr++;
	}

	category = pbx_builtin_getvar_helper(chan, "VM_CATEGORY");

	if (!(vmu = find_user(&svm, context, ext))) {
		ast_log(LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
		ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		return res;
	}

	/* Setup pre-file if appropriate */
	if (strcmp(vmu->context, "default"))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", ext, vmu->context);
	else
		ast_copy_string(ext_context, vmu->context, sizeof(ext_context));
	if (ast_test_flag(options, OPT_BUSY_GREETING))
		snprintf(prefile, sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, ext);
	else if (ast_test_flag(options, OPT_UNAVAIL_GREETING))
		snprintf(prefile, sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, ext);
	snprintf(tempfile, sizeof(tempfile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, ext);
	RETRIEVE(tempfile, -1);
	if (ast_fileexists(tempfile, NULL, NULL) > 0)
		ast_copy_string(prefile, tempfile, sizeof(prefile));
	DISPOSE(tempfile, -1);
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

	/* Check current or macro-calling context for special extensions */
	if (!ast_strlen_zero(vmu->exit)) {
		if (ast_exists_extension(chan, vmu->exit, "o", 1, chan->cid.cid_num))
			strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
	} else if (ast_exists_extension(chan, chan->context, "o", 1, chan->cid.cid_num))
		strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
	else if (!ast_strlen_zero(chan->macrocontext) && ast_exists_extension(chan, chan->macrocontext, "o", 1, chan->cid.cid_num)) {
		strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
		ousemacro = 1;
	}

	if (!ast_strlen_zero(vmu->exit)) {
		if (ast_exists_extension(chan, vmu->exit, "a", 1, chan->cid.cid_num))
			strncat(ecodes, "*", sizeof(ecodes) -  strlen(ecodes) - 1);
	} else if (ast_exists_extension(chan, chan->context, "a", 1, chan->cid.cid_num))
		strncat(ecodes, "*", sizeof(ecodes) -  strlen(ecodes) - 1);
	else if (!ast_strlen_zero(chan->macrocontext) && ast_exists_extension(chan, chan->macrocontext, "a", 1, chan->cid.cid_num)) {
		strncat(ecodes, "*", sizeof(ecodes) -  strlen(ecodes) - 1);
		ausemacro = 1;
	}

	/* Play the beginning intro if desired */
	if (!ast_strlen_zero(prefile)) {
		RETRIEVE(prefile, -1);
		if (ast_fileexists(prefile, NULL, NULL) > 0) {
			if (ast_streamfile(chan, prefile, chan->language) > -1) 
				res = ast_waitstream(chan, ecodes);
		} else {
			ast_log(LOG_DEBUG, "%s doesn't exist, doing what we can\n", prefile);
			res = invent_message(chan, vmu->context, ext, ast_test_flag(options, OPT_BUSY_GREETING), ecodes);
		}
		DISPOSE(prefile, -1);
		if (res < 0) {
			ast_log(LOG_DEBUG, "Hang up during prefile playback\n");
			free_user(vmu);
			return -1;
		}
	}
	if (res == '#') {
		/* On a '#' we skip the instructions */
		ast_set_flag(options, OPT_SILENT);
		res = 0;
	}
	if (!res && !ast_test_flag(options, OPT_SILENT)) {
		res = ast_streamfile(chan, INTRO, chan->language);
		if (!res)
			res = ast_waitstream(chan, ecodes);
		if (res == '#') {
			ast_set_flag(options, OPT_SILENT);
			res = 0;
		}
	}
	if (res > 0)
		ast_stopstream(chan);
	/* Check for a '*' here in case the caller wants to escape from voicemail to something
	   other than the operator -- an automated attendant or mailbox login for example */
	if (res == '*') {
		chan->exten[0] = 'a';
		chan->exten[1] = '\0';
		if (!ast_strlen_zero(vmu->exit)) {
			ast_copy_string(chan->context, vmu->exit, sizeof(chan->context));
		} else if (ausemacro && !ast_strlen_zero(chan->macrocontext)) {
			ast_copy_string(chan->context, chan->macrocontext, sizeof(chan->context));
		}
		chan->priority = 0;
		free_user(vmu);
		return 0;
	}
	/* Check for a '0' here */
	if (res == '0') {
	transfer:
		if (ast_test_flag(vmu, VM_OPERATOR)) {
			chan->exten[0] = 'o';
			chan->exten[1] = '\0';
			if (!ast_strlen_zero(vmu->exit)) {
				ast_copy_string(chan->context, vmu->exit, sizeof(chan->context));
			} else if (ousemacro && !ast_strlen_zero(chan->macrocontext)) {
				ast_copy_string(chan->context, chan->macrocontext, sizeof(chan->context));
			}
			ast_play_and_wait(chan, "transfer");
			chan->priority = 0;
			free_user(vmu);
			return 0;
		} else {
			ast_play_and_wait(chan, "vm-sorry");
			return 0;
		}
	}
	if (res < 0) {
		free_user(vmu);
		return -1;
	}
	/* The meat of recording the message...  All the announcements and beeps have been played*/
	ast_copy_string(fmt, vmfmts, sizeof(fmt));
	if (!ast_strlen_zero(fmt)) {
		msgnum = 0;

		if (vm_lock_path(dir)) {
			free_user(vmu);
			return ERROR_LOCK_PATH;
		}

		/* 
		 * This operation can be very expensive if done say over NFS or if the mailbox has 100+ messages
		 * in the mailbox.  So we should get this first so we don't cut off the first few seconds of the 
		 * message.  
		 */
		do {
			make_file(fn, sizeof(fn), dir, msgnum);
			if (!EXISTS(dir,msgnum,fn,chan->language))
				break;
			msgnum++;
		} while (msgnum < vmu->maxmsg);

		/* Now play the beep once we have the message number for our next message. */
		if (res >= 0) {
			/* Unless we're *really* silent, try to send the beep */
			res = ast_streamfile(chan, "beep", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
		}
		if (msgnum < vmu->maxmsg) {
			/* assign a variable with the name of the voicemail file */	  
			pbx_builtin_setvar_helper(chan, "VM_MESSAGEFILE", fn);
				
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
					"origtime=%ld\n"
					"category=%s\n",
					ext,
					chan->context,
					chan->macrocontext, 
					chan->exten,
					chan->priority,
					chan->name,
					ast_callerid_merge(callerid, sizeof(callerid), chan->cid.cid_name, chan->cid.cid_num, "Unknown"),
					date, (long)time(NULL),
					category ? category : ""); 
			} else
				ast_log(LOG_WARNING, "Error opening text file for output\n");
			res = play_record_review(chan, NULL, fn, vmmaxmessage, fmt, 1, vmu, &duration, dir, options->record_gain);
			if (res == '0') {
				if (txt)
					fclose(txt);
				goto transfer;
			}
			if (res > 0)
				res = 0;
			if (txt) {
				fprintf(txt, "duration=%d\n", duration);
				fclose(txt);
			}
				
			if (duration < vmminmessage) {
				if (option_verbose > 2) 
					ast_verbose( VERBOSE_PREFIX_3 "Recording was %d seconds long but needs to be at least %d - abandoning\n", duration, vmminmessage);
				DELETE(dir,msgnum,fn);
				/* XXX We should really give a prompt too short/option start again, with leave_vm_out called only after a timeout XXX */
				goto leave_vm_out;
			}
			/* Are there to be more recipients of this message? */
			while (tmpptr) {
				struct ast_vm_user recipu, *recip;
				char *exten, *context;
					
				exten = strsep(&tmpptr, "&");
				context = strchr(exten, '@');
				if (context) {
					*context = '\0';
					context++;
				}
				if ((recip = find_user(&recipu, context, exten))) {
					copy_message(chan, vmu, 0, msgnum, duration, recip, fmt);
					free_user(recip);
				}
			}
			if (ast_fileexists(fn, NULL, NULL)) {
				notify_new_message(chan, vmu, msgnum, duration, fmt, chan->cid.cid_num, chan->cid.cid_name);
				STORE(dir, vmu->mailbox, vmu->context, msgnum);
				DISPOSE(dir, msgnum);
			}
		} else {
			ast_unlock_path(dir);
			res = ast_streamfile(chan, "vm-mailboxfull", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			ast_log(LOG_WARNING, "No more messages possible\n");
		}
	} else
		ast_log(LOG_WARNING, "No format for saving voicemail?\n");
 leave_vm_out:
	free_user(vmu);

	return res;
}

static int resequence_mailbox(struct ast_vm_user *vmu, char *dir)
{
	/* we know max messages, so stop process when number is hit */

	int x,dest;
	char sfn[256];
	char dfn[256];

	if (vm_lock_path(dir))
		return ERROR_LOCK_PATH;

	for (x = 0, dest = 0; x < vmu->maxmsg; x++) {
		make_file(sfn, sizeof(sfn), dir, x);
		if (EXISTS(dir, x, sfn, NULL)) {
			
			if(x != dest) {
				make_file(dfn, sizeof(dfn), dir, dest);
				RENAME(dir, x, vmu->mailbox, vmu->context, dir, dest, sfn, dfn);
			}
			
			dest++;
		}
	}
	ast_unlock_path(dir);

	return 0;
}


static int say_and_wait(struct ast_channel *chan, int num, char *language)
{
	int d;
	d = ast_say_number(chan, num, AST_DIGIT_ANY, language, (char *) NULL);
	return d;
}

static int save_to_folder(struct ast_vm_user *vmu, char *dir, int msg, char *context, char *username, int box)
{
	char sfn[256];
	char dfn[256];
	char ddir[256];
	char *dbox = mbox(box);
	int x;
	make_file(sfn, sizeof(sfn), dir, msg);
	make_dir(ddir, sizeof(ddir), context, username, dbox);
	mkdir(ddir, 0700);

	if (vm_lock_path(ddir))
		return ERROR_LOCK_PATH;

	for (x = 0; x < vmu->maxmsg; x++) {
		make_file(dfn, sizeof(dfn), ddir, x);
		if (!EXISTS(ddir, x, dfn, NULL))
			break;
	}
	if (x >= vmu->maxmsg) {
		ast_unlock_path(ddir);
		return -1;
	}
	if (strcmp(sfn, dfn)) {
		COPY(dir, msg, ddir, x, username, context, sfn, dfn);
	}
	ast_unlock_path(ddir);
	
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
	unsigned char buf[256];
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

	if (adsi_begin_download(chan, addesc, adsifdn, adsisec, adsiver)) {
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
	if (adsi_load_session(chan, adsifdn, adsiver, 1) == 1) {
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
	x = adsi_load_session(chan, adsifdn, adsiver, 1);
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
	unsigned char buf[256];
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
	unsigned char buf[256];
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
	unsigned char buf[256];
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

static void adsi_message(struct ast_channel *chan, struct vm_state *vms)
{
	int bytes=0;
	unsigned char buf[256]; 
	char buf1[256], buf2[256];
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
	snprintf(fn2, sizeof(fn2), "%s.txt", vms->fn);
	f = fopen(fn2, "r");
	if (f) {
		while (!feof(f)) {	
			fgets((char *)buf, sizeof(buf), f);
			if (!feof(f)) {
				char *stringp=NULL;
				stringp = (char *)buf;
				strsep(&stringp, "=");
				val = strsep(&stringp, "=");
				if (!ast_strlen_zero(val)) {
					if (!strcmp((char *)buf, "callerid"))
						ast_copy_string(cid, val, sizeof(cid));
					if (!strcmp((char *)buf, "origdate"))
						ast_copy_string(datetime, val, sizeof(datetime));
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

	if (!vms->curmsg) {
		/* No prev key, provide "Folder" instead */
		keys[0] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
	}
	if (vms->curmsg >= vms->lastmsg) {
		/* If last message ... */
		if (vms->curmsg) {
			/* but not only message, provide "Folder" instead */
			keys[3] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
			bytes += adsi_voice_mode(buf + bytes, 0);

		} else {
			/* Otherwise if only message, leave blank */
			keys[3] = 1;
		}
	}

	if (!ast_strlen_zero(cid)) {
		ast_callerid_parse(cid, &name, &num);
		if (!name)
			name = num;
	} else
		name = "Unknown Caller";

	/* If deleted, show "undeleted" */

	if (vms->deleted[vms->curmsg])
		keys[1] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 11);

	/* Except "Exit" */
	keys[5] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 5);
	snprintf(buf1, sizeof(buf1), "%s%s", vms->curbox,
		strcasecmp(vms->curbox, "INBOX") ? " Messages" : "");
 	snprintf(buf2, sizeof(buf2), "Message %d of %d", vms->curmsg + 1, vms->lastmsg + 1);

 	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, name, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, datetime, "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += adsi_set_keys(buf + bytes, keys);
	bytes += adsi_voice_mode(buf + bytes, 0);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_delete(struct ast_channel *chan, struct vm_state *vms)
{
	int bytes=0;
	unsigned char buf[256];
	unsigned char keys[8];

	int x;

	if (!adsi_available(chan))
		return;

	/* New meaning for keys */
	for (x=0;x<5;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 6 + x);

	keys[6] = 0x0;
	keys[7] = 0x0;

	if (!vms->curmsg) {
		/* No prev key, provide "Folder" instead */
		keys[0] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
	}
	if (vms->curmsg >= vms->lastmsg) {
		/* If last message ... */
		if (vms->curmsg) {
			/* but not only message, provide "Folder" instead */
			keys[3] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 1);
		} else {
			/* Otherwise if only message, leave blank */
			keys[3] = 1;
		}
	}

	/* If deleted, show "undeleted" */
	if (vms->deleted[vms->curmsg]) 
		keys[1] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 11);

	/* Except "Exit" */
	keys[5] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 5);
	bytes += adsi_set_keys(buf + bytes, keys);
	bytes += adsi_voice_mode(buf + bytes, 0);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_status(struct ast_channel *chan, struct vm_state *vms)
{
	unsigned char buf[256] = "";
	char buf1[256] = "", buf2[256] = "";
	int bytes=0;
	unsigned char keys[8];
	int x;

	char *newm = (vms->newmessages == 1) ? "message" : "messages";
	char *oldm = (vms->oldmessages == 1) ? "message" : "messages";
	if (!adsi_available(chan))
		return;
	if (vms->newmessages) {
		snprintf(buf1, sizeof(buf1), "You have %d new", vms->newmessages);
		if (vms->oldmessages) {
			strncat(buf1, " and", sizeof(buf1) - strlen(buf1) - 1);
			snprintf(buf2, sizeof(buf2), "%d old %s.", vms->oldmessages, oldm);
		} else {
			snprintf(buf2, sizeof(buf2), "%s.", newm);
		}
	} else if (vms->oldmessages) {
		snprintf(buf1, sizeof(buf1), "You have %d old", vms->oldmessages);
		snprintf(buf2, sizeof(buf2), "%s.", oldm);
	} else {
		strcpy(buf1, "You have no messages.");
		buf2[0] = ' ';
		buf2[1] = '\0';
	}
 	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);

	for (x=0;x<6;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + x);
	keys[6] = 0;
	keys[7] = 0;

	/* Don't let them listen if there are none */
	if (vms->lastmsg < 0)
		keys[0] = 1;
	bytes += adsi_set_keys(buf + bytes, keys);

	bytes += adsi_voice_mode(buf + bytes, 0);

	adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_status2(struct ast_channel *chan, struct vm_state *vms)
{
	unsigned char buf[256] = "";
	char buf1[256] = "", buf2[256] = "";
	int bytes=0;
	unsigned char keys[8];
	int x;

	char *mess = (vms->lastmsg == 0) ? "message" : "messages";

	if (!adsi_available(chan))
		return;

	/* Original command keys */
	for (x=0;x<6;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + x);

	keys[6] = 0;
	keys[7] = 0;

	if ((vms->lastmsg + 1) < 1)
		keys[0] = 0;

	snprintf(buf1, sizeof(buf1), "%s%s has", vms->curbox,
		strcasecmp(vms->curbox, "INBOX") ? " folder" : "");

	if (vms->lastmsg + 1)
		snprintf(buf2, sizeof(buf2), "%d %s.", vms->lastmsg + 1, mess);
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
	unsigned char buf[256];
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

/*--- get_folder: Folder menu ---*/
/* Plays "press 1 for INBOX messages" etc
   Should possibly be internationalized
 */
static int get_folder(struct ast_channel *chan, int start)
{
	int x;
	int d;
	char fn[256];
	d = ast_play_and_wait(chan, "vm-press");	/* "Press" */
	if (d)
		return d;
	for (x = start; x< 5; x++) {	/* For all folders */
		if ((d = ast_say_number(chan, x, AST_DIGIT_ANY, chan->language, (char *) NULL)))
			return d;
		d = ast_play_and_wait(chan, "vm-for");	/* "for" */
		if (d)
			return d;
		snprintf(fn, sizeof(fn), "vm-%s", mbox(x));	/* Folder name */
		d = vm_play_folder_name(chan, fn);
		if (d)
			return d;
		d = ast_waitfordigit(chan, 500);
		if (d)
			return d;
	}
	d = ast_play_and_wait(chan, "vm-tocancel"); /* "or pound to cancel" */
	if (d)
		return d;
	d = ast_waitfordigit(chan, 4000);
	return d;
}

static int get_folder2(struct ast_channel *chan, char *fn, int start)
{
	int res = 0;
	res = ast_play_and_wait(chan, fn);	/* Folder name */
	while (((res < '0') || (res > '9')) &&
			(res != '#') && (res >= 0)) {
		res = get_folder(chan, 0);
	}
	return res;
}

static int vm_forwardoptions(struct ast_channel *chan, struct ast_vm_user *vmu, char *curdir, int curmsg, char *vmfts,
			     char *context, signed char record_gain)
{
	int cmd = 0;
	int retries = 0;
	int duration = 0;
	signed char zero_gain = 0;

	while ((cmd >= 0) && (cmd != 't') && (cmd != '*')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1': 
			/* prepend a message to the current message and return */
		{
			char file[200];
			snprintf(file, sizeof(file), "%s/msg%04d", curdir, curmsg);
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);
			cmd = ast_play_and_prepend(chan, NULL, file, 0, vmfmts, &duration, 1, silencethreshold, maxsilence);
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &zero_gain, sizeof(zero_gain), 0);
			break;
		}
		case '2': 
			cmd = 't';
			break;
		case '*':
			cmd = '*';
			break;
		default: 
			cmd = ast_play_and_wait(chan,"vm-forwardoptions");
				/* "Press 1 to prepend a message or 2 to forward the message without prepending" */
			if (!cmd)
				cmd = ast_play_and_wait(chan,"vm-starmain");
				/* "press star to return to the main menu" */
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

static int notify_new_message(struct ast_channel *chan, struct ast_vm_user *vmu, int msgnum, long duration, char *fmt, char *cidnum, char *cidname)
{
	char todir[256], fn[256], ext_context[256], *stringp;
	int newmsgs = 0, oldmsgs = 0;

	make_dir(todir, sizeof(todir), vmu->context, vmu->mailbox, "INBOX");
	make_file(fn, sizeof(fn), todir, msgnum);
	snprintf(ext_context, sizeof(ext_context), "%s@%s", vmu->mailbox, vmu->context);

	/* Attach only the first format */
	fmt = ast_strdupa(fmt);
	if (fmt) {
		stringp = fmt;
		strsep(&stringp, "|");

		if (!ast_strlen_zero(vmu->email)) {
			int attach_user_voicemail = ast_test_flag((&globalflags), VM_ATTACH);
			char *myserveremail = serveremail;
			attach_user_voicemail = ast_test_flag(vmu, VM_ATTACH);
			if (!ast_strlen_zero(vmu->serveremail))
				myserveremail = vmu->serveremail;
			sendmail(myserveremail, vmu, msgnum, vmu->context, vmu->mailbox, cidnum, cidname, fn, fmt, duration, attach_user_voicemail);
		}

		if (!ast_strlen_zero(vmu->pager)) {
			char *myserveremail = serveremail;
			if (!ast_strlen_zero(vmu->serveremail))
				myserveremail = vmu->serveremail;
			sendpage(myserveremail, vmu->pager, msgnum, vmu->context, vmu->mailbox, cidnum, cidname, duration, vmu);
		}
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
	}

	if (ast_test_flag(vmu, VM_DELETE)) {
		DELETE(todir, msgnum, fn);
	}

	/* Leave voicemail for someone */
	if (ast_app_has_voicemail(ext_context, NULL)) {
		ast_app_messagecount(ext_context, &newmsgs, &oldmsgs);
	}
	manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s@%s\r\nWaiting: %d\r\nNew: %d\r\nOld: %d\r\n", vmu->mailbox, vmu->context, ast_app_has_voicemail(ext_context, NULL), newmsgs, oldmsgs);
	run_externnotify(vmu->context, vmu->mailbox);
	return 0;
}

static int forward_message(struct ast_channel *chan, char *context, char *dir, int curmsg, struct ast_vm_user *sender,
			   char *fmt, int flag, signed char record_gain)
{
	char username[70]="";
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
	struct ast_vm_user *receiver = NULL, *extensions = NULL, *vmtmp = NULL, *vmfree;
	char tmp[256];
	char *stringp, *s;
	int saved_messages = 0, found = 0;
	int valid_extensions = 0;
	
	while (!res && !valid_extensions) {
		int use_directory = 0;
		if(ast_test_flag((&globalflags), VM_DIRECFORWARD)) {
			int done = 0;
			int retries = 0;
			cmd=0;
			while((cmd >= 0) && !done ){
				if (cmd)
					retries = 0;
				switch (cmd) {
				case '1': 
					use_directory = 0;
					done = 1;
					break;
				case '2': 
					use_directory = 1;
					done=1;
					break;
				case '*': 
					cmd = 't';
					done = 1;
					break;
				default: 
					/* Press 1 to enter an extension press 2 to use the directory */
					cmd = ast_play_and_wait(chan,"vm-forward");
					if (!cmd)
						cmd = ast_waitfordigit(chan,3000);
					if (!cmd)
						retries++;
					if (retries > 3)
					{
						cmd = 't';
						done = 1;
					}
					
				 }
			}
			if( cmd<0 || cmd=='t' )
				break;
		}
		
		if( use_directory ) {
			/* use app_directory */
			
			char old_context[sizeof(chan->context)];
			char old_exten[sizeof(chan->exten)];
			int old_priority;
			struct ast_app* app;

			
			app = pbx_findapp("Directory");
			if (app) {
				/* make mackup copies */
				memcpy(old_context, chan->context, sizeof(chan->context));
				memcpy(old_exten, chan->exten, sizeof(chan->exten));
				old_priority = chan->priority;
				
				/* call the the Directory, changes the channel */
				res = pbx_exec(chan, app, ((context)?context:chan->context), 1);
				
				ast_copy_string(username, chan->exten, sizeof(username));
				
				/* restore the old context, exten, and priority */
				memcpy(chan->context, old_context, sizeof(chan->context));
				memcpy(chan->exten, old_exten, sizeof(chan->exten));
				chan->priority = old_priority;
				
			} else {
				ast_log(LOG_WARNING, "Could not find the Directory application, disabling directory_forward\n");
				ast_clear_flag((&globalflags), VM_DIRECFORWARD);	
			}
		} else 	{
			/* Ask for an extension */
			res = ast_streamfile(chan, "vm-extension", chan->language);	/* "extension" */
			if (res)
				break;
			if ((res = ast_readstring(chan, username, sizeof(username) - 1, 2000, 10000, "#") < 0))
				break;
		}
		
		/* start all over if no username */
		if (ast_strlen_zero(username))
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
		/* "I am sorry, that's not a valid extension.  Please try again." */
		res = ast_play_and_wait(chan, "pbx-invalid");
	}
	/* check if we're clear to proceed */
	if (!extensions || !valid_extensions)
		return res;
	vmtmp = extensions;
	if (flag==1) {
		struct leave_vm_options leave_options;

		/* Send VoiceMail */
		memset(&leave_options, 0, sizeof(leave_options));
		leave_options.record_gain = record_gain;
		cmd = leave_voicemail(chan, username, &leave_options);
	} else {
		/* Forward VoiceMail */
		RETRIEVE(dir, curmsg);
		cmd = vm_forwardoptions(chan, sender, dir, curmsg, vmfmts, context, record_gain);
		if (!cmd) {
			while (!res && vmtmp) {
				/* if (ast_play_and_wait(chan, "vm-savedto"))
					break;
				*/
				snprintf(todir, sizeof(todir), "%s%s/%s/INBOX",  VM_SPOOL_DIR, vmtmp->context, vmtmp->mailbox);
				snprintf(sys, sizeof(sys), "mkdir -p %s\n", todir);
				snprintf(ext_context, sizeof(ext_context), "%s@%s", vmtmp->mailbox, vmtmp->context);
				ast_log(LOG_DEBUG, "%s", sys);
				ast_safe_system(sys);
		
				if ( (res = count_messages(receiver, todir)) )
					break;
				else
					todircount = res;
				ast_copy_string(tmp, fmt, sizeof(tmp));
				stringp = tmp;
				while ((s = strsep(&stringp, "|"))) {
					/* XXX This is a hack -- we should use build_filename or similar XXX */
					if (!strcasecmp(s, "wav49"))
						s = "WAV";
					snprintf(sys, sizeof(sys), "cp %s/msg%04d.%s %s/msg%04d.%s\n", dir, curmsg, s, todir, todircount, s);
					ast_log(LOG_DEBUG, "%s", sys);
					ast_safe_system(sys);
				}
				snprintf(sys, sizeof(sys), "cp %s/msg%04d.txt %s/msg%04d.txt\n", dir, curmsg, todir, todircount);
				ast_log(LOG_DEBUG, "%s", sys);
				ast_safe_system(sys);
				snprintf(fn, sizeof(fn), "%s/msg%04d", todir,todircount);

				STORE(todir, vmtmp->mailbox, vmtmp->context, todircount);
	
				/* load the information on the source message so we can send an e-mail like a new message */
				snprintf(miffile, sizeof(miffile), "%s/msg%04d.txt", dir, curmsg);
				if ((mif=ast_config_load(miffile))) {
	
					/* set callerid and duration variables */
					snprintf(callerid, sizeof(callerid), "FWD from: %s from %s", sender->fullname, ast_variable_retrieve(mif, NULL, "callerid"));
					s = ast_variable_retrieve(mif, NULL, "duration");
					if (s)
						duration = atoi(s);
					else
						duration = 0;
					if (!ast_strlen_zero(vmtmp->email)) {
						int attach_user_voicemail = ast_test_flag((&globalflags), VM_ATTACH);
						char *myserveremail = serveremail;
						attach_user_voicemail = ast_test_flag(vmtmp, VM_ATTACH);
						if (!ast_strlen_zero(vmtmp->serveremail))
							myserveremail = vmtmp->serveremail;
						sendmail(myserveremail, vmtmp, todircount, vmtmp->context, vmtmp->mailbox, chan->cid.cid_num, chan->cid.cid_name, fn, tmp, duration, attach_user_voicemail);
					}

					if (!ast_strlen_zero(vmtmp->pager)) {
						char *myserveremail = serveremail;
						if (!ast_strlen_zero(vmtmp->serveremail))
							myserveremail = vmtmp->serveremail;
						sendpage(myserveremail, vmtmp->pager, todircount, vmtmp->context, vmtmp->mailbox, chan->cid.cid_num, chan->cid.cid_name, duration, vmtmp);
					}
				  
					ast_config_destroy(mif); /* or here */
				}
				/* Leave voicemail for someone */
				manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext_context, has_voicemail(ext_context, NULL));
				run_externnotify(vmtmp->context, vmtmp->mailbox);
	
				saved_messages++;
				vmfree = vmtmp;
				vmtmp = vmtmp->next;
				free_user(vmfree);
			}
			if (saved_messages > 0) {
				/* give confirmation that the message was saved */
				/* commented out since we can't forward batches yet
				if (saved_messages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
				if (!res)
					res = ast_play_and_wait(chan, "vm-saved"); */
				if (!res)
					res = ast_play_and_wait(chan, "vm-msgsaved");
			}	
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
	return ast_control_streamfile(chan, file, "#", "*", "1456789", "0", "2", skipms);
}

static int play_message_category(struct ast_channel *chan, char *category)
{
	int res = 0;

	if (!ast_strlen_zero(category))
		res = ast_play_and_wait(chan, category);

	return res;
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
	if (!ast_strlen_zero(vmu->zonetag)) {
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
	tv_now = ast_tvnow();
	tnow = tv_now.tv_sec;
	localtime_r(&tnow,&time_then);

	/* Day difference */
	if (time_now.tm_year == time_then.tm_year)
		snprintf(temp,sizeof(temp),"%d",time_now.tm_yday);
	else
		snprintf(temp,sizeof(temp),"%d",(time_now.tm_year - time_then.tm_year) * 365 + (time_now.tm_yday - time_then.tm_yday));
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", temp);

	/* Can't think of how other diffs might be helpful, but I'm sure somebody will think of something. */
#endif
	if (the_zone)
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, the_zone->msg_format, the_zone->timezone);
       else if(!strcasecmp(chan->language,"se"))       /* SWEDISH syntax */
               res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' dB 'digits/at' k 'and' M", NULL);
       else if(!strcasecmp(chan->language,"no"))       /* NORWEGIAN syntax */
               res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Q 'digits/at' HM", NULL);
	else if(!strcasecmp(chan->language,"de"))	/* GERMAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Q 'digits/at' HM", NULL);
	else if (!strcasecmp(chan->language,"nl"))	/* DUTCH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q 'digits/nl-om' HM", NULL);
 	else if (!strcasecmp(chan->language,"it"))      /* ITALIAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q 'digits/at' 'digits/hours' k 'digits/e' M 'digits/minutes'", NULL);
	else if (!strcasecmp(chan->language,"gr"))
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q  H 'digits/kai' M ", NULL);
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
	if ((cid == NULL)||(context == NULL))
		return res;

	/* Strip off caller ID number from name */
	ast_log(LOG_DEBUG, "VM-CID: composite caller ID received: %s, context: %s\n", cid, context);
	ast_callerid_parse(cid, &name, &callerid);
	if ((callerid != NULL)&&(!res)&&(!ast_strlen_zero(callerid))){
		/* Check for internal contexts and only */
		/* say extension when the call didn't come from an internal context in the list */
		for (i = 0 ; i < MAX_NUM_CID_CONTEXTS ; i++){
			ast_log(LOG_DEBUG, "VM-CID: comparing internalcontext: %s\n", cidinternalcontexts[i]);
			if ((strcmp(cidinternalcontexts[i], context) == 0))
				break;
		}
		if (i != MAX_NUM_CID_CONTEXTS){ /* internal context? */
			if (!res) {
				snprintf(prefile, sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, context, callerid);
				if (!ast_strlen_zero(prefile)) {
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
	} else {
		/* Number unknown */
		ast_log(LOG_DEBUG, "VM-CID: From an unknown number\n");
		if (!res)
			/* BB: Say "from an unknown caller" as one phrase - it is already recorded by "the voice" anyhow */
			res = wait_file2(chan, vms, "vm-unknown-caller");
	}
	return res;
}

static int play_message_duration(struct ast_channel *chan, struct vm_state *vms, char *duration, int minduration)
{
	int res = 0;
	int durationm;
	int durations;
	/* Verify that we have a duration for the message */
	if((duration == NULL))
		return res;

	/* Convert from seconds to minutes */
	durations=atoi(duration);
	durationm=(durations / 60);

	ast_log(LOG_DEBUG, "VM-Duration: duration is: %d seconds converted to: %d minutes\n", durations, durationm);

	if((!res)&&(durationm>=minduration)) {
		res = ast_say_number(chan, durationm, AST_DIGIT_ANY, chan->language, (char *) NULL);
		res = wait_file2(chan, vms, "vm-minutes");
	}
	return res;
}

static int play_message(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms)
{
	int res = 0;
	char filename[256],*origtime, *cid, *context, *duration;
	char *category;
	struct ast_config *msg_cfg;

	vms->starting = 0; 
	make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
	adsi_message(chan, vms);
	if (!vms->curmsg)
		res = wait_file2(chan, vms, "vm-first");	/* "First" */
	else if (vms->curmsg == vms->lastmsg)
		res = wait_file2(chan, vms, "vm-last");		/* "last" */
	if (!res) {
               if (!strcasecmp(chan->language, "se")) {             /* SWEDISH syntax */
                       res = wait_file2(chan, vms, "vm-meddelandet");  /* "message" */
               }
               else {
                       res = wait_file2(chan, vms, "vm-message");      /* "message" */
               }
		if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
			if (!res)
				res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, chan->language, (char *) NULL);
		}
	}

	/* Retrieve info from VM attribute file */
	make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg);
	snprintf(filename,sizeof(filename), "%s.txt", vms->fn2);
	RETRIEVE(vms->curdir, vms->curmsg);
	msg_cfg = ast_config_load(filename);
	if (!msg_cfg) {
		ast_log(LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
		return 0;
	}
																									
	if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime"))) {
		ast_log(LOG_WARNING, "No origtime?!\n");
		DISPOSE(vms->curdir, vms->curmsg);
		ast_config_destroy(msg_cfg);
		return 0;
	}

	cid = ast_variable_retrieve(msg_cfg, "message", "callerid");
	duration = ast_variable_retrieve(msg_cfg, "message", "duration");
	category = ast_variable_retrieve(msg_cfg, "message", "category");

	context = ast_variable_retrieve(msg_cfg, "message", "context");
	if (!strncasecmp("macro",context,5)) /* Macro names in contexts are useless for our needs */
		context = ast_variable_retrieve(msg_cfg, "message","macrocontext");

	if (!res)
		res = play_message_category(chan, category);
	if ((!res) && (ast_test_flag(vmu, VM_ENVELOPE)))
		res = play_message_datetime(chan, vmu, origtime, filename);
	if ((!res) && (ast_test_flag(vmu, VM_SAYCID)))
		res = play_message_callerid(chan, vms, cid, context, 0);
        if ((!res) && (ast_test_flag(vmu, VM_SAYDURATION)))
                res = play_message_duration(chan, vms, duration, vmu->saydurationm);
	/* Allow pressing '1' to skip envelope / callerid */
	if (res == '1')
		res = 0;
	ast_config_destroy(msg_cfg);

	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
		vms->heard[vms->curmsg] = 1;
		printf("yay!\n");
		res = wait_file(chan, vms, vms->fn);
	}
	DISPOSE(vms->curdir, vms->curmsg);
	return res;
}

static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu,int box)
{
	int res = 0;
	int count_msg, last_msg;

	ast_copy_string(vms->curbox, mbox(box), sizeof(vms->curbox));
	
	/* Rename the member vmbox HERE so that we don't try to return before
	 * we know what's going on.
	 */
	snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", vms->curbox);
	
	make_dir(vms->curdir, sizeof(vms->curdir), vmu->context, vms->username, vms->curbox);
	count_msg = count_messages(vmu, vms->curdir);
	if (count_msg < 0)
		return count_msg;
	else
		vms->lastmsg = count_msg - 1;

	/*
	The following test is needed in case sequencing gets messed up.
	There appears to be more than one way to mess up sequence, so
	we will not try to find all of the root causes--just fix it when
	detected.
	*/

	last_msg = last_message_index(vmu, vms->curdir);
	if (last_msg < 0)
		return last_msg;
	else if(vms->lastmsg != last_msg)
	{
		ast_log(LOG_NOTICE, "Resequencing Mailbox: %s\n", vms->curdir);
		res = resequence_mailbox(vmu, vms->curdir);
		if (res)
			return res;
	}

	return 0;
}

static int close_mailbox(struct vm_state *vms, struct ast_vm_user *vmu)
{
	int x;
	int res = 0;

	if (vms->lastmsg <= -1)
		goto done;

	/* Get the deleted messages fixed */ 
	if (vm_lock_path(vms->curdir))
		return ERROR_LOCK_PATH;
	
	vms->curmsg = -1; 
	for (x=0;x < vmu->maxmsg;x++) { 
		if (!vms->deleted[x] && (strcasecmp(vms->curbox, "INBOX") || !vms->heard[x])) { 
			/* Save this message.  It's not in INBOX or hasn't been heard */ 
			make_file(vms->fn, sizeof(vms->fn), vms->curdir, x); 
			if (!EXISTS(vms->curdir, x, vms->fn, NULL)) 
				break;
			vms->curmsg++; 
			make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg); 
			if (strcmp(vms->fn, vms->fn2)) { 
				RENAME(vms->curdir, x, vmu->mailbox,vmu->context, vms->curdir, vms->curmsg, vms->fn, vms->fn2);
			} 
		} else if (!strcasecmp(vms->curbox, "INBOX") && vms->heard[x] && !vms->deleted[x]) { 
			/* Move to old folder before deleting */ 
			res = save_to_folder(vmu, vms->curdir, x, vmu->context, vms->username, 1);
			if (res == ERROR_LOCK_PATH) {
				/* If save failed do not delete the message */
				vms->deleted[x] = 0;
				vms->heard[x] = 0;
				--x;
			} 
		} 
	} 
	for (x = vms->curmsg + 1; x <= vmu->maxmsg; x++) { 
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, x); 
		if (!EXISTS(vms->curdir, x, vms->fn, NULL)) 
			break;
		DELETE(vms->curdir, x, vms->fn);
	} 
	ast_unlock_path(vms->curdir);

done:
	if (vms->deleted)
		memset(vms->deleted, 0, sizeof(vms->deleted)); 
	if (vms->heard)
		memset(vms->heard, 0, sizeof(vms->heard)); 

	return 0;
}

/* In Greek even though we CAN use a syntax like "friends messages"
 * ("filika mynhmata") it is not elegant. This also goes for "work/family messages"
 * ("ergasiaka/oikogeniaka mynhmata"). Therefore it is better to use a reversed 
 * syntax for the above three categories which is more elegant. 
*/

static int vm_play_folder_name_gr(struct ast_channel *chan, char *mbox)
{
	int cmd;
	char buf[sizeof(mbox)+1]; 

	memset(buf, '\0', sizeof(char)*(sizeof(buf)));
	strcpy(buf, mbox);
	strcat(buf,"s");

	if (!strcasecmp(mbox, "vm-INBOX") || !strcasecmp(mbox, "vm-Old")){
		cmd = ast_play_and_wait(chan, buf); /* "NEA / PALIA" */
		if (cmd)
		return cmd;
		return ast_play_and_wait(chan, "vm-messages"); /* "messages" -> "MYNHMATA" */
	} else {
		cmd = ast_play_and_wait(chan, "vm-messages"); /* "messages" -> "MYNHMATA" */
	  	if (cmd)
			return cmd;
	  	return ast_play_and_wait(chan, mbox); /* friends/family/work... -> "FILWN"/"OIKOGENIAS"/"DOULEIAS"*/
	}
}

static int vm_play_folder_name(struct ast_channel *chan, char *mbox)
{
	int cmd;

	if (!strcasecmp(chan->language, "it") || !strcasecmp(chan->language, "es") || !strcasecmp(chan->language, "fr") || !strcasecmp(chan->language, "pt")) { /* Italian, Spanish, French or Portuguese syntax */
		cmd = ast_play_and_wait(chan, "vm-messages"); /* "messages */
		if (cmd)
			return cmd;
		return ast_play_and_wait(chan, mbox);
	} else if (!strcasecmp(chan->language, "gr")){
		return vm_play_folder_name_gr(chan, mbox);
	} else {  /* Default English */
		cmd = ast_play_and_wait(chan, mbox);
		if (cmd)
			return cmd;
		return ast_play_and_wait(chan, "vm-messages"); /* "messages */
	}
}

 /* GREEK SYNTAX 
	In greek the plural for old/new is
	different so we need the following files	 
	We also need vm-denExeteMynhmata because 
	this syntax is different.
	
	-> vm-Olds.wav	: "Palia"
	-> vm-INBOXs.wav : "Nea"
	-> vm-denExeteMynhmata : "den exete mynhmata"
 */
					
	
static int vm_intro_gr(struct ast_channel *chan, struct vm_state *vms)
{
	int res = 0;

	if (vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-youhave");
		if (!res) 
			res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, chan->language, NULL);
		if (!res) {
			if ((vms->newmessages == 1)) {
				res = ast_play_and_wait(chan, "vm-INBOX");
				if (!res)
			 		res = ast_play_and_wait(chan, "vm-message");
		 	} else {
				res = ast_play_and_wait(chan, "vm-INBOXs");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
		 	}
		}	  
	} else if (vms->oldmessages){
		res = ast_play_and_wait(chan, "vm-youhave");
		if (!res)
			res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, chan->language, NULL);
		if ((vms->oldmessages == 1)){
			res = ast_play_and_wait(chan, "vm-Old");
			if (!res)
				res = ast_play_and_wait(chan, "vm-message");
		} else {
			res = ast_play_and_wait(chan, "vm-Olds");
		 	if (!res)
				res = ast_play_and_wait(chan, "vm-messages");
		}
	 } else if (!vms->oldmessages && !vms->newmessages) 
			res = ast_play_and_wait(chan, "vm-denExeteMynhmata"); 
	 return res;
}
	
/* Default English syntax */
static int vm_intro_en(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, chan->language);
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if ((vms->newmessages == 1))
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
				
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* ITALIAN syntax */
static int vm_intro_it(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	if (!vms->oldmessages && !vms->newmessages)
		res =	ast_play_and_wait(chan, "vm-no") ||
			ast_play_and_wait(chan, "vm-message");
	else
		res =	ast_play_and_wait(chan, "vm-youhave");
	if (!res && vms->newmessages) {
		res = (vms->newmessages == 1) ?
			ast_play_and_wait(chan, "digits/un") ||
			ast_play_and_wait(chan, "vm-nuovo") ||
			ast_play_and_wait(chan, "vm-message") :
			/* 2 or more new messages */
			say_and_wait(chan, vms->newmessages, chan->language) ||
			ast_play_and_wait(chan, "vm-nuovi") ||
			ast_play_and_wait(chan, "vm-messages");
		if (!res && vms->oldmessages)
			res =	ast_play_and_wait(chan, "vm-and");
	}
	if (!res && vms->oldmessages) {
		res = (vms->oldmessages == 1) ?
			ast_play_and_wait(chan, "digits/un") ||
			ast_play_and_wait(chan, "vm-vecchio") ||
			ast_play_and_wait(chan, "vm-message") :
			/* 2 or more old messages */
			say_and_wait(chan, vms->oldmessages, chan->language) ||
			ast_play_and_wait(chan, "vm-vecchi") ||
			ast_play_and_wait(chan, "vm-messages");
	}
	return res ? -1 : 0;
}

/* SWEDISH syntax */
static int vm_intro_se(struct ast_channel *chan, struct vm_state *vms)
{
        /* Introduce messages they have */
        int res;

	res = ast_play_and_wait(chan, "vm-youhave");
	if (res)
		return res;

        if (!vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-no");
		res = res ? res : ast_play_and_wait(chan, "vm-messages");
		return res;
        }

	if (vms->newmessages) {
		if ((vms->newmessages == 1)) {
			res = ast_play_and_wait(chan, "digits/ett");
			res = res ? res : ast_play_and_wait(chan, "vm-nytt");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->newmessages, chan->language);
			res = res ? res : ast_play_and_wait(chan, "vm-nya");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
		if (!res && vms->oldmessages)
			res = ast_play_and_wait(chan, "vm-and");
	}
	if (!res && vms->oldmessages) {
		if (vms->oldmessages == 1) {
			res = ast_play_and_wait(chan, "digits/ett");
			res = res ? res : ast_play_and_wait(chan, "vm-gammalt");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			res = res ? res : ast_play_and_wait(chan, "vm-gamla");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
	}

	return res;
}

/* NORWEGIAN syntax */
static int vm_intro_no(struct ast_channel *chan,struct vm_state *vms)
{
        /* Introduce messages they have */
        int res;

	res = ast_play_and_wait(chan, "vm-youhave");
	if (res)
		return res;

        if (!vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-no");
		res = res ? res : ast_play_and_wait(chan, "vm-messages");
		return res;
        }

	if (vms->newmessages) {
		if ((vms->newmessages == 1)) {
			res = ast_play_and_wait(chan, "digits/1");
			res = res ? res : ast_play_and_wait(chan, "vm-ny");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->newmessages, chan->language);
			res = res ? res : ast_play_and_wait(chan, "vm-nye");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
		if (!res && vms->oldmessages)
			res = ast_play_and_wait(chan, "vm-and");
	}
	if (!res && vms->oldmessages) {
		if (vms->oldmessages == 1) {
			res = ast_play_and_wait(chan, "digits/1");
			res = res ? res : ast_play_and_wait(chan, "vm-gamel");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			res = res ? res : ast_play_and_wait(chan, "vm-gamle");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
	}

	return res;
}

/* GERMAN syntax */
static int vm_intro_de(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			if ((vms->newmessages == 1))
				res = ast_play_and_wait(chan, "digits/1F");
			else
				res = say_and_wait(chan, vms->newmessages, chan->language);
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if ((vms->newmessages == 1))
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
				
		}
		if (!res && vms->oldmessages) {
			if (vms->oldmessages == 1)
				res = ast_play_and_wait(chan, "digits/1F");
			else
				res = say_and_wait(chan, vms->oldmessages, chan->language);
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* SPANISH syntax */
static int vm_intro_es(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	if (!vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-youhaveno");
		if (!res)
			res = ast_play_and_wait(chan, "vm-messages");
	} else {
		res = ast_play_and_wait(chan, "vm-youhave");
	}
	if (!res) {
		if (vms->newmessages) {
			if (!res) {
				if ((vms->newmessages == 1)) {
					res = ast_play_and_wait(chan, "digits/1M");
					if (!res)
						res = ast_play_and_wait(chan, "vm-message");
					if (!res)
						res = ast_play_and_wait(chan, "vm-INBOXs");
				} else {
					res = say_and_wait(chan, vms->newmessages, chan->language);
					if (!res)
						res = ast_play_and_wait(chan, "vm-messages");
					if (!res)
						res = ast_play_and_wait(chan, "vm-INBOX");
				}
			}
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
		}
		if (vms->oldmessages) {
			if (!res) {
				if (vms->oldmessages == 1) {
					res = ast_play_and_wait(chan, "digits/1M");
					if (!res)
						res = ast_play_and_wait(chan, "vm-message");
					if (!res)
						res = ast_play_and_wait(chan, "vm-Olds");
				} else {
					res = say_and_wait(chan, vms->oldmessages, chan->language);
					if (!res)
						res = ast_play_and_wait(chan, "vm-messages");
					if (!res)
						res = ast_play_and_wait(chan, "vm-Old");
				}
			}
		}
	}
return res;
}

/* FRENCH syntax */
static int vm_intro_fr(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, chan->language);
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if ((vms->newmessages == 1))
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
				
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* DUTCH syntax */
static int vm_intro_nl(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, chan->language);
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-INBOXs");
				else
					res = ast_play_and_wait(chan, "vm-INBOX");
			}
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if ((vms->newmessages == 1))
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
				
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-Olds");
				else
					res = ast_play_and_wait(chan, "vm-Old");
			}
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* PORTUGUESE syntax */
static int vm_intro_pt(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, chan->language, "f");
			if (!res) {
				if ((vms->newmessages == 1)) {
					res = ast_play_and_wait(chan, "vm-message");
					if (!res)
						res = ast_play_and_wait(chan, "vm-INBOXs");
				} else {
					res = ast_play_and_wait(chan, "vm-messages");
					if (!res)
						res = ast_play_and_wait(chan, "vm-INBOX");
				}
			}
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
		}
		if (!res && vms->oldmessages) {
			res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, chan->language, "f");
			if (!res) {
				if (vms->oldmessages == 1) {
					res = ast_play_and_wait(chan, "vm-message");
					if (!res)
						res = ast_play_and_wait(chan, "vm-Olds");
				} else {
					res = ast_play_and_wait(chan, "vm-messages");
					if (!res)
						res = ast_play_and_wait(chan, "vm-Old");
				}
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}


/* CZECH syntax */
/* in czech there must be declension of word new and message
 * czech   	: english 	   : czech	: english
 * --------------------------------------------------------
 * vm-youhave 	: you have 
 * vm-novou 	: one new 	   : vm-zpravu 	: message
 * vm-nove 	: 2-4 new	   : vm-zpravy 	: messages
 * vm-novych 	: 5-infinite new   : vm-zprav 	: messages
 * vm-starou	: one old
 * vm-stare	: 2-4 old 
 * vm-starych	: 5-infinite old
 * jednu	: one	- falling 4. 
 * vm-no	: no  ( no messages )
 */

static int vm_intro_cz(struct ast_channel *chan,struct vm_state *vms)
{
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			if (vms->newmessages == 1) {
				res = ast_play_and_wait(chan, "digits/jednu");
			} else {
				res = say_and_wait(chan, vms->newmessages, chan->language);
			}
			if (!res) {
				if ((vms->newmessages == 1))
					res = ast_play_and_wait(chan, "vm-novou");
				if ((vms->newmessages) > 1 && (vms->newmessages < 5))
					res = ast_play_and_wait(chan, "vm-nove");
				if (vms->newmessages > 4)
					res = ast_play_and_wait(chan, "vm-novych");
			}
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if ((vms->newmessages == 1))
					res = ast_play_and_wait(chan, "vm-zpravu");
				if ((vms->newmessages) > 1 && (vms->newmessages < 5))
					res = ast_play_and_wait(chan, "vm-zpravy");
				if (vms->newmessages > 4)
					res = ast_play_and_wait(chan, "vm-zprav");
			}
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			if (!res) {
				if ((vms->oldmessages == 1))
					res = ast_play_and_wait(chan, "vm-starou");
				if ((vms->oldmessages) > 1 && (vms->oldmessages < 5))
					res = ast_play_and_wait(chan, "vm-stare");
				if (vms->oldmessages > 4)
					res = ast_play_and_wait(chan, "vm-starych");
			}
			if (!res) {
				if ((vms->oldmessages == 1))
					res = ast_play_and_wait(chan, "vm-zpravu");
				if ((vms->oldmessages) > 1 && (vms->oldmessages < 5))
					res = ast_play_and_wait(chan, "vm-zpravy");
				if (vms->oldmessages > 4)
					res = ast_play_and_wait(chan, "vm-zprav");
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-zpravy");
			}
		}
	}
	return res;
}

static int vm_intro(struct ast_channel *chan,struct vm_state *vms)
{
	/* Play voicemail intro - syntax is different for different languages */
	if (!strcasecmp(chan->language, "de")) {	/* GERMAN syntax */
		return vm_intro_de(chan, vms);
	} else if (!strcasecmp(chan->language, "es")) { /* SPANISH syntax */
		return vm_intro_es(chan, vms);
	} else if (!strcasecmp(chan->language, "it")) { /* ITALIAN syntax */
		return vm_intro_it(chan, vms);
	} else if (!strcasecmp(chan->language, "fr")) {	/* FRENCH syntax */
		return vm_intro_fr(chan, vms);
	} else if (!strcasecmp(chan->language, "nl")) {	/* DUTCH syntax */
		return vm_intro_nl(chan, vms);
	} else if (!strcasecmp(chan->language, "pt")) {	/* PORTUGUESE syntax */
		return vm_intro_pt(chan, vms);
	} else if (!strcasecmp(chan->language, "cz")) {	/* CZECH syntax */
		return vm_intro_cz(chan, vms);
	} else if (!strcasecmp(chan->language, "gr")) {	/* GREEK syntax */
		return vm_intro_gr(chan, vms);
	} else if (!strcasecmp(chan->language, "se")) {	/* SWEDISH syntax */
		return vm_intro_se(chan, vms);
	} else if (!strcasecmp(chan->language, "no")) {	/* NORWEGIAN syntax */
		return vm_intro_no(chan, vms);
	} else {					/* Default to ENGLISH */
		return vm_intro_en(chan, vms);
	}
}

static int vm_instructions(struct ast_channel *chan, struct vm_state *vms, int skipadvanced)
{
	int res = 0;
	/* Play instructions and wait for new command */
	while (!res) {
		if (vms->starting) {
			if (vms->lastmsg > -1) {
				res = ast_play_and_wait(chan, "vm-onefor");
				if (!res)
					res = vm_play_folder_name(chan, vms->vmbox);
			}
			if (!res)
				res = ast_play_and_wait(chan, "vm-opts");
		} else {
			if (vms->curmsg)
				res = ast_play_and_wait(chan, "vm-prev");
			if (!res && !skipadvanced)
				res = ast_play_and_wait(chan, "vm-advopts");
			if (!res)
				res = ast_play_and_wait(chan, "vm-repeat");
			if (!res && (vms->curmsg != vms->lastmsg))
				res = ast_play_and_wait(chan, "vm-next");
			if (!res) {
				if (!vms->deleted[vms->curmsg])
					res = ast_play_and_wait(chan, "vm-delete");
				else
					res = ast_play_and_wait(chan, "vm-undelete");
				if (!res)
					res = ast_play_and_wait(chan, "vm-toforward");
				if (!res)
					res = ast_play_and_wait(chan, "vm-savemessage");
			}
		}
		if (!res)
			res = ast_play_and_wait(chan, "vm-helpexit");
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

static int vm_newuser(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain)
{
	int cmd = 0;
	int duration = 0;
	int tries = 0;
	char newpassword[80] = "";
	char newpassword2[80] = "";
	char prefile[256]="";
	unsigned char buf[256];
	int bytes=0;

	if (adsi_available(chan)) {
		bytes += adsi_logo(buf + bytes);
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "New User Setup", "");
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += adsi_voice_mode(buf + bytes, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}

	/* First, have the user change their password 
	   so they won't get here again */
	for (;;) {
		newpassword[1] = '\0';
		newpassword[0] = cmd = ast_play_and_wait(chan,"vm-newpassword");
		if (cmd == '#')
			newpassword[0] = '\0';
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		cmd = ast_readstring(chan,newpassword + strlen(newpassword),sizeof(newpassword)-1,2000,10000,"#");
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		newpassword2[1] = '\0';
		newpassword2[0] = cmd = ast_play_and_wait(chan,"vm-reenterpassword");
		if (cmd == '#')
			newpassword2[0] = '\0';
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		cmd = ast_readstring(chan,newpassword2 + strlen(newpassword2),sizeof(newpassword2)-1,2000,10000,"#");
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		if (!strcmp(newpassword, newpassword2))
			break;
		ast_log(LOG_NOTICE,"Password mismatch for user %s (%s != %s)\n", vms->username, newpassword, newpassword2);
		cmd = ast_play_and_wait(chan, "vm-mismatch");
		if (++tries == 3)
			return -1;
	}
	if (ast_strlen_zero(ext_pass_cmd)) 
		vm_change_password(vmu,newpassword);
	else 
		vm_change_password_shell(vmu,newpassword);
	
	cmd = ast_play_and_wait(chan,"vm-passchanged");

	/* If forcename is set, have the user record their name */	
	if (ast_test_flag(vmu, VM_FORCENAME)) {
		snprintf(prefile,sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, vms->username);
		cmd = play_record_review(chan,"vm-rec-name",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
	}

	/* If forcegreetings is set, have the user record their greetings */
	if (ast_test_flag(vmu, VM_FORCEGREET)) {
		snprintf(prefile,sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, vms->username);
		cmd = play_record_review(chan,"vm-rec-unv",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		snprintf(prefile,sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, vms->username);
		cmd = play_record_review(chan,"vm-rec-busy",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
	}

	return cmd;
}

static int vm_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain)
{
	int cmd = 0;
	int retries = 0;
	int duration = 0;
	char newpassword[80] = "";
	char newpassword2[80] = "";
	char prefile[256]="";
	unsigned char buf[256];
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
	while ((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1':
			snprintf(prefile,sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-unv",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
			break;
		case '2': 
			snprintf(prefile,sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-busy",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
			break;
		case '3': 
			snprintf(prefile,sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-name",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
			break;
		case '4': 
			cmd = vm_tempgreeting(chan, vmu, vms, fmtc, record_gain);
			break;
		case '5':
			if (vmu->password[0] == '-') {
				cmd = ast_play_and_wait(chan, "vm-no");
				break;
			}
			newpassword[1] = '\0';
			newpassword[0] = cmd = ast_play_and_wait(chan,"vm-newpassword");
			if (cmd == '#')
				newpassword[0] = '\0';
			else {
				if (cmd < 0)
					break;
				if ((cmd = ast_readstring(chan,newpassword + strlen(newpassword),sizeof(newpassword)-1,2000,10000,"#")) < 0) {
					break;
				}
			}
			newpassword2[1] = '\0';
			newpassword2[0] = cmd = ast_play_and_wait(chan,"vm-reenterpassword");
			if (cmd == '#')
				newpassword2[0] = '\0';
			else {
				if (cmd < 0)
					break;

				if ((cmd = ast_readstring(chan,newpassword2 + strlen(newpassword2),sizeof(newpassword2)-1,2000,10000,"#"))) {
					break;
				}
			}
			if (strcmp(newpassword, newpassword2)) {
				ast_log(LOG_NOTICE,"Password mismatch for user %s (%s != %s)\n", vms->username, newpassword, newpassword2);
				cmd = ast_play_and_wait(chan, "vm-mismatch");
				break;
			}
			if (ast_strlen_zero(ext_pass_cmd)) 
				vm_change_password(vmu,newpassword);
			else 
				vm_change_password_shell(vmu,newpassword);
			ast_log(LOG_DEBUG,"User %s set password to %s of length %d\n",vms->username,newpassword,(int)strlen(newpassword));
			cmd = ast_play_and_wait(chan,"vm-passchanged");
			break;
		case '*': 
			cmd = 't';
			break;
		default: 
			cmd = ast_play_and_wait(chan,"vm-options");
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

static int vm_tempgreeting(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain)
{
	int cmd = 0;
	int retries = 0;
	int duration = 0;
	char prefile[256]="";
	unsigned char buf[256];
	int bytes=0;

	if (adsi_available(chan))
	{
		bytes += adsi_logo(buf + bytes);
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Temp Greeting Menu", "");
		bytes += adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += adsi_voice_mode(buf + bytes, 0);
		adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}
	snprintf(prefile,sizeof(prefile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, vms->username);
	while((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		if (ast_fileexists(prefile, NULL, NULL) > 0) {
			switch (cmd) {
			case '1':
				cmd = play_record_review(chan,"vm-rec-temp",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
				break;
			case '2':
				ast_filedelete(prefile, NULL);
				ast_play_and_wait(chan,"vm-tempremoved");
				cmd = 't';	
				break;
			case '*': 
				cmd = 't';
				break;
			default:
				if (ast_fileexists(prefile, NULL, NULL) > 0) {
					cmd = ast_play_and_wait(chan,"vm-tempgreeting2");
				} else {
					cmd = ast_play_and_wait(chan,"vm-tempgreeting");
				} if (!cmd) {
					cmd = ast_waitfordigit(chan,6000);
				} if (!cmd) {
					retries++;
				} if (retries > 3) {
					cmd = 't';
				}
			}
		} else {
			play_record_review(chan,"vm-rec-temp",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain);
			cmd = 't';	
		}
	}
	if (cmd == 't')
		cmd = 0;
	return cmd;
}

/* GREEK SYNTAX */
	
static int vm_browse_messages_gr(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd=0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
	 	cmd = ast_play_and_wait(chan, "vm-youhaveno");
	 	if (!strcasecmp(vms->vmbox, "vm-INBOX") ||!strcasecmp(vms->vmbox, "vm-Old")){
			if (!cmd) {
		 		snprintf(vms->fn, sizeof(vms->fn), "vm-%ss", vms->curbox);
		 		cmd = ast_play_and_wait(chan, vms->fn);
			}
			if (!cmd)
		 		cmd = ast_play_and_wait(chan, "vm-messages");
	 	} else {
		 	if (!cmd)
				cmd = ast_play_and_wait(chan, "vm-messages");
			if (!cmd) {
			 	snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
			 	cmd = ast_play_and_wait(chan, vms->fn);
			}
		}
	} 
	return cmd;
}

/* Default English syntax */
static int vm_browse_messages_en(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd=0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		cmd = ast_play_and_wait(chan, "vm-youhave");
		if (!cmd) 
			cmd = ast_play_and_wait(chan, "vm-no");
		if (!cmd) {
			snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
			cmd = ast_play_and_wait(chan, vms->fn);
		}
		if (!cmd)
			cmd = ast_play_and_wait(chan, "vm-messages");
	}
	return cmd;
}

/* ITALIAN syntax */
static int vm_browse_messages_it(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
        int cmd=0;

        if (vms->lastmsg > -1) {
                cmd = play_message(chan, vmu, vms);
        } else {
                cmd = ast_play_and_wait(chan, "vm-no");
                if (!cmd)
                        cmd = ast_play_and_wait(chan, "vm-message");
                if (!cmd) {
                        snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
                        cmd = ast_play_and_wait(chan, vms->fn);
                }
        }
        return cmd;
}

/* SPANISH syntax */
static int vm_browse_messages_es(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd=0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		cmd = ast_play_and_wait(chan, "vm-youhaveno");
		if (!cmd)
			cmd = ast_play_and_wait(chan, "vm-messages");
		if (!cmd) {
			snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
			cmd = ast_play_and_wait(chan, vms->fn);
		}
	}
	return cmd;
}

/* PORTUGUESE syntax */
static int vm_browse_messages_pt(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd=0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		cmd = ast_play_and_wait(chan, "vm-no");
		if (!cmd) {
			snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
			cmd = ast_play_and_wait(chan, vms->fn);
		}
		if (!cmd)
			cmd = ast_play_and_wait(chan, "vm-messages");
	}
	return cmd;
}

static int vm_browse_messages(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	if (!strcasecmp(chan->language, "es")) {	/* SPANISH */
		return vm_browse_messages_es(chan, vms, vmu);
	} else if (!strcasecmp(chan->language, "it")) { /* ITALIAN */
		return vm_browse_messages_it(chan, vms, vmu);
	} else if (!strcasecmp(chan->language, "pt")) {	/* PORTUGUESE */
		return vm_browse_messages_pt(chan, vms, vmu);
	} else if (!strcasecmp(chan->language, "gr")){
		return vm_browse_messages_gr(chan, vms, vmu);   /* GREEK */
	} else {	/* Default to English syntax */
		return vm_browse_messages_en(chan, vms, vmu);
	}
}

static int vm_authenticate(struct ast_channel *chan, char *mailbox, int mailbox_size,
			   struct ast_vm_user *res_vmu, const char *context, const char *prefix,
			   int skipuser, int maxlogins, int silent)
{
	int useadsi, valid=0, logretries=0;
	char password[AST_MAX_EXTENSION]="", *passptr;
	struct ast_vm_user vmus, *vmu = NULL;

	/* If ADSI is supported, setup login screen */
	adsi_begin(chan, &useadsi);
	if (!skipuser && useadsi)
		adsi_login(chan);
	if (!silent && !skipuser && ast_streamfile(chan, "vm-login", chan->language)) {
		ast_log(LOG_WARNING, "Couldn't stream login file\n");
		return -1;
	}
	
	/* Authenticate them and get their mailbox/password */
	
	while (!valid && (logretries < maxlogins)) {
		/* Prompt for, and read in the username */
		if (!skipuser && ast_readstring(chan, mailbox, mailbox_size - 1, 2000, 10000, "#") < 0) {
			ast_log(LOG_WARNING, "Couldn't read username\n");
			return -1;
		}
		if (ast_strlen_zero(mailbox)) {
			if (chan->cid.cid_num) {
				ast_copy_string(mailbox, chan->cid.cid_num, mailbox_size);
			} else {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Username not entered\n");	
				return -1;
			}
		}
		if (useadsi)
			adsi_password(chan);

		if (!ast_strlen_zero(prefix)) {
			char fullusername[80] = "";
			ast_copy_string(fullusername, prefix, sizeof(fullusername));
			strncat(fullusername, mailbox, sizeof(fullusername) - 1 - strlen(fullusername));
			ast_copy_string(mailbox, fullusername, mailbox_size);
		}

		vmu = find_user(&vmus, context, mailbox);
		if (vmu && (vmu->password[0] == '\0' || (vmu->password[0] == '-' && vmu->password[1] == '\0'))) {
			/* saved password is blank, so don't bother asking */
			password[0] = '\0';
		} else {
			if (ast_streamfile(chan, "vm-password", chan->language)) {
				ast_log(LOG_WARNING, "Unable to stream password file\n");
				return -1;
			}
			if (ast_readstring(chan, password, sizeof(password) - 1, 2000, 10000, "#") < 0) {
				ast_log(LOG_WARNING, "Unable to read password\n");
				return -1;
			}
		}

		if (vmu) {
			passptr = vmu->password;
			if (passptr[0] == '-') passptr++;
		}
		if (vmu && !strcmp(passptr, password))
			valid++;
		else {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Incorrect password '%s' for user '%s' (context = %s)\n", password, mailbox, context ? context : "<any>");
			if (!ast_strlen_zero(prefix))
				mailbox[0] = '\0';
		}
		logretries++;
		if (!valid) {
			if (skipuser || logretries >= maxlogins) {
				if (ast_streamfile(chan, "vm-incorrect", chan->language)) {
					ast_log(LOG_WARNING, "Unable to stream incorrect message\n");
					return -1;
				}
			} else {
				if (useadsi)
					adsi_login(chan);
				if (ast_streamfile(chan, "vm-incorrect-mailbox", chan->language)) {
					ast_log(LOG_WARNING, "Unable to stream incorrect mailbox message\n");
					return -1;
				}
			}
			if (ast_waitstream(chan, ""))	/* Channel is hung up */
				return -1;
		}
	}
	if (!valid && (logretries >= maxlogins)) {
		ast_stopstream(chan);
		ast_play_and_wait(chan, "vm-goodbye");
		return -1;
	}
	if (vmu && !skipuser) {
		memcpy(res_vmu, vmu, sizeof(struct ast_vm_user));
	}
	return 0;
}

static int vm_execmain(struct ast_channel *chan, void *data)
{
	/* XXX This is, admittedly, some pretty horrendus code.  For some
	   reason it just seemed a lot easier to do with GOTO's.  I feel
	   like I'm back in my GWBASIC days. XXX */
	int res=-1;
	int cmd=0;
	int valid = 0;
	struct localuser *u;
	char prefixstr[80] ="";
	char ext_context[256]="";
	int box;
	int useadsi = 0;
	int skipuser = 0;
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	char *context=NULL;
	int silentexit = 0;
	struct ast_flags flags = { 0 };
	signed char record_gain = 0;

	LOCAL_USER_ADD(u);

	memset(&vms, 0, sizeof(vms));
	vms.lastmsg = -1;

	memset(&vmus, 0, sizeof(vmus));

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	if (!ast_strlen_zero(data)) {
		char *tmp;
		int argc;
		char *argv[2];
		char *opts[OPT_ARG_ARRAY_SIZE];

		tmp = ast_strdupa(data);
		argc = ast_app_separate_args(tmp, '|', argv, sizeof(argv) / sizeof(argv[0]));
		if (argc == 2) {
			if (ast_app_parse_options(vm_app_options, &flags, opts, argv[1])) {
				LOCAL_USER_REMOVE(u);
				return -1;
			}
			if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
				int gain;

				if (sscanf(opts[OPT_ARG_RECORDGAIN], "%d", &gain) != 1) {
					ast_log(LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
					LOCAL_USER_REMOVE(u);
					return -1;
				} else {
					record_gain = (signed char) gain;
				}
			}
		} else {
			/* old style options parsing */
			while (*argv[0]) {
				if (*argv[0] == 's') {
					ast_set_flag(&flags, OPT_SILENT);
					argv[0]++;
				} else if (*argv[0] == 'p') {
					ast_set_flag(&flags, OPT_PREPEND_MAILBOX);
					argv[0]++;
				} else 
					break;
			}

		}

		valid = ast_test_flag(&flags, OPT_SILENT);

		if ((context = strchr(argv[0], '@')))
			*context++ = '\0';

		if (ast_test_flag(&flags, OPT_PREPEND_MAILBOX))
			ast_copy_string(prefixstr, argv[0], sizeof(prefixstr));
		else
			ast_copy_string(vms.username, argv[0], sizeof(vms.username));

		if (!ast_strlen_zero(vms.username) && (vmu = find_user(&vmus, context ,vms.username)))
			skipuser++;
		else
			valid = 0;
	}

	if (!valid)
		res = vm_authenticate(chan, vms.username, sizeof(vms.username), &vmus, context, prefixstr, skipuser, maxlogins, 0);

	if (!res) {
		valid = 1;
		if (!skipuser)
			vmu = &vmus;
	} else {
		res = 0;
	}

	/* If ADSI is supported, setup login screen */
	adsi_begin(chan, &useadsi);

	if (!valid)
		goto out;

	vms.deleted = calloc(vmu->maxmsg, sizeof(int));
	vms.heard = calloc(vmu->maxmsg, sizeof(int));
	
	/* Set language from config to override channel language */
	if (!ast_strlen_zero(vmu->language))
		ast_copy_string(chan->language, vmu->language, sizeof(chan->language));
	snprintf(vms.curdir, sizeof(vms.curdir), "%s/%s", VM_SPOOL_DIR, vmu->context);
	mkdir(vms.curdir, 0700);
	snprintf(vms.curdir, sizeof(vms.curdir), "%s/%s/%s", VM_SPOOL_DIR, vmu->context, vms.username);
	mkdir(vms.curdir, 0700);
	/* Retrieve old and new message counts */
	res = open_mailbox(&vms, vmu, 1);
	if (res == ERROR_LOCK_PATH)
		goto out;
	vms.oldmessages = vms.lastmsg + 1;
	/* Start in INBOX */
	res = open_mailbox(&vms, vmu, 0);
	if (res == ERROR_LOCK_PATH)
		goto out;
	vms.newmessages = vms.lastmsg + 1;
		
	/* Select proper mailbox FIRST!! */
	if (!vms.newmessages && vms.oldmessages) {
		/* If we only have old messages start here */
		res = open_mailbox(&vms, vmu, 1);
		if (res == ERROR_LOCK_PATH)
			goto out;
	}

	if (useadsi)
		adsi_status(chan, &vms);
	res = 0;

	/* Check to see if this is a new user */
	if (!strcasecmp(vmu->mailbox, vmu->password) && 
	    (ast_test_flag(vmu, VM_FORCENAME | VM_FORCEGREET))) {
		if (ast_play_and_wait(chan, "vm-newuser") == -1)
			ast_log(LOG_WARNING, "Couldn't stream new user file\n");
		cmd = vm_newuser(chan, vmu, &vms, vmfmts, record_gain);
		if ((cmd == 't') || (cmd == '#')) {
			/* Timeout */
			res = 0;
			goto out;
		} else if (cmd < 0) {
			/* Hangup */
			res = -1;
			goto out;
		}
	}

	cmd = vm_intro(chan, &vms);

	vms.repeats = 0;
	vms.starting = 1;
	while ((cmd > -1) && (cmd != 't') && (cmd != '#')) {
		/* Run main menu */
		switch(cmd) {
		case '1':
			vms.curmsg = 0;
			/* Fall through */
		case '5':
			cmd = vm_browse_messages(chan, &vms, vmu);
			break;
		case '2': /* Change folders */
			if (useadsi)
				adsi_folders(chan, 0, "Change to folder...");
			cmd = get_folder2(chan, "vm-changeto", 0);
			if (cmd == '#') {
				cmd = 0;
			} else if (cmd > 0) {
				cmd = cmd - '0';
				res = close_mailbox(&vms, vmu);
				if (res == ERROR_LOCK_PATH)
					goto out;
				res = open_mailbox(&vms, vmu, cmd);
				if (res == ERROR_LOCK_PATH)
					goto out;
				cmd = 0;
			}
			if (useadsi)
				adsi_status2(chan, &vms);
				
			if (!cmd)
				cmd = vm_play_folder_name(chan, vms.vmbox);

			vms.starting = 1;
			break;
		case '3': /* Advanced options */
			cmd = 0;
			vms.repeats = 0;
			while ((cmd > -1) && (cmd != 't') && (cmd != '#')) {
				switch(cmd) {
				case '1': /* Reply */
					if (vms.lastmsg > -1) {
						cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 1, record_gain);
						if (cmd == ERROR_LOCK_PATH) {
							res = cmd;
							goto out;
						}
					} else
						cmd = ast_play_and_wait(chan, "vm-sorry");
					cmd = 't';
					break;
				case '2': /* Callback */
					ast_verbose( VERBOSE_PREFIX_3 "Callback Requested\n");
					if (!ast_strlen_zero(vmu->callback) && vms.lastmsg > -1) {
						cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 2, record_gain);
						if (cmd == 9) {
							silentexit = 1;
							goto out;
						} else if (cmd == ERROR_LOCK_PATH) {
							res = cmd;
							goto out;
						}
					}
					else 
						cmd = ast_play_and_wait(chan, "vm-sorry");
					cmd = 't';
					break;
				case '3': /* Envelope */
					if (vms.lastmsg > -1) {
						cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 3, record_gain);
						if (cmd == ERROR_LOCK_PATH) {
							res = cmd;
							goto out;
						}
					} else
						cmd = ast_play_and_wait(chan, "vm-sorry");
					cmd = 't';
					break;
				case '4': /* Dialout */
					if (!ast_strlen_zero(vmu->dialout)) {
						cmd = dialout(chan, vmu, NULL, vmu->dialout);
						if (cmd == 9) {
							silentexit = 1;
							goto out;
						}
					}
					else 
						cmd = ast_play_and_wait(chan, "vm-sorry");
					cmd = 't';
					break;

				case '5': /* Leave VoiceMail */
					if (ast_test_flag(vmu, VM_SVMAIL)) {
						cmd = forward_message(chan, context, vms.curdir, vms.curmsg, vmu, vmfmts, 1, record_gain);
						if (cmd == ERROR_LOCK_PATH) {
							res = cmd;
							goto out;
						}
					} else
						cmd = ast_play_and_wait(chan,"vm-sorry");
					cmd='t';
					break;
					
				case '*': /* Return to main menu */
					cmd = 't';
					break;

				default:
					cmd = 0;
					if (!vms.starting) {
						cmd = ast_play_and_wait(chan, "vm-toreply");
					}
					if (!ast_strlen_zero(vmu->callback) && !vms.starting && !cmd) {
						cmd = ast_play_and_wait(chan, "vm-tocallback");
					}
					if (!cmd && !vms.starting) {
						cmd = ast_play_and_wait(chan, "vm-tohearenv");
					}
					if (!ast_strlen_zero(vmu->dialout) && !cmd) {
						cmd = ast_play_and_wait(chan, "vm-tomakecall");
					}
					if (ast_test_flag(vmu, VM_SVMAIL) && !cmd)
						cmd=ast_play_and_wait(chan, "vm-leavemsg");
					if (!cmd)
						cmd = ast_play_and_wait(chan, "vm-starmain");
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
				cmd = play_message(chan, vmu, &vms);
			} else {
				cmd = ast_play_and_wait(chan, "vm-nomore");
			}
			break;
		case '6':
			if (vms.curmsg < vms.lastmsg) {
				vms.curmsg++;
				cmd = play_message(chan, vmu, &vms);
			} else {
				cmd = ast_play_and_wait(chan, "vm-nomore");
			}
			break;
		case '7':
			vms.deleted[vms.curmsg] = !vms.deleted[vms.curmsg];
			if (useadsi)
				adsi_delete(chan, &vms);
			if (vms.deleted[vms.curmsg]) 
				cmd = ast_play_and_wait(chan, "vm-deleted");
			else
				cmd = ast_play_and_wait(chan, "vm-undeleted");
			if (ast_test_flag((&globalflags), VM_SKIPAFTERCMD)) {
				if (vms.curmsg < vms.lastmsg) {
					vms.curmsg++;
					cmd = play_message(chan, vmu, &vms);
				} else {
					cmd = ast_play_and_wait(chan, "vm-nomore");
				}
			}
			break;
	
		case '8':
			if (vms.lastmsg > -1) {
				cmd = forward_message(chan, context, vms.curdir, vms.curmsg, vmu, vmfmts, 0, record_gain);
				if (cmd == ERROR_LOCK_PATH) {
					res = cmd;
					goto out;
				}
			} else
				cmd = ast_play_and_wait(chan, "vm-nomore");
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
				cmd = save_to_folder(vmu, vms.curdir, vms.curmsg, vmu->context, vms.username, cmd);
				if (cmd == ERROR_LOCK_PATH) {
					res = cmd;
					goto out;
				} else if (!cmd) {
					vms.deleted[vms.curmsg] = 1;
				} else {
					vms.deleted[vms.curmsg] = 0;
					vms.heard[vms.curmsg] = 0;
				}
			}
			make_file(vms.fn, sizeof(vms.fn), vms.curdir, vms.curmsg);
			if (useadsi)
				adsi_message(chan, &vms);
			snprintf(vms.fn, sizeof(vms.fn), "vm-%s", mbox(box));
			if (!cmd) {
				cmd = ast_play_and_wait(chan, "vm-message");
				if (!cmd)
					cmd = say_and_wait(chan, vms.curmsg + 1, chan->language);
				if (!cmd)
					cmd = ast_play_and_wait(chan, "vm-savedto");
				if (!cmd)
					cmd = vm_play_folder_name(chan, vms.fn);
			} else {
				cmd = ast_play_and_wait(chan, "vm-mailboxfull");
			}
			if (ast_test_flag((&globalflags), VM_SKIPAFTERCMD)) {
				if (vms.curmsg < vms.lastmsg) {
					vms.curmsg++;
					cmd = play_message(chan, vmu, &vms);
				} else {
					cmd = ast_play_and_wait(chan, "vm-nomore");
				}
			}
			break;
		case '*':
			if (!vms.starting) {
				cmd = ast_play_and_wait(chan, "vm-onefor");
				if (!cmd)
					cmd = vm_play_folder_name(chan, vms.vmbox);
				if (!cmd)
					cmd = ast_play_and_wait(chan, "vm-opts");
				if (!cmd)
					cmd = vm_instructions(chan, &vms, 1);
			} else
				cmd = 0;
			break;
		case '0':
			cmd = vm_options(chan, vmu, &vms, vmfmts, record_gain);
			if (useadsi)
				adsi_status(chan, &vms);
			break;
		default:	/* Nothing */
			cmd = vm_instructions(chan, &vms, 0);
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

out:
	if (res > -1) {
		ast_stopstream(chan);
		adsi_goodbye(chan);
		if (valid) {
			if (silentexit)
				res = ast_play_and_wait(chan, "vm-dialout");
			else 
				res = ast_play_and_wait(chan, "vm-goodbye");
			if (res > 0)
				res = 0;
		}
		if (useadsi)
			adsi_unload_session(chan);
	}
	if (vmu)
		close_mailbox(&vms, vmu);
	if (valid) {
		snprintf(ext_context, sizeof(ext_context), "%s@%s", vms.username, vmu->context);
		manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext_context, has_voicemail(ext_context, NULL));
		run_externnotify(vmu->context, vmu->mailbox);
	}
	if (vmu)
		free_user(vmu);
	if (vms.deleted)
		free(vms.deleted);
	if (vms.heard)
		free(vms.heard);
	LOCAL_USER_REMOVE(u);

	return res;
}

static int vm_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	struct leave_vm_options leave_options;
	int argc;
	char *argv[2];
	struct ast_flags flags = { 0 };
	char *opts[OPT_ARG_ARRAY_SIZE];
	
	LOCAL_USER_ADD(u);
	
	memset(&leave_options, 0, sizeof(leave_options));

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	if (!ast_strlen_zero(data)) {
		ast_copy_string(tmp, data, sizeof(tmp));
		argc = ast_app_separate_args(tmp, '|', argv, sizeof(argv) / sizeof(argv[0]));
		if (argc == 2) {
			if (ast_app_parse_options(vm_app_options, &flags, opts, argv[1])) {
				LOCAL_USER_REMOVE(u);
				return -1;
			}
			ast_copy_flags(&leave_options, &flags, OPT_SILENT | OPT_BUSY_GREETING | OPT_UNAVAIL_GREETING);
			if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
				int gain;

				if (sscanf(opts[OPT_ARG_RECORDGAIN], "%d", &gain) != 1) {
					ast_log(LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
					LOCAL_USER_REMOVE(u);
					return -1;
				} else {
					leave_options.record_gain = (signed char) gain;
				}
			}
		} else {
			/* old style options parsing */
			while (*argv[0]) {
				if (*argv[0] == 's') {
					ast_set_flag(&leave_options, OPT_SILENT);
					argv[0]++;
				} else if (*argv[0] == 'b') {
					ast_set_flag(&leave_options, OPT_BUSY_GREETING);
					argv[0]++;
				} else if (*argv[0] == 'u') {
					ast_set_flag(&leave_options, OPT_UNAVAIL_GREETING);
					argv[0]++;
				} else 
					break;
			}
		}
	} else {
		res = ast_app_getdata(chan, "vm-whichbox", tmp, sizeof(tmp) - 1, 0);
		if (res < 0) {
			LOCAL_USER_REMOVE(u);
			return res;
		}
		if (ast_strlen_zero(tmp)) {
			LOCAL_USER_REMOVE(u);
			return 0;
		}	
	}

	res = leave_voicemail(chan, argv[0], &leave_options);

	if (res == ERROR_LOCK_PATH) {
		ast_log(LOG_ERROR, "Could not leave voicemail. The path is already locked.\n");
		/*Send the call to n+101 priority, where n is the current priority*/
		if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101))
			ast_log(LOG_WARNING, "Extension %s, priority %d doesn't exist.\n", chan->exten, chan->priority + 101);
		res = 0;
	}
	
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

	ast_copy_string(tmp, data, sizeof(tmp));
	vmu = malloc(sizeof(struct ast_vm_user));
	if (vmu) {
		memset(vmu, 0, sizeof(struct ast_vm_user));
		ast_copy_string(vmu->context, context, sizeof(vmu->context));
		ast_copy_string(vmu->mailbox, mbox, sizeof(vmu->mailbox));

		populate_defaults(vmu);

		stringp = tmp;
		if ((s = strsep(&stringp, ","))) 
			ast_copy_string(vmu->password, s, sizeof(vmu->password));
		if (stringp && (s = strsep(&stringp, ","))) 
			ast_copy_string(vmu->fullname, s, sizeof(vmu->fullname));
		if (stringp && (s = strsep(&stringp, ","))) 
			ast_copy_string(vmu->email, s, sizeof(vmu->email));
		if (stringp && (s = strsep(&stringp, ","))) 
			ast_copy_string(vmu->pager, s, sizeof(vmu->pager));
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

static int vm_box_exists(struct ast_channel *chan, void *data) 
{
	struct localuser *u;
	struct ast_vm_user svm;
	char *context, *box;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "MailboxExists requires an argument: (vmbox[@context])\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	box = ast_strdupa(data);
	if (!box) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if ((context = strchr(box, '@'))) {
		*context = '\0';
		context++;
	}

	if (find_user(&svm, context, box)) {
		if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) 
			ast_log(LOG_WARNING, "VM box %s@%s exists, but extension %s, priority %d doesn't exist\n", box, context, chan->exten, chan->priority + 101);
	}
	LOCAL_USER_REMOVE(u);
	return 0;
}

static int vmauthenticate(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	char *s = data, *user=NULL, *context=NULL, mailbox[AST_MAX_EXTENSION];
	struct ast_vm_user vmus;
	char *options = NULL;
	int silent = 0;
	int res = -1;

	LOCAL_USER_ADD(u);
	
	if (s) {
		s = ast_strdupa(s);
		if (!s) {
			ast_log(LOG_ERROR, "Out of memory\n");
			return -1;
		}
		user = strsep(&s, "|");
		options = strsep(&s, "|");
		if (user) {
			s = user;
			user = strsep(&s, "@");
			context = strsep(&s, "");
		}
	}

	if (options) {
		silent = (strchr(options, 's')) != NULL;
	}

	if (!vm_authenticate(chan, mailbox, sizeof(mailbox), &vmus, context, NULL, 0, 3, silent)) {
		pbx_builtin_setvar_helper(chan, "AUTH_MAILBOX", mailbox);
		pbx_builtin_setvar_helper(chan, "AUTH_CONTEXT", vmus.context);
		res = 0;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

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
						if (strlen(vment->d_name) > 7 && !strncmp(vment->d_name + 7,".txt",4))
							vmcount++;
					closedir(vmdir);
				}
				snprintf(count,sizeof(count),"%d",vmcount);
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
	char *send_voicemail;
	char *astcallop;
	char *astreview;
	char *astskipcmd;
	char *asthearenv;
	char *astsaydurationinfo;
	char *astsaydurationminfo;
	char *silencestr;
	char *maxmsgstr;
	char *astdirfwd;
	char *thresholdstr;
	char *fmt;
	char *astemail;
 	char *astmailcmd = SENDMAIL;
	char *s,*q,*stringp;
	char *dialoutcxt = NULL;
	char *callbackcxt = NULL;	
	char *exitcxt = NULL;	
	char *extpc;
	char *emaildateformatstr;
	int x;
	int tmpadsi[4];

	cfg = ast_config_load(VOICEMAIL_CONFIG);
	ast_mutex_lock(&vmlock);
	cur = users;
	while (cur) {
		l = cur;
		cur = cur->next;
		ast_set_flag(l, VM_ALLOCED);	
		free_user(l);
	}
	zcur = zones;
	while (zcur) {
		zl = zcur;
		zcur = zcur->next;
		free_zone(zl);
	}
	zones = NULL;
	zonesl = NULL;
	users = NULL;
	usersl = NULL;
	memset(ext_pass_cmd, 0, sizeof(ext_pass_cmd));

	if (cfg) {
		/* General settings */

		/* Attach voice message to mail message ? */
		if (!(astattach = ast_variable_retrieve(cfg, "general", "attach"))) 
			astattach = "yes";
		ast_set2_flag((&globalflags), ast_true(astattach), VM_ATTACH);	

#ifdef USE_ODBC_STORAGE
		strcpy(odbc_database, "asterisk");
		if ((thresholdstr = ast_variable_retrieve(cfg, "general", "odbcstorage"))) {
			ast_copy_string(odbc_database, thresholdstr, sizeof(odbc_database));
		}
		strcpy(odbc_table, "voicemessages");
                if ((thresholdstr = ast_variable_retrieve(cfg, "general", "odbctable"))) {
                        ast_copy_string(odbc_table, thresholdstr, sizeof(odbc_table));
                }
#endif		
		/* Mail command */
		strcpy(mailcmd, SENDMAIL);
		if ((astmailcmd = ast_variable_retrieve(cfg, "general", "mailcmd")))
			ast_copy_string(mailcmd, astmailcmd, sizeof(mailcmd)); /* User setting */

		maxsilence = 0;
		if ((silencestr = ast_variable_retrieve(cfg, "general", "maxsilence"))) {
			maxsilence = atoi(silencestr);
			if (maxsilence > 0)
				maxsilence *= 1000;
		}
		
		if (!(maxmsgstr = ast_variable_retrieve(cfg, "general", "maxmsg"))) {
			maxmsg = MAXMSG;
		} else {
			maxmsg = atoi(maxmsgstr);
			if (maxmsg <= 0) {
				ast_log(LOG_WARNING, "Invalid number of messages per folder '%s'. Using default value %i\n", maxmsgstr, MAXMSG);
				maxmsg = MAXMSG;
			} else if (maxmsg > MAXMSGLIMIT) {
				ast_log(LOG_WARNING, "Maximum number of messages per folder is %i. Cannot accept value '%s'\n", MAXMSGLIMIT, maxmsgstr);
				maxmsg = MAXMSGLIMIT;
			}
		}

		/* Load date format config for voicemail mail */
		if ((emaildateformatstr = ast_variable_retrieve(cfg, "general", "emaildateformat"))) {
			ast_copy_string(emaildateformat, emaildateformatstr, sizeof(emaildateformat));
		}

		/* External password changing command */
		if ((extpc = ast_variable_retrieve(cfg, "general", "externpass"))) {
			ast_copy_string(ext_pass_cmd,extpc,sizeof(ext_pass_cmd));
		}

		/* External voicemail notify application */
		
		if ((notifystr = ast_variable_retrieve(cfg, "general", "externnotify"))) {
			ast_copy_string(externnotify, notifystr, sizeof(externnotify));
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
		ast_copy_string(serveremail, astemail, sizeof(serveremail));
		
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
				if (maxsilence <= vmminmessage)
					ast_log(LOG_WARNING, "maxsilence should be less than minmessage or you may get empty messages\n");
			} else {
				ast_log(LOG_WARNING, "Invalid min message time length\n");
			}
		}
		fmt = ast_variable_retrieve(cfg, "general", "format");
		if (!fmt)
			fmt = "wav";	
		ast_copy_string(vmfmts, fmt, sizeof(vmfmts));

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

		/* Force new user to record name ? */
		if (!(astattach = ast_variable_retrieve(cfg, "general", "forcename"))) 
			astattach = "no";
		ast_set2_flag((&globalflags), ast_true(astattach), VM_FORCENAME);

		/* Force new user to record greetings ? */
		if (!(astattach = ast_variable_retrieve(cfg, "general", "forcegreetings"))) 
			astattach = "no";
		ast_set2_flag((&globalflags), ast_true(astattach), VM_FORCEGREET);

		if ((s = ast_variable_retrieve(cfg, "general", "cidinternalcontexts"))){
			ast_log(LOG_DEBUG,"VM_CID Internal context string: %s\n",s);
			stringp = ast_strdupa(s);
			for (x = 0 ; x < MAX_NUM_CID_CONTEXTS ; x++){
				if (!ast_strlen_zero(stringp)) {
					q = strsep(&stringp,",");
					while ((*q == ' ')||(*q == '\t')) /* Eat white space between contexts */
						q++;
					ast_copy_string(cidinternalcontexts[x], q, sizeof(cidinternalcontexts[x]));
					ast_log(LOG_DEBUG,"VM_CID Internal context %d: %s\n", x, cidinternalcontexts[x]);
				} else {
					cidinternalcontexts[x][0] = '\0';
				}
			}
		}
		if (!(astreview = ast_variable_retrieve(cfg, "general", "review"))){
			ast_log(LOG_DEBUG,"VM Review Option disabled globally\n");
			astreview = "no";
		}
		ast_set2_flag((&globalflags), ast_true(astreview), VM_REVIEW);	

		if (!(astcallop = ast_variable_retrieve(cfg, "general", "operator"))){
			ast_log(LOG_DEBUG,"VM Operator break disabled globally\n");
			astcallop = "no";
		}
		ast_set2_flag((&globalflags), ast_true(astcallop), VM_OPERATOR);	

		if (!(astsaycid = ast_variable_retrieve(cfg, "general", "saycid"))) {
			ast_log(LOG_DEBUG,"VM CID Info before msg disabled globally\n");
			astsaycid = "no";
		} 
		ast_set2_flag((&globalflags), ast_true(astsaycid), VM_SAYCID);	

		if (!(send_voicemail = ast_variable_retrieve(cfg,"general", "sendvoicemail"))){
			ast_log(LOG_DEBUG,"Send Voicemail msg disabled globally\n");
			send_voicemail = "no";
		}
		ast_set2_flag((&globalflags), ast_true(send_voicemail), VM_SVMAIL);
	
		if (!(asthearenv = ast_variable_retrieve(cfg, "general", "envelope"))) {
			ast_log(LOG_DEBUG,"ENVELOPE before msg enabled globally\n");
			asthearenv = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(asthearenv), VM_ENVELOPE);	

		if (!(astsaydurationinfo = ast_variable_retrieve(cfg, "general", "sayduration"))) {
			ast_log(LOG_DEBUG,"Duration info before msg enabled globally\n");
			astsaydurationinfo = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(astsaydurationinfo), VM_SAYDURATION);	

		saydurationminfo = 2;
		if ((astsaydurationminfo = ast_variable_retrieve(cfg, "general", "saydurationm"))) {
			if (sscanf(astsaydurationminfo, "%d", &x) == 1) {
				saydurationminfo = x;
			} else {
				ast_log(LOG_WARNING, "Invalid min duration for say duration\n");
			}
		}

		if (!(astskipcmd = ast_variable_retrieve(cfg, "general", "nextaftercmd"))) {
			ast_log(LOG_DEBUG,"We are not going to skip to the next msg after save/delete\n");
			astskipcmd = "no";
		}
		ast_set2_flag((&globalflags), ast_true(astskipcmd), VM_SKIPAFTERCMD);

		if ((dialoutcxt = ast_variable_retrieve(cfg, "general", "dialout"))) {
			ast_copy_string(dialcontext, dialoutcxt, sizeof(dialcontext));
			ast_log(LOG_DEBUG, "found dialout context: %s\n", dialcontext);
		} else {
			dialcontext[0] = '\0';	
		}
		
		if ((callbackcxt = ast_variable_retrieve(cfg, "general", "callback"))) {
			ast_copy_string(callcontext, callbackcxt, sizeof(callcontext));
			ast_log(LOG_DEBUG, "found callback context: %s\n", callcontext);
		} else {
			callcontext[0] = '\0';
		}

		if ((exitcxt = ast_variable_retrieve(cfg, "general", "exitcontext"))) {
			ast_copy_string(exitcontext, exitcxt, sizeof(exitcontext));
			ast_log(LOG_DEBUG, "found operator context: %s\n", exitcontext);
		} else {
			exitcontext[0] = '\0';
		}

		if (!(astdirfwd = ast_variable_retrieve(cfg, "general", "usedirectory"))) 
			astdirfwd = "no";
		ast_set2_flag((&globalflags), ast_true(astdirfwd), VM_DIRECFORWARD);	
		cat = ast_category_browse(cfg, NULL);
		while (cat) {
			if (strcasecmp(cat, "general")) {
				var = ast_variable_browse(cfg, cat);
				if (strcasecmp(cat, "zonemessages")) {
					/* Process mailboxes in this context */
					while (var) {
						append_mailbox(cat, var->name, var->value);
						var = var->next;
					}
				} else {
					/* Timezones in this context */
					while (var) {
						struct vm_zone *z;
						z = malloc(sizeof(struct vm_zone));
						if (z != NULL) {
							char *msg_format, *timezone;
							msg_format = ast_strdupa(var->value);
							if (msg_format != NULL) {
								timezone = strsep(&msg_format, "|");
								if (msg_format) {
									ast_copy_string(z->name, var->name, sizeof(z->name));
									ast_copy_string(z->timezone, timezone, sizeof(z->timezone));
									ast_copy_string(z->msg_format, msg_format, sizeof(z->msg_format));
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
		memset(pagerfromstring,0,sizeof(pagerfromstring));
		memset(emailtitle,0,sizeof(emailtitle));
		strcpy(charset, "ISO-8859-1");
		if (emailbody) {
			free(emailbody);
			emailbody = NULL;
		}
		if (emailsubject) {
			free(emailsubject);
			emailsubject = NULL;
		}
               if (pagerbody) {
                       free(pagerbody);
                       pagerbody = NULL;
               }
               if (pagersubject) {
                       free(pagersubject);
                       pagersubject = NULL;
               }
		if ((s=ast_variable_retrieve(cfg, "general", "pbxskip")))
			ast_set2_flag((&globalflags), ast_true(s), VM_PBXSKIP);
		if ((s=ast_variable_retrieve(cfg, "general", "fromstring")))
			ast_copy_string(fromstring,s,sizeof(fromstring));
		if ((s=ast_variable_retrieve(cfg, "general", "pagerfromstring")))
			ast_copy_string(pagerfromstring,s,sizeof(pagerfromstring));
		if ((s=ast_variable_retrieve(cfg, "general", "charset")))
			ast_copy_string(charset,s,sizeof(charset));
		if ((s=ast_variable_retrieve(cfg, "general", "adsifdn"))) {
			sscanf(s, "%2x%2x%2x%2x", &tmpadsi[0], &tmpadsi[1], &tmpadsi[2], &tmpadsi[3]);
			for (x=0; x<4; x++) {
				memcpy(&adsifdn[x], &tmpadsi[x], 1);
			}
		}
		if ((s=ast_variable_retrieve(cfg, "general", "adsisec"))) {
			sscanf(s, "%2x%2x%2x%2x", &tmpadsi[0], &tmpadsi[1], &tmpadsi[2], &tmpadsi[3]);
			for (x=0; x<4; x++) {
				memcpy(&adsisec[x], &tmpadsi[x], 1);
			}
		}
		if ((s=ast_variable_retrieve(cfg, "general", "adsiver")))
			if (atoi(s)) {
				adsiver = atoi(s);
			}
		if ((s=ast_variable_retrieve(cfg, "general", "emailtitle"))) {
			ast_log(LOG_NOTICE, "Keyword 'emailtitle' is DEPRECATED, please use 'emailsubject' instead.\n");
			ast_copy_string(emailtitle,s,sizeof(emailtitle));
		}
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
               if ((s=ast_variable_retrieve(cfg, "general", "pagersubject")))
                       pagersubject = strdup(s);
               if ((s=ast_variable_retrieve(cfg, "general", "pagerbody"))) {
                       char *tmpread, *tmpwrite;
                       pagerbody = strdup(s);

                       /* substitute strings \t and \n into the apropriate characters */
                       tmpread = tmpwrite = pagerbody;
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
		ast_mutex_unlock(&vmlock);
		ast_config_destroy(cfg);
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
	
	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app3);
	res |= ast_unregister_application(app4);
	res |= ast_cli_unregister(&show_voicemail_users_cli);
	res |= ast_cli_unregister(&show_voicemail_zones_cli);
	ast_uninstall_vm_functions();
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, vm_exec, synopsis_vm, descrip_vm);
	res |= ast_register_application(app2, vm_execmain, synopsis_vmain, descrip_vmain);
	res |= ast_register_application(app3, vm_box_exists, synopsis_vm_box_exists, descrip_vm_box_exists);
	res |= ast_register_application(app4, vmauthenticate, synopsis_vmauthenticate, descrip_vmauthenticate);
	if (res)
		return(res);

	if ((res=load_config())) {
		return(res);
	}

	ast_cli_register(&show_voicemail_users_cli);
	ast_cli_register(&show_voicemail_zones_cli);

	/* compute the location of the voicemail spool directory */
	snprintf(VM_SPOOL_DIR, sizeof(VM_SPOOL_DIR), "%s/voicemail/", ast_config_AST_SPOOL_DIR);

	ast_install_vm_functions(has_voicemail, messagecount);

#if defined(USE_ODBC_STORAGE) && !defined(EXTENDED_ODBC_STORAGE)
	ast_log(LOG_WARNING, "The current ODBC storage table format will be changed soon."
				"Please update your tables as per the README and edit the apps/Makefile "
				"and uncomment the line containing EXTENDED_ODBC_STORAGE to enable the "
				"new table format.\n");
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
			destination[0] = cmd = ast_play_and_wait(chan,"vm-enter-num-to-call");
			if (!cmd)
				destination[0] = cmd = ast_play_and_wait(chan, "vm-then-pound");
			if (!cmd)
				destination[0] = cmd = ast_play_and_wait(chan, "vm-star-cancel");
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
		ast_copy_string(destination, num, sizeof(destination));
	}

	if (!ast_strlen_zero(destination)) {
		if (destination[strlen(destination) -1 ] == '*')
			return 0; 
		ast_verbose( VERBOSE_PREFIX_3 "Placing outgoing call to extension '%s' in context '%s' from context '%s'\n", destination, outgoing_context, chan->context);
		ast_copy_string(chan->exten, destination, sizeof(chan->exten));
		ast_copy_string(chan->context, outgoing_context, sizeof(chan->context));
		chan->priority = 0;
		return 9;
	}
	return 0;
}

static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg,
			    int option, signed char record_gain)
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
	RETRIEVE(vms->curdir, vms->curmsg);
	msg_cfg = ast_config_load(filename);
	DISPOSE(vms->curdir, vms->curmsg);
	if (!msg_cfg) {
		ast_log(LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
		return 0;
	}

	if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime")))
		return 0;

	cid = ast_variable_retrieve(msg_cfg, "message", "callerid");

	context = ast_variable_retrieve(msg_cfg, "message", "context");
	if (!strncasecmp("macro",context,5)) /* Macro names in contexts are useless for our needs */
		context = ast_variable_retrieve(msg_cfg, "message","macrocontext");

	if (option == 3) {

		if (!res)
			res = play_message_datetime(chan, vmu, origtime, filename);
		if (!res)
			res = play_message_callerid(chan, vms, cid, context, 0);
	} else if (option == 2) { /* Call back */

		if (!ast_strlen_zero(cid)) {
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
						if (!ast_strlen_zero(vmu->dialout)) {
							res = dialout(chan, vmu, NULL, vmu->dialout);
							if (res)
								return 9;
						} else {
							ast_verbose( VERBOSE_PREFIX_3 "Caller can not specify callback number - no dialout context available\n");
							res = ast_play_and_wait(chan, "vm-sorry");
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

						res = ast_play_and_wait(chan, "vm-sorry");
						retries++;
						break;
					default:
						if (num) {
							ast_verbose( VERBOSE_PREFIX_3 "Confirm CID number '%s' is number to use for callback\n", num);
							res = ast_play_and_wait(chan, "vm-num-i-have");
							if (!res)
								res = play_message_callerid(chan, vms, num, vmu->context, 1);
							if (!res)
								res = ast_play_and_wait(chan, "vm-tocallnum");
							/* Only prompt for a caller-specified number if there is a dialout context specified */
							if (!ast_strlen_zero(vmu->dialout)) {
								if (!res)
									res = ast_play_and_wait(chan, "vm-calldiffnum");
							}
						} else {
							res = ast_play_and_wait(chan, "vm-nonumber");
							if (!ast_strlen_zero(vmu->dialout)) {
								if (!res)
									res = ast_play_and_wait(chan, "vm-toenternumber");
							}
						}
						if (!res)
							res = ast_play_and_wait(chan, "vm-star-cancel");
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
		if (!ast_strlen_zero(cid)) {
			ast_callerid_parse(cid, &name, &num);
			if (!num) {
				ast_verbose(VERBOSE_PREFIX_3 "No CID number available, no reply sent\n");
				if (!res)
					res = ast_play_and_wait(chan, "vm-nonumber");
				return res;
			} else {
				if (find_user(NULL, vmu->context, num)) {
					struct leave_vm_options leave_options;

					ast_verbose(VERBOSE_PREFIX_3 "Leaving voicemail for '%s' in context '%s'\n", num, vmu->context);
					
					memset(&leave_options, 0, sizeof(leave_options));
					leave_options.record_gain = record_gain;
					res = leave_voicemail(chan, num, &leave_options);
					if (!res)
						res = 't';
					return res;
				} else {
					/* Sender has no mailbox, can't reply */
					ast_verbose( VERBOSE_PREFIX_3 "No mailbox number '%s' in context '%s', no reply sent\n", num, vmu->context);
					ast_play_and_wait(chan, "vm-nobox");
					res = 't';
					return res;
				}
			} 
			res = 0;
		}
	}

	ast_config_destroy(msg_cfg);

	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);
		vms->heard[msg] = 1;
		res = wait_file(chan, vms, vms->fn);
	}
	return res;
}
 
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt,
			      int outsidecaller, struct ast_vm_user *vmu, int *duration, const char *unlockdir,
			      signed char record_gain)
{
	/* Record message & let caller review or re-record it, or set options if applicable */
 	int res = 0;
 	int cmd = 0;
 	int max_attempts = 3;
 	int attempts = 0;
 	int recorded = 0;
 	int message_exists = 0;
	signed char zero_gain = 0;
 	/* Note that urgent and private are for flagging messages as such in the future */
 
	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_record_review called without duration pointer\n");
		return -1;
	}

 	cmd = '3';	 /* Want to start by recording */
 
	while ((cmd >= 0) && (cmd != 't')) {
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
				STORE(recordfile, vmu->mailbox, vmu->context, -1);
				DISPOSE(recordfile, -1);
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
 				cmd = ast_play_and_wait(chan, INTRO);
 				cmd = ast_play_and_wait(chan, "beep");
 			}
 			recorded = 1;
 			/* After an attempt has been made to record message, we have to take care of INTRO and beep for incoming messages, but not for greetings */
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);
			cmd = ast_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, silencethreshold, maxsilence, unlockdir);
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &zero_gain, sizeof(zero_gain), 0);
 			if (cmd == -1) {
 			/* User has hung up, no options to give */
 				return cmd;
			}
 			if (cmd == '0') {
 				break;
 			} else if (cmd == '*') {
 				break;
 			} 
#if 0			
 			else if (vmu->review && (*duration < 5)) {
 				/* Message is too short */
 				ast_verbose(VERBOSE_PREFIX_3 "Message too short\n");
				cmd = ast_play_and_wait(chan, "vm-tooshort");
 				cmd = vm_delete(recordfile);
 				break;
 			}
 			else if (vmu->review && (cmd == 2 && *duration < (maxsilence + 3))) {
 				/* Message is all silence */
 				ast_verbose(VERBOSE_PREFIX_3 "Nothing recorded\n");
 				cmd = vm_delete(recordfile);
				cmd = ast_play_and_wait(chan, "vm-nothingrecorded");
				if (!cmd)
 					cmd = ast_play_and_wait(chan, "vm-speakup");
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
 			cmd = ast_play_and_wait(chan, "vm-sorry");
 			break;
#if 0 
/*  XXX Commented out for the moment because of the dangers of deleting
    a message while recording (can put the message numbers out of sync) */
 		case '*':
 			/* Cancel recording, delete message, offer to take another message*/
 			cmd = ast_play_and_wait(chan, "vm-deleted");
 			cmd = vm_delete(recordfile);
 			if (outsidecaller) {
 				res = vm_exec(chan, NULL);
 				return res;
 			}
 			else
 				return 1;
#endif
 		case '0':
			if (message_exists || recorded) {
				cmd = ast_play_and_wait(chan, "vm-saveoper");
				if (!cmd)
					cmd = ast_waitfordigit(chan, 3000);
				if (cmd == '1') {
					ast_play_and_wait(chan, "vm-msgsaved");
					cmd = '0';
				} else {
					ast_play_and_wait(chan, "vm-deleted");
					DELETE(recordfile, -1, recordfile);
					cmd = '0';
				}
			}
			return cmd;
 		default:
			/* If the caller is an ouside caller, and the review option is enabled,
			   allow them to review the message, but let the owner of the box review
			   their OGM's */
			if (outsidecaller && !ast_test_flag(vmu, VM_REVIEW))
				return cmd;
 			if (message_exists) {
 				cmd = ast_play_and_wait(chan, "vm-review");
 			}
 			else {
 				cmd = ast_play_and_wait(chan, "vm-torerecord");
 				if (!cmd)
 					cmd = ast_waitfordigit(chan, 600);
 			}
 			
 			if (!cmd && outsidecaller && ast_test_flag(vmu, VM_OPERATOR)) {
 				cmd = ast_play_and_wait(chan, "vm-reachoper");
 				if (!cmd)
 					cmd = ast_waitfordigit(chan, 600);
 			}
#if 0
			if (!cmd)
 				cmd = ast_play_and_wait(chan, "vm-tocancelmsg");
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
		ast_play_and_wait(chan, "vm-goodbye");
 	if (cmd == 't')
 		cmd = 0;
 	return cmd;
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

