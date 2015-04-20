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
 * \brief Implementation of Inter-Asterisk eXchange Version 2
 *        as specified in RFC 5456
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \par See also
 * \arg \ref Config_iax
 *
 * \ingroup channel_drivers
 *
 * \todo Implement musicclass settings for IAX2 devices
 */

/*! \li \ref chan_iax2.c uses the configuration file \ref iax.conf
 * \addtogroup configuration_file
 */

/*! \page iax.conf iax.conf
 * \verbinclude iax.conf.sample
 */

/*!
 * \todo XXX The IAX2 channel driver needs its native bridge
 * code converted to the new bridge technology scheme.
 *
 * \note The chan_dahdi native bridge code can be used as an
 * example.  It also appears that chan_iax2 also has a native
 * transfer check like chan_dahdi to eliminate tromboned calls.
 *
 * \note The existing native bridge code is marked with the
 * IAX2_NATIVE_BRIDGING conditional.
 */

/*** MODULEINFO
	<use type="external">crypto</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/mman.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <signal.h>
#include <strings.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <regex.h>

#include "asterisk/paths.h"

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/translate.h"
#include "asterisk/md5.h"
#include "asterisk/crypto.h"
#include "asterisk/acl.h"
#include "asterisk/manager.h"
#include "asterisk/callerid.h"
#include "asterisk/app.h"
#include "asterisk/astdb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/features.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/localtime.h"
#include "asterisk/dnsmgr.h"
#include "asterisk/devicestate.h"
#include "asterisk/netsock.h"
#include "asterisk/stringfields.h"
#include "asterisk/linkedlists.h"
#include "asterisk/astobj2.h"
#include "asterisk/timing.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/test.h"
#include "asterisk/data.h"
#include "asterisk/security_events.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/bridge.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_system.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_compatibility.h"
#include "asterisk/format_cap.h"

#include "iax2/include/iax2.h"
#include "iax2/include/firmware.h"
#include "iax2/include/parser.h"
#include "iax2/include/provision.h"
#include "iax2/include/codec_pref.h"
#include "iax2/include/format_compatibility.h"

#include "jitterbuf.h"

/*** DOCUMENTATION
	<application name="IAX2Provision" language="en_US">
		<synopsis>
			Provision a calling IAXy with a given template.
		</synopsis>
		<syntax>
			<parameter name="template">
				<para>If not specified, defaults to <literal>default</literal>.</para>
			</parameter>
		</syntax>
		<description>
			<para>Provisions the calling IAXy (assuming the calling entity is in fact an IAXy) with the
			given <replaceable>template</replaceable>. Returns <literal>-1</literal> on error
			or <literal>0</literal> on success.</para>
		</description>
	</application>
	<function name="IAXPEER" language="en_US">
		<synopsis>
			Gets IAX peer information.
		</synopsis>
		<syntax>
			<parameter name="peername" required="true">
				<enumlist>
					<enum name="CURRENTCHANNEL">
						<para>If <replaceable>peername</replaceable> is specified to this value, return the IP address of the
						endpoint of the current channel</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="item">
				<para>If <replaceable>peername</replaceable> is specified, valid items are:</para>
				<enumlist>
					<enum name="ip">
						<para>(default) The IP address.</para>
					</enum>
					<enum name="status">
						<para>The peer's status (if <literal>qualify=yes</literal>)</para>
					</enum>
					<enum name="mailbox">
						<para>The configured mailbox.</para>
					</enum>
					<enum name="context">
						<para>The configured context.</para>
					</enum>
					<enum name="expire">
						<para>The epoch time of the next expire.</para>
					</enum>
					<enum name="dynamic">
						<para>Is it dynamic? (yes/no).</para>
					</enum>
					<enum name="callerid_name">
						<para>The configured Caller ID name.</para>
					</enum>
					<enum name="callerid_num">
						<para>The configured Caller ID number.</para>
					</enum>
					<enum name="codecs">
						<para>The configured codecs.</para>
					</enum>
					<enum name="codec[x]">
						<para>Preferred codec index number <replaceable>x</replaceable> (beginning
						with <literal>0</literal>)</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Gets information associated with the specified IAX2 peer.</para>
		</description>
		<see-also>
			<ref type="function">SIPPEER</ref>
		</see-also>
	</function>
	<function name="IAXVAR" language="en_US">
		<synopsis>
			Sets or retrieves a remote variable.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
		</syntax>
		<description>
			<para>Gets or sets a variable that is sent to a remote IAX2 peer during call setup.</para>
		</description>
	</function>
	<manager name="IAXpeers" language="en_US">
		<synopsis>
			List IAX peers.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
		</description>
	</manager>
	<manager name="IAXpeerlist" language="en_US">
		<synopsis>
			List IAX Peers.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>List all the IAX peers.</para>
		</description>
	</manager>
	<manager name="IAXnetstats" language="en_US">
		<synopsis>
			Show IAX Netstats.
		</synopsis>
		<syntax />
		<description>
			<para>Show IAX channels network statistics.</para>
		</description>
	</manager>
	<manager name="IAXregistry" language="en_US">
		<synopsis>
			Show IAX registrations.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Show IAX registrations.</para>
		</description>
	</manager>
 ***/

/* Define SCHED_MULTITHREADED to run the scheduler in a special
   multithreaded mode. */
#define SCHED_MULTITHREADED

/* Define DEBUG_SCHED_MULTITHREADED to keep track of where each
   thread is actually doing. */
#define DEBUG_SCHED_MULTITHREAD


#ifdef SO_NO_CHECK
static int nochecksums = 0;
#endif

#define PTR_TO_CALLNO(a) ((unsigned short)(unsigned long)(a))
#define CALLNO_TO_PTR(a) ((void *)(unsigned long)(a))

#define DEFAULT_THREAD_COUNT 10
#define DEFAULT_MAX_THREAD_COUNT 100
#define DEFAULT_RETRY_TIME 1000
#define MEMORY_SIZE 100
#define DEFAULT_DROP 3

#define DEBUG_SUPPORT

#define MIN_REUSE_TIME		60	/* Don't reuse a call number within 60 seconds */

/* Sample over last 100 units to determine historic jitter */
#define GAMMA (0.01)

static struct iax2_codec_pref prefs_global;

static const char tdesc[] = "Inter Asterisk eXchange Driver (Ver 2)";


/*! \brief Maximum transmission unit for the UDP packet in the trunk not to be
    fragmented. This is based on 1516 - ethernet - ip - udp - iax minus one g711 frame = 1240 */
#define MAX_TRUNK_MTU 1240

static int global_max_trunk_mtu;	/*!< Maximum MTU, 0 if not used */
static int trunk_timed, trunk_untimed, trunk_maxmtu, trunk_nmaxmtu ;	/*!< Trunk MTU statistics */

#define DEFAULT_CONTEXT "default"

static char default_parkinglot[AST_MAX_CONTEXT];

static char language[MAX_LANGUAGE] = "";
static char regcontext[AST_MAX_CONTEXT] = "";

static struct stasis_subscription *network_change_sub; /*!< subscription id for network change events */
static struct stasis_subscription *acl_change_sub; /*!< subscription id for ACL change events */
static int network_change_sched_id = -1;

static int maxauthreq = 3;
static int max_retries = 4;
static int ping_time = 21;
static int lagrq_time = 10;
static int maxjitterbuffer=1000;
static int resyncthreshold=1000;
static int maxjitterinterps=10;
static int jittertargetextra = 40; /* number of milliseconds the new jitter buffer adds on to its size */

#define MAX_TRUNKDATA           640 * 200       /*!< 40ms, uncompressed linear * 200 channels */

static int trunkfreq = 20;
static int trunkmaxsize = MAX_TRUNKDATA;

static int authdebug = 0;
static int autokill = 0;
static int iaxcompat = 0;
static int last_authmethod = 0;

static int iaxdefaultdpcache=10 * 60;	/* Cache dialplan entries for 10 minutes by default */

static int iaxdefaulttimeout = 5;		/* Default to wait no more than 5 seconds for a reply to come back */

static struct {
	unsigned int tos;
	unsigned int cos;
} qos = { 0, 0 };

static int min_reg_expire;
static int max_reg_expire;

static int srvlookup = 0;

static struct ast_timer *timer;				/* Timer for trunking */

static struct ast_netsock_list *netsock;
static struct ast_netsock_list *outsock;		/*!< used if sourceaddress specified and bindaddr == INADDR_ANY */
static int defaultsockfd = -1;

static int (*iax2_regfunk)(const char *username, int onoff) = NULL;

/* Ethernet, etc */
#define IAX_CAPABILITY_FULLBANDWIDTH	0xFFFF
/* T1, maybe ISDN */
#define IAX_CAPABILITY_MEDBANDWIDTH (IAX_CAPABILITY_FULLBANDWIDTH & \
                     ~AST_FORMAT_SLIN &      \
                     ~AST_FORMAT_SLIN16 &    \
                     ~AST_FORMAT_SIREN7 &       \
                     ~AST_FORMAT_SIREN14 &      \
                     ~AST_FORMAT_G719 &         \
                     ~AST_FORMAT_ULAW &         \
                     ~AST_FORMAT_ALAW &         \
                     ~AST_FORMAT_G722)
/* A modem */
#define IAX_CAPABILITY_LOWBANDWIDTH (IAX_CAPABILITY_MEDBANDWIDTH & \
                     ~AST_FORMAT_G726 &         \
                     ~AST_FORMAT_G726_AAL2 &    \
                     ~AST_FORMAT_ADPCM)

#define IAX_CAPABILITY_LOWFREE      (IAX_CAPABILITY_LOWBANDWIDTH & \
                     ~AST_FORMAT_G723)


#define DEFAULT_MAXMS		2000		/* Must be faster than 2 seconds by default */
#define DEFAULT_FREQ_OK		60 * 1000	/* How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK	10 * 1000	/* How often to check, if the host is down... */

/* if a pvt has encryption setup done and is running on the call */
#define IAX_CALLENCRYPTED(pvt) \
	(ast_test_flag64(pvt, IAX_ENCRYPTED) && ast_test_flag64(pvt, IAX_KEYPOPULATED))

#define IAX_DEBUGDIGEST(msg, key) do { \
		int idx; \
		char digest[33] = ""; \
		\
		if (!iaxdebug) \
			break; \
		\
		for (idx = 0; idx < 16; idx++) \
			sprintf(digest + (idx << 1), "%02hhx", (unsigned char) key[idx]); \
		\
		ast_log(LOG_NOTICE, msg " IAX_COMMAND_RTKEY to rotate key to '%s'\n", digest); \
	} while(0)

static struct io_context *io;
static struct ast_sched_context *sched;

static iax2_format iax2_capability = IAX_CAPABILITY_FULLBANDWIDTH;

static int iaxdebug = 0;

static int iaxtrunkdebug = 0;

static int test_losspct = 0;
#ifdef IAXTESTS
static int test_late = 0;
static int test_resync = 0;
static int test_jit = 0;
static int test_jitpct = 0;
#endif /* IAXTESTS */

static char accountcode[AST_MAX_ACCOUNT_CODE];
static char mohinterpret[MAX_MUSICCLASS];
static char mohsuggest[MAX_MUSICCLASS];
static int amaflags = 0;
static int adsi = 0;
static int delayreject = 0;
static int iax2_encryption = 0;

static struct ast_flags64 globalflags = { 0 };

static pthread_t netthreadid = AST_PTHREADT_NULL;

enum iax2_state {
	IAX_STATE_STARTED =			(1 << 0),
	IAX_STATE_AUTHENTICATED =	(1 << 1),
	IAX_STATE_TBD =				(1 << 2),
};

struct iax2_context {
	char context[AST_MAX_CONTEXT];
	struct iax2_context *next;
};


#define	IAX_HASCALLERID         (uint64_t)(1 << 0)    /*!< CallerID has been specified */
#define IAX_DELME               (uint64_t)(1 << 1)    /*!< Needs to be deleted */
#define IAX_TEMPONLY            (uint64_t)(1 << 2)    /*!< Temporary (realtime) */
#define IAX_TRUNK               (uint64_t)(1 << 3)    /*!< Treat as a trunk */
#define IAX_NOTRANSFER          (uint64_t)(1 << 4)    /*!< Don't native bridge */
#define IAX_USEJITTERBUF        (uint64_t)(1 << 5)    /*!< Use jitter buffer */
#define IAX_DYNAMIC             (uint64_t)(1 << 6)    /*!< dynamic peer */
#define IAX_SENDANI             (uint64_t)(1 << 7)    /*!< Send ANI along with CallerID */
#define IAX_RTSAVE_SYSNAME      (uint64_t)(1 << 8)    /*!< Save Systname on Realtime Updates */
#define IAX_ALREADYGONE         (uint64_t)(1 << 9)    /*!< Already disconnected */
#define IAX_PROVISION           (uint64_t)(1 << 10)   /*!< This is a provisioning request */
#define IAX_QUELCH              (uint64_t)(1 << 11)   /*!< Whether or not we quelch audio */
#define IAX_ENCRYPTED           (uint64_t)(1 << 12)   /*!< Whether we should assume encrypted tx/rx */
#define IAX_KEYPOPULATED        (uint64_t)(1 << 13)   /*!< Whether we have a key populated */
#define IAX_CODEC_USER_FIRST    (uint64_t)(1 << 14)   /*!< are we willing to let the other guy choose the codec? */
#define IAX_CODEC_NOPREFS       (uint64_t)(1 << 15)   /*!< Force old behaviour by turning off prefs */
#define IAX_CODEC_NOCAP         (uint64_t)(1 << 16)   /*!< only consider requested format and ignore capabilities*/
#define IAX_RTCACHEFRIENDS      (uint64_t)(1 << 17)   /*!< let realtime stay till your reload */
#define IAX_RTUPDATE            (uint64_t)(1 << 18)   /*!< Send a realtime update */
#define IAX_RTAUTOCLEAR         (uint64_t)(1 << 19)   /*!< erase me on expire */
#define IAX_RTIGNOREREGEXPIRE   (uint64_t)(1 << 21)   /*!< When using realtime, ignore registration expiration */
#define IAX_TRUNKTIMESTAMPS     (uint64_t)(1 << 22)   /*!< Send trunk timestamps */
#define IAX_TRANSFERMEDIA       (uint64_t)(1 << 23)   /*!< When doing IAX2 transfers, transfer media only */
#define IAX_MAXAUTHREQ          (uint64_t)(1 << 24)   /*!< Maximum outstanding AUTHREQ restriction is in place */
#define IAX_DELAYPBXSTART       (uint64_t)(1 << 25)   /*!< Don't start a PBX on the channel until the peer sends us a response, so that we've achieved a three-way handshake with them before sending voice or anything else */
#define IAX_ALLOWFWDOWNLOAD     (uint64_t)(1 << 26)   /*!< Allow the FWDOWNL command? */
#define IAX_IMMEDIATE           (uint64_t)(1 << 27)   /*!< Allow immediate off-hook to extension s */
#define IAX_SENDCONNECTEDLINE   (uint64_t)(1 << 28)   /*!< Allow sending of connected line updates */
#define IAX_RECVCONNECTEDLINE   (uint64_t)(1 << 29)   /*!< Allow receiving of connected line updates */
#define IAX_FORCE_ENCRYPT       (uint64_t)(1 << 30)   /*!< Forces call encryption, if encryption not possible hangup */
#define IAX_SHRINKCALLERID      (uint64_t)(1 << 31)   /*!< Turn on and off caller id shrinking */
static int global_rtautoclear = 120;

static int reload_config(int forced_reload);

/*!
 * \brief Call token validation settings.
 */
enum calltoken_peer_enum {
	/*! \brief Default calltoken required unless the ip is in the ignorelist */
	CALLTOKEN_DEFAULT = 0,
	/*! \brief Require call token validation. */
	CALLTOKEN_YES = 1,
	/*! \brief Require call token validation after a successful registration
	 *         using call token validation occurs. */
	CALLTOKEN_AUTO = 2,
	/*! \brief Do not require call token validation. */
	CALLTOKEN_NO = 3,
};

struct iax2_user {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(secret);
		AST_STRING_FIELD(dbsecret);
		AST_STRING_FIELD(accountcode);
		AST_STRING_FIELD(mohinterpret);
		AST_STRING_FIELD(mohsuggest);
		AST_STRING_FIELD(inkeys);               /*!< Key(s) this user can use to authenticate to us */
		AST_STRING_FIELD(language);
		AST_STRING_FIELD(cid_num);
		AST_STRING_FIELD(cid_name);
		AST_STRING_FIELD(parkinglot);           /*!< Default parkinglot for device */
	);

	int authmethods;
	int encmethods;
	int amaflags;
	int adsi;
	uint64_t flags;
	iax2_format capability;
	int maxauthreq; /*!< Maximum allowed outstanding AUTHREQs */
	int curauthreq; /*!< Current number of outstanding AUTHREQs */
	struct iax2_codec_pref prefs;
	struct ast_acl_list *acl;
	struct iax2_context *contexts;
	struct ast_variable *vars;
	enum calltoken_peer_enum calltoken_required;        /*!< Is calltoken validation required or not, can be YES, NO, or AUTO */
};

struct iax2_peer {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(username);
		AST_STRING_FIELD(description);		/*!< Description of the peer */
		AST_STRING_FIELD(secret);
		AST_STRING_FIELD(dbsecret);
		AST_STRING_FIELD(outkey);	    /*!< What key we use to talk to this peer */

		AST_STRING_FIELD(regexten);     /*!< Extension to register (if regcontext is used) */
		AST_STRING_FIELD(context);      /*!< For transfers only */
		AST_STRING_FIELD(peercontext);  /*!< Context to pass to peer */
		AST_STRING_FIELD(mailbox);	    /*!< Mailbox */
		AST_STRING_FIELD(mohinterpret);
		AST_STRING_FIELD(mohsuggest);
		AST_STRING_FIELD(inkeys);		/*!< Key(s) this peer can use to authenticate to us */
		/* Suggested caller id if registering */
		AST_STRING_FIELD(cid_num);		/*!< Default context (for transfer really) */
		AST_STRING_FIELD(cid_name);		/*!< Default context (for transfer really) */
		AST_STRING_FIELD(zonetag);		/*!< Time Zone */
		AST_STRING_FIELD(parkinglot);   /*!< Default parkinglot for device */
	);
	struct iax2_codec_pref prefs;
	struct ast_dnsmgr_entry *dnsmgr;		/*!< DNS refresh manager */
	struct ast_sockaddr addr;
	int formats;
	int sockfd;					/*!< Socket to use for transmission */
	struct ast_sockaddr mask;
	int adsi;
	uint64_t flags;

	/* Dynamic Registration fields */
	struct ast_sockaddr defaddr;			/*!< Default address if there is one */
	int authmethods;				/*!< Authentication methods (IAX_AUTH_*) */
	int encmethods;					/*!< Encryption methods (IAX_ENCRYPT_*) */

	int expire;					/*!< Schedule entry for expiry */
	int expiry;					/*!< How soon to expire */
	iax2_format capability;				/*!< Capability */

	/* Qualification */
	int callno;					/*!< Call number of POKE request */
	int pokeexpire;					/*!< Scheduled qualification-related task (ie iax2_poke_peer_s or iax2_poke_noanswer) */
	int lastms;					/*!< How long last response took (in ms), or -1 for no response */
	int maxms;					/*!< Max ms we will accept for the host to be up, 0 to not monitor */

	int pokefreqok;					/*!< How often to check if the host is up */
	int pokefreqnotok;				/*!< How often to check when the host has been determined to be down */
	int historicms;					/*!< How long recent average responses took */
	int smoothing;					/*!< Sample over how many units to determine historic ms */
	uint16_t maxcallno;				/*!< Max call number limit for this peer.  Set on registration */

	struct stasis_subscription *mwi_event_sub;	/*!< This subscription lets pollmailboxes know which mailboxes need to be polled */

	struct ast_acl_list *acl;
	enum calltoken_peer_enum calltoken_required;	/*!< Is calltoken validation required or not, can be YES, NO, or AUTO */

	struct ast_endpoint *endpoint; /*!< Endpoint structure for this peer */
};

#define IAX2_TRUNK_PREFACE (sizeof(struct iax_frame) + sizeof(struct ast_iax2_meta_hdr) + sizeof(struct ast_iax2_meta_trunk_hdr))

struct iax2_trunk_peer {
	ast_mutex_t lock;
	int sockfd;
	struct ast_sockaddr addr;
	struct timeval txtrunktime;		/*!< Transmit trunktime */
	struct timeval rxtrunktime;		/*!< Receive trunktime */
	struct timeval lasttxtime;		/*!< Last transmitted trunktime */
	struct timeval trunkact;		/*!< Last trunk activity */
	unsigned int lastsent;			/*!< Last sent time */
	/* Trunk data and length */
	unsigned char *trunkdata;
	unsigned int trunkdatalen;
	unsigned int trunkdataalloc;
	int trunkmaxmtu;
	int trunkerror;
	int calls;
	AST_LIST_ENTRY(iax2_trunk_peer) list;
};

static AST_LIST_HEAD_STATIC(tpeers, iax2_trunk_peer);

enum iax_reg_state {
	REG_STATE_UNREGISTERED = 0,
	REG_STATE_REGSENT,
	REG_STATE_AUTHSENT,
	REG_STATE_REGISTERED,
	REG_STATE_REJECTED,
	REG_STATE_TIMEOUT,
	REG_STATE_NOAUTH
};

enum iax_transfer_state {
	TRANSFER_NONE = 0,
	TRANSFER_BEGIN,
	TRANSFER_READY,
	TRANSFER_RELEASED,
	TRANSFER_PASSTHROUGH,
	TRANSFER_MBEGIN,
	TRANSFER_MREADY,
	TRANSFER_MRELEASED,
	TRANSFER_MPASSTHROUGH,
	TRANSFER_MEDIA,
	TRANSFER_MEDIAPASS
};

struct iax2_registry {
	struct ast_sockaddr addr;		/*!< Who we connect to for registration purposes */
	char username[80];
	char secret[80];			/*!< Password or key name in []'s */
	int expire;				/*!< Sched ID of expiration */
	int refresh;				/*!< How often to refresh */
	enum iax_reg_state regstate;
	int messages;				/*!< Message count, low 8 bits = new, high 8 bits = old */
	int callno;				/*!< Associated call number if applicable */
	struct ast_sockaddr us;			/*!< Who the server thinks we are */
	struct ast_dnsmgr_entry *dnsmgr;	/*!< DNS refresh manager */
	AST_LIST_ENTRY(iax2_registry) entry;
	int port;
	char hostname[];
};

static AST_LIST_HEAD_STATIC(registrations, iax2_registry);

/* Don't retry more frequently than every 10 ms, or less frequently than every 5 seconds */
#define MIN_RETRY_TIME		100
#define MAX_RETRY_TIME		10000

#define MAX_JITTER_BUFFER	50
#define MIN_JITTER_BUFFER	10

#define DEFAULT_TRUNKDATA	640 * 10	/*!< 40ms, uncompressed linear * 10 channels */

#define MAX_TIMESTAMP_SKEW	160		/*!< maximum difference between actual and predicted ts for sending */

/* If consecutive voice frame timestamps jump by more than this many milliseconds, then jitter buffer will resync */
#define TS_GAP_FOR_JB_RESYNC	5000

/* used for first_iax_message and last_iax_message.  If this bit is set it was TX, else RX */
#define MARK_IAX_SUBCLASS_TX	0x8000

static int iaxthreadcount = DEFAULT_THREAD_COUNT;
static int iaxmaxthreadcount = DEFAULT_MAX_THREAD_COUNT;
static int iaxdynamicthreadcount = 0;
static int iaxdynamicthreadnum = 0;
static int iaxactivethreadcount = 0;

struct iax_rr {
	int jitter;
	int losspct;
	int losscnt;
	int packets;
	int delay;
	int dropped;
	int ooo;
};

struct iax2_pvt_ref;

/* We use the high order bit as the validated flag, and the lower 15 as the
 * actual call number */
typedef uint16_t callno_entry;

struct chan_iax2_pvt {
	/*! Socket to send/receive on for this call */
	int sockfd;
	/*! ast_callid bound to dialog */
	ast_callid callid;
	/*! Last received voice format */
	iax2_format voiceformat;
	/*! Last received video format */
	iax2_format videoformat;
	/*! Last sent voice format */
	iax2_format svoiceformat;
	/*! Last sent video format */
	iax2_format svideoformat;
	/*! What we are capable of sending */
	iax2_format capability;
	/*! Last received timestamp */
	unsigned int last;
	/*! Last sent timestamp - never send the same timestamp twice in a single call */
	unsigned int lastsent;
	/*! Timestamp of the last video frame sent */
	unsigned int lastvsent;
	/*! Next outgoing timestamp if everything is good */
	unsigned int nextpred;
	/*! iax frame subclass that began iax2_pvt entry. 0x8000 bit is set on TX */
	int first_iax_message;
	/*! Last iax frame subclass sent or received for a iax2_pvt. 0x8000 bit is set on TX */
	int last_iax_message;
	/*! True if the last voice we transmitted was not silence/CNG */
	unsigned int notsilenttx:1;
	/*! Ping time */
	unsigned int pingtime;
	/*! Max time for initial response */
	int maxtime;
	/*! Peer Address */
	struct ast_sockaddr addr;
	/*! Actual used codec preferences */
	struct iax2_codec_pref prefs;
	/*! Requested codec preferences */
	struct iax2_codec_pref rprefs;
	/*! Our call number */
	unsigned short callno;
	/*! Our callno_entry entry */
	callno_entry callno_entry;
	/*! Peer callno */
	unsigned short peercallno;
	/*! Negotiated format, this is only used to remember what format was
	    chosen for an unauthenticated call so that the channel can get
	    created later using the right format */
	iax2_format chosenformat;
	/*! Peer selected format */
	iax2_format peerformat;
	/*! Peer capability */
	iax2_format peercapability;
	/*! timeval that we base our transmission on */
	struct timeval offset;
	/*! timeval that we base our delivery on */
	struct timeval rxcore;
	/*! The jitterbuffer */
	jitterbuf *jb;
	/*! active jb read scheduler id */
	int jbid;
	/*! LAG */
	int lag;
	/*! Error, as discovered by the manager */
	int error;
	/*! Owner if we have one */
	struct ast_channel *owner;
	/*! What's our state? */
	struct ast_flags state;
	/*! Expiry (optional) */
	int expiry;
	/*! Next outgoing sequence number */
	unsigned char oseqno;
	/*! Next sequence number they have not yet acknowledged */
	unsigned char rseqno;
	/*! Next incoming sequence number */
	unsigned char iseqno;
	/*! Last incoming sequence number we have acknowledged */
	unsigned char aseqno;

	AST_DECLARE_STRING_FIELDS(
		/*! Peer name */
		AST_STRING_FIELD(peer);
		/*! Default Context */
		AST_STRING_FIELD(context);
		/*! Caller ID if available */
		AST_STRING_FIELD(cid_num);
		AST_STRING_FIELD(cid_name);
		/*! Hidden Caller ID (i.e. ANI) if appropriate */
		AST_STRING_FIELD(ani);
		/*! DNID */
		AST_STRING_FIELD(dnid);
		/*! RDNIS */
		AST_STRING_FIELD(rdnis);
		/*! Requested Extension */
		AST_STRING_FIELD(exten);
		/*! Expected Username */
		AST_STRING_FIELD(username);
		/*! Expected Secret */
		AST_STRING_FIELD(secret);
		/*! MD5 challenge */
		AST_STRING_FIELD(challenge);
		/*! Public keys permitted keys for incoming authentication */
		AST_STRING_FIELD(inkeys);
		/*! Private key for outgoing authentication */
		AST_STRING_FIELD(outkey);
		/*! Preferred language */
		AST_STRING_FIELD(language);
		/*! Hostname/peername for naming purposes */
		AST_STRING_FIELD(host);

		AST_STRING_FIELD(dproot);
		AST_STRING_FIELD(accountcode);
		AST_STRING_FIELD(mohinterpret);
		AST_STRING_FIELD(mohsuggest);
		/*! received OSP token */
		AST_STRING_FIELD(osptoken);
		/*! Default parkinglot */
		AST_STRING_FIELD(parkinglot);
	);
	/*! AUTHREJ all AUTHREP frames */
	int authrej;
	/*! permitted authentication methods */
	int authmethods;
	/*! permitted encryption methods */
	int encmethods;
	/*! Encryption AES-128 Key */
	ast_aes_encrypt_key ecx;
	/*! Decryption AES-128 Key corresponding to ecx */
	ast_aes_decrypt_key mydcx;
	/*! Decryption AES-128 Key used to decrypt peer frames */
	ast_aes_decrypt_key dcx;
	/*! scheduler id associated with iax_key_rotate
	 * for encrypted calls*/
	int keyrotateid;
	/*! 32 bytes of semi-random data */
	unsigned char semirand[32];
	/*! Associated registry */
	struct iax2_registry *reg;
	/*! Associated peer for poking */
	struct iax2_peer *peerpoke;
	/*! IAX_ flags */
	uint64_t flags;
	int adsi;

	/*! Transferring status */
	enum iax_transfer_state transferring;
	/*! Transfer identifier */
	int transferid;
	/*! Who we are IAX transferring to */
	struct ast_sockaddr transfer;
	/*! What's the new call number for the transfer */
	unsigned short transfercallno;
	/*! Transfer encrypt AES-128 Key */
	ast_aes_encrypt_key tdcx;

	/*! Status of knowledge of peer ADSI capability */
	int peeradsicpe;

	/*! Callno of native bridge peer. (Valid if nonzero) */
	unsigned short bridgecallno;

	int pingid;			/*!< Transmit PING request */
	int lagid;			/*!< Retransmit lag request */
	int autoid;			/*!< Auto hangup for Dialplan requestor */
	int authid;			/*!< Authentication rejection ID */
	int authfail;			/*!< Reason to report failure */
	int initid;			/*!< Initial peer auto-congest ID (based on qualified peers) */
	int calling_ton;
	int calling_tns;
	int calling_pres;
	int amaflags;
	AST_LIST_HEAD_NOLOCK(, iax2_dpcache) dpentries;
	/*! variables inherited from the user definition */
	struct ast_variable *vars;
	/*! variables transmitted in a NEW packet */
	struct ast_variable *iaxvars;
	/*! last received remote rr */
	struct iax_rr remote_rr;
	/*! Current base time: (just for stats) */
	int min;
	/*! Dropped frame count: (just for stats) */
	int frames_dropped;
	/*! received frame count: (just for stats) */
	int frames_received;
	/*! Destroying this call initiated. */
	int destroy_initiated;
	/*! num bytes used for calltoken ie, even an empty ie should contain 2 */
	unsigned char calltoken_ie_len;
	/*! hold all signaling frames from the pbx thread until we have a destination callno */
	char hold_signaling;
	/*! frame queue for signaling frames from pbx thread waiting for destination callno */
	AST_LIST_HEAD_NOLOCK(signaling_queue, signaling_queue_entry) signaling_queue;
};

struct signaling_queue_entry {
	struct ast_frame f;
	AST_LIST_ENTRY(signaling_queue_entry) next;
};

enum callno_type {
	CALLNO_TYPE_NORMAL,
	CALLNO_TYPE_TRUNK,
};

#define PTR_TO_CALLNO_ENTRY(a) ((uint16_t)(unsigned long)(a))
#define CALLNO_ENTRY_TO_PTR(a) ((void *)(unsigned long)(a))

#define CALLNO_ENTRY_SET_VALIDATED(a) ((a) |= 0x8000)
#define CALLNO_ENTRY_IS_VALIDATED(a)  ((a) & 0x8000)
#define CALLNO_ENTRY_GET_CALLNO(a)    ((a) & 0x7FFF)

struct call_number_pool {
	size_t capacity;
	size_t available;
	callno_entry numbers[IAX_MAX_CALLS / 2 + 1];
};

AST_MUTEX_DEFINE_STATIC(callno_pool_lock);

/*! table of available call numbers */
static struct call_number_pool callno_pool;

/*! table of available trunk call numbers */
static struct call_number_pool callno_pool_trunk;

/*!
 * \brief a list of frames that may need to be retransmitted
 *
 * \note The contents of this list do not need to be explicitly destroyed
 * on module unload.  This is because all active calls are destroyed, and
 * all frames in this queue will get destroyed as a part of that process.
 *
 * \note Contents protected by the iaxsl[] locks
 */
static AST_LIST_HEAD_NOLOCK(, iax_frame) frame_queue[IAX_MAX_CALLS];

static struct ast_taskprocessor *transmit_processor;

static int randomcalltokendata;

static time_t max_calltoken_delay = 10;

/*!
 * This module will get much higher performance when doing a lot of
 * user and peer lookups if the number of buckets is increased from 1.
 * However, to maintain old behavior for Asterisk 1.4, these are set to
 * 1 by default.  When using multiple buckets, search order through these
 * containers is considered random, so you will not be able to depend on
 * the order the entires are specified in iax.conf for matching order. */
#ifdef LOW_MEMORY
#define MAX_PEER_BUCKETS 17
#else
#define MAX_PEER_BUCKETS 563
#endif
static struct ao2_container *peers;

#define MAX_USER_BUCKETS MAX_PEER_BUCKETS
static struct ao2_container *users;

/*! Table containing peercnt objects for every ip address consuming a callno */
static struct ao2_container *peercnts;

/*! Table containing custom callno limit rules for a range of ip addresses. */
static struct ao2_container *callno_limits;

/*! Table containing ip addresses not requiring calltoken validation */
static struct ao2_container *calltoken_ignores;

static uint16_t DEFAULT_MAXCALLNO_LIMIT = 2048;

static uint16_t DEFAULT_MAXCALLNO_LIMIT_NONVAL = 8192;

static uint16_t global_maxcallno;

/*! Total num of call numbers allowed to be allocated without calltoken validation */
static uint16_t global_maxcallno_nonval;

static uint16_t total_nonval_callno_used = 0;

/*! peer connection private, keeps track of all the call numbers
 *  consumed by a single ip address */
struct peercnt {
	/*! ip address consuming call numbers */
	struct ast_sockaddr addr;
	/*! Number of call numbers currently used by this ip address */
	uint16_t cur;
	/*! Max call numbers allowed for this ip address */
	uint16_t limit;
	/*! Specifies whether limit is set by a registration or not, if so normal
	 *  limit setting rules do not apply to this address. */
	unsigned char reg;
};

/*! used by both callno_limits and calltoken_ignores containers */
struct addr_range {
	/*! ip address range for custom callno limit rule */
	struct ast_ha ha;
	/*! callno limit for this ip address range, only used in callno_limits container */
	uint16_t limit;
	/*! delete me marker for reloads */
	unsigned char delme;
};

enum {
	/*! Extension exists */
	CACHE_FLAG_EXISTS      = (1 << 0),
	/*! Extension is nonexistent */
	CACHE_FLAG_NONEXISTENT = (1 << 1),
	/*! Extension can exist */
	CACHE_FLAG_CANEXIST    = (1 << 2),
	/*! Waiting to hear back response */
	CACHE_FLAG_PENDING     = (1 << 3),
	/*! Timed out */
	CACHE_FLAG_TIMEOUT     = (1 << 4),
	/*! Request transmitted */
	CACHE_FLAG_TRANSMITTED = (1 << 5),
	/*! Timeout */
	CACHE_FLAG_UNKNOWN     = (1 << 6),
	/*! Matchmore */
	CACHE_FLAG_MATCHMORE   = (1 << 7),
};

struct iax2_dpcache {
	char peercontext[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	struct timeval orig;
	struct timeval expiry;
	int flags;
	unsigned short callno;
	int waiters[256];
	AST_LIST_ENTRY(iax2_dpcache) cache_list;
	AST_LIST_ENTRY(iax2_dpcache) peer_list;
};

static AST_LIST_HEAD_STATIC(dpcache, iax2_dpcache);

static void reg_source_db(struct iax2_peer *p);
static struct iax2_peer *realtime_peer(const char *peername, struct ast_sockaddr *addr);
static struct iax2_user *realtime_user(const char *username, struct ast_sockaddr *addr);

static int ast_cli_netstats(struct mansession *s, int fd, int limit_fmt);
static char *complete_iax2_peers(const char *line, const char *word, int pos, int state, uint64_t flags);
static char *complete_iax2_unregister(const char *line, const char *word, int pos, int state);

enum iax2_thread_iostate {
	IAX_IOSTATE_IDLE,
	IAX_IOSTATE_READY,
	IAX_IOSTATE_PROCESSING,
	IAX_IOSTATE_SCHEDREADY,
};

enum iax2_thread_type {
	IAX_THREAD_TYPE_POOL,
	IAX_THREAD_TYPE_DYNAMIC,
};

struct iax2_pkt_buf {
	AST_LIST_ENTRY(iax2_pkt_buf) entry;
	size_t len;
	unsigned char buf[1];
};

struct iax2_thread {
	AST_LIST_ENTRY(iax2_thread) list;
	enum iax2_thread_type type;
	enum iax2_thread_iostate iostate;
#ifdef SCHED_MULTITHREADED
	void (*schedfunc)(const void *);
	const void *scheddata;
#endif
#ifdef DEBUG_SCHED_MULTITHREAD
	char curfunc[80];
#endif
	int actions;
	pthread_t threadid;
	int threadnum;
	struct ast_sockaddr ioaddr;
	unsigned char readbuf[4096];
	unsigned char *buf;
	ssize_t buf_len;
	size_t buf_size;
	int iofd;
	time_t checktime;
	ast_mutex_t lock;
	ast_cond_t cond;
	ast_mutex_t init_lock;
	ast_cond_t init_cond;
	/*! if this thread is processing a full frame,
	  some information about that frame will be stored
	  here, so we can avoid dispatching any more full
	  frames for that callno to other threads */
	struct {
		unsigned short callno;
		struct ast_sockaddr addr;
		unsigned char type;
		unsigned char csub;
	} ffinfo;
	/*! Queued up full frames for processing.  If more full frames arrive for
	 *  a call which this thread is already processing a full frame for, they
	 *  are queued up here. */
	AST_LIST_HEAD_NOLOCK(, iax2_pkt_buf) full_frames;
	unsigned char stop;
};

/* Thread lists */
static AST_LIST_HEAD_STATIC(idle_list, iax2_thread);
static AST_LIST_HEAD_STATIC(active_list, iax2_thread);
static AST_LIST_HEAD_STATIC(dynamic_list, iax2_thread);

static void *iax2_process_thread(void *data);
static void iax2_destroy(int callno);

static void signal_condition(ast_mutex_t *lock, ast_cond_t *cond)
{
	ast_mutex_lock(lock);
	ast_cond_signal(cond);
	ast_mutex_unlock(lock);
}

/*!
 * \brief an array of iax2 pvt structures
 *
 * The container for active chan_iax2_pvt structures is implemented as an
 * array for extremely quick direct access to the correct pvt structure
 * based on the local call number.  The local call number is used as the
 * index into the array where the associated pvt structure is stored.
 */
static struct chan_iax2_pvt *iaxs[IAX_MAX_CALLS];

static ast_callid iax_pvt_callid_get(int callno)
{
	return iaxs[callno]->callid;
}

static void iax_pvt_callid_set(int callno, ast_callid callid)
{
	iaxs[callno]->callid = callid;
}

static void iax_pvt_callid_new(int callno)
{
	ast_callid callid = ast_create_callid();
	char buffer[AST_CALLID_BUFFER_LENGTH];
	ast_callid_strnprint(buffer, sizeof(buffer), callid);
	iax_pvt_callid_set(callno, callid);
}

/*!
 * \brief Another container of iax2_pvt structures
 *
 * Active IAX2 pvt structs are also stored in this container, if they are a part
 * of an active call where we know the remote side's call number.  The reason
 * for this is that incoming media frames do not contain our call number.  So,
 * instead of having to iterate the entire iaxs array, we use this container to
 * look up calls where the remote side is using a given call number.
 */
static struct ao2_container *iax_peercallno_pvts;

/*!
 * \brief chan_iax2_pvt structure locks
 *
 * These locks are used when accessing a pvt structure in the iaxs array.
 * The index used here is the same as used in the iaxs array.  It is the
 * local call number for the associated pvt struct.
 */
static ast_mutex_t iaxsl[ARRAY_LEN(iaxs)];

/*!
 *  * \brief Another container of iax2_pvt structures
 *
 *  Active IAX2 pvt stucts used during transfering a call are stored here.
 */
static struct ao2_container *iax_transfercallno_pvts;

/* Flag to use with trunk calls, keeping these calls high up.  It halves our effective use
   but keeps the division between trunked and non-trunked better. */
#define TRUNK_CALL_START	(IAX_MAX_CALLS / 2)

/* Debug routines... */
static struct ast_sockaddr debugaddr;

static void iax_outputframe(struct iax_frame *f, struct ast_iax2_full_hdr *fhi, int rx, struct ast_sockaddr *addr, int datalen)
{
	if (iaxdebug ||
	    (addr && !ast_sockaddr_isnull(&debugaddr) &&
		(!ast_sockaddr_port(&debugaddr) ||
		  ast_sockaddr_port(&debugaddr) == ast_sockaddr_port(addr)) &&
		  !ast_sockaddr_cmp_addr(&debugaddr, addr))) {

		if (iaxdebug) {
			iax_showframe(f, fhi, rx, addr, datalen);
		} else {
			iaxdebug = 1;
			iax_showframe(f, fhi, rx, addr, datalen);
			iaxdebug = 0;
		}
	}
}

static void iax_debug_output(const char *data)
{
	if (iaxdebug)
		ast_verbose("%s", data);
}

static void iax_error_output(const char *data)
{
	ast_log(LOG_WARNING, "%s", data);
}

static void __attribute__((format(printf, 1, 2))) jb_error_output(const char *fmt, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ast_log(LOG_ERROR, "%s", buf);
}

static void __attribute__((format(printf, 1, 2))) jb_warning_output(const char *fmt, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ast_log(LOG_WARNING, "%s", buf);
}

static void __attribute__((format(printf, 1, 2))) jb_debug_output(const char *fmt, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ast_verbose("%s", buf);
}

static int expire_registry(const void *data);
static int iax2_answer(struct ast_channel *c);
static int iax2_call(struct ast_channel *c, const char *dest, int timeout);
static int iax2_devicestate(const char *data);
static int iax2_digit_begin(struct ast_channel *c, char digit);
static int iax2_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int iax2_do_register(struct iax2_registry *reg);
static int iax2_fixup(struct ast_channel *oldchannel, struct ast_channel *newchan);
static int iax2_hangup(struct ast_channel *c);
static int iax2_indicate(struct ast_channel *c, int condition, const void *data, size_t datalen);
static int iax2_poke_peer(struct iax2_peer *peer, int heldcall);
static int iax2_provision(struct ast_sockaddr *end, int sockfd, const char *dest, const char *template, int force);
static int iax2_send(struct chan_iax2_pvt *pvt, struct ast_frame *f, unsigned int ts, int seqno, int now, int transfer, int final);
static int iax2_sendhtml(struct ast_channel *c, int subclass, const char *data, int datalen);
static int iax2_sendimage(struct ast_channel *c, struct ast_frame *img);
static int iax2_sendtext(struct ast_channel *c, const char *text);
static int iax2_setoption(struct ast_channel *c, int option, void *data, int datalen);
static int iax2_queryoption(struct ast_channel *c, int option, void *data, int *datalen);
static int iax2_transfer(struct ast_channel *c, const char *dest);
static int iax2_write(struct ast_channel *c, struct ast_frame *f);
static int iax2_sched_add(struct ast_sched_context *sched, int when, ast_sched_cb callback, const void *data);

static int send_trunk(struct iax2_trunk_peer *tpeer, struct timeval *now);
static int send_command(struct chan_iax2_pvt *, char, int, unsigned int, const unsigned char *, int, int);
static int send_command_final(struct chan_iax2_pvt *, char, int, unsigned int, const unsigned char *, int, int);
static int send_command_immediate(struct chan_iax2_pvt *, char, int, unsigned int, const unsigned char *, int, int);
static int send_command_locked(unsigned short callno, char, int, unsigned int, const unsigned char *, int, int);
static int send_command_transfer(struct chan_iax2_pvt *, char, int, unsigned int, const unsigned char *, int);
static struct ast_channel *iax2_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static struct ast_frame *iax2_read(struct ast_channel *c);
static struct iax2_peer *build_peer(const char *name, struct ast_variable *v, struct ast_variable *alt, int temponly);
static struct iax2_user *build_user(const char *name, struct ast_variable *v, struct ast_variable *alt, int temponly);
static void realtime_update_peer(const char *peername, struct ast_sockaddr *sockaddr, time_t regtime);
static void *iax2_dup_variable_datastore(void *);
static void prune_peers(void);
static void prune_users(void);
static void iax2_free_variable_datastore(void *);

static int acf_channel_read(struct ast_channel *chan, const char *funcname, char *preparse, char *buf, size_t buflen);
static int decode_frame(ast_aes_decrypt_key *dcx, struct ast_iax2_full_hdr *fh, struct ast_frame *f, int *datalen);
static int encrypt_frame(ast_aes_encrypt_key *ecx, struct ast_iax2_full_hdr *fh, unsigned char *poo, int *datalen);
static void build_ecx_key(const unsigned char *digest, struct chan_iax2_pvt *pvt);
static void build_rand_pad(unsigned char *buf, ssize_t len);
static int get_unused_callno(enum callno_type type, int validated, callno_entry *entry);
static int replace_callno(const void *obj);
static void sched_delay_remove(struct ast_sockaddr *addr, callno_entry entry);
static void network_change_stasis_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message);
static void acl_change_stasis_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message);

static struct ast_channel_tech iax2_tech = {
	.type = "IAX2",
	.description = tdesc,
	.properties = AST_CHAN_TP_WANTSJITTER,
	.requester = iax2_request,
	.devicestate = iax2_devicestate,
	.send_digit_begin = iax2_digit_begin,
	.send_digit_end = iax2_digit_end,
	.send_text = iax2_sendtext,
	.send_image = iax2_sendimage,
	.send_html = iax2_sendhtml,
	.call = iax2_call,
	.hangup = iax2_hangup,
	.answer = iax2_answer,
	.read = iax2_read,
	.write = iax2_write,
	.write_video = iax2_write,
	.indicate = iax2_indicate,
	.setoption = iax2_setoption,
	.queryoption = iax2_queryoption,
	.transfer = iax2_transfer,
	.fixup = iax2_fixup,
	.func_channel_read = acf_channel_read,
};

/*!
 * \internal
 * \brief Obtain the owner channel lock if the owner exists.
 *
 * \param callno IAX2 call id.
 *
 * \note Assumes the iaxsl[callno] lock is already obtained.
 *
 * \note
 * IMPORTANT NOTE!!!  Any time this function is used, even if
 * iaxs[callno] was valid before calling it, it may no longer be
 * valid after calling it.  This function may unlock and lock
 * the mutex associated with this callno, meaning that another
 * thread may grab it and destroy the call.
 *
 * \return Nothing
 */
static void iax2_lock_owner(int callno)
{
	for (;;) {
		if (!iaxs[callno] || !iaxs[callno]->owner) {
			/* There is no owner lock to get. */
			break;
		}
		if (!ast_channel_trylock(iaxs[callno]->owner)) {
			/* We got the lock */
			break;
		}
		/* Avoid deadlock by pausing and trying again */
		DEADLOCK_AVOIDANCE(&iaxsl[callno]);
	}
}

/*!
 * \internal
 * \brief Check if a control subtype is allowed on the wire.
 *
 * \param subtype Control frame subtype to check if allowed to/from the wire.
 *
 * \retval non-zero if allowed.
 */
static int iax2_is_control_frame_allowed(int subtype)
{
	enum ast_control_frame_type control = subtype;
	int is_allowed;

	/*
	 * Note: If we compare the enumeration type, which does not have any
	 * negative constants, the compiler may optimize this code away.
	 * Therefore, we must perform an integer comparison here.
	 */
	if (subtype == -1) {
		return -1;
	}

	/* Default to not allowing control frames to pass. */
	is_allowed = 0;

	/*
	 * The switch default is not present in order to take advantage
	 * of the compiler complaining of a missing enum case.
	 */
	switch (control) {
	/*
	 * These control frames make sense to send/receive across the link.
	 */
	case AST_CONTROL_HANGUP:
	case AST_CONTROL_RING:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_ANSWER:
	case AST_CONTROL_BUSY:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_HOLD:
	case AST_CONTROL_UNHOLD:
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_CONNECTED_LINE:
	case AST_CONTROL_REDIRECTING:
	case AST_CONTROL_T38_PARAMETERS:
	case AST_CONTROL_AOC:
	case AST_CONTROL_INCOMPLETE:
	case AST_CONTROL_MCID:
		is_allowed = -1;
		break;

	/*
	 * These control frames do not make sense to send/receive across the link.
	 */
	case _XXX_AST_CONTROL_T38:
		/* The control value is deprecated in favor of AST_CONTROL_T38_PARAMETERS. */
	case AST_CONTROL_SRCUPDATE:
		/* Across an IAX link the source is still the same. */
	case AST_CONTROL_TRANSFER:
		/* A success/fail status report from calling ast_transfer() on this machine. */
	case AST_CONTROL_CC:
		/* The payload contains pointers that are valid for the sending machine only. */
	case AST_CONTROL_SRCCHANGE:
		/* Across an IAX link the source is still the same. */
	case AST_CONTROL_READ_ACTION:
		/* The action can only be done by the sending machine. */
	case AST_CONTROL_END_OF_Q:
		/* This frame would cause the call to unexpectedly hangup. */
	case AST_CONTROL_UPDATE_RTP_PEER:
		/* Only meaningful across a bridge on this machine for direct-media exchange. */
	case AST_CONTROL_PVT_CAUSE_CODE:
		/* Intended only for the sending machine's local channel structure. */
	case AST_CONTROL_MASQUERADE_NOTIFY:
		/* Intended only for masquerades when calling ast_indicate_data(). */
	case AST_CONTROL_STREAM_STOP:
	case AST_CONTROL_STREAM_SUSPEND:
	case AST_CONTROL_STREAM_RESTART:
	case AST_CONTROL_STREAM_REVERSE:
	case AST_CONTROL_STREAM_FORWARD:
		/* None of these playback stream control frames should go across the link. */
	case AST_CONTROL_RECORD_CANCEL:
	case AST_CONTROL_RECORD_STOP:
	case AST_CONTROL_RECORD_SUSPEND:
	case AST_CONTROL_RECORD_MUTE:
		/* None of these media recording control frames should go across the link. */
		break;
	}
	return is_allowed;
}

static void mwi_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg)
{
	/* The MWI subscriptions exist just so the core knows we care about those
	 * mailboxes.  However, we just grab the events out of the cache when it
	 * is time to send MWI, since it is only sent with a REGACK. */
}

static void network_change_stasis_subscribe(void)
{
	if (!network_change_sub) {
		network_change_sub = stasis_subscribe(ast_system_topic(),
			network_change_stasis_cb, NULL);
	}
}

static void network_change_stasis_unsubscribe(void)
{
	network_change_sub = stasis_unsubscribe_and_join(network_change_sub);
}

static void acl_change_stasis_subscribe(void)
{
	if (!acl_change_sub) {
		acl_change_sub = stasis_subscribe(ast_security_topic(),
			acl_change_stasis_cb, NULL);
	}
}

static void acl_change_stasis_unsubscribe(void)
{
	acl_change_sub = stasis_unsubscribe_and_join(acl_change_sub);
}

static int network_change_sched_cb(const void *data)
{
	struct iax2_registry *reg;
	network_change_sched_id = -1;
	AST_LIST_LOCK(&registrations);
	AST_LIST_TRAVERSE(&registrations, reg, entry) {
		iax2_do_register(reg);
	}
	AST_LIST_UNLOCK(&registrations);

	return 0;
}

static void network_change_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	/* This callback is only concerned with network change messages from the system topic. */
	if (stasis_message_type(message) != ast_network_change_type()) {
		return;
	}

	ast_verb(1, "IAX, got a network change message, renewing all IAX registrations.\n");
	if (network_change_sched_id == -1) {
		network_change_sched_id = iax2_sched_add(sched, 1000, network_change_sched_cb, NULL);
	}
}

static void acl_change_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	if (stasis_message_type(message) != ast_named_acl_change_type()) {
		return;
	}

	ast_log(LOG_NOTICE, "Reloading chan_iax2 in response to ACL change event.\n");
	reload_config(1);
}

static const struct ast_datastore_info iax2_variable_datastore_info = {
	.type = "IAX2_VARIABLE",
	.duplicate = iax2_dup_variable_datastore,
	.destroy = iax2_free_variable_datastore,
};

static void *iax2_dup_variable_datastore(void *old)
{
	AST_LIST_HEAD(, ast_var_t) *oldlist = old, *newlist;
	struct ast_var_t *oldvar, *newvar;

	newlist = ast_calloc(sizeof(*newlist), 1);
	if (!newlist) {
		ast_log(LOG_ERROR, "Unable to duplicate iax2 variables\n");
		return NULL;
	}

	AST_LIST_HEAD_INIT(newlist);
	AST_LIST_LOCK(oldlist);
	AST_LIST_TRAVERSE(oldlist, oldvar, entries) {
		newvar = ast_var_assign(ast_var_name(oldvar), ast_var_value(oldvar));
		if (newvar)
			AST_LIST_INSERT_TAIL(newlist, newvar, entries);
		else
			ast_log(LOG_ERROR, "Unable to duplicate iax2 variable '%s'\n", ast_var_name(oldvar));
	}
	AST_LIST_UNLOCK(oldlist);
	return newlist;
}

static void iax2_free_variable_datastore(void *old)
{
	AST_LIST_HEAD(, ast_var_t) *oldlist = old;
	struct ast_var_t *oldvar;

	AST_LIST_LOCK(oldlist);
	while ((oldvar = AST_LIST_REMOVE_HEAD(oldlist, entries))) {
		ast_free(oldvar);
	}
	AST_LIST_UNLOCK(oldlist);
	AST_LIST_HEAD_DESTROY(oldlist);
	ast_free(oldlist);
}


/* WARNING: insert_idle_thread should only ever be called within the
 * context of an iax2_process_thread() thread.
 */
static void insert_idle_thread(struct iax2_thread *thread)
{
	if (thread->type == IAX_THREAD_TYPE_DYNAMIC) {
		AST_LIST_LOCK(&dynamic_list);
		AST_LIST_INSERT_TAIL(&dynamic_list, thread, list);
		AST_LIST_UNLOCK(&dynamic_list);
	} else {
		AST_LIST_LOCK(&idle_list);
		AST_LIST_INSERT_TAIL(&idle_list, thread, list);
		AST_LIST_UNLOCK(&idle_list);
	}

	return;
}

static struct iax2_thread *find_idle_thread(void)
{
	struct iax2_thread *thread = NULL;

	/* Pop the head of the idle list off */
	AST_LIST_LOCK(&idle_list);
	thread = AST_LIST_REMOVE_HEAD(&idle_list, list);
	AST_LIST_UNLOCK(&idle_list);

	/* If we popped a thread off the idle list, just return it */
	if (thread) {
		memset(&thread->ffinfo, 0, sizeof(thread->ffinfo));
		return thread;
	}

	/* Pop the head of the dynamic list off */
	AST_LIST_LOCK(&dynamic_list);
	thread = AST_LIST_REMOVE_HEAD(&dynamic_list, list);
	AST_LIST_UNLOCK(&dynamic_list);

	/* If we popped a thread off the dynamic list, just return it */
	if (thread) {
		memset(&thread->ffinfo, 0, sizeof(thread->ffinfo));
		return thread;
	}

	/* If we can't create a new dynamic thread for any reason, return no thread at all */
	if (iaxdynamicthreadcount >= iaxmaxthreadcount || !(thread = ast_calloc(1, sizeof(*thread))))
		return NULL;

	/* Set default values */
	ast_atomic_fetchadd_int(&iaxdynamicthreadcount, 1);
	thread->threadnum = ast_atomic_fetchadd_int(&iaxdynamicthreadnum, 1);
	thread->type = IAX_THREAD_TYPE_DYNAMIC;

	/* Initialize lock and condition */
	ast_mutex_init(&thread->lock);
	ast_cond_init(&thread->cond, NULL);
	ast_mutex_init(&thread->init_lock);
	ast_cond_init(&thread->init_cond, NULL);
	ast_mutex_lock(&thread->init_lock);

	/* Create thread and send it on it's way */
	if (ast_pthread_create_background(&thread->threadid, NULL, iax2_process_thread, thread)) {
		ast_cond_destroy(&thread->cond);
		ast_mutex_destroy(&thread->lock);
		ast_mutex_unlock(&thread->init_lock);
		ast_cond_destroy(&thread->init_cond);
		ast_mutex_destroy(&thread->init_lock);
		ast_free(thread);
		return NULL;
	}

	/* this thread is not processing a full frame (since it is idle),
	   so ensure that the field for the full frame call number is empty */
	memset(&thread->ffinfo, 0, sizeof(thread->ffinfo));

	/* Wait for the thread to be ready before returning it to the caller */
	ast_cond_wait(&thread->init_cond, &thread->init_lock);

	/* Done with init_lock */
	ast_mutex_unlock(&thread->init_lock);

	return thread;
}

#ifdef SCHED_MULTITHREADED
static int __schedule_action(void (*func)(const void *data), const void *data, const char *funcname)
{
	struct iax2_thread *thread;
	static time_t lasterror;
	time_t t;

	thread = find_idle_thread();
	if (thread != NULL) {
		thread->schedfunc = func;
		thread->scheddata = data;
		thread->iostate = IAX_IOSTATE_SCHEDREADY;
#ifdef DEBUG_SCHED_MULTITHREAD
		ast_copy_string(thread->curfunc, funcname, sizeof(thread->curfunc));
#endif
		signal_condition(&thread->lock, &thread->cond);
		return 0;
	}
	time(&t);
	if (t != lasterror) {
		lasterror = t;
		ast_debug(1, "Out of idle IAX2 threads for scheduling! (%s)\n", funcname);
	}

	return -1;
}
#define schedule_action(func, data) __schedule_action(func, data, __PRETTY_FUNCTION__)
#endif

static int iax2_sched_replace(int id, struct ast_sched_context *con, int when,
		ast_sched_cb callback, const void *data)
{
	return ast_sched_replace(id, con, when, callback, data);
}

static int iax2_sched_add(struct ast_sched_context *con, int when,
		ast_sched_cb callback, const void *data)
{
	return ast_sched_add(con, when, callback, data);
}

/*
 * \brief Acquire the iaxsl[callno] if call exists and not having ongoing hangup.
 * \param callno Call number to lock.
 * \return 0 If call disappeared or has ongoing hangup procedure. 1 If call found and mutex is locked.
 */
static int iax2_lock_callno_unless_destroyed(int callno)
{
	ast_mutex_lock(&iaxsl[callno]);

	/* We acquired the lock; but the call was already destroyed (we came after full hang up procedures)
	 * or destroy initiated (in middle of hang up procedure. */
	if (!iaxs[callno] || iaxs[callno]->destroy_initiated) {
		ast_debug(3, "I wanted to lock callno %d, but it is dead or going to die.\n", callno);
		ast_mutex_unlock(&iaxsl[callno]);
		return 0;
	}

	/* Lock acquired, and callno is alive and kicking. */
	return 1;
}

static int send_ping(const void *data);

static void __send_ping(const void *data)
{
	int callno = PTR_TO_CALLNO(data);

	if (iax2_lock_callno_unless_destroyed(callno) == 0) {
		ast_debug(3, "Hangup initiated on call %d, aborting __send_ping\n", callno);
		return;
	}

	/* callno is now locked. */
	if (iaxs[callno]->peercallno) {
		/* Send PING packet. */
		send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_PING, 0, NULL, 0, -1);

		/* Schedule sending next ping. */
		iaxs[callno]->pingid = iax2_sched_add(sched, ping_time * 1000, send_ping, data);
	}

	ast_mutex_unlock(&iaxsl[callno]);
}

static int send_ping(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__send_ping, data))
#endif
		__send_ping(data);

	return 0;
}

static void encmethods_to_str(int e, struct ast_str **buf)
{
	ast_str_set(buf, 0, "(");
	if (e & IAX_ENCRYPT_AES128) {
		ast_str_append(buf, 0, "aes128");
	}
	if (e & IAX_ENCRYPT_KEYROTATE) {
		ast_str_append(buf, 0, ",keyrotate");
	}
	if (ast_str_strlen(*buf) > 1) {
		ast_str_append(buf, 0, ")");
	} else {
		ast_str_set(buf, 0, "No");
	}
}

static int get_encrypt_methods(const char *s)
{
	int e;
	if (!strcasecmp(s, "aes128"))
		e = IAX_ENCRYPT_AES128 | IAX_ENCRYPT_KEYROTATE;
	else if (ast_true(s))
		e = IAX_ENCRYPT_AES128 | IAX_ENCRYPT_KEYROTATE;
	else
		e = 0;
	return e;
}

static int send_lagrq(const void *data);

static void __send_lagrq(const void *data)
{
	int callno = PTR_TO_CALLNO(data);

	if (iax2_lock_callno_unless_destroyed(callno) == 0) {
		ast_debug(3, "Hangup initiated on call %d, aborting __send_lagrq\n", callno);
		return;
	}

	/* callno is now locked. */
	if (iaxs[callno]->peercallno) {
		/* Send LAGRQ packet. */
		send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_LAGRQ, 0, NULL, 0, -1);

		/* Schedule sending next lagrq. */
		iaxs[callno]->lagid = iax2_sched_add(sched, lagrq_time * 1000, send_lagrq, data);
	}

	ast_mutex_unlock(&iaxsl[callno]);
}

static int send_lagrq(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__send_lagrq, data))
#endif
		__send_lagrq(data);
	return 0;
}

static unsigned char compress_subclass(iax2_format subclass)
{
	int x;
	int power=-1;
	/* If it's 64 or smaller, just return it */
	if (subclass < IAX_FLAG_SC_LOG)
		return subclass;
	/* Otherwise find its power */
	for (x = 0; x < IAX_MAX_SHIFT; x++) {
		if (subclass & (1LL << x)) {
			if (power > -1) {
				ast_log(LOG_WARNING, "Can't compress subclass %lld\n", (long long) subclass);
				return 0;
			} else
				power = x;
		}
	}
	return power | IAX_FLAG_SC_LOG;
}

static iax2_format uncompress_subclass(unsigned char csub)
{
	/* If the SC_LOG flag is set, return 2^csub otherwise csub */
	if (csub & IAX_FLAG_SC_LOG) {
		/* special case for 'compressed' -1 */
		if (csub == 0xff)
			return -1;
		else
			return 1LL << (csub & ~IAX_FLAG_SC_LOG & IAX_MAX_SHIFT);
	}
	else
		return csub;
}

static struct ast_format *codec_choose_from_prefs(struct iax2_codec_pref *pref, struct ast_format_cap *cap)
{
	int x;
	struct ast_format *found_format = NULL;

	for (x = 0; x < ARRAY_LEN(pref->order); ++x) {
		struct ast_format *pref_format;
		uint64_t pref_bitfield;

		pref_bitfield = iax2_codec_pref_order_value_to_format_bitfield(pref->order[x]);
		if (!pref_bitfield) {
			break;
		}

		pref_format = ast_format_compatibility_bitfield2format(pref_bitfield);
		if (!pref_format) {
			/* The bitfield is not associated with any format. */
			continue;
		}
		found_format = ast_format_cap_get_compatible_format(cap, pref_format);
		if (found_format) {
			break;
		}
	}

	if (found_format && (ast_format_get_type(found_format) == AST_MEDIA_TYPE_AUDIO)) {
		return found_format;
	}

	ast_debug(4, "Could not find preferred codec - Returning zero codec.\n");
	ao2_cleanup(found_format);
	return NULL;
}

static iax2_format iax2_codec_choose(struct iax2_codec_pref *pref, iax2_format formats)
{
	struct ast_format_cap *cap;
	struct ast_format *tmpfmt;
	iax2_format format = 0;

	if ((cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		iax2_format_compatibility_bitfield2cap(formats, cap);
		tmpfmt = codec_choose_from_prefs(pref, cap);
		if (!tmpfmt) {
			ao2_ref(cap, -1);
			return 0;
		}

		format = ast_format_compatibility_format2bitfield(tmpfmt);
		ao2_ref(tmpfmt, -1);
		ao2_ref(cap, -1);
	}

	return format;
}

const char *iax2_getformatname(iax2_format format)
{
	struct ast_format *tmpfmt;

	tmpfmt = ast_format_compatibility_bitfield2format(format);
	if (!tmpfmt) {
		return "Unknown";
	}

	return ast_format_get_name(tmpfmt);
}

static const char *iax2_getformatname_multiple(iax2_format format, struct ast_str **codec_buf)
{
	struct ast_format_cap *cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	if (!cap) {
		return "(Nothing)";
	}
	iax2_format_compatibility_bitfield2cap(format, cap);
	ast_format_cap_get_names(cap, codec_buf);
	ao2_ref(cap, -1);

	return ast_str_buffer(*codec_buf);
}

static int iax2_parse_allow_disallow(struct iax2_codec_pref *pref, iax2_format *formats, const char *list, int allowing)
{
	int res, i;
	struct ast_format_cap *cap;

	/* We want to add the formats to the cap in the preferred order */
	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap || iax2_codec_pref_to_cap(pref, cap)) {
		ao2_cleanup(cap);
		return 1;
	}

	res = ast_format_cap_update_by_allow_disallow(cap, list, allowing);

	/* Adjust formats bitfield and pref list to match. */
	*formats = iax2_format_compatibility_cap2bitfield(cap);
	iax2_codec_pref_remove_missing(pref, *formats);

	for (i = 0; i < ast_format_cap_count(cap); i++) {
		struct ast_format *fmt = ast_format_cap_get_format(cap, i);

		iax2_codec_pref_append(pref, fmt, ast_format_cap_get_format_framing(cap, fmt));
		ao2_ref(fmt, -1);
	}

	ao2_ref(cap, -1);

	return res;
}

static int iax2_data_add_codecs(struct ast_data *root, const char *node_name, iax2_format formats)
{
	int res;
	struct ast_format_cap *cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		return -1;
	}
	iax2_format_compatibility_bitfield2cap(formats, cap);
	res = ast_data_add_codecs(root, node_name, cap);
	ao2_ref(cap, -1);
	return res;
}

/*!
 * \note The only member of the peer passed here guaranteed to be set is the name field
 */
static int peer_hash_cb(const void *obj, const int flags)
{
	const struct iax2_peer *peer = obj;
	const char *name = obj;

	return ast_str_hash(flags & OBJ_KEY ? name : peer->name);
}

/*!
 * \note The only member of the peer passed here guaranteed to be set is the name field
 */
static int peer_cmp_cb(void *obj, void *arg, int flags)
{
	struct iax2_peer *peer = obj, *peer2 = arg;
	const char *name = arg;

	return !strcmp(peer->name, flags & OBJ_KEY ? name : peer2->name) ?
			CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \note The only member of the user passed here guaranteed to be set is the name field
 */
static int user_hash_cb(const void *obj, const int flags)
{
	const struct iax2_user *user = obj;
	const char *name = obj;

	return ast_str_hash(flags & OBJ_KEY ? name : user->name);
}

/*!
 * \note The only member of the user passed here guaranteed to be set is the name field
 */
static int user_cmp_cb(void *obj, void *arg, int flags)
{
	struct iax2_user *user = obj, *user2 = arg;
	const char *name = arg;

	return !strcmp(user->name, flags & OBJ_KEY ? name : user2->name) ?
			CMP_MATCH | CMP_STOP : 0;
}

/*!
 * \note This funtion calls realtime_peer -> reg_source_db -> iax2_poke_peer -> find_callno,
 *       so do not call it with a pvt lock held.
 */
static struct iax2_peer *find_peer(const char *name, int realtime)
{
	struct iax2_peer *peer = NULL;

	peer = ao2_find(peers, name, OBJ_KEY);

	/* Now go for realtime if applicable */
	if (!peer && realtime) {
		peer = realtime_peer(name, NULL);
	}
	return peer;
}

static struct iax2_peer *peer_ref(struct iax2_peer *peer)
{
	ao2_ref(peer, +1);
	return peer;
}

static inline struct iax2_peer *peer_unref(struct iax2_peer *peer)
{
	ao2_ref(peer, -1);
	return NULL;
}

static struct iax2_user *find_user(const char *name)
{
	return ao2_find(users, name, OBJ_KEY);
}

static inline struct iax2_user *user_unref(struct iax2_user *user)
{
	ao2_ref(user, -1);
	return NULL;
}

static int iax2_getpeername(struct ast_sockaddr addr, char *host, int len)
{
	struct iax2_peer *peer = NULL;
	int res = 0;
	struct ao2_iterator i;

	i = ao2_iterator_init(peers, 0);
	while ((peer = ao2_iterator_next(&i))) {

		if (!ast_sockaddr_cmp(&peer->addr, &addr)) {
			ast_copy_string(host, peer->name, len);
			peer_unref(peer);
			res = 1;
			break;
		}
		peer_unref(peer);
	}
	ao2_iterator_destroy(&i);

	if (!peer) {
		peer = realtime_peer(NULL, &addr);
		if (peer) {
			ast_copy_string(host, peer->name, len);
			peer_unref(peer);
			res = 1;
		}
	}

	return res;
}

/* Call AST_SCHED_DEL on a scheduled task if it is found in scheduler. */
static int iax2_delete_from_sched(const void* data)
{
	int sched_id = (int)((long)(data));

	/* If call is still found in scheduler (not executed yet), delete it. */
	if (ast_sched_find_data(sched, sched_id)) {
		AST_SCHED_DEL(sched, sched_id);
	}
	return 0;
}

/*!\note Assumes the lock on the pvt is already held, when
 * iax2_destroy_helper() is called. */
static void iax2_destroy_helper(struct chan_iax2_pvt *pvt)
{
	/* Decrement AUTHREQ count if needed */
	if (ast_test_flag64(pvt, IAX_MAXAUTHREQ)) {
		struct iax2_user *user;

		user = ao2_find(users, pvt->username, OBJ_KEY);
		if (user) {
			ast_atomic_fetchadd_int(&user->curauthreq, -1);
			user_unref(user);
		}

		ast_clear_flag64(pvt, IAX_MAXAUTHREQ);
	}


	/* Mark call destroy initiated flag. */
	pvt->destroy_initiated = 1;

	/*
	 * Schedule deleting the scheduled (but didn't run yet) PINGs or LAGRQs.
	 * Already running tasks will be terminated because of destroy_initiated.
	 *
	 * Don't call AST_SCHED_DEL from this thread for pingid and lagid because
	 * it leads to a deadlock between the scheduler thread callback locking
	 * the callno mutex and this thread which holds the callno mutex one or
	 * more times.  It is better to have another thread delete the scheduled
	 * callbacks which doesn't lock the callno mutex.
	 */
	iax2_sched_add(sched, 0, iax2_delete_from_sched, (void*)(long)pvt->pingid);
	iax2_sched_add(sched, 0, iax2_delete_from_sched, (void*)(long)pvt->lagid);

	pvt->pingid = -1;
	pvt->lagid = -1;

	AST_SCHED_DEL(sched, pvt->autoid);
	AST_SCHED_DEL(sched, pvt->authid);
	AST_SCHED_DEL(sched, pvt->initid);
	AST_SCHED_DEL(sched, pvt->jbid);
	AST_SCHED_DEL(sched, pvt->keyrotateid);
}

static void iax2_frame_free(struct iax_frame *fr)
{
	AST_SCHED_DEL(sched, fr->retrans);
	iax_frame_free(fr);
}

static int scheduled_destroy(const void *vid)
{
	unsigned short callno = PTR_TO_CALLNO(vid);
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		ast_debug(1, "Really destroying %d now...\n", callno);
		iax2_destroy(callno);
	}
	ast_mutex_unlock(&iaxsl[callno]);
	return 0;
}

static void free_signaling_queue_entry(struct signaling_queue_entry *s)
{
	if (s->f.datalen) {
		ast_free(s->f.data.ptr);
	}
	ast_free(s);
}

/*! \brief This function must be called once we are sure the other side has
 *  given us a call number.  All signaling is held here until that point. */
static void send_signaling(struct chan_iax2_pvt *pvt)
{
	struct signaling_queue_entry *s = NULL;

	while ((s = AST_LIST_REMOVE_HEAD(&pvt->signaling_queue, next))) {
		iax2_send(pvt, &s->f, 0, -1, 0, 0, 0);
		free_signaling_queue_entry(s);
	}
	pvt->hold_signaling = 0;
}

/*! \brief All frames other than that of type AST_FRAME_IAX must be held until
 *  we have received a destination call number. */
static int queue_signalling(struct chan_iax2_pvt *pvt, struct ast_frame *f)
{
	struct signaling_queue_entry *qe;

	if (f->frametype == AST_FRAME_IAX || !pvt->hold_signaling) {
		return 1; /* do not queue this frame */
	} else if (!(qe = ast_calloc(1, sizeof(struct signaling_queue_entry)))) {
		return -1;  /* out of memory */
	}

	/* copy ast_frame into our queue entry */
	qe->f = *f;
	if (qe->f.datalen) {
		/* if there is data in this frame copy it over as well */
		if (!(qe->f.data.ptr = ast_malloc(qe->f.datalen))) {
			free_signaling_queue_entry(qe);
			return -1;
		}
		memcpy(qe->f.data.ptr, f->data.ptr, qe->f.datalen);
	}
	AST_LIST_INSERT_TAIL(&pvt->signaling_queue, qe, next);

	return 0;
}

static void pvt_destructor(void *obj)
{
	struct chan_iax2_pvt *pvt = obj;
	struct iax_frame *cur = NULL;
	struct signaling_queue_entry *s = NULL;

	ast_mutex_lock(&iaxsl[pvt->callno]);

	iax2_destroy_helper(pvt);

	sched_delay_remove(&pvt->addr, pvt->callno_entry);
	pvt->callno_entry = 0;

	/* Already gone */
	ast_set_flag64(pvt, IAX_ALREADYGONE);

	AST_LIST_TRAVERSE(&frame_queue[pvt->callno], cur, list) {
		/* Cancel any pending transmissions */
		cur->retries = -1;
	}

	ast_mutex_unlock(&iaxsl[pvt->callno]);

	while ((s = AST_LIST_REMOVE_HEAD(&pvt->signaling_queue, next))) {
		free_signaling_queue_entry(s);
	}

	if (pvt->reg) {
		pvt->reg->callno = 0;
	}

	if (!pvt->owner) {
		jb_frame frame;
		if (pvt->vars) {
		    ast_variables_destroy(pvt->vars);
		    pvt->vars = NULL;
		}

		while (jb_getall(pvt->jb, &frame) == JB_OK) {
			iax2_frame_free(frame.data);
		}

		jb_destroy(pvt->jb);
		ast_string_field_free_memory(pvt);
	}
}

static struct chan_iax2_pvt *new_iax(struct ast_sockaddr *addr, const char *host)
{
	struct chan_iax2_pvt *tmp;
	jb_conf jbconf;

	if (!(tmp = ao2_alloc(sizeof(*tmp), pvt_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(tmp, 32)) {
		ao2_ref(tmp, -1);
		tmp = NULL;
		return NULL;
	}

	tmp->prefs = prefs_global;
	tmp->pingid = -1;
	tmp->lagid = -1;
	tmp->autoid = -1;
	tmp->authid = -1;
	tmp->initid = -1;
	tmp->keyrotateid = -1;

	ast_string_field_set(tmp,exten, "s");
	ast_string_field_set(tmp,host, host);

	tmp->jb = jb_new();
	tmp->jbid = -1;
	jbconf.max_jitterbuf = maxjitterbuffer;
	jbconf.resync_threshold = resyncthreshold;
	jbconf.max_contig_interp = maxjitterinterps;
	jbconf.target_extra = jittertargetextra;
	jb_setconf(tmp->jb,&jbconf);

	AST_LIST_HEAD_INIT_NOLOCK(&tmp->dpentries);

	tmp->hold_signaling = 1;
	AST_LIST_HEAD_INIT_NOLOCK(&tmp->signaling_queue);

	return tmp;
}

static struct iax_frame *iaxfrdup2(struct iax_frame *fr)
{
	struct iax_frame *new = iax_frame_new(DIRECTION_INGRESS, fr->af.datalen, fr->cacheable);
	if (new) {
		size_t afdatalen = new->afdatalen;
		memcpy(new, fr, sizeof(*new));
		iax_frame_wrap(new, &fr->af);
		new->afdatalen = afdatalen;
		new->data = NULL;
		new->datalen = 0;
		new->direction = DIRECTION_INGRESS;
		new->retrans = -1;
	}
	return new;
}
/* keep these defined in this order.  They are used in find_callno to
 * determine whether or not a new call number should be allowed. */
enum {
	/* do not allow a new call number, only search ones in use for match */
	NEW_PREVENT = 0,
	/* search for match first, then allow a new one to be allocated */
	NEW_ALLOW = 1,
	/* do not search for match, force a new call number */
	NEW_FORCE = 2,
	/* do not search for match, force a new call number.  Signifies call number
	 * has been calltoken validated */
	NEW_ALLOW_CALLTOKEN_VALIDATED = 3,
};

static int match(struct ast_sockaddr *addr, unsigned short callno, unsigned short dcallno, const struct chan_iax2_pvt *cur, int check_dcallno)
{
	if (!ast_sockaddr_cmp(&cur->addr, addr)) {
		/* This is the main host */
		if ( (cur->peercallno == 0 || cur->peercallno == callno) &&
			 (check_dcallno ? dcallno == cur->callno : 1) ) {
			/* That's us.  Be sure we keep track of the peer call number */
			return 1;
		}
	}
	if (!ast_sockaddr_cmp(&cur->transfer, addr) && cur->transferring) {
		/* We're transferring */
		if ((dcallno == cur->callno) || (cur->transferring == TRANSFER_MEDIAPASS && cur->transfercallno == callno))
			return 1;
	}
	return 0;
}

static int make_trunk(unsigned short callno, int locked)
{
	int x;
	int res= 0;
	callno_entry entry;
	if (iaxs[callno]->oseqno) {
		ast_log(LOG_WARNING, "Can't make trunk once a call has started!\n");
		return -1;
	}
	if (callno >= TRUNK_CALL_START) {
		ast_log(LOG_WARNING, "Call %d is already a trunk\n", callno);
		return -1;
	}

	if (get_unused_callno(
			CALLNO_TYPE_TRUNK,
			CALLNO_ENTRY_IS_VALIDATED(iaxs[callno]->callno_entry),
			&entry)) {
		ast_log(LOG_WARNING, "Unable to trunk call: Insufficient space\n");
		return -1;
	}

	x = CALLNO_ENTRY_GET_CALLNO(entry);
	ast_mutex_lock(&iaxsl[x]);

	/*!
	 * \note We delete these before switching the slot, because if
	 * they fire in the meantime, they will generate a warning.
	 */
	AST_SCHED_DEL(sched, iaxs[callno]->pingid);
	AST_SCHED_DEL(sched, iaxs[callno]->lagid);
	iaxs[callno]->lagid = iaxs[callno]->pingid = -1;
	iaxs[x] = iaxs[callno];
	iaxs[x]->callno = x;

	/* since we copied over the pvt from a different callno, make sure the old entry is replaced
	 * before assigning the new one */
	if (iaxs[x]->callno_entry) {
		iax2_sched_add(
			sched,
			MIN_REUSE_TIME * 1000,
			replace_callno,
			CALLNO_ENTRY_TO_PTR(iaxs[x]->callno_entry));

	}
	iaxs[x]->callno_entry = entry;

	iaxs[callno] = NULL;
	/* Update the two timers that should have been started */
	iaxs[x]->pingid = iax2_sched_add(sched,
		ping_time * 1000, send_ping, (void *)(long)x);
	iaxs[x]->lagid = iax2_sched_add(sched,
		lagrq_time * 1000, send_lagrq, (void *)(long)x);

	if (locked)
		ast_mutex_unlock(&iaxsl[callno]);
	res = x;
	if (!locked)
		ast_mutex_unlock(&iaxsl[x]);

	/* We moved this call from a non-trunked to a trunked call */
	ast_debug(1, "Made call %d into trunk call %d\n", callno, x);

	return res;
}

static void store_by_transfercallno(struct chan_iax2_pvt *pvt)
{
	if (!pvt->transfercallno) {
		ast_log(LOG_ERROR, "This should not be called without a transfer call number.\n");
		return;
	}

	ao2_link(iax_transfercallno_pvts, pvt);
}

static void remove_by_transfercallno(struct chan_iax2_pvt *pvt)
{
	if (!pvt->transfercallno) {
		ast_log(LOG_ERROR, "This should not be called without a transfer call number.\n");
		return;
	}

	ao2_unlink(iax_transfercallno_pvts, pvt);
}
static void store_by_peercallno(struct chan_iax2_pvt *pvt)
{
	if (!pvt->peercallno) {
		ast_log(LOG_ERROR, "This should not be called without a peer call number.\n");
		return;
	}

	ao2_link(iax_peercallno_pvts, pvt);
}

static void remove_by_peercallno(struct chan_iax2_pvt *pvt)
{
	if (!pvt->peercallno) {
		ast_log(LOG_ERROR, "This should not be called without a peer call number.\n");
		return;
	}

	ao2_unlink(iax_peercallno_pvts, pvt);
}

static int addr_range_delme_cb(void *obj, void *arg, int flags)
{
	struct addr_range *lim = obj;
	lim->delme = 1;
	return 0;
}

static int addr_range_hash_cb(const void *obj, const int flags)
{
	const struct addr_range *lim = obj;
	return abs(ast_sockaddr_hash(&lim->ha.addr));
}

static int addr_range_cmp_cb(void *obj, void *arg, int flags)
{
	struct addr_range *lim1 = obj, *lim2 = arg;
	return (!(ast_sockaddr_cmp_addr(&lim1->ha.addr, &lim2->ha.addr)) &&
			!(ast_sockaddr_cmp_addr(&lim1->ha.netmask, &lim2->ha.netmask))) ?
			CMP_MATCH | CMP_STOP : 0;
}

static int peercnt_hash_cb(const void *obj, const int flags)
{
	const struct peercnt *peercnt = obj;

	if (ast_sockaddr_isnull(&peercnt->addr)) {
		return 0;
	}
	return ast_sockaddr_hash(&peercnt->addr);
}

static int peercnt_cmp_cb(void *obj, void *arg, int flags)
{
	struct peercnt *peercnt1 = obj, *peercnt2 = arg;
	return !ast_sockaddr_cmp_addr(&peercnt1->addr, &peercnt2->addr) ? CMP_MATCH | CMP_STOP : 0;
}

static int addr_range_match_address_cb(void *obj, void *arg, int flags)
{
	struct addr_range *addr_range = obj;
	struct ast_sockaddr *addr = arg;
	struct ast_sockaddr tmp_addr;

	ast_sockaddr_apply_netmask(addr, &addr_range->ha.netmask, &tmp_addr);

	if (!ast_sockaddr_cmp_addr(&tmp_addr, &addr_range->ha.addr)) {
		return CMP_MATCH | CMP_STOP;
	}
	return 0;
}

/*!
 * \internal
 *
 * \brief compares addr to calltoken_ignores table to determine if validation is required.
 */
static int calltoken_required(struct ast_sockaddr *addr, const char *name, int subclass)
{
	struct addr_range *addr_range;
	struct iax2_peer *peer = NULL;
	struct iax2_user *user = NULL;
	/* if no username is given, check for guest accounts */
	const char *find = S_OR(name, "guest");
	int res = 1;  /* required by default */
	int optional = 0;
	enum calltoken_peer_enum calltoken_required = CALLTOKEN_DEFAULT;
	/* There are only two cases in which calltoken validation is not required.
	 * Case 1. sin falls within the list of address ranges specified in the calltoken optional table and
	 *         the peer definition has not set the requirecalltoken option.
	 * Case 2. Username is a valid peer/user, and that peer has requirecalltoken set either auto or no.
	 */

	/* ----- Case 1 ----- */
	if ((addr_range = ao2_callback(calltoken_ignores, 0, addr_range_match_address_cb, addr))) {
		ao2_ref(addr_range, -1);
		optional = 1;
	}

	/* ----- Case 2 ----- */
	if ((subclass == IAX_COMMAND_NEW) && (user = find_user(find))) {
		calltoken_required = user->calltoken_required;
	} else if ((subclass == IAX_COMMAND_NEW) && (user = realtime_user(find, addr))) {
		calltoken_required = user->calltoken_required;
	} else if ((subclass != IAX_COMMAND_NEW) && (peer = find_peer(find, 0))) {
		calltoken_required = peer->calltoken_required;
	} else if ((subclass != IAX_COMMAND_NEW) && (peer = realtime_peer(find, addr))) {
		calltoken_required = peer->calltoken_required;
	}

	if (peer) {
		peer_unref(peer);
	}
	if (user) {
		user_unref(user);
	}

	ast_debug(1, "Determining if address %s with username %s requires calltoken validation.  Optional = %d  calltoken_required = %u \n", ast_sockaddr_stringify_addr(addr), name, optional, calltoken_required);
	if (((calltoken_required == CALLTOKEN_NO) || (calltoken_required == CALLTOKEN_AUTO)) ||
		(optional && (calltoken_required == CALLTOKEN_DEFAULT))) {
		res = 0;
	}

	return res;
}

/*!
 * \internal
 *
 * \brief set peercnt callno limit.
 *
 * \details
 * First looks in custom definitions. If not found, global limit
 * is used.  Entries marked as reg already have
 * a custom limit set by a registration and are not modified.
 */
static void set_peercnt_limit(struct peercnt *peercnt)
{
	uint16_t limit = global_maxcallno;
	struct addr_range *addr_range;
	struct ast_sockaddr addr;

	ast_sockaddr_copy(&addr, &peercnt->addr);

	if (peercnt->reg && peercnt->limit) {
		return; /* this peercnt has a custom limit set by a registration */
	}

	if ((addr_range = ao2_callback(callno_limits, 0, addr_range_match_address_cb, &addr))) {
		limit = addr_range->limit;
		ast_debug(1, "custom addr_range %d found for %s\n", limit, ast_sockaddr_stringify(&addr));
		ao2_ref(addr_range, -1);
	}

	peercnt->limit = limit;
}

/*!
 * \internal
 * \brief sets limits for all peercnts in table. done on reload to reflect changes in conf.
 */
static int set_peercnt_limit_all_cb(void *obj, void *arg, int flags)
{
	struct peercnt *peercnt = obj;

	set_peercnt_limit(peercnt);
	ast_debug(1, "Reset limits for peercnts table\n");

	return 0;
}

/*!
 * \internal
 * \brief returns match if delme is set.
 */
static int prune_addr_range_cb(void *obj, void *arg, int flags)
{
	struct addr_range *addr_range = obj;

	return addr_range->delme ? CMP_MATCH : 0;
}

/*!
 * \internal
 * \brief modifies peercnt entry in peercnts table. Used to set custom limit or mark a registered ip
 */
static void peercnt_modify(unsigned char reg, uint16_t limit, struct ast_sockaddr *sockaddr)
{
	/* this function turns off and on custom callno limits set by peer registration */
	struct peercnt *peercnt;
	struct peercnt tmp;

	ast_sockaddr_copy(&tmp.addr, sockaddr);

	if ((peercnt = ao2_find(peercnts, &tmp, OBJ_POINTER))) {
		peercnt->reg = reg;
		if (limit) {
			peercnt->limit = limit;
		} else {
			set_peercnt_limit(peercnt);
		}
		ast_debug(1, "peercnt entry %s modified limit:%d registered:%d", ast_sockaddr_stringify_addr(sockaddr), peercnt->limit, peercnt->reg);
		ao2_ref(peercnt, -1); /* decrement ref from find */
	}
}

/*!
 * \internal
 * \brief adds an ip to the peercnts table, increments connection count if it already exists
 *
 * \details First searches for the address in the peercnts table.  If found
 * the current count is incremented.  If not found a new peercnt is allocated
 * and linked into the peercnts table with a call number count of 1.
 */
static int peercnt_add(struct ast_sockaddr *addr)
{
	struct peercnt *peercnt;
	int res = 0;
	struct peercnt tmp;

	ast_sockaddr_copy(&tmp.addr, addr);

	/* Reasoning for peercnts container lock:  Two identical ip addresses
	 * could be added by different threads at the "same time". Without the container
	 * lock, both threads could alloc space for the same object and attempt
	 * to link to table.  With the lock, one would create the object and link
	 * to table while the other would find the already created peercnt object
	 * rather than creating a new one. */
	ao2_lock(peercnts);
	if ((peercnt = ao2_find(peercnts, &tmp, OBJ_POINTER))) {
		ao2_lock(peercnt);
	} else if ((peercnt = ao2_alloc(sizeof(*peercnt), NULL))) {
		ao2_lock(peercnt);
		/* create and set defaults */
		ast_sockaddr_copy(&peercnt->addr, addr);
		set_peercnt_limit(peercnt);
		/* guarantees it does not go away after unlocking table
		 * ao2_find automatically adds this */
		ao2_link(peercnts, peercnt);
	} else {
		ao2_unlock(peercnts);
		return -1;
	}

	/* check to see if the address has hit its callno limit.  If not increment cur. */
	if (peercnt->limit > peercnt->cur) {
		peercnt->cur++;
		ast_debug(1, "ip callno count incremented to %d for %s\n", peercnt->cur, ast_sockaddr_stringify_addr(addr));
	} else { /* max num call numbers for this peer has been reached! */
		ast_log(LOG_ERROR, "maxcallnumber limit of %d for %s has been reached!\n", peercnt->limit, ast_sockaddr_stringify_addr(addr));
		res = -1;
	}

	/* clean up locks and ref count */
	ao2_unlock(peercnt);
	ao2_unlock(peercnts);
	ao2_ref(peercnt, -1); /* decrement ref from find/alloc, only the container ref remains. */

	return res;
}

/*!
 * \internal
 * \brief decrements a peercnts table entry
 */
static void peercnt_remove(struct peercnt *peercnt)
{
	struct ast_sockaddr addr;

	ast_sockaddr_copy(&addr, &peercnt->addr);

	/*
	 * Container locked here since peercnt may be unlinked from
	 * list.  If left unlocked, peercnt_add could try and grab this
	 * entry from the table and modify it at the "same time" this
	 * thread attemps to unlink it.
	 */
	ao2_lock(peercnts);
	peercnt->cur--;
	ast_debug(1, "ip callno count decremented to %d for %s\n", peercnt->cur, ast_sockaddr_stringify_addr(&addr));
	/* if this was the last connection from the peer remove it from table */
	if (peercnt->cur == 0) {
		ao2_unlink(peercnts, peercnt);/* decrements ref from table, last ref is left to scheduler */
	}
	ao2_unlock(peercnts);
}

/*!
 * \internal
 * \brief called by scheduler to decrement object
 */
static int peercnt_remove_cb(const void *obj)
{
	struct peercnt *peercnt = (struct peercnt *) obj;

	peercnt_remove(peercnt);
	ao2_ref(peercnt, -1); /* decrement ref from scheduler */

	return 0;
}

/*!
 * \internal
 * \brief decrements peercnts connection count, finds by addr
 */
static int peercnt_remove_by_addr(struct ast_sockaddr *addr)
{
	struct peercnt *peercnt;
	struct peercnt tmp;

	ast_sockaddr_copy(&tmp.addr, addr);

	if ((peercnt = ao2_find(peercnts, &tmp, OBJ_POINTER))) {
		peercnt_remove(peercnt);
		ao2_ref(peercnt, -1); /* decrement ref from find */
	}
	return 0;
}

/*!
 * \internal
 * \brief Create callno_limit entry based on configuration
 */
static void build_callno_limits(struct ast_variable *v)
{
	struct addr_range *addr_range = NULL;
	struct addr_range tmp;
	struct ast_ha *ha;
	int limit;
	int error;
	int found;

	for (; v; v = v->next) {
		limit = -1;
		error = 0;
		found = 0;
		ha = ast_append_ha("permit", v->name, NULL, &error);

		/* check for valid config information */
		if (error) {
			ast_log(LOG_ERROR, "Call number limit for %s could not be added, Invalid address range\n.", v->name);
			continue;
		} else if ((sscanf(v->value, "%d", &limit) != 1) || (limit < 0)) {
			ast_log(LOG_ERROR, "Call number limit for %s could not be added. Invalid limit %s\n.", v->name, v->value);
			ast_free_ha(ha);
			continue;
		}

		ast_copy_ha(ha, &tmp.ha);
		/* find or create the addr_range */
		if ((addr_range = ao2_find(callno_limits, &tmp, OBJ_POINTER))) {
			ao2_lock(addr_range);
			found = 1;
		} else if (!(addr_range = ao2_alloc(sizeof(*addr_range), NULL))) {
			ast_free_ha(ha);
			return; /* out of memory */
		}

		/* copy over config data into addr_range object */
		ast_copy_ha(ha, &addr_range->ha); /* this is safe because only one ha is possible for each limit */
		ast_free_ha(ha); /* cleanup the tmp ha */
		addr_range->limit = limit;
		addr_range->delme = 0;

		/* cleanup */
		if (found) {
			ao2_unlock(addr_range);
		} else {
			ao2_link(callno_limits, addr_range);
		}
		ao2_ref(addr_range, -1); /* decrement ref from ao2_find and ao2_alloc, only container ref remains */
	}
}

/*!
 * \internal
 * \brief Create calltoken_ignores entry based on configuration
 */
static int add_calltoken_ignore(const char *addr)
{
	struct addr_range tmp;
	struct addr_range *addr_range = NULL;
	struct ast_ha *ha = NULL;
	int error = 0;

	if (ast_strlen_zero(addr)) {
		ast_log(LOG_WARNING, "invalid calltokenoptional %s\n", addr);
		return -1;
	}

	ha = ast_append_ha("permit", addr, NULL, &error);

	/* check for valid config information */
	if (error) {
		ast_log(LOG_WARNING, "Error %d creating calltokenoptional entry %s\n", error, addr);
		return -1;
	}

	ast_copy_ha(ha, &tmp.ha);
	/* find or create the addr_range */
	if ((addr_range = ao2_find(calltoken_ignores, &tmp, OBJ_POINTER))) {
		ao2_lock(addr_range);
		addr_range->delme = 0;
		ao2_unlock(addr_range);
	} else if ((addr_range = ao2_alloc(sizeof(*addr_range), NULL))) {
		/* copy over config data into addr_range object */
		ast_copy_ha(ha, &addr_range->ha); /* this is safe because only one ha is possible */
		ao2_link(calltoken_ignores, addr_range);
	} else {
		ast_free_ha(ha);
		return -1;
	}

	ast_free_ha(ha);
	ao2_ref(addr_range, -1); /* decrement ref from ao2_find and ao2_alloc, only container ref remains */

	return 0;
}

static char *handle_cli_iax2_show_callno_limits(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct peercnt *peercnt;
	struct ast_sockaddr addr;
	int found = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show callnumber usage";
		e->usage =
			"Usage: iax2 show callnumber usage [IP address]\n"
			"       Shows current IP addresses which are consuming iax2 call numbers\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		if (a->argc < 4 || a->argc > 5)
			return CLI_SHOWUSAGE;

		if (a->argc == 4) {
			ast_cli(a->fd, "%-45s %-12s %-12s\n", "Address", "Callno Usage", "Callno Limit");
		}

		i = ao2_iterator_init(peercnts, 0);
		while ((peercnt = ao2_iterator_next(&i))) {
			ast_sockaddr_copy(&addr, &peercnt->addr);

			if (a->argc == 5) {
				if (!strcasecmp(a->argv[4], ast_sockaddr_stringify(&addr))) {
					ast_cli(a->fd, "%-45s %-12s %-12s\n", "Address", "Callno Usage", "Callno Limit");
					ast_cli(a->fd, "%-45s %-12d %-12d\n", ast_sockaddr_stringify(&addr), peercnt->cur, peercnt->limit);
					ao2_ref(peercnt, -1);
					found = 1;
					break;
				}
			} else {
				ast_cli(a->fd, "%-45s %-12d %-12d\n", ast_sockaddr_stringify(&addr), peercnt->cur, peercnt->limit);
			}
			ao2_ref(peercnt, -1);
		}
		ao2_iterator_destroy(&i);

		if (a->argc == 4) {
			size_t pool_avail = callno_pool.available;
			size_t trunk_pool_avail = callno_pool_trunk.available;

			ast_cli(a->fd, "\nNon-CallToken Validation Callno Limit: %d\n"
			                 "Non-CallToken Validated Callno Used:   %d\n",
				global_maxcallno_nonval,
				total_nonval_callno_used);

			ast_cli(a->fd,   "Total Available Callno:                %zu\n"
			                 "Regular Callno Available:              %zu\n"
			                 "Trunk Callno Available:                %zu\n",
				pool_avail + trunk_pool_avail,
				pool_avail,
				trunk_pool_avail);
		} else if (a->argc == 5 && !found) {
			ast_cli(a->fd, "No call number table entries for %s found\n", a->argv[4] );
		}


		return CLI_SUCCESS;
	default:
		return NULL;
	}
}

static int get_unused_callno(enum callno_type type, int validated, callno_entry *entry)
{
	struct call_number_pool *pool = NULL;
	callno_entry swap;
	size_t choice;

	switch (type) {
	case CALLNO_TYPE_NORMAL:
		pool = &callno_pool;
		break;
	case CALLNO_TYPE_TRUNK:
		pool = &callno_pool_trunk;
		break;
	default:
		ast_assert(0);
		break;
	}

	/* If we fail, make sure this has a defined value */
	*entry = 0;

	/* We lock here primarily to ensure thread safety of the
	 * total_nonval_callno_used check and increment */
	ast_mutex_lock(&callno_pool_lock);

	/* Bail out if we don't have any available call numbers */
	if (!pool->available) {
		ast_log(LOG_WARNING, "Out of call numbers\n");
		ast_mutex_unlock(&callno_pool_lock);
		return 1;
	}

	/* Only a certain number of non-validated call numbers should be allocated.
	 * If there ever is an attack, this separates the calltoken validating users
	 * from the non-calltoken validating users. */
	if (!validated && total_nonval_callno_used >= global_maxcallno_nonval) {
		ast_log(LOG_WARNING,
			"NON-CallToken callnumber limit is reached. Current: %d Max: %d\n",
			total_nonval_callno_used,
			global_maxcallno_nonval);
		ast_mutex_unlock(&callno_pool_lock);
		return 1;
	}

	/* We use a modified Fisher-Yates-Durstenfeld Shuffle to maintain a list of
	 * available call numbers.  The array of call numbers begins as an ordered
	 * list from 1 -> n, and we keep a running tally of how many remain unclaimed
	 * - let's call that x.  When a call number is needed we pick a random index
	 * into the array between 0 and x and use that as our call number.  In a
	 * typical FYD shuffle, we would swap the value that we are extracting with
	 * the number at x, but in our case we swap and don't touch the value at x
	 * because it is effectively invisible.  We rely on the rest of the IAX2 core
	 * to return the number to us at some point.  Finally, we decrement x by 1
	 * which establishes our new unused range.
	 *
	 * When numbers are returned to the pool, we put them just past x and bump x
	 * by 1 so that this number is now available for re-use. */

	choice = ast_random() % pool->available;

	*entry = pool->numbers[choice];
	swap = pool->numbers[pool->available - 1];

	pool->numbers[choice] = swap;
	pool->available--;

	if (validated) {
		CALLNO_ENTRY_SET_VALIDATED(*entry);
	} else {
		total_nonval_callno_used++;
	}

	ast_mutex_unlock(&callno_pool_lock);

	return 0;
}

static int replace_callno(const void *obj)
{
	callno_entry entry = PTR_TO_CALLNO_ENTRY(obj);
	struct call_number_pool *pool;

	/* We lock here primarily to ensure thread safety of the
	 * total_nonval_callno_used check and decrement */
	ast_mutex_lock(&callno_pool_lock);

	if (!CALLNO_ENTRY_IS_VALIDATED(entry)) {
		if (total_nonval_callno_used) {
			total_nonval_callno_used--;
		} else {
			ast_log(LOG_ERROR,
				"Attempted to decrement total non calltoken validated "
				"callnumbers below zero.  Callno is: %d\n",
				CALLNO_ENTRY_GET_CALLNO(entry));
		}
	}

	if (CALLNO_ENTRY_GET_CALLNO(entry) < TRUNK_CALL_START) {
		pool = &callno_pool;
	} else {
		pool = &callno_pool_trunk;
	}

	ast_assert(pool->capacity > pool->available);

	/* This clears the validated flag */
	entry = CALLNO_ENTRY_GET_CALLNO(entry);

	pool->numbers[pool->available] = entry;
	pool->available++;

	ast_mutex_unlock(&callno_pool_lock);

	return 0;
}

static int create_callno_pools(void)
{
	uint16_t i;

	callno_pool.available = callno_pool_trunk.available = 0;

	/* We start at 2.  0 and 1 are reserved. */
	for (i = 2; i < TRUNK_CALL_START; i++) {
		callno_pool.numbers[callno_pool.available] = i;
		callno_pool.available++;
	}

	for (i = TRUNK_CALL_START; i < IAX_MAX_CALLS; i++) {
		callno_pool_trunk.numbers[callno_pool_trunk.available] = i;
		callno_pool_trunk.available++;
	}

	callno_pool.capacity = callno_pool.available;
	callno_pool_trunk.capacity = callno_pool_trunk.available;

	ast_assert(callno_pool.capacity && callno_pool_trunk.capacity);

	return 0;
}

/*!
 * \internal
 * \brief Schedules delayed removal of iax2_pvt call number data
 *
 * \note After MIN_REUSE_TIME has passed for a destroyed iax2_pvt, the callno is
 * available again, and the address from the previous connection must be decremented
 * from the peercnts table.  This function schedules these operations to take place.
 */
static void sched_delay_remove(struct ast_sockaddr *addr, callno_entry entry)
{
	int i;
	struct peercnt *peercnt;
	struct peercnt tmp;

	ast_sockaddr_copy(&tmp.addr, addr);

	if ((peercnt = ao2_find(peercnts, &tmp, OBJ_POINTER))) {
		/* refcount is incremented with ao2_find.  keep that ref for the scheduler */
		ast_debug(1, "schedule decrement of callno used for %s in %d seconds\n", ast_sockaddr_stringify_addr(addr), MIN_REUSE_TIME);
		i = iax2_sched_add(sched, MIN_REUSE_TIME * 1000, peercnt_remove_cb, peercnt);
		if (i == -1) {
			ao2_ref(peercnt, -1);
		}
	}

	iax2_sched_add(
		sched,
		MIN_REUSE_TIME * 1000,
		replace_callno,
		CALLNO_ENTRY_TO_PTR(entry));
}

/*!
 * \internal
 * \brief returns whether or not a frame is capable of starting a new IAX2 dialog.
 *
 * \note For this implementation, inbound pokes should _NOT_ be capable of allocating
 * a new callno.
 */
static inline int attribute_pure iax2_allow_new(int frametype, int subclass, int inbound)
{
	if (frametype != AST_FRAME_IAX) {
		return 0;
	}
	switch (subclass) {
	case IAX_COMMAND_NEW:
	case IAX_COMMAND_REGREQ:
	case IAX_COMMAND_FWDOWNL:
	case IAX_COMMAND_REGREL:
		return 1;
	case IAX_COMMAND_POKE:
		if (!inbound) {
			return 1;
		}
		break;
	}
	return 0;
}

/*
 * \note Calling this function while holding another pvt lock can cause a deadlock.
 */
static int __find_callno(unsigned short callno, unsigned short dcallno, struct ast_sockaddr *addr, int new, int sockfd, int return_locked, int check_dcallno)
{
	int res = 0;
	int x;
	/* this call is calltoken validated as long as it is either NEW_FORCE
	 * or NEW_ALLOW_CALLTOKEN_VALIDATED */
	int validated = (new > NEW_ALLOW) ? 1 : 0;
	char host[80];

	if (new <= NEW_ALLOW) {
		if (callno) {
			struct chan_iax2_pvt *pvt;
			struct chan_iax2_pvt tmp_pvt = {
				.callno = dcallno,
				.peercallno = callno,
				.transfercallno = callno,
				/* hack!! */
				.frames_received = check_dcallno,
			};

			ast_sockaddr_copy(&tmp_pvt.addr, addr);
			/* this works for finding normal call numbers not involving transfering */
			if ((pvt = ao2_find(iax_peercallno_pvts, &tmp_pvt, OBJ_POINTER))) {
				if (return_locked) {
					ast_mutex_lock(&iaxsl[pvt->callno]);
				}
				res = pvt->callno;
				ao2_ref(pvt, -1);
				pvt = NULL;
				return res;
			}
			/* this searches for transfer call numbers that might not get caught otherwise */
			memset(&tmp_pvt.addr, 0, sizeof(tmp_pvt.addr));
			ast_sockaddr_copy(&tmp_pvt.transfer, addr);
			if ((pvt = ao2_find(iax_transfercallno_pvts, &tmp_pvt, OBJ_POINTER))) {
				if (return_locked) {
					ast_mutex_lock(&iaxsl[pvt->callno]);
				}
				res = pvt->callno;
				ao2_ref(pvt, -1);
				pvt = NULL;
				return res;
			}
		}
			/* This will occur on the first response to a message that we initiated,
		 * such as a PING. */
		if (dcallno) {
			ast_mutex_lock(&iaxsl[dcallno]);
		}
		if (callno && dcallno && iaxs[dcallno] && !iaxs[dcallno]->peercallno && match(addr, callno, dcallno, iaxs[dcallno], check_dcallno)) {
			iaxs[dcallno]->peercallno = callno;
			res = dcallno;
			store_by_peercallno(iaxs[dcallno]);
			if (!res || !return_locked) {
				ast_mutex_unlock(&iaxsl[dcallno]);
			}
			return res;
		}
		if (dcallno) {
			ast_mutex_unlock(&iaxsl[dcallno]);
		}
	}
	if (!res && (new >= NEW_ALLOW)) {
		callno_entry entry;

		/* It may seem odd that we look through the peer list for a name for
		 * this *incoming* call.  Well, it is weird.  However, users don't
		 * have an IP address/port number that we can match against.  So,
		 * this is just checking for a peer that has that IP/port and
		 * assuming that we have a user of the same name.  This isn't always
		 * correct, but it will be changed if needed after authentication. */
		if (!iax2_getpeername(*addr, host, sizeof(host)))
			snprintf(host, sizeof(host), "%s", ast_sockaddr_stringify(addr));

		if (peercnt_add(addr)) {
			/* This address has hit its callnumber limit.  When the limit
			 * is reached, the connection is not added to the peercnts table.*/
			return 0;
		}

		if (get_unused_callno(CALLNO_TYPE_NORMAL, validated, &entry)) {
			/* since we ran out of space, remove the peercnt
			 * entry we added earlier */
			peercnt_remove_by_addr(addr);
			ast_log(LOG_WARNING, "No more space\n");
			return 0;
		}
		x = CALLNO_ENTRY_GET_CALLNO(entry);
		ast_mutex_lock(&iaxsl[x]);

		iaxs[x] = new_iax(addr, host);
		if (iaxs[x]) {
			if (iaxdebug)
				ast_debug(1, "Creating new call structure %d\n", x);
			iaxs[x]->callno_entry = entry;
			iaxs[x]->sockfd = sockfd;
			ast_sockaddr_copy(&iaxs[x]->addr, addr);
			iaxs[x]->peercallno = callno;
			iaxs[x]->callno = x;
			iaxs[x]->pingtime = DEFAULT_RETRY_TIME;
			iaxs[x]->expiry = min_reg_expire;
			iaxs[x]->pingid = iax2_sched_add(sched, ping_time * 1000, send_ping, (void *)(long)x);
			iaxs[x]->lagid = iax2_sched_add(sched, lagrq_time * 1000, send_lagrq, (void *)(long)x);
			iaxs[x]->amaflags = amaflags;
			ast_copy_flags64(iaxs[x], &globalflags, IAX_NOTRANSFER | IAX_TRANSFERMEDIA | IAX_USEJITTERBUF | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE | IAX_FORCE_ENCRYPT);
			ast_string_field_set(iaxs[x], accountcode, accountcode);
			ast_string_field_set(iaxs[x], mohinterpret, mohinterpret);
			ast_string_field_set(iaxs[x], mohsuggest, mohsuggest);
			ast_string_field_set(iaxs[x], parkinglot, default_parkinglot);

			if (iaxs[x]->peercallno) {
				store_by_peercallno(iaxs[x]);
			}
		} else {
			ast_log(LOG_WARNING, "Out of resources\n");
			ast_mutex_unlock(&iaxsl[x]);
			replace_callno(CALLNO_ENTRY_TO_PTR(entry));
			return 0;
		}
		if (!return_locked)
			ast_mutex_unlock(&iaxsl[x]);
		res = x;
	}
	return res;
}

static int find_callno(unsigned short callno, unsigned short dcallno, struct ast_sockaddr *addr, int new, int sockfd, int full_frame) {
	return __find_callno(callno, dcallno, addr, new, sockfd, 0, full_frame);
}

static int find_callno_locked(unsigned short callno, unsigned short dcallno, struct ast_sockaddr *addr, int new, int sockfd, int full_frame) {

	return __find_callno(callno, dcallno, addr, new, sockfd, 1, full_frame);
}

/*!
 * \brief Queue a frame to a call's owning asterisk channel
 *
 * \pre This function assumes that iaxsl[callno] is locked when called.
 *
 * \note IMPORTANT NOTE!!! Any time this function is used, even if iaxs[callno]
 * was valid before calling it, it may no longer be valid after calling it.
 * This function may unlock and lock the mutex associated with this callno,
 * meaning that another thread may grab it and destroy the call.
 */
static int iax2_queue_frame(int callno, struct ast_frame *f)
{
	iax2_lock_owner(callno);
	if (iaxs[callno] && iaxs[callno]->owner) {
		ast_queue_frame(iaxs[callno]->owner, f);
		ast_channel_unlock(iaxs[callno]->owner);
	}
	return 0;
}

/*!
 * \brief Queue a hold frame on the ast_channel owner
 *
 * This function queues a hold frame on the owner of the IAX2 pvt struct that
 * is active for the given call number.
 *
 * \pre Assumes lock for callno is already held.
 *
 * \note IMPORTANT NOTE!!! Any time this function is used, even if iaxs[callno]
 * was valid before calling it, it may no longer be valid after calling it.
 * This function may unlock and lock the mutex associated with this callno,
 * meaning that another thread may grab it and destroy the call.
 */
static int iax2_queue_hold(int callno, const char *musicclass)
{
	iax2_lock_owner(callno);
	if (iaxs[callno] && iaxs[callno]->owner) {
		ast_queue_hold(iaxs[callno]->owner, musicclass);
		ast_channel_unlock(iaxs[callno]->owner);
	}
	return 0;
}

/*!
 * \brief Queue an unhold frame on the ast_channel owner
 *
 * This function queues an unhold frame on the owner of the IAX2 pvt struct that
 * is active for the given call number.
 *
 * \pre Assumes lock for callno is already held.
 *
 * \note IMPORTANT NOTE!!! Any time this function is used, even if iaxs[callno]
 * was valid before calling it, it may no longer be valid after calling it.
 * This function may unlock and lock the mutex associated with this callno,
 * meaning that another thread may grab it and destroy the call.
 */
static int iax2_queue_unhold(int callno)
{
	iax2_lock_owner(callno);
	if (iaxs[callno] && iaxs[callno]->owner) {
		ast_queue_unhold(iaxs[callno]->owner);
		ast_channel_unlock(iaxs[callno]->owner);
	}
	return 0;
}

/*!
 * \brief Queue a hangup frame on the ast_channel owner
 *
 * This function queues a hangup frame on the owner of the IAX2 pvt struct that
 * is active for the given call number.
 *
 * \pre Assumes lock for callno is already held.
 *
 * \note IMPORTANT NOTE!!! Any time this function is used, even if iaxs[callno]
 * was valid before calling it, it may no longer be valid after calling it.
 * This function may unlock and lock the mutex associated with this callno,
 * meaning that another thread may grab it and destroy the call.
 */
static int iax2_queue_hangup(int callno)
{
	iax2_lock_owner(callno);
	if (iaxs[callno] && iaxs[callno]->owner) {
		ast_queue_hangup(iaxs[callno]->owner);
		ast_channel_unlock(iaxs[callno]->owner);
	}
	return 0;
}

/*!
 * \note This function assumes that iaxsl[callno] is locked when called.
 *
 * \note IMPORTANT NOTE!!! Any time this function is used, even if iaxs[callno]
 * was valid before calling it, it may no longer be valid after calling it.
 * This function calls iax2_queue_frame(), which may unlock and lock the mutex
 * associated with this callno, meaning that another thread may grab it and destroy the call.
 */
static int __do_deliver(void *data)
{
	/* Just deliver the packet by using queueing.  This is called by
	  the IAX thread with the iaxsl lock held. */
	struct iax_frame *fr = data;
	fr->retrans = -1;
	ast_clear_flag(&fr->af, AST_FRFLAG_HAS_TIMING_INFO);
	if (iaxs[fr->callno] && !ast_test_flag64(iaxs[fr->callno], IAX_ALREADYGONE))
		iax2_queue_frame(fr->callno, &fr->af);
	/* Free our iax frame */
	iax2_frame_free(fr);
	/* And don't run again */
	return 0;
}

static int handle_error(void)
{
	/* XXX Ideally we should figure out why an error occurred and then abort those
	   rather than continuing to try.  Unfortunately, the published interface does
	   not seem to work XXX */
#if 0
	struct sockaddr_in *sin;
	int res;
	struct msghdr m;
	struct sock_extended_err e;
	m.msg_name = NULL;
	m.msg_namelen = 0;
	m.msg_iov = NULL;
	m.msg_control = &e;
	m.msg_controllen = sizeof(e);
	m.msg_flags = 0;
	res = recvmsg(netsocket, &m, MSG_ERRQUEUE);
	if (res < 0)
		ast_log(LOG_WARNING, "Error detected, but unable to read error: %s\n", strerror(errno));
	else {
		if (m.msg_controllen) {
			sin = (struct sockaddr_in *)SO_EE_OFFENDER(&e);
			if (sin)
				ast_log(LOG_WARNING, "Receive error from %s\n", ast_inet_ntoa(sin->sin_addr));
			else
				ast_log(LOG_WARNING, "No address detected??\n");
		} else {
			ast_log(LOG_WARNING, "Local error: %s\n", strerror(e.ee_errno));
		}
	}
#endif
	return 0;
}

static int transmit_trunk(struct iax_frame *f, struct ast_sockaddr *addr, int sockfd)
{
	int res;
	res = ast_sendto(sockfd, f->data, f->datalen, 0, addr);

	if (res < 0) {
		ast_debug(1, "Received error: %s\n", strerror(errno));
		handle_error();
	} else
		res = 0;
	return res;
}

static int send_packet(struct iax_frame *f)
{
	int res;
	int callno = f->callno;

	/* Don't send if there was an error, but return error instead */
	if (!callno || !iaxs[callno] || iaxs[callno]->error)
	    return -1;

	/* Called with iaxsl held */
	if (iaxdebug) {
		ast_debug(3, "Sending %u on %d/%d to %s\n", f->ts, callno, iaxs[callno]->peercallno, ast_sockaddr_stringify(&iaxs[callno]->addr));
	}
	if (f->transfer) {
		iax_outputframe(f, NULL, 0, &iaxs[callno]->transfer, f->datalen - sizeof(struct ast_iax2_full_hdr));
		res = ast_sendto(iaxs[callno]->sockfd, f->data, f->datalen, 0, &iaxs[callno]->transfer);
	} else {
		iax_outputframe(f, NULL, 0, &iaxs[callno]->addr, f->datalen - sizeof(struct ast_iax2_full_hdr));
		res = ast_sendto(iaxs[callno]->sockfd, f->data, f->datalen, 0, &iaxs[callno]->addr);
	}
	if (res < 0) {
		if (iaxdebug)
			ast_debug(1, "Received error: %s\n", strerror(errno));
		handle_error();
	} else
		res = 0;

	return res;
}

/*!
 * \note Since this function calls iax2_queue_hangup(), the pvt struct
 *       for the given call number may disappear during its execution.
 */
static int iax2_predestroy(int callno)
{
	struct ast_channel *c = NULL;
	struct chan_iax2_pvt *pvt = iaxs[callno];

	if (!pvt)
		return -1;

	if (!ast_test_flag64(pvt, IAX_ALREADYGONE)) {
		iax2_destroy_helper(pvt);
		ast_set_flag64(pvt, IAX_ALREADYGONE);
	}

	if ((c = pvt->owner)) {
		ast_channel_tech_pvt_set(c, NULL);
		iax2_queue_hangup(callno);
		pvt->owner = NULL;
		ast_module_unref(ast_module_info->self);
	}

	return 0;
}

static void iax2_destroy(int callno)
{
	struct chan_iax2_pvt *pvt = NULL;
	struct ast_channel *owner = NULL;

retry:
	if ((pvt = iaxs[callno])) {
#if 0
		/* iax2_destroy_helper gets called from this function later on.  When
		 * called twice, we get the (previously) familiar FRACK! errors in
		 * devmode, from the scheduler.  An alternative to this approach is to
		 * reset the scheduler entries to -1 when they're deleted in
		 * iax2_destroy_helper().  That approach was previously decided to be
		 * "wrong" because "the memory is going to be deallocated anyway.  Why
		 * should we be resetting those values?" */
		iax2_destroy_helper(pvt);
#endif
	}

	owner = pvt ? pvt->owner : NULL;

	if (owner) {
		if (ast_channel_trylock(owner)) {
			ast_debug(3, "Avoiding IAX destroy deadlock\n");
			DEADLOCK_AVOIDANCE(&iaxsl[callno]);
			goto retry;
		}
	}

	if (!owner) {
		iaxs[callno] = NULL;
	}

	if (pvt) {
		if (!owner) {
			pvt->owner = NULL;
		} else {
			/* If there's an owner, prod it to give up */
			/* It is ok to use ast_queue_hangup() here instead of iax2_queue_hangup()
			 * because we already hold the owner channel lock. */
			ast_queue_hangup(owner);
		}

		if (pvt->peercallno) {
			remove_by_peercallno(pvt);
		}

		if (pvt->transfercallno) {
			remove_by_transfercallno(pvt);
		}

		if (!owner) {
			ao2_ref(pvt, -1);
			pvt = NULL;
		}
	}

	if (owner) {
		ast_channel_unlock(owner);
	}
}

static int update_packet(struct iax_frame *f)
{
	/* Called with iaxsl lock held, and iaxs[callno] non-NULL */
	struct ast_iax2_full_hdr *fh = f->data;
	struct ast_frame af;

	/* if frame is encrypted. decrypt before updating it. */
	if (f->encmethods) {
		decode_frame(&f->mydcx, fh, &af, &f->datalen);
	}
	/* Mark this as a retransmission */
	fh->dcallno = ntohs(IAX_FLAG_RETRANS | f->dcallno);
	/* Update iseqno */
	f->iseqno = iaxs[f->callno]->iseqno;
	fh->iseqno = f->iseqno;

	/* Now re-encrypt the frame */
	if (f->encmethods) {
	/* since this is a retransmit frame, create a new random padding
	 * before re-encrypting. */
		build_rand_pad(f->semirand, sizeof(f->semirand));
		encrypt_frame(&f->ecx, fh, f->semirand, &f->datalen);
	}
	return 0;
}

static int attempt_transmit(const void *data);
static void __attempt_transmit(const void *data)
{
	/* Attempt to transmit the frame to the remote peer...
	   Called without iaxsl held. */
	struct iax_frame *f = (struct iax_frame *)data;
	int freeme = 0;
	int callno = f->callno;

	/* Make sure this call is still active */
	if (callno)
		ast_mutex_lock(&iaxsl[callno]);
	if (callno && iaxs[callno]) {
		if (f->retries < 0) {
			/* Already ACK'd */
			freeme = 1;
		} else if (f->retries >= max_retries) {
			/* Too many attempts.  Record an error. */
			if (f->transfer) {
				/* Transfer timeout */
				send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_TXREJ, 0, NULL, 0, -1);
			} else if (f->final) {
				iax2_destroy(callno);
			} else {
				if (iaxs[callno]->owner) {
					ast_log(LOG_WARNING, "Max retries exceeded to host %s on %s (type = %u, subclass = %d, ts=%u, seqno=%d)\n",
						ast_sockaddr_stringify_addr(&iaxs[f->callno]->addr),
						ast_channel_name(iaxs[f->callno]->owner),
						f->af.frametype,
						f->af.subclass.integer,
						f->ts,
						f->oseqno);
				}
				iaxs[callno]->error = ETIMEDOUT;
				if (iaxs[callno]->owner) {
					struct ast_frame fr = { AST_FRAME_CONTROL, { AST_CONTROL_HANGUP }, .data.uint32 = AST_CAUSE_DESTINATION_OUT_OF_ORDER };
					/* Hangup the fd */
					iax2_queue_frame(callno, &fr); /* XXX */
					/* Remember, owner could disappear */
					if (iaxs[callno] && iaxs[callno]->owner)
						ast_channel_hangupcause_set(iaxs[callno]->owner, AST_CAUSE_DESTINATION_OUT_OF_ORDER);
				} else {
					if (iaxs[callno]->reg) {
						memset(&iaxs[callno]->reg->us, 0, sizeof(iaxs[callno]->reg->us));
						iaxs[callno]->reg->regstate = REG_STATE_TIMEOUT;
						iaxs[callno]->reg->refresh = IAX_DEFAULT_REG_EXPIRE;
					}
					iax2_destroy(callno);
				}
			}
			freeme = 1;
		} else {
			/* Update it if it needs it */
			update_packet(f);
			/* Attempt transmission */
			send_packet(f);
			f->retries++;
			/* Try again later after 10 times as long */
			f->retrytime *= 10;
			if (f->retrytime > MAX_RETRY_TIME)
				f->retrytime = MAX_RETRY_TIME;
			/* Transfer messages max out at one second */
			if (f->transfer && (f->retrytime > 1000))
				f->retrytime = 1000;
			f->retrans = iax2_sched_add(sched, f->retrytime, attempt_transmit, f);
		}
	} else {
		/* Make sure it gets freed */
		f->retries = -1;
		freeme = 1;
	}

	if (freeme) {
		/* Don't attempt delivery, just remove it from the queue */
		AST_LIST_REMOVE(&frame_queue[callno], f, list);
		ast_mutex_unlock(&iaxsl[callno]);
		f->retrans = -1; /* this is safe because this is the scheduled function */
		/* Free the IAX frame */
		iax2_frame_free(f);
	} else if (callno) {
		ast_mutex_unlock(&iaxsl[callno]);
	}
}

static int attempt_transmit(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__attempt_transmit, data))
#endif
		__attempt_transmit(data);
	return 0;
}

static char *handle_cli_iax2_prune_realtime(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct iax2_peer *peer = NULL;
	struct iax2_user *user = NULL;
	static const char * const choices[] = { "all", NULL };
	char *cmplt;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 prune realtime";
		e->usage =
			"Usage: iax2 prune realtime [<peername>|all]\n"
			"       Prunes object(s) from the cache\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			cmplt = ast_cli_complete(a->word, choices, a->n);
			if (!cmplt)
				cmplt = complete_iax2_peers(a->line, a->word, a->pos, a->n - sizeof(choices), IAX_RTCACHEFRIENDS);
			return cmplt;
		}
		return NULL;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	if (!strcmp(a->argv[3], "all")) {
		prune_users();
		prune_peers();
		ast_cli(a->fd, "Cache flushed successfully.\n");
		return CLI_SUCCESS;
	}
	peer = find_peer(a->argv[3], 0);
	user = find_user(a->argv[3]);
	if (peer || user) {
		if (peer) {
			if (ast_test_flag64(peer, IAX_RTCACHEFRIENDS)) {
				ast_set_flag64(peer, IAX_RTAUTOCLEAR);
				expire_registry(peer_ref(peer));
				ast_cli(a->fd, "Peer %s was removed from the cache.\n", a->argv[3]);
			} else {
				ast_cli(a->fd, "Peer %s is not eligible for this operation.\n", a->argv[3]);
			}
			peer_unref(peer);
		}
		if (user) {
			if (ast_test_flag64(user, IAX_RTCACHEFRIENDS)) {
				ast_set_flag64(user, IAX_RTAUTOCLEAR);
				ast_cli(a->fd, "User %s was removed from the cache.\n", a->argv[3]);
			} else {
				ast_cli(a->fd, "User %s is not eligible for this operation.\n", a->argv[3]);
			}
			ao2_unlink(users,user);
			user_unref(user);
		}
	} else {
		ast_cli(a->fd, "%s was not found in the cache.\n", a->argv[3]);
	}

	return CLI_SUCCESS;
}

static char *handle_cli_iax2_test_losspct(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 test losspct";
		e->usage =
			"Usage: iax2 test losspct <percentage>\n"
			"       For testing, throws away <percentage> percent of incoming packets\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	test_losspct = atoi(a->argv[3]);

	return CLI_SUCCESS;
}

#ifdef IAXTESTS
static char *handle_cli_iax2_test_late(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 test late";
		e->usage =
			"Usage: iax2 test late <ms>\n"
			"       For testing, count the next frame as <ms> ms late\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	test_late = atoi(a->argv[3]);

	return CLI_SUCCESS;
}

static char *handle_cli_iax2_test_resync(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 test resync";
		e->usage =
			"Usage: iax2 test resync <ms>\n"
			"       For testing, adjust all future frames by <ms> ms\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	test_resync = atoi(a->argv[3]);

	return CLI_SUCCESS;
}

static char *handle_cli_iax2_test_jitter(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 test jitter";
		e->usage =
			"Usage: iax2 test jitter <ms> <pct>\n"
			"       For testing, simulate maximum jitter of +/- <ms> on <pct>\n"
			"       percentage of packets. If <pct> is not specified, adds\n"
			"       jitter to all packets.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 4 || a->argc > 5)
		return CLI_SHOWUSAGE;

	test_jit = atoi(a->argv[3]);
	if (a->argc == 5)
		test_jitpct = atoi(a->argv[4]);

	return CLI_SUCCESS;
}
#endif /* IAXTESTS */

/*! \brief  peer_status: Report Peer status in character string */
/* 	returns 1 if peer is online, -1 if unmonitored */
static int peer_status(struct iax2_peer *peer, char *status, int statuslen)
{
	int res = 0;
	if (peer->maxms) {
		if (peer->lastms < 0) {
			ast_copy_string(status, "UNREACHABLE", statuslen);
		} else if (peer->lastms > peer->maxms) {
			snprintf(status, statuslen, "LAGGED (%d ms)", peer->lastms);
			res = 1;
		} else if (peer->lastms) {
			snprintf(status, statuslen, "OK (%d ms)", peer->lastms);
			res = 1;
		} else {
			ast_copy_string(status, "UNKNOWN", statuslen);
		}
	} else {
		ast_copy_string(status, "Unmonitored", statuslen);
		res = -1;
	}
	return res;
}

/*! \brief Show one peer in detail */
static char *handle_cli_iax2_show_peer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char status[30];
	char cbuf[256];
	struct iax2_peer *peer;
	struct ast_str *codec_buf = ast_str_alloca(256);
	struct ast_str *encmethods = ast_str_alloca(256);
	int load_realtime = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show peer";
		e->usage =
			"Usage: iax2 show peer <name>\n"
			"       Display details on specific IAX peer\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3)
			return complete_iax2_peers(a->line, a->word, a->pos, a->n, 0);
		return NULL;
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;

	load_realtime = (a->argc == 5 && !strcmp(a->argv[4], "load")) ? 1 : 0;

	peer = find_peer(a->argv[3], load_realtime);
	if (peer) {
		char *str_addr, *str_defaddr;
		char *str_port, *str_defport;

		str_addr = ast_strdupa(ast_sockaddr_stringify_addr(&peer->addr));
		str_port = ast_strdupa(ast_sockaddr_stringify_port(&peer->addr));
		str_defaddr = ast_strdupa(ast_sockaddr_stringify_addr(&peer->defaddr));
		str_defport = ast_strdupa(ast_sockaddr_stringify_port(&peer->defaddr));

		encmethods_to_str(peer->encmethods, &encmethods);
		ast_cli(a->fd, "\n\n");
		ast_cli(a->fd, "  * Name       : %s\n", peer->name);
		ast_cli(a->fd, "  Description  : %s\n", peer->description);
		ast_cli(a->fd, "  Secret       : %s\n", ast_strlen_zero(peer->secret) ? "<Not set>" : "<Set>");
		ast_cli(a->fd, "  Context      : %s\n", peer->context);
 		ast_cli(a->fd, "  Parking lot  : %s\n", peer->parkinglot);
		ast_cli(a->fd, "  Mailbox      : %s\n", peer->mailbox);
		ast_cli(a->fd, "  Dynamic      : %s\n", ast_test_flag64(peer, IAX_DYNAMIC) ? "Yes" : "No");
		ast_cli(a->fd, "  Callnum limit: %d\n", peer->maxcallno);
		ast_cli(a->fd, "  Calltoken req: %s\n", (peer->calltoken_required == CALLTOKEN_YES) ? "Yes" : ((peer->calltoken_required == CALLTOKEN_AUTO) ? "Auto" : "No"));
		ast_cli(a->fd, "  Trunk        : %s\n", ast_test_flag64(peer, IAX_TRUNK) ? "Yes" : "No");
		ast_cli(a->fd, "  Encryption   : %s\n", peer->encmethods ? ast_str_buffer(encmethods) : "No");
		ast_cli(a->fd, "  Callerid     : %s\n", ast_callerid_merge(cbuf, sizeof(cbuf), peer->cid_name, peer->cid_num, "<unspecified>"));
		ast_cli(a->fd, "  Expire       : %d\n", peer->expire);
		ast_cli(a->fd, "  ACL          : %s\n", (ast_acl_list_is_empty(peer->acl) ? "No" : "Yes"));
		ast_cli(a->fd, "  Addr->IP     : %s Port %s\n",  str_addr ? str_addr : "(Unspecified)", str_port);
		ast_cli(a->fd, "  Defaddr->IP  : %s Port %s\n", str_defaddr, str_defport);
		ast_cli(a->fd, "  Username     : %s\n", peer->username);
		ast_cli(a->fd, "  Codecs       : %s\n", iax2_getformatname_multiple(peer->capability, &codec_buf));

		if (iax2_codec_pref_string(&peer->prefs, cbuf, sizeof(cbuf)) < 0) {
			strcpy(cbuf, "Error"); /* Safe */
		}
		ast_cli(a->fd, "  Codec Order  : %s\n", cbuf);

		peer_status(peer, status, sizeof(status));
		ast_cli(a->fd, "  Status       : %s\n", status);
		ast_cli(a->fd, "  Qualify      : every %dms when OK, every %dms when UNREACHABLE (sample smoothing %s)\n", peer->pokefreqok, peer->pokefreqnotok, peer->smoothing ? "On" : "Off");
		ast_cli(a->fd, "\n");
		peer_unref(peer);
	} else {
		ast_cli(a->fd, "Peer %s not found.\n", a->argv[3]);
		ast_cli(a->fd, "\n");
	}

	return CLI_SUCCESS;
}

static char *complete_iax2_peers(const char *line, const char *word, int pos, int state, uint64_t flags)
{
	int which = 0;
	struct iax2_peer *peer;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;

	i = ao2_iterator_init(peers, 0);
	while ((peer = ao2_iterator_next(&i))) {
		if (!strncasecmp(peer->name, word, wordlen) && ++which > state
			&& (!flags || ast_test_flag64(peer, flags))) {
			res = ast_strdup(peer->name);
			peer_unref(peer);
			break;
		}
		peer_unref(peer);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_iax2_show_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct iax_frame *cur;
	int cnt = 0, dead = 0, final = 0, i = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show stats";
		e->usage =
			"Usage: iax2 show stats\n"
			"       Display statistics on IAX channel driver.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	for (i = 0; i < ARRAY_LEN(frame_queue); i++) {
		ast_mutex_lock(&iaxsl[i]);
		AST_LIST_TRAVERSE(&frame_queue[i], cur, list) {
			if (cur->retries < 0)
				dead++;
			if (cur->final)
				final++;
			cnt++;
		}
		ast_mutex_unlock(&iaxsl[i]);
	}

	ast_cli(a->fd, "    IAX Statistics\n");
	ast_cli(a->fd, "---------------------\n");
	ast_cli(a->fd, "Outstanding frames: %d (%d ingress, %d egress)\n", iax_get_frames(), iax_get_iframes(), iax_get_oframes());
	ast_cli(a->fd, "%d timed and %d untimed transmits; MTU %d/%d/%d\n", trunk_timed, trunk_untimed,
		trunk_maxmtu, trunk_nmaxmtu, global_max_trunk_mtu);
	ast_cli(a->fd, "Packets in transmit queue: %d dead, %d final, %d total\n\n", dead, final, cnt);

	trunk_timed = trunk_untimed = 0;
	if (trunk_maxmtu > trunk_nmaxmtu)
		trunk_nmaxmtu = trunk_maxmtu;

	return CLI_SUCCESS;
}

/*! \brief Set trunk MTU from CLI */
static char *handle_cli_iax2_set_mtu(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int mtuv;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 set mtu";
		e->usage =
			"Usage: iax2 set mtu <value>\n"
			"       Set the system-wide IAX IP mtu to <value> bytes net or\n"
			"       zero to disable. Disabling means that the operating system\n"
			"       must handle fragmentation of UDP packets when the IAX2 trunk\n"
			"       packet exceeds the UDP payload size. This is substantially\n"
			"       below the IP mtu. Try 1240 on ethernets. Must be 172 or\n"
			"       greater for G.711 samples.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	if (strncasecmp(a->argv[3], "default", strlen(a->argv[3])) == 0)
		mtuv = MAX_TRUNK_MTU;
	else
		mtuv = atoi(a->argv[3]);

	if (mtuv == 0) {
		ast_cli(a->fd, "Trunk MTU control disabled (mtu was %d)\n", global_max_trunk_mtu);
		global_max_trunk_mtu = 0;
		return CLI_SUCCESS;
	}
	if (mtuv < 172 || mtuv > 4000) {
		ast_cli(a->fd, "Trunk MTU must be between 172 and 4000\n");
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, "Trunk MTU changed from %d to %d\n", global_max_trunk_mtu, mtuv);
	global_max_trunk_mtu = mtuv;
	return CLI_SUCCESS;
}

static char *handle_cli_iax2_show_cache(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct iax2_dpcache *dp = NULL;
	char tmp[1024], *pc = NULL;
	int s, x, y;
	struct timeval now = ast_tvnow();

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show cache";
		e->usage =
			"Usage: iax2 show cache\n"
			"       Display currently cached IAX Dialplan results.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_LIST_LOCK(&dpcache);

	ast_cli(a->fd, "%-20.20s %-12.12s %-9.9s %-8.8s %s\n", "Peer/Context", "Exten", "Exp.", "Wait.", "Flags");

	AST_LIST_TRAVERSE(&dpcache, dp, cache_list) {
		s = dp->expiry.tv_sec - now.tv_sec;
		tmp[0] = '\0';
		if (dp->flags & CACHE_FLAG_EXISTS)
			strncat(tmp, "EXISTS|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_NONEXISTENT)
			strncat(tmp, "NONEXISTENT|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_CANEXIST)
			strncat(tmp, "CANEXIST|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_PENDING)
			strncat(tmp, "PENDING|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_TIMEOUT)
			strncat(tmp, "TIMEOUT|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_TRANSMITTED)
			strncat(tmp, "TRANSMITTED|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_MATCHMORE)
			strncat(tmp, "MATCHMORE|", sizeof(tmp) - strlen(tmp) - 1);
		if (dp->flags & CACHE_FLAG_UNKNOWN)
			strncat(tmp, "UNKNOWN|", sizeof(tmp) - strlen(tmp) - 1);
		/* Trim trailing pipe */
		if (!ast_strlen_zero(tmp)) {
			tmp[strlen(tmp) - 1] = '\0';
		} else {
			ast_copy_string(tmp, "(none)", sizeof(tmp));
		}
		y = 0;
		pc = strchr(dp->peercontext, '@');
		if (!pc) {
			pc = dp->peercontext;
		} else {
			pc++;
		}
		for (x = 0; x < ARRAY_LEN(dp->waiters); x++) {
			if (dp->waiters[x] > -1)
				y++;
		}
		if (s > 0) {
			ast_cli(a->fd, "%-20.20s %-12.12s %-9d %-8d %s\n", pc, dp->exten, s, y, tmp);
		} else {
			ast_cli(a->fd, "%-20.20s %-12.12s %-9.9s %-8d %s\n", pc, dp->exten, "(expired)", y, tmp);
		}
	}

	AST_LIST_UNLOCK(&dpcache);

	return CLI_SUCCESS;
}

static unsigned int calc_rxstamp(struct chan_iax2_pvt *p, unsigned int offset);

static void unwrap_timestamp(struct iax_frame *fr)
{
	/* Video mini frames only encode the lower 15 bits of the session
	 * timestamp, but other frame types (e.g. audio) encode 16 bits. */
	const int ts_shift = (fr->af.frametype == AST_FRAME_VIDEO) ? 15 : 16;
	const int lower_mask = (1 << ts_shift) - 1;
	const int upper_mask = ~lower_mask;
	const int last_upper = iaxs[fr->callno]->last & upper_mask;

	if ( (fr->ts & upper_mask) == last_upper ) {
		const int x = fr->ts - iaxs[fr->callno]->last;
		const int threshold = (ts_shift == 15) ? 25000 : 50000;

		if (x < -threshold) {
			/* Sudden big jump backwards in timestamp:
			   What likely happened here is that miniframe timestamp has circled but we haven't
			   gotten the update from the main packet.  We'll just pretend that we did, and
			   update the timestamp appropriately. */
			fr->ts = (last_upper + (1 << ts_shift)) | (fr->ts & lower_mask);
			if (iaxdebug)
				ast_debug(1, "schedule_delivery: pushed forward timestamp\n");
		} else if (x > threshold) {
			/* Sudden apparent big jump forwards in timestamp:
			   What's likely happened is this is an old miniframe belonging to the previous
			   top 15 or 16-bit timestamp that has turned up out of order.
			   Adjust the timestamp appropriately. */
			fr->ts = (last_upper - (1 << ts_shift)) | (fr->ts & lower_mask);
			if (iaxdebug)
				ast_debug(1, "schedule_delivery: pushed back timestamp\n");
		}
	}
}

static int get_from_jb(const void *p);

static void update_jbsched(struct chan_iax2_pvt *pvt)
{
	int when;

	when = ast_tvdiff_ms(ast_tvnow(), pvt->rxcore);

	when = jb_next(pvt->jb) - when;

	if (when <= 0) {
		/* XXX should really just empty until when > 0.. */
		when = 1;
	}

	pvt->jbid = iax2_sched_replace(pvt->jbid, sched, when, get_from_jb,
		CALLNO_TO_PTR(pvt->callno));
}

static void __get_from_jb(const void *p)
{
	int callno = PTR_TO_CALLNO(p);
	struct chan_iax2_pvt *pvt = NULL;
	struct iax_frame *fr;
	jb_frame frame;
	int ret;
	long ms;
	long next;
	struct timeval now = ast_tvnow();

	/* Make sure we have a valid private structure before going on */
	ast_mutex_lock(&iaxsl[callno]);
	pvt = iaxs[callno];
	if (!pvt) {
		/* No go! */
		ast_mutex_unlock(&iaxsl[callno]);
		return;
	}

	pvt->jbid = -1;

	/* round up a millisecond since ast_sched_runq does; */
	/* prevents us from spinning while waiting for our now */
	/* to catch up with runq's now */
	now.tv_usec += 1000;

	ms = ast_tvdiff_ms(now, pvt->rxcore);

	if(ms >= (next = jb_next(pvt->jb))) {
		struct ast_format *voicefmt;
		voicefmt = ast_format_compatibility_bitfield2format(pvt->voiceformat);
		ret = jb_get(pvt->jb, &frame, ms, voicefmt ? ast_format_get_default_ms(voicefmt) : 20);
		switch(ret) {
		case JB_OK:
			fr = frame.data;
			__do_deliver(fr);
			/* __do_deliver() can cause the call to disappear */
			pvt = iaxs[callno];
			break;
		case JB_INTERP:
		{
			struct ast_frame af = { 0, };

			/* create an interpolation frame */
			af.frametype = AST_FRAME_VOICE;
			af.subclass.format = voicefmt;
			af.samples  = frame.ms * (ast_format_get_sample_rate(voicefmt) / 1000);
			af.src  = "IAX2 JB interpolation";
			af.delivery = ast_tvadd(pvt->rxcore, ast_samp2tv(next, 1000));
			af.offset = AST_FRIENDLY_OFFSET;

			/* queue the frame:  For consistency, we would call __do_deliver here, but __do_deliver wants an iax_frame,
			 * which we'd need to malloc, and then it would free it.  That seems like a drag */
			if (!ast_test_flag64(iaxs[callno], IAX_ALREADYGONE)) {
				iax2_queue_frame(callno, &af);
				/* iax2_queue_frame() could cause the call to disappear */
				pvt = iaxs[callno];
			}
		}
			break;
		case JB_DROP:
			iax2_frame_free(frame.data);
			break;
		case JB_NOFRAME:
		case JB_EMPTY:
			/* do nothing */
			break;
		default:
			/* shouldn't happen */
			break;
		}
	}
	if (pvt)
		update_jbsched(pvt);
	ast_mutex_unlock(&iaxsl[callno]);
}

static int get_from_jb(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__get_from_jb, data))
#endif
		__get_from_jb(data);
	return 0;
}

/*!
 * \note This function assumes fr->callno is locked
 *
 * \note IMPORTANT NOTE!!! Any time this function is used, even if iaxs[callno]
 * was valid before calling it, it may no longer be valid after calling it.
 */
static int schedule_delivery(struct iax_frame *fr, int updatehistory, int fromtrunk, unsigned int *tsout)
{
	int type, len;
	int ret;
	int needfree = 0;

	/*
	 * Clear fr->af.data if there is no data in the buffer.  Things
	 * like AST_CONTROL_HOLD without a suggested music class must
	 * have a NULL pointer.
	 */
	if (!fr->af.datalen) {
		memset(&fr->af.data, 0, sizeof(fr->af.data));
	}

	/* Attempt to recover wrapped timestamps */
	unwrap_timestamp(fr);

	/* delivery time is sender's sent timestamp converted back into absolute time according to our clock */
	if ( !fromtrunk && !ast_tvzero(iaxs[fr->callno]->rxcore))
		fr->af.delivery = ast_tvadd(iaxs[fr->callno]->rxcore, ast_samp2tv(fr->ts, 1000));
	else {
#if 0
		ast_debug(1, "schedule_delivery: set delivery to 0 as we don't have an rxcore yet, or frame is from trunk.\n");
#endif
		fr->af.delivery = ast_tv(0,0);
	}

	type = JB_TYPE_CONTROL;
	len = 0;

	if(fr->af.frametype == AST_FRAME_VOICE) {
		type = JB_TYPE_VOICE;
		len = ast_codec_samples_count(&fr->af) / (ast_format_get_sample_rate(fr->af.subclass.format) / 1000);
	} else if(fr->af.frametype == AST_FRAME_CNG) {
		type = JB_TYPE_SILENCE;
	}

	if ( (!ast_test_flag64(iaxs[fr->callno], IAX_USEJITTERBUF)) ) {
		if (tsout)
			*tsout = fr->ts;
		__do_deliver(fr);
		return -1;
	}

	/* insert into jitterbuffer */
	/* TODO: Perhaps we could act immediately if it's not droppable and late */
	ret = jb_put(iaxs[fr->callno]->jb, fr, type, len, fr->ts,
			calc_rxstamp(iaxs[fr->callno],fr->ts));
	if (ret == JB_DROP) {
		needfree++;
	} else if (ret == JB_SCHED) {
		update_jbsched(iaxs[fr->callno]);
	}
	if (tsout)
		*tsout = fr->ts;
	if (needfree) {
		/* Free our iax frame */
		iax2_frame_free(fr);
		return -1;
	}
	return 0;
}

static int transmit_frame(void *data)
{
	struct iax_frame *fr = data;

	ast_mutex_lock(&iaxsl[fr->callno]);

	fr->sentyet = 1;

	if (iaxs[fr->callno]) {
		send_packet(fr);
	}

	if (fr->retries < 0) {
		ast_mutex_unlock(&iaxsl[fr->callno]);
		/* No retransmit requested */
		iax_frame_free(fr);
	} else {
		/* We need reliable delivery.  Schedule a retransmission */
		AST_LIST_INSERT_TAIL(&frame_queue[fr->callno], fr, list);
		fr->retries++;
		fr->retrans = iax2_sched_add(sched, fr->retrytime, attempt_transmit, fr);
		ast_mutex_unlock(&iaxsl[fr->callno]);
	}

	return 0;
}

static int iax2_transmit(struct iax_frame *fr)
{
	fr->sentyet = 0;

	return ast_taskprocessor_push(transmit_processor, transmit_frame, fr);
}

static int iax2_digit_begin(struct ast_channel *c, char digit)
{
	return send_command_locked(PTR_TO_CALLNO(ast_channel_tech_pvt(c)), AST_FRAME_DTMF_BEGIN, digit, 0, NULL, 0, -1);
}

static int iax2_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	return send_command_locked(PTR_TO_CALLNO(ast_channel_tech_pvt(c)), AST_FRAME_DTMF_END, digit, 0, NULL, 0, -1);
}

static int iax2_sendtext(struct ast_channel *c, const char *text)
{

	return send_command_locked(PTR_TO_CALLNO(ast_channel_tech_pvt(c)), AST_FRAME_TEXT,
		0, 0, (unsigned char *)text, strlen(text) + 1, -1);
}

static int iax2_sendimage(struct ast_channel *c, struct ast_frame *img)
{
	return send_command_locked(PTR_TO_CALLNO(ast_channel_tech_pvt(c)), AST_FRAME_IMAGE, img->subclass.integer, 0, img->data.ptr, img->datalen, -1);
}

static int iax2_sendhtml(struct ast_channel *c, int subclass, const char *data, int datalen)
{
	return send_command_locked(PTR_TO_CALLNO(ast_channel_tech_pvt(c)), AST_FRAME_HTML, subclass, 0, (unsigned char *)data, datalen, -1);
}

static int iax2_fixup(struct ast_channel *oldchannel, struct ast_channel *newchan)
{
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(newchan));
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno])
		iaxs[callno]->owner = newchan;
	else
		ast_log(LOG_WARNING, "Uh, this isn't a good sign...\n");
	ast_mutex_unlock(&iaxsl[callno]);
	return 0;
}

/*!
 * \note This function calls reg_source_db -> iax2_poke_peer -> find_callno,
 *       so do not call this with a pvt lock held.
 */
static struct iax2_peer *realtime_peer(const char *peername, struct ast_sockaddr *addr)
{
	struct ast_variable *var = NULL;
	struct ast_variable *tmp;
	struct iax2_peer *peer=NULL;
	time_t regseconds = 0, nowtime;
	int dynamic=0;
	char *str_addr, *str_port;

	str_addr = ast_strdupa(ast_sockaddr_stringify_addr(addr));
	str_port = ast_strdupa(ast_sockaddr_stringify_port(addr));

	if (peername) {
		var = ast_load_realtime("iaxpeers", "name", peername, "host", "dynamic", SENTINEL);
		if (!var && !ast_sockaddr_isnull(addr)) {
			var = ast_load_realtime("iaxpeers", "name", peername, "host", str_addr, SENTINEL);
		}
	} else if (!ast_sockaddr_isnull(addr)) {
		var = ast_load_realtime("iaxpeers", "ipaddr", str_addr, "port", str_port, SENTINEL);
		if (var) {
			/* We'll need the peer name in order to build the structure! */
			for (tmp = var; tmp; tmp = tmp->next) {
				if (!strcasecmp(tmp->name, "name"))
					peername = tmp->value;
			}
		}
	}
	if (!var && peername) { /* Last ditch effort */
		var = ast_load_realtime("iaxpeers", "name", peername, SENTINEL);
		/*!\note
		 * If this one loaded something, then we need to ensure that the host
		 * field matched.  The only reason why we can't have this as a criteria
		 * is because we only have the IP address and the host field might be
		 * set as a name (and the reverse PTR might not match).
		 */
		if (var && !ast_sockaddr_isnull(addr)) {
			for (tmp = var; tmp; tmp = tmp->next) {
				if (!strcasecmp(tmp->name, "host")) {
					struct ast_sockaddr *hostaddr = NULL;

					if (!ast_sockaddr_resolve(&hostaddr, tmp->value, PARSE_PORT_FORBID, AST_AF_UNSPEC)
						|| ast_sockaddr_cmp_addr(hostaddr, addr)) {
						/* No match */
						ast_variables_destroy(var);
						var = NULL;
					}
					ast_free(hostaddr);
					break;
				}
			}
		}
	}
	if (!var)
		return NULL;

	peer = build_peer(peername, var, NULL, ast_test_flag64((&globalflags), IAX_RTCACHEFRIENDS) ? 0 : 1);

	if (!peer) {
		ast_variables_destroy(var);
		return NULL;
	}

	for (tmp = var; tmp; tmp = tmp->next) {
		/* Make sure it's not a user only... */
		if (!strcasecmp(tmp->name, "type")) {
			if (strcasecmp(tmp->value, "friend") &&
			    strcasecmp(tmp->value, "peer")) {
				/* Whoops, we weren't supposed to exist! */
				peer = peer_unref(peer);
				break;
			}
		} else if (!strcasecmp(tmp->name, "regseconds")) {
			ast_get_time_t(tmp->value, &regseconds, 0, NULL);
		} else if (!strcasecmp(tmp->name, "ipaddr")) {
			int setport = ast_sockaddr_port(&peer->addr);
			if (ast_parse_arg(tmp->value, PARSE_ADDR | PARSE_PORT_FORBID, NULL)) {
				ast_log(LOG_WARNING, "Failed to parse sockaddr '%s' for ipaddr of realtime peer '%s'\n", tmp->value, tmp->name);
			} else {
				ast_sockaddr_parse(&peer->addr, tmp->value, 0);
			}
			ast_sockaddr_set_port(&peer->addr, setport);
		} else if (!strcasecmp(tmp->name, "port")) {
			int bindport;
			if (ast_parse_arg(tmp->value, PARSE_UINT32 | PARSE_IN_RANGE, &bindport, 0, 65535)) {
				bindport = IAX_DEFAULT_PORTNO;
			}
			ast_sockaddr_set_port(&peer->addr, bindport);
		} else if (!strcasecmp(tmp->name, "host")) {
			if (!strcasecmp(tmp->value, "dynamic"))
				dynamic = 1;
		}
	}

	ast_variables_destroy(var);

	if (ast_test_flag64((&globalflags), IAX_RTCACHEFRIENDS)) {
		ast_copy_flags64(peer, &globalflags, IAX_RTAUTOCLEAR|IAX_RTCACHEFRIENDS);
		if (ast_test_flag64(peer, IAX_RTAUTOCLEAR)) {
			if (peer->expire > -1) {
				if (!AST_SCHED_DEL(sched, peer->expire)) {
					peer->expire = -1;
					peer_unref(peer);
				}
			}
			peer->expire = iax2_sched_add(sched, (global_rtautoclear) * 1000, expire_registry, peer_ref(peer));
			if (peer->expire == -1)
				peer_unref(peer);
		}
		ao2_link(peers, peer);
		if (ast_test_flag64(peer, IAX_DYNAMIC))
			reg_source_db(peer);
	} else {
		ast_set_flag64(peer, IAX_TEMPONLY);
	}

	if (!ast_test_flag64(&globalflags, IAX_RTIGNOREREGEXPIRE) && dynamic) {
		time(&nowtime);
		if ((nowtime - regseconds) > IAX_DEFAULT_REG_EXPIRE) {
			memset(&peer->addr, 0, sizeof(peer->addr));
			realtime_update_peer(peer->name, &peer->addr, 0);
			ast_debug(1, "realtime_peer: Bah, '%s' is expired (%d/%d/%d)!\n",
				peername, (int)(nowtime - regseconds), (int)regseconds, (int)nowtime);
		}
		else {
			ast_debug(1, "realtime_peer: Registration for '%s' still active (%d/%d/%d)!\n",
				peername, (int)(nowtime - regseconds), (int)regseconds, (int)nowtime);
		}
	}

	return peer;
}

static struct iax2_user *realtime_user(const char *username, struct ast_sockaddr *addr)
{
	struct ast_variable *var;
	struct ast_variable *tmp;
	struct iax2_user *user=NULL;
	char *str_addr, *str_port;

	str_addr = ast_strdupa(ast_sockaddr_stringify_addr(addr));
	str_port = ast_strdupa(ast_sockaddr_stringify_port(addr));

	var = ast_load_realtime("iaxusers", "name", username, "host", "dynamic", SENTINEL);
	if (!var)
		var = ast_load_realtime("iaxusers", "name", username, "host", str_addr, SENTINEL);
	if (!var && !ast_sockaddr_isnull(addr)) {
		var = ast_load_realtime("iaxusers", "name", username, "ipaddr", str_addr, "port", str_port, SENTINEL);
		if (!var)
			var = ast_load_realtime("iaxusers", "ipaddr", str_addr, "port", str_port, SENTINEL);
	}
	if (!var) { /* Last ditch effort */
		var = ast_load_realtime("iaxusers", "name", username, SENTINEL);
		/*!\note
		 * If this one loaded something, then we need to ensure that the host
		 * field matched.  The only reason why we can't have this as a criteria
		 * is because we only have the IP address and the host field might be
		 * set as a name (and the reverse PTR might not match).
		 */
		if (var) {
			for (tmp = var; tmp; tmp = tmp->next) {
				if (!strcasecmp(tmp->name, "host")) {
					struct ast_sockaddr *hostaddr = NULL;

					if (!ast_sockaddr_resolve(&hostaddr, tmp->value, PARSE_PORT_FORBID, AST_AF_UNSPEC)
						|| ast_sockaddr_cmp_addr(hostaddr, addr)) {
						/* No match */
						ast_variables_destroy(var);
						var = NULL;
					}
					ast_free(hostaddr);
					break;
				}
			}
		}
	}
	if (!var)
		return NULL;

	tmp = var;
	while(tmp) {
		/* Make sure it's not a peer only... */
		if (!strcasecmp(tmp->name, "type")) {
			if (strcasecmp(tmp->value, "friend") &&
			    strcasecmp(tmp->value, "user")) {
				return NULL;
			}
		}
		tmp = tmp->next;
	}

	user = build_user(username, var, NULL, !ast_test_flag64((&globalflags), IAX_RTCACHEFRIENDS));

	ast_variables_destroy(var);

	if (!user)
		return NULL;

	if (ast_test_flag64((&globalflags), IAX_RTCACHEFRIENDS)) {
		ast_set_flag64(user, IAX_RTCACHEFRIENDS);
		ao2_link(users, user);
	} else {
		ast_set_flag64(user, IAX_TEMPONLY);
	}

	return user;
}

static void realtime_update_peer(const char *peername, struct ast_sockaddr *sockaddr, time_t regtime)
{
	char regseconds[20];
	const char *sysname = ast_config_AST_SYSTEM_NAME;
	char *syslabel = NULL;
	char *port;

	if (ast_strlen_zero(sysname))	/* No system name, disable this */
		sysname = NULL;
	else if (ast_test_flag64(&globalflags, IAX_RTSAVE_SYSNAME))
		syslabel = "regserver";

	snprintf(regseconds, sizeof(regseconds), "%d", (int)regtime);
	port = ast_strdupa(ast_sockaddr_stringify_port(sockaddr));
	ast_update_realtime("iaxpeers", "name", peername,
		"ipaddr", ast_sockaddr_isnull(sockaddr) ? "" : ast_sockaddr_stringify_addr(sockaddr),
		"port", ast_sockaddr_isnull(sockaddr) ? "" : port,
		"regseconds", regseconds, syslabel, sysname, SENTINEL); /* note syslable can be NULL */
}

struct create_addr_info {
	iax2_format capability;
	uint64_t flags;
	struct iax2_codec_pref prefs;
	int maxtime;
	int encmethods;
	int found;
	int sockfd;
	int adsi;
	char username[80];
	char secret[80];
	char outkey[80];
	char timezone[80];
	char cid_num[80];
	char cid_name[80];
	char context[AST_MAX_CONTEXT];
	char peercontext[AST_MAX_CONTEXT];
	char mohinterpret[MAX_MUSICCLASS];
	char mohsuggest[MAX_MUSICCLASS];
};

static int create_addr(const char *peername, struct ast_channel *c, struct ast_sockaddr *addr, struct create_addr_info *cai)
{
	struct iax2_peer *peer;
	int res = -1;

	ast_clear_flag64(cai, IAX_SENDANI | IAX_TRUNK);
	cai->sockfd = defaultsockfd;
	cai->maxtime = 0;

	if (!(peer = find_peer(peername, 1))) {
		struct ast_sockaddr peer_addr;

		peer_addr.ss.ss_family = AST_AF_UNSPEC;
		cai->found = 0;
		if (ast_get_ip_or_srv(&peer_addr, peername, srvlookup ? "_iax._udp" : NULL)) {
			ast_log(LOG_WARNING, "No such host: %s\n", peername);
			return -1;
		}

		if (!ast_sockaddr_port(&peer_addr)) {
			ast_sockaddr_set_port(&peer_addr, IAX_DEFAULT_PORTNO);
		}

		ast_sockaddr_copy(addr, &peer_addr);
		/*
		 * Use The global iax prefs for unknown peer/user.
		 * However, move the calling channel's native codec to
		 * the top of the preference list.
		 */
		cai->prefs = prefs_global;
		if (c) {
			int i;

			for (i = 0; i < ast_format_cap_count(ast_channel_nativeformats(c)); i++) {
				struct ast_format *format = ast_format_cap_get_format(
					ast_channel_nativeformats(c), i);
				iax2_codec_pref_prepend(&cai->prefs, format,
					ast_format_cap_get_format_framing(ast_channel_nativeformats(c), format),
					1);
				ao2_ref(format, -1);
			}
		}
		return 0;
	}

	cai->found = 1;

	/* if the peer has no address (current or default), return failure */
	if (ast_sockaddr_isnull(&peer->addr) && ast_sockaddr_isnull(&peer->defaddr)) {
		goto return_unref;
	}

	/* if the peer is being monitored and is currently unreachable, return failure */
	if (peer->maxms && ((peer->lastms > peer->maxms) || (peer->lastms < 0)))
		goto return_unref;

	ast_copy_flags64(cai, peer, IAX_SENDANI | IAX_TRUNK | IAX_NOTRANSFER | IAX_TRANSFERMEDIA | IAX_USEJITTERBUF | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE | IAX_FORCE_ENCRYPT);
	cai->maxtime = peer->maxms;
	cai->capability = peer->capability;
	cai->encmethods = peer->encmethods;
	cai->sockfd = peer->sockfd;
	cai->adsi = peer->adsi;
	cai->prefs = peer->prefs;
	/* Move the calling channel's native codec to the top of the preference list */
	if (c) {
		int i;

		for (i = 0; i < ast_format_cap_count(ast_channel_nativeformats(c)); i++) {
			struct ast_format *tmpfmt = ast_format_cap_get_format(
				ast_channel_nativeformats(c), i);
			iax2_codec_pref_prepend(&cai->prefs, tmpfmt,
				ast_format_cap_get_format_framing(ast_channel_nativeformats(c), tmpfmt),
				1);
			ao2_ref(tmpfmt, -1);
		}
	}
	ast_copy_string(cai->context, peer->context, sizeof(cai->context));
	ast_copy_string(cai->peercontext, peer->peercontext, sizeof(cai->peercontext));
	ast_copy_string(cai->username, peer->username, sizeof(cai->username));
	ast_copy_string(cai->timezone, peer->zonetag, sizeof(cai->timezone));
	ast_copy_string(cai->outkey, peer->outkey, sizeof(cai->outkey));
	ast_copy_string(cai->cid_num, peer->cid_num, sizeof(cai->cid_num));
	ast_copy_string(cai->cid_name, peer->cid_name, sizeof(cai->cid_name));
	ast_copy_string(cai->mohinterpret, peer->mohinterpret, sizeof(cai->mohinterpret));
	ast_copy_string(cai->mohsuggest, peer->mohsuggest, sizeof(cai->mohsuggest));
	if (ast_strlen_zero(peer->dbsecret)) {
		ast_copy_string(cai->secret, peer->secret, sizeof(cai->secret));
	} else {
		char *family;
		char *key = NULL;

		family = ast_strdupa(peer->dbsecret);
		key = strchr(family, '/');
		if (key)
			*key++ = '\0';
		if (!key || ast_db_get(family, key, cai->secret, sizeof(cai->secret))) {
			ast_log(LOG_WARNING, "Unable to retrieve database password for family/key '%s'!\n", peer->dbsecret);
			goto return_unref;
		}
	}

	if (!ast_sockaddr_isnull(&peer->addr)) {
		ast_sockaddr_copy(addr, &peer->addr);
	} else {
		ast_sockaddr_copy(addr, &peer->defaddr);
	}

	res = 0;

return_unref:
	peer_unref(peer);

	return res;
}

static void __auto_congest(const void *nothing)
{
	int callno = PTR_TO_CALLNO(nothing);
	struct ast_frame f = { AST_FRAME_CONTROL, { AST_CONTROL_CONGESTION } };
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		iaxs[callno]->initid = -1;
		iax2_queue_frame(callno, &f);
		ast_log(LOG_NOTICE, "Auto-congesting call due to slow response\n");
	}
	ast_mutex_unlock(&iaxsl[callno]);
}

static int auto_congest(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__auto_congest, data))
#endif
		__auto_congest(data);
	return 0;
}

static unsigned int iax2_datetime(const char *tz)
{
	struct timeval t = ast_tvnow();
	struct ast_tm tm;
	unsigned int tmp;
	ast_localtime(&t, &tm, ast_strlen_zero(tz) ? NULL : tz);
	tmp  = (tm.tm_sec >> 1) & 0x1f;			/* 5 bits of seconds */
	tmp |= (tm.tm_min & 0x3f) << 5;			/* 6 bits of minutes */
	tmp |= (tm.tm_hour & 0x1f) << 11;		/* 5 bits of hours */
	tmp |= (tm.tm_mday & 0x1f) << 16;		/* 5 bits of day of month */
	tmp |= ((tm.tm_mon + 1) & 0xf) << 21;		/* 4 bits of month */
	tmp |= ((tm.tm_year - 100) & 0x7f) << 25;	/* 7 bits of year */
	return tmp;
}

struct parsed_dial_string {
	char *username;
	char *password;
	char *key;
	char *peer;
	char *port;
	char *exten;
	char *context;
	char *options;
};

static int send_apathetic_reply(unsigned short callno, unsigned short dcallno,
		struct ast_sockaddr *addr, int command, int ts, unsigned char seqno,
		int sockfd, struct iax_ie_data *ied)
{
	struct {
		struct ast_iax2_full_hdr f;
		struct iax_ie_data ied;
	} data;
	size_t size = sizeof(struct ast_iax2_full_hdr);

	if (ied) {
		size += ied->pos;
		memcpy(&data.ied, ied->buf, ied->pos);
	}

	data.f.scallno = htons(0x8000 | callno);
	data.f.dcallno = htons(dcallno & ~IAX_FLAG_RETRANS);
	data.f.ts = htonl(ts);
	data.f.iseqno = seqno;
	data.f.oseqno = 0;
	data.f.type = AST_FRAME_IAX;
	data.f.csub = compress_subclass(command);

	iax_outputframe(NULL, &data.f, 0, addr, size - sizeof(struct ast_iax2_full_hdr));

	return ast_sendto(sockfd, &data, size, 0, addr);
}

static void add_empty_calltoken_ie(struct chan_iax2_pvt *pvt, struct iax_ie_data *ied)
{
	/* first make sure their are two empty bytes left in ied->buf */
	if (pvt && ied && (2 < ((int) sizeof(ied->buf) - ied->pos))) {
		ied->buf[ied->pos++] = IAX_IE_CALLTOKEN;  /* type */
		ied->buf[ied->pos++] = 0;   /* data size,  ZERO in this case */
		pvt->calltoken_ie_len = 2;
	}
}

static void resend_with_token(int callno, struct iax_frame *f, const char *newtoken)
{
	struct chan_iax2_pvt *pvt = iaxs[callno];
	int frametype = f->af.frametype;
	int subclass = f->af.subclass.integer;
	struct {
		struct ast_iax2_full_hdr fh;
		struct iax_ie_data ied;
	} data = {
		.ied.buf = { 0 },
		.ied.pos = 0,
	};
	/* total len - header len gives us the frame's IE len */
	int ie_data_pos = f->datalen - sizeof(struct ast_iax2_full_hdr);

	if (!pvt) {
		return;  /* this should not be possible if called from socket_process() */
	}

	/*
	 * Check to make sure last frame sent is valid for call token resend
	 * 1. Frame should _NOT_ be encrypted since it starts the IAX dialog
	 * 2. Frame should _NOT_ already have a destination callno
	 * 3. Frame must be a valid iax_frame subclass capable of starting dialog
	 * 4. Pvt must have a calltoken_ie_len which represents the number of
	 *    bytes at the end of the frame used for the previous calltoken ie.
	 * 5. Pvt's calltoken_ie_len must be _LESS_ than the total IE length
	 * 6. Total length of f->data must be _LESS_ than size of our data struct
	 *    because f->data must be able to fit within data.
	 */
	if (f->encmethods || f->dcallno || !iax2_allow_new(frametype, subclass, 0)
		|| !pvt->calltoken_ie_len || (pvt->calltoken_ie_len > ie_data_pos) ||
		(f->datalen > sizeof(data))) {

		return;  /* ignore resend, token was not valid for the dialog */
	}

	/* token is valid
	 * 1. Copy frame data over
	 * 2. Redo calltoken IE, it will always be the last ie in the frame.
	 *    NOTE: Having the ie always be last is not protocol specified,
	 *    it is only an implementation choice.  Since we only expect the ie to
	 *    be last for frames we have sent, this can no way be affected by
	 *    another end point.
	 * 3. Remove frame from queue
	 * 4. Free old frame
	 * 5. Clear previous seqnos
	 * 6. Resend with CALLTOKEN ie.
	 */

	/* ---1.--- */
	memcpy(&data, f->data, f->datalen);
	data.ied.pos = ie_data_pos;

	/* ---2.--- */
	/* move to the beginning of the calltoken ie so we can write over it */
	data.ied.pos -= pvt->calltoken_ie_len;
	iax_ie_append_str(&data.ied, IAX_IE_CALLTOKEN, newtoken);

	/* make sure to update token length incase it ever has to be stripped off again */
	pvt->calltoken_ie_len = data.ied.pos - ie_data_pos; /* new pos minus old pos tells how big token ie is */

	/* ---3.--- */
	AST_LIST_REMOVE(&frame_queue[callno], f, list);

	/* ---4.--- */
	iax2_frame_free(f);

	/* ---5.--- */
	pvt->oseqno = 0;
	pvt->rseqno = 0;
	pvt->iseqno = 0;
	pvt->aseqno = 0;
	if (pvt->peercallno) {
		remove_by_peercallno(pvt);
		pvt->peercallno = 0;
	}

	/* ---6.--- */
	send_command(pvt, AST_FRAME_IAX, subclass, 0, data.ied.buf, data.ied.pos, -1);
}

static void requirecalltoken_mark_auto(const char *name, int subclass)
{
	struct iax2_user *user = NULL;
	struct iax2_peer *peer = NULL;

	if (ast_strlen_zero(name)) {
		return; /* no username given */
	}

	if ((subclass == IAX_COMMAND_NEW) && (user = find_user(name)) && (user->calltoken_required == CALLTOKEN_AUTO)) {
		user->calltoken_required = CALLTOKEN_YES;
	} else if ((subclass != IAX_COMMAND_NEW) && (peer = find_peer(name, 1)) && (peer->calltoken_required == CALLTOKEN_AUTO)) {
		peer->calltoken_required = CALLTOKEN_YES;
	}

	if (peer) {
		peer_unref(peer);
	}
	if (user) {
		user_unref(user);
	}
}

/*!
 * \internal
 *
 * \brief handles calltoken logic for a received iax_frame.
 *
 * \note frametype must be AST_FRAME_IAX.
 *
 * \note
 * Three different cases are possible here.
 * Case 1. An empty calltoken is provided. This means the client supports
 *         calltokens but has not yet received one from us.  In this case
 *         a full calltoken IE is created and sent in a calltoken fullframe.
 * Case 2. A full calltoken is received and must be checked for validity.
 * Case 3. No calltoken is received indicating that the client does not
 *         support calltokens.  In this case it is up to the configuration
 *         to decide how this should be handled (reject or permit without calltoken)
 */
static int handle_call_token(struct ast_iax2_full_hdr *fh, struct iax_ies *ies,
		struct ast_sockaddr *addr, int fd)
{
#define CALLTOKEN_HASH_FORMAT "%s%u%d"  /* address + port + ts + randomcalldata */
#define CALLTOKEN_IE_FORMAT   "%u?%s"     /* time + ? + (40 char hash) */
	struct ast_str *buf = ast_str_alloca(256);
	time_t t = time(NULL);
	char hash[41]; /* 40 char sha1 hash */
	int subclass = uncompress_subclass(fh->csub);

	/* ----- Case 1 ----- */
	if (ies->calltoken && !ies->calltokendata) {  /* empty calltoken is provided, client supports calltokens */
		struct iax_ie_data ied = {
			.buf = { 0 },
			.pos = 0,
		};

		/* create the hash with their address data and our timestamp */
		ast_str_set(&buf, 0, CALLTOKEN_HASH_FORMAT, ast_sockaddr_stringify(addr), (unsigned int) t, randomcalltokendata);
		ast_sha1_hash(hash, ast_str_buffer(buf));

		ast_str_set(&buf, 0, CALLTOKEN_IE_FORMAT, (unsigned int) t, hash);
		iax_ie_append_str(&ied, IAX_IE_CALLTOKEN, ast_str_buffer(buf));
		send_apathetic_reply(1, ntohs(fh->scallno), addr, IAX_COMMAND_CALLTOKEN, ntohl(fh->ts), fh->iseqno + 1, fd, &ied);

		return 1;

	/* ----- Case 2 ----- */
	} else if (ies->calltoken && ies->calltokendata) { /* calltoken received, check to see if it is valid */
		char *rec_hash = NULL;    /* the received hash, make sure it matches with ours. */
		char *rec_ts = NULL;      /* received timestamp */
		unsigned int rec_time;  /* received time_t */

		/* split the timestamp from the hash data */
		rec_hash = strchr((char *) ies->calltokendata, '?');
		if (rec_hash) {
			*rec_hash++ = '\0';
			rec_ts = (char *) ies->calltokendata;
		}

		/* check that we have valid data before we do any comparisons */
		if (!rec_hash || !rec_ts) {
			goto reject;
		} else if (sscanf(rec_ts, "%u", &rec_time) != 1) {
			goto reject;
		}

		/* create a hash with their address and the _TOKEN'S_ timestamp */
		ast_str_set(&buf, 0, CALLTOKEN_HASH_FORMAT, ast_sockaddr_stringify(addr), (unsigned int) rec_time, randomcalltokendata);
		ast_sha1_hash(hash, ast_str_buffer(buf));

		/* compare hashes and then check timestamp delay */
		if (strcmp(hash, rec_hash)) {
			ast_log(LOG_WARNING, "Address %s failed CallToken hash inspection\n", ast_sockaddr_stringify(addr));
			goto reject; /* received hash does not match ours, reject */
		} else if ((t < rec_time) || ((t - rec_time) >= max_calltoken_delay)) {
			ast_log(LOG_WARNING, "Too much delay in IAX2 calltoken timestamp from address %s\n", ast_sockaddr_stringify(addr));
			goto reject; /* too much delay, reject */
		}

		/* at this point the call token is valid, returning 0
		 * will allow socket_process to continue as usual */
		requirecalltoken_mark_auto(ies->username, subclass);
		return 0;

	/* ----- Case 3 ----- */
	} else { /* calltokens are not supported for this client, how do we respond? */
		if (calltoken_required(addr, ies->username, subclass)) {
			ast_log(LOG_ERROR, "Call rejected, CallToken Support required. If unexpected, resolve by placing address %s in the calltokenoptional list or setting user %s requirecalltoken=no\n", ast_sockaddr_stringify(addr), S_OR(ies->username, "guest"));
			goto reject;
		}
		return 0; /* calltoken is not required for this addr, so permit it. */
	}

reject:
	/* received frame has failed calltoken inspection, send apathetic reject messages */
	if (subclass == IAX_COMMAND_REGREQ || subclass == IAX_COMMAND_REGREL) {
		send_apathetic_reply(1, ntohs(fh->scallno), addr, IAX_COMMAND_REGREJ, ntohl(fh->ts), fh->iseqno + 1, fd, NULL);
	} else {
		send_apathetic_reply(1, ntohs(fh->scallno), addr, IAX_COMMAND_REJECT, ntohl(fh->ts), fh->iseqno + 1, fd, NULL);
	}

	return 1;
}

/*!
 * \brief Parses an IAX dial string into its component parts.
 * \param data the string to be parsed
 * \param pds pointer to a \c struct \c parsed_dial_string to be filled in
 * \return nothing
 *
 * This function parses the string and fills the structure
 * with pointers to its component parts. The input string
 * will be modified.
 *
 * \note This function supports both plaintext passwords and RSA
 * key names; if the password string is formatted as '[keyname]',
 * then the keyname will be placed into the key field, and the
 * password field will be set to NULL.
 *
 * \note The dial string format is:
 *       [username[:password]@]peer[:port][/exten[@context]][/options]
 */
static void parse_dial_string(char *data, struct parsed_dial_string *pds)
{
	if (ast_strlen_zero(data))
		return;

	pds->peer = strsep(&data, "/");
	pds->exten = strsep(&data, "/");
	pds->options = data;

	if (pds->exten) {
		data = pds->exten;
		pds->exten = strsep(&data, "@");
		pds->context = data;
	}

	if (strchr(pds->peer, '@')) {
		data = pds->peer;
		pds->username = strsep(&data, "@");
		pds->peer = data;
	}

	if (pds->username) {
		data = pds->username;
		pds->username = strsep(&data, ":");
		pds->password = data;
	}

	data = pds->peer;
	pds->peer = strsep(&data, ":");
	pds->port = data;

	/*
	 * Check for a key name wrapped in [] in the password position.
	 * If found, move it to the key field instead.
	 */
	if (pds->password && (pds->password[0] == '[')) {
		pds->key = ast_strip_quoted(pds->password, "[", "]");
		pds->password = NULL;
	}
}

static int iax2_call(struct ast_channel *c, const char *dest, int timeout)
{
	struct ast_sockaddr addr;
	char *l=NULL, *n=NULL, *tmpstr;
	struct iax_ie_data ied;
	char *defaultrdest = "s";
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
	struct parsed_dial_string pds;
	struct create_addr_info cai;
	struct ast_var_t *var;
	struct ast_datastore *variablestore = ast_channel_datastore_find(c, &iax2_variable_datastore_info, NULL);
	const char* osp_token_ptr;
	unsigned int osp_token_length;
	unsigned char osp_block_index;
	unsigned int osp_block_length;
	unsigned char osp_buffer[256];
	char encoded_prefs[32];
	iax2_format iax2_tmpfmt;

	if ((ast_channel_state(c) != AST_STATE_DOWN) && (ast_channel_state(c) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "Channel is already in use (%s)?\n", ast_channel_name(c));
		return -1;
	}

	memset(&cai, 0, sizeof(cai));
	cai.encmethods = iax2_encryption;

	memset(&pds, 0, sizeof(pds));
	tmpstr = ast_strdupa(dest);
	parse_dial_string(tmpstr, &pds);

	if (ast_strlen_zero(pds.peer)) {
		ast_log(LOG_WARNING, "No peer provided in the IAX2 dial string '%s'\n", dest);
		return -1;
	}
	if (!pds.exten) {
		pds.exten = defaultrdest;
	}
	if (create_addr(pds.peer, c, &addr, &cai)) {
		ast_log(LOG_WARNING, "No address associated with '%s'\n", pds.peer);
		return -1;
	}
	if (ast_test_flag64(iaxs[callno], IAX_FORCE_ENCRYPT) && !cai.encmethods) {
		ast_log(LOG_WARNING, "Encryption forced for call, but not enabled\n");
		ast_channel_hangupcause_set(c, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		return -1;
	}
	if (ast_strlen_zero(cai.secret) && ast_test_flag64(iaxs[callno], IAX_FORCE_ENCRYPT)) {
		ast_log(LOG_WARNING, "Call terminated. No secret given and force encrypt enabled\n");
		return -1;
	}
	if (!pds.username && !ast_strlen_zero(cai.username))
		pds.username = cai.username;
	if (!pds.password && !ast_strlen_zero(cai.secret))
		pds.password = cai.secret;
	if (!pds.key && !ast_strlen_zero(cai.outkey))
		pds.key = cai.outkey;
	if (!pds.context && !ast_strlen_zero(cai.peercontext))
		pds.context = cai.peercontext;

	/* Keep track of the context for outgoing calls too */
	ast_channel_context_set(c, cai.context);

	if (pds.port) {
		int bindport;
		if (ast_parse_arg(pds.port, PARSE_UINT32 | PARSE_IN_RANGE, &bindport, 0, 65535)) {
			ast_sockaddr_set_port(&addr, bindport);
		}
	}

	l = ast_channel_connected(c)->id.number.valid ? ast_channel_connected(c)->id.number.str : NULL;
	n = ast_channel_connected(c)->id.name.valid ? ast_channel_connected(c)->id.name.str : NULL;

	/* Now build request */
	memset(&ied, 0, sizeof(ied));

	/* On new call, first IE MUST be IAX version of caller */
	iax_ie_append_short(&ied, IAX_IE_VERSION, IAX_PROTO_VERSION);
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, pds.exten);
	if (pds.options && strchr(pds.options, 'a')) {
		/* Request auto answer */
		iax_ie_append(&ied, IAX_IE_AUTOANSWER);
	}

	/* WARNING: this breaks down at 190 bits! */
	iax2_codec_pref_convert(&cai.prefs, encoded_prefs, sizeof(encoded_prefs), 1);
	iax_ie_append_str(&ied, IAX_IE_CODEC_PREFS, encoded_prefs);

	if (l) {
		iax_ie_append_str(&ied, IAX_IE_CALLING_NUMBER, l);
		iax_ie_append_byte(&ied, IAX_IE_CALLINGPRES,
			ast_party_id_presentation(&ast_channel_connected(c)->id));
	} else if (n) {
		iax_ie_append_byte(&ied, IAX_IE_CALLINGPRES,
			ast_party_id_presentation(&ast_channel_connected(c)->id));
	} else {
		iax_ie_append_byte(&ied, IAX_IE_CALLINGPRES, AST_PRES_NUMBER_NOT_AVAILABLE);
	}

	iax_ie_append_byte(&ied, IAX_IE_CALLINGTON, ast_channel_connected(c)->id.number.plan);
	iax_ie_append_short(&ied, IAX_IE_CALLINGTNS, ast_channel_dialed(c)->transit_network_select);

	if (n)
		iax_ie_append_str(&ied, IAX_IE_CALLING_NAME, n);
	if (ast_test_flag64(iaxs[callno], IAX_SENDANI)
		&& ast_channel_connected(c)->ani.number.valid
		&& ast_channel_connected(c)->ani.number.str) {
		iax_ie_append_str(&ied, IAX_IE_CALLING_ANI, ast_channel_connected(c)->ani.number.str);
	}

	if (!ast_strlen_zero(ast_channel_language(c)))
		iax_ie_append_str(&ied, IAX_IE_LANGUAGE, ast_channel_language(c));
	if (!ast_strlen_zero(ast_channel_dialed(c)->number.str)) {
		iax_ie_append_str(&ied, IAX_IE_DNID, ast_channel_dialed(c)->number.str);
	}
	if (ast_channel_redirecting(c)->from.number.valid
		&& !ast_strlen_zero(ast_channel_redirecting(c)->from.number.str)) {
		iax_ie_append_str(&ied, IAX_IE_RDNIS, ast_channel_redirecting(c)->from.number.str);
	}

	if (pds.context)
		iax_ie_append_str(&ied, IAX_IE_CALLED_CONTEXT, pds.context);

	if (pds.username)
		iax_ie_append_str(&ied, IAX_IE_USERNAME, pds.username);

	if (cai.encmethods)
		iax_ie_append_short(&ied, IAX_IE_ENCRYPTION, cai.encmethods);

	ast_mutex_lock(&iaxsl[callno]);

	if (!ast_strlen_zero(ast_channel_context(c)))
		ast_string_field_set(iaxs[callno], context, ast_channel_context(c));

	if (pds.username)
		ast_string_field_set(iaxs[callno], username, pds.username);

	iaxs[callno]->encmethods = cai.encmethods;

	iaxs[callno]->adsi = cai.adsi;

	ast_string_field_set(iaxs[callno], mohinterpret, cai.mohinterpret);
	ast_string_field_set(iaxs[callno], mohsuggest, cai.mohsuggest);

	if (pds.key)
		ast_string_field_set(iaxs[callno], outkey, pds.key);
	if (pds.password)
		ast_string_field_set(iaxs[callno], secret, pds.password);

	iax2_tmpfmt = iax2_format_compatibility_cap2bitfield(ast_channel_nativeformats(c));
	iax_ie_append_int(&ied, IAX_IE_FORMAT, (int) iax2_tmpfmt);
	iax_ie_append_versioned_uint64(&ied, IAX_IE_FORMAT2, 0, iax2_tmpfmt);

	iax_ie_append_int(&ied, IAX_IE_CAPABILITY, (int) iaxs[callno]->capability);
	iax_ie_append_versioned_uint64(&ied, IAX_IE_CAPABILITY2, 0, iaxs[callno]->capability);
	iax_ie_append_short(&ied, IAX_IE_ADSICPE, ast_channel_adsicpe(c));
	iax_ie_append_int(&ied, IAX_IE_DATETIME, iax2_datetime(cai.timezone));

	if (iaxs[callno]->maxtime) {
		/* Initialize pingtime and auto-congest time */
		iaxs[callno]->pingtime = iaxs[callno]->maxtime / 2;
		iaxs[callno]->initid = iax2_sched_add(sched, iaxs[callno]->maxtime * 2, auto_congest, CALLNO_TO_PTR(callno));
	} else if (autokill) {
		iaxs[callno]->pingtime = autokill / 2;
		iaxs[callno]->initid = iax2_sched_add(sched, autokill * 2, auto_congest, CALLNO_TO_PTR(callno));
	}

	/* Check if there is an OSP token */
	osp_token_ptr = pbx_builtin_getvar_helper(c, "IAX2OSPTOKEN");
	if (!ast_strlen_zero(osp_token_ptr)) {
		if ((osp_token_length = strlen(osp_token_ptr)) <= IAX_MAX_OSPTOKEN_SIZE) {
			osp_block_index = 0;
			while (osp_token_length > 0) {
				osp_block_length = IAX_MAX_OSPBLOCK_SIZE < osp_token_length ? IAX_MAX_OSPBLOCK_SIZE : osp_token_length;
				osp_buffer[0] = osp_block_index;
				memcpy(osp_buffer + 1, osp_token_ptr, osp_block_length);
				iax_ie_append_raw(&ied, IAX_IE_OSPTOKEN, osp_buffer, osp_block_length + 1);
				osp_block_index++;
				osp_token_ptr += osp_block_length;
				osp_token_length -= osp_block_length;
			}
		} else
			ast_log(LOG_WARNING, "OSP token is too long\n");
	} else if (iaxdebug)
		ast_debug(1, "OSP token is undefined\n");

	/* send the command using the appropriate socket for this peer */
	iaxs[callno]->sockfd = cai.sockfd;

	/* Add remote vars */
	if (variablestore) {
		AST_LIST_HEAD(, ast_var_t) *variablelist = variablestore->data;
		ast_debug(1, "Found an IAX variable store on this channel\n");
		AST_LIST_LOCK(variablelist);
		AST_LIST_TRAVERSE(variablelist, var, entries) {
			char tmp[256];
			int i;
			ast_debug(1, "Found IAXVAR '%s' with value '%s' (to transmit)\n", ast_var_name(var), ast_var_value(var));
			/* Automatically divide the value up into sized chunks */
			for (i = 0; i < strlen(ast_var_value(var)); i += 255 - (strlen(ast_var_name(var)) + 1)) {
				snprintf(tmp, sizeof(tmp), "%s=%s", ast_var_name(var), ast_var_value(var) + i);
				iax_ie_append_str(&ied, IAX_IE_VARIABLE, tmp);
			}
		}
		AST_LIST_UNLOCK(variablelist);
	}

	/* Transmit the string in a "NEW" request */
	add_empty_calltoken_ie(iaxs[callno], &ied); /* this _MUST_ be the last ie added */
	send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_NEW, 0, ied.buf, ied.pos, -1);

	ast_mutex_unlock(&iaxsl[callno]);
	ast_setstate(c, AST_STATE_RINGING);

	return 0;
}

static int iax2_hangup(struct ast_channel *c)
{
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
	struct iax_ie_data ied;
	int alreadygone;
	memset(&ied, 0, sizeof(ied));
	ast_mutex_lock(&iaxsl[callno]);
	if (callno && iaxs[callno]) {
		ast_debug(1, "We're hanging up %s now...\n", ast_channel_name(c));
		alreadygone = ast_test_flag64(iaxs[callno], IAX_ALREADYGONE);
		/* Send the hangup unless we have had a transmission error or are already gone */
		iax_ie_append_byte(&ied, IAX_IE_CAUSECODE, (unsigned char)ast_channel_hangupcause(c));
		if (!iaxs[callno]->error && !alreadygone) {
			if (send_command_final(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_HANGUP, 0, ied.buf, ied.pos, -1)) {
				ast_log(LOG_WARNING, "No final packet could be sent for callno %d\n", callno);
			}
			if (!iaxs[callno]) {
				ast_mutex_unlock(&iaxsl[callno]);
				return 0;
			}
		}
		/* Explicitly predestroy it */
		iax2_predestroy(callno);
		/* If we were already gone to begin with, destroy us now */
		if (iaxs[callno] && alreadygone) {
			ast_debug(1, "Really destroying %s now...\n", ast_channel_name(c));
			iax2_destroy(callno);
		} else if (iaxs[callno]) {
			if (ast_sched_add(sched, 10000, scheduled_destroy, CALLNO_TO_PTR(callno)) < 0) {
				ast_log(LOG_ERROR, "Unable to schedule iax2 callno %d destruction?!!  Destroying immediately.\n", callno);
				iax2_destroy(callno);
			}
		}
	} else if (ast_channel_tech_pvt(c)) {
		/* If this call no longer exists, but the channel still
		 * references it we need to set the channel's tech_pvt to null
		 * to avoid ast_channel_free() trying to free it.
		 */
		ast_channel_tech_pvt_set(c, NULL);
	}
	ast_mutex_unlock(&iaxsl[callno]);
	ast_verb(3, "Hungup '%s'\n", ast_channel_name(c));
	return 0;
}

/*!
 * \note expects the pvt to be locked
 */
static int wait_for_peercallno(struct chan_iax2_pvt *pvt)
{
	unsigned short callno = pvt->callno;

	if (!pvt->peercallno) {
		/* We don't know the remote side's call number, yet.  :( */
		int count = 10;
		while (count-- && pvt && !pvt->peercallno) {
			DEADLOCK_AVOIDANCE(&iaxsl[callno]);
			pvt = iaxs[callno];
		}
		if (!pvt || !pvt->peercallno) {
			return -1;
		}
	}

	return 0;
}

static int iax2_setoption(struct ast_channel *c, int option, void *data, int datalen)
{
	struct ast_option_header *h;
	int res;

	switch (option) {
	case AST_OPTION_TXGAIN:
	case AST_OPTION_RXGAIN:
		/* these two cannot be sent, because they require a result */
		errno = ENOSYS;
		return -1;
	case AST_OPTION_OPRMODE:
		errno = EINVAL;
		return -1;
	case AST_OPTION_SECURE_SIGNALING:
	case AST_OPTION_SECURE_MEDIA:
	{
		unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
		ast_mutex_lock(&iaxsl[callno]);
		if ((*(int *) data)) {
			ast_set_flag64(iaxs[callno], IAX_FORCE_ENCRYPT);
		} else {
			ast_clear_flag64(iaxs[callno], IAX_FORCE_ENCRYPT);
		}
		ast_mutex_unlock(&iaxsl[callno]);
		return 0;
	}
	/* These options are sent to the other side across the network where
	 * they will be passed to whatever channel is bridged there. Don't
	 * do anything silly like pass an option that transmits pointers to
	 * memory on this machine to a remote machine to use */
	case AST_OPTION_TONE_VERIFY:
	case AST_OPTION_TDD:
	case AST_OPTION_RELAXDTMF:
	case AST_OPTION_AUDIO_MODE:
	case AST_OPTION_DIGIT_DETECT:
	case AST_OPTION_FAX_DETECT:
	{
		unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
		struct chan_iax2_pvt *pvt;

		ast_mutex_lock(&iaxsl[callno]);
		pvt = iaxs[callno];

		if (wait_for_peercallno(pvt)) {
			ast_mutex_unlock(&iaxsl[callno]);
			return -1;
		}

		ast_mutex_unlock(&iaxsl[callno]);

		if (!(h = ast_malloc(datalen + sizeof(*h)))) {
			return -1;
		}

		h->flag = AST_OPTION_FLAG_REQUEST;
		h->option = htons(option);
		memcpy(h->data, data, datalen);
		res = send_command_locked(PTR_TO_CALLNO(ast_channel_tech_pvt(c)), AST_FRAME_CONTROL,
					  AST_CONTROL_OPTION, 0, (unsigned char *) h,
					  datalen + sizeof(*h), -1);
		ast_free(h);
		return res;
	}
	default:
		return -1;
	}

	/* Just in case someone does a break instead of a return */
	return -1;
}

static int iax2_queryoption(struct ast_channel *c, int option, void *data, int *datalen)
{
	switch (option) {
	case AST_OPTION_SECURE_SIGNALING:
	case AST_OPTION_SECURE_MEDIA:
	{
		unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
		ast_mutex_lock(&iaxsl[callno]);
		*((int *) data) = ast_test_flag64(iaxs[callno], IAX_FORCE_ENCRYPT) ? 1 : 0;
		ast_mutex_unlock(&iaxsl[callno]);
		return 0;
	}
	default:
		return -1;
	}
}

static struct ast_frame *iax2_read(struct ast_channel *c)
{
	ast_debug(1, "I should never be called!\n");
	return &ast_null_frame;
}

static int iax2_key_rotate(const void *vpvt)
{
	int res = 0;
	struct chan_iax2_pvt *pvt = (void *) vpvt;
	struct MD5Context md5;
	char key[17] = "";
	struct iax_ie_data ied = {
		.pos = 0,
	};

	ast_mutex_lock(&iaxsl[pvt->callno]);
	pvt->keyrotateid = ast_sched_add(sched, 120000 + (ast_random() % 180001), iax2_key_rotate, vpvt);

	snprintf(key, sizeof(key), "%lX", (unsigned long)ast_random());

	MD5Init(&md5);
	MD5Update(&md5, (unsigned char *) key, strlen(key));
	MD5Final((unsigned char *) key, &md5);

	IAX_DEBUGDIGEST("Sending", key);

	iax_ie_append_raw(&ied, IAX_IE_CHALLENGE, key, 16);

	res = send_command(pvt, AST_FRAME_IAX, IAX_COMMAND_RTKEY, 0, ied.buf, ied.pos, -1);

	build_ecx_key((unsigned char *) key, pvt);

	ast_mutex_unlock(&iaxsl[pvt->callno]);

	return res;
}

#if defined(IAX2_NATIVE_BRIDGING)
static int iax2_start_transfer(unsigned short callno0, unsigned short callno1, int mediaonly)
{
	int res;
	struct iax_ie_data ied0;
	struct iax_ie_data ied1;
	unsigned int transferid = (unsigned int)ast_random();

	if (IAX_CALLENCRYPTED(iaxs[callno0]) || IAX_CALLENCRYPTED(iaxs[callno1])) {
		ast_debug(1, "transfers are not supported for encrypted calls at this time\n");
		ast_set_flag64(iaxs[callno0], IAX_NOTRANSFER);
		ast_set_flag64(iaxs[callno1], IAX_NOTRANSFER);
		return 0;
	}

	memset(&ied0, 0, sizeof(ied0));
	iax_ie_append_addr(&ied0, IAX_IE_APPARENT_ADDR, &iaxs[callno1]->addr);
	iax_ie_append_short(&ied0, IAX_IE_CALLNO, iaxs[callno1]->peercallno);
	iax_ie_append_int(&ied0, IAX_IE_TRANSFERID, transferid);

	memset(&ied1, 0, sizeof(ied1));
	iax_ie_append_addr(&ied1, IAX_IE_APPARENT_ADDR, &iaxs[callno0]->addr);
	iax_ie_append_short(&ied1, IAX_IE_CALLNO, iaxs[callno0]->peercallno);
	iax_ie_append_int(&ied1, IAX_IE_TRANSFERID, transferid);

	res = send_command(iaxs[callno0], AST_FRAME_IAX, IAX_COMMAND_TXREQ, 0, ied0.buf, ied0.pos, -1);
	if (res)
		return -1;
	res = send_command(iaxs[callno1], AST_FRAME_IAX, IAX_COMMAND_TXREQ, 0, ied1.buf, ied1.pos, -1);
	if (res)
		return -1;
	iaxs[callno0]->transferring = mediaonly ? TRANSFER_MBEGIN : TRANSFER_BEGIN;
	iaxs[callno1]->transferring = mediaonly ? TRANSFER_MBEGIN : TRANSFER_BEGIN;
	return 0;
}
#endif	/* defined(IAX2_NATIVE_BRIDGING) */

#if defined(IAX2_NATIVE_BRIDGING)
static void lock_both(unsigned short callno0, unsigned short callno1)
{
	ast_mutex_lock(&iaxsl[callno0]);
	while (ast_mutex_trylock(&iaxsl[callno1])) {
		DEADLOCK_AVOIDANCE(&iaxsl[callno0]);
	}
}
#endif	/* defined(IAX2_NATIVE_BRIDGING) */

#if defined(IAX2_NATIVE_BRIDGING)
static void unlock_both(unsigned short callno0, unsigned short callno1)
{
	ast_mutex_unlock(&iaxsl[callno1]);
	ast_mutex_unlock(&iaxsl[callno0]);
}
#endif	/* defined(IAX2_NATIVE_BRIDGING) */

#if defined(IAX2_NATIVE_BRIDGING)
static enum ast_bridge_result iax2_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms)
{
	struct ast_channel *cs[3];
	struct ast_channel *who, *other;
	int to = -1;
	int res = -1;
	int transferstarted=0;
	struct ast_frame *f;
	unsigned short callno0 = PTR_TO_CALLNO(ast_channel_tech_pvt(c0));
	unsigned short callno1 = PTR_TO_CALLNO(ast_channel_tech_pvt(c1));
	struct timeval waittimer = {0, 0};

	/* We currently do not support native bridging if a timeoutms value has been provided */
	if (timeoutms > 0) {
		return AST_BRIDGE_FAILED;
	}

	timeoutms = -1;

	lock_both(callno0, callno1);
	if (!iaxs[callno0] || !iaxs[callno1]) {
		unlock_both(callno0, callno1);
		return AST_BRIDGE_FAILED;
	}
	/* Put them in native bridge mode */
	if (!(flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))) {
		iaxs[callno0]->bridgecallno = callno1;
		iaxs[callno1]->bridgecallno = callno0;
	}
	unlock_both(callno0, callno1);

	/* If not, try to bridge until we can execute a transfer, if we can */
	cs[0] = c0;
	cs[1] = c1;
	for (/* ever */;;) {
		/* Check in case we got masqueraded into */
		if ((ast_channel_tech(c0) != &iax2_tech) || (ast_channel_tech(c1) != &iax2_tech)) {
			ast_verb(3, "Can't masquerade, we're different...\n");
			/* Remove from native mode */
			if (ast_channel_tech(c0) == &iax2_tech) {
				ast_mutex_lock(&iaxsl[callno0]);
				iaxs[callno0]->bridgecallno = 0;
				ast_mutex_unlock(&iaxsl[callno0]);
			}
			if (ast_channel_tech(c1) == &iax2_tech) {
				ast_mutex_lock(&iaxsl[callno1]);
				iaxs[callno1]->bridgecallno = 0;
				ast_mutex_unlock(&iaxsl[callno1]);
			}
			return AST_BRIDGE_FAILED_NOWARN;
		}
		if (!(ast_format_cap_identical(ast_channel_nativeformats(c0), ast_channel_nativeformats(c1)))) {
			struct ast_str *c0_buf = ast_str_alloca(64);
			struct ast_str *c1_buf = ast_str_alloca(64);

			ast_verb(3, "Operating with different codecs [%s] [%s] , can't native bridge...\n",
				ast_format_cap_get_names(ast_channel_nativeformats(c0), &c0_buf),
				ast_format_cap_get_names(ast_channel_nativeformats(c1), &c1_buf));

			/* Remove from native mode */
			lock_both(callno0, callno1);
			if (iaxs[callno0])
				iaxs[callno0]->bridgecallno = 0;
			if (iaxs[callno1])
				iaxs[callno1]->bridgecallno = 0;
			unlock_both(callno0, callno1);
			return AST_BRIDGE_FAILED_NOWARN;
		}
		/* check if transferred and if we really want native bridging */
		if (!transferstarted && !ast_test_flag64(iaxs[callno0], IAX_NOTRANSFER) && !ast_test_flag64(iaxs[callno1], IAX_NOTRANSFER)) {
			/* Try the transfer */
			if (iax2_start_transfer(callno0, callno1, (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1)) ||
							ast_test_flag64(iaxs[callno0], IAX_TRANSFERMEDIA) | ast_test_flag64(iaxs[callno1], IAX_TRANSFERMEDIA)))
				ast_log(LOG_WARNING, "Unable to start the transfer\n");
			transferstarted = 1;
		}
		if ((iaxs[callno0]->transferring == TRANSFER_RELEASED) && (iaxs[callno1]->transferring == TRANSFER_RELEASED)) {
			/* Call has been transferred.  We're no longer involved */
			struct timeval now = ast_tvnow();
			if (ast_tvzero(waittimer)) {
				waittimer = now;
			} else if (now.tv_sec - waittimer.tv_sec > IAX_LINGER_TIMEOUT) {
				ast_channel_softhangup_internal_flag_add(c0, AST_SOFTHANGUP_DEV);
				ast_channel_softhangup_internal_flag_add(c1, AST_SOFTHANGUP_DEV);
				*fo = NULL;
				*rc = c0;
				res = AST_BRIDGE_COMPLETE;
				break;
			}
		}
		to = 1000;
		who = ast_waitfor_n(cs, 2, &to);
		/* XXX This will need to be updated to calculate
		 * timeout correctly once timeoutms is allowed to be
		 * > 0. Right now, this can go badly if the waitfor
		 * times out in less than a millisecond
		 */
		if (timeoutms > -1) {
			timeoutms -= (1000 - to);
			if (timeoutms < 0)
				timeoutms = 0;
		}
		if (!who) {
			if (!timeoutms) {
				res = AST_BRIDGE_RETRY;
				break;
			}
			if (ast_check_hangup(c0) || ast_check_hangup(c1)) {
				res = AST_BRIDGE_FAILED;
				break;
			}
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = NULL;
			*rc = who;
			res = AST_BRIDGE_COMPLETE;
			break;
		}
		other = (who == c0) ? c1 : c0;  /* the 'other' channel */
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass.integer) {
			case AST_CONTROL_VIDUPDATE:
			case AST_CONTROL_SRCUPDATE:
			case AST_CONTROL_SRCCHANGE:
			case AST_CONTROL_T38_PARAMETERS:
				ast_write(other, f);
				break;
			case AST_CONTROL_PVT_CAUSE_CODE:
				ast_channel_hangupcause_hash_set(other, f->data.ptr, f->datalen);
				break;
			default:
				*fo = f;
				*rc = who;
				res = AST_BRIDGE_COMPLETE;
				break;
			}
			if (res == AST_BRIDGE_COMPLETE) {
				break;
			}
		} else if (f->frametype == AST_FRAME_VOICE
			|| f->frametype == AST_FRAME_TEXT
			|| f->frametype == AST_FRAME_VIDEO
			|| f->frametype == AST_FRAME_IMAGE) {
			ast_write(other, f);
		} else if (f->frametype == AST_FRAME_DTMF) {
			/* monitored dtmf take out of the bridge.
			 * check if we monitor the specific source.
			 */
			int monitored_source = (who == c0) ? AST_BRIDGE_DTMF_CHANNEL_0 : AST_BRIDGE_DTMF_CHANNEL_1;

			if (flags & monitored_source) {
				*rc = who;
				*fo = f;
				res = AST_BRIDGE_COMPLETE;
				/* Remove from native mode */
				break;
			}
			ast_write(other, f);
		}
		ast_frfree(f);
		/* Swap who gets priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	lock_both(callno0, callno1);
	if(iaxs[callno0])
		iaxs[callno0]->bridgecallno = 0;
	if(iaxs[callno1])
		iaxs[callno1]->bridgecallno = 0;
	unlock_both(callno0, callno1);
	return res;
}
#endif	/* defined(IAX2_NATIVE_BRIDGING) */

static int iax2_answer(struct ast_channel *c)
{
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
	ast_debug(1, "Answering IAX2 call\n");
	return send_command_locked(callno, AST_FRAME_CONTROL, AST_CONTROL_ANSWER, 0, NULL, 0, -1);
}

static int iax2_indicate(struct ast_channel *c, int condition, const void *data, size_t datalen)
{
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
	struct chan_iax2_pvt *pvt;
	int res = 0;

	if (iaxdebug)
		ast_debug(1, "Indicating condition %d\n", condition);

	ast_mutex_lock(&iaxsl[callno]);
	pvt = iaxs[callno];

	if (wait_for_peercallno(pvt)) {
		res = -1;
		goto done;
	}

	switch (condition) {
	case AST_CONTROL_HOLD:
		if (strcasecmp(pvt->mohinterpret, "passthrough")) {
			ast_moh_start(c, data, pvt->mohinterpret);
			goto done;
		}
		break;
	case AST_CONTROL_UNHOLD:
		if (strcasecmp(pvt->mohinterpret, "passthrough")) {
			ast_moh_stop(c);
			goto done;
		}
		break;
	case AST_CONTROL_CONNECTED_LINE:
	case AST_CONTROL_REDIRECTING:
		if (!ast_test_flag64(pvt, IAX_SENDCONNECTEDLINE)) {
			/* We are not configured to allow sending these updates. */
			ast_debug(2, "Callno %d: Config blocked sending control frame %d.\n",
				callno, condition);
			goto done;
		}
		break;
	case AST_CONTROL_PVT_CAUSE_CODE:
	case AST_CONTROL_MASQUERADE_NOTIFY:
		res = -1;
		goto done;
	}

	res = send_command(pvt, AST_FRAME_CONTROL, condition, 0, data, datalen, -1);

done:
	ast_mutex_unlock(&iaxsl[callno]);

	return res;
}

static int iax2_transfer(struct ast_channel *c, const char *dest)
{
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
	struct iax_ie_data ied = { "", };
	char tmp[256], *context;
	enum ast_control_transfer message = AST_TRANSFER_SUCCESS;
	ast_copy_string(tmp, dest, sizeof(tmp));
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, tmp);
	if (context)
		iax_ie_append_str(&ied, IAX_IE_CALLED_CONTEXT, context);
	ast_debug(1, "Transferring '%s' to '%s'\n", ast_channel_name(c), dest);
	ast_queue_control_data(c, AST_CONTROL_TRANSFER, &message, sizeof(message));
	return send_command_locked(callno, AST_FRAME_IAX, IAX_COMMAND_TRANSFER, 0, ied.buf, ied.pos, -1);
}

static int iax2_getpeertrunk(struct ast_sockaddr addr)
{
	struct iax2_peer *peer;
	int res = 0;
	struct ao2_iterator i;

	i = ao2_iterator_init(peers, 0);
	while ((peer = ao2_iterator_next(&i))) {

		if (!ast_sockaddr_cmp(&peer->addr, &addr)) {
			res = ast_test_flag64(peer, IAX_TRUNK);
			peer_unref(peer);
			break;
		}
		peer_unref(peer);
	}
	ao2_iterator_destroy(&i);

	return res;
}

/*! \brief  Create new call, interface with the PBX core */
static struct ast_channel *ast_iax2_new(int callno, int state, iax2_format capability,
	struct iax2_codec_pref *prefs, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, unsigned int cachable)
{
	struct ast_channel *tmp = NULL;
	struct chan_iax2_pvt *i;
	struct iax2_peer *peer;
	struct ast_variable *v = NULL;
	struct ast_format_cap *native;
	struct ast_format *tmpfmt;
	ast_callid callid;
	char *peer_name = NULL;

	if (!(i = iaxs[callno])) {
		ast_log(LOG_WARNING, "No IAX2 pvt found for callno '%d' !\n", callno);
		return NULL;
	}

	if (!capability) {
		ast_log(LOG_WARNING, "No formats specified for call to: IAX2/%s-%d\n",
			i->host, i->callno);
		return NULL;
	}
	native = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!native) {
		return NULL;
	}
	if (iax2_codec_pref_best_bitfield2cap(capability, prefs, native)
		|| !ast_format_cap_count(native)) {
		ast_log(LOG_WARNING, "No requested formats available for call to: IAX2/%s-%d\n",
			i->host, i->callno);
		ao2_ref(native, -1);
		return NULL;
	}

	if (!ast_strlen_zero(i->peer)) {
		peer_name = ast_strdupa(i->peer);
	} else if (!ast_strlen_zero(i->host)) {
		peer_name = ast_strdupa(i->host);
	}

	/* Don't hold call lock while making a channel or looking up a peer */
	ast_mutex_unlock(&iaxsl[callno]);

	if (!ast_strlen_zero(peer_name)) {
		peer = find_peer(peer_name, 1);
		if (peer && peer->endpoint) {
			tmp = ast_channel_alloc_with_endpoint(1, state, i->cid_num, i->cid_name,
				i->accountcode, i->exten, i->context, assignedids, requestor,
				i->amaflags, peer->endpoint, "IAX2/%s-%d", i->host, i->callno);
		}
		ao2_cleanup(peer);
	}

	if (!tmp) {
		tmp = ast_channel_alloc(1, state, i->cid_num, i->cid_name, i->accountcode,
			i->exten, i->context, assignedids, requestor, i->amaflags, "IAX2/%s-%d",
			i->host, i->callno);
	}

	ast_mutex_lock(&iaxsl[callno]);
	if (i != iaxs[callno]) {
		if (tmp) {
			/* unlock and relock iaxsl[callno] to preserve locking order */
			ast_mutex_unlock(&iaxsl[callno]);
			ast_channel_unlock(tmp);
			tmp = ast_channel_release(tmp);
			ast_mutex_lock(&iaxsl[callno]);
		}
		ao2_ref(native, -1);
		return NULL;
	}
	if (!tmp) {
		ao2_ref(native, -1);
		return NULL;
	}

	ast_channel_stage_snapshot(tmp);

	if ((callid = iaxs[callno]->callid)) {
		ast_channel_callid_set(tmp, callid);
	}

	ast_channel_tech_set(tmp, &iax2_tech);

	/* We can support any format by default, until we get restricted */
	ast_channel_nativeformats_set(tmp, native);
	tmpfmt = ast_format_cap_get_format(native, 0);

	ast_channel_set_readformat(tmp, tmpfmt);
	ast_channel_set_rawreadformat(tmp, tmpfmt);
	ast_channel_set_writeformat(tmp, tmpfmt);
	ast_channel_set_rawwriteformat(tmp, tmpfmt);

	ao2_ref(tmpfmt, -1);
	ao2_ref(native, -1);

	ast_channel_tech_pvt_set(tmp, CALLNO_TO_PTR(i->callno));

	if (!ast_strlen_zero(i->parkinglot))
		ast_channel_parkinglot_set(tmp, i->parkinglot);
	/* Don't use ast_set_callerid() here because it will
	 * generate a NewCallerID event before the NewChannel event */
	if (!ast_strlen_zero(i->ani)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->ani);
	} else if (!ast_strlen_zero(i->cid_num)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}
	ast_channel_dialed(tmp)->number.str = ast_strdup(i->dnid);
	if (!ast_strlen_zero(i->rdnis)) {
		ast_channel_redirecting(tmp)->from.number.valid = 1;
		ast_channel_redirecting(tmp)->from.number.str = ast_strdup(i->rdnis);
	}
	ast_channel_caller(tmp)->id.name.presentation = i->calling_pres;
	ast_channel_caller(tmp)->id.number.presentation = i->calling_pres;
	ast_channel_caller(tmp)->id.number.plan = i->calling_ton;
	ast_channel_dialed(tmp)->transit_network_select = i->calling_tns;
	if (!ast_strlen_zero(i->language))
		ast_channel_language_set(tmp, i->language);
	if (!ast_strlen_zero(i->accountcode))
		ast_channel_accountcode_set(tmp, i->accountcode);
	if (i->amaflags)
		ast_channel_amaflags_set(tmp, i->amaflags);
	ast_channel_context_set(tmp, i->context);
	ast_channel_exten_set(tmp, i->exten);
	if (i->adsi)
		ast_channel_adsicpe_set(tmp, i->peeradsicpe);
	else
		ast_channel_adsicpe_set(tmp, AST_ADSI_UNAVAILABLE);
	i->owner = tmp;
	i->capability = capability;

	if (!cachable) {
		ast_set_flag(ast_channel_flags(tmp), AST_FLAG_DISABLE_DEVSTATE_CACHE);
	}

	/* Set inherited variables */
	if (i->vars) {
		for (v = i->vars ; v ; v = v->next)
			pbx_builtin_setvar_helper(tmp, v->name, v->value);
	}
	if (i->iaxvars) {
		struct ast_datastore *variablestore;
		struct ast_variable *var, *prev = NULL;
		AST_LIST_HEAD(, ast_var_t) *varlist;
		ast_debug(1, "Loading up the channel with IAXVARs\n");
		varlist = ast_calloc(1, sizeof(*varlist));
		variablestore = ast_datastore_alloc(&iax2_variable_datastore_info, NULL);
		if (variablestore && varlist) {
			variablestore->data = varlist;
			variablestore->inheritance = DATASTORE_INHERIT_FOREVER;
			AST_LIST_HEAD_INIT(varlist);
			for (var = i->iaxvars; var; var = var->next) {
				struct ast_var_t *newvar = ast_var_assign(var->name, var->value);
				if (prev)
					ast_free(prev);
				prev = var;
				if (!newvar) {
					/* Don't abort list traversal, as this would leave i->iaxvars in an inconsistent state. */
					ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
				} else {
					AST_LIST_INSERT_TAIL(varlist, newvar, entries);
				}
			}
			if (prev)
				ast_free(prev);
			i->iaxvars = NULL;
			ast_channel_datastore_add(i->owner, variablestore);
		} else {
			if (variablestore) {
				ast_datastore_free(variablestore);
			}
			if (varlist) {
				ast_free(varlist);
			}
		}
	}

	ast_channel_stage_snapshot_done(tmp);
	ast_channel_unlock(tmp);

	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
			/* unlock and relock iaxsl[callno] to preserve locking order */
			ast_mutex_unlock(&iaxsl[callno]);
			ast_hangup(tmp);
			ast_mutex_lock(&iaxsl[callno]);
			return NULL;
		}
	}

	ast_module_ref(ast_module_info->self);
	return tmp;
}

static unsigned int calc_txpeerstamp(struct iax2_trunk_peer *tpeer, int sampms, struct timeval *now)
{
	unsigned long int mssincetx; /* unsigned to handle overflows */
	long int ms, pred;

	tpeer->trunkact = *now;
	mssincetx = ast_tvdiff_ms(*now, tpeer->lasttxtime);
	if (mssincetx > 5000 || ast_tvzero(tpeer->txtrunktime)) {
		/* If it's been at least 5 seconds since the last time we transmitted on this trunk, reset our timers */
		tpeer->txtrunktime = *now;
		tpeer->lastsent = 999999;
	}
	/* Update last transmit time now */
	tpeer->lasttxtime = *now;

	/* Calculate ms offset */
	ms = ast_tvdiff_ms(*now, tpeer->txtrunktime);
	/* Predict from last value */
	pred = tpeer->lastsent + sampms;
	if (labs(ms - pred) < MAX_TIMESTAMP_SKEW)
		ms = pred;

	/* We never send the same timestamp twice, so fudge a little if we must */
	if (ms == tpeer->lastsent)
		ms = tpeer->lastsent + 1;
	tpeer->lastsent = ms;
	return ms;
}

static unsigned int fix_peerts(struct timeval *rxtrunktime, int callno, unsigned int ts)
{
	long ms;	/* NOT unsigned */
	if (ast_tvzero(iaxs[callno]->rxcore)) {
		/* Initialize rxcore time if appropriate */
		iaxs[callno]->rxcore = ast_tvnow();
		/* Round to nearest 20ms so traces look pretty */
		iaxs[callno]->rxcore.tv_usec -= iaxs[callno]->rxcore.tv_usec % 20000;
	}
	/* Calculate difference between trunk and channel */
	ms = ast_tvdiff_ms(*rxtrunktime, iaxs[callno]->rxcore);
	/* Return as the sum of trunk time and the difference between trunk and real time */
	return ms + ts;
}

static unsigned int calc_timestamp(struct chan_iax2_pvt *p, unsigned int ts, struct ast_frame *f)
{
	int ms;
	int voice = 0;
	int genuine = 0;
	int adjust;
	int rate = 0;
	struct timeval *delivery = NULL;


	/* What sort of frame do we have?: voice is self-explanatory
	   "genuine" means an IAX frame - things like LAGRQ/RP, PING/PONG, ACK
	   non-genuine frames are CONTROL frames [ringing etc], DTMF
	   The "genuine" distinction is needed because genuine frames must get a clock-based timestamp,
	   the others need a timestamp slaved to the voice frames so that they go in sequence
	*/
	if (f->frametype == AST_FRAME_VOICE) {
		voice = 1;
		rate = ast_format_get_sample_rate(f->subclass.format) / 1000;
		delivery = &f->delivery;
	} else if (f->frametype == AST_FRAME_IAX) {
		genuine = 1;
	} else if (f->frametype == AST_FRAME_CNG) {
		p->notsilenttx = 0;
	}

	if (ast_tvzero(p->offset)) {
		p->offset = ast_tvnow();
		/* Round to nearest 20ms for nice looking traces */
		p->offset.tv_usec -= p->offset.tv_usec % 20000;
	}
	/* If the timestamp is specified, just send it as is */
	if (ts)
		return ts;
	/* If we have a time that the frame arrived, always use it to make our timestamp */
	if (delivery && !ast_tvzero(*delivery)) {
		ms = ast_tvdiff_ms(*delivery, p->offset);
		if (ms < 0) {
			ms = 0;
		}
		if (iaxdebug)
			ast_debug(3, "calc_timestamp: call %d/%d: Timestamp slaved to delivery time\n", p->callno, iaxs[p->callno]->peercallno);
	} else {
		ms = ast_tvdiff_ms(ast_tvnow(), p->offset);
		if (ms < 0)
			ms = 0;
		if (voice) {
			/* On a voice frame, use predicted values if appropriate */
			adjust = (ms - p->nextpred);
			if (p->notsilenttx && abs(adjust) <= MAX_TIMESTAMP_SKEW) {
				/* Adjust our txcore, keeping voice and non-voice synchronized */
				/* AN EXPLANATION:
				   When we send voice, we usually send "calculated" timestamps worked out
			 	   on the basis of the number of samples sent. When we send other frames,
				   we usually send timestamps worked out from the real clock.
				   The problem is that they can tend to drift out of step because the
			    	   source channel's clock and our clock may not be exactly at the same rate.
				   We fix this by continuously "tweaking" p->offset.  p->offset is "time zero"
				   for this call.  Moving it adjusts timestamps for non-voice frames.
				   We make the adjustment in the style of a moving average.  Each time we
				   adjust p->offset by 10% of the difference between our clock-derived
				   timestamp and the predicted timestamp.  That's why you see "10000"
				   below even though IAX2 timestamps are in milliseconds.
				   The use of a moving average avoids offset moving too radically.
				   Generally, "adjust" roams back and forth around 0, with offset hardly
				   changing at all.  But if a consistent different starts to develop it
				   will be eliminated over the course of 10 frames (200-300msecs)
				*/
				if (adjust < 0)
					p->offset = ast_tvsub(p->offset, ast_samp2tv(abs(adjust), 10000));
				else if (adjust > 0)
					p->offset = ast_tvadd(p->offset, ast_samp2tv(adjust, 10000));

				if (!p->nextpred) {
					p->nextpred = ms; /*f->samples / rate;*/
					if (p->nextpred <= p->lastsent)
						p->nextpred = p->lastsent + 3;
				}
				ms = p->nextpred;
			} else {
			       /* in this case, just use the actual
				* time, since we're either way off
				* (shouldn't happen), or we're  ending a
				* silent period -- and seed the next
				* predicted time.  Also, round ms to the
				* next multiple of frame size (so our
				* silent periods are multiples of
				* frame size too) */

				if (iaxdebug && abs(adjust) > MAX_TIMESTAMP_SKEW )
					ast_debug(1, "predicted timestamp skew (%d) > max (%d), using real ts instead.\n",
						abs(adjust), MAX_TIMESTAMP_SKEW);

				if (f->samples >= rate) /* check to make sure we don't core dump */
				{
					int diff = ms % (f->samples / rate);
					if (diff)
					    ms += f->samples/rate - diff;
				}

				p->nextpred = ms;
				p->notsilenttx = 1;
			}
		} else if ( f->frametype == AST_FRAME_VIDEO ) {
			/*
			* IAX2 draft 03 says that timestamps MUST be in order.
			* It does not say anything about several frames having the same timestamp
			* When transporting video, we can have a frame that spans multiple iax packets
			* (so called slices), so it would make sense to use the same timestamp for all of
			* them
			* We do want to make sure that frames don't go backwards though
			*/
			if ( (unsigned int)ms < p->lastsent )
				ms = p->lastsent;
		} else {
			/* On a dataframe, use last value + 3 (to accomodate jitter buffer shrinking) if appropriate unless
			   it's a genuine frame */
			adjust = (ms - p->lastsent);
			if (genuine) {
				/* genuine (IAX LAGRQ etc) must keep their clock-based stamps */
				if (ms <= p->lastsent)
					ms = p->lastsent + 3;
			} else if (abs(adjust) <= MAX_TIMESTAMP_SKEW) {
				/* non-genuine frames (!?) (DTMF, CONTROL) should be pulled into the predicted stream stamps */
				ms = p->lastsent + 3;
			}
		}
	}
	p->lastsent = ms;
	if (voice) {
		p->nextpred = p->nextpred + f->samples / rate;
	}
	return ms;
}

static unsigned int calc_rxstamp(struct chan_iax2_pvt *p, unsigned int offset)
{
	/* Returns where in "receive time" we are.  That is, how many ms
	   since we received (or would have received) the frame with timestamp 0 */
	int ms;
#ifdef IAXTESTS
	int jit;
#endif /* IAXTESTS */
	/* Setup rxcore if necessary */
	if (ast_tvzero(p->rxcore)) {
		p->rxcore = ast_tvnow();
		if (iaxdebug)
			ast_debug(1, "calc_rxstamp: call=%d: rxcore set to %d.%6.6d - %ums\n",
					p->callno, (int)(p->rxcore.tv_sec), (int)(p->rxcore.tv_usec), offset);
		p->rxcore = ast_tvsub(p->rxcore, ast_samp2tv(offset, 1000));
#if 1
		if (iaxdebug)
			ast_debug(1, "calc_rxstamp: call=%d: works out as %d.%6.6d\n",
					p->callno, (int)(p->rxcore.tv_sec),(int)( p->rxcore.tv_usec));
#endif
	}

	ms = ast_tvdiff_ms(ast_tvnow(), p->rxcore);
#ifdef IAXTESTS
	if (test_jit) {
		if (!test_jitpct || ((100.0 * ast_random() / (RAND_MAX + 1.0)) < test_jitpct)) {
			jit = (int)((float)test_jit * ast_random() / (RAND_MAX + 1.0));
			if ((int)(2.0 * ast_random() / (RAND_MAX + 1.0)))
				jit = -jit;
			ms += jit;
		}
	}
	if (test_late) {
		ms += test_late;
		test_late = 0;
	}
#endif /* IAXTESTS */
	return ms;
}

static struct iax2_trunk_peer *find_tpeer(struct ast_sockaddr *addr, int fd)
{
	struct iax2_trunk_peer *tpeer = NULL;

	/* Finds and locks trunk peer */
	AST_LIST_LOCK(&tpeers);

	AST_LIST_TRAVERSE(&tpeers, tpeer, list) {
		if (!ast_sockaddr_cmp(&tpeer->addr, addr)) {
			ast_mutex_lock(&tpeer->lock);
			break;
		}
	}

	if (!tpeer) {
		if ((tpeer = ast_calloc(1, sizeof(*tpeer)))) {
			ast_mutex_init(&tpeer->lock);
			tpeer->lastsent = 9999;
			ast_sockaddr_copy(&tpeer->addr, addr);
			tpeer->trunkact = ast_tvnow();
			ast_mutex_lock(&tpeer->lock);
			tpeer->sockfd = fd;

#ifdef SO_NO_CHECK
			setsockopt(tpeer->sockfd, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
			ast_debug(1, "Created trunk peer for '%s'\n", ast_sockaddr_stringify(&tpeer->addr));
			AST_LIST_INSERT_TAIL(&tpeers, tpeer, list);
		}
	}

	AST_LIST_UNLOCK(&tpeers);

	return tpeer;
}

static int iax2_trunk_queue(struct chan_iax2_pvt *pvt, struct iax_frame *fr)
{
	struct ast_frame *f;
	struct iax2_trunk_peer *tpeer;
	void *tmp, *ptr;
	struct timeval now;
	struct ast_iax2_meta_trunk_entry *met;
	struct ast_iax2_meta_trunk_mini *mtm;

	f = &fr->af;
	tpeer = find_tpeer(&pvt->addr, pvt->sockfd);
	if (tpeer) {

		if (tpeer->trunkdatalen + f->datalen + 4 >= tpeer->trunkdataalloc) {
			/* Need to reallocate space */
			if (tpeer->trunkdataalloc < trunkmaxsize) {
				if (!(tmp = ast_realloc(tpeer->trunkdata, tpeer->trunkdataalloc + DEFAULT_TRUNKDATA + IAX2_TRUNK_PREFACE))) {
					ast_mutex_unlock(&tpeer->lock);
					return -1;
				}

				tpeer->trunkdataalloc += DEFAULT_TRUNKDATA;
				tpeer->trunkdata = tmp;
				ast_debug(1, "Expanded trunk '%s' to %u bytes\n", ast_sockaddr_stringify(&tpeer->addr), tpeer->trunkdataalloc);
			} else {
				ast_log(LOG_WARNING, "Maximum trunk data space exceeded to %s\n", ast_sockaddr_stringify(&tpeer->addr));
				ast_mutex_unlock(&tpeer->lock);
				return -1;
			}
		}

		/* Append to meta frame */
		ptr = tpeer->trunkdata + IAX2_TRUNK_PREFACE + tpeer->trunkdatalen;
		if (ast_test_flag64(&globalflags, IAX_TRUNKTIMESTAMPS)) {
			mtm = (struct ast_iax2_meta_trunk_mini *)ptr;
			mtm->len = htons(f->datalen);
			mtm->mini.callno = htons(pvt->callno);
			mtm->mini.ts = htons(0xffff & fr->ts);
			ptr += sizeof(struct ast_iax2_meta_trunk_mini);
			tpeer->trunkdatalen += sizeof(struct ast_iax2_meta_trunk_mini);
		} else {
			met = (struct ast_iax2_meta_trunk_entry *)ptr;
			/* Store call number and length in meta header */
			met->callno = htons(pvt->callno);
			met->len = htons(f->datalen);
			/* Advance pointers/decrease length past trunk entry header */
			ptr += sizeof(struct ast_iax2_meta_trunk_entry);
			tpeer->trunkdatalen += sizeof(struct ast_iax2_meta_trunk_entry);
		}
		/* Copy actual trunk data */
		memcpy(ptr, f->data.ptr, f->datalen);
		tpeer->trunkdatalen += f->datalen;

		tpeer->calls++;

		/* track the largest mtu we actually have sent */
		if (tpeer->trunkdatalen + f->datalen + 4 > trunk_maxmtu)
			trunk_maxmtu = tpeer->trunkdatalen + f->datalen + 4 ;

		/* if we have enough for a full MTU, ship it now without waiting */
		if (global_max_trunk_mtu > 0 && tpeer->trunkdatalen + f->datalen + 4 >= global_max_trunk_mtu) {
			now = ast_tvnow();
			send_trunk(tpeer, &now);
			trunk_untimed ++;
		}

		ast_mutex_unlock(&tpeer->lock);
	}
	return 0;
}

/* IAX2 encryption requires 16 to 32 bytes of random padding to be present
 * before the encryption data.  This function randomizes that data. */
static void build_rand_pad(unsigned char *buf, ssize_t len)
{
	long tmp;
	for (tmp = ast_random(); len > 0; tmp = ast_random()) {
		memcpy(buf, (unsigned char *) &tmp, (len > sizeof(tmp)) ? sizeof(tmp) : len);
		buf += sizeof(tmp);
		len -= sizeof(tmp);
	}
}

static void build_encryption_keys(const unsigned char *digest, struct chan_iax2_pvt *pvt)
{
	build_ecx_key(digest, pvt);
	ast_aes_set_decrypt_key(digest, &pvt->dcx);
}

static void build_ecx_key(const unsigned char *digest, struct chan_iax2_pvt *pvt)
{
	/* it is required to hold the corresponding decrypt key to our encrypt key
	 * in the pvt struct because queued frames occasionally need to be decrypted and
	 * re-encrypted when updated for a retransmission */
	build_rand_pad(pvt->semirand, sizeof(pvt->semirand));
	ast_aes_set_encrypt_key(digest, &pvt->ecx);
	ast_aes_set_decrypt_key(digest, &pvt->mydcx);
}

static void memcpy_decrypt(unsigned char *dst, const unsigned char *src, int len, ast_aes_decrypt_key *dcx)
{
#if 0
	/* Debug with "fake encryption" */
	int x;
	if (len % 16)
		ast_log(LOG_WARNING, "len should be multiple of 16, not %d!\n", len);
	for (x=0;x<len;x++)
		dst[x] = src[x] ^ 0xff;
#else
	unsigned char lastblock[16] = { 0 };
	int x;
	while(len > 0) {
		ast_aes_decrypt(src, dst, dcx);
		for (x=0;x<16;x++)
			dst[x] ^= lastblock[x];
		memcpy(lastblock, src, sizeof(lastblock));
		dst += 16;
		src += 16;
		len -= 16;
	}
#endif
}

static void memcpy_encrypt(unsigned char *dst, const unsigned char *src, int len, ast_aes_encrypt_key *ecx)
{
#if 0
	/* Debug with "fake encryption" */
	int x;
	if (len % 16)
		ast_log(LOG_WARNING, "len should be multiple of 16, not %d!\n", len);
	for (x=0;x<len;x++)
		dst[x] = src[x] ^ 0xff;
#else
	unsigned char curblock[16] = { 0 };
	int x;
	while(len > 0) {
		for (x=0;x<16;x++)
			curblock[x] ^= src[x];
		ast_aes_encrypt(curblock, dst, ecx);
		memcpy(curblock, dst, sizeof(curblock));
		dst += 16;
		src += 16;
		len -= 16;
	}
#endif
}

static int decode_frame(ast_aes_decrypt_key *dcx, struct ast_iax2_full_hdr *fh, struct ast_frame *f, int *datalen)
{
	int padding;
	unsigned char *workspace;

	workspace = ast_alloca(*datalen);
	memset(f, 0, sizeof(*f));
	if (ntohs(fh->scallno) & IAX_FLAG_FULL) {
		struct ast_iax2_full_enc_hdr *efh = (struct ast_iax2_full_enc_hdr *)fh;
		if (*datalen < 16 + sizeof(struct ast_iax2_full_hdr))
			return -1;
		/* Decrypt */
		memcpy_decrypt(workspace, efh->encdata, *datalen - sizeof(struct ast_iax2_full_enc_hdr), dcx);

		padding = 16 + (workspace[15] & 0x0f);
		if (iaxdebug)
			ast_debug(1, "Decoding full frame with length %d (padding = %d) (15=%02hhx)\n", *datalen, padding, workspace[15]);
		if (*datalen < padding + sizeof(struct ast_iax2_full_hdr))
			return -1;

		*datalen -= padding;
		memcpy(efh->encdata, workspace + padding, *datalen - sizeof(struct ast_iax2_full_enc_hdr));
		f->frametype = fh->type;
		if (f->frametype == AST_FRAME_VIDEO) {
			f->subclass.format = ast_format_compatibility_bitfield2format(uncompress_subclass(fh->csub & ~0x40) | ((fh->csub >> 6) & 0x1));
		} else if (f->frametype == AST_FRAME_VOICE) {
			f->subclass.format = ast_format_compatibility_bitfield2format(uncompress_subclass(fh->csub));
		} else {
			f->subclass.integer = uncompress_subclass(fh->csub);
		}
	} else {
		struct ast_iax2_mini_enc_hdr *efh = (struct ast_iax2_mini_enc_hdr *)fh;
		if (iaxdebug)
			ast_debug(1, "Decoding mini with length %d\n", *datalen);
		if (*datalen < 16 + sizeof(struct ast_iax2_mini_hdr))
			return -1;
		/* Decrypt */
		memcpy_decrypt(workspace, efh->encdata, *datalen - sizeof(struct ast_iax2_mini_enc_hdr), dcx);
		padding = 16 + (workspace[15] & 0x0f);
		if (*datalen < padding + sizeof(struct ast_iax2_mini_hdr))
			return -1;
		*datalen -= padding;
		memcpy(efh->encdata, workspace + padding, *datalen - sizeof(struct ast_iax2_mini_enc_hdr));
	}
	return 0;
}

static int encrypt_frame(ast_aes_encrypt_key *ecx, struct ast_iax2_full_hdr *fh, unsigned char *poo, int *datalen)
{
	int padding;
	unsigned char *workspace;
	workspace = ast_alloca(*datalen + 32);
	if (ntohs(fh->scallno) & IAX_FLAG_FULL) {
		struct ast_iax2_full_enc_hdr *efh = (struct ast_iax2_full_enc_hdr *)fh;
		if (iaxdebug)
			ast_debug(1, "Encoding full frame %d/%d with length %d\n", fh->type, fh->csub, *datalen);
		padding = 16 - ((*datalen - sizeof(struct ast_iax2_full_enc_hdr)) % 16);
		padding = 16 + (padding & 0xf);
		memcpy(workspace, poo, padding);
		memcpy(workspace + padding, efh->encdata, *datalen - sizeof(struct ast_iax2_full_enc_hdr));
		workspace[15] &= 0xf0;
		workspace[15] |= (padding & 0xf);
		if (iaxdebug)
			ast_debug(1, "Encoding full frame %d/%d with length %d + %d padding (15=%02hhx)\n", fh->type, fh->csub, *datalen, padding, workspace[15]);
		*datalen += padding;
		memcpy_encrypt(efh->encdata, workspace, *datalen - sizeof(struct ast_iax2_full_enc_hdr), ecx);
		if (*datalen >= 32 + sizeof(struct ast_iax2_full_enc_hdr))
			memcpy(poo, workspace + *datalen - 32, 32);
	} else {
		struct ast_iax2_mini_enc_hdr *efh = (struct ast_iax2_mini_enc_hdr *)fh;
		if (iaxdebug)
			ast_debug(1, "Encoding mini frame with length %d\n", *datalen);
		padding = 16 - ((*datalen - sizeof(struct ast_iax2_mini_enc_hdr)) % 16);
		padding = 16 + (padding & 0xf);
		memcpy(workspace, poo, padding);
		memcpy(workspace + padding, efh->encdata, *datalen - sizeof(struct ast_iax2_mini_enc_hdr));
		workspace[15] &= 0xf0;
		workspace[15] |= (padding & 0x0f);
		*datalen += padding;
		memcpy_encrypt(efh->encdata, workspace, *datalen - sizeof(struct ast_iax2_mini_enc_hdr), ecx);
		if (*datalen >= 32 + sizeof(struct ast_iax2_mini_enc_hdr))
			memcpy(poo, workspace + *datalen - 32, 32);
	}
	return 0;
}

static int decrypt_frame(int callno, struct ast_iax2_full_hdr *fh, struct ast_frame *f, int *datalen)
{
	int res=-1;
	if (!ast_test_flag64(iaxs[callno], IAX_KEYPOPULATED)) {
		/* Search for possible keys, given secrets */
		struct MD5Context md5;
		unsigned char digest[16];
		char *tmppw, *stringp;

		tmppw = ast_strdupa(iaxs[callno]->secret);
		stringp = tmppw;
		while ((tmppw = strsep(&stringp, ";"))) {
			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *)iaxs[callno]->challenge, strlen(iaxs[callno]->challenge));
			MD5Update(&md5, (unsigned char *)tmppw, strlen(tmppw));
			MD5Final(digest, &md5);
			build_encryption_keys(digest, iaxs[callno]);
			res = decode_frame(&iaxs[callno]->dcx, fh, f, datalen);
			if (!res) {
				ast_set_flag64(iaxs[callno], IAX_KEYPOPULATED);
				break;
			}
		}
	} else
		res = decode_frame(&iaxs[callno]->dcx, fh, f, datalen);
	return res;
}

static int iax2_send(struct chan_iax2_pvt *pvt, struct ast_frame *f, unsigned int ts, int seqno, int now, int transfer, int final)
{
	/* Queue a packet for delivery on a given private structure.  Use "ts" for
	   timestamp, or calculate if ts is 0.  Send immediately without retransmission
	   or delayed, with retransmission */
	struct ast_iax2_full_hdr *fh;
	struct ast_iax2_mini_hdr *mh;
	struct ast_iax2_video_hdr *vh;
	struct {
		struct iax_frame fr2;
		unsigned char buffer[4096];
	} frb;
	struct iax_frame *fr;
	int res;
	int sendmini=0;
	unsigned int lastsent;
	unsigned int fts;

	frb.fr2.afdatalen = sizeof(frb.buffer);

	if (!pvt) {
		ast_log(LOG_WARNING, "No private structure for packet?\n");
		return -1;
	}

	lastsent = pvt->lastsent;

	/* Calculate actual timestamp */
	fts = calc_timestamp(pvt, ts, f);

	/* Bail here if this is an "interp" frame; we don't want or need to send these placeholders out
	 * (the endpoint should detect the lost packet itself).  But, we want to do this here, so that we
	 * increment the "predicted timestamps" for voice, if we're predicting */
	if(f->frametype == AST_FRAME_VOICE && f->datalen == 0)
		return 0;
#if 0
	ast_log(LOG_NOTICE,
		"f->frametype %c= AST_FRAME_VOICE, %sencrypted, %srotation scheduled...\n",
		*("=!" + (f->frametype == AST_FRAME_VOICE)),
		IAX_CALLENCRYPTED(pvt) ? "" : "not ",
		pvt->keyrotateid != -1 ? "" : "no "
	);
#endif
	if (pvt->keyrotateid == -1 && f->frametype == AST_FRAME_VOICE && IAX_CALLENCRYPTED(pvt)) {
		iax2_key_rotate(pvt);
	}

	if ((ast_test_flag64(pvt, IAX_TRUNK) ||
			(((fts & 0xFFFF0000L) == (lastsent & 0xFFFF0000L)) ||
			((fts & 0xFFFF0000L) == ((lastsent + 0x10000) & 0xFFFF0000L))))
		/* High two bytes are the same on timestamp, or sending on a trunk */ &&
	    (f->frametype == AST_FRAME_VOICE)
		/* is a voice frame */ &&
		(ast_format_cmp(f->subclass.format, ast_format_compatibility_bitfield2format(pvt->svoiceformat)) ==
			AST_FORMAT_CMP_EQUAL)
		/* is the same type */ ) {
			/* Force immediate rather than delayed transmission */
			now = 1;
			/* Mark that mini-style frame is appropriate */
			sendmini = 1;
	}
	if ( f->frametype == AST_FRAME_VIDEO ) {
		/*
		 * If the lower 15 bits of the timestamp roll over, or if
		 * the video format changed then send a full frame.
		 * Otherwise send a mini video frame
		 */
		if (((fts & 0xFFFF8000L) == (pvt->lastvsent & 0xFFFF8000L)) &&
		(ast_format_cmp(f->subclass.format, ast_format_compatibility_bitfield2format(pvt->svideoformat)) ==
			AST_FORMAT_CMP_EQUAL)
		   ) {
			now = 1;
			sendmini = 1;
		} else {
			now = 0;
			sendmini = 0;
		}
		pvt->lastvsent = fts;
	}
	if (f->frametype == AST_FRAME_IAX) {
		/* 0x8000 marks this message as TX:, this bit will be stripped later */
		pvt->last_iax_message = f->subclass.integer | MARK_IAX_SUBCLASS_TX;
		if (!pvt->first_iax_message) {
			pvt->first_iax_message = pvt->last_iax_message;
		}
	}
	/* Allocate an iax_frame */
	if (now) {
		fr = &frb.fr2;
	} else
		fr = iax_frame_new(DIRECTION_OUTGRESS, ast_test_flag64(pvt, IAX_ENCRYPTED) ? f->datalen + 32 : f->datalen, (f->frametype == AST_FRAME_VOICE) || (f->frametype == AST_FRAME_VIDEO));
	if (!fr) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	/* Copy our prospective frame into our immediate or retransmitted wrapper */
	iax_frame_wrap(fr, f);

	fr->ts = fts;
	fr->callno = pvt->callno;
	fr->transfer = transfer;
	fr->final = final;
	fr->encmethods = 0;
	if (!sendmini) {
		/* We need a full frame */
		if (seqno > -1)
			fr->oseqno = seqno;
		else
			fr->oseqno = pvt->oseqno++;
		fr->iseqno = pvt->iseqno;
		fh = (struct ast_iax2_full_hdr *)(fr->af.data.ptr - sizeof(struct ast_iax2_full_hdr));
		fh->scallno = htons(fr->callno | IAX_FLAG_FULL);
		fh->ts = htonl(fr->ts);
		fh->oseqno = fr->oseqno;
		if (transfer) {
			fh->iseqno = 0;
		} else
			fh->iseqno = fr->iseqno;
		/* Keep track of the last thing we've acknowledged */
		if (!transfer)
			pvt->aseqno = fr->iseqno;
		fh->type = fr->af.frametype & 0xFF;

		if (fr->af.frametype == AST_FRAME_VIDEO) {
			iax2_format tmpfmt = ast_format_compatibility_format2bitfield(fr->af.subclass.format);
			tmpfmt |= fr->af.subclass.frame_ending ? 0x1LL : 0;
			fh->csub = compress_subclass(tmpfmt | ((tmpfmt & 0x1LL) << 6));
		} else if (fr->af.frametype == AST_FRAME_VOICE) {
			fh->csub = compress_subclass(ast_format_compatibility_format2bitfield(fr->af.subclass.format));
		} else {
			fh->csub = compress_subclass(fr->af.subclass.integer);
		}

		if (transfer) {
			fr->dcallno = pvt->transfercallno;
		} else
			fr->dcallno = pvt->peercallno;
		fh->dcallno = htons(fr->dcallno);
		fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_full_hdr);
		fr->data = fh;
		fr->retries = 0;
		/* Retry after 2x the ping time has passed */
		fr->retrytime = pvt->pingtime * 2;
		if (fr->retrytime < MIN_RETRY_TIME)
			fr->retrytime = MIN_RETRY_TIME;
		if (fr->retrytime > MAX_RETRY_TIME)
			fr->retrytime = MAX_RETRY_TIME;
		/* Acks' don't get retried */
		if ((f->frametype == AST_FRAME_IAX) && (f->subclass.integer == IAX_COMMAND_ACK))
			fr->retries = -1;
		else if (f->frametype == AST_FRAME_VOICE)
			pvt->svoiceformat = ast_format_compatibility_format2bitfield(f->subclass.format);
		else if (f->frametype == AST_FRAME_VIDEO)
			pvt->svideoformat = ast_format_compatibility_format2bitfield(f->subclass.format);
		if (ast_test_flag64(pvt, IAX_ENCRYPTED)) {
			if (ast_test_flag64(pvt, IAX_KEYPOPULATED)) {
				if (fr->transfer)
					iax_outputframe(fr, NULL, 2, &pvt->transfer, fr->datalen - sizeof(struct ast_iax2_full_hdr));
				else
					iax_outputframe(fr, NULL, 2, &pvt->addr, fr->datalen - sizeof(struct ast_iax2_full_hdr));
				encrypt_frame(&pvt->ecx, fh, pvt->semirand, &fr->datalen);
				fr->encmethods = pvt->encmethods;
				fr->ecx = pvt->ecx;
				fr->mydcx = pvt->mydcx;
				memcpy(fr->semirand, pvt->semirand, sizeof(fr->semirand));
			} else
				ast_log(LOG_WARNING, "Supposed to send packet encrypted, but no key?\n");
		}

		if (now) {
			res = send_packet(fr);
		} else
			res = iax2_transmit(fr);
	} else {
		if (ast_test_flag64(pvt, IAX_TRUNK)) {
			iax2_trunk_queue(pvt, fr);
			res = 0;
		} else if (fr->af.frametype == AST_FRAME_VIDEO) {
			/* Video frame have no sequence number */
			fr->oseqno = -1;
			fr->iseqno = -1;
			vh = (struct ast_iax2_video_hdr *)(fr->af.data.ptr - sizeof(struct ast_iax2_video_hdr));
			vh->zeros = 0;
			vh->callno = htons(0x8000 | fr->callno);
			vh->ts = htons((fr->ts & 0x7FFF) | (fr->af.subclass.frame_ending ? 0x8000 : 0));
			fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_video_hdr);
			fr->data = vh;
			fr->retries = -1;
			res = send_packet(fr);
		} else {
			/* Mini-frames have no sequence number */
			fr->oseqno = -1;
			fr->iseqno = -1;
			/* Mini frame will do */
			mh = (struct ast_iax2_mini_hdr *)(fr->af.data.ptr - sizeof(struct ast_iax2_mini_hdr));
			mh->callno = htons(fr->callno);
			mh->ts = htons(fr->ts & 0xFFFF);
			fr->datalen = fr->af.datalen + sizeof(struct ast_iax2_mini_hdr);
			fr->data = mh;
			fr->retries = -1;
			if (pvt->transferring == TRANSFER_MEDIAPASS)
				fr->transfer = 1;
			if (ast_test_flag64(pvt, IAX_ENCRYPTED)) {
				if (ast_test_flag64(pvt, IAX_KEYPOPULATED)) {
					encrypt_frame(&pvt->ecx, (struct ast_iax2_full_hdr *)mh, pvt->semirand, &fr->datalen);
				} else
					ast_log(LOG_WARNING, "Supposed to send packet encrypted, but no key?\n");
			}
			res = send_packet(fr);
		}
	}
	return res;
}

static char *handle_cli_iax2_show_users(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	regex_t regexbuf;
	int havepattern = 0;

#define FORMAT "%-15.15s  %-20.20s  %-15.15s  %-15.15s  %-5.5s  %-5.10s\n"
#define FORMAT2 "%-15.15s  %-20.20s  %-15.15d  %-15.15s  %-5.5s  %-5.10s\n"

	struct iax2_user *user = NULL;
	char auth[90];
	char *pstr = "";
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show users [like]";
		e->usage =
			"Usage: iax2 show users [like <pattern>]\n"
			"       Lists all known IAX2 users.\n"
			"       Optional regular expression pattern is used to filter the user list.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	switch (a->argc) {
	case 5:
		if (!strcasecmp(a->argv[3], "like")) {
			if (regcomp(&regexbuf, a->argv[4], REG_EXTENDED | REG_NOSUB))
				return CLI_SHOWUSAGE;
			havepattern = 1;
		} else
			return CLI_SHOWUSAGE;
	case 3:
		break;
	default:
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, FORMAT, "Username", "Secret", "Authen", "Def.Context", "A/C","Codec Pref");
	i = ao2_iterator_init(users, 0);
	for (; (user = ao2_iterator_next(&i)); user_unref(user)) {
		if (havepattern && regexec(&regexbuf, user->name, 0, NULL, 0))
			continue;

		if (!ast_strlen_zero(user->secret)) {
			ast_copy_string(auth,user->secret, sizeof(auth));
		} else if (!ast_strlen_zero(user->inkeys)) {
			snprintf(auth, sizeof(auth), "Key: %-15.15s ", user->inkeys);
		} else
			ast_copy_string(auth, "-no secret-", sizeof(auth));

		if(ast_test_flag64(user, IAX_CODEC_NOCAP))
			pstr = "REQ Only";
		else if(ast_test_flag64(user, IAX_CODEC_NOPREFS))
			pstr = "Disabled";
		else
			pstr = ast_test_flag64(user, IAX_CODEC_USER_FIRST) ? "Caller" : "Host";

		ast_cli(a->fd, FORMAT2, user->name, auth, user->authmethods,
			user->contexts ? user->contexts->context : DEFAULT_CONTEXT,
			ast_acl_list_is_empty(user->acl) ? "No" : "Yes", pstr);
	}
	ao2_iterator_destroy(&i);

	if (havepattern)
		regfree(&regexbuf);

	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

struct show_peers_context {
	regex_t regexbuf;
	int havepattern;
	char idtext[256];
	int registeredonly;
	int peerlist;
	int total_peers;
	int online_peers;
	int offline_peers;
	int unmonitored_peers;
};

#define PEERS_FORMAT2 "%-15.15s  %-40.40s %s   %-40.40s  %-9s %s  %-11s %-32.32s\n"
#define PEERS_FORMAT "%-15.15s  %-40.40s %s  %-40.40s  %-6s%s %s  %-11s %-32.32s\n"

static void _iax2_show_peers_one(int fd, struct mansession *s, struct show_peers_context *cont, struct iax2_peer *peer)
{
	char name[256] = "";
	char status[20];
	int retstatus;
	struct ast_str *encmethods = ast_str_alloca(256);

	char *tmp_host, *tmp_mask, *tmp_port;

	tmp_host = ast_strdupa(ast_sockaddr_stringify_addr(&peer->addr));
	tmp_mask = ast_strdupa(ast_sockaddr_stringify_addr(&peer->mask));
	tmp_port = ast_strdupa(ast_sockaddr_stringify_port(&peer->addr));

	if (!ast_strlen_zero(peer->username)) {
		snprintf(name, sizeof(name), "%s/%s", peer->name, peer->username);
	} else {
		ast_copy_string(name, peer->name, sizeof(name));
	}

	encmethods_to_str(peer->encmethods, &encmethods);
	retstatus = peer_status(peer, status, sizeof(status));
	if (retstatus > 0) {
		cont->online_peers++;
	} else if (!retstatus) {
		cont->offline_peers++;
	} else {
		cont->unmonitored_peers++;
	}

	if (s) {
		if (cont->peerlist) { /* IAXpeerlist */
			astman_append(s,
				"Event: PeerEntry\r\n%s"
				"Channeltype: IAX\r\n",
				cont->idtext);
			if (!ast_strlen_zero(peer->username)) {
				astman_append(s,
					"ObjectName: %s\r\n"
					"ObjectUsername: %s\r\n",
					peer->name,
					peer->username);
			} else {
				astman_append(s,
					"ObjectName: %s\r\n",
					name);
			}
		} else { /* IAXpeers */
			astman_append(s,
				"Event: PeerEntry\r\n%s"
				"Channeltype: IAX2\r\n"
				"ObjectName: %s\r\n",
				cont->idtext,
				name);
		}
		astman_append(s,
			"ChanObjectType: peer\r\n"
			"IPaddress: %s\r\n",
			tmp_host);
		if (cont->peerlist) { /* IAXpeerlist */
			astman_append(s,
				"Mask: %s\r\n"
				"Port: %s\r\n",
				tmp_mask,
				tmp_port);
		} else { /* IAXpeers */
			astman_append(s,
				"IPport: %s\r\n",
				tmp_port);
		}
		astman_append(s,
			"Dynamic: %s\r\n"
			"Trunk: %s\r\n"
			"Encryption: %s\r\n"
			"Status: %s\r\n",
			ast_test_flag64(peer, IAX_DYNAMIC) ? "yes" : "no",
			ast_test_flag64(peer, IAX_TRUNK) ? "yes" : "no",
			peer->encmethods ? ast_str_buffer(encmethods) : "no",
			status);
		if (cont->peerlist) { /* IAXpeerlist */
			astman_append(s, "\r\n");
		} else { /* IAXpeers */
			astman_append(s,
				"Description: %s\r\n\r\n",
				peer->description);
		}
	} else {
		ast_cli(fd, PEERS_FORMAT,
			name,
			tmp_host,
			ast_test_flag64(peer, IAX_DYNAMIC) ? "(D)" : "(S)",
			tmp_mask,
			tmp_port,
			ast_test_flag64(peer, IAX_TRUNK) ? "(T)" : "   ",
			peer->encmethods ? "(E)" : "   ",
			status,
			peer->description);
	}

	cont->total_peers++;
}

static int __iax2_show_peers(int fd, int *total, struct mansession *s, const int argc, const char * const argv[])
{
	struct show_peers_context cont = {
		.havepattern = 0,
		.idtext = "",
		.registeredonly = 0,

		.peerlist = 0,

		.total_peers = 0,
		.online_peers = 0,
		.offline_peers = 0,
		.unmonitored_peers = 0,
	};

	struct ao2_iterator i;

	struct iax2_peer *peer = NULL;

	switch (argc) {
	case 6:
		if (!strcasecmp(argv[3], "registered"))
			cont.registeredonly = 1;
		else
			return RESULT_SHOWUSAGE;
		if (!strcasecmp(argv[4], "like")) {
			if (regcomp(&cont.regexbuf, argv[5], REG_EXTENDED | REG_NOSUB))
				return RESULT_SHOWUSAGE;
			cont.havepattern = 1;
		} else
			return RESULT_SHOWUSAGE;
		break;
	case 5:
		if (!strcasecmp(argv[3], "like")) {
			if (regcomp(&cont.regexbuf, argv[4], REG_EXTENDED | REG_NOSUB))
				return RESULT_SHOWUSAGE;
			cont.havepattern = 1;
		} else
			return RESULT_SHOWUSAGE;
		break;
	case 4:
		if (!strcasecmp(argv[3], "registered")) {
			cont.registeredonly = 1;
		} else {
			return RESULT_SHOWUSAGE;
		}
		break;
	case 3:
		break;
	default:
		return RESULT_SHOWUSAGE;
	}


	if (!s) {
		ast_cli(fd, PEERS_FORMAT2, "Name/Username", "Host", "   ", "Mask", "Port", "   ", "Status", "Description");
	}

	i = ao2_iterator_init(peers, 0);
	for (; (peer = ao2_iterator_next(&i)); peer_unref(peer)) {

		if (cont.registeredonly && ast_sockaddr_isnull(&peer->addr)) {
			continue;
		}
		if (cont.havepattern && regexec(&cont.regexbuf, peer->name, 0, NULL, 0)) {
			continue;
		}

		_iax2_show_peers_one(fd, s, &cont, peer);

	}
	ao2_iterator_destroy(&i);

	if (!s) {
		ast_cli(fd,"%d iax2 peers [%d online, %d offline, %d unmonitored]\n",
			cont.total_peers, cont.online_peers, cont.offline_peers, cont.unmonitored_peers);
	}

	if (cont.havepattern) {
		regfree(&cont.regexbuf);
	}

	if (total) {
		*total = cont.total_peers;
	}

	return RESULT_SUCCESS;

}
#undef PEERS_FORMAT2
#undef PEERS_FORMAT

static char *handle_cli_iax2_show_threads(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct iax2_thread *thread = NULL;
	time_t t;
	int threadcount = 0, dynamiccount = 0;
	char type;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show threads";
		e->usage =
			"Usage: iax2 show threads\n"
			"       Lists status of IAX helper threads\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "IAX2 Thread Information\n");
	time(&t);
	ast_cli(a->fd, "Idle Threads:\n");
	AST_LIST_LOCK(&idle_list);
	AST_LIST_TRAVERSE(&idle_list, thread, list) {
#ifdef DEBUG_SCHED_MULTITHREAD
		ast_cli(a->fd, "Thread %d: state=%u, update=%d, actions=%d, func='%s'\n",
			thread->threadnum, thread->iostate, (int)(t - thread->checktime), thread->actions, thread->curfunc);
#else
		ast_cli(a->fd, "Thread %d: state=%u, update=%d, actions=%d\n",
			thread->threadnum, thread->iostate, (int)(t - thread->checktime), thread->actions);
#endif
		threadcount++;
	}
	AST_LIST_UNLOCK(&idle_list);
	ast_cli(a->fd, "Active Threads:\n");
	AST_LIST_LOCK(&active_list);
	AST_LIST_TRAVERSE(&active_list, thread, list) {
		if (thread->type == IAX_THREAD_TYPE_DYNAMIC)
			type = 'D';
		else
			type = 'P';
#ifdef DEBUG_SCHED_MULTITHREAD
		ast_cli(a->fd, "Thread %c%d: state=%u, update=%d, actions=%d, func='%s'\n",
			type, thread->threadnum, thread->iostate, (int)(t - thread->checktime), thread->actions, thread->curfunc);
#else
		ast_cli(a->fd, "Thread %c%d: state=%u, update=%d, actions=%d\n",
			type, thread->threadnum, thread->iostate, (int)(t - thread->checktime), thread->actions);
#endif
		threadcount++;
	}
	AST_LIST_UNLOCK(&active_list);
	ast_cli(a->fd, "Dynamic Threads:\n");
	AST_LIST_LOCK(&dynamic_list);
	AST_LIST_TRAVERSE(&dynamic_list, thread, list) {
#ifdef DEBUG_SCHED_MULTITHREAD
		ast_cli(a->fd, "Thread %d: state=%u, update=%d, actions=%d, func='%s'\n",
			thread->threadnum, thread->iostate, (int)(t - thread->checktime), thread->actions, thread->curfunc);
#else
		ast_cli(a->fd, "Thread %d: state=%u, update=%d, actions=%d\n",
			thread->threadnum, thread->iostate, (int)(t - thread->checktime), thread->actions);
#endif
		dynamiccount++;
	}
	AST_LIST_UNLOCK(&dynamic_list);
	ast_cli(a->fd, "%d of %d threads accounted for with %d dynamic threads\n", threadcount, iaxthreadcount, dynamiccount);
	return CLI_SUCCESS;
}

static char *handle_cli_iax2_unregister(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct iax2_peer *p;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 unregister";
		e->usage =
			"Usage: iax2 unregister <peername>\n"
			"       Unregister (force expiration) an IAX2 peer from the registry.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_iax2_unregister(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	p = find_peer(a->argv[2], 1);
	if (p) {
		if (p->expire > 0) {
			struct iax2_peer *peer;

			peer = ao2_find(peers, a->argv[2], OBJ_KEY);
			if (peer) {
				expire_registry(peer_ref(peer)); /* will release its own reference when done */
				peer_unref(peer); /* ref from ao2_find() */
				ast_cli(a->fd, "Peer %s unregistered\n", a->argv[2]);
			} else {
				ast_cli(a->fd, "Peer %s not found\n", a->argv[2]);
			}
		} else {
			ast_cli(a->fd, "Peer %s not registered\n", a->argv[2]);
		}
		peer_unref(p);
	} else {
		ast_cli(a->fd, "Peer unknown: %s. Not unregistered\n", a->argv[2]);
	}
	return CLI_SUCCESS;
}

static char *complete_iax2_unregister(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	struct iax2_peer *p = NULL;
	char *res = NULL;
	int wordlen = strlen(word);

	/* 0 - iax2; 1 - unregister; 2 - <peername> */
	if (pos == 2) {
		struct ao2_iterator i = ao2_iterator_init(peers, 0);
		while ((p = ao2_iterator_next(&i))) {
			if (!strncasecmp(p->name, word, wordlen) &&
				++which > state && p->expire > 0) {
				res = ast_strdup(p->name);
				peer_unref(p);
				break;
			}
			peer_unref(p);
		}
		ao2_iterator_destroy(&i);
	}

	return res;
}

static char *handle_cli_iax2_show_peers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show peers";
		e->usage =
			"Usage: iax2 show peers [registered] [like <pattern>]\n"
			"       Lists all known IAX2 peers.\n"
			"       Optional 'registered' argument lists only peers with known addresses.\n"
			"       Optional regular expression pattern is used to filter the peer list.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	switch (__iax2_show_peers(a->fd, NULL, NULL, a->argc, a->argv)) {
	case RESULT_SHOWUSAGE:
		return CLI_SHOWUSAGE;
	case RESULT_FAILURE:
		return CLI_FAILURE;
	default:
		return CLI_SUCCESS;
	}
}

static int manager_iax2_show_netstats(struct mansession *s, const struct message *m)
{
	ast_cli_netstats(s, -1, 0);
	astman_append(s, "\r\n");
	return RESULT_SUCCESS;
}

static int firmware_show_callback(struct ast_iax2_firmware_header *header,
	void *user_data)
{
	int *fd = user_data;

	ast_cli(*fd, "%-15.15s  %-15d %-15d\n",
		header->devname,
		ntohs(header->version),
		(int) ntohl(header->datalen));

	return 0;
}

static char *handle_cli_iax2_show_firmware(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show firmware";
		e->usage =
			"Usage: iax2 show firmware\n"
			"       Lists all known IAX firmware images.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 4)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "%-15.15s  %-15.15s %-15.15s\n", "Device", "Version", "Size");

	iax_firmware_traverse(
		a->argc == 3 ? NULL : a->argv[3],
		firmware_show_callback,
		(void *) &a->fd);

	return CLI_SUCCESS;
}

/*! \brief callback to display iax peers in manager */
static int manager_iax2_show_peers(struct mansession *s, const struct message *m)
{
	static const char * const a[] = { "iax2", "show", "peers" };
	const char *id = astman_get_header(m,"ActionID");
	char idtext[256] = "";
	int total = 0;

	if (!ast_strlen_zero(id))
		snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);

	astman_send_listack(s, m, "Peer status list will follow", "start");

	/* List the peers in separate manager events */
	__iax2_show_peers(-1, &total, s, 3, a);

	/* Send final confirmation */
	astman_send_list_complete_start(s, m, "PeerlistComplete", total);
	astman_send_list_complete_end(s);
	return 0;
}

/*! \brief callback to display iax peers in manager format */
static int manager_iax2_show_peer_list(struct mansession *s, const struct message *m)
{
	struct show_peers_context cont = {
		.havepattern = 0,
		.idtext = "",
		.registeredonly = 0,

		.peerlist = 1,

		.total_peers = 0,
		.online_peers = 0,
		.offline_peers = 0,
		.unmonitored_peers = 0,
	};

	struct iax2_peer *peer = NULL;
	struct ao2_iterator i;

	const char *id = astman_get_header(m,"ActionID");

	if (!ast_strlen_zero(id)) {
		snprintf(cont.idtext, sizeof(cont.idtext), "ActionID: %s\r\n", id);
	}

	astman_send_listack(s, m, "IAX Peer status list will follow", "start");

	i = ao2_iterator_init(peers, 0);
	for (; (peer = ao2_iterator_next(&i)); peer_unref(peer)) {
		_iax2_show_peers_one(-1, s, &cont, peer);
	}
	ao2_iterator_destroy(&i);

	astman_send_list_complete_start(s, m, "PeerlistComplete", cont.total_peers);
	astman_send_list_complete_end(s);

	return RESULT_SUCCESS;
}


static char *regstate2str(int regstate)
{
	switch(regstate) {
	case REG_STATE_UNREGISTERED:
		return "Unregistered";
	case REG_STATE_REGSENT:
		return "Request Sent";
	case REG_STATE_AUTHSENT:
		return "Auth. Sent";
	case REG_STATE_REGISTERED:
		return "Registered";
	case REG_STATE_REJECTED:
		return "Rejected";
	case REG_STATE_TIMEOUT:
		return "Timeout";
	case REG_STATE_NOAUTH:
		return "No Authentication";
	default:
		return "Unknown";
	}
}

static char *handle_cli_iax2_show_registry(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT2 "%-45.45s  %-6.6s  %-10.10s  %-45.45s %8.8s  %s\n"
#define FORMAT  "%-45.45s  %-6.6s  %-10.10s  %-45.45s %8d  %s\n"

	struct iax2_registry *reg = NULL;
	char host[80];
	char perceived[80];
	int counter = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show registry";
		e->usage =
			"Usage: iax2 show registry\n"
			"       Lists all registration requests and status.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3)
		return CLI_SHOWUSAGE;
	ast_cli(a->fd, FORMAT2, "Host", "dnsmgr", "Username", "Perceived", "Refresh", "State");
	AST_LIST_LOCK(&registrations);
	AST_LIST_TRAVERSE(&registrations, reg, entry) {
		snprintf(host, sizeof(host), "%s", ast_sockaddr_stringify(&reg->addr));

		snprintf(perceived, sizeof(perceived), "%s", ast_sockaddr_isnull(&reg->addr) ? "<Unregistered>" : ast_sockaddr_stringify(&reg->addr));

		ast_cli(a->fd, FORMAT, host,
				(reg->dnsmgr) ? "Y" : "N",
				reg->username, perceived, reg->refresh, regstate2str(reg->regstate));
		counter++;
	}
	AST_LIST_UNLOCK(&registrations);
	ast_cli(a->fd, "%d IAX2 registrations.\n", counter);
	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int manager_iax2_show_registry(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	struct iax2_registry *reg = NULL;
	char idtext[256] = "";
	char host[80] = "";
	char perceived[80] = "";
	int total = 0;

	if (!ast_strlen_zero(id))
		snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);

	astman_send_listack(s, m, "Registrations will follow", "start");

	AST_LIST_LOCK(&registrations);
	AST_LIST_TRAVERSE(&registrations, reg, entry) {
		snprintf(host, sizeof(host), "%s", ast_sockaddr_stringify(&reg->addr));

		snprintf(perceived, sizeof(perceived), "%s", ast_sockaddr_isnull(&reg->addr) ? "<Unregistered>" : ast_sockaddr_stringify(&reg->addr));

		astman_append(s,
			"Event: RegistryEntry\r\n"
			"%s"
			"Host: %s\r\n"
			"DNSmanager: %s\r\n"
			"Username: %s\r\n"
			"Perceived: %s\r\n"
			"Refresh: %d\r\n"
			"State: %s\r\n"
			"\r\n", idtext, host, (reg->dnsmgr) ? "Y" : "N", reg->username, perceived,
			reg->refresh, regstate2str(reg->regstate));

		total++;
	}
	AST_LIST_UNLOCK(&registrations);

	astman_send_list_complete_start(s, m, "RegistrationsComplete", total);
	astman_send_list_complete_end(s);

	return 0;
}

static char *handle_cli_iax2_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT2 "%-20.20s  %-40.40s  %-10.10s  %-11.11s  %-11.11s  %-7.7s  %-6.6s  %-6.6s  %s  %s  %9s\n"
#define FORMAT  "%-20.20s  %-40.40s  %-10.10s  %5.5d/%5.5d  %5.5d/%5.5d  %-5.5dms  %-4.4dms  %-4.4dms  %-6.6s  %s%s  %3s%s\n"
#define FORMATB "%-20.20s  %-40.40s  %-10.10s  %5.5d/%5.5d  %5.5d/%5.5d  [Native Bridged to ID=%5.5d]\n"
	int x;
	int numchans = 0;
	char first_message[10] = { 0, };
	char last_message[10] = { 0, };

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show channels";
		e->usage =
			"Usage: iax2 show channels\n"
			"       Lists all currently active IAX channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;
	ast_cli(a->fd, FORMAT2, "Channel", "Peer", "Username", "ID (Lo/Rem)", "Seq (Tx/Rx)", "Lag", "Jitter", "JitBuf", "Format", "FirstMsg", "LastMsg");
	for (x = 0; x < ARRAY_LEN(iaxs); x++) {
		ast_mutex_lock(&iaxsl[x]);
		if (iaxs[x]) {
			int lag, jitter, localdelay;
			jb_info jbinfo;
			if (ast_test_flag64(iaxs[x], IAX_USEJITTERBUF)) {
				jb_getinfo(iaxs[x]->jb, &jbinfo);
				jitter = jbinfo.jitter;
				localdelay = jbinfo.current - jbinfo.min;
			} else {
				jitter = -1;
				localdelay = 0;
			}

			iax_frame_subclass2str(iaxs[x]->first_iax_message & ~MARK_IAX_SUBCLASS_TX, first_message, sizeof(first_message));
			iax_frame_subclass2str(iaxs[x]->last_iax_message & ~MARK_IAX_SUBCLASS_TX, last_message, sizeof(last_message));
			lag = iaxs[x]->remote_rr.delay;
			ast_cli(a->fd, FORMAT,
				iaxs[x]->owner ? ast_channel_name(iaxs[x]->owner) : "(None)",
				ast_sockaddr_stringify_addr(&iaxs[x]->addr),
				S_OR(iaxs[x]->username, "(None)"),
				iaxs[x]->callno, iaxs[x]->peercallno,
				iaxs[x]->oseqno, iaxs[x]->iseqno,
				lag,
				jitter,
				localdelay,
				iax2_getformatname(iaxs[x]->voiceformat),
				(iaxs[x]->first_iax_message & MARK_IAX_SUBCLASS_TX) ? "Tx:" : "Rx:",
				first_message,
				(iaxs[x]->last_iax_message & MARK_IAX_SUBCLASS_TX) ? "Tx:" : "Rx:",
				last_message);
			numchans++;
		}
		ast_mutex_unlock(&iaxsl[x]);
	}
	ast_cli(a->fd, "%d active IAX channel%s\n", numchans, (numchans != 1) ? "s" : "");
	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
#undef FORMATB
}

static int ast_cli_netstats(struct mansession *s, int fd, int limit_fmt)
{
	int x;
	int numchans = 0;
	char first_message[10] = { 0, };
	char last_message[10] = { 0, };
#define ACN_FORMAT1 "%-20.25s %4u %4d %4d %5d %3d %5d %4d %6d %4d %4d %5d %3d %5d %4d %6d %s%s %4s%s\n"
#define ACN_FORMAT2 "%s %u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s%s %s%s\n"
	for (x = 0; x < ARRAY_LEN(iaxs); x++) {
		ast_mutex_lock(&iaxsl[x]);
		if (iaxs[x]) {
			int localjitter, localdelay, locallost, locallosspct, localdropped, localooo;
			jb_info jbinfo;
			iax_frame_subclass2str(iaxs[x]->first_iax_message & ~MARK_IAX_SUBCLASS_TX, first_message, sizeof(first_message));
			iax_frame_subclass2str(iaxs[x]->last_iax_message & ~MARK_IAX_SUBCLASS_TX, last_message, sizeof(last_message));

			if(ast_test_flag64(iaxs[x], IAX_USEJITTERBUF)) {
				jb_getinfo(iaxs[x]->jb, &jbinfo);
				localjitter = jbinfo.jitter;
				localdelay = jbinfo.current - jbinfo.min;
				locallost = jbinfo.frames_lost;
				locallosspct = jbinfo.losspct/1000;
				localdropped = jbinfo.frames_dropped;
				localooo = jbinfo.frames_ooo;
			} else {
				localjitter = -1;
				localdelay = 0;
				locallost = -1;
				locallosspct = -1;
				localdropped = 0;
				localooo = -1;
			}
			if (s)
				astman_append(s, limit_fmt ? ACN_FORMAT1 : ACN_FORMAT2,
					iaxs[x]->owner ? ast_channel_name(iaxs[x]->owner) : "(None)",
					iaxs[x]->pingtime,
					localjitter,
					localdelay,
					locallost,
					locallosspct,
					localdropped,
					localooo,
					iaxs[x]->frames_received/1000,
					iaxs[x]->remote_rr.jitter,
					iaxs[x]->remote_rr.delay,
					iaxs[x]->remote_rr.losscnt,
					iaxs[x]->remote_rr.losspct,
					iaxs[x]->remote_rr.dropped,
					iaxs[x]->remote_rr.ooo,
					iaxs[x]->remote_rr.packets/1000,
					(iaxs[x]->first_iax_message & MARK_IAX_SUBCLASS_TX) ? "Tx:" : "Rx:",
					first_message,
					(iaxs[x]->last_iax_message & MARK_IAX_SUBCLASS_TX) ? "Tx:" : "Rx:",
					last_message);
			else
				ast_cli(fd, limit_fmt ? ACN_FORMAT1 : ACN_FORMAT2,
					iaxs[x]->owner ? ast_channel_name(iaxs[x]->owner) : "(None)",
					iaxs[x]->pingtime,
					localjitter,
					localdelay,
					locallost,
					locallosspct,
					localdropped,
					localooo,
					iaxs[x]->frames_received/1000,
					iaxs[x]->remote_rr.jitter,
					iaxs[x]->remote_rr.delay,
					iaxs[x]->remote_rr.losscnt,
					iaxs[x]->remote_rr.losspct,
					iaxs[x]->remote_rr.dropped,
					iaxs[x]->remote_rr.ooo,
					iaxs[x]->remote_rr.packets/1000,
					(iaxs[x]->first_iax_message & MARK_IAX_SUBCLASS_TX) ? "Tx:" : "Rx:",
					first_message,
					(iaxs[x]->last_iax_message & MARK_IAX_SUBCLASS_TX) ? "Tx:" : "Rx:",
					last_message);
			numchans++;
		}
		ast_mutex_unlock(&iaxsl[x]);
	}

	return numchans;
}

static char *handle_cli_iax2_show_netstats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int numchans = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 show netstats";
		e->usage =
			"Usage: iax2 show netstats\n"
			"       Lists network status for all currently active IAX channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3)
		return CLI_SHOWUSAGE;
	ast_cli(a->fd, "                           -------- LOCAL ---------------------  -------- REMOTE --------------------\n");
	ast_cli(a->fd, "Channel               RTT  Jit  Del  Lost   %%  Drop  OOO  Kpkts  Jit  Del  Lost   %%  Drop  OOO  Kpkts FirstMsg    LastMsg\n");
	numchans = ast_cli_netstats(NULL, a->fd, 1);
	ast_cli(a->fd, "%d active IAX channel%s\n", numchans, (numchans != 1) ? "s" : "");
	return CLI_SUCCESS;
}

static char *handle_cli_iax2_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 set debug {on|off|peer}";
		e->usage =
			"Usage: iax2 set debug {on|off|peer peername}\n"
			"       Enables/Disables dumping of IAX packets for debugging purposes.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4 && !strcasecmp(a->argv[3], "peer"))
			return complete_iax2_peers(a->line, a->word, a->pos, a->n, 0);
		return NULL;
	}

	if (a->argc < e->args  || a->argc > e->args + 1)
		return CLI_SHOWUSAGE;

	if (!strcasecmp(a->argv[3], "peer")) {
		struct iax2_peer *peer;

		if (a->argc != e->args + 1)
			return CLI_SHOWUSAGE;

		peer = find_peer(a->argv[4], 1);

		if (!peer) {
			ast_cli(a->fd, "IAX2 peer '%s' does not exist\n", a->argv[e->args-1]);
			return CLI_FAILURE;
		}

		ast_sockaddr_copy(&debugaddr, &peer->addr);

		ast_cli(a->fd, "IAX2 Debugging Enabled for IP: %s\n", ast_sockaddr_stringify_port(&debugaddr));

		ao2_ref(peer, -1);
	} else if (!strncasecmp(a->argv[3], "on", 2)) {
		iaxdebug = 1;
		ast_cli(a->fd, "IAX2 Debugging Enabled\n");
	} else {
		iaxdebug = 0;
		memset(&debugaddr, 0, sizeof(debugaddr));
		ast_cli(a->fd, "IAX2 Debugging Disabled\n");
	}
	return CLI_SUCCESS;
}

static char *handle_cli_iax2_set_debug_trunk(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 set debug trunk {on|off}";
		e->usage =
			"Usage: iax2 set debug trunk {on|off}\n"
			"       Enables/Disables debugging of IAX trunking\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args - 1], "on", 2)) {
		iaxtrunkdebug = 1;
		ast_cli(a->fd, "IAX2 Trunk Debugging Enabled\n");
	} else {
		iaxtrunkdebug = 0;
		ast_cli(a->fd, "IAX2 Trunk Debugging Disabled\n");
	}
	return CLI_SUCCESS;
}

static char *handle_cli_iax2_set_debug_jb(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 set debug jb {on|off}";
		e->usage =
			"Usage: iax2 set debug jb {on|off}\n"
			"       Enables/Disables jitterbuffer debugging information\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args -1], "on", 2)) {
		jb_setoutput(jb_error_output, jb_warning_output, jb_debug_output);
		ast_cli(a->fd, "IAX2 Jitterbuffer Debugging Enabled\n");
	} else {
		jb_setoutput(jb_error_output, jb_warning_output, NULL);
		ast_cli(a->fd, "IAX2 Jitterbuffer Debugging Disabled\n");
	}
	return CLI_SUCCESS;
}

static int iax2_write(struct ast_channel *c, struct ast_frame *f)
{
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(c));
	int res = -1;
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
	/* If there's an outstanding error, return failure now */
		if (!iaxs[callno]->error) {
			if (ast_test_flag64(iaxs[callno], IAX_ALREADYGONE))
				res = 0;
				/* Don't waste bandwidth sending null frames */
			else if (f->frametype == AST_FRAME_NULL)
				res = 0;
			else if ((f->frametype == AST_FRAME_VOICE) && ast_test_flag64(iaxs[callno], IAX_QUELCH))
				res = 0;
			else if (!ast_test_flag(&iaxs[callno]->state, IAX_STATE_STARTED))
				res = 0;
			else
			/* Simple, just queue for transmission */
				res = iax2_send(iaxs[callno], f, 0, -1, 0, 0, 0);
		} else {
			ast_debug(1, "Write error: %s\n", strerror(errno));
		}
	}
	/* If it's already gone, just return */
	ast_mutex_unlock(&iaxsl[callno]);
	return res;
}

static int __send_command(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, const unsigned char *data, int datalen, int seqno,
		int now, int transfer, int final)
{
	struct ast_frame f = { 0, };
	int res = 0;

	f.frametype = type;
	f.subclass.integer = command;
	f.datalen = datalen;
	f.src = __FUNCTION__;
	f.data.ptr = (void *) data;

	if ((res = queue_signalling(i, &f)) <= 0) {
		return res;
	}

	return iax2_send(i, &f, ts, seqno, now, transfer, final);
}

static int send_command(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, const unsigned char *data, int datalen, int seqno)
{
	if (type == AST_FRAME_CONTROL && !iax2_is_control_frame_allowed(command)) {
		/* Control frame should not go out on the wire. */
		ast_debug(2, "Callno %d: Blocked sending control frame %d.\n",
			i->callno, command);
		return 0;
	}
	return __send_command(i, type, command, ts, data, datalen, seqno, 0, 0, 0);
}

static int send_command_locked(unsigned short callno, char type, int command, unsigned int ts, const unsigned char *data, int datalen, int seqno)
{
	int res;
	ast_mutex_lock(&iaxsl[callno]);
	res = send_command(iaxs[callno], type, command, ts, data, datalen, seqno);
	ast_mutex_unlock(&iaxsl[callno]);
	return res;
}

/*!
 * \note Since this function calls iax2_predestroy() -> iax2_queue_hangup(),
 *       the pvt struct for the given call number may disappear during its
 *       execution.
 */
static int send_command_final(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, const unsigned char *data, int datalen, int seqno)
{
	int call_num = i->callno;
	/* It is assumed that the callno has already been locked */
	iax2_predestroy(i->callno);
	if (!iaxs[call_num])
		return -1;
	return __send_command(i, type, command, ts, data, datalen, seqno, 0, 0, 1);
}

static int send_command_immediate(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, const unsigned char *data, int datalen, int seqno)
{
	return __send_command(i, type, command, ts, data, datalen, seqno, 1, 0, 0);
}

static int send_command_transfer(struct chan_iax2_pvt *i, char type, int command, unsigned int ts, const unsigned char *data, int datalen)
{
	return __send_command(i, type, command, ts, data, datalen, 0, 0, 1, 0);
}

static int apply_context(struct iax2_context *con, const char *context)
{
	while(con) {
		if (!strcmp(con->context, context) || !strcmp(con->context, "*"))
			return -1;
		con = con->next;
	}
	return 0;
}


static int check_access(int callno, struct ast_sockaddr *addr, struct iax_ies *ies)
{
	/* Start pessimistic */
	int res = -1;
	int version = 2;
	struct iax2_user *user = NULL, *best = NULL;
	int bestscore = 0;
	int gotcapability = 0;
	struct ast_variable *v = NULL, *tmpvar = NULL;
	struct ao2_iterator i;

	if (!iaxs[callno])
		return res;
	if (ies->called_number)
		ast_string_field_set(iaxs[callno], exten, ies->called_number);
	if (ies->calling_number) {
		if (ast_test_flag64(&globalflags, IAX_SHRINKCALLERID)) {
			ast_shrink_phone_number(ies->calling_number);
		}
		ast_string_field_set(iaxs[callno], cid_num, ies->calling_number);
	}
	if (ies->calling_name)
		ast_string_field_set(iaxs[callno], cid_name, ies->calling_name);
	if (ies->calling_ani)
		ast_string_field_set(iaxs[callno], ani, ies->calling_ani);
	if (ies->dnid)
		ast_string_field_set(iaxs[callno], dnid, ies->dnid);
	if (ies->rdnis)
		ast_string_field_set(iaxs[callno], rdnis, ies->rdnis);
	if (ies->called_context)
		ast_string_field_set(iaxs[callno], context, ies->called_context);
	if (ies->language)
		ast_string_field_set(iaxs[callno], language, ies->language);
	if (ies->username)
		ast_string_field_set(iaxs[callno], username, ies->username);
	if (ies->calling_ton > -1)
		iaxs[callno]->calling_ton = ies->calling_ton;
	if (ies->calling_tns > -1)
		iaxs[callno]->calling_tns = ies->calling_tns;
	if (ies->calling_pres > -1)
		iaxs[callno]->calling_pres = ies->calling_pres;
	if (ies->format)
		iaxs[callno]->peerformat = ies->format;
	if (ies->adsicpe)
		iaxs[callno]->peeradsicpe = ies->adsicpe;
	if (ies->capability) {
		gotcapability = 1;
		iaxs[callno]->peercapability = ies->capability;
	}
	if (ies->version)
		version = ies->version;

	/* Use provided preferences until told otherwise for actual preferences */
	if (ies->codec_prefs) {
		iax2_codec_pref_convert(&iaxs[callno]->rprefs, ies->codec_prefs, 32, 0);
	} else {
		memset(&iaxs[callno]->rprefs, 0, sizeof(iaxs[callno]->rprefs));
	}
	iaxs[callno]->prefs = iaxs[callno]->rprefs;

	if (!gotcapability) {
		iaxs[callno]->peercapability = iaxs[callno]->peerformat;
	}
	if (version > IAX_PROTO_VERSION) {
		ast_log(LOG_WARNING, "Peer '%s' has too new a protocol version (%d) for me\n",
				ast_sockaddr_stringify_addr(addr), version);
		return res;
	}
	/* Search the userlist for a compatible entry, and fill in the rest */
	i = ao2_iterator_init(users, 0);
	while ((user = ao2_iterator_next(&i))) {
		if ((ast_strlen_zero(iaxs[callno]->username) ||				/* No username specified */
			!strcmp(iaxs[callno]->username, user->name))	/* Or this username specified */
			&& (ast_apply_acl(user->acl, addr, "IAX2 user ACL: ") == AST_SENSE_ALLOW)	/* Access is permitted from this IP */
			&& (ast_strlen_zero(iaxs[callno]->context) ||			/* No context specified */
			     apply_context(user->contexts, iaxs[callno]->context))) {			/* Context is permitted */
			if (!ast_strlen_zero(iaxs[callno]->username)) {
				/* Exact match, stop right now. */
				if (best)
					user_unref(best);
				best = user;
				break;
			} else if (ast_strlen_zero(user->secret) && ast_strlen_zero(user->dbsecret) && ast_strlen_zero(user->inkeys)) {
				/* No required authentication */
				if (user->acl) {
					/* There was host authentication and we passed, bonus! */
					if (bestscore < 4) {
						bestscore = 4;
						if (best)
							user_unref(best);
						best = user;
						continue;
					}
				} else {
					/* No host access, but no secret, either, not bad */
					if (bestscore < 3) {
						bestscore = 3;
						if (best)
							user_unref(best);
						best = user;
						continue;
					}
				}
			} else {
				if (user->acl) {
					/* Authentication, but host access too, eh, it's something.. */
					if (bestscore < 2) {
						bestscore = 2;
						if (best)
							user_unref(best);
						best = user;
						continue;
					}
				} else {
					/* Authentication and no host access...  This is our baseline */
					if (bestscore < 1) {
						bestscore = 1;
						if (best)
							user_unref(best);
						best = user;
						continue;
					}
				}
			}
		}
		user_unref(user);
	}
	ao2_iterator_destroy(&i);
	user = best;
	if (!user && !ast_strlen_zero(iaxs[callno]->username)) {
		user = realtime_user(iaxs[callno]->username, addr);
		if (user && (ast_apply_acl(user->acl, addr, "IAX2 user ACL: ") == AST_SENSE_DENY		/* Access is denied from this IP */
			|| (!ast_strlen_zero(iaxs[callno]->context) &&					/* No context specified */
				!apply_context(user->contexts, iaxs[callno]->context)))) {	/* Context is permitted */
			user = user_unref(user);
		}
	}
	if (user) {
		/* We found our match (use the first) */
		/* copy vars */
		for (v = user->vars ; v ; v = v->next) {
			if((tmpvar = ast_variable_new(v->name, v->value, v->file))) {
				tmpvar->next = iaxs[callno]->vars;
				iaxs[callno]->vars = tmpvar;
			}
		}
		/* If a max AUTHREQ restriction is in place, activate it */
		if (user->maxauthreq > 0)
			ast_set_flag64(iaxs[callno], IAX_MAXAUTHREQ);
		iaxs[callno]->prefs = user->prefs;
		ast_copy_flags64(iaxs[callno], user, IAX_CODEC_USER_FIRST | IAX_IMMEDIATE | IAX_CODEC_NOPREFS | IAX_CODEC_NOCAP | IAX_FORCE_ENCRYPT);
		iaxs[callno]->encmethods = user->encmethods;
		/* Store the requested username if not specified */
		if (ast_strlen_zero(iaxs[callno]->username))
			ast_string_field_set(iaxs[callno], username, user->name);
		/* Store whether this is a trunked call, too, of course, and move if appropriate */
		ast_copy_flags64(iaxs[callno], user, IAX_TRUNK);
		iaxs[callno]->capability = user->capability;
		/* And use the default context */
		if (ast_strlen_zero(iaxs[callno]->context)) {
			if (user->contexts)
				ast_string_field_set(iaxs[callno], context, user->contexts->context);
			else
				ast_string_field_set(iaxs[callno], context, DEFAULT_CONTEXT);
		}
		/* And any input keys */
		ast_string_field_set(iaxs[callno], inkeys, user->inkeys);
		/* And the permitted authentication methods */
		iaxs[callno]->authmethods = user->authmethods;
		iaxs[callno]->adsi = user->adsi;
		/* If the user has callerid, override the remote caller id. */
		if (ast_test_flag64(user, IAX_HASCALLERID)) {
			iaxs[callno]->calling_tns = 0;
			iaxs[callno]->calling_ton = 0;
			ast_string_field_set(iaxs[callno], cid_num, user->cid_num);
			ast_string_field_set(iaxs[callno], cid_name, user->cid_name);
			ast_string_field_set(iaxs[callno], ani, user->cid_num);
			iaxs[callno]->calling_pres = AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
		} else if (ast_strlen_zero(iaxs[callno]->cid_num) && ast_strlen_zero(iaxs[callno]->cid_name)) {
			iaxs[callno]->calling_pres = AST_PRES_NUMBER_NOT_AVAILABLE;
		} /* else user is allowed to set their own CID settings */
		if (!ast_strlen_zero(user->accountcode))
			ast_string_field_set(iaxs[callno], accountcode, user->accountcode);
		if (!ast_strlen_zero(user->mohinterpret))
			ast_string_field_set(iaxs[callno], mohinterpret, user->mohinterpret);
		if (!ast_strlen_zero(user->mohsuggest))
			ast_string_field_set(iaxs[callno], mohsuggest, user->mohsuggest);
		if (!ast_strlen_zero(user->parkinglot))
			ast_string_field_set(iaxs[callno], parkinglot, user->parkinglot);
		if (user->amaflags)
			iaxs[callno]->amaflags = user->amaflags;
		if (!ast_strlen_zero(user->language))
			ast_string_field_set(iaxs[callno], language, user->language);
		ast_copy_flags64(iaxs[callno], user, IAX_NOTRANSFER | IAX_TRANSFERMEDIA | IAX_USEJITTERBUF | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
		/* Keep this check last */
		if (!ast_strlen_zero(user->dbsecret)) {
			char *family, *key=NULL;
			char buf[80];
			family = ast_strdupa(user->dbsecret);
			key = strchr(family, '/');
			if (key) {
				*key = '\0';
				key++;
			}
			if (!key || ast_db_get(family, key, buf, sizeof(buf)))
				ast_log(LOG_WARNING, "Unable to retrieve database password for family/key '%s'!\n", user->dbsecret);
			else
				ast_string_field_set(iaxs[callno], secret, buf);
		} else
			ast_string_field_set(iaxs[callno], secret, user->secret);
		res = 0;
		user = user_unref(user);
	} else {
		 /* user was not found, but we should still fake an AUTHREQ.
		  * Set authmethods to the last known authmethod used by the system
		  * Set a fake secret, it's not looked at, just required to attempt authentication.
		  * Set authrej so the AUTHREP is rejected without even looking at its contents */
		iaxs[callno]->authmethods = last_authmethod ? last_authmethod : (IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT);
		ast_string_field_set(iaxs[callno], secret, "badsecret");
		iaxs[callno]->authrej = 1;
		if (!ast_strlen_zero(iaxs[callno]->username)) {
			/* only send the AUTHREQ if a username was specified. */
			res = 0;
		}
	}
	ast_set2_flag64(iaxs[callno], iax2_getpeertrunk(*addr), IAX_TRUNK);
	return res;
}

static int raw_hangup(struct ast_sockaddr *addr, unsigned short src, unsigned short dst, int sockfd)
{
	struct ast_iax2_full_hdr fh;
	fh.scallno = htons(src | IAX_FLAG_FULL);
	fh.dcallno = htons(dst);
	fh.ts = 0;
	fh.oseqno = 0;
	fh.iseqno = 0;
	fh.type = AST_FRAME_IAX;
	fh.csub = compress_subclass(IAX_COMMAND_INVAL);
	iax_outputframe(NULL, &fh, 0, addr, 0);

	ast_debug(1, "Raw Hangup %s, src=%d, dst=%d\n", ast_sockaddr_stringify(addr), src, dst);
	return ast_sendto(sockfd, &fh, sizeof(fh), 0, addr);
}

static void merge_encryption(struct chan_iax2_pvt *p, unsigned int enc)
{
	/* Select exactly one common encryption if there are any */
	p->encmethods &= enc;
	if (p->encmethods) {
		if (!(p->encmethods & IAX_ENCRYPT_KEYROTATE)){ /* if key rotation is not supported, turn off keyrotation. */
			p->keyrotateid = -2;
		}
		if (p->encmethods & IAX_ENCRYPT_AES128)
			p->encmethods = IAX_ENCRYPT_AES128;
		else
			p->encmethods = 0;
	}
}

/*!
 * \pre iaxsl[call_num] is locked
 *
 * \note Since this function calls send_command_final(), the pvt struct for the given
 *       call number may disappear while executing this function.
 */
static int authenticate_request(int call_num)
{
	struct iax_ie_data ied;
	int res = -1, authreq_restrict = 0;
	char challenge[10];
	struct chan_iax2_pvt *p = iaxs[call_num];

	memset(&ied, 0, sizeof(ied));

	/* If an AUTHREQ restriction is in place, make sure we can send an AUTHREQ back */
	if (ast_test_flag64(p, IAX_MAXAUTHREQ)) {
		struct iax2_user *user;

		user = ao2_find(users, p->username, OBJ_KEY);
		if (user) {
			if (user->curauthreq == user->maxauthreq)
				authreq_restrict = 1;
			else
				user->curauthreq++;
			user = user_unref(user);
		}
	}

	/* If the AUTHREQ limit test failed, send back an error */
	if (authreq_restrict) {
		iax_ie_append_str(&ied, IAX_IE_CAUSE, "Unauthenticated call limit reached");
		iax_ie_append_byte(&ied, IAX_IE_CAUSECODE, AST_CAUSE_CALL_REJECTED);
		send_command_final(p, AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied.buf, ied.pos, -1);
		return 0;
	}

	iax_ie_append_short(&ied, IAX_IE_AUTHMETHODS, p->authmethods);
	if (p->authmethods & (IAX_AUTH_MD5 | IAX_AUTH_RSA)) {
		snprintf(challenge, sizeof(challenge), "%d", (int)ast_random());
		ast_string_field_set(p, challenge, challenge);
		/* snprintf(p->challenge, sizeof(p->challenge), "%d", (int)ast_random()); */
		iax_ie_append_str(&ied, IAX_IE_CHALLENGE, p->challenge);
	}
	if (p->encmethods)
		iax_ie_append_short(&ied, IAX_IE_ENCRYPTION, p->encmethods);

	iax_ie_append_str(&ied,IAX_IE_USERNAME, p->username);

	res = send_command(p, AST_FRAME_IAX, IAX_COMMAND_AUTHREQ, 0, ied.buf, ied.pos, -1);

	if (p->encmethods)
		ast_set_flag64(p, IAX_ENCRYPTED);

	return res;
}

static int authenticate_verify(struct chan_iax2_pvt *p, struct iax_ies *ies)
{
	char requeststr[256];
	char md5secret[256] = "";
	char secret[256] = "";
	char rsasecret[256] = "";
	int res = -1;
	int x;
	struct iax2_user *user;

	if (p->authrej) {
		return res;
	}

	user = ao2_find(users, p->username, OBJ_KEY);
	if (user) {
		if (ast_test_flag64(p, IAX_MAXAUTHREQ)) {
			ast_atomic_fetchadd_int(&user->curauthreq, -1);
			ast_clear_flag64(p, IAX_MAXAUTHREQ);
		}
		ast_string_field_set(p, host, user->name);
		user = user_unref(user);
	}
	if (ast_test_flag64(p, IAX_FORCE_ENCRYPT) && !p->encmethods) {
		ast_log(LOG_NOTICE, "Call Terminated, Incoming call is unencrypted while force encrypt is enabled.\n");
		return res;
	}
	if (!ast_test_flag(&p->state, IAX_STATE_AUTHENTICATED))
		return res;
	if (ies->password)
		ast_copy_string(secret, ies->password, sizeof(secret));
	if (ies->md5_result)
		ast_copy_string(md5secret, ies->md5_result, sizeof(md5secret));
	if (ies->rsa_result)
		ast_copy_string(rsasecret, ies->rsa_result, sizeof(rsasecret));
	if ((p->authmethods & IAX_AUTH_RSA) && !ast_strlen_zero(rsasecret) && !ast_strlen_zero(p->inkeys)) {
		struct ast_key *key;
		char *keyn;
		char *tmpkey;
		char *stringp=NULL;
		if (!(tmpkey = ast_strdup(p->inkeys))) {
			ast_log(LOG_ERROR, "Unable to create a temporary string for parsing stored 'inkeys'\n");
			return res;
		}
		stringp = tmpkey;
		keyn = strsep(&stringp, ":");
		while(keyn) {
			key = ast_key_get(keyn, AST_KEY_PUBLIC);
			if (key && !ast_check_signature(key, p->challenge, rsasecret)) {
				res = 0;
				break;
			} else if (!key)
				ast_log(LOG_WARNING, "requested inkey '%s' for RSA authentication does not exist\n", keyn);
			keyn = strsep(&stringp, ":");
		}
		ast_free(tmpkey);
	} else if (p->authmethods & IAX_AUTH_MD5) {
		struct MD5Context md5;
		unsigned char digest[16];
		char *tmppw, *stringp;
		tmppw = ast_strdupa(p->secret);
		stringp = tmppw;
		while((tmppw = strsep(&stringp, ";"))) {
			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *)p->challenge, strlen(p->challenge));
			MD5Update(&md5, (unsigned char *)tmppw, strlen(tmppw));
			MD5Final(digest, &md5);
			/* If they support md5, authenticate with it.  */
			for (x=0;x<16;x++)
				sprintf(requeststr + (x << 1), "%02hhx", digest[x]); /* safe */
			if (!strcasecmp(requeststr, md5secret)) {
				res = 0;
				break;
			}
		}
	} else if (p->authmethods & IAX_AUTH_PLAINTEXT) {
		if (!strcmp(secret, p->secret))
			res = 0;
	}
	return res;
}

/*! \brief Verify inbound registration */
static int register_verify(int callno, struct ast_sockaddr *addr, struct iax_ies *ies)
{
	char requeststr[256] = "";
	char peer[256] = "";
	char md5secret[256] = "";
	char rsasecret[256] = "";
	char secret[256] = "";
	struct iax2_peer *p = NULL;
	struct ast_key *key;
	char *keyn;
	int x;
	int expire = 0;
	int res = -1;

	ast_clear_flag(&iaxs[callno]->state, IAX_STATE_AUTHENTICATED);
	/* iaxs[callno]->peer[0] = '\0'; not necc. any more-- stringfield is pre-inited to null string */
	if (ies->username)
		ast_copy_string(peer, ies->username, sizeof(peer));
	if (ies->password)
		ast_copy_string(secret, ies->password, sizeof(secret));
	if (ies->md5_result)
		ast_copy_string(md5secret, ies->md5_result, sizeof(md5secret));
	if (ies->rsa_result)
		ast_copy_string(rsasecret, ies->rsa_result, sizeof(rsasecret));
	if (ies->refresh)
		expire = ies->refresh;

	if (ast_strlen_zero(peer)) {
		ast_log(LOG_NOTICE, "Empty registration from %s\n", ast_sockaddr_stringify_addr(addr));
		return -1;
	}

	/* SLD: first call to lookup peer during registration */
	ast_mutex_unlock(&iaxsl[callno]);
	p = find_peer(peer, 1);
	ast_mutex_lock(&iaxsl[callno]);
	if (!p || !iaxs[callno]) {
		if (iaxs[callno]) {
			int plaintext = ((last_authmethod & IAX_AUTH_PLAINTEXT) | (iaxs[callno]->authmethods & IAX_AUTH_PLAINTEXT));
			/* Anything, as long as it's non-blank */
			ast_string_field_set(iaxs[callno], secret, "badsecret");
			/* An AUTHREQ must be sent in response to a REGREQ of an invalid peer unless
			 * 1. A challenge already exists indicating a AUTHREQ was already sent out.
			 * 2. A plaintext secret is present in ie as result of a previous AUTHREQ requesting it.
			 * 3. A plaintext secret is present in the ie and the last_authmethod used by a peer happened
			 *    to be plaintext, indicating it is an authmethod used by other peers on the system.
			 *
			 * If none of these cases exist, res will be returned as 0 without authentication indicating
			 * an AUTHREQ needs to be sent out. */

			if (ast_strlen_zero(iaxs[callno]->challenge) &&
				!(!ast_strlen_zero(secret) && plaintext)) {
				/* by setting res to 0, an REGAUTH will be sent */
				res = 0;
			}
		}
		if (authdebug && !p)
			ast_log(LOG_NOTICE, "No registration for peer '%s' (from %s)\n", peer, ast_sockaddr_stringify_addr(addr));
		goto return_unref;
	}

	if (!ast_test_flag64(p, IAX_DYNAMIC)) {
		if (authdebug)
			ast_log(LOG_NOTICE, "Peer '%s' is not dynamic (from %s)\n", peer, ast_sockaddr_stringify_addr(addr));
		goto return_unref;
	}

	if (!ast_apply_acl(p->acl, addr, "IAX2 Peer ACL: ")) {
		if (authdebug)
			ast_log(LOG_NOTICE, "Host %s denied access to register peer '%s'\n", ast_sockaddr_stringify_addr(addr), p->name);
		goto return_unref;
	}
	ast_string_field_set(iaxs[callno], secret, p->secret);
	ast_string_field_set(iaxs[callno], inkeys, p->inkeys);
	/* Check secret against what we have on file */
	if (!ast_strlen_zero(rsasecret) && (p->authmethods & IAX_AUTH_RSA) && !ast_strlen_zero(iaxs[callno]->challenge)) {
		if (!ast_strlen_zero(p->inkeys)) {
			char *tmpkey;
			char *stringp=NULL;
			if (!(tmpkey = ast_strdup(p->inkeys))) {
				ast_log(LOG_ERROR, "Unable to create a temporary string for parsing stored 'inkeys'\n");
				goto return_unref;
			}
			stringp = tmpkey;
			keyn = strsep(&stringp, ":");
			while(keyn) {
				key = ast_key_get(keyn, AST_KEY_PUBLIC);
				if (key && !ast_check_signature(key, iaxs[callno]->challenge, rsasecret)) {
					ast_set_flag(&iaxs[callno]->state, IAX_STATE_AUTHENTICATED);
					break;
				} else if (!key)
					ast_log(LOG_WARNING, "requested inkey '%s' does not exist\n", keyn);
				keyn = strsep(&stringp, ":");
			}
			ast_free(tmpkey);
			if (!keyn) {
				if (authdebug)
					ast_log(LOG_NOTICE, "Host %s failed RSA authentication with inkeys '%s'\n", peer, p->inkeys);
				goto return_unref;
			}
		} else {
			if (authdebug)
				ast_log(LOG_NOTICE, "Host '%s' trying to do RSA authentication, but we have no inkeys\n", peer);
			goto return_unref;
		}
	} else if (!ast_strlen_zero(md5secret) && (p->authmethods & IAX_AUTH_MD5) && !ast_strlen_zero(iaxs[callno]->challenge)) {
		struct MD5Context md5;
		unsigned char digest[16];
		char *tmppw, *stringp;

		tmppw = ast_strdupa(p->secret);
		stringp = tmppw;
		while((tmppw = strsep(&stringp, ";"))) {
			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *)iaxs[callno]->challenge, strlen(iaxs[callno]->challenge));
			MD5Update(&md5, (unsigned char *)tmppw, strlen(tmppw));
			MD5Final(digest, &md5);
			for (x=0;x<16;x++)
				sprintf(requeststr + (x << 1), "%02hhx", digest[x]); /* safe */
			if (!strcasecmp(requeststr, md5secret))
				break;
		}
		if (tmppw) {
			ast_set_flag(&iaxs[callno]->state, IAX_STATE_AUTHENTICATED);
		} else {
			if (authdebug)
				ast_log(LOG_NOTICE, "Host %s failed MD5 authentication for '%s' (%s != %s)\n", ast_sockaddr_stringify_addr(addr), p->name, requeststr, md5secret);
			goto return_unref;
		}
	} else if (!ast_strlen_zero(secret) && (p->authmethods & IAX_AUTH_PLAINTEXT)) {
		/* They've provided a plain text password and we support that */
		if (strcmp(secret, p->secret)) {
			if (authdebug)
				ast_log(LOG_NOTICE, "Host %s did not provide proper plaintext password for '%s'\n", ast_sockaddr_stringify_addr(addr), p->name);
			goto return_unref;
		} else
			ast_set_flag(&iaxs[callno]->state, IAX_STATE_AUTHENTICATED);
	} else if (!ast_strlen_zero(iaxs[callno]->challenge) && ast_strlen_zero(md5secret) && ast_strlen_zero(rsasecret)) {
		/* if challenge has been sent, but no challenge response if given, reject. */
		goto return_unref;
	}
	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "IAX2/%s", p->name); /* Activate notification */

	/* either Authentication has taken place, or a REGAUTH must be sent before verifying registration */
	res = 0;

return_unref:
	if (iaxs[callno]) {
		ast_string_field_set(iaxs[callno], peer, peer);

		/* Choose lowest expiry number */
		if (expire && (expire < iaxs[callno]->expiry)) {
			iaxs[callno]->expiry = expire;
		}
	}

	if (p) {
		peer_unref(p);
	}
	return res;
}

static int authenticate(const char *challenge, const char *secret, const char *keyn, int authmethods, struct iax_ie_data *ied, struct ast_sockaddr *addr, struct chan_iax2_pvt *pvt)
{
	int res = -1;
	int x;
	if (!ast_strlen_zero(keyn)) {
		if (!(authmethods & IAX_AUTH_RSA)) {
			if (ast_strlen_zero(secret)) {
				ast_log(LOG_NOTICE, "Asked to authenticate to %s with an RSA key, but they don't allow RSA authentication\n", ast_sockaddr_stringify_addr(addr));
			}
		} else if (ast_strlen_zero(challenge)) {
			ast_log(LOG_NOTICE, "No challenge provided for RSA authentication to %s\n", ast_sockaddr_stringify_addr(addr));
		} else {
			char sig[256];
			struct ast_key *key;
			key = ast_key_get(keyn, AST_KEY_PRIVATE);
			if (!key) {
				ast_log(LOG_NOTICE, "Unable to find private key '%s'\n", keyn);
			} else {
				if (ast_sign(key, (char*)challenge, sig)) {
					ast_log(LOG_NOTICE, "Unable to sign challenge with key\n");
					res = -1;
				} else {
					iax_ie_append_str(ied, IAX_IE_RSA_RESULT, sig);
					res = 0;
				}
			}
		}
	}
	/* Fall back */
	if (res && !ast_strlen_zero(secret)) {
		if ((authmethods & IAX_AUTH_MD5) && !ast_strlen_zero(challenge)) {
			struct MD5Context md5;
			unsigned char digest[16];
			char digres[128];
			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *)challenge, strlen(challenge));
			MD5Update(&md5, (unsigned char *)secret, strlen(secret));
			MD5Final(digest, &md5);
			/* If they support md5, authenticate with it.  */
			for (x=0;x<16;x++)
				sprintf(digres + (x << 1),  "%02hhx", digest[x]); /* safe */
			if (pvt) {
				build_encryption_keys(digest, pvt);
			}
			iax_ie_append_str(ied, IAX_IE_MD5_RESULT, digres);
			res = 0;
		} else if (authmethods & IAX_AUTH_PLAINTEXT) {
			iax_ie_append_str(ied, IAX_IE_PASSWORD, secret);
			res = 0;
		} else
			ast_log(LOG_NOTICE, "No way to send secret to peer '%s' (their methods: %d)\n", ast_sockaddr_stringify_addr(addr), authmethods);
	}
	return res;
}

/*!
 * \note This function calls realtime_peer -> reg_source_db -> iax2_poke_peer -> find_callno,
 *       so do not call this function with a pvt lock held.
 */
static int authenticate_reply(struct chan_iax2_pvt *p, struct ast_sockaddr *addr, struct iax_ies *ies, const char *override, const char *okey)
{
	struct iax2_peer *peer = NULL;
	/* Start pessimistic */
	int res = -1;
	int authmethods = 0;
	struct iax_ie_data ied;
	uint16_t callno = p->callno;

	memset(&ied, 0, sizeof(ied));

	if (ies->username)
		ast_string_field_set(p, username, ies->username);
	if (ies->challenge)
		ast_string_field_set(p, challenge, ies->challenge);
	if (ies->authmethods)
		authmethods = ies->authmethods;
	if (authmethods & IAX_AUTH_MD5)
		merge_encryption(p, ies->encmethods);
	else
		p->encmethods = 0;

	/* Check for override RSA authentication first */
	if (!ast_strlen_zero(override) || !ast_strlen_zero(okey)) {
		/* Normal password authentication */
		res = authenticate(p->challenge, override, okey, authmethods, &ied, addr, p);
	} else {
		struct ao2_iterator i = ao2_iterator_init(peers, 0);
		while ((peer = ao2_iterator_next(&i))) {
			struct ast_sockaddr peer_addr;
			struct ast_sockaddr tmp_sockaddr1;
			struct ast_sockaddr tmp_sockaddr2;

			ast_sockaddr_copy(&peer_addr, &peer->addr);

			ast_sockaddr_apply_netmask(addr, &peer->mask, &tmp_sockaddr1);
			ast_sockaddr_apply_netmask(&peer_addr, &peer->mask, &tmp_sockaddr2);

			if ((ast_strlen_zero(p->peer) || !strcmp(p->peer, peer->name))
				/* No peer specified at our end, or this is the peer */
			    && (ast_strlen_zero(peer->username) || (!strcmp(peer->username, p->username)))
			    /* No username specified in peer rule, or this is the right username */
			    && (ast_sockaddr_isnull(&peer_addr) || !(ast_sockaddr_cmp_addr(&tmp_sockaddr1, &tmp_sockaddr2)))
			    /* No specified host, or this is our host */
				) {
				res = authenticate(p->challenge, peer->secret, peer->outkey, authmethods, &ied, addr, p);
				if (!res) {
					peer_unref(peer);
					break;
				}
			}
			peer_unref(peer);
		}
		ao2_iterator_destroy(&i);
		if (!peer) {
			/* We checked our list and didn't find one.  It's unlikely, but possible,
			   that we're trying to authenticate *to* a realtime peer */
			const char *peer_name = ast_strdupa(p->peer);
			ast_mutex_unlock(&iaxsl[callno]);
			if ((peer = realtime_peer(peer_name, NULL))) {
				ast_mutex_lock(&iaxsl[callno]);
				if (!(p = iaxs[callno])) {
					peer_unref(peer);
					return -1;
				}
				res = authenticate(p->challenge, peer->secret,peer->outkey, authmethods, &ied, addr, p);
				peer_unref(peer);
			}
			if (!peer) {
				ast_mutex_lock(&iaxsl[callno]);
				if (!(p = iaxs[callno]))
					return -1;
			}
		}
	}

	if (ies->encmethods) {
		ast_set_flag64(p, IAX_ENCRYPTED | IAX_KEYPOPULATED);
	} else if (ast_test_flag64(iaxs[callno], IAX_FORCE_ENCRYPT)) {
		ast_log(LOG_NOTICE, "Call initiated without encryption while forceencryption=yes option is set\n");
		return -1;             /* if force encryption is yes, and no encryption methods, then return -1 to hangup */
	}
	if (!res) {
		struct ast_datastore *variablestore;
		struct ast_variable *var, *prev = NULL;
		AST_LIST_HEAD(, ast_var_t) *varlist;
		varlist = ast_calloc(1, sizeof(*varlist));
		variablestore = ast_datastore_alloc(&iax2_variable_datastore_info, NULL);
		if (variablestore && varlist && p->owner) {
			variablestore->data = varlist;
			variablestore->inheritance = DATASTORE_INHERIT_FOREVER;
			AST_LIST_HEAD_INIT(varlist);
			for (var = ies->vars; var; var = var->next) {
				struct ast_var_t *newvar = ast_var_assign(var->name, var->value);
				if (prev)
					ast_free(prev);
				prev = var;
				if (!newvar) {
					/* Don't abort list traversal, as this would leave ies->vars in an inconsistent state. */
					ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
				} else {
					AST_LIST_INSERT_TAIL(varlist, newvar, entries);
				}
			}
			if (prev)
				ast_free(prev);
			ies->vars = NULL;
			ast_channel_datastore_add(p->owner, variablestore);
		} else {
			if (p->owner)
				ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
			if (variablestore)
				ast_datastore_free(variablestore);
			if (varlist)
				ast_free(varlist);
		}
	}

	if (!res)
		res = send_command(p, AST_FRAME_IAX, IAX_COMMAND_AUTHREP, 0, ied.buf, ied.pos, -1);
	return res;
}

static int iax2_do_register(struct iax2_registry *reg);

static void __iax2_do_register_s(const void *data)
{
	struct iax2_registry *reg = (struct iax2_registry *)data;

	if (ast_sockaddr_isnull(&reg->addr)) {
		reg->addr.ss.ss_family = AST_AF_UNSPEC;
		ast_dnsmgr_lookup(reg->hostname, &reg->addr, &reg->dnsmgr, srvlookup ? "_iax._udp" : NULL);
		if (!ast_sockaddr_port(&reg->addr)) {
			ast_sockaddr_set_port(&reg->addr, reg->port);
		} else {
			reg->port = ast_sockaddr_port(&reg->addr);
		}
	}

	reg->expire = -1;
	iax2_do_register(reg);
}

static int iax2_do_register_s(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__iax2_do_register_s, data))
#endif
		__iax2_do_register_s(data);
	return 0;
}

static int try_transfer(struct chan_iax2_pvt *pvt, struct iax_ies *ies)
{
	int newcall = 0;
	struct iax_ie_data ied;
	struct ast_sockaddr new;

	memset(&ied, 0, sizeof(ied));
	if (!ast_sockaddr_isnull(&ies->apparent_addr)) {
		ast_sockaddr_copy(&new, &ies->apparent_addr);
	}
	if (ies->callno) {
		newcall = ies->callno;
	}
	if (!newcall || ast_sockaddr_isnull(&new)) {
		ast_log(LOG_WARNING, "Invalid transfer request\n");
		return -1;
	}
	pvt->transfercallno = newcall;
	ast_sockaddr_copy(&pvt->transfer, &new);
	pvt->transferid = ies->transferid;
	/* only store by transfercallno if this is a new transfer,
	 * just in case we get a duplicate TXREQ */
	if (pvt->transferring == TRANSFER_NONE) {
		store_by_transfercallno(pvt);
	}
	pvt->transferring = TRANSFER_BEGIN;

	if (ies->transferid) {
		iax_ie_append_int(&ied, IAX_IE_TRANSFERID, ies->transferid);
	}
	send_command_transfer(pvt, AST_FRAME_IAX, IAX_COMMAND_TXCNT, 0, ied.buf, ied.pos);
	return 0;
}

static int complete_dpreply(struct chan_iax2_pvt *pvt, struct iax_ies *ies)
{
	char exten[256] = "";
	int status = CACHE_FLAG_UNKNOWN, expiry = iaxdefaultdpcache, x, matchmore = 0;
	struct iax2_dpcache *dp = NULL;

	if (ies->called_number)
		ast_copy_string(exten, ies->called_number, sizeof(exten));

	if (ies->dpstatus & IAX_DPSTATUS_EXISTS)
		status = CACHE_FLAG_EXISTS;
	else if (ies->dpstatus & IAX_DPSTATUS_CANEXIST)
		status = CACHE_FLAG_CANEXIST;
	else if (ies->dpstatus & IAX_DPSTATUS_NONEXISTENT)
		status = CACHE_FLAG_NONEXISTENT;

	if (ies->refresh)
		expiry = ies->refresh;
	if (ies->dpstatus & IAX_DPSTATUS_MATCHMORE)
		matchmore = CACHE_FLAG_MATCHMORE;

	AST_LIST_LOCK(&dpcache);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&dpcache, dp, peer_list) {
		if (strcmp(dp->exten, exten))
			continue;
		AST_LIST_REMOVE_CURRENT(peer_list);
		dp->callno = 0;
		dp->expiry.tv_sec = dp->orig.tv_sec + expiry;
		if (dp->flags & CACHE_FLAG_PENDING) {
			dp->flags &= ~CACHE_FLAG_PENDING;
			dp->flags |= status;
			dp->flags |= matchmore;
		}
		/* Wake up waiters */
		for (x = 0; x < ARRAY_LEN(dp->waiters); x++) {
			if (dp->waiters[x] > -1) {
				if (write(dp->waiters[x], "asdf", 4) < 0) {
				}
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&dpcache);

	return 0;
}

static int complete_transfer(int callno, struct iax_ies *ies)
{
	int peercallno = 0;
	struct chan_iax2_pvt *pvt = iaxs[callno];
	struct iax_frame *cur;
	jb_frame frame;

	if (ies->callno)
		peercallno = ies->callno;

	if (peercallno < 1) {
		ast_log(LOG_WARNING, "Invalid transfer request\n");
		return -1;
	}
	remove_by_transfercallno(pvt);
	/* since a transfer has taken place, the address will change.
	 * This must be accounted for in the peercnts table.  Remove
	 * the old address and add the new one */
	peercnt_remove_by_addr(&pvt->addr);
	peercnt_add(&pvt->transfer);
	/* now copy over the new address */
	memcpy(&pvt->addr, &pvt->transfer, sizeof(pvt->addr));
	memset(&pvt->transfer, 0, sizeof(pvt->transfer));
	/* Reset sequence numbers */
	pvt->oseqno = 0;
	pvt->rseqno = 0;
	pvt->iseqno = 0;
	pvt->aseqno = 0;

	if (pvt->peercallno) {
		remove_by_peercallno(pvt);
	}
	pvt->peercallno = peercallno;
	/*this is where the transfering call swiches hash tables */
	store_by_peercallno(pvt);
	pvt->transferring = TRANSFER_NONE;
	pvt->svoiceformat = -1;
	pvt->voiceformat = 0;
	pvt->svideoformat = -1;
	pvt->videoformat = 0;
	pvt->transfercallno = 0;
	memset(&pvt->rxcore, 0, sizeof(pvt->rxcore));
	memset(&pvt->offset, 0, sizeof(pvt->offset));
	/* reset jitterbuffer */
	while(jb_getall(pvt->jb,&frame) == JB_OK)
		iax2_frame_free(frame.data);
	jb_reset(pvt->jb);
	pvt->lag = 0;
	pvt->last = 0;
	pvt->lastsent = 0;
	pvt->nextpred = 0;
	pvt->pingtime = DEFAULT_RETRY_TIME;
	AST_LIST_TRAVERSE(&frame_queue[callno], cur, list) {
		/* We must cancel any packets that would have been transmitted
		   because now we're talking to someone new.  It's okay, they
		   were transmitted to someone that didn't care anyway. */
		cur->retries = -1;
	}
	return 0;
}

static void iax2_publish_registry(const char *username, const char *domain, const char *status, const char *cause)
{
	ast_system_publish_registry("IAX2", username, domain, status, cause);
}

/*! \brief Acknowledgment received for OUR registration */
static int iax2_ack_registry(struct iax_ies *ies, struct ast_sockaddr *addr, int callno)
{
	struct iax2_registry *reg;
	/* Start pessimistic */
	char peer[256] = "";
	char msgstatus[60];
	int refresh = 60;
	char ourip[256] = "<Unspecified>";
	struct ast_sockaddr oldus;
	struct ast_sockaddr us;
	int oldmsgs;

	if (!ast_sockaddr_isnull(&ies->apparent_addr)) {
		ast_sockaddr_copy(&us, &ies->apparent_addr);
	}
	if (ies->username) {
		ast_copy_string(peer, ies->username, sizeof(peer));
	}
	if (ies->refresh) {
		refresh = ies->refresh;
	}
	if (ies->calling_number) {
		/* We don't do anything with it really, but maybe we should */
	}
	reg = iaxs[callno]->reg;
	if (!reg) {
		ast_log(LOG_WARNING, "Registry acknowledge on unknown registry '%s'\n", peer);
		return -1;
	}
	ast_sockaddr_copy(&oldus, &reg->us);
	oldmsgs = reg->messages;
	if (ast_sockaddr_cmp(&reg->addr, addr)) {
		ast_log(LOG_WARNING, "Received unsolicited registry ack from '%s'\n", ast_sockaddr_stringify(addr));
		return -1;
	}
	ast_sockaddr_copy(&reg->us, &us);
	if (ies->msgcount >= 0) {
		reg->messages = ies->msgcount & 0xffff;		/* only low 16 bits are used in the transmission of the IE */
	}
	/* always refresh the registration at the interval requested by the server
	   we are registering to
	*/
	reg->refresh = refresh;
	reg->expire = iax2_sched_replace(reg->expire, sched,
		(5 * reg->refresh / 6) * 1000, iax2_do_register_s, reg);
	if (ast_sockaddr_cmp(&oldus, &reg->us) || (reg->messages != oldmsgs)) {

		if (reg->messages > 255) {
			snprintf(msgstatus, sizeof(msgstatus), " with %d new and %d old messages waiting", reg->messages & 0xff, reg->messages >> 8);
		} else if (reg->messages > 1) {
			snprintf(msgstatus, sizeof(msgstatus), " with %d new messages waiting", reg->messages);
		} else if (reg->messages > 0) {
			ast_copy_string(msgstatus, " with 1 new message waiting", sizeof(msgstatus));
		} else {
			ast_copy_string(msgstatus, " with no messages waiting", sizeof(msgstatus));
		}

		snprintf(ourip, sizeof(ourip), "%s", ast_sockaddr_stringify(&reg->us));

		ast_verb(3, "Registered IAX2 to '%s', who sees us as %s%s\n", ast_sockaddr_stringify(addr), ourip, msgstatus);
		iax2_publish_registry(reg->username, ast_sockaddr_stringify(addr), "Registered", NULL);
	}
	reg->regstate = REG_STATE_REGISTERED;
	return 0;
}

static int iax2_append_register(const char *hostname, const char *username,
	const char *secret, const char *porta)
{
	struct iax2_registry *reg;

	if (!(reg = ast_calloc(1, sizeof(*reg) + strlen(hostname) + 1))) {
		return -1;
	}

	reg->addr.ss.ss_family = AST_AF_UNSPEC;
	if (ast_dnsmgr_lookup(hostname, &reg->addr, &reg->dnsmgr, srvlookup ? "_iax._udp" : NULL) < 0) {
		ast_free(reg);
		return -1;
	}

	ast_copy_string(reg->username, username, sizeof(reg->username));
	strcpy(reg->hostname, hostname); /* Note: This is safe */

	if (secret) {
		ast_copy_string(reg->secret, secret, sizeof(reg->secret));
	}

	reg->expire = -1;
	reg->refresh = IAX_DEFAULT_REG_EXPIRE;

	reg->port = ast_sockaddr_port(&reg->addr);

	if (!porta && !reg->port) {
		reg->port = IAX_DEFAULT_PORTNO;
	} else if (porta) {
		sscanf(porta, "%5d", &reg->port);
	}

	ast_sockaddr_set_port(&reg->addr, reg->port);

	AST_LIST_LOCK(&registrations);
	AST_LIST_INSERT_HEAD(&registrations, reg, entry);
	AST_LIST_UNLOCK(&registrations);

	return 0;
}

static int iax2_register(const char *value, int lineno)
{
	char copy[256];
	char *username, *hostname, *secret;
	char *porta;
	char *stringp=NULL;

	if (!value)
		return -1;

	ast_copy_string(copy, value, sizeof(copy));
	stringp = copy;
	username = strsep(&stringp, "@");
	hostname = strsep(&stringp, "@");

	if (!hostname) {
		ast_log(LOG_WARNING, "Format for registration is user[:secret]@host[:port] at line %d\n", lineno);
		return -1;
	}

	stringp = username;
	username = strsep(&stringp, ":");
	secret = strsep(&stringp, ":");
	stringp = hostname;
	hostname = strsep(&stringp, ":");
	porta = strsep(&stringp, ":");

	if (porta && !atoi(porta)) {
		ast_log(LOG_WARNING, "%s is not a valid port number at line %d\n", porta, lineno);
		return -1;
	}

	return iax2_append_register(hostname, username, secret, porta);
}


static void register_peer_exten(struct iax2_peer *peer, int onoff)
{
	char multi[256];
	char *stringp, *ext;
	if (!ast_strlen_zero(regcontext)) {
		ast_copy_string(multi, S_OR(peer->regexten, peer->name), sizeof(multi));
		stringp = multi;
		while((ext = strsep(&stringp, "&"))) {
			if (onoff) {
				if (!ast_exists_extension(NULL, regcontext, ext, 1, NULL))
					ast_add_extension(regcontext, 1, ext, 1, NULL, NULL,
							  "Noop", ast_strdup(peer->name), ast_free_ptr, "IAX2");
			} else
				ast_context_remove_extension(regcontext, ext, 1, NULL);
		}
	}
}
static void prune_peers(void);

static void unlink_peer(struct iax2_peer *peer)
{
	if (peer->expire > -1) {
		if (!AST_SCHED_DEL(sched, peer->expire)) {
			peer->expire = -1;
			peer_unref(peer);
		}
	}

	if (peer->pokeexpire > -1) {
		if (!AST_SCHED_DEL(sched, peer->pokeexpire)) {
			peer->pokeexpire = -1;
			peer_unref(peer);
		}
	}

	ao2_unlink(peers, peer);
}

static void __expire_registry(const void *data)
{
	struct iax2_peer *peer = (struct iax2_peer *) data;
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	if (!peer)
		return;
	if (peer->expire == -1) {
		/* Removed already (possibly through CLI), ignore */
		return;
	}

	peer->expire = -1;

	ast_debug(1, "Expiring registration for peer '%s'\n", peer->name);
	if (ast_test_flag64((&globalflags), IAX_RTUPDATE) && (ast_test_flag64(peer, IAX_TEMPONLY|IAX_RTCACHEFRIENDS)))
		realtime_update_peer(peer->name, &peer->addr, 0);
	ast_endpoint_set_state(peer->endpoint, AST_ENDPOINT_OFFLINE);
	blob = ast_json_pack("{s: s, s: s}",
		"peer_status", "Unregistered",
		"cause", "Expired");
	ast_endpoint_blob_publish(peer->endpoint, ast_endpoint_state_type(), blob);
	/* modify entry in peercnts table as _not_ registered */
	peercnt_modify((unsigned char) 0, 0, &peer->addr);
	/* Reset the address */
	ast_sockaddr_setnull(&peer->addr);
	/* Reset expiry value */
	peer->expiry = min_reg_expire;
	if (!ast_test_flag64(peer, IAX_TEMPONLY))
		ast_db_del("IAX/Registry", peer->name);
	register_peer_exten(peer, 0);
	ast_devstate_changed(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_CACHABLE, "IAX2/%s", peer->name); /* Activate notification */
	if (iax2_regfunk)
		iax2_regfunk(peer->name, 0);

	if (ast_test_flag64(peer, IAX_RTAUTOCLEAR))
		unlink_peer(peer);

	peer_unref(peer);
}

static int expire_registry(const void *data)
{
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__expire_registry, data))
#endif
		__expire_registry(data);
	return 0;
}

static void reg_source_db(struct iax2_peer *p)
{
	char data[80];
	char *expiry;

	if (ast_test_flag64(p, IAX_TEMPONLY) || ast_db_get("IAX/Registry", p->name, data, sizeof(data))) {
		return;
	}

	expiry = strrchr(data, ':');
	if (!expiry) {
		ast_log(LOG_NOTICE, "IAX/Registry astdb entry missing expiry: '%s'\n", data);
		return;
	}
	*expiry++ = '\0';

	if (!ast_sockaddr_parse(&p->addr, data, PARSE_PORT_REQUIRE)) {
		ast_log(LOG_NOTICE, "IAX/Registry astdb host:port invalid - '%s'\n", data);
		return;
	}

	p->expiry = atoi(expiry);

	ast_verb(3, "Seeding '%s' at %s for %d\n", p->name,
		ast_sockaddr_stringify(&p->addr), p->expiry);

	iax2_poke_peer(p, 0);
	if (p->expire > -1) {
		if (!AST_SCHED_DEL(sched, p->expire)) {
			p->expire = -1;
			peer_unref(p);
		}
	}

	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "IAX2/%s", p->name); /* Activate notification */

	p->expire = iax2_sched_add(sched, (p->expiry + 10) * 1000, expire_registry, peer_ref(p));
	if (p->expire == -1) {
		peer_unref(p);
	}

	if (iax2_regfunk) {
		iax2_regfunk(p->name, 1);
	}

	register_peer_exten(p, 1);
}

/*!
 * \pre iaxsl[callno] is locked
 *
 * \note Since this function calls send_command_final(), the pvt struct for
 *       the given call number may disappear while executing this function.
 */
static int update_registry(struct ast_sockaddr *addr, int callno, char *devtype, int fd, unsigned short refresh)
{

	/* Called from IAX thread only, with proper iaxsl lock */
	struct iax_ie_data ied = {
		.pos = 0,
	};
	struct iax2_peer *p;
	int msgcount;
	char data[80];
	uint16_t version;
	const char *peer_name;
	int res = -1;
	char *str_addr;

	peer_name = ast_strdupa(iaxs[callno]->peer);

	/* SLD: Another find_peer call during registration - this time when we are really updating our registration */
	ast_mutex_unlock(&iaxsl[callno]);
	if (!(p = find_peer(peer_name, 1))) {
		ast_mutex_lock(&iaxsl[callno]);
		ast_log(LOG_WARNING, "No such peer '%s'\n", peer_name);
		return -1;
	}
	ast_mutex_lock(&iaxsl[callno]);
	if (!iaxs[callno])
		goto return_unref;

	if (ast_test_flag64((&globalflags), IAX_RTUPDATE) && (ast_test_flag64(p, IAX_TEMPONLY|IAX_RTCACHEFRIENDS))) {
		if (!ast_sockaddr_isnull(addr)) {
			time_t nowtime;
			time(&nowtime);
			realtime_update_peer(peer_name, addr, nowtime);
		} else {
			realtime_update_peer(peer_name, addr, 0);
		}
	}

	/* treat an unspecified refresh interval as the minimum */
	if (!refresh) {
		refresh = min_reg_expire;
	}
	if (refresh > max_reg_expire) {
		ast_log(LOG_NOTICE, "Restricting registration for peer '%s' to %d seconds (requested %d)\n",
			p->name, max_reg_expire, refresh);
		p->expiry = max_reg_expire;
	} else if (refresh < min_reg_expire) {
		ast_log(LOG_NOTICE, "Restricting registration for peer '%s' to %d seconds (requested %d)\n",
			p->name, min_reg_expire, refresh);
		p->expiry = min_reg_expire;
	} else {
		p->expiry = refresh;
	}

	if (ast_sockaddr_cmp(&p->addr, addr)) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

		if (iax2_regfunk) {
			iax2_regfunk(p->name, 1);
		}

		/* modify entry in peercnts table as _not_ registered */
		peercnt_modify((unsigned char) 0, 0, &p->addr);

		/* Stash the IP address from which they registered */
		ast_sockaddr_copy(&p->addr, addr);

		str_addr = ast_strdupa(ast_sockaddr_stringify_addr(addr));

		snprintf(data, sizeof(data), "%s:%d", ast_sockaddr_stringify(addr), p->expiry);

		if (!ast_test_flag64(p, IAX_TEMPONLY) && !ast_sockaddr_isnull(addr)) {
			ast_db_put("IAX/Registry", p->name, data);
			ast_verb(3, "Registered IAX2 '%s' (%s) at %s\n",
						p->name,
					    ast_test_flag(&iaxs[callno]->state, IAX_STATE_AUTHENTICATED) ? "AUTHENTICATED" : "UNAUTHENTICATED",
					    ast_sockaddr_stringify(addr));
			ast_endpoint_set_state(p->endpoint, AST_ENDPOINT_ONLINE);
			blob = ast_json_pack("{s: s, s: s, s: i}",
				"peer_status", "Registered",
				"address", str_addr,
				"port", ast_sockaddr_port(addr));
			register_peer_exten(p, 1);
			ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "IAX2/%s", p->name); /* Activate notification */
		} else if (!ast_test_flag64(p, IAX_TEMPONLY)) {
			ast_verb(3, "Unregistered IAX2 '%s' (%s)\n",
						p->name,
					    ast_test_flag(&iaxs[callno]->state, IAX_STATE_AUTHENTICATED) ? "AUTHENTICATED" : "UNAUTHENTICATED");
			ast_endpoint_set_state(p->endpoint, AST_ENDPOINT_OFFLINE);
			blob = ast_json_pack("{s: s}",
				"peer_status", "Unregistered");
			register_peer_exten(p, 0);
			ast_db_del("IAX/Registry", p->name);
			ast_devstate_changed(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_CACHABLE, "IAX2/%s", p->name); /* Activate notification */
		}

		ast_endpoint_blob_publish(p->endpoint, ast_endpoint_state_type(), blob);

		/* Update the host */
		/* Verify that the host is really there */
		iax2_poke_peer(p, callno);
	}

	/* modify entry in peercnts table as registered */
	if (p->maxcallno) {
		peercnt_modify((unsigned char) 1, p->maxcallno, &p->addr);
	}

	/* Make sure our call still exists, an INVAL at the right point may make it go away */
	if (!iaxs[callno]) {
		res = -1;
		goto return_unref;
	}

	/* Store socket fd */
	p->sockfd = fd;
	/* Setup the expiry */
	if (p->expire > -1) {
		if (!AST_SCHED_DEL(sched, p->expire)) {
			p->expire = -1;
			peer_unref(p);
		}
	}

	if (p->expiry && !ast_sockaddr_isnull(addr)) {
		p->expire = iax2_sched_add(sched, (p->expiry + 10) * 1000, expire_registry, peer_ref(p));
		if (p->expire == -1)
			peer_unref(p);
	}
	iax_ie_append_str(&ied, IAX_IE_USERNAME, p->name);
	iax_ie_append_int(&ied, IAX_IE_DATETIME, iax2_datetime(p->zonetag));
	if (!ast_sockaddr_isnull(addr)) {
		struct ast_sockaddr peer_addr;

		ast_sockaddr_copy(&peer_addr, &p->addr);

		iax_ie_append_short(&ied, IAX_IE_REFRESH, p->expiry);
		iax_ie_append_addr(&ied, IAX_IE_APPARENT_ADDR, &peer_addr);
		if (!ast_strlen_zero(p->mailbox)) {
			int new, old;
			RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

			msg = stasis_cache_get(ast_mwi_state_cache(), ast_mwi_state_type(), p->mailbox);
			if (msg) {
				struct ast_mwi_state *mwi_state = stasis_message_data(msg);
				new = mwi_state->new_msgs;
				old = mwi_state->old_msgs;
			} else { /* Fall back on checking the mailbox directly */
				ast_app_inboxcount(p->mailbox, &new, &old);
			}

			if (new > 255) {
				new = 255;
			}
			if (old > 255) {
				old = 255;
			}
			msgcount = (old << 8) | new;

			iax_ie_append_short(&ied, IAX_IE_MSGCOUNT, msgcount);
		}
		if (ast_test_flag64(p, IAX_HASCALLERID)) {
			iax_ie_append_str(&ied, IAX_IE_CALLING_NUMBER, p->cid_num);
			iax_ie_append_str(&ied, IAX_IE_CALLING_NAME, p->cid_name);
		}
	}
	if (iax_firmware_get_version(devtype, &version)) {
		iax_ie_append_short(&ied, IAX_IE_FIRMWAREVER, version);
	}

	res = 0;

return_unref:
	peer_unref(p);

	return res ? res : send_command_final(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_REGACK, 0, ied.buf, ied.pos, -1);
}

static int registry_authrequest(int callno)
{
	struct iax_ie_data ied;
	struct iax2_peer *p;
	char challenge[10];
	const char *peer_name;
	int sentauthmethod;

	peer_name = ast_strdupa(iaxs[callno]->peer);

	/* SLD: third call to find_peer in registration */
	ast_mutex_unlock(&iaxsl[callno]);
	if ((p = find_peer(peer_name, 1))) {
		last_authmethod = p->authmethods;
	}

	ast_mutex_lock(&iaxsl[callno]);
	if (!iaxs[callno])
		goto return_unref;

	memset(&ied, 0, sizeof(ied));
	/* The selection of which delayed reject is sent may leak information,
	 * if it sets a static response.  For example, if a host is known to only
	 * use MD5 authentication, then an RSA response would indicate that the
	 * peer does not exist, and vice-versa.
	 * Therefore, we use whatever the last peer used (which may vary over the
	 * course of a server, which should leak minimal information). */
	sentauthmethod = p ? p->authmethods : last_authmethod ? last_authmethod : (IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT);
	if (!p) {
		iaxs[callno]->authmethods = sentauthmethod;
	}
	iax_ie_append_short(&ied, IAX_IE_AUTHMETHODS, sentauthmethod);
	if (sentauthmethod & (IAX_AUTH_RSA | IAX_AUTH_MD5)) {
		/* Build the challenge */
		snprintf(challenge, sizeof(challenge), "%d", (int)ast_random());
		ast_string_field_set(iaxs[callno], challenge, challenge);
		iax_ie_append_str(&ied, IAX_IE_CHALLENGE, iaxs[callno]->challenge);
	}
	iax_ie_append_str(&ied, IAX_IE_USERNAME, peer_name);

return_unref:
	if (p) {
		peer_unref(p);
	}

	return iaxs[callno] ? send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_REGAUTH, 0, ied.buf, ied.pos, -1) : -1;
}

static int registry_rerequest(struct iax_ies *ies, int callno, struct ast_sockaddr *addr)
{
	struct iax2_registry *reg;
	/* Start pessimistic */
	struct iax_ie_data ied;
	char peer[256] = "";
	char challenge[256] = "";
	int res;
	int authmethods = 0;
	if (ies->authmethods)
		authmethods = ies->authmethods;
	if (ies->username)
		ast_copy_string(peer, ies->username, sizeof(peer));
	if (ies->challenge)
		ast_copy_string(challenge, ies->challenge, sizeof(challenge));
	memset(&ied, 0, sizeof(ied));
	reg = iaxs[callno]->reg;
	if (reg) {

		if (ast_sockaddr_cmp(&reg->addr, addr)) {
			ast_log(LOG_WARNING, "Received unsolicited registry authenticate request from '%s'\n", ast_sockaddr_stringify(addr));
			return -1;
		}
		if (ast_strlen_zero(reg->secret)) {
			ast_log(LOG_NOTICE, "No secret associated with peer '%s'\n", reg->username);
			reg->regstate = REG_STATE_NOAUTH;
			return -1;
		}
		iax_ie_append_str(&ied, IAX_IE_USERNAME, reg->username);
		iax_ie_append_short(&ied, IAX_IE_REFRESH, reg->refresh);
		if (reg->secret[0] == '[') {
			char tmpkey[256];
			ast_copy_string(tmpkey, reg->secret + 1, sizeof(tmpkey));
			tmpkey[strlen(tmpkey) - 1] = '\0';
			res = authenticate(challenge, NULL, tmpkey, authmethods, &ied, addr, NULL);
		} else
			res = authenticate(challenge, reg->secret, NULL, authmethods, &ied, addr, NULL);
		if (!res) {
			reg->regstate = REG_STATE_AUTHSENT;
			add_empty_calltoken_ie(iaxs[callno], &ied); /* this _MUST_ be the last ie added */
			return send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_REGREQ, 0, ied.buf, ied.pos, -1);
		} else
			return -1;
		ast_log(LOG_WARNING, "Registry acknowledge on unknown registery '%s'\n", peer);
	} else
		ast_log(LOG_NOTICE, "Can't reregister without a reg\n");
	return -1;
}

static void stop_stuff(int callno)
{
	iax2_destroy_helper(iaxs[callno]);
}

static void __auth_reject(const void *nothing)
{
	/* Called from IAX thread only, without iaxs lock */
	int callno = (int)(long)(nothing);
	struct iax_ie_data ied;
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		memset(&ied, 0, sizeof(ied));
		if (iaxs[callno]->authfail == IAX_COMMAND_REGREJ) {
			iax_ie_append_str(&ied, IAX_IE_CAUSE, "Registration Refused");
			iax_ie_append_byte(&ied, IAX_IE_CAUSECODE, AST_CAUSE_FACILITY_REJECTED);
		} else if (iaxs[callno]->authfail == IAX_COMMAND_REJECT) {
			iax_ie_append_str(&ied, IAX_IE_CAUSE, "No authority found");
			iax_ie_append_byte(&ied, IAX_IE_CAUSECODE, AST_CAUSE_FACILITY_NOT_SUBSCRIBED);
		}
		send_command_final(iaxs[callno], AST_FRAME_IAX, iaxs[callno]->authfail, 0, ied.buf, ied.pos, -1);
	}
	ast_mutex_unlock(&iaxsl[callno]);
}

static int auth_reject(const void *data)
{
	int callno = (int)(long)(data);
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno])
		iaxs[callno]->authid = -1;
	ast_mutex_unlock(&iaxsl[callno]);
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__auth_reject, data))
#endif
		__auth_reject(data);
	return 0;
}

static int auth_fail(int callno, int failcode)
{
	/* Schedule sending the authentication failure in one second, to prevent
	   guessing */
	if (iaxs[callno]) {
		iaxs[callno]->authfail = failcode;
		if (delayreject) {
			iaxs[callno]->authid = iax2_sched_replace(iaxs[callno]->authid,
				sched, 1000, auth_reject, (void *)(long)callno);
		} else
			auth_reject((void *)(long)callno);
	}
	return 0;
}

static void __auto_hangup(const void *nothing)
{
	/* Called from IAX thread only, without iaxs lock */
	int callno = (int)(long)(nothing);
	struct iax_ie_data ied;
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		memset(&ied, 0, sizeof(ied));
		iax_ie_append_str(&ied, IAX_IE_CAUSE, "Timeout");
		iax_ie_append_byte(&ied, IAX_IE_CAUSECODE, AST_CAUSE_NO_USER_RESPONSE);
		send_command_final(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_HANGUP, 0, ied.buf, ied.pos, -1);
	}
	ast_mutex_unlock(&iaxsl[callno]);
}

static int auto_hangup(const void *data)
{
	int callno = (int)(long)(data);
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		iaxs[callno]->autoid = -1;
	}
	ast_mutex_unlock(&iaxsl[callno]);
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__auto_hangup, data))
#endif
		__auto_hangup(data);
	return 0;
}

static void iax2_dprequest(struct iax2_dpcache *dp, int callno)
{
	struct iax_ie_data ied;
	/* Auto-hangup with 30 seconds of inactivity */
	iaxs[callno]->autoid = iax2_sched_replace(iaxs[callno]->autoid,
		sched, 30000, auto_hangup, (void *)(long)callno);
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, dp->exten);
	send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_DPREQ, 0, ied.buf, ied.pos, -1);
	dp->flags |= CACHE_FLAG_TRANSMITTED;
}

static int iax2_vnak(int callno)
{
	return send_command_immediate(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_VNAK, 0, NULL, 0, iaxs[callno]->iseqno);
}

static void vnak_retransmit(int callno, int last)
{
	struct iax_frame *f;

	AST_LIST_TRAVERSE(&frame_queue[callno], f, list) {
		/* Send a copy immediately */
		if (((unsigned char) (f->oseqno - last) < 128) &&
				(f->retries >= 0)) {
			send_packet(f);
		}
	}
}

static void __iax2_poke_peer_s(const void *data)
{
	struct iax2_peer *peer = (struct iax2_peer *)data;
	iax2_poke_peer(peer, 0);
	peer_unref(peer);
}

static int iax2_poke_peer_s(const void *data)
{
	struct iax2_peer *peer = (struct iax2_peer *)data;
	peer->pokeexpire = -1;
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__iax2_poke_peer_s, data))
#endif
		__iax2_poke_peer_s(data);
	return 0;
}

static int send_trunk(struct iax2_trunk_peer *tpeer, struct timeval *now)
{
	int res = 0;
	struct iax_frame *fr;
	struct ast_iax2_meta_hdr *meta;
	struct ast_iax2_meta_trunk_hdr *mth;
	int calls = 0;

	/* Point to frame */
	fr = (struct iax_frame *)tpeer->trunkdata;
	/* Point to meta data */
	meta = (struct ast_iax2_meta_hdr *)fr->afdata;
	mth = (struct ast_iax2_meta_trunk_hdr *)meta->data;
	if (tpeer->trunkdatalen) {
		/* We're actually sending a frame, so fill the meta trunk header and meta header */
		meta->zeros = 0;
		meta->metacmd = IAX_META_TRUNK;
		if (ast_test_flag64(&globalflags, IAX_TRUNKTIMESTAMPS))
			meta->cmddata = IAX_META_TRUNK_MINI;
		else
			meta->cmddata = IAX_META_TRUNK_SUPERMINI;
		mth->ts = htonl(calc_txpeerstamp(tpeer, trunkfreq, now));
		/* And the rest of the ast_iax2 header */
		fr->direction = DIRECTION_OUTGRESS;
		fr->retrans = -1;
		fr->transfer = 0;
		/* Any appropriate call will do */
		fr->data = fr->afdata;
		fr->datalen = tpeer->trunkdatalen + sizeof(struct ast_iax2_meta_hdr) + sizeof(struct ast_iax2_meta_trunk_hdr);
		res = transmit_trunk(fr, &tpeer->addr, tpeer->sockfd);
		calls = tpeer->calls;
#if 0
		ast_debug(1, "Trunking %d call chunks in %d bytes to %s:%d, ts=%d\n", calls, fr->datalen, ast_inet_ntoa(tpeer->addr.sin_addr), ntohs(tpeer->addr.sin_port), ntohl(mth->ts));
#endif
		/* Reset transmit trunk side data */
		tpeer->trunkdatalen = 0;
		tpeer->calls = 0;
	}
	if (res < 0)
		return res;
	return calls;
}

static inline int iax2_trunk_expired(struct iax2_trunk_peer *tpeer, struct timeval *now)
{
	/* Drop when trunk is about 5 seconds idle */
	if (now->tv_sec > tpeer->trunkact.tv_sec + 5)
		return 1;
	return 0;
}

static int timing_read(int *id, int fd, short events, void *cbdata)
{
	int res, processed = 0, totalcalls = 0;
	struct iax2_trunk_peer *tpeer = NULL, *drop = NULL;
	struct timeval now = ast_tvnow();

	if (iaxtrunkdebug) {
		ast_verbose("Beginning trunk processing. Trunk queue ceiling is %d bytes per host\n", trunkmaxsize);
	}

	if (timer) {
		if (ast_timer_ack(timer, 1) < 0) {
			ast_log(LOG_ERROR, "Timer failed acknowledge\n");
			return 0;
		}
	}

	/* For each peer that supports trunking... */
	AST_LIST_LOCK(&tpeers);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&tpeers, tpeer, list) {
		processed++;
		res = 0;
		ast_mutex_lock(&tpeer->lock);
		/* We can drop a single tpeer per pass.  That makes all this logic
		   substantially easier */
		if (!drop && iax2_trunk_expired(tpeer, &now)) {
			/* Take it out of the list, but don't free it yet, because it
			   could be in use */
			AST_LIST_REMOVE_CURRENT(list);
			drop = tpeer;
		} else {
			res = send_trunk(tpeer, &now);
			trunk_timed++;
			if (iaxtrunkdebug) {
				ast_verbose(" - Trunk peer (%s) has %d call chunk%s in transit, %u bytes backloged and has hit a high water mark of %u bytes\n",
							ast_sockaddr_stringify(&tpeer->addr),
							res,
							(res != 1) ? "s" : "",
							tpeer->trunkdatalen,
							tpeer->trunkdataalloc);
			}
		}
		totalcalls += res;
		res = 0;
		ast_mutex_unlock(&tpeer->lock);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&tpeers);

	if (drop) {
		ast_mutex_lock(&drop->lock);
		/*  Once we have this lock, we're sure nobody else is using it or could use it once we release it,
			because by the time they could get tpeerlock, we've already grabbed it */
		ast_debug(1, "Dropping unused iax2 trunk peer '%s'\n", ast_sockaddr_stringify(&drop->addr));
		if (drop->trunkdata) {
			ast_free(drop->trunkdata);
			drop->trunkdata = NULL;
		}
		ast_mutex_unlock(&drop->lock);
		ast_mutex_destroy(&drop->lock);
		ast_free(drop);
	}

	if (iaxtrunkdebug) {
		ast_verbose("Ending trunk processing with %d peers and %d call chunks processed\n", processed, totalcalls);
	}
	iaxtrunkdebug = 0;

	return 1;
}

struct dpreq_data {
	int callno;
	char context[AST_MAX_EXTENSION];
	char callednum[AST_MAX_EXTENSION];
	char *callerid;
};

static void dp_lookup(int callno, const char *context, const char *callednum, const char *callerid, int skiplock)
{
	unsigned short dpstatus = 0;
	struct iax_ie_data ied1;
	int mm;

	memset(&ied1, 0, sizeof(ied1));
	mm = ast_matchmore_extension(NULL, context, callednum, 1, callerid);
	/* Must be started */
	if (ast_exists_extension(NULL, context, callednum, 1, callerid)) {
		dpstatus = IAX_DPSTATUS_EXISTS;
	} else if (ast_canmatch_extension(NULL, context, callednum, 1, callerid)) {
		dpstatus = IAX_DPSTATUS_CANEXIST;
	} else {
		dpstatus = IAX_DPSTATUS_NONEXISTENT;
	}
	if (ast_ignore_pattern(context, callednum))
		dpstatus |= IAX_DPSTATUS_IGNOREPAT;
	if (mm)
		dpstatus |= IAX_DPSTATUS_MATCHMORE;
	if (!skiplock)
		ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		iax_ie_append_str(&ied1, IAX_IE_CALLED_NUMBER, callednum);
		iax_ie_append_short(&ied1, IAX_IE_DPSTATUS, dpstatus);
		iax_ie_append_short(&ied1, IAX_IE_REFRESH, iaxdefaultdpcache);
		send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_DPREP, 0, ied1.buf, ied1.pos, -1);
	}
	if (!skiplock)
		ast_mutex_unlock(&iaxsl[callno]);
}

static void *dp_lookup_thread(void *data)
{
	/* Look up for dpreq */
	struct dpreq_data *dpr = data;
	dp_lookup(dpr->callno, dpr->context, dpr->callednum, dpr->callerid, 0);
	if (dpr->callerid)
		ast_free(dpr->callerid);
	ast_free(dpr);
	return NULL;
}

static void spawn_dp_lookup(int callno, const char *context, const char *callednum, const char *callerid)
{
	pthread_t newthread;
	struct dpreq_data *dpr;

	if (!(dpr = ast_calloc(1, sizeof(*dpr))))
		return;

	dpr->callno = callno;
	ast_copy_string(dpr->context, context, sizeof(dpr->context));
	ast_copy_string(dpr->callednum, callednum, sizeof(dpr->callednum));
	if (callerid)
		dpr->callerid = ast_strdup(callerid);
	if (ast_pthread_create_detached(&newthread, NULL, dp_lookup_thread, dpr)) {
		ast_log(LOG_WARNING, "Unable to start lookup thread!\n");
	}
}

static int check_provisioning(struct ast_sockaddr *addr, int sockfd, char *si, unsigned int ver)
{
	unsigned int ourver;
	char rsi[80];
	snprintf(rsi, sizeof(rsi), "si-%s", si);
	if (iax_provision_version(&ourver, rsi, 1))
		return 0;
	ast_debug(1, "Service identifier '%s', we think '%08x', they think '%08x'\n", si, ourver, ver);
	if (ourver != ver)
		iax2_provision(addr, sockfd, NULL, rsi, 1);
	return 0;
}

static void construct_rr(struct chan_iax2_pvt *pvt, struct iax_ie_data *iep)
{
	jb_info stats;
	jb_getinfo(pvt->jb, &stats);

	memset(iep, 0, sizeof(*iep));

	iax_ie_append_int(iep,IAX_IE_RR_JITTER, stats.jitter);
	if(stats.frames_in == 0) stats.frames_in = 1;
	iax_ie_append_int(iep,IAX_IE_RR_LOSS, ((0xff & (stats.losspct/1000)) << 24 | (stats.frames_lost & 0x00ffffff)));
	iax_ie_append_int(iep,IAX_IE_RR_PKTS, stats.frames_in);
	iax_ie_append_short(iep,IAX_IE_RR_DELAY, stats.current - stats.min);
	iax_ie_append_int(iep,IAX_IE_RR_DROPPED, stats.frames_dropped);
	iax_ie_append_int(iep,IAX_IE_RR_OOO, stats.frames_ooo);
}

static void save_rr(struct iax_frame *fr, struct iax_ies *ies)
{
	iaxs[fr->callno]->remote_rr.jitter = ies->rr_jitter;
	iaxs[fr->callno]->remote_rr.losspct = ies->rr_loss >> 24;
	iaxs[fr->callno]->remote_rr.losscnt = ies->rr_loss & 0xffffff;
	iaxs[fr->callno]->remote_rr.packets = ies->rr_pkts;
	iaxs[fr->callno]->remote_rr.delay = ies->rr_delay;
	iaxs[fr->callno]->remote_rr.dropped = ies->rr_dropped;
	iaxs[fr->callno]->remote_rr.ooo = ies->rr_ooo;
}

static void save_osptoken(struct iax_frame *fr, struct iax_ies *ies)
{
	int i;
	unsigned int length, offset = 0;
	char full_osptoken[IAX_MAX_OSPBUFF_SIZE];

	for (i = 0; i < IAX_MAX_OSPBLOCK_NUM; i++) {
		length = ies->ospblocklength[i];
		if (length != 0) {
			if (length > IAX_MAX_OSPBLOCK_SIZE) {
				/* OSP token block length wrong, clear buffer */
				offset = 0;
				break;
			} else {
				memcpy(full_osptoken + offset, ies->osptokenblock[i], length);
				offset += length;
			}
		} else {
			break;
		}
	}
	*(full_osptoken + offset) = '\0';
	if (strlen(full_osptoken) != offset) {
		/* OSP token length wrong, clear buffer */
		*full_osptoken = '\0';
	}

	ast_string_field_set(iaxs[fr->callno], osptoken, full_osptoken);
}

static void log_jitterstats(unsigned short callno)
{
	int localjitter = -1, localdelay = 0, locallost = -1, locallosspct = -1, localdropped = 0, localooo = -1, localpackets = -1;
	jb_info jbinfo;

	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno] && iaxs[callno]->owner && ast_channel_name(iaxs[callno]->owner)) {
		if(ast_test_flag64(iaxs[callno], IAX_USEJITTERBUF)) {
			jb_getinfo(iaxs[callno]->jb, &jbinfo);
			localjitter = jbinfo.jitter;
			localdelay = jbinfo.current - jbinfo.min;
			locallost = jbinfo.frames_lost;
			locallosspct = jbinfo.losspct/1000;
			localdropped = jbinfo.frames_dropped;
			localooo = jbinfo.frames_ooo;
			localpackets = jbinfo.frames_in;
		}
		ast_debug(3, "JB STATS:%s ping=%u ljitterms=%d ljbdelayms=%d ltotlost=%d lrecentlosspct=%d ldropped=%d looo=%d lrecvd=%d rjitterms=%d rjbdelayms=%d rtotlost=%d rrecentlosspct=%d rdropped=%d rooo=%d rrecvd=%d\n",
			ast_channel_name(iaxs[callno]->owner),
			iaxs[callno]->pingtime,
			localjitter,
			localdelay,
			locallost,
			locallosspct,
			localdropped,
			localooo,
			localpackets,
			iaxs[callno]->remote_rr.jitter,
			iaxs[callno]->remote_rr.delay,
			iaxs[callno]->remote_rr.losscnt,
			iaxs[callno]->remote_rr.losspct/1000,
			iaxs[callno]->remote_rr.dropped,
			iaxs[callno]->remote_rr.ooo,
			iaxs[callno]->remote_rr.packets);
	}
	ast_mutex_unlock(&iaxsl[callno]);
}

static int socket_process(struct iax2_thread *thread);

/*!
 * \brief Handle any deferred full frames for this thread
 */
static void handle_deferred_full_frames(struct iax2_thread *thread)
{
	struct iax2_pkt_buf *pkt_buf;

	ast_mutex_lock(&thread->lock);

	while ((pkt_buf = AST_LIST_REMOVE_HEAD(&thread->full_frames, entry))) {
		ast_mutex_unlock(&thread->lock);

		thread->buf = pkt_buf->buf;
		thread->buf_len = pkt_buf->len;
		thread->buf_size = pkt_buf->len + 1;

		socket_process(thread);

		thread->buf = NULL;
		ast_free(pkt_buf);

		ast_mutex_lock(&thread->lock);
	}

	ast_mutex_unlock(&thread->lock);
}

/*!
 * \brief Queue the last read full frame for processing by a certain thread
 *
 * If there are already any full frames queued, they are sorted
 * by sequence number.
 */
static void defer_full_frame(struct iax2_thread *from_here, struct iax2_thread *to_here)
{
	struct iax2_pkt_buf *pkt_buf, *cur_pkt_buf;
	struct ast_iax2_full_hdr *fh, *cur_fh;

	if (!(pkt_buf = ast_calloc(1, sizeof(*pkt_buf) + from_here->buf_len)))
		return;

	pkt_buf->len = from_here->buf_len;
	memcpy(pkt_buf->buf, from_here->buf, pkt_buf->len);

	fh = (struct ast_iax2_full_hdr *) pkt_buf->buf;
	ast_mutex_lock(&to_here->lock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&to_here->full_frames, cur_pkt_buf, entry) {
		cur_fh = (struct ast_iax2_full_hdr *) cur_pkt_buf->buf;
		if (fh->oseqno < cur_fh->oseqno) {
			AST_LIST_INSERT_BEFORE_CURRENT(pkt_buf, entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END

	if (!cur_pkt_buf)
		AST_LIST_INSERT_TAIL(&to_here->full_frames, pkt_buf, entry);

	to_here->iostate = IAX_IOSTATE_READY;
	ast_cond_signal(&to_here->cond);

	ast_mutex_unlock(&to_here->lock);
}

static int socket_read(int *id, int fd, short events, void *cbdata)
{
	struct iax2_thread *thread;
	time_t t;
	static time_t last_errtime = 0;
	struct ast_iax2_full_hdr *fh;

	if (!(thread = find_idle_thread())) {
		time(&t);
		if (t != last_errtime) {
			last_errtime = t;
			ast_debug(1, "Out of idle IAX2 threads for I/O, pausing!\n");
		}
		usleep(1);
		return 1;
	}

	thread->iofd = fd;
	thread->buf_len = ast_recvfrom(fd, thread->readbuf, sizeof(thread->readbuf), 0, &thread->ioaddr);
	thread->buf_size = sizeof(thread->readbuf);
	thread->buf = thread->readbuf;
	if (thread->buf_len < 0) {
		if (errno != ECONNREFUSED && errno != EAGAIN)
			ast_log(LOG_WARNING, "Error: %s\n", strerror(errno));
		handle_error();
		thread->iostate = IAX_IOSTATE_IDLE;
		signal_condition(&thread->lock, &thread->cond);
		return 1;
	}
	if (test_losspct && ((100.0 * ast_random() / (RAND_MAX + 1.0)) < test_losspct)) { /* simulate random loss condition */
		thread->iostate = IAX_IOSTATE_IDLE;
		signal_condition(&thread->lock, &thread->cond);
		return 1;
	}

	/* Determine if this frame is a full frame; if so, and any thread is currently
	   processing a full frame for the same callno from this peer, then drop this
	   frame (and the peer will retransmit it) */
	fh = (struct ast_iax2_full_hdr *) thread->buf;
	if (ntohs(fh->scallno) & IAX_FLAG_FULL) {
		struct iax2_thread *cur = NULL;
		uint16_t callno = ntohs(fh->scallno) & ~IAX_FLAG_FULL;

		AST_LIST_LOCK(&active_list);
		AST_LIST_TRAVERSE(&active_list, cur, list) {
			if ((cur->ffinfo.callno == callno) &&
			    !ast_sockaddr_cmp_addr(&cur->ffinfo.addr, &thread->ioaddr))
				break;
		}
		if (cur) {
			/* we found another thread processing a full frame for this call,
			   so queue it up for processing later. */
			defer_full_frame(thread, cur);
			AST_LIST_UNLOCK(&active_list);
			thread->iostate = IAX_IOSTATE_IDLE;
			signal_condition(&thread->lock, &thread->cond);
			return 1;
		} else {
			/* this thread is going to process this frame, so mark it */
			thread->ffinfo.callno = callno;
			ast_sockaddr_copy(&thread->ffinfo.addr, &thread->ioaddr);
			thread->ffinfo.type = fh->type;
			thread->ffinfo.csub = fh->csub;
			AST_LIST_INSERT_HEAD(&active_list, thread, list);
		}
		AST_LIST_UNLOCK(&active_list);
	}

	/* Mark as ready and send on its way */
	thread->iostate = IAX_IOSTATE_READY;
#ifdef DEBUG_SCHED_MULTITHREAD
	ast_copy_string(thread->curfunc, "socket_process", sizeof(thread->curfunc));
#endif
	signal_condition(&thread->lock, &thread->cond);

	return 1;
}

static int socket_process_meta(int packet_len, struct ast_iax2_meta_hdr *meta, struct ast_sockaddr *addr, int sockfd,
	struct iax_frame *fr)
{
	unsigned char metatype;
	struct ast_iax2_meta_trunk_mini *mtm;
	struct ast_iax2_meta_trunk_hdr *mth;
	struct ast_iax2_meta_trunk_entry *mte;
	struct iax2_trunk_peer *tpeer;
	unsigned int ts;
	void *ptr;
	struct timeval rxtrunktime;
	struct ast_frame f = { 0, };

	if (packet_len < sizeof(*meta)) {
		ast_log(LOG_WARNING, "Rejecting packet from '%s' that is flagged as a meta frame but is too short\n",
				ast_sockaddr_stringify(addr));
		return 1;
	}

	if (meta->metacmd != IAX_META_TRUNK)
		return 1;

	if (packet_len < (sizeof(*meta) + sizeof(*mth))) {
		ast_log(LOG_WARNING, "midget meta trunk packet received (%d of %d min)\n", packet_len,
			(int) (sizeof(*meta) + sizeof(*mth)));
		return 1;
	}
	mth = (struct ast_iax2_meta_trunk_hdr *)(meta->data);
	ts = ntohl(mth->ts);
	metatype = meta->cmddata;
	packet_len -= (sizeof(*meta) + sizeof(*mth));
	ptr = mth->data;
	tpeer = find_tpeer(addr, sockfd);
	if (!tpeer) {
		ast_log(LOG_WARNING, "Unable to accept trunked packet from '%s': No matching peer\n",
				ast_sockaddr_stringify(addr));
		return 1;
	}
	tpeer->trunkact = ast_tvnow();
	if (!ts || ast_tvzero(tpeer->rxtrunktime))
		tpeer->rxtrunktime = tpeer->trunkact;
	rxtrunktime = tpeer->rxtrunktime;
	ast_mutex_unlock(&tpeer->lock);
	while (packet_len >= sizeof(*mte)) {
		/* Process channels */
		unsigned short callno, trunked_ts, len;

		if (metatype == IAX_META_TRUNK_MINI) {
			mtm = (struct ast_iax2_meta_trunk_mini *) ptr;
			ptr += sizeof(*mtm);
			packet_len -= sizeof(*mtm);
			len = ntohs(mtm->len);
			callno = ntohs(mtm->mini.callno);
			trunked_ts = ntohs(mtm->mini.ts);
		} else if (metatype == IAX_META_TRUNK_SUPERMINI) {
			mte = (struct ast_iax2_meta_trunk_entry *)ptr;
			ptr += sizeof(*mte);
			packet_len -= sizeof(*mte);
			len = ntohs(mte->len);
			callno = ntohs(mte->callno);
			trunked_ts = 0;
		} else {
			ast_log(LOG_WARNING, "Unknown meta trunk cmd from '%s': dropping\n", ast_sockaddr_stringify(addr));
			break;
		}
		/* Stop if we don't have enough data */
		if (len > packet_len)
			break;
		fr->callno = find_callno_locked(callno & ~IAX_FLAG_FULL, 0, addr, NEW_PREVENT, sockfd, 0);
		if (!fr->callno)
			continue;

		/* If it's a valid call, deliver the contents.  If not, we
		   drop it, since we don't have a scallno to use for an INVAL */
		/* Process as a mini frame */
		memset(&f, 0, sizeof(f));
		f.frametype = AST_FRAME_VOICE;
		if (!iaxs[fr->callno]) {
			/* drop it */
		} else if (iaxs[fr->callno]->voiceformat == 0) {
			ast_log(LOG_WARNING, "Received trunked frame before first full voice frame\n");
			iax2_vnak(fr->callno);
		} else {
			f.subclass.format = ast_format_compatibility_bitfield2format(iaxs[fr->callno]->voiceformat);
			f.datalen = len;
			if (f.datalen >= 0) {
				if (f.datalen)
					f.data.ptr = ptr;
				else
					f.data.ptr = NULL;
				if (trunked_ts)
					fr->ts = (iaxs[fr->callno]->last & 0xFFFF0000L) | (trunked_ts & 0xffff);
				else
					fr->ts = fix_peerts(&rxtrunktime, fr->callno, ts);
				/* Don't pass any packets until we're started */
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED)) {
					struct iax_frame *duped_fr;

					/* Common things */
					f.src = "IAX2";
					f.mallocd = 0;
					f.offset = 0;
					if (f.datalen && (f.frametype == AST_FRAME_VOICE))
						f.samples = ast_codec_samples_count(&f);
					else
						f.samples = 0;
					fr->outoforder = 0;
					iax_frame_wrap(fr, &f);
					duped_fr = iaxfrdup2(fr);
					if (duped_fr)
						schedule_delivery(duped_fr, 1, 1, &fr->ts);
					if (iaxs[fr->callno] && iaxs[fr->callno]->last < fr->ts)
						iaxs[fr->callno]->last = fr->ts;
				}
			} else {
				ast_log(LOG_WARNING, "Datalen < 0?\n");
			}
		}
		ast_mutex_unlock(&iaxsl[fr->callno]);
		ptr += len;
		packet_len -= len;
	}

	return 1;
}

static int acf_iaxvar_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *variablestore;
	AST_LIST_HEAD(, ast_var_t) *varlist;
	struct ast_var_t *var;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	variablestore = ast_channel_datastore_find(chan, &iax2_variable_datastore_info, NULL);
	if (!variablestore) {
		*buf = '\0';
		return 0;
	}
	varlist = variablestore->data;

	AST_LIST_LOCK(varlist);
	AST_LIST_TRAVERSE(varlist, var, entries) {
		if (strcmp(var->name, data) == 0) {
			ast_copy_string(buf, var->value, len);
			break;
		}
	}
	AST_LIST_UNLOCK(varlist);
	return 0;
}

static int acf_iaxvar_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *variablestore;
	AST_LIST_HEAD(, ast_var_t) *varlist;
	struct ast_var_t *var;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	variablestore = ast_channel_datastore_find(chan, &iax2_variable_datastore_info, NULL);
	if (!variablestore) {
		variablestore = ast_datastore_alloc(&iax2_variable_datastore_info, NULL);
		if (!variablestore) {
			ast_log(LOG_ERROR, "Memory allocation error\n");
			return -1;
		}
		varlist = ast_calloc(1, sizeof(*varlist));
		if (!varlist) {
			ast_datastore_free(variablestore);
			ast_log(LOG_ERROR, "Unable to assign new variable '%s'\n", data);
			return -1;
		}

		AST_LIST_HEAD_INIT(varlist);
		variablestore->data = varlist;
		variablestore->inheritance = DATASTORE_INHERIT_FOREVER;
		ast_channel_datastore_add(chan, variablestore);
	} else
		varlist = variablestore->data;

	AST_LIST_LOCK(varlist);
	AST_LIST_TRAVERSE_SAFE_BEGIN(varlist, var, entries) {
		if (strcmp(var->name, data) == 0) {
			AST_LIST_REMOVE_CURRENT(entries);
			ast_var_delete(var);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	var = ast_var_assign(data, value);
	if (var)
		AST_LIST_INSERT_TAIL(varlist, var, entries);
	else
		ast_log(LOG_ERROR, "Unable to assign new variable '%s'\n", data);
	AST_LIST_UNLOCK(varlist);
	return 0;
}

static struct ast_custom_function iaxvar_function = {
	.name = "IAXVAR",
	.read = acf_iaxvar_read,
	.write = acf_iaxvar_write,
};

static void set_hangup_source_and_cause(int callno, unsigned char causecode)
{
	iax2_lock_owner(callno);
	if (iaxs[callno] && iaxs[callno]->owner) {
		struct ast_channel *owner;
		const char *name;

		owner = iaxs[callno]->owner;
		if (causecode) {
			ast_channel_hangupcause_set(owner, causecode);
		}
		name = ast_strdupa(ast_channel_name(owner));
		ast_channel_ref(owner);
		ast_channel_unlock(owner);
		ast_mutex_unlock(&iaxsl[callno]);
		ast_set_hangupsource(owner, name, 0);
		ast_channel_unref(owner);
		ast_mutex_lock(&iaxsl[callno]);
	}
}

static int socket_process_helper(struct iax2_thread *thread)
{
	struct ast_sockaddr addr;
	int res;
	int updatehistory=1;
	int new = NEW_PREVENT;
	int dcallno = 0;
	char decrypted = 0;
	struct ast_iax2_full_hdr *fh = (struct ast_iax2_full_hdr *)thread->buf;
	struct ast_iax2_mini_hdr *mh = (struct ast_iax2_mini_hdr *)thread->buf;
	struct ast_iax2_meta_hdr *meta = (struct ast_iax2_meta_hdr *)thread->buf;
	struct ast_iax2_video_hdr *vh = (struct ast_iax2_video_hdr *)thread->buf;
	struct iax_frame *fr;
	struct iax_frame *cur;
	struct ast_frame f = { 0, };
	struct ast_channel *c = NULL;
	struct iax2_dpcache *dp;
	struct iax2_peer *peer;
	struct iax_ies ies;
	struct iax_ie_data ied0, ied1;
	iax2_format format;
	int fd;
	int exists;
	int minivid = 0;
	char empty[32]="";		/* Safety measure */
	struct iax_frame *duped_fr;
	char host_pref_buf[128];
	char caller_pref_buf[128];
	struct iax2_codec_pref pref;
	char *using_prefs = "mine";

	/* allocate an iax_frame with 4096 bytes of data buffer */
	fr = ast_alloca(sizeof(*fr) + 4096);
	memset(fr, 0, sizeof(*fr));
	fr->afdatalen = 4096; /* From ast_alloca() above */

	/* Copy frequently used parameters to the stack */
	res = thread->buf_len;
	fd = thread->iofd;
	ast_sockaddr_copy(&addr, &thread->ioaddr);

	if (res < sizeof(*mh)) {
		ast_log(LOG_WARNING, "midget packet received (%d of %d min)\n", res, (int) sizeof(*mh));
		return 1;
	}
	if ((vh->zeros == 0) && (ntohs(vh->callno) & 0x8000)) {
		if (res < sizeof(*vh)) {
			ast_log(LOG_WARNING, "Rejecting packet from '%s' that is flagged as a video frame but is too short\n",
					ast_sockaddr_stringify(&addr));
			return 1;
		}

		/* This is a video frame, get call number */
		fr->callno = find_callno(ntohs(vh->callno) & ~0x8000, dcallno, &addr, new, fd, 0);
		minivid = 1;
	} else if ((meta->zeros == 0) && !(ntohs(meta->metacmd) & 0x8000))
		return socket_process_meta(res, meta, &addr, fd, fr);

#ifdef DEBUG_SUPPORT
	if (res >= sizeof(*fh))
		iax_outputframe(NULL, fh, 1, &addr, res - sizeof(*fh));
#endif
	if (ntohs(mh->callno) & IAX_FLAG_FULL) {
		if (res < sizeof(*fh)) {
			ast_log(LOG_WARNING, "Rejecting packet from '%s' that is flagged as a full frame but is too short\n",
				ast_sockaddr_stringify(&addr));
			return 1;
		}

		/* Get the destination call number */
		dcallno = ntohs(fh->dcallno) & ~IAX_FLAG_RETRANS;


		/* check to make sure this full frame isn't encrypted before we attempt
		 * to look inside of it. If it is encrypted, decrypt it first. Its ok if the
		 * callno is not found here, that just means one hasn't been allocated for
		 * this connection yet. */
		if ((dcallno != 1) && (fr->callno = find_callno(ntohs(mh->callno) & ~IAX_FLAG_FULL, dcallno, &addr, NEW_PREVENT, fd, 1))) {
			ast_mutex_lock(&iaxsl[fr->callno]);
			if (iaxs[fr->callno] && ast_test_flag64(iaxs[fr->callno], IAX_ENCRYPTED)) {
				if (decrypt_frame(fr->callno, fh, &f, &res)) {
					ast_log(LOG_NOTICE, "Packet Decrypt Failed!\n");
					ast_mutex_unlock(&iaxsl[fr->callno]);
					return 1;
				}
				decrypted = 1;
			}
			ast_mutex_unlock(&iaxsl[fr->callno]);
		}

		/* Retrieve the type and subclass */
		f.frametype = fh->type;
		if (f.frametype == AST_FRAME_VIDEO) {
			f.subclass.format = ast_format_compatibility_bitfield2format(uncompress_subclass(fh->csub & ~0x40));
			if ((fh->csub >> 6) & 0x1) {
				f.subclass.frame_ending = 1;
			}
		} else if (f.frametype == AST_FRAME_VOICE) {
			f.subclass.format = ast_format_compatibility_bitfield2format(uncompress_subclass(fh->csub));
		} else {
			f.subclass.integer = uncompress_subclass(fh->csub);
		}

		/* Deal with POKE/PONG without allocating a callno */
		if (f.frametype == AST_FRAME_IAX && f.subclass.integer == IAX_COMMAND_POKE) {
			/* Reply back with a PONG, but don't care about the result. */
			send_apathetic_reply(1, ntohs(fh->scallno), &addr, IAX_COMMAND_PONG, ntohl(fh->ts), fh->iseqno + 1, fd, NULL);
			return 1;
		} else if (f.frametype == AST_FRAME_IAX && f.subclass.integer == IAX_COMMAND_ACK && dcallno == 1) {
			/* Ignore */
			return 1;
		}

		f.datalen = res - sizeof(*fh);
		if (f.datalen) {
			if (f.frametype == AST_FRAME_IAX) {
				if (iax_parse_ies(&ies, thread->buf + sizeof(struct ast_iax2_full_hdr), f.datalen)) {
					ast_log(LOG_WARNING, "Undecodable frame received from '%s'\n", ast_sockaddr_stringify(&addr));
					ast_variables_destroy(ies.vars);
					return 1;
				}
				f.data.ptr = NULL;
				f.datalen = 0;
			} else {
				f.data.ptr = thread->buf + sizeof(struct ast_iax2_full_hdr);
				memset(&ies, 0, sizeof(ies));
			}
		} else {
			if (f.frametype == AST_FRAME_IAX)
				f.data.ptr = NULL;
			else
				f.data.ptr = empty;
			memset(&ies, 0, sizeof(ies));
		}

		if (!dcallno && iax2_allow_new(f.frametype, f.subclass.integer, 1)) {
			/* only set NEW_ALLOW if calltoken checks out */
			if (handle_call_token(fh, &ies, &addr, fd)) {
				ast_variables_destroy(ies.vars);
				return 1;
			}

			if (ies.calltoken && ies.calltokendata) {
				/* if we've gotten this far, and the calltoken ie data exists,
				 * then calltoken validation _MUST_ have taken place.  If calltoken
				 * data is provided, it is always validated reguardless of any
				 * calltokenoptional or requirecalltoken options */
				new = NEW_ALLOW_CALLTOKEN_VALIDATED;
			} else {
				new = NEW_ALLOW;
			}
		}
	} else {
		/* Don't know anything about it yet */
		f.frametype = AST_FRAME_NULL;
		f.subclass.integer = 0;
		memset(&ies, 0, sizeof(ies));
	}

	if (!fr->callno) {
		int check_dcallno = 0;

		/*
		 * We enforce accurate destination call numbers for ACKs.  This forces the other
		 * end to know the destination call number before call setup can complete.
		 *
		 * Discussed in the following thread:
		 *    http://lists.digium.com/pipermail/asterisk-dev/2008-May/033217.html
		 */

		if ((ntohs(mh->callno) & IAX_FLAG_FULL) && ((f.frametype == AST_FRAME_IAX) && (f.subclass.integer == IAX_COMMAND_ACK))) {
			check_dcallno = 1;
		}

		if (!(fr->callno = find_callno(ntohs(mh->callno) & ~IAX_FLAG_FULL, dcallno, &addr, new, fd, check_dcallno))) {
			if (f.frametype == AST_FRAME_IAX && f.subclass.integer == IAX_COMMAND_NEW) {
				send_apathetic_reply(1, ntohs(fh->scallno), &addr, IAX_COMMAND_REJECT, ntohl(fh->ts), fh->iseqno + 1, fd, NULL);
			} else if (f.frametype == AST_FRAME_IAX && (f.subclass.integer == IAX_COMMAND_REGREQ || f.subclass.integer == IAX_COMMAND_REGREL)) {
				send_apathetic_reply(1, ntohs(fh->scallno), &addr, IAX_COMMAND_REGREJ, ntohl(fh->ts), fh->iseqno + 1, fd, NULL);
			}
			ast_variables_destroy(ies.vars);
			return 1;
		}
	}

	if (fr->callno > 0) {
		ast_callid mount_callid;
		ast_mutex_lock(&iaxsl[fr->callno]);
		if (iaxs[fr->callno] && ((mount_callid = iax_pvt_callid_get(fr->callno)))) {
			/* Bind to thread */
			ast_callid_threadassoc_add(mount_callid);
		}
	}

	if (!fr->callno || !iaxs[fr->callno]) {
		/* A call arrived for a nonexistent destination.  Unless it's an "inval"
		   frame, reply with an inval */
		if (ntohs(mh->callno) & IAX_FLAG_FULL) {
			/* We can only raw hangup control frames */
			if (((f.subclass.integer != IAX_COMMAND_INVAL) &&
				 (f.subclass.integer != IAX_COMMAND_TXCNT) &&
				 (f.subclass.integer != IAX_COMMAND_TXACC) &&
				 (f.subclass.integer != IAX_COMMAND_FWDOWNL))||
			    (f.frametype != AST_FRAME_IAX))
				raw_hangup(&addr, ntohs(fh->dcallno) & ~IAX_FLAG_RETRANS, ntohs(mh->callno) & ~IAX_FLAG_FULL,
				fd);
		}
		if (fr->callno > 0){
			ast_mutex_unlock(&iaxsl[fr->callno]);
		}
		ast_variables_destroy(ies.vars);
		return 1;
	}
	if (ast_test_flag64(iaxs[fr->callno], IAX_ENCRYPTED) && !decrypted) {
		if (decrypt_frame(fr->callno, fh, &f, &res)) {
			ast_log(LOG_NOTICE, "Packet Decrypt Failed!\n");
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
		decrypted = 1;
	}

#ifdef DEBUG_SUPPORT
	if (decrypted) {
		iax_outputframe(NULL, fh, 3, &addr, res - sizeof(*fh));
	}
#endif

	if (iaxs[fr->callno]->owner && fh->type == AST_FRAME_IAX &&
			(fh->csub == IAX_COMMAND_HANGUP
			|| fh->csub == IAX_COMMAND_REJECT
			|| fh->csub == IAX_COMMAND_REGREJ
			|| fh->csub == IAX_COMMAND_TXREJ)) {
		struct ast_control_pvt_cause_code *cause_code;
		int data_size = sizeof(*cause_code);
		char subclass[40] = "";

		/* get subclass text */
		iax_frame_subclass2str(fh->csub, subclass, sizeof(subclass));

		/* add length of "IAX2 " */
		data_size += 5;
		/* for IAX hangup frames, add length of () and number */
		data_size += 3;
		if (ies.causecode > 9) {
			data_size++;
		}
		if (ies.causecode > 99) {
			data_size++;
		}
		/* add length of subclass */
		data_size += strlen(subclass);

		cause_code = ast_alloca(data_size);
		memset(cause_code, 0, data_size);
		ast_copy_string(cause_code->chan_name, ast_channel_name(iaxs[fr->callno]->owner), AST_CHANNEL_NAME);

		cause_code->ast_cause = ies.causecode;
		snprintf(cause_code->code, data_size - sizeof(*cause_code) + 1, "IAX2 %s(%d)", subclass, ies.causecode);

		iax2_lock_owner(fr->callno);
		if (iaxs[fr->callno] && iaxs[fr->callno]->owner) {
			ast_queue_control_data(iaxs[fr->callno]->owner, AST_CONTROL_PVT_CAUSE_CODE, cause_code, data_size);
			ast_channel_hangupcause_hash_set(iaxs[fr->callno]->owner, cause_code, data_size);
			ast_channel_unlock(iaxs[fr->callno]->owner);
		}
		if (!iaxs[fr->callno]) {
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
	}

	/* count this frame */
	iaxs[fr->callno]->frames_received++;

	if (!ast_sockaddr_cmp(&addr, &iaxs[fr->callno]->addr) && !minivid &&
		f.subclass.integer != IAX_COMMAND_TXCNT &&		/* for attended transfer */
		f.subclass.integer != IAX_COMMAND_TXACC) {		/* for attended transfer */
		unsigned short new_peercallno;

		new_peercallno = (unsigned short) (ntohs(mh->callno) & ~IAX_FLAG_FULL);
		if (new_peercallno && new_peercallno != iaxs[fr->callno]->peercallno) {
			if (iaxs[fr->callno]->peercallno) {
				remove_by_peercallno(iaxs[fr->callno]);
			}
			iaxs[fr->callno]->peercallno = new_peercallno;
			store_by_peercallno(iaxs[fr->callno]);
		}
	}
	if (ntohs(mh->callno) & IAX_FLAG_FULL) {
		if (iaxdebug)
			ast_debug(1, "Received packet %d, (%u, %d)\n", fh->oseqno, f.frametype, f.subclass.integer);
		/* Check if it's out of order (and not an ACK or INVAL) */
		fr->oseqno = fh->oseqno;
		fr->iseqno = fh->iseqno;
		fr->ts = ntohl(fh->ts);
#ifdef IAXTESTS
		if (test_resync) {
			ast_debug(1, "Simulating frame ts resync, was %u now %u\n", fr->ts, fr->ts + test_resync);
			fr->ts += test_resync;
		}
#endif /* IAXTESTS */
#if 0
		if ( (ntohs(fh->dcallno) & IAX_FLAG_RETRANS) ||
		     ( (f.frametype != AST_FRAME_VOICE) && ! (f.frametype == AST_FRAME_IAX &&
								(f.subclass == IAX_COMMAND_NEW ||
								 f.subclass == IAX_COMMAND_AUTHREQ ||
								 f.subclass == IAX_COMMAND_ACCEPT ||
								 f.subclass == IAX_COMMAND_REJECT))      ) )
#endif
		if ((ntohs(fh->dcallno) & IAX_FLAG_RETRANS) || (f.frametype != AST_FRAME_VOICE))
			updatehistory = 0;
		if ((iaxs[fr->callno]->iseqno != fr->oseqno) &&
			(iaxs[fr->callno]->iseqno ||
				((f.subclass.integer != IAX_COMMAND_TXCNT) &&
				(f.subclass.integer != IAX_COMMAND_TXREADY) &&		/* for attended transfer */
				(f.subclass.integer != IAX_COMMAND_TXREL) &&		/* for attended transfer */
				(f.subclass.integer != IAX_COMMAND_UNQUELCH ) &&	/* for attended transfer */
				(f.subclass.integer != IAX_COMMAND_TXACC)) ||
				(f.frametype != AST_FRAME_IAX))) {
			if (
			 ((f.subclass.integer != IAX_COMMAND_ACK) &&
			  (f.subclass.integer != IAX_COMMAND_INVAL) &&
			  (f.subclass.integer != IAX_COMMAND_TXCNT) &&
			  (f.subclass.integer != IAX_COMMAND_TXREADY) &&		/* for attended transfer */
			  (f.subclass.integer != IAX_COMMAND_TXREL) &&		/* for attended transfer */
			  (f.subclass.integer != IAX_COMMAND_UNQUELCH ) &&	/* for attended transfer */
			  (f.subclass.integer != IAX_COMMAND_TXACC) &&
			  (f.subclass.integer != IAX_COMMAND_VNAK)) ||
			  (f.frametype != AST_FRAME_IAX)) {
				/* If it's not an ACK packet, it's out of order. */
				ast_debug(1, "Packet arrived out of order (expecting %d, got %d) (frametype = %u, subclass = %d)\n",
					iaxs[fr->callno]->iseqno, fr->oseqno, f.frametype, f.subclass.integer);
				/* Check to see if we need to request retransmission,
				 * and take sequence number wraparound into account */
				if ((unsigned char) (iaxs[fr->callno]->iseqno - fr->oseqno) < 128) {
					/* If we've already seen it, ack it XXX There's a border condition here XXX */
					if ((f.frametype != AST_FRAME_IAX) ||
							((f.subclass.integer != IAX_COMMAND_ACK) && (f.subclass.integer != IAX_COMMAND_INVAL))) {
						ast_debug(1, "Acking anyway\n");
						/* XXX Maybe we should handle its ack to us, but then again, it's probably outdated anyway, and if
						   we have anything to send, we'll retransmit and get an ACK back anyway XXX */
						send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
					}
				} else {
					/* Send a VNAK requesting retransmission */
					iax2_vnak(fr->callno);
				}
				ast_variables_destroy(ies.vars);
				ast_mutex_unlock(&iaxsl[fr->callno]);
				return 1;
			}
		} else {
			/* Increment unless it's an ACK or VNAK */
			if (((f.subclass.integer != IAX_COMMAND_ACK) &&
			    (f.subclass.integer != IAX_COMMAND_INVAL) &&
			    (f.subclass.integer != IAX_COMMAND_TXCNT) &&
			    (f.subclass.integer != IAX_COMMAND_TXACC) &&
				(f.subclass.integer != IAX_COMMAND_VNAK)) ||
			    (f.frametype != AST_FRAME_IAX))
				iaxs[fr->callno]->iseqno++;
		}
		/* Ensure text frames are NULL-terminated */
		if (f.frametype == AST_FRAME_TEXT && thread->buf[res - 1] != '\0') {
			if (res < thread->buf_size)
				thread->buf[res++] = '\0';
			else /* Trims one character from the text message, but that's better than overwriting the end of the buffer. */
				thread->buf[res - 1] = '\0';
		}

		/* Handle implicit ACKing unless this is an INVAL, and only if this is
		   from the real peer, not the transfer peer */
		if (!ast_sockaddr_cmp(&addr, &iaxs[fr->callno]->addr) &&
		    ((f.subclass.integer != IAX_COMMAND_INVAL) ||
		     (f.frametype != AST_FRAME_IAX))) {
			unsigned char x;
			int call_to_destroy;
			/* First we have to qualify that the ACKed value is within our window */
			if (iaxs[fr->callno]->rseqno >= iaxs[fr->callno]->oseqno || (fr->iseqno >= iaxs[fr->callno]->rseqno && fr->iseqno < iaxs[fr->callno]->oseqno))
				x = fr->iseqno;
			else
				x = iaxs[fr->callno]->oseqno;
			if ((x != iaxs[fr->callno]->oseqno) || (iaxs[fr->callno]->oseqno == fr->iseqno)) {
				/* The acknowledgement is within our window.  Time to acknowledge everything
				   that it says to */
				for (x=iaxs[fr->callno]->rseqno; x != fr->iseqno; x++) {
					/* Ack the packet with the given timestamp */
					if (iaxdebug)
						ast_debug(1, "Cancelling transmission of packet %d\n", x);
					call_to_destroy = 0;
					AST_LIST_TRAVERSE(&frame_queue[fr->callno], cur, list) {
						/* If it's our call, and our timestamp, mark -1 retries */
						if (x == cur->oseqno) {
							cur->retries = -1;
							/* Destroy call if this is the end */
							if (cur->final)
								call_to_destroy = fr->callno;
						}
					}
					if (call_to_destroy) {
						if (iaxdebug)
							ast_debug(1, "Really destroying %d, having been acked on final message\n", call_to_destroy);
						ast_mutex_lock(&iaxsl[call_to_destroy]);
						iax2_destroy(call_to_destroy);
						ast_mutex_unlock(&iaxsl[call_to_destroy]);
					}
				}
				/* Note how much we've received acknowledgement for */
				if (iaxs[fr->callno])
					iaxs[fr->callno]->rseqno = fr->iseqno;
				else {
					/* Stop processing now */
					ast_variables_destroy(ies.vars);
					ast_mutex_unlock(&iaxsl[fr->callno]);
					return 1;
				}
			} else {
				ast_debug(1, "Received iseqno %d not within window %d->%d\n", fr->iseqno, iaxs[fr->callno]->rseqno, iaxs[fr->callno]->oseqno);
			}
		}
		if (ast_sockaddr_cmp(&addr, &iaxs[fr->callno]->addr) &&
			((f.frametype != AST_FRAME_IAX) ||
			 ((f.subclass.integer != IAX_COMMAND_TXACC) &&
			  (f.subclass.integer != IAX_COMMAND_TXCNT)))) {
			/* Only messages we accept from a transfer host are TXACC and TXCNT */
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}

		/* when we receive the first full frame for a new incoming channel,
		   it is safe to start the PBX on the channel because we have now
		   completed a 3-way handshake with the peer */
		if ((f.frametype == AST_FRAME_VOICE) ||
		    (f.frametype == AST_FRAME_VIDEO) ||
		    (f.frametype == AST_FRAME_IAX)) {
			if (ast_test_flag64(iaxs[fr->callno], IAX_DELAYPBXSTART)) {
				ast_clear_flag64(iaxs[fr->callno], IAX_DELAYPBXSTART);
				if (!ast_iax2_new(fr->callno, AST_STATE_RING,
					iaxs[fr->callno]->chosenformat, &iaxs[fr->callno]->rprefs, NULL, NULL,
					ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_AUTHENTICATED))) {
					ast_variables_destroy(ies.vars);
					ast_mutex_unlock(&iaxsl[fr->callno]);
					return 1;
				}
			}

			if (ies.vars) {
				struct ast_datastore *variablestore = NULL;
				struct ast_variable *var, *prev = NULL;
				AST_LIST_HEAD(, ast_var_t) *varlist;

				iax2_lock_owner(fr->callno);
				if (!iaxs[fr->callno]) {
					ast_variables_destroy(ies.vars);
					ast_mutex_unlock(&iaxsl[fr->callno]);
					return 1;
				}
				if ((c = iaxs[fr->callno]->owner)) {
					varlist = ast_calloc(1, sizeof(*varlist));
					variablestore = ast_datastore_alloc(&iax2_variable_datastore_info, NULL);

					if (variablestore && varlist) {
						variablestore->data = varlist;
						variablestore->inheritance = DATASTORE_INHERIT_FOREVER;
						AST_LIST_HEAD_INIT(varlist);
						ast_debug(1, "I can haz IAX vars?\n");
						for (var = ies.vars; var; var = var->next) {
							struct ast_var_t *newvar = ast_var_assign(var->name, var->value);
							if (prev) {
								ast_free(prev);
							}
							prev = var;
							if (!newvar) {
								/* Don't abort list traversal, as this would leave ies.vars in an inconsistent state. */
								ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
							} else {
								AST_LIST_INSERT_TAIL(varlist, newvar, entries);
							}
						}
						if (prev) {
							ast_free(prev);
						}
						ies.vars = NULL;
						ast_channel_datastore_add(c, variablestore);
					} else {
						ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
						if (variablestore) {
							ast_datastore_free(variablestore);
						}
						if (varlist) {
							ast_free(varlist);
						}
					}
					ast_channel_unlock(c);
				} else {
					/* No channel yet, so transfer the variables directly over to the pvt,
					 * for later inheritance. */
					ast_debug(1, "No channel, so populating IAXVARs to the pvt, as an intermediate step.\n");
					for (var = ies.vars; var && var->next; var = var->next);
					if (var) {
						var->next = iaxs[fr->callno]->iaxvars;
						iaxs[fr->callno]->iaxvars = ies.vars;
						ies.vars = NULL;
					}
				}
			}

			if (ies.vars) {
				ast_debug(1, "I have IAX variables, but they were not processed\n");
			}
		}

		/* once we receive our first IAX Full Frame that is not CallToken related, send all
		 * queued signaling frames that were being held. */
		if ((f.frametype == AST_FRAME_IAX) && (f.subclass.integer != IAX_COMMAND_CALLTOKEN) && iaxs[fr->callno]->hold_signaling) {
			send_signaling(iaxs[fr->callno]);
		}

		if (f.frametype == AST_FRAME_VOICE) {
			if (ast_format_compatibility_format2bitfield(f.subclass.format) != iaxs[fr->callno]->voiceformat) {
					iaxs[fr->callno]->voiceformat = ast_format_compatibility_format2bitfield(f.subclass.format);
					ast_debug(1, "Ooh, voice format changed to '%s'\n", ast_format_get_name(f.subclass.format));
					if (iaxs[fr->callno]->owner) {
						iax2_lock_owner(fr->callno);
						if (iaxs[fr->callno]) {
							if (iaxs[fr->callno]->owner) {
								struct ast_format_cap *native = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
								if (native) {
									ast_format_cap_append(native, f.subclass.format, 0);
									ast_channel_nativeformats_set(iaxs[fr->callno]->owner, native);
									if (ast_channel_readformat(iaxs[fr->callno]->owner)) {
										ast_set_read_format(iaxs[fr->callno]->owner, ast_channel_readformat(iaxs[fr->callno]->owner));
									}
									ao2_ref(native, -1);
								}
								ast_channel_unlock(iaxs[fr->callno]->owner);
							}
						} else {
							ast_debug(1, "Neat, somebody took away the channel at a magical time but i found it!\n");
							/* Free remote variables (if any) */
							if (ies.vars) {
								ast_variables_destroy(ies.vars);
								ast_debug(1, "I can haz iaxvars, but they is no good.  :-(\n");
								ies.vars = NULL;
							}
							ast_mutex_unlock(&iaxsl[fr->callno]);
							return 1;
						}
					}
			}
		}
		if (f.frametype == AST_FRAME_VIDEO) {
			if (ast_format_compatibility_format2bitfield(f.subclass.format) != iaxs[fr->callno]->videoformat) {
				ast_debug(1, "Ooh, video format changed to %s\n", ast_format_get_name(f.subclass.format));
				iaxs[fr->callno]->videoformat = ast_format_compatibility_format2bitfield(f.subclass.format);
			}
		}
		if (f.frametype == AST_FRAME_IAX) {
			AST_SCHED_DEL(sched, iaxs[fr->callno]->initid);
			/* Handle the IAX pseudo frame itself */
			if (iaxdebug)
				ast_debug(1, "IAX subclass %d received\n", f.subclass.integer);

                        /* Update last ts unless the frame's timestamp originated with us. */
			if (iaxs[fr->callno]->last < fr->ts &&
                            f.subclass.integer != IAX_COMMAND_ACK &&
                            f.subclass.integer != IAX_COMMAND_PONG &&
                            f.subclass.integer != IAX_COMMAND_LAGRP) {
				iaxs[fr->callno]->last = fr->ts;
				if (iaxdebug)
					ast_debug(1, "For call=%d, set last=%u\n", fr->callno, fr->ts);
			}
			iaxs[fr->callno]->last_iax_message = f.subclass.integer;
			if (!iaxs[fr->callno]->first_iax_message) {
				iaxs[fr->callno]->first_iax_message = f.subclass.integer;
			}
			switch(f.subclass.integer) {
			case IAX_COMMAND_ACK:
				/* Do nothing */
				break;
			case IAX_COMMAND_QUELCH:
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED)) {
					ast_set_flag64(iaxs[fr->callno], IAX_QUELCH);
					if (ies.musiconhold) {
						const char *moh_suggest;

						iax2_lock_owner(fr->callno);
						if (!iaxs[fr->callno] || !iaxs[fr->callno]->owner) {
							break;
						}

						/*
						 * We already hold the owner lock so we do not
						 * need to check iaxs[fr->callno] after it returns.
						 */
						moh_suggest = iaxs[fr->callno]->mohsuggest;
						iax2_queue_hold(fr->callno, moh_suggest);
						ast_channel_unlock(iaxs[fr->callno]->owner);
					}
				}
				break;
			case IAX_COMMAND_UNQUELCH:
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED)) {
					iax2_lock_owner(fr->callno);
					if (!iaxs[fr->callno]) {
						break;
					}

					ast_clear_flag64(iaxs[fr->callno], IAX_QUELCH);
					if (!iaxs[fr->callno]->owner) {
						break;
					}

					/*
					 * We already hold the owner lock so we do not
					 * need to check iaxs[fr->callno] after it returns.
					 */
					iax2_queue_unhold(fr->callno);
					ast_channel_unlock(iaxs[fr->callno]->owner);
				}
				break;
			case IAX_COMMAND_TXACC:
				if (iaxs[fr->callno]->transferring == TRANSFER_BEGIN) {
					/* Ack the packet with the given timestamp */
					AST_LIST_TRAVERSE(&frame_queue[fr->callno], cur, list) {
						/* Cancel any outstanding txcnt's */
						if (cur->transfer) {
							cur->retries = -1;
						}
					}
					memset(&ied1, 0, sizeof(ied1));
					iax_ie_append_short(&ied1, IAX_IE_CALLNO, iaxs[fr->callno]->callno);
					send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_TXREADY, 0, ied1.buf, ied1.pos, -1);
					iaxs[fr->callno]->transferring = TRANSFER_READY;
				}
				break;
			case IAX_COMMAND_NEW:
				/* Ignore if it's already up */
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED | IAX_STATE_TBD))
					break;
				if (ies.provverpres && ies.serviceident && !(ast_sockaddr_isnull(&addr))) {
					ast_mutex_unlock(&iaxsl[fr->callno]);
					check_provisioning(&addr, fd, ies.serviceident, ies.provver);
					ast_mutex_lock(&iaxsl[fr->callno]);
					if (!iaxs[fr->callno]) {
						break;
					}
				}
				/* If we're in trunk mode, do it now, and update the trunk number in our frame before continuing */
				if (ast_test_flag64(iaxs[fr->callno], IAX_TRUNK)) {
					int new_callno;
					if ((new_callno = make_trunk(fr->callno, 1)) != -1)
						fr->callno = new_callno;
				}
				/* For security, always ack immediately */
				if (delayreject)
					send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				if (check_access(fr->callno, &addr, &ies)) {
					/* They're not allowed on */
					auth_fail(fr->callno, IAX_COMMAND_REJECT);
					if (authdebug) {
						ast_log(LOG_NOTICE, "Rejected connect attempt from %s, who was trying to reach '%s@%s'\n",
								ast_sockaddr_stringify(&addr), iaxs[fr->callno]->exten, iaxs[fr->callno]->context);
					}
					break;
				}
				if (ast_strlen_zero(iaxs[fr->callno]->secret) && ast_test_flag64(iaxs[fr->callno], IAX_FORCE_ENCRYPT)) {
					auth_fail(fr->callno, IAX_COMMAND_REJECT);
					ast_log(LOG_WARNING, "Rejected connect attempt.  No secret present while force encrypt enabled.\n");
					break;
				}
				if (strcasecmp(iaxs[fr->callno]->exten, "TBD")) {
					const char *context, *exten, *cid_num;

					context = ast_strdupa(iaxs[fr->callno]->context);
					exten = ast_strdupa(iaxs[fr->callno]->exten);
					cid_num = ast_strdupa(iaxs[fr->callno]->cid_num);

					/* This might re-enter the IAX code and need the lock */
					ast_mutex_unlock(&iaxsl[fr->callno]);
					exists = ast_exists_extension(NULL, context, exten, 1, cid_num);
					ast_mutex_lock(&iaxsl[fr->callno]);

					if (!iaxs[fr->callno]) {
						break;
					}
				} else
					exists = 0;
				/* Get OSP token if it does exist */
				save_osptoken(fr, &ies);
				if (ast_strlen_zero(iaxs[fr->callno]->secret) && ast_strlen_zero(iaxs[fr->callno]->inkeys)) {
					if (strcmp(iaxs[fr->callno]->exten, "TBD") && !exists) {
						memset(&ied0, 0, sizeof(ied0));
						iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No such context/extension");
						iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_NO_ROUTE_DESTINATION);
						send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
						if (!iaxs[fr->callno]) {
							break;
						}
						if (authdebug) {
							ast_log(LOG_NOTICE, "Rejected connect attempt from %s, request '%s@%s' does not exist\n",
									ast_sockaddr_stringify(&addr), iaxs[fr->callno]->exten, iaxs[fr->callno]->context);
						}
					} else {
						/* Select an appropriate format */

						if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOPREFS)) {
							if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
								using_prefs = "reqonly";
							} else {
								using_prefs = "disabled";
							}
							format = iaxs[fr->callno]->peerformat & iaxs[fr->callno]->capability;
							memset(&pref, 0, sizeof(pref));
							strcpy(caller_pref_buf, "disabled");
							strcpy(host_pref_buf, "disabled");
						} else {
							struct ast_format *tmpfmt;
							using_prefs = "mine";
							/* If the information elements are in here... use them */
							if (ies.codec_prefs)
								iax2_codec_pref_convert(&iaxs[fr->callno]->rprefs, ies.codec_prefs, 32, 0);
							if (iax2_codec_pref_index(&iaxs[fr->callno]->rprefs, 0, &tmpfmt)) {
								/* If we are codec_first_choice we let the caller have the 1st shot at picking the codec.*/
								if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_USER_FIRST)) {
									pref = iaxs[fr->callno]->rprefs;
									using_prefs = "caller";
								} else {
									pref = iaxs[fr->callno]->prefs;
								}
							} else
								pref = iaxs[fr->callno]->prefs;

							format = iax2_codec_choose(&pref, iaxs[fr->callno]->capability & iaxs[fr->callno]->peercapability);
							iax2_codec_pref_string(&iaxs[fr->callno]->rprefs, caller_pref_buf, sizeof(caller_pref_buf) - 1);
							iax2_codec_pref_string(&iaxs[fr->callno]->prefs, host_pref_buf, sizeof(host_pref_buf) - 1);
						}
						if (!format) {
							if(!ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP))
								format = iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability;
							if (!format) {
								memset(&ied0, 0, sizeof(ied0));
								iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
								iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
								send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
								if (!iaxs[fr->callno]) {
									break;
								}
								if (authdebug) {
									struct ast_str *peer_buf = ast_str_alloca(64);
									struct ast_str *cap_buf = ast_str_alloca(64);
									struct ast_str *peer_form_buf = ast_str_alloca(64);

									if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
										ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested '%s' incompatible with our capability '%s'.\n",
											ast_sockaddr_stringify(&addr),
											iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
									} else {
										ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability '%s'/'%s' incompatible with our capability '%s'.\n",
											ast_sockaddr_stringify(&addr),
											iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->peercapability, &peer_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
									}
								}
							} else {
								/* Pick one... */
								if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
									if(!(iaxs[fr->callno]->peerformat & iaxs[fr->callno]->capability))
										format = 0;
								} else {
									if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOPREFS)) {
										using_prefs = ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP) ? "reqonly" : "disabled";
										memset(&pref, 0, sizeof(pref));
										format = iax2_format_compatibility_best(iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability);
										strcpy(caller_pref_buf,"disabled");
										strcpy(host_pref_buf,"disabled");
									} else {
										struct ast_format *tmpfmt;
										using_prefs = "mine";
										if (iax2_codec_pref_index(&iaxs[fr->callno]->rprefs, 0, &tmpfmt)) {
											/* Do the opposite of what we tried above. */
											if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_USER_FIRST)) {
												pref = iaxs[fr->callno]->prefs;
											} else {
												pref = iaxs[fr->callno]->rprefs;
												using_prefs = "caller";
											}
											format = iax2_codec_choose(&pref, iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability);
										} else /* if no codec_prefs IE do it the old way */
											format = iax2_format_compatibility_best(iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability);
									}
								}

								if (!format) {
									struct ast_str *peer_buf = ast_str_alloca(64);
									struct ast_str *cap_buf = ast_str_alloca(64);
									struct ast_str *peer_form_buf = ast_str_alloca(64);

									memset(&ied0, 0, sizeof(ied0));
									iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
									iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
									ast_log(LOG_ERROR, "No best format in '%s'???\n", iax2_getformatname_multiple(iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability, &cap_buf));
									send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
									if (!iaxs[fr->callno]) {
										break;
									}
									if (authdebug) {
										ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability '%s'/'%s' incompatible with our capability '%s'.\n",
											ast_sockaddr_stringify(&addr),
											iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->peercapability, &peer_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
									}
									ast_set_flag64(iaxs[fr->callno], IAX_ALREADYGONE);
									break;
								}
							}
						}
						if (format) {
							/* No authentication required, let them in */
							memset(&ied1, 0, sizeof(ied1));
							iax_ie_append_int(&ied1, IAX_IE_FORMAT, format);
							iax_ie_append_versioned_uint64(&ied1, IAX_IE_FORMAT2, 0, format);
							send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACCEPT, 0, ied1.buf, ied1.pos, -1);
							if (strcmp(iaxs[fr->callno]->exten, "TBD")) {
								ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED);
								ast_verb(3, "Accepting UNAUTHENTICATED call from %s:\n"
												"%srequested format = %s,\n"
												"%srequested prefs = %s,\n"
												"%sactual format = %s,\n"
												"%shost prefs = %s,\n"
												"%spriority = %s\n",
												ast_sockaddr_stringify(&addr),
												VERBOSE_PREFIX_4,
												iax2_getformatname(iaxs[fr->callno]->peerformat),
												VERBOSE_PREFIX_4,
												caller_pref_buf,
												VERBOSE_PREFIX_4,
												iax2_getformatname(format),
												VERBOSE_PREFIX_4,
												host_pref_buf,
												VERBOSE_PREFIX_4,
												using_prefs);

								iaxs[fr->callno]->chosenformat = format;

								/* Since this is a new call, we should go ahead and set the callid for it. */
								iax_pvt_callid_new(fr->callno);
								ast_set_flag64(iaxs[fr->callno], IAX_DELAYPBXSTART);
							} else {
								ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_TBD);
								/* If this is a TBD call, we're ready but now what...  */
								ast_verb(3, "Accepted unauthenticated TBD call from %s\n", ast_sockaddr_stringify(&addr));
							}
						}
					}
					break;
				}
				if (iaxs[fr->callno]->authmethods & IAX_AUTH_MD5)
					merge_encryption(iaxs[fr->callno],ies.encmethods);
				else
					iaxs[fr->callno]->encmethods = 0;
				if (!authenticate_request(fr->callno) && iaxs[fr->callno])
					ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_AUTHENTICATED);
				break;
			case IAX_COMMAND_DPREQ:
				/* Request status in the dialplan */
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_TBD) &&
					!ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED) && ies.called_number) {
					if (iaxcompat) {
						/* Spawn a thread for the lookup */
						spawn_dp_lookup(fr->callno, iaxs[fr->callno]->context, ies.called_number, iaxs[fr->callno]->cid_num);
					} else {
						/* Just look it up */
						dp_lookup(fr->callno, iaxs[fr->callno]->context, ies.called_number, iaxs[fr->callno]->cid_num, 1);
					}
				}
				break;
			case IAX_COMMAND_HANGUP:
				ast_set_flag64(iaxs[fr->callno], IAX_ALREADYGONE);
				ast_debug(1, "Immediately destroying %d, having received hangup\n", fr->callno);
				/* Set hangup cause according to remote and hangupsource */
				if (iaxs[fr->callno]->owner) {
					set_hangup_source_and_cause(fr->callno, ies.causecode);
					if (!iaxs[fr->callno]) {
						break;
					}
				}

				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				iax2_destroy(fr->callno);
				break;
			case IAX_COMMAND_REJECT:
				/* Set hangup cause according to remote and hangup source */
				if (iaxs[fr->callno]->owner) {
					set_hangup_source_and_cause(fr->callno, ies.causecode);
					if (!iaxs[fr->callno]) {
						break;
					}
				}

				if (!ast_test_flag64(iaxs[fr->callno], IAX_PROVISION)) {
					if (iaxs[fr->callno]->owner && authdebug)
						ast_log(LOG_WARNING, "Call rejected by %s: %s\n",
							ast_sockaddr_stringify(&addr),
							ies.cause ? ies.cause : "<Unknown>");
					ast_debug(1, "Immediately destroying %d, having received reject\n",
						fr->callno);
				}
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK,
						       fr->ts, NULL, 0, fr->iseqno);
				if (!ast_test_flag64(iaxs[fr->callno], IAX_PROVISION))
					iaxs[fr->callno]->error = EPERM;
				iax2_destroy(fr->callno);
				break;
			case IAX_COMMAND_TRANSFER:
			{
				iax2_lock_owner(fr->callno);
				if (!iaxs[fr->callno]) {
					/* Initiating call went away before we could transfer. */
					break;
				}
				if (iaxs[fr->callno]->owner) {
					struct ast_channel *owner = iaxs[fr->callno]->owner;
					char *context = ast_strdupa(iaxs[fr->callno]->context);

					ast_channel_ref(owner);
					ast_channel_unlock(owner);
					ast_mutex_unlock(&iaxsl[fr->callno]);

					if (ast_bridge_transfer_blind(1, owner, ies.called_number,
								context, NULL, NULL) != AST_BRIDGE_TRANSFER_SUCCESS) {
						ast_log(LOG_WARNING, "Blind transfer of '%s' to '%s@%s' failed\n",
							ast_channel_name(owner), ies.called_number,
							context);
					}

					ast_channel_unref(owner);
					ast_mutex_lock(&iaxsl[fr->callno]);
				}

				break;
			}
			case IAX_COMMAND_ACCEPT:
				/* Ignore if call is already up or needs authentication or is a TBD */
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED | IAX_STATE_TBD | IAX_STATE_AUTHENTICATED))
					break;
				if (ast_test_flag64(iaxs[fr->callno], IAX_PROVISION)) {
					/* Send ack immediately, before we destroy */
					send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
					iax2_destroy(fr->callno);
					break;
				}
				if (ies.format) {
					iaxs[fr->callno]->peerformat = ies.format;
				} else {
					if (iaxs[fr->callno]->owner)
						iaxs[fr->callno]->peerformat = iax2_format_compatibility_cap2bitfield(ast_channel_nativeformats(iaxs[fr->callno]->owner));
					else
						iaxs[fr->callno]->peerformat = iaxs[fr->callno]->capability;
				}
				ast_verb(3, "Call accepted by %s (format %s)\n", ast_sockaddr_stringify(&addr),
						iax2_getformatname(iaxs[fr->callno]->peerformat));
				if (!(iaxs[fr->callno]->peerformat & iaxs[fr->callno]->capability)) {
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
					iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
					send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
					if (!iaxs[fr->callno]) {
						break;
					}
					if (authdebug) {
						struct ast_str *peer_buf = ast_str_alloca(64);
						struct ast_str *cap_buf = ast_str_alloca(64);

						ast_log(LOG_NOTICE, "Rejected call to %s, format %s incompatible with our capability %s.\n",
							ast_sockaddr_stringify(&addr),
							iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_buf),
							iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
					}
				} else {
					struct ast_format_cap *native = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

					ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED);
					iax2_lock_owner(fr->callno);
					if (iaxs[fr->callno] && iaxs[fr->callno]->owner && native) {
						struct ast_str *cap_buf = ast_str_alloca(64);

						/* Switch us to use a compatible format */
						iax2_codec_pref_best_bitfield2cap(
							iaxs[fr->callno]->peerformat, &iaxs[fr->callno]->rprefs,
							native);
						ast_channel_nativeformats_set(iaxs[fr->callno]->owner, native);
						ast_verb(3, "Format for call is %s\n", ast_format_cap_get_names(ast_channel_nativeformats(iaxs[fr->callno]->owner), &cap_buf));

						/* Setup read/write formats properly. */
						if (ast_channel_writeformat(iaxs[fr->callno]->owner))
							ast_set_write_format(iaxs[fr->callno]->owner, ast_channel_writeformat(iaxs[fr->callno]->owner));
						if (ast_channel_readformat(iaxs[fr->callno]->owner))
							ast_set_read_format(iaxs[fr->callno]->owner, ast_channel_readformat(iaxs[fr->callno]->owner));
						ast_channel_unlock(iaxs[fr->callno]->owner);
					}

					ao2_cleanup(native);
				}
				if (iaxs[fr->callno]) {
					AST_LIST_LOCK(&dpcache);
					AST_LIST_TRAVERSE(&iaxs[fr->callno]->dpentries, dp, peer_list)
						if (!(dp->flags & CACHE_FLAG_TRANSMITTED))
							iax2_dprequest(dp, fr->callno);
					AST_LIST_UNLOCK(&dpcache);
				}
				break;
			case IAX_COMMAND_POKE:
				/* Send back a pong packet with the original timestamp */
				send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_PONG, fr->ts, NULL, 0, -1);
				break;
			case IAX_COMMAND_PING:
			{
				struct iax_ie_data pingied;
				construct_rr(iaxs[fr->callno], &pingied);
				/* Send back a pong packet with the original timestamp */
				send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_PONG, fr->ts, pingied.buf, pingied.pos, -1);
			}
				break;
			case IAX_COMMAND_PONG:
				/* Calculate ping time */
				iaxs[fr->callno]->pingtime =  calc_timestamp(iaxs[fr->callno], 0, &f) - fr->ts;
				/* save RR info */
				save_rr(fr, &ies);

				/* Good time to write jb stats for this call */
				log_jitterstats(fr->callno);

				if (iaxs[fr->callno]->peerpoke) {
					RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
					peer = iaxs[fr->callno]->peerpoke;
					if ((peer->lastms < 0)  || (peer->historicms > peer->maxms)) {
						if (iaxs[fr->callno]->pingtime <= peer->maxms) {
							ast_log(LOG_NOTICE, "Peer '%s' is now REACHABLE! Time: %u\n", peer->name, iaxs[fr->callno]->pingtime);
							ast_endpoint_set_state(peer->endpoint, AST_ENDPOINT_ONLINE);
							blob = ast_json_pack("{s: s, s: i}",
								"peer_status", "Reachable",
								"time", iaxs[fr->callno]->pingtime);
							ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "IAX2/%s", peer->name); /* Activate notification */
						}
					} else if ((peer->historicms > 0) && (peer->historicms <= peer->maxms)) {
						if (iaxs[fr->callno]->pingtime > peer->maxms) {
							ast_log(LOG_NOTICE, "Peer '%s' is now TOO LAGGED (%u ms)!\n", peer->name, iaxs[fr->callno]->pingtime);
							ast_endpoint_set_state(peer->endpoint, AST_ENDPOINT_ONLINE);
							blob = ast_json_pack("{s: s, s: i}",
								"peer_status", "Lagged",
								"time", iaxs[fr->callno]->pingtime);
							ast_devstate_changed(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_CACHABLE, "IAX2/%s", peer->name); /* Activate notification */
						}
					}
					ast_endpoint_blob_publish(peer->endpoint, ast_endpoint_state_type(), blob);
					peer->lastms = iaxs[fr->callno]->pingtime;
					if (peer->smoothing && (peer->lastms > -1))
						peer->historicms = (iaxs[fr->callno]->pingtime + peer->historicms) / 2;
					else if (peer->smoothing && peer->lastms < 0)
						peer->historicms = (0 + peer->historicms) / 2;
					else
						peer->historicms = iaxs[fr->callno]->pingtime;

					/* Remove scheduled iax2_poke_noanswer */
					if (peer->pokeexpire > -1) {
						if (!AST_SCHED_DEL(sched, peer->pokeexpire)) {
							peer_unref(peer);
							peer->pokeexpire = -1;
						}
					}
					/* Schedule the next cycle */
					if ((peer->lastms < 0)  || (peer->historicms > peer->maxms))
						peer->pokeexpire = iax2_sched_add(sched, peer->pokefreqnotok, iax2_poke_peer_s, peer_ref(peer));
					else
						peer->pokeexpire = iax2_sched_add(sched, peer->pokefreqok, iax2_poke_peer_s, peer_ref(peer));
					if (peer->pokeexpire == -1)
						peer_unref(peer);
					/* and finally send the ack */
					send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
					/* And wrap up the qualify call */
					iax2_destroy(fr->callno);
					peer->callno = 0;
					ast_debug(1, "Peer %s: got pong, lastms %d, historicms %d, maxms %d\n", peer->name, peer->lastms, peer->historicms, peer->maxms);
				}
				break;
			case IAX_COMMAND_LAGRQ:
			case IAX_COMMAND_LAGRP:
				f.src = "LAGRQ";
				f.mallocd = 0;
				f.offset = 0;
				f.samples = 0;
				iax_frame_wrap(fr, &f);
				if (f.subclass.integer == IAX_COMMAND_LAGRQ) {
					/* Received a LAGRQ - echo back a LAGRP */
					fr->af.subclass.integer = IAX_COMMAND_LAGRP;
					iax2_send(iaxs[fr->callno], &fr->af, fr->ts, -1, 0, 0, 0);
				} else {
					/* Received LAGRP in response to our LAGRQ */
					unsigned int ts;
					/* This is a reply we've been given, actually measure the difference */
					ts = calc_timestamp(iaxs[fr->callno], 0, &fr->af);
					iaxs[fr->callno]->lag = ts - fr->ts;
					if (iaxdebug)
						ast_debug(1, "Peer %s lag measured as %dms\n",
							ast_sockaddr_stringify(&addr), iaxs[fr->callno]->lag);
				}
				break;
			case IAX_COMMAND_AUTHREQ:
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED | IAX_STATE_TBD)) {
					ast_log(LOG_WARNING, "Call on %s is already up, can't start on it\n", iaxs[fr->callno]->owner ? ast_channel_name(iaxs[fr->callno]->owner) : "<Unknown>");
					break;
				}
				if (authenticate_reply(iaxs[fr->callno], &iaxs[fr->callno]->addr, &ies, iaxs[fr->callno]->secret, iaxs[fr->callno]->outkey)) {
					struct ast_frame hangup_fr = { .frametype = AST_FRAME_CONTROL,
								.subclass.integer = AST_CONTROL_HANGUP,
					};
					ast_log(LOG_WARNING,
						"I don't know how to authenticate %s to %s\n",
						ies.username ? ies.username : "<unknown>", ast_sockaddr_stringify(&addr));
					iax2_queue_frame(fr->callno, &hangup_fr);
				}
				break;
			case IAX_COMMAND_AUTHREP:
				/* For security, always ack immediately */
				if (delayreject)
					send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				/* Ignore once we've started */
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED | IAX_STATE_TBD)) {
					ast_log(LOG_WARNING, "Call on %s is already up, can't start on it\n", iaxs[fr->callno]->owner ? ast_channel_name(iaxs[fr->callno]->owner) : "<Unknown>");
					break;
				}
				if (authenticate_verify(iaxs[fr->callno], &ies)) {
					if (authdebug)
						ast_log(LOG_NOTICE, "Host %s failed to authenticate as %s\n", ast_sockaddr_stringify(&addr),
								iaxs[fr->callno]->username);
					memset(&ied0, 0, sizeof(ied0));
					auth_fail(fr->callno, IAX_COMMAND_REJECT);
					break;
				}
				if (strcasecmp(iaxs[fr->callno]->exten, "TBD")) {
					/* This might re-enter the IAX code and need the lock */
					exists = ast_exists_extension(NULL, iaxs[fr->callno]->context, iaxs[fr->callno]->exten, 1, iaxs[fr->callno]->cid_num);
				} else
					exists = 0;
				if (strcmp(iaxs[fr->callno]->exten, "TBD") && !exists) {
					if (authdebug)
						ast_log(LOG_NOTICE, "Rejected connect attempt from %s, request '%s@%s' does not exist\n",
								ast_sockaddr_stringify(&addr),
								iaxs[fr->callno]->exten,
								iaxs[fr->callno]->context);
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No such context/extension");
					iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_NO_ROUTE_DESTINATION);
					send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
					if (!iaxs[fr->callno]) {
						break;
					}
				} else {
					/* Select an appropriate format */
					if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOPREFS)) {
						if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
							using_prefs = "reqonly";
						} else {
							using_prefs = "disabled";
						}
						format = iaxs[fr->callno]->peerformat & iaxs[fr->callno]->capability;
						memset(&pref, 0, sizeof(pref));
						strcpy(caller_pref_buf, "disabled");
						strcpy(host_pref_buf, "disabled");
					} else {
						struct ast_format *tmpfmt;
						using_prefs = "mine";
						if (ies.codec_prefs)
							iax2_codec_pref_convert(&iaxs[fr->callno]->rprefs, ies.codec_prefs, 32, 0);
						if (iax2_codec_pref_index(&iaxs[fr->callno]->rprefs, 0, &tmpfmt)) {
							if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_USER_FIRST)) {
								pref = iaxs[fr->callno]->rprefs;
								using_prefs = "caller";
							} else {
								pref = iaxs[fr->callno]->prefs;
							}
						} else /* if no codec_prefs IE do it the old way */
							pref = iaxs[fr->callno]->prefs;
						format = iax2_codec_choose(&pref, iaxs[fr->callno]->capability & iaxs[fr->callno]->peercapability);
						iax2_codec_pref_string(&iaxs[fr->callno]->rprefs, caller_pref_buf, sizeof(caller_pref_buf) - 1);
						iax2_codec_pref_string(&iaxs[fr->callno]->prefs, host_pref_buf, sizeof(host_pref_buf) - 1);
					}
					if (!format) {
						struct ast_str *cap_buf = ast_str_alloca(64);
						struct ast_str *peer_buf = ast_str_alloca(64);
						struct ast_str *peer_form_buf = ast_str_alloca(64);

						if(!ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
							ast_debug(1, "We don't do requested format %s, falling back to peer capability '%s'\n",
								iax2_getformatname(iaxs[fr->callno]->peerformat),
								iax2_getformatname_multiple(iaxs[fr->callno]->peercapability, &peer_buf));
							format = iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability;
						}
						if (!format) {
							if (authdebug) {
								if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
									ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested '%s' incompatible with our capability '%s'.\n",
											ast_sockaddr_stringify(&addr),
										iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
										iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
								} else {
									ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability '%s'/'%s' incompatible with our capability '%s'.\n",
										ast_sockaddr_stringify(&addr),
										iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
										iax2_getformatname_multiple(iaxs[fr->callno]->peercapability, &peer_buf),
										iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
								}
							}
							memset(&ied0, 0, sizeof(ied0));
							iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
							iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
							send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
							if (!iaxs[fr->callno]) {
								break;
							}
						} else {
							/* Pick one... */
							if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
								if(!(iaxs[fr->callno]->peerformat & iaxs[fr->callno]->capability))
									format = 0;
							} else {
								if(ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOPREFS)) {
									using_prefs = ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP) ? "reqonly" : "disabled";
									memset(&pref, 0, sizeof(pref));
									format = ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)
										? iaxs[fr->callno]->peerformat
										: iax2_format_compatibility_best(iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability);
									strcpy(caller_pref_buf,"disabled");
									strcpy(host_pref_buf,"disabled");
								} else {
									struct ast_format *tmpfmt;
									using_prefs = "mine";
									if (iax2_codec_pref_index(&iaxs[fr->callno]->rprefs, 0, &tmpfmt)) {
										/* Do the opposite of what we tried above. */
										if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_USER_FIRST)) {
											pref = iaxs[fr->callno]->prefs;
										} else {
											pref = iaxs[fr->callno]->rprefs;
											using_prefs = "caller";
										}
										format = iax2_codec_choose(&pref, iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability);
									} else /* if no codec_prefs IE do it the old way */
										format = iax2_format_compatibility_best(iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability);
								}
							}
							if (!format) {
								struct ast_str *cap_buf = ast_str_alloca(64);
								struct ast_str *peer_buf = ast_str_alloca(64);
								struct ast_str *peer_form_buf = ast_str_alloca(64);

								ast_log(LOG_ERROR, "No best format in %s???\n",
									iax2_getformatname_multiple(iaxs[fr->callno]->peercapability & iaxs[fr->callno]->capability, &cap_buf));
								if (authdebug) {
									if (ast_test_flag64(iaxs[fr->callno], IAX_CODEC_NOCAP)) {
										ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested '%s' incompatible with our capability '%s'.\n",
											ast_sockaddr_stringify(&addr),
											iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
									} else {
										ast_log(LOG_NOTICE, "Rejected connect attempt from %s, requested/capability '%s'/'%s' incompatible with our capability '%s'.\n",
											ast_sockaddr_stringify(&addr),
											iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &peer_form_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->peercapability, &peer_buf),
											iax2_getformatname_multiple(iaxs[fr->callno]->capability, &cap_buf));
									}
								}
								memset(&ied0, 0, sizeof(ied0));
								iax_ie_append_str(&ied0, IAX_IE_CAUSE, "Unable to negotiate codec");
								iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
								send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
								if (!iaxs[fr->callno]) {
									break;
								}
							}
						}
					}
					if (format) {
						/* Authentication received */
						memset(&ied1, 0, sizeof(ied1));
						iax_ie_append_int(&ied1, IAX_IE_FORMAT, format);
						iax_ie_append_versioned_uint64(&ied1, IAX_IE_FORMAT2, 0, format);
						send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACCEPT, 0, ied1.buf, ied1.pos, -1);
						if (strcmp(iaxs[fr->callno]->exten, "TBD")) {
							ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED);
							ast_verb(3, "Accepting AUTHENTICATED call from %s:\n"
											"%srequested format = %s,\n"
											"%srequested prefs = %s,\n"
											"%sactual format = %s,\n"
											"%shost prefs = %s,\n"
											"%spriority = %s\n",
											ast_sockaddr_stringify(&addr),
											VERBOSE_PREFIX_4,
											iax2_getformatname(iaxs[fr->callno]->peerformat),
											VERBOSE_PREFIX_4,
											caller_pref_buf,
											VERBOSE_PREFIX_4,
											iax2_getformatname(format),
											VERBOSE_PREFIX_4,
											host_pref_buf,
											VERBOSE_PREFIX_4,
											using_prefs);

							ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED);
							c = ast_iax2_new(fr->callno, AST_STATE_RING, format,
								&iaxs[fr->callno]->rprefs, NULL, NULL, 1);
							if (!c) {
								iax2_destroy(fr->callno);
							} else if (ies.vars) {
								struct ast_datastore *variablestore;
								struct ast_variable *var, *prev = NULL;
								AST_LIST_HEAD(, ast_var_t) *varlist;
								varlist = ast_calloc(1, sizeof(*varlist));
								variablestore = ast_datastore_alloc(&iax2_variable_datastore_info, NULL);
								if (variablestore && varlist) {
									variablestore->data = varlist;
									variablestore->inheritance = DATASTORE_INHERIT_FOREVER;
									AST_LIST_HEAD_INIT(varlist);
									ast_debug(1, "I can haz IAX vars? w00t\n");
									for (var = ies.vars; var; var = var->next) {
										struct ast_var_t *newvar = ast_var_assign(var->name, var->value);
										if (prev)
											ast_free(prev);
										prev = var;
										if (!newvar) {
											/* Don't abort list traversal, as this would leave ies.vars in an inconsistent state. */
											ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
										} else {
											AST_LIST_INSERT_TAIL(varlist, newvar, entries);
										}
									}
									if (prev)
										ast_free(prev);
									ies.vars = NULL;
									ast_channel_datastore_add(c, variablestore);
								} else {
									ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
									if (variablestore)
										ast_datastore_free(variablestore);
									if (varlist)
										ast_free(varlist);
								}
							}
						} else {
							ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_TBD);
							/* If this is a TBD call, we're ready but now what...  */
							ast_verb(3, "Accepted AUTHENTICATED TBD call from %s\n", ast_sockaddr_stringify(&addr));
							if (ast_test_flag64(iaxs[fr->callno], IAX_IMMEDIATE)) {
								goto immediatedial;
							}
						}
					}
				}
				break;
			case IAX_COMMAND_DIAL:
immediatedial:
				if (ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_TBD)) {
					ast_clear_flag(&iaxs[fr->callno]->state, IAX_STATE_TBD);
					ast_string_field_set(iaxs[fr->callno], exten, ies.called_number ? ies.called_number : "s");
					if (!ast_exists_extension(NULL, iaxs[fr->callno]->context, iaxs[fr->callno]->exten, 1, iaxs[fr->callno]->cid_num)) {
						if (authdebug)
							ast_log(LOG_NOTICE, "Rejected dial attempt from %s, request '%s@%s' does not exist\n",
									ast_sockaddr_stringify(&addr),
									iaxs[fr->callno]->exten,
									iaxs[fr->callno]->context);
						memset(&ied0, 0, sizeof(ied0));
						iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No such context/extension");
						iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_NO_ROUTE_DESTINATION);
						send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
						if (!iaxs[fr->callno]) {
							break;
						}
					} else {
						struct ast_str *cap_buf = ast_str_alloca(64);
						ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED);
						ast_verb(3, "Accepting DIAL from %s, formats = %s\n",
								ast_sockaddr_stringify(&addr),
								iax2_getformatname_multiple(iaxs[fr->callno]->peerformat, &cap_buf));
						ast_set_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED);
						send_command(iaxs[fr->callno], AST_FRAME_CONTROL, AST_CONTROL_PROGRESS, 0, NULL, 0, -1);
						c = ast_iax2_new(fr->callno, AST_STATE_RING,
							iaxs[fr->callno]->peerformat, &iaxs[fr->callno]->rprefs,
							NULL, NULL, 1);
						if (!c) {
							iax2_destroy(fr->callno);
						} else if (ies.vars) {
							struct ast_datastore *variablestore;
							struct ast_variable *var, *prev = NULL;
							AST_LIST_HEAD(, ast_var_t) *varlist;
							varlist = ast_calloc(1, sizeof(*varlist));
							variablestore = ast_datastore_alloc(&iax2_variable_datastore_info, NULL);
							ast_debug(1, "I can haz IAX vars? w00t\n");
							if (variablestore && varlist) {
								variablestore->data = varlist;
								variablestore->inheritance = DATASTORE_INHERIT_FOREVER;
								AST_LIST_HEAD_INIT(varlist);
								for (var = ies.vars; var; var = var->next) {
									struct ast_var_t *newvar = ast_var_assign(var->name, var->value);
									if (prev)
										ast_free(prev);
									prev = var;
									if (!newvar) {
										/* Don't abort list traversal, as this would leave ies.vars in an inconsistent state. */
										ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
									} else {
										AST_LIST_INSERT_TAIL(varlist, newvar, entries);
									}
								}
								if (prev)
									ast_free(prev);
								ies.vars = NULL;
								ast_channel_datastore_add(c, variablestore);
							} else {
								ast_log(LOG_ERROR, "Memory allocation error while processing IAX2 variables\n");
								if (variablestore)
									ast_datastore_free(variablestore);
								if (varlist)
									ast_free(varlist);
							}
						}
					}
				}
				break;
			case IAX_COMMAND_INVAL:
				iaxs[fr->callno]->error = ENOTCONN;
				ast_debug(1, "Immediately destroying %d, having received INVAL\n", fr->callno);
				iax2_destroy(fr->callno);
				ast_debug(1, "Destroying call %d\n", fr->callno);
				break;
			case IAX_COMMAND_VNAK:
				ast_debug(1, "Received VNAK: resending outstanding frames\n");
				/* Force retransmission */
				vnak_retransmit(fr->callno, fr->iseqno);
				break;
			case IAX_COMMAND_REGREQ:
			case IAX_COMMAND_REGREL:
				/* For security, always ack immediately */
				if (delayreject)
					send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				if (register_verify(fr->callno, &addr, &ies)) {
					if (!iaxs[fr->callno]) {
						break;
					}
					/* Send delayed failure */
					auth_fail(fr->callno, IAX_COMMAND_REGREJ);
					break;
				}
				if (!iaxs[fr->callno]) {
					break;
				}
				if ((ast_strlen_zero(iaxs[fr->callno]->secret) && ast_strlen_zero(iaxs[fr->callno]->inkeys)) ||
						ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_AUTHENTICATED)) {

					if (f.subclass.integer == IAX_COMMAND_REGREL) {
						ast_sockaddr_setnull(&addr);
					}
					if (update_registry(&addr, fr->callno, ies.devicetype, fd, ies.refresh)) {
						ast_log(LOG_WARNING, "Registry error\n");
					}
					if (!iaxs[fr->callno]) {
						break;
					}
					if (ies.provverpres && ies.serviceident && !(ast_sockaddr_isnull(&addr))) {
						ast_mutex_unlock(&iaxsl[fr->callno]);
						check_provisioning(&addr, fd, ies.serviceident, ies.provver);
						ast_mutex_lock(&iaxsl[fr->callno]);
					}
					break;
				}
				registry_authrequest(fr->callno);
				break;
			case IAX_COMMAND_REGACK:
				if (iax2_ack_registry(&ies, &addr, fr->callno)) {
					ast_log(LOG_WARNING, "Registration failure\n");
				}
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				iax2_destroy(fr->callno);
				break;
			case IAX_COMMAND_REGREJ:
				if (iaxs[fr->callno]->reg) {
					if (authdebug) {
						ast_log(LOG_NOTICE, "Registration of '%s' rejected: '%s' from: '%s'\n",
								iaxs[fr->callno]->reg->username, ies.cause ? ies.cause : "<unknown>",
								ast_sockaddr_stringify(&addr));
					}
					iax2_publish_registry(iaxs[fr->callno]->reg->username, ast_sockaddr_stringify(&addr), "Rejected", S_OR(ies.cause, "<unknown>"));
					iaxs[fr->callno]->reg->regstate = REG_STATE_REJECTED;
				}
				/* Send ack immediately, before we destroy */
				send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				iax2_destroy(fr->callno);
				break;
			case IAX_COMMAND_REGAUTH:
				/* Authentication request */
				if (registry_rerequest(&ies, fr->callno, &addr)) {
					memset(&ied0, 0, sizeof(ied0));
					iax_ie_append_str(&ied0, IAX_IE_CAUSE, "No authority found");
					iax_ie_append_byte(&ied0, IAX_IE_CAUSECODE, AST_CAUSE_FACILITY_NOT_SUBSCRIBED);
					send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
				}
				break;
			case IAX_COMMAND_TXREJ:
				while (iaxs[fr->callno]
					&& iaxs[fr->callno]->bridgecallno
					&& ast_mutex_trylock(&iaxsl[iaxs[fr->callno]->bridgecallno])) {
					DEADLOCK_AVOIDANCE(&iaxsl[fr->callno]);
				}
				if (!iaxs[fr->callno]) {
					break;
				}

				iaxs[fr->callno]->transferring = TRANSFER_NONE;
				ast_verb(3, "Channel '%s' unable to transfer\n", iaxs[fr->callno]->owner ? ast_channel_name(iaxs[fr->callno]->owner) : "<Unknown>");
				memset(&iaxs[fr->callno]->transfer, 0, sizeof(iaxs[fr->callno]->transfer));

				if (!iaxs[fr->callno]->bridgecallno) {
					break;
				}

				if (iaxs[iaxs[fr->callno]->bridgecallno]
					&& iaxs[iaxs[fr->callno]->bridgecallno]->transferring) {
					iaxs[iaxs[fr->callno]->bridgecallno]->transferring = TRANSFER_NONE;
					send_command(iaxs[iaxs[fr->callno]->bridgecallno], AST_FRAME_IAX, IAX_COMMAND_TXREJ, 0, NULL, 0, -1);
				}
				ast_mutex_unlock(&iaxsl[iaxs[fr->callno]->bridgecallno]);
				break;
			case IAX_COMMAND_TXREADY:
				while (iaxs[fr->callno]
					&& iaxs[fr->callno]->bridgecallno
					&& ast_mutex_trylock(&iaxsl[iaxs[fr->callno]->bridgecallno])) {
					DEADLOCK_AVOIDANCE(&iaxsl[fr->callno]);
				}
				if (!iaxs[fr->callno]) {
					break;
				}

				if (iaxs[fr->callno]->transferring == TRANSFER_BEGIN) {
					iaxs[fr->callno]->transferring = TRANSFER_READY;
				} else if (iaxs[fr->callno]->transferring == TRANSFER_MBEGIN) {
					iaxs[fr->callno]->transferring = TRANSFER_MREADY;
				} else {
					if (iaxs[fr->callno]->bridgecallno) {
						ast_mutex_unlock(&iaxsl[iaxs[fr->callno]->bridgecallno]);
					}
					break;
				}
				ast_verb(3, "Channel '%s' ready to transfer\n", iaxs[fr->callno]->owner ? ast_channel_name(iaxs[fr->callno]->owner) : "<Unknown>");

				if (!iaxs[fr->callno]->bridgecallno) {
					break;
				}

				if (!iaxs[iaxs[fr->callno]->bridgecallno]
					|| (iaxs[iaxs[fr->callno]->bridgecallno]->transferring != TRANSFER_READY
						&& iaxs[iaxs[fr->callno]->bridgecallno]->transferring != TRANSFER_MREADY)) {
					ast_mutex_unlock(&iaxsl[iaxs[fr->callno]->bridgecallno]);
					break;
				}

				/* Both sides are ready */

				/* XXX what isn't checked here is that both sides match transfer types. */

				if (iaxs[fr->callno]->transferring == TRANSFER_MREADY) {
					ast_verb(3, "Attempting media bridge of %s and %s\n", iaxs[fr->callno]->owner ? ast_channel_name(iaxs[fr->callno]->owner) : "<Unknown>",
							iaxs[iaxs[fr->callno]->bridgecallno]->owner ? ast_channel_name(iaxs[iaxs[fr->callno]->bridgecallno]->owner) : "<Unknown>");

					iaxs[iaxs[fr->callno]->bridgecallno]->transferring = TRANSFER_MEDIA;
					iaxs[fr->callno]->transferring = TRANSFER_MEDIA;

					memset(&ied0, 0, sizeof(ied0));
					memset(&ied1, 0, sizeof(ied1));
					iax_ie_append_short(&ied0, IAX_IE_CALLNO, iaxs[iaxs[fr->callno]->bridgecallno]->peercallno);
					iax_ie_append_short(&ied1, IAX_IE_CALLNO, iaxs[fr->callno]->peercallno);
					send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_TXMEDIA, 0, ied0.buf, ied0.pos, -1);
					send_command(iaxs[iaxs[fr->callno]->bridgecallno], AST_FRAME_IAX, IAX_COMMAND_TXMEDIA, 0, ied1.buf, ied1.pos, -1);
				} else {
					ast_verb(3, "Releasing %s and %s\n", iaxs[fr->callno]->owner ? ast_channel_name(iaxs[fr->callno]->owner) : "<Unknown>",
							iaxs[iaxs[fr->callno]->bridgecallno]->owner ? ast_channel_name(iaxs[iaxs[fr->callno]->bridgecallno]->owner) : "<Unknown>");

					iaxs[iaxs[fr->callno]->bridgecallno]->transferring = TRANSFER_RELEASED;
					iaxs[fr->callno]->transferring = TRANSFER_RELEASED;
					ast_set_flag64(iaxs[iaxs[fr->callno]->bridgecallno], IAX_ALREADYGONE);
					ast_set_flag64(iaxs[fr->callno], IAX_ALREADYGONE);

					/* Stop doing lag & ping requests */
					stop_stuff(fr->callno);
					stop_stuff(iaxs[fr->callno]->bridgecallno);

					memset(&ied0, 0, sizeof(ied0));
					memset(&ied1, 0, sizeof(ied1));
					iax_ie_append_short(&ied0, IAX_IE_CALLNO, iaxs[iaxs[fr->callno]->bridgecallno]->peercallno);
					iax_ie_append_short(&ied1, IAX_IE_CALLNO, iaxs[fr->callno]->peercallno);
					send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_TXREL, 0, ied0.buf, ied0.pos, -1);
					send_command(iaxs[iaxs[fr->callno]->bridgecallno], AST_FRAME_IAX, IAX_COMMAND_TXREL, 0, ied1.buf, ied1.pos, -1);
				}
				ast_mutex_unlock(&iaxsl[iaxs[fr->callno]->bridgecallno]);
				break;
			case IAX_COMMAND_TXREQ:
				try_transfer(iaxs[fr->callno], &ies);
				break;
			case IAX_COMMAND_TXCNT:
				if (iaxs[fr->callno]->transferring)
					send_command_transfer(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_TXACC, 0, NULL, 0);
				break;
			case IAX_COMMAND_TXREL:
				/* Send ack immediately, rather than waiting until we've changed addresses */
				send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				complete_transfer(fr->callno, &ies);
				stop_stuff(fr->callno);	/* for attended transfer to work with libiax */
				break;
			case IAX_COMMAND_TXMEDIA:
				if (iaxs[fr->callno]->transferring == TRANSFER_READY) {
					AST_LIST_TRAVERSE(&frame_queue[fr->callno], cur, list) {
						/* Cancel any outstanding frames and start anew */
						if (cur->transfer) {
							cur->retries = -1;
						}
					}
					/* Start sending our media to the transfer address, but otherwise leave the call as-is */
					iaxs[fr->callno]->transferring = TRANSFER_MEDIAPASS;
				}
				break;
			case IAX_COMMAND_RTKEY:
				if (!IAX_CALLENCRYPTED(iaxs[fr->callno])) {
					ast_log(LOG_WARNING,
						"we've been told to rotate our encryption key, "
						"but this isn't an encrypted call. bad things will happen.\n"
					);
					break;
				}

				IAX_DEBUGDIGEST("Receiving", ies.challenge);

				ast_aes_set_decrypt_key((unsigned char *) ies.challenge, &iaxs[fr->callno]->dcx);
				break;
			case IAX_COMMAND_DPREP:
				complete_dpreply(iaxs[fr->callno], &ies);
				break;
			case IAX_COMMAND_UNSUPPORT:
				ast_log(LOG_NOTICE, "Peer did not understand our iax command '%d'\n", ies.iax_unknown);
				break;
			case IAX_COMMAND_FWDOWNL:
				/* Firmware download */
				if (!ast_test_flag64(&globalflags, IAX_ALLOWFWDOWNLOAD)) {
					send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_UNSUPPORT, 0, NULL, 0, -1);
					break;
				}
				memset(&ied0, 0, sizeof(ied0));
				res = iax_firmware_append(&ied0, ies.devicetype, ies.fwdesc);
				if (res < 0)
					send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_REJECT, 0, ied0.buf, ied0.pos, -1);
				else if (res > 0)
					send_command_final(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_FWDATA, 0, ied0.buf, ied0.pos, -1);
				else
					send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_FWDATA, 0, ied0.buf, ied0.pos, -1);
				break;
			case IAX_COMMAND_CALLTOKEN:
			{
				struct iax_frame *cur;
				/* find last sent frame */
				if ((cur = AST_LIST_LAST(&frame_queue[fr->callno])) && ies.calltoken && ies.calltokendata) {
					resend_with_token(fr->callno, cur, (char *) ies.calltokendata);
				}
				break;
			}
			default:
				ast_debug(1, "Unknown IAX command %d on %d/%d\n", f.subclass.integer, fr->callno, iaxs[fr->callno]->peercallno);
				memset(&ied0, 0, sizeof(ied0));
				iax_ie_append_byte(&ied0, IAX_IE_IAX_UNKNOWN, f.subclass.integer);
				send_command(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_UNSUPPORT, 0, ied0.buf, ied0.pos, -1);
			}
			/* Free remote variables (if any) */
			if (ies.vars) {
				ast_variables_destroy(ies.vars);
				ast_debug(1, "I can haz IAX vars, but they is no good :-(\n");
				ies.vars = NULL;
			}

			/* Don't actually pass these frames along */
			if ((f.subclass.integer != IAX_COMMAND_ACK) &&
				(f.subclass.integer != IAX_COMMAND_TXCNT) &&
				(f.subclass.integer != IAX_COMMAND_TXACC) &&
				(f.subclass.integer != IAX_COMMAND_INVAL) &&
				(f.subclass.integer != IAX_COMMAND_VNAK)) {
				if (iaxs[fr->callno] && iaxs[fr->callno]->aseqno != iaxs[fr->callno]->iseqno) {
					send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
				}
			}
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
		/* Unless this is an ACK or INVAL frame, ack it */
		if (iaxs[fr->callno] && iaxs[fr->callno]->aseqno != iaxs[fr->callno]->iseqno)
			send_command_immediate(iaxs[fr->callno], AST_FRAME_IAX, IAX_COMMAND_ACK, fr->ts, NULL, 0,fr->iseqno);
	} else if (minivid) {
		f.frametype = AST_FRAME_VIDEO;
		if (iaxs[fr->callno]->videoformat > 0) {
			if (ntohs(vh->ts) & 0x8000LL) {
				f.subclass.frame_ending = 1;
			}
			f.subclass.format = ast_format_compatibility_bitfield2format(iaxs[fr->callno]->videoformat);
		} else {
			ast_log(LOG_WARNING, "Received mini frame before first full video frame\n");
			iax2_vnak(fr->callno);
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
		f.datalen = res - sizeof(*vh);
		if (f.datalen)
			f.data.ptr = thread->buf + sizeof(*vh);
		else
			f.data.ptr = NULL;
#ifdef IAXTESTS
		if (test_resync) {
			fr->ts = (iaxs[fr->callno]->last & 0xFFFF8000L) | ((ntohs(vh->ts) + test_resync) & 0x7fff);
		} else
#endif /* IAXTESTS */
			fr->ts = (iaxs[fr->callno]->last & 0xFFFF8000L) | (ntohs(vh->ts) & 0x7fff);
	} else {
		/* A mini frame */
		f.frametype = AST_FRAME_VOICE;
		if (iaxs[fr->callno]->voiceformat > 0)
			f.subclass.format = ast_format_compatibility_bitfield2format(iaxs[fr->callno]->voiceformat);
		else {
			ast_debug(1, "Received mini frame before first full voice frame\n");
			iax2_vnak(fr->callno);
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
		f.datalen = res - sizeof(struct ast_iax2_mini_hdr);
		if (f.datalen < 0) {
			ast_log(LOG_WARNING, "Datalen < 0?\n");
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
		if (f.datalen)
			f.data.ptr = thread->buf + sizeof(*mh);
		else
			f.data.ptr = NULL;
#ifdef IAXTESTS
		if (test_resync) {
			fr->ts = (iaxs[fr->callno]->last & 0xFFFF0000L) | ((ntohs(mh->ts) + test_resync) & 0xffff);
		} else
#endif /* IAXTESTS */
		fr->ts = (iaxs[fr->callno]->last & 0xFFFF0000L) | ntohs(mh->ts);
		/* FIXME? Surely right here would be the right place to undo timestamp wraparound? */
	}

	/* Don't pass any packets until we're started */
	if (!iaxs[fr->callno]
		|| !ast_test_flag(&iaxs[fr->callno]->state, IAX_STATE_STARTED)) {
		ast_variables_destroy(ies.vars);
		ast_mutex_unlock(&iaxsl[fr->callno]);
		return 1;
	}

	if (f.frametype == AST_FRAME_CONTROL) {
		if (!iax2_is_control_frame_allowed(f.subclass.integer)) {
			/* Control frame not allowed to come from the wire. */
			ast_debug(2, "Callno %d: Blocked receiving control frame %d.\n",
				fr->callno, f.subclass.integer);
			ast_variables_destroy(ies.vars);
			ast_mutex_unlock(&iaxsl[fr->callno]);
			return 1;
		}
		if (f.subclass.integer == AST_CONTROL_CONNECTED_LINE
			|| f.subclass.integer == AST_CONTROL_REDIRECTING) {
			if (iaxs[fr->callno]
				&& !ast_test_flag64(iaxs[fr->callno], IAX_RECVCONNECTEDLINE)) {
				/* We are not configured to allow receiving these updates. */
				ast_debug(2, "Callno %d: Config blocked receiving control frame %d.\n",
					fr->callno, f.subclass.integer);
				ast_variables_destroy(ies.vars);
				ast_mutex_unlock(&iaxsl[fr->callno]);
				return 1;
			}
		}

		iax2_lock_owner(fr->callno);
		if (iaxs[fr->callno] && iaxs[fr->callno]->owner) {
			if (f.subclass.integer == AST_CONTROL_BUSY) {
				ast_channel_hangupcause_set(iaxs[fr->callno]->owner, AST_CAUSE_BUSY);
			} else if (f.subclass.integer == AST_CONTROL_CONGESTION) {
				ast_channel_hangupcause_set(iaxs[fr->callno]->owner, AST_CAUSE_CONGESTION);
			}
			ast_channel_unlock(iaxs[fr->callno]->owner);
		}
	}

	if (f.frametype == AST_FRAME_CONTROL
		&& f.subclass.integer == AST_CONTROL_CONNECTED_LINE
		&& iaxs[fr->callno]) {
		struct ast_party_connected_line connected;

		/*
		 * Process a received connected line update.
		 *
		 * Initialize defaults.
		 */
		ast_party_connected_line_init(&connected);
		connected.id.number.presentation = iaxs[fr->callno]->calling_pres;
		connected.id.name.presentation = iaxs[fr->callno]->calling_pres;

		if (!ast_connected_line_parse_data(f.data.ptr, f.datalen, &connected)) {
			ast_string_field_set(iaxs[fr->callno], cid_num, connected.id.number.str);
			ast_string_field_set(iaxs[fr->callno], cid_name, connected.id.name.str);
			iaxs[fr->callno]->calling_pres = ast_party_id_presentation(&connected.id);

			iax2_lock_owner(fr->callno);
			if (iaxs[fr->callno] && iaxs[fr->callno]->owner) {
				ast_set_callerid(iaxs[fr->callno]->owner,
					S_COR(connected.id.number.valid, connected.id.number.str, ""),
					S_COR(connected.id.name.valid, connected.id.name.str, ""),
					NULL);
				ast_channel_caller(iaxs[fr->callno]->owner)->id.number.presentation = connected.id.number.presentation;
				ast_channel_caller(iaxs[fr->callno]->owner)->id.name.presentation = connected.id.name.presentation;
				ast_channel_unlock(iaxs[fr->callno]->owner);
			}
		}
		ast_party_connected_line_free(&connected);
	}

	/* Common things */
	f.src = "IAX2";
	f.mallocd = 0;
	f.offset = 0;
	f.len = 0;
	if (f.datalen && (f.frametype == AST_FRAME_VOICE)) {
		f.samples = ast_codec_samples_count(&f);
		/* We need to byteswap incoming slinear samples from network byte order */
		if (ast_format_cmp(f.subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)
			ast_frame_byteswap_be(&f);
	} else
		f.samples = 0;
	iax_frame_wrap(fr, &f);

	/* If this is our most recent packet, use it as our basis for timestamping */
	if (iaxs[fr->callno] && iaxs[fr->callno]->last < fr->ts) {
		/*iaxs[fr->callno]->last = fr->ts; (do it afterwards cos schedule/forward_delivery needs the last ts too)*/
		fr->outoforder = 0;
	} else {
		if (iaxdebug && iaxs[fr->callno]) {
			ast_debug(1, "Received out of order packet... (type=%u, subclass %d, ts = %u, last = %u)\n", f.frametype, f.subclass.integer, fr->ts, iaxs[fr->callno]->last);
		}
		fr->outoforder = 1;
	}
	fr->cacheable = ((f.frametype == AST_FRAME_VOICE) || (f.frametype == AST_FRAME_VIDEO));
	if (iaxs[fr->callno]) {
		duped_fr = iaxfrdup2(fr);
		if (duped_fr) {
			schedule_delivery(duped_fr, updatehistory, 0, &fr->ts);
		}
	}
	if (iaxs[fr->callno] && iaxs[fr->callno]->last < fr->ts) {
		iaxs[fr->callno]->last = fr->ts;
#if 1
		if (iaxdebug)
			ast_debug(1, "For call=%d, set last=%u\n", fr->callno, fr->ts);
#endif
	}

	/* Always run again */
	ast_variables_destroy(ies.vars);
	ast_mutex_unlock(&iaxsl[fr->callno]);
	return 1;
}

static int socket_process(struct iax2_thread *thread)
{
	int res = socket_process_helper(thread);
	if (ast_read_threadstorage_callid()) {
		ast_callid_threadassoc_remove();
	}
	return res;
}

/* Function to clean up process thread if it is cancelled */
static void iax2_process_thread_cleanup(void *data)
{
	struct iax2_thread *thread = data;
	ast_mutex_destroy(&thread->lock);
	ast_cond_destroy(&thread->cond);
	ast_mutex_destroy(&thread->init_lock);
	ast_cond_destroy(&thread->init_cond);
	ast_free(thread);
	/* Ignore check_return warning from Coverity for ast_atomic_dec_and_test below */
	ast_atomic_dec_and_test(&iaxactivethreadcount);
}

static void *iax2_process_thread(void *data)
{
	struct iax2_thread *thread = data;
	struct timeval wait;
	struct timespec ts;
	int put_into_idle = 0;
	int first_time = 1;
	int old_state;

	ast_atomic_fetchadd_int(&iaxactivethreadcount, 1);

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
	pthread_cleanup_push(iax2_process_thread_cleanup, data);

	for (;;) {
		/* Wait for something to signal us to be awake */
		ast_mutex_lock(&thread->lock);

		if (thread->stop) {
			ast_mutex_unlock(&thread->lock);
			break;
		}

		/* Flag that we're ready to accept signals */
		if (first_time) {
			signal_condition(&thread->init_lock, &thread->init_cond);
			first_time = 0;
		}

		/* Put into idle list if applicable */
		if (put_into_idle) {
			insert_idle_thread(thread);
		}

		if (thread->type == IAX_THREAD_TYPE_DYNAMIC) {
			struct iax2_thread *t = NULL;
			/* Wait to be signalled or time out */
			wait = ast_tvadd(ast_tvnow(), ast_samp2tv(30000, 1000));
			ts.tv_sec = wait.tv_sec;
			ts.tv_nsec = wait.tv_usec * 1000;
			if (ast_cond_timedwait(&thread->cond, &thread->lock, &ts) == ETIMEDOUT) {
				/* This thread was never put back into the available dynamic
				 * thread list, so just go away. */
				if (!put_into_idle || thread->stop) {
					ast_mutex_unlock(&thread->lock);
					break;
				}
				AST_LIST_LOCK(&dynamic_list);
				/* Account for the case where this thread is acquired *right* after a timeout */
				if ((t = AST_LIST_REMOVE(&dynamic_list, thread, list)))
					ast_atomic_fetchadd_int(&iaxdynamicthreadcount, -1);
				AST_LIST_UNLOCK(&dynamic_list);
				if (t) {
					/* This dynamic thread timed out waiting for a task and was
					 * not acquired immediately after the timeout,
					 * so it's time to go away. */
					ast_mutex_unlock(&thread->lock);
					break;
				}
				/* Someone grabbed our thread *right* after we timed out.
				 * Wait for them to set us up with something to do and signal
				 * us to continue. */
				wait = ast_tvadd(ast_tvnow(), ast_samp2tv(30000, 1000));
				ts.tv_sec = wait.tv_sec;
				ts.tv_nsec = wait.tv_usec * 1000;
				if (ast_cond_timedwait(&thread->cond, &thread->lock, &ts) == ETIMEDOUT) {
					ast_mutex_unlock(&thread->lock);
					break;
				}
			}
		} else {
			ast_cond_wait(&thread->cond, &thread->lock);
		}

		/* Go back into our respective list */
		put_into_idle = 1;

		ast_mutex_unlock(&thread->lock);

		if (thread->stop) {
			break;
		}

		/* See what we need to do */
		switch (thread->iostate) {
		case IAX_IOSTATE_IDLE:
			continue;
		case IAX_IOSTATE_READY:
			thread->actions++;
			thread->iostate = IAX_IOSTATE_PROCESSING;
			socket_process(thread);
			handle_deferred_full_frames(thread);
			break;
		case IAX_IOSTATE_SCHEDREADY:
			thread->actions++;
			thread->iostate = IAX_IOSTATE_PROCESSING;
#ifdef SCHED_MULTITHREADED
			thread->schedfunc(thread->scheddata);
#endif
			break;
		default:
			break;
		}

		/* The network thread added us to the active_thread list when we were given
		 * frames to process, Now that we are done, we must remove ourselves from
		 * the active list, and return to the idle list */
		AST_LIST_LOCK(&active_list);
		AST_LIST_REMOVE(&active_list, thread, list);
		AST_LIST_UNLOCK(&active_list);

		/* Make sure another frame didn't sneak in there after we thought we were done. */
		handle_deferred_full_frames(thread);

		time(&thread->checktime);
		thread->iostate = IAX_IOSTATE_IDLE;
#ifdef DEBUG_SCHED_MULTITHREAD
		thread->curfunc[0]='\0';
#endif
	}

	/*!
	 * \note For some reason, idle threads are exiting without being
	 * removed from an idle list, which is causing memory
	 * corruption.  Forcibly remove it from the list, if it's there.
	 */
	AST_LIST_LOCK(&idle_list);
	AST_LIST_REMOVE(&idle_list, thread, list);
	AST_LIST_UNLOCK(&idle_list);

	AST_LIST_LOCK(&dynamic_list);
	AST_LIST_REMOVE(&dynamic_list, thread, list);
	AST_LIST_UNLOCK(&dynamic_list);

	if (!thread->stop) {
		/* Nobody asked me to stop so nobody is waiting to join me. */
		pthread_detach(pthread_self());
	}

	/* I am exiting here on my own volition, I need to clean up my own data structures
	* Assume that I am no longer in any of the lists (idle, active, or dynamic)
	*/
	pthread_cleanup_pop(1);
	return NULL;
}

static int iax2_do_register(struct iax2_registry *reg)
{
	struct iax_ie_data ied;
	if (iaxdebug)
		ast_debug(1, "Sending registration request for '%s'\n", reg->username);

	if (reg->dnsmgr &&
	    ((reg->regstate == REG_STATE_TIMEOUT) || ast_sockaddr_isnull(&reg->addr))) {
		/* Maybe the IP has changed, force DNS refresh */
		ast_dnsmgr_refresh(reg->dnsmgr);
	}

	/*
	 * if IP has Changed, free allocated call to create a new one with new IP
	 * call has the pointer to IP and must be updated to the new one
	 */
	if (reg->dnsmgr && ast_dnsmgr_changed(reg->dnsmgr) && (reg->callno > 0)) {
		int callno = reg->callno;
		ast_mutex_lock(&iaxsl[callno]);
		iax2_destroy(callno);
		ast_mutex_unlock(&iaxsl[callno]);
		reg->callno = 0;
	}
	if (ast_sockaddr_isnull(&reg->addr)) {
		if (iaxdebug)
			ast_debug(1, "Unable to send registration request for '%s' without IP address\n", reg->username);
		/* Setup the next registration attempt */
		reg->expire = iax2_sched_replace(reg->expire, sched,
			(5 * reg->refresh / 6) * 1000, iax2_do_register_s, reg);
		return -1;
	}
	if (!ast_sockaddr_port(&reg->addr) && reg->port) {
		ast_sockaddr_set_port(&reg->addr, reg->port);
	}

	if (!reg->callno) {

		ast_debug(3, "Allocate call number\n");

		reg->callno = find_callno_locked(0, 0, &reg->addr, NEW_FORCE, defaultsockfd, 0);
		if (reg->callno < 1) {
			ast_log(LOG_WARNING, "Unable to create call for registration\n");
			return -1;
		} else
			ast_debug(3, "Registration created on call %d\n", reg->callno);
		iaxs[reg->callno]->reg = reg;
		ast_mutex_unlock(&iaxsl[reg->callno]);
	}
	/* Setup the next registration a little early */
	reg->expire = iax2_sched_replace(reg->expire, sched,
		(5 * reg->refresh / 6) * 1000, iax2_do_register_s, reg);
	/* Send the request */
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_str(&ied, IAX_IE_USERNAME, reg->username);
	iax_ie_append_short(&ied, IAX_IE_REFRESH, reg->refresh);
	add_empty_calltoken_ie(iaxs[reg->callno], &ied); /* this _MUST_ be the last ie added */
	send_command(iaxs[reg->callno],AST_FRAME_IAX, IAX_COMMAND_REGREQ, 0, ied.buf, ied.pos, -1);
	reg->regstate = REG_STATE_REGSENT;
	return 0;
}

static int iax2_provision(struct ast_sockaddr *end, int sockfd, const char *dest, const char *template, int force)
{
	/* Returns 1 if provisioned, -1 if not able to find destination, or 0 if no provisioning
	   is found for template */
	struct iax_ie_data provdata;
	struct iax_ie_data ied;
	unsigned int sig;
	struct ast_sockaddr addr;
	int callno;
	struct create_addr_info cai;

	memset(&cai, 0, sizeof(cai));

	ast_debug(1, "Provisioning '%s' from template '%s'\n", dest, template);

	if (iax_provision_build(&provdata, &sig, template, force)) {
		ast_debug(1, "No provisioning found for template '%s'\n", template);
		return 0;
	}

	if (end) {
		ast_sockaddr_copy(&addr, end);
		cai.sockfd = sockfd;
	} else if (create_addr(dest, NULL, &addr, &cai))
		return -1;

	/* Build the rest of the message */
	memset(&ied, 0, sizeof(ied));
	iax_ie_append_raw(&ied, IAX_IE_PROVISIONING, provdata.buf, provdata.pos);

	callno = find_callno_locked(0, 0, &addr, NEW_FORCE, cai.sockfd, 0);
	if (!callno)
		return -1;

	if (iaxs[callno]) {
		/* Schedule autodestruct in case they don't ever give us anything back */
		iaxs[callno]->autoid = iax2_sched_replace(iaxs[callno]->autoid,
			sched, 15000, auto_hangup, (void *)(long)callno);
		ast_set_flag64(iaxs[callno], IAX_PROVISION);
		/* Got a call number now, so go ahead and send the provisioning information */
		send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_PROVISION, 0, ied.buf, ied.pos, -1);
	}
	ast_mutex_unlock(&iaxsl[callno]);

	return 1;
}

static char *papp = "IAX2Provision";

/*! iax2provision
\ingroup applications
*/
static int iax2_prov_app(struct ast_channel *chan, const char *data)
{
	int res;
	char *sdata;
	char *opts;
	int force =0;
	unsigned short callno = PTR_TO_CALLNO(ast_channel_tech_pvt(chan));
	if (ast_strlen_zero(data))
		data = "default";
	sdata = ast_strdupa(data);
	opts = strchr(sdata, '|');
	if (opts)
		*opts='\0';

	if (ast_channel_tech(chan) != &iax2_tech) {
		ast_log(LOG_NOTICE, "Can't provision a non-IAX device!\n");
		return -1;
	}
	if (!callno || !iaxs[callno] || ast_sockaddr_isnull(&iaxs[callno]->addr)) {
		ast_log(LOG_NOTICE, "Can't provision something with no IP?\n");
		return -1;
	}
	res = iax2_provision(&iaxs[callno]->addr, iaxs[callno]->sockfd, NULL, sdata, force);
	ast_verb(3, "Provisioned IAXY at '%s' with '%s'= %d\n",
		ast_sockaddr_stringify(&iaxs[callno]->addr),
		sdata, res);
	return res;
}

static char *handle_cli_iax2_provision(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int force = 0;
	int res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 provision";
		e->usage =
			"Usage: iax2 provision <host> <template> [forced]\n"
			"       Provisions the given peer or IP address using a template\n"
			"       matching either 'template' or '*' if the template is not\n"
			"       found.  If 'forced' is specified, even empty provisioning\n"
			"       fields will be provisioned as empty fields.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3)
			return iax_prov_complete_template(a->line, a->word, a->pos, a->n);
		return NULL;
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;
	if (a->argc > 4) {
		if (!strcasecmp(a->argv[4], "forced"))
			force = 1;
		else
			return CLI_SHOWUSAGE;
	}
	res = iax2_provision(NULL, -1, a->argv[2], a->argv[3], force);
	if (res < 0)
		ast_cli(a->fd, "Unable to find peer/address '%s'\n", a->argv[2]);
	else if (res < 1)
		ast_cli(a->fd, "No template (including wildcard) matching '%s'\n", a->argv[3]);
	else
		ast_cli(a->fd, "Provisioning '%s' with template '%s'%s\n", a->argv[2], a->argv[3], force ? ", forced" : "");
	return CLI_SUCCESS;
}

static void __iax2_poke_noanswer(const void *data)
{
	struct iax2_peer *peer = (struct iax2_peer *)data;
	int callno;

	if (peer->lastms > -1) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

		ast_log(LOG_NOTICE, "Peer '%s' is now UNREACHABLE! Time: %d\n", peer->name, peer->lastms);
		ast_endpoint_set_state(peer->endpoint, AST_ENDPOINT_OFFLINE);
		blob = ast_json_pack("{s: s, s: i}",
			"peer_status", "Unreachable",
			"time", peer->lastms);
		ast_endpoint_blob_publish(peer->endpoint, ast_endpoint_state_type(), blob);
		ast_devstate_changed(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_CACHABLE, "IAX2/%s", peer->name); /* Activate notification */
	}
	if ((callno = peer->callno) > 0) {
		ast_mutex_lock(&iaxsl[callno]);
		iax2_destroy(callno);
		ast_mutex_unlock(&iaxsl[callno]);
	}
	peer->callno = 0;
	peer->lastms = -1;
	/* Try again quickly */
	peer->pokeexpire = iax2_sched_add(sched, peer->pokefreqnotok, iax2_poke_peer_s, peer_ref(peer));
	if (peer->pokeexpire == -1)
		peer_unref(peer);
}

static int iax2_poke_noanswer(const void *data)
{
	struct iax2_peer *peer = (struct iax2_peer *)data;
	peer->pokeexpire = -1;
#ifdef SCHED_MULTITHREADED
	if (schedule_action(__iax2_poke_noanswer, data))
#endif
		__iax2_poke_noanswer(data);
	peer_unref(peer);
	return 0;
}

static int iax2_poke_peer_cb(void *obj, void *arg, int flags)
{
	struct iax2_peer *peer = obj;

	iax2_poke_peer(peer, 0);

	return 0;
}

static int iax2_poke_peer(struct iax2_peer *peer, int heldcall)
{
	int callno;
	int poke_timeout;

	if (!peer->maxms || (ast_sockaddr_isnull(&peer->addr) && !peer->dnsmgr)) {
		/* IF we have no IP without dnsmgr, or this isn't to be monitored, return
		  immediately after clearing things out */
		peer->lastms = 0;
		peer->historicms = 0;
		peer->pokeexpire = -1;
		peer->callno = 0;
		return 0;
	}

	/* The peer could change the callno inside iax2_destroy, since we do deadlock avoidance */
	if ((callno = peer->callno) > 0) {
		ast_log(LOG_NOTICE, "Still have a callno...\n");
		ast_mutex_lock(&iaxsl[callno]);
		iax2_destroy(callno);
		ast_mutex_unlock(&iaxsl[callno]);
	}
	if (heldcall)
		ast_mutex_unlock(&iaxsl[heldcall]);
	callno = peer->callno = find_callno(0, 0, &peer->addr, NEW_FORCE, peer->sockfd, 0);
	if (heldcall)
		ast_mutex_lock(&iaxsl[heldcall]);
	if (callno < 1) {
		ast_log(LOG_WARNING, "Unable to allocate call for poking peer '%s'\n", peer->name);
		return -1;
	}

	if (peer->pokeexpire > -1) {
		if (!AST_SCHED_DEL(sched, peer->pokeexpire)) {
			peer->pokeexpire = -1;
			peer_unref(peer);
		}
	}

	if (peer->lastms < 0){
		/* If the host is already unreachable then use time less than the unreachable
		 * interval. 5/6 is arbitrary multiplier to get value less than
		 * peer->pokefreqnotok. Value less than peer->pokefreqnotok is used to expire
		 * current POKE before starting new POKE (which is scheduled after
		 * peer->pokefreqnotok). */
		poke_timeout = peer->pokefreqnotok * 5  / 6;
	} else {
		/* If the host is reachable, use timeout large enough to allow for multiple
		 * POKE retries. Limit this value to less than peer->pokefreqok. 5/6 is arbitrary
		 * multiplier to get value less than peer->pokefreqok. Value less than
		 * peer->pokefreqok is used to expire current POKE before starting new POKE
		 * (which is scheduled after peer->pokefreqok). */
		poke_timeout = MIN(MAX_RETRY_TIME * 2 + peer->maxms, peer->pokefreqok * 5  / 6);
	}

	/* Queue up a new task to handle no reply */
	peer->pokeexpire = iax2_sched_add(sched, poke_timeout, iax2_poke_noanswer, peer_ref(peer));

	if (peer->pokeexpire == -1)
		peer_unref(peer);

	/* And send the poke */
	ast_mutex_lock(&iaxsl[callno]);
	if (iaxs[callno]) {
		struct iax_ie_data ied = {
			.buf = { 0 },
			.pos = 0,
		};

		/* Speed up retransmission times for this qualify call */
		iaxs[callno]->pingtime = peer->maxms / 8;
		iaxs[callno]->peerpoke = peer;

		add_empty_calltoken_ie(iaxs[callno], &ied); /* this _MUST_ be the last ie added */
		send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_POKE, 0, ied.buf, ied.pos, -1);
	}
	ast_mutex_unlock(&iaxsl[callno]);

	return 0;
}

static void free_context(struct iax2_context *con)
{
	struct iax2_context *conl;
	while(con) {
		conl = con;
		con = con->next;
		ast_free(conl);
	}
}

static struct ast_channel *iax2_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	int callno;
	int res;
	struct ast_sockaddr addr;
	struct ast_channel *c;
	struct parsed_dial_string pds;
	struct create_addr_info cai;
	char *tmpstr;
	ast_callid callid;

	memset(&pds, 0, sizeof(pds));
	tmpstr = ast_strdupa(data);
	parse_dial_string(tmpstr, &pds);

	callid = ast_read_threadstorage_callid();

	if (ast_strlen_zero(pds.peer)) {
		ast_log(LOG_WARNING, "No peer provided in the IAX2 dial string '%s'\n", data);
		return NULL;
	}
	memset(&cai, 0, sizeof(cai));
	cai.capability = iax2_capability;

	ast_copy_flags64(&cai, &globalflags, IAX_NOTRANSFER | IAX_TRANSFERMEDIA | IAX_USEJITTERBUF | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);

	/* Populate our address from the given */
	if (create_addr(pds.peer, NULL, &addr, &cai)) {
		*cause = AST_CAUSE_UNREGISTERED;
		return NULL;
	}

	if (pds.port) {
		int bindport;
		ast_parse_arg(pds.port, PARSE_UINT32 | PARSE_IN_RANGE, &bindport, 0, 65535);
		ast_sockaddr_set_port(&addr, bindport);
	}

	callno = find_callno_locked(0, 0, &addr, NEW_FORCE, cai.sockfd, 0);
	if (callno < 1) {
		ast_log(LOG_WARNING, "Unable to create call\n");
		*cause = AST_CAUSE_CONGESTION;
		return NULL;
	}

	/* If this is a trunk, update it now */
	ast_copy_flags64(iaxs[callno], &cai, IAX_TRUNK | IAX_SENDANI | IAX_NOTRANSFER | IAX_TRANSFERMEDIA | IAX_USEJITTERBUF | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
	if (ast_test_flag64(&cai, IAX_TRUNK)) {
		int new_callno;
		if ((new_callno = make_trunk(callno, 1)) != -1)
			callno = new_callno;
	}
	iaxs[callno]->maxtime = cai.maxtime;
	if (callid) {
		iax_pvt_callid_set(callno, callid);
	}

	if (cai.found) {
		ast_string_field_set(iaxs[callno], host, pds.peer);
	}

	c = ast_iax2_new(callno, AST_STATE_DOWN, cai.capability, &cai.prefs, assignedids,
		requestor, cai.found);

	ast_mutex_unlock(&iaxsl[callno]);

	if (c) {
		struct ast_format_cap *joint;
		struct ast_format *format;
		if (callid) {
			ast_channel_lock(c);
			ast_channel_callid_set(c, callid);
			ast_channel_unlock(c);
		}

		joint = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!joint) {
			ast_hangup(c);
			return NULL;
		}

		ast_format_cap_get_compatible(ast_channel_nativeformats(c), cap, joint);

		/* If there is no joint format find one through translation */
		if (!ast_format_cap_count(joint)) {
			struct ast_format *best_fmt_cap = NULL;
			struct ast_format *best_fmt_native = NULL;

			res = ast_translator_best_choice(cap, ast_channel_nativeformats(c), &best_fmt_cap, &best_fmt_native);
			if (res < 0) {
				struct ast_str *native_cap_buf = ast_str_alloca(256);
				struct ast_str *cap_buf = ast_str_alloca(256);

				ast_log(LOG_WARNING, "Unable to create translator path for %s to %s on %s\n",
					ast_format_cap_get_names(ast_channel_nativeformats(c), &native_cap_buf),
					ast_format_cap_get_names(cap, &cap_buf),
					ast_channel_name(c));
				ast_hangup(c);
				ao2_ref(joint, -1);
				return NULL;
			}
			ast_format_cap_append(joint, best_fmt_native, 0);
			ao2_ref(best_fmt_cap, -1);
			ao2_ref(best_fmt_native, -1);
		}
		ast_channel_nativeformats_set(c, joint);
		format = ast_format_cap_get_format(ast_channel_nativeformats(c), 0);
		ast_channel_set_readformat(c, format);
		ast_channel_set_writeformat(c, format);

		ao2_ref(joint, -1);
		ao2_ref(format, -1);
	}

	return c;
}

static void *network_thread(void *ignore)
{
	if (timer) {
		ast_io_add(io, ast_timer_fd(timer), timing_read, AST_IO_IN | AST_IO_PRI, NULL);
	}

	for (;;) {
		pthread_testcancel();
		/* Wake up once a second just in case SIGURG was sent while
		 * we weren't in poll(), to make sure we don't hang when trying
		 * to unload. */
		if (ast_io_wait(io, 1000) <= 0) {
			break;
		}
	}

	return NULL;
}

static int start_network_thread(void)
{
	struct iax2_thread *thread;
	int threadcount = 0;
	int x;
	for (x = 0; x < iaxthreadcount; x++) {
		thread = ast_calloc(1, sizeof(*thread));
		if (thread) {
			thread->type = IAX_THREAD_TYPE_POOL;
			thread->threadnum = ++threadcount;
			ast_mutex_init(&thread->lock);
			ast_cond_init(&thread->cond, NULL);
			ast_mutex_init(&thread->init_lock);
			ast_cond_init(&thread->init_cond, NULL);

			ast_mutex_lock(&thread->init_lock);

			if (ast_pthread_create_background(&thread->threadid, NULL, iax2_process_thread, thread)) {
				ast_log(LOG_WARNING, "Failed to create new thread!\n");
				ast_mutex_destroy(&thread->lock);
				ast_cond_destroy(&thread->cond);
				ast_mutex_unlock(&thread->init_lock);
				ast_mutex_destroy(&thread->init_lock);
				ast_cond_destroy(&thread->init_cond);
				ast_free(thread);
				thread = NULL;
				continue;
			}
			/* Wait for the thread to be ready */
			ast_cond_wait(&thread->init_cond, &thread->init_lock);

			/* Done with init_lock */
			ast_mutex_unlock(&thread->init_lock);

			AST_LIST_LOCK(&idle_list);
			AST_LIST_INSERT_TAIL(&idle_list, thread, list);
			AST_LIST_UNLOCK(&idle_list);
		}
	}
	if (ast_pthread_create_background(&netthreadid, NULL, network_thread, NULL)) {
		ast_log(LOG_ERROR, "Failed to create new thread!\n");
		return -1;
	}
	ast_verb(2, "%d helper threads started\n", threadcount);
	return 0;
}

static struct iax2_context *build_context(const char *context)
{
	struct iax2_context *con;

	if ((con = ast_calloc(1, sizeof(*con))))
		ast_copy_string(con->context, context, sizeof(con->context));

	return con;
}

static int get_auth_methods(const char *value)
{
	int methods = 0;
	if (strstr(value, "rsa"))
		methods |= IAX_AUTH_RSA;
	if (strstr(value, "md5"))
		methods |= IAX_AUTH_MD5;
	if (strstr(value, "plaintext"))
		methods |= IAX_AUTH_PLAINTEXT;
	return methods;
}


/*! \brief Check if address can be used as packet source.
 \return 0  address available, 1  address unavailable, -1  error
*/
static int check_srcaddr(struct ast_sockaddr *addr)
{
	int sd;

	sd = socket(addr->ss.ss_family, SOCK_DGRAM, 0);
	if (sd < 0) {
		ast_log(LOG_ERROR, "Socket: %s\n", strerror(errno));
		return -1;
	}

	if (ast_bind(sd, addr) < 0) {
		ast_debug(1, "Can't bind: %s\n", strerror(errno));
		close(sd);
		return 1;
	}

	close(sd);
	return 0;
}

/*! \brief Parse the "sourceaddress" value,
  lookup in netsock list and set peer's sockfd. Defaults to defaultsockfd if
  not found. */
static int peer_set_srcaddr(struct iax2_peer *peer, const char *srcaddr)
{
	struct ast_sockaddr addr;
	int nonlocal = 1;
	int port = IAX_DEFAULT_PORTNO;
	int sockfd = defaultsockfd;
	char *tmp;
	char *host;
	char *portstr;

	tmp = ast_strdupa(srcaddr);
	ast_sockaddr_split_hostport(tmp, &host, &portstr, 0);

	if (portstr) {
		port = atoi(portstr);
		if (port < 1)
			port = IAX_DEFAULT_PORTNO;
	}

	addr.ss.ss_family = AST_AF_UNSPEC;
	if (!ast_get_ip(&addr, host)) {
		struct ast_netsock *sock;

		if (check_srcaddr(&addr) == 0) {
			/* ip address valid. */
			ast_sockaddr_set_port(&addr, port);

			if (!(sock = ast_netsock_find(netsock, &addr)))
				sock = ast_netsock_find(outsock, &addr);
			if (sock) {
				sockfd = ast_netsock_sockfd(sock);
				nonlocal = 0;
			} else {
				/* INADDR_ANY matches anyway! */
				ast_sockaddr_parse(&addr, "0.0.0.0", 0);
				ast_sockaddr_set_port(&addr, port);
				if (ast_netsock_find(netsock, &addr)) {
					sock = ast_netsock_bind(outsock, io, srcaddr, port, qos.tos, qos.cos, socket_read, NULL);
					if (sock) {
						sockfd = ast_netsock_sockfd(sock);
						ast_netsock_unref(sock);
						nonlocal = 0;
					} else {
						nonlocal = 2;
					}
				}
			}
		}
	}

	peer->sockfd = sockfd;

	if (nonlocal == 1) {
		ast_log(LOG_WARNING,
			"Non-local or unbound address specified (%s) in sourceaddress for '%s', reverting to default\n",
			srcaddr,
			peer->name);
		return -1;
	} else if (nonlocal == 2) {
		ast_log(LOG_WARNING,
			"Unable to bind to sourceaddress '%s' for '%s', reverting to default\n",
			srcaddr,
			peer->name);
		return -1;
	} else {
		ast_debug(1, "Using sourceaddress %s for '%s'\n", srcaddr, peer->name);
		return 0;
	}
}

static void peer_destructor(void *obj)
{
	struct iax2_peer *peer = obj;
	int callno = peer->callno;

	ast_free_acl_list(peer->acl);

	if (callno > 0) {
		ast_mutex_lock(&iaxsl[callno]);
		iax2_destroy(callno);
		ast_mutex_unlock(&iaxsl[callno]);
	}

	register_peer_exten(peer, 0);

	if (peer->dnsmgr)
		ast_dnsmgr_release(peer->dnsmgr);

	peer->mwi_event_sub = stasis_unsubscribe(peer->mwi_event_sub);

	ast_string_field_free_memory(peer);

	ast_endpoint_shutdown(peer->endpoint);
}

/*! \brief Create peer structure based on configuration */
static struct iax2_peer *build_peer(const char *name, struct ast_variable *v, struct ast_variable *alt, int temponly)
{
	struct iax2_peer *peer = NULL;
	struct ast_acl_list *oldacl = NULL;
	int maskfound = 0;
	int found = 0;
	int firstpass = 1;
	int subscribe_acl_change = 0;

	if (!temponly) {
		peer = ao2_find(peers, name, OBJ_KEY);
		if (peer && !ast_test_flag64(peer, IAX_DELME))
			firstpass = 0;
	}

	if (peer) {
		found++;
		if (firstpass) {
			oldacl = peer->acl;
			peer->acl = NULL;
		}
		unlink_peer(peer);
	} else if ((peer = ao2_alloc(sizeof(*peer), peer_destructor))) {
		peer->expire = -1;
		peer->pokeexpire = -1;
		peer->sockfd = defaultsockfd;
		if (ast_string_field_init(peer, 32))
			peer = peer_unref(peer);
		if (!(peer->endpoint = ast_endpoint_create("IAX2", name))) {
			peer = peer_unref(peer);
		}
	}

	if (peer) {
		if (firstpass) {
			ast_copy_flags64(peer, &globalflags, IAX_USEJITTERBUF | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE | IAX_FORCE_ENCRYPT);
			peer->encmethods = iax2_encryption;
			peer->adsi = adsi;
			ast_string_field_set(peer, secret, "");
			if (!found) {
				ast_string_field_set(peer, name, name);
				ast_sockaddr_parse(&peer->addr, "0.0.0.0", 0);
				ast_sockaddr_set_port(&peer->addr, IAX_DEFAULT_PORTNO);
				peer->expiry = min_reg_expire;
			}
			peer->prefs = prefs_global;
			peer->capability = iax2_capability;
			peer->smoothing = 0;
			peer->pokefreqok = DEFAULT_FREQ_OK;
			peer->pokefreqnotok = DEFAULT_FREQ_NOTOK;
			peer->maxcallno = 0;
			peercnt_modify((unsigned char) 0, 0, &peer->addr);
			peer->calltoken_required = CALLTOKEN_DEFAULT;
			ast_string_field_set(peer,context,"");
			ast_string_field_set(peer,peercontext,"");
			ast_clear_flag64(peer, IAX_HASCALLERID);
			ast_string_field_set(peer, cid_name, "");
			ast_string_field_set(peer, cid_num, "");
			ast_string_field_set(peer, mohinterpret, mohinterpret);
			ast_string_field_set(peer, mohsuggest, mohsuggest);
		}

		if (!v) {
			v = alt;
			alt = NULL;
		}
		while(v) {
			if (!strcasecmp(v->name, "secret")) {
				ast_string_field_set(peer, secret, v->value);
			} else if (!strcasecmp(v->name, "mailbox")) {
				ast_string_field_set(peer, mailbox, v->value);
			} else if (!strcasecmp(v->name, "hasvoicemail")) {
				if (ast_true(v->value) && ast_strlen_zero(peer->mailbox)) {
					/*
					 * hasvoicemail is a users.conf legacy voicemail enable method.
					 * hasvoicemail is only going to work for app_voicemail mailboxes.
					 */
					if (strchr(name, '@')) {
						ast_string_field_set(peer, mailbox, name);
					} else {
						ast_string_field_build(peer, mailbox, "%s@default", name);
					}
				}
			} else if (!strcasecmp(v->name, "mohinterpret")) {
				ast_string_field_set(peer, mohinterpret, v->value);
			} else if (!strcasecmp(v->name, "mohsuggest")) {
				ast_string_field_set(peer, mohsuggest, v->value);
			} else if (!strcasecmp(v->name, "dbsecret")) {
				ast_string_field_set(peer, dbsecret, v->value);
			} else if (!strcasecmp(v->name, "description")) {
				ast_string_field_set(peer, description, v->value);
			} else if (!strcasecmp(v->name, "trunk")) {
				ast_set2_flag64(peer, ast_true(v->value), IAX_TRUNK);
				if (ast_test_flag64(peer, IAX_TRUNK) && !timer) {
					ast_log(LOG_WARNING, "Unable to support trunking on peer '%s' without a timing interface\n", peer->name);
					ast_clear_flag64(peer, IAX_TRUNK);
				}
			} else if (!strcasecmp(v->name, "auth")) {
				peer->authmethods = get_auth_methods(v->value);
			} else if (!strcasecmp(v->name, "encryption")) {
				peer->encmethods |= get_encrypt_methods(v->value);
				if (!peer->encmethods) {
					ast_clear_flag64(peer, IAX_FORCE_ENCRYPT);
				}
			} else if (!strcasecmp(v->name, "forceencryption")) {
				if (ast_false(v->value)) {
					ast_clear_flag64(peer, IAX_FORCE_ENCRYPT);
				} else {
					peer->encmethods |= get_encrypt_methods(v->value);
					if (peer->encmethods) {
						ast_set_flag64(peer, IAX_FORCE_ENCRYPT);
					}
				}
			} else if (!strcasecmp(v->name, "transfer")) {
				if (!strcasecmp(v->value, "mediaonly")) {
					ast_set_flags_to64(peer, IAX_NOTRANSFER|IAX_TRANSFERMEDIA, IAX_TRANSFERMEDIA);
				} else if (ast_true(v->value)) {
					ast_set_flags_to64(peer, IAX_NOTRANSFER|IAX_TRANSFERMEDIA, 0);
				} else
					ast_set_flags_to64(peer, IAX_NOTRANSFER|IAX_TRANSFERMEDIA, IAX_NOTRANSFER);
			} else if (!strcasecmp(v->name, "jitterbuffer")) {
				ast_set2_flag64(peer, ast_true(v->value), IAX_USEJITTERBUF);
			} else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					/* They'll register with us */
					ast_set_flag64(peer, IAX_DYNAMIC);
					if (!found) {
						int peer_port = ast_sockaddr_port(&peer->addr);
						if (peer_port) {
							ast_sockaddr_set_port(&peer->defaddr, peer_port);
						}
						ast_sockaddr_setnull(&peer->addr);
					}
				} else {
					/* Non-dynamic.  Make sure we become that way if we're not */
					AST_SCHED_DEL(sched, peer->expire);
					ast_clear_flag64(peer, IAX_DYNAMIC);
					peer->addr.ss.ss_family = AST_AF_UNSPEC;
					if (ast_dnsmgr_lookup(v->value, &peer->addr, &peer->dnsmgr, srvlookup ? "_iax._udp" : NULL)) {
						return peer_unref(peer);
					}
					if (!ast_sockaddr_port(&peer->addr)) {
						ast_sockaddr_set_port(&peer->addr, IAX_DEFAULT_PORTNO);
					}
				}
			} else if (!strcasecmp(v->name, "defaultip")) {
				struct ast_sockaddr peer_defaddr_tmp;
				peer_defaddr_tmp.ss.ss_family = AF_UNSPEC;
				if (ast_get_ip(&peer_defaddr_tmp, v->value)) {
					return peer_unref(peer);
				}
				ast_sockaddr_set_port(&peer_defaddr_tmp, ast_sockaddr_port(&peer->defaddr));
				ast_sockaddr_copy(&peer->defaddr, &peer_defaddr_tmp);
			} else if (!strcasecmp(v->name, "sourceaddress")) {
				peer_set_srcaddr(peer, v->value);
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny") ||
					   !strcasecmp(v->name, "acl")) {
				ast_append_acl(v->name, v->value, &peer->acl, NULL, &subscribe_acl_change);
			} else if (!strcasecmp(v->name, "mask")) {
				maskfound++;
				ast_sockaddr_parse(&peer->mask, v->value, 0);
			} else if (!strcasecmp(v->name, "context")) {
				ast_string_field_set(peer, context, v->value);
			} else if (!strcasecmp(v->name, "regexten")) {
				ast_string_field_set(peer, regexten, v->value);
			} else if (!strcasecmp(v->name, "peercontext")) {
				ast_string_field_set(peer, peercontext, v->value);
			} else if (!strcasecmp(v->name, "port")) {
				int bindport;
				if (ast_parse_arg(v->value, PARSE_UINT32 | PARSE_IN_RANGE, &bindport, 0, 65535)) {
					bindport = IAX_DEFAULT_PORTNO;
				}
				if (ast_test_flag64(peer, IAX_DYNAMIC)) {
					ast_sockaddr_set_port(&peer->defaddr, bindport);
				} else {
					ast_sockaddr_set_port(&peer->addr, bindport);
				}
			} else if (!strcasecmp(v->name, "username")) {
				ast_string_field_set(peer, username, v->value);
			} else if (!strcasecmp(v->name, "allow")) {
				iax2_parse_allow_disallow(&peer->prefs, &peer->capability, v->value, 1);
			} else if (!strcasecmp(v->name, "disallow")) {
				iax2_parse_allow_disallow(&peer->prefs, &peer->capability, v->value, 0);
			} else if (!strcasecmp(v->name, "callerid")) {
				if (!ast_strlen_zero(v->value)) {
					char name2[80];
					char num2[80];
					ast_callerid_split(v->value, name2, sizeof(name2), num2, sizeof(num2));
					ast_string_field_set(peer, cid_name, name2);
					ast_string_field_set(peer, cid_num, num2);
				} else {
					ast_string_field_set(peer, cid_name, "");
					ast_string_field_set(peer, cid_num, "");
				}
				ast_set_flag64(peer, IAX_HASCALLERID);
			} else if (!strcasecmp(v->name, "fullname")) {
				ast_string_field_set(peer, cid_name, S_OR(v->value, ""));
				ast_set_flag64(peer, IAX_HASCALLERID);
			} else if (!strcasecmp(v->name, "cid_number")) {
				ast_string_field_set(peer, cid_num, S_OR(v->value, ""));
				ast_set_flag64(peer, IAX_HASCALLERID);
			} else if (!strcasecmp(v->name, "sendani")) {
				ast_set2_flag64(peer, ast_true(v->value), IAX_SENDANI);
			} else if (!strcasecmp(v->name, "inkeys")) {
				ast_string_field_set(peer, inkeys, v->value);
			} else if (!strcasecmp(v->name, "outkey")) {
				ast_string_field_set(peer, outkey, v->value);
			} else if (!strcasecmp(v->name, "qualify")) {
				if (!strcasecmp(v->value, "no")) {
					peer->maxms = 0;
				} else if (!strcasecmp(v->value, "yes")) {
					peer->maxms = DEFAULT_MAXMS;
				} else if (sscanf(v->value, "%30d", &peer->maxms) != 1) {
					ast_log(LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of iax.conf\n", peer->name, v->lineno);
					peer->maxms = 0;
				}
			} else if (!strcasecmp(v->name, "qualifysmoothing")) {
				peer->smoothing = ast_true(v->value);
			} else if (!strcasecmp(v->name, "qualifyfreqok")) {
				if (sscanf(v->value, "%30d", &peer->pokefreqok) != 1) {
					ast_log(LOG_WARNING, "Qualification testing frequency of peer '%s' when OK should a number of milliseconds at line %d of iax.conf\n", peer->name, v->lineno);
				}
			} else if (!strcasecmp(v->name, "qualifyfreqnotok")) {
				if (sscanf(v->value, "%30d", &peer->pokefreqnotok) != 1) {
					ast_log(LOG_WARNING, "Qualification testing frequency of peer '%s' when NOT OK should be a number of milliseconds at line %d of iax.conf\n", peer->name, v->lineno);
				}
			} else if (!strcasecmp(v->name, "timezone")) {
				ast_string_field_set(peer, zonetag, v->value);
			} else if (!strcasecmp(v->name, "adsi")) {
				peer->adsi = ast_true(v->value);
			} else if (!strcasecmp(v->name, "connectedline")) {
				if (ast_true(v->value)) {
					ast_set_flag64(peer, IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
				} else if (!strcasecmp(v->value, "send")) {
					ast_clear_flag64(peer, IAX_RECVCONNECTEDLINE);
					ast_set_flag64(peer, IAX_SENDCONNECTEDLINE);
				} else if (!strcasecmp(v->value, "receive")) {
					ast_clear_flag64(peer, IAX_SENDCONNECTEDLINE);
					ast_set_flag64(peer, IAX_RECVCONNECTEDLINE);
				} else {
					ast_clear_flag64(peer, IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
				}
			} else if (!strcasecmp(v->name, "maxcallnumbers")) {
				if (sscanf(v->value, "%10hu", &peer->maxcallno) != 1) {
					ast_log(LOG_WARNING, "maxcallnumbers must be set to a valid number. %s is not valid at line %d.\n", v->value, v->lineno);
				} else {
					peercnt_modify((unsigned char) 1, peer->maxcallno, &peer->addr);
				}
			} else if (!strcasecmp(v->name, "requirecalltoken")) {
				/* default is required unless in optional ip list */
				if (ast_false(v->value)) {
					peer->calltoken_required = CALLTOKEN_NO;
				} else if (!strcasecmp(v->value, "auto")) {
					peer->calltoken_required = CALLTOKEN_AUTO;
				} else if (ast_true(v->value)) {
					peer->calltoken_required = CALLTOKEN_YES;
				} else {
					ast_log(LOG_WARNING, "requirecalltoken must be set to a valid value. at line %d\n", v->lineno);
				}
			} /* else if (strcasecmp(v->name,"type")) */
			/*	ast_log(LOG_WARNING, "Ignoring %s\n", v->name); */
			v = v->next;
			if (!v) {
				v = alt;
				alt = NULL;
			}
		}
		if (!peer->authmethods)
			peer->authmethods = IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT;
		ast_clear_flag64(peer, IAX_DELME);
	}

	if (!maskfound && !ast_sockaddr_isnull(&peer->addr)) {
		if (ast_sockaddr_is_ipv4_mapped(&peer->addr)) {
			ast_sockaddr_parse(&peer->mask, "::ffff:ffff:ffff", 0);
		} else if (ast_sockaddr_is_ipv6(&peer->addr)) {
			ast_sockaddr_parse(&peer->mask, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", 0);
		} else {
			ast_sockaddr_parse(&peer->mask, "255.255.255.255", 0);
		}
	}

	if (oldacl) {
		ast_free_acl_list(oldacl);
	}

	if (!ast_strlen_zero(peer->mailbox)) {
		struct stasis_topic *mailbox_specific_topic;

		mailbox_specific_topic = ast_mwi_topic(peer->mailbox);
		if (mailbox_specific_topic) {
			peer->mwi_event_sub = stasis_subscribe_pool(mailbox_specific_topic, mwi_event_cb, NULL);
		}
	}

	if (subscribe_acl_change) {
		acl_change_stasis_subscribe();
	}

	return peer;
}

static void user_destructor(void *obj)
{
	struct iax2_user *user = obj;

	ast_free_acl_list(user->acl);
	free_context(user->contexts);
	if(user->vars) {
		ast_variables_destroy(user->vars);
		user->vars = NULL;
	}
	ast_string_field_free_memory(user);
}

/*! \brief Create in-memory user structure from configuration */
static struct iax2_user *build_user(const char *name, struct ast_variable *v, struct ast_variable *alt, int temponly)
{
	struct iax2_user *user = NULL;
	struct iax2_context *con, *conl = NULL;
	struct ast_acl_list *oldacl = NULL;
	struct iax2_context *oldcon = NULL;
	int format;
	int firstpass=1;
	int oldcurauthreq = 0;
	int subscribe_acl_change = 0;
	char *varname = NULL, *varval = NULL;
	struct ast_variable *tmpvar = NULL;

	if (!temponly) {
		user = ao2_find(users, name, OBJ_KEY);
		if (user && !ast_test_flag64(user, IAX_DELME))
			firstpass = 0;
	}

	if (user) {
		if (firstpass) {
			oldcurauthreq = user->curauthreq;
			oldacl = user->acl;
			oldcon = user->contexts;
			user->acl = NULL;
			user->contexts = NULL;
		}
		/* Already in the list, remove it and it will be added back (or FREE'd) */
		ao2_unlink(users, user);
 	} else {
		user = ao2_alloc(sizeof(*user), user_destructor);
	}

	if (user) {
		if (firstpass) {
			ast_string_field_free_memory(user);
			memset(user, 0, sizeof(struct iax2_user));
			if (ast_string_field_init(user, 32)) {
				user = user_unref(user);
				goto cleanup;
			}
			user->maxauthreq = maxauthreq;
			user->curauthreq = oldcurauthreq;
			user->prefs = prefs_global;
			user->capability = iax2_capability;
			user->encmethods = iax2_encryption;
			user->adsi = adsi;
			user->calltoken_required = CALLTOKEN_DEFAULT;
			ast_string_field_set(user, name, name);
			ast_string_field_set(user, language, language);
			ast_copy_flags64(user, &globalflags, IAX_USEJITTERBUF | IAX_CODEC_USER_FIRST | IAX_CODEC_NOPREFS | IAX_CODEC_NOCAP | IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE | IAX_FORCE_ENCRYPT);
			ast_clear_flag64(user, IAX_HASCALLERID);
			ast_string_field_set(user, cid_name, "");
			ast_string_field_set(user, cid_num, "");
			ast_string_field_set(user, accountcode, accountcode);
			ast_string_field_set(user, mohinterpret, mohinterpret);
			ast_string_field_set(user, mohsuggest, mohsuggest);
		}
		if (!v) {
			v = alt;
			alt = NULL;
		}
		while(v) {
			if (!strcasecmp(v->name, "context")) {
				con = build_context(v->value);
				if (con) {
					if (conl)
						conl->next = con;
					else
						user->contexts = con;
					conl = con;
				}
			} else if (!strcasecmp(v->name, "permit") ||
					   !strcasecmp(v->name, "deny") ||
					   !strcasecmp(v->name, "acl")) {
				ast_append_acl(v->name, v->value, &user->acl, NULL, &subscribe_acl_change);
			} else if (!strcasecmp(v->name, "setvar")) {
				varname = ast_strdupa(v->value);
				if ((varval = strchr(varname, '='))) {
					*varval = '\0';
					varval++;
					if((tmpvar = ast_variable_new(varname, varval, ""))) {
						tmpvar->next = user->vars;
						user->vars = tmpvar;
					}
				}
			} else if (!strcasecmp(v->name, "allow")) {
				iax2_parse_allow_disallow(&user->prefs, &user->capability, v->value, 1);
			} else if (!strcasecmp(v->name, "disallow")) {
				iax2_parse_allow_disallow(&user->prefs, &user->capability,v->value, 0);
			} else if (!strcasecmp(v->name, "trunk")) {
				ast_set2_flag64(user, ast_true(v->value), IAX_TRUNK);
				if (ast_test_flag64(user, IAX_TRUNK) && !timer) {
					ast_log(LOG_WARNING, "Unable to support trunking on user '%s' without a timing interface\n", user->name);
					ast_clear_flag64(user, IAX_TRUNK);
				}
			} else if (!strcasecmp(v->name, "auth")) {
				user->authmethods = get_auth_methods(v->value);
			} else if (!strcasecmp(v->name, "encryption")) {
				user->encmethods |= get_encrypt_methods(v->value);
				if (!user->encmethods) {
					ast_clear_flag64(user, IAX_FORCE_ENCRYPT);
				}
			} else if (!strcasecmp(v->name, "forceencryption")) {
				if (ast_false(v->value)) {
					ast_clear_flag64(user, IAX_FORCE_ENCRYPT);
				} else {
					user->encmethods |= get_encrypt_methods(v->value);
					if (user->encmethods) {
						ast_set_flag64(user, IAX_FORCE_ENCRYPT);
					}
				}
			} else if (!strcasecmp(v->name, "transfer")) {
				if (!strcasecmp(v->value, "mediaonly")) {
					ast_set_flags_to64(user, IAX_NOTRANSFER|IAX_TRANSFERMEDIA, IAX_TRANSFERMEDIA);
				} else if (ast_true(v->value)) {
					ast_set_flags_to64(user, IAX_NOTRANSFER|IAX_TRANSFERMEDIA, 0);
				} else
					ast_set_flags_to64(user, IAX_NOTRANSFER|IAX_TRANSFERMEDIA, IAX_NOTRANSFER);
			} else if (!strcasecmp(v->name, "codecpriority")) {
				if(!strcasecmp(v->value, "caller"))
					ast_set_flag64(user, IAX_CODEC_USER_FIRST);
				else if(!strcasecmp(v->value, "disabled"))
					ast_set_flag64(user, IAX_CODEC_NOPREFS);
				else if(!strcasecmp(v->value, "reqonly")) {
					ast_set_flag64(user, IAX_CODEC_NOCAP);
					ast_set_flag64(user, IAX_CODEC_NOPREFS);
				}
			} else if (!strcasecmp(v->name, "immediate")) {
				ast_set2_flag64(user, ast_true(v->value), IAX_IMMEDIATE);
			} else if (!strcasecmp(v->name, "jitterbuffer")) {
				ast_set2_flag64(user, ast_true(v->value), IAX_USEJITTERBUF);
			} else if (!strcasecmp(v->name, "dbsecret")) {
				ast_string_field_set(user, dbsecret, v->value);
			} else if (!strcasecmp(v->name, "secret")) {
				if (!ast_strlen_zero(user->secret)) {
					char *old = ast_strdupa(user->secret);

					ast_string_field_build(user, secret, "%s;%s", old, v->value);
				} else
					ast_string_field_set(user, secret, v->value);
			} else if (!strcasecmp(v->name, "callerid")) {
				if (!ast_strlen_zero(v->value) && strcasecmp(v->value, "asreceived")) {
					char name2[80];
					char num2[80];
					ast_callerid_split(v->value, name2, sizeof(name2), num2, sizeof(num2));
					ast_string_field_set(user, cid_name, name2);
					ast_string_field_set(user, cid_num, num2);
					ast_set_flag64(user, IAX_HASCALLERID);
				} else {
					ast_clear_flag64(user, IAX_HASCALLERID);
					ast_string_field_set(user, cid_name, "");
					ast_string_field_set(user, cid_num, "");
				}
			} else if (!strcasecmp(v->name, "fullname")) {
				if (!ast_strlen_zero(v->value)) {
					ast_string_field_set(user, cid_name, v->value);
					ast_set_flag64(user, IAX_HASCALLERID);
				} else {
					ast_string_field_set(user, cid_name, "");
					if (ast_strlen_zero(user->cid_num))
						ast_clear_flag64(user, IAX_HASCALLERID);
				}
			} else if (!strcasecmp(v->name, "cid_number")) {
				if (!ast_strlen_zero(v->value)) {
					ast_string_field_set(user, cid_num, v->value);
					ast_set_flag64(user, IAX_HASCALLERID);
				} else {
					ast_string_field_set(user, cid_num, "");
					if (ast_strlen_zero(user->cid_name))
						ast_clear_flag64(user, IAX_HASCALLERID);
				}
			} else if (!strcasecmp(v->name, "accountcode")) {
				ast_string_field_set(user, accountcode, v->value);
			} else if (!strcasecmp(v->name, "mohinterpret")) {
				ast_string_field_set(user, mohinterpret, v->value);
			} else if (!strcasecmp(v->name, "mohsuggest")) {
				ast_string_field_set(user, mohsuggest, v->value);
			} else if (!strcasecmp(v->name, "parkinglot")) {
				ast_string_field_set(user, parkinglot, v->value);
			} else if (!strcasecmp(v->name, "language")) {
				ast_string_field_set(user, language, v->value);
			} else if (!strcasecmp(v->name, "amaflags")) {
				format = ast_channel_string2amaflag(v->value);
				if (format < 0) {
					ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
				} else {
					user->amaflags = format;
				}
			} else if (!strcasecmp(v->name, "inkeys")) {
				ast_string_field_set(user, inkeys, v->value);
			} else if (!strcasecmp(v->name, "maxauthreq")) {
				user->maxauthreq = atoi(v->value);
				if (user->maxauthreq < 0)
					user->maxauthreq = 0;
			} else if (!strcasecmp(v->name, "adsi")) {
				user->adsi = ast_true(v->value);
			} else if (!strcasecmp(v->name, "connectedline")) {
				if (ast_true(v->value)) {
					ast_set_flag64(user, IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
				} else if (!strcasecmp(v->value, "send")) {
					ast_clear_flag64(user, IAX_RECVCONNECTEDLINE);
					ast_set_flag64(user, IAX_SENDCONNECTEDLINE);
				} else if (!strcasecmp(v->value, "receive")) {
					ast_clear_flag64(user, IAX_SENDCONNECTEDLINE);
					ast_set_flag64(user, IAX_RECVCONNECTEDLINE);
				} else {
					ast_clear_flag64(user, IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
				}
			} else if (!strcasecmp(v->name, "requirecalltoken")) {
				/* default is required unless in optional ip list */
				if (ast_false(v->value)) {
					user->calltoken_required = CALLTOKEN_NO;
				} else if (!strcasecmp(v->value, "auto")) {
					user->calltoken_required = CALLTOKEN_AUTO;
				} else if (ast_true(v->value)) {
					user->calltoken_required = CALLTOKEN_YES;
				} else {
					ast_log(LOG_WARNING, "requirecalltoken must be set to a valid value. at line %d\n", v->lineno);
				}
			} /* else if (strcasecmp(v->name,"type")) */
			/*	ast_log(LOG_WARNING, "Ignoring %s\n", v->name); */
			v = v->next;
			if (!v) {
				v = alt;
				alt = NULL;
			}
		}
		if (!user->authmethods) {
			if (!ast_strlen_zero(user->secret)) {
				user->authmethods = IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT;
				if (!ast_strlen_zero(user->inkeys))
					user->authmethods |= IAX_AUTH_RSA;
			} else if (!ast_strlen_zero(user->inkeys)) {
				user->authmethods = IAX_AUTH_RSA;
			} else {
				user->authmethods = IAX_AUTH_MD5 | IAX_AUTH_PLAINTEXT;
			}
		}
		ast_clear_flag64(user, IAX_DELME);
	}
cleanup:
	if (oldacl) {
		ast_free_acl_list(oldacl);
	}
	if (oldcon) {
		free_context(oldcon);
	}

	if (subscribe_acl_change) {
		acl_change_stasis_subscribe();
	}

	return user;
}

static int peer_delme_cb(void *obj, void *arg, int flags)
{
	struct iax2_peer *peer = obj;

	ast_set_flag64(peer, IAX_DELME);

	return 0;
}

static int user_delme_cb(void *obj, void *arg, int flags)
{
	struct iax2_user *user = obj;

	ast_set_flag64(user, IAX_DELME);

	return 0;
}

static void delete_users(void)
{
	struct iax2_registry *reg;

	ao2_callback(users, OBJ_NODATA, user_delme_cb, NULL);

	AST_LIST_LOCK(&registrations);
	while ((reg = AST_LIST_REMOVE_HEAD(&registrations, entry))) {
		if (sched) {
			AST_SCHED_DEL(sched, reg->expire);
		}
		if (reg->callno) {
			int callno = reg->callno;
			ast_mutex_lock(&iaxsl[callno]);
			if (iaxs[callno]) {
				iaxs[callno]->reg = NULL;
				iax2_destroy(callno);
			}
			ast_mutex_unlock(&iaxsl[callno]);
		}
		if (reg->dnsmgr)
			ast_dnsmgr_release(reg->dnsmgr);
		ast_free(reg);
	}
	AST_LIST_UNLOCK(&registrations);

	ao2_callback(peers, OBJ_NODATA, peer_delme_cb, NULL);
}

static void prune_users(void)
{
	struct iax2_user *user;
	struct ao2_iterator i;

	i = ao2_iterator_init(users, 0);
	while ((user = ao2_iterator_next(&i))) {
		if (ast_test_flag64(user, IAX_DELME) || ast_test_flag64(user, IAX_RTCACHEFRIENDS)) {
			ao2_unlink(users, user);
		}
		user_unref(user);
	}
	ao2_iterator_destroy(&i);
}

/* Prune peers who still are supposed to be deleted */
static void prune_peers(void)
{
	struct iax2_peer *peer;
	struct ao2_iterator i;

	i = ao2_iterator_init(peers, 0);
	while ((peer = ao2_iterator_next(&i))) {
		if (ast_test_flag64(peer, IAX_DELME) || ast_test_flag64(peer, IAX_RTCACHEFRIENDS)) {
			unlink_peer(peer);
		}
		peer_unref(peer);
	}
	ao2_iterator_destroy(&i);
}

static void set_config_destroy(void)
{
	strcpy(accountcode, "");
	strcpy(language, "");
	strcpy(mohinterpret, "");
	strcpy(mohsuggest, "");
	trunkmaxsize = MAX_TRUNKDATA;
	amaflags = 0;
	delayreject = 0;
	ast_clear_flag64((&globalflags), IAX_NOTRANSFER | IAX_TRANSFERMEDIA | IAX_USEJITTERBUF |
		IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
	delete_users();
	ao2_callback(callno_limits, OBJ_NODATA, addr_range_delme_cb, NULL);
	ao2_callback(calltoken_ignores, OBJ_NODATA, addr_range_delme_cb, NULL);
}

/*! \brief Load configuration */
static int set_config(const char *config_file, int reload, int forced)
{
	struct ast_config *cfg, *ucfg;
	iax2_format capability;
	struct ast_variable *v;
	char *cat;
	const char *utype;
	const char *tosval;
	int format;
	int portno = IAX_DEFAULT_PORTNO;
	int  x;
	int mtuv;
	int subscribe_network_change = 1;
	struct iax2_user *user;
	struct iax2_peer *peer;
	struct ast_netsock *ns;
	struct ast_flags config_flags = { (reload && !forced) ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_sockaddr bindaddr;
	struct iax2_codec_pref prefs_new;

	cfg = ast_config_load(config_file, config_flags);

	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config_file);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED)
			return 0;
		/* Otherwise we need to reread both files */
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		if ((cfg = ast_config_load(config_file, config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", config_file);
			ast_config_destroy(ucfg);
			return 0;
		}
		if (!cfg) {
			/* should have been able to load the config here */
			ast_log(LOG_ERROR, "Unable to load config %s again\n", config_file);
			return -1;
		}
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", config_file);
		return 0;
	} else { /* iax.conf changed, gotta reread users.conf, too */
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		if ((ucfg = ast_config_load("users.conf", config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "Config file users.conf is in an invalid format.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
	}

	if (reload) {
		set_config_destroy();
	}

	ast_sockaddr_parse(&bindaddr, "0.0.0.0:0", 0);

	/* Setup new codec prefs */
	capability = iax2_codec_pref_from_bitfield(&prefs_new, IAX_CAPABILITY_FULLBANDWIDTH);

	/* Reset Global Flags */
	memset(&globalflags, 0, sizeof(globalflags));
	ast_set_flag64(&globalflags, IAX_RTUPDATE);
	ast_set_flag64((&globalflags), IAX_SHRINKCALLERID);

#ifdef SO_NO_CHECK
	nochecksums = 0;
#endif
	/* Reset default parking lot */
	default_parkinglot[0] = '\0';

	min_reg_expire = IAX_DEFAULT_REG_EXPIRE;
	max_reg_expire = IAX_DEFAULT_REG_EXPIRE;
	global_max_trunk_mtu = MAX_TRUNK_MTU;
	global_maxcallno = DEFAULT_MAXCALLNO_LIMIT;
	global_maxcallno_nonval = DEFAULT_MAXCALLNO_LIMIT_NONVAL;

	maxauthreq = 3;

	srvlookup = 0;

	v = ast_variable_browse(cfg, "general");

	/* Seed initial tos value */
	tosval = ast_variable_retrieve(cfg, "general", "tos");
	if (tosval) {
		if (ast_str2tos(tosval, &qos.tos))
			ast_log(LOG_WARNING, "Invalid tos value, refer to QoS documentation\n");
	}
	/* Seed initial cos value */
	tosval = ast_variable_retrieve(cfg, "general", "cos");
	if (tosval) {
		if (ast_str2cos(tosval, &qos.cos))
			ast_log(LOG_WARNING, "Invalid cos value, refer to QoS documentation\n");
	}
	while(v) {
		if (!strcasecmp(v->name, "bindport")) {
			if (reload) {
				ast_log(LOG_NOTICE, "Ignoring bindport on reload\n");
			}
			else if (ast_parse_arg(v->value, PARSE_UINT32 | PARSE_IN_RANGE, &portno, 1024, 65535)) {
				portno = IAX_DEFAULT_PORTNO;
			}
		} else if (!strcasecmp(v->name, "pingtime")){
			ping_time = atoi(v->value);
		}
		else if (!strcasecmp(v->name, "iaxthreadcount")) {
			if (reload) {
				if (atoi(v->value) != iaxthreadcount)
					ast_log(LOG_NOTICE, "Ignoring any changes to iaxthreadcount during reload\n");
			} else {
				iaxthreadcount = atoi(v->value);
				if (iaxthreadcount < 1) {
					ast_log(LOG_NOTICE, "iaxthreadcount must be at least 1.\n");
					iaxthreadcount = 1;
				} else if (iaxthreadcount > 256) {
					ast_log(LOG_NOTICE, "limiting iaxthreadcount to 256\n");
					iaxthreadcount = 256;
				}
			}
		} else if (!strcasecmp(v->name, "iaxmaxthreadcount")) {
			if (reload) {
				AST_LIST_LOCK(&dynamic_list);
				iaxmaxthreadcount = atoi(v->value);
				AST_LIST_UNLOCK(&dynamic_list);
			} else {
				iaxmaxthreadcount = atoi(v->value);
				if (iaxmaxthreadcount < 0) {
					ast_log(LOG_NOTICE, "iaxmaxthreadcount must be at least 0.\n");
					iaxmaxthreadcount = 0;
				} else if (iaxmaxthreadcount > 256) {
					ast_log(LOG_NOTICE, "Limiting iaxmaxthreadcount to 256\n");
					iaxmaxthreadcount = 256;
				}
			}
		} else if (!strcasecmp(v->name, "nochecksums")) {
#ifdef SO_NO_CHECK
			if (ast_true(v->value))
				nochecksums = 1;
			else
				nochecksums = 0;
#else
			if (ast_true(v->value))
				ast_log(LOG_WARNING, "Disabling RTP checksums is not supported on this operating system!\n");
#endif
		}
		else if (!strcasecmp(v->name, "maxjitterbuffer"))
			maxjitterbuffer = atoi(v->value);
		else if (!strcasecmp(v->name, "resyncthreshold"))
			resyncthreshold = atoi(v->value);
		else if (!strcasecmp(v->name, "maxjitterinterps"))
			maxjitterinterps = atoi(v->value);
		else if (!strcasecmp(v->name, "jittertargetextra"))
			jittertargetextra = atoi(v->value);
		else if (!strcasecmp(v->name, "lagrqtime"))
			lagrq_time = atoi(v->value);
		else if (!strcasecmp(v->name, "maxregexpire"))
			max_reg_expire = atoi(v->value);
		else if (!strcasecmp(v->name, "minregexpire"))
			min_reg_expire = atoi(v->value);
		else if (!strcasecmp(v->name, "bindaddr")) {
			if (reload) {
				ast_log(LOG_NOTICE, "Ignoring bindaddr on reload\n");
			} else {

				if (!ast_parse_arg(v->value, PARSE_ADDR, NULL)) {

					ast_sockaddr_parse(&bindaddr, v->value, 0);

					if (!ast_sockaddr_port(&bindaddr)) {
						ast_sockaddr_set_port(&bindaddr, portno);
					}

					if (!(ns = ast_netsock_bindaddr(netsock, io, &bindaddr, qos.tos, qos.cos, socket_read, NULL))) {
						ast_log(LOG_WARNING, "Unable to apply binding to '%s' at line %d\n", v->value, v->lineno);
					} else {
						ast_verb(2, "Binding IAX2 to address %s\n", ast_sockaddr_stringify(&bindaddr));

						if (defaultsockfd < 0) {
							defaultsockfd = ast_netsock_sockfd(ns);
						}
						ast_netsock_unref(ns);
					}

				} else {
					ast_log(LOG_WARNING, "Invalid address '%s' specified, at line %d\n", v->value, v->lineno);
				}
			}
		} else if (!strcasecmp(v->name, "authdebug")) {
			authdebug = ast_true(v->value);
		} else if (!strcasecmp(v->name, "encryption")) {
				iax2_encryption |= get_encrypt_methods(v->value);
				if (!iax2_encryption) {
					ast_clear_flag64((&globalflags), IAX_FORCE_ENCRYPT);
				}
		} else if (!strcasecmp(v->name, "forceencryption")) {
			if (ast_false(v->value)) {
				ast_clear_flag64((&globalflags), IAX_FORCE_ENCRYPT);
			} else {
				iax2_encryption |= get_encrypt_methods(v->value);
				if (iax2_encryption) {
					ast_set_flag64((&globalflags), IAX_FORCE_ENCRYPT);
				}
			}
		} else if (!strcasecmp(v->name, "transfer")) {
			if (!strcasecmp(v->value, "mediaonly")) {
				ast_set_flags_to64((&globalflags), IAX_NOTRANSFER|IAX_TRANSFERMEDIA, IAX_TRANSFERMEDIA);
			} else if (ast_true(v->value)) {
				ast_set_flags_to64((&globalflags), IAX_NOTRANSFER|IAX_TRANSFERMEDIA, 0);
			} else
				ast_set_flags_to64((&globalflags), IAX_NOTRANSFER|IAX_TRANSFERMEDIA, IAX_NOTRANSFER);
		} else if (!strcasecmp(v->name, "codecpriority")) {
			if(!strcasecmp(v->value, "caller"))
				ast_set_flag64((&globalflags), IAX_CODEC_USER_FIRST);
			else if(!strcasecmp(v->value, "disabled"))
				ast_set_flag64((&globalflags), IAX_CODEC_NOPREFS);
			else if(!strcasecmp(v->value, "reqonly")) {
				ast_set_flag64((&globalflags), IAX_CODEC_NOCAP);
				ast_set_flag64((&globalflags), IAX_CODEC_NOPREFS);
			}
		} else if (!strcasecmp(v->name, "jitterbuffer"))
			ast_set2_flag64((&globalflags), ast_true(v->value), IAX_USEJITTERBUF);
		else if (!strcasecmp(v->name, "delayreject"))
			delayreject = ast_true(v->value);
		else if (!strcasecmp(v->name, "allowfwdownload"))
			ast_set2_flag64((&globalflags), ast_true(v->value), IAX_ALLOWFWDOWNLOAD);
		else if (!strcasecmp(v->name, "rtcachefriends"))
			ast_set2_flag64((&globalflags), ast_true(v->value), IAX_RTCACHEFRIENDS);
		else if (!strcasecmp(v->name, "rtignoreregexpire"))
			ast_set2_flag64((&globalflags), ast_true(v->value), IAX_RTIGNOREREGEXPIRE);
		else if (!strcasecmp(v->name, "rtupdate"))
			ast_set2_flag64((&globalflags), ast_true(v->value), IAX_RTUPDATE);
		else if (!strcasecmp(v->name, "rtsavesysname"))
			ast_set2_flag64((&globalflags), ast_true(v->value), IAX_RTSAVE_SYSNAME);
		else if (!strcasecmp(v->name, "trunktimestamps"))
			ast_set2_flag64(&globalflags, ast_true(v->value), IAX_TRUNKTIMESTAMPS);
		else if (!strcasecmp(v->name, "rtautoclear")) {
			int i = atoi(v->value);
			if(i > 0)
				global_rtautoclear = i;
			else
				i = 0;
			ast_set2_flag64((&globalflags), i || ast_true(v->value), IAX_RTAUTOCLEAR);
		} else if (!strcasecmp(v->name, "trunkfreq")) {
			trunkfreq = atoi(v->value);
			if (trunkfreq < 10) {
				ast_log(LOG_NOTICE, "trunkfreq must be between 10ms and 1000ms, using 10ms instead.\n");
				trunkfreq = 10;
			} else if (trunkfreq > 1000) {
				ast_log(LOG_NOTICE, "trunkfreq must be between 10ms and 1000ms, using 1000ms instead.\n");
				trunkfreq = 1000;
			}
			if (timer) {
				ast_timer_set_rate(timer, 1000 / trunkfreq);
			}
		} else if (!strcasecmp(v->name, "trunkmtu")) {
			mtuv = atoi(v->value);
			if (mtuv  == 0 )
				global_max_trunk_mtu = 0;
			else if (mtuv >= 172 && mtuv < 4000)
				global_max_trunk_mtu = mtuv;
			else
				ast_log(LOG_NOTICE, "trunkmtu value out of bounds (%d) at line %d\n",
					mtuv, v->lineno);
		} else if (!strcasecmp(v->name, "trunkmaxsize")) {
			trunkmaxsize = atoi(v->value);
			if (trunkmaxsize == 0)
				trunkmaxsize = MAX_TRUNKDATA;
		} else if (!strcasecmp(v->name, "autokill")) {
			if (sscanf(v->value, "%30d", &x) == 1) {
				if (x >= 0)
					autokill = x;
				else
					ast_log(LOG_NOTICE, "Nice try, but autokill has to be >0 or 'yes' or 'no' at line %d\n", v->lineno);
			} else if (ast_true(v->value)) {
				autokill = DEFAULT_MAXMS;
			} else {
				autokill = 0;
			}
		} else if (!strcasecmp(v->name, "bandwidth")) {
			if (!strcasecmp(v->value, "low")) {
				capability = iax2_codec_pref_from_bitfield(&prefs_new,
					IAX_CAPABILITY_LOWBANDWIDTH);
			} else if (!strcasecmp(v->value, "medium")) {
				capability = iax2_codec_pref_from_bitfield(&prefs_new,
					IAX_CAPABILITY_MEDBANDWIDTH);
			} else if (!strcasecmp(v->value, "high")) {
				capability = iax2_codec_pref_from_bitfield(&prefs_new,
					IAX_CAPABILITY_FULLBANDWIDTH);
			} else {
				ast_log(LOG_WARNING, "bandwidth must be either low, medium, or high\n");
			}
		} else if (!strcasecmp(v->name, "allow")) {
			iax2_parse_allow_disallow(&prefs_new, &capability, v->value, 1);
		} else if (!strcasecmp(v->name, "disallow")) {
			iax2_parse_allow_disallow(&prefs_new, &capability, v->value, 0);
		} else if (!strcasecmp(v->name, "register")) {
			iax2_register(v->value, v->lineno);
		} else if (!strcasecmp(v->name, "iaxcompat")) {
			iaxcompat = ast_true(v->value);
		} else if (!strcasecmp(v->name, "regcontext")) {
			ast_copy_string(regcontext, v->value, sizeof(regcontext));
			/* Create context if it doesn't exist already */
			ast_context_find_or_create(NULL, NULL, regcontext, "IAX2");
		} else if (!strcasecmp(v->name, "tos")) {
			if (ast_str2tos(v->value, &qos.tos))
				ast_log(LOG_WARNING, "Invalid tos value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "cos")) {
			if (ast_str2cos(v->value, &qos.cos))
				ast_log(LOG_WARNING, "Invalid cos value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "parkinglot")) {
			ast_copy_string(default_parkinglot, v->value, sizeof(default_parkinglot));
		} else if (!strcasecmp(v->name, "accountcode")) {
			ast_copy_string(accountcode, v->value, sizeof(accountcode));
		} else if (!strcasecmp(v->name, "mohinterpret")) {
			ast_copy_string(mohinterpret, v->value, sizeof(mohinterpret));
		} else if (!strcasecmp(v->name, "mohsuggest")) {
			ast_copy_string(mohsuggest, v->value, sizeof(mohsuggest));
		} else if (!strcasecmp(v->name, "amaflags")) {
			format = ast_channel_string2amaflag(v->value);
			if (format < 0) {
				ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
			} else {
				amaflags = format;
			}
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(language, v->value, sizeof(language));
		} else if (!strcasecmp(v->name, "maxauthreq")) {
			maxauthreq = atoi(v->value);
			if (maxauthreq < 0)
				maxauthreq = 0;
		} else if (!strcasecmp(v->name, "adsi")) {
			adsi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "srvlookup")) {
			srvlookup = ast_true(v->value);
		} else if (!strcasecmp(v->name, "connectedline")) {
			if (ast_true(v->value)) {
				ast_set_flag64((&globalflags), IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
			} else if (!strcasecmp(v->value, "send")) {
				ast_clear_flag64((&globalflags), IAX_RECVCONNECTEDLINE);
				ast_set_flag64((&globalflags), IAX_SENDCONNECTEDLINE);
			} else if (!strcasecmp(v->value, "receive")) {
				ast_clear_flag64((&globalflags), IAX_SENDCONNECTEDLINE);
				ast_set_flag64((&globalflags), IAX_RECVCONNECTEDLINE);
			} else {
				ast_clear_flag64((&globalflags), IAX_SENDCONNECTEDLINE | IAX_RECVCONNECTEDLINE);
			}
		} else if (!strcasecmp(v->name, "maxcallnumbers")) {
			if (sscanf(v->value, "%10hu", &global_maxcallno) != 1) {
				ast_log(LOG_WARNING, "maxcallnumbers must be set to a valid number.  %s is not valid at line %d\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "maxcallnumbers_nonvalidated")) {
			if (sscanf(v->value, "%10hu", &global_maxcallno_nonval) != 1) {
				ast_log(LOG_WARNING, "maxcallnumbers_nonvalidated must be set to a valid number.  %s is not valid at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "calltokenoptional")) {
			if (add_calltoken_ignore(v->value)) {
				ast_log(LOG_WARNING, "Invalid calltokenoptional address range - '%s' line %d\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "calltokenexpiration")) {
			int temp = -1;
			sscanf(v->value, "%u", &temp);
			if( temp <= 0 ){
				ast_log(LOG_WARNING, "Invalid calltokenexpiration value %s. Should be integer greater than 0.\n", v->value);
			} else {
				max_calltoken_delay = temp;
			}
		}  else if (!strcasecmp(v->name, "subscribe_network_change_event")) {
			if (ast_true(v->value)) {
				subscribe_network_change = 1;
			} else if (ast_false(v->value)) {
				subscribe_network_change = 0;
			} else {
				ast_log(LOG_WARNING, "subscribe_network_change_event value %s is not valid at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "shrinkcallerid")) {
			if (ast_true(v->value)) {
				ast_set_flag64((&globalflags), IAX_SHRINKCALLERID);
			} else if (ast_false(v->value)) {
				ast_clear_flag64((&globalflags), IAX_SHRINKCALLERID);
			} else {
				ast_log(LOG_WARNING, "shrinkcallerid value %s is not valid at line %d.\n", v->value, v->lineno);
			}
		}/*else if (strcasecmp(v->name,"type")) */
		/*	ast_log(LOG_WARNING, "Ignoring %s\n", v->name); */
		v = v->next;
	}

	if (subscribe_network_change) {
		network_change_stasis_subscribe();
	} else {
		network_change_stasis_unsubscribe();
	}

	if (defaultsockfd < 0) {

		ast_sockaddr_set_port(&bindaddr, portno);

		if (!(ns = ast_netsock_bindaddr(netsock, io, &bindaddr, qos.tos, qos.cos, socket_read, NULL))) {
			ast_log(LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		} else {
			ast_verb(2, "Binding IAX2 to default address %s\n", ast_sockaddr_stringify(&bindaddr));
			defaultsockfd = ast_netsock_sockfd(ns);
			ast_netsock_unref(ns);
		}
	}
	if (reload) {
		ast_netsock_release(outsock);
		outsock = ast_netsock_list_alloc();
		if (!outsock) {
			ast_log(LOG_ERROR, "Could not allocate outsock list.\n");
			return -1;
		}
		ast_netsock_init(outsock);
	}

	if (min_reg_expire > max_reg_expire) {
		ast_log(LOG_WARNING, "Minimum registration interval of %d is more than maximum of %d, resetting minimum to %d\n",
			min_reg_expire, max_reg_expire, max_reg_expire);
		min_reg_expire = max_reg_expire;
	}
	prefs_global = prefs_new;
	iax2_capability = capability;

	if (ucfg) {
		struct ast_variable *gen;
		int genhasiax;
		int genregisteriax;
		const char *hasiax, *registeriax;

		genhasiax = ast_true(ast_variable_retrieve(ucfg, "general", "hasiax"));
		genregisteriax = ast_true(ast_variable_retrieve(ucfg, "general", "registeriax"));
		gen = ast_variable_browse(ucfg, "general");
		cat = ast_category_browse(ucfg, NULL);
		while (cat) {
			if (strcasecmp(cat, "general")) {
				hasiax = ast_variable_retrieve(ucfg, cat, "hasiax");
				registeriax = ast_variable_retrieve(ucfg, cat, "registeriax");
				if (ast_true(hasiax) || (!hasiax && genhasiax)) {
					/* Start with general parameters, then specific parameters, user and peer */
					user = build_user(cat, gen, ast_variable_browse(ucfg, cat), 0);
					if (user) {
						ao2_link(users, user);
						user = user_unref(user);
					}
					peer = build_peer(cat, gen, ast_variable_browse(ucfg, cat), 0);
					if (peer) {
						if (ast_test_flag64(peer, IAX_DYNAMIC)) {
							reg_source_db(peer);
						}
						ao2_link(peers, peer);
						peer = peer_unref(peer);
					}
				}
				if (ast_true(registeriax) || (!registeriax && genregisteriax)) {
					char tmp[256];
					const char *host = ast_variable_retrieve(ucfg, cat, "host");
					const char *username = ast_variable_retrieve(ucfg, cat, "username");
					const char *secret = ast_variable_retrieve(ucfg, cat, "secret");
					if (!host)
						host = ast_variable_retrieve(ucfg, "general", "host");
					if (!username)
						username = ast_variable_retrieve(ucfg, "general", "username");
					if (!secret)
						secret = ast_variable_retrieve(ucfg, "general", "secret");
					if (!ast_strlen_zero(username) && !ast_strlen_zero(host)) {
						if (!ast_strlen_zero(secret))
							snprintf(tmp, sizeof(tmp), "%s:%s@%s", username, secret, host);
						else
							snprintf(tmp, sizeof(tmp), "%s@%s", username, host);
						iax2_register(tmp, 0);
					}
				}
			}
			cat = ast_category_browse(ucfg, cat);
		}
		ast_config_destroy(ucfg);
	}

	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (!strcasecmp(cat, "callnumberlimits")) {
				build_callno_limits(ast_variable_browse(cfg, cat));
			} else if (utype) {
				if (!strcasecmp(utype, "user") || !strcasecmp(utype, "friend")) {
					user = build_user(cat, ast_variable_browse(cfg, cat), NULL, 0);
					if (user) {
						ao2_link(users, user);
						user = user_unref(user);
					}
				}
				if (!strcasecmp(utype, "peer") || !strcasecmp(utype, "friend")) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat), NULL, 0);
					if (peer) {
						if (ast_test_flag64(peer, IAX_DYNAMIC))
							reg_source_db(peer);
						ao2_link(peers, peer);
						peer = peer_unref(peer);
					}
				} else if (strcasecmp(utype, "user")) {
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, config_file);
				}
			} else
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_config_destroy(cfg);
	return 1;
}

static void poke_all_peers(void)
{
	struct ao2_iterator i;
	struct iax2_peer *peer;

	i = ao2_iterator_init(peers, 0);
	while ((peer = ao2_iterator_next(&i))) {
		iax2_poke_peer(peer, 0);
		peer_unref(peer);
	}
	ao2_iterator_destroy(&i);
}
static int reload_config(int forced_reload)
{
	static const char config[] = "iax.conf";
	struct iax2_registry *reg;

	if (set_config(config, 1, forced_reload) > 0) {
		prune_peers();
		prune_users();
		ao2_callback(callno_limits, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, prune_addr_range_cb, NULL);
		ao2_callback(calltoken_ignores, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, prune_addr_range_cb, NULL);
		ao2_callback(peercnts, OBJ_NODATA, set_peercnt_limit_all_cb, NULL);
		trunk_timed = trunk_untimed = 0;
		trunk_nmaxmtu = trunk_maxmtu = 0;
		memset(&debugaddr, '\0', sizeof(debugaddr));

		AST_LIST_LOCK(&registrations);
		AST_LIST_TRAVERSE(&registrations, reg, entry)
			iax2_do_register(reg);
		AST_LIST_UNLOCK(&registrations);

		/* Qualify hosts, too */
		poke_all_peers();
	}

	iax_firmware_reload();
	iax_provision_reload(1);
	ast_unload_realtime("iaxpeers");

	return 0;
}

static char *handle_cli_iax2_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "iax2 reload";
		e->usage =
			"Usage: iax2 reload\n"
			"       Reloads IAX configuration from iax.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	reload_config(0);

	return CLI_SUCCESS;
}

static int reload(void)
{
	return reload_config(0);
}

static int cache_get_callno_locked(const char *data)
{
	struct ast_sockaddr addr;
	int x;
	int callno;
	struct iax_ie_data ied;
	struct create_addr_info cai;
	struct parsed_dial_string pds;
	char *tmpstr;

	for (x = 0; x < ARRAY_LEN(iaxs); x++) {
		/* Look for an *exact match* call.  Once a call is negotiated, it can only
		   look up entries for a single context */
		if (!ast_mutex_trylock(&iaxsl[x])) {
			if (iaxs[x] && !strcasecmp(data, iaxs[x]->dproot))
				return x;
			ast_mutex_unlock(&iaxsl[x]);
		}
	}

	/* No match found, we need to create a new one */

	memset(&cai, 0, sizeof(cai));
	memset(&ied, 0, sizeof(ied));
	memset(&pds, 0, sizeof(pds));

	tmpstr = ast_strdupa(data);
	parse_dial_string(tmpstr, &pds);

	if (ast_strlen_zero(pds.peer)) {
		ast_log(LOG_WARNING, "No peer provided in the IAX2 dial string '%s'\n", data);
		return -1;
	}

	/* Populate our address from the given */
	if (create_addr(pds.peer, NULL, &addr, &cai))
		return -1;

	ast_debug(1, "peer: %s, username: %s, password: %s, context: %s\n",
		pds.peer, pds.username, pds.password, pds.context);

	callno = find_callno_locked(0, 0, &addr, NEW_FORCE, cai.sockfd, 0);
	if (callno < 1) {
		ast_log(LOG_WARNING, "Unable to create call\n");
		return -1;
	}

	ast_string_field_set(iaxs[callno], dproot, data);
	iaxs[callno]->capability = IAX_CAPABILITY_FULLBANDWIDTH;

	iax_ie_append_short(&ied, IAX_IE_VERSION, IAX_PROTO_VERSION);
	iax_ie_append_str(&ied, IAX_IE_CALLED_NUMBER, "TBD");
	/* the string format is slightly different from a standard dial string,
	   because the context appears in the 'exten' position
	*/
	if (pds.exten)
		iax_ie_append_str(&ied, IAX_IE_CALLED_CONTEXT, pds.exten);
	if (pds.username)
		iax_ie_append_str(&ied, IAX_IE_USERNAME, pds.username);
	iax_ie_append_int(&ied, IAX_IE_FORMAT, IAX_CAPABILITY_FULLBANDWIDTH);
	iax_ie_append_int(&ied, IAX_IE_CAPABILITY, IAX_CAPABILITY_FULLBANDWIDTH);
	/* Keep password handy */
	if (pds.password)
		ast_string_field_set(iaxs[callno], secret, pds.password);
	if (pds.key)
		ast_string_field_set(iaxs[callno], outkey, pds.key);
	/* Start the call going */
	add_empty_calltoken_ie(iaxs[callno], &ied); /* this _MUST_ be the last ie added */
	send_command(iaxs[callno], AST_FRAME_IAX, IAX_COMMAND_NEW, 0, ied.buf, ied.pos, -1);

	return callno;
}

static struct iax2_dpcache *find_cache(struct ast_channel *chan, const char *data, const char *context, const char *exten, int priority)
{
	struct iax2_dpcache *dp = NULL;
	struct timeval now = ast_tvnow();
	int x, com[2], timeout, old = 0, outfd, doabort, callno;
	struct ast_channel *c = NULL;
	struct ast_frame *f = NULL;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&dpcache, dp, cache_list) {
		if (ast_tvcmp(now, dp->expiry) > 0) {
			AST_LIST_REMOVE_CURRENT(cache_list);
			if ((dp->flags & CACHE_FLAG_PENDING) || dp->callno)
				ast_log(LOG_WARNING, "DP still has peer field or pending or callno (flags = %d, peer = blah, callno = %d)\n", dp->flags, dp->callno);
			else
				ast_free(dp);
			continue;
		}
		if (!strcmp(dp->peercontext, data) && !strcmp(dp->exten, exten))
			break;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!dp) {
		/* No matching entry.  Create a new one. */
		/* First, can we make a callno? */
		if ((callno = cache_get_callno_locked(data)) < 0) {
			ast_log(LOG_WARNING, "Unable to generate call for '%s'\n", data);
			return NULL;
		}
		if (!(dp = ast_calloc(1, sizeof(*dp)))) {
			ast_mutex_unlock(&iaxsl[callno]);
			return NULL;
		}
		ast_copy_string(dp->peercontext, data, sizeof(dp->peercontext));
		ast_copy_string(dp->exten, exten, sizeof(dp->exten));
		dp->expiry = ast_tvnow();
		dp->orig = dp->expiry;
		/* Expires in 30 mins by default */
		dp->expiry.tv_sec += iaxdefaultdpcache;
		dp->flags = CACHE_FLAG_PENDING;
		for (x = 0; x < ARRAY_LEN(dp->waiters); x++)
			dp->waiters[x] = -1;
		/* Insert into the lists */
		AST_LIST_INSERT_TAIL(&dpcache, dp, cache_list);
		AST_LIST_INSERT_TAIL(&iaxs[callno]->dpentries, dp, peer_list);
		/* Send the request if we're already up */
		if (ast_test_flag(&iaxs[callno]->state, IAX_STATE_STARTED))
			iax2_dprequest(dp, callno);
		ast_mutex_unlock(&iaxsl[callno]);
	}

	/* By here we must have a dp */
	if (dp->flags & CACHE_FLAG_PENDING) {
		struct timeval start;
		int ms;
		/* Okay, here it starts to get nasty.  We need a pipe now to wait
		   for a reply to come back so long as it's pending */
		for (x = 0; x < ARRAY_LEN(dp->waiters); x++) {
			/* Find an empty slot */
			if (dp->waiters[x] < 0)
				break;
		}
		if (x >= ARRAY_LEN(dp->waiters)) {
			ast_log(LOG_WARNING, "No more waiter positions available\n");
			return NULL;
		}
		if (pipe(com)) {
			ast_log(LOG_WARNING, "Unable to create pipe for comm\n");
			return NULL;
		}
		dp->waiters[x] = com[1];
		/* Okay, now we wait */
		timeout = iaxdefaulttimeout * 1000;
		/* Temporarily unlock */
		AST_LIST_UNLOCK(&dpcache);
		/* Defer any dtmf */
		if (chan)
			old = ast_channel_defer_dtmf(chan);
		doabort = 0;
		start = ast_tvnow();
		while ((ms = ast_remaining_ms(start, timeout))) {
			c = ast_waitfor_nandfds(&chan, chan ? 1 : 0, &com[0], 1, NULL, &outfd, &ms);
			if (outfd > -1)
				break;
			if (!c)
				continue;
			if (!(f = ast_read(c))) {
				doabort = 1;
				break;
			}
			ast_frfree(f);
		}
		if (!ms) {
			ast_log(LOG_WARNING, "Timeout waiting for %s exten %s\n", data, exten);
		}
		AST_LIST_LOCK(&dpcache);
		dp->waiters[x] = -1;
		close(com[1]);
		close(com[0]);
		if (doabort) {
			/* Don't interpret anything, just abort.  Not sure what th epoint
			  of undeferring dtmf on a hung up channel is but hey whatever */
			if (!old && chan)
				ast_channel_undefer_dtmf(chan);
			return NULL;
		}
		if (!(dp->flags & CACHE_FLAG_TIMEOUT)) {
			/* Now to do non-independent analysis the results of our wait */
			if (dp->flags & CACHE_FLAG_PENDING) {
				/* Still pending... It's a timeout.  Wake everybody up.  Consider it no longer
				   pending.  Don't let it take as long to timeout. */
				dp->flags &= ~CACHE_FLAG_PENDING;
				dp->flags |= CACHE_FLAG_TIMEOUT;
				/* Expire after only 60 seconds now.  This is designed to help reduce backlog in heavily loaded
				   systems without leaving it unavailable once the server comes back online */
				dp->expiry.tv_sec = dp->orig.tv_sec + 60;
				for (x = 0; x < ARRAY_LEN(dp->waiters); x++) {
					if (dp->waiters[x] > -1) {
						if (write(dp->waiters[x], "asdf", 4) < 0) {
							ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
						}
					}
				}
			}
		}
		/* Our caller will obtain the rest */
		if (!old && chan)
			ast_channel_undefer_dtmf(chan);
	}
	return dp;
}

/*! \brief Part of the IAX2 switch interface */
static int iax2_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res = 0;
	struct iax2_dpcache *dp = NULL;
#if 0
	ast_log(LOG_NOTICE, "iax2_exists: con: %s, exten: %s, pri: %d, cid: %s, data: %s\n", context, exten, priority, callerid ? callerid : "<unknown>", data);
#endif
	if ((priority != 1) && (priority != 2))
		return 0;

	AST_LIST_LOCK(&dpcache);
	if ((dp = find_cache(chan, data, context, exten, priority))) {
		if (dp->flags & CACHE_FLAG_EXISTS)
			res = 1;
	} else {
		ast_log(LOG_WARNING, "Unable to make DP cache\n");
	}
	AST_LIST_UNLOCK(&dpcache);

	return res;
}

/*! \brief part of the IAX2 dial plan switch interface */
static int iax2_canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res = 0;
	struct iax2_dpcache *dp = NULL;
#if 0
	ast_log(LOG_NOTICE, "iax2_canmatch: con: %s, exten: %s, pri: %d, cid: %s, data: %s\n", context, exten, priority, callerid ? callerid : "<unknown>", data);
#endif
	if ((priority != 1) && (priority != 2))
		return 0;

	AST_LIST_LOCK(&dpcache);
	if ((dp = find_cache(chan, data, context, exten, priority))) {
		if (dp->flags & CACHE_FLAG_CANEXIST)
			res = 1;
	} else {
		ast_log(LOG_WARNING, "Unable to make DP cache\n");
	}
	AST_LIST_UNLOCK(&dpcache);

	return res;
}

/*! \brief Part of the IAX2 Switch interface */
static int iax2_matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res = 0;
	struct iax2_dpcache *dp = NULL;
#if 0
	ast_log(LOG_NOTICE, "iax2_matchmore: con: %s, exten: %s, pri: %d, cid: %s, data: %s\n", context, exten, priority, callerid ? callerid : "<unknown>", data);
#endif
	if ((priority != 1) && (priority != 2))
		return 0;

	AST_LIST_LOCK(&dpcache);
	if ((dp = find_cache(chan, data, context, exten, priority))) {
		if (dp->flags & CACHE_FLAG_MATCHMORE)
			res = 1;
	} else {
		ast_log(LOG_WARNING, "Unable to make DP cache\n");
	}
	AST_LIST_UNLOCK(&dpcache);

	return res;
}

/*! \brief Execute IAX2 dialplan switch */
static int iax2_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	char odata[256];
	char req[256];
	char *ncontext;
	struct iax2_dpcache *dp = NULL;
	struct ast_app *dial = NULL;
#if 0
	ast_log(LOG_NOTICE, "iax2_exec: con: %s, exten: %s, pri: %d, cid: %s, data: %s, newstack: %d\n", context, exten, priority, callerid ? callerid : "<unknown>", data, newstack);
#endif
	if (priority == 2) {
		/* Indicate status, can be overridden in dialplan */
		const char *dialstatus = pbx_builtin_getvar_helper(chan, "DIALSTATUS");
		if (dialstatus) {
			dial = pbx_findapp(dialstatus);
			if (dial)
				pbx_exec(chan, dial, "");
		}
		return -1;
	} else if (priority != 1)
		return -1;

	AST_LIST_LOCK(&dpcache);
	if ((dp = find_cache(chan, data, context, exten, priority))) {
		if (dp->flags & CACHE_FLAG_EXISTS) {
			ast_copy_string(odata, data, sizeof(odata));
			ncontext = strchr(odata, '/');
			if (ncontext) {
				*ncontext = '\0';
				ncontext++;
				snprintf(req, sizeof(req), "IAX2/%s/%s@%s", odata, exten, ncontext);
			} else {
				snprintf(req, sizeof(req), "IAX2/%s/%s", odata, exten);
			}
			ast_verb(3, "Executing Dial('%s')\n", req);
		} else {
			AST_LIST_UNLOCK(&dpcache);
			ast_log(LOG_WARNING, "Can't execute nonexistent extension '%s[@%s]' in data '%s'\n", exten, context, data);
			return -1;
		}
	}
	AST_LIST_UNLOCK(&dpcache);

	if ((dial = pbx_findapp("Dial")))
		return pbx_exec(chan, dial, req);
	else
		ast_log(LOG_WARNING, "No dial application registered\n");

	return -1;
}

static int function_iaxpeer(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct iax2_peer *peer;
	char *peername, *colname;

	peername = ast_strdupa(data);

	/* if our channel, return the IP address of the endpoint of current channel */
	if (!strcmp(peername,"CURRENTCHANNEL")) {
	        unsigned short callno;
		if (!chan || ast_channel_tech(chan) != &iax2_tech) {
			return -1;
		}
		callno = PTR_TO_CALLNO(ast_channel_tech_pvt(chan));
		ast_copy_string(buf, !ast_sockaddr_isnull(&iaxs[callno]->addr) ? ast_sockaddr_stringify_addr(&iaxs[callno]->addr) : "", len);
		return 0;
	}

	if ((colname = strchr(peername, ',')))
		*colname++ = '\0';
	else
		colname = "ip";

	if (!(peer = find_peer(peername, 1)))
		return -1;

	if (!strcasecmp(colname, "ip")) {
		ast_copy_string(buf, ast_sockaddr_stringify_addr(&peer->addr), len);
	} else  if (!strcasecmp(colname, "status")) {
		peer_status(peer, buf, len);
	} else  if (!strcasecmp(colname, "mailbox")) {
		ast_copy_string(buf, peer->mailbox, len);
	} else  if (!strcasecmp(colname, "context")) {
		ast_copy_string(buf, peer->context, len);
	} else  if (!strcasecmp(colname, "expire")) {
		snprintf(buf, len, "%d", peer->expire);
	} else  if (!strcasecmp(colname, "dynamic")) {
		ast_copy_string(buf, (ast_test_flag64(peer, IAX_DYNAMIC) ? "yes" : "no"), len);
	} else  if (!strcasecmp(colname, "callerid_name")) {
		ast_copy_string(buf, peer->cid_name, len);
	} else  if (!strcasecmp(colname, "callerid_num")) {
		ast_copy_string(buf, peer->cid_num, len);
	} else  if (!strcasecmp(colname, "codecs")) {
		struct ast_str *codec_buf = ast_str_alloca(256);

		iax2_getformatname_multiple(peer->capability, &codec_buf);
		ast_copy_string(buf, ast_str_buffer(codec_buf), len);
	} else  if (!strncasecmp(colname, "codec[", 6)) {
		char *codecnum, *ptr;
		struct ast_format *tmpfmt;

		/* skip over "codec" to the '[' */
		codecnum = colname + 5;
		*codecnum = '\0';
		codecnum++;
		if ((ptr = strchr(codecnum, ']'))) {
			*ptr = '\0';
		}
		if((iax2_codec_pref_index(&peer->prefs, atoi(codecnum), &tmpfmt))) {
			ast_copy_string(buf, ast_format_get_name(tmpfmt), len);
		} else {
			buf[0] = '\0';
		}
	} else {
		buf[0] = '\0';
	}

	peer_unref(peer);

	return 0;
}

static struct ast_custom_function iaxpeer_function = {
	.name = "IAXPEER",
	.read = function_iaxpeer,
};

static int acf_channel_read(struct ast_channel *chan, const char *funcname, char *args, char *buf, size_t buflen)
{
	struct chan_iax2_pvt *pvt;
	unsigned int callno;
	int res = 0;

	if (!chan || ast_channel_tech(chan) != &iax2_tech) {
		ast_log(LOG_ERROR, "This function requires a valid IAX2 channel\n");
		return -1;
	}

	callno = PTR_TO_CALLNO(ast_channel_tech_pvt(chan));
	ast_mutex_lock(&iaxsl[callno]);
	if (!(pvt = iaxs[callno])) {
		ast_mutex_unlock(&iaxsl[callno]);
		return -1;
	}

	if (!strcasecmp(args, "osptoken")) {
		ast_copy_string(buf, pvt->osptoken, buflen);
	} else if (!strcasecmp(args, "peerip")) {
		ast_copy_string(buf, !ast_sockaddr_isnull(&pvt->addr) ? ast_sockaddr_stringify_addr(&pvt->addr) : "", buflen);
	} else if (!strcasecmp(args, "peername")) {
		ast_copy_string(buf, pvt->username, buflen);
	} else if (!strcasecmp(args, "secure_signaling") || !strcasecmp(args, "secure_media")) {
		snprintf(buf, buflen, "%s", IAX_CALLENCRYPTED(pvt) ? "1" : "");
	} else {
		res = -1;
	}

	ast_mutex_unlock(&iaxsl[callno]);

	return res;
}

/*! \brief Part of the device state notification system ---*/
static int iax2_devicestate(const char *data)
{
	struct parsed_dial_string pds;
	char *tmp = ast_strdupa(data);
	struct iax2_peer *p;
	int res = AST_DEVICE_INVALID;

	memset(&pds, 0, sizeof(pds));
	parse_dial_string(tmp, &pds);

	if (ast_strlen_zero(pds.peer)) {
		ast_log(LOG_WARNING, "No peer provided in the IAX2 dial string '%s'\n", data);
		return res;
	}

	ast_debug(3, "Checking device state for device %s\n", pds.peer);

	/* SLD: FIXME: second call to find_peer during registration */
	if (!(p = find_peer(pds.peer, 1)))
		return res;

	res = AST_DEVICE_UNAVAILABLE;

	ast_debug(3, "Found peer. What's device state of %s? addr=%s, defaddr=%s maxms=%d, lastms=%d\n",
		pds.peer, ast_sockaddr_stringify(&p->addr), ast_sockaddr_stringify(&p->defaddr), p->maxms, p->lastms);

	if (((!ast_sockaddr_isnull(&p->addr)) || (!ast_sockaddr_isnull(&p->defaddr))) &&
	    (!p->maxms || ((p->lastms > -1) && (p->historicms <= p->maxms)))) {
		/* Peer is registered, or have default IP address
		   and a valid registration */
		if (p->historicms == 0 || p->historicms <= p->maxms)
			/* let the core figure out whether it is in use or not */
			res = AST_DEVICE_UNKNOWN;
	}

	peer_unref(p);

	return res;
}

static struct ast_switch iax2_switch =
{
	.name        = "IAX2",
	.description = "IAX Remote Dialplan Switch",
	.exists      = iax2_exists,
	.canmatch    = iax2_canmatch,
	.exec        = iax2_exec,
	.matchmore   = iax2_matchmore,
};

static struct ast_cli_entry cli_iax2[] = {
	AST_CLI_DEFINE(handle_cli_iax2_provision,           "Provision an IAX device"),
	AST_CLI_DEFINE(handle_cli_iax2_prune_realtime,      "Prune a cached realtime lookup"),
	AST_CLI_DEFINE(handle_cli_iax2_reload,              "Reload IAX configuration"),
	AST_CLI_DEFINE(handle_cli_iax2_set_mtu,             "Set the IAX systemwide trunking MTU"),
	AST_CLI_DEFINE(handle_cli_iax2_set_debug,           "Enable/Disable IAX debugging"),
	AST_CLI_DEFINE(handle_cli_iax2_set_debug_trunk,     "Enable/Disable IAX trunk debugging"),
	AST_CLI_DEFINE(handle_cli_iax2_set_debug_jb,        "Enable/Disable IAX jitterbuffer debugging"),
	AST_CLI_DEFINE(handle_cli_iax2_show_cache,          "Display IAX cached dialplan"),
	AST_CLI_DEFINE(handle_cli_iax2_show_channels,       "List active IAX channels"),
	AST_CLI_DEFINE(handle_cli_iax2_show_firmware,       "List available IAX firmware"),
	AST_CLI_DEFINE(handle_cli_iax2_show_netstats,       "List active IAX channel netstats"),
	AST_CLI_DEFINE(handle_cli_iax2_show_peer,           "Show details on specific IAX peer"),
	AST_CLI_DEFINE(handle_cli_iax2_show_peers,          "List defined IAX peers"),
	AST_CLI_DEFINE(handle_cli_iax2_show_registry,       "Display IAX registration status"),
	AST_CLI_DEFINE(handle_cli_iax2_show_stats,          "Display IAX statistics"),
	AST_CLI_DEFINE(handle_cli_iax2_show_threads,        "Display IAX helper thread info"),
	AST_CLI_DEFINE(handle_cli_iax2_show_users,          "List defined IAX users"),
	AST_CLI_DEFINE(handle_cli_iax2_test_losspct,        "Set IAX2 incoming frame loss percentage"),
	AST_CLI_DEFINE(handle_cli_iax2_unregister,          "Unregister (force expiration) an IAX2 peer from the registry"),
	AST_CLI_DEFINE(handle_cli_iax2_show_callno_limits,  "Show current entries in IP call number limit table"),
#ifdef IAXTESTS
	AST_CLI_DEFINE(handle_cli_iax2_test_jitter,         "Simulates jitter for testing"),
	AST_CLI_DEFINE(handle_cli_iax2_test_late,           "Test the receipt of a late frame"),
	AST_CLI_DEFINE(handle_cli_iax2_test_resync,         "Test a resync in received timestamps"),
#endif /* IAXTESTS */
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_iax2_peers_get)
{
	struct ast_data_query query = {
		.path = "/asterisk/channel/iax2/peers",
		.search = "peers/peer/name=test_peer_data_provider"
	};
	struct ast_data *node;
	struct iax2_peer *peer;

	switch (cmd) {
		case TEST_INIT:
			info->name = "iax2_peers_get_data_test";
			info->category = "/main/data/iax2/peers/";
			info->summary = "IAX2 peers data providers unit test";
			info->description =
				"Tests whether the IAX2 peers data provider implementation works as expected.";
			return AST_TEST_NOT_RUN;
		case TEST_EXECUTE:
			break;
	}

	/* build a test peer */
	peer = build_peer("test_peer_data_provider", NULL, NULL, 0);
	if (!peer) {
		return AST_TEST_FAIL;
	}
	peer->expiry= 1010;
	ao2_link(peers, peer);

	node = ast_data_get(&query);
	if (!node) {
		ao2_unlink(peers, peer);
		peer_unref(peer);
		return AST_TEST_FAIL;
	}

	/* check returned data node. */
	if (strcmp(ast_data_retrieve_string(node, "peer/name"), "test_peer_data_provider")) {
		ao2_unlink(peers, peer);
		peer_unref(peer);
		ast_data_free(node);
		return AST_TEST_FAIL;
	}

	if (ast_data_retrieve_int(node, "peer/expiry") != 1010) {
		ao2_unlink(peers, peer);
		peer_unref(peer);
		ast_data_free(node);
		return AST_TEST_FAIL;
	}

	/* release resources */
	ast_data_free(node);

	ao2_unlink(peers, peer);
	peer_unref(peer);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_iax2_users_get)
{
	struct ast_data_query query = {
		.path = "/asterisk/channel/iax2/users",
		.search = "users/user/name=test_user_data_provider"
	};
	struct ast_data *node;
	struct iax2_user *user;

	switch (cmd) {
		case TEST_INIT:
			info->name = "iax2_users_get_data_test";
			info->category = "/main/data/iax2/users/";
			info->summary = "IAX2 users data providers unit test";
			info->description =
				"Tests whether the IAX2 users data provider implementation works as expected.";
			return AST_TEST_NOT_RUN;
		case TEST_EXECUTE:
			break;
	}

	user = build_user("test_user_data_provider", NULL, NULL, 0);
	if (!user) {
		ast_test_status_update(test, "Failed to build a test user\n");
		return AST_TEST_FAIL;
	}
	user->amaflags = 1010;
	ao2_link(users, user);

	node = ast_data_get(&query);
	if (!node) {
		ast_test_status_update(test, "The data query to find our test user failed\n");
		ao2_unlink(users, user);
		user_unref(user);
		return AST_TEST_FAIL;
	}

	if (strcmp(ast_data_retrieve_string(node, "user/name"), "test_user_data_provider")) {
		ast_test_status_update(test, "Our data results did not return the test user created in the previous step.\n");
		ao2_unlink(users, user);
		user_unref(user);
		ast_data_free(node);
		return AST_TEST_FAIL;
	}

	if (ast_data_retrieve_int(node, "user/amaflags/value") != 1010) {
		ast_test_status_update(test, "The amaflags field in our test user was '%d' not the expected value '1010'\n", ast_data_retrieve_int(node, "user/amaflags/value"));
		ao2_unlink(users, user);
		user_unref(user);
		ast_data_free(node);
		return AST_TEST_FAIL;
	}

	ast_data_free(node);

	ao2_unlink(users, user);
	user_unref(user);

	return AST_TEST_PASS;
}
#endif

static void cleanup_thread_list(void *head)
{
	AST_LIST_HEAD(iax2_thread_list, iax2_thread);
	struct iax2_thread_list *list_head = head;
	struct iax2_thread *thread;

	AST_LIST_LOCK(list_head);
	while ((thread = AST_LIST_REMOVE_HEAD(list_head, list))) {
		pthread_t thread_id = thread->threadid;

		thread->stop = 1;
		signal_condition(&thread->lock, &thread->cond);

		AST_LIST_UNLOCK(list_head);
		pthread_join(thread_id, NULL);
		AST_LIST_LOCK(list_head);
	}
	AST_LIST_UNLOCK(list_head);
}

static int __unload_module(void)
{
	struct ast_context *con;
	int x;

	network_change_stasis_unsubscribe();
	acl_change_stasis_unsubscribe();

	ast_manager_unregister("IAXpeers");
	ast_manager_unregister("IAXpeerlist");
	ast_manager_unregister("IAXnetstats");
	ast_manager_unregister("IAXregistry");
	ast_unregister_application(papp);
	ast_cli_unregister_multiple(cli_iax2, ARRAY_LEN(cli_iax2));
	ast_unregister_switch(&iax2_switch);
	ast_channel_unregister(&iax2_tech);

	if (netthreadid != AST_PTHREADT_NULL) {
		pthread_cancel(netthreadid);
		pthread_kill(netthreadid, SIGURG);
		pthread_join(netthreadid, NULL);
	}

	for (x = 0; x < ARRAY_LEN(iaxs); x++) {
		if (iaxs[x]) {
			iax2_destroy(x);
		}
	}

	/* Call for all threads to halt */
	cleanup_thread_list(&active_list);
	cleanup_thread_list(&dynamic_list);
	cleanup_thread_list(&idle_list);

	ast_netsock_release(netsock);
	ast_netsock_release(outsock);
	for (x = 0; x < ARRAY_LEN(iaxs); x++) {
		if (iaxs[x]) {
			iax2_destroy(x);
		}
	}
	ast_manager_unregister( "IAXpeers" );
	ast_manager_unregister( "IAXpeerlist" );
	ast_manager_unregister( "IAXnetstats" );
	ast_manager_unregister( "IAXregistry" );
	ast_unregister_application(papp);
#ifdef TEST_FRAMEWORK
	AST_TEST_UNREGISTER(test_iax2_peers_get);
	AST_TEST_UNREGISTER(test_iax2_users_get);
#endif
	ast_data_unregister(NULL);
	ast_cli_unregister_multiple(cli_iax2, ARRAY_LEN(cli_iax2));
	ast_unregister_switch(&iax2_switch);
	ast_channel_unregister(&iax2_tech);
	delete_users();
	iax_provision_unload();
	iax_firmware_unload();

	for (x = 0; x < ARRAY_LEN(iaxsl); x++) {
		ast_mutex_destroy(&iaxsl[x]);
	}

	ao2_ref(peers, -1);
	ao2_ref(users, -1);
	ao2_ref(iax_peercallno_pvts, -1);
	ao2_ref(iax_transfercallno_pvts, -1);
	ao2_ref(callno_limits, -1);
	ao2_ref(calltoken_ignores, -1);
	if (timer) {
		ast_timer_close(timer);
		timer = NULL;
	}
	transmit_processor = ast_taskprocessor_unreference(transmit_processor);

	ast_sched_clean_by_callback(sched, peercnt_remove_cb, peercnt_remove_cb);
	ast_sched_context_destroy(sched);
	sched = NULL;
	ao2_ref(peercnts, -1);

	con = ast_context_find(regcontext);
	if (con)
		ast_context_destroy(con, "IAX2");
	ast_unload_realtime("iaxpeers");

	ao2_ref(iax2_tech.capabilities, -1);
	iax2_tech.capabilities = NULL;
	return 0;
}

static int unload_module(void)
{
	ast_custom_function_unregister(&iaxpeer_function);
	ast_custom_function_unregister(&iaxvar_function);
	return __unload_module();
}

static int peer_set_sock_cb(void *obj, void *arg, int flags)
{
	struct iax2_peer *peer = obj;

	if (peer->sockfd < 0)
		peer->sockfd = defaultsockfd;

	return 0;
}

static int pvt_hash_cb(const void *obj, const int flags)
{
	const struct chan_iax2_pvt *pvt = obj;

	return pvt->peercallno;
}

static int pvt_cmp_cb(void *obj, void *arg, int flags)
{
	struct chan_iax2_pvt *pvt = obj, *pvt2 = arg;

	/* The frames_received field is used to hold whether we're matching
	 * against a full frame or not ... */

	return match(&pvt2->addr, pvt2->peercallno, pvt2->callno, pvt,
		pvt2->frames_received) ? CMP_MATCH | CMP_STOP : 0;
}

static int transfercallno_pvt_hash_cb(const void *obj, const int flags)
{
	const struct chan_iax2_pvt *pvt = obj;

	return pvt->transfercallno;
}

static int transfercallno_pvt_cmp_cb(void *obj, void *arg, int flags)
{
	struct chan_iax2_pvt *pvt = obj, *pvt2 = arg;

	/* The frames_received field is used to hold whether we're matching
	 * against a full frame or not ... */

	return match(&pvt2->transfer, pvt2->transfercallno, pvt2->callno, pvt,
		pvt2->frames_received) ? CMP_MATCH | CMP_STOP : 0;
}

static int load_objects(void)
{
	peers = users = iax_peercallno_pvts = iax_transfercallno_pvts = NULL;
	peercnts = callno_limits = calltoken_ignores = NULL;

	if (!(peers = ao2_container_alloc(MAX_PEER_BUCKETS, peer_hash_cb, peer_cmp_cb))) {
		goto container_fail;
	} else if (!(users = ao2_container_alloc(MAX_USER_BUCKETS, user_hash_cb, user_cmp_cb))) {
		goto container_fail;
	} else if (!(iax_peercallno_pvts = ao2_container_alloc(IAX_MAX_CALLS, pvt_hash_cb, pvt_cmp_cb))) {
		goto container_fail;
	} else if (!(iax_transfercallno_pvts = ao2_container_alloc(IAX_MAX_CALLS, transfercallno_pvt_hash_cb, transfercallno_pvt_cmp_cb))) {
		goto container_fail;
	} else if (!(peercnts = ao2_container_alloc(MAX_PEER_BUCKETS, peercnt_hash_cb, peercnt_cmp_cb))) {
		goto container_fail;
	} else if (!(callno_limits = ao2_container_alloc(MAX_PEER_BUCKETS, addr_range_hash_cb, addr_range_cmp_cb))) {
		goto container_fail;
	} else if (!(calltoken_ignores = ao2_container_alloc(MAX_PEER_BUCKETS, addr_range_hash_cb, addr_range_cmp_cb))) {
		goto container_fail;
	} else if (create_callno_pools()) {
		goto container_fail;
	} else if  (!(transmit_processor = ast_taskprocessor_get("iax2_transmit", TPS_REF_DEFAULT))) {
		goto container_fail;
	}

	return 0;

container_fail:
	if (peers) {
		ao2_ref(peers, -1);
	}
	if (users) {
		ao2_ref(users, -1);
	}
	if (iax_peercallno_pvts) {
		ao2_ref(iax_peercallno_pvts, -1);
	}
	if (iax_transfercallno_pvts) {
		ao2_ref(iax_transfercallno_pvts, -1);
	}
	if (peercnts) {
		ao2_ref(peercnts, -1);
	}
	if (callno_limits) {
		ao2_ref(callno_limits, -1);
	}
	if (calltoken_ignores) {
		ao2_ref(calltoken_ignores, -1);
	}
	return AST_MODULE_LOAD_FAILURE;
}


#define DATA_EXPORT_IAX2_PEER(MEMBER)				\
	MEMBER(iax2_peer, name, AST_DATA_STRING)		\
	MEMBER(iax2_peer, username, AST_DATA_STRING)		\
	MEMBER(iax2_peer, secret, AST_DATA_PASSWORD)		\
	MEMBER(iax2_peer, dbsecret, AST_DATA_PASSWORD)		\
	MEMBER(iax2_peer, outkey, AST_DATA_STRING)		\
	MEMBER(iax2_peer, regexten, AST_DATA_STRING)		\
	MEMBER(iax2_peer, context, AST_DATA_STRING)		\
	MEMBER(iax2_peer, peercontext, AST_DATA_STRING)		\
	MEMBER(iax2_peer, mailbox, AST_DATA_STRING)		\
	MEMBER(iax2_peer, mohinterpret, AST_DATA_STRING)	\
	MEMBER(iax2_peer, mohsuggest, AST_DATA_STRING)		\
	MEMBER(iax2_peer, inkeys, AST_DATA_STRING)		\
	MEMBER(iax2_peer, cid_num, AST_DATA_STRING)		\
	MEMBER(iax2_peer, cid_name, AST_DATA_STRING)		\
	MEMBER(iax2_peer, zonetag, AST_DATA_STRING)		\
	MEMBER(iax2_peer, parkinglot, AST_DATA_STRING)		\
	MEMBER(iax2_peer, expiry, AST_DATA_SECONDS)		\
	MEMBER(iax2_peer, callno, AST_DATA_INTEGER)		\
	MEMBER(iax2_peer, lastms, AST_DATA_MILLISECONDS)	\
	MEMBER(iax2_peer, maxms, AST_DATA_MILLISECONDS)		\
	MEMBER(iax2_peer, pokefreqok, AST_DATA_MILLISECONDS)	\
	MEMBER(iax2_peer, pokefreqnotok, AST_DATA_MILLISECONDS)	\
	MEMBER(iax2_peer, historicms, AST_DATA_INTEGER)		\
	MEMBER(iax2_peer, smoothing, AST_DATA_BOOLEAN)		\
        MEMBER(iax2_peer, maxcallno, AST_DATA_INTEGER)

AST_DATA_STRUCTURE(iax2_peer, DATA_EXPORT_IAX2_PEER);

static int peers_data_provider_get(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct ast_data *data_peer;
	struct iax2_peer *peer;
	struct ao2_iterator i;
	char status[20];
	struct ast_str *encmethods = ast_str_alloca(256);

	i = ao2_iterator_init(peers, 0);
	while ((peer = ao2_iterator_next(&i))) {
		data_peer = ast_data_add_node(data_root, "peer");
		if (!data_peer) {
			peer_unref(peer);
			continue;
		}

		ast_data_add_structure(iax2_peer, data_peer, peer);

		iax2_data_add_codecs(data_peer, "codecs", peer->capability);

		peer_status(peer, status, sizeof(status));
		ast_data_add_str(data_peer, "status", status);

		ast_data_add_str(data_peer, "host", ast_sockaddr_stringify_host(&peer->addr));

		ast_data_add_str(data_peer, "mask", ast_sockaddr_stringify_addr(&peer->mask));

		ast_data_add_int(data_peer, "port", ast_sockaddr_port(&peer->addr));

		ast_data_add_bool(data_peer, "trunk", ast_test_flag64(peer, IAX_TRUNK));

		ast_data_add_bool(data_peer, "dynamic", ast_test_flag64(peer, IAX_DYNAMIC));

		encmethods_to_str(peer->encmethods, &encmethods);
		ast_data_add_str(data_peer, "encryption", peer->encmethods ? ast_str_buffer(encmethods) : "no");

		peer_unref(peer);

		if (!ast_data_search_match(search, data_peer)) {
			ast_data_remove_node(data_root, data_peer);
		}
	}
	ao2_iterator_destroy(&i);

	return 0;
}

#define DATA_EXPORT_IAX2_USER(MEMBER)					\
        MEMBER(iax2_user, name, AST_DATA_STRING)			\
        MEMBER(iax2_user, dbsecret, AST_DATA_PASSWORD)			\
        MEMBER(iax2_user, accountcode, AST_DATA_STRING)			\
        MEMBER(iax2_user, mohinterpret, AST_DATA_STRING)		\
        MEMBER(iax2_user, mohsuggest, AST_DATA_STRING)			\
        MEMBER(iax2_user, inkeys, AST_DATA_STRING)			\
        MEMBER(iax2_user, language, AST_DATA_STRING)			\
        MEMBER(iax2_user, cid_num, AST_DATA_STRING)			\
        MEMBER(iax2_user, cid_name, AST_DATA_STRING)			\
        MEMBER(iax2_user, parkinglot, AST_DATA_STRING)			\
        MEMBER(iax2_user, maxauthreq, AST_DATA_INTEGER)			\
        MEMBER(iax2_user, curauthreq, AST_DATA_INTEGER)

AST_DATA_STRUCTURE(iax2_user, DATA_EXPORT_IAX2_USER);

static int users_data_provider_get(const struct ast_data_search *search,
	struct ast_data *data_root)
{
	struct ast_data *data_user, *data_authmethods, *data_enum_node;
	struct iax2_user *user;
	struct ao2_iterator i;
	struct ast_str *auth;
	char *pstr = "";

	if (!(auth = ast_str_create(90))) {
		ast_log(LOG_ERROR, "Unable to create temporary string for storing 'secret'\n");
		return 0;
	}

	i = ao2_iterator_init(users, 0);
	for (; (user = ao2_iterator_next(&i)); user_unref(user)) {
		data_user = ast_data_add_node(data_root, "user");
		if (!data_user) {
			continue;
		}

		ast_data_add_structure(iax2_user, data_user, user);

		iax2_data_add_codecs(data_user, "codecs", user->capability);

		if (!ast_strlen_zero(user->secret)) {
			ast_str_set(&auth, 0, "%s", user->secret);
		} else if (!ast_strlen_zero(user->inkeys)) {
			ast_str_set(&auth, 0, "Key: %s", user->inkeys);
		} else {
			ast_str_set(&auth, 0, "no secret");
		}
		ast_data_add_password(data_user, "secret", ast_str_buffer(auth));

		ast_data_add_str(data_user, "context", user->contexts ? user->contexts->context : DEFAULT_CONTEXT);

		/* authmethods */
		data_authmethods = ast_data_add_node(data_user, "authmethods");
		if (!data_authmethods) {
			ast_data_remove_node(data_root, data_user);
			continue;
		}
		ast_data_add_bool(data_authmethods, "rsa", user->authmethods & IAX_AUTH_RSA);
		ast_data_add_bool(data_authmethods, "md5", user->authmethods & IAX_AUTH_MD5);
		ast_data_add_bool(data_authmethods, "plaintext", user->authmethods & IAX_AUTH_PLAINTEXT);

		/* amaflags */
		data_enum_node = ast_data_add_node(data_user, "amaflags");
		if (!data_enum_node) {
			ast_data_remove_node(data_root, data_user);
			continue;
		}
		ast_data_add_int(data_enum_node, "value", user->amaflags);
		ast_data_add_str(data_enum_node, "text", ast_channel_amaflags2string(user->amaflags));

		ast_data_add_bool(data_user, "access-control", ast_acl_list_is_empty(user->acl) ? 0 : 1);

		if (ast_test_flag64(user, IAX_CODEC_NOCAP)) {
			pstr = "REQ only";
		} else if (ast_test_flag64(user, IAX_CODEC_NOPREFS)) {
			pstr = "disabled";
		} else {
			pstr = ast_test_flag64(user, IAX_CODEC_USER_FIRST) ? "caller" : "host";
		}
		ast_data_add_str(data_user, "codec-preferences", pstr);

		if (!ast_data_search_match(search, data_user)) {
			ast_data_remove_node(data_root, data_user);
		}
	}
	ao2_iterator_destroy(&i);

	ast_free(auth);
	return 0;
}

static const struct ast_data_handler peers_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = peers_data_provider_get
};

static const struct ast_data_handler users_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = users_data_provider_get
};

static const struct ast_data_entry iax2_data_providers[] = {
	AST_DATA_ENTRY("asterisk/channel/iax2/peers", &peers_data_provider),
	AST_DATA_ENTRY("asterisk/channel/iax2/users", &users_data_provider),
};

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	static const char config[] = "iax.conf";
	int x = 0;
	struct iax2_registry *reg = NULL;

	if (!(iax2_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_append_by_type(iax2_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);

	if (load_objects()) {
		return AST_MODULE_LOAD_FAILURE;
	}

	memset(iaxs, 0, sizeof(iaxs));

	for (x = 0; x < ARRAY_LEN(iaxsl); x++) {
		ast_mutex_init(&iaxsl[x]);
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Failed to create scheduler thread\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_sched_start_thread(sched)) {
		ast_sched_context_destroy(sched);
		sched = NULL;
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(io = io_context_create())) {
		ast_log(LOG_ERROR, "Failed to create I/O context\n");
		ast_sched_context_destroy(sched);
		sched = NULL;
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(netsock = ast_netsock_list_alloc())) {
		ast_log(LOG_ERROR, "Failed to create netsock list\n");
		io_context_destroy(io);
		ast_sched_context_destroy(sched);
		sched = NULL;
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_netsock_init(netsock);

	outsock = ast_netsock_list_alloc();
	if (!outsock) {
		ast_log(LOG_ERROR, "Could not allocate outsock list.\n");
		io_context_destroy(io);
		ast_sched_context_destroy(sched);
		sched = NULL;
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_netsock_init(outsock);

	randomcalltokendata = ast_random();

	iax_set_output(iax_debug_output);
	iax_set_error(iax_error_output);
	jb_setoutput(jb_error_output, jb_warning_output, NULL);

	if ((timer = ast_timer_open())) {
		ast_timer_set_rate(timer, 1000 / trunkfreq);
	}

	if (set_config(config, 0, 0) == -1) {
		if (timer) {
			ast_timer_close(timer);
			timer = NULL;
		}
		return AST_MODULE_LOAD_DECLINE;
	}

#ifdef TEST_FRAMEWORK
	AST_TEST_REGISTER(test_iax2_peers_get);
	AST_TEST_REGISTER(test_iax2_users_get);
#endif

	/* Register AstData providers */
	ast_data_register_multiple(iax2_data_providers, ARRAY_LEN(iax2_data_providers));
	ast_cli_register_multiple(cli_iax2, ARRAY_LEN(cli_iax2));

	ast_register_application_xml(papp, iax2_prov_app);

	ast_custom_function_register(&iaxpeer_function);
	ast_custom_function_register(&iaxvar_function);

	ast_manager_register_xml("IAXpeers", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_iax2_show_peers);
	ast_manager_register_xml("IAXpeerlist", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_iax2_show_peer_list);
	ast_manager_register_xml("IAXnetstats", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_iax2_show_netstats);
	ast_manager_register_xml("IAXregistry", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_iax2_show_registry);

 	if (ast_channel_register(&iax2_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", "IAX2");
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_register_switch(&iax2_switch)) {
		ast_log(LOG_ERROR, "Unable to register IAX switch\n");
	}

	if (start_network_thread()) {
		ast_log(LOG_ERROR, "Unable to start network thread\n");
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	} else {
		ast_verb(2, "IAX Ready and Listening\n");
	}

	AST_LIST_LOCK(&registrations);
	AST_LIST_TRAVERSE(&registrations, reg, entry)
		iax2_do_register(reg);
	AST_LIST_UNLOCK(&registrations);

	ao2_callback(peers, 0, peer_set_sock_cb, NULL);
	ao2_callback(peers, 0, iax2_poke_peer_cb, NULL);


	iax_firmware_reload();
	iax_provision_reload(0);

	ast_realtime_require_field("iaxpeers", "name", RQ_CHAR, 10, "ipaddr", RQ_CHAR, 15, "port", RQ_UINTEGER2, 5, "regseconds", RQ_UINTEGER2, 6, SENTINEL);

	network_change_stasis_subscribe();

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Inter Asterisk eXchange (Ver 2)",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DRIVER,
		.nonoptreq = "res_crypto",
		);
