CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);

-- Running upgrade  -> 4da0c5f79a9c

CREATE TABLE sippeers (
    id INTEGER NOT NULL AUTO_INCREMENT, 
    name VARCHAR(40) NOT NULL, 
    ipaddr VARCHAR(45), 
    port INTEGER, 
    regseconds INTEGER, 
    defaultuser VARCHAR(40), 
    fullcontact VARCHAR(80), 
    regserver VARCHAR(20), 
    useragent VARCHAR(20), 
    lastms INTEGER, 
    host VARCHAR(40), 
    type ENUM('friend','user','peer'), 
    context VARCHAR(40), 
    permit VARCHAR(95), 
    deny VARCHAR(95), 
    secret VARCHAR(40), 
    md5secret VARCHAR(40), 
    remotesecret VARCHAR(40), 
    transport ENUM('udp','tcp','tls','ws','wss','udp,tcp','tcp,udp'), 
    dtmfmode ENUM('rfc2833','info','shortinfo','inband','auto'), 
    directmedia ENUM('yes','no','nonat','update'), 
    nat VARCHAR(29), 
    callgroup VARCHAR(40), 
    pickupgroup VARCHAR(40), 
    language VARCHAR(40), 
    disallow VARCHAR(200), 
    allow VARCHAR(200), 
    insecure VARCHAR(40), 
    trustrpid ENUM('yes','no'), 
    progressinband ENUM('yes','no','never'), 
    promiscredir ENUM('yes','no'), 
    useclientcode ENUM('yes','no'), 
    accountcode VARCHAR(40), 
    setvar VARCHAR(200), 
    callerid VARCHAR(40), 
    amaflags VARCHAR(40), 
    callcounter ENUM('yes','no'), 
    busylevel INTEGER, 
    allowoverlap ENUM('yes','no'), 
    allowsubscribe ENUM('yes','no'), 
    videosupport ENUM('yes','no'), 
    maxcallbitrate INTEGER, 
    rfc2833compensate ENUM('yes','no'), 
    mailbox VARCHAR(40), 
    `session-timers` ENUM('accept','refuse','originate'), 
    `session-expires` INTEGER, 
    `session-minse` INTEGER, 
    `session-refresher` ENUM('uac','uas'), 
    t38pt_usertpsource VARCHAR(40), 
    regexten VARCHAR(40), 
    fromdomain VARCHAR(40), 
    fromuser VARCHAR(40), 
    qualify VARCHAR(40), 
    defaultip VARCHAR(45), 
    rtptimeout INTEGER, 
    rtpholdtimeout INTEGER, 
    sendrpid ENUM('yes','no'), 
    outboundproxy VARCHAR(40), 
    callbackextension VARCHAR(40), 
    timert1 INTEGER, 
    timerb INTEGER, 
    qualifyfreq INTEGER, 
    constantssrc ENUM('yes','no'), 
    contactpermit VARCHAR(95), 
    contactdeny VARCHAR(95), 
    usereqphone ENUM('yes','no'), 
    textsupport ENUM('yes','no'), 
    faxdetect ENUM('yes','no'), 
    buggymwi ENUM('yes','no'), 
    auth VARCHAR(40), 
    fullname VARCHAR(40), 
    trunkname VARCHAR(40), 
    cid_number VARCHAR(40), 
    callingpres ENUM('allowed_not_screened','allowed_passed_screen','allowed_failed_screen','allowed','prohib_not_screened','prohib_passed_screen','prohib_failed_screen','prohib'), 
    mohinterpret VARCHAR(40), 
    mohsuggest VARCHAR(40), 
    parkinglot VARCHAR(40), 
    hasvoicemail ENUM('yes','no'), 
    subscribemwi ENUM('yes','no'), 
    vmexten VARCHAR(40), 
    autoframing ENUM('yes','no'), 
    rtpkeepalive INTEGER, 
    `call-limit` INTEGER, 
    g726nonstandard ENUM('yes','no'), 
    ignoresdpversion ENUM('yes','no'), 
    allowtransfer ENUM('yes','no'), 
    dynamic ENUM('yes','no'), 
    path VARCHAR(256), 
    supportpath ENUM('yes','no'), 
    PRIMARY KEY (id), 
    UNIQUE (name)
);

CREATE INDEX sippeers_name ON sippeers (name);

CREATE INDEX sippeers_name_host ON sippeers (name, host);

CREATE INDEX sippeers_ipaddr_port ON sippeers (ipaddr, port);

CREATE INDEX sippeers_host_port ON sippeers (host, port);

CREATE TABLE iaxfriends (
    id INTEGER NOT NULL AUTO_INCREMENT, 
    name VARCHAR(40) NOT NULL, 
    type ENUM('friend','user','peer'), 
    username VARCHAR(40), 
    mailbox VARCHAR(40), 
    secret VARCHAR(40), 
    dbsecret VARCHAR(40), 
    context VARCHAR(40), 
    regcontext VARCHAR(40), 
    host VARCHAR(40), 
    ipaddr VARCHAR(40), 
    port INTEGER, 
    defaultip VARCHAR(20), 
    sourceaddress VARCHAR(20), 
    mask VARCHAR(20), 
    regexten VARCHAR(40), 
    regseconds INTEGER, 
    accountcode VARCHAR(20), 
    mohinterpret VARCHAR(20), 
    mohsuggest VARCHAR(20), 
    inkeys VARCHAR(40), 
    outkeys VARCHAR(40), 
    language VARCHAR(10), 
    callerid VARCHAR(100), 
    cid_number VARCHAR(40), 
    sendani ENUM('yes','no'), 
    fullname VARCHAR(40), 
    trunk ENUM('yes','no'), 
    auth VARCHAR(20), 
    maxauthreq INTEGER, 
    requirecalltoken ENUM('yes','no','auto'), 
    encryption ENUM('yes','no','aes128'), 
    transfer ENUM('yes','no','mediaonly'), 
    jitterbuffer ENUM('yes','no'), 
    forcejitterbuffer ENUM('yes','no'), 
    disallow VARCHAR(200), 
    allow VARCHAR(200), 
    codecpriority VARCHAR(40), 
    qualify VARCHAR(10), 
    qualifysmoothing ENUM('yes','no'), 
    qualifyfreqok VARCHAR(10), 
    qualifyfreqnotok VARCHAR(10), 
    timezone VARCHAR(20), 
    adsi ENUM('yes','no'), 
    amaflags VARCHAR(20), 
    setvar VARCHAR(200), 
    PRIMARY KEY (id), 
    UNIQUE (name)
);

CREATE INDEX iaxfriends_name ON iaxfriends (name);

CREATE INDEX iaxfriends_name_host ON iaxfriends (name, host);

CREATE INDEX iaxfriends_name_ipaddr_port ON iaxfriends (name, ipaddr, port);

CREATE INDEX iaxfriends_ipaddr_port ON iaxfriends (ipaddr, port);

CREATE INDEX iaxfriends_host_port ON iaxfriends (host, port);

CREATE TABLE voicemail (
    uniqueid INTEGER NOT NULL AUTO_INCREMENT, 
    context VARCHAR(80) NOT NULL, 
    mailbox VARCHAR(80) NOT NULL, 
    password VARCHAR(80) NOT NULL, 
    fullname VARCHAR(80), 
    alias VARCHAR(80), 
    email VARCHAR(80), 
    pager VARCHAR(80), 
    attach ENUM('yes','no'), 
    attachfmt VARCHAR(10), 
    serveremail VARCHAR(80), 
    language VARCHAR(20), 
    tz VARCHAR(30), 
    deletevoicemail ENUM('yes','no'), 
    saycid ENUM('yes','no'), 
    sendvoicemail ENUM('yes','no'), 
    review ENUM('yes','no'), 
    tempgreetwarn ENUM('yes','no'), 
    operator ENUM('yes','no'), 
    envelope ENUM('yes','no'), 
    sayduration INTEGER, 
    forcename ENUM('yes','no'), 
    forcegreetings ENUM('yes','no'), 
    callback VARCHAR(80), 
    dialout VARCHAR(80), 
    exitcontext VARCHAR(80), 
    maxmsg INTEGER, 
    volgain NUMERIC(5, 2), 
    imapuser VARCHAR(80), 
    imappassword VARCHAR(80), 
    imapserver VARCHAR(80), 
    imapport VARCHAR(8), 
    imapflags VARCHAR(80), 
    stamp DATETIME, 
    PRIMARY KEY (uniqueid)
);

CREATE INDEX voicemail_mailbox ON voicemail (mailbox);

CREATE INDEX voicemail_context ON voicemail (context);

CREATE INDEX voicemail_mailbox_context ON voicemail (mailbox, context);

CREATE INDEX voicemail_imapuser ON voicemail (imapuser);

CREATE TABLE meetme (
    bookid INTEGER NOT NULL AUTO_INCREMENT, 
    confno VARCHAR(80) NOT NULL, 
    starttime DATETIME, 
    endtime DATETIME, 
    pin VARCHAR(20), 
    adminpin VARCHAR(20), 
    opts VARCHAR(20), 
    adminopts VARCHAR(20), 
    recordingfilename VARCHAR(80), 
    recordingformat VARCHAR(10), 
    maxusers INTEGER, 
    members INTEGER NOT NULL, 
    PRIMARY KEY (bookid)
);

CREATE INDEX meetme_confno_start_end ON meetme (confno, starttime, endtime);

CREATE TABLE musiconhold (
    name VARCHAR(80) NOT NULL, 
    mode ENUM('custom','files','mp3nb','quietmp3nb','quietmp3'), 
    directory VARCHAR(255), 
    application VARCHAR(255), 
    digit VARCHAR(1), 
    sort VARCHAR(10), 
    format VARCHAR(10), 
    stamp DATETIME, 
    PRIMARY KEY (name)
);

INSERT INTO alembic_version (version_num) VALUES ('4da0c5f79a9c');

-- Running upgrade 4da0c5f79a9c -> 43956d550a44

CREATE TABLE ps_endpoints (
    id VARCHAR(40) NOT NULL, 
    transport VARCHAR(40), 
    aors VARCHAR(200), 
    auth VARCHAR(40), 
    context VARCHAR(40), 
    disallow VARCHAR(200), 
    allow VARCHAR(200), 
    direct_media ENUM('yes','no'), 
    connected_line_method ENUM('invite','reinvite','update'), 
    direct_media_method ENUM('invite','reinvite','update'), 
    direct_media_glare_mitigation ENUM('none','outgoing','incoming'), 
    disable_direct_media_on_nat ENUM('yes','no'), 
    dtmf_mode ENUM('rfc4733','inband','info'), 
    external_media_address VARCHAR(40), 
    force_rport ENUM('yes','no'), 
    ice_support ENUM('yes','no'), 
    identify_by ENUM('username'), 
    mailboxes VARCHAR(40), 
    moh_suggest VARCHAR(40), 
    outbound_auth VARCHAR(40), 
    outbound_proxy VARCHAR(40), 
    rewrite_contact ENUM('yes','no'), 
    rtp_ipv6 ENUM('yes','no'), 
    rtp_symmetric ENUM('yes','no'), 
    send_diversion ENUM('yes','no'), 
    send_pai ENUM('yes','no'), 
    send_rpid ENUM('yes','no'), 
    timers_min_se INTEGER, 
    timers ENUM('forced','no','required','yes'), 
    timers_sess_expires INTEGER, 
    callerid VARCHAR(40), 
    callerid_privacy ENUM('allowed_not_screened','allowed_passed_screened','allowed_failed_screened','allowed','prohib_not_screened','prohib_passed_screened','prohib_failed_screened','prohib','unavailable'), 
    callerid_tag VARCHAR(40), 
    `100rel` ENUM('no','required','yes'), 
    aggregate_mwi ENUM('yes','no'), 
    trust_id_inbound ENUM('yes','no'), 
    trust_id_outbound ENUM('yes','no'), 
    use_ptime ENUM('yes','no'), 
    use_avpf ENUM('yes','no'), 
    media_encryption ENUM('no','sdes','dtls'), 
    inband_progress ENUM('yes','no'), 
    call_group VARCHAR(40), 
    pickup_group VARCHAR(40), 
    named_call_group VARCHAR(40), 
    named_pickup_group VARCHAR(40), 
    device_state_busy_at INTEGER, 
    fax_detect ENUM('yes','no'), 
    t38_udptl ENUM('yes','no'), 
    t38_udptl_ec ENUM('none','fec','redundancy'), 
    t38_udptl_maxdatagram INTEGER, 
    t38_udptl_nat ENUM('yes','no'), 
    t38_udptl_ipv6 ENUM('yes','no'), 
    tone_zone VARCHAR(40), 
    language VARCHAR(40), 
    one_touch_recording ENUM('yes','no'), 
    record_on_feature VARCHAR(40), 
    record_off_feature VARCHAR(40), 
    rtp_engine VARCHAR(40), 
    allow_transfer ENUM('yes','no'), 
    allow_subscribe ENUM('yes','no'), 
    sdp_owner VARCHAR(40), 
    sdp_session VARCHAR(40), 
    tos_audio INTEGER, 
    tos_video INTEGER, 
    cos_audio INTEGER, 
    cos_video INTEGER, 
    sub_min_expiry INTEGER, 
    from_domain VARCHAR(40), 
    from_user VARCHAR(40), 
    mwi_fromuser VARCHAR(40), 
    dtls_verify VARCHAR(40), 
    dtls_rekey VARCHAR(40), 
    dtls_cert_file VARCHAR(200), 
    dtls_private_key VARCHAR(200), 
    dtls_cipher VARCHAR(200), 
    dtls_ca_file VARCHAR(200), 
    dtls_ca_path VARCHAR(200), 
    dtls_setup ENUM('active','passive','actpass'), 
    srtp_tag_32 ENUM('yes','no'), 
    UNIQUE (id)
);

CREATE INDEX ps_endpoints_id ON ps_endpoints (id);

CREATE TABLE ps_auths (
    id VARCHAR(40) NOT NULL, 
    auth_type ENUM('md5','userpass'), 
    nonce_lifetime INTEGER, 
    md5_cred VARCHAR(40), 
    password VARCHAR(80), 
    realm VARCHAR(40), 
    username VARCHAR(40), 
    UNIQUE (id)
);

CREATE INDEX ps_auths_id ON ps_auths (id);

CREATE TABLE ps_aors (
    id VARCHAR(40) NOT NULL, 
    contact VARCHAR(40), 
    default_expiration INTEGER, 
    mailboxes VARCHAR(80), 
    max_contacts INTEGER, 
    minimum_expiration INTEGER, 
    remove_existing ENUM('yes','no'), 
    qualify_frequency INTEGER, 
    authenticate_qualify ENUM('yes','no'), 
    UNIQUE (id)
);

CREATE INDEX ps_aors_id ON ps_aors (id);

CREATE TABLE ps_contacts (
    id VARCHAR(40) NOT NULL, 
    uri VARCHAR(40), 
    expiration_time VARCHAR(40), 
    qualify_frequency INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_contacts_id ON ps_contacts (id);

CREATE TABLE ps_domain_aliases (
    id VARCHAR(40) NOT NULL, 
    domain VARCHAR(80), 
    UNIQUE (id)
);

CREATE INDEX ps_domain_aliases_id ON ps_domain_aliases (id);

CREATE TABLE ps_endpoint_id_ips (
    id VARCHAR(40) NOT NULL, 
    endpoint VARCHAR(40), 
    `match` VARCHAR(80), 
    UNIQUE (id)
);

CREATE INDEX ps_endpoint_id_ips_id ON ps_endpoint_id_ips (id);

UPDATE alembic_version SET version_num='43956d550a44' WHERE alembic_version.version_num = '4da0c5f79a9c';

-- Running upgrade 43956d550a44 -> 581a4264e537

CREATE TABLE extensions (
    id BIGINT NOT NULL AUTO_INCREMENT, 
    context VARCHAR(40) NOT NULL, 
    exten VARCHAR(40) NOT NULL, 
    priority INTEGER NOT NULL, 
    app VARCHAR(40) NOT NULL, 
    appdata VARCHAR(256) NOT NULL, 
    PRIMARY KEY (id), 
    UNIQUE (context, exten, priority), 
    UNIQUE (id)
);

UPDATE alembic_version SET version_num='581a4264e537' WHERE alembic_version.version_num = '43956d550a44';

-- Running upgrade 581a4264e537 -> 2fc7930b41b3

CREATE TABLE ps_systems (
    id VARCHAR(40) NOT NULL, 
    timer_t1 INTEGER, 
    timer_b INTEGER, 
    compact_headers ENUM('yes','no'), 
    threadpool_initial_size INTEGER, 
    threadpool_auto_increment INTEGER, 
    threadpool_idle_timeout INTEGER, 
    threadpool_max_size INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_systems_id ON ps_systems (id);

CREATE TABLE ps_globals (
    id VARCHAR(40) NOT NULL, 
    max_forwards INTEGER, 
    user_agent VARCHAR(40), 
    default_outbound_endpoint VARCHAR(40), 
    UNIQUE (id)
);

CREATE INDEX ps_globals_id ON ps_globals (id);

CREATE TABLE ps_transports (
    id VARCHAR(40) NOT NULL, 
    async_operations INTEGER, 
    bind VARCHAR(40), 
    ca_list_file VARCHAR(200), 
    cert_file VARCHAR(200), 
    cipher VARCHAR(200), 
    domain VARCHAR(40), 
    external_media_address VARCHAR(40), 
    external_signaling_address VARCHAR(40), 
    external_signaling_port INTEGER, 
    method ENUM('default','unspecified','tlsv1','sslv2','sslv3','sslv23'), 
    local_net VARCHAR(40), 
    password VARCHAR(40), 
    priv_key_file VARCHAR(200), 
    protocol ENUM('udp','tcp','tls','ws','wss'), 
    require_client_cert ENUM('yes','no'), 
    verify_client ENUM('yes','no'), 
    verifiy_server ENUM('yes','no'), 
    tos ENUM('yes','no'), 
    cos ENUM('yes','no'), 
    UNIQUE (id)
);

CREATE INDEX ps_transports_id ON ps_transports (id);

CREATE TABLE ps_registrations (
    id VARCHAR(40) NOT NULL, 
    auth_rejection_permanent ENUM('yes','no'), 
    client_uri VARCHAR(40), 
    contact_user VARCHAR(40), 
    expiration INTEGER, 
    max_retries INTEGER, 
    outbound_auth VARCHAR(40), 
    outbound_proxy VARCHAR(40), 
    retry_interval INTEGER, 
    forbidden_retry_interval INTEGER, 
    server_uri VARCHAR(40), 
    transport VARCHAR(40), 
    support_path ENUM('yes','no'), 
    UNIQUE (id)
);

CREATE INDEX ps_registrations_id ON ps_registrations (id);

ALTER TABLE ps_endpoints ADD COLUMN media_address VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN redirect_method ENUM('user','uri_core','uri_pjsip');

ALTER TABLE ps_endpoints ADD COLUMN set_var TEXT;

ALTER TABLE ps_endpoints CHANGE mwi_fromuser mwi_from_user VARCHAR(40) NULL;

ALTER TABLE ps_contacts ADD COLUMN outbound_proxy VARCHAR(40);

ALTER TABLE ps_contacts ADD COLUMN path TEXT;

ALTER TABLE ps_aors ADD COLUMN maximum_expiration INTEGER;

ALTER TABLE ps_aors ADD COLUMN outbound_proxy VARCHAR(40);

ALTER TABLE ps_aors ADD COLUMN support_path ENUM('yes','no');

UPDATE alembic_version SET version_num='2fc7930b41b3' WHERE alembic_version.version_num = '581a4264e537';

-- Running upgrade 2fc7930b41b3 -> 21e526ad3040

ALTER TABLE ps_globals ADD COLUMN debug VARCHAR(40);

UPDATE alembic_version SET version_num='21e526ad3040' WHERE alembic_version.version_num = '2fc7930b41b3';

-- Running upgrade 21e526ad3040 -> 28887f25a46f

CREATE TABLE queues (
    name VARCHAR(128) NOT NULL, 
    musiconhold VARCHAR(128), 
    announce VARCHAR(128), 
    context VARCHAR(128), 
    timeout INTEGER, 
    ringinuse ENUM('yes','no'), 
    setinterfacevar ENUM('yes','no'), 
    setqueuevar ENUM('yes','no'), 
    setqueueentryvar ENUM('yes','no'), 
    monitor_format VARCHAR(8), 
    membermacro VARCHAR(512), 
    membergosub VARCHAR(512), 
    queue_youarenext VARCHAR(128), 
    queue_thereare VARCHAR(128), 
    queue_callswaiting VARCHAR(128), 
    queue_quantity1 VARCHAR(128), 
    queue_quantity2 VARCHAR(128), 
    queue_holdtime VARCHAR(128), 
    queue_minutes VARCHAR(128), 
    queue_minute VARCHAR(128), 
    queue_seconds VARCHAR(128), 
    queue_thankyou VARCHAR(128), 
    queue_callerannounce VARCHAR(128), 
    queue_reporthold VARCHAR(128), 
    announce_frequency INTEGER, 
    announce_to_first_user ENUM('yes','no'), 
    min_announce_frequency INTEGER, 
    announce_round_seconds INTEGER, 
    announce_holdtime VARCHAR(128), 
    announce_position VARCHAR(128), 
    announce_position_limit INTEGER, 
    periodic_announce VARCHAR(50), 
    periodic_announce_frequency INTEGER, 
    relative_periodic_announce ENUM('yes','no'), 
    random_periodic_announce ENUM('yes','no'), 
    retry INTEGER, 
    wrapuptime INTEGER, 
    penaltymemberslimit INTEGER, 
    autofill ENUM('yes','no'), 
    monitor_type VARCHAR(128), 
    autopause ENUM('yes','no','all'), 
    autopausedelay INTEGER, 
    autopausebusy ENUM('yes','no'), 
    autopauseunavail ENUM('yes','no'), 
    maxlen INTEGER, 
    servicelevel INTEGER, 
    strategy ENUM('ringall','leastrecent','fewestcalls','random','rrmemory','linear','wrandom','rrordered'), 
    joinempty VARCHAR(128), 
    leavewhenempty VARCHAR(128), 
    reportholdtime ENUM('yes','no'), 
    memberdelay INTEGER, 
    weight INTEGER, 
    timeoutrestart ENUM('yes','no'), 
    defaultrule VARCHAR(128), 
    timeoutpriority VARCHAR(128), 
    PRIMARY KEY (name)
);

CREATE TABLE queue_members (
    queue_name VARCHAR(80) NOT NULL, 
    interface VARCHAR(80) NOT NULL, 
    uniqueid VARCHAR(80) NOT NULL, 
    membername VARCHAR(80), 
    state_interface VARCHAR(80), 
    penalty INTEGER, 
    paused INTEGER, 
    PRIMARY KEY (queue_name, interface)
);

UPDATE alembic_version SET version_num='28887f25a46f' WHERE alembic_version.version_num = '21e526ad3040';

-- Running upgrade 28887f25a46f -> 4c573e7135bd

ALTER TABLE ps_endpoints MODIFY tos_audio VARCHAR(10) NULL;

ALTER TABLE ps_endpoints MODIFY tos_video VARCHAR(10) NULL;

ALTER TABLE ps_endpoints DROP COLUMN cos_audio;

ALTER TABLE ps_endpoints DROP COLUMN cos_video;

ALTER TABLE ps_endpoints ADD COLUMN cos_audio INTEGER;

ALTER TABLE ps_endpoints ADD COLUMN cos_video INTEGER;

ALTER TABLE ps_transports MODIFY tos VARCHAR(10) NULL;

ALTER TABLE ps_transports DROP COLUMN cos;

ALTER TABLE ps_transports ADD COLUMN cos INTEGER;

UPDATE alembic_version SET version_num='4c573e7135bd' WHERE alembic_version.version_num = '28887f25a46f';

-- Running upgrade 4c573e7135bd -> 3855ee4e5f85

ALTER TABLE ps_endpoints ADD COLUMN message_context VARCHAR(40);

ALTER TABLE ps_contacts ADD COLUMN user_agent VARCHAR(40);

UPDATE alembic_version SET version_num='3855ee4e5f85' WHERE alembic_version.version_num = '4c573e7135bd';

-- Running upgrade 3855ee4e5f85 -> e96a0b8071c

ALTER TABLE ps_globals MODIFY user_agent VARCHAR(255) NULL;

ALTER TABLE ps_contacts MODIFY id VARCHAR(255) NULL;

ALTER TABLE ps_contacts MODIFY uri VARCHAR(255) NULL;

ALTER TABLE ps_contacts MODIFY user_agent VARCHAR(255) NULL;

ALTER TABLE ps_registrations MODIFY client_uri VARCHAR(255) NULL;

ALTER TABLE ps_registrations MODIFY server_uri VARCHAR(255) NULL;

UPDATE alembic_version SET version_num='e96a0b8071c' WHERE alembic_version.version_num = '3855ee4e5f85';

-- Running upgrade e96a0b8071c -> c6d929b23a8

CREATE TABLE ps_subscription_persistence (
    id VARCHAR(40) NOT NULL, 
    packet VARCHAR(2048), 
    src_name VARCHAR(128), 
    src_port INTEGER, 
    transport_key VARCHAR(64), 
    local_name VARCHAR(128), 
    local_port INTEGER, 
    cseq INTEGER, 
    tag VARCHAR(128), 
    endpoint VARCHAR(40), 
    expires INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_subscription_persistence_id ON ps_subscription_persistence (id);

UPDATE alembic_version SET version_num='c6d929b23a8' WHERE alembic_version.version_num = 'e96a0b8071c';

-- Running upgrade c6d929b23a8 -> 51f8cb66540e

ALTER TABLE ps_endpoints ADD COLUMN force_avp ENUM('yes','no');

ALTER TABLE ps_endpoints ADD COLUMN media_use_received_transport ENUM('yes','no');

UPDATE alembic_version SET version_num='51f8cb66540e' WHERE alembic_version.version_num = 'c6d929b23a8';

-- Running upgrade 51f8cb66540e -> 1d50859ed02e

ALTER TABLE ps_endpoints ADD COLUMN accountcode VARCHAR(20);

UPDATE alembic_version SET version_num='1d50859ed02e' WHERE alembic_version.version_num = '51f8cb66540e';

-- Running upgrade 1d50859ed02e -> 1758e8bbf6b

ALTER TABLE sippeers MODIFY useragent VARCHAR(255) NULL;

UPDATE alembic_version SET version_num='1758e8bbf6b' WHERE alembic_version.version_num = '1d50859ed02e';

-- Running upgrade 1758e8bbf6b -> 5139253c0423

ALTER TABLE queue_members DROP COLUMN uniqueid;

ALTER TABLE queue_members ADD COLUMN uniqueid INTEGER NOT NULL;

ALTER TABLE queue_members ADD UNIQUE (uniqueid);

ALTER TABLE queue_members MODIFY uniqueid INTEGER NOT NULL AUTO_INCREMENT;

UPDATE alembic_version SET version_num='5139253c0423' WHERE alembic_version.version_num = '1758e8bbf6b';

-- Running upgrade 5139253c0423 -> d39508cb8d8

CREATE TABLE queue_rules (
    rule_name VARCHAR(80) NOT NULL, 
    time VARCHAR(32) NOT NULL, 
    min_penalty VARCHAR(32) NOT NULL, 
    max_penalty VARCHAR(32) NOT NULL
);

UPDATE alembic_version SET version_num='d39508cb8d8' WHERE alembic_version.version_num = '5139253c0423';

-- Running upgrade d39508cb8d8 -> 5950038a6ead

ALTER TABLE ps_transports CHANGE verifiy_server verify_server ENUM('yes','no') NULL;

UPDATE alembic_version SET version_num='5950038a6ead' WHERE alembic_version.version_num = 'd39508cb8d8';

-- Running upgrade 5950038a6ead -> 10aedae86a32

ALTER TABLE sippeers MODIFY directmedia ENUM('yes','no','nonat','update','outgoing') NULL;

UPDATE alembic_version SET version_num='10aedae86a32' WHERE alembic_version.version_num = '5950038a6ead';

-- Running upgrade 10aedae86a32 -> 371a3bf4143e

ALTER TABLE ps_endpoints ADD COLUMN user_eq_phone ENUM('yes','no');

UPDATE alembic_version SET version_num='371a3bf4143e' WHERE alembic_version.version_num = '10aedae86a32';

-- Running upgrade 371a3bf4143e -> 15b1430ad6f1

ALTER TABLE ps_endpoints ADD COLUMN moh_passthrough ENUM('yes','no');

UPDATE alembic_version SET version_num='15b1430ad6f1' WHERE alembic_version.version_num = '371a3bf4143e';

-- Running upgrade 15b1430ad6f1 -> 945b1098bdd

ALTER TABLE ps_endpoints ADD COLUMN media_encryption_optimistic ENUM('yes','no');

UPDATE alembic_version SET version_num='945b1098bdd' WHERE alembic_version.version_num = '15b1430ad6f1';

-- Running upgrade 945b1098bdd -> 45e3f47c6c44

ALTER TABLE ps_globals ADD COLUMN endpoint_identifier_order VARCHAR(40);

UPDATE alembic_version SET version_num='45e3f47c6c44' WHERE alembic_version.version_num = '945b1098bdd';

-- Running upgrade 45e3f47c6c44 -> 23530d604b96

ALTER TABLE ps_endpoints ADD COLUMN rpid_immediate ENUM('yes','no');

UPDATE alembic_version SET version_num='23530d604b96' WHERE alembic_version.version_num = '45e3f47c6c44';

-- Running upgrade 23530d604b96 -> 31cd4f4891ec

ALTER TABLE ps_endpoints MODIFY dtmf_mode ENUM('rfc4733','inband','info','auto') NULL;

UPDATE alembic_version SET version_num='31cd4f4891ec' WHERE alembic_version.version_num = '23530d604b96';

-- Running upgrade 31cd4f4891ec -> 461d7d691209

ALTER TABLE ps_aors ADD COLUMN qualify_timeout INTEGER;

ALTER TABLE ps_contacts ADD COLUMN qualify_timeout INTEGER;

UPDATE alembic_version SET version_num='461d7d691209' WHERE alembic_version.version_num = '31cd4f4891ec';

-- Running upgrade 461d7d691209 -> a541e0b5e89

ALTER TABLE ps_globals ADD COLUMN max_initial_qualify_time INTEGER;

UPDATE alembic_version SET version_num='a541e0b5e89' WHERE alembic_version.version_num = '461d7d691209';

-- Running upgrade a541e0b5e89 -> 28b8e71e541f

ALTER TABLE ps_endpoints ADD COLUMN g726_non_standard ENUM('yes','no');

UPDATE alembic_version SET version_num='28b8e71e541f' WHERE alembic_version.version_num = 'a541e0b5e89';

-- Running upgrade 28b8e71e541f -> 498357a710ae

ALTER TABLE ps_endpoints ADD COLUMN rtp_keepalive INTEGER;

UPDATE alembic_version SET version_num='498357a710ae' WHERE alembic_version.version_num = '28b8e71e541f';

-- Running upgrade 498357a710ae -> 26f10cadc157

ALTER TABLE ps_endpoints ADD COLUMN rtp_timeout INTEGER;

ALTER TABLE ps_endpoints ADD COLUMN rtp_timeout_hold INTEGER;

UPDATE alembic_version SET version_num='26f10cadc157' WHERE alembic_version.version_num = '498357a710ae';

-- Running upgrade 26f10cadc157 -> 154177371065

ALTER TABLE ps_globals ADD COLUMN default_from_user VARCHAR(80);

UPDATE alembic_version SET version_num='154177371065' WHERE alembic_version.version_num = '26f10cadc157';

-- Running upgrade 154177371065 -> 28ce1e718f05

ALTER TABLE ps_registrations ADD COLUMN fatal_retry_interval INTEGER;

UPDATE alembic_version SET version_num='28ce1e718f05' WHERE alembic_version.version_num = '154177371065';

-- Running upgrade 28ce1e718f05 -> 339a3bdf53fc

ALTER TABLE ps_endpoints MODIFY accountcode VARCHAR(80) NULL;

ALTER TABLE sippeers MODIFY accountcode VARCHAR(80) NULL;

ALTER TABLE iaxfriends MODIFY accountcode VARCHAR(80) NULL;

UPDATE alembic_version SET version_num='339a3bdf53fc' WHERE alembic_version.version_num = '28ce1e718f05';

-- Running upgrade 339a3bdf53fc -> 189a235b3fd7

ALTER TABLE ps_globals ADD COLUMN keep_alive_interval INTEGER;

UPDATE alembic_version SET version_num='189a235b3fd7' WHERE alembic_version.version_num = '339a3bdf53fc';

-- Running upgrade 189a235b3fd7 -> 2d078ec071b7

ALTER TABLE ps_aors MODIFY contact VARCHAR(255) NULL;

UPDATE alembic_version SET version_num='2d078ec071b7' WHERE alembic_version.version_num = '189a235b3fd7';

-- Running upgrade 2d078ec071b7 -> 26d7f3bf0fa5

ALTER TABLE ps_endpoints ADD COLUMN bind_rtp_to_media_address ENUM('yes','no');

UPDATE alembic_version SET version_num='26d7f3bf0fa5' WHERE alembic_version.version_num = '2d078ec071b7';

-- Running upgrade 26d7f3bf0fa5 -> 136885b81223

ALTER TABLE ps_globals ADD COLUMN regcontext VARCHAR(80);

UPDATE alembic_version SET version_num='136885b81223' WHERE alembic_version.version_num = '26d7f3bf0fa5';

-- Running upgrade 136885b81223 -> 423f34ad36e2

ALTER TABLE ps_aors MODIFY qualify_timeout FLOAT NULL;

ALTER TABLE ps_contacts MODIFY qualify_timeout FLOAT NULL;

UPDATE alembic_version SET version_num='423f34ad36e2' WHERE alembic_version.version_num = '136885b81223';

-- Running upgrade 423f34ad36e2 -> dbc44d5a908

ALTER TABLE ps_systems ADD COLUMN disable_tcp_switch ENUM('yes','no');

ALTER TABLE ps_registrations ADD COLUMN line ENUM('yes','no');

ALTER TABLE ps_registrations ADD COLUMN endpoint VARCHAR(40);

UPDATE alembic_version SET version_num='dbc44d5a908' WHERE alembic_version.version_num = '423f34ad36e2';

-- Running upgrade dbc44d5a908 -> 3bcc0b5bc2c9

ALTER TABLE ps_transports ADD COLUMN allow_reload ENUM('yes','no');

UPDATE alembic_version SET version_num='3bcc0b5bc2c9' WHERE alembic_version.version_num = 'dbc44d5a908';

-- Running upgrade 3bcc0b5bc2c9 -> 5813202e92be

ALTER TABLE ps_globals ADD COLUMN contact_expiration_check_interval INTEGER;

UPDATE alembic_version SET version_num='5813202e92be' WHERE alembic_version.version_num = '3bcc0b5bc2c9';

-- Running upgrade 5813202e92be -> 1c688d9a003c

ALTER TABLE ps_globals ADD COLUMN default_voicemail_extension VARCHAR(40);

ALTER TABLE ps_aors ADD COLUMN voicemail_extension VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN voicemail_extension VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN mwi_subscribe_replaces_unsolicited INTEGER;

UPDATE alembic_version SET version_num='1c688d9a003c' WHERE alembic_version.version_num = '5813202e92be';

-- Running upgrade 1c688d9a003c -> 8d478ab86e29

ALTER TABLE ps_globals ADD COLUMN disable_multi_domain ENUM('yes','no');

UPDATE alembic_version SET version_num='8d478ab86e29' WHERE alembic_version.version_num = '1c688d9a003c';

-- Running upgrade 8d478ab86e29 -> 65eb22eb195

ALTER TABLE ps_globals ADD COLUMN unidentified_request_count INTEGER;

ALTER TABLE ps_globals ADD COLUMN unidentified_request_period INTEGER;

ALTER TABLE ps_globals ADD COLUMN unidentified_request_prune_interval INTEGER;

ALTER TABLE ps_globals ADD COLUMN default_realm VARCHAR(40);

UPDATE alembic_version SET version_num='65eb22eb195' WHERE alembic_version.version_num = '8d478ab86e29';

-- Running upgrade 65eb22eb195 -> 81b01a191a46

ALTER TABLE ps_contacts ADD COLUMN reg_server VARCHAR(20);

ALTER TABLE ps_contacts ADD CONSTRAINT ps_contacts_uq UNIQUE (id, reg_server);

UPDATE alembic_version SET version_num='81b01a191a46' WHERE alembic_version.version_num = '65eb22eb195';

-- Running upgrade 81b01a191a46 -> 6be31516058d

ALTER TABLE ps_contacts ADD COLUMN authenticate_qualify ENUM('yes','no');

UPDATE alembic_version SET version_num='6be31516058d' WHERE alembic_version.version_num = '81b01a191a46';

-- Running upgrade 6be31516058d -> d7e3c73eb2bf

ALTER TABLE ps_endpoints ADD COLUMN deny VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN permit VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN acl VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN contact_deny VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN contact_permit VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN contact_acl VARCHAR(40);

UPDATE alembic_version SET version_num='d7e3c73eb2bf' WHERE alembic_version.version_num = '6be31516058d';

-- Running upgrade d7e3c73eb2bf -> a845e4d8ade8

ALTER TABLE ps_contacts ADD COLUMN via_addr VARCHAR(40);

ALTER TABLE ps_contacts ADD COLUMN via_port INTEGER;

ALTER TABLE ps_contacts ADD COLUMN call_id VARCHAR(255);

UPDATE alembic_version SET version_num='a845e4d8ade8' WHERE alembic_version.version_num = 'd7e3c73eb2bf';

-- Running upgrade a845e4d8ade8 -> ef7efc2d3964

ALTER TABLE ps_contacts ADD COLUMN endpoint VARCHAR(40);

ALTER TABLE ps_contacts MODIFY expiration_time BIGINT NULL;

CREATE INDEX ps_contacts_qualifyfreq_exp ON ps_contacts (qualify_frequency, expiration_time);

CREATE INDEX ps_aors_qualifyfreq_contact ON ps_aors (qualify_frequency, contact);

UPDATE alembic_version SET version_num='ef7efc2d3964' WHERE alembic_version.version_num = 'a845e4d8ade8';

-- Running upgrade ef7efc2d3964 -> 9deac0ae4717

ALTER TABLE ps_endpoints ADD COLUMN subscribe_context VARCHAR(40);

UPDATE alembic_version SET version_num='9deac0ae4717' WHERE alembic_version.version_num = 'ef7efc2d3964';

-- Running upgrade 9deac0ae4717 -> 4a6c67fa9b7a

ALTER TABLE ps_endpoints ADD COLUMN fax_detect_timeout INTEGER;

UPDATE alembic_version SET version_num='4a6c67fa9b7a' WHERE alembic_version.version_num = '9deac0ae4717';

-- Running upgrade 4a6c67fa9b7a -> c7a44a5a0851

ALTER TABLE ps_globals ADD COLUMN mwi_tps_queue_high INTEGER;

ALTER TABLE ps_globals ADD COLUMN mwi_tps_queue_low INTEGER;

ALTER TABLE ps_globals ADD COLUMN mwi_disable_initial_unsolicited ENUM('yes','no');

UPDATE alembic_version SET version_num='c7a44a5a0851' WHERE alembic_version.version_num = '4a6c67fa9b7a';

-- Running upgrade c7a44a5a0851 -> 3772f8f828da

ALTER TABLE ps_endpoints MODIFY identify_by ENUM('username','auth_username') NULL;

UPDATE alembic_version SET version_num='3772f8f828da' WHERE alembic_version.version_num = 'c7a44a5a0851';

-- Running upgrade 3772f8f828da -> 4e2493ef32e6

ALTER TABLE ps_endpoints ADD COLUMN contact_user VARCHAR(80);

UPDATE alembic_version SET version_num='4e2493ef32e6' WHERE alembic_version.version_num = '3772f8f828da';

-- Running upgrade 4e2493ef32e6 -> 7f3e21abe318

ALTER TABLE ps_endpoints ADD COLUMN preferred_codec_only ENUM('yes','no');

UPDATE alembic_version SET version_num='7f3e21abe318' WHERE alembic_version.version_num = '4e2493ef32e6';

-- Running upgrade 7f3e21abe318 -> a6ef36f1309

ALTER TABLE ps_globals ADD COLUMN ignore_uri_user_options ENUM('yes','no');

UPDATE alembic_version SET version_num='a6ef36f1309' WHERE alembic_version.version_num = '7f3e21abe318';

-- Running upgrade a6ef36f1309 -> 4468b4a91372

ALTER TABLE ps_endpoints ADD COLUMN asymmetric_rtp_codec ENUM('yes','no');

UPDATE alembic_version SET version_num='4468b4a91372' WHERE alembic_version.version_num = 'a6ef36f1309';

-- Running upgrade 4468b4a91372 -> 28ab27a7826d

ALTER TABLE ps_endpoint_id_ips ADD COLUMN srv_lookups ENUM('yes','no');

UPDATE alembic_version SET version_num='28ab27a7826d' WHERE alembic_version.version_num = '4468b4a91372';

-- Running upgrade 28ab27a7826d -> 465e70e8c337

ALTER TABLE ps_endpoint_id_ips ADD COLUMN match_header VARCHAR(255);

UPDATE alembic_version SET version_num='465e70e8c337' WHERE alembic_version.version_num = '28ab27a7826d';

-- Running upgrade 465e70e8c337 -> 15db7b91a97a

ALTER TABLE ps_endpoints ADD COLUMN rtcp_mux ENUM('yes','no');

UPDATE alembic_version SET version_num='15db7b91a97a' WHERE alembic_version.version_num = '465e70e8c337';

-- Running upgrade 15db7b91a97a -> f638dbe2eb23

ALTER TABLE ps_transports ADD COLUMN symmetric_transport ENUM('yes','no');

ALTER TABLE ps_subscription_persistence ADD COLUMN contact_uri VARCHAR(256);

UPDATE alembic_version SET version_num='f638dbe2eb23' WHERE alembic_version.version_num = '15db7b91a97a';

-- Running upgrade f638dbe2eb23 -> 8fce4c573e15

ALTER TABLE ps_endpoints ADD COLUMN allow_overlap ENUM('yes','no');

UPDATE alembic_version SET version_num='8fce4c573e15' WHERE alembic_version.version_num = 'f638dbe2eb23';

-- Running upgrade 8fce4c573e15 -> 2da192dbbc65

CREATE TABLE ps_outbound_publishes (
    id VARCHAR(40) NOT NULL, 
    expiration INTEGER, 
    outbound_auth VARCHAR(40), 
    outbound_proxy VARCHAR(256), 
    server_uri VARCHAR(256), 
    from_uri VARCHAR(256), 
    to_uri VARCHAR(256), 
    event VARCHAR(40), 
    max_auth_attempts INTEGER, 
    transport VARCHAR(40), 
    multi_user ENUM('yes','no'), 
    `@body` VARCHAR(40), 
    `@context` VARCHAR(256), 
    `@exten` VARCHAR(256), 
    UNIQUE (id)
);

CREATE INDEX ps_outbound_publishes_id ON ps_outbound_publishes (id);

CREATE TABLE ps_inbound_publications (
    id VARCHAR(40) NOT NULL, 
    endpoint VARCHAR(40), 
    `event_asterisk-devicestate` VARCHAR(40), 
    `event_asterisk-mwi` VARCHAR(40), 
    UNIQUE (id)
);

CREATE INDEX ps_inbound_publications_id ON ps_inbound_publications (id);

CREATE TABLE ps_asterisk_publications (
    id VARCHAR(40) NOT NULL, 
    devicestate_publish VARCHAR(40), 
    mailboxstate_publish VARCHAR(40), 
    device_state ENUM('yes','no'), 
    device_state_filter VARCHAR(256), 
    mailbox_state ENUM('yes','no'), 
    mailbox_state_filter VARCHAR(256), 
    UNIQUE (id)
);

CREATE INDEX ps_asterisk_publications_id ON ps_asterisk_publications (id);

UPDATE alembic_version SET version_num='2da192dbbc65' WHERE alembic_version.version_num = '8fce4c573e15';

-- Running upgrade 2da192dbbc65 -> 1d0e332c32af

CREATE TABLE ps_resource_list (
    id VARCHAR(40) NOT NULL, 
    list_item VARCHAR(2048), 
    event VARCHAR(40), 
    full_state ENUM('yes','no'), 
    notification_batch_interval INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_resource_list_id ON ps_resource_list (id);

UPDATE alembic_version SET version_num='1d0e332c32af' WHERE alembic_version.version_num = '2da192dbbc65';

-- Running upgrade 1d0e332c32af -> 86bb1efa278d

ALTER TABLE ps_endpoints ADD COLUMN refer_blind_progress ENUM('yes','no');

UPDATE alembic_version SET version_num='86bb1efa278d' WHERE alembic_version.version_num = '1d0e332c32af';

-- Running upgrade 86bb1efa278d -> d7983954dd96

ALTER TABLE ps_endpoints ADD COLUMN notify_early_inuse_ringing ENUM('yes','no');

UPDATE alembic_version SET version_num='d7983954dd96' WHERE alembic_version.version_num = '86bb1efa278d';

-- Running upgrade d7983954dd96 -> 39959b9c2566

ALTER TABLE ps_endpoints ADD COLUMN max_audio_streams INTEGER;

ALTER TABLE ps_endpoints ADD COLUMN max_video_streams INTEGER;

UPDATE alembic_version SET version_num='39959b9c2566' WHERE alembic_version.version_num = 'd7983954dd96';

-- Running upgrade 39959b9c2566 -> 164abbd708c

ALTER TABLE ps_endpoints MODIFY dtmf_mode ENUM('rfc4733','inband','info','auto','auto_info') NULL;

UPDATE alembic_version SET version_num='164abbd708c' WHERE alembic_version.version_num = '39959b9c2566';

-- Running upgrade 164abbd708c -> 44ccced114ce

ALTER TABLE ps_endpoints ADD COLUMN webrtc ENUM('yes','no');

UPDATE alembic_version SET version_num='44ccced114ce' WHERE alembic_version.version_num = '164abbd708c';

-- Running upgrade 44ccced114ce -> f3d1c5d38b56

ALTER TABLE ps_contacts ADD COLUMN prune_on_boot ENUM('yes','no');

UPDATE alembic_version SET version_num='f3d1c5d38b56' WHERE alembic_version.version_num = '44ccced114ce';

-- Running upgrade f3d1c5d38b56 -> b83645976fdd

ALTER TABLE ps_endpoints ADD COLUMN dtls_fingerprint ENUM('SHA-1','SHA-256');

UPDATE alembic_version SET version_num='b83645976fdd' WHERE alembic_version.version_num = 'f3d1c5d38b56';

-- Running upgrade b83645976fdd -> a1698e8bb9c5

ALTER TABLE ps_endpoints ADD COLUMN incoming_mwi_mailbox VARCHAR(40);

UPDATE alembic_version SET version_num='a1698e8bb9c5' WHERE alembic_version.version_num = 'b83645976fdd';

-- Running upgrade a1698e8bb9c5 -> 20abce6d1e3c

ALTER TABLE ps_endpoints MODIFY identify_by ENUM('username','auth_username','ip') NULL;

UPDATE alembic_version SET version_num='20abce6d1e3c' WHERE alembic_version.version_num = 'a1698e8bb9c5';

-- Running upgrade 20abce6d1e3c -> de83fac997e2

ALTER TABLE ps_endpoints ADD COLUMN bundle ENUM('yes','no');

UPDATE alembic_version SET version_num='de83fac997e2' WHERE alembic_version.version_num = '20abce6d1e3c';

-- Running upgrade de83fac997e2 -> 041c0d3d1857

ALTER TABLE ps_endpoints ADD COLUMN dtls_auto_generate_cert ENUM('yes','no');

UPDATE alembic_version SET version_num='041c0d3d1857' WHERE alembic_version.version_num = 'de83fac997e2';

-- Running upgrade 041c0d3d1857 -> e2f04d309071

ALTER TABLE queue_members ADD COLUMN wrapuptime INTEGER;

UPDATE alembic_version SET version_num='e2f04d309071' WHERE alembic_version.version_num = '041c0d3d1857';

-- Running upgrade e2f04d309071 -> 52798ad97bdf

ALTER TABLE ps_endpoints MODIFY identify_by VARCHAR(80) NULL;

UPDATE alembic_version SET version_num='52798ad97bdf' WHERE alembic_version.version_num = 'e2f04d309071';

-- Running upgrade 52798ad97bdf -> d3e4284f8707

ALTER TABLE ps_subscription_persistence ADD COLUMN prune_on_boot ENUM('yes','no');

UPDATE alembic_version SET version_num='d3e4284f8707' WHERE alembic_version.version_num = '52798ad97bdf';

-- Running upgrade d3e4284f8707 -> 0be05c3a8225

ALTER TABLE ps_systems ADD COLUMN follow_early_media_fork ENUM('yes','no');

ALTER TABLE ps_systems ADD COLUMN accept_multiple_sdp_answers ENUM('yes','no');

ALTER TABLE ps_endpoints ADD COLUMN follow_early_media_fork ENUM('yes','no');

ALTER TABLE ps_endpoints ADD COLUMN accept_multiple_sdp_answers ENUM('yes','no');

UPDATE alembic_version SET version_num='0be05c3a8225' WHERE alembic_version.version_num = 'd3e4284f8707';

-- Running upgrade 0be05c3a8225 -> 19b00bc19b7b

ALTER TABLE ps_endpoints ADD COLUMN suppress_q850_reason_header ENUM('yes','no');

UPDATE alembic_version SET version_num='19b00bc19b7b' WHERE alembic_version.version_num = '0be05c3a8225';

-- Running upgrade 19b00bc19b7b -> 1d3ed26d9978

ALTER TABLE ps_contacts MODIFY uri VARCHAR(511) NULL;

UPDATE alembic_version SET version_num='1d3ed26d9978' WHERE alembic_version.version_num = '19b00bc19b7b';

-- Running upgrade 1d3ed26d9978 -> fe6592859b85

ALTER TABLE ps_endpoints MODIFY mwi_subscribe_replaces_unsolicited VARCHAR(5) NULL;

ALTER TABLE ps_endpoints MODIFY mwi_subscribe_replaces_unsolicited ENUM('0','1','off','on','false','true','no','yes') NULL;

UPDATE alembic_version SET version_num='fe6592859b85' WHERE alembic_version.version_num = '1d3ed26d9978';

-- Running upgrade fe6592859b85 -> 7f85dd44c775

ALTER TABLE ps_endpoints CHANGE suppress_q850_reason_header suppress_q850_reason_headers ENUM('yes','no') NULL;

UPDATE alembic_version SET version_num='7f85dd44c775' WHERE alembic_version.version_num = 'fe6592859b85';

-- Running upgrade 7f85dd44c775 -> 2bb1a85135ad

ALTER TABLE ps_globals ADD COLUMN use_callerid_contact ENUM('0','1','off','on','false','true','no','yes');

UPDATE alembic_version SET version_num='2bb1a85135ad' WHERE alembic_version.version_num = '7f85dd44c775';

-- Running upgrade 2bb1a85135ad -> 1ac563b350a8

ALTER TABLE ps_endpoints ADD COLUMN trust_connected_line ENUM('0','1','off','on','false','true','no','yes');

ALTER TABLE ps_endpoints ADD COLUMN send_connected_line ENUM('0','1','off','on','false','true','no','yes');

UPDATE alembic_version SET version_num='1ac563b350a8' WHERE alembic_version.version_num = '2bb1a85135ad';

-- Running upgrade 1ac563b350a8 -> 0838f8db6a61

ALTER TABLE ps_globals ADD COLUMN send_contact_status_on_update_registration ENUM('0','1','off','on','false','true','no','yes');

UPDATE alembic_version SET version_num='0838f8db6a61' WHERE alembic_version.version_num = '1ac563b350a8';

-- Running upgrade 0838f8db6a61 -> f3c0b8695b66

ALTER TABLE ps_globals ADD COLUMN taskprocessor_overload_trigger ENUM('none','global','pjsip_only');

UPDATE alembic_version SET version_num='f3c0b8695b66' WHERE alembic_version.version_num = '0838f8db6a61';

-- Running upgrade f3c0b8695b66 -> 80473bad3c16

ALTER TABLE ps_endpoints ADD COLUMN ignore_183_without_sdp ENUM('0','1','off','on','false','true','no','yes');

UPDATE alembic_version SET version_num='80473bad3c16' WHERE alembic_version.version_num = 'f3c0b8695b66';

-- Running upgrade 80473bad3c16 -> 3a094a18e75b

ALTER TABLE ps_globals ADD COLUMN norefersub ENUM('0','1','off','on','false','true','no','yes');

UPDATE alembic_version SET version_num='3a094a18e75b' WHERE alembic_version.version_num = '80473bad3c16';

-- Running upgrade 3a094a18e75b -> fbb7766f17bc

CREATE TABLE musiconhold_entry (
    name VARCHAR(80) NOT NULL, 
    position INTEGER NOT NULL, 
    entry VARCHAR(1024) NOT NULL, 
    PRIMARY KEY (name, position)
);

ALTER TABLE musiconhold_entry ADD CONSTRAINT fk_musiconhold_entry_name_musiconhold FOREIGN KEY(name) REFERENCES musiconhold (name);

ALTER TABLE musiconhold MODIFY mode ENUM('custom','files','mp3nb','quietmp3nb','quietmp3','playlist') NULL;

UPDATE alembic_version SET version_num='fbb7766f17bc' WHERE alembic_version.version_num = '3a094a18e75b';

