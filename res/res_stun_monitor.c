/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief STUN Network Monitor
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/event.h"
#include "asterisk/sched.h"
#include "asterisk/config.h"
#include "asterisk/stun.h"
#include "asterisk/netsock2.h"
#include "asterisk/lock.h"
#include <fcntl.h>

static const int DEFAULT_MONITOR_REFRESH = 30;

static const char stun_conf_file[] = "res_stun_monitor.conf";
static struct ast_sched_thread *sched;

static struct {
	struct sockaddr_in stunaddr;      /*!< The stun address we send requests to*/
	struct sockaddr_in externaladdr;  /*!< current perceived external address. */
	ast_mutex_t lock;
	unsigned int refresh;
	int stunsock;
	unsigned int monitor_enabled:1;
	unsigned int externaladdr_known:1;
} args;

static inline void stun_close_sock(void)
{
	if (args.stunsock != -1) {
		close(args.stunsock);
		args.stunsock = -1;
		memset(&args.externaladdr, 0, sizeof(args.externaladdr));
		args.externaladdr_known = 0;
	}
}

/* \brief purge the stun socket's receive buffer before issuing a new request
 *
 * XXX Note that this is somewhat of a hack.  This function is essentially doing
 * a cleanup on the socket rec buffer to handle removing any STUN responses we have not
 * handled.  This is called before sending out a new STUN request so we don't read
 * a latent previous response thinking it is new.
 */
static void stun_purge_socket(void)
{
	int flags = fcntl(args.stunsock, F_GETFL);
	int res = 0;
	unsigned char reply_buf[1024];

	fcntl(args.stunsock, F_SETFL, flags | O_NONBLOCK);
	while (res != -1) {
		/* throw away everything in the buffer until we reach the end. */
		res = recv(args.stunsock, reply_buf, sizeof(reply_buf), 0);
	}
	fcntl(args.stunsock, F_SETFL, flags & ~O_NONBLOCK);
}

/* \brief called by scheduler to send STUN request */
static int stun_monitor_request(const void *blarg)
{
	int res;
	int generate_event = 0;
	struct sockaddr_in answer = { 0, };


	/* once the stun socket goes away, this scheduler item will go away as well */
	ast_mutex_lock(&args.lock);
	if (args.stunsock == -1) {
		ast_log(LOG_ERROR, "STUN monitor: can not send STUN request, socket is not open\n");
		goto monitor_request_cleanup;
	}

	stun_purge_socket();

	if (!(ast_stun_request(args.stunsock, &args.stunaddr, NULL, &answer)) &&
		(memcmp(&args.externaladdr, &answer, sizeof(args.externaladdr)))) {
		const char *newaddr = ast_strdupa(ast_inet_ntoa(answer.sin_addr));
		int newport = ntohs(answer.sin_port);

		ast_log(LOG_NOTICE, "STUN MONITOR: Old external address/port %s:%d now seen as %s:%d \n",
			ast_inet_ntoa(args.externaladdr.sin_addr), ntohs(args.externaladdr.sin_port),
			newaddr, newport);

		memcpy(&args.externaladdr, &answer, sizeof(args.externaladdr));

		if (args.externaladdr_known) {
			/* the external address was already known, and has changed... generate event. */
			generate_event = 1;

		} else {
			/* this was the first external address we found, do not alert listeners
			 * until this address changes to something else. */
			args.externaladdr_known = 1;
		}
	}

	if (generate_event) {
		struct ast_event *event = ast_event_new(AST_EVENT_NETWORK_CHANGE, AST_EVENT_IE_END);
		if (!event) {
			ast_log(LOG_ERROR, "STUN monitor: could not create AST_EVENT_NETWORK_CHANGE event.\n");
			goto monitor_request_cleanup;
		}
		if (ast_event_queue(event)) {
			ast_event_destroy(event);
			event = NULL;
			ast_log(LOG_ERROR, "STUN monitor: could not queue AST_EVENT_NETWORK_CHANGE event.\n");
			goto monitor_request_cleanup;
		}
	}

monitor_request_cleanup:
	/* always refresh this scheduler item.  It will be removed elsewhere when
	 * it is supposed to go away */
	res = args.refresh * 1000;
	ast_mutex_unlock(&args.lock);

	return res;
}

/* \brief stops the stun monitor thread
 * \note do not hold the args->lock while calling this
 */
static void stun_stop_monitor(void)
{
	if (sched) {
		sched = ast_sched_thread_destroy(sched);
		ast_log(LOG_NOTICE, "STUN monitor stopped\n");
	}
	/* it is only safe to destroy the socket without holding arg->lock
	 * after the sched thread is destroyed */
	stun_close_sock();
}

/* \brief starts the stun monitor thread
 * \note The args->lock MUST be held when calling this function
 */
static int stun_start_monitor(void)
{
	struct ast_sockaddr dst;
	/* clean up any previous open socket */
	stun_close_sock();

	/* create destination ast_sockaddr */
	ast_sockaddr_from_sin(&dst, &args.stunaddr);

	/* open new socket binding */
	args.stunsock = socket(AF_INET, SOCK_DGRAM, 0);
	if (args.stunsock < 0) {
		ast_log(LOG_WARNING, "Unable to create STUN socket: %s\n", strerror(errno));
		return -1;
	}

	if (ast_connect(args.stunsock, &dst) != 0) {
		ast_log(LOG_WARNING, "SIP STUN Failed to connect to %s\n", ast_sockaddr_stringify(&dst));
		stun_close_sock();
		return -1;
	}

	/* if scheduler thread is not started, make sure to start it now */
	if (sched) {
		return 0; /* already started */
	}

	if (!(sched = ast_sched_thread_create())) {
		ast_log(LOG_ERROR, "Failed to create stun monitor scheduler thread\n");
		stun_close_sock();
		return -1;
	}

	if (ast_sched_thread_add_variable(sched, (args.refresh * 1000), stun_monitor_request, NULL, 1) < 0) {
		ast_log(LOG_ERROR, "Unable to schedule STUN network monitor \n");
		sched = ast_sched_thread_destroy(sched);
		stun_close_sock();
		return -1;
	}

	ast_log(LOG_NOTICE, "STUN monitor started\n");
	return 0;
}

static int load_config(int startup)
{
	struct ast_flags config_flags = { 0, };
	struct ast_config *cfg;
	struct ast_variable *v;

	if (!startup) {
		ast_set_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
	}

	if (!(cfg = ast_config_load2(stun_conf_file, "res_stun_monitor", config_flags)) ||
		cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", stun_conf_file);
		return -1;
	}

	if (cfg == CONFIG_STATUS_FILEUNCHANGED && !startup) {
		return 0;
	}

	/* set defaults */
	args.monitor_enabled = 0;
	memset(&args.stunaddr, 0, sizeof(args.stunaddr));
	args.refresh = DEFAULT_MONITOR_REFRESH;

	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, "stunaddr")) {
			args.stunaddr.sin_port = htons(STANDARD_STUN_PORT);
			if (ast_parse_arg(v->value, PARSE_INADDR, &args.stunaddr)) {
				ast_log(LOG_WARNING, "Invalid STUN server address: %s\n", v->value);
			} else {
				ast_log(LOG_NOTICE, "STUN monitor enabled: %s\n", v->value);
				args.monitor_enabled = 1;
			}
		} else if (!strcasecmp(v->name, "stunrefresh")) {
			if ((sscanf(v->value, "%30u", &args.refresh) != 1) || !args.refresh) {
				ast_log(LOG_WARNING, "Invalid stunrefresh value '%s', must be an integer > 0 at line %d\n", v->value, v->lineno);
				args.refresh = DEFAULT_MONITOR_REFRESH;
			} else {
				ast_log(LOG_NOTICE, "STUN Monitor set to refresh every %d seconds\n", args.refresh);
			}
		} else {
			ast_log(LOG_WARNING, "SIP STUN: invalid config option %s at line %d\n", v->value, v->lineno);
		}
	}

	ast_config_destroy(cfg);

	return 0;
}

static int __reload(int startup)
{
	int res;

	ast_mutex_lock(&args.lock);
	if (!(res = load_config(startup)) && args.monitor_enabled) {
		res = stun_start_monitor();
	}
	ast_mutex_unlock(&args.lock);

	if ((res == -1) || !args.monitor_enabled) {
		args.monitor_enabled = 0;
		stun_stop_monitor();
	}

	return res;
}

static int reload(void)
{
	return __reload(0);
}

static int unload_module(void)
{
	stun_stop_monitor();
	ast_mutex_destroy(&args.lock);
	return 0;
}

static int load_module(void)
{
	ast_mutex_init(&args.lock);
	args.stunsock = -1;
	memset(&args.externaladdr, 0, sizeof(args.externaladdr));
	args.externaladdr_known = 0;
	sched = NULL;
	if (__reload(1)) {
		stun_stop_monitor();
		ast_mutex_destroy(&args.lock);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "STUN Network Monitor",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND
	);
