/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief loadable MixMonitor functionality
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

/*!
 * \brief Start a mixmonitor on a channel.
 * \since 12.0.0
 *
 * \param chan Which channel to put the MixMonitor on
 * \param filename What the name of the file should be
 * \param options What options should be used for the mixmonitor
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
typedef int (*ast_mixmonitor_start_fn)(struct ast_channel *chan, const char *filename, const char *options);

/*!
 * \brief Stop a mixmonitor on a channel.
 * \since 12.0.0
 *
 * \param chan Which channel to stop a MixMonitor on
 * \param mixmon_id Stop the MixMonitor with this mixmonid if it is on the channel (may be NULL)
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
typedef int (*ast_mixmonitor_stop_fn)(struct ast_channel *chan, const char *mixmon_id);

/*!
 * \brief MixMonitor virtual methods table definition
 * \since 12.0.0
 */
struct ast_mixmonitor_methods {
	ast_mixmonitor_start_fn start;
	ast_mixmonitor_stop_fn stop;
};

/*!
 * \brief Setup MixMonitor virtual methods table. Use this to provide the MixMonitor functionality from a loadable module.
 * \since 12.0.0
 *
 * \param vmethod_table pointer to vmethod table providing mixmonitor functions
 *
 * \retval 0 if successful
 * \retval non-zero on failure
 */
int ast_set_mixmonitor_methods(struct ast_mixmonitor_methods *vmethod_table);

/*!
 * \brief Clear the MixMonitor virtual methods table. Use this to cleanup function pointers provided by a module that set.
 * \since 12.0.0
 */
void ast_clear_mixmonitor_methods(void);

/*!
 * \brief Start a mixmonitor on a channel with the given parameters
 * \since 12.0.0
 *
 * \param chan Which channel to apply the MixMonitor to
 * \param filename filename to use for the recording
 * \param options Optional arguments to be interpreted by the MixMonitor start function
 *
 * \retval 0 if successful
 * \retval non-zero on failure
 *
 * \note This function will always fail is nothing has set the mixmonitor methods
 */
int ast_start_mixmonitor(struct ast_channel *chan, const char *filename, const char *options);

/*!
 * \brief Stop a mixmonitor on a channel with the given parameters
 * \since 12.0.0
 *
 * \param chan Which channel to stop a MixMonitor on (may be NULL if mixmon_id is provided)
 * \param mixmon_id Which mixmon_id should be stopped (may be NULL if chan is provided)
 *
 * \retval 0 if successful
 * \retval non-zero on failure
 */
int ast_stop_mixmonitor(struct ast_channel *chan, const char *mixmon_id);
