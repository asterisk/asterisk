/*
 * res_pjsip_private.h
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#ifndef RES_PJSIP_PRIVATE_H_
#define RES_PJSIP_PRIVATE_H_

/*!
 * \todo XXX Functions prototyped in this file that begin with "ast_sip_"
 * need to be renamed so res_pjsip.so does not export the names outside
 * of the module.
 */

#include "asterisk/module.h"
#include "asterisk/compat.h"

struct ao2_container;
struct ast_threadpool_options;
struct ast_sip_cli_context;

/*!
 * \internal
 * \brief Initialize the configuration for res_pjsip
 */
int ast_res_pjsip_initialize_configuration(void);

/*!
 * \internal
 * \brief Annihilate the configuration objects
 */
void ast_res_pjsip_destroy_configuration(void);

/*!
 * \internal
 * \brief Reload the configuration
 */
int ast_res_pjsip_reload_configuration(void);

/*!
 * \internal
 * \brief Initialize transport support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_transport(void);

/*!
 * \internal
 * \brief Destroy transport support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_destroy_sorcery_transport(void);

/*!
 * \internal
 * \brief Initialize qualify support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_qualify(void);

/*!
 * \internal
 * \brief Initialize location support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_location(void);

/*!
 * \internal
 * \brief Destroy location support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_destroy_sorcery_location(void);

/*!
 * \internal
 * \brief Initialize domain aliases support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_domain_alias(void);

/*!
 * \internal
 * \brief Initialize authentication support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_auth(void);

/*!
 * \internal
 * \brief Destroy authentication support on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_destroy_sorcery_auth(void);

/*!
 * \internal
 * \brief Initialize the distributor module
 *
 * The distributor module is responsible for taking an incoming
 * SIP message and placing it into the threadpool. Once in the threadpool,
 * the distributor will perform endpoint lookups and authentication, and
 * then distribute the message up the stack to any further modules.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_sip_initialize_distributor(void);

/*!
 * \internal
 * \brief Destruct the distributor module.
 *
 * Unregisters pjsip modules and cleans up any allocated resources.
 */
void ast_sip_destroy_distributor(void);

/*!
 * \internal
 * \brief Initialize the transport events notify module
 * \since 13.18.0
 *
 * The transport events notify module is responsible for monitoring
 * when transports die and calling any registered callbacks when that
 * happens.  It also manages any PJPROJECT transport state callbacks
 * registered to it so the callbacks be more dynamic allowing module
 * loading/unloading.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_sip_initialize_transport_events(void);

/*!
 * \internal
 * \brief Destruct the transport events notify module.
 * \since 13.18.0
 */
void ast_sip_destroy_transport_events(void);

/*!
 * \internal
 * \brief Initialize global type on a sorcery instance
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_sorcery_global(void);

/*!
 * \internal
 * \brief Destroy global type on a sorcery instance
 * \since 13.3.0
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_destroy_sorcery_global(void);

/*!
 * \internal
 * \brief Initialize global headers support
 *
 * \return Nothing
 */
void ast_sip_initialize_global_headers(void);

/*!
 * \internal
 * \brief Destroy global headers support
 *
 * \return Nothing
 */
void ast_sip_destroy_global_headers(void);

/*!
 * \internal
 * \brief Pre-initialize OPTIONS request handling.
 *
 * \retval 0 on success
 * \retval other on failure
 */
int ast_res_pjsip_preinit_options_handling(void);

/*!
 * \internal
 * \brief Initialize OPTIONS request handling.
 *
 * XXX This currently includes qualifying peers. It shouldn't.
 * That should go into a registrar. When that occurs, we won't
 * need the reload stuff.
 *
 * \param reload Reload options handling
 *
 * \retval 0 on success
 * \retval other on failure
 */
int ast_res_pjsip_init_options_handling(int reload);

/*!
 * \internal Initialize message IP updating handling.
 *
 * \retval 0 on success
 * \retval other on failure
 */
int ast_res_pjsip_init_message_filter(void);

/*!
 * \internal
 * \brief Initialize transport storage for contacts.
 *
 * \retval 0 on success
 * \retval other on failure
 */
int ast_res_pjsip_init_contact_transports(void);

/*!
 * \internal
 * \brief Initialize system configuration
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_initialize_system(void);

/*!
 * \internal
 * \brief Destroy system configuration
 */
void ast_sip_destroy_system(void);

/*!
 * \internal
 * \brief Initialize nameserver configuration
 */
void ast_sip_initialize_dns(void);

/*!
 * \internal
 * \brief Initialize our own resolver support
 */
void ast_sip_initialize_resolver(void);

/*!
 * \internal
 * \brief Initialize global configuration
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_initialize_global(void);

/*!
 * \internal
 * \brief Clean up res_pjsip options handling
 */
void ast_res_pjsip_cleanup_options_handling(void);

/*!
 * \internal
 * \brief Clean up res_pjsip message ip updating handling
 */
void ast_res_pjsip_cleanup_message_filter(void);

/*!
 * \internal
 * \brief Get threadpool options
 */
void sip_get_threadpool_options(struct ast_threadpool_options *threadpool_options);

/*!
 * \internal
 * \brief Retrieve the name of the default outbound endpoint.
 *
 * \note This returns a memory allocated copy of the name that
 *       needs to be freed by the caller.
 *
 * \retval The name of the default outbound endpoint.
 * \retval NULL if configuration not found.
 */
char *ast_sip_global_default_outbound_endpoint(void);

/*!
 * \internal
 * \brief Functions for initializing and destroying the CLI.
 */
int ast_sip_initialize_cli(void);
void ast_sip_destroy_cli(void);

/*!
 * \internal
 * \brief Add res_pjsip global configuration options to the cli context.
 *
 * \param context context to add options to
 * \retval 0 Success, -1 on failure
 */
int sip_cli_print_global(struct ast_sip_cli_context *context);

/*!
 * \internal
 * \brief Add res_pjsip system configuration options to the cli context.
 *
 * \param context context to add options to
 * \retval 0 Success, -1 on failure
 */
int sip_cli_print_system(struct ast_sip_cli_context *context);

struct ast_sip_session_supplement;

/*!
 * \internal
 * \brief Finds or creates contact_status for a contact
 */
struct ast_sip_contact_status *ast_res_pjsip_find_or_create_contact_status(const struct ast_sip_contact *contact);

/*!
 * \internal
 * \brief Validate that the uri meets pjproject length restrictions
 */
int ast_sip_validate_uri_length(const char *uri);

/*!
 * \brief Initialize scheduler
 * \since 13.9.0
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_initialize_scheduler(void);

/*!
 * \internal
 * \brief Destroy scheduler
 * \since 13.9.0
 *
 * \retval -1 failure
 * \retval 0 success
 */
int ast_sip_destroy_scheduler(void);

/*!
 * \internal
 * \brief Determines if a uri will still be valid after an asterisk restart
 * \since 13.20.0
 *
 * \param uri uri to test
 * \param endpoint The associated endpoint
 * \param rdata The rdata to get transport information from
 *
 * \retval 1 Yes, 0 No
 */
int ast_sip_will_uri_survive_restart(pjsip_sip_uri *uri, struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata);

/*!
 * \internal
 * \brief Initialize the transport management module
 * \since 13.20.0
 *
 * The transport management module is responsible for 3 things...
 * 1.  It automatically destroys any reliable transport that does not
 * receive a valid request within system/timer_b milliseconds of the
 * connection being opened. (Attack mitigation)
 * 2.  Since it increments the reliable transport's reference count
 * for that period of time, it also prevents issues if the transport
 * disconnects while we're still trying to process a response.
 *  (Attack mitigation)
 * 3.  If enabled by global/keep_alive_interval, it sends '\r\n'
 * keepalives on reliable transports at the interval specified.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_sip_initialize_transport_management(void);

/*!
 * \internal
 * \brief Destruct the transport management module.
 * \since 13.20.0
 */
void ast_sip_destroy_transport_management(void);

#endif /* RES_PJSIP_PRIVATE_H_ */
