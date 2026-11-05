/*
 * chan_ed137 -- ED-137 radio channel driver (VCS side).
 *
 * Every radio listed in ed137.conf is a persistent SIP session: INVITE to the
 * radio, BYE on shutdown, retransmission and backoff, and the R2S keepalive
 * (payload type 123) while the session is up. The optional WG67KEY-IN
 * subscription reports the radio state (NOTIFY). Audio is PCMA over RTP with
 * the ED-137 extension header (profile 0x0167); the PTT state is carried in
 * that header.
 *
 * Dial(ED137/<radio>) attaches a channel to a radio session and listens to it
 * (fan-out to all listeners). The channel that owns the PTT transmits; the
 * owner is set by the ED137Key() application or the ED137Key AMI action.
 *
 * Base: channels/chan_audiosocket.c (Asterisk 20 and 22 channel API).
 *
 * \author Andrey Devyatkin aka RedCat
 */

/*** MODULEINFO
	<support_level>extended</support_level>
***/

#include "asterisk.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "asterisk/app.h"
#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_cap.h"
#include "asterisk/frame.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/manager.h"
#include "asterisk/devicestate.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/utils.h"

#define ED137_TECH_TYPE "ED137"
#define ED137_CONFIG_FILE "ed137.conf"
#define ED137_MAX_RADIOS 256
#define ED137_SIP_MAX 4096
#define ED137_SUB_RETX_MAX 10
#define ED137_FRAME_BYTES 160		/* G.711 A-law, 20 ms at 8 kHz */
#define ED137_FRAME_MS 20
#define ED137_JB_SLOTS 32		/* jitter buffer window: 640 ms */
#define ED137_JB_DELAY_MS 60		/* playout delay before the first frame */
#define ED137_RX_IDLE_MS 200		/* no audio for this long: RX closed */
#define ED137_PVT_QUEUE 16		/* frames queued per channel, oldest dropped */
#define ED137_ALAW_SILENCE 0xD5
#define ED137_TX_QUEUE 8
#define ED137_WG67_NO 0
#define ED137_WG67_YES 1
#define ED137_WG67_AUTO 2
#define ED137_TX_MAX_LAG_MS 200
#define ED137_RX_MUTE_TAIL_MS 500	/* RX to listeners stays muted this long after our PTT release */
#define ED137_RX_OPEN_PKTS 3		/* ED137RX open after this many packets: the first one has a transitional ext */

enum ed137_mode {
	ED137_MODE_IDLE = 0,
	ED137_MODE_RXONLY,
	ED137_MODE_TXRX,
};

enum ed137_ptt_repeat {
	ED137_PTT_REPEAT_IGNORE = 0,	/* repeat from the owner is dropped silently */
	ED137_PTT_REPEAT_ACK,		/* repeat from the owner is confirmed with ED137PTT */
};

enum ed137_key_result {
	ED137_KEY_OK = 0,
	ED137_KEY_BUSY,			/* another owner holds PTT */
	ED137_KEY_NOSESSION,		/* radio session is not up */
	ED137_KEY_NOTFOUND,		/* no such radio */
	ED137_KEY_NOTOWNER,		/* release requested by a non-owner */
	ED137_KEY_NOCHANNEL,		/* ED137 owner name is not a live channel of this radio */
};

enum ed137_sub_state {
	ED137_SUB_IDLE = 0,
	ED137_SUB_WAIT,			/* SUBSCRIBE sent, no answer yet */
	ED137_SUB_ACTIVE,		/* 2xx received, refresh scheduled */
};

enum ed137_inv_state {
	ED137_INV_IDLE = 0,
	ED137_INV_WAIT,			/* INVITE sent, retransmitted until answer or timeout */
	ED137_INV_BACKOFF,		/* no session, retry at due_ms */
	ED137_INV_UP,			/* 200 OK + ACK, keepalive running */
};

struct ed137_pvt;

struct ed137_general {
	char bindaddr[64];
	int bindport;
	char vcs_host[64];		/* IPv4 address radios see us at (Via, Contact, SDP) */
	char vcs_uri[160];		/* From URI, default sip:vcs@<vcs_host> */
	int expires;
	int invite_timeout;
};

struct ed137_dialog {
	char call_id[96];
	char local_tag[32];
	char remote_tag[48];
	char remote_uri[160];		/* Contact of the radio, used as Request-URI */
	unsigned int cseq;
	int state;
	int retries;			/* retransmits of the current request */
	int fails;			/* failed attempts, drives the INVITE backoff */
	int64_t due_ms;
	int64_t started_ms;
	size_t last_len;
	char last_msg[ED137_SIP_MAX];	/* retransmitted as is */
};

struct ed137_radio {
	char name[64];
	char ip[64];
	char uri[160];			/* SIP user of the radio, required in ed137.conf */
	int sip_port;
	int radio_rtp_port;		/* default radio media port, used until SDP says otherwise */
	int local_rtp_port;		/* our RTP port for this radio; 0 = any (firewall: set it) */
	int keepalive_ms;
	int subscribe;
	enum ed137_mode mode;
	enum ed137_ptt_repeat ptt_repeat;

	ast_mutex_t lock;		/* protects every runtime field below */
	struct ed137_dialog sub;	/* WG67KEY-IN subscription */
	struct ed137_dialog inv;	/* radio session */
	unsigned int notify_cseq;	/* highest NOTIFY CSeq seen on the subscription */
	int notify_reject;		/* notify_cseq = reject: a NOTIFY CSeq not above it gets 500 */
	char sub_state_seen[32];	/* radio's NOTIFY state for our VCS line */
	int rtp_fd;			/* keepalive socket: its port is the SDP m= port */
	int rtp_port;
	int peer_rtp_port;		/* radio media port from the INVITE 200 OK, 0 if none */
	int64_t ka_next_ms;
	uint16_t ka_seq;
	uint32_t ka_ts;
	uint32_t ka_ssrc;
	uint32_t ka_rx_word;		/* last extension word received from the radio */
	int ka_rx_seen;
	unsigned long long sdp_ver;	/* SDP origin version, +1 per re-INVITE */
	int reinv_pending;		/* re-INVITE sent, 200 OK awaited */
	int reinv_retx;
	int64_t reinv_due_ms;
	int64_t reinv_started_ms;
	uint32_t rx_ext;		/* last ext word0 of the RX stream (audio packet or end marker) */
	int rx_pkts_in_tx;		/* packets accepted in the current transmission */
	int64_t rx_close_at_ms;	/* end marker seen: RX closes then, after the jitter buffer drains */

	int ptt_keyed;
	char ptt_owner[128];
	char invite_extra[512];	/* extra INVITE headers: WG67-Version only with R2S */
	int rx_squelch;		/* rx_detect = squelch: RX follows the SQU bit of the extension */
	int rx_squ;			/* last SQU bit seen on the RX stream */
	int rx_sqi_valid;		/* last RX packet carried a Signal Quality feature */
	int rx_sqi;			/* last SQI index (0..31) */
	int wg67_mode;			/* ED137_WG67_NO, _YES or _AUTO (the wg67 setting) */
	int subscribe_auto;		/* subscribe = auto: off after a rejection, probed again at each session */
	int wg67_probe;			/* 2016 answers seen while probing the WG67 form */
	int wg67;			/* WG67-Version header (effective state) */
	int r2s;			/* WG-67 R2S: KEY-IN subscription, keepalive, liveness; a separate switch */
	int liveness_ms;		/* no packet from the radio for this long: the session is down */
	int64_t rx_seen_ms;
	enum ast_device_state devstate;	/* last state published as ED137:<radio> */
	int session_grace_s;		/* calls stay up this long when the session drops; 0 ends them at once */
	int64_t down_since_ms;		/* when the session went down (0 when up) */
	int tx_idle_s;			/* PTT is released when the owner's audio stops for this long */
	int64_t tx_audio_ms;
	struct ed137_pvt *ptt_keyer;	/* ED137 channel that took PTT; NULL for CLI or another channel */
	int rx_mute_on_tx;		/* no RX to listeners while our PTT is held and for a short tail */
	int64_t rx_mute_until_ms;
	int ptt_id;			/* PTT-id 1..63, header bits 11..6; written in TxRx mode (1 = 0x00400000 in the dump) */
	int sqi_reported;
	int64_t sqi_event_ms;
	int ptt_max_s;			/* PTT is released after this many seconds */
	int64_t ptt_started_ms;
	int tx_active;			/* PTT held or final packets pending: TX packets every 20 ms */
	int tx_final;			/* packets still to send with the PTT bit cleared after release */
	int64_t tx_next_ms;
	uint8_t tx_q[ED137_TX_QUEUE][ED137_FRAME_BYTES];
	int tx_head;
	int tx_count;
	unsigned long tx_frames;	/* TX packets sent */

	/* RX: one audio stream from the radio, jitter buffer, fan-out to listening channels */
	int rx_active;			/* audio seen within ED137_RX_IDLE_MS: ED137RX open */
	int rx_playing;
	uint16_t rx_next_seq;
	int64_t rx_next_play_ms;
	int64_t rx_last_ms;
	uint8_t rx_slot[ED137_JB_SLOTS][ED137_FRAME_BYTES];
	int rx_slot_valid[ED137_JB_SLOTS];
	uint16_t rx_slot_seq[ED137_JB_SLOTS];
	unsigned long rx_frames;	/* audio packets accepted */
	unsigned long rx_plc;		/* playout slots filled with silence */
	AST_LIST_HEAD_NOLOCK(, ed137_pvt) listeners;
	int listeners_count;

	AST_RWLIST_ENTRY(ed137_radio) entry;
};

struct ed137_pvt {
	struct ed137_radio *radio;
	struct ast_format *fmt;		/* alaw, reference owned by the pvt */
	char peer[128];			/* name of the requesting channel (Dial caller), for ED137CALL */
	int pipe_rd;			/* channel fd: readable while RX frames are queued */
	int pipe_wr;
	int attached;			/* on radio->listeners */
	int session_lost;		/* radio session ended: read() returns NULL, core hangs up */
	uint8_t q[ED137_PVT_QUEUE][ED137_FRAME_BYTES];
	int q_head;
	int q_count;
	struct ast_frame fr;
	uint8_t frbuf[AST_FRIENDLY_OFFSET + ED137_FRAME_BYTES];
	AST_LIST_ENTRY(ed137_pvt) entry;
};

static struct ed137_general ed137_general_cfg;
static AST_RWLIST_HEAD_STATIC(ed137_radios, ed137_radio);
static int ed137_radio_count;

/* ED137 channels alive: request() +1, hangup() -1. */
static int active_channels;
static int ed137_chan_seq;	/* suffix that makes channel names unique */

/* SIP socket and thread. */
static int ed137_sip_fd = -1;
static pthread_t ed137_thread;
static int ed137_running;

static const int64_t ed137_backoff_ms[] = { 5000, 10000, 30000 };

static struct ast_channel *ed137_request(const char *type,
	struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, int *cause);
static int ed137_call(struct ast_channel *ast, const char *dest, int timeout);
static int ed137_hangup(struct ast_channel *ast);
static struct ast_frame *ed137_read(struct ast_channel *ast);
static int ed137_write(struct ast_channel *ast, struct ast_frame *f);
static int ed137_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);

static struct ast_channel_tech ed137_channel_tech = {
	.type = ED137_TECH_TYPE,
	.description = "ED-137 Radio Channel Driver (VCS role)",
	.requester = ed137_request,
	.call = ed137_call,
	.hangup = ed137_hangup,
	.read = ed137_read,
	.write = ed137_write,
	.indicate = ed137_indicate,
};

/*! \brief ED137CALL: an ED137 channel attached to or detached from a radio. */
static void ed137_call_event(struct ed137_radio *r, struct ast_channel *ast, struct ed137_pvt *pvt,
	const char *state)
{
	manager_event(EVENT_FLAG_CALL, "ED137CALL",
		"Radio: %s\r\nState: %s\r\nChannel: %s\r\nPeer: %s\r\nLinkedid: %s\r\n",
		r->name, state, ast_channel_name(ast), pvt->peer, ast_channel_linkedid(ast));
}

/* ---------------- time and small helpers ---------------- */

static int64_t ed137_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *ed137_mode_name(enum ed137_mode mode)
{
	switch (mode) {
	case ED137_MODE_IDLE:
		return "idle";
	case ED137_MODE_RXONLY:
		return "rxonly";
	case ED137_MODE_TXRX:
		return "txrx";
	}
	return "?";
}

static const char *ed137_mode_sdp_type(enum ed137_mode mode)
{
	switch (mode) {
	case ED137_MODE_IDLE:
		return "Radio-Idle";
	case ED137_MODE_RXONLY:
		return "Radio-Rx";
	case ED137_MODE_TXRX:
		return "Radio-TxRx";
	}
	return "Radio-Idle";
}

static const char *ed137_repeat_name(enum ed137_ptt_repeat repeat)
{
	switch (repeat) {
	case ED137_PTT_REPEAT_IGNORE:
		return "ignore";
	case ED137_PTT_REPEAT_ACK:
		return "ack";
	}
	return "?";
}

static const char *ed137_sub_name(int state)
{
	switch (state) {
	case ED137_SUB_IDLE:
		return "idle";
	case ED137_SUB_WAIT:
		return "wait";
	case ED137_SUB_ACTIVE:
		return "active";
	}
	return "?";
}

static const char *ed137_inv_name(int state)
{
	switch (state) {
	case ED137_INV_IDLE:
		return "idle";
	case ED137_INV_WAIT:
		return "wait";
	case ED137_INV_BACKOFF:
		return "backoff";
	case ED137_INV_UP:
		return "up";
	}
	return "?";
}

static const char *ed137_key_status_name(enum ed137_key_result res)
{
	switch (res) {
	case ED137_KEY_OK:
		return "OK";
	case ED137_KEY_BUSY:
		return "BUSY";
	case ED137_KEY_NOSESSION:
		return "NOSESSION";
	case ED137_KEY_NOTFOUND:
		return "NOTFOUND";
	case ED137_KEY_NOCHANNEL:
		return "NOCHANNEL";
	case ED137_KEY_NOTOWNER:
		return "NOTOWNER";
	}
	return "UNKNOWN";
}

/* ---------------- radio list ---------------- */

static struct ed137_radio *ed137_radio_find(const char *name)
{
	struct ed137_radio *r;

	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		if (!strcmp(r->name, name)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&ed137_radios);
	return r;
}

static struct ed137_radio *ed137_radio_by_callid(const char *call_id)
{
	struct ed137_radio *r;

	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		if (r->sub.call_id[0] && !strcmp(r->sub.call_id, call_id)) {
			break;
		}
		if (r->inv.call_id[0] && !strcmp(r->inv.call_id, call_id)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&ed137_radios);
	return r;
}

static void ed137_close_radio(struct ed137_radio *r)
{
	if (r->rtp_fd >= 0) {
		close(r->rtp_fd);
		r->rtp_fd = -1;
	}
	ast_mutex_destroy(&r->lock);
	ast_free(r);
}

static void ed137_free_radios(void)
{
	struct ed137_radio *r;

	AST_RWLIST_WRLOCK(&ed137_radios);
	while ((r = AST_RWLIST_REMOVE_HEAD(&ed137_radios, entry))) {
		ed137_close_radio(r);
	}
	ed137_radio_count = 0;
	AST_RWLIST_UNLOCK(&ed137_radios);
}

/* ---------------- ed137.conf ---------------- */

/*! \brief Read an integer key with a range check. Returns 0 on success, 1 on error. */
static int ed137_get_int(struct ast_config *cfg, const char *cat, const char *key,
	int def, int min, int max, int *out)
{
	const char *v = ast_variable_retrieve(cfg, cat, key);
	int val;

	if (ast_strlen_zero(v)) {
		*out = def;
		return 0;
	}
	if (sscanf(v, "%d", &val) != 1 || val < min || val > max) {
		ast_log(LOG_ERROR, "ed137.conf [%s]: '%s = %s' is not in range %d..%d\n",
			cat, key, v, min, max);
		return 1;
	}
	*out = val;
	return 0;
}

/*! \brief yes/no (or auto, yes, no) for ed137.conf. An empty value gives def; anything else is an error. */
static int ed137_conf_bool(const char *cat, const char *key, const char *v, int def, int *out, const char *allowed)
{
	if (ast_strlen_zero(v)) {
		*out = def;
		return 0;
	}
	if (!strcasecmp(v, "yes") || !strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcmp(v, "1")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(v, "no") || !strcasecmp(v, "off") || !strcasecmp(v, "false") || !strcmp(v, "0")) {
		*out = 0;
		return 0;
	}
	ast_log(LOG_ERROR, "ed137.conf [%s]: %s '%s' must be %s\n", cat, key, v, allowed);
	*out = def;
	return 1;
}

static const char *const ed137_general_keys[] = {
	"bindaddr", "bindport", "vcs_host", "vcs_uri", "expires", "invite_timeout", NULL
};

static const char *const ed137_radio_keys[] = {
	"ip", "sip_port", "uri", "radio_rtp_port", "local_rtp_port", "keepalive_ms", "mode", "subscribe",
	"ptt_repeat", "notify_cseq", "rx_mute_on_tx", "ptt_max_s", "ptt_id", "rx_detect", "r2s", "wg67",
	"liveness_ms", "tx_idle_s", "session_grace_s", NULL
};

static int ed137_key_known(const char *const *known, const char *name)
{
	int i;

	for (i = 0; known[i]; i++) {
		if (!strcasecmp(known[i], name)) {
			return 1;
		}
	}
	return 0;
}

/*! \brief Unknown options are errors: a typo must not silently change the behaviour. */
static int ed137_check_keys(struct ast_config *cfg, const char *cat, const char *const *known)
{
	struct ast_variable *var;
	int errors = 0;

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (ed137_key_known(known, var->name)) {
			continue;
		}
		if (!strcasecmp(var->name, "ed137_version")) {
			ast_log(LOG_ERROR, "ed137.conf [%s]: ed137_version was removed, delete this line (wg67 and r2s select the behaviour)\n", cat);
		} else {
			ast_log(LOG_ERROR, "ed137.conf [%s]: unknown option '%s'\n", cat, var->name);
		}
		errors++;
	}
	return errors;
}

/*! \brief Device state of ED137:<radio>. Lock held. */
static enum ast_device_state ed137_device_state_locked(struct ed137_radio *r)
{
	if (r->inv.state != ED137_INV_UP) {
		return AST_DEVICE_UNAVAILABLE;
	}
	if (r->ptt_keyed) {
		return AST_DEVICE_BUSY;
	}
	if (!AST_LIST_EMPTY(&r->listeners)) {
		return AST_DEVICE_INUSE;
	}
	return AST_DEVICE_NOT_INUSE;
}

static const char *ed137_devstate_text(enum ast_device_state st)
{
	switch (st) {
	case AST_DEVICE_UNAVAILABLE:
		return "UNAVAILABLE";
	case AST_DEVICE_BUSY:
		return "BUSY";
	case AST_DEVICE_INUSE:
		return "INUSE";
	case AST_DEVICE_NOT_INUSE:
		return "NOT_INUSE";
	default:
		return "UNKNOWN";
	}
}

/*! \brief Publish ED137:<radio> when its state changed. Lock held. */
static void ed137_devstate_update_locked(struct ed137_radio *r)
{
	enum ast_device_state st = ed137_device_state_locked(r);

	if (st == r->devstate) {
		return;
	}
	r->devstate = st;
	ast_devstate_changed(st, AST_DEVSTATE_CACHABLE, "ED137:%s", r->name);
}

static int ed137_parse_general(struct ast_config *cfg, const char *cat)
{
	const char *v;
	struct in_addr addr;
	int errors = 0;

	errors += ed137_check_keys(cfg, cat, ed137_general_keys);

	v = ast_variable_retrieve(cfg, cat, "bindaddr");
	if (!ast_strlen_zero(v)) {
		if (inet_pton(AF_INET, v, &addr) != 1) {
			ast_log(LOG_ERROR, "ed137.conf [general]: bindaddr '%s' is not an IPv4 address\n", v);
			errors++;
		} else {
			ast_copy_string(ed137_general_cfg.bindaddr, v, sizeof(ed137_general_cfg.bindaddr));
		}
	}

	v = ast_variable_retrieve(cfg, cat, "vcs_host");
	if (ast_strlen_zero(v)) {
		ast_log(LOG_ERROR, "ed137.conf [general]: 'vcs_host' is required (IPv4 address radios see us at)\n");
		errors++;
	} else if (inet_pton(AF_INET, v, &addr) != 1) {
		ast_log(LOG_ERROR, "ed137.conf [general]: vcs_host '%s' is not an IPv4 address\n", v);
		errors++;
	} else {
		ast_copy_string(ed137_general_cfg.vcs_host, v, sizeof(ed137_general_cfg.vcs_host));
	}

	v = ast_variable_retrieve(cfg, cat, "vcs_uri");
	if (!ast_strlen_zero(v)) {
		ast_copy_string(ed137_general_cfg.vcs_uri, v, sizeof(ed137_general_cfg.vcs_uri));
	}

	errors += ed137_get_int(cfg, cat, "bindport", 5070, 1, 65535, &ed137_general_cfg.bindport);
	errors += ed137_get_int(cfg, cat, "expires", 30, 30, 3600, &ed137_general_cfg.expires);
	errors += ed137_get_int(cfg, cat, "invite_timeout", 5, 1, 60, &ed137_general_cfg.invite_timeout);
	return errors;
}

static void ed137_build_invite_extra(struct ed137_radio *r)
{
	snprintf(r->invite_extra, sizeof(r->invite_extra),
		"Subject: radio\r\nPriority: normal\r\nRecv-Info:\r\n%s"
		"Allow: PRACK, INVITE, ACK, BYE, CANCEL, UPDATE, SUBSCRIBE, NOTIFY, REFER, MESSAGE, OPTIONS\r\n"
		"Supported: replaces, 100rel, timer, norefersub\r\n"
		"User-Agent: chan_ed137\r\n",
		r->wg67 ? "WG67-Version: radio.01, radio.02\r\n" : "");
}

static int ed137_parse_radio(struct ast_config *cfg, const char *cat)
{
	struct ed137_radio *r;
	struct in_addr addr;
	const char *v;
	int errors = 0;

	errors += ed137_check_keys(cfg, cat, ed137_radio_keys);
	if (ed137_radio_find(cat)) {
		ast_log(LOG_ERROR, "ed137.conf: duplicate radio [%s]\n", cat);
		return 1;
	}
	if (ed137_radio_count >= ED137_MAX_RADIOS) {
		ast_log(LOG_ERROR, "ed137.conf: more than %d radios\n", ED137_MAX_RADIOS);
		return 1;
	}
	r = ast_calloc(1, sizeof(*r));
	if (!r) {
		return 1;
	}
	r->rtp_fd = -1;
	ast_copy_string(r->name, cat, sizeof(r->name));
	ast_mutex_init(&r->lock);

	v = ast_variable_retrieve(cfg, cat, "ip");
	if (ast_strlen_zero(v)) {
		ast_log(LOG_ERROR, "ed137.conf [%s]: 'ip' is required\n", cat);
		errors++;
	} else if (inet_pton(AF_INET, v, &addr) != 1) {
		ast_log(LOG_ERROR, "ed137.conf [%s]: ip '%s' is not an IPv4 address\n", cat, v);
		errors++;
	} else {
		ast_copy_string(r->ip, v, sizeof(r->ip));
	}

	v = ast_variable_retrieve(cfg, cat, "uri");
	if (ast_strlen_zero(v)) {
		ast_log(LOG_ERROR, "ed137.conf [%s]: 'uri' is required, e.g. sip:<user>@<ip>\n", cat);
		errors++;
	} else {
		ast_copy_string(r->uri, v, sizeof(r->uri));
	}

	errors += ed137_get_int(cfg, cat, "sip_port", 5060, 1, 65535, &r->sip_port);
	errors += ed137_get_int(cfg, cat, "radio_rtp_port", 5002, 1, 65535, &r->radio_rtp_port);
	errors += ed137_get_int(cfg, cat, "local_rtp_port", 0, 0, 65535, &r->local_rtp_port);
	if (!r->local_rtp_port) {
		ast_log(LOG_ERROR, "ed137.conf [%s]: 'local_rtp_port' is required\n", cat);
		errors++;
	}
	errors += ed137_get_int(cfg, cat, "ptt_max_s", 60, 1, 600, &r->ptt_max_s);
	errors += ed137_get_int(cfg, cat, "ptt_id", 1, 1, 63, &r->ptt_id);
	v = ast_variable_retrieve(cfg, cat, "rx_mute_on_tx");
	errors += ed137_conf_bool(cat, "rx_mute_on_tx", v, 1, &r->rx_mute_on_tx, "yes or no");
	v = ast_variable_retrieve(cfg, cat, "r2s");
	errors += ed137_conf_bool(cat, "r2s", v, 1, &r->r2s, "yes or no");
	v = ast_variable_retrieve(cfg, cat, "rx_detect");
	if (ast_strlen_zero(v) || !strcasecmp(v, "audio")) {
		r->rx_squelch = 0;
	} else if (!strcasecmp(v, "squelch")) {
		r->rx_squelch = 1;
	} else {
		ast_log(LOG_ERROR, "ED137: radio %s: rx_detect '%s' must be audio or squelch\n", cat, v);
		errors++;
	}
	r->sqi_reported = -1;
	v = ast_variable_retrieve(cfg, cat, "wg67");
	if (ast_strlen_zero(v) || !strcasecmp(v, "auto")) {
		r->wg67_mode = ED137_WG67_AUTO;
		r->wg67 = 1;
	} else {
		int on = 0;

		errors += ed137_conf_bool(cat, "wg67", v, 1, &on, "auto, yes or no");
		r->wg67_mode = on ? ED137_WG67_YES : ED137_WG67_NO;
		r->wg67 = on;
	}
	ed137_build_invite_extra(r);
	errors += ed137_get_int(cfg, cat, "liveness_ms", 3000, 1000, 30000, &r->liveness_ms);
	errors += ed137_get_int(cfg, cat, "tx_idle_s", 2, 1, 60, &r->tx_idle_s);
	errors += ed137_get_int(cfg, cat, "session_grace_s", 0, 0, 120, &r->session_grace_s);
	errors += ed137_get_int(cfg, cat, "keepalive_ms", 200, 50, 5000, &r->keepalive_ms);

	v = ast_variable_retrieve(cfg, cat, "mode");
	if (!ast_strlen_zero(v)) {
		if (!strcasecmp(v, "idle")) {
			r->mode = ED137_MODE_IDLE;
		} else if (!strcasecmp(v, "rxonly")) {
			r->mode = ED137_MODE_RXONLY;
		} else if (!strcasecmp(v, "txrx")) {
			r->mode = ED137_MODE_TXRX;
		} else {
			ast_log(LOG_ERROR, "ed137.conf [%s]: mode '%s' must be idle, rxonly or txrx\n", cat, v);
			errors++;
		}
	}

	v = ast_variable_retrieve(cfg, cat, "ptt_repeat");
	if (!ast_strlen_zero(v)) {
		if (!strcasecmp(v, "ignore")) {
			r->ptt_repeat = ED137_PTT_REPEAT_IGNORE;
		} else if (!strcasecmp(v, "ack")) {
			r->ptt_repeat = ED137_PTT_REPEAT_ACK;
		} else {
			ast_log(LOG_ERROR, "ed137.conf [%s]: ptt_repeat '%s' must be ignore or ack\n", cat, v);
			errors++;
		}
	}

	v = ast_variable_retrieve(cfg, cat, "subscribe");
	if (ast_strlen_zero(v) || !strcasecmp(v, "auto")) {
		r->subscribe = 1;
		r->subscribe_auto = 1;
	} else {
		errors += ed137_conf_bool(cat, "subscribe", v, 1, &r->subscribe, "auto, yes or no");
		r->subscribe_auto = 0;
	}

	v = ast_variable_retrieve(cfg, cat, "notify_cseq");
	if (!ast_strlen_zero(v)) {
		if (!strcasecmp(v, "accept")) {
			r->notify_reject = 0;
		} else if (!strcasecmp(v, "reject")) {
			r->notify_reject = 1;
		} else {
			ast_log(LOG_ERROR, "ed137.conf [%s]: notify_cseq '%s' must be accept or reject\n", cat, v);
			errors++;
		}
	}

	if (errors) {
		ed137_close_radio(r);
		return 1;
	}
	ed137_radio_count++;

	AST_RWLIST_WRLOCK(&ed137_radios);
	AST_RWLIST_INSERT_TAIL(&ed137_radios, r, entry);
	AST_RWLIST_UNLOCK(&ed137_radios);
	return 0;
}

/*! \brief Load ed137.conf. A missing file is not an error: no radios configured. */
static int ed137_load_config(void)
{
	struct ast_config *cfg;
	struct ast_flags flags = { 0 };
	const char *cat = NULL;
	int errors = 0;

	ast_copy_string(ed137_general_cfg.bindaddr, "0.0.0.0", sizeof(ed137_general_cfg.bindaddr));
	ed137_general_cfg.bindport = 5070;
	ed137_general_cfg.expires = 30;
	ed137_general_cfg.invite_timeout = 5;

	cfg = ast_config_load2(ED137_CONFIG_FILE, "chan_ed137", flags);
	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "ed137.conf: invalid configuration\n");
		return 1;
	}
	if (!cfg) {
		/* CONFIG_STATUS_FILEMISSING is NULL in this API. */
		ast_log(LOG_WARNING, "ed137.conf not found: no ED-137 radios configured\n");
		return 0;
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			errors += ed137_parse_general(cfg, cat);
		} else {
			errors += ed137_parse_radio(cfg, cat);
		}
	}
	ast_config_destroy(cfg);

	if (ed137_general_cfg.vcs_uri[0] == '\0' && ed137_general_cfg.vcs_host[0]) {
		snprintf(ed137_general_cfg.vcs_uri, sizeof(ed137_general_cfg.vcs_uri),
			"sip:vcs@%s", ed137_general_cfg.vcs_host);
	}
	if (ed137_radio_count && ed137_general_cfg.vcs_host[0] == '\0') {
		ast_log(LOG_ERROR, "ed137.conf: radios are configured but [general] vcs_host is missing\n");
		errors++;
	}
	return errors ? 1 : 0;
}

/* ---------------- SIP and RTP helpers ---------------- */

/*! \brief Append formatted text at offset len. Returns the new length. */
static size_t ed137_addf(char *buf, size_t size, size_t len, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (len >= size) {
		return len;
	}
	va_start(ap, fmt);
	n = vsnprintf(buf + len, size - len, fmt, ap);
	va_end(ap);
	if (n < 0) {
		return len;
	}
	if ((size_t)n >= size - len) {
		return size - 1;
	}
	return len + (size_t)n;
}

/*! \brief Copy the value of header 'name' (case-insensitive) into buf. Empty if absent. */
static const char *ed137_sip_hdr(const char *msg, const char *name, char *buf, size_t size)
{
	const char *p = msg;
	size_t nlen = strlen(name);

	buf[0] = '\0';
	while (p && *p) {
		const char *eol = strstr(p, "\r\n");
		size_t ll = eol ? (size_t)(eol - p) : strlen(p);
		size_t vlen;
		const char *v;

		if (ll == 0) {
			break;
		}
		if (ll > nlen && !strncasecmp(p, name, nlen) && p[nlen] == ':') {
			v = p + nlen + 1;
			vlen = ll - nlen - 1;
			while (vlen && (*v == ' ' || *v == '\t')) {
				v++;
				vlen--;
			}
			if (vlen >= size) {
				vlen = size - 1;
			}
			memcpy(buf, v, vlen);
			buf[vlen] = '\0';
			return buf;
		}
		if (!eol) {
			break;
		}
		p = eol + 2;
	}
	return buf;
}

/*! \brief Tag parameter of a From/To value. */
static void ed137_sip_tag(const char *hdr, char *tag, size_t size)
{
	const char *p = strstr(hdr, ";tag=");
	size_t n = 0;

	tag[0] = '\0';
	if (!p) {
		return;
	}
	p += 5;
	while (p[n] && p[n] != ';' && p[n] != '>' && p[n] != ' ' && p[n] != '\r' && n + 1 < size) {
		n++;
	}
	memcpy(tag, p, n);
	tag[n] = '\0';
}

/*! \brief URI inside <...>, or the whole value without parameters. */
static void ed137_sip_uri(const char *hdr, char *uri, size_t size)
{
	const char *lt = strchr(hdr, '<');
	const char *gt;
	size_t n;

	uri[0] = '\0';
	if (lt) {
		gt = strchr(lt, '>');
		if (!gt) {
			return;
		}
		n = (size_t)(gt - lt - 1);
		if (n >= size) {
			n = size - 1;
		}
		memcpy(uri, lt + 1, n);
		uri[n] = '\0';
		return;
	}
	n = strcspn(hdr, "; \r\n");
	if (n >= size) {
		n = size - 1;
	}
	memcpy(uri, hdr, n);
	uri[n] = '\0';
}

/*! \brief Status code of a response, or -1 for a request. */
static int ed137_sip_status(const char *msg)
{
	int code;

	if (strncmp(msg, "SIP/2.0 ", 8) != 0) {
		return -1;
	}
	if (sscanf(msg + 8, "%d", &code) != 1) {
		return -1;
	}
	return code;
}

/*! \brief Method of a request (first word) or of the CSeq of a response. */
static void ed137_sip_method(const char *msg, char *method, size_t size)
{
	char cseq[64];
	const char *sp;
	size_t n;

	if (ed137_sip_status(msg) < 0) {
		n = strcspn(msg, " ");
		if (n >= size) {
			n = size - 1;
		}
		memcpy(method, msg, n);
		method[n] = '\0';
		return;
	}
	ed137_sip_hdr(msg, "CSeq", cseq, sizeof(cseq));
	sp = strchr(cseq, ' ');
	if (!sp) {
		method[0] = '\0';
		return;
	}
	ast_copy_string(method, sp + 1, size);
}

static const char *ed137_sip_body(const char *msg)
{
	const char *b = strstr(msg, "\r\n\r\n");

	return b ? b + 4 : "";
}

/*! \brief Media port of the first m=audio line, 0 if none. */
static int ed137_sdp_audio_port(const char *sdp)
{
	const char *p = strstr(sdp, "m=audio ");
	int port = 0;

	if (p && sscanf(p + 8, "%d", &port) == 1 && port > 0 && port < 65536) {
		return port;
	}
	return 0;
}

static void ed137_new_branch(char *buf, size_t size)
{
	snprintf(buf, size, "z9hG4bK%08lx%08lx", (unsigned long)ast_random(), (unsigned long)ast_random());
}

static void ed137_dialog_reset(struct ed137_dialog *d)
{
	snprintf(d->call_id, sizeof(d->call_id), "%08lx%08lx@%s",
		(unsigned long)ast_random(), (unsigned long)ast_random(), ed137_general_cfg.vcs_host);
	snprintf(d->local_tag, sizeof(d->local_tag), "%08lx", (unsigned long)ast_random());
	d->remote_tag[0] = '\0';
	d->remote_uri[0] = '\0';
	d->cseq = 0;
	d->retries = 0;
	d->last_len = 0;
}

static void ed137_udp_send(int fd, const char *ip, int port, const void *buf, size_t len)
{
	struct sockaddr_in sa;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
		return;
	}
	if (sendto(fd, buf, len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		ast_log(LOG_WARNING, "ED137: sendto %s:%d failed: %s\n", ip, port, strerror(errno));
	}
}

/*! \brief Build a request. Empty to_tag / from_tag / call_id values are not printed as tags. */
static size_t ed137_build_request_branch(char *buf, size_t size, const char *branch, const char *method,
	const char *ruri, const char *to_uri, const char *to_tag, const char *from_tag, const char *call_id,
	unsigned int cseq, const char *extra, const char *body)
{
	size_t len = 0;

	len = ed137_addf(buf, size, len, "%s %s SIP/2.0\r\n", method, ruri);
	len = ed137_addf(buf, size, len, "Via: SIP/2.0/UDP %s:%d;rport;branch=%s\r\n",
		ed137_general_cfg.vcs_host, ed137_general_cfg.bindport, branch);
	len = ed137_addf(buf, size, len, "Max-Forwards: 19\r\n");
	len = ed137_addf(buf, size, len, "From: <%s>;tag=%s\r\n", ed137_general_cfg.vcs_uri, from_tag);
	len = ed137_addf(buf, size, len, "To: <%s>%s%s\r\n", to_uri,
		to_tag[0] ? ";tag=" : "", to_tag);
	len = ed137_addf(buf, size, len, "Contact: <sip:%s:%d>\r\n",
		ed137_general_cfg.vcs_host, ed137_general_cfg.bindport);
	len = ed137_addf(buf, size, len, "Call-ID: %s\r\n", call_id);
	len = ed137_addf(buf, size, len, "CSeq: %u %s\r\n", cseq, method);
	if (extra && extra[0]) {
		len = ed137_addf(buf, size, len, "%s", extra);
	}
	if (body && body[0]) {
		len = ed137_addf(buf, size, len, "Content-Type: application/sdp\r\n");
		len = ed137_addf(buf, size, len, "Content-Length: %zu\r\n\r\n%s", strlen(body), body);
	} else {
		len = ed137_addf(buf, size, len, "Content-Length: 0\r\n\r\n");
	}
	return len;
}

/*! \brief Build a request with a new Via branch. */
static size_t ed137_build_request(char *buf, size_t size, const char *method, const char *ruri,
	const char *to_uri, const char *to_tag, const char *from_tag, const char *call_id,
	unsigned int cseq, const char *extra, const char *body)
{
	char branch[40];

	ed137_new_branch(branch, sizeof(branch));
	return ed137_build_request_branch(buf, size, branch, method, ruri, to_uri, to_tag, from_tag,
		call_id, cseq, extra, body);
}

/*! \brief Response to a request, with the headers copied from it. */
static size_t ed137_build_response(char *buf, size_t size, const char *req, int code, const char *reason)
{
	char via[256], from[256], to[256], callid[160], cseq[64];
	size_t len = 0;

	ed137_sip_hdr(req, "Via", via, sizeof(via));
	ed137_sip_hdr(req, "From", from, sizeof(from));
	ed137_sip_hdr(req, "To", to, sizeof(to));
	ed137_sip_hdr(req, "Call-ID", callid, sizeof(callid));
	ed137_sip_hdr(req, "CSeq", cseq, sizeof(cseq));

	len = ed137_addf(buf, size, len, "SIP/2.0 %d %s\r\n", code, reason);
	len = ed137_addf(buf, size, len, "Via: %s\r\n", via);
	len = ed137_addf(buf, size, len, "From: %s\r\n", from);
	len = ed137_addf(buf, size, len, "To: %s\r\n", to);
	len = ed137_addf(buf, size, len, "Call-ID: %s\r\n", callid);
	len = ed137_addf(buf, size, len, "CSeq: %s\r\n", cseq);
	len = ed137_addf(buf, size, len, "Contact: <sip:%s:%d>\r\n",
		ed137_general_cfg.vcs_host, ed137_general_cfg.bindport);
	len = ed137_addf(buf, size, len, "Allow: INVITE, ACK, CANCEL, BYE, SUBSCRIBE, NOTIFY, OPTIONS\r\n");
	len = ed137_addf(buf, size, len, "Supported: \r\nWG67-Version: radio.02\r\n");
	len = ed137_addf(buf, size, len, "Content-Length: 0\r\n\r\n");
	return len;
}

/*! \brief Send a dialog request and keep it for retransmission. Lock held. */
static void ed137_dialog_send(struct ed137_radio *r, struct ed137_dialog *d, const char *msg, size_t len)
{
	if (len >= sizeof(d->last_msg)) {
		len = sizeof(d->last_msg) - 1;
	}
	memcpy(d->last_msg, msg, len);
	d->last_msg[len] = '\0';
	d->last_len = len;
	ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, msg, len);
}

/* ---------------- R2S keepalive ---------------- */

/*! \brief Extension word we send: TxRx mode bit, and PTT bit with the dump's PTT word. Lock held. */
static uint32_t ed137_ka_word_locked(const struct ed137_radio *r)
{
	uint32_t w = 0;

	if (r->mode == ED137_MODE_TXRX) {
		w |= (uint32_t)(r->ptt_id & 0x3F) << 22;	/* PTT-id; 1 gives the 0x00400000 seen in the dump */
	}
	if (r->ptt_keyed) {
		/* 0x00012100 is what the real VCS sends with PTT in the dump; meaning not decoded. */
		w |= 0x20000000 | 0x00012100;
	}
	return w;
}

/*! \brief Send one keepalive from the SDP port. Lock held. */
static void ed137_ka_send_locked(struct ed137_radio *r)
{
	uint8_t p[20];
	uint32_t word = ed137_ka_word_locked(r);
	int port = r->peer_rtp_port ? r->peer_rtp_port : r->radio_rtp_port;

	p[0] = 0x90;				/* V=2, X=1 */
	p[1] = 123;				/* PT 123, R2S */
	p[2] = (uint8_t)(r->ka_seq >> 8);
	p[3] = (uint8_t)r->ka_seq;
	p[4] = (uint8_t)(r->ka_ts >> 24);
	p[5] = (uint8_t)(r->ka_ts >> 16);
	p[6] = (uint8_t)(r->ka_ts >> 8);
	p[7] = (uint8_t)r->ka_ts;
	p[8] = (uint8_t)(r->ka_ssrc >> 24);
	p[9] = (uint8_t)(r->ka_ssrc >> 16);
	p[10] = (uint8_t)(r->ka_ssrc >> 8);
	p[11] = (uint8_t)r->ka_ssrc;
	p[12] = 0x01;				/* profile 0x0167 */
	p[13] = 0x67;
	p[14] = 0x00;				/* 1 word */
	p[15] = 0x01;
	p[16] = (uint8_t)(word >> 24);
	p[17] = (uint8_t)(word >> 16);
	p[18] = (uint8_t)(word >> 8);
	p[19] = (uint8_t)word;

	ed137_udp_send(r->rtp_fd, r->ip, port, p, sizeof(p));
	r->ka_seq++;
	r->ka_ts += (uint32_t)r->keepalive_ms * 8;
}

static void ed137_rx_audio_locked(struct ed137_radio *r, uint16_t seq, const uint8_t *payload,
	uint32_t ext, int64_t now);
static void ed137_rx_marker_locked(struct ed137_radio *r, uint32_t ext, int64_t now);
static int ed137_word_squ(uint32_t w);
static void ed137_sqi_report_locked(struct ed137_radio *r, int64_t now);
static int ed137_word_sqi(uint32_t w);
static void ed137_sip_reason_text(const char *msg, char *buf, size_t size);
static void ed137_rx_stop_locked(struct ed137_radio *r);
static void ed137_tx_start_locked(struct ed137_radio *r);

/*! \brief Read one packet from the radio: R2S keepalive (PT 123) or PCMA audio (PT 8). */
/*! \brief True when a datagram came from the given radio address (dotted quad). */
static int ed137_from_ip(const struct sockaddr_in *from, const char *ip)
{
	char txt[INET_ADDRSTRLEN];

	if (!inet_ntop(AF_INET, &from->sin_addr, txt, sizeof(txt))) {
		return 0;
	}
	return !strcmp(txt, ip);
}

static void ed137_audio_recv(struct ed137_radio *r)
{
	uint8_t buf[1024];
	struct sockaddr_in from;
	socklen_t flen = sizeof(from);
	ssize_t n = recvfrom(r->rtp_fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
	size_t off = 12;
	size_t ext_off = 0;
	int ext_words = 0;
	int cc, pt;
	uint16_t seq;
	uint32_t word = 0;

	if (n > 0 && (!ed137_from_ip(&from, r->ip) || !r->peer_rtp_port || ntohs(from.sin_port) != r->peer_rtp_port)) {
		return;	/* only the radio itself, from the RTP port of its session, may send audio here */
	}
	if (n < 12 || (buf[0] >> 6) != 2) {
		return;
	}
	pt = buf[1] & 0x7f;
	cc = buf[0] & 0x0f;
	off += 4 * (size_t)cc;
	if ((size_t)n < off) {
		return;
	}
	if (buf[0] & 0x10) {
		/* extension: 16-bit profile, 16-bit length in 32-bit words */
		if ((size_t)n < off + 4) {
			return;
		}
		ext_words = (buf[off + 2] << 8) | buf[off + 3];
		ext_off = off + 4;
		off = ext_off + 4 * (size_t)ext_words;
		if ((size_t)n < off) {
			return;
		}
	}
	seq = (uint16_t)((buf[2] << 8) | buf[3]);

	if (ext_words >= 1) {
		word = ((uint32_t)buf[ext_off] << 24) | ((uint32_t)buf[ext_off + 1] << 16)
			| ((uint32_t)buf[ext_off + 2] << 8) | buf[ext_off + 3];
	}
	ast_mutex_lock(&r->lock);
	r->rx_seen_ms = ed137_now_ms();
	if (pt == 123) {
		if (ext_words >= 1) {
			r->ka_rx_word = ((uint32_t)buf[ext_off] << 24) | ((uint32_t)buf[ext_off + 1] << 16)
				| ((uint32_t)buf[ext_off + 2] << 8) | buf[ext_off + 3];
			r->ka_rx_seen = 1;
		}
	} else if (pt == 8 && (size_t)n == off) {
		ed137_rx_marker_locked(r, word, ed137_now_ms());
	} else if (pt == 8 && (size_t)n - off == ED137_FRAME_BYTES) {
		if (ext_words >= 1) {
			r->rx_squ = ed137_word_squ(word);
			r->rx_sqi = ed137_word_sqi(word);
			r->rx_sqi_valid = r->rx_sqi >= 0;
			ed137_sqi_report_locked(r, ed137_now_ms());
		}
		if (r->rx_squelch && !r->rx_squ) {
			ed137_rx_marker_locked(r, word, ed137_now_ms());	/* carrier gone: close after the jitter buffer */
		} else {
			ed137_rx_audio_locked(r, seq, buf + off, word, ed137_now_ms());
		}
	}
	ast_mutex_unlock(&r->lock);
}

/*! \brief Radio RX event, from audio arrival. Lock held. */
/*! \brief SQU bit: bit 12 of the extension header, which is bit 28 of the first word. */
static int ed137_word_squ(uint32_t w)
{
    return (w >> 28) & 1;
}

/*! \brief SQI index of the Signal Quality feature (type 1, length 1) in the low 16 bits; -1 if absent. */
static int ed137_word_sqi(uint32_t w)
{
    if (((w >> 12) & 0xF) == 1 && ((w >> 8) & 0xF) == 1) {
        return (int)((w & 0xFF) >> 3);
    }
    return -1;
}

/*! \brief SQI index as text, same table as Wireshark's ED137A dissector. */
static void ed137_sqi_text(int idx, char *buf, size_t size)
{
    if (idx < 0) {
        snprintf(buf, size, "-");
    } else if (idx == 0) {
        snprintf(buf, size, "lower than -100 dBm");
    } else if (idx <= 14) {
        snprintf(buf, size, "lower than or equal to %.2f dBm", -100.0 + idx * 30.0 / 14.0);
    } else if (idx == 15) {
        snprintf(buf, size, "higher than -70 dBm");
    } else {
        snprintf(buf, size, "index %d (undefined)", idx);
    }
}

static void ed137_rx_event(struct ed137_radio *r, const char *state)
{
    char sqi[80];
    int idx = r->rx_sqi_valid ? r->rx_sqi : -1;

    ed137_sqi_text(idx, sqi, sizeof(sqi));
    ast_verb(3, "ED137: radio %s RX %s, ext %08x, SQI %s\n", r->name, state, (unsigned int)r->rx_ext, sqi);
    manager_event(EVENT_FLAG_CALL, "ED137RX",
        "Radio: %s\r\nState: %s\r\nExt: %08x\r\nSqi: %d\r\nSqiText: %s\r\n",
        r->name, state, (unsigned int)r->rx_ext, idx, sqi);
}

/*! \brief ED137SQI: the signal quality index changed; sent at most once per second. Lock held. */
static void ed137_sqi_report_locked(struct ed137_radio *r, int64_t now)
{
	char text[80];

	if (!r->rx_sqi_valid || r->rx_sqi == r->sqi_reported || now - r->sqi_event_ms < 1000) {
		return;
	}
	r->sqi_reported = r->rx_sqi;
	r->sqi_event_ms = now;
	ed137_sqi_text(r->rx_sqi, text, sizeof(text));
	manager_event(EVENT_FLAG_CALL, "ED137SQI",
		"Radio: %s\r\nSqi: %d\r\nSqiText: %s\r\n", r->name, r->rx_sqi, text);
}

/*! \brief Wake a channel: a frame is queued, or its session ended. */
static void ed137_pvt_wake(struct ed137_pvt *p)
{
	char c = 1;
	ssize_t rc;

	if (p->pipe_wr >= 0) {
		rc = write(p->pipe_wr, &c, 1);	/* a full pipe already means a pending wake-up */
		(void)rc;
	}
}

/*! \brief Audio packet from the radio into the jitter buffer. Lock held. */
static void ed137_rx_audio_locked(struct ed137_radio *r, uint16_t seq, const uint8_t *payload,
	uint32_t ext, int64_t now)
{
	int16_t diff;
	int idx;
	int i;

	r->rx_last_ms = now;
	r->rx_ext = ext;
	r->rx_close_at_ms = 0;
	if (!r->rx_playing) {
		for (i = 0; i < ED137_JB_SLOTS; i++) {
			r->rx_slot_valid[i] = 0;
		}
		r->rx_playing = 1;
		r->rx_next_seq = seq;
		r->rx_next_play_ms = now + ED137_JB_DELAY_MS;
		r->rx_pkts_in_tx = 0;
	}

	diff = (int16_t)(seq - r->rx_next_seq);
	if (diff < 0) {
		return;				/* too late: that slot was already played out */
	}
	if (diff >= ED137_JB_SLOTS) {		/* far ahead: resync to this packet */
		for (i = 0; i < ED137_JB_SLOTS; i++) {
			r->rx_slot_valid[i] = 0;
		}
		r->rx_next_seq = seq;
	}

	idx = seq % ED137_JB_SLOTS;
	memcpy(r->rx_slot[idx], payload, ED137_FRAME_BYTES);
	r->rx_slot_seq[idx] = seq;
	r->rx_slot_valid[idx] = 1;
	r->rx_frames++;
	r->rx_pkts_in_tx++;
	if (!r->rx_active && r->rx_pkts_in_tx >= ED137_RX_OPEN_PKTS) {
		r->rx_active = 1;
		ed137_rx_event(r, "open");
	}
}

/*! \brief End-of-transmission marker (empty PT 8, ext 00413100 in the dump). Lock held. */
static void ed137_rx_marker_locked(struct ed137_radio *r, uint32_t ext, int64_t now)
{
	r->rx_ext = ext;
	if (r->rx_playing) {
		r->rx_close_at_ms = now + ED137_JB_DELAY_MS;
	}
}

/*! \brief One RX frame to every listening channel. Lock held. */
static void ed137_rx_fanout_locked(struct ed137_radio *r, const uint8_t *frame)
{
	struct ed137_pvt *p;
	uint8_t silence[ED137_FRAME_BYTES];

	if (r->rx_mute_on_tx && (r->ptt_keyed || ed137_now_ms() < r->rx_mute_until_ms)) {
		memset(silence, ED137_ALAW_SILENCE, sizeof(silence));
		frame = silence;
	}
	AST_LIST_TRAVERSE(&r->listeners, p, entry) {
		int idx;

		if (p->q_count == ED137_PVT_QUEUE) {	/* full: drop the oldest */
			p->q_head = (p->q_head + 1) % ED137_PVT_QUEUE;
			p->q_count--;
		}
		idx = (p->q_head + p->q_count) % ED137_PVT_QUEUE;
		memcpy(p->q[idx], frame, ED137_FRAME_BYTES);
		p->q_count++;
		ed137_pvt_wake(p);
	}
}

/*! \brief Playout: one frame per 20 ms, silence where the packet is missing. Lock held. */
static void ed137_rx_play_locked(struct ed137_radio *r, int64_t now)
{
	int n = 0;

	while (n < 3 && now >= r->rx_next_play_ms) {
		int idx = r->rx_next_seq % ED137_JB_SLOTS;
		uint8_t frame[ED137_FRAME_BYTES];

		if (r->rx_slot_valid[idx] && r->rx_slot_seq[idx] == r->rx_next_seq) {
			memcpy(frame, r->rx_slot[idx], ED137_FRAME_BYTES);
			r->rx_slot_valid[idx] = 0;
		} else {
			memset(frame, ED137_ALAW_SILENCE, sizeof(frame));
			r->rx_plc++;
		}
		ed137_rx_fanout_locked(r, frame);
		r->rx_next_seq++;
		r->rx_next_play_ms += ED137_FRAME_MS;
		n++;
	}
}

/*! \brief Called every SIP-thread loop. RX closes after ED137_RX_IDLE_MS without audio. Lock held. */
static void ed137_rx_tick_locked(struct ed137_radio *r, int64_t now)
{
	if (!r->rx_playing) {
		return;
	}
	if ((r->rx_close_at_ms && now >= r->rx_close_at_ms) ||
	    now - r->rx_last_ms > ED137_RX_IDLE_MS) {
		ed137_rx_stop_locked(r);
		return;
	}
	ed137_rx_play_locked(r, now);
}

/*! \brief Stop playout; ED137RX closed if it was open. Lock held. */
static void ed137_rx_stop_locked(struct ed137_radio *r)
{
	r->rx_playing = 0;
	r->rx_close_at_ms = 0;
	if (r->rx_active) {
		r->rx_active = 0;
		ed137_rx_event(r, "closed");
	}
}

/*! \brief The session ended: every listening channel is told to hang up. Lock held. */
static void ed137_listeners_lost_locked(struct ed137_radio *r)
{
	struct ed137_pvt *p;

	AST_LIST_TRAVERSE(&r->listeners, p, entry) {
		p->session_lost = 1;
		ed137_pvt_wake(p);
	}
}

/* ---------------- PTT and session state ---------------- */

static void ed137_ptt_event(struct ed137_radio *r, const char *state, const char *owner)
{
	ast_verb(3, "ED137: radio %s PTT %s owner '%s'\n", r->name, state, owner);
	manager_event(EVENT_FLAG_CALL, "ED137PTT",
		"Radio: %s\r\nState: %s\r\nOwner: %s\r\nPttId: %d\r\n", r->name, state, owner, r->ptt_id);
}

/*! \brief Release PTT whatever the owner. Lock held. */
static void ed137_ptt_release_locked(struct ed137_radio *r, const char *why)
{
	char owner[sizeof(r->ptt_owner)];

	ast_copy_string(owner, r->ptt_owner, sizeof(owner));
	r->ptt_keyed = 0;
	r->ptt_owner[0] = '\0';
	r->ptt_keyer = NULL;
	r->tx_final = r->tx_active ? 2 : 0;
	r->rx_mute_until_ms = ed137_now_ms() + ED137_RX_MUTE_TAIL_MS;
	ast_verb(3, "ED137: radio %s PTT released: %s\n", r->name, why);
	ed137_ptt_event(r, "off", owner);
}

/*! \brief Session lost: no audio path, so PTT is released and the next INVITE is scheduled. Lock held. */
static void ed137_session_down(struct ed137_radio *r, const char *why, int64_t due_ms)
{
	r->inv.state = ED137_INV_BACKOFF;
	r->inv.due_ms = due_ms;
	r->peer_rtp_port = 0;
	r->ka_rx_seen = 0;
	r->reinv_pending = 0;
	r->tx_active = 0;
	r->tx_final = 0;
	r->tx_count = 0;
	ed137_rx_stop_locked(r);
	if (r->subscribe_auto) {
		r->subscribe = 1;	/* probe the subscription again after a session drop */
	}
	if (r->session_grace_s > 0) {
		if (!r->down_since_ms) {
			r->down_since_ms = ed137_now_ms();
		}
	} else {
		ed137_listeners_lost_locked(r);
	}
	ast_log(LOG_WARNING, "ED137: radio %s session down: %s\n", r->name, why);
	if (r->ptt_keyed) {
		ed137_ptt_release_locked(r, "session down");
	}
}

/*!
 * \brief Set or release PTT on a radio for an owner.
 *
 * Key:     no session -> NOSESSION; free -> owner takes PTT (event on);
 *          owner again -> repeat policy (ignore: nothing, ack: event on);
 *          other owner -> BUSY (event busy with the current holder).
 * Release: owner -> PTT off (event off); other owner -> NOTOWNER.
 * Release does not need a session, so PTT can never stay stuck.
 */
/*! \brief An ED137 owner name must be a live ED137 channel of this radio. Called without r->lock. */
static int ed137_owner_channel_ok(struct ed137_radio *r, const char *owner)
{
	struct ast_channel *c = ast_channel_get_by_name(owner);
	struct ed137_pvt *p;
	int ok = 0;

	if (!c) {
		return 0;
	}
	if (ast_channel_tech(c) == &ed137_channel_tech) {
		p = (struct ed137_pvt *)ast_channel_tech_pvt(c);
		ok = p && p->radio == r;
	}
	ast_channel_unref(c);
	return ok;
}

static enum ed137_key_result ed137_radio_key_by(struct ed137_radio *r, int on, const char *owner,
	struct ed137_pvt *keyer)
{
	enum ed137_key_result res = ED137_KEY_OK;

	if (on && !strncmp(owner, "ED137/", 6) && !ed137_owner_channel_ok(r, owner)) {
		return ED137_KEY_NOCHANNEL;
	}
	ast_mutex_lock(&r->lock);
	if (on) {
		if (r->inv.state != ED137_INV_UP) {
			res = ED137_KEY_NOSESSION;
		} else if (!r->ptt_keyed) {
			r->ptt_keyed = 1;
			r->ptt_keyer = keyer;
			ast_copy_string(r->ptt_owner, owner, sizeof(r->ptt_owner));
			ed137_ptt_event(r, "on", owner);
			ed137_tx_start_locked(r);
		} else if (!strcmp(r->ptt_owner, owner)) {
			if (r->ptt_repeat == ED137_PTT_REPEAT_ACK) {
				ed137_ptt_event(r, "on", owner);
			}
		} else {
			res = ED137_KEY_BUSY;
			ed137_ptt_event(r, "busy", r->ptt_owner);
		}
	} else {
		if (r->ptt_keyed && !strcmp(r->ptt_owner, owner)) {
			ed137_ptt_release_locked(r, "unkey by owner");
		} else if (r->ptt_keyed) {
			res = ED137_KEY_NOTOWNER;
		}
	}
	ast_mutex_unlock(&r->lock);
	return res;
}

static enum ed137_key_result ed137_radio_key(struct ed137_radio *r, int on, const char *owner)
{
	return ed137_radio_key_by(r, on, owner, NULL);
}

static int ed137_key_rc(enum ed137_key_result res)
{
	return res == ED137_KEY_OK ? 0 : -1;
}

/* ---------------- SIP messages from the radio ---------------- */

/*! \brief The radio's NOTIFY body: the line 'sip:<our VCS>,<state>' gives our state on the radio. */
static void ed137_notify_state(struct ed137_radio *r, const char *body)
{
    const char *p = body;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        size_t cl = ll < 255 ? ll : 255;
        char line[256];
        char *comma;

        memcpy(line, p, cl);
        line[cl] = '\0';
        while (cl && (line[cl - 1] == '\r' || line[cl - 1] == ' ')) {
            line[--cl] = '\0';
        }
        /* Subscriber lines: "<fid>,sip:<vcs>,<state>" (station) or "sip:<vcs>,<state>". Key lines have no sip: URI. */
        if (strstr(line, "sip:") && (!ed137_general_cfg.vcs_uri[0] || strstr(line, ed137_general_cfg.vcs_uri))) {
            comma = strrchr(line, ',');
            if (comma && comma[1]) {
                ast_copy_string(r->sub_state_seen, comma + 1, sizeof(r->sub_state_seen));
            }
        }
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
}

static void ed137_handle_request(struct ed137_radio *r, const char *msg, const struct sockaddr_in *from)
{
	char method[16], callid[160], buf[ED137_SIP_MAX], ip[INET_ADDRSTRLEN];
	size_t n;

	ed137_sip_method(msg, method, sizeof(method));
	ed137_sip_hdr(msg, "Call-ID", callid, sizeof(callid));

	if (!strcmp(method, "NOTIFY")) {
		char old[sizeof(r->sub_state_seen)], cseqv[64];
		unsigned int cseq_num;

		ed137_sip_hdr(msg, "CSeq", cseqv, sizeof(cseqv));
		cseq_num = (unsigned int)strtoul(cseqv, NULL, 10);
		if (r->notify_reject && cseq_num <= r->notify_cseq) {
			ast_log(LOG_WARNING, "ED137: radio %s NOTIFY CSeq %u is not above %u: 500 (notify_cseq = reject)\n",
				r->name, cseq_num, r->notify_cseq);
			n = ed137_build_response(buf, sizeof(buf), msg, 500, "Invalid CSeq");
		} else {
			r->notify_cseq = cseq_num;
			ast_copy_string(old, r->sub_state_seen, sizeof(old));
			ed137_notify_state(r, ed137_sip_body(msg));
			if (strcmp(old, r->sub_state_seen)) {
				ast_verb(3, "ED137: radio %s state for this VCS: %s\n", r->name, r->sub_state_seen);
			}
			n = ed137_build_response(buf, sizeof(buf), msg, 200, "OK");
		}
	} else if (!strcmp(method, "BYE")) {
		n = ed137_build_response(buf, sizeof(buf), msg, 200, "OK");
		if (!strcmp(callid, r->inv.call_id)) {
			char rs[200], why[256];

			ed137_sip_reason_text(msg, rs, sizeof(rs));
			snprintf(why, sizeof(why), "BYE from radio%s", rs);
			ed137_session_down(r, why, ed137_now_ms() + 200);
			r->inv.fails = 0;
		}
	} else if (!strcmp(method, "OPTIONS")) {
		n = ed137_build_response(buf, sizeof(buf), msg, 200, "OK");
	} else {
		n = ed137_build_response(buf, sizeof(buf), msg, 405, "Method Not Allowed");
	}

	inet_ntop(AF_INET, &from->sin_addr, ip, sizeof(ip));
	ed137_udp_send(ed137_sip_fd, ip, ntohs(from->sin_port), buf, n);
}

/*! \brief ACK for a final non-2xx answer to our INVITE (RFC 3261 17.1.1.3): the branch, CSeq and
 * Request-URI of the INVITE we sent last, and the To tag of the answer. */
static void ed137_send_ack_nonok(struct ed137_radio *r, const char *msg, unsigned int cseq)
{
    char buf[ED137_SIP_MAX], branch[40], tag[48];
    const char *ruri = r->inv.remote_uri[0] ? r->inv.remote_uri : r->uri;
    const char *via, *to, *eol, *t;
    size_t n, bl = 0, ml;

    branch[0] = '\0';
    tag[0] = '\0';
    ml = r->inv.last_len < sizeof(r->inv.last_msg) ? r->inv.last_len : sizeof(r->inv.last_msg) - 1;
    via = memmem(r->inv.last_msg, ml, "branch=", 7);
    if (via) {
        via += 7;
        while (bl < sizeof(branch) - 1 && via < r->inv.last_msg + ml && *via != ';' && *via != '\r' && *via != ' ') {
            branch[bl++] = *via++;
        }
        branch[bl] = '\0';
    }
    to = strcasestr(msg, "\r\nTo:");
    if (to) {
        eol = strstr(to + 2, "\r\n");
        t = memmem(to, eol ? (size_t)(eol - to) : strlen(to), ";tag=", 5);
        if (t) {
            t += 5;
            for (n = 0; t[n] && t[n] != ';' && t[n] != '\r' && t[n] != '>' && t[n] != ' ' && n < sizeof(tag) - 1; n++) {
                tag[n] = t[n];
            }
            tag[n] = '\0';
        }
    }
    n = ed137_build_request_branch(buf, sizeof(buf), branch, "ACK", ruri, r->uri, tag, r->inv.local_tag,
        r->inv.call_id, cseq, "", "");
    ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, buf, n);
}

static void ed137_send_ack(struct ed137_radio *r, unsigned int cseq)
{
	char buf[ED137_SIP_MAX];
	const char *ruri = r->inv.remote_uri[0] ? r->inv.remote_uri : r->uri;
	size_t n = ed137_build_request(buf, sizeof(buf), "ACK", ruri, r->uri, r->inv.remote_tag,
		r->inv.local_tag, r->inv.call_id, cseq, "", "");

	ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, buf, n);
}

static void ed137_sub_send(struct ed137_radio *r, int64_t now)
{
	char extra[512], buf[ED137_SIP_MAX];
	const char *ruri = r->sub.remote_uri[0] ? r->sub.remote_uri : r->uri;
	size_t n;

	snprintf(extra, sizeof(extra),
		"Event: WG67KEY-IN\r\nExpires: %d\r\nAccept: text/plain\r\n"
		"Allow-Events: presence, message-summary, refer, WG67KEY-IN, conference\r\n"
		"Supported: replaces, 100rel, timer, norefersub\r\nWG67-Version: radio.02\r\n",
		ed137_general_cfg.expires);
	r->sub.cseq++;
	n = ed137_build_request(buf, sizeof(buf), "SUBSCRIBE", ruri, r->uri, r->sub.remote_tag,
		r->sub.local_tag, r->sub.call_id, r->sub.cseq, extra, "");
	ed137_dialog_send(r, &r->sub, buf, n);
	r->sub.state = ED137_SUB_WAIT;
	r->sub.retries = 0;
	r->sub.started_ms = now;
	r->sub.due_ms = now + 1000;
}

/*! \brief Reason header of a SIP message as " (Reason: ...)", or "" when absent. */
static void ed137_sip_reason_text(const char *msg, char *buf, size_t size)
{
	const char *p = strcasestr(msg, "\r\nReason:");
	const char *e;
	size_t n;

	buf[0] = '\0';
	if (!p) {
		return;
	}
	p += strlen("\r\nReason:");
	while (*p == ' ') {
		p++;
	}
	e = strstr(p, "\r\n");
	n = e ? (size_t)(e - p) : strlen(p);
	snprintf(buf, size, " (Reason: %.*s)", (int)n, p);
}

static unsigned int ed137_sip_cseq_num(const char *msg)
{
	char cseq[64];

	ed137_sip_hdr(msg, "CSeq", cseq, sizeof(cseq));
	return (unsigned int)strtoul(cseq, NULL, 10);
}

/*! \brief SDP in the form the working client sends it. sdp_type: Radio-Idle for the INVITE, the mode type for re-INVITE. */
static size_t ed137_build_sdp(const struct ed137_radio *r, const char *sdp_type, char *buf, size_t size)
{
	size_t len = 0;

	len = ed137_addf(buf, size, len, "v=0\r\n");
	len = ed137_addf(buf, size, len, "o=- %llu %llu IN IP4 %s\r\n",
		r->sdp_ver, r->sdp_ver, ed137_general_cfg.vcs_host);
	len = ed137_addf(buf, size, len, "s=-\r\n");
	len = ed137_addf(buf, size, len, "c=IN IP4 %s\r\n", ed137_general_cfg.vcs_host);
	len = ed137_addf(buf, size, len, "t=0 0\r\n");
	len = ed137_addf(buf, size, len, "m=audio %d RTP/AVP 8 0 18%s\r\n", r->rtp_port, r->r2s ? " 123" : "");
	len = ed137_addf(buf, size, len, "a=rtpmap:8 PCMA/8000\r\n");
	len = ed137_addf(buf, size, len, "a=rtpmap:0 PCMU/8000\r\n");
	len = ed137_addf(buf, size, len, "a=rtpmap:18 G729/8000\r\n");
	if (r->r2s) {
		len = ed137_addf(buf, size, len, "a=rtpmap:123 R2S/8000\r\n");
	}
	len = ed137_addf(buf, size, len, "a=fmtp:18 annexb=yes\r\n");
	len = ed137_addf(buf, size, len, "a=sendrecv\r\n");
	len = ed137_addf(buf, size, len, "a=ptime:20\r\n");
	len = ed137_addf(buf, size, len, "a=rtphe:1\r\n");
	len = ed137_addf(buf, size, len, "a=ptt_rep:0\r\n");
	len = ed137_addf(buf, size, len, "a=sigtime:1\r\n");
	if (r->r2s) {
		len = ed137_addf(buf, size, len, "a=R2S-KeepAlivePeriod:%d\r\n", r->keepalive_ms);
	}
	len = ed137_addf(buf, size, len, "a=type:%s\r\n", sdp_type);
	if (r->r2s) {
		len = ed137_addf(buf, size, len, "a=R2S-KeepAliveMultiplier:6\r\n");
	}
	return len;
}

static void ed137_inv_start(struct ed137_radio *r, int64_t now)
{
	char buf[ED137_SIP_MAX], sdp[1024];
	size_t n;

	ed137_dialog_reset(&r->inv);
	r->sdp_ver = (unsigned long long)ast_random() & 0x7fffffffULL;
	r->reinv_pending = 0;
	(void)ed137_build_sdp(r, "Radio-Idle", sdp, sizeof(sdp));

	r->inv.cseq++;
	n = ed137_build_request(buf, sizeof(buf), "INVITE", r->uri, r->uri, "", r->inv.local_tag,
		r->inv.call_id, r->inv.cseq, r->invite_extra, sdp);
	ed137_dialog_send(r, &r->inv, buf, n);
	r->inv.state = ED137_INV_WAIT;
	r->inv.started_ms = now;
	r->inv.due_ms = now + 500;
	ast_verb(3, "ED137: radio %s INVITE sent (Radio-Idle), R2S %s, WG67 %s\n", r->name,
		r->r2s ? "on" : "off", r->wg67 ? "on" : "off");
}

/*! \brief Session is up: re-INVITE to the mode type (rxonly -> Radio-Rx), as the working client does. Lock held. */
static void ed137_reinvite_start(struct ed137_radio *r, int64_t now)
{
	char buf[ED137_SIP_MAX], sdp[1024];
	const char *type = ed137_mode_sdp_type(r->mode);
	const char *ruri = r->inv.remote_uri[0] ? r->inv.remote_uri : r->uri;
	size_t n;

	if (r->mode == ED137_MODE_IDLE || r->inv.state != ED137_INV_UP) {
		return;
	}
	r->sdp_ver++;
	(void)ed137_build_sdp(r, type, sdp, sizeof(sdp));

	r->inv.cseq++;
	n = ed137_build_request(buf, sizeof(buf), "INVITE", ruri, r->uri, r->inv.remote_tag,
		r->inv.local_tag, r->inv.call_id, r->inv.cseq, r->invite_extra, sdp);
	ed137_dialog_send(r, &r->inv, buf, n);
	r->reinv_pending = 1;
	r->reinv_retx = 0;
	r->reinv_started_ms = now;
	r->reinv_due_ms = now + 500;
	ast_verb(3, "ED137: radio %s re-INVITE sent, type %s\n", r->name, type);
}

/*! \brief re-INVITE retransmits (500 ms doubling) and its timeout. Lock held. */
static void ed137_reinvite_timers_locked(struct ed137_radio *r, int64_t now)
{
	int shift;

	if (!r->reinv_pending) {
		return;
	}
	if (now - r->reinv_started_ms >= (int64_t)ed137_general_cfg.invite_timeout * 1000) {
		r->reinv_pending = 0;
		ast_log(LOG_WARNING, "ED137: radio %s did not accept re-INVITE (%s), session stays Radio-Idle\n",
			r->name, ed137_mode_sdp_type(r->mode));
		return;
	}
	if (now >= r->reinv_due_ms) {
		shift = r->reinv_retx < 3 ? r->reinv_retx : 3;
		r->reinv_retx++;
		ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, r->inv.last_msg, r->inv.last_len);
		r->reinv_due_ms = now + (500LL << shift);
	}
}

/*! \brief 200 OK (or failure) to our re-INVITE. Lock held. */
static void ed137_reinvite_response(struct ed137_radio *r, int code, unsigned int cseq)
{
	r->reinv_pending = 0;
	if (code >= 300) {
		ast_log(LOG_WARNING, "ED137: radio %s rejected re-INVITE with %d, session stays Radio-Idle\n",
			r->name, code);
		return;
	}
	ed137_send_ack(r, cseq);
	ast_verb(2, "ED137: radio %s switched to %s\n", r->name, ed137_mode_sdp_type(r->mode));
}

/*! \brief INVITE failed (timeout or 3xx-6xx): backoff 5, 10, 30 s, PTT released. Lock held. */
static void ed137_inv_failed(struct ed137_radio *r, int64_t now, const char *why)
{
	int idx = r->inv.fails < 2 ? r->inv.fails : 2;

	r->inv.fails++;
	ed137_session_down(r, why, now + ed137_backoff_ms[idx]);
}

static void ed137_handle_response(struct ed137_radio *r, const char *msg, int64_t now)
{
	char method[16], tag[48], uri[160], hdr[256];
	int code = ed137_sip_status(msg);

	ed137_sip_method(msg, method, sizeof(method));

	if (!strcmp(method, "SUBSCRIBE")) {
		if (code >= 200 && code < 300) {
			ed137_sip_hdr(msg, "To", hdr, sizeof(hdr));
			ed137_sip_tag(hdr, tag, sizeof(tag));
			if (tag[0]) {
				ast_copy_string(r->sub.remote_tag, tag, sizeof(r->sub.remote_tag));
			}
			ed137_sip_hdr(msg, "Contact", hdr, sizeof(hdr));
			ed137_sip_uri(hdr, uri, sizeof(uri));
			if (uri[0]) {
				ast_copy_string(r->sub.remote_uri, uri, sizeof(r->sub.remote_uri));
			}
			if (r->sub.state != ED137_SUB_ACTIVE) {
				ast_debug(1, "ED137: radio %s subscribed (WG67KEY-IN)\n", r->name);
			}
			r->sub.state = ED137_SUB_ACTIVE;
			r->sub.retries = 0;
			r->sub.due_ms = now + (int64_t)ed137_general_cfg.expires * 1000 * 5 / 6;
		} else if (code >= 300) {
			char rs[200];

			ed137_sip_reason_text(msg, rs, sizeof(rs));
			r->sub.state = ED137_SUB_IDLE;
			if (r->subscribe_auto) {
				r->subscribe = 0;
				ast_log(LOG_WARNING, "ED137: radio %s SUBSCRIBE rejected with %d%s; WG67KEY-IN state reporting is off for this session (calls are not affected)\n",
					r->name, code, rs);
			} else {
				ast_log(LOG_WARNING, "ED137: radio %s SUBSCRIBE rejected with %d%s, retry in 5 s\n",
					r->name, code, rs);
				r->sub.due_ms = now + 5000;
			}
		}
		return;
	}

	if (!strcmp(method, "INVITE")) {
		unsigned int cseq_num = ed137_sip_cseq_num(msg);

		if (code < 200) {
			return;
		}
		if (r->inv.state == ED137_INV_UP && r->reinv_pending && cseq_num == r->inv.cseq) {
			if (code >= 300) {
				ed137_send_ack_nonok(r, msg, cseq_num);
			}
			ed137_reinvite_response(r, code, cseq_num);
			return;
		}
		if (code >= 300) {
			if (r->inv.state == ED137_INV_WAIT) {
				char rs[200], why[256];

				ed137_send_ack_nonok(r, msg, cseq_num);

				ed137_sip_reason_text(msg, rs, sizeof(rs));
				snprintf(why, sizeof(why), "INVITE rejected %d%s", code, rs);
				if (code == 501 && strstr(rs, "cause=2016") && r->wg67_mode == ED137_WG67_AUTO) {
					if (r->wg67_probe < 1) {
						r->wg67_probe++;
						r->wg67 = !r->wg67;
						ed137_build_invite_extra(r);
						ast_verb(2, "ED137: radio %s answered WG67 cause 2016, retrying INVITE with WG67 %s\n",
							r->name, r->wg67 ? "on" : "off");
						r->inv.state = ED137_INV_BACKOFF;
						r->inv.due_ms = now;
					} else {
						r->wg67_probe = 0;	/* both forms rejected: normal backoff, start again with WG67 */
						r->wg67 = 1;
						ed137_build_invite_extra(r);
						ed137_inv_failed(r, now, why);
					}
				} else {
					ed137_inv_failed(r, now, why);
				}
			}
			return;
		}
		if (r->inv.state == ED137_INV_UP) {
			ed137_send_ack(r, cseq_num);		/* 200 OK retransmission */
			return;
		}
		if (r->inv.state != ED137_INV_WAIT) {
			return;
		}

		ed137_sip_hdr(msg, "To", hdr, sizeof(hdr));
		ed137_sip_tag(hdr, tag, sizeof(tag));
		ast_copy_string(r->inv.remote_tag, tag, sizeof(r->inv.remote_tag));
		ed137_sip_hdr(msg, "Contact", hdr, sizeof(hdr));
		ed137_sip_uri(hdr, uri, sizeof(uri));
		ast_copy_string(r->inv.remote_uri, uri, sizeof(r->inv.remote_uri));
		r->peer_rtp_port = ed137_sdp_audio_port(ed137_sip_body(msg));
		if (!r->peer_rtp_port) {
			r->peer_rtp_port = r->radio_rtp_port;
		}

		r->inv.state = ED137_INV_UP;
		r->inv.fails = 0;
		r->wg67_probe = 0;
		r->ka_next_ms = now;
		r->rx_seen_ms = now;
		ed137_send_ack(r, cseq_num);
		ast_verb(2, "ED137: radio %s session up (radio RTP port %d, R2S %s, WG67 %s)\n", r->name, r->peer_rtp_port, r->r2s ? "on" : "off", r->wg67 ? "on" : "off");
		if (r->mode != ED137_MODE_IDLE) {
			ed137_reinvite_start(r, now);
		}
		return;
	}
}

/*! \brief SIP datagrams are accepted only from the address of a configured radio. */
static int ed137_sip_source_ok(const struct sockaddr_in *from)
{
	struct ed137_radio *r;
	int ok = 0;

	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		if (ed137_from_ip(from, r->ip)) {
			ok = 1;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&ed137_radios);
	return ok;
}

static void ed137_sip_recv(void)
{
	char buf[ED137_SIP_MAX];
	struct sockaddr_in from;
	socklen_t flen = sizeof(from);
	ssize_t n = recvfrom(ed137_sip_fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
	char callid[160];
	struct ed137_radio *r;
	int64_t now;

	if (n <= 0) {
		return;
	}
	if (!ed137_sip_source_ok(&from)) {
		ast_debug(3, "ED137: SIP datagram from an unknown address ignored\n");
		return;
	}
	buf[n] = '\0';

	ed137_sip_hdr(buf, "Call-ID", callid, sizeof(callid));
	r = ed137_radio_by_callid(callid);
	if (!r) {
		ast_debug(2, "ED137: message for unknown dialog '%s' ignored\n", callid);
		return;
	}

	now = ed137_now_ms();
	ast_mutex_lock(&r->lock);
	if (ed137_sip_status(buf) >= 0) {
		ed137_handle_response(r, buf, now);
	} else {
		ed137_handle_request(r, buf, &from);
	}
	ast_mutex_unlock(&r->lock);
}

/* ---------------- TX ---------------- */

/*! \brief PTT taken: TX packets (audio with the PTT bit) start now, every 20 ms. Lock held. */
static void ed137_tx_start_locked(struct ed137_radio *r)
{
	r->tx_active = 1;
	r->tx_final = 0;
	r->tx_count = 0;
	r->tx_next_ms = ed137_now_ms();
	r->ptt_started_ms = r->tx_next_ms;
	r->tx_audio_ms = r->tx_next_ms;
	ast_verb(3, "ED137: radio %s TX started\n", r->name);
}

/*! \brief One TX packet: PCMA from the queue or silence; the extension word is the R2S state (PTT bit while keyed). Lock held. */
static void ed137_tx_packet_locked(struct ed137_radio *r)
{
	uint8_t p[12 + 4 + 4 + ED137_FRAME_BYTES];
	uint32_t word = ed137_ka_word_locked(r);
	int port = r->peer_rtp_port ? r->peer_rtp_port : r->radio_rtp_port;
	uint32_t ts = r->ka_ts;

	if (r->tx_count) {
		memcpy(p + 20, r->tx_q[r->tx_head], ED137_FRAME_BYTES);
		r->tx_head = (r->tx_head + 1) % ED137_TX_QUEUE;
		r->tx_count--;
	} else {
		memset(p + 20, ED137_ALAW_SILENCE, ED137_FRAME_BYTES);
	}
	p[0] = 0x90;				/* V=2, X=1 */
	p[1] = 8;					/* PT 8, PCMA */
	p[2] = (uint8_t)(r->ka_seq >> 8);
	p[3] = (uint8_t)r->ka_seq;
	p[4] = (uint8_t)(ts >> 24);
	p[5] = (uint8_t)(ts >> 16);
	p[6] = (uint8_t)(ts >> 8);
	p[7] = (uint8_t)ts;
	p[8] = (uint8_t)(r->ka_ssrc >> 24);
	p[9] = (uint8_t)(r->ka_ssrc >> 16);
	p[10] = (uint8_t)(r->ka_ssrc >> 8);
	p[11] = (uint8_t)r->ka_ssrc;
	p[12] = 0x01;				/* profile 0x0167 */
	p[13] = 0x67;
	p[14] = 0x00;				/* 1 word */
	p[15] = 0x01;
	p[16] = (uint8_t)(word >> 24);
	p[17] = (uint8_t)(word >> 16);
	p[18] = (uint8_t)(word >> 8);
	p[19] = (uint8_t)word;

	ed137_udp_send(r->rtp_fd, r->ip, port, p, sizeof(p));
	r->ka_seq++;
	r->ka_ts += ED137_FRAME_BYTES;		/* 160 samples at 8 kHz */
	r->tx_frames++;
}

/*! \brief TX pacing: one packet per 20 ms while TX is active, two cleared packets after release,
 *  PTT max time. Lock held. */
static void ed137_tx_timers_locked(struct ed137_radio *r, int64_t now)
{
	int n = 0;

	if (!r->tx_active) {
		return;
	}
	if (r->inv.state != ED137_INV_UP) {
		r->tx_active = 0;
		r->tx_final = 0;
		r->tx_count = 0;
		return;
	}
	if (r->ptt_keyed && now - r->tx_audio_ms > (int64_t)r->tx_idle_s * 1000) {
		ast_log(LOG_WARNING, "ED137: radio %s no audio from the PTT owner for %d s, released\n",
			r->name, r->tx_idle_s);
		ed137_ptt_release_locked(r, "owner audio stopped");
	}
	if (r->ptt_keyed && now - r->ptt_started_ms >= (int64_t)r->ptt_max_s * 1000) {
		ast_log(LOG_WARNING, "ED137: radio %s PTT held longer than ptt_max_s, released\n", r->name);
		ed137_ptt_release_locked(r, "PTT max time");
	}
	if (now - r->tx_next_ms > ED137_TX_MAX_LAG_MS) {
		r->tx_next_ms = now;			/* stalled: resync instead of sending a burst */
	}
	while (now >= r->tx_next_ms && n < 4) {
		ed137_tx_packet_locked(r);
		r->tx_next_ms += ED137_FRAME_MS;
		n++;
		if (r->tx_final && --r->tx_final == 0) {
			r->tx_active = 0;
			r->tx_count = 0;
			ast_verb(3, "ED137: radio %s TX finished (%lu packets)\n", r->name, r->tx_frames);
			break;
		}
	}
}

/* ---------------- timers ---------------- */

/*! \brief Everything the radio needs on time: subscription, INVITE, keepalive. Lock held. */
static void ed137_timers_locked(struct ed137_radio *r, int64_t now)
{
	ed137_devstate_update_locked(r);
	if (r->down_since_ms && r->inv.state == ED137_INV_UP) {
		r->down_since_ms = 0;		/* session is back: the calls continue */
	}
	if (r->down_since_ms && now - r->down_since_ms > (int64_t)r->session_grace_s * 1000) {
		r->down_since_ms = 0;		/* grace is over: end the calls */
		ed137_listeners_lost_locked(r);
	}
	if (r->subscribe && r->wg67) {
		switch (r->sub.state) {
		case ED137_SUB_IDLE:
			if (now >= r->sub.due_ms) {
				ed137_dialog_reset(&r->sub);
				ed137_sub_send(r, now);
			}
			break;
		case ED137_SUB_WAIT:
			if (now >= r->sub.due_ms) {
				if (r->sub.retries < ED137_SUB_RETX_MAX) {
					r->sub.retries++;
					ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port,
						r->sub.last_msg, r->sub.last_len);
					r->sub.due_ms = now + 1000;
				} else {
					ast_log(LOG_WARNING, "ED137: radio %s no answer to SUBSCRIBE, new dialog in 5 s\n",
						r->name);
					r->sub.state = ED137_SUB_IDLE;
					r->sub.due_ms = now + 5000;
				}
			}
			break;
		case ED137_SUB_ACTIVE:
			if (now >= r->sub.due_ms) {
				ed137_sub_send(r, now);
			}
			break;
		}
	}

	switch (r->inv.state) {
	case ED137_INV_IDLE:
		break;
	case ED137_INV_WAIT:
		if (now - r->inv.started_ms >= (int64_t)ed137_general_cfg.invite_timeout * 1000) {
			ed137_inv_failed(r, now, "no answer to INVITE");
		} else if (now >= r->inv.due_ms) {
			int shift = r->inv.retries < 3 ? r->inv.retries : 3;

			r->inv.retries++;
			ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, r->inv.last_msg, r->inv.last_len);
			r->inv.due_ms = now + (500LL << shift);
		}
		break;
	case ED137_INV_BACKOFF:
		if (now >= r->inv.due_ms) {
			ed137_inv_start(r, now);
		}
		break;
	case ED137_INV_UP:
		if (r->r2s && now - r->rx_seen_ms > r->liveness_ms) {
			ed137_session_down(r, "no traffic from radio", now + 1000);
			break;
		}
		ed137_reinvite_timers_locked(r, now);
		ed137_tx_timers_locked(r, now);
		if (r->r2s && !r->tx_active && now >= r->ka_next_ms) {
			ed137_ka_send_locked(r);
			r->ka_next_ms = now + r->keepalive_ms;
		}
		break;
	}

	ed137_rx_tick_locked(r, now);
}

/* ---------------- SIP thread ---------------- */

static void *ed137_thread_main(void *data)
{
	struct pollfd pfds[ED137_MAX_RADIOS + 1];
	struct ed137_radio *rp[ED137_MAX_RADIOS];
	struct ed137_radio *r;

	(void)data;
	while (ed137_running) {
		int nfds = 1;
		int nr = 0;
		int i;
		int64_t now;

		pfds[0].fd = ed137_sip_fd;
		pfds[0].events = POLLIN;
		pfds[0].revents = 0;

		AST_RWLIST_RDLOCK(&ed137_radios);
		AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
			rp[nr] = r;
			pfds[nfds].fd = r->rtp_fd;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			nfds++;
			nr++;
		}
		AST_RWLIST_UNLOCK(&ed137_radios);

		if (poll(pfds, nfds, 10) < 0 && errno != EINTR) {
			ast_log(LOG_WARNING, "ED137: poll failed: %s\n", strerror(errno));
		}
		if (pfds[0].revents & POLLIN) {
			ed137_sip_recv();
		}
		for (i = 0; i < nr; i++) {
			if (pfds[i + 1].revents & POLLIN) {
				ed137_audio_recv(rp[i]);
			}
		}

		now = ed137_now_ms();
		AST_RWLIST_RDLOCK(&ed137_radios);
		AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
			ast_mutex_lock(&r->lock);
			ed137_timers_locked(r, now);
			ast_mutex_unlock(&r->lock);
		}
		AST_RWLIST_UNLOCK(&ed137_radios);
	}
	return NULL;
}

static int ed137_bind_udp(const char *ip, int port)
{
	struct sockaddr_in sa;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	int fl;

	if (fd < 0) {
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1 || bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(fd);
		return -1;
	}
	fl = fcntl(fd, F_GETFL, 0);
	if (fl >= 0) {
		fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	}
	return fd;
}

/*! \brief Open the SIP socket and one keepalive socket per radio, then start the thread. */
static int ed137_start_sip(void)
{
	struct ed137_radio *r;
	struct sockaddr_in sa;
	socklen_t slen = sizeof(sa);

	if (!ed137_radio_count) {
		return 0;
	}
	ed137_sip_fd = ed137_bind_udp(ed137_general_cfg.bindaddr, ed137_general_cfg.bindport);
	if (ed137_sip_fd < 0) {
		ast_log(LOG_ERROR, "ED137: cannot bind SIP %s:%d: %s\n", ed137_general_cfg.bindaddr,
			ed137_general_cfg.bindport, strerror(errno));
		return -1;
	}

	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		r->rtp_fd = ed137_bind_udp(ed137_general_cfg.bindaddr, r->local_rtp_port);
		if (r->rtp_fd < 0) {
			ast_log(LOG_ERROR, "ED137: cannot open RTP socket for radio %s on %s:%d: %s\n",
				r->name, ed137_general_cfg.bindaddr, r->local_rtp_port, strerror(errno));
			AST_RWLIST_UNLOCK(&ed137_radios);
			return -1;
		}
		memset(&sa, 0, sizeof(sa));
		slen = sizeof(sa);
		getsockname(r->rtp_fd, (struct sockaddr *)&sa, &slen);
		r->rtp_port = ntohs(sa.sin_port);
		r->ka_seq = (uint16_t)ast_random();
		r->ka_ts = (uint32_t)ast_random();
		r->ka_ssrc = (uint32_t)ast_random();
		ast_mutex_lock(&r->lock);
		ed137_dialog_reset(&r->sub);
		ed137_dialog_reset(&r->inv);
		r->sub.state = ED137_SUB_IDLE;
		r->sub.due_ms = 0;
		r->inv.state = ED137_INV_BACKOFF;
		r->inv.due_ms = ed137_now_ms();
		ast_mutex_unlock(&r->lock);
	}
	AST_RWLIST_UNLOCK(&ed137_radios);

	ed137_running = 1;
	if (ast_pthread_create_background(&ed137_thread, NULL, ed137_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "ED137: cannot start SIP thread\n");
		ed137_running = 0;
		return -1;
	}
	return 0;
}

/*! \brief Stop the thread, send BYE for running sessions, close sockets. */
static void ed137_stop_sip(void)
{
	struct ed137_radio *r;
	char buf[ED137_SIP_MAX];
	size_t n;

	if (ed137_running) {
		ed137_running = 0;
		pthread_join(ed137_thread, NULL);
	}

	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		ast_mutex_lock(&r->lock);
		if (r->inv.state == ED137_INV_UP) {
			const char *ruri = r->inv.remote_uri[0] ? r->inv.remote_uri : r->uri;

			r->inv.cseq++;
			n = ed137_build_request(buf, sizeof(buf), "BYE", ruri, r->uri, r->inv.remote_tag,
				r->inv.local_tag, r->inv.call_id, r->inv.cseq, "", "");
			ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, buf, n);
			r->inv.state = ED137_INV_IDLE;
		}
		ast_mutex_unlock(&r->lock);
	}
	AST_RWLIST_UNLOCK(&ed137_radios);

	if (ed137_sip_fd >= 0) {
		close(ed137_sip_fd);
		ed137_sip_fd = -1;
	}
}

/* ---------------- channel ---------------- */

/*! \brief Free a channel's private data: pipes and the format reference. */
static void ed137_pvt_free(struct ed137_pvt *pvt)
{
	if (!pvt) {
		return;
	}
	if (pvt->pipe_rd >= 0) {
		close(pvt->pipe_rd);
	}
	if (pvt->pipe_wr >= 0) {
		close(pvt->pipe_wr);
	}
	ao2_cleanup(pvt->fmt);
	ast_free(pvt);
}

/*! \brief Called by the core when the channel is ready for a frame: one RX frame from the radio. */
static struct ast_frame *ed137_read(struct ast_channel *ast)
{
	struct ed137_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ed137_radio *r;
	char c;

	if (!pvt || !pvt->radio) {
		return NULL;
	}
	r = pvt->radio;

	ast_mutex_lock(&r->lock);
	if (pvt->session_lost) {
		ast_mutex_unlock(&r->lock);
		return NULL;			/* NULL from read() hangs the channel up */
	}
	if (read(pvt->pipe_rd, &c, 1) != 1 || pvt->q_count == 0) {
		ast_mutex_unlock(&r->lock);
		return &ast_null_frame;
	}
	memcpy(pvt->frbuf + AST_FRIENDLY_OFFSET, pvt->q[pvt->q_head], ED137_FRAME_BYTES);
	pvt->q_head = (pvt->q_head + 1) % ED137_PVT_QUEUE;
	pvt->q_count--;
	ast_mutex_unlock(&r->lock);

	memset(&pvt->fr, 0, sizeof(pvt->fr));
	pvt->fr.frametype = AST_FRAME_VOICE;
	pvt->fr.subclass.format = pvt->fmt;
	pvt->fr.data.ptr = pvt->frbuf + AST_FRIENDLY_OFFSET;
	pvt->fr.datalen = ED137_FRAME_BYTES;
	pvt->fr.samples = ED137_FRAME_BYTES;
	pvt->fr.src = ED137_TECH_TYPE;
	return &pvt->fr;
}

/*! \brief Called by the core with audio for the radio: queued for transmission while the channel owns the PTT. */
static int ed137_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct ed137_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ed137_radio *r;

	if (!pvt || !pvt->radio || !f || f->frametype != AST_FRAME_VOICE) {
		return 0;
	}
	if (f->datalen != ED137_FRAME_BYTES) {
		return 0;				/* only 20 ms A-law frames reach the radio */
	}
	r = pvt->radio;
	ast_mutex_lock(&r->lock);
	if (r->ptt_keyed && (r->ptt_keyer == pvt || !strcmp(r->ptt_owner, ast_channel_name(ast)))) {
		r->tx_audio_ms = ed137_now_ms();
		if (r->tx_count == ED137_TX_QUEUE) {	/* full: drop the oldest, keep latency low */
			r->tx_head = (r->tx_head + 1) % ED137_TX_QUEUE;
			r->tx_count--;
		}
		memcpy(r->tx_q[(r->tx_head + r->tx_count) % ED137_TX_QUEUE], f->data.ptr, ED137_FRAME_BYTES);
		r->tx_count++;
	}
	ast_mutex_unlock(&r->lock);
	return 0;
}

/*! \brief RADIO_KEY / RADIO_UNKEY from the core, owner = this channel. */
static int ed137_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct ed137_pvt *pvt = ast_channel_tech_pvt(ast);

	(void)data;
	(void)datalen;
	if (!pvt || !pvt->radio) {
		return -1;
	}
	switch (condition) {
	case AST_CONTROL_RADIO_KEY:
		return ed137_key_rc(ed137_radio_key_by(pvt->radio, 1, ast_channel_name(ast), pvt));
	case AST_CONTROL_RADIO_UNKEY:
		return ed137_key_rc(ed137_radio_key_by(pvt->radio, 0, ast_channel_name(ast), pvt));
	default:
		return -1;
	}
}

/*! \brief Dial/Originate: attach the channel to the radio's RX and answer. */
static int ed137_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct ed137_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ed137_radio *r;
	int up;
	int listeners;

	(void)dest;
	(void)timeout;
	if (!pvt || !pvt->radio) {
		return -1;
	}
	r = pvt->radio;

	ast_mutex_lock(&r->lock);
	up = (r->inv.state == ED137_INV_UP);
	if (up && !pvt->attached) {
		AST_LIST_INSERT_TAIL(&r->listeners, pvt, entry);
		pvt->attached = 1;
		r->listeners_count++;
	}
	listeners = r->listeners_count;
	ast_mutex_unlock(&r->lock);

	if (!up) {
		ast_log(LOG_WARNING, "ED137: radio '%s' has no session\n", r->name);
		return -1;
	}
	ast_verb(3, "ED137: channel %s listens to radio %s (%d listener(s))\n",
		ast_channel_name(ast), r->name, listeners);
	ed137_call_event(r, ast, pvt, "attached");
	ast_queue_control(ast, AST_CONTROL_ANSWER);
	return 0;
}

/*! \brief The channel goes away: release its PTT, detach from RX, free the private data. */
static int ed137_hangup(struct ast_channel *ast)
{
	struct ed137_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ed137_radio *r;

	if (!pvt) {
		return 0;
	}
	r = pvt->radio;
	if (r) {
		ast_mutex_lock(&r->lock);
		if (r->ptt_keyed && (r->ptt_keyer == pvt || !strcmp(r->ptt_owner, ast_channel_name(ast)))) {
			ed137_ptt_release_locked(r, "keyer hung up");
		}
		ast_mutex_unlock(&r->lock);
		ast_mutex_lock(&r->lock);
		if (pvt->attached) {
			ed137_call_event(r, ast, pvt, "detached");
			AST_LIST_REMOVE(&r->listeners, pvt, entry);
			pvt->attached = 0;
			r->listeners_count--;
		}
		ast_mutex_unlock(&r->lock);
	}
	ast_channel_set_fd(ast, 0, -1);
	ed137_pvt_free(pvt);
	ast_channel_tech_pvt_set(ast, NULL);
	ast_atomic_fetchadd_int(&active_channels, -1);
	return 0;
}

/*! \brief Parse "<radio>[/<options>]" and create a channel for an up session. */
static struct ast_channel *ed137_request(const char *type,
	struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, int *cause)
{
	char *parse;
	struct ed137_radio *r;
	struct ed137_pvt *pvt = NULL;
	struct ast_channel *chan = NULL;
	struct ast_format_cap *caps = NULL;
	struct ast_format *fmt = NULL;
	int fds[2];
	int ed137_cause = AST_CAUSE_FAILURE;
	int up;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(radio);
		AST_APP_ARG(options);
	);

	(void)type;
	(void)cap;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "ED137: destination is required, e.g. Dial(ED137/station1)\n");
		goto failure;
	}
	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, '/');
	if (ast_strlen_zero(args.radio)) {
		ast_log(LOG_ERROR, "ED137: radio name is empty in '%s'\n", data);
		goto failure;
	}

	r = ed137_radio_find(args.radio);
	if (!r) {
		ast_log(LOG_WARNING, "ED137: no radio '%s' in ed137.conf\n", args.radio);
		ed137_cause = AST_CAUSE_UNALLOCATED;
		goto failure;
	}

	ast_mutex_lock(&r->lock);
	up = (r->inv.state == ED137_INV_UP);
	ast_mutex_unlock(&r->lock);
	if (!up) {
		ast_log(LOG_WARNING, "ED137: radio '%s' has no session\n", r->name);
		ed137_cause = AST_CAUSE_UNREGISTERED;
		goto failure;
	}

	/* The radio negotiates PCMA (PT 8) in the dump, so the channel is fixed to A-law. */
	fmt = ast_format_cache_get("alaw");
	if (!fmt) {
		ast_log(LOG_ERROR, "ED137: format 'alaw' is not available\n");
		goto failure;
	}
	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		goto failure;
	}
	ast_format_cap_append(caps, fmt, 0);

	if (pipe(fds) < 0) {
		ast_log(LOG_ERROR, "ED137: pipe() failed: %s\n", strerror(errno));
		goto failure;
	}
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
	fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);

	pvt = ast_calloc(1, sizeof(*pvt));
	if (!pvt) {
		close(fds[0]);
		close(fds[1]);
		goto failure;
	}
	pvt->radio = r;
	pvt->pipe_rd = fds[0];
	pvt->pipe_wr = fds[1];
	ao2_ref(fmt, +1);
	pvt->fmt = fmt;

	chan = ast_channel_alloc(1, AST_STATE_DOWN, "", "", "", "", "", assignedids,
		requestor, 0, "ED137/%s-%08x", r->name,
		(unsigned int)ast_atomic_fetchadd_int(&ed137_chan_seq, 1));
	if (!chan) {
		goto failure;
	}

	ast_channel_set_fd(chan, 0, pvt->pipe_rd);
	ast_channel_tech_set(chan, &ed137_channel_tech);
	ast_copy_string(pvt->peer, requestor ? ast_channel_name(requestor) : "", sizeof(pvt->peer));
	ast_channel_nativeformats_set(chan, caps);
	ast_channel_set_writeformat(chan, fmt);
	ast_channel_set_rawwriteformat(chan, fmt);
	ast_channel_set_readformat(chan, fmt);
	ast_channel_set_rawreadformat(chan, fmt);
	ast_channel_tech_pvt_set(chan, pvt);
	ast_atomic_fetchadd_int(&active_channels, 1);

	pbx_builtin_setvar_helper(chan, "ED137_RADIO", r->name);

	ast_channel_unlock(chan);

	ao2_ref(fmt, -1);
	ao2_ref(caps, -1);
	return chan;

failure:
	*cause = ed137_cause;
	ed137_pvt_free(pvt);
	ao2_cleanup(fmt);
	ao2_cleanup(caps);
	return NULL;
}

/* ---------------- CLI ---------------- */

static char *handle_cli_show_radios(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ed137_radio *r;
	int n = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ed137 show radios";
		e->usage =
			"Usage: ed137 show radios\n"
			"       Radios from ed137.conf: subscription, session, PTT and keepalive state.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "%-16s %-16s %-7s %-8s %-8s %-8s %-14s %-6s %-6s %-6s %-7s %-8s %-6s %-7s %-9s %s\n",
		"Name", "IP:Port", "Mode", "Repeat", "Sub", "Session", "Radio", "Peer", "PTT", "KA rx",
		"RX", "Frames", "PLC", "Listen", "Keepalive", "Owner");
	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		char ipport[96];

		snprintf(ipport, sizeof(ipport), "%s:%d", r->ip, r->sip_port);
		ast_mutex_lock(&r->lock);
		ast_cli(a->fd, "%-16s %-16s %-7s %-8s %-8s %-8s %-14s %-6d %-6s %-6s %-7s %-8lu %-6lu %-7d %-9d %s\n",
			r->name, ipport, ed137_mode_name(r->mode), ed137_repeat_name(r->ptt_repeat),
			r->subscribe ? ed137_sub_name(r->sub.state) : "off",
			ed137_inv_name(r->inv.state),
			r->sub_state_seen[0] ? r->sub_state_seen : "-",
			r->peer_rtp_port,
			r->ptt_keyed ? "on" : "off",
			r->ka_rx_seen ? "seen" : "-",
			r->rx_active ? "open" : "closed",
			r->rx_frames,
			r->rx_plc,
			r->listeners_count,
			r->keepalive_ms,
			r->ptt_keyed ? r->ptt_owner : "-");
		ast_mutex_unlock(&r->lock);
		n++;
	}
	AST_RWLIST_UNLOCK(&ed137_radios);
	ast_cli(a->fd, "%d radio(s), %d ED137 channel(s)\n", n, active_channels);
	return CLI_SUCCESS;
}

static char *handle_cli_key(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ed137_radio *r;
	int on;
	enum ed137_key_result res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ed137 key";
		e->usage =
			"Usage: ed137 key <radio> on|off <owner>\n"
			"       Set or release PTT with the same arbitration as RADIO_KEY.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != e->args + 3) {
		return CLI_SHOWUSAGE;
	}
	if (!strcasecmp(a->argv[e->args + 1], "on")) {
		on = 1;
	} else if (!strcasecmp(a->argv[e->args + 1], "off")) {
		on = 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	r = ed137_radio_find(a->argv[e->args]);
	if (!r) {
		ast_cli(a->fd, "ED137KEYSTATUS: %s\n", ed137_key_status_name(ED137_KEY_NOTFOUND));
		return CLI_FAILURE;
	}
	res = ed137_radio_key(r, on, a->argv[e->args + 2]);
	ast_cli(a->fd, "ED137KEYSTATUS: %s\n", ed137_key_status_name(res));
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_ed137[] = {
	AST_CLI_DEFINE(handle_cli_show_radios, "Show ED-137 radios: sessions, PTT, keepalive"),
	AST_CLI_DEFINE(handle_cli_key, "Set or release PTT on an ED-137 radio"),
};

/* ---------------- application ---------------- */

static int ed137key_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	struct ed137_radio *r;
	enum ed137_key_result res;
	const char *owner;
	int on;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(radio);
		AST_APP_ARG(state);
		AST_APP_ARG(owner);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ED137Key: usage ED137Key(<radio>,on|off[,<owner>])\n");
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (!strcasecmp(args.state, "on")) {
		on = 1;
	} else if (!strcasecmp(args.state, "off")) {
		on = 0;
	} else {
		ast_log(LOG_WARNING, "ED137Key: state '%s' must be on or off\n", args.state);
		return -1;
	}
	owner = ast_strlen_zero(args.owner) ? ast_channel_name(chan) : args.owner;
	r = ed137_radio_find(args.radio);
	res = r ? ed137_radio_key_by(r, on, owner,
		ast_channel_tech(chan) == &ed137_channel_tech ? (struct ed137_pvt *)ast_channel_tech_pvt(chan) : NULL)
		: ED137_KEY_NOTFOUND;
	pbx_builtin_setvar_helper(chan, "ED137KEYSTATUS", ed137_key_status_name(res));
	return 0;
}

/* ---------------- AMI ---------------- */

/*! \brief AMI ED137Key: Radio, State (on|off), Channel (the ED137 channel of this radio that owns PTT). */
static int ed137_ami_key(struct mansession *s, const struct message *m)
{
	const char *radio = astman_get_header(m, "Radio");
	const char *state = astman_get_header(m, "State");
	const char *chan = astman_get_header(m, "Channel");
	struct ed137_radio *r;
	struct ast_channel *c;
	struct ed137_pvt *p;
	enum ed137_key_result res = ED137_KEY_NOCHANNEL;
	int on = 0;

	if (ast_strlen_zero(radio) || ast_strlen_zero(state) || ast_strlen_zero(chan)) {
		astman_send_error(s, m, "Radio, State and Channel are required");
		return 0;
	}
	if (!strcasecmp(state, "on")) {
		on = 1;
	} else if (!strcasecmp(state, "off")) {
		on = 0;
	} else {
		astman_send_error(s, m, "State must be on or off");
		return 0;
	}

	r = ed137_radio_find(radio);
	if (!r) {
		res = ED137_KEY_NOTFOUND;
	} else {
		c = ast_channel_get_by_name(chan);
		if (c) {
			if (ast_channel_tech(c) == &ed137_channel_tech) {
				p = (struct ed137_pvt *)ast_channel_tech_pvt(c);
				if (p && p->radio == r) {
					res = ed137_radio_key_by(r, on, chan, p);
				}
			}
			ast_channel_unref(c);
		}
	}
	if (res != ED137_KEY_OK) {
		astman_send_error_va(s, m, "%s", ed137_key_status_name(res));
	} else if (on) {
		astman_send_ack(s, m, "PTT on");
	} else {
		astman_send_ack(s, m, "PTT off");
	}
	return 0;
}

/*** DOCUMENTATION
	<configInfo name="chan_ed137" language="en_US">
		<synopsis>
			ED-137 radio channel driver (VCS side).
		</synopsis>
		<description>
			<para>Connects dialplan and PBX calls to ED-137 radios: SIP sessions (INVITE, re-INVITE,
			BYE) and RTP audio with the R2S keepalive. Each radio is a persistent session; a channel
			<literal>ED137/&lt;radio&gt;-N</literal> attaches to it for the duration of a call.</para>
			<para>Configuration is read from <filename>ed137.conf</filename>. Each section other than
			<literal>general</literal> describes one radio.</para>
		</description>
		<configFile name="ed137.conf">
			<configObject name="general">
				<configOption name="bindaddr"><synopsis>Local address for SIP from the radios.</synopsis></configOption>
				<configOption name="bindport"><synopsis>Local SIP port for the radio sessions (required).</synopsis></configOption>
				<configOption name="vcs_host"><synopsis>This VCS address as seen by the radios (required).</synopsis></configOption>
				<configOption name="vcs_uri"><synopsis>From URI; must be in the radio's Allowed URI list (required).</synopsis></configOption>
				<configOption name="expires"><synopsis>WG67KEY-IN subscription expiry in seconds.</synopsis></configOption>
				<configOption name="invite_timeout"><synopsis>Base INVITE retransmission timeout in seconds.</synopsis></configOption>
			</configObject>
			<configObject name="station">
				<synopsis>One section per radio. The section name is the channel name ED137/&lt;name&gt;.</synopsis>
				<configOption name="ip"><synopsis>Radio address (required).</synopsis></configOption>
				<configOption name="sip_port"><synopsis>Radio SIP port.</synopsis></configOption>
				<configOption name="uri"><synopsis>Radio Own URI (required).</synopsis></configOption>
				<configOption name="radio_rtp_port"><synopsis>Radio RTP port (RTP Port 1 on the station).</synopsis></configOption>
				<configOption name="local_rtp_port"><synopsis>Our RTP port for this radio (required).</synopsis></configOption>
				<configOption name="keepalive_ms"><synopsis>R2S keepalive period in milliseconds. Keep at 200 or less.</synopsis></configOption>
				<configOption name="mode"><synopsis>idle, rxonly or txrx. txrx allows transmission.</synopsis></configOption>
				<configOption name="subscribe"><synopsis>auto (default): WG67KEY-IN subscription (state reporting only). After a rejection it is off until the next session drop. yes: always. no: never.</synopsis></configOption>
				<configOption name="ptt_repeat"><synopsis>ignore or ack: repeated PTT from the owner.</synopsis></configOption>
				<configOption name="notify_cseq"><synopsis>accept or reject NOTIFY with a lower CSeq.</synopsis></configOption>
				<configOption name="rx_mute_on_tx"><synopsis>No RX to listeners while our PTT is held and for 0.5 s after.</synopsis></configOption>
				<configOption name="ptt_max_s"><synopsis>PTT is released after this many seconds (1..600).</synopsis></configOption>
				<configOption name="ptt_id"><synopsis>PTT-id 1..63 written in TxRx mode. One value per source that can key the radio.</synopsis></configOption>
				<configOption name="rx_detect"><synopsis>audio: RX open while audio arrives. squelch: RX follows the SQU bit.</synopsis></configOption>
				<configOption name="r2s"><synopsis>R2S: SDP lines, keepalive and liveness check.</synopsis></configOption>
				<configOption name="wg67"><synopsis>auto (default): send WG67-Version, and on 501 cause 2016 retry the INVITE without it and remember the answer for this radio. yes: always send it. no: never send it.</synopsis></configOption>
				<configOption name="liveness_ms"><synopsis>No packet from the radio for this long: session down (r2s only).</synopsis></configOption>
				<configOption name="tx_idle_s"><synopsis>PTT is released when the owner's audio stops for this long (1..60).</synopsis></configOption>
				<configOption name="session_grace_s"><synopsis>Calls stay up this many seconds when the session drops (for example a station version change). 0 ends the calls at once.</synopsis></configOption>
			</configObject>
		</configFile>
	</configInfo>
	<application name="ED137Key" language="en_US">
		<synopsis>
			Take or release the PTT of an ED-137 radio.
		</synopsis>
		<syntax>
			<parameter name="radio" required="true">
				<para>Radio name from ed137.conf.</para>
			</parameter>
			<parameter name="state" required="true">
				<enumlist>
					<enum name="on"/>
					<enum name="off"/>
				</enumlist>
			</parameter>
			<parameter name="owner">
				<para>Owner name. Default: the name of the channel running the application.
				An owner named ED137/... must be a live ED137 channel of this radio.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sets <variable>ED137KEYSTATUS</variable> to OK, BUSY, NOCHANNEL, NOSESSION,
			NOTFOUND or NOTOWNER.</para>
		</description>
	</application>
	<manager name="ED137Key" language="en_US">
		<synopsis>
			Take or release the PTT of an ED-137 radio by channel name.
		</synopsis>
		<syntax>
			<parameter name="Radio" required="true"/>
			<parameter name="State" required="true">
				<enumlist>
					<enum name="on"/>
					<enum name="off"/>
				</enumlist>
			</parameter>
			<parameter name="Channel" required="true">
				<para>Name of the ED137 channel of this radio that owns the PTT.</para>
			</parameter>
		</syntax>
		<description>
			<para>Response Success with PTT on or PTT off, or Error with the status in Message:
			NOCHANNEL, NOTFOUND, BUSY, NOSESSION or NOTOWNER.</para>
		</description>
	</manager>
	<manager name="ED137Status" language="en_US">
		<synopsis>
			Status of the ED-137 radios.
		</synopsis>
		<syntax>
			<parameter name="Radio">
				<para>Only this radio. Default: all radios.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns one ED137Status event per radio, then ED137StatusComplete: session, subscription, PTT and owner, listeners, RX, SQI and device state (ED137:&lt;radio&gt;).</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="ED137PTT">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>PTT of an ED-137 radio was taken or released.</synopsis>
			<syntax>
				<parameter name="Radio"/>
				<parameter name="State"><enumlist><enum name="on"/><enum name="off"/></enumlist></parameter>
				<parameter name="Owner"/>
				<parameter name="PttId"/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ED137RX">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>The receive path of an ED-137 radio opened or closed.</synopsis>
			<syntax>
				<parameter name="Radio"/>
				<parameter name="State"><enumlist><enum name="open"/><enum name="closed"/></enumlist></parameter>
				<parameter name="Ext"><para>First extension word of the last packet.</para></parameter>
				<parameter name="Sqi"><para>Signal quality index 0..31, -1 if absent.</para></parameter>
				<parameter name="SqiText"/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ED137SQI">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>The signal quality index of an ED-137 radio changed (at most once per second).</synopsis>
			<syntax>
				<parameter name="Radio"/>
				<parameter name="Sqi"/>
				<parameter name="SqiText"/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ED137CALL">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>An ED-137 channel attached to or detached from a radio.</synopsis>
			<syntax>
				<parameter name="Radio"/>
				<parameter name="State"><enumlist><enum name="attached"/><enum name="detached"/></enumlist></parameter>
				<parameter name="Channel"/>
				<parameter name="Peer"><para>Name of the requesting (Dial caller) channel, empty for Originate.</para></parameter>
				<parameter name="Linkedid"/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
***/

/*! \brief AMI ED137Status: one ED137Status event per radio inside a list response. Optional Radio filter. */
static int ed137_ami_status(struct mansession *s, const struct message *m)
{
	const char *only = astman_get_header(m, "Radio");
	struct ed137_radio *r;
	int count = 0;

	astman_send_listack(s, m, "ED137 radio status follows", "start");
	AST_RWLIST_RDLOCK(&ed137_radios);
	AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
		struct ed137_pvt *p;
		char sqi[80];
		int listeners = 0;

		if (!ast_strlen_zero(only) && strcmp(only, r->name)) {
			continue;
		}
		ast_mutex_lock(&r->lock);
		AST_LIST_TRAVERSE(&r->listeners, p, entry) {
			listeners++;
		}
		ed137_sqi_text(r->rx_sqi_valid ? r->rx_sqi : -1, sqi, sizeof(sqi));
		astman_append(s,
			"Event: ED137Status\r\n"
			"Radio: %s\r\n"
			"IP: %s\r\n"
			"Port: %d\r\n"
			"Mode: %s\r\n"
			"Session: %s\r\n"
			"PeerRtpPort: %d\r\n"
			"R2S: %s\r\n"
			"WG67: %s\r\n"
			"Subscribe: %s\r\n"
			"SubscribeState: %s\r\n"
			"RadioState: %s\r\n"
			"PttKeyed: %s\r\n"
			"PttOwner: %s\r\n"
			"PttId: %d\r\n"
			"Listeners: %d\r\n"
			"RxOpen: %s\r\n"
			"Frames: %lu\r\n"
			"Sqi: %d\r\n"
			"SqiText: %s\r\n"
			"DeviceState: %s\r\n"
			"\r\n",
			r->name, r->ip, r->sip_port, ed137_mode_name(r->mode), ed137_inv_name(r->inv.state),
			r->peer_rtp_port, r->r2s ? "on" : "off", r->wg67 ? "on" : "off",
			r->subscribe ? "on" : "off", r->subscribe ? ed137_sub_name(r->sub.state) : "off",
			r->sub_state_seen[0] ? r->sub_state_seen : "-",
			r->ptt_keyed ? "on" : "off", r->ptt_keyed ? r->ptt_owner : "-", r->ptt_id,
			listeners, r->rx_active ? "open" : "closed", (unsigned long)r->rx_frames,
			r->rx_sqi_valid ? r->rx_sqi : -1, sqi, ed137_devstate_text(ed137_device_state_locked(r)));
		ast_mutex_unlock(&r->lock);
		count++;
	}
	AST_RWLIST_UNLOCK(&ed137_radios);
	astman_send_list_complete_start(s, m, "ED137StatusComplete", count);
	astman_send_list_complete_end(s);
	return 0;
}

/* ---------------- module ---------------- */

/*! \brief Graceful Asterisk shutdown: release every PTT and end every radio session with BYE.
 * Not run on "core stop now" or SIGTERM (see ast_register_atexit()). */
static void ed137_shutdown_cb(void)
{
    struct ed137_radio *r;
    char buf[ED137_SIP_MAX];
    size_t n;

    AST_RWLIST_RDLOCK(&ed137_radios);
    AST_RWLIST_TRAVERSE(&ed137_radios, r, entry) {
        ast_mutex_lock(&r->lock);
        if (r->ptt_keyed) {
            ed137_ptt_release_locked(r, "shutdown");
        }
        /* The timer thread does not run at shutdown: send the PTT-off packets here, before BYE. */
        while (r->tx_active && r->tx_final > 0 && r->inv.state == ED137_INV_UP) {
            ed137_tx_packet_locked(r);
            r->tx_final--;
        }
        r->tx_active = 0;
        r->tx_count = 0;
        if (r->inv.state == ED137_INV_UP) {
            const char *ruri = r->inv.remote_uri[0] ? r->inv.remote_uri : r->uri;

            r->inv.cseq++;
            n = ed137_build_request(buf, sizeof(buf), "BYE", ruri, r->uri, r->inv.remote_tag,
                r->inv.local_tag, r->inv.call_id, r->inv.cseq, "", "");
            ed137_udp_send(ed137_sip_fd, r->ip, r->sip_port, buf, n);
            r->inv.state = ED137_INV_IDLE;
        }
        ast_mutex_unlock(&r->lock);
    }
    AST_RWLIST_UNLOCK(&ed137_radios);
}

static int unload_module(void)
{
	ast_unregister_atexit(ed137_shutdown_cb);
	ast_cli_unregister_multiple(cli_ed137, ARRAY_LEN(cli_ed137));
	ast_unregister_application("ED137Key");
	ast_manager_unregister("ED137Key");
	ast_manager_unregister("ED137Status");
	ast_channel_unregister(&ed137_channel_tech);
	ao2_cleanup(ed137_channel_tech.capabilities);
	ed137_channel_tech.capabilities = NULL;
	ed137_stop_sip();
	ed137_free_radios();
	return 0;
}

static int load_module(void)
{
	if (ed137_load_config()) {
		ed137_free_radios();
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ed137_start_sip()) {
		ed137_stop_sip();
		ed137_free_radios();
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(ed137_channel_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ed137_stop_sip();
		ed137_free_radios();
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append_by_type(ed137_channel_tech.capabilities, AST_MEDIA_TYPE_AUDIO);
	if (ast_register_atexit(ed137_shutdown_cb)) {
		ast_log(LOG_WARNING, "ED137: could not register the shutdown handler\n");
	}
	if (ast_channel_register(&ed137_channel_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", ED137_TECH_TYPE);
		ao2_ref(ed137_channel_tech.capabilities, -1);
		ed137_channel_tech.capabilities = NULL;
		ed137_stop_sip();
		ed137_free_radios();
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_cli_register_multiple(cli_ed137, ARRAY_LEN(cli_ed137));
	if (ast_manager_register_xml("ED137Status", EVENT_FLAG_CALL, ed137_ami_status)) {
		ast_log(LOG_WARNING, "ED137: AMI action ED137Status not registered\n");
	}
	if (ast_manager_register_xml("ED137Key", EVENT_FLAG_CALL, ed137_ami_key)) {
		ast_log(LOG_WARNING, "ED137: AMI action ED137Key not registered\n");
	}
	if (ast_register_application_xml("ED137Key", ed137key_exec)) {
		ast_log(LOG_WARNING, "ED137: application ED137Key not registered\n");
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "ED-137 Radio Channel Driver",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
