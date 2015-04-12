/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Digital Milliwatt Test
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="Milliwatt" language="en_US">
		<synopsis>
			Generate a Constant 1004Hz tone at 0dbm (mu-law).
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="o">
						<para>Generate the tone at 1000Hz like previous version.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Previous versions of this application generated the tone at 1000Hz.  If for
			some reason you would prefer that behavior, supply the <literal>o</literal> option to get the
			old behavior.</para>
		</description>
	</application>
 ***/

static const char app[] = "Milliwatt";

static const char digital_milliwatt[] = {0x1e,0x0b,0x0b,0x1e,0x9e,0x8b,0x8b,0x9e} ;

static void *milliwatt_alloc(struct ast_channel *chan, void *params)
{
	return ast_calloc(1, sizeof(int));
}

static void milliwatt_release(struct ast_channel *chan, void *data)
{
	ast_free(data);
	return;
}

static int milliwatt_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	unsigned char buf[AST_FRIENDLY_OFFSET + 640];
	const int maxsamples = ARRAY_LEN(buf) - (AST_FRIENDLY_OFFSET / sizeof(buf[0]));
	int i, *indexp = (int *) data, res;
	struct ast_frame wf = {
		.frametype = AST_FRAME_VOICE,
		.offset = AST_FRIENDLY_OFFSET,
		.src = __FUNCTION__,
	};

	wf.subclass.format = ast_format_ulaw;
	wf.data.ptr = buf + AST_FRIENDLY_OFFSET;

	/* Instead of len, use samples, because channel.c generator_force
	* generate(chan, tmp, 0, 160) ignores len. In any case, len is
	* a multiple of samples, given by number of samples times bytes per
	* sample. In the case of ulaw, len = samples. for signed linear
	* len = 2 * samples */
	if (samples > maxsamples) {
		ast_log(LOG_WARNING, "Only doing %d samples (%d requested)\n", maxsamples, samples);
		samples = maxsamples;
	}

	len = samples * sizeof (buf[0]);
	wf.datalen = len;
	wf.samples = samples;

	/* create a buffer containing the digital milliwatt pattern */
	for (i = 0; i < len; i++) {
		buf[AST_FRIENDLY_OFFSET + i] = digital_milliwatt[(*indexp)++];
		*indexp &= 7;
	}

	res = ast_write(chan, &wf);
	ast_frfree(&wf);

	if (res < 0) {
		ast_log(LOG_WARNING,"Failed to write frame to '%s': %s\n",ast_channel_name(chan),strerror(errno));
		return -1;
	}

	return 0;
}

static struct ast_generator milliwattgen = {
	.alloc = milliwatt_alloc,
	.release = milliwatt_release,
	.generate = milliwatt_generate,
};

static int old_milliwatt_exec(struct ast_channel *chan)
{
	ast_set_write_format(chan, ast_format_ulaw);
	ast_set_read_format(chan, ast_format_ulaw);

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ast_activate_generator(chan,&milliwattgen,"milliwatt") < 0) {
		ast_log(LOG_WARNING,"Failed to activate generator on '%s'\n",ast_channel_name(chan));
		return -1;
	}

	while (!ast_safe_sleep(chan, 10000))
		;

	ast_deactivate_generator(chan);

	return -1;
}

static int milliwatt_exec(struct ast_channel *chan, const char *data)
{
	const char *options = data;
	int res = -1;

	if (!ast_strlen_zero(options) && strchr(options, 'o')) {
		return old_milliwatt_exec(chan);
	}

	res = ast_playtones_start(chan, 23255, "1004/1000", 0);

	while (!res) {
		res = ast_safe_sleep(chan, 10000);
	}

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, milliwatt_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Digital Milliwatt (mu-law) Test Application");
