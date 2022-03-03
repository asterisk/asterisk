/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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
 * \brief Universally unique identifier support
 *
 * \extref Depends on libuuid, a component of the e2fsprogs package - http://e2fsprogs.sourceforge.net/
 */

#include "asterisk.h"
#include <uuid/uuid.h>
#include <fcntl.h>

#include "asterisk/uuid.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"

AST_MUTEX_DEFINE_STATIC(uuid_lock);

static int has_dev_urandom;

struct ast_uuid {
	uuid_t uu;
};

/*!
 * \internal
 * \brief Generate a UUID.
 * \since 12.0.0
 *
 * \param uuid Fill this with a generated UUID.
 */
static void generate_uuid(struct ast_uuid *uuid)
{
	/* libuuid provides three methods of generating uuids,
	 * uuid_generate(), uuid_generate_random(), and uuid_generate_time().
	 *
	 * uuid_generate_random() creates a UUID based on random numbers. The method
	 * attempts to use either /dev/urandom or /dev/random to generate random values.
	 * If these resources are unavailable, then random numbers will be generated
	 * using C library calls to generate pseudorandom numbers.
	 * This method of generating UUIDs corresponds to section 4.4 of RFC 4122.
	 *
	 * uuid_generate_time() creates a UUID based on the current time plus
	 * a system identifier (MAC address of the ethernet interface). This
	 * method of generating UUIDs corresponds to section 4.2 of RFC 4122.
	 *
	 * uuid_generate() will check if /dev/urandom or /dev/random is available to
	 * use. If so, it will use uuid_generate_random(). Otherwise, it will use
	 * uuid_generate_time(). The idea is that it avoids using pseudorandom
	 * numbers if necessary.
	 *
	 * For our purposes, we do not use the time-based UUID at all. There are
	 * several reasons for this:
	 *
	 * 1) The time-based algorithm makes use of a daemon process (uuidd) in order
	 * to ensure that any concurrent requests for UUIDs result in unique results.
	 * Use of this daemon is a bit dodgy for a few reasons
	 *
	 *     a) libuuid assumes a hardcoded location for the .pid file of the daemon.
	 *     However, the daemon could already be running on the system in a different
	 *     location than expected. If this is the case, then attempting to connect
	 *     to the daemon will fail, and attempting to launch another instance in
	 *     the expected location will also fail.
	 *
	 *     b) If the daemon is not running, then the first attempt to create a
	 *     time-based UUID will result in launching the daemon. Because of the hard-
	 *     coded locations that libuuid assumes for the daemon, Asterisk must be
	 *     run with permissions that will allow for the daemon to be launched in
	 *     the expected directories.
	 *
	 *     c) Once the daemon is running, concurrent requests for UUIDs are thread-safe.
	 *     However, the actual launching of the daemon is not thread-safe since libuuid
	 *     uses no synchronization primitives to ensure that only one thread (or process)
	 *     launches the daemon.
	 *
	 *     d) When libuuid launches the daemon, it sets an inactivity timer.
	 *     If no UUID generation requests are issued in that time period,
	 *     then the daemon will exit. If a new request should occur after the daemon
	 *     exits, then the daemon will be relaunched. Given point c), we cannot
	 *     necessarily guarantee the thread-safety of time-based UUID generation since
	 *     we cannot necessarily guarantee the daemon is running as we expect.
	 *     We could set up a watchdog thread to generate UUIDs at regular intervals to
	 *     prevent the daemon from exiting, but frankly, that sucks.
	 *
	 * 2) Since the MAC address of the Ethernet interface is part of the UUID when
	 * using the time-based method, there is information leaked.
	 *
	 * Given these drawbacks, we stick to only using random UUIDs. The chance of /dev/random
	 * or /dev/urandom not existing on systems in this age is next to none.
	 */

	/* XXX Currently, we only protect this call if the user has no /dev/urandom on their system.
	 * If it turns out that there are issues with UUID generation despite the presence of
	 * /dev/urandom, then we may need to make the locking/unlocking unconditional.
	 */
	if (!has_dev_urandom) {
		ast_mutex_lock(&uuid_lock);
	}
	uuid_generate_random(uuid->uu);
	if (!has_dev_urandom) {
		ast_mutex_unlock(&uuid_lock);
	}
}

struct ast_uuid *ast_uuid_generate(void)
{
	struct ast_uuid *uuid = ast_malloc(sizeof(*uuid));

	if (!uuid) {
		return NULL;
	}
	generate_uuid(uuid);
	return uuid;
}

char *ast_uuid_to_str(struct ast_uuid *uuid, char *buf, size_t size)
{
	ast_assert(size >= AST_UUID_STR_LEN);
	uuid_unparse(uuid->uu, buf);
	return ast_str_to_lower(buf);
}

char *ast_uuid_generate_str(char *buf, size_t size)
{
	struct ast_uuid uuid;

	generate_uuid(&uuid);
	return ast_uuid_to_str(&uuid, buf, size);
}

struct ast_uuid *ast_str_to_uuid(char *str)
{
	struct ast_uuid *uuid = ast_malloc(sizeof(*uuid));
	int res;

	if (!uuid) {
		return NULL;
	}
	res = uuid_parse(str, uuid->uu);
	if (res) {
		ast_log(LOG_WARNING, "Unable to convert string %s into a UUID\n", str);
		ast_free(uuid);
		return NULL;
	}
	return uuid;
}

struct ast_uuid *ast_uuid_copy(struct ast_uuid *src)
{
	struct ast_uuid *dst = ast_malloc(sizeof(*dst));

	if (!dst) {
		return NULL;
	}
	uuid_copy(dst->uu, src->uu);
	return dst;
}

int ast_uuid_compare(struct ast_uuid *left, struct ast_uuid *right)
{
	return uuid_compare(left->uu, right->uu);
}

void ast_uuid_clear(struct ast_uuid *uuid)
{
	uuid_clear(uuid->uu);
}

int ast_uuid_is_nil(struct ast_uuid *uuid)
{
	return uuid_is_null(uuid->uu);
}

void ast_uuid_init(void)
{
	/* This requires some explanation.
	 *
	 * libuuid generates UUIDs based on random number generation. This involves
	 * opening a handle to /dev/urandom or /dev/random in order to get random
	 * data for the UUIDs.
	 *
	 * This is thread-safe, to a point. The problem is that the first attempt
	 * to generate a UUID will result in opening the random number handle. Once
	 * the handle is opened, all further generation is thread safe. This
	 * first generation can be potentially risky if multiple threads attempt
	 * to generate a UUID at the same time, though, since there is no thread
	 * synchronization used within libuuid. To get around this potential
	 * issue, we go ahead and generate a UUID up front so that the underlying
	 * work is done before we start requesting UUIDs for real.
	 *
	 * Think of this along the same lines as initializing a singleton.
	 */
	uuid_t uu;
	int dev_urandom_fd;

	dev_urandom_fd = open("/dev/urandom", O_RDONLY);
	if (dev_urandom_fd < 0) {
		ast_log(LOG_WARNING, "It appears your system does not have /dev/urandom on it. This\n"
				"means that UUID generation will use a pseudorandom number generator. Since\n"
				"the thread-safety of your system's random number generator cannot\n"
				"be guaranteed, we have to synchronize UUID generation. This may result\n"
				"in decreased performance. It is highly recommended that you set up your\n"
				"system to have /dev/urandom\n");
	} else {
		has_dev_urandom = 1;
		close(dev_urandom_fd);
	}
	uuid_generate_random(uu);

	ast_debug(1, "UUID system initiated\n");
}
