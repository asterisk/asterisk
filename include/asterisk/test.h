/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2010, Digium, Inc.
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

#endif

/*! Macros used for the Asterisk Test Suite AMI events */
#ifdef TEST_FRAMEWORK

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
 * \returns 0 on success
 * \returns any other value on failure
 */
int __ast_test_suite_event_notify(const char *file, const char *func, int line,
		const char *state, const char *fmt, ...)
		__attribute__((format(printf, 5, 6)));

/*!
 * \brief Notifies the test suite of a failed assert on an expression
 *
 * \details
 * If the expression provided evaluates to true, no action is taken.  If the expression
 * evaluates to a false, a TestEvent manager event is raised with a subtype of Assert, notifying
 * the test suite that the expression failed to evaluate to true.
 *
 * \param exp	The expression to evaluate
 *
 * \returns 0 on success
 * \returns any other value on failure
 */
int __ast_test_suite_assert_notify(const char *file, const char *func, int line,
		const char *exp);

/*!
 * \ref __ast_test_suite_event_notify()
 */
#define ast_test_suite_event_notify(s, f, ...) \
	__ast_test_suite_event_notify(__FILE__, __PRETTY_FUNCTION__, __LINE__, (s), (f), ## __VA_ARGS__)

/*!
 * \ref __ast_test_suite_assert_notify()
 */
#define ast_test_suite_assert(exp) \
	( (exp) ? (void)0 : __ast_test_suite_assert_notify(__FILE__, __PRETTY_FUNCTION__, __LINE__, #exp))

#else

#define ast_test_suite_event_notify(s, f, ...) (void)0;
#define ast_test_suite_assert(exp) (void)0;

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
 * \param error buffer string for failure results
 *
 * \retval AST_TEST_PASS for pass
 * \retval AST_TEST_FAIL for failure
 */
typedef enum ast_test_result_state (ast_test_cb_t)(struct ast_test_info *info,
		enum ast_test_command cmd, struct ast_test *test);

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
int ast_test_register(ast_test_cb_t *cb);

/*!
 * \brief update test's status during testing.
 *
 * \param test currently executing test
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __ast_test_status_update(const char *file, const char *func, int line,
		struct ast_test *test, const char *fmt, ...)
		__attribute__((format(printf, 5, 6)));

/*!
 * \ref __ast_test_status_update()
 */
#define ast_test_status_update(t, f, ...) __ast_test_status_update(__FILE__, __PRETTY_FUNCTION__, __LINE__, (t), (f), ## __VA_ARGS__)

/*!
 * \brief Check a test condition, failing the test if it's not true.
 *
 * \since 11.14.0
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
#define ast_test_validate(test, condition)				\
	do {								\
		if (!(condition)) {					\
			__ast_test_status_update(__FILE__, __PRETTY_FUNCTION__, __LINE__, (test), "Condition failed: %s\n", #condition); \
			return AST_TEST_FAIL;				\
		}							\
	} while(0)

#endif /* TEST_FRAMEWORK */
#endif /* _AST_TEST_H */
