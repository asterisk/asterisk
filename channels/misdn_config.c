/*
 * Asterisk -- An open source telephony toolkit.
 * 
 * Copyright (C) 2005, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
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
 *
 */

/*!
 * \file
 *
 * \brief chan_misdn configuration management
 * \author Christian Richter <crich@beronet.com>
 *
 * \ingroup channel_drivers
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "chan_misdn_config.h"

#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/lock.h>
#include <asterisk/pbx.h>
#include <asterisk/strings.h>
#include <asterisk/utils.h>

#define AST_LOAD_CFG ast_config_load
#define AST_DESTROY_CFG ast_config_destroy

#define NO_DEFAULT "<>"
#define NONE 0

#define GEN_CFG 1
#define PORT_CFG 2
#define NUM_GEN_ELEMENTS (sizeof(gen_spec) / sizeof(struct misdn_cfg_spec))
#define NUM_PORT_ELEMENTS (sizeof(port_spec) / sizeof(struct misdn_cfg_spec))

enum misdn_cfg_type {
	MISDN_CTYPE_STR,
	MISDN_CTYPE_INT,
	MISDN_CTYPE_BOOL,
	MISDN_CTYPE_BOOLINT,
	MISDN_CTYPE_MSNLIST,
	MISDN_CTYPE_ASTGROUP
};

struct msn_list {
	char *msn;
	struct msn_list *next;
};

union misdn_cfg_pt {
	char *str;
	int *num;
	struct msn_list *ml;
	ast_group_t *grp;
	void *any;
};

struct misdn_cfg_spec {
	char name[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	enum misdn_cfg_type type;
	char def[BUFFERSIZE];
	int boolint_def;
};

static const struct misdn_cfg_spec port_spec[] = {
	{ "name", MISDN_CFG_GROUPNAME, MISDN_CTYPE_STR, "default", NONE },
	{ "allowed_bearers", MISDN_CFG_ALLOWED_BEARERS, MISDN_CTYPE_STR, "all", NONE },
	{ "rxgain", MISDN_CFG_RXGAIN, MISDN_CTYPE_INT, "0", NONE },
	{ "txgain", MISDN_CFG_TXGAIN, MISDN_CTYPE_INT, "0", NONE },
	{ "te_choose_channel", MISDN_CFG_TE_CHOOSE_CHANNEL, MISDN_CTYPE_BOOL, "no", NONE },
	{ "far_alerting", MISDN_CFG_FAR_ALERTING, MISDN_CTYPE_BOOL, "no", NONE },
	{ "pmp_l1_check", MISDN_CFG_PMP_L1_CHECK, MISDN_CTYPE_BOOL, "no", NONE },
	{ "reject_cause", MISDN_CFG_REJECT_CAUSE, MISDN_CTYPE_INT, "21", NONE },
	{ "block_on_alarm", MISDN_CFG_ALARM_BLOCK, MISDN_CTYPE_BOOL, "no", NONE },
	{ "hdlc", MISDN_CFG_HDLC, MISDN_CTYPE_BOOL, "no", NONE },
	{ "context", MISDN_CFG_CONTEXT, MISDN_CTYPE_STR, "default", NONE },
	{ "language", MISDN_CFG_LANGUAGE, MISDN_CTYPE_STR, "en", NONE },
	{ "musicclass", MISDN_CFG_MUSICCLASS, MISDN_CTYPE_STR, "default", NONE },
	{ "callerid", MISDN_CFG_CALLERID, MISDN_CTYPE_STR, "", NONE },
	{ "method", MISDN_CFG_METHOD, MISDN_CTYPE_STR, "standard", NONE },
	{ "dialplan", MISDN_CFG_DIALPLAN, MISDN_CTYPE_INT, "0", NONE },
	{ "localdialplan", MISDN_CFG_LOCALDIALPLAN, MISDN_CTYPE_INT, "0", NONE },
	{ "cpndialplan", MISDN_CFG_CPNDIALPLAN, MISDN_CTYPE_INT, "0", NONE },
	{ "nationalprefix", MISDN_CFG_NATPREFIX, MISDN_CTYPE_STR, "0", NONE },
	{ "internationalprefix", MISDN_CFG_INTERNATPREFIX, MISDN_CTYPE_STR, "00", NONE },
	{ "presentation", MISDN_CFG_PRES, MISDN_CTYPE_INT, "-1", NONE },
	{ "screen", MISDN_CFG_SCREEN, MISDN_CTYPE_INT, "-1", NONE },
	{ "always_immediate", MISDN_CFG_ALWAYS_IMMEDIATE, MISDN_CTYPE_BOOL, "no", NONE },
	{ "nodialtone", MISDN_CFG_NODIALTONE, MISDN_CTYPE_BOOL, "no", NONE },
	{ "immediate", MISDN_CFG_IMMEDIATE, MISDN_CTYPE_BOOL, "no", NONE },
	{ "senddtmf", MISDN_CFG_SENDDTMF, MISDN_CTYPE_BOOL, "no", NONE },
	{ "hold_allowed", MISDN_CFG_HOLD_ALLOWED, MISDN_CTYPE_BOOL, "no", NONE },
	{ "early_bconnect", MISDN_CFG_EARLY_BCONNECT, MISDN_CTYPE_BOOL, "yes", NONE },
	{ "incoming_early_audio", MISDN_CFG_INCOMING_EARLY_AUDIO, MISDN_CTYPE_BOOL, "no", NONE },
	{ "echocancel", MISDN_CFG_ECHOCANCEL, MISDN_CTYPE_BOOLINT, "0", 128 },
#ifdef MISDN_1_2
	{ "pipeline", MISDN_CFG_PIPELINE, MISDN_CTYPE_STR, NO_DEFAULT, NONE },
#endif
	{ "need_more_infos", MISDN_CFG_NEED_MORE_INFOS, MISDN_CTYPE_BOOL, "0", NONE },
	{ "noautorespond_on_setup", MISDN_CFG_NOAUTORESPOND_ON_SETUP, MISDN_CTYPE_BOOL, "0", NONE },
	{ "overlapdial", MISDN_CFG_OVERLAP_DIAL, MISDN_CTYPE_BOOLINT, "0", 4 },
	{ "nttimeout", MISDN_CFG_NTTIMEOUT, MISDN_CTYPE_BOOL, "no", NONE },
	{ "bridging", MISDN_CFG_BRIDGING, MISDN_CTYPE_BOOL, "yes", NONE },
	{ "jitterbuffer", MISDN_CFG_JITTERBUFFER, MISDN_CTYPE_INT, "4000", NONE },
	{ "jitterbuffer_upper_threshold", MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD, MISDN_CTYPE_INT, "0", NONE },
	{ "callgroup", MISDN_CFG_CALLGROUP, MISDN_CTYPE_ASTGROUP, NO_DEFAULT, NONE },
	{ "pickupgroup", MISDN_CFG_PICKUPGROUP, MISDN_CTYPE_ASTGROUP, NO_DEFAULT, NONE },
	{ "msns", MISDN_CFG_MSNS, MISDN_CTYPE_MSNLIST, "*", NONE }
};

static const struct misdn_cfg_spec gen_spec[] = {
	{ "debug", MISDN_GEN_DEBUG, MISDN_CTYPE_INT, "0", NONE },
#ifndef MISDN_1_2
	{ "misdn_init", MISDN_GEN_MISDN_INIT, MISDN_CTYPE_STR, "/etc/misdn-init.conf", NONE },
#endif
	{ "tracefile", MISDN_GEN_TRACEFILE, MISDN_CTYPE_STR, "/var/log/asterisk/misdn.log", NONE },
	{ "bridging", MISDN_GEN_BRIDGING, MISDN_CTYPE_BOOL, "yes", NONE },
	{ "stop_tone_after_first_digit", MISDN_GEN_STOP_TONE, MISDN_CTYPE_BOOL, "yes", NONE },
	{ "append_digits2exten", MISDN_GEN_APPEND_DIGITS2EXTEN, MISDN_CTYPE_BOOL, "yes", NONE },
	{ "dynamic_crypt", MISDN_GEN_DYNAMIC_CRYPT, MISDN_CTYPE_BOOL, "no", NONE },
	{ "crypt_prefix", MISDN_GEN_CRYPT_PREFIX, MISDN_CTYPE_STR, NO_DEFAULT, NONE },
	{ "crypt_keys", MISDN_GEN_CRYPT_KEYS, MISDN_CTYPE_STR, NO_DEFAULT, NONE },
	{ "ntdebugflags", MISDN_GEN_NTDEBUGFLAGS, MISDN_CTYPE_INT, "0", NONE },
	{ "ntdebugfile", MISDN_GEN_NTDEBUGFILE, MISDN_CTYPE_STR, "/var/log/misdn-nt.log", NONE }
};

/* array of port configs, default is at position 0. */
static union misdn_cfg_pt **port_cfg;
/* max number of available ports, is set on init */
static int max_ports;
/* general config */
static union misdn_cfg_pt *general_cfg;
/* storing the ptp flag separated to save memory */
static int *ptp;
/* maps enum config elements to array positions */
static int *map;

static ast_mutex_t config_mutex; 

#define CLI_ERROR(name, value, section) ({ \
	ast_log(LOG_WARNING, "misdn.conf: \"%s=%s\" (section: %s) invalid or out of range. " \
		"Please edit your misdn.conf and then do a \"misdn reload\".\n", name, value, section); \
})

static void _enum_array_map (void)
{
	int i, j;

	for (i = MISDN_CFG_FIRST + 1; i < MISDN_CFG_LAST; ++i) {
		if (i == MISDN_CFG_PTP)
			continue;
		for (j = 0; j < NUM_PORT_ELEMENTS; ++j) {
			if (port_spec[j].elem == i) {
				map[i] = j;
				break;
			}
		}
	}
	for (i = MISDN_GEN_FIRST + 1; i < MISDN_GEN_LAST; ++i) {
		for (j = 0; j < NUM_GEN_ELEMENTS; ++j) {
			if (gen_spec[j].elem == i) {
				map[i] = j;
				break;
			}
		}
	}
}

static int get_cfg_position (char *name, int type)
{
	int i;

	switch (type) {
	case PORT_CFG:
		for (i = 0; i < NUM_PORT_ELEMENTS; ++i) {
			if (!strcasecmp(name, port_spec[i].name))
				return i;
		}
		break;
	case GEN_CFG:
		for (i = 0; i < NUM_GEN_ELEMENTS; ++i) {
			if (!strcasecmp(name, gen_spec[i].name))
				return i;
		}
	}

	return -1;
}

static inline void misdn_cfg_lock (void)
{
	ast_mutex_lock(&config_mutex);
}

static inline void misdn_cfg_unlock (void)
{
	ast_mutex_unlock(&config_mutex);
}

static void _free_msn_list (struct msn_list* iter)
{
	if (iter->next)
		_free_msn_list(iter->next);
	if (iter->msn)
		free(iter->msn);
	free(iter);
}

static void _free_port_cfg (void)
{
	int i, j;
	int gn = map[MISDN_CFG_GROUPNAME];
	union misdn_cfg_pt* free_list[max_ports + 2];
	
	memset(free_list, 0, sizeof(free_list));
	free_list[0] = port_cfg[0];
	for (i = 1; i <= max_ports; ++i) {
		if (port_cfg[i][gn].str) {
			/* we always have a groupname in the non-default case, so this is fine */
			for (j = 1; j <= max_ports; ++j) {
				if (free_list[j] && free_list[j][gn].str == port_cfg[i][gn].str)
					break;
				else if (!free_list[j]) {
					free_list[j] = port_cfg[i];
					break;
				}
			}
		}
	}
	for (j = 0; free_list[j]; ++j) {
		for (i = 0; i < NUM_PORT_ELEMENTS; ++i) {
			if (free_list[j][i].any) {
				if (port_spec[i].type == MISDN_CTYPE_MSNLIST)
					_free_msn_list(free_list[j][i].ml);
				else
					free(free_list[j][i].any);
			}
		}
	}
}

static void _free_general_cfg (void)
{
	int i;

	for (i = 0; i < NUM_GEN_ELEMENTS; i++) 
		if (general_cfg[i].any)
			free(general_cfg[i].any);
}

void misdn_cfg_get (int port, enum misdn_cfg_elements elem, void *buf, int bufsize)
{
	int place;

	if ((elem < MISDN_CFG_LAST) && !misdn_cfg_is_port_valid(port)) {
		memset(buf, 0, bufsize);
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get! Port number %d is not valid.\n", port);
		return;
	}

	misdn_cfg_lock();
	if (elem == MISDN_CFG_PTP) {
		if (!memcpy(buf, &ptp[port], (bufsize > ptp[port]) ? sizeof(ptp[port]) : bufsize))
			memset(buf, 0, bufsize);
	} else {
		if ((place = map[elem]) < 0) {
			memset (buf, 0, bufsize);
			ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get! Invalid element (%d) requested.\n", elem);
		} else {
			if (elem < MISDN_CFG_LAST) {
				switch (port_spec[place].type) {
				case MISDN_CTYPE_STR:
					if (port_cfg[port][place].str) {
						if (!memccpy(buf, port_cfg[port][place].str, 0, bufsize))
							memset(buf, 0, 1);
					} else if (port_cfg[0][place].str) {
						if (!memccpy(buf, port_cfg[0][place].str, 0, bufsize))
							memset(buf, 0, 1);
					}
					break;
				default:
					if (port_cfg[port][place].any)
						memcpy(buf, port_cfg[port][place].any, bufsize);
					else if (port_cfg[0][place].any)
						memcpy(buf, port_cfg[0][place].any, bufsize);
					else
						memset(buf, 0, bufsize);
				}
			} else {
				switch (gen_spec[place].type) {
				case MISDN_CTYPE_STR:
					if (!general_cfg[place].str || !memccpy(buf, general_cfg[place].str, 0, bufsize))
						memset(buf, 0, 1);
					break;
				default:
					if (general_cfg[place].any)
						memcpy(buf, general_cfg[place].any, bufsize);
					else
						memset(buf, 0, bufsize);
				}
			}
		}
	}
	misdn_cfg_unlock();
}

int misdn_cfg_is_msn_valid (int port, char* msn)
{
	int re = 0;
	struct msn_list *iter;

	if (!misdn_cfg_is_port_valid(port)) {
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_is_msn_valid! Port number %d is not valid.\n", port);
		return 0;
	}

	misdn_cfg_lock();
	if (port_cfg[port][map[MISDN_CFG_MSNS]].ml)
		iter = port_cfg[port][map[MISDN_CFG_MSNS]].ml;
	else
		iter = port_cfg[0][map[MISDN_CFG_MSNS]].ml;
	for (; iter; iter = iter->next) 
		if (*(iter->msn) == '*' || ast_extension_match(iter->msn, msn)) {
			re = 1;
			break;
		}
	misdn_cfg_unlock();

	return re;
}

int misdn_cfg_is_port_valid (int port)
{
	return (port >= 1 && port <= max_ports);
}

int misdn_cfg_is_group_method (char *group, enum misdn_cfg_method meth)
{
	int i, re = 0;
	char *method ;

	misdn_cfg_lock();

	method = port_cfg[0][map[MISDN_CFG_METHOD]].str;

	for (i = 1; i <= max_ports; i++) {
		if (port_cfg[i] && port_cfg[i][map[MISDN_CFG_GROUPNAME]].str) {
			if (!strcasecmp(port_cfg[i][map[MISDN_CFG_GROUPNAME]].str, group))
				method = (port_cfg[i][map[MISDN_CFG_METHOD]].str ? 
						  port_cfg[i][map[MISDN_CFG_METHOD]].str : port_cfg[0][map[MISDN_CFG_METHOD]].str);
		}
	}

	if (method) {
		switch (meth) {
		case METHOD_STANDARD:		re = !strcasecmp(method, "standard");
									break;
		case METHOD_ROUND_ROBIN:	re = !strcasecmp(method, "round_robin");
									break;
		case METHOD_STANDARD_DEC:	re = !strcasecmp(method, "standard_dec");
									break;
		}
	}
	misdn_cfg_unlock();

	return re;
}

void misdn_cfg_get_ports_string (char *ports)
{
	char tmp[16];
	int l, i;
	int gn = map[MISDN_CFG_GROUPNAME];

	*ports = 0;

	misdn_cfg_lock();
	for (i = 1; i <= max_ports; i++) {
		if (port_cfg[i][gn].str) {
			if (ptp[i])
				sprintf(tmp, "%dptp,", i);
			else
				sprintf(tmp, "%d,", i);
			strcat(ports, tmp);
		}
	}
	misdn_cfg_unlock();

	if ((l = strlen(ports)))
		ports[l-1] = 0;
}

void misdn_cfg_get_config_string (int port, enum misdn_cfg_elements elem, char* buf, int bufsize)
{
	int place;
	char tempbuf[BUFFERSIZE] = "";
	struct msn_list *iter;

	if ((elem < MISDN_CFG_LAST) && !misdn_cfg_is_port_valid(port)) {
		*buf = 0;
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get_config_string! Port number %d is not valid.\n", port);
		return;
	}

	place = map[elem];

	misdn_cfg_lock();
	if (elem == MISDN_CFG_PTP) {
		snprintf(buf, bufsize, " -> ptp: %s", ptp[port] ? "yes" : "no");
	}
	else if (elem > MISDN_CFG_FIRST && elem < MISDN_CFG_LAST) {
		switch (port_spec[place].type) {
		case MISDN_CTYPE_INT:
		case MISDN_CTYPE_BOOLINT:
			if (port_cfg[port][place].num)
				snprintf(buf, bufsize, " -> %s: %d", port_spec[place].name, *port_cfg[port][place].num);
			else if (port_cfg[0][place].num)
				snprintf(buf, bufsize, " -> %s: %d", port_spec[place].name, *port_cfg[0][place].num);
			else
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			break;
		case MISDN_CTYPE_BOOL:
			if (port_cfg[port][place].num)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, *port_cfg[port][place].num ? "yes" : "no");
			else if (port_cfg[0][place].num)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, *port_cfg[0][place].num ? "yes" : "no");
			else
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			break;
		case MISDN_CTYPE_ASTGROUP:
			if (port_cfg[port][place].grp)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, 
						 ast_print_group(tempbuf, sizeof(tempbuf), *port_cfg[port][place].grp));
			else if (port_cfg[0][place].grp)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, 
						 ast_print_group(tempbuf, sizeof(tempbuf), *port_cfg[0][place].grp));
			else
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			break;
		case MISDN_CTYPE_MSNLIST:
			if (port_cfg[port][place].ml)
				iter = port_cfg[port][place].ml;
			else
				iter = port_cfg[0][place].ml;
			if (iter) {
				for (; iter; iter = iter->next)
					sprintf(tempbuf, "%s%s, ", tempbuf, iter->msn);
				tempbuf[strlen(tempbuf)-2] = 0;
			}
			snprintf(buf, bufsize, " -> msns: %s", *tempbuf ? tempbuf : "none");
			break;
		case MISDN_CTYPE_STR:
			if ( port_cfg[port][place].str) {
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, port_cfg[port][place].str);
			} else if (port_cfg[0][place].str) {
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, port_cfg[0][place].str);
			} else {
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			}
			break;
		}
	} else if (elem > MISDN_GEN_FIRST && elem < MISDN_GEN_LAST) {
		switch (gen_spec[place].type) {
		case MISDN_CTYPE_INT:
		case MISDN_CTYPE_BOOLINT:
			if (general_cfg[place].num)
				snprintf(buf, bufsize, " -> %s: %d", gen_spec[place].name, *general_cfg[place].num);
			else
				snprintf(buf, bufsize, " -> %s:", gen_spec[place].name);
			break;
		case MISDN_CTYPE_BOOL:
			if (general_cfg[place].num)
				snprintf(buf, bufsize, " -> %s: %s", gen_spec[place].name, *general_cfg[place].num ? "yes" : "no");
			else
				snprintf(buf, bufsize, " -> %s:", gen_spec[place].name);
			break;
		case MISDN_CTYPE_STR:
			if ( general_cfg[place].str) {
				snprintf(buf, bufsize, " -> %s: %s", gen_spec[place].name, general_cfg[place].str);
			} else {
				snprintf(buf, bufsize, " -> %s:", gen_spec[place].name);
			}
			break;
		default:
			snprintf(buf, bufsize, " -> type of %s not handled yet", gen_spec[place].name);
			break;
		}
	} else {
		*buf = 0;
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get_config_string! Invalid config element (%d) requested.\n", elem);
	}
	misdn_cfg_unlock();
}

int misdn_cfg_get_next_port (int port)
{
	int p = -1;
	int gn = map[MISDN_CFG_GROUPNAME];
	
	misdn_cfg_lock();
	for (port++; port <= max_ports; port++) {
		if (port_cfg[port][gn].str) {
			p = port;
			break;
		}
	}
	misdn_cfg_unlock();

	return p;
}

int misdn_cfg_get_next_port_spin (int port)
{
	int p = misdn_cfg_get_next_port(port);
	return (p > 0) ? p : misdn_cfg_get_next_port(0);
}

static int _parse (union misdn_cfg_pt *dest, char *value, enum misdn_cfg_type type, int boolint_def)
{
	int re = 0;
	int len, tmp;
	char *valtmp;

	switch (type) {
	case MISDN_CTYPE_STR:
		if ((len = strlen(value))) {
			dest->str = (char *)malloc((len + 1) * sizeof(char));
			strncpy(dest->str, value, len);
			dest->str[len] = 0;
		} else {
			dest->str = (char *)malloc( sizeof(char));
			dest->str[0] = 0;
		}
		break;
	case MISDN_CTYPE_INT:
	{
		char *pat;
		if (strchr(value,'x')) 
			pat="%x";
		else
			pat="%d";
		if (sscanf(value, pat, &tmp)) {
			dest->num = (int *)malloc(sizeof(int));
			memcpy(dest->num, &tmp, sizeof(int));
		} else
			re = -1;
	}
		break;
	case MISDN_CTYPE_BOOL:
		dest->num = (int *)malloc(sizeof(int));
		*(dest->num) = (ast_true(value) ? 1 : 0);
		break;
	case MISDN_CTYPE_BOOLINT:
		dest->num = (int *)malloc(sizeof(int));
		if (sscanf(value, "%d", &tmp)) {
			memcpy(dest->num, &tmp, sizeof(int));
		} else {
			*(dest->num) = (ast_true(value) ? boolint_def : 0);
		}
		break;
	case MISDN_CTYPE_MSNLIST:
		for (valtmp = strsep(&value, ","); valtmp; valtmp = strsep(&value, ",")) {
			if ((len = strlen(valtmp))) {
				struct msn_list *ml = (struct msn_list *)malloc(sizeof(struct msn_list));
				ml->msn = (char *)calloc(len+1, sizeof(char));
				strncpy(ml->msn, valtmp, len);
				ml->next = dest->ml;
				dest->ml = ml;
			}
		}
		break;
	case MISDN_CTYPE_ASTGROUP:
		dest->grp = (ast_group_t *)malloc(sizeof(ast_group_t));
		*(dest->grp) = ast_get_group(value);
		break;
	}

	return re;
}

static void _build_general_config (struct ast_variable *v)
{
	int pos;

	for (; v; v = v->next) {
		if (((pos = get_cfg_position(v->name, GEN_CFG)) < 0) || 
			(_parse(&general_cfg[pos], v->value, gen_spec[pos].type, gen_spec[pos].boolint_def) < 0))
			CLI_ERROR(v->name, v->value, "general");
	}
}

static void _build_port_config (struct ast_variable *v, char *cat)
{
	int pos, i;
	union misdn_cfg_pt cfg_tmp[NUM_PORT_ELEMENTS];
	int cfg_for_ports[max_ports + 1];

	if (!v || !cat)
		return;

	memset(cfg_tmp, 0, sizeof(cfg_tmp));
	memset(cfg_for_ports, 0, sizeof(cfg_for_ports));

	if (!strcasecmp(cat, "default")) {
		cfg_for_ports[0] = 1;
	}

	if (((pos = get_cfg_position("name", PORT_CFG)) < 0) || 
		(_parse(&cfg_tmp[pos], cat, port_spec[pos].type, port_spec[pos].boolint_def) < 0)) {
		CLI_ERROR(v->name, v->value, cat);
		return;
	}

	for (; v; v = v->next) {
		if (!strcasecmp(v->name, "ports")) {
			char *token;
			char ptpbuf[BUFFERSIZE] = "";
			int start, end;
			for (token = strsep(&v->value, ","); token; token = strsep(&v->value, ","), *ptpbuf = 0) { 
				if (!*token)
					continue;
				if (sscanf(token, "%d-%d%s", &start, &end, ptpbuf) >= 2) {
					for (; start <= end; start++) {
						if (start <= max_ports && start > 0) {
							cfg_for_ports[start] = 1;
							ptp[start] = (strstr(ptpbuf, "ptp")) ? 1 : 0;
						} else
							CLI_ERROR(v->name, v->value, cat);
					}
				} else {
					if (sscanf(token, "%d%s", &start, ptpbuf)) {
						if (start <= max_ports && start > 0) {
							cfg_for_ports[start] = 1;
							ptp[start] = (strstr(ptpbuf, "ptp")) ? 1 : 0;
						} else
							CLI_ERROR(v->name, v->value, cat);
					} else
						CLI_ERROR(v->name, v->value, cat);
				}
			}
		} else {
			if (((pos = get_cfg_position(v->name, PORT_CFG)) < 0) || 
				(_parse(&cfg_tmp[pos], v->value, port_spec[pos].type, port_spec[pos].boolint_def) < 0))
				CLI_ERROR(v->name, v->value, cat);
		}
	}

	for (i = 0; i < (max_ports + 1); ++i) {
		if (cfg_for_ports[i]) {
			memcpy(port_cfg[i], cfg_tmp, sizeof(cfg_tmp));
		}
	}
}

void misdn_cfg_update_ptp (void)
{
#ifndef MISDN_1_2
	char misdn_init[BUFFERSIZE];
	char line[BUFFERSIZE];
	FILE *fp;
	char *tok, *p, *end;
	int port;

	misdn_cfg_get(0, MISDN_GEN_MISDN_INIT, &misdn_init, sizeof(misdn_init));

	if (misdn_init) {
		fp = fopen(misdn_init, "r");
		if (fp) {
			while(fgets(line, sizeof(line), fp)) {
				if (!strncmp(line, "nt_ptp", 6)) {
					for (tok = strtok_r(line,",=", &p);
						 tok;
						 tok = strtok_r(NULL,",=", &p)) {
						port = strtol(tok, &end, 10);
						if (end != tok && misdn_cfg_is_port_valid(port)) {
							misdn_cfg_lock();
							ptp[port] = 1;
							misdn_cfg_unlock();
						}
					}
				}
			}
			fclose(fp);
		} else {
			ast_log(LOG_WARNING,"Couldn't open %s: %s\n", misdn_init, strerror(errno));
		}
	}
#else
	int i;
	int proto;
	char filename[128];
	FILE *fp;

	for (i = 1; i <= max_ports; ++i) {
		snprintf(filename, sizeof(filename), "/sys/class/mISDN-stacks/st-%08x/protocol", i << 8);
		fp = fopen(filename, "r");
		if (!fp) {
			ast_log(LOG_WARNING, "Could not open %s: %s\n", filename, strerror(errno));
			continue;
		}
		if (fscanf(fp, "0x%08x", &proto) != 1)
			ast_log(LOG_WARNING, "Could not parse contents of %s!\n", filename);
		else
			ptp[i] = proto & 1<<5 ? 1 : 0;
		fclose(fp);
	}
#endif
}

static void _fill_defaults (void)
{
	int i;

	for (i = 0; i < NUM_PORT_ELEMENTS; ++i) {
		if (!port_cfg[0][i].any && strcasecmp(port_spec[i].def, NO_DEFAULT))
			_parse(&(port_cfg[0][i]), (char *)port_spec[i].def, port_spec[i].type, port_spec[i].boolint_def);
	}
	for (i = 0; i < NUM_GEN_ELEMENTS; ++i) {
		if (!general_cfg[i].any && strcasecmp(gen_spec[i].def, NO_DEFAULT))
			_parse(&(general_cfg[i]), (char *)gen_spec[i].def, gen_spec[i].type, gen_spec[i].boolint_def);
	}
}

void misdn_cfg_reload (void)
{
	misdn_cfg_init (0);
}

void misdn_cfg_destroy (void)
{
	misdn_cfg_lock();

	_free_port_cfg();
	_free_general_cfg();

	free(port_cfg);
	free(general_cfg);
	free(ptp);
	free(map);

	misdn_cfg_unlock();
	ast_mutex_destroy(&config_mutex);
}

int misdn_cfg_init (int this_max_ports)
{
	char config[] = "misdn.conf";
	char *cat, *p;
	int i;
	struct ast_config *cfg;
	struct ast_variable *v;

	if (!(cfg = AST_LOAD_CFG(config))) {
		ast_log(LOG_WARNING,"no misdn.conf ?\n");
		return -1;
	}

	misdn_cfg_lock();

	if (this_max_ports) {
		/* this is the first run */
		max_ports = this_max_ports;
		p = (char *)calloc(1, (max_ports + 1) * sizeof(union misdn_cfg_pt *)
						   + (max_ports + 1) * NUM_PORT_ELEMENTS * sizeof(union misdn_cfg_pt));
		port_cfg = (union misdn_cfg_pt **)p;
		p += (max_ports + 1) * sizeof(union misdn_cfg_pt *);
		for (i = 0; i <= max_ports; ++i) {
			port_cfg[i] = (union misdn_cfg_pt *)p;
			p += NUM_PORT_ELEMENTS * sizeof(union misdn_cfg_pt);
		}
		general_cfg = (union misdn_cfg_pt *)calloc(1, sizeof(union misdn_cfg_pt *) * NUM_GEN_ELEMENTS);
		ptp = (int *)calloc(max_ports + 1, sizeof(int));
		map = (int *)calloc(MISDN_GEN_LAST + 1, sizeof(int));
		_enum_array_map();
	}
	else {
		/* misdn reload */
		_free_port_cfg();
		_free_general_cfg();
		memset(port_cfg[0], 0, NUM_PORT_ELEMENTS * sizeof(union misdn_cfg_pt) * (max_ports + 1));
		memset(general_cfg, 0, sizeof(union misdn_cfg_pt *) * NUM_GEN_ELEMENTS);
		memset(ptp, 0, sizeof(int) * (max_ports + 1));
	}

	cat = ast_category_browse(cfg, NULL);

	while(cat) {
		v = ast_variable_browse(cfg, cat);
		if (!strcasecmp(cat,"general")) {
			_build_general_config(v);
		} else {
			_build_port_config(v, cat);
		}
		cat = ast_category_browse(cfg,cat);
	}

	_fill_defaults();

	misdn_cfg_unlock();
	AST_DESTROY_CFG(cfg);

	return 0;
}
