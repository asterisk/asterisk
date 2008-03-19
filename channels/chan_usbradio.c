/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2007, Jim Dixon
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Steve Henke, W9SH  <w9sh@arrl.net>
 * Based upon work by Mark Spencer <markster@digium.com> and Luigi Rizzo
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
 * \brief Channel driver for CM108 USB Cards with Radio Interface
 *
 * \author Jim Dixon  <jim@lambdatel.com>
 * \author Steve Henke  <w9sh@arrl.net>
 *
 * \par See also
 * \arg \ref Config_usbradio
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>asound</depend>
	<depend>usb</depend>
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <math.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <usb.h>
#include <alsa/asoundlib.h>

#define CHAN_USBRADIO           1

#define DEBUG_USBRADIO          0	
#define DEBUG_CAPTURES	 		1
		
#define DEBUG_CAP_RX_OUT		0   		
#define DEBUG_CAP_TX_OUT	    0			

#define DEBUG_FILETEST			0			 

#define RX_CAP_RAW_FILE			"/tmp/rx_cap_in.pcm"
#define RX_CAP_TRACE_FILE		"/tmp/rx_trace.pcm"
#define RX_CAP_OUT_FILE			"/tmp/rx_cap_out.pcm"

#define TX_CAP_RAW_FILE			"/tmp/tx_cap_in.pcm"
#define TX_CAP_TRACE_FILE		"/tmp/tx_trace.pcm"
#define TX_CAP_OUT_FILE			"/tmp/tx_cap_out.pcm"

#define	MIXER_PARAM_MIC_PLAYBACK_SW "Mic Playback Switch"
#define MIXER_PARAM_MIC_PLAYBACK_VOL "Mic Playback Volume"
#define	MIXER_PARAM_MIC_CAPTURE_SW "Mic Capture Switch"
#define	MIXER_PARAM_MIC_CAPTURE_VOL "Mic Capture Volume"
#define	MIXER_PARAM_MIC_BOOST "Auto Gain Control"
#define	MIXER_PARAM_SPKR_PLAYBACK_SW "Speaker Playback Switch"
#define	MIXER_PARAM_SPKR_PLAYBACK_VOL "Speaker Playback Volume"

#include "./xpmr/xpmr.h"

#if 0
#define traceusb1(a, ...) ast_debug(4, a __VA_ARGS__)
#else
#define traceusb1(a, ...)
#endif

#if 0
#define traceusb2(a, ...) ast_debug(4, a __VA_ARGS__)
#else
#define traceusb2(a, ...)
#endif

#ifdef __linux
#include <linux/soundcard.h>
#elif defined(__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/endian.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/dsp.h"

#define C108_VENDOR_ID		0x0d8c
#define C108_PRODUCT_ID  	0x000c
#define C108_HID_INTERFACE	3

#define HID_REPORT_GET		0x01
#define HID_REPORT_SET		0x09

#define HID_RT_INPUT		0x01
#define HID_RT_OUTPUT		0x02

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = "",
};
static struct ast_jb_conf global_jbconf;

/*! 
 * usbradio.conf parameters are
START_CONFIG

[general]
    ; General config options, with default values shown.
    ; You should use one section per device, with [general] being used
    ; for the device.
    ;
    ;
    ; debug = 0x0		; misc debug flags, default is 0

	; Set the device to use for I/O
	; devicenum = 0
	; Set hardware type here
	; hdwtype=0               ; 0=limey, 1=sph

	; rxboostset=0          ; no rx gain boost
	; rxctcssrelax=1        ; reduce talkoff from radios w/o CTCSS Tx HPF
	; rxctcssfreq=100.0      ; rx ctcss freq in floating point. must be in table
	; txctcssfreq=100.0      ; tx ctcss freq, any frequency permitted

	; carrierfrom=dsp     ;no,usb,usbinvert,dsp,vox
	; ctcssfrom=dsp       ;no,usb,dsp

	; rxdemod=flat            ; input type from radio: no,speaker,flat
	; txprelim=yes            ; output is pre-emphasised and limited
	; txtoctype=no            ; no,phase,notone

	; txmixa=composite        ;no,voice,tone,composite,auxvoice
	; txmixb=no               ;no,voice,tone,composite,auxvoice

	; invertptt=0

    ;------------------------------ JITTER BUFFER CONFIGURATION --------------------------
    ; jbenable = yes              ; Enables the use of a jitterbuffer on the receiving side of an
                                  ; USBRADIO channel. Defaults to "no". An enabled jitterbuffer will
                                  ; be used only if the sending side can create and the receiving
                                  ; side can not accept jitter. The USBRADIO channel can't accept jitter,
                                  ; thus an enabled jitterbuffer on the receive USBRADIO side will always
                                  ; be used if the sending side can create jitter.

    ; jbmaxsize = 200             ; Max length of the jitterbuffer in milliseconds.

    ; jbresyncthreshold = 1000    ; Jump in the frame timestamps over which the jitterbuffer is
                                  ; resynchronized. Useful to improve the quality of the voice, with
                                  ; big jumps in/broken timestamps, usualy sent from exotic devices
                                  ; and programs. Defaults to 1000.

    ; jbimpl = fixed              ; Jitterbuffer implementation, used on the receiving side of an USBRADIO
                                  ; channel. Two implementations are currenlty available - "fixed"
                                  ; (with size always equals to jbmax-size) and "adaptive" (with
                                  ; variable size, actually the new jb of IAX2). Defaults to fixed.

    ; jblog = no                  ; Enables jitterbuffer frame logging. Defaults to "no".
    ;-----------------------------------------------------------------------------------


END_CONFIG

 */

/*!
 * The following parameters are used in the driver:
 *
 *  FRAME_SIZE	the size of an audio frame, in samples.
 *		160 is used almost universally, so you should not change it.
 *
 *  FRAGS	the argument for the SETFRAGMENT ioctl.
 *		Overridden by the 'frags' parameter in usbradio.conf
 *
 *		Bits 0-7 are the base-2 log of the device's block size,
 *		bits 16-31 are the number of blocks in the driver's queue.
 *		There are a lot of differences in the way this parameter
 *		is supported by different drivers, so you may need to
 *		experiment a bit with the value.
 *		A good default for linux is 30 blocks of 64 bytes, which
 *		results in 6 frames of 320 bytes (160 samples).
 *		FreeBSD works decently with blocks of 256 or 512 bytes,
 *		leaving the number unspecified.
 *		Note that this only refers to the device buffer size,
 *		this module will then try to keep the lenght of audio
 *		buffered within small constraints.
 *
 *  QUEUE_SIZE	The max number of blocks actually allowed in the device
 *		driver's buffer, irrespective of the available number.
 *		Overridden by the 'queuesize' parameter in usbradio.conf
 *
 *		Should be >=2, and at most as large as the hw queue above
 *		(otherwise it will never be full).
 */

#define FRAME_SIZE	160
#define	QUEUE_SIZE	20

#if defined(__FreeBSD__)
#define	FRAGS	0x8
#else
#define	FRAGS	( ( (6 * 5) << 16 ) | 0xc )
#endif

/*
 * XXX text message sizes are probably 256 chars, but i am
 * not sure if there is a suitable definition anywhere.
 */
#define TEXT_SIZE	256

#if 0
#define	TRYOPEN	1				/* try to open on startup */
#endif
#define	O_CLOSE	0x444			/* special 'close' mode for device */
/* Which device to use */
#if defined( __OpenBSD__ ) || defined( __NetBSD__ )
#define DEV_DSP "/dev/audio"
#else
#define DEV_DSP "/dev/dsp"
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static char *config = "usbradio.conf";	/* default config file */
static char *config1 = "usbradio_tune.conf";    /* tune config file */

static FILE *frxcapraw = NULL, *frxcaptrace = NULL, *frxoutraw = NULL;
static FILE *ftxcapraw = NULL, *ftxcaptrace = NULL, *ftxoutraw = NULL;

static int usbradio_debug;
#if 0 /* maw asdf sph */
static int usbradio_debug_level = 0;
#endif

enum {RX_AUDIO_NONE,RX_AUDIO_SPEAKER,RX_AUDIO_FLAT};
enum {CD_IGNORE,CD_XPMR_NOISE,CD_XPMR_VOX,CD_HID,CD_HID_INVERT};
enum {SD_IGNORE,SD_HID,SD_HID_INVERT,SD_XPMR};    				 /* no,external,externalinvert,software */
enum {RX_KEY_CARRIER,RX_KEY_CARRIER_CODE};
enum {TX_OUT_OFF,TX_OUT_VOICE,TX_OUT_LSD,TX_OUT_COMPOSITE,TX_OUT_AUX};
enum {TOC_NONE,TOC_PHASE,TOC_NOTONE};

/*	DECLARE STRUCTURES */

/*
 * descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_usbradio_pvt {
	struct chan_usbradio_pvt *next;

	char *name;

	int total_blocks;           /* total blocks in the output device */
	int sounddev;
	enum { M_UNSET, M_FULL, M_READ, M_WRITE } duplex;
	i16 cdMethod;
	int autoanswer;
	int autohangup;
	int hookstate;
	unsigned int queuesize;		/* max fragments in queue */
	unsigned int frags;			/* parameter for SETFRAGMENT */

	int warned;                 /* various flags used for warnings */
#define WARN_used_blocks	1
#define WARN_speed		2
#define WARN_frag		4
	int w_errors;               /* overfull in the write path */
	struct timeval lastopen;

	int overridecontext;
	int mute;

	/* boost support. BOOST_SCALE * 10 ^(BOOST_MAX/20) must
	 * be representable in 16 bits to avoid overflows.
	 */
#define	BOOST_SCALE	(1<<9)
#define	BOOST_MAX	40          /* slightly less than 7 bits */
	int boost;                  /* input boost, scaled by BOOST_SCALE */
	char devicenum;
	int spkrmax;
	int micmax;

	pthread_t sthread;
	pthread_t hidthread;

	int     stophid;
	struct ast_channel *owner;
	char    ext[AST_MAX_EXTENSION];
	char    ctx[AST_MAX_CONTEXT];
	char    language[MAX_LANGUAGE];
	char    cid_name[256];          /* XXX */
	char    cid_num[256];           /* XXX */
	char    mohinterpret[MAX_MUSICCLASS];

	/* buffers used in usbradio_write, 2 per int by 2 channels by 6 times oversampling (48KS/s) */
	char    usbradio_write_buf[FRAME_SIZE * 2 * 2 * 6];
	char    usbradio_write_buf_1[FRAME_SIZE * 2 * 2 * 6];

	int     usbradio_write_dst;
	/* buffers used in usbradio_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char    usbradio_read_buf[FRAME_SIZE * (2 * 12) + AST_FRIENDLY_OFFSET];
	char    usbradio_read_buf_8k[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	int     readpos;         /* read position above */
	struct ast_frame read_f; /* returned by usbradio_read */
	

	char    debuglevel;
	char    radioduplex;

	char    lastrx;
	char    rxhidsq;
	char    rxcarrierdetect; /*!< status from pmr channel */
	char    rxctcssdecode;   /*!< status from pmr channel */

	char    rxkeytype;
	char    rxkeyed;         /*!< indicates rx signal present */

	char    lasttx;
	char    txkeyed;         /*! tx key request from upper layers  */
	char    txchankey;
	char    txtestkey;

	time_t  lasthidtime;
    struct ast_dsp *dsp;

	t_pmr_chan *pmrChan;

	char    rxcpusaver;
	char    txcpusaver;

	char    rxdemod;
	float   rxgain;
	char    rxcdtype;
	char    rxsdtype;
	int     rxsquelchadj;    /*!< this copy needs to be here for initialization */
	char    txtoctype;

	char    txprelim;
	float   txctcssgain;
	char    txmixa;
	char    txmixb;

	char    invertptt;

	char    rxctcssrelax;
	float   rxctcssgain;
	float   rxctcssfreq;
	float   txctcssfreq;

	int     rxmixerset;
	int     rxboostset;
	float   rxvoiceadj;
	float   rxctcssadj;
	int     txmixaset;
	int     txmixbset;
	int     txctcssadj;

	int    	hdwtype;
	int     hid_gpio_ctl;
	int     hid_gpio_ctl_loc;
	int     hid_io_cor;
	int     hid_io_cor_loc;
	int     hid_io_ctcss;
	int     hid_io_ctcss_loc;
	int     hid_io_ptt;
	int     hid_gpio_loc;

	struct {
		unsigned rxcapraw:1;
		unsigned txcapraw:1;
		unsigned txcap2:1;
		unsigned rxcap2:1;
	} b;
};

/* maw add additional defaults !!! */
static struct chan_usbradio_pvt usbradio_default = {
	.sounddev = -1,
	.duplex = M_UNSET,			/* XXX check this */
	.autoanswer = 1,
	.autohangup = 1,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.ext = "s",
	.ctx = "default",
	.readpos = AST_FRIENDLY_OFFSET,	/* start here on reads */
	.lastopen = { 0, 0 },
	.boost = BOOST_SCALE,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static void store_txtoctype(struct chan_usbradio_pvt *o, const char *s);
static int  hidhdwconfig(struct chan_usbradio_pvt *o);
static int  set_txctcss_level(struct chan_usbradio_pvt *o);
static void pmrdump(struct chan_usbradio_pvt *o);
static void mult_set(struct chan_usbradio_pvt *o);
static int  mult_calc(int value);
static void mixer_write(struct chan_usbradio_pvt *o);
static void tune_rxinput(struct chan_usbradio_pvt *o);
static void tune_rxvoice(struct chan_usbradio_pvt *o);
static void tune_rxctcss(struct chan_usbradio_pvt *o);
static void tune_txoutput(struct chan_usbradio_pvt *o, int value);
static void tune_write(struct chan_usbradio_pvt *o);

static char *usbradio_active;	 /* the active device */

static int  setformat(struct chan_usbradio_pvt *o, int mode);

static struct ast_channel *usbradio_request(const char *type, int format, void *data
, int *cause);
static int usbradio_digit_begin(struct ast_channel *c, char digit);
static int usbradio_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int usbradio_text(struct ast_channel *c, const char *text);
static int usbradio_hangup(struct ast_channel *c);
static int usbradio_answer(struct ast_channel *c);
static struct ast_frame *usbradio_read(struct ast_channel *chan);
static int usbradio_call(struct ast_channel *c, char *dest, int timeout);
static int usbradio_write(struct ast_channel *chan, struct ast_frame *f);
static int usbradio_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int usbradio_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

#if	DEBUG_FILETEST == 1
static int RxTestIt(struct chan_usbradio_pvt *o);
#endif

static char tdesc[] = "USB (CM108) Radio Channel Driver";

static const struct ast_channel_tech usbradio_tech = {
	.type = "Radio",
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = usbradio_request,
	.send_digit_begin = usbradio_digit_begin,
	.send_digit_end = usbradio_digit_end,
	.send_text = usbradio_text,
	.hangup = usbradio_hangup,
	.answer = usbradio_answer,
	.read = usbradio_read,
	.call = usbradio_call,
	.write = usbradio_write,
	.indicate = usbradio_indicate,
	.fixup = usbradio_fixup,
};

/* Call with:  devnum: alsa major device number, param: ascii Formal
Parameter Name, val1, first or only value, val2 second value, or 0 
if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.

Note: must add -lasound to end of linkage */

static int amixer_max(int devnum,char *param)
{
	int	rv,type;
	char str[15];
	snd_hctl_t *hctl;
	snd_ctl_elem_id_t *id;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *info;

	snprintf(str, sizeof(str), "hw:%d", devnum);
	if (snd_hctl_open(&hctl, str, 0))
		return -1;
	snd_hctl_load(hctl);
	id = alloca(snd_ctl_elem_id_sizeof());
	memset(id, 0, snd_ctl_elem_id_sizeof());
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, param);  
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem) {
		snd_hctl_close(hctl);
		return -1;
	}
	info = alloca(snd_ctl_elem_info_sizeof());
	memset(info, 0, snd_ctl_elem_info_sizeof());
	snd_hctl_elem_info(elem,info);
	type = snd_ctl_elem_info_get_type(info);
	rv = 0;
	switch (type) {
	case SND_CTL_ELEM_TYPE_INTEGER:
		rv = snd_ctl_elem_info_get_max(info);
		break;
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		rv = 1;
		break;
	}
	snd_hctl_close(hctl);
	return(rv);
}

/*! \brief Call with:  devnum: alsa major device number, param: ascii Formal
Parameter Name, val1, first or only value, val2 second value, or 0 
if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.

Note: must add -lasound to end of linkage */

static int setamixer(int devnum, char *param, int v1, int v2)
{
	int	type;
	char str[15];
	snd_hctl_t *hctl;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_value_t *control;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *info;

	snprintf(str, sizeof(str), "hw:%d", devnum);
	if (snd_hctl_open(&hctl, str, 0))
		return -1;
	snd_hctl_load(hctl);
	id = alloca(snd_ctl_elem_id_sizeof());
	memset(id, 0, snd_ctl_elem_id_sizeof());
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, param);  
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem) {
		snd_hctl_close(hctl);
		return -1;
	}
	info = alloca(snd_ctl_elem_info_sizeof());
	memset(info, 0, snd_ctl_elem_info_sizeof());
	snd_hctl_elem_info(elem,info);
	type = snd_ctl_elem_info_get_type(info);
	control = alloca(snd_ctl_elem_value_sizeof());
	memset(control, 0, snd_ctl_elem_value_sizeof());
	snd_ctl_elem_value_set_id(control, id);    
	switch (type) {
	case SND_CTL_ELEM_TYPE_INTEGER:
		snd_ctl_elem_value_set_integer(control, 0, v1);
		if (v2 > 0) snd_ctl_elem_value_set_integer(control, 1, v2);
		break;
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
		break;
	}
	if (snd_hctl_elem_write(elem, control)) {
		snd_hctl_close(hctl);
		return(-1);
	}
	snd_hctl_close(hctl);
	return 0;
}

static void hid_set_outputs(struct usb_dev_handle *handle,
         unsigned char *outputs)
{
	usb_control_msg(handle,
		USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
		HID_REPORT_SET,
		0 + (HID_RT_OUTPUT << 8),
		C108_HID_INTERFACE,
		(char *)outputs, 4, 5000);
}

static void hid_get_inputs(struct usb_dev_handle *handle,
         unsigned char *inputs)
{
	usb_control_msg(handle,
		USB_ENDPOINT_IN + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
		HID_REPORT_GET,
		0 + (HID_RT_INPUT << 8),
		C108_HID_INTERFACE,
		(char *)inputs, 4, 5000);
}

static struct usb_device *hid_device_init(void)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;

	usb_init();
	usb_find_busses();
	usb_find_devices();
	for (usb_bus = usb_busses; usb_bus; usb_bus = usb_bus->next) {
		for (dev = usb_bus->devices; dev; dev = dev->next) {
			if ((dev->descriptor.idVendor == C108_VENDOR_ID) && (dev->descriptor.idProduct == C108_PRODUCT_ID))
				return dev;
		}
	}
	return NULL;
}

static int hidhdwconfig(struct chan_usbradio_pvt *o)
{
	if (o->hdwtype == 1) {         /*sphusb */
		o->hid_gpio_ctl     =  0x08; /* set GPIO4 to output mode */
		o->hid_gpio_ctl_loc =  2;    /* For CTL of GPIO */
		o->hid_io_cor       =  4;    /* GPIO3 is COR */
		o->hid_io_cor_loc   =  1;    /* GPIO3 is COR */
		o->hid_io_ctcss     =  2;    /* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc =  1;    /* is GPIO 2 */
		o->hid_io_ptt       =  8;    /* GPIO 4 is PTT */
		o->hid_gpio_loc     =  1;    /* For ALL GPIO */
	} else if (o->hdwtype == 0) {  /* dudeusb */
		o->hid_gpio_ctl     =  0x0c;/* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc =  2;    /* For CTL of GPIO */
		o->hid_io_cor       =  2;    /* VOLD DN is COR */
		o->hid_io_cor_loc   =  0;    /* VOL DN COR */
		o->hid_io_ctcss     =  2;    /* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc =  1;    /* is GPIO 2 */
		o->hid_io_ptt       =  4;    /* GPIO 3 is PTT */
		o->hid_gpio_loc     =  1;    /* For ALL GPIO */
	} else if (o->hdwtype == 3) {  /* custom version */
		o->hid_gpio_ctl     =  0x0c; /* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc =  2;    /* For CTL of GPIO */
		o->hid_io_cor       =  2;    /* VOLD DN is COR */
		o->hid_io_cor_loc   =  0;    /* VOL DN COR */
		o->hid_io_ctcss     =  2;    /* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc =  1;    /* is GPIO 2 */
		o->hid_io_ptt       =  4;    /* GPIO 3 is PTT */
		o->hid_gpio_loc     =  1;    /* For ALL GPIO */
	}

	return 0;
}


static void *hidthread(void *arg)
{
	unsigned char buf[4], keyed;
	char lastrx, txtmp;
	struct usb_device *usb_dev;
	struct usb_dev_handle *usb_handle;
	struct chan_usbradio_pvt *o = arg;

	usb_dev = hid_device_init();
	if (usb_dev == NULL) {
		ast_log(LOG_ERROR, "USB HID device not found\n");
		pthread_exit(NULL);
	}
	usb_handle = usb_open(usb_dev);
	if (usb_handle == NULL) {
		ast_log(LOG_ERROR, "Not able to open USB device\n");
		pthread_exit(NULL);
	}
	if (usb_claim_interface(usb_handle, C108_HID_INTERFACE) < 0) {
	        if (usb_detach_kernel_driver_np(usb_handle, C108_HID_INTERFACE) < 0) {
			ast_log(LOG_ERROR, "Not able to detach the USB device\n");
			pthread_exit(NULL);
		}
		if (usb_claim_interface(usb_handle, C108_HID_INTERFACE) < 0) {
			ast_log(LOG_ERROR, "Not able to claim the USB device\n");
			pthread_exit(NULL);
		}
	}
	memset(buf, 0, sizeof(buf));
	buf[2] = o->hid_gpio_ctl;
	buf[1] = 0;
	hid_set_outputs(usb_handle, buf);
	traceusb1("hidthread: Starting normally!!\n");
	lastrx = 0;
	while (!o->stophid) {
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		hid_get_inputs(usb_handle, buf);
		keyed = !(buf[o->hid_io_cor_loc] & o->hid_io_cor);
		if (keyed != o->rxhidsq) {
			if (o->debuglevel)
				ast_log(LOG_NOTICE, "chan_usbradio() hidthread: update rxhidsq = %d\n", keyed);
			o->rxhidsq = keyed;		 
		}

		/* if change in tx stuff */
		txtmp = 0;
		if (o->txkeyed || o->txchankey || o->txtestkey || o->pmrChan->txPttOut)
			txtmp = 1;
		
		if (o->lasttx != txtmp) {
			o->lasttx = txtmp;
			if (o->debuglevel)
				ast_log(LOG_NOTICE, "hidthread: tx set to %d\n", txtmp);
			buf[o->hid_gpio_loc] = 0;
			if (txtmp)
				buf[o->hid_gpio_loc] = o->hid_io_ptt;
			buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
			hid_set_outputs(usb_handle, buf);
		}

		time(&o->lasthidtime);
		usleep(50000);
	}
	buf[o->hid_gpio_loc] = 0;
	if (o->invertptt)
		buf[o->hid_gpio_loc] = o->hid_io_ptt;
	buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
	hid_set_outputs(usb_handle, buf);
	pthread_exit(0);
}

/*! \brief
 * returns a pointer to the descriptor with the given name
 */
static struct chan_usbradio_pvt *find_desc(char *dev)
{
	struct chan_usbradio_pvt *o = NULL;

	if (!dev)
		ast_log(LOG_WARNING, "null dev\n");

	for (o = usbradio_default.next; o && o->name && dev && strcmp(o->name, dev) != 0; o = o->next);

	if (!o)
		ast_log(LOG_WARNING, "could not find <%s>\n", dev ? dev : "--no-device--");

	return o;
}

/*! \brief
 * split a string in extension-context, returns pointers to malloc'ed
 * strings.
 * If we do not have 'overridecontext' then the last @ is considered as
 * a context separator, and the context is overridden.
 * This is usually not very necessary as you can play with the dialplan,
 * and it is nice not to need it because you have '@' in SIP addresses.
 * Return value is the buffer address.
 */
#if	0
static char *ast_ext_ctx(const char *src, char **ext, char **ctx)
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (ext == NULL || ctx == NULL)
		return NULL;			/* error */

	*ext = *ctx = NULL;

	if (src && *src != '\0')
		*ext = ast_strdup(src);

	if (*ext == NULL)
		return NULL;

	if (!o->overridecontext) {
		/* parse from the right */
		*ctx = strrchr(*ext, '@');
		if (*ctx)
			*(*ctx)++ = '\0';
	}

	return *ext;
}
#endif

/*! \brief
 * Returns the number of blocks used in the audio output channel
 */
static int used_blocks(struct chan_usbradio_pvt *o)
{
	struct audio_buf_info info;

	if (ioctl(o->sounddev, SNDCTL_DSP_GETOSPACE, &info)) {
		if (!(o->warned & WARN_used_blocks)) {
			ast_log(LOG_WARNING, "Error reading output space\n");
			o->warned |= WARN_used_blocks;
		}
		return 1;
	}

	if (o->total_blocks == 0) {
		ast_debug(4, "fragtotal %d size %d avail %d\n", info.fragstotal, info.fragsize, info.fragments);
		o->total_blocks = info.fragments;
	}

	return o->total_blocks - info.fragments;
}

/*! \brief Write an exactly FRAME_SIZE sized frame */
static int soundcard_writeframe(struct chan_usbradio_pvt *o, short *data)
{
	int res;

	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	if (o->sounddev < 0)
		return 0;				/* not fatal */
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * XXX in some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) {	/* no room to write a block */
		if (o->w_errors++ == 0 && (usbradio_debug & 0x4))
			ast_log(LOG_WARNING, "write: used %d blocks (%d)\n", res, o->w_errors);
		return 0;
	}
	o->w_errors = 0;

	return write(o->sounddev, ((void *) data), FRAME_SIZE * 2 * 12);
}

/*
 * reset and close the device if opened,
 * then open and initialize it in the desired mode,
 * trigger reads and writes so we can start using it.
 */
static int setformat(struct chan_usbradio_pvt *o, int mode)
{
	int fmt, desired, res, fd;
	char device[20];

	if (o->sounddev >= 0) {
		ioctl(o->sounddev, SNDCTL_DSP_RESET, 0);
		close(o->sounddev);
		o->duplex = M_UNSET;
		o->sounddev = -1;
	}
	if (mode == O_CLOSE)		/* we are done */
		return 0;
	if (ast_tvdiff_ms(ast_tvnow(), o->lastopen) < 1000)
		return -1;				/* don't open too often */
	o->lastopen = ast_tvnow();
	strcpy(device, "/dev/dsp");
	if (o->devicenum)
		snprintf(device + strlen("/dev/dsp"), sizeof(device) - strlen("/dev/dsp"), "%d", o->devicenum);
	fd = o->sounddev = open(device, mode | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to re-open DSP device %d: %s\n", o->devicenum, strerror(errno));
		return -1;
	}
	if (o->owner)
		o->owner->fds[0] = fd;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	fmt = AFMT_S16_LE;
#else
	fmt = AFMT_S16_BE;
#endif
	res = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set format to 16-bit signed\n");
		return -1;
	}
	switch (mode) {
		case O_RDWR:
			res = ioctl(fd, SNDCTL_DSP_SETDUPLEX, 0);
			/* Check to see if duplex set (FreeBSD Bug) */
			res = ioctl(fd, SNDCTL_DSP_GETCAPS, &fmt);
			if (res == 0 && (fmt & DSP_CAP_DUPLEX)) {
				ast_verb(2, "Console is full duplex\n");
				o->duplex = M_FULL;
			};
			break;
		case O_WRONLY:
			o->duplex = M_WRITE;
			break;
		case O_RDONLY:
			o->duplex = M_READ;
			break;
	}

	fmt = 1;
	res = ioctl(fd, SNDCTL_DSP_STEREO, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	fmt = desired = 48000;							/* 8000 Hz desired */
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	if (fmt != desired) {
		if (!(o->warned & WARN_speed)) {
			ast_log(LOG_WARNING,
			    "Requested %d Hz, got %d Hz -- sound may be choppy\n",
			    desired, fmt);
			o->warned |= WARN_speed;
		}
	}
	/*
	 * on Freebsd, SETFRAGMENT does not work very well on some cards.
	 * Default to use 256 bytes, let the user override
	 */
	if (o->frags) {
		fmt = o->frags;
		res = ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
		if (res < 0) {
			if (!(o->warned & WARN_frag)) {
				ast_log(LOG_WARNING,
					"Unable to set fragment size -- sound may be choppy\n");
				o->warned |= WARN_frag;
			}
		}
	}
	/* on some cards, we need SNDCTL_DSP_SETTRIGGER to start outputting */
	res = PCM_ENABLE_INPUT | PCM_ENABLE_OUTPUT;
	res = ioctl(fd, SNDCTL_DSP_SETTRIGGER, &res);
	/* it may fail if we are in half duplex, never mind */
	return 0;
}

/*
 * some of the standard methods supported by channels.
 */
static int usbradio_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

static int usbradio_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verb(0, " << Console Received digit %c of duration %u ms >> \n", 
		digit, duration);
	return 0;
}

static int usbradio_text(struct ast_channel *c, const char *text)
{
	/* print received messages */
	ast_verb(0, " << Console Received text %s >> \n", text);
	return 0;
}

/*
 * handler for incoming calls. Either autoanswer, or start ringing
 */
static int usbradio_call(struct ast_channel *c, char *dest, int timeout)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;

	time(&o->lasthidtime);
	ast_pthread_create_background(&o->hidthread, NULL, hidthread, o);
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*
 * remote side answered the phone
 */
static int usbradio_answer(struct ast_channel *c)
{
	ast_setstate(c, AST_STATE_UP);

	return 0;
}

static int usbradio_hangup(struct ast_channel *c)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;

	c->tech_pvt = NULL;
	o->owner = NULL;
	ast_module_unref(ast_module_info->self);
	if (o->hookstate) {
		if (o->autoanswer || o->autohangup) {
			/* Assume auto-hangup too */
			o->hookstate = 0;
			setformat(o, O_CLOSE);
		}
	}
	o->stophid = 1;
	pthread_join(o->hidthread, NULL);
	return 0;
}


/* used for data coming from the network */
static int usbradio_write(struct ast_channel *c, struct ast_frame *f)
{
	int src,datalen;
	struct chan_usbradio_pvt *o = c->tech_pvt;

	traceusb2("usbradio_write() o->nosound=%d\n", o->nosound);	/*sph maw asdf */

	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */

	if (o->txkeyed || o->txtestkey)
		o->pmrChan->txPttIn = 1;
	else
		o->pmrChan->txPttIn = 0;

	#if DEBUG_CAPTURES == 1	/* to write input data to a file   datalen=320 */
	if (ftxcapraw && o->b.txcapraw) {
		i16 i, tbuff[f->datalen];
		for (i = 0; i < f->datalen; i += 2) {
			tbuff[i] = ((i16 *)(f->data))[i / 2];
			tbuff[i + 1] = o->txkeyed * M_Q13;
		}
		fwrite(tbuff, 2, f->datalen, ftxcapraw);
		/*fwrite(f->data,1,f->datalen,ftxcapraw); */
	}
	#endif

	PmrTx(o->pmrChan,(i16*)f->data,(i16*)o->usbradio_write_buf_1);

	#if 0	/* to write 48KS/s stereo data to a file */
	if (!ftxoutraw) ftxoutraw = fopen(TX_CAP_OUT_FILE,"w");
	if (ftxoutraw) fwrite(o->usbradio_write_buf_1,1,f->datalen * 2 * 6,ftxoutraw);
	#endif

	#if DEBUG_CAPTURES == 1
    if (o->b.txcap2 && ftxcaptrace)
		fwrite((o->pmrChan->ptxDebug), 1, FRAME_SIZE * 2 * 16, ftxcaptrace);
	#endif

	src = 0;					/* read position into f->data */
	datalen = f->datalen * 12;
	while (src < datalen) {
		/* Compute spare room in the buffer */
		int l = sizeof(o->usbradio_write_buf) - o->usbradio_write_dst;

		if (datalen - src >= l) {	/* enough to fill a frame */
			memcpy(o->usbradio_write_buf + o->usbradio_write_dst, o->usbradio_write_buf_1 + src, l);
			soundcard_writeframe(o, (short *) o->usbradio_write_buf);
			src += l;
			o->usbradio_write_dst = 0;
		} else {				/* copy residue */
			l = datalen - src;
			memcpy(o->usbradio_write_buf + o->usbradio_write_dst, o->usbradio_write_buf_1 + src, l);
			src += l;			/* but really, we are done */
			o->usbradio_write_dst += l;
		}
	}
	return 0;
}

static struct ast_frame *usbradio_read(struct ast_channel *c)
{
	int res;
	struct chan_usbradio_pvt *o = c->tech_pvt;
	struct ast_frame *f = &o->read_f, *f1;
	struct ast_frame wf = { AST_FRAME_CONTROL };
	time_t now;

	traceusb2("usbradio_read()\n");	/* sph maw asdf */

	if (o->lasthidtime) {
		time(&now);
		if ((now - o->lasthidtime) > 3) {
			ast_log(LOG_ERROR, "HID process has died or something!!\n");
			return NULL;
		}
	}
	if (o->lastrx && (!o->rxkeyed)) {
		o->lastrx = 0;
		wf.subclass = AST_CONTROL_RADIO_UNKEY;
		ast_queue_frame(o->owner, &wf);
	} else if ((!o->lastrx) && (o->rxkeyed)) {
		o->lastrx = 1;
		wf.subclass = AST_CONTROL_RADIO_KEY;
		ast_queue_frame(o->owner, &wf);
	}
	/* XXX can be simplified returning &ast_null_frame */
	/* prepare a NULL frame in case we don't have enough data to return */
	memset(f, 0, sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = usbradio_tech.type;

	res = read(o->sounddev, o->usbradio_read_buf + o->readpos, 
		sizeof(o->usbradio_read_buf) - o->readpos);
	if (res < 0)				/* audio data not ready, return a NULL frame */
		return f;

	o->readpos += res;
	if (o->readpos < sizeof(o->usbradio_read_buf))	/* not enough samples */
		return f;

	if (o->mute)
		return f;

	#if DEBUG_CAPTURES == 1
	if (o->b.rxcapraw && frxcapraw)
		fwrite((o->usbradio_read_buf + AST_FRIENDLY_OFFSET), 1, FRAME_SIZE * 2 * 2 * 6, frxcapraw);
	#endif

	#if 1
	PmrRx(         o->pmrChan,
		   (i16 *)(o->usbradio_read_buf + AST_FRIENDLY_OFFSET),
		   (i16 *)(o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET));

	#else
	static FILE *hInput;
	i16 iBuff[FRAME_SIZE * 2 * 6];

	o->pmrChan->b.rxCapture = 1;

	if(!hInput) {
		hInput = fopen("/usr/src/xpmr/testdata/rx_in.pcm", "r");
		if(!hInput) {
			ast_log(LOG_ERROR, " Input Data File Not Found.\n");
			return 0;
		}
	}

	if (0 == fread((void *)iBuff, 2, FRAME_SIZE * 2 * 6, hInput))
		exit;

	PmrRx(         o->pmrChan, 
		   (i16 *)iBuff,
		   (i16 *)(o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET));

	#endif

	#if 0
	if (!frxoutraw) frxoutraw = fopen(RX_CAP_OUT_FILE, "w");
    if (frxoutraw) fwrite((o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET), 1, FRAME_SIZE * 2, frxoutraw);
	#endif

	#if DEBUG_CAPTURES == 1
    if (frxcaptrace && o->b.rxcap2) fwrite((o->pmrChan->prxDebug), 1, FRAME_SIZE * 2 * 16, frxcaptrace);
	#endif

	if (o->rxcdtype == CD_HID && (o->pmrChan->rxExtCarrierDetect != o->rxhidsq))
		o->pmrChan->rxExtCarrierDetect = o->rxhidsq;
	if (o->rxcdtype == CD_HID_INVERT && (o->pmrChan->rxExtCarrierDetect == o->rxhidsq))
		o->pmrChan->rxExtCarrierDetect = !o->rxhidsq;
		
	if ( (o->rxcdtype == CD_HID && o->rxhidsq) ||
		 (o->rxcdtype == CD_HID_INVERT && !o->rxhidsq) ||
		 (o->rxcdtype == CD_XPMR_NOISE && o->pmrChan->rxCarrierDetect) ||
		 (o->rxcdtype == CD_XPMR_VOX && o->pmrChan->rxCarrierDetect) )
		res = 1;
	else
		res = 0;

	if (res != o->rxcarrierdetect) {
		o->rxcarrierdetect = res;
		if (o->debuglevel)
			ast_debug(4, "rxcarrierdetect = %d\n", res);
	}

	if (o->pmrChan->rxCtcss->decode != o->rxctcssdecode) {
		if (o->debuglevel)
			ast_debug(4, "rxctcssdecode = %d\n", o->pmrChan->rxCtcss->decode);
		o->rxctcssdecode = o->pmrChan->rxCtcss->decode;
	}

	if ( (  o->rxctcssfreq && (o->rxctcssdecode == o->pmrChan->rxCtcssIndex)) || 
		 ( !o->rxctcssfreq && o->rxcarrierdetect) ) 
		o->rxkeyed = 1;
	else
		o->rxkeyed = 0;


	o->readpos = AST_FRIENDLY_OFFSET;	/* reset read pointer for next frame */
	if (c->_state != AST_STATE_UP)	/* drop data if frame is not up */
		return f;
	/* ok we can build and deliver the frame to the caller */
	f->frametype = AST_FRAME_VOICE;
	f->subclass = AST_FORMAT_SLINEAR;
	f->samples = FRAME_SIZE;
	f->datalen = FRAME_SIZE * 2;
	f->data = o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET;
	if (o->boost != BOOST_SCALE) {	/* scale and clip values */
		int i, x;
		int16_t *p = (int16_t *) f->data;
		for (i = 0; i < f->samples; i++) {
			x = (p[i] * o->boost) / BOOST_SCALE;
			if (x > 32767)
				x = 32767;
			else if (x < -32768)
				x = -32768;
			p[i] = x;
		}
	}

	f->offset = AST_FRIENDLY_OFFSET;
	if (o->dsp) {
		f1 = ast_dsp_process(c, o->dsp, f);
		if ((f1->frametype == AST_FRAME_DTMF_END) || (f1->frametype == AST_FRAME_DTMF_BEGIN)) {
			if ((f1->subclass == 'm') || (f1->subclass == 'u'))
			    f1->frametype = AST_FRAME_DTMF_BEGIN;
			if (f1->frametype == AST_FRAME_DTMF_END)
			    ast_log(LOG_NOTICE,"Got DTMF char %c\n",f1->subclass);
			return f1;
		}
	}
	return f;
}

static int usbradio_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_usbradio_pvt *o = newchan->tech_pvt;
	ast_log(LOG_WARNING,"usbradio_fixup()\n");
	o->owner = newchan;
	return 0;
}

static int usbradio_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;
	int res = 0;

	switch (cond) {
	case AST_CONTROL_BUSY:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_RINGING:
	case -1:
		res = -1;
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
	case AST_CONTROL_VIDUPDATE:
		break;
	case AST_CONTROL_HOLD:
		ast_verb(0, " << Console Has Been Placed on Hold >> \n");
		ast_moh_start(c, data, o->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_verb(0, " << Console Has Been Retrieved from Hold >> \n");
		ast_moh_stop(c);
		break;
	case AST_CONTROL_RADIO_KEY:
		o->txkeyed = 1;
		if (o->debuglevel)
			ast_verb(0, " << Radio Transmit On. >> \n");
		break;
	case AST_CONTROL_RADIO_UNKEY:
		o->txkeyed = 0;
		if (o->debuglevel)
			ast_verb(0, " << Radio Transmit Off. >> \n");
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, c->name);
		return -1;
	}

	return res;
}

/*
 * allocate a new channel.
 */
static struct ast_channel *usbradio_new(struct chan_usbradio_pvt *o, char *ext, char *ctx, int state)
{
	struct ast_channel *c;
	char device[15] = "dsp";

	if (o->devicenum)
		snprintf(device + 3, sizeof(device) - 3, "%d", o->devicenum);
	c = ast_channel_alloc(1, state, o->cid_num, o->cid_name, "", ext, ctx, 0, "usbRadio/%s", device);
	if (c == NULL)
		return NULL;
	c->tech = &usbradio_tech;
	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	c->fds[0] = o->sounddev;	/* -1 if device closed, override later */
	c->nativeformats = AST_FORMAT_SLINEAR;
	c->readformat = AST_FORMAT_SLINEAR;
	c->writeformat = AST_FORMAT_SLINEAR;
	c->tech_pvt = o;

	if (!ast_strlen_zero(o->language))
		ast_string_field_set(c, language, o->language);
	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
	c->cid.cid_num = ast_strdup(o->cid_num);
	c->cid.cid_ani = ast_strdup(o->cid_num);
	c->cid.cid_name = ast_strdup(o->cid_name);
	if (!ast_strlen_zero(ext))
		c->cid.cid_dnid = ast_strdup(ext);

	o->owner = c;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(c, &global_jbconf);
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", c->name);
			ast_hangup(c);
			o->owner = c = NULL;
			/* XXX what about the channel itself ? */
			/* XXX what about usecnt ? */
		}
	}

	return c;
}

static struct ast_channel *usbradio_request(const char *type, int format, void *data, int *cause)
{
	struct ast_channel *c;
	struct chan_usbradio_pvt *o = find_desc(data);

	ast_debug(4, "usbradio_request ty <%s> data 0x%p <%s>\n", type, data, (char *) data);
	if (o == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", (char *) data);
		/* XXX we could default to 'dsp' perhaps ? */
		return NULL;
	}
	if ((format & AST_FORMAT_SLINEAR) == 0) {
		ast_log(LOG_NOTICE, "Format 0x%x unsupported\n", format);
		return NULL;
	}
	if (o->owner) {
		ast_log(LOG_NOTICE, "Already have a call (chan %p) on the usb channel\n", o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = usbradio_new(o, NULL, NULL, AST_STATE_DOWN);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new usb channel\n");
		return NULL;
	}
	return c;
}

static char *handle_cli_radio_key(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_usbradio_pvt *o = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "radio key";
		e->usage =
			"Usage: radio key\n"
			"       Simulates COR active.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	o = find_desc(usbradio_active);
	o->txtestkey = 1;

	return CLI_SUCCESS;
}

static char *handle_cli_radio_unkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_usbradio_pvt *o = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "radio unkey";
		e->usage =
			"Usage: radio unkey\n"
			"       Simulates COR un-active.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	o = find_desc(usbradio_active);
	o->txtestkey = 0;

	return CLI_SUCCESS;
}

static char *handle_cli_radio_tune(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_usbradio_pvt *o = NULL;
	int i = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "radio tune [rxnoise|rxvoice|rxtone|rxsquelch|rxcap|rxtracecap|"
			"txvoice|txtone|txcap|txtracecap|auxvoice|nocap|dump|save]";
		/* radio tune 6 3000        measured tx value */
		e->usage =
			"Usage: radio tune <function>\n"
			"       rxnoise\n"
			"       rxvoice\n"
			"       rxtone\n"
			"       rxsquelch [newsetting]\n"
			"       rxcap\n"
			"       rxtracecap\n"
			"       txvoice [newsetting]\n"
			"       txtone [newsetting]\n"
			"       txcap\n"
			"       txtracecap\n"
			"       auxvoice [newsetting]\n"
			"       nocap\n"
			"       dump\n"
			"       save (settings to tuning file)\n"
			"\n"
			"       All [newsetting]s are values 0-999\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc < 2) || (a->argc > 4))
		return CLI_SHOWUSAGE; 

	if (a->argc == 2) { /* just show stuff */
		ast_cli(a->fd, "Output A is currently set to %s.\n",
			o->txmixa == TX_OUT_COMPOSITE ? "composite" :
			o->txmixa == TX_OUT_VOICE ? "voice" :
			o->txmixa == TX_OUT_LSD ? "tone" :
			o->txmixa == TX_OUT_AUX ? "auxvoice" :
			"off");

		ast_cli(a->fd, "Output B is currently set to %s.\n",
			o->txmixb == TX_OUT_COMPOSITE ? "composite" :
			o->txmixb == TX_OUT_VOICE ? "voice" :
			o->txmixb == TX_OUT_LSD ? "tone" :
			o->txmixb == TX_OUT_AUX ? "auxvoice" :
			"off");

		ast_cli(a->fd, "Tx Voice Level currently set to %d\n", o->txmixaset);
		ast_cli(a->fd, "Tx Tone Level currently set to %d\n", o->txctcssadj);
		ast_cli(a->fd, "Rx Squelch currently set to %d\n", o->rxsquelchadj);
		return CLI_SHOWUSAGE;
	}

	o = find_desc(usbradio_active);

	if (!strcasecmp(a->argv[2], "rxnoise"))
		tune_rxinput(o);
	else if (!strcasecmp(a->argv[2], "rxvoice"))
		tune_rxvoice(o);
	else if (!strcasecmp(a->argv[2], "rxtone"))
		tune_rxctcss(o);
	else if (!strcasecmp(a->argv[2], "rxsquelch")) {
		if (a->argc == 3) {
		    ast_cli(a->fd, "Current Signal Strength is %d\n", ((32767 - o->pmrChan->rxRssi) * 1000 / 32767));
		    ast_cli(a->fd, "Current Squelch setting is %d\n", o->rxsquelchadj);
#if 0
			ast_cli(a->fd,"Current Raw RSSI        is %d\n",o->pmrChan->rxRssi);
		    ast_cli(a->fd,"Current (real) Squelch setting is %d\n",*(o->pmrChan->prxSquelchAdjust));
#endif
		} else {
			i = atoi(a->argv[3]);
			if ((i < 0) || (i > 999))
				return CLI_SHOWUSAGE;
			ast_cli(a->fd, "Changed Squelch setting to %d\n", i);
			o->rxsquelchadj = i;
			*(o->pmrChan->prxSquelchAdjust) = ((999 - i) * 32767) / 1000;
		}
	} else if (!strcasecmp(a->argv[2], "txvoice")) {
		i = 0;

		if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) &&
			(o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE)) {
			ast_log(LOG_ERROR, "No txvoice output configured.\n");
		} else if (a->argc == 3) {
			if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE))
				ast_cli(a->fd, "Current txvoice setting on Channel A is %d\n", o->txmixaset);
			else
				ast_cli(a->fd, "Current txvoice setting on Channel B is %d\n", o->txmixbset);
		} else {
			i = atoi(a->argv[3]);
			if ((i < 0) || (i > 999))
				return CLI_SHOWUSAGE;

			if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
			 	o->txmixaset = i;
				ast_cli(a->fd, "Changed txvoice setting on Channel A to %d\n", o->txmixaset);
			} else {
			 	o->txmixbset = i;   
				ast_cli(a->fd, "Changed txvoice setting on Channel B to %d\n", o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
			ast_cli(a->fd, "Changed Tx Voice Output setting to %d\n", i);
		}
		tune_txoutput(o,i);
	} else if (!strcasecmp(a->argv[2], "auxvoice")) {
		i = 0;
		if ( (o->txmixa != TX_OUT_AUX) && (o->txmixb != TX_OUT_AUX))
			ast_log(LOG_WARNING, "No auxvoice output configured.\n");
		else if (a->argc == 3) {
			if (o->txmixa == TX_OUT_AUX)
				ast_cli(a->fd, "Current auxvoice setting on Channel A is %d\n", o->txmixaset);
			else
				ast_cli(a->fd, "Current auxvoice setting on Channel B is %d\n", o->txmixbset);
		} else {
			i = atoi(a->argv[3]);
			if ((i < 0) || (i > 999))
				return CLI_SHOWUSAGE;
			if (o->txmixa == TX_OUT_AUX) {
				o->txmixbset = i;
				ast_cli(a->fd, "Changed auxvoice setting on Channel A to %d\n", o->txmixaset);
			} else {
				o->txmixbset = i;
				ast_cli(a->fd, "Changed auxvoice setting on Channel B to %d\n", o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
		}
		/* tune_auxoutput(o,i); */
	} else if (!strcasecmp(a->argv[2], "txtone")) {
		if (a->argc == 3)
			ast_cli(a->fd, "Current Tx CTCSS modulation setting = %d\n", o->txctcssadj);
		else {
			i = atoi(a->argv[3]);
			if ((i < 0) || (i > 999))
				return CLI_SHOWUSAGE;
			o->txctcssadj = i;
			set_txctcss_level(o);
			ast_cli(a->fd, "Changed Tx CTCSS modulation setting to %i\n", i);
		}
		o->txtestkey = 1;
		usleep(5000000);
		o->txtestkey = 0;
	} else if (!strcasecmp(a->argv[2],"dump"))
		pmrdump(o);
	else if (!strcasecmp(a->argv[2],"nocap")) {
		ast_cli(a->fd, "File capture (trace) was rx=%d tx=%d and now off.\n", o->b.rxcap2, o->b.txcap2);
		ast_cli(a->fd, "File capture (raw)   was rx=%d tx=%d and now off.\n", o->b.rxcapraw, o->b.txcapraw);
		o->b.rxcapraw = o->b.txcapraw = o->b.rxcap2 = o->b.txcap2 = o->pmrChan->b.rxCapture = o->pmrChan->b.txCapture = 0;
		if (frxcapraw) {
			fclose(frxcapraw);
			frxcapraw = NULL;
		}
		if (frxcaptrace) {
			fclose(frxcaptrace);
			frxcaptrace = NULL;
		}
		if (frxoutraw) {
			fclose(frxoutraw);
			frxoutraw = NULL;
		}
		if (ftxcapraw) {
			fclose(ftxcapraw);
			ftxcapraw = NULL;
		}
		if (ftxcaptrace) {
			fclose(ftxcaptrace);
			ftxcaptrace = NULL;
		}
		if (ftxoutraw) {
			fclose(ftxoutraw);
			ftxoutraw = NULL;
		}
	} else if (!strcasecmp(a->argv[2], "rxtracecap")) {
		if (!frxcaptrace)
			frxcaptrace = fopen(RX_CAP_TRACE_FILE, "w");
		ast_cli(a->fd, "Trace rx on.\n");
		o->b.rxcap2 = o->pmrChan->b.rxCapture = 1;
	} else if (!strcasecmp(a->argv[2], "txtracecap")) {
		if (!ftxcaptrace)
			ftxcaptrace = fopen(TX_CAP_TRACE_FILE, "w");
		ast_cli(a->fd, "Trace tx on.\n");
		o->b.txcap2 = o->pmrChan->b.txCapture = 1;
	} else if (!strcasecmp(a->argv[2], "rxcap")) {
		if (!frxcapraw)
			frxcapraw = fopen(RX_CAP_RAW_FILE, "w");
		ast_cli(a->fd, "cap rx raw on.\n");
		o->b.rxcapraw = 1;
	} else if (!strcasecmp(a->argv[2], "txcap")) {
		if (!ftxcapraw)
			ftxcapraw = fopen(TX_CAP_RAW_FILE, "w");
		ast_cli(a->fd, "cap tx raw on.\n");
		o->b.txcapraw = 1;
	} else if (!strcasecmp(a->argv[2], "save")) {
		tune_write(o);
		ast_cli(a->fd, "Saved radio tuning settings to usbradio_tune.conf\n");
	} else
		return CLI_SHOWUSAGE;
	return CLI_SUCCESS;
}

/*
	set transmit ctcss modulation level
	adjust mixer output or internal gain depending on output type
	setting range is 0.0 to 0.9
*/
static int set_txctcss_level(struct chan_usbradio_pvt *o)
{							  
	if (o->txmixa == TX_OUT_LSD) {
		o->txmixaset = (151 * o->txctcssadj) / 1000;
		mixer_write(o);
		mult_set(o);
	} else if (o->txmixb == TX_OUT_LSD) {
		o->txmixbset = (151 * o->txctcssadj) / 1000;
		mixer_write(o);
		mult_set(o);
	} else {
		*o->pmrChan->ptxCtcssAdjust = (o->txctcssadj * M_Q8) / 1000;
	}
	return 0;
}
/*
	CLI debugging on and off
*/
static char *handle_cli_radio_set_debug_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_usbradio_pvt *o = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "radio set debug [off]";
		e->usage =
			"Usage: radio set debug [off]\n"
			"       Enable/Disable radio debugging.\n";
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 3 || a->argc > 4)
		return CLI_SHOWUSAGE;
	if (a->argc == 4 && strncasecmp(a->argv[3], "off", 3))
		return CLI_SHOWUSAGE;

	o = find_desc(usbradio_active);

	if (a->argc == 3)
		o->debuglevel = 1;
	else
		o->debuglevel = 0;

	ast_cli(a->fd, "USB Radio debugging %s.\n", o->debuglevel ? "enabled" : "disabled");

	return CLI_SUCCESS;
}

static char *handle_cli_radio_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_usbradio_pvt *o = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "radio set debug {on|off}";
		e->usage =
			"Usage: radio set debug {on|off}\n"
			"       Enable/Disable radio debugging.\n";
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	o = find_desc(usbradio_active);

	if (!strncasecmp(a->argv[e->args - 1], "on", 2))
		o->debuglevel = 1;
	else if (!strncasecmp(a->argv[e->args - 1], "off", 3))
		o->debuglevel = 0;
	else
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "USB Radio debugging %s.\n", o->debuglevel ? "enabled" : "disabled");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_radio_set_debug_deprecated = AST_CLI_DEFINE(handle_cli_radio_set_debug_deprecated, "Enable/Disable Radio Debugging");
static struct ast_cli_entry cli_usbradio[] = {
	AST_CLI_DEFINE(handle_cli_radio_key,       "Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_cli_radio_unkey,     "Simulate Rx Signal Lusb"),
	AST_CLI_DEFINE(handle_cli_radio_tune,      "Radio Tune"),
	AST_CLI_DEFINE(handle_cli_radio_set_debug, "Enable/Disable Radio Debugging", .deprecate_cmd = &cli_radio_set_debug_deprecated),
};

/*
 * store the callerid components
 */
#if 0
static void store_callerid(struct chan_usbradio_pvt *o, const char *s)
{
	ast_callerid_split(s, o->cid_name, sizeof(o->cid_name), o->cid_num, sizeof(o->cid_num));
}
#endif

static void store_rxdemod(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no")) {
		o->rxdemod = RX_AUDIO_NONE;
	} else if (!strcasecmp(s, "speaker")) {
		o->rxdemod = RX_AUDIO_SPEAKER;
	} else if (!strcasecmp(s, "flat")) {
			o->rxdemod = RX_AUDIO_FLAT;
	} else {
		ast_log(LOG_WARNING, "Unrecognized rxdemod parameter: %s\n", s);
	}

	ast_debug(4, "set rxdemod = %s\n", s);
}

					   
static void store_txmixa(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no"))
		o->txmixa = TX_OUT_OFF;

	else if (!strcasecmp(s, "voice"))
		o->txmixa = TX_OUT_VOICE;
	else if (!strcasecmp(s, "tone"))
			o->txmixa = TX_OUT_LSD;
	else if (!strcasecmp(s, "composite"))
		o->txmixa = TX_OUT_COMPOSITE;
	else if (!strcasecmp(s, "auxvoice"))
		o->txmixb = TX_OUT_AUX;
	else
		ast_log(LOG_WARNING, "Unrecognized txmixa parameter: %s\n", s);

	ast_debug(4, "set txmixa = %s\n", s);
}

static void store_txmixb(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no"))
		o->txmixb = TX_OUT_OFF;
	else if (!strcasecmp(s, "voice"))
		o->txmixb = TX_OUT_VOICE;
	else if (!strcasecmp(s, "tone"))
			o->txmixb = TX_OUT_LSD;
	else if (!strcasecmp(s, "composite"))
		o->txmixb = TX_OUT_COMPOSITE;
	else if (!strcasecmp(s, "auxvoice"))
		o->txmixb = TX_OUT_AUX;
	else
		ast_log(LOG_WARNING, "Unrecognized txmixb parameter: %s\n", s);

	ast_debug(4, "set txmixb = %s\n", s);
}

static void store_rxcdtype(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no"))
		o->rxcdtype = CD_IGNORE;
	else if (!strcasecmp(s, "usb"))
		o->rxcdtype = CD_HID;
	else if (!strcasecmp(s, "dsp"))
		o->rxcdtype = CD_XPMR_NOISE;
	else if (!strcasecmp(s, "vox"))
		o->rxcdtype = CD_XPMR_VOX;
	else if (!strcasecmp(s, "usbinvert"))
		o->rxcdtype = CD_HID_INVERT;
	else
		ast_log(LOG_WARNING, "Unrecognized rxcdtype parameter: %s\n", s);

	ast_debug(4, "set rxcdtype = %s\n", s);
}

static void store_rxsdtype(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no") || !strcasecmp(s, "SD_IGNORE"))
		o->rxsdtype = SD_IGNORE;
	else if (!strcasecmp(s, "usb") || !strcasecmp(s, "SD_HID"))
		o->rxsdtype = SD_HID;
	else if (!strcasecmp(s, "usbinvert") || !strcasecmp(s, "SD_HID_INVERT"))
		o->rxsdtype = SD_HID_INVERT;
	else if (!strcasecmp(s, "software") || !strcasecmp(s, "SD_XPMR"))
		o->rxsdtype = SD_XPMR;
	else
		ast_log(LOG_WARNING, "Unrecognized rxsdtype parameter: %s\n", s);

	ast_debug(4, "set rxsdtype = %s\n", s);
}

static void store_rxgain(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	if (sscanf(s, "%f", &f) == 1)
		o->rxgain = f;
	ast_debug(4, "set rxgain = %f\n", f);
}

static void store_rxvoiceadj(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	if (sscanf(s, "%f", &f) == 1)
		o->rxvoiceadj = f;
	ast_debug(4, "set rxvoiceadj = %f\n", f);
}

static void store_rxctcssadj(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	if (sscanf(s, "%f", &f) == 1)
		o->rxctcssadj = f;
	ast_debug(4, "set rxctcssadj = %f\n", f);
}

static void store_txtoctype(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no") || !strcasecmp(s, "TOC_NONE"))
		o->txtoctype = TOC_NONE;
	else if (!strcasecmp(s, "phase") || !strcasecmp(s, "TOC_PHASE"))
		o->txtoctype = TOC_PHASE;
	else if (!strcasecmp(s, "notone") || !strcasecmp(s, "TOC_NOTONE"))
		o->txtoctype = TOC_NOTONE;
	else
		ast_log(LOG_WARNING, "Unrecognized txtoctype parameter: %s\n", s);

	ast_debug(4, "set txtoctype = %s\n", s);
}

static void store_rxctcssfreq(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	if (sscanf(s, "%f", &f) == 1)
		o->rxctcssfreq = f;
	ast_debug(4, "set rxctcss = %f\n", f);
}

static void store_txctcssfreq(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	if (sscanf(s, "%f", &f) == 1)
		o->txctcssfreq = f;
	ast_debug(4, "set txctcss = %f\n", f);
}

static void tune_txoutput(struct chan_usbradio_pvt *o, int value)
{
	o->txtestkey = 1;
	o->pmrChan->txPttIn = 1;

#if 0
	/* generate 1KHz tone at 7200 peak */
	o->pmrChan->spsSigGen1->freq = 10000;
	o->pmrChan->spsSigGen1->outputGain = (float)(0.22 * M_Q8);
	o->pmrChan->b.startSpecialTone = 1;
#endif

	TxTestTone(o->pmrChan, 1);

	usleep(5000000);
	/* o->pmrChan->b.stopSpecialTone = 1; */
	usleep(100000);

	TxTestTone(o->pmrChan, 0);

	o->pmrChan->txPttIn = 0;
	o->txtestkey = 0;
}

static void tune_rxinput(struct chan_usbradio_pvt *o)
{
	const int target = 23000;
	const int tolerance = 2000;
	const int settingmin = 1;
	const int settingstart = 2;
	const int maxtries = 12;

	float settingmax;
	
	int setting = 0, tries = 0, tmpdiscfactor, meas;
	int tunetype = 0;

	settingmax = o->micmax;

	if (o->pmrChan->rxDemod)
		tunetype = 1;

	setting = settingstart;

	while (tries < maxtries) {
		setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL, setting, 0);
		setamixer(o->devicenum, MIXER_PARAM_MIC_BOOST, o->rxboostset, 0);
		usleep(100000);
		if (o->rxcdtype == CD_XPMR_VOX || o->rxdemod == RX_AUDIO_SPEAKER) {
			ast_debug(4, "Measure Direct Input\n");
			o->pmrChan->spsMeasure->source = o->pmrChan->spsRx->source;
			o->pmrChan->spsMeasure->discfactor = 1000;
			o->pmrChan->spsMeasure->enabled = 1;
			o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
			usleep(400000);	
			meas = o->pmrChan->spsMeasure->apeak;
			o->pmrChan->spsMeasure->enabled = 0;	
		} else {
			ast_debug(4, "Measure HF Noise\n");
			tmpdiscfactor = o->pmrChan->spsRx->discfactor;
			o->pmrChan->spsRx->discfactor = (i16)1000;
			o->pmrChan->spsRx->discounteru = o->pmrChan->spsRx->discounterl = 0;
			o->pmrChan->spsRx->amax = o->pmrChan->spsRx->amin = 0;
			usleep(200000);
			meas = o->pmrChan->rxRssi;
			o->pmrChan->spsRx->discfactor = tmpdiscfactor;
			o->pmrChan->spsRx->discounteru = o->pmrChan->spsRx->discounterl = 0;
			o->pmrChan->spsRx->amax = o->pmrChan->spsRx->amin = 0;
		}
        if (!meas)
			meas++;
		ast_log(LOG_NOTICE, "tries=%d, setting=%d, meas=%i\n", tries, setting, meas);

		if ( meas < (target - tolerance) || meas > (target + tolerance) || tries < 3)
			setting = setting * target / meas;
		else if (tries > 4 && meas > (target - tolerance) && meas < (target + tolerance) )
			break;

		if (setting < settingmin)
			setting = settingmin;
		else if (setting > settingmax)
			setting = settingmax;

		tries++;
	}
	ast_log(LOG_NOTICE, "DONE tries=%d, setting=%d, meas=%i\n", tries,
		(setting * 1000) / o->micmax, meas);
	if (meas < (target - tolerance) || meas > (target + tolerance))
		ast_log(LOG_NOTICE, "ERROR: RX INPUT ADJUST FAILED.\n");
	else {
		ast_log(LOG_NOTICE, "INFO: RX INPUT ADJUST SUCCESS.\n");	
		o->rxmixerset = (setting * 1000) / o->micmax;
	}
}
/*
*/
static void tune_rxvoice(struct chan_usbradio_pvt *o)
{
	const int target = 7200;        /* peak */
	const int tolerance = 360;      /* peak to peak */
	const float settingmin = 0.1;
	const float settingmax = 4;
	const float settingstart = 1;
	const int maxtries = 12;

	float setting;

	int tries = 0, meas;

	ast_log(LOG_NOTICE, "INFO: RX VOICE ADJUST START.\n");	
	ast_log(LOG_NOTICE, "target=%d tolerance=%d\n", target, tolerance);

	if (!o->pmrChan->spsMeasure)
		ast_log(LOG_ERROR, "NO MEASURE BLOCK.\n");

	if (!o->pmrChan->spsMeasure->source || !o->pmrChan->prxVoiceAdjust )
		ast_log(LOG_ERROR, "NO SOURCE OR MEASURE SETTING.\n");

	o->pmrChan->spsMeasure->source = o->pmrChan->spsRxOut->sink;
	o->pmrChan->spsMeasure->enabled = 1;
	o->pmrChan->spsMeasure->discfactor = 1000;

	setting=settingstart;

	ast_debug(4, "ERROR: NO MEASURE BLOCK.\n");

	while (tries < maxtries) {
		*(o->pmrChan->prxVoiceAdjust) = setting * M_Q8;
		usleep(10000);
    	o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		usleep(1000000);
		meas = o->pmrChan->spsMeasure->apeak;
		ast_log(LOG_NOTICE, "tries=%d, setting=%f, meas=%i\n", tries, setting, meas);

		if (meas < (target - tolerance) || meas > (target + tolerance) || tries < 3)
			setting = setting * target / meas;
		else if (tries > 4 && meas > (target - tolerance) && meas < (target + tolerance))
			break;
		if (setting < settingmin)
			setting = settingmin;
		else if (setting > settingmax)
			setting = settingmax;

		tries++;
	}

	o->pmrChan->spsMeasure->enabled = 0;

	ast_log(LOG_NOTICE, "DONE tries=%d, setting=%f, meas=%f\n", tries, setting, (float)meas);
	if (meas < (target - tolerance) || meas > (target + tolerance))
		ast_log(LOG_ERROR, "RX VOICE GAIN ADJUST FAILED.\n");
	else {
		ast_log(LOG_NOTICE, "RX VOICE GAIN ADJUST SUCCESS.\n");
		o->rxvoiceadj = setting;
	}
}

static void tune_rxctcss(struct chan_usbradio_pvt *o)
{
	const int target = 4096;
	const int tolerance = 100;
	const float settingmin = 0.1;
	const float settingmax = 4;
	const float settingstart = 1;
	const int maxtries = 12;

	float setting;
	int tries = 0, meas;

	ast_log(LOG_NOTICE, "RX CTCSS ADJUST START.\n");	
	ast_log(LOG_NOTICE, "target=%d tolerance=%d \n", target, tolerance);

	o->pmrChan->spsMeasure->source = o->pmrChan->prxCtcssMeasure;
	o->pmrChan->spsMeasure->discfactor = 400;
	o->pmrChan->spsMeasure->enabled = 1;

	setting = settingstart;

	while (tries < maxtries) {
		*(o->pmrChan->prxCtcssAdjust) = setting * M_Q8;
		usleep(10000);
    	o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		usleep(500000);
		meas = o->pmrChan->spsMeasure->apeak;
		ast_debug(4, "tries=%d, setting=%f, meas=%i\n", tries, setting, meas);

		if (meas < (target - tolerance) || meas > (target + tolerance) || tries < 3)
			setting = setting * target / meas;
		else if (tries > 4 && meas > (target - tolerance) && meas < (target + tolerance))
			break;
		if (setting < settingmin)
			setting = settingmin;
		else if (setting > settingmax)
			setting = settingmax;

		tries++;
	}
	o->pmrChan->spsMeasure->enabled = 0;
	ast_debug(4, "DONE tries=%d, setting=%f, meas=%f\n", tries, setting, (float)meas);
	if (meas < (target - tolerance) || meas > (target + tolerance))
		ast_log(LOG_ERROR, "RX CTCSS GAIN ADJUST FAILED.\n");
	else {
		ast_log(LOG_NOTICE, "RX CTCSS GAIN ADJUST SUCCESS.\n");
		o->rxctcssadj = setting;
	}
}
/*
	this file then is included in chan_usbradio.conf
	#include /etc/asterisk/usbradio_tune.conf 
*/
static void tune_write(struct chan_usbradio_pvt *o)
{
	FILE *fp;
	
	fp = fopen("/etc/asterisk/usbradio_tune.conf", "w");
 
	if (!strcmp(o->name, "dsp"))
		fprintf(fp, "[general]\n");
	else
		fprintf(fp, "[%s]\n", o->name);

	fprintf(fp, "; name=%s\n", o->name);
	fprintf(fp, "; devicenum=%d\n", o->devicenum);

	fprintf(fp, "rxmixerset=%d\n", o->rxmixerset);
	fprintf(fp, "rxboostset=%d\n", o->rxboostset);
	fprintf(fp, "txmixaset=%d\n", o->txmixaset);
	fprintf(fp, "txmixbset=%d\n", o->txmixbset);

	fprintf(fp, "rxvoiceadj=%f\n", o->rxvoiceadj);
	fprintf(fp, "rxctcssadj=%f\n", o->rxctcssadj);
	fprintf(fp, "txctcssadj=%d\n", o->txctcssadj);

	fprintf(fp, "rxsquelchadj=%d\n", o->rxsquelchadj);
	fclose(fp);
}

static void mixer_write(struct chan_usbradio_pvt *o)
{
	setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_SW, 0, 0);
	setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_VOL, 0, 0);
	setamixer(o->devicenum, MIXER_PARAM_SPKR_PLAYBACK_SW, 1, 0);
	setamixer(o->devicenum, MIXER_PARAM_SPKR_PLAYBACK_VOL,
		o->txmixaset * o->spkrmax / 1000,
		o->txmixbset * o->spkrmax / 1000);
	setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL,
		o->rxmixerset * o->micmax / 1000, 0);
	setamixer(o->devicenum, MIXER_PARAM_MIC_BOOST, o->rxboostset, 0);
	setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_SW, 1, 0);
}
/*
	adjust dsp multiplier to add resolution to tx level adjustment
*/
static void mult_set(struct chan_usbradio_pvt *o)
{

	if (o->pmrChan->spsTxOutA) {
		o->pmrChan->spsTxOutA->outputGain = 
			mult_calc((o->txmixaset * 152) / 1000);
	}
	if (o->pmrChan->spsTxOutB) {
		o->pmrChan->spsTxOutB->outputGain = 
			mult_calc((o->txmixbset * 152) / 1000);
	}
}
/*
 * input 0 - 151 outputs are pot and multiplier
 */
static int mult_calc(int value)
{
	const int multx = M_Q8;
	int pot, mult;

	pot= ((int)(value / 4) * 4) + 2;
	mult = multx - ((multx * (3 - (value % 4))) / (pot + 2));
	return mult;
}

#define pd(x) ast_debug(4, #x" = %d\n", x)
#define pp(x) ast_debug(4, #x" = %p\n", x)
#define ps(x) ast_debug(4, #x" = %s\n", x)
#define pf(x) ast_debug(4, #x" = %f\n", x)
/*
*/
static void pmrdump(struct chan_usbradio_pvt *o)
{
	t_pmr_chan *p;

	p = o->pmrChan;

	ast_debug(4, "odump()\n");

	pd(o->devicenum);

	pd(o->rxdemod);
	pd(o->rxcdtype);
	pd(o->rxsdtype);
	pd(o->txtoctype);

	pd(o->rxmixerset);
	pf(o->rxvoiceadj);
	pf(o->rxctcssadj);
	pd(o->rxsquelchadj);
	 
	pd(o->txprelim);
	pd(o->txmixa);
	pd(o->txmixb);
	
	pd(o->txmixaset);
	pd(o->txmixbset);
	
	ast_debug(4, "pmrdump()\n");
 
	ast_debug(4, "prxSquelchAdjust=%d\n", *(o->pmrChan->prxSquelchAdjust));

	pd(p->rxCarrierPoint);
	pd(p->rxCarrierHyst);

	pd(p->rxCtcss->relax);
	pf(p->rxCtcssFreq);	
	pd(p->rxCtcssIndex);
	pf(p->txCtcssFreq);

	pd(p->txMixA);
	pd(p->txMixB);
    
	pd(p->rxDeEmpEnable);
	pd(p->rxCenterSlicerEnable);
	pd(p->rxCtcssDecodeEnable);
	pd(p->rxDcsDecodeEnable);

	pd(p->txHpfEnable);
	pd(p->txLimiterEnable);
	pd(p->txPreEmpEnable);
	pd(p->txLpfEnable);

	if (p->spsTxOutA)
		pd(p->spsTxOutA->outputGain);
	if (p->spsTxOutB)
		pd(p->spsTxOutB->outputGain);

	return;
}


/*
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_usbradio_pvt *store_config(struct ast_config *cfg, char *ctg)
{
	struct ast_variable *v;
	struct chan_usbradio_pvt *o;
	struct ast_config *cfg1;
	struct ast_flags config_flags = { 0 };

	if (ctg == NULL) {
		traceusb1(" store_config() ctg == NULL\n");
		o = &usbradio_default;
		ctg = "general";
	} else {
		if (!(o = ast_calloc(1, sizeof(*o)))){
			return NULL;
		}
		*o = usbradio_default;
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o->name = ast_strdup("dsp");
			usbradio_active = o->name;
		} else
			o->name = ast_strdup(ctg);
	}

	strcpy(o->mohinterpret, "default");
	o->micmax = amixer_max(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL);
	o->spkrmax = amixer_max(o->devicenum, MIXER_PARAM_SPKR_PLAYBACK_VOL);
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {

		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;
		CV_START(v->name, v->value);

		CV_UINT("frags", o->frags);
		CV_UINT("queuesize", o->queuesize);
		CV_UINT("devicenum", o->devicenum);
		CV_UINT("debug", usbradio_debug);
		CV_BOOL("rxcpusaver", o->rxcpusaver);
		CV_BOOL("txcpusaver", o->txcpusaver);
		CV_BOOL("invertptt", o->invertptt);
		CV_F("rxdemod", store_rxdemod(o, v->value));
		CV_BOOL("txprelim", o->txprelim);;
		CV_F("txmixa", store_txmixa(o, v->value));
		CV_F("txmixb", store_txmixb(o, v->value));
		CV_F("carrierfrom", store_rxcdtype(o, v->value));
		CV_F("rxsdtype", store_rxsdtype(o, v->value));
		CV_F("rxctcssfreq", store_rxctcssfreq(o, v->value));
		CV_F("txctcssfreq", store_txctcssfreq(o, v->value));
		CV_F("rxgain", store_rxgain(o, v->value));
		CV_BOOL("rxboostset", o->rxboostset);
		CV_UINT("rxctcssrelax", o->rxctcssrelax);
		CV_F("txtoctype", store_txtoctype(o, v->value));
		CV_UINT("hdwtype", o->hdwtype);
		CV_UINT("duplex", o->radioduplex);

		CV_END;
	}
	
	cfg1 = ast_config_load(config1, config_flags);
	if (!cfg1) {
		o->rxmixerset = 500;
		o->txmixaset = 500;
		o->txmixbset = 500;
		o->rxvoiceadj = 0.5;
		o->rxctcssadj = 0.5;
		o->txctcssadj = 200;
		o->rxsquelchadj = 500;
		ast_log(LOG_WARNING, "File %s not found, using default parameters.\n", config1);
	} else  {
		for (v = ast_variable_browse(cfg1, ctg); v; v = v->next) {
	
			CV_START(v->name, v->value);
			CV_UINT("rxmixerset", o->rxmixerset);
			CV_UINT("txmixaset", o->txmixaset);
			CV_UINT("txmixbset", o->txmixbset);
			CV_F("rxvoiceadj", store_rxvoiceadj(o, v->value));
			CV_F("rxctcssadj", store_rxctcssadj(o, v->value));
			CV_UINT("txctcssadj", o->txctcssadj);
			CV_UINT("rxsquelchadj", o->rxsquelchadj);
			CV_END;
		}
		ast_config_destroy(cfg1);
	}

	o->debuglevel = 0;

	if (o == &usbradio_default)		/* we are done with the default */
		return NULL;

	o->lastopen = ast_tvnow();	/* don't leave it 0 or tvdiff may wrap */
	o->dsp = ast_dsp_new();
	if (o->dsp) {
		ast_dsp_set_features(o->dsp, DSP_FEATURE_DTMF_DETECT);
		ast_dsp_set_digitmode(o->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
	}

	if (o->rxctcssfreq != 0 && o->rxdemod == RX_AUDIO_SPEAKER)
		ast_log(LOG_ERROR, "Incompatable Options  o->rxctcssfreq=%f and o->rxdemod=speaker\n", o->rxctcssfreq);	

	if (o->pmrChan == NULL) {
		t_pmr_chan tChan;

		memset(&tChan, 0, sizeof(tChan));

		tChan.rxDemod = o->rxdemod;
		tChan.rxCdType = o->rxcdtype;

		tChan.txMod = o->txprelim;

		tChan.txMixA = o->txmixa;
		tChan.txMixB = o->txmixb;

		tChan.rxCpuSaver = o->rxcpusaver;
		tChan.txCpuSaver = o->txcpusaver;

		tChan.rxCtcssFreq = o->rxctcssfreq;
		tChan.txCtcssFreq = o->txctcssfreq;

		o->pmrChan = createPmrChannel(&tChan, FRAME_SIZE);

		o->pmrChan->radioDuplex = o->radioduplex;

		o->pmrChan->rxCpuSaver = o->rxcpusaver;
		o->pmrChan->txCpuSaver = o->txcpusaver;

		*(o->pmrChan->prxSquelchAdjust) = 
			((999 - o->rxsquelchadj) * 32767) / 1000;

		o->pmrChan->spsRx->outputGain = o->rxvoiceadj*M_Q8;

		o->pmrChan->txTocType = o->txtoctype;

		if ((o->txmixa == TX_OUT_LSD) ||
			(o->txmixa == TX_OUT_COMPOSITE) ||
			(o->txmixb == TX_OUT_LSD) ||
			(o->txmixb == TX_OUT_COMPOSITE)) {
			*(o->pmrChan->prxCtcssAdjust) = o->rxctcssadj * M_Q8;
			set_txctcss_level(o);
		}

		o->pmrChan->rxCtcss->relax = o->rxctcssrelax;

	}

	if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) &&
		(o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE))
		ast_log(LOG_ERROR, "No txvoice output configured.\n");

	if (o->txctcssfreq && 
	    o->txmixa != TX_OUT_LSD && o->txmixa != TX_OUT_COMPOSITE  &&
		o->txmixb != TX_OUT_LSD && o->txmixb != TX_OUT_COMPOSITE)
		ast_log(LOG_ERROR, "No txtone output configured.\n");

	if (o->rxctcssfreq && o->pmrChan->rxCtcssIndex < 0)
		ast_log(LOG_ERROR, "Invalid CTCSS Frequency.\n");

	/* RxTestIt(o); */

	mixer_write(o);
	mult_set(o);    
	hidhdwconfig(o);

	/* pmrdump(o); */

	/* link into list of devices */
	if (o != &usbradio_default) {
		o->next = usbradio_default.next;
		usbradio_default.next = o;
	}
	return o;
}

#if	DEBUG_FILETEST == 1
/*
	Test It on a File
*/
int RxTestIt(struct chan_usbradio_pvt *o)
{
	const int numSamples = SAMPLES_PER_BLOCK;
	const int numChannels = 16;

	i16 sample, i, ii;
	
	i32 txHangTime;

	i16 txEnable;

	t_pmr_chan tChan;
	t_pmr_chan *pChan;

	FILE *hInput = NULL, *hOutput = NULL, *hOutputTx = NULL;
 
	i16 iBuff[numSamples * 2 * 6], oBuff[numSamples];

	ast_debug(4, "RxTestIt()\n");

	pChan = o->pmrChan;
	pChan->b.txCapture = 1;
	pChan->b.rxCapture = 1;

	txEnable = 0;

	hInput = fopen("/usr/src/xpmr/testdata/rx_in.pcm", "r");
	if (!hInput){
		ast_debug(4, " RxTestIt() File Not Found.\n");
		return 0;
	}
	hOutput = fopen("/usr/src/xpmr/testdata/rx_debug.pcm", "w");

	ast_debug(4, " RxTestIt() Working...\n");
			 	
	while (!feof(hInput)) {
		fread((void *)iBuff, 2, numSamples * 2 * 6, hInput);
		 
		if (txHangTime)
			txHangTime -= numSamples;
		if (txHangTime < 0)
			txHangTime = 0;
		
		if (pChan->rxCtcss->decode)
			txHangTime = (8000 / 1000 * 2000);

		if (pChan->rxCtcss->decode && !txEnable) {
			txEnable = 1;
			/* pChan->inputBlanking = (8000 / 1000 * 200); */
		} else if (!pChan->rxCtcss->decode && txEnable) {
			txEnable = 0;
		}

		PmrRx(pChan, iBuff, oBuff);

		fwrite((void *)pChan->prxDebug, 2, numSamples * numChannels, hOutput);
	}
	pChan->b.txCapture = 0;
	pChan->b.rxCapture = 0;

	if (hInput)
		fclose(hInput);
	if (hOutput)
		fclose(hOutput);

	ast_debug(4, " RxTestIt() Complete.\n");

	return 0;
}
#endif

#include "./xpmr/xpmr.c"
/*
*/
static int load_module(void)
{
	struct ast_config *cfg = NULL;
	char *ctg = NULL;
	struct ast_flags config_flags = { 0 };

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* load config file */
	if (!(cfg = ast_config_load(config, config_flags))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	do {
		store_config(cfg, ctg);
	} while ( (ctg = ast_category_browse(cfg, ctg)) != NULL);

	ast_config_destroy(cfg);

	if (find_desc(usbradio_active) == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", usbradio_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_channel_register(&usbradio_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'usb'\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_cli_register_multiple(cli_usbradio, sizeof(cli_usbradio) / sizeof(struct ast_cli_entry));

	return AST_MODULE_LOAD_SUCCESS;
}
/*
*/
static int unload_module(void)
{
	struct chan_usbradio_pvt *o;

	ast_log(LOG_WARNING, "unload_module() called\n");

	ast_channel_unregister(&usbradio_tech);
	ast_cli_unregister_multiple(cli_usbradio, sizeof(cli_usbradio) / sizeof(struct ast_cli_entry));

	for (o = usbradio_default.next; o; o = o->next) {

		ast_log(LOG_WARNING, "destroyPmrChannel() called\n");
		if (o->pmrChan)
			destroyPmrChannel(o->pmrChan);
		
		#if DEBUG_CAPTURES == 1
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (frxcaptrace) { fclose(frxcaptrace); frxcaptrace = NULL; }
		if (frxoutraw) { fclose(frxoutraw); frxoutraw = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
		if (ftxcaptrace) { fclose(ftxcaptrace); ftxcaptrace = NULL; }
		if (ftxoutraw) { fclose(ftxoutraw); ftxoutraw = NULL; }
		#endif

		close(o->sounddev);
		if (o->dsp)
			ast_dsp_free(o->dsp);
		if (o->owner)
			ast_softhangup(o->owner, AST_SOFTHANGUP_APPUNLOAD);
		if (o->owner)			/* XXX how ??? */
			return -1;
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "usb Console Channel Driver");

/*	end of file */


