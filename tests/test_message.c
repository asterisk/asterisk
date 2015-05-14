/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
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

/*! \file
 *
 * \brief Test module for out-of-call text message module
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/message.h"
#include "asterisk/pbx.h"
#include "asterisk/manager.h"
#include "asterisk/vector.h"

#define TEST_CATEGORY "/main/message/"

#define TEST_CONTEXT "__TEST_MESSAGE_CONTEXT__"
#define TEST_EXTENSION "test_message_extension"

/*! \brief The number of user events we should get in a dialplan test */
#define DEFAULT_EXPECTED_EVENTS 4

/*! \brief The current number of received user events */
static int received_user_events;

/*! \brief The number of user events we expect for this test */
static int expected_user_events;

/*! \brief Predicate for the \ref test_message_handler receiving a message */
static int handler_received_message;

/*! \brief Condition wait variable for all dialplan user events being received */
static ast_cond_t user_event_cond;

/*! \brief Mutex for \c user_event_cond */
AST_MUTEX_DEFINE_STATIC(user_event_lock);

/*! \brief Condition wait variable for \ref test_msg_handler receiving message */
static ast_cond_t handler_cond;

/*! \brief Mutex for \c handler_cond */
AST_MUTEX_DEFINE_STATIC(handler_lock);

/*! \brief The expected user event fields */
AST_VECTOR(var_vector, struct ast_variable *) expected_user_event_fields;

/*! \brief If a user event fails, the bad headers that didn't match */
AST_VECTOR(, struct ast_variable *) bad_headers;

static int test_msg_send(const struct ast_msg *msg, const char *to, const char *from);

static struct ast_msg_tech test_msg_tech = {
	.name = "testmsg",
	.msg_send = test_msg_send,
};

static int test_msg_handle_msg_cb(struct ast_msg *msg);
static int test_msg_has_destination_cb(const struct ast_msg *msg);

/*! \brief Our test message handler */
static struct ast_msg_handler test_msg_handler = {
	.name = "testmsg",
	.handle_msg = test_msg_handle_msg_cb,
	.has_destination = test_msg_has_destination_cb,
};

static int user_event_hook_cb(int category, const char *event, char *body);

/*! \brief AMI event hook that verifies whether or not we've gotten our user events */
static struct manager_custom_hook user_event_hook = {
	.file = AST_MODULE,
	.helper = user_event_hook_cb,
};

/*!
 * \brief Verifies a user event header/value pair
 *
 * \param user_event which user event to check
 * \param header The header to verify
 * \param value The value read from the event
 *
 * \retval -1 on error or evaluation failure
 * \retval 0 if match not needed or success
 */
static int verify_user_event_fields(int user_event, const char *header, const char *value)
{
	struct ast_variable *current;
	struct ast_variable *expected;
	regex_t regexbuf;
	int error;

	if (user_event >= AST_VECTOR_SIZE(&expected_user_event_fields)) {
		return -1;
	}

	expected = AST_VECTOR_GET(&expected_user_event_fields, user_event);
	if (!expected) {
		return -1;
	}

	for (current = expected; current; current = current->next) {
		struct ast_variable *bad_header;

		if (strcmp(current->name, header)) {
			continue;
		}

		error = regcomp(&regexbuf, current->value, REG_EXTENDED | REG_NOSUB);
		if (error) {
			char error_buf[128];
			regerror(error, &regexbuf, error_buf, sizeof(error_buf));
			ast_log(LOG_ERROR, "Failed to compile regex '%s' for header check '%s': %s\n",
				current->value, current->name, error_buf);
			return -1;
		}

		if (!regexec(&regexbuf, value, 0, NULL, 0)) {
			regfree(&regexbuf);
			return 0;
		}

		bad_header = ast_variable_new(header, value, __FILE__);
		if (bad_header) {
			struct ast_variable *bad_headers_head = NULL;

			if (user_event < AST_VECTOR_SIZE(&bad_headers)) {
				bad_headers_head = AST_VECTOR_GET(&bad_headers, user_event);
			}
			ast_variable_list_append(&bad_headers_head, bad_header);
			AST_VECTOR_REPLACE(&bad_headers, user_event, bad_headers_head);
		}
		regfree(&regexbuf);
		return -1;
	}

	return 0;
}

static int message_received;

static int test_msg_send(const struct ast_msg *msg, const char *to, const char *from)
{
	message_received = 1;

	return 0;
}

static int test_msg_handle_msg_cb(struct ast_msg *msg)
{
	ast_mutex_lock(&handler_lock);
	handler_received_message = 1;
	ast_cond_signal(&handler_cond);
	ast_mutex_unlock(&handler_lock);

	return 0;
}

static int test_msg_has_destination_cb(const struct ast_msg *msg)
{
	/* We only care about one destination: foo! */
	if (ast_strlen_zero(ast_msg_get_to(msg))) {
		return 0;
	}
	return (!strcmp(ast_msg_get_to(msg), "foo") ? 1 : 0);
}

static int user_event_hook_cb(int category, const char *event, char *body)
{
	char *parse;
	char *kvp;

	if (strcmp(event, "UserEvent")) {
		return -1;
	}

	parse = ast_strdupa(body);
	while ((kvp = strsep(&parse, "\r\n"))) {
		char *key, *value;

		kvp = ast_trim_blanks(kvp);
		if (ast_strlen_zero(kvp)) {
			continue;
		}
		key = strsep(&kvp, ":");
		value = ast_skip_blanks(kvp);
		verify_user_event_fields(received_user_events, key, value);
	}

	received_user_events++;

	ast_mutex_lock(&user_event_lock);
	if (received_user_events == expected_user_events) {
		ast_cond_signal(&user_event_cond);
	}
	ast_mutex_unlock(&user_event_lock);

	return 0;
}

/*! \brief Wait for the \ref test_msg_handler to receive the message */
static int handler_wait_for_message(struct ast_test *test)
{
	int error = 0;
	struct timeval wait_now = ast_tvnow();
	struct timespec wait_time = { .tv_sec = wait_now.tv_sec + 1, .tv_nsec = wait_now.tv_usec * 1000 };

	ast_mutex_lock(&handler_lock);
	while (!handler_received_message) {
		error = ast_cond_timedwait(&handler_cond, &handler_lock, &wait_time);
		if (error == ETIMEDOUT) {
			ast_test_status_update(test, "Test timed out while waiting for handler to get message\n");
			ast_test_set_result(test, AST_TEST_FAIL);
			break;
		}
	}
	ast_mutex_unlock(&handler_lock);

	return (error != ETIMEDOUT);
}

/*! \brief Wait for the expected number of user events to be received */
static int user_event_wait_for_events(struct ast_test *test, int expected_events)
{
	int error;
	struct timeval wait_now = ast_tvnow();
	struct timespec wait_time = { .tv_sec = wait_now.tv_sec + 1, .tv_nsec = wait_now.tv_usec * 1000 };

	expected_user_events = expected_events;

	ast_mutex_lock(&user_event_lock);
	while (received_user_events != expected_user_events) {
		error = ast_cond_timedwait(&user_event_cond, &user_event_lock, &wait_time);
		if (error == ETIMEDOUT) {
			ast_test_status_update(test, "Test timed out while waiting for %d expected user events\n", expected_events);
			ast_test_set_result(test, AST_TEST_FAIL);
			break;
		}
	}
	ast_mutex_unlock(&user_event_lock);

	ast_test_status_update(test, "Received %d of %d user events\n", received_user_events, expected_events);
	return !(received_user_events == expected_events);
}

static int verify_bad_headers(struct ast_test *test)
{
	int res = 0;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&bad_headers); i++) {
		struct ast_variable *headers;
		struct ast_variable *current;

		headers = AST_VECTOR_GET(&bad_headers, i);
		if (!headers) {
			continue;
		}

		res = -1;
		for (current = headers; current; current = current->next) {
			ast_test_status_update(test, "Expected UserEvent %d: Failed to match %s: %s\n",
				i, current->name, current->value);
			ast_test_set_result(test, AST_TEST_FAIL);
		}
	}

	return res;
}

AST_TEST_DEFINE(test_message_msg_tech_registration)
{
	int reg_result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test register/unregister of a message tech";
		info->description =
			"Test that:\n"
			"\tA message technology can be registered once only\n"
			"\tA registered message technology can be unregistered once only\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	reg_result = ast_msg_tech_register(&test_msg_tech);
	ast_test_validate(test, reg_result == 0);

	reg_result = ast_msg_tech_register(&test_msg_tech);
	ast_test_validate(test, reg_result == -1);

	reg_result = ast_msg_tech_unregister(&test_msg_tech);
	ast_test_validate(test, reg_result == 0);

	reg_result = ast_msg_tech_unregister(&test_msg_tech);
	ast_test_validate(test, reg_result == -1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_msg_handler_registration)
{
	int reg_result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test register/unregister of a message handler";
		info->description =
			"Test that:\n"
			"\tA message handler can be registered once only\n"
			"\tA registered message handler can be unregistered once only\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	reg_result = ast_msg_handler_register(&test_msg_handler);
	ast_test_validate(test, reg_result == 0);

	reg_result = ast_msg_handler_register(&test_msg_handler);
	ast_test_validate(test, reg_result == -1);

	reg_result = ast_msg_handler_unregister(&test_msg_handler);
	ast_test_validate(test, reg_result == 0);

	reg_result = ast_msg_handler_unregister(&test_msg_handler);
	ast_test_validate(test, reg_result == -1);

	return AST_TEST_PASS;
}

static void ast_msg_safe_destroy(void *obj)
{
	struct ast_msg *msg = obj;

	if (msg) {
		ast_msg_destroy(msg);
	}
}

AST_TEST_DEFINE(test_message_manipulation)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);
	RAII_VAR(struct ast_msg_var_iterator *, it_vars, NULL, ast_msg_var_iterator_destroy);
	int result;
	const char *actual;
	const char *out_name;
	const char *out_value;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test manipulating properties of a message";
		info->description =
			"This test covers the following:\n"
			"\tSetting/getting the body\n"
			"\tSetting/getting inbound/outbound variables\n"
			"\tIterating over variables\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	/* Test setting/getting to */
	result = ast_msg_set_to(msg, "testmsg:%s", "foo");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_to(msg);
	ast_test_validate(test, !strcmp(actual, "testmsg:foo"));

	/* Test setting/getting from */
	result = ast_msg_set_from(msg, "testmsg:%s", "bar");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_from(msg);
	ast_test_validate(test, !strcmp(actual, "testmsg:bar"));

	/* Test setting/getting body */
	result = ast_msg_set_body(msg, "BodyTest: %s", "foo");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_body(msg);
	ast_test_validate(test, !strcmp(actual, "BodyTest: foo"));

	/* Test setting/getting technology */
	result = ast_msg_set_tech(msg, "%s", "my_tech");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_tech(msg);
	ast_test_validate(test, !strcmp(actual, "my_tech"));

	/* Test setting/getting endpoint */
	result = ast_msg_set_endpoint(msg, "%s", "terminus");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_endpoint(msg);
	ast_test_validate(test, !strcmp(actual, "terminus"));

	/* Test setting/getting non-outbound variable */
	result = ast_msg_set_var(msg, "foo", "bar");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_var(msg, "foo");
	ast_test_validate(test, !strcmp(actual, "bar"));

	/* Test updating existing variable */
	result = ast_msg_set_var(msg, "foo", "new_bar");
	ast_test_validate(test, result == 0);
	actual = ast_msg_get_var(msg, "foo");
	ast_test_validate(test, !strcmp(actual, "new_bar"));

	/* Verify a non-outbound variable is not iterable */
	it_vars = ast_msg_var_iterator_init(msg);
	ast_test_validate(test, it_vars != NULL);
	ast_test_validate(test, ast_msg_var_iterator_next(msg, it_vars, &out_name, &out_value) == 0);
	ast_msg_var_iterator_destroy(it_vars);

	/* Test updating an existing variable as an outbound variable */
	result = ast_msg_set_var_outbound(msg, "foo", "outbound_bar");
	ast_test_validate(test, result == 0);
	it_vars = ast_msg_var_iterator_init(msg);
	ast_test_validate(test, it_vars != NULL);
	result = ast_msg_var_iterator_next(msg, it_vars, &out_name, &out_value);
	ast_test_validate(test, result == 1);
	ast_test_validate(test, !strcmp(out_name, "foo"));
	ast_test_validate(test, !strcmp(out_value, "outbound_bar"));
	ast_msg_var_unref_current(it_vars);
	result = ast_msg_var_iterator_next(msg, it_vars, &out_name, &out_value);
	ast_test_validate(test, result == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_queue_dialplan_nominal)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);
	struct ast_variable *expected;
	struct ast_variable *expected_response = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test enqueueing messages to the dialplan";
		info->description =
			"Test that a message enqueued for the dialplan is\n"
			"passed to that particular extension\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	expected = ast_variable_new("Verify","^To$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value","^foo$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 0, expected_response);

	expected_response = NULL;
	expected = ast_variable_new("Verify", "^From$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value","^bar$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 1, expected_response);

	expected_response = NULL;
	expected = ast_variable_new("Verify", "^Body$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value", "^a body$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 2, expected_response);

	expected_response = NULL;
	expected = ast_variable_new("Verify", "^Custom$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value", "^field$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 3, expected_response);

	ast_msg_set_to(msg, "foo");
	ast_msg_set_from(msg, "bar");
	ast_msg_set_body(msg, "a body");
	ast_msg_set_var_outbound(msg, "custom_data", "field");

	ast_msg_set_context(msg, TEST_CONTEXT);
	ast_msg_set_exten(msg, TEST_EXTENSION);

	ast_msg_queue(msg);
	msg = NULL;

	if (user_event_wait_for_events(test, DEFAULT_EXPECTED_EVENTS)) {
		ast_test_status_update(test, "Failed to received %d expected user events\n", DEFAULT_EXPECTED_EVENTS);
		return AST_TEST_FAIL;
	}

	if (verify_bad_headers(test)) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_queue_handler_nominal)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);
	int result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test enqueueing messages to a handler";
		info->description =
			"Test that a message enqueued can be handled by a\n"
			"non-dialplan handler\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	result = ast_msg_handler_register(&test_msg_handler);
	ast_test_validate(test, result == 0);

	ast_msg_set_to(msg, "foo");
	ast_msg_set_from(msg, "bar");
	ast_msg_set_body(msg, "a body");

	ast_msg_queue(msg);
	msg = NULL;

	/* This will automatically fail the test if we don't get the message */
	handler_wait_for_message(test);

	result = ast_msg_handler_unregister(&test_msg_handler);
	ast_test_validate(test, result == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_queue_both_nominal)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);
	struct ast_variable *expected;
	struct ast_variable *expected_response = NULL;
	int result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test enqueueing messages to a dialplan and custom handler";
		info->description =
			"Test that a message enqueued is passed to all\n"
			"handlers that can process it, dialplan as well as\n"
			"a custom handler\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	result = ast_msg_handler_register(&test_msg_handler);
	ast_test_validate(test, result == 0);

	expected = ast_variable_new("Verify","^To$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value","^foo$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 0, expected_response);

	expected_response = NULL;
	expected = ast_variable_new("Verify", "^From$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value","^bar$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 1, expected_response);

	expected_response = NULL;
	expected = ast_variable_new("Verify", "^Body$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	expected = ast_variable_new("Value", "^a body$", __FILE__);
	ast_variable_list_append(&expected_response, expected);
	AST_VECTOR_REPLACE(&expected_user_event_fields, 2, expected_response);

	ast_msg_set_to(msg, "foo");
	ast_msg_set_from(msg, "bar");
	ast_msg_set_body(msg, "a body");

	ast_msg_set_context(msg, TEST_CONTEXT);
	ast_msg_set_exten(msg, TEST_EXTENSION);

	ast_msg_queue(msg);
	msg = NULL;

	if (user_event_wait_for_events(test, DEFAULT_EXPECTED_EVENTS)) {
		ast_test_status_update(test, "Failed to received %d expected user events\n", DEFAULT_EXPECTED_EVENTS);
		ast_test_set_result(test, AST_TEST_FAIL);
	}

	/* This will automatically fail the test if we don't get the message */
	handler_wait_for_message(test);

	result = ast_msg_handler_unregister(&test_msg_handler);
	ast_test_validate(test, result == 0);

	if (verify_bad_headers(test)) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_has_destination_dialplan)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test checking for a dialplan destination";
		info->description =
			"Test that a message's destination is verified via the\n"
			"dialplan\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	ast_msg_set_context(msg, TEST_CONTEXT);
	ast_msg_set_exten(msg, TEST_EXTENSION);
	ast_test_validate(test, ast_msg_has_destination(msg) == 1);

	ast_msg_set_context(msg, "__I_SHOULD_NOT_EXIST_PLZ__");
	ast_test_validate(test, ast_msg_has_destination(msg) == 0);

	ast_msg_set_context(msg, TEST_CONTEXT);
	ast_msg_set_exten(msg, "__I_SHOULD_NOT_EXIST_PLZ__");
	ast_test_validate(test, ast_msg_has_destination(msg) == 0);

	ast_msg_set_exten(msg, NULL);
	ast_test_validate(test, ast_msg_has_destination(msg) == 0);

	ast_msg_set_context(msg, NULL);
	ast_msg_set_exten(msg, TEST_EXTENSION);
	ast_test_validate(test, ast_msg_has_destination(msg) == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_has_destination_handler)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);
	int result;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test checking for a handler destination";
		info->description =
			"Test that a message's destination is verified via a\n"
			"handler\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	result = ast_msg_handler_register(&test_msg_handler);
	ast_test_validate(test, result == 0);

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	ast_msg_set_to(msg, "foo");
	ast_msg_set_context(msg, TEST_CONTEXT);
	ast_msg_set_exten(msg, NULL);
	ast_test_validate(test, ast_msg_has_destination(msg) == 1);

	ast_msg_set_context(msg, NULL);
	ast_test_validate(test, ast_msg_has_destination(msg) == 1);

	ast_msg_set_to(msg, "__I_SHOULD_NOT_EXIST_PLZ__");
	ast_test_validate(test, ast_msg_has_destination(msg) == 0);

	result = ast_msg_handler_unregister(&test_msg_handler);
	ast_test_validate(test, result == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_message_msg_send)
{
	RAII_VAR(struct ast_msg *, msg, NULL, ast_msg_safe_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Test message routing";
		info->description =
			"Test that a message can be routed if it has\n"
			"a valid handler\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, ast_msg_tech_register(&test_msg_tech) == 0);
	ast_test_validate(test, ast_msg_handler_register(&test_msg_handler) == 0);

	msg = ast_msg_alloc();
	ast_test_validate(test, msg != NULL);

	ast_msg_set_to(msg, "foo");
	ast_msg_set_context(msg, TEST_CONTEXT);
	ast_msg_set_exten(msg, NULL);
	ast_test_validate(test, ast_msg_has_destination(msg) == 1);

	if (!ast_msg_send(msg, "testmsg:foo", "blah")) {
		msg = NULL;
	} else {
		ast_test_status_update(test, "Failed to send message\n");
		ast_test_set_result(test, AST_TEST_FAIL);
	}

	ast_test_validate(test, ast_msg_handler_unregister(&test_msg_handler) == 0);
	ast_test_validate(test, ast_msg_tech_unregister(&test_msg_tech) == 0);

	return AST_TEST_PASS;
}

static int test_init_cb(struct ast_test_info *info, struct ast_test *test)
{
	received_user_events = 0;
	handler_received_message = 0;
	message_received = 0;

	AST_VECTOR_INIT(&expected_user_event_fields, DEFAULT_EXPECTED_EVENTS);
	AST_VECTOR_INIT(&bad_headers, DEFAULT_EXPECTED_EVENTS);

	return 0;
}

#define FREE_VARIABLE_VECTOR(vector) do { \
	int i; \
	for (i = 0; i < AST_VECTOR_SIZE(&(vector)); i++) { \
		struct ast_variable *headers; \
		headers = AST_VECTOR_GET(&(vector), i); \
		if (!headers) { \
			continue; \
		} \
		ast_variables_destroy(headers); \
	} \
	AST_VECTOR_FREE(&(vector)); \
	} while (0)


static int test_cleanup_cb(struct ast_test_info *info, struct ast_test *test)
{
	FREE_VARIABLE_VECTOR(expected_user_event_fields);
	FREE_VARIABLE_VECTOR(bad_headers);

	return 0;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_message_msg_tech_registration);
	AST_TEST_UNREGISTER(test_message_msg_handler_registration);
	AST_TEST_UNREGISTER(test_message_manipulation);
	AST_TEST_UNREGISTER(test_message_queue_dialplan_nominal);
	AST_TEST_UNREGISTER(test_message_queue_handler_nominal);
	AST_TEST_UNREGISTER(test_message_queue_both_nominal);
	AST_TEST_UNREGISTER(test_message_has_destination_dialplan);
	AST_TEST_UNREGISTER(test_message_has_destination_handler);
	AST_TEST_UNREGISTER(test_message_msg_send);

	ast_context_destroy(NULL, AST_MODULE);

	ast_manager_unregister_hook(&user_event_hook);

	return 0;
}

static int create_test_dialplan(void)
{
	int res = 0;

	if (!ast_context_find_or_create(NULL, NULL, TEST_CONTEXT, AST_MODULE)) {
		return -1;
	}

	res |= ast_add_extension(TEST_CONTEXT, 0, TEST_EXTENSION, 1, NULL, NULL,
	                         "UserEvent", "TestMessageUnitTest,Verify:To,Value:${MESSAGE(to)}",
	                         NULL, AST_MODULE);
	res |= ast_add_extension(TEST_CONTEXT, 0, TEST_EXTENSION, 2, NULL, NULL,
	                         "UserEvent", "TestMessageUnitTest,Verify:From,Value:${MESSAGE(from)}",
	                         NULL, AST_MODULE);
	res |= ast_add_extension(TEST_CONTEXT, 0, TEST_EXTENSION, 3, NULL, NULL,
	                         "UserEvent", "TestMessageUnitTest,Verify:Body,Value:${MESSAGE(body)}",
	                         NULL, AST_MODULE);
	res |= ast_add_extension(TEST_CONTEXT, 0, TEST_EXTENSION, 4, NULL, NULL,
	                         "UserEvent", "TestMessageUnitTest,Verify:Custom,Value:${MESSAGE_DATA(custom_data)}",
	                         NULL, AST_MODULE);
	res |= ast_add_extension(TEST_CONTEXT, 0, TEST_EXTENSION, 5, NULL, NULL,
	                         "Set", "MESSAGE_DATA(custom_data)=${MESSAGE_DATA(custom_data)}",
	                         NULL, AST_MODULE);
	res |= ast_add_extension(TEST_CONTEXT, 0, TEST_EXTENSION, 6, NULL, NULL,
	                         "MessageSend", "testmsg:${MESSAGE(from)},testmsg:${MESSAGE(to)}",
	                         NULL, AST_MODULE);

	ast_manager_register_hook(&user_event_hook);

	return res;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_message_msg_tech_registration);
	AST_TEST_REGISTER(test_message_msg_handler_registration);
	AST_TEST_REGISTER(test_message_manipulation);
	AST_TEST_REGISTER(test_message_queue_dialplan_nominal);
	AST_TEST_REGISTER(test_message_queue_handler_nominal);
	AST_TEST_REGISTER(test_message_queue_both_nominal);
	AST_TEST_REGISTER(test_message_has_destination_dialplan);
	AST_TEST_REGISTER(test_message_has_destination_handler);
	AST_TEST_REGISTER(test_message_msg_send);

	create_test_dialplan();

	ast_test_register_init(TEST_CATEGORY, test_init_cb);
	ast_test_register_cleanup(TEST_CATEGORY, test_cleanup_cb);

	return AST_MODULE_LOAD_SUCCESS;
}


AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Out-of-call text message support");
