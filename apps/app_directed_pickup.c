/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Joshua Colp
 *
 * Joshua Colp <jcolp@digium.com>
 *
 * Portions merged from app_pickupchan, which was
 * Copyright (C) 2008, Gary Cook
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
 * \brief Directed Call Pickup Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Gary Cook
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/features.h"
#include "asterisk/manager.h"
#include "asterisk/callerid.h"
#include "asterisk/cel.h"

#define PICKUPMARK "PICKUPMARK"

/*** DOCUMENTATION
	<application name="Pickup" language="en_US">
		<synopsis>
			Directed extension call pickup.
		</synopsis>
		<syntax argsep="&amp;">
			<parameter name="ext" argsep="@" required="true">
				<argument name="extension" required="true"/>
				<argument name="context" />
			</parameter>
			<parameter name="ext2" argsep="@" multiple="true">
				<argument name="extension2" required="true"/>
				<argument name="context2"/>
			</parameter>
		</syntax>
		<description>
			<para>This application can pickup any ringing channel that is calling
			the specified <replaceable>extension</replaceable>. If no <replaceable>context</replaceable>
			is specified, the current context will be used. If you use the special string <literal>PICKUPMARK</literal>
			for the context parameter, for example 10@PICKUPMARK, this application
			tries to find a channel which has defined a <variable>PICKUPMARK</variable>
			channel variable with the same value as <replaceable>extension</replaceable>
			(in this example, <literal>10</literal>). When no parameter is specified, the application
			will pickup a channel matching the pickup group of the active channel.</para>
		</description>
	</application>
	<application name="PickupChan" language="en_US">
		<synopsis>
			Pickup a ringing channel.
		</synopsis>
		<syntax>
			<parameter name="channel" required="true" />
			<parameter name="channel2" multiple="true" />
			<parameter name="options" required="false">
				<optionlist>
					<option name="p">
						<para>Channel name specified partial name. Used when find channel by callid.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This will pickup a specified <replaceable>channel</replaceable> if ringing.</para>
		</description>
	</application>
 ***/

static const char app[] = "Pickup";
static const char app2[] = "PickupChan";
/*! \todo This application should return a result code, like PICKUPRESULT */

struct pickup_by_name_args {
	const char *name;
	size_t len;
};

static int pickup_by_name_cb(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	struct pickup_by_name_args *args = data;

	ast_channel_lock(target);
	if (!strncasecmp(target->name, args->name, args->len) && ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
}

/*! \brief Helper Function to walk through ALL channels checking NAME and STATE */
static struct ast_channel *my_ast_get_channel_by_name_locked(const char *channame)
{
	char *chkchan;
	struct pickup_by_name_args pickup_args;

	/* Check if channel name contains a '-'.
	 * In this case the channel name will be interpreted as full channel name.
	 */
	if (strchr(channame, '-')) {
		/* check full channel name */
		pickup_args.len = strlen(channame);
		pickup_args.name = channame;
	} else {
		/* need to append a '-' for the comparison so we check full channel name,
		 * i.e SIP/hgc- , use a temporary variable so original stays the same for
		 * debugging.
		 */
		pickup_args.len = strlen(channame) + 1;
		chkchan = alloca(pickup_args.len + 1);
		strcpy(chkchan, channame);
		strcat(chkchan, "-");
		pickup_args.name = chkchan;
	}

	return ast_channel_callback(pickup_by_name_cb, NULL, &pickup_args, 0);
}

/*! \brief Attempt to pick up named channel, does not use context */
static int pickup_by_channel(struct ast_channel *chan, char *pickup)
{
	int res = -1;
	struct ast_channel *target;/*!< Potential pickup target */

	target = my_ast_get_channel_by_name_locked(pickup);
	if (target) {
		/* Just check that we are not picking up the SAME as target. (i.e. ourself) */
		if (chan != target) {
			res = ast_do_pickup(chan, target);
		}
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

/* Attempt to pick up specified extension with context */
static int pickup_by_exten(struct ast_channel *chan, const char *exten, const char *context)
{
	struct ast_channel *target = NULL;/*!< Potential pickup target */
	struct ast_channel_iterator *iter;
	int res = -1;

	if (!(iter = ast_channel_iterator_by_exten_new(exten, context))) {
		return -1;
	}

	while ((target = ast_channel_iterator_next(iter))) {
		ast_channel_lock(target);
		if ((chan != target) && ast_can_pickup(target)) {
			ast_log(LOG_NOTICE, "%s pickup by %s\n", target->name, chan->name);
			break;
		}
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	ast_channel_iterator_destroy(iter);

	if (target) {
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

static int find_by_mark(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	const char *mark = data;
	const char *tmp;

	ast_channel_lock(target);
	tmp = pbx_builtin_getvar_helper(target, PICKUPMARK);
	if (tmp && !strcasecmp(tmp, mark) && ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
}

/* Attempt to pick up specified mark */
static int pickup_by_mark(struct ast_channel *chan, const char *mark)
{
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;

	/* The found channel is already locked. */
	target = ast_channel_callback(find_by_mark, NULL, (char *) mark, 0);
	if (target) {
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

static int find_channel_by_group(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	struct ast_channel *chan = data;/*!< Channel wanting to pickup call */

	ast_channel_lock(target);
	if (chan != target && (chan->pickupgroup & target->callgroup)
		&& ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
}

static int pickup_by_group(struct ast_channel *chan)
{
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;

	/* The found channel is already locked. */
	target = ast_channel_callback(find_channel_by_group, NULL, chan, 0);
	if (target) {
		ast_log(LOG_NOTICE, "pickup %s attempt by %s\n", target->name, chan->name);
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

/* application entry point for Pickup() */
static int pickup_exec(struct ast_channel *chan, const char *data)
{
	char *tmp = ast_strdupa(data);
	char *exten = NULL, *context = NULL;

	if (ast_strlen_zero(data)) {
		return pickup_by_group(chan) ? 0 : -1;
	}

	/* Parse extension (and context if there) */
	while (!ast_strlen_zero(tmp) && (exten = strsep(&tmp, "&"))) {
		if ((context = strchr(exten, '@')))
			*context++ = '\0';
		if (!ast_strlen_zero(context) && !strcasecmp(context, PICKUPMARK)) {
			if (!pickup_by_mark(chan, exten)) {
				/* Pickup successful.  Stop the dialplan this channel is a zombie. */
				return -1;
			}
		} else {
			if (!pickup_by_exten(chan, exten, !ast_strlen_zero(context) ? context : chan->context)) {
				/* Pickup successful.  Stop the dialplan this channel is a zombie. */
				return -1;
			}
		}
		ast_log(LOG_NOTICE, "No target channel found for %s.\n", exten);
	}

	/* Pickup failed.  Keep going in the dialplan. */
	return 0;
}

/* Find channel for pick up specified by partial channel name */ 
static int find_by_part(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	const char *part = data;
	int len = strlen(part);

	ast_channel_lock(target);
	if (len <= strlen(target->name) && !strncmp(target->name, part, len)
		&& ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
}

/* Attempt to pick up specified by partial channel name */ 
static int pickup_by_part(struct ast_channel *chan, const char *part)
{
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;

	/* The found channel is already locked. */
	target = ast_channel_callback(find_by_part, NULL, (char *) part, 0);
	if (target) {
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

/* application entry point for PickupChan() */
static int pickupchan_exec(struct ast_channel *chan, const char *data)
{
	int partial_pickup = 0;
	char *pickup = NULL;
	char *parse = ast_strdupa(data);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(options);
	);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.channel)) {
		ast_log(LOG_WARNING, "PickupChan requires an argument (channel)!\n");
		/* Pickup failed.  Keep going in the dialplan. */
		return 0;
	}

	if (!ast_strlen_zero(args.options) && strchr(args.options, 'p')) {
		partial_pickup = 1;
	}

	/* Parse channel */
	while (!ast_strlen_zero(args.channel) && (pickup = strsep(&args.channel, "&"))) {
		if (!strncasecmp(chan->name, pickup, strlen(pickup))) {
			ast_log(LOG_NOTICE, "Cannot pickup your own channel %s.\n", pickup);
		} else {
			if (partial_pickup) {
				if (!pickup_by_part(chan, pickup)) {
					/* Pickup successful.  Stop the dialplan this channel is a zombie. */
					return -1;
				}
			} else if (!pickup_by_channel(chan, pickup)) {
				/* Pickup successful.  Stop the dialplan this channel is a zombie. */
				return -1;
			}
			ast_log(LOG_NOTICE, "No target channel found for %s.\n", pickup);
		}
	}

	/* Pickup failed.  Keep going in the dialplan. */
	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app, pickup_exec);
	res |= ast_register_application_xml(app2, pickupchan_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Directed Call Pickup Application");
