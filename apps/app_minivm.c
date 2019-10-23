/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * and Edvina AB, Sollentuna, Sweden
 *
 * Mark Spencer <markster@digium.com> (Comedian Mail)
 * and Olle E. Johansson, Edvina.net <oej@edvina.net> (Mini-Voicemail changes)
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
 * \brief MiniVoiceMail - A Minimal Voicemail System for Asterisk
 *
 * A voicemail system in small building blocks, working together
 * based on the Comedian Mail voicemail system (app_voicemail.c).
 *
 * \par See also
 * \arg \ref Config_minivm_examples
 * \arg \ref App_minivm
 *
 * \ingroup applications
 *
 * \page App_minivm	Asterisk Mini-voicemail - A minimal voicemail system
 *
 *	This is a minimal voicemail system, building blocks for something
 *	else. It is built for multi-language systems.
 *	The current version is focused on accounts where voicemail is
 *	forwarded to users in e-mail. It's work in progress, with loosed ends hanging
 *	around from the old voicemail system and it's configuration.
 *
 *	Hopefully, we can expand this to be a full replacement of voicemail() and voicemailmain()
 *	in the future.
 *
 *	Dialplan applications
 *	- minivmRecord - record voicemail and send as e-mail ( \ref minivm_record_exec() )
 *	- minivmGreet - Play user's greeting or default greeting ( \ref minivm_greet_exec() )
 *	- minivmNotify - Notify user of message ( \ref minivm_notify_exec() )
 * 	- minivmDelete - Delete voicemail message ( \ref minivm_delete_exec() )
 *	- minivmAccMess - Record personal messages (busy | unavailable | temporary)
 *
 *	Dialplan functions
 *	- MINIVMACCOUNT() - A dialplan function
 *	- MINIVMCOUNTER() - Manage voicemail-related counters for accounts or domains
 *
 *	CLI Commands
 *	- minivm list accounts
 *	- minivm list zones
 *	- minivm list templates
 *	- minivm show stats
 *	- minivm show settings
 *
 *	Some notes
 *	- General configuration in minivm.conf
 *	- Users in realtime or configuration file
 *	- Or configured on the command line with just the e-mail address
 *
 *	Voicemail accounts are identified by userid and domain
 *
 *	Language codes are like setlocale - langcode_countrycode
 *	\note Don't use language codes like the rest of Asterisk, two letter countrycode. Use
 *	language_country like setlocale().
 *
 *	Examples:
 *		- Swedish, Sweden	sv_se
 *		- Swedish, Finland	sv_fi
 *		- English, USA		en_us
 *		- English, GB		en_gb
 *
 * \par See also
 * \arg \ref Config_minivm
 * \arg \ref Config_minivm_examples
 * \arg \ref Minivm_directories
 * \arg \ref app_minivm.c
 * \arg Comedian mail: app_voicemail.c
 * \arg \ref descrip_minivm_accmess
 * \arg \ref descrip_minivm_greet
 * \arg \ref descrip_minivm_record
 * \arg \ref descrip_minivm_delete
 * \arg \ref descrip_minivm_notify
 *
 * \arg \ref App_minivm_todo
 */
/*! \page Minivm_directories Asterisk Mini-Voicemail Directory structure
 *
 *	The directory structure for storing voicemail
 *		- AST_SPOOL_DIR - usually /var/spool/asterisk (configurable in asterisk.conf)
 *		- MVM_SPOOL_DIR - should be configurable, usually AST_SPOOL_DIR/voicemail
 *		- Domain	MVM_SPOOL_DIR/domain
 *		- Username	MVM_SPOOL_DIR/domain/username
 *			- /greet	: Recording of account owner's name
 *			- /busy		: Busy message
 *			- /unavailable  : Unavailable message
 *			- /temp		: Temporary message
 *
 *	For account anita@localdomain.xx the account directory would as a default be
 *		\b /var/spool/asterisk/voicemail/localdomain.xx/anita
 *
 *	To avoid transcoding, these sound files should be converted into several formats
 *	They are recorded in the format closest to the incoming streams
 *
 *
 * Back: \ref App_minivm
 */

/*! \page Config_minivm_examples Example dialplan for Mini-Voicemail
 * \section Example dialplan scripts for Mini-Voicemail
 *  \verbinclude extensions_minivm.conf.sample
 *
 * Back: \ref App_minivm
 */

/*! \page App_minivm_todo Asterisk Mini-Voicemail - todo
 *	- configure accounts from AMI?
 *	- test, test, test, test
 *	- fix "vm-theextensionis.gsm" voiceprompt from Allison in various formats
 *		"The extension you are calling"
 *	- For trunk, consider using channel storage for information passing between small applications
 *	- Set default directory for voicemail
 *	- New app for creating directory for account if it does not exist
 *	- Re-insert code for IMAP storage at some point
 *	- Jabber integration for notifications
 *	- Figure out how to handle video in voicemail
 *	- Integration with the HTTP server
 *	- New app for moving messages between mailboxes, and optionally mark it as "new"
 *
 *	For Asterisk 1.4/trunk
 *	- Use string fields for minivm_account
 *
 * Back: \ref App_minivm
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <dirent.h>
#include <locale.h>


#include "asterisk/paths.h"	/* use various paths */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/mwi.h"
#include "asterisk/dsp.h"
#include "asterisk/localtime.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/callerid.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/json.h"

/*** DOCUMENTATION
<application name="MinivmRecord" language="en_US">
	<synopsis>
		Receive Mini-Voicemail and forward via e-mail.
	</synopsis>
	<syntax>
		<parameter name="mailbox" required="true" argsep="@">
			<argument name="username" required="true">
				<para>Voicemail username</para>
			</argument>
			<argument name="domain" required="true">
				<para>Voicemail domain</para>
			</argument>
		</parameter>
		<parameter name="options" required="false">
			<optionlist>
				<option name="0">
					<para>Jump to the <literal>o</literal> extension in the current dialplan context.</para>
				</option>
				<option name="*">
					<para>Jump to the <literal>a</literal> extension in the current dialplan context.</para>
				</option>
				<option name="g">
					<argument name="gain">
						<para>Amount of gain to use</para>
					</argument>
					<para>Use the specified amount of gain when recording the voicemail message.
					The units are whole-number decibels (dB).</para>
				</option>
			</optionlist>
		</parameter>
	</syntax>
	<description>
		<para>This application is part of the Mini-Voicemail system, configured in <filename>minivm.conf</filename></para>
		<para>MiniVM records audio file in configured format and forwards message to e-mail and pager.</para>
		<para>If there's no user account for that address, a temporary account will be used with default options.</para>
		<para>The recorded file name and path will be stored in <variable>MVM_FILENAME</variable> and the duration
		of the message will be stored in <variable>MVM_DURATION</variable></para>
		<note><para>If the caller hangs up after the recording, the only way to send the message and clean up is to
		execute in the <literal>h</literal> extension. The application will exit if any of the following DTMF digits
		are received and the requested extension exist in the current context.</para></note>
		<variablelist>
			<variable name="MVM_RECORD_STATUS">
				<para>This is the status of the record operation</para>
				<value name="SUCCESS" />
				<value name="USEREXIT" />
				<value name="FAILED" />
			</variable>
		</variablelist>
	</description>
</application>
<application name="MinivmGreet" language="en_US">
	<synopsis>
		Play Mini-Voicemail prompts.
	</synopsis>
	<syntax>
		<parameter name="mailbox" required="true" argsep="@">
			<argument name="username" required="true">
				<para>Voicemail username</para>
			</argument>
			<argument name="domain" required="true">
				<para>Voicemail domain</para>
			</argument>
		</parameter>
		<parameter name="options" required="false">
			<optionlist>
				<option name="b">
					<para>Play the <literal>busy</literal> greeting to the calling party.</para>
				</option>
				<option name="s">
					<para>Skip the playback of instructions for leaving a message to the calling party.</para>
				</option>
				<option name="u">
					<para>Play the <literal>unavailable</literal> greeting.</para>
				</option>
			</optionlist>
		</parameter>
	</syntax>
	<description>
		<para>This application is part of the Mini-Voicemail system, configured in minivm.conf.</para>
		<para>MinivmGreet() plays default prompts or user specific prompts for an account.</para>
		<para>Busy and unavailable messages can be choosen, but will be overridden if a temporary
		message exists for the account.</para>
		<variablelist>
			<variable name="MVM_GREET_STATUS">
				<para>This is the status of the greeting playback.</para>
				<value name="SUCCESS" />
				<value name="USEREXIT" />
				<value name="FAILED" />
			</variable>
		</variablelist>
	</description>
</application>
<application name="MinivmNotify" language="en_US">
	<synopsis>
		Notify voicemail owner about new messages.
	</synopsis>
	<syntax>
		<parameter name="mailbox" required="true" argsep="@">
			<argument name="username" required="true">
				<para>Voicemail username</para>
			</argument>
			<argument name="domain" required="true">
				<para>Voicemail domain</para>
			</argument>
		</parameter>
		<parameter name="options" required="false">
			<optionlist>
				<option name="template">
					<para>E-mail template to use for voicemail notification</para>
				</option>
			</optionlist>
		</parameter>
	</syntax>
	<description>
		<para>This application is part of the Mini-Voicemail system, configured in minivm.conf.</para>
		<para>MiniVMnotify forwards messages about new voicemail to e-mail and pager. If there's no user
		account for that address, a temporary account will be used with default options (set in
		<filename>minivm.conf</filename>).</para>
		<para>If the channel variable <variable>MVM_COUNTER</variable> is set, this will be used in the message
		file name and available in the template for the message.</para>
		<para>If no template is given, the default email template will be used to send email and default pager
		template to send paging message (if the user account is configured with a paging address.</para>
		<variablelist>
			<variable name="MVM_NOTIFY_STATUS">
				<para>This is the status of the notification attempt</para>
				<value name="SUCCESS" />
				<value name="FAILED" />
			</variable>
		</variablelist>
	</description>
</application>
<application name="MinivmDelete" language="en_US">
	<synopsis>
		Delete Mini-Voicemail voicemail messages.
	</synopsis>
	<syntax>
		<parameter name="filename" required="true">
			<para>File to delete</para>
		</parameter>
	</syntax>
	<description>
		<para>This application is part of the Mini-Voicemail system, configured in <filename>minivm.conf</filename>.</para>
		<para>It deletes voicemail file set in MVM_FILENAME or given filename.</para>
		<variablelist>
			<variable name="MVM_DELETE_STATUS">
				<para>This is the status of the delete operation.</para>
				<value name="SUCCESS" />
				<value name="FAILED" />
			</variable>
		</variablelist>
	</description>
</application>

<application name="MinivmAccMess" language="en_US">
	<synopsis>
		Record account specific messages.
	</synopsis>
	<syntax>
		<parameter name="mailbox" required="true" argsep="@">
			<argument name="username" required="true">
				<para>Voicemail username</para>
			</argument>
			<argument name="domain" required="true">
				<para>Voicemail domain</para>
			</argument>
		</parameter>
		<parameter name="options" required="false">
			<optionlist>
				<option name="u">
					<para>Record the <literal>unavailable</literal> greeting.</para>
				</option>
				<option name="b">
					<para>Record the <literal>busy</literal> greeting.</para>
				</option>
				<option name="t">
					<para>Record the temporary greeting.</para>
				</option>
				<option name="n">
					<para>Account name.</para>
				</option>
			</optionlist>
		</parameter>
	</syntax>
	<description>
		<para>This application is part of the Mini-Voicemail system, configured in <filename>minivm.conf</filename>.</para>
		<para>Use this application to record account specific audio/video messages for busy, unavailable
		and temporary messages.</para>
		<para>Account specific directories will be created if they do not exist.</para>
		<variablelist>
			<variable name="MVM_ACCMESS_STATUS">
				<para>This is the result of the attempt to record the specified greeting.</para>
				<para><literal>FAILED</literal> is set if the file can't be created.</para>
				<value name="SUCCESS" />
				<value name="FAILED" />
			</variable>
		</variablelist>
	</description>
</application>
<application name="MinivmMWI" language="en_US">
	<synopsis>
		Send Message Waiting Notification to subscriber(s) of mailbox.
	</synopsis>
	<syntax>
		<parameter name="mailbox" required="true" argsep="@">
			<argument name="username" required="true">
				<para>Voicemail username</para>
			</argument>
			<argument name="domain" required="true">
				<para>Voicemail domain</para>
			</argument>
		</parameter>
		<parameter name="urgent" required="true">
			<para>Number of urgent messages in mailbox.</para>
		</parameter>
		<parameter name="new" required="true">
			<para>Number of new messages in mailbox.</para>
		</parameter>
		<parameter name="old" required="true">
			<para>Number of old messages in mailbox.</para>
		</parameter>
	</syntax>
	<description>
		<para>This application is part of the Mini-Voicemail system, configured in <filename>minivm.conf</filename>.</para>
		<para>MinivmMWI is used to send message waiting indication to any devices whose channels have
		subscribed to the mailbox passed in the first parameter.</para>
	</description>
</application>
<function name="MINIVMCOUNTER" language="en_US">
	<synopsis>
		Reads or sets counters for MiniVoicemail message.
	</synopsis>
	<syntax argsep=":">
		<parameter name="account" required="true">
			<para>If account is given and it exists, the counter is specific for the account.</para>
			<para>If account is a domain and the domain directory exists, counters are specific for a domain.</para>
		</parameter>
		<parameter name="name" required="true">
			<para>The name of the counter is a string, up to 10 characters.</para>
		</parameter>
		<parameter name="operand">
			<para>The counters never goes below zero. Valid operands for changing the value of a counter when assigning a value are:</para>
			<enumlist>
				<enum name="i"><para>Increment by value.</para></enum>
				<enum name="d"><para>Decrement by value.</para></enum>
				<enum name="s"><para>Set to value.</para></enum>
			</enumlist>
		</parameter>
	</syntax>
	<description>
		<para>The operation is atomic and the counter is locked while changing the value. The counters are stored as text files in the minivm account directories. It might be better to use realtime functions if you are using a database to operate your Asterisk.</para>
	</description>
	<see-also>
		<ref type="application">MinivmRecord</ref>
		<ref type="application">MinivmGreet</ref>
		<ref type="application">MinivmNotify</ref>
		<ref type="application">MinivmDelete</ref>
		<ref type="application">MinivmAccMess</ref>
		<ref type="application">MinivmMWI</ref>
		<ref type="function">MINIVMACCOUNT</ref>
	</see-also>
</function>
<function name="MINIVMACCOUNT" language="en_US">
	<synopsis>
		Gets MiniVoicemail account information.
	</synopsis>
	<syntax argsep=":">
		<parameter name="account" required="true" />
		<parameter name="item" required="true">
			<para>Valid items are:</para>
			<enumlist>
				<enum name="path">
					<para>Path to account mailbox (if account exists, otherwise temporary mailbox).</para>
				</enum>
				<enum name="hasaccount">
					<para>1 is static Minivm account exists, 0 otherwise.</para>
				</enum>
				<enum name="fullname">
					<para>Full name of account owner.</para>
				</enum>
				<enum name="email">
					<para>Email address used for account.</para>
				</enum>
				<enum name="etemplate">
					<para>Email template for account (default template if none is configured).</para>
				</enum>
				<enum name="ptemplate">
					<para>Pager template for account (default template if none is configured).</para>
				</enum>
				<enum name="accountcode">
					<para>Account code for the voicemail account.</para>
				</enum>
				<enum name="pincode">
					<para>Pin code for voicemail account.</para>
				</enum>
				<enum name="timezone">
					<para>Time zone for voicemail account.</para>
				</enum>
				<enum name="language">
					<para>Language for voicemail account.</para>
				</enum>
				<enum name="&lt;channel variable name&gt;">
					<para>Channel variable value (set in configuration for account).</para>
				</enum>
			</enumlist>
		</parameter>
	</syntax>
	<description>
		<para />
	</description>
	<see-also>
		<ref type="application">MinivmRecord</ref>
		<ref type="application">MinivmGreet</ref>
		<ref type="application">MinivmNotify</ref>
		<ref type="application">MinivmDelete</ref>
		<ref type="application">MinivmAccMess</ref>
		<ref type="application">MinivmMWI</ref>
		<ref type="function">MINIVMCOUNTER</ref>
	</see-also>
</function>
	<managerEvent language="en_US" name="MiniVoiceMail">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a notification is sent out by a MiniVoiceMail application</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Action">
					<para>What action was taken. Currently, this will always be <literal>SentNotification</literal></para>
				</parameter>
				<parameter name="Mailbox">
					<para>The mailbox that the notification was about, specified as <literal>mailbox</literal>@<literal>context</literal></para>
				</parameter>
				<parameter name="Counter">
					<para>A message counter derived from the <literal>MVM_COUNTER</literal> channel variable.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
***/

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif


#define MVM_REVIEW		(1 << 0)	/*!< Review message */
#define MVM_OPERATOR		(1 << 1)	/*!< Operator exit during voicemail recording */
#define MVM_REALTIME		(1 << 2)	/*!< This user is a realtime account */
#define MVM_SVMAIL		(1 << 3)
#define MVM_ENVELOPE		(1 << 4)
#define MVM_PBXSKIP		(1 << 9)
#define MVM_ALLOCED		(1 << 13)

/*! \brief Default mail command to mail voicemail. Change it with the
    mailcmd= command in voicemail.conf */
#define SENDMAIL "/usr/sbin/sendmail -t"

#define SOUND_INTRO		"vm-intro"
#define B64_BASEMAXINLINE 	256	/*!< Buffer size for Base 64 attachment encoding */
#define B64_BASELINELEN 	72	/*!< Line length for Base 64 endoded messages */
#define EOL			"\r\n"

#define MAX_DATETIME_FORMAT	512
#define MAX_NUM_CID_CONTEXTS	10

#define ERROR_LOCK_PATH		-100
#define	VOICEMAIL_DIR_MODE	0700

#define VOICEMAIL_CONFIG "minivm.conf"
#define ASTERISK_USERNAME "asterisk"	/*!< Default username for sending mail is asterisk\@localhost */

/*! \brief Message types for notification */
enum mvm_messagetype {
	MVM_MESSAGE_EMAIL,
	MVM_MESSAGE_PAGE
	/* For trunk: MVM_MESSAGE_JABBER, */
};

static char MVM_SPOOL_DIR[PATH_MAX];

/* Module declarations */
static char *app_minivm_record = "MinivmRecord"; 	/* Leave a message */
static char *app_minivm_greet = "MinivmGreet";		/* Play voicemail prompts */
static char *app_minivm_notify = "MinivmNotify";	/* Notify about voicemail by using one of several methods */
static char *app_minivm_delete = "MinivmDelete";	/* Notify about voicemail by using one of several methods */
static char *app_minivm_accmess = "MinivmAccMess";	/* Record personal voicemail messages */
static char *app_minivm_mwi = "MinivmMWI";



enum minivm_option_flags {
	OPT_SILENT =	   (1 << 0),
	OPT_BUSY_GREETING =    (1 << 1),
	OPT_UNAVAIL_GREETING = (1 << 2),
	OPT_TEMP_GREETING = (1 << 3),
	OPT_NAME_GREETING = (1 << 4),
	OPT_RECORDGAIN =  (1 << 5),
};

enum minivm_option_args {
	OPT_ARG_RECORDGAIN = 0,
	OPT_ARG_ARRAY_SIZE = 1,
};

AST_APP_OPTIONS(minivm_app_options, {
	AST_APP_OPTION('s', OPT_SILENT),
	AST_APP_OPTION('b', OPT_BUSY_GREETING),
	AST_APP_OPTION('u', OPT_UNAVAIL_GREETING),
	AST_APP_OPTION_ARG('g', OPT_RECORDGAIN, OPT_ARG_RECORDGAIN),
});

AST_APP_OPTIONS(minivm_accmess_options, {
	AST_APP_OPTION('b', OPT_BUSY_GREETING),
	AST_APP_OPTION('u', OPT_UNAVAIL_GREETING),
	AST_APP_OPTION('t', OPT_TEMP_GREETING),
	AST_APP_OPTION('n', OPT_NAME_GREETING),
});

/*!\internal
 * \brief Structure for linked list of Mini-Voicemail users: \ref minivm_accounts */
struct minivm_account {
	char username[AST_MAX_CONTEXT];	/*!< Mailbox username */
	char domain[AST_MAX_CONTEXT];	/*!< Voicemail domain */

	char pincode[10];		/*!< Secret pin code, numbers only */
	char fullname[120];		/*!< Full name, for directory app */
	char email[80];			/*!< E-mail address - override */
	char pager[80];			/*!< E-mail address to pager (no attachment) */
	char accountcode[AST_MAX_ACCOUNT_CODE];	/*!< Voicemail account account code */
	char serveremail[80];		/*!< From: Mail address */
	char externnotify[160];		/*!< Configurable notification command */
	char language[MAX_LANGUAGE];    /*!< Config: Language setting */
	char zonetag[80];		/*!< Time zone */
	char uniqueid[20];		/*!< Unique integer identifier */
	char exit[80];			/*!< Options for exiting from voicemail() */
	char attachfmt[80];		/*!< Format for voicemail audio file attachment */
	char etemplate[80];		/*!< Pager template */
	char ptemplate[80];		/*!< Voicemail format */
	unsigned int flags;		/*!< MVM_ flags */
	struct ast_variable *chanvars;	/*!< Variables for e-mail template */
	double volgain;			/*!< Volume gain for voicemails sent via e-mail */
	AST_LIST_ENTRY(minivm_account) list;
};

/*!\internal
 * \brief The list of e-mail accounts */
static AST_LIST_HEAD_STATIC(minivm_accounts, minivm_account);

/*!\internal
 * \brief Linked list of e-mail templates in various languages
 * These are used as templates for e-mails, pager messages and jabber messages
 * \ref message_templates
*/
struct minivm_template {
	char	name[80];		/*!< Template name */
	char	*body;			/*!< Body of this template */
	char	fromaddress[100];	/*!< Who's sending the e-mail? */
	char	serveremail[80];	/*!< From: Mail address */
	char	subject[100];		/*!< Subject line */
	char	charset[32];		/*!< Default character set for this template */
	char	locale[20];		/*!< Locale for setlocale() */
	char	dateformat[80];		/*!< Date format to use in this attachment */
	int	attachment;		/*!< Attachment of media yes/no - no for pager messages */
	AST_LIST_ENTRY(minivm_template) list;	/*!< List mechanics */
};

/*! \brief The list of e-mail templates */
static AST_LIST_HEAD_STATIC(message_templates, minivm_template);

/*! \brief Options for leaving voicemail with the voicemail() application */
struct leave_vm_options {
	unsigned int flags;
	signed char record_gain;
};

/*! \brief Structure for base64 encoding */
struct b64_baseio {
	int iocp;
	int iolen;
	int linelength;
	int ateof;
	unsigned char iobuf[B64_BASEMAXINLINE];
};

/*! \brief Voicemail time zones */
struct minivm_zone {
	char name[80];				/*!< Name of this time zone */
	char timezone[80];			/*!< Timezone definition */
	char msg_format[BUFSIZ];		/*!< Not used in minivm ...yet */
	AST_LIST_ENTRY(minivm_zone) list;	/*!< List mechanics */
};

/*! \brief The list of e-mail time zones */
static AST_LIST_HEAD_STATIC(minivm_zones, minivm_zone);

/*! \brief Structure for gathering statistics */
struct minivm_stats {
	int voicemailaccounts;		/*!< Number of static accounts */
	int timezones;			/*!< Number of time zones */
	int templates;			/*!< Number of templates */

	struct timeval reset;			/*!< Time for last reset */
	int receivedmessages;		/*!< Number of received messages since reset */
	struct timeval lastreceived;		/*!< Time for last voicemail sent */
};

/*! \brief Statistics for voicemail */
static struct minivm_stats global_stats;

AST_MUTEX_DEFINE_STATIC(minivmlock);	/*!< Lock to protect voicemail system */
AST_MUTEX_DEFINE_STATIC(minivmloglock);	/*!< Lock to protect voicemail system log file */

static FILE *minivmlogfile;		/*!< The minivm log file */

static int global_vmminmessage;		/*!< Minimum duration of messages */
static int global_vmmaxmessage;		/*!< Maximum duration of message */
static int global_maxsilence;		/*!< Maximum silence during recording */
static int global_maxgreet;		/*!< Maximum length of prompts  */
static int global_silencethreshold = 128;
static char global_mailcmd[160];	/*!< Configurable mail cmd */
static char global_externnotify[160]; 	/*!< External notification application */
static char global_logfile[PATH_MAX];	/*!< Global log file for messages */
static char default_vmformat[80];

static struct ast_flags globalflags = {0};	/*!< Global voicemail flags */
static int global_saydurationminfo;

static double global_volgain;	/*!< Volume gain for voicmemail via e-mail */

/*!\internal
 * \brief Default dateformat, can be overridden in configuration file */
#define DEFAULT_DATEFORMAT 	"%A, %B %d, %Y at %r"
#define DEFAULT_CHARSET		"ISO-8859-1"

/* Forward declarations */
static char *message_template_parse_filebody(const char *filename);
static char *message_template_parse_emailbody(const char *body);
static int create_vmaccount(char *name, struct ast_variable *var, int realtime);
static struct minivm_account *find_user_realtime(const char *domain, const char *username);
static char *handle_minivm_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

/*!\internal
 * \brief Create message template */
static struct minivm_template *message_template_create(const char *name)
{
	struct minivm_template *template;

	template = ast_calloc(1, sizeof(*template));
	if (!template)
		return NULL;

	/* Set some defaults for templates */
	ast_copy_string(template->name, name, sizeof(template->name));
	ast_copy_string(template->dateformat, DEFAULT_DATEFORMAT, sizeof(template->dateformat));
	ast_copy_string(template->charset, DEFAULT_CHARSET, sizeof(template->charset));
	ast_copy_string(template->subject, "New message in mailbox ${MVM_USERNAME}@${MVM_DOMAIN}", sizeof(template->subject));
	template->attachment = TRUE;

	return template;
}

/*!\internal
 * \brief Release memory allocated by message template */
static void message_template_free(struct minivm_template *template)
{
	if (template->body)
		ast_free(template->body);

	ast_free (template);
}

/*!\internal
 * \brief Build message template from configuration */
static int message_template_build(const char *name, struct ast_variable *var)
{
	struct minivm_template *template;
	int error = 0;

	template = message_template_create(name);
	if (!template) {
		ast_log(LOG_ERROR, "Out of memory, can't allocate message template object %s.\n", name);
		return -1;
	}

	while (var) {
		ast_debug(3, "Configuring template option %s = \"%s\" for template %s\n", var->name, var->value, name);
		if (!strcasecmp(var->name, "fromaddress")) {
			ast_copy_string(template->fromaddress, var->value, sizeof(template->fromaddress));
		} else if (!strcasecmp(var->name, "fromemail")) {
			ast_copy_string(template->serveremail, var->value, sizeof(template->serveremail));
		} else if (!strcasecmp(var->name, "subject")) {
			ast_copy_string(template->subject, var->value, sizeof(template->subject));
		} else if (!strcasecmp(var->name, "locale")) {
			ast_copy_string(template->locale, var->value, sizeof(template->locale));
		} else if (!strcasecmp(var->name, "attachmedia")) {
			template->attachment = ast_true(var->value);
		} else if (!strcasecmp(var->name, "dateformat")) {
			ast_copy_string(template->dateformat, var->value, sizeof(template->dateformat));
		} else if (!strcasecmp(var->name, "charset")) {
			ast_copy_string(template->charset, var->value, sizeof(template->charset));
		} else if (!strcasecmp(var->name, "templatefile")) {
			if (template->body)
				ast_free(template->body);
			template->body = message_template_parse_filebody(var->value);
			if (!template->body) {
				ast_log(LOG_ERROR, "Error reading message body definition file %s\n", var->value);
				error++;
			}
		} else if (!strcasecmp(var->name, "messagebody")) {
			if (template->body)
				ast_free(template->body);
			template->body = message_template_parse_emailbody(var->value);
			if (!template->body) {
				ast_log(LOG_ERROR, "Error parsing message body definition:\n          %s\n", var->value);
				error++;
			}
		} else {
			ast_log(LOG_ERROR, "Unknown message template configuration option \"%s=%s\"\n", var->name, var->value);
			error++;
		}
		var = var->next;
	}
	if (error)
		ast_log(LOG_ERROR, "-- %d errors found parsing message template definition %s\n", error, name);

	AST_LIST_LOCK(&message_templates);
	AST_LIST_INSERT_TAIL(&message_templates, template, list);
	AST_LIST_UNLOCK(&message_templates);

	global_stats.templates++;

	return error;
}

/*!\internal
 * \brief Find named template */
static struct minivm_template *message_template_find(const char *name)
{
	struct minivm_template *this, *res = NULL;

	if (ast_strlen_zero(name))
		return NULL;

	AST_LIST_LOCK(&message_templates);
	AST_LIST_TRAVERSE(&message_templates, this, list) {
		if (!strcasecmp(this->name, name)) {
			res = this;
			break;
		}
	}
	AST_LIST_UNLOCK(&message_templates);

	return res;
}


/*!\internal
 * \brief Clear list of templates */
static void message_destroy_list(void)
{
	struct minivm_template *this;
	AST_LIST_LOCK(&message_templates);
	while ((this = AST_LIST_REMOVE_HEAD(&message_templates, list))) {
		message_template_free(this);
	}

	AST_LIST_UNLOCK(&message_templates);
}

/*!\internal
 * \brief read buffer from file (base64 conversion) */
static int b64_inbuf(struct b64_baseio *bio, FILE *fi)
{
	int l;

	if (bio->ateof)
		return 0;

	if ((l = fread(bio->iobuf, 1, B64_BASEMAXINLINE, fi)) != B64_BASEMAXINLINE) {
		bio->ateof = 1;
		if (l == 0) {
			/* Assume EOF */
			return 0;
		}
	}

	bio->iolen = l;
	bio->iocp = 0;

	return 1;
}

/*!\internal
 * \brief read character from file to buffer (base64 conversion) */
static int b64_inchar(struct b64_baseio *bio, FILE *fi)
{
	if (bio->iocp >= bio->iolen) {
		if (!b64_inbuf(bio, fi))
			return EOF;
	}

	return bio->iobuf[bio->iocp++];
}

/*!\internal
 * \brief write buffer to file (base64 conversion) */
static int b64_ochar(struct b64_baseio *bio, int c, FILE *so)
{
	if (bio->linelength >= B64_BASELINELEN) {
		if (fputs(EOL,so) == EOF)
			return -1;

		bio->linelength= 0;
	}

	if (putc(((unsigned char) c), so) == EOF)
		return -1;

	bio->linelength++;

	return 1;
}

/*!\internal
 * \brief Encode file to base64 encoding for email attachment (base64 conversion) */
static int base_encode(char *filename, FILE *so)
{
	unsigned char dtable[B64_BASEMAXINLINE];
	int i,hiteof= 0;
	FILE *fi;
	struct b64_baseio bio;

	memset(&bio, 0, sizeof(bio));
	bio.iocp = B64_BASEMAXINLINE;

	if (!(fi = fopen(filename, "rb"))) {
		ast_log(LOG_WARNING, "Failed to open file: %s: %s\n", filename, strerror(errno));
		return -1;
	}

	for (i= 0; i<9; i++) {
		dtable[i]= 'A'+i;
		dtable[i+9]= 'J'+i;
		dtable[26+i]= 'a'+i;
		dtable[26+i+9]= 'j'+i;
	}
	for (i= 0; i < 8; i++) {
		dtable[i+18]= 'S'+i;
		dtable[26+i+18]= 's'+i;
	}
	for (i= 0; i < 10; i++) {
		dtable[52+i]= '0'+i;
	}
	dtable[62]= '+';
	dtable[63]= '/';

	while (!hiteof){
		unsigned char igroup[3], ogroup[4];
		int c,n;

		igroup[0]= igroup[1]= igroup[2]= 0;

		for (n= 0; n < 3; n++) {
			if ((c = b64_inchar(&bio, fi)) == EOF) {
				hiteof= 1;
				break;
			}
			igroup[n]= (unsigned char)c;
		}

		if (n> 0) {
			ogroup[0]= dtable[igroup[0]>>2];
			ogroup[1]= dtable[((igroup[0]&3)<<4) | (igroup[1]>>4)];
			ogroup[2]= dtable[((igroup[1]&0xF)<<2) | (igroup[2]>>6)];
			ogroup[3]= dtable[igroup[2]&0x3F];

			if (n<3) {
				ogroup[3]= '=';

				if (n<2)
					ogroup[2]= '=';
			}

			for (i= 0;i<4;i++)
				b64_ochar(&bio, ogroup[i], so);
		}
	}

	/* Put end of line - line feed */
	if (fputs(EOL, so) == EOF)
		return 0;

	fclose(fi);

	return 1;
}

static int get_date(char *s, int len)
{
	struct ast_tm tm;
	struct timeval now = ast_tvnow();

	ast_localtime(&now, &tm, NULL);
	return ast_strftime(s, len, "%a %b %e %r %Z %Y", &tm);
}


/*!\internal
 * \brief Free user structure - if it's allocated */
static void free_user(struct minivm_account *vmu)
{
	if (vmu->chanvars)
		ast_variables_destroy(vmu->chanvars);
	ast_free(vmu);
}



/*!\internal
 * \brief Prepare for voicemail template by adding channel variables
 * to the channel
*/
static void prep_email_sub_vars(struct ast_channel *channel, const struct minivm_account *vmu, const char *cidnum, const char *cidname, const char *dur, const char *date, const char *counter)
{
	char callerid[256];
	struct ast_variable *var;

	if (!channel) {
		ast_log(LOG_ERROR, "No allocated channel, giving up...\n");
		return;
	}

	for (var = vmu->chanvars ; var ; var = var->next) {
		pbx_builtin_setvar_helper(channel, var->name, var->value);
	}

	/* Prepare variables for substition in email body and subject */
	pbx_builtin_setvar_helper(channel, "MVM_NAME", vmu->fullname);
	pbx_builtin_setvar_helper(channel, "MVM_DUR", dur);
	pbx_builtin_setvar_helper(channel, "MVM_DOMAIN", vmu->domain);
	pbx_builtin_setvar_helper(channel, "MVM_USERNAME", vmu->username);
	pbx_builtin_setvar_helper(channel, "MVM_CALLERID", ast_callerid_merge(callerid, sizeof(callerid), cidname, cidnum, "Unknown Caller"));
	pbx_builtin_setvar_helper(channel, "MVM_CIDNAME", (cidname ? cidname : "an unknown caller"));
	pbx_builtin_setvar_helper(channel, "MVM_CIDNUM", (cidnum ? cidnum : "an unknown caller"));
	pbx_builtin_setvar_helper(channel, "MVM_DATE", date);
	if (!ast_strlen_zero(counter))
		pbx_builtin_setvar_helper(channel, "MVM_COUNTER", counter);
}

/*!\internal
 * \brief Set default values for Mini-Voicemail users */
static void populate_defaults(struct minivm_account *vmu)
{
	ast_copy_flags(vmu, (&globalflags), AST_FLAGS_ALL);
	ast_copy_string(vmu->attachfmt, default_vmformat, sizeof(vmu->attachfmt));
	vmu->volgain = global_volgain;
}

/*!\internal
 * \brief Allocate new vm user and set default values */
static struct minivm_account *mvm_user_alloc(void)
{
	struct minivm_account *new;

	new = ast_calloc(1, sizeof(*new));
	if (!new)
		return NULL;
	populate_defaults(new);

	return new;
}


/*!\internal
 * \brief Clear list of users */
static void vmaccounts_destroy_list(void)
{
	struct minivm_account *this;
	AST_LIST_LOCK(&minivm_accounts);
	while ((this = AST_LIST_REMOVE_HEAD(&minivm_accounts, list)))
		ast_free(this);
	AST_LIST_UNLOCK(&minivm_accounts);
}


/*!\internal
 * \brief Find user from static memory object list */
static struct minivm_account *find_account(const char *domain, const char *username, int createtemp)
{
	struct minivm_account *vmu = NULL, *cur;


	if (ast_strlen_zero(domain) || ast_strlen_zero(username)) {
		ast_log(LOG_NOTICE, "No username or domain? \n");
		return NULL;
	}
	ast_debug(3, "Looking for voicemail user %s in domain %s\n", username, domain);

	AST_LIST_LOCK(&minivm_accounts);
	AST_LIST_TRAVERSE(&minivm_accounts, cur, list) {
		/* Is this the voicemail account we're looking for? */
		if (!strcasecmp(domain, cur->domain) && !strcasecmp(username, cur->username))
			break;
	}
	AST_LIST_UNLOCK(&minivm_accounts);

	if (cur) {
		ast_debug(3, "Found account for %s@%s\n", username, domain);
		vmu = cur;

	} else
		vmu = find_user_realtime(domain, username);

	if (createtemp && !vmu) {
		/* Create a temporary user, send e-mail and be gone */
		vmu = mvm_user_alloc();
		ast_set2_flag(vmu, TRUE, MVM_ALLOCED);
		if (vmu) {
			ast_copy_string(vmu->username, username, sizeof(vmu->username));
			ast_copy_string(vmu->domain, domain, sizeof(vmu->domain));
			ast_debug(1, "Created temporary account\n");
		}

	}
	return vmu;
}

/*!\internal
 * \brief Find user in realtime storage
 * \return pointer to minivm_account structure
*/
static struct minivm_account *find_user_realtime(const char *domain, const char *username)
{
	struct ast_variable *var;
	struct minivm_account *retval;
	char name[MAXHOSTNAMELEN];

	retval = mvm_user_alloc();
	if (!retval)
		return NULL;

	if (username)
		ast_copy_string(retval->username, username, sizeof(retval->username));

	populate_defaults(retval);
	var = ast_load_realtime("minivm", "username", username, "domain", domain, SENTINEL);

	if (!var) {
		ast_free(retval);
		return NULL;
	}

	snprintf(name, sizeof(name), "%s@%s", username, domain);
	create_vmaccount(name, var, TRUE);

	ast_variables_destroy(var);
	return retval;
}

/*!\internal
 * \brief Check if the string would need encoding within the MIME standard, to
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

/*!\internal
 * \brief Encode a string according to the MIME rules for encoding strings
 * that are not 7-bit clean or contain control characters.
 *
 * Additionally, if the encoded string would exceed the MIME limit of 76
 * characters per line, then the encoding will be broken up into multiple
 * sections, separated by a space character, in order to facilitate
 * breaking up the associated header across multiple lines.
 *
 * \param end An expandable buffer for holding the result
 * \param maxlen \see ast_str
 * \param charset Character set in which the result should be encoded
 * \param start A string to be encoded
 * \param preamble The length of the first line already used for this string,
 * to ensure that each line maintains a maximum length of 76 chars.
 * \param postamble the length of any additional characters appended to the
 * line, used to ensure proper field wrapping.
 * \return The encoded string.
 */
static const char *ast_str_encode_mime(struct ast_str **end, ssize_t maxlen, const char *charset, const char *start, size_t preamble, size_t postamble)
{
	struct ast_str *tmp = ast_str_alloca(80);
	int first_section = 1;

	ast_str_reset(*end);
	ast_str_set(&tmp, -1, "=?%s?Q?", charset);
	for (; *start; start++) {
		int need_encoding = 0;
		if (*start < 33 || *start > 126 || strchr("()<>@,:;/\"[]?.=_", *start)) {
			need_encoding = 1;
		}
		if ((first_section && need_encoding && preamble + ast_str_strlen(tmp) > 70) ||
			(first_section && !need_encoding && preamble + ast_str_strlen(tmp) > 72) ||
			(!first_section && need_encoding && ast_str_strlen(tmp) > 70) ||
			(!first_section && !need_encoding && ast_str_strlen(tmp) > 72)) {
			/* Start new line */
			ast_str_append(end, maxlen, "%s%s?=", first_section ? "" : " ", ast_str_buffer(tmp));
			ast_str_set(&tmp, -1, "=?%s?Q?", charset);
			first_section = 0;
		}
		if (need_encoding && *start == ' ') {
			ast_str_append(&tmp, -1, "_");
		} else if (need_encoding) {
			ast_str_append(&tmp, -1, "=%hhX", *start);
		} else {
			ast_str_append(&tmp, -1, "%c", *start);
		}
	}
	ast_str_append(end, maxlen, "%s%s?=%s", first_section ? "" : " ", ast_str_buffer(tmp), ast_str_strlen(tmp) + postamble > 74 ? " " : "");
	return ast_str_buffer(*end);
}

/*!\internal
 * \brief Wraps a character sequence in double quotes, escaping occurences of quotes within the string.
 * \param from The string to work with.
 * \param buf The destination buffer to write the modified quoted string.
 * \param maxlen Always zero.  \see ast_str
 *
 * \return The destination string with quotes wrapped on it (the to field).
 */
static const char *ast_str_quote(struct ast_str **buf, ssize_t maxlen, const char *from)
{
	const char *ptr;

	/* We're only ever passing 0 to maxlen, so short output isn't possible */
	ast_str_set(buf, maxlen, "\"");
	for (ptr = from; *ptr; ptr++) {
		if (*ptr == '"' || *ptr == '\\') {
			ast_str_append(buf, maxlen, "\\%c", *ptr);
		} else {
			ast_str_append(buf, maxlen, "%c", *ptr);
		}
	}
	ast_str_append(buf, maxlen, "\"");

	return ast_str_buffer(*buf);
}

/*!\internal
 * \brief Send voicemail with audio file as an attachment */
static int sendmail(struct minivm_template *template, struct minivm_account *vmu, char *cidnum, char *cidname, const char *filename, char *format, int duration, int attach_user_voicemail, enum mvm_messagetype type, const char *counter)
{
	RAII_VAR(struct ast_str *, str1, ast_str_create(16), ast_free);
	RAII_VAR(struct ast_str *, str2, ast_str_create(16), ast_free);
	FILE *p = NULL;
	int pfd;
	char email[256] = "";
	char who[256] = "";
	char date[256];
	char bound[256];
	char fname[PATH_MAX];
	char dur[PATH_MAX];
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char mail_cmd_buffer[PATH_MAX];
	char sox_gain_tmpdir[PATH_MAX] = ""; /* Only used with volgain */
	char *file_to_delete = NULL, *dir_to_delete = NULL;
	struct timeval now;
	struct ast_tm tm;
	struct minivm_zone *the_zone = NULL;
	struct ast_channel *chan = NULL;
	char *fromaddress;
	char *fromemail;
	int res = -1;

	if (!str1 || !str2) {
		return -1;
	}

	if (type == MVM_MESSAGE_EMAIL) {
		if (vmu && !ast_strlen_zero(vmu->email)) {
			ast_copy_string(email, vmu->email, sizeof(email));
		} else if (!ast_strlen_zero(vmu->username) && !ast_strlen_zero(vmu->domain))
			snprintf(email, sizeof(email), "%s@%s", vmu->username, vmu->domain);
	} else if (type == MVM_MESSAGE_PAGE) {
		ast_copy_string(email, vmu->pager, sizeof(email));
	}

	if (ast_strlen_zero(email)) {
		ast_log(LOG_WARNING, "No address to send message to.\n");
		return -1;
	}

	ast_debug(3, "Sending mail to %s@%s - Using template %s\n", vmu->username, vmu->domain, template->name);

	if (!strcmp(format, "wav49"))
		format = "WAV";

	/* If we have a gain option, process it now with sox */
	if (type == MVM_MESSAGE_EMAIL && (vmu->volgain < -.001 || vmu->volgain > .001) ) {
		char sox_gain_cmd[PATH_MAX];

		ast_copy_string(sox_gain_tmpdir, "/tmp/minivm-gain-XXXXXX", sizeof(sox_gain_tmpdir));
		ast_debug(3, "sox_gain_tmpdir: %s\n", sox_gain_tmpdir);
		if (!mkdtemp(sox_gain_tmpdir)) {
			ast_log(LOG_WARNING, "Failed to create temporary directory for volgain: %d\n", errno);
			return -1;
		}
		snprintf(fname, sizeof(fname), "%s/output.%s", sox_gain_tmpdir, format);
		snprintf(sox_gain_cmd, sizeof(sox_gain_cmd), "sox -v %.4f %s.%s %s", vmu->volgain, filename, format, fname);
		ast_safe_system(sox_gain_cmd);
		ast_debug(3, "VOLGAIN: Stored at: %s.%s - Level: %.4f - Mailbox: %s\n", filename, format, vmu->volgain, vmu->username);

		/* Mark some things for deletion */
		file_to_delete = fname;
		dir_to_delete = sox_gain_tmpdir;
	} else {
		snprintf(fname, sizeof(fname), "%s.%s", filename, format);
	}

	if (template->attachment)
		ast_debug(1, "Attaching file '%s', format '%s', uservm is '%d'\n", fname, format, attach_user_voicemail);

	/* Make a temporary file instead of piping directly to sendmail, in case the mail
	   command hangs */
	pfd = mkstemp(tmp);
	if (pfd > -1) {
		p = fdopen(pfd, "w");
		if (!p) {
			close(pfd);
			pfd = -1;
		}
		ast_debug(1, "Opening temp file for e-mail: %s\n", tmp);
	}
	if (!p) {
		ast_log(LOG_WARNING, "Unable to open temporary file '%s'\n", tmp);
		goto out;
	}
	/* Allocate channel used for chanvar substitution */
	chan = ast_dummy_channel_alloc();
	if (!chan) {
		goto out;
	}

	snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);

	/* Does this user have a timezone specified? */
	if (!ast_strlen_zero(vmu->zonetag)) {
		/* Find the zone in the list */
		struct minivm_zone *z;
		AST_LIST_LOCK(&minivm_zones);
		AST_LIST_TRAVERSE(&minivm_zones, z, list) {
			if (strcmp(z->name, vmu->zonetag))
				continue;
			the_zone = z;
		}
		AST_LIST_UNLOCK(&minivm_zones);
	}

	now = ast_tvnow();
	ast_localtime(&now, &tm, the_zone ? the_zone->timezone : NULL);
	ast_strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", &tm);

	/* Start printing the email to the temporary file */
	fprintf(p, "Date: %s\n", date);

	/* Set date format for voicemail mail */
	ast_strftime(date, sizeof(date), template->dateformat, &tm);

	/* Populate channel with channel variables for substitution */
	prep_email_sub_vars(chan, vmu, cidnum, cidname, dur, date, counter);

	/* Find email address to use */
	/* If there's a server e-mail address in the account, use that, otherwise template */
	fromemail = ast_strlen_zero(vmu->serveremail) ?  template->serveremail : vmu->serveremail;

	/* Find name to user for server e-mail */
	fromaddress = ast_strlen_zero(template->fromaddress) ? "" : template->fromaddress;

	/* If needed, add hostname as domain */
	if (ast_strlen_zero(fromemail))
		fromemail = "asterisk";

	if (strchr(fromemail, '@'))
		ast_copy_string(who, fromemail, sizeof(who));
	else  {
		char host[MAXHOSTNAMELEN];
		gethostname(host, sizeof(host)-1);
		snprintf(who, sizeof(who), "%s@%s", fromemail, host);
	}

	if (ast_strlen_zero(fromaddress)) {
		fprintf(p, "From: Asterisk PBX <%s>\n", who);
	} else {
		ast_debug(4, "Fromaddress template: %s\n", fromaddress);
		ast_str_substitute_variables(&str1, 0, chan, fromaddress);
		if (check_mime(ast_str_buffer(str1))) {
			int first_line = 1;
			char *ptr;
			ast_str_encode_mime(&str2, 0, template->charset, ast_str_buffer(str1), strlen("From: "), strlen(who) + 3);
			while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
				*ptr = '\0';
				fprintf(p, "%s %s\n", first_line ? "From:" : "", ast_str_buffer(str2));
				first_line = 0;
				/* Substring is smaller, so this will never grow */
				ast_str_set(&str2, 0, "%s", ptr + 1);
			}
			fprintf(p, "%s %s <%s>\n", first_line ? "From:" : "", ast_str_buffer(str2), who);
		} else {
			fprintf(p, "From: %s <%s>\n", ast_str_quote(&str2, 0, ast_str_buffer(str1)), who);
		}
	}

	fprintf(p, "Message-ID: <Asterisk-%u-%s-%d-%s>\n", (unsigned int)ast_random(), vmu->username, (int)getpid(), who);

	if (ast_strlen_zero(vmu->email)) {
		snprintf(email, sizeof(email), "%s@%s", vmu->username, vmu->domain);
	} else {
		ast_copy_string(email, vmu->email, sizeof(email));
	}

	if (check_mime(vmu->fullname)) {
		int first_line = 1;
		char *ptr;
		ast_str_encode_mime(&str2, 0, template->charset, vmu->fullname, strlen("To: "), strlen(email) + 3);
		while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
			*ptr = '\0';
			fprintf(p, "%s %s\n", first_line ? "To:" : "", ast_str_buffer(str2));
			first_line = 0;
			/* Substring is smaller, so this will never grow */
			ast_str_set(&str2, 0, "%s", ptr + 1);
		}
		fprintf(p, "%s %s <%s>\n", first_line ? "To:" : "", ast_str_buffer(str2), email);
	} else {
		fprintf(p, "To: %s <%s>\n", ast_str_quote(&str2, 0, vmu->fullname), email);
	}

	if (!ast_strlen_zero(template->subject)) {
		ast_str_substitute_variables(&str1, 0, chan, template->subject);
		if (check_mime(ast_str_buffer(str1))) {
			int first_line = 1;
			char *ptr;
			ast_str_encode_mime(&str2, 0, template->charset, ast_str_buffer(str1), strlen("Subject: "), 0);
			while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
				*ptr = '\0';
				fprintf(p, "%s %s\n", first_line ? "Subject:" : "", ast_str_buffer(str2));
				first_line = 0;
				/* Substring is smaller, so this will never grow */
				ast_str_set(&str2, 0, "%s", ptr + 1);
			}
			fprintf(p, "%s %s\n", first_line ? "Subject:" : "", ast_str_buffer(str2));
		} else {
			fprintf(p, "Subject: %s\n", ast_str_buffer(str1));
		}
	} else {
		fprintf(p, "Subject: New message in mailbox %s@%s\n", vmu->username, vmu->domain);
		ast_debug(1, "Using default subject for this email \n");
	}

	if (DEBUG_ATLEAST(3))
		fprintf(p, "X-Asterisk-debug: template %s user account %s@%s\n", template->name, vmu->username, vmu->domain);
	fprintf(p, "MIME-Version: 1.0\n");

	/* Something unique. */
	snprintf(bound, sizeof(bound), "voicemail_%s%d%u", vmu->username, (int)getpid(), (unsigned int)ast_random());

	fprintf(p, "Content-Type: multipart/mixed; boundary=\"%s\"\n\n\n", bound);

	fprintf(p, "--%s\n", bound);
	fprintf(p, "Content-Type: text/plain; charset=%s\nContent-Transfer-Encoding: 8bit\n\n", template->charset);
	if (!ast_strlen_zero(template->body)) {
		ast_str_substitute_variables(&str1, 0, chan, template->body);
		ast_debug(3, "Message now: %s\n-----\n", ast_str_buffer(str1));
		fprintf(p, "%s\n", ast_str_buffer(str1));
	} else {
		fprintf(p, "Dear %s:\n\n\tJust wanted to let you know you were just left a %s long message \n"
			"in mailbox %s from %s, on %s so you might\n"
			"want to check it when you get a chance.  Thanks!\n\n\t\t\t\t--Asterisk\n\n", vmu->fullname,
			dur,  vmu->username, (cidname ? cidname : (cidnum ? cidnum : "an unknown caller")), date);
		ast_debug(3, "Using default message body (no template)\n-----\n");
	}
	/* Eww. We want formats to tell us their own MIME type */
	if (template->attachment) {
		char *ctype = "audio/x-";
		ast_debug(3, "Attaching file to message: %s\n", fname);
		if (!strcasecmp(format, "ogg"))
			ctype = "application/";

		fprintf(p, "--%s\n", bound);
		fprintf(p, "Content-Type: %s%s; name=\"voicemailmsg.%s\"\n", ctype, format, format);
		fprintf(p, "Content-Transfer-Encoding: base64\n");
		fprintf(p, "Content-Description: Voicemail sound attachment.\n");
		fprintf(p, "Content-Disposition: attachment; filename=\"voicemail%s.%s\"\n\n", counter ? counter : "", format);

		base_encode(fname, p);
		fprintf(p, "\n\n--%s--\n.\n", bound);
	}
	fclose(p);

	chan = ast_channel_unref(chan);

	if (file_to_delete && dir_to_delete) {
		/* We can't delete these files ourselves because the mail command will execute in
		   the background and we'll end up deleting them out from under it. */
		res = snprintf(mail_cmd_buffer, sizeof(mail_cmd_buffer),
					   "( %s < %s ; rm -f %s %s ; rmdir %s ) &",
					   global_mailcmd, tmp, tmp, file_to_delete, dir_to_delete);
	} else {
		res = snprintf(mail_cmd_buffer, sizeof(mail_cmd_buffer),
					   "( %s < %s ; rm -f %s ) &",
					   global_mailcmd, tmp, tmp);
	}

	if (res < sizeof(mail_cmd_buffer)) {
		file_to_delete = dir_to_delete = NULL;
	} else {
		ast_log(LOG_ERROR, "Could not send message, command line too long\n");
		res = -1;
		goto out;
	}

	ast_safe_system(mail_cmd_buffer);
	ast_debug(1, "Sent message to %s with command '%s'%s\n", vmu->email, global_mailcmd, template->attachment ? " - (media attachment)" : "");
	ast_debug(3, "Actual command used: %s\n", mail_cmd_buffer);

	res = 0;

out:
	if (file_to_delete) {
		unlink(file_to_delete);
	}

	if (dir_to_delete) {
		rmdir(dir_to_delete);
	}

	return res;
}

/*!\internal
 * \brief Create directory based on components */
static int make_dir(char *dest, int len, const char *domain, const char *username, const char *folder)
{
	return snprintf(dest, len, "%s%s/%s%s%s", MVM_SPOOL_DIR, domain, username, ast_strlen_zero(folder) ? "" : "/", folder ? folder : "");
}

/*!\internal
 * \brief Checks if directory exists. Does not create directory, but builds string in dest
 * \param dest    String. base directory.
 * \param len    Int. Length base directory string.
 * \param domain String. Ignored if is null or empty string.
 * \param username String. Ignored if is null or empty string.
 * \param folder  String. Ignored if is null or empty string.
 * \return 0 on failure, 1 on success.
 */
static int check_dirpath(char *dest, int len, char *domain, char *username, char *folder)
{
	struct stat filestat;
	make_dir(dest, len, domain, username, folder ? folder : "");
	if (stat(dest, &filestat)== -1)
		return FALSE;
	else
		return TRUE;
}

/*!\internal
 * \brief basically mkdir -p $dest/$domain/$username/$folder
 * \param dest    String. base directory.
 * \param len     Length of directory string
 * \param domain  String. Ignored if is null or empty string.
 * \param folder  String. Ignored if is null or empty string.
 * \param username  String. Ignored if is null or empty string.
 * \return -1 on failure, 0 on success.
 */
static int create_dirpath(char *dest, int len, char *domain, char *username, char *folder)
{
	int res;
	make_dir(dest, len, domain, username, folder);
	if ((res = ast_mkdir(dest, 0777))) {
		ast_log(LOG_WARNING, "ast_mkdir '%s' failed: %s\n", dest, strerror(res));
		return -1;
	}
	ast_debug(2, "Creating directory for %s@%s folder %s : %s\n", username, domain, folder, dest);
	return 0;
}


/*!\internal
 * \brief Play intro message before recording voicemail
 */
static int invent_message(struct ast_channel *chan, char *domain, char *username, int busy, char *ecodes)
{
	int res;
	char fn[PATH_MAX];

	ast_debug(2, "Still preparing to play message ...\n");

	snprintf(fn, sizeof(fn), "%s%s/%s/greet", MVM_SPOOL_DIR, domain, username);

	if (ast_fileexists(fn, NULL, NULL) > 0) {
		res = ast_streamfile(chan, fn, ast_channel_language(chan));
		if (res)
			return -1;
		res = ast_waitstream(chan, ecodes);
		if (res)
			return res;
	} else {
		int numericusername = 1;
		char *i = username;

		ast_debug(2, "No personal prompts. Using default prompt set for language\n");

		while (*i)  {
			ast_debug(2, "Numeric? Checking %c\n", *i);
			if (!isdigit(*i)) {
				numericusername = FALSE;
				break;
			}
			i++;
		}

		if (numericusername) {
			if (ast_streamfile(chan, "vm-theperson", ast_channel_language(chan)))
				return -1;
			if ((res = ast_waitstream(chan, ecodes)))
				return res;

			res = ast_say_digit_str(chan, username, ecodes, ast_channel_language(chan));
			if (res)
				return res;
		} else {
			if (ast_streamfile(chan, "vm-theextensionis", ast_channel_language(chan)))
				return -1;
			if ((res = ast_waitstream(chan, ecodes)))
				return res;
		}
	}

	res = ast_streamfile(chan, busy ? "vm-isonphone" : "vm-isunavail", ast_channel_language(chan));
	if (res)
		return -1;
	res = ast_waitstream(chan, ecodes);
	return res;
}

/*!\internal
 * \brief Delete media files and attribute file */
static int vm_delete(char *file)
{
	int res;

	ast_debug(1, "Deleting voicemail file %s\n", file);

	res = unlink(file);	/* Remove the meta data file */
	res |=  ast_filedelete(file, NULL);	/* remove the media file */
	return res;
}


/*!\internal
 * \brief Record voicemail message & let caller review or re-record it, or set options if applicable */
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt,
			      int outsidecaller, struct minivm_account *vmu, int *duration, int *sound_duration, const char *unlockdir,
			      signed char record_gain)
{
	int cmd = 0;
	int max_attempts = 3;
	int attempts = 0;
	int recorded = 0;
	int message_exists = 0;
	signed char zero_gain = 0;
	char *acceptdtmf = "#";
	char *canceldtmf = "";

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
			ast_verb(3, "Saving message as is\n");
			ast_stream_and_wait(chan, "vm-msgsaved", "");
			cmd = 't';
			break;
		case '2':
			/* Review */
			ast_verb(3, "Reviewing the message\n");
			ast_streamfile(chan, recordfile, ast_channel_language(chan));
			cmd = ast_waitstream(chan, AST_DIGIT_ANY);
			break;
		case '3':
			message_exists = 0;
			/* Record */
			if (recorded == 1)
				ast_verb(3, "Re-recording the message\n");
			else
				ast_verb(3, "Recording the message\n");
			if (recorded && outsidecaller)
				cmd = ast_play_and_wait(chan, "beep");
			recorded = 1;
			/* After an attempt has been made to record message, we have to take care of INTRO and beep for incoming messages, but not for greetings */
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);
			if (ast_test_flag(vmu, MVM_OPERATOR))
				canceldtmf = "0";
			cmd = ast_play_and_record_full(chan, playfile, recordfile, maxtime, fmt, duration, sound_duration, 0, global_silencethreshold, global_maxsilence, unlockdir, acceptdtmf, canceldtmf, 0, AST_RECORD_IF_EXISTS_OVERWRITE);
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &zero_gain, sizeof(zero_gain), 0);
			if (cmd == -1) /* User has hung up, no options to give */
				return cmd;
			if (cmd == '0')
				break;
 			else if (cmd == '*')
 				break;
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
 		case '0':
			if(!ast_test_flag(vmu, MVM_OPERATOR)) {
 				cmd = ast_play_and_wait(chan, "vm-sorry");
 				break;
			}
			if (message_exists || recorded) {
				cmd = ast_play_and_wait(chan, "vm-saveoper");
				if (!cmd)
					cmd = ast_waitfordigit(chan, 3000);
				if (cmd == '1') {
					ast_play_and_wait(chan, "vm-msgsaved");
					cmd = '0';
				} else {
					ast_play_and_wait(chan, "vm-deleted");
					vm_delete(recordfile);
					cmd = '0';
				}
			}
			return cmd;
 		default:
			/* If the caller is an ouside caller, and the review option is enabled,
			   allow them to review the message, but let the owner of the box review
			   their OGM's */
			if (outsidecaller && !ast_test_flag(vmu, MVM_REVIEW))
				return cmd;
 			if (message_exists) {
 				cmd = ast_play_and_wait(chan, "vm-review");
 			} else {
 				cmd = ast_play_and_wait(chan, "vm-torerecord");
 				if (!cmd)
 					cmd = ast_waitfordigit(chan, 600);
 			}

 			if (!cmd && outsidecaller && ast_test_flag(vmu, MVM_OPERATOR)) {
 				cmd = ast_play_and_wait(chan, "vm-reachoper");
 				if (!cmd)
 					cmd = ast_waitfordigit(chan, 600);
 			}
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

/*! \brief Run external notification for voicemail message */
static void run_externnotify(struct ast_channel *chan, struct minivm_account *vmu)
{
	char fquser[AST_MAX_CONTEXT * 2];
	char *argv[5] = { NULL };
	struct ast_party_caller *caller;
	char *cid;
	int idx;

	if (ast_strlen_zero(vmu->externnotify) && ast_strlen_zero(global_externnotify)) {
		return;
	}

	snprintf(fquser, sizeof(fquser), "%s@%s", vmu->username, vmu->domain);

	caller = ast_channel_caller(chan);
	idx = 0;
	argv[idx++] = ast_strlen_zero(vmu->externnotify) ? global_externnotify : vmu->externnotify;
	argv[idx++] = fquser;
	cid = S_COR(caller->id.name.valid, caller->id.name.str, NULL);
	if (cid) {
		argv[idx++] = cid;
	}
	cid = S_COR(caller->id.number.valid, caller->id.number.str, NULL);
	if (cid) {
		argv[idx++] = cid;
	}
	argv[idx] = NULL;

	ast_debug(1, "Executing: %s %s %s %s\n",
		argv[0], argv[1], argv[2] ?: "", argv[3] ?: "");
	ast_safe_execvp(1, argv[0], argv);
}

/*!\internal
 * \brief Send message to voicemail account owner */
static int notify_new_message(struct ast_channel *chan, const char *templatename, struct minivm_account *vmu, const char *filename, long duration, const char *format, char *cidnum, char *cidname)
{
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct ast_mwi_state *, mwi_state, NULL, ao2_cleanup);
	char *stringp;
	struct minivm_template *etemplate;
	char *messageformat;
	int res = 0;
	char oldlocale[100];
	const char *counter;

	if (!ast_strlen_zero(vmu->attachfmt)) {
		if (strstr(format, vmu->attachfmt)) {
			format = vmu->attachfmt;
		} else {
			ast_log(LOG_WARNING, "Attachment format '%s' is not one of the recorded formats '%s'.  Falling back to default format for '%s@%s'.\n", vmu->attachfmt, format, vmu->username, vmu->domain);
		}
	}

	etemplate = message_template_find(vmu->etemplate);
	if (!etemplate)
		etemplate = message_template_find(templatename);
	if (!etemplate)
		etemplate = message_template_find("email-default");

	/* Attach only the first format */
	stringp = messageformat = ast_strdupa(format);
	strsep(&stringp, "|");

	if (!ast_strlen_zero(etemplate->locale)) {
		char *new_locale;
		ast_copy_string(oldlocale, setlocale(LC_TIME, NULL), sizeof(oldlocale));
		ast_debug(2, "Changing locale from %s to %s\n", oldlocale, etemplate->locale);
		new_locale = setlocale(LC_TIME, etemplate->locale);
		if (new_locale == NULL) {
			ast_log(LOG_WARNING, "-_-_- Changing to new locale did not work. Locale: %s\n", etemplate->locale);
		}
	}



	/* Read counter if available */
	ast_channel_lock(chan);
	if ((counter = pbx_builtin_getvar_helper(chan, "MVM_COUNTER"))) {
		counter = ast_strdupa(counter);
	}
	ast_channel_unlock(chan);

	if (ast_strlen_zero(counter)) {
		ast_debug(2, "MVM_COUNTER not found\n");
	} else {
		ast_debug(2, "MVM_COUNTER found - will use it with value %s\n", counter);
	}

	res = sendmail(etemplate, vmu, cidnum, cidname, filename, messageformat, duration, etemplate->attachment, MVM_MESSAGE_EMAIL, counter);

	if (res == 0 && !ast_strlen_zero(vmu->pager))  {
		/* Find template for paging */
		etemplate = message_template_find(vmu->ptemplate);
		if (!etemplate)
			etemplate = message_template_find("pager-default");

		if (!ast_strlen_zero(etemplate->locale)) {
			ast_copy_string(oldlocale, setlocale(LC_TIME, ""), sizeof(oldlocale));
			setlocale(LC_TIME, etemplate->locale);
		}

		res = sendmail(etemplate, vmu, cidnum, cidname, filename, messageformat, duration, etemplate->attachment, MVM_MESSAGE_PAGE, counter);
	}

	mwi_state = ast_mwi_create(vmu->username, vmu->domain);
	if (!mwi_state) {
		goto notify_cleanup;
	}
	mwi_state->snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan));

	json_object = ast_json_pack("{s: s, s: s, s: s}",
		"Event", "MiniVoiceMail",
		"Action", "SentNotification",
		"Counter", counter ?: "");
	if (!json_object) {
		goto notify_cleanup;
	}
	message = ast_mwi_blob_create(mwi_state, ast_mwi_vm_app_type(), json_object);
	if (!message) {
		goto notify_cleanup;
	}
	stasis_publish(ast_mwi_topic(mwi_state->uniqueid), message);

notify_cleanup:
	run_externnotify(chan, vmu);		/* Run external notification */
	if (!ast_strlen_zero(etemplate->locale)) {
		setlocale(LC_TIME, oldlocale);	/* Reset to old locale */
	}
	return res;
}


/*!\internal
 * \brief Record voicemail message, store into file prepared for sending e-mail */
static int leave_voicemail(struct ast_channel *chan, char *username, struct leave_vm_options *options)
{
	char tmptxtfile[PATH_MAX];
	char callerid[256];
	FILE *txt;
	int res = 0, txtdes;
	int duration = 0;
	int sound_duration = 0;
	char date[256];
	char tmpdir[PATH_MAX];
	char ext_context[256] = "";
	char fmt[80];
	char *domain;
	char tmp[256] = "";
	struct minivm_account *vmu;
	int userdir;

	ast_copy_string(tmp, username, sizeof(tmp));
	username = tmp;
	domain = strchr(tmp, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	}

	if (!(vmu = find_account(domain, username, TRUE))) {
		/* We could not find user, let's exit */
		ast_log(LOG_ERROR, "Can't allocate temporary account for '%s@%s'\n", username, domain);
		pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "FAILED");
		return 0;
	}

	/* Setup pre-file if appropriate */
	if (strcmp(vmu->domain, "localhost"))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", username, vmu->domain);
	else
		ast_copy_string(ext_context, vmu->domain, sizeof(ext_context));

	/* The meat of recording the message...  All the announcements and beeps have been played*/
	if (ast_strlen_zero(vmu->attachfmt))
		ast_copy_string(fmt, default_vmformat, sizeof(fmt));
	else
		ast_copy_string(fmt, vmu->attachfmt, sizeof(fmt));

	if (ast_strlen_zero(fmt)) {
		ast_log(LOG_WARNING, "No format for saving voicemail? Default %s\n", default_vmformat);
		pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "FAILED");
		return res;
	}

	userdir = check_dirpath(tmpdir, sizeof(tmpdir), vmu->domain, username, "tmp");

	/* If we have no user directory, use generic temporary directory */
	if (!userdir) {
		create_dirpath(tmpdir, sizeof(tmpdir), "0000_minivm_temp", "mediafiles", "");
		ast_debug(3, "Creating temporary directory %s\n", tmpdir);
	}


	snprintf(tmptxtfile, sizeof(tmptxtfile), "%s/XXXXXX", tmpdir);

	/* XXX This file needs to be in temp directory */
	txtdes = mkstemp(tmptxtfile);
	if (txtdes < 0) {
		ast_log(LOG_ERROR, "Unable to create message file %s: %s\n", tmptxtfile, strerror(errno));
		res = ast_streamfile(chan, "vm-mailboxfull", ast_channel_language(chan));
		if (!res)
			res = ast_waitstream(chan, "");
		pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "FAILED");
		return res;
	}

	if (res >= 0) {
		/* Unless we're *really* silent, try to send the beep */
		res = ast_streamfile(chan, "beep", ast_channel_language(chan));
		if (!res)
			res = ast_waitstream(chan, "");
	}

	/* OEJ XXX Maybe this can be turned into a log file? Hmm. */
	/* Store information */
	ast_debug(2, "Open file for metadata: %s\n", tmptxtfile);

	res = play_record_review(chan, NULL, tmptxtfile, global_vmmaxmessage, fmt, 1, vmu, &duration, &sound_duration, NULL, options->record_gain);

	txt = fdopen(txtdes, "w+");
	if (!txt) {
		ast_log(LOG_WARNING, "Error opening text file for output\n");
	} else {
		struct ast_tm tm;
		struct timeval now = ast_tvnow();
		char timebuf[30];
		char logbuf[BUFSIZ];
		get_date(date, sizeof(date));
		ast_localtime(&now, &tm, NULL);
		ast_strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm);

		ast_callerid_merge(callerid, sizeof(callerid),
			S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL),
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
			"Unknown");
		snprintf(logbuf, sizeof(logbuf),
			/* "Mailbox:domain:macrocontext:exten:priority:callerchan:callerid:origdate:origtime:duration:durationstatus:accountcode" */
			"%s:%s:%s:%s:%d:%s:%s:%s:%s:%d:%s:%s\n",
			username,
			ast_channel_context(chan),
			ast_channel_macrocontext(chan),
			ast_channel_exten(chan),
			ast_channel_priority(chan),
			ast_channel_name(chan),
			callerid,
			date,
			timebuf,
			duration,
			duration < global_vmminmessage ? "IGNORED" : "OK",
			vmu->accountcode
		);
		fprintf(txt, "%s", logbuf);
		if (minivmlogfile) {
			ast_mutex_lock(&minivmloglock);
			fprintf(minivmlogfile, "%s", logbuf);
			ast_mutex_unlock(&minivmloglock);
		}

		if (sound_duration < global_vmminmessage) {
			ast_verb(3, "Recording was %d seconds long but needs to be at least %d - abandoning\n", sound_duration, global_vmminmessage);
			fclose(txt);
			ast_filedelete(tmptxtfile, NULL);
			unlink(tmptxtfile);
			pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "FAILED");
			return 0;
		}
		fclose(txt); /* Close log file */
		if (ast_fileexists(tmptxtfile, NULL, NULL) <= 0) {
			ast_debug(1, "The recorded media file is gone, so we should remove the .txt file too!\n");
			unlink(tmptxtfile);
			pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "FAILED");
			if(ast_test_flag(vmu, MVM_ALLOCED))
				free_user(vmu);
			return 0;
		}

		/* Set channel variables for the notify application */
		pbx_builtin_setvar_helper(chan, "MVM_FILENAME", tmptxtfile);
		snprintf(timebuf, sizeof(timebuf), "%d", duration);
		pbx_builtin_setvar_helper(chan, "MVM_DURATION", timebuf);
		pbx_builtin_setvar_helper(chan, "MVM_FORMAT", fmt);

	}
	global_stats.lastreceived = ast_tvnow();
	global_stats.receivedmessages++;
#if 0
	/* Go ahead and delete audio files from system, they're not needed any more */
	if (ast_fileexists(tmptxtfile, NULL, NULL) <= 0) {
		ast_filedelete(tmptxtfile, NULL);
		 /* Even not being used at the moment, it's better to convert ast_log to ast_debug anyway */
		ast_debug(2, "-_-_- Deleted audio file after notification :: %s \n", tmptxtfile);
	}
#endif

	if (res > 0)
		res = 0;

	if(ast_test_flag(vmu, MVM_ALLOCED))
		free_user(vmu);

	pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "SUCCESS");
	return res;
}

/*!\internal
 * \brief Queue a message waiting event */
static void queue_mwi_event(const char *channel_id, const char *mbx, const char *ctx, int urgent, int new, int old)
{
	char *mailbox, *context;

	mailbox = ast_strdupa(mbx);
	context = ast_strdupa(ctx);
	if (ast_strlen_zero(context)) {
		context = "default";
	}

	ast_publish_mwi_state_channel(mailbox, context, new + urgent, old, channel_id);
}

/*!\internal
 * \brief Send MWI using interal Asterisk event subsystem */
static int minivm_mwi_exec(struct ast_channel *chan, const char *data)
{
	int argc;
	char *argv[4];
	int res = 0;
	char *tmpptr;
	char tmp[PATH_MAX];
	char *mailbox;
	char *domain;
	if (ast_strlen_zero(data))  {
		ast_log(LOG_ERROR, "Minivm needs at least an account argument \n");
		return -1;
	}
	tmpptr = ast_strdupa((char *)data);
	argc = ast_app_separate_args(tmpptr, ',', argv, ARRAY_LEN(argv));
	if (argc < 4) {
		ast_log(LOG_ERROR, "%d arguments passed to MiniVM_MWI, need 4.\n", argc);
		return -1;
	}
	ast_copy_string(tmp, argv[0], sizeof(tmp));
	mailbox = tmp;
	domain = strchr(tmp, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	}
	if (ast_strlen_zero(domain) || ast_strlen_zero(mailbox)) {
		ast_log(LOG_ERROR, "Need mailbox@context as argument. Sorry. Argument 0 %s\n", argv[0]);
		return -1;
	}
	queue_mwi_event(ast_channel_uniqueid(chan), mailbox, domain, atoi(argv[1]), atoi(argv[2]), atoi(argv[3]));

	return res;
}


/*!\internal
 * \brief Notify voicemail account owners - either generic template or user specific */
static int minivm_notify_exec(struct ast_channel *chan, const char *data)
{
	int argc;
	char *argv[2];
	int res = 0;
	char tmp[PATH_MAX];
	char *domain;
	char *tmpptr;
	struct minivm_account *vmu;
	char *username;
	const char *template = "";
	const char *filename;
	const char *format;
	const char *duration_string;
	if (ast_strlen_zero(data))  {
		ast_log(LOG_ERROR, "Minivm needs at least an account argument \n");
		return -1;
	}
	tmpptr = ast_strdupa((char *)data);
	argc = ast_app_separate_args(tmpptr, ',', argv, ARRAY_LEN(argv));

	if (argc == 2 && !ast_strlen_zero(argv[1]))
		template = argv[1];

	ast_copy_string(tmp, argv[0], sizeof(tmp));
	username = tmp;
	domain = strchr(tmp, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	}
	if (ast_strlen_zero(domain) || ast_strlen_zero(username)) {
		ast_log(LOG_ERROR, "Need username@domain as argument. Sorry. Argument 0 %s\n", argv[0]);
		return -1;
	}

	if(!(vmu = find_account(domain, username, TRUE))) {
		/* We could not find user, let's exit */
		ast_log(LOG_WARNING, "Could not allocate temporary memory for '%s@%s'\n", username, domain);
		pbx_builtin_setvar_helper(chan, "MVM_NOTIFY_STATUS", "FAILED");
		return -1;
	}

	ast_channel_lock(chan);
	if ((filename = pbx_builtin_getvar_helper(chan, "MVM_FILENAME"))) {
		filename = ast_strdupa(filename);
	}
	ast_channel_unlock(chan);
	/* Notify of new message to e-mail and pager */
	if (!ast_strlen_zero(filename)) {
		ast_channel_lock(chan);
		if ((format = pbx_builtin_getvar_helper(chan, "MVM_FORMAT"))) {
			format = ast_strdupa(format);
		}
		if ((duration_string = pbx_builtin_getvar_helper(chan, "MVM_DURATION"))) {
			duration_string = ast_strdupa(duration_string);
		}
		ast_channel_unlock(chan);
		res = notify_new_message(chan, template, vmu, filename, atoi(duration_string),
			format,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
			S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL));
	}

	pbx_builtin_setvar_helper(chan, "MVM_NOTIFY_STATUS", res == 0 ? "SUCCESS" : "FAILED");


	if(ast_test_flag(vmu, MVM_ALLOCED))
		free_user(vmu);

	/* Ok, we're ready to rock and roll. Return to dialplan */

	return res;

}

/*!\internal
 * \brief Dialplan function to record voicemail */
static int minivm_record_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *tmp;
	struct leave_vm_options leave_options;
	int argc;
	char *argv[2];
	struct ast_flags flags = { 0 };
	char *opts[OPT_ARG_ARRAY_SIZE];

	memset(&leave_options, 0, sizeof(leave_options));

	/* Answer channel if it's not already answered */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	if (ast_strlen_zero(data))  {
		ast_log(LOG_ERROR, "Minivm needs at least an account argument \n");
		return -1;
	}
	tmp = ast_strdupa((char *)data);
	argc = ast_app_separate_args(tmp, ',', argv, ARRAY_LEN(argv));
	if (argc == 2) {
		if (ast_app_parse_options(minivm_app_options, &flags, opts, argv[1])) {
			return -1;
		}
		ast_copy_flags(&leave_options, &flags, OPT_SILENT | OPT_BUSY_GREETING | OPT_UNAVAIL_GREETING );
		if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
			int gain;

			if (sscanf(opts[OPT_ARG_RECORDGAIN], "%30d", &gain) != 1) {
				ast_log(LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
				return -1;
			} else
				leave_options.record_gain = (signed char) gain;
		}
	}

	/* Now run the appliation and good luck to you! */
	res = leave_voicemail(chan, argv[0], &leave_options);

	if (res == ERROR_LOCK_PATH) {
		ast_log(LOG_ERROR, "Could not leave voicemail. The path is already locked.\n");
		pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "FAILED");
		res = 0;
	}
	pbx_builtin_setvar_helper(chan, "MVM_RECORD_STATUS", "SUCCESS");

	return res;
}

/*!\internal
 * \brief Play voicemail prompts - either generic or user specific */
static int minivm_greet_exec(struct ast_channel *chan, const char *data)
{
	struct leave_vm_options leave_options = { 0, '\0'};
	int argc;
	char *argv[2];
	struct ast_flags flags = { 0 };
	char *opts[OPT_ARG_ARRAY_SIZE];
	int res = 0;
	int ausemacro = 0;
	int ousemacro = 0;
	int ouseexten = 0;
	char tmp[PATH_MAX];
	char dest[PATH_MAX];
	char prefile[PATH_MAX] = "";
	char tempfile[PATH_MAX] = "";
	char ext_context[256] = "";
	char *domain;
	char ecodes[16] = "#";
	char *tmpptr;
	struct minivm_account *vmu;
	char *username;

	if (ast_strlen_zero(data))  {
		ast_log(LOG_ERROR, "Minivm needs at least an account argument \n");
		return -1;
	}
	tmpptr = ast_strdupa((char *)data);
	argc = ast_app_separate_args(tmpptr, ',', argv, ARRAY_LEN(argv));

	if (argc == 2) {
		if (ast_app_parse_options(minivm_app_options, &flags, opts, argv[1]))
			return -1;
		ast_copy_flags(&leave_options, &flags, OPT_SILENT | OPT_BUSY_GREETING | OPT_UNAVAIL_GREETING );
	}

	ast_copy_string(tmp, argv[0], sizeof(tmp));
	username = tmp;
	domain = strchr(tmp, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	}
	if (ast_strlen_zero(domain) || ast_strlen_zero(username)) {
		ast_log(LOG_ERROR, "Need username@domain as argument. Sorry. Argument:  %s\n", argv[0]);
		return -1;
	}
	ast_debug(1, "Trying to find configuration for user %s in domain %s\n", username, domain);

	if (!(vmu = find_account(domain, username, TRUE))) {
		ast_log(LOG_ERROR, "Could not allocate memory. \n");
		return -1;
	}

	/* Answer channel if it's not already answered */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	/* Setup pre-file if appropriate */
	if (strcmp(vmu->domain, "localhost"))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", username, vmu->domain);
	else
		ast_copy_string(ext_context, vmu->domain, sizeof(ext_context));

	if (ast_test_flag(&leave_options, OPT_BUSY_GREETING)) {
		res = check_dirpath(dest, sizeof(dest), vmu->domain, username, "busy");
		if (res)
			snprintf(prefile, sizeof(prefile), "%s%s/%s/busy", MVM_SPOOL_DIR, vmu->domain, username);
	} else if (ast_test_flag(&leave_options, OPT_UNAVAIL_GREETING)) {
		res = check_dirpath(dest, sizeof(dest), vmu->domain, username, "unavail");
		if (res)
			snprintf(prefile, sizeof(prefile), "%s%s/%s/unavail", MVM_SPOOL_DIR, vmu->domain, username);
	}
	/* Check for temporary greeting - it overrides busy and unavail */
	snprintf(tempfile, sizeof(tempfile), "%s%s/%s/temp", MVM_SPOOL_DIR, vmu->domain, username);
	if (!(res = check_dirpath(dest, sizeof(dest), vmu->domain, username, "temp"))) {
		ast_debug(2, "Temporary message directory does not exist, using default (%s)\n", tempfile);
		ast_copy_string(prefile, tempfile, sizeof(prefile));
	}
	ast_debug(2, "Preparing to play message ...\n");

	/* Check current or macro-calling context for special extensions */
	if (ast_test_flag(vmu, MVM_OPERATOR)) {
		if (!ast_strlen_zero(vmu->exit)) {
			if (ast_exists_extension(chan, vmu->exit, "o", 1,
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
				strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
				ouseexten = 1;
			}
		} else if (ast_exists_extension(chan, ast_channel_context(chan), "o", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
			ouseexten = 1;
		}
		else if (!ast_strlen_zero(ast_channel_macrocontext(chan))
			&& ast_exists_extension(chan, ast_channel_macrocontext(chan), "o", 1,
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			strncat(ecodes, "0", sizeof(ecodes) - strlen(ecodes) - 1);
			ousemacro = 1;
		}
	}

	if (!ast_strlen_zero(vmu->exit)) {
		if (ast_exists_extension(chan, vmu->exit, "a", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			strncat(ecodes, "*", sizeof(ecodes) -  strlen(ecodes) - 1);
		}
	} else if (ast_exists_extension(chan, ast_channel_context(chan), "a", 1,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
		strncat(ecodes, "*", sizeof(ecodes) -  strlen(ecodes) - 1);
	} else if (!ast_strlen_zero(ast_channel_macrocontext(chan))
		&& ast_exists_extension(chan, ast_channel_macrocontext(chan), "a", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
		strncat(ecodes, "*", sizeof(ecodes) -  strlen(ecodes) - 1);
		ausemacro = 1;
	}

	res = 0;	/* Reset */
	/* Play the beginning intro if desired */
	if (!ast_strlen_zero(prefile)) {
		if (ast_streamfile(chan, prefile, ast_channel_language(chan)) > -1)
			res = ast_waitstream(chan, ecodes);
	} else {
		ast_debug(2, "%s doesn't exist, doing what we can\n", prefile);
		res = invent_message(chan, vmu->domain, username, ast_test_flag(&leave_options, OPT_BUSY_GREETING), ecodes);
	}
	if (res < 0) {
		ast_debug(2, "Hang up during prefile playback\n");
		pbx_builtin_setvar_helper(chan, "MVM_GREET_STATUS", "FAILED");
		if(ast_test_flag(vmu, MVM_ALLOCED))
			free_user(vmu);
		return -1;
	}
	if (res == '#') {
		/* On a '#' we skip the instructions */
		ast_set_flag(&leave_options, OPT_SILENT);
		res = 0;
	}
	if (!res && !ast_test_flag(&leave_options, OPT_SILENT)) {
		res = ast_streamfile(chan, SOUND_INTRO, ast_channel_language(chan));
		if (!res)
			res = ast_waitstream(chan, ecodes);
		if (res == '#') {
			ast_set_flag(&leave_options, OPT_SILENT);
			res = 0;
		}
	}
	if (res > 0)
		ast_stopstream(chan);
	/* Check for a '*' here in case the caller wants to escape from voicemail to something
	   other than the operator -- an automated attendant or mailbox login for example */
	if (res == '*') {
		ast_channel_exten_set(chan, "a");
		if (!ast_strlen_zero(vmu->exit)) {
			ast_channel_context_set(chan, vmu->exit);
		} else if (ausemacro && !ast_strlen_zero(ast_channel_macrocontext(chan))) {
			ast_channel_context_set(chan, ast_channel_macrocontext(chan));
		}
		ast_channel_priority_set(chan, 0);
		pbx_builtin_setvar_helper(chan, "MVM_GREET_STATUS", "USEREXIT");
		res = 0;
	} else if (res == '0') { /* Check for a '0' here */
		if(ouseexten || ousemacro) {
			ast_channel_exten_set(chan, "o");
			if (!ast_strlen_zero(vmu->exit)) {
				ast_channel_context_set(chan, vmu->exit);
			} else if (ousemacro && !ast_strlen_zero(ast_channel_macrocontext(chan))) {
				ast_channel_context_set(chan, ast_channel_macrocontext(chan));
			}
			ast_play_and_wait(chan, "transfer");
			ast_channel_priority_set(chan, 0);
			pbx_builtin_setvar_helper(chan, "MVM_GREET_STATUS", "USEREXIT");
		}
		res =  0;
	} else if (res < 0) {
		pbx_builtin_setvar_helper(chan, "MVM_GREET_STATUS", "FAILED");
		res = -1;
	} else
		pbx_builtin_setvar_helper(chan, "MVM_GREET_STATUS", "SUCCESS");

	if(ast_test_flag(vmu, MVM_ALLOCED))
		free_user(vmu);


	/* Ok, we're ready to rock and roll. Return to dialplan */
	return res;

}

/*!\internal
 * \brief Dialplan application to delete voicemail */
static int minivm_delete_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char filename[BUFSIZ];

	if (!ast_strlen_zero(data)) {
		ast_copy_string(filename, (char *) data, sizeof(filename));
	} else {
		ast_channel_lock(chan);
		ast_copy_string(filename, pbx_builtin_getvar_helper(chan, "MVM_FILENAME"), sizeof(filename));
		ast_channel_unlock(chan);
	}

	if (ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR, "No filename given in application arguments or channel variable MVM_FILENAME\n");
		return res;
	}

	/* Go ahead and delete audio files from system, they're not needed any more */
	/* We should look for both audio and text files here */
	if (ast_fileexists(filename, NULL, NULL) > 0) {
		res = vm_delete(filename);
		if (res) {
			ast_debug(2, "Can't delete file: %s\n", filename);
			pbx_builtin_setvar_helper(chan, "MVM_DELETE_STATUS", "FAILED");
		} else {
			ast_debug(2, "Deleted voicemail file :: %s \n", filename);
			pbx_builtin_setvar_helper(chan, "MVM_DELETE_STATUS", "SUCCESS");
		}
	} else {
		ast_debug(2, "Filename does not exist: %s\n", filename);
		pbx_builtin_setvar_helper(chan, "MVM_DELETE_STATUS", "FAILED");
	}

	return res;
}

/*! \brief Record specific messages for voicemail account */
static int minivm_accmess_exec(struct ast_channel *chan, const char *data)
{
	int argc = 0;
	char *argv[2];
	char filename[PATH_MAX];
	char tmp[PATH_MAX];
	char *domain;
	char *tmpptr = NULL;
	struct minivm_account *vmu;
	char *username;
	struct ast_flags flags = { 0 };
	char *opts[OPT_ARG_ARRAY_SIZE];
	int error = FALSE;
	char *message = NULL;
	char *prompt = NULL;
	int duration;

	if (ast_strlen_zero(data))  {
		ast_log(LOG_ERROR, "MinivmAccmess needs at least two arguments: account and option\n");
		error = TRUE;
	} else {
		tmpptr = ast_strdupa((char *)data);
		argc = ast_app_separate_args(tmpptr, ',', argv, ARRAY_LEN(argv));
	}

	if (argc <=1) {
		ast_log(LOG_ERROR, "MinivmAccmess needs at least two arguments: account and option\n");
		error = TRUE;
	}
	if (!error && strlen(argv[1]) > 1) {
		ast_log(LOG_ERROR, "MinivmAccmess can only handle one option at a time. Bad option string: %s\n", argv[1]);
		error = TRUE;
	}

	if (!error && ast_app_parse_options(minivm_accmess_options, &flags, opts, argv[1])) {
		ast_log(LOG_ERROR, "Can't parse option %s\n", argv[1]);
		error = TRUE;
	}

	if (error) {
		pbx_builtin_setvar_helper(chan, "MVM_ACCMESS_STATUS", "FAILED");
		return -1;
	}

	ast_copy_string(tmp, argv[0], sizeof(tmp));
	username = tmp;
	domain = strchr(tmp, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	}
	if (ast_strlen_zero(domain) || ast_strlen_zero(username)) {
		ast_log(LOG_ERROR, "Need username@domain as argument. Sorry. Argument 0 %s\n", argv[0]);
		pbx_builtin_setvar_helper(chan, "MVM_ACCMESS_STATUS", "FAILED");
		return -1;
	}

	if(!(vmu = find_account(domain, username, TRUE))) {
		/* We could not find user, let's exit */
		ast_log(LOG_WARNING, "Could not allocate temporary memory for '%s@%s'\n", username, domain);
		pbx_builtin_setvar_helper(chan, "MVM_ACCMESS_STATUS", "FAILED");
		return -1;
	}

	/* Answer channel if it's not already answered */
	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	/* Here's where the action is */
	if (ast_test_flag(&flags, OPT_BUSY_GREETING)) {
		message = "busy";
		prompt = "vm-rec-busy";
	} else if (ast_test_flag(&flags, OPT_UNAVAIL_GREETING)) {
		message = "unavailable";
		prompt = "vm-rec-unv";
	} else if (ast_test_flag(&flags, OPT_TEMP_GREETING)) {
		message = "temp";
		prompt = "vm-rec-temp";
	} else if (ast_test_flag(&flags, OPT_NAME_GREETING)) {
		message = "greet";
		prompt = "vm-rec-name";
	}
	snprintf(filename,sizeof(filename), "%s%s/%s/%s", MVM_SPOOL_DIR, vmu->domain, vmu->username, message);
	/* Maybe we should check the result of play_record_review ? */
	play_record_review(chan, prompt, filename, global_maxgreet, default_vmformat, 0, vmu, &duration, NULL, NULL, FALSE);

	ast_debug(1, "Recorded new %s message in %s (duration %d)\n", message, filename, duration);

	if(ast_test_flag(vmu, MVM_ALLOCED))
		free_user(vmu);

	pbx_builtin_setvar_helper(chan, "MVM_ACCMESS_STATUS", "SUCCESS");

	/* Ok, we're ready to rock and roll. Return to dialplan */
	return 0;
}

/*! \brief Append new mailbox to mailbox list from configuration file */
static int create_vmaccount(char *name, struct ast_variable *var, int realtime)
{
	struct minivm_account *vmu;
	char *domain;
	char *username;
	char accbuf[BUFSIZ];

	ast_debug(3, "Creating %s account for [%s]\n", realtime ? "realtime" : "static", name);

	ast_copy_string(accbuf, name, sizeof(accbuf));
	username = accbuf;
	domain = strchr(accbuf, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	}
	if (ast_strlen_zero(domain)) {
		ast_log(LOG_ERROR, "No domain given for mini-voicemail account %s. Not configured.\n", name);
		return 0;
	}

	ast_debug(3, "Creating static account for user %s domain %s\n", username, domain);

	/* Allocate user account */
	vmu = ast_calloc(1, sizeof(*vmu));
	if (!vmu)
		return 0;

	ast_copy_string(vmu->domain, domain, sizeof(vmu->domain));
	ast_copy_string(vmu->username, username, sizeof(vmu->username));

	populate_defaults(vmu);

	ast_debug(3, "...Configuring account %s\n", name);

	while (var) {
		ast_debug(3, "Configuring %s = \"%s\" for account %s\n", var->name, var->value, name);
		if (!strcasecmp(var->name, "serveremail")) {
			ast_copy_string(vmu->serveremail, var->value, sizeof(vmu->serveremail));
		} else if (!strcasecmp(var->name, "email")) {
			ast_copy_string(vmu->email, var->value, sizeof(vmu->email));
		} else if (!strcasecmp(var->name, "accountcode")) {
			ast_copy_string(vmu->accountcode, var->value, sizeof(vmu->accountcode));
		} else if (!strcasecmp(var->name, "pincode")) {
			ast_copy_string(vmu->pincode, var->value, sizeof(vmu->pincode));
		} else if (!strcasecmp(var->name, "domain")) {
			ast_copy_string(vmu->domain, var->value, sizeof(vmu->domain));
		} else if (!strcasecmp(var->name, "language")) {
			ast_copy_string(vmu->language, var->value, sizeof(vmu->language));
		} else if (!strcasecmp(var->name, "timezone")) {
			ast_copy_string(vmu->zonetag, var->value, sizeof(vmu->zonetag));
		} else if (!strcasecmp(var->name, "externnotify")) {
			ast_copy_string(vmu->externnotify, var->value, sizeof(vmu->externnotify));
		} else if (!strcasecmp(var->name, "etemplate")) {
			ast_copy_string(vmu->etemplate, var->value, sizeof(vmu->etemplate));
		} else if (!strcasecmp(var->name, "ptemplate")) {
			ast_copy_string(vmu->ptemplate, var->value, sizeof(vmu->ptemplate));
		} else if (!strcasecmp(var->name, "fullname")) {
			ast_copy_string(vmu->fullname, var->value, sizeof(vmu->fullname));
		} else if (!strcasecmp(var->name, "setvar")) {
			char *varval;
			char varname[strlen(var->value) + 1];
			struct ast_variable *tmpvar;

			strcpy(varname, var->value); /* safe */
			if ((varval = strchr(varname, '='))) {
				*varval = '\0';
				varval++;
				if ((tmpvar = ast_variable_new(varname, varval, ""))) {
					tmpvar->next = vmu->chanvars;
					vmu->chanvars = tmpvar;
				}
			}
		} else if (!strcasecmp(var->name, "pager")) {
			ast_copy_string(vmu->pager, var->value, sizeof(vmu->pager));
		} else if (!strcasecmp(var->name, "volgain")) {
			sscanf(var->value, "%30lf", &vmu->volgain);
		} else {
			ast_log(LOG_ERROR, "Unknown configuration option for minivm account %s : %s\n", name, var->name);
		}
		var = var->next;
	}
	ast_debug(3, "...Linking account %s\n", name);

	AST_LIST_LOCK(&minivm_accounts);
	AST_LIST_INSERT_TAIL(&minivm_accounts, vmu, list);
	AST_LIST_UNLOCK(&minivm_accounts);

	global_stats.voicemailaccounts++;

	ast_debug(2, "MVM :: Created account %s@%s - tz %s etemplate %s %s\n", username, domain, ast_strlen_zero(vmu->zonetag) ? "" : vmu->zonetag, ast_strlen_zero(vmu->etemplate) ? "" : vmu->etemplate, realtime ? "(realtime)" : "");
	return 0;
}

/*! \brief Free Mini Voicemail timezone */
static void free_zone(struct minivm_zone *z)
{
	ast_free(z);
}

/*! \brief Clear list of timezones */
static void timezone_destroy_list(void)
{
	struct minivm_zone *this;

	AST_LIST_LOCK(&minivm_zones);
	while ((this = AST_LIST_REMOVE_HEAD(&minivm_zones, list)))
		free_zone(this);

	AST_LIST_UNLOCK(&minivm_zones);
}

/*! \brief Add time zone to memory list */
static int timezone_add(const char *zonename, const char *config)
{
	struct minivm_zone *newzone;
	char *msg_format, *timezone_str;

	newzone = ast_calloc(1, sizeof(*newzone));
	if (newzone == NULL)
		return 0;

	msg_format = ast_strdupa(config);

	timezone_str = strsep(&msg_format, "|");
	if (!msg_format) {
		ast_log(LOG_WARNING, "Invalid timezone definition : %s\n", zonename);
		ast_free(newzone);
		return 0;
	}

	ast_copy_string(newzone->name, zonename, sizeof(newzone->name));
	ast_copy_string(newzone->timezone, timezone_str, sizeof(newzone->timezone));
	ast_copy_string(newzone->msg_format, msg_format, sizeof(newzone->msg_format));

	AST_LIST_LOCK(&minivm_zones);
	AST_LIST_INSERT_TAIL(&minivm_zones, newzone, list);
	AST_LIST_UNLOCK(&minivm_zones);

	global_stats.timezones++;

	return 0;
}

/*! \brief Read message template from file */
static char *message_template_parse_filebody(const char *filename) {
	char buf[BUFSIZ * 6];
	char readbuf[BUFSIZ];
	char filenamebuf[BUFSIZ];
	char *writepos;
	char *messagebody;
	FILE *fi;
	int lines = 0;

	if (ast_strlen_zero(filename))
		return NULL;
	if (*filename == '/')
		ast_copy_string(filenamebuf, filename, sizeof(filenamebuf));
	else
		snprintf(filenamebuf, sizeof(filenamebuf), "%s/%s", ast_config_AST_CONFIG_DIR, filename);

	if (!(fi = fopen(filenamebuf, "r"))) {
		ast_log(LOG_ERROR, "Can't read message template from file: %s\n", filenamebuf);
		return NULL;
	}
	writepos = buf;
	while (fgets(readbuf, sizeof(readbuf), fi)) {
		lines ++;
		if (writepos != buf) {
			*writepos = '\n';		/* Replace EOL with new line */
			writepos++;
		}
		ast_copy_string(writepos, readbuf, sizeof(buf) - (writepos - buf));
		writepos += strlen(readbuf) - 1;
	}
	fclose(fi);
	messagebody = ast_calloc(1, strlen(buf + 1));
	ast_copy_string(messagebody, buf, strlen(buf) + 1);
	ast_debug(4, "---> Size of allocation %d\n", (int) strlen(buf + 1) );
	ast_debug(4, "---> Done reading message template : \n%s\n---- END message template--- \n", messagebody);

	return messagebody;
}

/*! \brief Parse emailbody template from configuration file */
static char *message_template_parse_emailbody(const char *configuration)
{
	char *tmpread, *tmpwrite;
	char *emailbody = ast_strdup(configuration);

	/* substitute strings \t and \n into the apropriate characters */
	tmpread = tmpwrite = emailbody;
	while ((tmpwrite = strchr(tmpread,'\\'))) {
	       int len = strlen("\n");
	       switch (tmpwrite[1]) {
	       case 'n':
		      memmove(tmpwrite + len, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
		      tmpwrite[0] = '\n';
		      break;
	       case 't':
		      memmove(tmpwrite + len, tmpwrite + 2, strlen(tmpwrite + 2) + 1);
		      tmpwrite[0] = '\t';
		      break;
	       default:
		      ast_log(LOG_NOTICE, "Substitution routine does not support this character: %c\n", tmpwrite[1]);
	       }
	       tmpread = tmpwrite + len;
	}
	return emailbody;
}

/*! \brief Apply general configuration options */
static int apply_general_options(struct ast_variable *var)
{
	int error = 0;

	while (var) {
		/* Mail command */
		if (!strcmp(var->name, "mailcmd")) {
			ast_copy_string(global_mailcmd, var->value, sizeof(global_mailcmd)); /* User setting */
		} else if (!strcmp(var->name, "maxgreet")) {
			global_maxgreet = atoi(var->value);
		} else if (!strcmp(var->name, "maxsilence")) {
			global_maxsilence = atoi(var->value);
			if (global_maxsilence > 0)
				global_maxsilence *= 1000;
		} else if (!strcmp(var->name, "logfile")) {
			if (!ast_strlen_zero(var->value) ) {
				if(*(var->value) == '/')
					ast_copy_string(global_logfile, var->value, sizeof(global_logfile));
				else
					snprintf(global_logfile, sizeof(global_logfile), "%s/%s", ast_config_AST_LOG_DIR, var->value);
			}
		} else if (!strcmp(var->name, "externnotify")) {
			/* External voicemail notify application */
			ast_copy_string(global_externnotify, var->value, sizeof(global_externnotify));
		} else if (!strcmp(var->name, "silencetreshold")) {
			/* Silence treshold */
			global_silencethreshold = atoi(var->value);
		} else if (!strcmp(var->name, "maxmessage")) {
			int x;
			if (sscanf(var->value, "%30d", &x) == 1) {
				global_vmmaxmessage = x;
			} else {
				error ++;
				ast_log(LOG_WARNING, "Invalid max message time length\n");
			}
		} else if (!strcmp(var->name, "minmessage")) {
			int x;
			if (sscanf(var->value, "%30d", &x) == 1) {
				global_vmminmessage = x;
				if (global_maxsilence <= global_vmminmessage)
					ast_log(LOG_WARNING, "maxsilence should be less than minmessage or you may get empty messages\n");
			} else {
				error ++;
				ast_log(LOG_WARNING, "Invalid min message time length\n");
			}
		} else if (!strcmp(var->name, "format")) {
			ast_copy_string(default_vmformat, var->value, sizeof(default_vmformat));
		} else if (!strcmp(var->name, "review")) {
			ast_set2_flag((&globalflags), ast_true(var->value), MVM_REVIEW);
		} else if (!strcmp(var->name, "operator")) {
			ast_set2_flag((&globalflags), ast_true(var->value), MVM_OPERATOR);
		}
		var = var->next;
	}
	return error;
}

/*! \brief Load minivoicemail configuration */
static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	char *cat;
	const char *chanvar;
	int error = 0;
	struct minivm_template *template;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " VOICEMAIL_CONFIG " is in an invalid format.  Aborting.\n");
		return 0;
	}

	ast_mutex_lock(&minivmlock);

	/* Destroy lists to reconfigure */
	message_destroy_list();		/* Destroy list of voicemail message templates */
	timezone_destroy_list();	/* Destroy list of timezones */
	vmaccounts_destroy_list();	/* Destroy list of voicemail accounts */
	ast_debug(2, "Destroyed memory objects...\n");

	/* First, set some default settings */
	global_externnotify[0] = '\0';
	global_logfile[0] = '\0';
	global_vmmaxmessage = 2000;
	global_maxgreet = 2000;
	global_vmminmessage = 0;
	strcpy(global_mailcmd, SENDMAIL);
	global_maxsilence = 0;
	global_saydurationminfo = 2;
	ast_copy_string(default_vmformat, "wav", sizeof(default_vmformat));
	ast_set2_flag((&globalflags), FALSE, MVM_REVIEW);
	ast_set2_flag((&globalflags), FALSE, MVM_OPERATOR);
	/* Reset statistics */
	memset(&global_stats, 0, sizeof(global_stats));
	global_stats.reset = ast_tvnow();

	global_silencethreshold = ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE);

	/* Make sure we could load configuration file */
	if (!cfg) {
		ast_log(LOG_WARNING, "Failed to load configuration file. Module activated with default settings.\n");
		ast_mutex_unlock(&minivmlock);
		return 0;
	}

	ast_debug(2, "Loaded configuration file, now parsing\n");

	/* General settings */

	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		ast_debug(3, "Found configuration section [%s]\n", cat);
		if (!strcasecmp(cat, "general")) {
			/* Nothing right now */
			error += apply_general_options(ast_variable_browse(cfg, cat));
		} else if (!strncasecmp(cat, "template-", 9))  {
			/* Template */
			char *name = cat + 9;

			/* Now build and link template to list */
			error += message_template_build(name, ast_variable_browse(cfg, cat));
		} else {
			var = ast_variable_browse(cfg, cat);
			if (!strcasecmp(cat, "zonemessages")) {
				/* Timezones in this context */
				while (var) {
					timezone_add(var->name, var->value);
					var = var->next;
				}
			} else {
				/* Create mailbox from this */
				error += create_vmaccount(cat, var, FALSE);
			}
		}
		/* Find next section in configuration file */
		cat = ast_category_browse(cfg, cat);
	}

	/* Configure the default email template */
	message_template_build("email-default", NULL);
	template = message_template_find("email-default");

	/* Load date format config for voicemail mail */
	if ((chanvar = ast_variable_retrieve(cfg, "general", "emaildateformat")))
		ast_copy_string(template->dateformat, chanvar, sizeof(template->dateformat));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "emailfromstring")))
		ast_copy_string(template->fromaddress, chanvar, sizeof(template->fromaddress));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "emailaaddress")))
		ast_copy_string(template->serveremail, chanvar, sizeof(template->serveremail));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "emailcharset")))
		ast_copy_string(template->charset, chanvar, sizeof(template->charset));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "emailsubject")))
		ast_copy_string(template->subject, chanvar, sizeof(template->subject));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "emailbody")))
		template->body = message_template_parse_emailbody(chanvar);
	template->attachment = TRUE;

	message_template_build("pager-default", NULL);
	template = message_template_find("pager-default");
	if ((chanvar = ast_variable_retrieve(cfg, "general", "pagerfromstring")))
		ast_copy_string(template->fromaddress, chanvar, sizeof(template->fromaddress));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "pageraddress")))
		ast_copy_string(template->serveremail, chanvar, sizeof(template->serveremail));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "pagercharset")))
		ast_copy_string(template->charset, chanvar, sizeof(template->charset));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "pagersubject")))
		ast_copy_string(template->subject, chanvar,sizeof(template->subject));
	if ((chanvar = ast_variable_retrieve(cfg, "general", "pagerbody")))
		template->body = message_template_parse_emailbody(chanvar);
	template->attachment = FALSE;

	if (error)
		ast_log(LOG_ERROR, "--- A total of %d errors found in mini-voicemail configuration\n", error);

	ast_mutex_unlock(&minivmlock);
	ast_config_destroy(cfg);

	/* Close log file if it's open and disabled */
	if(minivmlogfile)
		fclose(minivmlogfile);

	/* Open log file if it's enabled */
	if(!ast_strlen_zero(global_logfile)) {
		minivmlogfile = fopen(global_logfile, "a");
		if(!minivmlogfile)
			ast_log(LOG_ERROR, "Failed to open minivm log file %s : %s\n", global_logfile, strerror(errno));
		if (minivmlogfile)
			ast_debug(3, "Opened log file %s \n", global_logfile);
	}

	return 0;
}

/*! \brief CLI routine for listing templates */
static char *handle_minivm_list_templates(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct minivm_template *this;
#define HVLT_OUTPUT_FORMAT "%-15s %-10s %-10s %-15.15s %-50s\n"
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "minivm list templates";
		e->usage =
			"Usage: minivm list templates\n"
			"       Lists message templates for e-mail, paging and IM\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&message_templates);
	if (AST_LIST_EMPTY(&message_templates)) {
		ast_cli(a->fd, "There are no message templates defined\n");
		AST_LIST_UNLOCK(&message_templates);
		return CLI_FAILURE;
	}
	ast_cli(a->fd, HVLT_OUTPUT_FORMAT, "Template name", "Charset", "Locale", "Attach media", "Subject");
	ast_cli(a->fd, HVLT_OUTPUT_FORMAT, "-------------", "-------", "------", "------------", "-------");
	AST_LIST_TRAVERSE(&message_templates, this, list) {
		ast_cli(a->fd, HVLT_OUTPUT_FORMAT, this->name,
			S_OR(this->charset, "-"),
			S_OR(this->locale, "-"),
			this->attachment ? "Yes" : "No",
			S_OR(this->subject, "-"));
		count++;
	}
	AST_LIST_UNLOCK(&message_templates);
	ast_cli(a->fd, "\n * Total: %d minivoicemail message templates\n", count);
	return CLI_SUCCESS;
}

static char *complete_minivm_show_users(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	int wordlen;
	struct minivm_account *vmu;
	const char *domain = "";

	/* 0 - minivm; 1 - list; 2 - accounts; 3 - for; 4 - <domain> */
	if (pos > 4)
		return NULL;
	wordlen = strlen(word);
	AST_LIST_TRAVERSE(&minivm_accounts, vmu, list) {
		if (!strncasecmp(word, vmu->domain, wordlen)) {
			if (domain && strcmp(domain, vmu->domain) && ++which > state)
				return ast_strdup(vmu->domain);
			/* ignore repeated domains ? */
			domain = vmu->domain;
		}
	}
	return NULL;
}

/*! \brief CLI command to list voicemail accounts */
static char *handle_minivm_show_users(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct minivm_account *vmu;
#define HMSU_OUTPUT_FORMAT "%-23s %-15s %-15s %-10s %-10s %-50s\n"
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "minivm list accounts [for]";
		e->usage =
			"Usage: minivm list accounts [for <domain>]\n"
			"       Lists all mailboxes currently set up\n";
		return NULL;
	case CLI_GENERATE:
		return complete_minivm_show_users(a->line, a->word, a->pos, a->n);
	}

	if ((a->argc < 3) || (a->argc > 5) || (a->argc == 4))
		return CLI_SHOWUSAGE;
	if ((a->argc == 5) && strcmp(a->argv[3],"for"))
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&minivm_accounts);
	if (AST_LIST_EMPTY(&minivm_accounts)) {
		ast_cli(a->fd, "There are no voicemail users currently defined\n");
		AST_LIST_UNLOCK(&minivm_accounts);
		return CLI_FAILURE;
	}
	ast_cli(a->fd, HMSU_OUTPUT_FORMAT, "User", "E-Template", "P-template", "Zone", "Format", "Full name");
	ast_cli(a->fd, HMSU_OUTPUT_FORMAT, "----", "----------", "----------", "----", "------", "---------");
	AST_LIST_TRAVERSE(&minivm_accounts, vmu, list) {
		char tmp[256] = "";
		if ((a->argc == 3) || ((a->argc == 5) && !strcmp(a->argv[4], vmu->domain))) {
			count++;
			snprintf(tmp, sizeof(tmp), "%s@%s", vmu->username, vmu->domain);
			ast_cli(a->fd, HMSU_OUTPUT_FORMAT, tmp, S_OR(vmu->etemplate, "-"),
				S_OR(vmu->ptemplate, "-"),
				S_OR(vmu->zonetag, "-"),
				S_OR(vmu->attachfmt, "-"),
				vmu->fullname);
		}
	}
	AST_LIST_UNLOCK(&minivm_accounts);
	ast_cli(a->fd, "\n * Total: %d minivoicemail accounts\n", count);
	return CLI_SUCCESS;
}

/*! \brief Show a list of voicemail zones in the CLI */
static char *handle_minivm_show_zones(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct minivm_zone *zone;
#define HMSZ_OUTPUT_FORMAT "%-15s %-20s %-45s\n"
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "minivm list zones";
		e->usage =
			"Usage: minivm list zones\n"
			"       Lists zone message formats\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&minivm_zones);
	if (!AST_LIST_EMPTY(&minivm_zones)) {
		ast_cli(a->fd, HMSZ_OUTPUT_FORMAT, "Zone", "Timezone", "Message Format");
		ast_cli(a->fd, HMSZ_OUTPUT_FORMAT, "----", "--------", "--------------");
		AST_LIST_TRAVERSE(&minivm_zones, zone, list) {
			ast_cli(a->fd, HMSZ_OUTPUT_FORMAT, zone->name, zone->timezone, zone->msg_format);
		}
	} else {
		ast_cli(a->fd, "There are no voicemail zones currently defined\n");
		res = CLI_FAILURE;
	}
	AST_LIST_UNLOCK(&minivm_zones);

	return res;
}

/*! \brief CLI Show settings */
static char *handle_minivm_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "minivm show settings";
		e->usage =
			"Usage: minivm show settings\n"
			"       Display Mini-Voicemail general settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "* Mini-Voicemail general settings\n");
	ast_cli(a->fd, "  -------------------------------\n");
	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "  Mail command (shell):               %s\n", global_mailcmd);
	ast_cli(a->fd, "  Max silence:                        %d\n", global_maxsilence);
	ast_cli(a->fd, "  Silence threshold:                  %d\n", global_silencethreshold);
	ast_cli(a->fd, "  Max message length (secs):          %d\n", global_vmmaxmessage);
	ast_cli(a->fd, "  Min message length (secs):          %d\n", global_vmminmessage);
	ast_cli(a->fd, "  Default format:                     %s\n", default_vmformat);
	ast_cli(a->fd, "  Extern notify (shell):              %s\n", global_externnotify);
	ast_cli(a->fd, "  Logfile:                            %s\n", global_logfile[0] ? global_logfile : "<disabled>");
	ast_cli(a->fd, "  Operator exit:                      %s\n", ast_test_flag(&globalflags, MVM_OPERATOR) ? "Yes" : "No");
	ast_cli(a->fd, "  Message review:                     %s\n", ast_test_flag(&globalflags, MVM_REVIEW) ? "Yes" : "No");

	ast_cli(a->fd, "\n");
	return CLI_SUCCESS;
}

/*! \brief Show stats */
static char *handle_minivm_show_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_tm timebuf;
	char buf[BUFSIZ];

	switch (cmd) {

	case CLI_INIT:
		e->command = "minivm show stats";
		e->usage =
			"Usage: minivm show stats\n"
			"       Display Mini-Voicemail counters\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "* Mini-Voicemail statistics\n");
	ast_cli(a->fd, "  -------------------------\n");
	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "  Voicemail accounts:                  %5d\n", global_stats.voicemailaccounts);
	ast_cli(a->fd, "  Templates:                           %5d\n", global_stats.templates);
	ast_cli(a->fd, "  Timezones:                           %5d\n", global_stats.timezones);
	if (global_stats.receivedmessages == 0) {
		ast_cli(a->fd, "  Received messages since last reset:  <none>\n");
	} else {
		ast_cli(a->fd, "  Received messages since last reset:  %d\n", global_stats.receivedmessages);
		ast_localtime(&global_stats.lastreceived, &timebuf, NULL);
		ast_strftime(buf, sizeof(buf), "%a %b %e %r %Z %Y", &timebuf);
		ast_cli(a->fd, "  Last received voicemail:             %s\n", buf);
	}
	ast_localtime(&global_stats.reset, &timebuf, NULL);
	ast_strftime(buf, sizeof(buf), "%a %b %e %r %Z %Y", &timebuf);
	ast_cli(a->fd, "  Last reset:                          %s\n", buf);

	ast_cli(a->fd, "\n");
	return CLI_SUCCESS;
}

/*! \brief  ${MINIVMACCOUNT()} Dialplan function - reads account data */
static int minivm_account_func_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct minivm_account *vmu;
	char *username, *domain, *colname;

	username = ast_strdupa(data);

	if ((colname = strchr(username, ':'))) {
		*colname = '\0';
		colname++;
	} else {
		colname = "path";
	}
	if ((domain = strchr(username, '@'))) {
		*domain = '\0';
		domain++;
	}
	if (ast_strlen_zero(username) || ast_strlen_zero(domain)) {
		ast_log(LOG_ERROR, "This function needs a username and a domain: username@domain\n");
		return 0;
	}

	if (!(vmu = find_account(domain, username, TRUE)))
		return 0;

	if (!strcasecmp(colname, "hasaccount")) {
		ast_copy_string(buf, (ast_test_flag(vmu, MVM_ALLOCED) ? "0" : "1"), len);
	} else  if (!strcasecmp(colname, "fullname")) {
		ast_copy_string(buf, vmu->fullname, len);
	} else  if (!strcasecmp(colname, "email")) {
		if (!ast_strlen_zero(vmu->email))
			ast_copy_string(buf, vmu->email, len);
		else
			snprintf(buf, len, "%s@%s", vmu->username, vmu->domain);
	} else  if (!strcasecmp(colname, "pager")) {
		ast_copy_string(buf, vmu->pager, len);
	} else  if (!strcasecmp(colname, "etemplate")) {
		if (!ast_strlen_zero(vmu->etemplate))
			ast_copy_string(buf, vmu->etemplate, len);
		else
			ast_copy_string(buf, "email-default", len);
	} else  if (!strcasecmp(colname, "language")) {
		ast_copy_string(buf, vmu->language, len);
	} else  if (!strcasecmp(colname, "timezone")) {
		ast_copy_string(buf, vmu->zonetag, len);
	} else  if (!strcasecmp(colname, "ptemplate")) {
		if (!ast_strlen_zero(vmu->ptemplate))
			ast_copy_string(buf, vmu->ptemplate, len);
		else
			ast_copy_string(buf, "email-default", len);
	} else  if (!strcasecmp(colname, "accountcode")) {
		ast_copy_string(buf, vmu->accountcode, len);
	} else  if (!strcasecmp(colname, "pincode")) {
		ast_copy_string(buf, vmu->pincode, len);
	} else  if (!strcasecmp(colname, "path")) {
		check_dirpath(buf, len, vmu->domain, vmu->username, NULL);
	} else {	/* Look in channel variables */
		struct ast_variable *var;

		for (var = vmu->chanvars ; var ; var = var->next)
			if (!strcmp(var->name, colname)) {
				ast_copy_string(buf, var->value, len);
				break;
			}
	}

	if(ast_test_flag(vmu, MVM_ALLOCED))
		free_user(vmu);

	return 0;
}

/*! \brief lock directory

   only return failure if ast_lock_path returns 'timeout',
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

/*! \brief Access counter file, lock directory, read and possibly write it again changed
	\param directory	Directory to crate file in
	\param countername	filename
	\param value		If set to zero, we only read the variable
	\param operand		0 to read, 1 to set new value, 2 to change
	\return -1 on error, otherwise counter value
*/
static int access_counter_file(char *directory, char *countername, int value, int operand)
{
	char filename[BUFSIZ];
	char readbuf[BUFSIZ];
	FILE *counterfile;
	int old = 0, counter = 0;

	/* Lock directory */
	if (vm_lock_path(directory)) {
		return -1;	/* Could not lock directory */
	}
	snprintf(filename, sizeof(filename), "%s/%s.counter", directory, countername);
	if (operand != 1) {
		counterfile = fopen(filename, "r");
		if (counterfile) {
			if(fgets(readbuf, sizeof(readbuf), counterfile)) {
				ast_debug(3, "Read this string from counter file: %s\n", readbuf);
				old = counter = atoi(readbuf);
			}
			fclose(counterfile);
		}
	}
	switch (operand) {
	case 0:	/* Read only */
		ast_unlock_path(directory);
		ast_debug(2, "MINIVM Counter %s/%s: Value %d\n", directory, countername, counter);
		return counter;
		break;
	case 1: /* Set new value */
		counter = value;
		break;
	case 2: /* Change value */
		counter += value;
		if (counter < 0)	/* Don't allow counters to fall below zero */
			counter = 0;
		break;
	}

	/* Now, write the new value to the file */
	counterfile = fopen(filename, "w");
	if (!counterfile) {
		ast_log(LOG_ERROR, "Could not open counter file for writing : %s - %s\n", filename, strerror(errno));
		ast_unlock_path(directory);
		return -1;	/* Could not open file for writing */
	}
	fprintf(counterfile, "%d\n\n", counter);
	fclose(counterfile);
	ast_unlock_path(directory);
	ast_debug(2, "MINIVM Counter %s/%s: Old value %d New value %d\n", directory, countername, old, counter);
	return counter;
}

/*! \brief  ${MINIVMCOUNTER()} Dialplan function - read counters */
static int minivm_counter_func_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *username, *domain, *countername;
	char userpath[BUFSIZ];
	int res;

	*buf = '\0';

	username = ast_strdupa(data);

	if ((countername = strchr(username, ':'))) {
		*countername = '\0';
		countername++;
	}

	if ((domain = strchr(username, '@'))) {
		*domain = '\0';
		domain++;
	}

	/* If we have neither username nor domain now, let's give up */
	if (ast_strlen_zero(username) && ast_strlen_zero(domain)) {
		ast_log(LOG_ERROR, "No account given\n");
		return -1;
	}

	if (ast_strlen_zero(countername)) {
		ast_log(LOG_ERROR, "This function needs two arguments: Account:countername\n");
		return -1;
	}

	/* We only have a domain, no username */
	if (!ast_strlen_zero(username) && ast_strlen_zero(domain)) {
		domain = username;
		username = NULL;
	}

	/* If we can't find account or if the account is temporary, return. */
	if (!ast_strlen_zero(username) && !find_account(domain, username, FALSE)) {
		ast_log(LOG_ERROR, "Minivm account does not exist: %s@%s\n", username, domain);
		return 0;
	}

	create_dirpath(userpath, sizeof(userpath), domain, username, NULL);

	/* We have the path, now read the counter file */
	res = access_counter_file(userpath, countername, 0, 0);
	if (res >= 0)
		snprintf(buf, len, "%d", res);
	return 0;
}

/*! \brief  ${MINIVMCOUNTER()} Dialplan function - changes counter data */
static int minivm_counter_func_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	char *username, *domain, *countername, *operand;
	char userpath[BUFSIZ];
	int change = 0;
	int operation = 0;

	if(!value)
		return -1;
	change = atoi(value);

	username = ast_strdupa(data);

	if ((countername = strchr(username, ':'))) {
		*countername = '\0';
		countername++;
	}
	if ((operand = strchr(countername, ':'))) {
		*operand = '\0';
		operand++;
	}

	if ((domain = strchr(username, '@'))) {
		*domain = '\0';
		domain++;
	}

	/* If we have neither username nor domain now, let's give up */
	if (ast_strlen_zero(username) && ast_strlen_zero(domain)) {
		ast_log(LOG_ERROR, "No account given\n");
		return -1;
	}

	/* We only have a domain, no username */
	if (!ast_strlen_zero(username) && ast_strlen_zero(domain)) {
		domain = username;
		username = NULL;
	}

	if (ast_strlen_zero(operand) || ast_strlen_zero(countername)) {
		ast_log(LOG_ERROR, "Writing to this function requires three arguments: Account:countername:operand\n");
		return -1;
	}

	/* If we can't find account or if the account is temporary, return. */
	if (!ast_strlen_zero(username) && !find_account(domain, username, FALSE)) {
		ast_log(LOG_ERROR, "Minivm account does not exist: %s@%s\n", username, domain);
		return 0;
	}

	create_dirpath(userpath, sizeof(userpath), domain, username, NULL);
	/* Now, find out our operator */
	if (*operand == 'i') /* Increment */
		operation = 2;
	else if (*operand == 'd') {
		change = change * -1;
		operation = 2;
	} else if (*operand == 's')
		operation = 1;
	else {
		ast_log(LOG_ERROR, "Unknown operator: %s\n", operand);
		return -1;
	}

	/* We have the path, now read the counter file */
	access_counter_file(userpath, countername, change, operation);
	return 0;
}


/*! \brief CLI commands for Mini-voicemail */
static struct ast_cli_entry cli_minivm[] = {
	AST_CLI_DEFINE(handle_minivm_show_users, "List defined mini-voicemail boxes"),
	AST_CLI_DEFINE(handle_minivm_show_zones, "List zone message formats"),
	AST_CLI_DEFINE(handle_minivm_list_templates, "List message templates"),
	AST_CLI_DEFINE(handle_minivm_reload, "Reload Mini-voicemail configuration"),
	AST_CLI_DEFINE(handle_minivm_show_stats, "Show some mini-voicemail statistics"),
	AST_CLI_DEFINE(handle_minivm_show_settings, "Show mini-voicemail general settings"),
};

static struct ast_custom_function minivm_counter_function = {
	.name = "MINIVMCOUNTER",
	.read = minivm_counter_func_read,
	.write = minivm_counter_func_write,
};

static struct ast_custom_function minivm_account_function = {
	.name = "MINIVMACCOUNT",
	.read = minivm_account_func_read,
};

/*! \brief Load mini voicemail module */
static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app_minivm_record, minivm_record_exec);
	res = ast_register_application_xml(app_minivm_greet, minivm_greet_exec);
	res = ast_register_application_xml(app_minivm_notify, minivm_notify_exec);
	res = ast_register_application_xml(app_minivm_delete, minivm_delete_exec);
	res = ast_register_application_xml(app_minivm_accmess, minivm_accmess_exec);
	res = ast_register_application_xml(app_minivm_mwi, minivm_mwi_exec);

	ast_custom_function_register(&minivm_account_function);
	ast_custom_function_register(&minivm_counter_function);
	if (res)
		return(res);

	if ((res = load_config(0)))
		return(res);

	ast_cli_register_multiple(cli_minivm, ARRAY_LEN(cli_minivm));

	/* compute the location of the voicemail spool directory */
	snprintf(MVM_SPOOL_DIR, sizeof(MVM_SPOOL_DIR), "%s/voicemail/", ast_config_AST_SPOOL_DIR);

	return res;
}

/*! \brief Reload mini voicemail module */
static int reload(void)
{
	return(load_config(1));
}

/*! \brief Reload cofiguration */
static char *handle_minivm_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{

	switch (cmd) {
	case CLI_INIT:
		e->command = "minivm reload";
		e->usage =
			"Usage: minivm reload\n"
			"       Reload mini-voicemail configuration and reset statistics\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	reload();
	ast_cli(a->fd, "\n-- Mini voicemail re-configured \n");
	return CLI_SUCCESS;
}

/*! \brief Unload mini voicemail module */
static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_minivm_record);
	res |= ast_unregister_application(app_minivm_greet);
	res |= ast_unregister_application(app_minivm_notify);
	res |= ast_unregister_application(app_minivm_delete);
	res |= ast_unregister_application(app_minivm_accmess);
	res |= ast_unregister_application(app_minivm_mwi);

	ast_cli_unregister_multiple(cli_minivm, ARRAY_LEN(cli_minivm));
	ast_custom_function_unregister(&minivm_account_function);
	ast_custom_function_unregister(&minivm_counter_function);

	message_destroy_list();		/* Destroy list of voicemail message templates */
	timezone_destroy_list();	/* Destroy list of timezones */
	vmaccounts_destroy_list();	/* Destroy list of voicemail accounts */

	return res;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Mini VoiceMail (A minimal Voicemail e-mail System)",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
