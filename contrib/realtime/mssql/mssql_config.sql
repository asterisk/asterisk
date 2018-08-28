BEGIN TRANSACTION;

CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL
);

GO

-- Running upgrade  -> 4da0c5f79a9c

CREATE TABLE sippeers (
    id INTEGER NOT NULL IDENTITY(1,1), 
    name VARCHAR(40) NOT NULL, 
    ipaddr VARCHAR(45) NULL, 
    port INTEGER NULL, 
    regseconds INTEGER NULL, 
    defaultuser VARCHAR(40) NULL, 
    fullcontact VARCHAR(80) NULL, 
    regserver VARCHAR(20) NULL, 
    useragent VARCHAR(20) NULL, 
    lastms INTEGER NULL, 
    host VARCHAR(40) NULL, 
    type VARCHAR(6) NULL, 
    context VARCHAR(40) NULL, 
    permit VARCHAR(95) NULL, 
    [deny] VARCHAR(95) NULL, 
    secret VARCHAR(40) NULL, 
    md5secret VARCHAR(40) NULL, 
    remotesecret VARCHAR(40) NULL, 
    transport VARCHAR(7) NULL, 
    dtmfmode VARCHAR(9) NULL, 
    directmedia VARCHAR(6) NULL, 
    nat VARCHAR(29) NULL, 
    callgroup VARCHAR(40) NULL, 
    pickupgroup VARCHAR(40) NULL, 
    language VARCHAR(40) NULL, 
    disallow VARCHAR(200) NULL, 
    allow VARCHAR(200) NULL, 
    insecure VARCHAR(40) NULL, 
    trustrpid VARCHAR(3) NULL, 
    progressinband VARCHAR(5) NULL, 
    promiscredir VARCHAR(3) NULL, 
    useclientcode VARCHAR(3) NULL, 
    accountcode VARCHAR(40) NULL, 
    setvar VARCHAR(200) NULL, 
    callerid VARCHAR(40) NULL, 
    amaflags VARCHAR(40) NULL, 
    callcounter VARCHAR(3) NULL, 
    busylevel INTEGER NULL, 
    allowoverlap VARCHAR(3) NULL, 
    allowsubscribe VARCHAR(3) NULL, 
    videosupport VARCHAR(3) NULL, 
    maxcallbitrate INTEGER NULL, 
    rfc2833compensate VARCHAR(3) NULL, 
    mailbox VARCHAR(40) NULL, 
    [session-timers] VARCHAR(9) NULL, 
    [session-expires] INTEGER NULL, 
    [session-minse] INTEGER NULL, 
    [session-refresher] VARCHAR(3) NULL, 
    t38pt_usertpsource VARCHAR(40) NULL, 
    regexten VARCHAR(40) NULL, 
    fromdomain VARCHAR(40) NULL, 
    fromuser VARCHAR(40) NULL, 
    qualify VARCHAR(40) NULL, 
    defaultip VARCHAR(45) NULL, 
    rtptimeout INTEGER NULL, 
    rtpholdtimeout INTEGER NULL, 
    sendrpid VARCHAR(3) NULL, 
    outboundproxy VARCHAR(40) NULL, 
    callbackextension VARCHAR(40) NULL, 
    timert1 INTEGER NULL, 
    timerb INTEGER NULL, 
    qualifyfreq INTEGER NULL, 
    constantssrc VARCHAR(3) NULL, 
    contactpermit VARCHAR(95) NULL, 
    contactdeny VARCHAR(95) NULL, 
    usereqphone VARCHAR(3) NULL, 
    textsupport VARCHAR(3) NULL, 
    faxdetect VARCHAR(3) NULL, 
    buggymwi VARCHAR(3) NULL, 
    auth VARCHAR(40) NULL, 
    fullname VARCHAR(40) NULL, 
    trunkname VARCHAR(40) NULL, 
    cid_number VARCHAR(40) NULL, 
    callingpres VARCHAR(21) NULL, 
    mohinterpret VARCHAR(40) NULL, 
    mohsuggest VARCHAR(40) NULL, 
    parkinglot VARCHAR(40) NULL, 
    hasvoicemail VARCHAR(3) NULL, 
    subscribemwi VARCHAR(3) NULL, 
    vmexten VARCHAR(40) NULL, 
    autoframing VARCHAR(3) NULL, 
    rtpkeepalive INTEGER NULL, 
    [call-limit] INTEGER NULL, 
    g726nonstandard VARCHAR(3) NULL, 
    ignoresdpversion VARCHAR(3) NULL, 
    allowtransfer VARCHAR(3) NULL, 
    dynamic VARCHAR(3) NULL, 
    path VARCHAR(256) NULL, 
    supportpath VARCHAR(3) NULL, 
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
    CONSTRAINT sip_session_timers_values CHECK ([session-timers] IN ('accept', 'refuse', 'originate')), 
    CONSTRAINT sip_session_refresher_values CHECK ([session-refresher] IN ('uac', 'uas')), 
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
);

GO

CREATE INDEX sippeers_name ON sippeers (name);

GO

CREATE INDEX sippeers_name_host ON sippeers (name, host);

GO

CREATE INDEX sippeers_ipaddr_port ON sippeers (ipaddr, port);

GO

CREATE INDEX sippeers_host_port ON sippeers (host, port);

GO

CREATE TABLE iaxfriends (
    id INTEGER NOT NULL IDENTITY(1,1), 
    name VARCHAR(40) NOT NULL, 
    type VARCHAR(6) NULL, 
    username VARCHAR(40) NULL, 
    mailbox VARCHAR(40) NULL, 
    secret VARCHAR(40) NULL, 
    dbsecret VARCHAR(40) NULL, 
    context VARCHAR(40) NULL, 
    regcontext VARCHAR(40) NULL, 
    host VARCHAR(40) NULL, 
    ipaddr VARCHAR(40) NULL, 
    port INTEGER NULL, 
    defaultip VARCHAR(20) NULL, 
    sourceaddress VARCHAR(20) NULL, 
    mask VARCHAR(20) NULL, 
    regexten VARCHAR(40) NULL, 
    regseconds INTEGER NULL, 
    accountcode VARCHAR(20) NULL, 
    mohinterpret VARCHAR(20) NULL, 
    mohsuggest VARCHAR(20) NULL, 
    inkeys VARCHAR(40) NULL, 
    outkeys VARCHAR(40) NULL, 
    language VARCHAR(10) NULL, 
    callerid VARCHAR(100) NULL, 
    cid_number VARCHAR(40) NULL, 
    sendani VARCHAR(3) NULL, 
    fullname VARCHAR(40) NULL, 
    trunk VARCHAR(3) NULL, 
    auth VARCHAR(20) NULL, 
    maxauthreq INTEGER NULL, 
    requirecalltoken VARCHAR(4) NULL, 
    encryption VARCHAR(6) NULL, 
    transfer VARCHAR(9) NULL, 
    jitterbuffer VARCHAR(3) NULL, 
    forcejitterbuffer VARCHAR(3) NULL, 
    disallow VARCHAR(200) NULL, 
    allow VARCHAR(200) NULL, 
    codecpriority VARCHAR(40) NULL, 
    qualify VARCHAR(10) NULL, 
    qualifysmoothing VARCHAR(3) NULL, 
    qualifyfreqok VARCHAR(10) NULL, 
    qualifyfreqnotok VARCHAR(10) NULL, 
    timezone VARCHAR(20) NULL, 
    adsi VARCHAR(3) NULL, 
    amaflags VARCHAR(20) NULL, 
    setvar VARCHAR(200) NULL, 
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
);

GO

CREATE INDEX iaxfriends_name ON iaxfriends (name);

GO

CREATE INDEX iaxfriends_name_host ON iaxfriends (name, host);

GO

CREATE INDEX iaxfriends_name_ipaddr_port ON iaxfriends (name, ipaddr, port);

GO

CREATE INDEX iaxfriends_ipaddr_port ON iaxfriends (ipaddr, port);

GO

CREATE INDEX iaxfriends_host_port ON iaxfriends (host, port);

GO

CREATE TABLE voicemail (
    uniqueid INTEGER NOT NULL IDENTITY(1,1), 
    context VARCHAR(80) NOT NULL, 
    mailbox VARCHAR(80) NOT NULL, 
    password VARCHAR(80) NOT NULL, 
    fullname VARCHAR(80) NULL, 
    alias VARCHAR(80) NULL, 
    email VARCHAR(80) NULL, 
    pager VARCHAR(80) NULL, 
    attach VARCHAR(3) NULL, 
    attachfmt VARCHAR(10) NULL, 
    serveremail VARCHAR(80) NULL, 
    language VARCHAR(20) NULL, 
    tz VARCHAR(30) NULL, 
    deletevoicemail VARCHAR(3) NULL, 
    saycid VARCHAR(3) NULL, 
    sendvoicemail VARCHAR(3) NULL, 
    review VARCHAR(3) NULL, 
    tempgreetwarn VARCHAR(3) NULL, 
    operator VARCHAR(3) NULL, 
    envelope VARCHAR(3) NULL, 
    sayduration INTEGER NULL, 
    forcename VARCHAR(3) NULL, 
    forcegreetings VARCHAR(3) NULL, 
    callback VARCHAR(80) NULL, 
    dialout VARCHAR(80) NULL, 
    exitcontext VARCHAR(80) NULL, 
    maxmsg INTEGER NULL, 
    volgain NUMERIC(5, 2) NULL, 
    imapuser VARCHAR(80) NULL, 
    imappassword VARCHAR(80) NULL, 
    imapserver VARCHAR(80) NULL, 
    imapport VARCHAR(8) NULL, 
    imapflags VARCHAR(80) NULL, 
    stamp DATETIME NULL, 
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
);

GO

CREATE INDEX voicemail_mailbox ON voicemail (mailbox);

GO

CREATE INDEX voicemail_context ON voicemail (context);

GO

CREATE INDEX voicemail_mailbox_context ON voicemail (mailbox, context);

GO

CREATE INDEX voicemail_imapuser ON voicemail (imapuser);

GO

CREATE TABLE meetme (
    bookid INTEGER NOT NULL IDENTITY(1,1), 
    confno VARCHAR(80) NOT NULL, 
    starttime DATETIME NULL, 
    endtime DATETIME NULL, 
    pin VARCHAR(20) NULL, 
    adminpin VARCHAR(20) NULL, 
    opts VARCHAR(20) NULL, 
    adminopts VARCHAR(20) NULL, 
    recordingfilename VARCHAR(80) NULL, 
    recordingformat VARCHAR(10) NULL, 
    maxusers INTEGER NULL, 
    members INTEGER NOT NULL, 
    PRIMARY KEY (bookid)
);

GO

CREATE INDEX meetme_confno_start_end ON meetme (confno, starttime, endtime);

GO

CREATE TABLE musiconhold (
    name VARCHAR(80) NOT NULL, 
    mode VARCHAR(10) NULL, 
    directory VARCHAR(255) NULL, 
    application VARCHAR(255) NULL, 
    digit VARCHAR(1) NULL, 
    sort VARCHAR(10) NULL, 
    format VARCHAR(10) NULL, 
    stamp DATETIME NULL, 
    PRIMARY KEY (name), 
    CONSTRAINT moh_mode_values CHECK (mode IN ('custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3'))
);

GO

INSERT INTO alembic_version (version_num) VALUES ('4da0c5f79a9c');

GO

-- Running upgrade 4da0c5f79a9c -> 43956d550a44

CREATE TABLE ps_endpoints (
    id VARCHAR(40) NOT NULL, 
    transport VARCHAR(40) NULL, 
    aors VARCHAR(200) NULL, 
    auth VARCHAR(40) NULL, 
    context VARCHAR(40) NULL, 
    disallow VARCHAR(200) NULL, 
    allow VARCHAR(200) NULL, 
    direct_media VARCHAR(3) NULL, 
    connected_line_method VARCHAR(8) NULL, 
    direct_media_method VARCHAR(8) NULL, 
    direct_media_glare_mitigation VARCHAR(8) NULL, 
    disable_direct_media_on_nat VARCHAR(3) NULL, 
    dtmf_mode VARCHAR(7) NULL, 
    external_media_address VARCHAR(40) NULL, 
    force_rport VARCHAR(3) NULL, 
    ice_support VARCHAR(3) NULL, 
    identify_by VARCHAR(8) NULL, 
    mailboxes VARCHAR(40) NULL, 
    moh_suggest VARCHAR(40) NULL, 
    outbound_auth VARCHAR(40) NULL, 
    outbound_proxy VARCHAR(40) NULL, 
    rewrite_contact VARCHAR(3) NULL, 
    rtp_ipv6 VARCHAR(3) NULL, 
    rtp_symmetric VARCHAR(3) NULL, 
    send_diversion VARCHAR(3) NULL, 
    send_pai VARCHAR(3) NULL, 
    send_rpid VARCHAR(3) NULL, 
    timers_min_se INTEGER NULL, 
    timers VARCHAR(8) NULL, 
    timers_sess_expires INTEGER NULL, 
    callerid VARCHAR(40) NULL, 
    callerid_privacy VARCHAR(23) NULL, 
    callerid_tag VARCHAR(40) NULL, 
    [100rel] VARCHAR(8) NULL, 
    aggregate_mwi VARCHAR(3) NULL, 
    trust_id_inbound VARCHAR(3) NULL, 
    trust_id_outbound VARCHAR(3) NULL, 
    use_ptime VARCHAR(3) NULL, 
    use_avpf VARCHAR(3) NULL, 
    media_encryption VARCHAR(4) NULL, 
    inband_progress VARCHAR(3) NULL, 
    call_group VARCHAR(40) NULL, 
    pickup_group VARCHAR(40) NULL, 
    named_call_group VARCHAR(40) NULL, 
    named_pickup_group VARCHAR(40) NULL, 
    device_state_busy_at INTEGER NULL, 
    fax_detect VARCHAR(3) NULL, 
    t38_udptl VARCHAR(3) NULL, 
    t38_udptl_ec VARCHAR(10) NULL, 
    t38_udptl_maxdatagram INTEGER NULL, 
    t38_udptl_nat VARCHAR(3) NULL, 
    t38_udptl_ipv6 VARCHAR(3) NULL, 
    tone_zone VARCHAR(40) NULL, 
    language VARCHAR(40) NULL, 
    one_touch_recording VARCHAR(3) NULL, 
    record_on_feature VARCHAR(40) NULL, 
    record_off_feature VARCHAR(40) NULL, 
    rtp_engine VARCHAR(40) NULL, 
    allow_transfer VARCHAR(3) NULL, 
    allow_subscribe VARCHAR(3) NULL, 
    sdp_owner VARCHAR(40) NULL, 
    sdp_session VARCHAR(40) NULL, 
    tos_audio INTEGER NULL, 
    tos_video INTEGER NULL, 
    cos_audio INTEGER NULL, 
    cos_video INTEGER NULL, 
    sub_min_expiry INTEGER NULL, 
    from_domain VARCHAR(40) NULL, 
    from_user VARCHAR(40) NULL, 
    mwi_fromuser VARCHAR(40) NULL, 
    dtls_verify VARCHAR(40) NULL, 
    dtls_rekey VARCHAR(40) NULL, 
    dtls_cert_file VARCHAR(200) NULL, 
    dtls_private_key VARCHAR(200) NULL, 
    dtls_cipher VARCHAR(200) NULL, 
    dtls_ca_file VARCHAR(200) NULL, 
    dtls_ca_path VARCHAR(200) NULL, 
    dtls_setup VARCHAR(7) NULL, 
    srtp_tag_32 VARCHAR(3) NULL, 
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
    CONSTRAINT pjsip_100rel_values CHECK ([100rel] IN ('no', 'required', 'yes')), 
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
);

GO

CREATE INDEX ps_endpoints_id ON ps_endpoints (id);

GO

CREATE TABLE ps_auths (
    id VARCHAR(40) NOT NULL, 
    auth_type VARCHAR(8) NULL, 
    nonce_lifetime INTEGER NULL, 
    md5_cred VARCHAR(40) NULL, 
    password VARCHAR(80) NULL, 
    realm VARCHAR(40) NULL, 
    username VARCHAR(40) NULL, 
    UNIQUE (id), 
    CONSTRAINT pjsip_auth_type_values CHECK (auth_type IN ('md5', 'userpass'))
);

GO

CREATE INDEX ps_auths_id ON ps_auths (id);

GO

CREATE TABLE ps_aors (
    id VARCHAR(40) NOT NULL, 
    contact VARCHAR(40) NULL, 
    default_expiration INTEGER NULL, 
    mailboxes VARCHAR(80) NULL, 
    max_contacts INTEGER NULL, 
    minimum_expiration INTEGER NULL, 
    remove_existing VARCHAR(3) NULL, 
    qualify_frequency INTEGER NULL, 
    authenticate_qualify VARCHAR(3) NULL, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (remove_existing IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (authenticate_qualify IN ('yes', 'no'))
);

GO

CREATE INDEX ps_aors_id ON ps_aors (id);

GO

CREATE TABLE ps_contacts (
    id VARCHAR(40) NOT NULL, 
    uri VARCHAR(40) NULL, 
    expiration_time VARCHAR(40) NULL, 
    qualify_frequency INTEGER NULL, 
    UNIQUE (id)
);

GO

CREATE INDEX ps_contacts_id ON ps_contacts (id);

GO

CREATE TABLE ps_domain_aliases (
    id VARCHAR(40) NOT NULL, 
    domain VARCHAR(80) NULL, 
    UNIQUE (id)
);

GO

CREATE INDEX ps_domain_aliases_id ON ps_domain_aliases (id);

GO

CREATE TABLE ps_endpoint_id_ips (
    id VARCHAR(40) NOT NULL, 
    endpoint VARCHAR(40) NULL, 
    match VARCHAR(80) NULL, 
    UNIQUE (id)
);

GO

CREATE INDEX ps_endpoint_id_ips_id ON ps_endpoint_id_ips (id);

GO

UPDATE alembic_version SET version_num='43956d550a44' WHERE alembic_version.version_num = '4da0c5f79a9c';

GO

-- Running upgrade 43956d550a44 -> 581a4264e537

CREATE TABLE extensions (
    id BIGINT NOT NULL IDENTITY(1,1), 
    context VARCHAR(40) NOT NULL, 
    exten VARCHAR(40) NOT NULL, 
    priority INTEGER NOT NULL, 
    app VARCHAR(40) NOT NULL, 
    appdata VARCHAR(256) NOT NULL, 
    PRIMARY KEY (id), 
    UNIQUE (context, exten, priority), 
    UNIQUE (id)
);

GO

UPDATE alembic_version SET version_num='581a4264e537' WHERE alembic_version.version_num = '43956d550a44';

GO

-- Running upgrade 581a4264e537 -> 2fc7930b41b3

CREATE TABLE ps_systems (
    id VARCHAR(40) NOT NULL, 
    timer_t1 INTEGER NULL, 
    timer_b INTEGER NULL, 
    compact_headers VARCHAR(3) NULL, 
    threadpool_initial_size INTEGER NULL, 
    threadpool_auto_increment INTEGER NULL, 
    threadpool_idle_timeout INTEGER NULL, 
    threadpool_max_size INTEGER NULL, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (compact_headers IN ('yes', 'no'))
);

GO

CREATE INDEX ps_systems_id ON ps_systems (id);

GO

CREATE TABLE ps_globals (
    id VARCHAR(40) NOT NULL, 
    max_forwards INTEGER NULL, 
    user_agent VARCHAR(40) NULL, 
    default_outbound_endpoint VARCHAR(40) NULL, 
    UNIQUE (id)
);

GO

CREATE INDEX ps_globals_id ON ps_globals (id);

GO

CREATE TABLE ps_transports (
    id VARCHAR(40) NOT NULL, 
    async_operations INTEGER NULL, 
    bind VARCHAR(40) NULL, 
    ca_list_file VARCHAR(200) NULL, 
    cert_file VARCHAR(200) NULL, 
    cipher VARCHAR(200) NULL, 
    domain VARCHAR(40) NULL, 
    external_media_address VARCHAR(40) NULL, 
    external_signaling_address VARCHAR(40) NULL, 
    external_signaling_port INTEGER NULL, 
    method VARCHAR(11) NULL, 
    local_net VARCHAR(40) NULL, 
    password VARCHAR(40) NULL, 
    priv_key_file VARCHAR(200) NULL, 
    protocol VARCHAR(3) NULL, 
    require_client_cert VARCHAR(3) NULL, 
    verify_client VARCHAR(3) NULL, 
    verifiy_server VARCHAR(3) NULL, 
    tos VARCHAR(3) NULL, 
    cos VARCHAR(3) NULL, 
    UNIQUE (id), 
    CONSTRAINT pjsip_transport_method_values CHECK (method IN ('default', 'unspecified', 'tlsv1', 'sslv2', 'sslv3', 'sslv23')), 
    CONSTRAINT pjsip_transport_protocol_values CHECK (protocol IN ('udp', 'tcp', 'tls', 'ws', 'wss')), 
    CONSTRAINT yesno_values CHECK (require_client_cert IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (verify_client IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (verifiy_server IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (tos IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (cos IN ('yes', 'no'))
);

GO

CREATE INDEX ps_transports_id ON ps_transports (id);

GO

CREATE TABLE ps_registrations (
    id VARCHAR(40) NOT NULL, 
    auth_rejection_permanent VARCHAR(3) NULL, 
    client_uri VARCHAR(40) NULL, 
    contact_user VARCHAR(40) NULL, 
    expiration INTEGER NULL, 
    max_retries INTEGER NULL, 
    outbound_auth VARCHAR(40) NULL, 
    outbound_proxy VARCHAR(40) NULL, 
    retry_interval INTEGER NULL, 
    forbidden_retry_interval INTEGER NULL, 
    server_uri VARCHAR(40) NULL, 
    transport VARCHAR(40) NULL, 
    support_path VARCHAR(3) NULL, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (auth_rejection_permanent IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (support_path IN ('yes', 'no'))
);

GO

CREATE INDEX ps_registrations_id ON ps_registrations (id);

GO

ALTER TABLE ps_endpoints ADD media_address VARCHAR(40) NULL;

GO

ALTER TABLE ps_endpoints ADD redirect_method VARCHAR(9) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_redirect_method_values CHECK (redirect_method IN ('user', 'uri_core', 'uri_pjsip'));

GO

ALTER TABLE ps_endpoints ADD set_var TEXT NULL;

GO

EXEC sp_rename 'ps_endpoints.mwi_fromuser', mwi_from_user, 'COLUMN';

GO

ALTER TABLE ps_contacts ADD outbound_proxy VARCHAR(40) NULL;

GO

ALTER TABLE ps_contacts ADD path TEXT NULL;

GO

ALTER TABLE ps_aors ADD maximum_expiration INTEGER NULL;

GO

ALTER TABLE ps_aors ADD outbound_proxy VARCHAR(40) NULL;

GO

ALTER TABLE ps_aors ADD support_path VARCHAR(3) NULL;

GO

ALTER TABLE ps_aors ADD CONSTRAINT yesno_values CHECK (support_path IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='2fc7930b41b3' WHERE alembic_version.version_num = '581a4264e537';

GO

-- Running upgrade 2fc7930b41b3 -> 21e526ad3040

ALTER TABLE ps_globals ADD debug VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='21e526ad3040' WHERE alembic_version.version_num = '2fc7930b41b3';

GO

-- Running upgrade 21e526ad3040 -> 28887f25a46f

CREATE TABLE queues (
    name VARCHAR(128) NOT NULL, 
    musiconhold VARCHAR(128) NULL, 
    announce VARCHAR(128) NULL, 
    context VARCHAR(128) NULL, 
    timeout INTEGER NULL, 
    ringinuse VARCHAR(3) NULL, 
    setinterfacevar VARCHAR(3) NULL, 
    setqueuevar VARCHAR(3) NULL, 
    setqueueentryvar VARCHAR(3) NULL, 
    monitor_format VARCHAR(8) NULL, 
    membermacro VARCHAR(512) NULL, 
    membergosub VARCHAR(512) NULL, 
    queue_youarenext VARCHAR(128) NULL, 
    queue_thereare VARCHAR(128) NULL, 
    queue_callswaiting VARCHAR(128) NULL, 
    queue_quantity1 VARCHAR(128) NULL, 
    queue_quantity2 VARCHAR(128) NULL, 
    queue_holdtime VARCHAR(128) NULL, 
    queue_minutes VARCHAR(128) NULL, 
    queue_minute VARCHAR(128) NULL, 
    queue_seconds VARCHAR(128) NULL, 
    queue_thankyou VARCHAR(128) NULL, 
    queue_callerannounce VARCHAR(128) NULL, 
    queue_reporthold VARCHAR(128) NULL, 
    announce_frequency INTEGER NULL, 
    announce_to_first_user VARCHAR(3) NULL, 
    min_announce_frequency INTEGER NULL, 
    announce_round_seconds INTEGER NULL, 
    announce_holdtime VARCHAR(128) NULL, 
    announce_position VARCHAR(128) NULL, 
    announce_position_limit INTEGER NULL, 
    periodic_announce VARCHAR(50) NULL, 
    periodic_announce_frequency INTEGER NULL, 
    relative_periodic_announce VARCHAR(3) NULL, 
    random_periodic_announce VARCHAR(3) NULL, 
    retry INTEGER NULL, 
    wrapuptime INTEGER NULL, 
    penaltymemberslimit INTEGER NULL, 
    autofill VARCHAR(3) NULL, 
    monitor_type VARCHAR(128) NULL, 
    autopause VARCHAR(3) NULL, 
    autopausedelay INTEGER NULL, 
    autopausebusy VARCHAR(3) NULL, 
    autopauseunavail VARCHAR(3) NULL, 
    maxlen INTEGER NULL, 
    servicelevel INTEGER NULL, 
    strategy VARCHAR(11) NULL, 
    joinempty VARCHAR(128) NULL, 
    leavewhenempty VARCHAR(128) NULL, 
    reportholdtime VARCHAR(3) NULL, 
    memberdelay INTEGER NULL, 
    weight INTEGER NULL, 
    timeoutrestart VARCHAR(3) NULL, 
    defaultrule VARCHAR(128) NULL, 
    timeoutpriority VARCHAR(128) NULL, 
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
);

GO

CREATE TABLE queue_members (
    queue_name VARCHAR(80) NOT NULL, 
    interface VARCHAR(80) NOT NULL, 
    uniqueid VARCHAR(80) NOT NULL, 
    membername VARCHAR(80) NULL, 
    state_interface VARCHAR(80) NULL, 
    penalty INTEGER NULL, 
    paused INTEGER NULL, 
    PRIMARY KEY (queue_name, interface)
);

GO

UPDATE alembic_version SET version_num='28887f25a46f' WHERE alembic_version.version_num = '21e526ad3040';

GO

-- Running upgrade 28887f25a46f -> 4c573e7135bd

ALTER TABLE ps_endpoints ALTER COLUMN tos_audio VARCHAR(10);

GO

ALTER TABLE ps_endpoints ALTER COLUMN tos_video VARCHAR(10);

GO

ALTER TABLE ps_endpoints DROP COLUMN cos_audio;

GO

ALTER TABLE ps_endpoints DROP COLUMN cos_video;

GO

ALTER TABLE ps_endpoints ADD cos_audio INTEGER NULL;

GO

ALTER TABLE ps_endpoints ADD cos_video INTEGER NULL;

GO

ALTER TABLE ps_transports ALTER COLUMN tos VARCHAR(10);

GO

declare @const_name varchar(256)
select @const_name = [name] from sys.check_constraints
where parent_object_id = object_id('ps_transports')
and col_name(parent_object_id, parent_column_id) = 'cos'
exec('alter table ps_transports drop constraint ' + @const_name);

GO

ALTER TABLE ps_transports DROP COLUMN cos;

GO

ALTER TABLE ps_transports ADD cos INTEGER NULL;

GO

UPDATE alembic_version SET version_num='4c573e7135bd' WHERE alembic_version.version_num = '28887f25a46f';

GO

-- Running upgrade 4c573e7135bd -> 3855ee4e5f85

ALTER TABLE ps_endpoints ADD message_context VARCHAR(40) NULL;

GO

ALTER TABLE ps_contacts ADD user_agent VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='3855ee4e5f85' WHERE alembic_version.version_num = '4c573e7135bd';

GO

-- Running upgrade 3855ee4e5f85 -> e96a0b8071c

ALTER TABLE ps_globals ALTER COLUMN user_agent VARCHAR(255);

GO

ALTER TABLE ps_contacts ALTER COLUMN id VARCHAR(255);

GO

ALTER TABLE ps_contacts ALTER COLUMN uri VARCHAR(255);

GO

ALTER TABLE ps_contacts ALTER COLUMN user_agent VARCHAR(255);

GO

ALTER TABLE ps_registrations ALTER COLUMN client_uri VARCHAR(255);

GO

ALTER TABLE ps_registrations ALTER COLUMN server_uri VARCHAR(255);

GO

UPDATE alembic_version SET version_num='e96a0b8071c' WHERE alembic_version.version_num = '3855ee4e5f85';

GO

-- Running upgrade e96a0b8071c -> c6d929b23a8

CREATE TABLE ps_subscription_persistence (
    id VARCHAR(40) NOT NULL, 
    packet VARCHAR(2048) NULL, 
    src_name VARCHAR(128) NULL, 
    src_port INTEGER NULL, 
    transport_key VARCHAR(64) NULL, 
    local_name VARCHAR(128) NULL, 
    local_port INTEGER NULL, 
    cseq INTEGER NULL, 
    tag VARCHAR(128) NULL, 
    endpoint VARCHAR(40) NULL, 
    expires INTEGER NULL, 
    UNIQUE (id)
);

GO

CREATE INDEX ps_subscription_persistence_id ON ps_subscription_persistence (id);

GO

UPDATE alembic_version SET version_num='c6d929b23a8' WHERE alembic_version.version_num = 'e96a0b8071c';

GO

-- Running upgrade c6d929b23a8 -> 51f8cb66540e

ALTER TABLE ps_endpoints ADD force_avp VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (force_avp IN ('yes', 'no'));

GO

ALTER TABLE ps_endpoints ADD media_use_received_transport VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (media_use_received_transport IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='51f8cb66540e' WHERE alembic_version.version_num = 'c6d929b23a8';

GO

-- Running upgrade 51f8cb66540e -> 1d50859ed02e

ALTER TABLE ps_endpoints ADD accountcode VARCHAR(20) NULL;

GO

UPDATE alembic_version SET version_num='1d50859ed02e' WHERE alembic_version.version_num = '51f8cb66540e';

GO

-- Running upgrade 1d50859ed02e -> 1758e8bbf6b

ALTER TABLE sippeers ALTER COLUMN useragent VARCHAR(255);

GO

UPDATE alembic_version SET version_num='1758e8bbf6b' WHERE alembic_version.version_num = '1d50859ed02e';

GO

-- Running upgrade 1758e8bbf6b -> 5139253c0423

ALTER TABLE queue_members DROP COLUMN uniqueid;

GO

ALTER TABLE queue_members ADD uniqueid INTEGER NOT NULL;

GO

ALTER TABLE queue_members ADD UNIQUE (uniqueid);

GO

UPDATE alembic_version SET version_num='5139253c0423' WHERE alembic_version.version_num = '1758e8bbf6b';

GO

-- Running upgrade 5139253c0423 -> d39508cb8d8

CREATE TABLE queue_rules (
    rule_name VARCHAR(80) NOT NULL, 
    time VARCHAR(32) NOT NULL, 
    min_penalty VARCHAR(32) NOT NULL, 
    max_penalty VARCHAR(32) NOT NULL
);

GO

UPDATE alembic_version SET version_num='d39508cb8d8' WHERE alembic_version.version_num = '5139253c0423';

GO

-- Running upgrade d39508cb8d8 -> 5950038a6ead

ALTER TABLE ps_transports DROP CONSTRAINT yesno_values;

GO

ALTER TABLE ps_transports ALTER COLUMN verifiy_server VARCHAR(3);

GO

EXEC sp_rename 'ps_transports.verifiy_server', verify_server, 'COLUMN';

GO

ALTER TABLE ps_transports ALTER COLUMN verify_server VARCHAR(3);

GO

ALTER TABLE ps_transports ADD CONSTRAINT yesno_values CHECK (verify_server IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='5950038a6ead' WHERE alembic_version.version_num = 'd39508cb8d8';

GO

-- Running upgrade 5950038a6ead -> 10aedae86a32

ALTER TABLE sippeers DROP CONSTRAINT sip_directmedia_values;

GO

ALTER TABLE sippeers ALTER COLUMN directmedia VARCHAR(8);

GO

ALTER TABLE sippeers ADD CONSTRAINT sip_directmedia_values_v2 CHECK (directmedia IN ('yes', 'no', 'nonat', 'update', 'outgoing'));

GO

UPDATE alembic_version SET version_num='10aedae86a32' WHERE alembic_version.version_num = '5950038a6ead';

GO

-- Running upgrade 10aedae86a32 -> eb88a14f2a

ALTER TABLE ps_endpoints ADD media_encryption_optimistic VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (media_encryption_optimistic IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='eb88a14f2a' WHERE alembic_version.version_num = '10aedae86a32';

GO

-- Running upgrade eb88a14f2a -> 371a3bf4143e

ALTER TABLE ps_endpoints ADD user_eq_phone VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (user_eq_phone IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='371a3bf4143e' WHERE alembic_version.version_num = 'eb88a14f2a';

GO

-- Running upgrade 371a3bf4143e -> 45e3f47c6c44

ALTER TABLE ps_globals ADD endpoint_identifier_order VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='45e3f47c6c44' WHERE alembic_version.version_num = '371a3bf4143e';

GO

-- Running upgrade 45e3f47c6c44 -> 23530d604b96

ALTER TABLE ps_endpoints ADD rpid_immediate VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (rpid_immediate IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='23530d604b96' WHERE alembic_version.version_num = '45e3f47c6c44';

GO

-- Running upgrade 23530d604b96 -> 31cd4f4891ec

ALTER TABLE ps_endpoints DROP CONSTRAINT pjsip_dtmf_mode_values;

GO

ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode VARCHAR(7);

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_dtmf_mode_values_v2 CHECK (dtmf_mode IN ('rfc4733', 'inband', 'info', 'auto'));

GO

UPDATE alembic_version SET version_num='31cd4f4891ec' WHERE alembic_version.version_num = '23530d604b96';

GO

-- Running upgrade 31cd4f4891ec -> 461d7d691209

ALTER TABLE ps_aors ADD qualify_timeout INTEGER NULL;

GO

ALTER TABLE ps_contacts ADD qualify_timeout INTEGER NULL;

GO

UPDATE alembic_version SET version_num='461d7d691209' WHERE alembic_version.version_num = '31cd4f4891ec';

GO

-- Running upgrade 461d7d691209 -> a541e0b5e89

ALTER TABLE ps_globals ADD max_initial_qualify_time INTEGER NULL;

GO

UPDATE alembic_version SET version_num='a541e0b5e89' WHERE alembic_version.version_num = '461d7d691209';

GO

-- Running upgrade a541e0b5e89 -> 28b8e71e541f

ALTER TABLE ps_endpoints ADD g726_non_standard VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (g726_non_standard IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='28b8e71e541f' WHERE alembic_version.version_num = 'a541e0b5e89';

GO

-- Running upgrade 28b8e71e541f -> 498357a710ae

ALTER TABLE ps_endpoints ADD rtp_keepalive INTEGER NULL;

GO

UPDATE alembic_version SET version_num='498357a710ae' WHERE alembic_version.version_num = '28b8e71e541f';

GO

-- Running upgrade 498357a710ae -> 26f10cadc157

ALTER TABLE ps_endpoints ADD rtp_timeout INTEGER NULL;

GO

ALTER TABLE ps_endpoints ADD rtp_timeout_hold INTEGER NULL;

GO

UPDATE alembic_version SET version_num='26f10cadc157' WHERE alembic_version.version_num = '498357a710ae';

GO

-- Running upgrade 26f10cadc157 -> 154177371065

ALTER TABLE ps_globals ADD default_from_user VARCHAR(80) NULL;

GO

UPDATE alembic_version SET version_num='154177371065' WHERE alembic_version.version_num = '26f10cadc157';

GO

-- Running upgrade 154177371065 -> 28ce1e718f05

ALTER TABLE ps_registrations ADD fatal_retry_interval INTEGER NULL;

GO

UPDATE alembic_version SET version_num='28ce1e718f05' WHERE alembic_version.version_num = '154177371065';

GO

-- Running upgrade 28ce1e718f05 -> 189a235b3fd7

ALTER TABLE ps_globals ADD keep_alive_interval INTEGER NULL;

GO

UPDATE alembic_version SET version_num='189a235b3fd7' WHERE alembic_version.version_num = '28ce1e718f05';

GO

-- Running upgrade 189a235b3fd7 -> 2d078ec071b7

ALTER TABLE ps_aors ALTER COLUMN contact VARCHAR(255);

GO

UPDATE alembic_version SET version_num='2d078ec071b7' WHERE alembic_version.version_num = '189a235b3fd7';

GO

-- Running upgrade 2d078ec071b7 -> 26d7f3bf0fa5

ALTER TABLE ps_endpoints ADD bind_rtp_to_media_address VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (bind_rtp_to_media_address IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='26d7f3bf0fa5' WHERE alembic_version.version_num = '2d078ec071b7';

GO

-- Running upgrade 26d7f3bf0fa5 -> 136885b81223

ALTER TABLE ps_globals ADD regcontext VARCHAR(80) NULL;

GO

UPDATE alembic_version SET version_num='136885b81223' WHERE alembic_version.version_num = '26d7f3bf0fa5';

GO

-- Running upgrade 136885b81223 -> 423f34ad36e2

ALTER TABLE ps_aors ALTER COLUMN qualify_timeout FLOAT;

GO

ALTER TABLE ps_contacts ALTER COLUMN qualify_timeout FLOAT;

GO

UPDATE alembic_version SET version_num='423f34ad36e2' WHERE alembic_version.version_num = '136885b81223';

GO

-- Running upgrade 423f34ad36e2 -> dbc44d5a908

ALTER TABLE ps_systems ADD disable_tcp_switch VARCHAR(3) NULL;

GO

ALTER TABLE ps_systems ADD CONSTRAINT yesno_values CHECK (disable_tcp_switch IN ('yes', 'no'));

GO

ALTER TABLE ps_registrations ADD line VARCHAR(3) NULL;

GO

ALTER TABLE ps_registrations ADD CONSTRAINT yesno_values CHECK (line IN ('yes', 'no'));

GO

ALTER TABLE ps_registrations ADD endpoint VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='dbc44d5a908' WHERE alembic_version.version_num = '423f34ad36e2';

GO

-- Running upgrade dbc44d5a908 -> 3bcc0b5bc2c9

ALTER TABLE ps_transports ADD allow_reload VARCHAR(3) NULL;

GO

ALTER TABLE ps_transports ADD CONSTRAINT yesno_values CHECK (allow_reload IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='3bcc0b5bc2c9' WHERE alembic_version.version_num = 'dbc44d5a908';

GO

-- Running upgrade 3bcc0b5bc2c9 -> 5813202e92be

ALTER TABLE ps_globals ADD contact_expiration_check_interval INTEGER NULL;

GO

UPDATE alembic_version SET version_num='5813202e92be' WHERE alembic_version.version_num = '3bcc0b5bc2c9';

GO

-- Running upgrade 5813202e92be -> 1c688d9a003c

ALTER TABLE ps_globals ADD default_voicemail_extension VARCHAR(40) NULL;

GO

ALTER TABLE ps_aors ADD voicemail_extension VARCHAR(40) NULL;

GO

ALTER TABLE ps_endpoints ADD voicemail_extension VARCHAR(40) NULL;

GO

ALTER TABLE ps_endpoints ADD mwi_subscribe_replaces_unsolicited INTEGER NULL;

GO

UPDATE alembic_version SET version_num='1c688d9a003c' WHERE alembic_version.version_num = '5813202e92be';

GO

-- Running upgrade 1c688d9a003c -> 8d478ab86e29

ALTER TABLE ps_globals ADD disable_multi_domain VARCHAR(3) NULL;

GO

ALTER TABLE ps_globals ADD CONSTRAINT yesno_values CHECK (disable_multi_domain IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='8d478ab86e29' WHERE alembic_version.version_num = '1c688d9a003c';

GO

-- Running upgrade 8d478ab86e29 -> 65eb22eb195

ALTER TABLE ps_globals ADD unidentified_request_count INTEGER NULL;

GO

ALTER TABLE ps_globals ADD unidentified_request_period INTEGER NULL;

GO

ALTER TABLE ps_globals ADD unidentified_request_prune_interval INTEGER NULL;

GO

ALTER TABLE ps_globals ADD default_realm VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='65eb22eb195' WHERE alembic_version.version_num = '8d478ab86e29';

GO

-- Running upgrade 65eb22eb195 -> 81b01a191a46

ALTER TABLE ps_contacts ADD reg_server VARCHAR(20) NULL;

GO

ALTER TABLE ps_contacts ADD CONSTRAINT ps_contacts_uq UNIQUE (id, reg_server);

GO

UPDATE alembic_version SET version_num='81b01a191a46' WHERE alembic_version.version_num = '65eb22eb195';

GO

-- Running upgrade 81b01a191a46 -> 6be31516058d

ALTER TABLE ps_contacts ADD authenticate_qualify VARCHAR(3) NULL;

GO

ALTER TABLE ps_contacts ADD CONSTRAINT yesno_values CHECK (authenticate_qualify IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='6be31516058d' WHERE alembic_version.version_num = '81b01a191a46';

GO

-- Running upgrade 6be31516058d -> bca7113d796f

ALTER TABLE ps_endpoints ADD [deny] VARCHAR(95) NULL;

GO

ALTER TABLE ps_endpoints ADD permit VARCHAR(95) NULL;

GO

ALTER TABLE ps_endpoints ADD acl VARCHAR(40) NULL;

GO

ALTER TABLE ps_endpoints ADD contact_deny VARCHAR(95) NULL;

GO

ALTER TABLE ps_endpoints ADD contact_permit VARCHAR(95) NULL;

GO

ALTER TABLE ps_endpoints ADD contact_acl VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='bca7113d796f' WHERE alembic_version.version_num = '6be31516058d';

GO

-- Running upgrade bca7113d796f -> a845e4d8ade8

ALTER TABLE ps_contacts ADD via_addr VARCHAR(40) NULL;

GO

ALTER TABLE ps_contacts ADD via_port INTEGER NULL;

GO

ALTER TABLE ps_contacts ADD call_id VARCHAR(255) NULL;

GO

UPDATE alembic_version SET version_num='a845e4d8ade8' WHERE alembic_version.version_num = 'bca7113d796f';

GO

-- Running upgrade a845e4d8ade8 -> ef7efc2d3964

ALTER TABLE ps_contacts ADD endpoint VARCHAR(40) NULL;

GO

ALTER TABLE ps_contacts ALTER COLUMN expiration_time BIGINT;

GO

CREATE INDEX ps_contacts_qualifyfreq_exp ON ps_contacts (qualify_frequency, expiration_time);

GO

CREATE INDEX ps_aors_qualifyfreq_contact ON ps_aors (qualify_frequency, contact);

GO

UPDATE alembic_version SET version_num='ef7efc2d3964' WHERE alembic_version.version_num = 'a845e4d8ade8';

GO

-- Running upgrade ef7efc2d3964 -> 9deac0ae4717

ALTER TABLE ps_endpoints ADD subscribe_context VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='9deac0ae4717' WHERE alembic_version.version_num = 'ef7efc2d3964';

GO

-- Running upgrade 9deac0ae4717 -> 4a6c67fa9b7a

ALTER TABLE ps_endpoints ADD fax_detect_timeout INTEGER NULL;

GO

UPDATE alembic_version SET version_num='4a6c67fa9b7a' WHERE alembic_version.version_num = '9deac0ae4717';

GO

-- Running upgrade 4a6c67fa9b7a -> c7a44a5a0851

ALTER TABLE ps_globals ADD mwi_tps_queue_high INTEGER NULL;

GO

ALTER TABLE ps_globals ADD mwi_tps_queue_low INTEGER NULL;

GO

ALTER TABLE ps_globals ADD mwi_disable_initial_unsolicited VARCHAR(3) NULL;

GO

ALTER TABLE ps_globals ADD CONSTRAINT yesno_values CHECK (mwi_disable_initial_unsolicited IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='c7a44a5a0851' WHERE alembic_version.version_num = '4a6c67fa9b7a';

GO

-- Running upgrade c7a44a5a0851 -> 3772f8f828da

ALTER TABLE ps_endpoints DROP CONSTRAINT ck_ps_endpoints_identify_by_pjsip_identify_by_values;

GO

ALTER TABLE ps_endpoints ALTER COLUMN identify_by VARCHAR(13);

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_identify_by_values CHECK (identify_by IN ('username', 'auth_username'));

GO

UPDATE alembic_version SET version_num='3772f8f828da' WHERE alembic_version.version_num = 'c7a44a5a0851';

GO

-- Running upgrade 3772f8f828da -> 4e2493ef32e6

ALTER TABLE ps_endpoints ADD contact_user VARCHAR(80) NULL;

GO

UPDATE alembic_version SET version_num='4e2493ef32e6' WHERE alembic_version.version_num = '3772f8f828da';

GO

-- Running upgrade 4e2493ef32e6 -> a6ef36f1309

ALTER TABLE ps_globals ADD ignore_uri_user_options VARCHAR(3) NULL;

GO

ALTER TABLE ps_globals ADD CONSTRAINT yesno_values CHECK (ignore_uri_user_options IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='a6ef36f1309' WHERE alembic_version.version_num = '4e2493ef32e6';

GO

-- Running upgrade a6ef36f1309 -> 4468b4a91372

ALTER TABLE ps_endpoints ADD asymmetric_rtp_codec VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (asymmetric_rtp_codec IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='4468b4a91372' WHERE alembic_version.version_num = 'a6ef36f1309';

GO

-- Running upgrade 4468b4a91372 -> 28ab27a7826d

ALTER TABLE ps_endpoint_id_ips ADD srv_lookups VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoint_id_ips ADD CONSTRAINT yesno_values CHECK (srv_lookups IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='28ab27a7826d' WHERE alembic_version.version_num = '4468b4a91372';

GO

-- Running upgrade 28ab27a7826d -> 465e70e8c337

ALTER TABLE ps_endpoint_id_ips ADD match_header VARCHAR(255) NULL;

GO

UPDATE alembic_version SET version_num='465e70e8c337' WHERE alembic_version.version_num = '28ab27a7826d';

GO

-- Running upgrade 465e70e8c337 -> 15db7b91a97a

ALTER TABLE ps_endpoints ADD rtcp_mux VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (rtcp_mux IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='15db7b91a97a' WHERE alembic_version.version_num = '465e70e8c337';

GO

-- Running upgrade 15db7b91a97a -> f638dbe2eb23

ALTER TABLE ps_transports ADD symmetric_transport VARCHAR(3) NULL;

GO

ALTER TABLE ps_transports ADD CONSTRAINT yesno_values CHECK (symmetric_transport IN ('yes', 'no'));

GO

ALTER TABLE ps_subscription_persistence ADD contact_uri VARCHAR(256) NULL;

GO

UPDATE alembic_version SET version_num='f638dbe2eb23' WHERE alembic_version.version_num = '15db7b91a97a';

GO

-- Running upgrade f638dbe2eb23 -> 8fce4c573e15

ALTER TABLE ps_endpoints ADD allow_overlap VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (allow_overlap IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='8fce4c573e15' WHERE alembic_version.version_num = 'f638dbe2eb23';

GO

-- Running upgrade 8fce4c573e15 -> 2da192dbbc65

CREATE TABLE ps_outbound_publishes (
    id VARCHAR(40) NOT NULL, 
    expiration INTEGER NULL, 
    outbound_auth VARCHAR(40) NULL, 
    outbound_proxy VARCHAR(256) NULL, 
    server_uri VARCHAR(256) NULL, 
    from_uri VARCHAR(256) NULL, 
    to_uri VARCHAR(256) NULL, 
    event VARCHAR(40) NULL, 
    max_auth_attempts INTEGER NULL, 
    transport VARCHAR(40) NULL, 
    multi_user VARCHAR(3) NULL, 
    [@body] VARCHAR(40) NULL, 
    [@context] VARCHAR(256) NULL, 
    [@exten] VARCHAR(256) NULL, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (multi_user IN ('yes', 'no'))
);

GO

CREATE INDEX ps_outbound_publishes_id ON ps_outbound_publishes (id);

GO

CREATE TABLE ps_inbound_publications (
    id VARCHAR(40) NOT NULL, 
    endpoint VARCHAR(40) NULL, 
    [event_asterisk-devicestate] VARCHAR(40) NULL, 
    [event_asterisk-mwi] VARCHAR(40) NULL, 
    UNIQUE (id)
);

GO

CREATE INDEX ps_inbound_publications_id ON ps_inbound_publications (id);

GO

CREATE TABLE ps_asterisk_publications (
    id VARCHAR(40) NOT NULL, 
    devicestate_publish VARCHAR(40) NULL, 
    mailboxstate_publish VARCHAR(40) NULL, 
    device_state VARCHAR(3) NULL, 
    device_state_filter VARCHAR(256) NULL, 
    mailbox_state VARCHAR(3) NULL, 
    mailbox_state_filter VARCHAR(256) NULL, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (device_state IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (mailbox_state IN ('yes', 'no'))
);

GO

CREATE INDEX ps_asterisk_publications_id ON ps_asterisk_publications (id);

GO

UPDATE alembic_version SET version_num='2da192dbbc65' WHERE alembic_version.version_num = '8fce4c573e15';

GO

-- Running upgrade 2da192dbbc65 -> 1d0e332c32af

CREATE TABLE ps_resource_list (
    id VARCHAR(40) NOT NULL, 
    list_item VARCHAR(2048) NULL, 
    event VARCHAR(40) NULL, 
    full_state VARCHAR(3) NULL, 
    notification_batch_interval INTEGER NULL, 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (full_state IN ('yes', 'no'))
);

GO

CREATE INDEX ps_resource_list_id ON ps_resource_list (id);

GO

UPDATE alembic_version SET version_num='1d0e332c32af' WHERE alembic_version.version_num = '2da192dbbc65';

GO

-- Running upgrade 1d0e332c32af -> 86bb1efa278d

ALTER TABLE ps_endpoints ADD refer_blind_progress VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (refer_blind_progress IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='86bb1efa278d' WHERE alembic_version.version_num = '1d0e332c32af';

GO

-- Running upgrade 86bb1efa278d -> d7983954dd96

ALTER TABLE ps_endpoints ADD notify_early_inuse_ringing VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (notify_early_inuse_ringing IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='d7983954dd96' WHERE alembic_version.version_num = '86bb1efa278d';

GO

-- Running upgrade d7983954dd96 -> 164abbd708c

ALTER TABLE ps_endpoints DROP CONSTRAINT pjsip_dtmf_mode_values_v2;

GO

ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode VARCHAR(9);

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_dtmf_mode_values_v3 CHECK (dtmf_mode IN ('rfc4733', 'inband', 'info', 'auto', 'auto_info'));

GO

UPDATE alembic_version SET version_num='164abbd708c' WHERE alembic_version.version_num = 'd7983954dd96';

GO

-- Running upgrade 164abbd708c -> f3d1c5d38b56

ALTER TABLE ps_contacts ADD prune_on_boot VARCHAR(3) NULL;

GO

ALTER TABLE ps_contacts ADD CONSTRAINT yesno_values CHECK (prune_on_boot IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='f3d1c5d38b56' WHERE alembic_version.version_num = '164abbd708c';

GO

-- Running upgrade f3d1c5d38b56 -> b83645976fdd

ALTER TABLE ps_endpoints ADD dtls_fingerprint VARCHAR(7) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT sha_hash_values CHECK (dtls_fingerprint IN ('SHA-1', 'SHA-256'));

GO

UPDATE alembic_version SET version_num='b83645976fdd' WHERE alembic_version.version_num = 'f3d1c5d38b56';

GO

-- Running upgrade b83645976fdd -> a1698e8bb9c5

ALTER TABLE ps_endpoints ADD incoming_mwi_mailbox VARCHAR(40) NULL;

GO

UPDATE alembic_version SET version_num='a1698e8bb9c5' WHERE alembic_version.version_num = 'b83645976fdd';

GO

-- Running upgrade a1698e8bb9c5 -> 20abce6d1e3c

ALTER TABLE ps_endpoints DROP CONSTRAINT ck_ps_endpoints_identify_by_pjsip_identify_by_values;

GO

ALTER TABLE ps_endpoints ALTER COLUMN identify_by VARCHAR(13);

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT pjsip_identify_by_values CHECK (identify_by IN ('username', 'auth_username', 'ip'));

GO

UPDATE alembic_version SET version_num='20abce6d1e3c' WHERE alembic_version.version_num = 'a1698e8bb9c5';

GO

-- Running upgrade 20abce6d1e3c -> 52798ad97bdf

ALTER TABLE ps_endpoints DROP CONSTRAINT ck_ps_endpoints_identify_by_pjsip_identify_by_values;

GO

ALTER TABLE ps_endpoints ALTER COLUMN identify_by VARCHAR(80);

GO

UPDATE alembic_version SET version_num='52798ad97bdf' WHERE alembic_version.version_num = '20abce6d1e3c';

GO

-- Running upgrade 52798ad97bdf -> d3e4284f8707

ALTER TABLE ps_subscription_persistence ADD prune_on_boot VARCHAR(3) NULL;

GO

ALTER TABLE ps_subscription_persistence ADD CONSTRAINT yesno_values CHECK (prune_on_boot IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='d3e4284f8707' WHERE alembic_version.version_num = '52798ad97bdf';

GO

-- Running upgrade d3e4284f8707 -> 0be05c3a8225

ALTER TABLE ps_systems ADD follow_early_media_fork VARCHAR(3) NULL;

GO

ALTER TABLE ps_systems ADD CONSTRAINT yesno_values CHECK (follow_early_media_fork IN ('yes', 'no'));

GO

ALTER TABLE ps_systems ADD accept_multiple_sdp_answers VARCHAR(3) NULL;

GO

ALTER TABLE ps_systems ADD CONSTRAINT yesno_values CHECK (accept_multiple_sdp_answers IN ('yes', 'no'));

GO

ALTER TABLE ps_endpoints ADD follow_early_media_fork VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (follow_early_media_fork IN ('yes', 'no'));

GO

ALTER TABLE ps_endpoints ADD accept_multiple_sdp_answers VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (accept_multiple_sdp_answers IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='0be05c3a8225' WHERE alembic_version.version_num = 'd3e4284f8707';

GO

-- Running upgrade 0be05c3a8225 -> 19b00bc19b7b

ALTER TABLE ps_endpoints ADD suppress_q850_reason_header VARCHAR(3) NULL;

GO

ALTER TABLE ps_endpoints ADD CONSTRAINT yesno_values CHECK (suppress_q850_reason_header IN ('yes', 'no'));

GO

UPDATE alembic_version SET version_num='19b00bc19b7b' WHERE alembic_version.version_num = '0be05c3a8225';

GO

-- Running upgrade 19b00bc19b7b -> 1d3ed26d9978

ALTER TABLE ps_contacts ALTER COLUMN uri VARCHAR(511);

GO

UPDATE alembic_version SET version_num='1d3ed26d9978' WHERE alembic_version.version_num = '19b00bc19b7b';

GO

COMMIT;

GO

