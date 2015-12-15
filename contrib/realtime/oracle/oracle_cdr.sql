SET TRANSACTION READ WRITE

/

CREATE TABLE alembic_version (
    version_num VARCHAR2(32 CHAR) NOT NULL
)

/

-- Running upgrade None -> 210693f3123d

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

COMMIT

/

