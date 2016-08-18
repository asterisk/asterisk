#!/usr/bin/python

import optparse
import astdicts
import astconfigparser
import socket
import re

PREFIX = 'pjsip_'

###############################################################################
### some utility functions
###############################################################################


def section_by_type(section, pjsip, type):
    """Finds a section based upon the given type, adding it if not found."""
    def __find_dict(mdicts, key, val):
        """Given a list of mult-dicts, return the multi-dict that contains
           the given key/value pair."""

        def found(d):
            return key in d and val in d[key]

        try:
            return [d for d in mdicts if found(d)][0]
        except IndexError:
            raise LookupError("Dictionary not located for key = %s, value = %s"
                              % (key, val))

    try:
        return __find_dict(pjsip.section(section), 'type', type)
    except LookupError:
        # section for type doesn't exist, so add
        sect = pjsip.add_section(section)
        sect['type'] = type
        return sect


def set_value(key=None, val=None, section=None, pjsip=None,
              nmapped=None, type='endpoint'):
    """Sets the key to the value within the section in pjsip.conf"""
    def _set_value(k, v, s, r, n):
        set_value(key if key else k, v, s, r, n, type)

    # if no value or section return the set_value
    # function with the enclosed key and type
    if not val and not section:
        return _set_value

    # otherwise try to set the value
    section_by_type(section, pjsip, type)[key] = \
        val[0] if isinstance(val, list) else val


def merge_value(key=None, val=None, section=None, pjsip=None,
                nmapped=None, type='endpoint', section_to=None):
    """Merge values from the given section with those from the default."""
    def _merge_value(k, v, s, r, n):
        merge_value(key if key else k, v, s, r, n, type, section_to)

    # if no value or section return the merge_value
    # function with the enclosed key and type
    if not val and not section:
        return _merge_value

    # should return a single value section list
    try:
        sect = sip.section(section)[0]
    except LookupError:
        sect = sip.default(section)[0]
    # for each merged value add it to pjsip.conf
    for i in sect.get_merged(key):
        set_value(key, i, section_to if section_to else section,
                  pjsip, nmapped, type)


def non_mapped(nmapped):
    """Write non-mapped sip.conf values to the non-mapped object"""
    def _non_mapped(section, key, val):
        """Writes a non-mapped value from sip.conf to the non-mapped object."""
        if section not in nmapped:
            nmapped[section] = astconfigparser.Section()
            if isinstance(val, list):
                for v in val:
                    # since coming from sip.conf we can assume
                    # single section lists
                    nmapped[section][0][key] = v
            else:
                nmapped[section][0][key] = val
    return _non_mapped

###############################################################################
### mapping functions -
###      define f(key, val, section) where key/val are the key/value pair to
###      write to given section in pjsip.conf
###############################################################################


def set_dtmfmode(key, val, section, pjsip, nmapped):
    """
    Sets the dtmfmode value.  If value matches allowable option in pjsip
    then map it, otherwise set it to none.
    """
    key = 'dtmf_mode'
    # available pjsip.conf values: rfc4733, inband, info, none
    if val == 'inband' or val == 'info':
        set_value(key, val, section, pjsip, nmapped)
    elif val == 'rfc2833':
        set_value(key, 'rfc4733', section, pjsip, nmapped)
    else:
        nmapped(section, key, val + " ; did not fully map - set to none")
        set_value(key, 'none', section, pjsip, nmapped)


def from_nat(key, val, section, pjsip, nmapped):
    """Sets values from nat into the appropriate pjsip.conf options."""
    # nat from sip.conf can be comma separated list of values:
    # yes/no, [auto_]force_rport, [auto_]comedia
    if 'yes' in val:
        set_value('rtp_symmetric', 'yes', section, pjsip, nmapped)
        set_value('rewrite_contact', 'yes', section, pjsip, nmapped)
    if 'comedia' in val:
        set_value('rtp_symmetric', 'yes', section, pjsip, nmapped)
    if 'force_rport' in val:
        set_value('force_rport', 'yes', section, pjsip, nmapped)
        set_value('rewrite_contact', 'yes', section, pjsip, nmapped)


def set_timers(key, val, section, pjsip, nmapped):
    """
    Sets the timers in pjsip.conf from the session-timers option
    found in sip.conf.
    """
    # pjsip.conf values can be yes/no, required, always
    if val == 'originate':
        set_value('timers', 'always', section, pjsip, nmapped)
    elif val == 'accept':
        set_value('timers', 'required', section, pjsip, nmapped)
    elif val == 'never':
        set_value('timers', 'no', section, pjsip, nmapped)
    else:
        set_value('timers', 'yes', section, pjsip, nmapped)


def set_direct_media(key, val, section, pjsip, nmapped):
    """
    Maps values from the sip.conf comma separated direct_media option
    into pjsip.conf direct_media options.
    """
    if 'yes' in val:
        set_value('direct_media', 'yes', section, pjsip, nmapped)
    if 'update' in val:
        set_value('direct_media_method', 'update', section, pjsip, nmapped)
    if 'outgoing' in val:
        set_value('directed_media_glare_mitigation', 'outgoing', section,
                  pjsip, nmapped)
    if 'nonat' in val:
        set_value('disable_directed_media_on_nat', 'yes', section, pjsip,
                  nmapped)
    if 'no' in val:
        set_value('direct_media', 'no', section, pjsip, nmapped)


def from_sendrpid(key, val, section, pjsip, nmapped):
    """Sets the send_rpid/pai values in pjsip.conf."""
    if val == 'yes' or val == 'rpid':
        set_value('send_rpid', 'yes', section, pjsip, nmapped)
    elif val == 'pai':
        set_value('send_pai', 'yes', section, pjsip, nmapped)


def set_media_encryption(key, val, section, pjsip, nmapped):
    """Sets the media_encryption value in pjsip.conf"""
    try:
        dtls = sip.get(section, 'dtlsenable')[0]
        if dtls == 'yes':
            # If DTLS is enabled, then that overrides SDES encryption.
            return
    except LookupError:
        pass

    if val == 'yes':
        set_value('media_encryption', 'sdes', section, pjsip, nmapped)


def from_recordfeature(key, val, section, pjsip, nmapped):
    """
    If record on/off feature is set to automixmon then set
    one_touch_recording, otherwise it can't be mapped.
    """
    set_value('one_touch_recording', 'yes', section, pjsip, nmapped)
    set_value(key, val, section, pjsip, nmapped)

def set_record_on_feature(key, val, section, pjsip, nmapped):
    """Sets the record_on_feature in pjsip.conf"""
    from_recordfeature('record_on_feature', val, section, pjsip, nmapped)

def set_record_off_feature(key, val, section, pjsip, nmapped):
    """Sets the record_off_feature in pjsip.conf"""
    from_recordfeature('record_off_feature', val, section, pjsip, nmapped)

def from_progressinband(key, val, section, pjsip, nmapped):
    """Sets the inband_progress value in pjsip.conf"""
    # progressinband can = yes/no/never
    if val == 'never':
        val = 'no'
    set_value('inband_progress', val, section, pjsip, nmapped)


def build_host(config, host, section, port_key):
    """
    Returns a string composed of a host:port. This assumes that the host
    may have a port as part of the initial value. The port_key is only used
    if the host does not already have a port set on it.
    Throws a LookupError if the key does not exist
    """
    port = None

    try:
        socket.inet_pton(socket.AF_INET6, host)
        if not host.startswith('['):
            # SIP URI will need brackets.
            host = '[' + host + ']'
        else:
            # If brackets are present, there may be a port as well
            port = re.match('\[.*\]:(\d+)', host)
    except socket.error:
        # No biggie. It's just not an IPv6 address
        port = re.match('.*:(\d+)', host)

    result = host

    if not port:
        try:
            port = config.get(section, port_key)[0]
            result += ':' + port
        except LookupError:
            pass

    return result


def from_host(key, val, section, pjsip, nmapped):
    """
    Sets contact info in an AOR section in pjsip.conf using 'host'
    and 'port' data from sip.conf
    """
    # all aors have the same name as the endpoint so makes
    # it easy to set endpoint's 'aors' value
    set_value('aors', section, section, pjsip, nmapped)
    if val == 'dynamic':
        # Easy case. Just set the max_contacts on the aor and we're done
        set_value('max_contacts', 1, section, pjsip, nmapped, 'aor')
        return

    result = 'sip:'

    # More difficult case. The host will be either a hostname or
    # IP address and may or may not have a port specified. pjsip.conf
    # expects the contact to be a SIP URI.

    user = None

    try:
        user = sip.multi_get(section, ['defaultuser', 'username'])[0]
        result += user + '@'
    except LookupError:
        # It's fine if there's no user name
        pass

    result += build_host(sip, val, section, 'port')

    set_value('contact', result, section, pjsip, nmapped, 'aor')


def from_mailbox(key, val, section, pjsip, nmapped):
    """
    Determines whether a mailbox configured in sip.conf should map to
    an endpoint or aor in pjsip.conf. If subscribemwi is true, then the
    mailboxes are set on an aor. Otherwise the mailboxes are set on the
    endpoint.
    """

    try:
        subscribemwi = sip.get(section, 'subscribemwi')[0]
    except LookupError:
        # No subscribemwi option means default it to 'no'
        subscribemwi = 'no'

    set_value('mailboxes', val, section, pjsip, nmapped, 'aor'
              if subscribemwi == 'yes' else 'endpoint')


def setup_auth(key, val, section, pjsip, nmapped):
    """
    Sets up authentication information for a specific endpoint based on the
    'secret' setting on a peer in sip.conf
    """
    set_value('username', section, section, pjsip, nmapped, 'auth')
    # In chan_sip, if a secret and an md5secret are both specified on a peer,
    # then in practice, only the md5secret is used. If both are encountered
    # then we build an auth section that has both an md5_cred and password.
    # However, the auth_type will indicate to authenticators to use the
    # md5_cred, so like with sip.conf, the password will be there but have
    # no purpose.
    if key == 'secret':
        set_value('password', val, section, pjsip, nmapped, 'auth')
    else:
        set_value('md5_cred', val, section, pjsip, nmapped, 'auth')
        set_value('auth_type', 'md5', section, pjsip, nmapped, 'auth')

    realms = [section]
    try:
        auths = sip.get('authentication', 'auth')
        for i in auths:
            user, at, realm = i.partition('@')
            realms.append(realm)
    except LookupError:
        pass

    realm_str = ','.join(realms)

    set_value('auth', section, section, pjsip, nmapped)
    set_value('outbound_auth', realm_str, section, pjsip, nmapped)


def setup_ident(key, val, section, pjsip, nmapped):
    """
    Examines the 'type' field for a sip.conf peer and creates an identify
    section if the type is either 'peer' or 'friend'. The identify section uses
    either the host or defaultip field of the sip.conf peer.
    """
    if val != 'peer' and val != 'friend':
        return

    try:
        ip = sip.get(section, 'host')[0]
    except LookupError:
        return

    if ip == 'dynamic':
        try:
            ip = sip.get(section, 'defaultip')[0]
        except LookupError:
            return

    set_value('endpoint', section, section, pjsip, nmapped, 'identify')
    set_value('match', ip, section, pjsip, nmapped, 'identify')


def from_encryption_taglen(key, val, section, pjsip, nmapped):
    """Sets the srtp_tag32 option based on sip.conf encryption_taglen"""
    if val == '32':
        set_value('srtp_tag_32', 'yes', section, pjsip, nmapped)


def from_dtlsenable(key, val, section, pjsip, nmapped):
    """Optionally sets media_encryption=dtls based on sip.conf dtlsenable"""
    if val == 'yes':
        set_value('media_encryption', 'dtls', section, pjsip, nmapped)

###############################################################################

# options in pjsip.conf on an endpoint that have no sip.conf equivalent:
# type, rtp_ipv6, 100rel, trust_id_outbound, aggregate_mwi,
# connected_line_method

# known sip.conf peer keys that can be mapped to a pjsip.conf section/key
peer_map = [
    # sip.conf option      mapping function     pjsip.conf option(s)
    ###########################################################################
    ['context',            set_value],
    ['dtmfmode',           set_dtmfmode],
    ['disallow',           merge_value],
    ['allow',              merge_value],
    ['nat',                from_nat],            # rtp_symmetric, force_rport,
                                                 # rewrite_contact
    ['icesupport',         set_value('ice_support')],
    ['autoframing',        set_value('use_ptime')],
    ['outboundproxy',      set_value('outbound_proxy')],
    ['mohsuggest',         set_value('moh_suggest')],
    ['session-timers',     set_timers],          # timers
    ['session-minse',      set_value('timers_min_se')],
    ['session-expires',    set_value('timers_sess_expires')],
    ['externip',           set_value('external_media_address')],
    ['externhost',         set_value('external_media_address')],
    # identify_by ?
    ['directmedia',        set_direct_media],    # direct_media
                                                 # direct_media_method
                                                 # directed_media_glare_mitigation
                                                 # disable_directed_media_on_nat
    ['callerid',           set_value],           # callerid
    ['callingpres',        set_value('callerid_privacy')],
    ['cid_tag',            set_value('callerid_tag')],
    ['trustpid',           set_value('trust_id_inbound')],
    ['sendrpid',           from_sendrpid],       # send_pai, send_rpid
    ['send_diversion',     set_value],
    ['encrpytion',         set_media_encryption],
    ['avpf',               set_value('use_avpf')],
    ['recordonfeature',    set_record_on_feature],  # automixon
    ['recordofffeature',   set_record_off_feature],  # automixon
    ['progressinband',     from_progressinband], # in_band_progress
    ['callgroup',          set_value('call_group')],
    ['pickupgroup',        set_value('pickup_group')],
    ['namedcallgroup',     set_value('named_call_group')],
    ['namedpickupgroup',   set_value('named_pickup_group')],
    ['allowtransfer',      set_value('allow_transfer')],
    ['fromuser',           set_value('from_user')],
    ['fromdomain',         set_value('from_domain')],
    ['mwifrom',            set_value('mwi_from_user')],
    ['tos_audio',          set_value],
    ['tos_video',          set_value],
    ['cos_audio',          set_value],
    ['cos_video',          set_value],
    ['sdpowner',           set_value('sdp_owner')],
    ['sdpsession',         set_value('sdp_session')],
    ['tonezone',           set_value('tone_zone')],
    ['language',           set_value],
    ['allowsubscribe',     set_value('allow_subscribe')],
    ['subminexpiry',       set_value('sub_min_expiry')],
    ['rtp_engine',         set_value],
    ['mailbox',            from_mailbox],
    ['busylevel',          set_value('device_state_busy_at')],
    ['secret',             setup_auth],
    ['md5secret',          setup_auth],
    ['type',               setup_ident],
    ['dtlsenable',         from_dtlsenable],
    ['dtlsverify',         set_value('dtls_verify')],
    ['dtlsrekey',          set_value('dtls_rekey')],
    ['dtlscertfile',       set_value('dtls_cert_file')],
    ['dtlsprivatekey',     set_value('dtls_private_key')],
    ['dtlscipher',         set_value('dtls_cipher')],
    ['dtlscafile',         set_value('dtls_ca_file')],
    ['dtlscapath',         set_value('dtls_ca_path')],
    ['dtlssetup',          set_value('dtls_setup')],
    ['encryption_taglen',  from_encryption_taglen],

############################ maps to an aor ###################################

    ['host',               from_host],           # contact, max_contacts
    ['qualifyfreq',        set_value('qualify_frequency', type='aor')],

############################# maps to auth#####################################
#        type = auth
#        username
#        password
#        md5_cred
#        realm
#        nonce_lifetime
#        auth_type
######################### maps to acl/security ################################

    ['permit',             merge_value(type='acl', section_to='acl')],
    ['deny',               merge_value(type='acl', section_to='acl')],
    ['acl',                merge_value(type='acl', section_to='acl')],
    ['contactpermit',      merge_value('contact_permit', type='acl', section_to='acl')],
    ['contactdeny',        merge_value('contact_deny', type='acl', section_to='acl')],
    ['contactacl',         merge_value('contact_acl', type='acl', section_to='acl')],

########################### maps to transport #################################
#        type = transport
#        protocol
#        bind
#        async_operations
#        ca_list_file
#        cert_file
#        privkey_file
#        password
#        external_signaling_address - externip & externhost
#        external_signaling_port
#        external_media_address
#        domain
#        verify_server
#        verify_client
#        require_client_cert
#        method
#        cipher
#        localnet
######################### maps to domain_alias ################################
#        type = domain_alias
#        domain
######################### maps to registration ################################
#        type = registration
#        server_uri
#        client_uri
#        contact_user
#        transport
#        outbound_proxy
#        expiration
#        retry_interval
#        max_retries
#        auth_rejection_permanent
#        outbound_auth
########################### maps to identify ##################################
#        type = identify
#        endpoint
#        match
]


def add_localnet(section, pjsip, nmapped):
    """
    Adds localnet values from sip.conf's general section to a transport in
    pjsip.conf. Ideally, we would have just created a template with the
    localnet sections, but because this is a script, it's not hard to add
    the same thing on to every transport.
    """
    try:
        merge_value('local_net', sip.get('general', 'localnet')[0], 'general',
                    pjsip, nmapped, 'transport', section)
    except LookupError:
        # No localnet options configured. No biggie!
        pass


def set_transport_common(section, pjsip, nmapped):
    """
    sip.conf has several global settings that in pjsip.conf apply to individual
    transports. This function adds these global settings to each individual
    transport.

    The settings included are:
    localnet
    tos_sip
    cos_sip
    """

    try:
        merge_value('local_net', sip.get('general', 'localnet')[0], 'general',
                    pjsip, nmapped, 'transport', section)
    except LookupError:
        # No localnet options configured. Move on.
        pass

    try:
        set_value('tos', sip.get('general', 'sip_tos')[0], 'general', pjsip,
                  nmapped, 'transport', section)
    except LookupError:
        pass

    try:
        set_value('cos', sip.get('general', 'sip_cos')[0], 'general', pjsip,
                  nmapped, 'transport', section)
    except LookupError:
        pass


def split_hostport(addr):
    """
    Given an address in the form 'addr:port' separate the addr and port
    components.
    Returns a two-tuple of strings, (addr, port). If no port is present in the
    string, then the port section of the tuple is None.
    """
    try:
        socket.inet_pton(socket.AF_INET6, addr)
        if not addr.startswith('['):
            return (addr, None)
        else:
            # If brackets are present, there may be a port as well
            match = re.match('\[(.*\)]:(\d+)', addr)
            if match:
                return (match.group(1), match.group(2))
            else:
                return (addr, None)
    except socket.error:
        pass

    # IPv4 address or hostname
    host, sep, port = addr.rpartition(':')

    if not sep and not port:
        return (host, None)
    else:
        return (host, port)


def create_udp(sip, pjsip, nmapped):
    """
    Creates a 'transport-udp' section in the pjsip.conf file based
    on the following settings from sip.conf:

    bindaddr (or udpbindaddr)
    bindport
    externaddr (or externip)
    externhost
    """

    try:
        bind = sip.multi_get('general', ['udpbindaddr', 'bindaddr'])[0]
    except LookupError:
        bind = ''

    bind = build_host(sip, bind, 'general', 'bindport')

    try:
        extern_addr = sip.multi_get('general', ['externaddr', 'externip',
                                    'externhost'])[0]
        host, port = split_hostport(extern_addr)
        set_value('external_signaling_address', host, 'transport-udp', pjsip,
                  nmapped, 'transport')
        if port:
            set_value('external_signaling_port', port, 'transport-udp', pjsip,
                      nmapped, 'transport')
    except LookupError:
        pass

    set_value('protocol', 'udp', 'transport-udp', pjsip, nmapped, 'transport')
    set_value('bind', bind, 'transport-udp', pjsip, nmapped, 'transport')
    set_transport_common('transport-udp', pjsip, nmapped)


def create_tcp(sip, pjsip, nmapped):
    """
    Creates a 'transport-tcp' section in the pjsip.conf file based
    on the following settings from sip.conf:

    tcpenable
    tcpbindaddr
    externtcpport
    """

    try:
        enabled = sip.get('general', 'tcpenable')[0]
    except:
        # No value means disabled by default. No need for a tranport
        return

    if enabled == 'no':
        return

    try:
        bind = sip.get('general', 'tcpbindaddr')[0]
        bind = build_host(sip, bind, 'general', 'bindport')
    except LookupError:
        # No tcpbindaddr means to default to the udpbindaddr
        bind = pjsip.get('transport-udp', 'bind')[0]

    try:
        extern_addr = sip.multi_get('general', ['externaddr', 'externip',
                                    'externhost'])[0]
        host, port = split_hostport(extern_addr)
        try:
            tcpport = sip.get('general', 'externtcpport')[0]
        except:
            tcpport = port
        set_value('external_signaling_address', host, 'transport-tcp', pjsip,
                  nmapped, 'transport')
        if tcpport:
            set_value('external_signaling_port', tcpport, 'transport-tcp',
                      pjsip, nmapped, 'transport')
    except LookupError:
        pass

    set_value('protocol', 'tcp', 'transport-tcp', pjsip, nmapped, 'transport')
    set_value('bind', bind, 'transport-tcp', pjsip, nmapped, 'transport')
    set_transport_common('transport-tcp', pjsip, nmapped)


def set_tls_bindaddr(val, pjsip, nmapped):
    """
    Creates the TCP bind address. This has two possible methods of
    working:
    Use the 'tlsbindaddr' option from sip.conf directly if it has both
    an address and port. If no port is present, use 5061
    If there is no 'tlsbindaddr' option present in sip.conf, use the
    previously-established UDP bind address and port 5061
    """
    try:
        bind = sip.get('general', 'tlsbindaddr')[0]
        explicit = True
    except LookupError:
        # No tlsbindaddr means to default to the bindaddr but with standard TLS
        # port
        bind = pjsip.get('transport-udp', 'bind')[0]
        explicit = False

    matchv4 = re.match('\d+\.\d+\.\d+\.\d+:\d+', bind)
    matchv6 = re.match('\[.*\]:d+', bind)
    if matchv4 or matchv6:
        if explicit:
            # They provided a port. We'll just use it.
            set_value('bind', bind, 'transport-tls', pjsip, nmapped,
                      'transport')
            return
        else:
            # Need to strip the port from the UDP address
            index = bind.rfind(':')
            bind = bind[:index]

    # Reaching this point means either there was no port provided or we
    # stripped the port off. We need to add on the default 5061 port

    bind += ':5061'

    set_value('bind', bind, 'transport-tls', pjsip, nmapped, 'transport')


def set_tls_private_key(val, pjsip, nmapped):
    """Sets privkey_file based on sip.conf tlsprivatekey or sslprivatekey"""
    set_value('priv_key_file', val, 'transport-tls', pjsip, nmapped,
              'transport')


def set_tls_cipher(val, pjsip, nmapped):
    """Sets cipher based on sip.conf tlscipher or sslcipher"""
    set_value('cipher', val, 'transport-tls', pjsip, nmapped, 'transport')


def set_tls_cafile(val, pjsip, nmapped):
    """Sets ca_list_file based on sip.conf tlscafile"""
    set_value('ca_list_file', val, 'transport-tls', pjsip, nmapped,
              'transport')


def set_tls_verifyclient(val, pjsip, nmapped):
    """Sets verify_client based on sip.conf tlsverifyclient"""
    set_value('verify_client', val, 'transport-tls', pjsip, nmapped,
              'transport')


def set_tls_verifyserver(val, pjsip, nmapped):
    """Sets verify_server based on sip.conf tlsdontverifyserver"""

    if val == 'no':
        set_value('verify_server', 'yes', 'transport-tls', pjsip, nmapped,
                  'transport')
    else:
        set_value('verify_server', 'no', 'transport-tls', pjsip, nmapped,
                  'transport')


def set_tls_method(val, pjsip, nmapped):
    """Sets method based on sip.conf tlsclientmethod or sslclientmethod"""
    set_value('method', val, 'transport-tls', pjsip, nmapped, 'transport')


def create_tls(sip, pjsip, nmapped):
    """
    Creates a 'transport-tls' section in pjsip.conf based on the following
    settings from sip.conf:

    tlsenable (or sslenable)
    tlsbindaddr (or sslbindaddr)
    tlsprivatekey (or sslprivatekey)
    tlscipher (or sslcipher)
    tlscafile
    tlscapath (or tlscadir)
    tlscertfile (or sslcert or tlscert)
    tlsverifyclient
    tlsdontverifyserver
    tlsclientmethod (or sslclientmethod)
    """

    tls_map = [
        (['tlsbindaddr', 'sslbindaddr'], set_tls_bindaddr),
        (['tlsprivatekey', 'sslprivatekey'], set_tls_private_key),
        (['tlscipher', 'sslcipher'], set_tls_cipher),
        (['tlscafile'], set_tls_cafile),
        (['tlsverifyclient'], set_tls_verifyclient),
        (['tlsdontverifyserver'], set_tls_verifyserver),
        (['tlsclientmethod', 'sslclientmethod'], set_tls_method)
    ]

    try:
        enabled = sip.multi_get('general', ['tlsenable', 'sslenable'])[0]
    except LookupError:
        # Not enabled. Don't create a transport
        return

    if enabled == 'no':
        return

    set_value('protocol', 'tls', 'transport-tls', pjsip, nmapped, 'transport')

    for i in tls_map:
        try:
            i[1](sip.multi_get('general', i[0])[0], pjsip, nmapped)
        except LookupError:
            pass

    set_transport_common('transport-tls', pjsip, nmapped)
    try:
        extern_addr = sip.multi_get('general', ['externaddr', 'externip',
                                    'externhost'])[0]
        host, port = split_hostport(extern_addr)
        try:
            tlsport = sip.get('general', 'externtlsport')[0]
        except:
            tlsport = port
        set_value('external_signaling_address', host, 'transport-tls', pjsip,
                  nmapped, 'transport')
        if tlsport:
            set_value('external_signaling_port', tlsport, 'transport-tls',
                      pjsip, nmapped, 'transport')
    except LookupError:
        pass


def map_transports(sip, pjsip, nmapped):
    """
    Finds options in sip.conf general section pertaining to
    transport configuration and creates appropriate transport
    configuration sections in pjsip.conf.

    sip.conf only allows a single UDP transport, TCP transport,
    and TLS transport. As such, the mapping into PJSIP can be made
    consistent by defining three sections:

    transport-udp
    transport-tcp
    transport-tls

    To accommodate the default behaviors in sip.conf, we'll need to
    create the UDP transport first, followed by the TCP and TLS transports.
    """

    # First create a UDP transport. Even if no bind parameters were provided
    # in sip.conf, chan_sip would always bind to UDP 0.0.0.0:5060
    create_udp(sip, pjsip, nmapped)

    # TCP settings may be dependent on UDP settings, so do it second.
    create_tcp(sip, pjsip, nmapped)
    create_tls(sip, pjsip, nmapped)


def map_auth(sip, pjsip, nmapped):
    """
    Creates auth sections based on entries in the authentication section of
    sip.conf. pjsip.conf section names consist of "auth_" followed by the name
    of the realm.
    """
    try:
        auths = sip.get('authentication', 'auth')
    except LookupError:
        return

    for i in auths:
        creds, at, realm = i.partition('@')
        if not at and not realm:
            # Invalid. Move on
            continue
        user, colon, secret = creds.partition(':')
        if not secret:
            user, sharp, md5 = creds.partition('#')
            if not md5:
                #Invalid. move on
                continue
        section = "auth_" + realm

        set_value('realm', realm, section, pjsip, nmapped, 'auth')
        set_value('username', user, section, pjsip, nmapped, 'auth')
        if secret:
            set_value('password', secret, section, pjsip, nmapped, 'auth')
        else:
            set_value('md5_cred', md5, section, pjsip, nmapped, 'auth')
            set_value('auth_type', 'md5', section, pjsip, nmapped, 'auth')


class Registration:
    """
    Class for parsing and storing information in a register line in sip.conf.
    """
    def __init__(self, line, retry_interval, max_attempts, outbound_proxy):
        self.retry_interval = retry_interval
        self.max_attempts = max_attempts
        self.outbound_proxy = outbound_proxy
        self.parse(line)

    def parse(self, line):
        """
        Initial parsing routine for register lines in sip.conf.

        This splits the line into the part before the host, and the part
        after the '@' symbol. These two parts are then passed to their
        own parsing routines
        """

        # register =>
        # [peer?][transport://]user[@domain][:secret[:authuser]]@host[:port][/extension][~expiry]

        prehost, at, host_part = line.rpartition('@')
        if not prehost:
            raise

        self.parse_host_part(host_part)
        self.parse_user_part(prehost)

    def parse_host_part(self, host_part):
        """
        Parsing routine for the part after the final '@' in a register line.
        The strategy is to use partition calls to peel away the data starting
        from the right and working to the left.
        """
        pre_expiry, sep, expiry = host_part.partition('~')
        pre_extension, sep, self.extension = pre_expiry.partition('/')
        self.host, sep, self.port = pre_extension.partition(':')

        self.expiry = expiry if expiry else '120'

    def parse_user_part(self, user_part):
        """
        Parsing routine for the part before the final '@' in a register line.
        The only mandatory part of this line is the user portion. The strategy
        here is to start by using partition calls to remove everything to
        the right of the user, then finish by using rpartition calls to remove
        everything to the left of the user.
        """
        colons = user_part.count(':')
        if (colons == 3):
            # :domainport:secret:authuser
            pre_auth, sep, port_auth = user_part.partition(':')
            self.domainport, sep, auth = port_auth.partition(':')
            self.secret, sep, self.authuser = auth.partition(':')
        elif (colons == 2):
            # :secret:authuser
            pre_auth, sep, auth = user_part.partition(':')
            self.secret, sep, self.authuser = auth.partition(':')
        elif (colons == 1):
            # :secret
            pre_auth, sep, self.secret = user_part.partition(':')
        elif (colons == 0):
            # No port, secret, or authuser
            pre_auth = user_part
        else:
            # Invalid setting
            raise

        pre_domain, sep, self.domain = pre_auth.partition('@')
        self.peer, sep, post_peer = pre_domain.rpartition('?')
        transport, sep, self.user = post_peer.rpartition('://')

        self.protocol = transport if transport else 'udp'

    def write(self, pjsip, nmapped):
        """
        Write parsed registration data into a section in pjsip.conf

        Most of the data in self will get written to a registration section.
        However, there will also need to be an auth section created if a
        secret or authuser is present.

        General mapping of values:
        A combination of self.host and self.port is server_uri
        A combination of self.user, self.domain, and self.domainport is
          client_uri
        self.expiry is expiration
        self.extension is contact_user
        self.protocol will map to one of the mapped transports
        self.secret and self.authuser will result in a new auth section, and
          outbound_auth will point to that section.
        XXX self.peer really doesn't map to anything :(
        """

        section = 'reg_' + self.host

        set_value('retry_interval', self.retry_interval, section, pjsip,
                  nmapped, 'registration')
        set_value('max_retries', self.max_attempts, section, pjsip, nmapped,
                  'registration')
        if self.extension:
            set_value('contact_user', self.extension, section, pjsip, nmapped,
                      'registration')

        set_value('expiration', self.expiry, section, pjsip, nmapped,
                  'registration')

        if self.protocol == 'udp':
            set_value('transport', 'transport-udp', section, pjsip, nmapped,
                      'registration')
        elif self.protocol == 'tcp':
            set_value('transport', 'transport-tcp', section, pjsip, nmapped,
                      'registration')
        elif self.protocol == 'tls':
            set_value('transport', 'transport-tls', section, pjsip, nmapped,
                      'registration')

        auth_section = 'auth_reg_' + self.host

        if hasattr(self, 'secret') and self.secret:
            set_value('password', self.secret, auth_section, pjsip, nmapped,
                      'auth')
            if hasattr(self, 'authuser'):
                set_value('username', self.authuser or self.user, auth_section,
                          pjsip, nmapped, 'auth')
            set_value('outbound_auth', auth_section, section, pjsip, nmapped,
                      'registration')

        client_uri = "sip:%s@" % self.user
        if self.domain:
            client_uri += self.domain
        else:
            client_uri += self.host

        if hasattr(self, 'domainport') and self.domainport:
            client_uri += ":" + self.domainport
        elif self.port:
            client_uri += ":" + self.port

        set_value('client_uri', client_uri, section, pjsip, nmapped,
                  'registration')

        server_uri = "sip:%s" % self.host
        if self.port:
            server_uri += ":" + self.port

        set_value('server_uri', server_uri, section, pjsip, nmapped,
                  'registration')

        if self.outbound_proxy:
            set_value('outboundproxy', self.outbound_proxy, section, pjsip,
                      nmapped, 'registartion')


def map_registrations(sip, pjsip, nmapped):
    """
    Gathers all necessary outbound registration data in sip.conf and creates
    corresponding registration sections in pjsip.conf
    """
    try:
        regs = sip.get('general', 'register')
    except LookupError:
        return

    try:
        retry_interval = sip.get('general', 'registertimeout')[0]
    except LookupError:
        retry_interval = '20'

    try:
        max_attempts = sip.get('general', 'registerattempts')[0]
    except LookupError:
        max_attempts = '10'

    try:
        outbound_proxy = sip.get('general', 'outboundproxy')[0]
    except LookupError:
        outbound_proxy = ''

    for i in regs:
        reg = Registration(i, retry_interval, max_attempts, outbound_proxy)
        reg.write(pjsip, nmapped)


def map_peer(sip, section, pjsip, nmapped):
    """
    Map the options from a peer section in sip.conf into the appropriate
    sections in pjsip.conf
    """
    for i in peer_map:
        try:
            # coming from sip.conf the values should mostly be a list with a
            # single value.  In the few cases that they are not a specialized
            # function (see merge_value) is used to retrieve the values.
            i[1](i[0], sip.get(section, i[0])[0], section, pjsip, nmapped)
        except LookupError:
            pass  # key not found in sip.conf


def find_non_mapped(sections, nmapped):
    """
    Determine sip.conf options that were not properly mapped to pjsip.conf
    options.
    """
    for section, sect in sections.iteritems():
        try:
            # since we are pulling from sip.conf this should always
            # be a single value list
            sect = sect[0]
            # loop through the section and store any values that were not
            # mapped
            for key in sect.keys(True):
                for i in peer_map:
                    if i[0] == key:
                        break
                else:
                    nmapped(section, key, sect[key])
        except LookupError:
            pass


def convert(sip, filename, non_mappings, include):
    """
    Entry point for configuration file conversion. This
    function will create a pjsip.conf object and begin to
    map specific sections from sip.conf into it.
    Returns the new pjsip.conf object once completed
    """
    pjsip = astconfigparser.MultiOrderedConfigParser()
    non_mappings[filename] = astdicts.MultiOrderedDict()
    nmapped = non_mapped(non_mappings[filename])
    if not include:
        # Don't duplicate transport and registration configs
        map_transports(sip, pjsip, nmapped)
        map_registrations(sip, pjsip, nmapped)
    map_auth(sip, pjsip, nmapped)
    for section in sip.sections():
        if section == 'authentication':
            pass
        else:
            map_peer(sip, section, pjsip, nmapped)

    find_non_mapped(sip.defaults(), nmapped)
    find_non_mapped(sip.sections(), nmapped)

    for key, val in sip.includes().iteritems():
        pjsip.add_include(PREFIX + key, convert(val, PREFIX + key,
                          non_mappings, True)[0])
    return pjsip, non_mappings


def write_pjsip(filename, pjsip, non_mappings):
    """
    Write pjsip.conf file to disk
    """
    try:
        with open(filename, 'wt') as fp:
            fp.write(';--\n')
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n')
            fp.write('Non mapped elements start\n')
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n\n')
            astconfigparser.write_dicts(fp, non_mappings[filename])
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n')
            fp.write('Non mapped elements end\n')
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n')
            fp.write('--;\n\n')
            # write out include file(s)
            pjsip.write(fp)

    except IOError:
        print "Could not open file ", filename, " for writing"

###############################################################################


def cli_options():
    """
    Parse command line options and apply them. If invalid input is given,
    print usage information
    """
    global PREFIX
    usage = "usage: %prog [options] [input-file [output-file]]\n\n" \
		"Converts the chan_sip configuration input-file to the chan_pjsip output-file.\n"\
        "The input-file defaults to 'sip.conf'.\n" \
        "The output-file defaults to 'pjsip.conf'."
    parser = optparse.OptionParser(usage=usage)
    parser.add_option('-p', '--prefix', dest='prefix', default=PREFIX,
                      help='output prefix for include files')

    options, args = parser.parse_args()
    PREFIX = options.prefix

    sip_filename = args[0] if len(args) else 'sip.conf'
    pjsip_filename = args[1] if len(args) == 2 else 'pjsip.conf'

    return sip_filename, pjsip_filename

if __name__ == "__main__":
    sip_filename, pjsip_filename = cli_options()
    # configuration parser for sip.conf
    sip = astconfigparser.MultiOrderedConfigParser()
    print 'Reading', sip_filename
    sip.read(sip_filename)
    print 'Converting to PJSIP...'
    pjsip, non_mappings = convert(sip, pjsip_filename, dict(), False)
    print 'Writing', pjsip_filename
    write_pjsip(pjsip_filename, pjsip, non_mappings)
