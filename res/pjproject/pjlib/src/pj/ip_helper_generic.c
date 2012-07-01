/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pj/ip_helper.h>
#include <pj/addr_resolv.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/string.h>
#include <pj/compat/socket.h>
#include <pj/sock.h>

/* Set to 1 to enable tracing */
#if 0
#   include <pj/log.h>
#   define THIS_FILE	"ip_helper_generic.c"
#   define TRACE_(exp)	PJ_LOG(5,exp)
    static const char *get_os_errmsg(void)
    {
	static char errmsg[PJ_ERR_MSG_SIZE];
	pj_strerror(pj_get_os_error(), errmsg, sizeof(errmsg));
	return errmsg;
    }
    static const char *get_addr(void *addr)
    {
	static char txt[PJ_INET6_ADDRSTRLEN];
	struct sockaddr *ad = (struct sockaddr*)addr;
	if (ad->sa_family != PJ_AF_INET && ad->sa_family != PJ_AF_INET6)
	    return "?";
	return pj_inet_ntop2(ad->sa_family, pj_sockaddr_get_addr(ad), 
			     txt, sizeof(txt));
    }
#else
#   define TRACE_(exp)
#endif


#if 0
    /* dummy */

#elif defined(PJ_HAS_IFADDRS_H) && PJ_HAS_IFADDRS_H != 0 && \
      defined(PJ_HAS_NET_IF_H) && PJ_HAS_NET_IF_H != 0
/* Using getifaddrs() is preferred since it can work with both IPv4 and IPv6 */
static pj_status_t if_enum_by_af(int af,
				 unsigned *p_cnt,
				 pj_sockaddr ifs[])
{
    struct ifaddrs *ifap = NULL, *it;
    unsigned max;

    PJ_ASSERT_RETURN(af==PJ_AF_INET || af==PJ_AF_INET6, PJ_EINVAL);
    
    TRACE_((THIS_FILE, "Starting interface enum with getifaddrs() for af=%d",
	    af));

    if (getifaddrs(&ifap) != 0) {
	TRACE_((THIS_FILE, " getifarrds() failed: %s", get_os_errmsg()));
	return PJ_RETURN_OS_ERROR(pj_get_netos_error());
    }

    it = ifap;
    max = *p_cnt;
    *p_cnt = 0;
    for (; it!=NULL && *p_cnt < max; it = it->ifa_next) {
	struct sockaddr *ad = it->ifa_addr;

	TRACE_((THIS_FILE, " checking %s", it->ifa_name));

	if ((it->ifa_flags & IFF_UP)==0) {
	    TRACE_((THIS_FILE, "  interface is down"));
	    continue; /* Skip when interface is down */
	}

#if PJ_IP_HELPER_IGNORE_LOOPBACK_IF
	if (it->ifa_flags & IFF_LOOPBACK) {
	    TRACE_((THIS_FILE, "  loopback interface"));
	    continue; /* Skip loopback interface */
	}
#endif

	if (ad==NULL) {
	    TRACE_((THIS_FILE, "  NULL address ignored"));
	    continue; /* reported to happen on Linux 2.6.25.9 
			 with ppp interface */
	}

	if (ad->sa_family != af) {
	    TRACE_((THIS_FILE, "  address %s ignored (af=%d)", 
		    get_addr(ad), ad->sa_family));
	    continue; /* Skip when interface is down */
	}

	/* Ignore 0.0.0.0/8 address. This is a special address
	 * which doesn't seem to have practical use.
	 */
	if (af==pj_AF_INET() &&
	    (pj_ntohl(((pj_sockaddr_in*)ad)->sin_addr.s_addr) >> 24) == 0)
	{
	    TRACE_((THIS_FILE, "  address %s ignored (0.0.0.0/8 class)", 
		    get_addr(ad), ad->sa_family));
	    continue;
	}

	TRACE_((THIS_FILE, "  address %s (af=%d) added at index %d", 
		get_addr(ad), ad->sa_family, *p_cnt));

	pj_bzero(&ifs[*p_cnt], sizeof(ifs[0]));
	pj_memcpy(&ifs[*p_cnt], ad, pj_sockaddr_get_len(ad));
	PJ_SOCKADDR_RESET_LEN(&ifs[*p_cnt]);
	(*p_cnt)++;
    }

    freeifaddrs(ifap);
    TRACE_((THIS_FILE, "done, found %d address(es)", *p_cnt));
    return (*p_cnt != 0) ? PJ_SUCCESS : PJ_ENOTFOUND;
}

#elif defined(SIOCGIFCONF) && \
      defined(PJ_HAS_NET_IF_H) && PJ_HAS_NET_IF_H != 0

/* Note: this does not work with IPv6 */
static pj_status_t if_enum_by_af(int af,
				 unsigned *p_cnt,
				 pj_sockaddr ifs[])
{
    pj_sock_t sock;
    char buf[512];
    struct ifconf ifc;
    struct ifreq *ifr;
    int i, count;
    pj_status_t status;

    PJ_ASSERT_RETURN(af==PJ_AF_INET || af==PJ_AF_INET6, PJ_EINVAL);
    
    TRACE_((THIS_FILE, "Starting interface enum with SIOCGIFCONF for af=%d",
	    af));

    status = pj_sock_socket(af, PJ_SOCK_DGRAM, 0, &sock);
    if (status != PJ_SUCCESS)
	return status;

    /* Query available interfaces */
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
	int oserr = pj_get_netos_error();
	TRACE_((THIS_FILE, " ioctl(SIOCGIFCONF) failed: %s", get_os_errmsg()));
	pj_sock_close(sock);
	return PJ_RETURN_OS_ERROR(oserr);
    }

    /* Done with socket */
    pj_sock_close(sock);

    /* Interface interfaces */
    ifr = (struct ifreq*) ifc.ifc_req;
    count = ifc.ifc_len / sizeof(struct ifreq);
    if (count > *p_cnt)
	count = *p_cnt;

    *p_cnt = 0;
    for (i=0; i<count; ++i) {
	struct ifreq *itf = &ifr[i];
	struct sockaddr *ad = &itf->ifr_addr;
	
	TRACE_((THIS_FILE, " checking interface %s", itf->ifr_name));

	/* Skip address with different family */
	if (ad->sa_family != af) {
	    TRACE_((THIS_FILE, "  address %s (af=%d) ignored",
		    get_addr(ad), (int)ad->sa_family));
	    continue;
	}

	if ((itf->ifr_flags & IFF_UP)==0) {
	    TRACE_((THIS_FILE, "  interface is down"));
	    continue; /* Skip when interface is down */
	}

#if PJ_IP_HELPER_IGNORE_LOOPBACK_IF
	if (itf->ifr_flags & IFF_LOOPBACK) {
	    TRACE_((THIS_FILE, "  loopback interface"));
	    continue; /* Skip loopback interface */
	}
#endif

	/* Ignore 0.0.0.0/8 address. This is a special address
	 * which doesn't seem to have practical use.
	 */
	if (af==pj_AF_INET() &&
	    (pj_ntohl(((pj_sockaddr_in*)ad)->sin_addr.s_addr) >> 24) == 0)
	{
	    TRACE_((THIS_FILE, "  address %s ignored (0.0.0.0/8 class)", 
		    get_addr(ad), ad->sa_family));
	    continue;
	}

	TRACE_((THIS_FILE, "  address %s (af=%d) added at index %d", 
		get_addr(ad), ad->sa_family, *p_cnt));

	pj_bzero(&ifs[*p_cnt], sizeof(ifs[0]));
	pj_memcpy(&ifs[*p_cnt], ad, pj_sockaddr_get_len(ad));
	PJ_SOCKADDR_RESET_LEN(&ifs[*p_cnt]);
	(*p_cnt)++;
    }

    TRACE_((THIS_FILE, "done, found %d address(es)", *p_cnt));
    return (*p_cnt != 0) ? PJ_SUCCESS : PJ_ENOTFOUND;
}

#elif defined(PJ_HAS_NET_IF_H) && PJ_HAS_NET_IF_H != 0
/* Note: this does not work with IPv6 */
static pj_status_t if_enum_by_af(int af, unsigned *p_cnt, pj_sockaddr ifs[])
{
    struct if_nameindex *if_list;
    struct ifreq ifreq;
    pj_sock_t sock;
    unsigned i, max_count;
    pj_status_t status;

    PJ_ASSERT_RETURN(af==PJ_AF_INET || af==PJ_AF_INET6, PJ_EINVAL);

    TRACE_((THIS_FILE, "Starting if_nameindex() for af=%d", af));

    status = pj_sock_socket(af, PJ_SOCK_DGRAM, 0, &sock);
    if (status != PJ_SUCCESS)
	return status;

    if_list = if_nameindex();
    if (if_list == NULL)
	return PJ_ENOTFOUND;

    max_count = *p_cnt;
    *p_cnt = 0;
    for (i=0; if_list[i].if_index && *p_cnt<max_count; ++i) {
	struct sockaddr *ad;
	int rc;

	strncpy(ifreq.ifr_name, if_list[i].if_name, IFNAMSIZ);

	TRACE_((THIS_FILE, " checking interface %s", ifreq.ifr_name));

	if ((rc=ioctl(sock, SIOCGIFFLAGS, &ifreq)) != 0) {
	    TRACE_((THIS_FILE, "  ioctl(SIOCGIFFLAGS) failed: %s",
		    get_os_errmsg()));
	    continue;	/* Failed to get flags, continue */
	}

	if ((ifreq.ifr_flags & IFF_UP)==0) {
	    TRACE_((THIS_FILE, "  interface is down"));
	    continue; /* Skip when interface is down */
	}

#if PJ_IP_HELPER_IGNORE_LOOPBACK_IF
	if (ifreq.ifr_flags & IFF_LOOPBACK) {
	    TRACE_((THIS_FILE, "  loopback interface"));
	    continue; /* Skip loopback interface */
	}
#endif

	/* Note: SIOCGIFADDR does not work for IPv6! */
	if ((rc=ioctl(sock, SIOCGIFADDR, &ifreq)) != 0) {
	    TRACE_((THIS_FILE, "  ioctl(SIOCGIFADDR) failed: %s",
		    get_os_errmsg()));
	    continue;	/* Failed to get address, continue */
	}

	ad = (struct sockaddr*) &ifreq.ifr_addr;

	if (ad->sa_family != af) {
	    TRACE_((THIS_FILE, "  address %s family %d ignored", 
			       get_addr(&ifreq.ifr_addr),
			       ifreq.ifr_addr.sa_family));
	    continue;	/* Not address family that we want, continue */
	}

	/* Ignore 0.0.0.0/8 address. This is a special address
	 * which doesn't seem to have practical use.
	 */
	if (af==pj_AF_INET() &&
	    (pj_ntohl(((pj_sockaddr_in*)ad)->sin_addr.s_addr) >> 24) == 0)
	{
	    TRACE_((THIS_FILE, "  address %s ignored (0.0.0.0/8 class)", 
		    get_addr(ad), ad->sa_family));
	    continue;
	}

	/* Got an address ! */
	TRACE_((THIS_FILE, "  address %s (af=%d) added at index %d", 
		get_addr(ad), ad->sa_family, *p_cnt));

	pj_bzero(&ifs[*p_cnt], sizeof(ifs[0]));
	pj_memcpy(&ifs[*p_cnt], ad, pj_sockaddr_get_len(ad));
	PJ_SOCKADDR_RESET_LEN(&ifs[*p_cnt]);
	(*p_cnt)++;
    }

    if_freenameindex(if_list);
    pj_sock_close(sock);

    TRACE_((THIS_FILE, "done, found %d address(es)", *p_cnt));
    return (*p_cnt != 0) ? PJ_SUCCESS : PJ_ENOTFOUND;
}

#else
static pj_status_t if_enum_by_af(int af,
				 unsigned *p_cnt,
				 pj_sockaddr ifs[])
{
    pj_status_t status;

    PJ_ASSERT_RETURN(p_cnt && *p_cnt > 0 && ifs, PJ_EINVAL);

    pj_bzero(ifs, sizeof(ifs[0]) * (*p_cnt));

    /* Just get one default route */
    status = pj_getdefaultipinterface(af, &ifs[0]);
    if (status != PJ_SUCCESS)
	return status;

    *p_cnt = 1;
    return PJ_SUCCESS;
}
#endif /* SIOCGIFCONF */

/*
 * Enumerate the local IP interface currently active in the host.
 */
PJ_DEF(pj_status_t) pj_enum_ip_interface(int af,
					 unsigned *p_cnt,
					 pj_sockaddr ifs[])
{
    unsigned start;
    pj_status_t status;

    start = 0;
    if (af==PJ_AF_INET6 || af==PJ_AF_UNSPEC) {
	unsigned max = *p_cnt;
	status = if_enum_by_af(PJ_AF_INET6, &max, &ifs[start]);
	if (status == PJ_SUCCESS) {
	    start += max;
	    (*p_cnt) -= max;
	}
    }

    if (af==PJ_AF_INET || af==PJ_AF_UNSPEC) {
	unsigned max = *p_cnt;
	status = if_enum_by_af(PJ_AF_INET, &max, &ifs[start]);
	if (status == PJ_SUCCESS) {
	    start += max;
	    (*p_cnt) -= max;
	}
    }

    *p_cnt = start;

    return (*p_cnt != 0) ? PJ_SUCCESS : PJ_ENOTFOUND;
}

/*
 * Enumerate the IP routing table for this host.
 */
PJ_DEF(pj_status_t) pj_enum_ip_route(unsigned *p_cnt,
				     pj_ip_route_entry routes[])
{
    pj_sockaddr itf;
    pj_status_t status;

    PJ_ASSERT_RETURN(p_cnt && *p_cnt > 0 && routes, PJ_EINVAL);

    pj_bzero(routes, sizeof(routes[0]) * (*p_cnt));

    /* Just get one default route */
    status = pj_getdefaultipinterface(PJ_AF_INET, &itf);
    if (status != PJ_SUCCESS)
	return status;
    
    routes[0].ipv4.if_addr.s_addr = itf.ipv4.sin_addr.s_addr;
    routes[0].ipv4.dst_addr.s_addr = 0;
    routes[0].ipv4.mask.s_addr = 0;
    *p_cnt = 1;

    return PJ_SUCCESS;
}

