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

/*!
 * \file
 * \author Mark Spencer <markster@digium.com>
 * \brief Comedian Mail - Voicemail System
 *
 * unixODBC (http://www.unixodbc.org/)
 * A source distribution of University of Washington's IMAP c-client
 *         (http://www.washington.edu/imap/)
 *
 * \par See also
 * \arg \ref voicemail.conf "Config_voicemail"
 * \note For information about voicemail IMAP storage, https://wiki.asterisk.org/wiki/display/AST/IMAP+Voicemail+Storage
 * \ingroup applications
 * \todo This module requires res_adsi to load. This needs to be optional
 * during compilation.
 *
 * \todo This file is now almost impossible to work with, due to all \#ifdefs.
 *       Feels like the database code before realtime. Someone - please come up
 *       with a plan to clean this up.
 */

/*! \li \ref app_voicemail.c uses configuration file \ref voicemail.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page voicemail.conf voicemail.conf
 * \verbinclude voicemail.conf.sample
 */

#include "asterisk.h"

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

#include "asterisk/paths.h"	/* use ast_config_AST_SPOOL_DIR */
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <dirent.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/wait.h>
#endif

#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/module.h"
#include "asterisk/adsi.h"
#include "asterisk/app.h"
#include "asterisk/mwi.h"
#include "asterisk/manager.h"
#include "asterisk/dsp.h"
#include "asterisk/localtime.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/stringfields.h"
#include "asterisk/strings.h"
#include "asterisk/smdi.h"
#include "asterisk/astobj2.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/test.h"
#include "asterisk/format_cache.h"

#ifdef ODBC_STORAGE
#include "asterisk/res_odbc.h"
#endif

#ifdef IMAP_STORAGE
#include "asterisk/threadstorage.h"
#endif

/*** DOCUMENTATION
	<application name="VoiceMail" language="en_US">
		<synopsis>
			Leave a Voicemail message.
		</synopsis>
		<syntax>
			<parameter name="mailboxs" argsep="&amp;" required="true">
				<argument name="mailbox1" argsep="@" required="true">
					<argument name="mailbox" required="true" />
					<argument name="context" />
				</argument>
				<argument name="mailbox2" argsep="@" multiple="true">
					<argument name="mailbox" required="true" />
					<argument name="context" />
				</argument>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="b">
						<para>Play the <literal>busy</literal> greeting to the calling party.</para>
					</option>
					<option name="d">
						<argument name="c" />
						<para>Accept digits for a new extension in context <replaceable>c</replaceable>,
						if played during the greeting. Context defaults to the current context.</para>
					</option>
					<option name="e">
						<para>Play greetings as early media -- only answer the channel just
						before accepting the voice message.</para>
					</option>
					<option name="g">
						<argument name="#" required="true" />
						<para>Use the specified amount of gain when recording the voicemail
						message. The units are whole-number decibels (dB). Only works on supported
						technologies, which is DAHDI only.</para>
					</option>
					<option name="s">
						<para>Skip the playback of instructions for leaving a message to the
						calling party.</para>
					</option>
					<option name="S">
						<para>Skip the playback of instructions for leaving a message to the
						calling party, but only if a greeting has been recorded by the
						mailbox user.</para>
					</option>
					<option name="t">
						<argument name="x" required="false" />
						<para>Play a custom beep tone to the caller instead of the default one.
						If this option is used but no file is specified, the beep is suppressed.</para>
					</option>
					<option name="u">
						<para>Play the <literal>unavailable</literal> greeting.</para>
					</option>
					<option name="U">
						<para>Mark message as <literal>URGENT</literal>.</para>
					</option>
					<option name="P">
						<para>Mark message as <literal>PRIORITY</literal>.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application allows the calling party to leave a message for the specified
			list of mailboxes. When multiple mailboxes are specified, the greeting will be taken from
			the first mailbox specified. Dialplan execution will stop if the specified mailbox does not
			exist.</para>
			<para>The Voicemail application will exit if any of the following DTMF digits are received:</para>
			<enumlist>
				<enum name="0">
					<para>Jump to the <literal>o</literal> extension in the current dialplan context.</para>
				</enum>
				<enum name="*">
					<para>Jump to the <literal>a</literal> extension in the current dialplan context.</para>
				</enum>
			</enumlist>
			<para>This application will set the following channel variable upon completion:</para>
			<variablelist>
				<variable name="VMSTATUS">
					<para>This indicates the status of the execution of the VoiceMail application.</para>
					<value name="SUCCESS" />
					<value name="USEREXIT" />
					<value name="FAILED" />
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">VoiceMailMain</ref>
		</see-also>
	</application>
	<application name="VoiceMailMain" language="en_US">
		<synopsis>
			Check Voicemail messages.
		</synopsis>
		<syntax>
			<parameter name="mailbox" required="true" argsep="@">
				<argument name="mailbox" />
				<argument name="context" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="p">
						<para>Consider the <replaceable>mailbox</replaceable> parameter as a prefix to
						the mailbox that is entered by the caller.</para>
					</option>
					<option name="g">
						<argument name="#" required="true" />
						<para>Use the specified amount of gain when recording a voicemail message.
						The units are whole-number decibels (dB).</para>
					</option>
					<option name="r">
						<para>"Read only". Prevent user from deleting any messages.</para>
						<para>This applies only to specific executions of VoiceMailMain, NOT the mailbox itself.</para>
					</option>
					<option name="s">
						<para>Skip checking the passcode for the mailbox.</para>
					</option>
					<option name="a">
						<argument name="folder" required="true" />
						<para>Skip folder prompt and go directly to <replaceable>folder</replaceable> specified.
						Defaults to <literal>INBOX</literal> (or <literal>0</literal>).</para>
						<enumlist>
							<enum name="0"><para>INBOX</para></enum>
							<enum name="1"><para>Old</para></enum>
							<enum name="2"><para>Work</para></enum>
							<enum name="3"><para>Family</para></enum>
							<enum name="4"><para>Friends</para></enum>
							<enum name="5"><para>Cust1</para></enum>
							<enum name="6"><para>Cust2</para></enum>
							<enum name="7"><para>Cust3</para></enum>
							<enum name="8"><para>Cust4</para></enum>
							<enum name="9"><para>Cust5</para></enum>
						</enumlist>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application allows the calling party to check voicemail messages. A specific
			<replaceable>mailbox</replaceable>, and optional corresponding <replaceable>context</replaceable>,
			may be specified. If a <replaceable>mailbox</replaceable> is not provided, the calling party will
			be prompted to enter one. If a <replaceable>context</replaceable> is not specified, the
			<literal>default</literal> context will be used.</para>
			<para>The VoiceMailMain application will exit if the following DTMF digit is entered as Mailbox
			or Password, and the extension exists:</para>
			<enumlist>
				<enum name="*">
					<para>Jump to the <literal>a</literal> extension in the current dialplan context.</para>
				</enum>
			</enumlist>
		</description>
		<see-also>
			<ref type="application">VoiceMail</ref>
		</see-also>
	</application>
	<application name="VMAuthenticate" language="en_US">
		<synopsis>
			Authenticate with Voicemail passwords.
		</synopsis>
		<syntax>
			<parameter name="mailbox" required="true" argsep="@">
				<argument name="mailbox" />
				<argument name="context" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="s">
						<para>Skip playing the initial prompts.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application behaves the same way as the Authenticate application, but the passwords
			are taken from <filename>voicemail.conf</filename>. If the <replaceable>mailbox</replaceable> is
			specified, only that mailbox's password will be considered valid. If the <replaceable>mailbox</replaceable>
			is not specified, the channel variable <variable>AUTH_MAILBOX</variable> will be set with the authenticated
			mailbox.</para>
			<para>The VMAuthenticate application will exit if the following DTMF digit is entered as Mailbox
			or Password, and the extension exists:</para>
			<enumlist>
				<enum name="*">
					<para>Jump to the <literal>a</literal> extension in the current dialplan context.</para>
				</enum>
			</enumlist>
		</description>
	</application>
	<application name="VoiceMailPlayMsg" language="en_US">
		<synopsis>
			Play a single voice mail msg from a mailbox by msg id.
		</synopsis>
		<syntax>
			<parameter name="mailbox" required="true" argsep="@">
				<argument name="mailbox" />
				<argument name="context" />
			</parameter>
			<parameter name="msg_id" required="true">
				<para>The msg id of the msg to play back. </para>
			</parameter>
		</syntax>
		<description>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="VOICEMAIL_PLAYBACKSTATUS">
					<para>The status of the playback attempt as a text string.</para>
					<value name="SUCCESS"/>
					<value name="FAILED"/>
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="VMSayName" language="en_US">
		<synopsis>
			Play the name of a voicemail user
		</synopsis>
		<syntax>
			<parameter name="mailbox" required="true" argsep="@">
				<argument name="mailbox" />
				<argument name="context" />
			</parameter>
		</syntax>
		<description>
			<para>This application will say the recorded name of the voicemail user specified as the
			argument to this application. If no context is provided, <literal>default</literal> is assumed.</para>
			<para>Similar to the Background() application, playback of the recorded
			name can be interrupted by entering an extension, which will be searched
			for in the current context.</para>
		</description>
	</application>
	<function name="VM_INFO" language="en_US">
		<synopsis>
			Returns the selected attribute from a mailbox.
		</synopsis>
		<syntax argsep=",">
			<parameter name="mailbox" argsep="@" required="true">
				<argument name="mailbox" required="true" />
				<argument name="context" />
			</parameter>
			<parameter name="attribute" required="true">
				<optionlist>
					<option name="count">
						<para>Count of messages in specified <replaceable>folder</replaceable>.
						If <replaceable>folder</replaceable> is not specified, defaults to <literal>INBOX</literal>.</para>
					</option>
					<option name="email">
						<para>E-mail address associated with the mailbox.</para>
					</option>
					<option name="exists">
						<para>Returns a boolean of whether the corresponding <replaceable>mailbox</replaceable> exists.</para>
					</option>
					<option name="fullname">
						<para>Full name associated with the mailbox.</para>
					</option>
					<option name="language">
						<para>Mailbox language if overridden, otherwise the language of the channel.</para>
					</option>
					<option name="locale">
						<para>Mailbox locale if overridden, otherwise global locale.</para>
					</option>
					<option name="pager">
						<para>Pager e-mail address associated with the mailbox.</para>
					</option>
					<option name="password">
						<para>Mailbox access password.</para>
					</option>
					<option name="tz">
						<para>Mailbox timezone if overridden, otherwise global timezone</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="folder" required="false">
				<para>If not specified, <literal>INBOX</literal> is assumed.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns the selected attribute from the specified <replaceable>mailbox</replaceable>.
			If <replaceable>context</replaceable> is not specified, defaults to the <literal>default</literal>
			context. Where the <replaceable>folder</replaceable> can be specified, common folders
			include <literal>INBOX</literal>, <literal>Old</literal>, <literal>Work</literal>,
			<literal>Family</literal> and <literal>Friends</literal>.</para>
		</description>
	</function>
	<manager name="VoicemailUsersList" language="en_US">
		<synopsis>
			List All Voicemail User Information.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="VoicemailUserStatus" language="en_US">
		<synopsis>
			Show the status of given voicemail user's info.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Context" required="true">
				<para>The context you want to check.</para>
			</parameter>
			<parameter name="Mailbox" required="true">
				<para>The mailbox you want to check.</para>
			</parameter>
		</syntax>
		<description>
			<para>Retrieves the status of the given voicemail user.</para>
		</description>
	</manager>
	<manager name="VoicemailRefresh" language="en_US">
		<synopsis>
			Tell Asterisk to poll mailboxes for a change
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Context" />
			<parameter name="Mailbox" />
		</syntax>
		<description>
			<para>Normally, MWI indicators are only sent when Asterisk itself
			changes a mailbox.  With external programs that modify the content
			of a mailbox from outside the application, an option exists called
			<literal>pollmailboxes</literal> that will cause voicemail to
			continually scan all mailboxes on a system for changes.  This can
			cause a large amount of load on a system.  This command allows
			external applications to signal when a particular mailbox has
			changed, thus permitting external applications to modify mailboxes
			and MWI to work without introducing considerable CPU load.</para>
			<para>If <replaceable>Context</replaceable> is not specified, all
			mailboxes on the system will be polled for changes.  If
			<replaceable>Context</replaceable> is specified, but
			<replaceable>Mailbox</replaceable> is omitted, then all mailboxes
			within <replaceable>Context</replaceable> will be polled.
			Otherwise, only a single mailbox will be polled for changes.</para>
		</description>
	</manager>
	<manager name="VoicemailBoxSummary" language="en_US">
		<synopsis>
			Show the mailbox contents of given voicemail user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Context" required="true">
				<para>The context you want to check.</para>
			</parameter>
			<parameter name="Mailbox" required="true">
				<para>The mailbox you want to check.</para>
			</parameter>
		</syntax>
		<description>
			<para>Retrieves the contents of the given voicemail user's mailbox.</para>
		</description>
	</manager>
	<manager name="VoicemailMove" language="en_US">
		<synopsis>
			Move Voicemail between mailbox folders of given user.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Context" required="true">
				<para>The context of the Voicemail you want to move.</para>
			</parameter>
			<parameter name="Mailbox" required="true">
				<para>The mailbox of the Voicemail you want to move.</para>
			</parameter>
			<parameter name="Folder" required="true">
				<para>The Folder containing the Voicemail you want to move.</para>
			</parameter>
			<parameter name="ID" required="true">
				<para>The ID of the Voicemail you want to move.</para>
			</parameter>
			<parameter name="ToFolder" required="true">
				<para>The Folder you want to move the Voicemail to.</para>
			</parameter>
		</syntax>
		<description>
			<para>Move a given Voicemail between Folders within a user's Mailbox.</para>
		</description>
	</manager>
	<manager name="VoicemailRemove" language="en_US">
		<synopsis>
			Remove Voicemail from mailbox folder.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Context" required="true">
				<para>The context of the Voicemail you want to remove.</para>
			</parameter>
			<parameter name="Mailbox" required="true">
				<para>The mailbox of the Voicemail you want to remove.</para>
			</parameter>
			<parameter name="Folder" required="true">
				<para>The Folder containing the Voicemail you want to remove.</para>
			</parameter>
			<parameter name="ID" required="true">
				<para>The ID of the Voicemail you want to remove.</para>
			</parameter>
		</syntax>
		<description>
			<para>Remove a given Voicemail from a user's Mailbox Folder.</para>
		</description>
	</manager>
	<manager name="VoicemailForward" language="en_US">
		<synopsis>
			Forward Voicemail from one mailbox folder to another between given users.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Context" required="true">
				<para>The context of the Voicemail you want to move.</para>
			</parameter>
			<parameter name="Mailbox" required="true">
				<para>The mailbox of the Voicemail you want to move.</para>
			</parameter>
			<parameter name="Folder" required="true">
				<para>The Folder containing the Voicemail you want to move.</para>
			</parameter>
			<parameter name="ID" required="true">
				<para>The ID of the Voicemail you want to move.</para>
			</parameter>
			<parameter name="ToContext" required="true">
				<para>The context you want to move the Voicemail to.</para>
			</parameter>
			<parameter name="ToMailbox" required="true">
				<para>The mailbox you want to move the Voicemail to.</para>
			</parameter>
			<parameter name="ToFolder" required="true">
				<para>The Folder you want to move the Voicemail to.</para>
			</parameter>
		</syntax>
		<description>
			<para>Forward a given Voicemail from a user's Mailbox Folder to
			another user's Mailbox Folder. Can be used to copy between
			Folders within a mailbox by specifying the to context and user
			as the same as the from.</para>
		</description>
	</manager>
 ***/

#ifdef IMAP_STORAGE
static char imapserver[48] = "localhost";
static char imapport[8] = "143";
static char imapflags[128];
static char imapfolder[64] = "INBOX";
static char imapparentfolder[64];
static char greetingfolder[64] = "INBOX";
static char authuser[32];
static char authpassword[42];
static int imapversion = 1;

static int expungeonhangup = 1;
static int imapgreetings;
static int imap_poll_logout;
static char delimiter;

/* mail_open cannot be protected on a stream basis */
ast_mutex_t mail_open_lock;

struct vm_state;
struct ast_vm_user;

AST_THREADSTORAGE(ts_vmstate);

/* Forward declarations for IMAP */
static int init_mailstream(struct vm_state *vms, int box);
static void write_file(char *filename, char *buffer, unsigned long len);
static char *get_header_by_tag(char *header, char *tag, char *buf, size_t len);
static void vm_imap_delete(char *file, int msgnum, struct ast_vm_user *vmu);
static char *get_user_by_mailbox(char *mailbox, char *buf, size_t len);
static struct vm_state *get_vm_state_by_imapuser(const char *user, int interactive);
static struct vm_state *get_vm_state_by_mailbox(const char *mailbox, const char *context, int interactive);
static struct vm_state *create_vm_state_from_user(struct ast_vm_user *vmu);
static void vmstate_insert(struct vm_state *vms);
static void vmstate_delete(struct vm_state *vms);
static void set_update(MAILSTREAM * stream);
static void init_vm_state(struct vm_state *vms);
static int save_body(BODY *body, struct vm_state *vms, char *section, char *format, int is_intro);
static void get_mailbox_delimiter(struct vm_state *vms, MAILSTREAM *stream);
static void mm_parsequota (MAILSTREAM *stream, unsigned char *msg, QUOTALIST *pquota);
static void imap_mailbox_name(char *spec, size_t len, struct vm_state *vms, int box, int target);
static int imap_store_file(const char *dir, const char *mailboxuser, const char *mailboxcontext, int msgnum, struct ast_channel *chan, struct ast_vm_user *vmu, char *fmt, int duration, struct vm_state *vms, const char *flag, const char *msg_id);
static void vm_imap_update_msg_id(char *dir, int msgnum, const char *msg_id, struct ast_vm_user *vmu, struct ast_config *msg_cfg, int folder);
static void update_messages_by_imapuser(const char *user, unsigned long number);
static int vm_delete(char *file);

static int imap_remove_file (char *dir, int msgnum);
static int imap_retrieve_file (const char *dir, const int msgnum, const char *mailbox, const char *context);
static int imap_delete_old_greeting (char *dir, struct vm_state *vms);
static void check_quota(struct vm_state *vms, char *mailbox);
static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu, int box);
static void imap_logout(const char *mailbox_id);

struct vmstate {
	struct vm_state *vms;
	AST_LIST_ENTRY(vmstate) list;
};

static AST_LIST_HEAD_STATIC(vmstates, vmstate);

#endif

#define SMDI_MWI_WAIT_TIMEOUT 1000 /* 1 second */

#define COMMAND_TIMEOUT 5000
/* Don't modify these here; set your umask at runtime instead */
#define	VOICEMAIL_DIR_MODE	0777
#define	VOICEMAIL_FILE_MODE	0666
#define	CHUNKSIZE	65536

#define VOICEMAIL_CONFIG "voicemail.conf"
#define ASTERISK_USERNAME "asterisk"

/* Define fast-forward, pause, restart, and reverse keys
 * while listening to a voicemail message - these are
 * strings, not characters */
#define DEFAULT_LISTEN_CONTROL_FORWARD_KEY "#"
#define DEFAULT_LISTEN_CONTROL_REVERSE_KEY "*"
#define DEFAULT_LISTEN_CONTROL_PAUSE_KEY "0"
#define DEFAULT_LISTEN_CONTROL_RESTART_KEY "2"
#define DEFAULT_LISTEN_CONTROL_STOP_KEY "13456789"
#define VALID_DTMF "1234567890*#" /* Yes ABCD are valid dtmf but what phones have those? */

/* Default mail command to mail voicemail. Change it with the
 * mailcmd= command in voicemail.conf */
#define SENDMAIL "/usr/sbin/sendmail -t"
#define INTRO "vm-intro"

#define MAX_MAIL_BODY_CONTENT_SIZE 134217728L // 128 Mbyte

#define MAXMSG 100
#define MAXMSGLIMIT 9999

#define MINPASSWORD 0 /*!< Default minimum mailbox password length */

#ifdef IMAP_STORAGE
#define ENDL "\r\n"
#else
#define ENDL "\n"
#endif

#define MAX_DATETIME_FORMAT	512
#define MAX_NUM_CID_CONTEXTS 10

#define VM_REVIEW        (1 << 0)   /*!< After recording, permit the caller to review the recording before saving */
#define VM_OPERATOR      (1 << 1)   /*!< Allow 0 to be pressed to go to 'o' extension */
#define VM_SAYCID        (1 << 2)   /*!< Repeat the CallerID info during envelope playback */
#define VM_SVMAIL        (1 << 3)   /*!< Allow the user to compose a new VM from within VoicemailMain */
#define VM_ENVELOPE      (1 << 4)   /*!< Play the envelope information (who-from, time received, etc.) */
#define VM_SAYDURATION   (1 << 5)   /*!< Play the length of the message during envelope playback */
#define VM_SKIPAFTERCMD  (1 << 6)   /*!< After deletion, assume caller wants to go to the next message */
#define VM_FORCENAME     (1 << 7)   /*!< Have new users record their name */
#define VM_FORCEGREET    (1 << 8)   /*!< Have new users record their greetings */
#define VM_PBXSKIP       (1 << 9)   /*!< Skip the [PBX] preamble in the Subject line of emails */
#define VM_DIRECTFORWARD (1 << 10)  /*!< Permit caller to use the Directory app for selecting to which mailbox to forward a VM */
#define VM_ATTACH        (1 << 11)  /*!< Attach message to voicemail notifications? */
#define VM_DELETE        (1 << 12)  /*!< Delete message after sending notification */
#define VM_ALLOCED       (1 << 13)  /*!< Structure was malloc'ed, instead of placed in a return (usually static) buffer */
#define VM_SEARCH        (1 << 14)  /*!< Search all contexts for a matching mailbox */
#define VM_TEMPGREETWARN (1 << 15)  /*!< Remind user tempgreeting is set */
#define VM_MOVEHEARD     (1 << 16)  /*!< Move a "heard" message to Old after listening to it */
#define VM_MESSAGEWRAP   (1 << 17)  /*!< Wrap around from the last message to the first, and vice-versa */
#define VM_FWDURGAUTO    (1 << 18)  /*!< Autoset of Urgent flag on forwarded Urgent messages set globally */
#define VM_EMAIL_EXT_RECS (1 << 19)  /*!< Send voicemail emails when an external recording is added to a mailbox */
#define ERROR_LOCK_PATH  -100
#define ERROR_MAX_MSGS   -101
#define OPERATOR_EXIT     300

enum vm_box {
	NEW_FOLDER = 		0,
	OLD_FOLDER =		1,
	WORK_FOLDER =		2,
	FAMILY_FOLDER =		3,
	FRIENDS_FOLDER =	4,
	GREETINGS_FOLDER =	-1
};

enum vm_option_flags {
	OPT_SILENT =           (1 << 0),
	OPT_BUSY_GREETING =    (1 << 1),
	OPT_UNAVAIL_GREETING = (1 << 2),
	OPT_RECORDGAIN =       (1 << 3),
	OPT_PREPEND_MAILBOX =  (1 << 4),
	OPT_AUTOPLAY =         (1 << 6),
	OPT_DTMFEXIT =         (1 << 7),
	OPT_MESSAGE_Urgent =   (1 << 8),
	OPT_MESSAGE_PRIORITY = (1 << 9),
	OPT_EARLYM_GREETING =  (1 << 10),
	OPT_BEEP =             (1 << 11),
	OPT_SILENT_IF_GREET =  (1 << 12),
	OPT_READONLY =         (1 << 13),
};

enum vm_option_args {
	OPT_ARG_RECORDGAIN = 0,
	OPT_ARG_PLAYFOLDER = 1,
	OPT_ARG_DTMFEXIT   = 2,
	OPT_ARG_BEEP_TONE  = 3,
	/* This *must* be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE = 4,
};

enum vm_passwordlocation {
	OPT_PWLOC_VOICEMAILCONF = 0,
	OPT_PWLOC_SPOOLDIR      = 1,
	OPT_PWLOC_USERSCONF     = 2,
};

AST_APP_OPTIONS(vm_app_options, {
	AST_APP_OPTION('s', OPT_SILENT),
	AST_APP_OPTION('S', OPT_SILENT_IF_GREET),
	AST_APP_OPTION('b', OPT_BUSY_GREETING),
	AST_APP_OPTION('u', OPT_UNAVAIL_GREETING),
	AST_APP_OPTION_ARG('g', OPT_RECORDGAIN, OPT_ARG_RECORDGAIN),
	AST_APP_OPTION_ARG('d', OPT_DTMFEXIT, OPT_ARG_DTMFEXIT),
	AST_APP_OPTION('p', OPT_PREPEND_MAILBOX),
	AST_APP_OPTION_ARG('a', OPT_AUTOPLAY, OPT_ARG_PLAYFOLDER),
	AST_APP_OPTION('U', OPT_MESSAGE_Urgent),
	AST_APP_OPTION('P', OPT_MESSAGE_PRIORITY),
	AST_APP_OPTION('e', OPT_EARLYM_GREETING),
	AST_APP_OPTION_ARG('t', OPT_BEEP, OPT_ARG_BEEP_TONE),
	AST_APP_OPTION('r', OPT_READONLY),
});

static const char * const mailbox_folders[] = {
#ifdef IMAP_STORAGE
	imapfolder,
#else
	"INBOX",
#endif
	"Old",
	"Work",
	"Family",
	"Friends",
	"Cust1",
	"Cust2",
	"Cust3",
	"Cust4",
	"Cust5",
	"Deleted",
	"Urgent",
};

static int load_config(int reload);
#ifdef TEST_FRAMEWORK
static int load_config_from_memory(int reload, struct ast_config *cfg, struct ast_config *ucfg);
#endif
static int actual_load_config(int reload, struct ast_config *cfg, struct ast_config *ucfg);

/*! \page vmlang Voicemail Language Syntaxes Supported

	\par Syntaxes supported, not really language codes.
	\arg \b en    - English
	\arg \b de    - German
	\arg \b es    - Spanish
	\arg \b fr    - French
	\arg \b it    - Italian
	\arg \b nl    - Dutch
	\arg \b pt    - Portuguese
	\arg \b pt_BR - Portuguese (Brazil)
	\arg \b gr    - Greek
	\arg \b no    - Norwegian
	\arg \b se    - Swedish
	\arg \b tw    - Chinese (Taiwan)
	\arg \b ua - Ukrainian

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

Japanese requires the following additional soundfile:
\arg \b jp-arimasu          there is
\arg \b jp-arimasen         there is not
\arg \b jp-oshitekudasai    please press
\arg \b jp-ni               article ni
\arg \b jp-ga               article ga
\arg \b jp-wa               article wa
\arg \b jp-wo               article wo

Chinese (Taiwan) requires the following additional soundfile:
\arg \b vm-tong		A class-word for call (tong1)
\arg \b vm-ri		A class-word for day (ri4)
\arg \b vm-you		You (ni3)
\arg \b vm-haveno   Have no (mei2 you3)
\arg \b vm-have     Have (you3)
\arg \b vm-listen   To listen (yao4 ting1)


\note Don't use vm-INBOX or vm-Old, because they are the name of the INBOX and Old folders,
spelled among others when you have to change folder. For the above reasons, vm-INBOX
and vm-Old are spelled plural, to make them sound more as folder name than an adjective.

*/

#define MAX_VM_MBOX_ID_LEN (AST_MAX_EXTENSION)
#define MAX_VM_CONTEXT_LEN (AST_MAX_CONTEXT)
/* MAX_VM_MAILBOX_LEN allows enough room for the '@' and NULL terminator */
#define MAX_VM_MAILBOX_LEN (MAX_VM_MBOX_ID_LEN + MAX_VM_CONTEXT_LEN)

/*! Structure for linked list of users
 * Use ast_vm_user_destroy() to free one of these structures. */
struct ast_vm_user {
	char context[MAX_VM_CONTEXT_LEN];/*!< Voicemail context */
	char mailbox[MAX_VM_MBOX_ID_LEN];/*!< Mailbox id, unique within vm context */
	char password[80];               /*!< Secret pin code, numbers only */
	char fullname[80];               /*!< Full name, for directory app */
	char *email;                     /*!< E-mail address */
	char *emailsubject;              /*!< E-mail subject */
	char *emailbody;                 /*!< E-mail body */
	char pager[80];                  /*!< E-mail address to pager (no attachment) */
	char serveremail[80];            /*!< From: Mail address */
	char fromstring[100];            /*!< From: Username */
	char language[MAX_LANGUAGE];     /*!< Config: Language setting */
	char zonetag[80];                /*!< Time zone */
	char locale[20];                 /*!< The locale (for presentation of date/time) */
	char callback[80];
	char dialout[80];
	char uniqueid[80];               /*!< Unique integer identifier */
	char exit[80];
	char attachfmt[20];              /*!< Attachment format */
	unsigned int flags;              /*!< VM_ flags */
	int saydurationm;
	int minsecs;                     /*!< Minimum number of seconds per message for this mailbox */
	int maxmsg;                      /*!< Maximum number of msgs per folder for this mailbox */
	int maxdeletedmsg;               /*!< Maximum number of deleted msgs saved for this mailbox */
	int maxsecs;                     /*!< Maximum number of seconds per message for this mailbox */
	int passwordlocation;            /*!< Storage location of the password */
#ifdef IMAP_STORAGE
	char imapserver[48];             /*!< IMAP server address */
	char imapport[8];                /*!< IMAP server port */
	char imapflags[128];             /*!< IMAP optional flags */
	char imapuser[80];               /*!< IMAP server login */
	char imappassword[80];           /*!< IMAP server password if authpassword not defined */
	char imapfolder[64];             /*!< IMAP voicemail folder */
	char imapvmshareid[80];          /*!< Shared mailbox ID to use rather than the dialed one */
	int imapversion;                 /*!< If configuration changes, use the new values */
#endif
	double volgain;                  /*!< Volume gain for voicemails sent via email */
	AST_LIST_ENTRY(ast_vm_user) list;
};

/*! Voicemail time zones */
struct vm_zone {
	AST_LIST_ENTRY(vm_zone) list;
	char name[80];
	char timezone[80];
	char msg_format[512];
};

#define VMSTATE_MAX_MSG_ARRAY 256

/*! Voicemail mailbox state */
struct vm_state {
	char curbox[80];
	char username[80];
	char context[80];
	char curdir[PATH_MAX];
	char vmbox[PATH_MAX];
	char fn[PATH_MAX];
	char intro[PATH_MAX];
	int *deleted;
	int *heard;
	int dh_arraysize; /* used for deleted / heard allocation */
	int curmsg;
	int lastmsg;
	int newmessages;
	int oldmessages;
	int urgentmessages;
	int starting;
	int repeats;
#ifdef IMAP_STORAGE
	ast_mutex_t lock;
	int updated;                         /*!< decremented on each mail check until 1 -allows delay */
	long *msgArray;
	unsigned msg_array_max;
	MAILSTREAM *mailstream;
	int vmArrayIndex;
	char imapuser[80];                   /*!< IMAP server login */
	char imapfolder[64];                 /*!< IMAP voicemail folder */
	char imapserver[48];                 /*!< IMAP server address */
	char imapport[8];                    /*!< IMAP server port */
	char imapflags[128];                 /*!< IMAP optional flags */
	int imapversion;
	int interactive;
	char introfn[PATH_MAX];              /*!< Name of prepended file */
	unsigned int quota_limit;
	unsigned int quota_usage;
	struct vm_state *persist_vms;
#endif
};

#ifdef ODBC_STORAGE
static char odbc_database[80] = "asterisk";
static char odbc_table[80] = "voicemessages";
#define RETRIEVE(a,b,c,d) retrieve_file(a,b)
#define DISPOSE(a,b) remove_file(a,b)
#define STORE(a,b,c,d,e,f,g,h,i,j,k) store_file(a,b,c,d)
#define EXISTS(a,b,c,d) (message_exists(a,b))
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(a,b,c,d,e,f))
#define COPY(a,b,c,d,e,f,g,h) (copy_file(a,b,c,d,e,f))
#define DELETE(a,b,c,d) (delete_file(a,b))
#define UPDATE_MSG_ID(a, b, c, d, e, f) (odbc_update_msg_id((a), (b), (c)))
#else
#ifdef IMAP_STORAGE
#define DISPOSE(a,b) (imap_remove_file(a,b))
#define STORE(a,b,c,d,e,f,g,h,i,j,k) (imap_store_file(a,b,c,d,e,f,g,h,i,j,k))
#define RETRIEVE(a,b,c,d) imap_retrieve_file(a,b,c,d)
#define EXISTS(a,b,c,d) (ast_fileexists(c,NULL,d) > 0)
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(g,h));
#define COPY(a,b,c,d,e,f,g,h) (copy_file(g,h));
#define DELETE(a,b,c,d) (vm_imap_delete(a,b,d))
#define UPDATE_MSG_ID(a, b, c, d, e, f) (vm_imap_update_msg_id((a), (b), (c), (d), (e), (f)))
#else
#define RETRIEVE(a,b,c,d)
#define DISPOSE(a,b)
#define STORE(a,b,c,d,e,f,g,h,i,j,k)
#define EXISTS(a,b,c,d) (ast_fileexists(c,NULL,d) > 0)
#define RENAME(a,b,c,d,e,f,g,h) (rename_file(g,h));
#define COPY(a,b,c,d,e,f,g,h) (copy_plain_file(g,h));
#define DELETE(a,b,c,d) (vm_delete(c))
#define UPDATE_MSG_ID(a, b, c, d, e, f)
#endif
#endif

static char VM_SPOOL_DIR[PATH_MAX];

static char ext_pass_cmd[128];
static char ext_pass_check_cmd[128];

static int my_umask;

#define PWDCHANGE_INTERNAL (1 << 1)
#define PWDCHANGE_EXTERNAL (1 << 2)
static int pwdchange = PWDCHANGE_INTERNAL;

#ifdef ODBC_STORAGE
#define tdesc "Comedian Mail (Voicemail System) with ODBC Storage"
#else
# ifdef IMAP_STORAGE
# define tdesc "Comedian Mail (Voicemail System) with IMAP Storage"
# else
# define tdesc "Comedian Mail (Voicemail System)"
# endif
#endif

static char userscontext[AST_MAX_EXTENSION] = "default";

static char *addesc = "Comedian Mail";

/* Leave a message */
static char *voicemail_app = "VoiceMail";

/* Check mail, control, etc */
static char *voicemailmain_app = "VoiceMailMain";

static char *vmauthenticate_app = "VMAuthenticate";

static char *playmsg_app = "VoiceMailPlayMsg";

static char *sayname_app = "VMSayName";

static AST_LIST_HEAD_STATIC(users, ast_vm_user);
static AST_LIST_HEAD_STATIC(zones, vm_zone);
static char zonetag[80];
static char locale[20];
static int maxsilence;
static int maxmsg = MAXMSG;
static int maxdeletedmsg;
static int silencethreshold = 128;
static char serveremail[80] = ASTERISK_USERNAME;
static char mailcmd[160] = SENDMAIL;	/* Configurable mail cmd */
static char externnotify[160];
static struct ast_smdi_interface *smdi_iface = NULL;
static char vmfmts[80] = "wav";
static double volgain;
static int vmminsecs;
static int vmmaxsecs;
static int maxgreet;
static int skipms = 3000;
static int maxlogins = 3;
static int minpassword = MINPASSWORD;
static int passwordlocation;
static char aliasescontext[MAX_VM_CONTEXT_LEN];

/*! Poll mailboxes for changes since there is something external to
 *  app_voicemail that may change them. */
static unsigned int poll_mailboxes;

/*! By default, poll every 30 seconds */
#define DEFAULT_POLL_FREQ 30
/*! Polling frequency */
static unsigned int poll_freq = DEFAULT_POLL_FREQ;

AST_MUTEX_DEFINE_STATIC(poll_lock);
static ast_cond_t poll_cond = PTHREAD_COND_INITIALIZER;
static pthread_t poll_thread = AST_PTHREADT_NULL;
static unsigned char poll_thread_run;

static struct ast_taskprocessor *mwi_subscription_tps;

struct alias_mailbox_mapping {
	char *alias;
	char *mailbox;
	char buf[0];
};

struct mailbox_alias_mapping {
	char *alias;
	char *mailbox;
	char buf[0];
};

#define MAPPING_BUCKETS 511
static struct ao2_container *alias_mailbox_mappings;
AO2_STRING_FIELD_HASH_FN(alias_mailbox_mapping, alias);
AO2_STRING_FIELD_CMP_FN(alias_mailbox_mapping, alias);

static struct ao2_container *mailbox_alias_mappings;
AO2_STRING_FIELD_HASH_FN(mailbox_alias_mapping, mailbox);
AO2_STRING_FIELD_CMP_FN(mailbox_alias_mapping, mailbox);

/* custom audio control prompts for voicemail playback */
static char listen_control_forward_key[12];
static char listen_control_reverse_key[12];
static char listen_control_pause_key[12];
static char listen_control_restart_key[12];
static char listen_control_stop_key[12];

/* custom password sounds */
static char vm_login[80] = "vm-login";
static char vm_newuser[80] = "vm-newuser";
static char vm_password[80] = "vm-password";
static char vm_newpassword[80] = "vm-newpassword";
static char vm_passchanged[80] = "vm-passchanged";
static char vm_reenterpassword[80] = "vm-reenterpassword";
static char vm_mismatch[80] = "vm-mismatch";
static char vm_invalid_password[80] = "vm-invalid-password";
static char vm_pls_try_again[80] = "vm-pls-try-again";

/*
 * XXX If we have the time, motivation, etc. to fix up this prompt, one of the following would be appropriate:
 * 1. create a sound along the lines of "Please try again.  When done, press the pound key" which could be spliced
 * from existing sound clips.  This would require some programming changes in the area of vm_forward options and also
 * app.c's __ast_play_and_record function
 * 2. create a sound prompt saying "Please try again.  When done recording, press any key to stop and send the prepended
 * message."  At the time of this comment, I think this would require new voice work to be commissioned.
 * 3. Something way different like providing instructions before a time out or a post-recording menu.  This would require
 * more effort than either of the other two.
 */
static char vm_prepend_timeout[80] = "vm-then-pound";

static struct ast_flags globalflags = {0};

static int saydurationminfo = 2;

static char dialcontext[AST_MAX_CONTEXT] = "";
static char callcontext[AST_MAX_CONTEXT] = "";
static char exitcontext[AST_MAX_CONTEXT] = "";

static char cidinternalcontexts[MAX_NUM_CID_CONTEXTS][64];


static char *emailbody;
static char *emailsubject;
static char *pagerbody;
static char *pagersubject;
static char fromstring[100];
static char pagerfromstring[100];
static char charset[32] = "ISO-8859-1";

static unsigned char adsifdn[4] = "\x00\x00\x00\x0F";
static unsigned char adsisec[4] = "\x9B\xDB\xF7\xAC";
static int adsiver = 1;
static char emaildateformat[32] = "%A, %B %d, %Y at %r";
static char pagerdateformat[32] = "%A, %B %d, %Y at %r";

/* Forward declarations - generic */
static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu, int box);
static int close_mailbox(struct vm_state *vms, struct ast_vm_user *vmu);
static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg, int option, signed char record_gain);
static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context);
static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime,
			char *fmt, int outsidecaller, struct ast_vm_user *vmu, int *duration, int *sound_duration, const char *unlockdir,
			signed char record_gain, struct vm_state *vms, char *flag, const char *msg_id, int forwardintro);
static int vm_tempgreeting(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain);
static int vm_play_folder_name(struct ast_channel *chan, char *mbox);
static int notify_new_message(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msgnum, long duration, char *fmt, char *cidnum, char *cidname, const char *flag);
static void make_email_file(FILE *p, char *srcemail, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, const char *fromfolder, char *cidnum, char *cidname, char *attach, char *attach2, char *format, int duration, int attach_user_voicemail, struct ast_channel *chan, const char *category, int imap, const char *flag, const char *msg_id);
static void apply_options(struct ast_vm_user *vmu, const char *options);
static int add_email_attachment(FILE *p, struct ast_vm_user *vmu, char *format, char *attach, char *greeting_attachment, char *mailbox, char *bound, char *filename, int last, int msgnum);
static int is_valid_dtmf(const char *key);
static void read_password_from_file(const char *secretfn, char *password, int passwordlen);
static int write_password_to_file(const char *secretfn, const char *password);
static const char *substitute_escapes(const char *value);
static int message_range_and_existence_check(struct vm_state *vms, const char *msg_ids [], size_t num_msgs, int *msg_nums, struct ast_vm_user *vmu);
static void notify_new_state(struct ast_vm_user *vmu);
static int append_vmu_info_astman(struct mansession *s, struct ast_vm_user *vmu, const char* event_name, const char* actionid);
static int append_vmbox_info_astman(struct mansession *s, const struct message *m, struct ast_vm_user *vmu, const char* event_name, const char* actionid);


/*!
 * Place a message in the indicated folder
 *
 * \param vmu Voicemail user
 * \param vms Current voicemail state for the user
 * \param msg The message number to save
 * \param box The folder into which the message should be saved
 * \param[out] newmsg The new message number of the saved message
 * \param move Tells whether to copy or to move the message
 *
 * \note the "move" parameter is only honored for IMAP voicemail presently
 * \retval 0 Success
 * \retval other Failure
 */
static int save_to_folder(struct ast_vm_user *vmu, struct vm_state *vms, int msg, int box, int *newmsg, int move);

static struct ast_vm_mailbox_snapshot *vm_mailbox_snapshot_create(const char *mailbox, const char *context, const char *folder, int descending, enum ast_vm_snapshot_sort_val sort_val, int combine_INBOX_and_OLD);
static struct ast_vm_mailbox_snapshot *vm_mailbox_snapshot_destroy(struct ast_vm_mailbox_snapshot *mailbox_snapshot);

static int vm_msg_forward(const char *from_mailbox, const char *from_context, const char *from_folder, const char *to_mailbox, const char *to_context, const char *to_folder, size_t num_msgs, const char *msg_ids[], int delete_old);
static int vm_msg_move(const char *mailbox, const char *context, size_t num_msgs, const char *oldfolder, const char *old_msg_ids[], const char *newfolder);
static int vm_msg_remove(const char *mailbox, const char *context, size_t num_msgs, const char *folder, const char *msgs[]);
static int vm_msg_play(struct ast_channel *chan, const char *mailbox, const char *context, const char *folder, const char *msg_num, ast_vm_msg_play_cb cb);

#ifdef TEST_FRAMEWORK
static int vm_test_destroy_user(const char *context, const char *mailbox);
static int vm_test_create_user(const char *context, const char *mailbox);
#endif

/*!
 * \internal
 * \brief Parse the given mailbox_id into mailbox and context.
 * \since 12.0.0
 *
 * \param mailbox_id The mailbox\@context string to separate.
 * \param mailbox Where the mailbox part will start.
 * \param context Where the context part will start.  ("default" if not present)
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int separate_mailbox(char *mailbox_id, char **mailbox, char **context)
{
	if (ast_strlen_zero(mailbox_id) || !mailbox || !context) {
		return -1;
	}
	*context = mailbox_id;
	*mailbox = strsep(context, "@");
	if (ast_strlen_zero(*mailbox)) {
		return -1;
	}
	if (ast_strlen_zero(*context)) {
		*context = "default";
	}
	return 0;
}

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
	int context_len = strlen(context) + 1;
	int mailbox_len = strlen(mailbox) + 1;
	struct inprocess *i, *arg = ast_alloca(sizeof(*arg) + context_len + mailbox_len);
	arg->context = arg->mailbox + mailbox_len;
	ast_copy_string(arg->mailbox, mailbox, mailbox_len); /* SAFE */
	ast_copy_string(arg->context, context, context_len); /* SAFE */
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
	if (!(i = ao2_alloc(sizeof(*i) + context_len + mailbox_len, NULL))) {
		ao2_unlock(inprocess_container);
		return 0;
	}
	i->context = i->mailbox + mailbox_len;
	ast_copy_string(i->mailbox, mailbox, mailbox_len); /* SAFE */
	ast_copy_string(i->context, context, context_len); /* SAFE */
	i->count = delta;
	ao2_link(inprocess_container, i);
	ao2_unlock(inprocess_container);
	ao2_ref(i, -1);
	return 0;
}

#if !(defined(ODBC_STORAGE) || defined(IMAP_STORAGE))
static int __has_voicemail(const char *context, const char *mailbox, const char *folder, int shortcircuit);
#endif

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


/*!
 * \brief Sets default voicemail system options to a voicemail user.
 *
 * This applies select global settings to a newly created (dynamic) instance of a voicemail user.
 * - all the globalflags
 * - the saydurationminfo
 * - the callcontext
 * - the dialcontext
 * - the exitcontext
 * - vmmaxsecs, vmmaxmsg, maxdeletedmsg
 * - volume gain.
 * - emailsubject, emailbody set to NULL
 */
static void populate_defaults(struct ast_vm_user *vmu)
{
	ast_copy_flags(vmu, (&globalflags), AST_FLAGS_ALL);
	vmu->passwordlocation = passwordlocation;
	if (saydurationminfo) {
		vmu->saydurationm = saydurationminfo;
	}
	ast_copy_string(vmu->callback, callcontext, sizeof(vmu->callback));
	ast_copy_string(vmu->dialout, dialcontext, sizeof(vmu->dialout));
	ast_copy_string(vmu->exit, exitcontext, sizeof(vmu->exit));
	ast_copy_string(vmu->zonetag, zonetag, sizeof(vmu->zonetag));
	ast_copy_string(vmu->locale, locale, sizeof(vmu->locale));
	if (vmminsecs) {
		vmu->minsecs = vmminsecs;
	}
	if (vmmaxsecs) {
		vmu->maxsecs = vmmaxsecs;
	}
	if (maxmsg) {
		vmu->maxmsg = maxmsg;
	}
	if (maxdeletedmsg) {
		vmu->maxdeletedmsg = maxdeletedmsg;
	}
	vmu->volgain = volgain;
	ast_free(vmu->email);
	vmu->email = NULL;
	ast_free(vmu->emailsubject);
	vmu->emailsubject = NULL;
	ast_free(vmu->emailbody);
	vmu->emailbody = NULL;
#ifdef IMAP_STORAGE
	ast_copy_string(vmu->imapfolder, imapfolder, sizeof(vmu->imapfolder));
	ast_copy_string(vmu->imapserver, imapserver, sizeof(vmu->imapserver));
	ast_copy_string(vmu->imapport, imapport, sizeof(vmu->imapport));
	ast_copy_string(vmu->imapflags, imapflags, sizeof(vmu->imapflags));
#endif
}

/*!
 * \brief Sets a specific property value.
 * \param vmu The voicemail user object to work with.
 * \param var The name of the property to be set.
 * \param value The value to be set to the property.
 *
 * The property name must be one of the understood properties. See the source for details.
 */
static void apply_option(struct ast_vm_user *vmu, const char *var, const char *value)
{
	int x;
	if (!strcasecmp(var, "attach")) {
		ast_set2_flag(vmu, ast_true(value), VM_ATTACH);
	} else if (!strcasecmp(var, "attachfmt")) {
		ast_copy_string(vmu->attachfmt, value, sizeof(vmu->attachfmt));
	} else if (!strcasecmp(var, "attachextrecs")) {
		ast_set2_flag(vmu, ast_true(value), VM_EMAIL_EXT_RECS);
	} else if (!strcasecmp(var, "serveremail")) {
		ast_copy_string(vmu->serveremail, value, sizeof(vmu->serveremail));
	} else if (!strcasecmp(var, "fromstring")) {
		ast_copy_string(vmu->fromstring, value, sizeof(vmu->fromstring));
	} else if (!strcasecmp(var, "emailbody")) {
		ast_free(vmu->emailbody);
		vmu->emailbody = ast_strdup(substitute_escapes(value));
	} else if (!strcasecmp(var, "emailsubject")) {
		ast_free(vmu->emailsubject);
		vmu->emailsubject = ast_strdup(substitute_escapes(value));
	} else if (!strcasecmp(var, "language")) {
		ast_copy_string(vmu->language, value, sizeof(vmu->language));
	} else if (!strcasecmp(var, "tz")) {
		ast_copy_string(vmu->zonetag, value, sizeof(vmu->zonetag));
	} else if (!strcasecmp(var, "locale")) {
		ast_copy_string(vmu->locale, value, sizeof(vmu->locale));
#ifdef IMAP_STORAGE
	} else if (!strcasecmp(var, "imapuser")) {
		ast_copy_string(vmu->imapuser, value, sizeof(vmu->imapuser));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imapserver")) {
		ast_copy_string(vmu->imapserver, value, sizeof(vmu->imapserver));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imapport")) {
		ast_copy_string(vmu->imapport, value, sizeof(vmu->imapport));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imapflags")) {
		ast_copy_string(vmu->imapflags, value, sizeof(vmu->imapflags));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imappassword") || !strcasecmp(var, "imapsecret")) {
		ast_copy_string(vmu->imappassword, value, sizeof(vmu->imappassword));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imapfolder")) {
		ast_copy_string(vmu->imapfolder, value, sizeof(vmu->imapfolder));
		vmu->imapversion = imapversion;
	} else if (!strcasecmp(var, "imapvmshareid")) {
		ast_copy_string(vmu->imapvmshareid, value, sizeof(vmu->imapvmshareid));
		vmu->imapversion = imapversion;
#endif
	} else if (!strcasecmp(var, "delete") || !strcasecmp(var, "deletevoicemail")) {
		ast_set2_flag(vmu, ast_true(value), VM_DELETE);
	} else if (!strcasecmp(var, "saycid")){
		ast_set2_flag(vmu, ast_true(value), VM_SAYCID);
	} else if (!strcasecmp(var, "sendvoicemail")){
		ast_set2_flag(vmu, ast_true(value), VM_SVMAIL);
	} else if (!strcasecmp(var, "review")){
		ast_set2_flag(vmu, ast_true(value), VM_REVIEW);
	} else if (!strcasecmp(var, "tempgreetwarn")){
		ast_set2_flag(vmu, ast_true(value), VM_TEMPGREETWARN);
	} else if (!strcasecmp(var, "messagewrap")){
		ast_set2_flag(vmu, ast_true(value), VM_MESSAGEWRAP);
	} else if (!strcasecmp(var, "operator")) {
		ast_set2_flag(vmu, ast_true(value), VM_OPERATOR);
	} else if (!strcasecmp(var, "envelope")){
		ast_set2_flag(vmu, ast_true(value), VM_ENVELOPE);
	} else if (!strcasecmp(var, "moveheard")){
		ast_set2_flag(vmu, ast_true(value), VM_MOVEHEARD);
	} else if (!strcasecmp(var, "sayduration")){
		ast_set2_flag(vmu, ast_true(value), VM_SAYDURATION);
	} else if (!strcasecmp(var, "saydurationm")){
		if (sscanf(value, "%30d", &x) == 1) {
			vmu->saydurationm = x;
		} else {
			ast_log(AST_LOG_WARNING, "Invalid min duration for say duration\n");
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
	} else if (!strcasecmp(var, "minsecs")) {
		if (sscanf(value, "%30d", &x) == 1 && x >= 0) {
			vmu->minsecs = x;
		} else {
			ast_log(LOG_WARNING, "Invalid min message length of %s. Using global value %d\n", value, vmminsecs);
			vmu->minsecs = vmminsecs;
		}
	} else if (!strcasecmp(var, "maxmessage") || !strcasecmp(var, "maxsecs")) {
		vmu->maxsecs = atoi(value);
		if (vmu->maxsecs <= 0) {
			ast_log(AST_LOG_WARNING, "Invalid max message length of %s. Using global value %d\n", value, vmmaxsecs);
			vmu->maxsecs = vmmaxsecs;
		} else {
			vmu->maxsecs = atoi(value);
		}
		if (!strcasecmp(var, "maxmessage"))
			ast_log(AST_LOG_WARNING, "Option 'maxmessage' has been deprecated in favor of 'maxsecs'.  Please make that change in your voicemail config.\n");
	} else if (!strcasecmp(var, "maxmsg")) {
		vmu->maxmsg = atoi(value);
		/* Accept maxmsg=0 (Greetings only voicemail) */
		if (vmu->maxmsg < 0) {
			ast_log(AST_LOG_WARNING, "Invalid number of messages per folder maxmsg=%s. Using default value %d\n", value, MAXMSG);
			vmu->maxmsg = MAXMSG;
		} else if (vmu->maxmsg > MAXMSGLIMIT) {
			ast_log(AST_LOG_WARNING, "Maximum number of messages per folder is %d. Cannot accept value maxmsg=%s\n", MAXMSGLIMIT, value);
			vmu->maxmsg = MAXMSGLIMIT;
		}
	} else if (!strcasecmp(var, "nextaftercmd")) {
		ast_set2_flag(vmu, ast_true(value), VM_SKIPAFTERCMD);
	} else if (!strcasecmp(var, "backupdeleted")) {
		if (sscanf(value, "%30d", &x) == 1)
			vmu->maxdeletedmsg = x;
		else if (ast_true(value))
			vmu->maxdeletedmsg = MAXMSG;
		else
			vmu->maxdeletedmsg = 0;

		if (vmu->maxdeletedmsg < 0) {
			ast_log(AST_LOG_WARNING, "Invalid number of deleted messages saved per mailbox backupdeleted=%s. Using default value %d\n", value, MAXMSG);
			vmu->maxdeletedmsg = MAXMSG;
		} else if (vmu->maxdeletedmsg > MAXMSGLIMIT) {
			ast_log(AST_LOG_WARNING, "Maximum number of deleted messages saved per mailbox is %d. Cannot accept value backupdeleted=%s\n", MAXMSGLIMIT, value);
			vmu->maxdeletedmsg = MAXMSGLIMIT;
		}
	} else if (!strcasecmp(var, "volgain")) {
		sscanf(value, "%30lf", &vmu->volgain);
	} else if (!strcasecmp(var, "passwordlocation")) {
		if (!strcasecmp(value, "spooldir")) {
			vmu->passwordlocation = OPT_PWLOC_SPOOLDIR;
		} else {
			vmu->passwordlocation = OPT_PWLOC_VOICEMAILCONF;
		}
	} else if (!strcasecmp(var, "options")) {
		apply_options(vmu, value);
	}
}

static char *vm_check_password_shell(char *command, char *buf, size_t len)
{
	int fds[2], pid = 0;

	memset(buf, 0, len);

	if (pipe(fds)) {
		snprintf(buf, len, "FAILURE: Pipe failed: %s", strerror(errno));
	} else {
		/* good to go*/
		pid = ast_safe_fork(0);

		if (pid < 0) {
			/* ok maybe not */
			close(fds[0]);
			close(fds[1]);
			snprintf(buf, len, "FAILURE: Fork failed");
		} else if (pid) {
			/* parent */
			close(fds[1]);
			if (read(fds[0], buf, len) < 0) {
				ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
			}
			close(fds[0]);
		} else {
			/*  child */
			AST_DECLARE_APP_ARGS(arg,
				AST_APP_ARG(v)[20];
			);
			char *mycmd = ast_strdupa(command);

			close(fds[0]);
			dup2(fds[1], STDOUT_FILENO);
			close(fds[1]);
			ast_close_fds_above_n(STDOUT_FILENO);

			AST_NONSTANDARD_APP_ARGS(arg, mycmd, ' ');

			execv(arg.v[0], arg.v);
			printf("FAILURE: %s", strerror(errno));
			_exit(0);
		}
	}
	return buf;
}

/*!
 * \brief Check that password meets minimum required length
 * \param vmu The voicemail user to change the password for.
 * \param password The password string to check
 *
 * \return zero on ok, 1 on not ok.
 */
static int check_password(struct ast_vm_user *vmu, char *password)
{
	/* check minimum length */
	if (strlen(password) < minpassword)
		return 1;
	/* check that password does not contain '*' character */
	if (!ast_strlen_zero(password) && password[0] == '*')
		return 1;
	if (!ast_strlen_zero(ext_pass_check_cmd)) {
		char cmd[255], buf[255];

		ast_debug(1, "Verify password policies for %s\n", password);

		snprintf(cmd, sizeof(cmd), "%s %s %s %s %s", ext_pass_check_cmd, vmu->mailbox, vmu->context, vmu->password, password);
		if (vm_check_password_shell(cmd, buf, sizeof(buf))) {
			ast_debug(5, "Result: %s\n", buf);
			if (!strncasecmp(buf, "VALID", 5)) {
				ast_debug(3, "Passed password check: '%s'\n", buf);
				return 0;
			} else if (!strncasecmp(buf, "FAILURE", 7)) {
				ast_log(AST_LOG_WARNING, "Unable to execute password validation script: '%s'.\n", buf);
				return 0;
			} else {
				ast_log(AST_LOG_NOTICE, "Password doesn't match policies for user %s %s\n", vmu->mailbox, password);
				return 1;
			}
		}
	}
	return 0;
}

/*!
 * \brief Performs a change of the voicemail passowrd in the realtime engine.
 * \param vmu The voicemail user to change the password for.
 * \param password The new value to be set to the password for this user.
 *
 * This only works if there is a realtime engine configured.
 * This is called from the (top level) vm_change_password.
 *
 * \return zero on success, -1 on error.
 */
static int change_password_realtime(struct ast_vm_user *vmu, const char *password)
{
	int res = -1;
	if (!strcmp(vmu->password, password)) {
		/* No change (but an update would return 0 rows updated, so we opt out here) */
		return 0;
	}

	if (strlen(password) > 10) {
		ast_realtime_require_field("voicemail", "password", RQ_CHAR, strlen(password), SENTINEL);
	}
	if (ast_update2_realtime("voicemail", "context", vmu->context, "mailbox", vmu->mailbox, SENTINEL, "password", password, SENTINEL) > 0) {
		ast_test_suite_event_notify("PASSWORDCHANGED", "Message: realtime engine updated with new password\r\nPasswordSource: realtime");
		ast_copy_string(vmu->password, password, sizeof(vmu->password));
		res = 0;
	}
	return res;
}

/*!
 * \brief Destructively Parse options and apply.
 */
static void apply_options(struct ast_vm_user *vmu, const char *options)
{
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

/*!
 * \brief Loads the options specific to a voicemail user.
 *
 * This is called when a vm_user structure is being set up, such as from load_options.
 */
static void apply_options_full(struct ast_vm_user *retval, struct ast_variable *var)
{
	for (; var; var = var->next) {
		if (!strcasecmp(var->name, "vmsecret")) {
			ast_copy_string(retval->password, var->value, sizeof(retval->password));
		} else if (!strcasecmp(var->name, "secret") || !strcasecmp(var->name, "password")) { /* don't overwrite vmsecret if it exists */
			if (ast_strlen_zero(retval->password)) {
				if (!ast_strlen_zero(var->value) && var->value[0] == '*') {
					ast_log(LOG_WARNING, "Invalid password detected for mailbox %s.  The password"
						"\n\tmust be reset in voicemail.conf.\n", retval->mailbox);
				} else {
					ast_copy_string(retval->password, var->value, sizeof(retval->password));
				}
			}
		} else if (!strcasecmp(var->name, "uniqueid")) {
			ast_copy_string(retval->uniqueid, var->value, sizeof(retval->uniqueid));
		} else if (!strcasecmp(var->name, "pager")) {
			ast_copy_string(retval->pager, var->value, sizeof(retval->pager));
		} else if (!strcasecmp(var->name, "email")) {
			ast_free(retval->email);
			retval->email = ast_strdup(var->value);
		} else if (!strcasecmp(var->name, "fullname")) {
			ast_copy_string(retval->fullname, var->value, sizeof(retval->fullname));
		} else if (!strcasecmp(var->name, "context")) {
			ast_copy_string(retval->context, var->value, sizeof(retval->context));
		} else if (!strcasecmp(var->name, "emailsubject")) {
			ast_free(retval->emailsubject);
			retval->emailsubject = ast_strdup(substitute_escapes(var->value));
		} else if (!strcasecmp(var->name, "emailbody")) {
			ast_free(retval->emailbody);
			retval->emailbody = ast_strdup(substitute_escapes(var->value));
#ifdef IMAP_STORAGE
		} else if (!strcasecmp(var->name, "imapuser")) {
			ast_copy_string(retval->imapuser, var->value, sizeof(retval->imapuser));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(var->name, "imapserver")) {
			ast_copy_string(retval->imapserver, var->value, sizeof(retval->imapserver));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(var->name, "imapport")) {
			ast_copy_string(retval->imapport, var->value, sizeof(retval->imapport));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(var->name, "imapflags")) {
			ast_copy_string(retval->imapflags, var->value, sizeof(retval->imapflags));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(var->name, "imappassword") || !strcasecmp(var->name, "imapsecret")) {
			ast_copy_string(retval->imappassword, var->value, sizeof(retval->imappassword));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(var->name, "imapfolder")) {
			ast_copy_string(retval->imapfolder, var->value, sizeof(retval->imapfolder));
			retval->imapversion = imapversion;
		} else if (!strcasecmp(var->name, "imapvmshareid")) {
			ast_copy_string(retval->imapvmshareid, var->value, sizeof(retval->imapvmshareid));
			retval->imapversion = imapversion;
#endif
		} else
			apply_option(retval, var->name, var->value);
	}
}

/*!
 * \brief Determines if a DTMF key entered is valid.
 * \param key The character to be compared. expects a single character. Though is capable of handling a string, this is internally copies using ast_strdupa.
 *
 * Tests the character entered against the set of valid DTMF characters.
 * \return 1 if the character entered is a valid DTMF digit, 0 if the character is invalid.
 */
static int is_valid_dtmf(const char *key)
{
	int i;
	char *local_key = ast_strdupa(key);

	for (i = 0; i < strlen(key); ++i) {
		if (!strchr(VALID_DTMF, *local_key)) {
			ast_log(AST_LOG_WARNING, "Invalid DTMF key \"%c\" used in voicemail configuration file\n", *local_key);
			return 0;
		}
		local_key++;
	}
	return 1;
}

/*!
 * \brief Finds a voicemail user from the realtime engine.
 * \param ivm
 * \param context
 * \param mailbox
 *
 * This is called as a fall through case when the normal find_user() was not able to find a user. That is, the default it so look in the usual voicemail users file first.
 *
 * \return The ast_vm_user structure for the user that was found.
 */
static struct ast_vm_user *find_user_realtime(struct ast_vm_user *ivm, const char *context, const char *mailbox)
{
	struct ast_variable *var;
	struct ast_vm_user *retval;

	if ((retval = (ivm ? ivm : ast_calloc(1, sizeof(*retval))))) {
		if (ivm) {
			memset(retval, 0, sizeof(*retval));
		}
		populate_defaults(retval);
		if (!ivm) {
			ast_set_flag(retval, VM_ALLOCED);
		}
		if (mailbox) {
			ast_copy_string(retval->mailbox, mailbox, sizeof(retval->mailbox));
		}
		if (!context && ast_test_flag((&globalflags), VM_SEARCH)) {
			var = ast_load_realtime("voicemail", "mailbox", mailbox, SENTINEL);
		} else {
			var = ast_load_realtime("voicemail", "mailbox", mailbox, "context", context, SENTINEL);
		}
		if (var) {
			apply_options_full(retval, var);
			ast_variables_destroy(var);
		} else {
			if (!ivm)
				ast_free(retval);
			retval = NULL;
		}
	}
	return retval;
}

/*!
 * \brief Finds a voicemail user from the users file or the realtime engine.
 * \param ivm
 * \param context
 * \param mailbox
 *
 * \return The ast_vm_user structure for the user that was found.
 */
static struct ast_vm_user *find_user(struct ast_vm_user *ivm, const char *context, const char *mailbox)
{
	/* This function could be made to generate one from a database, too */
	struct ast_vm_user *vmu = NULL, *cur;
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
		if ((vmu = (ivm ? ivm : ast_calloc(1, sizeof(*vmu))))) {
			ast_free(vmu->email);
			ast_free(vmu->emailbody);
			ast_free(vmu->emailsubject);
			*vmu = *cur;
			vmu->email = ast_strdup(cur->email);
			vmu->emailbody = ast_strdup(cur->emailbody);
			vmu->emailsubject = ast_strdup(cur->emailsubject);
			ast_set2_flag(vmu, !ivm, VM_ALLOCED);
			AST_LIST_NEXT(vmu, list) = NULL;
		}
	}
	AST_LIST_UNLOCK(&users);
	if (!vmu) {
		vmu = find_user_realtime(ivm, context, mailbox);
	}
	if (!vmu && !ast_strlen_zero(aliasescontext)) {
		struct alias_mailbox_mapping *mapping;
		char *search_string = ast_alloca(MAX_VM_MAILBOX_LEN);

		snprintf(search_string, MAX_VM_MAILBOX_LEN, "%s%s%s",
			mailbox,
			ast_strlen_zero(context) ? "" : "@",
			S_OR(context, ""));

		mapping = ao2_find(alias_mailbox_mappings, search_string, OBJ_SEARCH_KEY);
		if (mapping) {
			char *search_mailbox = NULL;
			char *search_context = NULL;

			separate_mailbox(ast_strdupa(mapping->mailbox), &search_mailbox, &search_context);
			ao2_ref(mapping, -1);
			vmu = find_user(ivm, search_mailbox, search_context);
		}
	}

	return vmu;
}

/*!
 * \brief Resets a user password to a specified password.
 * \param context
 * \param mailbox
 * \param newpass
 *
 * This does the actual change password work, called by the vm_change_password() function.
 *
 * \return zero on success, -1 on error.
 */
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

/*!
 * \brief Check if configuration file is valid
 */
static inline int valid_config(const struct ast_config *cfg)
{
	return cfg && cfg != CONFIG_STATUS_FILEINVALID;
}

/*!
 * \brief The handler for the change password option.
 * \param vmu The voicemail user to work with.
 * \param newpassword The new password (that has been gathered from the appropriate prompting).
 * This is called when a new user logs in for the first time and the option to force them to change their password is set.
 * It is also called when the user wants to change their password from menu option '5' on the mailbox options menu.
 */
static void vm_change_password(struct ast_vm_user *vmu, const char *newpassword)
{
	struct ast_config   *cfg = NULL;
	struct ast_variable *var = NULL;
	struct ast_category *cat = NULL;
	char *category = NULL;
	const char *tmp = NULL;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };
	char secretfn[PATH_MAX] = "";
	int found = 0;

	if (!change_password_realtime(vmu, newpassword))
		return;

	/* check if we should store the secret in the spool directory next to the messages */
	switch (vmu->passwordlocation) {
	case OPT_PWLOC_SPOOLDIR:
		snprintf(secretfn, sizeof(secretfn), "%s%s/%s/secret.conf", VM_SPOOL_DIR, vmu->context, vmu->mailbox);
		if (write_password_to_file(secretfn, newpassword) == 0) {
			ast_test_suite_event_notify("PASSWORDCHANGED", "Message: secret.conf updated with new password\r\nPasswordSource: secret.conf");
			ast_verb(4, "Writing voicemail password to file %s succeeded\n", secretfn);
			reset_user_pw(vmu->context, vmu->mailbox, newpassword);
			ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
			break;
		} else {
			ast_verb(4, "Writing voicemail password to file %s failed, falling back to config file\n", secretfn);
		}
		/* Fall-through */
	case OPT_PWLOC_VOICEMAILCONF:
		if ((cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags)) && valid_config(cfg)) {
			while ((category = ast_category_browse(cfg, category))) {
				if (!strcasecmp(category, vmu->context)) {
					char *value = NULL;
					char *new = NULL;
					if (!(tmp = ast_variable_retrieve(cfg, category, vmu->mailbox))) {
						ast_log(AST_LOG_WARNING, "We could not find the mailbox.\n");
						break;
					}
					value = strstr(tmp, ",");
					if (!value) {
						new = ast_malloc(strlen(newpassword) + 1);
						sprintf(new, "%s", newpassword);
					} else {
						new = ast_malloc((strlen(value) + strlen(newpassword) + 1));
						sprintf(new, "%s%s", newpassword, value);
					}
					if (!(cat = ast_category_get(cfg, category, NULL))) {
						ast_log(AST_LOG_WARNING, "Failed to get category structure.\n");
						ast_free(new);
						break;
					}
					ast_variable_update(cat, vmu->mailbox, new, NULL, 0);
					found = 1;
					ast_free(new);
				}
			}
			/* save the results */
			if (found) {
				ast_test_suite_event_notify("PASSWORDCHANGED", "Message: voicemail.conf updated with new password\r\nPasswordSource: voicemail.conf");
				reset_user_pw(vmu->context, vmu->mailbox, newpassword);
				ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
				ast_config_text_file_save(VOICEMAIL_CONFIG, cfg, "app_voicemail");
				ast_config_destroy(cfg);
				break;
			}

			ast_config_destroy(cfg);
		}
		/* Fall-through */
	case OPT_PWLOC_USERSCONF:
		/* check users.conf and update the password stored for the mailbox */
		/* if no vmsecret entry exists create one. */
		if ((cfg = ast_config_load("users.conf", config_flags)) && valid_config(cfg)) {
			ast_debug(4, "we are looking for %s\n", vmu->mailbox);
			for (category = ast_category_browse(cfg, NULL); category; category = ast_category_browse(cfg, category)) {
				ast_debug(4, "users.conf: %s\n", category);
				if (!strcasecmp(category, vmu->mailbox)) {
					char new[strlen(newpassword) + 1];
					if (!ast_variable_retrieve(cfg, category, "vmsecret")) {
						ast_debug(3, "looks like we need to make vmsecret!\n");
						var = ast_variable_new("vmsecret", newpassword, "");
					} else {
						var = NULL;
					}

					sprintf(new, "%s", newpassword);
					if (!(cat = ast_category_get(cfg, category, NULL))) {
						ast_debug(4, "failed to get category!\n");
						ast_free(var);
						break;
					}
					if (!var) {
						ast_variable_update(cat, "vmsecret", new, NULL, 0);
					} else {
						ast_variable_append(cat, var);
					}
					found = 1;
					break;
				}
			}
			/* save the results and clean things up */
			if (found) {
				ast_test_suite_event_notify("PASSWORDCHANGED", "Message: users.conf updated with new password\r\nPasswordSource: users.conf");
				reset_user_pw(vmu->context, vmu->mailbox, newpassword);
				ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
				ast_config_text_file_save("users.conf", cfg, "app_voicemail");
			}

			ast_config_destroy(cfg);
		}
	}
}

static void vm_change_password_shell(struct ast_vm_user *vmu, char *newpassword)
{
	char buf[255];
	snprintf(buf, sizeof(buf), "%s %s %s %s", ext_pass_cmd, vmu->context, vmu->mailbox, newpassword);
	ast_debug(1, "External password: %s\n",buf);
	if (!ast_safe_system(buf)) {
		ast_test_suite_event_notify("PASSWORDCHANGED", "Message: external script updated with new password\r\nPasswordSource: external");
		ast_copy_string(vmu->password, newpassword, sizeof(vmu->password));
		/* Reset the password in memory, too */
		reset_user_pw(vmu->context, vmu->mailbox, newpassword);
	}
}

/*!
 * \brief Creates a file system path expression for a folder within the voicemail data folder and the appropriate context.
 * \param dest The variable to hold the output generated path expression. This buffer should be of size PATH_MAX.
 * \param len The length of the path string that was written out.
 * \param context
 * \param ext
 * \param folder
 *
 * The path is constructed as
 * 	VM_SPOOL_DIRcontext/ext/folder
 *
 * \return zero on success, -1 on error.
 */
static int make_dir(char *dest, int len, const char *context, const char *ext, const char *folder)
{
	return snprintf(dest, len, "%s%s/%s/%s", VM_SPOOL_DIR, context, ext, folder);
}

/*!
 * \brief Creates a file system path expression for a folder within the voicemail data folder and the appropriate context.
 * \param dest The variable to hold the output generated path expression. This buffer should be of size PATH_MAX.
 * \param len The length of the path string that was written out.
 * \param dir
 * \param num
 *
 * The path is constructed as
 * 	VM_SPOOL_DIRcontext/ext/folder
 *
 * \return zero on success, -1 on error.
 */
static int make_file(char *dest, const int len, const char *dir, const int num)
{
	return snprintf(dest, len, "%s/msg%04d", dir, num);
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
	int res;

	make_dir(dest, len, context, ext, folder);
	if ((res = ast_mkdir(dest, mode))) {
		ast_log(AST_LOG_WARNING, "ast_mkdir '%s' failed: %s\n", dest, strerror(res));
		return -1;
	}
	return 0;
}

static const char *mbox(struct ast_vm_user *vmu, int id)
{
#ifdef IMAP_STORAGE
	if (vmu && id == 0) {
		return vmu->imapfolder;
	}
#endif
	return (id >= 0 && id < ARRAY_LEN(mailbox_folders)) ? mailbox_folders[id] : "Unknown";
}

static const char *vm_index_to_foldername(int id)
{
	return mbox(NULL, id);
}


static int get_folder_by_name(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_LEN(mailbox_folders); i++) {
		if (strcasecmp(name, mailbox_folders[i]) == 0) {
			return i;
		}
	}

	return -1;
}

static void free_user(struct ast_vm_user *vmu)
{
	if (!vmu) {
		return;
	}

	ast_free(vmu->email);
	vmu->email = NULL;
	ast_free(vmu->emailbody);
	vmu->emailbody = NULL;
	ast_free(vmu->emailsubject);
	vmu->emailsubject = NULL;

	if (ast_test_flag(vmu, VM_ALLOCED)) {
		ast_free(vmu);
	}
}

static void free_user_final(struct ast_vm_user *vmu)
{
	if (!vmu) {
		return;
	}

	if (!ast_strlen_zero(vmu->mailbox)) {
		ast_delete_mwi_state_full(vmu->mailbox, vmu->context, NULL);
	}

	free_user(vmu);
}

static int vm_allocate_dh(struct vm_state *vms, struct ast_vm_user *vmu, int count_msg) {

	int arraysize = (vmu->maxmsg > count_msg ? vmu->maxmsg : count_msg);

	/* remove old allocation */
	if (vms->deleted) {
		ast_free(vms->deleted);
		vms->deleted = NULL;
	}
	if (vms->heard) {
		ast_free(vms->heard);
		vms->heard = NULL;
	}
	vms->dh_arraysize = 0;

	if (arraysize > 0) {
		if (!(vms->deleted = ast_calloc(arraysize, sizeof(int)))) {
			return -1;
		}
		if (!(vms->heard = ast_calloc(arraysize, sizeof(int)))) {
			ast_free(vms->deleted);
			vms->deleted = NULL;
			return -1;
		}
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

	/* If greetings aren't stored in IMAP, just delete the file */
	if (msgnum < 0 && !imapgreetings) {
		ast_filedelete(file, NULL);
		return;
	}

	if (!(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 1)) && !(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 0))) {
		ast_log(LOG_WARNING, "Couldn't find a vm_state for mailbox %s. Unable to set \\DELETED flag for message %d\n", vmu->mailbox, msgnum);
		return;
	}

	if (msgnum < 0) {
		imap_delete_old_greeting(file, vms);
		return;
	}

	/* find real message number based on msgnum */
	/* this may be an index into vms->msgArray based on the msgnum. */
	messageNum = vms->msgArray[msgnum];
	if (messageNum == 0) {
		ast_log(LOG_WARNING, "msgnum %d, mailbox message %lu is zero.\n", msgnum, messageNum);
		return;
	}
	ast_debug(3, "deleting msgnum %d, which is mailbox message %lu\n", msgnum, messageNum);
	/* delete message */
	snprintf (arg, sizeof(arg), "%lu", messageNum);
	ast_mutex_lock(&vms->lock);
	mail_setflag (vms->mailstream, arg, "\\DELETED");
	mail_expunge(vms->mailstream);
	ast_mutex_unlock(&vms->lock);
}

static void vm_imap_update_msg_id(char *dir, int msgnum, const char *msg_id, struct ast_vm_user *vmu, struct ast_config *msg_cfg, int folder)
{
	struct ast_channel *chan;
	char *cid;
	char *cid_name;
	char *cid_num;
	struct vm_state *vms;
	const char *duration_str;
	int duration = 0;

	/*
	 * First, get things initially set up. If any of this fails, then
	 * back out before doing anything substantial
	 */
	vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 0);
	if (!vms) {
		return;
	}

	if (open_mailbox(vms, vmu, folder)) {
		return;
	}

	chan = ast_dummy_channel_alloc();
	if (!chan) {
		close_mailbox(vms, vmu);
		return;
	}

	/*
	 * We need to make sure the new message we save has the same
	 * callerid, flag, and duration as the original message
	 */
	cid = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "callerid"));

	if (!ast_strlen_zero(cid)) {
		ast_callerid_parse(cid, &cid_name, &cid_num);
		ast_party_caller_init(ast_channel_caller(chan));
		if (!ast_strlen_zero(cid_name)) {
			ast_channel_caller(chan)->id.name.valid = 1;
			ast_channel_caller(chan)->id.name.str = ast_strdup(cid_name);
		}
		if (!ast_strlen_zero(cid_num)) {
			ast_channel_caller(chan)->id.number.valid = 1;
			ast_channel_caller(chan)->id.number.str = ast_strdup(cid_num);
		}
	}

	duration_str = ast_variable_retrieve(msg_cfg, "message", "duration");

	if (!ast_strlen_zero(duration_str)) {
		sscanf(duration_str, "%30d", &duration);
	}

	/*
	 * IMAP messages cannot be altered once delivered. So we have to delete the
	 * current message and then re-add it with the updated message ID.
	 *
	 * Furthermore, there currently is no atomic way to create a new message and to
	 * store it in an arbitrary folder. So we have to save it to the INBOX and then
	 * move to the appropriate folder.
	 */
	if (!imap_store_file(dir, vmu->mailbox, vmu->context, msgnum, chan, vmu, vmfmts,
			duration, vms, ast_variable_retrieve(msg_cfg, "message", "flag"), msg_id)) {
		if (folder != NEW_FOLDER) {
			save_to_folder(vmu, vms, msgnum, folder, NULL, 1);
		}
		vm_imap_delete(dir, msgnum, vmu);
	}
	close_mailbox(vms, vmu);
	ast_channel_unref(chan);
}

static int imap_retrieve_greeting(const char *dir, const int msgnum, struct ast_vm_user *vmu)
{
	struct vm_state *vms_p;
	char *file, *filename;
	char dest[PATH_MAX];
	int i;
	BODY *body;
	int ret = 0;
	int curr_mbox;

	/* This function is only used for retrieval of IMAP greetings
	 * regular messages are not retrieved this way, nor are greetings
	 * if they are stored locally*/
	if (msgnum > -1 || !imapgreetings) {
		return 0;
	} else {
		file = strrchr(ast_strdupa(dir), '/');
		if (file)
			*file++ = '\0';
		else {
			ast_debug(1, "Failed to procure file name from directory passed.\n");
			return -1;
		}
	}

	/* check if someone is accessing this box right now... */
	if (!(vms_p = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 1)) &&
		!(vms_p = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 0))) {
		/* Unlike when retrieving a message, it is reasonable not to be able to find a
		* vm_state for a mailbox when trying to retrieve a greeting. Just create one,
		* that's all we need to do.
		*/
		if (!(vms_p = create_vm_state_from_user(vmu))) {
			ast_log(LOG_NOTICE, "Unable to create vm_state object!\n");
			return -1;
		}
	}

	/* Greetings will never have a prepended message */
	*vms_p->introfn = '\0';

	ast_mutex_lock(&vms_p->lock);

	/* get the current mailbox so that we can point the mailstream back to it later */
	curr_mbox = get_folder_by_name(vms_p->curbox);

	if (init_mailstream(vms_p, GREETINGS_FOLDER) || !vms_p->mailstream) {
		ast_log(AST_LOG_ERROR, "IMAP mailstream is NULL or can't init_mailstream\n");
		ast_mutex_unlock(&vms_p->lock);
		return -1;
	}

	/*XXX Yuck, this could probably be done a lot better */
	for (i = 0; i < vms_p->mailstream->nmsgs; i++) {
		mail_fetchstructure(vms_p->mailstream, i + 1, &body);
		/* We have the body, now we extract the file name of the first attachment. */
		if (body->nested.part && body->nested.part->next && body->nested.part->next->body.parameter->value) {
			char *attachment = body->nested.part->next->body.parameter->value;
			char copy[strlen(attachment) + 1];

			strcpy(copy, attachment); /* safe */
			attachment = copy;

			filename = strsep(&attachment, ".");
			if (!strcmp(filename, file)) {
				ast_copy_string(vms_p->fn, dir, sizeof(vms_p->fn));
				vms_p->msgArray[vms_p->curmsg] = i + 1;
				create_dirpath(dest, sizeof(dest), vmu->context, vms_p->username, "");
				save_body(body, vms_p, "2", attachment, 0);
				ret = 0;
				break;
			}
		} else {
			ast_log(AST_LOG_ERROR, "There is no file attached to this IMAP message.\n");
			ret = -1;
			break;
		}
	}

	if (curr_mbox != -1) {
		/* restore previous mbox stream */
		if (init_mailstream(vms_p, curr_mbox) || !vms_p->mailstream) {
			ast_log(AST_LOG_ERROR, "IMAP mailstream is NULL or can't init_mailstream\n");
			ret = -1;
		}
	}
	ast_mutex_unlock(&vms_p->lock);
	return ret;
}

static int imap_retrieve_file(const char *dir, const int msgnum, const char *mailbox, const char *context)
{
	BODY *body;
	char *header_content;
	char *attachedfilefmt;
	char buf[80];
	struct vm_state *vms;
	char text_file[PATH_MAX];
	FILE *text_file_ptr;
	int res = 0;
	struct ast_vm_user *vmu;
	int curr_mbox;

	if (!(vmu = find_user(NULL, context, mailbox))) {
		ast_log(LOG_WARNING, "Couldn't find user with mailbox %s@%s\n", mailbox, context);
		return -1;
	}

	if (msgnum < 0) {
		if (imapgreetings) {
			res = imap_retrieve_greeting(dir, msgnum, vmu);
			goto exit;
		} else {
			res = 0;
			goto exit;
		}
	}

	/* Before anything can happen, we need a vm_state so that we can
	 * actually access the imap server through the vms->mailstream
	 */
	if (!(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 1)) && !(vms = get_vm_state_by_mailbox(vmu->mailbox, vmu->context, 0))) {
		/* This should not happen. If it does, then I guess we'd
		 * need to create the vm_state, extract which mailbox to
		 * open, and then set up the msgArray so that the correct
		 * IMAP message could be accessed. If I have seen correctly
		 * though, the vms should be obtainable from the vmstates list
		 * and should have its msgArray properly set up.
		 */
		ast_log(LOG_ERROR, "Couldn't find a vm_state for mailbox %s!!! Oh no!\n", vmu->mailbox);
		res = -1;
		goto exit;
	}

	/* Ensure we have the correct mailbox open and have a valid mailstream for it */
	curr_mbox = get_folder_by_name(vms->curbox);
	if (curr_mbox < 0) {
		ast_debug(3, "Mailbox folder curbox not set, defaulting to Inbox\n");
		curr_mbox = 0;
	}
	init_mailstream(vms, curr_mbox);
	if (!vms->mailstream) {
		ast_log(AST_LOG_ERROR, "IMAP mailstream for %s is NULL\n", vmu->mailbox);
		res = -1;
		goto exit;
	}

	make_file(vms->fn, sizeof(vms->fn), dir, msgnum);
	snprintf(vms->introfn, sizeof(vms->introfn), "%sintro", vms->fn);

	/* Don't try to retrieve a message from IMAP if it already is on the file system */
	if (ast_fileexists(vms->fn, NULL, NULL) > 0) {
		res = 0;
		goto exit;
	}

	ast_debug(3, "Before mail_fetchheaders, curmsg is: %d, imap messages is %lu\n", msgnum, vms->msgArray[msgnum]);
	if (vms->msgArray[msgnum] == 0) {
		ast_log(LOG_WARNING, "Trying to access unknown message\n");
		res = -1;
		goto exit;
	}

	/* This will only work for new messages... */
	ast_mutex_lock(&vms->lock);
	header_content = mail_fetchheader (vms->mailstream, vms->msgArray[msgnum]);
	ast_mutex_unlock(&vms->lock);
	/* empty string means no valid header */
	if (ast_strlen_zero(header_content)) {
		ast_log(LOG_ERROR, "Could not fetch header for message number %ld\n", vms->msgArray[msgnum]);
		res = -1;
		goto exit;
	}

	ast_mutex_lock(&vms->lock);
	mail_fetchstructure(vms->mailstream, vms->msgArray[msgnum], &body);
	ast_mutex_unlock(&vms->lock);

	/* We have the body, now we extract the file name of the first attachment. */
	if (body->nested.part && body->nested.part->next && body->nested.part->next->body.parameter->value) {
		attachedfilefmt = ast_strdupa(body->nested.part->next->body.parameter->value);
	} else {
		ast_log(LOG_ERROR, "There is no file attached to this IMAP message.\n");
		res = -1;
		goto exit;
	}

	/* Find the format of the attached file */

	strsep(&attachedfilefmt, ".");
	if (!attachedfilefmt) {
		ast_log(LOG_ERROR, "File format could not be obtained from IMAP message attachment\n");
		res = -1;
		goto exit;
	}

	save_body(body, vms, "2", attachedfilefmt, 0);
	if (save_body(body, vms, "3", attachedfilefmt, 1)) {
		*vms->introfn = '\0';
	}

	/* Get info from headers!! */
	snprintf(text_file, sizeof(text_file), "%s.%s", vms->fn, "txt");

	if (!(text_file_ptr = fopen(text_file, "w"))) {
		ast_log(LOG_ERROR, "Unable to open/create file %s: %s\n", text_file, strerror(errno));
		goto exit;
	}

	fprintf(text_file_ptr, "%s\n", "[message]");

	if (get_header_by_tag(header_content, "X-Asterisk-VM-Caller-ID-Name:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "callerid=\"%s\" ", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Caller-ID-Num:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "<%s>\n", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Context:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "context=%s\n", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Orig-time:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "origtime=%s\n", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Duration:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "duration=%s\n", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Category:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "category=%s\n", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Flag:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "flag=%s\n", S_OR(buf, ""));
	}
	if (get_header_by_tag(header_content, "X-Asterisk-VM-Message-ID:", buf, sizeof(buf))) {
		fprintf(text_file_ptr, "msg_id=%s\n", S_OR(buf, ""));
	}
	fclose(text_file_ptr);

exit:
	free_user(vmu);
	return res;
}

static int folder_int(const char *folder)
{
	/*assume a NULL folder means INBOX*/
	if (!folder) {
		return 0;
	}
	if (!strcasecmp(folder, imapfolder)) {
		return 0;
	} else if (!strcasecmp(folder, "Old")) {
		return 1;
	} else if (!strcasecmp(folder, "Work")) {
		return 2;
	} else if (!strcasecmp(folder, "Family")) {
		return 3;
	} else if (!strcasecmp(folder, "Friends")) {
		return 4;
	} else if (!strcasecmp(folder, "Cust1")) {
		return 5;
	} else if (!strcasecmp(folder, "Cust2")) {
		return 6;
	} else if (!strcasecmp(folder, "Cust3")) {
		return 7;
	} else if (!strcasecmp(folder, "Cust4")) {
		return 8;
	} else if (!strcasecmp(folder, "Cust5")) {
		return 9;
	} else if (!strcasecmp(folder, "Urgent")) {
		return 11;
	} else { /*assume they meant INBOX if folder is not found otherwise*/
		return 0;
	}
}

static int __messagecount(const char *context, const char *mailbox, const char *folder)
{
	SEARCHPGM *pgm;
	SEARCHHEADER *hdr;

	struct ast_vm_user *vmu, vmus;
	struct vm_state *vms_p;
	int ret = 0;
	int fold = folder_int(folder);
	int urgent = 0;

	/* If URGENT, then look at INBOX */
	if (fold == 11) {
		fold = NEW_FOLDER;
		urgent = 1;
	}

	if (ast_strlen_zero(mailbox))
		return 0;

	/* We have to get the user before we can open the stream! */
	memset(&vmus, 0, sizeof(vmus));
	vmu = find_user(&vmus, context, mailbox);
	if (!vmu) {
		ast_log(AST_LOG_WARNING, "Couldn't find mailbox %s in context %s\n", mailbox, context);
		free_user(vmu);
		return -1;
	} else {
		/* No IMAP account available */
		if (vmu->imapuser[0] == '\0') {
			ast_log(AST_LOG_WARNING, "IMAP user not set for mailbox %s\n", vmu->mailbox);
			free_user(vmu);
			return -1;
		}
	}

	/* No IMAP account available */
	if (vmu->imapuser[0] == '\0') {
		ast_log(AST_LOG_WARNING, "IMAP user not set for mailbox %s\n", vmu->mailbox);
		free_user(vmu);
		return -1;
	}

	/* check if someone is accessing this box right now... */
	vms_p = get_vm_state_by_imapuser(vmu->imapuser, 1);
	if (!vms_p) {
		vms_p = get_vm_state_by_mailbox(mailbox, context, 1);
	}
	if (vms_p) {
		ast_debug(3, "Returning before search - user is logged in\n");
		if (fold == 0) { /* INBOX */
			free_user(vmu);
			return urgent ? vms_p->urgentmessages : vms_p->newmessages;
		}
		if (fold == 1) { /* Old messages */
			free_user(vmu);
			return vms_p->oldmessages;
		}
	}

	/* add one if not there... */
	vms_p = get_vm_state_by_imapuser(vmu->imapuser, 0);
	if (!vms_p) {
		vms_p = get_vm_state_by_mailbox(mailbox, context, 0);
	}

	if (!vms_p) {
		vms_p = create_vm_state_from_user(vmu);
	}
	ret = init_mailstream(vms_p, fold);
	if (!vms_p->mailstream) {
		ast_log(AST_LOG_ERROR, "Houston we have a problem - IMAP mailstream is NULL\n");
		free_user(vmu);
		return -1;
	}
	if (ret == 0) {
		ast_mutex_lock(&vms_p->lock);
		pgm = mail_newsearchpgm ();
		hdr = mail_newsearchheader ("X-Asterisk-VM-Extension", (char *)(!ast_strlen_zero(vmu->imapvmshareid) ? vmu->imapvmshareid : mailbox));
		hdr->next = mail_newsearchheader("X-Asterisk-VM-Context", (char *) S_OR(context, "default"));
		pgm->header = hdr;
		if (fold != OLD_FOLDER) {
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
		/* look for urgent messages */
		if (fold == NEW_FOLDER) {
			if (urgent) {
				pgm->flagged = 1;
				pgm->unflagged = 0;
			} else {
				pgm->flagged = 0;
				pgm->unflagged = 1;
			}
		}
		pgm->undeleted = 1;
		pgm->deleted = 0;

		vms_p->vmArrayIndex = 0;
		mail_search_full (vms_p->mailstream, NULL, pgm, NIL);
		if (fold == 0 && urgent == 0)
			vms_p->newmessages = vms_p->vmArrayIndex;
		if (fold == 1)
			vms_p->oldmessages = vms_p->vmArrayIndex;
		if (fold == 0 && urgent == 1)
			vms_p->urgentmessages = vms_p->vmArrayIndex;
		/*Freeing the searchpgm also frees the searchhdr*/
		mail_free_searchpgm(&pgm);
		ast_mutex_unlock(&vms_p->lock);
		free_user(vmu);
		vms_p->updated = 0;
		return vms_p->vmArrayIndex;
	} else {
		ast_mutex_lock(&vms_p->lock);
		mail_ping(vms_p->mailstream);
		ast_mutex_unlock(&vms_p->lock);
	}
	free_user(vmu);
	return 0;
}

static int imap_check_limits(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu, int msgnum)
{
	/* Check if mailbox is full */
	check_quota(vms, vmu->imapfolder);
	if (vms->quota_limit && vms->quota_usage >= vms->quota_limit) {
		ast_debug(1, "*** QUOTA EXCEEDED!! %u >= %u\n", vms->quota_usage, vms->quota_limit);
		if (chan) {
			ast_play_and_wait(chan, "vm-mailboxfull");
		}
		return -1;
	}

	/* Check if we have exceeded maxmsg */
	ast_debug(3, "Checking message number quota: mailbox has %d messages, maximum is set to %d, current messages %d\n", msgnum, vmu->maxmsg, inprocess_count(vmu->mailbox, vmu->context, 0));
	if (msgnum >= vmu->maxmsg - inprocess_count(vmu->mailbox, vmu->context, +1)) {
		ast_log(LOG_WARNING, "Unable to leave message since we will exceed the maximum number of messages allowed (%u >= %u)\n", msgnum, vmu->maxmsg);
		if (chan) {
			ast_play_and_wait(chan, "vm-mailboxfull");
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		}
		return -1;
	}

	return 0;
}

/*!
 * \brief Gets the number of messages that exist in a mailbox folder.
 * \param mailbox_id
 * \param folder
 *
 * This method is used when IMAP backend is used.
 * \return The number of messages in this mailbox folder (zero or more).
 */
static int messagecount(const char *mailbox_id, const char *folder)
{
	char *context;
	char *mailbox;
	int count;

	if (ast_strlen_zero(mailbox_id)
		|| separate_mailbox(ast_strdupa(mailbox_id), &mailbox, &context)) {
		return 0;
	}

	if (ast_strlen_zero(folder) || !strcmp(folder, "INBOX")) {
		count = __messagecount(context, mailbox, "INBOX") + __messagecount(context, mailbox, "Urgent");
	} else {
		count = __messagecount(context, mailbox, folder);
	}
	return count < 0 ? 0 : count;
}

static int imap_store_file(const char *dir, const char *mailboxuser, const char *mailboxcontext, int msgnum, struct ast_channel *chan, struct ast_vm_user *vmu, char *fmt, int duration, struct vm_state *vms, const char *flag, const char *msg_id)
{
	char *myserveremail = serveremail;
	char fn[PATH_MAX];
	char introfn[PATH_MAX];
	char mailbox[256];
	char *stringp;
	FILE *p = NULL;
	char tmp[80] = "/tmp/astmail-XXXXXX";
	long len;
	void *buf;
	int tempcopy = 0;
	STRING str;
	int ret; /* for better error checking */
	char *imap_flags = NIL;
	int msgcount;
	int box = NEW_FOLDER;

	snprintf(mailbox, sizeof(mailbox), "%s@%s", vmu->mailbox, vmu->context);
	msgcount = messagecount(mailbox, "INBOX") + messagecount(mailbox, "Old");

	/* Back out early if this is a greeting and we don't want to store greetings in IMAP */
	if (msgnum < 0) {
		if(!imapgreetings) {
			return 0;
		} else {
			box = GREETINGS_FOLDER;
		}
	}

	if (imap_check_limits(chan, vms, vmu, msgcount)) {
		return -1;
	}

	/* Set urgent flag for IMAP message */
	if (!ast_strlen_zero(flag) && !strcmp(flag, "Urgent")) {
		ast_debug(3, "Setting message flag \\\\FLAGGED.\n");
		imap_flags = "\\FLAGGED";
	}

	/* Attach only the first format */
	fmt = ast_strdupa(fmt);
	stringp = fmt;
	strsep(&stringp, "|");

	if (!ast_strlen_zero(vmu->serveremail))
		myserveremail = vmu->serveremail;

	if (msgnum > -1)
		make_file(fn, sizeof(fn), dir, msgnum);
	else
		ast_copy_string (fn, dir, sizeof(fn));

	snprintf(introfn, sizeof(introfn), "%sintro", fn);
	if (ast_fileexists(introfn, NULL, NULL) <= 0) {
		*introfn = '\0';
	}

	if (ast_strlen_zero(vmu->email)) {
		/* We need the vmu->email to be set when we call make_email_file, but
		 * if we keep it set, a duplicate e-mail will be created. So at the end
		 * of this function, we will revert back to an empty string if tempcopy
		 * is 1.
		 */
		vmu->email = ast_strdup(vmu->imapuser);
		tempcopy = 1;
	}

	if (!strcmp(fmt, "wav49"))
		fmt = "WAV";
	ast_debug(3, "Storing file '%s', format '%s'\n", fn, fmt);

	/* Make a temporary file instead of piping directly to sendmail, in case the mail
	   command hangs. */
	if (!(p = ast_file_mkftemp(tmp, VOICEMAIL_FILE_MODE & ~my_umask))) {
		ast_log(AST_LOG_WARNING, "Unable to store '%s' (can't create temporary file)\n", fn);
		if (tempcopy) {
			ast_free(vmu->email);
			vmu->email = NULL;
		}
		return -1;
	}

	if (msgnum < 0 && imapgreetings) {
		if ((ret = init_mailstream(vms, GREETINGS_FOLDER))) {
			ast_log(AST_LOG_WARNING, "Unable to open mailstream.\n");
			return -1;
		}
		imap_delete_old_greeting(fn, vms);
	}

	make_email_file(p, myserveremail, vmu, msgnum, vmu->context, vmu->mailbox, "INBOX",
		chan ? S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL) : NULL,
		chan ? S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL) : NULL,
		fn, introfn, fmt, duration, 1, chan, NULL, 1, flag, msg_id);
	/* read mail file to memory */
	len = ftell(p);
	rewind(p);
	if (!(buf = ast_malloc(len + 1))) {
		ast_log(AST_LOG_ERROR, "Can't allocate %ld bytes to read message\n", len + 1);
		fclose(p);
		if (tempcopy)
			*(vmu->email) = '\0';
		return -1;
	}
	if (fread(buf, 1, len, p) != len) {
		if (ferror(p)) {
			ast_log(LOG_ERROR, "Error while reading mail file: %s\n", tmp);
			return -1;
		}
	}
	((char *) buf)[len] = '\0';
	INIT(&str, mail_string, buf, len);
	ret = init_mailstream(vms, box);
	if (ret == 0) {
		imap_mailbox_name(mailbox, sizeof(mailbox), vms, box, 1);
		ast_mutex_lock(&vms->lock);
		if(!mail_append_full(vms->mailstream, mailbox, imap_flags, NIL, &str))
			ast_log(LOG_ERROR, "Error while sending the message to %s\n", mailbox);
		ast_mutex_unlock(&vms->lock);
		fclose(p);
		unlink(tmp);
		ast_free(buf);
	} else {
		ast_log(LOG_ERROR, "Could not initialize mailstream for %s\n", mailbox);
		fclose(p);
		unlink(tmp);
		ast_free(buf);
		return -1;
	}
	ast_debug(3, "%s stored\n", fn);

	if (tempcopy)
		*(vmu->email) = '\0';
	inprocess_count(vmu->mailbox, vmu->context, -1);
	return 0;

}

/*!
 * \brief Gets the number of messages that exist in the inbox folder.
 * \param mailbox_context
 * \param newmsgs The variable that is updated with the count of new messages within this inbox.
 * \param oldmsgs The variable that is updated with the count of old messages within this inbox.
 * \param urgentmsgs The variable that is updated with the count of urgent messages within this inbox.
 *
 * This method is used when IMAP backend is used.
 * Simultaneously determines the count of new,old, and urgent messages. The total messages would then be the sum of these three.
 *
 * \return zero on success, -1 on error.
 */

static int inboxcount2(const char *mailbox_context, int *urgentmsgs, int *newmsgs, int *oldmsgs)
{
	char tmp[PATH_MAX] = "";
	char *mailboxnc;
	char *context;
	char *mb;
	char *cur;
	int count = 0;
	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	if (urgentmsgs)
		*urgentmsgs = 0;

	ast_debug(3, "Mailbox is set to %s\n", mailbox_context);
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox_context))
		return 0;

	ast_copy_string(tmp, mailbox_context, sizeof(tmp));
	context = strchr(tmp, '@');
	if (strchr(mailbox_context, ',')) {
		int tmpnew, tmpold, tmpurgent;
		ast_copy_string(tmp, mailbox_context, sizeof(tmp));
		mb = tmp;
		while ((cur = strsep(&mb, ", "))) {
			if (!ast_strlen_zero(cur)) {
				if (inboxcount2(cur, urgentmsgs ? &tmpurgent : NULL, newmsgs ? &tmpnew : NULL, oldmsgs ? &tmpold : NULL))
					return -1;
				else {
					if (newmsgs)
						*newmsgs += tmpnew;
					if (oldmsgs)
						*oldmsgs += tmpold;
					if (urgentmsgs)
						*urgentmsgs += tmpurgent;
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
		mailboxnc = (char *) mailbox_context;
	}

	if (newmsgs) {
		struct ast_vm_user *vmu = find_user(NULL, context, mailboxnc);
		if (!vmu) {
			ast_log(AST_LOG_ERROR, "Couldn't find mailbox %s in context %s\n", mailboxnc, context);
			return -1;
		}
		if ((count = __messagecount(context, mailboxnc, vmu->imapfolder)) < 0) {
			free_user(vmu);
			return -1;
		}
		*newmsgs = count;
		free_user(vmu);
	}
	if (oldmsgs) {
		if ((count = __messagecount(context, mailboxnc, "Old")) < 0) {
			return -1;
		}
		*oldmsgs = count;
	}
	if (urgentmsgs) {
		if ((count = __messagecount(context, mailboxnc, "Urgent")) < 0) {
			return -1;
		}
		*urgentmsgs = count;
	}
	return 0;
}

/*!
 * \brief Determines if the given folder has messages.
 * \param mailbox The \@ delimited string for user\@context. If no context is found, uses 'default' for the context.
 * \param folder the folder to look in
 *
 * This function is used when the mailbox is stored in an IMAP back end.
 * This invokes the messagecount(). Here we are interested in the presence of messages (> 0) only, not the actual count.
 * \return 1 if the folder has one or more messages. zero otherwise.
 */

static int has_voicemail(const char *mailbox, const char *folder)
{
	char tmp[256], *tmp2, *box, *context;
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	tmp2 = tmp;
	if (strchr(tmp2, ',') || strchr(tmp2, '&')) {
		while ((box = strsep(&tmp2, ",&"))) {
			if (!ast_strlen_zero(box)) {
				if (has_voicemail(box, folder)) {
					return 1;
				}
			}
		}
	}
	if ((context = strchr(tmp, '@'))) {
		*context++ = '\0';
	} else {
		context = "default";
	}
	return __messagecount(context, tmp, folder) > 0 ? 1 : 0;
}

/*!
 * \brief Copies a message from one mailbox to another.
 * \param chan
 * \param vmu
 * \param imbox
 * \param msgnum
 * \param duration
 * \param recip
 * \param fmt
 * \param dir
 * \param flag, dest_folder
 *
 * This works with IMAP storage based mailboxes.
 *
 * \return zero on success, -1 on error.
 */
static int copy_message(struct ast_channel *chan, struct ast_vm_user *vmu, int imbox, int msgnum, long duration, struct ast_vm_user *recip, char *fmt, char *dir, char *flag, const char *dest_folder)
{
	struct vm_state *sendvms = NULL;
	char messagestring[10]; /*I guess this could be a problem if someone has more than 999999999 messages...*/
	if (msgnum >= recip->maxmsg) {
		ast_log(LOG_WARNING, "Unable to copy mail, mailbox %s is full\n", recip->mailbox);
		return -1;
	}
	if (!(sendvms = get_vm_state_by_imapuser(vmu->imapuser, 0))) {
		ast_log(LOG_ERROR, "Couldn't get vm_state for originator's mailbox!!\n");
		return -1;
	}
	if (!get_vm_state_by_imapuser(recip->imapuser, 0)) {
		ast_log(LOG_ERROR, "Couldn't get vm_state for destination mailbox!\n");
		return -1;
	}
	snprintf(messagestring, sizeof(messagestring), "%ld", sendvms->msgArray[msgnum]);
	ast_mutex_lock(&sendvms->lock);
	if ((mail_copy(sendvms->mailstream, messagestring, (char *) mbox(vmu, imbox)) == T)) {
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

	if (box == OLD_FOLDER) {
		ast_copy_string(vms->curbox, mbox(NULL, NEW_FOLDER), sizeof(vms->curbox));
	} else {
		ast_copy_string(vms->curbox, mbox(NULL, box), sizeof(vms->curbox));
	}

	if (box == NEW_FOLDER) {
		ast_copy_string(vms->vmbox, "vm-INBOX", sizeof(vms->vmbox));
	} else {
		snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", mbox(NULL, box));
	}

	/* Build up server information */
	ast_build_string(&t, &left, "{%s:%s/imap", S_OR(vms->imapserver, imapserver), S_OR(vms->imapport, imapport));

	/* Add authentication user if present */
	if (!ast_strlen_zero(authuser))
		ast_build_string(&t, &left, "/authuser=%s", authuser);

	/* Add flags if present */
	if (!ast_strlen_zero(imapflags) || !(ast_strlen_zero(vms->imapflags))) {
		ast_build_string(&t, &left, "/%s", S_OR(vms->imapflags, imapflags));
	}

	/* End with username */
#if 1
	ast_build_string(&t, &left, "/user=%s}", vms->imapuser);
#else
	ast_build_string(&t, &left, "/user=%s/novalidate-cert}", vms->imapuser);
#endif
	if (box == NEW_FOLDER || box == OLD_FOLDER)
		snprintf(spec, len, "%s%s", tmp, use_folder? vms->imapfolder: "INBOX");
	else if (box == GREETINGS_FOLDER)
		snprintf(spec, len, "%s%s", tmp, greetingfolder);
	else {	/* Other folders such as Friends, Family, etc... */
		if (!ast_strlen_zero(imapparentfolder)) {
			/* imapparentfolder would typically be set to INBOX */
			snprintf(spec, len, "%s%s%c%s", tmp, imapparentfolder, delimiter, mbox(NULL, box));
		} else {
			snprintf(spec, len, "%s%s", tmp, mbox(NULL, box));
		}
	}
}

static int init_mailstream(struct vm_state *vms, int box)
{
	MAILSTREAM *stream = NIL;
	long debug;
	char tmp[256];

	if (!vms) {
		ast_log(LOG_ERROR, "vm_state is NULL!\n");
		return -1;
	}
	ast_debug(3, "vm_state user is:%s\n", vms->imapuser);
	if (vms->mailstream == NIL || !vms->mailstream) {
		ast_debug(1, "mailstream not set.\n");
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
		ast_mutex_lock(&mail_open_lock);
		stream = mail_open (stream, tmp, debug ? OP_DEBUG : NIL);
		ast_mutex_unlock(&mail_open_lock);
		ast_mutex_unlock(&vms->lock);
		if (stream == NIL) {
			ast_log(LOG_ERROR, "Can't connect to imap server %s\n", tmp);
			return -1;
		}
		get_mailbox_delimiter(vms, stream);
		/* update delimiter in imapfolder */
		for (cp = vms->imapfolder; *cp; cp++)
			if (*cp == '/')
				*cp = delimiter;
	}
	/* Now connect to the target folder */
	imap_mailbox_name(tmp, sizeof(tmp), vms, box, 1);
	ast_debug(3, "Before mail_open, server: %s, box:%d\n", tmp, box);
	ast_mutex_lock(&vms->lock);
	ast_mutex_lock(&mail_open_lock);
	vms->mailstream = mail_open (stream, tmp, debug ? OP_DEBUG : NIL);
	/* Create the folder if it doesn't exist */
	if (vms->mailstream && !mail_status(vms->mailstream, tmp, SA_UIDNEXT)) {
		mail_create(vms->mailstream, tmp);
	}
	ast_mutex_unlock(&mail_open_lock);
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
	int urgent = 0;

	/* If Urgent, then look at INBOX */
	if (box == 11) {
		box = NEW_FOLDER;
		urgent = 1;
	}

	ast_copy_string(vms->imapuser, vmu->imapuser, sizeof(vms->imapuser));
	ast_copy_string(vms->imapfolder, vmu->imapfolder, sizeof(vms->imapfolder));
	ast_copy_string(vms->imapserver, vmu->imapserver, sizeof(vms->imapserver));
	ast_copy_string(vms->imapport, vmu->imapport, sizeof(vms->imapport));
	ast_copy_string(vms->imapflags, vmu->imapflags, sizeof(vms->imapflags));
	vms->imapversion = vmu->imapversion;
	ast_debug(3, "Before init_mailstream, user is %s\n", vmu->imapuser);

	if (init_mailstream(vms, box) || !vms->mailstream) {
		ast_log(AST_LOG_ERROR, "Could not initialize mailstream\n");
		return -1;
	}

	create_dirpath(vms->curdir, sizeof(vms->curdir), vmu->context, vms->username, vms->curbox);

	/* Check Quota */
	if  (box == 0)  {
		ast_debug(3, "Mailbox name set to: %s, about to check quotas\n", mbox(vmu, box));
		check_quota(vms, (char *) mbox(vmu, box));
	}

	ast_mutex_lock(&vms->lock);
	pgm = mail_newsearchpgm();

	/* Check IMAP folder for Asterisk messages only... */
	hdr = mail_newsearchheader("X-Asterisk-VM-Extension", (!ast_strlen_zero(vmu->imapvmshareid) ? vmu->imapvmshareid : vmu->mailbox));
	hdr->next = mail_newsearchheader("X-Asterisk-VM-Context", vmu->context);
	pgm->header = hdr;
	pgm->deleted = 0;
	pgm->undeleted = 1;

	/* if box = NEW_FOLDER, check for new, if box = OLD_FOLDER, check for read */
	if (box == NEW_FOLDER && urgent == 1) {
		pgm->unseen = 1;
		pgm->seen = 0;
		pgm->flagged = 1;
		pgm->unflagged = 0;
	} else if (box == NEW_FOLDER && urgent == 0) {
		pgm->unseen = 1;
		pgm->seen = 0;
		pgm->flagged = 0;
		pgm->unflagged = 1;
	} else if (box == OLD_FOLDER) {
		pgm->seen = 1;
		pgm->unseen = 0;
	}

	ast_debug(3, "Before mail_search_full, user is %s\n", vmu->imapuser);

	vms->vmArrayIndex = 0;
	mail_search_full (vms->mailstream, NULL, pgm, NIL);
	vms->lastmsg = vms->vmArrayIndex - 1;
	mail_free_searchpgm(&pgm);
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

	ast_mutex_unlock(&vms->lock);
	return 0;
}

static void write_file(char *filename, char *buffer, unsigned long len)
{
	FILE *output;

	if (!filename || !buffer) {
		return;
	}

	if (!(output = fopen(filename, "w"))) {
		ast_log(LOG_ERROR, "Unable to open/create file %s: %s\n", filename, strerror(errno));
		return;
	}

	if (fwrite(buffer, len, 1, output) != 1) {
		if (ferror(output)) {
			ast_log(LOG_ERROR, "Short write while writing e-mail body: %s.\n", strerror(errno));
		}
	}
	fclose (output);
}

static void update_messages_by_imapuser(const char *user, unsigned long number)
{
	struct vm_state *vms = get_vm_state_by_imapuser(user, 1);

	if (!vms && !(vms = get_vm_state_by_imapuser(user, 0))) {
		return;
	}

	ast_debug(3, "saving mailbox message number %lu as message %d. Interactive set to %d\n", number, vms->vmArrayIndex, vms->interactive);

	/* Ensure we have room for the next message. */
	if (vms->vmArrayIndex >= vms->msg_array_max) {
		long *new_mem = ast_realloc(vms->msgArray, 2 * vms->msg_array_max * sizeof(long));
		if (!new_mem) {
			return;
		}
		vms->msgArray = new_mem;
		vms->msg_array_max *= 2;
	}

	vms->msgArray[vms->vmArrayIndex++] = number;
}

void mm_searched(MAILSTREAM *stream, unsigned long number)
{
	char *mailbox = stream->mailbox, buf[1024] = "", *user;

	if (!(user = get_user_by_mailbox(mailbox, buf, sizeof(buf))))
		return;

	update_messages_by_imapuser(user, number);
}

static struct ast_vm_user *find_user_realtime_imapuser(const char *imapuser)
{
	struct ast_variable *var;
	struct ast_vm_user *vmu;

	vmu = ast_calloc(1, sizeof *vmu);
	if (!vmu)
		return NULL;

	populate_defaults(vmu);
	ast_set_flag(vmu, VM_ALLOCED);

	var = ast_load_realtime("voicemail", "imapuser", imapuser, NULL);
	if (var) {
		apply_options_full(vmu, var);
		ast_variables_destroy(var);
		return vmu;
	} else {
		ast_free(vmu);
		return NULL;
	}
}

/* Interfaces to C-client */

void mm_exists(MAILSTREAM * stream, unsigned long number)
{
	/* mail_ping will callback here if new mail! */
	ast_debug(4, "Entering EXISTS callback for message %ld\n", number);
	if (number == 0) return;
	set_update(stream);
}


void mm_expunged(MAILSTREAM * stream, unsigned long number)
{
	/* mail_ping will callback here if expunged mail! */
	ast_debug(4, "Entering EXPUNGE callback for message %ld\n", number);
	if (number == 0) return;
	set_update(stream);
}


void mm_flags(MAILSTREAM * stream, unsigned long number)
{
	/* mail_ping will callback here if read mail! */
	ast_debug(4, "Entering FLAGS callback for message %ld\n", number);
	if (number == 0) return;
	set_update(stream);
}


void mm_notify(MAILSTREAM * stream, char *string, long errflg)
{
	ast_debug(5, "Entering NOTIFY callback, errflag is %ld, string is %s\n", errflg, string);
	mm_log (string, errflg);
}


void mm_list(MAILSTREAM * stream, int delim, char *mailbox, long attributes)
{
	if (delimiter == '\0') {
		delimiter = delim;
	}

	ast_debug(5, "Delimiter set to %c and mailbox %s\n", delim, mailbox);
	if (attributes & LATT_NOINFERIORS)
		ast_debug(5, "no inferiors\n");
	if (attributes & LATT_NOSELECT)
		ast_debug(5, "no select\n");
	if (attributes & LATT_MARKED)
		ast_debug(5, "marked\n");
	if (attributes & LATT_UNMARKED)
		ast_debug(5, "unmarked\n");
}


void mm_lsub(MAILSTREAM * stream, int delim, char *mailbox, long attributes)
{
	ast_debug(5, "Delimiter set to %c and mailbox %s\n", delim, mailbox);
	if (attributes & LATT_NOINFERIORS)
		ast_debug(5, "no inferiors\n");
	if (attributes & LATT_NOSELECT)
		ast_debug(5, "no select\n");
	if (attributes & LATT_MARKED)
		ast_debug(5, "marked\n");
	if (attributes & LATT_UNMARKED)
		ast_debug(5, "unmarked\n");
}


void mm_status(MAILSTREAM * stream, char *mailbox, MAILSTATUS * status)
{
	struct ast_str *str;

	if (!DEBUG_ATLEAST(5) || !(str = ast_str_create(256))) {
	    return;
	}

	ast_str_append(&str, 0, " Mailbox %s", mailbox);
	if (status->flags & SA_MESSAGES) {
		ast_str_append(&str, 0, ", %lu messages", status->messages);
	}
	if (status->flags & SA_RECENT) {
		ast_str_append(&str, 0, ", %lu recent", status->recent);
	}
	if (status->flags & SA_UNSEEN) {
		ast_str_append(&str, 0, ", %lu unseen", status->unseen);
	}
	if (status->flags & SA_UIDVALIDITY) {
		ast_str_append(&str, 0, ", %lu UID validity", status->uidvalidity);
	}
	if (status->flags & SA_UIDNEXT) {
		ast_str_append(&str, 0, ", %lu next UID", status->uidnext);
	}
	ast_log(LOG_DEBUG, "%s\n", ast_str_buffer(str));

	ast_free(str);
}


void mm_log(char *string, long errflg)
{
	switch ((short) errflg) {
		case NIL:
			ast_debug(1, "IMAP Info: %s\n", string);
			break;
		case PARSE:
		case WARN:
			ast_log(AST_LOG_WARNING, "IMAP Warning: %s\n", string);
			break;
		case ERROR:
			ast_log(AST_LOG_ERROR, "IMAP Error: %s\n", string);
			break;
	}
}


void mm_dlog(char *string)
{
	ast_log(AST_LOG_NOTICE, "%s\n", string);
}


void mm_login(NETMBX * mb, char *user, char *pwd, long trial)
{
	struct ast_vm_user *vmu;

	ast_debug(4, "Entering callback mm_login\n");

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
	ast_log(AST_LOG_ERROR, "IMAP access FATAL error: %s\n", string);
}

/* C-client callback to handle quota */
static void mm_parsequota(MAILSTREAM *stream, unsigned char *msg, QUOTALIST *pquota)
{
	struct vm_state *vms;
	char *mailbox = stream->mailbox, *user;
	char buf[1024] = "";
	unsigned long usage = 0, limit = 0;

	while (pquota) {
		usage = pquota->usage;
		limit = pquota->limit;
		pquota = pquota->next;
	}

	if (!(user = get_user_by_mailbox(mailbox, buf, sizeof(buf))) || (!(vms = get_vm_state_by_imapuser(user, 2)) && !(vms = get_vm_state_by_imapuser(user, 0)))) {
		ast_log(AST_LOG_ERROR, "No state found.\n");
		return;
	}

	ast_debug(3, "User %s usage is %lu, limit is %lu\n", user, usage, limit);

	vms->quota_usage = usage;
	vms->quota_limit = limit;
}

static char *get_header_by_tag(char *header, char *tag, char *buf, size_t len)
{
	char *start, *eol_pnt;
	int taglen;

	if (ast_strlen_zero(header) || ast_strlen_zero(tag))
		return NULL;

	taglen = strlen(tag) + 1;
	if (taglen < 1)
		return NULL;

	if (!(start = strcasestr(header, tag)))
		return NULL;

	/* Since we can be called multiple times we should clear our buffer */
	memset(buf, 0, len);

	ast_copy_string(buf, start+taglen, len);
	if ((eol_pnt = strchr(buf,'\r')) || (eol_pnt = strchr(buf,'\n')))
		*eol_pnt = '\0';
	return buf;
}

static char *get_user_by_mailbox(char *mailbox, char *buf, size_t len)
{
	char *start, *eol_pnt, *quote;

	if (ast_strlen_zero(mailbox))
		return NULL;

	if (!(start = strstr(mailbox, "/user=")))
		return NULL;

	ast_copy_string(buf, start+6, len);

	if (!(quote = strchr(buf, '"'))) {
		if ((eol_pnt = strchr(buf, '/')) || (eol_pnt = strchr(buf, '}'))) {
			*eol_pnt = '\0';
		}
		return buf;
	} else {
		if ((eol_pnt = strchr(quote + 1, '"'))) {
			*eol_pnt = '\0';
		}
		return quote + 1;
	}
}

static struct vm_state *create_vm_state_from_user(struct ast_vm_user *vmu)
{
	struct vm_state *vms_p;

	pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
	if ((vms_p = pthread_getspecific(ts_vmstate.key)) && !strcmp(vms_p->imapuser, vmu->imapuser) && !strcmp(vms_p->username, vmu->mailbox)) {
		return vms_p;
	}
	ast_debug(5, "Adding new vmstate for %s\n", vmu->imapuser);
	/* XXX: Is this correctly freed always? */
	if (!(vms_p = ast_calloc(1, sizeof(*vms_p))))
		return NULL;
	ast_copy_string(vms_p->imapuser, vmu->imapuser, sizeof(vms_p->imapuser));
	ast_copy_string(vms_p->imapfolder, vmu->imapfolder, sizeof(vms_p->imapfolder));
	ast_copy_string(vms_p->imapserver, vmu->imapserver, sizeof(vms_p->imapserver));
	ast_copy_string(vms_p->imapport, vmu->imapport, sizeof(vms_p->imapport));
	ast_copy_string(vms_p->imapflags, vmu->imapflags, sizeof(vms_p->imapflags));
	ast_copy_string(vms_p->username, vmu->mailbox, sizeof(vms_p->username)); /* save for access from interactive entry point */
	ast_copy_string(vms_p->context, vmu->context, sizeof(vms_p->context));
	vms_p->mailstream = NIL; /* save for access from interactive entry point */
	vms_p->imapversion = vmu->imapversion;
	ast_debug(5, "Copied %s to %s\n", vmu->imapuser, vms_p->imapuser);
	vms_p->updated = 1;
	/* set mailbox to INBOX! */
	ast_copy_string(vms_p->curbox, mbox(vmu, 0), sizeof(vms_p->curbox));
	init_vm_state(vms_p);
	vmstate_insert(vms_p);
	return vms_p;
}

static struct vm_state *get_vm_state_by_imapuser(const char *user, int interactive)
{
	struct vmstate *vlist = NULL;

	if (interactive) {
		struct vm_state *vms;
		pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
		if ((vms = pthread_getspecific(ts_vmstate.key)) && !strcmp(vms->imapuser, user)) {
			return vms;
		}
	}

	AST_LIST_LOCK(&vmstates);
	AST_LIST_TRAVERSE(&vmstates, vlist, list) {
		if (!vlist->vms) {
			ast_debug(3, "error: vms is NULL for %s\n", user);
			continue;
		}
		if (vlist->vms->imapversion != imapversion) {
			continue;
		}

		if (!strcmp(vlist->vms->imapuser, user) && (interactive == 2 || vlist->vms->interactive == interactive)) {
			AST_LIST_UNLOCK(&vmstates);
			return vlist->vms;
		}
	}
	AST_LIST_UNLOCK(&vmstates);

	ast_debug(3, "%s not found in vmstates\n", user);

	return NULL;
}

static struct vm_state *get_vm_state_by_mailbox(const char *mailbox, const char *context, int interactive)
{

	struct vmstate *vlist = NULL;
	const char *local_context = S_OR(context, "default");

	if (interactive) {
		struct vm_state *vms;
		pthread_once(&ts_vmstate.once, ts_vmstate.key_init);
		if ((vms = pthread_getspecific(ts_vmstate.key)) &&
		    !strcmp(vms->username,mailbox) && !strcmp(vms->context, local_context)) {
			return vms;
		}
	}

	AST_LIST_LOCK(&vmstates);
	AST_LIST_TRAVERSE(&vmstates, vlist, list) {
		if (!vlist->vms) {
			ast_debug(3, "error: vms is NULL for %s\n", mailbox);
			continue;
		}
		if (vlist->vms->imapversion != imapversion) {
			continue;
		}

		ast_debug(3, "comparing mailbox %s@%s (i=%d) to vmstate mailbox %s@%s (i=%d)\n", mailbox, local_context, interactive, vlist->vms->username, vlist->vms->context, vlist->vms->interactive);

		if (!strcmp(vlist->vms->username, mailbox) && !strcmp(vlist->vms->context, local_context) && vlist->vms->interactive == interactive) {
			ast_debug(3, "Found it!\n");
			AST_LIST_UNLOCK(&vmstates);
			return vlist->vms;
		}
	}
	AST_LIST_UNLOCK(&vmstates);

	ast_debug(3, "%s not found in vmstates\n", mailbox);

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
			ast_debug(3, "Duplicate mailbox %s, copying message info...\n", vms->username);
			vms->newmessages = altvms->newmessages;
			vms->oldmessages = altvms->oldmessages;
			vms->vmArrayIndex = altvms->vmArrayIndex;
			/* XXX: no msgArray copying? */
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

	if (!(v = ast_calloc(1, sizeof(*v))))
		return;

	v->vms = vms;

	ast_debug(3, "Inserting vm_state for user:%s, mailbox %s\n", vms->imapuser, vms->username);

	AST_LIST_LOCK(&vmstates);
	AST_LIST_INSERT_TAIL(&vmstates, v, list);
	AST_LIST_UNLOCK(&vmstates);
}

static void vmstate_delete(struct vm_state *vms)
{
	struct vmstate *vc = NULL;
	struct vm_state *altvms = NULL;

	/* If interactive, we should copy pertinent info
	   back to the persistent state (to make update immediate) */
	if (vms->interactive == 1 && (altvms = vms->persist_vms)) {
		ast_debug(3, "Duplicate mailbox %s, copying message info...\n", vms->username);
		altvms->newmessages = vms->newmessages;
		altvms->oldmessages = vms->oldmessages;
		altvms->updated = 1;
		vms->mailstream = mail_close(vms->mailstream);

		/* Interactive states are not stored within the persistent list */
		return;
	}

	ast_debug(3, "Removing vm_state for user:%s, mailbox %s\n", vms->imapuser, vms->username);

	AST_LIST_LOCK(&vmstates);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&vmstates, vc, list) {
		if (vc->vms == vms) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&vmstates);

	if (vc) {
		ast_mutex_destroy(&vc->vms->lock);
		ast_free(vc->vms->msgArray);
		vc->vms->msgArray = NULL;
		vc->vms->msg_array_max = 0;
		/* XXX: is no one supposed to free vms itself? */
		ast_free(vc);
	} else {
		ast_log(AST_LOG_ERROR, "No vmstate found for user:%s, mailbox %s\n", vms->imapuser, vms->username);
	}
}

static void set_update(MAILSTREAM * stream)
{
	struct vm_state *vms;
	char *mailbox = stream->mailbox, *user;
	char buf[1024] = "";

	if (!(user = get_user_by_mailbox(mailbox, buf, sizeof(buf))) || !(vms = get_vm_state_by_imapuser(user, 0))) {
		if (user && DEBUG_ATLEAST(3))
			ast_log(AST_LOG_WARNING, "User %s mailbox not found for update.\n", user);
		return;
	}

	ast_debug(3, "User %s mailbox set for update.\n", user);

	vms->updated = 1; /* Set updated flag since mailbox changed */
}

static void init_vm_state(struct vm_state *vms)
{
	vms->msg_array_max = VMSTATE_MAX_MSG_ARRAY;
	vms->msgArray = ast_calloc(vms->msg_array_max, sizeof(long));
	if (!vms->msgArray) {
		/* Out of mem? This can't be good. */
		vms->msg_array_max = 0;
	}
	vms->vmArrayIndex = 0;
	ast_mutex_init(&vms->lock);
}

static int save_body(BODY *body, struct vm_state *vms, char *section, char *format, int is_intro)
{
	char *body_content;
	char *body_decoded;
	char *fn = is_intro ? vms->introfn : vms->fn;
	unsigned long len = 0;
	unsigned long newlen = 0;
	char filename[256];

	if (!body || body == NIL)
		return -1;

	ast_mutex_lock(&vms->lock);
	body_content = mail_fetchbody(vms->mailstream, vms->msgArray[vms->curmsg], section, &len);
	ast_mutex_unlock(&vms->lock);
	if (len > MAX_MAIL_BODY_CONTENT_SIZE) {
		ast_log(AST_LOG_ERROR,
			"Msgno %ld, section %s. The body's content size %ld is huge (max %ld). User:%s, mailbox %s\n",
			vms->msgArray[vms->curmsg], section, len, MAX_MAIL_BODY_CONTENT_SIZE, vms->imapuser, vms->username);
		return -1;
	}
	if (body_content != NIL && len) {
		snprintf(filename, sizeof(filename), "%s.%s", fn, format);
		/* ast_debug(1, body_content); */
		body_decoded = rfc822_base64((unsigned char *) body_content, len, &newlen);
		/* If the body of the file is empty, return an error */
		if (!newlen || !body_decoded) {
			return -1;
		}
		write_file(filename, (char *) body_decoded, newlen);
	} else {
		ast_debug(5, "Body of message is NULL.\n");
		return -1;
	}
	return 0;
}

/*!
 * \brief Get delimiter via mm_list callback
 * \param vms		The voicemail state object
 * \param stream
 *
 * Determines the delimiter character that is used by the underlying IMAP based mail store.
 */
/* MUTEX should already be held */
static void get_mailbox_delimiter(struct vm_state *vms, MAILSTREAM *stream) {
	char tmp[50];
	snprintf(tmp, sizeof(tmp), "{%s}", S_OR(vms->imapserver, imapserver));
	mail_list(stream, tmp, "*");
}

/*!
 * \brief Check Quota for user
 * \param vms a pointer to a vm_state struct, will use the mailstream property of this.
 * \param mailbox the mailbox to check the quota for.
 *
 * Calls imap_getquotaroot, which will populate its results into the vm_state vms input structure.
 */
static void check_quota(struct vm_state *vms, char *mailbox) {
	ast_mutex_lock(&vms->lock);
	mail_parameters(NULL, SET_QUOTA, (void *) mm_parsequota);
	ast_debug(3, "Mailbox name set to: %s, about to check quotas\n", mailbox);
	if (vms && vms->mailstream != NULL) {
		imap_getquotaroot(vms->mailstream, mailbox);
	} else {
		ast_log(AST_LOG_WARNING, "Mailstream not available for mailbox: %s\n", mailbox);
	}
	ast_mutex_unlock(&vms->lock);
}

#endif /* IMAP_STORAGE */

/*! \brief Lock file path
 * only return failure if ast_lock_path returns 'timeout',
 * not if the path does not exist or any other reason
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

#define MSG_ID_LEN 256

/* Used to attach a unique identifier to an msg_id */
static int msg_id_incrementor;

/*!
 * \brief Sets the destination string to a uniquely identifying msg_id string
 * \param dst pointer to a character buffer that should contain MSG_ID_LEN characters.
 */
static void generate_msg_id(char *dst);

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
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}
	res = ast_odbc_prepare(obj, stmt, gps->sql);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Prepare failed![%s]\n", gps->sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}
	for (i = 0; i < gps->argc; i++)
		SQLBindParameter(stmt, i + 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(gps->argv[i]), 0, gps->argv[i], 0, NULL);

	return stmt;
}

static void odbc_update_msg_id(char *dir, int msg_num, char *msg_id)
{
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	struct odbc_obj *obj;
	char msg_num_str[20];
	char *argv[] = { msg_id, dir, msg_num_str };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 3, .argv = argv };

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(LOG_WARNING, "Unable to update message ID for message %d in %s\n", msg_num, dir);
		return;
	}

	snprintf(msg_num_str, sizeof(msg_num_str), "%d", msg_num);
	snprintf(sql, sizeof(sql), "UPDATE %s SET msg_id=? WHERE dir=? AND msgnum=?", odbc_table);
	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
	} else {
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	}
	ast_odbc_release_obj(obj);
	return;
}

/*!
 * \brief Retrieves a file from an ODBC data store.
 * \param dir the path to the file to be retrieved.
 * \param msgnum the message number, such as within a mailbox folder.
 *
 * This method is used by the RETRIEVE macro when mailboxes are stored in an ODBC back end.
 * The purpose is to get the message from the database store to the local file system, so that the message may be played, or the information file may be read.
 *
 * The file is looked up by invoking a SQL on the odbc_table (default 'voicemessages') using the dir and msgnum input parameters.
 * The output is the message information file with the name msgnum and the extension .txt
 * and the message file with the extension of its format, in the directory with base file name of the msgnum.
 *
 * \return 0 on success, -1 on error.
 */
static int retrieve_file(char *dir, int msgnum)
{
	int x = 0;
	int res;
	int fd = -1;
	size_t fdlen = 0;
	void *fdm = MAP_FAILED;
	SQLSMALLINT colcount = 0;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char fmt[80] = "";
	char *c;
	char coltitle[256];
	SQLSMALLINT collen;
	SQLSMALLINT datatype;
	SQLSMALLINT decimaldigits;
	SQLSMALLINT nullable;
	SQLULEN colsize;
	SQLLEN colsize2;
	FILE *f = NULL;
	char rowdata[80];
	char fn[PATH_MAX];
	char full_fn[PATH_MAX];
	char msgnums[80];
	char msg_id[MSG_ID_LEN] = "";
	char *argv[] = { dir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 2, .argv = argv };
	struct odbc_obj *obj;

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return -1;
	}

	ast_copy_string(fmt, vmfmts, sizeof(fmt));
	c = strchr(fmt, '|');
	if (c)
		*c = '\0';
	if (!strcasecmp(fmt, "wav49"))
		strcpy(fmt, "WAV");

	snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
	if (msgnum > -1)
		make_file(fn, sizeof(fn), dir, msgnum);
	else
		ast_copy_string(fn, dir, sizeof(fn));

	/* Create the information file */
	snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);

	if (!(f = fopen(full_fn, "w+"))) {
		ast_log(AST_LOG_WARNING, "Failed to open/create '%s'\n", full_fn);
		goto bail;
	}

	snprintf(full_fn, sizeof(full_fn), "%s.%s", fn, fmt);
	snprintf(sql, sizeof(sql), "SELECT * FROM %s WHERE dir=? AND msgnum=?", odbc_table);

	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		goto bail;
	}

	res = SQLFetch(stmt);
	if (!SQL_SUCCEEDED(res)) {
		if (res != SQL_NO_DATA) {
			ast_log(AST_LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		}
		goto bail_with_handle;
	}

	fd = open(full_fn, O_RDWR | O_CREAT | O_TRUNC, VOICEMAIL_FILE_MODE);
	if (fd < 0) {
		ast_log(AST_LOG_WARNING, "Failed to write '%s': %s\n", full_fn, strerror(errno));
		goto bail_with_handle;
	}

	res = SQLNumResultCols(stmt, &colcount);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Column Count error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}

	fprintf(f, "[message]\n");
	for (x = 0; x < colcount; x++) {
		rowdata[0] = '\0';
		colsize = 0;
		collen = sizeof(coltitle);
		res = SQLDescribeCol(stmt, x + 1, (unsigned char *) coltitle, sizeof(coltitle), &collen,
							&datatype, &colsize, &decimaldigits, &nullable);
		if (!SQL_SUCCEEDED(res)) {
			ast_log(AST_LOG_WARNING, "SQL Describe Column error!\n[%s]\n\n", sql);
			goto bail_with_handle;
		}
		if (!strcasecmp(coltitle, "recording")) {
			off_t offset;
			res = SQLGetData(stmt, x + 1, SQL_BINARY, rowdata, 0, &colsize2);
			fdlen = colsize2;
			if (fd > -1) {
				char tmp[1] = "";
				lseek(fd, fdlen - 1, SEEK_SET);
				if (write(fd, tmp, 1) != 1) {
					close(fd);
					fd = -1;
					continue;
				}
				/* Read out in small chunks */
				for (offset = 0; offset < colsize2; offset += CHUNKSIZE) {
					if ((fdm = mmap(NULL, CHUNKSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset)) == MAP_FAILED) {
						ast_log(AST_LOG_WARNING, "Could not mmap the output file: %s (%d)\n", strerror(errno), errno);
						goto bail_with_handle;
					}
					res = SQLGetData(stmt, x + 1, SQL_BINARY, fdm, CHUNKSIZE, NULL);
					munmap(fdm, CHUNKSIZE);
					if (!SQL_SUCCEEDED(res)) {
						ast_log(AST_LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
						unlink(full_fn);
						goto bail_with_handle;
					}
				}
				if (truncate(full_fn, fdlen) < 0) {
					ast_log(LOG_WARNING, "Unable to truncate '%s': %s\n", full_fn, strerror(errno));
				}
			}
		} else {
			res = SQLGetData(stmt, x + 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
			if (res == SQL_NULL_DATA && !strcasecmp(coltitle, "msg_id")) {
				/* Generate msg_id now, but don't store it until we're done with this
				   connection */
				generate_msg_id(msg_id);
				snprintf(rowdata, sizeof(rowdata), "%s", msg_id);
			} else if (res == SQL_NULL_DATA && !strcasecmp(coltitle, "category")) {
				/* Ignore null column value for category */
				ast_debug(3, "Ignoring null category column in ODBC voicemail retrieve_file.\n");
				continue;
			} else if (!SQL_SUCCEEDED(res)) {
				ast_log(AST_LOG_WARNING, "SQL Get Data error! coltitle=%s\n[%s]\n\n", coltitle, sql);
				goto bail_with_handle;
			}
			if (strcasecmp(coltitle, "msgnum") && strcasecmp(coltitle, "dir")) {
				fprintf(f, "%s=%s\n", coltitle, rowdata);
			}
		}
	}

bail_with_handle:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

bail:
	if (f)
		fclose(f);
	if (fd > -1)
		close(fd);

	ast_odbc_release_obj(obj);

	/* If res_odbc is configured to only allow a single database connection, we
	   will deadlock if we try to do this before releasing the connection we
	   were just using. */
	if (!ast_strlen_zero(msg_id)) {
		odbc_update_msg_id(dir, msgnum, msg_id);
	}

	return x - 1;
}

/*!
 * \brief Determines the highest message number in use for a given user and mailbox folder.
 * \param vmu
 * \param dir the folder the mailbox folder to look for messages. Used to construct the SQL where clause.
 *
 * This method is used when mailboxes are stored in an ODBC back end.
 * Typical use to set the msgnum would be to take the value returned from this method and add one to it.
 *
 * \return the value of zero or greater to indicate the last message index in use, -1 to indicate none.

 */
static int last_message_index(struct ast_vm_user *vmu, char *dir)
{
	int x = -1;
	int res;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char rowdata[20];
	char *argv[] = { dir };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 1, .argv = argv };
	struct odbc_obj *obj;

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return -1;
	}

	snprintf(sql, sizeof(sql), "SELECT msgnum FROM %s WHERE dir=? order by msgnum desc", odbc_table);

	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		goto bail;
	}

	res = SQLFetch(stmt);
	if (!SQL_SUCCEEDED(res)) {
		if (res == SQL_NO_DATA) {
			ast_log(AST_LOG_DEBUG, "Directory '%s' has no messages and therefore no index was retrieved.\n", dir);
		} else {
			ast_log(AST_LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		}
		goto bail_with_handle;
	}

	res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}

	if (sscanf(rowdata, "%30d", &x) != 1) {
		ast_log(AST_LOG_WARNING, "Failed to read message index!\n");
	}

bail_with_handle:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

bail:
	ast_odbc_release_obj(obj);

	return x;
}

/*!
 * \brief Determines if the specified message exists.
 * \param dir the folder the mailbox folder to look for messages.
 * \param msgnum the message index to query for.
 *
 * This method is used when mailboxes are stored in an ODBC back end.
 *
 * \return greater than zero if the message exists, zero when the message does not exist or on error.
 */
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
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return 0;
	}

	snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir=? AND msgnum=?", odbc_table);
	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		goto bail;
	}

	res = SQLFetch(stmt);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}

	res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}

	if (sscanf(rowdata, "%30d", &x) != 1) {
		ast_log(AST_LOG_WARNING, "Failed to read message count!\n");
	}

bail_with_handle:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

bail:
	ast_odbc_release_obj(obj);
	return x;
}

/*!
 * \brief returns the number of messages found.
 * \param vmu
 * \param dir the folder the mailbox folder to look for messages. Used to construct the SQL where clause.
 *
 * This method is used when mailboxes are stored in an ODBC back end.
 *
 * \return The count of messages being zero or more, less than zero on error.
 */
static int count_messages(struct ast_vm_user *vmu, char *dir)
{
	int x = -1;
	int res;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char rowdata[20];
	char *argv[] = { dir };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 1, .argv = argv };
	struct odbc_obj *obj;

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return -1;
	}

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir=?", odbc_table);
	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		goto bail;
	}

	res = SQLFetch(stmt);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}

	res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}

	if (sscanf(rowdata, "%30d", &x) != 1) {
		ast_log(AST_LOG_WARNING, "Failed to read message count!\n");
	}

bail_with_handle:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

bail:
	ast_odbc_release_obj(obj);
	return x;
}

/*!
 * \brief Deletes a message from the mailbox folder.
 * \param sdir The mailbox folder to work in.
 * \param smsg The message index to be deleted.
 *
 * This method is used when mailboxes are stored in an ODBC back end.
 * The specified message is directly deleted from the database 'voicemessages' table.
 */
static void delete_file(const char *sdir, int smsg)
{
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char msgnums[20];
	char *argv[] = { NULL, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 2, .argv = argv };
	struct odbc_obj *obj;

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return;
	}

	argv[0] = ast_strdupa(sdir);

	snprintf(msgnums, sizeof(msgnums), "%d", smsg);
	snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE dir=? AND msgnum=?", odbc_table);
	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
	} else {
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	}
	ast_odbc_release_obj(obj);

	return;
}

/*!
 * \brief Copies a voicemail from one mailbox to another.
 * \param sdir the folder for which to look for the message to be copied.
 * \param smsg the index of the message to be copied.
 * \param ddir the destination folder to copy the message into.
 * \param dmsg the index to be used for the copied message.
 * \param dmailboxuser The user who owns the mailbox tha contains the destination folder.
 * \param dmailboxcontext The context for the destination user.
 *
 * This method is used for the COPY macro when mailboxes are stored in an ODBC back end.
 */
static void copy_file(char *sdir, int smsg, char *ddir, int dmsg, char *dmailboxuser, char *dmailboxcontext)
{
	SQLHSTMT stmt;
	char sql[512];
	char msgnums[20];
	char msgnumd[20];
	char msg_id[MSG_ID_LEN];
	struct odbc_obj *obj;
	char *argv[] = { ddir, msgnumd, msg_id, dmailboxuser, dmailboxcontext, sdir, msgnums };
	struct generic_prepare_struct gps = { .sql = sql, .argc = 7, .argv = argv };

	generate_msg_id(msg_id);
	delete_file(ddir, dmsg);
	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return;
	}

	snprintf(msgnums, sizeof(msgnums), "%d", smsg);
	snprintf(msgnumd, sizeof(msgnumd), "%d", dmsg);
	snprintf(sql, sizeof(sql), "INSERT INTO %s (dir, msgnum, msg_id, context, callerid, origtime, duration, recording, flag, mailboxuser, mailboxcontext) SELECT ?,?,?,context,callerid,origtime,duration,recording,flag,?,? FROM %s WHERE dir=? AND msgnum=?", odbc_table, odbc_table);
	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt)
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s] (You probably don't have MySQL 4.1 or later installed)\n\n", sql);
	else
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);

	return;
}

struct insert_data {
	char *sql;
	const char *dir;
	const char *msgnums;
	void *data;
	SQLLEN datalen;
	SQLLEN indlen;
	const char *context;
	const char *callerid;
	const char *origtime;
	const char *duration;
	const char *mailboxuser;
	const char *mailboxcontext;
	const char *category;
	const char *flag;
	const char *msg_id;
};

static SQLHSTMT insert_data_cb(struct odbc_obj *obj, void *vdata)
{
	struct insert_data *data = vdata;
	int res;
	SQLHSTMT stmt;

	res = SQLAllocHandle(SQL_HANDLE_STMT, obj->con, &stmt);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Alloc Handle failed!\n");
		return NULL;
	}

	SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->dir), 0, (void *) data->dir, 0, NULL);
	SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->msgnums), 0, (void *) data->msgnums, 0, NULL);
	SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, data->datalen, 0, (void *) data->data, data->datalen, &data->indlen);
	SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->context), 0, (void *) data->context, 0, NULL);
	SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->callerid), 0, (void *) data->callerid, 0, NULL);
	SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->origtime), 0, (void *) data->origtime, 0, NULL);
	SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->duration), 0, (void *) data->duration, 0, NULL);
	SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->mailboxuser), 0, (void *) data->mailboxuser, 0, NULL);
	SQLBindParameter(stmt, 10, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->mailboxcontext), 0, (void *) data->mailboxcontext, 0, NULL);
	SQLBindParameter(stmt, 11, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->flag), 0, (void *) data->flag, 0, NULL);
	SQLBindParameter(stmt, 12, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->msg_id), 0, (void *) data->msg_id, 0, NULL);
	if (!ast_strlen_zero(data->category)) {
		SQLBindParameter(stmt, 13, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, strlen(data->category), 0, (void *) data->category, 0, NULL);
	}
	res = ast_odbc_execute_sql(obj, stmt, data->sql);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Direct Execute failed!\n");
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return NULL;
	}

	return stmt;
}

/*!
 * \brief Stores a voicemail into the database.
 * \param dir the folder the mailbox folder to store the message.
 * \param mailboxuser the user owning the mailbox folder.
 * \param mailboxcontext
 * \param msgnum the message index for the message to be stored.
 *
 * This method is used when mailboxes are stored in an ODBC back end.
 * The message sound file and information file is looked up on the file system.
 * A SQL query is invoked to store the message into the (MySQL) database.
 *
 * \return the zero on success -1 on error.
 */
static int store_file(const char *dir, const char *mailboxuser, const char *mailboxcontext, int msgnum)
{
	int res = 0;
	int fd = -1;
	void *fdm = MAP_FAILED;
	off_t fdlen = -1;
	SQLHSTMT stmt;
	char sql[PATH_MAX];
	char msgnums[20];
	char fn[PATH_MAX];
	char full_fn[PATH_MAX];
	char fmt[80]="";
	char *c;
	struct ast_config *cfg = NULL;
	struct odbc_obj *obj;
	struct insert_data idata = { .sql = sql, .msgnums = msgnums, .dir = dir, .mailboxuser = mailboxuser, .mailboxcontext = mailboxcontext,
		.context = "", .callerid = "", .origtime = "", .duration = "", .category = "", .flag = "", .msg_id = "" };
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };

	delete_file(dir, msgnum);

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return -1;
	}

	do {
		ast_copy_string(fmt, vmfmts, sizeof(fmt));
		c = strchr(fmt, '|');
		if (c)
			*c = '\0';
		if (!strcasecmp(fmt, "wav49"))
			strcpy(fmt, "WAV");
		snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
		if (msgnum > -1)
			make_file(fn, sizeof(fn), dir, msgnum);
		else
			ast_copy_string(fn, dir, sizeof(fn));
		snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
		cfg = ast_config_load(full_fn, config_flags);
		snprintf(full_fn, sizeof(full_fn), "%s.%s", fn, fmt);
		fd = open(full_fn, O_RDWR);
		if (fd < 0) {
			ast_log(AST_LOG_WARNING, "Open of sound file '%s' failed: %s\n", full_fn, strerror(errno));
			res = -1;
			break;
		}
		if (valid_config(cfg)) {
			if (!(idata.context = ast_variable_retrieve(cfg, "message", "context"))) {
				idata.context = "";
			}
			if (!(idata.callerid = ast_variable_retrieve(cfg, "message", "callerid"))) {
				idata.callerid = "";
			}
			if (!(idata.origtime = ast_variable_retrieve(cfg, "message", "origtime"))) {
				idata.origtime = "";
			}
			if (!(idata.duration = ast_variable_retrieve(cfg, "message", "duration"))) {
				idata.duration = "";
			}
			if (!(idata.category = ast_variable_retrieve(cfg, "message", "category"))) {
				idata.category = "";
			}
			if (!(idata.flag = ast_variable_retrieve(cfg, "message", "flag"))) {
				idata.flag = "";
			}
			if (!(idata.msg_id = ast_variable_retrieve(cfg, "message", "msg_id"))) {
				idata.msg_id = "";
			}
		}
		fdlen = lseek(fd, 0, SEEK_END);
		if (fdlen < 0 || lseek(fd, 0, SEEK_SET) < 0) {
			ast_log(AST_LOG_WARNING, "Failed to process sound file '%s': %s\n", full_fn, strerror(errno));
			res = -1;
			break;
		}
		fdm = mmap(NULL, fdlen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (fdm == MAP_FAILED) {
			ast_log(AST_LOG_WARNING, "Memory map failed for sound file '%s'!\n", full_fn);
			res = -1;
			break;
		}
		idata.data = fdm;
		idata.datalen = idata.indlen = fdlen;

		if (!ast_strlen_zero(idata.category))
			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,callerid,origtime,duration,mailboxuser,mailboxcontext,flag,msg_id,category) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", odbc_table);
		else
			snprintf(sql, sizeof(sql), "INSERT INTO %s (dir,msgnum,recording,context,callerid,origtime,duration,mailboxuser,mailboxcontext,flag,msg_id) VALUES (?,?,?,?,?,?,?,?,?,?,?)", odbc_table);

		if (ast_strlen_zero(idata.origtime)) {
			idata.origtime = "0";
		}

		if (ast_strlen_zero(idata.duration)) {
			idata.duration = "0";
		}

		if ((stmt = ast_odbc_direct_execute(obj, insert_data_cb, &idata))) {
			SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		} else {
			ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
			res = -1;
		}
	} while (0);

	ast_odbc_release_obj(obj);

	if (valid_config(cfg))
		ast_config_destroy(cfg);
	if (fdm != MAP_FAILED)
		munmap(fdm, fdlen);
	if (fd > -1)
		close(fd);
	return res;
}

/*!
 * \brief Renames a message in a mailbox folder.
 * \param sdir The folder of the message to be renamed.
 * \param smsg The index of the message to be renamed.
 * \param mailboxuser The user to become the owner of the message after it is renamed. Usually this will be the same as the original owner.
 * \param mailboxcontext The context to be set for the message. Usually this will be the same as the original context.
 * \param ddir The destination folder for the message to be renamed into
 * \param dmsg The destination message for the message to be renamed.
 *
 * This method is used by the RENAME macro when mailboxes are stored in an ODBC back end.
 * The is usually used to resequence the messages in the mailbox, such as to delete messag index 0, it would be called successively to slide all the other messages down one index.
 * But in theory, because the SQL query performs an update on (dir, msgnum, mailboxuser, mailboxcontext) in the database, it should be possible to have the message relocated to another mailbox or context as well.
 */
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
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return;
	}

	snprintf(msgnums, sizeof(msgnums), "%d", smsg);
	snprintf(msgnumd, sizeof(msgnumd), "%d", dmsg);
	snprintf(sql, sizeof(sql), "UPDATE %s SET dir=?, msgnum=?, mailboxuser=?, mailboxcontext=? WHERE dir=? AND msgnum=?", odbc_table);
	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt)
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
	else
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	ast_odbc_release_obj(obj);
	return;
}

/*!
 * \brief Removes a voicemail message file.
 * \param dir the path to the message file.
 * \param msgnum the unique number for the message within the mailbox.
 *
 * Removes the message content file and the information file.
 * This method is used by the DISPOSE macro when mailboxes are stored in an ODBC back end.
 * Typical use is to clean up after a RETRIEVE operation.
 * Note that this does not remove the message from the mailbox folders, to do that we would use delete_file().
 * \return zero on success, -1 on error.
 */
static int remove_file(char *dir, int msgnum)
{
	char fn[PATH_MAX] = "";
	char full_fn[PATH_MAX + 4]; /* Plus .txt */
	char msgnums[80];

	if (msgnum > -1) {
		snprintf(msgnums, sizeof(msgnums), "%d", msgnum);
		make_file(fn, sizeof(fn), dir, msgnum);
	} else {
		ast_copy_string(fn, dir, sizeof(fn));
	}
	ast_filedelete(fn, NULL);
	snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
	unlink(full_fn);
	return 0;
}
#else
#ifndef IMAP_STORAGE
/*!
 * \brief Find all .txt files - even if they are not in sequence from 0000.
 * \param vmu
 * \param dir
 *
 * This method is used when mailboxes are stored on the filesystem. (not ODBC and not IMAP).
 *
 * \return the count of messages, zero or more.
 */
static int count_messages(struct ast_vm_user *vmu, char *dir)
{

	int vmcount = 0;
	DIR *vmdir = NULL;
	struct dirent *vment = NULL;

	if (vm_lock_path(dir))
		return ERROR_LOCK_PATH;

	if ((vmdir = opendir(dir))) {
		while ((vment = readdir(vmdir))) {
			if (strlen(vment->d_name) > 7 && !strncmp(vment->d_name + 7, ".txt", 4)) {
				vmcount++;
			}
		}
		closedir(vmdir);
	}
	ast_unlock_path(dir);

	return vmcount;
}

/*!
 * \brief Renames a message in a mailbox folder.
 * \param sfn The path to the mailbox information and data file to be renamed.
 * \param dfn The path for where the message data and information files will be renamed to.
 *
 * This method is used by the RENAME macro when mailboxes are stored on the filesystem. (not ODBC and not IMAP).
 */
static void rename_file(char *sfn, char *dfn)
{
	char stxt[PATH_MAX];
	char dtxt[PATH_MAX];
	ast_filerename(sfn, dfn, NULL);
	snprintf(stxt, sizeof(stxt), "%s.txt", sfn);
	snprintf(dtxt, sizeof(dtxt), "%s.txt", dfn);
	if (ast_check_realtime("voicemail_data")) {
		ast_update_realtime("voicemail_data", "filename", sfn, "filename", dfn, SENTINEL);
	}
	rename(stxt, dtxt);
}

/*!
 * \brief Determines the highest message number in use for a given user and mailbox folder.
 * \param vmu
 * \param dir the folder the mailbox folder to look for messages. Used to construct the SQL where clause.
 *
 * This method is used when mailboxes are stored on the filesystem. (not ODBC and not IMAP).
 * Typical use to set the msgnum would be to take the value returned from this method and add one to it.
 *
 * \note Should always be called with a lock already set on dir.
 * \return the value of zero or greaterto indicate the last message index in use, -1 to indicate none.
 */
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
			ast_debug(4, "%s map[%d] = %d, count = %d\n", dir, msgdirint, map[msgdirint], stopcount);
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

#endif /* #ifndef IMAP_STORAGE */
#endif /* #else of #ifdef ODBC_STORAGE */
#ifndef IMAP_STORAGE
/*!
 * \brief Utility function to copy a file.
 * \param infile The path to the file to be copied. The file must be readable, it is opened in read only mode.
 * \param outfile The path for which to copy the file to. The directory permissions must allow the creation (or truncation) of the file, and allow for opening the file in write only mode.
 *
 * When the compiler option HARDLINK_WHEN_POSSIBLE is set, the copy operation will attempt to use the hard link facility instead of copy the file (to save disk space). If the link operation fails, it falls back to the copy operation.
 * The copy operation copies up to 4096 bytes at once.
 *
 * \return zero on success, -1 on error.
 */
static int copy(char *infile, char *outfile)
{
	int ifd;
	int ofd;
	int res = -1;
	int len;
	char buf[4096];

#ifdef HARDLINK_WHEN_POSSIBLE
	/* Hard link if possible; saves disk space & is faster */
	if (!link(infile, outfile)) {
		return 0;
	}
#endif

	if ((ifd = open(infile, O_RDONLY)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to open %s in read-only mode: %s\n", infile, strerror(errno));
		return -1;
	}

	if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, VOICEMAIL_FILE_MODE)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to open %s in write-only mode: %s\n", outfile, strerror(errno));
		close(ifd);
		return -1;
	}

	for (;;) {
		int wrlen;

		len = read(ifd, buf, sizeof(buf));
		if (!len) {
			res = 0;
			break;
		}

		if (len < 0) {
			ast_log(AST_LOG_WARNING, "Read failed on %s: %s\n", infile, strerror(errno));
			break;
		}

		wrlen = write(ofd, buf, len);
		if (errno == ENOMEM || errno == ENOSPC || wrlen != len) {
			ast_log(AST_LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, wrlen, len, strerror(errno));
			break;
		}
	}

	close(ifd);
	close(ofd);
	if (res) {
		unlink(outfile);
	}

	return res;
}

/*!
 * \brief Copies a voicemail information (envelope) file.
 * \param frompath
 * \param topath
 *
 * Every voicemail has the data (.wav) file, and the information file.
 * This function performs the file system copying of the information file for a voicemail, handling the internal fields and their values.
 * This is used by the COPY macro when not using IMAP storage.
 */
static void copy_plain_file(char *frompath, char *topath)
{
	char frompath2[PATH_MAX], topath2[PATH_MAX];
	struct ast_variable *tmp, *var = NULL;
	const char *origmailbox = "", *context = "", *exten = "";
	const char *priority = "", *callerchan = "", *callerid = "", *origdate = "";
	const char *origtime = "", *category = "", *duration = "";

	ast_filecopy(frompath, topath, NULL);
	snprintf(frompath2, sizeof(frompath2), "%s.txt", frompath);
	snprintf(topath2, sizeof(topath2), "%s.txt", topath);

	if (ast_check_realtime("voicemail_data")) {
		var = ast_load_realtime("voicemail_data", "filename", frompath, SENTINEL);
		/* This cycle converts ast_variable linked list, to va_list list of arguments, may be there is a better way to do it? */
		for (tmp = var; tmp; tmp = tmp->next) {
			if (!strcasecmp(tmp->name, "origmailbox")) {
				origmailbox = tmp->value;
			} else if (!strcasecmp(tmp->name, "context")) {
				context = tmp->value;
			} else if (!strcasecmp(tmp->name, "exten")) {
				exten = tmp->value;
			} else if (!strcasecmp(tmp->name, "priority")) {
				priority = tmp->value;
			} else if (!strcasecmp(tmp->name, "callerchan")) {
				callerchan = tmp->value;
			} else if (!strcasecmp(tmp->name, "callerid")) {
				callerid = tmp->value;
			} else if (!strcasecmp(tmp->name, "origdate")) {
				origdate = tmp->value;
			} else if (!strcasecmp(tmp->name, "origtime")) {
				origtime = tmp->value;
			} else if (!strcasecmp(tmp->name, "category")) {
				category = tmp->value;
			} else if (!strcasecmp(tmp->name, "duration")) {
				duration = tmp->value;
			}
		}
		ast_store_realtime("voicemail_data", "filename", topath, "origmailbox", origmailbox, "context", context, "exten", exten, "priority", priority, "callerchan", callerchan, "callerid", callerid, "origdate", origdate, "origtime", origtime, "category", category, "duration", duration, SENTINEL);
	}
	copy(frompath2, topath2);
	ast_variables_destroy(var);
}
#endif

/*!
 * \brief Removes the voicemail sound and information file.
 * \param file The path to the sound file. This will be the folder and message index, without the extension.
 *
 * This is used by the DELETE macro when voicemails are stored on the file system.
 *
 * \return zero on success, -1 on error.
 */
static int vm_delete(char *file)
{
	char *txt;
	int txtsize = 0;

	txtsize = (strlen(file) + 5)*sizeof(char);
	txt = ast_alloca(txtsize);
	/* Sprintf here would safe because we alloca'd exactly the right length,
	 * but trying to eliminate all sprintf's anyhow
	 */
	if (ast_check_realtime("voicemail_data")) {
		ast_destroy_realtime("voicemail_data", "filename", file, SENTINEL);
	}
	snprintf(txt, txtsize, "%s.txt", file);
	unlink(txt);
	return ast_filedelete(file, NULL);
}

static void prep_email_sub_vars(struct ast_channel *ast, struct ast_vm_user *vmu, int msgnum, char *context, char *mailbox, const char *fromfolder, char *cidnum, char *cidname, char *dur, char *date, const char *category, const char *flag)
{
	char callerid[256];
	char num[12];
	char fromdir[256], fromfile[256];
	struct ast_config *msg_cfg;
	const char *origcallerid, *origtime;
	char origcidname[80], origcidnum[80], origdate[80];
	int inttime;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };

	/* Prepare variables for substitution in email body and subject */
	pbx_builtin_setvar_helper(ast, "VM_NAME", vmu->fullname);
	pbx_builtin_setvar_helper(ast, "VM_DUR", dur);
	snprintf(num, sizeof(num), "%d", msgnum);
	pbx_builtin_setvar_helper(ast, "VM_MSGNUM", num);
	pbx_builtin_setvar_helper(ast, "VM_CONTEXT", context);
	pbx_builtin_setvar_helper(ast, "VM_MAILBOX", mailbox);
	pbx_builtin_setvar_helper(ast, "VM_CALLERID", (!ast_strlen_zero(cidname) || !ast_strlen_zero(cidnum)) ?
		ast_callerid_merge(callerid, sizeof(callerid), cidname, cidnum, NULL) : "an unknown caller");
	pbx_builtin_setvar_helper(ast, "VM_CIDNAME", (!ast_strlen_zero(cidname) ? cidname : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_CIDNUM", (!ast_strlen_zero(cidnum) ? cidnum : "an unknown caller"));
	pbx_builtin_setvar_helper(ast, "VM_DATE", date);
	pbx_builtin_setvar_helper(ast, "VM_CATEGORY", category ? ast_strdupa(category) : "no category");
	pbx_builtin_setvar_helper(ast, "VM_FLAG", flag);

	/* Retrieve info from VM attribute file */
	make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, fromfolder);
	make_file(fromfile, sizeof(fromfile), fromdir, msgnum - 1);
	if (strlen(fromfile) < sizeof(fromfile) - 5) {
		strcat(fromfile, ".txt");
	}
	if (!(msg_cfg = ast_config_load(fromfile, config_flags)) || !(valid_config(msg_cfg))) {
		ast_debug(1, "Config load for message text file '%s' failed\n", fromfile);
		return;
	}

	if ((origcallerid = ast_variable_retrieve(msg_cfg, "message", "callerid"))) {
		pbx_builtin_setvar_helper(ast, "ORIG_VM_CALLERID", origcallerid);
		ast_callerid_split(origcallerid, origcidname, sizeof(origcidname), origcidnum, sizeof(origcidnum));
		pbx_builtin_setvar_helper(ast, "ORIG_VM_CIDNAME", origcidname);
		pbx_builtin_setvar_helper(ast, "ORIG_VM_CIDNUM", origcidnum);
	}

	if ((origtime = ast_variable_retrieve(msg_cfg, "message", "origtime")) && sscanf(origtime, "%30d", &inttime) == 1) {
		struct timeval tv = { inttime, };
		struct ast_tm tm;
		ast_localtime(&tv, &tm, NULL);
		ast_strftime_locale(origdate, sizeof(origdate), emaildateformat, &tm, S_OR(vmu->locale, NULL));
		pbx_builtin_setvar_helper(ast, "ORIG_VM_DATE", origdate);
	}
	ast_config_destroy(msg_cfg);
}

/*!
 * \brief Wraps a character sequence in double quotes, escaping occurences of quotes within the string.
 * \param from The string to work with.
 * \param buf The buffer into which to write the modified quoted string.
 * \param maxlen Always zero, but see \see ast_str
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

/*! \brief
 * fill in *tm for current time according to the proper timezone, if any.
 * \return tm so it can be used as a function argument.
 */
static const struct ast_tm *vmu_tm(const struct ast_vm_user *vmu, struct ast_tm *tm)
{
	const struct vm_zone *z = NULL;
	struct timeval t = ast_tvnow();

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
 * \param end An expandable buffer for holding the result
 * \param maxlen Always zero, but see \see ast_str
 * \param start A string to be encoded
 * \param preamble The length of the first line already used for this string,
 * to ensure that each line maintains a maximum length of 76 chars.
 * \param postamble the length of any additional characters appended to the
 * line, used to ensure proper field wrapping.
 * \retval The encoded string.
 */
static const char *ast_str_encode_mime(struct ast_str **end, ssize_t maxlen, const char *start, size_t preamble, size_t postamble)
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

/*!
 * \brief Creates the email file to be sent to indicate a new voicemail exists for a user.
 * \param p The output file to generate the email contents into.
 * \param srcemail The email address to send the email to, presumably the email address for the owner of the mailbox.
 * \param vmu The voicemail user who is sending the voicemail.
 * \param msgnum The message index in the mailbox folder.
 * \param context
 * \param mailbox The voicemail box to read the voicemail to be notified in this email.
 * \param fromfolder
 * \param cidnum The caller ID number.
 * \param cidname The caller ID name.
 * \param attach the name of the sound file to be attached to the email, if attach_user_voicemail == 1.
 * \param attach2
 * \param format The message sound file format. i.e. .wav
 * \param duration The time of the message content, in seconds.
 * \param attach_user_voicemail if 1, the sound file is attached to the email.
 * \param chan
 * \param category
 * \param imap if == 1, indicates the target folder for the email notification to be sent to will be an IMAP mailstore. This causes additional mailbox headers to be set, which would facilitate searching for the email in the destination IMAP folder.
 * \param flag, msg_id
 *
 * The email body, and base 64 encoded attachment (if any) are stored to the file identified by *p. This method does not actually send the email.  That is done by invoking the configure 'mailcmd' and piping this generated file into it, or with the sendemail() function.
 */
static void make_email_file(FILE *p,
		char *srcemail,
		struct ast_vm_user *vmu,
		int msgnum,
		char *context,
		char *mailbox,
		const char *fromfolder,
		char *cidnum,
		char *cidname,
		char *attach,
		char *attach2,
		char *format,
		int duration,
		int attach_user_voicemail,
		struct ast_channel *chan,
		const char *category,
		int imap,
		const char *flag,
		const char *msg_id)
{
	char date[256];
	char host[MAXHOSTNAMELEN] = "";
	char who[256];
	char bound[256];
	char dur[256];
	struct ast_tm tm;
	char enc_cidnum[256] = "", enc_cidname[256] = "";
	struct ast_str *str1 = ast_str_create(16), *str2 = ast_str_create(16);
	char *greeting_attachment;
	char filename[256];
	int first_line;
	char *emailsbuf;
	char *email;

	if (!str1 || !str2) {
		ast_free(str1);
		ast_free(str2);
		return;
	}

	if (cidnum) {
		strip_control_and_high(cidnum, enc_cidnum, sizeof(enc_cidnum));
	}
	if (cidname) {
		strip_control_and_high(cidname, enc_cidname, sizeof(enc_cidname));
	}
	gethostname(host, sizeof(host) - 1);

	if (strchr(srcemail, '@')) {
		ast_copy_string(who, srcemail, sizeof(who));
	} else {
		snprintf(who, sizeof(who), "%s@%s", srcemail, host);
	}

	greeting_attachment = strrchr(ast_strdupa(attach), '/');
	if (greeting_attachment) {
		*greeting_attachment++ = '\0';
	}

	snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
	ast_strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", vmu_tm(vmu, &tm));
	fprintf(p, "Date: %s" ENDL, date);

	/* Set date format for voicemail mail */
	ast_strftime_locale(date, sizeof(date), emaildateformat, &tm, S_OR(vmu->locale, NULL));

	if (!ast_strlen_zero(fromstring) || !ast_strlen_zero(vmu->fromstring)) {
		struct ast_channel *ast;
		char *e_fromstring = !ast_strlen_zero(vmu->fromstring) ? vmu->fromstring : fromstring;
		if ((ast = ast_dummy_channel_alloc())) {
			char *ptr;
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, enc_cidnum, enc_cidname, dur, date, category, flag);
			ast_str_substitute_variables(&str1, 0, ast, e_fromstring);

			if (check_mime(ast_str_buffer(str1))) {
				first_line = 1;
				ast_str_encode_mime(&str2, 0, ast_str_buffer(str1), strlen("From: "), strlen(who) + 3);
				while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
					*ptr = '\0';
					fprintf(p, "%s %s" ENDL, first_line ? "From:" : "", ast_str_buffer(str2));
					first_line = 0;
					/* Substring is smaller, so this will never grow */
					ast_str_set(&str2, 0, "%s", ptr + 1);
				}
				fprintf(p, "%s %s <%s>" ENDL, first_line ? "From:" : "", ast_str_buffer(str2), who);
			} else {
				fprintf(p, "From: %s <%s>" ENDL, ast_str_quote(&str2, 0, ast_str_buffer(str1)), who);
			}
			ast = ast_channel_unref(ast);
		} else {
			ast_log(AST_LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else {
		fprintf(p, "From: Asterisk PBX <%s>" ENDL, who);
	}

	emailsbuf = ast_strdupa(vmu->email);
	fprintf(p, "To:");
	first_line = 1;
	while ((email = strsep(&emailsbuf, "|"))) {
		char *next = emailsbuf;
		if (check_mime(vmu->fullname)) {
			char *ptr;
			ast_str_encode_mime(&str2, 0, vmu->fullname, first_line ? strlen("To: ") : 0, strlen(email) + 3 + (next ? strlen(",") : 0));
			while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
				*ptr = '\0';
				fprintf(p, " %s" ENDL, ast_str_buffer(str2));
				/* Substring is smaller, so this will never grow */
				ast_str_set(&str2, 0, "%s", ptr + 1);
			}
			fprintf(p, " %s <%s>%s" ENDL, ast_str_buffer(str2), email, next ? "," : "");
		} else {
			fprintf(p, " %s <%s>%s" ENDL, ast_str_quote(&str2, 0, vmu->fullname), email, next ? "," : "");
		}
		first_line = 0;
	}

	if (msgnum <= -1) {
		fprintf(p, "Subject: New greeting '%s' on %s." ENDL, greeting_attachment, date);
	} else if (!ast_strlen_zero(emailsubject) || !ast_strlen_zero(vmu->emailsubject)) {
		char *e_subj = !ast_strlen_zero(vmu->emailsubject) ? vmu->emailsubject : emailsubject;
		struct ast_channel *ast;
		if ((ast = ast_dummy_channel_alloc())) {
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, category, flag);
			ast_str_substitute_variables(&str1, 0, ast, e_subj);
			if (check_mime(ast_str_buffer(str1))) {
				char *ptr;
				first_line = 1;
				ast_str_encode_mime(&str2, 0, ast_str_buffer(str1), strlen("Subject: "), 0);
				while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
					*ptr = '\0';
					fprintf(p, "%s %s" ENDL, first_line ? "Subject:" : "", ast_str_buffer(str2));
					first_line = 0;
					/* Substring is smaller, so this will never grow */
					ast_str_set(&str2, 0, "%s", ptr + 1);
				}
				fprintf(p, "%s %s" ENDL, first_line ? "Subject:" : "", ast_str_buffer(str2));
			} else {
				fprintf(p, "Subject: %s" ENDL, ast_str_buffer(str1));
			}
			ast = ast_channel_unref(ast);
		} else {
			ast_log(AST_LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else if (ast_test_flag((&globalflags), VM_PBXSKIP)) {
		if (ast_strlen_zero(flag)) {
			fprintf(p, "Subject: New message %d in mailbox %s" ENDL, msgnum + 1, mailbox);
		} else {
			fprintf(p, "Subject: New %s message %d in mailbox %s" ENDL, flag, msgnum + 1, mailbox);
		}
	} else {
		if (ast_strlen_zero(flag)) {
			fprintf(p, "Subject: [PBX]: New message %d in mailbox %s" ENDL, msgnum + 1, mailbox);
		} else {
			fprintf(p, "Subject: [PBX]: New %s message %d in mailbox %s" ENDL, flag, msgnum + 1, mailbox);
		}
	}

	fprintf(p, "Message-ID: <Asterisk-%d-%u-%s-%d@%s>" ENDL, msgnum + 1,
		(unsigned int) ast_random(), mailbox, (int) getpid(), host);
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
		/* flag added for Urgent */
		fprintf(p, "X-Asterisk-VM-Flag: %s" ENDL, S_OR(flag, ""));
		fprintf(p, "X-Asterisk-VM-Priority: %d" ENDL, chan ? ast_channel_priority(chan) : 0);
		fprintf(p, "X-Asterisk-VM-Caller-ID-Num: %s" ENDL, enc_cidnum);
		fprintf(p, "X-Asterisk-VM-Caller-ID-Name: %s" ENDL, enc_cidname);
		fprintf(p, "X-Asterisk-VM-Duration: %d" ENDL, duration);
		if (!ast_strlen_zero(category)) {
			fprintf(p, "X-Asterisk-VM-Category: %s" ENDL, category);
		} else {
			fprintf(p, "X-Asterisk-VM-Category: " ENDL);
		}
		fprintf(p, "X-Asterisk-VM-Message-Type: %s" ENDL, msgnum > -1 ? "Message" : greeting_attachment);
		fprintf(p, "X-Asterisk-VM-Orig-date: %s" ENDL, date);
		fprintf(p, "X-Asterisk-VM-Orig-time: %ld" ENDL, (long) time(NULL));
		fprintf(p, "X-Asterisk-VM-Message-ID: %s" ENDL, msg_id);
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
		snprintf(bound, sizeof(bound), "----voicemail_%d%s%d%u", msgnum + 1, mailbox,
			(int) getpid(), (unsigned int) ast_random());

		fprintf(p, "Content-Type: multipart/mixed; boundary=\"%s\"" ENDL, bound);
		fprintf(p, ENDL ENDL "This is a multi-part message in MIME format." ENDL ENDL);
		fprintf(p, "--%s" ENDL, bound);
	}
	fprintf(p, "Content-Type: text/plain; charset=%s" ENDL "Content-Transfer-Encoding: 8bit" ENDL ENDL, charset);
	if (msgnum <= -1) {
		fprintf(p, "This message is to let you know that your greeting '%s' was changed on %s." ENDL
				"Please do not delete this message, lest your greeting vanish with it." ENDL ENDL,
				greeting_attachment, date);
	} else if (emailbody || vmu->emailbody) {
		char* e_body = vmu->emailbody ? vmu->emailbody : emailbody;
		struct ast_channel *ast;
		if ((ast = ast_dummy_channel_alloc())) {
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, category, flag);
			ast_str_substitute_variables(&str1, 0, ast, e_body);
#ifdef IMAP_STORAGE
				{
					/* Convert body to native line terminators for IMAP backend */
					char *line = ast_str_buffer(str1), *next;
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
			fprintf(p, "%s" ENDL, ast_str_buffer(str1));
#endif
			ast = ast_channel_unref(ast);
		} else {
			ast_log(AST_LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else {
		if (strcmp(vmu->mailbox, mailbox)) {
			/* Forwarded type */
			struct ast_config *msg_cfg;
			const char *v;
			int inttime;
			char fromdir[256], fromfile[256], origdate[80] = "", origcallerid[80] = "";
			struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
			/* Retrieve info from VM attribute file */
			make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, fromfolder);
			make_file(fromfile, sizeof(fromfile), fromdir, msgnum);
			if (strlen(fromfile) < sizeof(fromfile) - 5) {
				strcat(fromfile, ".txt");
			}
			if ((msg_cfg = ast_config_load(fromfile, config_flags)) && valid_config(msg_cfg)) {
				if ((v = ast_variable_retrieve(msg_cfg, "message", "callerid"))) {
					ast_copy_string(origcallerid, v, sizeof(origcallerid));
				}

				/* You might be tempted to do origdate, except that a) it's in the wrong
				 * format, and b) it's missing for IMAP recordings. */
				if ((v = ast_variable_retrieve(msg_cfg, "message", "origtime")) && sscanf(v, "%30d", &inttime) == 1) {
					struct timeval tv = { inttime, };
					struct ast_tm tm;
					ast_localtime(&tv, &tm, NULL);
					ast_strftime_locale(origdate, sizeof(origdate), emaildateformat, &tm, S_OR(vmu->locale, NULL));
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

	if (imap || attach_user_voicemail) {
		if (!ast_strlen_zero(attach2)) {
			snprintf(filename, sizeof(filename), "msg%04d.%s", msgnum, format);
			ast_debug(5, "creating second attachment filename %s\n", filename);
			add_email_attachment(p, vmu, format, attach, greeting_attachment, mailbox, bound, filename, 0, msgnum);
			snprintf(filename, sizeof(filename), "msgintro%04d.%s", msgnum, format);
			ast_debug(5, "creating attachment filename %s\n", filename);
			add_email_attachment(p, vmu, format, attach2, greeting_attachment, mailbox, bound, filename, 1, msgnum);
		} else {
			snprintf(filename, sizeof(filename), "msg%04d.%s", msgnum, format);
			ast_debug(5, "creating attachment filename %s, no second attachment.\n", filename);
			add_email_attachment(p, vmu, format, attach, greeting_attachment, mailbox, bound, filename, 1, msgnum);
		}
	}
	ast_free(str1);
	ast_free(str2);
}

static int add_email_attachment(FILE *p, struct ast_vm_user *vmu, char *format, char *attach, char *greeting_attachment, char *mailbox, char *bound, char *filename, int last, int msgnum)
{
	char fname[PATH_MAX] = "";
	char sox_gain_tmpdir[PATH_MAX];
	char *file_to_delete = NULL, *dir_to_delete = NULL;
	int res;
	char altfname[PATH_MAX] = "";
	int altused = 0;
	char altformat[80] = "";
	char *c = NULL;

	/* Eww. We want formats to tell us their own MIME type */
	char *mime_type = (!strcasecmp(format, "ogg")) ? "application/" : "audio/x-";

	/* Users of multiple file formats need special attention. */
	snprintf(fname, sizeof(fname), "%s.%s", attach, format);
	if (!ast_file_is_readable(fname)) {
		ast_copy_string(altformat, vmfmts, sizeof(altformat));
		c = strchr(altformat, '|');
		if (c) {
			*c = '\0';
		}
		ast_log(AST_LOG_WARNING, "Failed to open file: %s: %s - trying first/alternate format %s\n", fname, strerror(errno), altformat);
		snprintf(altfname, sizeof(altfname), "%s.%s", attach, altformat);
		if (!ast_file_is_readable(altfname)) {
			ast_log(AST_LOG_WARNING, "Failed to open file: %s: %s - alternate format %s failure\n", altfname, strerror(errno), altformat);
		} else {
			altused = 1;
		}
	}

	/* This 'while' loop will only execute once. We use it so that we can 'break' */
	while (vmu->volgain < -.001 || vmu->volgain > .001 || altused) {
		char tmpdir[PATH_MAX];

		create_dirpath(tmpdir, sizeof(tmpdir), vmu->context, vmu->mailbox, "tmp");

		res = snprintf(sox_gain_tmpdir, sizeof(sox_gain_tmpdir), "%s/vm-gain-XXXXXX", tmpdir);
		if (res >= sizeof(sox_gain_tmpdir)) {
			ast_log(LOG_ERROR, "Failed to create temporary directory path %s: Out of buffer space\n", tmpdir);
			break;
		}

		if (mkdtemp(sox_gain_tmpdir)) {
			int soxstatus = 0;
			char sox_gain_cmd[PATH_MAX];

			ast_debug(3, "sox_gain_tmpdir: %s\n", sox_gain_tmpdir);

			/* Save for later */
			dir_to_delete = sox_gain_tmpdir;

			res = snprintf(fname, sizeof(fname), "%s/output.%s", sox_gain_tmpdir, format);
			if (res >= sizeof(fname)) {
				ast_log(LOG_ERROR, "Failed to create filename buffer for %s/output.%s: Too long\n", sox_gain_tmpdir, format);
				break;
			}

			if (!altused) {
				res = snprintf(sox_gain_cmd, sizeof(sox_gain_cmd), "sox -v %.4f %s.%s %s",
							   vmu->volgain, attach, format, fname);
			} else {
				if (!strcasecmp(format, "wav")) {
					if (vmu->volgain < -.001 || vmu->volgain > .001) {
						res = snprintf(sox_gain_cmd, sizeof(sox_gain_cmd), "sox -v %.4f %s.%s -e signed-integer -b 16 %s",
									   vmu->volgain, attach, altformat, fname);
					} else {
						res = snprintf(sox_gain_cmd, sizeof(sox_gain_cmd), "sox %s.%s -e signed-integer -b 16 %s",
									   attach, altformat, fname);
					}
				} else {
					if (vmu->volgain < -.001 || vmu->volgain > .001) {
						res = snprintf(sox_gain_cmd, sizeof(sox_gain_cmd), "sox -v %.4f %s.%s %s",
									   vmu->volgain, attach, altformat, fname);
					} else {
						res = snprintf(sox_gain_cmd, sizeof(sox_gain_cmd), "sox %s.%s %s",
									   attach, altformat, fname);
					}
				}
			}

			if (res >= sizeof(sox_gain_cmd)) {
				ast_log(LOG_ERROR, "Failed to generate sox command, out of buffer space\n");
				break;
			}

			soxstatus = ast_safe_system(sox_gain_cmd);
			if (!soxstatus) {
				/* Save for later */
				file_to_delete = fname;
				ast_debug(3, "VOLGAIN: Stored at: %s - Level: %.4f - Mailbox: %s\n", fname, vmu->volgain, mailbox);
			} else {
				ast_log(LOG_WARNING, "Sox failed to re-encode %s: %s (have you installed support for all sox file formats?)\n",
						fname,
						soxstatus == 1 ? "Problem with command line options" : "An error occurred during file processing");
				ast_log(LOG_WARNING, "Voicemail attachment will have no volume gain.\n");
			}
		}

		break;
	}

	if (!file_to_delete) {
		res = snprintf(fname, sizeof(fname), "%s.%s", attach, format);
		if (res >= sizeof(fname)) {
			ast_log(LOG_ERROR, "Failed to create filename buffer for %s.%s: Too long\n", attach, format);
			return -1;
		}
	}

	fprintf(p, "--%s" ENDL, bound);
	if (msgnum > -1)
		fprintf(p, "Content-Type: %s%s; name=\"%s\"" ENDL, mime_type, format, filename);
	else
		fprintf(p, "Content-Type: %s%s; name=\"%s.%s\"" ENDL, mime_type, format, greeting_attachment, format);
	fprintf(p, "Content-Transfer-Encoding: base64" ENDL);
	fprintf(p, "Content-Description: Voicemail sound attachment." ENDL);
	if (msgnum > -1)
		fprintf(p, "Content-Disposition: attachment; filename=\"%s\"" ENDL ENDL, filename);
	else
		fprintf(p, "Content-Disposition: attachment; filename=\"%s.%s\"" ENDL ENDL, greeting_attachment, format);
	ast_base64_encode_file_path(fname, p, ENDL);
	if (last)
		fprintf(p, ENDL ENDL "--%s--" ENDL "." ENDL, bound);

	if (file_to_delete) {
		unlink(file_to_delete);
	}

	if (dir_to_delete) {
		rmdir(dir_to_delete);
	}

	return 0;
}

static int sendmail(char *srcemail,
		struct ast_vm_user *vmu,
		int msgnum,
		char *context,
		char *mailbox,
		const char *fromfolder,
		char *cidnum,
		char *cidname,
		char *attach,
		char *attach2,
		char *format,
		int duration,
		int attach_user_voicemail,
		struct ast_channel *chan,
		const char *category,
		const char *flag,
		const char *msg_id)
{
	FILE *p = NULL;
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[256];
	char *stringp;

	if (vmu && ast_strlen_zero(vmu->email)) {
		ast_log(AST_LOG_WARNING, "E-mail address missing for mailbox [%s].  E-mail will not be sent.\n", vmu->mailbox);
		return(0);
	}

	/* Mail only the first format */
	format = ast_strdupa(format);
	stringp = format;
	strsep(&stringp, "|");

	if (!strcmp(format, "wav49"))
		format = "WAV";
	ast_debug(3, "Attaching file '%s', format '%s', uservm is '%d', global is %u\n", attach, format, attach_user_voicemail, ast_test_flag((&globalflags), VM_ATTACH));
	/* Make a temporary file instead of piping directly to sendmail, in case the mail
	   command hangs */
	if ((p = ast_file_mkftemp(tmp, VOICEMAIL_FILE_MODE & ~my_umask)) == NULL) {
		ast_log(AST_LOG_WARNING, "Unable to launch '%s' (can't create temporary file)\n", mailcmd);
		return -1;
	} else {
		make_email_file(p, srcemail, vmu, msgnum, context, mailbox, fromfolder, cidnum, cidname, attach, attach2, format, duration, attach_user_voicemail, chan, category, 0, flag, msg_id);
		fclose(p);
		snprintf(tmp2, sizeof(tmp2), "( %s < %s ; rm -f %s ) &", mailcmd, tmp, tmp);
		ast_safe_system(tmp2);
		ast_debug(1, "Sent mail to %s with command '%s'\n", vmu->email, mailcmd);
	}
	return 0;
}

static int sendpage(char *srcemail, char *pager, int msgnum, char *context, char *mailbox, const char *fromfolder, char *cidnum, char *cidname, int duration, struct ast_vm_user *vmu, const char *category, const char *flag)
{
	char enc_cidnum[256], enc_cidname[256];
	char date[256];
	char host[MAXHOSTNAMELEN] = "";
	char who[256];
	char dur[PATH_MAX];
	char tmp[80] = "/tmp/astmail-XXXXXX";
	char tmp2[PATH_MAX];
	struct ast_tm tm;
	FILE *p;
	struct ast_str *str1 = ast_str_create(16), *str2 = ast_str_create(16);

	if (!str1 || !str2) {
		ast_free(str1);
		ast_free(str2);
		return -1;
	}

	if (cidnum) {
		strip_control_and_high(cidnum, enc_cidnum, sizeof(enc_cidnum));
	}
	if (cidname) {
		strip_control_and_high(cidname, enc_cidname, sizeof(enc_cidname));
	}

	if ((p = ast_file_mkftemp(tmp, VOICEMAIL_FILE_MODE & ~my_umask)) == NULL) {
		ast_log(AST_LOG_WARNING, "Unable to launch '%s' (can't create temporary file)\n", mailcmd);
		ast_free(str1);
		ast_free(str2);
		return -1;
	}
	gethostname(host, sizeof(host)-1);
	if (strchr(srcemail, '@')) {
		ast_copy_string(who, srcemail, sizeof(who));
	} else {
		snprintf(who, sizeof(who), "%s@%s", srcemail, host);
	}
	snprintf(dur, sizeof(dur), "%d:%02d", duration / 60, duration % 60);
	ast_strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", vmu_tm(vmu, &tm));
	fprintf(p, "Date: %s\n", date);

	/* Reformat for custom pager format */
	ast_strftime_locale(date, sizeof(date), pagerdateformat, vmu_tm(vmu, &tm), S_OR(vmu->locale, NULL));

	if (!ast_strlen_zero(pagerfromstring)) {
		struct ast_channel *ast;
		if ((ast = ast_dummy_channel_alloc())) {
			char *ptr;
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, enc_cidnum, enc_cidname, dur, date, category, flag);
			ast_str_substitute_variables(&str1, 0, ast, pagerfromstring);

			if (check_mime(ast_str_buffer(str1))) {
				int first_line = 1;
				ast_str_encode_mime(&str2, 0, ast_str_buffer(str1), strlen("From: "), strlen(who) + 3);
				while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
					*ptr = '\0';
					fprintf(p, "%s %s" ENDL, first_line ? "From:" : "", ast_str_buffer(str2));
					first_line = 0;
					/* Substring is smaller, so this will never grow */
					ast_str_set(&str2, 0, "%s", ptr + 1);
				}
				fprintf(p, "%s %s <%s>" ENDL, first_line ? "From:" : "", ast_str_buffer(str2), who);
			} else {
				fprintf(p, "From: %s <%s>" ENDL, ast_str_quote(&str2, 0, ast_str_buffer(str1)), who);
			}
			ast = ast_channel_unref(ast);
		} else {
			ast_log(AST_LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else {
		fprintf(p, "From: Asterisk PBX <%s>" ENDL, who);
	}

	if (check_mime(vmu->fullname)) {
		int first_line = 1;
		char *ptr;
		ast_str_encode_mime(&str2, 0, vmu->fullname, strlen("To: "), strlen(pager) + 3);
		while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
			*ptr = '\0';
			fprintf(p, "%s %s" ENDL, first_line ? "To:" : "", ast_str_buffer(str2));
			first_line = 0;
			/* Substring is smaller, so this will never grow */
			ast_str_set(&str2, 0, "%s", ptr + 1);
		}
		fprintf(p, "%s %s <%s>" ENDL, first_line ? "To:" : "", ast_str_buffer(str2), pager);
	} else {
		fprintf(p, "To: %s <%s>" ENDL, ast_str_quote(&str2, 0, vmu->fullname), pager);
	}

	if (!ast_strlen_zero(pagersubject)) {
		struct ast_channel *ast;
		if ((ast = ast_dummy_channel_alloc())) {
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, category, flag);
			ast_str_substitute_variables(&str1, 0, ast, pagersubject);
			if (check_mime(ast_str_buffer(str1))) {
				int first_line = 1;
				char *ptr;
				ast_str_encode_mime(&str2, 0, ast_str_buffer(str1), strlen("Subject: "), 0);
				while ((ptr = strchr(ast_str_buffer(str2), ' '))) {
					*ptr = '\0';
					fprintf(p, "%s %s" ENDL, first_line ? "Subject:" : "", ast_str_buffer(str2));
					first_line = 0;
					/* Substring is smaller, so this will never grow */
					ast_str_set(&str2, 0, "%s", ptr + 1);
				}
				fprintf(p, "%s %s" ENDL, first_line ? "Subject:" : "", ast_str_buffer(str2));
			} else {
				fprintf(p, "Subject: %s" ENDL, ast_str_buffer(str1));
			}
			ast = ast_channel_unref(ast);
		} else {
			ast_log(AST_LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else {
		if (ast_strlen_zero(flag)) {
			fprintf(p, "Subject: New VM\n\n");
		} else {
			fprintf(p, "Subject: New %s VM\n\n", flag);
		}
	}

	if (pagerbody) {
		struct ast_channel *ast;
		if ((ast = ast_dummy_channel_alloc())) {
			prep_email_sub_vars(ast, vmu, msgnum + 1, context, mailbox, fromfolder, cidnum, cidname, dur, date, category, flag);
			ast_str_substitute_variables(&str1, 0, ast, pagerbody);
			fprintf(p, "%s" ENDL, ast_str_buffer(str1));
			ast = ast_channel_unref(ast);
		} else {
			ast_log(AST_LOG_WARNING, "Cannot allocate the channel for variables substitution\n");
		}
	} else {
		fprintf(p, "New %s long %s msg in box %s\n"
				"from %s, on %s", dur, flag, mailbox, (cidname ? cidname : (cidnum ? cidnum : "unknown")), date);
	}

	fclose(p);
	snprintf(tmp2, sizeof(tmp2), "( %s < %s ; rm -f %s ) &", mailcmd, tmp, tmp);
	ast_safe_system(tmp2);
	ast_debug(1, "Sent page to %s with command '%s'\n", pager, mailcmd);
	ast_free(str1);
	ast_free(str2);
	return 0;
}

/*!
 * \brief Gets the current date and time, as formatted string.
 * \param s The buffer to hold the output formatted date.
 * \param len the length of the buffer. Used to prevent buffer overflow in ast_strftime.
 *
 * The date format string used is "%a %b %e %r UTC %Y".
 *
 * \return zero on success, -1 on error.
 */
static int get_date(char *s, int len)
{
	struct ast_tm tm;
	struct timeval t = ast_tvnow();

	ast_localtime(&t, &tm, "UTC");

	return ast_strftime(s, len, "%a %b %e %r UTC %Y", &tm);
}

static int invent_message(struct ast_channel *chan, char *context, char *ext, int busy, char *ecodes)
{
	int res;
	char fn[PATH_MAX];
	char dest[PATH_MAX];

	snprintf(fn, sizeof(fn), "%s%s/%s/greet", VM_SPOOL_DIR, context, ext);

	if ((res = create_dirpath(dest, sizeof(dest), context, ext, ""))) {
		ast_log(AST_LOG_WARNING, "Failed to make directory(%s)\n", fn);
		return -1;
	}

	RETRIEVE(fn, -1, ext, context);
	if (ast_fileexists(fn, NULL, NULL) > 0) {
		res = ast_stream_and_wait(chan, fn, ecodes);
		if (res) {
			DISPOSE(fn, -1);
			return res;
		}
	} else {
		/* Dispose just in case */
		DISPOSE(fn, -1);
		res = ast_stream_and_wait(chan, "vm-theperson", ecodes);
		if (res)
			return res;
		res = ast_say_digit_str(chan, ext, ecodes, ast_channel_language(chan));
		if (res)
			return res;
	}
	res = ast_stream_and_wait(chan, busy ? "vm-isonphone" : "vm-isunavail", ecodes);
	return res;
}

static void free_zone(struct vm_zone *z)
{
	ast_free(z);
}

#ifdef ODBC_STORAGE

static int count_messages_in_folder(struct odbc_obj *odbc, const char *context, const char *mailbox, const char *folder, int *messages)
{
	int res;
	char sql[PATH_MAX];
	char rowdata[20];
	SQLHSTMT stmt = NULL;
	struct generic_prepare_struct gps = { .sql = sql, .argc = 0 };

	if (!messages) {
		return 0;
	}

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir = '%s%s/%s/%s'", odbc_table, VM_SPOOL_DIR, context, mailbox, folder);
	if (!(stmt = ast_odbc_prepare_and_execute(odbc, generic_prepare, &gps))) {
		ast_log(LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		return 1;
	}
	res = SQLFetch(stmt);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return 1;
	}
	res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
		SQLFreeHandle(SQL_HANDLE_STMT, stmt);
		return 1;
	}

	*messages = atoi(rowdata);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

	return 0;
}

static int inboxcount2(const char *mailbox, int *urgentmsgs, int *newmsgs, int *oldmsgs)
{
	char tmp[PATH_MAX] = "";
	struct odbc_obj *obj;
	char *context;

	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	if (urgentmsgs)
		*urgentmsgs = 0;

	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;

	ast_copy_string(tmp, mailbox, sizeof(tmp));

	if (strchr(mailbox, ' ') || strchr(mailbox, ',')) {
		int u, n, o;
		char *next, *remaining = tmp;
		while ((next = strsep(&remaining, " ,"))) {
			if (inboxcount2(next, urgentmsgs ? &u : NULL, &n, &o)) {
				return -1;
			}
			if (urgentmsgs) {
				*urgentmsgs += u;
			}
			if (newmsgs) {
				*newmsgs += n;
			}
			if (oldmsgs) {
				*oldmsgs += o;
			}
		}
		return 0;
	}

	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	} else
		context = "default";

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return -1;
	}

	if (count_messages_in_folder(obj, context, tmp, "INBOX", newmsgs)
	   || count_messages_in_folder(obj, context, tmp, "Old", oldmsgs)
	   || count_messages_in_folder(obj, context, tmp, "Urgent", urgentmsgs)) {
		ast_log(AST_LOG_WARNING, "Failed to obtain message count for mailbox %s@%s\n",
				tmp, context);
	}

	ast_odbc_release_obj(obj);
	return 0;
}

/*!
 * \brief Gets the number of messages that exist in a mailbox folder.
 * \param mailbox_id
 * \param folder
 *
 * This method is used when ODBC backend is used.
 * \return The number of messages in this mailbox folder (zero or more).
 */
static int messagecount(const char *mailbox_id, const char *folder)
{
	struct odbc_obj *obj = NULL;
	char *context;
	char *mailbox;
	int nummsgs = 0;
	int res;
	SQLHSTMT stmt = NULL;
	char sql[PATH_MAX];
	char rowdata[20];
	struct generic_prepare_struct gps = { .sql = sql, .argc = 0 };

	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox_id)
		|| separate_mailbox(ast_strdupa(mailbox_id), &mailbox, &context)) {
		return 0;
	}

	if (ast_strlen_zero(folder)) {
		folder = "INBOX";
	}

	obj = ast_odbc_request_obj(odbc_database, 0);
	if (!obj) {
		ast_log(AST_LOG_WARNING, "Failed to obtain database object for '%s'!\n", odbc_database);
		return 0;
	}

	if (!strcmp(folder, "INBOX")) {
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir = '%s%s/%s/INBOX' OR dir = '%s%s/%s/Urgent'", odbc_table, VM_SPOOL_DIR, context, mailbox, VM_SPOOL_DIR, context, mailbox);
	} else {
		snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE dir = '%s%s/%s/%s'", odbc_table, VM_SPOOL_DIR, context, mailbox, folder);
	}

	stmt = ast_odbc_prepare_and_execute(obj, generic_prepare, &gps);
	if (!stmt) {
		ast_log(AST_LOG_WARNING, "SQL Execute error!\n[%s]\n\n", sql);
		goto bail;
	}
	res = SQLFetch(stmt);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Fetch error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}
	res = SQLGetData(stmt, 1, SQL_CHAR, rowdata, sizeof(rowdata), NULL);
	if (!SQL_SUCCEEDED(res)) {
		ast_log(AST_LOG_WARNING, "SQL Get Data error!\n[%s]\n\n", sql);
		goto bail_with_handle;
	}
	nummsgs = atoi(rowdata);

bail_with_handle:
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);

bail:
	ast_odbc_release_obj(obj);
	return nummsgs;
}

/*!
 * \brief Determines if the given folder has messages.
 * \param mailbox The \@ delimited string for user\@context. If no context is found, uses 'default' for the context.
 *
 * This function is used when the mailbox is stored in an ODBC back end.
 * This invokes the messagecount(). Here we are interested in the presence of messages (> 0) only, not the actual count.
 * \return 1 if the folder has one or more messages. zero otherwise.
 */
static int has_voicemail(const char *mailboxes, const char *folder)
{
	char *parse;
	char *mailbox;

	parse = ast_strdupa(mailboxes);
	while ((mailbox = strsep(&parse, ",&"))) {
		if (messagecount(mailbox, folder)) {
			return 1;
		}
	}
	return 0;
}
#endif
#ifndef IMAP_STORAGE
/*!
 * \brief Copies a message from one mailbox to another.
 * \param chan
 * \param vmu
 * \param imbox
 * \param msgnum
 * \param duration
 * \param recip
 * \param fmt
 * \param dir
 * \param flag, dest_folder
 *
 * This is only used by file storage based mailboxes.
 *
 * \return zero on success, -1 on error.
 */
static int copy_message(struct ast_channel *chan, struct ast_vm_user *vmu, int imbox, int msgnum, long duration, struct ast_vm_user *recip, char *fmt, char *dir, const char *flag, const char *dest_folder)
{
	char fromdir[PATH_MAX], todir[PATH_MAX], frompath[PATH_MAX], topath[PATH_MAX];
	const char *frombox = mbox(vmu, imbox);
	const char *userfolder;
	int recipmsgnum;
	int res = 0;

	ast_log(AST_LOG_NOTICE, "Copying message from %s@%s to %s@%s\n", vmu->mailbox, vmu->context, recip->mailbox, recip->context);

	if (!ast_strlen_zero(flag) && !strcmp(flag, "Urgent")) { /* If urgent, copy to Urgent folder */
		userfolder = "Urgent";
	} else if (!ast_strlen_zero(dest_folder)) {
		userfolder = dest_folder;
	} else {
		userfolder = "INBOX";
	}

	create_dirpath(todir, sizeof(todir), recip->context, recip->mailbox, userfolder);

	if (!dir)
		make_dir(fromdir, sizeof(fromdir), vmu->context, vmu->mailbox, frombox);
	else
		ast_copy_string(fromdir, dir, sizeof(fromdir));

	make_file(frompath, sizeof(frompath), fromdir, msgnum);
	make_dir(todir, sizeof(todir), recip->context, recip->mailbox, userfolder);

	if (vm_lock_path(todir))
		return ERROR_LOCK_PATH;

	recipmsgnum = last_message_index(recip, todir) + 1;
	if (recipmsgnum < recip->maxmsg - (imbox ? 0 : inprocess_count(vmu->mailbox, vmu->context, 0))) {
		make_file(topath, sizeof(topath), todir, recipmsgnum);
#ifndef ODBC_STORAGE
		if (EXISTS(fromdir, msgnum, frompath, chan ? ast_channel_language(chan) : "")) {
			COPY(fromdir, msgnum, todir, recipmsgnum, recip->mailbox, recip->context, frompath, topath);
		} else {
#endif
			/* If we are prepending a message for ODBC, then the message already
			 * exists in the database, but we want to force copying from the
			 * filesystem (since only the FS contains the prepend). */
			copy_plain_file(frompath, topath);
			STORE(todir, recip->mailbox, recip->context, recipmsgnum, chan, recip, fmt, duration, NULL, NULL, NULL);
			vm_delete(topath);
#ifndef ODBC_STORAGE
		}
#endif
	} else {
		ast_log(AST_LOG_ERROR, "Recipient mailbox %s@%s is full\n", recip->mailbox, recip->context);
		res = -1;
	}
	ast_unlock_path(todir);
	if (chan) {
		struct ast_party_caller *caller = ast_channel_caller(chan);
		notify_new_message(chan, recip, NULL, recipmsgnum, duration, fmt,
			S_COR(caller->id.number.valid, caller->id.number.str, NULL),
			S_COR(caller->id.name.valid, caller->id.name.str, NULL),
			flag);
	}

	return res;
}
#endif
#if !(defined(IMAP_STORAGE) || defined(ODBC_STORAGE))

static int messagecount(const char *mailbox_id, const char *folder)
{
	char *context;
	char *mailbox;

	if (ast_strlen_zero(mailbox_id)
		|| separate_mailbox(ast_strdupa(mailbox_id), &mailbox, &context)) {
		return 0;
	}

	return __has_voicemail(context, mailbox, folder, 0) + (folder && strcmp(folder, "INBOX") ? 0 : __has_voicemail(context, mailbox, "Urgent", 0));
}

static int __has_voicemail(const char *context, const char *mailbox, const char *folder, int shortcircuit)
{
	DIR *dir;
	struct dirent *de;
	char fn[256];
	int ret = 0;
	struct alias_mailbox_mapping *mapping;
	char *c;
	char *m;

	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;

	if (ast_strlen_zero(folder))
		folder = "INBOX";
	if (ast_strlen_zero(context))
		context = "default";

	c = (char *)context;
	m = (char *)mailbox;

	if (!ast_strlen_zero(aliasescontext)) {
		char tmp[MAX_VM_MAILBOX_LEN];

		snprintf(tmp, MAX_VM_MAILBOX_LEN, "%s@%s", mailbox, context);
		mapping = ao2_find(alias_mailbox_mappings, tmp, OBJ_SEARCH_KEY);
		if (mapping) {
			separate_mailbox(ast_strdupa(mapping->mailbox), &m, &c);
			ao2_ref(mapping, -1);
		}
	}

	snprintf(fn, sizeof(fn), "%s%s/%s/%s", VM_SPOOL_DIR, c, m, folder);

	if (!(dir = opendir(fn)))
		return 0;

	while ((de = readdir(dir))) {
		if (!strncasecmp(de->d_name, "msg", 3)) {
			if (shortcircuit) {
				ret = 1;
				break;
			} else if (!strncasecmp(de->d_name + 8, "txt", 3)) {
				ret++;
			}
		}
	}

	closedir(dir);

	return ret;
}

/*!
 * \brief Determines if the given folder has messages.
 * \param mailbox The \@ delimited string for user\@context. If no context is found, uses 'default' for the context.
 * \param folder the folder to look in
 *
 * This function is used when the mailbox is stored in a filesystem back end.
 * This invokes the __has_voicemail(). Here we are interested in the presence of messages (> 0) only, not the actual count.
 * \return 1 if the folder has one or more messages. zero otherwise.
 */
static int has_voicemail(const char *mailbox, const char *folder)
{
	char tmp[256], *tmp2 = tmp, *box, *context;
	ast_copy_string(tmp, mailbox, sizeof(tmp));
	if (ast_strlen_zero(folder)) {
		folder = "INBOX";
	}
	while ((box = strsep(&tmp2, ",&"))) {
		if ((context = strchr(box, '@')))
			*context++ = '\0';
		else
			context = "default";
		if (__has_voicemail(context, box, folder, 1))
			return 1;
		/* If we are checking INBOX, we should check Urgent as well */
		if (!strcmp(folder, "INBOX") && __has_voicemail(context, box, "Urgent", 1)) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \brief Check the given mailbox's message count.
 * \param mailbox The \@ delimited string for user\@context. If no context is found, uses 'default' for the context.
 * \param urgentmsgs  urgent message count.
 * \param newmsgs new message count.
 * \param oldmsgs old message count pointer
 * \return -1 if error occurred, 0 otherwise.
 */
static int inboxcount2(const char *mailbox, int *urgentmsgs, int *newmsgs, int *oldmsgs)
{
	char tmp[256];
	char *context;

	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox)) {
		return 0;
	}

	if (newmsgs) {
		*newmsgs = 0;
	}
	if (oldmsgs) {
		*oldmsgs = 0;
	}
	if (urgentmsgs) {
		*urgentmsgs = 0;
	}

	if (strchr(mailbox, ',')) {
		int tmpnew, tmpold, tmpurgent;
		char *mb, *cur;

		ast_copy_string(tmp, mailbox, sizeof(tmp));
		mb = tmp;
		while ((cur = strsep(&mb, ", "))) {
			if (!ast_strlen_zero(cur)) {
				if (inboxcount2(cur, urgentmsgs ? &tmpurgent : NULL, newmsgs ? &tmpnew : NULL, oldmsgs ? &tmpold : NULL)) {
					return -1;
				} else {
					if (newmsgs) {
						*newmsgs += tmpnew;
					}
					if (oldmsgs) {
						*oldmsgs += tmpold;
					}
					if (urgentmsgs) {
						*urgentmsgs += tmpurgent;
					}
				}
			}
		}
		return 0;
	}

	ast_copy_string(tmp, mailbox, sizeof(tmp));

	if ((context = strchr(tmp, '@'))) {
		*context++ = '\0';
	} else {
		context = "default";
	}

	if (newmsgs) {
		*newmsgs = __has_voicemail(context, tmp, "INBOX", 0);
	}
	if (oldmsgs) {
		*oldmsgs = __has_voicemail(context, tmp, "Old", 0);
	}
	if (urgentmsgs) {
		*urgentmsgs = __has_voicemail(context, tmp, "Urgent", 0);
	}

	return 0;
}

#endif

/* Exactly the same function for file-based, ODBC-based, and IMAP-based, so why create 3 different copies? */
static int inboxcount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	int urgentmsgs = 0;
	int res = inboxcount2(mailbox, &urgentmsgs, newmsgs, oldmsgs);
	if (newmsgs) {
		*newmsgs += urgentmsgs;
	}
	return res;
}

static void run_externnotify(const char *context, const char *extension, const char *flag)
{
	char arguments[255];
	char ext_context[256] = "";
	int newvoicemails = 0, oldvoicemails = 0, urgentvoicemails = 0;
	struct ast_smdi_mwi_message *mwi_msg;

	if (!ast_strlen_zero(context))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", extension, context);
	else
		ast_copy_string(ext_context, extension, sizeof(ext_context));

	if (smdi_iface) {
		if (ast_app_has_voicemail(ext_context, NULL))
			ast_smdi_mwi_set(smdi_iface, extension);
		else
			ast_smdi_mwi_unset(smdi_iface, extension);

		if ((mwi_msg = ast_smdi_mwi_message_wait_station(smdi_iface, SMDI_MWI_WAIT_TIMEOUT, extension))) {
			ast_log(AST_LOG_ERROR, "Error executing SMDI MWI change for %s\n", extension);
			if (!strncmp(mwi_msg->cause, "INV", 3))
				ast_log(AST_LOG_ERROR, "Invalid MWI extension: %s\n", mwi_msg->fwd_st);
			else if (!strncmp(mwi_msg->cause, "BLK", 3))
				ast_log(AST_LOG_WARNING, "MWI light was already on or off for %s\n", mwi_msg->fwd_st);
			ast_log(AST_LOG_WARNING, "The switch reported '%s'\n", mwi_msg->cause);
			ao2_ref(mwi_msg, -1);
		} else {
			ast_debug(1, "Successfully executed SMDI MWI change for %s\n", extension);
		}
	}

	if (!ast_strlen_zero(externnotify)) {
		if (inboxcount2(ext_context, &urgentvoicemails, &newvoicemails, &oldvoicemails)) {
			ast_log(AST_LOG_ERROR, "Problem in calculating number of voicemail messages available for extension %s\n", extension);
		} else {
			snprintf(arguments, sizeof(arguments), "%s %s %s %d %d %d &",
				externnotify, S_OR(context, "\"\""),
				extension, newvoicemails,
				oldvoicemails, urgentvoicemails);
			ast_debug(1, "Executing %s\n", arguments);
			ast_safe_system(arguments);
		}
	}
}

/*!
 * \brief Variables used for saving a voicemail.
 *
 * This includes the record gain, mode flags, and the exit context of the chanel that was used for leaving the voicemail.
 */
struct leave_vm_options {
	unsigned int flags;
	signed char record_gain;
	char *exitcontext;
	char *beeptone;
};

static void generate_msg_id(char *dst)
{
	/* msg id is time of msg_id generation plus an incrementing value
	 * called each time a new msg_id is generated. This should achieve uniqueness,
	 * but only in single system solutions.
	 */
	unsigned int unique_counter = ast_atomic_fetchadd_int(&msg_id_incrementor, +1);
	snprintf(dst, MSG_ID_LEN, "%ld-%08x", (long) time(NULL), unique_counter);
}

/*!
 * \internal
 * \brief Creates a voicemail based on a specified file to a mailbox.
 * \param recdata A vm_recording_data containing filename and voicemail txt info.
 * \retval -1 failure
 * \retval 0 success
 *
 * This is installed to the app.h voicemail functions and accommodates all voicemail
 * storage methods. It should probably be broken out along with leave_voicemail at
 * some point in the future.
 *
 * This function currently only works for a single recipient and only uses the format
 * specified in recording_ext.
 */
static int msg_create_from_file(struct ast_vm_recording_data *recdata)
{
	/* voicemail recipient structure */
	struct ast_vm_user *recipient; /* points to svm once it's been created */
	struct ast_vm_user svm; /* struct storing the voicemail recipient */

	/* File paths */
	char tmpdir[PATH_MAX]; /* directory temp files are stored in */
	char tmptxtfile[PATH_MAX]; /* tmp file for voicemail txt file */
	char desttxtfile[PATH_MAX]; /* final destination for txt file */
	char tmpaudiofile[PATH_MAX]; /* tmp file where audio is stored */
	char dir[PATH_MAX]; /* destination for tmp files on completion */
	char destination[PATH_MAX]; /* destination with msgXXXX.  Basically <dir>/msgXXXX */

	/* stuff that only seems to be needed for IMAP */
	#ifdef IMAP_STORAGE
	struct vm_state *vms = NULL;
	char ext_context[256] = "";
	int newmsgs = 0;
	int oldmsgs = 0;
	#endif

	/* miscellaneous operational variables */
	int res = 0; /* Used to store error codes from functions */
	int txtdes /* File descriptor for the text file used to write the voicemail info */;
	FILE *txt; /* FILE pointer to text file used to write the voicemail info */
	char date[256]; /* string used to hold date of the voicemail (only used for ODBC) */
	int msgnum; /* the 4 digit number designated to the voicemail */
	int duration = 0; /* Length of the audio being recorded in seconds */
	struct ast_filestream *recording_fs; /*used to read the recording to get duration data */

	/* We aren't currently doing anything with category, since it comes from a channel variable and
	 * this function doesn't use channels, but this function could add that as an argument later. */
	const char *category = NULL; /* pointless for now */
	char msg_id[MSG_ID_LEN];

	/* Start by checking to see if the file actually exists... */
	if (!(ast_fileexists(recdata->recording_file, recdata->recording_ext, NULL))) {
		ast_log(LOG_ERROR, "File: %s not found.\n", recdata->recording_file);
		return -1;
	}

	memset(&svm, 0, sizeof(svm));
	if (!(recipient = find_user(&svm, recdata->context, recdata->mailbox))) {
		ast_log(LOG_ERROR, "No entry in voicemail config file for '%s@%s'\n", recdata->mailbox, recdata->context);
		return -1;
	}

	/* determine duration in seconds */
	if ((recording_fs = ast_readfile(recdata->recording_file, recdata->recording_ext, NULL, 0, 0, VOICEMAIL_DIR_MODE))) {
		if (!ast_seekstream(recording_fs, 0, SEEK_END)) {
			long framelength = ast_tellstream(recording_fs);
			int sample_rate = ast_ratestream(recording_fs);
			if (sample_rate) {
				duration = (int) (framelength / sample_rate);
			} else {
				ast_log(LOG_ERROR,"Unable to determine sample rate of recording %s\n", recdata->recording_file);
			}
		}
		ast_closestream(recording_fs);
	}

	/* If the duration was below the minimum duration for the user, let's just drop the whole thing now */
	if (duration < recipient->minsecs) {
		ast_log(LOG_NOTICE, "Copying recording to voicemail %s@%s skipped because duration was shorter than "
					"minmessage of recipient\n", recdata->mailbox, recdata->context);
		return -1;
	}

	/* Note that this number must be dropped back to a net sum of zero before returning from this function */

	if ((res = create_dirpath(tmpdir, sizeof(tmpdir), recipient->context, recdata->mailbox, "tmp"))) {
		ast_log(LOG_ERROR, "Failed to make directory.\n");
	}

	snprintf(tmptxtfile, sizeof(tmptxtfile), "%s/XXXXXX", tmpdir);
	txtdes = mkstemp(tmptxtfile);
	if (txtdes < 0) {
		chmod(tmptxtfile, VOICEMAIL_FILE_MODE & ~my_umask);
		/* Something screwed up.  Abort. */
		ast_log(AST_LOG_ERROR, "Unable to create message file: %s\n", strerror(errno));
		free_user(recipient);
		return -1;
	}

	/* Store information */
	txt = fdopen(txtdes, "w+");
	if (txt) {
		generate_msg_id(msg_id);
		get_date(date, sizeof(date));
		fprintf(txt,
			";\n"
			"; Message Information file\n"
			";\n"
			"[message]\n"
			"origmailbox=%s\n"
			"context=%s\n"
			"exten=%s\n"
			"rdnis=Unknown\n"
			"priority=%d\n"
			"callerchan=%s\n"
			"callerid=%s\n"
			"origdate=%s\n"
			"origtime=%ld\n"
			"category=%s\n"
			"msg_id=%s\n"
			"flag=\n" /* flags not supported in copy from file yet */
			"duration=%d\n", /* Don't have any reliable way to get duration of file. */

			recdata->mailbox,
			S_OR(recdata->call_context, ""),
			S_OR(recdata->call_extension, ""),
			recdata->call_priority,
			S_OR(recdata->call_callerchan, "Unknown"),
			S_OR(recdata->call_callerid, "Unknown"),
			date, (long) time(NULL),
			S_OR(category, ""),
			msg_id,
			duration);

		/* Since we are recording from a file, we shouldn't need to do anything else with
		 * this txt file */
		fclose(txt);

	} else {
		ast_log(LOG_WARNING, "Error opening text file for output\n");
		if (ast_check_realtime("voicemail_data")) {
			ast_destroy_realtime("voicemail_data", "filename", tmptxtfile, SENTINEL);
		}
		free_user(recipient);
		return -1;
	}

	/* At this point, the actual creation of a voicemail message should be finished.
	 * Now we just need to copy the files being recorded into the receiving folder. */

	create_dirpath(dir, sizeof(dir), recipient->context, recipient->mailbox, recdata->folder);

#ifdef IMAP_STORAGE
	/* make recipient info into an inboxcount friendly string */
	snprintf(ext_context, sizeof(ext_context), "%s@%s", recipient->mailbox, recipient->context);

	/* Is ext a mailbox? */
	/* must open stream for this user to get info! */
	res = inboxcount(ext_context, &newmsgs, &oldmsgs);
	if (res < 0) {
		ast_log(LOG_NOTICE, "Can not leave voicemail, unable to count messages\n");
		free_user(recipient);
		unlink(tmptxtfile);
		return -1;
	}
	if (!(vms = get_vm_state_by_mailbox(recipient->mailbox, recipient->context, 0))) {
	/* It is possible under certain circumstances that inboxcount did not
	 * create a vm_state when it was needed. This is a catchall which will
	 * rarely be used.
	 */
		if (!(vms = create_vm_state_from_user(recipient))) {
			ast_log(LOG_ERROR, "Couldn't allocate necessary space\n");
			free_user(recipient);
			unlink(tmptxtfile);
			return -1;
		}
	}
	vms->newmessages++;

	/* here is a big difference! We add one to it later */
	msgnum = newmsgs + oldmsgs;
	ast_debug(3, "Messagecount set to %d\n", msgnum);
	snprintf(destination, sizeof(destination), "%simap/msg%s%04d", VM_SPOOL_DIR, recipient->mailbox, msgnum);

	/* Check to see if we have enough room in the mailbox. If not, spit out an error and end
	 * Note that imap_check_limits raises inprocess_count if successful */
	if ((res = imap_check_limits(NULL, vms, recipient, msgnum))) {
		ast_log(LOG_NOTICE, "Didn't copy to voicemail. Mailbox for %s@%s is full.\n", recipient->mailbox, recipient->context);
		inprocess_count(recipient->mailbox, recipient->context, -1);
		free_user(recipient);
		unlink(tmptxtfile);
		return -1;
	}

#else

	/* Check to see if the mailbox is full for ODBC/File storage */
	ast_debug(3, "mailbox = %d : inprocess = %d\n", count_messages(recipient, dir),
		inprocess_count(recipient->mailbox, recipient->context, 0));
	if (count_messages(recipient, dir) > recipient->maxmsg - inprocess_count(recipient->mailbox, recipient->context, +1)) {
		ast_log(AST_LOG_WARNING, "Didn't copy to voicemail. Mailbox for %s@%s is full.\n", recipient->mailbox, recipient->context);
		inprocess_count(recipient->mailbox, recipient->context, -1);
		free_user(recipient);
		unlink(tmptxtfile);
		return -1;
	}

	msgnum = last_message_index(recipient, dir) + 1;
#endif

	/* Lock the directory receiving the voicemail since we want it to still exist when we attempt to copy the voicemail.
	 * We need to unlock it before we return. */
	if (vm_lock_path(dir)) {
		ast_log(LOG_ERROR, "Couldn't lock directory %s.  Voicemail will be lost.\n", dir);
		/* Delete files */
		ast_filedelete(tmptxtfile, NULL);
		unlink(tmptxtfile);
		free_user(recipient);
		return -1;
	}

	make_file(destination, sizeof(destination), dir, msgnum);

	make_file(tmpaudiofile, sizeof(tmpaudiofile), tmpdir, msgnum);

	if (ast_filecopy(recdata->recording_file, tmpaudiofile, recdata->recording_ext)) {
		ast_log(LOG_ERROR, "Audio file failed to copy to tmp dir. Probably low disk space.\n");

		inprocess_count(recipient->mailbox, recipient->context, -1);
		ast_unlock_path(dir);
		free_user(recipient);
		unlink(tmptxtfile);
		return -1;
	}

	/* Alright, try to copy to the destination folder now. */
	if (ast_filerename(tmpaudiofile, destination, recdata->recording_ext)) {
		ast_log(LOG_ERROR, "Audio file failed to move to destination directory. Permissions/Overlap?\n");
		inprocess_count(recipient->mailbox, recipient->context, -1);
		ast_unlock_path(dir);
		free_user(recipient);
		unlink(tmptxtfile);
		return -1;
	}

	snprintf(desttxtfile, sizeof(desttxtfile), "%s.txt", destination);
	rename(tmptxtfile, desttxtfile);

	if (chmod(desttxtfile, VOICEMAIL_FILE_MODE) < 0) {
		ast_log(AST_LOG_ERROR, "Couldn't set permissions on voicemail text file %s: %s", desttxtfile, strerror(errno));
	}


	ast_unlock_path(dir);
	inprocess_count(recipient->mailbox, recipient->context, -1);

	/* If we copied something, we should store it either to ODBC or IMAP if we are using those. The STORE macro allows us
	 * to do both with one line and is also safe to use with file storage mode. Also, if we are using ODBC, now is a good
	 * time to create the voicemail database entry. */
	if (ast_fileexists(destination, NULL, NULL) > 0) {
		struct ast_channel *chan = NULL;
		char fmt[80];
		char clid[80];
		char cidnum[80], cidname[80];
		int send_email;

		if (ast_check_realtime("voicemail_data")) {
			get_date(date, sizeof(date));
			ast_store_realtime("voicemail_data",
				"origmailbox", recdata->mailbox,
				"context", S_OR(recdata->context, ""),
				"exten", S_OR(recdata->call_extension, ""),
				"priority", recdata->call_priority,
				"callerchan", S_OR(recdata->call_callerchan, "Unknown"),
				"callerid", S_OR(recdata->call_callerid, "Unknown"),
				"origdate", date,
				"origtime", time(NULL),
				"category", S_OR(category, ""),
				"filename", tmptxtfile,
				"duration", duration,
				SENTINEL);
		}

		STORE(dir, recipient->mailbox, recipient->context, msgnum, NULL, recipient, fmt, 0, vms, "", msg_id);

		send_email = ast_test_flag(recipient, VM_EMAIL_EXT_RECS);

		if (send_email) {
			/* Send an email if possible, fall back to just notifications if not. */
			ast_copy_string(fmt, recdata->recording_ext, sizeof(fmt));
			ast_copy_string(clid, recdata->call_callerid, sizeof(clid));
			ast_callerid_split(clid, cidname, sizeof(cidname), cidnum, sizeof(cidnum));

			/* recdata->call_callerchan itself no longer exists, so we can't use the real channel. Use a dummy one. */
			chan = ast_dummy_channel_alloc();
		}
		if (chan) {
			notify_new_message(chan, recipient, NULL, msgnum, duration, fmt, cidnum, cidname, "");
			ast_channel_unref(chan);
		} else {
			if (send_email) { /* We tried and failed. */
				ast_log(LOG_WARNING, "Failed to allocate dummy channel, email will not be sent\n");
			}
			notify_new_state(recipient);
		}
	}

	free_user(recipient);
	unlink(tmptxtfile);
	return 0;
}

/*!
 * \brief Prompts the user and records a voicemail to a mailbox.
 * \param chan
 * \param ext
 * \param options OPT_BUSY_GREETING, OPT_UNAVAIL_GREETING
 *
 *
 *
 * \return zero on success, -1 on error.
 */
static int leave_voicemail(struct ast_channel *chan, char *ext, struct leave_vm_options *options)
{
#ifdef IMAP_STORAGE
	int newmsgs, oldmsgs;
#endif
	char txtfile[PATH_MAX];
	char tmptxtfile[PATH_MAX];
	struct vm_state *vms = NULL;
	char callerid[256];
	FILE *txt;
	char date[256];
	int txtdes;
	int res = 0;
	int msgnum;
	int duration = 0;
	int sound_duration = 0;
	int ouseexten = 0;
	int greeting_only = 0;
	char tmpdur[16];
	char priority[16];
	char origtime[16];
	char dir[PATH_MAX];
	char tmpdir[PATH_MAX];
	char fn[PATH_MAX];
	char prefile[PATH_MAX] = "";
	char tempfile[PATH_MAX] = "";
	char ext_context[256] = "";
	char fmt[80];
	char *context;
	char ecodes[17] = "#";
	struct ast_str *tmp = ast_str_create(16);
	char *tmpptr;
	struct ast_vm_user *vmu;
	struct ast_vm_user svm;
	const char *category = NULL;
	const char *code;
	const char *alldtmf = "0123456789ABCD*#";
	char flag[80];

	if (!tmp) {
		return -1;
	}

	ast_str_set(&tmp, 0, "%s", ext);
	ext = ast_str_buffer(tmp);
	if ((context = strchr(ext, '@'))) {
		*context++ = '\0';
		tmpptr = strchr(context, '&');
	} else {
		tmpptr = strchr(ext, '&');
	}

	if (tmpptr)
		*tmpptr++ = '\0';

	ast_channel_lock(chan);
	if ((category = pbx_builtin_getvar_helper(chan, "VM_CATEGORY"))) {
		category = ast_strdupa(category);
	}
	ast_channel_unlock(chan);

	if (ast_test_flag(options, OPT_MESSAGE_Urgent)) {
		ast_copy_string(flag, "Urgent", sizeof(flag));
	} else if (ast_test_flag(options, OPT_MESSAGE_PRIORITY)) {
		ast_copy_string(flag, "PRIORITY", sizeof(flag));
	} else {
		flag[0] = '\0';
	}

	ast_debug(3, "Before find_user\n");
	memset(&svm, 0, sizeof(svm));
	if (!(vmu = find_user(&svm, context, ext))) {
		ast_log(AST_LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		ast_free(tmp);
		return res;
	}

	/* If maxmsg is zero, act as a "greetings only" voicemail: Exit successfully without recording */
	if (vmu->maxmsg == 0) {
		greeting_only = 1;
		ast_set_flag(options, OPT_SILENT);
	}

	/* Setup pre-file if appropriate */
	if (strcmp(vmu->context, "default"))
		snprintf(ext_context, sizeof(ext_context), "%s@%s", ext, vmu->context);
	else
		ast_copy_string(ext_context, vmu->mailbox, sizeof(ext_context));

	/* Set the path to the prefile. Will be one of
		VM_SPOOL_DIRcontext/ext/busy
		VM_SPOOL_DIRcontext/ext/unavail
	   Depending on the flag set in options.
	*/
	if (ast_test_flag(options, OPT_BUSY_GREETING)) {
		snprintf(prefile, sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, ext);
	} else if (ast_test_flag(options, OPT_UNAVAIL_GREETING)) {
		snprintf(prefile, sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, ext);
	}
	/* Set the path to the tmpfile as
		VM_SPOOL_DIR/context/ext/temp
	   and attempt to create the folder structure.
	*/
	snprintf(tempfile, sizeof(tempfile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, ext);
	if ((res = create_dirpath(tmpdir, sizeof(tmpdir), vmu->context, ext, "tmp"))) {
		ast_log(AST_LOG_WARNING, "Failed to make directory (%s)\n", tempfile);
		free_user(vmu);
		ast_free(tmp);
		return -1;
	}
	RETRIEVE(tempfile, -1, vmu->mailbox, vmu->context);
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

	/* Check current context for special extensions */
	if (ast_test_flag(vmu, VM_OPERATOR)) {
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
	}

	if (!ast_strlen_zero(vmu->exit)) {
		if (ast_exists_extension(chan, vmu->exit, "a", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			strncat(ecodes, "*", sizeof(ecodes) - strlen(ecodes) - 1);
		}
	} else if (ast_exists_extension(chan, ast_channel_context(chan), "a", 1,
		S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
		strncat(ecodes, "*", sizeof(ecodes) - strlen(ecodes) - 1);
	}

	if (ast_test_flag(options, OPT_DTMFEXIT)) {
		for (code = alldtmf; *code; code++) {
			char e[2] = "";
			e[0] = *code;
			if (strchr(ecodes, e[0]) == NULL
				&& ast_canmatch_extension(chan,
					(!ast_strlen_zero(options->exitcontext) ? options->exitcontext : ast_channel_context(chan)),
					e, 1, S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
				strncat(ecodes, e, sizeof(ecodes) - strlen(ecodes) - 1);
			}
		}
	}

	/* Play the beginning intro if desired */
	if (!ast_strlen_zero(prefile)) {
#ifdef ODBC_STORAGE
		int success =
#endif
			RETRIEVE(prefile, -1, ext, context);
		if (ast_fileexists(prefile, NULL, NULL) > 0) {
			if (ast_streamfile(chan, prefile, ast_channel_language(chan)) > -1) {
				/* We know we have a greeting at this point, so squelch the instructions
				 * if that is what is being asked of us */
				if (ast_test_flag(options, OPT_SILENT_IF_GREET)) {
					ast_set_flag(options, OPT_SILENT);
				}
				res = ast_waitstream(chan, ecodes);
			}
#ifdef ODBC_STORAGE
			if (success == -1) {
				/* We couldn't retrieve the file from the database, but we found it on the file system. Let's put it in the database. */
				ast_debug(1, "Greeting not retrieved from database, but found in file storage. Inserting into database\n");
				store_file(prefile, vmu->mailbox, vmu->context, -1);
			}
#endif
		} else {
			ast_debug(1, "%s doesn't exist, doing what we can\n", prefile);
			res = invent_message(chan, vmu->context, ext, ast_test_flag(options, OPT_BUSY_GREETING), ecodes);
		}
		DISPOSE(prefile, -1);
		if (res < 0) {
			ast_debug(1, "Hang up during prefile playback\n");
			free_user(vmu);
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
			ast_free(tmp);
			return -1;
		}
	}
	if (res == '#') {
		/* On a '#' we skip the instructions */
		ast_set_flag(options, OPT_SILENT);
		res = 0;
	}
	if (!res && !ast_test_flag(options, OPT_SILENT)) {
		res = ast_stream_and_wait(chan, INTRO, ecodes);
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
		ast_channel_exten_set(chan, "a");
		if (!ast_strlen_zero(vmu->exit)) {
			ast_channel_context_set(chan, vmu->exit);
		}
		ast_channel_priority_set(chan, 0);
		free_user(vmu);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "USEREXIT");
		ast_free(tmp);
		return 0;
	}

	/* Check for a '0' here */
	if (ast_test_flag(vmu, VM_OPERATOR) && res == '0') {
	transfer:
		if (ouseexten) {
			ast_channel_exten_set(chan, "o");
			if (!ast_strlen_zero(vmu->exit)) {
				ast_channel_context_set(chan, vmu->exit);
			}
			ast_play_and_wait(chan, "transfer");
			ast_channel_priority_set(chan, 0);
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "USEREXIT");
		}
		free_user(vmu);
		ast_free(tmp);
		return OPERATOR_EXIT;
	}

	/* Allow all other digits to exit Voicemail and return to the dialplan */
	if (ast_test_flag(options, OPT_DTMFEXIT) && res > 0) {
		if (!ast_strlen_zero(options->exitcontext)) {
			ast_channel_context_set(chan, options->exitcontext);
		}
		free_user(vmu);
		ast_free(tmp);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "USEREXIT");
		return res;
	}

	if (greeting_only) {
		ast_debug(3, "Greetings only VM (maxmsg=0), Skipping voicemail recording\n");
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "SUCCESS");
		res = 0;
		goto leave_vm_out;
	}

	if (res < 0) {
		free_user(vmu);
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		ast_free(tmp);
		return -1;
	}
	/* The meat of recording the message...  All the announcements and beeps have been played*/
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}
	ast_copy_string(fmt, vmfmts, sizeof(fmt));
	if (!ast_strlen_zero(fmt)) {
		char msg_id[MSG_ID_LEN] = "";
		msgnum = 0;

#ifdef IMAP_STORAGE
		/* Is ext a mailbox? */
		/* must open stream for this user to get info! */
		res = inboxcount(ext_context, &newmsgs, &oldmsgs);
		if (res < 0) {
			ast_log(AST_LOG_NOTICE, "Can not leave voicemail, unable to count messages\n");
			free_user(vmu);
			ast_free(tmp);
			return -1;
		}
		if (!(vms = get_vm_state_by_mailbox(ext, context, 0))) {
		/* It is possible under certain circumstances that inboxcount did not
		 * create a vm_state when it was needed. This is a catchall which will
		 * rarely be used.
		 */
			if (!(vms = create_vm_state_from_user(vmu))) {
				ast_log(AST_LOG_ERROR, "Couldn't allocate necessary space\n");
				free_user(vmu);
				ast_free(tmp);
				return -1;
			}
		}
		vms->newmessages++;

		/* here is a big difference! We add one to it later */
		msgnum = newmsgs + oldmsgs;
		ast_debug(3, "Messagecount set to %d\n", msgnum);
		snprintf(fn, sizeof(fn), "%simap/msg%s%04d", VM_SPOOL_DIR, vmu->mailbox, msgnum);
		/* set variable for compatibility */
		pbx_builtin_setvar_helper(chan, "VM_MESSAGEFILE", "IMAP_STORAGE");

		if ((res = imap_check_limits(chan, vms, vmu, msgnum))) {
			goto leave_vm_out;
		}
#else
		if (count_messages(vmu, dir) >= vmu->maxmsg - inprocess_count(vmu->mailbox, vmu->context, +1)) {
			res = ast_streamfile(chan, "vm-mailboxfull", ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, "");
			ast_log(AST_LOG_WARNING, "No more messages possible\n");
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
			inprocess_count(vmu->mailbox, vmu->context, -1);
			goto leave_vm_out;
		}

#endif
		snprintf(tmptxtfile, sizeof(tmptxtfile), "%s/XXXXXX", tmpdir);
		txtdes = mkstemp(tmptxtfile);
		chmod(tmptxtfile, VOICEMAIL_FILE_MODE & ~my_umask);
		if (txtdes < 0) {
			res = ast_streamfile(chan, "vm-mailboxfull", ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, "");
			ast_log(AST_LOG_ERROR, "Unable to create message file: %s\n", strerror(errno));
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
			inprocess_count(vmu->mailbox, vmu->context, -1);
			goto leave_vm_out;
		}

		/* Now play the beep once we have the message number for our next message. */
		if (res >= 0) {
			/* Unless we're *really* silent, try to send the beep */
			/* Play default or custom beep, unless no beep desired */
			if (!ast_strlen_zero(options->beeptone)) {
				res = ast_stream_and_wait(chan, options->beeptone, "");
			}
		}

		/* Store information in real-time storage */
		if (ast_check_realtime("voicemail_data")) {
			snprintf(priority, sizeof(priority), "%d", ast_channel_priority(chan));
			snprintf(origtime, sizeof(origtime), "%ld", (long) time(NULL));
			get_date(date, sizeof(date));
			ast_callerid_merge(callerid, sizeof(callerid),
				S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL),
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
				"Unknown");
			ast_store_realtime("voicemail_data",
				"origmailbox", ext,
				"context", ast_channel_context(chan),
				"exten", ast_channel_exten(chan),
				"priority", priority,
				"callerchan", ast_channel_name(chan),
				"callerid", callerid,
				"origdate", date,
				"origtime", origtime,
				"category", S_OR(category, ""),
				"filename", tmptxtfile,
				SENTINEL);
		}

		/* Store information */
		txt = fdopen(txtdes, "w+");
		if (txt) {
			generate_msg_id(msg_id);
			get_date(date, sizeof(date));
			ast_callerid_merge(callerid, sizeof(callerid),
				S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL),
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
				"Unknown");
			fprintf(txt,
				";\n"
				"; Message Information file\n"
				";\n"
				"[message]\n"
				"origmailbox=%s\n"
				"context=%s\n"
				"exten=%s\n"
				"rdnis=%s\n"
				"priority=%d\n"
				"callerchan=%s\n"
				"callerid=%s\n"
				"origdate=%s\n"
				"origtime=%ld\n"
				"category=%s\n"
				"msg_id=%s\n",
				ext,
				ast_channel_context(chan),
				ast_channel_exten(chan),
				S_COR(ast_channel_redirecting(chan)->from.number.valid,
					ast_channel_redirecting(chan)->from.number.str, "unknown"),
				ast_channel_priority(chan),
				ast_channel_name(chan),
				callerid,
				date, (long) time(NULL),
				category ? category : "",
				msg_id);
		} else {
			ast_log(AST_LOG_WARNING, "Error opening text file for output\n");
			inprocess_count(vmu->mailbox, vmu->context, -1);
			if (ast_check_realtime("voicemail_data")) {
				ast_destroy_realtime("voicemail_data", "filename", tmptxtfile, SENTINEL);
			}
			res = ast_streamfile(chan, "vm-mailboxfull", ast_channel_language(chan));
			goto leave_vm_out;
		}
		res = play_record_review(chan, NULL, tmptxtfile, vmu->maxsecs, fmt, 1, vmu, &duration, &sound_duration, NULL, options->record_gain, vms, flag, msg_id, 0);

		/* At this point, either we were instructed to make the message Urgent
		   by arguments to VoiceMail or during the review process by the person
		   leaving the message. So we update the directory where we want this
		   message to go. */
		if (!strcmp(flag, "Urgent")) {
			create_dirpath(dir, sizeof(dir), vmu->context, ext, "Urgent");
		}

		if (txt) {
			fprintf(txt, "flag=%s\n", flag);
			if (sound_duration < vmu->minsecs) {
				fclose(txt);
				ast_verb(3, "Recording was %d seconds long but needs to be at least %d - abandoning\n", sound_duration, vmu->minsecs);
				ast_filedelete(tmptxtfile, NULL);
				unlink(tmptxtfile);
				if (ast_check_realtime("voicemail_data")) {
					ast_destroy_realtime("voicemail_data", "filename", tmptxtfile, SENTINEL);
				}
				inprocess_count(vmu->mailbox, vmu->context, -1);
			} else {
				fprintf(txt, "duration=%d\n", duration);
				fclose(txt);
				if (vm_lock_path(dir)) {
					ast_log(AST_LOG_ERROR, "Couldn't lock directory %s.  Voicemail will be lost.\n", dir);
					/* Delete files */
					ast_filedelete(tmptxtfile, NULL);
					unlink(tmptxtfile);
					inprocess_count(vmu->mailbox, vmu->context, -1);
				} else if (ast_fileexists(tmptxtfile, NULL, NULL) <= 0) {
					ast_debug(1, "The recorded media file is gone, so we should remove the .txt file too!\n");
					unlink(tmptxtfile);
					ast_unlock_path(dir);
					inprocess_count(vmu->mailbox, vmu->context, -1);
					if (ast_check_realtime("voicemail_data")) {
						ast_destroy_realtime("voicemail_data", "filename", tmptxtfile, SENTINEL);
					}
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

					/* Properly set permissions on voicemail text descriptor file.
					   Unfortunately mkstemp() makes this file 0600 on most unix systems. */
					if (chmod(txtfile, VOICEMAIL_FILE_MODE) < 0)
						ast_log(AST_LOG_ERROR, "Couldn't set permissions on voicemail text file %s: %s", txtfile, strerror(errno));

					ast_unlock_path(dir);
					if (ast_check_realtime("voicemail_data")) {
						snprintf(tmpdur, sizeof(tmpdur), "%d", duration);
						ast_update_realtime("voicemail_data", "filename", tmptxtfile, "filename", fn, "duration", tmpdur, SENTINEL);
					}
					/* We must store the file first, before copying the message, because
					 * ODBC storage does the entire copy with SQL.
					 */
					if (ast_fileexists(fn, NULL, NULL) > 0) {
						STORE(dir, vmu->mailbox, vmu->context, msgnum, chan, vmu, fmt, duration, vms, flag, msg_id);
					}

					/* Are there to be more recipients of this message? */
					while (tmpptr) {
						struct ast_vm_user recipu, *recip;
						char *exten, *cntx;

						exten = strsep(&tmpptr, "&");
						cntx = strchr(exten, '@');
						if (cntx) {
							*cntx = '\0';
							cntx++;
						}
						memset(&recipu, 0, sizeof(recipu));
						if ((recip = find_user(&recipu, cntx, exten))) {
							copy_message(chan, vmu, 0, msgnum, duration, recip, fmt, dir, flag, NULL);
							free_user(recip);
						}
					}

					/* Notification needs to happen after the copy, though. */
					if (ast_fileexists(fn, NULL, NULL)) {
#ifdef IMAP_STORAGE
						notify_new_message(chan, vmu, vms, msgnum, duration, fmt,
							S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
							S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL),
							flag);
#else
						notify_new_message(chan, vmu, NULL, msgnum, duration, fmt,
							S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
							S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL),
							flag);
#endif
					}

					/* Disposal needs to happen after the optional move and copy */
					if (ast_fileexists(fn, NULL, NULL)) {
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

		if (sound_duration < vmu->minsecs)
			/* XXX We should really give a prompt too short/option start again, with leave_vm_out called only after a timeout XXX */
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		else
			pbx_builtin_setvar_helper(chan, "VMSTATUS", "SUCCESS");
	} else
		ast_log(AST_LOG_WARNING, "No format for saving voicemail?\n");
leave_vm_out:
	free_user(vmu);

#ifdef IMAP_STORAGE
	/* expunge message - use UID Expunge if supported on IMAP server*/
	ast_debug(3, "*** Checking if we can expunge, expungeonhangup set to %d\n", expungeonhangup);
	if (expungeonhangup == 1 && vms->mailstream != NULL) {
		ast_mutex_lock(&vms->lock);
#ifdef HAVE_IMAP_TK2006
		if (LEVELUIDPLUS (vms->mailstream)) {
			mail_expunge_full(vms->mailstream, NIL, EX_UID);
		} else
#endif
			mail_expunge(vms->mailstream);
		ast_mutex_unlock(&vms->lock);
	}
#endif

	ast_free(tmp);
	return res;
}

#if !defined(IMAP_STORAGE)
static int resequence_mailbox(struct ast_vm_user *vmu, char *dir, int stopcount)
{
	/* we know the actual number of messages, so stop process when number is hit */

	int x, dest;
	char sfn[PATH_MAX];
	char dfn[PATH_MAX];

	if (vm_lock_path(dir)) {
		return ERROR_LOCK_PATH;
	}

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
	d = ast_say_number(chan, num, AST_DIGIT_ANY, language, NULL);
	return d;
}

static int save_to_folder(struct ast_vm_user *vmu, struct vm_state *vms, int msg, int box, int *newmsg, int move)
{
#ifdef IMAP_STORAGE
	/* we must use mbox(x) folder names, and copy the message there */
	/* simple. huh? */
	char sequence[10];
	char mailbox[256];
	int res;
	int curr_mbox;

	/* get the real IMAP message number for this message */
	snprintf(sequence, sizeof(sequence), "%ld", vms->msgArray[msg]);

	ast_debug(3, "Copying sequence %s to mailbox %s\n", sequence, mbox(vmu, box));
	ast_mutex_lock(&vms->lock);
	/* if save to Old folder, put in INBOX as read */
	if (box == OLD_FOLDER) {
		mail_setflag(vms->mailstream, sequence, "\\Seen");
	} else if (box == NEW_FOLDER) {
		mail_clearflag(vms->mailstream, sequence, "\\Seen");
	}
	if (!strcasecmp(mbox(vmu, NEW_FOLDER), vms->curbox) && (box == NEW_FOLDER || box == OLD_FOLDER)) {
		ast_mutex_unlock(&vms->lock);
		return 0;
	}

	/* get the current mailbox so that we can point the mailstream back to it later */
	curr_mbox = get_folder_by_name(vms->curbox);

	/* Create the folder if it doesn't exist */
	imap_mailbox_name(mailbox, sizeof(mailbox), vms, box, 1); /* Get the full mailbox name */
	if (vms->mailstream && !mail_status(vms->mailstream, mailbox, SA_UIDNEXT)) {
    		if (mail_create(vms->mailstream, mailbox) != NIL) {
			ast_log(AST_LOG_NOTICE, "Folder %s created!\n", mbox(vmu, box));
		}
	}

	/* restore previous mbox stream */
	if (init_mailstream(vms, curr_mbox) || !vms->mailstream) {
		ast_log(AST_LOG_ERROR, "IMAP mailstream is NULL or can't init_mailstream\n");
		res = -1;
	} else {
		if (move) {
			res = !mail_move(vms->mailstream, sequence, (char *) mbox(vmu, box));
		} else {
			res = !mail_copy(vms->mailstream, sequence, (char *) mbox(vmu, box));
		}
	}
	ast_mutex_unlock(&vms->lock);
	return res;
#else
	char *dir = vms->curdir;
	char *username = vms->username;
	char *context = vmu->context;
	char sfn[PATH_MAX];
	char dfn[PATH_MAX];
	char ddir[PATH_MAX];
	const char *dbox = mbox(vmu, box);
	int x, i;
	create_dirpath(ddir, sizeof(ddir), context, username, dbox);

	if (vm_lock_path(ddir))
		return ERROR_LOCK_PATH;

	x = last_message_index(vmu, ddir) + 1;

	if (box == 10 && x >= vmu->maxdeletedmsg) { /* "Deleted" folder*/
		x--;
		for (i = 1; i <= x; i++) {
			/* Push files down a "slot".  The oldest file (msg0000) will be deleted. */
			make_file(sfn, sizeof(sfn), ddir, i);
			make_file(dfn, sizeof(dfn), ddir, i - 1);
			if (EXISTS(ddir, i, sfn, NULL)) {
				RENAME(ddir, i, vmu->mailbox, vmu->context, ddir, i - 1, sfn, dfn);
			} else
				break;
		}
	} else {
		if (x >= vmu->maxmsg) {
			ast_unlock_path(ddir);
			return ERROR_MAX_MSGS;
		}
	}
	make_file(sfn, sizeof(sfn), dir, msg);
	make_file(dfn, sizeof(dfn), ddir, x);
	if (strcmp(sfn, dfn)) {
		COPY(dir, msg, ddir, x, username, context, sfn, dfn);
	}
	ast_unlock_path(ddir);

	if (newmsg) {
		*newmsg = x;
	}
	return 0;
#endif
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
	int bytes = 0;
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
	bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 2, "Advanced", "Advanced", "3", 1);
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
	for (x = 0; x < 5; x++) {
		snprintf(num, sizeof(num), "%d", x);
		bytes += ast_adsi_load_soft_key(buf + bytes, ADSI_KEY_APPS + 12 + x, mbox(NULL, x), mbox(NULL, x), num, 1);
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

	ast_debug(1, "Done downloading scripts...\n");

#ifdef DISPLAY
	/* Add last dot */
	bytes = 0;
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "   ......", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
#endif
	ast_debug(1, "Restarting session...\n");

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
	if (x < 0) {
		*useadsi = 0;
		ast_channel_adsicpe_set(chan, AST_ADSI_UNAVAILABLE);
		return;
	}
	if (!x) {
		if (adsi_load_vmail(chan, useadsi)) {
			ast_log(AST_LOG_WARNING, "Unable to upload voicemail scripts\n");
			return;
		}
	} else
		*useadsi = 1;
}

static void adsi_login(struct ast_channel *chan)
{
	unsigned char buf[256];
	int bytes = 0;
	unsigned char keys[8];
	int x;
	if (!ast_adsi_available(chan))
		return;

	for (x = 0; x < 8; x++)
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
	int bytes = 0;
	unsigned char keys[8];
	int x;
	if (!ast_adsi_available(chan))
		return;

	for (x = 0; x < 8; x++)
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
	int bytes = 0;
	unsigned char keys[8];
	int x, y;

	if (!ast_adsi_available(chan))
		return;

	for (x = 0; x < 5; x++) {
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
	int bytes = 0;
	unsigned char buf[256];
	char buf1[256], buf2[256];
	char fn2[PATH_MAX];

	char cid[256] = "";
	char *val;
	char *name, *num;
	char datetime[21] = "";
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
			if (!fgets((char *) buf, sizeof(buf), f)) {
				continue;
			}
			if (!feof(f)) {
				char *stringp = NULL;
				stringp = (char *) buf;
				strsep(&stringp, "=");
				val = strsep(&stringp, "=");
				if (!ast_strlen_zero(val)) {
					if (!strcmp((char *) buf, "callerid"))
						ast_copy_string(cid, val, sizeof(cid));
					if (!strcmp((char *) buf, "origdate"))
						ast_copy_string(datetime, val, sizeof(datetime));
				}
			}
		}
		fclose(f);
	}
	/* New meaning for keys */
	for (x = 0; x < 5; x++)
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
	} else {
		name = "Unknown Caller";
	}

	/* If deleted, show "undeleted" */
#ifdef IMAP_STORAGE
	ast_mutex_lock(&vms->lock);
#endif
	if (vms->deleted[vms->curmsg]) {
		keys[1] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 11);
	}
#ifdef IMAP_STORAGE
	ast_mutex_unlock(&vms->lock);
#endif

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
	int bytes = 0;
	unsigned char buf[256];
	unsigned char keys[8];

	int x;

	if (!ast_adsi_available(chan))
		return;

	/* New meaning for keys */
	for (x = 0; x < 5; x++)
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
#ifdef IMAP_STORAGE
	ast_mutex_lock(&vms->lock);
#endif
	if (vms->deleted[vms->curmsg]) {
		keys[1] = ADSI_KEY_SKT | (ADSI_KEY_APPS + 11);
	}
#ifdef IMAP_STORAGE
	ast_mutex_unlock(&vms->lock);
#endif

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
	int bytes = 0;
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

	for (x = 0; x < 6; x++)
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
	int bytes = 0;
	unsigned char keys[8];
	int x;

	char *mess = (vms->lastmsg == 0) ? "message" : "messages";

	if (!ast_adsi_available(chan))
		return;

	/* Original command keys */
	for (x = 0; x < 6; x++)
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
	int bytes = 0;

	if (!ast_adsi_available(chan))
		return;
	bytes += adsi_logo(buf + bytes);
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_LEFT, 0, " ", "");
	bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Goodbye", "");
	bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
	bytes += ast_adsi_voice_mode(buf + bytes, 0);

	ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
}

/*!\brief get_folder: Folder menu
 *	Plays "press 1 for INBOX messages" etc.
 *	Should possibly be internationalized
 */
static int get_folder(struct ast_channel *chan, int start)
{
	int x;
	int d;
	char fn[PATH_MAX];
	d = ast_play_and_wait(chan, "vm-press");	/* "Press" */
	if (d)
		return d;
	for (x = start; x < 5; x++) {	/* For all folders */
		if ((d = ast_say_number(chan, x, AST_DIGIT_ANY, ast_channel_language(chan), NULL)))
			return d;
		d = ast_play_and_wait(chan, "vm-for");	/* "for" */
		if (d)
			return d;
		snprintf(fn, sizeof(fn), "vm-%s", mbox(NULL, x));	/* Folder name */

		/* The inbox folder can have its name changed under certain conditions
		 * so this checks if the sound file exists for the inbox folder name and
		 * if it doesn't, plays the default name instead. */
		if (x == 0) {
			if (ast_fileexists(fn, NULL, NULL)) {
				d = vm_play_folder_name(chan, fn);
			} else {
				ast_verb(4, "Failed to find file %s; falling back to INBOX\n", fn);
				d = vm_play_folder_name(chan, "vm-INBOX");
			}
		} else {
			ast_test_suite_event_notify("PLAYBACK", "Message: folder name %s", fn);
			d = vm_play_folder_name(chan, fn);
		}

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

/* Japanese Syntax */
static int get_folder_ja(struct ast_channel *chan, int start)
{
	int x;
	int d;
	char fn[256];
	for (x = start; x < 5; x++) {    /* For all folders */
		if ((d = ast_say_number(chan, x, AST_DIGIT_ANY, ast_channel_language(chan), (char *) NULL))) {
			return d;
		}
		snprintf(fn, sizeof(fn), "vm-%s", mbox(NULL, x));     /* Folder name */
		d = vm_play_folder_name(chan, fn);
		if (d) {
			return d;
		}
		d = ast_waitfordigit(chan, 500);
		if (d) {
			return d;
		}
	}
	d = ast_play_and_wait(chan, "vm-tocancel"); /* "or pound to cancel" */
	if (d) {
		return d;
	}
	d = ast_waitfordigit(chan, 4000);
	return d;
}

/*!
 * \brief plays a prompt and waits for a keypress.
 * \param chan
 * \param fn the name of the voice prompt file to be played. For example, 'vm-changeto', 'vm-savefolder'
 * \param start Does not appear to be used at this time.
 *
 * This is used by the main menu option to move a message to a folder or to save a message into a folder.
 * After playing the  message identified by the fn parameter value, it calls get_folder(), which plays the
 * prompting for the number inputs that correspond to the available folders.
 *
 * \return zero on success, or -1 on error.
 */
static int get_folder2(struct ast_channel *chan, char *fn, int start)
{
	int res = 0;
	int loops = 0;

	res = ast_play_and_wait(chan, fn);	/* Folder name */
	while (((res < '0') || (res > '9')) &&
		   (res != '#') && (res >= 0) &&
		   loops < 4) {
		/* res = get_folder(chan, 0); */
		if (!strcasecmp(ast_channel_language(chan), "ja")) {   /* Japanese syntax */
			res = get_folder_ja(chan, 0);
		} else { /* Default syntax */
			res = get_folder(chan, 0);
		}
		loops++;
	}
	if (loops == 4) { /* give up */
		ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c", '#', '#');
		return '#';
	}
	ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
		isprint(res) ? res : '?', isprint(res) ? res : '?');
	return res;
}

/*!
 * \brief presents the option to prepend to an existing message when forwarding it.
 * \param chan
 * \param vmu
 * \param curdir
 * \param curmsg
 * \param vm_fmts
 * \param context
 * \param record_gain
 * \param duration
 * \param vms
 * \param flag
 *
 * Presents a prompt for 1 to prepend the current message, 2 to forward the message without prepending, or * to return to the main menu.
 *
 * This is invoked from forward_message() when performing a forward operation (option 8 from main menu).
 * \return zero on success, -1 on error.
 */
static int vm_forwardoptions(struct ast_channel *chan, struct ast_vm_user *vmu, char *curdir, int curmsg, char *vm_fmts,
			char *context, signed char record_gain, long *duration, struct vm_state *vms, char *flag)
{
	int cmd = 0;
	int retries = 0, prepend_duration = 0, already_recorded = 0;
	char msgfile[PATH_MAX], backup[PATH_MAX], backup_textfile[PATH_MAX];
	char textfile[PATH_MAX];
	struct ast_config *msg_cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
#ifndef IMAP_STORAGE
	signed char zero_gain = 0;
#else
	const char *msg_id = NULL;
#endif
	const char *duration_str;

	/* Must always populate duration correctly */
	make_file(msgfile, sizeof(msgfile), curdir, curmsg);
	strcpy(textfile, msgfile);
	strcpy(backup, msgfile);
	strcpy(backup_textfile, msgfile);
	strncat(textfile, ".txt", sizeof(textfile) - strlen(textfile) - 1);
	strncat(backup, "-bak", sizeof(backup) - strlen(backup) - 1);
	strncat(backup_textfile, "-bak.txt", sizeof(backup_textfile) - strlen(backup_textfile) - 1);

	if ((msg_cfg = ast_config_load(textfile, config_flags)) && valid_config(msg_cfg) && (duration_str = ast_variable_retrieve(msg_cfg, "message", "duration"))) {
		*duration = atoi(duration_str);
	} else {
		*duration = 0;
	}

	while ((cmd >= 0) && (cmd != 't') && (cmd != '*')) {
		if (cmd)
			retries = 0;
		switch (cmd) {
		case '1':

#ifdef IMAP_STORAGE
			/* Record new intro file */
			if (msg_cfg && msg_cfg != CONFIG_STATUS_FILEINVALID) {
				msg_id = ast_variable_retrieve(msg_cfg, "message", "msg_id");
			}
			make_file(vms->introfn, sizeof(vms->introfn), curdir, curmsg);
			strncat(vms->introfn, "intro", sizeof(vms->introfn));
			ast_play_and_wait(chan, "vm-record-prepend");
			ast_play_and_wait(chan, "beep");
			cmd = play_record_review(chan, NULL, vms->introfn, vmu->maxsecs, vm_fmts, 1, vmu, (int *) duration, NULL, NULL, record_gain, vms, flag, msg_id, 1);
			if (cmd == -1) {
				break;
			}
			cmd = 't';
#else

			/* prepend a message to the current message, update the metadata and return */

			make_file(msgfile, sizeof(msgfile), curdir, curmsg);
			strcpy(textfile, msgfile);
			strncat(textfile, ".txt", sizeof(textfile) - 1);
			*duration = 0;

			/* if we can't read the message metadata, stop now */
			if (!valid_config(msg_cfg)) {
				cmd = 0;
				break;
			}

			/* Back up the original file, so we can retry the prepend and restore it after forward. */
#ifndef IMAP_STORAGE
			if (already_recorded) {
				ast_filecopy(backup, msgfile, NULL);
				copy(backup_textfile, textfile);
			}
			else {
				ast_filecopy(msgfile, backup, NULL);
				copy(textfile, backup_textfile);
			}
#endif
			already_recorded = 1;

			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);

			cmd = ast_play_and_prepend(chan, NULL, msgfile, 0, vm_fmts, &prepend_duration, NULL, 1, silencethreshold, maxsilence);

			if (cmd == 'S') { /* If we timed out, tell the user it didn't work properly and clean up the files */
				ast_stream_and_wait(chan, vm_pls_try_again, ""); /* this might be removed if a proper vm_prepend_timeout is ever recorded */
				ast_stream_and_wait(chan, vm_prepend_timeout, "");
				ast_filerename(backup, msgfile, NULL);
			}

			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &zero_gain, sizeof(zero_gain), 0);


			if ((duration_str = ast_variable_retrieve(msg_cfg, "message", "duration")))
				*duration = atoi(duration_str);

			if (prepend_duration) {
				struct ast_category *msg_cat;
				/* need enough space for a maximum-length message duration */
				char duration_buf[12];

				*duration += prepend_duration;
				msg_cat = ast_category_get(msg_cfg, "message", NULL);
				snprintf(duration_buf, sizeof(duration_buf), "%ld", *duration);
				if (!ast_variable_update(msg_cat, "duration", duration_buf, NULL, 0)) {
					ast_config_text_file_save(textfile, msg_cfg, "app_voicemail");
				}
			}

#endif
			break;
		case '2':
			/* NULL out introfile so we know there is no intro! */
#ifdef IMAP_STORAGE
			*vms->introfn = '\0';
#endif
			cmd = 't';
			break;
		case '*':
			cmd = '*';
			break;
		default:
			/* If time_out and return to menu, reset already_recorded */
			already_recorded = 0;

			cmd = ast_play_and_wait(chan, "vm-forwardoptions");
				/* "Press 1 to prepend a message or 2 to forward the message without prepending" */
			if (!cmd) {
				cmd = ast_play_and_wait(chan, "vm-starmain");
				/* "press star to return to the main menu" */
			}
			if (!cmd) {
				cmd = ast_waitfordigit(chan, 6000);
			}
			if (!cmd) {
				retries++;
			}
			if (retries > 3) {
				cmd = '*'; /* Let's cancel this beast */
			}
			ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
				isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
		}
	}

	if (valid_config(msg_cfg))
		ast_config_destroy(msg_cfg);
	if (prepend_duration)
		*duration = prepend_duration;

	if (already_recorded && cmd == -1) {
		/* restore original message if prepention cancelled */
		ast_filerename(backup, msgfile, NULL);
		rename(backup_textfile, textfile);
	}

	if (cmd == 't' || cmd == 'S') /* XXX entering this block with a value of 'S' is probably no longer possible. */
		cmd = 0;
	return cmd;
}

static void queue_mwi_event(const char *channel_id, const char *box, int urgent, int new, int old)
{
	char *mailbox;
	char *context;

	if (separate_mailbox(ast_strdupa(box), &mailbox, &context)) {
		return;
	}

	ast_debug(3, "Queueing event for mailbox %s  New: %d   Old: %d\n", box, new + urgent, old);
	ast_publish_mwi_state_channel(mailbox, context, new + urgent, old, channel_id);

	if (!ast_strlen_zero(aliasescontext)) {
		struct ao2_iterator *aliases;
		struct mailbox_alias_mapping *mapping;

		aliases = ao2_find(mailbox_alias_mappings, box, OBJ_SEARCH_KEY | OBJ_MULTIPLE);
		while ((mapping = ao2_iterator_next(aliases))) {
			char alias[strlen(mapping->alias) + 1];
			strcpy(alias, mapping->alias); /* safe */
			mailbox = NULL;
			context = NULL;
			ast_debug(3, "Found alias mapping: %s -> %s\n", mapping->alias, box);
			separate_mailbox(alias, &mailbox, &context);
			ast_publish_mwi_state_channel(mailbox, context, new + urgent, old, channel_id);
			ao2_ref(mapping, -1);
		}
		ao2_iterator_destroy(aliases);
	}
}

/*!
 * \brief Sends email notification that a user has a new voicemail waiting for them.
 * \param chan
 * \param vmu
 * \param vms
 * \param msgnum
 * \param duration
 * \param fmt
 * \param cidnum The Caller ID phone number value.
 * \param cidname The Caller ID name value.
 * \param flag
 *
 * \return zero on success, -1 on error.
 */
static int notify_new_message(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msgnum, long duration, char *fmt, char *cidnum, char *cidname, const char *flag)
{
	char todir[PATH_MAX], fn[PATH_MAX], ext_context[PATH_MAX], *stringp;
	int newmsgs = 0, oldmsgs = 0, urgentmsgs = 0;
	const char *category;
	char *myserveremail = serveremail;

	ast_channel_lock(chan);
	if ((category = pbx_builtin_getvar_helper(chan, "VM_CATEGORY"))) {
		category = ast_strdupa(category);
	}
	ast_channel_unlock(chan);

#ifndef IMAP_STORAGE
	make_dir(todir, sizeof(todir), vmu->context, vmu->mailbox, !ast_strlen_zero(flag) && !strcmp(flag, "Urgent") ? "Urgent" : "INBOX");
#else
	snprintf(todir, sizeof(todir), "%simap", VM_SPOOL_DIR);
#endif
	make_file(fn, sizeof(fn), todir, msgnum);
	snprintf(ext_context, sizeof(ext_context), "%s@%s", vmu->mailbox, vmu->context);

	if (!ast_strlen_zero(vmu->attachfmt)) {
		if (strstr(fmt, vmu->attachfmt))
			fmt = vmu->attachfmt;
		else
			ast_log(AST_LOG_WARNING, "Attachment format '%s' is not one of the recorded formats '%s'.  Falling back to default format for '%s@%s'.\n", vmu->attachfmt, fmt, vmu->mailbox, vmu->context);
	}

	/* Attach only the first format */
	fmt = ast_strdupa(fmt);
	stringp = fmt;
	strsep(&stringp, "|");

	if (!ast_strlen_zero(vmu->serveremail))
		myserveremail = vmu->serveremail;

	if (!ast_strlen_zero(vmu->email)) {
		int attach_user_voicemail = ast_test_flag(vmu, VM_ATTACH);
		char *msg_id = NULL;
#ifdef IMAP_STORAGE
		struct ast_config *msg_cfg;
		struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
		char filename[PATH_MAX];

		snprintf(filename, sizeof(filename), "%s.txt", fn);
		msg_cfg = ast_config_load(filename, config_flags);
		if (msg_cfg && msg_cfg != CONFIG_STATUS_FILEINVALID) {
			msg_id = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "msg_id"));
			ast_config_destroy(msg_cfg);
		}
#endif

		if (attach_user_voicemail)
			RETRIEVE(todir, msgnum, vmu->mailbox, vmu->context);

		/* XXX possible imap issue, should category be NULL XXX */
		sendmail(myserveremail, vmu, msgnum, vmu->context, vmu->mailbox, mbox(vmu, 0), cidnum, cidname, fn, NULL, fmt, duration, attach_user_voicemail, chan, category, flag, msg_id);

		if (attach_user_voicemail)
			DISPOSE(todir, msgnum);
	}

	if (!ast_strlen_zero(vmu->pager)) {
		sendpage(myserveremail, vmu->pager, msgnum, vmu->context, vmu->mailbox, mbox(vmu, 0), cidnum, cidname, duration, vmu, category, flag);
	}

	if (ast_test_flag(vmu, VM_DELETE))
		DELETE(todir, msgnum, fn, vmu);

	/* Leave voicemail for someone */
	if (ast_app_has_voicemail(ext_context, NULL))
		ast_app_inboxcount2(ext_context, &urgentmsgs, &newmsgs, &oldmsgs);

	queue_mwi_event(ast_channel_uniqueid(chan), ext_context, urgentmsgs, newmsgs, oldmsgs);
	run_externnotify(vmu->context, vmu->mailbox, flag);

#ifdef IMAP_STORAGE
	vm_delete(fn);  /* Delete the file, but not the IMAP message */
	if (ast_test_flag(vmu, VM_DELETE))  { /* Delete the IMAP message if delete = yes */
		vm_imap_delete(NULL, vms->curmsg, vmu);
		vms->newmessages--;  /* Fix new message count */
	}
#endif

	return 0;
}

/*!
 * \brief Sends a voicemail message to a mailbox recipient.
 * \param chan
 * \param context
 * \param vms
 * \param sender
 * \param fmt
 * \param is_new_message Used to indicate the mode for which this method was invoked.
 *             Will be 0 when called to forward an existing message (option 8)
 *             Will be 1 when called to leave a message (option 3->5)
 * \param record_gain
 * \param urgent
 *
 * Reads the destination mailbox(es) from keypad input for CID, or if use_directory feature is enabled, the Directory.
 *
 * When in the leave message mode (is_new_message == 1):
 *   - allow the leaving of a message for ourselves. (Will not allow us to forward a message to ourselves, when is_new_message == 0).
 *   - attempt to determine the context and mailbox, and then invoke leave_message() function to record and store the message.
 *
 * When in the forward message mode (is_new_message == 0):
 *   - retrieves the current message to be forwarded
 *   - copies the original message to a temporary file, so updates to the envelope can be done.
 *   - determines the target mailbox and folders
 *   - copies the message into the target mailbox, using copy_message() or by generating the message into an email attachment if using imap folders.
 *
 * \return zero on success, -1 on error.
 */
static int forward_message(struct ast_channel *chan, char *context, struct vm_state *vms, struct ast_vm_user *sender, char *fmt, int is_new_message, signed char record_gain, int urgent)
{
#ifdef IMAP_STORAGE
	int todircount = 0;
	struct vm_state *dstvms;
#endif
	char username[70]="";
	char fn[PATH_MAX]; /* for playback of name greeting */
	char ecodes[16] = "#";
	int res = 0, cmd = 0;
	struct ast_vm_user *receiver = NULL, *vmtmp;
	AST_LIST_HEAD_NOLOCK_STATIC(extensions, ast_vm_user);
	char *stringp;
	const char *s;
	const char mailbox_context[256];
	int saved_messages = 0;
	int valid_extensions = 0;
	char *dir;
	int curmsg;
	char urgent_str[7] = "";
	int prompt_played = 0;
#ifndef IMAP_STORAGE
	char msgfile[PATH_MAX], textfile[PATH_MAX], backup[PATH_MAX], backup_textfile[PATH_MAX];
#endif
	if (ast_test_flag((&globalflags), VM_FWDURGAUTO)) {
		ast_copy_string(urgent_str, urgent ? "Urgent" : "", sizeof(urgent_str));
	}

	if (vms == NULL) return -1;
	dir = vms->curdir;
	curmsg = vms->curmsg;

	ast_test_suite_event_notify("FORWARD", "Message: entering forward message menu");
	while (!res && !valid_extensions) {
		int use_directory = 0;
		if (ast_test_flag((&globalflags), VM_DIRECTFORWARD)) {
			int done = 0;
			int retries = 0;
			cmd = 0;
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
					done = 1;
					break;
				case '*':
					cmd = 't';
					done = 1;
					break;
				default:
					/* Press 1 to enter an extension press 2 to use the directory */
					cmd = ast_play_and_wait(chan, "vm-forward");
					if (!cmd) {
						cmd = ast_waitfordigit(chan, 3000);
					}
					if (!cmd) {
						retries++;
					}
					if (retries > 3) {
						cmd = 't';
						done = 1;
					}
					ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
						isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
				}
			}
			if (cmd < 0 || cmd == 't')
				break;
		}

		if (use_directory) {
			/* use app_directory */

			struct ast_app* directory_app;

			directory_app = pbx_findapp("Directory");
			if (directory_app) {
				char vmcontext[256];
				char old_context[strlen(ast_channel_context(chan)) + 1];
				char old_exten[strlen(ast_channel_exten(chan)) + 1];
				int old_priority;
				/* make backup copies */
				strcpy(old_context, ast_channel_context(chan)); /* safe */
				strcpy(old_exten, ast_channel_exten(chan)); /* safe */
				old_priority = ast_channel_priority(chan);

				/* call the Directory, changes the channel */
				snprintf(vmcontext, sizeof(vmcontext), "%s,,v", context ? context : "default");
				res = pbx_exec(chan, directory_app, vmcontext);

				ast_copy_string(username, ast_channel_exten(chan), sizeof(username));

				/* restore the old context, exten, and priority */
				ast_channel_context_set(chan, old_context);
				ast_channel_exten_set(chan, old_exten);
				ast_channel_priority_set(chan, old_priority);
			} else {
				ast_log(AST_LOG_WARNING, "Could not find the Directory application, disabling directory_forward\n");
				ast_clear_flag((&globalflags), VM_DIRECTFORWARD);
			}
		} else {
			/* Ask for an extension */
			res = ast_streamfile(chan, "vm-extension", ast_channel_language(chan));	/* "extension" */
			prompt_played++;
			if (res || prompt_played > 4)
				break;
			if ((res = ast_readstring(chan, username, sizeof(username) - 1, 2000, 10000, "#")) < 0)
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
			snprintf((char*)mailbox_context, sizeof(mailbox_context), "%s@%s", s, context ? context : "default");
			if ((is_new_message == 1 || strcmp(s, sender->mailbox)) && (receiver = find_user(NULL, context, s))) {
				int oldmsgs;
				int newmsgs;
				int capacity;

				if (inboxcount(mailbox_context, &newmsgs, &oldmsgs)) {
					ast_log(LOG_ERROR, "Problem in calculating number of voicemail messages available for extension %s\n", mailbox_context);
					/* Shouldn't happen, but allow trying another extension if it does */
					res = ast_play_and_wait(chan, "pbx-invalid");
					valid_extensions = 0;
					break;
				}
#ifdef IMAP_STORAGE
				if (!(dstvms = get_vm_state_by_mailbox(s, context, 0))) {
					if (!(dstvms = create_vm_state_from_user(receiver))) {
						ast_log(AST_LOG_ERROR, "Couldn't allocate necessary space\n");
						/* Shouldn't happen, but allow trying another extension if it does */
						res = ast_play_and_wait(chan, "pbx-invalid");
						valid_extensions = 0;
						break;
					}
				}
				check_quota(dstvms, imapfolder);
				if (dstvms->quota_limit && dstvms->quota_usage >= dstvms->quota_limit) {
					ast_log(LOG_NOTICE, "Mailbox '%s' is exceeded quota %u >= %u\n", mailbox_context, dstvms->quota_usage, dstvms->quota_limit);
					res = ast_play_and_wait(chan, "vm-mailboxfull");
					valid_extensions = 0;
					while ((vmtmp = AST_LIST_REMOVE_HEAD(&extensions, list))) {
						inprocess_count(vmtmp->mailbox, vmtmp->context, -1);
						free_user(vmtmp);
					}
					break;
				}
#endif
				capacity = receiver->maxmsg - inprocess_count(receiver->mailbox, receiver->context, +1);
				if ((newmsgs + oldmsgs) >= capacity) {
					ast_log(LOG_NOTICE, "Mailbox '%s' is full with capacity of %d, prompting for another extension.\n", mailbox_context, capacity);
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
				/* XXX Optimization for the future.  When we encounter a single bad extension,
				 * bailing out on all of the extensions may not be the way to go.  We should
				 * probably just bail on that single extension, then allow the user to enter
				 * several more. XXX
				 */
				while ((receiver = AST_LIST_REMOVE_HEAD(&extensions, list))) {
					free_user(receiver);
				}
				ast_log(LOG_NOTICE, "'%s' is not a valid mailbox\n", mailbox_context);
				/* "I am sorry, that's not a valid extension.  Please try again." */
				res = ast_play_and_wait(chan, "pbx-invalid");
				valid_extensions = 0;
				break;
			}

			/* play name if available, else play extension number */
			snprintf(fn, sizeof(fn), "%s%s/%s/greet", VM_SPOOL_DIR, receiver->context, s);
			RETRIEVE(fn, -1, s, receiver->context);
			if (ast_fileexists(fn, NULL, NULL) > 0) {
				res = ast_stream_and_wait(chan, fn, ecodes);
				if (res) {
					DISPOSE(fn, -1);
					return res;
				}
			} else {
				res = ast_say_digit_str(chan, s, ecodes, ast_channel_language(chan));
			}
			DISPOSE(fn, -1);

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
		snprintf(mailbox, sizeof(mailbox), "%s@%s", username, context);

		/* Send VoiceMail */
		memset(&leave_options, 0, sizeof(leave_options));
		leave_options.record_gain = record_gain;
		leave_options.beeptone = "beep";
		cmd = leave_voicemail(chan, mailbox, &leave_options);
	} else {
		/* Forward VoiceMail */
		long duration = 0;
		struct vm_state vmstmp;
		int copy_msg_result = 0;
#ifdef IMAP_STORAGE
		char filename[PATH_MAX];
		struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
		const char *msg_id = NULL;
		struct ast_config *msg_cfg;
#endif
		memcpy(&vmstmp, vms, sizeof(vmstmp));

		RETRIEVE(dir, curmsg, sender->mailbox, sender->context);
#ifdef IMAP_STORAGE
		make_file(filename, sizeof(filename), dir, curmsg);
		strncat(filename, ".txt", sizeof(filename) - strlen(filename) - 1);
		msg_cfg = ast_config_load(filename, config_flags);
		if (msg_cfg && msg_cfg == CONFIG_STATUS_FILEINVALID) {
			msg_id = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "msg_id"));
			ast_config_destroy(msg_cfg);
		}
#endif

		cmd = vm_forwardoptions(chan, sender, vmstmp.curdir, curmsg, vmfmts, S_OR(context, "default"), record_gain, &duration, &vmstmp, urgent_str);
		if (!cmd) {
			AST_LIST_TRAVERSE_SAFE_BEGIN(&extensions, vmtmp, list) {
#ifdef IMAP_STORAGE
				int attach_user_voicemail;
				char *myserveremail = serveremail;

				/* get destination mailbox */
				dstvms = get_vm_state_by_mailbox(vmtmp->mailbox, vmtmp->context, 0);
				if (!dstvms) {
					dstvms = create_vm_state_from_user(vmtmp);
				}
				if (dstvms) {
					init_mailstream(dstvms, 0);
					if (!dstvms->mailstream) {
						ast_log(AST_LOG_ERROR, "IMAP mailstream for %s is NULL\n", vmtmp->mailbox);
					} else {
						copy_msg_result = STORE(vmstmp.curdir, vmtmp->mailbox, vmtmp->context, curmsg, chan, vmtmp, fmt, duration, dstvms, urgent_str, msg_id);
						run_externnotify(vmtmp->context, vmtmp->mailbox, urgent_str);
					}
				} else {
					ast_log(AST_LOG_ERROR, "Could not find state information for mailbox %s\n", vmtmp->mailbox);
				}
				if (!ast_strlen_zero(vmtmp->serveremail))
					myserveremail = vmtmp->serveremail;
				attach_user_voicemail = ast_test_flag(vmtmp, VM_ATTACH);
				/* NULL category for IMAP storage */
				sendmail(myserveremail, vmtmp, todircount, vmtmp->context, vmtmp->mailbox,
					dstvms->curbox,
					S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL),
					S_COR(ast_channel_caller(chan)->id.name.valid, ast_channel_caller(chan)->id.name.str, NULL),
					vmstmp.fn, vmstmp.introfn, fmt, duration, attach_user_voicemail, chan,
					NULL, urgent_str, msg_id);
#else
				copy_msg_result = copy_message(chan, sender, 0, curmsg, duration, vmtmp, fmt, dir, urgent_str, NULL);
#endif
				saved_messages++;
				AST_LIST_REMOVE_CURRENT(list);
				inprocess_count(vmtmp->mailbox, vmtmp->context, -1);
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
				res = ast_play_and_wait(chan, "vm-msgforwarded");
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
	if ((res = ast_stream_and_wait(chan, file, AST_DIGIT_ANY)) < 0)
		ast_log(AST_LOG_WARNING, "Unable to play message %s\n", file);
	return res;
}

static int wait_file(struct ast_channel *chan, struct vm_state *vms, char *file)
{
	ast_test_suite_event_notify("PLAYVOICE", "Message: Playing %s", file);
	return ast_control_streamfile(chan, file, listen_control_forward_key, listen_control_reverse_key, listen_control_stop_key, listen_control_pause_key, listen_control_restart_key, skipms, NULL);
}

static int play_message_category(struct ast_channel *chan, const char *category)
{
	int res = 0;

	if (!ast_strlen_zero(category))
		res = ast_play_and_wait(chan, category);

	if (res) {
		ast_log(AST_LOG_WARNING, "No sound file for category '%s' was found.\n", category);
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
		ast_log(AST_LOG_WARNING, "Couldn't find origtime in %s\n", filename);
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
	ast_localtime(&tv_now, &time_then, NULL);

	/* Day difference */
	if (time_now.tm_year == time_then.tm_year)
		snprintf(temp, sizeof(temp), "%d", time_now.tm_yday);
	else
		snprintf(temp, sizeof(temp), "%d", (time_now.tm_year - time_then.tm_year) * 365 + (time_now.tm_yday - time_then.tm_yday));
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", temp);

	/* Can't think of how other diffs might be helpful, but I'm sure somebody will think of something. */
#endif
	if (the_zone) {
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), the_zone->msg_format, the_zone->timezone);
	} else if (!strncasecmp(ast_channel_language(chan), "de", 2)) {     /* GERMAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' Q 'digits/at' HM", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "gr", 2)) {     /* GREEK syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' q  H 'digits/kai' M ", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "is", 2)) {     /* ICELANDIC syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' Q 'digits/at' HM", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "it", 2)) {     /* ITALIAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' q 'digits/at' 'digits/hours' k 'digits/e' M 'digits/minutes'", NULL);
	} else if (!strcasecmp(ast_channel_language(chan),"ja")) {     /* Japanese syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "PHM q 'jp-ni' 'vm-received'", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "nl", 2)) {     /* DUTCH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' q 'digits/nl-om' HM", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "no", 2)) {     /* NORWEGIAN syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' Q 'digits/at' HM", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "pl", 2)) {     /* POLISH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' Q HM", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "pt_BR", 5)) {  /* Brazilian PORTUGUESE syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' Ad 'digits/pt-de' B 'digits/pt-de' Y 'digits/pt-as' HM ", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "se", 2)) {     /* SWEDISH syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' dB 'digits/at' k 'and' M", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "zh", 2)) {     /* CHINESE (Taiwan) syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "qR 'vm-received'", NULL);
	} else if (!strncasecmp(ast_channel_language(chan), "vi", 2)) {     /* VIETNAMESE syntax */
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' A 'digits/day' dB 'digits/year' Y 'digits/at' k 'hours' M 'minutes'", NULL);
	} else {
		res = ast_say_date_with_format(chan, t, AST_DIGIT_ANY, ast_channel_language(chan), "'vm-received' q 'digits/at' IMp", NULL);
	}
#if 0
	pbx_builtin_setvar_helper(chan, "DIFF_DAY", NULL);
#endif
	return res;
}



static int play_message_callerid(struct ast_channel *chan, struct vm_state *vms, char *cid, const char *context, int callback, int saycidnumber)
{
	int res = 0;
	int i;
	char *callerid, *name;
	char prefile[PATH_MAX] = "";

	/* If voicemail cid is not enabled, or we didn't get cid or context from
	 * the attribute file, leave now.
	 *
	 * TODO Still need to change this so that if this function is called by the
	 * message envelope (and someone is explicitly requesting to hear the CID),
	 * it does not check to see if CID is enabled in the config file.
	 */
	if ((cid == NULL)||(context == NULL))
		return res;

	/* Strip off caller ID number from name */
	ast_debug(1, "VM-CID: composite caller ID received: %s, context: %s\n", cid, context);
	ast_callerid_parse(cid, &name, &callerid);
	if ((!ast_strlen_zero(callerid)) && strcmp(callerid, "Unknown")) {
		/* Check for internal contexts and only */
		/* say extension when the call didn't come from an internal context in the list */
		for (i = 0 ; i < MAX_NUM_CID_CONTEXTS ; i++){
			ast_debug(1, "VM-CID: comparing internalcontext: %s\n", cidinternalcontexts[i]);
			if ((strcmp(cidinternalcontexts[i], context) == 0))
				break;
		}
		if (i != MAX_NUM_CID_CONTEXTS){ /* internal context? */
			if (!res) {
				snprintf(prefile, sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, context, callerid);
				if (!ast_strlen_zero(prefile)) {
					/* See if we can find a recorded name for this callerid
					 * and if found, use that instead of saying number. */
					if (ast_fileexists(prefile, NULL, NULL) > 0) {
						ast_verb(3, "Playing envelope info: CID number '%s' matches mailbox number, playing recorded name\n", callerid);
						if (!callback)
							res = wait_file2(chan, vms, "vm-from");
						res = ast_stream_and_wait(chan, prefile, "");
					} else {
						ast_verb(3, "Playing envelope info: message from '%s'\n", callerid);
						/* Say "from extension" as one saying to sound smoother */
						if (!callback)
							res = wait_file2(chan, vms, "vm-from-extension");
						res = ast_say_digit_str(chan, callerid, "", ast_channel_language(chan));
					}
				}
			}
		} else if (!res) {
			ast_debug(1, "VM-CID: Numeric caller id: (%s)\n", callerid);
			/* If there is a recording for this numeric callerid then play that */
			if (!callback) {
				/* See if we can find a recorded name for this person instead of their extension number */
				snprintf(prefile, sizeof(prefile), "%s/recordings/callerids/%s", ast_config_AST_SPOOL_DIR, callerid);
				if (!saycidnumber && ast_fileexists(prefile, NULL, NULL) > 0) {
					ast_verb(3, "Playing recorded name for CID number '%s' - '%s'\n", callerid,prefile);
					wait_file2(chan, vms, "vm-from");
					res = ast_stream_and_wait(chan, prefile, "");
					ast_verb(3, "Played recorded name result '%d'\n", res);
				} else {
					/* Since this is all nicely figured out, why not say "from phone number" in this case" */
					wait_file2(chan, vms, "vm-from-phonenumber");
					res = ast_say_digit_str(chan, callerid, AST_DIGIT_ANY, ast_channel_language(chan));
				}
			} else {
				res = ast_say_digit_str(chan, callerid, AST_DIGIT_ANY, ast_channel_language(chan));
			}
		}
	} else {
		/* Number unknown */
		ast_debug(1, "VM-CID: From an unknown number\n");
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
	durations = atoi(duration);
	durationm = (durations / 60);

	ast_debug(1, "VM-Duration: duration is: %d seconds converted to: %d minutes\n", durations, durationm);

	if ((!res) && (durationm >= minduration)) {
		res = wait_file2(chan, vms, "vm-duration");

		/* POLISH syntax */
		if (!strncasecmp(ast_channel_language(chan), "pl", 2)) {
			div_t num = div(durationm, 10);

			if (durationm == 1) {
				res = ast_play_and_wait(chan, "digits/1z");
				res = res ? res : ast_play_and_wait(chan, "vm-minute-ta");
			} else if (num.rem > 1 && num.rem < 5 && num.quot != 1) {
				if (num.rem == 2) {
					if (!num.quot) {
						res = ast_play_and_wait(chan, "digits/2-ie");
					} else {
						res = say_and_wait(chan, durationm - 2 , ast_channel_language(chan));
						res = res ? res : ast_play_and_wait(chan, "digits/2-ie");
					}
				} else {
					res = say_and_wait(chan, durationm, ast_channel_language(chan));
				}
				res = res ? res : ast_play_and_wait(chan, "vm-minute-ty");
			} else {
				res = say_and_wait(chan, durationm, ast_channel_language(chan));
				res = res ? res : ast_play_and_wait(chan, "vm-minute-t");
			}
		/* DEFAULT syntax */
		} else {
			res = ast_say_number(chan, durationm, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
			res = wait_file2(chan, vms, "vm-minutes");
		}
	}
	return res;
}

static int play_message(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms)
{
	int res = 0;
	char filename[PATH_MAX], *cid;
	const char *origtime, *context, *category, *duration, *flag;
	struct ast_config *msg_cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };

	vms->starting = 0;
	make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
	adsi_message(chan, vms);
	if (!vms->curmsg) {
		res = wait_file2(chan, vms, "vm-first");	/* "First" */
	} else if (vms->curmsg == vms->lastmsg) {
		res = wait_file2(chan, vms, "vm-last");		/* "last" */
	}

	snprintf(filename, sizeof(filename), "%s.txt", vms->fn);
	RETRIEVE(vms->curdir, vms->curmsg, vmu->mailbox, vmu->context);
	msg_cfg = ast_config_load(filename, config_flags);
	if (!valid_config(msg_cfg)) {
		ast_log(LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
		return 0;
	}
	flag = ast_variable_retrieve(msg_cfg, "message", "flag");

	/* Play the word urgent if we are listening to urgent messages */
	if (!ast_strlen_zero(flag) && !strcmp(flag, "Urgent")) {
		res = wait_file2(chan, vms, "vm-Urgent");	/* "urgent" */
	}

	if (!res) {
		/* XXX Why are we playing messages above, and then playing the same language-specific stuff here? */
		/* POLISH syntax */
		if (!strncasecmp(ast_channel_language(chan), "pl", 2)) {
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
		/* HEBREW syntax */
		} else if (!strncasecmp(ast_channel_language(chan), "he", 2)) {
			if (!vms->curmsg) {
				res = wait_file2(chan, vms, "vm-message");
				res = wait_file2(chan, vms, "vm-first");
			} else if (vms->curmsg == vms->lastmsg) {
				res = wait_file2(chan, vms, "vm-message");
				res = wait_file2(chan, vms, "vm-last");
			} else {
				res = wait_file2(chan, vms, "vm-message");
				res = wait_file2(chan, vms, "vm-number");
				res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, ast_channel_language(chan), "f");
			}
		/* ICELANDIC syntax */
		} else if (!strncasecmp(ast_channel_language(chan), "is", 2)) {
			res = wait_file2(chan, vms, "vm-message");
			if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
				res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, ast_channel_language(chan), "n");
			}
		/* VIETNAMESE syntax */
		} else if (!strncasecmp(ast_channel_language(chan), "vi", 2)) {
			if (!vms->curmsg) {
				res = wait_file2(chan, vms, "vm-message");
				res = wait_file2(chan, vms, "vm-first");
			} else if (vms->curmsg == vms->lastmsg) {
				res = wait_file2(chan, vms, "vm-message");
				res = wait_file2(chan, vms, "vm-last");
			} else {
				res = wait_file2(chan, vms, "vm-message");
				res = wait_file2(chan, vms, "vm-number");
				res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, ast_channel_language(chan), "f");
			}
		} else {
			if (!strncasecmp(ast_channel_language(chan), "se", 2)) { /* SWEDISH syntax */
				res = wait_file2(chan, vms, "vm-meddelandet");  /* "message" */
			} else { /* DEFAULT syntax */
				res = wait_file2(chan, vms, "vm-message");
			}
			if (vms->curmsg && (vms->curmsg != vms->lastmsg)) {
				if (!res) {
					ast_test_suite_event_notify("PLAYBACK", "Message: message number");
					res = ast_say_number(chan, vms->curmsg + 1, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
				}
			}
		}
	}

	if (!valid_config(msg_cfg)) {
		ast_log(AST_LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
		return 0;
	}

	if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime"))) {
		ast_log(AST_LOG_WARNING, "No origtime?!\n");
		DISPOSE(vms->curdir, vms->curmsg);
		ast_config_destroy(msg_cfg);
		return 0;
	}

	cid = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "callerid"));
	duration = ast_variable_retrieve(msg_cfg, "message", "duration");
	category = ast_variable_retrieve(msg_cfg, "message", "category");

	context = ast_variable_retrieve(msg_cfg, "message", "context");
	if (!res) {
		res = play_message_category(chan, category);
	}
	if ((!res) && (ast_test_flag(vmu, VM_ENVELOPE))) {
		res = play_message_datetime(chan, vmu, origtime, filename);
	}
	if ((!res) && (ast_test_flag(vmu, VM_SAYCID))) {
		res = play_message_callerid(chan, vms, cid, context, 0, 0);
	}
	if ((!res) && (ast_test_flag(vmu, VM_SAYDURATION))) {
		res = play_message_duration(chan, vms, duration, vmu->saydurationm);
	}
	/* Allow pressing '1' to skip envelope / callerid */
	if (res == '1') {
		ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c", res, res);
		res = 0;
	}
	ast_config_destroy(msg_cfg);

	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
#ifdef IMAP_STORAGE
		ast_mutex_lock(&vms->lock);
#endif
		vms->heard[vms->curmsg] = 1;
#ifdef IMAP_STORAGE
		ast_mutex_unlock(&vms->lock);
		/*IMAP storage stores any prepended message from a forward
		 * as a separate file from the rest of the message
		 */
		if (!ast_strlen_zero(vms->introfn) && ast_fileexists(vms->introfn, NULL, NULL) > 0) {
			wait_file(chan, vms, vms->introfn);
		}
#endif
		if ((res = wait_file(chan, vms, vms->fn)) < 0) {
			ast_log(AST_LOG_WARNING, "Playback of message %s failed\n", vms->fn);
			res = 0;
		}
		ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
			isprint(res) ? res : '?', isprint(res) ? res : '?');
	}
	DISPOSE(vms->curdir, vms->curmsg);
	return res;
}

#ifdef IMAP_STORAGE
static int imap_remove_file(char *dir, int msgnum)
{
	char fn[PATH_MAX];
	char full_fn[PATH_MAX];
	char intro[PATH_MAX] = {0,};

	if (msgnum > -1) {
		make_file(fn, sizeof(fn), dir, msgnum);
		snprintf(intro, sizeof(intro), "%sintro", fn);
	} else
		ast_copy_string(fn, dir, sizeof(fn));

	if ((msgnum < 0 && imapgreetings) || msgnum > -1) {
		ast_filedelete(fn, NULL);
		if (!ast_strlen_zero(intro)) {
			ast_filedelete(intro, NULL);
		}
		snprintf(full_fn, sizeof(full_fn), "%s.txt", fn);
		unlink(full_fn);
	}
	return 0;
}



static int imap_delete_old_greeting (char *dir, struct vm_state *vms)
{
	char *file, *filename;
	char arg[11];
	int i;
	BODY* body;
	int curr_mbox;

	file = strrchr(ast_strdupa(dir), '/');
	if (file) {
		*file++ = '\0';
	} else {
		ast_log(AST_LOG_ERROR, "Failed to procure file name from directory passed. You should never see this.\n");
		return -1;
	}

	ast_mutex_lock(&vms->lock);

	/* get the current mailbox so that we can point the mailstream back to it later */
	curr_mbox = get_folder_by_name(vms->curbox);

	if (init_mailstream(vms, GREETINGS_FOLDER) || !vms->mailstream) {
		ast_log(AST_LOG_ERROR, "IMAP mailstream is NULL or can't init_mailstream\n");
		ast_mutex_unlock(&vms->lock);
		return -1;
	}

	for (i = 0; i < vms->mailstream->nmsgs; i++) {
		mail_fetchstructure(vms->mailstream, i + 1, &body);
		/* We have the body, now we extract the file name of the first attachment. */
		if (body->nested.part->next && body->nested.part->next->body.parameter->value) {
			char *attachment = body->nested.part->next->body.parameter->value;
			char copy[strlen(attachment) + 1];

			strcpy(copy, attachment); /* safe */
			attachment = copy;

			filename = strsep(&attachment, ".");
			if (!strcmp(filename, file)) {
				snprintf(arg, sizeof(arg), "%d", i + 1);
				mail_setflag(vms->mailstream, arg, "\\DELETED");
			}
		} else {
			ast_log(AST_LOG_ERROR, "There is no file attached to this IMAP message.\n");
			ast_mutex_unlock(&vms->lock);
			return -1;
		}
	}
	mail_expunge(vms->mailstream);

	if (curr_mbox != -1) {
		/* restore previous mbox stream */
		if (init_mailstream(vms, curr_mbox) || !vms->mailstream) {
			ast_log(AST_LOG_ERROR, "IMAP mailstream is NULL or can't init_mailstream\n");
		}
	}

	ast_mutex_unlock(&vms->lock);
	return 0;
}

#elif !defined(IMAP_STORAGE)
static int open_mailbox(struct vm_state *vms, struct ast_vm_user *vmu, int box)
{
	int count_msg, last_msg;

	ast_copy_string(vms->curbox, mbox(vmu, box), sizeof(vms->curbox));

	/* Rename the member vmbox HERE so that we don't try to return before
	 * we know what's going on.
	 */
	snprintf(vms->vmbox, sizeof(vms->vmbox), "vm-%s", vms->curbox);

	/* Faster to make the directory than to check if it exists. */
	create_dirpath(vms->curdir, sizeof(vms->curdir), vmu->context, vms->username, vms->curbox);

	/* traverses directory using readdir (or select query for ODBC) */
	count_msg = count_messages(vmu, vms->curdir);
	if (count_msg < 0) {
		return count_msg;
	} else {
		vms->lastmsg = count_msg - 1;
	}

	if (vm_allocate_dh(vms, vmu, count_msg)) {
		return -1;
	}

	/*
	The following test is needed in case sequencing gets messed up.
	There appears to be more than one way to mess up sequence, so
	we will not try to find all of the root causes--just fix it when
	detected.
	*/

	if (vm_lock_path(vms->curdir)) {
		ast_log(AST_LOG_ERROR, "Could not open mailbox %s:  mailbox is locked\n", vms->curdir);
		return ERROR_LOCK_PATH;
	}

	/* for local storage, checks directory for messages up to maxmsg limit */
	last_msg = last_message_index(vmu, vms->curdir);
	ast_unlock_path(vms->curdir);

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
	int last_msg_idx = 0;

#ifndef IMAP_STORAGE
	int res = 0, nummsg;
	char fn2[PATH_MAX];
#endif

	if (vms->lastmsg <= -1) {
		goto done;
	}

	vms->curmsg = -1;
#ifndef IMAP_STORAGE
	/* Get the deleted messages fixed */
	if (vm_lock_path(vms->curdir)) {
		return ERROR_LOCK_PATH;
	}

	/* update count as message may have arrived while we've got mailbox open */
	last_msg_idx = last_message_index(vmu, vms->curdir);
	if (last_msg_idx != vms->lastmsg) {
		ast_log(AST_LOG_NOTICE, "%d messages received after mailbox opened.\n", last_msg_idx - vms->lastmsg);
	}

	/* must check up to last detected message, just in case it is erroneously greater than maxmsg */
	for (x = 0; x < last_msg_idx + 1; x++) {
		if (!vms->deleted[x] && ((strcasecmp(vms->curbox, "INBOX") && strcasecmp(vms->curbox, "Urgent")) || !vms->heard[x] || (vms->heard[x] && !ast_test_flag(vmu, VM_MOVEHEARD)))) {
			/* Save this message.  It's not in INBOX or hasn't been heard */
			make_file(vms->fn, sizeof(vms->fn), vms->curdir, x);
			if (!EXISTS(vms->curdir, x, vms->fn, NULL)) {
				break;
			}
			vms->curmsg++;
			make_file(fn2, sizeof(fn2), vms->curdir, vms->curmsg);
			if (strcmp(vms->fn, fn2)) {
				RENAME(vms->curdir, x, vmu->mailbox, vmu->context, vms->curdir, vms->curmsg, vms->fn, fn2);
			}
		} else if ((!strcasecmp(vms->curbox, "INBOX") || !strcasecmp(vms->curbox, "Urgent")) && vms->heard[x] && ast_test_flag(vmu, VM_MOVEHEARD) && !vms->deleted[x]) {
			/* Move to old folder before deleting */
			res = save_to_folder(vmu, vms, x, 1, NULL, 0);
			if (res == ERROR_LOCK_PATH || res == ERROR_MAX_MSGS) {
				/* If save failed do not delete the message */
				ast_log(AST_LOG_WARNING, "Save failed.  Not moving message: %s.\n", res == ERROR_LOCK_PATH ? "unable to lock path" : "destination folder full");
				vms->deleted[x] = 0;
				vms->heard[x] = 0;
				--x;
			}
		} else if (vms->deleted[x] && vmu->maxdeletedmsg) {
			/* Move to deleted folder */
			res = save_to_folder(vmu, vms, x, 10, NULL, 0);
			if (res == ERROR_LOCK_PATH) {
				/* If save failed do not delete the message */
				vms->deleted[x] = 0;
				vms->heard[x] = 0;
				--x;
			}
		} else if (vms->deleted[x] && ast_check_realtime("voicemail_data")) {
			/* If realtime storage enabled - we should explicitly delete this message,
			cause RENAME() will overwrite files, but will keep duplicate records in RT-storage */
			make_file(vms->fn, sizeof(vms->fn), vms->curdir, x);
			if (EXISTS(vms->curdir, x, vms->fn, NULL)) {
				DELETE(vms->curdir, x, vms->fn, vmu);
			}
		}
	}

	/* Delete ALL remaining messages */
	nummsg = x - 1;
	for (x = vms->curmsg + 1; x <= nummsg; x++) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, x);
		if (EXISTS(vms->curdir, x, vms->fn, NULL)) {
			DELETE(vms->curdir, x, vms->fn, vmu);
		}
	}
	ast_unlock_path(vms->curdir);
#else /* defined(IMAP_STORAGE) */
	ast_mutex_lock(&vms->lock);
	if (vms->deleted) {
		/* Since we now expunge after each delete, deleting in reverse order
		 * ensures that no reordering occurs between each step. */
		last_msg_idx = vms->dh_arraysize;
		for (x = last_msg_idx - 1; x >= 0; x--) {
			if (vms->deleted[x]) {
				ast_debug(3, "IMAP delete of %d\n", x);
				DELETE(vms->curdir, x, vms->fn, vmu);
			}
		}
	}
#endif

done:
	if (vms->deleted) {
		ast_free(vms->deleted);
		vms->deleted = NULL;
	}
	if (vms->heard) {
		ast_free(vms->heard);
		vms->heard = NULL;
	}
	vms->dh_arraysize = 0;
#ifdef IMAP_STORAGE
	ast_mutex_unlock(&vms->lock);
#endif

	return 0;
}

/* In Greek even though we CAN use a syntax like "friends messages"
 * ("filika mynhmata") it is not elegant. This also goes for "work/family messages"
 * ("ergasiaka/oikogeniaka mynhmata"). Therefore it is better to use a reversed
 * syntax for the above three categories which is more elegant.
 */

static int vm_play_folder_name_gr(struct ast_channel *chan, char *box)
{
	int cmd;
	char *buf;

	buf = ast_alloca(strlen(box) + 2);
	strcpy(buf, box);
	strcat(buf, "s");

	if (!strcasecmp(box, "vm-INBOX") || !strcasecmp(box, "vm-Old")){
		cmd = ast_play_and_wait(chan, buf); /* "NEA / PALIA" */
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages"); /* "messages" -> "MYNHMATA" */
	} else {
		cmd = ast_play_and_wait(chan, "vm-messages"); /* "messages" -> "MYNHMATA" */
		return cmd ? cmd : ast_play_and_wait(chan, box); /* friends/family/work... -> "FILWN"/"OIKOGENIAS"/"DOULEIAS"*/
	}
}

static int vm_play_folder_name_ja(struct ast_channel *chan, char *box)
{
	int cmd;

	if (!strcasecmp(box, "vm-INBOX") || !strcasecmp(box, "vm-Old")) {
		cmd = ast_play_and_wait(chan, box);
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages");
	} else {
		cmd = ast_play_and_wait(chan, box);
		return cmd;
	}
}

static int vm_play_folder_name_pl(struct ast_channel *chan, char *box)
{
	int cmd;

	if (!strcasecmp(box, "vm-INBOX") || !strcasecmp(box, "vm-Old")) {
		if (!strcasecmp(box, "vm-INBOX"))
			cmd = ast_play_and_wait(chan, "vm-new-e");
		else
			cmd = ast_play_and_wait(chan, "vm-old-e");
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages");
	} else {
		cmd = ast_play_and_wait(chan, "vm-messages");
		return cmd ? cmd : ast_play_and_wait(chan, box);
	}
}

static int vm_play_folder_name_ua(struct ast_channel *chan, char *box)
{
	int cmd;

	if (!strcasecmp(box, "vm-Family") || !strcasecmp(box, "vm-Friends") || !strcasecmp(box, "vm-Work")){
		cmd = ast_play_and_wait(chan, "vm-messages");
		return cmd ? cmd : ast_play_and_wait(chan, box);
	} else {
		cmd = ast_play_and_wait(chan, box);
		return cmd ? cmd : ast_play_and_wait(chan, "vm-messages");
	}
}

static int vm_play_folder_name(struct ast_channel *chan, char *box)
{
	int cmd;

	if (  !strncasecmp(ast_channel_language(chan), "it", 2) ||
		  !strncasecmp(ast_channel_language(chan), "es", 2) ||
		  !strncasecmp(ast_channel_language(chan), "pt", 2)) { /* Italian, Spanish, or Portuguese syntax */
		cmd = ast_play_and_wait(chan, "vm-messages"); /* "messages */
		return cmd ? cmd : ast_play_and_wait(chan, box);
	} else if (!strncasecmp(ast_channel_language(chan), "gr", 2)) {
		return vm_play_folder_name_gr(chan, box);
	} else if (!strncasecmp(ast_channel_language(chan), "he", 2)) {  /* Hebrew syntax */
		return ast_play_and_wait(chan, box);
	} else if (!strncasecmp(ast_channel_language(chan), "ja", 2)) {  /* Japanese syntax */
		return vm_play_folder_name_ja(chan, box);
	} else if (!strncasecmp(ast_channel_language(chan), "pl", 2)) {
		return vm_play_folder_name_pl(chan, box);
	} else if (!strncasecmp(ast_channel_language(chan), "ua", 2)) {  /* Ukrainian syntax */
		return vm_play_folder_name_ua(chan, box);
	} else if (!strncasecmp(ast_channel_language(chan), "vi", 2)) {
		return ast_play_and_wait(chan, box);
	} else {  /* Default English */
		cmd = ast_play_and_wait(chan, box);
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
			res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
		if (!res) {
			if (vms->newmessages == 1) {
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
			res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, ast_channel_language(chan), NULL);
		if (vms->oldmessages == 1){
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
 *  4) Pass the gender of the language's word for "message" as an argument to
 *     this function which is can in turn pass on to the functions which
 *     say numbers and put endings on nouns and adjectives.
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

		if (!(res = ast_say_number(chan, lastnum, AST_DIGIT_ANY, ast_channel_language(chan), message_gender))) {
			res = ast_say_counted_adjective(chan, lastnum, "vm-new", message_gender);
		}

		if (!res && vms->oldmessages) {
			res = ast_play_and_wait(chan, "vm-and");
		}
	}

	if (!res && vms->oldmessages) {
		lastnum = vms->oldmessages;

		if (!(res = ast_say_number(chan, lastnum, AST_DIGIT_ANY, ast_channel_language(chan), message_gender))) {
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
	int res = 0;

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
						res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
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
						res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
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
						res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
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

/* Japanese syntax */
static int vm_intro_ja(struct ast_channel *chan,struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	if (vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-INBOX");
		if (!res)
			res = ast_play_and_wait(chan, "vm-message");
		if (!res)
			res = ast_play_and_wait(chan, "jp-ga");
		if (!res)
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
		if (vms->oldmessages && !res)
			res = ast_play_and_wait(chan, "silence/1");

	}
	if (vms->oldmessages) {
		res = ast_play_and_wait(chan, "vm-Old");
		if (!res)
			res = ast_play_and_wait(chan, "vm-message");
		if (!res)
			res = ast_play_and_wait(chan, "jp-ga");
		if (!res)
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
	}
	if (!vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-messages");
		if (!res)
			res = ast_play_and_wait(chan, "jp-wa");
		if (!res)
			res = ast_play_and_wait(chan, "jp-arimasen");
	}
	else {
		res = ast_play_and_wait(chan, "jp-arimasu");
	}
	return res;
} /* Japanese */

/* Default English syntax */
static int vm_intro_en(struct ast_channel *chan, struct vm_state *vms)
{
	int res;

	/* Introduce messages they have */
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->urgentmessages) {
			res = say_and_wait(chan, vms->urgentmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-Urgent");
			if ((vms->oldmessages || vms->newmessages) && !res) {
				res = ast_play_and_wait(chan, "vm-and");
			} else if (!res) {
				if (vms->urgentmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}

		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
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
			if (!vms->urgentmessages && !vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* ICELANDIC syntax */
static int vm_intro_is(struct ast_channel *chan, struct vm_state *vms)
{
	int res;

	/* Introduce messages they have */
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->urgentmessages) {
			/* Digits 1-4 are spoken in neutral and plural when talking about messages,
			   however, feminine is used for 1 as it is the same as the neutral for plural,
			   and singular neutral is the same after 1. */
			if (vms->urgentmessages < 5) {
				char recname[16];
				if (vms->urgentmessages == 1)
					snprintf(recname, sizeof(recname), "digits/1kvk");
				else
					snprintf(recname, sizeof(recname), "digits/%dhk", vms->urgentmessages);
				res = ast_play_and_wait(chan, recname);
			} else if (!res)
				res = ast_play_and_wait(chan, "vm-Urgent");
			if ((vms->oldmessages || vms->newmessages) && !res) {
				res = ast_play_and_wait(chan, "vm-and");
			} else if (!res)
				res = ast_play_and_wait(chan, "vm-messages");
		}
		if (vms->newmessages) {
			if (vms->newmessages < 5) {
				char recname[16];
				if (vms->newmessages == 1)
					snprintf(recname, sizeof(recname), "digits/1kvk");
				else
					snprintf(recname, sizeof(recname), "digits/%dhk", vms->newmessages);
				res = ast_play_and_wait(chan, recname);
			} else
				res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res)
				res = ast_play_and_wait(chan, "vm-messages");
		}
		if (!res && vms->oldmessages) {
			if (vms->oldmessages < 5) {
				char recname[16];
				if (vms->oldmessages == 1)
					snprintf(recname, sizeof(recname), "digits/1kvk");
				else
					snprintf(recname, sizeof(recname), "digits/%dhk", vms->oldmessages);
				res = ast_play_and_wait(chan, recname);
			} else
				res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
			if (!res)
				res = ast_play_and_wait(chan, "vm-messages");
		}
		if (!res) {
			if (!vms->urgentmessages && !vms->oldmessages && !vms->newmessages) {
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
	if (!vms->oldmessages && !vms->newmessages &&!vms->urgentmessages)
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
			say_and_wait(chan, vms->newmessages, ast_channel_language(chan)) ||
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
			say_and_wait(chan, vms->oldmessages, ast_channel_language(chan)) ||
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
					res = say_and_wait(chan, vms->newmessages - 2 , ast_channel_language(chan));
					res = res ? res : ast_play_and_wait(chan, "digits/2-ie");
				}
			} else {
				res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			}
			res = res ? res : ast_play_and_wait(chan, "vm-new-e");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		} else {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
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
					res = say_and_wait(chan, vms->oldmessages - 2 , ast_channel_language(chan));
					res = res ? res : ast_play_and_wait(chan, "digits/2-ie");
				}
			} else {
				res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			}
			res = res ? res : ast_play_and_wait(chan, "vm-old-e");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		} else {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
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

	if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
		res = ast_play_and_wait(chan, "vm-no");
		res = res ? res : ast_play_and_wait(chan, "vm-messages");
		return res;
	}

	if (vms->newmessages) {
		if (vms->newmessages == 1) {
			res = ast_play_and_wait(chan, "digits/ett");
			res = res ? res : ast_play_and_wait(chan, "vm-nytt");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
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
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			res = res ? res : ast_play_and_wait(chan, "vm-gamla");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
	}

	return res;
}

/* NORWEGIAN syntax */
static int vm_intro_no(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;

	res = ast_play_and_wait(chan, "vm-youhave");
	if (res)
		return res;

	if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
		res = ast_play_and_wait(chan, "vm-no");
		res = res ? res : ast_play_and_wait(chan, "vm-messages");
		return res;
	}

	if (vms->newmessages) {
		if (vms->newmessages == 1) {
			res = ast_play_and_wait(chan, "digits/1");
			res = res ? res : ast_play_and_wait(chan, "vm-ny");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
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
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			res = res ? res : ast_play_and_wait(chan, "vm-gamle");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
	}

	return res;
}

/* Danish syntax */
static int vm_intro_da(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;

	res = ast_play_and_wait(chan, "vm-youhave");
	if (res)
		return res;

	if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
		res = ast_play_and_wait(chan, "vm-no");
		res = res ? res : ast_play_and_wait(chan, "vm-messages");
		return res;
	}

	if (vms->newmessages) {
		if ((vms->newmessages == 1)) {
			res = ast_play_and_wait(chan, "digits/1");
			res = res ? res : ast_play_and_wait(chan, "vm-INBOX");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			res = res ? res : ast_play_and_wait(chan, "vm-INBOXs");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
		if (!res && vms->oldmessages)
			res = ast_play_and_wait(chan, "vm-and");
	}
	if (!res && vms->oldmessages) {
		if (vms->oldmessages == 1) {
			res = ast_play_and_wait(chan, "digits/1");
			res = res ? res : ast_play_and_wait(chan, "vm-Old");
			res = res ? res : ast_play_and_wait(chan, "vm-message");
		} else {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			res = res ? res : ast_play_and_wait(chan, "vm-Olds");
			res = res ? res : ast_play_and_wait(chan, "vm-messages");
		}
	}

	return res;
}


/* GERMAN syntax */
static int vm_intro_de(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			if (vms->newmessages == 1)
				res = ast_play_and_wait(chan, "digits/1F");
			else
				res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}

		}
		if (!res && vms->oldmessages) {
			if (vms->oldmessages == 1)
				res = ast_play_and_wait(chan, "digits/1F");
			else
				res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
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
			if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* SPANISH syntax */
static int vm_intro_es(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
		res = ast_play_and_wait(chan, "vm-youhaveno");
		if (!res)
			res = ast_play_and_wait(chan, "vm-messages");
	} else {
		res = ast_play_and_wait(chan, "vm-youhave");
	}
	if (!res) {
		if (vms->newmessages) {
			if (!res) {
				if (vms->newmessages == 1) {
					res = ast_play_and_wait(chan, "digits/1M");
					if (!res)
						res = ast_play_and_wait(chan, "vm-message");
					if (!res)
						res = ast_play_and_wait(chan, "vm-INBOXs");
				} else {
					res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
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
					res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
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
static int vm_intro_pt_BR(struct ast_channel *chan, struct vm_state *vms) {
	/* Introduce messages they have */
	int res;
	if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
		res = ast_play_and_wait(chan, "vm-nomessages");
		return res;
	} else {
		res = ast_play_and_wait(chan, "vm-youhave");
	}
	if (vms->newmessages) {
		if (!res)
			res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
		if (vms->newmessages == 1) {
			if (!res)
				res = ast_play_and_wait(chan, "vm-message");
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOXs");
		} else {
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
			res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
		if (vms->oldmessages == 1) {
			if (!res)
				res = ast_play_and_wait(chan, "vm-message");
			if (!res)
				res = ast_play_and_wait(chan, "vm-Olds");
		} else {
			if (!res)
				res = ast_play_and_wait(chan, "vm-messages");
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
		}
	}
	return res;
}

/* FRENCH syntax */
static int vm_intro_fr(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}

		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
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
			if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* DUTCH syntax */
static int vm_intro_nl(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-INBOXs");
				else
					res = ast_play_and_wait(chan, "vm-INBOX");
			}
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-message");
				else
					res = ast_play_and_wait(chan, "vm-messages");
			}

		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
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
			if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-messages");
			}
		}
	}
	return res;
}

/* PORTUGUESE syntax */
static int vm_intro_pt(struct ast_channel *chan, struct vm_state *vms)
{
	/* Introduce messages they have */
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = ast_say_number(chan, vms->newmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
			if (!res) {
				if (vms->newmessages == 1) {
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
			res = ast_say_number(chan, vms->oldmessages, AST_DIGIT_ANY, ast_channel_language(chan), "f");
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
			if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
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

static int vm_intro_cs(struct ast_channel *chan, struct vm_state *vms)
{
	int res;
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			if (vms->newmessages == 1) {
				res = ast_play_and_wait(chan, "digits/jednu");
			} else {
				res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			}
			if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-novou");
				if ((vms->newmessages) > 1 && (vms->newmessages < 5))
					res = ast_play_and_wait(chan, "vm-nove");
				if (vms->newmessages > 4)
					res = ast_play_and_wait(chan, "vm-novych");
			}
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
			else if (!res) {
				if (vms->newmessages == 1)
					res = ast_play_and_wait(chan, "vm-zpravu");
				if ((vms->newmessages) > 1 && (vms->newmessages < 5))
					res = ast_play_and_wait(chan, "vm-zpravy");
				if (vms->newmessages > 4)
					res = ast_play_and_wait(chan, "vm-zprav");
			}
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-starou");
				if ((vms->oldmessages) > 1 && (vms->oldmessages < 5))
					res = ast_play_and_wait(chan, "vm-stare");
				if (vms->oldmessages > 4)
					res = ast_play_and_wait(chan, "vm-starych");
			}
			if (!res) {
				if (vms->oldmessages == 1)
					res = ast_play_and_wait(chan, "vm-zpravu");
				if ((vms->oldmessages) > 1 && (vms->oldmessages < 5))
					res = ast_play_and_wait(chan, "vm-zpravy");
				if (vms->oldmessages > 4)
					res = ast_play_and_wait(chan, "vm-zprav");
			}
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages && !vms->urgentmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-zpravy");
			}
		}
	}
	return res;
}

/* CHINESE (Taiwan) syntax */
static int vm_intro_zh(struct ast_channel *chan, struct vm_state *vms)
{
	int res;
	/* Introduce messages they have */
	res = ast_play_and_wait(chan, "vm-you");

	if (!res && vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-have");
		if (!res)
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
		if (!res)
			res = ast_play_and_wait(chan, "vm-tong");
		if (!res)
			res = ast_play_and_wait(chan, "vm-INBOX");
		if (vms->oldmessages && !res)
			res = ast_play_and_wait(chan, "vm-and");
		else if (!res)
			res = ast_play_and_wait(chan, "vm-messages");
	}
	if (!res && vms->oldmessages) {
		res = ast_play_and_wait(chan, "vm-have");
		if (!res)
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
		if (!res)
			res = ast_play_and_wait(chan, "vm-tong");
		if (!res)
			res = ast_play_and_wait(chan, "vm-Old");
		if (!res)
			res = ast_play_and_wait(chan, "vm-messages");
	}
	if (!res && !vms->oldmessages && !vms->newmessages) {
		res = ast_play_and_wait(chan, "vm-haveno");
		if (!res)
			res = ast_play_and_wait(chan, "vm-messages");
	}
	return res;
}

/* Vietnamese syntax */
static int vm_intro_vi(struct ast_channel *chan, struct vm_state *vms)
{
	int res;

	/* Introduce messages they have */
	res = ast_play_and_wait(chan, "vm-youhave");
	if (!res) {
		if (vms->newmessages) {
			res = say_and_wait(chan, vms->newmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-INBOX");
			if (vms->oldmessages && !res)
				res = ast_play_and_wait(chan, "vm-and");
		}
		if (!res && vms->oldmessages) {
			res = say_and_wait(chan, vms->oldmessages, ast_channel_language(chan));
			if (!res)
				res = ast_play_and_wait(chan, "vm-Old");
		}
		if (!res) {
			if (!vms->oldmessages && !vms->newmessages) {
				res = ast_play_and_wait(chan, "vm-no");
				if (!res)
					res = ast_play_and_wait(chan, "vm-message");
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
		RETRIEVE(prefile, -1, vmu->mailbox, vmu->context);
		if (ast_fileexists(prefile, NULL, NULL) > 0) {
			ast_play_and_wait(chan, "vm-tempgreetactive");
		}
		DISPOSE(prefile, -1);
	}

	/* Play voicemail intro - syntax is different for different languages */
	if (0) {
		return 0;
	} else if (!strncasecmp(ast_channel_language(chan), "cs", 2)) {  /* CZECH syntax */
		return vm_intro_cs(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "cz", 2)) {  /* deprecated CZECH syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "cz is not a standard language code.  Please switch to using cs instead.\n");
		}
		return vm_intro_cs(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "de", 2)) {  /* GERMAN syntax */
		return vm_intro_de(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "es", 2)) {  /* SPANISH syntax */
		return vm_intro_es(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "fr", 2)) {  /* FRENCH syntax */
		return vm_intro_fr(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "gr", 2)) {  /* GREEK syntax */
		return vm_intro_gr(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "he", 2)) {  /* HEBREW syntax */
		return vm_intro_he(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "is", 2)) {  /* ICELANDIC syntax */
		return vm_intro_is(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "it", 2)) {  /* ITALIAN syntax */
		return vm_intro_it(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "ja", 2)) {  /* JAPANESE syntax */
		return vm_intro_ja(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "nl", 2)) {  /* DUTCH syntax */
		return vm_intro_nl(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "no", 2)) {  /* NORWEGIAN syntax */
		return vm_intro_no(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "da", 2)) {  /* DANISH syntax */
		return vm_intro_da(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "pl", 2)) {  /* POLISH syntax */
		return vm_intro_pl(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "pt_BR", 5)) {  /* BRAZILIAN PORTUGUESE syntax */
		return vm_intro_pt_BR(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "pt", 2)) {  /* PORTUGUESE syntax */
		return vm_intro_pt(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "ru", 2)) {  /* RUSSIAN syntax */
		return vm_intro_multilang(chan, vms, "n");
	} else if (!strncasecmp(ast_channel_language(chan), "se", 2)) {  /* SWEDISH syntax */
		return vm_intro_se(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "ua", 2)) {  /* UKRAINIAN syntax */
		return vm_intro_multilang(chan, vms, "n");
	} else if (!strncasecmp(ast_channel_language(chan), "vi", 2)) { /* VIETNAMESE syntax */
		return vm_intro_vi(chan, vms);
	} else if (!strncasecmp(ast_channel_language(chan), "zh", 2)) { /* CHINESE (Taiwan) syntax */
		return vm_intro_zh(chan, vms);
	} else {                                             /* Default to ENGLISH */
		return vm_intro_en(chan, vms);
	}
}

static int vm_instructions_en(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int skipadvanced, int in_urgent, int nodelete)
{
	int res = 0;
	/* Play instructions and wait for new command */
	while (!res) {
		if (vms->starting) {
			if (vms->lastmsg > -1) {
				if (skipadvanced)
					res = ast_play_and_wait(chan, "vm-onefor-full");
				else
					res = ast_play_and_wait(chan, "vm-onefor");
				if (!res)
					res = vm_play_folder_name(chan, vms->vmbox);
			}
			if (!res) {
				if (skipadvanced)
					res = ast_play_and_wait(chan, "vm-opts-full");
				else
					res = ast_play_and_wait(chan, "vm-opts");
			}
		} else {
			/* Added for additional help */
			if (skipadvanced) {
				res = ast_play_and_wait(chan, "vm-onefor-full");
				if (!res)
					res = vm_play_folder_name(chan, vms->vmbox);
				res = ast_play_and_wait(chan, "vm-opts-full");
			}
			/* Logic:
			 * If the current message is not the first OR
			 * if we're listening to the first new message and there are
			 * also urgent messages, then prompt for navigation to the
			 * previous message
			 */
			if (vms->curmsg || (!in_urgent && vms->urgentmessages > 0) || (ast_test_flag(vmu, VM_MESSAGEWRAP) && vms->lastmsg > 0)) {
				res = ast_play_and_wait(chan, "vm-prev");
			}
			if (!res && !skipadvanced)
				res = ast_play_and_wait(chan, "vm-advopts");
			if (!res)
				res = ast_play_and_wait(chan, "vm-repeat");
			/* Logic:
			 * If we're not listening to the last message OR
			 * we're listening to the last urgent message and there are
			 * also new non-urgent messages, then prompt for navigation
			 * to the next message
			 */
			if (!res && ((vms->curmsg != vms->lastmsg) || (in_urgent && vms->newmessages > 0) ||
				(ast_test_flag(vmu, VM_MESSAGEWRAP) && vms->lastmsg > 0) )) {
				res = ast_play_and_wait(chan, "vm-next");
			}
			if (!res) {
				int curmsg_deleted;
#ifdef IMAP_STORAGE
				ast_mutex_lock(&vms->lock);
#endif
				curmsg_deleted = vms->deleted[vms->curmsg];
#ifdef IMAP_STORAGE
				ast_mutex_unlock(&vms->lock);
#endif
				if (!nodelete) {
					if (!curmsg_deleted) {
						res = ast_play_and_wait(chan, "vm-delete");
					} else {
						res = ast_play_and_wait(chan, "vm-undelete");
					}
				}
				if (!res) {
					res = ast_play_and_wait(chan, "vm-toforward");
				}
				if (!res) {
					res = ast_play_and_wait(chan, "vm-savemessage");
				}
			}
		}
		if (!res) {
			res = ast_play_and_wait(chan, "vm-helpexit");
		}
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

static int vm_instructions_ja(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms,  int skipadvanced, int in_urgent, int nodelete)
{
	int res = 0;
	/* Play instructions and wait for new command */
	while (!res) {
		if (vms->starting) {
			if (vms->lastmsg > -1) {
				res = vm_play_folder_name(chan, vms->vmbox);
				if (!res)
					res = ast_play_and_wait(chan, "jp-wa");
				if (!res)
					res = ast_play_and_wait(chan, "digits/1");
				if (!res)
					res = ast_play_and_wait(chan, "jp-wo");
				if (!res)
					res = ast_play_and_wait(chan, "silence/1");
			}
			if (!res)
				res = ast_play_and_wait(chan, "vm-opts");
		} else {
			/* Added for additional help */
			if (skipadvanced) {
				res = vm_play_folder_name(chan, vms->vmbox);
				if (!res)
					res = ast_play_and_wait(chan, "jp-wa");
				if (!res)
					res = ast_play_and_wait(chan, "digits/1");
				if (!res)
					res = ast_play_and_wait(chan, "jp-wo");
				if (!res)
					res = ast_play_and_wait(chan, "silence/1");
				res = ast_play_and_wait(chan, "vm-opts-full");
			}
			/* Logic:
			 * If the current message is not the first OR
			 * if we're listening to the first new message and there are
			 * also urgent messages, then prompt for navigation to the
			 * previous message
			 */
			if (vms->curmsg || (!in_urgent && vms->urgentmessages > 0) || (ast_test_flag(vmu, VM_MESSAGEWRAP) && vms->lastmsg > 0)) {
				res = ast_play_and_wait(chan, "vm-prev");
			}
			if (!res && !skipadvanced)
				res = ast_play_and_wait(chan, "vm-advopts");
			if (!res)
				res = ast_play_and_wait(chan, "vm-repeat");
			/* Logic:
			 * If we're not listening to the last message OR
			 * we're listening to the last urgent message and there are
			 * also new non-urgent messages, then prompt for navigation
			 * to the next message
			 */
			if (!res && ((vms->curmsg != vms->lastmsg) || (in_urgent && vms->newmessages > 0) ||
				(ast_test_flag(vmu, VM_MESSAGEWRAP) && vms->lastmsg > 0) )) {
				res = ast_play_and_wait(chan, "vm-next");
			}
			if (!res) {
				int curmsg_deleted;
#ifdef IMAP_STORAGE
				ast_mutex_lock(&vms->lock);
#endif
				curmsg_deleted = vms->deleted[vms->curmsg];
#ifdef IMAP_STORAGE
				ast_mutex_unlock(&vms->lock);
#endif
				if (!curmsg_deleted) {
					res = ast_play_and_wait(chan, "vm-delete");
				} else {
					res = ast_play_and_wait(chan, "vm-undelete");
				}
				if (!res) {
					res = ast_play_and_wait(chan, "vm-toforward");
				}
				if (!res) {
					res = ast_play_and_wait(chan, "vm-savemessage");
				}
			}
		}

		if (!res) {
			res = ast_play_and_wait(chan, "vm-helpexit");
		}
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

static int vm_instructions_zh(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms,  int skipadvanced, int in_urgent, int nodelete)
{
	int res = 0;
	/* Play instructions and wait for new command */
	while (!res) {
		if (vms->lastmsg > -1) {
			res = ast_play_and_wait(chan, "vm-listen");
			if (!res)
				res = vm_play_folder_name(chan, vms->vmbox);
			if (!res)
				res = ast_play_and_wait(chan, "press");
			if (!res)
				res = ast_play_and_wait(chan, "digits/1");
		}
		if (!res)
			res = ast_play_and_wait(chan, "vm-opts");
		if (!res) {
			vms->starting = 0;
			return vm_instructions_en(chan, vmu, vms, skipadvanced, in_urgent, nodelete);
		}
	}
	return res;
}

static int vm_instructions(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int skipadvanced, int in_urgent, int nodelete)
{
	if (!strncasecmp(ast_channel_language(chan), "ja", 2)) { /* Japanese syntax */
		return vm_instructions_ja(chan, vmu, vms, skipadvanced, in_urgent, nodelete);
	} else if (vms->starting && !strncasecmp(ast_channel_language(chan), "zh", 2)) { /* CHINESE (Taiwan) syntax */
		return vm_instructions_zh(chan, vmu, vms, skipadvanced, in_urgent, nodelete);
	} else {					/* Default to ENGLISH */
		return vm_instructions_en(chan, vmu, vms, skipadvanced, in_urgent, nodelete);
	}
}

static int vm_newuser_setup(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain)
{
	int cmd = 0;
	int duration = 0;
	int tries = 0;
	char newpassword[80] = "";
	char newpassword2[80] = "";
	char prefile[PATH_MAX] = "";
	unsigned char buf[256];
	int bytes = 0;

	ast_test_suite_event_notify("NEWUSER", "Message: entering new user state");
	if (ast_adsi_available(chan)) {
		bytes += adsi_logo(buf + bytes);
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "New User Setup", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}

	/* If forcename is set, have the user record their name */
	if (ast_test_flag(vmu, VM_FORCENAME)) {
		snprintf(prefile, sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, vms->username);
		if (ast_fileexists(prefile, NULL, NULL) < 1) {
			cmd = play_record_review(chan, "vm-rec-name", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
		}
	}

	/* If forcegreetings is set, have the user record their greetings */
	if (ast_test_flag(vmu, VM_FORCEGREET)) {
		snprintf(prefile, sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, vms->username);
		if (ast_fileexists(prefile, NULL, NULL) < 1) {
			cmd = play_record_review(chan, "vm-rec-unv", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
		}

		snprintf(prefile, sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, vms->username);
		if (ast_fileexists(prefile, NULL, NULL) < 1) {
			cmd = play_record_review(chan, "vm-rec-busy", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
		}
	}

	/*
	 * Change the password last since new users will be able to skip over any steps this one comes before
	 * by hanging up and calling back to voicemail main since the password is used to verify new user status.
	 */
	for (;;) {
		newpassword[1] = '\0';
		newpassword[0] = cmd = ast_play_and_wait(chan, vm_newpassword);
		if (cmd == '#')
			newpassword[0] = '\0';
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		cmd = ast_readstring(chan, newpassword + strlen(newpassword), sizeof(newpassword) - 1, 2000, 10000, "#");
		if (cmd < 0 || cmd == 't' || cmd == '#')
			return cmd;
		cmd = check_password(vmu, newpassword); /* perform password validation */
		if (cmd != 0) {
			ast_log(AST_LOG_NOTICE, "Invalid password for user %s (%s)\n", vms->username, newpassword);
			cmd = ast_play_and_wait(chan, vm_invalid_password);
		} else {
			newpassword2[1] = '\0';
			newpassword2[0] = cmd = ast_play_and_wait(chan, vm_reenterpassword);
			if (cmd == '#')
				newpassword2[0] = '\0';
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
			cmd = ast_readstring(chan, newpassword2 + strlen(newpassword2), sizeof(newpassword2) - 1, 2000, 10000, "#");
			if (cmd < 0 || cmd == 't' || cmd == '#')
				return cmd;
			if (!strcmp(newpassword, newpassword2))
				break;
			ast_log(AST_LOG_NOTICE, "Password mismatch for user %s (%s != %s)\n", vms->username, newpassword, newpassword2);
			cmd = ast_play_and_wait(chan, vm_mismatch);
		}
		if (++tries == 3)
			return -1;
		if (cmd != 0) {
			cmd = ast_play_and_wait(chan, vm_pls_try_again);
		}
	}
	if (pwdchange & PWDCHANGE_INTERNAL)
		vm_change_password(vmu, newpassword);
	if ((pwdchange & PWDCHANGE_EXTERNAL) && !ast_strlen_zero(ext_pass_cmd))
		vm_change_password_shell(vmu, newpassword);

	ast_debug(1, "User %s set password to %s of length %d\n", vms->username, newpassword, (int) strlen(newpassword));
	cmd = ast_play_and_wait(chan, vm_passchanged);

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
	int bytes = 0;

	ast_test_suite_event_notify("VMOPTIONS", "Message: entering mailbox options");
	if (ast_adsi_available(chan)) {
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
		case '1': /* Record your unavailable message */
			snprintf(prefile, sizeof(prefile), "%s%s/%s/unavail", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan, "vm-rec-unv", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			break;
		case '2':  /* Record your busy message */
			snprintf(prefile, sizeof(prefile), "%s%s/%s/busy", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan, "vm-rec-busy", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			break;
		case '3': /* Record greeting */
			snprintf(prefile, sizeof(prefile), "%s%s/%s/greet", VM_SPOOL_DIR, vmu->context, vms->username);
			cmd = play_record_review(chan, "vm-rec-name", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			break;
		case '4':  /* manage the temporary greeting */
			cmd = vm_tempgreeting(chan, vmu, vms, fmtc, record_gain);
			break;
		case '5': /* change password */
			if (vmu->password[0] == '-') {
				cmd = ast_play_and_wait(chan, "vm-no");
				break;
			}
			newpassword[1] = '\0';
			newpassword[0] = cmd = ast_play_and_wait(chan, vm_newpassword);
			if (cmd == '#')
				newpassword[0] = '\0';
			else {
				if (cmd < 0)
					break;
				if ((cmd = ast_readstring(chan, newpassword + strlen(newpassword), sizeof(newpassword) - 1, 2000, 10000, "#")) < 0) {
					break;
				}
			}
			cmd = check_password(vmu, newpassword); /* perform password validation */
			if (cmd != 0) {
				ast_log(AST_LOG_NOTICE, "Invalid password for user %s (%s)\n", vms->username, newpassword);
				cmd = ast_play_and_wait(chan, vm_invalid_password);
				if (!cmd) {
					cmd = ast_play_and_wait(chan, vm_pls_try_again);
				}
				break;
			}
			newpassword2[1] = '\0';
			newpassword2[0] = cmd = ast_play_and_wait(chan, vm_reenterpassword);
			if (cmd == '#')
				newpassword2[0] = '\0';
			else {
				if (cmd < 0)
					break;

				if ((cmd = ast_readstring(chan, newpassword2 + strlen(newpassword2), sizeof(newpassword2) - 1, 2000, 10000, "#")) < 0) {
					break;
				}
			}
			if (strcmp(newpassword, newpassword2)) {
				ast_log(AST_LOG_NOTICE, "Password mismatch for user %s (%s != %s)\n", vms->username, newpassword, newpassword2);
				cmd = ast_play_and_wait(chan, vm_mismatch);
				if (!cmd) {
					cmd = ast_play_and_wait(chan, vm_pls_try_again);
				}
				break;
			}

			if (pwdchange & PWDCHANGE_INTERNAL) {
				vm_change_password(vmu, newpassword);
			}
			if ((pwdchange & PWDCHANGE_EXTERNAL) && !ast_strlen_zero(ext_pass_cmd)) {
				vm_change_password_shell(vmu, newpassword);
			}

			ast_debug(1, "User %s set password to %s of length %d\n",
				vms->username, newpassword, (int) strlen(newpassword));
			cmd = ast_play_and_wait(chan, vm_passchanged);
			break;
		case '*':
			cmd = 't';
			break;
		default:
			cmd = 0;
			snprintf(prefile, sizeof(prefile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, vms->username);
			RETRIEVE(prefile, -1, vmu->mailbox, vmu->context);
			if (ast_fileexists(prefile, NULL, NULL)) {
				cmd = ast_play_and_wait(chan, "vm-tmpexists");
			}
			DISPOSE(prefile, -1);
			if (!cmd) {
				cmd = ast_play_and_wait(chan, "vm-options");
			}
			if (!cmd) {
				cmd = ast_waitfordigit(chan, 6000);
			}
			if (!cmd) {
				retries++;
			}
			if (retries > 3) {
				cmd = 't';
			}
			ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
				isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
		}
	}
	if (cmd == 't')
		cmd = 0;
	return cmd;
}

/*!
 * \brief The handler for 'record a temporary greeting'.
 * \param chan
 * \param vmu
 * \param vms
 * \param fmtc
 * \param record_gain
 *
 * This is option 4 from the mailbox options menu.
 * This function manages the following promptings:
 * 1: play / record / review the temporary greeting. : invokes play_record_review().
 * 2: remove (delete) the temporary greeting.
 * *: return to the main menu.
 *
 * \return zero on success, -1 on error.
 */
static int vm_tempgreeting(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, char *fmtc, signed char record_gain)
{
	int cmd = 0;
	int retries = 0;
	int duration = 0;
	char prefile[PATH_MAX] = "";
	unsigned char buf[256];
	int bytes = 0;

	if (ast_adsi_available(chan)) {
		bytes += adsi_logo(buf + bytes);
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 3, ADSI_JUST_CENT, 0, "Temp Greeting Menu", "");
		bytes += ast_adsi_display(buf + bytes, ADSI_COMM_PAGE, 4, ADSI_JUST_CENT, 0, "Not Done", "");
		bytes += ast_adsi_set_line(buf + bytes, ADSI_COMM_PAGE, 1);
		bytes += ast_adsi_voice_mode(buf + bytes, 0);
		ast_adsi_transmit_message(chan, buf, bytes, ADSI_MSG_DISPLAY);
	}

	ast_test_suite_event_notify("TEMPGREETING", "Message: entering temp greeting options");
	snprintf(prefile, sizeof(prefile), "%s%s/%s/temp", VM_SPOOL_DIR, vmu->context, vms->username);
	while ((cmd >= 0) && (cmd != 't')) {
		if (cmd)
			retries = 0;
		RETRIEVE(prefile, -1, vmu->mailbox, vmu->context);
		if (ast_fileexists(prefile, NULL, NULL) <= 0) {
			cmd = play_record_review(chan, "vm-rec-temp", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
			if (cmd == -1) {
				break;
			}
			cmd = 't';
		} else {
			switch (cmd) {
			case '1':
				cmd = play_record_review(chan, "vm-rec-temp", prefile, maxgreet, fmtc, 0, vmu, &duration, NULL, NULL, record_gain, vms, NULL, NULL, 0);
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
				if (!cmd) {
					cmd = ast_waitfordigit(chan, 6000);
				}
				if (!cmd) {
					retries++;
				}
				if (retries > 3) {
					cmd = 't';
				}
				ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
					isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
			}
		}
		DISPOSE(prefile, -1);
	}
	if (cmd == 't')
		cmd = 0;
	return cmd;
}

/*!
 * \brief Greek syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_gr(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd = 0;

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

/*!
 * \brief Default English syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_en(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd = 0;

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

/*!
 *\brief Italian syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_it(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd;

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

/*!
 * \brief Japanese syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_ja(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd = 0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
		cmd = ast_play_and_wait(chan, vms->fn);
		if (!cmd)
			cmd = ast_play_and_wait(chan, "vm-messages");
		if (!cmd)
			cmd = ast_play_and_wait(chan, "jp-wa");
		if (!cmd)
			cmd = ast_play_and_wait(chan, "jp-arimasen");
	}
	return cmd;
}

/*!
 * \brief Spanish syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_es(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd;

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

/*!
 * \brief Portuguese syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_pt(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd;

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

/*!
 * \brief Chinese (Taiwan)syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_zh(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		cmd = ast_play_and_wait(chan, "vm-you");
		if (!cmd)
			cmd = ast_play_and_wait(chan, "vm-haveno");
		if (!cmd)
			cmd = ast_play_and_wait(chan, "vm-messages");
		if (!cmd) {
			snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
			cmd = ast_play_and_wait(chan, vms->fn);
		}
	}
	return cmd;
}

/*!
 * \brief Vietnamese syntax for 'You have N messages' greeting.
 * \param chan
 * \param vms
 * \param vmu
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages_vi(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	int cmd = 0;

	if (vms->lastmsg > -1) {
		cmd = play_message(chan, vmu, vms);
	} else {
		cmd = ast_play_and_wait(chan, "vm-no");
		if (!cmd) {
			snprintf(vms->fn, sizeof(vms->fn), "vm-%s", vms->curbox);
			cmd = ast_play_and_wait(chan, vms->fn);
		}
	}
	return cmd;
}

/*!
 * \brief Top level method to invoke the language variant vm_browse_messages_XX function.
 * \param chan The channel for the current user. We read the language property from this.
 * \param vms passed into the language-specific vm_browse_messages function.
 * \param vmu passed into the language-specific vm_browse_messages function.
 *
 * The method to be invoked is determined by the value of language code property in the user's channel.
 * The default (when unable to match) is to use english.
 *
 * \return zero on success, -1 on error.
 */
static int vm_browse_messages(struct ast_channel *chan, struct vm_state *vms, struct ast_vm_user *vmu)
{
	if (!strncasecmp(ast_channel_language(chan), "es", 2)) {         /* SPANISH */
		return vm_browse_messages_es(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "gr", 2)) {  /* GREEK */
		return vm_browse_messages_gr(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "he", 2)) {  /* HEBREW */
		return vm_browse_messages_he(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "it", 2)) {  /* ITALIAN */
		return vm_browse_messages_it(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "ja", 2)) {  /* JAPANESE */
		return vm_browse_messages_ja(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "pt", 2)) {  /* PORTUGUESE */
		return vm_browse_messages_pt(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "vi", 2)) {  /* VIETNAMESE */
		return vm_browse_messages_vi(chan, vms, vmu);
	} else if (!strncasecmp(ast_channel_language(chan), "zh", 2)) {  /* CHINESE (Taiwan) */
		return vm_browse_messages_zh(chan, vms, vmu);
	} else {                                             /* Default to English syntax */
		return vm_browse_messages_en(chan, vms, vmu);
	}
}

static int vm_authenticate(struct ast_channel *chan, char *mailbox, int mailbox_size,
			struct ast_vm_user *res_vmu, const char *context, const char *prefix,
			int skipuser, int max_logins, int silent)
{
	int useadsi = 0, valid = 0, logretries = 0;
	char password[AST_MAX_EXTENSION], *passptr = NULL;
	struct ast_vm_user vmus, *vmu = NULL;

	/* If ADSI is supported, setup login screen */
	adsi_begin(chan, &useadsi);
	if (!skipuser && useadsi)
		adsi_login(chan);
	if (!silent && !skipuser && ast_streamfile(chan, vm_login, ast_channel_language(chan))) {
		ast_log(AST_LOG_WARNING, "Couldn't stream login file\n");
		return -1;
	}

	/* Authenticate them and get their mailbox/password */

	while (!valid && (logretries < max_logins)) {
		/* Prompt for, and read in the username */
		if (!skipuser && ast_readstring(chan, mailbox, mailbox_size - 1, 2000, 10000, "#") < 0) {
			ast_log(AST_LOG_WARNING, "Couldn't read username\n");
			return -1;
		}
		if (ast_strlen_zero(mailbox)) {
			if (ast_channel_caller(chan)->id.number.valid && ast_channel_caller(chan)->id.number.str) {
				ast_copy_string(mailbox, ast_channel_caller(chan)->id.number.str, mailbox_size);
			} else {
				ast_verb(3, "Username not entered\n");
				return -1;
			}
		} else if (mailbox[0] == '*') {
			/* user entered '*' */
			ast_verb(4, "Mailbox begins with '*', attempting jump to extension 'a'\n");
			if (ast_exists_extension(chan, ast_channel_context(chan), "a", 1,
				S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
				return -1;
			}
			ast_verb(4, "Jump to extension 'a' failed; setting mailbox to NULL\n");
			mailbox[0] = '\0';
		}

		if (useadsi)
			adsi_password(chan);

		if (!ast_strlen_zero(prefix)) {
			char fullusername[80];

			ast_copy_string(fullusername, prefix, sizeof(fullusername));
			strncat(fullusername, mailbox, sizeof(fullusername) - 1 - strlen(fullusername));
			ast_copy_string(mailbox, fullusername, mailbox_size);
		}

		ast_debug(1, "Before find user for mailbox %s\n", mailbox);
		memset(&vmus, 0, sizeof(vmus));
		vmu = find_user(&vmus, context, mailbox);
		if (vmu && (vmu->password[0] == '\0' || (vmu->password[0] == '-' && vmu->password[1] == '\0'))) {
			/* saved password is blank, so don't bother asking */
			password[0] = '\0';
		} else {
			if (ast_streamfile(chan, vm_password, ast_channel_language(chan))) {
				if (!ast_check_hangup(chan)) {
					ast_log(AST_LOG_WARNING, "Unable to stream password file\n");
				}
				free_user(vmu);
				return -1;
			}
			if (ast_readstring(chan, password, sizeof(password) - 1, 2000, 10000, "#") < 0) {
				ast_log(AST_LOG_NOTICE, "Unable to read password\n");
				free_user(vmu);
				return -1;
			} else if (password[0] == '*') {
				/* user entered '*' */
				ast_verb(4, "Password begins with '*', attempting jump to extension 'a'\n");
				if (ast_exists_extension(chan, ast_channel_context(chan), "a", 1,
					S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
					mailbox[0] = '*';
					free_user(vmu);
					return -1;
				}
				ast_verb(4, "Jump to extension 'a' failed; setting mailbox and user to NULL\n");
				mailbox[0] = '\0';
				/* if the password entered was '*', do not let a user mailbox be created if the extension 'a' is not defined */
				free_user(vmu);
				vmu = NULL;
			}
		}

		if (vmu) {
			passptr = vmu->password;
			if (passptr[0] == '-') passptr++;
		}
		if (vmu && !strcmp(passptr, password))
			valid++;
		else {
			ast_verb(3, "Incorrect password '%s' for user '%s' (context = %s)\n", password, mailbox, context ? context : "default");
			if (!ast_strlen_zero(prefix))
				mailbox[0] = '\0';
		}
		logretries++;
		if (!valid) {
			if (skipuser || logretries >= max_logins) {
				if (ast_streamfile(chan, "vm-incorrect", ast_channel_language(chan))) {
					ast_log(AST_LOG_WARNING, "Unable to stream incorrect message\n");
					free_user(vmu);
					return -1;
				}
				if (ast_waitstream(chan, "")) {	/* Channel is hung up */
					free_user(vmu);
					return -1;
				}
			} else {
				if (useadsi)
					adsi_login(chan);
				if (ast_streamfile(chan, "vm-incorrect-mailbox", ast_channel_language(chan))) {
					ast_log(AST_LOG_WARNING, "Unable to stream incorrect mailbox message\n");
					free_user(vmu);
					return -1;
				}
			}
		}
	}
	if (!valid && (logretries >= max_logins)) {
		ast_stopstream(chan);
		ast_play_and_wait(chan, "vm-goodbye");
		free_user(vmu);
		return -1;
	}
	if (vmu && !skipuser) {
		*res_vmu = *vmu;
	}
	return 0;
}

static int play_message_by_id_helper(struct ast_channel *chan,
	struct ast_vm_user *vmu,
	struct vm_state *vms,
	const char *msg_id)
{
	if (message_range_and_existence_check(vms, &msg_id, 1, &vms->curmsg, vmu)) {
		return -1;
	}
	/* Found the msg, so play it back */

	make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);

#ifdef IMAP_STORAGE
	/*IMAP storage stores any prepended message from a forward
	 * as a separate file from the rest of the message
	 */
	if (!ast_strlen_zero(vms->introfn) && ast_fileexists(vms->introfn, NULL, NULL) > 0) {
		wait_file(chan, vms, vms->introfn);
	}
#endif
	RETRIEVE(vms->curdir,vms->curmsg,vmu->mailbox, vmu->context);

	if ((wait_file(chan, vms, vms->fn)) < 0) {
		ast_log(AST_LOG_WARNING, "Playback of message %s failed\n", vms->fn);
	} else {
#ifdef IMAP_STORAGE
		ast_mutex_lock(&vms->lock);
#endif
		vms->heard[vms->curmsg] = 1;
#ifdef IMAP_STORAGE
		ast_mutex_unlock(&vms->lock);
#endif
	}
	DISPOSE(vms->curdir, vms->curmsg);
	return 0;
}

/*!
 * \brief Finds a message in a specific mailbox by msg_id and plays it to the channel
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
static int play_message_by_id(struct ast_channel *chan, const char *mailbox, const char *context, const char *msg_id)
{
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	int res = 0;
	int open = 0;
	int played = 0;
	int i;

	memset(&vmus, 0, sizeof(vmus));
	memset(&vms, 0, sizeof(vms));

	if (!(vmu = find_user(&vmus, context, mailbox))) {
		goto play_msg_cleanup;
	}

	/* Iterate through every folder, find the msg, and play it */
	for (i = 0; i < ARRAY_LEN(mailbox_folders) && !played; i++) {
		ast_copy_string(vms.username, mailbox, sizeof(vms.username));
		vms.lastmsg = -1;

		/* open the mailbox state */
		if ((res = open_mailbox(&vms, vmu, i)) < 0) {
			ast_log(LOG_WARNING, "Could not open mailbox %s\n", mailbox);
			res = -1;
			goto play_msg_cleanup;
		}
		open = 1;

		/* play msg if it exists in this mailbox */
		if ((vms.lastmsg != -1) && !(play_message_by_id_helper(chan, vmu, &vms, msg_id))) {
			played = 1;
		}

		/* close mailbox */
		if ((res = close_mailbox(&vms, vmu) == ERROR_LOCK_PATH)) {
			res = -1;
			goto play_msg_cleanup;
		}
		open = 0;
	}

play_msg_cleanup:
	if (!played) {
		res = -1;
	}

	if (vmu && open) {
		close_mailbox(&vms, vmu);
	}

#ifdef IMAP_STORAGE
	if (vmu) {
		vmstate_delete(&vms);
	}
#endif

	free_user(vmu);

	return res;
}

static int vm_playmsgexec(struct ast_channel *chan, const char *data)
{
	char *parse;
	char *mailbox = NULL;
	char *context = NULL;
	int res;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(mailbox);
		AST_APP_ARG(msg_id);
	);

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_debug(1, "Before ast_answer\n");
		ast_answer(chan);
	}

	if (ast_strlen_zero(data)) {
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.mailbox) || ast_strlen_zero(args.msg_id)) {
		return -1;
	}

	if ((context = strchr(args.mailbox, '@'))) {
		*context++ = '\0';
	}
	mailbox = args.mailbox;

	res = play_message_by_id(chan, mailbox, context, args.msg_id);
	pbx_builtin_setvar_helper(chan, "VOICEMAIL_PLAYBACKSTATUS", res ? "FAILED" : "SUCCESS");

	return 0;
}

static int show_mailbox_details(struct ast_cli_args *a)
{
#define VMBOX_STRING_HEADER_FORMAT "%-32.32s %-32.32s %-16.16s %-16.16s %-16.16s %-16.16s\n"
#define VMBOX_STRING_DATA_FORMAT   "%-32.32s %-32.32s %-16.16s %-16.16s %-16.16s %-16.16s\n"

	const char *mailbox = a->argv[3];
	const char *context = a->argv[4];
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	memset(&vmus, 0, sizeof(vmus));
	memset(&vms, 0, sizeof(vms));

	if (!(vmu = find_user(&vmus, context, mailbox))) {
		ast_cli(a->fd, "Can't find voicemail user %s@%s\n", mailbox, context);
		return -1;
	}

	ast_cli(a->fd, VMBOX_STRING_HEADER_FORMAT, "Full Name", "Email", "Pager", "Language", "Locale", "Time Zone");
	ast_cli(a->fd, VMBOX_STRING_DATA_FORMAT, vmu->fullname, vmu->email, vmu->pager, vmu->language, vmu->locale, vmu->zonetag);

	return 0;
}

static int show_mailbox_snapshot(struct ast_cli_args *a)
{
#define VM_STRING_HEADER_FORMAT "%-8.8s %-32.32s %-32.32s %-9.9s %-6.6s %-30.30s\n"
	const char *mailbox = a->argv[3];
	const char *context = a->argv[4];
	struct ast_vm_mailbox_snapshot *mailbox_snapshot;
	struct ast_vm_msg_snapshot *msg;
	int i;

	/* Take a snapshot of the mailbox and walk through each folder's contents */
	mailbox_snapshot = ast_vm_mailbox_snapshot_create(mailbox, context, NULL, 0, AST_VM_SNAPSHOT_SORT_BY_ID, 0);
	if (!mailbox_snapshot) {
		ast_cli(a->fd, "Can't create snapshot for voicemail user %s@%s\n", mailbox, context);
		return -1;
	}

	ast_cli(a->fd, VM_STRING_HEADER_FORMAT, "Folder", "Caller ID", "Date", "Duration", "Flag", "ID");

	for (i = 0; i < mailbox_snapshot->folders; i++) {
		AST_LIST_TRAVERSE(&((mailbox_snapshot)->snapshots[i]), msg, msg) {
			ast_cli(a->fd, VM_STRING_HEADER_FORMAT, msg->folder_name, msg->callerid, msg->origdate, msg->duration,
					msg->flag, msg->msg_id);
		}
	}

	ast_cli(a->fd, "%d Message%s Total\n", mailbox_snapshot->total_msg_num, ESS(mailbox_snapshot->total_msg_num));
	/* done, destroy. */
	mailbox_snapshot = ast_vm_mailbox_snapshot_destroy(mailbox_snapshot);

	return 0;
}

static int show_messages_for_mailbox(struct ast_cli_args *a)
{
	if (show_mailbox_details(a)){
		return -1;
	}
	ast_cli(a->fd, "\n");
	return show_mailbox_snapshot(a);
}

static int forward_message_from_mailbox(struct ast_cli_args *a)
{
	const char *from_mailbox = a->argv[2];
	const char *from_context = a->argv[3];
	const char *from_folder = a->argv[4];
	const char *id[] = { a->argv[5] };
	const char *to_mailbox = a->argv[6];
	const char *to_context = a->argv[7];
	const char *to_folder = a->argv[8];
	int ret = vm_msg_forward(from_mailbox, from_context, from_folder, to_mailbox, to_context, to_folder, 1, id, 0);
	if (ret) {
		ast_cli(a->fd, "Error forwarding message %s from mailbox %s@%s %s to mailbox %s@%s %s\n",
					id[0], from_mailbox, from_context, from_folder, to_mailbox, to_context, to_folder);
	} else {
		ast_cli(a->fd, "Forwarded message %s from mailbox %s@%s %s to mailbox %s@%s %s\n",
					id[0], from_mailbox, from_context, from_folder, to_mailbox, to_context, to_folder);
	}
	return ret;
}

static int move_message_from_mailbox(struct ast_cli_args *a)
{
	const char *mailbox = a->argv[2];
	const char *context = a->argv[3];
	const char *from_folder = a->argv[4];
	const char *id[] = { a->argv[5] };
	const char *to_folder = a->argv[6];
	int ret =  vm_msg_move(mailbox, context, 1, from_folder, id, to_folder);
	if (ret) {
		ast_cli(a->fd, "Error moving message %s from mailbox %s@%s %s to %s\n",
					id[0], mailbox, context, from_folder, to_folder);
	} else {
		ast_cli(a->fd, "Moved message %s from mailbox %s@%s %s to %s\n",
					id[0], mailbox, context, from_folder, to_folder);
	}
	return ret;
}

static int remove_message_from_mailbox(struct ast_cli_args *a)
{
	const char *mailbox = a->argv[2];
	const char *context = a->argv[3];
	const char *folder = a->argv[4];
	const char *id[] = { a->argv[5] };
	int ret = vm_msg_remove(mailbox, context, 1, folder, id);
	if (ret) {
		ast_cli(a->fd, "Error removing message %s from mailbox %s@%s %s\n",
					id[0], mailbox, context, folder);
	} else {
		ast_cli(a->fd, "Removed message %s from mailbox %s@%s %s\n",
					id[0], mailbox, context, folder);
	}
	return ret;
}

static char *complete_voicemail_show_mailbox(struct ast_cli_args *a)
{
	const char *word = a->word;
	int pos = a->pos;
	int state = a->n;
	int which = 0;
	int wordlen;
	struct ast_vm_user *vmu;
	const char *context = "", *mailbox = "";
	char *ret = NULL;

	/* 0 - voicemail; 1 - show; 2 - mailbox; 3 - <mailbox>; 4 - <context> */
	if (pos == 3) {
		wordlen = strlen(word);
		AST_LIST_LOCK(&users);
		AST_LIST_TRAVERSE(&users, vmu, list) {
			if (!strncasecmp(word, vmu->mailbox , wordlen)) {
				if (mailbox && strcmp(mailbox, vmu->mailbox) && ++which > state) {
					ret = ast_strdup(vmu->mailbox);
					AST_LIST_UNLOCK(&users);
					return ret;
				}
				mailbox = vmu->mailbox;
			}
		}
		AST_LIST_UNLOCK(&users);
	} else if (pos == 4) {
		/* Only display contexts that match the user in pos 3 */
		const char *box = a->argv[3];
		wordlen = strlen(word);
		AST_LIST_LOCK(&users);
		AST_LIST_TRAVERSE(&users, vmu, list) {
			if (!strncasecmp(word, vmu->context, wordlen) && !strcasecmp(box, vmu->mailbox)) {
				if (context && strcmp(context, vmu->context) && ++which > state) {
					ret = ast_strdup(vmu->context);
					AST_LIST_UNLOCK(&users);
					return ret;
				}
				context = vmu->context;
			}
		}
		AST_LIST_UNLOCK(&users);
	}

	return ret;
}

static char *handle_voicemail_show_mailbox(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail show mailbox";
		e->usage =
		"Usage: voicemail show mailbox <mailbox> <context>\n"
		"       Show contents of mailbox <mailbox>@<context>\n";
		return NULL;
	case CLI_GENERATE:
		return complete_voicemail_show_mailbox(a);
	case CLI_HANDLER:
		break;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (show_messages_for_mailbox(a)) {
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

/* Handles filling in data for one of the following three formats (based on maxpos = 5|6|8):

	maxpos = 5
	0 - voicemail; 1 - forward; 2 - <from_mailbox>; 3 - <from_context>; 4 - <from_folder>; 5 - <messageid>;
	maxpos = 6
	0 - voicemail; 1 - forward; 2 - <from_mailbox>; 3 - <from_context>; 4 - <from_folder>; 5 - <messageid>;
                                                                        6 - <to_folder>;
	maxpos = 8
	0 - voicemail; 1 - forward; 2 - <from_mailbox>; 3 - <from_context>; 4 - <from_folder>; 5 - <messageid>;
                                6 - <to_mailbox>;   7 - <to_context>;   8 - <to_folder>;

	Passing in the maximum expected position 'maxpos' helps us fill in the missing entries in one function
	instead of three by taking advantage of the overlap in the command sequence between forward, move and
	remove as each of these use nearly the same syntax up until their maximum number of arguments.
	The value of pos = 6 changes to be either <messageid> or <folder> based on maxpos being 6 or 8.
*/

static char *complete_voicemail_move_message(struct ast_cli_args *a, int maxpos)
{
	const char *word = a->word;
	int pos = a->pos;
	int state = a->n;
	int which = 0;
	int wordlen;
	struct ast_vm_user *vmu;
	const char *context = "", *mailbox = "", *folder = "", *id = "";
	char *ret = NULL;

	if (pos > maxpos) {
		/* If the passed in pos is above the max, return NULL to avoid 'over-filling' the cli */
		return NULL;
	}

	/* if we are in pos 2 or pos 6 in 'forward' mode */
	if (pos == 2 || (pos == 6 && maxpos == 8)) {
		/* find users */
		wordlen = strlen(word);
		AST_LIST_LOCK(&users);
		AST_LIST_TRAVERSE(&users, vmu, list) {
			if (!strncasecmp(word, vmu->mailbox , wordlen)) {
				if (mailbox && strcmp(mailbox, vmu->mailbox) && ++which > state) {
					ret = ast_strdup(vmu->mailbox);
					AST_LIST_UNLOCK(&users);
					return ret;
				}
				mailbox = vmu->mailbox;
			}
		}
		AST_LIST_UNLOCK(&users);
	} else if (pos == 3 || pos == 7) {
		/* find contexts that match the user */
		mailbox = (pos == 3) ? a->argv[2] : a->argv[6];
		wordlen = strlen(word);
		AST_LIST_LOCK(&users);
		AST_LIST_TRAVERSE(&users, vmu, list) {
			if (!strncasecmp(word, vmu->context, wordlen) && !strcasecmp(mailbox, vmu->mailbox)) {
				if (context && strcmp(context, vmu->context) && ++which > state) {
					ret = ast_strdup(vmu->context);
					AST_LIST_UNLOCK(&users);
					return ret;
				}
				context = vmu->context;
			}
		}
		AST_LIST_UNLOCK(&users);
	} else if (pos == 4 || pos == 8 || (pos == 6 && maxpos == 6) ) {
		int i;
		/* Walk through the standard folders */
		wordlen = strlen(word);
		for (i = 0; i < ARRAY_LEN(mailbox_folders); i++) {
			if (folder && !strncasecmp(word, mailbox_folders[i], wordlen) && ++which > state) {
				return ast_strdup(mailbox_folders[i]);
			}
			folder = mailbox_folders[i];
		}
	} else if (pos == 5) {
		/* find messages in the folder */
		struct ast_vm_mailbox_snapshot *mailbox_snapshot;
		struct ast_vm_msg_snapshot *msg;
		mailbox = a->argv[2];
		context = a->argv[3];
		folder = a->argv[4];
		wordlen = strlen(word);

		/* Take a snapshot of the mailbox and snag the individual info */
		if ((mailbox_snapshot = ast_vm_mailbox_snapshot_create(mailbox, context, folder, 0, AST_VM_SNAPSHOT_SORT_BY_ID, 0))) {
			int i;
			/* we are only requesting the one folder, but we still need to know it's index */
			for (i = 0; i < ARRAY_LEN(mailbox_folders); i++) {
				if (!strcasecmp(mailbox_folders[i], folder)) {
					break;
				}
			}
			AST_LIST_TRAVERSE(&((mailbox_snapshot)->snapshots[i]), msg, msg) {
				if (id && !strncasecmp(word, msg->msg_id, wordlen) && ++which > state) {
					ret = ast_strdup(msg->msg_id);
					break;
				}
				id = msg->msg_id;
			}
			/* done, destroy. */
			mailbox_snapshot = ast_vm_mailbox_snapshot_destroy(mailbox_snapshot);
		}
	}

	return ret;
}

static char *handle_voicemail_forward_message(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail forward";
		e->usage =
		"Usage: voicemail forward <from_mailbox> <from_context> <from_folder> <messageid> <to_mailbox> <to_context> <to_folder>\n"
		"       Forward message <messageid> in mailbox <mailbox>@<context> <from_folder>\n"
		"       to mailbox <mailbox>@<context> <to_folder>\n";
		return NULL;
	case CLI_GENERATE:
		return complete_voicemail_move_message(a, 8);
	case CLI_HANDLER:
		break;
	}

	if (a->argc != 9) {
		return CLI_SHOWUSAGE;
	}

	if (forward_message_from_mailbox(a)) {
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

static char *handle_voicemail_move_message(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail move";
		e->usage =
		"Usage: voicemail move <mailbox> <context> <from_folder> <messageid> <to_folder>\n"
		"       Move message <messageid> in mailbox <mailbox>&<context> from <from_folder> to <to_folder>\n";
		return NULL;
	case CLI_GENERATE:
		return complete_voicemail_move_message(a, 6);
	case CLI_HANDLER:
		break;
	}

	if (a->argc != 7) {
		return CLI_SHOWUSAGE;
	}

	if (move_message_from_mailbox(a)) {
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

static char *handle_voicemail_remove_message(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail remove";
		e->usage =
		"Usage: voicemail remove <mailbox> <context> <from_folder> <messageid>\n"
		"       Remove message <messageid> from <from_folder> in mailbox <mailbox>@<context>\n";
		return NULL;
	case CLI_GENERATE:
		return complete_voicemail_move_message(a, 5);
	case CLI_HANDLER:
		break;
	}

	if (a->argc != 6) {
		return CLI_SHOWUSAGE;
	}

	if (remove_message_from_mailbox(a)) {
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

static int vm_execmain(struct ast_channel *chan, const char *data)
{
	/* XXX This is, admittedly, some pretty horrendous code.  For some
	   reason it just seemed a lot easier to do with GOTO's.  I feel
	   like I'm back in my GWBASIC days. XXX */
	int res = -1;
	int cmd = 0;
	int valid = 0;
	char prefixstr[80] ="";
	char ext_context[256]="";
	int box;
	int useadsi = 0;
	int skipuser = 0;
	struct vm_state vms = {{0}};
	struct ast_vm_user *vmu = NULL, vmus = {{0}};
	char *context = NULL;
	int silentexit = 0;
	struct ast_flags flags = { 0 };
	signed char record_gain = 0;
	int play_auto = 0;
	int play_folder = 0;
	int in_urgent = 0;
	int nodelete = 0;
#ifdef IMAP_STORAGE
	int deleted = 0;
#endif

	/* Add the vm_state to the active list and keep it active */
	vms.lastmsg = -1;

	ast_test_suite_event_notify("START", "Message: vm_execmain started");
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_debug(1, "Before ast_answer\n");
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
			if (ast_app_parse_options(vm_app_options, &flags, opts, args.argv1))
				return -1;
			if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
				int gain;
				if (!ast_strlen_zero(opts[OPT_ARG_RECORDGAIN])) {
					if (sscanf(opts[OPT_ARG_RECORDGAIN], "%30d", &gain) != 1) {
						ast_log(AST_LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
						return -1;
					} else {
						record_gain = (signed char) gain;
					}
				} else {
					ast_log(AST_LOG_WARNING, "Invalid Gain level set with option g\n");
				}
			}
			if (ast_test_flag(&flags, OPT_AUTOPLAY) ) {
				play_auto = 1;
				if (!ast_strlen_zero(opts[OPT_ARG_PLAYFOLDER])) {
					/* See if it is a folder name first */
					if (isdigit(opts[OPT_ARG_PLAYFOLDER][0])) {
						if (sscanf(opts[OPT_ARG_PLAYFOLDER], "%30d", &play_folder) != 1) {
							play_folder = -1;
						}
					} else {
						play_folder = get_folder_by_name(opts[OPT_ARG_PLAYFOLDER]);
					}
				} else {
					ast_log(AST_LOG_WARNING, "Invalid folder set with option a\n");
				}
				if (play_folder > 9 || play_folder < 0) {
					ast_log(AST_LOG_WARNING,
						"Invalid value '%s' provided for folder autoplay option. Defaulting to 'INBOX'\n",
						opts[OPT_ARG_PLAYFOLDER]);
					play_folder = 0;
				}
			}
			if (ast_test_flag(&flags, OPT_READONLY)) {
				nodelete = 1;
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

		if (!ast_strlen_zero(vms.username)) {
			if ((vmu = find_user(&vmus, context ,vms.username))) {
				skipuser++;
			} else {
				ast_log(LOG_WARNING, "Mailbox '%s%s%s' doesn't exist\n", vms.username, context ? "@": "", context ? context : "");
				valid = 0;
			}
		} else {
			valid = 0;
		}
	}

	if (!valid)
		res = vm_authenticate(chan, vms.username, sizeof(vms.username), &vmus, context, prefixstr, skipuser, maxlogins, 0);

	ast_debug(1, "After vm_authenticate\n");

	if (vms.username[0] == '*') {
		ast_debug(1, "user pressed * in context '%s'\n", ast_channel_context(chan));

		/* user entered '*' */
		if (!ast_goto_if_exists(chan, ast_channel_context(chan), "a", 1)) {
			ast_test_suite_event_notify("REDIRECT", "Message: redirecting user to 'a' extension");
			res = 0;	/* prevent hangup */
			goto out;
		}
	}

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
	ast_test_suite_event_notify("AUTHENTICATED", "Message: vm_user authenticated");

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
	if (!ast_strlen_zero(vmu->language)) {
		ast_channel_lock(chan);
		ast_channel_language_set(chan, vmu->language);
		ast_channel_unlock(chan);
	}

	/* Retrieve urgent, old and new message counts */
	ast_debug(1, "Before open_mailbox\n");
	res = open_mailbox(&vms, vmu, OLD_FOLDER); /* Count all messages, even Urgent */
	if (res < 0)
		goto out;
	vms.oldmessages = vms.lastmsg + 1;
	ast_debug(1, "Number of old messages: %d\n", vms.oldmessages);
	/* check INBOX */
	res = open_mailbox(&vms, vmu, NEW_FOLDER);
	if (res < 0)
		goto out;
	vms.newmessages = vms.lastmsg + 1;
	ast_debug(1, "Number of new messages: %d\n", vms.newmessages);
	/* Start in Urgent */
	in_urgent = 1;
	res = open_mailbox(&vms, vmu, 11); /*11 is the Urgent folder */
	if (res < 0)
		goto out;
	vms.urgentmessages = vms.lastmsg + 1;
	ast_debug(1, "Number of urgent messages: %d\n", vms.urgentmessages);

	/* Select proper mailbox FIRST!! */
	if (play_auto) {
		ast_test_suite_event_notify("AUTOPLAY", "Message: auto-playing messages");
		if (vms.urgentmessages) {
			in_urgent = 1;
			res = open_mailbox(&vms, vmu, 11);
		} else {
			in_urgent = 0;
			res = open_mailbox(&vms, vmu, play_folder);
		}
		if (res < 0)
			goto out;

		/* If there are no new messages, inform the user and hangup */
		if (vms.lastmsg == -1) {
			in_urgent = 0;
			cmd = vm_browse_messages(chan, &vms, vmu);
			res = 0;
			goto out;
		}
	} else {
		if (!vms.newmessages && !vms.urgentmessages && vms.oldmessages) {
			/* If we only have old messages start here */
			res = open_mailbox(&vms, vmu, OLD_FOLDER); /* Count all messages, even Urgent */
			in_urgent = 0;
			play_folder = 1;
			if (res < 0)
				goto out;
		} else if (!vms.urgentmessages && vms.newmessages) {
			/* If we have new messages but none are urgent */
			in_urgent = 0;
			res = open_mailbox(&vms, vmu, NEW_FOLDER);
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
		if (ast_play_and_wait(chan, vm_newuser) == -1)
			ast_log(AST_LOG_WARNING, "Couldn't stream new user file\n");
		cmd = vm_newuser_setup(chan, vmu, &vms, vmfmts, record_gain);
		if ((cmd == 't') || (cmd == '#')) {
			/* Timeout */
			ast_test_suite_event_notify("TIMEOUT", "Message: response from user timed out");
			res = 0;
			goto out;
		} else if (cmd < 0) {
			/* Hangup */
			ast_test_suite_event_notify("HANGUP", "Message: hangup detected");
			res = -1;
			goto out;
		}
	}
#ifdef IMAP_STORAGE
		ast_debug(3, "Checking quotas: comparing %u to %u\n", vms.quota_usage, vms.quota_limit);
		if (vms.quota_limit && vms.quota_usage >= vms.quota_limit) {
			ast_debug(1, "*** QUOTA EXCEEDED!!\n");
			cmd = ast_play_and_wait(chan, "vm-mailboxfull");
		}
		ast_debug(3, "Checking quotas: User has %d messages and limit is %d.\n", (vms.newmessages + vms.oldmessages), vmu->maxmsg);
		if ((vms.newmessages + vms.oldmessages) >= vmu->maxmsg) {
			ast_log(AST_LOG_WARNING, "No more messages possible.  User has %d messages and limit is %d.\n", (vms.newmessages + vms.oldmessages), vmu->maxmsg);
			cmd = ast_play_and_wait(chan, "vm-mailboxfull");
		}
#endif

	ast_test_suite_event_notify("INTRO", "Message: playing intro menu");
	if (play_auto) {
		cmd = '1';
	} else {
		cmd = vm_intro(chan, vmu, &vms);
	}
	ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
		isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');

	vms.repeats = 0;
	vms.starting = 1;
	while ((cmd > -1) && (cmd != 't') && (cmd != '#')) {
		/* Run main menu */
		switch (cmd) {
		case '1': /* First message */
			vms.curmsg = 0;
			/* Fall through */
		case '5': /* Play current message */
			ast_test_suite_event_notify("BROWSE", "Message: browsing message %d\r\nVoicemail: %d", vms.curmsg, vms.curmsg);
			cmd = vm_browse_messages(chan, &vms, vmu);
			break;
		case '2': /* Change folders */
			ast_test_suite_event_notify("CHANGEFOLDER", "Message: browsing to a different folder");
			if (useadsi)
				adsi_folders(chan, 0, "Change to folder...");

			cmd = get_folder2(chan, "vm-changeto", 0);
			ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
				isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
			if (cmd == '#') {
				cmd = 0;
			} else if (cmd > 0) {
				cmd = cmd - '0';
				res = close_mailbox(&vms, vmu);
				if (res == ERROR_LOCK_PATH)
					goto out;
				/* If folder is not urgent, set in_urgent to zero! */
				if (cmd != 11) in_urgent = 0;
				res = open_mailbox(&vms, vmu, cmd);
				if (res < 0)
					goto out;
				play_folder = cmd;
				cmd = 0;
			}
			if (useadsi)
				adsi_status2(chan, &vms);

			if (!cmd) {
				cmd = vm_play_folder_name(chan, vms.vmbox);
			}

			vms.starting = 1;
			vms.curmsg = 0;
			break;
		case '3': /* Advanced options */
			ast_test_suite_event_notify("ADVOPTIONS", "Message: entering advanced options menu");
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
					} else {
						cmd = ast_play_and_wait(chan, "vm-sorry");
					}
					cmd = 't';
					break;
				case '2': /* Callback */
					if (!vms.starting)
						ast_verb(3, "Callback Requested\n");
					if (!ast_strlen_zero(vmu->callback) && vms.lastmsg > -1 && !vms.starting) {
						cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 2, record_gain);
						if (cmd == 9) {
							silentexit = 1;
							goto out;
						} else if (cmd == ERROR_LOCK_PATH) {
							res = cmd;
							goto out;
						}
					} else {
						cmd = ast_play_and_wait(chan, "vm-sorry");
					}
					cmd = 't';
					break;
				case '3': /* Envelope */
					if (vms.lastmsg > -1 && !vms.starting) {
						cmd = advanced_options(chan, vmu, &vms, vms.curmsg, 3, record_gain);
						if (cmd == ERROR_LOCK_PATH) {
							res = cmd;
							goto out;
						}
					} else {
						cmd = ast_play_and_wait(chan, "vm-sorry");
					}
					cmd = 't';
					break;
				case '4': /* Dialout */
					if (!ast_strlen_zero(vmu->dialout)) {
						cmd = dialout(chan, vmu, NULL, vmu->dialout);
						if (cmd == 9) {
							silentexit = 1;
							goto out;
						}
					} else {
						cmd = ast_play_and_wait(chan, "vm-sorry");
					}
					cmd = 't';
					break;

				case '5': /* Leave VoiceMail */
					if (ast_test_flag(vmu, VM_SVMAIL)) {
						cmd = forward_message(chan, context, &vms, vmu, vmfmts, 1, record_gain, 0);
						if (cmd == ERROR_LOCK_PATH || cmd == OPERATOR_EXIT) {
							res = cmd;
							goto out;
						}
					} else {
						cmd = ast_play_and_wait(chan, "vm-sorry");
					}
					cmd = 't';
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
					if (ast_test_flag(vmu, VM_SVMAIL) && !cmd) {
						cmd = ast_play_and_wait(chan, "vm-leavemsg");
					}
					if (!cmd) {
						cmd = ast_play_and_wait(chan, "vm-starmain");
					}
					if (!cmd) {
						cmd = ast_waitfordigit(chan, 6000);
					}
					if (!cmd) {
						vms.repeats++;
					}
					if (vms.repeats > 3) {
						cmd = 't';
					}
					ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
						isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
				}
			}
			if (cmd == 't') {
				cmd = 0;
				vms.repeats = 0;
			}
			break;
		case '4': /* Go to the previous message */
			ast_test_suite_event_notify("PREVMSG", "Message: browsing message %d\r\nVoicemail: %d", vms.curmsg - 1, vms.curmsg - 1);
			if (vms.curmsg > 0) {
				vms.curmsg--;
				cmd = play_message(chan, vmu, &vms);
			} else {
				/* Check if we were listening to new
				   messages.  If so, go to Urgent messages
				   instead of saying "no more messages"
				*/
				if (in_urgent == 0 && vms.urgentmessages > 0) {
					/* Check for Urgent messages */
					in_urgent = 1;
					res = close_mailbox(&vms, vmu);
					if (res == ERROR_LOCK_PATH)
						goto out;
					res = open_mailbox(&vms, vmu, 11);  /* Open Urgent folder */
					if (res < 0)
						goto out;
					ast_debug(1, "No more new messages, opened INBOX and got %d Urgent messages\n", vms.lastmsg + 1);
					vms.curmsg = vms.lastmsg;
					if (vms.lastmsg < 0) {
						cmd = ast_play_and_wait(chan, "vm-nomore");
					}
				} else if (ast_test_flag(vmu, VM_MESSAGEWRAP) && vms.lastmsg > 0) {
					vms.curmsg = vms.lastmsg;
					cmd = play_message(chan, vmu, &vms);
				} else {
					cmd = ast_play_and_wait(chan, "vm-nomore");
				}
			}
			break;
		case '6': /* Go to the next message */
			ast_test_suite_event_notify("PREVMSG", "Message: browsing message %d\r\nVoicemail: %d", vms.curmsg + 1, vms.curmsg + 1);
			if (vms.curmsg < vms.lastmsg) {
				vms.curmsg++;
				cmd = play_message(chan, vmu, &vms);
			} else {
				if (in_urgent && vms.newmessages > 0) {
					/* Check if we were listening to urgent
					 * messages.  If so, go to regular new messages
				 	 * instead of saying "no more messages"
					 */
					in_urgent = 0;
					res = close_mailbox(&vms, vmu);
					if (res == ERROR_LOCK_PATH)
						goto out;
					res = open_mailbox(&vms, vmu, NEW_FOLDER);
					if (res < 0)
						goto out;
					ast_debug(1, "No more urgent messages, opened INBOX and got %d new messages\n", vms.lastmsg + 1);
					vms.curmsg = -1;
					if (vms.lastmsg < 0) {
						cmd = ast_play_and_wait(chan, "vm-nomore");
					}
				} else if (ast_test_flag(vmu, VM_MESSAGEWRAP) && vms.lastmsg > 0) {
					vms.curmsg = 0;
					cmd = play_message(chan, vmu, &vms);
				} else {
					cmd = ast_play_and_wait(chan, "vm-nomore");
				}
			}
			break;
		case '7': /* Delete the current message */
			if (!nodelete && vms.curmsg >= 0 && vms.curmsg <= vms.lastmsg) {
				vms.deleted[vms.curmsg] = !vms.deleted[vms.curmsg];
				if (useadsi)
					adsi_delete(chan, &vms);
				if (vms.deleted[vms.curmsg]) {
					if (play_folder == 0) {
						if (in_urgent) {
							vms.urgentmessages--;
						} else {
							vms.newmessages--;
						}
					}
					else if (play_folder == 1)
						vms.oldmessages--;
					cmd = ast_play_and_wait(chan, "vm-deleted");
				} else {
					if (play_folder == 0) {
						if (in_urgent) {
							vms.urgentmessages++;
						} else {
							vms.newmessages++;
						}
					}
					else if (play_folder == 1)
						vms.oldmessages++;
					cmd = ast_play_and_wait(chan, "vm-undeleted");
				}
				if (ast_test_flag(vmu, VM_SKIPAFTERCMD)) {
					if (vms.curmsg < vms.lastmsg) {
						vms.curmsg++;
						cmd = play_message(chan, vmu, &vms);
					} else if (ast_test_flag(vmu, VM_MESSAGEWRAP) && vms.lastmsg > 0) {
						vms.curmsg = 0;
						cmd = play_message(chan, vmu, &vms);
					} else {
						/* Check if we were listening to urgent
						   messages.  If so, go to regular new messages
						   instead of saying "no more messages"
						*/
						if (in_urgent == 1) {
							/* Check for new messages */
							in_urgent = 0;
							res = close_mailbox(&vms, vmu);
							if (res == ERROR_LOCK_PATH)
								goto out;
							res = open_mailbox(&vms, vmu, NEW_FOLDER);
							if (res < 0)
								goto out;
							ast_debug(1, "No more urgent messages, opened INBOX and got %d new messages\n", vms.lastmsg + 1);
							vms.curmsg = -1;
							if (vms.lastmsg < 0) {
								cmd = ast_play_and_wait(chan, "vm-nomore");
							}
						} else {
							cmd = ast_play_and_wait(chan, "vm-nomore");
						}
					}
				}
			} else /* Delete not valid if we haven't selected a message */
				cmd = 0;
#ifdef IMAP_STORAGE
			deleted = 1;
#endif
			break;

		case '8': /* Forward the current message */
			if (vms.lastmsg > -1) {
				cmd = forward_message(chan, context, &vms, vmu, vmfmts, 0, record_gain, in_urgent);
				if (cmd == ERROR_LOCK_PATH) {
					res = cmd;
					goto out;
				}
			} else {
				/* Check if we were listening to urgent
				   messages.  If so, go to regular new messages
				   instead of saying "no more messages"
				*/
				if (in_urgent == 1 && vms.newmessages > 0) {
					/* Check for new messages */
					in_urgent = 0;
					res = close_mailbox(&vms, vmu);
					if (res == ERROR_LOCK_PATH)
						goto out;
					res = open_mailbox(&vms, vmu, NEW_FOLDER);
					if (res < 0)
						goto out;
					ast_debug(1, "No more urgent messages, opened INBOX and got %d new messages\n", vms.lastmsg + 1);
					vms.curmsg = -1;
					if (vms.lastmsg < 0) {
						cmd = ast_play_and_wait(chan, "vm-nomore");
					}
				} else {
					cmd = ast_play_and_wait(chan, "vm-nomore");
				}
			}
			break;
		case '9': /* Save message to folder */
			ast_test_suite_event_notify("SAVEMSG", "Message: saving message %d\r\nVoicemail: %d", vms.curmsg, vms.curmsg);
			if (vms.curmsg < 0 || vms.curmsg > vms.lastmsg) {
				/* No message selected */
				cmd = 0;
				break;
			}
			if (useadsi)
				adsi_folders(chan, 1, "Save to folder...");
			cmd = get_folder2(chan, "vm-savefolder", 1);
			ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
				isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
			box = 0;	/* Shut up compiler */
			if (cmd == '#') {
				cmd = 0;
				break;
			} else if (cmd > 0) {
				box = cmd = cmd - '0';
				cmd = save_to_folder(vmu, &vms, vms.curmsg, cmd, NULL, 0);
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
			snprintf(vms.fn, sizeof(vms.fn), "vm-%s", mbox(vmu, box));
			if (!cmd) {
				cmd = ast_play_and_wait(chan, "vm-message");
				if (!cmd)
					cmd = say_and_wait(chan, vms.curmsg + 1, ast_channel_language(chan));
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
				} else if (ast_test_flag(vmu, VM_MESSAGEWRAP) && vms.lastmsg > 0) {
					vms.curmsg = 0;
					cmd = play_message(chan, vmu, &vms);
				} else {
					/* Check if we were listening to urgent
					   messages.  If so, go to regular new messages
					   instead of saying "no more messages"
					*/
					if (in_urgent == 1 && vms.newmessages > 0) {
						/* Check for new messages */
						in_urgent = 0;
						res = close_mailbox(&vms, vmu);
						if (res == ERROR_LOCK_PATH)
							goto out;
						res = open_mailbox(&vms, vmu, NEW_FOLDER);
						if (res < 0)
							goto out;
						ast_debug(1, "No more urgent messages, opened INBOX and got %d new messages\n", vms.lastmsg + 1);
						vms.curmsg = -1;
						if (vms.lastmsg < 0) {
							cmd = ast_play_and_wait(chan, "vm-nomore");
						}
					} else {
						cmd = ast_play_and_wait(chan, "vm-nomore");
					}
				}
			}
			break;
		case '*': /* Help */
			if (!vms.starting) {
				if (!strncasecmp(ast_channel_language(chan), "ja", 2)) {
					cmd = vm_play_folder_name(chan, vms.vmbox);
					if (!cmd)
						cmd = ast_play_and_wait(chan, "jp-wa");
					if (!cmd)
						cmd = ast_play_and_wait(chan, "digits/1");
					if (!cmd)
						cmd = ast_play_and_wait(chan, "jp-wo");
					if (!cmd)
						cmd = ast_play_and_wait(chan, "silence/1");
					if (!cmd)
						cmd = ast_play_and_wait(chan, "vm-opts");
					if (!cmd)
						cmd = vm_instructions(chan, vmu, &vms, 1, in_urgent, nodelete);
					break;
				}
				cmd = ast_play_and_wait(chan, "vm-onefor");
				if (!strncasecmp(ast_channel_language(chan), "he", 2)) {
					cmd = ast_play_and_wait(chan, "vm-for");
				}
				if (!cmd)
					cmd = vm_play_folder_name(chan, vms.vmbox);
				if (!cmd)
					cmd = ast_play_and_wait(chan, "vm-opts");
				if (!cmd)
					cmd = vm_instructions(chan, vmu, &vms, 1, in_urgent, nodelete);
			} else
				cmd = 0;
			break;
		case '0': /* Mailbox options */
			cmd = vm_options(chan, vmu, &vms, vmfmts, record_gain);
			if (useadsi)
				adsi_status(chan, &vms);
			/* Reopen play_folder */
			res = open_mailbox(&vms, vmu, play_folder);
			if (res < 0) {
				goto out;
			}
			vms.starting = 1;
 			break;
		default:	/* Nothing */
			ast_test_suite_event_notify("PLAYBACK", "Message: instructions");
			cmd = vm_instructions(chan, vmu, &vms, 0, in_urgent, nodelete);
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
		int new = 0, old = 0, urgent = 0;
		snprintf(ext_context, sizeof(ext_context), "%s@%s", vms.username, vmu->context);
		/* Urgent flag not passwd to externnotify here */
		run_externnotify(vmu->context, vmu->mailbox, NULL);
		ast_app_inboxcount2(ext_context, &urgent, &new, &old);
		queue_mwi_event(ast_channel_uniqueid(chan), ext_context, urgent, new, old);
	}
#ifdef IMAP_STORAGE
	/* expunge message - use UID Expunge if supported on IMAP server*/
	ast_debug(3, "*** Checking if we can expunge, deleted set to %d, expungeonhangup set to %d\n", deleted, expungeonhangup);
	if (vmu && deleted == 1 && expungeonhangup == 1 && vms.mailstream != NULL) {
		ast_mutex_lock(&vms.lock);
#ifdef HAVE_IMAP_TK2006
		if (LEVELUIDPLUS (vms.mailstream)) {
			mail_expunge_full(vms.mailstream, NIL, EX_UID);
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

#ifdef IMAP_STORAGE
	pthread_setspecific(ts_vmstate.key, NULL);
#endif
	return res;
}

static int vm_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *tmp;
	struct leave_vm_options leave_options;
	struct ast_flags flags = { 0 };
	char *opts[OPT_ARG_ARRAY_SIZE];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(argv0);
		AST_APP_ARG(argv1);
	);

	memset(&leave_options, 0, sizeof(leave_options));

	if (!ast_strlen_zero(data)) {
		tmp = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, tmp);
		if (args.argc == 2) {
			if (ast_app_parse_options(vm_app_options, &flags, opts, args.argv1))
				return -1;
			ast_copy_flags(&leave_options, &flags, OPT_SILENT | OPT_SILENT_IF_GREET | OPT_BUSY_GREETING | OPT_UNAVAIL_GREETING | OPT_MESSAGE_Urgent | OPT_MESSAGE_PRIORITY | OPT_DTMFEXIT);
			if (ast_test_flag(&flags, OPT_RECORDGAIN)) {
				int gain;

				if (sscanf(opts[OPT_ARG_RECORDGAIN], "%30d", &gain) != 1) {
					ast_log(AST_LOG_WARNING, "Invalid value '%s' provided for record gain option\n", opts[OPT_ARG_RECORDGAIN]);
					return -1;
				} else {
					leave_options.record_gain = (signed char) gain;
				}
			}
			if (ast_test_flag(&flags, OPT_DTMFEXIT)) {
				if (!ast_strlen_zero(opts[OPT_ARG_DTMFEXIT]))
					leave_options.exitcontext = opts[OPT_ARG_DTMFEXIT];
			}
		}
		if (ast_test_flag(&flags, OPT_BEEP)) { /* Use custom beep (or none at all) */
			leave_options.beeptone = opts[OPT_ARG_BEEP_TONE];
		} else { /* Use default beep */
			leave_options.beeptone = "beep";
		}
	} else {
		char temp[256];
		res = ast_app_getdata(chan, "vm-whichbox", temp, sizeof(temp) - 1, 0);
		if (res < 0)
			return res;
		if (ast_strlen_zero(temp))
			return 0;
		args.argv0 = ast_strdupa(temp);
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_test_flag(&flags, OPT_EARLYM_GREETING)) {
			ast_indicate(chan, AST_CONTROL_PROGRESS);
		} else {
			ast_answer(chan);
		}
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
		ast_log(AST_LOG_ERROR, "Could not leave voicemail. The path is already locked.\n");
		pbx_builtin_setvar_helper(chan, "VMSTATUS", "FAILED");
		res = 0;
	}

	return res;
}

static int add_message_id(struct ast_config *msg_cfg, char *dir, int msg, char *filename, char *id, size_t id_size, struct ast_vm_user *vmu, int folder)
{
	struct ast_variable *var;
	struct ast_category *cat;
	generate_msg_id(id);

	var = ast_variable_new("msg_id", id, "");
	if (!var) {
		return -1;
	}

	cat = ast_category_get(msg_cfg, "message", NULL);
	if (!cat) {
		ast_log(LOG_ERROR, "Voicemail data file %s/%d.txt has no [message] category?\n", dir, msg);
		ast_variables_destroy(var);
		return -1;
	}

	ast_variable_append(cat, var);

	if (ast_config_text_file_save(filename, msg_cfg, "app_voicemail")) {
		ast_log(LOG_WARNING, "Unable to update %s to have a message ID\n", filename);
		return -1;
	}

	UPDATE_MSG_ID(dir, msg, id, vmu, msg_cfg, folder);
	return 0;
}

static struct ast_vm_user *find_or_create(const char *context, const char *box)
{
	struct ast_vm_user *vmu;

	if (!ast_strlen_zero(box) && box[0] == '*') {
		ast_log(LOG_WARNING, "Mailbox %s in context %s begins with '*' character.  The '*' character,"
				"\n\twhen it is the first character in a mailbox or password, is used to jump to a"
				"\n\tpredefined extension 'a'.  A mailbox or password beginning with '*' is not valid"
				"\n\tand will be ignored.\n", box, context);
		return NULL;
	}

	AST_LIST_TRAVERSE(&users, vmu, list) {
		if (ast_test_flag((&globalflags), VM_SEARCH) && !strcasecmp(box, vmu->mailbox)) {
			if (strcasecmp(vmu->context, context)) {
				ast_log(LOG_WARNING, "\nIt has been detected that you have defined mailbox '%s' in separate\
						\n\tcontexts and that you have the 'searchcontexts' option on. This type of\
						\n\tconfiguration creates an ambiguity that you likely do not want. Please\
						\n\tamend your voicemail.conf file to avoid this situation.\n", box);
			}
			ast_log(LOG_WARNING, "Ignoring duplicated mailbox %s\n", box);
			return NULL;
		}
		if (!strcasecmp(context, vmu->context) && !strcasecmp(box, vmu->mailbox)) {
			ast_log(LOG_WARNING, "Ignoring duplicated mailbox %s in context %s\n", box, context);
			return NULL;
		}
	}

	if (!(vmu = ast_calloc(1, sizeof(*vmu))))
		return NULL;

	ast_copy_string(vmu->context, context, sizeof(vmu->context));
	ast_copy_string(vmu->mailbox, box, sizeof(vmu->mailbox));

	AST_LIST_INSERT_TAIL(&users, vmu, list);

	return vmu;
}

static int append_mailbox(const char *context, const char *box, const char *data)
{
	/* Assumes lock is already held */
	char *tmp;
	char *stringp;
	char *s;
	struct ast_vm_user *vmu;
	char mailbox_full[MAX_VM_MAILBOX_LEN];
	int new = 0, old = 0, urgent = 0;
	char secretfn[PATH_MAX] = "";

	tmp = ast_strdupa(data);

	if (!(vmu = find_or_create(context, box)))
		return -1;

	populate_defaults(vmu);

	stringp = tmp;
	if ((s = strsep(&stringp, ","))) {
		if (!ast_strlen_zero(s) && s[0] == '*') {
			ast_log(LOG_WARNING, "Invalid password detected for mailbox %s.  The password"
				"\n\tmust be reset in voicemail.conf.\n", box);
		}
		/* assign password regardless of validity to prevent NULL password from being assigned */
		ast_copy_string(vmu->password, s, sizeof(vmu->password));
	}
	if (stringp && (s = strsep(&stringp, ","))) {
		ast_copy_string(vmu->fullname, s, sizeof(vmu->fullname));
	}
	if (stringp && (s = strsep(&stringp, ","))) {
		vmu->email = ast_strdup(s);
	}
	if (stringp && (s = strsep(&stringp, ","))) {
		ast_copy_string(vmu->pager, s, sizeof(vmu->pager));
	}
	if (stringp) {
		apply_options(vmu, stringp);
	}

	switch (vmu->passwordlocation) {
	case OPT_PWLOC_SPOOLDIR:
		snprintf(secretfn, sizeof(secretfn), "%s%s/%s/secret.conf", VM_SPOOL_DIR, vmu->context, vmu->mailbox);
		read_password_from_file(secretfn, vmu->password, sizeof(vmu->password));
	}

	snprintf(mailbox_full, MAX_VM_MAILBOX_LEN, "%s%s%s",
		box,
		ast_strlen_zero(context) ? "" : "@",
		context);

	inboxcount2(mailbox_full, &urgent, &new, &old);
#ifdef IMAP_STORAGE
	imap_logout(mailbox_full);
#endif
	queue_mwi_event(NULL, mailbox_full, urgent, new, old);

	return 0;
}

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_voicemail_vmuser)
{
	int res = 0;
	struct ast_vm_user *vmu;
	/* language parameter seems to only be used for display in manager action */
	static const char options_string[] = "attach=yes|attachfmt=wav49|"
		"serveremail=someguy@digium.com|fromstring=Voicemail System|tz=central|delete=yes|saycid=yes|"
		"sendvoicemail=yes|review=yes|tempgreetwarn=yes|messagewrap=yes|operator=yes|"
		"envelope=yes|moveheard=yes|sayduration=yes|saydurationm=5|forcename=yes|"
		"forcegreetings=yes|callback=somecontext|dialout=somecontext2|"
		"exitcontext=somecontext3|minsecs=10|maxsecs=100|nextaftercmd=yes|"
		"backupdeleted=50|volgain=1.3|passwordlocation=spooldir|emailbody="
		"Dear ${VM_NAME}:\n\n\tYou were just left a ${VM_DUR} long message|emailsubject="
		"[PBX]: New message \\\\${VM_MSGNUM}\\\\ in mailbox ${VM_MAILBOX}";
#ifdef IMAP_STORAGE
	static const char option_string2[] = "imapuser=imapuser|imappassword=imappasswd|"
		"imapfolder=INBOX|imapvmshareid=6000|imapserver=imapserver|imapport=1234|imapflags=flagged";
#endif

	switch (cmd) {
	case TEST_INIT:
		info->name = "vmuser";
		info->category = "/apps/app_voicemail/";
		info->summary = "Vmuser unit test";
		info->description =
			"This tests passing all supported parameters to apply_options, the voicemail user config parser";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(vmu = ast_calloc(1, sizeof(*vmu)))) {
		return AST_TEST_NOT_RUN;
	}
	populate_defaults(vmu);
	ast_set_flag(vmu, VM_ALLOCED);

	apply_options(vmu, options_string);

	if (!ast_test_flag(vmu, VM_ATTACH)) {
		ast_test_status_update(test, "Parse failure for attach option\n");
		res = 1;
	}
	if (strcasecmp(vmu->attachfmt, "wav49")) {
		ast_test_status_update(test, "Parse failure for attachfmt option\n");
		res = 1;
	}
	if (strcasecmp(vmu->fromstring, "Voicemail System")) {
		ast_test_status_update(test, "Parse failure for fromstring option\n");
		res = 1;
	}
	if (strcasecmp(vmu->serveremail, "someguy@digium.com")) {
		ast_test_status_update(test, "Parse failure for serveremail option\n");
		res = 1;
	}
	if (!vmu->emailsubject || strcasecmp(vmu->emailsubject, "[PBX]: New message \\${VM_MSGNUM}\\ in mailbox ${VM_MAILBOX}")) {
		ast_test_status_update(test, "Parse failure for emailsubject option\n");
		res = 1;
	}
	if (!vmu->emailbody || strcasecmp(vmu->emailbody, "Dear ${VM_NAME}:\n\n\tYou were just left a ${VM_DUR} long message")) {
		ast_test_status_update(test, "Parse failure for emailbody option\n");
		res = 1;
	}
	if (strcasecmp(vmu->zonetag, "central")) {
		ast_test_status_update(test, "Parse failure for tz option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_DELETE)) {
		ast_test_status_update(test, "Parse failure for delete option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_SAYCID)) {
		ast_test_status_update(test, "Parse failure for saycid option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_SVMAIL)) {
		ast_test_status_update(test, "Parse failure for sendvoicemail option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_REVIEW)) {
		ast_test_status_update(test, "Parse failure for review option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_TEMPGREETWARN)) {
		ast_test_status_update(test, "Parse failure for tempgreetwarm option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_MESSAGEWRAP)) {
		ast_test_status_update(test, "Parse failure for messagewrap option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_OPERATOR)) {
		ast_test_status_update(test, "Parse failure for operator option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_ENVELOPE)) {
		ast_test_status_update(test, "Parse failure for envelope option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_MOVEHEARD)) {
		ast_test_status_update(test, "Parse failure for moveheard option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_SAYDURATION)) {
		ast_test_status_update(test, "Parse failure for sayduration option\n");
		res = 1;
	}
	if (vmu->saydurationm != 5) {
		ast_test_status_update(test, "Parse failure for saydurationm option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_FORCENAME)) {
		ast_test_status_update(test, "Parse failure for forcename option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_FORCEGREET)) {
		ast_test_status_update(test, "Parse failure for forcegreetings option\n");
		res = 1;
	}
	if (strcasecmp(vmu->callback, "somecontext")) {
		ast_test_status_update(test, "Parse failure for callbacks option\n");
		res = 1;
	}
	if (strcasecmp(vmu->dialout, "somecontext2")) {
		ast_test_status_update(test, "Parse failure for dialout option\n");
		res = 1;
	}
	if (strcasecmp(vmu->exit, "somecontext3")) {
		ast_test_status_update(test, "Parse failure for exitcontext option\n");
		res = 1;
	}
	if (vmu->minsecs != 10) {
		ast_test_status_update(test, "Parse failure for minsecs option\n");
		res = 1;
	}
	if (vmu->maxsecs != 100) {
		ast_test_status_update(test, "Parse failure for maxsecs option\n");
		res = 1;
	}
	if (!ast_test_flag(vmu, VM_SKIPAFTERCMD)) {
		ast_test_status_update(test, "Parse failure for nextaftercmd option\n");
		res = 1;
	}
	if (vmu->maxdeletedmsg != 50) {
		ast_test_status_update(test, "Parse failure for backupdeleted option\n");
		res = 1;
	}
	if (vmu->volgain != 1.3) {
		ast_test_status_update(test, "Parse failure for volgain option\n");
		res = 1;
	}
	if (vmu->passwordlocation != OPT_PWLOC_SPOOLDIR) {
		ast_test_status_update(test, "Parse failure for passwordlocation option\n");
		res = 1;
	}
#ifdef IMAP_STORAGE
	apply_options(vmu, option_string2);

	if (strcasecmp(vmu->imapuser, "imapuser")) {
		ast_test_status_update(test, "Parse failure for imapuser option\n");
		res = 1;
	}
	if (strcasecmp(vmu->imappassword, "imappasswd")) {
		ast_test_status_update(test, "Parse failure for imappasswd option\n");
		res = 1;
	}
	if (strcasecmp(vmu->imapfolder, "INBOX")) {
		ast_test_status_update(test, "Parse failure for imapfolder option\n");
		res = 1;
	}
	if (strcasecmp(vmu->imapvmshareid, "6000")) {
		ast_test_status_update(test, "Parse failure for imapvmshareid option\n");
		res = 1;
	}
	if (strcasecmp(vmu->imapserver, "imapserver")) {
		ast_test_status_update(test, "Parse failure for imapserver option\n");
		res = 1;
	}
	if (strcasecmp(vmu->imapport, "1234")) {
		ast_test_status_update(test, "Parse failure for imapport option\n");
		res = 1;
	}
	if (strcasecmp(vmu->imapflags, "flagged")) {
		ast_test_status_update(test, "Parse failure for imapflags option\n");
		res = 1;
	}
#endif

	free_user(vmu);
	return res ? AST_TEST_FAIL : AST_TEST_PASS;
}
#endif

static int acf_vm_info(struct ast_channel *chan, const char *cmd, char *args, char *buf, size_t len)
{
	struct ast_vm_user svm;
	struct ast_vm_user *vmu = NULL;
	char *parse;
	char *mailbox;
	char *context;
	int res = 0;

	AST_DECLARE_APP_ARGS(arg,
		AST_APP_ARG(mailbox_context);
		AST_APP_ARG(attribute);
		AST_APP_ARG(folder);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(args)) {
		ast_log(LOG_ERROR, "VM_INFO requires an argument (<mailbox>[@<context>],attribute[,folder])\n");
		return -1;
	}

	parse = ast_strdupa(args);
	AST_STANDARD_APP_ARGS(arg, parse);

	if (ast_strlen_zero(arg.mailbox_context)
		|| ast_strlen_zero(arg.attribute)
		|| separate_mailbox(ast_strdupa(arg.mailbox_context), &mailbox, &context)) {
		ast_log(LOG_ERROR, "VM_INFO requires an argument (<mailbox>[@<context>],attribute[,folder])\n");
		return -1;
	}

	memset(&svm, 0, sizeof(svm));
	vmu = find_user(&svm, context, mailbox);

	if (!strncasecmp(arg.attribute, "exists", 5)) {
		ast_copy_string(buf, vmu ? "1" : "0", len);
		free_user(vmu);
		return 0;
	}

	if (vmu) {
		if (!strncasecmp(arg.attribute, "password", 8)) {
			ast_copy_string(buf, vmu->password, len);
		} else if (!strncasecmp(arg.attribute, "fullname", 8)) {
			ast_copy_string(buf, vmu->fullname, len);
		} else if (!strncasecmp(arg.attribute, "email", 5)) {
			ast_copy_string(buf, vmu->email, len);
		} else if (!strncasecmp(arg.attribute, "pager", 5)) {
			ast_copy_string(buf, vmu->pager, len);
		} else if (!strncasecmp(arg.attribute, "language", 8)) {
			ast_copy_string(buf, S_OR(vmu->language, ast_channel_language(chan)), len);
		} else if (!strncasecmp(arg.attribute, "locale", 6)) {
			ast_copy_string(buf, vmu->locale, len);
		} else if (!strncasecmp(arg.attribute, "tz", 2)) {
			ast_copy_string(buf, vmu->zonetag, len);
		} else if (!strncasecmp(arg.attribute, "count", 5)) {
			char *mailbox_id;

			mailbox_id = ast_alloca(strlen(mailbox) + strlen(context) + 2);
			sprintf(mailbox_id, "%s@%s", mailbox, context);/* Safe */

			/* If mbxfolder is empty messagecount will default to INBOX */
			res = messagecount(mailbox_id, arg.folder);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to retrieve message count for mailbox %s\n", arg.mailbox_context);
				free_user(vmu);
				return -1;
			}
			snprintf(buf, len, "%d", res);
		} else {
			ast_log(LOG_ERROR, "Unknown attribute '%s' for VM_INFO\n", arg.attribute);
			free_user(vmu);
			return -1;
		}
		free_user(vmu);
	}

	return 0;
}

static struct ast_custom_function vm_info_acf = {
	.name = "VM_INFO",
	.read = acf_vm_info,
};

static int vmauthenticate(struct ast_channel *chan, const char *data)
{
	char *s, *user = NULL, *context = NULL, mailbox[AST_MAX_EXTENSION] = "";
	struct ast_vm_user vmus = {{0}};
	char *options = NULL;
	int silent = 0, skipuser = 0;
	int res = -1;

	if (data) {
		s = ast_strdupa(data);
		user = strsep(&s, ",");
		options = strsep(&s, ",");
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
	} else if (mailbox[0] == '*') {
		/* user entered '*' */
		if (!ast_goto_if_exists(chan, ast_channel_context(chan), "a", 1)) {
			res = 0;	/* prevent hangup */
		}
	}

	return res;
}

static char *show_users_realtime(int fd, const char *context)
{
	struct ast_config *cfg;
	const char *cat = NULL;

	if (!(cfg = ast_load_realtime_multientry("voicemail",
		"context", context, SENTINEL))) {
		return CLI_FAILURE;
	}

	ast_cli(fd,
		"\n"
		"=============================================================\n"
		"=== Configured Voicemail Users ==============================\n"
		"=============================================================\n"
		"===\n");

	while ((cat = ast_category_browse(cfg, cat))) {
		struct ast_variable *var = NULL;
		ast_cli(fd,
			"=== Mailbox ...\n"
			"===\n");
		for (var = ast_variable_browse(cfg, cat); var; var = var->next)
			ast_cli(fd, "=== ==> %s: %s\n", var->name, var->value);
		ast_cli(fd,
			"===\n"
			"=== ---------------------------------------------------------\n"
			"===\n");
	}

	ast_cli(fd,
		"=============================================================\n"
		"\n");

	ast_config_destroy(cfg);

	return CLI_SUCCESS;
}

static char *complete_voicemail_show_users(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	int wordlen;
	struct ast_vm_user *vmu;
	const char *context = "";
	char *ret;

	/* 0 - voicemail; 1 - show; 2 - users; 3 - for; 4 - <context> */
	if (pos > 4)
		return NULL;
	wordlen = strlen(word);
	AST_LIST_LOCK(&users);
	AST_LIST_TRAVERSE(&users, vmu, list) {
		if (!strncasecmp(word, vmu->context, wordlen)) {
			if (context && strcmp(context, vmu->context) && ++which > state) {
				ret = ast_strdup(vmu->context);
				AST_LIST_UNLOCK(&users);
				return ret;
			}
			/* ignore repeated contexts ? */
			context = vmu->context;
		}
	}
	AST_LIST_UNLOCK(&users);
	return NULL;
}

/*! \brief Show a list of voicemail users in the CLI */
static char *handle_voicemail_show_users(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_vm_user *vmu;
#define HVSU_OUTPUT_FORMAT "%-10s %-5s %-25s %-10s %6s\n"
	const char *context = NULL;
	int users_counter = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail show users [for]";
		e->usage =
			"Usage: voicemail show users [for <context>]\n"
			"       Lists all mailboxes currently set up\n";
		return NULL;
	case CLI_GENERATE:
		return complete_voicemail_show_users(a->line, a->word, a->pos, a->n);
	}

	if ((a->argc < 3) || (a->argc > 5) || (a->argc == 4))
		return CLI_SHOWUSAGE;
	if (a->argc == 5) {
		if (strcmp(a->argv[3],"for"))
			return CLI_SHOWUSAGE;
		context = a->argv[4];
	}

	if (ast_check_realtime("voicemail")) {
		if (!context) {
			ast_cli(a->fd, "You must specify a specific context to show users from realtime!\n");
			return CLI_SHOWUSAGE;
		}
		return show_users_realtime(a->fd, context);
	}

	AST_LIST_LOCK(&users);
	if (AST_LIST_EMPTY(&users)) {
		ast_cli(a->fd, "There are no voicemail users currently defined\n");
		AST_LIST_UNLOCK(&users);
		return CLI_FAILURE;
	}
	if (!context) {
		ast_cli(a->fd, HVSU_OUTPUT_FORMAT, "Context", "Mbox", "User", "Zone", "NewMsg");
	} else {
		int count = 0;
		AST_LIST_TRAVERSE(&users, vmu, list) {
			if (!strcmp(context, vmu->context)) {
				count++;
				break;
			}
		}
		if (count) {
			ast_cli(a->fd, HVSU_OUTPUT_FORMAT, "Context", "Mbox", "User", "Zone", "NewMsg");
		} else {
			ast_cli(a->fd, "No such voicemail context \"%s\"\n", context);
			AST_LIST_UNLOCK(&users);
			return CLI_FAILURE;
		}
	}
	AST_LIST_TRAVERSE(&users, vmu, list) {
		int newmsgs = 0, oldmsgs = 0;
		char count[12], tmp[256] = "";

		if (!context || !strcmp(context, vmu->context)) {
			snprintf(tmp, sizeof(tmp), "%s@%s", vmu->mailbox, ast_strlen_zero(vmu->context) ? "default" : vmu->context);
			inboxcount(tmp, &newmsgs, &oldmsgs);
			snprintf(count, sizeof(count), "%d", newmsgs);
			ast_cli(a->fd, HVSU_OUTPUT_FORMAT, vmu->context, vmu->mailbox, vmu->fullname, vmu->zonetag, count);
			users_counter++;
		}
	}
	AST_LIST_UNLOCK(&users);
	ast_cli(a->fd, "%d voicemail users configured.\n", users_counter);
	return CLI_SUCCESS;
}

/*! \brief Show a list of voicemail zones in the CLI */
static char *handle_voicemail_show_zones(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct vm_zone *zone;
#define HVSZ_OUTPUT_FORMAT "%-15s %-20s %-45s\n"
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail show zones";
		e->usage =
			"Usage: voicemail show zones\n"
			"       Lists zone message formats\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&zones);
	if (!AST_LIST_EMPTY(&zones)) {
		ast_cli(a->fd, HVSZ_OUTPUT_FORMAT, "Zone", "Timezone", "Message Format");
		AST_LIST_TRAVERSE(&zones, zone, list) {
			ast_cli(a->fd, HVSZ_OUTPUT_FORMAT, zone->name, zone->timezone, zone->msg_format);
		}
	} else {
		ast_cli(a->fd, "There are no voicemail zones currently defined\n");
		res = CLI_FAILURE;
	}
	AST_LIST_UNLOCK(&zones);

	return res;
}

/*! \brief Show a list of voicemail zones in the CLI */
static char *handle_voicemail_show_aliases(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator aliases;
	struct alias_mailbox_mapping *mapping;
#define ALIASES_OUTPUT_FORMAT "%-32s %-32s\n"
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail show aliases";
		e->usage =
			"Usage: voicemail show aliases\n"
			"       Lists mailbox aliases\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	if (ast_strlen_zero(aliasescontext)) {
		ast_cli(a->fd, "Aliases are not enabled\n");
		return res;
	}

	ast_cli(a->fd, "Aliases context: %s\n", aliasescontext);
	ast_cli(a->fd, ALIASES_OUTPUT_FORMAT, "Alias", "Mailbox");

	aliases = ao2_iterator_init(alias_mailbox_mappings, 0);
	while ((mapping = ao2_iterator_next(&aliases))) {
		ast_cli(a->fd, ALIASES_OUTPUT_FORMAT, mapping->alias, mapping->mailbox);
		ao2_ref(mapping, -1);
	}
	ao2_iterator_destroy(&aliases);

	return res;
}

/*! \brief Reload voicemail configuration from the CLI */
static char *handle_voicemail_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voicemail reload";
		e->usage =
			"Usage: voicemail reload\n"
			"       Reload voicemail configuration\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "Reloading voicemail configuration...\n");
	load_config(1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_voicemail[] = {
	AST_CLI_DEFINE(handle_voicemail_show_users, "List defined voicemail boxes"),
	AST_CLI_DEFINE(handle_voicemail_show_zones, "List zone message formats"),
	AST_CLI_DEFINE(handle_voicemail_show_aliases, "List mailbox aliases"),
	AST_CLI_DEFINE(handle_voicemail_reload, "Reload voicemail configuration"),
	AST_CLI_DEFINE(handle_voicemail_show_mailbox, "Display a mailbox's content details"),
	AST_CLI_DEFINE(handle_voicemail_forward_message, "Forward message to another folder"),
	AST_CLI_DEFINE(handle_voicemail_move_message, "Move message to another folder"),
	AST_CLI_DEFINE(handle_voicemail_remove_message, "Remove message"),
};

static int poll_subscribed_mailbox(struct ast_mwi_state *mwi_state, void *data)
{
	int new = 0, old = 0, urgent = 0;

	if (!mwi_state) {
		/* This should only occur due to allocation failure of a default mwi state object */
		return 0;
	}

	inboxcount2(mwi_state->uniqueid, &urgent, &new, &old);

#ifdef IMAP_STORAGE
	if (imap_poll_logout) {
		imap_logout(mwi_state->uniqueid);
	}
#endif

	if (urgent != mwi_state->urgent_msgs || new != mwi_state->new_msgs || old != mwi_state->old_msgs) {
		queue_mwi_event(NULL, mwi_state->uniqueid, urgent, new, old);
		run_externnotify(NULL, mwi_state->uniqueid, NULL);
	}

	return 0;
}

static void *mb_poll_thread(void *data)
{
	while (poll_thread_run) {
		struct timespec ts = { 0, };
		struct timeval wait;

		ast_mwi_state_callback_subscribed(poll_subscribed_mailbox, NULL);

		if (!poll_thread_run) {
			break;
		}

		wait = ast_tvadd(ast_tvnow(), ast_samp2tv(poll_freq, 1));
		ts.tv_sec = wait.tv_sec;
		ts.tv_nsec = wait.tv_usec * 1000;

		ast_mutex_lock(&poll_lock);
		ast_cond_timedwait(&poll_cond, &poll_lock, &ts);
		ast_mutex_unlock(&poll_lock);
	}

	return NULL;
}

#ifdef IMAP_STORAGE
static void imap_logout(const char *mailbox_id)
{
	char *context;
	char *mailbox;
	struct ast_vm_user vmus;
	RAII_VAR(struct ast_vm_user *, vmu, NULL, free_user);
	struct vm_state *vms = NULL;

	if (ast_strlen_zero(mailbox_id)
		|| separate_mailbox(ast_strdupa(mailbox_id), &mailbox, &context)) {
		return;
	}

	memset(&vmus, 0, sizeof(vmus));

	if (!(vmu = find_user(&vmus, context, mailbox)) || vmu->imapuser[0] == '\0') {
		return;
	}

	vms = get_vm_state_by_imapuser(vmu->imapuser, 0);
	if (!vms) {
		vms = get_vm_state_by_mailbox(mailbox, context, 0);
	}
	if (!vms) {
		return;
	}

	ast_mutex_lock(&vms->lock);
	vms->mailstream = mail_close(vms->mailstream);
	ast_mutex_unlock(&vms->lock);

	vmstate_delete(vms);
}

static int imap_close_subscribed_mailbox(struct ast_mwi_state *mwi_state, void *data)
{
	if (mwi_state && !ast_strlen_zero(mwi_state->uniqueid)) {
		imap_logout(mwi_state->uniqueid);
	}

	return 0;
}

#endif

static int mwi_handle_unsubscribe2(void *data)
{
	struct ast_mwi_state *mwi_state = data;

	/*
	 * Go ahead and clear the implicit MWI publisher here to avoid a leak. If a backing
	 * configuration is available it'll re-initialize (reset the cached state) on its
	 * next publish.
	 */
	ast_delete_mwi_state_full(mwi_state->uniqueid, NULL, NULL);

#ifdef IMAP_STORAGE
	imap_close_subscribed_mailbox(mwi_state, NULL);
#endif

	ao2_ref(mwi_state, -1);
	return 0;
}

static void mwi_handle_unsubscribe(const char *id, struct ast_mwi_subscriber *sub)
{
	void *data = ast_mwi_subscriber_data(sub);

	/* Don't bump data's reference. We'll just use the one returned above */
	if (ast_taskprocessor_push(mwi_subscription_tps, mwi_handle_unsubscribe2, data) < 0) {
		/* A reference was returned for data when retrieving, so remove it on error */
		ao2_ref(data, -1);
	}
}

static int mwi_handle_subscribe2(void *data)
{
	poll_subscribed_mailbox(data, NULL);
	ao2_ref(data, -1);
	return 0;
}

static void mwi_handle_subscribe(const char *id, struct ast_mwi_subscriber *sub)
{
	void *data = ast_mwi_subscriber_data(sub);

	/* Don't bump data's reference. We'll just use the one returned above */
	if (ast_taskprocessor_push(mwi_subscription_tps, mwi_handle_subscribe2, data) < 0) {
		/* A reference was returned for data when retrieving, so remove it on error */
		ao2_ref(data, -1);
	}
}

struct ast_mwi_observer mwi_observer = {
	.on_subscribe = mwi_handle_subscribe,
	.on_unsubscribe = mwi_handle_unsubscribe,
};

static void start_poll_thread(void)
{
	int errcode;
	ast_mwi_add_observer(&mwi_observer);

	poll_thread_run = 1;

	if ((errcode = ast_pthread_create(&poll_thread, NULL, mb_poll_thread, NULL))) {
		ast_log(LOG_ERROR, "Could not create thread: %s\n", strerror(errcode));
	}
}

static void stop_poll_thread(void)
{
	poll_thread_run = 0;

	ast_mutex_lock(&poll_lock);
	ast_cond_signal(&poll_cond);
	ast_mutex_unlock(&poll_lock);

	pthread_join(poll_thread, NULL);
	poll_thread = AST_PTHREADT_NULL;

	ast_mwi_remove_observer(&mwi_observer);
}

/*!
 * \brief Append vmu info string into given astman with event_name.
 * \return 0 failed. 1 otherwise.
*/
static int append_vmu_info_astman(
		struct mansession *s,
		struct ast_vm_user *vmu,
		const char* event_name,
		const char* actionid
		)
{
	int new;
	int old;
	char *mailbox;
	int ret;

	if((s == NULL) || (vmu == NULL) || (event_name == NULL) || (actionid == NULL)) {
		ast_log(LOG_ERROR, "Wrong input parameter.");
		return 0;
	}

	/* create mailbox string */
	if (!ast_strlen_zero(vmu->context)) {
		ret = ast_asprintf(&mailbox, "%s@%s", vmu->mailbox, vmu->context);
	} else {
		ret = ast_asprintf(&mailbox, "%s", vmu->mailbox);
	}
	if (ret == -1) {
		ast_log(LOG_ERROR, "Could not create mailbox string. err[%s]\n", strerror(errno));
		return 0;
	}

	/* get mailbox count */
	ret = inboxcount(mailbox, &new, &old);
	ast_free(mailbox);
	if (ret == -1) {
		ast_log(LOG_ERROR, "Could not get mailbox count. user[%s], context[%s]\n",
			vmu->mailbox ?: "", vmu->context ?: "");
		return 0;
	}

	astman_append(s,
		"Event: %s\r\n"
		"%s"
		"VMContext: %s\r\n"
		"VoiceMailbox: %s\r\n"
		"Fullname: %s\r\n"
		"Email: %s\r\n"
		"Pager: %s\r\n"
		"ServerEmail: %s\r\n"
		"FromString: %s\r\n"
		"MailCommand: %s\r\n"
		"Language: %s\r\n"
		"TimeZone: %s\r\n"
		"Callback: %s\r\n"
		"Dialout: %s\r\n"
		"UniqueID: %s\r\n"
		"ExitContext: %s\r\n"
		"SayDurationMinimum: %d\r\n"
		"SayEnvelope: %s\r\n"
		"SayCID: %s\r\n"
		"AttachMessage: %s\r\n"
		"AttachmentFormat: %s\r\n"
		"DeleteMessage: %s\r\n"
		"VolumeGain: %.2f\r\n"
		"CanReview: %s\r\n"
		"CallOperator: %s\r\n"
		"MaxMessageCount: %d\r\n"
		"MaxMessageLength: %d\r\n"
		"NewMessageCount: %d\r\n"
		"OldMessageCount: %d\r\n"
#ifdef IMAP_STORAGE
		"IMAPUser: %s\r\n"
		"IMAPServer: %s\r\n"
		"IMAPPort: %s\r\n"
		"IMAPFlags: %s\r\n"
#endif
		"\r\n",

		event_name,
		actionid,
		vmu->context,
		vmu->mailbox,
		vmu->fullname,
		vmu->email,
		vmu->pager,
		ast_strlen_zero(vmu->serveremail) ? serveremail : vmu->serveremail,
		ast_strlen_zero(vmu->fromstring) ? fromstring : vmu->fromstring,
		mailcmd,
		vmu->language,
		vmu->zonetag,
		vmu->callback,
		vmu->dialout,
		vmu->uniqueid,
		vmu->exit,
		vmu->saydurationm,
		ast_test_flag(vmu, VM_ENVELOPE) ? "Yes" : "No",
		ast_test_flag(vmu, VM_SAYCID) ? "Yes" : "No",
		ast_test_flag(vmu, VM_ATTACH) ? "Yes" : "No",
		vmu->attachfmt,
		ast_test_flag(vmu, VM_DELETE) ? "Yes" : "No",
		vmu->volgain,
		ast_test_flag(vmu, VM_REVIEW) ? "Yes" : "No",
		ast_test_flag(vmu, VM_OPERATOR) ? "Yes" : "No",
		vmu->maxmsg,
		vmu->maxsecs,
		new,
		old
#ifdef IMAP_STORAGE
		,
		vmu->imapuser,
		vmu->imapserver,
		vmu->imapport,
		vmu->imapflags
#endif
		);

	return 1;

}


/*!
 * \brief Append vmbox info string into given astman with event_name.
 * \return 0 if unable to append details, 1 otherwise.
*/
static int append_vmbox_info_astman(
		struct mansession *s,
		const struct message *m,
		struct ast_vm_user *vmu,
		const char* event_name,
		const char* actionid)
{
	struct ast_vm_mailbox_snapshot *mailbox_snapshot;
	struct ast_vm_msg_snapshot *msg;
	int nummessages = 0;
	int i;

	/* Take a snapshot of the mailbox */
	mailbox_snapshot = ast_vm_mailbox_snapshot_create(vmu->mailbox, vmu->context, NULL, 0, AST_VM_SNAPSHOT_SORT_BY_ID, 0);
	if (!mailbox_snapshot) {
		ast_log(LOG_ERROR, "Could not append voicemail box info for box %s@%s.",
			vmu->mailbox, vmu->context);
		return 0;
	}

	astman_send_listack(s, m, "Voicemail box detail will follow", "start");
	/* walk through each folder's contents and append info for each message */
	for (i = 0; i < mailbox_snapshot->folders; i++) {
		AST_LIST_TRAVERSE(&((mailbox_snapshot)->snapshots[i]), msg, msg) {
			astman_append(s,
				"Event: %s\r\n"
				"%s"
				"Folder: %s\r\n"
				"CallerID: %s\r\n"
				"Date: %s\r\n"
				"Duration: %s\r\n"
				"Flag: %s\r\n"
				"ID: %s\r\n"
				"\r\n",
				event_name,
				actionid,
				msg->folder_name,
				msg->callerid,
				msg->origdate,
				msg->duration,
				msg->flag,
				msg->msg_id
			);
			nummessages++;
		}
	}

	/* done, destroy. */
	mailbox_snapshot = ast_vm_mailbox_snapshot_destroy(mailbox_snapshot);
	astman_send_list_complete_start(s, m, "VoicemailBoxDetailComplete", nummessages);
	astman_send_list_complete_end(s);

	return 1;
}

static int manager_match_mailbox(struct ast_mwi_state *mwi_state, void *data)
{
	const char *context = astman_get_header(data, "Context");
	const char *mailbox = astman_get_header(data, "Mailbox");
	const char *at;

	if (!ast_strlen_zero(mwi_state->uniqueid)) {
		if (
			/* First case: everything matches */
			(ast_strlen_zero(context) && ast_strlen_zero(mailbox)) ||
			/* Second case: match the mailbox only */
			(ast_strlen_zero(context) && !ast_strlen_zero(mailbox) &&
			 (at = strchr(mwi_state->uniqueid, '@')) &&
			 strncmp(mailbox, mwi_state->uniqueid, at - mwi_state->uniqueid) == 0) ||
			/* Third case: match the context only */
			(!ast_strlen_zero(context) && ast_strlen_zero(mailbox) &&
			 (at = strchr(mwi_state->uniqueid, '@')) &&
			 strcmp(context, at + 1) == 0) ||
			/* Final case: match an exact specified mailbox */
			(!ast_strlen_zero(context) && !ast_strlen_zero(mailbox) &&
			 (at = strchr(mwi_state->uniqueid, '@')) &&
			 strncmp(mailbox, mwi_state->uniqueid, at - mwi_state->uniqueid) == 0 &&
			 strcmp(context, at + 1) == 0)
			) {
			poll_subscribed_mailbox(mwi_state, NULL);
		}
	}

	return 0;
}

static int manager_voicemail_refresh(struct mansession *s, const struct message *m)
{
	ast_mwi_state_callback_all(manager_match_mailbox, (void *)m);
	astman_send_ack(s, m, "Refresh sent");
	return RESULT_SUCCESS;
}

static int manager_status_voicemail_user(struct mansession *s, const struct message *m)
{
	struct ast_vm_user *vmu = NULL;
	const char *id = astman_get_header(m, "ActionID");
	char actionid[128];
	struct ast_vm_user svm;
	int ret;

	const char *context = astman_get_header(m, "Context");
	const char *mailbox = astman_get_header(m, "Mailbox");

	if ((ast_strlen_zero(context) || ast_strlen_zero(mailbox))) {
		astman_send_error(s, m, "Need 'Context' and 'Mailbox' parameters.");
		return RESULT_SUCCESS;
	}

	actionid[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(actionid, sizeof(actionid), "ActionID: %s\r\n", id);
	}

	/* find user */
	memset(&svm, 0, sizeof(svm));
	vmu = find_user(&svm, context, mailbox);
	if (!vmu) {
		/* could not find it */
		astman_send_ack(s, m, "There is no voicemail user of the given info.");
		return RESULT_SUCCESS;
	}

	astman_send_listack(s, m, "Voicemail user detail will follow", "start");

	/* append vmu info event */
	ret = append_vmu_info_astman(s, vmu, "VoicemailUserDetail", actionid);
	free_user(vmu);
	if(ret == 0) {
		ast_log(LOG_ERROR, "Could not append voicemail user info.");
	}

	astman_send_list_complete_start(s, m, "VoicemailUserDetailComplete", 1);
	astman_send_list_complete_end(s);

	return RESULT_SUCCESS;
}

/*! \brief Manager list voicemail users command */
static int manager_list_voicemail_users(struct mansession *s, const struct message *m)
{
	struct ast_vm_user *vmu = NULL;
	const char *id = astman_get_header(m, "ActionID");
	char actionid[128];
	int num_users = 0;
	int ret;

	actionid[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(actionid, sizeof(actionid), "ActionID: %s\r\n", id);
	}

	AST_LIST_LOCK(&users);

	if (AST_LIST_EMPTY(&users)) {
		astman_send_ack(s, m, "There are no voicemail users currently defined.");
		AST_LIST_UNLOCK(&users);
		return RESULT_SUCCESS;
	}

	astman_send_listack(s, m, "Voicemail user list will follow", "start");

	AST_LIST_TRAVERSE(&users, vmu, list) {
		/* append vmu info event */
		ret = append_vmu_info_astman(s, vmu, "VoicemailUserEntry", actionid);
		if(ret == 0) {
			ast_log(LOG_ERROR, "Could not append voicemail user info.");
			continue;
		}
		++num_users;
	}

	astman_send_list_complete_start(s, m, "VoicemailUserEntryComplete", num_users);
	astman_send_list_complete_end(s);

	AST_LIST_UNLOCK(&users);

	return RESULT_SUCCESS;
}

static int manager_get_mailbox_summary(struct mansession *s, const struct message *m)
{
	struct ast_vm_user *vmu = NULL;
	const char *id = astman_get_header(m, "ActionID");
	char actionid[128];
	struct ast_vm_user svm;

	const char *context = astman_get_header(m, "Context");
	const char *mailbox = astman_get_header(m, "Mailbox");

	if ((ast_strlen_zero(context) || ast_strlen_zero(mailbox))) {
		astman_send_error(s, m, "Need 'Context' and 'Mailbox' parameters.");
		return 0;
	}

	actionid[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(actionid, sizeof(actionid), "ActionID: %s\r\n", id);
	}

	/* find user */
	memset(&svm, 0, sizeof(svm));
	vmu = find_user(&svm, context, mailbox);
	if (!vmu) {
		/* could not find it */
		astman_send_ack(s, m, "There is no voicemail user matching the given user.");
		return 0;
	}

	/* Append the mailbox details */
	if (!append_vmbox_info_astman(s, m, vmu, "VoicemailBoxDetail", actionid)) {
		astman_send_error(s, m, "Unable to get mailbox info for the given user.");
	}

	free_user(vmu);
	return 0;
}

static int manager_voicemail_move(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	const char *context = astman_get_header(m, "Context");
	const char *from_folder = astman_get_header(m, "Folder");
	const char *id[] = { astman_get_header(m, "ID") };
	const char *to_folder = astman_get_header(m, "ToFolder");

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified, required");
		return 0;
	}
	if (ast_strlen_zero(context)) {
		astman_send_error(s, m, "Context not specified, required");
		return 0;
	}
	if (ast_strlen_zero(from_folder)) {
		astman_send_error(s, m, "Folder not specified, required");
		return 0;
	}
	if (ast_strlen_zero(id[0])) {
		astman_send_error(s, m, "ID not specified, required");
		return 0;
	}
	if (ast_strlen_zero(to_folder)) {
		astman_send_error(s, m, "ToFolder not specified, required");
		return 0;
	}

	if (vm_msg_move(mailbox, context, 1, from_folder, id, to_folder)) {
		astman_send_ack(s, m, "Message move failed\n");
	} else {
		astman_send_ack(s, m, "Message move successful\n");
	}

	return 0;
}

static int manager_voicemail_remove(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	const char *context = astman_get_header(m, "Context");
	const char *folder = astman_get_header(m, "Folder");
	const char *id[] = { astman_get_header(m, "ID") };

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified, required");
		return 0;
	}
	if (ast_strlen_zero(context)) {
		astman_send_error(s, m, "Context not specified, required");
		return 0;
	}
	if (ast_strlen_zero(folder)) {
		astman_send_error(s, m, "Folder not specified, required");
		return 0;
	}
	if (ast_strlen_zero(id[0])) {
		astman_send_error(s, m, "ID not specified, required");
		return 0;
	}

	if (vm_msg_remove(mailbox, context, 1, folder, id)) {
		astman_send_ack(s, m, "Message remove failed\n");
	} else {
		astman_send_ack(s, m, "Message remove successful\n");
	}

	return 0;
}

static int manager_voicemail_forward(struct mansession *s, const struct message *m)
{
	const char *from_mailbox = astman_get_header(m, "Mailbox");
	const char *from_context = astman_get_header(m, "Context");
	const char *from_folder = astman_get_header(m, "Folder");
	const char *id[] = { astman_get_header(m, "ID") };
	const char *to_mailbox = astman_get_header(m, "ToMailbox");
	const char *to_context = astman_get_header(m, "ToContext");
	const char *to_folder = astman_get_header(m, "ToFolder");

	if (ast_strlen_zero(from_mailbox)) {
		astman_send_error(s, m, "Mailbox not specified, required");
		return 0;
	}
	if (ast_strlen_zero(from_context)) {
		astman_send_error(s, m, "Context not specified, required");
		return 0;
	}
	if (ast_strlen_zero(from_folder)) {
		astman_send_error(s, m, "Folder not specified, required");
		return 0;
	}
	if (ast_strlen_zero(id[0])) {
		astman_send_error(s, m, "ID not specified, required");
		return 0;
	}
	if (ast_strlen_zero(to_mailbox)) {
		astman_send_error(s, m, "ToMailbox not specified, required");
		return 0;
	}
	if (ast_strlen_zero(to_context)) {
		astman_send_error(s, m, "ToContext not specified, required");
		return 0;
	}
	if (ast_strlen_zero(to_folder)) {
		astman_send_error(s, m, "ToFolder not specified, required");
		return 0;
	}

	if (vm_msg_forward(from_mailbox, from_context, from_folder, to_mailbox, to_context, to_folder, 1, id, 0)) {
		astman_send_ack(s, m, "Message forward failed\n");
	} else {
		astman_send_ack(s, m, "Message forward successful\n");
	}

	return 0;
}

/*! \brief Free the users structure. */
static void free_vm_users(void)
{
	struct ast_vm_user *current;
	AST_LIST_LOCK(&users);
	while ((current = AST_LIST_REMOVE_HEAD(&users, list))) {
		ast_set_flag(current, VM_ALLOCED);
		free_user_final(current);
	}
	AST_LIST_UNLOCK(&users);
}

/*! \brief Free the zones structure. */
static void free_vm_zones(void)
{
	struct vm_zone *zcur;
	AST_LIST_LOCK(&zones);
	while ((zcur = AST_LIST_REMOVE_HEAD(&zones, list)))
		free_zone(zcur);
	AST_LIST_UNLOCK(&zones);
}

static const char *substitute_escapes(const char *value)
{
	char *current;

	/* Add 16 for fudge factor */
	struct ast_str *str = ast_str_thread_get(&ast_str_thread_global_buf, strlen(value) + 16);

	ast_str_reset(str);

	/* Substitute strings \r, \n, and \t into the appropriate characters */
	for (current = (char *) value; *current; current++) {
		if (*current == '\\') {
			current++;
			if (!*current) {
				ast_log(AST_LOG_NOTICE, "Incomplete escape at end of value.\n");
				break;
			}
			switch (*current) {
			case '\\':
				ast_str_append(&str, 0, "\\");
				break;
			case 'r':
				ast_str_append(&str, 0, "\r");
				break;
			case 'n':
#ifdef IMAP_STORAGE
				if (!str->used || str->str[str->used - 1] != '\r') {
					ast_str_append(&str, 0, "\r");
				}
#endif
				ast_str_append(&str, 0, "\n");
				break;
			case 't':
				ast_str_append(&str, 0, "\t");
				break;
			default:
				ast_log(AST_LOG_NOTICE, "Substitution routine does not support this character: \\%c\n", *current);
				break;
			}
		} else {
			ast_str_append(&str, 0, "%c", *current);
		}
	}

	return ast_str_buffer(str);
}

static int load_config(int reload)
{
	struct ast_config *cfg, *ucfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int res;

	ast_unload_realtime("voicemail");
	ast_unload_realtime("voicemail_data");

	if ((cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
		if ((ucfg = ast_config_load("users.conf", config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
			return 0;
		} else if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "Config file users.conf is in an invalid format.  Avoiding.\n");
			ucfg = NULL;
		}
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		if ((cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_config_destroy(ucfg);
			ast_log(LOG_ERROR, "Config file " VOICEMAIL_CONFIG " is in an invalid format.  Aborting.\n");
			return 0;
		}
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " VOICEMAIL_CONFIG " is in an invalid format.  Aborting.\n");
		return 0;
	} else {
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		if ((ucfg = ast_config_load("users.conf", config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "Config file users.conf is in an invalid format.  Avoiding.\n");
			ucfg = NULL;
		}
	}

	res = actual_load_config(reload, cfg, ucfg);

	ast_config_destroy(cfg);
	ast_config_destroy(ucfg);

	return res;
}

#ifdef TEST_FRAMEWORK
static int load_config_from_memory(int reload, struct ast_config *cfg, struct ast_config *ucfg)
{
	ast_unload_realtime("voicemail");
	ast_unload_realtime("voicemail_data");
	return actual_load_config(reload, cfg, ucfg);
}
#endif

static struct alias_mailbox_mapping *alias_mailbox_mapping_create(const char *alias, const char *mailbox)
{
	struct alias_mailbox_mapping *mapping;
	size_t from_len = strlen(alias) + 1;
	size_t to_len = strlen(mailbox) + 1;

	mapping = ao2_alloc(sizeof(*mapping) + from_len + to_len, NULL);
	if (!mapping) {
		return NULL;
	}
	mapping->alias = mapping->buf;
	mapping->mailbox = mapping->buf + from_len;
	ast_copy_string(mapping->alias, alias, from_len); /* Safe */
	ast_copy_string(mapping->mailbox, mailbox, to_len); /* Safe */

	return mapping;
}

static void load_aliases(struct ast_config *cfg)
{
	struct ast_variable *var;

	if (ast_strlen_zero(aliasescontext)) {
		return;
	}
	var = ast_variable_browse(cfg, aliasescontext);
	while (var) {
		struct alias_mailbox_mapping *mapping = alias_mailbox_mapping_create(var->name, var->value);
		if (mapping) {
			ao2_link(alias_mailbox_mappings, mapping);
			ao2_link(mailbox_alias_mappings, mapping);
			ao2_ref(mapping, -1);
		}
		var = var->next;
	}
}

static void load_zonemessages(struct ast_config *cfg)
{
	struct ast_variable *var;

	var = ast_variable_browse(cfg, "zonemessages");
	while (var) {
		if (var->value) {
			struct vm_zone *z;
			char *msg_format, *tzone;
			char storage[strlen(var->value) + 1];

			z = ast_malloc(sizeof(*z));
			if (!z) {
				return;
			}

			strcpy(storage, var->value); /* safe */
			msg_format = storage;
			tzone = strsep(&msg_format, "|,");
			if (msg_format) {
				ast_copy_string(z->name, var->name, sizeof(z->name));
				ast_copy_string(z->timezone, tzone, sizeof(z->timezone));
				ast_copy_string(z->msg_format, msg_format, sizeof(z->msg_format));
				AST_LIST_LOCK(&zones);
				AST_LIST_INSERT_HEAD(&zones, z, list);
				AST_LIST_UNLOCK(&zones);
			} else {
				ast_log(AST_LOG_WARNING, "Invalid timezone definition at line %d\n", var->lineno);
				ast_free(z);
			}
		}
		var = var->next;
	}
}

static void load_users(struct ast_config *cfg)
{
	struct ast_variable *var;
	char *cat = NULL;

	while ((cat = ast_category_browse(cfg, cat))) {
		if (strcasecmp(cat, "general") == 0
			|| strcasecmp(cat, aliasescontext) == 0
			|| strcasecmp(cat, "zonemessages") == 0) {
			continue;
		}

		var = ast_variable_browse(cfg, cat);
		while (var) {
			append_mailbox(cat, var->name, var->value);
			var = var->next;
		}
	}
}

static int actual_load_config(int reload, struct ast_config *cfg, struct ast_config *ucfg)
{
	struct ast_vm_user *current;
	char *cat;
	const char *val;
	char *q, *stringp, *tmp;
	int x;
	unsigned int tmpadsi[4];
	char secretfn[PATH_MAX] = "";
	long tps_queue_low;
	long tps_queue_high;

#ifdef IMAP_STORAGE
	ast_copy_string(imapparentfolder, "\0", sizeof(imapparentfolder));
#endif
	/* set audio control prompts */
	strcpy(listen_control_forward_key, DEFAULT_LISTEN_CONTROL_FORWARD_KEY);
	strcpy(listen_control_reverse_key, DEFAULT_LISTEN_CONTROL_REVERSE_KEY);
	strcpy(listen_control_pause_key, DEFAULT_LISTEN_CONTROL_PAUSE_KEY);
	strcpy(listen_control_restart_key, DEFAULT_LISTEN_CONTROL_RESTART_KEY);
	strcpy(listen_control_stop_key, DEFAULT_LISTEN_CONTROL_STOP_KEY);

#ifdef IMAP_STORAGE
	ast_mwi_state_callback_all(imap_close_subscribed_mailbox, NULL);
#endif

	/* Free all the users structure */
	free_vm_users();

	/* Free all the zones structure */
	free_vm_zones();

	/* Remove all aliases */
	ao2_callback(alias_mailbox_mappings, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
	ao2_callback(mailbox_alias_mappings, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	AST_LIST_LOCK(&users);

	memset(ext_pass_cmd, 0, sizeof(ext_pass_cmd));
	memset(ext_pass_check_cmd, 0, sizeof(ext_pass_check_cmd));

	if (cfg) {
		/* General settings */

		if (!(val = ast_variable_retrieve(cfg, "general", "userscontext")))
			val = "default";
		ast_copy_string(userscontext, val, sizeof(userscontext));

		aliasescontext[0] = '\0';
		val = ast_variable_retrieve(cfg, "general", "aliasescontext");
		ast_copy_string(aliasescontext, S_OR(val, ""), sizeof(aliasescontext));

		/* Attach voice message to mail message ? */
		if (!(val = ast_variable_retrieve(cfg, "general", "attach")))
			val = "yes";
		ast_set2_flag((&globalflags), ast_true(val), VM_ATTACH);

		if (!(val = ast_variable_retrieve(cfg, "general", "searchcontexts")))
			val = "no";
		ast_set2_flag((&globalflags), ast_true(val), VM_SEARCH);

		volgain = 0.0;
		if ((val = ast_variable_retrieve(cfg, "general", "volgain")))
			sscanf(val, "%30lf", &volgain);

#ifdef ODBC_STORAGE
		strcpy(odbc_database, "asterisk");
		if ((val = ast_variable_retrieve(cfg, "general", "odbcstorage"))) {
			ast_copy_string(odbc_database, val, sizeof(odbc_database));
		}
		strcpy(odbc_table, "voicemessages");
		if ((val = ast_variable_retrieve(cfg, "general", "odbctable"))) {
			ast_copy_string(odbc_table, val, sizeof(odbc_table));
		}
#endif
		/* Mail command */
		strcpy(mailcmd, SENDMAIL);
		if ((val = ast_variable_retrieve(cfg, "general", "mailcmd")))
			ast_copy_string(mailcmd, val, sizeof(mailcmd)); /* User setting */

		maxsilence = 0;
		if ((val = ast_variable_retrieve(cfg, "general", "maxsilence"))) {
			maxsilence = atoi(val);
			if (maxsilence > 0)
				maxsilence *= 1000;
		}

		if (!(val = ast_variable_retrieve(cfg, "general", "maxmsg"))) {
			maxmsg = MAXMSG;
		} else {
			maxmsg = atoi(val);
			if (maxmsg < 0) {
				ast_log(AST_LOG_WARNING, "Invalid number of messages per folder '%s'. Using default value %i\n", val, MAXMSG);
				maxmsg = MAXMSG;
			} else if (maxmsg > MAXMSGLIMIT) {
				ast_log(AST_LOG_WARNING, "Maximum number of messages per folder is %i. Cannot accept value '%s'\n", MAXMSGLIMIT, val);
				maxmsg = MAXMSGLIMIT;
			}
		}

		if (!(val = ast_variable_retrieve(cfg, "general", "backupdeleted"))) {
			maxdeletedmsg = 0;
		} else {
			if (sscanf(val, "%30d", &x) == 1)
				maxdeletedmsg = x;
			else if (ast_true(val))
				maxdeletedmsg = MAXMSG;
			else
				maxdeletedmsg = 0;

			if (maxdeletedmsg < 0) {
				ast_log(AST_LOG_WARNING, "Invalid number of deleted messages saved per mailbox '%s'. Using default value %i\n", val, MAXMSG);
				maxdeletedmsg = MAXMSG;
			} else if (maxdeletedmsg > MAXMSGLIMIT) {
				ast_log(AST_LOG_WARNING, "Maximum number of deleted messages saved per mailbox is %i. Cannot accept value '%s'\n", MAXMSGLIMIT, val);
				maxdeletedmsg = MAXMSGLIMIT;
			}
		}

		/* Load date format config for voicemail mail */
		if ((val = ast_variable_retrieve(cfg, "general", "emaildateformat"))) {
			ast_copy_string(emaildateformat, val, sizeof(emaildateformat));
		}

		/* Load date format config for voicemail pager mail */
		if ((val = ast_variable_retrieve(cfg, "general", "pagerdateformat"))) {
			ast_copy_string(pagerdateformat, val, sizeof(pagerdateformat));
		}

		/* External password changing command */
		if ((val = ast_variable_retrieve(cfg, "general", "externpass"))) {
			ast_copy_string(ext_pass_cmd, val, sizeof(ext_pass_cmd));
			pwdchange = PWDCHANGE_EXTERNAL;
		} else if ((val = ast_variable_retrieve(cfg, "general", "externpassnotify"))) {
			ast_copy_string(ext_pass_cmd, val, sizeof(ext_pass_cmd));
			pwdchange = PWDCHANGE_EXTERNAL | PWDCHANGE_INTERNAL;
		}

		/* External password validation command */
		if ((val = ast_variable_retrieve(cfg, "general", "externpasscheck"))) {
			ast_copy_string(ext_pass_check_cmd, val, sizeof(ext_pass_check_cmd));
			ast_debug(1, "found externpasscheck: %s\n", ext_pass_check_cmd);
		}

#ifdef IMAP_STORAGE
		/* IMAP server address */
		if ((val = ast_variable_retrieve(cfg, "general", "imapserver"))) {
			ast_copy_string(imapserver, val, sizeof(imapserver));
		} else {
			ast_copy_string(imapserver, "localhost", sizeof(imapserver));
		}
		/* IMAP server port */
		if ((val = ast_variable_retrieve(cfg, "general", "imapport"))) {
			ast_copy_string(imapport, val, sizeof(imapport));
		} else {
			ast_copy_string(imapport, "143", sizeof(imapport));
		}
		/* IMAP server flags */
		if ((val = ast_variable_retrieve(cfg, "general", "imapflags"))) {
			ast_copy_string(imapflags, val, sizeof(imapflags));
		}
		/* IMAP server master username */
		if ((val = ast_variable_retrieve(cfg, "general", "authuser"))) {
			ast_copy_string(authuser, val, sizeof(authuser));
		}
		/* IMAP server master password */
		if ((val = ast_variable_retrieve(cfg, "general", "authpassword"))) {
			ast_copy_string(authpassword, val, sizeof(authpassword));
		}
		/* Expunge on exit */
		if ((val = ast_variable_retrieve(cfg, "general", "expungeonhangup"))) {
			if (ast_false(val))
				expungeonhangup = 0;
			else
				expungeonhangup = 1;
		} else {
			expungeonhangup = 1;
		}
		/* IMAP voicemail folder */
		if ((val = ast_variable_retrieve(cfg, "general", "imapfolder"))) {
			ast_copy_string(imapfolder, val, sizeof(imapfolder));
		} else {
			ast_copy_string(imapfolder, "INBOX", sizeof(imapfolder));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "imapparentfolder"))) {
			ast_copy_string(imapparentfolder, val, sizeof(imapparentfolder));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "imapgreetings"))) {
			imapgreetings = ast_true(val);
		} else {
			imapgreetings = 0;
		}
		if ((val = ast_variable_retrieve(cfg, "general", "greetingfolder"))) {
			ast_copy_string(greetingfolder, val, sizeof(greetingfolder));
		} else if ((val = ast_variable_retrieve(cfg, "general", "greetingsfolder"))) {
			/* Also support greetingsfolder as documented in voicemail.conf.sample */
			ast_copy_string(greetingfolder, val, sizeof(greetingfolder));
		} else {
			ast_copy_string(greetingfolder, imapfolder, sizeof(greetingfolder));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "imap_poll_logout"))) {
			imap_poll_logout = ast_true(val);
		} else {
			imap_poll_logout = 0;
		}

		/* There is some very unorthodox casting done here. This is due
		 * to the way c-client handles the argument passed in. It expects a
		 * void pointer and casts the pointer directly to a long without
		 * first dereferencing it. */
		if ((val = ast_variable_retrieve(cfg, "general", "imapreadtimeout"))) {
			mail_parameters(NIL, SET_READTIMEOUT, (void *) (atol(val)));
		} else {
			mail_parameters(NIL, SET_READTIMEOUT, (void *) 60L);
		}

		if ((val = ast_variable_retrieve(cfg, "general", "imapwritetimeout"))) {
			mail_parameters(NIL, SET_WRITETIMEOUT, (void *) (atol(val)));
		} else {
			mail_parameters(NIL, SET_WRITETIMEOUT, (void *) 60L);
		}

		if ((val = ast_variable_retrieve(cfg, "general", "imapopentimeout"))) {
			mail_parameters(NIL, SET_OPENTIMEOUT, (void *) (atol(val)));
		} else {
			mail_parameters(NIL, SET_OPENTIMEOUT, (void *) 60L);
		}

		if ((val = ast_variable_retrieve(cfg, "general", "imapclosetimeout"))) {
			mail_parameters(NIL, SET_CLOSETIMEOUT, (void *) (atol(val)));
		} else {
			mail_parameters(NIL, SET_CLOSETIMEOUT, (void *) 60L);
		}

		/* Increment configuration version */
		imapversion++;
#endif
		/* External voicemail notify application */
		if ((val = ast_variable_retrieve(cfg, "general", "externnotify"))) {
			ast_copy_string(externnotify, val, sizeof(externnotify));
			ast_debug(1, "found externnotify: %s\n", externnotify);
		} else {
			externnotify[0] = '\0';
		}

		/* SMDI voicemail notification */
		if ((val = ast_variable_retrieve(cfg, "general", "smdienable")) && ast_true(val)) {
			ast_debug(1, "Enabled SMDI voicemail notification\n");
			if ((val = ast_variable_retrieve(cfg, "general", "smdiport"))) {
				smdi_iface = ast_smdi_interface_find(val);
			} else {
				ast_debug(1, "No SMDI interface set, trying default (/dev/ttyS0)\n");
				smdi_iface = ast_smdi_interface_find("/dev/ttyS0");
			}
			if (!smdi_iface) {
				ast_log(AST_LOG_ERROR, "No valid SMDI interface specfied, disabling SMDI voicemail notification\n");
			}
		}

		/* Silence treshold */
		silencethreshold = ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE);
		if ((val = ast_variable_retrieve(cfg, "general", "silencethreshold")))
			silencethreshold = atoi(val);

		if (!(val = ast_variable_retrieve(cfg, "general", "serveremail")))
			val = ASTERISK_USERNAME;
		ast_copy_string(serveremail, val, sizeof(serveremail));

		vmmaxsecs = 0;
		if ((val = ast_variable_retrieve(cfg, "general", "maxsecs"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				vmmaxsecs = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid max message time length\n");
			}
		} else if ((val = ast_variable_retrieve(cfg, "general", "maxmessage"))) {
			static int maxmessage_deprecate = 0;
			if (maxmessage_deprecate == 0) {
				maxmessage_deprecate = 1;
				ast_log(AST_LOG_WARNING, "Setting 'maxmessage' has been deprecated in favor of 'maxsecs'.\n");
			}
			if (sscanf(val, "%30d", &x) == 1) {
				vmmaxsecs = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid max message time length\n");
			}
		}

		vmminsecs = 0;
		if ((val = ast_variable_retrieve(cfg, "general", "minsecs"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				vmminsecs = x;
				if (maxsilence / 1000 >= vmminsecs) {
					ast_log(AST_LOG_WARNING, "maxsilence should be less than minsecs or you may get empty messages\n");
				}
			} else {
				ast_log(AST_LOG_WARNING, "Invalid min message time length\n");
			}
		} else if ((val = ast_variable_retrieve(cfg, "general", "minmessage"))) {
			static int maxmessage_deprecate = 0;
			if (maxmessage_deprecate == 0) {
				maxmessage_deprecate = 1;
				ast_log(AST_LOG_WARNING, "Setting 'minmessage' has been deprecated in favor of 'minsecs'.\n");
			}
			if (sscanf(val, "%30d", &x) == 1) {
				vmminsecs = x;
				if (maxsilence / 1000 >= vmminsecs) {
					ast_log(AST_LOG_WARNING, "maxsilence should be less than minmessage or you may get empty messages\n");
				}
			} else {
				ast_log(AST_LOG_WARNING, "Invalid min message time length\n");
			}
		}

		val = ast_variable_retrieve(cfg, "general", "format");
		if (!val) {
			val = "wav";
		} else {
			tmp = ast_strdupa(val);
			val = ast_format_str_reduce(tmp);
			if (!val) {
				ast_log(LOG_ERROR, "Error processing format string, defaulting to format 'wav'\n");
				val = "wav";
			}
		}
		ast_copy_string(vmfmts, val, sizeof(vmfmts));

		skipms = 3000;
		if ((val = ast_variable_retrieve(cfg, "general", "maxgreet"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				maxgreet = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid max message greeting length\n");
			}
		}

		if ((val = ast_variable_retrieve(cfg, "general", "skipms"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				skipms = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid skipms value\n");
			}
		}

		maxlogins = 3;
		if ((val = ast_variable_retrieve(cfg, "general", "maxlogins"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				maxlogins = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid max failed login attempts\n");
			}
		}

		minpassword = MINPASSWORD;
		if ((val = ast_variable_retrieve(cfg, "general", "minpassword"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				minpassword = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid minimum password length.  Default to %d\n", minpassword);
			}
		}

		/* Force new user to record name ? */
		if (!(val = ast_variable_retrieve(cfg, "general", "forcename")))
			val = "no";
		ast_set2_flag((&globalflags), ast_true(val), VM_FORCENAME);

		/* Force new user to record greetings ? */
		if (!(val = ast_variable_retrieve(cfg, "general", "forcegreetings")))
			val = "no";
		ast_set2_flag((&globalflags), ast_true(val), VM_FORCEGREET);

		if ((val = ast_variable_retrieve(cfg, "general", "cidinternalcontexts"))) {
			ast_debug(1, "VM_CID Internal context string: %s\n", val);
			stringp = ast_strdupa(val);
			for (x = 0 ; x < MAX_NUM_CID_CONTEXTS ; x++){
				if (!ast_strlen_zero(stringp)) {
					q = strsep(&stringp, ",");
					while ((*q == ' ')||(*q == '\t')) /* Eat white space between contexts */
						q++;
					ast_copy_string(cidinternalcontexts[x], q, sizeof(cidinternalcontexts[x]));
					ast_debug(1, "VM_CID Internal context %d: %s\n", x, cidinternalcontexts[x]);
				} else {
					cidinternalcontexts[x][0] = '\0';
				}
			}
		}
		if (!(val = ast_variable_retrieve(cfg, "general", "review"))){
			ast_debug(1, "VM Review Option disabled globally\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_REVIEW);

		/* Temporary greeting reminder */
		if (!(val = ast_variable_retrieve(cfg, "general", "tempgreetwarn"))) {
			ast_debug(1, "VM Temporary Greeting Reminder Option disabled globally\n");
			val = "no";
		} else {
			ast_debug(1, "VM Temporary Greeting Reminder Option enabled globally\n");
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_TEMPGREETWARN);
		if (!(val = ast_variable_retrieve(cfg, "general", "messagewrap"))){
			ast_debug(1, "VM next message wrap disabled globally\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_MESSAGEWRAP);

		if (!(val = ast_variable_retrieve(cfg, "general", "operator"))){
			ast_debug(1, "VM Operator break disabled globally\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_OPERATOR);

		if (!(val = ast_variable_retrieve(cfg, "general", "saycid"))) {
			ast_debug(1, "VM CID Info before msg disabled globally\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_SAYCID);

		if (!(val = ast_variable_retrieve(cfg, "general", "sendvoicemail"))){
			ast_debug(1, "Send Voicemail msg disabled globally\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_SVMAIL);

		if (!(val = ast_variable_retrieve(cfg, "general", "envelope"))) {
			ast_debug(1, "ENVELOPE before msg enabled globally\n");
			val = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_ENVELOPE);

		if (!(val = ast_variable_retrieve(cfg, "general", "moveheard"))) {
			ast_debug(1, "Move Heard enabled globally\n");
			val = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_MOVEHEARD);

		if (!(val = ast_variable_retrieve(cfg, "general", "forward_urgent_auto"))) {
			ast_debug(1, "Autoset of Urgent flag on forwarded Urgent messages disabled globally\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_FWDURGAUTO);

		if (!(val = ast_variable_retrieve(cfg, "general", "sayduration"))) {
			ast_debug(1, "Duration info before msg enabled globally\n");
			val = "yes";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_SAYDURATION);

		saydurationminfo = 2;
		if ((val = ast_variable_retrieve(cfg, "general", "saydurationm"))) {
			if (sscanf(val, "%30d", &x) == 1) {
				saydurationminfo = x;
			} else {
				ast_log(AST_LOG_WARNING, "Invalid min duration for say duration\n");
			}
		}

		if (!(val = ast_variable_retrieve(cfg, "general", "nextaftercmd"))) {
			ast_debug(1, "We are not going to skip to the next msg after save/delete\n");
			val = "no";
		}
		ast_set2_flag((&globalflags), ast_true(val), VM_SKIPAFTERCMD);

		if ((val = ast_variable_retrieve(cfg, "general", "dialout"))) {
			ast_copy_string(dialcontext, val, sizeof(dialcontext));
			ast_debug(1, "found dialout context: %s\n", dialcontext);
		} else {
			dialcontext[0] = '\0';
		}

		if ((val = ast_variable_retrieve(cfg, "general", "callback"))) {
			ast_copy_string(callcontext, val, sizeof(callcontext));
			ast_debug(1, "found callback context: %s\n", callcontext);
		} else {
			callcontext[0] = '\0';
		}

		if ((val = ast_variable_retrieve(cfg, "general", "exitcontext"))) {
			ast_copy_string(exitcontext, val, sizeof(exitcontext));
			ast_debug(1, "found operator context: %s\n", exitcontext);
		} else {
			exitcontext[0] = '\0';
		}

		/* load password sounds configuration */
		if ((val = ast_variable_retrieve(cfg, "general", "vm-login")))
			ast_copy_string(vm_login, val, sizeof(vm_login));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-newuser")))
			ast_copy_string(vm_newuser, val, sizeof(vm_newuser));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-password")))
			ast_copy_string(vm_password, val, sizeof(vm_password));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-newpassword")))
			ast_copy_string(vm_newpassword, val, sizeof(vm_newpassword));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-invalid-password")))
			ast_copy_string(vm_invalid_password, val, sizeof(vm_invalid_password));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-passchanged")))
			ast_copy_string(vm_passchanged, val, sizeof(vm_passchanged));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-reenterpassword")))
			ast_copy_string(vm_reenterpassword, val, sizeof(vm_reenterpassword));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-mismatch")))
			ast_copy_string(vm_mismatch, val, sizeof(vm_mismatch));
		if ((val = ast_variable_retrieve(cfg, "general", "vm-pls-try-again"))) {
			ast_copy_string(vm_pls_try_again, val, sizeof(vm_pls_try_again));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "vm-prepend-timeout"))) {
			ast_copy_string(vm_prepend_timeout, val, sizeof(vm_prepend_timeout));
		}
		/* load configurable audio prompts */
		if ((val = ast_variable_retrieve(cfg, "general", "listen-control-forward-key")) && is_valid_dtmf(val))
			ast_copy_string(listen_control_forward_key, val, sizeof(listen_control_forward_key));
		if ((val = ast_variable_retrieve(cfg, "general", "listen-control-reverse-key")) && is_valid_dtmf(val))
			ast_copy_string(listen_control_reverse_key, val, sizeof(listen_control_reverse_key));
		if ((val = ast_variable_retrieve(cfg, "general", "listen-control-pause-key")) && is_valid_dtmf(val))
			ast_copy_string(listen_control_pause_key, val, sizeof(listen_control_pause_key));
		if ((val = ast_variable_retrieve(cfg, "general", "listen-control-restart-key")) && is_valid_dtmf(val))
			ast_copy_string(listen_control_restart_key, val, sizeof(listen_control_restart_key));
		if ((val = ast_variable_retrieve(cfg, "general", "listen-control-stop-key")) && is_valid_dtmf(val))
			ast_copy_string(listen_control_stop_key, val, sizeof(listen_control_stop_key));

		if (!(val = ast_variable_retrieve(cfg, "general", "usedirectory")))
			val = "no";
		ast_set2_flag((&globalflags), ast_true(val), VM_DIRECTFORWARD);

		if (!(val = ast_variable_retrieve(cfg, "general", "passwordlocation"))) {
			val = "voicemail.conf";
		}
		if (!(strcmp(val, "spooldir"))) {
			passwordlocation = OPT_PWLOC_SPOOLDIR;
		} else {
			passwordlocation = OPT_PWLOC_VOICEMAILCONF;
		}

		poll_freq = DEFAULT_POLL_FREQ;
		if ((val = ast_variable_retrieve(cfg, "general", "pollfreq"))) {
			if (sscanf(val, "%30u", &poll_freq) != 1) {
				poll_freq = DEFAULT_POLL_FREQ;
				ast_log(AST_LOG_ERROR, "'%s' is not a valid value for the pollfreq option!\n", val);
			}
		}

		poll_mailboxes = 0;
		if ((val = ast_variable_retrieve(cfg, "general", "pollmailboxes")))
			poll_mailboxes = ast_true(val);

		memset(fromstring, 0, sizeof(fromstring));
		memset(pagerfromstring, 0, sizeof(pagerfromstring));
		strcpy(charset, "ISO-8859-1");
		if (emailbody) {
			ast_free(emailbody);
			emailbody = NULL;
		}
		if (emailsubject) {
			ast_free(emailsubject);
			emailsubject = NULL;
		}
		if (pagerbody) {
			ast_free(pagerbody);
			pagerbody = NULL;
		}
		if (pagersubject) {
			ast_free(pagersubject);
			pagersubject = NULL;
		}
		if ((val = ast_variable_retrieve(cfg, "general", "pbxskip")))
			ast_set2_flag((&globalflags), ast_true(val), VM_PBXSKIP);
		if ((val = ast_variable_retrieve(cfg, "general", "fromstring")))
			ast_copy_string(fromstring, val, sizeof(fromstring));
		if ((val = ast_variable_retrieve(cfg, "general", "pagerfromstring")))
			ast_copy_string(pagerfromstring, val, sizeof(pagerfromstring));
		if ((val = ast_variable_retrieve(cfg, "general", "charset")))
			ast_copy_string(charset, val, sizeof(charset));
		if ((val = ast_variable_retrieve(cfg, "general", "adsifdn"))) {
			sscanf(val, "%2x%2x%2x%2x", &tmpadsi[0], &tmpadsi[1], &tmpadsi[2], &tmpadsi[3]);
			for (x = 0; x < 4; x++) {
				memcpy(&adsifdn[x], &tmpadsi[x], 1);
			}
		}
		if ((val = ast_variable_retrieve(cfg, "general", "adsisec"))) {
			sscanf(val, "%2x%2x%2x%2x", &tmpadsi[0], &tmpadsi[1], &tmpadsi[2], &tmpadsi[3]);
			for (x = 0; x < 4; x++) {
				memcpy(&adsisec[x], &tmpadsi[x], 1);
			}
		}
		if ((val = ast_variable_retrieve(cfg, "general", "adsiver"))) {
			if (atoi(val)) {
				adsiver = atoi(val);
			}
		}
		if ((val = ast_variable_retrieve(cfg, "general", "tz"))) {
			ast_copy_string(zonetag, val, sizeof(zonetag));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "locale"))) {
			ast_copy_string(locale, val, sizeof(locale));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "emailsubject"))) {
			emailsubject = ast_strdup(substitute_escapes(val));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "emailbody"))) {
			emailbody = ast_strdup(substitute_escapes(val));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "pagersubject"))) {
			pagersubject = ast_strdup(substitute_escapes(val));
		}
		if ((val = ast_variable_retrieve(cfg, "general", "pagerbody"))) {
			pagerbody = ast_strdup(substitute_escapes(val));
		}

		tps_queue_high = AST_TASKPROCESSOR_HIGH_WATER_LEVEL;
		if ((val = ast_variable_retrieve(cfg, "general", "tps_queue_high"))) {
			if (sscanf(val, "%30ld", &tps_queue_high) != 1 || tps_queue_high <= 0) {
				ast_log(AST_LOG_WARNING, "Invalid the taskprocessor high water alert trigger level '%s'\n", val);
				tps_queue_high = AST_TASKPROCESSOR_HIGH_WATER_LEVEL;
			}
		}
		tps_queue_low = -1;
		if ((val = ast_variable_retrieve(cfg, "general", "tps_queue_low"))) {
			if (sscanf(val, "%30ld", &tps_queue_low) != 1 ||
				tps_queue_low < -1 || tps_queue_high < tps_queue_low) {
				ast_log(AST_LOG_WARNING, "Invalid the taskprocessor low water clear alert level '%s'\n", val);
				tps_queue_low = -1;
			}
		}
		if (ast_taskprocessor_alert_set_levels(mwi_subscription_tps, tps_queue_low, tps_queue_high)) {
			ast_log(AST_LOG_WARNING, "Failed to set alert levels for voicemail taskprocessor.\n");
		}

		/* load mailboxes from users.conf */
		if (ucfg) {
			for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
				if (!strcasecmp(cat, "general")) {
					continue;
				}
				if (!ast_true(ast_config_option(ucfg, cat, "hasvoicemail")))
					continue;
				if ((current = find_or_create(userscontext, cat))) {
					populate_defaults(current);
					apply_options_full(current, ast_variable_browse(ucfg, cat));
					ast_copy_string(current->context, userscontext, sizeof(current->context));
					if (!ast_strlen_zero(current->password) && current->passwordlocation == OPT_PWLOC_VOICEMAILCONF) {
						current->passwordlocation = OPT_PWLOC_USERSCONF;
					}

					switch (current->passwordlocation) {
					case OPT_PWLOC_SPOOLDIR:
						snprintf(secretfn, sizeof(secretfn), "%s%s/%s/secret.conf", VM_SPOOL_DIR, current->context, current->mailbox);
						read_password_from_file(secretfn, current->password, sizeof(current->password));
					}
				}
			}
		}

		/* load mailboxes from voicemail.conf */

		/*
		 * Aliases must be loaded before users or the aliases won't be notified
		 * if there's existing voicemail in the user mailbox.
		 */
		load_aliases(cfg);

		load_zonemessages(cfg);

		load_users(cfg);

		AST_LIST_UNLOCK(&users);

		if (poll_mailboxes && poll_thread == AST_PTHREADT_NULL)
			start_poll_thread();
		if (!poll_mailboxes && poll_thread != AST_PTHREADT_NULL)
			stop_poll_thread();;

		return 0;
	} else {
		AST_LIST_UNLOCK(&users);
		ast_log(AST_LOG_WARNING, "Failed to load configuration file.\n");
		return 0;
	}
}

static int sayname(struct ast_channel *chan, const char *mailbox, const char *context)
{
	int res = -1;
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s%s/%s/greet", VM_SPOOL_DIR, context, mailbox);
	ast_debug(2, "About to try retrieving name file %s\n", dir);
	RETRIEVE(dir, -1, mailbox, context);
	if (ast_fileexists(dir, NULL, NULL)) {
		res = ast_stream_and_wait(chan, dir, AST_DIGIT_ANY);
	}
	DISPOSE(dir, -1);
	return res;
}

/*!
 * \internal
 * \brief Play a recorded user name for the mailbox to the specified channel.
 *
 * \param chan Where to play the recorded name file.
 * \param mailbox_id The mailbox name.
 *
 * \retval 0 Name played without interruption
 * \retval dtmf ASCII value of the DTMF which interrupted playback.
 * \retval -1 Unable to locate mailbox or hangup occurred.
 */
static int vm_sayname(struct ast_channel *chan, const char *mailbox_id)
{
	char *context;
	char *mailbox;

	if (ast_strlen_zero(mailbox_id)
		|| separate_mailbox(ast_strdupa(mailbox_id), &mailbox, &context)) {
		return -1;
	}
	return sayname(chan, mailbox, context);
}

static void read_password_from_file(const char *secretfn, char *password, int passwordlen) {
	struct ast_config *pwconf;
	struct ast_flags config_flags = { 0 };

	pwconf = ast_config_load(secretfn, config_flags);
	if (valid_config(pwconf)) {
		const char *val = ast_variable_retrieve(pwconf, "general", "password");
		if (val) {
			ast_copy_string(password, val, passwordlen);
			ast_config_destroy(pwconf);
			return;
		}
		ast_config_destroy(pwconf);
	}
	ast_log(LOG_NOTICE, "Failed reading voicemail password from %s, using secret from config file\n", secretfn);
}

static int write_password_to_file(const char *secretfn, const char *password) {
	struct ast_config *conf;
	struct ast_category *cat;
	struct ast_variable *var;
	int res = -1;

	if (!(conf = ast_config_new())) {
		ast_log(LOG_ERROR, "Error creating new config structure\n");
		return res;
	}
	if (!(cat = ast_category_new("general", "", 1))) {
		ast_log(LOG_ERROR, "Error creating new category structure\n");
		ast_config_destroy(conf);
		return res;
	}
	if (!(var = ast_variable_new("password", password, ""))) {
		ast_log(LOG_ERROR, "Error creating new variable structure\n");
		ast_config_destroy(conf);
		ast_category_destroy(cat);
		return res;
	}
	ast_category_append(conf, cat);
	ast_variable_append(cat, var);
	if (!ast_config_text_file_save(secretfn, conf, "app_voicemail")) {
		res = 0;
	} else {
		ast_log(LOG_ERROR, "Error writing voicemail password to %s\n", secretfn);
	}

	ast_config_destroy(conf);
	return res;
}

static int vmsayname_exec(struct ast_channel *chan, const char *data)
{
	char *context;
	char *mailbox;
	int res;

	if (ast_strlen_zero(data)
		|| separate_mailbox(ast_strdupa(data), &mailbox, &context)) {
		ast_log(LOG_WARNING, "VMSayName requires argument mailbox@context\n");
		return -1;
	}

	if ((res = sayname(chan, mailbox, context)) < 0) {
		ast_debug(3, "Greeting not found for '%s@%s', falling back to mailbox number.\n", mailbox, context);
		res = ast_stream_and_wait(chan, "vm-extension", AST_DIGIT_ANY);
		if (!res) {
			res = ast_say_character_str(chan, mailbox, AST_DIGIT_ANY, ast_channel_language(chan), AST_SAY_CASE_NONE);
		}
	}

	return res;
}

#ifdef TEST_FRAMEWORK
static int fake_write(struct ast_channel *ast, struct ast_frame *frame)
{
	return 0;
}

static struct ast_frame *fake_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

AST_TEST_DEFINE(test_voicemail_vmsayname)
{
	char dir[PATH_MAX];
	char dir2[PATH_MAX];
	static const char TEST_CONTEXT[] = "very_long_unique_context_so_that_nobody_will_ever_have_the_same_one_configured_3141592653";
	static const char TEST_EXTENSION[] = "1234";

	struct ast_channel *test_channel1 = NULL;
	int res = -1;
	struct ast_format_cap *capabilities;

	static const struct ast_channel_tech fake_tech = {
		.write = fake_write,
		.read = fake_read,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "vmsayname_exec";
		info->category = "/apps/app_voicemail/";
		info->summary = "Vmsayname unit test";
		info->description =
			"This tests passing various parameters to vmsayname";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(test_channel1 = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL,
        NULL, NULL, 0, 0, "TestChannel1"))) {
		goto exit_vmsayname_test;
	}

	/* normally this is done in the channel driver */
	capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!capabilities) {
		goto exit_vmsayname_test;
	}
	ast_format_cap_append(capabilities, ast_format_gsm, 0);
	ast_channel_nativeformats_set(test_channel1, capabilities);
	ao2_ref(capabilities, -1);
	ast_channel_set_writeformat(test_channel1, ast_format_gsm);
	ast_channel_set_rawwriteformat(test_channel1, ast_format_gsm);
	ast_channel_set_readformat(test_channel1, ast_format_gsm);
	ast_channel_set_rawreadformat(test_channel1, ast_format_gsm);
	ast_channel_tech_set(test_channel1, &fake_tech);

	ast_channel_unlock(test_channel1);

	ast_test_status_update(test, "Test playing of extension when greeting is not available...\n");
	snprintf(dir, sizeof(dir), "%s@%s", TEST_EXTENSION, TEST_CONTEXT); /* not a dir, don't get confused */
	if (!(res = vmsayname_exec(test_channel1, dir))) {
		snprintf(dir, sizeof(dir), "%s%s/%s/greet", VM_SPOOL_DIR, TEST_CONTEXT, TEST_EXTENSION);
		if (ast_fileexists(dir, NULL, NULL)) {
			ast_test_status_update(test, "This should not happen, most likely means clean up from previous test failed\n");
			res = -1;
			goto exit_vmsayname_test;
		} else {
			/* no greeting already exists as expected, let's create one to fully test sayname */
			if ((res = create_dirpath(dir, sizeof(dir), TEST_CONTEXT, TEST_EXTENSION, ""))) {
				ast_log(AST_LOG_WARNING, "Failed to make test directory\n");
				goto exit_vmsayname_test;
			}
			snprintf(dir, sizeof(dir), "%s/sounds/beep.gsm", ast_config_AST_DATA_DIR);
			snprintf(dir2, sizeof(dir2), "%s%s/%s/greet.gsm", VM_SPOOL_DIR, TEST_CONTEXT, TEST_EXTENSION);
			/* we're not going to hear the sound anyway, just use a valid gsm audio file */
			if ((res = symlink(dir, dir2))) {
				ast_log(LOG_WARNING, "Symlink reported %s\n", strerror(errno));
				goto exit_vmsayname_test;
			}
			ast_test_status_update(test, "Test playing created mailbox greeting...\n");
			snprintf(dir, sizeof(dir), "%s@%s", TEST_EXTENSION, TEST_CONTEXT); /* not a dir, don't get confused */
			res = vmsayname_exec(test_channel1, dir);

			/* TODO: there may be a better way to do this */
			unlink(dir2);
			snprintf(dir2, sizeof(dir2), "%s%s/%s", VM_SPOOL_DIR, TEST_CONTEXT, TEST_EXTENSION);
			rmdir(dir2);
			snprintf(dir2, sizeof(dir2), "%s%s", VM_SPOOL_DIR, TEST_CONTEXT);
			rmdir(dir2);
		}
	}

exit_vmsayname_test:

	ast_hangup(test_channel1);

	return res ? AST_TEST_FAIL : AST_TEST_PASS;
}

struct test_files {
	char dir[256];
	char file[256];
	char txtfile[256];
};

AST_TEST_DEFINE(test_voicemail_msgcount)
{
	int i, j, res = AST_TEST_PASS, syserr;
	struct ast_vm_user *vmu;
	struct ast_vm_user svm;
	struct vm_state vms;
#ifdef IMAP_STORAGE
	struct ast_channel *chan = NULL;
#endif
	/* Using ast_alloca instead of just declaring tmp as an array is a workaround for a GCC 10 issue with -Wrestrict */
	struct test_files *tmp = ast_alloca(sizeof(struct test_files) * 3);
	char syscmd[256];
	const char origweasels[] = "tt-weasels";
	const char testcontext[] = "test";
	const char testmailbox[] = "00000000";
	const char testspec[] = "00000000@test";
	FILE *txt;
	int new, old, urgent;
	const char *folders[3] = { "Old", "Urgent", "INBOX" };
	const int folder2mbox[3] = { 1, 11, 0 };
	const int expected_results[3][12] = {
		/* hasvm-old, hasvm-urgent, hasvm-new, ic-old, ic-urgent, ic-new, ic2-old, ic2-urgent, ic2-new, mc-old, mc-urgent, mc-new */
		{          1,            0,         0,      1,         0,      0,       1,          0,       0,      1,         0,      0 },
		{          1,            1,         1,      1,         0,      1,       1,          1,       0,      1,         1,      1 },
		{          1,            1,         1,      1,         0,      2,       1,          1,       1,      1,         1,      2 },
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_voicemail_msgcount";
		info->category = "/apps/app_voicemail/";
		info->summary = "Test Voicemail status checks";
		info->description =
			"Verify that message counts are correct when retrieved through the public API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Make sure the original path was completely empty */
	snprintf(syscmd, sizeof(syscmd), "rm -rf \"%s%s/%s\"", VM_SPOOL_DIR, testcontext, testmailbox);
	if ((syserr = ast_safe_system(syscmd))) {
		ast_test_status_update(test, "Unable to clear test directory: %s\n",
			syserr > 0 ? strerror(syserr) : "unable to fork()");
		return AST_TEST_FAIL;
	}

#ifdef IMAP_STORAGE
	if (!(chan = ast_dummy_channel_alloc())) {
		ast_test_status_update(test, "Unable to create dummy channel\n");
		return AST_TEST_FAIL;
	}
#endif

	memset(&svm, 0, sizeof(svm));
	if (!(vmu = find_user(&svm, testcontext, testmailbox)) &&
		!(vmu = find_or_create(testcontext, testmailbox))) {
		ast_test_status_update(test, "Cannot create vmu structure\n");
		ast_unreplace_sigchld();
#ifdef IMAP_STORAGE
		chan = ast_channel_unref(chan);
#endif
		return AST_TEST_FAIL;
	}

	populate_defaults(vmu);
	memset(&vms, 0, sizeof(vms));

	/* Create temporary voicemail */
	for (i = 0; i < 3; i++) {
		create_dirpath(tmp[i].dir, sizeof(tmp[i].dir), testcontext, testmailbox, folders[i]);
		make_file(tmp[i].file, sizeof(tmp[i].file), tmp[i].dir, 0);
		snprintf(tmp[i].txtfile, sizeof(tmp[i].txtfile), "%s.txt", tmp[i].file);

		if (ast_fileexists(origweasels, "gsm", "en") > 0) {
			snprintf(syscmd, sizeof(syscmd), "cp \"%s/sounds/en/%s.gsm\" \"%s/%s/%s/%s/msg0000.gsm\"", ast_config_AST_DATA_DIR, origweasels,
				VM_SPOOL_DIR, testcontext, testmailbox, folders[i]);
			if ((syserr = ast_safe_system(syscmd))) {
				ast_test_status_update(test, "Unable to create test voicemail: %s\n",
					syserr > 0 ? strerror(syserr) : "unable to fork()");
				ast_unreplace_sigchld();
#ifdef IMAP_STORAGE
				chan = ast_channel_unref(chan);
#endif
				free_user(vmu);
				return AST_TEST_FAIL;
			}
		}

		if ((txt = fopen(tmp[i].txtfile, "w+"))) {
			fprintf(txt, "; just a stub\n[message]\nflag=%s\n", strcmp(folders[i], "Urgent") ? "" : "Urgent");
			fclose(txt);
		} else {
			ast_test_status_update(test, "Unable to write message file '%s'\n", tmp[i].txtfile);
			res = AST_TEST_FAIL;
			break;
		}
		open_mailbox(&vms, vmu, folder2mbox[i]);
		STORE(tmp[i].dir, testmailbox, testcontext, 0, chan, vmu, "gsm", 600, &vms, strcmp(folders[i], "Urgent") ? "" : "Urgent", NULL);

		/* hasvm-old, hasvm-urgent, hasvm-new, ic-old, ic-urgent, ic-new, ic2-old, ic2-urgent, ic2-new, mc-old, mc-urgent, mc-new */
		for (j = 0; j < 3; j++) {
			/* folder[2] is INBOX, __has_voicemail will default back to INBOX */
			if (ast_app_has_voicemail(testspec, (j==2 ? NULL : folders[j])) != expected_results[i][0 + j]) {
				ast_test_status_update(test, "has_voicemail(%s, %s) returned %d and we expected %d\n",
					testspec, folders[j], ast_app_has_voicemail(testspec, folders[j]), expected_results[i][0 + j]);
				res = AST_TEST_FAIL;
			}
		}

		new = old = urgent = 0;
		if (ast_app_inboxcount(testspec, &new, &old)) {
			ast_test_status_update(test, "inboxcount returned failure\n");
			res = AST_TEST_FAIL;
		} else if (old != expected_results[i][3 + 0] || new != expected_results[i][3 + 2]) {
			ast_test_status_update(test, "inboxcount(%s) returned old=%d (expected %d) and new=%d (expected %d)\n",
				testspec, old, expected_results[i][3 + 0], new, expected_results[i][3 + 2]);
			res = AST_TEST_FAIL;
		}

		new = old = urgent = 0;
		if (ast_app_inboxcount2(testspec, &urgent, &new, &old)) {
			ast_test_status_update(test, "inboxcount2 returned failure\n");
			res = AST_TEST_FAIL;
		} else if (old != expected_results[i][6 + 0] ||
				urgent != expected_results[i][6 + 1] ||
				   new != expected_results[i][6 + 2]    ) {
			ast_test_status_update(test, "inboxcount2(%s) returned old=%d (expected %d), urgent=%d (expected %d), and new=%d (expected %d)\n",
				testspec, old, expected_results[i][6 + 0], urgent, expected_results[i][6 + 1], new, expected_results[i][6 + 2]);
			res = AST_TEST_FAIL;
		}

		new = old = urgent = 0;
		for (j = 0; j < 3; j++) {
			if (ast_app_messagecount(testspec, folders[j]) != expected_results[i][9 + j]) {
				ast_test_status_update(test, "messagecount(%s, %s) returned %d and we expected %d\n",
					testspec, folders[j], ast_app_messagecount(testspec, folders[j]), expected_results[i][9 + j]);
				res = AST_TEST_FAIL;
			}
		}
	}

	for (i = 0; i < 3; i++) {
		/* This is necessary if the voicemails are stored on an ODBC/IMAP
		 * server, in which case, the rm below will not affect the
		 * voicemails. */
		DELETE(tmp[i].dir, 0, tmp[i].file, vmu);
		DISPOSE(tmp[i].dir, 0);
	}

	if (vms.deleted) {
		ast_free(vms.deleted);
	}
	if (vms.heard) {
		ast_free(vms.heard);
	}

#ifdef IMAP_STORAGE
	chan = ast_channel_unref(chan);
#endif

	/* And remove test directory */
	snprintf(syscmd, sizeof(syscmd), "rm -rf \"%s%s/%s\"", VM_SPOOL_DIR, testcontext, testmailbox);
	if ((syserr = ast_safe_system(syscmd))) {
		ast_test_status_update(test, "Unable to clear test directory: %s\n",
			syserr > 0 ? strerror(syserr) : "unable to fork()");
	}

	free_user(vmu);
	return res;
}

AST_TEST_DEFINE(test_voicemail_notify_endl)
{
	int res = AST_TEST_PASS;
	char testcontext[] = "test";
	char testmailbox[] = "00000000";
	char from[] = "test@example.net", cidnum[] = "1234", cidname[] = "Mark Spencer", format[] = "gsm";
	char attach[256], attach2[256];
	char buf[256] = ""; /* No line should actually be longer than 80 */
	struct ast_channel *chan = NULL;
	struct ast_vm_user *vmu, vmus = {
		.flags = 0,
	};
	FILE *file;
	struct {
		char *name;
		enum { INT, FLAGVAL, STATIC, STRPTR } type;
		void *location;
		union {
			int intval;
			char *strval;
		} u;
	} test_items[] = {
		{ "plain jane config", STATIC, vmus.password, .u.strval = "1234" }, /* No, this doesn't change this test any. */
		{ "emailsubject", STRPTR, vmus.emailsubject, .u.strval = "Oogly boogly\xf8koogly with what appears to be UTF-8" },
		{ "emailbody", STRPTR, vmus.emailbody, .u.strval = "This is a test\n\twith multiple\nlines\nwithin\n" },
		{ "serveremail", STATIC, vmus.serveremail, .u.strval = "\"\xf8Something\xe8that\xd8seems to have UTF-8 chars\" <test@example.net>" },
		{ "attachment flag", FLAGVAL, &vmus.flags, .u.intval = VM_ATTACH },
		{ "attach2", STRPTR, attach2, .u.strval = "" },
		{ "attach", STRPTR, attach, .u.strval = "" },
	};
	int which;

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_voicemail_notify_endl";
		info->category = "/apps/app_voicemail/";
		info->summary = "Test Voicemail notification end-of-line";
		info->description =
			"Verify that notification emails use a consistent end-of-line character";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	snprintf(attach, sizeof(attach), "%s/sounds/en/tt-weasels", ast_config_AST_DATA_DIR);
	snprintf(attach2, sizeof(attach2), "%s/sounds/en/tt-somethingwrong", ast_config_AST_DATA_DIR);

	if (!(vmu = find_user(&vmus, testcontext, testmailbox)) &&
		!(vmu = find_or_create(testcontext, testmailbox))) {
		ast_test_status_update(test, "Cannot create vmu structure\n");
		return AST_TEST_NOT_RUN;
	}

	if (vmu != &vmus && !(vmu = find_user(&vmus, testcontext, testmailbox))) {
		ast_test_status_update(test, "Cannot find vmu structure?!!\n");
		return AST_TEST_NOT_RUN;
	}

	populate_defaults(vmu);
	vmu->email = ast_strdup("test2@example.net");
#ifdef IMAP_STORAGE
	/* TODO When we set up the IMAP server test, we'll need to have credentials for the VMU structure added here */
#endif

	file = tmpfile();
	for (which = 0; which < ARRAY_LEN(test_items); which++) {
		/* Kill previous test, if any */
		rewind(file);
		if (ftruncate(fileno(file), 0)) {
			ast_test_status_update(test, "Cannot truncate test output file: %s\n", strerror(errno));
			res = AST_TEST_FAIL;
			break;
		}

		/* Make each change, in order, to the test mailbox */
		if (test_items[which].type == INT) {
			*((int *) test_items[which].location) = test_items[which].u.intval;
		} else if (test_items[which].type == FLAGVAL) {
			if (ast_test_flag(vmu, test_items[which].u.intval)) {
				ast_clear_flag(vmu, test_items[which].u.intval);
			} else {
				ast_set_flag(vmu, test_items[which].u.intval);
			}
		} else if (test_items[which].type == STATIC) {
			strcpy(test_items[which].location, test_items[which].u.strval);
		} else if (test_items[which].type == STRPTR) {
			test_items[which].location = test_items[which].u.strval;
		}

		make_email_file(file, from, vmu, 0, testcontext, testmailbox, "INBOX", cidnum, cidname, attach, attach2, format, 999, 1, chan, NULL, 0, NULL, NULL);
		rewind(file);
		while (fgets(buf, sizeof(buf), file)) {
			if (
			(strlen(buf) > 1 &&
#ifdef IMAP_STORAGE
			buf[strlen(buf) - 2] != '\r'
#else
			buf[strlen(buf) - 2] == '\r'
#endif
			)
			|| buf[strlen(buf) - 1] != '\n') {
				res = AST_TEST_FAIL;
			}
		}
	}
	fclose(file);
	free_user(vmu);
	return res;
}

AST_TEST_DEFINE(test_voicemail_load_config)
{
	int res = AST_TEST_PASS;
	struct ast_vm_user *vmu;
	struct ast_config *cfg;
	char config_filename[32] = "/tmp/voicemail.conf.XXXXXX";
	int fd;
	FILE *file;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_voicemail_load_config";
		info->category = "/apps/app_voicemail/";
		info->summary = "Test loading Voicemail config";
		info->description =
			"Verify that configuration is loaded consistently. "
			"This is to test regressions of ASTERISK-18838 where it was noticed that "
			"some options were loaded after the mailboxes were instantiated, causing "
			"those options not to be set correctly.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* build a config file by hand... */
	if ((fd = mkstemp(config_filename)) < 0) {
		return AST_TEST_FAIL;
	}
	if (!(file = fdopen(fd, "w"))) {
		close(fd);
		unlink(config_filename);
		return AST_TEST_FAIL;
	}
	fputs("[general]\ncallback=somecontext\nlocale=de_DE.UTF-8\ntz=european\n[test]", file);
	fputs("00000001 => 9999,Mr. Test,,,callback=othercontext|locale=nl_NL.UTF-8|tz=central\n", file);
	fputs("00000002 => 9999,Mrs. Test\n", file);
	fclose(file);

	if (!(cfg = ast_config_load(config_filename, config_flags)) || !valid_config(cfg)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	load_config_from_memory(1, cfg, NULL);
	ast_config_destroy(cfg);

#define CHECK(u, attr, value) else if (strcmp(u->attr, value)) { \
	ast_test_status_update(test, "mailbox %s should have %s '%s', but has '%s'\n", \
	u->mailbox, #attr, value, u->attr); res = AST_TEST_FAIL; break; }

	AST_LIST_LOCK(&users);
	AST_LIST_TRAVERSE(&users, vmu, list) {
		if (!strcmp(vmu->mailbox, "00000001")) {
			if (0); /* trick to get CHECK to work */
			CHECK(vmu, callback, "othercontext")
			CHECK(vmu, locale, "nl_NL.UTF-8")
			CHECK(vmu, zonetag, "central")
		} else if (!strcmp(vmu->mailbox, "00000002")) {
			if (0); /* trick to get CHECK to work */
			CHECK(vmu, callback, "somecontext")
			CHECK(vmu, locale, "de_DE.UTF-8")
			CHECK(vmu, zonetag, "european")
		}
	}
	AST_LIST_UNLOCK(&users);

#undef CHECK

	/* restore config */
	load_config(1); /* this might say "Failed to load configuration file." */

cleanup:
	unlink(config_filename);
	return res;
}

AST_TEST_DEFINE(test_voicemail_vm_info)
{
	struct ast_vm_user *vmu;
	struct ast_channel *chan = NULL;
	const char testcontext[] = "test";
	const char testmailbox[] = "00000000";
	const char vminfo_cmd[] = "VM_INFO";
	char vminfo_buf[256], vminfo_args[256];
	int res = AST_TEST_PASS;
	int test_ret = 0;
	int test_counter = 0;

	struct {
		char *vminfo_test_args;
		char *vminfo_expected;
		int vminfo_ret;
	} test_items[] = {
		{ "", "", -1 },				/* Missing argument */
		{ "00000000@test,badparam", "", -1 },	/* Wrong argument */
		{ "00000000@test", "", -1 },		/* Missing argument */
		{ "00000000@test,exists", "1", 0 },
		{ "11111111@test,exists", "0", 0 },	/* Invalid mailbox */
		{ "00000000@test,email", "vm-info-test@example.net", 0 },
		{ "11111111@test,email", "", 0 },	/* Invalid mailbox */
		{ "00000000@test,fullname", "Test Framework Mailbox", 0 },
		{ "00000000@test,pager", "vm-info-pager-test@example.net", 0 },
		{ "00000000@test,locale", "en_US", 0 },
		{ "00000000@test,tz", "central", 0 },
		{ "00000000@test,language", "en", 0 },
		{ "00000000@test,password", "9876", 0 },
	};

	switch (cmd) {
		case TEST_INIT:
			info->name = "test_voicemail_vm_info";
			info->category = "/apps/app_voicemail/";
			info->summary = "VM_INFO unit test";
			info->description =
				"This tests passing various parameters to VM_INFO";
			return AST_TEST_NOT_RUN;
		case TEST_EXECUTE:
			break;
	}

	if (!(chan = ast_dummy_channel_alloc())) {
		ast_test_status_update(test, "Unable to create dummy channel\n");
		return AST_TEST_FAIL;
	}

	if (!(vmu = find_user(NULL, testcontext, testmailbox)) &&
			!(vmu = find_or_create(testcontext, testmailbox))) {
		ast_test_status_update(test, "Cannot create vmu structure\n");
		chan = ast_channel_unref(chan);
		return AST_TEST_FAIL;
	}

	populate_defaults(vmu);

	vmu->email = ast_strdup("vm-info-test@example.net");
	ast_copy_string(vmu->fullname, "Test Framework Mailbox", sizeof(vmu->fullname));
	ast_copy_string(vmu->pager, "vm-info-pager-test@example.net", sizeof(vmu->pager));
	ast_copy_string(vmu->language, "en", sizeof(vmu->language));
	ast_copy_string(vmu->zonetag, "central", sizeof(vmu->zonetag));
	ast_copy_string(vmu->locale, "en_US", sizeof(vmu->zonetag));
	ast_copy_string(vmu->password, "9876", sizeof(vmu->password));

	for (test_counter = 0; test_counter < ARRAY_LEN(test_items); test_counter++) {
		ast_copy_string(vminfo_args, test_items[test_counter].vminfo_test_args, sizeof(vminfo_args));
		test_ret = acf_vm_info(chan, vminfo_cmd, vminfo_args, vminfo_buf, sizeof(vminfo_buf));
		if (strcmp(vminfo_buf, test_items[test_counter].vminfo_expected)) {
			ast_test_status_update(test, "VM_INFO respose was: '%s', but expected: '%s'\n", vminfo_buf, test_items[test_counter].vminfo_expected);
			res = AST_TEST_FAIL;
		}
		if (!(test_ret == test_items[test_counter].vminfo_ret)) {
			ast_test_status_update(test, "VM_INFO return code was: '%i', but expected '%i'\n", test_ret, test_items[test_counter].vminfo_ret);
			res = AST_TEST_FAIL;
		}
	}

	chan = ast_channel_unref(chan);
	free_user(vmu);
	return res;
}
#endif /* defined(TEST_FRAMEWORK) */

static const struct ast_vm_functions vm_table = {
	.module_version = VM_MODULE_VERSION,
	.module_name = AST_MODULE,

	.has_voicemail = has_voicemail,
	.inboxcount = inboxcount,
	.inboxcount2 = inboxcount2,
	.messagecount = messagecount,
	.copy_recording_to_vm = msg_create_from_file,
	.index_to_foldername = vm_index_to_foldername,
	.mailbox_snapshot_create = vm_mailbox_snapshot_create,
	.mailbox_snapshot_destroy = vm_mailbox_snapshot_destroy,
	.msg_move = vm_msg_move,
	.msg_remove = vm_msg_remove,
	.msg_forward = vm_msg_forward,
	.msg_play = vm_msg_play,
};

static const struct ast_vm_greeter_functions vm_greeter_table = {
	.module_version = VM_GREETER_MODULE_VERSION,
	.module_name = AST_MODULE,

	.sayname = vm_sayname,
};

static int reload(void)
{
	return load_config(1);
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(voicemail_app);
	res |= ast_unregister_application(voicemailmain_app);
	res |= ast_unregister_application(vmauthenticate_app);
	res |= ast_unregister_application(playmsg_app);
	res |= ast_unregister_application(sayname_app);
	res |= ast_custom_function_unregister(&vm_info_acf);
	res |= ast_manager_unregister("VoicemailUsersList");
	res |= ast_manager_unregister("VoicemailUserStatus");
	res |= ast_manager_unregister("VoicemailRefresh");
	res |= ast_manager_unregister("VoicemailBoxSummary");
	res |= ast_manager_unregister("VoicemailMove");
	res |= ast_manager_unregister("VoicemailRemove");
	res |= ast_manager_unregister("VoicemailForward");
#ifdef TEST_FRAMEWORK
	res |= AST_TEST_UNREGISTER(test_voicemail_vmsayname);
	res |= AST_TEST_UNREGISTER(test_voicemail_msgcount);
	res |= AST_TEST_UNREGISTER(test_voicemail_vmuser);
	res |= AST_TEST_UNREGISTER(test_voicemail_notify_endl);
	res |= AST_TEST_UNREGISTER(test_voicemail_load_config);
	res |= AST_TEST_UNREGISTER(test_voicemail_vm_info);
#endif
	ast_cli_unregister_multiple(cli_voicemail, ARRAY_LEN(cli_voicemail));
	ast_vm_unregister(vm_table.module_name);
	ast_vm_greeter_unregister(vm_greeter_table.module_name);
#ifdef TEST_FRAMEWORK
	ast_uninstall_vm_test_functions();
#endif
	ao2_ref(inprocess_container, -1);

	ao2_container_unregister("voicemail_alias_mailbox_mappings");
	ao2_cleanup(alias_mailbox_mappings);
	ao2_container_unregister("voicemail_mailbox_alias_mappings");
	ao2_cleanup(mailbox_alias_mappings);

	if (poll_thread != AST_PTHREADT_NULL)
		stop_poll_thread();

	mwi_subscription_tps = ast_taskprocessor_unreference(mwi_subscription_tps);
	ast_unload_realtime("voicemail");
	ast_unload_realtime("voicemail_data");

#ifdef IMAP_STORAGE
	ast_mwi_state_callback_all(imap_close_subscribed_mailbox, NULL);
#endif
	free_vm_users();
	free_vm_zones();
	return res;
}

static void print_mappings(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct alias_mailbox_mapping *mapping = v_obj;

	if (!mapping) {
		return;
	}
	prnt(where, "Alias: %s Mailbox: %s", mapping->alias, mapping->mailbox);
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS.
 *
 * If a dependency, allocation or environment variable fails tests, return AST_MODULE_LOAD_FAILURE.
 *
 * If the module can't load the configuration file, can't register as a provider or
 * has another issue not fatal to Asterisk itself, return AST_MODULE_LOAD_DECLINE.
 *
 * On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	int res;
	my_umask = umask(0);
	umask(my_umask);

	inprocess_container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 573,
		inprocess_hash_fn, NULL, inprocess_cmp_fn);
	if (!inprocess_container) {
		return AST_MODULE_LOAD_DECLINE;
	}

	alias_mailbox_mappings = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, MAPPING_BUCKETS,
		alias_mailbox_mapping_hash_fn, NULL, alias_mailbox_mapping_cmp_fn);
	if (!alias_mailbox_mappings) {
		ast_log(LOG_ERROR, "Unable to create alias_mailbox_mappings container\n");
		ao2_cleanup(inprocess_container);
		return AST_MODULE_LOAD_DECLINE;
	}
	res = ao2_container_register("voicemail_alias_mailbox_mappings", alias_mailbox_mappings, print_mappings);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register alias_mailbox_mappings container\n");
		ao2_cleanup(inprocess_container);
		ao2_cleanup(alias_mailbox_mappings);
		return AST_MODULE_LOAD_DECLINE;
	}

	mailbox_alias_mappings = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, MAPPING_BUCKETS,
		mailbox_alias_mapping_hash_fn, NULL, mailbox_alias_mapping_cmp_fn);
	if (!mailbox_alias_mappings) {
		ast_log(LOG_ERROR, "Unable to create mailbox_alias_mappings container\n");
		ao2_cleanup(inprocess_container);
		ao2_container_unregister("voicemail_alias_mailbox_mappings");
		ao2_cleanup(alias_mailbox_mappings);
		return AST_MODULE_LOAD_DECLINE;
	}
	res = ao2_container_register("voicemail_mailbox_alias_mappings", mailbox_alias_mappings, print_mappings);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register mailbox_alias_mappings container\n");
		ao2_cleanup(inprocess_container);
		ao2_container_unregister("voicemail_alias_mailbox_mappings");
		ao2_cleanup(alias_mailbox_mappings);
		ao2_cleanup(mailbox_alias_mappings);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* compute the location of the voicemail spool directory */
	snprintf(VM_SPOOL_DIR, sizeof(VM_SPOOL_DIR), "%s/voicemail/", ast_config_AST_SPOOL_DIR);

	if (!(mwi_subscription_tps = ast_taskprocessor_get("app_voicemail", 0))) {
		ast_log(AST_LOG_WARNING, "failed to reference mwi subscription taskprocessor.  MWI will not work\n");
	}

	if ((res = load_config(0))) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = ast_register_application_xml(voicemail_app, vm_exec);
	res |= ast_register_application_xml(voicemailmain_app, vm_execmain);
	res |= ast_register_application_xml(vmauthenticate_app, vmauthenticate);
	res |= ast_register_application_xml(playmsg_app, vm_playmsgexec);
	res |= ast_register_application_xml(sayname_app, vmsayname_exec);
	res |= ast_custom_function_register(&vm_info_acf);
	res |= ast_manager_register_xml("VoicemailUsersList", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, manager_list_voicemail_users);
	res |= ast_manager_register_xml("VoicemailUserStatus", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, manager_status_voicemail_user);
	res |= ast_manager_register_xml("VoicemailRefresh", EVENT_FLAG_USER, manager_voicemail_refresh);
	res |= ast_manager_register_xml("VoicemailBoxSummary", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, manager_get_mailbox_summary);
	res |= ast_manager_register_xml("VoicemailMove", EVENT_FLAG_USER, manager_voicemail_move);
	res |= ast_manager_register_xml("VoicemailRemove", EVENT_FLAG_USER, manager_voicemail_remove);
	res |= ast_manager_register_xml("VoicemailForward", EVENT_FLAG_USER, manager_voicemail_forward);
#ifdef TEST_FRAMEWORK
	res |= AST_TEST_REGISTER(test_voicemail_vmsayname);
	res |= AST_TEST_REGISTER(test_voicemail_msgcount);
	res |= AST_TEST_REGISTER(test_voicemail_vmuser);
	res |= AST_TEST_REGISTER(test_voicemail_notify_endl);
	res |= AST_TEST_REGISTER(test_voicemail_load_config);
	res |= AST_TEST_REGISTER(test_voicemail_vm_info);
#endif

	if (res) {
		ast_log(LOG_ERROR, "Failure registering applications, functions or tests\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* ast_vm_register may return DECLINE if another module registered for vm */
	res = ast_vm_register(&vm_table);
	if (res) {
		ast_log(LOG_ERROR, "Failure registering as a voicemail provider\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* ast_vm_greeter_register may return DECLINE if another module registered as a greeter */
	res = ast_vm_greeter_register(&vm_greeter_table);
	if (res) {
		ast_log(LOG_ERROR, "Failure registering as a greeter provider\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cli_voicemail, ARRAY_LEN(cli_voicemail));

#ifdef TEST_FRAMEWORK
	ast_install_vm_test_functions(vm_test_create_user, vm_test_destroy_user);
#endif

	ast_realtime_require_field("voicemail", "uniqueid", RQ_UINTEGER3, 11, "password", RQ_CHAR, 10, SENTINEL);
	ast_realtime_require_field("voicemail_data", "filename", RQ_CHAR, 30, "duration", RQ_UINTEGER3, 5, SENTINEL);

	return AST_MODULE_LOAD_SUCCESS;
}

static int dialout(struct ast_channel *chan, struct ast_vm_user *vmu, char *num, char *outgoing_context)
{
	int cmd = 0;
	char destination[80] = "";
	int retries = 0;

	if (!num) {
		ast_verb(3, "Destination number will be entered manually\n");
		while (retries < 3 && cmd != 't') {
			destination[1] = '\0';
			destination[0] = cmd = ast_play_and_wait(chan, "vm-enter-num-to-call");
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
					ast_verb(3, "User hit '*' to cancel outgoing call\n");
					return 0;
				}
				if ((cmd = ast_readstring(chan, destination + strlen(destination), sizeof(destination) - 1, 6000, 10000, "#")) < 0)
					retries++;
				else
					cmd = 't';
			}
			ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
				isprint(cmd) ? cmd : '?', isprint(cmd) ? cmd : '?');
		}
		if (retries >= 3) {
			return 0;
		}

	} else {
		ast_verb(3, "Destination number is CID number '%s'\n", num);
		ast_copy_string(destination, num, sizeof(destination));
	}

	if (!ast_strlen_zero(destination)) {
		if (destination[strlen(destination) -1 ] == '*')
			return 0;
		ast_verb(3, "Placing outgoing call to extension '%s' in context '%s' from context '%s'\n", destination, outgoing_context, ast_channel_context(chan));
		ast_channel_exten_set(chan, destination);
		ast_channel_context_set(chan, outgoing_context);
		ast_channel_priority_set(chan, 0);
		return 9;
	}
	return 0;
}

/*!
 * \brief The advanced options within a message.
 * \param chan
 * \param vmu
 * \param vms
 * \param msg
 * \param option
 * \param record_gain
 *
 * Provides handling for the play message envelope, call the person back, or reply to message.
 *
 * \return zero on success, -1 on error.
 */
static int advanced_options(struct ast_channel *chan, struct ast_vm_user *vmu, struct vm_state *vms, int msg, int option, signed char record_gain)
{
	int res = 0;
	char filename[PATH_MAX];
	struct ast_config *msg_cfg = NULL;
	const char *origtime, *context;
	char *name, *num;
	int retries = 0;
	char *cid;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE, };

	vms->starting = 0;

	make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);

	/* Retrieve info from VM attribute file */
	snprintf(filename, sizeof(filename), "%s.txt", vms->fn);
	RETRIEVE(vms->curdir, vms->curmsg, vmu->mailbox, vmu->context);
	msg_cfg = ast_config_load(filename, config_flags);
	DISPOSE(vms->curdir, vms->curmsg);
	if (!valid_config(msg_cfg)) {
		ast_log(AST_LOG_WARNING, "No message attribute file?!! (%s)\n", filename);
		return 0;
	}

	if (!(origtime = ast_variable_retrieve(msg_cfg, "message", "origtime"))) {
		ast_config_destroy(msg_cfg);
		return 0;
	}

	cid = ast_strdupa(ast_variable_retrieve(msg_cfg, "message", "callerid"));

	context = ast_variable_retrieve(msg_cfg, "message", "context");
	switch (option) {
	case 3: /* Play message envelope */
		if (!res) {
			res = play_message_datetime(chan, vmu, origtime, filename);
		}
		if (!res) {
			res = play_message_callerid(chan, vms, cid, context, 0, 1);
		}

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
					ast_verb(3, "Caller can not specify callback number - no dialout context available\n");
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
					ast_verb(3, "Confirm CID number '%s' is number to use for callback\n", num);
					res = ast_play_and_wait(chan, "vm-num-i-have");
					if (!res)
						res = play_message_callerid(chan, vms, num, vmu->context, 1, 1);
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
				if (!res) {
					res = ast_play_and_wait(chan, "vm-star-cancel");
				}
				if (!res) {
					res = ast_waitfordigit(chan, 6000);
				}
				if (!res) {
					retries++;
					if (retries > 3) {
						res = 't';
					}
				}
				ast_test_suite_event_notify("USERPRESS", "Message: User pressed %c\r\nDTMF: %c",
					isprint(res) ? res : '?', isprint(res) ? res : '?');
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
			ast_verb(3, "No CID number available, no reply sent\n");
			if (!res)
				res = ast_play_and_wait(chan, "vm-nonumber");
			ast_config_destroy(msg_cfg);
			return res;
		} else {
			struct ast_vm_user vmu2, *vmu3;
			memset(&vmu2, 0, sizeof(vmu2));
			vmu3 = find_user(&vmu2, vmu->context, num);
			if (vmu3) {
				struct leave_vm_options leave_options;
				char mailbox[AST_MAX_EXTENSION * 2 + 2];
				snprintf(mailbox, sizeof(mailbox), "%s@%s", num, vmu->context);

				ast_verb(3, "Leaving voicemail for '%s' in context '%s'\n", num, vmu->context);

				memset(&leave_options, 0, sizeof(leave_options));
				leave_options.record_gain = record_gain;
				leave_options.beeptone = "beep";
				res = leave_voicemail(chan, mailbox, &leave_options);
				if (!res)
					res = 't';
				ast_config_destroy(msg_cfg);
				free_user(vmu3);
				return res;
			} else {
				/* Sender has no mailbox, can't reply */
				ast_verb(3, "No mailbox number '%s' in context '%s', no reply sent\n", num, vmu->context);
				ast_play_and_wait(chan, "vm-nobox");
				res = 't';
				ast_config_destroy(msg_cfg);
				return res;
			}
		}
		res = 0;

		break;
	}

	ast_config_destroy(msg_cfg);

#ifndef IMAP_STORAGE
	if (!res) {
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, msg);
		vms->heard[msg] = 1;
		res = wait_file(chan, vms, vms->fn);
	}
#endif
	return res;
}

static int play_record_review(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt,
			int outsidecaller, struct ast_vm_user *vmu, int *duration, int *sound_duration, const char *unlockdir,
			signed char record_gain, struct vm_state *vms, char *flag, const char *msg_id, int forwardintro)
{
	/* Record message & let caller review or re-record it, or set options if applicable */
	int res = 0;
	int cmd = 0;
	int max_attempts = 3;
	int attempts = 0;
	int recorded = 0;
	int msg_exists = 0;
	signed char zero_gain = 0;
	char tempfile[PATH_MAX];
	char *acceptdtmf = "#";
	char *canceldtmf = "";
	int canceleddtmf = 0;

	/* Note that urgent and private are for flagging messages as such in the future */

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(AST_LOG_WARNING, "Error play_record_review called without duration pointer\n");
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
			if (!msg_exists) {
				/* In this case, 1 is to record a message */
				cmd = '3';
				break;
			} else {
				/* Otherwise 1 is to save the existing message */
				ast_verb(3, "Saving message as is\n");
				if (!outsidecaller)
					ast_filerename(tempfile, recordfile, NULL);
				if (!forwardintro) {
					ast_stream_and_wait(chan, "vm-msgsaved", "");
				}
				if (!outsidecaller) {
					/* Saves to IMAP server only if imapgreeting=yes */
					STORE(recordfile, vmu->mailbox, vmu->context, -1, chan, vmu, fmt, *duration, vms, flag, msg_id);
					DISPOSE(recordfile, -1);
				}
				cmd = 't';
				return res;
			}
		case '2':
			/* Review */
			ast_verb(3, "Reviewing the message\n");
			cmd = ast_stream_and_wait(chan, tempfile, AST_DIGIT_ANY);
			break;
		case '3':
			msg_exists = 0;
			/* Record */
			if (recorded == 1)
				ast_verb(3, "Re-recording the message\n");
			else
				ast_verb(3, "Recording the message\n");

			if (recorded && outsidecaller) {
				if (forwardintro) {
					cmd = ast_play_and_wait(chan, "vm-record-prepend");
				} else {
					cmd = ast_play_and_wait(chan, INTRO);
				}
				cmd = ast_play_and_wait(chan, "beep");
			}
			if (cmd == -1) {
				/* User has hung up, no options to give */
				ast_debug(1, "User hung up before message could be rerecorded\n");
				ast_filedelete(tempfile, NULL);
				return cmd;
			}
			recorded = 1;
			/* After an attempt has been made to record message, we have to take care of INTRO and beep for incoming messages, but not for greetings */
			if (record_gain)
				ast_channel_setoption(chan, AST_OPTION_RXGAIN, &record_gain, sizeof(record_gain), 0);
			if (ast_test_flag(vmu, VM_OPERATOR))
				canceldtmf = "0";
			cmd = ast_play_and_record_full(chan, playfile, tempfile, maxtime, fmt, duration, sound_duration, 0, silencethreshold, maxsilence, unlockdir, acceptdtmf, canceldtmf, 0, AST_RECORD_IF_EXISTS_OVERWRITE);
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
#if 0
			} else if (vmu->review && sound_duration && (*sound_duration < 5)) {
				/* Message is too short */
				ast_verb(3, "Message too short\n");
				cmd = ast_play_and_wait(chan, "vm-tooshort");
				cmd = ast_filedelete(tempfile, NULL);
				break;
			} else if (vmu->review && (cmd == 2 && sound_duration && *sound_duration < (maxsilence + 3))) {
				/* Message is all silence */
				ast_verb(3, "Nothing recorded\n");
				cmd = ast_filedelete(tempfile, NULL);
				cmd = ast_play_and_wait(chan, "vm-nothingrecorded");
				if (!cmd)
					cmd = ast_play_and_wait(chan, "vm-speakup");
				break;
#endif
			} else {
				/* If all is well, a message exists */
				msg_exists = 1;
				cmd = 0;
			}
			break;
		case '4':
			if (outsidecaller) {  /* only mark vm messages */
				/* Mark Urgent */
				if ((flag && ast_strlen_zero(flag)) || (!ast_strlen_zero(flag) && strcmp(flag, "Urgent"))) {
					ast_verb(3, "marking message as Urgent\n");
					res = ast_play_and_wait(chan, "vm-marked-urgent");
					strcpy(flag, "Urgent");
				} else if (flag) {
					ast_verb(3, "UNmarking message as Urgent\n");
					res = ast_play_and_wait(chan, "vm-marked-nonurgent");
					strcpy(flag, "");
				} else {
					ast_play_and_wait(chan, "vm-sorry");
				}
				cmd = 0;
			} else {
				cmd = ast_play_and_wait(chan, "vm-sorry");
			}
			break;
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
			if (msg_exists || recorded) {
				cmd = ast_play_and_wait(chan, "vm-saveoper");
				if (!cmd)
					cmd = ast_waitfordigit(chan, 3000);
				if (cmd == '1') {
					ast_filerename(tempfile, recordfile, NULL);
					ast_play_and_wait(chan, "vm-msgsaved");
					cmd = '0';
				} else if (cmd == '4') {
					if (flag) {
						ast_play_and_wait(chan, "vm-marked-urgent");
						strcpy(flag, "Urgent");
					}
					ast_play_and_wait(chan, "vm-msgsaved");
					cmd = '0';
				} else {
					ast_play_and_wait(chan, "vm-deleted");
					DELETE(tempfile, -1, tempfile, vmu);
					DISPOSE(tempfile, -1);
					cmd = '0';
				}
			}
			return cmd;
		default:
			/* If the caller is an outside caller and the review option is enabled or it's forward intro
			   allow them to review the message, but let the owner of the box review
			   their OGM's */
			if (outsidecaller && !ast_test_flag(vmu, VM_REVIEW) && !forwardintro)
				return cmd;
			if (msg_exists) {
				cmd = ast_play_and_wait(chan, "vm-review");
				if (!cmd && outsidecaller) {
					if ((flag && ast_strlen_zero(flag)) || (!ast_strlen_zero(flag) && strcmp(flag, "Urgent"))) {
						cmd = ast_play_and_wait(chan, "vm-review-urgent");
					} else if (flag) {
						cmd = ast_play_and_wait(chan, "vm-review-nonurgent");
					}
				}
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

static struct ast_vm_msg_snapshot *vm_msg_snapshot_alloc(void)
{
	struct ast_vm_msg_snapshot *msg_snapshot;

	if (!(msg_snapshot = ast_calloc(1, sizeof(*msg_snapshot)))) {
		return NULL;
	}

	if (ast_string_field_init(msg_snapshot, 512)) {
		ast_free(msg_snapshot);
		return NULL;
	}

	return msg_snapshot;
}

static struct ast_vm_msg_snapshot *vm_msg_snapshot_destroy(struct ast_vm_msg_snapshot *msg_snapshot)
{
	ast_string_field_free_memory(msg_snapshot);
	ast_free(msg_snapshot);

	return NULL;
}

#ifdef TEST_FRAMEWORK

static int vm_test_destroy_user(const char *context, const char *mailbox)
{
	struct ast_vm_user *vmu;

	AST_LIST_LOCK(&users);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&users, vmu, list) {
		if (!strcmp(context, vmu->context)
			&& !strcmp(mailbox, vmu->mailbox)) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(vmu);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&users);
	return 0;
}

static int vm_test_create_user(const char *context, const char *mailbox)
{
	struct ast_vm_user *vmu;

	if (!(vmu = find_or_create(context, mailbox))) {
		return -1;
	}
	populate_defaults(vmu);
	return 0;
}

#endif

/*!
 * \brief Create and store off all the msgs in an open mailbox
 *
 * \note TODO XXX This function should work properly for all
 *       voicemail storage options, but is far more expensive for
 *       ODBC at the moment.  This is because the RETRIEVE macro
 *       not only pulls out the message's meta data file from the
 *       database, but also the actual audio for each message, temporarily
 *       writing it to the file system.  This is an area that needs
 *       to be made more efficient.
 */
static int vm_msg_snapshot_create(struct ast_vm_user *vmu,
	struct vm_state *vms,
	struct ast_vm_mailbox_snapshot *mailbox_snapshot,
	int snapshot_index,
	int mailbox_index,
	int descending,
	enum ast_vm_snapshot_sort_val sort_val)
{
	struct ast_vm_msg_snapshot *msg_snapshot;
	struct ast_vm_msg_snapshot *msg_snapshot_tmp;
	struct ast_config *msg_cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
	char filename[PATH_MAX];
	const char *value;

	for (vms->curmsg = 0; vms->curmsg <= vms->lastmsg; vms->curmsg++) {
		int inserted = 0;
		/* Find the msg */
		make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
		snprintf(filename, sizeof(filename), "%s.txt", vms->fn);
		RETRIEVE(vms->curdir, vms->curmsg, vmu->mailbox, vmu->context);
		msg_cfg = ast_config_load(filename, config_flags);
		if (!msg_cfg || msg_cfg == CONFIG_STATUS_FILEINVALID) {
			DISPOSE(vms->curdir, vms->curmsg);
			continue;
		}

		/* Create the snapshot object */
		if (!(msg_snapshot = vm_msg_snapshot_alloc())) {
			ast_config_destroy(msg_cfg);
			return -1;
		}

		/* Fill in the snapshot object */
		if ((value = ast_variable_retrieve(msg_cfg, "message", "msg_id"))) {
			ast_string_field_set(msg_snapshot, msg_id, value);
		} else {
			/* Message snapshots *really* should have a
			 * message ID. Add one to the message config
			 * if it does not already exist
			 */
			char id[MSG_ID_LEN];
			if (!(add_message_id(msg_cfg, vms->curdir, vms->curmsg,
							filename, id, sizeof(id), vmu, mailbox_index))) {
				ast_string_field_set(msg_snapshot, msg_id, id);
			} else {
				ast_log(LOG_WARNING, "Unable to create a message ID for message %s/%d\n", vms->curdir, vms->curmsg);
			}
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "callerid"))) {
			ast_string_field_set(msg_snapshot, callerid, value);
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "callerchan"))) {
			ast_string_field_set(msg_snapshot, callerchan, value);
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "exten"))) {
			ast_string_field_set(msg_snapshot, exten, value);
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "origdate"))) {
			ast_string_field_set(msg_snapshot, origdate, value);
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "origtime"))) {
			ast_string_field_set(msg_snapshot, origtime, value);
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "duration"))) {
			ast_string_field_set(msg_snapshot, duration, value);
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "flag"))) {
			ast_string_field_set(msg_snapshot, flag, value);
		}
		msg_snapshot->msg_number = vms->curmsg;
		ast_string_field_set(msg_snapshot, folder_name, mailbox_folders[mailbox_index]);

		/* store msg snapshot in mailbox snapshot */
		switch (sort_val) {
		default:
		case AST_VM_SNAPSHOT_SORT_BY_ID:
			if (descending) {
				AST_LIST_INSERT_HEAD(&mailbox_snapshot->snapshots[snapshot_index], msg_snapshot, msg);
			} else {
				AST_LIST_INSERT_TAIL(&mailbox_snapshot->snapshots[snapshot_index], msg_snapshot, msg);
			}
			inserted = 1;
			break;
		case AST_VM_SNAPSHOT_SORT_BY_TIME:
			AST_LIST_TRAVERSE_SAFE_BEGIN(&mailbox_snapshot->snapshots[snapshot_index], msg_snapshot_tmp, msg) {
				int val = strcmp(msg_snapshot->origtime, msg_snapshot_tmp->origtime);
				if (descending && val >= 0) {
					AST_LIST_INSERT_BEFORE_CURRENT(msg_snapshot, msg);
					inserted = 1;
					break;
				} else if (!descending && val <= 0) {
					AST_LIST_INSERT_BEFORE_CURRENT(msg_snapshot, msg);
					inserted = 1;
					break;
				}
			}
			AST_LIST_TRAVERSE_SAFE_END;
			break;
		}

		if (!inserted) {
			AST_LIST_INSERT_TAIL(&mailbox_snapshot->snapshots[snapshot_index], msg_snapshot, msg);
		}

		mailbox_snapshot->total_msg_num++;

		/* cleanup configs and msg */
		ast_config_destroy(msg_cfg);
		DISPOSE(vms->curdir, vms->curmsg);
	}

	return 0;
}

static struct ast_vm_mailbox_snapshot *vm_mailbox_snapshot_create(const char *mailbox,
	const char *context,
	const char *folder,
	int descending,
	enum ast_vm_snapshot_sort_val sort_val,
	int combine_INBOX_and_OLD)
{
	struct ast_vm_mailbox_snapshot *mailbox_snapshot;
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	int res;
	int i;
	int this_index_only = -1;
	int open = 0;
	int inbox_index = get_folder_by_name("INBOX");
	int old_index = get_folder_by_name("Old");
	int urgent_index = get_folder_by_name("Urgent");

	if (ast_strlen_zero(mailbox)) {
		ast_log(LOG_WARNING, "Cannot create a mailbox snapshot since no mailbox was specified\n");
		return NULL;
	}

	memset(&vmus, 0, sizeof(vmus));

	if (!(ast_strlen_zero(folder))) {
		/* find the folder index */
		for (i = 0; i < ARRAY_LEN(mailbox_folders); i++) {
			if (!strcasecmp(mailbox_folders[i], folder)) {
				this_index_only = i;
				break;
			}
		}
		if (this_index_only == -1) {
			/* Folder was specified and it did not match any folder in our list */
			return NULL;
		}
	}

	if (!(vmu = find_user(&vmus, context, mailbox))) {
		ast_log(AST_LOG_WARNING, "Failed to create mailbox snapshot for unknown voicemail user %s@%s\n", mailbox, context);
		return NULL;
	}

	if (!(mailbox_snapshot = ast_calloc(1, sizeof(*mailbox_snapshot)))) {
		ast_log(AST_LOG_ERROR, "Failed to allocate memory for mailbox snapshot\n");
		free_user(vmu);
		return NULL;
	}

	if (!(mailbox_snapshot->snapshots = ast_calloc(ARRAY_LEN(mailbox_folders), sizeof(*mailbox_snapshot->snapshots)))) {
		ast_free(mailbox_snapshot);
		free_user(vmu);
		return NULL;
	}

	mailbox_snapshot->folders = ARRAY_LEN(mailbox_folders);

	for (i = 0; i < mailbox_snapshot->folders; i++) {
		int msg_folder_index = i;

		/* We want this message in the snapshot if any of the following:
		 *   No folder was specified.
		 *   The specified folder matches the current folder.
		 *   The specified folder is INBOX AND we were asked to combine messages AND the current folder is either Old or Urgent.
		 */
		if (!(this_index_only == -1 || this_index_only == i || (this_index_only == inbox_index && combine_INBOX_and_OLD && (i == old_index || i == urgent_index)))) {
			continue;
		}

		/* Make sure that Old or Urgent messages are marked as being in INBOX. */
		if (combine_INBOX_and_OLD && (i == old_index || i == urgent_index)) {
			msg_folder_index = inbox_index;
		}

		memset(&vms, 0, sizeof(vms));
		ast_copy_string(vms.username, mailbox, sizeof(vms.username));
		vms.lastmsg = -1;
		open = 0;

		/* open the mailbox state */
		if ((res = open_mailbox(&vms, vmu, i)) < 0) {
			ast_log(LOG_WARNING, "Could not open mailbox %s\n", mailbox);
			goto snapshot_cleanup;
		}
		open = 1;

		/* Iterate through each msg, storing off info */
		if (vms.lastmsg != -1) {
			if ((vm_msg_snapshot_create(vmu, &vms, mailbox_snapshot, msg_folder_index, i, descending, sort_val))) {
				ast_log(LOG_WARNING, "Failed to create msg snapshots for %s@%s\n", mailbox, context);
				goto snapshot_cleanup;
			}
		}

		/* close mailbox */
		if ((res = close_mailbox(&vms, vmu) == ERROR_LOCK_PATH)) {
			goto snapshot_cleanup;
		}
		open = 0;
	}

snapshot_cleanup:
	if (vmu && open) {
		close_mailbox(&vms, vmu);
	}

#ifdef IMAP_STORAGE
	if (vmu) {
		vmstate_delete(&vms);
	}
#endif

	free_user(vmu);
	return mailbox_snapshot;
}

static struct ast_vm_mailbox_snapshot *vm_mailbox_snapshot_destroy(struct ast_vm_mailbox_snapshot *mailbox_snapshot)
{
	int i;
	struct ast_vm_msg_snapshot *msg_snapshot;

	for (i = 0; i < mailbox_snapshot->folders; i++) {
		while ((msg_snapshot = AST_LIST_REMOVE_HEAD(&mailbox_snapshot->snapshots[i], msg))) {
			msg_snapshot = vm_msg_snapshot_destroy(msg_snapshot);
		}
	}
	ast_free(mailbox_snapshot->snapshots);
	ast_free(mailbox_snapshot);
	return NULL;
}

/*!
 * \brief common bounds checking and existence check for Voicemail API functions.
 *
 * \details
 * This is called by vm_msg_move, vm_msg_remove, and vm_msg_forward to
 * ensure that data passed in are valid. This ensures that given the
 * desired message IDs, they can be found.
 *
 * \param vms The voicemail state corresponding to an open mailbox
 * \param msg_ids An array of message identifiers
 * \param num_msgs The number of identifiers in msg_ids
 * \param[out] msg_nums The message indexes corresponding to the given
 * \param vmu
 * message IDs
 * \pre vms must have open_mailbox() called on it prior to this function.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
static int message_range_and_existence_check(struct vm_state *vms, const char *msg_ids [], size_t num_msgs, int *msg_nums, struct ast_vm_user *vmu)
{
	int i;
	int res = 0;
	for (i = 0; i < num_msgs; ++i) {
		const char *msg_id = msg_ids[i];
		int found = 0;
		for (vms->curmsg = 0; vms->curmsg <= vms->lastmsg; vms->curmsg++) {
			const char *other_msg_id;
			char filename[PATH_MAX];
			struct ast_config *msg_cfg;
			struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };

			make_file(vms->fn, sizeof(vms->fn), vms->curdir, vms->curmsg);
			snprintf(filename, sizeof(filename), "%s.txt", vms->fn);
			RETRIEVE(vms->curdir, vms->curmsg, vmu->mailbox, vmu->context);
			msg_cfg = ast_config_load(filename, config_flags);
			if (!msg_cfg || msg_cfg == CONFIG_STATUS_FILEINVALID) {
				DISPOSE(vms->curdir, vms->curmsg);
				res = -1;
				goto done;
			}

			other_msg_id = ast_variable_retrieve(msg_cfg, "message", "msg_id");

			if (!ast_strlen_zero(other_msg_id) && !strcmp(other_msg_id, msg_id)) {
				/* Message found. We can get out of this inner loop
				 * and move on to the next message to find
				 */
				found = 1;
				msg_nums[i] = vms->curmsg;
				ast_config_destroy(msg_cfg);
				DISPOSE(vms->curdir, vms->curmsg);
				break;
			}
			ast_config_destroy(msg_cfg);
			DISPOSE(vms->curdir, vms->curmsg);
		}
		if (!found) {
			/* If we can't find one of the message IDs requested, then OH NO! */
			res = -1;
			goto done;
		}
	}

done:
	return res;
}

static void notify_new_state(struct ast_vm_user *vmu)
{
	int new = 0, old = 0, urgent = 0;
	char ext_context[1024];

	snprintf(ext_context, sizeof(ext_context), "%s@%s", vmu->mailbox, vmu->context);
	run_externnotify(vmu->context, vmu->mailbox, NULL);
	ast_app_inboxcount2(ext_context, &urgent, &new, &old);
	queue_mwi_event(NULL, ext_context, urgent, new, old);
}

static int vm_msg_forward(const char *from_mailbox,
	const char *from_context,
	const char *from_folder,
	const char *to_mailbox,
	const char *to_context,
	const char *to_folder,
	size_t num_msgs,
	const char *msg_ids [],
	int delete_old)
{
	struct vm_state from_vms;
	struct ast_vm_user *vmu = NULL, vmus;
	struct ast_vm_user *to_vmu = NULL, to_vmus;
	struct ast_config *msg_cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
	char filename[PATH_MAX];
	int from_folder_index;
	int open = 0;
	int res = 0;
	int i;
	int *msg_nums;

	if (ast_strlen_zero(from_mailbox) || ast_strlen_zero(to_mailbox)) {
		ast_log(LOG_WARNING, "Cannot forward message because either the from or to mailbox was not specified\n");
		return -1;
	}

	if (!num_msgs) {
		ast_log(LOG_WARNING, "Invalid number of messages specified to forward: %zu\n", num_msgs);
		return -1;
	}

	if (ast_strlen_zero(from_folder) || ast_strlen_zero(to_folder)) {
		ast_log(LOG_WARNING, "Cannot forward message because the from_folder or to_folder was not specified\n");
		return -1;
	}

	memset(&vmus, 0, sizeof(vmus));
	memset(&to_vmus, 0, sizeof(to_vmus));
	memset(&from_vms, 0, sizeof(from_vms));

	from_folder_index = get_folder_by_name(from_folder);
	if (from_folder_index == -1) {
		return -1;
	}

	if (get_folder_by_name(to_folder) == -1) {
		return -1;
	}

	if (!(vmu = find_user(&vmus, from_context, from_mailbox))) {
		ast_log(LOG_WARNING, "Can't find voicemail user to forward from (%s@%s)\n", from_mailbox, from_context);
		return -1;
	}

	if (!(to_vmu = find_user(&to_vmus, to_context, to_mailbox))) {
		ast_log(LOG_WARNING, "Can't find voicemail user to forward to (%s@%s)\n", to_mailbox, to_context);
		free_user(vmu);
		return -1;
	}

	ast_copy_string(from_vms.username, from_mailbox, sizeof(from_vms.username));
	from_vms.lastmsg = -1;
	open = 0;

	/* open the mailbox state */
	if ((res = open_mailbox(&from_vms, vmu, from_folder_index)) < 0) {
		ast_log(LOG_WARNING, "Could not open mailbox %s\n", from_mailbox);
		res = -1;
		goto vm_forward_cleanup;
	}

	open = 1;

	if ((from_vms.lastmsg + 1) < num_msgs) {
		ast_log(LOG_WARNING, "Folder %s has less than %zu messages\n", from_folder, num_msgs);
		res = -1;
		goto vm_forward_cleanup;
	}

	msg_nums = ast_alloca(sizeof(int) * num_msgs);

	if ((res = message_range_and_existence_check(&from_vms, msg_ids, num_msgs, msg_nums, vmu) < 0)) {
		goto vm_forward_cleanup;
	}

	/* Now we actually forward the messages */
	for (i = 0; i < num_msgs; i++) {
		int cur_msg = msg_nums[i];
		int duration = 0;
		const char *value;

		make_file(from_vms.fn, sizeof(from_vms.fn), from_vms.curdir, cur_msg);
		snprintf(filename, sizeof(filename), "%s.txt", from_vms.fn);
		RETRIEVE(from_vms.curdir, cur_msg, vmu->mailbox, vmu->context);
		msg_cfg = ast_config_load(filename, config_flags);
		/* XXX This likely will not fail since we previously ensured that the
		 * message we are looking for exists. However, there still could be some
		 * circumstance where this fails, so atomicity is not guaranteed.
		 */
		if (!msg_cfg || msg_cfg == CONFIG_STATUS_FILEINVALID) {
			DISPOSE(from_vms.curdir, cur_msg);
			continue;
		}
		if ((value = ast_variable_retrieve(msg_cfg, "message", "duration"))) {
			duration = atoi(value);
		}

		copy_message(NULL, vmu, from_folder_index, cur_msg, duration, to_vmu, vmfmts, from_vms.curdir, "", to_folder);

		if (delete_old) {
			from_vms.deleted[cur_msg] = 1;
		}
		ast_config_destroy(msg_cfg);
		DISPOSE(from_vms.curdir, cur_msg);
	}

	/* close mailbox */
	if ((res = close_mailbox(&from_vms, vmu) == ERROR_LOCK_PATH)) {
		res = -1;
		goto vm_forward_cleanup;
	}
	open = 0;

vm_forward_cleanup:
	if (vmu && open) {
		close_mailbox(&from_vms, vmu);
	}
#ifdef IMAP_STORAGE
	if (vmu) {
		vmstate_delete(&from_vms);
	}
#endif

	if (!res) {
		notify_new_state(to_vmu);
	}

	free_user(vmu);
	free_user(to_vmu);
	return res;
}

static int vm_msg_move(const char *mailbox,
	const char *context,
	size_t num_msgs,
	const char *oldfolder,
	const char *old_msg_ids [],
	const char *newfolder)
{
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	int old_folder_index;
	int new_folder_index;
	int open = 0;
	int res = 0;
	int i;
	int *old_msg_nums;

	if (ast_strlen_zero(mailbox)) {
		ast_log(LOG_WARNING, "Cannot move message because no mailbox was specified\n");
		return -1;
	}

	if (!num_msgs) {
		ast_log(LOG_WARNING, "Invalid number of messages specified to move: %zu\n", num_msgs);
		return -1;
	}

	if (ast_strlen_zero(oldfolder) || ast_strlen_zero(newfolder)) {
		ast_log(LOG_WARNING, "Cannot move message because either oldfolder or newfolder was not specified\n");
		return -1;
	}

	old_folder_index = get_folder_by_name(oldfolder);
	new_folder_index = get_folder_by_name(newfolder);

	memset(&vmus, 0, sizeof(vmus));
	memset(&vms, 0, sizeof(vms));

	if (old_folder_index == -1 || new_folder_index == -1) {
		return -1;
	}

	if (!(vmu = find_user(&vmus, context, mailbox))) {
		return -1;
	}

	ast_copy_string(vms.username, mailbox, sizeof(vms.username));
	vms.lastmsg = -1;
	open = 0;

	/* open the mailbox state */
	if ((res = open_mailbox(&vms, vmu, old_folder_index)) < 0) {
		ast_log(LOG_WARNING, "Could not open mailbox %s\n", mailbox);
		res = -1;
		goto vm_move_cleanup;
	}

	open = 1;

	if ((vms.lastmsg + 1) < num_msgs) {
		ast_log(LOG_WARNING, "Folder %s has less than %zu messages\n", oldfolder, num_msgs);
		res = -1;
		goto vm_move_cleanup;
	}

	old_msg_nums = ast_alloca(sizeof(int) * num_msgs);

	if ((res = message_range_and_existence_check(&vms, old_msg_ids, num_msgs, old_msg_nums, vmu)) < 0) {
		goto vm_move_cleanup;
	}

	/* Now actually move the message */
	for (i = 0; i < num_msgs; ++i) {
		if (save_to_folder(vmu, &vms, old_msg_nums[i], new_folder_index, NULL, 0)) {
			res = -1;
			goto vm_move_cleanup;
		}
		vms.deleted[old_msg_nums[i]] = 1;
	}

	/* close mailbox */
	if ((res = close_mailbox(&vms, vmu) == ERROR_LOCK_PATH)) {
		res = -1;
		goto vm_move_cleanup;
	}
	open = 0;

vm_move_cleanup:
	if (vmu && open) {
		close_mailbox(&vms, vmu);
	}
#ifdef IMAP_STORAGE
	if (vmu) {
		vmstate_delete(&vms);
	}
#endif

	if (!res) {
		notify_new_state(vmu);
	}

	free_user(vmu);
	return res;
}

static int vm_msg_remove(const char *mailbox,
	const char *context,
	size_t num_msgs,
	const char *folder,
	const char *msgs[])
{
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	int folder_index;
	int open = 0;
	int res = 0;
	int i;
	int *msg_nums;

	if (ast_strlen_zero(mailbox)) {
		ast_log(LOG_WARNING, "Cannot remove message because no mailbox was specified\n");
		return -1;
	}

	if (!num_msgs) {
		ast_log(LOG_WARNING, "Invalid number of messages specified to remove: %zu\n", num_msgs);
		return -1;
	}

	if (ast_strlen_zero(folder)) {
		ast_log(LOG_WARNING, "Cannot remove message because no folder was specified\n");
		return -1;
	}

	memset(&vmus, 0, sizeof(vmus));
	memset(&vms, 0, sizeof(vms));

	folder_index = get_folder_by_name(folder);
	if (folder_index == -1) {
		ast_log(LOG_WARNING, "Could not remove msgs from unknown folder %s\n", folder);
		return -1;
	}

	if (!(vmu = find_user(&vmus, context, mailbox))) {
		ast_log(LOG_WARNING, "Can't find voicemail user to remove msg from (%s@%s)\n", mailbox, context);
		return -1;
	}

	ast_copy_string(vms.username, mailbox, sizeof(vms.username));
	vms.lastmsg = -1;
	open = 0;

	/* open the mailbox state */
	if ((res = open_mailbox(&vms, vmu, folder_index)) < 0) {
		ast_log(LOG_WARNING, "Could not open mailbox %s\n", mailbox);
		res = -1;
		goto vm_remove_cleanup;
	}

	open = 1;

	if ((vms.lastmsg + 1) < num_msgs) {
		ast_log(LOG_WARNING, "Folder %s has less than %zu messages\n", folder, num_msgs);
		res = -1;
		goto vm_remove_cleanup;
	}

	msg_nums = ast_alloca(sizeof(int) * num_msgs);

	if ((res = message_range_and_existence_check(&vms, msgs, num_msgs, msg_nums, vmu)) < 0) {
		goto vm_remove_cleanup;
	}

	for (i = 0; i < num_msgs; i++) {
		vms.deleted[msg_nums[i]] = 1;
	}

	/* close mailbox */
	if ((res = close_mailbox(&vms, vmu) == ERROR_LOCK_PATH)) {
		res = -1;
		ast_log(AST_LOG_ERROR, "Failed to close mailbox folder %s while removing msgs\n", folder);
		goto vm_remove_cleanup;
	}
	open = 0;

vm_remove_cleanup:
	if (vmu && open) {
		close_mailbox(&vms, vmu);
	}
#ifdef IMAP_STORAGE
	if (vmu) {
		vmstate_delete(&vms);
	}
#endif

	if (!res) {
		notify_new_state(vmu);
	}

	free_user(vmu);
	return res;
}

static int vm_msg_play(struct ast_channel *chan,
	const char *mailbox,
	const char *context,
	const char *folder,
	const char *msg_id,
	ast_vm_msg_play_cb cb)
{
	struct vm_state vms;
	struct ast_vm_user *vmu = NULL, vmus;
	int res = 0;
	int open = 0;
	int i;
	char filename[PATH_MAX];
	struct ast_config *msg_cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
	int duration = 0;
	const char *value;

	if (ast_strlen_zero(mailbox)) {
		ast_log(LOG_WARNING, "Cannot play message because no mailbox was specified\n");
		return -1;
	}

	if (ast_strlen_zero(folder)) {
		ast_log(LOG_WARNING, "Cannot play message because no folder was specified\n");
		return -1;
	}

	if (ast_strlen_zero(msg_id)) {
		ast_log(LOG_WARNING, "Cannot play message because no message number was specified\n");
		return -1;
	}

	memset(&vmus, 0, sizeof(vmus));
	memset(&vms, 0, sizeof(vms));

	if (ast_strlen_zero(context)) {
		context = "default";
	}

	if (!(vmu = find_user(&vmus, context, mailbox))) {
		return -1;
	}

	i = get_folder_by_name(folder);
	ast_copy_string(vms.username, mailbox, sizeof(vms.username));
	vms.lastmsg = -1;
	if ((res = open_mailbox(&vms, vmu, i)) < 0) {
		ast_log(LOG_WARNING, "Could not open mailbox %s\n", mailbox);
		goto play2_msg_cleanup;
	}
	open = 1;

	if (message_range_and_existence_check(&vms, &msg_id, 1, &vms.curmsg, vmu)) {
		res = -1;
		goto play2_msg_cleanup;
	}

	/* Find the msg */
	make_file(vms.fn, sizeof(vms.fn), vms.curdir, vms.curmsg);
	snprintf(filename, sizeof(filename), "%s.txt", vms.fn);
	RETRIEVE(vms.curdir, vms.curmsg, vmu->mailbox, vmu->context);

	msg_cfg = ast_config_load(filename, config_flags);
	if (!msg_cfg || msg_cfg == CONFIG_STATUS_FILEINVALID) {
		DISPOSE(vms.curdir, vms.curmsg);
		res = -1;
		goto play2_msg_cleanup;
	}
	if ((value = ast_variable_retrieve(msg_cfg, "message", "duration"))) {
		duration = atoi(value);
	}
	ast_config_destroy(msg_cfg);

#ifdef IMAP_STORAGE
	/*IMAP storage stores any prepended message from a forward
	 * as a separate file from the rest of the message
	 */
	if (!ast_strlen_zero(vms.introfn) && ast_fileexists(vms.introfn, NULL, NULL) > 0) {
		wait_file(chan, &vms, vms.introfn);
	}
#endif
	if (cb) {
		cb(chan, vms.fn, duration);
	} else if ((wait_file(chan, &vms, vms.fn)) < 0) {
		ast_log(AST_LOG_WARNING, "Playback of message %s failed\n", vms.fn);
	} else {
		res = 0;
	}

	vms.heard[vms.curmsg] = 1;

	/* cleanup configs and msg */
	DISPOSE(vms.curdir, vms.curmsg);

play2_msg_cleanup:
	if (vmu && open) {
		close_mailbox(&vms, vmu);
	}

#ifdef IMAP_STORAGE
	if (vmu) {
		vmstate_delete(&vms);
	}
#endif

	if (!res) {
		notify_new_state(vmu);
	}

	free_user(vmu);
	return res;
}

/* This is a workaround so that menuselect displays a proper description
 * AST_MODULE_INFO(, , "Comedian Mail (Voicemail System)"
 */

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, tdesc,
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.optional_modules = "res_adsi,res_smdi",
);
