/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2013, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
 * Russell Bryant <russell@digium.com>
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
 * \brief Test Framework API
 *
 * For an overview on how to use the test API, see \ref AstUnitTestAPI
 *
 * \author David Vossel <dvossel@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */

#ifndef _AST_TEST_H_
#define _AST_TEST_H_

#ifdef TEST_FRAMEWORK
#include "asterisk/cli.h"
#include "asterisk/strings.h"
#endif

/*!

\page AstUnitTestAPI Asterisk Unit Test API

\section UnitTestAPIUsage How to Use the Unit Test API

\subsection DefineTest Define a Test

   Create a callback function for the test using the AST_TEST_DEFINE macro.

   Each defined test has three arguments available to it's test code.
       \param struct ast_test_info *info
       \param enum ast_test_command cmd
       \param struct ast_test *test

   While these arguments are not visible they are passed to every test function
   defined using the AST_TEST_DEFINE macro.

   Below is an example of how to define and write a test function.

\code
   AST_TEST_DEFINE(sample_test_cb) \\The name of the callback function
   {                               \\The the function's body
      switch (cmd) {
      case TEST_INIT:
          info->name = "sample_test";
          info->category = "main/test/";
          info->summary = "sample test for example purpose";
          info->description = "This demonstrates how to initialize a test function";

          return AST_TEST_NOT_RUN;
      case TEST_EXECUTE:
          break;
      }
      \test code
      .
      .
      .
      if (fail) {                 \\ the following is just some example logic
          ast_test_status_update(test, "an error occured because...");
          res = AST_RESULT_FAIL;
      } else {
          res = AST_RESULT_PASS
      }
      return res;                 \\ result must be of type enum ast_test_result_state
   }
\endcode

      Details of the test execution, especially failure details, should be provided
      by using the ast_test_status_update() function.

\subsection RegisterTest Register a Test

   Register the test using the AST_TEST_REGISTER macro.

   AST_TEST_REGISTER uses the callback function to retrieve all the information
   pertaining to a test, so the callback function is the only argument required
   for registering a test.

   AST_TEST_REGISTER(sample_test_cb);    \\ Test callback function defined by AST_TEST_DEFINE

   Tests are unregestered by using the AST_TEST_UNREGISTER macro.

   AST_TEST_UNREGISTER(sample_test_cb);  \\ Remove a registered test by callback function

\subsection ExecuteTest Execute a Test

   Execute and generate test results via CLI commands

   CLI Examples:
\code
   'test show registered all'  will show every registered test.
   'test execute all'          will execute every registered test.
   'test show results all'     will show detailed results for ever executed test
   'test generate results xml' will generate a test report in xml format
   'test generate results txt' will generate a test report in txt format
\endcode
*/

/*! Macros used for defining and registering a test */
#ifdef TEST_FRAMEWORK

#define AST_TEST_DEFINE(hdr) static enum ast_test_result_state hdr(struct ast_test_info *info, enum ast_test_command cmd, struct ast_test *test)
#define AST_TEST_REGISTER(cb) ast_test_register(cb)
#define AST_TEST_UNREGISTER(cb) ast_test_unregister(cb)

#else

#define AST_TEST_DEFINE(hdr) static enum ast_test_result_state attribute_unused hdr(struct ast_test_info *info, enum ast_test_command cmd, struct ast_test *test)
#define AST_TEST_REGISTER(cb)
#define AST_TEST_UNREGISTER(cb)
#define ast_test_status_update(a,b,c...)
#define ast_test_debug(test, fmt, ...)	ast_cli		/* Dummy function that should not be called. */

#endif

/*! Macros used for the Asterisk Test Suite AMI events */
#ifdef TEST_FRAMEWORK

struct stasis_topic;
struct stasis_message_type;

/*!
 * \since 12
 * \brief Obtain the \ref stasis_topic for \ref ast_test_suite_event_notify
 * messages
 *
 * \retval A stasis topic
 */
struct stasis_topic *ast_test_suite_topic(void);

/*!
 * \since 12
 * \brief Obtain the \ref stasis_message_type for \ref ast_test_suite_event_notify
 * messages
 *
 * \retval A stasis message type
 */
struct stasis_message_type *ast_test_suite_message_type(void);

/*!
 * \since 12
 * \brief The message payload in a \ref ast_test_suite_message_type
 */
struct ast_test_suite_message_payload;

/*!
 * \since 12
 * \brief Get the JSON for a \ref ast_test_suite_message_payload
 *
 * \retval An \ref ast_json object
 */
struct ast_json *ast_test_suite_get_blob(struct ast_test_suite_message_payload *payload);

/*!
 * \brief Notifies the test suite of a change in application state
 *
 * \details
 * Raises a TestEvent manager event with a subtype of StateChange.  Additional parameters
 * The fmt parameter allows additional parameters to be added to the manager event using
 * printf style statement formatting.
 *
 * \param state		The state the application has changed to
 * \param fmt		The message with format parameters to add to the manager event
 *
 * \return Nothing
 */
void __ast_test_suite_event_notify(const char *file, const char *func, int line, const char *state, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/*!
 * \ref __ast_test_suite_event_notify()
 */
#define ast_test_suite_event_notify(s, f, ...) \
	__ast_test_suite_event_notify(__FILE__, __PRETTY_FUNCTION__, __LINE__, (s), (f), ## __VA_ARGS__)

#else

#define ast_test_suite_event_notify(s, f, ...)

#endif

enum ast_test_result_state {
	AST_TEST_NOT_RUN,
	AST_TEST_PASS,
	AST_TEST_FAIL,
};

enum ast_test_command {
	TEST_INIT,
	TEST_EXECUTE,
};

/*!
 * \brief An Asterisk unit test.
 *
 * This is an opaque type.
 */
struct ast_test;

/*!
 * \brief Contains all the initialization information required to store a new test definition
 */
struct ast_test_info {
	/*! \brief name of test, unique to category */
	const char *name;
	/*!
	 * \brief test category
	 *
	 * Tests are categorized in a directory tree style hierarchy.  It is expected that
	 * this string have both a leading and trailing forward slash ('/').
	 */
	const char *category;
	/*! \brief optional short summary of test */
	const char *summary;
	/*! \brief optional brief detailed description of test */
	const char *description;
};

#ifdef TEST_FRAMEWORK
/*!
 * \brief Generic test callback function
 *
 * \param info The test info object
 * \param cmd What to perform in the test
 * \param test The actual test object being manipulated
 *
 * \retval AST_TEST_PASS for pass
 * \retval AST_TEST_FAIL for failure
 */
typedef enum ast_test_result_state (ast_test_cb_t)(struct ast_test_info *info,
	enum ast_test_command cmd, struct ast_test *test);

/*!
 * \since 12
 * \brief A test initialization callback function
 *
 * \param info The test info object
 * \param test The actual test object that will be manipulated
 *
 * \retval 0 success
 * \retval other failure. This will fail the test.
 */
typedef int (ast_test_init_cb_t)(struct ast_test_info *info, struct ast_test *test);

/*!
 * \since 12
 * \brief A test cleanup callback function
 *
 * \param info The test info object
 * \param test The actual test object that was executed
 *
 * \retval 0 success
 * \retval other failure. This will fail the test.
 */
typedef int (ast_test_cleanup_cb_t)(struct ast_test_info *info, struct ast_test *test);

/*!
 * \brief unregisters a test with the test framework
 *
 * \param test callback function (required)
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_test_unregister(ast_test_cb_t *cb);

/*!
 * \brief registers a test with the test framework
 *
 * \param test callback function (required)
 *
 * \retval 0 success
 * \retval -1 failure
 */
#define ast_test_register(cb) __ast_test_register(cb, AST_MODULE_SELF)
int __ast_test_register(ast_test_cb_t *cb, struct ast_module *module);

/*!
 * \since 12
 * \brief Register an initialization function to be run before each test
 * executes
 *
 * This function lets a registered test have an initialization function that
 * will be run prior to test execution. Each category may have a single init
 * function.
 *
 * If the initialization function returns a non-zero value, the test will not
 * be executed and the result will be set to \ref AST_TEST_FAIL.
 *
 * \retval 0 success
 * \retval other failure
 */
int ast_test_register_init(const char *category, ast_test_init_cb_t *cb);

/*!
 * \since 12
 * \brief Register a cleanup function to be run after each test executes
 *
 * This function lets a registered test have a cleanup function that will be
 * run immediately after test execution. Each category may have a single
 * cleanup function.
 *
 * If the cleanup function returns a non-zero value, the test result will be
 * set to \ref AST_TEST_FAIL.
 *
 * \retval 0 success
 * \retval other failure
 */
int ast_test_register_cleanup(const char *category, ast_test_cleanup_cb_t *cb);


/*!
 * \brief Unit test debug output.
 * \since 12.0.0
 *
 * \param test Unit test control structure.
 * \param fmt printf type format string.
 *
 * \return Nothing
 */
void ast_test_debug(struct ast_test *test, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/*!
 * \brief Set the result of a test.
 *
 * If the caller of this function sets the result to AST_TEST_FAIL, returning
 * AST_TEST_PASS from the test will not pass the test. This lets a test writer
 * end and fail a test and continue on with logic, catching multiple failure
 * conditions within a single test.
 */
void ast_test_set_result(struct ast_test *test, enum ast_test_result_state state);


/*!
 * \brief update test's status during testing.
 *
 * \param test currently executing test
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __ast_test_status_update(const char *file, const char *func, int line, struct ast_test *test, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

/*!
 * \ref __ast_test_status_update()
 */
#define ast_test_status_update(t, f, ...) __ast_test_status_update(__FILE__, __PRETTY_FUNCTION__, __LINE__, (t), (f), ## __VA_ARGS__)

/*!
 * \brief Check a test condition, failing the test if it's not true.
 *
 * \since 12.0.0
 *
 * This macro evaluates \a condition. If the condition evaluates to true (non-zero),
 * nothing happens. If it evaluates to false (zero), then the failure is printed
 * using \ref ast_test_status_update, and the current test is ended with AST_TEST_FAIL.
 *
 * Sadly, the name 'ast_test_assert' was already taken.
 *
 * Note that since this macro returns from the current test, there must not be any
 * cleanup work to be done before returning. Use \ref RAII_VAR for test cleanup.
 *
 * \param test Currently executing test
 * \param condition Boolean condition to check.
 */
#define ast_test_validate(test, condition, ...)				\
	do {								\
		if (!(condition)) {					\
			__ast_test_status_update(__FILE__, __PRETTY_FUNCTION__, __LINE__, (test), "%s: %s\n", strlen(#__VA_ARGS__) ? #__VA_ARGS__ : "Condition failed", #condition); \
			return AST_TEST_FAIL;				\
		}							\
	} while(0)

/*!
 * \brief Check a test condition, report error and goto cleanup label if failed.
 *
 * \since 13.4.0
 *
 * This macro evaluates \a condition. If the condition evaluates to true (non-zero),
 * nothing happens. If it evaluates to false (zero), then the failure is printed
 * using \ref ast_test_status_update, the variable \a rc_variable is set to AST_TEST_FAIL,
 * and a goto to \a cleanup_label is executed.
 *
 * \param test Currently executing test
 * \param condition Boolean condition to check.
 * \param rc_variable Variable to receive AST_TEST_FAIL.
 * \param cleanup_label The label to go to on failure.
 */
#define ast_test_validate_cleanup(test, condition, rc_variable, cleanup_label) ({ \
	if (!(condition)) {	\
		ast_test_status_update((test), "%s: %s\n", "Condition failed", #condition); \
		rc_variable = AST_TEST_FAIL; \
		goto cleanup_label; \
	} \
})

#endif /* TEST_FRAMEWORK */
#endif /* _AST_TEST_H */
