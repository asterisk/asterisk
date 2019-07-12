/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/conversions.h"
#include "asterisk/module.h"
#include "asterisk/mwi.h"
#include "asterisk/stasis.h"
#include "asterisk/test.h"

#define test_category "/mwi/"

#define MAILBOX_PREFIX "test~" /* Hopefully sufficiently unlikely */
#define MAILBOX_COUNT 500
#define MAILBOX_SIZE 32

AST_VECTOR(subscriptions, struct ast_mwi_subscriber *);
AST_VECTOR(publishers, struct ast_mwi_publisher *);

/*!
 * For testing purposes each subscribed mailbox is a number. This value is
 * the summation of all mailboxes.
 */
static size_t sum_total;

/*! Test variable that tracks the running total of mailboxes */
static size_t running_total;

/*! This value is set to check if MWI data is zero before publishing */
static int expect_zero;

static int num_to_mailbox(char *mailbox, size_t size, size_t num)
{
	if (snprintf(mailbox, 10, MAILBOX_PREFIX "%zu", num) == -1) {
		ast_log(LOG_ERROR, "Unable to convert mailbox to string\n");
		return -1;
	}

	return 0;
}

static int mailbox_to_num(const char *mailbox, size_t *num)
{
	const char *p = strchr(mailbox, '~');

	if (!p) {
		ast_log(LOG_ERROR, "Prefix separator '~' not found in '%s'\n", mailbox);
		return -1;
	}

	if (ast_str_to_umax(++p, num)) {
		ast_log(LOG_ERROR, "Unable to convert mailbox '%s' to numeric\n", mailbox);
		return -1;
	}

	return 0;
}

static int validate_data(struct ast_mwi_state *mwi_state)
{
	size_t num;
	size_t val;

	if (mailbox_to_num(mwi_state->uniqueid, &num)) {
		return -1;
	}

	running_total += num;

	val = expect_zero ? 0 : num;

	if (mwi_state->urgent_msgs != val || mwi_state->new_msgs != val ||
			mwi_state->old_msgs != val) {
		ast_log(LOG_ERROR, "Unexpected MWI state data for '%s', %d != %zu\n",
				mwi_state->uniqueid, mwi_state->urgent_msgs, val);
		return -1;
	}

	return num;
}

static void handle_validate(const char *mailbox, struct ast_mwi_subscriber *sub)
{
	struct ast_mwi_state *mwi_state = ast_mwi_subscriber_data(sub);

	if (ast_begins_with(mwi_state->uniqueid, MAILBOX_PREFIX)) {
		validate_data(mwi_state);
	}

	ao2_cleanup(mwi_state);
}

struct ast_mwi_observer mwi_observer = {
	.on_subscribe = handle_validate,
	.on_unsubscribe = handle_validate
};

static void mwi_type_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	/* No op since we are not really testing stasis topic handling here */
}

static int subscriptions_destroy(struct subscriptions *subs)
{
	running_total = expect_zero = 0;

	AST_VECTOR_CALLBACK_VOID(subs, ast_mwi_unsubscribe_and_join);
	AST_VECTOR_FREE(subs);

	ast_mwi_remove_observer(&mwi_observer);

	if (running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed to destroy all MWI subscriptions: running=%zu, sum=%zu\n",
				running_total, sum_total);
		return -1;
	}

	return 0;
}

static int subscriptions_create(struct subscriptions *subs)
{
	size_t i;

	if (ast_mwi_add_observer(&mwi_observer) ||
		AST_VECTOR_INIT(subs, MAILBOX_COUNT)) {
		return -1;
	}

	sum_total = running_total = 0;
	expect_zero = 1;

	for (i = 0; i < MAILBOX_COUNT; ++i) {
		struct ast_mwi_subscriber *sub;
		char mailbox[MAILBOX_SIZE];

		if (num_to_mailbox(mailbox, MAILBOX_SIZE, i)) {
			break;
		}

		sub = ast_mwi_subscribe_pool(mailbox, mwi_type_cb, NULL);
		if (!sub) {
			ast_log(LOG_ERROR, "Failed to create a MWI subscriber for mailbox '%s'\n", mailbox);
			break;
		}

		if (AST_VECTOR_APPEND(subs, sub)) {
			ast_log(LOG_ERROR, "Failed to add to MWI sub to vector for mailbox '%s'\n", mailbox);
			ao2_ref(sub, -1);
			break;
		}

		sum_total += i;
	}

	if (i != MAILBOX_COUNT || running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed to create all MWI subscriptions: running=%zu, sum=%zu\n",
				running_total, sum_total);
		subscriptions_destroy(subs);
		return -1;
	}

	return 0;
}

static int publishers_destroy(struct publishers *pubs)
{
	size_t i;

	if (pubs) {
		/* Remove explicit publishers */
		AST_VECTOR_CALLBACK_VOID(pubs, ao2_cleanup);
		AST_VECTOR_FREE(pubs);
		return 0;
	}

	for (i = 0; i < MAILBOX_COUNT; ++i) {
		char mailbox[MAILBOX_SIZE];

		/* Remove implicit publishers */
		if (num_to_mailbox(mailbox, MAILBOX_SIZE, i)) {
			return -1;
		}

		ast_delete_mwi_state(mailbox, NULL);
	}

	return 0;
}

static int publishers_create(struct publishers *pubs)
{
	size_t i;

	if (AST_VECTOR_INIT(pubs, MAILBOX_COUNT)) {
		return -1;
	}

	for (i = 0; i < MAILBOX_COUNT; ++i) {
		struct ast_mwi_publisher *pub;
		char mailbox[MAILBOX_SIZE];

		if (num_to_mailbox(mailbox, MAILBOX_SIZE, i)) {
			break;
		}

		/* Create the MWI publisher */
		pub = ast_mwi_add_publisher(mailbox);
		if (!pub) {
			ast_log(LOG_ERROR, "Failed to create an MWI publisher for mailbox '%s'\n", mailbox);
			break;
		}

		if (AST_VECTOR_APPEND(pubs, pub)) {
			ast_log(LOG_ERROR, "Failed to add to an MWI publisher to vector for mailbox '%s'\n", mailbox);
			ao2_ref(pub, -1);
			break;
		}
	}

	if (i != MAILBOX_COUNT) {
		ast_log(LOG_ERROR, "Failed to create all MWI publishers: count=%zu\n", i);
		publishers_destroy(pubs);
		return -1;
	}

	return 0;
}

static int implicit_publish_cb(struct ast_mwi_state *mwi_state, void *data)
{
	size_t num;

	if (!ast_begins_with(mwi_state->uniqueid, MAILBOX_PREFIX)) {
		/* Ignore any mailboxes not prefixed */
		return 0;
	}

	num = validate_data(mwi_state);
	if (num < 0) {
		return CMP_STOP;
	}

	ast_mwi_publish_by_mailbox(mwi_state->uniqueid, NULL, num, num, num, NULL, NULL);

	return 0;
}

static int explicit_publish_cb(struct ast_mwi_state *mwi_state, void *data)
{
	struct publishers *pubs = data;
	struct ast_mwi_publisher *pub;
	size_t num;

	if (!ast_begins_with(mwi_state->uniqueid, MAILBOX_PREFIX)) {
		/* Ignore any mailboxes not prefixed */
		return 0;
	}

	num = validate_data(mwi_state);
	if (num < 0) {
		return CMP_STOP;
	}

	if (mailbox_to_num(mwi_state->uniqueid, &num)) {
		return CMP_STOP;
	}

	/* Mailbox number will always be the index */
	pub = AST_VECTOR_GET(pubs, num);

	if (!pub) {
		ast_log(LOG_ERROR, "Unable to locate MWI publisher for mailbox '%s'\n", mwi_state->uniqueid);
		return CMP_STOP;
	}

	ast_mwi_publish(pub, num, num, num, NULL, NULL);

	return 0;
}

static int publish(on_mwi_state cb, void *user_data)
{
	/* First time there is no state data */
	expect_zero = 1;

	running_total = 0;
	ast_mwi_state_callback_all(cb, user_data);

	if (running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed MWI state callback (1): running=%zu, sum=%zu\n",
				running_total, sum_total);
		return -1;
	}

	/* Second time check valid state data exists */
	running_total = expect_zero = 0;
	ast_mwi_state_callback_all(cb, user_data);

	if (running_total != sum_total) {
		ast_log(LOG_ERROR, "Failed MWI state callback (2): running=%zu, sum=%zu\n",
				running_total, sum_total);
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(implicit_publish)
{
	struct subscriptions subs;
	int rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test implicit publishing of MWI state";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, !subscriptions_create(&subs));

	ast_test_validate_cleanup(test, !publish(implicit_publish_cb, NULL),
		rc, cleanup);

cleanup:
	if (subscriptions_destroy(&subs) || publishers_destroy(NULL)) {
		return AST_TEST_FAIL;
	}

	return rc;
}

AST_TEST_DEFINE(explicit_publish)
{
	struct subscriptions subs;
	struct publishers pubs;
	int rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = test_category;
		info->summary = "Test explicit publishing of MWI state";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, !subscriptions_create(&subs));
	ast_test_validate_cleanup(test, !publishers_create(&pubs), rc, cleanup);

	ast_test_validate_cleanup(test, !publish(explicit_publish_cb, &pubs),
		rc, cleanup);

cleanup:
	if (subscriptions_destroy(&subs) || publishers_destroy(&pubs)) {
		return AST_TEST_FAIL;
	}

	return rc;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(implicit_publish);
	AST_TEST_UNREGISTER(explicit_publish);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(implicit_publish);
	AST_TEST_REGISTER(explicit_publish);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "MWI testing");
