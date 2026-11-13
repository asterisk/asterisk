/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Ben Kolodny
 *
 * Ben Kolodny <3232963@gmail.com>
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
 * \brief Opt-in posix_spawn() for ast_safe_system()
 *
 * fork() copies the page tables of the whole Asterisk process while holding
 * its mm lock, which on large processes delays other threads (timing
 * included) for tens of milliseconds.  With the asterisk.conf option
 * safe_system_spawn, ast_safe_system() starts its child with posix_spawn()
 * instead (glibc >= 2.34).
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <sched.h>
#include <signal.h>
#include <unistd.h>

#include "asterisk/_private.h"
#include "asterisk/app.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"

#if defined(__linux__) && defined(__GLIBC__) && \
	(__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
/*
 * glibc's posix_spawn() has been clone(CLONE_VM|CLONE_VFORK) based since 2.24,
 * so it does not copy our page tables.  2.34 is required for
 * posix_spawn_file_actions_addclosefrom_np(), needed to close inherited fds
 * the way safe_exec_prep() does.
 */
#define AST_SAFE_SYSTEM_SPAWN 1
#include <spawn.h>
#ifdef HAVE_CAP
#include <sys/capability.h>
#include <sys/prctl.h>
#include <linux/securebits.h>
#endif
#endif

#ifdef AST_SAFE_SYSTEM_SPAWN

extern char **environ;

/*! \brief Set if capabilities could not be restored after a spawn: fork() is used from then on */
static int spawn_disabled;

/*! \brief What safe_system_caps_clear() changed, for safe_system_caps_restore() */
struct safe_system_caps {
#ifdef HAVE_CAP
	/*! The thread's capabilities before the change, NULL if nothing was changed */
	cap_t saved;
	/*! The thread's ambient capabilities before the change, one bit each */
	uint64_t ambient;
#else
	char unused;
#endif
};

#ifdef HAVE_CAP
/*! \brief Capabilities are numbered 0-63 in the kernel ABI and are kept one bit each */
#define AST_SAFE_SYSTEM_MAX_CAPS 64

/*!
 * \internal
 * \brief Clear the calling thread's inheritable (and so ambient) capabilities.
 *
 * The fork() path drops every capability in the child before execve()
 * (child_cap is the empty set).  posix_spawn() cannot do that in the child.
 * Of the capability sets only the inheritable and ambient ones carry into
 * the executed program, and they are per thread on Linux: cap_set_proc()
 * changes the calling thread only.  (This relies on Asterisk not being
 * linked with libpsx, which makes libcap apply changes to all threads.)
 * So the spawning thread clears its own inheritable set for the duration
 * of posix_spawn(), which gives the child the same capabilities after
 * execve() as the fork() path and touches no other thread.  Clearing the
 * inheritable set also clears the ambient set.
 *
 * Asterisk itself makes CAP_NET_ADMIN inheritable when it changes to a run
 * user; a service manager may set others, and ambient capabilities.
 *
 * Only done when safe_system_caps_restore() is expected to be able to put
 * everything back: each inheritable capability must also be permitted and
 * in the bounding set, and ambient capabilities must be raisable.
 *
 * \retval 0 done, or nothing to do
 * \retval -1 nothing was changed: use the fork() path
 */
static int safe_system_caps_clear(struct safe_system_caps *caps)
{
	cap_t cur;
	cap_t tmp;
	cap_t checked = NULL;
	cap_value_t cap;
	int res = -1;

	caps->saved = NULL;
	caps->ambient = 0;

	cur = cap_get_proc();
	tmp = cur ? cap_dup(cur) : NULL;
	if (!tmp || cap_clear_flag(tmp, CAP_INHERITABLE)) {
		goto done;
	}
	if (!cap_compare(cur, tmp)) {
		/* Nothing inheritable, so nothing ambient either */
		res = 0;
		goto done;
	}

	/* The capabilities without the inheritable ones, which are added back as they are checked */
	checked = cap_dup(tmp);
	if (!checked) {
		goto done;
	}
	for (cap = 0; cap < AST_SAFE_SYSTEM_MAX_CAPS; cap++) {
		cap_flag_value_t inheritable;
		cap_flag_value_t permitted;

		if (cap_get_flag(cur, cap, CAP_INHERITABLE, &inheritable)) {
			/* Past the last capability libcap knows */
			break;
		}
		if (inheritable != CAP_SET) {
			continue;
		}
		if (cap_get_flag(cur, cap, CAP_PERMITTED, &permitted) || permitted != CAP_SET
			|| cap_get_bound(cap) != 1) {
			/* Could not be made inheritable again */
			goto done;
		}
		if (cap_set_flag(checked, CAP_INHERITABLE, 1, &cap, CAP_SET)) {
			goto done;
		}
		switch (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0)) {
		case 0:
			break;
		case 1:
			caps->ambient |= 1ULL << cap;
			break;
		default:
			/* Not known whether it is ambient, so it could not be put back */
			goto done;
		}
	}
	if (cap_compare(cur, checked)) {
		/* An inheritable capability that could not be checked */
		goto done;
	}
	if (caps->ambient && (prctl(PR_GET_SECUREBITS, 0, 0, 0, 0) & SECBIT_NO_CAP_AMBIENT_RAISE)) {
		/* Could not be made ambient again */
		goto done;
	}

	if (cap_set_proc(tmp)) {
		goto done;
	}
	caps->saved = cur;
	cur = NULL;
	res = 0;

done:
	if (checked) {
		cap_free(checked);
	}
	if (tmp) {
		cap_free(tmp);
	}
	if (cur) {
		cap_free(cur);
	}
	return res;
}

/*!
 * \internal
 * \brief Undo safe_system_caps_clear() on the calling thread
 *
 * \retval 0 restored, or nothing to restore
 * \retval -1 failed; not expected, safe_system_caps_clear() checks first
 */
static int safe_system_caps_restore(struct safe_system_caps *caps)
{
	cap_value_t cap;
	int res = 0;

	if (!caps->saved) {
		return 0;
	}

	if (cap_set_proc(caps->saved)) {
		ast_log(LOG_ERROR, "Unable to restore capabilities after spawn: %s\n", strerror(errno));
		res = -1;
	}
	for (cap = 0; !res && cap < AST_SAFE_SYSTEM_MAX_CAPS; cap++) {
		if ((caps->ambient & (1ULL << cap)) && prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0)) {
			ast_log(LOG_ERROR, "Unable to restore ambient capability %d after spawn: %s\n", cap, strerror(errno));
			res = -1;
		}
	}
	cap_free(caps->saved);

	return res;
}
#else
static int safe_system_caps_clear(struct safe_system_caps *caps)
{
	return 0;
}

static int safe_system_caps_restore(struct safe_system_caps *caps)
{
	return 0;
}
#endif

/*!
 * \internal
 * \brief Start "/bin/sh -c s" with posix_spawn() instead of fork().
 *
 * The child is set up the same way safe_exec_prep() sets up a forked one:
 * descriptors above stderr are closed, with high priority (-p) the
 * scheduling policy is reset, and it has the same capabilities after
 * execve() (see safe_system_caps_clear()).  Signal dispositions and the
 * signal mask are inherited exactly as across fork() and execve().
 *
 * As in ast_safe_fork(), all signals are blocked on this thread around the
 * spawn, so no signal handler runs while its capabilities are changed.
 *
 * \return pid of the child, after ast_replace_sigchld() (the caller waits)
 * \retval -1 spawn not possible, the caller uses the fork() path
 */
static pid_t safe_system_spawn(const char *s)
{
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attr;
	struct safe_system_caps caps;
	char *argv[] = { "/bin/sh", "-c", (char *) s, NULL };
	sigset_t all;
	sigset_t old_mask;
	short flags = POSIX_SPAWN_SETSIGMASK;
	pid_t pid = -1;
	int res;
	int sigmask_res;

	if (ast_atomic_fetch_add(&spawn_disabled, 0, __ATOMIC_RELAXED)) {
		return -1;
	}

	if (posix_spawn_file_actions_init(&actions)) {
		return -1;
	}
	if (posix_spawnattr_init(&attr)) {
		posix_spawn_file_actions_destroy(&actions);
		return -1;
	}

	sigfillset(&all);
	if (pthread_sigmask(SIG_BLOCK, &all, &old_mask)) {
		posix_spawnattr_destroy(&attr);
		posix_spawn_file_actions_destroy(&actions);
		return -1;
	}

	res = posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
	if (!res) {
		/* The child gets this thread's original signal mask, as with fork() */
		res = posix_spawnattr_setsigmask(&attr, &old_mask);
	}
	if (!res && ast_opt_high_priority) {
		struct sched_param sched = { .sched_priority = 0, };

		res = posix_spawnattr_setschedpolicy(&attr, SCHED_OTHER);
		if (!res) {
			res = posix_spawnattr_setschedparam(&attr, &sched);
		}
		flags |= POSIX_SPAWN_SETSCHEDULER;
	}
	if (!res) {
		res = posix_spawnattr_setflags(&attr, flags);
	}
	if (!res && safe_system_caps_clear(&caps)) {
		res = EPERM;
	}

	if (!res) {
		ast_replace_sigchld();
		res = posix_spawn(&pid, "/bin/sh", &actions, &attr, argv, environ);
		if (safe_system_caps_restore(&caps)) {
			/* The child, if any, is already running: this is not a spawn failure */
			ast_atomic_fetch_or(&spawn_disabled, 1, __ATOMIC_RELAXED);
			ast_log(LOG_ERROR, "Using fork() for ast_safe_system() from now on\n");
		}
		if (res) {
			ast_unreplace_sigchld();
			pid = -1;
		}
	}
	sigmask_res = pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
	if (sigmask_res) {
		ast_log(LOG_ERROR, "Unable to restore the signal mask after spawn: %s\n", strerror(sigmask_res));
	}

	if (res) {
		ast_debug(1, "posix_spawn() not usable (%s), using fork()\n", strerror(res));
	}

	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&actions);

	return pid;
}

#endif /* AST_SAFE_SYSTEM_SPAWN */

const char *ast_safe_system_spawn_status(void)
{
	if (!ast_opt_safe_system_spawn) {
		return "Disabled";
	}
#ifdef AST_SAFE_SYSTEM_SPAWN
	if (ast_atomic_fetch_add(&spawn_disabled, 0, __ATOMIC_RELAXED)) {
		return "Enabled, but fork() forced after a capability restore failure";
	}
	return "Enabled (fork() still used when a spawn cannot be set up)";
#else
	return "Enabled, but not supported here (fork() used)";
#endif
}

pid_t ast_safe_system_spawn(const char *s)
{
	if (!ast_opt_safe_system_spawn) {
		return -1;
	}
#ifdef AST_SAFE_SYSTEM_SPAWN
	return safe_system_spawn(s);
#else
	return -1;
#endif
}
