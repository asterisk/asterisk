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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/pickup.h"
#include "asterisk/manager.h"
#include "asterisk/callerid.h"

#define PICKUPMARK "PICKUPMARK"

/*** DOCUMENTATION
	<application name="Pickup" language="en_US">
		<synopsis>
			Directed extension call pickup.
		</synopsis>
		<syntax>
			<parameter name="targets" argsep="&amp;">
				<argument name="extension" argsep="@" required="true">
					<para>Specification of the pickup target.</para>
					<argument name="extension" required="true"/>
					<argument name="context" />
				</argument>
				<argument name="extension2" argsep="@" multiple="true">
					<para>Additional specifications of pickup targets.</para>
					<argument name="extension2" required="true"/>
					<argument name="context2"/>
				</argument>
			</parameter>
		</syntax>
		<description>
			<para>This application can pickup a specified ringing channel.  The channel
			to pickup can be specified in the following ways.</para>
			<para>1) If no <replaceable>extension</replaceable> targets are specified,
			the application will pickup a channel matching the pickup group of the
			requesting channel.</para>
			<para>2) If the <replaceable>extension</replaceable> is specified with a
			<replaceable>context</replaceable> of the special string
			<literal>PICKUPMARK</literal> (for example 10@PICKUPMARK), the application
			will pickup a channel which has defined the channel variable
			<variable>PICKUPMARK</variable> with the same value as
			<replaceable>extension</replaceable> (in this example,
			<literal>10</literal>).</para>
			<para>3) If the <replaceable>extension</replaceable> is specified
			with or without a <replaceable>context</replaceable>, the channel with a
			matching <replaceable>extension</replaceable> and <replaceable>context</replaceable>
			will be picked up.  If no <replaceable>context</replaceable> is specified,
			the current context will be used.</para>
			<note><para>The <replaceable>extension</replaceable> is typically set on
			matching channels by the dial application that created the channel.  The
			<replaceable>context</replaceable> is set on matching channels by the
			channel driver for the device.</para></note>
		</description>
	</application>
	<application name="PickupChan" language="en_US">
		<synopsis>
			Pickup a ringing channel.
		</synopsis>
		<syntax >
			<parameter name="channel" argsep="&amp;" required="true">
				<argument name="channel" required="true" />
				<argument name="channel2" required="false" multiple="true" />
				<para>List of channel names or channel uniqueids to pickup if ringing.
					For example, a channel name could be <literal>SIP/bob</literal> or
					<literal>SIP/bob-00000000</literal> to find
					<literal>SIP/bob-00000000</literal>.
				</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="p">
						<para>Supplied channel names are prefixes.  For example,
							<literal>SIP/bob</literal> will match
							<literal>SIP/bob-00000000</literal> and
							<literal>SIP/bobby-00000000</literal>.
						</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Pickup a specified <replaceable>channel</replaceable> if ringing.</para>
		</description>
	</application>
 ***/

static const char app[] = "Pickup";
static const char app2[] = "PickupChan";

struct pickup_by_name_args {
	/*! Channel attempting to pickup a call. */
	struct ast_channel *chan;
	/*! Channel uniqueid or partial channel name to match. */
	const char *name;
	/*! Length of partial channel name to match. */
	size_t len;
};

static int find_by_name(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	struct pickup_by_name_args *args = data;

	if (args->chan == target) {
		/* The channel attempting to pickup a call cannot pickup itself. */
		return 0;
	}

	ast_channel_lock(target);
	if (!strncasecmp(ast_channel_name(target), args->name, args->len)
		&& ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
}

static int find_by_uniqueid(void *obj, void *arg, void *data, int flags)
{
	struct ast_channel *target = obj;/*!< Potential pickup target */
	struct pickup_by_name_args *args = data;

	if (args->chan == target) {
		/* The channel attempting to pickup a call cannot pickup itself. */
		return 0;
	}

	ast_channel_lock(target);
	if (!strcasecmp(ast_channel_uniqueid(target), args->name)
		&& ast_can_pickup(target)) {
		/* Return with the channel still locked on purpose */
		return CMP_MATCH | CMP_STOP;
	}
	ast_channel_unlock(target);

	return 0;
}

/*! \brief Helper Function to walk through ALL channels checking NAME and STATE */
static struct ast_channel *find_by_channel(struct ast_channel *chan, const char *channame)
{
	struct ast_channel *target;
	char *chkchan;
	struct pickup_by_name_args pickup_args;

	pickup_args.chan = chan;

	if (strchr(channame, '-')) {
		/*
		 * Use the given channel name string as-is.  This allows a full channel
		 * name with a typical sequence number to be used as well as still
		 * allowing the odd partial channel name that has a '-' in it to still
		 * work, i.e. Local/bob@en-phone.
		 */
		pickup_args.len = strlen(channame);
		pickup_args.name = channame;
	} else {
		/*
		 * Append a '-' for the comparison so we check the channel name less
		 * a sequence number, i.e Find SIP/bob- and not SIP/bobby.
		 */
		pickup_args.len = strlen(channame) + 1;
		chkchan = ast_alloca(pickup_args.len + 1);
		strcpy(chkchan, channame);/* Safe */
		strcat(chkchan, "-");
		pickup_args.name = chkchan;
	}
	target = ast_channel_callback(find_by_name, NULL, &pickup_args, 0);
	if (target) {
		return target;
	}

	/* Now try a search for uniqueid. */
	pickup_args.name = channame;
	pickup_args.len = 0;
	return ast_channel_callback(find_by_uniqueid, NULL, &pickup_args, 0);
}

/*! \brief Attempt to pick up named channel. */
static int pickup_by_channel(struct ast_channel *chan, const char *name)
{
	int res = -1;
	struct ast_channel *target;/*!< Potential pickup target */

	/* The found channel is already locked. */
	target = find_by_channel(chan, name);
	if (target) {
		res = ast_do_pickup(chan, target);
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
			ast_log(LOG_NOTICE, "%s pickup by %s\n", ast_channel_name(target), ast_channel_name(chan));
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
	struct ast_channel *chan = arg;
	const char *mark = data;
	const char *tmp;

	if (chan == target) {
		/* The channel attempting to pickup a call cannot pickup itself. */
		return 0;
	}

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
	target = ast_channel_callback(find_by_mark, chan, (char *) mark, 0);
	if (target) {
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

static int pickup_by_group(struct ast_channel *chan)
{
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;

	/* The found channel is already locked. */
	target = ast_pickup_find_by_group(chan);
	if (target) {
		ast_log(LOG_NOTICE, "pickup %s attempt by %s\n", ast_channel_name(target), ast_channel_name(chan));
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

/* application entry point for Pickup() */
static int pickup_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	char *exten;
	char *context;

	if (ast_strlen_zero(data)) {
		return pickup_by_group(chan) ? 0 : -1;
	}

	/* Parse extension (and context if there) */
	parse = ast_strdupa(data);
	for (;;) {
		if (ast_strlen_zero(parse)) {
			break;
		}
		exten = strsep(&parse, "&");
		if (ast_strlen_zero(exten)) {
			continue;
		}

		context = strchr(exten, '@');
		if (context) {
			*context++ = '\0';
		}
		if (!ast_strlen_zero(context) && !strcasecmp(context, PICKUPMARK)) {
			if (!pickup_by_mark(chan, exten)) {
				/* Pickup successful.  Stop the dialplan this channel is a zombie. */
				return -1;
			}
		} else {
			if (ast_strlen_zero(context)) {
				context = (char *) ast_channel_context(chan);
			}
			if (!pickup_by_exten(chan, exten, context)) {
				/* Pickup successful.  Stop the dialplan this channel is a zombie. */
				return -1;
			}
		}
		ast_log(LOG_NOTICE, "No target channel found for %s@%s.\n", exten, context);
	}

	/* Pickup failed.  Keep going in the dialplan. */
	return 0;
}

/* Find channel for pick up specified by partial channel name */
static struct ast_channel *find_by_part(struct ast_channel *chan, const char *part)
{
	struct ast_channel *target;
	struct pickup_by_name_args pickup_args;

	pickup_args.chan = chan;

	/* Try a partial channel name search. */
	pickup_args.name = part;
	pickup_args.len = strlen(part);
	target = ast_channel_callback(find_by_name, NULL, &pickup_args, 0);
	if (target) {
		return target;
	}

	/* Now try a search for uniqueid. */
	pickup_args.name = part;
	pickup_args.len = 0;
	return ast_channel_callback(find_by_uniqueid, NULL, &pickup_args, 0);
}

/* Attempt to pick up specified by partial channel name */
static int pickup_by_part(struct ast_channel *chan, const char *part)
{
	struct ast_channel *target;/*!< Potential pickup target */
	int res = -1;

	/* The found channel is already locked. */
	target = find_by_part(chan, part);
	if (target) {
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		target = ast_channel_unref(target);
	}

	return res;
}

enum OPT_PICKUPCHAN_FLAGS {
	OPT_PICKUPCHAN_PARTIAL =   (1 << 0),	/* Channel name is a partial name. */
};

AST_APP_OPTIONS(pickupchan_opts, BEGIN_OPTIONS
	AST_APP_OPTION('p', OPT_PICKUPCHAN_PARTIAL),
END_OPTIONS);

/* application entry point for PickupChan() */
static int pickupchan_exec(struct ast_channel *chan, const char *data)
{
	char *pickup = NULL;
	char *parse = ast_strdupa(data);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(options);
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);
	struct ast_flags opts;

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.channel)) {
		ast_log(LOG_WARNING, "PickupChan requires an argument (channel)!\n");
		/* Pickup failed.  Keep going in the dialplan. */
		return 0;
	}
	if (ast_app_parse_options(pickupchan_opts, &opts, NULL, args.options)) {
		/*
		 * General invalid option syntax.
		 * Pickup failed.  Keep going in the dialplan.
		 */
		return 0;
	}

	/* Parse channel */
	for (;;) {
		if (ast_strlen_zero(args.channel)) {
			break;
		}
		pickup = strsep(&args.channel, "&");
		if (ast_strlen_zero(pickup)) {
			continue;
		}

		if (ast_test_flag(&opts, OPT_PICKUPCHAN_PARTIAL)) {
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
