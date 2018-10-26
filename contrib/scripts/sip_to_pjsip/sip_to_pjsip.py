#!/usr/bin/env python

from __future__ import print_function

import sys
import optparse
import socket
try:
    from urllib.parse import urlparse
except ImportError:
    from urlparse import urlparse # Python 2.7 required for Literal IPv6 Addresses
import astdicts
import astconfigparser

PREFIX = 'pjsip_'
QUIET = False

###############################################################################
### some utility functions
###############################################################################


def section_by_type(section, pjsip, type):
    """Finds a section based upon the given type, adding it if not found."""
    def __find_dict(mdicts, key, val):
        """Given a list of multi-dicts, return the multi-dict that contains
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


def ignore(key=None, val=None, section=None, pjsip=None,
           nmapped=None, type='endpoint'):
    """Ignore a key and mark it as mapped"""


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
                nmapped=None, type='endpoint', section_to=None,
                key_to=None):
    """Merge values from the given section with those from the default."""
    def _merge_value(k, v, s, r, n):
        merge_value(key if key else k, v, s, r, n, type, section_to, key_to)

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
        set_value(key_to if key_to else key, i,
                  section_to if section_to else section,
                  pjsip, nmapped, type)

def merge_codec_value(key=None, val=None, section=None, pjsip=None,
                nmapped=None, type='endpoint', section_to=None,
                key_to=None):
    """Merge values from allow/deny with those from the default. Special treatment for all"""
    def _merge_codec_value(k, v, s, r, n):
        merge_codec_value(key if key else k, v, s, r, n, type, section_to, key_to)

    # if no value or section return the merge_codec_value
    # function with the enclosed key and type
    if not val and not section:
        return _merge_codec_value

    if key == 'allow':
        try:
            disallow = sip.get(section, 'disallow')[0]
            if disallow == 'all':
                #don't inherit
                for i in sip.get(section, 'allow'):
                    set_value(key, i, section, pjsip, nmapped, type)
            else:
                merge_value(key, val, section, pjsip, nmapped, type, section_to, key_to)
        except LookupError:
            print("lookup error", file=sys.stderr)
            merge_value(key, val, section, pjsip, nmapped, type, section_to, key_to)
            return
    elif key == 'disallow':
        try:
            allow = sip.get(section, 'allow')[0]
            if allow == 'all':
                #don't inherit
                for i in sip.get(section, 'disallow'):
                    set_value(key, i, section, pjsip, nmapped, type)
            else:
                merge_value(key, val, section, pjsip, nmapped, type, section_to, key_to)
        except LookupError:
            merge_value(key, val, section, pjsip, nmapped, type, section_to, key_to)
            return
    else:
        merge_value(key, val, section, pjsip, nmapped, type, section_to, key_to)


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


def setup_udptl(section, pjsip, nmapped):
    """Sets values from udptl into the appropriate pjsip.conf options."""
    try:
        val = sip.get(section, 't38pt_udptl')[0]
    except LookupError:
        try:
            val = sip.get('general', 't38pt_udptl')[0]
        except LookupError:
            return

    ec = 'none'
    if 'yes' in val:
        set_value('t38_udptl', 'yes', section, pjsip, nmapped)
    if 'no' in val:
        set_value('t38_udptl', 'no', section, pjsip, nmapped)
    if 'redundancy' in val:
        ec = 'redundancy'
    if 'fec' in val:
        ec = 'fec'
    set_value('t38_udptl_ec', ec, section, pjsip, nmapped)

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
    # 'required' is a new feature of chan_pjsip, which rejects
    #            all SIP clients not supporting Session Timers
    # 'Accept' is the default value of chan_sip and maps to 'yes'
    # chan_sip ignores the case, for example 'session-timers=Refuse'
    val = val.lower()
    if val == 'originate':
        set_value('timers', 'always', section, pjsip, nmapped)
    elif val == 'refuse':
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


def build_host(config, host, section='general', port_key=None):
    """
    Returns a string composed of a host:port. This assumes that the host
    may have a port as part of the initial value. The port_key overrides
    a port in host, see parameter 'bindport' in chan_sip.
    """
    try:
        socket.inet_pton(socket.AF_INET6, host)
        if not host.startswith('['):
            # SIP URI will need brackets.
            host = '[' + host + ']'
    except socket.error:
        pass

    # Literal IPv6 (like [::]), IPv4, or hostname
    # does not work for IPv6 without brackets; case catched above
    url = urlparse('sip://' + host)

    if port_key:
        try:
            port = config.get(section, port_key)[0]
            host = url.hostname # no port, but perhaps no brackets
            try:
                socket.inet_pton(socket.AF_INET6, host)
                if not host.startswith('['):
                    # SIP URI will need brackets.
                    host = '[' + host + ']'
            except socket.error:
                pass
            return host + ':' + port
        except LookupError:
            pass

    # Returns host:port, in brackets if required
    # TODO Does not compress IPv6, for example 0:0:0:0:0:0:0:0 should get [::]
    return url.netloc


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
# type, 100rel, trust_id_outbound, aggregate_mwi, connected_line_method

# known sip.conf peer keys that can be mapped to a pjsip.conf section/key
peer_map = [
    # sip.conf option      mapping function     pjsip.conf option(s)
    ###########################################################################
    ['context',            set_value],
    ['dtmfmode',           set_dtmfmode],
    ['disallow',           merge_codec_value],
    ['allow',              merge_codec_value],
    ['nat',                from_nat],            # rtp_symmetric, force_rport,
                                                 # rewrite_contact
    ['rtptimeout',         set_value('rtp_timeout')],
    ['icesupport',         set_value('ice_support')],
    ['autoframing',        set_value('use_ptime')],
    ['outboundproxy',      set_value('outbound_proxy')],
    ['mohsuggest',         set_value('moh_suggest')],
    ['session-timers',     set_timers],          # timers
    ['session-minse',      set_value('timers_min_se')],
    ['session-expires',    set_value('timers_sess_expires')],
    # identify_by ?
    ['canreinvite',        set_direct_media],    # direct_media alias
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
    ['encryption',         set_media_encryption],
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
    ['setvar',             ignore],

############################ maps to an aor ###################################

    ['host',               from_host],           # contact, max_contacts
    ['qualifyfreq',        set_value('qualify_frequency', type='aor')],
    ['maxexpiry',          set_value('maximum_expiration', type='aor')],
    ['minexpiry',          set_value('minimum_expiration', type='aor')],
    ['defaultexpiry',      set_value('default_expiration', type='aor')],

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
    ['contactpermit',      merge_value(type='acl', section_to='acl', key_to='contact_permit')],
    ['contactdeny',        merge_value(type='acl', section_to='acl', key_to='contact_deny')],
    ['contactacl',         merge_value(type='acl', section_to='acl', key_to='contact_acl')],

########################### maps to transport #################################
#        type = transport
#        protocol
#        bind
#        async_operations
#        ca_list_file
#        ca_list_path
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


def split_hostport(addr):
    """
    Given an address in the form 'host:port' separate the host and port
    components.
    Returns a two-tuple of strings, (host, port). If no port is present in the
    string, then the port section of the tuple is None.
    """
    try:
        socket.inet_pton(socket.AF_INET6, addr)
        if not addr.startswith('['):
            return (addr, None)
    except socket.error:
        pass

    # Literal IPv6 (like [::]), IPv4, or hostname
    # does not work for IPv6 without brackets; case catched above
    url = urlparse('sip://' + addr)
    # TODO Does not compress IPv6, for example 0:0:0:0:0:0:0:0 should get [::]
    return (url.hostname, url.port)


def set_transport_common(section, sip, pjsip, protocol, nmapped):
    """
    sip.conf has several global settings that in pjsip.conf apply to individual
    transports. This function adds these global settings to each individual
    transport.

    The settings included are:
    externaddr (or externip)
    externhost
    externtcpport for TCP
    externtlsport for TLS
    localnet
    tos_sip
    cos_sip
    """
    try:
        extern_addr = sip.multi_get('general', ['externaddr', 'externip',
                                                'externhost'])[0]
        host, port = split_hostport(extern_addr)
        try:
            port = sip.get('general', 'extern' + protocol + 'port')[0]
        except LookupError:
            pass
        set_value('external_media_address', host, section, pjsip,
                  nmapped, 'transport')
        set_value('external_signaling_address', host, section, pjsip,
                  nmapped, 'transport')
        if port:
            set_value('external_signaling_port', port, section, pjsip,
                      nmapped, 'transport')
    except LookupError:
        pass

    try:
        merge_value('localnet', sip.get('general', 'localnet')[0], 'general',
                    pjsip, nmapped, 'transport', section, "local_net")
    except LookupError:
        # No localnet options configured. Move on.
        pass

    try:
        set_value('tos', sip.get('general', 'tos_sip')[0], section, pjsip,
                  nmapped, 'transport')
    except LookupError:
        pass

    try:
        set_value('cos', sip.get('general', 'cos_sip')[0], section, pjsip,
                  nmapped, 'transport')
    except LookupError:
        pass


def get_bind(sip, pjsip, protocol):
    """
    Given the protocol (udp, tcp, or tls), return
    - the bind address, like [::] or 0.0.0.0
    - name of the section to be created
    """
    section = 'transport-' + protocol

    # UDP cannot be disabled in chan_sip
    if protocol != 'udp':
        try:
            enabled = sip.get('general', protocol + 'enable')[0]
        except LookupError:
            # No value means disabled by default. Don't create this transport
            return (None, section)
        if enabled != 'yes':
            return (None, section)

    try:
        bind = pjsip.get(section, 'bind')[0]
        # The first run created an transport already but this
        # server was not configured for IPv4/IPv6 Dual Stack
        return (None, section)
    except LookupError:
        pass

    try:
        bind = pjsip.get(section + '6', 'bind')[0]
        # The first run created an IPv6 transport, because
        # the server was configured with :: as bindaddr.
        # Now, re-use its port and create the IPv4 transport
        host, port = split_hostport(bind)
        bind = '0.0.0.0'
        if port:
            bind += ':' + str(port)
    except LookupError:
        # This is the first run, no transport in pjsip exists.
        try:
            bind = sip.get('general', protocol + 'bindaddr')[0]
        except LookupError:
            if protocol == 'udp':
                try:
                    bind = sip.get('general', 'bindaddr')[0]
                except LookupError:
                    bind = '0.0.0.0'
            else:
                try:
                    bind = pjsip.get('transport-udp6', 'bind')[0]
                except LookupError:
                    bind = pjsip.get('transport-udp', 'bind')[0]
                # Only TCP reuses host:port of UDP, others reuse just host
                if protocol == 'tls':
                    bind, port = split_hostport(bind)
        host, port = split_hostport(bind)
        if host == '::':
            section += '6'

    if protocol == 'udp':
        host = build_host(sip, bind, 'general', 'bindport')
    else:
        host = build_host(sip, bind)

    return (host, section)


def create_udp(sip, pjsip, nmapped):
    """
    Creates a 'transport-udp' section in the pjsip.conf file based
    on the following settings from sip.conf:

    bindaddr (or udpbindaddr)
    bindport
    """
    protocol = 'udp'
    bind, section = get_bind(sip, pjsip, protocol)

    set_value('protocol', protocol, section, pjsip, nmapped, 'transport')
    set_value('bind', bind, section, pjsip, nmapped, 'transport')
    set_transport_common(section, sip, pjsip, protocol, nmapped)


def create_tcp(sip, pjsip, nmapped):
    """
    Creates a 'transport-tcp' section in the pjsip.conf file based
    on the following settings from sip.conf:

    tcpenable
    tcpbindaddr (or bindaddr)
    """
    protocol = 'tcp'
    bind, section = get_bind(sip, pjsip, protocol)
    if not bind:
        return

    set_value('protocol', protocol, section, pjsip, nmapped, 'transport')
    set_value('bind', bind, section, pjsip, nmapped, 'transport')
    set_transport_common(section, sip, pjsip, protocol, nmapped)


def set_tls_cert_file(val, pjsip, section, nmapped):
    """Sets cert_file based on sip.conf tlscertfile"""
    set_value('cert_file', val, section, pjsip, nmapped,
              'transport')


def set_tls_private_key(val, pjsip, section, nmapped):
    """Sets privkey_file based on sip.conf tlsprivatekey or sslprivatekey"""
    set_value('priv_key_file', val, section, pjsip, nmapped,
              'transport')


def set_tls_cipher(val, pjsip, section, nmapped):
    """Sets cipher based on sip.conf tlscipher or sslcipher"""
    set_value('cipher', val, section, pjsip, nmapped, 'transport')


def set_tls_cafile(val, pjsip, section, nmapped):
    """Sets ca_list_file based on sip.conf tlscafile"""
    set_value('ca_list_file', val, section, pjsip, nmapped,
              'transport')


def set_tls_capath(val, pjsip, section, nmapped):
    """Sets ca_list_path based on sip.conf tlscapath"""
    set_value('ca_list_path', val, section, pjsip, nmapped,
              'transport')


def set_tls_verifyclient(val, pjsip, section, nmapped):
    """Sets verify_client based on sip.conf tlsverifyclient"""
    set_value('verify_client', val, section, pjsip, nmapped,
              'transport')


def set_tls_verifyserver(val, pjsip, section, nmapped):
    """Sets verify_server based on sip.conf tlsdontverifyserver"""

    if val == 'no':
        set_value('verify_server', 'yes', section, pjsip, nmapped,
                  'transport')
    else:
        set_value('verify_server', 'no', section, pjsip, nmapped,
                  'transport')


def create_tls(sip, pjsip, nmapped):
    """
    Creates a 'transport-tls' section in pjsip.conf based on the following
    settings from sip.conf:

    tlsenable (or sslenable)
    tlsbindaddr (or sslbindaddr or bindaddr)
    tlsprivatekey (or sslprivatekey)
    tlscipher (or sslcipher)
    tlscafile
    tlscapath (or tlscadir)
    tlscertfile (or sslcert or tlscert)
    tlsverifyclient
    tlsdontverifyserver
    tlsclientmethod (or sslclientmethod)
    """
    protocol = 'tls'
    bind, section = get_bind(sip, pjsip, protocol)
    if not bind:
        return

    set_value('protocol', protocol, section, pjsip, nmapped, 'transport')
    set_value('bind', bind, section, pjsip, nmapped, 'transport')
    set_transport_common(section, sip, pjsip, protocol, nmapped)

    tls_map = [
        (['tlscertfile', 'sslcert', 'tlscert'], set_tls_cert_file),
        (['tlsprivatekey', 'sslprivatekey'], set_tls_private_key),
        (['tlscipher', 'sslcipher'], set_tls_cipher),
        (['tlscafile'], set_tls_cafile),
        (['tlscapath', 'tlscadir'], set_tls_capath),
        (['tlsverifyclient'], set_tls_verifyclient),
        (['tlsdontverifyserver'], set_tls_verifyserver)
    ]

    for i in tls_map:
        try:
            i[1](sip.multi_get('general', i[0])[0], pjsip, section, nmapped)
        except LookupError:
            pass

    try:
        method = sip.multi_get('general', ['tlsclientmethod',
                                           'sslclientmethod'])[0]
        if section != 'transport-' + protocol + '6':  # print only once
            print('In chan_sip, you specified the TLS version. With chan_sip,' \
                  ' this was just for outbound client connections. In' \
                  ' chan_pjsip, this value is for client and server. Instead,' \
                  ' consider not to specify \'tlsclientmethod\' for chan_sip' \
                  ' and \'method = sslv23\' for chan_pjsip.', file=sys.stderr)
    except LookupError:
        """
        OpenSSL emerged during the 90s. SSLv2 and SSLv3 were the only
        existing methods at that time. The OpenSSL project continued. And as
        of today (OpenSSL 1.0.2) this does not start SSLv2 and SSLv3 anymore
        but TLSv1.0 and v1.2. Or stated differently: This method should
        have been called 'method = secure' or 'method = automatic' back in
        the 90s. The PJProject did not realize this and uses 'tlsv1' as
        default when unspecified, which disables TLSv1.2. chan_sip used
        'sslv23' as default when unspecified, which gives TLSv1.0 and v1.2.
        """
        method = 'sslv23'
    set_value('method', method, section, pjsip, nmapped, 'transport')


def map_transports(sip, pjsip, nmapped):
    """
    Finds options in sip.conf general section pertaining to
    transport configuration and creates appropriate transport
    configuration sections in pjsip.conf.

    sip.conf only allows a single UDP transport, TCP transport,
    and TLS transport for each IP version. As such, the mapping
    into PJSIP can be made consistent by defining six sections:

    transport-udp6
    transport-udp
    transport-tcp6
    transport-tcp
    transport-tls6
    transport-tls

    To accommodate the default behaviors in sip.conf, we'll need to
    create the UDP transports first, followed by the TCP and TLS transports.
    """

    # First create a UDP transport. Even if no bind parameters were provided
    # in sip.conf, chan_sip would always bind to UDP 0.0.0.0:5060
    create_udp(sip, pjsip, nmapped)
    create_udp(sip, pjsip, nmapped)

    # TCP settings may be dependent on UDP settings, so do it second.
    create_tcp(sip, pjsip, nmapped)
    create_tcp(sip, pjsip, nmapped)
    create_tls(sip, pjsip, nmapped)
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
        self.peer = ''
        self.protocol = 'udp'
        protocols = ['udp', 'tcp', 'tls']
        for protocol in protocols:
            position = user_part.find(protocol + '://')
            if -1 < position:
                post_transport = user_part[position + 6:]
                self.peer, sep, self.protocol = user_part[:position + 3].rpartition('?')
                user_part = post_transport
                break

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

        self.user, sep, self.domain = pre_auth.partition('@')

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
            set_value('username', self.authuser if hasattr(self, 'authuser')
                      else self.user, auth_section, pjsip, nmapped, 'auth')
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
                      nmapped, 'registration')


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


def map_setvars(sip, section, pjsip, nmapped):
    """
    Map all setvar in peer section to the appropriate endpoint set_var
    """
    try:
        setvars = sip.section(section)[0].get('setvar')
    except LookupError:
        return

    for setvar in setvars:
        set_value('set_var', setvar, section, pjsip, nmapped)


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

    setup_udptl(section, pjsip, nmapped)

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


def map_system(sip, pjsip, nmapped):
    section = 'system' # Just a label; you as user can change that
    type = 'system' # Not a label, therefore not the same as section

    try:
        user_agent = sip.get('general', 'useragent')[0]
        set_value('user_agent', user_agent, 'global', pjsip, nmapped, 'global')
    except LookupError:
        pass


    try:
        sipdebug = sip.get('general', 'sipdebug')[0]
        set_value('debug', sipdebug, 'global', pjsip, nmapped, 'global')
    except LookupError:
        pass

    try:
        useroption_parsing = sip.get('general', 'legacy_useroption_parsing')[0]
        set_value('ignore_uri_user_options', useroption_parsing, 'global', pjsip, nmapped, 'global')
    except LookupError:
        pass

    try:
        timer_t1 = sip.get('general', 'timert1')[0]
        set_value('timer_t1', timer_t1, section, pjsip, nmapped, type)
    except LookupError:
        pass

    try:
        timer_b = sip.get('general', 'timerb')[0]
        set_value('timer_b', timer_b, section, pjsip, nmapped, type)
    except LookupError:
        pass

    try:
        compact_headers = sip.get('general', 'compactheaders')[0]
        set_value('compact_headers', compact_headers, section, pjsip, nmapped, type)
    except LookupError:
        pass


def convert(sip, filename, non_mappings, include):
    """
    Entry point for configuration file conversion. This
    function will create a pjsip.conf object and begin to
    map specific sections from sip.conf into it.
    Returns the new pjsip.conf object once completed
    """
    pjsip = sip.__class__()
    non_mappings[filename] = astdicts.MultiOrderedDict()
    nmapped = non_mapped(non_mappings[filename])
    if not include:
        # Don't duplicate transport and registration configs
        map_system(sip, pjsip, nmapped)
        map_transports(sip, pjsip, nmapped)
        map_registrations(sip, pjsip, nmapped)
    map_auth(sip, pjsip, nmapped)
    for section in sip.sections():
        if section == 'authentication':
            pass
        else:
            map_peer(sip, section, pjsip, nmapped)
            map_setvars(sip, section, pjsip, nmapped)

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
        print("Could not open file " + filename + " for writing", file=sys.stderr)

###############################################################################


def cli_options():
    """
    Parse command line options and apply them. If invalid input is given,
    print usage information
    """
    global PREFIX
    global QUIET
    usage = "usage: %prog [options] [input-file [output-file]]\n\n" \
        "Converts the chan_sip configuration input-file to the chan_pjsip output-file.\n" \
        "The input-file defaults to 'sip.conf'.\n" \
        "The output-file defaults to 'pjsip.conf'."
    parser = optparse.OptionParser(usage=usage)
    parser.add_option('-p', '--prefix', dest='prefix', default=PREFIX,
                      help='output prefix for include files')
    parser.add_option('-q', '--quiet', dest='quiet', default=False, action='store_true',
                      help="don't print messages to stdout")

    options, args = parser.parse_args()
    PREFIX = options.prefix
    if options.quiet:
        QUIET = True

    sip_filename = args[0] if len(args) else 'sip.conf'
    pjsip_filename = args[1] if len(args) == 2 else 'pjsip.conf'

    return sip_filename, pjsip_filename


def info(msg):
    if QUIET:
        return
    print(msg)


if __name__ == "__main__":
    sip_filename, pjsip_filename = cli_options()
    # configuration parser for sip.conf
    sip = astconfigparser.MultiOrderedConfigParser()
    info('Please, report any issue at:')
    info('    https://issues.asterisk.org/')
    info('Reading ' + sip_filename)
    sip.read(sip_filename)
    info('Converting to PJSIP...')
    pjsip, non_mappings = convert(sip, pjsip_filename, dict(), False)
    info('Writing ' + pjsip_filename)
    write_pjsip(pjsip_filename, pjsip, non_mappings)
