/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \author Mark Spencer <markster@digium.com>
 * 
 * \par See also
 * \arg \ref Config_vm
 * \ingroup applications
 * \note This module requires res_adsi to load.
 */

/*** MODULEINFO
	<depend>res_adsi</depend>
	<depend>res_smdi</depend>
 ***/

/*** MAKEOPTS
<category name="MENUSELECT_OPTS_app_voicemail" displayname="Voicemail Build Options" positive_output="yes" remove_on_change="apps/app_voicemail.o apps/app_voicemail.so apps/app_directory.o apps/app_directory.so">
	<member name="ODBC_STORAGE" displayname="Storage of Voicemail using ODBC">
		<depend>unixodbc</depend>
		<depend>ltdl</depend>
		<conflict>IMAP_STORAGE</conflict>
		<defaultenabled>no</defaultenabled>
	</member>
	<member name="IMAP_STORAGE" displayname="Storage of Voicemail using IMAP4">
		<depend>imap_tk</depend>
		<conflict>ODBC_STORAGE</conflict>
		<use>ssl</use>
		<defaultenabled>no</defaultenabled>
	</member>
</category>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#ifdef IMAP_STORAGE
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#ifdef USE_SYSTEM_IMAP
#include <imap/c-client.h>
#include <imap/imap4r1.h>
#include <imap/linkage.h>
#elif defined (USE_SYSTEM_CCLIENT)
#include <c-client/c-client.h>
#include <c-client/imap4r1.h>
#include <c-client/linkage.h>
#else
#include "c-client.h"
#include "imap4r1.h"
#include "linkage.h"
#endif
#endif
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
#include "asterisk/stringfields.h"
#include "asterisk/smdi.h"
#include "asterisk/astobj2.h"
#ifdef ODBC_STORAGE
#include "asterisk/res_odbc.h"
#endif

#ifdef IMAP_STORAGE
#include "asterisk/threadstorage.h"

AST_MUTEX_DEFINE_STATIC(imaptemp_lock);
static char imaptemp[1024];
static char imapserver[48];
static char imapport[8];
static char imapflags[128];
static char imapfolder[64];
static char authuser[32];
static char authpassword[42];
static int imapversion = 1;

static int expungeonhangup = 1;
static char delimiter = '\0';
static const long DEFAULT_IMAP_TCP_TIMEOUT = 60L;

struct vm_state;
struct ast_vm_user;

AST_THREADSTORAGE(ts_vmstate, ts_vmstate_init);

static int init_mailstream (struct vm_state *vms, int box);
static void write_file (char *filename, char *buffer, unsigned long len);
/*static void status (MAILSTREAM *stream); */ /* No need for this. */
static char *get_header_by_tag(char *header, char *tag);
static void vm_imap_delete(char *file, int msgnum, struct ast_vm_user *vmu);
static char *get_user_by_mailbox(char *mailbox);
static struct vm_state *get_vm_state_by_imapuser(char *user, int interactive);
static struct vm_state *get_vm_state_by_mailbox(const char *mailbox, const char *context, int interactive);
static struct vm_state *create_vm_state_from_user(struct ast_vm_user *vmu);
static void vmstate_insert(struct vm_state *vms);
static void vmstate_delete(struct vm_state *vms);
static void set_update(MAILSTREAM * stream);
static void init_vm_state(struct vm_state *vms);
static void copy_msgArray(struct vm_state *dst, struct vm_state *src);
static int save_body(BODY *body, struct vm_state *vms, char *section, char *format);
static void get_mailbox_delimiter(MAILSTREAM *stream);
static void mm_parsequota (MAILSTREAM *stream, unsigned char *msg, QUOTALIST *pquota);
static void imap_mailbox_name(char *spec, size_t len, struct vm_state *vms, int box, int target);
static int imap_store_file(char *dir, char *mailboxuser, char *mailboxcontext, int msgnum, struct ast_channel *chan, struct ast_vm_user *vmu, char *fmt, int duration, struct vm_state *vms);
static void check_quota(struct vm_state *vms, char *mailbox);
static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu,int box);
struct vmstate {
	struct vm_state *vms;
	struct vmstate *next;
};
AST_MUTEX_DEFINE_STATIC(vmstate_lock);
static struct vmstate *vmstates = NULL;
#endif

#define SMDI_MWI_WAIT_TIMEOUT 1000 /* 1 second */

#define COMMAND_TIMEOUT 5000
/* Don't modify these here; set your umask at runtime instead */
#define	VOICEMAIL_DIR_MODE	0777
#define	VOICEMAIL_FILE_MODE	0666
#define	CHUNKSIZE	65536

#define VOICEMAIL_CONFIG "voicemail.conf"
#define ASTERISK_USERNAME "asterisk"

/* Default mail command to mail voicemail. Change it with the
    mailcmd= command in voicemail.conf */
#define SENDMAIL "/usr/sbin/sendmail -t"

#define INTRO "vm-intro"

#define MAXMSG 100
#ifndef IMAP_STORAGE
#define MAXMSGLIMIT 9999
#else
#define MAXMSGLIMIT 255
#endif

#define BASEMAXINLINE 256
#define BASELINELEN 72
#define BASEMAXINLINE 256
#ifdef IMAP_STORAGE
#define ENDL "\r\n"
#else
#define ENDL "\n"
#endif

#define MAX_DATETIME_FORMAT	512
#define MAX_NUM_CID_CONTEXTS 10

#define VM_REVIEW        (1 << 0)
#define VM_OPERATOR      (1 << 1)
#define VM_SAYCID        (1 << 2)
#define VM_SVMAIL        (1 << 3)
#define VM_ENVELOPE      (1 << 4)
#define VM_SAYDURATION   (1 << 5)
#define VM_SKIPAFTERCMD  (1 << 6)
#define VM_FORCENAME     (1 << 7)   /*!< Have new users record their name */
#define VM_FORCEGREET    (1 << 8)   /*!< Have new users record their greetings */
#define VM_PBXSKIP       (1 << 9)
#define VM_DIRECFORWARD  (1 << 10)  /*!< directory_forward */
#define VM_ATTACH        (1 << 11)
#define VM_DELETE        (1 << 12)
#define VM_ALLOCED       (1 << 13)
#define VM_SEARCH        (1 << 14)
#define VM_TEMPGREETWARN (1 << 15)  /*!< Remind user tempgreeting is set */
#define ERROR_LOCK_PATH  -100
#define ERROR_MAILBOX_FULL	-200
#define OPERATOR_EXIT		300


enum {
	OPT_SILENT =           (1 << 0),
	OPT_BUSY_GREETING =    (1 << 1),
	OPT_UNAVAIL_GREETING = (1 << 2),
	OPT_RECORDGAIN =       (1 << 3),
	OPT_PREPEND_MAILBOX =  (1 << 4),
	OPT_PRIORITY_JUMP =    (1 << 5),
	OPT_AUTOPLAY =         (1 << 6),
} vm_option_flags;

enum {
	OPT_ARG_RECORDGAIN = 0,
	OPT_ARG_PLAYFOLDER = 1,
	/* This *must* be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE = 2,
} vm_option_args;

AST_APP_OPTIONS(vm_app_options, {
	AST_APP_OPTION('s', OPT_SILENT),
	AST_APP_OPTION('b', OPT_BUSY_GREETING),
	AST_APP_OPTION('u', OPT_UNAVAIL_GREETING),
	AST_APP_OPTION_ARG('g', OPT_RECORDGAIN, OPT_ARG_RECORDGAIN),
	AST_APP_OPTION('p', OPT_PREPEND_MAILBOX),
	AST_APP_OPTION('j', OPT_PRIORITY_JUMP),
	AST_APP_OPTION_ARG('a', OPT_AUTOPLAY, OPT_ARG_PLAYFOLDER),
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
	\arg \b pt - Polish
	\arg \b pt - Portuguese
	\arg \b pt_BR - Portuguese (Brazil)
	\arg \b gr - Greek
	\arg \b no - Norwegian
	\arg \b se - Swedish
	\arg \b ua - Ukrainian
	\arg \b he - Hebrew

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

Polish uses:
\arg \b vm-new-a	'new', feminine singular accusative
\arg \b vm-new-e	'new', feminine plural accusative
\arg \b vm-new-ych	'new', feminine plural genitive
\arg \b vm-old-a	'old', feminine singular accusative
\arg \b vm-old-e	'old', feminine plural accusative
\arg \b vm-old-ych	'old', feminine plural genitive
\arg \b digits/1-a	'one', not always same as 'digits/1'
\arg \b digits/2-ie	'two', not always same as 'digits/2'

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

Hebrew also uses:
\arg \b vm-INBOX1 '1 new message'
\arg \b vm-OLD1   '1 old message'
\arg \b vm-shtei	'shtei'
\arg \b vm-nomessages 'you have no new messages'

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
	char context[AST_MAX_CONTEXT];   /*!< Voicemail context */
	char mailbox[AST_MAX_EXTENSION]; /*!< Mailbox id, unique within vm context */
	char password[80];               /*!< Secret pin code, numbers only */
	char fullname[80];               /*!< Full name, for directory app */
	char email[80];                  /*!< E-mail address */
	char pager[80];                  /*!< E-mail address to pager (no attachment) */
	char serveremail[80];            /*!< From: Mail address */
	char mailcmd[160];               /*!< Configurable mail command */
	char language[MAX_LANGUAGE];     /*!< Config: Language setting */
	char zonetag[80];                /*!< Time zone */
	char callback[80];
	char dialout[80];
	char uniqueid[80];               /*!< Unique integer identifier */
	char exit[80];
	char attachfmt[20];              /*!< Attachment format */
	unsigned int flags;              /*!< VM_ flags */	
	int saydurationm;
	int maxmsg;                      /*!< Maximum number of msgs per folder for this mailbox */
#ifdef IMAP_STORAGE
	char imapuser[80];	/* IMAP server login */
	char imappassword[80];	/* IMAP server password if authpassword not defined */
	char imapvmshareid[80]; /* Shared mailbox ID to use rather than the dialed one */
	int imapversion;                 /*!< If configuration changes, use the new values */
#endif
	double volgain;		/*!< Volume gain for voicemails sent via email */
	AST_LIST_ENTRY(ast_vm_user) list;
};

struct vm_zone {
	AST_LIST_ENTRY(vm_zone) list;
	char name[80];
	char timezone[80];
	char msg_format[512];
};

struct vm_state {
	char curbox[80];
	char username[80];
	char context[80];
	char curdir[PATH_MAX];
	char vmbox[PATH_MAX];
	char fn[PATH_MAX];
	char fn2[PATH_MAX];
	int *deleted;
	int *heard;
	int dh_arraysize; /* used for deleted / heard allocation */
	int curmsg;
	int lastmsg;
	int newmessages;
	int oldmessages;
	int starting;
	int repeats;
#ifdef IMAP_STORAGE
	ast_mutex_t lock;
	int updated; /* decremented on each mail check until 1 -allows delay */
	long msgArray[256];
	MAILSTREAM *mailstream;
	int vmArrayIndex;
	char imapuser[80]; /* IMAP server login */
	int imapversion;
	int interactive;
	unsigned int quota_limit;
	unsigned int quota_usage;
	struct vm_state *persist_vms;
#endif
};
static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg, int option, signed char record_gain);
static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context);
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime,
			char *fmt, int outsidecaller, struct ast_vm_user *vmu, int *duration, const char *unlockdir,
			signed char record_gain, struct vm_state *vms);
static int vm_tempgreeting(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain);
static int vm_play_folder_name(struct ast_channel *chan, char *mbox);
static int notify_new_message(struct ast_channel *chan, struct ast_vm_user *vmu, int msgnum, long duration, char *fmt, char *cidnum, char *cidname);
static void make_email_file(FILE *p, char *srcemail, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, const char *folder, char *cidnum, char *cidname, char *attach, char *format, int duration, int attach_user_voicemail, struct ast_channel *chan, const char *category, int imap);
#if !(defined(ODBC_STORAGE) || defined(IMAP_STORAGE))
static int __has_voicemail(const char *context, const char *mailbox, const char *folder, int shortcircuit);
#endif
static void apply_options(struct ast_vm_user *vmu, const char *options);

struct ao2_container *inprocess_container;

struct inprocess {
	int count;
	char *context;
	char mailbox[0];
};

static int inprocess_hash_fn(const void *obj, const int flags)
{
	const struct inprocess *i = obj;
	return atoi(i->mailbox);
}

static int inprocess_cmp_fn(void *obj, void *arg, int flags)
{
	struct inprocess *i = obj, *j = arg;
	if (strcmp(i->mailbox, j->mailbox)) {
		return 0;
	}
	return !strcmp(i->context, j->context) ? CMP_MATCH : 0;
}

static int inprocess_count(const char *context, const char *mailbox, int delta)
{
	struct inprocess *i, *arg = alloca(sizeof(*arg) + strlen(context) + strlen(mailbox) + 2);
	arg->context = arg->mailbox + strlen(mailbox) + 1;
	strcpy(arg->mailbox, mailbox); /* SAFE */
	strcpy(arg->context, context); /* SAFE */
	ao2_lock(inprocess_container);
	if ((i = ao2_find(inprocess_container, arg, 0))) {
		int ret = ast_atomic_fetchadd_int(&i->count, delta);
		ao2_unlock(inprocess_container);
		ao2_ref(i, -1);
		return ret;
	}
	if (delta < 0) {
		ast_log(LOG_WARNING, "BUG: ref count decrement on non-existing object???\n");
	}
	if (!(i = ao2_alloc(sizeof(*i) + strlen(context) + strlen(mailbox) + 2, NULL))) {
		ao2_unlock(inprocess_container);
		return 0;
	}
	i->context = i->mailbox + strlen(mailbox) + 1;
	strcpy(i->mailbox, mailbox); /* SAFE */
	strcpy(i->context, context); /* SAFE */
	i->count = delta;
	ao2_link(inprocess_container, i);
	ao2_unlock(inprocess_container);
	ao2_ref(i, -1);
	return 0;
}

#ifdef ODBC_STORAGE
static char odbc_database[80];
static char odbc_table[80];
#define RETRIEVE(a,b,c) retrieve_file(a,b)
#define DISPOSE(a,b) remove_file(a,b)
#define STORE(a,b,c,d,e,f,g,h,i) store_file(a,b,c,d)
#define EXISTS(a,b,c,d) (message_exists(a,b))
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(a,b,c,d,e,f))
#define COPY(a,b,c,d,e,f,g,h) (copy_file(a,b,c,d,e,f))
#define DELETE(a,b,c,d) (delete_file(a,b))
#else
#ifdef IMAP_STORAGE
#define RETRIEVE(a,b,c) imap_retrieve_file(a,b,c)
#define DISPOSE(a,b) remove_file(a,b)
#define STORE(a,b,c,d,e,f,g,h,i) (imap_store_file(a,b,c,d,e,f,g,h,i))
#define EXISTS(a,b,c,d) (ast_fileexists(c,NULL,d) > 0)
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(g,h));
#define COPY(a,b,c,d,e,f,g,h) (copy_file(g,h));
#define DELETE(a,b,c,d) (vm_imap_delete(a,b,d))
#else
#define RETRIEVE(a,b,c)
#define DISPOSE(a,b)
#define STORE(a,b,c,d,e,f,g,h,i)
#define EXISTS(a,b,c,d) (ast_fileexists(c,NULL,d) > 0)
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(g,h));
#define COPY(a,b,c,d,e,f,g,h) (copy_plain_file(g,h));
#define DELETE(a,b,c,d) (vm_delete(c))
#endif
#endif

static char VM_SPOOL_DIR[PATH_MAX];

static char ext_pass_cmd[128];

static int my_umask;

#if ODBC_STORAGE
#define tdesc "Comedian Mail (Voicemail System) with ODBC Storage"
#elif IMAP_STORAGE
#define tdesc "Comedian Mail (Voicemail System) with IMAP Storage"
#else
#define tdesc "Comedian Mail (Voicemail System)"
#endif

static char userscontext[AST_MAX_EXTENSION] = "default";

static char *addesc = "Comedian Mail";

static char *synopsis_vm =
"Leave a Voicemail message";

static char *descrip_vm =
"  VoiceMail(mailbox[@context][&mailbox[@context]][...][|options]): This\n"
"application allows the calling party to leave a message for the specified\n"
"list of mailboxes. When multiple mailboxes are specified, the greeting will\n"
"be taken from the first mailbox specified. Dialplan execution will stop if the\n"
"specified mailbox does not exist.\n"
"  The Voicemail application will exit if any of the following DTMF digits are\n"
"received:\n"
"    0 - Jump to the 'o' extension in the current dialplan context.\n"
"    * - Jump to the 'a' extension in the current dialplan context.\n"
"  This application will set the following channel variable upon completion:\n"
"    VMSTATUS - This indicates the status of the execution of the VoiceMail\n"
"               application. The possible values are:\n"
"               SUCCESS | USEREXIT | FAILED\n\n"
"  Options:\n"
"    b    - Play the 'busy' greeting to the calling party.\n"
"    g(#) - Use the specified amount of gain when recording the voicemail\n"
"           message. The units are whole-number decibels (dB).\n"
"           Only works on supported technologies, which is Zap only.\n"
"    s    - Skip the playback of instructions for leaving a message to the\n"
"           calling party.\n"
"    u    - Play the 'unavailable' greeting.\n"
"    j    - Jump to priority n+101 if the mailbox is not found or some other\n"
"           error occurs.\n";

static char *synopsis_vmain =
"Check Voicemail messages";

static char *descrip_vmain =
"  VoiceMailMain([mailbox][@context][|options]): This application allows the\n"
"calling party to check voicemail messages. A specific mailbox, and optional\n"
"corresponding context, may be specified. If a mailbox is not provided, the\n"
"calling party will be prompted to enter one. If a context is not specified,\n"
"the 'default' context will be used.\n\n"
"  Options:\n"
"    p    - Consider the mailbox parameter as a prefix to the mailbox that\n"
"           is entered by the caller.\n"
"    g(#) - Use the specified amount of gain when recording a voicemail\n"
"           message. The units are whole-number decibels (dB).\n"
"    s    - Skip checking the passcode for the mailbox.\n"
"    a(#) - Skip folder prompt and go directly to folder specified.\n"
"           Defaults to INBOX\n";

static char *synopsis_vm_box_exists =
"Check to see if Voicemail mailbox exists";

static char *descrip_vm_box_exists =
"  MailboxExists(mailbox[@context][|options]): Check to see if the specified\n"
"mailbox exists. If no voicemail context is specified, the 'default' context\n"
"will be used.\n"
"  This application will set the following channel variable upon completion:\n"
"    VMBOXEXISTSSTATUS - This will contain the status of the execution of the\n"
"                        MailboxExists application. Possible values include:\n"
"                        SUCCESS | FAILED\n\n"
"  Options:\n"
"    j - Jump to priority n+101 if the mailbox is found.\n";

static char *synopsis_vmauthenticate =
"Authenticate with Voicemail passwords";

static char *descrip_vmauthenticate =
"  VMAuthenticate([mailbox][@context][|options]): This application behaves the\n"
"same way as the Authenticate application, but the passwords are taken from\n"
"voicemail.conf.\n"
"  If the mailbox is specified, only that mailbox's password will be considered\n"
"valid. If the mailbox is not specified, the channel variable AUTH_MAILBOX will\n"
"be set with the authenticated mailbox.\n\n"
"  Options:\n"
"    s - Skip playing the initial prompts.\n";

/* Leave a message */
static char *app = "VoiceMail";

/* Check mail, control, etc */
static char *app2 = "VoiceMailMain";

static char *app3 = "MailboxExists";
static char *app4 = "VMAuthenticate";

static AST_LIST_HEAD_STATIC(users, ast_vm_user);
static AST_LIST_HEAD_STATIC(zones, vm_zone);
static char zonetag[80];
static int maxsilence;
static int maxmsg;
static int silencethreshold = 128;
static char serveremail[80];
static char mailcmd[160];	/* Configurable mail cmd */
static char externnotify[160]; 
static struct ast_smdi_interface *smdi_iface = NULL;
static char vmfmts[80];
static double volgain;
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


/*!
 * \brief Strips control and non 7-bit clean characters from input string.
 *
 * \note To map control and none 7-bit characters to a 7-bit clean characters
 *  please use ast_str_encode_mine().
 */
static char *strip_control_and_high(const char *input, char *buf, size_t buflen)
{
	char *bufptr = buf;
	for (; *input; input++) {
		if (*input < 32) {
			continue;
		}
		*bufptr++ = *input;
		if (bufptr == buf + buflen - 1) {
			break;
		}
	}
	*bufptr = '\0';
	return buf;
}

static void populate_defaults(struct ast_vm_user *vmu)
{
	ast_copy_flags(vmu, (&globalflags), AST_FLAGS_ALL);	
	if (saydurationminfo)
		vmu->saydurationm = saydurationminfo;
	ast_copy_string(vmu->callback, callcontext, sizeof(vmu->callback));
	ast_copy_string(vmu->dialout, dialcontext, sizeof(vmu->dialout));
	ast_copy_string(vmu->exit, exitcontext, sizeof(vmu->exit));
	ast_copy_string(vmu->zonetag, zonetag, sizeof(vmu->zonetag));
	if (maxmsg)
		vmu->maxmsg = maxmsg;
	vmu->volgain = volgain;
}

static void apply_option(struct ast_vm_user *vmu, const char *var, const char *value)
{
	int x;
	if (!strcasecmp(var, "attach")) {
		ast_set2_flag(vmu, ast_true(value), VM_ATTACH);
	} else if (!strcasecmp(var, "attachfmt")) {
		ast_copy_string(vmu->attachfmt, value, sizeof(vmu->attachfmt));
	} else if (!strcasecmp(var, "serveremail")) {
		ast_copy_string(vmu->serveremail, value, sizeof(vmu->serveremail));
	} else if (!strcasecmp(var, "language")) {
		ast_copy_string(vmu->language, value, sizeof(vmu->language));
	} else if (!strcasecmp(var, "tz")) {
		ast_copy_string(vmu->zonetag, value, sizeof(vmu->zonetag));
#ifdef IMAP_STORAGE
	} else if (!strcasecmp(var, "imapuser")) {
		ast_copy_string(vmu->imapuser, value, sizeof(vmu->imapuser));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imappassword") || !strcasecmp(var, "imapsecret")) {
		ast_copy_string(vmu->imappassword, value, sizeof(vmu->imappassword));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imapvmshareid")) {
		ast_copy_string(vmu->imapvmshareid, value, sizeof(vmu->imapvmshareid));
		vmu->imapversion = imapversion;
#endif
	} else if (!strcasecmp(var, "delete") || !strcasecmp(var, "deletevoicemail")) {
		ast_set2_flag(vmu, ast_true(value), VM_DELETE);	
	} else if (!strcasecmp(var, "saycid")){
		ast_set2_flag(vmu, ast_true(value), VM_SAYCID);	
	} else if (!strcasecmp(var,"sendvoicemail")){
		ast_set2_flag(vmu, ast_true(value), VM_SVMAIL);	
	} else if (!strcasecmp(var, "review")){
		ast_set2_flag(vmu, ast_true(value), VM_REVIEW);
	} else if (!strcasecmp(var, "tempgreetwarn")){
		ast_set2_flag(vmu, ast_true(value), VM_TEMPGREETWARN);	
	} else if (!strcasecmp(var, "operator")){
		ast_set2_flag(vmu, ast_true(value), VM_OPERATOR);	
	} else if (!strcasecmp(var, "envelope")){
		ast_set2_flag(vmu, ast_true(value), VM_ENVELOPE);	
	} else if (!strcasecmp(var, "sayduration")){
		ast_set2_flag(vmu, ast_true(value), VM_SAYDURATION);	
	} else if (!strcasecmp(var, "saydurationm")){
		if (sscanf(value, "%30d", &x) == 1) {
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
	} else if (!strcasecmp(var, "volgain")) {
		sscanf(value, "%30lf", &vmu->volgain);
	} else if (!strcasecmp(var, "options")) {
		apply_options(vmu, value);
	}
}

static int change_password_realtime(struct ast_vm_user *vmu, const char *password)
{
	int res = -1;
	if (!strcmp(vmu->password, password)) {
		/* No change (but an update would return 0 rows updated, so we opt out here) */
		res = 0;
	} else if (!ast_strlen_zero(vmu->uniqueid)) {
		if (ast_update_realtime("voicemail", "uniqueid", vmu->uniqueid, "password", password, NULL) > 0) {
			ast_copy_string(vmu->password, password, sizeof(vmu->password));
			res = 0;
		}
	}
	return res;
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

static void apply_options_full(struct ast_vm_user *retval, struct ast_variable *var)
{
	struct ast_variable *tmp;
	tmp = var;
	while (tmp) {
		if (!strcasecmp(tmp->name, "vmsecret")) {
			ast_copy_string(retval->password, tmp->value, sizeof(retval->password));
		} else if (!strcasecmp(tmp->name, "secret") || !strcasecmp(tmp->name, "password")) { /* don't overwrite vmsecret if it exists */
			if (ast_strlen_zero(retval->password))
				ast_copy_string(retval->password, tmp->value, sizeof(retval->password));
		} else if (!strcasecmp(tmp->name, "uniqueid")) {
			ast_copy_string(retval->uniqueid, tmp->value, sizeof(retval->uniqueid));
		} else if (!strcasecmp(tmp->name, "pager")) {
			ast_copy_string(retval->pager, tmp->value, sizeof(retval->pager));
		} else if (!strcasecmp(tmp->name, "email")) {
			ast_copy_string(retval->email, tmp->value, sizeof(retval->email));
		} else if (!strcasecmp(tmp->name, "fullname")) {
			ast_copy_string(retval->fullname, tmp->value, sizeof(retval->fullname));
		} else if (!strcasecmp(tmp->name, "context")) {
			ast_copy_string(retval->context, tmp->value, sizeof(retval->context));
#ifdef IMAP_STORAGE
		} else if (!strcasecmp(tmp->name, "imapuser")) {
			ast_copy_string(retval->imapuser, tmp->value, sizeof(retval->imapuser));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(tmp->name, "imappassword") || !strcasecmp(tmp->name, "imapsecret")) {
			ast_copy_string(retval->imappassword, tmp->value, sizeof(retval->imappassword));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(tmp->name, "imapvmshareid")) {
			ast_copy_string(retval->imapvmshareid, tmp->value, sizeof(retval->imapvmshareid));
			retval->imapversion = imapversion;
#endif
		} else
			apply_option(retval, tmp->name, tmp->value);
		tmp = tmp->next;
	} 
}

static struct ast_vm_user *find_user_realtime(struct ast_vm_user *ivm, const char *context, const char *mailbox)
{
	struct ast_variable *var;
	struct ast_vm_user *retval;

	if ((retval = (ivm ? ivm : ast_calloc(1, sizeof(*retval))))) {
		if (!ivm)
			ast_set_flag(retval, VM_ALLOCED);	
		else
			memset(retval, 0, sizeof(*retval));
		if (mailbox) 
			ast_copy_string(retval->mailbox, mailbox, sizeof(retval->mailbox));
		populate_defaults(retval);
		if (!context && ast_test_flag((&globalflags), VM_SEARCH))
			var = ast_load_realtime("voicemail", "mailbox", mailbox, NULL);
		else
			var = ast_load_realtime("voicemail", "mailbox", mailbox, "context", context, NULL);
		if (var) {
			apply_options_full(retval, var);
			ast_variables_destroy(var);
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
	AST_LIST_LOCK(&users);

	if (!context && !ast_test_flag((&globalflags), VM_SEARCH))
		context = "default";

	AST_LIST_TRAVERSE(&users, cur, list) {
#ifdef IMAP_STORAGE
		if (cur->imapversion != imapversion) {
			continue;
		}
#endif
		if (ast_test_flag((&globalflags), VM_SEARCH) && !strcasecmp(mailbox, cur->mailbox))
			break;
		if (context && (!strcasecmp(context, cur->context)) && (!strcasecmp(mailbox, cur->mailbox)))
			break;
	}
	if (cur) {
		/* Make a copy, so that on a reload, we have no race */
		if ((vmu = (ivm ? ivm : ast_malloc(sizeof(*vmu))))) {
			memcpy(vmu, cur, sizeof(*vmu));
			ast_set2_flag(vmu, !ivm, VM_ALLOCED);
			AST_LIST_NEXT(vmu, list) = NULL;
		}
	} else
		vmu = find_user_realtime(ivm, context, mailbox);
	AST_LIST_UNLOCK(&users);
	return vmu;
}

static int reset_user_pw(const char *context, const char *mailbox, const char *newpass)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *cur;
	int res = -1;
	AST_LIST_LOCK(&users);
	AST_LIST_TRAVERSE(&users, cur, list) {
		if ((!context || !strcasecmp(context, cur->context)) &&
			(!strcasecmp(mailbox, cur->mailbox)))
				break;
	}
	if (cur) {
		ast_copy_string(cur->password, newpass, sizeof(cur->password));
		res = 0;
	}
	AST_LIST_UNLOCK(&users);
	return res;
}

static void vm_change_password(struct ast_vm_user *vmu, const char *newpassword)
{
	struct ast_config   *cfg=NULL;
	struct ast_variable *var=NULL;
	struct ast_category *cat=NULL;
	char *category=NULL, *value=NULL, *new=NULL;
	const char *tmp=NULL;
					
	if (!change_password_realtime(vmu, newpassword))
		return;

	/* check voicemail.conf */
	if ((cfg = ast_config_load_with_comments(VOICEMAIL_CONFIG))) {
		while ((category = ast_category_browse(cfg, category))) {
			if (!strcasecmp(category, vmu->context)) {
				tmp = ast_variable_retrieve(cfg, category, vmu->mailbox);
				if (!tmp) {
					ast_log(LOG_WARNING, "We could not find the mailbox.\n");
					break;
				}
				value = strstr(tmp,",");
				if (!value) {
					new = alloca(strlen(newpassword)+1);
					sprintf(new, "%s", newpassword);
				} else {
					new = alloca((strlen(value)+strlen(newpassword)+1));
					sprintf(new,"%s%s", newpassword, value);
				}
				if (!(cat = ast_category_get(cfg, category))) {
					ast_log(LOG_WARNING, "Failed to get category structure.\n");
					break;
				}
				ast_variable_update(cat, vmu->mailbox, new, NULL, 0);
			}
		}
		/* save the results */
		reset_user_pw(vmu->context, vmu->mailbox, newpassword);
		ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
		config_text_file_save(VOICEMAIL_CONFIG, cfg, "AppVoicemail");
	}
	category = NULL;
	var = NULL;
	/* check users.conf and update the password stored for the mailbox*/
	/* if no vmsecret entry exists create one. */
	if ((cfg = ast_config_load_with_comments("users.conf"))) {
		if (option_debug > 3)
			ast_log(LOG_DEBUG, "we are looking for %s\n", vmu->mailbox);
		while ((category = ast_category_browse(cfg, category))) {
			if (option_debug > 3)
				ast_log(LOG_DEBUG, "users.conf: %s\n", category);
			if (!strcasecmp(category, vmu->mailbox)) {
				if (!(tmp = ast_variable_retrieve(cfg, category, "vmsecret"))) {
					if (option_debug > 3)
						ast_log(LOG_DEBUG, "looks like we need to make vmsecret!\n");
					var = ast_variable_new("vmsecret", newpassword);
				} 
				new = alloca(strlen(newpassword)+1);
				sprintf(new, "%s", newpassword);
				if (!(cat = ast_category_get(cfg, category))) {
					if (option_debug > 3)
						ast_log(LOG_DEBUG, "failed to get category!\n");
					break;
				}
				if (!var)		
					ast_variable_update(cat, "vmsecret", new, NULL, 0);
				else
					ast_variable_append(cat, var);
			}
		}
		/* save the results and clean things up */
		reset_user_pw(vmu->context, vmu->mailbox, newpassword);	
		ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
		config_text_file_save("users.conf", cfg, "AppVoicemail");
	}
}

static void vm_change_password_shell(struct ast_vm_user *vmu, char *newpassword)
{
	char buf[255];
	snprintf(buf,255,"%s %s %s %s",ext_pass_cmd,vmu->context,vmu->mailbox,newpassword);
	if (!ast_safe_system(buf)) {
		ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
		/* Reset the password in memory, too */
		reset_user_pw(vmu->context, vmu->mailbox, newpassword);
	}
}

static int make_dir(char *dest, int len, const char *context, const char *ext, const char *folder)
{
	return snprintf(dest, len, "%s%s/%s/%s", VM_SPOOL_DIR, context, ext, folder);
}

static int make_file(char *dest, const int len, const char *dir, const int num)
{
	return snprintf(dest, len, "%s/msg%04d", dir, num);
}

/* same as mkstemp, but return a FILE * */
static FILE *vm_mkftemp(char *template)
{
	FILE *p = NULL;
	int pfd = mkstemp(template);
	chmod(template, VOICEMAIL_FILE_MODE & ~my_umask);
	if (pfd > -1) {
		p = fdopen(pfd, "w+");
		if (!p) {
			close(pfd);
			pfd = -1;
		}
	}
	return p;
}

/*! \brief basically mkdir -p $dest/$context/$ext/$folder
 * \param dest    String. base directory.
 * \param len     Length of dest.
 * \param context String. Ignored if is null or empty string.
 * \param ext     String. Ignored if is null or empty string.
 * \param folder  String. Ignored if is null or empty string. 
 * \return -1 on failure, 0 on success.
 */
static int create_dirpath(char *dest, int len, const char *context, const char *ext, const char *folder)
{
	mode_t	mode = VOICEMAIL_DIR_MODE;

	if (!ast_strlen_zero(context)) {
		make_dir(dest, len, context, "", "");
		if (mkdir(dest, mode) && errno != EEXIST) {
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dest, strerror(errno));
			return -1;
		}
	}
	if (!ast_strlen_zero(ext)) {
		make_dir(dest, len, context, ext, "");
		if (mkdir(dest, mode) && errno != EEXIST) {
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dest, strerror(errno));
			return -1;
		}
	}
	if (!ast_strlen_zero(folder)) {
		make_dir(dest, len, context, ext, folder);
		if (mkdir(dest, mode) && errno != EEXIST) {
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dest, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static char *mbox(int id)
{
	static char *msgs[] = {
		"INBOX",
		"Old",
		"Work",
		"Family",
		"Friends",
		"Cust1",
		"Cust2",
		"Cust3",
		"Cust4",
		"Cust5",
	};
	return (id >= 0 && id < (sizeof(msgs)/sizeof(msgs[0]))) ? msgs[id] : "tmp";
}

static void free_user(struct ast_vm_user *vmu)
{
	if (ast_test_flag(vmu, VM_ALLOCED))
		free(vmu);
}

static int vm_allocate_dh(struct vm_state *vms, struct ast_vm_user *vmu, int count_msg) {

	int arraysize = (vmu->maxmsg > count_msg ? vmu->maxmsg : count_msg);
	if (!vms->dh_arraysize) {
		/* initial allocation */
		if (!(vms->deleted = ast_calloc(arraysize, sizeof(int)))) {
			return -1;
		}
		if (!(vms->heard = ast_calloc(arraysize, sizeof(int)))) {
			return -1;
		}
		vms->dh_arraysize = arraysize;
	} else if (vms->dh_arraysize < arraysize) {
		if (!(vms->deleted = ast_realloc(vms->deleted, arraysize * sizeof(int)))) {
			return -1;
		}
		if (!(vms->heard = ast_realloc(vms->heard, arraysize * sizeof(int)))) {
			return -1;
		}
		memset(vms->deleted, 0, arraysize * sizeof(int));
		memset(vms->heard, 0, arraysize * sizeof(int));
		vms->dh_arraysize = arraysize;
	}

	return 0;
}

/* All IMAP-specific functions should go in this block. This
 * keeps them from being spread out all over the code */
#ifdef IMAP_STORAGE
static void vm_imap_delete(char *file, int msgnum, struct ast_vm_user *vmu)
{
	char arg[10];
	struct vm_state *vms;
	unsigned long messageNum;

	/* Greetings aren't stored in IMAP, so we delete them from disk */
	if (msgnum < 0) {
		ast_filedelete(file, NULL);
		return;
	}

	if (!(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 1)) && !(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 0))) {
		ast_log(LOG_WARNING, "Couldn't find a vm_state for mailbox %s. Unable to set \\DELETED flag for message %d\n", vmu->mailbox, msgnum);
		return;
	}

	/* find real message number based on msgnum */
	/* this may be an index into vms->msgArray based on the msgnum. */
	messageNum = vms->msgArray[msgnum];
	if (messageNum == 0) {
		ast_log(LOG_WARNING, "msgnum %d, mailbox message %lu is zero.\n",msgnum,messageNum);
		return;
	}
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "deleting msgnum %d, which is mailbox message %lu\n",msgnum,messageNum);
	/* delete message */
	snprintf (arg, sizeof(arg), "%lu",messageNum);
	ast_mutex_lock(&vms->lock);
	mail_setflag (vms->mailstream,arg,"\\DELETED");
	mail_expunge(vms->mailstream);
	ast_mutex_unlock(&vms->lock);
}

static int imap_retrieve_file(const char *dir, const int msgnum, const struct ast_vm_user *vmu)
{
	BODY *body;
	char *header_content;
	char *attachedfilefmt;
	const char *cid_num;
	const char *cid_name;
	const char *duration;
	const char *context;
	const char *category;
	const char *origtime;
	struct vm_state *vms;
	char text_file[PATH_MAX];
	FILE *text_file_ptr;

	/* Greetings are not stored on the IMAP server, so we should not
	 * attempt to retrieve them.
	 */
	if (msgnum < 0) {
		return 0;
	}
	
	/* Before anything can happen, we need a vm_state so that we can
	 * actually access the imap server through the vms->mailstream
	 */
	if(!(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 1)) && !(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 0))) {
		/* This should not happen. If it does, then I guess we'd
		 * need to create the vm_state, extract which mailbox to
		 * open, and then set up the msgArray so that the correct
		 * IMAP message could be accessed. If I have seen correctly
		 * though, the vms should be obtainable from the vmstates list
		 * and should have its msgArray properly set up.
		 */
		ast_log(LOG_ERROR, "Couldn't find a vm_state for mailbox %s!!! Oh no!\n", vmu->mailbox);
		return -1;
	}
	
	make_file(vms->fn, sizeof(vms->fn), dir, msgnum);

	/* Don't try to retrieve a message from IMAP if it already is on the file system */
	if (ast_fileexists(vms->fn, NULL, NULL) > 0) {
		return 0;
	}

	if (option_debug > 2)
		ast_log (LOG_DEBUG,"Before mail_fetchheaders, curmsg is: %d, imap messages is %lu\n", msgnum, vms->msgArray[msgnum]);
	if (vms->msgArray[msgnum] == 0) {
		ast_log (LOG_WARNING,"Trying to access unknown message\n");
		return -1;
	}

	/* This will only work for new messages... */
	ast_mutex_lock(&vms->lock);
	header_content = mail_fetchheader (vms->mailstream, vms->msgArray[msgnum]);
	ast_mutex_unlock(&vms->lock);
	/* empty string means no valid header */
	if (ast_strlen_zero(header_content)) {
		ast_log (LOG_ERROR,"Could not fetch header for message number %ld\n",vms->msgArray[msgnum]);
		return -1;
	}

	ast_mutex_lock(&vms->lock);
	mail_fetchstructure (vms->mailstream,vms->msgArray[msgnum],&body);
	ast_mutex_unlock(&vms->lock);

	/* We have the body, now we extract the file name of the first attachment. */
	if (body->nested.part && body->nested.part->next && body->nested.part->next->body.parameter->value) {
		attachedfilefmt = ast_strdupa(body->nested.part->next->body.parameter->value);
	} else {
		ast_log(LOG_ERROR, "There is no file attached to this IMAP message.\n");
		return -1;
	}
	
	/* Find the format of the attached file */

	strsep(&attachedfilefmt, ".");
	if (!attachedfilefmt) {
		ast_log(LOG_ERROR, "File format could not be obtained from IMAP message attachment\n");
		return -1;
	}
	
	save_body(body, vms, "2", attachedfilefmt);

	/* Get info from headers!! */
	snprintf(text_file, sizeof(text_file), "%s.%s", vms->fn, "txt");

	if (!(text_file_ptr = fopen(text_file, "w"))) {
		ast_log(LOG_WARNING, "Unable to open/create file %s: %s\n", text_file, strerror(errno));
	}

	fprintf(text_file_ptr, "%s\n", "[message]");

	cid_name = get_header_by_tag(header_content, "X-Asterisk-VM-Caller-ID-Name:");
	fprintf(text_file_ptr, "callerid=\"%s\" ", S_OR(cid_name, ""));
	cid_num = get_header_by_tag(header_content, "X-Asterisk-VM-Caller-ID-Num:");
	fprintf(text_file_ptr, "<%s>\n", S_OR(cid_num, ""));
	context = get_header_by_tag(header_content, "X-Asterisk-VM-Context:");
	fprintf(text_file_ptr, "context=%s\n", S_OR(context, ""));
	origtime = get_header_by_tag(header_content, "X-Asterisk-VM-Orig-time:");
	fprintf(text_file_ptr, "origtime=%s\n", S_OR(origtime, ""));
	duration = get_header_by_tag(header_content, "X-Asterisk-VM-Duration:");
	fprintf(text_file_ptr, "duration=%s\n", S_OR(origtime, ""));
	category = get_header_by_tag(header_content, "X-Asterisk-VM-Category:");
	fprintf(text_file_ptr, "category=%s\n", S_OR(category, ""));
	
	fclose(text_file_ptr);
	return 0;
}

static int folder_int(const char *folder)
{
	/*assume a NULL folder means INBOX*/
	if (!folder)
		return 0;
	if (!strcasecmp(folder, "INBOX"))
		return 0;
	else if (!strcasecmp(folder, "Old"))
		return 1;
	else if (!strcasecmp(folder, "Work"))
		return 2;
	else if (!strcasecmp(folder, "Family"))
		return 3;
	else if (!strcasecmp(folder, "Friends"))
		return 4;
	else if (!strcasecmp(folder, "Cust1"))
		return 5;
	else if (!strcasecmp(folder, "Cust2"))
		return 6;
	else if (!strcasecmp(folder, "Cust3"))
		return 7;
	else if (!strcasecmp(folder, "Cust4"))
		return 8;
	else if (!strcasecmp(folder, "Cust5"))
		return 9;
	else /*assume they meant INBOX if folder is not found otherwise*/
		return 0;
}

static int messagecount(const char *context, const char *mailbox, const char *folder)
{
	SEARCHPGM *pgm;
	SEARCHHEADER *hdr;

	struct ast_vm_user *vmu, vmus;
	struct vm_state *vms_p;
	int ret = 0;
	int fold = folder_int(folder);
	
	if (ast_strlen_zero(mailbox))
		return 0;

	/* We have to get the user before we can open the stream! */
	/* ast_log (LOG_DEBUG,"Before find_user, context is %s and mailbox is %s\n",context,mailbox); */
	vmu = find_user(&vmus, context, mailbox);
	if (!vmu) {
		ast_log (LOG_ERROR,"Couldn't find mailbox %s in context %s\n",mailbox,context);
		return -1;
	} else {
		/* No IMAP account available */
		if (vmu->imapuser[0] == '\0') {
			ast_log (LOG_WARNING,"IMAP user not set for mailbox %s\n",vmu->mailbox);
			return -1;
		}
	}

	/* check if someone is accessing this box right now... */
	vms_p = get_vm_state_by_imapuser(vmu->imapuser,1);
	if (!vms_p) {
		vms_p = get_vm_state_by_mailbox(mailbox, context, 1);
	}
	if (vms_p) {
		if (option_debug > 2)
			ast_log (LOG_DEBUG,"Returning before search - user is logged in\n");
		if (fold == 0) {/*INBOX*/
			return vms_p->newmessages;
		}
		if (fold == 1) {/*Old messages*/
		 	return vms_p->oldmessages;
		}
	}

	/* add one if not there... */
	vms_p = get_vm_state_by_imapuser(vmu->imapuser,0);
	if (!vms_p) {
		vms_p = get_vm_state_by_mailbox(mailbox, context, 0);
	}

	if (!vms_p) {
		if (!(vms_p = create_vm_state_from_user(vmu))) {
			ast_log(LOG_WARNING, "Unable to allocate space for new vm_state!\n");
			return -1;
		}
	}
	ret = init_mailstream(vms_p, fold);
	if (!vms_p->mailstream) {
		ast_log (LOG_ERROR,"IMAP mailstream is NULL\n");
		return -1;
	}
	if (ret == 0) {
		ast_mutex_lock(&vms_p->lock);
		pgm = mail_newsearchpgm ();
		hdr = mail_newsearchheader ("X-Asterisk-VM-Extension", (char *)(!ast_strlen_zero(vmu->imapvmshareid) ? vmu->imapvmshareid : mailbox));
		hdr->next = mail_newsearchheader("X-Asterisk-VM-Context", (char *) S_OR(context, "default"));
		pgm->header = hdr;
		if (fold != 1) {
			pgm->unseen = 1;
			pgm->seen = 0;
		}
		/* In the special case where fold is 1 (old messages) we have to do things a bit
		 * differently. Old messages are stored in the INBOX but are marked as "seen"
		 */
		else {
			pgm->unseen = 0;
			pgm->seen = 1;
		}
		pgm->undeleted = 1;
		pgm->deleted = 0;

		vms_p->vmArrayIndex = 0;
		mail_search_full (vms_p->mailstream, NULL, pgm, NIL);
		if (fold == 0)
			vms_p->newmessages = vms_p->vmArrayIndex;
		if (fold == 1)
			vms_p->oldmessages = vms_p->vmArrayIndex;
		/*Freeing the searchpgm also frees the searchhdr*/
		mail_free_searchpgm(&pgm);
		ast_mutex_unlock(&vms_p->lock);
		vms_p->updated = 0;
		return vms_p->vmArrayIndex;
	} else {
		ast_mutex_lock(&vms_p->lock);
		mail_ping(vms_p->mailstream);
		ast_mutex_unlock(&vms_p->lock);
	}
	return 0;
}

static int imap_check_limits(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu, int msgnum)
{
	/* Check if mailbox is full */
	check_quota(vms, imapfolder);
	if (vms->quota_limit && vms->quota_usage >= vms->quota_limit) {
		if (option_debug)
			ast_log(LOG_DEBUG, "*** QUOTA EXCEEDED!! %u >= %u\n", vms->quota_usage, vms->quota_limit);
		ast_play_and_wait(chan, "vm-mailboxfull");
		return -1;
	}

	/* Check if we have exceeded maxmsg */
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Checking message number quota: mailbox has %d messages, maximum is set to %d, current messages %d\n", msgnum, vmu->maxmsg, inprocess_count(vmu->mailbox, vmu->context, 0));
	if (msgnum >= vmu->maxmsg - inprocess_count(vmu->mailbox, vmu->context, +1)) {
		ast_log(LOG_WARNING, "Unable to leave message since we will exceed the maximum number of messages allowed (%u >= %u)\n", msgnum, vmu->maxmsg);
		ast_play_and_wait(chan, "vm-mailboxfull");
		inprocess_count(vmu->mailbox, vmu->context, -1);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		return -1;
	}
	return 0;
}

static int imap_store_file(char *dir, char *mailboxuser, char *mailboxcontext, int msgnum, struct ast_channel *chan, struct ast_vm_user *vmu, char *fmt, int duration, struct vm_state *vms)
{
	char *myserveremail = serveremail;
	char fn[PATH_MAX];
	char mailbox[256];
	char *stringp;
	FILE *p=NULL;
	char tmp[80] = "/tmp/astmail-XXXXXX";
	long len;
	void *buf;
	int tempcopy = 0;
	STRING str;
	int msgcount = (messagecount(vmu->context, vmu->mailbox, "INBOX") + messagecount(vmu->context, vmu->mailbox, "Old"));

	/*Greetings are not retrieved from IMAP, so there is no reason to attempt storing them there either*/
	if (msgnum < 0)
		return 0;

	if (imap_check_limits(chan, vms, vmu, msgcount)) {
		return -1;
	}

	/* Attach only the first format */
	fmt = ast_strdupa(fmt);
	stringp = fmt;
	strsep(&stringp, "|");

	if (!ast_strlen_zero(vmu->serveremail))
		myserveremail = vmu->serveremail;

	make_file(fn, sizeof(fn), dir, msgnum);

	if (ast_strlen_zero(vmu->email)) {
		/*we need the vmu->email to be set when we call make_email_file, but if we keep it set,
		 * a duplicate e-mail will be created. So at the end of this function, we will revert back to an empty
		 * string if tempcopy is 1
		 */
		ast_copy_string(vmu->email, vmu->imapuser, sizeof(vmu->email));
		tempcopy = 1;
	}

	if (!strcmp(fmt, "wav49"))
		fmt = "WAV";
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Storing file '%s', format '%s'\n", fn, fmt);
	/* Make a temporary file instead of piping directly to sendmail, in case the mail
	   command hangs */
	if ((p = vm_mkftemp(tmp)) == NULL) {
		ast_log(LOG_WARNING, "Unable to store '%s' (can't create temporary file)\n", fn);
		if (tempcopy)
			*(vmu->email) = '\0';
		return -1;
	} else {
		make_email_file(p, myserveremail, vmu, msgnum, vmu->context, vmu->mailbox, vms->curbox, S_OR(chan->cid.cid_num, NULL), S_OR(chan->cid.cid_name, NULL), fn, fmt, duration, 1, chan, NULL, 1);
		/* read mail file to memory */
		len = ftell(p);
		rewind(p);
		if ((buf = ast_malloc(len+1)) == NIL) {
			ast_log(LOG_ERROR, "Can't allocate %ld bytes to read message\n", len+1);
			fclose(p);
			return -1;
		}
		if (fread(buf, len, 1, p) != 1) {
			ast_log(LOG_WARNING, "Short read: %s\n", strerror(errno));
		}
		((char *)buf)[len] = '\0';
		INIT(&str, mail_string, buf, len);
		init_mailstream(vms, 0);
		imap_mailbox_name(mailbox, sizeof(mailbox), vms, 0, 1);
		ast_mutex_lock(&vms->lock);
		if (!mail_append(vms->mailstream, mailbox, &str))
			ast_log(LOG_ERROR, "Error while sending the message to %s\n", mailbox);
		ast_mutex_unlock(&vms->lock);
		fclose(p);
		unlink(tmp);
		ast_free(buf);
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "%s stored\n", fn);
		/* Using messagecount to populate the last place in the msgArray
		 * is less than optimal, but it's the only way given the current setup
		 */
		messagecount(vmu->context, vmu->mailbox, "INBOX");
	}
	if (tempcopy)
		*(vmu->email) = '\0';
	inprocess_count(vmu->mailbox, vmu->context, -1);
	return 0;

}

static int inboxcount(const char *mailbox_context, int *newmsgs, int *oldmsgs)
{
	char tmp[PATH_MAX] = "";
	char *mailboxnc; 	
	char *context;
	char *mb;
	char *cur;
	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;

	if (option_debug > 2)
	 	ast_log (LOG_DEBUG,"Mailbox is set to %s\n",mailbox_context);
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox_context))
		return 0;
	
	ast_copy_string(tmp, mailbox_context, sizeof(tmp));
	context = strchr(tmp, '@');
	if (strchr(mailbox_context, ',')) {
		int tmpnew, tmpold;
		ast_copy_string(tmp, mailbox_context, sizeof(tmp));
		mb = tmp;
		while ((cur = strsep(&mb, ", "))) {
			if (!ast_strlen_zero(cur)) {
				if (inboxcount(cur, newmsgs ? &tmpnew : NULL, oldmsgs ? &tmpold : NULL))
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
	if (context) {
		*context = '\0';
		mailboxnc = tmp;
		context++;
	} else {
		context = "default";
		mailboxnc = (char *)mailbox_context;
	}
	if (newmsgs) {
		if ((*newmsgs = messagecount(context, mailboxnc, "INBOX")) < 0)
			return -1;
	}
	if (oldmsgs) {
		if ((*oldmsgs = messagecount(context, mailboxnc, "Old")) < 0)
			return -1;
	}
	return 0;
}
	

static int has_voicemail(const char *mailbox, const char *folder)
{
	char tmp[256], *tmp2, *mbox, *context;
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	tmp2 = tmp;
	if (strchr(tmp2, ',')) {
		while ((mbox = strsep(&tmp2, ","))) {
			if (!ast_strlen_zero(mbox)) {
				if (has_voicemail(mbox, folder))
					return 1;
			}
		}
	}
	if ((context= strchr(tmp, '@')))
		*context++ = '\0';
	else
		context = "default";
	return messagecount(context, tmp, folder) ? 1 : 0;
}

static int copy_message(struct ast_channel *chan, struct ast_vm_user *vmu, int imbox, int msgnum, long duration, struct ast_vm_user *recip, char *fmt, char *dir)
{
	struct vm_state *sendvms = NULL, *destvms = NULL;
	char messagestring[10]; /*I guess this could be a problem if someone has more than 999999999 messages...*/
	if (msgnum >= recip->maxmsg) {
		ast_log(LOG_WARNING, "Unable to copy mail, mailbox %s is full\n", recip->mailbox);
		return -1;
	}
	if (!(sendvms = get_vm_state_by_imapuser(vmu->imapuser, 0))) {
		ast_log(LOG_ERROR, "Couldn't get vm_state for originator's mailbox!!\n");
		return -1;
	}
	if (!(destvms = get_vm_state_by_imapuser(recip->imapuser, 0))) {
		ast_log(LOG_ERROR, "Couldn't get vm_state for destination mailbox!\n");
		return -1;
	}
	snprintf(messagestring, sizeof(messagestring), "%ld", sendvms->msgArray[msgnum]);
	ast_mutex_lock(&sendvms->lock);
	if ((mail_copy(sendvms->mailstream, messagestring, (char *) mbox(imbox)) == T)) {
		ast_mutex_unlock(&sendvms->lock);
		return 0;
	}
	ast_mutex_unlock(&sendvms->lock);
	ast_log(LOG_WARNING, "Unable to copy message from mailbox %s to mailbox %s\n", vmu->mailbox, recip->mailbox);
	return -1;
}

static void imap_mailbox_name(char *spec, size_t len, struct vm_state *vms, int box, int use_folder)
{
	char tmp[256], *t = tmp;
	size_t left = sizeof(tmp);

	if (box == 1) {
		ast_copy_string(vms->curbox, mbox(0), sizeof(vms->curbox));
		snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", mbox(1));
	} else {
		ast_copy_string(vms->curbox, mbox(box), sizeof(vms->curbox));
		snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", vms->curbox);
	}

	/* Build up server information */
	ast_build_string(&t, &left, "{%s:%s/imap", imapserver, imapport);

	/* Add authentication user if present */
	if (!ast_strlen_zero(authuser))
		ast_build_string(&t, &left, "/authuser=%s", authuser);

	/* Add flags if present */
	if (!ast_strlen_zero(imapflags))
		ast_build_string(&t, &left, "/%s", imapflags);

	/* End with username */
#if 1
	ast_build_string(&t, &left, "/user=%s}", vms->imapuser);
#else
	ast_build_string(&t, &left, "/user=%s/novalidate-cert}", vms->imapuser);
#endif

	if (box == 0 || box == 1)
		snprintf(spec, len, "%s%s", tmp, use_folder? imapfolder: "INBOX");
	else
		snprintf(spec, len, "%s%s%c%s", tmp, imapfolder, delimiter, mbox(box));
}

static int init_mailstream(struct vm_state *vms, int box)
{
	MAILSTREAM *stream = NIL;
	long debug;
	char tmp[256];
	
	if (!vms) {
		ast_log (LOG_ERROR,"vm_state is NULL!\n");
		return -1;
	}
	if (option_debug > 2)
		ast_log (LOG_DEBUG,"vm_state user is:%s\n",vms->imapuser);
	if (vms->mailstream == NIL || !vms->mailstream) {
		if (option_debug)
			ast_log (LOG_DEBUG,"mailstream not set.\n");
	} else {
		stream = vms->mailstream;
	}
	/* debug = T;  user wants protocol telemetry? */
	debug = NIL;  /* NO protocol telemetry? */

	if (delimiter == '\0') {		/* did not probe the server yet */
		char *cp;
#ifdef USE_SYSTEM_IMAP
#include <imap/linkage.c>
#elif defined(USE_SYSTEM_CCLIENT)
#include <c-client/linkage.c>
#else
#include "linkage.c"
#endif
		/* Connect to INBOX first to get folders delimiter */
		imap_mailbox_name(tmp, sizeof(tmp), vms, 0, 1);
		ast_mutex_lock(&vms->lock);
		stream = mail_open (stream, tmp, debug ? OP_DEBUG : NIL);
		ast_mutex_unlock(&vms->lock);
		if (stream == NIL) {
			ast_log (LOG_ERROR, "Can't connect to imap server %s\n", tmp);
			return -1;
		}
		get_mailbox_delimiter(stream);
		/* update delimiter in imapfolder */
		for (cp = imapfolder; *cp; cp++)
			if (*cp == '/')
				*cp = delimiter;
	}
	/* Now connect to the target folder */
	imap_mailbox_name(tmp, sizeof(tmp), vms, box, 1);
	if (option_debug > 2)
		ast_log (LOG_DEBUG,"Before mail_open, server: %s, box:%d\n", tmp, box);
	ast_mutex_lock(&vms->lock);
	vms->mailstream = mail_open (stream, tmp, debug ? OP_DEBUG : NIL);
	ast_mutex_unlock(&vms->lock);
	if (vms->mailstream == NIL) {
		return -1;
	} else {
		return 0;
	}
}

static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu, int box)
{
	SEARCHPGM *pgm;
	SEARCHHEADER *hdr;
	int ret;

	ast_copy_string(vms->imapuser,vmu->imapuser, sizeof(vms->imapuser));
	vms->imapversion = vmu->imapversion;

	if (option_debug > 2)
		ast_log(LOG_DEBUG,"Before init_mailstream, user is %s\n",vmu->imapuser);
	ret = init_mailstream(vms, box);
	if (ret != 0 || !vms->mailstream) {
		ast_log (LOG_ERROR,"Could not initialize mailstream\n");
		return -1;
	}
	
	create_dirpath(vms->curdir, sizeof(vms->curdir), vmu->context, vms->username, vms->curbox);

	/* Check Quota */
	if  (box == 0)  {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Mailbox name set to: %s, about to check quotas\n", mbox(box));
		check_quota(vms,(char *)mbox(box));
	}

	ast_mutex_lock(&vms->lock);
	pgm = mail_newsearchpgm();

	/* Check IMAP folder for Asterisk messages only... */
	hdr = mail_newsearchheader ("X-Asterisk-VM-Extension", (!ast_strlen_zero(vmu->imapvmshareid) ? vmu->imapvmshareid : vmu->mailbox));
	hdr->next = mail_newsearchheader("X-Asterisk-VM-Context", vmu->context);
	pgm->header = hdr;
	pgm->deleted = 0;
	pgm->undeleted = 1;

	/* if box = 0, check for new, if box = 1, check for read */
	if (box == 0) {
		pgm->unseen = 1;
		pgm->seen = 0;
	} else if (box == 1) {
		pgm->seen = 1;
		pgm->unseen = 0;
	}

	vms->vmArrayIndex = 0;
	if (option_debug > 2)
		ast_log(LOG_DEBUG,"Before mail_search_full, user is %s\n",vmu->imapuser);
	mail_search_full (vms->mailstream, NULL, pgm, NIL);

	vms->lastmsg = vms->vmArrayIndex - 1;
	/* Since IMAP storage actually stores both old and new messages in the same IMAP folder,
	 * ensure to allocate enough space to account for all of them. Warn if old messages
	 * have not been checked first as that is required.
	 */
	if (box == 0 && !vms->dh_arraysize) {
		ast_log(LOG_WARNING, "The code expects the old messages to be checked first, fix the code.\n");
	}
	if (vm_allocate_dh(vms, vmu, box == 0 ? vms->vmArrayIndex + vms->oldmessages : vms->lastmsg)) {
		ast_mutex_unlock(&vms->lock);
		return -1;
	}

	mail_free_searchpgm(&pgm);
	ast_mutex_unlock(&vms->lock);
	return 0;
}

static void write_file(char *filename, char *buffer, unsigned long len)
{
	FILE *output;

	output = fopen (filename, "w");
	if (fwrite (buffer, len, 1, output) != 1) {
		ast_log(LOG_WARNING, "Short write: %s\n", strerror(errno));
	}
	fclose (output);
}

void mm_searched(MAILSTREAM *stream, unsigned long number)
{
	struct vm_state *vms;
	char *mailbox;
	char *user;
	mailbox = stream->mailbox;
	user = get_user_by_mailbox(mailbox);
	vms = get_vm_state_by_imapuser(user,2);
	if (!vms) {
		vms = get_vm_state_by_imapuser(user, 0);
	}
	if (vms) {
		if (option_debug > 2)
			ast_log (LOG_DEBUG, "saving mailbox message number %lu as message %d. Interactive set to %d\n",number,vms->vmArrayIndex,vms->interactive);
		vms->msgArray[vms->vmArrayIndex++] = number;
	} else {
		ast_log (LOG_ERROR, "No state found.\n");
	}
}

static struct ast_vm_user *find_user_realtime_imapuser(const char *imapuser)
{
	struct ast_variable *var;
	struct ast_vm_user *vmu;

	vmu = ast_calloc(1, sizeof *vmu);
	if (!vmu)
		return NULL;
	ast_set_flag(vmu, VM_ALLOCED);
	populate_defaults(vmu);

	var = ast_load_realtime("voicemail", "imapuser", imapuser, NULL);
	if (var) {
		apply_options_full(vmu, var);
		ast_variables_destroy(var);
		return vmu;
	} else {
		free(vmu);
		return NULL;
	}
}

/* Interfaces to C-client */

void mm_exists(MAILSTREAM * stream, unsigned long number)
{
	/* mail_ping will callback here if new mail! */
	if (option_debug > 3)
		ast_log (LOG_DEBUG, "Entering EXISTS callback for message %ld\n", number);
	if (number == 0) return;
	set_update(stream);
}


void mm_expunged(MAILSTREAM * stream, unsigned long number)
{
	/* mail_ping will callback here if expunged mail! */
	if (option_debug > 3)
		ast_log (LOG_DEBUG, "Entering EXPUNGE callback for message %ld\n", number);
	if (number == 0) return;
	set_update(stream);
}


void mm_flags(MAILSTREAM * stream, unsigned long number)
{
	/* mail_ping will callback here if read mail! */
	if (option_debug > 3)
		ast_log (LOG_DEBUG, "Entering FLAGS callback for message %ld\n", number);
	if (number == 0) return;
	set_update(stream);
}


void mm_notify(MAILSTREAM * stream, char *string, long errflg)
{
	mm_log (string, errflg);
}


void mm_list(MAILSTREAM * stream, int delim, char *mailbox, long attributes)
{
	if (delimiter == '\0') {
		delimiter = delim;
	}
	if (option_debug > 4) {
		ast_log(LOG_DEBUG, "Delimiter set to %c and mailbox %s\n",delim, mailbox);
		if (attributes & LATT_NOINFERIORS)
			ast_log(LOG_DEBUG, "no inferiors\n");
		if (attributes & LATT_NOSELECT)
			ast_log(LOG_DEBUG, "no select\n");
		if (attributes & LATT_MARKED)
			ast_log(LOG_DEBUG, "marked\n");
		if (attributes & LATT_UNMARKED)
			ast_log(LOG_DEBUG, "unmarked\n");
	}
}


void mm_lsub(MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
	if (option_debug > 4) {
		ast_log(LOG_DEBUG, "Delimiter set to %c and mailbox %s\n",delimiter, mailbox);
		if (attributes & LATT_NOINFERIORS)
			ast_log(LOG_DEBUG, "no inferiors\n");
		if (attributes & LATT_NOSELECT)
			ast_log(LOG_DEBUG, "no select\n");
		if (attributes & LATT_MARKED)
			ast_log(LOG_DEBUG, "marked\n");
		if (attributes & LATT_UNMARKED)
			ast_log(LOG_DEBUG, "unmarked\n");
	}
}


void mm_status(MAILSTREAM * stream, char *mailbox, MAILSTATUS * status)
{
	ast_log (LOG_NOTICE," Mailbox %s", mailbox);
	if (status->flags & SA_MESSAGES)
		ast_log (LOG_NOTICE,", %lu messages", status->messages);
	if (status->flags & SA_RECENT)
		ast_log (LOG_NOTICE,", %lu recent", status->recent);
	if (status->flags & SA_UNSEEN)
		ast_log (LOG_NOTICE,", %lu unseen", status->unseen);
	if (status->flags & SA_UIDVALIDITY)
		ast_log (LOG_NOTICE,", %lu UID validity", status->uidvalidity);
	if (status->flags & SA_UIDNEXT)
		ast_log (LOG_NOTICE,", %lu next UID", status->uidnext);
	ast_log (LOG_NOTICE,"\n");
}


void mm_log(char *string, long errflg)
{
	switch ((short) errflg) {
		case NIL:
			if (option_debug)
				ast_log(LOG_DEBUG,"IMAP Info: %s\n", string);
			break;
		case PARSE:
		case WARN:
			ast_log (LOG_WARNING,"IMAP Warning: %s\n", string);
			break;
		case ERROR:
			ast_log (LOG_ERROR,"IMAP Error: %s\n", string);
			break;
	}
}


void mm_dlog(char *string)
{
	ast_log (LOG_NOTICE, "%s\n", string);
}


void mm_login(NETMBX * mb, char *user, char *pwd, long trial)
{
	struct ast_vm_user *vmu;

	if (option_debug > 3)
		ast_log(LOG_DEBUG, "Entering callback mm_login\n");

	ast_copy_string(user, mb->user, MAILTMPLEN);

	/* We should only do this when necessary */
	if (!ast_strlen_zero(authpassword)) {
		ast_copy_string(pwd, authpassword, MAILTMPLEN);
	} else {
		AST_LIST_TRAVERSE(&users, vmu, list) {
			if (!strcasecmp(mb->user, vmu->imapuser)) {
				ast_copy_string(pwd, vmu->imappassword, MAILTMPLEN);
				break;
			}
		}
		if (!vmu) {
			if ((vmu = find_user_realtime_imapuser(mb->user))) {
				ast_copy_string(pwd, vmu->imappassword, MAILTMPLEN);
				free_user(vmu);
			}
		}
	}
}


void mm_critical(MAILSTREAM * stream)
{
}


void mm_nocritical(MAILSTREAM * stream)
{
}


long mm_diskerror(MAILSTREAM * stream, long errcode, long serious)
{
	kill (getpid (), SIGSTOP);
	return NIL;
}


void mm_fatal(char *string)
{
	ast_log(LOG_ERROR,"IMAP access FATAL error: %s\n", string);
}

/* C-client callback to handle quota */
static void mm_parsequota(MAILSTREAM *stream, unsigned char *msg, QUOTALIST *pquota)
{
	struct vm_state *vms;
	char *mailbox;
	char *user;
	unsigned long usage = 0;
	unsigned long limit = 0;
	
	while (pquota) {
		usage = pquota->usage;
		limit = pquota->limit;
		pquota = pquota->next;
	}
	
	mailbox = stream->mailbox;
	user = get_user_by_mailbox(mailbox);
	vms = get_vm_state_by_imapuser(user,2);
	if (!vms) {
		vms = get_vm_state_by_imapuser(user, 0);
	}
	if (vms) {
		if (option_debug > 2)
			ast_log (LOG_DEBUG, "User %s usage is %lu, limit is %lu\n",user,usage,limit);
		vms->quota_usage = usage;
		vms->quota_limit = limit;
	} else {
		ast_log (LOG_ERROR, "No state found.\n");
	}
}

static char *get_header_by_tag(char *header, char *tag)
{
	char *start;
	int taglen;
	char *eol_pnt;

	if (!header || !tag)
		return NULL;

	taglen = strlen(tag) + 1;
	if (taglen < 1)
		return NULL;

	start = strstr(header, tag);
	if (!start)
		return NULL;

	ast_mutex_lock(&imaptemp_lock);
	ast_copy_string(imaptemp, start+taglen, sizeof(imaptemp));
	ast_mutex_unlock(&imaptemp_lock);
	if ((eol_pnt = strchr(imaptemp,'\r')) || (eol_pnt = strchr(imaptemp,'\n')))
		*eol_pnt = '\0';
	return imaptemp;
}

static char *get_user_by_mailbox(char *mailbox)
{
	char *start, *quote;
	char *eol_pnt;

	if (!mailbox)
		return NULL;

	start = strstr(mailbox,"/user=");
	if (!start)
		return NULL;

	ast_mutex_lock(&imaptemp_lock);
	ast_copy_string(imaptemp, start+6, sizeof(imaptemp));
	ast_mutex_unlock(&imaptemp_lock);

	quote = strchr(imaptemp,'\"');
	if (!quote) {  /* if username is not in quotes */
		eol_pnt = strchr(imaptemp,'/');
		if (!eol_pnt) {
			eol_pnt = strchr(imaptemp,'}');
		}
		*eol_pnt = '\0';
		return imaptemp;
	} else {
		eol_pnt = strchr(imaptemp+1,'\"');
		*eol_pnt = '\0';
		return imaptemp+1;
	}
}

static struct vm_state *create_vm_state_from_user(struct ast_vm_user *vmu)
{
	struct vm_state *vms_p;

	pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
	if ((vms_p = pthread_getspecific(ts_vmstate.key)) && !strcmp(vms_p->imapuser, vmu->imapuser) && !strcmp(vms_p->username, vmu->mailbox)) {
		return vms_p;
	}
	if (option_debug > 4)
		ast_log(LOG_DEBUG,"Adding new vmstate for %s\n",vmu->imapuser);
	if (!(vms_p = ast_calloc(1, sizeof(*vms_p))))
		return NULL;
	ast_copy_string(vms_p->imapuser,vmu->imapuser, sizeof(vms_p->imapuser));
	ast_copy_string(vms_p->username, vmu->mailbox, sizeof(vms_p->username)); /* save for access from interactive entry point */
	ast_copy_string(vms_p->context, vmu->context, sizeof(vms_p->context));
	vms_p->mailstream = NIL; /* save for access from interactive entry point */
	vms_p->imapversion = vmu->imapversion;
	if (option_debug > 4)
		ast_log(LOG_DEBUG,"Copied %s to %s\n",vmu->imapuser,vms_p->imapuser);
	vms_p->updated = 1;
	/* set mailbox to INBOX! */
	ast_copy_string(vms_p->curbox, mbox(0), sizeof(vms_p->curbox));
	init_vm_state(vms_p);
	vmstate_insert(vms_p);
	return vms_p;
}

static struct vm_state *get_vm_state_by_imapuser(char *user, int interactive)
{
	struct vmstate *vlist = NULL;

	if (interactive) {
		struct vm_state *vms;
		pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
		vms = pthread_getspecific(ts_vmstate.key);
		return vms;
	}

	ast_mutex_lock(&vmstate_lock);
	vlist = vmstates;
	while (vlist) {
		if (vlist->vms && vlist->vms->imapversion == imapversion) {
			if (vlist->vms->imapuser) {
				if (!strcmp(vlist->vms->imapuser,user)) {
					if (interactive == 2) {
						ast_mutex_unlock(&vmstate_lock);
						return vlist->vms;
					} else if (vlist->vms->interactive == interactive) {
						ast_mutex_unlock(&vmstate_lock);
						return vlist->vms;
					}
				}
			} else {
				if (option_debug > 2)
					ast_log(LOG_DEBUG, "	error: imapuser is NULL for %s\n",user);
			}
		} else {
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "	error: vms is NULL for %s\n",user);
		}
		vlist = vlist->next;
	}
	ast_mutex_unlock(&vmstate_lock);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "%s not found in vmstates\n",user);
	return NULL;
}

static struct vm_state *get_vm_state_by_mailbox(const char *mailbox, const char *context, int interactive)
{ 
	struct vmstate *vlist = NULL;
	const char *local_context = S_OR(context, "default");

	if (interactive) {
		struct vm_state *vms;
		pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
		vms = pthread_getspecific(ts_vmstate.key);
		return vms;
	}

	ast_mutex_lock(&vmstate_lock);
	vlist = vmstates;
	if (option_debug > 2) 
		ast_log(LOG_DEBUG, "Mailbox set to %s\n",mailbox);
	while (vlist) {
		if (vlist->vms) {
			if (vlist->vms->username && vlist->vms->context && vlist->vms->imapversion == imapversion) {
				if (option_debug > 2)
					ast_log(LOG_DEBUG, "	comparing mailbox %s (i=%d) to vmstate mailbox %s (i=%d)\n",mailbox,interactive,vlist->vms->username,vlist->vms->interactive);
				if (!strcmp(vlist->vms->username,mailbox) && !(strcmp(vlist->vms->context, local_context))) {
					if (option_debug > 2)
						ast_log(LOG_DEBUG, "	Found it!\n");
					ast_mutex_unlock(&vmstate_lock);
					return vlist->vms;
				}
			} else {
				if (option_debug > 2)
					ast_log(LOG_DEBUG, "	error: username or context is NULL for %s\n",mailbox);
			}
		} else {
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "	error: vms is NULL for %s\n",mailbox);
		}
		vlist = vlist->next;
	}
	ast_mutex_unlock(&vmstate_lock);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "%s not found in vmstates\n",mailbox);
	return NULL;
}

static void vmstate_insert(struct vm_state *vms) 
{
	struct vmstate *v;
	struct vm_state *altvms;

	/* If interactive, it probably already exists, and we should
	   use the one we already have since it is more up to date.
	   We can compare the username to find the duplicate */
	if (vms->interactive == 1) {
		altvms = get_vm_state_by_mailbox(vms->username, vms->context, 0);
		if (altvms) {
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "Duplicate mailbox %s, copying message info...\n",vms->username);
			vms->newmessages = altvms->newmessages;
			vms->oldmessages = altvms->oldmessages;
			/* memcpy(vms->msgArray, altvms->msgArray, sizeof(long)*256); */
			copy_msgArray(vms, altvms);
			vms->vmArrayIndex = altvms->vmArrayIndex;
			vms->lastmsg = altvms->lastmsg;
			vms->curmsg = altvms->curmsg;
			/* get a pointer to the persistent store */
			vms->persist_vms = altvms;
			/* Reuse the mailstream? */
#ifdef REALLY_FAST_EVEN_IF_IT_MEANS_RESOURCE_LEAKS
			vms->mailstream = altvms->mailstream;
#else
			vms->mailstream = NIL;
#endif
		}
		return;
	}

	v = (struct vmstate *)malloc(sizeof(struct vmstate));
	if (!v) {
		ast_log(LOG_ERROR, "Out of memory\n");
	}
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Inserting vm_state for user:%s, mailbox %s\n",vms->imapuser,vms->username);
	ast_mutex_lock(&vmstate_lock);
	v->vms = vms;
	v->next = vmstates;
	vmstates = v;
	ast_mutex_unlock(&vmstate_lock);
}

static void vmstate_delete(struct vm_state *vms) 
{
	struct vmstate *vc, *vf = NULL, *vl = NULL;
	struct vm_state *altvms;

	/* If interactive, we should copy pertainent info
	   back to the persistent state (to make update immediate) */
	if (vms->interactive == 1) {
		altvms = vms->persist_vms;
		if (altvms) {
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "Duplicate mailbox %s, copying message info...\n",vms->username);
			altvms->newmessages = vms->newmessages;
			altvms->oldmessages = vms->oldmessages;
			altvms->updated = 1;
		}
		vms->mailstream = mail_close(vms->mailstream);

		/* Interactive states are not stored within the persistent list */
		return;
	}

	ast_mutex_lock(&vmstate_lock);
	vc = vmstates;
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Removing vm_state for user:%s, mailbox %s\n",vms->imapuser,vms->username);
	while (vc) {
		if (vc->vms == vms) {
			vf = vc;
			if (vl)
				vl->next = vc->next;
			else
				vmstates = vc->next;
			break;
		}
		vl = vc;
		vc = vc->next;
	}
	if (!vf) {
		ast_log(LOG_ERROR, "No vmstate found for user:%s, mailbox %s\n",vms->imapuser,vms->username);
	} else {
		ast_mutex_destroy(&vf->vms->lock);
		free(vf);
	}
	ast_mutex_unlock(&vmstate_lock);
}

static void set_update(MAILSTREAM * stream) 
{
	struct vm_state *vms;
	char *mailbox;
	char *user;

	mailbox = stream->mailbox;
	user = get_user_by_mailbox(mailbox);
	vms = get_vm_state_by_imapuser(user, 0);
	if (vms) {
		if (option_debug > 2)
			ast_log (LOG_DEBUG, "User %s mailbox set for update.\n",user);
		vms->updated = 1; /* set updated flag since mailbox changed */
	} else {
		if (option_debug > 2)
			ast_log (LOG_WARNING, "User %s mailbox not found for update.\n",user);
	}
}

static void init_vm_state(struct vm_state *vms) 
{
	int x;
	vms->vmArrayIndex = 0;
	for (x = 0; x < 256; x++) {
		vms->msgArray[x] = 0;
	}
	ast_mutex_init(&vms->lock);
}

static void copy_msgArray(struct vm_state *dst, struct vm_state *src)
{
	int x;
	for (x = 0; x<256; x++) {
		dst->msgArray[x] = src->msgArray[x];
	}
}

static int save_body(BODY *body, struct vm_state *vms, char *section, char *format) 
{
	char *body_content;
	char *body_decoded;
	unsigned long len;
	unsigned long newlen;
	char filename[256];
	
	if (!body || body == NIL)
		return -1;
	ast_mutex_lock(&vms->lock);
	body_content = mail_fetchbody (vms->mailstream, vms->msgArray[vms->curmsg], section, &len);
	ast_mutex_unlock(&vms->lock);
	if (body_content != NIL) {
		snprintf(filename, sizeof(filename), "%s.%s", vms->fn, format);
		/* ast_log (LOG_DEBUG,body_content); */
		body_decoded = rfc822_base64 ((unsigned char *)body_content, len, &newlen);
		write_file (filename, (char *) body_decoded, newlen);
	}
	return 0;
}

/* get delimiter via mm_list callback */
/* MUTEX should already be held */
static void get_mailbox_delimiter(MAILSTREAM *stream) {
	char tmp[50];
	snprintf(tmp, sizeof(tmp), "{%s}", imapserver);
	mail_list(stream, tmp, "*");
}

/* Check Quota for user */
static void check_quota(struct vm_state *vms, char *mailbox) {
	ast_mutex_lock(&vms->lock);
	mail_parameters(NULL, SET_QUOTA, (void *) mm_parsequota);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Mailbox name set to: %s, about to check quotas\n", mailbox);
	if (vms && vms->mailstream != NULL) {
		imap_getquotaroot(vms->mailstream, mailbox);
	} else {
		ast_log(LOG_WARNING,"Mailstream not available for mailbox: %s\n",mailbox);
	}
	ast_mutex_unlock(&vms->lock);
}
#endif

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


#ifdef ODBC_STORAGE
struct generic_prepare_struct {
	char *sql;
	int argc;
	char **argv;
};

static SQLHSTMT generic_prepare(struct odbc_obj *obj, void *data)
{
	struct generic_prepare_struct *gps = data;
	int res, i;
	SQLHSTMT stmt;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}
	res = SQLPrepare(stmt, (unsigned char *)gps->sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", gps->sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	for (i = 0; i < gps->argc; i++)
		SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(gps->argv[i]), 0, gps->argv[i], 0, NULL);

	return stmt;
}

static int retrieve_file(char *dir, int msgnum)
{
	int x = 0;
	int res;
	int fd=-1;
	size_t fdlen = 0;
	void *fdm = MAP_FAILED;
	SQLSMALLINT colcount=0;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char fmt[80]="";
	char *c;
	char coltitle[256];
	SQLSMALLINT collen;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	SQLULEN colsize;
	SQLLEN colsize2;
	FILE *f=NULL;
	char rowdata[80];
	char fn[PATH_MAX];
	char full_fn[PATH_MAX];
	char msgnums[80];
	char *argv[] = { dir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 2, .argv = argv };

	struct odbc_obj *obj;
	obj = ast_odbc_request_obj(odbc_database, 0);
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
		
		if (!(f = fopen(full_fn, "w+"))) {
		        ast_log(LOG_WARNING, "Failed to open/create '%s'\n", full_fn);
		        goto yuck;
		}
		
		snprintf(full_fn, sizeof(full_fn), "%s.%s", fn, fmt);
		snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE dir=? AND msgnum=?",odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if (res == SQL_NO_DATA) {
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		else if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		fd = open(full_fn, O_RDWR | O_CREAT | O_TRUNC, 0770);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Failed to write '%s': %s\n", full_fn, strerror(errno));
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLNumResultCols(stmt, &colcount);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {	
			ast_log(LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		if (f) 
			fprintf(f, "[message]\n");
		for (x=0;x<colcount;x++) {
			rowdata[0] = '\0';
			colsize = 0;
			collen = sizeof(coltitle);
			res = SQLDescribeCol(stmt, x + 1, (unsigned char *)coltitle, sizeof(coltitle), &collen, 
						&datatype, &colsize, &decimaldigits, &nullable);
			if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
				ast_log(LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
				SQLFreeHandle (SQL_HANDLE_STMT, stmt);
				ast_odbc_release_obj(obj);
				goto yuck;
			}
			if (!strcasecmp(coltitle, "recording")) {
				off_t offset;
				res = SQLGetData(stmt, x + 1, SQL_BINARY, rowdata, 0, &colsize2);
				fdlen = colsize2;
				if (fd > -1) {
					char tmp[1]="";
					lseek(fd, fdlen - 1, SEEK_SET);
					if (write(fd, tmp, 1) != 1) {
						close(fd);
						fd = -1;
						continue;
					}
					/* Read out in small chunks */
					for (offset = 0; offset < colsize2; offset += CHUNKSIZE) {
						if ((fdm = mmap(NULL, CHUNKSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset)) == MAP_FAILED) {
							ast_log(LOG_WARNING, "Could not mmap the output file: %s (%d)\n", strerror(errno), errno);
							SQLFreeHandle(SQL_HANDLE_STMT, stmt);
							ast_odbc_release_obj(obj);
							goto yuck;
						} else {
							res = SQLGetData(stmt, x + 1, SQL_BINARY, fdm, CHUNKSIZE, NULL);
							munmap(fdm, CHUNKSIZE);
							if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
								ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
								unlink(full_fn);
								SQLFreeHandle(SQL_HANDLE_STMT, stmt);
								ast_odbc_release_obj(obj);
								goto yuck;
							}
						}
					}
					if (truncate(full_fn, fdlen) < 0) {
						ast_log(LOG_WARNING, "Unable to truncate '%s': %s\n", full_fn, strerror(errno));
					}
				}
			} else {
				SQLLEN ind;
				res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), &ind);
				if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
					SQLINTEGER nativeerror = 0;
					SQLSMALLINT diagbytes = 0;
					unsigned char state[10], diagnostic[256];
					SQLGetDiagRec(SQL_HANDLE_STMT, stmt, 1, state, &nativeerror, diagnostic, sizeof(diagnostic), &diagbytes);
					ast_log(LOG_WARNING, "SQL Get Data error: %s: %s!\n[%s]\n\n", state, diagnostic, sql);
					SQLFreeHandle (SQL_HANDLE_STMT, stmt);
					ast_odbc_release_obj(obj);
					goto yuck;
				}
				if (strcasecmp(coltitle, "msgnum") && strcasecmp(coltitle, "dir") && f)
					fprintf(f, "%s=%s\n", coltitle, rowdata);
			}
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	if (f)
		fclose(f);
	if (fd > -1)
		close(fd);
	return x - 1;
}

static int last_message_index(struct ast_vm_user *vmu, char *dir)
{
	int x = 0;
	int res;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char rowdata[20];
	char *argv[] = { dir };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 1, .argv = argv };

	struct odbc_obj *obj;
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(sql, sizeof(sql), "SELECT msgnum FROM %s WHERE dir=? order by msgnum desc limit 1", odbc_table);

		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			if (res == SQL_NO_DATA) {
				ast_log(LOG_DEBUG, "Directory '%s' has no messages and therefore no index was retrieved.\n", dir);
			} else {
				ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			}

			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		if (sscanf(rowdata, "%30d", &x) != 1)
			ast_log(LOG_WARNING, "Failed to read message index!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return x;
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
	char sql[PATH_MAX];
	char rowdata[20];
	char msgnums[20];
	char *argv[] = { dir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 2, .argv = argv };

	struct odbc_obj *obj;
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir=? AND msgnum=?",odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		if (sscanf(rowdata, "%30d", &x) != 1)
			ast_log(LOG_WARNING, "Failed to read message count!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	return x;
}

static int count_messages(struct ast_vm_user *vmu, char *dir)
{
	int x = 0;
	int res;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char rowdata[20];
	char *argv[] = { dir };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 1, .argv = argv };

	struct odbc_obj *obj;
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir=?", odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		if (sscanf(rowdata, "%30d", &x) != 1)
			ast_log(LOG_WARNING, "Failed to read message count!\n");
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		return x;
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	return x - 1;

}

static void delete_file(char *sdir, int smsg)
{
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char msgnums[20];
	char *argv[] = { sdir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 2, .argv = argv };

	struct odbc_obj *obj;
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", smsg);
		snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE dir=? AND msgnum=?",odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt)
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		else
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
	return;	
}

static void copy_file(char *sdir, int smsg, char *ddir, int dmsg, char *dmailboxuser, char *dmailboxcontext)
{
	SQLHSTMT stmt;
	char sql[512];
	char msgnums[20];
	char msgnumd[20];
	struct odbc_obj *obj;
	char *argv[] = { ddir, msgnumd, dmailboxuser, dmailboxcontext, sdir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 6, .argv = argv };

	delete_file(ddir, dmsg);
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", smsg);
		snprintf(msgnumd, sizeof(msgnumd), "%d", dmsg);
		snprintf(sql, sizeof(sql), "INSERT INTO %s (dir, msgnum, context, macrocontext, callerid, origtime, duration, recording, mailboxuser, mailboxcontext) SELECT ?,?,context,macrocontext,callerid,origtime,duration,recording,?,? FROM %s WHERE dir=? AND msgnum=?",odbc_table,odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt)
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s] (You probably don't have MySQL 4.1 or later installed)\n\n", sql);
		else
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
	return;	
}

struct insert_cb_struct {
	char *dir;
	char *msgnum;
	void *recording;
	size_t recordinglen;
	SQLLEN indlen;
	const char *context;
	const char *macrocontext;
	const char *callerid;
	const char *origtime;
	const char *duration;
	char *mailboxuser;
	char *mailboxcontext;
	const char *category;
	char *sql;
};

static SQLHSTMT insert_cb(struct odbc_obj *obj, void *vd)
{
	struct insert_cb_struct *d = vd;
	int res;
	SQLHSTMT stmt;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	res = SQLPrepare(stmt, (unsigned char *)d->sql, SQL_NTS);
	if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
		ast_log(LOG_WARNING, "SQL Prepare failed![%s]\n", d->sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->dir), 0, (void *)d->dir, 0, NULL);
	SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->msgnum), 0, (void *)d->msgnum, 0, NULL);
	SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, d->recordinglen, 0, (void *)d->recording, 0, &d->indlen);
	SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->context), 0, (void *)d->context, 0, NULL);
	SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->macrocontext), 0, (void *)d->macrocontext, 0, NULL);
	SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->callerid), 0, (void *)d->callerid, 0, NULL);
	SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->origtime), 0, (void *)d->origtime, 0, NULL);
	SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->duration), 0, (void *)d->duration, 0, NULL);
	SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->mailboxuser), 0, (void *)d->mailboxuser, 0, NULL);
	SQLBindParameter(stmt, 10, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->mailboxcontext), 0, (void *)d->mailboxcontext, 0, NULL);
	if (!ast_strlen_zero(d->category)) {
		SQLBindParameter(stmt, 11, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(d->category), 0, (void *)d->category, 0, NULL);
	}

	return stmt;
}

static int store_file(char *dir, char *mailboxuser, char *mailboxcontext, int msgnum)
{
	int x = 0;
	int fd = -1;
	void *fdm = MAP_FAILED;
	size_t fdlen = -1;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char msgnums[20];
	char fn[PATH_MAX];
	char full_fn[PATH_MAX];
	char fmt[80]="";
	char *c;
	struct insert_cb_struct d = {
		.dir = dir,
		.msgnum = msgnums,
		.context = "",
		.macrocontext = "",
		.callerid = "",
		.origtime = "",
		.duration = "",
		.mailboxuser = mailboxuser,
		.mailboxcontext = mailboxcontext,
		.category = "",
		.sql = sql
	};
	struct ast_config *cfg=NULL;
	struct odbc_obj *obj;

	delete_file(dir, msgnum);
	obj = ast_odbc_request_obj(odbc_database, 0);
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
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		if (cfg) {
			d.context = ast_variable_retrieve(cfg, "message", "context");
			if (!d.context) d.context = "";
			d.macrocontext = ast_variable_retrieve(cfg, "message", "macrocontext");
			if (!d.macrocontext) d.macrocontext = "";
			d.callerid = ast_variable_retrieve(cfg, "message", "callerid");
			if (!d.callerid) d.callerid = "";
			d.origtime = ast_variable_retrieve(cfg, "message", "origtime");
			if (!d.origtime) d.origtime = "";
			d.duration = ast_variable_retrieve(cfg, "message", "duration");
			if (!d.duration) d.duration = "";
			d.category = ast_variable_retrieve(cfg, "message", "category");
			if (!d.category) d.category = "";
		}
		fdlen = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		printf("Length is %zd\n", fdlen);
		fdm = mmap(NULL, fdlen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (fdm == MAP_FAILED) {
			ast_log(LOG_WARNING, "Memory map failed!\n");
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		d.recording = fdm;
		d.recordinglen = d.indlen = fdlen; /* SQL_LEN_DATA_AT_EXEC(fdlen); */
		if (!ast_strlen_zero(d.category)) 
			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,macrocontext,callerid,origtime,duration,mailboxuser,mailboxcontext,category) VALUES (?,?,?,?,?,?,?,?,?,?,?)",odbc_table); 
		else
			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,macrocontext,callerid,origtime,duration,mailboxuser,mailboxcontext) VALUES (?,?, ? , ?,?,?,?,?,?,?)",odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, insert_cb, &d);
		if (stmt) {
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		}
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
yuck:
	if (cfg)
		ast_config_destroy(cfg);
	if (fdm != MAP_FAILED)
		munmap(fdm, fdlen);
	if (fd > -1)
		close(fd);
	return x;
}

static void rename_file(char *sdir, int smsg, char *mailboxuser, char *mailboxcontext, char *ddir, int dmsg)
{
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char msgnums[20];
	char msgnumd[20];
	struct odbc_obj *obj;
	char *argv[] = { ddir, msgnumd, mailboxuser, mailboxcontext, sdir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 6, .argv = argv };

	delete_file(ddir, dmsg);
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(msgnums, sizeof(msgnums), "%d", smsg);
		snprintf(msgnumd, sizeof(msgnumd), "%d", dmsg);
		snprintf(sql, sizeof(sql), "UPDATE %s SET dir=?, msgnum=?, mailboxuser=?, mailboxcontext=? WHERE dir=? AND msgnum=?",odbc_table);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt)
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		else
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
	return;	
}

#else
#ifndef IMAP_STORAGE
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
	char stxt[PATH_MAX];
	char dtxt[PATH_MAX];
	ast_filerename(sfn,dfn,NULL);
	snprintf(stxt, sizeof(stxt), "%s.txt", sfn);
	snprintf(dtxt, sizeof(dtxt), "%s.txt", dfn);
	rename(stxt, dtxt);
}
#endif

/*! 
 * \brief Determines the highest message number in use for a given user and mailbox folder.
 * \param vmu 
 * \param dir the folder the mailbox folder to look for messages. Used to construct the SQL where clause.
 *
 * This method is used when mailboxes are stored on the filesystem. (not ODBC and not IMAP).
 * Typical use to set the msgnum would be to take the value returned from this method and add one to it.
 *
 * \note Should always be called with a lock already set on dir.
 * \return the value of zero or greater to indicate the last message index in use, -1 to indicate none.
 */
#if (!defined(IMAP_STORAGE) && !defined(ODBC_STORAGE))
static int last_message_index(struct ast_vm_user *vmu, char *dir)
{
	int x;
	unsigned char map[MAXMSGLIMIT] = "";
	DIR *msgdir;
	struct dirent *msgdirent;
	int msgdirint;
	char extension[4];
	int stopcount = 0;

	/* Reading the entire directory into a file map scales better than
	 * doing a stat repeatedly on a predicted sequence.  I suspect this
	 * is partially due to stat(2) internally doing a readdir(2) itself to
	 * find each file. */
	if (!(msgdir = opendir(dir))) {
		return -1;
	}

	while ((msgdirent = readdir(msgdir))) {
		if (sscanf(msgdirent->d_name, "msg%30d.%3s", &msgdirint, extension) == 2 && !strcmp(extension, "txt") && msgdirint < MAXMSGLIMIT) {
			map[msgdirint] = 1;
			stopcount++;
			if (option_debug > 3) {
				ast_log(LOG_DEBUG, "%s map[%d] = %d, count = %d\n", dir, msgdirint, map[msgdirint], stopcount);
			}
		}
	}
	closedir(msgdir);

	for (x = 0; x < vmu->maxmsg; x++) {
		if (map[x] == 1) {
			stopcount--;
		} else if (map[x] == 0 && !stopcount) {
			break;
		}
	}

	return x - 1;
}
#endif
#endif
#if (defined(IMAP_STORAGE) || defined(ODBC_STORAGE))
static int remove_file(char *dir, int msgnum)
{
	char fn[PATH_MAX];
	char full_fn[PATH_MAX];
	char msgnums[80];
	
	if (msgnum > -1) {
		snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
		make_file(fn, sizeof(fn), dir, msgnum);
	} else {
#ifndef IMAP_STORAGE
		ast_copy_string(fn, dir, sizeof(fn));
#else
		/*IMAP stores greetings locally so it should not
		 * try to dispose of them
		 */
		return 0;
#endif
	}
	ast_filedelete(fn, NULL);	
	snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
	unlink(full_fn);
	return 0;
}
#endif

#ifndef IMAP_STORAGE
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
			ast_log(LOG_WARNING, "Unable to open %s in read-only mode: %s\n", infile, strerror(errno));
			return -1;
		}
		if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, VOICEMAIL_FILE_MODE)) < 0) {
			ast_log(LOG_WARNING, "Unable to open %s in write-only mode: %s\n", outfile, strerror(errno));
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
#endif

#ifndef IMAP_STORAGE
static void copy_plain_file(char *frompath, char *topath)
{
	char frompath2[PATH_MAX], topath2[PATH_MAX];
	ast_filecopy(frompath, topath, NULL);
	snprintf(frompath2, sizeof(frompath2), "%s.txt", frompath);
	snprintf(topath2, sizeof(topath2), "%s.txt", topath);
	copy(frompath2, topath2);
}
#endif

#ifndef IMAP_STORAGE
static int vm_delete(char *file)
{
	char *txt;
	int txtsize = 0;

	txtsize = (strlen(file) + 5)*sizeof(char);
	txt = alloca(txtsize);
	/* Sprintf here would safe because we alloca'd exactly the right length,
	 * but trying to eliminate all sprintf's anyhow
	 */
	snprintf(txt, txtsize, "%s.txt", file);
	unlink(txt);
	return ast_filedelete(file, NULL);
}
#endif

static int inbuf(struct baseio *bio, FILE *fi)
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

static int inchar(struct baseio *bio, FILE *fi)
{
	if (bio->iocp>=bio->iolen) {
		if (!inbuf(bio, fi))
			return EOF;
	}

	return bio->iobuf[bio->iocp++];
}

static int ochar(struct baseio *bio, int c, FILE *so)
{
	if (bio->linelength >= BASELINELEN) {
		if (fputs(ENDL, so) == EOF) {
			return -1;
		}

		bio->linelength = 0;
	}

	if (putc(((unsigned char) c), so) == EOF) {
		return -1;
	}

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
		ast_log(LOG_WARNING, "Failed to open file: %s: %s\n", filename, strerror(errno));
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

	fclose(fi);
	
	if (fputs(ENDL, so) == EOF) {
		return 0;
	}

	return 1;
}

static void prep_email_sub_vars(struct ast_channel *ast, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, const char *fromfolder, char *cidnum, char *cidname, char *dur, char *date, char *passdata, size_t passdatasize, const char *category)
{
	char callerid[256];
	char fromdir[256], fromfile[256];
	struct ast_config *msg_cfg;
	const char *origcallerid, *origtime;
	char origcidname[80], origcidnum[80], origdate[80];
	int inttime;

	/* Prepare variables for substition in email body and subject */
	pbx_builtin_setvar_helper(ast, "VM_NAME", vmu->fullname);
	pbx_builtin_setvar_helper(ast, "VM_DUR", dur);
	snprintf(passdata, passdatasize, "%d", msgnum);
	pbx_builtin_setvar_helper(ast, "VM_MSGNUM", passdata);
	pbx_builtin_setvar_helper(ast, "VM_CONTEXT", context);
	pbx_builtin_setvar_helper(ast, "VM_MAILBOX", mailbox);
	pbx_builtin_setvar_helper(ast, "VM_CALLERID", (!ast_strlen_zero(cidname) || !ast_strlen_zero(cidnum)) ?
		ast_callerid_merge(callerid, sizeof(callerid), cidname, cidnum, NULL) : "an unknown caller");
	pbx_builtin_setvar_helper(ast, "VM_CIDNAME", (!ast_strlen_zero(cidname) ? cidname : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_CIDNUM", (!ast_strlen_zero(cidnum) ? cidnum : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_DATE", date);
	pbx_builtin_setvar_helper(ast, "VM_CATEGORY", category ? ast_strdupa(category) : "no category");

	/* Retrieve info from VM attribute file */
	make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, fromfolder);
	make_file(fromfile, sizeof(fromfile), fromdir, msgnum - 1);
	if (strlen(fromfile) < sizeof(fromfile) - 5) {
		strcat(fromfile, ".txt");
	}
	if (!(msg_cfg = ast_config_load(fromfile))) {
		if (option_debug > 0) {
			ast_log(LOG_DEBUG, "Config load for message text file '%s' failed\n", fromfile);
		}
		return;
	}

	if ((origcallerid = ast_variable_retrieve(msg_cfg, "message", "callerid"))) {
		pbx_builtin_setvar_helper(ast, "ORIG_VM_CALLERID", origcallerid);
		ast_callerid_split(origcallerid, origcidname, sizeof(origcidname), origcidnum, sizeof(origcidnum));
		pbx_builtin_setvar_helper(ast, "ORIG_VM_CIDNAME", origcidname);
		pbx_builtin_setvar_helper(ast, "ORIG_VM_CIDNUM", origcidnum);
	}

	if ((origtime = ast_variable_retrieve(msg_cfg, "message", "origtime")) && sscanf(origtime, "%30d", &inttime) == 1) {
		time_t ttime = inttime;
		struct tm tm;
		ast_localtime(&ttime, &tm, NULL);
		strftime(origdate, sizeof(origdate), emaildateformat, &tm);
		pbx_builtin_setvar_helper(ast, "ORIG_VM_DATE", origdate);
	}
	ast_config_destroy(msg_cfg);
}

static char *quote(const char *from, char *to, size_t len)
{
	char *ptr = to;
	*ptr++ = '"';
	for (; ptr < to + len - 1; from++) {
		if (*from == '"')
			*ptr++ = '\\';
		else if (*from == '\0')
			break;
		*ptr++ = *from;
	}
	if (ptr < to + len - 1)
		*ptr++ = '"';
	*ptr = '\0';
	return to;
}
/*
 * fill in *tm for current time according to the proper timezone, if any.
 * Return tm so it can be used as a function argument.
 */
static const struct tm *vmu_tm(const struct ast_vm_user *vmu, struct tm *tm)
{
	const struct vm_zone *z = NULL;
	time_t t = time(NULL);

	/* Does this user have a timezone specified? */
	if (!ast_strlen_zero(vmu->zonetag)) {
		/* Find the zone in the list */
		AST_LIST_LOCK(&zones);
		AST_LIST_TRAVERSE(&zones, z, list) {
			if (!strcmp(z->name, vmu->zonetag))
				break;
		}
		AST_LIST_UNLOCK(&zones);
	}
	ast_localtime(&t, tm, z ? z->timezone : NULL);
	return tm;
}

/*!\brief Check if the string would need encoding within the MIME standard, to
 * avoid confusing certain mail software that expects messages to be 7-bit
 * clean.
 */
static int check_mime(const char *str)
{
	for (; *str; str++) {
		if (*str > 126 || *str < 32 || strchr("()<>@,:;/\"[]?.=", *str)) {
			return 1;
		}
	}
	return 0;
}

/*!\brief Encode a string according to the MIME rules for encoding strings
 * that are not 7-bit clean or contain control characters.
 *
 * Additionally, if the encoded string would exceed the MIME limit of 76
 * characters per line, then the encoding will be broken up into multiple
 * sections, separated by a space character, in order to facilitate
 * breaking up the associated header across multiple lines.
 *
 * \param start A string to be encoded
 * \param end An expandable buffer for holding the result
 * \param preamble The length of the first line already used for this string,
 * to ensure that each line maintains a maximum length of 76 chars.
 * \param postamble the length of any additional characters appended to the
 * line, used to ensure proper field wrapping.
 * \retval The encoded string.
 */
static char *encode_mime_str(const char *start, char *end, size_t endsize, size_t preamble, size_t postamble)
{
	char tmp[80];
	int first_section = 1;
	size_t endlen = 0, tmplen = 0;
	*end = '\0';

	tmplen = snprintf(tmp, sizeof(tmp), "=?%s?Q?", charset);
	for (; *start; start++) {
		int need_encoding = 0;
		if (*start < 33 || *start > 126 || strchr("()<>@,:;/\"[]?.=_", *start)) {
			need_encoding = 1;
		}
		if ((first_section && need_encoding && preamble + tmplen > 70) ||
			(first_section && !need_encoding && preamble + tmplen > 72) ||
			(!first_section && need_encoding && tmplen > 70) ||
			(!first_section && !need_encoding && tmplen > 72)) {
			/* Start new line */
			endlen += snprintf(end + endlen, endsize - endlen, "%s%s?=", first_section ? "" : " ", tmp);
			tmplen = snprintf(tmp, sizeof(tmp), "=?%s?Q?", charset);
			first_section = 0;
		}
		if (need_encoding && *start == ' ') {
			tmplen += snprintf(tmp + tmplen, sizeof(tmp) - tmplen, "_");
		} else if (need_encoding) {
			tmplen += snprintf(tmp + tmplen, sizeof(tmp) - tmplen, "=%hhX", *start);
		} else {
			tmplen += snprintf(tmp + tmplen, sizeof(tmp) - tmplen, "%c", *start);
		}
	}
	snprintf(end + endlen, endsize - endlen, "%s%s?=%s", first_section ? "" : " ", tmp, endlen + postamble > 74 ? " " : "");
	return end;
}

static void make_email_file(FILE *p, char *srcemail, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, const char *fromfolder, char *cidnum, char *cidname, char *attach, char *format, int duration, int attach_user_voicemail, struct ast_channel *chan, const char *category, int imap)
{
	char date[256];
	char host[MAXHOSTNAMELEN] = "";
	char who[256];
	char bound[256];
	char fname[256];
	char dur[256];
	char tmpcmd[256];
	char enc_cidnum[256] = "", enc_cidname[256] = "";
	struct tm tm;
	char *passdata = NULL, *passdata2;
	size_t len_passdata = 0, len_passdata2, tmplen;

	/* One alloca for multiple fields */
	len_passdata2 = strlen(vmu->fullname);
	if (emailsubject && (tmplen = strlen(emailsubject)) > len_passdata2) {
		len_passdata2 = tmplen;
	}
	if ((tmplen = strlen(emailtitle)) > len_passdata2) {
		len_passdata2 = tmplen;
	}
	if ((tmplen = strlen(fromstring)) > len_passdata2) {
		len_passdata2 = tmplen;
	}
	len_passdata2 = len_passdata2 * 3 + 200;
	passdata2 = alloca(len_passdata2);

	if (cidnum) {
		strip_control_and_high(cidnum, enc_cidnum, sizeof(enc_cidnum));
	}
	if (cidname) {
		strip_control_and_high(cidname, enc_cidname, sizeof(enc_cidname));
	}
	gethostname(host, sizeof(host) - 1);
	if (strchr(srcemail, '@'))
		ast_copy_string(who, srcemail, sizeof(who));
	else {
		snprintf(who, sizeof(who), "%s@%s", srcemail, host);
	}
	snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
	strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", vmu_tm(vmu, &tm));
	fprintf(p, "Date: %s" ENDL, date);

	/* Set date format for voicemail mail */
	strftime(date, sizeof(date), emaildateformat, &tm);

	if (*fromstring) {
		struct ast_channel *ast;
		if ((ast = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Substitution/voicemail"))) {
			char *ptr;
			memset(passdata2, 0, len_passdata2);
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, enc_cidnum, enc_cidname, dur, date, passdata2, len_passdata2, category);
			pbx_substitute_variables_helper(ast, fromstring, passdata2, len_passdata2);
			len_passdata = strlen(passdata2) * 3 + 300;
			passdata = alloca(len_passdata);
			if (check_mime(passdata2)) {
				int first_line = 1;
				encode_mime_str(passdata2, passdata, len_passdata, strlen("From: "), strlen(who) + 3);
				while ((ptr = strchr(passdata, ' '))) {
					*ptr = '\0';
					fprintf(p, "%s %s" ENDL, first_line ? "From:" : "", passdata);
					first_line = 0;
					passdata = ptr + 1;
				}
				fprintf(p, "%s %s <%s>" ENDL, first_line ? "From:" : "", passdata, who);
			} else {
				fprintf(p, "From: %s <%s>" ENDL, quote(passdata2, passdata, len_passdata), who);
			}
			ast_channel_free(ast);
		} else
			ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
	} else
		fprintf(p, "From: Asterisk PBX <%s>" ENDL, who);

	if (check_mime(vmu->fullname)) {
		int first_line = 1;
		char *ptr;
		encode_mime_str(vmu->fullname, passdata2, len_passdata2, strlen("To: "), strlen(vmu->email) + 3);
		while ((ptr = strchr(passdata2, ' '))) {
			*ptr = '\0';
			fprintf(p, "%s %s" ENDL, first_line ? "To:" : "", passdata2);
			first_line = 0;
			passdata2 = ptr + 1;
		}
		fprintf(p, "%s %s <%s>" ENDL, first_line ? "To:" : "", passdata2, vmu->email);
	} else {
		fprintf(p, "To: %s <%s>" ENDL, quote(vmu->fullname, passdata2, len_passdata2), vmu->email);
	}
	if (emailsubject) {
		struct ast_channel *ast;
		if ((ast = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Substitution/voicemail"))) {
			int vmlen = strlen(emailsubject) * 3 + 200;
			/* Only allocate more space if the previous was not large enough */
			if (vmlen > len_passdata) {
				passdata = alloca(vmlen);
				len_passdata = vmlen;
			}

			memset(passdata, 0, len_passdata);
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, passdata, len_passdata, category);
			pbx_substitute_variables_helper(ast, emailsubject, passdata, len_passdata);
			if (check_mime(passdata)) {
				int first_line = 1;
				char *ptr;
				encode_mime_str(passdata, passdata2, len_passdata2, strlen("Subject: "), 0);
				while ((ptr = strchr(passdata2, ' '))) {
					*ptr = '\0';
					fprintf(p, "%s %s" ENDL, first_line ? "Subject:" : "", passdata2);
					first_line = 0;
					passdata2 = ptr + 1;
				}
				fprintf(p, "%s %s" ENDL, first_line ? "Subject:" : "", passdata2);
			} else {
				fprintf(p, "Subject: %s" ENDL, passdata);
			}
			ast_channel_free(ast);
		} else {
			ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else	if (*emailtitle) {
		fprintf(p, emailtitle, msgnum + 1, mailbox) ;
		fprintf(p, ENDL) ;
	} else if (ast_test_flag((&globalflags), VM_PBXSKIP)) {
		fprintf(p, "Subject: New message %d in mailbox %s" ENDL, msgnum + 1, mailbox);
	} else {
		fprintf(p, "Subject: [PBX]: New message %d in mailbox %s" ENDL, msgnum + 1, mailbox);
	}
	fprintf(p, "Message-ID: <Asterisk-%d-%d-%s-%d@%s>" ENDL, msgnum + 1, (unsigned int)ast_random(), mailbox, (int)getpid(), host);
	if (imap) {
		/* additional information needed for IMAP searching */
		fprintf(p, "X-Asterisk-VM-Message-Num: %d" ENDL, msgnum + 1);
		/* fprintf(p, "X-Asterisk-VM-Orig-Mailbox: %s" ENDL, ext); */
		fprintf(p, "X-Asterisk-VM-Server-Name: %s" ENDL, fromstring);
		fprintf(p, "X-Asterisk-VM-Context: %s" ENDL, context);
#ifdef IMAP_STORAGE
		fprintf(p, "X-Asterisk-VM-Extension: %s" ENDL, (!ast_strlen_zero(vmu->imapvmshareid) ? vmu->imapvmshareid : mailbox));
#else
		fprintf(p, "X-Asterisk-VM-Extension: %s" ENDL, mailbox);
#endif
		fprintf(p, "X-Asterisk-VM-Priority: %d" ENDL, chan->priority);
		fprintf(p, "X-Asterisk-VM-Caller-channel: %s" ENDL, chan->name);
		fprintf(p, "X-Asterisk-VM-Caller-ID-Num: %s" ENDL, enc_cidnum);
		fprintf(p, "X-Asterisk-VM-Caller-ID-Name: %s" ENDL, enc_cidname);
		fprintf(p, "X-Asterisk-VM-Duration: %d" ENDL, duration);
		if (!ast_strlen_zero(category)) {
			fprintf(p, "X-Asterisk-VM-Category: %s" ENDL, category);
		}
		fprintf(p, "X-Asterisk-VM-Orig-date: %s" ENDL, date);
		fprintf(p, "X-Asterisk-VM-Orig-time: %ld" ENDL, (long)time(NULL));
	}
	if (!ast_strlen_zero(cidnum)) {
		fprintf(p, "X-Asterisk-CallerID: %s" ENDL, enc_cidnum);
	}
	if (!ast_strlen_zero(cidname)) {
		fprintf(p, "X-Asterisk-CallerIDName: %s" ENDL, enc_cidname);
	}
	fprintf(p, "MIME-Version: 1.0" ENDL);
	if (attach_user_voicemail) {
		/* Something unique. */
		snprintf(bound, sizeof(bound), "----voicemail_%d%s%d%d", msgnum + 1, mailbox, (int)getpid(), (unsigned int)ast_random());

		fprintf(p, "Content-Type: multipart/mixed; boundary=\"%s\"" ENDL, bound);
		fprintf(p, ENDL ENDL "This is a multi-part message in MIME format." ENDL ENDL);
		fprintf(p, "--%s" ENDL, bound);
	}
	fprintf(p, "Content-Type: text/plain; charset=%s" ENDL "Content-Transfer-Encoding: 8bit" ENDL ENDL, charset);
	if (emailbody) {
		struct ast_channel *ast;
		if ((ast = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Substitution/voicemail"))) {
			char *passdata;
			int vmlen = strlen(emailbody)*3 + 200;
			if ((passdata = alloca(vmlen))) {
				memset(passdata, 0, vmlen);
				prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, passdata, vmlen, category);
				pbx_substitute_variables_helper(ast, emailbody, passdata, vmlen);
#ifdef IMAP_STORAGE
				{
					/* Convert body to native line terminators for IMAP backend */
					char *line = passdata, *next;
					do {
						/* Terminate line before outputting it to the file */
						if ((next = strchr(line, '\n'))) {
							*next++ = '\0';
						}
						fprintf(p, "%s" ENDL, line);
						line = next;
					} while (!ast_strlen_zero(line));
				}
#else
				fprintf(p, "%s" ENDL, passdata);
#endif
			} else
				ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
			ast_channel_free(ast);
		} else
			ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
	} else {
		if (strcmp(vmu->mailbox, mailbox)) {
			/* Forwarded type */
			struct ast_config *msg_cfg;
			const char *v;
			int inttime;
			char fromdir[256], fromfile[256], origdate[80] = "", origcallerid[80] = "";
			/* Retrieve info from VM attribute file */
			make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, fromfolder);
			make_file(fromfile, sizeof(fromfile), fromdir, msgnum);
			if (strlen(fromfile) < sizeof(fromfile) - 5) {
				strcat(fromfile, ".txt");
			}
			if ((msg_cfg = ast_config_load(fromfile))) {
				if ((v = ast_variable_retrieve(msg_cfg, "message", "callerid"))) {
					ast_copy_string(origcallerid, v, sizeof(origcallerid));
				}

				/* You might be tempted to do origdate, except that a) it's in the wrong
				 * format, and b) it's missing for IMAP recordings. */
				if ((v = ast_variable_retrieve(msg_cfg, "message", "origtime")) && sscanf(v, "%30d", &inttime) == 1) {
					time_t ttime = inttime;
					struct tm tm;
					ast_localtime(&ttime, &tm, NULL);
					strftime(origdate, sizeof(origdate), emaildateformat, &tm);
				}
				fprintf(p, "Dear %s:" ENDL ENDL "\tJust wanted to let you know you were just forwarded"
					" a %s long message (number %d)" ENDL "in mailbox %s from %s, on %s" ENDL
					"(originally sent by %s on %s)" ENDL "so you might want to check it when you get a"
					" chance.  Thanks!" ENDL ENDL "\t\t\t\t--Asterisk" ENDL ENDL, vmu->fullname, dur,
					msgnum + 1, mailbox, (cidname ? cidname : (cidnum ? cidnum : "an unknown caller")),
					date, origcallerid, origdate);
				ast_config_destroy(msg_cfg);
			} else {
				goto plain_message;
			}
		} else {
plain_message:
			fprintf(p, "Dear %s:" ENDL ENDL "\tJust wanted to let you know you were just left a "
				"%s long message (number %d)" ENDL "in mailbox %s from %s, on %s so you might" ENDL
				"want to check it when you get a chance.  Thanks!" ENDL ENDL "\t\t\t\t--Asterisk"
				ENDL ENDL, vmu->fullname, dur, msgnum + 1, mailbox,
				(cidname ? cidname : (cidnum ? cidnum : "an unknown caller")), date);
		}
	}
	if (attach_user_voicemail) {
		/* Eww. We want formats to tell us their own MIME type */
		char *ctype = (!strcasecmp(format, "ogg")) ? "application/" : "audio/x-";
		char tmpdir[256], newtmp[256];
		int tmpfd = -1;
		int soxstatus = 0;

		if (vmu->volgain < -.001 || vmu->volgain > .001) {
			create_dirpath(tmpdir, sizeof(tmpdir), vmu->context, vmu->mailbox, "tmp");
			snprintf(newtmp, sizeof(newtmp), "%s/XXXXXX", tmpdir);
			tmpfd = mkstemp(newtmp);
			chmod(newtmp, VOICEMAIL_FILE_MODE & ~my_umask);
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "newtmp: %s\n", newtmp);
			if (tmpfd > -1) {
				snprintf(tmpcmd, sizeof(tmpcmd), "sox -v %.4f %s.%s %s.%s", vmu->volgain, attach, format, newtmp, format);
				if ((soxstatus = ast_safe_system(tmpcmd)) == 0) {
					attach = newtmp;
					if (option_debug > 2) {
						ast_log(LOG_DEBUG, "VOLGAIN: Stored at: %s.%s - Level: %.4f - Mailbox: %s\n", attach, format, vmu->volgain, mailbox);
					}
				} else {
					ast_log(LOG_WARNING, "Sox failed to re-encode %s.%s: %s (have you installed support for all sox file formats?)\n", attach, format,
						soxstatus == 1 ? "Problem with command line options" : "An error occurred during file processing");
					ast_log(LOG_WARNING, "Voicemail attachment will have no volume gain.\n");
				}
			}
		}
		fprintf(p, "--%s" ENDL, bound);
		fprintf(p, "Content-Type: %s%s; name=\"msg%04d.%s\"" ENDL, ctype, format, msgnum + 1, format);
		fprintf(p, "Content-Transfer-Encoding: base64" ENDL);
		fprintf(p, "Content-Description: Voicemail sound attachment." ENDL);
		fprintf(p, "Content-Disposition: attachment; filename=\"msg%04d.%s\"" ENDL ENDL, msgnum + 1, format);
		snprintf(fname, sizeof(fname), "%s.%s", attach, format);
		base_encode(fname, p);
		fprintf(p, ENDL "--%s--" ENDL "." ENDL, bound);
		if (tmpfd > -1) {
			if (soxstatus == 0) {
				unlink(fname);
			}
			close(tmpfd);
			unlink(newtmp);
		}
	}
}

static int sendmail(char *srcemail, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, char *fromfolder, char *cidnum, char *cidname, char *attach, char *format, int duration, int attach_user_voicemail, struct ast_channel *chan, const char *category)
{
	FILE *p=NULL;
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[256];
	char *stringp;

	if (vmu && ast_strlen_zero(vmu->email)) {
		ast_log(LOG_WARNING, "E-mail address missing for mailbox [%s].  E-mail will not be sent.\n", vmu->mailbox);
		return(0);
	}

	/* Mail only the first format */
	format = ast_strdupa(format);
	stringp = format;
	strsep(&stringp, "|");

	if (!strcmp(format, "wav49"))
		format = "WAV";
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Attaching file '%s', format '%s', uservm is '%d', global is %d\n", attach, format, attach_user_voicemail, ast_test_flag((&globalflags), VM_ATTACH));
	/* Make a temporary file instead of piping directly to sendmail, in case the mail
	   command hangs */
	if ((p = vm_mkftemp(tmp)) == NULL) {
		ast_log(LOG_WARNING, "Unable to launch '%s' (can't create temporary file)\n", mailcmd);
		return -1;
	} else {
		make_email_file(p, srcemail, vmu, msgnum, context, mailbox, fromfolder, cidnum, cidname, attach, format, duration, attach_user_voicemail, chan, category, 0);
		fclose(p);
		snprintf(tmp2, sizeof(tmp2), "( %s < %s ; rm -f %s ) &", mailcmd, tmp, tmp);
		ast_safe_system(tmp2);
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Sent mail to %s with command '%s'\n", vmu->email, mailcmd);
	}
	return 0;
}

static int sendpage(char *srcemail, char *pager, int msgnum, char *context, char *mailbox, const char *fromfolder, char *cidnum, char *cidname, int duration, struct ast_vm_user *vmu, const char *category)
{
	char date[256];
	char host[MAXHOSTNAMELEN] = "";
	char who[256];
	char dur[PATH_MAX];
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[PATH_MAX];
	struct tm tm;
	FILE *p;

	if ((p = vm_mkftemp(tmp)) == NULL) {
		ast_log(LOG_WARNING, "Unable to launch '%s' (can't create temporary file)\n", mailcmd);
		return -1;
	} else {
		gethostname(host, sizeof(host)-1);
		if (strchr(srcemail, '@'))
			ast_copy_string(who, srcemail, sizeof(who));
		else {
			snprintf(who, sizeof(who), "%s@%s", srcemail, host);
		}
		snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", vmu_tm(vmu, &tm));
		fprintf(p, "Date: %s\n", date);

		if (*pagerfromstring) {
			struct ast_channel *ast;
			if ((ast = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Substitution/voicemail"))) {
				char *passdata;
				int vmlen = strlen(fromstring)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, passdata, vmlen, category);
					pbx_substitute_variables_helper(ast, pagerfromstring, passdata, vmlen);
					fprintf(p, "From: %s <%s>\n", passdata, who);
				} else 
					ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else
			fprintf(p, "From: Asterisk PBX <%s>\n", who);
		fprintf(p, "To: %s\n", pager);
		if (pagersubject) {
			struct ast_channel *ast;
			if ((ast = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Substitution/voicemail"))) {
				char *passdata;
				int vmlen = strlen(pagersubject) * 3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, passdata, vmlen, category);
					pbx_substitute_variables_helper(ast, pagersubject, passdata, vmlen);
					fprintf(p, "Subject: %s\n\n", passdata);
				} else ast_log(LOG_WARNING, "Cannot allocate workspace for variable substitution\n");
				ast_channel_free(ast);
			} else ast_log(LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		} else
			fprintf(p, "Subject: New VM\n\n");
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", &tm);
		if (pagerbody) {
			struct ast_channel *ast;
			if ((ast = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Substitution/voicemail"))) {
				char *passdata;
				int vmlen = strlen(pagerbody)*3 + 200;
				if ((passdata = alloca(vmlen))) {
					memset(passdata, 0, vmlen);
					prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, passdata, vmlen, category);
					pbx_substitute_variables_helper(ast, pagerbody, passdata, vmlen);
					fprintf(p, "%s\n", passdata);
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
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Sent page to %s with command '%s'\n", pager, mailcmd);
	}
	return 0;
}

static int get_date(char *s, int len)
{
	struct tm tm;
	time_t t;

	time(&t);

	ast_localtime(&t, &tm, NULL);

	return strftime(s, len, "%a %b %e %r %Z %Y", &tm);
}

static int play_greeting(struct ast_channel *chan, struct ast_vm_user *vmu, char *filename, char *ecodes)
{
	int res = -2;

#ifdef ODBC_STORAGE
	int success = 
#endif
	RETRIEVE(filename, -1, vmu);
	if (ast_fileexists(filename, NULL, NULL) > 0) {
		res = ast_streamfile(chan, filename, chan->language);
		if (res > -1) 
			res = ast_waitstream(chan, ecodes);
#ifdef ODBC_STORAGE
		if (success == -1) {
			/* We couldn't retrieve the file from the database, but we found it on the file system. Let's put it in the database. */
			if (option_debug)
				ast_log(LOG_DEBUG, "Greeting not retrieved from database, but found in file storage. Inserting into database\n");
			store_file(filename, vmu->mailbox, vmu->context, -1);
		}
#endif
	}
	DISPOSE(filename, -1);

	return res;
}

static int invent_message(struct ast_channel *chan, struct ast_vm_user *vmu, char *ext, int busy, char *ecodes)
{
	int res;
	char fn[PATH_MAX];
	char dest[PATH_MAX];

	snprintf(fn, sizeof(fn), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, ext);

	if ((res = create_dirpath(dest, sizeof(dest), vmu->context, ext, "greet"))) {
		ast_log(LOG_WARNING, "Failed to make directory(%s)\n", fn);
		return -1;
	}

	res = play_greeting(chan, vmu, fn, ecodes);
	if (res == -2) {
		/* File did not exist */
		res = ast_stream_and_wait(chan, "vm-theperson", chan->language, ecodes);
		if (res)
			return res;
		res = ast_say_digit_str(chan, ext, ecodes, chan->language);
	}

	if (res)
		return res;

	res = ast_stream_and_wait(chan, busy ? "vm-isonphone" : "vm-isunavail", chan->language, ecodes);
	return res;
}

static void free_zone(struct vm_zone *z)
{
	free(z);
}

#ifdef ODBC_STORAGE
/*! XXX \todo Fix this function to support multiple mailboxes in the intput string */
static int inboxcount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	int x = -1;
	int res;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char rowdata[20];
	char tmp[PATH_MAX] = "";
	struct odbc_obj *obj;
	char *context;
	struct generic_prepare_struct gps = { .sql = sql, .argc = 0 };

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
	
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir = '%s%s/%s/%s'", odbc_table, VM_SPOOL_DIR, context, tmp, "INBOX");
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		*newmsgs = atoi(rowdata);
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);

		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir = '%s%s/%s/%s'", odbc_table, VM_SPOOL_DIR, context, tmp, "Old");
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLFetch(stmt);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
		if ((res != SQL_SUCCESS) && (res != SQL_SUCCESS_WITH_INFO)) {
			ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
			SQLFreeHandle (SQL_HANDLE_STMT, stmt);
			ast_odbc_release_obj(obj);
			goto yuck;
		}
		SQLFreeHandle (SQL_HANDLE_STMT, stmt);
		ast_odbc_release_obj(obj);
		*oldmsgs = atoi(rowdata);
		x = 0;
	} else
		ast_log(LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		
yuck:
	return x;
}

static int messagecount(const char *context, const char *mailbox, const char *folder)
{
	struct odbc_obj *obj = NULL;
	int nummsgs = 0;
	int res;
	SQLHSTMT stmt = NULL;
	char sql[PATH_MAX];
	char rowdata[20];
	struct generic_prepare_struct gps = { .sql = sql, .argc = 0 };
	if (!folder)
		folder = "INBOX";
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (obj) {
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir = '%s%s/%s/%s'", odbc_table, VM_SPOOL_DIR, context, mailbox, folder);
		stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
		if (!stmt) {
			ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
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
	if (obj)
		ast_odbc_release_obj(obj);
	return nummsgs;
}

static int has_voicemail(const char *mailbox, const char *folder)
{
	char tmp[256], *tmp2 = tmp, *mbox, *context;
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	while ((context = mbox = strsep(&tmp2, ","))) {
		strsep(&context, "@");
		if (ast_strlen_zero(context))
			context = "default";
		if (messagecount(context, mbox, folder))
			return 1;
	}
	return 0;
}
#endif
#ifndef IMAP_STORAGE
/* copy message only used by file storage */
static int copy_message(struct ast_channel *chan, struct ast_vm_user *vmu, int imbox, int msgnum, long duration, struct ast_vm_user *recip, char *fmt, char *dir)
{
	char fromdir[PATH_MAX], todir[PATH_MAX], frompath[PATH_MAX], topath[PATH_MAX];
	const char *frombox = mbox(imbox);
	int recipmsgnum;
	int res = 0;

	ast_log(LOG_NOTICE, "Copying message from %s@%s to %s@%s\n", vmu->mailbox, vmu->context, recip->mailbox, recip->context);

	create_dirpath(todir, sizeof(todir), recip->context, recip->mailbox, "INBOX");
	
	if (!dir)
		make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, frombox);
	else
		ast_copy_string(fromdir, dir, sizeof(fromdir));

	make_file(frompath, sizeof(frompath), fromdir, msgnum);

	if (vm_lock_path(todir))
		return ERROR_LOCK_PATH;

	recipmsgnum = 0;
	do {
		make_file(topath, sizeof(topath), todir, recipmsgnum);
		if (!EXISTS(todir, recipmsgnum, topath, chan->language))
			break;
		recipmsgnum++;
	} while (recipmsgnum < recip->maxmsg);
	if (recipmsgnum < recip->maxmsg - (imbox ? 0 : inprocess_count(vmu->mailbox, vmu->context, 0))) {
#ifndef ODBC_STORAGE
		if (EXISTS(fromdir, msgnum, frompath, chan->language)) {
			COPY(fromdir, msgnum, todir, recipmsgnum, recip->mailbox, recip->context, frompath, topath);
		} else {
#endif
			/* If we are prepending a message for ODBC, then the message already
			 * exists in the database, but we want to force copying from the
			 * filesystem (since only the FS contains the prepend). */
			copy_plain_file(frompath, topath);
			STORE(todir, recip->mailbox, recip->context, recipmsgnum, chan, recip, fmt, duration, NULL);
			vm_delete(topath);
#ifndef ODBC_STORAGE
		}
#endif
	} else {
		ast_log(LOG_ERROR, "Recipient mailbox %s@%s is full\n", recip->mailbox, recip->context);
		res = -1;
	}
	ast_unlock_path(todir);
	notify_new_message(chan, recip, recipmsgnum, duration, fmt, S_OR(chan->cid.cid_num, NULL), S_OR(chan->cid.cid_name, NULL));
	
	return res;
}
#endif
#if !(defined(IMAP_STORAGE) || defined(ODBC_STORAGE))
static int messagecount(const char *context, const char *mailbox, const char *folder)
{
	return __has_voicemail(context, mailbox, folder, 0);
}


static int __has_voicemail(const char *context, const char *mailbox, const char *folder, int shortcircuit)
{
	DIR *dir;
	struct dirent *de;
	char fn[256];
	int ret = 0;
	if (!folder)
		folder = "INBOX";
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;
	if (!context)
		context = "default";
	snprintf(fn, sizeof(fn), "%s%s/%s/%s", VM_SPOOL_DIR, context, mailbox, folder);
	dir = opendir(fn);
	if (!dir)
		return 0;
	while ((de = readdir(dir))) {
		if (!strncasecmp(de->d_name, "msg", 3)) {
			if (shortcircuit) {
				ret = 1;
				break;
			} else if (!strncasecmp(de->d_name + 8, "txt", 3))
				ret++;
		}
	}
	closedir(dir);
	return ret;
}


static int has_voicemail(const char *mailbox, const char *folder)
{
	char tmp[256], *tmp2 = tmp, *mbox, *context;
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	while ((mbox = strsep(&tmp2, ","))) {
		if ((context = strchr(mbox, '@')))
			*context++ = '\0';
		else
			context = "default";
		if (__has_voicemail(context, mbox, folder, 1))
			return 1;
	}
	return 0;
}


static int inboxcount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	char tmp[256];
	char *context;

	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;
	if (strchr(mailbox, ',')) {
		int tmpnew, tmpold;
		char *mb, *cur;

		ast_copy_string(tmp, mailbox, sizeof(tmp));
		mb = tmp;
		while ((cur = strsep(&mb, ", "))) {
			if (!ast_strlen_zero(cur)) {
				if (inboxcount(cur, newmsgs ? &tmpnew : NULL, oldmsgs ? &tmpold : NULL))
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
	if (newmsgs)
		*newmsgs = __has_voicemail(context, tmp, "INBOX", 0);
	if (oldmsgs)
		*oldmsgs = __has_voicemail(context, tmp, "Old", 0);
	return 0;
}

#endif

static void run_externnotify(char *context, char *extension)
{
	char arguments[255];
	char ext_context[256] = "";
	int newvoicemails = 0, oldvoicemails = 0;
	struct ast_smdi_mwi_message *mwi_msg;

	if (!ast_strlen_zero(context))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", extension, context);
	else
		ast_copy_string(ext_context, extension, sizeof(ext_context));

	if (!strcasecmp(externnotify, "smdi")) {
		if (ast_app_has_voicemail(ext_context, NULL)) 
			ast_smdi_mwi_set(smdi_iface, extension);
		else
			ast_smdi_mwi_unset(smdi_iface, extension);

		if ((mwi_msg = ast_smdi_mwi_message_wait_station(smdi_iface, SMDI_MWI_WAIT_TIMEOUT, extension))) {
			ast_log(LOG_ERROR, "Error executing SMDI MWI change for %s\n", extension);
			if (!strncmp(mwi_msg->cause, "INV", 3))
				ast_log(LOG_ERROR, "Invalid MWI extension: %s\n", mwi_msg->fwd_st);
			else if (!strncmp(mwi_msg->cause, "BLK", 3))
				ast_log(LOG_WARNING, "MWI light was already on or off for %s\n", mwi_msg->fwd_st);
			ast_log(LOG_WARNING, "The switch reported '%s'\n", mwi_msg->cause);
			ASTOBJ_UNREF(mwi_msg, ast_smdi_mwi_message_destroy);
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "Successfully executed SMDI MWI change for %s\n", extension);
		}
	} else if (!ast_strlen_zero(externnotify)) {
		if (inboxcount(ext_context, &newvoicemails, &oldvoicemails)) {
			ast_log(LOG_ERROR, "Problem in calculating number of voicemail messages available for extension %s\n", extension);
		} else {
			snprintf(arguments, sizeof(arguments), "%s %s %s %d&", externnotify, context, extension, newvoicemails);
			if (option_debug)
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
#ifdef IMAP_STORAGE
	int newmsgs, oldmsgs;
#endif
	struct vm_state *vms = NULL;
	char txtfile[PATH_MAX], tmptxtfile[PATH_MAX];
	char callerid[256];
	FILE *txt;
	char date[50];
	int txtdes;
	int res = 0;
	int msgnum;
	int duration = 0;
	int ausemacro = 0;
	int ousemacro = 0;
	int ouseexten = 0;
	char dir[PATH_MAX], tmpdir[PATH_MAX];
	char dest[PATH_MAX];
	char fn[PATH_MAX];
	char prefile[PATH_MAX] = "";
	char tempfile[PATH_MAX] = "";
	char ext_context[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2] = "";
	char fmt[80];
	char *context;
	char ecodes[16] = "#";
	char tmp[1324] = "", *tmpptr;
	struct ast_vm_user *vmu;
	struct ast_vm_user svm;
	const char *category = NULL;

	if (strlen(ext) > sizeof(tmp) - 1) {
		ast_log(LOG_WARNING, "List of extensions is too long (>%ld).  Truncating.\n", (long) sizeof(tmp) - 1);
	}
	ast_copy_string(tmp, ext, sizeof(tmp));
	ext = tmp;
	context = strchr(tmp, '@');
	if (context) {
		*context++ = '\0';
		tmpptr = strchr(context, '&');
	} else {
		tmpptr = strchr(ext, '&');
	}

	if (tmpptr)
		*tmpptr++ = '\0';

	category = pbx_builtin_getvar_helper(chan, "VM_CATEGORY");

	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Before find_user\n");
	if (!(vmu = find_user(&svm, context, ext))) {
		ast_log(LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
		if (ast_test_flag(options, OPT_PRIORITY_JUMP) || ast_opt_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		return res;
	}
	/* Setup pre-file if appropriate */
	if (strcmp(vmu->context, "default"))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", ext, vmu->context);
	else
		ast_copy_string(ext_context, vmu->mailbox, sizeof(ext_context));
	if (ast_test_flag(options, OPT_BUSY_GREETING)) {
		res = create_dirpath(dest, sizeof(dest), vmu->context, ext, "busy");
		snprintf(prefile, sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, ext);
	} else if (ast_test_flag(options, OPT_UNAVAIL_GREETING)) {
		res = create_dirpath(dest, sizeof(dest), vmu->context, ext, "unavail");
		snprintf(prefile, sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, ext);
	}
	snprintf(tempfile, sizeof(tempfile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, ext);
	if ((res = create_dirpath(dest, sizeof(dest), vmu->context, ext, "temp"))) {
		ast_log(LOG_WARNING, "Failed to make directory (%s)\n", tempfile);
		return -1;
	}
	RETRIEVE(tempfile, -1, vmu);
	if (ast_fileexists(tempfile, NULL, NULL) > 0)
		ast_copy_string(prefile, tempfile, sizeof(prefile));
	DISPOSE(tempfile, -1);
	/* It's easier just to try to make it than to check for its existence */
#ifndef IMAP_STORAGE
	create_dirpath(dir, sizeof(dir), vmu->context, ext, "INBOX");
#else
	snprintf(dir, sizeof(dir), "%simap", VM_SPOOL_DIR);
	if (mkdir(dir, VOICEMAIL_DIR_MODE) && errno != EEXIST) {
		ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
	}
#endif
	create_dirpath(tmpdir, sizeof(tmpdir), vmu->context, ext, "tmp");

	/* Check current or macro-calling context for special extensions */
	if (ast_test_flag(vmu, VM_OPERATOR)) {
		if (!ast_strlen_zero(vmu->exit)) {
			if (ast_exists_extension(chan, vmu->exit, "o", 1, chan->cid.cid_num)) {
				strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
				ouseexten = 1;
			}
		} else if (ast_exists_extension(chan, chan->context, "o", 1, chan->cid.cid_num)) {
			strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
			ouseexten = 1;
		}
		else if (!ast_strlen_zero(chan->macrocontext) && ast_exists_extension(chan, chan->macrocontext, "o", 1, chan->cid.cid_num)) {
		strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
		ousemacro = 1;
		}
	}

	if (!ast_strlen_zero(vmu->exit)) {
		if (ast_exists_extension(chan, vmu->exit, "a", 1, chan->cid.cid_num))
			strncat(ecodes, "*", sizeof(ecodes) - strlen(ecodes) - 1);
	} else if (ast_exists_extension(chan, chan->context, "a", 1, chan->cid.cid_num))
		strncat(ecodes, "*", sizeof(ecodes) - strlen(ecodes) - 1);
	else if (!ast_strlen_zero(chan->macrocontext) && ast_exists_extension(chan, chan->macrocontext, "a", 1, chan->cid.cid_num)) {
		strncat(ecodes, "*", sizeof(ecodes) - strlen(ecodes) - 1);
		ausemacro = 1;
	}

	/* Play the beginning intro if desired */
	if (!ast_strlen_zero(prefile)) {
		res = play_greeting(chan, vmu, prefile, ecodes);
		if (res == -2) {
			/* The file did not exist */
			if (option_debug)
				ast_log(LOG_DEBUG, "%s doesn't exist, doing what we can\n", prefile);
			res = invent_message(chan, vmu, ext, ast_test_flag(options, OPT_BUSY_GREETING), ecodes);
		}
		if (res < 0) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Hang up during prefile playback\n");
			free_user(vmu);
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
			return -1;
		}
	}
	if (res == '#') {
		/* On a '#' we skip the instructions */
		ast_set_flag(options, OPT_SILENT);
		res = 0;
	}
	if (!res && !ast_test_flag(options, OPT_SILENT)) {
		res = ast_stream_and_wait(chan, INTRO, chan->language, ecodes);
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
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "USEREXIT");
		return 0;
	}

	/* Check for a '0' here */
	if (res == '0') {
	transfer:
		if (ouseexten || ousemacro) {
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
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "USEREXIT");
		}
		return OPERATOR_EXIT;
	}
	if (res < 0) {
		free_user(vmu);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		return -1;
	}
	/* The meat of recording the message...  All the announcements and beeps have been played*/
	ast_copy_string(fmt, vmfmts, sizeof(fmt));
	if (!ast_strlen_zero(fmt)) {
		msgnum = 0;

#ifdef IMAP_STORAGE
		/* Is ext a mailbox? */
		/* must open stream for this user to get info! */
		res = inboxcount(ext_context, &newmsgs, &oldmsgs);
		if (res < 0) {
			ast_log(LOG_NOTICE,"Can not leave voicemail, unable to count messages\n");
			return -1;
		}
		if (!(vms = get_vm_state_by_mailbox(ext, context, 0))) {
		/*It is possible under certain circumstances that inboxcount did not create a vm_state when it was needed. This is a catchall which will
		 * rarely be used*/
			if (!(vms = create_vm_state_from_user(vmu))) {
				ast_log(LOG_ERROR, "Couldn't allocate necessary space\n");
				return -1;
			}
		}
		vms->newmessages++;
		/* here is a big difference! We add one to it later */
		msgnum = newmsgs + oldmsgs;
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Messagecount set to %d\n",msgnum);
		snprintf(fn, sizeof(fn), "%simap/msg%s%04d", VM_SPOOL_DIR, vmu->mailbox, msgnum);
		/* set variable for compatability */
		pbx_builtin_setvar_helper(chan, "VM_MESSAGEFILE", "IMAP_STORAGE");

		if (imap_check_limits(chan, vms, vmu, msgnum)) {
			goto leave_vm_out;
		}
#else
		if (count_messages(vmu, dir) >= vmu->maxmsg - inprocess_count(vmu->mailbox, vmu->context, +1)) {
			res = ast_streamfile(chan, "vm-mailboxfull", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			ast_log(LOG_WARNING, "No more messages possible\n");
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
			inprocess_count(vmu->mailbox, vmu->context, -1);
			goto leave_vm_out;
		}

#endif
		snprintf(tmptxtfile, sizeof(tmptxtfile), "%s/XXXXXX", tmpdir);
		txtdes = mkstemp(tmptxtfile);
		chmod(tmptxtfile, VOICEMAIL_FILE_MODE & ~my_umask);
		if (txtdes < 0) {
			res = ast_streamfile(chan, "vm-mailboxfull", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			ast_log(LOG_ERROR, "Unable to create message file: %s\n", strerror(errno));
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
			inprocess_count(vmu->mailbox, vmu->context, -1);
			goto leave_vm_out;
		}

		/* Now play the beep once we have the message number for our next message. */
		if (res >= 0) {
			/* Unless we're *really* silent, try to send the beep */
			res = ast_stream_and_wait(chan, "beep", chan->language, "");
		}
				
		/* Store information */
		txt = fdopen(txtdes, "w+");
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
				ast_callerid_merge(callerid, sizeof(callerid), S_OR(chan->cid.cid_name, NULL), S_OR(chan->cid.cid_num, NULL), "Unknown"),
				date, (long)time(NULL),
				category ? category : ""); 
		} else
			ast_log(LOG_WARNING, "Error opening text file for output\n");
		res = play_record_review(chan, NULL, tmptxtfile, vmmaxmessage, fmt, 1, vmu, &duration, NULL, options->record_gain, vms);

		if (txt) {
			if (duration < vmminmessage) {
				fclose(txt);
				if (option_verbose > 2) 
					ast_verbose( VERBOSE_PREFIX_3 "Recording was %d seconds long but needs to be at least %d - abandoning\n", duration, vmminmessage);
				ast_filedelete(tmptxtfile, NULL);
				unlink(tmptxtfile);
				inprocess_count(vmu->mailbox, vmu->context, -1);
			} else {
				fprintf(txt, "duration=%d\n", duration);
				fclose(txt);
				if (vm_lock_path(dir)) {
					ast_log(LOG_ERROR, "Couldn't lock directory %s.  Voicemail will be lost.\n", dir);
					/* Delete files */
					ast_filedelete(tmptxtfile, NULL);
					unlink(tmptxtfile);
					inprocess_count(vmu->mailbox, vmu->context, -1);
				} else if (ast_fileexists(tmptxtfile, NULL, NULL) <= 0) {
					if (option_debug) 
						ast_log(LOG_DEBUG, "The recorded media file is gone, so we should remove the .txt file too!\n");
					unlink(tmptxtfile);
					ast_unlock_path(dir);
					inprocess_count(vmu->mailbox, vmu->context, -1);
				} else {
#ifndef IMAP_STORAGE
					msgnum = last_message_index(vmu, dir) + 1;
#endif
					make_file(fn, sizeof(fn), dir, msgnum);

					/* assign a variable with the name of the voicemail file */ 
#ifndef IMAP_STORAGE
					pbx_builtin_setvar_helper(chan, "VM_MESSAGEFILE", fn);
#else
					pbx_builtin_setvar_helper(chan, "VM_MESSAGEFILE", "IMAP_STORAGE");
#endif

					snprintf(txtfile, sizeof(txtfile), "%s.txt", fn);
					ast_filerename(tmptxtfile, fn, NULL);
					rename(tmptxtfile, txtfile);
					inprocess_count(vmu->mailbox, vmu->context, -1);

					ast_unlock_path(dir);
					/* We must store the file first, before copying the message, because
					 * ODBC storage does the entire copy with SQL.
					 */
					if (ast_fileexists(fn, NULL, NULL) > 0) {
						STORE(dir, vmu->mailbox, vmu->context, msgnum, chan, vmu, fmt, duration, vms);
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
							copy_message(chan, vmu, 0, msgnum, duration, recip, fmt, dir);
							free_user(recip);
						}
					}
					/* Notification and disposal needs to happen after the copy, though. */
					if (ast_fileexists(fn, NULL, NULL)) {
						notify_new_message(chan, vmu, msgnum, duration, fmt, S_OR(chan->cid.cid_num, NULL), S_OR(chan->cid.cid_name, NULL));
						DISPOSE(dir, msgnum);
					}
				}
			}
		} else {
			inprocess_count(vmu->mailbox, vmu->context, -1);
		}
		if (res == '0') {
			goto transfer;
		} else if (res > 0 && res != 't')
			res = 0;

		if (duration < vmminmessage)
			/* XXX We should really give a prompt too short/option start again, with leave_vm_out called only after a timeout XXX */
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		else
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "SUCCESS");
	} else
		ast_log(LOG_WARNING, "No format for saving voicemail?\n");
leave_vm_out:
	free_user(vmu);
	
	return res;
}

#if !defined(IMAP_STORAGE)
static int resequence_mailbox(struct ast_vm_user *vmu, char *dir, int stopcount)
{
	/* we know the actual number of messages, so stop process when number is hit */

	int x,dest;
	char sfn[PATH_MAX];
	char dfn[PATH_MAX];

	if (vm_lock_path(dir))
		return ERROR_LOCK_PATH;

	for (x = 0, dest = 0; dest != stopcount && x < vmu->maxmsg + 10; x++) {
		make_file(sfn, sizeof(sfn), dir, x);
		if (EXISTS(dir, x, sfn, NULL)) {
			
			if (x != dest) {
				make_file(dfn, sizeof(dfn), dir, dest);
				RENAME(dir, x, vmu->mailbox, vmu->context, dir, dest, sfn, dfn);
			}
			
			dest++;
		}
	}
	ast_unlock_path(dir);

	return dest;
}
#endif

static int say_and_wait(struct ast_channel *chan, int num, const char *language)
{
	int d;
	d = ast_say_number(chan, num, AST_DIGIT_ANY, language, (char *) NULL);
	return d;
}

static int save_to_folder(struct ast_vm_user *vmu, struct vm_state *vms, int msg, int box)
{
#ifdef IMAP_STORAGE
	/* we must use mbox(x) folder names, and copy the message there */
	/* simple. huh? */
	char sequence[10];
	/* get the real IMAP message number for this message */
	snprintf(sequence, sizeof(sequence), "%ld", vms->msgArray[msg]);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Copying sequence %s to mailbox %s\n",sequence,(char *) mbox(box));
	ast_mutex_lock(&vms->lock);
	if (box == 1) {
		mail_setflag(vms->mailstream, sequence, "\\Seen");
	} else if (box == 0) {
		mail_clearflag(vms->mailstream, sequence, "\\Seen");
	}
	if (!strcasecmp(mbox(0), vms->curbox) && (box == 0 || box == 1)) {
		ast_mutex_unlock(&vms->lock);
		return 0;
	} else {
		int res = !mail_copy(vms->mailstream,sequence,(char *) mbox(box)); 
		ast_mutex_unlock(&vms->lock);
		return res;
	}
#else
	char *dir = vms->curdir;
	char *username = vms->username;
	char *context = vmu->context;
	char sfn[PATH_MAX];
	char dfn[PATH_MAX];
	char ddir[PATH_MAX];
	const char *dbox = mbox(box);
	int x;
	make_file(sfn, sizeof(sfn), dir, msg);
	create_dirpath(ddir, sizeof(ddir), context, username, dbox);

	if (vm_lock_path(ddir))
		return ERROR_LOCK_PATH;

	for (x = 0; x < vmu->maxmsg; x++) {
		make_file(dfn, sizeof(dfn), ddir, x);
		if (!EXISTS(ddir, x, dfn, NULL))
			break;
	}
	if (x >= vmu->maxmsg) {
		ast_unlock_path(ddir);
		return ERROR_MAILBOX_FULL;
	}
	if (strcmp(sfn, dfn)) {
		COPY(dir, msg, ddir, x, username, context, sfn, dfn);
	}
	ast_unlock_path(ddir);
#endif
	return 0;
}

static int adsi_logo(unsigned char *buf)
{
	int bytes = 0;
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_CENT, 0, "Comedian Mail", "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_CENT, 0, "(C)2002-2006 Digium, Inc.", "");
	return bytes;
}

static int adsi_load_vmail(struct ast_channel *chan, int *useadsi)
{
	unsigned char buf[256];
	int bytes=0;
	int x;
	char num[5];

	*useadsi = 0;
	bytes += ast_adsi_data_mode(buf + bytes);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	bytes = 0;
	bytes += adsi_logo(buf);
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Downloading Scripts", "");
#ifdef DISPLAY
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   .", "");
#endif
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_data_mode(buf + bytes);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);

	if (ast_adsi_begin_download(chan, addesc, adsifdn, adsisec, adsiver)) {
		bytes = 0;
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Load Cancelled.", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "ADSI Unavailable", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
		return 0;
	}

#ifdef DISPLAY
	/* Add a dot */
	bytes = 0;
	bytes += ast_adsi_logo(buf);
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Downloading Scripts", "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ..", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif
	bytes = 0;
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 0, "Listen", "Listen", "1", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 1, "Folder", "Folder", "2", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 2, "Advanced", "Advnced", "3", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 3, "Options", "Options", "0", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 4, "Help", "Help", "*", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 5, "Exit", "Exit", "#", 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ...", "");
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif

	bytes = 0;
	/* These buttons we load but don't use yet */
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 6, "Previous", "Prev", "4", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 8, "Repeat", "Repeat", "5", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 7, "Delete", "Delete", "7", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 9, "Next", "Next", "6", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 10, "Save", "Save", "9", 1);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 11, "Undelete", "Restore", "7", 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   ....", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif

	bytes = 0;
	for (x=0;x<5;x++) {
		snprintf(num, sizeof(num), "%d", x);
		bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 12 + x, mbox(x), mbox(x), num, 1);
	}
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 12 + 5, "Cancel", "Cancel", "#", 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

#ifdef DISPLAY
	/* Add another dot */
	bytes = 0;
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, "   .....", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
#endif

	if (ast_adsi_end_download(chan)) {
		bytes = 0;
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Download Unsuccessful.", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "ADSI Unavailable", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
		return 0;
	}
	bytes = 0;
	bytes += ast_adsi_download_disconnect(buf + bytes);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DOWNLOAD);

	if (option_debug)
		ast_log(LOG_DEBUG, "Done downloading scripts...\n");

#ifdef DISPLAY
	/* Add last dot */
	bytes = 0;
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "   ......", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
#endif
	if (option_debug)
		ast_log(LOG_DEBUG, "Restarting session...\n");

	bytes = 0;
	/* Load the session now */
	if (ast_adsi_load_session(chan, adsifdn, adsiver, 1) == 1) {
		*useadsi = 1;
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Scripts Loaded!", "");
	} else
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Load Failed!", "");

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	return 0;
}

static void adsi_begin(struct ast_channel *chan, int *useadsi)
{
	int x;
	if (!ast_adsi_available(chan))
		return;
	x = ast_adsi_load_session(chan, adsifdn, adsiver, 1);
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
	if (!ast_adsi_available(chan))
		return;

	for (x=0;x<8;x++)
		keys[x] = 0;
	/* Set one key for next */
	keys[3] = ADSI_KEY_APPS + 3;

	bytes += adsi_logo(buf + bytes);
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, " ", "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, " ", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_input_format(buf + bytes, 1, ADSI_DIR_FROM_LEFT, 0, "Mailbox: ******", "");
	bytes += ast_adsi_input_control(buf + bytes, ADSI_COMM_PAGE, 4, 1, 1, ADSI_JUST_LEFT);
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 3, "Enter", "Enter", "#", 1);
	bytes += ast_adsi_set_keys(buf + bytes, keys);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_password(struct ast_channel *chan)
{
	unsigned char buf[256];
	int bytes=0;
	unsigned char keys[8];
	int x;
	if (!ast_adsi_available(chan))
		return;

	for (x=0;x<8;x++)
		keys[x] = 0;
	/* Set one key for next */
	keys[3] = ADSI_KEY_APPS + 3;

	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_input_format(buf + bytes, 1, ADSI_DIR_FROM_LEFT, 0, "Password: ******", "");
	bytes += ast_adsi_input_control(buf + bytes, ADSI_COMM_PAGE, 4, 0, 1, ADSI_JUST_LEFT);
	bytes += ast_adsi_set_keys(buf + bytes, keys);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);
	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_folders(struct ast_channel *chan, int start, char *label)
{
	unsigned char buf[256];
	int bytes=0;
	unsigned char keys[8];
	int x,y;

	if (!ast_adsi_available(chan))
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

	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_CENT, 0, label, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_CENT, 0, " ", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_set_keys(buf + bytes, keys);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_message(struct ast_channel *chan, struct vm_state *vms)
{
	int bytes=0;
	unsigned char buf[256]; 
	char buf1[256], buf2[256];
	char fn2[PATH_MAX];

	char cid[256]="";
	char *val;
	char *name, *num;
	char datetime[21]="";
	FILE *f;

	unsigned char keys[8];

	int x;

	if (!ast_adsi_available(chan))
		return;

	/* Retrieve important info */
	snprintf(fn2, sizeof(fn2), "%s.txt", vms->fn);
	f = fopen(fn2, "r");
	if (f) {
		while (!feof(f)) {	
			if (!fgets((char *)buf, sizeof(buf), f)) {
				continue;
			}
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
			bytes += ast_adsi_voice_mode(buf + bytes, 0);

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

	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, name, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_LEFT, 0, datetime, "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_set_keys(buf + bytes, keys);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_delete(struct ast_channel *chan, struct vm_state *vms)
{
	int bytes=0;
	unsigned char buf[256];
	unsigned char keys[8];

	int x;

	if (!ast_adsi_available(chan))
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
	bytes += ast_adsi_set_keys(buf + bytes, keys);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
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
	if (!ast_adsi_available(chan))
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
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);

	for (x=0;x<6;x++)
		keys[x] = ADSI_KEY_SKT | (ADSI_KEY_APPS + x);
	keys[6] = 0;
	keys[7] = 0;

	/* Don't let them listen if there are none */
	if (vms->lastmsg < 0)
		keys[0] = 1;
	bytes += ast_adsi_set_keys(buf + bytes, keys);

	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

static void adsi_status2(struct ast_channel *chan, struct vm_state *vms)
{
	unsigned char buf[256] = "";
	char buf1[256] = "", buf2[256] = "";
	int bytes=0;
	unsigned char keys[8];
	int x;

	char *mess = (vms->lastmsg == 0) ? "message" : "messages";

	if (!ast_adsi_available(chan))
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
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 1, ADSI_JUST_LEFT, 0, buf1, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 2, ADSI_JUST_LEFT, 0, buf2, "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, "", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_set_keys(buf + bytes, keys);

	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	
}

/*
static void adsi_clear(struct ast_channel *chan)
{
	char buf[256];
	int bytes=0;
	if (!ast_adsi_available(chan))
		return;
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}
*/

static void adsi_goodbye(struct ast_channel *chan)
{
	unsigned char buf[256];
	int bytes=0;

	if (!ast_adsi_available(chan))
		return;
	bytes += adsi_logo(buf + bytes);
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, " ", "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Goodbye", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

/*--- get_folder: Folder menu ---*/
/* Plays "press 1 for INBOX messages" etc
   Should possibly be internationalized
 */
static int get_folder(struct ast_channel *chan, int start)
{
	int x;
	int d;
	char fn[PATH_MAX];
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
	int loops = 0;
	res = ast_play_and_wait(chan, fn);	/* Folder name */
	while (((res < '0') || (res > '9')) &&
			(res != '#') && (res >= 0) &&
			loops < 4) {
		res = get_folder(chan, 0);
		loops++;
	}
	if (loops == 4) { /* give up */
		return '#';
	}
	return res;
}

static int vm_forwardoptions(struct ast_channel *chan, struct ast_vm_user *vmu, char *curdir, int curmsg, char *vmfmts,
			     char *context, signed char record_gain, long *duration, struct vm_state *vms)
{
	int cmd = 0;
	int retries = 0, prepend_duration = 0, already_recorded = 0;
	signed char zero_gain = 0;
	struct ast_config *msg_cfg;
	const char *duration_cstr;
	char msgfile[PATH_MAX], backup[PATH_MAX];
	char textfile[PATH_MAX];
	char backup_textfile[PATH_MAX];
	struct ast_category *msg_cat;
	char duration_str[12] = "";

	ast_log(LOG_NOTICE, "curdir=%s\n", curdir);
	/* Must always populate duration correctly */
	make_file(msgfile, sizeof(msgfile), curdir, curmsg);
	strcpy(textfile, msgfile);
	strcpy(backup, msgfile);
	strcpy(backup_textfile, msgfile);
	strncat(textfile, ".txt", sizeof(textfile) - strlen(textfile) - 1);
	strncat(backup, "-bak", sizeof(backup) - strlen(backup) - 1);
	strncat(backup_textfile, "-bak.txt", sizeof(backup_textfile) - strlen(backup_textfile) - 1);

	if (!(msg_cfg = ast_config_load(textfile))) {
		return -1;
	}

	*duration = 0;
	if ((duration_cstr = ast_variable_retrieve(msg_cfg, "message", "duration"))) {
		*duration = atoi(duration_cstr);
	}

	while ((cmd >= 0) && (cmd != 't') && (cmd != '*')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1': 
			/* prepend a message to the current message, update the metadata and return */
		{
			prepend_duration = 0;

			/* Back up the original file, so we can retry the prepend */
#ifndef IMAP_STORAGE
			if (already_recorded) {
				ast_filecopy(backup, msgfile, NULL);
				copy(textfile, backup_textfile);
			} else {
				ast_filecopy(msgfile, backup, NULL);
				copy(textfile, backup_textfile);
			}
#endif
			already_recorded = 1;

			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);

			cmd = ast_play_and_prepend(chan, NULL, msgfile, 0, vmfmts, &prepend_duration, 1, silencethreshold, maxsilence);
			if (cmd == 'S') {
				ast_filerename(backup, msgfile, NULL);
			}

			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &zero_gain, sizeof(zero_gain), 0);

			if (prepend_duration) {
				prepend_duration += *duration;
			}

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

	if (already_recorded && cmd == -1) {
		/* Restore original files, if operation cancelled */
		ast_filerename(backup, msgfile, NULL);
		if (duration_cstr) {
			ast_copy_string(duration_str, duration_cstr, sizeof(duration_str));
		}
	} else if (prepend_duration) {
		*duration = prepend_duration;
		snprintf(duration_str, sizeof(duration_str), "%d", prepend_duration);
	}

	msg_cat = ast_category_get(msg_cfg, "message");
	if (!ast_strlen_zero(duration_str) && !ast_variable_update(msg_cat, "duration", duration_str, NULL, 0)) {
		config_text_file_save(textfile, msg_cfg, "app_voicemail");
	}
	ast_config_destroy(msg_cfg);

	if (cmd == 't' || cmd == 'S')
		cmd = 0;
	return cmd;
}

static int notify_new_message(struct ast_channel *chan, struct ast_vm_user *vmu, int msgnum, long duration, char *fmt, char *cidnum, char *cidname)
{
	char todir[PATH_MAX], fn[PATH_MAX], ext_context[PATH_MAX], *stringp;
	int newmsgs = 0, oldmsgs = 0;
	const char *category = pbx_builtin_getvar_helper(chan, "VM_CATEGORY");

#ifndef IMAP_STORAGE
	make_dir(todir, sizeof(todir), vmu->context, vmu->mailbox, "INBOX");
#else
	snprintf(todir, sizeof(todir), "%simap", VM_SPOOL_DIR);
#endif
	make_file(fn, sizeof(fn), todir, msgnum);
	snprintf(ext_context, sizeof(ext_context), "%s@%s", vmu->mailbox, vmu->context);

	if (!ast_strlen_zero(vmu->attachfmt)) {
		if (strstr(fmt, vmu->attachfmt)) {
			fmt = vmu->attachfmt;
		} else {
			ast_log(LOG_WARNING, "Attachment format '%s' is not one of the recorded formats '%s'.  Falling back to default format for '%s@%s'.\n", vmu->attachfmt, fmt, vmu->mailbox, vmu->context);
		}
	}

	/* Attach only the first format */
	fmt = ast_strdupa(fmt);
	stringp = fmt;
	strsep(&stringp, "|");

	if (!ast_strlen_zero(vmu->email)) {
		int attach_user_voicemail = ast_test_flag((&globalflags), VM_ATTACH);
		char *myserveremail = serveremail;
		attach_user_voicemail = ast_test_flag(vmu, VM_ATTACH);
		if (!ast_strlen_zero(vmu->serveremail))
			myserveremail = vmu->serveremail;
		
		if (attach_user_voicemail)
			RETRIEVE(todir, msgnum, vmu);

		/*XXX possible imap issue, should category be NULL XXX*/
		sendmail(myserveremail, vmu, msgnum, vmu->context, vmu->mailbox, mbox(0), cidnum, cidname, fn, fmt, duration, attach_user_voicemail, chan, category);

		if (attach_user_voicemail)
			DISPOSE(todir, msgnum);
	}

	if (!ast_strlen_zero(vmu->pager)) {
		char *myserveremail = serveremail;
		if (!ast_strlen_zero(vmu->serveremail))
			myserveremail = vmu->serveremail;
		sendpage(myserveremail, vmu->pager, msgnum, vmu->context, vmu->mailbox, mbox(0), cidnum, cidname, duration, vmu, category);
	}

	if (ast_test_flag(vmu, VM_DELETE)) {
		DELETE(todir, msgnum, fn, vmu);
	}

	/* Leave voicemail for someone */
	if (ast_app_has_voicemail(ext_context, NULL)) {
		ast_app_inboxcount(ext_context, &newmsgs, &oldmsgs);
	}
	manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s@%s\r\nWaiting: %d\r\nNew: %d\r\nOld: %d\r\n", vmu->mailbox, vmu->context, ast_app_has_voicemail(ext_context, NULL), newmsgs, oldmsgs);
	run_externnotify(vmu->context, vmu->mailbox);
	return 0;
}

static int forward_message(struct ast_channel *chan, char *context, struct vm_state *vms, struct ast_vm_user *sender, char *fmt, int is_new_message, signed char record_gain)
{
#ifdef IMAP_STORAGE
	int todircount=0;
	struct vm_state *dstvms;
#else
	char textfile[PATH_MAX], backup[PATH_MAX], backup_textfile[PATH_MAX];
#endif
	char username[70]="";
	int res = 0, cmd = 0;
	struct ast_vm_user *receiver = NULL, *vmtmp;
	AST_LIST_HEAD_NOLOCK_STATIC(extensions, ast_vm_user);
	char *stringp;
	const char *s;
	int saved_messages = 0;
	int valid_extensions = 0;
	char *dir;
	int curmsg;
	int prompt_played = 0;

	if (vms == NULL) return -1;
	dir = vms->curdir;
	curmsg = vms->curmsg;
	
	while (!res && !valid_extensions) {
		int use_directory = 0;
		if (ast_test_flag((&globalflags), VM_DIRECFORWARD)) {
			int done = 0;
			int retries = 0;
			cmd=0;
			while ((cmd >= 0) && !done ){
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
			if (cmd < 0 || cmd == 't')
				break;
		}
		
		if (use_directory) {
			/* use app_directory */
			
			char old_context[sizeof(chan->context)];
			char old_exten[sizeof(chan->exten)];
			int old_priority;
			struct ast_app* app;

			
			app = pbx_findapp("Directory");
			if (app) {
				char vmcontext[256];
				/* make backup copies */
				memcpy(old_context, chan->context, sizeof(chan->context));
				memcpy(old_exten, chan->exten, sizeof(chan->exten));
				old_priority = chan->priority;
				
				/* call the the Directory, changes the channel */
				snprintf(vmcontext, sizeof(vmcontext), "%s||v", context ? context : "default");
				res = pbx_exec(chan, app, vmcontext);
				
				ast_copy_string(username, chan->exten, sizeof(username));
				
				/* restore the old context, exten, and priority */
				memcpy(chan->context, old_context, sizeof(chan->context));
				memcpy(chan->exten, old_exten, sizeof(chan->exten));
				chan->priority = old_priority;
				
			} else {
				ast_log(LOG_WARNING, "Could not find the Directory application, disabling directory_forward\n");
				ast_clear_flag((&globalflags), VM_DIRECFORWARD);	
			}
		} else {
			/* Ask for an extension */
			res = ast_streamfile(chan, "vm-extension", chan->language);	/* "extension" */
			prompt_played++;
			if (res || prompt_played > 4)
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
			/* Don't forward to ourselves but allow leaving a message for ourselves (is_new_message == 1).  find_user is going to malloc since we have a NULL as first argument */
			if ((is_new_message == 1 || strcmp(s,sender->mailbox)) && (receiver = find_user(NULL, context, s))) {
				int oldmsgs;
				int newmsgs;
				int capacity;
				if (inboxcount(s, &newmsgs, &oldmsgs)) {
					ast_log(LOG_ERROR, "Problem in calculating number of voicemail messages available for extension %s\n", s);
					/* Shouldn't happen, but allow trying another extension if it does */
					res = ast_play_and_wait(chan, "pbx-invalid");
					valid_extensions = 0;
					break;
				}
				capacity = receiver->maxmsg - inprocess_count(receiver->mailbox, receiver->context, +1);
				if ((newmsgs + oldmsgs) >= capacity) {
					ast_log(LOG_NOTICE, "Mailbox '%s' is full with capacity of %d, prompting for another extension.\n", s, capacity);
					res = ast_play_and_wait(chan, "vm-mailboxfull");
					valid_extensions = 0;
					while ((vmtmp = AST_LIST_REMOVE_HEAD(&extensions, list))) {
						inprocess_count(vmtmp->mailbox, vmtmp->context, -1);
						free_user(vmtmp);
					}
					inprocess_count(receiver->mailbox, receiver->context, -1);
					break;
				}
				AST_LIST_INSERT_HEAD(&extensions, receiver, list);
			} else {
				ast_log(LOG_NOTICE, "'%s' is not a valid mailbox\n", s);
				/* "I am sorry, that's not a valid extension.  Please try again." */
				res = ast_play_and_wait(chan, "pbx-invalid");
				valid_extensions = 0;
				break;
			}
			s = strsep(&stringp, "*");
		}
		/* break from the loop of reading the extensions */
		if (valid_extensions)
			break;
	}
	/* check if we're clear to proceed */
	if (AST_LIST_EMPTY(&extensions) || !valid_extensions)
		return res;
	if (is_new_message == 1) {
		struct leave_vm_options leave_options;
		char mailbox[AST_MAX_EXTENSION * 2 + 2];
		/* Make sure that context doesn't get set as a literal "(null)" (or else find_user won't find it) */
		if (context)
			snprintf(mailbox, sizeof(mailbox), "%s@%s", username, context);
		else
			ast_copy_string(mailbox, username, sizeof(mailbox));

		/* Send VoiceMail */
		memset(&leave_options, 0, sizeof(leave_options));
		leave_options.record_gain = record_gain;
		cmd = leave_voicemail(chan, mailbox, &leave_options);
	} else {
		/* Forward VoiceMail */
		long duration = 0;
		struct vm_state vmstmp;
#ifndef IMAP_STORAGE
		char msgfile[PATH_MAX];
#endif
		int copy_msg_result = 0;

		memcpy(&vmstmp, vms, sizeof(vmstmp));

		RETRIEVE(dir, curmsg, sender);

		cmd = vm_forwardoptions(chan, sender, vmstmp.curdir, curmsg, vmfmts, S_OR(context, "default"), record_gain, &duration, &vmstmp);
		if (!cmd) {
			AST_LIST_TRAVERSE_SAFE_BEGIN(&extensions, vmtmp, list) {
#ifdef IMAP_STORAGE
				char *myserveremail;
				int attach_user_voicemail;
				/* get destination mailbox */
				dstvms = get_vm_state_by_mailbox(vmtmp->mailbox, vmtmp->context, 0);
				if (!dstvms) {
					dstvms = create_vm_state_from_user(vmtmp);
				}
				if (dstvms) {
					init_mailstream(dstvms, 0);
					if (!dstvms->mailstream) {
						ast_log (LOG_ERROR,"IMAP mailstream for %s is NULL\n",vmtmp->mailbox);
					} else {
						copy_msg_result = STORE(vmstmp.curdir, vmtmp->mailbox, vmtmp->context, dstvms->curmsg, chan, vmtmp, fmt, duration, dstvms);
						run_externnotify(vmtmp->context, vmtmp->mailbox); 
					}
				} else {
					ast_log (LOG_ERROR,"Could not find state information for mailbox %s\n",vmtmp->mailbox);
				}
				myserveremail = serveremail;
				if (!ast_strlen_zero(vmtmp->serveremail))
					myserveremail = vmtmp->serveremail;
				attach_user_voicemail = ast_test_flag(vmtmp, VM_ATTACH);
				/* NULL category for IMAP storage */
				sendmail(myserveremail, vmtmp, todircount, vmtmp->context, vmtmp->mailbox, dstvms->curbox, S_OR(chan->cid.cid_num, NULL), S_OR(chan->cid.cid_name, NULL), vms->fn, fmt, duration, attach_user_voicemail, chan, NULL);
#else
				copy_msg_result = copy_message(chan, sender, 0, curmsg, duration, vmtmp, fmt, dir);
#endif
				saved_messages++;
				inprocess_count(vmtmp->mailbox, vmtmp->context, -1);
				AST_LIST_REMOVE_CURRENT(&extensions, list);
				free_user(vmtmp);
				if (res)
					break;
			}
			AST_LIST_TRAVERSE_SAFE_END;
			if (saved_messages > 0 && !copy_msg_result) {
				/* give confirmation that the message was saved */
				/* commented out since we can't forward batches yet
				if (saved_messages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
				if (!res)
					res = ast_play_and_wait(chan, "vm-saved"); */
				res = ast_play_and_wait(chan, "vm-msgsaved");
			}
#ifndef IMAP_STORAGE
			else {
				/* with IMAP, mailbox full warning played by imap_check_limits */
				res = ast_play_and_wait(chan, "vm-mailboxfull");
			}
			/* Restore original message without prepended message if backup exists */
			make_file(msgfile, sizeof(msgfile), dir, curmsg);
			strcpy(textfile, msgfile);
			strcpy(backup, msgfile);
			strcpy(backup_textfile, msgfile);
			strncat(textfile, ".txt", sizeof(textfile) - strlen(textfile) - 1);
			strncat(backup, "-bak", sizeof(backup) - strlen(backup) - 1);
			strncat(backup_textfile, "-bak.txt", sizeof(backup_textfile) - strlen(backup_textfile) - 1);
			if (ast_fileexists(backup, NULL, NULL) > 0) {
				ast_filerename(backup, msgfile, NULL);
				rename(backup_textfile, textfile);
			}
#endif
		}
		DISPOSE(dir, curmsg);
#ifndef IMAP_STORAGE
		if (cmd) { /* assuming hangup, cleanup backup file */
			make_file(msgfile, sizeof(msgfile), dir, curmsg);
			strcpy(textfile, msgfile);
			strcpy(backup_textfile, msgfile);
			strncat(textfile, ".txt", sizeof(textfile) - strlen(textfile) - 1);
			strncat(backup_textfile, "-bak.txt", sizeof(backup_textfile) - strlen(backup_textfile) - 1);
			rename(backup_textfile, textfile);
		}
#endif
	}

	/* If anything failed above, we still have this list to free */
	while ((vmtmp = AST_LIST_REMOVE_HEAD(&extensions, list))) {
		inprocess_count(vmtmp->mailbox, vmtmp->context, -1);
		free_user(vmtmp);
	}
	return res ? res : cmd;
}

static int wait_file2(struct ast_channel *chan, struct vm_state *vms, char *file)
{
	int res;
	if ((res = ast_stream_and_wait(chan, file, chan->language, AST_DIGIT_ANY)) < 0) 
		ast_log(LOG_WARNING, "Unable to play message %s\n", file); 
	return res;
}

static int wait_file(struct ast_channel *chan, struct vm_state *vms, char *file) 
{
	return ast_control_streamfile(chan, file, "#", "*", "1456789", "0", "2", skipms);
}

static int play_message_category(struct ast_channel *chan, const char *category)
{
	int res = 0;

	if (!ast_strlen_zero(category))
		res = ast_play_and_wait(chan, category);

	if (res) {
		ast_log(LOG_WARNING, "No sound file for category '%s' was found.\n", category);
		res = 0;
	}

	return res;
}

static int play_message_datetime(struct ast_channel *chan, struct ast_vm_user *vmu, const char *origtime, const char *filename)
{
	int res = 0;
	struct vm_zone *the_zone = NULL;
	time_t t;

	if (ast_get_time_t(origtime, &t, 0, NULL)) {
		ast_log(LOG_WARNING, "Couldn't find origtime in %s\n", filename);
		return 0;
	}

	/* Does this user have a timezone specified? */
	if (!ast_strlen_zero(vmu->zonetag)) {
		/* Find the zone in the list */
		struct vm_zone *z;
		AST_LIST_LOCK(&zones);
		AST_LIST_TRAVERSE(&zones, z, list) {
			if (!strcmp(z->name, vmu->zonetag)) {
				the_zone = z;
				break;
			}
		}
		AST_LIST_UNLOCK(&zones);
	}

/* No internal variable parsing for now, so we'll comment it out for the time being */
#if 0
	/* Set the DIFF_* variables */
	ast_localtime(&t, &time_now, NULL);
	tv_now = ast_tvnow();
	tnow = tv_now.tv_sec;
	ast_localtime(&tnow, &time_then, NULL);

	/* Day difference */
	if (time_now.tm_year == time_then.tm_year)
		snprintf(temp,sizeof(temp),"%d",time_now.tm_yday);
	else
		snprintf(temp,sizeof(temp),"%d",(time_now.tm_year - time_then.tm_year) * 365 + (time_now.tm_yday - time_then.tm_yday));
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", temp);

	/* Can't think of how other diffs might be helpful, but I'm sure somebody will think of something. */
#endif
	if (the_zone) {
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, the_zone->msg_format, the_zone->timezone);
	} else if (!strncasecmp(chan->language, "de", 2)) {    /* GERMAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Q 'digits/at' HM", NULL);
	} else if (!strncasecmp(chan->language, "gr", 2)) {    /* GREEK syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q  H 'digits/kai' M ", NULL);
	} else if (!strncasecmp(chan->language, "he", 2)) {    /* HEBREW syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Ad 'at2' kM", NULL);
	} else if (!strncasecmp(chan->language, "it", 2)) {    /* ITALIAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q 'digits/at' 'digits/hours' k 'digits/e' M 'digits/minutes'", NULL);
	} else if (!strncasecmp(chan->language, "nl", 2)) {    /* DUTCH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q 'digits/nl-om' HM", NULL);
	} else if (!strncasecmp(chan->language, "no", 2)) {    /* NORWEGIAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Q 'digits/at' HM", NULL);
	} else if (!strncasecmp(chan->language, "pl", 2)) {    /* POLISH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Q HM", NULL);
	} else if (!strncasecmp(chan->language, "pt_BR", 5)) { /* PORTUGUESE syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' Ad 'digits/pt-de' B 'digits/pt-de' Y 'digits/pt-as' HM ", NULL);
	} else if (!strncasecmp(chan->language, "se", 2)) {    /* SWEDISH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' dB 'digits/at' k 'and' M", NULL);
	} else {
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, chan->language, "'vm-received' q 'digits/at' IMp", NULL);
	}
#if 0
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", NULL);
#endif
	return res;
}



static int play_message_callerid(struct ast_channel *chan, struct vm_state *vms, char *cid, const char *context, int callback)
{
	int res = 0;
	int i;
	char *callerid, *name;
	char prefile[PATH_MAX] = "";
	

	/* If voicemail cid is not enabled, or we didn't get cid or context from the attribute file, leave now. */
	/* BB: Still need to change this so that if this function is called by the message envelope (and someone is explicitly requesting to hear the CID), it does not check to see if CID is enabled in the config file */
	if ((cid == NULL)||(context == NULL))
		return res;

	/* Strip off caller ID number from name */
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "VM-CID: composite caller ID received: %s, context: %s\n", cid, context);
	ast_callerid_parse(cid, &name, &callerid);
	if ((!ast_strlen_zero(callerid)) && strcmp(callerid, "Unknown")) {
		/* Check for internal contexts and only */
		/* say extension when the call didn't come from an internal context in the list */
		for (i = 0 ; i < MAX_NUM_CID_CONTEXTS ; i++){
			if (option_debug > 2)
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
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Playing envelope info: CID number '%s' matches mailbox number, playing recorded name\n", callerid);
						if (!callback)
							res = wait_file2(chan, vms, "vm-from");
						res = ast_stream_and_wait(chan, prefile, chan->language, "");
					} else {
						if (option_verbose > 2)
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
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "VM-CID: Numeric caller id: (%s)\n",callerid);
			/* BB: Since this is all nicely figured out, why not say "from phone number" in this case" */
			if (!callback)
				res = wait_file2(chan, vms, "vm-from-phonenumber");
			res = ast_say_digit_str(chan, callerid, AST_DIGIT_ANY, chan->language);
		}
	} else {
		/* Number unknown */
		if (option_debug)
			ast_log(LOG_DEBUG, "VM-CID: From an unknown number\n");
		/* Say "from an unknown caller" as one phrase - it is already recorded by "the voice" anyhow */
		res = wait_file2(chan, vms, "vm-unknown-caller");
	}
	return res;
}

static int play_message_duration(struct ast_channel *chan, struct vm_state *vms, const char *duration, int minduration)
{
	int res = 0;
	int durationm;
	int durations;
	/* Verify that we have a duration for the message */
	if (duration == NULL)
		return res;

	/* Convert from seconds to minutes */
	durations=atoi(duration);
	durationm=(durations / 60);

	if (option_debug > 2)
		ast_log(LOG_DEBUG, "VM-Duration: duration is: %d seconds converted to: %d minutes\n", durations, durationm);

	if ((!res) && (durationm >= minduration)) {
		res = wait_file2(chan, vms, "vm-duration");

		/* POLISH syntax */
		if (!strncasecmp(chan->language, "pl", 2)) {
			div_t num = div(durationm, 10);

			if (durationm == 1) {
				res = ast_play_and_wait(chan, "digits/1z");
				res = res ? res : ast_play_and_wait(chan, "vm-minute-ta");
			} else if (num.rem > 1 && num.rem < 5 && num.quot != 1) {
				if (num.rem == 2) {
					if (!num.quot) {
						res = ast_play_and_wait(chan, "digits/2-ie");
					} else {
						res = say_and_wait(chan, durationm - 2 , chan->language);
						res = res ? res : ast_play_and_wait(chan, "digits/2-ie");
					}
				} else {
					res = say_and_wait(chan, durationm, chan->language);
				}
				res = res ? res : ast_play_and_wait(chan, "vm-minute-ty");
			} else {
				res = say_and_wait(chan, durationm, chan->language);
				res = res ? res : ast_play_and_wait(chan, "vm-minute-t");
			}
		/* DEFAULT syntax */
		} else {
			res = ast_say_number(chan, durationm, AST_DIGIT_ANY, chan->language, NULL);
			res = wait_file2(chan, vms, "vm-minutes");
		}
	}
	return res;
}

static int play_message(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms)
{
	int res = 0;
	char filename[256], *cid;
	const char *origtime, *context, *category, *duration;
	struct ast_config *msg_cfg;

	vms->starting = 0; 
	make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
	adsi_message(chan, vms);
	
	if (!strncasecmp(chan->language, "he", 2)) {        /* HEBREW FORMAT */
		/*
		 * The syntax in hebrew for counting the number of message is up side down
		 * in comparison to english.
		 */
		if (!vms->curmsg) {
			res = wait_file2(chan, vms, "vm-message");
			res = wait_file2(chan, vms, "vm-first");    /* "First" */
		} else if (vms->curmsg == vms->lastmsg) {
			res = wait_file2(chan, vms, "vm-message");
			res = wait_file2(chan, vms, "vm-last");     /* "last" */
		} else {
			res = wait_file2(chan, vms, "vm-message");  /* "message" */
			if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
				ast_log(LOG_DEBUG, "curmsg: %d\n", vms->curmsg);
				ast_log(LOG_DEBUG, "lagmsg: %d\n", vms->lastmsg);
				if (!res) {
					res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, chan->language, "f");
				}
			}
		}

	} else if (!strncasecmp(chan->language, "pl", 2)) { /* POLISH FORMAT */
		if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
			int ten, one;
			char nextmsg[256];
			ten = (vms->curmsg + 1) / 10;
			one = (vms->curmsg + 1) % 10;

			if (vms->curmsg < 20) {
				snprintf(nextmsg, sizeof(nextmsg), "digits/n-%d", vms->curmsg + 1);
				res = wait_file2(chan, vms, nextmsg);
			} else {
				snprintf(nextmsg, sizeof(nextmsg), "digits/n-%d", ten * 10);
				res = wait_file2(chan, vms, nextmsg);
				if (one > 0) {
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/n-%d", one);
						res = wait_file2(chan, vms, nextmsg);
					}
				}
			}
		}
		if (!res)
			res = wait_file2(chan, vms, "vm-message");			

	} else if (!strncasecmp(chan->language, "se", 2)) { /* SWEDISH FORMAT */
		if (!vms->curmsg)
			res = wait_file2(chan, vms, "vm-first");	/* "First" */
		else if (vms->curmsg == vms->lastmsg)
			res = wait_file2(chan, vms, "vm-last");		/* "last" */		
		res = wait_file2(chan, vms, "vm-meddelandet");  /* "message" */
		if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
			if (!res)
				res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, chan->language, NULL);
		}
		/* We know that the difference between English and Swedish
		 * is very small, however, we differ the two for standartization
		 * purposes, and possible changes to either of these in the 
		 * future
		 */
	} else {
		if (!vms->curmsg)								/* Default syntax */
			res = wait_file2(chan, vms, "vm-first");	/* "First" */
		else if (vms->curmsg == vms->lastmsg)
			res = wait_file2(chan, vms, "vm-last");		/* "last" */		
		res = wait_file2(chan, vms, "vm-message");
		if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
			if (!res)
				res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, chan->language, NULL);
		}
	}

	/* Retrieve info from VM attribute file */
	make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg);
	snprintf(filename, sizeof(filename), "%s.txt", vms->fn2);
	RETRIEVE(vms->curdir, vms->curmsg, vmu);
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

	cid = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "callerid"));
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
		if ((res = wait_file(chan, vms, vms->fn)) < 0) {
			ast_log(LOG_WARNING, "Playback of message %s failed\n", vms->fn);
			res = 0;
		}
	}
	DISPOSE(vms->curdir, vms->curmsg);
	return res;
}

#ifndef IMAP_STORAGE
static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu,int box)
{
	int count_msg, last_msg;

	ast_copy_string(vms->curbox, mbox(box), sizeof(vms->curbox));
	
	/* Rename the member vmbox HERE so that we don't try to return before
	 * we know what's going on.
	 */
	snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", vms->curbox);
	
	/* Faster to make the directory than to check if it exists. */
	create_dirpath(vms->curdir, sizeof(vms->curdir), vmu->context, vms->username, vms->curbox);

	/* traverses directory using readdir (or select query for ODBC) */
	count_msg = count_messages(vmu, vms->curdir);
	if (count_msg < 0)
		return count_msg;
	else
		vms->lastmsg = count_msg - 1;

	if (vm_allocate_dh(vms, vmu, count_msg)) {
		return -1;
	}

	/*
	The following test is needed in case sequencing gets messed up.
	There appears to be more than one way to mess up sequence, so
	we will not try to find all of the root causes--just fix it when
	detected.
	*/

	/* for local storage, checks directory for messages up to maxmsg limit */
	last_msg = last_message_index(vmu, vms->curdir);

	if (last_msg < -1) {
		return last_msg;
	} else if (vms->lastmsg != last_msg) {
		ast_log(LOG_NOTICE, "Resequencing Mailbox: %s, expected %d but found %d message(s) in box with max threshold of %d.\n", vms->curdir, last_msg + 1, vms->lastmsg + 1, vmu->maxmsg);
		resequence_mailbox(vmu, vms->curdir, count_msg);
	}

	return 0;
}
#endif

static int close_mailbox(struct vm_state *vms, struct ast_vm_user *vmu)
{
	int x = 0;
#ifndef IMAP_STORAGE
	int last_msg_index;
	int res = 0, nummsg;
#endif

	if (vms->lastmsg <= -1)
		goto done;

	vms->curmsg = -1; 
#ifndef IMAP_STORAGE
	/* Get the deleted messages fixed */ 
	if (vm_lock_path(vms->curdir))
		return ERROR_LOCK_PATH;

	last_msg_index = last_message_index(vmu, vms->curdir);
	if (last_msg_index !=  vms->lastmsg) {
		ast_log(LOG_NOTICE, "%d messages arrived while mailbox was open\n", last_msg_index - vms->lastmsg);
	}
 
	/* must check up to last detected message, just in case it is erroneously greater than maxmsg */
	for (x = 0; x < last_msg_index + 1; x++) { 
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
			res = save_to_folder(vmu, vms, x, 1);
			if (res == ERROR_LOCK_PATH || res == ERROR_MAILBOX_FULL) {
				/* If save failed do not delete the message */
				ast_log(LOG_WARNING, "Save failed.  Not moving message: %s.\n", res == ERROR_LOCK_PATH ? "unable to lock path" : "destination folder full");
				vms->deleted[x] = 0;
				vms->heard[x] = 0;
				--x;
			} 
		} 
	} 

	/* Delete ALL remaining messages */
	nummsg = x - 1;
	for (x = vms->curmsg + 1; x <= nummsg; x++) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, x);
		if (EXISTS(vms->curdir, x, vms->fn, NULL))
			DELETE(vms->curdir, x, vms->fn, vmu);
	}
	ast_unlock_path(vms->curdir);
#else /* defined(IMAP_STORAGE) */
	if (vms->deleted) {
		/* Since we now expunge after each delete, deleting in reverse order
		 * ensures that no reordering occurs between each step. */
		for (x = vms->dh_arraysize - 1; x >= 0; x--) {
			if (vms->deleted[x]) {
				if (option_debug > 2) {
					ast_log(LOG_DEBUG, "IMAP delete of %d\n", x);
				}
				DELETE(vms->curdir, x, vms->fn, vmu);
			}
		}
	}
#endif

done:
	if (vms->deleted)
		memset(vms->deleted, 0, vms->dh_arraysize * sizeof(int)); 
	if (vms->heard)
		memset(vms->heard, 0, vms->dh_arraysize * sizeof(int)); 

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
	char *buf;

	buf = alloca(strlen(mbox)+2); 
	strcpy(buf, mbox);
	strcat(buf,"s");

	if (!strcasecmp(mbox, "vm-INBOX") || !strcasecmp(mbox, "vm-Old")){
		cmd = ast_play_and_wait(chan, buf); /* "NEA / PALIA" */
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages"); /* "messages" -> "MYNHMATA" */
	} else {
		cmd = ast_play_and_wait(chan, "vm-messages"); /* "messages" -> "MYNHMATA" */
		return cmd ? cmd : ast_play_and_wait(chan, mbox); /* friends/family/work... -> "FILWN"/"OIKOGENIAS"/"DOULEIAS"*/
	}
}

static int vm_play_folder_name_pl(struct ast_channel *chan, char *mbox)
{
	int cmd;

	if (!strcasecmp(mbox, "vm-INBOX") || !strcasecmp(mbox, "vm-Old")) {
		if (!strcasecmp(mbox, "vm-INBOX"))
			cmd = ast_play_and_wait(chan, "vm-new-e");
		else
			cmd = ast_play_and_wait(chan, "vm-old-e");
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages");
	} else {
		cmd = ast_play_and_wait(chan, "vm-messages");
		return cmd ? cmd : ast_play_and_wait(chan, mbox);
	}
}

static int vm_play_folder_name_ua(struct ast_channel *chan, char *mbox)
{
	int cmd;

	if (!strcasecmp(mbox, "vm-Family") || !strcasecmp(mbox, "vm-Friends") || !strcasecmp(mbox, "vm-Work")){
		cmd = ast_play_and_wait(chan, "vm-messages");
		return cmd ? cmd : ast_play_and_wait(chan, mbox);
	} else {
		cmd = ast_play_and_wait(chan, mbox);
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages");
	}
}

static int vm_play_folder_name(struct ast_channel *chan, char *mbox)
{
	int cmd;

	if (  !strncasecmp(chan->language, "it", 2) ||
		  !strncasecmp(chan->language, "es", 2) ||
		  !strncasecmp(chan->language, "pt", 2)) { /* Italian, Spanish, or Portuguese syntax */
		cmd = ast_play_and_wait(chan, "vm-messages"); /* "messages */
		return cmd ? cmd : ast_play_and_wait(chan, mbox);
	} else if (!strncasecmp(chan->language, "gr", 2)) {
		return vm_play_folder_name_gr(chan, mbox);
	} else if (!strncasecmp(chan->language, "pl", 2)) {
		return vm_play_folder_name_pl(chan, mbox);
	} else if (!strncasecmp(chan->language, "ua", 2)) {  /* Ukrainian syntax */
		return vm_play_folder_name_ua(chan, mbox);
	} else if (!strncasecmp(chan->language, "he", 2)) {  /* Hebrew syntax */
		cmd = ast_play_and_wait(chan, mbox);
		return cmd;
	} else {  /* Default English */
		cmd = ast_play_and_wait(chan, mbox);
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages"); /* "messages */
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
static int vm_intro_en(struct ast_channel *chan, struct vm_state *vms)
{
	int res;

	/* Introduce messages they have */
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

/* Version of vm_intro() designed to work for many languages.
 *
 * It is hoped that this function can prevent the proliferation of 
 * language-specific vm_intro() functions and in time replace the language-
 * specific functions which already exist.  An examination of the language-
 * specific functions revealed that they all corrected the same deficiencies
 * in vm_intro_en() (which was the default function). Namely:
 *
 *  1) The vm-Old and vm-INBOX sound files were overloaded.  The English 
 *     wording of the voicemail greeting hides this problem.  For example,
 *     vm-INBOX contains only the word "new".  This means that both of these
 *     sequences produce valid utterances:
 *      * vm-youhave digit/1 vm-INBOX vm-message (you have one new message)
 *      * vm-press digit/1 vm-for vm-INBOX vm-messages (press 1 for new messages)
 *     However, if we rerecord vm-INBOX to say "the new" (which is unavoidable
 *     in many languages) the first utterance becomes "you have 1 the new message".
 *  2) The function contains hardcoded rules for pluralizing the word "message".
 *     These rules are correct for English, but not for many other languages.
 *  3) No attempt is made to pluralize the adjectives ("old" and "new") as
 *     required in many languages.
 *  4) The gender of the word for "message" is not specified. This is a problem
 *     because in many languages the gender of the number in phrases such
 *     as "you have one new message" must match the gender of the word
 *     meaning "message".
 *
 * Fixing these problems for each new language has meant duplication of effort.
 * This new function solves the problems in the following general ways:
 *  1) Add new sound files vm-new and vm-old.  These can be linked to vm-INBOX
 *     and vm-Old respectively for those languages where it makes sense.
 *  2) Call ast_say_counted_noun() to put the proper gender and number prefix
 *     on vm-message.
 *  3) Call ast_say_counted_adjective() to put the proper gender and number
 *     prefix on vm-new and vm-old (none for English).
 *  4) Pass the gender of the language's word for "message" as an agument to
 *     this function which is can in turn pass on to the functions which 
 *     say numbers and put endings on nounds and adjectives.
 *
 * All languages require these messages:
 *  vm-youhave		"You have..."
 *  vm-and		"and"
 *  vm-no		"no" (in the sense of "none", as in "you have no messages")
 *
 * To use it for English, you will need these additional sound files:
 *  vm-new		"new"
 *  vm-message		"message", singular
 *  vm-messages		"messages", plural
 *
 * If you use it for Russian and other slavic languages, you will need these additional sound files:
 *
 *  vm-newn		"novoye" (singular, neuter)
 *  vm-newx		"novikh" (counting plural form, genative plural)
 *  vm-message		"sobsheniye" (singular form)
 *  vm-messagex1	"sobsheniya" (first counting plural form, genative singular)
 *  vm-messagex2	"sobsheniy" (second counting plural form, genative plural)
 *  digits/1n		"odno" (neuter singular for phrases such as "one message" or "thirty one messages")
 *  digits/2n		"dva" (neuter singular)
 */
static int vm_intro_multilang(struct ast_channel *chan, struct vm_state *vms, const char message_gender[])
{
	int res;
	int lastnum = 0;

	res = ast_play_and_wait(chan, "vm-youhave");

	if (!res && vms->newmessages) {
		lastnum = vms->newmessages;

		if (!(res = ast_say_number(chan, lastnum, AST_DIGIT_ANY, chan->language, message_gender))) {
			res = ast_say_counted_adjective(chan, lastnum, "vm-new", message_gender);
		}

		if (!res && vms->oldmessages) {
			res = ast_play_and_wait(chan, "vm-and");
		}
	}

	if (!res && vms->oldmessages) {
		lastnum = vms->oldmessages;

		if (!(res = ast_say_number(chan, lastnum, AST_DIGIT_ANY, chan->language, message_gender))) {
			res = ast_say_counted_adjective(chan, lastnum, "vm-old", message_gender);
		}
	}

	if (!res) {
		if (lastnum == 0) {
			res = ast_play_and_wait(chan, "vm-no");
		}
		if (!res) {
			res = ast_say_counted_noun(chan, lastnum, "vm-message");
		}
	}

	return res;
}

/* Default Hebrew syntax */
static int vm_intro_he(struct ast_channel *chan, struct vm_state *vms)
{
	int res=0;

	/* Introduce messages they have */
	if (!res) {
		if ((vms->newmessages) || (vms->oldmessages)) {
			res = ast_play_and_wait(chan, "vm-youhave");
		}
		/*
		 * The word "shtei" refers to the number 2 in hebrew when performing a count
		 * of elements. In Hebrew, there are 6 forms of enumerating the number 2 for
		 * an element, this is one of them.
		 */
		if (vms->newmessages) {
			if (!res) {
				if (vms->newmessages == 1) {
					res = ast_play_and_wait(chan, "vm-INBOX1");
				} else {
					if (vms->newmessages == 2) {
						res = ast_play_and_wait(chan, "vm-shtei");
					} else {
						res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, chan->language, "f");
					}
					res = ast_play_and_wait(chan, "vm-INBOX");
				}
			}
			if (vms->oldmessages && !res) {
				res = ast_play_and_wait(chan, "vm-and");
				if (vms->oldmessages == 1) {
					res = ast_play_and_wait(chan, "vm-Old1");
				} else {
					if (vms->oldmessages == 2) {
						res = ast_play_and_wait(chan, "vm-shtei");
					} else {
						res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, chan->language, "f");
					}
					res = ast_play_and_wait(chan, "vm-Old");
				}
			}
		}
		if (!res && vms->oldmessages && !vms->newmessages) {
			if (!res) {
				if (vms->oldmessages == 1) {
					res = ast_play_and_wait(chan, "vm-Old1");
				} else {
					if (vms->oldmessages == 2) {
						res = ast_play_and_wait(chan, "vm-shtei");
					} else {
						res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, chan->language, "f");            
					}
					res = ast_play_and_wait(chan, "vm-Old");
				}
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				if (!res) {
					res = ast_play_and_wait(chan, "vm-nomessages");
				}
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
	return res;
}

/* POLISH syntax */
static int vm_intro_pl(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	div_t num;

	if (!vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-no");
		res = res ? res : ast_play_and_wait(chan, "vm-messages");
		return res;
	} else {
		res = ast_play_and_wait(chan, "vm-youhave");
	}

	if (vms->newmessages) {
		num = div(vms->newmessages, 10);
		if (vms->newmessages == 1) {
			res = ast_play_and_wait(chan, "digits/1-a");
			res = res ? res : ast_play_and_wait(chan, "vm-new-a");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else if (num.rem > 1 && num.rem < 5 && num.quot != 1) {
			if (num.rem == 2) {
				if (!num.quot) {
					res = ast_play_and_wait(chan, "digits/2-ie");
				} else {
					res = say_and_wait(chan, vms->newmessages - 2 , chan->language);
					res = res ? res : ast_play_and_wait(chan, "digits/2-ie");
				}
			} else {
				res = say_and_wait(chan, vms->newmessages, chan->language);
			}
			res = res ? res : ast_play_and_wait(chan, "vm-new-e");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		} else {
			res = say_and_wait(chan, vms->newmessages, chan->language);
			res = res ? res : ast_play_and_wait(chan, "vm-new-ych");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
		if (!res && vms->oldmessages)
			res = ast_play_and_wait(chan, "vm-and");
	}
	if (!res && vms->oldmessages) {
		num = div(vms->oldmessages, 10);
		if (vms->oldmessages == 1) {
			res = ast_play_and_wait(chan, "digits/1-a");
			res = res ? res : ast_play_and_wait(chan, "vm-old-a");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else if (num.rem > 1 && num.rem < 5 && num.quot != 1) {
			if (num.rem == 2) {
				if (!num.quot) {
					res = ast_play_and_wait(chan, "digits/2-ie");
				} else {
					res = say_and_wait(chan, vms->oldmessages - 2 , chan->language);
					res = res ? res : ast_play_and_wait(chan, "digits/2-ie");
				}
			} else {
				res = say_and_wait(chan, vms->oldmessages, chan->language);
			}
			res = res ? res : ast_play_and_wait(chan, "vm-old-e");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		} else {
			res = say_and_wait(chan, vms->oldmessages, chan->language);
			res = res ? res : ast_play_and_wait(chan, "vm-old-ych");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
	}

	return res;
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

/* BRAZILIAN PORTUGUESE syntax */
static int vm_intro_pt_BR(struct ast_channel *chan,struct vm_state *vms) {
	/* Introduce messages they have */
	int res;
	if (!vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-nomessages");
		return res;
	}
	else {
		res = ast_play_and_wait(chan, "vm-youhave");
	}
	if (vms->newmessages) {
		if (!res)
			res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, chan->language, "f");
		if ((vms->newmessages == 1)) {
			if (!res)
				res = ast_play_and_wait(chan, "vm-message");
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOXs");
		}
		else {
			if (!res)
				res = ast_play_and_wait(chan, "vm-messages");
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
		}
		if (vms->oldmessages && !res)
			res = ast_play_and_wait(chan, "vm-and");
	}
	if (vms->oldmessages) {
		if (!res)
			res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, chan->language, "f");
		if (vms->oldmessages == 1) {
			if (!res)
				res = ast_play_and_wait(chan, "vm-message");
			if (!res)
				res = ast_play_and_wait(chan, "vm-Olds");
		}
		else {
			if (!res)
		res = ast_play_and_wait(chan, "vm-messages");
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
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
				if (vms->newmessages == 1)
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
 * czech        : english        : czech      : english
 * --------------------------------------------------------
 * vm-youhave   : you have 
 * vm-novou     : one new        : vm-zpravu  : message
 * vm-nove      : 2-4 new        : vm-zpravy  : messages
 * vm-novych    : 5-infinite new : vm-zprav   : messages
 * vm-starou	: one old
 * vm-stare     : 2-4 old 
 * vm-starych   : 5-infinite old
 * jednu        : one	- falling 4. 
 * vm-no        : no  ( no messages )
 */

static int vm_intro_cs(struct ast_channel *chan,struct vm_state *vms)
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

static int vm_intro(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms)
{
	char prefile[256];
	
	/* Notify the user that the temp greeting is set and give them the option to remove it */
	snprintf(prefile, sizeof(prefile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, vms->username);
	if (ast_test_flag(vmu, VM_TEMPGREETWARN)) {
		RETRIEVE(prefile, -1, vmu);
		if (ast_fileexists(prefile, NULL, NULL) > 0)
			ast_play_and_wait(chan, "vm-tempgreetactive");
		DISPOSE(prefile, -1);
	}

	/* Play voicemail intro - syntax is different for different languages */
	if (0) {
		return 0;
	} else if (!strncasecmp(chan->language, "cs", 2)) {  /* CZECH syntax */
		return vm_intro_cs(chan, vms);
	} else if (!strncasecmp(chan->language, "cz", 2)) {  /* deprecated CZECH syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "cz is not a standard language code.  Please switch to using cs instead.\n");
		}
		return vm_intro_cs(chan, vms);
	} else if (!strncasecmp(chan->language, "de", 2)) {  /* GERMAN syntax */
		return vm_intro_de(chan, vms);
	} else if (!strncasecmp(chan->language, "es", 2)) {  /* SPANISH syntax */
		return vm_intro_es(chan, vms);
	} else if (!strncasecmp(chan->language, "fr", 2)) {  /* FRENCH syntax */
		return vm_intro_fr(chan, vms);
	} else if (!strncasecmp(chan->language, "gr", 2)) {  /* GREEK syntax */
		return vm_intro_gr(chan, vms);
	} else if (!strncasecmp(chan->language, "he", 2)) {  /* HEBREW syntax */
		return vm_intro_he(chan, vms);
	} else if (!strncasecmp(chan->language, "it", 2)) {  /* ITALIAN syntax */
		return vm_intro_it(chan, vms);
	} else if (!strncasecmp(chan->language, "nl", 2)) {  /* DUTCH syntax */
		return vm_intro_nl(chan, vms);
	} else if (!strncasecmp(chan->language, "no", 2)) {  /* NORWEGIAN syntax */
		return vm_intro_no(chan, vms);
	} else if (!strncasecmp(chan->language, "pl", 2)) {  /* POLISH syntax */
		return vm_intro_pl(chan, vms);
	} else if (!strncasecmp(chan->language, "pt_BR", 5)) {  /* BRAZILIAN PORTUGUESE syntax */
		return vm_intro_pt_BR(chan, vms);
	} else if (!strncasecmp(chan->language, "pt", 2)) {  /* PORTUGUESE syntax */
		return vm_intro_pt(chan, vms);
	} else if (!strncasecmp(chan->language, "ru", 2)) {  /* RUSSIAN syntax */
		return vm_intro_multilang(chan, vms, "n");
	} else if (!strncasecmp(chan->language, "se", 2)) {  /* SWEDISH syntax */
		return vm_intro_se(chan, vms);
	} else if (!strncasecmp(chan->language, "ua", 2)) {  /* UKRAINIAN syntax */
		return vm_intro_multilang(chan, vms, "n");
	} else {                                             /* Default to ENGLISH */
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
				if (!strncasecmp(chan->language, "he", 2)) {
					res = ast_play_and_wait(chan, "vm-for");
				}
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
	char prefile[PATH_MAX] = "";
	unsigned char buf[256];
	int bytes=0;

	if (ast_adsi_available(chan)) {
		bytes += adsi_logo(buf + bytes);
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "New User Setup", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
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
		if (cmd == 0) {
			cmd = ast_play_and_wait(chan, "vm-pls-try-again");
		}
	}
	if (ast_strlen_zero(ext_pass_cmd)) 
		vm_change_password(vmu,newpassword);
	else 
		vm_change_password_shell(vmu,newpassword);
	if (option_debug > 2)
		ast_log(LOG_DEBUG,"User %s set password to %s of length %d\n",vms->username,newpassword,(int)strlen(newpassword));
	cmd = ast_play_and_wait(chan,"vm-passchanged");

	/* If forcename is set, have the user record their name */	
	if (ast_test_flag(vmu, VM_FORCENAME)) {
		snprintf(prefile,sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, vms->username);
		if (ast_fileexists(prefile, NULL, NULL) < 1) {
			cmd = play_record_review(chan, "vm-rec-name", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
		}
	}

	/* If forcegreetings is set, have the user record their greetings */
	if (ast_test_flag(vmu, VM_FORCEGREET)) {
		snprintf(prefile,sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, vms->username);
		if (ast_fileexists(prefile, NULL, NULL) < 1) {
			cmd = play_record_review(chan, "vm-rec-unv", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
		}

		snprintf(prefile,sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, vms->username);
		if (ast_fileexists(prefile, NULL, NULL) < 1) {
			cmd = play_record_review(chan, "vm-rec-busy", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
		}
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
	char prefile[PATH_MAX] = "";
	unsigned char buf[256];
	int bytes=0;

	if (ast_adsi_available(chan))
	{
		bytes += adsi_logo(buf + bytes);
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Options Menu", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}
	while ((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1':
			snprintf(prefile,sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-unv",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
			break;
		case '2': 
			snprintf(prefile,sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-busy",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
			break;
		case '3': 
			snprintf(prefile,sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan,"vm-rec-name",prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
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

				if ((cmd = ast_readstring(chan,newpassword2 + strlen(newpassword2),sizeof(newpassword2)-1,2000,10000,"#")) < 0) {
					break;
				}
			}
			if (strcmp(newpassword, newpassword2)) {
				ast_log(LOG_NOTICE,"Password mismatch for user %s (%s != %s)\n", vms->username, newpassword, newpassword2);
				cmd = ast_play_and_wait(chan, "vm-mismatch");
				if (!cmd) {
					cmd = ast_play_and_wait(chan, "vm-pls-try-again");
				}
				break;
			}
			if (ast_strlen_zero(ext_pass_cmd)) 
				vm_change_password(vmu,newpassword);
			else 
				vm_change_password_shell(vmu,newpassword);
			if (option_debug > 2)
				ast_log(LOG_DEBUG,"User %s set password to %s of length %d\n",vms->username,newpassword,(int)strlen(newpassword));
			cmd = ast_play_and_wait(chan,"vm-passchanged");
			break;
		case '*': 
			cmd = 't';
			break;
		default: 
			cmd = 0;
			snprintf(prefile, sizeof(prefile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, vms->username);
			RETRIEVE(prefile, -1, vmu);
			if (ast_fileexists(prefile, NULL, NULL))
				cmd = ast_play_and_wait(chan, "vm-tmpexists");
			DISPOSE(prefile, -1);
			if (!cmd)
				cmd = ast_play_and_wait(chan, "vm-options");
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
	int res;
	int cmd = 0;
	int retries = 0;
	int duration = 0;
	char prefile[PATH_MAX] = "";
	unsigned char buf[256];
	char dest[PATH_MAX];
	int bytes = 0;

	if (ast_adsi_available(chan)) {
		bytes += adsi_logo(buf + bytes);
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Temp Greeting Menu", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}

	snprintf(prefile, sizeof(prefile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, vms->username);
	if ((res = create_dirpath(dest, sizeof(dest), vmu->context, vms->username, "temp"))) {
		ast_log(LOG_WARNING, "Failed to create directory (%s).\n", prefile);
		return -1;
	}
	while ((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		RETRIEVE(prefile, -1, vmu);
		if (ast_fileexists(prefile, NULL, NULL) <= 0) {
			play_record_review(chan, "vm-rec-temp", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
			cmd = 't';	
		} else {
			switch (cmd) {
			case '1':
				cmd = play_record_review(chan, "vm-rec-temp", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, record_gain, vms);
				break;
			case '2':
				DELETE(prefile, -1, prefile, vmu);
				ast_play_and_wait(chan, "vm-tempremoved");
				cmd = 't';	
				break;
			case '*': 
				cmd = 't';
				break;
			default:
				cmd = ast_play_and_wait(chan,
					ast_fileexists(prefile, NULL, NULL) > 0 ? /* XXX always true ? */
						"vm-tempgreeting2" : "vm-tempgreeting");
				if (!cmd)
					cmd = ast_waitfordigit(chan,6000);
				if (!cmd)
					retries++;
				if (retries > 3)
					cmd = 't';
			}
		}
		DISPOSE(prefile, -1);
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

/* Hebrew Syntax */
static int vm_browse_messages_he(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd = 0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		if (!strcasecmp(vms->fn, "INBOX")) {
			cmd = ast_play_and_wait(chan, "vm-nonewmessages");
		} else {
			cmd = ast_play_and_wait(chan, "vm-nomessages");
		}
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
	if (!strncasecmp(chan->language, "es", 2)) {         /* SPANISH */
		return vm_browse_messages_es(chan, vms, vmu);
	} else if (!strncasecmp(chan->language, "gr", 2)) {  /* GREEK */
		return vm_browse_messages_gr(chan, vms, vmu);
	} else if (!strncasecmp(chan->language, "he", 2)) {  /* HEBREW */
		return vm_browse_messages_he(chan, vms, vmu);
	} else if (!strncasecmp(chan->language, "it", 2)) {  /* ITALIAN */
		return vm_browse_messages_it(chan, vms, vmu);
	} else if (!strncasecmp(chan->language, "pt", 2)) {  /* PORTUGUESE */
		return vm_browse_messages_pt(chan, vms, vmu);
	} else {                                             /* Default to English syntax */
		return vm_browse_messages_en(chan, vms, vmu);
	}
}

static int vm_authenticate(struct ast_channel *chan, char *mailbox, int mailbox_size,
			struct ast_vm_user *res_vmu, const char *context, const char *prefix,
			int skipuser, int maxlogins, int silent)
{
	int useadsi=0, valid=0, logretries=0;
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

		if (option_debug)
			ast_log(LOG_DEBUG, "Before find user for mailbox %s\n",mailbox);
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
				ast_verbose( VERBOSE_PREFIX_3 "Incorrect password '%s' for user '%s' (context = %s)\n", password, mailbox, context ? context : "default");
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
	struct ast_module_user *u;
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
	int play_auto = 0;
	int play_folder = 0;
#ifdef IMAP_STORAGE
	int deleted = 0;
#endif
	u = ast_module_user_add(chan);

	/* Add the vm_state to the active list and keep it active */
	memset(&vms, 0, sizeof(vms));
	vms.lastmsg = -1;

	memset(&vmus, 0, sizeof(vmus));

	if (chan->_state != AST_STATE_UP) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Before ast_answer\n");
		ast_answer(chan);
	}

	if (!ast_strlen_zero(data)) {
		char *opts[OPT_ARG_ARRAY_SIZE];
		char *parse;
		AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(argv0);
			AST_APP_ARG(argv1);
		);

		parse = ast_strdupa(data);

		AST_STANDARD_APP_ARGS(args, parse);

		if (args.argc == 2) {
			if (ast_app_parse_options(vm_app_options, &flags, opts, args.argv1)) {
				ast_module_user_remove(u);
				return -1;
			}
			if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
				int gain;
				if (!ast_strlen_zero(opts[OPT_ARG_RECORDGAIN])) {
					if (sscanf(opts[OPT_ARG_RECORDGAIN], "%30d", &gain) != 1) {
						ast_log(LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
						ast_module_user_remove(u);
						return -1;
					} else {
						record_gain = (signed char) gain;
					}
				} else {
					ast_log(LOG_WARNING, "Invalid Gain level set with option g\n");
				}
			}
			if (ast_test_flag(&flags, OPT_AUTOPLAY) ) {
				play_auto = 1;
				if (opts[OPT_ARG_PLAYFOLDER]) {
					if (sscanf(opts[OPT_ARG_PLAYFOLDER], "%30d", &play_folder) != 1) {
						ast_log(LOG_WARNING, "Invalid value '%s' provided for folder autoplay option\n", opts[OPT_ARG_PLAYFOLDER]);
					}
				} else {
					ast_log(LOG_WARNING, "Invalid folder set with option a\n");
				}	
				if ( play_folder > 9 || play_folder < 0) {
					ast_log(LOG_WARNING, "Invalid value '%d' provided for folder autoplay option\n", play_folder);
					play_folder = 0;
				}
			}
		} else {
			/* old style options parsing */
			while (*(args.argv0)) {
				if (*(args.argv0) == 's')
					ast_set_flag(&flags, OPT_SILENT);
				else if (*(args.argv0) == 'p')
					ast_set_flag(&flags, OPT_PREPEND_MAILBOX);
				else 
					break;
				(args.argv0)++;
			}

		}

		valid = ast_test_flag(&flags, OPT_SILENT);

		if ((context = strchr(args.argv0, '@')))
			*context++ = '\0';

		if (ast_test_flag(&flags, OPT_PREPEND_MAILBOX))
			ast_copy_string(prefixstr, args.argv0, sizeof(prefixstr));
		else
			ast_copy_string(vms.username, args.argv0, sizeof(vms.username));

		if (!ast_strlen_zero(vms.username) && (vmu = find_user(&vmus, context ,vms.username)))
			skipuser++;
		else
			valid = 0;
	}

	if (!valid)
		res = vm_authenticate(chan, vms.username, sizeof(vms.username), &vmus, context, prefixstr, skipuser, maxlogins, 0);

	if (option_debug)
		ast_log(LOG_DEBUG, "After vm_authenticate\n");
	if (!res) {
		valid = 1;
		if (!skipuser)
			vmu = &vmus;
	} else {
		res = 0;
	}

	/* If ADSI is supported, setup login screen */
	adsi_begin(chan, &useadsi);

	if (!valid) {
		goto out;
	}

#ifdef IMAP_STORAGE
	pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
	pthread_setspecific(ts_vmstate.key, &vms);

	vms.interactive = 1;
	vms.updated = 1;
	if (vmu)
		ast_copy_string(vms.context, vmu->context, sizeof(vms.context));
	vmstate_insert(&vms);
	init_vm_state(&vms);
#endif

	/* Set language from config to override channel language */
	if (!ast_strlen_zero(vmu->language))
		ast_string_field_set(chan, language, vmu->language);
	create_dirpath(vms.curdir, sizeof(vms.curdir), vmu->context, vms.username, "");
	/* Retrieve old and new message counts */
	if (option_debug)
		ast_log(LOG_DEBUG, "Before open_mailbox\n");
	res = open_mailbox(&vms, vmu, 1);
	if (res < 0)
		goto out;
	vms.oldmessages = vms.lastmsg + 1;
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Number of old messages: %d\n",vms.oldmessages);
	/* Start in INBOX */
	res = open_mailbox(&vms, vmu, 0);
	if (res < 0)
		goto out;
	vms.newmessages = vms.lastmsg + 1;
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Number of new messages: %d\n",vms.newmessages);
		
	/* Select proper mailbox FIRST!! */
	if (play_auto) {
		res = open_mailbox(&vms, vmu, play_folder);
		if (res < 0)
			goto out;

		/* If there are no new messages, inform the user and hangup */
		if (vms.lastmsg == -1) {
			cmd = vm_browse_messages(chan, &vms, vmu);
			res = 0;
			goto out;
		}
	} else {
		if (!vms.newmessages && vms.oldmessages) {
			/* If we only have old messages start here */
			res = open_mailbox(&vms, vmu, 1);
			play_folder = 1;
			if (res < 0)
				goto out;
		}
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
#ifdef IMAP_STORAGE
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Checking quotas: comparing %u to %u\n",vms.quota_usage,vms.quota_limit);
		if (vms.quota_limit && vms.quota_usage >= vms.quota_limit) {
			if (option_debug)
				ast_log(LOG_DEBUG, "*** QUOTA EXCEEDED!!\n");
			cmd = ast_play_and_wait(chan, "vm-mailboxfull");
		}
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Checking quotas: User has %d messages and limit is %d.\n",(vms.newmessages + vms.oldmessages),vmu->maxmsg);
		if ((vms.newmessages + vms.oldmessages) >= vmu->maxmsg) {
			ast_log(LOG_WARNING, "No more messages possible.  User has %d messages and limit is %d.\n",(vms.newmessages + vms.oldmessages),vmu->maxmsg);
			cmd = ast_play_and_wait(chan, "vm-mailboxfull");
		}
#endif
	if (play_auto) {
		cmd = '1';
	} else {
		cmd = vm_intro(chan, vmu, &vms);
	}

	vms.repeats = 0;
	vms.starting = 1;
	while ((cmd > -1) && (cmd != 't') && (cmd != '#')) {
		/* Run main menu */
		switch (cmd) {
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
				if (res < 0)
					goto out;
				play_folder = cmd;
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
				switch (cmd) {
				case '1': /* Reply */
					if (vms.lastmsg > -1 && !vms.starting) {
						cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 1, record_gain);
						if (cmd == ERROR_LOCK_PATH || cmd == OPERATOR_EXIT) {
							res = cmd;
							goto out;
						}
					} else
						cmd = ast_play_and_wait(chan, "vm-sorry");
					cmd = 't';
					break;
				case '2': /* Callback */
					if (option_verbose > 2 && !vms.starting)
						ast_verbose( VERBOSE_PREFIX_3 "Callback Requested\n");
					if (!ast_strlen_zero(vmu->callback) && vms.lastmsg > -1 && !vms.starting) {
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
					if (vms.lastmsg > -1 && !vms.starting) {
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
						cmd = forward_message(chan, context, &vms, vmu, vmfmts, 1, record_gain);
						if (cmd == ERROR_LOCK_PATH || cmd == OPERATOR_EXIT) {
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
			if (vms.curmsg > 0) {
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
			if (vms.curmsg >= 0 && vms.curmsg <= vms.lastmsg) {
				vms.deleted[vms.curmsg] = !vms.deleted[vms.curmsg];
				if (useadsi)
					adsi_delete(chan, &vms);
				if (vms.deleted[vms.curmsg]) {
					if (play_folder == 0)
						vms.newmessages--;
					else if (play_folder == 1)
						vms.oldmessages--;
					cmd = ast_play_and_wait(chan, "vm-deleted");
				}
				else {
					if (play_folder == 0)
						vms.newmessages++;
					else if (play_folder == 1)
						vms.oldmessages++;
					cmd = ast_play_and_wait(chan, "vm-undeleted");
				}
				if (ast_test_flag((&globalflags), VM_SKIPAFTERCMD)) {
					if (vms.curmsg < vms.lastmsg) {
						vms.curmsg++;
						cmd = play_message(chan, vmu, &vms);
					} else {
						cmd = ast_play_and_wait(chan, "vm-nomore");
					}
				}
			} else /* Delete not valid if we haven't selected a message */
				cmd = 0;
#ifdef IMAP_STORAGE
			deleted = 1;
#endif
			break;
	
		case '8':
			if (vms.lastmsg > -1) {
				cmd = forward_message(chan, context, &vms, vmu, vmfmts, 0, record_gain);
				if (cmd == ERROR_LOCK_PATH) {
					res = cmd;
					goto out;
				}
			} else
				cmd = ast_play_and_wait(chan, "vm-nomore");
			break;
		case '9':
			if (vms.curmsg < 0 || vms.curmsg > vms.lastmsg) {
				/* No message selected */
				cmd = 0;
				break;
			}
			if (useadsi)
				adsi_folders(chan, 1, "Save to folder...");
			cmd = get_folder2(chan, "vm-savefolder", 1);
			box = 0;	/* Shut up compiler */
			if (cmd == '#') {
				cmd = 0;
				break;
			} else if (cmd > 0) {
				box = cmd = cmd - '0';
				cmd = save_to_folder(vmu, &vms, vms.curmsg, cmd);
				if (cmd == ERROR_LOCK_PATH) {
					res = cmd;
					goto out;
#ifndef IMAP_STORAGE
				} else if (!cmd) {
					vms.deleted[vms.curmsg] = 1;
#endif
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
				if (!strncasecmp(chan->language, "he", 2)) {
					cmd = ast_play_and_wait(chan, "vm-for");
				}
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
		if (valid && res != OPERATOR_EXIT) {
			if (silentexit)
				res = ast_play_and_wait(chan, "vm-dialout");
			else 
				res = ast_play_and_wait(chan, "vm-goodbye");
		}
		if ((valid && res > 0) || res == OPERATOR_EXIT) {
			res = 0;
		}
		if (useadsi)
			ast_adsi_unload_session(chan);
	}
	if (vmu)
		close_mailbox(&vms, vmu);
	if (valid) {
		snprintf(ext_context, sizeof(ext_context), "%s@%s", vms.username, vmu->context);
		manager_event(EVENT_FLAG_CALL, "MessageWaiting", "Mailbox: %s\r\nWaiting: %d\r\n", ext_context, has_voicemail(ext_context, NULL));
		run_externnotify(vmu->context, vmu->mailbox);
	}
#ifdef IMAP_STORAGE
	/* expunge message - use UID Expunge if supported on IMAP server*/
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "*** Checking if we can expunge, deleted set to %d, expungeonhangup set to %d\n",deleted,expungeonhangup);
	if (vmu && deleted == 1 && expungeonhangup == 1 && vms.mailstream != NULL) {
		ast_mutex_lock(&vms.lock);
#ifdef HAVE_IMAP_TK2006
		if (LEVELUIDPLUS (vms.mailstream)) {
			mail_expunge_full(vms.mailstream,NIL,EX_UID);
		} else 
#endif
			mail_expunge(vms.mailstream);
		ast_mutex_unlock(&vms.lock);
	}
	/*  before we delete the state, we should copy pertinent info
	 *  back to the persistent model */
	if (vmu) {
		vmstate_delete(&vms);
	}
#endif
	if (vmu)
		free_user(vmu);
	if (vms.deleted)
		free(vms.deleted);
	if (vms.heard)
		free(vms.heard);

#ifdef IMAP_STORAGE
	pthread_setspecific(ts_vmstate.key, NULL);
#endif
	ast_module_user_remove(u);
	return res;
}

static int vm_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	char *tmp;
	struct leave_vm_options leave_options;
	struct ast_flags flags = { 0 };
	static int deprecate_warning = 0;
	char *opts[OPT_ARG_ARRAY_SIZE];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(argv0);
		AST_APP_ARG(argv1);
	);

	u = ast_module_user_add(chan);
	
	memset(&leave_options, 0, sizeof(leave_options));

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	if (!ast_strlen_zero(data)) {
		tmp = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, tmp);
		if (args.argc == 2) {
			if (ast_app_parse_options(vm_app_options, &flags, opts, args.argv1)) {
				ast_module_user_remove(u);
				return -1;
			}
			ast_copy_flags(&leave_options, &flags, OPT_SILENT | OPT_BUSY_GREETING | OPT_UNAVAIL_GREETING | OPT_PRIORITY_JUMP);
			if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
				int gain;

				if (sscanf(opts[OPT_ARG_RECORDGAIN], "%30d", &gain) != 1) {
					ast_log(LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
					ast_module_user_remove(u);
					return -1;
				} else {
					leave_options.record_gain = (signed char) gain;
				}
			}
		} else {
			/* old style options parsing */
			int old = 0;
			char *orig_argv0 = args.argv0;
			while (*(args.argv0)) {
				if (*(args.argv0) == 's') {
					old = 1;
					ast_set_flag(&leave_options, OPT_SILENT);
				} else if (*(args.argv0) == 'b') {
					old = 1;
					ast_set_flag(&leave_options, OPT_BUSY_GREETING);
				} else if (*(args.argv0) == 'u') {
					old = 1;
					ast_set_flag(&leave_options, OPT_UNAVAIL_GREETING);
				} else if (*(args.argv0) == 'j') {
					old = 1;
					ast_set_flag(&leave_options, OPT_PRIORITY_JUMP);
				} else
					break;
				(args.argv0)++;
			}
			if (!deprecate_warning && old) {
				deprecate_warning = 1;
				ast_log(LOG_WARNING, "Prefixing the mailbox with an option is deprecated ('%s').\n", orig_argv0);
				ast_log(LOG_WARNING, "Please move all leading options to the second argument.\n");
			}
		}
	} else {
		char tmp[256];
		res = ast_app_getdata(chan, "vm-whichbox", tmp, sizeof(tmp) - 1, 0);
		if (res < 0) {
			ast_module_user_remove(u);
			return res;
		}
		if (ast_strlen_zero(tmp)) {
			ast_module_user_remove(u);
			return 0;
		}
		args.argv0 = ast_strdupa(tmp);
	}

	res = leave_voicemail(chan, args.argv0, &leave_options);
	if (res == 't') {
		ast_play_and_wait(chan, "vm-goodbye");
		res = 0;
	}

	if (res == OPERATOR_EXIT) {
		res = 0;
	}

	if (res == ERROR_LOCK_PATH) {
		ast_log(LOG_ERROR, "Could not leave voicemail. The path is already locked.\n");
		/*Send the call to n+101 priority, where n is the current priority*/
		if (ast_test_flag(&leave_options, OPT_PRIORITY_JUMP) || ast_opt_priority_jumping)
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101))
				ast_log(LOG_WARNING, "Extension %s, priority %d doesn't exist.\n", chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		res = 0;
	}
	
	ast_module_user_remove(u);

	return res;
}

static struct ast_vm_user *find_or_create(char *context, char *mbox)
{
	struct ast_vm_user *vmu;
	AST_LIST_TRAVERSE(&users, vmu, list) {
		if (ast_test_flag((&globalflags), VM_SEARCH) && !strcasecmp(mbox, vmu->mailbox)) {
			if (strcasecmp(vmu->context, context)) {
				ast_log(LOG_WARNING, "\nIt has been detected that you have defined mailbox '%s' in separate\
						\n\tcontexts and that you have the 'searchcontexts' option on. This type of\
						\n\tconfiguration creates an ambiguity that you likely do not want. Please\
						\n\tamend your voicemail.conf file to avoid this situation.\n", mbox);
			}
			ast_log(LOG_WARNING, "Ignoring duplicated mailbox %s\n", mbox);
			return NULL;
		}
		if (!strcasecmp(context, vmu->context) && !strcasecmp(mbox, vmu->mailbox)) {
			ast_log(LOG_WARNING, "Ignoring duplicated mailbox %s in context %s\n", mbox, context);
			return NULL;
		}
	}
	
	if ((vmu = ast_calloc(1, sizeof(*vmu)))) {
		ast_copy_string(vmu->context, context, sizeof(vmu->context));
		ast_copy_string(vmu->mailbox, mbox, sizeof(vmu->mailbox));
		AST_LIST_INSERT_TAIL(&users, vmu, list);
	}
	return vmu;
}

static int append_mailbox(char *context, char *mbox, char *data)
{
	/* Assumes lock is already held */
	char *tmp;
	char *stringp;
	char *s;
	struct ast_vm_user *vmu;

	tmp = ast_strdupa(data);

	if ((vmu = find_or_create(context, mbox))) {
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
	}
	return 0;
}

static int vm_box_exists(struct ast_channel *chan, void *data) 
{
	struct ast_module_user *u;
	struct ast_vm_user svm;
	char *context, *box;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(mbox);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "MailboxExists requires an argument: (vmbox[@context][|options])\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	box = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, box);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if ((context = strchr(args.mbox, '@'))) {
		*context = '\0';
		context++;
	}

	if (find_user(&svm, context, args.mbox)) {
		pbx_builtin_setvar_helper(chan, "VMBOXEXISTSSTATUS", "SUCCESS");
		if (priority_jump || ast_opt_priority_jumping)
			if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) 
				ast_log(LOG_WARNING, "VM box %s@%s exists, but extension %s, priority %d doesn't exist\n", box, context, chan->exten, chan->priority + 101);
	} else
		pbx_builtin_setvar_helper(chan, "VMBOXEXISTSSTATUS", "FAILED");
	ast_module_user_remove(u);
	return 0;
}

static int vmauthenticate(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *s = data, *user=NULL, *context=NULL, mailbox[AST_MAX_EXTENSION] = "";
	struct ast_vm_user vmus;
	char *options = NULL;
	int silent = 0, skipuser = 0;
	int res = -1;

	u = ast_module_user_add(chan);
	
	if (s) {
		s = ast_strdupa(s);
		user = strsep(&s, "|");
		options = strsep(&s, "|");
		if (user) {
			s = user;
			user = strsep(&s, "@");
			context = strsep(&s, "");
			if (!ast_strlen_zero(user))
				skipuser++;
			ast_copy_string(mailbox, user, sizeof(mailbox));
		}
	}

	if (options) {
		silent = (strchr(options, 's')) != NULL;
	}

	if (!vm_authenticate(chan, mailbox, sizeof(mailbox), &vmus, context, NULL, skipuser, 3, silent)) {
		pbx_builtin_setvar_helper(chan, "AUTH_MAILBOX", mailbox);
		pbx_builtin_setvar_helper(chan, "AUTH_CONTEXT", vmus.context);
		ast_play_and_wait(chan, "auth-thankyou");
		res = 0;
	}

	ast_module_user_remove(u);
	return res;
}

static char voicemail_show_users_help[] =
"Usage: voicemail show users [for <context>]\n"
"       Lists all mailboxes currently set up\n";

static char voicemail_show_zones_help[] =
"Usage: voicemail show zones\n"
"       Lists zone message formats\n";

static int handle_voicemail_show_users(int fd, int argc, char *argv[])
{
	struct ast_vm_user *vmu;
	char *output_format = "%-10s %-5s %-25s %-10s %6s\n";

	if ((argc < 3) || (argc > 5) || (argc == 4)) return RESULT_SHOWUSAGE;
	else if ((argc == 5) && strcmp(argv[3],"for")) return RESULT_SHOWUSAGE;

	AST_LIST_LOCK(&users);
	if (!AST_LIST_EMPTY(&users)) {
		if (argc == 3)
			ast_cli(fd, output_format, "Context", "Mbox", "User", "Zone", "NewMsg");
		else {
			int count = 0;
			AST_LIST_TRAVERSE(&users, vmu, list) {
				if (!strcmp(argv[4],vmu->context))
					count++;
			}
			if (count) {
				ast_cli(fd, output_format, "Context", "Mbox", "User", "Zone", "NewMsg");
			} else {
				ast_cli(fd, "No such voicemail context \"%s\"\n", argv[4]);
				AST_LIST_UNLOCK(&users);
				return RESULT_FAILURE;
			}
		}
		AST_LIST_TRAVERSE(&users, vmu, list) {
			int newmsgs = 0, oldmsgs = 0;
			char count[12], tmp[256] = "";

			if ((argc == 3) || ((argc == 5) && !strcmp(argv[4],vmu->context))) {
				snprintf(tmp, sizeof(tmp), "%s@%s", vmu->mailbox, ast_strlen_zero(vmu->context) ? "default" : vmu->context);
				inboxcount(tmp, &newmsgs, &oldmsgs);
				snprintf(count,sizeof(count),"%d",newmsgs);
				ast_cli(fd, output_format, vmu->context, vmu->mailbox, vmu->fullname, vmu->zonetag, count);
			}
		}
	} else {
		ast_cli(fd, "There are no voicemail users currently defined\n");
		AST_LIST_UNLOCK(&users);
		return RESULT_FAILURE;
	}
	AST_LIST_UNLOCK(&users);
	return RESULT_SUCCESS;
}

static int handle_voicemail_show_zones(int fd, int argc, char *argv[])
{
	struct vm_zone *zone;
	char *output_format = "%-15s %-20s %-45s\n";
	int res = RESULT_SUCCESS;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	AST_LIST_LOCK(&zones);
	if (!AST_LIST_EMPTY(&zones)) {
		ast_cli(fd, output_format, "Zone", "Timezone", "Message Format");
		AST_LIST_TRAVERSE(&zones, zone, list) {
			ast_cli(fd, output_format, zone->name, zone->timezone, zone->msg_format);
		}
	} else {
		ast_cli(fd, "There are no voicemail zones currently defined\n");
		res = RESULT_FAILURE;
	}
	AST_LIST_UNLOCK(&zones);

	return res;
}

static char *complete_voicemail_show_users(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	int wordlen;
	struct ast_vm_user *vmu;
	const char *context = "";

	/* 0 - show; 1 - voicemail; 2 - users; 3 - for; 4 - <context> */
	if (pos > 4)
		return NULL;
	if (pos == 3)
		return (state == 0) ? ast_strdup("for") : NULL;
	wordlen = strlen(word);
	AST_LIST_TRAVERSE(&users, vmu, list) {
		if (!strncasecmp(word, vmu->context, wordlen)) {
			if (context && strcmp(context, vmu->context) && ++which > state)
				return ast_strdup(vmu->context);
			/* ignore repeated contexts ? */
			context = vmu->context;
		}
	}
	return NULL;
}

static struct ast_cli_entry cli_show_voicemail_users_deprecated = {
	{ "show", "voicemail", "users", NULL },
	handle_voicemail_show_users, NULL,
	NULL, complete_voicemail_show_users };

static struct ast_cli_entry cli_show_voicemail_zones_deprecated = {
	{ "show", "voicemail", "zones", NULL },
	handle_voicemail_show_zones, NULL,
	NULL, NULL };

static struct ast_cli_entry cli_voicemail[] = {
	{ { "voicemail", "show", "users", NULL },
	handle_voicemail_show_users, "List defined voicemail boxes",
	voicemail_show_users_help, complete_voicemail_show_users, &cli_show_voicemail_users_deprecated },

	{ { "voicemail", "show", "zones", NULL },
	handle_voicemail_show_zones, "List zone message formats",
	voicemail_show_zones_help, NULL, &cli_show_voicemail_zones_deprecated },
};

static void free_vm_users(void)
{
	struct ast_vm_user *cur;
	struct vm_zone *zcur;

	AST_LIST_LOCK(&users);
	while ((cur = AST_LIST_REMOVE_HEAD(&users, list))) {
		ast_set_flag(cur, VM_ALLOCED);
		free_user(cur);
	}
	AST_LIST_UNLOCK(&users);

	AST_LIST_LOCK(&zones);
	while ((zcur = AST_LIST_REMOVE_HEAD(&zones, list))) {
		free_zone(zcur);
	}
	AST_LIST_UNLOCK(&zones);
}

static int load_config(void)
{
	struct ast_vm_user *cur;
	struct ast_config *cfg, *ucfg;
	char *cat;
	struct ast_variable *var;
	const char *notifystr = NULL;
	const char *smdistr = NULL;
	const char *astattach;
	const char *astsearch;
	const char *astsaycid;
	const char *send_voicemail;
#ifdef IMAP_STORAGE
	const char *imap_server;
	const char *imap_port;
	const char *imap_flags;
	const char *imap_folder;
	const char *auth_user;
	const char *auth_password;
	const char *expunge_on_hangup;
	const char *imap_timeout;
#endif
	const char *astcallop;
	const char *astreview;
	const char *asttempgreetwarn;
	const char *astskipcmd;
	const char *asthearenv;
	const char *astsaydurationinfo;
	const char *astsaydurationminfo;
	const char *silencestr;
	const char *maxmsgstr;
	const char *astdirfwd;
	const char *thresholdstr;
	const char *fmt;
	const char *astemail;
	const char *ucontext;
	const char *astmailcmd = SENDMAIL;
	const char *astforcename;
	const char *astforcegreet;
	const char *s;
	char *q,*stringp, *tmp;
	const char *dialoutcxt = NULL;
	const char *callbackcxt = NULL;	
	const char *exitcxt = NULL;	
	const char *extpc;
	const char *emaildateformatstr;
	const char *volgainstr;
	int x;
	int tmpadsi[4];

	cfg = ast_config_load(VOICEMAIL_CONFIG);

	free_vm_users();

	AST_LIST_LOCK(&users);

	memset(ext_pass_cmd, 0, sizeof(ext_pass_cmd));

	if (cfg) {
		/* General settings */

		if (!(ucontext = ast_variable_retrieve(cfg, "general", "userscontext")))
			ucontext = "default";
		ast_copy_string(userscontext, ucontext, sizeof(userscontext));
		/* Attach voice message to mail message ? */
		if (!(astattach = ast_variable_retrieve(cfg, "general", "attach"))) 
			astattach = "yes";
		ast_set2_flag((&globalflags), ast_true(astattach), VM_ATTACH);	

		if (!(astsearch = ast_variable_retrieve(cfg, "general", "searchcontexts")))
			astsearch = "no";
		ast_set2_flag((&globalflags), ast_true(astsearch), VM_SEARCH);

		volgain = 0.0;
		if ((volgainstr = ast_variable_retrieve(cfg, "general", "volgain")))
			sscanf(volgainstr, "%30lf", &volgain);

#ifdef ODBC_STORAGE
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
#ifdef IMAP_STORAGE
		/* IMAP server address */
		if ((imap_server = ast_variable_retrieve(cfg, "general", "imapserver"))) {
			ast_copy_string(imapserver, imap_server, sizeof(imapserver));
		} else {
			ast_copy_string(imapserver,"localhost", sizeof(imapserver));
		}
		/* IMAP server port */
		if ((imap_port = ast_variable_retrieve(cfg, "general", "imapport"))) {
			ast_copy_string(imapport, imap_port, sizeof(imapport));
		} else {
			ast_copy_string(imapport,"143", sizeof(imapport));
		}
		/* IMAP server flags */
		if ((imap_flags = ast_variable_retrieve(cfg, "general", "imapflags"))) {
			ast_copy_string(imapflags, imap_flags, sizeof(imapflags));
		}
		/* IMAP server master username */
		if ((auth_user = ast_variable_retrieve(cfg, "general", "authuser"))) {
			ast_copy_string(authuser, auth_user, sizeof(authuser));
		}
		/* IMAP server master password */
		if ((auth_password = ast_variable_retrieve(cfg, "general", "authpassword"))) {
			ast_copy_string(authpassword, auth_password, sizeof(authpassword));
		}
		/* Expunge on exit */
		if ((expunge_on_hangup = ast_variable_retrieve(cfg, "general", "expungeonhangup"))) {
			if (ast_false(expunge_on_hangup))
				expungeonhangup = 0;
			else
				expungeonhangup = 1;
		} else {
			expungeonhangup = 1;
		}
		/* IMAP voicemail folder */
		if ((imap_folder = ast_variable_retrieve(cfg, "general", "imapfolder"))) {
			ast_copy_string(imapfolder, imap_folder, sizeof(imapfolder));
		} else {
			ast_copy_string(imapfolder,"INBOX", sizeof(imapfolder));
		}

		/* There is some very unorthodox casting done here. This is due
		 * to the way c-client handles the argument passed in. It expects a 
		 * void pointer and casts the pointer directly to a long without
		 * first dereferencing it. */

		if ((imap_timeout = ast_variable_retrieve(cfg, "general", "imapreadtimeout"))) {
			mail_parameters(NIL, SET_READTIMEOUT, (void *) (atol(imap_timeout)));
		} else {
			mail_parameters(NIL, SET_READTIMEOUT, (void *) DEFAULT_IMAP_TCP_TIMEOUT);
		}

		if ((imap_timeout = ast_variable_retrieve(cfg, "general", "imapwritetimeout"))) {
			mail_parameters(NIL, SET_WRITETIMEOUT, (void *) (atol(imap_timeout)));
		} else {
			mail_parameters(NIL, SET_WRITETIMEOUT, (void *) DEFAULT_IMAP_TCP_TIMEOUT);
		}

		if ((imap_timeout = ast_variable_retrieve(cfg, "general", "imapopentimeout"))) {
			mail_parameters(NIL, SET_OPENTIMEOUT, (void *) (atol(imap_timeout)));
		} else {
			mail_parameters(NIL, SET_OPENTIMEOUT, (void *) DEFAULT_IMAP_TCP_TIMEOUT);
		}

		if ((imap_timeout = ast_variable_retrieve(cfg, "general", "imapclosetimeout"))) {
			mail_parameters(NIL, SET_CLOSETIMEOUT, (void *) (atol(imap_timeout)));
		} else {
			mail_parameters(NIL, SET_CLOSETIMEOUT, (void *) DEFAULT_IMAP_TCP_TIMEOUT);
		}

		/* Increment configuration version */
		imapversion++;
#endif
		/* External voicemail notify application */
		
		if ((notifystr = ast_variable_retrieve(cfg, "general", "externnotify"))) {
			ast_copy_string(externnotify, notifystr, sizeof(externnotify));
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "found externnotify: %s\n", externnotify);
			if (!strcasecmp(externnotify, "smdi")) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Using SMDI for external voicemail notification\n");
				if ((smdistr = ast_variable_retrieve(cfg, "general", "smdiport"))) {
					smdi_iface = ast_smdi_interface_find(smdistr);
				} else {
					if (option_debug)
						ast_log(LOG_DEBUG, "No SMDI interface set, trying default (/dev/ttyS0)\n");
					smdi_iface = ast_smdi_interface_find("/dev/ttyS0");
				}

				if (!smdi_iface) {
					ast_log(LOG_ERROR, "No valid SMDI interface specfied, disabling external voicemail notification\n");
					externnotify[0] = '\0';
				}
			}
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
			if (sscanf(s, "%30d", &x) == 1) {
				vmmaxmessage = x;
			} else {
				ast_log(LOG_WARNING, "Invalid max message time length\n");
			}
		}

		vmminmessage = 0;
		if ((s = ast_variable_retrieve(cfg, "general", "minmessage"))) {
			if (sscanf(s, "%30d", &x) == 1) {
				vmminmessage = x;
				if (maxsilence / 1000 >= vmminmessage)
					ast_log(LOG_WARNING, "maxsilence should be less than minmessage or you may get empty messages\n");
			} else {
				ast_log(LOG_WARNING, "Invalid min message time length\n");
			}
		}
		fmt = ast_variable_retrieve(cfg, "general", "format");
		if (!fmt) {
			fmt = "wav";	
		} else {
			tmp = ast_strdupa(fmt);
			fmt = ast_format_str_reduce(tmp);
			if (!fmt) {
				ast_log(LOG_ERROR, "Error processing format string, defaulting to format 'wav'\n");
				fmt = "wav";
			}
		}

		ast_copy_string(vmfmts, fmt, sizeof(vmfmts));

		skipms = 3000;
		if ((s = ast_variable_retrieve(cfg, "general", "maxgreet"))) {
			if (sscanf(s, "%30d", &x) == 1) {
				maxgreet = x;
			} else {
				ast_log(LOG_WARNING, "Invalid max message greeting length\n");
			}
		}

		if ((s = ast_variable_retrieve(cfg, "general", "skipms"))) {
			if (sscanf(s, "%30d", &x) == 1) {
				skipms = x;
			} else {
				ast_log(LOG_WARNING, "Invalid skipms value\n");
			}
		}

		maxlogins = 3;
		if ((s = ast_variable_retrieve(cfg, "general", "maxlogins"))) {
			if (sscanf(s, "%30d", &x) == 1) {
				maxlogins = x;
			} else {
				ast_log(LOG_WARNING, "Invalid max failed login attempts\n");
			}
		}

		/* Force new user to record name ? */
		if (!(astforcename = ast_variable_retrieve(cfg, "general", "forcename"))) 
			astforcename = "no";
		ast_set2_flag((&globalflags), ast_true(astforcename), VM_FORCENAME);

		/* Force new user to record greetings ? */
		if (!(astforcegreet = ast_variable_retrieve(cfg, "general", "forcegreetings"))) 
			astforcegreet = "no";
		ast_set2_flag((&globalflags), ast_true(astforcegreet), VM_FORCEGREET);

		if ((s = ast_variable_retrieve(cfg, "general", "cidinternalcontexts"))){
			if (option_debug > 2)
				ast_log(LOG_DEBUG,"VM_CID Internal context string: %s\n",s);
			stringp = ast_strdupa(s);
			for (x = 0 ; x < MAX_NUM_CID_CONTEXTS ; x++){
				if (!ast_strlen_zero(stringp)) {
					q = strsep(&stringp,",");
					while ((*q == ' ')||(*q == '\t')) /* Eat white space between contexts */
						q++;
					ast_copy_string(cidinternalcontexts[x], q, sizeof(cidinternalcontexts[x]));
					if (option_debug > 2)
						ast_log(LOG_DEBUG,"VM_CID Internal context %d: %s\n", x, cidinternalcontexts[x]);
				} else {
					cidinternalcontexts[x][0] = '\0';
				}
			}
		}
		if (!(astreview = ast_variable_retrieve(cfg, "general", "review"))){
			if (option_debug)
				ast_log(LOG_DEBUG,"VM Review Option disabled globally\n");
			astreview = "no";
		}
		ast_set2_flag((&globalflags), ast_true(astreview), VM_REVIEW);	

		/*Temperary greeting reminder */
		if (!(asttempgreetwarn = ast_variable_retrieve(cfg, "general", "tempgreetwarn"))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "VM Temperary Greeting Reminder Option disabled globally\n");
			asttempgreetwarn = "no";
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "VM Temperary Greeting Reminder Option enabled globally\n");
		}
		ast_set2_flag((&globalflags), ast_true(asttempgreetwarn), VM_TEMPGREETWARN);

		if (!(astcallop = ast_variable_retrieve(cfg, "general", "operator"))){
			if (option_debug)
				ast_log(LOG_DEBUG,"VM Operator break disabled globally\n");
			astcallop = "no";
		}
		ast_set2_flag((&globalflags), ast_true(astcallop), VM_OPERATOR);	

		if (!(astsaycid = ast_variable_retrieve(cfg, "general", "saycid"))) {
			if (option_debug)
				ast_log(LOG_DEBUG,"VM CID Info before msg disabled globally\n");
			astsaycid = "no";
		} 
		ast_set2_flag((&globalflags), ast_true(astsaycid), VM_SAYCID);	

		if (!(send_voicemail = ast_variable_retrieve(cfg,"general", "sendvoicemail"))){
			if (option_debug)
				ast_log(LOG_DEBUG,"Send Voicemail msg disabled globally\n");
			send_voicemail = "no";
		}
		ast_set2_flag((&globalflags), ast_true(send_voicemail), VM_SVMAIL);
	
		if (!(asthearenv = ast_variable_retrieve(cfg, "general", "envelope"))) {
			if (option_debug)
				ast_log(LOG_DEBUG,"ENVELOPE before msg enabled globally\n");
			asthearenv = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(asthearenv), VM_ENVELOPE);	

		if (!(astsaydurationinfo = ast_variable_retrieve(cfg, "general", "sayduration"))) {
			if (option_debug)
				ast_log(LOG_DEBUG,"Duration info before msg enabled globally\n");
			astsaydurationinfo = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(astsaydurationinfo), VM_SAYDURATION);	

		saydurationminfo = 2;
		if ((astsaydurationminfo = ast_variable_retrieve(cfg, "general", "saydurationm"))) {
			if (sscanf(astsaydurationminfo, "%30d", &x) == 1) {
				saydurationminfo = x;
			} else {
				ast_log(LOG_WARNING, "Invalid min duration for say duration\n");
			}
		}

		if (!(astskipcmd = ast_variable_retrieve(cfg, "general", "nextaftercmd"))) {
			if (option_debug)
				ast_log(LOG_DEBUG,"We are not going to skip to the next msg after save/delete\n");
			astskipcmd = "no";
		}
		ast_set2_flag((&globalflags), ast_true(astskipcmd), VM_SKIPAFTERCMD);

		if ((dialoutcxt = ast_variable_retrieve(cfg, "general", "dialout"))) {
			ast_copy_string(dialcontext, dialoutcxt, sizeof(dialcontext));
			if (option_debug)
				ast_log(LOG_DEBUG, "found dialout context: %s\n", dialcontext);
		} else {
			dialcontext[0] = '\0';	
		}
		
		if ((callbackcxt = ast_variable_retrieve(cfg, "general", "callback"))) {
			ast_copy_string(callcontext, callbackcxt, sizeof(callcontext));
			if (option_debug)
				ast_log(LOG_DEBUG, "found callback context: %s\n", callcontext);
		} else {
			callcontext[0] = '\0';
		}

		if ((exitcxt = ast_variable_retrieve(cfg, "general", "exitcontext"))) {
			ast_copy_string(exitcontext, exitcxt, sizeof(exitcontext));
			if (option_debug)
				ast_log(LOG_DEBUG, "found operator context: %s\n", exitcontext);
		} else {
			exitcontext[0] = '\0';
		}

		if (!(astdirfwd = ast_variable_retrieve(cfg, "general", "usedirectory"))) 
			astdirfwd = "no";
		ast_set2_flag((&globalflags), ast_true(astdirfwd), VM_DIRECFORWARD);	
		if ((ucfg = ast_config_load("users.conf"))) {	
			for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
				if (!ast_true(ast_config_option(ucfg, cat, "hasvoicemail")))
					continue;
				if ((cur = find_or_create(userscontext, cat))) {
					populate_defaults(cur);
					apply_options_full(cur, ast_variable_browse(ucfg, cat));
					ast_copy_string(cur->context, userscontext, sizeof(cur->context));
				}
			}
			ast_config_destroy(ucfg);
		}
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
						if ((z = ast_malloc(sizeof(*z)))) {
							char *msg_format, *timezone;
							msg_format = ast_strdupa(var->value);
							timezone = strsep(&msg_format, "|");
							if (msg_format) {
								ast_copy_string(z->name, var->name, sizeof(z->name));
								ast_copy_string(z->timezone, timezone, sizeof(z->timezone));
								ast_copy_string(z->msg_format, msg_format, sizeof(z->msg_format));
								AST_LIST_LOCK(&zones);
								AST_LIST_INSERT_HEAD(&zones, z, list);
								AST_LIST_UNLOCK(&zones);
							} else {
								ast_log(LOG_WARNING, "Invalid timezone definition at line %d\n", var->lineno);
								free(z);
							}
						} else {
							free(z);
							AST_LIST_UNLOCK(&users);
							ast_config_destroy(cfg);
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
		if ((s = ast_variable_retrieve(cfg, "general", "pbxskip")))
			ast_set2_flag((&globalflags), ast_true(s), VM_PBXSKIP);
		if ((s = ast_variable_retrieve(cfg, "general", "fromstring")))
			ast_copy_string(fromstring,s,sizeof(fromstring));
		if ((s = ast_variable_retrieve(cfg, "general", "pagerfromstring")))
			ast_copy_string(pagerfromstring,s,sizeof(pagerfromstring));
		if ((s = ast_variable_retrieve(cfg, "general", "charset")))
			ast_copy_string(charset,s,sizeof(charset));
		if ((s = ast_variable_retrieve(cfg, "general", "adsifdn"))) {
			sscanf(s, "%2x%2x%2x%2x", &tmpadsi[0], &tmpadsi[1], &tmpadsi[2], &tmpadsi[3]);
			for (x = 0; x < 4; x++) {
				memcpy(&adsifdn[x], &tmpadsi[x], 1);
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "adsisec"))) {
			sscanf(s, "%2x%2x%2x%2x", &tmpadsi[0], &tmpadsi[1], &tmpadsi[2], &tmpadsi[3]);
			for (x = 0; x < 4; x++) {
				memcpy(&adsisec[x], &tmpadsi[x], 1);
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "adsiver")))
			if (atoi(s)) {
				adsiver = atoi(s);
			}
		if ((s = ast_variable_retrieve(cfg, "general", "emailtitle"))) {
			ast_log(LOG_NOTICE, "Keyword 'emailtitle' is DEPRECATED, please use 'emailsubject' instead.\n");
			ast_copy_string(emailtitle,s,sizeof(emailtitle));
		}
		if ((s = ast_variable_retrieve(cfg, "general", "emailsubject")))
			emailsubject = ast_strdup(s);
		if ((s = ast_variable_retrieve(cfg, "general", "emailbody"))) {
			char *tmpread, *tmpwrite;
			emailbody = ast_strdup(s);

			/* substitute strings \t and \n into the appropriate characters */
			tmpread = tmpwrite = emailbody;
			while ((tmpwrite = strchr(tmpread,'\\'))) {
				switch (tmpwrite[1]) {
				case 'r':
					memmove(tmpwrite + 1, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
					*tmpwrite = '\r';
					break;
				case 'n':
					memmove(tmpwrite + 1, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
					*tmpwrite = '\n';
					break;
				case 't':
					memmove(tmpwrite + 1, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
					*tmpwrite = '\t';
					break;
				default:
					ast_log(LOG_NOTICE, "Substitution routine does not support this character: %c\n", tmpwrite[1]);
				}
				tmpread = tmpwrite + 1;
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "tz"))) {
			ast_copy_string(zonetag, s, sizeof(zonetag));
		}
		if ((s = ast_variable_retrieve(cfg, "general", "pagersubject")))
			pagersubject = ast_strdup(s);
		if ((s = ast_variable_retrieve(cfg, "general", "pagerbody"))) {
			char *tmpread, *tmpwrite;
			pagerbody = ast_strdup(s);

			/* substitute strings \t and \n into the appropriate characters */
			tmpread = tmpwrite = pagerbody;
			while ((tmpwrite = strchr(tmpread, '\\'))) {
				switch (tmpwrite[1]) {
				case 'r':
					memmove(tmpwrite + 1, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
					*tmpwrite = '\r';
					break;
				case 'n':
					memmove(tmpwrite + 1, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
					*tmpwrite = '\n';
					break;
				case 't':
					memmove(tmpwrite + 1, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
					*tmpwrite = '\t';
					break;
				default:
					ast_log(LOG_NOTICE, "Substitution routine does not support this character: %c\n", tmpwrite[1]);
				}
				tmpread = tmpwrite + 1;
			}
		}
		AST_LIST_UNLOCK(&users);
		ast_config_destroy(cfg);
		return 0;
	} else {
		AST_LIST_UNLOCK(&users);
		ast_log(LOG_WARNING, "Failed to load configuration file.\n");
		return 0;
	}
}

static int reload(void)
{
	return(load_config());
}

static int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app3);
	res |= ast_unregister_application(app4);
	ast_cli_unregister_multiple(cli_voicemail, sizeof(cli_voicemail) / sizeof(struct ast_cli_entry));
	ast_uninstall_vm_functions();
	ao2_ref(inprocess_container, -1);
	
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;
	char *adsi_loaded = ast_module_helper("", "res_adsi", 0, 0, 0, 0);
	char *smdi_loaded = ast_module_helper("", "res_smdi", 0, 0, 0, 0);
	free(adsi_loaded);
	free(smdi_loaded);

	if (!adsi_loaded) {
		ast_log(LOG_ERROR, "app_voicemail.so depends upon res_adsi.so\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!smdi_loaded) {
		ast_log(LOG_ERROR, "app_voicemail.so depends upon res_smdi.so\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(inprocess_container = ao2_container_alloc(573, inprocess_hash_fn, inprocess_cmp_fn))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	my_umask = umask(0);
	umask(my_umask);
	res = ast_register_application(app, vm_exec, synopsis_vm, descrip_vm);
	res |= ast_register_application(app2, vm_execmain, synopsis_vmain, descrip_vmain);
	res |= ast_register_application(app3, vm_box_exists, synopsis_vm_box_exists, descrip_vm_box_exists);
	res |= ast_register_application(app4, vmauthenticate, synopsis_vmauthenticate, descrip_vmauthenticate);
	if (res)
		return(res);

	if ((res=load_config())) {
		return(res);
	}

	ast_cli_register_multiple(cli_voicemail, sizeof(cli_voicemail) / sizeof(struct ast_cli_entry));

	/* compute the location of the voicemail spool directory */
	snprintf(VM_SPOOL_DIR, sizeof(VM_SPOOL_DIR), "%s/voicemail/", ast_config_AST_SPOOL_DIR);

	ast_install_vm_functions(has_voicemail, inboxcount, messagecount);

	return res;
}

static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context) 
{
	int cmd = 0;
	char destination[80] = "";
	int retries = 0;

	if (!num) {
		if (option_verbose > 2)
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
					if (option_verbose > 2)
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
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Destination number is CID number '%s'\n", num);
		ast_copy_string(destination, num, sizeof(destination));
	}

	if (!ast_strlen_zero(destination)) {
		if (destination[strlen(destination) -1 ] == '*')
			return 0; 
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Placing outgoing call to extension '%s' in context '%s' from context '%s'\n", destination, outgoing_context, chan->context);
		ast_copy_string(chan->exten, destination, sizeof(chan->exten));
		ast_copy_string(chan->context, outgoing_context, sizeof(chan->context));
		chan->priority = 0;
		return 9;
	}
	return 0;
}

static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg, int option, signed char record_gain)
{
	int res = 0;
	char filename[PATH_MAX];
	struct ast_config *msg_cfg = NULL;
	const char *origtime, *context;
	char *cid, *name, *num;
	int retries = 0;

	vms->starting = 0; 
	make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);

	/* Retrieve info from VM attribute file */
	make_file(vms->fn2, sizeof(vms->fn2), vms->curdir, vms->curmsg);
	snprintf(filename,sizeof(filename), "%s.txt", vms->fn2);
	RETRIEVE(vms->curdir, vms->curmsg, vmu);
	msg_cfg = ast_config_load(filename);
	DISPOSE(vms->curdir, vms->curmsg);
	if (!msg_cfg) {
		ast_log(LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
		return 0;
	}

	if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime"))) {
		ast_config_destroy(msg_cfg);
		return 0;
	}

	cid = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "callerid"));

	context = ast_variable_retrieve(msg_cfg, "message", "context");
	if (!strncasecmp("macro",context,5)) /* Macro names in contexts are useless for our needs */
		context = ast_variable_retrieve(msg_cfg, "message","macrocontext");
	switch (option) {
	case 3:
		if (!res)
			res = play_message_datetime(chan, vmu, origtime, filename);
		if (!res)
			res = play_message_callerid(chan, vms, cid, context, 0);

		res = 't';
		break;

	case 2:	/* Call back */

		if (ast_strlen_zero(cid))
			break;

		ast_callerid_parse(cid, &name, &num);
		while ((res > -1) && (res != 't')) {
			switch (res) {
			case '1':
				if (num) {
					/* Dial the CID number */
					res = dialout(chan, vmu, num, vmu->callback);
					if (res) {
						ast_config_destroy(msg_cfg);
						return 9;
					}
				} else {
					res = '2';
				}
				break;

			case '2':
				/* Want to enter a different number, can only do this if there's a dialout context for this user */
				if (!ast_strlen_zero(vmu->dialout)) {
					res = dialout(chan, vmu, NULL, vmu->dialout);
					if (res) {
						ast_config_destroy(msg_cfg);
						return 9;
					}
				} else {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Caller can not specify callback number - no dialout context available\n");
					res = ast_play_and_wait(chan, "vm-sorry");
				}
				ast_config_destroy(msg_cfg);
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
					if (option_verbose > 2)
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
				if (!res) {
					retries++;
					if (retries > 3)
						res = 't';
				}
				break; 
				
			}
			if (res == 't')
				res = 0;
			else if (res == '*')
				res = -1;
		}
		break;
		
	case 1:	/* Reply */
		/* Send reply directly to sender */
		if (ast_strlen_zero(cid))
			break;

		ast_callerid_parse(cid, &name, &num);
		if (!num) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "No CID number available, no reply sent\n");
			if (!res)
				res = ast_play_and_wait(chan, "vm-nonumber");
			ast_config_destroy(msg_cfg);
			return res;
		} else {
			struct ast_vm_user vmu2;
			if (find_user(&vmu2, vmu->context, num)) {
				struct leave_vm_options leave_options;
				char mailbox[AST_MAX_EXTENSION * 2 + 2];
				snprintf(mailbox, sizeof(mailbox), "%s@%s", num, vmu->context);

				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Leaving voicemail for '%s' in context '%s'\n", num, vmu->context);
				
				memset(&leave_options, 0, sizeof(leave_options));
				leave_options.record_gain = record_gain;
				res = leave_voicemail(chan, mailbox, &leave_options);
				if (!res)
					res = 't';
				ast_config_destroy(msg_cfg);
				return res;
			} else {
				/* Sender has no mailbox, can't reply */
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_3 "No mailbox number '%s' in context '%s', no reply sent\n", num, vmu->context);
				ast_play_and_wait(chan, "vm-nobox");
				res = 't';
				ast_config_destroy(msg_cfg);
				return res;
			}
		} 
		res = 0;

		break;
	}

#ifndef IMAP_STORAGE
	ast_config_destroy(msg_cfg);

	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);
		vms->heard[msg] = 1;
		res = wait_file(chan, vms, vms->fn);
	}
#endif
	return res;
}

static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt,
			int outsidecaller, struct ast_vm_user *vmu, int *duration, const char *unlockdir,
			signed char record_gain, struct vm_state *vms)
{
	/* Record message & let caller review or re-record it, or set options if applicable */
	int res = 0;
	int cmd = 0;
	int max_attempts = 3;
	int attempts = 0;
	int recorded = 0;
	int message_exists = 0;
	signed char zero_gain = 0;
	char tempfile[PATH_MAX];
	char *acceptdtmf = "#";
	char *canceldtmf = "";
	int canceleddtmf = 0;

	/* Note that urgent and private are for flagging messages as such in the future */

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_record_review called without duration pointer\n");
		return -1;
	}

	if (!outsidecaller)
		snprintf(tempfile, sizeof(tempfile), "%s.tmp", recordfile);
	else
		ast_copy_string(tempfile, recordfile, sizeof(tempfile));

	cmd = '3';  /* Want to start by recording */

	while ((cmd >= 0) && (cmd != 't')) {
		switch (cmd) {
		case '1':
			if (!message_exists) {
				/* In this case, 1 is to record a message */
				cmd = '3';
				break;
			} else {
				/* Otherwise 1 is to save the existing message */
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Saving message as is\n");
				if (!outsidecaller)
					ast_filerename(tempfile, recordfile, NULL);
				ast_stream_and_wait(chan, "vm-msgsaved", chan->language, "");
				if (!outsidecaller) {
					STORE(recordfile, vmu->mailbox, vmu->context, -1, chan, vmu, fmt, *duration, vms);
					DISPOSE(recordfile, -1);
				}
				cmd = 't';
				return res;
			}
		case '2':
			/* Review */
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Reviewing the message\n");
			cmd = ast_stream_and_wait(chan, tempfile, chan->language, AST_DIGIT_ANY);
			break;
		case '3':
			message_exists = 0;
			/* Record */
			if (recorded == 1) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Re-recording the message\n");
			} else {	
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Recording the message\n");
			}
			if (recorded && outsidecaller) {
				cmd = ast_play_and_wait(chan, INTRO);
				cmd = ast_play_and_wait(chan, "beep");
			}
			recorded = 1;
			/* After an attempt has been made to record message, we have to take care of INTRO and beep for incoming messages, but not for greetings */
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);
			if (ast_test_flag(vmu, VM_OPERATOR))
				canceldtmf = "0";
			cmd = ast_play_and_record_full(chan, playfile, tempfile, maxtime, fmt, duration, silencethreshold, maxsilence, unlockdir, acceptdtmf, canceldtmf);
			if (strchr(canceldtmf, cmd)) {
			/* need this flag here to distinguish between pressing '0' during message recording or after */
				canceleddtmf = 1;
			}
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &zero_gain, sizeof(zero_gain), 0);
			if (cmd == -1) {
				/* User has hung up, no options to give */
				if (!outsidecaller) {
					/* user was recording a greeting and they hung up, so let's delete the recording. */
					ast_filedelete(tempfile, NULL);
				}
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
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Message too short\n");
				cmd = ast_play_and_wait(chan, "vm-tooshort");
				cmd = ast_filedelete(tempfile, NULL);
				break;
			}
			else if (vmu->review && (cmd == 2 && *duration < (maxsilence + 3))) {
				/* Message is all silence */
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Nothing recorded\n");
				cmd = ast_filedelete(tempfile, NULL);
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
			cmd = ast_filedelete(tempfile, NULL);
			if (outsidecaller) {
				res = vm_exec(chan, NULL);
				return res;
			}
			else
				return 1;
#endif
		case '0':
			if (!ast_test_flag(vmu, VM_OPERATOR) || (!canceleddtmf && !outsidecaller)) {
				cmd = ast_play_and_wait(chan, "vm-sorry");
				break;
			}
			if (message_exists || recorded) {
				cmd = ast_play_and_wait(chan, "vm-saveoper");
				if (!cmd)
					cmd = ast_waitfordigit(chan, 3000);
				if (cmd == '1') {
					ast_filerename(tempfile, recordfile, NULL);
					ast_play_and_wait(chan, "vm-msgsaved");
					cmd = '0';
				} else {
					ast_play_and_wait(chan, "vm-deleted");
					DELETE(tempfile, -1, tempfile, vmu);
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
			} else {
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
	if (!outsidecaller && (cmd == -1 || cmd == 't')) {
		/* Hang up or timeout, so delete the recording. */
		ast_filedelete(tempfile, NULL);
	}

	if (cmd != 't' && outsidecaller)
		ast_play_and_wait(chan, "vm-goodbye");

	return cmd;
}

/* This is a workaround so that menuselect displays a proper description
 * AST_MODULE_INFO(, , "Comedian Mail (Voicemail System)"
 */
 
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, tdesc,
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		);
