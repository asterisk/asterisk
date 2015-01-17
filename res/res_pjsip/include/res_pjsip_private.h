/*
 * res_pjsip.h
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#ifndef RES_PJSIP_PRIVATE_H_
#define RES_PJSIP_PRIVATE_H_

#include "asterisk/module.h"
#include "asterisk/compat.h"

struct ao2_container;
struct ast_threadpool_options;

/*!
 * \brief Initialize the configuration for res_pjsip
 */
int ast_res_pjsip_initialize_configuration(const struct ast_module_info *ast_module_info);

/*!
 * \brief Annihilate the configuration objects
 */
void ast_res_pjsip_destroy_configuration(void);

/*!
 * \brief Reload the configuration
 */
int ast_res_pjsip_reload_configuration(void);

/*!
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
 * \brief Initialize transport storage for contacts.
 *
 * \retval 0 on success
 * \retval other on failure
 */
int ast_res_pjsip_init_contact_transports(void);

/*!
 * \brief Initialize outbound authentication support
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_initialize_outbound_authentication(void);

/*!
 * \brief Initialize system configuration
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_initialize_system(void);

/*!
 * \brief Destroy system configuration
 */
void ast_sip_destroy_system(void);

/*!
 * \brief Initialize nameserver configuration
 */
void ast_sip_initialize_dns(void);

/*!
 * \brief Initialize global configuration
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_initialize_global(void);

/*!
 * \brief Clean up res_pjsip options handling
 */
void ast_res_pjsip_cleanup_options_handling(void);

/*!
 * \brief Get threadpool options
 */
void sip_get_threadpool_options(struct ast_threadpool_options *threadpool_options);

/*!
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
 * \brief Functions for initializing and destroying the CLI.
 */
int ast_sip_initialize_cli(void);
void ast_sip_destroy_cli(void);

#endif /* RES_PJSIP_PRIVATE_H_ */
