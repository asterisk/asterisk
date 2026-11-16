/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Ben Kolodny
 *
 * Ben Kolodny <3232963@gmail.com>
 *
 * See https://www.asterisk.org for more information about
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
 * \brief Extension state legacy callback tests
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/pbx.h"
#include "asterisk/devicestate.h"
#include "asterisk/astobj2.h"
#include "asterisk/lock.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

#define TEST_CATEGORY "/main/extension_state/"
#define TEST_CONTEXT "test_extension_state"
#define TEST_REGISTRAR "test_extension_state"

/*!
 * \brief Prefix for the devices used by the test hints
 *
 * Device state is global, so the hints use device names reserved for this test
 * to avoid affecting, or being affected by, any other hint.
 */
#define TEST_DEVICE_PREFIX "Custom:test_extension_state_"

/*! \brief How long to wait for asynchronous events, in seconds */
#define TEST_TIMEOUT 5

/*!
 * \brief Tracking for a single extension state callback
 *
 * While a callback is registered it holds its own reference to the watcher, which
 * is released only by the destroy callback. This keeps the watcher valid for as long
 * as the callback can use it, even if the test gives up waiting and returns.
 */
struct test_watcher {
	/*! The ID returned when adding the callback */
	int id;
	/*! The last extension state received */
	enum ast_extension_states last_state;
	/*! Number of times the destroy callback was invoked */
	int destroyed;
	/*! The spelling of the extension this watcher subscribed to */
	char exten[AST_MAX_EXTENSION];
};

/*! \brief Protects watcher state and test_run */
AST_MUTEX_DEFINE_STATIC(test_lock);
static ast_cond_t test_cond;

/*! \brief Used to make the extensions, and so the devices, differ between test runs */
static unsigned int test_run;

static struct test_watcher *test_watcher_alloc(const char *exten)
{
	struct test_watcher *watcher;

	watcher = ao2_alloc_options(sizeof(*watcher), NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!watcher) {
		return NULL;
	}

	watcher->id = -1;
	watcher->last_state = AST_EXTENSION_REMOVED;
	ast_copy_string(watcher->exten, exten, sizeof(watcher->exten));

	return watcher;
}

static int state_changed(const char *context, const char *exten, struct ast_state_cb_info *info, void *data)
{
	struct test_watcher *watcher = data;

	ast_mutex_lock(&test_lock);
	watcher->last_state = info->exten_state;
	ast_cond_broadcast(&test_cond);
	ast_mutex_unlock(&test_lock);

	return 0;
}

static void state_destroyed(int id, void *data)
{
	struct test_watcher *watcher = data;

	ast_mutex_lock(&test_lock);
	watcher->destroyed++;
	ast_cond_broadcast(&test_cond);
	ast_mutex_unlock(&test_lock);

	/* Release the reference held by the callback */
	ao2_ref(watcher, -1);
}

/*!
 * \brief Add an extension state callback for a watcher
 *
 * \retval 0 on success
 * \retval -1 on failure, in which case the destroy callback must not have been
 *         invoked as the caller retains ownership of the callback data
 */
static int test_watcher_add(struct ast_test *test, const char *context, struct test_watcher *watcher)
{
	int destroyed;

	/* The callback holds its own reference to the watcher */
	ao2_ref(watcher, +1);

	watcher->id = ast_extension_state_add_destroy_extended(context, watcher->exten,
		state_changed, state_destroyed, watcher);
	if (watcher->id >= 0) {
		return 0;
	}

	/*
	 * On failure the caller keeps ownership, so the reference given to the callback
	 * is ours to release. If the destroy callback was wrongly invoked it has already
	 * released it, synchronously, before the add returned.
	 */
	ast_mutex_lock(&test_lock);
	destroyed = watcher->destroyed;
	ast_mutex_unlock(&test_lock);

	if (destroyed) {
		ast_test_status_update(test, "Destroy callback for '%s' invoked %d times on failed add\n",
			watcher->exten, destroyed);
	} else {
		ao2_ref(watcher, -1);
	}

	return -1;
}

/*!
 * \brief Remove an extension state callback and wait for it to be destroyed
 *
 * \retval 0 if the destroy callback was invoked exactly once
 * \retval -1 otherwise
 *
 * \note If destruction does not occur in time the callback keeps its reference,
 *       so the watcher remains valid if the callback is invoked later.
 */
static int test_watcher_remove(struct ast_test *test, struct test_watcher *watcher)
{
	struct timeval now = ast_tvnow();
	struct timespec deadline = {
		.tv_sec = now.tv_sec + TEST_TIMEOUT,
		.tv_nsec = now.tv_usec * 1000,
	};
	int destroyed;

	ast_extension_state_del(watcher->id, state_changed);

	ast_mutex_lock(&test_lock);
	while (!watcher->destroyed) {
		if (ast_cond_timedwait(&test_cond, &test_lock, &deadline) == ETIMEDOUT) {
			break;
		}
	}
	destroyed = watcher->destroyed;
	ast_mutex_unlock(&test_lock);

	if (destroyed != 1) {
		ast_test_status_update(test, "Destroy callback for '%s' invoked %d times, expected 1\n",
			watcher->exten, destroyed);
		return -1;
	}

	return 0;
}

/*! \brief Wait until a watcher has received the given extension state */
static int test_watcher_wait_for_state(struct test_watcher *watcher, enum ast_extension_states state)
{
	struct timeval now = ast_tvnow();
	struct timespec deadline = {
		.tv_sec = now.tv_sec + TEST_TIMEOUT,
		.tv_nsec = now.tv_usec * 1000,
	};
	int res;

	ast_mutex_lock(&test_lock);
	while (watcher->last_state != state) {
		if (ast_cond_timedwait(&test_cond, &test_lock, &deadline) == ETIMEDOUT) {
			break;
		}
	}
	res = watcher->last_state == state ? 0 : -1;
	ast_mutex_unlock(&test_lock);

	return res;
}

static int dialplan_setup(void)
{
	if (!ast_context_find_or_create(NULL, NULL, TEST_CONTEXT, TEST_REGISTRAR)) {
		return -1;
	}

	/*
	 * A pattern hint creates a concrete hint, and so a device, using the spelling
	 * of whichever lookup comes first.
	 */
	if (ast_add_extension(TEST_CONTEXT, 0, "_X.", PRIORITY_HINT, NULL, NULL,
		TEST_DEVICE_PREFIX "${EXTEN}", NULL, NULL, TEST_REGISTRAR)) {
		return -1;
	}

	return 0;
}

static void dialplan_cleanup(void)
{
	ast_context_destroy_by_name(TEST_CONTEXT, TEST_REGISTRAR);
}

/*!
 * \brief Watch two spellings of an extension that differ only by dashes
 *
 * This mirrors two phones with BLF keys for the same extension, one configured
 * with a dash and one without. The first spelling creates the hint from the
 * pattern. Both watchers must be added, both must be notified when the device
 * of the hint changes state, and both must be destroyed exactly once.
 *
 * \param test The test
 * \param base The leading digits of the extension
 * \param suffix The trailing part of the extension, joined by a dash when dashed
 * \param dashed_first Whether the dashed spelling is watched first, naming the device
 *
 * The test run number is added to the extension, so that runs use different
 * devices and a run is not affected by state left behind by an earlier one.
 */
static enum ast_test_result_state dash_spelling_check(struct ast_test *test,
	const char *base, const char *suffix, int dashed_first)
{
	struct test_watcher *watchers[2] = { NULL, NULL };
	char first[AST_MAX_EXTENSION];
	char second[AST_MAX_EXTENSION];
	char device[AST_MAX_EXTENSION + sizeof(TEST_DEVICE_PREFIX)];
	enum ast_test_result_state res = AST_TEST_PASS;
	unsigned int run;
	int added[2] = { 0, 0 };
	int i;

	ast_mutex_lock(&test_lock);
	run = ++test_run;
	ast_mutex_unlock(&test_lock);

	snprintf(first, sizeof(first), "%s%u%s%s", base, run, dashed_first ? "-" : "", suffix);
	snprintf(second, sizeof(second), "%s%u%s%s", base, run, dashed_first ? "" : "-", suffix);
	/* The device is named after the spelling that created the hint */
	snprintf(device, sizeof(device), TEST_DEVICE_PREFIX "%s", first);

	watchers[0] = test_watcher_alloc(first);
	watchers[1] = test_watcher_alloc(second);
	if (!watchers[0] || !watchers[1]) {
		ast_test_status_update(test, "Failed to allocate watchers\n");
		ao2_cleanup(watchers[0]);
		ao2_cleanup(watchers[1]);
		return AST_TEST_FAIL;
	}

	if (dialplan_setup()) {
		ast_test_status_update(test, "Failed to set up dialplan\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	for (i = 0; i < ARRAY_LEN(watchers); i++) {
		if (test_watcher_add(test, TEST_CONTEXT, watchers[i])) {
			ast_test_status_update(test, "Failed to watch '%s'\n", watchers[i]->exten);
			res = AST_TEST_FAIL;
		} else {
			added[i] = 1;
		}
	}

	if (res == AST_TEST_PASS) {
		ast_devstate_changed_literal(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, device);
		for (i = 0; i < ARRAY_LEN(watchers); i++) {
			if (test_watcher_wait_for_state(watchers[i], AST_EXTENSION_INUSE)) {
				ast_test_status_update(test, "Watcher of '%s' was not notified of InUse\n",
					watchers[i]->exten);
				res = AST_TEST_FAIL;
			}
		}

		ast_devstate_changed_literal(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, device);
		for (i = 0; i < ARRAY_LEN(watchers); i++) {
			if (test_watcher_wait_for_state(watchers[i], AST_EXTENSION_NOT_INUSE)) {
				ast_test_status_update(test, "Watcher of '%s' was not notified of Idle\n",
					watchers[i]->exten);
				res = AST_TEST_FAIL;
			}
		}
	}

	for (i = 0; i < ARRAY_LEN(watchers); i++) {
		if (added[i] && test_watcher_remove(test, watchers[i])) {
			res = AST_TEST_FAIL;
		}
	}

cleanup:
	dialplan_cleanup();
	ast_device_state_clear_cache(device);
	ao2_ref(watchers[0], -1);
	ao2_ref(watchers[1], -1);

	return res;
}

AST_TEST_DEFINE(dash_spelling_dashless_first)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Watch a dashed extension after the dashless spelling created the hint";
		info->description =
			"Dashes are ignored in extension matching, so '1000x' and '1000-x' are the\n"
			"same extension. Watching the dashless spelling first creates the hint from a\n"
			"pattern. Watching the dashed spelling must then also succeed, both watchers\n"
			"must be notified of state changes of the hint's device, and both must be\n"
			"destroyed exactly once.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return dash_spelling_check(test, "1000", "x", 0);
}

AST_TEST_DEFINE(dash_spelling_dashed_first)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Watch a dashless extension after the dashed spelling created the hint";
		info->description =
			"Watching the dashed spelling first creates the hint from a pattern. Watching\n"
			"the dashless spelling must then also succeed, both watchers must be notified\n"
			"of state changes of the hint's device, and both must be destroyed exactly once.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return dash_spelling_check(test, "2000", "y", 1);
}

AST_TEST_DEFINE(add_failure_ownership)
{
	struct test_watcher *watcher;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = TEST_CATEGORY;
		info->summary = "Failed callback registration leaves data owned by the caller";
		info->description =
			"Adding a callback for an extension that does not exist must fail without\n"
			"invoking the destroy callback, so the caller can release its own data.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	watcher = test_watcher_alloc("3000");
	if (!watcher) {
		return AST_TEST_FAIL;
	}

	if (!test_watcher_add(test, "test_extension_state_nonexistent", watcher)) {
		ast_test_status_update(test, "Callback for nonexistent extension unexpectedly added\n");
		test_watcher_remove(test, watcher);
		res = AST_TEST_FAIL;
	} else {
		/*
		 * Any destroy callback on the failure path happens synchronously while
		 * the add unwinds, so it has already been counted.
		 */
		ast_mutex_lock(&test_lock);
		if (watcher->destroyed) {
			res = AST_TEST_FAIL;
		}
		ast_mutex_unlock(&test_lock);
	}

	ao2_ref(watcher, -1);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(dash_spelling_dashless_first);
	AST_TEST_UNREGISTER(dash_spelling_dashed_first);
	AST_TEST_UNREGISTER(add_failure_ownership);
	ast_cond_destroy(&test_cond);

	return 0;
}

static int load_module(void)
{
	ast_cond_init(&test_cond, NULL);

	/* Start somewhere other than where an earlier load of this module likely started */
	test_run = ast_random() % 100000;

	AST_TEST_REGISTER(dash_spelling_dashless_first);
	AST_TEST_REGISTER(dash_spelling_dashed_first);
	AST_TEST_REGISTER(add_failure_ownership);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Extension state tests");
