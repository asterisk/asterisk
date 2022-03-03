/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C)  2004 - 2005 Steve Rodgers
 *
 * Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
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
 * \brief Central Station Alarm receiver for Ademco Contact ID
 * \author Steve Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 * *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING ***
 *
 * Use at your own risk. Please consult the GNU GPL license document included with Asterisk.         *
 *
 * *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING *** WARNING ***
 *
 * \ingroup applications
 */

/*! \li \ref app_alarmreceiver.c uses the configuration file \ref alarmreceiver.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page alarmreceiver.conf alarmreceiver.conf
 * \verbinclude alarmreceiver.conf.sample
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <math.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/config.h"
#include "asterisk/localtime.h"
#include "asterisk/callerid.h"
#include "asterisk/astdb.h"
#include "asterisk/utils.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h"

#define ALMRCV_CONFIG "alarmreceiver.conf"
#define UNKNOWN_FORMAT "UNKNOWN_FORMAT"

#define ADEMCO_CONTACT_ID "ADEMCO_CONTACT_ID"
/*
	AAAA _ID_ P CCC XX ZZZ S

where AAAA is the account number, _ID_ is 18 or 98, P is the pin status (alarm or restore), CCC
is the alarm code which is pre-defined by Ademco (but you may be able to reprogram it in the panel), XX
is the dialer group, partition or area number, ZZZ is the zone or user number and S is the checksum
*/

#define ADEMCO_EXPRESS_4_1 "ADEMCO_EXPRESS_4_1"
/*
	AAAA _ID_ C S

where AAAA is the account number, _ID_ is 17, C is the alarm code and S is the checksum.
*/

#define ADEMCO_EXPRESS_4_2 "ADEMCO_EXPRESS_4_2"
/*
	AAAA _ID_ C Z S

where AAAA is the account number, _ID_ is 27, C is the alarm code, Z is the zone or user number and S is the checksum.
*/

#define ADEMCO_HIGH_SPEED "ADEMCO_HIGH_SPEED"
/*
	AAAA _ID_ PPPP PPPP X S

where AAAA is the account number, _ID_ is 55, PPPP PPPP is the status of each zone, X
is a special digit which describes the type of information in the PPPP PPPP fields and S is checksum.
Each P field contains one of the following values:
        1  new alarm           3  new restore           5  normal
        2  new opening         4  new closing           6  outstanding
The X field contains one of the following values:
        0  AlarmNet messages
        1  ambush or duress
        2  opening by user (the first P field contains the user number)
        3  bypass (the P fields indicate which zones are bypassed)
        4  closing by user (the first P field contain the user number)
        5  trouble (the P fields contain which zones are in trouble)
        6  system trouble
        7  normal message (the P fields indicate zone status)
        8  low battery (the P fields indicate zone status)
        9  test (the P fields indicate zone status)
*/
#define ADEMCO_SUPER_FAST "ADEMCO_SUPER_FAST"
/*
	AAAA _ID_ PPPP PPPP X
where AAA is the account number, _ID_ is 56
*/

#define ADEMCO_MSG_TYPE_1 "18"
#define ADEMCO_MSG_TYPE_2 "98"
#define ADEMCO_MSG_TYPE_3 "17"
#define ADEMCO_MSG_TYPE_4 "27"
#define ADEMCO_MSG_TYPE_5 "55"
#define ADEMCO_MSG_TYPE_6 "56"

#define ADEMCO_AUDIO_CALL_NEXT "606"

struct {
  char digit;
  char weight;
} digits_mapping[] = { {'0', 10}, {'1', 1} , {'2', 2}, {'3', 3}, {'4', 4}, {'5', 5},
	{'6', 6}, {'7', 7}, {'8', 8}, {'9', 9}, {'*', 11}, {'#', 12},
	{'A', 13}, {'B', 14}, {'C', 15} };

struct event_node{
	char data[17];
	struct event_node *next;
};

typedef struct event_node event_node_t;

struct timeval call_start_time;

static const char app[] = "AlarmReceiver";
/*** DOCUMENTATION
	<application name="AlarmReceiver" language="en_US">
		<synopsis>
			Provide support for receiving alarm reports from a burglar or fire alarm panel.
		</synopsis>
		<syntax />
		<description>
			<para>This application should be called whenever there is an alarm panel calling in to dump its events.
			The application will handshake with the alarm panel, and receive events, validate them, handshake them,
			and store them until the panel hangs up. Once the panel hangs up, the application will run the system
			command specified by the eventcmd setting in <filename>alarmreceiver.conf</filename> and pipe the
			events to the standard input of the application.
			The configuration file also contains settings for DTMF timing, and for the loudness of the
			acknowledgement tones.</para>
			<note><para>Few Ademco DTMF signalling formats are detected automatically: Contact ID, Express 4+1,
			Express 4+2, High Speed and Super Fast.</para></note>
			<para>The application is affected by the following variables:</para>
			<variablelist>
				<variable name="ALARMRECEIVER_CALL_LIMIT">
					<para>Maximum call time, in milliseconds.</para>
					<para>If set, this variable causes application to exit after the specified time.</para>
				</variable>
				<variable name="ALARMRECEIVER_RETRIES_LIMIT">
					<para>Maximum number of retries per call.</para>
					<para>If set, this variable causes application to exit after the specified number of messages.</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="filename">alarmreceiver.conf</ref>
		</see-also>
	</application>
 ***/

/* Config Variables */
static int fdtimeout = 2000;
static int sdtimeout = 200;
static int answait = 1250;
static int toneloudness = 4096;
static int log_individual_events = 0;
static int no_group_meta = 0;
static char event_spool_dir[128] = {'\0'};
static char event_app[128] = {'\0'};
static char db_family[128] = {'\0'};
static char time_stamp_format[128] = {"%a %b %d, %Y @ %H:%M:%S %Z"};

/* Misc variables */
static char event_file[14] = "/event-XXXXXX";

/*!
 * \brief Attempt to access a database variable and increment it
 *
 * \note Only if the user defined db-family in alarmreceiver.conf
 *
 * The alarmreceiver app will write statistics to a few variables
 * in this family if it is defined. If the new key doesn't exist in the
 * family, then create it and set its value to 1.
 *
 * \param key A database key to increment
 */
static void database_increment(char *key)
{
	unsigned v;
	char value[16];

	if (ast_strlen_zero(db_family)) {
		return;	/* If not defined, don't do anything */
	}

	if (ast_db_get(db_family, key, value, sizeof(value) - 1)) {
		ast_verb(4, "AlarmReceiver: Creating database entry %s and setting to 1\n", key);
		/* Guess we have to create it */
		ast_db_put(db_family, key, "1");
		return;
	}

	sscanf(value, "%30u", &v);
	v++;

	ast_verb(4, "AlarmReceiver: New value for %s: %u\n", key, v);
	snprintf(value, sizeof(value), "%u", v);

	if (ast_db_put(db_family, key, value)) {
		ast_verb(4, "AlarmReceiver: database_increment write error\n");
	}

	return;
}

/*!
 * \brief Receive a fixed length DTMF string.
 *
 * \note Doesn't give preferential treatment to any digit,
 * \note allow different timeout values for the first and all subsequent digits
 *
 * \param chan Asterisk Channel
 * \param digit_string Digits String
 * \param buf_size The size of the Digits String buffer
 * \param expected Digits expected for this message type
 * \param received Pointer to number of digits received so far
 *
 * \retval 0 if all digits were successfully received
 * \retval 1 if a timeout occurred
 * \retval -1 if the caller hung up or on channel errors
 */
static int receive_dtmf_digits(struct ast_channel *chan, char *digit_string, int buf_size, int expected, int *received)
{
	int rtn = 0;
	int r;
	struct ast_frame *f;
	struct timeval lastdigittime;

	lastdigittime = ast_tvnow();
	while (*received < expected && *received < buf_size - 1) {
		/* If timed out, leave */
		if (ast_tvdiff_ms(ast_tvnow(), lastdigittime) > ((*received > 0) ? sdtimeout : fdtimeout)) {
			ast_verb(4, "AlarmReceiver: DTMF Digit Timeout on %s\n", ast_channel_name(chan));
			ast_debug(1, "AlarmReceiver: DTMF timeout on chan %s\n", ast_channel_name(chan));
			rtn = 1;
			break;
		}

		if ((r = ast_waitfor(chan, -1)) < 0) {
			ast_debug(1, "Waitfor returned %d\n", r);
			continue;
		}

		if ((f = ast_read(chan)) == NULL) {
			rtn = -1;
			break;
		}

		/* If they hung up, leave */
		if ((f->frametype == AST_FRAME_CONTROL)
			&& (f->subclass.integer == AST_CONTROL_HANGUP)) {
			if (f->data.uint32) {
				ast_channel_hangupcause_set(chan, f->data.uint32);
			}
			ast_frfree(f);
			rtn = -1;
			break;
		}

		/* If not DTMF, just do it again */
		if (f->frametype != AST_FRAME_DTMF) {
			ast_frfree(f);
			continue;
		}

		/* Save digit */
		digit_string[(*received)++] = f->subclass.integer;
		ast_frfree(f);

		lastdigittime = ast_tvnow();
	}

	/* Null terminate the end of the digit_string */
	digit_string[*received] = '\0';

	return rtn;
}

/*!
 * \brief Write metadata to log file
 *
 * \param logfile Log File Pointer
 * \param signalling_type Signaling Type
 * \param chan Asterisk Channel
 * \param no_checksum Expecting messages without checksum
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int write_metadata(FILE *logfile, char *signalling_type, struct ast_channel *chan, int no_checksum)
{
	struct timeval t;
	struct ast_tm now;
	char *cl;
	char *cn;
	char workstring[80];
	char timestamp[80];

	/* Extract the caller ID location */
	ast_copy_string(workstring,
		S_COR(ast_channel_caller(chan)->id.number.valid,
		ast_channel_caller(chan)->id.number.str, ""), sizeof(workstring));
	ast_shrink_phone_number(workstring);
	if (ast_strlen_zero(workstring)) {
		cl = "<unknown>";
	} else {
		cl = workstring;
	}
	cn = S_COR(ast_channel_caller(chan)->id.name.valid,
		ast_channel_caller(chan)->id.name.str, "<unknown>");

	/* Get the current time */
	t = ast_tvnow();
	ast_localtime(&t, &now, NULL);

	/* Format the time */
	ast_strftime(timestamp, sizeof(timestamp), time_stamp_format, &now);

	if (no_group_meta && fprintf(logfile, "PROTOCOL=%s\n"
			"CHECKSUM=%s\n"
			"CALLINGFROM=%s\n"
			"CALLERNAME=%s\n"
			"TIMESTAMP=%s\n\n",
			signalling_type, (!no_checksum) ? "yes" : "no", cl, cn, timestamp) > -1) {
		return 0;
	} else if (fprintf(logfile, "\n\n[metadata]\n\n"
			"PROTOCOL=%s\n"
			"CHECKSUM=%s\n"
			"CALLINGFROM=%s\n"
			"CALLERNAME=%s\n"
			"TIMESTAMP=%s\n\n"
			"[events]\n\n",
			signalling_type, (!no_checksum) ? "yes" : "no", cl, cn, timestamp) > -1) {
		return 0;
	}

	ast_verb(3, "AlarmReceiver: can't write metadata\n");
	ast_debug(1, "AlarmReceiver: can't write metadata\n");
	return -1;
}

/*!
 * \brief Log a single event
 *
 * \param logfile Log File Pointer
 * \param event Event Structure
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int write_event(FILE *logfile, event_node_t *event)
{
	if (fprintf(logfile, "%s%s\n", no_group_meta ? "event=" : "", event->data) < 0) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Log events if configuration key logindividualevents is enabled or on exit
 *
 * \param chan Asterisk Channel
 * \param signalling_type Signaling Type
 * \param event Event Structure
 * \param no_checksum Expecting messages without checksum
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int log_events(struct ast_channel *chan, char *signalling_type, event_node_t *event, int no_checksum)
{
	char workstring[sizeof(event_spool_dir) + sizeof(event_file)] = "";
	int fd;
	FILE *logfile;
	event_node_t *elp = event;

	if (!ast_strlen_zero(event_spool_dir)) {

		/* Make a template */
		ast_copy_string(workstring, event_spool_dir, sizeof(workstring));
		strncat(workstring, event_file, sizeof(workstring) - strlen(workstring) - 1);

		/* Make the temporary file */
		fd = mkstemp(workstring);

		if (fd == -1) {
			ast_verb(3, "AlarmReceiver: can't make temporary file\n");
			ast_debug(1, "AlarmReceiver: can't make temporary file\n");
			return -1;
		}

		if ((logfile = fdopen(fd, "w")) == NULL) {
			return -1;
		}

		/* Write the file */
		if (write_metadata(logfile, signalling_type, chan, no_checksum)) {
			fflush(logfile);
			fclose(logfile);
			return -1;
		}

		while ((elp != NULL) && (write_event(logfile, elp) == 0)) {
			elp = elp->next;
		}

		fflush(logfile);
		fclose(logfile);
	}

	return 0;
}

/*!
 * \brief Verify Ademco checksum
 * \since 11.0
 *
 * \param event Received DTMF String
 * \param expected Number of Digits expected
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int ademco_verify_checksum(char *event, int expected)
{
	int checksum = 0;
	int i, j;

	for (j = 0; j < expected; j++) {
		for (i = 0; i < ARRAY_LEN(digits_mapping); i++) {
			if (digits_mapping[i].digit == event[j]) {
				break;
			}
		}

		if (i >= ARRAY_LEN(digits_mapping)) {
			ast_verb(2, "AlarmReceiver: Bad DTMF character %c, trying again\n", event[j]);
			return -1;
		}

		checksum += digits_mapping[i].weight;
	}

	/* Checksum is mod(15) of the total */
	if (!(checksum % 15)) {
		return 0;
	}

	return -1;
}

/*!
 * \brief Send a single tone burst for a specified duration and frequency.
 * \since 11.0
 *
 * \param chan Asterisk Channel
 * \param tone_freq Frequency of the tone to send
 * \param tone_duration Tone duration in ms
 * \param delay Delay before sending the tone
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int send_tone_burst(struct ast_channel *chan, const char *tone_freq, int tone_duration, int delay)
{
	if (delay && ast_safe_sleep(chan, delay)) {
		return -1;
	}

	if (ast_playtones_start(chan, toneloudness, tone_freq, 0)) {
		return -1;
	}

	if (ast_safe_sleep(chan, tone_duration)) {
		return -1;
	}

	ast_playtones_stop(chan);
	return 0;
}

/*!
 * \brief Check if the message is in known and valid Ademco format
 *
 * \param signalling_type Expected signalling type for the message
 * \param event event received
 *
 * \retval 0 The event is valid
 * \retval -1 The event is not valid
 */
static int ademco_check_valid(char *signalling_type, char *event)
{
	if (!strcmp(signalling_type, UNKNOWN_FORMAT)) {
		return 1;
	}

	if (!strcmp(signalling_type, ADEMCO_CONTACT_ID)
		&& strncmp(event + 4, ADEMCO_MSG_TYPE_1, 2)
		&& strncmp(event + 4, ADEMCO_MSG_TYPE_2, 2)) {
		return -1;
	}

	if (!strcmp(signalling_type, ADEMCO_EXPRESS_4_1) && strncmp(event + 4, ADEMCO_MSG_TYPE_3, 2)) {
		return -1;
	}

	if (!strcmp(signalling_type, ADEMCO_EXPRESS_4_2) && strncmp(event + 4, ADEMCO_MSG_TYPE_4, 2)) {
		return -1;
	}

	if (!strcmp(signalling_type, ADEMCO_HIGH_SPEED) && strncmp(event + 4, ADEMCO_MSG_TYPE_5, 2)) {
		return -1;
	}

	if (!strcmp(signalling_type, ADEMCO_SUPER_FAST) && strncmp(event + 4, ADEMCO_MSG_TYPE_6, 2)) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Detect the message format of an event
 *
 * \param signalling_type Expected signalling type for the message
 * \param event event received
 * \param no_checksum Should we calculate checksum for the message
 *
 * \returns The expected digits for the detected event type
 */
static int ademco_detect_format(char *signalling_type, char *event, int *no_checksum)
{
	int res = 16;

	if (!strncmp(event + 4, ADEMCO_MSG_TYPE_1, 2)
		|| !strncmp(event + 4, ADEMCO_MSG_TYPE_2, 2)) {
		sprintf(signalling_type, "%s", ADEMCO_CONTACT_ID);
	}

	if (!strncmp(event + 4, ADEMCO_MSG_TYPE_3, 2)) {
		sprintf(signalling_type, "%s", ADEMCO_EXPRESS_4_1);
		res = 8;
	}

	if (!strncmp(event + 4, ADEMCO_MSG_TYPE_4, 2)) {
		sprintf(signalling_type, "%s", ADEMCO_EXPRESS_4_2);
		res = 9;
	}

	if (!strncmp(event + 4, ADEMCO_MSG_TYPE_5, 2)) {
		sprintf(signalling_type, "%s", ADEMCO_HIGH_SPEED);
	}

	if (!strncmp(event + 4, ADEMCO_MSG_TYPE_6, 2)) {
		sprintf(signalling_type, "%s", ADEMCO_SUPER_FAST);
		*no_checksum = 1;
		res = 15;
	}

	if (strcmp(signalling_type, UNKNOWN_FORMAT)) {
		ast_verb(4, "AlarmMonitoring: Detected format %s.\n", signalling_type);
		ast_debug(1, "AlarmMonitoring: Autodetected format %s.\n", signalling_type);
	}

	return res;
}

/*!
 * \brief Receive Ademco ContactID or other format Data String
 *
 * \param chan Asterisk Channel
 * \param ehead Pointer to events list
 * \param signalling_type Expected signalling type for the message
 * \param no_checksum Should we calculate checksum for the message
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int receive_ademco_event(struct ast_channel *chan, event_node_t **ehead, char *signalling_type, int *no_checksum)
{
	int res = 0;
	const char *limit;
	char event[17];
	event_node_t *enew, *elp;
	int got_some_digits = 0;
	int events_received = 0;
	int ack_retries = 0;
	int limit_retries = 0;
	int expected_length = sizeof(event) - 1;

	database_increment("calls-received");

	/* Wait for first event */
	ast_verb(4, "AlarmReceiver: Waiting for first event from panel...\n");

	while (res >= 0) {
		int digits_received = 0;

		res = 0;

		if (log_individual_events) {
			sprintf(signalling_type, "%s", UNKNOWN_FORMAT);
			expected_length = 16;
			*no_checksum = 0;
		}

		if (got_some_digits == 0) {
			/* Send ACK tone sequence */
			ast_verb(4, "AlarmReceiver: Sending 1400Hz 100ms burst (ACK)\n");
			res = send_tone_burst(chan, "1400", 100, 0);
			if (!res) {
				ast_verb(4, "AlarmReceiver: Sending 2300Hz 100ms burst (ACK)\n");
				res = send_tone_burst(chan, "2300", 100, 100);
			}
		}
		if (res) {
			return -1;
		}

		res = receive_dtmf_digits(chan, event, sizeof(event), expected_length, &digits_received);
		if (res < 0) {
			if (events_received == 0) {
				/* Hangup with no events received should be logged in the DB */
				database_increment("no-events-received");
				ast_verb(4, "AlarmReceiver: No events received!\n");
			} else {
				if (ack_retries) {
					database_increment("ack-retries");
					ast_verb(4, "AlarmReceiver: ACK retries during this call: %d\n", ack_retries);
				}
			}
			ast_verb(4, "AlarmReceiver: App exiting...\n");
			break;
		}

		if (!strcmp(signalling_type, UNKNOWN_FORMAT) && digits_received > 5) {
			expected_length = ademco_detect_format(signalling_type, event, no_checksum);

			if (res > 0) {
				if (digits_received == expected_length) {
					res = limit_retries = 0;
				} else if (digits_received == expected_length - 1
					&& (!strcmp(signalling_type, ADEMCO_EXPRESS_4_2)
					|| !strcmp(signalling_type, ADEMCO_EXPRESS_4_1))) {
					/* ADEMCO EXPRESS without checksum */
					res = limit_retries = 0;
					expected_length--;
					*no_checksum = 1;
					ast_verb(4, "AlarmMonitoring: Skipping checksum for format %s.\n", signalling_type);
					ast_debug(1, "AlarmMonitoring: Skipping checksum for format %s.\n", signalling_type);
				}
			}
		}

		ast_channel_lock(chan);
		limit = pbx_builtin_getvar_helper(chan, "ALARMRECEIVER_CALL_LIMIT");
		if (!ast_strlen_zero(limit)) {
			if (ast_tvdiff_ms(ast_tvnow(), call_start_time) > atoi(limit)) {
				ast_channel_unlock(chan);
				return -1;
			}
		}
		limit = pbx_builtin_getvar_helper(chan, "ALARMRECEIVER_RETRIES_LIMIT");
		ast_channel_unlock(chan);
		if (!ast_strlen_zero(limit)) {
			if (limit_retries + 1 >= atoi(limit)) {
				return -1;
			}
		}

		if (res) {
			/* Didn't get all of the digits */
			ast_verb(2, "AlarmReceiver: Incomplete string: %s, trying again...\n", event);
			limit_retries++;

			if (!events_received && strcmp(signalling_type, UNKNOWN_FORMAT))
			{
				sprintf(signalling_type, "%s", UNKNOWN_FORMAT);
				expected_length = sizeof(event) - 1;
			}

			if (!got_some_digits) {
				got_some_digits = (!ast_strlen_zero(event)) ? 1 : 0;
				ack_retries++;
			}
			continue;
		}

		got_some_digits = 1;

		ast_verb(2, "AlarmReceiver: Received Event %s\n", event);
		ast_debug(1, "AlarmReceiver: Received event: %s\n", event);

		/* Calculate checksum */
		if (!(*no_checksum) && ademco_verify_checksum(event, expected_length)) {
			database_increment("checksum-errors");
			ast_verb(2, "AlarmReceiver: Nonzero checksum\n");
			ast_debug(1, "AlarmReceiver: Nonzero checksum\n");
			continue;
		}

		/* Check the message type for correctness */
		if (ademco_check_valid(signalling_type, event)) {
			database_increment("format-errors");
			ast_verb(2, "AlarmReceiver: Wrong message type\n");
			ast_debug(1, "AlarmReceiver: Wrong message type\n");
			continue;
		}

		events_received++;

		/* Queue the Event */
		if (!(enew = ast_calloc(1, sizeof(*enew)))) {
			return -1;
		}

		enew->next = NULL;
		ast_copy_string(enew->data, event, sizeof(enew->data));

		/* Insert event onto end of list */
		if (*ehead == NULL) {
			*ehead = enew;
		} else {
			for (elp = *ehead; elp->next != NULL; elp = elp->next) {
				;
			}
			elp->next = enew;
		}

		/* Let the user have the option of logging the single event before sending the kissoff tone */
		if (log_individual_events && log_events(chan, signalling_type, enew, *no_checksum)) {
			return -1;
		}

		/* Send the kissoff tone (1400 Hz, 900 ms, after 200ms delay) */
		if (send_tone_burst(chan, "1400", 900, 200)) {
			return -1;
		}

		/* If audio call follows, exit alarm receiver app */
		if (!strcmp(signalling_type, ADEMCO_CONTACT_ID)
			&& !strncmp(event + 7, ADEMCO_AUDIO_CALL_NEXT, 3)) {
			ast_verb(4, "AlarmReceiver: App exiting... Audio call next!\n");
			return 0;
		}
	}

	return res;
}

/*!
 * \brief This is the main function called by Asterisk Core whenever the App is invoked in the extension logic.
 *
 * \param chan Asterisk Channel
 * \param data Application data
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int alarmreceiver_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int no_checksum = 0;
	event_node_t *elp, *efree;
	char signalling_type[64] = "";
	event_node_t *event_head = NULL;

	if ((ast_format_cmp(ast_channel_writeformat(chan), ast_format_ulaw) == AST_FORMAT_CMP_NOT_EQUAL) &&
		(ast_format_cmp(ast_channel_writeformat(chan), ast_format_alaw) == AST_FORMAT_CMP_NOT_EQUAL)) {
		ast_verb(4, "AlarmReceiver: Setting write format to Mu-law\n");
		if (ast_set_write_format(chan, ast_format_ulaw)) {
			ast_log(LOG_WARNING, "AlarmReceiver: Unable to set write format to Mu-law on %s\n",ast_channel_name(chan));
			return -1;
		}
	}

	if ((ast_format_cmp(ast_channel_readformat(chan), ast_format_ulaw) == AST_FORMAT_CMP_NOT_EQUAL) &&
		(ast_format_cmp(ast_channel_readformat(chan), ast_format_alaw) == AST_FORMAT_CMP_NOT_EQUAL)) {
		ast_verb(4, "AlarmReceiver: Setting read format to Mu-law\n");
		if (ast_set_read_format(chan, ast_format_ulaw)) {
			ast_log(LOG_WARNING, "AlarmReceiver: Unable to set read format to Mu-law on %s\n",ast_channel_name(chan));
			return -1;
		}
	}

	/* Set default values for this invocation of the application */
	ast_copy_string(signalling_type, UNKNOWN_FORMAT, sizeof(signalling_type));
	call_start_time = ast_tvnow();

	/* Answer the channel if it is not already */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_verb(4, "AlarmReceiver: Answering channel\n");
		if (ast_answer(chan)) {
			return -1;
		}
	}

	/* Wait for the connection to settle post-answer */
	ast_verb(4, "AlarmReceiver: Waiting for connection to stabilize\n");
	if (ast_safe_sleep(chan, answait)) {
		return -1;
	}

	/* Attempt to receive the events */
	receive_ademco_event(chan, &event_head, signalling_type, &no_checksum);

	/* Events queued by receiver, write them all out here if so configured */
	if (!log_individual_events) {
		res = log_events(chan, signalling_type, event_head, no_checksum);
	}

	/* Do we exec a command line at the end? */
	if ((!res) && (!ast_strlen_zero(event_app)) && (event_head)) {
		ast_debug(1,"Alarmreceiver: executing: %s\n", event_app);
		ast_safe_system(event_app);
	}

	/* Free up the data allocated in our linked list */
	for (elp = event_head; (elp != NULL);) {
		efree = elp;
		elp = elp->next;
		ast_free(efree);
	}

	return 0;
}

/*!
 * \brief Load the configuration from the configuration file
 *
 * \param reload True on reload
 *
 * \retval 1 success
 * \retval 0 failure
 */
static int load_config(int reload)
{
	struct ast_config *cfg;
	const char *value;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	/* Read in the config file */
	cfg = ast_config_load(ALMRCV_CONFIG, config_flags);

	if (!cfg) {
		ast_verb(4, "AlarmReceiver: No config file\n");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n",
			ALMRCV_CONFIG);
		return 0;
	}

	if ((value = ast_variable_retrieve(cfg, "general", "eventcmd")) != NULL) {
		ast_copy_string(event_app, value, sizeof(event_app));
	}

	if ((value = ast_variable_retrieve(cfg, "general", "loudness")) != NULL) {
		toneloudness = atoi(value);
		if (toneloudness < 100) {
			toneloudness = 100;
		} else if (toneloudness > 8192) {
			toneloudness = 8192;
		}
	}

	if ((value = ast_variable_retrieve(cfg, "general", "fdtimeout")) != NULL) {
		fdtimeout = atoi(value);
		if (fdtimeout < 1000) {
			fdtimeout = 1000;
		} else if (fdtimeout > 10000) {
			fdtimeout = 10000;
		}
	}

	if ((value = ast_variable_retrieve(cfg, "general", "sdtimeout")) != NULL) {
		sdtimeout = atoi(value);
		if (sdtimeout < 110) {
			sdtimeout = 110;
		} else if (sdtimeout > 4000) {
			sdtimeout = 4000;
		}
	}

	if ((value = ast_variable_retrieve(cfg, "general", "answait")) != NULL) {
		answait = atoi(value);
		if (answait < 500) {
			answait = 500;
		} else if (answait > 10000) {
			answait = 10000;
		}
	}

	if ((value = ast_variable_retrieve(cfg, "general", "no_group_meta")) != NULL) {
		no_group_meta = ast_true(value);
	}

	if ((value = ast_variable_retrieve(cfg, "general", "logindividualevents")) != NULL) {
		log_individual_events = ast_true(value);
	}

	if ((value = ast_variable_retrieve(cfg, "general", "eventspooldir")) != NULL) {
		ast_copy_string(event_spool_dir, value, sizeof(event_spool_dir));
	}

	if ((value = ast_variable_retrieve(cfg, "general", "timestampformat")) != NULL) {
		ast_copy_string(time_stamp_format, value, sizeof(time_stamp_format));
	}

	if ((value = ast_variable_retrieve(cfg, "general", "db-family")) != NULL) {
		ast_copy_string(db_family, value, sizeof(db_family));
	}

	ast_config_destroy(cfg);

	return 1;
}

/*!
 * \brief Unregister Alarm Receiver App
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int unload_module(void)
{
	return ast_unregister_application(app);
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (load_config(0)) {
		if (ast_register_application_xml(app, alarmreceiver_exec)) {
			return AST_MODULE_LOAD_DECLINE;
		}
		return AST_MODULE_LOAD_SUCCESS;
	}

	return AST_MODULE_LOAD_DECLINE;
}

static int reload(void)
{
	if (load_config(1)) {
		return AST_MODULE_LOAD_SUCCESS;
	}

	return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Alarm Receiver for Asterisk",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
