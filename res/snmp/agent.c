/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief SNMP Agent / SubAgent support for Asterisk
 *
 * \author Thorsten Lockert <tholo@voop.as>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/* Needed for net-snmp headers */
#define ASTMM_LIBC ASTMM_IGNORE
#include "asterisk.h"

/*
 * There is some collision collision between netsmp and asterisk names,
 * causing build under AST_DEVMODE to fail.
 *
 * The following PACKAGE_* macros are one place.
 * Also netsnmp has an improper check for HAVE_DMALLOC_H, using
 *    #if HAVE_DMALLOC_H   instead of #ifdef HAVE_DMALLOC_H
 * As a countermeasure we define it to 0, however this will fail
 * when the proper check is implemented.
 */
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#ifndef HAVE_DMALLOC_H
#define HAVE_DMALLOC_H 0	/* XXX we shouldn't do this */
#endif

#if defined(__OpenBSD__)
/*
 * OpenBSD uses old "legacy" cc which has a rather pedantic builtin preprocessor.
 * Using a macro which is not #defined throws an error.
 */
#define __NetBSD_Version__ 0
#endif

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#if !defined(RONLY) && defined(NETSNMP_OLDAPI_RONLY)
#define RONLY NETSNMP_OLDAPI_RONLY
#endif

#include "asterisk/paths.h"	/* need ast_config_AST_SOCKET */
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/indications.h"
#include "asterisk/ast_version.h"
#include "asterisk/pbx.h"

/* Colission between Net-SNMP and Asterisk */
#define unload_module ast_unload_module
#include "asterisk/module.h"
#undef unload_module

#include "agent.h"

/* Helper functions in Net-SNMP, header file not installed by default */
int header_generic(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **);
int header_simple_table(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **, int);
int register_sysORTable(oid *, size_t, const char *);
int unregister_sysORTable(oid *, size_t);

/* Forward declaration */
static void init_asterisk_mib(void);

/*
 * Anchor for all the Asterisk MIB values
 */
static oid asterisk_oid[] = { 1, 3, 6, 1, 4, 1, 22736, 1 };

/*
 * MIB values -- these correspond to values in the Asterisk MIB,
 * and MUST be kept in sync with the MIB for things to work as
 * expected.
 */
#define ASTVERSION				1
#define		ASTVERSTRING			1
#define		ASTVERTAG				2

#define	ASTCONFIGURATION		2
#define		ASTCONFUPTIME			1
#define		ASTCONFRELOADTIME		2
#define		ASTCONFPID				3
#define		ASTCONFSOCKET			4
#define		ASTCONFACTIVECALLS	5
#define		ASTCONFPROCESSEDCALLS   6

#define	ASTMODULES				3
#define		ASTMODCOUNT				1

#define	ASTINDICATIONS			4
#define		ASTINDCOUNT				1
#define		ASTINDCURRENT			2

#define		ASTINDTABLE				3
#define			ASTINDINDEX				1
#define			ASTINDCOUNTRY			2
#define			ASTINDALIAS				3
#define			ASTINDDESCRIPTION		4

#define	ASTCHANNELS				5
#define		ASTCHANCOUNT			1

#define		ASTCHANTABLE			2
#define			ASTCHANINDEX			1
#define			ASTCHANNAME				2
#define			ASTCHANLANGUAGE			3
#define			ASTCHANTYPE				4
#define			ASTCHANMUSICCLASS		5
#define			ASTCHANBRIDGE			6
#define			ASTCHANMASQ				7
#define			ASTCHANMASQR			8
#define			ASTCHANWHENHANGUP		9
#define			ASTCHANAPP				10
#define			ASTCHANDATA				11
#define			ASTCHANCONTEXT			12
#define			ASTCHANMACROCONTEXT		13
#define			ASTCHANMACROEXTEN		14
#define			ASTCHANMACROPRI			15
#define			ASTCHANEXTEN			16
#define			ASTCHANPRI				17
#define			ASTCHANACCOUNTCODE		18
#define			ASTCHANFORWARDTO		19
#define			ASTCHANUNIQUEID			20
#define			ASTCHANCALLGROUP		21
#define			ASTCHANPICKUPGROUP		22
#define			ASTCHANSTATE			23
#define			ASTCHANMUTED			24
#define			ASTCHANRINGS			25
#define			ASTCHANCIDDNID			26
#define			ASTCHANCIDNUM			27
#define			ASTCHANCIDNAME			28
#define			ASTCHANCIDANI			29
#define			ASTCHANCIDRDNIS			30
#define			ASTCHANCIDPRES			31
#define			ASTCHANCIDANI2			32
#define			ASTCHANCIDTON			33
#define			ASTCHANCIDTNS			34
#define			ASTCHANAMAFLAGS			35
#define			ASTCHANADSI				36
#define			ASTCHANTONEZONE			37
#define			ASTCHANHANGUPCAUSE		38
#define			ASTCHANVARIABLES		39
#define			ASTCHANFLAGS			40
#define			ASTCHANTRANSFERCAP		41

#define		ASTCHANTYPECOUNT		3

#define		ASTCHANTYPETABLE		4
#define			ASTCHANTYPEINDEX		1
#define			ASTCHANTYPENAME			2
#define			ASTCHANTYPEDESC			3
#define			ASTCHANTYPEDEVSTATE		4
#define			ASTCHANTYPEINDICATIONS	5
#define			ASTCHANTYPETRANSFER		6
#define			ASTCHANTYPECHANNELS		7

#define		ASTCHANSCALARS			5
#define			ASTCHANBRIDGECOUNT		1

void *agent_thread(void *arg)
{
	ast_verb(2, "Starting %sAgent\n", res_snmp_agentx_subagent ? "Sub" : "");

	snmp_enable_stderrlog();

	if (res_snmp_agentx_subagent)
		netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
							   NETSNMP_DS_AGENT_ROLE,
							   1);

	init_agent("asterisk");

	init_asterisk_mib();

	init_snmp("asterisk");

	if (!res_snmp_agentx_subagent)
		init_master_agent();

	while (res_snmp_dont_stop)
		agent_check_and_process(1);

	snmp_shutdown("asterisk");

	ast_verb(2, "Terminating %sAgent\n", res_snmp_agentx_subagent ? "Sub" : "");

	return NULL;
}

static u_char *
ast_var_channels(struct variable *vp, oid *name, size_t *length,
				 int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;

	if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

	if (vp->magic != ASTCHANCOUNT)
		return NULL;

	long_ret = ast_active_channels();

	return (u_char *)&long_ret;
}

static u_char *ast_var_channels_table(struct variable *vp, oid *name, size_t *length,
									int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;
	static u_char bits_ret[2];
	static char string_ret[256];
	struct ast_channel *chan, *bridge;
	struct timeval tval;
	u_char *ret = NULL;
	int i, bit;
	struct ast_str *out = ast_str_alloca(2048);
	struct ast_channel_iterator *iter;

	if (header_simple_table(vp, name, length, exact, var_len, write_method, ast_active_channels()))
		return NULL;

	i = name[*length - 1] - 1;

	if (!(iter = ast_channel_iterator_all_new())) {
		return NULL;
	}

	while ((chan = ast_channel_iterator_next(iter)) && i) {
		ast_channel_unref(chan);
		i--;
	}

	iter = ast_channel_iterator_destroy(iter);

	if (chan == NULL) {
		return NULL;
	}

	*var_len = sizeof(long_ret);

	ast_channel_lock(chan);

	switch (vp->magic) {
	case ASTCHANINDEX:
		long_ret = name[*length - 1];
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANNAME:
		if (!ast_strlen_zero(ast_channel_name(chan))) {
			ast_copy_string(string_ret, ast_channel_name(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANLANGUAGE:
		if (!ast_strlen_zero(ast_channel_language(chan))) {
			ast_copy_string(string_ret, ast_channel_language(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANTYPE:
		ast_copy_string(string_ret, ast_channel_tech(chan)->type, sizeof(string_ret));
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMUSICCLASS:
		if (!ast_strlen_zero(ast_channel_musicclass(chan))) {
			ast_copy_string(string_ret, ast_channel_musicclass(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANBRIDGE:
		ast_channel_unlock(chan);
		bridge = ast_channel_bridge_peer(chan);
		if (bridge) {
			ast_channel_lock(bridge);
			ast_copy_string(string_ret, ast_channel_name(bridge), sizeof(string_ret));
			ast_channel_unlock(bridge);
			ast_channel_unref(bridge);

			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		ast_channel_lock(chan);
		break;
	case ASTCHANMASQ:
		if (ast_channel_masq(chan) && !ast_strlen_zero(ast_channel_name(ast_channel_masq(chan)))) {
			ast_copy_string(string_ret, ast_channel_name(ast_channel_masq(chan)), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANMASQR:
		if (ast_channel_masqr(chan) && !ast_strlen_zero(ast_channel_name(ast_channel_masqr(chan)))) {
			ast_copy_string(string_ret, ast_channel_name(ast_channel_masqr(chan)), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANWHENHANGUP:
		if (!ast_tvzero(*ast_channel_whentohangup(chan))) {
			gettimeofday(&tval, NULL);
			long_ret = difftime(ast_channel_whentohangup(chan)->tv_sec, tval.tv_sec) * 100 - tval.tv_usec / 10000;
			ret= (u_char *)&long_ret;
		}
		break;
	case ASTCHANAPP:
		if (ast_channel_appl(chan)) {
			ast_copy_string(string_ret, ast_channel_appl(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANDATA:
		if (ast_channel_data(chan)) {
			ast_copy_string(string_ret, ast_channel_data(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANCONTEXT:
		ast_copy_string(string_ret, ast_channel_context(chan), sizeof(string_ret));
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMACROCONTEXT:
		ast_copy_string(string_ret, ast_channel_macrocontext(chan), sizeof(string_ret));
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMACROEXTEN:
		ast_copy_string(string_ret, ast_channel_macroexten(chan), sizeof(string_ret));
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMACROPRI:
		long_ret = ast_channel_macropriority(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANEXTEN:
		ast_copy_string(string_ret, ast_channel_exten(chan), sizeof(string_ret));
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANPRI:
		long_ret = ast_channel_priority(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANACCOUNTCODE:
		if (!ast_strlen_zero(ast_channel_accountcode(chan))) {
			ast_copy_string(string_ret, ast_channel_accountcode(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANFORWARDTO:
		if (!ast_strlen_zero(ast_channel_call_forward(chan))) {
			ast_copy_string(string_ret, ast_channel_call_forward(chan), sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANUNIQUEID:
		ast_copy_string(string_ret, ast_channel_uniqueid(chan), sizeof(string_ret));
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANCALLGROUP:
		long_ret = ast_channel_callgroup(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANPICKUPGROUP:
		long_ret = ast_channel_pickupgroup(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANSTATE:
		long_ret = ast_channel_state(chan) & 0xffff;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANMUTED:
		long_ret = ast_channel_state(chan) & AST_STATE_MUTE ? 1 : 2;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANRINGS:
		long_ret = ast_channel_rings(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDDNID:
		if (ast_channel_dialed(chan)->number.str) {
			ast_copy_string(string_ret, ast_channel_dialed(chan)->number.str, sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANCIDNUM:
		if (ast_channel_caller(chan)->id.number.valid && ast_channel_caller(chan)->id.number.str) {
			ast_copy_string(string_ret, ast_channel_caller(chan)->id.number.str, sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANCIDNAME:
		if (ast_channel_caller(chan)->id.name.valid && ast_channel_caller(chan)->id.name.str) {
			ast_copy_string(string_ret, ast_channel_caller(chan)->id.name.str, sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANCIDANI:
		if (ast_channel_caller(chan)->ani.number.valid && ast_channel_caller(chan)->ani.number.str) {
			ast_copy_string(string_ret, ast_channel_caller(chan)->ani.number.str, sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANCIDRDNIS:
		if (ast_channel_redirecting(chan)->from.number.valid && ast_channel_redirecting(chan)->from.number.str) {
			ast_copy_string(string_ret, ast_channel_redirecting(chan)->from.number.str, sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANCIDPRES:
		long_ret = ast_party_id_presentation(&ast_channel_caller(chan)->id);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDANI2:
		long_ret = ast_channel_caller(chan)->ani2;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDTON:
		long_ret = ast_channel_caller(chan)->id.number.plan;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDTNS:
		long_ret = ast_channel_dialed(chan)->transit_network_select;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANAMAFLAGS:
		long_ret = ast_channel_amaflags(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANADSI:
		long_ret = ast_channel_adsicpe(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANTONEZONE:
		if (ast_channel_zone(chan)) {
			ast_copy_string(string_ret, ast_channel_zone(chan)->country, sizeof(string_ret));
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		break;
	case ASTCHANHANGUPCAUSE:
		long_ret = ast_channel_hangupcause(chan);
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANVARIABLES:
		if (pbx_builtin_serialize_variables(chan, &out)) {
			*var_len = ast_str_strlen(out);
			ret = (u_char *)ast_str_buffer(out);
		}
		break;
	case ASTCHANFLAGS:
		bits_ret[0] = 0;
		for (bit = 0; bit < 8; bit++)
			bits_ret[0] |= ((ast_channel_flags(chan)->flags & (1 << bit)) >> bit) << (7 - bit);
		bits_ret[1] = 0;
		for (bit = 0; bit < 8; bit++)
			bits_ret[1] |= (((ast_channel_flags(chan)->flags >> 8) & (1 << bit)) >> bit) << (7 - bit);
		*var_len = 2;
		ret = bits_ret;
		break;
	case ASTCHANTRANSFERCAP:
		long_ret = ast_channel_transfercapability(chan);
		ret = (u_char *)&long_ret;
	default:
		break;
	}

	ast_channel_unlock(chan);
	chan = ast_channel_unref(chan);

	return ret;
}

static u_char *ast_var_channel_types(struct variable *vp, oid *name, size_t *length,
								   int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;
	struct ast_variable *channel_types, *next;

	if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

	if (vp->magic != ASTCHANTYPECOUNT)
		return NULL;

	for (long_ret = 0, channel_types = next = ast_channeltype_list(); next; next = next->next)
		long_ret++;
	ast_variables_destroy(channel_types);

	return (u_char *)&long_ret;
}

static u_char *ast_var_channel_types_table(struct variable *vp, oid *name, size_t *length,
										int exact, size_t *var_len, WriteMethod **write_method)
{
	const struct ast_channel_tech *tech = NULL;
	struct ast_variable *channel_types, *next;
	static unsigned long long_ret;
	struct ast_channel *chan;
	u_long i;

	if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
		return NULL;

	channel_types = ast_channeltype_list();
	for (i = 1, next = channel_types; next && i != name[*length - 1]; next = next->next, i++)
		;
	if (next != NULL)
		tech = ast_get_channel_tech(next->name);
	ast_variables_destroy(channel_types);
	if (next == NULL || tech == NULL)
		return NULL;

	switch (vp->magic) {
	case ASTCHANTYPEINDEX:
		long_ret = name[*length - 1];
		return (u_char *)&long_ret;
	case ASTCHANTYPENAME:
		*var_len = strlen(tech->type);
		return (u_char *)tech->type;
	case ASTCHANTYPEDESC:
		*var_len = strlen(tech->description);
		return (u_char *)tech->description;
	case ASTCHANTYPEDEVSTATE:
		long_ret = tech->devicestate ? 1 : 2;
		return (u_char *)&long_ret;
	case ASTCHANTYPEINDICATIONS:
		long_ret = tech->indicate ? 1 : 2;
		return (u_char *)&long_ret;
	case ASTCHANTYPETRANSFER:
		long_ret = tech->transfer ? 1 : 2;
		return (u_char *)&long_ret;
	case ASTCHANTYPECHANNELS:
	{
		struct ast_channel_iterator *iter;

		long_ret = 0;

		if (!(iter = ast_channel_iterator_all_new())) {
			return NULL;
		}

		while ((chan = ast_channel_iterator_next(iter))) {
			if (ast_channel_tech(chan) == tech) {
				long_ret++;
			}
			chan = ast_channel_unref(chan);
		}

		ast_channel_iterator_destroy(iter);

		return (u_char *)&long_ret;
	}
	default:
		break;
	}
	return NULL;
}

static u_char *ast_var_channel_bridge(struct variable *vp, oid *name, size_t *length,
	int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;
	struct ast_channel *chan = NULL;
	struct ast_channel_iterator *iter;

	long_ret = 0;

	if (header_generic(vp, name, length, exact, var_len, write_method)) {
		return NULL;
	}

	if (!(iter = ast_channel_iterator_all_new())) {
		return NULL;
	}

	while ((chan = ast_channel_iterator_next(iter))) {
		ast_channel_lock(chan);
		if (ast_channel_is_bridged(chan)) {
			long_ret++;
		}
		ast_channel_unlock(chan);
		chan = ast_channel_unref(chan);
	}

	ast_channel_iterator_destroy(iter);

	*var_len = sizeof(long_ret);

	return (vp->magic == ASTCHANBRIDGECOUNT) ? (u_char *) &long_ret : NULL;
}

static u_char *ast_var_Config(struct variable *vp, oid *name, size_t *length,
							 int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;
	struct timeval tval;

	if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

	switch (vp->magic) {
	case ASTCONFUPTIME:
		gettimeofday(&tval, NULL);
		long_ret = difftime(tval.tv_sec, ast_startuptime.tv_sec) * 100 + tval.tv_usec / 10000 - ast_startuptime.tv_usec / 10000;
		return (u_char *)&long_ret;
	case ASTCONFRELOADTIME:
		gettimeofday(&tval, NULL);
		if (ast_lastreloadtime.tv_sec)
			long_ret = difftime(tval.tv_sec, ast_lastreloadtime.tv_sec) * 100 + tval.tv_usec / 10000 - ast_lastreloadtime.tv_usec / 10000;
		else
			long_ret = difftime(tval.tv_sec, ast_startuptime.tv_sec) * 100 + tval.tv_usec / 10000 - ast_startuptime.tv_usec / 10000;
		return (u_char *)&long_ret;
	case ASTCONFPID:
		long_ret = getpid();
		return (u_char *)&long_ret;
	case ASTCONFSOCKET:
		*var_len = strlen(ast_config_AST_SOCKET);
		return (u_char *)ast_config_AST_SOCKET;
	case ASTCONFACTIVECALLS:
		long_ret = ast_active_calls();
		return (u_char *)&long_ret;
	case ASTCONFPROCESSEDCALLS:
		long_ret = ast_processed_calls();
		return (u_char *)&long_ret;
	default:
		break;
	}
	return NULL;
}

static u_char *ast_var_indications(struct variable *vp, oid *name, size_t *length,
								  int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;
	static char ret_buf[128];
	struct ast_tone_zone *tz = NULL;

	if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

	switch (vp->magic) {
	case ASTINDCOUNT:
	{
		struct ao2_iterator i;

		long_ret = 0;

		i = ast_tone_zone_iterator_init();
		while ((tz = ao2_iterator_next(&i))) {
			tz = ast_tone_zone_unref(tz);
			long_ret++;
		}
		ao2_iterator_destroy(&i);

		return (u_char *) &long_ret;
	}
	case ASTINDCURRENT:
		tz = ast_get_indication_zone(NULL);
		if (tz) {
			ast_copy_string(ret_buf, tz->country, sizeof(ret_buf));
			*var_len = strlen(ret_buf);
			tz = ast_tone_zone_unref(tz);
			return (u_char *) ret_buf;
		}
		*var_len = 0;
		return NULL;
	default:
		break;
	}
	return NULL;
}

static u_char *ast_var_indications_table(struct variable *vp, oid *name, size_t *length,
									   int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;
	static char ret_buf[256];
	struct ast_tone_zone *tz = NULL;
	int i;
	struct ao2_iterator iter;

	if (header_simple_table(vp, name, length, exact, var_len, write_method, -1)) {
		return NULL;
	}

	i = name[*length - 1] - 1;

	iter = ast_tone_zone_iterator_init();

	while ((tz = ao2_iterator_next(&iter)) && i) {
		tz = ast_tone_zone_unref(tz);
		i--;
	}
	ao2_iterator_destroy(&iter);

	if (tz == NULL) {
		return NULL;
	}

	switch (vp->magic) {
	case ASTINDINDEX:
		ast_tone_zone_unref(tz);
		long_ret = name[*length - 1];
		return (u_char *)&long_ret;
	case ASTINDCOUNTRY:
		ast_copy_string(ret_buf, tz->country, sizeof(ret_buf));
		ast_tone_zone_unref(tz);
		*var_len = strlen(ret_buf);
		return (u_char *) ret_buf;
	case ASTINDALIAS:
		/* No longer exists */
		ast_tone_zone_unref(tz);
		return NULL;
	case ASTINDDESCRIPTION:
		ast_tone_zone_lock(tz);
		ast_copy_string(ret_buf, tz->description, sizeof(ret_buf));
		ast_tone_zone_unlock(tz);
		ast_tone_zone_unref(tz);
		*var_len = strlen(ret_buf);
		return (u_char *) ret_buf;
	default:
		ast_tone_zone_unref(tz);
		break;
	}
	return NULL;
}

static int countmodule(const char *mod, const char *desc, int use, const char *status,
		const char *like, enum ast_module_support_level support_level)
{
	return 1;
}

static u_char *ast_var_Modules(struct variable *vp, oid *name, size_t *length,
							  int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;

	if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

	if (vp->magic != ASTMODCOUNT)
		return NULL;

	long_ret = ast_update_module_list(countmodule, NULL);

	return (u_char *)&long_ret;
}

static u_char *ast_var_Version(struct variable *vp, oid *name, size_t *length,
							  int exact, size_t *var_len, WriteMethod **write_method)
{
	static unsigned long long_ret;

	if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

	switch (vp->magic) {
	case ASTVERSTRING:
	{
		const char *version = ast_get_version();
		*var_len = strlen(version);
		return (u_char *)version;
	}
	case ASTVERTAG:
		sscanf(ast_get_version_num(), "%30lu", &long_ret);
		return (u_char *)&long_ret;
	default:
		break;
	}
	return NULL;
}

static int term_asterisk_mib(int majorID, int minorID, void *serverarg, void *clientarg)
{
	unregister_sysORTable(asterisk_oid, OID_LENGTH(asterisk_oid));
	return 0;
}

static void init_asterisk_mib(void)
{
	static struct variable4 asterisk_vars[] = {
		{ASTVERSTRING,           ASN_OCTET_STR, RONLY, ast_var_Version,             2, {ASTVERSION, ASTVERSTRING}},
		{ASTVERTAG,              ASN_UNSIGNED,  RONLY, ast_var_Version,             2, {ASTVERSION, ASTVERTAG}},
		{ASTCONFUPTIME,          ASN_TIMETICKS, RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFUPTIME}},
		{ASTCONFRELOADTIME,      ASN_TIMETICKS, RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFRELOADTIME}},
		{ASTCONFPID,             ASN_INTEGER,   RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFPID}},
		{ASTCONFSOCKET,          ASN_OCTET_STR, RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFSOCKET}},
		{ASTCONFACTIVECALLS,     ASN_GAUGE,   	RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFACTIVECALLS}},
		{ASTCONFPROCESSEDCALLS,  ASN_COUNTER,   RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFPROCESSEDCALLS}},
		{ASTMODCOUNT,            ASN_INTEGER,   RONLY, ast_var_Modules ,            2, {ASTMODULES, ASTMODCOUNT}},
		{ASTINDCOUNT,            ASN_INTEGER,   RONLY, ast_var_indications,         2, {ASTINDICATIONS, ASTINDCOUNT}},
		{ASTINDCURRENT,          ASN_OCTET_STR, RONLY, ast_var_indications,         2, {ASTINDICATIONS, ASTINDCURRENT}},
		{ASTINDINDEX,            ASN_INTEGER,   RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDINDEX}},
		{ASTINDCOUNTRY,          ASN_OCTET_STR, RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDCOUNTRY}},
		{ASTINDALIAS,            ASN_OCTET_STR, RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDALIAS}},
		{ASTINDDESCRIPTION,      ASN_OCTET_STR, RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDDESCRIPTION}},
		{ASTCHANCOUNT,           ASN_GAUGE,     RONLY, ast_var_channels,            2, {ASTCHANNELS, ASTCHANCOUNT}},
		{ASTCHANINDEX,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANINDEX}},
		{ASTCHANNAME,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANNAME}},
		{ASTCHANLANGUAGE,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANLANGUAGE}},
		{ASTCHANTYPE,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANTYPE}},
		{ASTCHANMUSICCLASS,      ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMUSICCLASS}},
		{ASTCHANBRIDGE,          ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANBRIDGE}},
		{ASTCHANMASQ,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMASQ}},
		{ASTCHANMASQR,           ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMASQR}},
		{ASTCHANWHENHANGUP,      ASN_TIMETICKS, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANWHENHANGUP}},
		{ASTCHANAPP,             ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANAPP}},
		{ASTCHANDATA,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANDATA}},
		{ASTCHANCONTEXT,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCONTEXT}},
		{ASTCHANMACROCONTEXT,    ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMACROCONTEXT}},
		{ASTCHANMACROEXTEN,      ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMACROEXTEN}},
		{ASTCHANMACROPRI,        ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMACROPRI}},
		{ASTCHANEXTEN,           ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANEXTEN}},
		{ASTCHANPRI,             ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANPRI}},
		{ASTCHANACCOUNTCODE,     ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANACCOUNTCODE}},
		{ASTCHANFORWARDTO,       ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANFORWARDTO}},
		{ASTCHANUNIQUEID,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANUNIQUEID}},
		{ASTCHANCALLGROUP,       ASN_UNSIGNED,  RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCALLGROUP}},
		{ASTCHANPICKUPGROUP,     ASN_UNSIGNED,  RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANPICKUPGROUP}},
		{ASTCHANSTATE,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANSTATE}},
		{ASTCHANMUTED,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMUTED}},
		{ASTCHANRINGS,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANRINGS}},
		{ASTCHANCIDDNID,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDDNID}},
		{ASTCHANCIDNUM,          ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDNUM}},
		{ASTCHANCIDNAME,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDNAME}},
		{ASTCHANCIDANI,          ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDANI}},
		{ASTCHANCIDRDNIS,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDRDNIS}},
		{ASTCHANCIDPRES,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDPRES}},
		{ASTCHANCIDANI2,         ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDANI2}},
		{ASTCHANCIDTON,          ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDTON}},
		{ASTCHANCIDTNS,          ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDTNS}},
		{ASTCHANAMAFLAGS,        ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANAMAFLAGS}},
		{ASTCHANADSI,            ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANADSI}},
		{ASTCHANTONEZONE,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANTONEZONE}},
		{ASTCHANHANGUPCAUSE,     ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANHANGUPCAUSE}},
		{ASTCHANVARIABLES,       ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANVARIABLES}},
		{ASTCHANFLAGS,           ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANFLAGS}},
		{ASTCHANTRANSFERCAP,     ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANTRANSFERCAP}},
		{ASTCHANTYPECOUNT,       ASN_INTEGER,   RONLY, ast_var_channel_types,       2, {ASTCHANNELS, ASTCHANTYPECOUNT}},
		{ASTCHANTYPEINDEX,       ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEINDEX}},
		{ASTCHANTYPENAME,        ASN_OCTET_STR, RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPENAME}},
		{ASTCHANTYPEDESC,        ASN_OCTET_STR, RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEDESC}},
		{ASTCHANTYPEDEVSTATE,    ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEDEVSTATE}},
		{ASTCHANTYPEINDICATIONS, ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEINDICATIONS}},
		{ASTCHANTYPETRANSFER,    ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPETRANSFER}},
		{ASTCHANTYPECHANNELS,    ASN_GAUGE,     RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPECHANNELS}},
		{ASTCHANBRIDGECOUNT,     ASN_GAUGE,     RONLY, ast_var_channel_bridge,      3, {ASTCHANNELS, ASTCHANSCALARS, ASTCHANBRIDGECOUNT}},
	};

	register_sysORTable(asterisk_oid, OID_LENGTH(asterisk_oid),
			"ASTERISK-MIB implementation for Asterisk.");

	REGISTER_MIB("res_snmp", asterisk_vars, variable4, asterisk_oid);

	snmp_register_callback(SNMP_CALLBACK_LIBRARY,
			   SNMP_CALLBACK_SHUTDOWN,
			   term_asterisk_mib, NULL);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * c-file-offsets: ((case-label . 0))
 * tab-width: 4
 * indent-tabs-mode: t
 * End:
 */
