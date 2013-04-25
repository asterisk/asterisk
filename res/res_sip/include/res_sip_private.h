/*
 * res_sip.h
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#ifndef RES_SIP_PRIVATE_H_
#define RES_SIP_PRIVATE_H_

struct ao2_container;

/*!
 * \brief Initialize the configuration for res_sip
 */
int ast_res_sip_initialize_configuration(void);

/*!
 * \brief Annihilate the configuration objects
 */
void ast_res_sip_destroy_configuration(void);

/*!
 * \brief Reload the configuration
 */
int ast_res_sip_reload_configuration(void);

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
int ast_res_sip_init_options_handling(int reload);

/*!
 * \brief Initialize outbound authentication support
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sip_initialize_outbound_authentication(void);

/*!
 * \brief Get the current defined endpoints
 *
 * \retval The current endpoints loaded by res_sip
 */
struct ao2_container *ast_res_sip_get_endpoints(void);

#endif /* RES_SIP_PRIVATE_H_ */
