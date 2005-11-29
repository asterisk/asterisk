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



#ifndef CHAN_MISDN_CONFIG_H
#define CHAN_MISDN_CONFIG_H

#define BUFFERSIZE 512

enum misdn_cfg_elements {

	/* port config items */
	MISDN_CFG_FIRST = 0,
	MISDN_CFG_PTP,                 /* int (bool) */
	MISDN_CFG_GROUPNAME,           /* char[] */
	MISDN_CFG_RXGAIN,              /* int */
	MISDN_CFG_TXGAIN,              /* int */
	MISDN_CFG_TE_CHOOSE_CHANNEL,   /* int (bool) */
	MISDN_CFG_CONTEXT,             /* char[] */
	MISDN_CFG_LANGUAGE,            /* char[] */
	MISDN_CFG_CALLERID,            /* char[] */
	MISDN_CFG_METHOD,              /* char[] */
	MISDN_CFG_DIALPLAN,            /* int */
	MISDN_CFG_NATPREFIX,           /* char[] */
	MISDN_CFG_INTERNATPREFIX,      /* char[] */
	MISDN_CFG_PRES,                /* int (bool) */
	MISDN_CFG_ALWAYS_IMMEDIATE,    /* int (bool) */
	MISDN_CFG_IMMEDIATE,           /* int (bool) */
	MISDN_CFG_HOLD_ALLOWED,        /* int (bool) */
	MISDN_CFG_EARLY_BCONNECT,      /* int (bool) */
	MISDN_CFG_USE_CALLINGPRES,     /* int (bool) */
	MISDN_CFG_ECHOCANCEL,          /* int */
	MISDN_CFG_ECHOCANCELWHENBRIDGED,  /* int (bool) */
	MISDN_CFG_ECHOTRAINING,        /* int (bool) */
	MISDN_CFG_CALLGROUP,           /* ast_group_t */
	MISDN_CFG_PICKUPGROUP,         /* ast_group_t */
	MISDN_CFG_MSNS,                /* char[] */
	MISDN_CFG_LAST,
	
	/* general config items */
	MISDN_GEN_FIRST,
	MISDN_GEN_DEBUG,               /* int */
	MISDN_GEN_TRACEFILE,           /* char[] */
	MISDN_GEN_TRACE_CALLS,         /* int (bool) */
	MISDN_GEN_TRACE_DIR,           /* char[] */
	MISDN_GEN_BRIDGING,            /* int (bool) */
	MISDN_GEN_STOP_TONE,           /* int (bool) */
	MISDN_GEN_APPEND_DIGITS2EXTEN, /* int (bool) */
	MISDN_GEN_L1_INFO_OK,          /* int (bool) */
	MISDN_GEN_CLEAR_L3,            /* int (bool) */
	MISDN_GEN_DYNAMIC_CRYPT,       /* int (bool) */
	MISDN_GEN_CRYPT_PREFIX,        /* char[] */
	MISDN_GEN_CRYPT_KEYS,          /* char[] */
	MISDN_GEN_LAST
};

enum misdn_cfg_method {
	METHOD_STANDARD = 0,
	METHOD_ROUND_ROBIN
};

/* you must call misdn_cfg_init before any other function of this header file */
void misdn_cfg_init(int max_ports); 
void misdn_cfg_reload(void);
void misdn_cfg_destroy(void);

/* if you requst a general config element, the port value is ignored. if the requested 
 * value is not available, or the buffer is too small, the buffer will be nulled (in 
 * case of a char* only its first byte will be nulled). */
void misdn_cfg_get(int port, enum misdn_cfg_elements elem, void* buf, int bufsize);

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

#endif
