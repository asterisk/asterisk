BEGIN;

CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);

-- Running upgrade  -> a2e9769475e

CREATE TABLE voicemail_messages (
    dir VARCHAR(255) NOT NULL, 
    msgnum INTEGER NOT NULL, 
    context VARCHAR(80), 
    macrocontext VARCHAR(80), 
    callerid VARCHAR(80), 
    origtime INTEGER, 
    duration INTEGER, 
    recording BYTEA, 
    flag VARCHAR(30), 
    category VARCHAR(30), 
    mailboxuser VARCHAR(30), 
    mailboxcontext VARCHAR(30), 
    msg_id VARCHAR(40)
);

ALTER TABLE voicemail_messages ADD CONSTRAINT voicemail_messages_dir_msgnum PRIMARY KEY (dir, msgnum);

CREATE INDEX voicemail_messages_dir ON voicemail_messages (dir);

INSERT INTO alembic_version (version_num) VALUES ('a2e9769475e') RETURNING alembic_version.version_num;

-- Running upgrade a2e9769475e -> 39428242f7f5

ALTER TABLE voicemail_messages ALTER COLUMN recording TYPE BYTEA;

UPDATE alembic_version SET version_num='39428242f7f5' WHERE alembic_version.version_num = 'a2e9769475e';

-- Running upgrade 39428242f7f5 -> 1c55c341360f

ALTER TABLE voicemail_messages DROP COLUMN macrocontext;

UPDATE alembic_version SET version_num='1c55c341360f' WHERE alembic_version.version_num = '39428242f7f5';

-- Running upgrade 1c55c341360f -> 64fae6bbe7fb

DROP INDEX voicemail_messages_dir;

UPDATE alembic_version SET version_num='64fae6bbe7fb' WHERE alembic_version.version_num = '1c55c341360f';

COMMIT;

