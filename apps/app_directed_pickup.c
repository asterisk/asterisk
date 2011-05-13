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
		<syntax argsep="&amp;">
			<parameter name="channel" required="true" />
			<parameter name="channel2" multiple="true" />
		</syntax>
		<description>
			<para>This will pickup a specified <replaceable>channel</replaceable> if ringing.</para>
		</description>
	</application>
 ***/

static const char *app = "Pickup";
static const char *app2 = "PickupChan";
/*! \todo This application should return a result code, like PICKUPRESULT */

/* Helper function that determines whether a channel is capable of being picked up */
static int can_pickup(struct ast_channel *chan)
{
	if (!chan->pbx && !chan->masq &&
		!ast_test_flag(chan, AST_FLAG_ZOMBIE) &&
		(chan->_state == AST_STATE_RINGING ||
		 chan->_state == AST_STATE_RING ||
		 chan->_state == AST_STATE_DOWN)) {
		return 1;
	}
	return 0;
}

/*! \brief Helper Function to walk through ALL channels checking NAME and STATE */
static struct ast_channel *my_ast_get_channel_by_name_locked(const char *channame)
{
	struct ast_channel *chan;
	char *chkchan;
	size_t channame_len, chkchan_len;

	channame_len = strlen(channame);

	/* Check if channel name contains a '-'.
	 * In this case the channel name will be interpreted as full channel name.
	 */
	if (strchr(channame, '-')) {
		/* check full channel name */
		chkchan_len = channame_len;
		chkchan = (char *)channame;
	} else {
		/* need to append a '-' for the comparison so we check full channel name,
		 * i.e SIP/hgc- , use a temporary variable so original stays the same for
		 * debugging.
		 */
		chkchan_len = channame_len + 1;
		chkchan = alloca(chkchan_len + 1);
		strcpy(chkchan, channame);
		strcat(chkchan, "-");
	}

	for (chan = ast_walk_channel_by_name_prefix_locked(NULL, channame, channame_len);
		 chan;
		 chan = ast_walk_channel_by_name_prefix_locked(chan, channame, channame_len)) {
		if (!strncasecmp(chan->name, chkchan, chkchan_len) && can_pickup(chan)) {
			return chan;
		}
		ast_channel_unlock(chan);
	}
	return NULL;
}

/*! \brief Attempt to pick up specified channel named , does not use context */
static int pickup_by_channel(struct ast_channel *chan, char *pickup)
{
	int res = 0;
	struct ast_channel *target;

	if (!(target = my_ast_get_channel_by_name_locked(pickup)))
		return -1;

	/* Just check that we are not picking up the SAME as target */
	if (chan != target) {
		res = ast_do_pickup(chan, target);
	}
	ast_channel_unlock(target);

	return res;
}

struct pickup_criteria {
	const char *exten;
	const char *context;
	struct ast_channel *chan;
};

static int find_by_exten(struct ast_channel *c, void *data)
{
	struct pickup_criteria *info = data;

	return (!strcasecmp(c->macroexten, info->exten) || !strcasecmp(c->exten, info->exten)) &&
		!strcasecmp(c->dialcontext, info->context) &&
		(info->chan != c) && can_pickup(c);
}

/* Attempt to pick up specified extension with context */
static int pickup_by_exten(struct ast_channel *chan, const char *exten, const char *context)
{
	struct ast_channel *target = NULL;
	struct pickup_criteria search = {
		.exten = exten,
		.context = context,
		.chan = chan,
	};

	target = ast_channel_search_locked(find_by_exten, &search);

	if (target) {
		int res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		return res;
	}

	return -1;
}

static int find_by_mark(struct ast_channel *c, void *data)
{
	const char *mark = data;
	const char *tmp;

	return (tmp = pbx_builtin_getvar_helper(c, PICKUPMARK)) &&
		!strcasecmp(tmp, mark) &&
		can_pickup(c);
}

/* Attempt to pick up specified mark */
static int pickup_by_mark(struct ast_channel *chan, const char *mark)
{
	struct ast_channel *target = ast_channel_search_locked(find_by_mark, (char *) mark);

	if (target) {
		int res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		return res;
	}

	return -1;
}

static int find_by_group(struct ast_channel *c, void *data)
{
	struct ast_channel *chan = data;

	return (c != chan) && (chan->pickupgroup & c->callgroup) && can_pickup(c);
}

static int pickup_by_group(struct ast_channel *chan)
{
	struct ast_channel *target = ast_channel_search_locked(find_by_group, chan);

	if (target) {
		int res;

		ast_log(LOG_NOTICE, "%s, pickup attempt by %s\n", target->name, chan->name);
		res = ast_do_pickup(chan, target);
		ast_channel_unlock(target);
		return res;
	}

	return -1;
}

/* application entry point for Pickup() */
static int pickup_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *tmp = ast_strdupa(data);
	char *exten = NULL, *context = NULL;

	if (ast_strlen_zero(data)) {
		res = pickup_by_group(chan);
		return res;
	}

	/* Parse extension (and context if there) */
	while (!ast_strlen_zero(tmp) && (exten = strsep(&tmp, "&"))) {
		if ((context = strchr(exten, '@')))
			*context++ = '\0';
		if (!ast_strlen_zero(context) && !strcasecmp(context, PICKUPMARK)) {
			if (!pickup_by_mark(chan, exten))
				break;
		} else {
			if (!pickup_by_exten(chan, exten, !ast_strlen_zero(context) ? context : chan->context))
				break;
		}
		ast_log(LOG_NOTICE, "No target channel found for %s.\n", exten);
	}

	return res;
}

/* application entry point for PickupChan() */
static int pickupchan_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *tmp = ast_strdupa(data);
	char *pickup = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "PickupChan requires an argument (channel)!\n");
		return -1;	
	}

	/* Parse channel */
	while (!ast_strlen_zero(tmp) && (pickup = strsep(&tmp, "&"))) {
		if (!strncasecmp(chan->name, pickup, strlen(pickup))) {
			ast_log(LOG_NOTICE, "Cannot pickup your own channel %s.\n", pickup);
		} else {
			if (!pickup_by_channel(chan, pickup)) {
				break;
			}
			ast_log(LOG_NOTICE, "No target channel found for %s.\n", pickup);
		}
	}

	return res;
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
