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

#include "chan_misdn_config.h"

#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/lock.h>
#include <asterisk/strings.h>

#include <asterisk/utils.h>
#define AST_LOAD_CFG ast_config_load
#define AST_DESTROY_CFG ast_config_destroy

#define DEF_ECHOCANCEL 128
#define DEF_ECHOTRAINING 1

struct msn_list {
	char *msn;
	struct msn_list *next;
};

struct port_config {
	char *name;
	int *rxgain;
	int *txgain;
	int *te_choose_channel;
	char *context;
	char *language;
	char *callerid;
	char *method;
	int *dialplan; 
	char *nationalprefix;
	char *internationalprefix;
	int *pres;
	int *always_immediate;
	int *immediate;
	int *hold_allowed;
	int *early_bconnect;
	int *use_callingpres;
	int *echocancel;
	int *echocancelwhenbridged;
	int *echotraining;
	struct msn_list *msn_list;
	ast_group_t *callgroup;		/* Call group */
	ast_group_t *pickupgroup;	/* Pickup group */
};

struct general_config {
	int *debug;
	char *tracefile;
	int *trace_calls;
	char *trace_dir;
	int *bridging;
	int *stop_tone_after_first_digit;
	int *append_digits2exten;
	int *l1_info_ok;
	int *clear_l3;
	int *dynamic_crypt;
	char *crypt_prefix;
	char *crypt_keys;
};

/* array of port configs, default is at position 0. */
static struct port_config **port_cfg;
/* max number of available ports, is set on init */
static int max_ports;
/* general config */
static struct general_config *general_cfg;
/* storing the ptp flag separated to save memory */
static int *ptp;

static ast_mutex_t config_mutex; 


static inline void misdn_cfg_lock (void) {
	ast_mutex_lock(&config_mutex);
}

static inline void misdn_cfg_unlock (void) {
	ast_mutex_unlock(&config_mutex);
}

static void free_msn_list (struct msn_list* iter) {
	if (iter->next)
		free_msn_list(iter->next);
	if (iter->msn)
		free(iter->msn);
	free(iter);
}

static void free_port_cfg (void) {

	struct port_config **free_list = (struct port_config **)calloc(max_ports + 1, sizeof(struct port_config *));

	int i, j;

	for (i = 0; i < max_ports; i++) {
		if (port_cfg[i]) {
			for (j = 0; j < max_ports && free_list[j]; j++) {
				if (free_list[j] && free_list[j] == port_cfg[i])
					continue; /* already in list */
				free_list[j] = port_cfg[i];
			}
		}
	}

#define FREE_ELEM(elem) ({ \
		if (free_list[i]->elem) \
			free(free_list[i]->elem); \
	})
	
	for (i = 0; i < max_ports; i++) {
		if (free_list[i]) {
			FREE_ELEM(name);
			FREE_ELEM(rxgain);
			FREE_ELEM(txgain);
			FREE_ELEM(te_choose_channel);
			FREE_ELEM(context);
			FREE_ELEM(language);
			FREE_ELEM(callerid);
			FREE_ELEM(method);
			FREE_ELEM(dialplan);
			FREE_ELEM(nationalprefix);
			FREE_ELEM(internationalprefix);
			FREE_ELEM(pres);
			FREE_ELEM(always_immediate);
			FREE_ELEM(immediate);
			FREE_ELEM(hold_allowed);
			FREE_ELEM(early_bconnect);
			FREE_ELEM(use_callingpres);
			FREE_ELEM(echocancel);
			FREE_ELEM(echocancelwhenbridged);
			FREE_ELEM(echotraining);
			if (free_list[i]->msn_list)
				free_msn_list(free_list[i]->msn_list);
			FREE_ELEM(callgroup);
			FREE_ELEM(pickupgroup);
			free(free_list[i]);
		}
	}
	free(free_list);
}

static void free_general_cfg (void) {

#define FREE_GEN_ELEM(elem) ({ \
		if (general_cfg->elem) \
			free(general_cfg->elem); \
	})
	
	FREE_GEN_ELEM(debug);
	FREE_GEN_ELEM(tracefile);
	FREE_GEN_ELEM(trace_calls);
	FREE_GEN_ELEM(trace_dir);
	FREE_GEN_ELEM(bridging);
	FREE_GEN_ELEM(stop_tone_after_first_digit);
	FREE_GEN_ELEM(append_digits2exten);
	FREE_GEN_ELEM(l1_info_ok);
	FREE_GEN_ELEM(clear_l3);
	FREE_GEN_ELEM(dynamic_crypt);
	FREE_GEN_ELEM(crypt_prefix);
	FREE_GEN_ELEM(crypt_keys);
}

#define GET_PORTCFG_STRCPY(item) ({ \
		char *temp; \
		if (port_cfg[port] && port_cfg[port]->item) \
			temp = port_cfg[port]->item; \
		else \
			temp = port_cfg[0]->item; \
		if (!temp || !memccpy((void *)buf, (void *)temp, '\0', bufsize)) \
			memset(buf, 0, 1); \
	})

#define GET_GENCFG_STRCPY(item) ({ \
		if (general_cfg && general_cfg->item) { \
			if (!memccpy((void *)buf, (void *)general_cfg->item, '\0', bufsize)) \
				memset(buf, 0, 1); \
		} else \
			memset(buf, 0, 1); \
	})

#define GET_PORTCFG_MEMCPY(item) ({ \
		typeof(port_cfg[0]->item) temp; \
		if (port_cfg[port] && port_cfg[port]->item) \
			temp = port_cfg[port]->item; \
		else \
			temp = port_cfg[0]->item; \
		if (temp) { \
			int l = sizeof(*temp); \
			if (l > bufsize) \
				memset(buf, 0, bufsize); \
			else \
				memcpy(buf, temp, l); \
		} else \
			memset(buf, 0, bufsize); \
	})

#define GET_GENCFG_MEMCPY(item) ({ \
		if (general_cfg && general_cfg->item) { \
			typeof(general_cfg->item) temp = general_cfg->item; \
			int l = sizeof(*temp); \
			if (l > bufsize) \
				memset(buf, 0, bufsize); \
			else \
				memcpy(buf, temp, l); \
		} else \
			memset(buf, 0, bufsize); \
	})

void misdn_cfg_get(int port, enum misdn_cfg_elements elem, void *buf, int bufsize) {
	
	if (!(elem > MISDN_GEN_FIRST) && !misdn_cfg_is_port_valid(port)) {
		memset(buf, 0, bufsize);
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get! Port number %d is not valid.\n", port);
		return;
	}

	misdn_cfg_lock();
	
	switch (elem) {
		
		/* port config elements */
			
		case MISDN_CFG_PTP:		if (sizeof(ptp[port]) <= bufsize)
							memcpy(buf, &ptp[port], sizeof(ptp[port]));
						else
							buf = 0; /* error, should not happen */
						break;
		case MISDN_CFG_GROUPNAME:	GET_PORTCFG_STRCPY(name);
						break;
		case MISDN_CFG_RXGAIN:		GET_PORTCFG_MEMCPY(rxgain);
						break;
		case MISDN_CFG_TXGAIN:		GET_PORTCFG_MEMCPY(txgain);
						break;
		case MISDN_CFG_TE_CHOOSE_CHANNEL:
						GET_PORTCFG_MEMCPY(te_choose_channel);
						break;
		case MISDN_CFG_CONTEXT:		GET_PORTCFG_STRCPY(context);
						break;
		case MISDN_CFG_LANGUAGE:	GET_PORTCFG_STRCPY(language);
						break;
		case MISDN_CFG_CALLERID:	GET_PORTCFG_STRCPY(callerid);
						break;
		case MISDN_CFG_METHOD:		GET_PORTCFG_STRCPY(method);
						break;
		case MISDN_CFG_DIALPLAN:	GET_PORTCFG_MEMCPY(dialplan);
						break;
		case MISDN_CFG_NATPREFIX:	GET_PORTCFG_STRCPY(nationalprefix);
						break;
		case MISDN_CFG_INTERNATPREFIX:
						GET_PORTCFG_STRCPY(internationalprefix);
						break;
		case MISDN_CFG_PRES:		GET_PORTCFG_MEMCPY(pres);
						break;
		case MISDN_CFG_ALWAYS_IMMEDIATE:
						GET_PORTCFG_MEMCPY(always_immediate);
						break;
		case MISDN_CFG_IMMEDIATE:	GET_PORTCFG_MEMCPY(immediate);
						break;
		case MISDN_CFG_HOLD_ALLOWED:
						GET_PORTCFG_MEMCPY(hold_allowed);
						break;
		case MISDN_CFG_EARLY_BCONNECT:
						GET_PORTCFG_MEMCPY(early_bconnect);
						break;
		case MISDN_CFG_USE_CALLINGPRES:
						GET_PORTCFG_MEMCPY(use_callingpres);
						break;
		case MISDN_CFG_ECHOCANCEL:
						GET_PORTCFG_MEMCPY(echocancel );
						break;
		case MISDN_CFG_ECHOCANCELWHENBRIDGED:
						GET_PORTCFG_MEMCPY(echocancelwhenbridged);
						break;
		case MISDN_CFG_ECHOTRAINING:
						GET_PORTCFG_MEMCPY(echotraining);
						break;
		case MISDN_CFG_CALLGROUP:	GET_PORTCFG_MEMCPY(callgroup);
						break;
		case MISDN_CFG_PICKUPGROUP:	GET_PORTCFG_MEMCPY(pickupgroup);
						break;
		
		/* general config elements */
			
		case MISDN_GEN_DEBUG:		GET_GENCFG_MEMCPY(debug);
						break;
		case MISDN_GEN_TRACEFILE:	GET_GENCFG_STRCPY(tracefile);
						break;
		case MISDN_GEN_TRACE_CALLS: GET_GENCFG_MEMCPY(trace_calls);
						break;
		case MISDN_GEN_TRACE_DIR:	GET_GENCFG_STRCPY(trace_dir);
						break;
		case MISDN_GEN_BRIDGING:	GET_GENCFG_MEMCPY(bridging);
						break;
		case MISDN_GEN_STOP_TONE:	GET_GENCFG_MEMCPY(stop_tone_after_first_digit);
						break;
		case MISDN_GEN_APPEND_DIGITS2EXTEN: 
						GET_GENCFG_MEMCPY(append_digits2exten);
						break;
		case MISDN_GEN_L1_INFO_OK:	GET_GENCFG_MEMCPY(l1_info_ok);
						break;
		case MISDN_GEN_CLEAR_L3:	GET_GENCFG_MEMCPY(clear_l3);
						break;
		case MISDN_GEN_DYNAMIC_CRYPT:	GET_GENCFG_MEMCPY(dynamic_crypt);
						break;
		case MISDN_GEN_CRYPT_PREFIX:	GET_GENCFG_STRCPY(crypt_prefix);
						break;
		case MISDN_GEN_CRYPT_KEYS: 	GET_GENCFG_STRCPY(crypt_keys);
						break;
		default:			memset(buf, 0, bufsize);
	}
	
	misdn_cfg_unlock();
}

int misdn_cfg_is_msn_valid (int port, char* msn) {
	
	if (!misdn_cfg_is_port_valid(port)) {
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_is_msn_valid! Port number %d is not valid.\n", port);
		return 0;
	}
	
	struct msn_list *iter;
	
	misdn_cfg_lock();
	
	if (port_cfg[port]->msn_list)
		iter = port_cfg[port]->msn_list;
	else
		iter = port_cfg[0]->msn_list;
	for (; iter; iter = iter->next) 
		if (*(iter->msn) == '*' || !strcasecmp(iter->msn, msn)) {
			misdn_cfg_unlock();
			return 1;
		}
	
	misdn_cfg_unlock();
	
	return 0;
}

int misdn_cfg_is_port_valid (int port) {
	
	misdn_cfg_lock();
	
	if (port < 1 || port > max_ports) {
		misdn_cfg_unlock();
		return 0;
	}

	int valid = (port_cfg[port] != NULL);

	misdn_cfg_unlock();

	return valid;
}

int misdn_cfg_is_group_method (char *group, enum misdn_cfg_method meth) {

	int i, re = 0;
	char *method = NULL;

	misdn_cfg_lock();
	
	for (i = 0; i < max_ports; i++) {
		if (port_cfg[i]) {
			if (!strcasecmp(port_cfg[i]->name, group))
				method = port_cfg[i]->method ? port_cfg[i]->method : port_cfg[0]->method;
		}
	}

	if (method) {
		switch (meth) {
		case METHOD_STANDARD:		re = !strcasecmp(method, "standard");
									break;
		case METHOD_ROUND_ROBIN:	re = !strcasecmp(method, "round_robin");
									break;
		}
	}

	misdn_cfg_unlock();

	return re;
}

void misdn_cfg_get_ports_string (char *ports) {
	*ports = 0;
	char tmp[16];
	int l;
	
	misdn_cfg_lock();

	int i = 1;
	for (; i <= max_ports; i++) {
		if (port_cfg[i]) {
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

#define GET_CFG_STRING(typestr, type) ({ \
		if (port_cfg[port] && port_cfg[port]->type) \
			snprintf(buf, bufsize, "%s " #typestr ": %s", begin, port_cfg[port]->type); \
		else \
			snprintf(buf, bufsize, "%s " #typestr ": %s", begin, port_cfg[0]->type); \
	}) \

#define GET_GEN_STRING(typestr, type) ({ \
		snprintf(buf, bufsize, "%s " #typestr ": %s", begin, general_cfg->type ? general_cfg->type : "not set"); \
	}) \

#define GET_CFG_INT(typestr, type) ({ \
		if (port_cfg[port] && port_cfg[port]->type) \
			snprintf(buf, bufsize, "%s " #typestr ": %d", begin, *port_cfg[port]->type); \
		else \
			snprintf(buf, bufsize, "%s " #typestr ": %d", begin, *port_cfg[0]->type); \
	}) \

#define GET_GEN_INT(typestr, type) ({ \
		snprintf(buf, bufsize, "%s " #typestr ": %d", begin, general_cfg->type ? *general_cfg->type : 0); \
	}) \

#define GET_CFG_BOOL(typestr, type, yes, no) ({ \
		int bool; \
		if (port_cfg[port] && port_cfg[port]->type) \
			bool = *port_cfg[port]->type; \
		else \
			bool = *port_cfg[0]->type; \
		snprintf(buf, bufsize, "%s " #typestr ": %s", begin, bool ? #yes : #no); \
	}) \

#define GET_CFG_HYBRID(typestr, type, yes, no) ({ \
		int bool; \
		if (port_cfg[port] && port_cfg[port]->type) \
			bool = *port_cfg[port]->type; \
		else \
			bool = *port_cfg[0]->type; \
		if (bool == 1 || bool == 0) \
			snprintf(buf, bufsize, "%s " #typestr ": %s", begin, bool ? #yes : #no); \
		else \
			snprintf(buf, bufsize, "%s " #typestr ": %d", begin, bool); \
	}) \

#define GET_GEN_BOOL(typestr, type, yes, no) ({ \
		snprintf(buf, bufsize, "%s " #typestr ": %s", begin, general_cfg->type ? (*general_cfg->type ? #yes : #no) : "not set"); \
	}) \

#define GET_CFG_AST_GROUP_T(typestr, type) ({ \
		ast_group_t *tmp; \
		if (port_cfg[port] && port_cfg[port]->type) \
			tmp = port_cfg[port]->type; \
		else \
			tmp = port_cfg[0]->type; \
		if (tmp) { \
			char tmpbuf[256]; \
			snprintf(buf, bufsize, "%s " #typestr ": %s", begin, ast_print_group(tmpbuf, sizeof(tmpbuf), *tmp)); \
		} else \
			snprintf(buf, bufsize, "%s " #typestr ": %s", begin, "none"); \
	}) \

void misdn_cfg_get_config_string(int port, enum misdn_cfg_elements elem, char* buf, int bufsize) {

	if (!(elem > MISDN_GEN_FIRST) && !misdn_cfg_is_port_valid(port)) {
		*buf = 0;
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get_config_string! Port number %d is not valid.\n", port);
		return;
	}
	
	char begin[] = " -> ";
	
	misdn_cfg_lock();
	
	switch (elem) {
		
		case MISDN_CFG_PTP:		snprintf(buf, bufsize, "%s PTP: %s", begin, ptp[port] ? "yes" : "no");
									break;
		case MISDN_CFG_GROUPNAME:	GET_CFG_STRING(GROUPNAME, name);
									break;
		case MISDN_CFG_RXGAIN:		GET_CFG_INT(RXGAIN, rxgain);
									break;
		case MISDN_CFG_TXGAIN:		GET_CFG_INT(TXGAIN, txgain);
									break;
		case MISDN_CFG_TE_CHOOSE_CHANNEL:
						GET_CFG_BOOL(TE_CHOOSE_CHANNEL, te_choose_channel, yes, no);
									break;
		case MISDN_CFG_CONTEXT:		GET_CFG_STRING(CONTEXT, context);
									break;
		case MISDN_CFG_LANGUAGE:	GET_CFG_STRING(LANGUAGE, language);
									break;
		case MISDN_CFG_CALLERID:	GET_CFG_STRING(CALLERID, callerid);
									break;
		case MISDN_CFG_METHOD:		GET_CFG_STRING(METHOD, method);
									break;
		case MISDN_CFG_DIALPLAN:	GET_CFG_INT(DIALPLAN, dialplan);
									break;
		case MISDN_CFG_NATPREFIX:	GET_CFG_STRING(NATIONALPREFIX, nationalprefix);
									break;
		case MISDN_CFG_INTERNATPREFIX:
						GET_CFG_STRING(INTERNATIONALPREFIX, internationalprefix);
									break;
		case MISDN_CFG_PRES:		GET_CFG_BOOL(PRESENTATION, pres, allowed, not_screened);
									break;
		case MISDN_CFG_ALWAYS_IMMEDIATE:
						GET_CFG_BOOL(ALWAYS_IMMEDIATE, always_immediate, yes, no);
									break;
		case MISDN_CFG_IMMEDIATE:	GET_CFG_BOOL(IMMEDIATE, immediate, yes, no);
									break;
		case MISDN_CFG_HOLD_ALLOWED:
						GET_CFG_BOOL(HOLD_ALLOWED, hold_allowed, yes, no);
									break;
		case MISDN_CFG_EARLY_BCONNECT:
						GET_CFG_BOOL(EARLY_BCONNECT, early_bconnect, yes, no);
									break;
		case MISDN_CFG_USE_CALLINGPRES:
						GET_CFG_BOOL(USE_CALLINGPRES, use_callingpres, yes, no);
									break;
		case MISDN_CFG_ECHOCANCEL:	GET_CFG_HYBRID(ECHOCANCEL, echocancel, yes, no);
									break;
		case MISDN_CFG_ECHOCANCELWHENBRIDGED:
						GET_CFG_BOOL(ECHOCANCELWHENBRIDGED, echocancelwhenbridged, yes, no);
									break;
		case MISDN_CFG_ECHOTRAINING:
						GET_CFG_HYBRID(ECHOTRAINING, echotraining, yes, no);
									break;
		case MISDN_CFG_CALLGROUP:	GET_CFG_AST_GROUP_T(CALLINGGROUP, callgroup);
									break;
		case MISDN_CFG_PICKUPGROUP:	GET_CFG_AST_GROUP_T(PICKUPGROUP, pickupgroup);
									break;
		case MISDN_CFG_MSNS:		{
							char tmpbuffer[BUFFERSIZE];
							tmpbuffer[0] = 0;
							struct msn_list *iter;
							if (port_cfg[port]->msn_list)
								iter = port_cfg[port]->msn_list;
							else
								iter = port_cfg[0]->msn_list;
							if (iter) {
								for (; iter; iter = iter->next)
									sprintf(tmpbuffer, "%s%s, ", tmpbuffer, iter->msn);
								tmpbuffer[strlen(tmpbuffer)-2] = 0;
							}
							snprintf(buf, bufsize, "%s MSNs: %s", begin, *tmpbuffer ? tmpbuffer : "none"); \
						}
						break;
		
		/* general config elements */
		
		case MISDN_GEN_DEBUG:		GET_GEN_INT(DEBUG_LEVEL, debug);
									break;
		case MISDN_GEN_TRACEFILE:	GET_GEN_STRING(TRACEFILE, tracefile);
									break;
		case MISDN_GEN_TRACE_CALLS: GET_GEN_BOOL(TRACE_CALLS, trace_calls, true, false);
									break;
		case MISDN_GEN_TRACE_DIR:	GET_GEN_STRING(TRACE_DIR, trace_dir);
									break;
		case MISDN_GEN_BRIDGING:	GET_GEN_BOOL(BRIDGING, bridging, yes, no);
									break;
		case MISDN_GEN_STOP_TONE:	GET_GEN_BOOL(STOP_TONE_AFTER_FIRST_DIGIT, stop_tone_after_first_digit, yes, no);
									break;
		case MISDN_GEN_APPEND_DIGITS2EXTEN: 
						GET_GEN_BOOL(APPEND_DIGITS2EXTEN, append_digits2exten, yes, no);
									break;
		case MISDN_GEN_L1_INFO_OK:	GET_GEN_BOOL(L1_INFO_OK, l1_info_ok, yes, no);
									break;
		case MISDN_GEN_CLEAR_L3:	GET_GEN_BOOL(CLEAR_L3, clear_l3, yes, no);
									break;
		case MISDN_GEN_DYNAMIC_CRYPT:
						GET_GEN_BOOL(DYNAMIC_CRYPT,dynamic_crypt, yes, no);
									break;
		case MISDN_GEN_CRYPT_PREFIX:
						GET_GEN_STRING(CRYPT_PREFIX, crypt_prefix);
									break;
		case MISDN_GEN_CRYPT_KEYS:	GET_GEN_STRING(CRYPT_KEYS, crypt_keys);
									break;
									
		default:			*buf = 0;
									break;
	}

	misdn_cfg_unlock();
}

int misdn_cfg_get_next_port (int port) {
	
	misdn_cfg_lock();
	
	for (port++; port <= max_ports; port++) {
		if (port_cfg[port]) {
			misdn_cfg_unlock();
			return port;
		}
	}
	
	misdn_cfg_unlock();
	
	return -1;
}

int misdn_cfg_get_next_port_spin (int port) {

	int ret = misdn_cfg_get_next_port(port);

	if (ret > 0)
		return ret;

	return misdn_cfg_get_next_port(0);
}

#define PARSE_GEN_INT(item) ({ \
		if (!strcasecmp(v->name, #item)) { \
			int temp; \
			if (!sscanf(v->value, "%d", &temp)) { \
				ast_log(LOG_WARNING, "Value \"%s\" for \"" #item "\" (generals section) invalid or out of range! Please edit your misdn.conf and then do a \"misdn reload\".\n", v->value); \
			} else { \
				general_cfg->item = (int *)malloc(sizeof(int)); \
				memcpy(general_cfg->item, &temp, sizeof(int)); \
			} \
			continue; \
		} \
	}) \

#define PARSE_GEN_BOOL(item) ({ \
		if (!strcasecmp(v->name, #item)) { \
			general_cfg->item = (int *)malloc(sizeof(int)); \
			*(general_cfg->item) = ast_true(v->value)?1:0; \
			continue; \
		} \
	})

#define PARSE_GEN_STR(item) ({ \
		if (!strcasecmp(v->name, #item)) { \
			int l = strlen(v->value); \
			if (l) { \
				general_cfg->item = (char *)calloc(l+1, sizeof(char)); \
				strncpy(general_cfg->item,v->value, l); \
			} \
			continue; \
		} \
	})

static void build_general_config(struct ast_variable *v) {

	if (!v) 
		return;

	for (; v; v = v->next) {

		PARSE_GEN_INT(debug);
		PARSE_GEN_STR(tracefile);
		PARSE_GEN_BOOL(trace_calls);
		PARSE_GEN_STR(trace_dir);
		PARSE_GEN_BOOL(bridging);
		PARSE_GEN_BOOL(stop_tone_after_first_digit);
		PARSE_GEN_BOOL(append_digits2exten);
		PARSE_GEN_BOOL(l1_info_ok);
		PARSE_GEN_BOOL(clear_l3);
		PARSE_GEN_BOOL(dynamic_crypt);
		PARSE_GEN_STR(crypt_prefix);
		PARSE_GEN_STR(crypt_keys);
		
	}
}

#define PARSE_CFG_HYBRID(item, def) ({ \
		if (!strcasecmp(v->name, #item)) { \
			new->item = (int *)malloc(sizeof(int)); \
			if (!sscanf(v->value, "%d", new->item)) { \
				if (ast_true(v->value)) \
					*new->item = def; \
				else \
					*new->item = 0; \
			} \
			continue; \
		} \
	}) \

#define PARSE_CFG_INT(item) ({ \
		if (!strcasecmp(v->name, #item)) { \
			new->item = (int *)malloc(sizeof(int)); \
			if (!sscanf(v->value, "%d", new->item)) { \
				ast_log(LOG_WARNING, "Value \"%s\" for \"" #item "\" of group \"%s\" invalid or out of range! Please edit your misdn.conf and then do a \"misdn reload\".\n", v->value, cat); \
				free(new->item); \
				new->item = NULL; \
			} \
			continue; \
		} \
	}) \

#define PARSE_CFG_BOOL(item) ({ \
		if (!strcasecmp(v->name, #item)) { \
			new->item = (int *)malloc(sizeof(int)); \
			*(new->item) = ast_true(v->value)?1:0; \
			continue; \
		} \
	})

#define PARSE_CFG_STR(item) ({ \
		if (!strcasecmp(v->name, #item)) { \
			int l = strlen(v->value); \
			if (l) { \
				new->item = (char *)calloc(l+1, sizeof(char)); \
				strncpy(new->item,v->value,l); \
			} \
			continue; \
		} \
	})

static void build_port_config(struct ast_variable *v, char *cat) {
	if (!v || !cat)
		return;

	int cfg_for_ports[max_ports + 1];
	int i = 0;
	for (; i < (max_ports + 1); i++) {
		cfg_for_ports[i] = 0;
	}
	
	/* we store the default config at position 0 */
	if (!strcasecmp(cat, "default")) {
		cfg_for_ports[0] = 1;
	}

	struct port_config* new = (struct port_config *)calloc(1, sizeof(struct port_config));
	
	{
		int l = strlen(cat);
		new->name = (char *)calloc(l+1, sizeof(char));
		strncpy(new->name, cat, l);
	}
	
	for (; v; v=v->next) {
		if (!strcasecmp(v->name, "ports")) {
			/* TODO check for value not beeing set, like PORTS= */
			char *iter;
			char *value = v->value;
			while ((iter = strchr(value, ',')) != NULL) {
				*iter = 0;
				/* strip spaces */
				while (*value && *value == ' ') {
					value++;
				}
				/* TODO check for char not 0-9 */

				if (*value){
					int p = atoi(value);
					if (p <= max_ports && p > 0) {
						cfg_for_ports[p] = 1;
						if (strstr(value, "ptp"))
							ptp[p] = 1;
					} else
						ast_log(LOG_WARNING, "Port value \"%s\" of group %s invalid or out of range! Please edit your misdn.conf and then do a \"misdn reload\".\n", value, cat);
					value = ++iter;
				}
			}
			/* the remaining or the only one */
			/* strip spaces */
			while (*value && *value == ' ') {
				value++;
			}
			/* TODO check for char not 0-9 */
			if (*value) {
				int p = atoi(value);
				if (p <= max_ports && p > 0) {
					cfg_for_ports[p] = 1;
					if (strstr(value, "ptp"))
						ptp[p] = 1;
				} else
					ast_log(LOG_WARNING, "Port value \"%s\" of group %s invalid or out of range! Please edit your misdn.conf and then do a \"misdn reload\".\n", value, cat);
			}
			continue;
		}
		PARSE_CFG_STR(context);
		PARSE_CFG_INT(dialplan);
		PARSE_CFG_STR(nationalprefix);
		PARSE_CFG_STR(internationalprefix);
		PARSE_CFG_STR(language);
		if (!strcasecmp(v->name, "presentation")) {
			if (v->value && strlen(v->value)) {
				new->pres = (int *)malloc(sizeof(int));
				if (!strcasecmp(v->value, "allowed")) {
					*(new->pres) = 1;
				}
				/* TODO: i assume if it is not "allowed", it is "not_screened" */
				else {
					*(new->pres) = 0;
				}
			}
			continue;
		}
		PARSE_CFG_INT(rxgain);
		PARSE_CFG_INT(txgain);
		PARSE_CFG_BOOL(te_choose_channel);
		PARSE_CFG_BOOL(immediate);
		PARSE_CFG_BOOL(always_immediate);
		PARSE_CFG_BOOL(hold_allowed);
		PARSE_CFG_BOOL(early_bconnect);
		PARSE_CFG_BOOL(use_callingpres);
		PARSE_CFG_HYBRID(echocancel, DEF_ECHOCANCEL);
		PARSE_CFG_BOOL(echocancelwhenbridged);
		PARSE_CFG_HYBRID(echotraining, DEF_ECHOTRAINING);
		PARSE_CFG_STR(callerid);
		PARSE_CFG_STR(method);
		if (!strcasecmp(v->name, "msns")) {
			/* TODO check for value not beeing set, like msns= */
			char *iter;
			char *value = v->value;

			while ((iter = strchr(value, ',')) != NULL) {
				*iter = 0;
				/* strip spaces */
				while (*value && *value == ' ') {
					value++;
				}
				/* TODO check for char not 0-9 */
				if (*value){
					int l = strlen(value);
					if (l) {
						struct msn_list *ml = (struct msn_list *)calloc(1, sizeof(struct msn_list));
						ml->msn = (char *)calloc(l+1, sizeof(char));
						strncpy(ml->msn,value,l);
						ml->next = new->msn_list;
						new->msn_list = ml;
					}
					value = ++iter;
				}
			}
			/* the remaining or the only one */
			/* strip spaces */
			while (*value && *value == ' ') {
				value++;
			}
			/* TODO check for char not 0-9 */
			if (*value) {
				int l = strlen(value);
				if (l) {
					struct msn_list *ml = (struct msn_list *)calloc(1, sizeof(struct msn_list));
					ml->msn = (char *)calloc(l+1, sizeof(char));
					strncpy(ml->msn,value,l);
					ml->next = new->msn_list;
					new->msn_list = ml;
				}
			}
			continue;
		}
		if (!strcasecmp(v->name, "callgroup")) {
			new->callgroup = (ast_group_t *)malloc(sizeof(ast_group_t));
			*(new->callgroup)=ast_get_group(v->value);
			continue;
		}
		if (!strcasecmp(v->name, "pickupgroup")) {
			new->pickupgroup = (ast_group_t *)malloc(sizeof(ast_group_t));
			*(new->pickupgroup)=ast_get_group(v->value);
			continue;
		}
	}
	/* store the new config in our array of port configs */
	for (i = 0; i < (max_ports + 1); i++) {
		if (cfg_for_ports[i])
			port_cfg[i] = new;
	}
}


static void fill_defaults (void) {
	
	/* general defaults */
	if (!general_cfg->debug)
		general_cfg->debug = (int*)calloc(1, sizeof(int));
	if (!general_cfg->trace_calls)
		general_cfg->trace_calls = (int*)calloc(1, sizeof(int));
	if (!general_cfg->trace_dir) {
		general_cfg->trace_dir = (char *)malloc(10 * sizeof(char));
		sprintf(general_cfg->trace_dir, "/var/log/");
	}
	if (!general_cfg->bridging) {
		general_cfg->bridging = (int*)malloc(sizeof(int));
		*general_cfg->bridging = 1;
	}
	if (!general_cfg->stop_tone_after_first_digit) {
		general_cfg->stop_tone_after_first_digit = (int*)malloc(sizeof(int));
		*general_cfg->stop_tone_after_first_digit = 1;
	}
	if (!general_cfg->append_digits2exten) {
		general_cfg->append_digits2exten = (int*)malloc(sizeof(int));
		*general_cfg->append_digits2exten = 1;
	}
	if (!general_cfg->l1_info_ok) {
		general_cfg->l1_info_ok = (int*)malloc(sizeof(int));
		*general_cfg->l1_info_ok = 1;
	}
	if (!general_cfg->clear_l3)
		general_cfg->clear_l3 =(int*)calloc(1, sizeof(int));
	if (!general_cfg->dynamic_crypt)
		general_cfg->dynamic_crypt = (int*)calloc(1, sizeof(int));

	/* defaults for default port config */
	if (!port_cfg[0])
		port_cfg[0] = (struct port_config*)calloc(1, sizeof(struct port_config));
	if (!port_cfg[0]->name) {
		port_cfg[0]->name = (char *)malloc(8 * sizeof(char));
		sprintf(port_cfg[0]->name, "default");
	}
	if (!port_cfg[0]->rxgain)
		port_cfg[0]->rxgain = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->txgain)
		port_cfg[0]->txgain = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->te_choose_channel)
		port_cfg[0]->te_choose_channel = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->context) {
		port_cfg[0]->context = (char *)malloc(8 * sizeof(char));
		sprintf(port_cfg[0]->context, "default");
	}
	if (!port_cfg[0]->language) {
		port_cfg[0]->language = (char *)malloc(3 * sizeof(char));
		sprintf(port_cfg[0]->language, "en");
	}
	if (!port_cfg[0]->callerid)
		port_cfg[0]->callerid = (char *)calloc(1, sizeof(char));
	if (!port_cfg[0]->method) {
		port_cfg[0]->method = (char *)malloc(9 * sizeof(char));
		sprintf(port_cfg[0]->method, "standard");
	}
	if (!port_cfg[0]->dialplan)
		port_cfg[0]->dialplan = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->nationalprefix) {
		port_cfg[0]->nationalprefix = (char *)malloc(2 * sizeof(char));
		sprintf(port_cfg[0]->nationalprefix, "0");
	}
	if (!port_cfg[0]->internationalprefix) {
		port_cfg[0]->internationalprefix = (char *)malloc(3 * sizeof(char));
		sprintf(port_cfg[0]->internationalprefix, "00");
	}
	if (!port_cfg[0]->pres) {
		port_cfg[0]->pres = (int *)malloc(sizeof(int));
		*port_cfg[0]->pres = 1;
	}
	if (!port_cfg[0]->always_immediate)
		port_cfg[0]->always_immediate = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->immediate)
		port_cfg[0]->immediate = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->hold_allowed)
		port_cfg[0]->hold_allowed = (int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->early_bconnect) {
		port_cfg[0]->early_bconnect = (int *)malloc(sizeof(int));
		*port_cfg[0]->early_bconnect = 1;
	}
	if (!port_cfg[0]->echocancel)
		port_cfg[0]->echocancel=(int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->echocancelwhenbridged)
		port_cfg[0]->echocancelwhenbridged=(int *)calloc(1, sizeof(int));
	if (!port_cfg[0]->echotraining) {
		port_cfg[0]->echotraining=(int *)malloc(sizeof(int));
		*port_cfg[0]->echotraining = 1;
	}
	if (!port_cfg[0]->use_callingpres) {
		port_cfg[0]->use_callingpres = (int *)malloc(sizeof(int));
		*port_cfg[0]->use_callingpres = 1;
	}
	if (!port_cfg[0]->msn_list) {
		port_cfg[0]->msn_list = (struct msn_list *)malloc(sizeof(struct msn_list));
		port_cfg[0]->msn_list->next = NULL;
		port_cfg[0]->msn_list->msn = (char *)calloc(2, sizeof(char));
		*(port_cfg[0]->msn_list->msn) = '*';
	}
}

void misdn_cfg_reload (void) {
	misdn_cfg_init (0);
}

void misdn_cfg_destroy (void) {

	misdn_cfg_lock();
	
	free_port_cfg();
	free_general_cfg();
	
	free(port_cfg);
	free(general_cfg);
	free(ptp);

	misdn_cfg_unlock();
	ast_mutex_destroy(&config_mutex);
}

void misdn_cfg_init (int this_max_ports)
{
	char config[]="misdn.conf";
	
	struct ast_config *cfg;
	cfg = AST_LOAD_CFG(config);
	if (!cfg) {
		ast_log(LOG_WARNING,"no misdn.conf ?\n");
		return;
	}

	misdn_cfg_lock();
	
	if (this_max_ports) {
		/* this is the first run */
		max_ports = this_max_ports;
		port_cfg = (struct port_config **)calloc(max_ports + 1, sizeof(struct port_config *));
		general_cfg = (struct general_config*)calloc(1, sizeof(struct general_config));
		ptp = (int *)calloc(max_ports + 1, sizeof(int));
	}
	else {
		free_port_cfg();
		free_general_cfg();
		port_cfg = memset(port_cfg, 0, sizeof(struct port_config *) * (max_ports + 1));
		general_cfg = memset(general_cfg, 0, sizeof(struct general_config));
		ptp = memset(ptp, 0, sizeof(int) * (max_ports + 1));
	}
	
	char *cat;
	cat = ast_category_browse(cfg, NULL);

	while(cat) {
		struct ast_variable *v=ast_variable_browse(cfg,cat);
		if (!strcasecmp(cat,"general")) {
			build_general_config (v);
		} else {
			build_port_config (v, cat);
		}
		cat=ast_category_browse(cfg,cat);
	}

	fill_defaults();
	
	misdn_cfg_unlock();
	
	AST_DESTROY_CFG(cfg);
}
