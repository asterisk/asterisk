/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/sorcery.h"
#include "include/res_pjsip_private.h"

#define TIMER_T1_MIN 100
#define DEFAULT_TIMER_T1 500
#define DEFAULT_TIMER_B 32000

struct system_config {
	SORCERY_OBJECT(details);
	/*! Transaction Timer T1 value */
	unsigned int timert1;
	/*! Transaction Timer B value */
	unsigned int timerb;
	/*! Should we use short forms for headers? */
	unsigned int compactheaders;
};

static struct ast_sorcery *system_sorcery;

static void *system_alloc(const char *name)
{
	struct system_config *system = ast_sorcery_generic_alloc(sizeof(*system), NULL);

	if (!system) {
		return NULL;
	}

	return system;
}

static int system_apply(const struct ast_sorcery *system_sorcery, void *obj)
{
	struct system_config *system = obj;
	int min_timerb;

	if (system->timert1 < TIMER_T1_MIN) {
		ast_log(LOG_WARNING, "Timer T1 setting is too low. Setting to %d\n", TIMER_T1_MIN);
		system->timert1 = TIMER_T1_MIN;
	}

	min_timerb = 64 * system->timert1;

	if (system->timerb < min_timerb) {
		ast_log(LOG_WARNING, "Timer B setting is too low. Setting to %d\n", min_timerb);
		system->timerb = min_timerb;
	}

	pjsip_cfg()->tsx.t1 = system->timert1;
	pjsip_cfg()->tsx.td = system->timerb;

	if (system->compactheaders) {
		extern pj_bool_t pjsip_use_compact_form;
		pjsip_use_compact_form = PJ_TRUE;
	}

	return 0;
}

int ast_sip_initialize_system(void)
{
	system_sorcery = ast_sorcery_open();
	if (!system_sorcery) {
		ast_log(LOG_ERROR, "Failed to open SIP system sorcery\n");
		return -1;
	}

	ast_sorcery_apply_config(system_sorcery, "res_pjsip");

	ast_sorcery_apply_default(system_sorcery, "system", "config", "pjsip.conf,criteria=type=system");

	if (ast_sorcery_object_register(system_sorcery, "system", system_alloc, NULL, system_apply)) {
		ast_sorcery_unref(system_sorcery);
		system_sorcery = NULL;
		return -1;
	}

	ast_sorcery_object_field_register(system_sorcery, "system", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(system_sorcery, "system", "timert1", __stringify(DEFAULT_TIMER_T1),
			OPT_UINT_T, 0, FLDSET(struct system_config, timert1));
	ast_sorcery_object_field_register(system_sorcery, "system", "timerb", __stringify(DEFAULT_TIMER_B),
			OPT_UINT_T, 0, FLDSET(struct system_config, timerb));
	ast_sorcery_object_field_register(system_sorcery, "system", "compactheaders", "no",
			OPT_BOOL_T, 1, FLDSET(struct system_config, compactheaders));

	ast_sorcery_load(system_sorcery);

	return 0;
}
