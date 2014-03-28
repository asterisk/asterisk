BEGIN TRANSACTION;

CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL
);

GO

-- Running upgrade None -> a2e9769475e

CREATE TABLE voicemail_messages (
    dir VARCHAR(255) NOT NULL, 
    msgnum INTEGER NOT NULL, 
    context VARCHAR(80) NULL, 
    macrocontext VARCHAR(80) NULL, 
    callerid VARCHAR(80) NULL, 
    origtime INTEGER NULL, 
    duration INTEGER NULL, 
    recording IMAGE NULL, 
    flag VARCHAR(30) NULL, 
    category VARCHAR(30) NULL, 
    mailboxuser VARCHAR(30) NULL, 
    mailboxcontext VARCHAR(30) NULL, 
    msg_id VARCHAR(40) NULL
);

GO

ALTER TABLE voicemail_messages ADD CONSTRAINT voicemail_messages_dir_msgnum PRIMARY KEY (dir, msgnum);

GO

CREATE INDEX voicemail_messages_dir ON voicemail_messages (dir);

GO

INSERT INTO alembic_version (version_num) VALUES ('a2e9769475e');

GO

COMMIT;

