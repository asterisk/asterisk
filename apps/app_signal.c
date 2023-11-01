/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Channel signaling applications
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

/*** DOCUMENTATION
	<application name="Signal" language="en_US">
		<synopsis>
			Sends a signal to any waiting channels.
		</synopsis>
		<syntax>
			<parameter name="signalname" required="true">
				<para>Name of signal to send.</para>
			</parameter>
			<parameter name="payload" required="false">
				<para>Payload data to deliver.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sends a named signal to any channels that may be
			waiting for one. Acts as a producer in a simple
			message queue.</para>
			<variablelist>
				<variable name="SIGNALSTATUS">
					<value name="SUCCESS">
						Signal was successfully sent to at least
						one listener for processing.
					</value>
					<value name="FAILURE">
						Signal could not be sent or nobody
						was listening for this signal.
					</value>
				</variable>
			</variablelist>
			<example title="Send a signal named workdone">
			same => n,Signal(workdone,Work has completed)
			</example>
		</description>
		<see-also>
			<ref type="application">WaitForSignal</ref>
		</see-also>
	</application>
	<application name="WaitForSignal" language="en_US">
		<synopsis>
			Waits for a named signal on a channel.
		</synopsis>
		<syntax>
			<parameter name="signalname" required="true">
				<para>Name of signal to send.</para>
			</parameter>
			<parameter name="signaltimeout" required="false">
				<para>Maximum time, in seconds, to wait for signal.</para>
			</parameter>
		</syntax>
		<description>
			<para>Waits for <replaceable>signaltimeout</replaceable> seconds on the current
			channel to receive a signal with name <replaceable>signalname</replaceable>.
			Acts as a consumer in a simple message queue.</para>
			<para>Result of signal wait will be stored in the following variables:</para>
			<variablelist>
				<variable name="WAITFORSIGNALSTATUS">
					<value name="SIGNALED">
						Signal was received.
					</value>
					<value name="TIMEOUT">
						Timed out waiting for signal.
					</value>
					<value name="HANGUP">
						Channel hung up before signal was received.
					</value>
				</variable>
				<variable name="WAITFORSIGNALPAYLOAD">
					<para>Data payload attached to signal, if it exists</para>
				</variable>
			</variablelist>
			<example title="Wait for the workdone signal, indefinitely, and print out payload">
			same => n,WaitForSignal(workdone)
			same => n,NoOp(Received: ${WAITFORSIGNALPAYLOAD})
			</example>
		</description>
		<see-also>
			<ref type="application">Signal</ref>
		</see-also>
	</application>
 ***/

static const char * const app = "Signal";
static const char * const app2 = "WaitForSignal";

struct signalitem {
	ast_mutex_t lock;
	char name[AST_MAX_CONTEXT];
	int sig_alert_pipe[2];
	int watchers;
	unsigned int signaled:1;
	char *payload;
	AST_LIST_ENTRY(signalitem) entry;		/*!< Next Signal item */
};

static AST_RWLIST_HEAD_STATIC(signals, signalitem);

static struct signalitem *alloc_signal(const char *sname)
{
	struct signalitem *s;

	if (!(s = ast_calloc(1, sizeof(*s)))) {
		return NULL;
	}

	ast_mutex_init(&s->lock);
	ast_copy_string(s->name, sname, sizeof(s->name));

	s->sig_alert_pipe[0] = -1;
	s->sig_alert_pipe[1] = -1;
	s->watchers = 0;
	s->payload = NULL;
	ast_alertpipe_init(s->sig_alert_pipe);

	return s;
}

static int dealloc_signal(struct signalitem *s)
{
	if (s->watchers) { /* somebody is still using us... refuse to go away */
		ast_debug(1, "Signal '%s' is still being used by %d listener(s)\n", s->name, s->watchers);
		return -1;
	}
	ast_alertpipe_close(s->sig_alert_pipe);
	ast_mutex_destroy(&s->lock);
	if (s->payload) {
		ast_free(s->payload);
		s->payload = NULL;
	}
	ast_free(s);
	s = NULL;
	return 0;
}

static int remove_signal(char *sname)
{
	int res = -1;
	struct signalitem *s;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&signals, s, entry) {
		if (!strcmp(s->name, sname)) {
			AST_LIST_REMOVE_CURRENT(entry);
			res = dealloc_signal(s);
			ast_debug(1, "Removed signal '%s'\n", sname);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

static struct signalitem *get_signal(char *sname, int addnew)
{
	struct signalitem *s = NULL;
	AST_RWLIST_WRLOCK(&signals);
	AST_LIST_TRAVERSE(&signals, s, entry) {
		if (!strcasecmp(s->name, sname)) {
			ast_debug(1, "Using existing signal item '%s'\n", sname);
			break;
		}
	}
	if (!s) {
		if (addnew) { /* signal doesn't exist, so create it */
			s = alloc_signal(sname);
			/* Totally fail if we fail to find/create an entry */
			if (s) {
				ast_debug(1, "Created new signal item '%s'\n", sname);
				AST_RWLIST_INSERT_HEAD(&signals, s, entry);
			} else {
				ast_log(LOG_WARNING, "Failed to create signal item for '%s'\n", sname);
			}
		} else {
			ast_debug(1, "Signal '%s' doesn't exist, and not creating it\n", sname);
		}
	}
	AST_RWLIST_UNLOCK(&signals);
	return s;
}

static int wait_for_signal_or_hangup(struct ast_channel *chan, char *signame, int timeout)
{
	struct signalitem *s = NULL;
	int ms, remaining_time, res = 1, goaway = 0;
	struct timeval start;
	struct ast_frame *frame = NULL;

	remaining_time = timeout;
	start = ast_tvnow();

	s = get_signal(signame, 1);

	ast_mutex_lock(&s->lock);
	s->watchers = s->watchers + 1; /* we unlock, because a) other people need to use this and */
	ast_mutex_unlock(&s->lock); /* b) the signal will be available to us as long as watchers > 0 */

	while (timeout == 0 || remaining_time > 0) {
		int ofd, exception;

		ms = 1000;
		errno = 0;
		if (ast_waitfor_nandfds(&chan, 1, &s->sig_alert_pipe[0], 1, &exception, &ofd, &ms)) { /* channel won */
			if (!(frame = ast_read(chan))) { /* channel hung up */
				ast_debug(1, "Channel '%s' did not return a frame; probably hung up.\n", ast_channel_name(chan));
				res = -1;
				break;
			} else {
				ast_frfree(frame); /* handle frames */
			}
		} else if (ofd == s->sig_alert_pipe[0]) { /* fd won */
			if (ast_alertpipe_read(s->sig_alert_pipe) == AST_ALERT_READ_SUCCESS) {
				ast_debug(1, "Alert pipe has data for us\n");
				res = 0;
				break;
			} else {
				ast_debug(1, "Alert pipe does not have data for us\n");
			}
		} else { /* nobody won */
			if (ms && (ofd < 0)) {
				if (!((errno == 0) || (errno == EINTR))) {
					ast_log(LOG_WARNING, "Something bad happened while channel '%s' was polling.\n", ast_channel_name(chan));
					break;
				}
			} /* else, nothing happened */
		}
		if (timeout) {
			remaining_time = ast_remaining_ms(start, timeout);
		}
	}

	/* WRLOCK the list so that if we're going to destroy the signal now, nobody else can grab it before that happens. */
	AST_RWLIST_WRLOCK(&signals);
	ast_mutex_lock(&s->lock);
	if (s->payload) {
		pbx_builtin_setvar_helper(chan, "WAITFORSIGNALPAYLOAD", s->payload);
	}
	s->watchers = s->watchers - 1;
	if (s->watchers) { /* folks are still waiting for this, pass it on... */
		int save_errno = errno;
		if (ast_alertpipe_write(s->sig_alert_pipe)) {
			ast_log(LOG_WARNING, "%s: write() failed: %s\n", __FUNCTION__, strerror(errno));
		}
		errno = save_errno;
	} else { /* nobody else is waiting for this */
		goaway = 1; /* we were the last guy using this, so mark signal item for destruction */
	}
	ast_mutex_unlock(&s->lock);

	if (goaway) {
		/* remove_signal calls ast_mutex_destroy, so don't call it with the mutex itself locked. */
		remove_signal(signame);
	}
	AST_RWLIST_UNLOCK(&signals);

	return res;
}

static int send_signal(char *signame, char *payload)
{
	struct signalitem *s;
	int save_errno = errno;
	int res = 0;

	s = get_signal(signame, 0); /* if signal doesn't exist already, no point in creating it, because nobody could be waiting for it! */

	if (!s) {
		return -1; /* this signal didn't exist, so we can't send a signal for it */
	}

	/* at this point, we know someone is listening, since signals are destroyed when watchers gets down to 0 */
	ast_mutex_lock(&s->lock);
	s->signaled = 1;
	if (payload && *payload) {
		int len = strlen(payload);
		if (s->payload) {
			ast_free(s->payload); /* if there was already a payload, replace it */
			s->payload = NULL;
		}
		s->payload = ast_malloc(len + 1);
		if (!s->payload) {
			ast_log(LOG_WARNING, "Failed to allocate signal payload '%s'\n", payload);
		} else {
			ast_copy_string(s->payload, payload, len + 1);
		}
	}
	if (ast_alertpipe_write(s->sig_alert_pipe)) {
		ast_log(LOG_WARNING, "%s: write() failed: %s\n", __FUNCTION__, strerror(errno));
		s->signaled = 0; /* okay, so we didn't send a signal after all... */
		res = -1;
	}
	errno = save_errno;
	ast_debug(1, "Sent '%s' signal to %d listeners\n", signame, s->watchers);
	ast_mutex_unlock(&s->lock);

	return res;
}

static int waitsignal_exec(struct ast_channel *chan, const char *data)
{
	char *argcopy;
	int r = 0, timeoutms = 0;
	double timeout = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(signame);
		AST_APP_ARG(sigtimeout);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Signal() requires arguments\n");
		return -1;
	}

	argcopy = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, argcopy);

	if (ast_strlen_zero(args.signame)) {
		ast_log(LOG_WARNING, "Missing signal name\n");
		return -1;
	}
	if (strlen(args.signame) >= AST_MAX_CONTEXT) {
		ast_log(LOG_WARNING, "Signal name '%s' is too long\n", args.signame);
		return -1;
	}
	if (!ast_strlen_zero(args.sigtimeout)) {
		if (sscanf(args.sigtimeout, "%30lg", &timeout) != 1 || timeout < 0) {
			ast_log(LOG_WARNING, "Invalid timeout provided: %s. Defaulting to no timeout.\n", args.sigtimeout);
		} else {
			timeoutms = timeout * 1000; /* sec to msec */
		}
	}

	if (timeout > 0) {
		ast_debug(1, "Waiting for signal '%s' for %d ms\n", args.signame, timeoutms);
	} else {
		ast_debug(1, "Waiting for signal '%s', indefinitely\n", args.signame);
	}

	r = wait_for_signal_or_hangup(chan, args.signame, timeoutms);

	if (r == 1) {
		ast_verb(3, "Channel '%s' timed out, waiting for signal '%s'\n", ast_channel_name(chan), args.signame);
		pbx_builtin_setvar_helper(chan, "WAITFORSIGNALSTATUS", "TIMEOUT");
	} else if (!r) {
		ast_verb(3, "Received signal '%s' on channel '%s'\n", args.signame, ast_channel_name(chan));
		pbx_builtin_setvar_helper(chan, "WAITFORSIGNALSTATUS", "SIGNALED");
	} else {
		pbx_builtin_setvar_helper(chan, "WAITFORSIGNALSTATUS", "HANGUP");
		ast_verb(3, "Channel '%s' hung up\n", ast_channel_name(chan));
		return -1;
	}

	return 0;
}

static int signal_exec(struct ast_channel *chan, const char *data)
{
	char *argcopy;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(signame);
		AST_APP_ARG(payload);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Signal() requires arguments\n");
		return -1;
	}

	argcopy = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, argcopy);

	if (ast_strlen_zero(args.signame)) {
		ast_log(LOG_WARNING, "Missing signal name\n");
		return -1;
	}
	if (strlen(args.signame) >= AST_MAX_CONTEXT) {
		ast_log(LOG_WARNING, "Signal name '%s' is too long\n", args.signame);
		return -1;
	}

	if (send_signal(args.signame, args.payload)) {
		pbx_builtin_setvar_helper(chan, "SIGNALSTATUS", "FAILURE");
	} else {
		pbx_builtin_setvar_helper(chan, "SIGNALSTATUS", "SUCCESS");
	}

	return 0;
}

static int unload_module(void)
{
	struct signalitem *s;
	int res = 0;

	/* To avoid a locking nightmare, and for logistical reasons, this module
	 * will refuse to unload if watchers > 0. That way we know a signal's
	 * pipe won't disappear while it's being used. */

	AST_RWLIST_WRLOCK(&signals);
	/* Don't just use AST_RWLIST_REMOVE_HEAD, because if dealloc_signal fails, it should stay in the list. */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&signals, s, entry) {
		int mres = dealloc_signal(s);
		res |= mres;
		if (!mres) {
			AST_LIST_REMOVE_CURRENT(entry);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&signals);

	/* One or more signals still has watchers. */
	if (res) {
		ast_log(LOG_WARNING, "One or more signals is currently in use. Unload failed.\n");
		return res;
	}

	res |= ast_unregister_application(app);
	res |= ast_unregister_application(app2);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app, signal_exec);
	res |= ast_register_application_xml(app2, waitsignal_exec);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Channel Signaling Applications");
