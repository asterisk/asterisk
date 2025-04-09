/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, CyCore Systems, Inc
 *
 * Seán C McCord <scm@cycoresys.com>
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
 * \brief AudioSocket application -- transmit and receive audio through a TCP socket
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>res_audiosocket</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"
#include "errno.h"
#include <uuid/uuid.h>

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/res_audiosocket.h"
#include "asterisk/utils.h"
#include "asterisk/format_cache.h"

#define AST_MODULE "app_audiosocket"
#define AUDIOSOCKET_CONFIG "audiosocket.conf"
#define MAX_WAIT_TIMEOUT_MSEC 2000

/*** DOCUMENTATION
	<application name="AudioSocket" language="en_US">
		<since>
			<version>18.0.0</version>
		</since>
		<synopsis>
			Transmit and receive PCM audio between a channel and a TCP socket server.
		</synopsis>
		<syntax>
			<parameter name="uuid" required="true">
				<para>UUID is the universally-unique identifier of the call for the audio socket service.  This ID must conform to the string form of a standard UUID.</para>
			</parameter>
			<parameter name="service" required="true">
				<para>Service is the name or IP address and port number of the audio socket service to which this call should be connected.  This should be in the form host:port, such as myserver:9019. IPv6 addresses can be specified in square brackets, like [::1]:9019</para>
			</parameter>
		</syntax>
		<description>
			<para>Connects to the given TCP server, then transmits channel audio as 16-bit, 8KHz mono PCM over that socket (other codecs available via the channel driver interface). In turn, PCM audio is received from the socket and sent to the channel.  Only audio frames and DTMF frames will be transmitted.</para>
			<para>Protocol is specified at https://docs.asterisk.org/Configuration/Channel-Drivers/AudioSocket/</para>
			<para>This application does not automatically answer and should generally be preceded by an application such as Answer() or Progress().</para>
		</description>
	</application>
 ***/

static const char app[] = "AudioSocket";

static int audiosocket_run(struct ast_channel *chan, const char *id, const int svc, const char *server);

static int audiosocket_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	struct ast_format *readFormat, *writeFormat;
	const char *chanName;
	int res;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(idStr);
		AST_APP_ARG(server);
	);

	int s = 0;
	uuid_t uu;


	chanName = ast_channel_name(chan);

	/* Parse and validate arguments */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (ast_strlen_zero(args.idStr)) {
		ast_log(LOG_ERROR, "UUID is required\n");
		return -1;
	}
	if (uuid_parse(args.idStr, uu)) {
		ast_log(LOG_ERROR, "Failed to parse UUID '%s'\n", args.idStr);
		return -1;
	}
	if ((s = ast_audiosocket_connect(args.server, chan)) < 0) {
		/* The res module will already output a log message, so another is not needed */
		return -1;
	}

	/* Save current channel audio format and force to linear PCM. */

	writeFormat = ao2_bump(ast_channel_writeformat(chan));
	readFormat = ao2_bump(ast_channel_readformat(chan));

	if (ast_set_write_format(chan, ast_format_slin)) {
		ast_log(LOG_ERROR, "Failed to set write format to SLINEAR for channel '%s'\n", chanName);
		res = -1;
		goto end;
	}

	if (ast_set_read_format(chan, ast_format_slin)) {
		ast_log(LOG_ERROR, "Failed to set read format to SLINEAR for channel '%s'\n", chanName);
		res = -1;
		goto end;
	}

	/* Only a requested hangup or socket closure from the remote end will
	   return a 0 value (normal exit). All other events that disrupt an
	   active connection are treated as errors for now (non-zero). */

	res = audiosocket_run(chan, args.idStr, s, args.server);

end:

	/* Restore previous formats and close the connection */
	if (ast_set_write_format(chan, writeFormat)) {
		ast_log(LOG_ERROR, "Failed to restore write format for channel '%s'\n", chanName);
	}
	if (ast_set_read_format(chan, readFormat)) {
		ast_log(LOG_ERROR, "Failed to restore read format for channel '%s'\n", chanName);
	}
	ao2_ref(writeFormat, -1);
	ao2_ref(readFormat, -1);
	close(s);

	return res;
}

static int audiosocket_run(struct ast_channel *chan, const char *id, int svc, const char *server)
{
	const char *chanName;
	struct ast_channel *targetChan;
	int ms = MAX_WAIT_TIMEOUT_MSEC;
	int outfd = -1;
	struct ast_frame *f;
	int hangup;

	if (!chan || ast_channel_state(chan) != AST_STATE_UP) {
		ast_log(LOG_ERROR, "Channel is %s\n", chan ? "not answered" : "missing");
		return -1;
	}

	if (ast_audiosocket_init(svc, id)) {
		ast_log(LOG_ERROR, "Failed to initialize AudioSocket\n");
		return -1;
	}

	chanName = ast_channel_name(chan);

	while (1) {
		/* Timeout is hard-coded currently, could be made into an
		   argument if needed, but 2 seconds seems like a realistic
		   time range to give. */
		targetChan = ast_waitfor_nandfds(&chan, 1, &svc, 1, NULL, &outfd, &ms);
		ms = MAX_WAIT_TIMEOUT_MSEC;

		if (targetChan) {
			/* Receive frame from connected channel. */
			f = ast_read(chan);
			if (!f) {
				ast_log(LOG_WARNING, "Failed to receive frame from channel '%s' connected to AudioSocket server '%s'", chanName, server);
				return -1;
			}

			if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_DTMF) {
				/* Send audio frame or DTMF frame to audiosocket */
				if (ast_audiosocket_send_frame(svc, f)) {
					ast_log(LOG_WARNING, "Failed to forward frame from channel '%s' to AudioSocket server '%s'\n",
						chanName, server);
					ast_frfree(f);
					return -1;
				}
			}
			ast_frfree(f);

		} else if (outfd >= 0) {
			/* Receive audio frame from audiosocket. */
			f = ast_audiosocket_receive_frame_with_hangup(svc, &hangup);
			if (hangup) {
				/* Graceful termination, no frame to free. */
				return 0;
			}
			if (!f) {
				ast_log(LOG_WARNING, "Failed to receive frame from AudioSocket server '%s'"
					" connected to channel '%s'\n", server, chanName);
				return -1;
			}
			/* Send audio frame to connected channel. */
			if (ast_write(chan, f)) {
				ast_log(LOG_WARNING, "Failed to forward frame from AudioSocket server '%s' to channel '%s'\n", server, chanName);
				ast_frfree(f);
				return -1;
			}
			ast_frfree(f);

		} else {
			/* Neither the channel nor audio socket had activity
			   before timeout. Assume connection was lost. */
			ast_log(LOG_ERROR, "Reached timeout after %d ms of no activity on AudioSocket connection between '%s' and '%s'\n", MAX_WAIT_TIMEOUT_MSEC, chanName, server);
			return -1;
		}
	}
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, audiosocket_exec);
}

AST_MODULE_INFO(
	ASTERISK_GPL_KEY,
	AST_MODFLAG_LOAD_ORDER,
	"AudioSocket Application",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load =	load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.requires = "res_audiosocket",
);
