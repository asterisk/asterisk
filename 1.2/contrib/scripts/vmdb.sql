drop table if exists users;
create table users (
context VARCHAR(80) NOT NULL,
mailbox VARCHAR(80) NOT NULL,
password VARCHAR(80) NOT NULL DEFAULT '',
fullname VARCHAR(80) NOT NULL DEFAULT '',
email VARCHAR(80) NOT NULL DEFAULT '',
pager VARCHAR(80) NOT NULL DEFAULT '',
options VARCHAR(160) NOT NULL DEFAULT '',
PRIMARY KEY (context, mailbox)
);
