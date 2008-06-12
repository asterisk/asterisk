/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Modified from app_zapbarge by David Troy <dave@toad.net>
 *
 * Special thanks to comphealth.com for sponsoring this
 * GPL application.
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
 * \brief DAHDI Scanner
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/dahdi.h"

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/say.h"
#include "asterisk/options.h"

static char *app = "DAHDIScan";
static char *deprecated_app = "ZapScan";

static char *synopsis = "Scan DAHDI channels to monitor calls";

static char *descrip =
"  DAHDIScan([group]) allows a call center manager to monitor DAHDI channels in\n"
"a convenient way.  Use '#' to select the next channel and use '*' to exit\n"
"Limit scanning to a channel GROUP by setting the option group argument.\n";


#define CONF_SIZE 160

static struct ast_channel *get_dahdi_channel_locked(int num) {
	char name[80];
	
	snprintf(name, sizeof(name), "%s/%d-1", dahdi_chan_name, num);
	return ast_get_channel_by_name_locked(name);
}

static int careful_write(int fd, unsigned char *data, int len)
{
	int res;
	while (len) {
		res = write(fd, data, len);
		if (res < 1) {
			if (errno != EAGAIN) {
				ast_log(LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
				return -1;
			} else {
				return 0;
			}
		}
		len -= res;
		data += res;
	}
	return 0;
}

static int conf_run(struct ast_channel *chan, int confno, int confflags)
{
	int fd;
	struct dahdi_confinfo dahdic;
	struct ast_frame *f;
	struct ast_channel *c;
	struct ast_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int flags;
	int retrydahdi;
	int origfd;
	int ret = -1;
	char input[4];
	int ic = 0;
	
	DAHDI_BUFFERINFO bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;
	
	/* Set it into U-law mode (write) */
	if (ast_set_write_format(chan, AST_FORMAT_ULAW) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to write ulaw mode\n", chan->name);
		goto outrun;
	}
	
	/* Set it into U-law mode (read) */
	if (ast_set_read_format(chan, AST_FORMAT_ULAW) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to read ulaw mode\n", chan->name);
		goto outrun;
	}
	ast_indicate(chan, -1);
	retrydahdi = strcasecmp(chan->tech->type, "DAHDI");
 dahdiretry:
	origfd = chan->fds[0];
	if (retrydahdi) {
		fd = open("/dev/dahdi/pseudo", O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		/* Make non-blocking */
		flags = fcntl(fd, F_GETFL);
		if (flags < 0) {
			ast_log(LOG_WARNING, "Unable to get flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
			ast_log(LOG_WARNING, "Unable to set flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE;
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = 4;
		if (ioctl(fd, DAHDI_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		nfds = 1;
	} else {
		/* XXX Make sure we're not running on a pseudo channel XXX */
		fd = chan->fds[0];
		nfds = 0;
	}
	memset(&dahdic, 0, sizeof(dahdic));
	/* Check to see if we're in a conference... */
	dahdic.chan = 0;
	if (ioctl(fd, DAHDI_GETCONF, &dahdic)) {
		ast_log(LOG_WARNING, "Error getting conference\n");
		close(fd);
		goto outrun;
	}
	if (dahdic.confmode) {
		/* Whoa, already in a conference...  Retry... */
		if (!retrydahdi) {
			ast_debug(1, "DAHDI channel is in a conference already, retrying with pseudo\n");
			retrydahdi = 1;
			goto dahdiretry;
		}
	}
	memset(&dahdic, 0, sizeof(dahdic));
	/* Add us to the conference */
	dahdic.chan = 0;
	dahdic.confno = confno;
	dahdic.confmode = DAHDI_CONF_MONITORBOTH;

	if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		goto outrun;
	}
	ast_debug(1, "Placed channel %s in DAHDI channel %d monitor\n", chan->name, confno);

	for (;;) {
		outfd = -1;
		ms = -1;
		c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
		if (c) {
			if (c->fds[0] != origfd) {
				if (retrydahdi) {
					/* Kill old pseudo */
					close(fd);
				}
				ast_debug(1, "Ooh, something swapped out under us, starting over\n");
				retrydahdi = 0;
				goto dahdiretry;
			}
			f = ast_read(c);
			if (!f) {
				break;
			}
			if (f->frametype == AST_FRAME_DTMF) {
				if (f->subclass == '#') {
					ret = 0;
					break;
				} else if (f->subclass == '*') {
					ret = -1;
					break;
				} else {
					input[ic++] = f->subclass;
				}
				if (ic == 3) {
					input[ic++] = '\0';
					ic = 0;
					ret = atoi(input);
					ast_verb(3, "DAHDIScan: change channel to %d\n", ret);
					break;
				}
			}

			if (fd != chan->fds[0]) {
				if (f->frametype == AST_FRAME_VOICE) {
					if (f->subclass == AST_FORMAT_ULAW) {
						/* Carefully write */
						careful_write(fd, f->data.ptr, f->datalen);
					} else {
						ast_log(LOG_WARNING, "Huh?  Got a non-ulaw (%d) frame in the conference\n", f->subclass);
					}
				}
			}
			ast_frfree(f);
		} else if (outfd > -1) {
			res = read(outfd, buf, CONF_SIZE);
			if (res > 0) {
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass = AST_FORMAT_ULAW;
				fr.datalen = res;
				fr.samples = res;
				fr.data.ptr = buf;
				fr.offset = AST_FRIENDLY_OFFSET;
				if (ast_write(chan, &fr) < 0) {
					ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
					/* break; */
				}
			} else {
				ast_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
			}
		}
	}
	if (f) {
		ast_frfree(f);
	}
	if (fd != chan->fds[0]) {
		close(fd);
	} else {
		/* Take out of conference */
		/* Add us to the conference */
		dahdic.chan = 0;
		dahdic.confno = 0;
		dahdic.confmode = 0;
		if (ioctl(fd, DAHDI_SETCONF, &dahdic)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
		}
	}

 outrun:

	return ret;
}

static int conf_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	int confflags = 0;
	int confno = 0;
	char confstr[80] = "", *tmp = NULL;
	struct ast_channel *tempchan = NULL, *lastchan = NULL, *ichan = NULL;
	struct ast_frame *f;
	char *desired_group;
	int input = 0, search_group = 0;

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	desired_group = ast_strdupa(data);
	if (!ast_strlen_zero(desired_group)) {
		ast_verb(3, "Scanning for group %s\n", desired_group);
		search_group = 1;
	}

	for (;;) {
		if (ast_waitfor(chan, 100) < 0)
			break;

		f = ast_read(chan);
		if (!f)
			break;
		if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '*')) {
			ast_frfree(f);
			break;
		}
		ast_frfree(f);
		ichan = NULL;
		if(input) {
			ichan = get_dahdi_channel_locked(input);
			input = 0;
		}

		tempchan = ichan ? ichan : ast_channel_walk_locked(tempchan);

		if (!tempchan && !lastchan) {
			break;
		}

		if (tempchan && search_group) {
			const char *mygroup;
			if ((mygroup = pbx_builtin_getvar_helper(tempchan, "GROUP")) && (!strcmp(mygroup, desired_group))) {
				ast_verb(3, "Found Matching Channel %s in group %s\n", tempchan->name, desired_group);
			} else {
				ast_channel_unlock(tempchan);
				lastchan = tempchan;
				continue;
			}
		}
		if (tempchan && (!strcmp(tempchan->tech->type, "DAHDI")) && (tempchan != chan)) {
			ast_verb(3, "DAHDI channel %s is in-use, monitoring...\n", tempchan->name);
			ast_copy_string(confstr, tempchan->name, sizeof(confstr));
			ast_channel_unlock(tempchan);
			if ((tmp = strchr(confstr, '-'))) {
				*tmp = '\0';
			}
			confno = atoi(strchr(confstr, '/') + 1);
			ast_stopstream(chan);
			ast_say_number(chan, confno, AST_DIGIT_ANY, chan->language, (char *) NULL);
			res = conf_run(chan, confno, confflags);
			if (res < 0) {
				break;
			}
			input = res;
		} else if (tempchan) {
			ast_channel_unlock(tempchan);
		}
		lastchan = tempchan;
	}
	return res;
}

static int conf_exec_warn(struct ast_channel *chan, void *data)
{
    ast_log(LOG_WARNING, "Use of the command %s is deprecated, please use %s instead.\n", deprecated_app, app);
    return conf_exec(chan, data);
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	ast_register_application(deprecated_app, conf_exec_warn, synopsis, descrip);
	return ((ast_register_application(app, conf_exec, synopsis, descrip)) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Scan DAHDI channels application");

