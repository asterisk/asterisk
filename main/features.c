/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
 * Copyright (C) 2012, Russell Bryant
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

/*! \file
 *
 * \brief Routines implementing call features as call pickup, parking and transfer
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*! \li \ref features.c uses the configuration file \ref features.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page features.conf features.conf
 * \verbinclude features.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/utils.h"
#include "asterisk/adsi.h"
#include "asterisk/devicestate.h"
#include "asterisk/monitor.h"
#include "asterisk/audiohook.h"
#include "asterisk/global_datastores.h"
#include "asterisk/astobj2.h"
#include "asterisk/test.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_features.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/bridge_after.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/features_config.h"

/* BUGBUG TEST_FRAMEWORK is disabled because parking tests no longer work. */
#undef TEST_FRAMEWORK

/*
 * Party A - transferee
 * Party B - transferer
 * Party C - target of transfer
 *
 * DTMF attended transfer works within the channel bridge.
 * Unfortunately, when either party A or B in the channel bridge
 * hangs up, that channel is not completely hung up until the
 * transfer completes.  This is a real problem depending upon
 * the channel technology involved.
 *
 * For chan_dahdi, the channel is crippled until the hangup is
 * complete.  Either the channel is not useable (analog) or the
 * protocol disconnect messages are held up (PRI/BRI/SS7) and
 * the media is not released.
 *
 * For chan_sip, a call limit of one is going to block that
 * endpoint from any further calls until the hangup is complete.
 *
 * For party A this is a minor problem.  The party A channel
 * will only be in this condition while party B is dialing and
 * when party B and C are conferring.  The conversation between
 * party B and C is expected to be a short one.  Party B is
 * either asking a question of party C or announcing party A.
 * Also party A does not have much incentive to hangup at this
 * point.
 *
 * For party B this can be a major problem during a blonde
 * transfer.  (A blonde transfer is our term for an attended
 * transfer that is converted into a blind transfer. :))  Party
 * B could be the operator.  When party B hangs up, he assumes
 * that he is out of the original call entirely.  The party B
 * channel will be in this condition while party C is ringing,
 * while attempting to recall party B, and while waiting between
 * call attempts.
 *
 * WARNING:
 * The ATXFER_NULL_TECH conditional is a hack to fix the
 * problem.  It will replace the party B channel technology with
 * a NULL channel driver.  The consequences of this code is that
 * the 'h' extension will not be able to access any channel
 * technology specific information like SIP statistics for the
 * call.
 *
 * Uncomment the ATXFER_NULL_TECH define below to replace the
 * party B channel technology in the channel bridge to complete
 * hanging up the channel technology.
 */
//#define ATXFER_NULL_TECH	1

/*** DOCUMENTATION
	<application name="Bridge" language="en_US">
		<synopsis>
			Bridge two channels.
		</synopsis>
		<syntax>
			<parameter name="channel" required="true">
				<para>The current channel is bridged to the specified <replaceable>channel</replaceable>.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="p">
						<para>Play a courtesy tone to <replaceable>channel</replaceable>.</para>
					</option>
					<option name="F" argsep="^">
						<argument name="context" required="false" />
						<argument name="exten" required="false" />
						<argument name="priority" required="true" />
						<para>When the bridger hangs up, transfer the <emphasis>bridged</emphasis> party
						to the specified destination and <emphasis>start</emphasis> execution at that location.</para>
						<note>
							<para>Any channel variables you want the called channel to inherit from the caller channel must be
							prefixed with one or two underbars ('_').</para>
						</note>
						<note>
							<para>This option will override the 'x' option</para>
						</note>
					</option>
					<option name="F">
						<para>When the bridger hangs up, transfer the <emphasis>bridged</emphasis> party
						to the next priority of	the current extension and <emphasis>start</emphasis> execution
						at that location.</para>
						<note>
							<para>Any channel variables you want the called channel to inherit from the caller channel must be
							prefixed with one or two underbars ('_').</para>
						</note>
						<note>
							<para>Using this option from a Macro() or GoSub() might not make sense as there would be no return points.</para>
						</note>
						<note>
							<para>This option will override the 'x' option</para>
						</note>
					</option>

					<option name="h">
						<para>Allow the called party to hang up by sending the
						<replaceable>*</replaceable> DTMF digit.</para>
					</option>
					<option name="H">
						<para>Allow the calling party to hang up by pressing the
						<replaceable>*</replaceable> DTMF digit.</para>
					</option>
					<option name="k">
						<para>Allow the called party to enable parking of the call by sending
						the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
					</option>
					<option name="K">
						<para>Allow the calling party to enable parking of the call by sending
						 the DTMF sequence defined for call parking in <filename>features.conf</filename>.</para>
					</option>
					<option name="L(x[:y][:z])">
						<para>Limit the call to <replaceable>x</replaceable> ms. Play a warning
						when <replaceable>y</replaceable> ms are left. Repeat the warning every
						<replaceable>z</replaceable> ms. The following special variables can be
						used with this option:</para>
						<variablelist>
							<variable name="LIMIT_PLAYAUDIO_CALLER">
								<para>Play sounds to the caller. yes|no (default yes)</para>
							</variable>
							<variable name="LIMIT_PLAYAUDIO_CALLEE">
								<para>Play sounds to the callee. yes|no</para>
							</variable>
							<variable name="LIMIT_TIMEOUT_FILE">
								<para>File to play when time is up.</para>
							</variable>
							<variable name="LIMIT_CONNECT_FILE">
								<para>File to play when call begins.</para>
							</variable>
							<variable name="LIMIT_WARNING_FILE">
								<para>File to play as warning if <replaceable>y</replaceable> is
								defined. The default is to say the time remaining.</para>
							</variable>
						</variablelist>
					</option>
					<option name="S(x)">
						<para>Hang up the call after <replaceable>x</replaceable> seconds *after* the called party has answered the call.</para>
					</option>
					<option name="t">
						<para>Allow the called party to transfer the calling party by sending the
						DTMF sequence defined in <filename>features.conf</filename>.</para>
					</option>
					<option name="T">
						<para>Allow the calling party to transfer the called party by sending the
						DTMF sequence defined in <filename>features.conf</filename>.</para>
					</option>
					<option name="w">
						<para>Allow the called party to enable recording of the call by sending
						the DTMF sequence defined for one-touch recording in <filename>features.conf</filename>.</para>
					</option>
					<option name="W">
						<para>Allow the calling party to enable recording of the call by sending
						the DTMF sequence defined for one-touch recording in <filename>features.conf</filename>.</para>
					</option>
					<option name="x">
						<para>Cause the called party to be hung up after the bridge, instead of being
						restarted in the dialplan.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Allows the ability to bridge two channels via the dialplan.</para>
			<para>This application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="BRIDGERESULT">
					<para>The result of the bridge attempt as a text string.</para>
					<value name="SUCCESS" />
					<value name="FAILURE" />
					<value name="LOOP" />
					<value name="NONEXISTENT" />
					<value name="INCOMPATIBLE" />
				</variable>
			</variablelist>
		</description>
	</application>
	<manager name="Bridge" language="en_US">
		<synopsis>
			Bridge two channels already in the PBX.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel1" required="true">
				<para>Channel to Bridge to Channel2.</para>
			</parameter>
			<parameter name="Channel2" required="true">
				<para>Channel to Bridge to Channel1.</para>
			</parameter>
			<parameter name="Tone">
				<para>Play courtesy tone to Channel 2.</para>
				<enumlist>
					<enum name="no" />
					<enum name="Channel1" />
					<enum name="Channel2" />
					<enum name="Both" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Bridge together two channels already in the PBX.</para>
		</description>
	</manager>
 ***/

/* TODO Scrape all of the parking stuff out of features.c */

typedef enum {
	FEATURE_INTERPRET_DETECT, /* Used by ast_feature_detect */
	FEATURE_INTERPRET_DO,     /* Used by feature_interpret */
	FEATURE_INTERPRET_CHECK,  /* Used by feature_check */
} feature_interpret_op;

struct ast_dial_features {
	/*! Channel's feature flags. */
	struct ast_flags my_features;
	/*! Bridge peer's feature flags. */
	struct ast_flags peer_features;
};

static void *dial_features_duplicate(void *data)
{
	struct ast_dial_features *df = data, *df_copy;

	if (!(df_copy = ast_calloc(1, sizeof(*df)))) {
		return NULL;
	}

	memcpy(df_copy, df, sizeof(*df));

	return df_copy;
}

static const struct ast_datastore_info dial_features_info = {
	.type = "dial-features",
	.destroy = ast_free_ptr,
	.duplicate = dial_features_duplicate,
};

/*!
 * \internal
 * \brief Set the features datastore if it doesn't exist.
 *
 * \param chan Channel to add features datastore
 * \param my_features The channel's feature flags
 * \param peer_features The channel's bridge peer feature flags
 *
 * \retval TRUE if features datastore already existed.
 */
static int add_features_datastore(struct ast_channel *chan, const struct ast_flags *my_features, const struct ast_flags *peer_features)
{
	struct ast_datastore *datastore;
	struct ast_dial_features *dialfeatures;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &dial_features_info, NULL);
	ast_channel_unlock(chan);
	if (datastore) {
		/* Already exists. */
		return 1;
	}

	/* Create a new datastore with specified feature flags. */
	datastore = ast_datastore_alloc(&dial_features_info, NULL);
	if (!datastore) {
		ast_log(LOG_WARNING, "Unable to create channel features datastore.\n");
		return 0;
	}
	dialfeatures = ast_calloc(1, sizeof(*dialfeatures));
	if (!dialfeatures) {
		ast_log(LOG_WARNING, "Unable to allocate memory for feature flags.\n");
		ast_datastore_free(datastore);
		return 0;
	}
	ast_copy_flags(&dialfeatures->my_features, my_features, AST_FLAGS_ALL);
	ast_copy_flags(&dialfeatures->peer_features, peer_features, AST_FLAGS_ALL);
	datastore->inheritance = DATASTORE_INHERIT_FOREVER;
	datastore->data = dialfeatures;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
	return 0;
}

struct ast_bridge_thread_obj
{
	struct ast_bridge_config bconfig;
	struct ast_channel *chan;
	struct ast_channel *peer;
	struct ast_callid *callid;                             /*<! callid pointer (Only used to bind thread) */
	unsigned int return_to_pbx:1;
};

static const struct ast_datastore_info channel_app_data_datastore = {
	.type = "Channel appdata datastore",
	.destroy = ast_free_ptr,
};

/*
 * \internal
 * \brief Get the extension for a given builtin feature
 *
 * \pre expects features_lock to be readlocked
 *
 * \retval 0 success
 * \retval non-zero failiure
 */
static int builtin_feature_get_exten(struct ast_channel *chan, const char *feature_name,
		char *buf, size_t len)
{
	SCOPED_CHANNELLOCK(lock, chan);

	return ast_get_builtin_feature(chan, feature_name, buf, len);
}

static void set_config_flags(struct ast_channel *chan, struct ast_bridge_config *config)
{
/* BUGBUG there is code that checks AST_BRIDGE_IGNORE_SIGS but no code to set it. */
/* BUGBUG there is code that checks AST_BRIDGE_REC_CHANNEL_0 but no code to set it. */
/* BUGBUG there is code that checks AST_BRIDGE_REC_CHANNEL_1 but no code to set it. */
	ast_clear_flag(config, AST_FLAGS_ALL);

	if (ast_test_flag(&config->features_caller, AST_FEATURE_DTMF_MASK)) {
		ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
	}
	if (ast_test_flag(&config->features_callee, AST_FEATURE_DTMF_MASK)) {
		ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
	}

	if (!(ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_0) && ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_1))) {
		RAII_VAR(struct ao2_container *, applicationmap, NULL, ao2_cleanup);

		ast_channel_lock(chan);
		applicationmap = ast_get_chan_applicationmap(chan);
		ast_channel_unlock(chan);

		if (!applicationmap) {
			return;
		}

		/* If an applicationmap exists for this channel at all, then the channel needs the DTMF flag set */
		ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
	}
}

void ast_channel_log(char *title, struct ast_channel *chan);

void ast_channel_log(char *title, struct ast_channel *chan) /* for debug, this is handy enough to justify keeping it in the source */
{
	ast_log(LOG_NOTICE, "______ %s (%lx)______\n", title, (unsigned long) chan);
	ast_log(LOG_NOTICE, "CHAN: name: %s;  appl: %s; data: %s; contxt: %s;  exten: %s; pri: %d;\n",
		ast_channel_name(chan), ast_channel_appl(chan), ast_channel_data(chan),
		ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan));
	ast_log(LOG_NOTICE, "CHAN: acctcode: %s;  dialcontext: %s; amaflags: %x; maccontxt: %s;  macexten: %s; macpri: %d;\n",
		ast_channel_accountcode(chan), ast_channel_dialcontext(chan), ast_channel_amaflags(chan),
		ast_channel_macrocontext(chan), ast_channel_macroexten(chan), ast_channel_macropriority(chan));
	ast_log(LOG_NOTICE, "CHAN: masq: %p;  masqr: %p; uniqueID: %s; linkedID:%s\n",
		ast_channel_masq(chan), ast_channel_masqr(chan),
		ast_channel_uniqueid(chan), ast_channel_linkedid(chan));
	if (ast_channel_masqr(chan)) {
		ast_log(LOG_NOTICE, "CHAN: masquerading as: %s;  cdr: %p;\n",
			ast_channel_name(ast_channel_masqr(chan)), ast_channel_cdr(ast_channel_masqr(chan)));
	}

	ast_log(LOG_NOTICE, "===== done ====\n");
}

static void set_bridge_features_on_config(struct ast_bridge_config *config, const char *features)
{
	const char *feature;

	if (ast_strlen_zero(features)) {
		return;
	}

	for (feature = features; *feature; feature++) {
		struct ast_flags *party;

		if (isupper(*feature)) {
			party = &config->features_caller;
		} else {
			party = &config->features_callee;
		}

		switch (tolower(*feature)) {
		case 't' :
			ast_set_flag(party, AST_FEATURE_REDIRECT);
			break;
		case 'k' :
			ast_set_flag(party, AST_FEATURE_PARKCALL);
			break;
		case 'h' :
			ast_set_flag(party, AST_FEATURE_DISCONNECT);
			break;
		case 'w' :
			ast_set_flag(party, AST_FEATURE_AUTOMON);
			break;
		case 'x' :
			ast_set_flag(party, AST_FEATURE_AUTOMIXMON);
			break;
		default :
			ast_log(LOG_WARNING, "Skipping unknown feature code '%c'\n", *feature);
			break;
		}
	}
}

static void add_features_datastores(struct ast_channel *caller, struct ast_channel *callee, struct ast_bridge_config *config)
{
	if (add_features_datastore(caller, &config->features_caller, &config->features_callee)) {
		/*
		 * If we don't return here, then when we do a builtin_atxfer we
		 * will copy the disconnect flags over from the atxfer to the
		 * callee (Party C).
		 */
		return;
	}

	add_features_datastore(callee, &config->features_callee, &config->features_caller);
}

static void clear_dialed_interfaces(struct ast_channel *chan)
{
	struct ast_datastore *di_datastore;

	ast_channel_lock(chan);
	if ((di_datastore = ast_channel_datastore_find(chan, &dialed_interface_info, NULL))) {
		if (option_debug) {
			ast_log(LOG_DEBUG, "Removing dialed interfaces datastore on %s since we're bridging\n", ast_channel_name(chan));
		}
		if (!ast_channel_datastore_remove(chan, di_datastore)) {
			ast_datastore_free(di_datastore);
		}
	}
	ast_channel_unlock(chan);
}

/*!
 * \internal
 * \brief Helper to add a builtin DTMF feature hook to the features struct.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 * \param flags Feature flags on the channel.
 * \param feature_flag Feature flag to test.
 * \param feature_name features.conf name of feature.
 * \param feature_bridge Bridge feature enum to get hook callback.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int builtin_features_helper(struct ast_bridge_features *features, struct ast_channel *chan,
	struct ast_flags *flags, unsigned int feature_flag, const char *feature_name, enum ast_bridge_builtin_feature feature_bridge)
{
	char dtmf[AST_FEATURE_MAX_LEN];
	int res;

	res = 0;
	if (ast_test_flag(flags, feature_flag)
		&& !builtin_feature_get_exten(chan, feature_name, dtmf, sizeof(dtmf))
		&& !ast_strlen_zero(dtmf)) {
		res = ast_bridge_features_enable(features, feature_bridge, dtmf, NULL, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PULL | AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE);
		if (res) {
			ast_log(LOG_ERROR, "Channel %s: Requested DTMF feature %s not available.\n",
				ast_channel_name(chan), feature_name);
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Setup bridge builtin features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_bridge_features_builtin(struct ast_bridge_features *features, struct ast_channel *chan)
{
	struct ast_flags *flags;
	int res;

	ast_channel_lock(chan);
	flags = ast_bridge_features_ds_get(chan);
	ast_channel_unlock(chan);
	if (!flags) {
		return 0;
	}

	res = 0;
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_REDIRECT, "blindxfer", AST_BRIDGE_BUILTIN_BLINDTRANSFER);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_REDIRECT, "atxfer", AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_DISCONNECT, "disconnect", AST_BRIDGE_BUILTIN_HANGUP);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_PARKCALL, "parkcall", AST_BRIDGE_BUILTIN_PARKCALL);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_AUTOMON, "automon", AST_BRIDGE_BUILTIN_AUTOMON);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_AUTOMIXMON, "automixmon", AST_BRIDGE_BUILTIN_AUTOMIXMON);

	return res ? -1 : 0;
}

struct dynamic_dtmf_hook_run {
	/*! Offset into app_name[] where the channel name that activated the hook starts. */
	int activated_offset;
	/*! Offset into app_name[] where the dynamic feature name starts. */
	int feature_offset;
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

static void dynamic_dtmf_hook_callback(struct ast_bridge_channel *bridge_channel,
	const void *payload, size_t payload_size)
{
	struct ast_channel *chan = bridge_channel->chan;
	const struct dynamic_dtmf_hook_run *run_data = payload;

	pbx_builtin_setvar_helper(chan, "DYNAMIC_FEATURENAME",
		&run_data->app_name[run_data->feature_offset]);
	pbx_builtin_setvar_helper(chan, "DYNAMIC_WHO_ACTIVATED",
		&run_data->app_name[run_data->activated_offset]);

	ast_bridge_channel_run_app(bridge_channel, run_data->app_name,
		run_data->app_args_offset ? &run_data->app_name[run_data->app_args_offset] : NULL,
		run_data->moh_offset ? &run_data->app_name[run_data->moh_offset] : NULL);
}

static int dynamic_dtmf_hook_run_callback(struct ast_bridge_channel *bridge_channel,
	ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size)
{
	callback(bridge_channel, payload, payload_size);
	return 0;
}

struct dynamic_dtmf_hook_data {
	/*! Which side of bridge to run app (AST_FEATURE_FLAG_ONSELF/AST_FEATURE_FLAG_ONPEER) */
	unsigned int flags;
	/*! Offset into app_name[] where the dynamic feature name starts. */
	int feature_offset;
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

/*!
 * \internal
 * \brief Activated dynamic DTMF feature hook.
 * \since 12.0.0
 *
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
static int dynamic_dtmf_hook_trip(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct dynamic_dtmf_hook_data *pvt = hook_pvt;
	int (*run_it)(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_callback_fn callback, const void *payload, size_t payload_size);
	struct dynamic_dtmf_hook_run *run_data;
	const char *activated_name;
	size_t len_name;
	size_t len_args;
	size_t len_moh;
	size_t len_feature;
	size_t len_activated;
	size_t len_data;

	/* Determine lengths of things. */
	len_name = strlen(pvt->app_name) + 1;
	len_args = pvt->app_args_offset ? strlen(&pvt->app_name[pvt->app_args_offset]) + 1 : 0;
	len_moh = pvt->moh_offset ? strlen(&pvt->app_name[pvt->moh_offset]) + 1 : 0;
	len_feature = strlen(&pvt->app_name[pvt->feature_offset]) + 1;
	ast_channel_lock(bridge_channel->chan);
	activated_name = ast_strdupa(ast_channel_name(bridge_channel->chan));
	ast_channel_unlock(bridge_channel->chan);
	len_activated = strlen(activated_name) + 1;
	len_data = sizeof(*run_data) + len_name + len_args + len_moh + len_feature + len_activated;

	/* Fill in dynamic feature run hook data. */
	run_data = ast_alloca(len_data);
	run_data->app_args_offset = len_args ? len_name : 0;
	run_data->moh_offset = len_moh ? len_name + len_args : 0;
	run_data->feature_offset = len_name + len_args + len_moh;
	run_data->activated_offset = len_name + len_args + len_moh + len_feature;
	strcpy(run_data->app_name, pvt->app_name);/* Safe */
	if (len_args) {
		strcpy(&run_data->app_name[run_data->app_args_offset],
			&pvt->app_name[pvt->app_args_offset]);/* Safe */
	}
	if (len_moh) {
		strcpy(&run_data->app_name[run_data->moh_offset],
			&pvt->app_name[pvt->moh_offset]);/* Safe */
	}
	strcpy(&run_data->app_name[run_data->feature_offset],
		&pvt->app_name[pvt->feature_offset]);/* Safe */
	strcpy(&run_data->app_name[run_data->activated_offset], activated_name);/* Safe */

	if (ast_test_flag(pvt, AST_FEATURE_FLAG_ONPEER)) {
		run_it = ast_bridge_channel_write_callback;
	} else {
		run_it = dynamic_dtmf_hook_run_callback;
	}
	run_it(bridge_channel, dynamic_dtmf_hook_callback, run_data, len_data);
	return 0;
}

/*!
 * \internal
 * \brief Add a dynamic DTMF feature hook to the bridge features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param flags Which side of bridge to run app (AST_FEATURE_FLAG_ONSELF/AST_FEATURE_FLAG_ONPEER).
 * \param dtmf DTMF trigger sequence.
 * \param feature_name Name of the dynamic feature.
 * \param app_name Dialplan application name to run.
 * \param app_args Dialplan application arguments. (Empty or NULL if no arguments)
 * \param moh_class MOH class to play to peer. (Empty or NULL if no MOH played)
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dynamic_dtmf_hook_add(struct ast_bridge_features *features, unsigned int flags, const char *dtmf, const char *feature_name, const char *app_name, const char *app_args, const char *moh_class)
{
	struct dynamic_dtmf_hook_data *hook_data;
	size_t len_name = strlen(app_name) + 1;
	size_t len_args = ast_strlen_zero(app_args) ? 0 : strlen(app_args) + 1;
	size_t len_moh = ast_strlen_zero(moh_class) ? 0 : strlen(moh_class) + 1;
	size_t len_feature = strlen(feature_name) + 1;
	size_t len_data = sizeof(*hook_data) + len_name + len_args + len_moh + len_feature;
	int res;

	/* Fill in application run hook data. */
	hook_data = ast_malloc(len_data);
	if (!hook_data) {
		return -1;
	}
	hook_data->flags = flags;
	hook_data->app_args_offset = len_args ? len_name : 0;
	hook_data->moh_offset = len_moh ? len_name + len_args : 0;
	hook_data->feature_offset = len_name + len_args + len_moh;
	strcpy(hook_data->app_name, app_name);/* Safe */
	if (len_args) {
		strcpy(&hook_data->app_name[hook_data->app_args_offset], app_args);/* Safe */
	}
	if (len_moh) {
		strcpy(&hook_data->app_name[hook_data->moh_offset], moh_class);/* Safe */
	}
	strcpy(&hook_data->app_name[hook_data->feature_offset], feature_name);/* Safe */

	res = ast_bridge_dtmf_hook(features, dtmf, dynamic_dtmf_hook_trip, hook_data,
		ast_free_ptr, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
	if (res) {
		ast_free(hook_data);
	}
	return res;
}

static int setup_dynamic_feature(void *obj, void *arg, void *data, int flags)
{
	struct ast_applicationmap_item *item = obj;
	struct ast_bridge_features *features = arg;
	int *res = data;

	*res |= dynamic_dtmf_hook_add(features,
		item->activate_on_self ? AST_FEATURE_FLAG_ONSELF : AST_FEATURE_FLAG_ONPEER,
		item->dtmf, item->name, item->app, item->app_data, item->moh_class);

	return 0;
}

/*!
 * \internal
 * \brief Setup bridge dynamic features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_bridge_features_dynamic(struct ast_bridge_features *features, struct ast_channel *chan)
{
	RAII_VAR(struct ao2_container *, applicationmap, NULL, ao2_cleanup);
	int res = 0;

	ast_channel_lock(chan);
	applicationmap = ast_get_chan_applicationmap(chan);
	ast_channel_unlock(chan);
	if (!applicationmap) {
		return 0;
	}

	ao2_callback_data(applicationmap, 0, setup_dynamic_feature, features, &res);

	return res;
}

/* BUGBUG this really should be made a private function of bridge_basic.c after struct ast_call_feature is made an ao2 object. */
int ast_bridge_channel_setup_features(struct ast_bridge_channel *bridge_channel)
{
	int res = 0;

	/* Always pass through any DTMF digits. */
	bridge_channel->features->dtmf_passthrough = 1;

	res |= setup_bridge_features_builtin(bridge_channel->features, bridge_channel->chan);
	res |= setup_bridge_features_dynamic(bridge_channel->features, bridge_channel->chan);

	return res;
}

static void bridge_config_set_limits_warning_values(struct ast_bridge_config *config, struct ast_bridge_features_limits *limits)
{
	if (config->end_sound) {
		ast_string_field_set(limits, duration_sound, config->end_sound);
	}

	if (config->warning_sound) {
		ast_string_field_set(limits, warning_sound, config->warning_sound);
	}

	if (config->start_sound) {
		ast_string_field_set(limits, connect_sound, config->start_sound);
	}

	limits->frequency = config->warning_freq;
	limits->warning = config->play_warning;
}

/*!
 * \internal brief Setup limit hook structures on calls that need limits
 *
 * \param config ast_bridge_config which provides the limit data
 * \param caller_limits pointer to an ast_bridge_features_limits struct which will store the caller side limits
 * \param callee_limits pointer to an ast_bridge_features_limits struct which will store the callee side limits
 */
static void bridge_config_set_limits(struct ast_bridge_config *config, struct ast_bridge_features_limits *caller_limits, struct ast_bridge_features_limits *callee_limits)
{
	if (ast_test_flag(&config->features_caller, AST_FEATURE_PLAY_WARNING)) {
		bridge_config_set_limits_warning_values(config, caller_limits);
	}

	if (ast_test_flag(&config->features_callee, AST_FEATURE_PLAY_WARNING)) {
		bridge_config_set_limits_warning_values(config, callee_limits);
	}

	caller_limits->duration = config->timelimit;
	callee_limits->duration = config->timelimit;
}

/*!
 * \internal
 * \brief Check if Monitor needs to be started on a channel.
 * \since 12.0.0
 *
 * \param chan The bridge considers this channel the caller.
 * \param peer The bridge considers this channel the callee.
 *
 * \return Nothing
 */
static void bridge_check_monitor(struct ast_channel *chan, struct ast_channel *peer)
{
	const char *value;
	const char *monitor_args = NULL;
	struct ast_channel *monitor_chan = NULL;

	ast_channel_lock(chan);
	value = pbx_builtin_getvar_helper(chan, "AUTO_MONITOR");
	if (!ast_strlen_zero(value)) {
		monitor_args = ast_strdupa(value);
		monitor_chan = chan;
	}
	ast_channel_unlock(chan);
	if (!monitor_chan) {
		ast_channel_lock(peer);
		value = pbx_builtin_getvar_helper(peer, "AUTO_MONITOR");
		if (!ast_strlen_zero(value)) {
			monitor_args = ast_strdupa(value);
			monitor_chan = peer;
		}
		ast_channel_unlock(peer);
	}
	if (monitor_chan) {
		struct ast_app *monitor_app;

		monitor_app = pbx_findapp("Monitor");
		if (monitor_app) {
			pbx_exec(monitor_chan, monitor_app, monitor_args);
		}
	}
}

/*!
 * \internal
 * \brief Send the peer channel on its way on bridge start failure.
 * \since 12.0.0
 *
 * \param chan Chan to put into autoservice.
 * \param peer Chan to send to after bridge goto or run hangup handlers and hangup.
 *
 * \return Nothing
 */
static void bridge_failed_peer_goto(struct ast_channel *chan, struct ast_channel *peer)
{
	if (ast_bridge_setup_after_goto(peer)
		|| ast_pbx_start(peer)) {
		ast_autoservice_chan_hangup_peer(chan, peer);
	}
}

static int pre_bridge_setup(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config,
		struct ast_bridge_features *chan_features, struct ast_bridge_features *peer_features)
{
	int res;

	set_bridge_features_on_config(config, pbx_builtin_getvar_helper(chan, "BRIDGE_FEATURES"));
	add_features_datastores(chan, peer, config);

	/*
	 * This is an interesting case.  One example is if a ringing
	 * channel gets redirected to an extension that picks up a
	 * parked call.  This will make sure that the call taken out of
	 * parking gets told that the channel it just got bridged to is
	 * still ringing.
	 */
	if (ast_channel_state(chan) == AST_STATE_RINGING
		&& ast_channel_visible_indication(peer) != AST_CONTROL_RINGING) {
		ast_indicate(peer, AST_CONTROL_RINGING);
	}

	bridge_check_monitor(chan, peer);

	set_config_flags(chan, config);

	/* Answer if need be */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_raw_answer(chan)) {
			return -1;
		}
	}

#ifdef FOR_DEBUG
	/* show the two channels and cdrs involved in the bridge for debug & devel purposes */
	ast_channel_log("Pre-bridge CHAN Channel info", chan);
	ast_channel_log("Pre-bridge PEER Channel info", peer);
#endif

	/*
	 * If we are bridging a call, stop worrying about forwarding
	 * loops.  We presume that if a call is being bridged, that the
	 * humans in charge know what they're doing.  If they don't,
	 * well, what can we do about that?
	 */
	clear_dialed_interfaces(chan);
	clear_dialed_interfaces(peer);

	res = 0;
	ast_channel_lock(chan);
	res |= ast_bridge_features_ds_append(chan, &config->features_caller);
	ast_channel_unlock(chan);
	ast_channel_lock(peer);
	res |= ast_bridge_features_ds_append(peer, &config->features_callee);
	ast_channel_unlock(peer);

	if (res) {
		return -1;
	}

	if (config->timelimit) {
		struct ast_bridge_features_limits call_duration_limits_chan;
		struct ast_bridge_features_limits call_duration_limits_peer;
		int abandon_call = 0; /* TRUE if set limits fails so we can abandon the call. */

		if (ast_bridge_features_limits_construct(&call_duration_limits_chan)) {
			ast_log(LOG_ERROR, "Could not construct caller duration limits. Bridge canceled.\n");

			return -1;
		}

		if (ast_bridge_features_limits_construct(&call_duration_limits_peer)) {
			ast_log(LOG_ERROR, "Could not construct callee duration limits. Bridge canceled.\n");
			ast_bridge_features_limits_destroy(&call_duration_limits_chan);

			return -1;
		}

		bridge_config_set_limits(config, &call_duration_limits_chan, &call_duration_limits_peer);

		if (ast_bridge_features_set_limits(chan_features, &call_duration_limits_chan, 0)) {
			abandon_call = 1;
		}
		if (ast_bridge_features_set_limits(peer_features, &call_duration_limits_peer, 0)) {
			abandon_call = 1;
		}

		/* At this point we are done with the limits structs since they have been copied to the individual feature sets. */
		ast_bridge_features_limits_destroy(&call_duration_limits_chan);
		ast_bridge_features_limits_destroy(&call_duration_limits_peer);

		if (abandon_call) {
			ast_log(LOG_ERROR, "Could not set duration limits on one or more sides of the call. Bridge canceled.\n");
			return -1;
		}
	}

	return 0;
}

/*!
 * \brief bridge the call and set CDR
 *
 * \param chan The bridge considers this channel the caller.
 * \param peer The bridge considers this channel the callee.
 * \param config Configuration for this bridge.
 *
 * Set start time, check for two channels,check if monitor on
 * check for feature activation, create new CDR
 * \retval res on success.
 * \retval -1 on failure to bridge.
 */
int ast_bridge_call(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config)
{
	int res;
	struct ast_bridge *bridge;
	struct ast_bridge_features chan_features;
	struct ast_bridge_features *peer_features;

	/* Setup features. */
	res = ast_bridge_features_init(&chan_features);
	peer_features = ast_bridge_features_new();
	if (res || !peer_features) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	if (pre_bridge_setup(chan, peer, config, &chan_features, peer_features)) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	/* Create bridge */
	bridge = ast_bridge_basic_new();
	if (!bridge) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	/* Put peer into the bridge */
	if (ast_bridge_impart(bridge, peer, NULL, peer_features, 1)) {
		ast_bridge_destroy(bridge);
		ast_bridge_features_cleanup(&chan_features);
		bridge_failed_peer_goto(chan, peer);
		return -1;
	}

	/* Join bridge */
	ast_bridge_join(bridge, chan, NULL, &chan_features, NULL, 1);

	/*
	 * If the bridge was broken for a hangup that isn't real, then
	 * don't run the h extension, because the channel isn't really
	 * hung up.  This should really only happen with
	 * AST_SOFTHANGUP_ASYNCGOTO.
	 */
	res = -1;
	ast_channel_lock(chan);
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		res = 0;
	}
	ast_channel_unlock(chan);

	ast_bridge_features_cleanup(&chan_features);

/* BUGBUG this is used by Dial and FollowMe for CDR information.  By Queue for Queue stats like CDRs. */
	if (res && config->end_bridge_callback) {
		config->end_bridge_callback(config->end_bridge_callback_data);
	}

	return res;
}

enum play_tone_action {
	PLAYTONE_NONE = 0,
	PLAYTONE_CHANNEL1 = (1 << 0),
	PLAYTONE_CHANNEL2 = (1 << 1),
	PLAYTONE_BOTH = PLAYTONE_CHANNEL1 | PLAYTONE_CHANNEL2,
};

static enum play_tone_action parse_playtone(const char *playtone_val)
{
	if (ast_strlen_zero(playtone_val) || ast_false(playtone_val)) {
		return PLAYTONE_NONE;
	} if (!strcasecmp(playtone_val, "channel1")) {
		return PLAYTONE_CHANNEL1;
	} else if (!strcasecmp(playtone_val, "channel2") || ast_true(playtone_val)) {
		return PLAYTONE_CHANNEL2;
	} else if (!strcasecmp(playtone_val, "both")) {
		return PLAYTONE_BOTH;
	} else {
		/* Invalid input. Assume none */
		return PLAYTONE_NONE;
	}
}

/*!
 * \brief Bridge channels together
 * \param s
 * \param m
 *
 * Make sure valid channels were specified,
 * send errors if any of the channels could not be found/locked, answer channels if needed,
 * create the placeholder channels and grab the other channels
 * make the channels compatible, send error if we fail doing so
 * setup the bridge thread object and start the bridge.
 *
 * \retval 0
 */
static int action_bridge(struct mansession *s, const struct message *m)
{
	const char *channela = astman_get_header(m, "Channel1");
	const char *channelb = astman_get_header(m, "Channel2");
	enum play_tone_action playtone = parse_playtone(astman_get_header(m, "Tone"));
	RAII_VAR(struct ast_channel *, chana, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chanb, NULL, ao2_cleanup);
	const char *chana_exten;
	const char *chana_context;
	int chana_priority;
	const char *chanb_exten;
	const char *chanb_context;
	int chanb_priority;
	struct ast_bridge *bridge;
	char buf[256];
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg_a, NULL, ao2_cleanup);
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg_b, NULL, ao2_cleanup);

	/* make sure valid channels were specified */
	if (ast_strlen_zero(channela) || ast_strlen_zero(channelb)) {
		astman_send_error(s, m, "Missing channel parameter in request");
		return 0;
	}

	ast_debug(1, "Performing Bridge action on %s and %s\n", channela, channelb);

	/* Start with chana */
	chana = ast_channel_get_by_name_prefix(channela, strlen(channela));
	if (!chana) {
		snprintf(buf, sizeof(buf), "Channel1 does not exist: %s", channela);
		astman_send_error(s, m, buf);
		return 0;
	}
	xfer_cfg_a = ast_get_chan_features_xfer_config(chana);
	ast_channel_lock(chana);
	chana_exten = ast_strdupa(ast_channel_exten(chana));
	chana_context = ast_strdupa(ast_channel_context(chana));
	chana_priority = ast_channel_priority(chana);
	if (!ast_test_flag(ast_channel_flags(chana), AST_FLAG_IN_AUTOLOOP)) {
		chana_priority++;
	}
	ast_channel_unlock(chana);

	chanb = ast_channel_get_by_name_prefix(channelb, strlen(channelb));
	if (!chanb) {
		snprintf(buf, sizeof(buf), "Channel2 does not exist: %s", channelb);
		astman_send_error(s, m, buf);
		return 0;
	}
	xfer_cfg_b = ast_get_chan_features_xfer_config(chanb);
	ast_channel_lock(chanb);
	chanb_exten = ast_strdupa(ast_channel_exten(chanb));
	chanb_context = ast_strdupa(ast_channel_context(chanb));
	chanb_priority = ast_channel_priority(chanb);
	if (!ast_test_flag(ast_channel_flags(chanb), AST_FLAG_IN_AUTOLOOP)) {
		chanb_priority++;
	}
	ast_channel_unlock(chanb);

	bridge = ast_bridge_basic_new();
	if (!bridge) {
		astman_send_error(s, m, "Unable to create bridge\n");
		return 0;
	}

	ast_bridge_set_after_go_on(chana, chana_context, chana_exten, chana_priority, NULL);
	if (ast_bridge_add_channel(bridge, chana, NULL, playtone & PLAYTONE_CHANNEL1, xfer_cfg_a ? xfer_cfg_a->xfersound : NULL)) {
		snprintf(buf, sizeof(buf), "Unable to add Channel1 to bridge: %s", ast_channel_name(chana));
		astman_send_error(s, m, buf);
		ast_bridge_destroy(bridge);
		return 0;
	}

	ast_bridge_set_after_go_on(chanb, chanb_context, chanb_exten, chanb_priority, NULL);
	if (ast_bridge_add_channel(bridge, chanb, NULL, playtone & PLAYTONE_CHANNEL2, xfer_cfg_b ? xfer_cfg_b->xfersound : NULL)) {
		snprintf(buf, sizeof(buf), "Unable to add Channel2 to bridge: %s", ast_channel_name(chanb));
		astman_send_error(s, m, buf);
		ast_bridge_destroy(bridge);
		return 0;
	}

	astman_send_ack(s, m, "Channels have been bridged");

	return 0;
}

static char *app_bridge = "Bridge";

enum {
	BRIDGE_OPT_PLAYTONE = (1 << 0),
	OPT_CALLEE_HANGUP =	(1 << 1),
	OPT_CALLER_HANGUP =	(1 << 2),
	OPT_DURATION_LIMIT = (1 << 3),
	OPT_DURATION_STOP =	(1 << 4),
	OPT_CALLEE_TRANSFER = (1 << 5),
	OPT_CALLER_TRANSFER = (1 << 6),
	OPT_CALLEE_MONITOR = (1 << 7),
	OPT_CALLER_MONITOR = (1 << 8),
	OPT_CALLEE_PARK = (1 << 9),
	OPT_CALLER_PARK = (1 << 10),
	OPT_CALLEE_KILL = (1 << 11),
	OPT_CALLEE_GO_ON = (1 << 12),
};

enum {
	OPT_ARG_DURATION_LIMIT = 0,
	OPT_ARG_DURATION_STOP,
	OPT_ARG_CALLEE_GO_ON,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(bridge_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('p', BRIDGE_OPT_PLAYTONE),
	AST_APP_OPTION_ARG('F', OPT_CALLEE_GO_ON, OPT_ARG_CALLEE_GO_ON),
	AST_APP_OPTION('h', OPT_CALLEE_HANGUP),
	AST_APP_OPTION('H', OPT_CALLER_HANGUP),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION_ARG('L', OPT_DURATION_LIMIT, OPT_ARG_DURATION_LIMIT),
	AST_APP_OPTION_ARG('S', OPT_DURATION_STOP, OPT_ARG_DURATION_STOP),
	AST_APP_OPTION('t', OPT_CALLEE_TRANSFER),
	AST_APP_OPTION('T', OPT_CALLER_TRANSFER),
	AST_APP_OPTION('w', OPT_CALLEE_MONITOR),
	AST_APP_OPTION('W', OPT_CALLER_MONITOR),
	AST_APP_OPTION('x', OPT_CALLEE_KILL),
END_OPTIONS );

int ast_bridge_timelimit(struct ast_channel *chan, struct ast_bridge_config *config,
	char *parse, struct timeval *calldurationlimit)
{
	char *stringp = ast_strdupa(parse);
	char *limit_str, *warning_str, *warnfreq_str;
	const char *var;
	int play_to_caller = 0, play_to_callee = 0;
	int delta;

	limit_str = strsep(&stringp, ":");
	warning_str = strsep(&stringp, ":");
	warnfreq_str = strsep(&stringp, ":");

	config->timelimit = atol(limit_str);
	if (warning_str)
		config->play_warning = atol(warning_str);
	if (warnfreq_str)
		config->warning_freq = atol(warnfreq_str);

	if (!config->timelimit) {
		ast_log(LOG_WARNING, "Bridge does not accept L(%s), hanging up.\n", limit_str);
		config->timelimit = config->play_warning = config->warning_freq = 0;
		config->warning_sound = NULL;
		return -1; /* error */
	} else if ( (delta = config->play_warning - config->timelimit) > 0) {
		int w = config->warning_freq;

		/*
		 * If the first warning is requested _after_ the entire call
		 * would end, and no warning frequency is requested, then turn
		 * off the warning. If a warning frequency is requested, reduce
		 * the 'first warning' time by that frequency until it falls
		 * within the call's total time limit.
		 *
		 * Graphically:
		 *                timelim->|    delta        |<-playwarning
		 *      0__________________|_________________|
		 *                       | w  |    |    |    |
		 *
		 * so the number of intervals to cut is 1+(delta-1)/w
		 */
		if (w == 0) {
			config->play_warning = 0;
		} else {
			config->play_warning -= w * ( 1 + (delta-1)/w );
			if (config->play_warning < 1)
				config->play_warning = config->warning_freq = 0;
		}
	}

	ast_channel_lock(chan);

	var = pbx_builtin_getvar_helper(chan, "LIMIT_PLAYAUDIO_CALLER");
	play_to_caller = var ? ast_true(var) : 1;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_PLAYAUDIO_CALLEE");
	play_to_callee = var ? ast_true(var) : 0;

	if (!play_to_caller && !play_to_callee)
		play_to_caller = 1;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_WARNING_FILE");
	config->warning_sound = !ast_strlen_zero(var) ? ast_strdup(var) : ast_strdup("timeleft");

	/* The code looking at config wants a NULL, not just "", to decide
	 * that the message should not be played, so we replace "" with NULL.
	 * Note, pbx_builtin_getvar_helper _can_ return NULL if the variable is
	 * not found.
	 */

	var = pbx_builtin_getvar_helper(chan, "LIMIT_TIMEOUT_FILE");
	config->end_sound = !ast_strlen_zero(var) ? ast_strdup(var) : NULL;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_CONNECT_FILE");
	config->start_sound = !ast_strlen_zero(var) ? ast_strdup(var) : NULL;

	ast_channel_unlock(chan);

	/* undo effect of S(x) in case they are both used */
	calldurationlimit->tv_sec = 0;
	calldurationlimit->tv_usec = 0;

	/* more efficient to do it like S(x) does since no advanced opts */
	if (!config->play_warning && !config->start_sound && !config->end_sound && config->timelimit) {
		calldurationlimit->tv_sec = config->timelimit / 1000;
		calldurationlimit->tv_usec = (config->timelimit % 1000) * 1000;
		ast_verb(3, "Setting call duration limit to %.3lf seconds.\n",
			calldurationlimit->tv_sec + calldurationlimit->tv_usec / 1000000.0);
		play_to_caller = 0;
		play_to_callee = 0;
		config->timelimit = 0;
		config->play_warning = 0;
		config->warning_freq = 0;
	} else {
		ast_verb(4, "Limit Data for this call:\n");
		ast_verb(4, "timelimit      = %ld ms (%.3lf s)\n", config->timelimit, config->timelimit / 1000.0);
		ast_verb(4, "play_warning   = %ld ms (%.3lf s)\n", config->play_warning, config->play_warning / 1000.0);
		ast_verb(4, "play_to_caller = %s\n", play_to_caller ? "yes" : "no");
		ast_verb(4, "play_to_callee = %s\n", play_to_callee ? "yes" : "no");
		ast_verb(4, "warning_freq   = %ld ms (%.3lf s)\n", config->warning_freq, config->warning_freq / 1000.0);
		ast_verb(4, "start_sound    = %s\n", S_OR(config->start_sound, ""));
		ast_verb(4, "warning_sound  = %s\n", config->warning_sound);
		ast_verb(4, "end_sound      = %s\n", S_OR(config->end_sound, ""));
	}
	if (play_to_caller)
		ast_set_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING);
	if (play_to_callee)
		ast_set_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING);
	return 0;
}


/*!
 * \brief Bridge channels
 * \param chan
 * \param data channel to bridge with.
 *
 * Split data, check we aren't bridging with ourself, check valid channel,
 * answer call if not already, check compatible channels, setup bridge config
 * now bridge call, if transfered party hangs up return to PBX extension.
 */
static int bridge_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct ast_channel *, current_dest_chan, NULL, ao2_cleanup);
	char *tmp_data  = NULL;
	struct ast_flags opts = { 0, };
	struct ast_bridge_config bconfig = { { 0, }, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct timeval calldurationlimit = { 0, };
	const char *context;
	const char *extension;
	int priority;
	struct ast_bridge_features chan_features;
	struct ast_bridge_features *peer_features;
	struct ast_bridge *bridge;
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg, NULL, ao2_cleanup);

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dest_chan);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Bridge require at least 1 argument specifying the other end of the bridge\n");
		return -1;
	}

	tmp_data = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, tmp_data);
	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(bridge_exec_options, &opts, opt_args, args.options);

	/* make sure we have a valid end point */
	if (!(current_dest_chan = ast_channel_get_by_name_prefix(args.dest_chan,
			strlen(args.dest_chan)))) {
		ast_log(LOG_WARNING, "Bridge failed because channel %s does not exist\n",
			args.dest_chan);
		return 0;
	}

	/* avoid bridge with ourselves */
	if (chan == current_dest_chan) {
		ast_log(LOG_WARNING, "Unable to bridge channel %s with itself\n", ast_channel_name(chan));
		return 0;
	}

	if (ast_test_flag(&opts, OPT_DURATION_LIMIT)
		&& !ast_strlen_zero(opt_args[OPT_ARG_DURATION_LIMIT])
		&& ast_bridge_timelimit(chan, &bconfig, opt_args[OPT_ARG_DURATION_LIMIT], &calldurationlimit)) {
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "FAILURE");
		goto done;
	}

	if (ast_test_flag(&opts, OPT_CALLEE_TRANSFER))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_REDIRECT);
	if (ast_test_flag(&opts, OPT_CALLER_TRANSFER))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_REDIRECT);
	if (ast_test_flag(&opts, OPT_CALLEE_HANGUP))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);
	if (ast_test_flag(&opts, OPT_CALLER_HANGUP))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
	if (ast_test_flag(&opts, OPT_CALLEE_MONITOR))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_AUTOMON);
	if (ast_test_flag(&opts, OPT_CALLER_MONITOR))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_AUTOMON);
	if (ast_test_flag(&opts, OPT_CALLEE_PARK))
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_PARKCALL);
	if (ast_test_flag(&opts, OPT_CALLER_PARK))
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_PARKCALL);

	/* Setup after bridge goto location. */
	if (ast_test_flag(&opts, OPT_CALLEE_GO_ON)) {
		ast_channel_lock(chan);
		context = ast_strdupa(ast_channel_context(chan));
		extension = ast_strdupa(ast_channel_exten(chan));
		priority = ast_channel_priority(chan);
		ast_channel_unlock(chan);
		ast_bridge_set_after_go_on(current_dest_chan, context, extension, priority,
			opt_args[OPT_ARG_CALLEE_GO_ON]);
	} else if (!ast_test_flag(&opts, OPT_CALLEE_KILL)) {
		ast_channel_lock(current_dest_chan);
		context = ast_strdupa(ast_channel_context(current_dest_chan));
		extension = ast_strdupa(ast_channel_exten(current_dest_chan));
		priority = ast_channel_priority(current_dest_chan);
		ast_channel_unlock(current_dest_chan);
		ast_bridge_set_after_goto(current_dest_chan, context, extension, priority);
	}

	if (ast_bridge_features_init(&chan_features)) {
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	peer_features = ast_bridge_features_new();
	if (!peer_features) {
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	if (pre_bridge_setup(chan, current_dest_chan, &bconfig, &chan_features, peer_features)) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	bridge = ast_bridge_basic_new();
	if (!bridge) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		goto done;
	}

	xfer_cfg = ast_get_chan_features_xfer_config(current_dest_chan);
	if (ast_bridge_add_channel(bridge, current_dest_chan, peer_features,
		ast_test_flag(&opts, BRIDGE_OPT_PLAYTONE), xfer_cfg ? xfer_cfg->xfersound : NULL)) {
		ast_bridge_features_destroy(peer_features);
		ast_bridge_features_cleanup(&chan_features);
		ast_bridge_destroy(bridge);
		goto done;
	}

	ast_bridge_join(bridge, chan, NULL, &chan_features, NULL, 1);

	ast_bridge_features_cleanup(&chan_features);

	/* The bridge has ended, set BRIDGERESULT to SUCCESS. */
	pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "SUCCESS");
done:
	ast_free((char *) bconfig.warning_sound);
	ast_free((char *) bconfig.end_sound);
	ast_free((char *) bconfig.start_sound);

	return 0;
}

/*! \internal \brief Clean up resources on Asterisk shutdown */
static void features_shutdown(void)
{
	ast_features_config_shutdown();

	ast_manager_unregister("Bridge");

	ast_unregister_application(app_bridge);

}

int ast_features_init(void)
{
	int res;

	res = ast_features_config_init();
	if (res) {
		return res;
	}
	res |= ast_register_application2(app_bridge, bridge_exec, NULL, NULL, NULL);
	res |= ast_manager_register_xml_core("Bridge", EVENT_FLAG_CALL, action_bridge);

	if (res) {
		features_shutdown();
	} else {
		ast_register_atexit(features_shutdown);
	}

	return res;
}
