BEGIN TRANSACTION;

CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);

GO

-- Running upgrade  -> 210693f3123d

CREATE TABLE cdr (
    accountcode VARCHAR(20) NULL, 
    src VARCHAR(80) NULL, 
    dst VARCHAR(80) NULL, 
    dcontext VARCHAR(80) NULL, 
    clid VARCHAR(80) NULL, 
    channel VARCHAR(80) NULL, 
    dstchannel VARCHAR(80) NULL, 
    lastapp VARCHAR(80) NULL, 
    lastdata VARCHAR(80) NULL, 
    start DATETIME NULL, 
    answer DATETIME NULL, 
    [end] DATETIME NULL, 
    duration INTEGER NULL, 
    billsec INTEGER NULL, 
    disposition VARCHAR(45) NULL, 
    amaflags VARCHAR(45) NULL, 
    userfield VARCHAR(256) NULL, 
    uniqueid VARCHAR(150) NULL, 
    linkedid VARCHAR(150) NULL, 
    peeraccount VARCHAR(20) NULL, 
    sequence INTEGER NULL
);

GO

INSERT INTO alembic_version (version_num) VALUES ('210693f3123d');

GO

-- Running upgrade 210693f3123d -> 54cde9847798

ALTER TABLE cdr ALTER COLUMN accountcode VARCHAR(80);

GO

ALTER TABLE cdr ALTER COLUMN peeraccount VARCHAR(80);

GO

UPDATE alembic_version SET version_num='54cde9847798' WHERE alembic_version.version_num = '210693f3123d';

GO

COMMIT;

GO

