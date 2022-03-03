/*
 * Asterisk -- An open source telephony toolkit.
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

/*!
 * \file
 * \brief Prometheus PJSIP Outbound Registration Metrics
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

#include "asterisk.h"

#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis_system.h"
#include "asterisk/res_prometheus.h"

#ifdef HAVE_PJPROJECT
#include "asterisk/res_pjsip.h"
#endif /* HAVE_PJPROJECT */

#include "prometheus_internal.h"

#ifdef HAVE_PJPROJECT

/*! \internal \brief Our one and only Stasis message router */
static struct stasis_message_router *router;

/*!
 * \internal
 * \brief Wrapper object around our Metrics
 *
 * \details We keep a wrapper around the metric so we can easily
 * update the value when the state of the registration changes, as
 * well as remove and unregister the metric when someone destroys
 * or reloads the registration
 */
struct prometheus_metric_wrapper {
	/*!
	 * \brief The actual metric. Worth noting that we do *NOT*
	 * own the metric, as it is registered with res_prometheus.
	 * Luckily, that module doesn't destroy metrics unless we
	 * tell it to or if the module unloads.
	 */
	struct prometheus_metric *metric;
	/*!
	 * \brief Unique key to look up the metric
	 */
	char key[128];
};

/*!
 * \internal Vector of metric wrappers
 *
 * \details
 * Why a vector and not an ao2_container? Two reasons:
 * (1) There's rarely a ton of outbound registrations, so an ao2_container
 * is overkill when we can just walk a vector
 * (2) The lifetime of wrappers is well contained
 */
static AST_VECTOR(, struct prometheus_metric_wrapper *) metrics;

AST_MUTEX_DEFINE_STATIC(metrics_lock);

/*!
 * \internal
 * \brief Create a wrapper for a metric given a key
 *
 * \param key The unique key
 *
 * \retval NULL on error
 * \retval malloc'd metric wrapper on success
 */
static struct prometheus_metric_wrapper *create_wrapper(const char *key)
{
	struct prometheus_metric_wrapper *wrapper;

	wrapper = ast_calloc(1, sizeof(*wrapper));
	if (!wrapper) {
		return NULL;
	}

	ast_copy_string(wrapper->key, key, sizeof(wrapper->key));
	return wrapper;
}

/*!
 * \internal
 * \brief Get a wrapper by its key
 *
 * \param key The unqiue key for the wrapper
 *
 * \retval NULL on no wrapper found :-\
 * \retval wrapper on success
 */
static struct prometheus_metric_wrapper *get_wrapper(const char *key)
{
	int i;
	SCOPED_MUTEX(lock, &metrics_lock);

	for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
		struct prometheus_metric_wrapper *wrapper = AST_VECTOR_GET(&metrics, i);

		if (!strcmp(wrapper->key, key)) {
			return wrapper;
		}
	}

	return NULL;
}

/*!
 * \internal
 * \brief Convert an outbound registration state to a numeric value
 *
 * \param state The state to convert
 *
 * \retval int representation of the state
 */
static int registration_state_to_int(const char *state)
{
	if (!strcasecmp(state, "Registered")) {
		return 1;
	} else if (!strcasecmp(state, "Rejected")) {
		return 2;
	}
	return 0;
}

/*!
 * \internal
 * \brief Sorcery observer callback called when a registration object is deleted
 *
 * \param obj The opaque object that was deleted
 */
static void registration_deleted_observer(const void *obj)
{
	struct ast_variable *fields;
	struct ast_variable *it_fields;
	int i;
	SCOPED_MUTEX(lock, &metrics_lock);

	/*
	 * Because our object is opaque, we have to do some pretty ... interesting
	 * things here to try and figure out what just happened.
	 */
	fields = ast_sorcery_objectset_create(ast_sip_get_sorcery(), obj);
	if (!fields) {
		ast_debug(1, "Unable to convert presumed registry object %p to strings; bailing on delete\n", obj);
		return;
	}

	for (it_fields = fields; it_fields; it_fields = it_fields->next) {
		if (strcasecmp(it_fields->name, "client_uri")) {
			continue;
		}

		for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
			struct prometheus_metric_wrapper *wrapper = AST_VECTOR_GET(&metrics, i);

			if (strcmp(wrapper->key, it_fields->value)) {
				continue;
			}

			ast_debug(1, "Registration metric '%s' deleted; purging with prejudice\n", wrapper->key);
			AST_VECTOR_REMOVE(&metrics, i, 1);
			/* This will free the metric as well */
			prometheus_metric_unregister(wrapper->metric);
			ast_free(wrapper);
		}
	}

	ast_variables_destroy(fields);
}

static const struct ast_sorcery_observer registration_observer = {
	.deleted = registration_deleted_observer,
};

/*!
 * \internal
 * \brief Sorcery observer called when an object is loaded/reloaded
 *
 * \param name The name of the object
 * \param sorcery The sorcery handle
 * \param object_type The type of object
 * \param reloaded Whether or not we reloaded the state/definition of the object
 *
 * \details
 * In our case, we only care when we re-load the registration object. We
 * wait for the registration to occur in order to create our Prometheus
 * metric, so we just punt on object creation. On reload, however, fundamental
 * properties of the metric may have been changed, which means we have to remove
 * the existing definition of the metric and allow the new registration stasis
 * message to re-build it.
 */
static void registration_loaded_observer(const char *name, const struct ast_sorcery *sorcery, const char *object_type, int reloaded)
{
	SCOPED_MUTEX(lock, &metrics_lock);
	int i;

	if (!reloaded) {
		/* Meh */
		return;
	}

	if (strcmp(object_type, "registration")) {
		/* Not interested */
		return;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&metrics); i++) {
		struct prometheus_metric_wrapper *wrapper = AST_VECTOR_GET(&metrics, i);
		struct ast_variable search_fields = {
			.name = "client_uri",
			.value = wrapper->key,
			.next = NULL,
		};
		void *obj;

		ast_debug(1, "Checking for the existance of registration metric %s\n", wrapper->key);
		obj = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), object_type, AST_RETRIEVE_FLAG_DEFAULT, &search_fields);
		if (!obj) {
			ast_debug(1, "Registration metric '%s' not found; purging with prejudice\n", wrapper->key);
			AST_VECTOR_REMOVE(&metrics, i, 1);
			/* This will free the metric as well */
			prometheus_metric_unregister(wrapper->metric);
			ast_free(wrapper);
			continue;
		}
		ao2_ref(obj, -1);
	}

}

static const struct ast_sorcery_instance_observer observer_callbacks_registrations = {
	.object_type_loaded = registration_loaded_observer,
};

/*!
 * \internal
 * \brief Callback for Stasis Registry messages
 *
 * \param data Callback data, always NULL
 * \param sub Stasis subscription
 * \param message Our Registry message
 *
 * \details
 * The Stasis Registry message both updates the state of the Prometheus metric
 * as well as forces its creation.
 */
static void registry_message_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_json_payload *payload = stasis_message_data(message);
	struct ast_json *json = payload->json;
	const char *username = ast_json_string_get(ast_json_object_get(json, "username"));
	const char *status_str = ast_json_string_get(ast_json_object_get(json, "status"));
	const char *domain = ast_json_string_get(ast_json_object_get(json, "domain"));
	const char *channel_type = ast_json_string_get(ast_json_object_get(json, "channeltype"));
	struct prometheus_metric metric = PROMETHEUS_METRIC_STATIC_INITIALIZATION(
		PROMETHEUS_METRIC_GAUGE,
		"asterisk_pjsip_outbound_registration_status",
		"Current registration status. 0=Unregistered; 1=Registered; 2=Rejected.",
		NULL
	);
	struct prometheus_metric_wrapper *wrapper;
	char eid_str[32];

	ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

	PROMETHEUS_METRIC_SET_LABEL(&metric, 0, "eid", eid_str);
	PROMETHEUS_METRIC_SET_LABEL(&metric, 1, "username", username);
	PROMETHEUS_METRIC_SET_LABEL(&metric, 2, "domain", domain);
	PROMETHEUS_METRIC_SET_LABEL(&metric, 3, "channel_type", channel_type);
	snprintf(metric.value, sizeof(metric.value), "%d", registration_state_to_int(status_str));

	wrapper = get_wrapper(username);
	if (wrapper) {
		ast_mutex_lock(&wrapper->metric->lock);
		/* Safe */
		strcpy(wrapper->metric->value, metric.value);
		ast_mutex_unlock(&wrapper->metric->lock);
	} else {
		wrapper = create_wrapper(username);
		if (!wrapper) {
			return;
		}

		wrapper->metric = prometheus_gauge_create(metric.name, metric.help);
		if (!wrapper->metric) {
			ast_free(wrapper);
			return;
		}
		*(wrapper->metric) = metric;

		prometheus_metric_register(wrapper->metric);
		AST_VECTOR_APPEND(&metrics, wrapper);
	}
}

#endif /* HAVE_PJPROJECT */

/*!
 * \internal
 * \brief Callback invoked when the core module is unloaded
 */
static void pjsip_outbound_registration_metrics_unload_cb(void)
{
#ifdef HAVE_PJPROJECT
	stasis_message_router_unsubscribe_and_join(router);
	router = NULL;
	ast_sorcery_instance_observer_remove(ast_sip_get_sorcery(), &observer_callbacks_registrations);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "registration", &registration_observer);
#endif /* HAVE_PJPROJECT */
}

/*!
 * \internal
 * \brief Metrics provider definition
 */
static struct prometheus_metrics_provider provider = {
	.name = "pjsip_outbound_registration",
	.unload_cb = pjsip_outbound_registration_metrics_unload_cb,
};

int pjsip_outbound_registration_metrics_init(void)
{
	prometheus_metrics_provider_register(&provider);

#ifdef HAVE_PJPROJECT
	router = stasis_message_router_create(ast_system_topic());
	if (!router) {
		goto cleanup;
	}

	if (stasis_message_router_add(router, ast_system_registry_type(), registry_message_cb, NULL)) {
		goto cleanup;
	}

	if (ast_sorcery_instance_observer_add(ast_sip_get_sorcery(), &observer_callbacks_registrations)) {
		goto cleanup;
	}

	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "registration", &registration_observer)) {
		goto cleanup;
	}
#endif /* HAVE_PJPROJECT */
	return 0;

#ifdef HAVE_PJPROJECT
cleanup:
	ao2_cleanup(router);
	router = NULL;
	ast_sorcery_instance_observer_remove(ast_sip_get_sorcery(), &observer_callbacks_registrations);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "registration", &registration_observer);

	return -1;
#endif /* HAVE_PJPROJECT */
}
