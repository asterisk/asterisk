/*
 * Chan_Misdn -- Channel Driver for Asterisk
 *
 * Interface to mISDN
 *
 * Copyright (C) 2004, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/*! \file
 * \brief Interface to mISDN - Config
 * \author Christian Richter <crich@beronet.com>
 */




#ifndef CHAN_MISDN_CONFIG_H
#define CHAN_MISDN_CONFIG_H

#define BUFFERSIZE 512

enum misdn_cfg_elements {

	/* port config items */
	MISDN_CFG_FIRST = 0,
	MISDN_CFG_GROUPNAME,           /* char[] */
	MISDN_CFG_ALLOWED_BEARERS,           /* char[] */
	MISDN_CFG_FAR_ALERTING,        /* int (bool) */
	MISDN_CFG_RXGAIN,              /* int */
	MISDN_CFG_TXGAIN,              /* int */
	MISDN_CFG_TE_CHOOSE_CHANNEL,   /* int (bool) */
	MISDN_CFG_PMP_L1_CHECK,        /* int (bool) */
	MISDN_CFG_REJECT_CAUSE,		/* int */
	MISDN_CFG_ALARM_BLOCK,        /* int (bool) */
	MISDN_CFG_HDLC,                /* int (bool) */
	MISDN_CFG_CONTEXT,             /* char[] */
	MISDN_CFG_LANGUAGE,            /* char[] */
	MISDN_CFG_MUSICCLASS,            /* char[] */
	MISDN_CFG_CALLERID,            /* char[] */
	MISDN_CFG_METHOD,              /* char[] */
	MISDN_CFG_DIALPLAN,            /* int */
	MISDN_CFG_LOCALDIALPLAN,       /* int */
	MISDN_CFG_CPNDIALPLAN,       /* int */
	MISDN_CFG_TON_PREFIX_UNKNOWN,         /* char[] */
	MISDN_CFG_TON_PREFIX_INTERNATIONAL,   /* char[] */
	MISDN_CFG_TON_PREFIX_NATIONAL,        /* char[] */
	MISDN_CFG_TON_PREFIX_NETWORK_SPECIFIC,/* char[] */
	MISDN_CFG_TON_PREFIX_SUBSCRIBER,      /* char[] */
	MISDN_CFG_TON_PREFIX_ABBREVIATED,     /* char[] */
	MISDN_CFG_PRES,                /* int */
	MISDN_CFG_SCREEN,              /* int */
	MISDN_CFG_DISPLAY_CONNECTED,   /* int */
	MISDN_CFG_DISPLAY_SETUP,       /* int */
	MISDN_CFG_ALWAYS_IMMEDIATE,    /* int (bool) */
	MISDN_CFG_NODIALTONE,    /* int (bool) */
	MISDN_CFG_IMMEDIATE,           /* int (bool) */
	MISDN_CFG_SENDDTMF,           /* int (bool) */
	MISDN_CFG_ASTDTMF,            /* int (bool) */
	MISDN_CFG_HOLD_ALLOWED,        /* int (bool) */
	MISDN_CFG_EARLY_BCONNECT,      /* int (bool) */
	MISDN_CFG_INCOMING_EARLY_AUDIO,      /* int (bool) */
	MISDN_CFG_ECHOCANCEL,          /* int */
	MISDN_CFG_CC_REQUEST_RETENTION,/* bool */
	MISDN_CFG_OUTGOING_COLP,       /* int */
#ifdef MISDN_1_2
	MISDN_CFG_PIPELINE,      /* char[] */
#endif

#ifdef WITH_BEROEC
	MISDN_CFG_BNECHOCANCEL,
	MISDN_CFG_BNEC_ANTIHOWL,
	MISDN_CFG_BNEC_NLP,
	MISDN_CFG_BNEC_ZEROCOEFF,
	MISDN_CFG_BNEC_TD,
	MISDN_CFG_BNEC_ADAPT,
#endif
	MISDN_CFG_NEED_MORE_INFOS,     /* bool */
	MISDN_CFG_NOAUTORESPOND_ON_SETUP,     /* bool */
	MISDN_CFG_NTTIMEOUT,     	/* bool */
	MISDN_CFG_BRIDGING,              /* bool */
	MISDN_CFG_JITTERBUFFER,             /* int */
	MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD,              /* int */
	MISDN_CFG_CALLGROUP,           /* ast_group_t */
	MISDN_CFG_PICKUPGROUP,         /* ast_group_t */
	MISDN_CFG_MAX_IN,              /* int */
	MISDN_CFG_MAX_OUT,              /* int */
	MISDN_CFG_L1_TIMEOUT,          /* int */
	MISDN_CFG_OVERLAP_DIAL, 	/* int (bool)*/
	MISDN_CFG_MSNS,                /* char[] */
	MISDN_CFG_FAXDETECT,           /* char[] */
 	MISDN_CFG_FAXDETECT_CONTEXT,   /* char[] */
 	MISDN_CFG_FAXDETECT_TIMEOUT,   /* int */
	MISDN_CFG_PTP,                 /* int (bool) */
	MISDN_CFG_LAST,

	/* general config items */
	MISDN_GEN_FIRST,
#ifndef MISDN_1_2
	MISDN_GEN_MISDN_INIT,           /* char[] */
#endif
	MISDN_GEN_DEBUG,               /* int */
	MISDN_GEN_TRACEFILE,           /* char[] */
	MISDN_GEN_BRIDGING,            /* int (bool) */
	MISDN_GEN_STOP_TONE,           /* int (bool) */
	MISDN_GEN_APPEND_DIGITS2EXTEN, /* int (bool) */
	MISDN_GEN_DYNAMIC_CRYPT,       /* int (bool) */
	MISDN_GEN_CRYPT_PREFIX,        /* char[] */
	MISDN_GEN_CRYPT_KEYS,          /* char[] */
	MISDN_GEN_NTKEEPCALLS,          /* int (bool) */
	MISDN_GEN_NTDEBUGFLAGS,          /* int */
	MISDN_GEN_NTDEBUGFILE,          /* char[] */
	MISDN_GEN_LAST
};

enum misdn_cfg_method {
	METHOD_STANDARD = 0,
	METHOD_ROUND_ROBIN,
	METHOD_STANDARD_DEC
};

/* you must call misdn_cfg_init before any other function of this header file */
int misdn_cfg_init(int max_ports, int reload);
void misdn_cfg_reload(void);
void misdn_cfg_destroy(void);

void misdn_cfg_update_ptp( void );

/* if you requst a general config element, the port value is ignored. if the requested
 * value is not available, or the buffer is too small, the buffer will be nulled (in
 * case of a char* only its first byte will be nulled). */
void misdn_cfg_get(int port, enum misdn_cfg_elements elem, void* buf, int bufsize);

/* returns the enum element for the given name, returns MISDN_CFG_FIRST if none was found */
enum misdn_cfg_elements misdn_cfg_get_elem (char *name);

/* fills the buffer with the name of the given config element */
void misdn_cfg_get_name (enum misdn_cfg_elements elem, void *buf, int bufsize);

/* fills the buffer with the description of the given config element */
void misdn_cfg_get_desc (enum misdn_cfg_elements elem, void *buf, int bufsize, void *buf_default, int bufsize_default);

/* fills the buffer with a ',' separated list of all active ports */
void misdn_cfg_get_ports_string(char *ports);

/* fills the buffer with a nice printable string representation of the config element */
void misdn_cfg_get_config_string(int port, enum misdn_cfg_elements elem, char* buf, int bufsize);

/* returns the next available port number. returns -1 if the last one was reached. */
int misdn_cfg_get_next_port(int port);
int misdn_cfg_get_next_port_spin(int port);

int misdn_cfg_is_msn_valid(int port, char* msn);
int misdn_cfg_is_port_valid(int port);
int misdn_cfg_is_group_method(char *group, enum misdn_cfg_method meth);

#if 0
char *misdn_cfg_get_next_group(char *group);
int misdn_cfg_get_next_port_in_group(int port, char *group);
#endif

struct ast_jb_conf *misdn_get_global_jbconf(void);

#endif
