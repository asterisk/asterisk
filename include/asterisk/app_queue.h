/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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

/*! \brief Queue call */
struct ast_queue_caller_info {
	struct ast_channel *chan;
	const char *context;
	const char *queue_name;
	const char *digits;
	int prio;
	int pending;
	int pos;
	int start;
	int expire;
};

/*! \brief Queue agent */
struct ast_queue_agent_info {
	const char *interface;
	const char *state_interface;
	const char *member_name;
	int queuepos;
	int penalty;
	int calls;
	int status;
	unsigned int paused:1;
	unsigned int dynamic:1;
	unsigned int available:1;
};

struct ast_queue_strategy_callbacks {
	/*!
	 * \brief Callback when a call enters a queue
	 * \param caller
	 */
	void (*enter_queue)(struct ast_queue_caller_info *caller);
	/*!
	 * \brief Callback to check if a call can be handled, call once per second
	 * \param caller
	 * \retval 2 call has expired, remove call from queue
	 * \retval 1 call can be handled
	 * \retval 0 not our turn yet
	 * \retval -1 Use default app_queue algorithm
	 */
	int (*is_our_turn)(struct ast_queue_caller_info *caller);
	/*!
	 * \brief Callback to calculate agent metric
	 * \param caller
	 * \param agent
	 * \return positive metric to use
	 * \retval 0 ignore agent for now
	 * \retval -1 Use default app_queue algorithm
	 */
	int (*calc_metric)(struct ast_queue_caller_info *caller, struct ast_queue_agent_info *agent);
};

int __ast_queue_register_external_strategy_provider(struct ast_queue_strategy_callbacks *callbacks, const char *name, void *mod);

/*!
 * \brief Register external queue strategy provider
 * \param callbacks
 * \retval 0 on success, -1 on failure (provider already registered)
 */
#define ast_queue_register_external_strategy_provider(callbacks, name) __ast_queue_register_external_strategy_provider(callbacks, name, AST_MODULE_SELF)

/*!
 * \brief Unregister external queue strategy provider
 * \param callbacks
 * \retval 0 on success, -1 on failure (provider not currently registered)
 */
int ast_queue_unregister_external_strategy_provider(struct ast_queue_strategy_callbacks *callbacks);
