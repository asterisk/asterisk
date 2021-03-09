/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2018, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

/*!
 * \file
 *
 * \brief REMB Modifier Module
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/channel.h"
#include "asterisk/framehook.h"
#include "asterisk/rtp_engine.h"

struct remb_values {
	/*! \brief The amount of bitrate to use for REMB received from the channel */
	float receive_bitrate;
	/*! \brief The amount of bitrate to use for REMB sent to the channel */
	float send_bitrate;
};

static void remb_values_free(void *data)
{
	ast_free(data);
}

static const struct ast_datastore_info remb_info = {
	.type = "REMB Values",
	.destroy = remb_values_free,
};

static struct ast_frame *remb_hook_event_cb(struct ast_channel *chan, struct ast_frame *frame, enum ast_framehook_event event, void *data)
{
	struct ast_rtp_rtcp_feedback *feedback;
	struct ast_datastore *remb_store;
	struct remb_values *remb_values;
	int exp;
	float bitrate = 0.0;

	if (!frame) {
		return NULL;
	}

	switch (event) {
	case AST_FRAMEHOOK_EVENT_READ:
	case AST_FRAMEHOOK_EVENT_WRITE:
		break;
	case AST_FRAMEHOOK_EVENT_ATTACHED:
	case AST_FRAMEHOOK_EVENT_DETACHED:
		return frame;
	}

	/* We only care about REMB frames, all others will be unmodified */
	if (frame->subclass.integer != AST_RTP_RTCP_PSFB) {
		return frame;
	}

	feedback = frame->data.ptr;
	if (feedback->fmt != AST_RTP_RTCP_FMT_REMB) {
		return frame;
	}

	remb_store = ast_channel_datastore_find(chan, &remb_info, NULL);
	if (!remb_store) {
		return frame;
	}
	remb_values = remb_store->data;

	/* If a bitrate override has been set apply it to the REMB Frame */
	if (event == AST_FRAMEHOOK_EVENT_READ && remb_values->receive_bitrate) {
		bitrate = remb_values->receive_bitrate;
	} else if (event == AST_FRAMEHOOK_EVENT_WRITE && remb_values->send_bitrate) {
		bitrate = remb_values->send_bitrate;
	} else {
		return frame;
	}

	/*
	 * The mantissa only has 18 bits available, so make sure it fits. Adjust the
	 * value and exponent for those values that don't.
	 *
	 * For example given the following:
	 *
	 * bitrate = 123456789.0
	 * frexp(bitrate, &exp);
	 *
	 * 'exp' should now equal 27 (number of bits needed to represent the value). Since
	 * the mantissa must fit into an 18-bit unsigned integer, and the given bitrate is
	 * too large to fit, we must subtract 18 from the exponent in order to get the
	 * number of times the bitrate will fit into that size integer.
	 *
	 * exp -= 18;
	 *
	 * 'exp' is now equal to 9. Now we can get the mantissa that fits into an 18-bit
	 * unsigned integer by dividing the bitrate by 2^exp:
	 *
	 * mantissa = 123456789.0 / 2^9
	 *
	 * This makes the final mantissa equal to 241126 (implicitly cast), which is less
	 * than 262143 (the max value that can be put into an unsigned 18-bit integer).
	 * So now we have the following:
	 *
	 * exp = 9;
	 * mantissa = 241126;
	 *
	 * If we multiply that back we should come up with something close to the original
	 * bit rate:
	 *
	 * 241126 * 2^9 = 123456512
	 *
	 * Precision is lost due to the nature of floating point values. Easier to why from
	 * the binary:
	 *
	 * 241126 * 2^9 = 241126 << 9 = 111010110111100110 << 9 = 111010110111100110000000000
	 *
	 * Precision on the "lower" end is lost due to zeros being shifted in. This loss is
	 * both expected and acceptable.
	 */
	frexp(bitrate, &exp);
	exp = exp > 18 ? exp - 18 : 0;

	feedback->remb.br_mantissa = bitrate / (1 << exp);
	feedback->remb.br_exp = exp;

	return frame;
}

static char *handle_remb_set(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	unsigned int bitrate;
	struct ast_datastore *remb_store;
	struct remb_values *remb_values;
	struct ast_framehook_interface interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = remb_hook_event_cb,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "remb set {send|receive}";
		e->usage =
			"Usage: remb set {send|receive} <channel> <bitrate in bits>\n"
			"       Set the REMB value which overwrites what we send or receive\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[4], "%30d", &bitrate) != 1) {
		ast_cli(a->fd, "%s is not a valid bitrate in bits\n", a->argv[4]);
		return CLI_SUCCESS;
	} else if (strcasecmp(a->argv[2], "send") && strcasecmp(a->argv[2], "receive")) {
		ast_cli(a->fd, "%s is not a valid direction for REMB\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	chan = ast_channel_get_by_name(a->argv[3]);
	if (!chan) {
		ast_cli(a->fd, "%s is not a known channel\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_channel_lock(chan);

	remb_store = ast_channel_datastore_find(chan, &remb_info, NULL);
	if (!remb_store) {
		int framehook_id;

		framehook_id = ast_framehook_attach(chan, &interface);
		if (framehook_id < 0) {
			ast_cli(a->fd, "Could not attach framehook for modifying REMB\n");
			ast_channel_unlock(chan);
			ast_channel_unref(chan);
			return CLI_SUCCESS;
		}

		remb_values = ast_calloc(1, sizeof(*remb_values));
		if (!remb_values) {
			ast_cli(a->fd, "Could not create a place to store provided REMB value\n");
			ast_framehook_detach(chan, framehook_id);
			ast_channel_unlock(chan);
			ast_channel_unref(chan);
			return CLI_SUCCESS;
		}

		remb_store = ast_datastore_alloc(&remb_info, NULL);
		if (!remb_store) {
			ast_cli(a->fd, "Could not create a place to store provided REMB value\n");
			ast_framehook_detach(chan, framehook_id);
			ast_channel_unlock(chan);
			ast_channel_unref(chan);
			ast_free(remb_values);
			return CLI_SUCCESS;
		}

		remb_store->data = remb_values;
		ast_channel_datastore_add(chan, remb_store);
	} else {
		remb_values = remb_store->data;
	}

	if (!strcasecmp(a->argv[2], "send")) {
		remb_values->send_bitrate = bitrate;
	} else if (!strcasecmp(a->argv[2], "receive")) {
		remb_values->receive_bitrate = bitrate;
	}

	ast_channel_unlock(chan);
	ast_channel_unref(chan);

	ast_cli(a->fd, "Set REMB %s override to a bitrate of %s on %s\n", a->argv[2], a->argv[3], a->argv[4]);

	return CLI_SUCCESS;
}

static struct ast_cli_entry remb_cli[] = {
	AST_CLI_DEFINE(handle_remb_set, "Set the REMB value which overwrites what is sent or received"),
};

static int load_module(void)
{
	ast_cli_register_multiple(remb_cli, ARRAY_LEN(remb_cli));
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(remb_cli, ARRAY_LEN(remb_cli));
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "REMB Modifier Module",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
);
