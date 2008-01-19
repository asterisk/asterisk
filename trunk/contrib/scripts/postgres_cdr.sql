
/*
 * Id: postgres_cdr.sql,v 1.8.2.11 2003/10/10 11:15:43 pnixon Exp $
 *
 * --- Peter Nixon [ codemonkey@peternixon.net ]
 *
 * This is a PostgreSQL schema for doing CDR accounting with Asterisk
 *
 * The calls will automatically be logged as long as the module is loaded.
 *
 */


CREATE TABLE cdr (
        AcctId		BIGSERIAL PRIMARY KEY,
	calldate	TIMESTAMP with time zone NOT NULL DEFAULT now(),
	clid		VARCHAR(80) NOT NULL default '',
	src		VARCHAR(80) NOT NULL default '',
	dst		VARCHAR(80) NOT NULL default '',
	dcontext	VARCHAR(80) NOT NULL default '',
	channel		VARCHAR(80) NOT NULL default '',
	dstchannel	VARCHAR(80) NOT NULL default '',
	lastapp		VARCHAR(80) NOT NULL default '',
	lastdata	VARCHAR(80) NOT NULL default '',
	duration	INTEGER NOT NULL default '0',
	billsec		INTEGER NOT NULL default '0',
	disposition	VARCHAR(45) NOT NULL default '',
	amaflags	INTEGER NOT NULL default '0',
	accountcode	VARCHAR(20) NOT NULL default '',
	uniqueid	VARCHAR(32) NOT NULL default '',
	userfield	VARCHAR(255) NOT NULL default ''
);

