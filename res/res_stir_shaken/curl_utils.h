/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#ifndef _CURL_UTILS_H
#define _CURL_UTILS_H

#include <curl/curl.h>
#include "asterisk/acl.h"

#define AST_CURL_DEFAULT_MAX_HEADER_LEN 2048

#ifndef CURL_WRITEFUNC_ERROR
#define CURL_WRITEFUNC_ERROR 0
#endif

/*! \defgroup curl_wrappers CURL Convenience Wrappers
 * @{

\section Overwiew Overview

While libcurl is extremely flexible in what it allows you to do,
that flexibility comes at complexity price. The convenience wrappers
defined here aim to take away some of that complexity for run-of-the-mill
requests.

\par A Basic Example

If all you need to do is receive a document into a buffer...

\code
	char *url = "https://someurl";
	size_t returned_length;
	char *returned_data = NULL;

	long rc = ast_curler_simple(url, &returned_length, &returned_data, NULL);

	ast_log(LOG_ERROR, "rc: %ld  size: %zu  doc: %.*s \n",
		rc, returned_length,
		(int)returned_length, returned_data);
	ast_free(returned_data);
\endcode

If you need the headers as well...

\code
	char *url = "https://someurl";
	size_t returned_length;
	char *returned_data = NULL;
	struct ast_variable *headers;

	long rc = ast_curler_simple(url, &returned_length, &returned_data,
		&headers);

	ast_log(LOG_ERROR, "rc: %ld  size: %zu  doc: %.*s \n",
		rc, returned_length,
		(int)returned_length, returned_data);

	ast_free(returned_data);
	ast_variables_destroy(headers);
\endcode

\par A More Complex Example

If you need more control, you can specify callbacks to capture
the response headers, do something other than write the data
to a memory buffer, or do some special socket manipulation like
check that the server's IP address matched an acl.

Let's write the data to a file, capture the headers,
and make sure the server's IP address is whitelisted.

The default callbacks can do that so all we need to do is
supply the data.

\code
	char *url = "http://something";

	struct ast_curl_write_data data = {
		.output = fopen("myfile.txt", "w");
		.debug_info = url,
	};
	struct ast_curl_header_data hdata = {
		.debug_info = url,
	};
	struct ast_curl_open_socket_data osdata = {
		.acl = my_acl_list,
		.debug_info = url,
	};
	struct ast_curl_optional_data opdata = {
		.open_socket_cb = ast_curl_open_socket_cb,
		.open_socket_data = &osdata,
	};

	long rc = ast_curler(url, 0, ast_curl_write_default_cb, &data,
		ast_curl_header_default_cb, &hdata, &opdata);

	fclose(data.output);
	ast_variables_destroy(hdata.headers);

\endcode

If you need even more control, you can supply your own
callbacks as well.  This is a silly example of providing
your own write callback.  It's basically what
ast_curler_write_to_file() does.

\code
static size_t my_write_cb(char *data, size_t size,
	size_t nmemb, void *client_data)
{
	FILE *fp = (FILE *)client_data;
	return fwrite(data, size, nmemb, fp);
}

static long myfunc(char *url, char *file)
{
	FILE *fp = fopen(file, "w");
	long rc = ast_curler(url, 0, my_write_cb, fp, NULL, NULL, NULL);
	fclose(fp);
	return rc;
}
\endcode
 */

/*!
 * \defgroup HeaderCallback  Header Callback
 * \ingroup curl_wrappers
 * @{
 *
 * If you need to access the headers returned on the response,
 * you can define a callback that curl will call for every
 * header it receives.
 *
 * Your callback must follow the specification defined for
 * CURLOPT_HEADERFUNCTION and implement the curl_write_callback
 * prototype.
 *
 * The following ast_curl_headers objects compose a default
 * implementation that will accumulate the headers in an
 * ast_variable list.
 */

/*!
 *
 * \brief Context structure passed to \ref ast_curl_header_default_cb
 *
 */
struct curl_header_data {
	/*!
	 * curl's default max header length is 100k but we rarely
	 * need that much. It's also possible that a malicious remote
	 * server could send tons of 100k headers in an attempt to
	 * cause an out-of-memory condition.  Setting this value
	 * will cause us to simply ignore any header with a length
	 * that exceeds it.  If not set, the length defined in
	 * #AST_CURL_DEFAULT_MAX_HEADER_LEN will be used.
	 */
	size_t max_header_len;
	/*!
	 * Identifying info placed at the start of log and trace messages.
	 */
	char *debug_info;
	/*!
	 * This list will contain all the headers received.
	 * \note curl converts all header names to lower case.
	 */
	struct ast_variable *headers;
	/*!
	 * \internal
	 * Private flag used to keep track of whether we're
	 * capturing headers or not.  We only want them after
	 * we've seen an HTTP response code in the 2XX range
	 * and before the blank line that separaes the headers
	 * from the body.
	 */
	int _capture;
};

/*!
 * \brief A default implementation of a header callback.
 *
 * This is an implementation of #CURLOPT_HEADERFUNCTION that performs
 * basic sanity checks and saves headers in the
 * ast_curl_header_data.headers ast_variable list.
 *
 * The curl prototype for this function is \ref curl_write_callback
 *
 * \warning If you decide to write your own callback, curl doesn't
 * guarantee a terminating NULL in data passed to the callbacks!
 *
 * \param data Will contain a header line that may not be NULL terminated.
 * \param size Always 1.
 * \param nitems The number of bytes in data.
 * \param client_data A pointer to whatever structure you passed to
 *        \ref ast_curler in the \p curl_header_data parameter.
 *
 * \return Number of bytes handled.  Must be (size * nitems) or an
 *         error is signalled.
 */
size_t curl_header_cb(char *data, size_t size,
	size_t nitems, void *client_data);

void curl_header_data_free(void *obj);

/*!
 *  @}
 */

/*!
 * \defgroup DataCallback  Received Data Callback
 * \ingroup curl_wrappers
 * @{
 *
 * If you need to do something with the data received other than
 * save it in a memory buffer, you can define a callback that curl
 * will call for each "chunk" of data it receives from the server.
 *
 * Your callback must follow the specification defined for
 * CURLOPT_WRITEFUNCTION and implement the 'curl_write_callback'
 * prototype.
 *
 * The following ast_curl_write objects compose a default
 * implementation that will write the data to any FILE *
 * descriptor you choose.
 */

/*!
 * \brief Context structure passed to \ref ast_curl_write_default_cb.
 */
struct curl_write_data {
	/*!
	 * If this value is > 0, the request will be cancelled when
	 * \a bytes_downloaded exceeds it.
	 */
	size_t max_download_bytes;
	/*!
	 * Where to write to.  Could be anything you can get a FILE* for.
	 * A file opened with fopen, a buffer opened with open_memstream(), etc.
	 * Required by \ref ast_curl_write_default_cb.
	 */
	FILE *output;
	/*!
	 * Identifying info placed at the start of log and trace messages.
	 */
	char *debug_info;
	/*!
	 * Keeps track of the number of bytes read so far.
	 * This is updated by the callback regardless of
	 * whether the output stream is updating
	 * \ref stream_bytes_downloaded.
	 */
	size_t bytes_downloaded;
	/*!
	 * A buffer to be used for anything the output stream needs.
	 * For instance, the address of this member can be passed to
	 * open_memstream which will update it as it reads data. When
	 * the memstream is flushed/closed, this will contain all of
	 * the data read so far.  You must free this yourself with
	 * ast_std_free().
	 */
	char *stream_buffer;
	/*!
	 * Keeps track of the number of bytes read so far.
	 * Can be used by memstream.
	 */
	size_t stream_bytes_downloaded;
	/*!
	 * \internal
	 * Set if we automatically opened a memstream
	 */
	int _internal_memstream;
};

/*!
 * \brief A default implementation of a write data callback.

 * This is a default implementation of the function described
 * by CURLOPT_WRITEFUNCTION that writes data received to a
 * user-provided FILE *.  This function is called by curl itself
 * when it determines it has enough data to warrant a write.
 * This may be influenced by the value of
 * ast_curl_optional_data.per_write_buffer_size.
 * See the CURLOPT_WRITEFUNCTION documentation for more info.
 *
 * The curl prototype for this function is 'curl_write_callback'
 *
 * \param data Data read by curl.
 * \param size Always 1.
 * \param nitems The number of bytes read.
 * \param client_data A pointer to whatever structure you passed to
 *        \ref ast_curler in the \p curl_write_data parameter.
 *
 * \return Number of bytes handled.  Must be (size * nitems) or an
 *         error is signalled.
 */
size_t curl_write_cb(char *data, size_t size, size_t nmemb, void *clientp);

void curl_write_data_free(void *obj);

/*!
 *  @}
 */

/*!
 * \defgroup OpenSocket  Open Socket Callback
 * \ingroup curl_wrappers
 * @{
 *
 * If you need to allocate the socket curl uses to make the
 * request yourself or you need to do some checking on the
 * request's resolved IP address, this is the callback for you.
 *
 * Your callback must follow the specification defined for
 * CURLOPT_OPENSOCKETFUNCTION and implement the
 * 'curl_opensocket_callback' prototype.
 *
 * The following ast_open_socket objects compose a default
 * implementation that will not allow requests to servers
 * not whitelisted in the provided ast_acl_list.
 *
 */

/*!
 * \brief Context structure passed to \ref ast_curl_open_socket_default_cb
 */
struct curl_open_socket_data {
	/*!
	 * The acl should provide a whitelist.  Request to servers
	 * with addresses not allowed by the acl will be rejected.
	 */
	const struct ast_acl_list *acl;
	/*!
	 * Identifying info placed at the start of log and trace messages.
	 */
	char *debug_info;
	/*!
	 * \internal
	 * Set by the callback and passed to curl.
	 */
	curl_socket_t sockfd;
};

/*!
 * \brief A default implementation of an open socket callback.

 * This is an implementation of the function described
 * by CURLOPT_OPENSOCKETFUNCTION that checks the request's IP
 * address against a user-supplied ast_acl_list and either rejects
 * the request if the IP address isn't allowed, or opens a socket
 * and returns it to curl.
 * See the CURLOPT_OPENSOCKETFUNCTION documentation for more info.
 *
 * \param client_data A pointer to whatever structure you passed to
 *        \ref ast_curler in the \p curl_write_data parameter.
 * \param purpose Will always be CURLSOCKTYPE_IPCXN
 * \param address The request server's resolved IP address
 *
 * \return A socket opened by socket() or -1 to signal an error.
 */
curl_socket_t curl_open_socket_cb(void *client_data,
	curlsocktype purpose, struct curl_sockaddr *address);

void curl_open_socket_data_free(void *obj);

/*!
 *  @}
 */

/*!
 * \defgroup OptionalData  Optional Data
 * \ingroup curl_wrappers
 * @{

 * \brief Structure pased to \ref ast_curler with infrequenty used
 * control data.
 */
struct curl_optional_data {
	/*!
	 * If not set, AST_CURL_USER_AGENT
	 * (defined in asterisk.h) will be used.
	 */
	const char *user_agent;
	/*!
	 * Set this to limit the amount of data in each call to
	 * ast_curl_write_cb_t.
	 */
	size_t per_write_buffer_size;
	/*!
	 * Set this to a custom function that has a matching
	 * prototype, set it to \ref ast_curl_open_socket_default_cb
	 * to use the default callback, or leave it at NULL
	 * to not use any callback.
	 * \note Will not be called if open_socket_data is NULL.
	 */
	curl_opensocket_callback curl_open_socket_cb;
	/*!
	 * Set this to whatever your curl_open_socket_cb needs.
	 * If using \ref ast_curl_open_socket_default_cb, this MUST
	 * be set to an \ref ast_curl_open_socket_data structure.
	 * If set to NULL, curl_open_socket_cb will not be called.
	 */
	void *curl_open_socket_data;
};

/*!
 *  @}
 */

/*!
 * \defgroup requests Making Requests
 * \ingroup curl_wrappers
 * @{
 */

/*!
 * \brief Perform a curl request.
 *
 * \param url The URL to request.
 * \param request_timeout If > 0, timeout after this number of seconds.
 * \param curl_write_data A pointer to a \ref curl_write_data structure. If
 *        curl_write_data.output is NULL, open_memstream will be called to
 *        provide one and the resulting data will be available in
 *        curl_write_data.stream_buffer with the number of bytes
 *        retrieved in curl_write_data.stream_bytes_downloaded.
 *        You must free curl_write_data.stream_buffer yourself with
 *        ast_std_free() when you no longer need it.
 * \param curl_header_data A pointer to a \ref ast_curl_header_data structure.
 *        The headers read will be in the curl_header_data.headers
 *        ast_variable list which you must free with ast_variables_destroy()
 *        when you're done with them.
 * \param curl_open_socket_data A pointer to an \ref curl_open_socket_data
 *        structure or NULL if you don't need it.
 * \retval An HTTP response code.
 * \retval -1 for internal error.
 */
long curler(const char *url, int request_timeout,
	struct curl_write_data *write_data,
	struct curl_header_data *header_data,
	struct curl_open_socket_data *open_socket_data);

/*!
 * \brief Really simple document retrieval to memory
 *
 * \param url The URL to retrieve
 * \param returned_length Pointer to a size_t to hold document length.
 * \param returned_data Pointer to a buffer which will be updated to
 *        point to the data.  Must be freed with ast_std_free() after use.
 * \param headers Pointer to an ast_variable * that will contain
 *        the response headers. Must be freed with ast_variables_destroy()
 *        Set to NULL if you don't need the headers.
 * \retval An HTTP response code.
 * \retval -1 for internal error.
 */
long curl_download_to_memory(const char *url, size_t *returned_length,
	char **returned_data, struct ast_variable **headers);

/*!
 * \brief Really simple document retrieval to file
 *
 * \param url The URL to retrieve.
 * \param filename The filename to save it to.
 * \retval An HTTP response code.
 * \retval -1 for internal error.
 */
long curl_download_to_file(const char *url, char *filename);

/*!
 *  @}
 */

/*!
 *  @}
 */
#endif /* _CURL_UTILS_H */
