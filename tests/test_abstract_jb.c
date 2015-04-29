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
 * \brief Abstract Jitterbuffer Tests
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * Tests the abstract jitter buffer API.  This tests both adaptive and fixed
 * jitter buffers.  Functions defined in abstract_jb that are not part of the
 * abstract jitter buffer API are not tested by this unit test.
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"

#define DEFAULT_FRAME_MS 160
#define DEFAULT_CONFIG_FLAGS 0
#define DEFAULT_CONFIG_SIZE (DEFAULT_FRAME_MS) * 10
#define DEFAULT_CONFIG_RESYNC_THRESHOLD (DEFAULT_FRAME_MS) * 2
#define DEFAULT_CONFIG_TARGET_EXTRA -1

/*!
 * \internal
 * \brief Destructor for a jitter buffer
 *
 * \param jb The jitter buffer to destroy
 *
 * \note This will destroy all frames still in the jitter buffer
 */
static void dispose_jitterbuffer(struct ast_jb *jb)
{
	if (!jb || !jb->impl || !jb->jbobj) {
		return;
	}

	jb->impl->empty_and_reset(jb->jbobj);

	jb->impl->destroy(jb->jbobj);
	jb->impl = NULL;
	jb->jbobj = NULL;
}

/*!
 * \internal
 * \brief Create a test frame
 *
 * \param timestamp the time in ms of the frame
 * \param seqno the frame's sequence number
 *
 * \returns a malloc'd frame
 */
static struct ast_frame *create_test_frame(long timestamp,
		int seqno)
{
	struct ast_frame f = {0};

	f.subclass.format = ast_format_slin;
	f.frametype = AST_FRAME_VOICE;
	f.src = "TEST";
	f.ts = timestamp;
	f.len = DEFAULT_FRAME_MS;
	f.seqno = seqno;

	return ast_frisolate(&f);
}

/*! \internal
 * \brief Test two numeric (long int) values.
*/
#define LONG_INT_TEST(actual, expected) do { \
	if ((actual) != (expected)) { \
		ast_test_status_update(test, #actual ": expected [%ld]; actual [%ld]\n", (long int)(expected), (long int)(actual)); \
		return AST_TEST_FAIL; \
	} } while (0)

/*! \internal
 * \brief Test two numeric (int) values.
*/
#define INT_TEST(actual, expected) do { \
	if ((actual) != (expected)) { \
		ast_test_status_update(test, #actual ": expected [%d]; actual [%d]\n", (expected), (actual)); \
		return AST_TEST_FAIL; \
	} } while (0)

/*! \internal
 * \brief Test two numeric (unsigned int) values.
*/
#define UINT_TEST(actual, expected) do { \
	if ((actual) != (expected)) { \
		ast_test_status_update(test, #actual ": expected [%u]; actual [%u]\n", (expected), (actual)); \
		return AST_TEST_FAIL; \
	} } while (0)

/*! \internal
 * \brief Test two string values
*/
#define STRING_TEST(actual, expected) do { \
	if (strcmp((actual), (expected))) { \
		ast_test_status_update(test, #actual ": expected [%s]; actual [%s]\n", (expected), (actual)); \
		return AST_TEST_FAIL; \
	} } while (0)

/*! \internal
 * \brief Verify that two frames have the same properties
 */
#define VERIFY_FRAME(actual, expected) do { \
	UINT_TEST((actual)->frametype, (expected)->frametype); \
	INT_TEST((actual)->seqno, (expected)->seqno); \
	LONG_INT_TEST((actual)->ts, (expected)->ts); \
	LONG_INT_TEST((actual)->len, (expected)->len); \
	STRING_TEST((actual)->src, (expected)->src); \
} while (0)

/*! \internal
 * \brief Get the implementation for a jitter buffer
 */
#define OBTAIN_JITTERBUFFER_IMPL(impl, ast_jb_type, literal_name) do { \
	(impl) = ast_jb_get_impl((ast_jb_type)); \
	if (!(impl)) { \
		ast_test_status_update(test, "Error: no %s jitterbuffer defined\n", (literal_name)); \
		return AST_TEST_FAIL; \
	} \
	if (strcmp((impl)->name, (literal_name))) { \
		ast_test_status_update(test, "Error: requested %s jitterbuffer and received %s\n", (literal_name), (impl)->name); \
		return AST_TEST_FAIL; \
	} } while (0)

/*! \internal
 * \brief Make a jitter buffer configuration object with default values
 */
#define MAKE_DEFAULT_CONFIG(conf, impl) do { \
	(conf)->flags = DEFAULT_CONFIG_FLAGS; \
	strcpy((conf)->impl, (impl)->name); \
	(conf)->max_size = DEFAULT_CONFIG_SIZE; \
	(conf)->resync_threshold = DEFAULT_CONFIG_RESYNC_THRESHOLD; \
	(conf)->target_extra = DEFAULT_CONFIG_TARGET_EXTRA; \
	} while (0)

/*!
 * \internal
 * \brief A container object for the jitter buffers, used for all tests
 */
static struct ast_jb default_jb = {
	.impl = NULL,
	.jbobj = NULL
};

/*!
 * \internal
 * \brief Construct a test name
 */
#define TEST_NAME(type_name, specifier) type_name ## _  ## specifier

#define TEST_NAME2(test_name) #test_name
#define STRINGIFY_TESTNAME(test_name) TEST_NAME2(test_name)

/*!
 * \internal
 * \brief Test nominal construction of a jitter buffer
 *
 * \param type_name The enum type of the jitter buffer to create
 * \param literal_type_name The literal name of the type - "fixed" or "adaptive"
 */
#define test_create_nominal(type_name, literal_type_name) AST_TEST_DEFINE(TEST_NAME(type_name, create)) {\
	RAII_VAR(struct ast_jb *, jb, &default_jb, dispose_jitterbuffer); \
	const struct ast_jb_impl *impl; \
	struct ast_jb_conf conf; \
\
	switch (cmd) { \
	case TEST_INIT: \
		info->name = STRINGIFY_TESTNAME(TEST_NAME(type_name, create)); \
		info->category = "/main/abstract_jb/"; \
		info->summary = "Test nominal creation of a " literal_type_name " jitterbuffer"; \
		info->description = \
			"Tests nominal creation of a " literal_type_name " jitterbuffer using the " \
			" jitterbuffer API."; \
		return AST_TEST_NOT_RUN; \
	case TEST_EXECUTE: \
		break; \
	} \
 \
	ast_test_status_update(test, "Executing " STRINGIFY_TESTNAME(TEST_NAME(type_name,  create))"...\n"); \
	OBTAIN_JITTERBUFFER_IMPL(impl, (type_name), (literal_type_name)); \
	MAKE_DEFAULT_CONFIG(&conf, impl); \
 \
	jb->jbobj = impl->create(&conf); \
	jb->impl = impl; \
	if (!jb->jbobj) { \
		ast_test_status_update(test, "Error: Failed to adaptive jitterbuffer\n"); \
		return AST_TEST_FAIL; \
	} \
 \
	return AST_TEST_PASS; \
}

/*!
 * \internal
 * \brief Test putting the initial frame into a jitter buffer
 *
 * \param type_name The enum type of the jitter buffer to create
 * \param literal_type_name The literal name of the type - "fixed" or "adaptive"
 */
#define test_put_first(type_name, literal_type_name) AST_TEST_DEFINE(TEST_NAME(type_name,  put_first)) {\
	RAII_VAR(struct ast_jb *, jb, &default_jb, dispose_jitterbuffer); \
	const struct ast_jb_impl *impl; \
	struct ast_jb_conf conf; \
	RAII_VAR(struct ast_frame *, expected_frame, NULL, ast_frame_dtor); \
	RAII_VAR(struct ast_frame *, actual_frame, NULL, ast_frame_dtor); \
	int res; \
\
	switch (cmd) { \
	case TEST_INIT: \
		info->name = STRINGIFY_TESTNAME(TEST_NAME(type_name,  put_first)); \
		info->category = "/main/abstract_jb/"; \
		info->summary = "Test putting a frame into a " literal_type_name " jitterbuffer"; \
		info->description = \
			"This tests putting a single frame into a " literal_type_name " jitterbuffer " \
			"when the jitterbuffer is empty and verifying that it is indeed " \
			"the first frame on the jitterbufffer"; \
		return AST_TEST_NOT_RUN; \
	case TEST_EXECUTE: \
		break; \
	} \
\
	ast_test_status_update(test, "Executing " STRINGIFY_TESTNAME(TEST_NAME(type_name,  create))"...\n"); \
	OBTAIN_JITTERBUFFER_IMPL(impl, (type_name), (literal_type_name)); \
	MAKE_DEFAULT_CONFIG(&conf, impl); \
	jb->jbobj = impl->create(&conf); \
	jb->impl = impl; \
	if (!jb->jbobj) { \
		ast_test_status_update(test, "Error: Failed to adaptive jitterbuffer\n"); \
		return AST_TEST_FAIL; \
	} \
\
	expected_frame = create_test_frame(1000, 0); \
	res = jb->impl->put_first(jb->jbobj, \
		expected_frame, \
		1100); \
	if (res != AST_JB_IMPL_OK) { \
		ast_test_status_update(test, "Error: Got %d back from put_first (expected %d)\n", \
			res, AST_JB_IMPL_OK); \
		return AST_TEST_FAIL; \
	} \
\
	res = jb->impl->remove(jb->jbobj, &actual_frame); \
	if (!actual_frame || res != AST_JB_IMPL_OK) { \
		ast_test_status_update(test, "Error: failed to retrieve first frame\n"); \
		return AST_TEST_FAIL; \
	} \
	expected_frame = create_test_frame(1000, 0); \
	VERIFY_FRAME(actual_frame, expected_frame); \
	return AST_TEST_PASS; \
}

/*!
 * \internal
 * \brief Test putting a voice frames into a jitter buffer
 *
 * \param type_name The enum type of the jitter buffer to create
 * \param literal_type_name The literal name of the type - "fixed" or "adaptive"
 */
#define test_put(type_name, literal_type_name) AST_TEST_DEFINE(TEST_NAME(type_name, put)) {\
	RAII_VAR(struct ast_jb *, jb, &default_jb, dispose_jitterbuffer); \
	const struct ast_jb_impl *impl; \
	struct ast_jb_conf conf; \
	RAII_VAR(struct ast_frame *, expected_frame, NULL, ast_frame_dtor); \
	RAII_VAR(struct ast_frame *, actual_frame, NULL, ast_frame_dtor); \
	int res; \
	long next; \
	int i; \
\
	switch (cmd) { \
	case TEST_INIT: \
		info->name = STRINGIFY_TESTNAME(TEST_NAME(type_name, put)); \
		info->category = "/main/abstract_jb/"; \
		info->summary = "Test putting frames onto a " literal_type_name " jitterbuffer"; \
		info->description = \
			"This tests putting multiple frames into a " literal_type_name " jitterbuffer"; \
		return AST_TEST_NOT_RUN; \
	case TEST_EXECUTE: \
		break; \
	} \
\
	ast_test_status_update(test, "Executing "STRINGIFY_TESTNAME(TEST_NAME(type_name, put))"...\n"); \
	OBTAIN_JITTERBUFFER_IMPL(impl, (type_name), (literal_type_name)); \
	MAKE_DEFAULT_CONFIG(&conf, impl); \
	jb->jbobj = impl->create(&conf); \
	jb->impl = impl; \
\
	expected_frame = create_test_frame(1000, 0); \
	jb->impl->put_first(jb->jbobj, expected_frame, 1100); \
	for (i = 1; i < 10; i++) { \
		expected_frame = create_test_frame(1000 + i * DEFAULT_FRAME_MS, 0); \
		res = jb->impl->put(jb->jbobj, \
			expected_frame, \
			1100 + i * DEFAULT_FRAME_MS); \
		if (res != AST_JB_IMPL_OK) { \
			ast_test_status_update(test, "Error: On frame %d, got %d back from put (expected %d)\n", \
				i, res, AST_JB_IMPL_OK); \
			return AST_TEST_FAIL; \
		} \
	} \
\
	for (i = 0; i < 10; i++) { \
		expected_frame = create_test_frame(1000 + i * DEFAULT_FRAME_MS, 0); \
		next = jb->impl->next(jb->jbobj); \
		res = jb->impl->get(jb->jbobj, &actual_frame, next, DEFAULT_FRAME_MS); \
		if (res != AST_JB_IMPL_OK) { \
			ast_test_status_update(test, "Error: failed to retrieve frame %i at time %ld\n", \
				i, next); \
			return AST_TEST_FAIL; \
		} \
		VERIFY_FRAME(actual_frame, expected_frame); \
		ast_frfree(expected_frame); \
		expected_frame = NULL; \
	} \
	return AST_TEST_PASS; \
}

/*!
 * \internal
 * \brief Test overflowing the limits of a jitter buffer
 *
 * \param type_name The enum type of the jitter buffer to create
 * \param literal_type_name The literal name of the type - "fixed" or "adaptive"
 * \param overflow_limit The number of frames at which we expect the buffer to overflow
 */
#define test_put_overflow(type_name, literal_type_name, overflow_limit) AST_TEST_DEFINE(TEST_NAME(type_name, put_overflow)) {\
	RAII_VAR(struct ast_jb *, jb, &default_jb, dispose_jitterbuffer); \
	const struct ast_jb_impl *impl; \
	struct ast_jb_conf conf; \
	RAII_VAR(struct ast_frame *, expected_frame, NULL, ast_frame_dtor); \
	int res; \
	int i; \
\
	switch (cmd) { \
	case TEST_INIT: \
		info->name = STRINGIFY_TESTNAME(TEST_NAME(type_name,  put_overflow)); \
		info->category = "/main/abstract_jb/"; \
		info->summary = "Test putting frames onto a " literal_type_name " jitterbuffer " \
			"that ends up overflowing the maximum allowed slots in the buffer"; \
		info->description = \
			"This tests putting multiple frames into a " literal_type_name " jitterbuffer " \
			"until the jitterbuffer overflows"; \
		return AST_TEST_NOT_RUN; \
	case TEST_EXECUTE: \
		break; \
	} \
\
	ast_test_status_update(test, "Executing "STRINGIFY_TESTNAME(TEST_NAME(type_name, put_overflow))"...\n"); \
	OBTAIN_JITTERBUFFER_IMPL(impl, (type_name), (literal_type_name)); \
	MAKE_DEFAULT_CONFIG(&conf, impl); \
	jb->jbobj = impl->create(&conf); \
	jb->impl = impl; \
\
	expected_frame = create_test_frame(1000, 0); \
	jb->impl->put_first(jb->jbobj, expected_frame, 1100); \
	for (i = 1; i <= (overflow_limit); i++) { \
		expected_frame = create_test_frame(1000 + i * DEFAULT_FRAME_MS, 0); \
		res = jb->impl->put(jb->jbobj, \
			expected_frame, \
			1100 + i * DEFAULT_FRAME_MS); \
		if (res != AST_JB_IMPL_OK) { \
			ast_test_status_update(test, "Error: On frame %d, got %d back from put (expected %d)\n", \
				i, res, AST_JB_IMPL_OK); \
			return AST_TEST_FAIL; \
		} \
	} \
\
	for (i = (overflow_limit)+1; i < (overflow_limit) + 5; i++) { \
		expected_frame = create_test_frame(1000 + i * DEFAULT_FRAME_MS, 0); \
		res = jb->impl->put(jb->jbobj, \
			expected_frame, \
			1100 + i * DEFAULT_FRAME_MS); \
		if (res != AST_JB_IMPL_DROP) { \
			expected_frame = NULL; \
			ast_test_status_update(test, "Error: On frame %d, got %d back from put (expected %d)\n", \
				i, res, AST_JB_IMPL_DROP); \
			return AST_TEST_FAIL; \
		} \
		ast_frfree(expected_frame); \
		expected_frame = NULL;\
	} \
\
	return AST_TEST_PASS; \
}

/*!
 * \internal
 * \brief Test putting voice frames into a jitter buffer out of order
 *
 * \param type_name The enum type of the jitter buffer to create
 * \param literal_type_name The literal name of the type - "fixed" or "adaptive"
 * \param synch_limit The synchronization limit for this particular type of jitter buffer
 */
#define test_put_out_of_order(type_name, literal_type_name, synch_limit) AST_TEST_DEFINE(TEST_NAME(type_name, put_out_of_order)) {\
	RAII_VAR(struct ast_jb *, jb, &default_jb, dispose_jitterbuffer); \
	const struct ast_jb_impl *impl; \
	struct ast_jb_conf conf; \
	RAII_VAR(struct ast_frame *, actual_frame, NULL, ast_frame_dtor); \
	RAII_VAR(struct ast_frame *, expected_frame, NULL, ast_frame_dtor); \
	int res; \
	long next; \
	int i; \
\
	switch (cmd) { \
	case TEST_INIT: \
		info->name = STRINGIFY_TESTNAME(TEST_NAME(type_name, put_out_of_order)); \
		info->category = "/main/abstract_jb/"; \
		info->summary = "Test putting out of order frames onto a " literal_type_name " jitterbuffer"; \
		info->description = \
			"This tests putting multiple frames into a " literal_type_name " jitterbuffer " \
			"that arrive out of order.  Every 3rd frame is put in out of order."; \
		return AST_TEST_NOT_RUN; \
	case TEST_EXECUTE: \
		break; \
	} \
\
	ast_test_status_update(test, "Executing " STRINGIFY_TESTNAME(TEST_NAME(type_name, put_out_of_order)) "...\n"); \
	OBTAIN_JITTERBUFFER_IMPL(impl, (type_name), (literal_type_name)); \
	MAKE_DEFAULT_CONFIG(&conf, impl); \
	conf.resync_threshold = (synch_limit); \
	jb->jbobj = impl->create(&conf); \
	jb->impl = impl; \
\
	expected_frame = create_test_frame(1000, 0); \
	jb->impl->put_first(jb->jbobj, expected_frame, 1100); \
	for (i = 1; i <= 10; i++) { \
		if (i % 3 == 1 && i != 10) { \
			expected_frame = create_test_frame(1000 + ((i + 1) * DEFAULT_FRAME_MS), 0); \
		} else if (i % 3 == 2) { \
			expected_frame = create_test_frame(1000 + ((i - 1) * DEFAULT_FRAME_MS), 0); \
		} else { \
			expected_frame = create_test_frame(1000 + i * DEFAULT_FRAME_MS, 0); \
		} \
		res = jb->impl->put(jb->jbobj, \
			expected_frame, \
			1100 + i * DEFAULT_FRAME_MS); \
		if (res != AST_JB_IMPL_OK) { \
			ast_test_status_update(test, "Error: On frame %d, got %d back from put (expected %d)\n", \
				i, res, AST_JB_IMPL_OK); \
			return AST_TEST_FAIL; \
		} \
	} \
\
	for (i = 0; i <= 10; i++) { \
		expected_frame = create_test_frame(1000 + i * DEFAULT_FRAME_MS, 0); \
		next = jb->impl->next(jb->jbobj); \
		res = jb->impl->get(jb->jbobj, &actual_frame, next, DEFAULT_FRAME_MS); \
		if (res != AST_JB_IMPL_OK) { \
			ast_test_status_update(test, "Error: failed to retrieve frame at %ld\n", \
				next); \
			return AST_TEST_FAIL; \
		} \
		VERIFY_FRAME(actual_frame, expected_frame); \
		ast_frfree(expected_frame); \
		expected_frame = NULL; \
	} \
\
	return AST_TEST_PASS; \
}


test_create_nominal(AST_JB_ADAPTIVE, "adaptive")

test_put_first(AST_JB_ADAPTIVE, "adaptive")

test_put(AST_JB_ADAPTIVE, "adaptive")

test_put_overflow(AST_JB_ADAPTIVE, "adaptive", 10)

test_put_out_of_order(AST_JB_ADAPTIVE, "adaptive", DEFAULT_FRAME_MS * 2)

test_create_nominal(AST_JB_FIXED, "fixed")

test_put_first(AST_JB_FIXED, "fixed")

test_put(AST_JB_FIXED, "fixed")

test_put_overflow(AST_JB_FIXED, "fixed", 12)

test_put_out_of_order(AST_JB_FIXED, "fixed", DEFAULT_CONFIG_RESYNC_THRESHOLD)

static int load_module(void)
{
	AST_TEST_REGISTER(TEST_NAME(AST_JB_ADAPTIVE, create));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_ADAPTIVE, put_first));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_ADAPTIVE, put));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_ADAPTIVE, put_overflow));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_ADAPTIVE, put_out_of_order));

	AST_TEST_REGISTER(TEST_NAME(AST_JB_FIXED, create));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_FIXED, put_first));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_FIXED, put));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_FIXED, put_overflow));
	AST_TEST_REGISTER(TEST_NAME(AST_JB_FIXED, put_out_of_order));

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Abstract JitterBuffer API Tests");
