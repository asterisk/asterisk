/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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

   Each defined test has three arguments avaliable to it's test code.
       \param struct ast_test_info *info
       \param enum ast_test_command cmd
       \param struct ast_test_args *args

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
          ast_str_set(&args->ast_test_error_str, 0 , "an error occured because...");
          res = AST_RESULT_FAIL;
      } else {
          res = AST_RESULT_PASS
      }
      return res;                 \\ result must be of type enum ast_test_result_state
   }
\endcode

   Every callback function is passed an ast_test_args object which contains
   an ast_str allowing the function to provide an optional short description of
   what went wrong if the test failed. This is done by writing to
   args->ast_test_error_str.

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

#define AST_TEST_DEFINE(hdr) static enum ast_test_result_state hdr(struct ast_test_info *info, enum ast_test_command cmd, struct ast_test_args *args)
#define AST_TEST_REGISTER(cb) ast_test_register(cb)
#define AST_TEST_UNREGISTER(cb) ast_test_unregister(cb)

#else

#define AST_TEST_DEFINE(hdr) static enum ast_test_result_state attribute_unused hdr(struct ast_test_info *info, enum ast_test_command cmd, struct ast_test_args *args)
#define AST_TEST_REGISTER(cb)
#define AST_TEST_UNREGISTER(cb)

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
 *  This struct is passed to ast_test_status_update() providing a place to push
 *  the update to. In the future this structure may expand beyond simply being
 *  a wrapper for cli args to including other status update options as well.
 */
struct ast_test_status_args {
	/*! pointer to cli arg used for updating status */
	struct ast_cli_args *cli;
};

/*!
 * tools made available to the callback function during test execution
 */
struct ast_test_args {
	struct ast_str *ast_test_error_str;  /*! optional error str to describe error result */
	struct ast_test_status_args status_update;
};

/*!
 * Contains all the initilization information required to store a new test definition
 */
struct ast_test_info {
	/*! name of test, unique to category */
	const char *name;
	/*! test category */
	const char *category;
	/*! optional short summary of test */
	const char *summary;
	/*! optional brief detailed description of test */
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
typedef enum ast_test_result_state (ast_test_cb_t)(struct ast_test_info *info, enum ast_test_command cmd, struct ast_test_args *args);

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
 * \param ast_test_status_args defines everywhere the update should go.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_test_status_update(struct ast_test_status_args *args, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

#endif /* TEST_FRAMEWORK */
#endif /* _AST_TEST_H */
