/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2008, Digium, Inc.
 *
 * Tilghman Lesher <res_curl_v1@the-tilghman.com>
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
 * \brief curl resource engine
 *
 * \author Tilghman Lesher <res_curl_v1@the-tilghman.com>
 *
 * Depends on the CURL library  - http://curl.haxx.se/
 *
 */

/*! \li \ref res_curl.c uses the configuration file \ref res_curl.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page res_curl.conf res_curl.conf
 * \verbinclude res_curl.conf.sample
 */

/*** MODULEINFO
	<depend>curl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <curl/curl.h>

#include "asterisk/module.h"
#include "asterisk/res_curl.h"

size_t ast_curl_header_default_cb(char *data, size_t size,
	size_t nitems, void *client_data)
{
	struct ast_curl_header_data *cb_data = client_data;
	size_t realsize = size * nitems;
	size_t adjusted_size = realsize;
	char *debug_info = S_OR(cb_data->debug_info, "");
	char *start = data;
	char *colon = NULL;
	struct ast_variable *h;
	char *header;
	char *value;
	SCOPE_ENTER(3, "'%s': Header received with %zu bytes\n",
		debug_info, realsize);

	if (cb_data->max_header_len == 0) {
		cb_data->max_header_len = AST_CURL_DEFAULT_MAX_HEADER_LEN;
	}

	if (realsize > cb_data->max_header_len) {
		/*
		 * Silently ignore any header over the length limit.
		 */
		SCOPE_EXIT_RTN_VALUE(realsize, "oversize header: %zu > %zu\n",
			realsize, cb_data->max_header_len);
	}

	/* Per CURL: buffer may not be NULL terminated. */

	/* Skip blanks */
	while (*start && ((unsigned char) *start) < 33 && start < data + realsize) {
		start++;
		adjusted_size--;
	}

	if (adjusted_size < strlen("HTTP/") + 1) {
		/* this is probably the \r\n\r\n sequence that ends the headers */
		cb_data->_capture = 0;
		SCOPE_EXIT_RTN_VALUE(realsize, "undersized header.  probably end-of-headers marker: %zu\n",
			adjusted_size);
	}

	/*
	 * We only want headers from a 2XX response
	 * so don't start capturing until we see
	 * the 2XX.
	 */
	if (ast_begins_with(start, "HTTP/")) {
		int code;
		/*
		 * HTTP/1.1 200 OK
		 * We want there to be a version after the HTTP/
		 * and reason text after the code but we don't care
		 * what they are.
		 */
		int rc = sscanf(start, "HTTP/%*s %d %*s", &code);
		if (rc == 1) {
			if (code / 100 == 2) {
				cb_data->_capture = 1;
			}
		}
		SCOPE_EXIT_RTN_VALUE(realsize, "HTTP response code: %d\n",
			code);
	}

	if (!cb_data->_capture) {
		SCOPE_EXIT_RTN_VALUE(realsize, "not capturing\n");
	}

	header = ast_alloca(adjusted_size + 1);
	ast_copy_string(header, start, adjusted_size + 1);

	/* We have a NULL terminated string now */

	colon = strchr(header, ':');
	if (!colon) {
		SCOPE_EXIT_RTN_VALUE(realsize, "No colon in the eader.  Weird\n");
	}

	*colon++ = '\0';
	value = colon;
	value = ast_skip_blanks(ast_trim_blanks(value));

	h = ast_variable_new(header, value, __FILE__);
	if (!h) {
		SCOPE_EXIT_LOG_RTN_VALUE(CURL_WRITEFUNC_ERROR, LOG_WARNING,
			"'%s': Unable to allocate memory for header '%s'\n",
			debug_info, header);
	}
	ast_variable_list_append(&cb_data->headers, h);

	SCOPE_EXIT_RTN_VALUE(realsize, "header: <%s>  value: <%s>",
		header, value);
}

size_t ast_curl_write_default_cb(char *data, size_t size,
	size_t nmemb, void *client_data)
{
	struct ast_curl_write_data *cb_data = client_data;
	size_t realsize = size * nmemb;
	size_t bytes_written = 0;
	char *debug_info = S_OR(cb_data->debug_info, "");
	SCOPE_ENTER(3, "'%s': Writing data chunk of %zu bytes\n",
		debug_info, realsize);

	if (!cb_data->output) {
		cb_data->output = open_memstream(
			&cb_data->stream_buffer,
			&cb_data->stream_bytes_downloaded);
		if (!cb_data->output) {
			SCOPE_EXIT_LOG_RTN_VALUE(CURL_WRITEFUNC_ERROR, LOG_WARNING,
				"'%s': Xfer failed. "
				"open_memstream failed: %s\n", debug_info, strerror(errno));
		}
		cb_data->_internal_memstream = 1;
	}

	if (cb_data->max_download_bytes > 0 &&
		cb_data->stream_bytes_downloaded + realsize >
		cb_data->max_download_bytes) {
		SCOPE_EXIT_LOG_RTN_VALUE(CURL_WRITEFUNC_ERROR, LOG_WARNING,
			"'%s': Xfer failed. "
			"Exceeded maximum %zu bytes transferred\n", debug_info,
			cb_data->max_download_bytes);
	}

	bytes_written = fwrite(data, 1, realsize, cb_data->output);
	cb_data->bytes_downloaded += bytes_written;
	if (bytes_written != realsize) {
		SCOPE_EXIT_LOG_RTN_VALUE(CURL_WRITEFUNC_ERROR, LOG_WARNING,
			"'%s': Xfer failed. "
			"Expected to write %zu bytes but wrote %zu\n",
			debug_info, realsize, bytes_written);
	}

	SCOPE_EXIT_RTN_VALUE(realsize, "Wrote %zu bytes\n", bytes_written);
}

curl_socket_t ast_curl_open_socket_default_cb(void *client_data,
	curlsocktype purpose, struct curl_sockaddr *address)
{
	struct ast_curl_open_socket_data *cb_data = client_data;
	char *debug_info = S_OR(cb_data->debug_info, "");
	SCOPE_ENTER(3, "'%s': Opening socket\n", debug_info);

	if (!ast_acl_list_is_empty((struct ast_acl_list *)cb_data->acl)) {
		struct ast_sockaddr ast_address = { {0,} };

		ast_sockaddr_copy_sockaddr(&ast_address, &address->addr, address->addrlen);

		if (ast_apply_acl((struct ast_acl_list *)cb_data->acl, &ast_address, NULL) != AST_SENSE_ALLOW) {
			SCOPE_EXIT_LOG_RTN_VALUE(CURL_SOCKET_BAD, LOG_WARNING,
				"'%s': Unable to apply acl\n", debug_info);
		}
	}

	cb_data->sockfd = socket(address->family, address->socktype, address->protocol);
	if (cb_data->sockfd < 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(CURL_SOCKET_BAD, LOG_WARNING,
			"'%s': Failed to open socket: %s\n", debug_info, strerror(errno));
	}

	SCOPE_EXIT_RTN_VALUE(cb_data->sockfd, "Success");
}

long ast_curler(char *url, int request_timeout,
	curl_write_callback write_cb, void *write_data,
	curl_write_callback header_cb, void *header_data,
	struct ast_curl_optional_data *optional_data)
{
	RAII_VAR(CURL *, curl, NULL, curl_easy_cleanup);
	long http_code = 0;
	struct ast_curl_write_data *default_write_data = NULL;
	CURLcode rc;

	SCOPE_ENTER(1, "'%s': Retrieving\n", url);

	if (ast_strlen_zero(url)) {
		SCOPE_EXIT_LOG_RTN_VALUE(500, LOG_ERROR, "'missing': url is missing\n");
	}

	if (!write_cb || !write_data) {
		SCOPE_EXIT_LOG_RTN_VALUE(500, LOG_ERROR, "'%s': Either wite_cb and write_data are missing\n", url);
	}
	if (write_cb == ast_curl_write_default_cb) {
		default_write_data = write_data;
	}

	curl = curl_easy_init();
	if (!curl) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "'%s': Failed to set up CURL instance\n", url);
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (request_timeout) {
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, request_timeout);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_data);

	if (header_cb && header_data) {
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, header_data);
	}

	if (optional_data && !ast_strlen_zero(optional_data->user_agent)) {
		curl_easy_setopt(curl, CURLOPT_USERAGENT, optional_data->user_agent);
	}
	if (optional_data && optional_data->curl_open_socket_cb && optional_data->curl_open_socket_data) {
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, optional_data->curl_open_socket_cb);
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, optional_data->curl_open_socket_data);
	}

	if (optional_data && optional_data->per_write_buffer_size) {
		curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, optional_data->per_write_buffer_size);
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

	rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		char *err = ast_strdupa(curl_easy_strerror(rc));
		curl_easy_cleanup(curl);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "'%s': %s\n", url, err);
	}
	if (default_write_data) {
		fflush(default_write_data->output);
		if (default_write_data->_internal_memstream) {
			fclose(default_write_data->output);
		}
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);
	curl = NULL;

	SCOPE_EXIT_RTN_VALUE(http_code, "'%s': Done: %ld\n", url, http_code);
}

long ast_url_download_to_memory(char *url, size_t *returned_length,
	char **returned_data, struct ast_variable **headers)
{
	struct ast_curl_write_data data = {
		.debug_info = url,
	};
	struct ast_curl_header_data hdata = {
		.debug_info = url,
	};

	long rc = ast_curler(url, 0, ast_curl_write_default_cb, &data,
		headers ? ast_curl_header_default_cb : NULL, &hdata, NULL);
	*returned_length = data.stream_bytes_downloaded;
	*returned_data = data.stream_buffer;
	*headers = hdata.headers;

	return rc;
}

static size_t my_write_cb(char *data, size_t size,
	size_t nmemb, void *client_data)
{
	FILE *fp = (FILE *)client_data;
	return fwrite(data, size, nmemb, fp);
}

long ast_url_download_to_file(char *url, char *filename)
{
	FILE *fp = NULL;
	long rc = 0;

	if (ast_strlen_zero(url) || ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR,"url or filename was NULL\n");
		return -1;
	}
	fp = fopen(filename, "w");
	if (!fp) {
		ast_log(LOG_ERROR,"Unable to open file '%s': %s\n", filename,
			strerror(errno));
		return -1;
	}
	rc = ast_curler(url, 0, my_write_cb, fp, NULL, NULL, NULL);
	fclose(fp);
	return rc;
}

static int unload_module(void)
{
	curl_global_cleanup();

	return 0;
}

static int load_module(void)
{
	if (curl_global_init(CURL_GLOBAL_ALL)) {
		ast_log(LOG_ERROR, "Unable to initialize the cURL library. Cannot load res_curl.so\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "cURL Resource Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DEPEND,
);
