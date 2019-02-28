CREATE TABLE alembic_version (
    version_num VARCHAR2(32 CHAR) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
)

/

-- Running upgrade  -> a2e9769475e

CREATE TABLE voicemail_messages (
    dir VARCHAR2(255 CHAR) NOT NULL, 
    msgnum INTEGER NOT NULL, 
    context VARCHAR2(80 CHAR), 
    macrocontext VARCHAR2(80 CHAR), 
    callerid VARCHAR2(80 CHAR), 
    origtime INTEGER, 
    duration INTEGER, 
    recording BLOB, 
    flag VARCHAR2(30 CHAR), 
    category VARCHAR2(30 CHAR), 
    mailboxuser VARCHAR2(30 CHAR), 
    mailboxcontext VARCHAR2(30 CHAR), 
    msg_id VARCHAR2(40 CHAR)
)

/

ALTER TABLE voicemail_messages ADD CONSTRAINT voicemail_messages_dir_msgnum PRIMARY KEY (dir, msgnum)

/

CREATE INDEX voicemail_messages_dir ON voicemail_messages (dir)

/

INSERT INTO alembic_version (version_num) VALUES ('a2e9769475e')

/

-- Running upgrade a2e9769475e -> 39428242f7f5

ALTER TABLE voicemail_messages MODIFY recording BLOB

/

UPDATE alembic_version SET version_num='39428242f7f5' WHERE alembic_version.version_num = 'a2e9769475e'

/

