CREATE TABLE alembic_version (
    version_num VARCHAR2(32 CHAR) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
)

/

-- Running upgrade  -> 4da0c5f79a9c

CREATE TABLE sippeers (
    id INTEGER NOT NULL, 
    name VARCHAR2(40 CHAR) NOT NULL, 
    ipaddr VARCHAR2(45 CHAR), 
    port INTEGER, 
    regseconds INTEGER, 
    defaultuser VARCHAR2(40 CHAR), 
    fullcontact VARCHAR2(80 CHAR), 
    regserver VARCHAR2(20 CHAR), 
    useragent VARCHAR2(20 CHAR), 
    lastms INTEGER, 
    host VARCHAR2(40 CHAR), 
    type VARCHAR(6 CHAR), 
    context VARCHAR2(40 CHAR), 
    permit VARCHAR2(95 CHAR), 
    deny VARCHAR2(95 CHAR), 
    secret VARCHAR2(40 CHAR), 
    md5secret VARCHAR2(40 CHAR), 
    remotesecret VARCHAR2(40 CHAR), 
    transport VARCHAR(7 CHAR), 
    dtmfmode VARCHAR(9 CHAR), 
    directmedia VARCHAR(6 CHAR), 
    nat VARCHAR2(29 CHAR), 
    callgroup VARCHAR2(40 CHAR), 
    pickupgroup VARCHAR2(40 CHAR), 
    language VARCHAR2(40 CHAR), 
    disallow VARCHAR2(200 CHAR), 
    allow VARCHAR2(200 CHAR), 
    insecure VARCHAR2(40 CHAR), 
    trustrpid VARCHAR(3 CHAR), 
    progressinband VARCHAR(5 CHAR), 
    promiscredir VARCHAR(3 CHAR), 
    useclientcode VARCHAR(3 CHAR), 
    accountcode VARCHAR2(40 CHAR), 
    setvar VARCHAR2(200 CHAR), 
    callerid VARCHAR2(40 CHAR), 
    amaflags VARCHAR2(40 CHAR), 
    callcounter VARCHAR(3 CHAR), 
    busylevel INTEGER, 
    allowoverlap VARCHAR(3 CHAR), 
    allowsubscribe VARCHAR(3 CHAR), 
    videosupport VARCHAR(3 CHAR), 
    maxcallbitrate INTEGER, 
    rfc2833compensate VARCHAR(3 CHAR), 
    mailbox VARCHAR2(40 CHAR), 
    "session-timers" VARCHAR(9 CHAR), 
    "session-expires" INTEGER, 
    "session-minse" INTEGER, 
    "session-refresher" VARCHAR(3 CHAR), 
    t38pt_usertpsource VARCHAR2(40 CHAR), 
    regexten VARCHAR2(40 CHAR), 
    fromdomain VARCHAR2(40 CHAR), 
    fromuser VARCHAR2(40 CHAR), 
    qualify VARCHAR2(40 CHAR), 
    defaultip VARCHAR2(45 CHAR), 
    rtptimeout INTEGER, 
    rtpholdtimeout INTEGER, 
    sendrpid VARCHAR(3 CHAR), 
    outboundproxy VARCHAR2(40 CHAR), 
    callbackextension VARCHAR2(40 CHAR), 
    timert1 INTEGER, 
    timerb INTEGER, 
    qualifyfreq INTEGER, 
    constantssrc VARCHAR(3 CHAR), 
    contactpermit VARCHAR2(95 CHAR), 
    contactdeny VARCHAR2(95 CHAR), 
    usereqphone VARCHAR(3 CHAR), 
    textsupport VARCHAR(3 CHAR), 
    faxdetect VARCHAR(3 CHAR), 
    buggymwi VARCHAR(3 CHAR), 
    auth VARCHAR2(40 CHAR), 
    fullname VARCHAR2(40 CHAR), 
    trunkname VARCHAR2(40 CHAR), 
    cid_number VARCHAR2(40 CHAR), 
    callingpres VARCHAR(21 CHAR), 
    mohinterpret VARCHAR2(40 CHAR), 
    mohsuggest VARCHAR2(40 CHAR), 
    parkinglot VARCHAR2(40 CHAR), 
    hasvoicemail VARCHAR(3 CHAR), 
    subscribemwi VARCHAR(3 CHAR), 
    vmexten VARCHAR2(40 CHAR), 
    autoframing VARCHAR(3 CHAR), 
    rtpkeepalive INTEGER, 
    "call-limit" INTEGER, 
    g726nonstandard VARCHAR(3 CHAR), 
    ignoresdpversion VARCHAR(3 CHAR), 
    allowtransfer VARCHAR(3 CHAR), 
    dynamic VARCHAR(3 CHAR), 
    path VARCHAR2(256 CHAR), 
    supportpath VARCHAR(3 CHAR), 
    PRIMARY KEY (id), 
    UNIQUE (name), 
    CONSTRAINT type_values CHECK (type IN ('friend', 'user', 'peer')), 
    CONSTRAINT sip_transport_values CHECK (transport IN ('udp', 'tcp', 'tls', 'ws', 'wss', 'udp,tcp', 'tcp,udp')), 
    CONSTRAINT sip_dtmfmode_values CHECK (dtmfmode IN ('rfc2833', 'info', 'shortinfo', 'inband', 'auto')), 
    CONSTRAINT sip_directmedia_values CHECK (directmedia IN ('yes', 'no', 'nonat', 'update')), 
    CONSTRAINT yes_no_values CHECK (trustrpid IN ('yes', 'no')), 
    CONSTRAINT sip_progressinband_values CHECK (progressinband IN ('yes', 'no', 'never')), 
    CONSTRAINT yes_no_values CHECK (promiscredir IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (useclientcode IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (callcounter IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (allowoverlap IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (allowsubscribe IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (videosupport IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (rfc2833compensate IN ('yes', 'no')), 
    CONSTRAINT sip_session_timers_values CHECK ("session-timers" IN ('accept', 'refuse', 'originate')), 
    CONSTRAINT sip_session_refresher_values CHECK ("session-refresher" IN ('uac', 'uas')), 
    CONSTRAINT yes_no_values CHECK (sendrpid IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (constantssrc IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (usereqphone IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (textsupport IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (faxdetect IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (buggymwi IN ('yes', 'no')), 
    CONSTRAINT sip_callingpres_values CHECK (callingpres IN ('allowed_not_screened', 'allowed_passed_screen', 'allowed_failed_screen', 'allowed', 'prohib_not_screened', 'prohib_passed_screen', 'prohib_failed_screen', 'prohib')), 
    CONSTRAINT yes_no_values CHECK (hasvoicemail IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (subscribemwi IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (autoframing IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (g726nonstandard IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (ignoresdpversion IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (allowtransfer IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (dynamic IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (supportpath IN ('yes', 'no'))
)

/

CREATE INDEX sippeers_name ON sippeers (name)

/

CREATE INDEX sippeers_name_host ON sippeers (name, host)

/

CREATE INDEX sippeers_ipaddr_port ON sippeers (ipaddr, port)

/

CREATE INDEX sippeers_host_port ON sippeers (host, port)

/

CREATE TABLE iaxfriends (
    id INTEGER NOT NULL, 
    name VARCHAR2(40 CHAR) NOT NULL, 
    type VARCHAR(6 CHAR), 
    username VARCHAR2(40 CHAR), 
    mailbox VARCHAR2(40 CHAR), 
    secret VARCHAR2(40 CHAR), 
    dbsecret VARCHAR2(40 CHAR), 
    context VARCHAR2(40 CHAR), 
    regcontext VARCHAR2(40 CHAR), 
    host VARCHAR2(40 CHAR), 
    ipaddr VARCHAR2(40 CHAR), 
    port INTEGER, 
    defaultip VARCHAR2(20 CHAR), 
    sourceaddress VARCHAR2(20 CHAR), 
    mask VARCHAR2(20 CHAR), 
    regexten VARCHAR2(40 CHAR), 
    regseconds INTEGER, 
    accountcode VARCHAR2(20 CHAR), 
    mohinterpret VARCHAR2(20 CHAR), 
    mohsuggest VARCHAR2(20 CHAR), 
    inkeys VARCHAR2(40 CHAR), 
    outkeys VARCHAR2(40 CHAR), 
    language VARCHAR2(10 CHAR), 
    callerid VARCHAR2(100 CHAR), 
    cid_number VARCHAR2(40 CHAR), 
    sendani VARCHAR(3 CHAR), 
    fullname VARCHAR2(40 CHAR), 
    trunk VARCHAR(3 CHAR), 
    auth VARCHAR2(20 CHAR), 
    maxauthreq INTEGER, 
    requirecalltoken VARCHAR(4 CHAR), 
    encryption VARCHAR(6 CHAR), 
    transfer VARCHAR(9 CHAR), 
    jitterbuffer VARCHAR(3 CHAR), 
    forcejitterbuffer VARCHAR(3 CHAR), 
    disallow VARCHAR2(200 CHAR), 
    allow VARCHAR2(200 CHAR), 
    codecpriority VARCHAR2(40 CHAR), 
    qualify VARCHAR2(10 CHAR), 
    qualifysmoothing VARCHAR(3 CHAR), 
    qualifyfreqok VARCHAR2(10 CHAR), 
    qualifyfreqnotok VARCHAR2(10 CHAR), 
    timezone VARCHAR2(20 CHAR), 
    adsi VARCHAR(3 CHAR), 
    amaflags VARCHAR2(20 CHAR), 
    setvar VARCHAR2(200 CHAR), 
    PRIMARY KEY (id), 
    UNIQUE (name), 
    CONSTRAINT type_values CHECK (type IN ('friend', 'user', 'peer')), 
    CONSTRAINT yes_no_values CHECK (sendani IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (trunk IN ('yes', 'no')), 
    CONSTRAINT iax_requirecalltoken_values CHECK (requirecalltoken IN ('yes', 'no', 'auto')), 
    CONSTRAINT iax_encryption_values CHECK (encryption IN ('yes', 'no', 'aes128')), 
    CONSTRAINT iax_transfer_values CHECK (transfer IN ('yes', 'no', 'mediaonly')), 
    CONSTRAINT yes_no_values CHECK (jitterbuffer IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (forcejitterbuffer IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (qualifysmoothing IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (adsi IN ('yes', 'no'))
)

/

CREATE INDEX iaxfriends_name ON iaxfriends (name)

/

CREATE INDEX iaxfriends_name_host ON iaxfriends (name, host)

/

CREATE INDEX iaxfriends_name_ipaddr_port ON iaxfriends (name, ipaddr, port)

/

CREATE INDEX iaxfriends_ipaddr_port ON iaxfriends (ipaddr, port)

/

CREATE INDEX iaxfriends_host_port ON iaxfriends (host, port)

/

CREATE TABLE voicemail (
    uniqueid INTEGER NOT NULL, 
    context VARCHAR2(80 CHAR) NOT NULL, 
    mailbox VARCHAR2(80 CHAR) NOT NULL, 
    password VARCHAR2(80 CHAR) NOT NULL, 
    fullname VARCHAR2(80 CHAR), 
    alias VARCHAR2(80 CHAR), 
    email VARCHAR2(80 CHAR), 
    pager VARCHAR2(80 CHAR), 
    attach VARCHAR(3 CHAR), 
    attachfmt VARCHAR2(10 CHAR), 
    serveremail VARCHAR2(80 CHAR), 
    language VARCHAR2(20 CHAR), 
    tz VARCHAR2(30 CHAR), 
    deletevoicemail VARCHAR(3 CHAR), 
    saycid VARCHAR(3 CHAR), 
    sendvoicemail VARCHAR(3 CHAR), 
    review VARCHAR(3 CHAR), 
    tempgreetwarn VARCHAR(3 CHAR), 
    operator VARCHAR(3 CHAR), 
    envelope VARCHAR(3 CHAR), 
    sayduration INTEGER, 
    forcename VARCHAR(3 CHAR), 
    forcegreetings VARCHAR(3 CHAR), 
    callback VARCHAR2(80 CHAR), 
    dialout VARCHAR2(80 CHAR), 
    exitcontext VARCHAR2(80 CHAR), 
    maxmsg INTEGER, 
    volgain NUMERIC(5, 2), 
    imapuser VARCHAR2(80 CHAR), 
    imappassword VARCHAR2(80 CHAR), 
    imapserver VARCHAR2(80 CHAR), 
    imapport VARCHAR2(8 CHAR), 
    imapflags VARCHAR2(80 CHAR), 
    stamp DATE, 
    PRIMARY KEY (uniqueid), 
    CONSTRAINT yes_no_values CHECK (attach IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (deletevoicemail IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (saycid IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (sendvoicemail IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (review IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (tempgreetwarn IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (operator IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (envelope IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (forcename IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (forcegreetings IN ('yes', 'no'))
)

/

CREATE INDEX voicemail_mailbox ON voicemail (mailbox)

/

CREATE INDEX voicemail_context ON voicemail (context)

/

CREATE INDEX voicemail_mailbox_context ON voicemail (mailbox, context)

/

CREATE INDEX voicemail_imapuser ON voicemail (imapuser)

/

CREATE TABLE meetme (
    bookid INTEGER NOT NULL, 
    confno VARCHAR2(80 CHAR) NOT NULL, 
    starttime DATE, 
    endtime DATE, 
    pin VARCHAR2(20 CHAR), 
    adminpin VARCHAR2(20 CHAR), 
    opts VARCHAR2(20 CHAR), 
    adminopts VARCHAR2(20 CHAR), 
    recordingfilename VARCHAR2(80 CHAR), 
    recordingformat VARCHAR2(10 CHAR), 
    maxusers INTEGER, 
    members INTEGER NOT NULL, 
    PRIMARY KEY (bookid)
)

/

CREATE INDEX meetme_confno_start_end ON meetme (confno, starttime, endtime)

/

CREATE TABLE musiconhold (
    name VARCHAR2(80 CHAR) NOT NULL, 
    "mode" VARCHAR(10 CHAR), 
    directory VARCHAR2(255 CHAR), 
    application VARCHAR2(255 CHAR), 
    digit VARCHAR2(1 CHAR), 
    sort VARCHAR2(10 CHAR), 
    format VARCHAR2(10 CHAR), 
    stamp DATE, 
    PRIMARY KEY (name), 
    CONSTRAINT moh_mode_values CHECK ("mode" IN ('custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3'))
)

/

INSERT INTO alembic_version (version_num) VALUES ('4da0c5f79a9c')

/

-- Running upgrade 4da0c5f79a9c -> 43956d550a44

CREATE TABLE ps_endpoints (
    id VARCHAR2(40 CHAR) NOT NULL, 
    transport VARCHAR2(40 CHAR), 
    aors VARCHAR2(200 CHAR), 
    auth VARCHAR2(40 CHAR), 
    context VARCHAR2(40 CHAR), 
    disallow VARCHAR2(200 CHAR), 
    allow VARCHAR2(200 CHAR), 
    direct_media VARCHAR(3 CHAR), 
    connected_line_method VARCHAR(8 CHAR), 
    direct_media_method VARCHAR(8 CHAR), 
    direct_media_glare_mitigation VARCHAR(8 CHAR), 
    disable_direct_media_on_nat VARCHAR(3 CHAR), 
    dtmf_mode VARCHAR(7 CHAR), 
    external_media_address VARCHAR2(40 CHAR), 
    force_rport VARCHAR(3 CHAR), 
    ice_support VARCHAR(3 CHAR), 
    identify_by VARCHAR(8 CHAR), 
    mailboxes VARCHAR2(40 CHAR), 
    moh_suggest VARCHAR2(40 CHAR), 
    outbound_auth VARCHAR2(40 CHAR), 
    outbound_proxy VARCHAR2(40 CHAR), 
    rewrite_contact VARCHAR(3 CHAR), 
    rtp_ipv6 VARCHAR(3 CHAR), 
    rtp_symmetric VARCHAR(3 CHAR), 
    send_diversion VARCHAR(3 CHAR), 
    send_pai VARCHAR(3 CHAR), 
    send_rpid VARCHAR(3 CHAR), 
    timers_min_se INTEGER, 
    timers VARCHAR(8 CHAR), 
    timers_sess_expires INTEGER, 
    callerid VARCHAR2(40 CHAR), 
    callerid_privacy VARCHAR(23 CHAR), 
    callerid_tag VARCHAR2(40 CHAR), 
    "100rel" VARCHAR(8 CHAR), 
    aggregate_mwi VARCHAR(3 CHAR), 
    trust_id_inbound VARCHAR(3 CHAR), 
    trust_id_outbound VARCHAR(3 CHAR), 
    use_ptime VARCHAR(3 CHAR), 
    use_avpf VARCHAR(3 CHAR), 
    media_encryption VARCHAR(4 CHAR), 
    inband_progress VARCHAR(3 CHAR), 
    call_group VARCHAR2(40 CHAR), 
    pickup_group VARCHAR2(40 CHAR), 
    named_call_group VARCHAR2(40 CHAR), 
    named_pickup_group VARCHAR2(40 CHAR), 
    device_state_busy_at INTEGER, 
    fax_detect VARCHAR(3 CHAR), 
    t38_udptl VARCHAR(3 CHAR), 
    t38_udptl_ec VARCHAR(10 CHAR), 
    t38_udptl_maxdatagram INTEGER, 
    t38_udptl_nat VARCHAR(3 CHAR), 
    t38_udptl_ipv6 VARCHAR(3 CHAR), 
    tone_zone VARCHAR2(40 CHAR), 
    language VARCHAR2(40 CHAR), 
    one_touch_recording VARCHAR(3 CHAR), 
    record_on_feature VARCHAR2(40 CHAR), 
    record_off_feature VARCHAR2(40 CHAR), 
    rtp_engine VARCHAR2(40 CHAR), 
    allow_transfer VARCHAR(3 CHAR), 
    allow_subscribe VARCHAR(3 CHAR), 
    sdp_owner VARCHAR2(40 CHAR), 
    sdp_session VARCHAR2(40 CHAR), 
    tos_audio INTEGER, 
    tos_video INTEGER, 
    cos_audio INTEGER, 
    cos_video INTEGER, 
    sub_min_expiry INTEGER, 
    from_domain VARCHAR2(40 CHAR), 
    from_user VARCHAR2(40 CHAR), 
    mwi_fromuser VARCHAR2(40 CHAR), 
    dtls_verify VARCHAR2(40 CHAR), 
    dtls_rekey VARCHAR2(40 CHAR), 
    dtls_cert_file VARCHAR2(200 CHAR), 
    dtls_private_key VARCHAR2(200 CHAR), 
    dtls_cipher VARCHAR2(200 CHAR), 
    dtls_ca_file VARCHAR2(200 CHAR), 
    dtls_ca_path VARCHAR2(200 CHAR), 
    dtls_setup VARCHAR(7 CHAR), 
    srtp_tag_32 VARCHAR(3 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (direct_media IN ('yes', 'no')), 
    CONSTRAINT pjsip_connected_line_method_values CHECK (connected_line_method IN ('invite', 'reinvite', 'update')), 
    CONSTRAINT pjsip_connected_line_method_values CHECK (direct_media_method IN ('invite', 'reinvite', 'update')), 
    CONSTRAINT pjsip_direct_media_glare_mitigation_values CHECK (direct_media_glare_mitigation IN ('none', 'outgoing', 'incoming')), 
    CONSTRAINT yesno_values CHECK (disable_direct_media_on_nat IN ('yes', 'no')), 
    CONSTRAINT pjsip_dtmf_mode_values CHECK (dtmf_mode IN ('rfc4733', 'inband', 'info')), 
    CONSTRAINT yesno_values CHECK (force_rport IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (ice_support IN ('yes', 'no')), 
    CONSTRAINT pjsip_identify_by_values CHECK (identify_by IN ('username')), 
    CONSTRAINT yesno_values CHECK (rewrite_contact IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (rtp_ipv6 IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (rtp_symmetric IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (send_diversion IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (send_pai IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (send_rpid IN ('yes', 'no')), 
    CONSTRAINT pjsip_timer_values CHECK (timers IN ('forced', 'no', 'required', 'yes')), 
    CONSTRAINT pjsip_cid_privacy_values CHECK (callerid_privacy IN ('allowed_not_screened', 'allowed_passed_screened', 'allowed_failed_screened', 'allowed', 'prohib_not_screened', 'prohib_passed_screened', 'prohib_failed_screened', 'prohib', 'unavailable')), 
    CONSTRAINT pjsip_100rel_values CHECK ("100rel" IN ('no', 'required', 'yes')), 
    CONSTRAINT yesno_values CHECK (aggregate_mwi IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (trust_id_inbound IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (trust_id_outbound IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (use_ptime IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (use_avpf IN ('yes', 'no')), 
    CONSTRAINT pjsip_media_encryption_values CHECK (media_encryption IN ('no', 'sdes', 'dtls')), 
    CONSTRAINT yesno_values CHECK (inband_progress IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (fax_detect IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (t38_udptl IN ('yes', 'no')), 
    CONSTRAINT pjsip_t38udptl_ec_values CHECK (t38_udptl_ec IN ('none', 'fec', 'redundancy')), 
    CONSTRAINT yesno_values CHECK (t38_udptl_nat IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (t38_udptl_ipv6 IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (one_touch_recording IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (allow_transfer IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (allow_subscribe IN ('yes', 'no')), 
    CONSTRAINT pjsip_dtls_setup_values CHECK (dtls_setup IN ('active', 'passive', 'actpass')), 
    CONSTRAINT yesno_values CHECK (srtp_tag_32 IN ('yes', 'no'))
)

/

CREATE INDEX ps_endpoints_id ON ps_endpoints (id)

/

CREATE TABLE ps_auths (
    id VARCHAR2(40 CHAR) NOT NULL, 
    auth_type VARCHAR(8 CHAR), 
    nonce_lifetime INTEGER, 
    md5_cred VARCHAR2(40 CHAR), 
    password VARCHAR2(80 CHAR), 
    realm VARCHAR2(40 CHAR), 
    username VARCHAR2(40 CHAR), 
    UNIQUE (id), 
    CONSTRAINT pjsip_auth_type_values CHECK (auth_type IN ('md5', 'userpass'))
)

/

CREATE INDEX ps_auths_id ON ps_auths (id)

/

CREATE TABLE ps_aors (
    id VARCHAR2(40 CHAR) NOT NULL, 
    contact VARCHAR2(40 CHAR), 
    default_expiration INTEGER, 
    mailboxes VARCHAR2(80 CHAR), 
    max_contacts INTEGER, 
    minimum_expiration INTEGER, 
    remove_existing VARCHAR(3 CHAR), 
    qualify_frequency INTEGER, 
    authenticate_qualify VARCHAR(3 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (remove_existing IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (authenticate_qualify IN ('yes', 'no'))
)

/

CREATE INDEX ps_aors_id ON ps_aors (id)

/

CREATE TABLE ps_contacts (
    id VARCHAR2(40 CHAR) NOT NULL, 
    uri VARCHAR2(40 CHAR), 
    expiration_time VARCHAR2(40 CHAR), 
    qualify_frequency INTEGER, 
    UNIQUE (id)
)

/

CREATE INDEX ps_contacts_id ON ps_contacts (id)

/

CREATE TABLE ps_domain_aliases (
    id VARCHAR2(40 CHAR) NOT NULL, 
    domain VARCHAR2(80 CHAR), 
    UNIQUE (id)
)

/

CREATE INDEX ps_domain_aliases_id ON ps_domain_aliases (id)

/

CREATE TABLE ps_endpoint_id_ips (
    id VARCHAR2(40 CHAR) NOT NULL, 
    endpoint VARCHAR2(40 CHAR), 
    match VARCHAR2(80 CHAR), 
    UNIQUE (id)
)

/

CREATE INDEX ps_endpoint_id_ips_id ON ps_endpoint_id_ips (id)

/

UPDATE alembic_version SET version_num='43956d550a44' WHERE alembic_version.version_num = '4da0c5f79a9c'

/

-- Running upgrade 43956d550a44 -> 581a4264e537

CREATE TABLE extensions (
    id NUMBER(19) NOT NULL, 
    context VARCHAR2(40 CHAR) NOT NULL, 
    exten VARCHAR2(40 CHAR) NOT NULL, 
    priority INTEGER NOT NULL, 
    app VARCHAR2(40 CHAR) NOT NULL, 
    appdata VARCHAR2(256 CHAR) NOT NULL, 
    PRIMARY KEY (id), 
    UNIQUE (context, exten, priority), 
    UNIQUE (id)
)

/

UPDATE alembic_version SET version_num='581a4264e537' WHERE alembic_version.version_num = '43956d550a44'

/

-- Running upgrade 581a4264e537 -> 2fc7930b41b3

CREATE TABLE ps_systems (
    id VARCHAR2(40 CHAR) NOT NULL, 
    timer_t1 INTEGER, 
    timer_b INTEGER, 
    compact_headers VARCHAR(3 CHAR), 
    threadpool_initial_size INTEGER, 
    threadpool_auto_increment INTEGER, 
    threadpool_idle_timeout INTEGER, 
    threadpool_max_size INTEGER, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (compact_headers IN ('yes', 'no'))
)

/

CREATE INDEX ps_systems_id ON ps_systems (id)

/

CREATE TABLE ps_globals (
    id VARCHAR2(40 CHAR) NOT NULL, 
    max_forwards INTEGER, 
    user_agent VARCHAR2(40 CHAR), 
    default_outbound_endpoint VARCHAR2(40 CHAR), 
    UNIQUE (id)
)

/

CREATE INDEX ps_globals_id ON ps_globals (id)

/

CREATE TABLE ps_transports (
    id VARCHAR2(40 CHAR) NOT NULL, 
    async_operations INTEGER, 
    bind VARCHAR2(40 CHAR), 
    ca_list_file VARCHAR2(200 CHAR), 
    cert_file VARCHAR2(200 CHAR), 
    cipher VARCHAR2(200 CHAR), 
    domain VARCHAR2(40 CHAR), 
    external_media_address VARCHAR2(40 CHAR), 
    external_signaling_address VARCHAR2(40 CHAR), 
    external_signaling_port INTEGER, 
    method VARCHAR(11 CHAR), 
    local_net VARCHAR2(40 CHAR), 
    password VARCHAR2(40 CHAR), 
    priv_key_file VARCHAR2(200 CHAR), 
    protocol VARCHAR(3 CHAR), 
    require_client_cert VARCHAR(3 CHAR), 
    verify_client VARCHAR(3 CHAR), 
    verifiy_server VARCHAR(3 CHAR), 
    tos VARCHAR(3 CHAR), 
    cos VARCHAR(3 CHAR), 
    UNIQUE (id), 
    CONSTRAINT pjsip_transport_method_values CHECK (method IN ('default', 'unspecified', 'tlsv1', 'sslv2', 'sslv3', 'sslv23')), 
    CONSTRAINT pjsip_transport_protocol_values CHECK (protocol IN ('udp', 'tcp', 'tls', 'ws', 'wss')), 
    CONSTRAINT yesno_values CHECK (require_client_cert IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (verify_client IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (verifiy_server IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (tos IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (cos IN ('yes', 'no'))
)

/

CREATE INDEX ps_transports_id ON ps_transports (id)

/

CREATE TABLE ps_registrations (
    id VARCHAR2(40 CHAR) NOT NULL, 
    auth_rejection_permanent VARCHAR(3 CHAR), 
    client_uri VARCHAR2(40 CHAR), 
    contact_user VARCHAR2(40 CHAR), 
    expiration INTEGER, 
    max_retries INTEGER, 
    outbound_auth VARCHAR2(40 CHAR), 
    outbound_proxy VARCHAR2(40 CHAR), 
    retry_interval INTEGER, 
    forbidden_retry_interval INTEGER, 
    server_uri VARCHAR2(40 CHAR), 
    transport VARCHAR2(40 CHAR), 
    support_path VARCHAR(3 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (auth_rejection_permanent IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (support_path IN ('yes', 'no'))
)

/

CREATE INDEX ps_registrations_id ON ps_registrations (id)

/

ALTER TABLE ps_endpoints ADD media_address VARCHAR2(40 CHAR)

/

ALTER TABLE ps_endpoints ADD redirect_method VARCHAR(9 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_redirect_method_values CHECK (redirect_method IN ('user', 'uri_core', 'uri_pjsip'))

/

ALTER TABLE ps_endpoints ADD set_var CLOB

/

ALTER TABLE ps_endpoints RENAME COLUMN mwi_fromuser TO mwi_from_user

/

ALTER TABLE ps_contacts ADD outbound_proxy VARCHAR2(40 CHAR)

/

ALTER TABLE ps_contacts ADD path CLOB

/

ALTER TABLE ps_aors ADD maximum_expiration INTEGER

/

ALTER TABLE ps_aors ADD outbound_proxy VARCHAR2(40 CHAR)

/

ALTER TABLE ps_aors ADD support_path VARCHAR(3 CHAR)

/

ALTER TABLE ps_aors ADD CONSTRAINT yesno_values CHECK (support_path IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='2fc7930b41b3' WHERE alembic_version.version_num = '581a4264e537'

/

-- Running upgrade 2fc7930b41b3 -> 21e526ad3040

ALTER TABLE ps_globals ADD debug VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='21e526ad3040' WHERE alembic_version.version_num = '2fc7930b41b3'

/

-- Running upgrade 21e526ad3040 -> 28887f25a46f

CREATE TABLE queues (
    name VARCHAR2(128 CHAR) NOT NULL, 
    musiconhold VARCHAR2(128 CHAR), 
    announce VARCHAR2(128 CHAR), 
    context VARCHAR2(128 CHAR), 
    timeout INTEGER, 
    ringinuse VARCHAR(3 CHAR), 
    setinterfacevar VARCHAR(3 CHAR), 
    setqueuevar VARCHAR(3 CHAR), 
    setqueueentryvar VARCHAR(3 CHAR), 
    monitor_format VARCHAR2(8 CHAR), 
    membermacro VARCHAR2(512 CHAR), 
    membergosub VARCHAR2(512 CHAR), 
    queue_youarenext VARCHAR2(128 CHAR), 
    queue_thereare VARCHAR2(128 CHAR), 
    queue_callswaiting VARCHAR2(128 CHAR), 
    queue_quantity1 VARCHAR2(128 CHAR), 
    queue_quantity2 VARCHAR2(128 CHAR), 
    queue_holdtime VARCHAR2(128 CHAR), 
    queue_minutes VARCHAR2(128 CHAR), 
    queue_minute VARCHAR2(128 CHAR), 
    queue_seconds VARCHAR2(128 CHAR), 
    queue_thankyou VARCHAR2(128 CHAR), 
    queue_callerannounce VARCHAR2(128 CHAR), 
    queue_reporthold VARCHAR2(128 CHAR), 
    announce_frequency INTEGER, 
    announce_to_first_user VARCHAR(3 CHAR), 
    min_announce_frequency INTEGER, 
    announce_round_seconds INTEGER, 
    announce_holdtime VARCHAR2(128 CHAR), 
    announce_position VARCHAR2(128 CHAR), 
    announce_position_limit INTEGER, 
    periodic_announce VARCHAR2(50 CHAR), 
    periodic_announce_frequency INTEGER, 
    relative_periodic_announce VARCHAR(3 CHAR), 
    random_periodic_announce VARCHAR(3 CHAR), 
    retry INTEGER, 
    wrapuptime INTEGER, 
    penaltymemberslimit INTEGER, 
    autofill VARCHAR(3 CHAR), 
    monitor_type VARCHAR2(128 CHAR), 
    autopause VARCHAR(3 CHAR), 
    autopausedelay INTEGER, 
    autopausebusy VARCHAR(3 CHAR), 
    autopauseunavail VARCHAR(3 CHAR), 
    maxlen INTEGER, 
    servicelevel INTEGER, 
    strategy VARCHAR(11 CHAR), 
    joinempty VARCHAR2(128 CHAR), 
    leavewhenempty VARCHAR2(128 CHAR), 
    reportholdtime VARCHAR(3 CHAR), 
    memberdelay INTEGER, 
    weight INTEGER, 
    timeoutrestart VARCHAR(3 CHAR), 
    defaultrule VARCHAR2(128 CHAR), 
    timeoutpriority VARCHAR2(128 CHAR), 
    PRIMARY KEY (name), 
    CONSTRAINT yesno_values CHECK (ringinuse IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (setinterfacevar IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (setqueuevar IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (setqueueentryvar IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (announce_to_first_user IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (relative_periodic_announce IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (random_periodic_announce IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (autofill IN ('yes', 'no')), 
    CONSTRAINT queue_autopause_values CHECK (autopause IN ('yes', 'no', 'all')), 
    CONSTRAINT yesno_values CHECK (autopausebusy IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (autopauseunavail IN ('yes', 'no')), 
    CONSTRAINT queue_strategy_values CHECK (strategy IN ('ringall', 'leastrecent', 'fewestcalls', 'random', 'rrmemory', 'linear', 'wrandom', 'rrordered')), 
    CONSTRAINT yesno_values CHECK (reportholdtime IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (timeoutrestart IN ('yes', 'no'))
)

/

CREATE TABLE queue_members (
    queue_name VARCHAR2(80 CHAR) NOT NULL, 
    interface VARCHAR2(80 CHAR) NOT NULL, 
    uniqueid VARCHAR2(80 CHAR) NOT NULL, 
    membername VARCHAR2(80 CHAR), 
    state_interface VARCHAR2(80 CHAR), 
    penalty INTEGER, 
    paused INTEGER, 
    PRIMARY KEY (queue_name, interface)
)

/

UPDATE alembic_version SET version_num='28887f25a46f' WHERE alembic_version.version_num = '21e526ad3040'

/

-- Running upgrade 28887f25a46f -> 4c573e7135bd

ALTER TABLE ps_endpoints MODIFY tos_audio VARCHAR2(10 CHAR)

/

ALTER TABLE ps_endpoints MODIFY tos_video VARCHAR2(10 CHAR)

/

ALTER TABLE ps_endpoints DROP COLUMN cos_audio

/

ALTER TABLE ps_endpoints DROP COLUMN cos_video

/

ALTER TABLE ps_endpoints ADD cos_audio INTEGER

/

ALTER TABLE ps_endpoints ADD cos_video INTEGER

/

ALTER TABLE ps_transports MODIFY tos VARCHAR2(10 CHAR)

/

ALTER TABLE ps_transports DROP COLUMN cos

/

ALTER TABLE ps_transports ADD cos INTEGER

/

UPDATE alembic_version SET version_num='4c573e7135bd' WHERE alembic_version.version_num = '28887f25a46f'

/

-- Running upgrade 4c573e7135bd -> 3855ee4e5f85

ALTER TABLE ps_endpoints ADD message_context VARCHAR2(40 CHAR)

/

ALTER TABLE ps_contacts ADD user_agent VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='3855ee4e5f85' WHERE alembic_version.version_num = '4c573e7135bd'

/

-- Running upgrade 3855ee4e5f85 -> e96a0b8071c

ALTER TABLE ps_globals MODIFY user_agent VARCHAR2(255 CHAR)

/

ALTER TABLE ps_contacts MODIFY id VARCHAR2(255 CHAR)

/

ALTER TABLE ps_contacts MODIFY uri VARCHAR2(255 CHAR)

/

ALTER TABLE ps_contacts MODIFY user_agent VARCHAR2(255 CHAR)

/

ALTER TABLE ps_registrations MODIFY client_uri VARCHAR2(255 CHAR)

/

ALTER TABLE ps_registrations MODIFY server_uri VARCHAR2(255 CHAR)

/

UPDATE alembic_version SET version_num='e96a0b8071c' WHERE alembic_version.version_num = '3855ee4e5f85'

/

-- Running upgrade e96a0b8071c -> c6d929b23a8

CREATE TABLE ps_subscription_persistence (
    id VARCHAR2(40 CHAR) NOT NULL, 
    packet VARCHAR2(2048 CHAR), 
    src_name VARCHAR2(128 CHAR), 
    src_port INTEGER, 
    transport_key VARCHAR2(64 CHAR), 
    local_name VARCHAR2(128 CHAR), 
    local_port INTEGER, 
    cseq INTEGER, 
    tag VARCHAR2(128 CHAR), 
    endpoint VARCHAR2(40 CHAR), 
    expires INTEGER, 
    UNIQUE (id)
)

/

CREATE INDEX ps_subscription_persistence_id ON ps_subscription_persistence (id)

/

UPDATE alembic_version SET version_num='c6d929b23a8' WHERE alembic_version.version_num = 'e96a0b8071c'

/

-- Running upgrade c6d929b23a8 -> 51f8cb66540e

ALTER TABLE ps_endpoints ADD force_avp VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (force_avp IN ('yes', 'no'))

/

ALTER TABLE ps_endpoints ADD media_use_received_transport VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (media_use_received_transport IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='51f8cb66540e' WHERE alembic_version.version_num = 'c6d929b23a8'

/

-- Running upgrade 51f8cb66540e -> 1d50859ed02e

ALTER TABLE ps_endpoints ADD accountcode VARCHAR2(20 CHAR)

/

UPDATE alembic_version SET version_num='1d50859ed02e' WHERE alembic_version.version_num = '51f8cb66540e'

/

-- Running upgrade 1d50859ed02e -> 1758e8bbf6b

ALTER TABLE sippeers MODIFY useragent VARCHAR2(255 CHAR)

/

UPDATE alembic_version SET version_num='1758e8bbf6b' WHERE alembic_version.version_num = '1d50859ed02e'

/

-- Running upgrade 1758e8bbf6b -> 5139253c0423

ALTER TABLE queue_members DROP COLUMN uniqueid

/

ALTER TABLE queue_members ADD uniqueid INTEGER NOT NULL

/

ALTER TABLE queue_members ADD UNIQUE (uniqueid)

/

UPDATE alembic_version SET version_num='5139253c0423' WHERE alembic_version.version_num = '1758e8bbf6b'

/

-- Running upgrade 5139253c0423 -> d39508cb8d8

CREATE TABLE queue_rules (
    rule_name VARCHAR2(80 CHAR) NOT NULL, 
    time VARCHAR2(32 CHAR) NOT NULL, 
    min_penalty VARCHAR2(32 CHAR) NOT NULL, 
    max_penalty VARCHAR2(32 CHAR) NOT NULL
)

/

UPDATE alembic_version SET version_num='d39508cb8d8' WHERE alembic_version.version_num = '5139253c0423'

/

-- Running upgrade d39508cb8d8 -> 5950038a6ead

ALTER TABLE ps_transports MODIFY verifiy_server VARCHAR(3 CHAR)

/

ALTER TABLE ps_transports RENAME COLUMN verifiy_server TO verify_server

/

ALTER TABLE ps_transports ADD CONSTRAINT yesno_values CHECK (verifiy_server IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='5950038a6ead' WHERE alembic_version.version_num = 'd39508cb8d8'

/

-- Running upgrade 5950038a6ead -> 10aedae86a32

ALTER TABLE sippeers DROP CONSTRAINT sip_directmedia_values

/

ALTER TABLE sippeers MODIFY directmedia VARCHAR(8 CHAR)

/

ALTER TABLE sippeers ADD CONSTRAINT sip_directmedia_values_v2 CHECK (directmedia IN ('yes', 'no', 'nonat', 'update', 'outgoing'))

/

UPDATE alembic_version SET version_num='10aedae86a32' WHERE alembic_version.version_num = '5950038a6ead'

/

-- Running upgrade 10aedae86a32 -> 371a3bf4143e

ALTER TABLE ps_endpoints ADD user_eq_phone VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (user_eq_phone IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='371a3bf4143e' WHERE alembic_version.version_num = '10aedae86a32'

/

-- Running upgrade 371a3bf4143e -> 15b1430ad6f1

ALTER TABLE ps_endpoints ADD moh_passthrough VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (moh_passthrough IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='15b1430ad6f1' WHERE alembic_version.version_num = '371a3bf4143e'

/

-- Running upgrade 15b1430ad6f1 -> 945b1098bdd

ALTER TABLE ps_endpoints ADD media_encryption_optimistic VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (media_encryption_optimistic IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='945b1098bdd' WHERE alembic_version.version_num = '15b1430ad6f1'

/

-- Running upgrade 945b1098bdd -> 45e3f47c6c44

ALTER TABLE ps_globals ADD endpoint_identifier_order VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='45e3f47c6c44' WHERE alembic_version.version_num = '945b1098bdd'

/

-- Running upgrade 45e3f47c6c44 -> 23530d604b96

ALTER TABLE ps_endpoints ADD rpid_immediate VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (rpid_immediate IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='23530d604b96' WHERE alembic_version.version_num = '45e3f47c6c44'

/

-- Running upgrade 23530d604b96 -> 31cd4f4891ec

ALTER TABLE ps_endpoints DROP CONSTRAINT pjsip_dtmf_mode_values

/

ALTER TABLE ps_endpoints MODIFY dtmf_mode VARCHAR(7 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_dtmf_mode_values_v2 CHECK (dtmf_mode IN ('rfc4733', 'inband', 'info', 'auto'))

/

UPDATE alembic_version SET version_num='31cd4f4891ec' WHERE alembic_version.version_num = '23530d604b96'

/

-- Running upgrade 31cd4f4891ec -> 461d7d691209

ALTER TABLE ps_aors ADD qualify_timeout INTEGER

/

ALTER TABLE ps_contacts ADD qualify_timeout INTEGER

/

UPDATE alembic_version SET version_num='461d7d691209' WHERE alembic_version.version_num = '31cd4f4891ec'

/

-- Running upgrade 461d7d691209 -> a541e0b5e89

ALTER TABLE ps_globals ADD max_initial_qualify_time INTEGER

/

UPDATE alembic_version SET version_num='a541e0b5e89' WHERE alembic_version.version_num = '461d7d691209'

/

-- Running upgrade a541e0b5e89 -> 28b8e71e541f

ALTER TABLE ps_endpoints ADD g726_non_standard VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (g726_non_standard IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='28b8e71e541f' WHERE alembic_version.version_num = 'a541e0b5e89'

/

-- Running upgrade 28b8e71e541f -> 498357a710ae

ALTER TABLE ps_endpoints ADD rtp_keepalive INTEGER

/

UPDATE alembic_version SET version_num='498357a710ae' WHERE alembic_version.version_num = '28b8e71e541f'

/

-- Running upgrade 498357a710ae -> 26f10cadc157

ALTER TABLE ps_endpoints ADD rtp_timeout INTEGER

/

ALTER TABLE ps_endpoints ADD rtp_timeout_hold INTEGER

/

UPDATE alembic_version SET version_num='26f10cadc157' WHERE alembic_version.version_num = '498357a710ae'

/

-- Running upgrade 26f10cadc157 -> 154177371065

ALTER TABLE ps_globals ADD default_from_user VARCHAR2(80 CHAR)

/

UPDATE alembic_version SET version_num='154177371065' WHERE alembic_version.version_num = '26f10cadc157'

/

-- Running upgrade 154177371065 -> 28ce1e718f05

ALTER TABLE ps_registrations ADD fatal_retry_interval INTEGER

/

UPDATE alembic_version SET version_num='28ce1e718f05' WHERE alembic_version.version_num = '154177371065'

/

-- Running upgrade 28ce1e718f05 -> 339a3bdf53fc

ALTER TABLE ps_endpoints MODIFY accountcode VARCHAR2(80 CHAR)

/

ALTER TABLE sippeers MODIFY accountcode VARCHAR2(80 CHAR)

/

ALTER TABLE iaxfriends MODIFY accountcode VARCHAR2(80 CHAR)

/

UPDATE alembic_version SET version_num='339a3bdf53fc' WHERE alembic_version.version_num = '28ce1e718f05'

/

-- Running upgrade 339a3bdf53fc -> 189a235b3fd7

ALTER TABLE ps_globals ADD keep_alive_interval INTEGER

/

UPDATE alembic_version SET version_num='189a235b3fd7' WHERE alembic_version.version_num = '339a3bdf53fc'

/

-- Running upgrade 189a235b3fd7 -> 2d078ec071b7

ALTER TABLE ps_aors MODIFY contact VARCHAR2(255 CHAR)

/

UPDATE alembic_version SET version_num='2d078ec071b7' WHERE alembic_version.version_num = '189a235b3fd7'

/

-- Running upgrade 2d078ec071b7 -> 26d7f3bf0fa5

ALTER TABLE ps_endpoints ADD bind_rtp_to_media_address VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (bind_rtp_to_media_address IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='26d7f3bf0fa5' WHERE alembic_version.version_num = '2d078ec071b7'

/

-- Running upgrade 26d7f3bf0fa5 -> 136885b81223

ALTER TABLE ps_globals ADD regcontext VARCHAR2(80 CHAR)

/

UPDATE alembic_version SET version_num='136885b81223' WHERE alembic_version.version_num = '26d7f3bf0fa5'

/

-- Running upgrade 136885b81223 -> 423f34ad36e2

ALTER TABLE ps_aors MODIFY qualify_timeout FLOAT

/

ALTER TABLE ps_contacts MODIFY qualify_timeout FLOAT

/

UPDATE alembic_version SET version_num='423f34ad36e2' WHERE alembic_version.version_num = '136885b81223'

/

-- Running upgrade 423f34ad36e2 -> dbc44d5a908

ALTER TABLE ps_systems ADD disable_tcp_switch VARCHAR(3 CHAR)

/

ALTER TABLE ps_systems ADD CONSTRAINT yesno_values CHECK (disable_tcp_switch IN ('yes', 'no'))

/

ALTER TABLE ps_registrations ADD line VARCHAR(3 CHAR)

/

ALTER TABLE ps_registrations ADD CONSTRAINT yesno_values CHECK (line IN ('yes', 'no'))

/

ALTER TABLE ps_registrations ADD endpoint VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='dbc44d5a908' WHERE alembic_version.version_num = '423f34ad36e2'

/

-- Running upgrade dbc44d5a908 -> 3bcc0b5bc2c9

ALTER TABLE ps_transports ADD allow_reload VARCHAR(3 CHAR)

/

ALTER TABLE ps_transports ADD CONSTRAINT yesno_values CHECK (allow_reload IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='3bcc0b5bc2c9' WHERE alembic_version.version_num = 'dbc44d5a908'

/

-- Running upgrade 3bcc0b5bc2c9 -> 5813202e92be

ALTER TABLE ps_globals ADD contact_expiration_check_interval INTEGER

/

UPDATE alembic_version SET version_num='5813202e92be' WHERE alembic_version.version_num = '3bcc0b5bc2c9'

/

-- Running upgrade 5813202e92be -> 1c688d9a003c

ALTER TABLE ps_globals ADD default_voicemail_extension VARCHAR2(40 CHAR)

/

ALTER TABLE ps_aors ADD voicemail_extension VARCHAR2(40 CHAR)

/

ALTER TABLE ps_endpoints ADD voicemail_extension VARCHAR2(40 CHAR)

/

ALTER TABLE ps_endpoints ADD mwi_subscribe_replaces_unsolicited INTEGER

/

UPDATE alembic_version SET version_num='1c688d9a003c' WHERE alembic_version.version_num = '5813202e92be'

/

-- Running upgrade 1c688d9a003c -> 8d478ab86e29

ALTER TABLE ps_globals ADD disable_multi_domain VARCHAR(3 CHAR)

/

ALTER TABLE ps_globals ADD CONSTRAINT yesno_values CHECK (disable_multi_domain IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='8d478ab86e29' WHERE alembic_version.version_num = '1c688d9a003c'

/

-- Running upgrade 8d478ab86e29 -> 65eb22eb195

ALTER TABLE ps_globals ADD unidentified_request_count INTEGER

/

ALTER TABLE ps_globals ADD unidentified_request_period INTEGER

/

ALTER TABLE ps_globals ADD unidentified_request_prune_interval INTEGER

/

ALTER TABLE ps_globals ADD default_realm VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='65eb22eb195' WHERE alembic_version.version_num = '8d478ab86e29'

/

-- Running upgrade 65eb22eb195 -> 81b01a191a46

ALTER TABLE ps_contacts ADD reg_server VARCHAR2(20 CHAR)

/

ALTER TABLE ps_contacts ADD CONSTRAINT ps_contacts_uq UNIQUE (id, reg_server)

/

UPDATE alembic_version SET version_num='81b01a191a46' WHERE alembic_version.version_num = '65eb22eb195'

/

-- Running upgrade 81b01a191a46 -> 6be31516058d

ALTER TABLE ps_contacts ADD authenticate_qualify VARCHAR(3 CHAR)

/

ALTER TABLE ps_contacts ADD CONSTRAINT yesno_values CHECK (authenticate_qualify IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='6be31516058d' WHERE alembic_version.version_num = '81b01a191a46'

/

-- Running upgrade 6be31516058d -> d7e3c73eb2bf

ALTER TABLE ps_endpoints ADD deny VARCHAR2(95 CHAR)

/

ALTER TABLE ps_endpoints ADD permit VARCHAR2(95 CHAR)

/

ALTER TABLE ps_endpoints ADD acl VARCHAR2(40 CHAR)

/

ALTER TABLE ps_endpoints ADD contact_deny VARCHAR2(95 CHAR)

/

ALTER TABLE ps_endpoints ADD contact_permit VARCHAR2(95 CHAR)

/

ALTER TABLE ps_endpoints ADD contact_acl VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='d7e3c73eb2bf' WHERE alembic_version.version_num = '6be31516058d'

/

-- Running upgrade d7e3c73eb2bf -> a845e4d8ade8

ALTER TABLE ps_contacts ADD via_addr VARCHAR2(40 CHAR)

/

ALTER TABLE ps_contacts ADD via_port INTEGER

/

ALTER TABLE ps_contacts ADD call_id VARCHAR2(255 CHAR)

/

UPDATE alembic_version SET version_num='a845e4d8ade8' WHERE alembic_version.version_num = 'd7e3c73eb2bf'

/

-- Running upgrade a845e4d8ade8 -> ef7efc2d3964

ALTER TABLE ps_contacts ADD endpoint VARCHAR2(40 CHAR)

/

ALTER TABLE ps_contacts MODIFY expiration_time NUMBER(19)

/

CREATE INDEX ps_contacts_qualifyfreq_exp ON ps_contacts (qualify_frequency, expiration_time)

/

CREATE INDEX ps_aors_qualifyfreq_contact ON ps_aors (qualify_frequency, contact)

/

UPDATE alembic_version SET version_num='ef7efc2d3964' WHERE alembic_version.version_num = 'a845e4d8ade8'

/

-- Running upgrade ef7efc2d3964 -> 9deac0ae4717

ALTER TABLE ps_endpoints ADD subscribe_context VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='9deac0ae4717' WHERE alembic_version.version_num = 'ef7efc2d3964'

/

-- Running upgrade 9deac0ae4717 -> 4a6c67fa9b7a

ALTER TABLE ps_endpoints ADD fax_detect_timeout INTEGER

/

UPDATE alembic_version SET version_num='4a6c67fa9b7a' WHERE alembic_version.version_num = '9deac0ae4717'

/

-- Running upgrade 4a6c67fa9b7a -> c7a44a5a0851

ALTER TABLE ps_globals ADD mwi_tps_queue_high INTEGER

/

ALTER TABLE ps_globals ADD mwi_tps_queue_low INTEGER

/

ALTER TABLE ps_globals ADD mwi_disable_initial_unsolicited VARCHAR(3 CHAR)

/

ALTER TABLE ps_globals ADD CONSTRAINT yesno_values CHECK (mwi_disable_initial_unsolicited IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='c7a44a5a0851' WHERE alembic_version.version_num = '4a6c67fa9b7a'

/

-- Running upgrade c7a44a5a0851 -> 3772f8f828da

ALTER TABLE ps_endpoints MODIFY identify_by VARCHAR(13 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_identify_by_values CHECK (identify_by IN ('username', 'auth_username'))

/

UPDATE alembic_version SET version_num='3772f8f828da' WHERE alembic_version.version_num = 'c7a44a5a0851'

/

-- Running upgrade 3772f8f828da -> 4e2493ef32e6

ALTER TABLE ps_endpoints ADD contact_user VARCHAR2(80 CHAR)

/

UPDATE alembic_version SET version_num='4e2493ef32e6' WHERE alembic_version.version_num = '3772f8f828da'

/

-- Running upgrade 4e2493ef32e6 -> 7f3e21abe318

ALTER TABLE ps_endpoints ADD preferred_codec_only VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (preferred_codec_only IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='7f3e21abe318' WHERE alembic_version.version_num = '4e2493ef32e6'

/

-- Running upgrade 7f3e21abe318 -> a6ef36f1309

ALTER TABLE ps_globals ADD ignore_uri_user_options VARCHAR(3 CHAR)

/

ALTER TABLE ps_globals ADD CONSTRAINT yesno_values CHECK (ignore_uri_user_options IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='a6ef36f1309' WHERE alembic_version.version_num = '7f3e21abe318'

/

-- Running upgrade a6ef36f1309 -> 4468b4a91372

ALTER TABLE ps_endpoints ADD asymmetric_rtp_codec VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (asymmetric_rtp_codec IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='4468b4a91372' WHERE alembic_version.version_num = 'a6ef36f1309'

/

-- Running upgrade 4468b4a91372 -> 28ab27a7826d

ALTER TABLE ps_endpoint_id_ips ADD srv_lookups VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoint_id_ips ADD CONSTRAINT yesno_values CHECK (srv_lookups IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='28ab27a7826d' WHERE alembic_version.version_num = '4468b4a91372'

/

-- Running upgrade 28ab27a7826d -> 465e70e8c337

ALTER TABLE ps_endpoint_id_ips ADD match_header VARCHAR2(255 CHAR)

/

UPDATE alembic_version SET version_num='465e70e8c337' WHERE alembic_version.version_num = '28ab27a7826d'

/

-- Running upgrade 465e70e8c337 -> 15db7b91a97a

ALTER TABLE ps_endpoints ADD rtcp_mux VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (rtcp_mux IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='15db7b91a97a' WHERE alembic_version.version_num = '465e70e8c337'

/

-- Running upgrade 15db7b91a97a -> f638dbe2eb23

ALTER TABLE ps_transports ADD symmetric_transport VARCHAR(3 CHAR)

/

ALTER TABLE ps_transports ADD CONSTRAINT yesno_values CHECK (symmetric_transport IN ('yes', 'no'))

/

ALTER TABLE ps_subscription_persistence ADD contact_uri VARCHAR2(256 CHAR)

/

UPDATE alembic_version SET version_num='f638dbe2eb23' WHERE alembic_version.version_num = '15db7b91a97a'

/

-- Running upgrade f638dbe2eb23 -> 8fce4c573e15

ALTER TABLE ps_endpoints ADD allow_overlap VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (allow_overlap IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='8fce4c573e15' WHERE alembic_version.version_num = 'f638dbe2eb23'

/

-- Running upgrade 8fce4c573e15 -> 2da192dbbc65

CREATE TABLE ps_outbound_publishes (
    id VARCHAR2(40 CHAR) NOT NULL, 
    expiration INTEGER, 
    outbound_auth VARCHAR2(40 CHAR), 
    outbound_proxy VARCHAR2(256 CHAR), 
    server_uri VARCHAR2(256 CHAR), 
    from_uri VARCHAR2(256 CHAR), 
    to_uri VARCHAR2(256 CHAR), 
    event VARCHAR2(40 CHAR), 
    max_auth_attempts INTEGER, 
    transport VARCHAR2(40 CHAR), 
    multi_user VARCHAR(3 CHAR), 
    "@body" VARCHAR2(40 CHAR), 
    "@context" VARCHAR2(256 CHAR), 
    "@exten" VARCHAR2(256 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (multi_user IN ('yes', 'no'))
)

/

CREATE INDEX ps_outbound_publishes_id ON ps_outbound_publishes (id)

/

CREATE TABLE ps_inbound_publications (
    id VARCHAR2(40 CHAR) NOT NULL, 
    endpoint VARCHAR2(40 CHAR), 
    "event_asterisk-devicestate" VARCHAR2(40 CHAR), 
    "event_asterisk-mwi" VARCHAR2(40 CHAR), 
    UNIQUE (id)
)

/

CREATE INDEX ps_inbound_publications_id ON ps_inbound_publications (id)

/

CREATE TABLE ps_asterisk_publications (
    id VARCHAR2(40 CHAR) NOT NULL, 
    devicestate_publish VARCHAR2(40 CHAR), 
    mailboxstate_publish VARCHAR2(40 CHAR), 
    device_state VARCHAR(3 CHAR), 
    device_state_filter VARCHAR2(256 CHAR), 
    mailbox_state VARCHAR(3 CHAR), 
    mailbox_state_filter VARCHAR2(256 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (device_state IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (mailbox_state IN ('yes', 'no'))
)

/

CREATE INDEX ps_asterisk_publications_id ON ps_asterisk_publications (id)

/

UPDATE alembic_version SET version_num='2da192dbbc65' WHERE alembic_version.version_num = '8fce4c573e15'

/

-- Running upgrade 2da192dbbc65 -> 1d0e332c32af

CREATE TABLE ps_resource_list (
    id VARCHAR2(40 CHAR) NOT NULL, 
    list_item VARCHAR2(2048 CHAR), 
    event VARCHAR2(40 CHAR), 
    full_state VARCHAR(3 CHAR), 
    notification_batch_interval INTEGER, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (full_state IN ('yes', 'no'))
)

/

CREATE INDEX ps_resource_list_id ON ps_resource_list (id)

/

UPDATE alembic_version SET version_num='1d0e332c32af' WHERE alembic_version.version_num = '2da192dbbc65'

/

-- Running upgrade 1d0e332c32af -> 86bb1efa278d

ALTER TABLE ps_endpoints ADD refer_blind_progress VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (refer_blind_progress IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='86bb1efa278d' WHERE alembic_version.version_num = '1d0e332c32af'

/

-- Running upgrade 86bb1efa278d -> d7983954dd96

ALTER TABLE ps_endpoints ADD notify_early_inuse_ringing VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (notify_early_inuse_ringing IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='d7983954dd96' WHERE alembic_version.version_num = '86bb1efa278d'

/

-- Running upgrade d7983954dd96 -> 39959b9c2566

ALTER TABLE ps_endpoints ADD max_audio_streams INTEGER

/

ALTER TABLE ps_endpoints ADD max_video_streams INTEGER

/

UPDATE alembic_version SET version_num='39959b9c2566' WHERE alembic_version.version_num = 'd7983954dd96'

/

-- Running upgrade 39959b9c2566 -> 164abbd708c

ALTER TABLE ps_endpoints DROP CONSTRAINT pjsip_dtmf_mode_values_v2

/

ALTER TABLE ps_endpoints MODIFY dtmf_mode VARCHAR(9 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_dtmf_mode_values_v3 CHECK (dtmf_mode IN ('rfc4733', 'inband', 'info', 'auto', 'auto_info'))

/

UPDATE alembic_version SET version_num='164abbd708c' WHERE alembic_version.version_num = '39959b9c2566'

/

-- Running upgrade 164abbd708c -> 44ccced114ce

ALTER TABLE ps_endpoints ADD webrtc VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (webrtc IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='44ccced114ce' WHERE alembic_version.version_num = '164abbd708c'

/

-- Running upgrade 44ccced114ce -> f3d1c5d38b56

ALTER TABLE ps_contacts ADD prune_on_boot VARCHAR(3 CHAR)

/

ALTER TABLE ps_contacts ADD CONSTRAINT yesno_values CHECK (prune_on_boot IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='f3d1c5d38b56' WHERE alembic_version.version_num = '44ccced114ce'

/

-- Running upgrade f3d1c5d38b56 -> b83645976fdd

ALTER TABLE ps_endpoints ADD dtls_fingerprint VARCHAR(7 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT sha_hash_values CHECK (dtls_fingerprint IN ('SHA-1', 'SHA-256'))

/

UPDATE alembic_version SET version_num='b83645976fdd' WHERE alembic_version.version_num = 'f3d1c5d38b56'

/

-- Running upgrade b83645976fdd -> a1698e8bb9c5

ALTER TABLE ps_endpoints ADD incoming_mwi_mailbox VARCHAR2(40 CHAR)

/

UPDATE alembic_version SET version_num='a1698e8bb9c5' WHERE alembic_version.version_num = 'b83645976fdd'

/

-- Running upgrade a1698e8bb9c5 -> 20abce6d1e3c

ALTER TABLE ps_endpoints MODIFY identify_by VARCHAR(13 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_identify_by_values CHECK (identify_by IN ('username', 'auth_username', 'ip'))

/

UPDATE alembic_version SET version_num='20abce6d1e3c' WHERE alembic_version.version_num = 'a1698e8bb9c5'

/

-- Running upgrade 20abce6d1e3c -> de83fac997e2

ALTER TABLE ps_endpoints ADD bundle VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (bundle IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='de83fac997e2' WHERE alembic_version.version_num = '20abce6d1e3c'

/

-- Running upgrade de83fac997e2 -> 041c0d3d1857

ALTER TABLE ps_endpoints ADD dtls_auto_generate_cert VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (dtls_auto_generate_cert IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='041c0d3d1857' WHERE alembic_version.version_num = 'de83fac997e2'

/

-- Running upgrade 041c0d3d1857 -> e2f04d309071

ALTER TABLE queue_members ADD wrapuptime INTEGER

/

UPDATE alembic_version SET version_num='e2f04d309071' WHERE alembic_version.version_num = '041c0d3d1857'

/

-- Running upgrade e2f04d309071 -> 52798ad97bdf

ALTER TABLE ps_endpoints MODIFY identify_by VARCHAR2(80 CHAR)

/

UPDATE alembic_version SET version_num='52798ad97bdf' WHERE alembic_version.version_num = 'e2f04d309071'

/

-- Running upgrade 52798ad97bdf -> d3e4284f8707

ALTER TABLE ps_subscription_persistence ADD prune_on_boot VARCHAR(3 CHAR)

/

ALTER TABLE ps_subscription_persistence ADD CONSTRAINT yesno_values CHECK (prune_on_boot IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='d3e4284f8707' WHERE alembic_version.version_num = '52798ad97bdf'

/

-- Running upgrade d3e4284f8707 -> 0be05c3a8225

ALTER TABLE ps_systems ADD follow_early_media_fork VARCHAR(3 CHAR)

/

ALTER TABLE ps_systems ADD CONSTRAINT yesno_values CHECK (follow_early_media_fork IN ('yes', 'no'))

/

ALTER TABLE ps_systems ADD accept_multiple_sdp_answers VARCHAR(3 CHAR)

/

ALTER TABLE ps_systems ADD CONSTRAINT yesno_values CHECK (accept_multiple_sdp_answers IN ('yes', 'no'))

/

ALTER TABLE ps_endpoints ADD follow_early_media_fork VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (follow_early_media_fork IN ('yes', 'no'))

/

ALTER TABLE ps_endpoints ADD accept_multiple_sdp_answers VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (accept_multiple_sdp_answers IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='0be05c3a8225' WHERE alembic_version.version_num = 'd3e4284f8707'

/

-- Running upgrade 0be05c3a8225 -> 19b00bc19b7b

ALTER TABLE ps_endpoints ADD suppress_q850_reason_header VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (suppress_q850_reason_header IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='19b00bc19b7b' WHERE alembic_version.version_num = '0be05c3a8225'

/

-- Running upgrade 19b00bc19b7b -> 1d3ed26d9978

ALTER TABLE ps_contacts MODIFY uri VARCHAR2(511 CHAR)

/

UPDATE alembic_version SET version_num='1d3ed26d9978' WHERE alembic_version.version_num = '19b00bc19b7b'

/

-- Running upgrade 1d3ed26d9978 -> fe6592859b85

ALTER TABLE ps_endpoints MODIFY mwi_subscribe_replaces_unsolicited VARCHAR2(5 CHAR)

/

ALTER TABLE ps_endpoints MODIFY mwi_subscribe_replaces_unsolicited VARCHAR(5 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT ast_bool_values CHECK (mwi_subscribe_replaces_unsolicited IN ('0', '1', 'off', 'on', 'false', 'true', 'no', 'yes'))

/

UPDATE alembic_version SET version_num='fe6592859b85' WHERE alembic_version.version_num = '1d3ed26d9978'

/

-- Running upgrade fe6592859b85 -> 7f85dd44c775

ALTER TABLE ps_endpoints MODIFY suppress_q850_reason_header VARCHAR(3 CHAR)

/

ALTER TABLE ps_endpoints RENAME COLUMN suppress_q850_reason_header TO suppress_q850_reason_headers

/

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (suppress_q850_reason_header IN ('yes', 'no'))

/

UPDATE alembic_version SET version_num='7f85dd44c775' WHERE alembic_version.version_num = 'fe6592859b85'

/

-- Running upgrade 7f85dd44c775 -> 2bb1a85135ad

ALTER TABLE ps_globals ADD use_callerid_contact VARCHAR(5 CHAR)

/

ALTER TABLE ps_globals ADD CONSTRAINT ast_bool_values CHECK (use_callerid_contact IN ('0', '1', 'off', 'on', 'false', 'true', 'no', 'yes'))

/

UPDATE alembic_version SET version_num='2bb1a85135ad' WHERE alembic_version.version_num = '7f85dd44c775'

/

-- Running upgrade 2bb1a85135ad -> 1ac563b350a8

ALTER TABLE ps_endpoints ADD trust_connected_line VARCHAR(5 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT ast_bool_values CHECK (trust_connected_line IN ('0', '1', 'off', 'on', 'false', 'true', 'no', 'yes'))

/

ALTER TABLE ps_endpoints ADD send_connected_line VARCHAR(5 CHAR)

/

ALTER TABLE ps_endpoints ADD CONSTRAINT ast_bool_values CHECK (send_connected_line IN ('0', '1', 'off', 'on', 'false', 'true', 'no', 'yes'))

/

UPDATE alembic_version SET version_num='1ac563b350a8' WHERE alembic_version.version_num = '2bb1a85135ad'

/

