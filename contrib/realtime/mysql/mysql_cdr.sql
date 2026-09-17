CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);

-- Running upgrade  -> 210693f3123d

CREATE TABLE cdr (
    accountcode VARCHAR(20), 
    src VARCHAR(80), 
    dst VARCHAR(80), 
    dcontext VARCHAR(80), 
    clid VARCHAR(80), 
    channel VARCHAR(80), 
    dstchannel VARCHAR(80), 
    lastapp VARCHAR(80), 
    lastdata VARCHAR(80), 
    start DATETIME, 
    answer DATETIME, 
    end DATETIME, 
    duration INTEGER, 
    billsec INTEGER, 
    disposition VARCHAR(45), 
    amaflags VARCHAR(45), 
    userfield VARCHAR(256), 
    uniqueid VARCHAR(150), 
    linkedid VARCHAR(150), 
    peeraccount VARCHAR(20), 
    sequence INTEGER
);

INSERT INTO alembic_version (version_num) VALUES ('210693f3123d');

-- Running upgrade 210693f3123d -> 54cde9847798

ALTER TABLE cdr MODIFY accountcode VARCHAR(80) NULL;

ALTER TABLE cdr MODIFY peeraccount VARCHAR(80) NULL;

UPDATE alembic_version SET version_num='54cde9847798' WHERE alembic_version.version_num = '210693f3123d';

