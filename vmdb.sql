drop table if exists users;
create table users (mailbox VARCHAR(80) NOT NULL PRIMARY KEY, context VARCHAR(80), password VARCHAR(80), fullname VARCHAR(80), email VARCHAR(80), pager VARCHAR(80), options VARCHAR(160));
