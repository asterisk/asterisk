CREATE TABLE alembic_version (
    version_num VARCHAR2(32 CHAR) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
)

/

-- Running upgrade  -> 210693f3123d

CREATE TABLE cdr (
    accountcode VARCHAR2(20 CHAR), 
    src VARCHAR2(80 CHAR), 
    dst VARCHAR2(80 CHAR), 
    dcontext VARCHAR2(80 CHAR), 
    clid VARCHAR2(80 CHAR), 
    channel VARCHAR2(80 CHAR), 
    dstchannel VARCHAR2(80 CHAR), 
    lastapp VARCHAR2(80 CHAR), 
    lastdata VARCHAR2(80 CHAR), 
    "start" DATE, 
    answer DATE, 
    end DATE, 
    duration INTEGER, 
    billsec INTEGER, 
    disposition VARCHAR2(45 CHAR), 
    amaflags VARCHAR2(45 CHAR), 
    userfield VARCHAR2(256 CHAR), 
    uniqueid VARCHAR2(150 CHAR), 
    linkedid VARCHAR2(150 CHAR), 
    peeraccount VARCHAR2(20 CHAR), 
    sequence INTEGER
)

/

INSERT INTO alembic_version (version_num) VALUES ('210693f3123d')

/

-- Running upgrade 210693f3123d -> 54cde9847798

ALTER TABLE cdr MODIFY accountcode VARCHAR2(80 CHAR)

/

ALTER TABLE cdr MODIFY peeraccount VARCHAR2(80 CHAR)

/

UPDATE alembic_version SET version_num='54cde9847798' WHERE alembic_version.version_num = '210693f3123d'

/

