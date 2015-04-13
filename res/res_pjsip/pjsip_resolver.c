/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib-util/errno.h>

#include <arpa/nameser.h>

#include "asterisk/astobj2.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_query_set.h"
#include "asterisk/dns_srv.h"
#include "asterisk/dns_naptr.h"
#include "asterisk/res_pjsip.h"
#include "include/res_pjsip_private.h"

#ifdef HAVE_PJSIP_EXTERNAL_RESOLVER

/*! \brief Structure which contains transport+port information for an active query */
struct sip_target {
	/*! \brief The transport to be used */
	pjsip_transport_type_e transport;
	/*! \brief The port */
	int port;
};

/*! \brief The vector used for current targets */
AST_VECTOR(targets, struct sip_target);

/*! \brief Structure which keeps track of resolution */
struct sip_resolve {
	/*! \brief Addresses currently being resolved, indexed based on index of queries in query set */
	struct targets resolving;
	/*! \brief Active queries */
	struct ast_dns_query_set *queries;
	/*! \brief Current viable server addresses */
	pjsip_server_addresses addresses;
	/*! \brief Callback to invoke upon completion */
	pjsip_resolver_callback *callback;
	/*! \brief User provided data */
	void *token;
};

/*! \brief Our own defined transports, reduces the size of sip_available_transports */
enum sip_resolver_transport {
	SIP_RESOLVER_TRANSPORT_UDP,
	SIP_RESOLVER_TRANSPORT_TCP,
	SIP_RESOLVER_TRANSPORT_TLS,
	SIP_RESOLVER_TRANSPORT_UDP6,
	SIP_RESOLVER_TRANSPORT_TCP6,
	SIP_RESOLVER_TRANSPORT_TLS6,
};

/*! \brief Available transports on the system */
static int sip_available_transports[] = {
	/* This is a list of transports with whether they are available as a valid transport
	 * stored. We use our own identifier as to reduce the size of sip_available_transports.
	 * As this array is only manipulated at startup it does not require a lock to protect
	 * it.
	 */
	[SIP_RESOLVER_TRANSPORT_UDP] = 0,
	[SIP_RESOLVER_TRANSPORT_TCP] = 0,
	[SIP_RESOLVER_TRANSPORT_TLS] = 0,
	[SIP_RESOLVER_TRANSPORT_UDP6] = 0,
	[SIP_RESOLVER_TRANSPORT_TCP6] = 0,
	[SIP_RESOLVER_TRANSPORT_TLS6] = 0,
};

/*!
 * \internal
 * \brief Destroy resolution data
 *
 * \param data The resolution data to destroy
 *
 * \return Nothing
 */
static void sip_resolve_destroy(void *data)
{
	struct sip_resolve *resolve = data;

	AST_VECTOR_FREE(&resolve->resolving);
	ao2_cleanup(resolve->queries);
}

/*!
 * \internal
 * \brief Check whether a transport is available or not
 *
 * \param transport The PJSIP transport type
 *
 * \return 1 success (transport is available)
 * \return 0 failure (transport is not available)
 */
static int sip_transport_is_available(enum pjsip_transport_type_e transport)
{
	enum sip_resolver_transport resolver_transport;

	if (transport == PJSIP_TRANSPORT_UDP) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_UDP;
	} else if (transport == PJSIP_TRANSPORT_TCP) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TCP;
	} else if (transport == PJSIP_TRANSPORT_TLS) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TLS;
	} else if (transport == PJSIP_TRANSPORT_UDP6) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_UDP6;
	} else if (transport == PJSIP_TRANSPORT_TCP6) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TCP6;
	} else if (transport == PJSIP_TRANSPORT_TLS6) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TLS6;
	} else {
		return 0;
	}

	return sip_available_transports[resolver_transport];
}

/*!
 * \internal
 * \brief Add a query to be resolved
 *
 * \param resolve The ongoing resolution
 * \param name What to resolve
 * \param rr_type The type of record to look up
 * \param rr_class The type of class to look up
 * \param transport The transport to use for any resulting records
 * \param port The port to use for any resulting records - if not specified the default for the transport is used
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int sip_resolve_add(struct sip_resolve *resolve, const char *name, int rr_type, int rr_class, pjsip_transport_type_e transport, int port)
{
	struct sip_target target = {
		.transport = transport,
		.port = port,
	};

	if (!resolve->queries) {
		resolve->queries = ast_dns_query_set_create();
	}

	if (!resolve->queries) {
		return -1;
	}

	if (!port) {
		target.port = pjsip_transport_get_default_port_for_type(transport);
	}

	if (AST_VECTOR_APPEND(&resolve->resolving, target)) {
		return -1;
	}

	ast_debug(2, "[%p] Added target '%s' with record type '%d', transport '%s', and port '%d'\n", resolve, name, rr_type,
		pjsip_transport_get_type_name(transport), target.port);

	return ast_dns_query_set_add(resolve->queries, name, rr_type, rr_class);
}

/*!
 * \internal
 * \brief Task used to invoke the user specific callback
 *
 * \param data The complete resolution
 *
 * \return Nothing
 */
static int sip_resolve_invoke_user_callback(void *data)
{
	struct sip_resolve *resolve = data;
	int idx;

	for (idx = 0; idx < resolve->addresses.count; ++idx) {
		/* This includes space for the IP address, [, ], :, and the port */
		char addr[PJ_INET6_ADDRSTRLEN + 10];

		ast_debug(2, "[%p] Address '%d' is %s with transport '%s'\n",
			resolve, idx, pj_sockaddr_print(&resolve->addresses.entry[idx].addr, addr, sizeof(addr), 3),
			pjsip_transport_get_type_name(resolve->addresses.entry[idx].type));
	}

	ast_debug(2, "[%p] Invoking user callback with '%d' addresses\n", resolve, resolve->addresses.count);
	resolve->callback(resolve->addresses.count ? PJ_SUCCESS : PJLIB_UTIL_EDNSNOANSWERREC, resolve->token, &resolve->addresses);

	ao2_ref(resolve, -1);

	return 0;
}

/*!
 * \internal
 * \brief Handle a NAPTR record according to RFC3263
 *
 * \param resolve The ongoing resolution
 * \param record The NAPTR record itself
 * \param service The service to look for
 * \param transport The transport to use for resulting queries
 *
 * \retval 0 success
 * \retval -1 failure (record not handled / supported)
 */
static int sip_resolve_handle_naptr(struct sip_resolve *resolve, const struct ast_dns_record *record,
	const char *service, pjsip_transport_type_e transport)
{
	if (strcasecmp(ast_dns_naptr_get_service(record), service)) {
		return -1;
	}

	if (!sip_transport_is_available(transport) &&
		!sip_transport_is_available(transport + PJSIP_TRANSPORT_IPV6)) {
		return -1;
	}

	if (strcasecmp(ast_dns_naptr_get_flags(record), "s")) {
		ast_debug(2, "[%p] NAPTR service %s received with unsupported flags '%s'\n",
			resolve, service, ast_dns_naptr_get_flags(record));
		return -1;
	}

	if (ast_strlen_zero(ast_dns_naptr_get_replacement(record))) {
		return -1;
	}

	return sip_resolve_add(resolve, ast_dns_naptr_get_replacement(record), ns_t_srv, ns_c_in,
		transport, 0);
}

/*!
 * \internal
 * \brief Query set callback function, invoked when all queries have completed
 *
 * \param query_set The completed query set
 *
 * \return Nothing
 */
static void sip_resolve_callback(const struct ast_dns_query_set *query_set)
{
	struct sip_resolve *resolve = ast_dns_query_set_get_data(query_set);
	struct ast_dns_query_set *queries = resolve->queries;
	struct targets resolving;
	int idx, address_count = 0, have_naptr = 0, have_srv = 0;
	unsigned short order = 0;
	int strict_order = 0;

	ast_debug(2, "[%p] All parallel queries completed\n", resolve);

	resolve->queries = NULL;

	/* This purposely steals the resolving list so we can add entries to the new one in the same loop and also have access
	 * to the old.
	 */
	resolving = resolve->resolving;
	AST_VECTOR_INIT(&resolve->resolving, 0);

	/* The order of queries is what defines the preference order for the records within this specific query set.
	 * The preference order overall is defined as a result of drilling down from other records. Each
	 * completed query set starts placing records at the beginning, moving others that may have already been present.
	 */
	for (idx = 0; idx < ast_dns_query_set_num_queries(queries); ++idx) {
		struct ast_dns_query *query = ast_dns_query_set_get(queries, idx);
		struct ast_dns_result *result = ast_dns_query_get_result(query);
		struct sip_target *target;
		const struct ast_dns_record *record;

		if (!result) {
			ast_debug(2, "[%p] No result information for target '%s' of type '%d'\n", resolve,
				ast_dns_query_get_name(query), ast_dns_query_get_rr_type(query));
			continue;
		}

		target = AST_VECTOR_GET_ADDR(&resolving, idx);
		for (record = ast_dns_result_get_records(result); record; record = ast_dns_record_get_next(record)) {

			if (ast_dns_record_get_rr_type(record) == ns_t_a ||
				ast_dns_record_get_rr_type(record) == ns_t_aaaa) {
				/* If NAPTR or SRV records exist the subsequent results from them take preference */
				if (have_naptr || have_srv) {
					ast_debug(2, "[%p] %s record being skipped on target '%s' because NAPTR or SRV record exists\n",
						resolve, ast_dns_record_get_rr_type(record) == ns_t_a ? "A" : "AAAA",
						ast_dns_query_get_name(query));
					continue;
				}

				/* PJSIP has a fixed maximum number of addresses that can exist, so limit ourselves to that */
				if (address_count == PJSIP_MAX_RESOLVED_ADDRESSES) {
					continue;
				}

				resolve->addresses.entry[address_count].type = target->transport;

				/* Populate address information for the new address entry */
				if (ast_dns_record_get_rr_type(record) == ns_t_a) {
					ast_debug(2, "[%p] A record received on target '%s'\n", resolve, ast_dns_query_get_name(query));
					resolve->addresses.entry[address_count].addr_len = sizeof(pj_sockaddr_in);
					pj_sockaddr_init(pj_AF_INET(), &resolve->addresses.entry[address_count].addr, NULL,
						target->port);
					resolve->addresses.entry[address_count].addr.ipv4.sin_addr = *(struct pj_in_addr*)ast_dns_record_get_data(record);
				} else {
					ast_debug(2, "[%p] AAAA record received on target '%s'\n", resolve, ast_dns_query_get_name(query));
					resolve->addresses.entry[address_count].addr_len = sizeof(pj_sockaddr_in6);
					pj_sockaddr_init(pj_AF_INET6(), &resolve->addresses.entry[address_count].addr, NULL,
						target->port);
					pj_memcpy(&resolve->addresses.entry[address_count].addr.ipv6.sin6_addr, ast_dns_record_get_data(record),
						ast_dns_record_get_data_size(record));
				}

				address_count++;
			} else if (ast_dns_record_get_rr_type(record) == ns_t_srv) {
				if (have_naptr) {
					ast_debug(2, "[%p] SRV record being skipped on target '%s' because NAPTR record exists\n",
						resolve, ast_dns_query_get_name(query));
					continue;
				}

				/* SRV records just create new queries for AAAA+A, nothing fancy */
				ast_debug(2, "[%p] SRV record received on target '%s'\n", resolve, ast_dns_query_get_name(query));

				if (sip_transport_is_available(target->transport + PJSIP_TRANSPORT_IPV6)) {
					sip_resolve_add(resolve, ast_dns_srv_get_host(record), ns_t_aaaa, ns_c_in, target->transport + PJSIP_TRANSPORT_IPV6,
						ast_dns_srv_get_port(record));
					have_srv = 1;
				}

				if (sip_transport_is_available(target->transport)) {
					sip_resolve_add(resolve, ast_dns_srv_get_host(record), ns_t_a, ns_c_in, target->transport,
						ast_dns_srv_get_port(record));
					have_srv = 1;
				}
			} else if (ast_dns_record_get_rr_type(record) == ns_t_naptr) {
				int added = -1;

				ast_debug(2, "[%p] NAPTR record received on target '%s'\n", resolve, ast_dns_query_get_name(query));

				if (strict_order && (ast_dns_naptr_get_order(record) != order)) {
					ast_debug(2, "[%p] NAPTR record skipped because order '%hu' does not match strict order '%hu'\n",
						resolve, ast_dns_naptr_get_order(record), order);
					continue;
				}

				if (target->transport == PJSIP_TRANSPORT_UNSPECIFIED || target->transport == PJSIP_TRANSPORT_UDP) {
					added = sip_resolve_handle_naptr(resolve, record, "sip+d2u", PJSIP_TRANSPORT_UDP);
				}
				if (target->transport == PJSIP_TRANSPORT_UNSPECIFIED || target->transport == PJSIP_TRANSPORT_TCP) {
					added = sip_resolve_handle_naptr(resolve, record, "sip+d2t", PJSIP_TRANSPORT_TCP);
				}
				if (target->transport == PJSIP_TRANSPORT_UNSPECIFIED || target->transport == PJSIP_TRANSPORT_TLS) {
					added = sip_resolve_handle_naptr(resolve, record, "sips+d2t", PJSIP_TRANSPORT_TLS);
				}

				/* If this record was successfully handled then we need to limit ourselves to this order */
				if (!added) {
					have_naptr = 1;
					strict_order = 1;
					order = ast_dns_naptr_get_order(record);
				}
			}
		}
	}

	/* Update the server addresses count, this is not limited as it can never exceed the max allowed */
	resolve->addresses.count = address_count;

	/* Free the vector we stole as we are responsible for it */
	AST_VECTOR_FREE(&resolving);

	/* If additional queries were added start the resolution process again */
	if (resolve->queries) {
		ast_debug(2, "[%p] New queries added, performing parallel resolution again\n", resolve);
		ast_dns_query_set_resolve_async(resolve->queries, sip_resolve_callback, resolve);
		ao2_ref(queries, -1);
		return;
	}

	ast_debug(2, "[%p] Resolution completed - %d viable targets\n", resolve, resolve->addresses.count);

	/* Push a task to invoke the callback, we do this so it is guaranteed to run in a PJSIP thread */
	ao2_ref(resolve, +1);
	if (ast_sip_push_task(NULL, sip_resolve_invoke_user_callback, resolve)) {
		ao2_ref(resolve, -1);
	}

	ao2_ref(queries, -1);
}

/*!
 * \internal
 * \brief Determine what address family a host may be if it is already an IP address
 *
 * \param host The host (which may be an IP address)
 *
 * \retval 6 The host is an IPv6 address
 * \retval 4 The host is an IPv4 address
 * \retval 0 The host is not an IP address
 */
static int sip_resolve_get_ip_addr_ver(const pj_str_t *host)
{
	pj_in_addr dummy;
	pj_in6_addr dummy6;

	if (pj_inet_aton(host, &dummy) > 0) {
		return 4;
	}

	if (pj_inet_pton(pj_AF_INET6(), host, &dummy6) == PJ_SUCCESS) {
		return 6;
	}

	return 0;
}

/*!
 * \internal
 * \brief Perform SIP resolution of a host
 *
 * \param resolver Configured resolver instance
 * \param pool Memory pool to allocate things from
 * \param target The target we are resolving
 * \param token User data to pass to the resolver callback
 * \param cb User resolver callback to invoke upon resolution completion
 */
static void sip_resolve(pjsip_resolver_t *resolver, pj_pool_t *pool, const pjsip_host_info *target,
	void *token, pjsip_resolver_callback *cb)
{
	int ip_addr_ver;
	pjsip_transport_type_e type = target->type;
	struct sip_resolve *resolve;
	char host[NI_MAXHOST];
	int res = 0;

	ast_copy_pj_str(host, &target->addr.host, sizeof(host));

	ast_debug(2, "Performing SIP DNS resolution of target '%s'\n", host);

	/* If the provided target is already an address don't bother resolving */
	ip_addr_ver = sip_resolve_get_ip_addr_ver(&target->addr.host);

	/* Determine the transport to use if none has been explicitly specified */
	if (type == PJSIP_TRANSPORT_UNSPECIFIED) {
		/* If we've been told to use a secure or reliable transport restrict ourselves to that */
#if PJ_HAS_TCP
		if (target->flag & PJSIP_TRANSPORT_SECURE) {
			type = PJSIP_TRANSPORT_TLS;
		} else if (target->flag & PJSIP_TRANSPORT_RELIABLE) {
			type = PJSIP_TRANSPORT_TCP;
		} else
#endif
		/* According to the RFC otherwise if an explicit IP address OR an explicit port is specified
		 * we use UDP
		 */
		if (ip_addr_ver || target->addr.port) {
			type = PJSIP_TRANSPORT_UDP;
		}

		if (ip_addr_ver == 6) {
			type = (pjsip_transport_type_e)((int) type + PJSIP_TRANSPORT_IPV6);
		}
	}

	ast_debug(2, "Transport type for target '%s' is '%s'\n", host, pjsip_transport_get_type_name(type));

	/* If it's already an address call the callback immediately */
	if (ip_addr_ver) {
		pjsip_server_addresses addresses = {
			.entry[0].type = type,
			.count = 1,
		};

		if (ip_addr_ver == 4) {
			addresses.entry[0].addr_len = sizeof(pj_sockaddr_in);
			pj_sockaddr_init(pj_AF_INET(), &addresses.entry[0].addr, NULL, 0);
			pj_inet_aton(&target->addr.host, &addresses.entry[0].addr.ipv4.sin_addr);
		} else {
			addresses.entry[0].addr_len = sizeof(pj_sockaddr_in6);
			pj_sockaddr_init(pj_AF_INET6(), &addresses.entry[0].addr, NULL, 0);
			pj_inet_pton(pj_AF_INET6(), &target->addr.host, &addresses.entry[0].addr.ipv6.sin6_addr);
		}

		pj_sockaddr_set_port(&addresses.entry[0].addr, !target->addr.port ? pjsip_transport_get_default_port_for_type(type) : target->addr.port);

		ast_debug(2, "Target '%s' is an IP address, skipping resolution\n", host);

		cb(PJ_SUCCESS, token, &addresses);

		return;
	}

	resolve = ao2_alloc_options(sizeof(*resolve), sip_resolve_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!resolve) {
		cb(PJ_ENOMEM, token, NULL);
		return;
	}

	resolve->callback = cb;
	resolve->token = token;

	if (AST_VECTOR_INIT(&resolve->resolving, 2)) {
		ao2_ref(resolve, -1);
		cb(PJ_ENOMEM, token, NULL);
		return;
	}

	ast_debug(2, "[%p] Created resolution tracking for target '%s'\n", resolve, host);

	/* If no port has been specified we can do NAPTR + SRV */
	if (!target->addr.port) {
		char srv[NI_MAXHOST];

		res |= sip_resolve_add(resolve, host, ns_t_naptr, ns_c_in, type, 0);

		if ((type == PJSIP_TRANSPORT_TLS || type == PJSIP_TRANSPORT_UNSPECIFIED) &&
			(sip_transport_is_available(PJSIP_TRANSPORT_TLS) ||
				sip_transport_is_available(PJSIP_TRANSPORT_TLS6))) {
			snprintf(srv, sizeof(srv), "_sips._tcp.%s", host);
			res |= sip_resolve_add(resolve, srv, ns_t_srv, ns_c_in, PJSIP_TRANSPORT_TLS, 0);
		}
		if ((type == PJSIP_TRANSPORT_TCP || type == PJSIP_TRANSPORT_UNSPECIFIED) &&
			(sip_transport_is_available(PJSIP_TRANSPORT_TCP) ||
				sip_transport_is_available(PJSIP_TRANSPORT_TCP6))) {
			snprintf(srv, sizeof(srv), "_sip._tcp.%s", host);
			res |= sip_resolve_add(resolve, srv, ns_t_srv, ns_c_in, PJSIP_TRANSPORT_TCP, 0);
		}
		if ((type == PJSIP_TRANSPORT_UDP || type == PJSIP_TRANSPORT_UNSPECIFIED) &&
			(sip_transport_is_available(PJSIP_TRANSPORT_UDP) ||
				sip_transport_is_available(PJSIP_TRANSPORT_UDP6))) {
			snprintf(srv, sizeof(srv), "_sip._udp.%s", host);
			res |= sip_resolve_add(resolve, srv, ns_t_srv, ns_c_in, PJSIP_TRANSPORT_UDP, 0);
		}
	}

	if ((type == PJSIP_TRANSPORT_UNSPECIFIED && sip_transport_is_available(PJSIP_TRANSPORT_UDP6)) ||
		sip_transport_is_available(type + PJSIP_TRANSPORT_IPV6)) {
		res |= sip_resolve_add(resolve, host, ns_t_aaaa, ns_c_in, (type == PJSIP_TRANSPORT_UNSPECIFIED ? PJSIP_TRANSPORT_UDP6 : type + PJSIP_TRANSPORT_IPV6), target->addr.port);
	}

	if ((type == PJSIP_TRANSPORT_UNSPECIFIED && sip_transport_is_available(PJSIP_TRANSPORT_UDP)) ||
		sip_transport_is_available(type)) {
		res |= sip_resolve_add(resolve, host, ns_t_a, ns_c_in, (type == PJSIP_TRANSPORT_UNSPECIFIED ? PJSIP_TRANSPORT_UDP : type), target->addr.port);
	}

	if (res) {
		ao2_ref(resolve, -1);
		cb(PJ_ENOMEM, token, NULL);
		return;
	}

	ast_debug(2, "[%p] Starting initial resolution using parallel queries for target '%s'\n", resolve, host);
	ast_dns_query_set_resolve_async(resolve->queries, sip_resolve_callback, resolve);

	ao2_ref(resolve, -1);
}

/*!
 * \internal
 * \brief Determine if a specific transport is configured on the system
 *
 * \param pool A memory pool to allocate things from
 * \param transport The type of transport to check
 * \param name A friendly name to print in the verbose message
 *
 * \return Nothing
 */
static void sip_check_transport(pj_pool_t *pool, pjsip_transport_type_e transport, const char *name)
{
	pjsip_tpmgr_fla2_param prm;
	enum sip_resolver_transport resolver_transport;

	pjsip_tpmgr_fla2_param_default(&prm);
	prm.tp_type = transport;

	if (transport == PJSIP_TRANSPORT_UDP) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_UDP;
	} else if (transport == PJSIP_TRANSPORT_TCP) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TCP;
	} else if (transport == PJSIP_TRANSPORT_TLS) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TLS;
	} else if (transport == PJSIP_TRANSPORT_UDP6) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_UDP6;
	} else if (transport == PJSIP_TRANSPORT_TCP6) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TCP6;
	} else if (transport == PJSIP_TRANSPORT_TLS6) {
		resolver_transport = SIP_RESOLVER_TRANSPORT_TLS6;
	} else {
		ast_verb(2, "'%s' is an unsupported SIP transport\n", name);
		return;
	}

	if (pjsip_tpmgr_find_local_addr2(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()),
		pool, &prm) == PJ_SUCCESS) {
		ast_verb(2, "'%s' is an available SIP transport\n", name);
		sip_available_transports[resolver_transport] = 1;
	} else {
		ast_verb(2, "'%s' is not an available SIP transport, disabling resolver support for it\n",
			name);
	}
}

/*! \brief External resolver implementation for PJSIP */
static pjsip_ext_resolver resolver = {
	.resolve = sip_resolve,
};

/*!
 * \internal
 * \brief Task to determine available transports and set ourselves an external resolver
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int sip_replace_resolver(void *data)
{
	pj_pool_t *pool;


	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Transport Availability", 256, 256);
	if (!pool) {
		return -1;
	}

	/* Determine what transports are available on the system */
	sip_check_transport(pool, PJSIP_TRANSPORT_UDP, "UDP+IPv4");
	sip_check_transport(pool, PJSIP_TRANSPORT_TCP, "TCP+IPv4");
	sip_check_transport(pool, PJSIP_TRANSPORT_TLS, "TLS+IPv4");
	sip_check_transport(pool, PJSIP_TRANSPORT_UDP6, "UDP+IPv6");
	sip_check_transport(pool, PJSIP_TRANSPORT_TCP6, "TCP+IPv6");
	sip_check_transport(pool, PJSIP_TRANSPORT_TLS6, "TLS+IPv6");

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);

	/* Replace the PJSIP resolver with our own implementation */
	pjsip_endpt_set_ext_resolver(ast_sip_get_pjsip_endpoint(), &resolver);
	return 0;
}

void ast_sip_initialize_resolver(void)
{
	/* Replace the existing PJSIP resolver with our own implementation */
	ast_sip_push_task_synchronous(NULL, sip_replace_resolver, NULL);
}

#else

void ast_sip_initialize_resolver(void)
{
	/* External resolver support does not exist in the version of PJSIP in use */
	ast_log(LOG_NOTICE, "The version of PJSIP in use does not support external resolvers, using PJSIP provided resolver\n");
}

#endif
