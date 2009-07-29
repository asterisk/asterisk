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

/*** MODULEINFO
	<depend>bluetooth</depend>
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pthread.h>
#include <signal.h>

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
#include "asterisk/io.h"

#define MBL_CONFIG "chan_mobile.conf"
#define MBL_CONFIG_OLD "mobile.conf"

#define DEVICE_FRAME_SIZE 48
#define DEVICE_FRAME_FORMAT AST_FORMAT_SLINEAR
#define CHANNEL_FRAME_SIZE 320

static int prefformat = DEVICE_FRAME_FORMAT;

static int discovery_interval = 60;			/* The device discovery interval, default 60 seconds. */
static pthread_t discovery_thread = AST_PTHREADT_NULL;	/* The discovery thread */
static sdp_session_t *sdp_session;

AST_MUTEX_DEFINE_STATIC(unload_mutex);
static int unloading_flag = 0;
static inline int check_unloading(void);
static inline void set_unloading(void);

enum mbl_type {
	MBL_TYPE_PHONE,
	MBL_TYPE_HEADSET
};

struct adapter_pvt {
	int dev_id;					/* device id */
	int hci_socket;					/* device descriptor */
	char id[31];					/* the 'name' from mobile.conf */
	bdaddr_t addr;					/* adddress of adapter */
	unsigned int inuse:1;				/* are we in use ? */
	unsigned int alignment_detection:1;		/* do alignment detection on this adpater? */
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
	struct ast_channel *owner;			/* Channel we belong to, possibly NULL */
	struct ast_frame fr;				/* "null" frame */
	ast_mutex_t lock;				/*!< pvt lock */
	/*! queue for messages we are expecting */
	AST_LIST_HEAD_NOLOCK(msg_queue, msg_queue_entry) msg_queue;
	enum mbl_type type;				/* Phone or Headset */
	char id[31];					/* The id from mobile.conf */
	int group;					/* group number for group dialling */
	bdaddr_t addr;					/* address of device */
	struct adapter_pvt *adapter;			/* the adapter we use */
	char context[AST_MAX_CONTEXT];			/* the context for incoming calls */
	struct hfp_pvt *hfp;				/*!< hfp pvt */
	int rfcomm_port;				/* rfcomm port number */
	int rfcomm_socket;				/* rfcomm socket descriptor */
	char rfcomm_buf[256];
	char io_buf[CHANNEL_FRAME_SIZE + AST_FRIENDLY_OFFSET];
	struct ast_smoother *smoother;			/* our smoother, for making 48 byte frames */
	int sco_socket;					/* sco socket descriptor */
	pthread_t monitor_thread;			/* monitor thread handle */
	int timeout;					/*!< used to set the timeout for rfcomm data (may be used in the future) */
	unsigned int no_callsetup:1;
	unsigned int has_sms:1;
	unsigned int do_alignment_detection:1;
	unsigned int alignment_detection_triggered:1;
	unsigned int blackberry:1;
	short alignment_samples[4];
	int alignment_count;
	int ring_sched_id;
	struct ast_dsp *dsp;
	struct sched_context *sched;

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

	AST_LIST_ENTRY(mbl_pvt) entry;
};

static AST_RWLIST_HEAD_STATIC(devices, mbl_pvt);

static int handle_response_ok(struct mbl_pvt *pvt, char *buf);
static int handle_response_error(struct mbl_pvt *pvt, char *buf);
static int handle_response_ciev(struct mbl_pvt *pvt, char *buf);
static int handle_response_clip(struct mbl_pvt *pvt, char *buf);
static int handle_response_ring(struct mbl_pvt *pvt, char *buf);
static int handle_response_cmti(struct mbl_pvt *pvt, char *buf);
static int handle_response_cmgr(struct mbl_pvt *pvt, char *buf);
static int handle_sms_prompt(struct mbl_pvt *pvt, char *buf);

/* CLI stuff */
static char *handle_cli_mobile_show_devices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_search(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_mobile_rfcomm(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry mbl_cli[] = {
	AST_CLI_DEFINE(handle_cli_mobile_show_devices, "Show Bluetooth Cell / Mobile devices"),
	AST_CLI_DEFINE(handle_cli_mobile_search,       "Search for Bluetooth Cell / Mobile devices"),
	AST_CLI_DEFINE(handle_cli_mobile_rfcomm,       "Send commands to the rfcomm port for debugging"),
};

/* App stuff */
static char *app_mblstatus = "MobileStatus";
static char *mblstatus_synopsis = "MobileStatus(Device,Variable)";
static char *mblstatus_desc =
"MobileStatus(Device,Variable)\n"
"  Device - Id of mobile device from mobile.conf\n"
"  Variable - Variable to store status in will be 1-3.\n"
"             In order, Disconnected, Connected & Free, Connected & Busy.\n";

static char *app_mblsendsms = "MobileSendSMS";
static char *mblsendsms_synopsis = "MobileSendSMS(Device,Dest,Message)";
static char *mblsendsms_desc =
"MobileSendSms(Device,Dest,Message)\n"
"  Device - Id of device from mobile.conf\n"
"  Dest - destination\n"
"  Message - text of the message\n";

static struct ast_channel *mbl_new(int state, struct mbl_pvt *pvt, char *cid_num,
		const struct ast_channel *requestor);
static struct ast_channel *mbl_request(const char *type, int format,
		const struct ast_channel *requestor, void *data, int *cause);
static int mbl_call(struct ast_channel *ast, char *dest, int timeout);
static int mbl_hangup(struct ast_channel *ast);
static int mbl_answer(struct ast_channel *ast);
static int mbl_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static struct ast_frame *mbl_read(struct ast_channel *ast);
static int mbl_write(struct ast_channel *ast, struct ast_frame *frame);
static int mbl_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int mbl_devicestate(void *data);

static void do_alignment_detection(struct mbl_pvt *pvt, char *buf, int buflen);

static int mbl_queue_control(struct mbl_pvt *pvt, enum ast_control_frame_type control);
static int mbl_queue_hangup(struct mbl_pvt *pvt);
static int mbl_ast_hangup(struct mbl_pvt *pvt);

static int rfcomm_connect(bdaddr_t src, bdaddr_t dst, int remote_channel);
static int rfcomm_write(int rsock, char *buf);
static int rfcomm_write_full(int rsock, char *buf, size_t count);
static int rfcomm_wait(int rsock, int *ms);
static ssize_t rfcomm_read(int rsock, char *buf, size_t count);

static int sco_connect(bdaddr_t src, bdaddr_t dst);
static int sco_write(int s, char *buf, int len);
static int sco_accept(int *id, int fd, short events, void *data);
static int sco_bind(struct adapter_pvt *adapter);

static void *do_sco_listen(void *data);
static int sdp_search(char *addr, int profile);

static int headset_send_ring(const void *data);

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

#define HFP_CIND_UNKNOWN	-1
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
static char *hfp_parse_clip(struct hfp_pvt *hfp, char *buf);
static int hfp_parse_cmti(struct hfp_pvt *hfp, char *buf);
static int hfp_parse_cmgr(struct hfp_pvt *hfp, char *buf, char **from_number, char **text);
static int hfp_parse_brsf(struct hfp_pvt *hfp, const char *buf);
static int hfp_parse_cind(struct hfp_pvt *hfp, char *buf);
static int hfp_parse_cind_test(struct hfp_pvt *hfp, char *buf);

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
static int hfp_send_cnmi(struct hfp_pvt *hfp);
static int hfp_send_cmgr(struct hfp_pvt *hfp, int index);
static int hfp_send_cmgs(struct hfp_pvt *hfp, const char *number);
static int hfp_send_sms_text(struct hfp_pvt *hfp, const char *message);
static int hfp_send_chup(struct hfp_pvt *hfp);
static int hfp_send_atd(struct hfp_pvt *hfp, const char *number);
static int hfp_send_ata(struct hfp_pvt *hfp);

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
typedef enum {
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
	AT_CNMI,
	AT_CMER,
	AT_CIND_TEST,
} at_message_t;

static int at_match_prefix(char *buf, char *prefix);
static at_message_t at_read_full(int rsock, char *buf, size_t count);
static inline const char *at_msg2str(at_message_t msg);

struct msg_queue_entry {
	at_message_t expected;
	at_message_t response_to;
	void *data;

	AST_LIST_ENTRY(msg_queue_entry) entry;
};

static int msg_queue_push(struct mbl_pvt *pvt, at_message_t expect, at_message_t response_to);
static int msg_queue_push_data(struct mbl_pvt *pvt, at_message_t expect, at_message_t response_to, void *data);
static struct msg_queue_entry *msg_queue_pop(struct mbl_pvt *pvt);
static void msg_queue_free_and_pop(struct mbl_pvt *pvt);
static void msg_queue_flush(struct mbl_pvt *pvt);
static struct msg_queue_entry *msg_queue_head(struct mbl_pvt *pvt);

/*
 * channel stuff
 */

static const struct ast_channel_tech mbl_tech = {
	.type = "Mobile",
	.description = "Bluetooth Mobile Device Channel Driver",
	.capabilities = AST_FORMAT_SLINEAR,
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

/* CLI Commands implementation */

static char *handle_cli_mobile_show_devices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mbl_pvt *pvt;
	char bdaddr[18];
	char group[6];

#define FORMAT1 "%-15.15s %-17.17s %-5.5s %-15.15s %-9.9s %-5.5s %-3.3s\n"

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

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, FORMAT1, "ID", "Address", "Group", "Adapter", "Connected", "State", "SMS");
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		ast_mutex_lock(&pvt->lock);
		ba2str(&pvt->addr, bdaddr);
		snprintf(group, sizeof(group), "%d", pvt->group);
		ast_cli(a->fd, FORMAT1,
				pvt->id,
				bdaddr,
				group,
				pvt->adapter->id,
				pvt->connected ? "Yes" : "No",
				(!pvt->connected) ? "None" : (pvt->owner) ? "Busy" : (pvt->outgoing_sms || pvt->incoming_sms) ? "SMS" : "Free",
				(pvt->has_sms) ? "Yes" : "No"
		       );
		ast_mutex_unlock(&pvt->lock);
	}
	AST_RWLIST_UNLOCK(&devices);

#undef FORMAT1

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

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	/* find a free adapter */
	AST_RWLIST_RDLOCK(&adapters);
	AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
		if (!adapter->inuse)
			break;
	}
	AST_RWLIST_UNLOCK(&adapters);

	if (!adapter) {
		ast_cli(a->fd, "All Bluetooth adapters are in use at this time.\n");
		return CLI_SUCCESS;
	}

	len  = 8;
	max_rsp = 255;
	flags = IREQ_CACHE_FLUSH;

	ii = alloca(max_rsp * sizeof(inquiry_info));
	num_rsp = hci_inquiry(adapter->dev_id, len, max_rsp, NULL, &ii, flags);
	if (num_rsp > 0) {
		ast_cli(a->fd, FORMAT1, "Address", "Name", "Usable", "Type", "Port");
		for (i = 0; i < num_rsp; i++) {
			ba2str(&(ii + i)->bdaddr, addr);
			name[0] = 0x00;
			if (hci_read_remote_name(adapter->hci_socket, &(ii + i)->bdaddr, sizeof(name) - 1, name, 0) < 0)
				strcpy(name, "[unknown]");
			phport = sdp_search(addr, HANDSFREE_AGW_PROFILE_ID);
			if (!phport)
				hsport = sdp_search(addr, HEADSET_PROFILE_ID);
			else
				hsport = 0;
			ast_cli(a->fd, FORMAT2, addr, name, (phport > 0 || hsport > 0) ? "Yes" : "No",
				(phport > 0) ? "Phone" : "Headset", (phport > 0) ? phport : hsport);
		}
	} else
		ast_cli(a->fd, "No Bluetooth Cell / Mobile devices found.\n");

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

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, a->argv[2]))
			break;
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

/*

	Dialplan applications implementation

*/

static int mbl_status_exec(struct ast_channel *ast, const char *data)
{

	struct mbl_pvt *pvt;
	char *parse;
	int stat;
	char status[2];

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(device);
		AST_APP_ARG(variable);
	);

	if (ast_strlen_zero(data))
		return -1;

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.device) || ast_strlen_zero(args.variable))
		return -1;

	stat = 1;

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, args.device))
			break;
	}
	AST_RWLIST_UNLOCK(&devices);

	if (pvt) {
		ast_mutex_lock(&pvt->lock);
		if (pvt->connected)
			stat = 2;
		if (pvt->owner)
			stat = 3;
		ast_mutex_unlock(&pvt->lock);
	}

	snprintf(status, sizeof(status), "%d", stat);
	pbx_builtin_setvar_helper(ast, args.variable, status);

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

	if (ast_strlen_zero(data))
		return -1;

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
		if (!strcmp(pvt->id, args.device))
			break;
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

	if (!pvt->has_sms) {
		ast_log(LOG_ERROR,"Bluetooth device %s doesn't handle SMS -- SMS will not be sent.\n", args.device);
		goto e_unlock_pvt;
	}

	message = ast_strdup(args.message);

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

static struct ast_channel *mbl_new(int state, struct mbl_pvt *pvt, char *cid_num,
		const struct ast_channel *requestor)
{

	struct ast_channel *chn;

	pvt->answered = 0;
	pvt->alignment_count = 0;
	pvt->alignment_detection_triggered = 0;
	if (pvt->adapter->alignment_detection)
		pvt->do_alignment_detection = 1;
	else
		pvt->do_alignment_detection = 0;

	ast_smoother_reset(pvt->smoother, DEVICE_FRAME_SIZE);
	ast_dsp_digitreset(pvt->dsp);

	chn = ast_channel_alloc(1, state, cid_num, pvt->id, 0, 0, pvt->context,
			requestor ? requestor->linkedid : "", 0,
			"Mobile/%s-%04lx", pvt->id, ast_random() & 0xffff);
	if (!chn) {
		goto e_return;
	}

	chn->tech = &mbl_tech;
	chn->nativeformats = prefformat;
	chn->rawreadformat = prefformat;
	chn->rawwriteformat = prefformat;
	chn->writeformat = prefformat;
	chn->readformat = prefformat;
	chn->tech_pvt = pvt;

	if (state == AST_STATE_RING)
		chn->rings = 1;

	ast_string_field_set(chn, language, "en");
	pvt->owner = chn;

	if (pvt->sco_socket != -1) {
		ast_channel_set_fd(chn, 0, pvt->sco_socket);
	}

	return chn;

e_return:
	return NULL;
}

static struct ast_channel *mbl_request(const char *type, int format,
		const struct ast_channel *requestor, void *data, int *cause)
{

	struct ast_channel *chn = NULL;
	struct mbl_pvt *pvt;
	char *dest_dev = NULL;
	char *dest_num = NULL;
	int oldformat, group = -1;

	if (!data) {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		*cause = AST_CAUSE_INCOMPATIBLE_DESTINATION;
		return NULL;
	}

	oldformat = format;
	format &= (AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_WARNING, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		*cause = AST_CAUSE_FACILITY_NOT_IMPLEMENTED;
		return NULL;
	}

	dest_dev = ast_strdupa((char *)data);

	dest_num = strchr(dest_dev, '/');
	if (dest_num)
		*dest_num++ = 0x00;

	if (((dest_dev[0] == 'g') || (dest_dev[0] == 'G')) && ((dest_dev[1] >= '0') && (dest_dev[1] <= '9'))) {
		group = atoi(&dest_dev[1]);
	}

	/* Find requested device and make sure it's connected. */
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (group > -1 && pvt->group == group && pvt->connected && !pvt->owner) {
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
	chn = mbl_new(AST_STATE_DOWN, pvt, NULL, requestor);
	ast_mutex_unlock(&pvt->lock);
	if (!chn) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure.\n");
		*cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
		return NULL;
	}

	return chn;

}

static int mbl_call(struct ast_channel *ast, char *dest, int timeout)
{

	struct mbl_pvt *pvt;
	char *dest_dev = NULL;
	char *dest_num = NULL;

	dest_dev = ast_strdupa((char *)dest);

	pvt = ast->tech_pvt;

	if (pvt->type == MBL_TYPE_PHONE) {
		dest_num = strchr(dest_dev, '/');
		if (!dest_num) {
			ast_log(LOG_WARNING, "Cant determine destination number.\n");
			return -1;
		}
		*dest_num++ = 0x00;
	}

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "mbl_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	ast_debug(1, "Calling %s on %s\n", dest, ast->name);

	ast_mutex_lock(&pvt->lock);
	if (pvt->type == MBL_TYPE_PHONE) {
		if (hfp_send_atd(pvt->hfp, dest_num)) {
			ast_mutex_unlock(&pvt->lock);
			ast_log(LOG_ERROR, "error sending ATD command on %s\n", pvt->id);
			return -1;
		}
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

	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	pvt = ast->tech_pvt;

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
	ast->tech_pvt = NULL;

	ast_mutex_unlock(&pvt->lock);

	ast_setstate(ast, AST_STATE_DOWN);

	return 0;

}

static int mbl_answer(struct ast_channel *ast)
{

	struct mbl_pvt *pvt;

	pvt = ast->tech_pvt;

	if (pvt->type == MBL_TYPE_HEADSET)
		return 0;

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
	struct mbl_pvt *pvt = ast->tech_pvt;

	if (pvt->type == MBL_TYPE_HEADSET)
		return 0;

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

	struct mbl_pvt *pvt = ast->tech_pvt;
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
	pvt->fr.subclass = DEVICE_FRAME_FORMAT;
	pvt->fr.src = "Mobile";
	pvt->fr.offset = AST_FRIENDLY_OFFSET;
	pvt->fr.mallocd = 0;
	pvt->fr.delivery.tv_sec = 0;
	pvt->fr.delivery.tv_usec = 0;
	pvt->fr.data.ptr = pvt->io_buf + AST_FRIENDLY_OFFSET;

	if ((r = read(pvt->sco_socket, pvt->fr.data.ptr, DEVICE_FRAME_SIZE)) == -1) {
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

	if (pvt->do_alignment_detection)
		do_alignment_detection(pvt, pvt->fr.data.ptr, r);

	fr = ast_dsp_process(ast, pvt->dsp, &pvt->fr);

	ast_mutex_unlock(&pvt->lock);

	return fr;

e_return:
	ast_mutex_unlock(&pvt->lock);
	return fr;
}

static int mbl_write(struct ast_channel *ast, struct ast_frame *frame)
{

	struct mbl_pvt *pvt = ast->tech_pvt;
	struct ast_frame *f;

	ast_debug(3, "*** mbl_write\n");

	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	while (ast_mutex_trylock(&pvt->lock)) {
		CHANNEL_DEADLOCK_AVOIDANCE(ast);
	}

	ast_smoother_feed(pvt->smoother, frame);

	while ((f = ast_smoother_read(pvt->smoother))) {
		sco_write(pvt->sco_socket, f->data.ptr, f->datalen);
		ast_frfree(f);
	}

	ast_mutex_unlock(&pvt->lock);

	return 0;

}

static int mbl_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{

	struct mbl_pvt *pvt = newchan->tech_pvt;

	if (!pvt) {
		ast_debug(1, "fixup failed, no pvt on newchan\n");
		return -1;
	}

	ast_mutex_lock(&pvt->lock);
	if (pvt->owner == oldchan)
		pvt->owner = newchan;
	ast_mutex_unlock(&pvt->lock);

	return 0;

}

static int mbl_devicestate(void *data)
{

	char *device;
	int res = AST_DEVICE_INVALID;
	struct mbl_pvt *pvt;

	device = ast_strdupa(S_OR(data, ""));

	ast_debug(1, "Checking device state for device %s\n", device);

	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!strcmp(pvt->id, device))
			break;
	}
	AST_RWLIST_UNLOCK(&devices);

	if (!pvt)
		return res;

	ast_mutex_lock(&pvt->lock);
	if (pvt->connected) {
		if (pvt->owner)
			res = AST_DEVICE_INUSE;
		else
			res = AST_DEVICE_NOT_INUSE;
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

	If the result is <= 100 then clear the flag so we dont come back in here...

	This seems to work OK....

*/

static void do_alignment_detection(struct mbl_pvt *pvt, char *buf, int buflen)
{

	int i;
	short a, *s;
	char *p;

	if (pvt->alignment_detection_triggered) {
		for (i=buflen, p=buf+buflen-1; i>0; i--, p--)
			*p = *(p-1);
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
	} else
		pvt->do_alignment_detection = 0;

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
		} else
			break;
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
				ast_queue_hangup(pvt->owner);
				ast_channel_unlock(pvt->owner);
				break;
			}
		} else
			break;
	}
	return 0;
}

static int mbl_ast_hangup(struct mbl_pvt *pvt)
{
	int res = 0;
	for (;;) {
		if (pvt->owner) {
			if (ast_channel_trylock(pvt->owner)) {
				DEADLOCK_AVOIDANCE(&pvt->lock);
			} else {
				res = ast_hangup(pvt->owner);
				/* no need to unlock, ast_hangup() frees the
				 * channel */
				break;
			}
		} else
			break;
	}
	return res;
}

/*

	rfcomm helpers

*/

static int rfcomm_connect(bdaddr_t src, bdaddr_t dst, int remote_channel)
{

	struct sockaddr_rc addr;
	int s;

	if ((s = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)) < 0) {
		ast_debug(1, "socket() failed (%d).\n", errno);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, &src);
	addr.rc_channel = (uint8_t) 1;
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_debug(1, "bind() failed (%d).\n", errno);
		close(s);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.rc_family = AF_BLUETOOTH;
	bacpy(&addr.rc_bdaddr, &dst);
	addr.rc_channel = remote_channel;
	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_debug(1, "connect() failed (%d).\n", errno);
		close(s);
		return -1;
	}

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
	if (outfd < 0)
		outfd = 0;

	return outfd;
}

#ifdef RFCOMM_READ_DEBUG
#define rfcomm_read_debug(c) __rfcomm_read_debug(c)
static void __rfcomm_read_debug(char c)
{
	if (c == '\r')
		ast_debug(2, "rfcomm_read: \\r\n");
	else if (c == '\n')
		ast_debug(2, "rfcomm_read: \\n\n");
	else
		ast_debug(2, "rfcomm_read: %c\n", c);
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

	if (!result)
		result = &c;

	if ((res = read(rsock, result, 1)) < 1) {
		return res;
	}
	rfcomm_read_debug(*result);

	if (*result != expected) {
		return -2;
	}

	return 1;
}

/*!
 * \brief Read a character from the given stream and append it to the given
 * buffer if it matches the expected character.
 */
static int rfcomm_read_and_append_char(int rsock, char **buf, size_t count, size_t *in_count, char *result, char expected)
{
	int res;
	char c;

	if (!result)
		result = &c;

	if ((res = rfcomm_read_and_expect_char(rsock, result, expected)) < 1) {
		return res;
	}

	rfcomm_append_buf(buf, count, in_count, *result);
	return 1;
}

/*!
 * \brief Read until '\r\n'.
 * This function consumes the '\r\n' but does not add it to buf.
 */
static int rfcomm_read_until_crlf(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;

	while ((res = read(rsock, &c, 1)) == 1) {
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
	}
	return res;
}

/*!
 * \brief Read the remainder of an AT SMS prompt.
 * \note the entire parsed string is '\r\n> '
 *
 * By the time this function is executed, only a ' ' is left to read.
 */
static int rfcomm_read_sms_prompt(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	if ((res = rfcomm_read_and_append_char(rsock, buf, count, in_count, NULL, ' ')) < 1)
	       goto e_return;

	return 1;

e_return:
	ast_log(LOG_ERROR, "error parsing SMS prompt on rfcomm socket\n");
	return res;
}

/*!
 * \brief Read and AT result code.
 * \note the entire parsed string is '\r\n<result code>\r\n'
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

	if (res != 1)
		return res;

	/* check for CMGR, which contains an embedded \r\n */
	if (*in_count >= 5 && !strncmp(*buf - *in_count, "+CMGR", 5)) {
		rfcomm_append_buf(buf, count, in_count, '\r');
		rfcomm_append_buf(buf, count, in_count, '\n');
		return rfcomm_read_until_crlf(rsock, buf, count, in_count);
	}

	return 1;

e_return:
	ast_log(LOG_ERROR, "error parsing AT result on rfcomm socket");
	return res;
}

/*!
 * \brief Read the remainder of an AT command.
 * \note the entire parsed string is '<at command>\r'
 */
static int rfcomm_read_command(int rsock, char **buf, size_t count, size_t *in_count)
{
	int res;
	char c;

	while ((res = read(rsock, &c, 1)) == 1) {
		rfcomm_read_debug(c);
		/* stop when we get to '\r' */
		if (c == '\r')
			break;

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
 * prompt respectively.  When messages are read the leading and trailing '\r'
 * and '\n' characters are discarded.  If the given buffer is not large enough
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

	if (res < 1)
		return res;
	else
		return in_count;
}

/*

	sco helpers and callbacks

*/

static int sco_connect(bdaddr_t src, bdaddr_t dst)
{

	struct sockaddr_sco addr;
	int s;

	if ((s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO)) < 0) {
		ast_debug(1, "socket() failed (%d).\n", errno);
		return -1;
	}

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

	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ast_debug(1, "sco connect() failed (%d).\n", errno);
		close(s);
		return -1;
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
		ast_debug(3, "sco write error %d\n", errno);
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

	addrlen = sizeof(struct sockaddr_sco);
	if ((sock = accept(fd, (struct sockaddr *)&addr, &addrlen)) == -1) {
		ast_log(LOG_ERROR, "error accepting audio connection on adapter %s\n", adapter->id);
		return 0;
	}

	len = sizeof(so);
	getsockopt(sock, SOL_SCO, SCO_OPTIONS, &so, &len);

	ba2str(&addr.sco_bdaddr, saddr);
	ast_debug(1, "Incoming Audio Connection from device %s MTU is %d\n", saddr, so.mtu);

	/* figure out which device this sco connection belongs to */
	pvt = NULL;
	AST_RWLIST_RDLOCK(&devices);
	AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
		if (!bacmp(&pvt->addr, &addr.sco_bdaddr))
			break;
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
 * \brief Read an AT message and clasify it.
 * \param rsock an rfcomm socket
 * \param buf the buffer to store the result in
 * \param count the size of the buffer or the maximum number of characters to read
 * \return the type of message received, in addition buf will contain the
 * message received and will be null terminated
 * \see at_read()
 */
static at_message_t at_read_full(int rsock, char *buf, size_t count)
{
	ssize_t s;
	if ((s = rfcomm_read(rsock, buf, count - 1)) < 1)
		return s;
	buf[s] = '\0';

	if (!strcmp("OK", buf)) {
		return AT_OK;
	} else if (!strcmp("ERROR", buf)) {
		return AT_ERROR;
	} else if (!strcmp("RING", buf)) {
		return AT_RING;
	} else if (!strcmp("AT+CKPD=200", buf)) {
		return AT_CKPD;
	} else if (!strcmp("> ", buf)) {
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
	} else {
		return AT_UNKNOWN;
	}
}

/*!
 * \brief Get the string representation of the given AT message.
 * \param msg the message to process
 * \return a string describing the given message
 */
static inline const char *at_msg2str(at_message_t msg)
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
	case AT_CNMI:
		return "AT+CNMI";
	case AT_CMER:
		return "AT+CMER";
	case AT_CIND_TEST:
		return "AT+CIND=?";
	}
}


/*
 * bluetooth handsfree profile helpers
 */

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
	if (!value)
		value = &v;

	if (!sscanf(buf, "+CIEV: %d,%d", &i, value)) {
		ast_debug(2, "[%s] error parsing CIEV event '%s'\n", hfp->owner->id, buf);
		return HFP_CIND_NONE;
	}

	if (i >= sizeof(hfp->cind_state)) {
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
 * @note buf will be modified when the CID string is parsed
 * \return NULL on error (parse error) or a pointer to the caller id
 * information in buf
 */
static char *hfp_parse_clip(struct hfp_pvt *hfp, char *buf)
{
	int i, state;
	char *clip = NULL;
	size_t s;

	/* parse clip info in the following format:
	 * +CLIP: "123456789",128,...
	 */
	state = 0;
	s = strlen(buf);
	for (i = 0; i < s && state != 3; i++) {
		switch (state) {
		case 0: /* search for start of the number (") */
			if (buf[i] == '"') {
				state++;
			}
			break;
		case 1: /* mark the number */
			clip = &buf[i];
			state++;
			/* fall through */
		case 2: /* search for the end of the number (") */
			if (buf[i] == '"') {
				buf[i] = '\0';
				state++;
			}
			break;
		}
	}

	if (state != 3) {
		return NULL;
	}

	return clip;
}

/*!
 * \brief Parse a CMTI notification.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * @note buf will be modified when the CMTI message is parsed
 * \return -1 on error (parse error) or the index of the new sms message
 */
static int hfp_parse_cmti(struct hfp_pvt *hfp, char *buf)
{
	int index = -1;

	/* parse cmti info in the following format:
	 * +CMTI: <mem>,<index> 
	 */
	if (!sscanf(buf, "+CMTI: %*[^,],%d", &index)) {
		ast_debug(2, "[%s] error parsing CMTI event '%s'\n", hfp->owner->id, buf);
		return -1;
	}

	return index;
}

/*!
 * \brief Parse a CMGR message.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 * \param from_number a pointer to a char pointer which will store the from
 * number
 * \param text a pointer to a char pointer which will store the message text
 * @note buf will be modified when the CMGR message is parsed
 * \retval -1 parse error
 * \retval 0 success
 */
static int hfp_parse_cmgr(struct hfp_pvt *hfp, char *buf, char **from_number, char **text)
{
	int i, state;
	size_t s;

	/* parse cmgr info in the following format:
	 * +CMGR: <msg status>,"+123456789",...\r\n
	 * <message text>
	 */
	state = 0;
	s = strlen(buf);
	for (i = 0; i < s && s != 6; i++) {
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
		case 4: /* search for the start of the message text (\n) */
			if (buf[i] == '\n') {
				state++;
			}
			break;
		case 5: /* mark the start of the message text */
			if (text) {
				*text = &buf[i];
				state++;
			}
			break;
		}
	}

	if (state != 6) {
		return -1;
	}

	return 0;
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
	snprintf(cmd, sizeof(cmd), "AT+BRSF=%d\r", hfp_brsf2int(brsf));
	return rfcomm_write(hfp->rsock, cmd);
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

#if 0
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
#endif

/*!
 * \brief Enable or disable calling line identification.
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
 */
static int hfp_send_cnmi(struct hfp_pvt *hfp)
{
	return rfcomm_write(hfp->rsock, "AT+CNMI=2,1,0,0,0\r");
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
 * \brief Send the text of an SMS message.
 * \param hfp an hfp_pvt struct
 * \param message the text of the message
 */
static int hfp_send_sms_text(struct hfp_pvt *hfp, const char *message)
{
	char cmd[162];
	snprintf(cmd, sizeof(cmd), "%.160s\x1a", message);
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
 * \brief Parse BRSF data.
 * \param hfp an hfp_pvt struct
 * \param buf the buffer to parse (null terminated)
 */
static int hfp_parse_brsf(struct hfp_pvt *hfp, const char *buf)
{
	int brsf;

	if (!sscanf(buf, "+BRSF:%d", &brsf))
		return -1;

	hfp_int2brsf(brsf, &hfp->brsf);

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
	if (group >= sizeof(hfp->cind_state)) {
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
	if (state == 2)
		hfp_parse_cind_indicator(hfp, group, indicator);

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
	char *indicator = NULL, *values;

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
			values = &buf[i];
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
static int msg_queue_push(struct mbl_pvt *pvt, at_message_t expect, at_message_t response_to)
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
static int msg_queue_push_data(struct mbl_pvt *pvt, at_message_t expect, at_message_t response_to, void *data)
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
		if (msg->data)
			ast_free(msg->data);
		ast_free(msg);
	}
}

/*!
 * \brief Remove all itmes from the queue and free them.
 * \param pvt a mbl_pvt structure
 */
static void msg_queue_flush(struct mbl_pvt *pvt)
{
	struct msg_queue_entry *msg;
	while ((msg = msg_queue_head(pvt)))
		msg_queue_free_and_pop(pvt);
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

	sdp helpers

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
		ast_debug(1, "sdp_connect() failed on device %s.\n", addr);
		return 0;
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
		} else
			ast_debug(1, "No responses returned for device %s.\n", addr);
	} else
		ast_debug(1, "sdp_service_search_attr_req() failed on device %s.\n", addr);

	sdp_list_free(search_list, 0);
	sdp_list_free(attrid_list, 0);
	sdp_close(session);

	return port;

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

	int err = 0;
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

	if (!(session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY)))
		ast_log(LOG_WARNING, "Failed to connect sdp and create session.\n");
	else
		err = sdp_record_register(session, record, 0);

	sdp_data_free(channel);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	sdp_list_free(svc_uuid_list, 0);

	return session;

}

/*

	Thread routines

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
			ast_debug(1, "[%s] BSRF sent successfully\n", pvt->id);

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
			ast_debug(1, "[%s] caling line indication enabled\n", pvt->id);
			if (hfp_send_vgs(pvt->hfp, 15) || msg_queue_push(pvt, AT_OK, AT_VGS)) {
				ast_debug(1, "[%s] error synchronizing gain settings\n", pvt->id);
				goto e_return;
			}

			pvt->timeout = -1;
			pvt->hfp->initialized = 1;
			ast_verb(3, "Bluetooth Device %s initialized and ready.\n", pvt->id);

			break;
		case AT_VGS:
			ast_debug(1, "[%s] volume level synchronization successful\n", pvt->id);

			/* set the SMS operating mode to text mode */
			if (hfp_send_cmgf(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMGF)) {
				ast_debug(1, "[%s] error setting CMGF\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CMGF:
			ast_debug(1, "[%s] sms text mode enabled\n", pvt->id);
			/* turn on SMS new message indication */
			if (hfp_send_cnmi(pvt->hfp) || msg_queue_push(pvt, AT_OK, AT_CNMI)) {
				ast_debug(1, "[%s] error setting CNMI\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CNMI:
			ast_debug(1, "[%s] sms new message indication enabled\n", pvt->id);
			pvt->has_sms = 1;
			break;
		/* end initialization stuff */

		case AT_A:
			ast_debug(1, "[%s] answer sent successfully\n", pvt->id);
			pvt->needchup = 1;
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
		case AT_CMGR:
			ast_debug(1, "[%s] successfully read sms message\n", pvt->id);
			pvt->incoming_sms = 0;
			break;
		case AT_CMGS:
			ast_debug(1, "[%s] successfully sent sms message\n", pvt->id);
			pvt->outgoing_sms = 0;
			break;
		case AT_VTS:
			ast_debug(1, "[%s] digit sent successfully\n", pvt->id);
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
			|| entry->expected == AT_CMS_ERROR
			|| entry->expected == AT_CMGR
			|| entry->expected == AT_SMS_PROMPT)) {
		switch (entry->response_to) {

		/* initialization stuff */
		case AT_BRSF:
			ast_debug(1, "[%s] error reading BSRF\n", pvt->id);
			goto e_return;
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
		case AT_VGS:
			ast_debug(1, "[%s] volume level synchronization failed\n", pvt->id);

			/* this is not a fatal error, let's continue with initialization */

			/* set the SMS operating mode to text mode */
			if (hfp_send_cmgf(pvt->hfp, 1) || msg_queue_push(pvt, AT_OK, AT_CMGF)) {
				ast_debug(1, "[%s] error setting CMGF\n", pvt->id);
				goto e_return;
			}
			break;
		case AT_CMGF:
			ast_debug(1, "[%s] error setting CMGF\n", pvt->id);
			ast_debug(1, "[%s] no SMS support\n", pvt->id);
			break;
		case AT_CNMI:
			ast_debug(1, "[%s] error setting CNMI\n", pvt->id);
			ast_debug(1, "[%s] no SMS support\n", pvt->id);
			break;
		/* end initialization stuff */

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
			ast_debug(1, "[%s] error reading sms message\n", pvt->id);
			pvt->incoming_sms = 0;
			break;
		case AT_CMGS:
			ast_debug(1, "[%s] error sending sms message\n", pvt->id);
			pvt->outgoing_sms = 0;
			break;
		case AT_VTS:
			ast_debug(1, "[%s] error sending digit\n", pvt->id);
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
					ast_log(LOG_ERROR, "[%s] error queueing hangup, disconnectiong...\n", pvt->id);
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
					if (mbl_queue_hangup(pvt)) {
						ast_log(LOG_ERROR, "[%s] error queueing hangup, disconnectiong...\n", pvt->id);
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
	char *clip;
	struct msg_queue_entry *msg;
	struct ast_channel *chan;

	if ((msg = msg_queue_head(pvt)) && msg->expected == AT_CLIP) {
		msg_queue_free_and_pop(pvt);

		pvt->needcallerid = 0;
		if (!(clip = hfp_parse_clip(pvt->hfp, buf))) {
			ast_debug(1, "[%s] error parsing CLIP: %s\n", pvt->id, buf);
		}

		if (!(chan = mbl_new(AST_STATE_RING, pvt, clip, NULL))) {
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
 * \brief Handle AT+CMTI messages.
 * \param pvt a mbl_pvt structure
 * \param buf a null terminated buffer containing an AT message
 * \retval 0 success
 * \retval -1 error
 */
static int handle_response_cmti(struct mbl_pvt *pvt, char *buf)
{
	int index = hfp_parse_cmti(pvt->hfp, buf);
	if (index > 0) {
		ast_debug(1, "[%s] incoming sms message\n", pvt->id);

		if (hfp_send_cmgr(pvt->hfp, index)
				|| msg_queue_push(pvt, AT_CMGR, AT_CMGR)) {
			ast_debug(1, "[%s] error sending CMGR to retrieve SMS message\n", pvt->id);
			return -1;
		}

		pvt->incoming_sms = 1;
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
	char *from_number = NULL, *text = NULL;
	struct ast_channel *chan;
	struct msg_queue_entry *msg;

	if ((msg = msg_queue_head(pvt)) && msg->expected == AT_CMGR) {
		msg_queue_free_and_pop(pvt);

		if (hfp_parse_cmgr(pvt->hfp, buf, &from_number, &text)
				|| msg_queue_push(pvt, AT_OK, AT_CMGR)) {

			ast_debug(1, "[%s] error parsing sms message, disconnecting\n", pvt->id);
			return -1;
		}

		/* XXX this channel probably does not need to be associated with this pvt */
		if (!(chan = mbl_new(AST_STATE_DOWN, pvt, NULL, NULL))) {
			ast_debug(1, "[%s] error creating sms message channel, disconnecting\n", pvt->id);
			return -1;
		}

		strcpy(chan->exten, "sms");
		pbx_builtin_setvar_helper(chan, "SMSSRC", from_number);
		pbx_builtin_setvar_helper(chan, "SMSTXT", text);

		if (ast_pbx_start(chan)) {
			ast_log(LOG_ERROR, "[%s] unable to start pbx on incoming sms\n", pvt->id);
			mbl_ast_hangup(pvt);
		}
	} else {
		ast_debug(1, "[%s] got unexpected +CMGR message, ignoring\n", pvt->id);
	}

	return 0;
}

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
		ast_debug(1, "[%s] error, got sms prompt but no pending sms messages\n", pvt->id);
		return 0;
	}

	if (hfp_send_sms_text(pvt->hfp, msg->data)
			|| msg_queue_push(pvt, AT_OK, AT_CMGS)) {
		msg_queue_free_and_pop(pvt);
		ast_debug(1, "[%s] error sending sms message\n", pvt->id);
		return 0;
	}

	msg_queue_free_and_pop(pvt);
	return 0;
}


static void *do_monitor_phone(void *data)
{
	struct mbl_pvt *pvt = (struct mbl_pvt *)data;
	struct hfp_pvt *hfp = pvt->hfp;
	char buf[256];
	int t;
	at_message_t at_msg;
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

		if (!rfcomm_wait(pvt->rfcomm_socket, &t)) {
			ast_debug(1, "[%s] timeout waiting for rfcomm data, disconnecting\n", pvt->id);
			ast_mutex_lock(&pvt->lock);
			if (!hfp->initialized) {
				if ((entry = msg_queue_head(pvt))) {
					switch (entry->response_to) {
					case AT_CIND_TEST:
						if (pvt->blackberry)
							ast_debug(1, "[%s] timeout during CIND test\n", hfp->owner->id);
						else
							ast_debug(1, "[%s] timeout during CIND test, try setting 'blackberry=yes'\n", hfp->owner->id);
						break;
					case AT_CMER:
						if (pvt->blackberry)
							ast_debug(1, "[%s] timeout after sending CMER, try setting 'blackberry=no'\n", hfp->owner->id);
						else
							ast_debug(1, "[%s] timeout after sending CMER\n", hfp->owner->id);
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

		if ((at_msg = at_read_full(hfp->rsock, buf, sizeof(buf))) < 0) {
			/* XXX gnu specific strerror_r is assummed here, this
			 * is not really safe.  See the strerror(3) man page
			 * for more info. */
			ast_debug(1, "[%s] error reading from device: %s (%d)\n", pvt->id, strerror_r(errno, buf, sizeof(buf)), errno);
			break;
		}

		ast_debug(1, "[%s] %s\n", pvt->id, buf);

		switch (at_msg) {
		case AT_BRSF:
			ast_mutex_lock(&pvt->lock);
			if (handle_response_brsf(pvt, buf)) {
				ast_mutex_unlock(&pvt->lock);
				goto e_cleanup;
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
		case AT_UNKNOWN:
			ast_debug(1, "[%s] ignoring unknown message: %s\n", pvt->id, buf);
			break;
		case AT_PARSE_ERROR:
			ast_debug(1, "[%s] error parsing message\n", pvt->id);
			goto e_cleanup;
		case AT_READ_ERROR:
			ast_debug(1, "[%s] error reading from device: %s (%d)\n", pvt->id, strerror_r(errno, buf, sizeof(buf)), errno);
			goto e_cleanup;
		default:
			break;
		}
	}

e_cleanup:

	if (!hfp->initialized)
		ast_verb(3, "Error initializing Bluetooth device %s.\n", pvt->id);

	ast_mutex_lock(&pvt->lock);
	if (pvt->owner) {
		ast_debug(1, "[%s] device disconnected, hanging up owner\n", pvt->id);
		pvt->needchup = 0;
		mbl_queue_hangup(pvt);
	}

	close(pvt->rfcomm_socket);
	close(pvt->sco_socket);
	pvt->sco_socket = -1;

	msg_queue_flush(pvt);

	pvt->connected = 0;
	hfp->initialized = 0;

	pvt->adapter->inuse = 0;
	ast_mutex_unlock(&pvt->lock);

	ast_verb(3, "Bluetooth Device %s has disconnected.\n", pvt->id);
	manager_event(EVENT_FLAG_SYSTEM, "MobileStatus", "Status: Disconnect\r\nDevice: %s\r\n", pvt->id);

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

static void *do_monitor_headset(void *data)
{

	struct mbl_pvt *pvt = (struct mbl_pvt *)data;
	char buf[256];
	int t;
	at_message_t at_msg;
	struct ast_channel *chan = NULL;

	ast_verb(3, "Bluetooth Device %s initialised and ready.\n", pvt->id);

	while (!check_unloading()) {

		t = ast_sched_wait(pvt->sched);
		if (t == -1) {
			t = 6000;
		}

		ast_sched_runq(pvt->sched);

		if (rfcomm_wait(pvt->rfcomm_socket, &t) == 0)
			continue;

		if ((at_msg = at_read_full(pvt->rfcomm_socket, buf, sizeof(buf))) < 0) {
			if (strerror_r(errno, buf, sizeof(buf)))
				ast_debug(1, "[%s] error reading from device\n", pvt->id);
			else
				ast_debug(1, "[%s] error reading from device: %s (%d)\n", pvt->id, buf, errno);

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
					if ((pvt->sco_socket = sco_connect(pvt->adapter->addr, pvt->addr)) == -1) {
						ast_log(LOG_ERROR, "[%s] unable to create audio connection\n", pvt->id);
						mbl_queue_hangup(pvt);
						ast_mutex_unlock(&pvt->lock);
						goto e_cleanup;
					}

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

				if ((pvt->sco_socket = sco_connect(pvt->adapter->addr, pvt->addr)) == -1) {
					ast_log(LOG_ERROR, "[%s] unable to create audio connection\n", pvt->id);
					ast_mutex_unlock(&pvt->lock);
					goto e_cleanup;
				}

				pvt->incoming = 1;

				if (!(chan = mbl_new(AST_STATE_UP, pvt, NULL, NULL))) {
					ast_log(LOG_ERROR, "[%s] unable to allocate channel for incoming call\n", pvt->id);
					ast_mutex_unlock(&pvt->lock);
					goto e_cleanup;
				}

				ast_channel_set_fd(chan, 0, pvt->sco_socket);

				ast_copy_string(chan->exten, "s", AST_MAX_EXTENSION);
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

	while (!check_unloading()) {
		AST_RWLIST_RDLOCK(&adapters);
		AST_RWLIST_TRAVERSE(&adapters, adapter, entry) {
			if (!adapter->inuse) {
				AST_RWLIST_RDLOCK(&devices);
				AST_RWLIST_TRAVERSE(&devices, pvt, entry) {
					ast_mutex_lock(&pvt->lock);
					if (!adapter->inuse && !pvt->connected && !strcmp(adapter->id, pvt->adapter->id)) {
						if ((pvt->rfcomm_socket = rfcomm_connect(adapter->addr, pvt->addr, pvt->rfcomm_port)) > -1) {
							if (start_monitor(pvt)) {
								pvt->connected = 1;
								adapter->inuse = 1;
								manager_event(EVENT_FLAG_SYSTEM, "MobileStatus", "Status: Connect\r\nDevice: %s\r\n", pvt->id);
								ast_verb(3, "Bluetooth Device %s has connected, initializing...\n", pvt->id);
							}
						}
					}
					ast_mutex_unlock(&pvt->lock);
				}
				AST_RWLIST_UNLOCK(&devices);
			}
		}
		AST_RWLIST_UNLOCK(&adapters);


		/* Go to sleep (only if we are not unloading) */
		if (!check_unloading())
			sleep(discovery_interval);
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
	int res;

	while (!check_unloading()) {
		/* check for new sco connections */
		if ((res = ast_io_wait(adapter->accept_io, 0)) == -1) {
			/* handle errors */
			ast_log(LOG_ERROR, "ast_io_wait() failed for adapter %s\n", adapter->id);
			break;
		}

		/* handle audio data */
		if ((res = ast_io_wait(adapter->io, 1)) == -1) {
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

	/* attempt to connect to the adapter */
	adapter->dev_id = hci_devid(address);
	adapter->hci_socket = hci_open_dev(adapter->dev_id);
	if (adapter->dev_id < 0 || adapter->hci_socket < 0) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Unable to communicate with adapter.\n", adapter->id);
		goto e_free_adapter;
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
		ast_log(LOG_ERROR, "Skipping adapter %s. Error binding audio connection listerner socket.\n", adapter->id);
		goto e_destroy_io;
	}

	/* add the socket to the io context */
	if (!(adapter->sco_id = ast_io_add(adapter->accept_io, adapter->sco_socket, sco_accept, AST_IO_IN, adapter))) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Error adding listener socket to I/O context.\n", adapter->id);
		goto e_close_sco;
	}

	/* start the sco listener for this adapter */
	if (ast_pthread_create_background(&adapter->sco_listener_thread, NULL, do_sco_listen, adapter)) {
		ast_log(LOG_ERROR, "Skipping adapter %s. Error creating audio connection listerner thread.\n", adapter->id);
		goto e_remove_sco;
	}

	/* add the adapter to our global list */
	AST_RWLIST_WRLOCK(&adapters);
	AST_RWLIST_INSERT_HEAD(&adapters, adapter, entry);
	AST_RWLIST_UNLOCK(&adapters);
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
e_free_adapter:
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
	struct mbl_pvt *pvt;
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
		if (!strcmp(adapter->id, adapter_str))
			break;
	}
	AST_RWLIST_UNLOCK(&adapters);
	if (!adapter) {
		ast_log(LOG_ERROR, "Skiping device %s. Unknown adapter '%s' specified.\n", cat, adapter_str);
		goto e_return;
	}

	address = ast_variable_retrieve(cfg, cat, "address");
	port = ast_variable_retrieve(cfg, cat, "port");
	if (ast_strlen_zero(port) || ast_strlen_zero(address)) {
		ast_log(LOG_ERROR, "Skipping device %s. Missing required port or address setting.\n", cat);
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

	/* populate the pvt structure */
	pvt->adapter = adapter;
	ast_copy_string(pvt->id, cat, sizeof(pvt->id));
	str2ba(address, &pvt->addr);
	pvt->timeout = -1;
	pvt->rfcomm_socket = -1;
	pvt->rfcomm_port = atoi(port);
	pvt->sco_socket = -1;
	pvt->monitor_thread = AST_PTHREADT_NULL;
	pvt->ring_sched_id = -1;

	/* setup the smoother */
	if (!(pvt->smoother = ast_smoother_new(DEVICE_FRAME_SIZE))) {
		ast_log(LOG_ERROR, "Skipping device %s. Error setting up frame smoother.\n", cat);
		goto e_free_pvt;
	}

	/* setup the dsp */
	if (!(pvt->dsp = ast_dsp_new())) {
		ast_log(LOG_ERROR, "Skipping device %s. Error setting up dsp for dtmf detection.\n", cat);
		goto e_free_smoother;
	}

	/* setup the scheduler */
	if (!(pvt->sched = sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create scheduler context for headset device\n");
		goto e_free_dsp;
	}

	ast_dsp_set_features(pvt->dsp, DSP_FEATURE_DIGIT_DETECT);
	ast_dsp_set_digitmode(pvt->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);

	for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
		if (!strcasecmp(v->name, "type")) {
			if (!strcasecmp(v->value, "headset"))
				pvt->type = MBL_TYPE_HEADSET;
			else
				pvt->type = MBL_TYPE_PHONE;
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(pvt->context, v->value, sizeof(pvt->context));
		} else if (!strcasecmp(v->name, "group")) {
			/* group is set to 0 if invalid */
			pvt->group = atoi(v->value);
		} else if (!strcasecmp(v->name, "nocallsetup")) {
			pvt->no_callsetup = ast_true(v->value);

			if (pvt->no_callsetup)
				ast_debug(1, "Setting nocallsetup mode for device %s.\n", pvt->id);
		} else if (!strcasecmp(v->name, "blackberry")) {
			pvt->blackberry = ast_true(v->value);
		}
	}

	if (pvt->type == MBL_TYPE_PHONE) {
		if (!(pvt->hfp = ast_calloc(1, sizeof(*pvt->hfp)))) {
			ast_log(LOG_ERROR, "Skipping device %s. Error allocating memory.\n", pvt->id);
			goto e_free_sched;
		}

		pvt->hfp->owner = pvt;
		pvt->hfp->rport = pvt->rfcomm_port;
		pvt->hfp->nocallsetup = pvt->no_callsetup;
	}

	AST_RWLIST_WRLOCK(&devices);
	AST_RWLIST_INSERT_HEAD(&devices, pvt, entry);
	AST_RWLIST_UNLOCK(&devices);
	ast_debug(1, "Loaded device %s.\n", pvt->id);

	return pvt;

e_free_sched:
	sched_context_destroy(pvt->sched);
e_free_dsp:
	ast_dsp_free(pvt->dsp);
e_free_smoother:
	ast_smoother_free(pvt->smoother);
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
	if (!cfg)
		return -1;

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
		if (strcasecmp(cat, "general") && strcasecmp(cat, "adapter")) {
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

	/* Unregister the CLI & APP */
	ast_cli_unregister_multiple(mbl_cli, sizeof(mbl_cli) / sizeof(mbl_cli[0]));
	ast_unregister_application(app_mblstatus);
	ast_unregister_application(app_mblsendsms);

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

		ast_smoother_free(pvt->smoother);
		ast_dsp_free(pvt->dsp);
		sched_context_destroy(pvt->sched);
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

	if (sdp_session)
		sdp_close(sdp_session);

	return 0;
}

static int load_module(void)
{

	int dev_id, s;

	/* Check if we have Bluetooth, no point loading otherwise... */
	dev_id = hci_get_route(NULL);
	s = hci_open_dev(dev_id);
	if (dev_id < 0 || s < 0) {
		ast_log(LOG_ERROR, "No Bluetooth devices found. Not loading module.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	hci_close_dev(s);

	if (mbl_load_config()) {
		ast_log(LOG_ERROR, "Errors reading config file %s. Not loading module.\n", MBL_CONFIG);
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
	ast_register_application(app_mblstatus, mbl_status_exec, mblstatus_synopsis, mblstatus_desc);
	ast_register_application(app_mblsendsms, mbl_sendsms_exec, mblsendsms_synopsis, mblsendsms_desc);

	return AST_MODULE_LOAD_SUCCESS;

e_cleanup:
	if (sdp_session)
		sdp_close(sdp_session);

	return AST_MODULE_LOAD_FAILURE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Bluetooth Mobile Device Channel Driver",
		.load = load_module,
		.unload = unload_module,
);
