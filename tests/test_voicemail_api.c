/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Matt Jordan
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Skeleton Test
 *
 * \author\verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * Tests for the publicly exposed Voicemail API
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <sys/stat.h>

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/paths.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/format_cache.h"

/*!
 * \internal
 * \brief Permissions to set on the voicemail directories we create
 *
 * \note taken from app_voicemail
 */
#define VOICEMAIL_DIR_MODE 0777

/*!
 * \internal
 * \brief Permissions to set on the voicemail files we create
 *
 * \note taken from app_voicemail
 */
#define VOICEMAIL_FILE_MODE 0666

/*!
 * \internal
 * \brief The number of mock snapshot objects we use for tests
 */
#define TOTAL_SNAPSHOTS 4

/*!
 * \internal
 * \brief Create and populate the mock message objects and create the
 * envelope files on the file system
 */
#define VM_API_TEST_SETUP do { \
	if (!ast_vm_is_registered()) { \
		ast_test_status_update(test, "No voicemail provider registered.\n"); \
		return AST_TEST_FAIL; \
	} else if (test_vm_api_test_setup()) { \
		VM_API_TEST_CLEANUP; \
		ast_test_status_update(test, "Failed to set up necessary mock objects for voicemail API test\n"); \
		return AST_TEST_FAIL; \
	} else { \
		int i = 0; \
		for (; i < TOTAL_SNAPSHOTS; i++) { \
			ast_test_status_update(test, "Created message in %s/%s with ID %s\n", \
				test_snapshots[i]->exten, test_snapshots[i]->folder_name, test_snapshots[i]->msg_id); \
		} \
} } while (0)

/*!
 * \internal
 * \brief Safely cleanup after a test run.
 *
 * \note This should be called both when a test fails and when it passes
 */
#define VM_API_TEST_CLEANUP test_vm_api_test_teardown()

/*!
 * \internal
 * \brief Safely cleanup a snapshot and a test run.
 *
 * \note It assumes that the mailbox snapshot object is test_mbox_snapshot
 */
#define VM_API_SNAPSHOT_TEST_CLEANUP \
		if (test_mbox_snapshot) { \
			test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot); \
		} \
		VM_API_TEST_CLEANUP; \

/*!
 * \internal
 * \brief Verify the expected result from two string values obtained
 * from a mailbox snapshot.
 *
 * \note It assumes the mailbox snapshot object is test_mbox_snapshot
 */
#define VM_API_STRING_FIELD_VERIFY(expected, actual) do { \
	if (strcmp((expected), (actual))) { \
		ast_test_status_update(test, "Test failed for parameter %s: Expected [%s], Actual [%s]\n", #actual, expected, actual); \
		VM_API_SNAPSHOT_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Verify the expected result from two integer values.
 *
 * \note It assumes the mailbox snapshot object is test_mbox_snapshot
 */
#define VM_API_INT_VERIFY(expected, actual) do { \
	if ((expected) != (actual)) { \
		ast_test_status_update(test, "Test failed for parameter %s: Expected [%d], Actual [%d]\n", #actual, (int)expected, (int)actual); \
		VM_API_SNAPSHOT_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Verify that a mailbox snapshot contains the expected message
 * snapshot, in the correct position, with the expected values.
 *
 * \note It assumes the mailbox snapshot object is test_mbox_snapshot
 */
#define VM_API_SNAPSHOT_MSG_VERIFY(expected, actual, expected_folder, expected_index) do { \
	struct ast_vm_msg_snapshot *msg; \
	int found = 0; \
	int counter = 0; \
	AST_LIST_TRAVERSE(&((actual)->snapshots[get_folder_by_name(expected_folder)]), msg, msg) { \
		if (!(strcmp(msg->msg_id, (expected)->msg_id))) { \
			ast_test_status_update(test, "Found message %s in snapshot\n", msg->msg_id); \
			found = 1; \
			if ((expected_index) != counter) { \
				ast_test_status_update(test, "Expected message %s at index %d; Actual [%d]\n", \
					(expected)->msg_id, (expected_index), counter); \
				VM_API_SNAPSHOT_TEST_CLEANUP; \
				return AST_TEST_FAIL; \
			} \
			VM_API_STRING_FIELD_VERIFY((expected)->callerid, msg->callerid); \
			VM_API_STRING_FIELD_VERIFY((expected)->callerchan, msg->callerchan); \
			VM_API_STRING_FIELD_VERIFY((expected)->exten, msg->exten); \
			VM_API_STRING_FIELD_VERIFY((expected)->origdate, msg->origdate); \
			VM_API_STRING_FIELD_VERIFY((expected)->origtime, msg->origtime); \
			VM_API_STRING_FIELD_VERIFY((expected)->duration, msg->duration); \
			VM_API_STRING_FIELD_VERIFY((expected)->folder_name, msg->folder_name); \
			VM_API_STRING_FIELD_VERIFY((expected)->flag, msg->flag); \
			VM_API_INT_VERIFY((expected)->msg_number, msg->msg_number); \
			break; \
		} \
		++counter; \
	} \
	if (!found) { \
		ast_test_status_update(test, "Test failed for message snapshot %s: not found in mailbox snapshot\n", (expected)->msg_id); \
		VM_API_SNAPSHOT_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
} } while (0)


/*!
 * \internal
 * \brief Create a message snapshot, failing the test if the snapshot could not be created.
 *
 * \note This requires having a snapshot named test_mbox_snapshot.
 */
#define VM_API_SNAPSHOT_CREATE(mailbox, context, folder, desc, sort, old_and_inbox) do { \
	if (!(test_mbox_snapshot = ast_vm_mailbox_snapshot_create( \
		(mailbox), (context), (folder), (desc), (sort), (old_and_inbox)))) { \
		ast_test_status_update(test, "Failed to create voicemail mailbox snapshot\n"); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Create a message snapshot, failing the test if the snapshot could be created.
 *
 * \note This is used to test off nominal conditions.
 * \note This requires having a snapshot named test_mbox_snapshot.
 */
#define VM_API_SNAPSHOT_OFF_NOMINAL_TEST(mailbox, context, folder, desc, sort, old_and_inbox) do { \
	if ((test_mbox_snapshot = ast_vm_mailbox_snapshot_create( \
		(mailbox), (context), (folder), (desc), (sort), (old_and_inbox)))) { \
		ast_test_status_update(test, "Created mailbox snapshot when none was expected\n"); \
		test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Move a voicemail message, failing the test if the message could not be moved
 */
#define VM_API_MOVE_MESSAGE(mailbox, context, number_of_messages, source, message_numbers_in, dest) do { \
	if (ast_vm_msg_move((mailbox), (context), (number_of_messages), (source), (message_numbers_in), (dest))) { \
		ast_test_status_update(test, "Failed to move message %s@%s from %s to %s\n", \
			(mailbox) ? (mailbox): "(NULL)", (context) ? (context) : "(NULL)", (source) ? (source) : "(NULL)", (dest) ? (dest) : "(NULL)"); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Attempt to move a voicemail message, failing the test if the message could be moved
 */
#define VM_API_MOVE_MESSAGE_OFF_NOMINAL(mailbox, context, number_of_messages, source, message_numbers_in, dest) do { \
	if (!ast_vm_msg_move((mailbox), (context), (number_of_messages), (source), (message_numbers_in), (dest))) { \
		ast_test_status_update(test, "Succeeded to move message %s@%s from %s to %s when we really shouldn't\n", \
			(mailbox) ? (mailbox): "(NULL)", (context) ? (context) : "(NULL)", (source) ? (source) : "(NULL)", (dest) ? (dest) : "(NULL)"); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Remove a message, failing the test if the method failed or if the message is still present.
 */
#define VM_API_REMOVE_MESSAGE(mailbox, context, number_of_messages, folder, message_numbers_in) do { \
	if (ast_vm_msg_remove((mailbox), (context), (number_of_messages), (folder), (message_numbers_in))) { \
		ast_test_status_update(test, "Failed to remove message from mailbox %s@%s, folder %s", \
			(mailbox) ? (mailbox): "(NULL)", (context) ? (context) : "(NULL)", (folder) ? (folder) : "(NULL)"); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} \
	VM_API_SNAPSHOT_CREATE((mailbox), (context), (folder), 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0); \
	VM_API_INT_VERIFY(0, test_mbox_snapshot->total_msg_num); \
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot); \
} while (0)

/*!
 * \internal
 * \brief Remove a message, failing the test if the method succeeds
 */
#define VM_API_REMOVE_MESSAGE_OFF_NOMINAL(mailbox, context, number_of_messages, folder, message_numbers_in) do { \
	if (!ast_vm_msg_remove((mailbox), (context), (number_of_messages), (folder), (message_numbers_in))) { \
		ast_test_status_update(test, "Succeeded in removing message from mailbox %s@%s, folder %s, when expected result was failure\n", \
				(mailbox) ? (mailbox): "(NULL)", (context) ? (context) : "(NULL)", (folder) ? (folder) : "(NULL)"); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Forward a message, failing the test if the message could not be forwarded
 */
# define VM_API_FORWARD_MESSAGE(from_mailbox, from_context, from_folder, to_mailbox, to_context, to_folder, number_of_messages, message_numbers_in, delete_old) do { \
	if (ast_vm_msg_forward((from_mailbox), (from_context), (from_folder), (to_mailbox), (to_context), (to_folder), (number_of_messages), (message_numbers_in), (delete_old))) { \
		ast_test_status_update(test, "Failed to forward message from %s@%s [%s] to %s@%s [%s]\n", \
			(from_mailbox) ? (from_mailbox) : "(NULL)", (from_context) ? (from_context) : "(NULL)", (from_folder) ? (from_folder) : "(NULL)", \
			(to_mailbox) ? (to_mailbox) : "(NULL)", (to_context) ? (to_context) : "(NULL)", (to_folder) ? (to_folder) : "(NULL)"); \
			VM_API_TEST_CLEANUP; \
			return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Forward a message, failing the test if the message was successfully forwarded
 */
#define VM_API_FORWARD_MESSAGE_OFF_NOMINAL(from_mailbox, from_context, from_folder, to_mailbox, to_context, to_folder, number_of_messages, message_numbers_in, delete_old) do { \
	if (!ast_vm_msg_forward((from_mailbox), (from_context), (from_folder), (to_mailbox), (to_context), (to_folder), (number_of_messages), (message_numbers_in), (delete_old))) { \
		ast_test_status_update(test, "Succeeded in forwarding message from %s@%s [%s] to %s@%s [%s] when expected result was fail\n", \
			(from_mailbox) ? (from_mailbox) : "(NULL)", (from_context) ? (from_context) : "(NULL)", (from_folder) ? (from_folder) : "(NULL)", \
			(to_mailbox) ? (to_mailbox) : "(NULL)", (to_context) ? (to_context) : "(NULL)", (to_folder) ? (to_folder) : "(NULL)"); \
			VM_API_TEST_CLEANUP; \
			return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal                                                                                                              .
 * \brief Playback a message on a channel or callback function                                                            .
 *                                                                                                                        .
 * \note The channel name must be test_channel.
 * \note Fail the test if the message could not be played.
 */
#define VM_API_PLAYBACK_MESSAGE(channel, mailbox, context, folder, message, callback_fn) do { \
	if (ast_vm_msg_play((channel), (mailbox), (context), (folder), (message), (callback_fn))) { \
		ast_test_status_update(test, "Failed nominal playback message test\n"); \
		ast_hangup(test_channel); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)

/*!
 * \internal
 * \brief Playback a message on a channel or callback function.
 *
 * \note The channel name must be test_channel.
 * \note Fail the test if the message is successfully played
 */
#define VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(channel, mailbox, context, folder, message, callback_fn) do { \
	if (!ast_vm_msg_play((channel), (mailbox), (context), (folder), (message), (callback_fn))) { \
		ast_test_status_update(test, "Succeeded in playing back of message when expected result was to fail\n"); \
		ast_hangup(test_channel); \
		VM_API_TEST_CLEANUP; \
		return AST_TEST_FAIL; \
	} } while (0)


/*!
 * \internal
 * \brief Possible names of folders.
 *
 * \note Taken from app_voicemail
 */
static const char * const mailbox_folders[] = {
	"INBOX",
	"Old",
	"Work",
	"Family",
	"Friends",
	"Cust1",
	"Cust2",
	"Cust3",
	"Cust4",
	"Cust5",
	"Deleted",
	"Urgent",
};

/*!
 * \internal
 * \brief Message snapshots representing the messages that are used by the various tests
 */
static struct ast_vm_msg_snapshot *test_snapshots[TOTAL_SNAPSHOTS];

/*!
 * \internal
 * \brief Tracks whether or not we entered into the message playback callback function
 */
static int global_entered_playback_callback = 0;

/*!
 * \internal
 * \brief Get a folder index by its name
 */
static int get_folder_by_name(const char *folder)
{
	size_t i;

	for (i = 0; i < ARRAY_LEN(mailbox_folders); i++) {
		if (strcasecmp(folder, mailbox_folders[i]) == 0) {
			return i;
		}
	}

	return -1;
}

/*!
 * \internal
 * \brief Get a mock snapshot object
 *
 * \param context The mailbox context
 * \param exten The mailbox extension
 * \param callerid The caller ID of the person leaving the message
 *
 * \returns an ast_vm_msg_snapshot object on success
 * \returns NULL on error
 */
static struct ast_vm_msg_snapshot *test_vm_api_create_mock_snapshot(const char *context, const char *exten, const char *callerid)
{
	char msg_id_hash[AST_MAX_CONTEXT + AST_MAX_EXTENSION + sizeof(callerid) + 1];
	char msg_id_buf[256];
	struct ast_vm_msg_snapshot *snapshot;

	snprintf(msg_id_hash, sizeof(msg_id_hash), "%s%s%s", exten, context, callerid);
	snprintf(msg_id_buf, sizeof(msg_id_buf), "%ld-%d", (long)time(NULL), ast_str_hash(msg_id_hash));

	if ((snapshot = ast_calloc(1, sizeof(*snapshot)))) {
		if (ast_string_field_init(snapshot, 128)) {
			ast_free(snapshot);
			return NULL;
		}
		ast_string_field_set(snapshot, msg_id, msg_id_buf);
		ast_string_field_set(snapshot, exten, exten);
		ast_string_field_set(snapshot, callerid, callerid);
	}
	return snapshot;
}

/*!
 * \internal
 * \brief Destroy a mock snapshot object
 */
static void test_vm_api_destroy_mock_snapshot(struct ast_vm_msg_snapshot *snapshot)
{
	if (snapshot) {
		ast_string_field_free_memory(snapshot);
		ast_free(snapshot);
	}
}

/*!
 * \internal
 * \brief Make a voicemail mailbox folder based on the values provided in a message snapshot
 *
 * \param snapshot The snapshot containing the information to create the folder from
 *
 * \returns 0 on success
 * \returns 1 on failure
 */
static int test_vm_api_create_voicemail_folder(const char *folder_path)
{
	mode_t mode = VOICEMAIL_DIR_MODE;
	int res;

	if ((res = ast_mkdir(folder_path, mode))) {
		ast_log(AST_LOG_ERROR, "ast_mkdir '%s' failed: %s\n", folder_path, strerror(res));
		return 1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Create the voicemail files specified by a snapshot
 *
 * \param context The context of the mailbox
 * \param mailbox The actual mailbox
 * \param snapshot The message snapshot object containing the relevant envelope data
 *
 * \note This will symbolic link the sound file 'beep.gsm' to act as the 'sound' portion of the voicemail.
 * Certain actions in app_voicemail will fail if an actual sound file does not exist
 *
 * \returns 0 on success
 * \returns 1 on any failure
 */
static int test_vm_api_create_voicemail_files(const char *context, const char *mailbox, struct ast_vm_msg_snapshot *snapshot)
{
	FILE *msg_file;
	char folder_path[PATH_MAX];
	char msg_path[PATH_MAX];
	char snd_path[PATH_MAX];
	char beep_path[PATH_MAX];

	/* Note that we create both the text and a dummy sound file here.  Without
	 * the sound file, a number of the voicemail operations 'silently' fail, as it
	 * does not believe that an actual voicemail exists
	 */
	snprintf(folder_path, sizeof(folder_path), "%s/voicemail/%s/%s/%s",
		ast_config_AST_SPOOL_DIR, context, mailbox, snapshot->folder_name);
	snprintf(msg_path, sizeof(msg_path), "%s/msg%04u.txt",
		folder_path, snapshot->msg_number);
	snprintf(snd_path, sizeof(snd_path), "%s/msg%04u.gsm",
		folder_path, snapshot->msg_number);
	snprintf(beep_path, sizeof(beep_path), "%s/sounds/en/beep.gsm", ast_config_AST_DATA_DIR);

	if (test_vm_api_create_voicemail_folder(folder_path)) {
		return 1;
	}

	if (ast_lock_path(folder_path) == AST_LOCK_FAILURE) {
		ast_log(AST_LOG_ERROR, "Unable to lock directory %s\n", folder_path);
		return 1;
	}

	if (symlink(beep_path, snd_path)) {
		ast_unlock_path(folder_path);
		ast_log(AST_LOG_ERROR, "Failed to create a symbolic link from %s to %s: %s\n",
			beep_path, snd_path, strerror(errno));
		return 1;
	}

	if (!(msg_file = fopen(msg_path, "w"))) {
		/* Attempt to remove the sound file */
		unlink(snd_path);
		ast_unlock_path(folder_path);
		ast_log(AST_LOG_ERROR, "Failed to open %s for writing\n", msg_path);
		return 1;
	}

	fprintf(msg_file, ";\n; Message Information file\n;\n"
		"[message]\n"
		"origmailbox=%s\n"
		"context=%s\n"
		"macrocontext=%s\n"
		"exten=%s\n"
		"rdnis=%s\n"
		"priority=%d\n"
		"callerchan=%s\n"
		"callerid=%s\n"
		"origdate=%s\n"
		"origtime=%s\n"
		"category=%s\n"
		"msg_id=%s\n"
		"flag=%s\n"
		"duration=%s\n",
		mailbox,
		context,
		"",
		snapshot->exten,
		"unknown",
		1,
		snapshot->callerchan,
		snapshot->callerid,
		snapshot->origdate,
		snapshot->origtime,
		"",
		snapshot->msg_id,
		snapshot->flag,
		snapshot->duration);
	fclose(msg_file);

	if (chmod(msg_path, VOICEMAIL_FILE_MODE) < 0) {
		ast_unlock_path(folder_path);
		ast_log(AST_LOG_ERROR, "Couldn't set permissions on voicemail text file %s: %s", msg_path, strerror(errno));
		return 1;
	}
	ast_unlock_path(folder_path);

	return 0;
}

/*!
 * \internal
 * \brief Destroy the voicemail on the file system associated with a snapshot
 *
 * \param snapshot The snapshot describing the voicemail
 */
static void test_vm_api_remove_voicemail(struct ast_vm_msg_snapshot *snapshot)
{
	char msg_path[PATH_MAX];
	char snd_path[PATH_MAX];
	char folder_path[PATH_MAX];

	if (!snapshot) {
		return;
	}

	snprintf(folder_path, sizeof(folder_path), "%s/voicemail/%s/%s/%s",
		ast_config_AST_SPOOL_DIR, "default", snapshot->exten, snapshot->folder_name);

	snprintf(msg_path, sizeof(msg_path), "%s/msg%04u.txt",
			folder_path, snapshot->msg_number);
	snprintf(snd_path, sizeof(snd_path), "%s/msg%04u.gsm",
			folder_path, snapshot->msg_number);
	unlink(msg_path);
	unlink(snd_path);

	return;
}

/*!
 * \internal
 * \brief Destroy the voicemails associated with a mailbox snapshot
 *
 * \param mailbox The actual mailbox name
 * \param mailbox_snapshot The mailbox snapshot containing the voicemails to destroy
 *
 * \note It is necessary to specify not just the snapshot, but the mailbox itself.  The
 * message snapshots contained in the snapshot may have originated from a different mailbox
 * then the one we're destroying, which means that we can't determine the files to delete
 * without knowing the actual mailbox they exist in.
 */
static void test_vm_api_destroy_mailbox_voicemails(const char *mailbox, struct ast_vm_mailbox_snapshot *mailbox_snapshot)
{
	struct ast_vm_msg_snapshot *msg;
	int i;

	for (i = 0; i < 12; ++i) {
		AST_LIST_TRAVERSE(&mailbox_snapshot->snapshots[i], msg, msg) {
			ast_string_field_set(msg, exten, mailbox);
			test_vm_api_remove_voicemail(msg);
		}
	}
}

/*!
 * \internal
 * \brief Use snapshots to remove all messages in the mailboxes
 */
static void test_vm_api_remove_all_messages(void)
{
	struct ast_vm_mailbox_snapshot *mailbox_snapshot;

	/* Take a snapshot of each mailbox and remove the contents.  Note that we need to use
	 * snapshots of the mailboxes in addition to our tracked test snapshots, as there's a good chance
	 * we've created copies of the snapshots */
	if ((mailbox_snapshot = ast_vm_mailbox_snapshot_create("test_vm_api_1234", "default", NULL, 0, AST_VM_SNAPSHOT_SORT_BY_ID, 0))) {
		test_vm_api_destroy_mailbox_voicemails("test_vm_api_1234", mailbox_snapshot);
		mailbox_snapshot = ast_vm_mailbox_snapshot_destroy(mailbox_snapshot);
	} else {
		ast_log(AST_LOG_WARNING, "Failed to create mailbox snapshot - could not remove test messages for test_vm_api_1234\n");
	}
	if ((mailbox_snapshot = ast_vm_mailbox_snapshot_create("test_vm_api_2345", "default", NULL, 0, AST_VM_SNAPSHOT_SORT_BY_ID, 0))) {
		test_vm_api_destroy_mailbox_voicemails("test_vm_api_2345", mailbox_snapshot);
		mailbox_snapshot = ast_vm_mailbox_snapshot_destroy(mailbox_snapshot);
	} else {
		ast_log(AST_LOG_WARNING, "Failed to create mailbox snapshot - could not remove test messages for test_vm_api_2345\n");
	}
}

/*!
 * \internal
 * \brief Set up the necessary voicemails for a unit test run
 *
 * \details
 * This creates 4 voicemails, stores them on the file system, and creates snapshot objects
 * representing them for expected/actual value comparisons in the array test_snapshots.
 *
 * test_snapshots[0] => in test_vm_1234@default, folder INBOX, message 0
 * test_snapshots[1] => in test_vm_1234@default, folder Old, message 0
 * test_snapshots[2] => in test_vm_2345@default, folder INBOX, message 0
 * test_snapshots[3] => in test_vm_2345@default, folder Old, message 1
 *
 * \returns 0 on success
 * \returns 1 on failure
 */
static int test_vm_api_test_setup(void)
{
	int i, res = 0;
	struct ast_vm_msg_snapshot *msg_one = NULL;
	struct ast_vm_msg_snapshot *msg_two = NULL;
	struct ast_vm_msg_snapshot *msg_three = NULL;
	struct ast_vm_msg_snapshot *msg_four = NULL;

	/* Make the four sample voicemails */
	if (   !((msg_one = test_vm_api_create_mock_snapshot("default", "test_vm_api_1234", "\"Phil\" <2000>")))
		|| !((msg_two = test_vm_api_create_mock_snapshot("default", "test_vm_api_1234", "\"Noel\" <8000>")))
		|| !((msg_three = test_vm_api_create_mock_snapshot("default", "test_vm_api_2345", "\"Phil\" <2000>")))
		|| !((msg_four = test_vm_api_create_mock_snapshot("default", "test_vm_api_2345", "\"Bill\" <3000>")))) {
		ast_log(AST_LOG_ERROR, "Failed to create mock snapshots for test\n");
		test_vm_api_destroy_mock_snapshot(msg_one);
		test_vm_api_destroy_mock_snapshot(msg_two);
		test_vm_api_destroy_mock_snapshot(msg_three);
		test_vm_api_destroy_mock_snapshot(msg_four);
		return 1;
	}

	/* Create the voicemail users */
	if (ast_vm_test_create_user("default", "test_vm_api_1234")
		|| ast_vm_test_create_user("default", "test_vm_api_2345")) {
		ast_log(AST_LOG_ERROR, "Failed to create test voicemail users\n");
		test_vm_api_destroy_mock_snapshot(msg_one);
		test_vm_api_destroy_mock_snapshot(msg_two);
		test_vm_api_destroy_mock_snapshot(msg_three);
		test_vm_api_destroy_mock_snapshot(msg_four);
		/* Note that the cleanup macro will ensure that any test user that
		 * was successfully created is removed
		 */
		return 1;
	}

	/* Now that the users exist from the perspective of the voicemail
	 * application, attempt to remove any existing voicemails
	 */
	test_vm_api_remove_all_messages();

	/* Set the basic properties on each */
	ast_string_field_set(msg_one, callerchan, "SIP/2000-00000000");
	ast_string_field_set(msg_one, origdate, "Mon Mar 19 04:14:21 PM UTC 2012");
	ast_string_field_set(msg_one, origtime, "1332173661");
	ast_string_field_set(msg_one, duration, "8");
	ast_string_field_set(msg_one, folder_name, "Old");
	msg_one->msg_number = 0;
	test_snapshots[0] = msg_one;

	ast_string_field_set(msg_two, callerchan, "SIP/8000-00000001");
	ast_string_field_set(msg_two, origdate, "Mon Mar 19 06:16:13 PM UTC 2012");
	ast_string_field_set(msg_two, origtime, "1332180973");
	ast_string_field_set(msg_two, duration, "24");
	ast_string_field_set(msg_two, folder_name, "INBOX");
	msg_two->msg_number = 0;
	test_snapshots[1] = msg_two;

	ast_string_field_set(msg_three, callerchan, "IAX/2000-000000a3");
	ast_string_field_set(msg_three, origdate, "Thu Mar 22 23:13:03 PM UTC 2012");
	ast_string_field_set(msg_three, origtime, "1332181251");
	ast_string_field_set(msg_three, duration, "25");
	ast_string_field_set(msg_three, folder_name, "INBOX");
	msg_three->msg_number = 0;
	test_snapshots[2] = msg_three;

	ast_string_field_set(msg_four, callerchan, "DAHDI/3000-00000010");
	ast_string_field_set(msg_four, origdate, "Fri Mar 23 03:01:03 AM UTC 2012");
	ast_string_field_set(msg_four, origtime, "1332181362");
	ast_string_field_set(msg_four, duration, "13");
	ast_string_field_set(msg_four, folder_name, "INBOX");
	msg_three->msg_number = 1;
	test_snapshots[3] = msg_four;

	/* Store the messages */
	for (i = 0; i < TOTAL_SNAPSHOTS; ++i) {
		if (test_vm_api_create_voicemail_files("default", test_snapshots[i]->exten, test_snapshots[i])) {
			/* On a failure, the test_vm_api_test_teardown method will remove and
			 * unlink any created files. Since we failed to create the file, clean
			 * up the object here instead */
			ast_log(AST_LOG_ERROR, "Failed to store voicemail %s/%s\n",
				"default", test_snapshots[i]->exten);
			test_vm_api_destroy_mock_snapshot(test_snapshots[i]);
			test_snapshots[i] = NULL;
			res = 1;
		}
	}

	return res;
}

static void test_vm_api_test_teardown(void)
{
	int i;

	/* Remove our test message snapshots */
	for (i = 0; i < TOTAL_SNAPSHOTS; ++i) {
		test_vm_api_remove_voicemail(test_snapshots[i]);
		test_vm_api_destroy_mock_snapshot(test_snapshots[i]);
		test_snapshots[i] = NULL;
	}

	test_vm_api_remove_all_messages();

	/* Remove the test users */
	ast_vm_test_destroy_user("default", "test_vm_api_1234");
	ast_vm_test_destroy_user("default", "test_vm_api_2345");
}

/*!
 * \internal
 * \brief Update the test snapshots with a new mailbox snapshot
 *
 * \param mailbox_snapshot The new mailbox shapshot to update the test snapshots with
 */
static void test_vm_api_update_test_snapshots(struct ast_vm_mailbox_snapshot *mailbox_snapshot)
{
	int i, j;
	struct ast_vm_msg_snapshot *msg;

	for (i = 0; i < TOTAL_SNAPSHOTS; ++i) {
		for (j = 0; j < 12; ++j) {
			AST_LIST_TRAVERSE(&mailbox_snapshot->snapshots[j], msg, msg) {
				if (!strcmp(msg->msg_id, test_snapshots[i]->msg_id)) {
					ast_string_field_set(test_snapshots[i], callerid, msg->callerid);
					ast_string_field_set(test_snapshots[i], callerchan, msg->callerchan);
					ast_string_field_set(test_snapshots[i], exten, msg->exten);
					ast_string_field_set(test_snapshots[i], origdate, msg->origdate);
					ast_string_field_set(test_snapshots[i], origtime, msg->origtime);
					ast_string_field_set(test_snapshots[i], duration, msg->duration);
					ast_string_field_set(test_snapshots[i], folder_name, msg->folder_name);
					ast_string_field_set(test_snapshots[i], flag, msg->flag);
					test_snapshots[i]->msg_number = msg->msg_number;
				}
			}
		}
	}
}

/*!
 * \internal
 * \brief A callback function for message playback
 *
 * \param chan The channel the file would be played back on
 * \param file The file to play back
 * \param duration The duration of the file
 *
 * \note This sets global_entered_playback_callback to 1 if the parameters
 * passed to the callback are minimally valid
 */
static void message_playback_callback_fn(struct ast_channel *chan, const char *file, int duration)
{
	if ((chan) && !ast_strlen_zero(file) && duration > 0) {
		global_entered_playback_callback = 1;
	} else {
		ast_log(AST_LOG_WARNING, "Entered into message playback callback function with invalid parameters\n");
	}
}

/*!
 * \internal
 * \brief Dummy channel write function for mock_channel_tech
 */
static int test_vm_api_mock_channel_write(struct ast_channel *chan, struct ast_frame *frame)
{
	return 0;
}

/*!
 * \internal
 * \brief Dummy channel read function for mock_channel_tech
 */
static struct ast_frame *test_vm_api_mock_channel_read(struct ast_channel *chan)
{
	return &ast_null_frame;
}

/*!
 * \internal
 * \brief A dummy channel technology
 */
static const struct ast_channel_tech mock_channel_tech = {
		.write = test_vm_api_mock_channel_write,
		.read = test_vm_api_mock_channel_read,
};

/*!
 * \internal
 * \brief Create a dummy channel suitable for 'playing back' gsm sound files on
 *
 * \returns a channel on success
 * \returns NULL on failure
 */
static struct ast_channel *test_vm_api_create_mock_channel(void)
{
	struct ast_channel *mock_channel;
	struct ast_format_cap *native_formats;

	if (!(mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel"))) {
		return NULL;
	}

	ast_channel_set_writeformat(mock_channel, ast_format_gsm);
	ast_channel_set_rawwriteformat(mock_channel, ast_format_gsm);
	ast_channel_set_readformat(mock_channel, ast_format_gsm);
	ast_channel_set_rawreadformat(mock_channel, ast_format_gsm);
	ast_channel_tech_set(mock_channel, &mock_channel_tech);
	native_formats = ast_channel_nativeformats(mock_channel);
	ast_format_cap_append(native_formats, ast_channel_writeformat(mock_channel), 0);

	ast_channel_unlock(mock_channel);

	return mock_channel;
}

AST_TEST_DEFINE(voicemail_api_nominal_snapshot)
{
	struct ast_vm_mailbox_snapshot *test_mbox_snapshot = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "nominal_snapshot";
		info->category = "/main/voicemail_api/";
		info->summary = "Nominal mailbox snapshot tests";
		info->description =
			"Test retrieving mailbox snapshots";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	ast_test_status_update(test, "Test retrieving message 1 from INBOX of test_vm_1234\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(1, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 0);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 0 from Old of test_vm_1234\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "Old", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(1, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "Old", 0);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 0, 1 from Old and INBOX of test_vm_1234 ordered by time\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 1);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "INBOX", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 1);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 1, 0 from Old and INBOX of test_vm_1234 ordered by time desc\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 1, AST_VM_SNAPSHOT_SORT_BY_TIME, 1);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "INBOX", 1);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 0, 1 from Old and INBOX of test_vm_1234 ordered by id\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_ID, 1);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "INBOX", 1);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 1, 0 from Old and INBOX of test_vm_1234 ordered by id desc\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 1, AST_VM_SNAPSHOT_SORT_BY_ID, 1);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "INBOX", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 1);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 0, 1 from all folders of test_vm_1234 ordered by id\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", NULL, 0, AST_VM_SNAPSHOT_SORT_BY_ID, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "Old", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 0);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 0, 1 from all folders of test_vm_1234 ordered by time\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", NULL, 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "Old", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 0);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test retrieving message 0, 1 from all folders of test_vm_1234, default context ordered by time\n");
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", NULL, NULL, 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[0], test_mbox_snapshot, "Old", 0);
	VM_API_SNAPSHOT_MSG_VERIFY(test_snapshots[1], test_mbox_snapshot, "INBOX", 0);
	ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_off_nominal_snapshot)
{
	struct ast_vm_mailbox_snapshot *test_mbox_snapshot = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "off_nominal_snapshot";
		info->category = "/main/voicemail_api/";
		info->summary = "Off nominal mailbox snapshot tests";
		info->description =
			"Test off nominal requests for mailbox snapshots.  This includes"
			" testing the following:\n"
			" * Access to non-exisstent mailbox\n"
			" * Access to NULL mailbox\n"
			" * Access to non-existent context\n"
			" * Access to non-existent folder\n"
			" * Access to NULL folder\n"
			" * Invalid sort identifier";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	ast_test_status_update(test, "Test access to non-existent mailbox test_vm_api_3456\n");
	VM_API_SNAPSHOT_OFF_NOMINAL_TEST("test_vm_api_3456", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);

	ast_test_status_update(test, "Test access to null mailbox\n");
	VM_API_SNAPSHOT_OFF_NOMINAL_TEST(NULL, "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);

	ast_test_status_update(test, "Test access non-existent context test_vm_api_defunct\n");
	VM_API_SNAPSHOT_OFF_NOMINAL_TEST("test_vm_api_1234", "test_vm_api_defunct", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);

	ast_test_status_update(test, "Test non-existent folder test_vm_api_platypus\n");
	VM_API_SNAPSHOT_OFF_NOMINAL_TEST("test_vm_api_1234", "default", "test_vm_api_platypus", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_nominal_move)
{
	struct ast_vm_mailbox_snapshot *test_mbox_snapshot = NULL;
	const char *inbox_msg_id;
	const char *old_msg_id;
	const char *multi_msg_ids[2];

	switch (cmd) {
	case TEST_INIT:
		info->name = "nominal_move";
		info->category = "/main/voicemail_api/";
		info->summary = "Nominal move voicemail tests";
		info->description =
			"Test nominal requests to move a voicemail to a different"
			" folder.  This includes moving messages given a context,"
			" given a NULL context, and moving multiple messages";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;
	old_msg_id = test_snapshots[0]->msg_id;
	inbox_msg_id = test_snapshots[1]->msg_id;

	multi_msg_ids[0] = test_snapshots[2]->msg_id;
	multi_msg_ids[1] = test_snapshots[3]->msg_id;

	ast_test_status_update(test, "Test move of test_vm_api_1234 message from INBOX to Family\n");
	VM_API_MOVE_MESSAGE("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move of test_vm_api_1234 message from Old to Family\n");
	VM_API_MOVE_MESSAGE("test_vm_api_1234", NULL, 1, "Old", &old_msg_id, "Family");

	/* Take a snapshot and update the test snapshots for verification */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "Family", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	test_vm_api_update_test_snapshots(test_mbox_snapshot);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	VM_API_STRING_FIELD_VERIFY("Family", test_snapshots[0]->folder_name);
	VM_API_STRING_FIELD_VERIFY("Family", test_snapshots[1]->folder_name);
	VM_API_INT_VERIFY(0, test_snapshots[1]->msg_number);
	VM_API_INT_VERIFY(1, test_snapshots[0]->msg_number);

	/* Move both of the 2345 messages to Family */
	ast_test_status_update(test, "Test move of test_vm_api_2345 messages from Inbox to Family\n");
	VM_API_MOVE_MESSAGE("test_vm_api_2345", "default", 2, "INBOX", multi_msg_ids, "Family");

	/* Take a snapshot and update the test snapshots for verification */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "Family", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	test_vm_api_update_test_snapshots(test_mbox_snapshot);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	VM_API_STRING_FIELD_VERIFY("Family", test_snapshots[2]->folder_name);
	VM_API_STRING_FIELD_VERIFY("Family", test_snapshots[3]->folder_name);

	ast_test_status_update(test, "Test move of test_vm_api_2345 message from Family to INBOX\n");
	VM_API_MOVE_MESSAGE("test_vm_api_2345", "default", 2, "Family", multi_msg_ids, "INBOX");

	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	test_vm_api_update_test_snapshots(test_mbox_snapshot);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	VM_API_STRING_FIELD_VERIFY("INBOX", test_snapshots[2]->folder_name);
	VM_API_STRING_FIELD_VERIFY("INBOX", test_snapshots[3]->folder_name);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_off_nominal_move)
{
	const char *inbox_msg_id;
	const char *multi_msg_ids[4];

	switch (cmd) {
	case TEST_INIT:
		info->name = "off_nominal_move";
		info->category = "/main/voicemail_api/";
		info->summary = "Off nominal mailbox message move tests";
		info->description =
			"Test nominal requests to move a voicemail to a different"
			" folder.  This includes testing the following:\n"
			" * Moving to a non-existent mailbox\n"
			" * Moving to a NULL mailbox\n"
			" * Moving to a non-existent context\n"
			" * Moving to/from non-existent folder\n"
			" * Moving to/from NULL folder\n"
			" * Invalid message identifier(s)";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	inbox_msg_id = test_snapshots[1]->msg_id;

	multi_msg_ids[0] = test_snapshots[0]->msg_id;
	multi_msg_ids[1] = test_snapshots[1]->msg_id;
	multi_msg_ids[2] = test_snapshots[2]->msg_id;
	multi_msg_ids[3] = test_snapshots[3]->msg_id;

	ast_test_status_update(test, "Test move attempt for invalid mailbox test_vm_3456\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_3456", "default", 1, "INBOX", &inbox_msg_id, "Family");

	VM_API_MOVE_MESSAGE_OFF_NOMINAL(NULL, "default", 1, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt for invalid context test_vm_api_defunct\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "test_vm_api_defunct", 1, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt to invalid folder\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id, "SPAMALOT");

	ast_test_status_update(test, "Test move attempt from invalid folder\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "MEATINACAN", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt to NULL folder\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id, NULL);

	ast_test_status_update(test, "Test move attempt from NULL folder\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, NULL, &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt with non-existent message number\n");
	inbox_msg_id = "6";
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt with invalid message number\n");
	inbox_msg_id = "";
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt with 0 number of messages\n");
	inbox_msg_id = test_snapshots[1]->msg_id;
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 0, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt with invalid number of messages\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", -30, "INBOX", &inbox_msg_id, "Family");

	ast_test_status_update(test, "Test move attempt with non-existent multiple messages, where some messages exist\n");
	VM_API_MOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 4, "INBOX", multi_msg_ids, "Family");

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_nominal_remove)
{
	struct ast_vm_mailbox_snapshot *test_mbox_snapshot = NULL;
	const char *inbox_msg_id;
	const char *old_msg_id;
	const char *multi_msg_ids[2];

	switch (cmd) {
	case TEST_INIT:
		info->name = "nominal_remove";
		info->category = "/main/voicemail_api/";
		info->summary = "Nominal mailbox remove message tests";
		info->description =
			"Tests removing messages from voicemail folders.  Includes"
			" both removing messages one at a time, and in a set";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	old_msg_id = test_snapshots[0]->msg_id;
	inbox_msg_id = test_snapshots[1]->msg_id;

	multi_msg_ids[0] = test_snapshots[2]->msg_id;
	multi_msg_ids[1] = test_snapshots[3]->msg_id;

	ast_test_status_update(test, "Test removing a single message from INBOX\n");
	VM_API_REMOVE_MESSAGE("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id);

	ast_test_status_update(test, "Test removing a single message from Old\n");
	VM_API_REMOVE_MESSAGE("test_vm_api_1234", "default", 1, "Old", &old_msg_id);

	ast_test_status_update(test, "Test removing multiple messages from INBOX\n");
	VM_API_REMOVE_MESSAGE("test_vm_api_2345", "default", 2, "INBOX", multi_msg_ids);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_off_nominal_remove)
{
	const char *inbox_msg_id;
	const char *multi_msg_ids[2];
	const char *empty_msg_ids[] = { };

	switch (cmd) {
	case TEST_INIT:
		info->name = "off_nominal_remove";
		info->category = "/main/voicemail_api/";
		info->summary = "Off nominal mailbox message removal tests";
		info->description =
			"Test off nominal requests for removing messages from "
			"a mailbox.  This includes:\n"
			" * Removing messages with an invalid mailbox\n"
			" * Removing messages from a NULL mailbox\n"
			" * Removing messages from an invalid context\n"
			" * Removing messages from an invalid folder\n"
			" * Removing messages from a NULL folder\n"
			" * Removing messages with bad identifiers";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	inbox_msg_id = test_snapshots[1]->msg_id;
	multi_msg_ids[0] = test_snapshots[2]->msg_id;
	multi_msg_ids[1] = test_snapshots[3]->msg_id;

	ast_test_status_update(test, "Test removing a single message with an invalid mailbox\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_3456", "default", 1, "INBOX", &inbox_msg_id);

	ast_test_status_update(test, "Test removing a single message with a NULL mailbox\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL(NULL, "default", 1, "INBOX", &inbox_msg_id);

	ast_test_status_update(test, "Test removing a single message with an invalid context\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "defunct", 1, "INBOX", &inbox_msg_id);

	ast_test_status_update(test, "Test removing a single message with an invalid folder\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "SPAMINACAN", &inbox_msg_id);

	ast_test_status_update(test, "Test removing a single message with a NULL folder\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, NULL, &inbox_msg_id);

	ast_test_status_update(test, "Test removing a single message with an invalid message number\n");
	inbox_msg_id = "POOPOO";
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 1, "INBOX", &inbox_msg_id);

	ast_test_status_update(test, "Test removing multiple messages with a single invalid message number\n");
	multi_msg_ids[1] = "POOPOO";
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_2345", "default", 2, "INBOX", multi_msg_ids);

	ast_test_status_update(test, "Test removing no messages with no message numbers\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", 0, "INBOX", empty_msg_ids);

	ast_test_status_update(test, "Test removing multiple messages with an invalid size specifier\n");
	VM_API_REMOVE_MESSAGE_OFF_NOMINAL("test_vm_api_2345", "default", -30, "INBOX", multi_msg_ids);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_nominal_forward)
{
	struct ast_vm_mailbox_snapshot *test_mbox_snapshot = NULL;
	const char *inbox_msg_id;
	const char *multi_msg_ids[2];

	switch (cmd) {
	case TEST_INIT:
		info->name = "nominal_forward";
		info->category = "/main/voicemail_api/";
		info->summary = "Nominal message forward tests";
		info->description =
			"Tests the nominal cases of forwarding messages"
			" between mailboxes";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	inbox_msg_id = test_snapshots[1]->msg_id;

	multi_msg_ids[0] = test_snapshots[2]->msg_id;
	multi_msg_ids[1] = test_snapshots[3]->msg_id;

	ast_test_status_update(test, "Test forwarding message 0 from test_vm_api_1234 INBOX to test_vm_api_2345 INBOX\n");
	VM_API_FORWARD_MESSAGE("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	/* Make sure we didn't delete the message */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(1, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	/* We should now have a total of 3 messages in test_vm_api_2345 INBOX */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(3, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test forwarding message 0 from test_vm_api_1234 INBOX with default context to test_vm_api_2345 INBOX\n");
	VM_API_FORWARD_MESSAGE("test_vm_api_1234", NULL, "INBOX", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	/* Make sure we didn't delete the message */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(1, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	/* We should now have a total of 4 messages in test_vm_api_2345 INBOX */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(4, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test forwarding message 0 from test_vm_api_1234 INBOX to test_vm_api_2345 INBOX with default context\n");
	VM_API_FORWARD_MESSAGE("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", NULL, "INBOX", 1, &inbox_msg_id, 0);

	/* Make sure we didn't delete the message */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(1, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	/* We should now have a total of 5 messages in test_vm_api_2345 INBOX */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(5, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test forwarding message 0 from test_vm_api_1234 INBOX to test_vm_api_2345 INBOX, deleting original\n");
	VM_API_FORWARD_MESSAGE("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", NULL, "INBOX", 1, &inbox_msg_id, 1);

	/* Make sure we deleted the message */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(0, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	/* We should now have a total of 6 messages in test_vm_api_2345 INBOX */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(6, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test forwarding 2 messages from test_vm_api_2345 INBOX to test_vm_api_1234 INBOX");
	VM_API_FORWARD_MESSAGE("test_vm_api_2345", "default", "INBOX", "test_vm_api_1234", "default", "INBOX", 2, multi_msg_ids, 0);

	/* Make sure we didn't delete the messages */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(6, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	/* We should now have a total of 2 messages in test_vm_api_1234 INBOX */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_test_status_update(test, "Test forwarding 2 messages from test_vm_api_2345 INBOX to test_vm_api_1234 Family, deleting original\n");
	VM_API_FORWARD_MESSAGE("test_vm_api_2345", "default", "INBOX", "test_vm_api_1234", "default", "Family", 2, multi_msg_ids, 1);
	/* Make sure we deleted the messages */
	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "INBOX", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(4, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	/* We should now have a total of 2 messages in test_vm_api_1234 Family */
	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "Family", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_off_nominal_forward)
{
	const char *inbox_msg_id;
	const char *multi_msg_ids[4];

	const char *empty_msg_ids[] = { };

	switch (cmd) {
	case TEST_INIT:
		info->name = "off_nominal_forward";
		info->category = "/main/voicemail_api/";
		info->summary = "Off nominal message forwarding tests";
		info->description =
			"Test off nominal forwarding of messages.  This includes:\n"
			" * Invalid/NULL from mailbox\n"
			" * Invalid from context\n"
			" * Invalid/NULL from folder\n"
			" * Invalid/NULL to mailbox\n"
			" * Invalid to context\n"
			" * Invalid/NULL to folder\n"
			" * Invalid message numbers\n"
			" * Invalid number of messages";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	inbox_msg_id = test_snapshots[1]->msg_id;

	multi_msg_ids[0] = test_snapshots[0]->msg_id;
	multi_msg_ids[1] = test_snapshots[1]->msg_id;
	multi_msg_ids[2] = test_snapshots[2]->msg_id;
	multi_msg_ids[3] = test_snapshots[3]->msg_id;

	ast_test_status_update(test, "Test forwarding from an invalid mailbox\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_3456", "default", "INBOX", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding from a NULL mailbox\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL(NULL, "default", "INBOX", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding from an invalid context\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "defunct", "INBOX", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding from an invalid folder\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "POTTEDMEAT", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding from a NULL folder\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", NULL, "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding to an invalid mailbox\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_3456", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding to a NULL mailbox\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", NULL, "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding to an invalid context\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "defunct", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding to an invalid folder\n");

	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "default", "POTTEDMEAT", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding to a NULL folder\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "default", NULL, 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding when no messages are select\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "default", "INBOX", 0, empty_msg_ids, 0);

	ast_test_status_update(test, "Test forwarding a message that doesn't exist\n");
	inbox_msg_id = "POOPOO";
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "default", "INBOX", 1, &inbox_msg_id, 0);

	ast_test_status_update(test, "Test forwarding multiple messages, where some messages don't exist\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_2345", "default", "INBOX", "test_vm_api_1234", "default", "INBOX", 4, multi_msg_ids, 0);

	ast_test_status_update(test, "Test forwarding a message with an invalid size specifier\n");
	VM_API_FORWARD_MESSAGE_OFF_NOMINAL("test_vm_api_1234", "default", "INBOX", "test_vm_api_2345", "default", "INBOX", -30, &inbox_msg_id, 0);

	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_nominal_msg_playback)
{
	struct ast_vm_mailbox_snapshot *test_mbox_snapshot = NULL;
	struct ast_channel *test_channel;
	const char *message_id_1234;
	const char *message_id_2345[2];

	switch (cmd) {
	case TEST_INIT:
		info->name = "nominal_msg_playback";
		info->category = "/main/voicemail_api/";
		info->summary = "Nominal message playback";
		info->description =
			"Tests playing back a message on a provided"
			" channel or callback function";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;

	message_id_1234 = test_snapshots[1]->msg_id;
	message_id_2345[0] = test_snapshots[2]->msg_id;
	message_id_2345[1] = test_snapshots[3]->msg_id;

	if (!(test_channel = test_vm_api_create_mock_channel())) {
		ast_log(AST_LOG_ERROR, "Failed to create mock channel for testing\n");
		VM_API_TEST_CLEANUP;
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Playing back message from test_vm_api_1234 to mock channel\n");
	VM_API_PLAYBACK_MESSAGE(test_channel, "test_vm_api_1234", "default", "INBOX", message_id_1234, NULL);

	ast_test_status_update(test, "Playing back message from test_vm_api_2345 to callback function\n");
	VM_API_PLAYBACK_MESSAGE(test_channel, "test_vm_api_2345", "default", "INBOX", message_id_2345[0], &message_playback_callback_fn);
	VM_API_INT_VERIFY(1, global_entered_playback_callback);
	global_entered_playback_callback = 0;

	ast_test_status_update(test, "Playing back message from test_vm_api_2345 to callback function with default context\n");
	VM_API_PLAYBACK_MESSAGE(test_channel, "test_vm_api_2345", NULL, "INBOX", message_id_2345[1], &message_playback_callback_fn);
	VM_API_INT_VERIFY(1, global_entered_playback_callback);
	global_entered_playback_callback = 0;

	VM_API_SNAPSHOT_CREATE("test_vm_api_1234", "default", "Old", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	VM_API_SNAPSHOT_CREATE("test_vm_api_2345", "default", "Old", 0, AST_VM_SNAPSHOT_SORT_BY_TIME, 0);
	VM_API_INT_VERIFY(2, test_mbox_snapshot->total_msg_num);
	test_mbox_snapshot = ast_vm_mailbox_snapshot_destroy(test_mbox_snapshot);

	ast_hangup(test_channel);
	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(voicemail_api_off_nominal_msg_playback)
{
	struct ast_channel *test_channel;
	const char *msg_id;
	const char *invalid_msg_id = "POOPOO";

	switch (cmd) {
	case TEST_INIT:
		info->name = "off_nominal_msg_playback";
		info->category = "/main/voicemail_api/";
		info->summary = "Off nominal message playback";
		info->description =
			"Tests off nominal conditions in playing back a "
			"message.  This includes:\n"
			" * Invalid/NULL mailbox\n"
			" * Invalid context\n"
			" * Invalid/NULL folder\n"
			" * Invalid message identifiers";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	VM_API_TEST_SETUP;
	msg_id = test_snapshots[0]->msg_id;

	if (!(test_channel = test_vm_api_create_mock_channel())) {
		ast_log(AST_LOG_ERROR, "Failed to create mock channel for testing\n");
		VM_API_TEST_CLEANUP;
		return AST_TEST_FAIL;
	}

	ast_test_status_update(test, "Playing back message from invalid mailbox\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, "test_vm_api_3456", "default", "INBOX", msg_id, NULL);

	ast_test_status_update(test, "Playing back message from NULL mailbox\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, NULL, "default", "INBOX", msg_id, NULL);

	ast_test_status_update(test, "Playing back message from invalid context\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, "test_vm_api_1234", "defunct", "INBOX", msg_id, NULL);

	ast_test_status_update(test, "Playing back message from invalid folder\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, "test_vm_api_1234", "default", "BACON", msg_id, NULL);

	ast_test_status_update(test, "Playing back message from NULL folder\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, "test_vm_api_1234", "default",  NULL, msg_id, NULL);

	ast_test_status_update(test, "Playing back message with invalid message specifier\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, "test_vm_api_1234", "default", "INBOX", invalid_msg_id, NULL);

	ast_test_status_update(test, "Playing back message with NULL message specifier\n");
	VM_API_PLAYBACK_MESSAGE_OFF_NOMINAL(test_channel, "test_vm_api_1234", "default", "INBOX", NULL, NULL);
	ast_hangup(test_channel);
	VM_API_TEST_CLEANUP;

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	/* Snapshot tests */
	AST_TEST_UNREGISTER(voicemail_api_nominal_snapshot);
	AST_TEST_UNREGISTER(voicemail_api_off_nominal_snapshot);

	/* Move Tests */
	AST_TEST_UNREGISTER(voicemail_api_nominal_move);
	AST_TEST_UNREGISTER(voicemail_api_off_nominal_move);

	/* Remove Tests */
	AST_TEST_UNREGISTER(voicemail_api_nominal_remove);
	AST_TEST_UNREGISTER(voicemail_api_off_nominal_remove);

	/* Forward Tests */
	AST_TEST_UNREGISTER(voicemail_api_nominal_forward);
	AST_TEST_UNREGISTER(voicemail_api_off_nominal_forward);

	/* Message Playback Tests */
	AST_TEST_UNREGISTER(voicemail_api_nominal_msg_playback);
	AST_TEST_UNREGISTER(voicemail_api_off_nominal_msg_playback);
	return 0;
}

static int load_module(void)
{
	/* Snapshot tests */
	AST_TEST_REGISTER(voicemail_api_nominal_snapshot);
	AST_TEST_REGISTER(voicemail_api_off_nominal_snapshot);

	/* Move Tests */
	AST_TEST_REGISTER(voicemail_api_nominal_move);
	AST_TEST_REGISTER(voicemail_api_off_nominal_move);

	/* Remove Tests */
	AST_TEST_REGISTER(voicemail_api_nominal_remove);
	AST_TEST_REGISTER(voicemail_api_off_nominal_remove);

	/* Forward Tests */
	AST_TEST_REGISTER(voicemail_api_nominal_forward);
	AST_TEST_REGISTER(voicemail_api_off_nominal_forward);

	/* Message Playback Tests */
	AST_TEST_REGISTER(voicemail_api_nominal_msg_playback);
	AST_TEST_REGISTER(voicemail_api_off_nominal_msg_playback);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Core Voicemail API Tests");
