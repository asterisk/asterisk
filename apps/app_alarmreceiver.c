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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

#define ALMRCV_CONFIG "alarmreceiver.conf"
#define ADEMCO_CONTACT_ID "ADEMCO_CONTACT_ID"
#define ADEMCO_MSG_TYPE_1 "18"
#define ADEMCO_MSG_TYPE_2 "98"
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
			<note><para>Only 1 signalling format is supported at this time: Ademco Contact ID.</para></note>
		</description>
		<see-also>
			<ref type="filename">alarmreceiver.conf</ref>
		</see-also>
	</application>
 ***/

/* Config Variables */
static int fdtimeout = 2000;
static int sdtimeout = 200;
static int toneloudness = 4096;
static int log_individual_events = 0;
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
 * \return Nothing
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
 * \param length Length of the message we expect
 * \param fdto First Digit Timeout
 * \param sdto Other Digits Timeout
 *
 * \retval 0 if all digits were successfully received
 * \retval 1 if a timeout occurred
 * \retval -1 if the caller hung up or on channel errors
 */
static int receive_dtmf_digits(struct ast_channel *chan, char *digit_string, int buf_size, int length, int fdto, int sdto)
{
	int rtn = 0;
	int i = 0;
	int r;
	struct ast_frame *f;
	struct timeval lastdigittime;

	lastdigittime = ast_tvnow();
	while (i < length && i < buf_size - 1) {
		/* If timed out, leave */
		if (ast_tvdiff_ms(ast_tvnow(), lastdigittime) > ((i > 0) ? sdto : fdto)) {
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
		digit_string[i++] = f->subclass.integer;
		ast_frfree(f);

		lastdigittime = ast_tvnow();
	}

	/* Null terminate the end of the digit string */
	digit_string[i] = '\0';
	return rtn;
}

/*!
 * \brief Write metadata to log file
 *
 * \param logfile Log File Pointer
 * \param signalling_type Signaling Type
 * \param chan Asterisk Channel
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int write_metadata(FILE *logfile, char *signalling_type, struct ast_channel *chan)
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

	if (fprintf(logfile, "\n\n[metadata]\n\n"
			"PROTOCOL=%s\n"
			"CALLINGFROM=%s\n"
			"CALLERNAME=%s\n"
			"TIMESTAMP=%s\n\n"
			"[events]\n\n", signalling_type, cl, cn, timestamp) < 0) {
		ast_verb(3, "AlarmReceiver: can't write metadata\n");
		ast_debug(1, "AlarmReceiver: can't write metadata\n");
		return -1;
	}

	return 0;
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
	if (fprintf(logfile, "%s\n", event->data) < 0) {
		return -1;
	}
	return 0;
}

/*!
 * \brief Log events if configuration key logindividualevents is enabled
 *
 * \param chan Asterisk Channel
 * \param signalling_type Signaling Type
 * \param event Event Structure
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int log_events(struct ast_channel *chan, char *signalling_type, event_node_t *event)
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
		if (write_metadata(logfile, signalling_type, chan) != 0) {
			fflush(logfile);
			fclose(logfile);
			return -1;
		}

		while ((write_event(logfile, elp) > 0) && (elp != NULL)) {
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
 * \brief Send a single tone burst for a specifed duration and frequency.
 * \since 11.0
 *
 * \param chan Asterisk Channel
 * \param tone_freq Frequency of the tone to send
 * \param tone_duration Tone duration in ms
 * \param tldn Tone loudness
 * \param delay Delay before sending the tone
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int send_tone_burst(struct ast_channel *chan, const char *tone_freq, int tone_duration, int tldn, int delay)
{
	if (delay && ast_safe_sleep(chan, delay)) {
		return -1;
	}

	if (ast_playtones_start(chan, tldn, tone_freq, 0)) {
		return -1;
	}

	if (ast_safe_sleep(chan, tone_duration)) {
		return -1;
	}

	ast_playtones_stop(chan);
	return 0;
}

/*!
 * \brief Receive Ademco ContactID Data String
 *
 * \param chan Asterisk Channel
 * \param fdto First Digit Timeout
 * \param sdto Other Digits Timeout
 * \param tldn Tone loudness
 * \param ehead Pointer to events list
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int receive_ademco_contact_id(struct ast_channel *chan, int fdto, int sdto, int tldn, event_node_t **ehead)
{
	int res = 0;
	int exit_on_next = 0;
	char event[17];
	event_node_t *enew, *elp;
	int got_some_digits = 0;
	int events_received = 0;
	int ack_retries = 0;

	database_increment("calls-received");

	/* Wait for first event */
	ast_verb(4, "AlarmReceiver: Waiting for first event from panel...\n");

	while (res >= 0) {
		res = 0;
		if (got_some_digits == 0) {
			/* Send ACK tone sequence */
			ast_verb(4, "AlarmReceiver: Sending 1400Hz 100ms burst (ACK)\n");
			res = send_tone_burst(chan, "1400", 100, tldn, 0);
			if (!res) {
				ast_verb(4, "AlarmReceiver: Sending 2300Hz 100ms burst (ACK)\n");
				res = send_tone_burst(chan, "2300", 100, tldn, 100);
			}
		}
		if (res) {
			return -1;
		}

		if (exit_on_next) {
			res = send_tone_burst(chan, "1400", 900, tldn, 200);
			return 0;
		}

		res = receive_dtmf_digits(chan, event, sizeof(event), sizeof(event) - 1, fdto, sdto);
		if (res < 0) {
			if (events_received == 0) {
				/* Hangup with no events received should be logged in the DB */
				database_increment("no-events-received");
			} else {
				if (ack_retries) {
					ast_verb(4, "AlarmReceiver: ACK retries during this call: %d\n", ack_retries);
					database_increment("ack-retries");
				}
			}
			ast_verb(4, "AlarmReceiver: App exiting...\n");
			break;
		}

		if (res) {
			/* Didn't get all of the digits */
			ast_verb(2, "AlarmReceiver: Incomplete string: %s, trying again...\n", event);

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
		if (ademco_verify_checksum(event, 16)) {
			database_increment("checksum-errors");
			ast_verb(2, "AlarmReceiver: Nonzero checksum\n");
			ast_debug(1, "AlarmReceiver: Nonzero checksum\n");
			continue;
		}

		/* Check the message type for correctness */
		if (strncmp(event + 4, ADEMCO_MSG_TYPE_1, 2)) {
			if (strncmp(event + 4, ADEMCO_MSG_TYPE_2, 2)) {
				database_increment("format-errors");
				ast_verb(2, "AlarmReceiver: Wrong message type\n");
				ast_debug(1, "AlarmReceiver: Wrong message type\n");
			continue;
			}
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

		/* Audio call follows, exit alarm receiver app */
		if (!strncmp(event + 7, ADEMCO_AUDIO_CALL_NEXT, 3)) {
			ast_verb(4, "AlarmReceiver: App exiting... Audio call next!\n");
			exit_on_next = 1;
		}

		/* Let the user have the option of logging the single event before sending the kissoff tone */
		if (log_individual_events) {
			res = log_events(chan, ADEMCO_CONTACT_ID, enew);
			if (res) {
				return -1;
			}
		}

		/* Send the kissoff tone (1400 Hz, 900 ms, after 200ms delay) */
		res = send_tone_burst(chan, "1400", 900, tldn, 200);
		if (res) {
			return -1;
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
	event_node_t *elp, *efree;
	char signalling_type[64] = "";
	event_node_t *event_head = NULL;

	/* Set write and read formats to ULAW */
	ast_verb(4, "AlarmReceiver: Setting read and write formats to ULAW\n");

	if (ast_set_write_format_by_id(chan,AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "AlarmReceiver: Unable to set write format to Mu-law on %s\n",ast_channel_name(chan));
		return -1;
	}

	if (ast_set_read_format_by_id(chan,AST_FORMAT_ULAW)) {
		ast_log(LOG_WARNING, "AlarmReceiver: Unable to set read format to Mu-law on %s\n",ast_channel_name(chan));
		return -1;
	}

	/* Set default values for this invocation of the application */
	ast_copy_string(signalling_type, ADEMCO_CONTACT_ID, sizeof(signalling_type));

	/* Answer the channel if it is not already */
	ast_verb(4, "AlarmReceiver: Answering channel\n");
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if ((res = ast_answer(chan))) {
			return -1;
		}
	}

	/* Wait for the connection to settle post-answer */
	ast_verb(4, "AlarmReceiver: Waiting for connection to stabilize\n");
	res = ast_safe_sleep(chan, 1250);

	/* Attempt to receive the events */
	if (!res) {
		/* Determine the protocol to receive in advance */
		/* Note: Ademco contact is the only one supported at this time */
		/* Others may be added later */
		if (!strcmp(signalling_type, ADEMCO_CONTACT_ID)) {
			receive_ademco_contact_id(chan, fdtimeout, sdtimeout, toneloudness, &event_head);
		} else {
			res = -1;
		}
	}

	/* Events queued by receiver, write them all out here if so configured */
	if ((!res) && (log_individual_events == 0)) {
		res = log_events(chan, signalling_type, event_head);
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
 * \retval 1 success
 * \retval 0 failure
 */
static int load_config(void)
{
	struct ast_config *cfg;
	const char *value;
	struct ast_flags config_flags = { 0 };

	/* Read in the config file */
	cfg = ast_config_load(ALMRCV_CONFIG, config_flags);

	if (!cfg) {
		ast_verb(4, "AlarmReceiver: No config file\n");
		return 0;
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
	if (load_config()) {
		if (ast_register_application_xml(app, alarmreceiver_exec)) {
			return AST_MODULE_LOAD_FAILURE;
		}
		return AST_MODULE_LOAD_SUCCESS;
	} else {
		return AST_MODULE_LOAD_DECLINE;
	}
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Alarm Receiver for Asterisk");
