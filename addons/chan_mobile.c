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
 * \brief Bluetooth Mobile Device channel driver
 *
 * \author Dave Bowerman <david.bowerman@gmail.com>
 *
 * \ingroup channel_drivers
 */

/*! \li \ref chan_mobile.c uses the configuration file \ref chan_mobile.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page chan_mobile.conf chan_mobile.conf
 * \verbinclude chan_mobile.conf.sample
 */

/*** MODULEINFO
	<depend>bluetooth</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pthread.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sco.h>
#include <bluetooth/l2cap.h>

#include "asterisk/compat.h"
#include "asterisk/lock.h"
#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/cli.h"
#include "asterisk/devicestate.h"
#include "asterisk/causes.h"
#include "asterisk/dsp.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/message.h"
#include "asterisk/io.h"
#include "asterisk/smoother.h"
#include "asterisk/format_cache.h"

#define MBL_CONFIG "chan_mobile.conf"
#define MBL_CONFIG_OLD "mobile.conf"

#define DEVICE_FRAME_SIZE_DEFAULT 48
#define DEVICE_FRAME_SIZE_MAX 256
#define DEVICE_FRAME_FORMAT ast_format_slin
#define CHANNEL_FRAME_SIZE 80

/* SMS UCS-2 message limits */
#define SMS_UCS2_SINGLE_MAX 70       /*!< Max UCS-2 chars in single SMS (no UDH) */
#define SMS_UCS2_PART_MAX 67         /*!< Max UCS-2 chars per part (with UDH) */
#define SMS_MAX_PARTS 10             /*!< Maximum multi-part SMS segments */
#define SMS_UDH_HEX_LEN 12           /*!< UDH hex string length for concatenation */

#define SMS_CMTI_DELAY_MS 5000    /*!< Delay in ms before reading SMS after CMTI (for multi-part) */

static int discovery_interval = 60;			/*!< The device discovery interval, default 60 seconds. */
static pthread_t discovery_thread = AST_PTHREADT_NULL;	/*!< The discovery thread */
static sdp_session_t *sdp_session;

AST_MUTEX_DEFINE_STATIC(unload_mutex);
static int unloading_flag = 0;
static inline int check_unloading(void);
static inline void set_unloading(void);

enum mbl_type {
	MBL_TYPE_PHONE,
	MBL_TYPE_HEADSET
};

/* SMS operating modes */
enum sms_mode {
	SMS_MODE_OFF = 0,   /*!< Disabled via configuration */
	SMS_MODE_NO,        /*!< Not supported by device (AT commands failed) */
	SMS_MODE_TEXT,      /*!< Text mode (AT+CMGF=1) */
	SMS_MODE_PDU        /*!< PDU mode (AT+CMGF=0) */
};

/* Polling interval for status updates (5 minutes = 300000ms) */
#define STATUS_POLL_INTERVAL 300000

/* Device connection states */
enum mbl_state {
	MBL_STATE_INIT,         /* Just loaded from config */
	MBL_STATE_DISCONNECTED, /* Not connected */
	MBL_STATE_CONNECTING,   /* RFCOMM connection in progress */
	MBL_STATE_CONNECTED,    /* RFCOMM connected, initializing HFP/HSP */
	MBL_STATE_READY,        /* Fully initialized, ready for calls */
	MBL_STATE_RING,         /* Incoming call ringing */
	MBL_STATE_DIAL,         /* Outgoing call dialing */
	MBL_STATE_ACTIVE,       /* Call in progress */
	MBL_STATE_ERROR,        /* Error state */
};

/* Adapter states */
enum adapter_state {
	ADAPTER_STATE_INIT,      /* Just loaded */
	ADAPTER_STATE_NOT_FOUND, /* Adapter not available/connected */
	ADAPTER_STATE_READY,     /* Ready to connect devices */
	ADAPTER_STATE_BUSY,      /* Device connected and in use */
	ADAPTER_STATE_ERROR,     /* Error state */
};

struct adapter_pvt {
	int dev_id;					/*!< device id */
	int hci_socket;					/*!< device descriptor */
	char id[31];					/*!< the 'name' from mobile.conf */
	bdaddr_t addr;					/*!< address of adapter */
	enum adapter_state state;			/*!< adapter state */
	unsigned int inuse:1;				/*!< are we in use ? */
	unsigned int alignment_detection:1;		/*!< do alignment detection on this adapter? */
	struct io_context *io;				/*!< io context for audio connections */
	struct io_context *accept_io;			/*!< io context for sco listener */
	int *sco_id;					/*!< the io context id of the sco listener socket */
	int sco_socket;					/*!< sco listener socket */
	pthread_t sco_listener_thread;			/*!< sco listener thread */
	AST_LIST_ENTRY(adapter_pvt) entry;
};

static AST_RWLIST_HEAD_STATIC(adapters, adapter_pvt);

struct msg_queue_entry;
struct hfp_pvt;
struct mbl_pvt {
	struct ast_channel *owner;			/*!< Channel we belong to, possibly NULL */
	struct ast_frame fr;				/*!< "null" frame */
	ast_mutex_t lock;				/*!< pvt lock */
	/*! queue for messages we are expecting */
	AST_LIST_HEAD_NOLOCK(msg_queue, msg_queue_entry) msg_queue;
	enum mbl_type type;				/*!< Phone or Headset */
	enum mbl_state state;				/*!< Device state */
	char id[31];					/*!< The id from mobile.conf */
	char remote_name[32];				/*!< Remote device name */
	char profile_name[8];				/*!< "HFP" or "HSP" */
	int group;					/*!< group number for group dialling */
	bdaddr_t addr;					/*!< address of device */
	struct adapter_pvt *adapter;			/*!< the adapter we use */
	char context[AST_MAX_CONTEXT];			/*!< the context for incoming calls */
	struct hfp_pvt *hfp;				/*!< hfp pvt */
	int rfcomm_port;				/*!< rfcomm port number */
	int rfcomm_socket;				/*!< rfcomm socket descriptor */
	char rfcomm_buf[256];
	char io_buf[DEVICE_FRAME_SIZE_MAX + AST_FRIENDLY_OFFSET];
	struct ast_smoother *bt_out_smoother;			/*!< our bt_out_smoother, for making sco_mtu byte frames */
	struct ast_smoother *bt_in_smoother;			/*!< our smoother, for making "normal" CHANNEL_FRAME_SIZEed byte frames */
	int sco_socket;					/*!< sco socket descriptor */
	int sco_mtu;					/*!< negotiated SCO/eSCO packet size */
	int bt_ver;					/*!< Remote Bluetooth Version (LMP) */
	int mtu_sync_count;				/*!< for detecting eSCO packet size changes */
	pthread_t monitor_thread;			/*!< monitor thread handle */
	int timeout;					/*!< used to set the timeout for rfcomm data (may be used in the future) */
	unsigned int no_callsetup:1;
	enum sms_mode sms_mode;				/*!< SMS operating mode */
	unsigned int do_alignment_detection:1;
	unsigned int alignment_detection_triggered:1;
	unsigned int blackberry:1;
	short alignment_samples[4];
	int alignment_count;
	int ring_sched_id;
	int status_sched_id;			/*!\< scheduler ID for periodic status polling */
	struct ast_dsp *dsp;
	struct ast_sched_context *sched;
	int hangupcause;

	/* flags */
	unsigned int outgoing:1;	/*!< outgoing call */
	unsigned int incoming:1;	/*!< incoming call */
	unsigned int outgoing_sms:1;	/*!< outgoing sms */
	unsigned int incoming_sms:1;	/*!< outgoing sms */
	unsigned int needcallerid:1;	/*!< we need callerid */
	unsigned int needchup:1;	/*!< we need to send a chup */
	unsigned int needring:1;	/*!< we need to send a RING */
	unsigned int answered:1;	/*!< we sent/received an answer */
	unsigned int connected:1;	/*!< do we have an rfcomm connection to a device */
	unsigned int has_utf8:1;    /*!< device supports UTF-8 charset */
	unsigned int has_ucs2:1;    /*!< device supports UCS2 charset (hex-encoded Unicode) */
	unsigned int has_gsm:1;     /*!< device supports GSM 7-bit charset */
	unsigned int has_ira:1;     /*!< device supports IRA (ASCII) charset */
	unsigned int utf8_candidate:1; /*!< device might support UTF-8 */
	unsigned int profile_incompatible:1; /*!< device lacks required HS/HF profile */
	char cscs_active[16];       /*!< currently active charset */
	char cscs_list[128];        /*!< raw list of supported charsets from AT+CSCS=? */
	int sdp_fail_count;         /*!< count of consecutive SDP failures for profile detection */
	int hfp_init_fail_count;    /*!< count of consecutive HFP initialization failures */
	bdaddr_t last_checked_addr; /*!< last address we tried to connect - for detecting config changes */

	int sms_index_to_read;      /*!< SMS index to read after storage selection */
	char sms_storage_pending[4]; /*!< Storage currently being set via CPMS */
	unsigned int sms_delete_after_read:1; /*!< Delete SMS after reading */
	int sms_pending_indices[32]; /*!< Indices found by CMGL scan or CMTI queue */
	int sms_pending_count;       /*!< Number of pending indices */
	int sms_cmti_sched_id;       /*!< Scheduler ID for delayed CMTI read */
	unsigned int sms_send_in_progress:1; /*!< SMS send is in progress - reject new sends */

	/* CNMI smart configuration storage */
	int cnmi_mode_vals[10];      /*!< Valid mode values from AT+CNMI=? */
	int cnmi_mt_vals[10];        /*!< Valid mt values from AT+CNMI=? */
	int cnmi_bm_vals[10];        /*!< Valid bm values from AT+CNMI=? */
	int cnmi_ds_vals[10];        /*!< Valid ds values from AT+CNMI=? */
	int cnmi_bfr_vals[10];       /*!< Valid bfr values from AT+CNMI=? */
	int cnmi_selected[5];        /*!< Selected CNMI values [mode,mt,bm,ds,bfr] */
	unsigned int cnmi_test_done:1; /*!< AT+CNMI=? query completed */

	AST_LIST_ENTRY(mbl_pvt) entry;
};

/*! Structure used by hfp_parse_clip to return two items */
struct cidinfo {
	char *cnum;
	char *cnam;
};

static AST_RWLIST_HEAD_STATIC(devices, mbl_pvt);

static int handle_response_ok(struct mbl_pvt *pvt, char *buf);
static int handle_response_error(struct mbl_pvt *pvt, char *buf);
static int handle_response_ciev(struct mbl_pvt *pvt, char *buf);
static int handle_response_clip(struct mbl_pvt *pvt, char *buf);
static int handle_response_ring(struct mbl_pvt *pvt, char *buf);
static int handle_response_cmti(struct mbl_pvt *pvt, char *buf);
static void process_pending_sms(struct mbl_pvt *pvt);
static int mbl_cmti_delayed_read(const void *data);
static int handle_response_cmgr(struct mbl_pvt *pvt, char *buf);
static int handle_response_cmgl(struct mbl_pvt *pvt, char *buf);
static int handle_response_cusd(struct mbl_pvt *pvt, char *buf);
static int handle_response_busy(struct mbl_pvt *pvt);
static int handle_response_no_dialtone(struct mbl_pvt *pvt, char *buf);
static int handle_response_no_carrier(struct mbl_pvt *pvt, char *buf);
static int handle_sms_prompt(struct mbl_pvt *pvt, char *buf);

/* PDU logging helpers */
static void log_pdu_submit(const char *pvt_id, const char *pdu_hex);
static void log_pdu_deliver(const char *pvt_id, const char *pdu_hex);

/* CLI stuff */
static char *handle_cli_mobile_show_devices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_show_adapters(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_show_adapter(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_search(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_rfcomm(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_cusd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_show_device(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry mbl_cli[] = {
	AST_CLI_DEFINE(handle_cli_mobile_show_devices,  "Show Bluetooth Cell / Mobile devices"),
	AST_CLI_DEFINE(handle_cli_mobile_show_device,   "Show detailed Bluetooth device status"),
	AST_CLI_DEFINE(handle_cli_mobile_show_adapters, "Show Bluetooth adapters"),
	AST_CLI_DEFINE(handle_cli_mobile_show_adapter,  "Show detailed Bluetooth adapter info"),
	AST_CLI_DEFINE(handle_cli_mobile_search,        "Search for Bluetooth Cell / Mobile devices"),
	AST_CLI_DEFINE(handle_cli_mobile_rfcomm,        "Send commands to the rfcomm port for debugging"),
	AST_CLI_DEFINE(handle_cli_mobile_cusd,          "Send CUSD commands to the mobile"),
};

/*** DOCUMENTATION
	<function name="MOBILE_STATUS" language="en_US">
		<synopsis>
			Get the status of a Bluetooth mobile device.
		</synopsis>
		<syntax>
			<parameter name="Device" required="true">
				<para>ID of the mobile device from <filename>chan_mobile.conf</filename></para>
			</parameter>
			<parameter name="Type" required="false">
				<para>Optional status type to query. If not specified, defaults to CONNECTION.</para>
				<enumlist>
					<enum name="CONNECTION">
						<para>Connection status:</para>
						<enumlist>
							<enum name="DISCONNECTED"><para>Device is not connected</para></enum>
							<enum name="CONNECTED_FREE"><para>Connected and available for calls</para></enum>
							<enum name="CONNECTED_BUSY"><para>Connected but busy with a call</para></enum>
						</enumlist>
					</enum>
					<enum name="SIGNAL">
						<para>Signal strength (0-5)</para>
					</enum>
					<enum name="ROAM">
						<para>Roaming status:</para>
						<enumlist>
							<enum name="NOT_ROAMING"><para>Not roaming</para></enum>
							<enum name="ROAMING"><para>Roaming</para></enum>
						</enumlist>
					</enum>
					<enum name="PROVIDER">
						<para>Operator/provider name</para>
					</enum>
					<enum name="MCCMNC">
						<para>Mobile Country Code / Mobile Network Code</para>
					</enum>
					<enum name="REGSTATUS">
						<para>Network registration status (per 3GPP TS 27.007):</para>
						<enumlist>
							<enum name="NOT_REGISTERED"><para>Not registered, not searching</para></enum>
							<enum name="REGISTERED_HOME"><para>Registered on home network</para></enum>
							<enum name="SEARCHING"><para>Not registered, searching for network</para></enum>
							<enum name="DENIED"><para>Registration denied</para></enum>
							<enum name="UNKNOWN"><para>Unknown (out of coverage)</para></enum>
							<enum name="REGISTERED_ROAMING"><para>Registered, roaming</para></enum>
						</enumlist>
					</enum>
					<enum name="BATTERY">
						<para>Battery level (0-100 percent)</para>
					</enum>
					<enum name="CHARGING">
						<para>Charging status:</para>
						<enumlist>
							<enum name="NOT_CHARGING"><para>Not charging</para></enum>
							<enum name="CHARGING"><para>Charging</para></enum>
						</enumlist>
					</enum>
			</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>This function retrieves status information from a Bluetooth mobile device.
			The device must be configured in <filename>chan_mobile.conf</filename> and connected.
			If the requested status type is not yet available, an empty string is returned.</para>
			<example title="Query connection status">
			Set(status=${MOBILE_STATUS(myphone)})
			</example>
			<example title="Query signal strength">
			Set(signal=${MOBILE_STATUS(myphone,SIGNAL)})
			</example>
		</description>
	</function>
	<application name="MobileSendSMS" language="en_US">
		<synopsis>
			Send an SMS message via a Bluetooth mobile device.
		</synopsis>
		<syntax>
			<parameter name="Device" required="true">
				<para>ID of the mobile device from <filename>chan_mobile.conf</filename></para>
			</parameter>
			<parameter name="Destination" required="true">
				<para>Destination phone number for the SMS message</para>
			</parameter>
			<parameter name="Message" required="true">
				<para>Text of the message to send</para>
			</parameter>
		</syntax>
		<description>
			<para>This application sends an SMS text message using the specified Bluetooth
			mobile device. The device must be configured in <filename>chan_mobile.conf</filename>,
			connected, and support SMS functionality.</para>
		</description>
	</application>
	<info name="Mobile_Message_Technology" language="en_US" tech="mobile">
		<para>The <literal>mobile</literal> message technology allows sending SMS messages
		through Bluetooth mobile devices using Asterisk's MESSAGE framework. This provides
		an alternative to the <literal>MobileSendSMS</literal> application with better
		integration into Asterisk's messaging infrastructure.</para>
		<para>Messages are sent using the <literal>MessageSend()</literal> application
		with a special URI format that specifies both the device and destination number.</para>
		<xi:include xpointer="xpointer(/docs/info[@name='Mobile_Message_URI'])" />
	</info>
	<info name="Mobile_Message_URI" language="en_US" tech="mobile">
		<para><emphasis>URI Format:</emphasis></para>
		<para><literal>mobile:device_id/phone_number</literal></para>
		<para>Where:</para>
		<para>- <literal>device_id</literal> is the device identifier from <filename>chan_mobile.conf</filename></para>
		<para>- <literal>phone_number</literal> is the destination phone number (international format recommended)</para>
		<para><emphasis>Example Usage:</emphasis></para>
		<para>Send an SMS via device 'phone1' to +15551234567:</para>
		<para><literal>MessageSend(mobile:phone1/+15551234567,Hello from Asterisk!)</literal></para>
		<para><emphasis>Comparison with MobileSendSMS:</emphasis></para>
		<para>The MESSAGE technology offers several advantages over the <literal>MobileSendSMS</literal> application:</para>
		<para>- Integration with Asterisk's MESSAGE routing and manipulation framework</para>
		<para>- Support for message queuing and delivery status tracking</para>
		<para>- Ability to route messages through dialplan based on URI patterns</para>
		<para>- Better handling of Unicode and multi-part messages in PDU mode</para>
		<para>However, <literal>MobileSendSMS</literal> remains available for simple use cases
		and backward compatibility with existing dialplans.</para>
	</info>
 ***/

/* App stuff */
static char *app_mblsendsms = "MobileSendSMS";

/* Function read callback - forward declaration */
static int mbl_status_read(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len);

static struct ast_custom_function mobile_status_function = {
	.name = "MOBILE_STATUS",
	.read = mbl_status_read,
};

static struct ast_channel *mbl_new(int state, struct mbl_pvt *pvt, struct cidinfo *cidinfo,
		const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor);
static struct ast_channel *mbl_request(const char *type, struct ast_format_cap *cap,
		const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int mbl_call(struct ast_channel *ast, const char *dest, int timeout);
static int mbl_hangup(struct ast_channel *ast);
static int mbl_answer(struct ast_channel *ast);
static int mbl_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static struct ast_frame *mbl_read(struct ast_channel *ast);
static int mbl_write(struct ast_channel *ast, struct ast_frame *frame);
static int mbl_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int mbl_devicestate(const char *data);

static void do_alignment_detection(struct mbl_pvt *pvt, char *buf, int buflen);

static int mbl_queue_control(struct mbl_pvt *pvt, enum ast_control_frame_type control);
static int mbl_queue_hangup(struct mbl_pvt *pvt);
static int mbl_ast_hangup(struct mbl_pvt *pvt);
static int mbl_has_service(struct mbl_pvt *pvt);

static int rfcomm_connect(bdaddr_t src, bdaddr_t dst, int remote_channel);
static int rfcomm_write(int rsock, char *buf);
static int rfcomm_write_full(int rsock, char *buf, size_t count);
static int rfcomm_wait(int rsock, int *ms);
static ssize_t rfcomm_read(int rsock, char *buf, size_t count);

static int sco_connect(bdaddr_t src, bdaddr_t dst, int *mtu);
static int sco_write(int s, char *buf, int len);
static int sco_accept(int *id, int fd, short events, void *data);
static int sco_bind(struct adapter_pvt *adapter);

static void *do_sco_listen(void *data);
static int sdp_search(char *addr, int profile);

static int headset_send_ring(const void *data);
static int mbl_status_poll(const void *data);

/*
 * bluetooth handsfree profile helpers
 */

#define HFP_HF_ECNR	(1 << 0)
#define HFP_HF_CW	(1 << 1)
#define HFP_HF_CID	(1 << 2)
#define HFP_HF_VOICE	(1 << 3)
#define HFP_HF_VOLUME	(1 << 4)
#define HFP_HF_STATUS	(1 << 5)
#define HFP_HF_CONTROL	(1 << 6)

#define HFP_AG_CW	(1 << 0)
#define HFP_AG_ECNR	(1 << 1)
#define HFP_AG_VOICE	(1 << 2)
#define HFP_AG_RING	(1 << 3)
#define HFP_AG_TAG	(1 << 4)
#define HFP_AG_REJECT	(1 << 5)
#define HFP_AG_STATUS	(1 << 6)
#define HFP_AG_CONTROL	(1 << 7)
#define HFP_AG_ERRORS	(1 << 8)

/* HFP 1.6+ AG features */
#define HFP_AG_CODEC	(1 << 9)	/* Codec negotiation (HFP 1.6) */
#define HFP_AG_HFIND	(1 << 10)	/* HF indicators (HFP 1.7) */
#define HFP_AG_ESCO_S4	(1 << 11)	/* eSCO S4 settings (HFP 1.7) */

#define HFP_CIND_UNKNOWN	(-1)
#define HFP_CIND_NONE		0
#define HFP_CIND_SERVICE	1
#define HFP_CIND_CALL		2
#define HFP_CIND_CALLSETUP	3
#define HFP_CIND_CALLHELD	4
#define HFP_CIND_SIGNAL		5
#define HFP_CIND_ROAM		6
#define HFP_CIND_BATTCHG	7

/* call indicator values */
#define HFP_CIND_CALL_NONE	0
#define HFP_CIND_CALL_ACTIVE	1

/* callsetup indicator values */
#define HFP_CIND_CALLSETUP_NONE		0
#define HFP_CIND_CALLSETUP_INCOMING	1
#define HFP_CIND_CALLSETUP_OUTGOING	2
#define HFP_CIND_CALLSETUP_ALERTING	3

/* service indicator values */
#define HFP_CIND_SERVICE_NONE		0
#define HFP_CIND_SERVICE_AVAILABLE	1

/*!
 * \brief This struct holds HFP features that we support.
 */
struct hfp_hf {
	int ecnr:1;	/*!< echo-cancel/noise reduction */
	int cw:1;	/*!< call waiting and three way calling */
	int cid:1;	/*!< cli presentation (callier id) */
	int voice:1;	/*!< voice recognition activation */
	int volume:1;	/*!< remote volume control */
	int status:1;	/*!< enhanced call status */
	int control:1;	/*!< enhanced call control*/
};

/*!
 * \brief This struct holds HFP features the AG supports.
 */
struct hfp_ag {
	int cw:1;	/*!< three way calling */
	int ecnr:1;	/*!< echo-cancel/noise reduction */
	int voice:1;	/*!< voice recognition */
	int ring:1;	/*!< in band ring tone capability */
	int tag:1;	/*!< attach a number to a voice tag */
	int reject:1;	/*!< ability to reject a call */
	int status:1;	/*!< enhanced call status */
	int control:1;	/*!< enhanced call control*/
	int errors:1;	/*!< extended error result codes*/
};

/*!
 * \brief This struct holds mappings for indications.
 */
struct hfp_cind {
	int service;	/*!< whether we have service or not */
	int call;	/*!< call state */
	int callsetup;	/*!< bluetooth call setup indications */
	int callheld;	/*!< bluetooth call hold indications */
	int signal;	/*!< signal strength */
	int roam;	/*!< roaming indicator */
	int battchg;	/*!< battery charge indicator */
};

/*!
 * \brief This struct holds state information about the current hfp connection.
 */
struct hfp_pvt {
	struct mbl_pvt *owner;		/*!< the mbl_pvt struct that owns this struct */
	int initialized:1;		/*!< whether a service level connection exists or not */
	int nocallsetup:1;		/*!< whether we detected a callsetup indicator */
	struct hfp_ag brsf;		/*!< the supported feature set of the AG */
	int cind_index[16];		/*!< the cind/ciev index to name mapping for this AG */
	int cind_state[16];		/*!< the cind/ciev state for this AG */
	struct hfp_cind cind_map;	/*!< the cind name to index mapping for this AG */
	int rsock;			/*!< our rfcomm socket */
	int rport;			/*!< our rfcomm port */
	int sent_alerting;		/*!< have we sent alerting? */
	int hfp_version;		/*!< detected HFP version: 10=1.0, 15=1.5, 16=1.6, 17=1.7 */
	int brsf_raw;			/*!< raw BRSF value from phone */

	/* Network registration status */
	int creg;			/*!< Circuit switched registration status (0-5) */
	int cgreg;			/*!< Packet switched registration status (0-5) */

	/* Operator information */
	char provider_name[64];		/*!< Operator name from AT+COPS */
	char mccmnc[10];		/*!< MCC/MNC code from AT+COPS format 2 */

	/* Battery status */
	int battery_percent;		/*!< Battery level 0-100, or -1 if unknown */
	int charging;			/*!< 0=discharging, 1=charging, -1=unknown */

	/* Capability flags for unsupported commands */
	unsigned int no_creg:1;		/*!< Device doesn't support AT+CREG */
	unsigned int no_cgreg:1;	/*!< Device doesn't support AT+CGREG */
	unsigned int no_cops:1;		/*!< Device doesn't support AT+COPS */
	unsigned int no_cbc:1;		/*!< Device doesn't support AT+CBC */
	unsigned int no_cind_signal:1; /*!< Device missing signal indicator in CIND */
};

/* Our supported features.
 * we only support caller id
 */
static struct hfp_hf hfp_our_brsf = {
	.ecnr = 0,
	.cw = 0,
	.cid = 1,
	.voice = 0,
	.volume = 0,
	.status = 0,
	.control = 0,
};

static int hfp_parse_ciev(struct hfp_pvt *hfp, char *buf, int *value);
static struct cidinfo hfp_parse_clip(struct hfp_pvt *hfp, char *buf);
static int parse_next_token(char string[], const int start, const char delim);
static int hfp_parse_cmti_full(struct hfp_pvt *hfp, char *buf, char *mem);
static int hfp_parse_cmgr(struct hfp_pvt *hfp, char *buf, char **from_number, char **from_name, char **text);
static int hfp_parse_brsf(struct hfp_pvt *hfp, const char *buf);
static int hfp_parse_cind(struct hfp_pvt *hfp, char *buf);
static int hfp_parse_cind_test(struct hfp_pvt *hfp, char *buf);
static char *hfp_parse_cusd(struct hfp_pvt *hfp, char *buf);
static int hfp_parse_cscs(struct hfp_pvt *hfp, char *buf, struct mbl_pvt *pvt);
static int hfp_parse_creg(char *buf);
static int hfp_parse_cops(char *buf, char *oper, size_t oper_len, int *format);
static int hfp_parse_cbc(char *buf, int *level, int *charging);

static int hfp_brsf2int(struct hfp_hf *hf);
static struct hfp_ag *hfp_int2brsf(int brsf, struct hfp_ag *ag);

static int hfp_send_brsf(struct hfp_pvt *hfp, struct hfp_hf *brsf);
static int hfp_send_cind(struct hfp_pvt *hfp);
static int hfp_send_cind_test(struct hfp_pvt *hfp);
static int hfp_send_cmer(struct hfp_pvt *hfp, int status);
static int hfp_send_clip(struct hfp_pvt *hfp, int status);
static int hfp_send_vgs(struct hfp_pvt *hfp, int value);

#if 0
static int hfp_send_vgm(struct hfp_pvt *hfp, int value);
#endif
static int hfp_send_dtmf(struct hfp_pvt *hfp, char digit);
static int hfp_send_cmgf(struct hfp_pvt *hfp, int mode);
static int hfp_send_cnmi(struct hfp_pvt *hfp, int mode);
static int hfp_send_cmgr(struct hfp_pvt *hfp, int index);
static int hfp_send_cpms(struct hfp_pvt *hfp, const char *mem);
static int hfp_send_cmgs(struct hfp_pvt *hfp, const char *number);
static int hfp_send_sms_text(struct hfp_pvt *hfp, const char *message);
static int hfp_send_chup(struct hfp_pvt *hfp);
static int hfp_send_atd(struct hfp_pvt *hfp, const char *number);
static int hfp_send_ata(struct hfp_pvt *hfp);
static int hfp_send_cusd(struct hfp_pvt *hfp, const char *code);
static int hfp_send_cscs(struct hfp_pvt *hfp, const char *charset);
static int hfp_send_creg(struct hfp_pvt *hfp, int mode);
static int hfp_send_cgreg(struct hfp_pvt *hfp, int mode);
static int hfp_send_cops(struct hfp_pvt *hfp, int format, int query);
static int hfp_send_cbc(struct hfp_pvt *hfp);

/* Encoding conversion helpers */
static int utf8_to_ucs2_hex(const char *utf8, char *hex, size_t hexlen);
static int ucs2_hex_to_utf8(const char *hex, char *utf8, size_t utf8len);
static int is_gsm7_compatible(const char *text);

/* SMS mode helper */
static inline const char *sms_mode_to_str(enum sms_mode mode);

/* PDU encoding/decoding for SMS */
static int sms_encode_pdu(const char *dest, const char *message, int use_ucs2, char *pdu_out, size_t pdu_len);
static int sms_decode_pdu(const char *pdu_hex, char *from_number, size_t from_len, char *message, size_t msg_len);
static int gsm7_encode(const char *utf8, unsigned char *gsm7, size_t gsm7_len, int *septets);
static int gsm7_decode(const unsigned char *gsm7, int septets, char *utf8, size_t utf8_len);

/* SMS-specific UDH (User Data Header) helpers for multi-part messages */
static const char *sms_strip_udh_hex(const char *hex);
static int sms_generate_concat_udh_hex(int ref, int total_parts, int part_num, char *udh_hex);
static int sms_get_next_concat_ref(void);

/*
 * bluetooth headset profile helpers
 */
static int hsp_send_ok(int rsock);
static int hsp_send_error(int rsock);
static int hsp_send_vgs(int rsock, int gain);
static int hsp_send_vgm(int rsock, int gain);
static int hsp_send_ring(int rsock);

/*
 * Hayes AT command helpers
 */
enum at_message {
	/* errors */
	AT_PARSE_ERROR = -2,
	AT_READ_ERROR = -1,
	AT_UNKNOWN = 0,
	/* at responses */
	AT_OK,
	AT_ERROR,
	AT_RING,
	AT_BRSF,
	AT_CIND,
	AT_CIEV,
	AT_CLIP,
	AT_CMTI,
	AT_CMGR,
	AT_SMS_PROMPT,
	AT_CMS_ERROR,
	/* at commands */
	AT_A,
	AT_D,
	AT_CHUP,
	AT_CKPD,
	AT_CMGS,
	AT_VGM,
	AT_VGS,
	AT_VTS,
	AT_CMGF,
	AT_CMGD,		/* AT+CMGD */
	AT_CMGF_PDU,		/* AT+CMGF=0 (PDU mode fallback) */
	AT_CNMI,
	AT_CNMI_FALLBACK1,	/* First CNMI fallback (1,1,0,0,0) */
	AT_CNMI_FALLBACK2,	/* Second CNMI fallback (1,2,0,0,0) */
	AT_CNMI_FALLBACK3,	/* Third CNMI fallback (3,1,0,0,0) - mode 3 for link-active only */
	AT_CMER,
	AT_CIND_TEST,
	AT_CUSD,
	AT_BUSY,
	AT_NO_DIALTONE,
	AT_NO_CARRIER,
	AT_ECAM,
	AT_CSCS,
	AT_CSCS_SET,
	AT_CSCS_VERIFY,
	/* Device status commands */
	AT_CREG,		/* +CREG response */
	AT_CREG_SET,		/* AT+CREG=1 command */
	AT_CGREG,		/* +CGREG response */
	AT_CGREG_SET,		/* AT+CGREG=1 command */
	AT_COPS,		/* +COPS response */
	AT_COPS_SET_NUMERIC,	/* AT+COPS=3,2 (numeric format) */
	AT_COPS_SET_ALPHA,	/* AT+COPS=3,0 (alphanumeric format) */
	AT_COPS_QUERY,		/* AT+COPS? query */
	AT_COPS_DONE,		/* COPS query chain completed */
	AT_COPS_FALLBACK,	/* AT+COPS? query (fallback after set failure) */
	AT_CBC,			/* +CBC response */
	/* SMS polling commands */
	AT_CNMI_TEST,		/* AT+CNMI=? (test supported values) */
	AT_CNMI_QUERY,		/* AT+CNMI? (query current settings) */
	AT_CPMS,		/* AT+CPMS (set message storage) */
	AT_CMGL,		/* AT+CMGL (list messages) */
	AT_CSQ,			/* +CSQ response / AT+CSQ command */
};

static int at_match_prefix(char *buf, char *prefix);
static enum at_message at_read_full(int rsock, char *buf, size_t count);
static inline const char *at_msg2str(enum at_message msg);

struct msg_queue_entry {
	enum at_message expected;
	enum at_message response_to;
	void *data;

	AST_LIST_ENTRY(msg_queue_entry) entry;
};

static int msg_queue_push(struct mbl_pvt *pvt, enum at_message expect, enum at_message response_to);
static int msg_queue_push_data(struct mbl_pvt *pvt, enum at_message expect, enum at_message response_to, void *data);
static struct msg_queue_entry *msg_queue_pop(struct mbl_pvt *pvt);
static void msg_queue_free_and_pop(struct mbl_pvt *pvt);
static void msg_queue_flush(struct mbl_pvt *pvt);
static struct msg_queue_entry *msg_queue_head(struct mbl_pvt *pvt);

/*
 * channel stuff
 */

static struct ast_channel_tech mbl_tech = {
	.type = "Mobile",
	.description = "Bluetooth Mobile Device Channel Driver",
	.requester = mbl_request,
	.call = mbl_call,
	.hangup = mbl_hangup,
	.answer = mbl_answer,
	.send_digit_end = mbl_digit_end,
	.read = mbl_read,
	.write = mbl_write,
	.fixup = mbl_fixup,
	.devicestate = mbl_devicestate
};

/*
 * State helper functions
 */

static const char *mbl_state2str(enum mbl_state state)
{
	switch (state) {
	case MBL_STATE_INIT:         return "Init";
	case MBL_STATE_DISCONNECTED: return "Disconnected";
	case MBL_STATE_CONNECTING:   return "Connecting";
	case MBL_STATE_CONNECTED:    return "Connected";
	case MBL_STATE_READY:        return "Ready";
	case MBL_STATE_RING:         return "Ring";
	case MBL_STATE_DIAL:         return "Dial";
	case MBL_STATE_ACTIVE:       return "Active";
	case MBL_STATE_ERROR:        return "Error";
	default:                     return "Unknown";
	}
}

static const char *adapter_state2str(enum adapter_state state)
{
	switch (state) {
	case ADAPTER_STATE_INIT:      return "Init";
	case ADAPTER_STATE_NOT_FOUND: return "NotFound";
	case ADAPTER_STATE_READY:     return "Ready";
	case ADAPTER_STATE_BUSY:      return "Busy";
	case ADAPTER_STATE_ERROR:     return "Error";
	default:                      return "Unknown";
	}
}

static void mbl_set_state(struct mbl_pvt *pvt, enum mbl_state new_state)
{
	if (pvt->state != new_state) {
		ast_verb(3, "[%s] State: %s -> %s\n", pvt->id,
			mbl_state2str(pvt->state), mbl_state2str(new_state));
		pvt->state = new_state;
	}
}

/* Convert LMP version to Bluetooth version string */
static const char *mbl_lmp_vertostr(int lmp_ver)
{
	switch (lmp_ver) {
	case 0: return "1.0b";
	case 1: return "1.1";
	case 2: return "1.2";
	case 3: return "2.0";
	case 4: return "2.1";
	case 5: return "3.0";
	case 6: return "4.0";
	case 7: return "4.1";
	case 8: return "4.2";
	case 9: return "5.0";
	case 10: return "5.1";
	case 11: return "5.2";
	case 12: return "5.3";
	case 13: return "5.4";
	default: return "?";
	}
}

/* Convert network registration status to human-readable string */
static const char *regstatus_to_str(int status)
{
	switch (status) {
	case 0: return "Not Registered";
	case 1: return "Registered (Home)";
	case 2: return "Searching";
	case 3: return "Denied";
	case 4: return "Unknown";
	case 5: return "Registered (Roaming)";
	default: return "N/A";
	}
}

/* Generate visual signal strength bar */
static const char *signal_bar(int level)
{
	static char bar[16];
	int i, max = 5;
	level = (level > max) ? max : (level < 0) ? 0 : level;
	bar[0] = '[';
	for (i = 0; i < max; i++) {
		bar[i + 1] = (i < level) ? '|' : ' ';
	}
	bar[max + 1] = ']';
	bar[max + 2] = '\0';
	return bar;
}

/* CLI Commands implementation */

static char *handle_cli_mobile_show_devices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mbl_pvt *pvt;
	char bdaddr[18];

#define FORMAT1 "%-12.12s %-17.17s %-14.14s %-8.8s %-3.3s %-8.8s %-6.6s %-3.3s %-10.10s %-12.12s\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile show devices";
		e->usage =
			"Usage: mobile show devices\n"
			"       Shows the state of Bluetooth Cell / Mobile devices.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, FORMAT1, "ID", "Address", "Operator", "Profile", "SMS", "Encoding", "Batt", "Sig", "State", "Name");
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		char sms_status[4];
		char batt_str[7];
		char sig_str[4];
		char profile[12];
		char encoding[9];
		char oper[15];

		ast_mutex_lock(&pvt->lock);
		ba2str(&pvt->addr, bdaddr);

		/* Show HFP version if detected, otherwise profile name */
		if (pvt->hfp && pvt->hfp->hfp_version > 0) {
			snprintf(profile, sizeof(profile), "HFP %d.%d",
				pvt->hfp->hfp_version / 10, pvt->hfp->hfp_version % 10);
		} else if (pvt->profile_name[0]) {
			ast_copy_string(profile, pvt->profile_name, sizeof(profile));
		} else {
			ast_copy_string(profile, "-", sizeof(profile));
		}

		/* SMS status */
		ast_copy_string(sms_status, sms_mode_to_str(pvt->sms_mode), sizeof(sms_status));
		
		/* Encoding */
		ast_copy_string(encoding, pvt->cscs_active[0] ? pvt->cscs_active : "Default", sizeof(encoding));

		/* Battery status */
		if (pvt->hfp && pvt->hfp->initialized) {
			if (pvt->hfp->battery_percent >= 0) {
				snprintf(batt_str, sizeof(batt_str), "%d%%", pvt->hfp->battery_percent);
			} else {
				int batt = pvt->hfp->cind_state[pvt->hfp->cind_map.battchg];
				snprintf(batt_str, sizeof(batt_str), "~%d%%", batt * 20);
			}
		} else {
			ast_copy_string(batt_str, "-", sizeof(batt_str));
		}

		/* Signal level */
		if (pvt->hfp && pvt->hfp->initialized) {
			int sig = pvt->hfp->cind_state[pvt->hfp->cind_map.signal];
			snprintf(sig_str, sizeof(sig_str), "%d", sig);
		} else {
			ast_copy_string(sig_str, "-", sizeof(sig_str));
		}

		/* Operator name - fallback to MCC/MNC if name not available */
		if (pvt->hfp && pvt->hfp->provider_name[0]) {
			ast_copy_string(oper, pvt->hfp->provider_name, sizeof(oper));
		} else if (pvt->hfp && pvt->hfp->mccmnc[0]) {
			ast_copy_string(oper, pvt->hfp->mccmnc, sizeof(oper));
		} else {
			ast_copy_string(oper, "-", sizeof(oper));
		}

		ast_cli(a->fd, FORMAT1,
				pvt->id,
				bdaddr,
				oper,
				profile,
				sms_status,
				encoding,
				batt_str,
				sig_str,
				mbl_state2str(pvt->state),
				pvt->remote_name[0] ? pvt->remote_name : "-"
		       );
		ast_mutex_unlock(&pvt->lock);
	}
	AST_RWLIST_UNLOCK(&devices);

#undef FORMAT1

	return CLI_SUCCESS;
}

static char *handle_cli_mobile_show_device(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mbl_pvt *pvt;
	char bdaddr[18];

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile show device";
		e->usage =
			"Usage: mobile show device <device_id>\n"
			"       Shows detailed status for a Bluetooth device.\n";
		return NULL;
	case CLI_GENERATE:
		/* Tab completion for device IDs */
		if (a->pos == 3) {
			int wordlen = strlen(a->word);
			int which = 0;
			AST_RWLIST_RDLOCK(&devices);
			AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
				if (!strncasecmp(a->word, pvt->id, wordlen) && ++which > a->n) {
					char *ret = ast_strdup(pvt->id);
					AST_RWLIST_UNLOCK(&devices);
					return ret;
				}
			}
			AST_RWLIST_UNLOCK(&devices);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, a->argv[3])) {
			break;
		}
	}

	if (!pvt) {
		ast_cli(a->fd, "Device '%s' not found\n", a->argv[3]);
		AST_RWLIST_UNLOCK(&devices);
		return CLI_SUCCESS;
	}

	ast_mutex_lock(&pvt->lock);
	ba2str(&pvt->addr, bdaddr);

	ast_cli(a->fd, "Device: %s\n", pvt->id);
	ast_cli(a->fd, "Address: %s\n", bdaddr);
	ast_cli(a->fd, "Name: %s\n", pvt->remote_name[0] ? pvt->remote_name : "-");
	ast_cli(a->fd, "Type: %s\n", pvt->type == MBL_TYPE_PHONE ? "Phone" : "Headset");
	ast_cli(a->fd, "State: %s\n", mbl_state2str(pvt->state));
	ast_cli(a->fd, "Profile: %s\n", pvt->profile_name[0] ? pvt->profile_name : "-");

	if (pvt->hfp) {
		if (pvt->hfp->hfp_version > 0) {
			ast_cli(a->fd, "HFP Version: %d.%d\n",
				pvt->hfp->hfp_version / 10, pvt->hfp->hfp_version % 10);
		}

		if (pvt->hfp->initialized) {
			int sig = pvt->hfp->cind_state[pvt->hfp->cind_map.signal];
			int roam = pvt->hfp->cind_state[pvt->hfp->cind_map.roam];

			ast_cli(a->fd, "Signal: %d %s\n", sig, signal_bar(sig));
			ast_cli(a->fd, "Roaming: %s\n", roam ? "Yes" : "No");

			/* Battery - prefer CBC if available, fallback to HFP indicator */
			if (pvt->hfp->battery_percent >= 0) {
				const char *chrg = pvt->hfp->charging == 1 ? "Charging" :
				                   pvt->hfp->charging == 0 ? "Discharging" : "Unknown";
				ast_cli(a->fd, "Battery: %d%% (%s)\n", pvt->hfp->battery_percent, chrg);
			} else {
				int batt = pvt->hfp->cind_state[pvt->hfp->cind_map.battchg];
				ast_cli(a->fd, "Battery: ~%d%% (HFP)\n", batt * 20);
			}

			/* Provider information */
			if (pvt->hfp->provider_name[0]) {
				ast_cli(a->fd, "Provider: %s\n", pvt->hfp->provider_name);
			}
			if (pvt->hfp->mccmnc[0]) {
				ast_cli(a->fd, "MCC/MNC: %s\n", pvt->hfp->mccmnc);
			}

			/* Registration status */
			if (!pvt->hfp->no_creg) {
				ast_cli(a->fd, "CS Registration: %s\n", regstatus_to_str(pvt->hfp->creg));
			}
			if (!pvt->hfp->no_cgreg) {
				ast_cli(a->fd, "PS Registration: %s\n", regstatus_to_str(pvt->hfp->cgreg));
			}
		}
	}

	if (pvt->sco_mtu > 0) {
		ast_cli(a->fd, "SCO MTU: %d\n", pvt->sco_mtu);
	}
	if (pvt->bt_ver > 0) {
		ast_cli(a->fd, "BT Version: %s\n", mbl_lmp_vertostr(pvt->bt_ver));
	}

	/* SMS and charset capabilities */
	ast_cli(a->fd, "SMS Support: %s\n", sms_mode_to_str(pvt->sms_mode));
	ast_cli(a->fd, "Active Charset: %s\n", pvt->cscs_active[0] ? pvt->cscs_active : "-");
	ast_cli(a->fd, "Supported Charsets: %s\n", pvt->cscs_list[0] ? pvt->cscs_list : "-");

	ast_mutex_unlock(&pvt->lock);
	AST_RWLIST_UNLOCK(&devices);

	return CLI_SUCCESS;
}

static char *handle_cli_mobile_show_adapters(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct adapter_pvt *adapter;
	char bdaddr[18];
	int ctl_sock;

#define FORMAT1 "%-10.10s %-17.17s %-8.8s %-5.5s %-5.5s %-8.8s %-5.5s\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile show adapters";
		e->usage =
			"Usage: mobile show adapters\n"
			"       Shows the state of Bluetooth adapters.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ctl_sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);

	ast_cli(a->fd, FORMAT1, "ID", "Address", "State", "InUse", "Power", "RFKill", "BTVer");
	AST_RWLIST_RDLOCK(&adapters);
	AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
		const char *power_status = "-";
		const char *rfkill_status = "-";
		const char *bt_version = "-";
		ba2str(&adapter->addr, bdaddr);

		/* Get power status and BT version if we have a valid device */
		if (ctl_sock >= 0 && adapter->dev_id >= 0) {
			struct hci_dev_info di;
			memset(&di, 0, sizeof(di));
			di.dev_id = adapter->dev_id;
			if (ioctl(ctl_sock, HCIGETDEVINFO, &di) == 0) {
				/* Verify this is still our adapter by checking address */
				if (bacmp(&di.bdaddr, &adapter->addr) == 0) {
					power_status = (di.flags & (1 << HCI_UP)) ? "UP" : "DOWN";

					/* Read BT version if adapter is UP */
					if (adapter->hci_socket >= 0) {
						struct hci_version ver;
						if (hci_read_local_version(adapter->hci_socket, &ver, 1000) == 0) {
							bt_version = mbl_lmp_vertostr(ver.lmp_ver);
						}
					}
				} else {
					/* Address doesn't match - adapter was replaced or removed */
					power_status = "Gone";
				}
			} else {
				/* ioctl failed - device not found */
				power_status = "Gone";
			}
		}

		/* Check rfkill status from sysfs - only if adapter is present */
		if (adapter->dev_id >= 0 && strcmp(power_status, "Gone") != 0) {
			char hci_path[128];
			DIR *hci_dir;

			snprintf(hci_path, sizeof(hci_path), "/sys/class/bluetooth/hci%d", adapter->dev_id);
			hci_dir = opendir(hci_path);
			if (hci_dir) {
				struct dirent *entry;
				while ((entry = readdir(hci_dir)) != NULL) {
					if (strncmp(entry->d_name, "rfkill", 6) == 0) {
						char soft_path[256], hard_path[256];
						int soft = 0, hard = 0;
						FILE *f;

						/* Read soft block */
						snprintf(soft_path, sizeof(soft_path), "%s/%s/soft", hci_path, entry->d_name);
						f = fopen(soft_path, "r");
						if (f) {
							if (fscanf(f, "%d", &soft) != 1) {
								soft = 0;
							}
							fclose(f);
						}

						/* Read hard block */
						snprintf(hard_path, sizeof(hard_path), "%s/%s/hard", hci_path, entry->d_name);
						f = fopen(hard_path, "r");
						if (f) {
							if (fscanf(f, "%d", &hard) != 1) {
								hard = 0;
							}
							fclose(f);
						}

						if (hard) {
							rfkill_status = "Hard";
						} else if (soft) {
							rfkill_status = "Soft";
						} else {
							rfkill_status = "OK";
						}
						break;  /* Found rfkill for this hci device */
					}
				}
				closedir(hci_dir);
			}
		}

		ast_cli(a->fd, FORMAT1,
				adapter->id,
				bdaddr,
				adapter_state2str(adapter->state),
				adapter->inuse ? "Yes" : "No",
				power_status,
				rfkill_status,
				bt_version
		       );
	}
	AST_RWLIST_UNLOCK(&adapters);

	if (ctl_sock >= 0) {
		close(ctl_sock);
	}

#undef FORMAT1

	return CLI_SUCCESS;
}

/* Detailed adapter info command */
static char *handle_cli_mobile_show_adapter(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct adapter_pvt *adapter;
	char bdaddr[18];
	int ctl_sock;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile show adapter";
		e->usage =
			"Usage: mobile show adapter <id>\n"
			"       Shows detailed info for a specific Bluetooth adapter.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&adapters);
	AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
		if (!strcmp(adapter->id, a->argv[3])) {
			break;
		}
	}

	if (!adapter) {
		AST_RWLIST_UNLOCK(&adapters);
		ast_cli(a->fd, "Adapter '%s' not found.\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ba2str(&adapter->addr, bdaddr);
	ast_cli(a->fd, "\nAdapter: %s\n", adapter->id);
	ast_cli(a->fd, "  Address:      %s\n", bdaddr);
	ast_cli(a->fd, "  State:        %s\n", adapter_state2str(adapter->state));
	ast_cli(a->fd, "  InUse:        %s\n", adapter->inuse ? "Yes" : "No");

	/* Get detailed info if adapter is available */
	ctl_sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (ctl_sock >= 0 && adapter->dev_id >= 0) {
		struct hci_dev_info di;
		memset(&di, 0, sizeof(di));
		di.dev_id = adapter->dev_id;

		if (ioctl(ctl_sock, HCIGETDEVINFO, &di) == 0 && bacmp(&di.bdaddr, &adapter->addr) == 0) {
			/* Power and scan status */
			ast_cli(a->fd, "  Power:        %s\n", (di.flags & (1 << HCI_UP)) ? "UP" : "DOWN");
			ast_cli(a->fd, "  Inquiry Scan: %s\n", (di.flags & (1 << HCI_ISCAN)) ? "Yes" : "No");
			ast_cli(a->fd, "  Page Scan:    %s\n", (di.flags & (1 << HCI_PSCAN)) ? "Yes" : "No");

			/* Version info */
			if (adapter->hci_socket >= 0) {
				struct hci_version ver;
				if (hci_read_local_version(adapter->hci_socket, &ver, 1000) == 0) {
					ast_cli(a->fd, "\n  Hardware:\n");
					ast_cli(a->fd, "    Manufacturer: 0x%04x\n", ver.manufacturer);
					ast_cli(a->fd, "    HCI Version:  %d.%d\n", ver.hci_ver, ver.hci_rev);
					ast_cli(a->fd, "    LMP Version:  %d.%d (BT %s)\n", ver.lmp_ver, ver.lmp_subver, mbl_lmp_vertostr(ver.lmp_ver));
				}

				/* Features */
				uint8_t features[8];
				if (hci_read_local_features(adapter->hci_socket, features, 1000) == 0) {
					ast_cli(a->fd, "\n  Features:\n    ");
					int first = 1;

					/* Check common features */
					if (features[0] & 0x01) {
						ast_cli(a->fd, "%s3-slot", first ? "" : ", ");
						first = 0;
					}
					if (features[0] & 0x02) {
						ast_cli(a->fd, "%s5-slot", first ? "" : ", ");
						first = 0;
					}
					if (features[0] & 0x04) {
						ast_cli(a->fd, "%sEncrypt", first ? "" : ", ");
						first = 0;
					}
					if (features[3] & 0x80) {
						ast_cli(a->fd, "%seSCO", first ? "" : ", ");
						first = 0;
					}
					if (features[3] & 0x08) {
						ast_cli(a->fd, "%sEDR-ACL-2M", first ? "" : ", ");
						first = 0;
					}
					if (features[3] & 0x10) {
						ast_cli(a->fd, "%sEDR-ACL-3M", first ? "" : ", ");
						first = 0;
					}
					if (features[4] & 0x40) {
						ast_cli(a->fd, "%sLE", first ? "" : ", ");
						first = 0;
					}
					if (features[6] & 0x01) {
						ast_cli(a->fd, "%sSC", first ? "" : ", ");
						first = 0;
					}
					if (first) {
						ast_cli(a->fd, "None");
					}
					ast_cli(a->fd, "\n");
				}
			}
		} else {
			ast_cli(a->fd, "  Power:        Gone\n");
		}
		close(ctl_sock);
	}

	ast_cli(a->fd, "\n");
	AST_RWLIST_UNLOCK(&adapters);

	return CLI_SUCCESS;
}

static char *handle_cli_mobile_search(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)

{
	struct adapter_pvt *adapter;
	inquiry_info *ii = NULL;
	int max_rsp, num_rsp;
	int len, flags;
	int i, phport, hsport;
	char addr[19] = {0};
	char name[31] = {0};

#define FORMAT1 "%-17.17s %-30.30s %-6.6s %-7.7s %-4.4s\n"
#define FORMAT2 "%-17.17s %-30.30s %-6.6s %-7.7s %d\n"

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile search";
		e->usage =
			"Usage: mobile search\n"
			"       Searches for Bluetooth Cell / Mobile devices in range.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	/* find a free adapter */
	AST_RWLIST_RDLOCK(&adapters);
	AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
		if (!adapter->inuse) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&adapters);

	if (!adapter) {
		ast_cli(a->fd, "All Bluetooth adapters are in use at this time.\n");
		return CLI_SUCCESS;
	}

	len  = 8;
	max_rsp = 255;
	flags = IREQ_CACHE_FLUSH;

	ii = ast_alloca(max_rsp * sizeof(inquiry_info));
	num_rsp = hci_inquiry(adapter->dev_id, len, max_rsp, NULL, &ii, flags);
	if (num_rsp > 0) {
		ast_cli(a->fd, FORMAT1, "Address", "Name", "Usable", "Type", "Port");
		for (i = 0; i < num_rsp; i++) {
			ba2str(&(ii + i)->bdaddr, addr);
			name[0] = 0x00;
			if (hci_read_remote_name(adapter->hci_socket, &(ii + i)->bdaddr, sizeof(name) - 1, name, 0) < 0) {
				strcpy(name, "[unknown]");
			}
			phport = sdp_search(addr, HANDSFREE_AGW_PROFILE_ID);
			if (!phport) {
				hsport = sdp_search(addr, HEADSET_PROFILE_ID);
			}
			else {
				hsport = 0;
			}
			ast_cli(a->fd, FORMAT2, addr, name, (phport > 0 || hsport > 0) ? "Yes" : "No",
				(phport > 0) ? "Phone" : "Headset", (phport > 0) ? phport : hsport);
		}
	} else {
		ast_cli(a->fd, "No Bluetooth Cell / Mobile devices found.\n");
	}

#undef FORMAT1
#undef FORMAT2

	return CLI_SUCCESS;
}

static char *handle_cli_mobile_rfcomm(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char buf[128];
	struct mbl_pvt *pvt = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile rfcomm";
		e->usage =
			"Usage: mobile rfcomm <device ID> <command>\n"
			"       Send <command> to the rfcomm port on the device\n"
			"       with the specified <device ID>.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, a->argv[2])) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);

	if (!pvt) {
		ast_cli(a->fd, "Device %s not found.\n", a->argv[2]);
		goto e_return;
	}

	ast_mutex_lock(&pvt->lock);
	if (!pvt->connected) {
		ast_cli(a->fd, "Device %s not connected.\n", a->argv[2]);
		goto e_unlock_pvt;
	}

	snprintf(buf, sizeof(buf), "%s\r", a->argv[3]);
	rfcomm_write(pvt->rfcomm_socket, buf);
	msg_queue_push(pvt, AT_OK, AT_UNKNOWN);

e_unlock_pvt:
	ast_mutex_unlock(&pvt->lock);
e_return:
	return CLI_SUCCESS;
}

static char *handle_cli_mobile_cusd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char buf[128];
	struct mbl_pvt *pvt = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mobile cusd";
		e->usage =
			"Usage: mobile cusd <device ID> <command>\n"
			"       Send cusd <command> to the rfcomm port on the device\n"
			"       with the specified <device ID>.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, a->argv[2])) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);

	if (!pvt) {
		ast_cli(a->fd, "Device %s not found.\n", a->argv[2]);
		goto e_return;
	}

	ast_mutex_lock(&pvt->lock);
	if (!pvt->connected) {
		ast_cli(a->fd, "Device %s not connected.\n", a->argv[2]);
		goto e_unlock_pvt;
	}

	snprintf(buf, sizeof(buf), "%s", a->argv[3]);
	if (hfp_send_cusd(pvt->hfp, buf) || msg_queue_push(pvt, AT_OK, AT_CUSD)) {
		ast_cli(a->fd, "[%s] error sending CUSD\n", pvt->id);
		goto e_unlock_pvt;
	}

e_unlock_pvt:
	ast_mutex_unlock(&pvt->lock);
e_return:
	return CLI_SUCCESS;
}

/*

	Dialplan applications implementation

*/

static int mbl_status_read(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	struct mbl_pvt *pvt;
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(device);
		AST_APP_ARG(type);
	);

	if (ast_strlen_zero(data)) {
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.device)) {
		return -1;
	}

	/* Default type is CONNECTION */
	if (ast_strlen_zero(args.type)) {
		args.type = "CONNECTION";
	}

	/* Initialize buffer to empty string */
	buf[0] = '\0';

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, args.device)) {
			break;
		}
	}

	if (pvt) {
		ast_mutex_lock(&pvt->lock);

		if (!strcasecmp(args.type, "CONNECTION")) {
			const char *stat = "DISCONNECTED";
			if (pvt->connected) {
				stat = "CONNECTED_FREE";
			}
			if (pvt->owner) {
				stat = "CONNECTED_BUSY";
			}
			ast_copy_string(buf, stat, len);
		} else if (!strcasecmp(args.type, "SIGNAL")) {
			if (pvt->hfp && pvt->hfp->initialized) {
				snprintf(buf, len, "%d",
						pvt->hfp->cind_state[pvt->hfp->cind_map.signal]);
			}
		} else if (!strcasecmp(args.type, "ROAM")) {
			if (pvt->hfp && pvt->hfp->initialized) {
				ast_copy_string(buf,
					pvt->hfp->cind_state[pvt->hfp->cind_map.roam] ? "ROAMING" : "NOT_ROAMING", len);
			}
		} else if (!strcasecmp(args.type, "PROVIDER")) {
			if (pvt->hfp && pvt->hfp->provider_name[0]) {
				ast_copy_string(buf, pvt->hfp->provider_name, len);
			}
		} else if (!strcasecmp(args.type, "MCCMNC")) {
			if (pvt->hfp && pvt->hfp->mccmnc[0]) {
				ast_copy_string(buf, pvt->hfp->mccmnc, len);
			}
		} else if (!strcasecmp(args.type, "REGSTATUS")) {
			if (pvt->hfp && pvt->hfp->creg >= 0) {
				const char *regstat;
				switch (pvt->hfp->creg) {
				case 0:
					regstat = "NOT_REGISTERED";
					break;
				case 1:
					regstat = "REGISTERED_HOME";
					break;
				case 2:
					regstat = "SEARCHING";
					break;
				case 3:
					regstat = "DENIED";
					break;
				case 4:
					regstat = "UNKNOWN";
					break;
				case 5:
					regstat = "REGISTERED_ROAMING";
					break;
				default:
					regstat = NULL;
					break;
				}
				if (regstat) {
					ast_copy_string(buf, regstat, len);
				}
			}
		} else if (!strcasecmp(args.type, "BATTERY")) {
			if (pvt->hfp) {
				if (pvt->hfp->battery_percent >= 0) {
					snprintf(buf, len, "%d", pvt->hfp->battery_percent);
				} else if (pvt->hfp->initialized) {
					snprintf(buf, len, "%d",
						pvt->hfp->cind_state[pvt->hfp->cind_map.battchg] * 20);
				}
			}
		} else if (!strcasecmp(args.type, "CHARGING")) {
			if (pvt->hfp && pvt->hfp->charging >= 0) {
				ast_copy_string(buf,
					pvt->hfp->charging ? "CHARGING" : "NOT_CHARGING", len);
			}
		} else if (!strcasecmp(args.type, "CHARSETS")) {
			if (pvt->hfp) {
				ast_copy_string(buf, pvt->cscs_list, len);
			}
		}

		ast_mutex_unlock(&pvt->lock);
	}
	AST_RWLIST_UNLOCK(&devices);

	return 0;
}

static int mbl_sendsms_exec(struct ast_channel *ast, const char *data)
{

	struct mbl_pvt *pvt;
	char *parse, *message;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(device);
		AST_APP_ARG(dest);
		AST_APP_ARG(message);
	);

	if (ast_strlen_zero(data)) {
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.device)) {
		ast_log(LOG_ERROR,"NULL device for message -- SMS will not be sent.\n");
		return -1;
	}

	if (ast_strlen_zero(args.dest)) {
		ast_log(LOG_ERROR,"NULL destination for message -- SMS will not be sent.\n");
		return -1;
	}

	if (ast_strlen_zero(args.message)) {
		ast_log(LOG_ERROR,"NULL Message to be sent -- SMS will not be sent.\n");
		return -1;
	}

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, args.device)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);

	if (!pvt) {
		ast_log(LOG_ERROR,"Bluetooth device %s wasn't found in the list -- SMS will not be sent.\n", args.device);
		goto e_return;
	}

	ast_mutex_lock(&pvt->lock);
	if (!pvt->connected) {
		ast_log(LOG_ERROR,"Bluetooth device %s wasn't connected -- SMS will not be sent.\n", args.device);
		goto e_unlock_pvt;
	}

	if (pvt->sms_mode < SMS_MODE_TEXT) {
		ast_log(LOG_ERROR,"Bluetooth device %s doesn't handle SMS -- SMS will not be sent.\n", args.device);
		goto e_unlock_pvt;
	}

	message = ast_strdup(args.message);

	ast_verb(3, "[%s] SMS: sending to %s (%zu chars)\n", pvt->id, args.dest, strlen(args.message));

	if (hfp_send_cmgs(pvt->hfp, args.dest)
		|| msg_queue_push_data(pvt, AT_SMS_PROMPT, AT_CMGS, message)) {

		ast_log(LOG_ERROR, "[%s] problem sending SMS message\n", pvt->id);
		goto e_free_message;
	}

	ast_mutex_unlock(&pvt->lock);

	return 0;

e_free_message:
	ast_free(message);
e_unlock_pvt:
	ast_mutex_unlock(&pvt->lock);
e_return:
	return -1;
}

/*

	Channel Driver callbacks

*/

static struct ast_channel *mbl_new(int state, struct mbl_pvt *pvt, struct cidinfo *cidinfo,
		const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *chn;

	pvt->answered = 0;
	pvt->alignment_count = 0;
	pvt->alignment_detection_triggered = 0;
	if (pvt->adapter->alignment_detection) {
		pvt->do_alignment_detection = 1;
	} else {
		pvt->do_alignment_detection = 0;
	}

	ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);
	ast_smoother_reset(pvt->bt_in_smoother, CHANNEL_FRAME_SIZE);
	ast_dsp_digitreset(pvt->dsp);

	chn = ast_channel_alloc(1, state,
		cidinfo ? cidinfo->cnum : NULL,
		cidinfo ? cidinfo->cnam : NULL,
		0, 0, pvt->context, assignedids, requestor, 0,
		"Mobile/%s-%04lx", pvt->id, ast_random() & 0xffff);
	if (!chn) {
		goto e_return;
	}

	ast_channel_tech_set(chn, &mbl_tech);
	ast_channel_nativeformats_set(chn, mbl_tech.capabilities);
	ast_channel_set_rawreadformat(chn, DEVICE_FRAME_FORMAT);
	ast_channel_set_rawwriteformat(chn, DEVICE_FRAME_FORMAT);
	ast_channel_set_writeformat(chn, DEVICE_FRAME_FORMAT);
	ast_channel_set_readformat(chn, DEVICE_FRAME_FORMAT);
	ast_channel_tech_pvt_set(chn, pvt);

	if (state == AST_STATE_RING) {
		ast_channel_rings_set(chn, 1);
	}

	ast_channel_language_set(chn, "en");
	pvt->owner = chn;

	if (pvt->sco_socket != -1) {
		ast_channel_set_fd(chn, 0, pvt->sco_socket);
	}
	ast_channel_unlock(chn);

	return chn;

e_return:
	return NULL;
}

static struct ast_channel *mbl_request(const char *type, struct ast_format_cap *cap,
		const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{

	struct ast_channel *chn = NULL;
	struct mbl_pvt *pvt;
	char *dest_dev = NULL;
	char *dest_num = NULL;
	int group = -1;

	if (!data) {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		*cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;
		return NULL;
	}

	if (ast_format_cap_iscompatible_format(cap, DEVICE_FRAME_FORMAT) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_WARNING, "Asked to get a channel of unsupported format '%s'\n", ast_format_cap_get_names(cap, &codec_buf));
		*cause = AST_CAUSE_FACILITY_NOT_IMPLEMENTED;
		return NULL;
	}

	dest_dev = ast_strdupa(data);

	dest_num = strchr(dest_dev, '/');
	if (dest_num) {
		*dest_num++ = 0x00;
	}

	if (((dest_dev[0] == 'g') || (dest_dev[0] == 'G')) && ((dest_dev[1] >= '0') && (dest_dev[1] <= '9'))) {
		group = atoi(&dest_dev[1]);
	}

	/* Find requested device and make sure it's connected. */
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (group > -1 && pvt->group == group && pvt->connected && !pvt->owner) {
			if (!mbl_has_service(pvt)) {
				continue;
			}

			break;
		} else if (!strcmp(pvt->id, dest_dev)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);
	if (!pvt || !pvt->connected || pvt->owner) {
		ast_log(LOG_WARNING, "Request to call on device %s which is not connected / already in use.\n", dest_dev);
		*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
		return NULL;
	}

	if ((pvt->type == MBL_TYPE_PHONE) && !dest_num) {
		ast_log(LOG_WARNING, "Can't determine destination number.\n");
		*cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;
		return NULL;
	}

	ast_mutex_lock(&pvt->lock);
	chn = mbl_new(AST_STATE_DOWN, pvt, NULL, assignedids, requestor);
	ast_mutex_unlock(&pvt->lock);
	if (!chn) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure.\n");
		*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
		return NULL;
	}

	return chn;

}

static int mbl_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct mbl_pvt *pvt;
	char *dest_dev;
	char *dest_num = NULL;

	dest_dev = ast_strdupa(dest);

	pvt = ast_channel_tech_pvt(ast);

	if (pvt->type == MBL_TYPE_PHONE) {
		dest_num = strchr(dest_dev, '/');
		if (!dest_num) {
			ast_log(LOG_WARNING, "Cant determine destination number.\n");
			return -1;
		}
		*dest_num++ = 0x00;
	}

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "mbl_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}

	ast_debug(1, "Calling %s on %s\n", dest, ast_channel_name(ast));

	ast_mutex_lock(&pvt->lock);
	if (pvt->type == MBL_TYPE_PHONE) {
		if (hfp_send_atd(pvt->hfp, dest_num)) {
			ast_mutex_unlock(&pvt->lock);
			ast_log(LOG_ERROR, "error sending ATD command on %s\n", pvt->id);
			return -1;
		}
		pvt->hangupcause = 0;
		pvt->needchup = 1;
		msg_queue_push(pvt, AT_OK, AT_D);
	} else {
		if (hsp_send_ring(pvt->rfcomm_socket)) {
			ast_log(LOG_ERROR, "[%s] error ringing device\n", pvt->id);
			ast_mutex_unlock(&pvt->lock);
			return -1;
		}

		if ((pvt->ring_sched_id = ast_sched_add(pvt->sched, 6000, headset_send_ring, pvt)) == -1) {
			ast_log(LOG_ERROR, "[%s] error ringing device\n", pvt->id);
			ast_mutex_unlock(&pvt->lock);
			return -1;
		}

		pvt->outgoing = 1;
		pvt->needring = 1;
	}
	ast_mutex_unlock(&pvt->lock);

	return 0;

}

static int mbl_hangup(struct ast_channel *ast)
{

	struct mbl_pvt *pvt;

	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	pvt = ast_channel_tech_pvt(ast);

	ast_debug(1, "[%s] hanging up device\n", pvt->id);

	ast_mutex_lock(&pvt->lock);
	ast_channel_set_fd(ast, 0, -1);
	close(pvt->sco_socket);
	pvt->sco_socket = -1;

	if (pvt->needchup) {
		hfp_send_chup(pvt->hfp);
		msg_queue_push(pvt, AT_OK, AT_CHUP);
		pvt->needchup = 0;
	}

	pvt->outgoing = 0;
	pvt->incoming = 0;
	pvt->needring = 0;
	pvt->owner = NULL;
	ast_channel_tech_pvt_set(ast, NULL);

	ast_mutex_unlock(&pvt->lock);

	ast_setstate(ast, AST_STATE_DOWN);

	return 0;

}

static int mbl_answer(struct ast_channel *ast)
{

	struct mbl_pvt *pvt;

	pvt = ast_channel_tech_pvt(ast);

	if (pvt->type == MBL_TYPE_HEADSET) {
		return 0;
	}

	ast_mutex_lock(&pvt->lock);
	if (pvt->incoming) {
		hfp_send_ata(pvt->hfp);
		msg_queue_push(pvt, AT_OK, AT_A);
		pvt->answered = 1;
	}
	ast_mutex_unlock(&pvt->lock);

	return 0;

}

static int mbl_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct mbl_pvt *pvt = ast_channel_tech_pvt(ast);

	if (pvt->type == MBL_TYPE_HEADSET) {
		return 0;
	}

	ast_mutex_lock(&pvt->lock);
	if (hfp_send_dtmf(pvt->hfp, digit)) {
		ast_mutex_unlock(&pvt->lock);
		ast_debug(1, "[%s] error sending digit %c\n", pvt->id, digit);
		return -1;
	}
	msg_queue_push(pvt, AT_OK, AT_VTS);
	ast_mutex_unlock(&pvt->lock);

	ast_debug(1, "[%s] dialed %c\n", pvt->id, digit);

	return 0;
}

static struct ast_frame *mbl_read(struct ast_channel *ast)
{

	struct mbl_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_frame *fr = &ast_null_frame;
	int r;

	ast_debug(3, "*** mbl_read()\n");

	while (ast_mutex_trylock(&pvt->lock)) {
		CHANNEL_DEADLOCK_AVOIDANCE(ast);
	}

	if (!pvt->owner || pvt->sco_socket == -1) {
		goto e_return;
	}

	memset(&pvt->fr, 0x00, sizeof(struct ast_frame));
	pvt->fr.frametype = AST_FRAME_VOICE;
	pvt->fr.subclass.format = DEVICE_FRAME_FORMAT;
	pvt->fr.src = "Mobile";
	pvt->fr.offset = AST_FRIENDLY_OFFSET;
	pvt->fr.mallocd = 0;
	pvt->fr.delivery.tv_sec = 0;
	pvt->fr.delivery.tv_usec = 0;
	pvt->fr.data.ptr = pvt->io_buf + AST_FRIENDLY_OFFSET;

	do {
		if ((r = read(pvt->sco_socket, pvt->fr.data.ptr, pvt->sco_mtu)) == -1) {
			if (errno != EAGAIN && errno != EINTR) {
				ast_debug(1, "[%s] read error %d, going to wait for new connection\n", pvt->id, errno);
				close(pvt->sco_socket);
				pvt->sco_socket = -1;
				ast_channel_set_fd(ast, 0, -1);
			}
			goto e_return;
		}

		pvt->fr.datalen = r;
		pvt->fr.samples = r / 2;

		/* Log first packet and MTU mismatches for debugging */
		if (pvt->mtu_sync_count == 0 && r > 0) {
			/* First packet after reset or if sizes match */
			if (r != pvt->sco_mtu) {
				ast_log(LOG_NOTICE, "[%s] SCO packet size mismatch: got %d bytes, expected MTU=%d (HV3=48, HV2=30, HV1=10)\n",
					pvt->id, r, pvt->sco_mtu);
			}
		}

		if (r > 0 && r != pvt->sco_mtu) {
			pvt->mtu_sync_count++;
			if (pvt->mtu_sync_count == 1) {
				ast_debug(1, "[%s] SCO MTU mismatch #1: received=%d, expected=%d\n", pvt->id, r, pvt->sco_mtu);
			} else if (pvt->mtu_sync_count > 10) {
				ast_log(LOG_NOTICE, "[%s] Adjusting SCO MTU from %d to %d based on incoming packets (phone uses fixed packet size)\n",
					pvt->id, pvt->sco_mtu, r);
				pvt->sco_mtu = r;
				ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);
				pvt->mtu_sync_count = 0;
			}
		} else {
			pvt->mtu_sync_count = 0;
		}

		if (pvt->do_alignment_detection) {
			do_alignment_detection(pvt, pvt->fr.data.ptr, r);
		}

		ast_smoother_feed(pvt->bt_in_smoother, &pvt->fr);
		fr = ast_smoother_read(pvt->bt_in_smoother);
	} while (fr == NULL);
	fr = ast_dsp_process(ast, pvt->dsp, fr);

	ast_mutex_unlock(&pvt->lock);

	return fr;

e_return:
	ast_mutex_unlock(&pvt->lock);
	return fr;
}

static int mbl_write(struct ast_channel *ast, struct ast_frame *frame)
{

	struct mbl_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_frame *f;

	ast_debug(3, "*** mbl_write\n");

	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	while (ast_mutex_trylock(&pvt->lock)) {
		CHANNEL_DEADLOCK_AVOIDANCE(ast);
	}

	ast_smoother_feed(pvt->bt_out_smoother, frame);

	while ((f = ast_smoother_read(pvt->bt_out_smoother))) {
		sco_write(pvt->sco_socket, f->data.ptr, f->datalen);
	}

	ast_mutex_unlock(&pvt->lock);

	return 0;

}

static int mbl_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{

	struct mbl_pvt *pvt = ast_channel_tech_pvt(newchan);

	if (!pvt) {
		ast_debug(1, "fixup failed, no pvt on newchan\n");
		return -1;
	}

	ast_mutex_lock(&pvt->lock);
	if (pvt->owner == oldchan) {
		pvt->owner = newchan;
	}
	ast_mutex_unlock(&pvt->lock);

	return 0;

}

static int mbl_devicestate(const char *data)
{

	char *device;
	int res = AST_DEVICE_INVALID;
	struct mbl_pvt *pvt;

	device = ast_strdupa(S_OR(data, ""));

	ast_debug(1, "Checking device state for device %s\n", device);

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, device)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);

	if (!pvt) {
		return res;
	}

	ast_mutex_lock(&pvt->lock);
	if (pvt->connected) {
		if (pvt->owner) {
			res = AST_DEVICE_INUSE;
		}
		else {
			res = AST_DEVICE_NOT_INUSE;
		}

		if (!mbl_has_service(pvt)) {
			res = AST_DEVICE_UNAVAILABLE;
		}
	}
	ast_mutex_unlock(&pvt->lock);

	return res;

}

/*

	Callback helpers

*/

/*

	do_alignment_detection()

	This routine attempts to detect where we get misaligned sco audio data from the bluetooth adaptor.

	Its enabled by alignmentdetect=yes under the adapter entry in mobile.conf

	Some adapters suffer a problem where occasionally they will byte shift the audio stream one byte to the right.
	The result is static or white noise on the inbound (from the adapter) leg of the call.
	This is characterised by a sudden jump in magnitude of the value of the 16 bit samples.

	Here we look at the first 4 48 byte frames. We average the absolute values of each sample in the frame,
	then average the sum of the averages of frames 1, 2, and 3.
	Frame zero is usually zero.
	If the end result > 100, and it usually is if we have the problem, set a flag and compensate by shifting the bytes
	for each subsequent frame during the call.

	If the result is <= 100 then clear the flag so we don't come back in here...

	This seems to work OK....

*/

static void do_alignment_detection(struct mbl_pvt *pvt, char *buf, int buflen)
{

	int i;
	short a, *s;
	char *p;

	if (pvt->alignment_detection_triggered) {
		for (i=buflen, p=buf+buflen-1; i>0; i--, p--) {
			*p = *(p-1);
		}
		*(p+1) = 0;
		return;
	}

	if (pvt->alignment_count < 4) {
		s = (short *)buf;
		for (i=0, a=0; i<buflen/2; i++) {
			a += *s++;
			a /= i+1;
		}
		pvt->alignment_samples[pvt->alignment_count++] = a;
		return;
	}

	ast_debug(1, "Alignment Detection result is [%-d %-d %-d %-d]\n", pvt->alignment_samples[0], pvt->alignment_samples[1], pvt->alignment_samples[2], pvt->alignment_samples[3]);

	a = abs(pvt->alignment_samples[1]) + abs(pvt->alignment_samples[2]) + abs(pvt->alignment_samples[3]);
	a /= 3;
	if (a > 100) {
		pvt->alignment_detection_triggered = 1;
		ast_debug(1, "Alignment Detection Triggered.\n");
	} else {
		pvt->do_alignment_detection = 0;
	}

}

static int mbl_queue_control(struct mbl_pvt *pvt, enum ast_control_frame_type control)
{
	for (;;) {
		if (pvt->owner) {
			if (ast_channel_trylock(pvt->owner)) {
				DEADLOCK_AVOIDANCE(&pvt->lock);
			} else {
				ast_queue_control(pvt->owner, control);
				ast_channel_unlock(pvt->owner);
				break;
			}
		} else {
			break;
		}
	}
	return 0;
}

static int mbl_queue_hangup(struct mbl_pvt *pvt)
{
	for (;;) {
		if (pvt->owner) {
			if (ast_channel_trylock(pvt->owner)) {
				DEADLOCK_AVOIDANCE(&pvt->lock);
			} else {
				if (pvt->hangupcause != 0) {
					ast_channel_hangupcause_set(pvt->owner, pvt->hangupcause);
				}
				ast_queue_hangup(pvt->owner);
				ast_channel_unlock(pvt->owner);
				break;
			}
		} else {
			break;
		}
	}
	return 0;
}

static int mbl_ast_hangup(struct mbl_pvt *pvt)
{
	ast_hangup(pvt->owner);
	return 0;
}

/*!
 * \brief Check if a mobile device has service.
 * \param pvt a mbl_pvt struct
 * \retval 1 this device has service
 * \retval 0 no service
 *
 * \note This function will always indicate that service is available if the
 * given device does not support service indication.
 */
static int mbl_has_service(struct mbl_pvt *pvt)
{

	if (pvt->type != MBL_TYPE_PHONE) {
		return 1;
	}

	if (!pvt->hfp->cind_map.service) {
		return 1;
	}

	if (pvt->hfp->cind_state[pvt->hfp->cind_map.service] == HFP_CIND_SERVICE_AVAILABLE) {
		return 1;
	}

	return 0;
}

/*

	rfcomm helpers

*/

static int rfcomm_connect(bdaddr_t src, bdaddr_t dst, int remote_channel)
{
	struct sockaddr_rc addr;
	int s;
	int flags;
	struct pollfd pfd;
	int res;
	int error = 0;
	socklen_t len;

	if ((s = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)) < 0) {
		ast_debug(1, "socket() failed (%d).\n", errno);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, &src);
	addr.rc_channel = (uint8_t) 0;
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_debug(1, "bind() failed (%d).\n", errno);
		close(s);
		return -1;
	}

	/* Set non-blocking */
	flags = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);

	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, &dst);
	addr.rc_channel = remote_channel;

	ast_debug(1, "Attempting connection to channel %d\n", remote_channel);
	
	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (errno != EINPROGRESS) {
			ast_debug(1, "connect() failed (%d).\n", errno);
			close(s);
			return -1;
		}
	}

	/* Wait for connection with timeout (e.g., 5000ms) */
	pfd.fd = s;
	pfd.events = POLLOUT;
	
	res = poll(&pfd, 1, 5000); 
	if (res == 0) {
		ast_debug(1, "connect() timed out.\n");
		close(s);
		return -1;
	} else if (res < 0) {
		ast_debug(1, "poll() failed: %s (errno=%d)\n", strerror(errno), errno);
		close(s);
		return -1;
	}

	/* Check for socket error */
	len = sizeof(error);
	if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
		ast_debug(1, "getsockopt() failed: %s (errno=%d)\n", strerror(errno), errno);
		close(s);
		return -1;
	}

	if (error != 0) {
		ast_debug(1, "connect() failed with error %d: %s\n", error, strerror(error));
		close(s);
		return -1;
	}

	/* Restore blocking mode */
	fcntl(s, F_SETFL, flags);

	return s;
}

/*!
 * \brief Write to an rfcomm socket.
 * \param rsock the socket to write to
 * \param buf the null terminated buffer to write
 *
 * This function will write characters from buf.  The buffer must be null
 * terminated.
 *
 * \retval -1 error
 * \retval 0 success
 */
static int rfcomm_write(int rsock, char *buf)
{
	return rfcomm_write_full(rsock, buf, strlen(buf));
}

/*!
 * \brief Write to an rfcomm socket.
 * \param rsock the socket to write to
 * \param buf the buffer to write
 * \param count the number of characters from the buffer to write
 *
 * This function will write count characters from buf.  It will always write
 * count chars unless it encounters an error.
 *
 * \retval -1 error
 * \retval 0 success
 */
static int rfcomm_write_full(int rsock, char *buf, size_t count)
{
	char *p = buf;
	ssize_t out_count;

	ast_debug(1, "rfcomm_write() (%d) [%.*s]\n", rsock, (int) count, buf);
	ast_verb(3, "AT-> %.*s\n", (int) count, buf);
	while (count > 0) {
		if ((out_count = write(rsock, p, count)) == -1) {
			ast_debug(1, "rfcomm_write() error [%d]\n", errno);
			return -1;
		}
		count -= out_count;
		p += out_count;
	}

	return 0;
}

/*!
 * \brief Wait for activity on an rfcomm socket.
 * \param rsock the socket to watch
 * \param ms a pointer to an int containing a timeout in ms
 * \return zero on timeout and the socket fd (non-zero) otherwise
 * \retval 0 timeout
 */
static int rfcomm_wait(int rsock, int *ms)
{
	int exception, outfd;
	outfd = ast_waitfor_n_fd(&rsock, 1, ms, &exception);
	if (outfd < 0) {
		outfd = 0;
	}

	return outfd;
}

#define RFCOMM_READ_DEBUG 1
#ifdef RFCOMM_READ_DEBUG
#define rfcomm_read_debug(c) __rfcomm_read_debug(c)
static void __rfcomm_read_debug(char c)
{
	if (c == '\r') {
		ast_debug(3, "rfcomm_read: \\r (0x0D)\n");
	} else if (c == '\n') {
		ast_debug(3, "rfcomm_read: \\n (0x0A)\n");
	} else if (c >= 0x20 && c <= 0x7E) {
		ast_debug(3, "rfcomm_read: '%c' (0x%02X)\n", c, (unsigned char)c);
	} else {
		ast_debug(3, "rfcomm_read: 0x%02X\n", (unsigned char)c);
	}
}
#else
#define rfcomm_read_debug(c)
#endif

/*!
 * \brief Append the given character to the given buffer and increase the
 * in_count.
 */
static void inline rfcomm_append_buf(char **buf, size_t count, size_t *in_count, char c)
{
	if (*in_count < count) {
		(*in_count)++;
		*(*buf)++ = c;
	}
}

/*!
 * \brief Read a character from the given stream and check if it matches what
 * we expected.
 */
static int rfcomm_read_and_expect_char(int rsock, char *result, char expected)
{
	int res;
	char c;
	struct pollfd pfd;
	int poll_res;

	if (!result) {
		result = &c;
	}

	pfd.fd = rsock;
	pfd.events = POLLIN;

	for (;;) {
		res = read(rsock, result, 1);
		if (res == 1) {
			rfcomm_read_debug(*result);
			if (*result != expected) {
				return -2;
			}
			return 1;
		} else if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			/* Non-blocking socket has no data - wait for more */
			poll_res = poll(&pfd, 1, 2000);  /* 2 second timeout */
			if (poll_res <= 0) {
				return poll_res == 0 ? 0 : -1;
			}
			/* Data available, retry read */
		} else {
			return res;
		}
	}
}

/*!
 * \brief Read a character from the given stream and append it to the given
 * buffer if it matches the expected character.
 */
static int rfcomm_read_and_append_char(int rsock, char **buf, size_t count, size_t *in_count, char *result, char expected)
{
	int res;
	char c;

	if (!result) {
		result = &c;
	}

	if ((res = rfcomm_read_and_expect_char(rsock, result, expected)) < 1) {
		return res;
	}

	rfcomm_append_buf(buf, count, in_count, *result);
	return 1;
}

/*!
 * \brief Read until \verbatim '\r\n'. \endverbatim
 * This function consumes the \verbatim'\r\n'\endverbatim but does not add it to buf.
 */
static int rfcomm_read_until_crlf(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;
	struct pollfd pfd;
	int poll_res;

	pfd.fd = rsock;
	pfd.events = POLLIN;

	for (;;) {
		res = read(rsock, &c, 1);
		if (res == 1) {
			rfcomm_read_debug(c);
			if (c == '\r') {
				if ((res = rfcomm_read_and_expect_char(rsock, &c, '\n')) == 1) {
					break;
				} else if (res == -2) {
					rfcomm_append_buf(buf, count, in_count, '\r');
				} else {
					rfcomm_append_buf(buf, count, in_count, '\r');
					break;
				}
			}
			rfcomm_append_buf(buf, count, in_count, c);
		} else if (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			/* Non-blocking socket has no data - wait for more */
			poll_res = poll(&pfd, 1, 2000);  /* 2 second timeout */
			if (poll_res <= 0) {
				/* Timeout or error - give up */
				return poll_res == 0 ? 0 : -1;
			}
			/* Data available, retry read */
		} else {
			/* EOF or error */
			return res;
		}
	}
	return 1;  /* Success */
}

/*!
 * \brief Read the remainder of an AT SMS prompt.
 * \note the entire parsed string is \verbatim '\r\n> ' \endverbatim or \verbatim '\r\n>\r' \endverbatim
 *
 * By the time this function is executed, only a ' ' or '\r' is left to read.
 * Different phones send different characters after the > prompt:
 * - Some phones send '> ' (space)
 * - Other phones send '>\r' (carriage return)
 */
static int rfcomm_read_sms_prompt(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;

	/* Try to read space first */
	if ((res = rfcomm_read_and_expect_char(rsock, &c, ' ')) == 1) {
		rfcomm_append_buf(buf, count, in_count, c);
		return 1;
	} else if (res == -2 && c == '\r') {
		/* Got \r instead of space - this is also valid for some phones */
		rfcomm_append_buf(buf, count, in_count, c);
		return 1;
	} else if (res < 0 && res != -2) {
		goto e_return;
	}

	/* Got something unexpected */
	ast_log(LOG_WARNING, "Unexpected character 0x%02X after > prompt (expected space or \\r)\n", (unsigned char)c);
	rfcomm_append_buf(buf, count, in_count, c);
	return 1;  /* Still return success - SMS prompt was received */

e_return:
	ast_log(LOG_ERROR, "error parsing SMS prompt on rfcomm socket\n");
	return res;
}

/*!
 * \brief Read until a \verbatim \r\nOK\r\n \endverbatim message.
 */
static int rfcomm_read_until_ok(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;
	int loop_count = 0;

	ast_debug(1, "rfcomm_read_until_ok: starting\n");

	/* here, we read until finding a \r\n, then we read one character at a
	 * time looking for the string '\r\nOK\r\n'.  If we only find a partial
	 * match, we place that in the buffer and try again. */

	for (;;) {
		loop_count++;
		ast_debug(1, "rfcomm_read_until_ok: loop %d, calling rfcomm_read_until_crlf\n", loop_count);
		if ((res = rfcomm_read_until_crlf(rsock, buf, count, in_count)) != 1) {
			ast_debug(1, "rfcomm_read_until_ok: rfcomm_read_until_crlf returned %d\n", res);
			break;
		}
		ast_debug(1, "rfcomm_read_until_ok: read line, in_count=%zu\n", *in_count);

		/* Check if the line we just read was "OK" - some phones send OK as a simple line
		 * without the full \r\nOK\r\n structure afterwards */
		{
			char *line_start = *buf - *in_count;
			/* Find the last line in the buffer (may include previous content) */
			char *last_line = line_start;
			for (char *p = line_start; p < *buf; p++) {
				if (*p == '\n' && p + 1 < *buf) {
					last_line = p + 1;
				}
			}
			/* Check if from last_line to current position is "OK" */
			size_t last_line_len = *buf - last_line;
			
			/* Log the content we're checking - limit to 80 chars for readability */
			ast_debug(1, "rfcomm_read_until_ok: last_line_len=%zu, content='%.*s'\n",
				last_line_len, (int)(last_line_len > 80 ? 80 : last_line_len), last_line);
			
			if (last_line_len == 2 && last_line[0] == 'O' && last_line[1] == 'K') {
				ast_debug(1, "rfcomm_read_until_ok: found 'OK' line, returning success\n");
				return 1;  /* Success - we found OK */
			}
		}

		rfcomm_append_buf(buf, count, in_count, '\r');
		rfcomm_append_buf(buf, count, in_count, '\n');

		if ((res = rfcomm_read_and_expect_char(rsock, &c, '\r')) != 1) {
			if (res != -2) {
				ast_debug(1, "rfcomm_read_until_ok: expecting \\r got res=%d\n", res);
				break;
			}

			rfcomm_append_buf(buf, count, in_count, c);
			continue;
		}

		if ((res = rfcomm_read_and_expect_char(rsock, &c, '\n')) != 1) {
			if (res != -2) {
				break;
			}

			rfcomm_append_buf(buf, count, in_count, '\r');
			rfcomm_append_buf(buf, count, in_count, c);
			continue;
		}
		if ((res = rfcomm_read_and_expect_char(rsock, &c, 'O')) != 1) {
			if (res != -2) {
				break;
			}

			rfcomm_append_buf(buf, count, in_count, '\r');
			rfcomm_append_buf(buf, count, in_count, '\n');
			rfcomm_append_buf(buf, count, in_count, c);
			continue;
		}

		if ((res = rfcomm_read_and_expect_char(rsock, &c, 'K')) != 1) {
			if (res != -2) {
				break;
			}

			rfcomm_append_buf(buf, count, in_count, '\r');
			rfcomm_append_buf(buf, count, in_count, '\n');
			rfcomm_append_buf(buf, count, in_count, 'O');
			rfcomm_append_buf(buf, count, in_count, c);
			continue;
		}

		if ((res = rfcomm_read_and_expect_char(rsock, &c, '\r')) != 1) {
			if (res != -2) {
				break;
			}

			rfcomm_append_buf(buf, count, in_count, '\r');
			rfcomm_append_buf(buf, count, in_count, '\n');
			rfcomm_append_buf(buf, count, in_count, 'O');
			rfcomm_append_buf(buf, count, in_count, 'K');
			rfcomm_append_buf(buf, count, in_count, c);
			continue;
		}

		if ((res = rfcomm_read_and_expect_char(rsock, &c, '\n')) != 1) {
			if (res != -2) {
				break;
			}

			rfcomm_append_buf(buf, count, in_count, '\r');
			rfcomm_append_buf(buf, count, in_count, '\n');
			rfcomm_append_buf(buf, count, in_count, 'O');
			rfcomm_append_buf(buf, count, in_count, 'K');
			rfcomm_append_buf(buf, count, in_count, '\r');
			rfcomm_append_buf(buf, count, in_count, c);
			continue;
		}

		/* we have successfully parsed a '\r\nOK\r\n' string */
		return 1;
	}

	return res;
}

/*!
 * \brief Read the remainder of a +CMGR message.
 * \note the entire parsed string is \verbatim '+CMGR: ...\r\n...\r\n...\r\n...\r\nOK\r\n' \endverbatim
 */
static int rfcomm_read_cmgr(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;

	ast_debug(1, "rfcomm_read_cmgr: starting multi-line CMGR read\n");

	/* append the \r\n that was stripped by the calling function */
	rfcomm_append_buf(buf, count, in_count, '\r');
	rfcomm_append_buf(buf, count, in_count, '\n');

	if ((res = rfcomm_read_until_ok(rsock, buf, count, in_count)) != 1) {
		ast_log(LOG_ERROR, "error reading +CMGR message on rfcomm socket\n");
	}

	ast_debug(1, "rfcomm_read_cmgr: completed with res=%d, in_count=%zu\n", res, *in_count);
	return res;
}

/*!
 * \brief Read the remainder of a +CMGL message (just the SMS body line).
 * \note Each +CMGL response contains header + body, and there may be multiple
 * +CMGL entries before the final OK. We read lines until we get content.
 * Some phones send empty lines before the body, so we skip those.
 * Format: '+CMGL: ...\r\n[empty lines]\r\n<body>\r\n'
 */
static int rfcomm_read_cmgl(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	int attempts = 0;
	size_t line_start;

	/* append the \r\n that was stripped by the calling function */
	rfcomm_append_buf(buf, count, in_count, '\r');
	rfcomm_append_buf(buf, count, in_count, '\n');

	/* Read lines until we get actual body content (skip empty lines)
	 * Some phones may send extra blank lines before the SMS body */
	do {
		line_start = *in_count;
		if ((res = rfcomm_read_until_crlf(rsock, buf, count, in_count)) != 1) {
			ast_log(LOG_ERROR, "error reading +CMGL message body on rfcomm socket\n");
			return res;
		}
		attempts++;
		/* Check if we got actual content (more than just \r\n terminator) */
	} while ((*in_count - line_start) == 0 && attempts < 3);

	return res;
}

/*!
 * \brief Read and AT result code.
 * \note the entire parsed string is \verbatim '\r\n<result code>\r\n' \endverbatim
 */
static int rfcomm_read_result(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;

	if ((res = rfcomm_read_and_expect_char(rsock, &c, '\n')) < 1) {
		goto e_return;
	}

	if ((res = rfcomm_read_and_append_char(rsock, buf, count, in_count, &c, '>')) == 1) {
		return rfcomm_read_sms_prompt(rsock, buf, count, in_count);
	} else if (res != -2) {
		goto e_return;
	}

	rfcomm_append_buf(buf, count, in_count, c);
	res = rfcomm_read_until_crlf(rsock, buf, count, in_count);

	if (res != 1) {
		return res;
	}

	/* check for CMGR, which contains an embedded \r\n pairs terminated by
	 * an \r\nOK\r\n message */
	ast_debug(1, "rfcomm_read_result: in_count=%zu, checking for CMGR: starts_with='%.5s'\n", 
		*in_count, *in_count >= 5 ? (*buf - *in_count) : "");
	if (*in_count >= 5 && !strncmp(*buf - *in_count, "+CMGR", 5)) {
		ast_debug(1, "rfcomm_read_result: CMGR detected, calling rfcomm_read_cmgr\n");
		return rfcomm_read_cmgr(rsock, buf, count, in_count);
	}

	/* check for CMGL, which has a single body line following the header
	 * (multiple +CMGL entries may appear before the final OK) */
	if (*in_count >= 5 && !strncmp(*buf - *in_count, "+CMGL", 5)) {
		ast_debug(1, "rfcomm_read_result: CMGL detected, calling rfcomm_read_cmgl\n");
		return rfcomm_read_cmgl(rsock, buf, count, in_count);
	}

	return 1;

e_return:
	ast_log(LOG_ERROR, "error parsing AT result on rfcomm socket\n");
	return res;
}

/*!
 * \brief Read the remainder of an AT command.
 * \note the entire parsed string is \verbatim '<at command>\r' \endverbatim
 */
static int rfcomm_read_command(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;

	while ((res = read(rsock, &c, 1)) == 1) {
		rfcomm_read_debug(c);
		/* stop when we get to '\r' */
		if (c == '\r') {
			break;
		}

		rfcomm_append_buf(buf, count, in_count, c);
	}
	return res;
}

/*!
 * \brief Read one Hayes AT message from an rfcomm socket.
 * \param rsock the rfcomm socket to read from
 * \param buf the buffer to store the result in
 * \param count the size of the buffer or the maximum number of characters to read
 *
 * Here we need to read complete Hayes AT messages.  The AT message formats we
 * support are listed below.
 *
 * \verbatim
 * \r\n<result code>\r\n
 * <at command>\r
 * \r\n>
 * \endverbatim
 *
 * These formats correspond to AT result codes, AT commands, and the AT SMS
 * prompt respectively.  When messages are read the leading and trailing \verbatim '\r' \endverbatim
 * and \verbatim '\n' \endverbatim characters are discarded.  If the given buffer is not large enough
 * to hold the response, what does not fit in the buffer will be dropped.
 *
 * \note The rfcomm connection to the device is asynchronous, so there is no
 * guarantee that responses will be returned in a single read() call. We handle
 * this by blocking until we can read an entire response.
 *
 * \retval 0 end of file
 * \retval -1 read error
 * \retval -2 parse error
 * \retval other the number of characters added to buf
 */
static ssize_t rfcomm_read(int rsock, char *buf, size_t count)
{
	ssize_t res;
	size_t in_count = 0;
	char c;

	if ((res = rfcomm_read_and_expect_char(rsock, &c, '\r')) == 1) {
		res = rfcomm_read_result(rsock, &buf, count, &in_count);
	} else if (res == -2) {
		rfcomm_append_buf(&buf, count, &in_count, c);
		res = rfcomm_read_command(rsock, &buf, count, &in_count);
	}

	if (res < 1) {
		return res;
	} else {
		return in_count;
	}
}

/*

	sco helpers and callbacks

*/

static int sco_connect(bdaddr_t src, bdaddr_t dst, int *mtu)
{
	struct sockaddr_sco addr;
	struct sco_options so;
	socklen_t len;
	int s;
	char src_str[18], dst_str[18];

	/* Log the addresses for debugging */
	ba2str(&src, src_str);
	ba2str(&dst, dst_str);
	ast_log(LOG_NOTICE, "SCO connect: src=%s dst=%s\n", src_str, dst_str);

	if ((s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO)) < 0) {
		ast_log(LOG_WARNING, "SCO socket() failed: %s (errno=%d)\n", strerror(errno), errno);
		return -1;
	}

	/*
	 * Set voice setting to CVSD 16-bit (0x0060) before connecting.
	 * This is critical for compatibility with older Bluetooth phones (BT 1.2/2.0).
	 * Without this, newer adapters may negotiate transparent/mSBC mode causing
	 * one-way audio or connection failures.
	 *
	 * BT_VOICE_CVSD_16BIT = 0x0060
	 * BT_VOICE_TRANSPARENT = 0x0003
	 */
#ifdef BT_VOICE
	{
		struct bt_voice voice;
		memset(&voice, 0, sizeof(voice));
		voice.setting = 0x0060;  /* BT_VOICE_CVSD_16BIT */

		if (setsockopt(s, SOL_BLUETOOTH, BT_VOICE, &voice, sizeof(voice)) < 0) {
			ast_log(LOG_WARNING, "SCO setsockopt(BT_VOICE) failed: %s - proceeding without explicit codec\n",
				strerror(errno));
			/* Continue anyway - kernel may still negotiate correctly */
		} else {
			ast_log(LOG_NOTICE, "SCO voice setting configured: 0x%04x (CVSD 16-bit)\n", voice.setting);
		}
	}
#else
	ast_log(LOG_NOTICE, "BT_VOICE not available in this kernel - using default codec negotiation\n");
#endif

/* XXX this does not work with the do_sco_listen() thread (which also bind()s
 * to this address).  Also I am not sure if it is necessary. */
#if 0
	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, &src);
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_debug(1, "bind() failed (%d).\n", errno);
		close(s);
		return -1;
	}
#endif

	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, &dst);

	ast_debug(1, "SCO connecting to %s...\n", dst_str);

	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_log(LOG_WARNING, "SCO connect() failed: %s (errno=%d)\n", strerror(errno), errno);
		close(s);
		return -1;
	}

	ast_log(LOG_NOTICE, "SCO connection established to %s\n", dst_str);

	/* Get negotiated SCO/eSCO options and log details */
	if (mtu) {
		len = sizeof(so);
		memset(&so, 0, sizeof(so));
		if (getsockopt(s, SOL_SCO, SCO_OPTIONS, &so, &len) < 0) {
			ast_log(LOG_WARNING, "getsockopt(SCO_OPTIONS) failed: %s (errno=%d), using default MTU=%d\n",
				strerror(errno), errno, DEVICE_FRAME_SIZE_DEFAULT);
			*mtu = DEVICE_FRAME_SIZE_DEFAULT;
		} else {
			*mtu = so.mtu;
			ast_log(LOG_NOTICE, "SCO negotiated parameters: MTU=%d\n", so.mtu);
		}
	}

	return s;
}

static int sco_write(int s, char *buf, int len)
{

	int r;

	if (s == -1) {
		ast_debug(3, "sco_write() not ready\n");
		return 0;
	}

	ast_debug(3, "sco_write()\n");

	r = write(s, buf, len);
	if (r == -1) {
		ast_log(LOG_WARNING, "sco_write() failed: %s (%d) - len %d\n", strerror(errno), errno, len);
		return 0;
	}
	

	return 1;

}

/*!
 * \brief Accept SCO connections.
 * This function is an ast_io callback function used to accept incoming sco
 * audio connections.
 */
static int sco_accept(int *id, int fd, short events, void *data)
{
	struct adapter_pvt *adapter = (struct adapter_pvt *) data;
	struct sockaddr_sco addr;
	socklen_t addrlen;
	struct mbl_pvt *pvt;
	socklen_t len;
	char saddr[18];
	struct sco_options so;
	int sock;
	int mtu;

	addrlen = sizeof(struct sockaddr_sco);
	if ((sock = accept(fd, (struct sockaddr *)&addr, &addrlen)) == -1) {
		ast_log(LOG_ERROR, "error accepting audio connection on adapter %s\n", adapter->id);
		return 0;
	}

	len = sizeof(so);
	memset(&so, 0, sizeof(so));
	if (getsockopt(sock, SOL_SCO, SCO_OPTIONS, &so, &len) < 0) {
		ast_log(LOG_WARNING, "getsockopt(SCO_OPTIONS) failed: %s (errno=%d), using default MTU\n", strerror(errno), errno);
		mtu = DEVICE_FRAME_SIZE_DEFAULT;
	} else {
		mtu = so.mtu;
	}

	ba2str(&addr.sco_bdaddr, saddr);
	ast_log(LOG_NOTICE, "Incoming SCO connection from %s: negotiated MTU=%d bytes\n", saddr, mtu);

	/* Log expected HV packet types for reference */
	ast_log(LOG_NOTICE, "  Expected SCO packet sizes: HV3=48 (30 voice), HV2=30 (20 voice), HV1=10 (10 voice), eSCO=%d\n", mtu);

	/* Log voice settings for debugging codec issues */
	{
		uint16_t vs;
		if (hci_read_voice_setting(adapter->hci_socket, &vs, 1000) < 0) {
			ast_log(LOG_WARNING, "Failed to read adapter voice setting: %s\n", strerror(errno));
		} else {
			vs = htobs(vs);
			ast_log(LOG_NOTICE, "Adapter %s voice setting: 0x%04x (%s)\n", 
				adapter->id, vs,
				vs == 0x0060 ? "CVSD 16-bit" : 
				vs == 0x0063 ? "Transparent 16-bit" : "Unknown");
		}
	}

#ifdef BT_VOICE
	/* Read the negotiated voice setting on the socket */
	{
		struct bt_voice voice;
		socklen_t vlen = sizeof(voice);
		memset(&voice, 0, sizeof(voice));
		if (getsockopt(sock, SOL_BLUETOOTH, BT_VOICE, &voice, &vlen) < 0) {
			ast_debug(1, "getsockopt(BT_VOICE) failed: %s (errno=%d)\n", strerror(errno), errno);
		} else {
			ast_log(LOG_NOTICE, "SCO socket voice setting: 0x%04x (%s)\n",
				voice.setting,
				voice.setting == 0x0060 ? "CVSD 16-bit" :
				voice.setting == 0x0063 ? "Transparent 16-bit" : "Unknown");
		}
	}
#endif

	/* figure out which device this sco connection belongs to */
	pvt = NULL;
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!bacmp(&pvt->addr, &addr.sco_bdaddr)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);
	if (!pvt) {
		ast_log(LOG_WARNING, "could not find device for incoming audio connection\n");
		close(sock);
		return 1;
	}

	ast_mutex_lock(&pvt->lock);
	if (pvt->sco_socket != -1) {
		close(pvt->sco_socket);
		pvt->sco_socket = -1;
	}

	pvt->sco_socket = sock;
	pvt->sco_mtu = mtu;

	/* Reset smoother to use negotiated MTU */
	ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);

	if (pvt->owner) {
		ast_channel_set_fd(pvt->owner, 0, sock);
	} else {
		ast_debug(1, "incoming audio connection for pvt without owner\n");
	}

	ast_mutex_unlock(&pvt->lock);

	return 1;
}

/*!
 * \brief Bind an SCO listener socket for the given adapter.
 * \param adapter an adapter_pvt
 * \return -1 on error, non zero on success
 */
static int sco_bind(struct adapter_pvt *adapter)
{
	struct sockaddr_sco addr;
	int opt = 1;

	if ((adapter->sco_socket = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO)) < 0) {
		ast_log(LOG_ERROR, "Unable to create sco listener socket for adapter %s.\n", adapter->id);
		goto e_return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, &adapter->addr);
	if (bind(adapter->sco_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_log(LOG_ERROR, "Unable to bind sco listener socket. (%d)\n", errno);
		goto e_close_socket;
	}
	if (setsockopt(adapter->sco_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		ast_log(LOG_ERROR, "Unable to setsockopt sco listener socket.\n");
		goto e_close_socket;
	}
	if (listen(adapter->sco_socket, 5) < 0) {
		ast_log(LOG_ERROR, "Unable to listen sco listener socket.\n");
		goto e_close_socket;
	}

	return adapter->sco_socket;

e_close_socket:
	close(adapter->sco_socket);
	adapter->sco_socket = -1;
e_return:
	return -1;
}

/*
 * Hayes AT command helpers.
 */

/*!
 * \brief Match the given buffer with the given prefix.
 * \param buf the buffer to match
 * \param prefix the prefix to match
 */
static int at_match_prefix(char *buf, char *prefix)
{
	return !strncmp(buf, prefix, strlen(prefix));
}

/*!
 * \brief Read an AT message and classify it.
 * \param rsock an rfcomm socket
 * \param buf the buffer to store the result in
 * \param count the size of the buffer or the maximum number of characters to read
 * \return the type of message received, in addition buf will contain the
 * message received and will be null terminated
 * \see at_read()
 */
static enum at_message at_read_full(int rsock, char *buf, size_t count)
{
	ssize_t s;
	char *p;
	
	if ((s = rfcomm_read(rsock, buf, count - 1)) < 1) {
		return s;
	}
	buf[s] = '\0';

	/* Skip leading whitespace/newlines - some phones send extra \r\n before responses */
	p = buf;
	while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
		p++;
	}
	
	/* If the message is empty after stripping whitespace, return unknown */
	if (!*p) {
		buf[0] = '\0';  /* Make sure buf is empty for logging */
		return AT_UNKNOWN;
	}
	
	/* If we stripped anything, shift the content to the start of buf */
	if (p != buf) {
		memmove(buf, p, strlen(p) + 1);
	}

	if (!strcmp("OK", buf)) {
		return AT_OK;
	} else if (!strcmp("ERROR", buf)) {
		return AT_ERROR;
	} else if (!strcmp("RING", buf)) {
		return AT_RING;
	} else if (!strcmp("AT+CKPD=200", buf)) {
		return AT_CKPD;
	} else if (!strcmp("> ", buf) || !strcmp(">\r", buf) || !strcmp(">", buf)) {
		return AT_SMS_PROMPT;
	} else if (at_match_prefix(buf, "+CMTI:")) {
		return AT_CMTI;
	} else if (at_match_prefix(buf, "+CIEV:")) {
		return AT_CIEV;
	} else if (at_match_prefix(buf, "+BRSF:")) {
		return AT_BRSF;
	} else if (at_match_prefix(buf, "+CIND:")) {
		return AT_CIND;
	} else if (at_match_prefix(buf, "+CLIP:")) {
		return AT_CLIP;
	} else if (at_match_prefix(buf, "+CMGR:")) {
		return AT_CMGR;
	} else if (at_match_prefix(buf, "+VGM:")) {
		return AT_VGM;
	} else if (at_match_prefix(buf, "+VGS:")) {
		return AT_VGS;
	} else if (at_match_prefix(buf, "+CMS ERROR:")) {
		return AT_CMS_ERROR;
	} else if (at_match_prefix(buf, "AT+VGM=")) {
		return AT_VGM;
	} else if (at_match_prefix(buf, "AT+VGS=")) {
		return AT_VGS;
	} else if (at_match_prefix(buf, "+CUSD:")) {
		return AT_CUSD;
	} else if (at_match_prefix(buf, "BUSY")) {
		return AT_BUSY;
	} else if (at_match_prefix(buf, "NO DIALTONE")) {
		return AT_NO_DIALTONE;
	} else if (at_match_prefix(buf, "NO CARRIER")) {
		return AT_NO_CARRIER;
	} else if (at_match_prefix(buf, "*ECAV:")) {
		return AT_ECAM;
	} else if (at_match_prefix(buf, "+CSCS:")) {
		return AT_CSCS;
	} else if (at_match_prefix(buf, "+CMGL:")) {
		return AT_CMGL;
	} else if (at_match_prefix(buf, "+CPMS:")) {
		return AT_CPMS;
	} else if (at_match_prefix(buf, "+CREG:")) {
		return AT_CREG;
	} else if (at_match_prefix(buf, "+CGREG:")) {
		return AT_CGREG;
	} else if (at_match_prefix(buf, "+COPS:")) {
		return AT_COPS;
	} else if (at_match_prefix(buf, "+CNMI:")) {
		return AT_CNMI;
	} else if (at_match_prefix(buf, "+CBC:")) {
		return AT_CBC;
	} else if (at_match_prefix(buf, "+CSQ:")) {
		return AT_CSQ;
	} else {
		return AT_UNKNOWN;
	}
}

/*!
 * \brief Get the string representation of the given AT message.
 * \param msg the message to process
 * \return a string describing the given message
 */
static inline const char *at_msg2str(enum at_message msg)
{
	switch (msg) {
	/* errors */
	case AT_PARSE_ERROR:
		return "PARSE ERROR";
	case AT_READ_ERROR:
		return "READ ERROR";
	default:
	case AT_UNKNOWN:
		return "UNKNOWN";
	/* at responses */
	case AT_OK:
		return "OK";
	case AT_ERROR:
		return "ERROR";
	case AT_RING:
		return "RING";
	case AT_BRSF:
		return "AT+BRSF";
	case AT_CIND:
		return "AT+CIND";
	case AT_CIEV:
		return "AT+CIEV";
	case AT_CLIP:
		return "AT+CLIP";
	case AT_CMTI:
		return "AT+CMTI";
	case AT_CMGR:
		return "AT+CMGR";
	case AT_SMS_PROMPT:
		return "SMS PROMPT";
	case AT_CMS_ERROR:
		return "+CMS ERROR";
	case AT_CSCS:
		return "+CSCS";
	case AT_CSCS_SET:
		return "+CSCS (Set)";
	case AT_CSCS_VERIFY:
		return "+CSCS (Verify)";
	case AT_BUSY:
		return "BUSY";
	case AT_NO_DIALTONE:
		return "NO DIALTONE";
	case AT_NO_CARRIER:
		return "NO CARRIER";
	/* at commands */
	case AT_A:
		return "ATA";
	case AT_D:
		return "ATD";
	case AT_CHUP:
		return "AT+CHUP";
	case AT_CKPD:
		return "AT+CKPD";
	case AT_CMGS:
		return "AT+CMGS";
	case AT_VGM:
		return "AT+VGM";
	case AT_VGS:
		return "AT+VGS";
	case AT_VTS:
		return "AT+VTS";
	case AT_CMGF:
		return "AT+CMGF";
	case AT_CMGL:
		return "AT+CMGL";
	case AT_CMGD:
		return "AT+CMGD";
	case AT_CMGF_PDU:
		return "AT+CMGF (PDU)";
	case AT_CNMI:
		return "AT+CNMI";
	case AT_CNMI_TEST:
		return "AT+CNMI=?";
	case AT_CNMI_QUERY:
		return "AT+CNMI?";
	case AT_CPMS:
		return "AT+CPMS";
	case AT_CMER:
		return "AT+CMER";
	case AT_CIND_TEST:
		return "AT+CIND=?";
	case AT_CUSD:
		return "AT+CUSD";
	case AT_ECAM:
		return "AT*ECAM";
	/* Device status commands */
	case AT_CREG:
		return "AT+CREG";
	case AT_CREG_SET:
		return "AT+CREG (Set)";
	case AT_CGREG:
		return "AT+CGREG";
	case AT_CGREG_SET:
		return "AT+CGREG (Set)";
	case AT_COPS:
		return "AT+COPS";
	case AT_COPS_SET_NUMERIC:
		return "AT+COPS=3,2";
	case AT_COPS_SET_ALPHA:
		return "AT+COPS=3,0";
	case AT_COPS_QUERY:
		return "AT+COPS?";
	case AT_COPS_DONE:
		return "AT+COPS (Done)";
	case AT_CBC:
		return "AT+CBC";
	case AT_CSQ:
		return "AT+CSQ";
	}
}

/*
 * bluetooth handsfree profile helpers
 */

 /*!
 * \brief Parse a ECAV event.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \return -1 on error (parse error) or a ECAM value on success
 *
 * Example:
 * \verbatim *ECAV: <ccid>,<ccstatus>,<calltype>[,<processid>]
                    [,exitcause][,<number>,<type>] \endverbatim
 *
 * Example indicating busy:
 * \verbatim *ECAV: 1,7,1 \endverbatim
 */
static int hfp_parse_ecav(struct hfp_pvt *hfp, char *buf)
{
	int ccid = 0;
	int ccstatus = 0;
	int calltype = 0;

	if (!sscanf(buf, "*ECAV: %2d,%2d,%2d", &ccid, &ccstatus, &calltype)) {
		ast_debug(1, "[%s] error parsing ECAV event '%s'\n", hfp->owner->id, buf);
		return -1;
	}

	return ccstatus;
}

/*!
 * \brief Enable Sony Ericsson extensions / indications.
 * \param hfp an hfp_pvt struct
 */
static int hfp_send_ecam(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT*ECAM=1\r");
}

/*!
 * \brief Parse a CIEV event.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \param value a pointer to an int to store the event value in (can be NULL)
 * \return 0 on error (parse error, or unknown event) or a HFP_CIND_* value on
 * success
 */
static int hfp_parse_ciev(struct hfp_pvt *hfp, char *buf, int *value)
{
	int i, v;
	if (!value) {
		value = &v;
	}

	if (!sscanf(buf, "+CIEV: %d,%d", &i, value)) {
		ast_debug(2, "[%s] error parsing CIEV event '%s'\n", hfp->owner->id, buf);
		return HFP_CIND_NONE;
	}

	if (i >= ARRAY_LEN(hfp->cind_state)) {
		ast_debug(2, "[%s] CIEV event index too high (%s)\n", hfp->owner->id, buf);
		return HFP_CIND_NONE;
	}

	hfp->cind_state[i] = *value;
	return hfp->cind_index[i];
}

/*!
 * \brief Parse a CLIP event.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \note buf will be modified when the CID string is parsed
 * \return a cidinfo structure pointing to the cnam and cnum
 * data in buf.  On parse errors, either or both pointers
 * will point to null strings
 */
static struct cidinfo hfp_parse_clip(struct hfp_pvt *hfp, char *buf)
{
	int i;
	int tokens[7];  /* Need 7 tokens for full CLIP: +CLIP:,num,type,subaddr,satype,alpha,validity */
	char *cnamtmp;
	char delim = ' ';	/* First token terminates with space */
	int invalid = 0;	/* Number of invalid chars in cnam */
	struct cidinfo cidinfo = { NULL, NULL };

	/* parse clip info in the following format:
	 * +CLIP: "123456789",128,...
	 * 3GPP TS 27.007: +CLIP: <number>,<type>[,<subaddr>,<satype>[,[<alpha>][,<CLI validity>]]]
	 * token 0 = +CLIP:
	 * token 1 = <number> (quoted)
	 * token 2 = <type>
	 * token 3 = <subaddr> (optional, may be empty)
	 * token 4 = <satype> (optional, may be empty)
	 * token 5 = <alpha> (optional, the caller name - quoted)
	 * token 6 = <CLI validity> (optional)
	 */
	ast_debug(3, "[%s] hfp_parse_clip is processing \"%s\"\n", hfp->owner->id, buf);
	tokens[0] = 0;		/* First token starts in position 0 */
	for (i = 1; i < ARRAY_LEN(tokens); i++) {
		tokens[i] = parse_next_token(buf, tokens[i - 1], delim);
		delim = ',';	/* Subsequent tokens terminate with comma */
	}
	ast_debug(3, "[%s] hfp_parse_clip found tokens: 0=%s, 1=%s, 2=%s, 3=%s, 4=%s, 5=%s, 6=%s\n",
		hfp->owner->id, &buf[tokens[0]], &buf[tokens[1]], &buf[tokens[2]],
		&buf[tokens[3]], &buf[tokens[4]], &buf[tokens[5]], &buf[tokens[6]]);

	/* Clean up cnum, and make sure it is legitimate since it is untrusted. */
	cidinfo.cnum = ast_strip_quoted(&buf[tokens[1]], "\"", "\"");
	if (!ast_isphonenumber(cidinfo.cnum)) {
		ast_debug(1, "[%s] hfp_parse_clip invalid cidinfo.cnum data \"%s\" - deleting\n",
			hfp->owner->id, cidinfo.cnum);
		cidinfo.cnum = "";
	}

	/*
	 * CNAM (alpha) is in token 5 per 3GPP TS 27.007.
	 * Token 6 is the validity indicator.
	 * If token 5 is empty, we have no caller name.
	 */
	cidinfo.cnam = &buf[tokens[5]];

	/* If token 5 is empty, check token 4 as a fallback */
	if (buf[tokens[5]] == '\0' && buf[tokens[4]] != '\0') {
		char *check = &buf[tokens[4]];
		while (*check == ' ') {
			check++;
		}
		if (*check == '"') {
			cidinfo.cnam = check;
		}
	}

	/* Clean up CNAM. */
	cidinfo.cnam = ast_strip_quoted(cidinfo.cnam, "\"", "\"");
    
	/* Check if we need to decode UCS-2 */
	if (hfp->owner && !strcasecmp(hfp->owner->cscs_active, "UCS2")) {
		char decoded[256];
		if (ucs2_hex_to_utf8(cidinfo.cnam, decoded, sizeof(decoded)) > 0) {
			/* We can safely copy back because decoded UTF-8 is always shorter 
			 * than UCS-2 hex encoding (1 char -> 4 hex bytes vs 1-4 UTF-8 bytes) */
			ast_debug(2, "[%s] hfp_parse_clip decoded CNAM from UCS2: %s\n", 
				hfp->owner->id, cidinfo.cnam);
			ast_log(LOG_NOTICE, "[%s] CLIP Decoded: '%s' (Original: '%s')\n", hfp->owner->id, decoded, cidinfo.cnam);
			strcpy(cidinfo.cnam, decoded);
		}
	}
	
    /* Only filter if we haven't confirmed UTF-8 support (or using UCS-2 which is effectively UTF-8 capable) */
    if (!hfp->owner->has_utf8 && !hfp->owner->has_ucs2) {
	    for (cnamtmp = cidinfo.cnam; *cnamtmp != '\0'; cnamtmp++) {
		    if (!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-,abcdefghijklmnopqrstuvwxyz_", *cnamtmp)) {
			    *cnamtmp = '_';	/* Invalid.  Replace with underscore. */
			    invalid++;
		    }
	    }
    }
	if (invalid) {
		ast_debug(2, "[%s] hfp_parse_clip replaced %d invalid byte(s) in cnam data\n",
			hfp->owner->id, invalid);
	}
	ast_debug(2, "[%s] hfp_parse_clip returns cnum=%s and cnam=%s\n",
		hfp->owner->id, cidinfo.cnum, cidinfo.cnam);

	return cidinfo;
}

/*!
 * \brief Terminate current token and return an index to start of the next token.
 * \param string the null-terminated string being parsed (will be altered!)
 * \param start where the current token starts
 * \param delim the token termination delimiter.  \0 is also considered a terminator.
 * \return index of the next token.  May be the same as this token if the string is
 * exhausted.
 */
static int parse_next_token(char string[], const int start, const char delim)
{
	int index;
	int quoting = 0;

	for (index = start; string[index] != 0; index++) {
		if ((string[index] == delim) && !quoting ) {
			/* Found the delimiter, outside of quotes.  This is the end of the token. */
			string[index] = '\0';	/* Terminate this token. */
			index++;		/* Point the index to the start of the next token. */
			break;			/* We're done. */
		} else if (string[index] == '"' && !quoting) {
			/* Found a beginning quote mark.  Remember it. */
			quoting = 1;
		} else if (string[index] == '"' ) {
			/* Found the end quote mark. */
			quoting = 0;
		}
	}
	return index;
}

/*!
 * \brief Parse a CMTI notification.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \note buf will be modified when the CMTI message is parsed
 * \return -1 on error (parse error) or the index of the new sms message
 */
static int hfp_parse_cmti_full(struct hfp_pvt *hfp, char *buf, char *mem)
{
	int index = -1;
	char mem_buf[16] = "";

	/* parse cmti info in the following format:
	 * +CMTI: <mem>,<index>
	 * Example: +CMTI: "MT",12
	 */
	if (sscanf(buf, "+CMTI: \"%15[^\"]\",%d", mem_buf, &index) == 2) {
		if (mem) {
			ast_copy_string(mem, mem_buf, 4); /* usually 2 chars like "SM", "MT" */
		}
		return index;
	}
	/* Try without quotes just in case */
	if (sscanf(buf, "+CMTI: %15[^,],%d", mem_buf, &index) == 2) {
		if (mem) {
			ast_copy_string(mem, mem_buf, 4);
		}
		return index;
	}

	/* Fallback to old parsing if format doesn't match above (e.g. no quotes or different spacing) */
	if (!sscanf(buf, "+CMTI: %*[^,],%d", &index)) {
		ast_debug(2, "[%s] error parsing CMTI event '%s'\n", hfp->owner->id, buf);
		return -1;
	}
	/* If we get here, valid index but mem failed to parse detailedly? 
	 * Retain index. mem will be empty if not parsed. */
	return index;
}

static int hfp_send_cpms(struct hfp_pvt *hfp, const char *mem)
{
	char cmd[32];
	/* Set memory for reading (mem1) to the one where message arrived.
	 * Format: AT+CPMS="<mem>"
	 */
	snprintf(cmd, sizeof(cmd), "AT+CPMS=\"%s\"\r", mem);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Parse a CMGR message.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \param from_number a pointer to a char pointer which will store the from
 * number
 * \param text a pointer to a char pointer which will store the message text
 * \note buf will be modified when the CMGR message is parsed
 * \retval -1 parse error
 * \retval 0 success
 */
static int hfp_parse_cmgr(struct hfp_pvt *hfp, char *buf, char **from_number, char **from_name, char **text)
{
	int i, state;
	size_t s;

	/* parse cmgr info in the following format:
	 * +CMGR: <msg status>,"+123456789",...\r\n
	 * <message text>
	 */
	state = 0;
	s = strlen(buf);
	for (i = 0; i < s && state != 6; i++) {
		switch (state) {
		case 0: /* search for start of the number section (,) */
			if (buf[i] == ',') {
				state++;
			}
			break;
		case 1: /* find the opening quote (") */
			if (buf[i] == '"') {
				state++;
			}
			break;
		case 2: /* mark the start of the number */
			if (from_number) {
				*from_number = &buf[i];
				state++;
			}
			/* fall through */
		case 3: /* search for the end of the number (") */
			if (buf[i] == '"') {
				buf[i] = '\0';
				state++;
			}
			break;
		case 4: /* search for start of name or date (comma) */
			if (buf[i] == ',') {
				state++;
			}
			break;
		case 5: /* check if name is present (starts with ") */
			if (buf[i] == '"') {
				/* Name is present */
				state++; /* Go to 6 (capture name) */
			} else if (buf[i] == ',') {
				/* Empty name field, skip to date */
				/* from_name remains NULL */
				state = 8; /* Go to date skipping */ 
			} else {
				/* Probably unquoted empty field or just unexpected char, skip */
			}
			break;
		case 6: /* capture name */
			if (from_name) {
				*from_name = &buf[i];
				state++;
			}
			/* fall through */
		case 7: /* find end of name */
			if (buf[i] == '"') {
				buf[i] = '\0';
				state++;
			}
			break;
		case 8: /* search for the start of the message text (\n) */
			if (buf[i] == '\n') {
				state++;
			}
			break;
		case 9: /* mark start of text */
			if (text) {
				*text = &buf[i];
				state++;
			}
			/* fall through */
		case 10: /* find end of text (EOF) - handled by loop termination */
			break;
		}
	}

	return 0;
}

/*!
 * \brief Parse a CUSD answer.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \note buf will be modified when the CUSD string is parsed
 * \return NULL on error (parse error) or a pointer to the cusd message
 * information in buf
 */
static char *hfp_parse_cusd(struct hfp_pvt *hfp, char *buf)
{
	int i, message_start, message_end;
	char *cusd;
	size_t s;

	/* parse cusd message in the following format:
	 * +CUSD: 0,"100,00 EURO, valid till 01.01.2010, you are using tariff "Mega Tariff". More informations *111#."
	 */
	message_start = 0;
	message_end = 0;
	s = strlen(buf);

	/* Find the start of the message (") */
	for (i = 0; i < s; i++) {
		if (buf[i] == '"') {
			message_start = i + 1;
			break;
		}
	}

	if (message_start == 0 || message_start >= s) {
		return NULL;
	}

	/* Find the end of the message (") */
	for (i = s; i > 0; i--) {
		if (buf[i] == '"') {
			message_end = i;
			break;
		}
	}

	if (message_end == 0) {
		return NULL;
	}

	if (message_start >= message_end) {
		return NULL;
	}

	cusd = &buf[message_start];
	buf[message_end] = '\0';

	return cusd;
}

/*!
 * \brief Convert a hfp_hf struct to a BRSF int.
 * \param hf an hfp_hf brsf object
 * \return an integer representing the given brsf struct
 */
static int hfp_brsf2int(struct hfp_hf *hf)
{
	int brsf = 0;

	brsf |= hf->ecnr ? HFP_HF_ECNR : 0;
	brsf |= hf->cw ? HFP_HF_CW : 0;
	brsf |= hf->cid ? HFP_HF_CID : 0;
	brsf |= hf->voice ? HFP_HF_VOICE : 0;
	brsf |= hf->volume ? HFP_HF_VOLUME : 0;
	brsf |= hf->status ? HFP_HF_STATUS : 0;
	brsf |= hf->control ? HFP_HF_CONTROL : 0;

	return brsf;
}

/*!
 * \brief Convert a BRSF int to an hfp_ag struct.
 * \param brsf a brsf integer
 * \param ag a AG (hfp_ag) brsf object
 * \return a pointer to the given hfp_ag object populated with the values from
 * the given brsf integer
 */
static struct hfp_ag *hfp_int2brsf(int brsf, struct hfp_ag *ag)
{
	ag->cw = brsf & HFP_AG_CW ? 1 : 0;
	ag->ecnr = brsf & HFP_AG_ECNR ? 1 : 0;
	ag->voice = brsf & HFP_AG_VOICE ? 1 : 0;
	ag->ring = brsf & HFP_AG_RING ? 1 : 0;
	ag->tag = brsf & HFP_AG_TAG ? 1 : 0;
	ag->reject = brsf & HFP_AG_REJECT ? 1 : 0;
	ag->status = brsf & HFP_AG_STATUS ? 1 : 0;
	ag->control = brsf & HFP_AG_CONTROL ? 1 : 0;
	ag->errors = brsf & HFP_AG_ERRORS ? 1 : 0;

	return ag;
}

/*!
 * \brief Send a BRSF request.
 * \param hfp an hfp_pvt struct
 * \param brsf an hfp_hf brsf struct
 *
 * \retval 0 on success
 * \retval -1 on error
 */
static int hfp_send_brsf(struct hfp_pvt *hfp, struct hfp_hf *brsf)
{
	char cmd[32];
	int val = hfp_brsf2int(brsf);
	ast_log(LOG_NOTICE, "Sending AT+BRSF=%d\n", val);
	snprintf(cmd, sizeof(cmd), "AT+BRSF=%d\r", val);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send CMGL to list messages
 * \note In text mode uses quoted status like "REC UNREAD"
 *       In PDU mode uses numeric status: 0=REC UNREAD, 1=REC READ, 4=ALL
 */
static int hfp_send_cmgl(struct hfp_pvt *hfp, const char *status)
{
	char cmd[64];
	/* Check if we're in PDU mode - if owner has pdu mode, use numeric status */
	if (hfp->owner && hfp->owner->sms_mode == SMS_MODE_PDU) {
		/* Convert text status to PDU numeric: 0=unread, 1=read, 4=all */
		int num_status = 4;  /* default to ALL */
		if (!strcmp(status, "REC UNREAD")) {
			num_status = 0;
		} else if (!strcmp(status, "REC READ")) {
			num_status = 1;
		} else if (!strcmp(status, "ALL")) {
			num_status = 4;
		}
		snprintf(cmd, sizeof(cmd), "AT+CMGL=%d\r", num_status);
	} else {
		/* Text mode - use quoted string */
		snprintf(cmd, sizeof(cmd), "AT+CMGL=\"%s\"\r", status);
	}
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send CMGD to delete a message
 */
static int hfp_send_cmgd(struct hfp_pvt *hfp, int index)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+CMGD=%d\r", index);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Parse CMGL response to get message index
 * Format: +CMGL: <index>,... or +CMGL: <index>,... 
 */
static int hfp_parse_cmgl_response(struct hfp_pvt *hfp, char *buf)
{
	int index = -1;
	if (sscanf(buf, "+CMGL: %d", &index) == 1) {
		return index;
	}
	return -1;
}

/*!
 * \brief Parse CPMS response to get used/total counts
 * Format: +CPMS: <used1>,<total1>,...
 */
static void hfp_parse_cpms_response(struct hfp_pvt *hfp, char *buf, int *used, int *total)
{
	int u = 0, t = 0;
	/* Check for response format +CPMS: "SM",1,10,... or +CPMS: 1,10,... */
	/* Often it's +CPMS: <used1>,<total1>,<used2>,<total2>,... */
	if (sscanf(buf, "+CPMS: %d,%d", &u, &t) == 2) {
		if (used) {
			*used = u;
		}
		if (total) {
			*total = t;
		}
	} else if (sscanf(buf, "+CPMS: \"%*[^\"]\",%d,%d", &u, &t) == 2) {
		if (used) {
			*used = u;
		}
		if (total) {
			*total = t;
		}
	}
}

/*!
 * \brief Send the CIND read command.
 * \param hfp an hfp_pvt struct
 */
static int hfp_send_cind(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CIND?\r");
}

/*!
 * \brief Send the CIND test command.
 * \param hfp an hfp_pvt struct
 */
static int hfp_send_cind_test(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CIND=?\r");
}

/*!
 * \brief Enable or disable indicator events reporting.
 * \param hfp an hfp_pvt struct
 * \param status enable or disable events reporting (should be 1 or 0)
 */
static int hfp_send_cmer(struct hfp_pvt *hfp, int status)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+CMER=3,0,0,%d\r", status ? 1 : 0);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send the current speaker gain level.
 * \param hfp an hfp_pvt struct
 * \param value the value to send (must be between 0 and 15)
 */
static int hfp_send_vgs(struct hfp_pvt *hfp, int value)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+VGS=%d\r", value);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send the current microphone gain level.
 * \param hfp an hfp_pvt struct
 * \param value the value to send (must be between 0 and 15)
 */
static int hfp_send_vgm(struct hfp_pvt *hfp, int value)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+VGM=%d\r", value);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send CUSD command.
 * \param hfp an hfp_pvt struct
 * \param code the CUSD code to send
 * \return 0 on success, -1 on error
 */
static int hfp_send_cscs(struct hfp_pvt *hfp, const char *charset)
{
	char buf[64];
	if (charset) {
		snprintf(buf, sizeof(buf), "AT+CSCS=\"%s\"\r", charset);
	} else {
		snprintf(buf, sizeof(buf), "AT+CSCS=?\r");
	}

	return rfcomm_write(hfp->rsock, buf);
}

/*!
 * \brief Send AT+CREG command for network registration status
 * \param hfp an hfp_pvt struct
 * \param mode 0=disable, 1=enable unsolicited, -1=query current
 * \return 0 on success, -1 on error
 */
static int hfp_send_creg(struct hfp_pvt *hfp, int mode)
{
	char cmd[32];
	if (mode < 0) {
		snprintf(cmd, sizeof(cmd), "AT+CREG?\r");
	} else {
		snprintf(cmd, sizeof(cmd), "AT+CREG=%d\r", mode);
	}
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send AT+CGREG command for GPRS registration status
 * \param hfp an hfp_pvt struct
 * \param mode 0=disable, 1=enable unsolicited, -1=query current
 * \return 0 on success, -1 on error
 */
static int hfp_send_cgreg(struct hfp_pvt *hfp, int mode)
{
	char cmd[32];
	if (mode < 0) {
		snprintf(cmd, sizeof(cmd), "AT+CGREG?\r");
	} else {
		snprintf(cmd, sizeof(cmd), "AT+CGREG=%d\r", mode);
	}
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send AT+COPS command for operator information
 * \param hfp an hfp_pvt struct
 * \param format 0=long alphanumeric, 1=short alphanumeric, 2=numeric MCC/MNC
 * \param query if true, send AT+COPS? query, otherwise AT+COPS=3,<format>
 * \return 0 on success, -1 on error
 */
static int hfp_send_cops(struct hfp_pvt *hfp, int format, int query)
{
	char cmd[32];
	if (query) {
		snprintf(cmd, sizeof(cmd), "AT+COPS?\r");
	} else {
		snprintf(cmd, sizeof(cmd), "AT+COPS=3,%d\r", format);
	}
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send AT+CSQ command for signal quality
 * \param hfp an hfp_pvt struct
 * \return 0 on success, -1 on error
 */
static int hfp_send_csq(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CSQ\r");
}

/*!
 * \brief Send AT+CBC command for battery status
 * \param hfp an hfp_pvt struct
 * \return 0 on success, -1 on error
 */
static int hfp_send_cbc(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CBC\r");
}

/*
 * bluetooth headset profile helpers calling line identification.
 * \param hfp an hfp_pvt struct
 * \param status enable or disable calling line identification (should be 1 or
 * 0)
 */
static int hfp_send_clip(struct hfp_pvt *hfp, int status)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+CLIP=%d\r", status ? 1 : 0);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send a DTMF command.
 * \param hfp an hfp_pvt struct
 * \param digit the dtmf digit to send
 * \return the result of rfcomm_write() or -1 on an invalid digit being sent
 */
static int hfp_send_dtmf(struct hfp_pvt *hfp, char digit)
{
	char cmd[10];

	switch(digit) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case '*':
	case '#':
		snprintf(cmd, sizeof(cmd), "AT+VTS=%c\r", digit);
		return rfcomm_write(hfp->rsock, cmd);
	default:
		return -1;
	}
}

/*!
 * \brief Set the SMS mode.
 * \param hfp an hfp_pvt struct
 * \param mode the sms mode (0 = PDU, 1 = Text)
 */
static int hfp_send_cmgf(struct hfp_pvt *hfp, int mode)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+CMGF=%d\r", mode);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Setup SMS new message indication.
 * \param hfp an hfp_pvt struct
 * \param mode CNMI mode: 0=primary (2,1,0,0,0), 1=fallback1 (1,1,0,0,0), 2=fallback2 (1,2,0,0,0), 3=(1,1,0,0,1)
 */
static int hfp_send_cnmi(struct hfp_pvt *hfp, int mode)
{
	/* Try different CNMI configurations based on mode parameter
	 * Note: CNMI mode values mean:
	 *   0 = buffer in TA
	 *   1 = discard old indication and forward new
	 *   2 = buffer in TA and forward new
	 *   3 = forward if DTE-DCE link active, else behave as mode 0
	 */
	switch (mode) {
	case 0:
		return rfcomm_write(hfp->rsock, "AT+CNMI=2,1,0,0,0\r");  /* mode 2, mt 1 */
	case 1:
		return rfcomm_write(hfp->rsock, "AT+CNMI=1,1,0,0,0\r");  /* mode 1, mt 1 */
	case 2:
		return rfcomm_write(hfp->rsock, "AT+CNMI=1,2,0,0,0\r");  /* mode 1, mt 2 */
	case 3:
		return rfcomm_write(hfp->rsock, "AT+CNMI=3,1,0,0,0\r");  /* mode 3, mt 1 (forward if link active) */
	case 4:
		return rfcomm_write(hfp->rsock, "AT+CNMI=3,2,0,0,0\r");  /* mode 3, mt 2 (forward if link active) */
	case 5:
		return rfcomm_write(hfp->rsock, "AT+CNMI=1,1,0,0,1\r");  /* mode 1, mt 1, bfr 1 */
	default:
		return rfcomm_write(hfp->rsock, "AT+CNMI=2,1,0,0,0\r");
	}
}

/*!
 * \brief Send custom CNMI command with specific parameters.
 * \param hfp an hfp_pvt struct
 * \param mode,mt,bm,ds,bfr CNMI parameter values
 */
static int hfp_send_cnmi_custom(struct hfp_pvt *hfp, int mode, int mt, int bm, int ds, int bfr)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "AT+CNMI=%d,%d,%d,%d,%d\r", mode, mt, bm, ds, bfr);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Query supported CNMI parameter ranges.
 * \param hfp an hfp_pvt struct
 */
static int hfp_send_cnmi_test(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CNMI=?\r");
}

/*!
 * \brief Parse +CNMI: test response and extract valid values for each parameter.
 * \param buf Response buffer containing "+CNMI: (vals),(vals),..."
 * \param mode_vals Array to store valid mode values (terminated by -1)
 * \param mt_vals Array to store valid mt values (terminated by -1)
 * \param bm_vals Array to store valid bm values (terminated by -1)
 * \param ds_vals Array to store valid ds values (terminated by -1)
 * \param bfr_vals Array to store valid bfr values (terminated by -1)
 * \return 0 on success, -1 on parse error
 *
 * Expected format: +CNMI: (0,1,2),(0,1,2),(0,2),(0,1),(0,1)
 * Each parenthesized group contains valid values for that parameter.
 */
static int hfp_parse_cnmi_test(const char *buf, int *mode_vals, int *mt_vals, 
                               int *bm_vals, int *ds_vals, int *bfr_vals)
{
	const char *p;
	int *arrays[5] = {mode_vals, mt_vals, bm_vals, ds_vals, bfr_vals};
	int param = 0;
	int idx = 0;
	
	/* Initialize all arrays with terminator */
	for (int i = 0; i < 5; i++) {
		arrays[i][0] = -1;
	}
	
	p = strchr(buf, ':');
	if (!p) {
		return -1;
	}
	p++;
	
	while (*p && param < 5) {
		/* Skip whitespace */
		while (*p == ' ') {
			p++;
		}
		
		if (*p == '(') {
			p++;
			idx = 0;
			/* Parse values in parentheses */
			while (*p && *p != ')' && idx < 9) {
				if (*p >= '0' && *p <= '9') {
					arrays[param][idx++] = *p - '0';
					arrays[param][idx] = -1;  /* Terminate */
				}
				p++;
			}
			if (*p == ')') {
				p++;
			}
			param++;
		} else if (*p == ',') {
			p++;
		} else {
			p++;
		}
	}
	
	return (param >= 2) ? 0 : -1;  /* Need at least mode and mt params */
}

/*!
 * \brief Check if a value is in a list of valid values.
 * \param val Value to check
 * \param valid_list Array of valid values terminated by -1
 * \return 1 if valid, 0 if not
 */
static int cnmi_value_valid(int val, const int *valid_list)
{
	for (int i = 0; valid_list[i] != -1; i++) {
		if (valid_list[i] == val) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \brief Select the best CNMI mode value from available options.
 * Priority: 2 (buffer+forward), 1 (discard buffer+forward), 0 (buffer only)
 * We prefer mode 2 or 1 because they forward new SMS to TE immediately.
 */
static int cnmi_select_mode(const int *valid)
{
	/* Prefer 2, then 1, then 0 */
	if (cnmi_value_valid(2, valid)) {
		return 2;
	}
	if (cnmi_value_valid(1, valid)) {
		return 1;
	}
	if (cnmi_value_valid(0, valid)) {
		return 0;
	}
	return -1;  /* No valid value */
}

/*!
 * \brief Select the best CNMI mt (message type) value from available options.
 * Priority: 1 (+CMTI notification), 2 (direct +CMT), 0 (no indication)
 * We prefer 1 because it tells us a message arrived and where to read it.
 */
static int cnmi_select_mt(const int *valid)
{
	/* Prefer 1 (CMTI), then 2 (CMT direct), avoid 0 (no notification) */
	if (cnmi_value_valid(1, valid)) {
		return 1;
	}
	if (cnmi_value_valid(2, valid)) {
		return 2;
	}
	return 0;  /* Fall back to 0, meaning no notifications */
}

/*!
 * \brief Select CNMI bm (broadcast message) value - we don't need CBM.
 */
static int cnmi_select_bm(const int *valid)
{
	/* We don't use CBM, prefer 0 */
	if (cnmi_value_valid(0, valid)) {
		return 0;
	}
	if (valid[0] != -1) {
		return valid[0];
	}
	return 0;
}

/*!
 * \brief Select CNMI ds (delivery status) value - nice to have but not required.
 */
static int cnmi_select_ds(const int *valid)
{
	/* Prefer 1 (show status reports), but 0 is fine */
	if (cnmi_value_valid(1, valid)) {
		return 1;
	}
	if (cnmi_value_valid(0, valid)) {
		return 0;
	}
	if (valid[0] != -1) {
		return valid[0];
	}
	return 0;
}

/*!
 * \brief Select CNMI bfr (buffer) value.
 * This controls whether buffer is flushed on mode exit.
 */
static int cnmi_select_bfr(const int *valid)
{
	/* Prefer 0 (flush), then 1 (clear), then whatever is available */
	if (cnmi_value_valid(0, valid)) {
		return 0;
	}
	if (cnmi_value_valid(1, valid)) {
		return 1;
	}
	if (valid[0] != -1) {
		return valid[0];
	}
	return 0;
}

/*!
 * \brief Format CNMI parameter list for logging.
 */
static void cnmi_format_values(const int *vals, char *buf, size_t len)
{
	char *p = buf;
	size_t remaining = len;
	int first = 1;
	
	for (int i = 0; vals[i] != -1 && remaining > 2; i++) {
		int written = snprintf(p, remaining, "%s%d", first ? "" : ",", vals[i]);
		if (written > 0 && (size_t)written < remaining) {
			p += written;
			remaining -= written;
		}
		first = 0;
	}
	if (first) {
		ast_copy_string(buf, "(none)", len);
	}
}

/*!
 * \brief Log decoded CNMI parameters with explanations.
 */
static void cnmi_log_parsed(const char *devid, const int *mode_vals, const int *mt_vals,
                            const int *bm_vals, const int *ds_vals, const int *bfr_vals)
{
	char mode_str[32], mt_str[32], bm_str[32], ds_str[32], bfr_str[32];
	
	cnmi_format_values(mode_vals, mode_str, sizeof(mode_str));
	cnmi_format_values(mt_vals, mt_str, sizeof(mt_str));
	cnmi_format_values(bm_vals, bm_str, sizeof(bm_str));
	cnmi_format_values(ds_vals, ds_str, sizeof(ds_str));
	cnmi_format_values(bfr_vals, bfr_str, sizeof(bfr_str));
	
	ast_log(LOG_NOTICE, "[%s] CNMI supported parameters:\n", devid);
	ast_log(LOG_NOTICE, "[%s]   mode=%s (0=buffer, 1=discard+forward, 2=buffer+forward, 3=forward if link)\n", devid, mode_str);
	ast_log(LOG_NOTICE, "[%s]   mt=%s (0=none, 1=+CMTI index, 2=+CMT direct, 3=class3 direct)\n", devid, mt_str);
	ast_log(LOG_NOTICE, "[%s]   bm=%s (0=none, 2=+CBM direct, 3=class3 CBM)\n", devid, bm_str);
	ast_log(LOG_NOTICE, "[%s]   ds=%s (0=none, 1=+CDS status reports, 2=class2 buffer)\n", devid, ds_str);
	ast_log(LOG_NOTICE, "[%s]   bfr=%s (0=flush buffer to TE, 1=clear buffer)\n", devid, bfr_str);
}

/*!
 * \brief Read an SMS message.
 * \param hfp an hfp_pvt struct
 * \param index the location of the requested message
 */
static int hfp_send_cmgr(struct hfp_pvt *hfp, int index)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+CMGR=%d\r", index);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Start sending an SMS message.
 * \param hfp an hfp_pvt struct
 * \param number the destination of the message
 */
static int hfp_send_cmgs(struct hfp_pvt *hfp, const char *number)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r", number);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Start sending an SMS message in PDU mode.
 * \param hfp an hfp_pvt struct
 * \param pdu_len length of PDU in octets (excluding SMSC byte)
 */
static int hfp_send_cmgs_pdu(struct hfp_pvt *hfp, int pdu_len)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "AT+CMGS=%d\r", pdu_len);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send the text of an SMS message.
 * \param hfp an hfp_pvt struct
 * \param message the text of the message
 *
 * For UCS-2 messages with UDH (concatenated SMS):
 * - UDH: 12 hex chars
 * - Message: 67 UCS-2 chars × 4 = 268 hex chars
 * - Total: 280 hex chars max per part
 * Buffer sized for single-part 70 UCS-2 chars (280 hex) plus Ctrl-Z
 */
static int hfp_send_sms_text(struct hfp_pvt *hfp, const char *message)
{
	char cmd[320];  /* 280 hex max + margin + Ctrl-Z + null */
	snprintf(cmd, sizeof(cmd), "%.300s\x1a", message);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send SMS PDU data (hex string followed by Ctrl-Z).
 * \param hfp an hfp_pvt struct
 * \param pdu_hex hex-encoded PDU string
 */
static int hfp_send_sms_pdu(struct hfp_pvt *hfp, const char *pdu_hex)
{
	char cmd[520];  /* Max PDU ~175 bytes = 350 hex + \x1a + null */
	snprintf(cmd, sizeof(cmd), "%s\x1a", pdu_hex);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send AT+CHUP.
 * \param hfp an hfp_pvt struct
 */
static int hfp_send_chup(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CHUP\r");
}

/*!
 * \brief Send ATD.
 * \param hfp an hfp_pvt struct
 * \param number the number to send
 */
static int hfp_send_atd(struct hfp_pvt *hfp, const char *number)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "ATD%s;\r", number);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Send ATA.
 * \param hfp an hfp_pvt struct
 */
static int hfp_send_ata(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "ATA\r");
}

/*!
 * \brief Send CUSD.
 * \param hfp an hfp_pvt struct
 * \param code the CUSD code to send
 */
static int hfp_send_cusd(struct hfp_pvt *hfp, const char *code)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "AT+CUSD=1,\"%s\",15\r", code);
	return rfcomm_write(hfp->rsock, cmd);
}

/*!
 * \brief Parse BRSF data.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 */

/*!
 * \brief Detect HFP version from BRSF feature bits.
 * \param brsf the raw BRSF value from +BRSF response
 * \return HFP version: 10=1.0, 15=1.5, 16=1.6, 17=1.7
 */
static int hfp_detect_version(int brsf)
{
	if (brsf & HFP_AG_ESCO_S4) {
		return 17;  /* eSCO S4 → HFP 1.7 */
	}
	if (brsf & HFP_AG_HFIND) {
		return 17;  /* HF indicators → HFP 1.7 */
	}
	if (brsf & HFP_AG_CODEC) {
		return 16;  /* Codec negotiation → HFP 1.6 */
	}
	if (brsf & HFP_AG_CONTROL) {
		return 15;  /* Enhanced call control → HFP 1.5 */
	}
	if (brsf & HFP_AG_STATUS) {
		return 15;  /* Enhanced call status → HFP 1.5 */
	}
	return 10;  /* Baseline HFP 1.0 */
}

static int hfp_parse_brsf(struct hfp_pvt *hfp, const char *buf)
{
	int brsf;

	if (!sscanf(buf, "+BRSF:%d", &brsf)) {
		return -1;
	}

	hfp->brsf_raw = brsf;
	hfp->hfp_version = hfp_detect_version(brsf);
	hfp_int2brsf(brsf, &hfp->brsf);

	ast_verb(3, "[%s] Phone HFP %d.%d (BRSF=%d)%s\n",
		hfp->owner->id,
		hfp->hfp_version / 10, hfp->hfp_version % 10,
		brsf,
		(brsf & HFP_AG_CODEC) ? " [codec-neg]" : " [CVSD-only]");

	return 0;
}

/*!
 * \brief Parse and store the given indicator.
 * \param hfp an hfp_pvt struct
 * \param group the indicator group
 * \param indicator the indicator to parse
 */
static int hfp_parse_cind_indicator(struct hfp_pvt *hfp, int group, char *indicator)
{
	int value;

	/* store the current indicator */
	if (group >= ARRAY_LEN(hfp->cind_state)) {
		ast_debug(1, "ignoring CIND state '%s' for group %d, we only support up to %d indicators\n", indicator, group, (int) sizeof(hfp->cind_state));
		return -1;
	}

	if (!sscanf(indicator, "%d", &value)) {
		ast_debug(1, "error parsing CIND state '%s' for group %d\n", indicator, group);
		return -1;
	}

	hfp->cind_state[group] = value;
	return 0;
}

/*!
 * \brief Read the result of the AT+CIND? command.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \note hfp_send_cind_test() and hfp_parse_cind_test() should be called at
 * least once before this function is called.
 */
static int hfp_parse_cind(struct hfp_pvt *hfp, char *buf)
{
	int i, state, group;
	size_t s;
	char *indicator = NULL;

	/* parse current state of all of our indicators.  The list is in the
	 * following format:
	 * +CIND: 1,0,2,0,0,0,0
	 */
	group = 0;
	state = 0;
	s = strlen(buf);
	for (i = 0; i < s; i++) {
		switch (state) {
		case 0: /* search for start of the status indicators (a space) */
			if (buf[i] == ' ') {
				group++;
				state++;
			}
			break;
		case 1: /* mark this indicator */
			indicator = &buf[i];
			state++;
			break;
		case 2: /* search for the start of the next indicator (a comma) */
			if (buf[i] == ',') {
				buf[i] = '\0';

				hfp_parse_cind_indicator(hfp, group, indicator);

				group++;
				state = 1;
			}
			break;
		}
	}

	/* store the last indicator */
	if (state == 2) {
		hfp_parse_cind_indicator(hfp, group, indicator);
	}

	return 0;
}

/*!
 * \brief Parse the result of the AT+CIND=? command.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 */
static int hfp_parse_cind_test(struct hfp_pvt *hfp, char *buf)
{
	int i, state, group;
	size_t s;
	char *indicator = NULL;

	hfp->nocallsetup = 1;

	/* parse the indications list.  It is in the follwing format:
	 * +CIND: ("ind1",(0-1)),("ind2",(0-5))
	 */
	group = 0;
	state = 0;
	s = strlen(buf);
	for (i = 0; i < s; i++) {
		switch (state) {
		case 0: /* search for start of indicator block */
			if (buf[i] == '(') {
				group++;
				state++;
			}
			break;
		case 1: /* search for '"' in indicator block */
			if (buf[i] == '"') {
				state++;
			}
			break;
		case 2: /* mark the start of the indicator name */
			indicator = &buf[i];
			state++;
			break;
		case 3: /* look for the end of the indicator name */
			if (buf[i] == '"') {
				buf[i] = '\0';
				state++;
			}
			break;
		case 4: /* find the start of the value range */
			if (buf[i] == '(') {
				state++;
			}
			break;
		case 5: /* mark the start of the value range */
			state++;
			break;
		case 6: /* find the end of the value range */
			if (buf[i] == ')') {
				buf[i] = '\0';
				state++;
			}
			break;
		case 7: /* process the values we found */
			if (group < sizeof(hfp->cind_index)) {
				if (!strcmp(indicator, "service")) {
					hfp->cind_map.service = group;
					hfp->cind_index[group] = HFP_CIND_SERVICE;
				} else if (!strcmp(indicator, "call")) {
					hfp->cind_map.call = group;
					hfp->cind_index[group] = HFP_CIND_CALL;
				} else if (!strcmp(indicator, "callsetup")) {
					hfp->nocallsetup = 0;
					hfp->cind_map.callsetup = group;
					hfp->cind_index[group] = HFP_CIND_CALLSETUP;
				} else if (!strcmp(indicator, "call_setup")) { /* non standard call setup identifier */
					hfp->nocallsetup = 0;
					hfp->cind_map.callsetup = group;
					hfp->cind_index[group] = HFP_CIND_CALLSETUP;
				} else if (!strcmp(indicator, "callheld")) {
					hfp->cind_map.callheld = group;
					hfp->cind_index[group] = HFP_CIND_CALLHELD;
				} else if (!strcmp(indicator, "signal")) {
					hfp->cind_map.signal = group;
					hfp->cind_index[group] = HFP_CIND_SIGNAL;
				} else if (!strcmp(indicator, "roam")) {
					hfp->cind_map.roam = group;
					hfp->cind_index[group] = HFP_CIND_ROAM;
				} else if (!strcmp(indicator, "battchg")) {
					hfp->cind_map.battchg = group;
					hfp->cind_index[group] = HFP_CIND_BATTCHG;
				} else {
					hfp->cind_index[group] = HFP_CIND_UNKNOWN;
					ast_debug(2, "ignoring unknown CIND indicator '%s'\n", indicator);
				}
			} else {
					ast_debug(1, "can't store indicator %d (%s), we only support up to %d indicators", group, indicator, (int) sizeof(hfp->cind_index));
			}

			state = 0;
			break;
		}
	}

	hfp->owner->no_callsetup = hfp->nocallsetup;

	return 0;
}

/*
 * Bluetooth Headset Profile helpers
 */

/*!
 * \brief Send an OK AT response.
 * \param rsock the rfcomm socket to use
 */
static int hsp_send_ok(int rsock)
{
	return rfcomm_write(rsock, "\r\nOK\r\n");
}

/*!
 * \brief Send an ERROR AT response.
 * \param rsock the rfcomm socket to use
 */
static int hsp_send_error(int rsock)
{
	return rfcomm_write(rsock, "\r\nERROR\r\n");
}

/*!
 * \brief Send a speaker gain unsolicited AT response
 * \param rsock the rfcomm socket to use
 * \param gain the speaker gain value
 */
static int hsp_send_vgs(int rsock, int gain)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "\r\n+VGS=%d\r\n", gain);
	return rfcomm_write(rsock, cmd);
}

/*!
 * \brief Send a microphone gain unsolicited AT response
 * \param rsock the rfcomm socket to use
 * \param gain the microphone gain value
 */
static int hsp_send_vgm(int rsock, int gain)
{
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "\r\n+VGM=%d\r\n", gain);
	return rfcomm_write(rsock, cmd);
}

/*!
 * \brief Send a RING unsolicited AT response.
 * \param rsock the rfcomm socket to use
 */
static int hsp_send_ring(int rsock)
{
	return rfcomm_write(rsock, "\r\nRING\r\n");
}

/*
 * message queue functions
 */

/*!
 * \brief Add an item to the back of the queue.
 * \param pvt a mbl_pvt structure
 * \param expect the msg we expect to receive
 * \param response_to the message that was sent to generate the expected
 * response
 */
static int msg_queue_push(struct mbl_pvt *pvt, enum at_message expect, enum at_message response_to)
{
	struct msg_queue_entry *msg;
	if (!(msg = ast_calloc(1, sizeof(*msg)))) {
		return -1;
	}
	msg->expected = expect;
	msg->response_to = response_to;

	AST_LIST_INSERT_TAIL(&pvt->msg_queue, msg, entry);
	return 0;
}

/*!
 * \brief Add an item to the back of the queue with data.
 * \param pvt a mbl_pvt structure
 * \param expect the msg we expect to receive
 * \param response_to the message that was sent to generate the expected
 * response
 * \param data data associated with this message, it will be freed when the
 * message is freed
 */
static int msg_queue_push_data(struct mbl_pvt *pvt, enum at_message expect, enum at_message response_to, void *data)
{
	struct msg_queue_entry *msg;
	if (!(msg = ast_calloc(1, sizeof(*msg)))) {
		return -1;
	}
	msg->expected = expect;
	msg->response_to = response_to;
	msg->data = data;

	AST_LIST_INSERT_TAIL(&pvt->msg_queue, msg, entry);
	return 0;
}

/*!
 * \brief Remove an item from the front of the queue.
 * \param pvt a mbl_pvt structure
 * \return a pointer to the removed item
 */
static struct msg_queue_entry *msg_queue_pop(struct mbl_pvt *pvt)
{
	return AST_LIST_REMOVE_HEAD(&pvt->msg_queue, entry);
}

/*!
 * \brief Remove an item from the front of the queue, and free it.
 * \param pvt a mbl_pvt structure
 */
static void msg_queue_free_and_pop(struct mbl_pvt *pvt)
{
	struct msg_queue_entry *msg;
	if ((msg = msg_queue_pop(pvt))) {
		if (msg->data) {
			ast_free(msg->data);
		}
		ast_free(msg);
	}
}

/*!
 * \brief Remove all items from the queue and free them.
 * \param pvt a mbl_pvt structure
 */
static void msg_queue_flush(struct mbl_pvt *pvt)
{
	struct msg_queue_entry *msg;
	while ((msg = msg_queue_head(pvt))) {
		msg_queue_free_and_pop(pvt);
	}
}

/*!
 * \brief Get the head of a queue.
 * \param pvt a mbl_pvt structure
 * \return a pointer to the head of the given msg queue
 */
static struct msg_queue_entry *msg_queue_head(struct mbl_pvt *pvt)
{
	return AST_LIST_FIRST(&pvt->msg_queue);
}

/*
 * sdp helpers
 */

static int sdp_search(char *addr, int profile)
{

	sdp_session_t *session = 0;
	bdaddr_t bdaddr;
	uuid_t svc_uuid;
	uint32_t range = 0x0000ffff;
	sdp_list_t *response_list, *search_list, *attrid_list;
	int status, port;
	sdp_list_t *proto_list;
	sdp_record_t *sdprec;

	str2ba(addr, &bdaddr);
	port = 0;
	session = sdp_connect(BDADDR_ANY, &bdaddr, SDP_RETRY_IF_BUSY);
	if (!session) {
		/* Connection failed - this is a transient error, return -1 */
		ast_debug(1, "sdp_connect() failed on device %s: %s (%d)\n", addr, strerror(errno), errno);
		return -1;  /* Transient error - device unreachable */
	}

	sdp_uuid32_create(&svc_uuid, profile);
	search_list = sdp_list_append(0, &svc_uuid);
	attrid_list = sdp_list_append(0, &range);
	response_list = 0x00;
	status = sdp_service_search_attr_req(session, search_list, SDP_ATTR_REQ_RANGE, attrid_list, &response_list);
	if (status == 0) {
		if (response_list) {
			sdprec = (sdp_record_t *) response_list->data;
			proto_list = 0x00;
			if (sdp_get_access_protos(sdprec, &proto_list) == 0) {
				port = sdp_get_proto_port(proto_list, RFCOMM_UUID);
				sdp_list_free(proto_list, 0);
			}
			sdp_record_free(sdprec);
			sdp_list_free(response_list, 0);
		} else {
			ast_debug(1, "No responses returned for device %s (profile not supported).\n", addr);
		}
	} else {
		ast_debug(1, "sdp_service_search_attr_req() failed on device %s.\n", addr);
	}

	sdp_list_free(search_list, 0);
	sdp_list_free(attrid_list, 0);
	sdp_close(session);

	return port;  /* 0 = profile not found, >0 = port number */

}

static sdp_session_t *sdp_register(void)
{
	uint32_t service_uuid_int[] = {0, 0, 0, GENERIC_AUDIO_SVCLASS_ID};
	uint8_t rfcomm_channel = 1;
	const char *service_name = "Asterisk PABX";
	const char *service_dsc = "Asterisk PABX";
	const char *service_prov = "Asterisk";

	uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid, svc_class1_uuid, svc_class2_uuid;
	sdp_list_t  *l2cap_list = 0, *rfcomm_list = 0, *root_list = 0, *proto_list = 0, *access_proto_list = 0, *svc_uuid_list = 0;
	sdp_data_t *channel = 0;

	sdp_session_t *session = 0;

	sdp_record_t *record = sdp_record_alloc();

	sdp_uuid128_create(&svc_uuid, &service_uuid_int);
	sdp_set_service_id(record, svc_uuid);

	sdp_uuid32_create(&svc_class1_uuid, GENERIC_AUDIO_SVCLASS_ID);
	sdp_uuid32_create(&svc_class2_uuid, HEADSET_PROFILE_ID);

	svc_uuid_list = sdp_list_append(0, &svc_class1_uuid);
	svc_uuid_list = sdp_list_append(svc_uuid_list, &svc_class2_uuid);
	sdp_set_service_classes(record, svc_uuid_list);

	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root_list = sdp_list_append(0, &root_uuid);
	sdp_set_browse_groups( record, root_list );

	sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
	l2cap_list = sdp_list_append(0, &l2cap_uuid);
	proto_list = sdp_list_append(0, l2cap_list);

	sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
	channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
	rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
	sdp_list_append(rfcomm_list, channel);
	sdp_list_append(proto_list, rfcomm_list);

	access_proto_list = sdp_list_append(0, proto_list);
	sdp_set_access_protos(record, access_proto_list);

	sdp_set_info_attr(record, service_name, service_prov, service_dsc);

	if (!(session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY))) {
		ast_log(LOG_WARNING, "Failed to connect sdp and create session.\n");
	} else {
		if (sdp_record_register(session, record, 0) < 0) {
			ast_log(LOG_WARNING, "Failed to sdp_record_register error: %d\n", errno);
			return NULL;
		}
	}

	sdp_data_free(channel);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	sdp_list_free(svc_uuid_list, 0);

	return session;

}

/*
 * Thread routines
 */

/*!
 * \brief Handle the BRSF response.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_brsf(struct mbl_pvt *pvt, char *buf)
{
	struct msg_queue_entry *entry;
	if ((entry = msg_queue_head(pvt)) && entry->expected == AT_BRSF) {
		if (hfp_parse_brsf(pvt->hfp, buf)) {
			ast_debug(1, "[%s] error parsing BRSF\n", pvt->id);
			goto e_return;
		}

		/* Log device features */
		ast_verb(3, "[%s] Device features: %s%s%s%s%s%s%s%s%s\n", pvt->id,
			pvt->hfp->brsf.cw ? "3-Way " : "",
			pvt->hfp->brsf.ecnr ? "EC/NR " : "",
			pvt->hfp->brsf.voice ? "Voice " : "",
			pvt->hfp->brsf.ring ? "InBandRing " : "",
			pvt->hfp->brsf.tag ? "VoiceTag " : "",
			pvt->hfp->brsf.reject ? "Reject " : "",
			pvt->hfp->brsf.status ? "EnhStatus " : "",
			pvt->hfp->brsf.control ? "EnhControl " : "",
			pvt->hfp->brsf.errors ? "ExtErrors" : "");

		if (msg_queue_push(pvt, AT_OK, AT_BRSF)) {
			ast_debug(1, "[%s] error handling BRSF\n", pvt->id);
			goto e_return;
		}

		msg_queue_free_and_pop(pvt);
	} else if (entry) {
		ast_debug(1, "[%s] received unexpected AT message 'BRSF' when expecting %s, ignoring\n", pvt->id, at_msg2str(entry->expected));
	} else {
		ast_debug(1, "[%s] received unexpected AT message 'BRSF'\n", pvt->id);
	}

	return 0;

e_return:
	msg_queue_free_and_pop(pvt);
	return -1;
}

/*!
 * \brief Handle the CIND response.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cind(struct mbl_pvt *pvt, char *buf)
{
	struct msg_queue_entry *entry;
	if ((entry = msg_queue_head(pvt)) && entry->expected == AT_CIND) {
		switch (entry->response_to) {
		case AT_CIND_TEST:
			if (hfp_parse_cind_test(pvt->hfp, buf) || msg_queue_push(pvt, AT_OK, AT_CIND_TEST)) {
				ast_debug(1, "[%s] error performing CIND test\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CIND:
			if (hfp_parse_cind(pvt->hfp, buf) || msg_queue_push(pvt, AT_OK, AT_CIND)) {
				ast_debug(1, "[%s] error getting CIND state\n", pvt->id);
				goto e_return;
			}
			break;
		default:
			ast_debug(1, "[%s] error getting CIND state\n", pvt->id);
			goto e_return;
		}
		msg_queue_free_and_pop(pvt);
	} else if (entry) {
		ast_debug(1, "[%s] received unexpected AT message 'CIND' when expecting %s, ignoring\n", pvt->id, at_msg2str(entry->expected));
	} else {
		ast_debug(1, "[%s] received unexpected AT message 'CIND'\n", pvt->id);
	}

	return 0;

e_return:
	msg_queue_free_and_pop(pvt);
	return -1;
}

/*!
 * \brief Handle OK AT messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_ok(struct mbl_pvt *pvt, char *buf)
{
	struct msg_queue_entry *entry;
	if ((entry = msg_queue_head(pvt)) && entry->expected == AT_OK) {
		switch (entry->response_to) {

		/* initialization stuff */
		case AT_BRSF:
			/* Query for CSCS support before proceeding */
			if (hfp_send_cscs(pvt->hfp, NULL) || msg_queue_push(pvt, AT_CSCS, AT_CSCS)) {
				ast_debug(1, "[%s] error sending CSCS query\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CSCS:
			/* Select best charset in priority order: UTF-8 > UCS2 > GSM */
			if (pvt->has_utf8 && !pvt->cscs_active[0]) {
				ast_debug(1, "[%s] Charsets: %s\n", pvt->id, pvt->cscs_list);
				ast_debug(1, "[%s] Selecting UTF-8 charset\n", pvt->id);
				if (hfp_send_cscs(pvt->hfp, "UTF-8") || msg_queue_push(pvt, AT_OK, AT_CSCS_SET)) {
					ast_debug(1, "[%s] error sending CSCS set\n", pvt->id);
					goto e_return;
				}
				ast_copy_string(pvt->cscs_active, "UTF-8", sizeof(pvt->cscs_active));
				break;
			} else if (pvt->has_ucs2 && !pvt->cscs_active[0]) {
				ast_debug(1, "[%s] Charsets: %s\n", pvt->id, pvt->cscs_list);
				ast_debug(1, "[%s] Selecting UCS2 charset (Unicode with hex encoding)\n", pvt->id);
				if (hfp_send_cscs(pvt->hfp, "UCS2") || msg_queue_push(pvt, AT_OK, AT_CSCS_SET)) {
					ast_debug(1, "[%s] error sending CSCS set\n", pvt->id);
					goto e_return;
				}
				ast_copy_string(pvt->cscs_active, "UCS2", sizeof(pvt->cscs_active));
				break;
			} else if (pvt->has_gsm && !pvt->cscs_active[0]) {
				/* Explicitly set GSM to ensure consistent behavior */
				ast_debug(1, "[%s] Charsets: %s\n", pvt->id, pvt->cscs_list);
				ast_debug(1, "[%s] Selecting GSM 7-bit charset\n", pvt->id);
				if (hfp_send_cscs(pvt->hfp, "GSM") || msg_queue_push(pvt, AT_OK, AT_CSCS_SET)) {
					ast_debug(1, "[%s] error sending CSCS set\n", pvt->id);
					goto e_return;
				}
				ast_copy_string(pvt->cscs_active, "GSM", sizeof(pvt->cscs_active));
				break;
			} else if (!pvt->cscs_active[0]) {
				/* Fallback to IRA/default */
				ast_copy_string(pvt->cscs_active, "IRA", sizeof(pvt->cscs_active));
			}
			/* Fall through to continue initialization */
		case AT_CSCS_VERIFY:
			ast_verb(3, "[%s] Charset: %s (supported: %s%s%s%s)\n",
				pvt->id, pvt->cscs_active,
				pvt->has_utf8 ? "UTF-8 " : "",
				pvt->has_ucs2 ? "UCS2 " : "",
				pvt->has_gsm ? "GSM " : "",
				pvt->has_ira ? "IRA " : "");
			/* Fall through to CIND */
		case AT_CSCS_SET:
			if (entry->response_to == AT_CSCS_SET) {
				/* Charset was set, continue with CIND */
				ast_debug(1, "[%s] Charset %s set successfully\n", pvt->id, pvt->cscs_active);
			}

			/* If this is a blackberry do CMER now, otherwise
			 * continue with CIND as normal. */
			if (pvt->blackberry) {
				if (hfp_send_cmer(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMER)) {
					ast_debug(1, "[%s] error sending CMER\n", pvt->id);
					goto e_return;
				}
			} else {
				if (hfp_send_cind_test(pvt->hfp) || msg_queue_push(pvt, AT_CIND, AT_CIND_TEST)) {
					ast_debug(1, "[%s] error sending CIND test\n", pvt->id);
					goto e_return;
				}
			}
			break;

		case AT_CIND_TEST:
			ast_debug(1, "[%s] CIND test sent successfully\n", pvt->id);

			ast_debug(2, "[%s] call: %d\n", pvt->id, pvt->hfp->cind_map.call);
			ast_debug(2, "[%s] callsetup: %d\n", pvt->id, pvt->hfp->cind_map.callsetup);
			ast_debug(2, "[%s] service: %d\n", pvt->id, pvt->hfp->cind_map.service);

			/* Check if we have a signal indicator */
			if (pvt->hfp->cind_map.signal == 0) {
				ast_verb(3, "[%s] Device has no signal indicator in CIND - enabling AT+CSQ polling\n", pvt->id);
				pvt->hfp->no_cind_signal = 1;
			}

			if (hfp_send_cind(pvt->hfp) || msg_queue_push(pvt, AT_CIND, AT_CIND)) {
				ast_debug(1, "[%s] error requesting CIND state\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CIND:
			ast_debug(1, "[%s] CIND sent successfully\n", pvt->id);

			/* check if a call is active */
			if (pvt->hfp->cind_state[pvt->hfp->cind_map.call]) {
				ast_verb(3, "Bluetooth Device %s has a call in progress - delaying connection.\n", pvt->id);
				goto e_return;
			}

			/* If this is NOT a blackberry proceed with CMER,
			 * otherwise send CLIP. */
			if (!pvt->blackberry) {
				if (hfp_send_cmer(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMER)) {
					ast_debug(1, "[%s] error sending CMER\n", pvt->id);
					goto e_return;
				}
			} else {
				if (hfp_send_clip(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CLIP)) {
					ast_debug(1, "[%s] error enabling calling line notification\n", pvt->id);
					goto e_return;
				}
			}
			break;
		case AT_CMER:
			ast_debug(1, "[%s] CMER sent successfully\n", pvt->id);

			/* If this is a blackberry proceed with the CIND test,
			 * otherwise send CLIP. */
			if (pvt->blackberry) {
				if (hfp_send_cind_test(pvt->hfp) || msg_queue_push(pvt, AT_CIND, AT_CIND_TEST)) {
					ast_debug(1, "[%s] error sending CIND test\n", pvt->id);
					goto e_return;
				}
			} else {
				if (hfp_send_clip(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CLIP)) {
					ast_debug(1, "[%s] error enabling calling line notification\n", pvt->id);
					goto e_return;
				}
			}
			break;
		case AT_CLIP:
			ast_debug(1, "[%s] calling line indication enabled\n", pvt->id);
			if (hfp_send_ecam(pvt->hfp) || msg_queue_push(pvt, AT_OK, AT_ECAM)) {
				ast_debug(1, "[%s] error enabling Sony Ericsson call monitoring extensions\n", pvt->id);
				goto e_return;
			}

			break;
		case AT_ECAM:
			ast_debug(1, "[%s] Sony Ericsson call monitoring is active on device\n", pvt->id);
			if (hfp_send_vgs(pvt->hfp, 15) || msg_queue_push(pvt, AT_OK, AT_VGS)) {
				ast_debug(1, "[%s] error synchronizing gain settings\n", pvt->id);
				goto e_return;
			}

			pvt->timeout = -1;
			pvt->hfp->initialized = 1;
			mbl_set_state(pvt, MBL_STATE_READY);
			ast_verb(3, "Bluetooth Device %s initialized and ready.\n", pvt->id);

			/* Process any SMS that arrived during initialization */
			process_pending_sms(pvt);

			break;
		case AT_VGS:
			ast_debug(1, "[%s] volume level synchronization successful\n", pvt->id);

			/* Try to set SMS operating mode - text mode first */
			if (pvt->sms_mode != SMS_MODE_OFF) {
				ast_debug(1, "[%s] SMS: attempting to enable text mode (AT+CMGF=1)\n", pvt->id);
				if (hfp_send_cmgf(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMGF)) {
					ast_debug(1, "[%s] error setting CMGF\n", pvt->id);
					goto e_return;
				}
			}
			break;
		case AT_CMGF:
			ast_debug(1, "[%s] SMS: text mode (AT+CMGF=1) accepted\n", pvt->id);
			pvt->sms_mode = SMS_MODE_TEXT;
			/* turn on SMS new message indication */
			ast_debug(1, "[%s] SMS: enabling new message notifications (AT+CNMI)\n", pvt->id);
			if (hfp_send_cnmi(pvt->hfp, 0) || msg_queue_push(pvt, AT_OK, AT_CNMI)) {
				ast_debug(1, "[%s] error setting CNMI\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CMGF_PDU:
			ast_debug(1, "[%s] SMS: PDU mode (AT+CMGF=0) accepted\n", pvt->id);
			pvt->sms_mode = SMS_MODE_PDU;
			/* turn on SMS new message indication */
			ast_debug(1, "[%s] SMS: enabling new message notifications (AT+CNMI)\n", pvt->id);
			if (hfp_send_cnmi(pvt->hfp, 0) || msg_queue_push(pvt, AT_OK, AT_CNMI)) {
				ast_debug(1, "[%s] error setting CNMI\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CNMI:
		case AT_CNMI_FALLBACK1:
		case AT_CNMI_FALLBACK2:
		case AT_CNMI_FALLBACK3:
			ast_debug(1, "[%s] SMS: new message notifications enabled\n", pvt->id);
			ast_verb(3, "[%s] SMS: %s mode enabled, charset=%s\n",
				pvt->id, sms_mode_to_str(pvt->sms_mode),
				pvt->cscs_active[0] ? pvt->cscs_active : "default");
			/* Continue to device status queries */
			if (!pvt->hfp->no_cops) {
				if (hfp_send_cops(pvt->hfp, 2, 0) || msg_queue_push(pvt, AT_OK, AT_COPS_SET_NUMERIC)) {
					ast_debug(1, "[%s] error setting COPS numeric format\n", pvt->id);
					pvt->hfp->no_cops = 1;
				}
			}
			break;
		case AT_CNMI_TEST:
			/* CNMI test query OK - now send the auto-selected values if valid */
			if (pvt->cnmi_test_done && pvt->cnmi_selected[0] > 0 && pvt->cnmi_selected[1] > 0) {
				ast_verb(3, "[%s] SMS: Sending auto-selected AT+CNMI=%d,%d,%d,%d,%d\n",
					pvt->id, pvt->cnmi_selected[0], pvt->cnmi_selected[1],
					pvt->cnmi_selected[2], pvt->cnmi_selected[3], pvt->cnmi_selected[4]);
				if (hfp_send_cnmi_custom(pvt->hfp, 
				    pvt->cnmi_selected[0], pvt->cnmi_selected[1],
				    pvt->cnmi_selected[2], pvt->cnmi_selected[3], 
				    pvt->cnmi_selected[4]) || msg_queue_push(pvt, AT_OK, AT_CNMI_QUERY)) {
					ast_debug(1, "[%s] error sending custom CNMI\n", pvt->id);
					/* Continue anyway, sending still works */
				} else {
					break;  /* Wait for OK response */
				}
			} else {
				ast_verb(3, "[%s] SMS: CNMI test completed - no valid mode/mt, receiving disabled, sending enabled (%s mode)\n",
					pvt->id, sms_mode_to_str(pvt->sms_mode));
			}
			/* Fall through to continue initialization if no valid CNMI values */
			if (!pvt->hfp->no_cops) {
				if (hfp_send_cops(pvt->hfp, 2, 0) || msg_queue_push(pvt, AT_OK, AT_COPS_SET_NUMERIC)) {
					pvt->hfp->no_cops = 1;
				} else {
					break;
				}
			}
			goto chain_creg;
		case AT_CNMI_QUERY:
			/* Custom CNMI values accepted! */
			ast_debug(1, "[%s] SMS: Custom CNMI accepted - notifications enabled\n", pvt->id);
			ast_verb(3, "[%s] SMS: %s mode enabled (auto-configured CNMI), charset=%s\n",
				pvt->id, sms_mode_to_str(pvt->sms_mode),
				pvt->cscs_active[0] ? pvt->cscs_active : "default");
			/* Continue to device status queries */
			if (!pvt->hfp->no_cops) {
				if (hfp_send_cops(pvt->hfp, 2, 0) || msg_queue_push(pvt, AT_OK, AT_COPS_SET_NUMERIC)) {
					ast_debug(1, "[%s] error setting COPS numeric format\n", pvt->id);
					pvt->hfp->no_cops = 1;
				}
			}
			break;
		case AT_COPS_SET_NUMERIC:
			/* AT+COPS=3,2 OK received - send query for MCC/MNC */
			if (hfp_send_cops(pvt->hfp, 0, 1) || msg_queue_push(pvt, AT_COPS, AT_COPS_SET_NUMERIC)) {
				ast_debug(1, "[%s] error querying COPS (numeric)\n", pvt->id);
				pvt->hfp->no_cops = 1;
				goto chain_creg;
			}
			break;
		case AT_COPS_QUERY:
			/* After numeric COPS query - now query alphanumeric format for provider name */
			if (hfp_send_cops(pvt->hfp, 0, 0) || msg_queue_push(pvt, AT_OK, AT_COPS_SET_ALPHA)) {
				ast_debug(1, "[%s] error setting COPS alphanumeric format\n", pvt->id);
				/* Non-fatal, continue without provider name */
				goto chain_creg;
			}
			break;
		case AT_COPS_SET_ALPHA:
			/* AT+COPS=3,0 OK received - send query for provider name */
			if (hfp_send_cops(pvt->hfp, 0, 1) || msg_queue_push(pvt, AT_COPS, AT_COPS_SET_ALPHA)) {
				ast_debug(1, "[%s] error querying COPS (alpha)\n", pvt->id);
				goto chain_creg;
			}
			break;
		case AT_COPS_DONE:
			/* COPS query chain completed - continue to CREG */
			goto chain_creg;

		/* Device status query chain - CREG */
		case AT_CREG_SET:
			/* Query current CREG status */
			if (hfp_send_creg(pvt->hfp, -1) || msg_queue_push(pvt, AT_CREG, AT_CREG)) {
				ast_debug(1, "[%s] error querying CREG\n", pvt->id);
				pvt->hfp->no_creg = 1;
				goto chain_cgreg;
			}
			break;
		case AT_CREG:
			ast_debug(1, "[%s] CREG status received\n", pvt->id);
			goto chain_cgreg;

		/* Device status query chain - CGREG */
		case AT_CGREG_SET:
			/* Query current CGREG status */
			if (hfp_send_cgreg(pvt->hfp, -1) || msg_queue_push(pvt, AT_CGREG, AT_CGREG)) {
				ast_debug(1, "[%s] error querying CGREG\n", pvt->id);
				pvt->hfp->no_cgreg = 1;
				goto chain_cbc;
			}
			break;
		case AT_CGREG:
			ast_debug(1, "[%s] CGREG status received\n", pvt->id);
			goto chain_cbc;

		/* Device status query chain - CBC (battery) */
		case AT_CBC:
			ast_debug(1, "[%s] CBC battery status received\n", pvt->id);
			/* Start periodic status polling (every 5 minutes) */
			if (pvt->status_sched_id == -1) {
				pvt->status_sched_id = ast_sched_add(pvt->sched, STATUS_POLL_INTERVAL, mbl_status_poll, pvt);
				if (pvt->status_sched_id != -1) {
					ast_debug(1, "[%s] Status polling scheduled\n", pvt->id);
				}
			}
			break;

		case AT_A:
			ast_debug(1, "[%s] answer sent successfully\n", pvt->id);
			pvt->needchup = 1;

			/*
			 * SCO connection decision based on HFP version:
			 * - HFP 1.6+: Per spec, Audio Gateway (phone) initiates SCO
			 * - HFP ≤1.5: Legacy phones may expect host to initiate
			 *
			 * Only attempt host-initiated SCO for older phones.
			 */
			if (pvt->incoming && pvt->sco_socket == -1) {
				if (pvt->hfp->hfp_version >= 16) {
					ast_debug(1, "[%s] HFP %d.%d - waiting for phone to initiate SCO (per spec)\n",
						pvt->id, pvt->hfp->hfp_version / 10, pvt->hfp->hfp_version % 10);
					/* Don't try host SCO - phone should initiate */
				} else {
					ast_debug(1, "[%s] HFP %d.%d - trying host-initiated CVSD SCO (legacy)\n",
						pvt->id, pvt->hfp->hfp_version / 10, pvt->hfp->hfp_version % 10);
					if ((pvt->sco_socket = sco_connect(pvt->adapter->addr, pvt->addr, &pvt->sco_mtu)) == -1) {
						ast_log(LOG_WARNING, "[%s] host SCO failed - waiting for phone to initiate\n", pvt->id);
						/* Don't fail - phone may still initiate SCO */
					} else {
						ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);
						if (pvt->owner) {
							ast_channel_set_fd(pvt->owner, 0, pvt->sco_socket);
						}
					}
				}
			}
			break;
		case AT_D:
			ast_debug(1, "[%s] dial sent successfully\n", pvt->id);
			pvt->needchup = 1;
			pvt->outgoing = 1;
			mbl_queue_control(pvt, AST_CONTROL_PROGRESS);
			break;
		case AT_CHUP:
			ast_debug(1, "[%s] successful hangup\n", pvt->id);
			break;
		case AT_CMGS:
			ast_verb(3, "[%s] SMS: sent successfully\n", pvt->id);
			pvt->outgoing_sms = 0;
			pvt->sms_send_in_progress = 0;
			break;
		case AT_VTS:
			ast_debug(1, "[%s] digit sent successfully\n", pvt->id);
			break;
		case AT_CUSD:
			ast_debug(1, "[%s] CUSD code sent successfully\n", pvt->id);
			break;
		case AT_CPMS:
			/* CPMS OK received - +CPMS: response was already parsed by handle_response_cpms */
			/* Check if we're reading a specific message (from CMTI notification) */
			if (pvt->sms_index_to_read > 0) {
				/* Targeted read: read the specific index from CMTI notification */
				ast_verb(3, "[%s] Storage '%s' selected, now reading SMS at index %d\n", 
					pvt->id, pvt->sms_storage_pending, pvt->sms_index_to_read);
				if (hfp_send_cmgr(pvt->hfp, pvt->sms_index_to_read) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
					ast_debug(1, "[%s] error sending CMGR to retrieve SMS message\n", pvt->id);
				}
			} else {
				/* Full scan: scan for unread messages */
				ast_debug(1, "[%s] Scanning \"%s\" for unread messages...\n", pvt->id, pvt->sms_storage_pending);
				if (hfp_send_cmgl(pvt->hfp, "REC UNREAD") || msg_queue_push(pvt, AT_OK, AT_CMGL)) {
					ast_debug(1, "[%s] error sending CMGL\n", pvt->id);
				}
			}
			/* Note: we don't clear sms_storage_pending yet, we need it for context */
			break;
		case AT_CMGL:
			/* CMGL list complete - now process collected indices */
			ast_debug(1, "[%s] CMGL scan complete on storage \"%s\", found %d messages\n", 
				pvt->id, pvt->sms_storage_pending, pvt->sms_pending_count);
			
			/* Start reading the first pending message */
			if (pvt->sms_pending_count > 0) {
				int idx = pvt->sms_pending_indices[0];
				/* Shift the queue */
				memmove(pvt->sms_pending_indices, pvt->sms_pending_indices + 1, 
					(pvt->sms_pending_count - 1) * sizeof(int));
				pvt->sms_pending_count--;
				
				pvt->sms_index_to_read = idx;
				ast_verb(3, "[%s] Reading SMS at index %d (%d remaining)\n", pvt->id, idx, pvt->sms_pending_count);
				if (hfp_send_cmgr(pvt->hfp, idx) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
					ast_debug(1, "[%s] error sending CMGR for index %d\n", pvt->id, idx);
				}
			} else if (!strcmp(pvt->sms_storage_pending, "ME")) {
				/* No messages in ME, try SM next */
				ast_verb(3, "[%s] Finished scanning ME, now scanning SM\n", pvt->id);
				ast_copy_string(pvt->sms_storage_pending, "SM", sizeof(pvt->sms_storage_pending));
				if (hfp_send_cpms(pvt->hfp, "SM") || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					ast_debug(1, "[%s] error sending CPMS fallback to SM\n", pvt->id);
					pvt->sms_storage_pending[0] = '\0';
				}
			} else {
				/* Scan complete */
				pvt->sms_storage_pending[0] = '\0';
			}
			break;
		case AT_CMGD:
			ast_debug(1, "[%s] SMS deleted successfully\n", pvt->id);
			break;

		case AT_CMGR:
			/* CMGR response handled */
			/* Check if we need to delete the message */
			if (pvt->sms_delete_after_read && pvt->sms_index_to_read > 0) {
				ast_verb(3, "[%s] Deleting read SMS at index %d\n", pvt->id, pvt->sms_index_to_read);
				if (hfp_send_cmgd(pvt->hfp, pvt->sms_index_to_read) || msg_queue_push(pvt, AT_OK, AT_CMGD)) {
					ast_debug(1, "[%s] error sending CMGD to delete SMS\n", pvt->id);
				}
			}
			pvt->sms_index_to_read = 0;
			
			/* Continue reading remaining pending messages */
			if (pvt->sms_pending_count > 0) {
				int idx = pvt->sms_pending_indices[0];
				memmove(pvt->sms_pending_indices, pvt->sms_pending_indices + 1, 
					(pvt->sms_pending_count - 1) * sizeof(int));
				pvt->sms_pending_count--;
				
				pvt->sms_index_to_read = idx;
				ast_verb(3, "[%s] Reading next SMS at index %d (%d remaining)\n", pvt->id, idx, pvt->sms_pending_count);
				if (hfp_send_cmgr(pvt->hfp, idx) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
					ast_debug(1, "[%s] error sending CMGR for index %d\n", pvt->id, idx);
				}
			} else if (!ast_strlen_zero(pvt->sms_storage_pending)) {
				/* All messages from current storage read, try next storage */
				if (!strcmp(pvt->sms_storage_pending, "ME")) {
					ast_verb(3, "[%s] Finished reading from ME, now scanning SM\n", pvt->id);
					ast_copy_string(pvt->sms_storage_pending, "SM", sizeof(pvt->sms_storage_pending));
					if (hfp_send_cpms(pvt->hfp, "SM") || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
						ast_debug(1, "[%s] error sending CPMS for SM\n", pvt->id);
						pvt->sms_storage_pending[0] = '\0';
					}
				} else {
					/* All done */
					pvt->sms_storage_pending[0] = '\0';
				}
			}
			break;
		case AT_UNKNOWN:
		default:
			ast_debug(1, "[%s] received OK for unhandled request: %s\n", pvt->id, at_msg2str(entry->response_to));
			break;
		}
		msg_queue_free_and_pop(pvt);
	} else if (entry) {
		ast_debug(1, "[%s] received AT message 'OK' when expecting %s, ignoring\n", pvt->id, at_msg2str(entry->expected));
	} else {
		ast_debug(1, "[%s] received unexpected AT message 'OK'\n", pvt->id);
	}
	return 0;

/* Labels for device status query chain - allows graceful fallback */
chain_creg:
	msg_queue_free_and_pop(pvt);
	if (!pvt->hfp->no_creg) {
		/* Enable CREG unsolicited notifications and query */
		if (hfp_send_creg(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CREG_SET)) {
			ast_debug(1, "[%s] error enabling CREG\n", pvt->id);
			pvt->hfp->no_creg = 1;
			goto chain_cgreg_start;
		}
		return 0;
	}
chain_cgreg_start:
chain_cgreg:
	msg_queue_free_and_pop(pvt);
	if (!pvt->hfp->no_cgreg) {
		/* Enable CGREG unsolicited notifications and query */
		if (hfp_send_cgreg(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CGREG_SET)) {
			ast_debug(1, "[%s] error enabling CGREG\n", pvt->id);
			pvt->hfp->no_cgreg = 1;
			goto chain_cbc_start;
		}
		return 0;
	}
chain_cbc_start:
chain_cbc:
	msg_queue_free_and_pop(pvt);
	if (!pvt->hfp->no_cbc) {
		/* Query battery status */
		if (hfp_send_cbc(pvt->hfp) || msg_queue_push(pvt, AT_CBC, AT_CBC)) {
			ast_debug(1, "[%s] error querying CBC\n", pvt->id);
			pvt->hfp->no_cbc = 1;
		} else {
			return 0;
		}
	}
	/* All device status queries done or skipped */
	return 0;

e_return:
	msg_queue_free_and_pop(pvt);
	return -1;
}

/*!
 * \brief Handle ERROR AT messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_error(struct mbl_pvt *pvt, char *buf)
{
	struct msg_queue_entry *entry;
	if ((entry = msg_queue_head(pvt))
			&& (entry->expected == AT_OK
			|| entry->expected == AT_ERROR
			|| entry->expected == AT_BRSF
			|| entry->expected == AT_CMS_ERROR
			|| entry->expected == AT_CMGR
			|| entry->expected == AT_CBC
			|| entry->expected == AT_SMS_PROMPT)) {
		switch (entry->response_to) {

		/* initialization stuff */
		case AT_BRSF:
			/* BT 1.x devices may not support AT+BRSF - treat as HFP 1.0 and continue */
			if (pvt->bt_ver <= 1) {
				ast_verb(3, "[%s] BT 1.x device doesn't support BRSF - assuming HFP 1.0\n", pvt->id);
				pvt->hfp->hfp_version = 10;
				pvt->hfp->brsf_raw = 0;
				
				/* Query for CSCS support before proceeding. 
				 * Even for 1.x devices, we try invalid/query to see how it reacts or force invalid/set 
				 */
				if (hfp_send_cscs(pvt->hfp, NULL) || msg_queue_push(pvt, AT_CSCS, AT_CSCS)) {
					ast_debug(1, "[%s] error sending CSCS query\n", pvt->id);
					goto e_return;
				}
				break;
			}
			/* Fallback to non-BRSF init? or just ignore error */
			break;

		case AT_CPMS:
			/* If CPMS set failed, try fallback */
			ast_debug(1, "[%s] AT+CPMS=\"%s\" failed\n", pvt->id, pvt->sms_storage_pending);
			
			if (!strcmp(pvt->sms_storage_pending, "MT")) {
				ast_verb(3, "[%s] AT+CPMS=\"MT\" failed, trying fallback to \"ME\"\n", pvt->id);
				ast_copy_string(pvt->sms_storage_pending, "ME", sizeof(pvt->sms_storage_pending));
				if (hfp_send_cpms(pvt->hfp, "ME") || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					/* If send fails, trigger next fallback immediately */
					goto cpms_fallback_me_failed;
				}
				msg_queue_free_and_pop(pvt);
				return 0;
			}
			
cpms_fallback_me_failed:
			if (!strcmp(pvt->sms_storage_pending, "ME")) {
				ast_verb(3, "[%s] AT+CPMS=\"ME\" failed, trying fallback to \"SM\"\n", pvt->id);
				ast_copy_string(pvt->sms_storage_pending, "SM", sizeof(pvt->sms_storage_pending));
				if (hfp_send_cpms(pvt->hfp, "SM") || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					goto cpms_fallback_final;
				}
				msg_queue_free_and_pop(pvt);
				return 0;
			}

cpms_fallback_final:
			/* All CPMS attempts failed, try to read the message anyway */
			ast_debug(1, "[%s] All AT+CPMS attempts failed, trying to read SMS anyway\n", pvt->id);
			if (pvt->sms_index_to_read > 0) {
				if (hfp_send_cmgr(pvt->hfp, pvt->sms_index_to_read)
						|| msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
					ast_debug(1, "[%s] error sending CMGR to retrieve SMS message\n", pvt->id);
					pvt->sms_index_to_read = 0;
				} else {
					pvt->incoming_sms = 1; /* Mark as incoming SMS operation */
				}
			}
			pvt->sms_storage_pending[0] = '\0';
			msg_queue_free_and_pop(pvt);
			return 0;

		case AT_CIND_TEST:
			ast_debug(1, "[%s] error during CIND test\n", pvt->id);
			goto e_return;
		case AT_CIND:
			ast_debug(1, "[%s] error requesting CIND state\n", pvt->id);
			goto e_return;
		case AT_CMER:
			ast_debug(1, "[%s] error during CMER request\n", pvt->id);
			goto e_return;
		case AT_CLIP:
			ast_debug(1, "[%s] error enabling calling line indication\n", pvt->id);
			goto e_return;
		case AT_CSCS:
			/* Error asking for supported charsets. Fallback to trying UCS2 directly. */
			ast_verb(3, "[%s] CSCS query failed - trying UCS2 default\n", pvt->id);
			if (hfp_send_cscs(pvt->hfp, "UCS2") || msg_queue_push(pvt, AT_OK, AT_CSCS_SET)) {
				ast_debug(1, "[%s] error sending CSCS set\n", pvt->id);
				goto e_return;
			}
			/* Set active to UCS2 so we know what we tried */
			ast_copy_string(pvt->cscs_active, "UCS2", sizeof(pvt->cscs_active));
			break;
		case AT_CSCS_SET:
			/* Error setting charset */
			ast_debug(1, "[%s] error setting CSCS to %s\n", pvt->id, pvt->cscs_active);
			if (!strcasecmp(pvt->cscs_active, "UCS2")) {
				/* If UCS2 failed, try GSM */
				ast_verb(3, "[%s] CSCS=UCS2 failed - trying GSM\n", pvt->id);
				if (hfp_send_cscs(pvt->hfp, "GSM") || msg_queue_push(pvt, AT_OK, AT_CSCS_SET)) {
					ast_debug(1, "[%s] error sending CSCS set\n", pvt->id);
					goto e_return;
				}
				ast_copy_string(pvt->cscs_active, "GSM", sizeof(pvt->cscs_active));
			} else {
				/* If GSM (or anything else) failed, give up and proceed */
				ast_verb(3, "[%s] CSCS set failed - continuing with default/IRA\n", pvt->id);
				pvt->cscs_active[0] = '\0'; /* Clear active charset */
				
				/* Proceed to CIND */
				if (pvt->blackberry) {
					if (hfp_send_cmer(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMER)) {
						ast_debug(1, "[%s] error sending CMER\n", pvt->id);
						goto e_return;
					}
				} else {
					if (hfp_send_cind_test(pvt->hfp) || msg_queue_push(pvt, AT_CIND, AT_CIND_TEST)) {
						ast_debug(1, "[%s] error sending CIND test\n", pvt->id);
						goto e_return;
					}
				}
			}
			break;
		case AT_VGS:
			ast_debug(1, "[%s] volume level synchronization failed\n", pvt->id);

			/* this is not a fatal error, let's continue with initialization */

			/* Try to set SMS operating mode - text mode first */
			if (pvt->sms_mode != SMS_MODE_OFF) {
				if (hfp_send_cmgf(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMGF)) {
					ast_debug(1, "[%s] error setting CMGF\n", pvt->id);
					goto e_return;
				}
			}
			break;
		case AT_CMGF:
			/* Text mode failed, try PDU mode as fallback */
			ast_verb(3, "[%s] SMS: text mode failed, trying PDU mode (AT+CMGF=0)\n", pvt->id);
			if (hfp_send_cmgf(pvt->hfp, 0) || msg_queue_push(pvt, AT_OK, AT_CMGF_PDU)) {
				ast_debug(1, "[%s] error setting CMGF for PDU mode\n", pvt->id);
				pvt->sms_mode = SMS_MODE_NO;
				goto err_chain_creg;
			}
			break;
		case AT_CMGF_PDU:
			/* PDU mode also failed - SMS not supported */
			pvt->sms_mode = SMS_MODE_NO;
			ast_verb(3, "[%s] SMS: PDU mode also failed - SMS disabled\n", pvt->id);
			/* Continue with device status chain */
			if (!pvt->hfp->no_cops) {
				if (hfp_send_cops(pvt->hfp, 2, 0) || msg_queue_push(pvt, AT_OK, AT_COPS_SET_NUMERIC)) {
					pvt->hfp->no_cops = 1;
				} else {
					break;
				}
			}
			/* Fall through to start CREG if COPS disabled */
			goto err_chain_creg;
		
		case AT_COPS_SET_NUMERIC:
			/* Some devices (like Nokia 6310i) reject numeric set but support query.
			 * Instead of disabling COPS, try falling back to simple query. */
			ast_verb(3, "[%s] AT+COPS=3,2 failed - trying AT+COPS? query fallback\n", pvt->id);
			if (hfp_send_cops(pvt->hfp, 0, 1) || msg_queue_push(pvt, AT_COPS, AT_COPS_FALLBACK)) {
				/* If even query fails to send, then disable */
				pvt->hfp->no_cops = 1;
			} else {
				/* Successfully sent query command, break to wait for response */
				break; 
			}
			/* If query push failed, fall through to CREG */
			goto err_chain_creg;

		case AT_COPS_QUERY:
			/* Query failed - now we really have to disable it */
			ast_verb(3, "[%s] AT+COPS? query also failed - disabling COPS support\n", pvt->id);
			pvt->hfp->no_cops = 1;
			goto err_chain_creg;

		case AT_CNMI:
			/* Primary CNMI (2,1,0,0,0) failed, try first fallback (1,1,0,0,0) */
			ast_verb(3, "[%s] SMS: CNMI mode 2,1 failed, trying 1,1\n", pvt->id);
			if (hfp_send_cnmi(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CNMI_FALLBACK1)) {
				ast_debug(1, "[%s] error setting CNMI fallback1\n", pvt->id);
				pvt->sms_mode = SMS_MODE_NO;
				goto err_chain_creg;
			}
			break;
		case AT_CNMI_FALLBACK1:
			/* First fallback (1,1,0,0,0) failed, try second fallback (1,2,0,0,0) */
			ast_verb(3, "[%s] SMS: CNMI mode 1,1 failed, trying 1,2\n", pvt->id);
			if (hfp_send_cnmi(pvt->hfp, 2) || msg_queue_push(pvt, AT_OK, AT_CNMI_FALLBACK2)) {
				ast_debug(1, "[%s] error setting CNMI fallback2\n", pvt->id);
				pvt->sms_mode = SMS_MODE_NO;
				goto err_chain_creg;
			}
			break;
		case AT_CNMI_FALLBACK2:
			/* Second fallback (1,2,0,0,0) failed, try third fallback with mode 3 (3,1,0,0,0) */
			ast_verb(3, "[%s] SMS: CNMI mode 1,2 failed, trying 3,1 (link-active mode)\n", pvt->id);
			if (hfp_send_cnmi(pvt->hfp, 3) || msg_queue_push(pvt, AT_OK, AT_CNMI_FALLBACK3)) {
				ast_debug(1, "[%s] error setting CNMI fallback3\n", pvt->id);
				pvt->sms_mode = SMS_MODE_NO;
				goto err_chain_creg;
			}
			break;
		case AT_CNMI_FALLBACK3:
			/* All CNMI modes failed - query what values are supported for debugging */
			ast_verb(3, "[%s] SMS: all CNMI modes failed, querying supported values\n", pvt->id);
			if (hfp_send_cnmi_test(pvt->hfp) || msg_queue_push(pvt, AT_OK, AT_CNMI_TEST)) {
				/* Query failed, continue without it */
				ast_verb(3, "[%s] SMS: CNMI test query failed - receiving disabled, sending enabled (%s mode)\n",
					pvt->id, sms_mode_to_str(pvt->sms_mode));
				goto cnmi_done;
			}
			break;
		case AT_CNMI_TEST:
			/* CNMI test query failed - this is fine, we tried */
			ast_verb(3, "[%s] SMS: CNMI=? not supported - receiving disabled, sending enabled (%s mode)\n",
				pvt->id, sms_mode_to_str(pvt->sms_mode));
			/* Fall through */
cnmi_done:
			/* Continue with device status chain */
			if (!pvt->hfp->no_cops) {
				if (hfp_send_cops(pvt->hfp, 2, 0) || msg_queue_push(pvt, AT_OK, AT_COPS_SET_NUMERIC)) {
					pvt->hfp->no_cops = 1;
				} else {
					break;
				}
			}
			/* Fall through to start CREG if COPS disabled */
			goto err_chain_creg;
		case AT_ECAM:
			ast_debug(1, "[%s] Mobile does not support Sony Ericsson extensions\n", pvt->id);

			/* this is not a fatal error, let's continue with the initialization */

			if (hfp_send_vgs(pvt->hfp, 15) || msg_queue_push(pvt, AT_OK, AT_VGS)) {
				ast_debug(1, "[%s] error synchronizing gain settings\n", pvt->id);
				goto e_return;
			}

			pvt->timeout = -1;
			pvt->hfp->initialized = 1;
			mbl_set_state(pvt, MBL_STATE_READY);
			ast_verb(3, "Bluetooth Device %s initialized and ready.\n", pvt->id);

			/* Process any SMS that arrived during initialization */
			process_pending_sms(pvt);

			break;
		/* end initialization stuff */

		/* Device status command errors - non-fatal, continue chain */
		case AT_COPS_SET_ALPHA:
		case AT_COPS_DONE:
		case AT_COPS:
			ast_verb(3, "[%s] AT+COPS not supported, disabling\n", pvt->id);
			pvt->hfp->no_cops = 1;
			/* Continue to CREG */
			if (!pvt->hfp->no_creg) {
				if (hfp_send_creg(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CREG_SET)) {
					pvt->hfp->no_creg = 1;
				} else {
					break;
				}
			}
			/* Fall through to CGREG if CREG disabled */
		case AT_CREG_SET:
		case AT_CREG:
			if (entry->response_to == AT_CREG_SET || entry->response_to == AT_CREG) {
				ast_verb(3, "[%s] AT+CREG not supported, disabling\n", pvt->id);
				pvt->hfp->no_creg = 1;
			}
			/* Continue to CGREG */
			if (!pvt->hfp->no_cgreg) {
				if (hfp_send_cgreg(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CGREG_SET)) {
					pvt->hfp->no_cgreg = 1;
				} else {
					break;
				}
			}
			/* Fall through to CBC if CGREG disabled */
		case AT_CGREG_SET:
		case AT_CGREG:
			if (entry->response_to == AT_CGREG_SET || entry->response_to == AT_CGREG) {
				ast_verb(3, "[%s] AT+CGREG not supported, disabling\n", pvt->id);
				pvt->hfp->no_cgreg = 1;
			}
			/* Continue to CBC */
			if (!pvt->hfp->no_cbc) {
				if (hfp_send_cbc(pvt->hfp) || msg_queue_push(pvt, AT_CBC, AT_CBC)) {
					pvt->hfp->no_cbc = 1;
				} else {
					break;
				}
			}
			/* All device status done */
			break;
		case AT_CBC:
			ast_verb(3, "[%s] AT+CBC not supported, disabling\n", pvt->id);
			pvt->hfp->no_cbc = 1;
			/* Device status chain complete */
			break;

		case AT_A:
			ast_debug(1, "[%s] answer failed\n", pvt->id);
			mbl_queue_hangup(pvt);
			break;
		case AT_D:
			ast_debug(1, "[%s] dial failed\n", pvt->id);
			pvt->needchup = 0;
			mbl_queue_control(pvt, AST_CONTROL_CONGESTION);
			break;
		case AT_CHUP:
			ast_debug(1, "[%s] error sending hangup, disconnecting\n", pvt->id);
			goto e_return;
		case AT_CMGR:
			ast_debug(1, "[%s] error reading sms message (index %d, mem %s)\n", pvt->id, pvt->sms_index_to_read, pvt->sms_storage_pending);
			
			/* Read-First Strategy Failed:
			 * If sms_storage_pending is empty, it means we tried to read directly from notification index and it failed.
			 * In this case, start the Full Storage Scan on "ME" then "SM".
			 * (We skip "MT" for scan because it's usually a combination/alias and CMGL might duplicate or behave oddly)
			 */
			if (ast_strlen_zero(pvt->sms_storage_pending)) {
				ast_verb(3, "[%s] Direct SMS read failed (index %d), starting Full Storage Scan on \"ME\"\n", pvt->id, pvt->sms_index_to_read);
				ast_copy_string(pvt->sms_storage_pending, "ME", sizeof(pvt->sms_storage_pending));
				if (hfp_send_cpms(pvt->hfp, "ME") || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					/* If ME fails, try SM immediately */
					goto scan_fallback_sm;
				}
			}
			/* If we were scanning ME and CPMS failed (unlikely here, handled in CPMS error usually, but safety check) */
			else if (!strcmp(pvt->sms_storage_pending, "ME")) {
scan_fallback_sm:
				ast_verb(3, "[%s] Storage scan on ME failed, trying SM\n", pvt->id);
				ast_copy_string(pvt->sms_storage_pending, "SM", sizeof(pvt->sms_storage_pending));
				if (hfp_send_cpms(pvt->hfp, "SM") || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					ast_debug(1, "[%s] error sending CPMS fallback to SM\n", pvt->id);
					pvt->sms_storage_pending[0] = '\0';
				}
			}

			pvt->incoming_sms = 0;
			pvt->sms_index_to_read = 0;
			/* Don't clear sms_storage_pending if we just set it! Only if we are done. */
			if (ast_strlen_zero(pvt->sms_storage_pending)) {
				/* All done or failed */
			}
			break;
		case AT_CMGS:
			ast_debug(1, "[%s] error sending sms message\n", pvt->id);
			pvt->outgoing_sms = 0;
			pvt->sms_send_in_progress = 0;
			break;
		case AT_VTS:
			ast_debug(1, "[%s] error sending digit\n", pvt->id);
			break;
		case AT_CUSD:
			ast_verb(0, "[%s] error sending CUSD command\n", pvt->id);
			break;
		case AT_UNKNOWN:
		default:
			ast_debug(1, "[%s] received ERROR for unhandled request: %s\n", pvt->id, at_msg2str(entry->response_to));
			break;
		}
		msg_queue_free_and_pop(pvt);
	} else if (entry) {
		ast_debug(1, "[%s] received AT message 'ERROR' when expecting %s, ignoring\n", pvt->id, at_msg2str(entry->expected));
	} else {
		ast_debug(1, "[%s] received unexpected AT message 'ERROR'\n", pvt->id);
	}

	return 0;

/* Label for starting device status chain from error handlers */
err_chain_creg:
	msg_queue_free_and_pop(pvt);
	if (!pvt->hfp->no_creg) {
		if (hfp_send_creg(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CREG_SET)) {
			pvt->hfp->no_creg = 1;
		} else {
			return 0;
		}
	}
	if (!pvt->hfp->no_cgreg) {
		if (hfp_send_cgreg(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CGREG_SET)) {
			pvt->hfp->no_cgreg = 1;
		} else {
			return 0;
		}
	}
	if (!pvt->hfp->no_cbc) {
		if (hfp_send_cbc(pvt->hfp) || msg_queue_push(pvt, AT_CBC, AT_CBC)) {
			pvt->hfp->no_cbc = 1;
		} else {
			return 0;
		}
	}
	return 0;

e_return:
	msg_queue_free_and_pop(pvt);
	return -1;
}

/*!
 * \brief Handle AT+CIEV messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_ciev(struct mbl_pvt *pvt, char *buf)
{
	int i;
	switch (hfp_parse_ciev(pvt->hfp, buf, &i)) {
	case HFP_CIND_CALL:
		switch (i) {
		case HFP_CIND_CALL_NONE:
			ast_debug(1, "[%s] line disconnected\n", pvt->id);
			if (pvt->owner) {
				ast_debug(1, "[%s] hanging up owner\n", pvt->id);
				if (mbl_queue_hangup(pvt)) {
					ast_log(LOG_ERROR, "[%s] error queueing hangup, disconnecting...\n", pvt->id);
					return -1;
				}
			}
			pvt->needchup = 0;
			pvt->needcallerid = 0;
			pvt->incoming = 0;
			pvt->outgoing = 0;
			break;
		case HFP_CIND_CALL_ACTIVE:
			if (pvt->outgoing) {
				ast_debug(1, "[%s] remote end answered\n", pvt->id);

				if (pvt->sco_socket == -1) {
					if ((pvt->sco_socket = sco_connect(pvt->adapter->addr, pvt->addr, &pvt->sco_mtu)) == -1) {
						ast_log(LOG_ERROR, "[%s] unable to create audio connection\n", pvt->id);
					} else {
						ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);
						if (pvt->owner) {
							ast_channel_set_fd(pvt->owner, 0, pvt->sco_socket);
						}
					}
				}

				hfp_send_vgs(pvt->hfp, 13);
				hfp_send_vgm(pvt->hfp, 13);

				mbl_queue_control(pvt, AST_CONTROL_ANSWER);
			} else if (pvt->incoming && pvt->answered) {
				ast_setstate(pvt->owner, AST_STATE_UP);
			} else if (pvt->incoming) {
				/* user answered from handset, disconnecting */
				ast_verb(3, "[%s] user answered bluetooth device from handset, disconnecting\n", pvt->id);
				mbl_queue_hangup(pvt);
				return -1;
			}
			break;
		}
		break;

	case HFP_CIND_CALLSETUP:
		switch (i) {
		case HFP_CIND_CALLSETUP_NONE:
			if (pvt->hfp->cind_state[pvt->hfp->cind_map.call] != HFP_CIND_CALL_ACTIVE) {
				if (pvt->owner) {
					if (pvt->hfp->sent_alerting == 1) {
						handle_response_busy(pvt);
					}
					if (mbl_queue_hangup(pvt)) {
						ast_log(LOG_ERROR, "[%s] error queueing hangup, disconnecting...\n", pvt->id);
						return -1;
					}
				}
				pvt->needchup = 0;
				pvt->needcallerid = 0;
				pvt->incoming = 0;
				pvt->outgoing = 0;
			}
			break;
		case HFP_CIND_CALLSETUP_INCOMING:
			ast_debug(1, "[%s] incoming call, waiting for caller id\n", pvt->id);
			pvt->needcallerid = 1;
			pvt->incoming = 1;
			break;
		case HFP_CIND_CALLSETUP_OUTGOING:
			if (pvt->outgoing) {
				pvt->hfp->sent_alerting = 0;
				ast_debug(1, "[%s] outgoing call\n", pvt->id);
			} else {
				ast_verb(3, "[%s] user dialed from handset, disconnecting\n", pvt->id);
				return -1;
			}
			break;
		case HFP_CIND_CALLSETUP_ALERTING:
			if (pvt->outgoing) {
				ast_debug(1, "[%s] remote alerting\n", pvt->id);
				mbl_queue_control(pvt, AST_CONTROL_RINGING);
				pvt->hfp->sent_alerting = 1;
			}
			break;
		}
		break;
	case HFP_CIND_NONE:
		ast_debug(1, "[%s] error parsing CIND: %s\n", pvt->id, buf);
		break;
	}
	return 0;
}

/*!
 * \brief Handle AT+CLIP messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_clip(struct mbl_pvt *pvt, char *buf)
{
	struct msg_queue_entry *msg;
	struct ast_channel *chan;
	struct cidinfo cidinfo;
	char decoded_name[256];

	if ((msg = msg_queue_head(pvt)) && msg->expected == AT_CLIP) {
		msg_queue_free_and_pop(pvt);

		pvt->needcallerid = 0;
		cidinfo = hfp_parse_clip(pvt->hfp, buf);

		/* Decode caller name if in UCS2 mode */
		if (!strcmp(pvt->cscs_active, "UCS2") && cidinfo.cnam[0]) {
			ucs2_hex_to_utf8(cidinfo.cnam, decoded_name, sizeof(decoded_name));
			ast_copy_string(cidinfo.cnam, decoded_name, sizeof(cidinfo.cnam));
			ast_debug(2, "[%s] CLIP: decoded caller name from UCS2: %s\n", pvt->id, cidinfo.cnam);
		}

		if (!(chan = mbl_new(AST_STATE_RING, pvt, &cidinfo, NULL, NULL))) {
			ast_log(LOG_ERROR, "[%s] unable to allocate channel for incoming call\n", pvt->id);
			hfp_send_chup(pvt->hfp);
			msg_queue_push(pvt, AT_OK, AT_CHUP);
			return -1;
		}

		/* from this point on, we need to send a chup in the event of a
		 * hangup */
		pvt->needchup = 1;

		if (ast_pbx_start(chan)) {
			ast_log(LOG_ERROR, "[%s] unable to start pbx on incoming call\n", pvt->id);
			mbl_ast_hangup(pvt);
			return -1;
		}
	}

	return 0;
}

/*!
 * \brief Handle RING messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_ring(struct mbl_pvt *pvt, char *buf)
{
	if (pvt->needcallerid) {
		ast_debug(1, "[%s] got ring while waiting for caller id\n", pvt->id);
		return msg_queue_push(pvt, AT_CLIP, AT_UNKNOWN);
	} else {
		return 0;
	}
}

/*!
 * \brief Scheduler callback for delayed CMTI read.
 * \param data pointer to mbl_pvt structure
 * \return 0 (do not reschedule)
 * 
 * This function fires SMS_CMTI_DELAY_MS after the last CMTI notification,
 * allowing multi-part SMS messages to fully arrive before reading.
 */
static int mbl_cmti_delayed_read(const void *data)
{
	struct mbl_pvt *pvt = (struct mbl_pvt *) data;
	
	ast_debug(1, "[%s] SMS: mbl_cmti_delayed_read callback fired!\n", pvt->id);
	
	ast_mutex_lock(&pvt->lock);
	
	/* Clear scheduler ID since we're running now */
	pvt->sms_cmti_sched_id = -1;
	
	if (!pvt->connected || !pvt->hfp || !pvt->hfp->initialized) {
		ast_debug(1, "[%s] SMS: delayed read cancelled - device not ready\n", pvt->id);
		ast_mutex_unlock(&pvt->lock);
		return 0;
	}
	
	if (pvt->sms_pending_count <= 0) {
		ast_debug(1, "[%s] SMS: delayed read - no pending messages\n", pvt->id);
		ast_mutex_unlock(&pvt->lock);
		return 0;
	}
	
	ast_verb(3, "[%s] SMS: delayed read triggered - processing %d queued notifications\n", 
		pvt->id, pvt->sms_pending_count);
	
	/* Start reading the first pending message */
	int idx = pvt->sms_pending_indices[0];
	pvt->sms_index_to_read = idx;
	
	/* Select storage if we have one stored, otherwise read directly */
	if (!ast_strlen_zero(pvt->sms_storage_pending)) {
		ast_verb(3, "[%s] SMS: selecting storage '%s' for delayed read of index %d\n", 
			pvt->id, pvt->sms_storage_pending, idx);
		if (hfp_send_cpms(pvt->hfp, pvt->sms_storage_pending) || 
			msg_queue_push(pvt, AT_OK, AT_CPMS)) {
			ast_debug(1, "[%s] error sending CPMS for delayed SMS read\n", pvt->id);
		}
	} else {
		ast_verb(3, "[%s] SMS: reading queued index %d directly\n", pvt->id, idx);
		if (hfp_send_cmgr(pvt->hfp, idx) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
			ast_debug(1, "[%s] error sending CMGR for delayed SMS read\n", pvt->id);
		}
	}
	
	/* Remove the first index from queue */
	memmove(pvt->sms_pending_indices, pvt->sms_pending_indices + 1, 
		(pvt->sms_pending_count - 1) * sizeof(int));
	pvt->sms_pending_count--;
	
	pvt->incoming_sms = 1;
	
	ast_mutex_unlock(&pvt->lock);
	return 0;  /* Don't reschedule */
}

/*!
 * \brief Process SMS notifications that were queued during initialization.
 * \param pvt a mbl_pvt structure
 * 
 * Called after device initialization completes to read any SMS messages
 * that arrived (via +CMTI) while init was in progress.
 */
static void process_pending_sms(struct mbl_pvt *pvt)
{
	if (pvt->sms_pending_count > 0) {
		ast_verb(3, "[%s] SMS: processing %d pending notifications from init\n", 
			pvt->id, pvt->sms_pending_count);
		
		/* Start reading the first pending message */
		int idx = pvt->sms_pending_indices[0];
		
		/* Shift the queue */
		memmove(pvt->sms_pending_indices, pvt->sms_pending_indices + 1, 
			(pvt->sms_pending_count - 1) * sizeof(int));
		pvt->sms_pending_count--;
		
		pvt->sms_index_to_read = idx;
		
		/* Select storage if we have one stored */
		if (!ast_strlen_zero(pvt->sms_storage_pending)) {
			ast_verb(3, "[%s] SMS: selecting storage '%s' for deferred index %d\n", 
				pvt->id, pvt->sms_storage_pending, idx);
			if (hfp_send_cpms(pvt->hfp, pvt->sms_storage_pending) || 
				msg_queue_push(pvt, AT_OK, AT_CPMS)) {
				ast_debug(1, "[%s] error sending CPMS for deferred SMS\n", pvt->id);
				pvt->sms_storage_pending[0] = '\0';
			}
		} else {
			/* Direct read */
			ast_verb(3, "[%s] SMS: reading deferred index %d\n", pvt->id, idx);
			if (hfp_send_cmgr(pvt->hfp, idx) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
				ast_debug(1, "[%s] error sending CMGR for deferred SMS\n", pvt->id);
			}
		}
	}
}

/*!
 * \brief Handle AT+CMTI messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cmti(struct mbl_pvt *pvt, char *buf)
{
	char mem[16] = "";
	int index = hfp_parse_cmti_full(pvt->hfp, buf, mem);
	if (index > 0) {
		ast_verb(3, "[%s] SMS: new message notification (index %d, mem '%s')\n", pvt->id, index, mem);

		/* Check if device is still initializing - defer SMS processing to avoid queue conflicts */
		if (!pvt->hfp->initialized) {
			ast_verb(3, "[%s] SMS: device still initializing, queueing SMS index %d for later\n", pvt->id, index);
			/* Store in pending queue for processing after init */
			if (pvt->sms_pending_count < 32) {
				pvt->sms_pending_indices[pvt->sms_pending_count++] = index;
				ast_copy_string(pvt->sms_storage_pending, mem, sizeof(pvt->sms_storage_pending));
			} else {
				ast_log(LOG_WARNING, "[%s] SMS: pending queue full, dropping notification for index %d\n", pvt->id, index);
			}
			return 0;  /* Don't start SMS read commands during init */
		}

		/* Queue the notification - don't read immediately.
		 * This allows multi-part SMS messages to fully arrive before we start reading.
		 */
		if (pvt->sms_pending_count < 32) {
			pvt->sms_pending_indices[pvt->sms_pending_count++] = index;
			ast_verb(3, "[%s] SMS: queued index %d (%d total pending)\n", pvt->id, index, pvt->sms_pending_count);
		} else {
			ast_log(LOG_WARNING, "[%s] SMS: pending queue full, dropping notification for index %d\n", pvt->id, index);
			return 0;
		}
		
		/* Store storage info if provided */
		if (!ast_strlen_zero(mem)) {
			ast_copy_string(pvt->sms_storage_pending, mem, sizeof(pvt->sms_storage_pending));
		}
		
		/* Cancel existing timer and schedule a new one.
		 * This resets the delay on each new CMTI so multi-part SMS parts all arrive.
		 */
		if (pvt->sms_cmti_sched_id > -1) {
			ast_verb(4, "[%s] SMS: resetting read timer (new CMTI arrived)\n", pvt->id);
			AST_SCHED_DEL(pvt->sched, pvt->sms_cmti_sched_id);
		}
		
		pvt->sms_cmti_sched_id = ast_sched_add(pvt->sched, SMS_CMTI_DELAY_MS, mbl_cmti_delayed_read, pvt);
		ast_debug(1, "[%s] SMS: ast_sched_add returned id=%d (sched=%p)\n", 
			pvt->id, pvt->sms_cmti_sched_id, pvt->sched);
		if (pvt->sms_cmti_sched_id < 0) {
			ast_log(LOG_WARNING, "[%s] SMS: failed to schedule delayed read\n", pvt->id);
			/* Fall back to immediate read */
			pvt->sms_index_to_read = index;
			if (!ast_strlen_zero(pvt->sms_storage_pending)) {
				if (hfp_send_cpms(pvt->hfp, pvt->sms_storage_pending) || msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					ast_debug(1, "[%s] error sending CPMS\n", pvt->id);
				}
			} else {
				if (hfp_send_cmgr(pvt->hfp, index) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
					ast_debug(1, "[%s] error sending CMGR\n", pvt->id);
				}
			}
			pvt->incoming_sms = 1;
		} else {
			ast_verb(3, "[%s] SMS: scheduled delayed read in %d ms\n", pvt->id, SMS_CMTI_DELAY_MS);
		}

		return 0;
	} else {
		ast_debug(1, "[%s] error parsing incoming sms message alert, disconnecting\n", pvt->id);
		return -1;
	}
}

/*!
 * \brief Handle AT+CMGR messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cmgr(struct mbl_pvt *pvt, char *buf)
{
	char *from_number = NULL, *from_name = NULL, *text = NULL;
	struct ast_msg *msg;
	struct msg_queue_entry *entry;
	char from_uri[128];
	char *decoded_text = NULL;
	char *decoded_name = NULL;
	char pdu_from[32];
	char pdu_message[512];
	int have_queue_entry = 0;

	/* Check if we have a matching queue entry */
	if ((entry = msg_queue_head(pvt)) && entry->expected == AT_CMGR) {
		msg_queue_free_and_pop(pvt);
		have_queue_entry = 1;
	} else if (pvt->sms_pending_count > 0 || pvt->incoming_sms) {
		/* No queue entry but we're expecting SMS - process anyway.
		 * This handles queue contention between SMS send and receive operations.
		 */
		ast_debug(1, "[%s] CMGR: processing without queue entry (pending=%d, incoming=%d)\n",
			pvt->id, pvt->sms_pending_count, pvt->incoming_sms);
		have_queue_entry = 1;  /* Treat as valid */
	}

	if (have_queue_entry) {
		if (pvt->sms_mode == SMS_MODE_PDU) {
			/* PDU mode: buf contains hex-encoded PDU after +CMGR: header */
			/* Format: +CMGR: <stat>,[<alpha>],<length>\r\n<pdu>\r\n */
			ast_debug(1, "[%s] CMGR PDU mode response: '%s'\n", pvt->id, buf);
			
			/* Find the PDU data line (after the +CMGR: response line) */
			char *pdu_start = strchr(buf, '\n');
			char pdu_buffer[512];
			
			if (pdu_start) {
				pdu_start++;
				/* Skip any additional whitespace/newlines */
				while (*pdu_start == '\r' || *pdu_start == '\n' || *pdu_start == ' ') {
					pdu_start++;
				}
				/* Remove trailing whitespace */
				char *end = pdu_start + strlen(pdu_start) - 1;
				while (end > pdu_start && (*end == '\r' || *end == '\n' || *end == ' ')) {
					*end-- = '\0';
				}
				
				ast_debug(1, "[%s] PDU data after header: '%s' (len=%zu)\n", 
					pvt->id, pdu_start, strlen(pdu_start));
				
				/* Check if PDU data is empty or just "OK" */
				if (strlen(pdu_start) < 10 || !strncmp(pdu_start, "OK", 2)) {
					ast_log(LOG_WARNING, "[%s] SMS: empty or invalid PDU data in CMGR response (length insufficient)\n", pvt->id);
					pvt->incoming_sms = 0;
					return 0;  /* Non-fatal - message might be deleted or empty */
				}
			} else {
				/* PDU body not in buffer - need to read it from socket.
				 * This happens when CMGR arrives without queue entry due to contention,
				 * and rfcomm_read_cmgr wasn't called to read the multi-line response.
				 */
				ast_debug(1, "[%s] CMGR: PDU body not in buffer, reading from socket\n", pvt->id);
				
				/* Read the next line which should be the PDU data */
				char temp_buf[512];
				
				/* Read until we get a line with content (skip empty lines) */
				int max_attempts = 5;
				pdu_buffer[0] = '\0';
				pdu_start = NULL;
				
				for (int attempt = 0; attempt < max_attempts && !pdu_start; attempt++) {
					ssize_t read_res = rfcomm_read(pvt->rfcomm_socket, temp_buf, sizeof(temp_buf) - 1);
					if (read_res <= 0) {
						ast_debug(1, "[%s] CMGR: failed to read PDU body (res=%zd)\n", pvt->id, read_res);
						break;
					}
					temp_buf[read_res] = '\0';
					
					/* Skip empty lines and OK */
					char *p = temp_buf;
					while (*p == '\r' || *p == '\n' || *p == ' ') {
						p++;
					}
					
					if (*p && strncmp(p, "OK", 2) != 0) {
						/* Found content - this should be the PDU */
						ast_copy_string(pdu_buffer, p, sizeof(pdu_buffer));
						/* Remove trailing whitespace */
						char *end = pdu_buffer + strlen(pdu_buffer) - 1;
						while (end > pdu_buffer && (*end == '\r' || *end == '\n' || *end == ' ')) {
							*end-- = '\0';
						}
						pdu_start = pdu_buffer;
						ast_debug(1, "[%s] CMGR: read PDU body from socket: '%s'\n", pvt->id, pdu_start);
					}
				}
				
				if (!pdu_start || strlen(pdu_start) < 10) {
					ast_debug(1, "[%s] CMGR: failed to get valid PDU body\n", pvt->id);
					pvt->incoming_sms = 0;
					return 0;  /* Non-fatal */
				}
			}
			
			/* Log received PDU header for debugging */
			log_pdu_deliver(pvt->id, pdu_start);
			
			/* Decode the PDU */
			if (sms_decode_pdu(pdu_start, pdu_from, sizeof(pdu_from), 
					pdu_message, sizeof(pdu_message)) == 0) {
				from_number = pdu_from;
				text = pdu_message;
				ast_verb(3, "[%s] SMS: received from %s (%zu chars, mode=PDU)\n",
					pvt->id, from_number, strlen(text));
			} else {
				ast_log(LOG_WARNING, "[%s] error decoding PDU SMS, PDU='%s'\n", pvt->id, pdu_start);
				pvt->incoming_sms = 0;
				return 0;  /* Non-fatal */
			}
		} else {
			/* Text mode: parse as before */
			if (hfp_parse_cmgr(pvt->hfp, buf, &from_number, &from_name, &text)) {
				ast_debug(1, "[%s] error parsing sms message, disconnecting\n", pvt->id);
				return -1;
			}

			/* Decode text based on active charset */
			if (!strcmp(pvt->cscs_active, "UCS2") && text) {
				/* UCS2: text is hex-encoded, strip SMS UDH if present, then decode to UTF-8 */
				const char *sms_text = sms_strip_udh_hex(text);
				size_t utf8_len = strlen(sms_text) / 2 + 1;
				decoded_text = ast_calloc(1, utf8_len);
				if (decoded_text) {
					ucs2_hex_to_utf8(sms_text, decoded_text, utf8_len);
					text = decoded_text;
					ast_verb(3, "[%s] SMS: received from %s (%zu chars, decoded from UCS2)\n", 
						pvt->id, from_number ? from_number : "unknown", strlen(text));
					ast_log(LOG_NOTICE, "[%s] SMS Decoded: '%s' (Original: '%s')\n", pvt->id, decoded_text, buf);
				}
			} else {
				ast_verb(3, "[%s] SMS: received from %s (%zu chars)\n", pvt->id, 
					from_number ? from_number : "unknown", text ? strlen(text) : 0);
			}

			/* Decode name based on active charset */
			if (!strcmp(pvt->cscs_active, "UCS2") && from_name) {
				size_t utf8_len = strlen(from_name) / 2 + 1;
				decoded_name = ast_calloc(1, utf8_len);
				if (decoded_name) {
					ucs2_hex_to_utf8(from_name, decoded_name, utf8_len);
					ast_log(LOG_NOTICE, "[%s] SMS Name Decoded: '%s' (Original: '%s')\n", pvt->id, decoded_name, from_name);
					from_name = decoded_name;
				}
			}
		}
		pvt->incoming_sms = 0;

		/* Create message using ast_msg API for SIP MESSAGE routing */
		msg = ast_msg_alloc();
		if (!msg) {
			ast_log(LOG_ERROR, "[%s] failed to allocate ast_msg for SMS\n", pvt->id);
			ast_free(decoded_text);
			return 0;  /* Non-fatal, don't disconnect */
		}

		/* Set message properties */
		snprintf(from_uri, sizeof(from_uri), "mobile:%s/%s", pvt->id, 
			from_number ? from_number : "unknown");
		ast_msg_set_from(msg, "%s", from_uri);
		ast_msg_set_to(msg, "%s", "sms:incoming");
		ast_msg_set_body(msg, "%s", text ? text : "");
		ast_msg_set_exten(msg, "sms");
		ast_msg_set_context(msg, "%s", pvt->context);
		ast_msg_set_tech(msg, "mobile");
		ast_msg_set_endpoint(msg, "%s", pvt->id);
		ast_msg_set_var(msg, "SMSSRC", from_number ? from_number : "");
		ast_msg_set_var(msg, "SMSNAME", from_name ? from_name : "");
		ast_msg_set_var(msg, "SMSTXT", text ? text : "");

		ast_log(LOG_NOTICE, "[%s] Setting SMS variables: SMSSRC='%s', SMSNAME='%s', SMSTXT='%s', Body='%s'\n",
			pvt->id, 
			from_number ? from_number : "",
			from_name ? from_name : "",
			text ? text : "",
			text ? text : "");

		ast_free(decoded_text);  /* Free if allocated */
		ast_free(decoded_name);  /* Free if allocated */

		/* Queue message for routing through dialplan */
		if (ast_msg_queue(msg)) {
			ast_log(LOG_WARNING, "[%s] failed to queue SMS message for routing\n", pvt->id);
			/* ast_msg_queue handles cleanup on failure */
		} else {
			ast_verb(3, "[%s] SMS: queued for dialplan routing\n", pvt->id);
		}
		
		/* Check if there are more pending SMS messages to read */
		if (pvt->sms_pending_count > 0) {
			ast_debug(1, "[%s] SMS: %d more pending, triggering next read\n", 
				pvt->id, pvt->sms_pending_count);
			
			/* Get next index from queue */
			int next_idx = pvt->sms_pending_indices[0];
			pvt->sms_index_to_read = next_idx;
			
			/* Shift the queue */
			memmove(pvt->sms_pending_indices, pvt->sms_pending_indices + 1, 
				(pvt->sms_pending_count - 1) * sizeof(int));
			pvt->sms_pending_count--;
			
			/* Read the next message */
			if (!ast_strlen_zero(pvt->sms_storage_pending)) {
				ast_verb(3, "[%s] SMS: reading next pending index %d from storage '%s'\n", 
					pvt->id, next_idx, pvt->sms_storage_pending);
				if (hfp_send_cpms(pvt->hfp, pvt->sms_storage_pending) || 
					msg_queue_push(pvt, AT_OK, AT_CPMS)) {
					ast_debug(1, "[%s] error sending CPMS for next SMS\n", pvt->id);
				}
			} else {
				ast_verb(3, "[%s] SMS: reading next pending index %d\n", pvt->id, next_idx);
				if (hfp_send_cmgr(pvt->hfp, next_idx) || msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
					ast_debug(1, "[%s] error sending CMGR for next SMS\n", pvt->id);
				}
			}
			pvt->incoming_sms = 1;
		}
	} else {
		ast_debug(1, "[%s] got unexpected +CMGR message, ignoring\n", pvt->id);
	}

	return 0;
}

/*!
 * \brief Convert UTF-8 string to UCS-2 hex encoding
 * \param utf8 Input UTF-8 string
 * \param hex Output buffer for hex string
 * \param hexlen Size of hex buffer
 * \return Number of characters written, or -1 on error
 *
 * UCS-2 uses big-endian byte order and each 16-bit codepoint is encoded
 * as 4 hex characters. Example: "A" -> "0041", "Привіт" -> "041F0440..."
 */
static int utf8_to_ucs2_hex(const char *utf8, char *hex, size_t hexlen)
{
	const unsigned char *src = (const unsigned char *)utf8;
	char *dst = hex;
	size_t remaining = hexlen - 1;  /* Leave room for null terminator */
	uint32_t codepoint;

	while (*src && remaining >= 4) {
		/* Decode UTF-8 to codepoint */
		if (*src < 0x80) {
			codepoint = *src++;
		} else if ((*src & 0xE0) == 0xC0) {
			if (!src[1]) {
				break;
			}
			codepoint = (*src++ & 0x1F) << 6;
			codepoint |= (*src++ & 0x3F);
		} else if ((*src & 0xF0) == 0xE0) {
			if (!src[1] || !src[2]) {
				break;
			}
			codepoint = (*src++ & 0x0F) << 12;
			codepoint |= (*src++ & 0x3F) << 6;
			codepoint |= (*src++ & 0x3F);
		} else if ((*src & 0xF8) == 0xF0) {
			/* 4-byte UTF-8 (emoji, etc.) - UCS-2 can't represent these directly */
			/* Use replacement character or skip */
			src += 4;
			codepoint = 0xFFFD;  /* Replacement character */
		} else {
			src++;  /* Skip invalid byte */
			continue;
		}

		/* Encode to UCS-2 hex (big endian) */
		if (codepoint <= 0xFFFF) {
			snprintf(dst, remaining + 1, "%04X", (unsigned int)codepoint);
			dst += 4;
			remaining -= 4;
		}
	}
	*dst = '\0';
	return (int)(dst - hex);
}

/* Static SMS reference counter for concatenated messages (wraps at 255) */
static int sms_concat_ref = 0;

/*!
 * \brief Get next SMS concatenation reference number
 * \return Reference number (1-255, wraps around)
 */
static int sms_get_next_concat_ref(void)
{
	sms_concat_ref = (sms_concat_ref % 255) + 1;
	return sms_concat_ref;
}

/*!
 * \brief Strip User Data Header (UDH) from SMS hex data
 * \param hex Input hex string (may contain UDH at start)
 * \return Pointer to the message content after UDH (same buffer, advanced)
 *
 * Used for receiving concatenated SMS. Detects common UDH formats:
 *   05 00 03 xx yy zz - 8-bit reference
 *   06 08 04 xxxx yy zz - 16-bit reference
 */
static const char *sms_strip_udh_hex(const char *hex)
{
	size_t hex_len = strlen(hex);
	char hexbuf[5];
	
	if (hex_len >= 12) {  /* Minimum UDH: "0500" + content */
		unsigned int udhl = 0;
		unsigned int iei = 0;
		
		/* Parse UDHL (first byte) */
		memcpy(hexbuf, hex, 2);
		hexbuf[2] = '\0';
		if (sscanf(hexbuf, "%02x", &udhl) == 1 && udhl >= 5 && udhl <= 7) {
			/* Parse IEI (second byte) */
			memcpy(hexbuf, hex + 2, 2);
			if (sscanf(hexbuf, "%02x", &iei) == 1 && (iei == 0x00 || iei == 0x08)) {
				/* Found concatenated SMS UDH, skip it
				 * Skip UDHL (1 byte = 2 hex chars) + UDH content (udhl bytes = udhl*2 hex chars)
				 */
				size_t skip_chars = 2 + (udhl * 2);
				if (skip_chars <= hex_len) {
					ast_debug(2, "SMS: stripping %zu hex chars of UDH (UDHL=%u, IEI=%02X)\n", 
						skip_chars, udhl, iei);
					return hex + skip_chars;
				}
			}
		}
	}
	return hex;  /* No UDH found, return original */
}

/*!
 * \brief Generate UDH (User Data Header) for concatenated SMS in hex format
 * \param ref Message reference number (8-bit, 1-255)
 * \param total_parts Total number of parts in the message
 * \param part_num Current part number (1-based)
 * \param udh_hex Output buffer for hex string (must be at least 13 bytes)
 * \return Length of UDH in hex characters (12)
 *
 * Used for sending concatenated SMS. Generates 8-bit reference UDH:
 *   05 - UDHL (5 bytes of UDH data follow)
 *   00 - IEI (Concatenated SMS, 8-bit reference)
 *   03 - IE length (3 bytes: ref + total + part)
 *   XX - Reference number
 *   YY - Total parts
 *   ZZ - Part number
 */
static int sms_generate_concat_udh_hex(int ref, int total_parts, int part_num, char *udh_hex)
{
	snprintf(udh_hex, 13, "050003%02X%02X%02X", 
		ref & 0xFF, total_parts & 0xFF, part_num & 0xFF);
	return 12;  /* Always 12 hex chars */
}

/*!
 * \brief Convert UCS-2 hex string to UTF-8
 * \param hex Input hex string (4 chars per character)
 * \param utf8 Output buffer for UTF-8 string
 * \param utf8len Size of utf8 buffer
 * \return Number of bytes written, or -1 on error
 */
static int ucs2_hex_to_utf8(const char *hex, char *utf8, size_t utf8len)
{
	char *dst = utf8;
	size_t remaining = utf8len - 1;
	uint32_t codepoint;
	char hexbuf[5];

	/* Pure UCS-2 to UTF-8 conversion - no SMS-specific handling here.
	 * For SMS messages with UDH, the caller should strip the UDH first
	 * using sms_strip_udh_hex() before calling this function.
	 */

	while (*hex && remaining > 0) {
		/* Read 4 hex chars */
		if (strlen(hex) < 4) {
			break;
		}
		memcpy(hexbuf, hex, 4);
		hexbuf[4] = '\0';
		hex += 4;

		if (sscanf(hexbuf, "%4x", &codepoint) != 1) {
			break;
		}

		/* Encode to UTF-8 */
		if (codepoint < 0x80) {
			if (remaining < 1) {
				break;
			}
			*dst++ = (char)codepoint;
			remaining--;
		} else if (codepoint < 0x800) {
			if (remaining < 2) {
				break;
			}
			*dst++ = 0xC0 | (codepoint >> 6);
			*dst++ = 0x80 | (codepoint & 0x3F);
			remaining -= 2;
		} else {
			if (remaining < 3) {
				break;
			}
			*dst++ = 0xE0 | (codepoint >> 12);
			*dst++ = 0x80 | ((codepoint >> 6) & 0x3F);
			*dst++ = 0x80 | (codepoint & 0x3F);
			remaining -= 3;
		}
	}
	*dst = '\0';
	return (int)(dst - utf8);
}

/*!
 * \brief Check if text is GSM 7-bit compatible (ASCII subset)
 * \param text UTF-8 text to check
 * \return 1 if GSM 7-bit compatible, 0 otherwise
 */
static int is_gsm7_compatible(const char *text) 
{
	if (!text) {
		return 1;
	}
	while (*text) {
		unsigned char c = (unsigned char)*text;
		/* Basic GSM 7-bit alphabet: most ASCII chars 0x20-0x7E */
		if (c > 127) {
			return 0;  /* Non-ASCII */
		}
		text++;
	}
	return 1;
}

/*!
 * \brief Convert SMS mode to string
 * \param mode SMS mode enum value
 * \return Static string representation
 */
static inline const char *sms_mode_to_str(enum sms_mode mode)
{
	switch (mode) {
	case SMS_MODE_OFF:  return "off";
	case SMS_MODE_NO:   return "no";
	case SMS_MODE_TEXT: return "text";
	case SMS_MODE_PDU:  return "pdu";
	default:            return "unknown";
	}
}

/*!
 * \brief Encode UTF-8 to GSM 7-bit packed format
 * \param utf8 Input UTF-8 string
 * \param gsm7 Output buffer for packed GSM 7-bit data
 * \param gsm7_len Size of output buffer
 * \param septets Output: number of septets (7-bit characters) encoded
 * \return Number of bytes written, or -1 on error
 *
 * GSM 7-bit packing: 8 septets (7-bit chars) pack into 7 bytes.
 * Example: "ABC" = 0x41, 0x42, 0x43 -> packed as: C1 E1 30
 */
static int gsm7_encode(const char *utf8, unsigned char *gsm7, size_t gsm7_len, int *septets)
{
	const unsigned char *src = (const unsigned char *)utf8;
	int shift = 0;
	size_t out_idx = 0;
	int septet_count = 0;
	unsigned int accumulator = 0;

	while (*src && out_idx < gsm7_len) {
		unsigned char c = *src++;
		
		/* Only handle basic ASCII for now - GSM extension chars not supported */
		if (c > 127) {
			c = '?';  /* Replace non-GSM chars with ? */
		}
		
		/* Add this septet to the accumulator */
		accumulator |= (c & 0x7F) << shift;
		shift += 7;
		septet_count++;
		
		/* Output complete bytes */
		while (shift >= 8 && out_idx < gsm7_len) {
			gsm7[out_idx++] = accumulator & 0xFF;
			accumulator >>= 8;
			shift -= 8;
		}
	}
	
	/* Output remaining bits */
	if (shift > 0 && out_idx < gsm7_len) {
		gsm7[out_idx++] = accumulator & 0xFF;
	}
	
	if (septets) {
		*septets = septet_count;
	}
	return (int)out_idx;
}

/*!
 * \brief Decode GSM 7-bit packed format to UTF-8
 * \param gsm7 Input packed GSM 7-bit data
 * \param septets Number of septets to decode
 * \param utf8 Output buffer for UTF-8 string
 * \param utf8_len Size of output buffer
 * \return Number of characters decoded, or -1 on error
 */
static int gsm7_decode(const unsigned char *gsm7, int septets, char *utf8, size_t utf8_len)
{
	int shift = 0;
	int byte_idx = 0;
	int char_count = 0;
	unsigned int accumulator = 0;

	while (char_count < septets && char_count < (int)utf8_len - 1) {
		/* Load more bits if needed */
		while (shift < 7) {
			accumulator |= gsm7[byte_idx++] << shift;
			shift += 8;
		}
		
		/* Extract a septet */
		utf8[char_count++] = accumulator & 0x7F;
		accumulator >>= 7;
		shift -= 7;
	}
	
	utf8[char_count] = '\0';
	return char_count;
}

/*!
 * \brief Encode a phone number in BCD format
 * \param number Phone number string (may include +)
 * \param bcd Output buffer (must be at least (strlen(number)+1)/2 bytes)
 * \param bcd_len Output: number of bytes written
 * \return Address type byte (0x91 for international, 0x81 for national)
 */
static int encode_phone_bcd(const char *number, unsigned char *bcd, int *bcd_len)
{
	int type = 0x81;  /* National */
	const char *p = number;
	int i = 0;
	int nibble = 0;
	unsigned char byte = 0;
	
	if (*p == '+') {
		type = 0x91;  /* International */
		p++;
	}
	
	while (*p) {
		unsigned char digit;
		if (*p >= '0' && *p <= '9') {
			digit = *p - '0';
		} else if (*p == '*') {
			digit = 0x0A;
		} else if (*p == '#') {
			digit = 0x0B;
		} else {
			p++;
			continue;  /* Skip invalid chars */
		}
		
		if (nibble == 0) {
			byte = digit;
			nibble = 1;
		} else {
			byte |= digit << 4;
			bcd[i++] = byte;
			nibble = 0;
		}
		p++;
	}
	
	/* Pad with 0xF if odd number of digits */
	if (nibble) {
		byte |= 0xF0;
		bcd[i++] = byte;
	}
	
	*bcd_len = i;
	return type;
}

/*!
 * \brief Decode BCD phone number
 * \param bcd BCD encoded number
 * \param num_digits Number of digits (not bytes)
 * \param type Address type (0x91 = international)
 * \param number Output buffer
 * \param number_len Size of output buffer
 */
static void decode_phone_bcd(const unsigned char *bcd, int num_digits, int type, char *number, size_t number_len)
{
	int i = 0;
	int out = 0;
	
	if (type == 0x91 && out < (int)number_len - 1) {
		number[out++] = '+';
	}
	
	for (i = 0; i < (num_digits + 1) / 2 && out < (int)number_len - 1; i++) {
		unsigned char lo = bcd[i] & 0x0F;
		unsigned char hi = (bcd[i] >> 4) & 0x0F;
		
		if (lo <= 9) {
			number[out++] = '0' + lo;
		}
		if (hi <= 9 && out < (int)number_len - 1) {
			number[out++] = '0' + hi;
		}
	}
	number[out] = '\0';
}

/*!
 * \brief Get human-readable description of PDU type
 * \param pdu_type The PDU type byte
 * \param is_mt 1 if mobile-terminated (received), 0 if mobile-originated (sent)
 * \param buf Output buffer
 * \param buflen Size of output buffer
 */
static void pdu_type_to_string(unsigned char pdu_type, int is_mt, char *buf, size_t buflen)
{
	int mti = pdu_type & 0x03;
	int rd_mms = (pdu_type >> 2) & 1;
	int vpf_sri = (pdu_type >> 3) & 3;
	int srr_lp = (pdu_type >> 5) & 1;
	int udhi = (pdu_type >> 6) & 1;
	int rp = (pdu_type >> 7) & 1;

	const char *mti_str;
	if (is_mt) {
		/* Mobile-Terminated */
		switch (mti) {
		case 0: mti_str = "SMS-DELIVER"; break;
		case 2: mti_str = "SMS-STATUS-REPORT"; break;
		default: mti_str = "Reserved"; break;
		}
		snprintf(buf, buflen, "MTI=%s, MMS=%d, SRI=%d, UDHI=%d, RP=%d",
			mti_str, rd_mms, vpf_sri, udhi, rp);
	} else {
		/* Mobile-Originated */
		const char *vpf_str;
		switch (vpf_sri) {
		case 0: vpf_str = "None"; break;
		case 1: vpf_str = "Enhanced"; break;
		case 2: vpf_str = "Relative"; break;
		case 3: vpf_str = "Absolute"; break;
		default: vpf_str = "?"; break;
		}
		switch (mti) {
		case 1: mti_str = "SMS-SUBMIT"; break;
		case 0: mti_str = "SMS-DELIVER-REPORT"; break;
		default: mti_str = "Reserved"; break;
		}
		snprintf(buf, buflen, "MTI=%s, RD=%d, VPF=%s, SRR=%d, UDHI=%d, RP=%d",
			mti_str, rd_mms, vpf_str, srr_lp, udhi, rp);
	}
}

/*!
 * \brief Get human-readable description of Data Coding Scheme
 * \param dcs The DCS byte
 * \param buf Output buffer
 * \param buflen Size of output buffer
 */
static void dcs_to_string(unsigned char dcs, char *buf, size_t buflen)
{
	if ((dcs & 0xC0) == 0x00) {
		/* General Data Coding indication */
		int compressed = (dcs >> 5) & 1;
		int class_meaning = (dcs >> 4) & 1;
		int alphabet = (dcs >> 2) & 3;
		int msg_class = dcs & 3;
		
		const char *alphabet_str;
		switch (alphabet) {
		case 0: alphabet_str = "GSM-7"; break;
		case 1: alphabet_str = "8-bit"; break;
		case 2: alphabet_str = "UCS-2"; break;
		default: alphabet_str = "Reserved"; break;
		}
		
		if (class_meaning) {
			snprintf(buf, buflen, "%s, %s, Class %d",
				alphabet_str, compressed ? "compressed" : "uncompressed", msg_class);
		} else {
			snprintf(buf, buflen, "%s, %s",
				alphabet_str, compressed ? "compressed" : "uncompressed");
		}
	} else if ((dcs & 0xF0) == 0xF0) {
		/* Data coding/message class */
		int alphabet = (dcs >> 2) & 1;
		int msg_class = dcs & 3;
		snprintf(buf, buflen, "%s, Class %d (immediate)", 
			alphabet ? "8-bit" : "GSM-7", msg_class);
	} else {
		snprintf(buf, buflen, "Special (0x%02X)", dcs);
	}
}

/*!
 * \brief Log PDU header for SMS-SUBMIT (outgoing message)
 * \param pvt_id Device identifier for logging
 * \param pdu_hex Hex string of the PDU
 */
static void log_pdu_submit(const char *pvt_id, const char *pdu_hex)
{
	char address[32] = "";
	char pdu_type_str[128] = "";
	char dcs_str[64] = "";
	int pos = 0;
	int smsc_len, pdu_type, mr, da_len, da_type, pid, dcs, udl;
	int da_bytes;
	
	if (!pdu_hex || strlen(pdu_hex) < 26) {
		ast_log(LOG_WARNING, "[%s] PDU too short for parsing\n", pvt_id);
		return;
	}
	
	/* Parse hex string */
	sscanf(pdu_hex + pos, "%2x", &smsc_len); pos += 2;
	if (smsc_len > 0) {
		pos += smsc_len * 2;  /* Skip SMSC data */
	}
	
	sscanf(pdu_hex + pos, "%2x", &pdu_type); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &mr); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &da_len); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &da_type); pos += 2;
	
	/* Decode destination address */
	da_bytes = (da_len + 1) / 2;
	if (da_type == 0x91 || da_type == 0xA1) {
		strcpy(address, "+");
	}
	for (int i = 0; i < da_bytes; i++) {
		int byte;
		size_t len = strlen(address);
		if (len >= sizeof(address) - 2) {
			break;
		}
		sscanf(pdu_hex + pos + i * 2, "%2x", &byte);
		int lo = byte & 0x0F;
		int hi = (byte >> 4) & 0x0F;
		if (lo <= 9) {
			address[len++] = '0' + lo;
			address[len] = '\0';
		}
		if (hi <= 9 && len < sizeof(address) - 1) {
			address[len++] = '0' + hi;
			address[len] = '\0';
		}
	}
	pos += da_bytes * 2;
	
	sscanf(pdu_hex + pos, "%2x", &pid); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &dcs); pos += 2;
	
	/* VPF in PDU type tells us if VP is present */
	int vpf = (pdu_type >> 3) & 3;
	if (vpf == 2) {
		pos += 2;      /* Relative VP: 1 byte */
	} else if (vpf == 3) {
		pos += 14;     /* Absolute VP: 7 bytes */
	} else if (vpf == 1) {
		pos += 14;     /* Enhanced VP: 7 bytes */
	}
	
	sscanf(pdu_hex + pos, "%2x", &udl); pos += 2;
	
	pdu_type_to_string((unsigned char)pdu_type, 0, pdu_type_str, sizeof(pdu_type_str));
	dcs_to_string((unsigned char)dcs, dcs_str, sizeof(dcs_str));
	
	ast_log(LOG_NOTICE, "[%s] SMS-SUBMIT PDU: To=%s, MR=%d, %s, DCS=%s, UDL=%d\n",
		pvt_id, address, mr, pdu_type_str, dcs_str, udl);
}

/*!
 * \brief Log PDU header for SMS-DELIVER (incoming message)
 * \param pvt_id Device identifier for logging
 * \param pdu_hex Hex string of the PDU
 */
static void log_pdu_deliver(const char *pvt_id, const char *pdu_hex)
{
	char address[32] = "";
	char pdu_type_str[128] = "";
	char dcs_str[64] = "";
	char timestamp[32] = "";
	int pos = 0;
	int smsc_len, pdu_type, oa_len, oa_type, pid, dcs, udl;
	int oa_bytes;
	
	if (!pdu_hex || strlen(pdu_hex) < 26) {
		ast_log(LOG_WARNING, "[%s] PDU too short for parsing\n", pvt_id);
		return;
	}
	
	/* Parse hex string */
	sscanf(pdu_hex + pos, "%2x", &smsc_len); pos += 2;
	if (smsc_len > 0) {
		pos += smsc_len * 2;  /* Skip SMSC data */
	}
	
	sscanf(pdu_hex + pos, "%2x", &pdu_type); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &oa_len); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &oa_type); pos += 2;
	
	/* Decode originating address */
	oa_bytes = (oa_len + 1) / 2;
	if (oa_type == 0x91) {
		strcpy(address, "+");
	}
	for (int i = 0; i < oa_bytes; i++) {
		int byte;
		size_t len = strlen(address);
		if (len >= sizeof(address) - 2) {
			break;
		}
		sscanf(pdu_hex + pos + i * 2, "%2x", &byte);
		int lo = byte & 0x0F;
		int hi = (byte >> 4) & 0x0F;
		if (lo <= 9) {
			address[len++] = '0' + lo;
			address[len] = '\0';
		}
		if (hi <= 9 && len < sizeof(address) - 1) {
			address[len++] = '0' + hi;
			address[len] = '\0';
		}
	}
	pos += oa_bytes * 2;
	
	sscanf(pdu_hex + pos, "%2x", &pid); pos += 2;
	sscanf(pdu_hex + pos, "%2x", &dcs); pos += 2;
	
	/* SCTS (Service Centre Time Stamp) - 7 bytes, decode to readable format */
	if (strlen(pdu_hex + pos) >= 14) {
		int scts[7];
		for (int i = 0; i < 7; i++) {
			sscanf(pdu_hex + pos + i * 2, "%2x", &scts[i]);
		}
		/* BCD decode: swap nibbles */
		int year = ((scts[0] >> 4) & 0xF) + ((scts[0] & 0xF) * 10);
		int month = ((scts[1] >> 4) & 0xF) + ((scts[1] & 0xF) * 10);
		int day = ((scts[2] >> 4) & 0xF) + ((scts[2] & 0xF) * 10);
		int hour = ((scts[3] >> 4) & 0xF) + ((scts[3] & 0xF) * 10);
		int min = ((scts[4] >> 4) & 0xF) + ((scts[4] & 0xF) * 10);
		int sec = ((scts[5] >> 4) & 0xF) + ((scts[5] & 0xF) * 10);
		snprintf(timestamp, sizeof(timestamp), "20%02d-%02d-%02d %02d:%02d:%02d",
			year, month, day, hour, min, sec);
		pos += 14;
	}
	
	sscanf(pdu_hex + pos, "%2x", &udl); pos += 2;
	
	pdu_type_to_string((unsigned char)pdu_type, 1, pdu_type_str, sizeof(pdu_type_str));
	dcs_to_string((unsigned char)dcs, dcs_str, sizeof(dcs_str));
	
	ast_log(LOG_NOTICE, "[%s] SMS-DELIVER PDU: From=%s, %s, DCS=%s, UDL=%d, Time=%s\n",
		pvt_id, address, pdu_type_str, dcs_str, udl, timestamp);
}

/*!
 * \brief Encode SMS message in PDU format (SMS-SUBMIT)
 * \param dest Destination phone number
 * \param message UTF-8 message text
 * \param use_ucs2 If non-zero, use UCS2 encoding; otherwise try GSM 7-bit
 * \param pdu_out Output buffer for PDU hex string
 * \param pdu_len Size of output buffer
 * \return Length of PDU in octets (for AT+CMGS=<length>), or -1 on error
 *
 * PDU structure for SMS-SUBMIT:
 * - SMSC length (00 = use default)
 * - PDU type (11 = SMS-SUBMIT)
 * - Message reference (00)
 * - Destination address length + type + number
 * - Protocol ID (00)
 * - Data coding scheme (00=GSM7, 08=UCS2)
 * - User data length + encoded message
 */
static int sms_encode_pdu(const char *dest, const char *message, int use_ucs2, char *pdu_out, size_t pdu_len)
{
	unsigned char pdu[256];
	int pdu_idx = 0;
	unsigned char dest_bcd[16];
	int dest_bcd_len;
	int dest_type;
	int dest_digits;
	const char *d;
	char *hex = pdu_out;
	int i;
	
	/* SMSC: use default (00) */
	pdu[pdu_idx++] = 0x00;
	
	/* PDU type: SMS-SUBMIT
	 * Bit 0-1: MTI = 01 (SMS-SUBMIT)
	 * Bit 2: RD = 0 (accept duplicates)
	 * Bit 3-4: VPF = 00 (no validity period field)
	 * Bit 5: SRR = 0 (no status report request)
	 * Bit 6: UDHI = 0 (no user data header)
	 * Bit 7: RP = 0 (no reply path)
	 * Result: 0000 0001 = 0x01
	 */
	pdu[pdu_idx++] = 0x01;
	
	/* Message reference (let phone assign) */
	pdu[pdu_idx++] = 0x00;
	
	/* Destination address */
	d = dest;
	if (*d == '+') {
		d++;
	}
	dest_digits = strlen(d);
	pdu[pdu_idx++] = dest_digits;  /* Address length in digits */
	
	dest_type = encode_phone_bcd(dest, dest_bcd, &dest_bcd_len);
	pdu[pdu_idx++] = dest_type;
	memcpy(&pdu[pdu_idx], dest_bcd, dest_bcd_len);
	pdu_idx += dest_bcd_len;
	
	/* Protocol ID */
	pdu[pdu_idx++] = 0x00;
	
	if (use_ucs2) {
		/* UCS-2 encoding */
		unsigned char ucs2_data[280];
		int ucs2_len = 0;
		const unsigned char *src = (const unsigned char *)message;
		uint32_t codepoint;
		
		/* Data Coding Scheme: UCS-2 */
		pdu[pdu_idx++] = 0x08;
		
		/* Convert UTF-8 to UCS-2 big-endian */
		while (*src && ucs2_len < 140) {
			if (*src < 0x80) {
				codepoint = *src++;
			} else if ((*src & 0xE0) == 0xC0) {
				if (!src[1]) {
					break;
				}
				codepoint = (*src++ & 0x1F) << 6;
				codepoint |= (*src++ & 0x3F);
			} else if ((*src & 0xF0) == 0xE0) {
				if (!src[1] || !src[2]) {
					break;
				}
				codepoint = (*src++ & 0x0F) << 12;
				codepoint |= (*src++ & 0x3F) << 6;
				codepoint |= (*src++ & 0x3F);
			} else {
				src++;
				codepoint = 0xFFFD;
			}
			
			if (codepoint <= 0xFFFF) {
				/* UCS-2 big-endian as per GSM 03.38 standard */
				ucs2_data[ucs2_len++] = (codepoint >> 8) & 0xFF;  /* High byte first */
				ucs2_data[ucs2_len++] = codepoint & 0xFF;         /* Low byte second */
			}
		}

		/* User data length (in octets for UCS-2) */
		pdu[pdu_idx++] = ucs2_len;
		memcpy(&pdu[pdu_idx], ucs2_data, ucs2_len);
		pdu_idx += ucs2_len;
	} else {
		/* GSM 7-bit encoding */
		unsigned char gsm7_data[160];
		int septets;
		int gsm7_bytes;
		
		/* Data Coding Scheme: GSM 7-bit */
		pdu[pdu_idx++] = 0x00;
		
		gsm7_bytes = gsm7_encode(message, gsm7_data, sizeof(gsm7_data), &septets);
		if (gsm7_bytes < 0) {
			return -1;
		}
		
		/* User data length (in septets for GSM 7-bit) */
		pdu[pdu_idx++] = septets;
		memcpy(&pdu[pdu_idx], gsm7_data, gsm7_bytes);
		pdu_idx += gsm7_bytes;
	}
	
	/* Convert to hex string */
	if (pdu_len < (size_t)(pdu_idx * 2 + 1)) {
		return -1;
	}
	
	for (i = 0; i < pdu_idx; i++) {
		snprintf(hex + i * 2, 3, "%02X", pdu[i]);
	}
	
	/* Return length excluding SMSC byte (for AT+CMGS) */
	return pdu_idx - 1;
}

/*!
 * \brief Decode SMS-DELIVER PDU to extract sender and message
 * \param pdu_hex Hex string of PDU from +CMGR response
 * \param from_number Output buffer for sender number
 * \param from_len Size of from_number buffer
 * \param message Output buffer for decoded message (UTF-8)
 * \param msg_len Size of message buffer
 * \return 0 on success, -1 on error
 */
static int sms_decode_pdu(const char *pdu_hex, char *from_number, size_t from_len, char *message, size_t msg_len)
{
	unsigned char pdu[256];
	int pdu_len = 0;
	int idx = 0;
	int smsc_len;
	int pdu_type;
	int oa_len, oa_type;
	int oa_bytes;
	int pid, dcs;
	int udl;
	int udhi;  /* User Data Header Indicator */
	int udhl = 0;  /* User Data Header Length (in bytes) */
	size_t hex_len = strlen(pdu_hex);
	
	/* Convert hex to bytes */
	for (pdu_len = 0; pdu_len < (int)sizeof(pdu) && pdu_len * 2 + 1 < (int)hex_len; pdu_len++) {
		unsigned int byte;
		if (sscanf(&pdu_hex[pdu_len * 2], "%2x", &byte) != 1) {
			break;
		}
		pdu[pdu_len] = byte;
	}
	
	if (pdu_len < 10) {
		return -1;  /* Too short */
	}
	
	/* SMSC length */
	smsc_len = pdu[idx++];
	idx += smsc_len;  /* Skip SMSC */
	
	if (idx >= pdu_len) {
		return -1;
	}
	
	/* PDU type - check for UDHI bit (bit 6) */
	pdu_type = pdu[idx++];
	udhi = (pdu_type & 0x40) ? 1 : 0;  /* User Data Header Indicator */
	
	/* Originating address length (in digits) */
	oa_len = pdu[idx++];
	oa_type = pdu[idx++];
	oa_bytes = (oa_len + 1) / 2;
	
	if (idx + oa_bytes > pdu_len) {
		return -1;
	}
	
	decode_phone_bcd(&pdu[idx], oa_len, oa_type, from_number, from_len);
	idx += oa_bytes;
	
	/* Protocol ID */
	if (idx >= pdu_len) {
		return -1;
	}
	pid = pdu[idx++];
	(void)pid;
	
	/* Data Coding Scheme */
	if (idx >= pdu_len) {
		return -1;
	}
	dcs = pdu[idx++];
	
	/* Skip timestamp (7 bytes) */
	idx += 7;
	
	if (idx >= pdu_len) {
		return -1;
	}
	
	/* User data length */
	udl = pdu[idx++];
	
	/* Handle UDH if present */
	if (udhi && idx < pdu_len) {
		udhl = pdu[idx];  /* UDH length (first byte after UDL) */
		ast_debug(2, "PDU: UDHI set, UDH length=%d bytes\n", udhl);
		/* Skip UDH: UDHL byte + UDHL bytes of header data */
		idx += 1 + udhl;
		
		/* For UCS-2: UDL is total bytes, subtract UDH size */
		if ((dcs & 0x0C) == 0x08) {
			udl -= (1 + udhl);  /* Subtract UDHL + UDH content */
		} else {
			/* For GSM 7-bit: UDL is septets, need different calculation */
			/* UDH takes (1 + udhl) bytes = (1 + udhl) * 8 bits = ceil that to septets */
			int udh_bits = (1 + udhl) * 8;
			int udh_septets = (udh_bits + 6) / 7;  /* Round up to include fill bits */
			udl -= udh_septets;
		}
	}
	
	if ((dcs & 0x0C) == 0x08) {
		/* UCS-2 encoding */
		int ucs2_len = udl;
		char *out = message;
		int remaining = msg_len - 1;
		int i;
		
		for (i = 0; i < ucs2_len && remaining > 0 && idx + 1 < pdu_len; i += 2) {
			uint16_t codepoint = (pdu[idx] << 8) | pdu[idx + 1];
			idx += 2;
			
			if (codepoint < 0x80) {
				if (remaining < 1) {
					break;
				}
				*out++ = (char)codepoint;
				remaining--;
			} else if (codepoint < 0x800) {
				if (remaining < 2) {
					break;
				}
				*out++ = 0xC0 | (codepoint >> 6);
				*out++ = 0x80 | (codepoint & 0x3F);
				remaining -= 2;
			} else {
				if (remaining < 3) {
					break;
				}
				*out++ = 0xE0 | (codepoint >> 12);
				*out++ = 0x80 | ((codepoint >> 6) & 0x3F);
				*out++ = 0x80 | (codepoint & 0x3F);
				remaining -= 3;
			}
		}
		*out = '\0';
	} else {
		/* GSM 7-bit encoding */
		int gsm7_bytes = (udl * 7 + 7) / 8;
		if (idx + gsm7_bytes > pdu_len) {
			gsm7_bytes = pdu_len - idx;
		}
		gsm7_decode(&pdu[idx], udl, message, msg_len);
	}
	
	return 0;
}

/*!
 * \brief Send SMS via SIP MESSAGE technology
 * \param msg The message to send
 * \param to Destination URI in format mobile:device/number
 * \param from Source URI
 * \return 0 on success, -1 on failure
 *
 * URI format: mobile:device_id/phone_number
 * Example: mobile:phone1/+15551234567
 */
static int mobile_msg_send(const struct ast_msg *msg, const char *to, const char *from)
{
	struct mbl_pvt *pvt;
	char *device_id, *number, *uri_copy, *slash;
	const char *body;
	char *message;

	if (ast_strlen_zero(to)) {
		ast_log(LOG_ERROR, "mobile MESSAGE: no destination specified\n");
		return -1;
	}

	/* Parse URI: mobile:device/number - skip the "mobile:" prefix */
	if (!(uri_copy = ast_strdupa(to))) {
		return -1;
	}

	/* Skip "mobile:" prefix if present */
	if (!strncmp(uri_copy, "mobile:", 7)) {
		device_id = uri_copy + 7;
	} else {
		device_id = uri_copy;
	}

	/* Find the slash separating device from number */
	slash = strchr(device_id, '/');
	if (!slash) {
		ast_log(LOG_ERROR, "mobile MESSAGE: invalid URI format '%s', expected mobile:device/number\n", to);
		return -1;
	}
	*slash = '\0';
	number = slash + 1;

	if (ast_strlen_zero(device_id) || ast_strlen_zero(number)) {
		ast_log(LOG_ERROR, "mobile MESSAGE: missing device or number in '%s'\n", to);
		return -1;
	}

	/* Find the device */
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, device_id)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devices);

	if (!pvt) {
		ast_log(LOG_ERROR, "mobile MESSAGE: device '%s' not found\n", device_id);
		return -1;
	}

	ast_mutex_lock(&pvt->lock);

	if (!pvt->connected) {
		ast_log(LOG_ERROR, "mobile MESSAGE: device '%s' not connected\n", device_id);
		ast_mutex_unlock(&pvt->lock);
		return -1;
	}

	if (pvt->sms_mode < SMS_MODE_TEXT) {
		ast_log(LOG_ERROR, "mobile MESSAGE: device '%s' does not support SMS\n", device_id);
		ast_mutex_unlock(&pvt->lock);
		return -1;
	}

	/* Check if another SMS send is already in progress */
	if (pvt->sms_send_in_progress) {
		ast_log(LOG_WARNING, "mobile MESSAGE: device '%s' is busy sending another SMS, try again later\n", device_id);
		ast_mutex_unlock(&pvt->lock);
		return -1;
	}

	body = ast_msg_get_body(msg);
	if (ast_strlen_zero(body)) {
		ast_log(LOG_WARNING, "mobile MESSAGE: empty message body\n");
		ast_mutex_unlock(&pvt->lock);
		return -1;
	}

	/* Check message length limits:
	 * - Single SMS: 160 GSM7 chars or 70 UCS2 chars
	 * - Multi-part: (153 GSM7 or 67 UCS2) * 10 parts max
	 * For now, we only support single-part SMS in PDU mode (full multi-part TODO)
	 */
	size_t body_len = strlen(body);
	int use_ucs2 = !is_gsm7_compatible(body);
	int max_chars = use_ucs2 ? 70 : 160;  /* Single SMS limit */
	int max_total = use_ucs2 ? 670 : 1530;  /* 10 parts limit */

	if ((int)body_len > max_total) {
		ast_log(LOG_WARNING, "mobile MESSAGE: message too long (%zu chars, max %d for %s). Truncating.\n",
			body_len, max_total, use_ucs2 ? "UCS2" : "GSM7");
		/* Will be truncated below */
	}

	if (pvt->sms_mode == SMS_MODE_PDU) {
		/* PDU mode: encode message as PDU */
		char pdu_hex[520];
		int pdu_len;
		const char *msg_text = body;

		/* Truncate if too long (single part for now)
		 * For UTF-8, we need to count codepoints, not bytes.
		 * Cyrillic is 2 bytes per char, so 70 chars = 140 bytes max.
		 * We need a buffer large enough for 70 chars × 4 bytes (max UTF-8) + null = 281 bytes.
		 */
		char truncated[300];
		
		/* Count UTF-8 codepoints */
		const unsigned char *p = (const unsigned char *)body;
		int char_count = 0;
		const unsigned char *truncate_pos = NULL;
		
		while (*p) {
			if (char_count == max_chars && !truncate_pos) {
				truncate_pos = p;
			}
			char_count++;
			/* Skip UTF-8 sequence */
			if (*p < 0x80) {
				p++;
			} else if ((*p & 0xE0) == 0xC0) {
				p += 2;
			} else if ((*p & 0xF0) == 0xE0) {
				p += 3;
			} else if ((*p & 0xF8) == 0xF0) {
				p += 4;
			} else {
				p++;  /* Invalid, skip */
			}
		}
		
		if (char_count > max_chars && truncate_pos) {
			size_t trunc_len = truncate_pos - (const unsigned char *)body;
			if (trunc_len >= sizeof(truncated)) {
				trunc_len = sizeof(truncated) - 1;
			}
			memcpy(truncated, body, trunc_len);
			truncated[trunc_len] = '\0';
			msg_text = truncated;
			ast_verb(3, "[%s] SMS MESSAGE: truncating to %d chars for single PDU (was %d chars)\n",
				pvt->id, max_chars, char_count);
		}

		pdu_len = sms_encode_pdu(number, msg_text, use_ucs2, pdu_hex, sizeof(pdu_hex));
		if (pdu_len < 0) {
			ast_log(LOG_ERROR, "[%s] error encoding SMS PDU\n", pvt->id);
			ast_mutex_unlock(&pvt->lock);
			return -1;
		}

		message = ast_strdup(pdu_hex);
		if (!message) {
			ast_mutex_unlock(&pvt->lock);
			return -1;
		}

		ast_verb(3, "[%s] SMS MESSAGE: sending to %s (%zu chars, mode=PDU, encoding=%s)\n",
			pvt->id, number, strlen(msg_text), use_ucs2 ? "UCS2" : "GSM7");

		if (hfp_send_cmgs_pdu(pvt->hfp, pdu_len)
			|| msg_queue_push_data(pvt, AT_SMS_PROMPT, AT_CMGS, message)) {
			ast_log(LOG_ERROR, "[%s] problem sending SMS message\n", pvt->id);
			ast_free(message);
			ast_mutex_unlock(&pvt->lock);
			return -1;
		}
		pvt->sms_send_in_progress = 1;
	} else {
		/* Text mode: handle encoding based on active charset */
		if (!strcmp(pvt->cscs_active, "UCS2")) {
			/* UCS2 mode: convert UTF-8 to hex-encoded UCS-2 */
			size_t hexlen = strlen(body) * 4 + 1;
			char *hexbuf = ast_calloc(1, hexlen);
			if (!hexbuf) {
				ast_mutex_unlock(&pvt->lock);
				return -1;
			}
			utf8_to_ucs2_hex(body, hexbuf, hexlen);
			
			/* Count UCS-2 characters (each is 4 hex chars) */
			size_t ucs2_chars = strlen(hexbuf) / 4;
			
			/* Single SMS: 70 UCS-2 chars (no UDH)
			 * Multi-part: 67 UCS-2 chars per part (3 UCS-2 chars = 6 bytes reserved for UDH)
			 * UDH in UCS-2: 6 bytes = 3 UCS-2 char spaces (12 hex chars)
			 */
			if (ucs2_chars <= SMS_UCS2_SINGLE_MAX) {
				/* Single part - no UDH needed */
				message = hexbuf;
				ast_verb(3, "[%s] SMS MESSAGE: sending to %s (%zu UCS2 chars, single part)\n",
					pvt->id, number, ucs2_chars);
					
				if (hfp_send_cmgs(pvt->hfp, number)
					|| msg_queue_push_data(pvt, AT_SMS_PROMPT, AT_CMGS, message)) {
					ast_log(LOG_ERROR, "[%s] problem sending SMS message\n", pvt->id);
					ast_free(message);
					ast_mutex_unlock(&pvt->lock);
					return -1;
				}
				ast_debug(1, "[%s] SMS queued: expecting AT_SMS_PROMPT, response_to=AT_CMGS\n", pvt->id);
			} else {
				/* Multi-part SMS with UDH
				 * Each part: 12 hex chars UDH + 67*4=268 hex chars message = 280 hex chars = 140 bytes
				 */
				int chars_per_part = SMS_UCS2_PART_MAX;
				int total_parts = ((int)ucs2_chars + chars_per_part - 1) / chars_per_part;
				int sms_ref = sms_get_next_concat_ref();
				int part;
				char udh_hex[13];
				
				if (total_parts > SMS_MAX_PARTS) {
					ast_log(LOG_WARNING, "[%s] SMS MESSAGE: message requires %d parts, limiting to %d\n",
						pvt->id, total_parts, SMS_MAX_PARTS);
					total_parts = SMS_MAX_PARTS;
				}
				
				ast_verb(3, "[%s] SMS MESSAGE: sending to %s (%zu UCS2 chars, %d parts, ref=%d)\n",
					pvt->id, number, ucs2_chars, total_parts, sms_ref);
				
				for (part = 1; part <= total_parts; part++) {
					int start_char = (part - 1) * chars_per_part;
					int end_char = start_char + chars_per_part;
					size_t start_hex = start_char * 4;
					size_t part_hex_len;
					char *part_message;
					
					if (end_char > (int)ucs2_chars) {
						end_char = (int)ucs2_chars;
					}
					part_hex_len = (end_char - start_char) * 4;
					
					/* Generate UDH and concatenate with message part */
					sms_generate_concat_udh_hex(sms_ref, total_parts, part, udh_hex);
					
					/* Allocate: UDH + message part + null */
					part_message = ast_calloc(1, SMS_UDH_HEX_LEN + part_hex_len + 1);
					if (!part_message) {
						ast_free(hexbuf);
						ast_mutex_unlock(&pvt->lock);
						return -1;
					}
					
					/* Copy UDH + message content */
					memcpy(part_message, udh_hex, SMS_UDH_HEX_LEN);
					memcpy(part_message + SMS_UDH_HEX_LEN, hexbuf + start_hex, part_hex_len);
					part_message[SMS_UDH_HEX_LEN + part_hex_len] = '\0';
					
					ast_debug(1, "[%s] SMS part %d/%d: %zu hex chars (UDH+%d UCS2 chars)\n",
						pvt->id, part, total_parts, 12 + part_hex_len, end_char - start_char);
					
					if (hfp_send_cmgs(pvt->hfp, number)
						|| msg_queue_push_data(pvt, AT_SMS_PROMPT, AT_CMGS, part_message)) {
						ast_log(LOG_ERROR, "[%s] problem sending SMS part %d/%d\n", pvt->id, part, total_parts);
						ast_free(part_message);
						ast_free(hexbuf);
						ast_mutex_unlock(&pvt->lock);
						return -1;
					}
				}
				ast_free(hexbuf);
				ast_debug(1, "[%s] SMS queued: %d parts, expecting AT_SMS_PROMPT for each\n", pvt->id, total_parts);
			}
			/* Skip the common message queue code below for UCS2 - already handled above */
			pvt->sms_send_in_progress = 1;
			ast_mutex_unlock(&pvt->lock);
			return 0;
		} else {
			/* GSM/IRA mode: only ASCII allowed */
			if (!is_gsm7_compatible(body)) {
				ast_log(LOG_ERROR, "mobile MESSAGE: device '%s' charset '%s' cannot encode Unicode. "
					"Message rejected.\n", device_id, pvt->cscs_active);
				ast_mutex_unlock(&pvt->lock);
				return -1;
			}
			message = ast_strdup(body);
			ast_verb(3, "[%s] SMS MESSAGE: sending to %s (%zu chars, charset=%s)\n",
				pvt->id, number, strlen(body), pvt->cscs_active);
			
			if (!message) {
				ast_mutex_unlock(&pvt->lock);
				return -1;
			}

			if (hfp_send_cmgs(pvt->hfp, number)
				|| msg_queue_push_data(pvt, AT_SMS_PROMPT, AT_CMGS, message)) {
				ast_log(LOG_ERROR, "[%s] problem sending SMS message\n", pvt->id);
				ast_free(message);
				ast_mutex_unlock(&pvt->lock);
				return -1;
			}
			pvt->sms_send_in_progress = 1;
			ast_debug(1, "[%s] SMS queued: expecting AT_SMS_PROMPT, response_to=AT_CMGS\n", pvt->id);
		}
	}

	ast_mutex_unlock(&pvt->lock);
	return 0;
}

static const struct ast_msg_tech mobile_msg_tech = {
	.name = "mobile",
	.msg_send = mobile_msg_send,
};

/*!
 * \brief Send an SMS message from the queue.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_sms_prompt(struct mbl_pvt *pvt, char *buf)
{
	struct msg_queue_entry *msg;
	if (!(msg = msg_queue_head(pvt))) {
		ast_debug(1, "[%s] error, got sms prompt with no pending sms messages\n", pvt->id);
		return 0;
	}

	if (msg->expected != AT_SMS_PROMPT) {
		ast_debug(1, "[%s] error, got sms prompt but queue head expects %s (response_to=%s), not AT_SMS_PROMPT\n",
			pvt->id, at_msg2str(msg->expected), at_msg2str(msg->response_to));
		return 0;
	}

	/* Send message data based on SMS mode */
	int send_result;
	if (pvt->sms_mode == SMS_MODE_PDU) {
		/* PDU mode: data is hex-encoded PDU string */
		log_pdu_submit(pvt->id, msg->data);  /* Log PDU header details */
		send_result = hfp_send_sms_pdu(pvt->hfp, msg->data);
	} else {
		/* Text mode: data is message text */
		send_result = hfp_send_sms_text(pvt->hfp, msg->data);
	}

	if (send_result || msg_queue_push(pvt, AT_OK, AT_CMGS)) {
		msg_queue_free_and_pop(pvt);
		ast_debug(1, "[%s] error sending sms message\n", pvt->id);
		return 0;
	}

	msg_queue_free_and_pop(pvt);
	return 0;
}

/*!
 * \brief Handle CUSD messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cusd(struct mbl_pvt *pvt, char *buf)
{
	char *cusd;

	if (!(cusd = hfp_parse_cusd(pvt->hfp, buf))) {
		ast_verb(0, "[%s] error parsing CUSD: %s\n", pvt->id, buf);
		return 0;
	}

	ast_verb(0, "[%s] CUSD response: %s\n", pvt->id, cusd);

	return 0;
}

/*!
 * \brief Parse a CSCS response.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \return 1 if UTF-8 is found/supported, 0 otherwise
 */
static int hfp_parse_cscs(struct hfp_pvt *hfp, char *buf, struct mbl_pvt *pvt)
{
	int found = 0;
	char *start;

	/* Suppress unused parameter warning */
	(void)hfp;

	/* Store raw list for display (strip +CSCS: prefix) */
	start = strchr(buf, ':');
	if (start) {
		start++;
		while (*start == ' ') {
			start++;
		}
		ast_copy_string(pvt->cscs_list, start, sizeof(pvt->cscs_list));
	} else {
		ast_copy_string(pvt->cscs_list, buf, sizeof(pvt->cscs_list));
	}

	/* Parse individual charsets */
	if (strstr(buf, "\"UTF-8\"") || strstr(buf, "\"UTF8\"")) {
		pvt->has_utf8 = 1;
		found = 1;
	}
	if (strstr(buf, "\"UCS2\"") || strstr(buf, "\"UCS-2\"")) {
		pvt->has_ucs2 = 1;
		found = 1;
	}
	if (strstr(buf, "\"GSM\"")) {
		pvt->has_gsm = 1;
		found = 1;
	}
	if (strstr(buf, "\"IRA\"")) {
		pvt->has_ira = 1;
		found = 1;
	}

	return found;
}

/*!
 * \brief Parse +CREG or +CGREG response
 * Format: +CREG: <n>,<stat> or +CREG: <stat>
 * \param buf the buffer to parse (null terminated)
 * \return registration status (0-5), or -1 on error
 *
 * Status values:
 *   0 = Not registered, not searching
 *   1 = Registered, home network
 *   2 = Not registered, searching
 *   3 = Registration denied
 *   4 = Unknown
 *   5 = Registered, roaming
 */
static int hfp_parse_creg(char *buf)
{
	int n, stat;
	char *p = strchr(buf, ':');
	if (!p) {
		return -1;
	}
	p++;
	while (*p == ' ') {
		p++;
	}

	/* Try two-value format first: <n>,<stat> */
	if (sscanf(p, "%d,%d", &n, &stat) == 2) {
		return stat;
	}
	/* Fall back to single value format: <stat> */
	if (sscanf(p, "%d", &stat) == 1) {
		return stat;
	}
	return -1;
}

/*!
 * \brief Parse +COPS response
 * Format: +COPS: <mode>[,<format>,<oper>]
 * \param buf response buffer
 * \param oper buffer to store operator string (or NULL)
 * \param oper_len size of oper buffer
 * \param format pointer to store format (0=alpha, 2=numeric), or NULL
 * \return 0 on success, -1 on error
 */
static int hfp_parse_cops(char *buf, char *oper, size_t oper_len, int *format)
{
	char *p, *start, *end;
	int mode, fmt;

	p = strchr(buf, ':');
	if (!p) {
		return -1;
	}
	p++;

	/* Parse mode and format: +COPS: <mode>,<format>,"<oper>" */
	if (sscanf(p, "%d,%d", &mode, &fmt) == 2) {
		if (format) {
			*format = fmt;
		}
	} else {
		if (format) {
			*format = -1;  /* Unknown format */
		}
	}

	/* Look for quoted operator string */
	start = strchr(p, '"');
	if (!start) {
		if (oper) {
			oper[0] = '\0';
		}
		return 0;  /* No operator registered */
	}
	start++;
	end = strchr(start, '"');
	if (!end) {
		return -1;
	}

	if (oper) {
		size_t len = end - start;
		if (len >= oper_len) {
			len = oper_len - 1;
		}
		memcpy(oper, start, len);
		oper[len] = '\0';
	}
	return 0;
}

/*!
 * \brief Parse +CBC response
 * Format: +CBC: <bcs>,<bcl> where bcs=charging status, bcl=level 0-100
 * \param buf response buffer
 * \param level pointer to store battery level (0-100)
 * \param charging pointer to store charging status (0=not, 1=charging)
 * \return 0 on success, -1 on error
 *
 * bcs values:
 *   0 = Not charging (battery powered)
 *   1 = Charging
 *   2 = Charging finished
 *   3 = Powered by external source, no battery
 */
static int hfp_parse_cbc(char *buf, int *level, int *charging)
{
	int bcs, bcl;
	char *p = strchr(buf, ':');
	if (!p) {
		return -1;
	}
	p++;

	if (sscanf(p, "%d,%d", &bcs, &bcl) != 2) {
		return -1;
	}

	if (level) {
		*level = bcl;
	}
	if (charging) {
		/* bcs: 0=not charging, 1=charging, 2=finished, 3=no battery */
		*charging = (bcs == 1) ? 1 : 0;
	}
	return 0;
}

/*!
 * \brief Parse +CSQ response
 * Format: +CSQ: <rssi>,<ber>
 * \param buf the buffer to parse (null terminated)
 * \return rssi value (0-31) or 99 (unknown)
 */
static int hfp_parse_csq(char *buf)
{
	int rssi, ber;
	char *p = strchr(buf, ':');
	if (!p) {
		return 99;
	}
	p++;
	
	if (sscanf(p, "%d,%d", &rssi, &ber) == 2) {
		return rssi;
	}
	return 99;
}

/*!
 * \brief Handle CPMS response lines.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cpms(struct mbl_pvt *pvt, char *buf)
{
	int used = 0, total = 0;
	hfp_parse_cpms_response(pvt->hfp, buf, &used, &total);
	ast_verb(3, "[%s] Storage \"%s\": Used %d/%d messages\n", pvt->id, pvt->sms_storage_pending, used, total);
	
	/* If we don't have a specific index to read, use the used count as a hint.
	 * On Samsung phones and others, the newest message is often stored at index = used count.
	 * This is a workaround for phones where CMGL doesn't work over Bluetooth.
	 */
	if (pvt->sms_index_to_read == 0 && used > 0) {
		pvt->sms_index_to_read = used;
		ast_debug(1, "[%s] SMS: No specific index, will try reading at index %d (from CPMS used count)\n", pvt->id, used);
	}
	
	return 0;
}

/*!
 * \brief Handle CMGL response lines.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cmgl(struct mbl_pvt *pvt, char *buf)
{
	int index;
	/* Format: +CMGL: <index>,... */
	if ((index = hfp_parse_cmgl_response(pvt->hfp, buf)) > 0) {
		ast_verb(3, "[%s] Found unread SMS at index %d\n", pvt->id, index);
		
		/* Queue the index for later processing (after CMGL OK) */
		if (pvt->sms_pending_count < 32) {
			pvt->sms_pending_indices[pvt->sms_pending_count++] = index;
		} else {
			ast_debug(1, "[%s] Too many pending SMS indices, ignoring index %d\n", pvt->id, index);
		}
		
		return 0;
	}
	return 0;
}

/*!
 * \brief Handle BUSY messages.
 * \param pvt a mbl_pvt structure
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_busy(struct mbl_pvt *pvt)
{
	pvt->hangupcause = AST_CAUSE_USER_BUSY;
	pvt->needchup = 1;
	mbl_queue_control(pvt, AST_CONTROL_BUSY);
	return 0;
}

/*!
 * \brief Handle NO DIALTONE messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_no_dialtone(struct mbl_pvt *pvt, char *buf)
{
	ast_verb(1, "[%s] mobile reports NO DIALTONE\n", pvt->id);
	pvt->needchup = 1;
	mbl_queue_control(pvt, AST_CONTROL_CONGESTION);
	return 0;
}

/*!
 * \brief Handle NO CARRIER messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_no_carrier(struct mbl_pvt *pvt, char *buf)
{
	ast_verb(1, "[%s] mobile reports NO CARRIER\n", pvt->id);
	pvt->needchup = 1;
	mbl_queue_control(pvt, AST_CONTROL_CONGESTION);
	return 0;
}

static void *do_monitor_phone(void *data)
{
	struct mbl_pvt *pvt = (struct mbl_pvt *)data;
	struct hfp_pvt *hfp = pvt->hfp;
	char buf[350];
	int t;
	enum at_message at_msg;
	struct msg_queue_entry *entry;

	/* Note: At one point the initialization procedure was neatly contained
	 * in the hfp_init() function, but that initialization method did not
	 * work with non standard devices.  As a result, the initialization
	 * procedure is not spread throughout the event handling loop.
	 */

	/* start initialization with the BRSF request */
	ast_mutex_lock(&pvt->lock);
	pvt->timeout = 10000;
	if (hfp_send_brsf(hfp, &hfp_our_brsf)  || msg_queue_push(pvt, AT_BRSF, AT_BRSF)) {
		ast_debug(1, "[%s] error sending BRSF\n", hfp->owner->id);
		goto e_cleanup;
	}
	ast_mutex_unlock(&pvt->lock);

	while (!check_unloading()) {
		ast_mutex_lock(&pvt->lock);
		t = pvt->timeout;
		ast_mutex_unlock(&pvt->lock);

		/* Track if we're actually waiting for a device response */
		int waiting_for_response = (t > 0);

		/* Check scheduler for pending callbacks (e.g., delayed SMS read) */
		int sched_wait = ast_sched_wait(pvt->sched);
		if (pvt->sms_pending_count > 0) {
			ast_debug(1, "[%s] SMS sched check: timeout=%d, sched_wait=%d, pending=%d, sched_id=%d, sched=%p\n", 
				pvt->id, t, sched_wait, pvt->sms_pending_count, pvt->sms_cmti_sched_id, pvt->sched);
		}
		if (sched_wait >= 0 && (t < 0 || sched_wait < t)) {
			t = sched_wait;
		}
		if (t < 0) {
			t = 30000;  /* Default 30 seconds if idle */
		}

		/* Debug: Log wait time before rfcomm_wait */
		if (pvt->sms_pending_count > 0) {
			ast_debug(1, "[%s] SMS: waiting for data (timeout=%d ms)\n", pvt->id, t);
		}
		
		int wait_result = rfcomm_wait(pvt->rfcomm_socket, &t);
		
		if (!wait_result) {
			/* Timeout - run any scheduled tasks that are now due */
			if (pvt->sms_pending_count > 0) {
				ast_debug(1, "[%s] SMS: rfcomm_wait timeout, running scheduler\n", pvt->id);
			}
			ast_sched_runq(pvt->sched);
			
			/* If we weren't waiting for a device response, just continue */
			if (!waiting_for_response) {
				continue;
			}
			
			/* Actual timeout while waiting for device response - disconnect */
			ast_debug(1, "[%s] timeout waiting for rfcomm data, disconnecting\n", pvt->id);
			ast_mutex_lock(&pvt->lock);
			if (!hfp->initialized) {
				if ((entry = msg_queue_head(pvt))) {
					switch (entry->response_to) {
					case AT_CIND_TEST:
						if (pvt->blackberry) {
							ast_debug(1, "[%s] timeout during CIND test\n", hfp->owner->id);
						}
						else {
							ast_debug(1, "[%s] timeout during CIND test, try setting 'blackberry=yes'\n", hfp->owner->id);
						}
						break;
					case AT_CMER:
						if (pvt->blackberry) {
							ast_debug(1, "[%s] timeout after sending CMER, try setting 'blackberry=no'\n", hfp->owner->id);
						}
						else {
							ast_debug(1, "[%s] timeout after sending CMER\n", hfp->owner->id);
						}
						break;
					default:
						ast_debug(1, "[%s] timeout while waiting for %s in response to %s\n", pvt->id, at_msg2str(entry->expected), at_msg2str(entry->response_to));
						break;
					}
				}
			}
			ast_mutex_unlock(&pvt->lock);
			goto e_cleanup;
		}
		
		/* Also run scheduler after successful read in case tasks are due */
		ast_sched_runq(pvt->sched);

		if ((at_msg = at_read_full(hfp->rsock, buf, sizeof(buf))) < 0) {
			ast_debug(1, "[%s] error reading from device: %s (%d)\n", pvt->id, strerror(errno), errno);
			break;
		}

		ast_debug(1, "[%s] read %s\n", pvt->id, buf);
		ast_verb(3, "[%s] AT<- %s [type=%s]\n", pvt->id, buf, at_msg2str(at_msg));

		switch (at_msg) {
		case AT_BRSF:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_brsf(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CSCS:
			ast_mutex_lock(&pvt->lock);
			if ((entry = msg_queue_head(pvt))) {
				if (entry->response_to == AT_CSCS) {
					/* Capability Query */
					if (hfp_parse_cscs(hfp, buf, pvt)) {
						pvt->utf8_candidate = 1;
					}
				} else if (entry->response_to == AT_CSCS_VERIFY) {
					/* Verify Query */
					if (hfp_parse_cscs(hfp, buf, pvt)) {
						pvt->has_utf8 = 1;
					}
				}
				msg_queue_push(pvt, AT_OK, entry->response_to);
				msg_queue_free_and_pop(pvt);
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CIND:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_cind(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_OK:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_ok(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CMS_ERROR:
		case AT_ERROR:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_error(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_RING:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_ring(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CIEV:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_ciev(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CLIP:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_clip(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CMTI:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_cmti(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CPMS:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_cpms(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CMGL:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_cmgl(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CMGR:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_cmgr(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_SMS_PROMPT:
			ast_mutex_lock(&pvt->lock);
			if (handle_sms_prompt(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CUSD:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_cusd(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_BUSY:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_busy(pvt)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_NO_DIALTONE:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_no_dialtone(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_NO_CARRIER:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_no_carrier(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_ECAM:
			ast_mutex_lock(&pvt->lock);
			if (hfp_parse_ecav(hfp, buf) == 7) {
				if (handle_response_busy(pvt)) {
					ast_mutex_unlock(&pvt->lock);
					goto e_cleanup;
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CREG:
			ast_mutex_lock(&pvt->lock);
			{
				int stat = hfp_parse_creg(buf);
				if (stat >= 0) {
					hfp->creg = stat;
					ast_debug(2, "[%s] CREG status: %d\n", pvt->id, stat);
				}
				/* If this was a queued response, mark as received */
				if ((entry = msg_queue_head(pvt)) && entry->expected == AT_CREG) {
					msg_queue_push(pvt, AT_OK, AT_CREG);
					msg_queue_free_and_pop(pvt);
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CGREG:
			ast_mutex_lock(&pvt->lock);
			{
				int stat = hfp_parse_creg(buf);  /* Same format as CREG */
				if (stat >= 0) {
					hfp->cgreg = stat;
					ast_debug(2, "[%s] CGREG status: %d\n", pvt->id, stat);
				}
				if ((entry = msg_queue_head(pvt)) && entry->expected == AT_CGREG) {
					msg_queue_push(pvt, AT_OK, AT_CGREG);
					msg_queue_free_and_pop(pvt);
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_COPS:
			ast_mutex_lock(&pvt->lock);
			{
				char oper[64];
				int fmt = -1;
				if (hfp_parse_cops(buf, oper, sizeof(oper), &fmt) == 0) {
					if ((entry = msg_queue_head(pvt)) && entry->expected == AT_COPS) {
						/* Check response_to to determine which query this was */
						if (entry->response_to == AT_COPS_SET_NUMERIC) {
							/* This was the numeric query - store MCC/MNC */
							if (fmt == 2 || oper[0]) {
								ast_copy_string(hfp->mccmnc, oper, sizeof(hfp->mccmnc));
								ast_debug(2, "[%s] COPS MCC/MNC: %s\n", pvt->id, hfp->mccmnc);
							}
							/* Now query alphanumeric format for provider name */
							msg_queue_push(pvt, AT_OK, AT_COPS_QUERY);
						} else if (entry->response_to == AT_COPS_FALLBACK) {
							/* This was the fallback query - store whatever we got */
							if (fmt == 2 || oper[0]) {
								ast_copy_string(hfp->mccmnc, oper, sizeof(hfp->mccmnc));
								ast_debug(2, "[%s] COPS Fallback: %s\n", pvt->id, hfp->mccmnc);
							}
							/* We are done, don't try to set validity or query alpha */
							msg_queue_push(pvt, AT_OK, AT_COPS_DONE);
						} else {
							/* This was the alphanumeric query - store provider name */
							if (fmt == 0 || (fmt != 2 && oper[0])) {
								/* Check if response is UCS-2 hex encoded (phone in UCS2 mode) */
								if (!strcasecmp(pvt->cscs_active, "UCS2")) {
									/* UCS-2 mode - decode it */
									char decoded[64];
									if (ucs2_hex_to_utf8(oper, decoded, sizeof(decoded)) > 0) {
										ast_copy_string(hfp->provider_name, decoded, sizeof(hfp->provider_name));
										ast_debug(2, "[%s] COPS Provider (decoded): %s\n", pvt->id, hfp->provider_name);
									} else {
										ast_copy_string(hfp->provider_name, oper, sizeof(hfp->provider_name));
										ast_debug(2, "[%s] COPS Provider: %s\n", pvt->id, hfp->provider_name);
									}
								} else {
									ast_copy_string(hfp->provider_name, oper, sizeof(hfp->provider_name));
									ast_debug(2, "[%s] COPS Provider: %s\n", pvt->id, hfp->provider_name);
								}
							}
							
							/* Fallback: if provider name is empty, use MCC/MNC */
							if (ast_strlen_zero(hfp->provider_name) && !ast_strlen_zero(hfp->mccmnc)) {
								ast_copy_string(hfp->provider_name, hfp->mccmnc, sizeof(hfp->provider_name));
								ast_debug(2, "[%s] COPS Provider empty, using MCC/MNC: %s\n", pvt->id, hfp->provider_name);
							}

							/* Continue to CREG chain */
							msg_queue_push(pvt, AT_OK, AT_COPS_DONE);
						}
						msg_queue_free_and_pop(pvt);
					}
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CSQ:
			ast_mutex_lock(&pvt->lock);
			{
				int rssi = hfp_parse_csq(buf);
				if (rssi != 99) {
					/* Map RSSI 0-31 to Signal 0-5
					 * 0      -> 0 (-113 dBm or less)
					 * 1-6    -> 1 (-111 to -101 dBm)
					 * 7-12   -> 2 (-99 to -89 dBm)
					 * 13-18  -> 3 (-87 to -77 dBm)
					 * 19-24  -> 4 (-75 to -65 dBm)
					 * 25-31  -> 5 (-63 dBm to -51 dBm or greater)
					 */
					int sig_level;
					if (rssi == 0) {
						sig_level = 0;
					} else if (rssi <= 6) {
						sig_level = 1;
					} else if (rssi <= 12) {
						sig_level = 2;
					} else if (rssi <= 18) {
						sig_level = 3;
					} else if (rssi <= 24) {
						sig_level = 4;
					} else {
						sig_level = 5;
					}
					
					pvt->hfp->cind_state[pvt->hfp->cind_map.signal] = sig_level;
					ast_debug(2, "[%s] CSQ RSSI: %d -> Signal: %d\n", pvt->id, rssi, sig_level);
				}
				if ((entry = msg_queue_head(pvt)) && entry->expected == AT_CSQ) {
					msg_queue_push(pvt, AT_OK, AT_CSQ);
					msg_queue_free_and_pop(pvt);
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CBC:
			ast_mutex_lock(&pvt->lock);
			{
				int level, charging;
				if (hfp_parse_cbc(buf, &level, &charging) == 0) {
					hfp->battery_percent = level;
					hfp->charging = charging;
					ast_debug(2, "[%s] CBC: %d%% %s\n", pvt->id, level,
						charging ? "charging" : "discharging");
				}
				if ((entry = msg_queue_head(pvt)) && entry->expected == AT_CBC) {
					msg_queue_push(pvt, AT_OK, AT_CBC);
					msg_queue_free_and_pop(pvt);
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_CNMI:
			/* This is the +CNMI: response from AT+CNMI=? test query */
			ast_mutex_lock(&pvt->lock);
			{
				/* Parse and decode the supported CNMI values */
				if (hfp_parse_cnmi_test(buf, pvt->cnmi_mode_vals, pvt->cnmi_mt_vals,
				                        pvt->cnmi_bm_vals, pvt->cnmi_ds_vals, 
				                        pvt->cnmi_bfr_vals) == 0) {
					/* Log decoded meanings as NOTICE */
					cnmi_log_parsed(pvt->id, pvt->cnmi_mode_vals, pvt->cnmi_mt_vals,
					                pvt->cnmi_bm_vals, pvt->cnmi_ds_vals, pvt->cnmi_bfr_vals);
					
					/* Select optimal values */
					pvt->cnmi_selected[0] = cnmi_select_mode(pvt->cnmi_mode_vals);
					pvt->cnmi_selected[1] = cnmi_select_mt(pvt->cnmi_mt_vals);
					pvt->cnmi_selected[2] = cnmi_select_bm(pvt->cnmi_bm_vals);
					pvt->cnmi_selected[3] = cnmi_select_ds(pvt->cnmi_ds_vals);
					pvt->cnmi_selected[4] = cnmi_select_bfr(pvt->cnmi_bfr_vals);
					
					ast_log(LOG_NOTICE, "[%s] CNMI auto-selected: AT+CNMI=%d,%d,%d,%d,%d\n",
						pvt->id, pvt->cnmi_selected[0], pvt->cnmi_selected[1],
						pvt->cnmi_selected[2], pvt->cnmi_selected[3], pvt->cnmi_selected[4]);
					
					pvt->cnmi_test_done = 1;
					
					/* Check if we can receive SMS notifications */
					if (pvt->cnmi_selected[0] <= 0 || pvt->cnmi_selected[1] <= 0) {
						ast_log(LOG_WARNING, "[%s] CNMI: No valid mode/mt combination for SMS reception\n", pvt->id);
					}
				} else {
					ast_debug(1, "[%s] Failed to parse CNMI test response\n", pvt->id);
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		case AT_UNKNOWN:
			ast_debug(1, "[%s] ignoring unknown message: %s\n", pvt->id, buf);
			break;
		case AT_PARSE_ERROR:
			ast_debug(1, "[%s] error parsing message\n", pvt->id);
			goto e_cleanup;
		case AT_READ_ERROR:
			ast_debug(1, "[%s] error reading from device: %s (%d)\n", pvt->id, strerror(errno), errno);
			goto e_cleanup;
		default:
			break;
		}
	}

e_cleanup:

	ast_mutex_lock(&pvt->lock);

	if (!hfp->initialized) {
		pvt->hfp_init_fail_count++;
		if (pvt->hfp_init_fail_count >= 2) {
			/* Mark device as incompatible after 2 failures */
			pvt->profile_incompatible = 1;
			mbl_set_state(pvt, MBL_STATE_ERROR);
			ast_log(LOG_WARNING, "[%s] HFP initialization failed %d times. "
				"Device does not support Hands-Free Profile properly. "
				"This may be a legacy device that only supports HSP (Headset Profile) "
				"or has incompatible HFP implementation. Will not retry connection.\n",
				pvt->id, pvt->hfp_init_fail_count);
		} else {
			ast_verb(3, "[%s] HFP initialization failed (attempt %d/2), will retry...\n",
				pvt->id, pvt->hfp_init_fail_count);
		}
	} else {
		/* Successful init resets counter */
		pvt->hfp_init_fail_count = 0;
	}

	if (pvt->owner) {
		ast_debug(1, "[%s] device disconnected, hanging up owner\n", pvt->id);
		pvt->needchup = 0;
		mbl_queue_hangup(pvt);
	}

	close(pvt->rfcomm_socket);
	close(pvt->sco_socket);
	pvt->sco_socket = -1;

	msg_queue_flush(pvt);

	/* Cancel status polling if scheduled */
	if (pvt->status_sched_id != -1) {
		AST_SCHED_DEL(pvt->sched, pvt->status_sched_id);
		pvt->status_sched_id = -1;
	}

	pvt->connected = 0;
	hfp->initialized = 0;
	if (!pvt->profile_incompatible) {
		mbl_set_state(pvt, MBL_STATE_DISCONNECTED);
	}

	pvt->adapter->inuse = 0;
	pvt->adapter->state = ADAPTER_STATE_READY;
	ast_mutex_unlock(&pvt->lock);

	if (!pvt->profile_incompatible) {
		ast_verb(3, "Bluetooth Device %s has disconnected.\n", pvt->id);
		manager_event(EVENT_FLAG_SYSTEM, "MobileStatus", "Status: Disconnect\r\nDevice: %s\r\n", pvt->id);
	}

	return NULL;
}

static int headset_send_ring(const void *data)
{
	struct mbl_pvt *pvt = (struct mbl_pvt *) data;
	ast_mutex_lock(&pvt->lock);
	if (!pvt->needring) {
		ast_mutex_unlock(&pvt->lock);
		return 0;
	}
	ast_mutex_unlock(&pvt->lock);

	if (hsp_send_ring(pvt->rfcomm_socket)) {
		ast_debug(1, "[%s] error sending RING\n", pvt->id);
		return 0;
	}
	return 1;
}

/*!
 * \brief Periodic status polling callback
 * Queries battery status and registration when device is idle
 * \param data pointer to mbl_pvt structure
 * \return 1 to reschedule, 0 to stop
 */
static int mbl_status_poll(const void *data)
{
	struct mbl_pvt *pvt = (struct mbl_pvt *) data;

	ast_mutex_lock(&pvt->lock);

	/* Skip if device not ready or in a call */
	if (!pvt->connected || !pvt->hfp || !pvt->hfp->initialized || pvt->owner) {
		ast_mutex_unlock(&pvt->lock);
		return 1;  /* Reschedule */
	}

	/* Query battery status if supported */
	if (!pvt->hfp->no_cbc) {
		if (hfp_send_cbc(pvt->hfp) || msg_queue_push(pvt, AT_CBC, AT_CBC)) {
			ast_debug(1, "[%s] error querying CBC for status poll\n", pvt->id);
		}
	}

	/* Poll signal if not supported via CIND */
	if (pvt->hfp->no_cind_signal) {
		if (hfp_send_csq(pvt->hfp) || msg_queue_push(pvt, AT_CSQ, AT_CSQ)) {
			ast_debug(1, "[%s] error querying CSQ for status poll\n", pvt->id);
		}
	}

	ast_mutex_unlock(&pvt->lock);
	return 1;  /* Reschedule */
}

static void *do_monitor_headset(void *data)
{

	struct mbl_pvt *pvt = (struct mbl_pvt *)data;
	char buf[256];
	int t;
	enum at_message at_msg;
	struct ast_channel *chan = NULL;

	ast_verb(3, "Bluetooth Device %s initialised and ready.\n", pvt->id);

	while (!check_unloading()) {

		t = ast_sched_wait(pvt->sched);
		if (t == -1) {
			t = 6000;
		}

		ast_sched_runq(pvt->sched);

		if (rfcomm_wait(pvt->rfcomm_socket, &t) == 0) {
			continue;
		}

		if ((at_msg = at_read_full(pvt->rfcomm_socket, buf, sizeof(buf))) < 0) {
			ast_debug(1, "[%s] error reading from device: %s (%d)\n", pvt->id, strerror(errno), errno);
			goto e_cleanup;
		}
		ast_debug(1, "[%s] %s\n", pvt->id, buf);

		switch (at_msg) {
		case AT_VGS:
		case AT_VGM:
			/* XXX volume change requested, we will just
			 * pretend to do something with it */
			if (hsp_send_ok(pvt->rfcomm_socket)) {
				ast_debug(1, "[%s] error sending AT message 'OK'\n", pvt->id);
				goto e_cleanup;
			}
			break;
		case AT_CKPD:
			ast_mutex_lock(&pvt->lock);
			if (pvt->outgoing) {
				pvt->needring = 0;
				hsp_send_ok(pvt->rfcomm_socket);
				if (pvt->answered) {
					/* we have an answered call up to the
					 * HS, he wants to hangup */
					mbl_queue_hangup(pvt);
				} else {
					/* we have an outgoing call to the HS,
					 * he wants to answer */
					if ((pvt->sco_socket = sco_connect(pvt->adapter->addr, pvt->addr, &pvt->sco_mtu)) == -1) {
						ast_log(LOG_ERROR, "[%s] unable to create audio connection\n", pvt->id);
						mbl_queue_hangup(pvt);
						ast_mutex_unlock(&pvt->lock);
						goto e_cleanup;
					}

					/* Reset smoother to use negotiated MTU */
					ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);

					ast_channel_set_fd(pvt->owner, 0, pvt->sco_socket);

					mbl_queue_control(pvt, AST_CONTROL_ANSWER);
					pvt->answered = 1;

					if (hsp_send_vgs(pvt->rfcomm_socket, 13) || hsp_send_vgm(pvt->rfcomm_socket, 13)) {
						ast_debug(1, "[%s] error sending VGS/VGM\n", pvt->id);
						mbl_queue_hangup(pvt);
						ast_mutex_unlock(&pvt->lock);
						goto e_cleanup;
					}
				}
			} else if (pvt->incoming) {
				/* we have an incoming call from the
				 * HS, he wants to hang up */
				mbl_queue_hangup(pvt);
			} else {
				/* no call is up, HS wants to dial */
				hsp_send_ok(pvt->rfcomm_socket);

				if ((pvt->sco_socket = sco_connect(pvt->adapter->addr, pvt->addr, &pvt->sco_mtu)) == -1) {
					ast_log(LOG_ERROR, "[%s] unable to create audio connection\n", pvt->id);
					ast_mutex_unlock(&pvt->lock);
					goto e_cleanup;
				}

				/* Reset smoother to use negotiated MTU */
				ast_smoother_reset(pvt->bt_out_smoother, pvt->sco_mtu);

				pvt->incoming = 1;

				if (!(chan = mbl_new(AST_STATE_UP, pvt, NULL, NULL, NULL))) {
					ast_log(LOG_ERROR, "[%s] unable to allocate channel for incoming call\n", pvt->id);
					ast_mutex_unlock(&pvt->lock);
					goto e_cleanup;
				}

				ast_channel_set_fd(chan, 0, pvt->sco_socket);

				ast_channel_exten_set(chan, "s");
				if (ast_pbx_start(chan)) {
					ast_log(LOG_ERROR, "[%s] unable to start pbx on incoming call\n", pvt->id);
					ast_hangup(chan);
					ast_mutex_unlock(&pvt->lock);
					goto e_cleanup;
				}
			}
			ast_mutex_unlock(&pvt->lock);
			break;
		default:
			ast_debug(1, "[%s] received unknown AT command: %s (%s)\n", pvt->id, buf, at_msg2str(at_msg));
			if (hsp_send_error(pvt->rfcomm_socket)) {
				ast_debug(1, "[%s] error sending AT message 'ERROR'\n", pvt->id);
				goto e_cleanup;
			}
			break;
		}
	}

e_cleanup:
	ast_mutex_lock(&pvt->lock);
	if (pvt->owner) {
		ast_debug(1, "[%s] device disconnected, hanging up owner\n", pvt->id);
		mbl_queue_hangup(pvt);
	}

	close(pvt->rfcomm_socket);
	close(pvt->sco_socket);
	pvt->sco_socket = -1;

	pvt->connected = 0;

	pvt->needring = 0;
	pvt->outgoing = 0;
	pvt->incoming = 0;

	pvt->adapter->inuse = 0;
	ast_mutex_unlock(&pvt->lock);

	manager_event(EVENT_FLAG_SYSTEM, "MobileStatus", "Status: Disconnect\r\nDevice: %s\r\n", pvt->id);
	ast_verb(3, "Bluetooth Device %s has disconnected\n", pvt->id);

	return NULL;

}

static int start_monitor(struct mbl_pvt *pvt)
{

	if (pvt->type == MBL_TYPE_PHONE) {
		pvt->hfp->rsock = pvt->rfcomm_socket;

		if (ast_pthread_create_background(&pvt->monitor_thread, NULL, do_monitor_phone, pvt) < 0) {
			pvt->monitor_thread = AST_PTHREADT_NULL;
			return 0;
		}
	} else {
		if (ast_pthread_create_background(&pvt->monitor_thread, NULL, do_monitor_headset, pvt) < 0) {
			pvt->monitor_thread = AST_PTHREADT_NULL;
			return 0;
		}
	}

	return 1;

}

static void *do_discovery(void *data)
{
	struct adapter_pvt *adapter;
	struct mbl_pvt *pvt;
	struct mbl_pvt *candidates[32];
	int cand_count = 0;
	int i;

	while (!check_unloading()) {
		cand_count = 0;

		/* Phase 1: Check for adapter removal/init and identify candidates */
		AST_RWLIST_RDLOCK(&adapters);
		AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
			/* Check if Ready/Busy adapters have gone away or powered down */
			if (adapter->state == ADAPTER_STATE_READY || adapter->state == ADAPTER_STATE_BUSY) {
				int ctl_sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
				if (ctl_sock >= 0) {
					struct hci_dev_info di;
					int adapter_gone = 1;
					int adapter_down = 0;

					memset(&di, 0, sizeof(di));
					di.dev_id = adapter->dev_id;
					if (ioctl(ctl_sock, HCIGETDEVINFO, &di) == 0) {
						/* Verify address still matches */
						if (bacmp(&di.bdaddr, &adapter->addr) == 0) {
							adapter_gone = 0;  /* Still there */
							/* Check if powered down */
							if (!(di.flags & (1 << HCI_UP))) {
								adapter_down = 1;
							}
						}
					}
					close(ctl_sock);

					if (adapter_gone || adapter_down) {
						struct mbl_pvt *pvt;

						if (adapter_gone) {
							ast_verb(3, "Adapter %s has been removed\n", adapter->id);
						} else {
							ast_verb(3, "Adapter %s has been powered down\n", adapter->id);
						}

						if (adapter->hci_socket >= 0) {
							close(adapter->hci_socket);
							adapter->hci_socket = -1;
						}
						adapter->inuse = 0;

						if (adapter_gone) {
							adapter->dev_id = -1;
							adapter->state = ADAPTER_STATE_NOT_FOUND;
						} else {
							/* Keep dev_id but mark not ready */
							adapter->state = ADAPTER_STATE_NOT_FOUND;
						}

						/* Update all devices using this adapter */
						AST_RWLIST_RDLOCK(&devices);
						AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
							if (!strcmp(pvt->adapter->id, adapter->id)) {
								ast_mutex_lock(&pvt->lock);
								if (pvt->connected) {
									if (pvt->rfcomm_socket > -1) {
										close(pvt->rfcomm_socket);
										pvt->rfcomm_socket = -1;
									}
									pvt->connected = 0;
									mbl_set_state(pvt, MBL_STATE_DISCONNECTED);
									ast_verb(3, "Bluetooth Device %s has been disconnected\n", pvt->id);
								}
								ast_mutex_unlock(&pvt->lock);
							}
						}
						AST_RWLIST_UNLOCK(&devices);
					}
				}
			}

			/* Try to initialize adapters that weren't found at startup */
			if (adapter->state == ADAPTER_STATE_NOT_FOUND) {
				char addr_str[18];
				int ctl_sock;
				struct hci_dev_info di;
				int found_dev_id = -1;

				ba2str(&adapter->addr, addr_str);

				/* Scan all HCI devices to find one with matching address */
				ctl_sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
				if (ctl_sock >= 0) {
					int dev_id;
					/* Scan device IDs 0-15 */
					for (dev_id = 0; dev_id < 16 && found_dev_id < 0; dev_id++) {
						memset(&di, 0, sizeof(di));
						di.dev_id = dev_id;
						if (ioctl(ctl_sock, HCIGETDEVINFO, &di) == 0) {
							if (bacmp(&di.bdaddr, &adapter->addr) == 0) {
								found_dev_id = dev_id;
								ast_debug(1, "Adapter %s: found at dev_id=%d\n", adapter->id, dev_id);
							}
						}
					}

					if (found_dev_id >= 0) {
						adapter->dev_id = found_dev_id;

						/* Re-read device info for the found device */
						memset(&di, 0, sizeof(di));
						di.dev_id = adapter->dev_id;
						if (ioctl(ctl_sock, HCIGETDEVINFO, &di) == 0) {
							/* Check for RFKill first - do not attempt power on if blocked */
							char hci_path[128];
							DIR *hci_dir;
							int rfkill_blocked = 0;

							snprintf(hci_path, sizeof(hci_path), "/sys/class/bluetooth/hci%d", adapter->dev_id);
							hci_dir = opendir(hci_path);
							if (hci_dir) {
								struct dirent *entry;
								while ((entry = readdir(hci_dir)) != NULL) {
									if (strncmp(entry->d_name, "rfkill", 6) == 0) {
										char soft_path[256], hard_path[256];
										int soft = 0, hard = 0;
										FILE *f;

										/* Read soft block */
										snprintf(soft_path, sizeof(soft_path), "%s/%s/soft", hci_path, entry->d_name);
										f = fopen(soft_path, "r");
										if (f) {
											if (fscanf(f, "%d", &soft) != 1) {
												soft = 0;
											}
											fclose(f);
										}

										/* Read hard block */
										snprintf(hard_path, sizeof(hard_path), "%s/%s/hard", hci_path, entry->d_name);
										f = fopen(hard_path, "r");
										if (f) {
											if (fscanf(f, "%d", &hard) != 1) {
												hard = 0;
											}
											fclose(f);
										}

										if (soft || hard) {
											rfkill_blocked = 1;
											ast_verb(3, "Adapter %s is %s blocked\n", 
												adapter->id, hard ? "Hardware" : "Software");
											break; 
										}
									}
								}
								closedir(hci_dir);
							}

							if (rfkill_blocked) {
								continue; /* Skip this adapter if RF killed */
							}

							if (!(di.flags & (1 << HCI_UP))) {
								/* Device is DOWN, try to power it on */
								ast_verb(3, "Adapter %s is DOWN, powering on...\n", adapter->id);
								if (ioctl(ctl_sock, HCIDEVUP, adapter->dev_id) < 0 && errno != EALREADY) {
									ast_log(LOG_WARNING, "Failed to power on adapter %s: %s\n",
										adapter->id, strerror(errno));
									continue;
								} else {
									ast_verb(3, "Adapter %s powered on successfully\n", adapter->id);
								}
							}
						}
					} else {
						ast_debug(1, "Adapter %s: no HCI device found for %s\n", adapter->id, addr_str);
						adapter->dev_id = -1;
					}
					close(ctl_sock);
				}

				if (adapter->dev_id >= 0) {
					adapter->hci_socket = hci_open_dev(adapter->dev_id);
					ast_debug(1, "Adapter %s: hci_open_dev returned socket=%d\n",
						adapter->id, adapter->hci_socket);
					if (adapter->hci_socket >= 0) {
						uint16_t vs;
						hci_read_voice_setting(adapter->hci_socket, &vs, 1000);
						vs = htobs(vs);
						ast_debug(1, "Adapter %s: voice setting=0x%04x\n", adapter->id, vs);
						if (vs == 0x0060) {
							adapter->state = ADAPTER_STATE_READY;
							ast_verb(3, "Adapter %s is now available\n", adapter->id);
						} else {
							close(adapter->hci_socket);
							adapter->hci_socket = -1;
							ast_log(LOG_WARNING, "Adapter %s voice setting is 0x%04x, must be 0x0060\n",
								adapter->id, vs);
						}
					}
				}
			}
		}

		/* Collect candidates for connection */
		AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
			if (adapter->state == ADAPTER_STATE_READY && !adapter->inuse) {
				AST_RWLIST_RDLOCK(&devices);
				AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
					ast_mutex_lock(&pvt->lock);
					if (!pvt->connected && !strcmp(pvt->adapter->id, adapter->id)) {
						if (cand_count < 32) {
							candidates[cand_count++] = pvt;
						}
					}
					ast_mutex_unlock(&pvt->lock);
				}
				AST_RWLIST_UNLOCK(&devices);
			}
		}
		AST_RWLIST_UNLOCK(&adapters);

		/* Phase 2: Process candidates (unlocked) */
		for (i = 0; i < cand_count; i++) {
			if (check_unloading()) {
				break;
			}
			pvt = candidates[i];
			ast_mutex_lock(&pvt->lock);
			
			/* Check if device address changed (config was modified) - reset failure flags */
			if (bacmp(&pvt->addr, &pvt->last_checked_addr) != 0) {
				if (pvt->profile_incompatible || pvt->sdp_fail_count || pvt->hfp_init_fail_count) {
					char addr_str[18];
					ba2str(&pvt->addr, addr_str);
					ast_verb(3, "[%s] Device address changed to %s, resetting failure counters\n", 
						pvt->id, addr_str);
					pvt->profile_incompatible = 0;
					pvt->sdp_fail_count = 0;
					pvt->hfp_init_fail_count = 0;
					pvt->rfcomm_port = 0;  /* Re-detect port for new device */
					mbl_set_state(pvt, MBL_STATE_INIT);
				}
				bacpy(&pvt->last_checked_addr, &pvt->addr);
			}

			/* Verify condition again under lock */
			if (!pvt->connected && pvt->adapter->state == ADAPTER_STATE_READY && !pvt->adapter->inuse) {
				struct adapter_pvt *adapter = pvt->adapter;
				
				ast_debug(1, "[%s] Discovery: rfcomm_port=%d, profile_incompatible=%d, adapter=%s\n",
					pvt->id, pvt->rfcomm_port, pvt->profile_incompatible, adapter->id);

				/* Deferred port detection - skip if already marked incompatible */
				if (pvt->rfcomm_port == 0 && !pvt->profile_incompatible) {
					char addr_str[18];
					int detected_port;
					ba2str(&pvt->addr, addr_str);
					
					ast_debug(1, "Detecting port for %s (type=%s)\n", pvt->id,
						pvt->type == MBL_TYPE_HEADSET ? "headset" : "phone");
					
					if (pvt->type == MBL_TYPE_HEADSET) {
						detected_port = sdp_search(addr_str, HEADSET_PROFILE_ID);
					} else {
						detected_port = sdp_search(addr_str, HANDSFREE_AGW_PROFILE_ID);
					}
					
					if (detected_port > 0) {
						ast_verb(3, "Auto-detected port %d for device %s\n", detected_port, pvt->id);
						pvt->rfcomm_port = detected_port;
						pvt->sdp_fail_count = 0;
						/* Set profile name based on type */
						if (pvt->type == MBL_TYPE_HEADSET) {
							ast_copy_string(pvt->profile_name, "HSP", sizeof(pvt->profile_name));
						} else {
							ast_copy_string(pvt->profile_name, "HFP", sizeof(pvt->profile_name));
						}
					} else if (detected_port == -1) {
						/* Transient error (device unreachable) - don't count as profile failure */
						ast_debug(1, "[%s] Device unreachable (transient error), will retry...\n", pvt->id);
					} else {
						/* detected_port == 0: SDP connected but profile not found - count as failure */
						pvt->sdp_fail_count++;
						if (pvt->sdp_fail_count >= 3) {
							/* Print helpful message and mark as incompatible */
							pvt->profile_incompatible = 1;
							mbl_set_state(pvt, MBL_STATE_ERROR);
							if (pvt->type == MBL_TYPE_HEADSET) {
								ast_log(LOG_WARNING, "[%s] Device does not support Headset Profile (HS role, UUID 0x1108). "
									"This device may only support Audio Gateway (AG) roles. "
									"A Bluetooth headset must expose the HS or HF profile, not the AG profile. "
									"Will not retry connection.\n", pvt->id);
						} else {
							ast_log(LOG_WARNING, "[%s] Device does not support Hands-Free AG Profile (UUID 0x111f). "
								"A mobile phone must expose the Audio Gateway (AG) role for HFP. "
								"If this is a headset, set type=headset in mobile.conf. "
								"Will not retry connection.\n", pvt->id);
								}
						} else {
							ast_debug(1, "Port detection failed for %s (attempt %d/3)\n", pvt->id, pvt->sdp_fail_count);
						}
					}
				}

				if (pvt->rfcomm_port > 0 && !pvt->profile_incompatible) {
					if ((pvt->rfcomm_socket = rfcomm_connect(adapter->addr, pvt->addr, pvt->rfcomm_port)) > -1) {
						mbl_set_state(pvt, MBL_STATE_CONNECTING);

						if (hci_read_remote_name(adapter->hci_socket, &pvt->addr,
								sizeof(pvt->remote_name) - 1, pvt->remote_name, 1000) < 0) {
							pvt->remote_name[0] = '\0';
						}

						if (start_monitor(pvt)) {
							pvt->connected = 1;
							adapter->inuse = 1;
							adapter->state = ADAPTER_STATE_BUSY;
							mbl_set_state(pvt, MBL_STATE_CONNECTED);
							manager_event(EVENT_FLAG_SYSTEM, "MobileStatus", "Status: Connect\r\nDevice: %s\r\n", pvt->id);
								
								/* Query Remote Version */
								{
									struct hci_conn_info_req *cr = alloca(sizeof(*cr) + sizeof(struct hci_conn_info));
									bacpy(&cr->bdaddr, &pvt->addr);
									cr->type = ACL_LINK;
									if (ioctl(adapter->hci_socket, HCIGETCONNINFO, (unsigned long)cr) == 0) {
										uint16_t handle = htobs(cr->conn_info[0].handle);
										struct hci_version ver;
										if (hci_read_remote_version(adapter->hci_socket, handle, &ver, 1000) == 0) {
											pvt->bt_ver = ver.lmp_ver;
											ast_verb(4, "Bluetooth Device %s has LMP version %d\n", pvt->id, pvt->bt_ver);
										}
									}
								}
							ast_verb(3, "Bluetooth Device %s (%s) has connected, initializing...\n",
								pvt->id, pvt->remote_name[0] ? pvt->remote_name : "unknown");
						}
					}
				}
			}
			ast_mutex_unlock(&pvt->lock);
		}

		/* Go to sleep (only if we are not unloading) */
		if (!check_unloading()) {
			sleep(discovery_interval);
		}
	}

	return NULL;
}

/*!
 * \brief Service new and existing SCO connections.
 * This thread accepts new sco connections and handles audio data.  There is
 * one do_sco_listen thread for each adapter.
 */
static void *do_sco_listen(void *data)
{
	struct adapter_pvt *adapter = (struct adapter_pvt *) data;

	while (!check_unloading()) {
		/* check for new sco connections */
		if (ast_io_wait(adapter->accept_io, 0) == -1) {
			/* handle errors */
			ast_log(LOG_ERROR, "ast_io_wait() failed for adapter %s\n", adapter->id);
			break;
		}

		/* handle audio data */
		if (ast_io_wait(adapter->io, 1) == -1) {
			ast_log(LOG_ERROR, "ast_io_wait() failed for audio on adapter %s\n", adapter->id);
			break;
		}
	}

	return NULL;
}

/*

	Module

*/

/*!
 * \brief Load an adapter from the configuration file.
 * \param cfg the config to load the adapter from
 * \param cat the adapter to load
 *
 * This function loads the given adapter and starts the sco listener thread for
 * that adapter.
 *
 * \return NULL on error, a pointer to the adapter that was loaded on success
 */
static struct adapter_pvt *mbl_load_adapter(struct ast_config *cfg, const char *cat)
{
	const char *id, *address;
	struct adapter_pvt *adapter;
	struct ast_variable *v;
	struct hci_dev_req dr;
	uint16_t vs;

	id = ast_variable_retrieve(cfg, cat, "id");
	address = ast_variable_retrieve(cfg, cat, "address");

	if (ast_strlen_zero(id) || ast_strlen_zero(address)) {
		ast_log(LOG_ERROR, "Skipping adapter. Missing id or address settings.\n");
		goto e_return;
	}

	ast_debug(1, "Reading configuration for adapter %s %s.\n", id, address);

	if (!(adapter = ast_calloc(1, sizeof(*adapter)))) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Error allocating memory.\n", id);
		goto e_return;
	}

	ast_copy_string(adapter->id, id, sizeof(adapter->id));
	str2ba(address, &adapter->addr);
	adapter->hci_socket = -1;
	adapter->sco_socket = -1;

	/* attempt to connect to the adapter using address */
	adapter->dev_id = hci_get_route(&adapter->addr);
	ast_debug(1, "Adapter %s: address=%s dev_id=%d\n", adapter->id, address, adapter->dev_id);

	if (adapter->dev_id < 0) {
		ast_log(LOG_WARNING, "Adapter %s (%s) not found. Will retry when available.\n", adapter->id, address);
		adapter->state = ADAPTER_STATE_NOT_FOUND;

		/* Still add to list so discovery can try later */

		AST_RWLIST_WRLOCK(&adapters);
		AST_RWLIST_INSERT_HEAD(&adapters, adapter, entry);
		AST_RWLIST_UNLOCK(&adapters);
		return adapter;
	}

	adapter->hci_socket = hci_open_dev(adapter->dev_id);
	if (adapter->hci_socket < 0) {
		ast_log(LOG_WARNING, "Adapter %s: Unable to open HCI device. Will retry when available.\n", adapter->id);
		adapter->state = ADAPTER_STATE_NOT_FOUND;

		AST_RWLIST_WRLOCK(&adapters);
		AST_RWLIST_INSERT_HEAD(&adapters, adapter, entry);
		AST_RWLIST_UNLOCK(&adapters);
		return adapter;
	}

	/* check voice setting */
	hci_read_voice_setting(adapter->hci_socket, &vs, 1000);
	vs = htobs(vs);
	if (vs != 0x0060) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Voice setting must be 0x0060 - see 'man hciconfig' for details.\n", adapter->id);
		goto e_hci_close_dev;
	}

	for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
		if (!strcasecmp(v->name, "forcemaster")) {
			if (ast_true(v->value)) {
				dr.dev_id = adapter->dev_id;
				if (hci_strtolm("master", &dr.dev_opt)) {
					if (ioctl(adapter->hci_socket, HCISETLINKMODE, (unsigned long) &dr) < 0) {
						ast_log(LOG_WARNING, "Unable to set adapter %s link mode to MASTER. Ignoring 'forcemaster' option.\n", adapter->id);
					}
				}
			}
		} else if (!strcasecmp(v->name, "alignmentdetection")) {
			adapter->alignment_detection = ast_true(v->value);
		}
	}

	/* create io contexts */
	if (!(adapter->accept_io = io_context_create())) {
		ast_log(LOG_ERROR, "Unable to create I/O context for audio connection listener\n");
		goto e_hci_close_dev;
	}

	if (!(adapter->io = io_context_create())) {
		ast_log(LOG_ERROR, "Unable to create I/O context for audio connections\n");
		goto e_destroy_accept_io;
	}

	/* bind the sco listener socket */
	if (sco_bind(adapter) < 0) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Error binding audio connection listener socket.\n", adapter->id);
		goto e_destroy_io;
	}

	/* add the socket to the io context */
	if (!(adapter->sco_id = ast_io_add(adapter->accept_io, adapter->sco_socket, sco_accept, AST_IO_IN, adapter))) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Error adding listener socket to I/O context.\n", adapter->id);
		goto e_close_sco;
	}

	/* start the sco listener for this adapter */
	if (ast_pthread_create_background(&adapter->sco_listener_thread, NULL, do_sco_listen, adapter)) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Error creating audio connection listener thread.\n", adapter->id);
		goto e_remove_sco;
	}

	/* add the adapter to our global list */
	AST_RWLIST_WRLOCK(&adapters);
	AST_RWLIST_INSERT_HEAD(&adapters, adapter, entry);
	AST_RWLIST_UNLOCK(&adapters);
	adapter->state = ADAPTER_STATE_READY;
	ast_debug(1, "Loaded adapter %s %s.\n", adapter->id, address);

	return adapter;

e_remove_sco:
	ast_io_remove(adapter->accept_io, adapter->sco_id);
e_close_sco:
	close(adapter->sco_socket);
e_destroy_io:
	io_context_destroy(adapter->io);
e_destroy_accept_io:
	io_context_destroy(adapter->accept_io);
e_hci_close_dev:
	hci_close_dev(adapter->hci_socket);
	ast_free(adapter);
e_return:
	return NULL;
}

/*!
 * \brief Load a device from the configuration file.
 * \param cfg the config to load the device from
 * \param cat the device to load
 * \return NULL on error, a pointer to the device that was loaded on success
 */
static struct mbl_pvt *mbl_load_device(struct ast_config *cfg, const char *cat)
{
	struct mbl_pvt *pvt, *tmp;
	struct adapter_pvt *adapter;
	struct ast_variable *v;
	const char *address, *adapter_str, *port;
	ast_debug(1, "Reading configuration for device %s.\n", cat);

	adapter_str = ast_variable_retrieve(cfg, cat, "adapter");
	if(ast_strlen_zero(adapter_str)) {
		ast_log(LOG_ERROR, "Skipping device %s. No adapter specified.\n", cat);
		goto e_return;
	}

	/* find the adapter */
	AST_RWLIST_RDLOCK(&adapters);
	AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
			if (!strcmp(adapter->id, adapter_str)) {
				break;
			}
	}
	AST_RWLIST_UNLOCK(&adapters);
	if (!adapter) {
		ast_log(LOG_ERROR, "Skipping device %s. Unknown adapter '%s' specified.\n", cat, adapter_str);
		goto e_return;
	}

	/* check if this adapter is already in use by another device */
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, tmp, entry) {
		if (tmp->adapter == adapter) {
			ast_log(LOG_ERROR, "Skipping device %s. Adapter '%s' is already in use by device '%s'.\n", cat, adapter_str, tmp->id);
			AST_RWLIST_UNLOCK(&devices);
			goto e_return;
		}
	}
	AST_RWLIST_UNLOCK(&devices);

	address = ast_variable_retrieve(cfg, cat, "address");
	port = ast_variable_retrieve(cfg, cat, "port");
	if (ast_strlen_zero(address)) {
		ast_log(LOG_ERROR, "Skipping device %s. Missing required address setting.\n", cat);
		goto e_return;
	}

	/* create and initialize our pvt structure */
	if (!(pvt = ast_calloc(1, sizeof(*pvt)))) {
		ast_log(LOG_ERROR, "Skipping device %s. Error allocating memory.\n", cat);
		goto e_return;
	}

	ast_mutex_init(&pvt->lock);
	AST_LIST_HEAD_INIT_NOLOCK(&pvt->msg_queue);

	/* set some defaults */

	pvt->type = MBL_TYPE_PHONE;
	ast_copy_string(pvt->context, "default", sizeof(pvt->context));

	/* Parse type early - needed for SDP profile selection */
	{
		const char *type_str = ast_variable_retrieve(cfg, cat, "type");
		if (type_str && !strcasecmp(type_str, "headset")) {
			pvt->type = MBL_TYPE_HEADSET;
		}
	}

	/* populate the pvt structure */
	pvt->adapter = adapter;
	ast_copy_string(pvt->id, cat, sizeof(pvt->id));
	str2ba(address, &pvt->addr);
	pvt->timeout = -1;
	pvt->rfcomm_socket = -1;

	/* Handle port: if not specified or "auto", detect via SDP */
	if (ast_strlen_zero(port) || !strcasecmp(port, "auto") || atoi(port) == 0) {
		int detected_port = 0;
		char addr_str[18];

		ast_copy_string(addr_str, address, sizeof(addr_str));

		/* Only try SDP if adapter is ready */
		if (adapter->state == ADAPTER_STATE_READY || adapter->state == ADAPTER_STATE_BUSY) {
			/* Search based on device type - no fallback between profiles */
			if (pvt->type == MBL_TYPE_HEADSET) {
				/* Headset: search for HSP */
				detected_port = sdp_search(addr_str, HEADSET_PROFILE_ID);
				if (detected_port > 0) {
					ast_log(LOG_NOTICE, "[%s] Auto-detected HSP port %d\n", cat, detected_port);
					pvt->rfcomm_port = detected_port;
					ast_copy_string(pvt->profile_name, "HSP", sizeof(pvt->profile_name));
				} else if (detected_port == -1) {
					ast_log(LOG_NOTICE, "[%s] Device not reachable, will retry when available.\n", cat);
					pvt->rfcomm_port = 0;
				} else {
					ast_log(LOG_WARNING, "[%s] Headset does not support HSP. Check device.\n", cat);
					pvt->rfcomm_port = 0;
				}
			} else {
				/* Phone: search for HFP only (no HSP fallback) */
				detected_port = sdp_search(addr_str, HANDSFREE_AGW_PROFILE_ID);
				if (detected_port > 0) {
					ast_log(LOG_NOTICE, "[%s] Auto-detected HFP port %d\n", cat, detected_port);
					pvt->rfcomm_port = detected_port;
					pvt->type = MBL_TYPE_PHONE;
					ast_copy_string(pvt->profile_name, "HFP", sizeof(pvt->profile_name));
				} else if (detected_port == -1) {
					ast_log(LOG_NOTICE, "[%s] Device not reachable, will retry when available.\n", cat);
					pvt->rfcomm_port = 0;
				} else {
					ast_log(LOG_WARNING, "[%s] Phone does not support HFP. "
							"If this is a headset, set type=headset in mobile.conf.\n", cat);
					pvt->rfcomm_port = 0;
				}
			}
		} else {
			/* Adapter not ready, defer port detection */
			ast_log(LOG_NOTICE, "[%s] Adapter not ready, deferring port detection.\n", cat);
			pvt->rfcomm_port = 0;
		}
	} else {
		pvt->rfcomm_port = atoi(port);
		/* Default to HFP when port is manually specified for phones */
		if (pvt->type == MBL_TYPE_PHONE) {
			ast_copy_string(pvt->profile_name, "HFP", sizeof(pvt->profile_name));
		} else {
			ast_copy_string(pvt->profile_name, "HSP", sizeof(pvt->profile_name));
		}
	}

	pvt->state = MBL_STATE_INIT;

	pvt->sco_socket = -1;
	pvt->sco_mtu = DEVICE_FRAME_SIZE_DEFAULT;
	pvt->monitor_thread = AST_PTHREADT_NULL;
	pvt->ring_sched_id = -1;
	pvt->status_sched_id = -1;
	pvt->sms_cmti_sched_id = -1;
	pvt->sms_mode = SMS_MODE_OFF;  /* Disabled by default, set to YES or AUTO in config to enable */

	/* setup the bt_out_smoother */
	if (!(pvt->bt_out_smoother = ast_smoother_new(pvt->sco_mtu))) {
		ast_log(LOG_ERROR, "Skipping device %s. Error setting up frame bt_out_smoother.\n", cat);
		goto e_free_pvt;
	}

	/* setup the bt_in_smoother */
	if (!(pvt->bt_in_smoother = ast_smoother_new(CHANNEL_FRAME_SIZE))) {
		ast_log(LOG_ERROR, "Skipping device %s. Error setting up frame bt_in_smoother.\n", cat);
		goto e_free_bt_out_smoother;
	}

	/* setup the dsp */
	if (!(pvt->dsp = ast_dsp_new())) {
		ast_log(LOG_ERROR, "Skipping device %s. Error setting up dsp for dtmf detection.\n", cat);
		goto e_free_bt_in_smoother;
	}

	/* setup the scheduler */
	if (!(pvt->sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create scheduler context for headset device\n");
		goto e_free_dsp;
	}

	ast_dsp_set_features(pvt->dsp, DSP_FEATURE_DIGIT_DETECT);
	ast_dsp_set_digitmode(pvt->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);

	for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
		if (!strcasecmp(v->name, "type")) {
			if (!strcasecmp(v->value, "headset")) {
				pvt->type = MBL_TYPE_HEADSET;
			}
			else {
				pvt->type = MBL_TYPE_PHONE;
			}
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(pvt->context, v->value, sizeof(pvt->context));
		} else if (!strcasecmp(v->name, "sms_delete_after_read")) {
			pvt->sms_delete_after_read = ast_true(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			/* group is set to 0 if invalid */
			pvt->group = atoi(v->value);
		} else if (!strcasecmp(v->name, "sms")) {
			if (ast_true(v->value) || !strcasecmp(v->value, "auto")) {
				/* Enable SMS - will be set to TEXT or PDU during init */
				pvt->sms_mode = SMS_MODE_NO;
			} else {
				/* Explicitly disabled */
				pvt->sms_mode = SMS_MODE_OFF;
			}
		} else if (!strcasecmp(v->name, "nocallsetup")) {
			pvt->no_callsetup = ast_true(v->value);

			if (pvt->no_callsetup) {
				ast_debug(1, "Setting nocallsetup mode for device %s.\n", pvt->id);
			}
		} else if (!strcasecmp(v->name, "blackberry")) {
			pvt->blackberry = ast_true(v->value);
			pvt->sms_mode = SMS_MODE_OFF;  /* BlackBerry doesn't support SMS over HFP */
		}
	}
	
	if (ast_strlen_zero(pvt->context)) {
		ast_copy_string(pvt->context, "default", sizeof(pvt->context));
	}

	if (pvt->type == MBL_TYPE_PHONE) {
		if (!(pvt->hfp = ast_calloc(1, sizeof(*pvt->hfp)))) {
			ast_log(LOG_ERROR, "Skipping device %s. Error allocating memory.\n", pvt->id);
			goto e_free_sched;
		}

		pvt->hfp->owner = pvt;
		pvt->hfp->rport = pvt->rfcomm_port;
		pvt->hfp->nocallsetup = pvt->no_callsetup;
		/* Initialize device status fields to unknown */
		pvt->hfp->battery_percent = -1;
		pvt->hfp->charging = -1;
		pvt->hfp->creg = -1;
		pvt->hfp->cgreg = -1;
	} else {
		pvt->sms_mode = SMS_MODE_OFF;  /* Headsets don't support SMS */
	}

	AST_RWLIST_WRLOCK(&devices);
	AST_RWLIST_INSERT_HEAD(&devices, pvt, entry);
	AST_RWLIST_UNLOCK(&devices);
	ast_debug(1, "Loaded device %s.\n", pvt->id);

	return pvt;

e_free_sched:
	ast_sched_context_destroy(pvt->sched);
e_free_dsp:
	ast_dsp_free(pvt->dsp);
e_free_bt_in_smoother:
	ast_smoother_free(pvt->bt_in_smoother);
e_free_bt_out_smoother:
	ast_smoother_free(pvt->bt_out_smoother);
e_free_pvt:
	ast_free(pvt);
e_return:
	return NULL;
}

static int mbl_load_config(void)
{
	struct ast_config *cfg;
	const char *cat;
	struct ast_variable *v;
	struct ast_flags config_flags = { 0 };

	cfg = ast_config_load(MBL_CONFIG, config_flags);
	if (!cfg) {
		cfg = ast_config_load(MBL_CONFIG_OLD, config_flags);
	}
	if (!cfg) {
		return -1;
	}

	/* parse [general] section */
	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, "interval")) {
			if (!sscanf(v->value, "%d", &discovery_interval)) {
				ast_log(LOG_NOTICE, "error parsing 'interval' in general section, using default value\n");
			}
		}
	}

	/* load adapters */
	for (cat = ast_category_browse(cfg, NULL); cat; cat = ast_category_browse(cfg, cat)) {
		if (!strcasecmp(cat, "adapter")) {
			mbl_load_adapter(cfg, cat);
		}
	}

	if (AST_RWLIST_EMPTY(&adapters)) {
		ast_log(LOG_ERROR,
			"***********************************************************************\n"
			"No adapters could be loaded from the configuration file.\n"
			"Please review mobile.conf. See sample for details.\n"
			"***********************************************************************\n"
		       );
		ast_config_destroy(cfg);
		return -1;
	}

	/* now load devices */
	for (cat = ast_category_browse(cfg, NULL); cat; cat = ast_category_browse(cfg, cat)) {
		if (strcasecmp(cat, "general") != 0 && strcasecmp(cat, "adapter")) {
			mbl_load_device(cfg, cat);
		}
	}

	ast_config_destroy(cfg);

	return 0;
}

/*!
 * \brief Check if the module is unloading.
 * \retval 0 not unloading
 * \retval 1 unloading
 */
static inline int check_unloading()
{
	int res;
	ast_mutex_lock(&unload_mutex);
	res = unloading_flag;
	ast_mutex_unlock(&unload_mutex);

	return res;
}

/*!
 * \brief Set the unloading flag.
 */
static inline void set_unloading()
{
	ast_mutex_lock(&unload_mutex);
	unloading_flag = 1;
	ast_mutex_unlock(&unload_mutex);
}

static int unload_module(void)
{
	struct mbl_pvt *pvt;
	struct adapter_pvt *adapter;

	/* First, take us out of the channel loop */
	ast_channel_unregister(&mbl_tech);

	/* Unregister the CLI & APP & function */
	ast_cli_unregister_multiple(mbl_cli, sizeof(mbl_cli) / sizeof(mbl_cli[0]));
	ast_custom_function_unregister(&mobile_status_function);
	ast_unregister_application(app_mblsendsms);
	ast_msg_tech_unregister(&mobile_msg_tech);

	/* signal everyone we are unloading */
	set_unloading();

	/* Kill the discovery thread */
	if (discovery_thread != AST_PTHREADT_NULL) {
		pthread_kill(discovery_thread, SIGURG);
		pthread_join(discovery_thread, NULL);
	}

	/* stop the sco listener threads */
	AST_RWLIST_WRLOCK(&adapters);
	AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
		pthread_kill(adapter->sco_listener_thread, SIGURG);
		pthread_join(adapter->sco_listener_thread, NULL);
	}
	AST_RWLIST_UNLOCK(&adapters);

	/* Destroy the device list */
	AST_RWLIST_WRLOCK(&devices);
	while ((pvt = AST_RWLIST_REMOVE_HEAD(&devices, entry))) {
		if (pvt->monitor_thread != AST_PTHREADT_NULL) {
			pthread_kill(pvt->monitor_thread, SIGURG);
			pthread_join(pvt->monitor_thread, NULL);
		}

		close(pvt->sco_socket);
		close(pvt->rfcomm_socket);

		msg_queue_flush(pvt);

		if (pvt->hfp) {
			ast_free(pvt->hfp);
		}

		ast_smoother_free(pvt->bt_out_smoother);
		ast_smoother_free(pvt->bt_in_smoother);
		ast_dsp_free(pvt->dsp);
		ast_sched_context_destroy(pvt->sched);
		ast_free(pvt);
	}
	AST_RWLIST_UNLOCK(&devices);

	/* Destroy the adapter list */
	AST_RWLIST_WRLOCK(&adapters);
	while ((adapter = AST_RWLIST_REMOVE_HEAD(&adapters, entry))) {
		close(adapter->sco_socket);
		io_context_destroy(adapter->io);
		io_context_destroy(adapter->accept_io);
		hci_close_dev(adapter->hci_socket);
		ast_free(adapter);
	}
	AST_RWLIST_UNLOCK(&adapters);

	if (sdp_session) {
		sdp_close(sdp_session);
	}

	ao2_ref(mbl_tech.capabilities, -1);
	mbl_tech.capabilities = NULL;
	return 0;
}

static int load_module(void)
{

	int dev_id, s;

	if (!(mbl_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_append(mbl_tech.capabilities, DEVICE_FRAME_FORMAT, 0);

	/* Check if we have Bluetooth - warn if not but still load (adapters may be connected later) */
	dev_id = hci_get_route(NULL);
	s = hci_open_dev(dev_id);
	if (dev_id < 0 || s < 0) {
		ast_log(LOG_WARNING, "No Bluetooth devices found. Module will wait for adapters to become available.\n");
	} else {
		hci_close_dev(s);
	}

	if (mbl_load_config()) {

		ast_log(LOG_ERROR, "Errors reading config file %s. Not loading module.\n", MBL_CONFIG);
		ao2_ref(mbl_tech.capabilities, -1);
		mbl_tech.capabilities = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	sdp_session = sdp_register();

	/* Spin the discovery thread */
	if (ast_pthread_create_background(&discovery_thread, NULL, do_discovery, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to create discovery thread.\n");
		goto e_cleanup;
	}

	/* register our channel type */
	if (ast_channel_register(&mbl_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", "Mobile");
		goto e_cleanup;
	}

	ast_cli_register_multiple(mbl_cli, sizeof(mbl_cli) / sizeof(mbl_cli[0]));
	ast_custom_function_register(&mobile_status_function);
	ast_register_application_xml(app_mblsendsms, mbl_sendsms_exec);

	/* Register message technology for SIP MESSAGE routing */
	if (ast_msg_tech_register(&mobile_msg_tech)) {
		ast_log(LOG_WARNING, "Unable to register message technology 'mobile'\n");
		/* Non-fatal - continue without MESSAGE support */
	}

	return AST_MODULE_LOAD_SUCCESS;

e_cleanup:
	unload_module();

	return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Bluetooth Mobile Device Channel Driver",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER
);
