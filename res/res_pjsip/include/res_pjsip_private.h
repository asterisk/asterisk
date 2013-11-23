/*
 * res_pjsip.h
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#ifndef RES_PJSIP_PRIVATE_H_
#define RES_PJSIP_PRIVATE_H_

#include "asterisk/module.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"

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
 * \brief Function pointer for channel snapshot callbacks.
 */
typedef int (*on_channel_snapshot_t)(
	const struct ast_channel_snapshot *snapshot, int last, void *arg);

/*!
 * \brief For every channel snapshot on an endpoint snapshot call the given
 *        'on_channel_snapshot' handler.
 *
 * \param endpoint_snapshot snapshot of an endpoint
 * \param on_channel_snapshot callback for each channel snapshot
 * \param arg user data passed to handler
 * \retval 0 Success, non-zero on failure
 */
int ast_sip_for_each_channel_snapshot(const struct ast_endpoint_snapshot *endpoint_snapshot,
				      on_channel_snapshot_t on_channel_snapshot,
				      void *arg);

#endif /* RES_PJSIP_PRIVATE_H_ */
