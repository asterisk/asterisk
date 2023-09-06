BEGIN;

CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);

-- Running upgrade  -> 4105ee839f58

CREATE TABLE queue_log (
    id BIGSERIAL NOT NULL, 
    time TIMESTAMP WITHOUT TIME ZONE, 
    callid VARCHAR(80), 
    queuename VARCHAR(256), 
    agent VARCHAR(80), 
    event VARCHAR(32), 
    data1 VARCHAR(100), 
    data2 VARCHAR(100), 
    data3 VARCHAR(100), 
    data4 VARCHAR(100), 
    data5 VARCHAR(100), 
    PRIMARY KEY (id), 
    UNIQUE (id)
);

INSERT INTO alembic_version (version_num) VALUES ('4105ee839f58');

COMMIT;

