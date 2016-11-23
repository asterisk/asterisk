/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Russell Bryant <russelb@clemson.edu>
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
 * \brief Applications to test connection and produce report in text file
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Russell Bryant <russelb@clemson.edu>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="TestServer" language="en_US">
		<synopsis>
			Execute Interface Test Server.
		</synopsis>
		<syntax />
		<description>
			<para>Perform test server function and write call report. Results stored in
			<filename>/var/log/asterisk/testreports/&lt;testid&gt;-server.txt</filename></para>
		</description>
		<see-also>
			<ref type="application">TestClient</ref>
		</see-also>
	</application>
	<application name="TestClient" language="en_US">
		<synopsis>
			Execute Interface Test Client.
		</synopsis>
		<syntax>
			<parameter name="testid" required="true">
				<para>An ID to identify this test.</para>
			</parameter>
		</syntax>
		<description>
			<para>Executes test client with given <replaceable>testid</replaceable>. Results stored in
			<filename>/var/log/asterisk/testreports/&lt;testid&gt;-client.txt</filename></para>
		</description>
		<see-also>
			<ref type="application">TestServer</ref>
		</see-also>
	</application>
 ***/

static char *tests_app = "TestServer";
static char *testc_app = "TestClient";

static int measurenoise(struct ast_channel *chan, int ms, char *who)
{
	int res=0;
	int mssofar;
	int noise=0;
	int samples=0;
	int x;
	short *foo;
	struct timeval start;
	struct ast_frame *f;
	struct ast_format *rformat;

	rformat = ao2_bump(ast_channel_readformat(chan));
	if (ast_set_read_format(chan, ast_format_slin)) {
		ast_log(LOG_NOTICE, "Unable to set to linear mode!\n");
		ao2_cleanup(rformat);
		return -1;
	}
	start = ast_tvnow();
	for(;;) {
		mssofar = ast_tvdiff_ms(ast_tvnow(), start);
		if (mssofar > ms)
			break;
		res = ast_waitfor(chan, ms - mssofar);
		if (res < 1)
			break;
		f = ast_read(chan);
		if (!f) {
			res = -1;
			break;
		}
		if ((f->frametype == AST_FRAME_VOICE) &&
			(ast_format_cmp(f->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)) {
			foo = (short *)f->data.ptr;
			for (x=0;x<f->samples;x++) {
				noise += abs(foo[x]);
				samples++;
			}
		}
		ast_frfree(f);
	}

	if (rformat) {
		if (ast_set_read_format(chan, rformat)) {
			ast_log(LOG_NOTICE, "Unable to restore original format!\n");
			ao2_ref(rformat, -1);
			return -1;
		}
		ao2_ref(rformat, -1);
	}
	if (res < 0)
		return res;
	if (!samples) {
		ast_log(LOG_NOTICE, "No samples were received from the other side!\n");
		return -1;
	}
	ast_debug(1, "%s: Noise: %d, samples: %d, avg: %d\n", who, noise, samples, noise / samples);
	return (noise / samples);
}

static int sendnoise(struct ast_channel *chan, int ms)
{
	int res;
	res = ast_tonepair_start(chan, 1537, 2195, ms, 8192);
	if (!res) {
		res = ast_waitfordigit(chan, ms);
		ast_tonepair_stop(chan);
	}
	return res;
}

static int testclient_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	const char *testid=data;
	char fn[80];
	char serverver[80];
	FILE *f;

	/* Check for test id */
	if (ast_strlen_zero(testid)) {
		ast_log(LOG_WARNING, "TestClient requires an argument - the test id\n");
		return -1;
	}

	if (ast_channel_state(chan) != AST_STATE_UP)
		res = ast_answer(chan);

	/* Wait a few just to be sure things get started */
	res = ast_safe_sleep(chan, 3000);
	/* Transmit client version */
	if (!res)
		res = ast_dtmf_stream(chan, NULL, "8378*1#", 0, 0);
	ast_debug(1, "Transmit client version\n");

	/* Read server version */
	ast_debug(1, "Read server version\n");
	if (!res)
		res = ast_app_getdata(chan, NULL, serverver, sizeof(serverver) - 1, 0);
	if (res > 0)
		res = 0;
	ast_debug(1, "server version: %s\n", serverver);

	if (res > 0)
		res = 0;

	if (!res)
		res = ast_safe_sleep(chan, 1000);
	/* Send test id */
	if (!res)
		res = ast_dtmf_stream(chan, NULL, testid, 0, 0);
	if (!res)
		res = ast_dtmf_stream(chan, NULL, "#", 0, 0);
	ast_debug(1, "send test identifier: %s\n", testid);

	if ((res >=0) && (!ast_strlen_zero(testid))) {
		/* Make the directory to hold the test results in case it's not there */
		snprintf(fn, sizeof(fn), "%s/testresults", ast_config_AST_LOG_DIR);
		ast_mkdir(fn, 0777);
		snprintf(fn, sizeof(fn), "%s/testresults/%s-client.txt", ast_config_AST_LOG_DIR, testid);
		if ((f = fopen(fn, "w+"))) {
			setlinebuf(f);
			fprintf(f, "CLIENTCHAN:    %s\n", ast_channel_name(chan));
			fprintf(f, "CLIENTTEST ID: %s\n", testid);
			fprintf(f, "ANSWER:        PASS\n");
			res = 0;

			if (!res) {
				/* Step 1: Wait for "1" */
				ast_debug(1, "TestClient: 2.  Wait DTMF 1\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 1:   %s\n", (res != '1') ? "FAIL" : "PASS");
				if (res == '1')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				res = ast_safe_sleep(chan, 1000);
			}
			if (!res) {
				/* Step 2: Send "2" */
				ast_debug(1, "TestClient: 2.  Send DTMF 2\n");
				res = ast_dtmf_stream(chan, NULL, "2", 0, 0);
				fprintf(f, "SEND DTMF 2:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 3: Wait one second */
				ast_debug(1, "TestClient: 3.  Wait one second\n");
				res = ast_safe_sleep(chan, 1000);
				fprintf(f, "WAIT 1 SEC:    %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 4: Measure noise */
				ast_debug(1, "TestClient: 4.  Measure noise\n");
				res = measurenoise(chan, 5000, "TestClient");
				fprintf(f, "MEASURENOISE:  %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 5: Wait for "4" */
				ast_debug(1, "TestClient: 5.  Wait DTMF 4\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 4:   %s\n", (res != '4') ? "FAIL" : "PASS");
				if (res == '4')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				/* Step 6: Transmit tone noise */
				ast_debug(1, "TestClient: 6.  Transmit tone\n");
				res = sendnoise(chan, 6000);
				fprintf(f, "SENDTONE:      %s\n", (res < 0) ? "FAIL" : "PASS");
			}
			if (!res || (res == '5')) {
				/* Step 7: Wait for "5" */
				ast_debug(1, "TestClient: 7.  Wait DTMF 5\n");
				if (!res)
					res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 5:   %s\n", (res != '5') ? "FAIL" : "PASS");
				if (res == '5')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				/* Step 8: Wait one second */
				ast_debug(1, "TestClient: 8.  Wait one second\n");
				res = ast_safe_sleep(chan, 1000);
				fprintf(f, "WAIT 1 SEC:    %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 9: Measure noise */
				ast_debug(1, "TestClient: 9.  Measure tone\n");
				res = measurenoise(chan, 4000, "TestClient");
				fprintf(f, "MEASURETONE:   %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 10: Send "7" */
				ast_debug(1, "TestClient: 10.  Send DTMF 7\n");
				res = ast_dtmf_stream(chan, NULL, "7", 0, 0);
				fprintf(f, "SEND DTMF 7:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res =0;
			}
			if (!res) {
				/* Step 11: Wait for "8" */
				ast_debug(1, "TestClient: 11.  Wait DTMF 8\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 8:   %s\n", (res != '8') ? "FAIL" : "PASS");
				if (res == '8')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				res = ast_safe_sleep(chan, 1000);
			}
			if (!res) {
				/* Step 12: Hangup! */
				ast_debug(1, "TestClient: 12.  Hangup\n");
			}

			ast_debug(1, "-- TEST COMPLETE--\n");
			fprintf(f, "-- END TEST--\n");
			fclose(f);
			res = -1;
		} else
			res = -1;
	} else {
		ast_log(LOG_NOTICE, "Did not read a test ID on '%s'\n", ast_channel_name(chan));
		res = -1;
	}
	return res;
}

static int testserver_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char testid[80]="";
	char fn[80];
	FILE *f;
	if (ast_channel_state(chan) != AST_STATE_UP)
		res = ast_answer(chan);
	/* Read version */
	ast_debug(1, "Read client version\n");
	if (!res)
		res = ast_app_getdata(chan, NULL, testid, sizeof(testid) - 1, 0);
	if (res > 0)
		res = 0;

	ast_debug(1, "client version: %s\n", testid);
	ast_debug(1, "Transmit server version\n");

	res = ast_safe_sleep(chan, 1000);
	if (!res)
		res = ast_dtmf_stream(chan, NULL, "8378*1#", 0, 0);
	if (res > 0)
		res = 0;

	if (!res)
		res = ast_app_getdata(chan, NULL, testid, sizeof(testid) - 1, 0);
	ast_debug(1, "read test identifier: %s\n", testid);
	/* Check for sneakyness */
	if (strchr(testid, '/'))
		res = -1;
	if ((res >=0) && (!ast_strlen_zero(testid))) {
		/* Got a Test ID!  Whoo hoo! */
		/* Make the directory to hold the test results in case it's not there */
		snprintf(fn, sizeof(fn), "%s/testresults", ast_config_AST_LOG_DIR);
		ast_mkdir(fn, 0777);
		snprintf(fn, sizeof(fn), "%s/testresults/%s-server.txt", ast_config_AST_LOG_DIR, testid);
		if ((f = fopen(fn, "w+"))) {
			setlinebuf(f);
			fprintf(f, "SERVERCHAN:    %s\n", ast_channel_name(chan));
			fprintf(f, "SERVERTEST ID: %s\n", testid);
			fprintf(f, "ANSWER:        PASS\n");
			ast_debug(1, "Processing Test ID '%s'\n", testid);
			res = ast_safe_sleep(chan, 1000);
			if (!res) {
				/* Step 1: Send "1" */
				ast_debug(1, "TestServer: 1.  Send DTMF 1\n");
				res = ast_dtmf_stream(chan, NULL, "1", 0,0 );
				fprintf(f, "SEND DTMF 1:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 2: Wait for "2" */
				ast_debug(1, "TestServer: 2.  Wait DTMF 2\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 2:   %s\n", (res != '2') ? "FAIL" : "PASS");
				if (res == '2')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				/* Step 3: Measure noise */
				ast_debug(1, "TestServer: 3.  Measure noise\n");
				res = measurenoise(chan, 6000, "TestServer");
				fprintf(f, "MEASURENOISE:  %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 4: Send "4" */
				ast_debug(1, "TestServer: 4.  Send DTMF 4\n");
				res = ast_dtmf_stream(chan, NULL, "4", 0, 0);
				fprintf(f, "SEND DTMF 4:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 5: Wait one second */
				ast_debug(1, "TestServer: 5.  Wait one second\n");
				res = ast_safe_sleep(chan, 1000);
				fprintf(f, "WAIT 1 SEC:    %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 6: Measure noise */
				ast_debug(1, "TestServer: 6.  Measure tone\n");
				res = measurenoise(chan, 4000, "TestServer");
				fprintf(f, "MEASURETONE:   %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 7: Send "5" */
				ast_debug(1, "TestServer: 7.  Send DTMF 5\n");
				res = ast_dtmf_stream(chan, NULL, "5", 0, 0);
				fprintf(f, "SEND DTMF 5:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 8: Transmit tone noise */
				ast_debug(1, "TestServer: 8.  Transmit tone\n");
				res = sendnoise(chan, 6000);
				fprintf(f, "SENDTONE:      %s\n", (res < 0) ? "FAIL" : "PASS");
			}

			if (!res || (res == '7')) {
				/* Step 9: Wait for "7" */
				ast_debug(1, "TestServer: 9.  Wait DTMF 7\n");
				if (!res)
					res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 7:   %s\n", (res != '7') ? "FAIL" : "PASS");
				if (res == '7')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				res = ast_safe_sleep(chan, 1000);
			}
			if (!res) {
				/* Step 10: Send "8" */
				ast_debug(1, "TestServer: 10.  Send DTMF 8\n");
				res = ast_dtmf_stream(chan, NULL, "8", 0, 0);
				fprintf(f, "SEND DTMF 8:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 11: Wait for hangup to arrive! */
				ast_debug(1, "TestServer: 11.  Waiting for hangup\n");
				res = ast_safe_sleep(chan, 10000);
				fprintf(f, "WAIT HANGUP:   %s\n", (res < 0) ? "PASS" : "FAIL");
			}

			ast_log(LOG_NOTICE, "-- TEST COMPLETE--\n");
			fprintf(f, "-- END TEST--\n");
			fclose(f);
			res = -1;
		} else
			res = -1;
	} else {
		ast_log(LOG_NOTICE, "Did not read a test ID on '%s'\n", ast_channel_name(chan));
		res = -1;
	}
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(testc_app);
	res |= ast_unregister_application(tests_app);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(testc_app, testclient_exec);
	res |= ast_register_application_xml(tests_app, testserver_exec);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Interface Test Application");

