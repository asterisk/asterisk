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

#include <curl/curl.h>

#include "asterisk.h"
#include "asterisk/config.h"

#include "curl_utils.h"

void curl_header_data_free(void *obj)
{
	struct curl_header_data *cb_data = obj;
	if (!cb_data) {
		return;
	}
	ast_variables_destroy(cb_data->headers);
	if (cb_data->debug_info) {
		ast_free(cb_data->debug_info);
	}
	ast_free(cb_data);
}

size_t curl_header_cb(char *data, size_t size,
	size_t nitems, void *client_data)
{
	struct curl_header_data *cb_data = client_data;
	size_t realsize = size * nitems;
	size_t adjusted_size = realsize;
	char *debug_info = S_OR(cb_data->debug_info, "");
	char *start = data;
	char *colon = NULL;
	struct ast_variable *h;
	char *header;
	char *value;
	SCOPE_ENTER(5, "'%s': Header received with %zu bytes\n",
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
		SCOPE_EXIT_RTN_VALUE(realsize, "No colon in the header.  Weird\n");
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

void curl_write_data_free(void *obj)
{
	struct curl_write_data *cb_data = obj;
	if (!cb_data) {
		return;
	}
	if (cb_data->output) {
		fclose(cb_data->output);
	}
	if (cb_data->debug_info) {
		ast_free(cb_data->debug_info);
	}
	ast_std_free(cb_data->stream_buffer);
	ast_free(cb_data);
}

size_t curl_write_cb(char *data, size_t size,
	size_t nmemb, void *client_data)
{
	struct curl_write_data *cb_data = client_data;
	size_t realsize = size * nmemb;
	size_t bytes_written = 0;
	char *debug_info = S_OR(cb_data->debug_info, "");
	SCOPE_ENTER(5, "'%s': Writing data chunk of %zu bytes\n",
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

void curl_open_socket_data_free(void *obj)
{
	struct curl_open_socket_data *cb_data = obj;
	if (!cb_data) {
		return;
	}
	if (cb_data->debug_info) {
		ast_free(cb_data->debug_info);
	}
	ast_free(cb_data);
}

curl_socket_t curl_open_socket_cb(void *client_data,
	curlsocktype purpose, struct curl_sockaddr *address)
{
	struct curl_open_socket_data *cb_data = client_data;
	char *debug_info = S_OR(cb_data->debug_info, "");
	SCOPE_ENTER(5, "'%s': Opening socket\n", debug_info);

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

long curler(const char *url, int request_timeout,
	struct curl_write_data *write_data,
	struct curl_header_data *header_data,
	struct curl_open_socket_data *open_socket_data)
{
	RAII_VAR(CURL *, curl, NULL, curl_easy_cleanup);
	long http_code = 0;
	CURLcode rc;

	SCOPE_ENTER(1, "'%s': Retrieving\n", url);

	if (ast_strlen_zero(url)) {
		SCOPE_EXIT_LOG_RTN_VALUE(500, LOG_ERROR, "'missing': url is missing\n");
	}

	if (!write_data) {
		SCOPE_EXIT_LOG_RTN_VALUE(500, LOG_ERROR, "'%s': Either wite_cb and write_data are missing\n", url);
	}

	curl = curl_easy_init();
	if (!curl) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "'%s': Failed to set up CURL instance\n", url);
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (request_timeout) {
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, request_timeout);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_data);

	if (header_data) {
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, header_data);
	}

	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);

	if (open_socket_data) {
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, curl_open_socket_cb);
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, open_socket_data);
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	/*
	 * ATIS-1000074 specifically says to NOT follow redirections.
	 */
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0);

	rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		char *err = ast_strdupa(curl_easy_strerror(rc));
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "'%s': %s\n", url, err);
	}

	fflush(write_data->output);
	if (write_data->_internal_memstream) {
		fclose(write_data->output);
		write_data->output = NULL;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);
	curl = NULL;

	SCOPE_EXIT_RTN_VALUE(http_code, "'%s': Done: %ld\n", url, http_code);
}

long curl_download_to_memory(const char *url, size_t *returned_length,
	char **returned_data, struct ast_variable **headers)
{
	struct curl_write_data data = {
		.debug_info = ast_strdupa(url),
	};
	struct curl_header_data hdata = {
		.debug_info = ast_strdupa(url),
	};

	long rc = curler(url, 0, &data, headers ? &hdata : NULL, NULL);

	*returned_length = data.stream_bytes_downloaded;
	*returned_data = data.stream_buffer;
	if (headers) {
		*headers = hdata.headers;
	}

	return rc;
}

long curl_download_to_file(const char *url, char *filename)
{
	FILE *fp = NULL;
	long rc = 0;
	struct curl_write_data data = {
		.debug_info = ast_strdup(url),
	};

	if (ast_strlen_zero(url) || ast_strlen_zero(filename)) {
		ast_log(LOG_ERROR,"url or filename was NULL\n");
		return -1;
	}
	data.output = fopen(filename, "w");
	if (!fp) {
		ast_log(LOG_ERROR,"Unable to open file '%s': %s\n", filename,
			strerror(errno));
		return -1;
	}
	rc = curler(url, 0, &data,  NULL, NULL);
	fclose(data.output);
	ast_free(data.debug_info);
	return rc;
}

