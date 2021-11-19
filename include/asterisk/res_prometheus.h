/*
 * res_prometheus: Asterisk Prometheus Metrics
 *
 * Copyright (C) 2019 Sangoma, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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

#ifndef RES_PROMETHEUS_H__
#define RES_PROMETHEUS_H__

/*!
 * \file
 *
 * \brief Asterisk Prometheus Metrics
 *
 * This module provides the base APIs and functionality for exposing a
 * metrics route in Asterisk's HTTP server suitable for consumption by
 * a Prometheus server. It does not provide any metrics itself.
 */

#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stringfields.h"

/*!
 * \brief How many labels a single metric can have
 */
#define PROMETHEUS_MAX_LABELS 8

/*!
 * \brief How long a label name can be
 */
#define PROMETHEUS_MAX_NAME_LENGTH 64

/*!
 * \brief How long a label value can be
 */
#define PROMETHEUS_MAX_LABEL_LENGTH 128

/*!
 * \brief How large of a value we can store
 */
#define PROMETHEUS_MAX_VALUE_LENGTH 32

/*!
 * \brief Prometheus general configuration
 *
 * While the config file should generally provide the configuration
 * for this module, it is useful for testing purposes to allow the
 * configuration to be injected into the module. This struct is
 * public to allow this to occur.
 *
 * \note
 * Modifying the configuration outside of testing purposes is not
 * encouraged.
 */
struct prometheus_general_config {
	/*! \brief Whether or not the module is enabled */
	unsigned int enabled;
	/*! \brief Whether or not core metrics are enabled */
	unsigned int core_metrics_enabled;
	AST_DECLARE_STRING_FIELDS(
		/*! \brief The HTTP URI we register ourselves to */
		AST_STRING_FIELD(uri);
		/*! \brief Auth username for Basic Auth */
		AST_STRING_FIELD(auth_username);
		/*! \brief Auth password for Basic Auth */
		AST_STRING_FIELD(auth_password);
		/*! \brief Auth realm */
		AST_STRING_FIELD(auth_realm);
	);
};

/*!
 * \brief A function table for a metrics provider
 *
 * It's generally nice to separate out things that provide metrics
 * from the core of this module. For those that want to be notified
 * when things happen in the core module, they can provide an instance
 * of this function table using \c prometheus_metrics_provider_register
 * and be notified when module affecting changes occur.
 */
struct prometheus_metrics_provider {
	/*!
	 * \brief Handy name of the provider for debugging purposes
	 */
	const char *name;
	/*!
	 * \brief Reload callback
	 *
	 * \param config The reloaded config
	 *
	 * \retval 0 success
	 * \retval -1 error
	 */
	int (* const reload_cb)(struct prometheus_general_config *config);
	/*!
	 * \brief Unload callback.
	 */
	void (* const unload_cb)(void);
};

/*!
 * \brief Prometheus metric type
 *
 * \note
 * Clearly, at some point, we should support summaries and histograms.
 * As an initial implementation, counters / gauges give us quite a
 * bit of functionality.
 */
enum prometheus_metric_type {
	/*!
	 * \brief A metric whose value always goes up
	 */
	PROMETHEUS_METRIC_COUNTER = 0,
	/*!
	 * \brief A metric whose value can bounce around like a jackrabbit
	 */
	PROMETHEUS_METRIC_GAUGE,
};

/*!
 * \brief How the metric was allocated.
 *
 * \note Clearly, you don't want to get this wrong.
 */
enum prometheus_metric_allocation_strategy {
	/*!
	 * \brief The metric was allocated on the stack
	 */
	PROMETHEUS_METRIC_ALLOCD = 0,
	/*!
	 * \brief The metric was allocated on the heap
	 */
	PROMETHEUS_METRIC_MALLOCD,
};

/*!
 * \brief A label that further defines a metric
 */
struct prometheus_label {
	/*!
	 * \brief The name of the label
	 */
	char name[PROMETHEUS_MAX_NAME_LENGTH];
	/*!
	 * \brief The value of the label
	 */
	char value[PROMETHEUS_MAX_LABEL_LENGTH];
};

/*!
 * \brief An actual, honest to god, metric.
 *
 * A bit of effort has gone into making this structure as efficient as we
 * possibly can. Given that a *lot* of metrics can theoretically be dumped out,
 * and that Asterisk attempts to be a "real-time" system, we want this process
 * to be as efficient as possible. Countering that is the ridiculous flexibility
 * that Prometheus allows for (and, to an extent, wants) - namely the notion of
 * families of metrics delineated by their labels.
 *
 * In order to balance this, metrics have arrays of labels. While this makes for
 * a very large struct (such that loading one of these into memory is probably
 * going to blow your cache), you will at least get the whole thing, since
 * you're going to need those labels to figure out what you're looking like.
 *
 * A hierarchy of metrics occurs when all metrics have the same \c name, but
 * different labels.
 *
 * We manage the hierarchy by allowing a metric to maintain their own list of
 * related metrics. When metrics are registered (/c prometheus_metric_register),
 * the function will automatically determine the hierarchy and place them into
 * the appropriate lists. When you are creating metrics on the fly in a callback
 * (\c prometheus_callback_register), you have to manage this hierarchy
 * yourself, and only print out the first metric in a chain.
 *
 * Note that **EVERYTHING** in a metric is immutable once registered, save for
 * its value. Modifying the hierarchy, labels, name, help, whatever is going to
 * result in a "bad time", and is also expressly against Prometheus law. (Don't
 * get your liver eaten.)
 */
struct prometheus_metric {
	/*!
	 * \brief What type of metric we are
	 */
	enum prometheus_metric_type type;
	/*!
	 * \brief How this metric was allocated
	 */
	enum prometheus_metric_allocation_strategy allocation_strategy;
	/*!
	 * \brief A lock protecting the metric \c value
	 *
	 * \note The metric must be locked prior to updating its value!
	 */
	ast_mutex_t lock;
	/*!
	 * \brief Pointer to a static string defining this metric's help text.
	 */
	const char *help;
	/*!
	 * \brief Our metric name
	 */
	char name[PROMETHEUS_MAX_NAME_LENGTH];
	/*!
	 * \brief The metric's labels
	 */
	struct prometheus_label labels[PROMETHEUS_MAX_LABELS];
	/*!
	 * \brief The current value.
	 *
	 * If \c get_metric_value is set, this value is ignored until the callback
	 * happens
	 */
	char value[PROMETHEUS_MAX_VALUE_LENGTH];
	/*!
	 * \brief Callback function to obtain the metric value
	 *
	 * If updates need to happen when the metric is gathered, provide the
	 * callback function. Otherwise, leave it \c NULL.
	 */
	void (* get_metric_value)(struct prometheus_metric *metric);
	/*!
	 * \brief A list of children metrics
	 *
	 * Children metrics have the same name but different label.
	 *
	 * Registration of a metric will automatically nest the metrics; otherwise
	 * they are treated independently.
	 *
	 * The help of the first metric in a chain of related metrics is the only
	 * one that will be printed.
	 *
	 * For metrics output during a callback, the handler is responsible for
	 * managing the children. For metrics that are registered, the registration
	 * automatically nests the metrics.
	 */
	AST_LIST_HEAD_NOLOCK(, prometheus_metric) children;
	AST_LIST_ENTRY(prometheus_metric) entry;
};

/*!
 * \brief Convenience macro for initializing a metric on the stack
 *
 * When initializing a metric on the stack, various fields have to be provided
 * to initialize the metric correctly. This macro can be used to simplify the
 * process.
 *
 * Example Usage:
 * \code
 *	struct prometheus_metric test_counter_one =
 *		PROMETHEUS_METRIC_STATIC_INITIALIZATION(
 *			PROMETHEUS_METRIC_COUNTER,
 *			"test_counter_one",
 *			"A test counter",
 *			NULL);
 *	struct prometheus_metric test_counter_two =
 * 		PROMETHEUS_METRIC_STATIC_INITIALIZATION(
 *			PROMETHEUS_METRIC_COUNTER,
 *			"test_counter_two",
 *			"A test counter",
 *			metric_values_get_counter_value_cb);
 * \endcode
 *
 * \param mtype The metric type. See \c prometheus_metric_type
 * \param n Name of the metric
 * \param h Help text for the metric
 * \param cb Callback function. Optional; may be \c NULL
 */
#define PROMETHEUS_METRIC_STATIC_INITIALIZATION(mtype, n, h, cb) { \
	.type = (mtype), \
	.allocation_strategy = PROMETHEUS_METRIC_ALLOCD, \
	.lock = AST_MUTEX_INIT_VALUE, \
	.name = (n), \
	.help = (h), \
	.children = AST_LIST_HEAD_NOLOCK_INIT_VALUE, \
	.get_metric_value = (cb), \
}

/*!
 * \brief Convenience macro for setting a label / value in a metric
 *
 * When creating nested metrics, it's helpful to set their label after they have
 * been declared but before they have been registered. This macro acts as a
 * convenience function to set the labels properly on a declared metric.
 *
 * \note Setting labels *after* registration will lead to a "bad time"
 *
 * Example Usage:
 * \code
 *	PROMETHEUS_METRIC_SET_LABEL(
 *		test_gauge_child_two, 0, "key_one", "value_two");
 *	PROMETHEUS_METRIC_SET_LABEL(
 *		test_gauge_child_two, 1, "key_two", "value_two");
 * \endcode
 *
 * \param metric The metric to set the label on
 * \param label Position of the label to set
 * \param n Name of the label
 * \param v Value of the label
 */
#define PROMETHEUS_METRIC_SET_LABEL(metric, label, n, v) do { \
	ast_assert((label) < PROMETHEUS_MAX_LABELS); \
	ast_copy_string((metric)->labels[(label)].name, (n), sizeof((metric)->labels[(label)].name)); \
	ast_copy_string((metric)->labels[(label)].value, (v), sizeof((metric)->labels[(label)].value)); \
} while (0)

/*!
 * \brief Destroy a metric and all its children
 *
 * \note If you still want the children, make sure you remove the head of the
 * \c children list first.
 *
 * \param metric The metric to destroy
 */
void prometheus_metric_free(struct prometheus_metric *metric);

/*!
 * \brief Create a malloc'd counter metric
 *
 * \note The metric must be registered after creation
 *
 * \param name The name of the metric
 * \param help Help text for the metric
 *
 * \retval prometheus_metric on success
 * \retval NULL on error
 */
struct prometheus_metric *prometheus_counter_create(const char *name,
	const char *help);

/*!
 * \brief Create a malloc'd gauge metric
 *
 * \note The metric must be registered after creation
 *
 * \param name The name of the metric
 * \param help Help text for the metric
 *
 * \retval prometheus_metric on success
 * \retval NULL on error
 */
struct prometheus_metric *prometheus_gauge_create(const char *name,
	const char *help);

/*!
 * \brief Convert a metric (and its children) into Prometheus compatible text
 *
 * \param metric The metric to convert to a string
 * \param[out] output The \c ast_str string to populate with the metric(s)
 */
void prometheus_metric_to_string(struct prometheus_metric *metric,
	struct ast_str **output);

/*!
 * \brief Defines a callback that will be invoked when the HTTP route is called
 *
 * This callback presents the second way of passing metrics to a Prometheus
 * server. For metrics that are generated often or whose value needs to be
 * stored, metrics can be created and registered. For metrics that can be
 * obtained "on-the-fly", this mechanism is preferred. When the HTTP route is
 * queried by prometheus, the registered callbacks are invoked. The string passed
 * to the callback should be populated with stack-allocated metrics using
 * \c prometheus_metric_to_string.
 *
 * Example Usage:
 * \code
 *	static void prometheus_metric_callback(struct ast_str **output)
 *	{
 *		struct prometheus_metric test_counter =
 *			PROMETHEUS_METRIC_STATIC_INITIALIZATION(
 *				PROMETHEUS_METRIC_COUNTER,
 *				"test_counter",
 *				"A test counter",
 *				NULL);
 *
 *		prometheus_metric_to_string(&test_counter, output);
 *	}
 *
 *	static void load_module(void)
 *	{
 *		struct prometheus_callback callback = {
 *			.name = "test_callback",
 *			.callback_fn = &prometheus_metric_callback,
 *		};
 *
 *		prometheus_callback_register(&callback);
 *	}
 *
 * \endcode
 *
 */
struct prometheus_callback {
	/*!
	 * \brief The name of our callback (always useful for debugging)
	 */
	const char *name;
	/*!
	 * \brief The callback function to invoke
	 */
	void (* callback_fn)(struct ast_str **output);
};

/*!
 * Register a metric for collection
 *
 * \param metric The metric to register
 *
 * \retval 0 success
 * \retval -1 error
 */
int prometheus_metric_register(struct prometheus_metric *metric);

/*!
 * \brief Remove a registered metric
 *
 * \param metric The metric to unregister
 *
 * \note Unregistering also destroys the metric, if found
 *
 * \retval 0 The metric was found, unregistered, and disposed of
 * \retval -1 The metric was not found
 */
int prometheus_metric_unregister(struct prometheus_metric *metric);

/*!
 * The current number of registered metrics
 *
 * \retval The current number of registered metrics
 */
int prometheus_metric_registered_count(void);

/*!
 * Register a metric callback
 *
 * \param callback The callback to register
 *
 * \retval 0 success
 * \retval -1 error
 */
int prometheus_callback_register(struct prometheus_callback *callback);

/*!
 * \brief Remove a registered callback
 *
 * \param callback The callback to unregister
 */
void prometheus_callback_unregister(struct prometheus_callback *callback);

/*!
 * \brief Register a metrics provider
 *
 * \param provider The provider function table to register
 */
void prometheus_metrics_provider_register(const struct prometheus_metrics_provider *provider);

/*!
 * \brief Retrieve the current configuration of the module
 *
 * config is an AO2 ref counted object
 *
 * \note
 * This should primarily be done for testing purposes.
 *
 * \retval NULL on error
 * \retval config on success
 */
struct prometheus_general_config *prometheus_general_config_get(void);

/*!
 * \brief Set the configuration for the module
 *
 * This is not a ref-stealing function. The reference count to \c config
 * will be incremented as a result of calling this method.
 *
 * \note
 * This should primarily be done for testing purposes
 */
void prometheus_general_config_set(struct prometheus_general_config *config);

/*!
 * \brief Allocate a new configuration object
 *
 * The returned object is an AO2 ref counted object
 *
 * \retval NULL on error
 * \retval config on success
 */
void *prometheus_general_config_alloc(void);

#endif /* #ifndef RES_PROMETHEUS_H__ */
